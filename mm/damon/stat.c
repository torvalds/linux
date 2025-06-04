// SPDX-License-Identifier: GPL-2.0
/*
 * Shows data access monitoring resutls in simple metrics.
 */

#define pr_fmt(fmt) "damon-stat: " fmt

#include <linux/damon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "damon_stat."

static int damon_stat_enabled_store(
		const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops enabled_param_ops = {
	.set = damon_stat_enabled_store,
	.get = param_get_bool,
};

static bool enabled __read_mostly = IS_ENABLED(
	CONFIG_DAMON_STAT_ENABLED_DEFAULT);
module_param_cb(enabled, &enabled_param_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled, "Enable of disable DAMON_STAT");

static unsigned long estimated_memory_bandwidth __read_mostly;
module_param(estimated_memory_bandwidth, ulong, 0400);
MODULE_PARM_DESC(estimated_memory_bandwidth,
		"Estimated memory bandwidth usage in bytes per second");

static struct damon_ctx *damon_stat_context;

static void damon_stat_set_estimated_memory_bandwidth(struct damon_ctx *c)
{
	struct damon_target *t;
	struct damon_region *r;
	unsigned long access_bytes = 0;

	damon_for_each_target(t, c) {
		damon_for_each_region(r, t)
			access_bytes += (r->ar.end - r->ar.start) *
				r->nr_accesses;
	}
	estimated_memory_bandwidth = access_bytes * USEC_PER_MSEC *
		MSEC_PER_SEC / c->attrs.aggr_interval;
}

static int damon_stat_after_aggregation(struct damon_ctx *c)
{
	static unsigned long last_refresh_jiffies;

	/* avoid unnecessarily frequent stat update */
	if (time_before_eq(jiffies, last_refresh_jiffies +
				msecs_to_jiffies(5 * MSEC_PER_SEC)))
		return 0;
	last_refresh_jiffies = jiffies;

	damon_stat_set_estimated_memory_bandwidth(c);
	return 0;
}

static struct damon_ctx *damon_stat_build_ctx(void)
{
	struct damon_ctx *ctx;
	struct damon_attrs attrs;
	struct damon_target *target;
	unsigned long start = 0, end = 0;

	ctx = damon_new_ctx();
	if (!ctx)
		return NULL;
	attrs = (struct damon_attrs) {
		.sample_interval = 5 * USEC_PER_MSEC,
		.aggr_interval = 100 * USEC_PER_MSEC,
		.ops_update_interval = 60 * USEC_PER_MSEC * MSEC_PER_SEC,
		.min_nr_regions = 10,
		.max_nr_regions = 1000,
	};
	/*
	 * auto-tune sampling and aggregation interval aiming 4% DAMON-observed
	 * accesses ratio, keeping sampling interval in [5ms, 10s] range.
	 */
	attrs.intervals_goal = (struct damon_intervals_goal) {
		.access_bp = 400, .aggrs = 3,
		.min_sample_us = 5000, .max_sample_us = 10000000,
	};
	if (damon_set_attrs(ctx, &attrs))
		goto free_out;

	/*
	 * auto-tune sampling and aggregation interval aiming 4% DAMON-observed
	 * accesses ratio, keeping sampling interval in [5ms, 10s] range.
	 */
	ctx->attrs.intervals_goal = (struct damon_intervals_goal) {
		.access_bp = 400, .aggrs = 3,
		.min_sample_us = 5000, .max_sample_us = 10000000,
	};
	if (damon_select_ops(ctx, DAMON_OPS_PADDR))
		goto free_out;

	target = damon_new_target();
	if (!target)
		goto free_out;
	damon_add_target(ctx, target);
	if (damon_set_region_biggest_system_ram_default(target, &start, &end))
		goto free_out;
	ctx->callback.after_aggregation = damon_stat_after_aggregation;
	return ctx;
free_out:
	damon_destroy_ctx(ctx);
	return NULL;
}

static int damon_stat_start(void)
{
	damon_stat_context = damon_stat_build_ctx();
	if (!damon_stat_context)
		return -ENOMEM;
	return damon_start(&damon_stat_context, 1, true);
}

static void damon_stat_stop(void)
{
	damon_stop(&damon_stat_context, 1);
	damon_destroy_ctx(damon_stat_context);
}

static bool damon_stat_init_called;

static int damon_stat_enabled_store(
		const char *val, const struct kernel_param *kp)
{
	bool is_enabled = enabled;
	int err;

	err = kstrtobool(val, &enabled);
	if (err)
		return err;

	if (is_enabled == enabled)
		return 0;

	if (!damon_stat_init_called)
		/*
		 * probably called from command line parsing (parse_args()).
		 * Cannot call damon_new_ctx().  Let damon_stat_init() handle.
		 */
		return 0;

	if (enabled)
		return damon_stat_start();
	damon_stat_stop();
	return 0;
}

static int __init damon_stat_init(void)
{
	int err = 0;

	damon_stat_init_called = true;

	/* probably set via command line */
	if (enabled)
		err = damon_stat_start();
	return err;
}

module_init(damon_stat_init);
