// SPDX-License-Identifier: GPL-2.0
/*
 * working set size estimation: monitor access pattern of given process and
 * print estimated working set size (total size of regions that showing some
 * access).
 */

#define pr_fmt(fmt) "damon_sample_wsse: " fmt

#include <linux/damon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "damon_sample_wsse."

static int target_pid __read_mostly;
module_param(target_pid, int, 0600);

static int damon_sample_wsse_enable_store(
		const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops enabled_param_ops = {
	.set = damon_sample_wsse_enable_store,
	.get = param_get_bool,
};

static bool enabled __read_mostly;
module_param_cb(enabled, &enabled_param_ops, &enabled, 0600);
MODULE_PARM_DESC(enabled, "Enable or disable DAMON_SAMPLE_WSSE");

static struct damon_ctx *ctx;
static struct pid *target_pidp;

static int damon_sample_wsse_repeat_call_fn(void *data)
{
	struct damon_ctx *c = data;
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

static struct damon_call_control repeat_call_control = {
	.fn = damon_sample_wsse_repeat_call_fn,
	.repeat = true,
};

static int damon_sample_wsse_start(void)
{
	struct damon_target *target;
	int err;

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

	err = damon_start(&ctx, 1, true);
	if (err)
		return err;
	repeat_call_control.data = ctx;
	return damon_call(ctx, &repeat_call_control);
}

static void damon_sample_wsse_stop(void)
{
	pr_info("stop\n");
	if (ctx) {
		damon_stop(&ctx, 1);
		damon_destroy_ctx(ctx);
	}
}

static int damon_sample_wsse_enable_store(
		const char *val, const struct kernel_param *kp)
{
	bool is_enabled = enabled;
	int err;

	err = kstrtobool(val, &enabled);
	if (err)
		return err;

	if (enabled == is_enabled)
		return 0;

	if (!damon_initialized())
		return 0;

	if (enabled) {
		err = damon_sample_wsse_start();
		if (err)
			enabled = false;
		return err;
	}
	damon_sample_wsse_stop();
	return 0;
}

static int __init damon_sample_wsse_init(void)
{
	int err = 0;

	if (!damon_initialized()) {
		err = -ENOMEM;
		if (enabled)
			enabled = false;
	}

	if (enabled) {
		err = damon_sample_wsse_start();
		if (err)
			enabled = false;
	}
	return err;
}

module_init(damon_sample_wsse_init);
