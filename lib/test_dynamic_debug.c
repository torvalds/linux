// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module for testing dynamic_debug
 *
 * Authors:
 *      Jim Cromie	<jim.cromie@gmail.com>
 */

#define pr_fmt(fmt) "test_dd: " fmt

#include <linux/module.h>

static void do_prints(void); /* device under test */

/* run tests by reading or writing sysfs node */

static int param_set_do_prints(const char *instr, const struct kernel_param *kp)
{
	do_prints();
	return 0;
}

static int param_get_do_prints(char *buffer, const struct kernel_param *kp)
{
	do_prints();
	return scnprintf(buffer, PAGE_SIZE, "did do_prints\n");
}

static const struct kernel_param_ops param_ops_do_prints = {
	.set = param_set_do_prints,
	.get = param_get_do_prints,
};

module_param_cb(do_prints, &param_ops_do_prints, NULL, 0600);

static void do_alpha(void)
{
	pr_debug("do alpha\n");
}
static void do_beta(void)
{
	pr_debug("do beta\n");
}

static void do_prints(void)
{
	do_alpha();
	do_beta();
}

static int __init test_dynamic_debug_init(void)
{
	pr_debug("init start\n");

	do_prints();

	pr_debug("init done\n");
	return 0;
}

static void __exit test_dynamic_debug_exit(void)
{
	pr_debug("exiting\n");
}

module_init(test_dynamic_debug_init);
module_exit(test_dynamic_debug_exit);

MODULE_AUTHOR("Jim Cromie <jim.cromie@gmail.com>");
MODULE_LICENSE("GPL");
