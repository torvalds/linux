// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dhrystone benchmark test module
 *
 * Copyright (C) 2022 Glider bv
 */

#include "dhry.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/smp.h>

#define DHRY_VAX	1757

static int dhry_run_set(const char *val, const struct kernel_param *kp);
static const struct kernel_param_ops run_ops = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = dhry_run_set,
};
static bool dhry_run;
module_param_cb(run, &run_ops, &dhry_run, 0200);
MODULE_PARM_DESC(run, "Run the test (default: false)");

static int iterations = -1;
module_param(iterations, int, 0644);
MODULE_PARM_DESC(iterations,
		"Number of iterations through the benchmark (default: auto)");

static void dhry_benchmark(void)
{
	unsigned int cpu = get_cpu();
	int i, n;

	if (iterations > 0) {
		n = dhry(iterations);
		goto report;
	}

	for (i = DHRY_VAX; i > 0; i <<= 1) {
		n = dhry(i);
		if (n != -EAGAIN)
			break;
	}

report:
	put_cpu();
	if (n >= 0)
		pr_info("CPU%u: Dhrystones per Second: %d (%d DMIPS)\n", cpu,
			n, n / DHRY_VAX);
	else if (n == -EAGAIN)
		pr_err("Please increase the number of iterations\n");
	else
		pr_err("Dhrystone benchmark failed error %pe\n", ERR_PTR(n));
}

static int dhry_run_set(const char *val, const struct kernel_param *kp)
{
	int ret;

	if (val) {
		ret = param_set_bool(val, kp);
		if (ret)
			return ret;
	} else {
		dhry_run = true;
	}

	if (dhry_run && system_state == SYSTEM_RUNNING)
		dhry_benchmark();

	return 0;
}

static int __init dhry_init(void)
{
	if (dhry_run)
		dhry_benchmark();

	return 0;
}
module_init(dhry_init);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("Dhrystone benchmark test module");
MODULE_LICENSE("GPL");
