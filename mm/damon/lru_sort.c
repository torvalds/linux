// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON-based LRU-lists Sorting
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-lru-sort: " fmt

#include <linux/damon.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "damon_lru_sort."

/*
 * Enable or disable DAMON_LRU_SORT.
 *
 * You can enable DAMON_LRU_SORT by setting the value of this parameter as
 * ``Y``.  Setting it as ``N`` disables DAMON_LRU_SORT.  Note that
 * DAMON_LRU_SORT could do no real monitoring and LRU-lists sorting due to the
 * watermarks-based activation condition.  Refer to below descriptions for the
 * watermarks parameter for this.
 */
static bool enabled __read_mostly;

/*
 * Make DAMON_LRU_SORT reads the input parameters again, except ``enabled``.
 *
 * Input parameters that updated while DAMON_LRU_SORT is running are not
 * applied by default.  Once this parameter is set as ``Y``, DAMON_LRU_SORT
 * reads values of parametrs except ``enabled`` again.  Once the re-reading is
 * done, this parameter is set as ``N``.  If invalid parameters are found while
 * the re-reading, DAMON_LRU_SORT will be disabled.
 */
static bool commit_inputs __read_mostly;
module_param(commit_inputs, bool, 0600);

/*
 * Access frequency threshold for hot memory regions identification in permil.
 *
 * If a memory region is accessed in frequency of this or higher,
 * DAMON_LRU_SORT identifies the region as hot, and mark it as accessed on the
 * LRU list, so that it could not be reclaimed under memory pressure.  50% by
 * default.
 */
static unsigned long hot_thres_access_freq = 500;
module_param(hot_thres_access_freq, ulong, 0600);

/*
 * Time threshold for cold memory regions identification in microseconds.
 *
 * If a memory region is not accessed for this or longer time, DAMON_LRU_SORT
 * identifies the region as cold, and mark it as unaccessed on the LRU list, so
 * that it could be reclaimed first under memory pressure.  120 seconds by
 * default.
 */
static unsigned long cold_min_age __read_mostly = 120000000;
module_param(cold_min_age, ulong, 0600);

/*
 * Limit of time for trying the LRU lists sorting in milliseconds.
 *
 * DAMON_LRU_SORT tries to use only up to this time within a time window
 * (quota_reset_interval_ms) for trying LRU lists sorting.  This can be used
 * for limiting CPU consumption of DAMON_LRU_SORT.  If the value is zero, the
 * limit is disabled.
 *
 * 10 ms by default.
 */
static unsigned long quota_ms __read_mostly = 10;
module_param(quota_ms, ulong, 0600);

/*
 * The time quota charge reset interval in milliseconds.
 *
 * The charge reset interval for the quota of time (quota_ms).  That is,
 * DAMON_LRU_SORT does not try LRU-lists sorting for more than quota_ms
 * milliseconds or quota_sz bytes within quota_reset_interval_ms milliseconds.
 *
 * 1 second by default.
 */
static unsigned long quota_reset_interval_ms __read_mostly = 1000;
module_param(quota_reset_interval_ms, ulong, 0600);

/*
 * The watermarks check time interval in microseconds.
 *
 * Minimal time to wait before checking the watermarks, when DAMON_LRU_SORT is
 * enabled but inactive due to its watermarks rule.  5 seconds by default.
 */
static unsigned long wmarks_interval __read_mostly = 5000000;
module_param(wmarks_interval, ulong, 0600);

/*
 * Free memory rate (per thousand) for the high watermark.
 *
 * If free memory of the system in bytes per thousand bytes is higher than
 * this, DAMON_LRU_SORT becomes inactive, so it does nothing but periodically
 * checks the watermarks.  200 (20%) by default.
 */
static unsigned long wmarks_high __read_mostly = 200;
module_param(wmarks_high, ulong, 0600);

/*
 * Free memory rate (per thousand) for the middle watermark.
 *
 * If free memory of the system in bytes per thousand bytes is between this and
 * the low watermark, DAMON_LRU_SORT becomes active, so starts the monitoring
 * and the LRU-lists sorting.  150 (15%) by default.
 */
static unsigned long wmarks_mid __read_mostly = 150;
module_param(wmarks_mid, ulong, 0600);

/*
 * Free memory rate (per thousand) for the low watermark.
 *
 * If free memory of the system in bytes per thousand bytes is lower than this,
 * DAMON_LRU_SORT becomes inactive, so it does nothing but periodically checks
 * the watermarks.  50 (5%) by default.
 */
static unsigned long wmarks_low __read_mostly = 50;
module_param(wmarks_low, ulong, 0600);

/*
 * Sampling interval for the monitoring in microseconds.
 *
 * The sampling interval of DAMON for the hot/cold memory monitoring.  Please
 * refer to the DAMON documentation for more detail.  5 ms by default.
 */
static unsigned long sample_interval __read_mostly = 5000;
module_param(sample_interval, ulong, 0600);

/*
 * Aggregation interval for the monitoring in microseconds.
 *
 * The aggregation interval of DAMON for the hot/cold memory monitoring.
 * Please refer to the DAMON documentation for more detail.  100 ms by default.
 */
static unsigned long aggr_interval __read_mostly = 100000;
module_param(aggr_interval, ulong, 0600);

/*
 * Minimum number of monitoring regions.
 *
 * The minimal number of monitoring regions of DAMON for the hot/cold memory
 * monitoring.  This can be used to set lower-bound of the monitoring quality.
 * But, setting this too high could result in increased monitoring overhead.
 * Please refer to the DAMON documentation for more detail.  10 by default.
 */
static unsigned long min_nr_regions __read_mostly = 10;
module_param(min_nr_regions, ulong, 0600);

/*
 * Maximum number of monitoring regions.
 *
 * The maximum number of monitoring regions of DAMON for the hot/cold memory
 * monitoring.  This can be used to set upper-bound of the monitoring overhead.
 * However, setting this too low could result in bad monitoring quality.
 * Please refer to the DAMON documentation for more detail.  1000 by default.
 */
static unsigned long max_nr_regions __read_mostly = 1000;
module_param(max_nr_regions, ulong, 0600);

/*
 * Start of the target memory region in physical address.
 *
 * The start physical address of memory region that DAMON_LRU_SORT will do work
 * against.  By default, biggest System RAM is used as the region.
 */
static unsigned long monitor_region_start __read_mostly;
module_param(monitor_region_start, ulong, 0600);

/*
 * End of the target memory region in physical address.
 *
 * The end physical address of memory region that DAMON_LRU_SORT will do work
 * against.  By default, biggest System RAM is used as the region.
 */
static unsigned long monitor_region_end __read_mostly;
module_param(monitor_region_end, ulong, 0600);

/*
 * PID of the DAMON thread
 *
 * If DAMON_LRU_SORT is enabled, this becomes the PID of the worker thread.
 * Else, -1.
 */
static int kdamond_pid __read_mostly = -1;
module_param(kdamond_pid, int, 0400);

/*
 * Number of hot memory regions that tried to be LRU-sorted.
 */
static unsigned long nr_lru_sort_tried_hot_regions __read_mostly;
module_param(nr_lru_sort_tried_hot_regions, ulong, 0400);

/*
 * Total bytes of hot memory regions that tried to be LRU-sorted.
 */
static unsigned long bytes_lru_sort_tried_hot_regions __read_mostly;
module_param(bytes_lru_sort_tried_hot_regions, ulong, 0400);

/*
 * Number of hot memory regions that successfully be LRU-sorted.
 */
static unsigned long nr_lru_sorted_hot_regions __read_mostly;
module_param(nr_lru_sorted_hot_regions, ulong, 0400);

/*
 * Total bytes of hot memory regions that successfully be LRU-sorted.
 */
static unsigned long bytes_lru_sorted_hot_regions __read_mostly;
module_param(bytes_lru_sorted_hot_regions, ulong, 0400);

/*
 * Number of times that the time quota limit for hot regions have exceeded
 */
static unsigned long nr_hot_quota_exceeds __read_mostly;
module_param(nr_hot_quota_exceeds, ulong, 0400);

/*
 * Number of cold memory regions that tried to be LRU-sorted.
 */
static unsigned long nr_lru_sort_tried_cold_regions __read_mostly;
module_param(nr_lru_sort_tried_cold_regions, ulong, 0400);

/*
 * Total bytes of cold memory regions that tried to be LRU-sorted.
 */
static unsigned long bytes_lru_sort_tried_cold_regions __read_mostly;
module_param(bytes_lru_sort_tried_cold_regions, ulong, 0400);

/*
 * Number of cold memory regions that successfully be LRU-sorted.
 */
static unsigned long nr_lru_sorted_cold_regions __read_mostly;
module_param(nr_lru_sorted_cold_regions, ulong, 0400);

/*
 * Total bytes of cold memory regions that successfully be LRU-sorted.
 */
static unsigned long bytes_lru_sorted_cold_regions __read_mostly;
module_param(bytes_lru_sorted_cold_regions, ulong, 0400);

/*
 * Number of times that the time quota limit for cold regions have exceeded
 */
static unsigned long nr_cold_quota_exceeds __read_mostly;
module_param(nr_cold_quota_exceeds, ulong, 0400);

static struct damon_ctx *ctx;
static struct damon_target *target;

struct damon_lru_sort_ram_walk_arg {
	unsigned long start;
	unsigned long end;
};

static int walk_system_ram(struct resource *res, void *arg)
{
	struct damon_lru_sort_ram_walk_arg *a = arg;

	if (a->end - a->start < resource_size(res)) {
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
	struct damon_lru_sort_ram_walk_arg arg = {};

	walk_system_ram_res(0, ULONG_MAX, &arg, walk_system_ram);
	if (arg.end <= arg.start)
		return false;

	*start = arg.start;
	*end = arg.end;
	return true;
}

/* Create a DAMON-based operation scheme for hot memory regions */
static struct damos *damon_lru_sort_new_hot_scheme(unsigned int hot_thres)
{
	struct damos_access_pattern pattern = {
		/* Find regions having PAGE_SIZE or larger size */
		.min_sz_region = PAGE_SIZE,
		.max_sz_region = ULONG_MAX,
		/* and accessed for more than the threshold */
		.min_nr_accesses = hot_thres,
		.max_nr_accesses = UINT_MAX,
		/* no matter its age */
		.min_age_region = 0,
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
		 * Do not try LRU-lists sorting of hot pages for more than half
		 * of quota_ms milliseconds within quota_reset_interval_ms.
		 */
		.ms = quota_ms / 2,
		.sz = 0,
		.reset_interval = quota_reset_interval_ms,
		/* Within the quota, mark hotter regions accessed first. */
		.weight_sz = 0,
		.weight_nr_accesses = 1,
		.weight_age = 0,
	};

	return damon_new_scheme(
			&pattern,
			/* prioritize those on LRU lists, as soon as found */
			DAMOS_LRU_PRIO,
			/* under the quota. */
			&quota,
			/* (De)activate this according to the watermarks. */
			&wmarks);
}

/* Create a DAMON-based operation scheme for cold memory regions */
static struct damos *damon_lru_sort_new_cold_scheme(unsigned int cold_thres)
{
	struct damos_access_pattern pattern = {
		/* Find regions having PAGE_SIZE or larger size */
		.min_sz_region = PAGE_SIZE,
		.max_sz_region = ULONG_MAX,
		/* and not accessed at all */
		.min_nr_accesses = 0,
		.max_nr_accesses = 0,
		/* for min_age or more micro-seconds */
		.min_age_region = cold_thres,
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
		 * Do not try LRU-lists sorting of cold pages for more than
		 * half of quota_ms milliseconds within
		 * quota_reset_interval_ms.
		 */
		.ms = quota_ms / 2,
		.sz = 0,
		.reset_interval = quota_reset_interval_ms,
		/* Within the quota, mark colder regions not accessed first. */
		.weight_sz = 0,
		.weight_nr_accesses = 0,
		.weight_age = 1,
	};

	return damon_new_scheme(
			&pattern,
			/* mark those as not accessed, as soon as found */
			DAMOS_LRU_DEPRIO,
			/* under the quota. */
			&quota,
			/* (De)activate this according to the watermarks. */
			&wmarks);
}

static int damon_lru_sort_apply_parameters(void)
{
	struct damos *scheme, *next_scheme;
	struct damon_addr_range addr_range;
	unsigned int hot_thres, cold_thres;
	int err = 0;

	err = damon_set_attrs(ctx, sample_interval, aggr_interval, 0,
			min_nr_regions, max_nr_regions);
	if (err)
		return err;

	/* free previously set schemes */
	damon_for_each_scheme_safe(scheme, next_scheme, ctx)
		damon_destroy_scheme(scheme);

	/* aggr_interval / sample_interval is the maximum nr_accesses */
	hot_thres = aggr_interval / sample_interval * hot_thres_access_freq /
		1000;
	scheme = damon_lru_sort_new_hot_scheme(hot_thres);
	if (!scheme)
		return -ENOMEM;
	damon_add_scheme(ctx, scheme);

	cold_thres = cold_min_age / aggr_interval;
	scheme = damon_lru_sort_new_cold_scheme(cold_thres);
	if (!scheme)
		return -ENOMEM;
	damon_add_scheme(ctx, scheme);

	if (monitor_region_start > monitor_region_end)
		return -EINVAL;
	if (!monitor_region_start && !monitor_region_end &&
			!get_monitoring_region(&monitor_region_start,
				&monitor_region_end))
		return -EINVAL;
	addr_range.start = monitor_region_start;
	addr_range.end = monitor_region_end;
	return damon_set_regions(target, &addr_range, 1);
}

static int damon_lru_sort_turn(bool on)
{
	int err;

	if (!on) {
		err = damon_stop(&ctx, 1);
		if (!err)
			kdamond_pid = -1;
		return err;
	}

	err = damon_lru_sort_apply_parameters();
	if (err)
		return err;

	err = damon_start(&ctx, 1, true);
	if (err)
		return err;
	kdamond_pid = ctx->kdamond->pid;
	return 0;
}

static struct delayed_work damon_lru_sort_timer;
static void damon_lru_sort_timer_fn(struct work_struct *work)
{
	static bool last_enabled;
	bool now_enabled;

	now_enabled = enabled;
	if (last_enabled != now_enabled) {
		if (!damon_lru_sort_turn(now_enabled))
			last_enabled = now_enabled;
		else
			enabled = last_enabled;
	}
}
static DECLARE_DELAYED_WORK(damon_lru_sort_timer, damon_lru_sort_timer_fn);

static bool damon_lru_sort_initialized;

static int damon_lru_sort_enabled_store(const char *val,
		const struct kernel_param *kp)
{
	int rc = param_set_bool(val, kp);

	if (rc < 0)
		return rc;

	if (!damon_lru_sort_initialized)
		return rc;

	schedule_delayed_work(&damon_lru_sort_timer, 0);

	return 0;
}

static const struct kernel_param_ops enabled_param_ops = {
	.set = damon_lru_sort_enabled_store,
	.get = param_get_bool,
};

module_param_cb(enabled, &enabled_param_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled,
	"Enable or disable DAMON_LRU_SORT (default: disabled)");

static int damon_lru_sort_handle_commit_inputs(void)
{
	int err;

	if (!commit_inputs)
		return 0;

	err = damon_lru_sort_apply_parameters();
	commit_inputs = false;
	return err;
}

static int damon_lru_sort_after_aggregation(struct damon_ctx *c)
{
	struct damos *s;

	/* update the stats parameter */
	damon_for_each_scheme(s, c) {
		if (s->action == DAMOS_LRU_PRIO) {
			nr_lru_sort_tried_hot_regions = s->stat.nr_tried;
			bytes_lru_sort_tried_hot_regions = s->stat.sz_tried;
			nr_lru_sorted_hot_regions = s->stat.nr_applied;
			bytes_lru_sorted_hot_regions = s->stat.sz_applied;
			nr_hot_quota_exceeds = s->stat.qt_exceeds;
		} else if (s->action == DAMOS_LRU_DEPRIO) {
			nr_lru_sort_tried_cold_regions = s->stat.nr_tried;
			bytes_lru_sort_tried_cold_regions = s->stat.sz_tried;
			nr_lru_sorted_cold_regions = s->stat.nr_applied;
			bytes_lru_sorted_cold_regions = s->stat.sz_applied;
			nr_cold_quota_exceeds = s->stat.qt_exceeds;
		}
	}

	return damon_lru_sort_handle_commit_inputs();
}

static int damon_lru_sort_after_wmarks_check(struct damon_ctx *c)
{
	return damon_lru_sort_handle_commit_inputs();
}

static int __init damon_lru_sort_init(void)
{
	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;

	if (damon_select_ops(ctx, DAMON_OPS_PADDR)) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}

	ctx->callback.after_wmarks_check = damon_lru_sort_after_wmarks_check;
	ctx->callback.after_aggregation = damon_lru_sort_after_aggregation;

	target = damon_new_target();
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);

	schedule_delayed_work(&damon_lru_sort_timer, 0);

	damon_lru_sort_initialized = true;
	return 0;
}

module_init(damon_lru_sort_init);
