// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON-based page reclamation
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-reclaim: " fmt

#include <linux/damon.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#include "modules-common.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "damon_reclaim."

/*
 * Enable or disable DAMON_RECLAIM.
 *
 * You can enable DAMON_RCLAIM by setting the value of this parameter as ``Y``.
 * Setting it as ``N`` disables DAMON_RECLAIM.  Note that DAMON_RECLAIM could
 * do no real monitoring and reclamation due to the watermarks-based activation
 * condition.  Refer to below descriptions for the watermarks parameter for
 * this.
 */
static bool enabled __read_mostly;

/*
 * Make DAMON_RECLAIM reads the input parameters again, except ``enabled``.
 *
 * Input parameters that updated while DAMON_RECLAIM is running are not applied
 * by default.  Once this parameter is set as ``Y``, DAMON_RECLAIM reads values
 * of parametrs except ``enabled`` again.  Once the re-reading is done, this
 * parameter is set as ``N``.  If invalid parameters are found while the
 * re-reading, DAMON_RECLAIM will be disabled.
 */
static bool commit_inputs __read_mostly;
module_param(commit_inputs, bool, 0600);

/*
 * Time threshold for cold memory regions identification in microseconds.
 *
 * If a memory region is not accessed for this or longer time, DAMON_RECLAIM
 * identifies the region as cold, and reclaims.  120 seconds by default.
 */
static unsigned long min_age __read_mostly = 120000000;
module_param(min_age, ulong, 0600);

/*
 * Limit of time for trying the reclamation in milliseconds.
 *
 * DAMON_RECLAIM tries to use only up to this time within a time window
 * (quota_reset_interval_ms) for trying reclamation of cold pages.  This can be
 * used for limiting CPU consumption of DAMON_RECLAIM.  If the value is zero,
 * the limit is disabled.
 *
 * 10 ms by default.
 */
static unsigned long quota_ms __read_mostly = 10;
module_param(quota_ms, ulong, 0600);

/*
 * Limit of size of memory for the reclamation in bytes.
 *
 * DAMON_RECLAIM charges amount of memory which it tried to reclaim within a
 * time window (quota_reset_interval_ms) and makes no more than this limit is
 * tried.  This can be used for limiting consumption of CPU and IO.  If this
 * value is zero, the limit is disabled.
 *
 * 128 MiB by default.
 */
static unsigned long quota_sz __read_mostly = 128 * 1024 * 1024;
module_param(quota_sz, ulong, 0600);

/*
 * The time/size quota charge reset interval in milliseconds.
 *
 * The charge reset interval for the quota of time (quota_ms) and size
 * (quota_sz).  That is, DAMON_RECLAIM does not try reclamation for more than
 * quota_ms milliseconds or quota_sz bytes within quota_reset_interval_ms
 * milliseconds.
 *
 * 1 second by default.
 */
static unsigned long quota_reset_interval_ms __read_mostly = 1000;
module_param(quota_reset_interval_ms, ulong, 0600);

/*
 * The watermarks check time interval in microseconds.
 *
 * Minimal time to wait before checking the watermarks, when DAMON_RECLAIM is
 * enabled but inactive due to its watermarks rule.  5 seconds by default.
 */
static unsigned long wmarks_interval __read_mostly = 5000000;
module_param(wmarks_interval, ulong, 0600);

/*
 * Free memory rate (per thousand) for the high watermark.
 *
 * If free memory of the system in bytes per thousand bytes is higher than
 * this, DAMON_RECLAIM becomes inactive, so it does nothing but periodically
 * checks the watermarks.  500 (50%) by default.
 */
static unsigned long wmarks_high __read_mostly = 500;
module_param(wmarks_high, ulong, 0600);

/*
 * Free memory rate (per thousand) for the middle watermark.
 *
 * If free memory of the system in bytes per thousand bytes is between this and
 * the low watermark, DAMON_RECLAIM becomes active, so starts the monitoring
 * and the reclaiming.  400 (40%) by default.
 */
static unsigned long wmarks_mid __read_mostly = 400;
module_param(wmarks_mid, ulong, 0600);

/*
 * Free memory rate (per thousand) for the low watermark.
 *
 * If free memory of the system in bytes per thousand bytes is lower than this,
 * DAMON_RECLAIM becomes inactive, so it does nothing but periodically checks
 * the watermarks.  In the case, the system falls back to the LRU-based page
 * granularity reclamation logic.  200 (20%) by default.
 */
static unsigned long wmarks_low __read_mostly = 200;
module_param(wmarks_low, ulong, 0600);

static struct damon_attrs damon_reclaim_mon_attrs = {
	.sample_interval = 5000,	/* 5 ms */
	.aggr_interval = 100000,	/* 100 ms */
	.ops_update_interval = 0,
	.min_nr_regions = 10,
	.max_nr_regions = 1000,
};
DEFINE_DAMON_MODULES_MON_ATTRS_PARAMS(damon_reclaim_mon_attrs);

/*
 * Start of the target memory region in physical address.
 *
 * The start physical address of memory region that DAMON_RECLAIM will do work
 * against.  By default, biggest System RAM is used as the region.
 */
static unsigned long monitor_region_start __read_mostly;
module_param(monitor_region_start, ulong, 0600);

/*
 * End of the target memory region in physical address.
 *
 * The end physical address of memory region that DAMON_RECLAIM will do work
 * against.  By default, biggest System RAM is used as the region.
 */
static unsigned long monitor_region_end __read_mostly;
module_param(monitor_region_end, ulong, 0600);

/*
 * PID of the DAMON thread
 *
 * If DAMON_RECLAIM is enabled, this becomes the PID of the worker thread.
 * Else, -1.
 */
static int kdamond_pid __read_mostly = -1;
module_param(kdamond_pid, int, 0400);

/*
 * Number of memory regions that tried to be reclaimed.
 */
static unsigned long nr_reclaim_tried_regions __read_mostly;
module_param(nr_reclaim_tried_regions, ulong, 0400);

/*
 * Total bytes of memory regions that tried to be reclaimed.
 */
static unsigned long bytes_reclaim_tried_regions __read_mostly;
module_param(bytes_reclaim_tried_regions, ulong, 0400);

/*
 * Number of memory regions that successfully be reclaimed.
 */
static unsigned long nr_reclaimed_regions __read_mostly;
module_param(nr_reclaimed_regions, ulong, 0400);

/*
 * Total bytes of memory regions that successfully be reclaimed.
 */
static unsigned long bytes_reclaimed_regions __read_mostly;
module_param(bytes_reclaimed_regions, ulong, 0400);

/*
 * Number of times that the time/space quota limits have exceeded
 */
static unsigned long nr_quota_exceeds __read_mostly;
module_param(nr_quota_exceeds, ulong, 0400);

static struct damon_ctx *ctx;
static struct damon_target *target;

static struct damos *damon_reclaim_new_scheme(void)
{
	struct damos_access_pattern pattern = {
		/* Find regions having PAGE_SIZE or larger size */
		.min_sz_region = PAGE_SIZE,
		.max_sz_region = ULONG_MAX,
		/* and not accessed at all */
		.min_nr_accesses = 0,
		.max_nr_accesses = 0,
		/* for min_age or more micro-seconds */
		.min_age_region = min_age /
			damon_reclaim_mon_attrs.aggr_interval,
		.max_age_region = UINT_MAX,
	};
	struct damos_watermarks wmarks = {
		.metric = DAMOS_WMARK_FREE_MEM_RATE,
		.interval = wmarks_interval,
		.high = wmarks_high,
		.mid = wmarks_mid,
		.low = wmarks_low,
	};
	struct damos_quota quota = {
		/*
		 * Do not try reclamation for more than quota_ms milliseconds
		 * or quota_sz bytes within quota_reset_interval_ms.
		 */
		.ms = quota_ms,
		.sz = quota_sz,
		.reset_interval = quota_reset_interval_ms,
		/* Within the quota, page out older regions first. */
		.weight_sz = 0,
		.weight_nr_accesses = 0,
		.weight_age = 1
	};

	return damon_new_scheme(
			&pattern,
			/* page out those, as soon as found */
			DAMOS_PAGEOUT,
			/* under the quota. */
			&quota,
			/* (De)activate this according to the watermarks. */
			&wmarks);
}

static int damon_reclaim_apply_parameters(void)
{
	struct damos *scheme;
	struct damon_addr_range addr_range;
	int err = 0;

	err = damon_set_attrs(ctx, &damon_reclaim_mon_attrs);
	if (err)
		return err;

	/* Will be freed by next 'damon_set_schemes()' below */
	scheme = damon_reclaim_new_scheme();
	if (!scheme)
		return -ENOMEM;
	err = damon_set_schemes(ctx, &scheme, 1);
	if (err)
		return err;

	if (monitor_region_start > monitor_region_end)
		return -EINVAL;
	if (!monitor_region_start && !monitor_region_end &&
	    !damon_find_biggest_system_ram(&monitor_region_start,
					   &monitor_region_end))
		return -EINVAL;
	addr_range.start = monitor_region_start;
	addr_range.end = monitor_region_end;
	return damon_set_regions(target, &addr_range, 1);
}

static int damon_reclaim_turn(bool on)
{
	int err;

	if (!on) {
		err = damon_stop(&ctx, 1);
		if (!err)
			kdamond_pid = -1;
		return err;
	}

	err = damon_reclaim_apply_parameters();
	if (err)
		return err;

	err = damon_start(&ctx, 1, true);
	if (err)
		return err;
	kdamond_pid = ctx->kdamond->pid;
	return 0;
}

static struct delayed_work damon_reclaim_timer;
static void damon_reclaim_timer_fn(struct work_struct *work)
{
	static bool last_enabled;
	bool now_enabled;

	now_enabled = enabled;
	if (last_enabled != now_enabled) {
		if (!damon_reclaim_turn(now_enabled))
			last_enabled = now_enabled;
		else
			enabled = last_enabled;
	}
}
static DECLARE_DELAYED_WORK(damon_reclaim_timer, damon_reclaim_timer_fn);

static bool damon_reclaim_initialized;

static int damon_reclaim_enabled_store(const char *val,
		const struct kernel_param *kp)
{
	int rc = param_set_bool(val, kp);

	if (rc < 0)
		return rc;

	/* system_wq might not initialized yet */
	if (!damon_reclaim_initialized)
		return rc;

	schedule_delayed_work(&damon_reclaim_timer, 0);
	return 0;
}

static const struct kernel_param_ops enabled_param_ops = {
	.set = damon_reclaim_enabled_store,
	.get = param_get_bool,
};

module_param_cb(enabled, &enabled_param_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled,
	"Enable or disable DAMON_RECLAIM (default: disabled)");

static int damon_reclaim_handle_commit_inputs(void)
{
	int err;

	if (!commit_inputs)
		return 0;

	err = damon_reclaim_apply_parameters();
	commit_inputs = false;
	return err;
}

static int damon_reclaim_after_aggregation(struct damon_ctx *c)
{
	struct damos *s;

	/* update the stats parameter */
	damon_for_each_scheme(s, c) {
		nr_reclaim_tried_regions = s->stat.nr_tried;
		bytes_reclaim_tried_regions = s->stat.sz_tried;
		nr_reclaimed_regions = s->stat.nr_applied;
		bytes_reclaimed_regions = s->stat.sz_applied;
		nr_quota_exceeds = s->stat.qt_exceeds;
	}

	return damon_reclaim_handle_commit_inputs();
}

static int damon_reclaim_after_wmarks_check(struct damon_ctx *c)
{
	return damon_reclaim_handle_commit_inputs();
}

static int __init damon_reclaim_init(void)
{
	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;

	if (damon_select_ops(ctx, DAMON_OPS_PADDR)) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}

	ctx->callback.after_wmarks_check = damon_reclaim_after_wmarks_check;
	ctx->callback.after_aggregation = damon_reclaim_after_aggregation;

	target = damon_new_target();
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);

	schedule_delayed_work(&damon_reclaim_timer, 0);

	damon_reclaim_initialized = true;
	return 0;
}

module_init(damon_reclaim_init);
