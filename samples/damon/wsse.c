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

static int target_pid __read_mostly;
module_param(target_pid, int, 0600);

static int damon_sample_wsse_enable_store(
		const char *val, const struct kernel_param *kp);

static const struct kernel_param_ops enable_param_ops = {
	.set = damon_sample_wsse_enable_store,
	.get = param_get_bool,
};

static bool enable __read_mostly;
module_param_cb(enable, &enable_param_ops, &enable, 0600);
MODULE_PARM_DESC(enable, "Enable or disable DAMON_SAMPLE_WSSE");

static int damon_sample_wsse_start(void)
{
	pr_info("start\n");
	return 0;
}

static void damon_sample_wsse_stop(void)
{
	pr_info("stop\n");
}

static int damon_sample_wsse_enable_store(
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
		return damon_sample_wsse_start();
	damon_sample_wsse_stop();
	return 0;
}

static int __init damon_sample_wsse_init(void)
{
	return 0;
}

module_init(damon_sample_wsse_init);
