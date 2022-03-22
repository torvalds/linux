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
module_param(enabled, bool, 0600);

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

/*
 * Sampling interval for the monitoring in microseconds.
 *
 * The sampling interval of DAMON for the cold memory monitoring.  Please refer
 * to the DAMON documentation for more detail.  5 ms by default.
 */
static unsigned long sample_interval __read_mostly = 5000;
module_param(sample_interval, ulong, 0600);

/*
 * Aggregation interval for the monitoring in microseconds.
 *
 * The aggregation interval of DAMON for the cold memory monitoring.  Please
 * refer to the DAMON documentation for more detail.  100 ms by default.
 */
static unsigned long aggr_interval __read_mostly = 100000;
module_param(aggr_interval, ulong, 0600);

/*
 * Minimum number of monitoring regions.
 *
 * The minimal number of monitoring regions of DAMON for the cold memory
 * monitoring.  This can be used to set lower-bound of the monitoring quality.
 * But, setting this too high could result in increased monitoring overhead.
 * Please refer to the DAMON documentation for more detail.  10 by default.
 */
static unsigned long min_nr_regions __read_mostly = 10;
module_param(min_nr_regions, ulong, 0600);

/*
 * Maximum number of monitoring regions.
 *
 * The maximum number of monitoring regions of DAMON for the cold memory
 * monitoring.  This can be used to set upper-bound of the monitoring overhead.
 * However, setting this too low could result in bad monitoring quality.
 * Please refer to the DAMON documentation for more detail.  1000 by default.
 */
static unsigned long max_nr_regions __read_mostly = 1000;
module_param(max_nr_regions, ulong, 0600);

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

struct damon_reclaim_ram_walk_arg {
	unsigned long start;
	unsigned long end;
};

static int walk_system_ram(struct resource *res, void *arg)
{
	struct damon_reclaim_ram_walk_arg *a = arg;

	if (a->end - a->start < res->end - res->start) {
		a->start = res->start;
		a->end = res->end;
	}
	return 0;
}

/*
 * Find biggest 'System RAM' resource and store its start and end address in
 * @start and @end, respectively.  If no System RAM is found, returns false.
 */
static bool get_monitoring_region(unsigned long *start, unsigned long *end)
{
	struct damon_reclaim_ram_walk_arg arg = {};

	walk_system_ram_res(0, ULONG_MAX, &arg, walk_system_ram);
	if (arg.end <= arg.start)
		return false;

	*start = arg.start;
	*end = arg.end;
	return true;
}

static struct damos *damon_reclaim_new_scheme(void)
{
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
	struct damos *scheme = damon_new_scheme(
			/* Find regions having PAGE_SIZE or larger size */
			PAGE_SIZE, ULONG_MAX,
			/* and not accessed at all */
			0, 0,
			/* for min_age or more micro-seconds, and */
			min_age / aggr_interval, UINT_MAX,
			/* page out those, as soon as found */
			DAMOS_PAGEOUT,
			/* under the quota. */
			&quota,
			/* (De)activate this according to the watermarks. */
			&wmarks);

	return scheme;
}

static int damon_reclaim_turn(bool on)
{
	struct damon_region *region;
	struct damos *scheme;
	int err;

	if (!on) {
		err = damon_stop(&ctx, 1);
		if (!err)
			kdamond_pid = -1;
		return err;
	}

	err = damon_set_attrs(ctx, sample_interval, aggr_interval, 0,
			min_nr_regions, max_nr_regions);
	if (err)
		return err;

	if (monitor_region_start > monitor_region_end)
		return -EINVAL;
	if (!monitor_region_start && !monitor_region_end &&
			!get_monitoring_region(&monitor_region_start,
				&monitor_region_end))
		return -EINVAL;
	/* DAMON will free this on its own when finish monitoring */
	region = damon_new_region(monitor_region_start, monitor_region_end);
	if (!region)
		return -ENOMEM;
	damon_add_region(region, target);

	/* Will be freed by 'damon_set_schemes()' below */
	scheme = damon_reclaim_new_scheme();
	if (!scheme) {
		err = -ENOMEM;
		goto free_region_out;
	}
	err = damon_set_schemes(ctx, &scheme, 1);
	if (err)
		goto free_scheme_out;

	err = damon_start(&ctx, 1);
	if (!err) {
		kdamond_pid = ctx->kdamond->pid;
		return 0;
	}

free_scheme_out:
	damon_destroy_scheme(scheme);
free_region_out:
	damon_destroy_region(region, target);
	return err;
}

#define ENABLE_CHECK_INTERVAL_MS	1000
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

	schedule_delayed_work(&damon_reclaim_timer,
			msecs_to_jiffies(ENABLE_CHECK_INTERVAL_MS));
}
static DECLARE_DELAYED_WORK(damon_reclaim_timer, damon_reclaim_timer_fn);

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
	return 0;
}

static int __init damon_reclaim_init(void)
{
	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;

	damon_pa_set_primitives(ctx);
	ctx->callback.after_aggregation = damon_reclaim_after_aggregation;

	target = damon_new_target();
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);

	schedule_delayed_work(&damon_reclaim_timer, 0);
	return 0;
}

module_init(damon_reclaim_init);
