// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON-based LRU-lists Sorting
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-lru-sort: " fmt

#include <linux/damon.h>
#include <linux/kstrtox.h>
#include <linux/module.h>

#include "modules-common.h"

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

static struct damos_quota damon_lru_sort_quota = {
	/* Use up to 10 ms per 1 sec, by default */
	.ms = 10,
	.sz = 0,
	.reset_interval = 1000,
	/* Within the quota, mark hotter regions accessed first. */
	.weight_sz = 0,
	.weight_nr_accesses = 1,
	.weight_age = 0,
};
DEFINE_DAMON_MODULES_DAMOS_TIME_QUOTA(damon_lru_sort_quota);

static struct damos_watermarks damon_lru_sort_wmarks = {
	.metric = DAMOS_WMARK_FREE_MEM_RATE,
	.interval = 5000000,	/* 5 seconds */
	.high = 200,		/* 20 percent */
	.mid = 150,		/* 15 percent */
	.low = 50,		/* 5 percent */
};
DEFINE_DAMON_MODULES_WMARKS_PARAMS(damon_lru_sort_wmarks);

static struct damon_attrs damon_lru_sort_mon_attrs = {
	.sample_interval = 5000,	/* 5 ms */
	.aggr_interval = 100000,	/* 100 ms */
	.ops_update_interval = 0,
	.min_nr_regions = 10,
	.max_nr_regions = 1000,
};
DEFINE_DAMON_MODULES_MON_ATTRS_PARAMS(damon_lru_sort_mon_attrs);

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
 * Scale factor for DAMON_LRU_SORT to ops address conversion.
 *
 * This parameter must not be set to 0.
 */
static unsigned long addr_unit __read_mostly = 1;

/*
 * PID of the DAMON thread
 *
 * If DAMON_LRU_SORT is enabled, this becomes the PID of the worker thread.
 * Else, -1.
 */
static int kdamond_pid __read_mostly = -1;
module_param(kdamond_pid, int, 0400);

static struct damos_stat damon_lru_sort_hot_stat;
DEFINE_DAMON_MODULES_DAMOS_STATS_PARAMS(damon_lru_sort_hot_stat,
		lru_sort_tried_hot_regions, lru_sorted_hot_regions,
		hot_quota_exceeds);

static struct damos_stat damon_lru_sort_cold_stat;
DEFINE_DAMON_MODULES_DAMOS_STATS_PARAMS(damon_lru_sort_cold_stat,
		lru_sort_tried_cold_regions, lru_sorted_cold_regions,
		cold_quota_exceeds);

static struct damos_access_pattern damon_lru_sort_stub_pattern = {
	/* Find regions having PAGE_SIZE or larger size */
	.min_sz_region = PAGE_SIZE,
	.max_sz_region = ULONG_MAX,
	/* no matter its access frequency */
	.min_nr_accesses = 0,
	.max_nr_accesses = UINT_MAX,
	/* no matter its age */
	.min_age_region = 0,
	.max_age_region = UINT_MAX,
};

static struct damon_ctx *ctx;
static struct damon_target *target;

static struct damos *damon_lru_sort_new_scheme(
		struct damos_access_pattern *pattern, enum damos_action action)
{
	struct damos_quota quota = damon_lru_sort_quota;

	/* Use half of total quota for hot/cold pages sorting */
	quota.ms = quota.ms / 2;

	return damon_new_scheme(
			/* find the pattern, and */
			pattern,
			/* (de)prioritize on LRU-lists */
			action,
			/* for each aggregation interval */
			0,
			/* under the quota. */
			&quota,
			/* (De)activate this according to the watermarks. */
			&damon_lru_sort_wmarks,
			NUMA_NO_NODE);
}

/* Create a DAMON-based operation scheme for hot memory regions */
static struct damos *damon_lru_sort_new_hot_scheme(unsigned int hot_thres)
{
	struct damos_access_pattern pattern = damon_lru_sort_stub_pattern;

	pattern.min_nr_accesses = hot_thres;
	return damon_lru_sort_new_scheme(&pattern, DAMOS_LRU_PRIO);
}

/* Create a DAMON-based operation scheme for cold memory regions */
static struct damos *damon_lru_sort_new_cold_scheme(unsigned int cold_thres)
{
	struct damos_access_pattern pattern = damon_lru_sort_stub_pattern;

	pattern.max_nr_accesses = 0;
	pattern.min_age_region = cold_thres;
	return damon_lru_sort_new_scheme(&pattern, DAMOS_LRU_DEPRIO);
}

static int damon_lru_sort_apply_parameters(void)
{
	struct damon_ctx *param_ctx;
	struct damon_target *param_target;
	struct damos *hot_scheme, *cold_scheme;
	unsigned int hot_thres, cold_thres;
	int err;

	err = damon_modules_new_paddr_ctx_target(&param_ctx, &param_target);
	if (err)
		return err;

	/*
	 * If monitor_region_start/end are unset, always silently
	 * reset addr_unit to 1.
	 */
	if (!monitor_region_start && !monitor_region_end)
		addr_unit = 1;
	param_ctx->addr_unit = addr_unit;
	param_ctx->min_sz_region = max(DAMON_MIN_REGION / addr_unit, 1);

	if (!damon_lru_sort_mon_attrs.sample_interval) {
		err = -EINVAL;
		goto out;
	}

	err = damon_set_attrs(param_ctx, &damon_lru_sort_mon_attrs);
	if (err)
		goto out;

	err = -ENOMEM;
	hot_thres = damon_max_nr_accesses(&damon_lru_sort_mon_attrs) *
		hot_thres_access_freq / 1000;
	hot_scheme = damon_lru_sort_new_hot_scheme(hot_thres);
	if (!hot_scheme)
		goto out;

	cold_thres = cold_min_age / damon_lru_sort_mon_attrs.aggr_interval;
	cold_scheme = damon_lru_sort_new_cold_scheme(cold_thres);
	if (!cold_scheme) {
		damon_destroy_scheme(hot_scheme);
		goto out;
	}

	damon_set_schemes(param_ctx, &hot_scheme, 1);
	damon_add_scheme(param_ctx, cold_scheme);

	err = damon_set_region_biggest_system_ram_default(param_target,
					&monitor_region_start,
					&monitor_region_end);
	if (err)
		goto out;
	err = damon_commit_ctx(ctx, param_ctx);
out:
	damon_destroy_ctx(param_ctx);
	return err;
}

static int damon_lru_sort_handle_commit_inputs(void)
{
	int err;

	if (!commit_inputs)
		return 0;

	err = damon_lru_sort_apply_parameters();
	commit_inputs = false;
	return err;
}

static int damon_lru_sort_damon_call_fn(void *arg)
{
	struct damon_ctx *c = arg;
	struct damos *s;

	/* update the stats parameter */
	damon_for_each_scheme(s, c) {
		if (s->action == DAMOS_LRU_PRIO)
			damon_lru_sort_hot_stat = s->stat;
		else if (s->action == DAMOS_LRU_DEPRIO)
			damon_lru_sort_cold_stat = s->stat;
	}

	return damon_lru_sort_handle_commit_inputs();
}

static struct damon_call_control call_control = {
	.fn = damon_lru_sort_damon_call_fn,
	.repeat = true,
};

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
	return damon_call(ctx, &call_control);
}

static int damon_lru_sort_addr_unit_store(const char *val,
		const struct kernel_param *kp)
{
	unsigned long input_addr_unit;
	int err = kstrtoul(val, 0, &input_addr_unit);

	if (err)
		return err;
	if (!input_addr_unit)
		return -EINVAL;

	addr_unit = input_addr_unit;
	return 0;
}

static const struct kernel_param_ops addr_unit_param_ops = {
	.set = damon_lru_sort_addr_unit_store,
	.get = param_get_ulong,
};

module_param_cb(addr_unit, &addr_unit_param_ops, &addr_unit, 0600);
MODULE_PARM_DESC(addr_unit,
	"Scale factor for DAMON_LRU_SORT to ops address conversion (default: 1)");

static int damon_lru_sort_enabled_store(const char *val,
		const struct kernel_param *kp)
{
	bool is_enabled = enabled;
	bool enable;
	int err;

	err = kstrtobool(val, &enable);
	if (err)
		return err;

	if (is_enabled == enable)
		return 0;

	/* Called before init function.  The function will handle this. */
	if (!damon_initialized())
		goto set_param_out;

	err = damon_lru_sort_turn(enable);
	if (err)
		return err;

set_param_out:
	enabled = enable;
	return err;
}

static const struct kernel_param_ops enabled_param_ops = {
	.set = damon_lru_sort_enabled_store,
	.get = param_get_bool,
};

module_param_cb(enabled, &enabled_param_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled,
	"Enable or disable DAMON_LRU_SORT (default: disabled)");

static int __init damon_lru_sort_init(void)
{
	int err;

	if (!damon_initialized()) {
		err = -ENOMEM;
		goto out;
	}
	err = damon_modules_new_paddr_ctx_target(&ctx, &target);
	if (err)
		goto out;

	call_control.data = ctx;

	/* 'enabled' has set before this function, probably via command line */
	if (enabled)
		err = damon_lru_sort_turn(true);

out:
	if (err && enabled)
		enabled = false;
	return err;
}

module_init(damon_lru_sort_init);
