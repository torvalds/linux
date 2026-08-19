// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON-based page reclamation
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#define pr_fmt(fmt) "damon-reclaim: " fmt

#include <linux/damon.h>
#include <linux/kstrtox.h>
#include <linux/module.h>

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
 * of parameters except ``enabled`` again.  Once the re-reading is done, this
 * parameter is set as ``N``.  If invalid parameters are found while the
 * re-reading, DAMON_RECLAIM will be disabled.
 */
static bool commit_inputs __read_mostly;

/*
 * Time threshold for cold memory regions identification in microseconds.
 *
 * If a memory region is not accessed for this or longer time, DAMON_RECLAIM
 * identifies the region as cold, and reclaims.  120 seconds by default.
 */
static unsigned long min_age __read_mostly = 120000000;
module_param(min_age, ulong, 0600);

static struct damos_quota damon_reclaim_quota = {
	/* use up to 10 ms time, reclaim up to 128 MiB per 1 sec by default */
	.ms = 10,
	.sz = 128 * 1024 * 1024,
	.reset_interval = 1000,
	/* Within the quota, page out older regions first. */
	.weight_sz = 0,
	.weight_nr_accesses = 0,
	.weight_age = 1
};
DEFINE_DAMON_MODULES_DAMOS_QUOTAS(damon_reclaim_quota);

/*
 * Desired level of memory pressure-stall time in microseconds.
 *
 * While keeping the caps that set by other quotas, DAMON_RECLAIM automatically
 * increases and decreases the effective level of the quota aiming this level of
 * memory pressure is incurred.  System-wide ``some`` memory PSI in microseconds
 * per quota reset interval (``quota_reset_interval_ms``) is collected and
 * compared to this value to see if the aim is satisfied.  Value zero means
 * disabling this auto-tuning feature.
 *
 * Disabled by default.
 */
static unsigned long quota_mem_pressure_us __read_mostly;
module_param(quota_mem_pressure_us, ulong, 0600);

/*
 * User-specifiable feedback for auto-tuning of the effective quota.
 *
 * While keeping the caps that set by other quotas, DAMON_RECLAIM automatically
 * increases and decreases the effective level of the quota aiming receiving this
 * feedback of value ``10,000`` from the user.  DAMON_RECLAIM assumes the feedback
 * value and the quota are positively proportional.  Value zero means disabling
 * this auto-tuning feature.
 *
 * Disabled by default.
 *
 */
static unsigned long quota_autotune_feedback __read_mostly;
module_param(quota_autotune_feedback, ulong, 0600);

/*
 * Auto-tune monitoring intervals.
 *
 * If this parameter is set as ``Y``, DAMON_RECLAIM automatically tunes DAMON's
 * sampling and aggregation intervals.  The auto-tuning aims to capture
 * meaningful amount of access events in each DAMON-snapshot, while keeping the
 * sampling intervals 5 milliseconds in minimum, and 10 seconds in maximum.
 * Setting this as ``N`` disables the auto-tuning.
 *
 * Disabled by default.
 */
static bool autotune_monitoring_intervals __read_mostly;
module_param(autotune_monitoring_intervals, bool, 0600);

static struct damos_watermarks damon_reclaim_wmarks = {
	.metric = DAMOS_WMARK_FREE_MEM_RATE,
	.interval = 5000000,	/* 5 seconds */
	.high = 500,		/* 50 percent */
	.mid = 400,		/* 40 percent */
	.low = 200,		/* 20 percent */
};
DEFINE_DAMON_MODULES_WMARKS_PARAMS(damon_reclaim_wmarks);

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
 * against.  By default, the system's entire physical memory is used as the
 * region.
 */
static unsigned long monitor_region_start __read_mostly;
module_param(monitor_region_start, ulong, 0600);

/*
 * End of the target memory region in physical address.
 *
 * The end physical address of memory region that DAMON_RECLAIM will do work
 * against.  By default, the system's entire physical memory is used as the
 * region.
 */
static unsigned long monitor_region_end __read_mostly;
module_param(monitor_region_end, ulong, 0600);

/*
 * Scale factor for DAMON_RECLAIM to ops address conversion.
 *
 * This parameter must not be set to 0.
 */
static unsigned long addr_unit __read_mostly = 1;

/*
 * Skip anonymous pages reclamation.
 *
 * If this parameter is set as ``Y``, DAMON_RECLAIM does not reclaim anonymous
 * pages.  By default, ``N``.
 */
static bool skip_anon __read_mostly;
module_param(skip_anon, bool, 0600);

static struct damos_stat damon_reclaim_stat;
DEFINE_DAMON_MODULES_DAMOS_STATS_PARAMS(damon_reclaim_stat,
		reclaim_tried_regions, reclaimed_regions, quota_exceeds);

static struct damon_ctx *ctx;
static struct damon_target *target;

static struct damos *damon_reclaim_new_scheme(unsigned long aggr_interval)
{
	struct damos_access_pattern pattern = {
		/* Find regions having PAGE_SIZE or larger size */
		.min_sz_region = PAGE_SIZE,
		.max_sz_region = ULONG_MAX,
		/* and not accessed at all */
		.min_nr_accesses = 0,
		.max_nr_accesses = 0,
		/* for min_age or more micro-seconds */
		.min_age_region = min_age / aggr_interval,
		.max_age_region = UINT_MAX,
	};

	return damon_new_scheme(
			&pattern,
			/* page out those, as soon as found */
			DAMOS_PAGEOUT,
			/* for each aggregation interval */
			0,
			/* under the quota. */
			&damon_reclaim_quota,
			/* (De)activate this according to the watermarks. */
			&damon_reclaim_wmarks,
			NUMA_NO_NODE);
}

static int damon_reclaim_apply_parameters(void)
{
	struct damon_ctx *param_ctx;
	struct damon_target *param_target;
	struct damon_attrs attrs;
	struct damos *scheme;
	struct damos_quota_goal *goal;
	struct damos_filter *filter;
	int err;

	err = damon_modules_new_paddr_ctx_target(&param_ctx, &param_target);
	if (err)
		return err;

	param_ctx->addr_unit = addr_unit;
	param_ctx->min_region_sz = max(DAMON_MIN_REGION_SZ / addr_unit, 1);

	if (!is_power_of_2(param_ctx->min_region_sz)) {
		err = -EINVAL;
		goto out;
	}

	if (!damon_reclaim_mon_attrs.aggr_interval) {
		err = -EINVAL;
		goto out;
	}

	attrs = damon_reclaim_mon_attrs;
	if (autotune_monitoring_intervals) {
		attrs.sample_interval = 5000;
		attrs.aggr_interval = 100000;
		attrs.intervals_goal.access_bp = 40;
		attrs.intervals_goal.aggrs = 3;
		attrs.intervals_goal.min_sample_us = 5000;
		attrs.intervals_goal.max_sample_us = 10 * 1000 * 1000;
	}
	err = damon_set_attrs(param_ctx, &attrs);
	if (err)
		goto out;

	err = -ENOMEM;
	scheme = damon_reclaim_new_scheme(attrs.aggr_interval);
	if (!scheme)
		goto out;
	damon_set_schemes(param_ctx, &scheme, 1);

	if (quota_mem_pressure_us) {
		goal = damos_new_quota_goal(DAMOS_QUOTA_SOME_MEM_PSI_US,
				quota_mem_pressure_us);
		if (!goal)
			goto out;
		damos_add_quota_goal(&scheme->quota, goal);
	}

	if (quota_autotune_feedback) {
		goal = damos_new_quota_goal(DAMOS_QUOTA_USER_INPUT, 10000);
		if (!goal)
			goto out;
		goal->current_value = quota_autotune_feedback;
		damos_add_quota_goal(&scheme->quota, goal);
	}

	if (skip_anon) {
		filter = damos_new_filter(DAMOS_FILTER_TYPE_ANON, true, false);
		if (!filter)
			goto out;
		damos_add_filter(scheme, filter);
	}

	err = damon_set_region_system_rams_default(param_target,
			&monitor_region_start, &monitor_region_end,
			param_ctx->addr_unit, param_ctx->min_region_sz);
	if (err)
		goto out;
	err = damon_commit_ctx(ctx, param_ctx);
out:
	damon_destroy_ctx(param_ctx);
	return err;
}

static int damon_reclaim_commit_inputs_fn(void *arg)
{
	return damon_reclaim_apply_parameters();
}

static int damon_reclaim_commit_inputs_store(const char *val,
					     const struct kernel_param *kp)
{
	bool commit_inputs_request;
	int err;
	struct damon_call_control control = {
		.fn = damon_reclaim_commit_inputs_fn,
	};

	if (!val) {
		commit_inputs_request = true;
	} else {
		err = kstrtobool(val, &commit_inputs_request);
		if (err)
			return err;
	}

	if (!commit_inputs_request)
		return 0;

	/*
	 * Skip damon_call() if ctx is not initialized to avoid
	 * NULL pointer dereference.
	 */
	if (!ctx)
		return -EINVAL;

	err = damon_call(ctx, &control);

	return err ? err : control.return_code;
}

static const struct kernel_param_ops commit_inputs_param_ops = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = damon_reclaim_commit_inputs_store,
	.get = param_get_bool,
};

module_param_cb(commit_inputs, &commit_inputs_param_ops, &commit_inputs, 0600);

static int damon_reclaim_damon_call_fn(void *arg)
{
	struct damon_ctx *c = arg;
	struct damos *s;

	/* update the stats parameter */
	damon_for_each_scheme(s, c)
		damon_reclaim_stat = s->stat;

	return 0;
}

static struct damon_call_control call_control = {
	.fn = damon_reclaim_damon_call_fn,
	.repeat = true,
};

static int damon_reclaim_turn(bool on)
{
	int err;

	if (!on)
		return damon_stop(&ctx, 1);

	err = damon_reclaim_apply_parameters();
	if (err)
		return err;

	err = damon_start(&ctx, 1, true);
	if (err)
		return err;
	return damon_call(ctx, &call_control);
}

static int damon_reclaim_addr_unit_store(const char *val,
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
	.set = damon_reclaim_addr_unit_store,
	.get = param_get_ulong,
};

module_param_cb(addr_unit, &addr_unit_param_ops, &addr_unit, 0600);
MODULE_PARM_DESC(addr_unit,
	"Scale factor for DAMON_RECLAIM to ops address conversion (default: 1)");

static bool damon_reclaim_enabled(void)
{
	if (!ctx)
		return false;
	return damon_is_running(ctx);
}

static int damon_reclaim_enabled_store(const char *val,
		const struct kernel_param *kp)
{
	int err;

	err = kstrtobool(val, &enabled);
	if (err)
		return err;

	if (damon_reclaim_enabled() == enabled)
		return 0;

	/* Called before init function.  The function will handle this. */
	if (!damon_initialized())
		return 0;

	/* damon_modules_new_paddr_ctx_target() in the init function failed. */
	if (!ctx)
		return -ENOMEM;

	return damon_reclaim_turn(enabled);
}

static int damon_reclaim_enabled_load(char *buffer,
		const struct kernel_param *kp)
{
	return sprintf(buffer, "%c\n", damon_reclaim_enabled() ? 'Y' : 'N');
}

static const struct kernel_param_ops enabled_param_ops = {
	.set = damon_reclaim_enabled_store,
	.get = damon_reclaim_enabled_load,
};

module_param_cb(enabled, &enabled_param_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled,
	"Enable or disable DAMON_RECLAIM (default: disabled)");

static int damon_reclaim_kdamond_pid_store(const char *val,
		const struct kernel_param *kp)
{
	/*
	 * kdamond_pid is read-only, but kernel command line could write it.
	 * Do nothing here.
	 */
	return 0;
}

static int damon_reclaim_kdamond_pid_load(char *buffer,
		const struct kernel_param *kp)
{
	int kdamond_pid = -1;

	if (ctx) {
		kdamond_pid = damon_kdamond_pid(ctx);
		if (kdamond_pid < 0)
			kdamond_pid = -1;
	}
	return sprintf(buffer, "%d\n", kdamond_pid);
}

static const struct kernel_param_ops kdamond_pid_param_ops = {
	.set = damon_reclaim_kdamond_pid_store,
	.get = damon_reclaim_kdamond_pid_load,
};

/*
 * PID of the DAMON thread
 *
 * If DAMON_RECLAIM is enabled, this becomes the PID of the worker thread.
 * Else, -1.
 */
module_param_cb(kdamond_pid, &kdamond_pid_param_ops, NULL, 0400);

static int __init damon_reclaim_init(void)
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
		err = damon_reclaim_turn(true);

out:
	if (err && enabled)
		enabled = false;
	return err;
}

module_init(damon_reclaim_init);
