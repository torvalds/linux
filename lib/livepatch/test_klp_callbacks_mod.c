// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>

static int test_klp_callbacks_mod_init(void)
{
	pr_info("%s\n", __func__);
	return 0;
}

static void test_klp_callbacks_mod_exit(void)
{
	pr_info("%s\n", __func__);
}

module_init(test_klp_callbacks_mod_init);
module_exit(test_klp_callbacks_mod_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: target module");
