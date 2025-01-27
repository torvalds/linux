// SPDX-License-Identifier: GPL-2.0
/*
 * proactive reclamation: monitor access pattern of a given process, find
 * regiosn that seems not accessed, and proactively page out the regions.
 */

#define pr_fmt(fmt) "damon_sample_prcl: " fmt

#include <linux/damon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int target_pid __read_mostly;
module_param(target_pid, int, 0600);

static int damon_sample_prcl_enable_store(
		const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops enable_param_ops = {
	.set = damon_sample_prcl_enable_store,
	.get = param_get_bool,
};

static bool enable __read_mostly;
module_param_cb(enable, &enable_param_ops, &enable, 0600);
MODULE_PARM_DESC(enable, "Enable of disable DAMON_SAMPLE_WSSE");

static struct damon_ctx *ctx;
static struct pid *target_pidp;

static int damon_sample_prcl_after_aggregate(struct damon_ctx *c)
{
	struct damon_target *t;

	damon_for_each_target(t, c) {
		struct damon_region *r;
		unsigned long wss = 0;

		damon_for_each_region(r, t) {
			if (r->nr_accesses > 0)
				wss += r->ar.end - r->ar.start;
		}
		pr_info("wss: %lu\n", wss);
	}
	return 0;
}

static int damon_sample_prcl_start(void)
{
	struct damon_target *target;
	struct damos *scheme;

	pr_info("start\n");

	ctx = damon_new_ctx();
	if (!ctx)
		return -ENOMEM;
	if (damon_select_ops(ctx, DAMON_OPS_VADDR)) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}

	target = damon_new_target();
	if (!target) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_add_target(ctx, target);
	target_pidp = find_get_pid(target_pid);
	if (!target_pidp) {
		damon_destroy_ctx(ctx);
		return -EINVAL;
	}
	target->pid = target_pidp;

	ctx->callback.after_aggregation = damon_sample_prcl_after_aggregate;

	scheme = damon_new_scheme(
			&(struct damos_access_pattern) {
			.min_sz_region = PAGE_SIZE,
			.max_sz_region = ULONG_MAX,
			.min_nr_accesses = 0,
			.max_nr_accesses = 0,
			.min_age_region = 50,
			.max_age_region = UINT_MAX},
			DAMOS_PAGEOUT,
			0,
			&(struct damos_quota){},
			&(struct damos_watermarks){},
			NUMA_NO_NODE);
	if (!scheme) {
		damon_destroy_ctx(ctx);
		return -ENOMEM;
	}
	damon_set_schemes(ctx, &scheme, 1);

	return damon_start(&ctx, 1, true);
}

static void damon_sample_prcl_stop(void)
{
	pr_info("stop\n");
	if (ctx) {
		damon_stop(&ctx, 1);
		damon_destroy_ctx(ctx);
	}
	if (target_pidp)
		put_pid(target_pidp);
}

static int damon_sample_prcl_enable_store(
		const char *val, const struct kernel_param *kp)
{
	bool enabled = enable;
	int err;

	err = kstrtobool(val, &enable);
	if (err)
		return err;

	if (enable == enabled)
		return 0;

	if (enable)
		return damon_sample_prcl_start();
	damon_sample_prcl_stop();
	return 0;
}

static int __init damon_sample_prcl_init(void)
{
	return 0;
}

module_init(damon_sample_prcl_init);
