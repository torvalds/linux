// SPDX-License-Identifier: GPL-2.0-or-later OR copyleft-next-0.3.1
/*
 * proc sysctl test driver
 *
 * Copyright (C) 2017 Luis R. Rodriguez <mcgrof@kernel.org>
 */

/*
 * This module provides an interface to the proc sysctl interfaces.  This
 * driver requires CONFIG_PROC_SYSCTL. It will not normally be loaded by the
 * system unless explicitly requested by name. You can also build this driver
 * into your kernel.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>

static int i_zero;
static int i_one_hundred = 100;
static int match_int_ok = 1;


static struct {
	struct ctl_table_header *test_h_setup_node;
	struct ctl_table_header *test_h_mnt;
	struct ctl_table_header *test_h_mnterror;
	struct ctl_table_header *empty_add;
	struct ctl_table_header *empty;
} sysctl_test_headers;

struct test_sysctl_data {
	int int_0001;
	int int_0002;
	int int_0003[4];

	int boot_int;

	unsigned int uint_0001;

	char string_0001[65];

#define SYSCTL_TEST_BITMAP_SIZE	65536
	unsigned long *bitmap_0001;
};

static struct test_sysctl_data test_data = {
	.int_0001 = 60,
	.int_0002 = 1,

	.int_0003[0] = 0,
	.int_0003[1] = 1,
	.int_0003[2] = 2,
	.int_0003[3] = 3,

	.boot_int = 0,

	.uint_0001 = 314,

	.string_0001 = "(none)",
};

/* These are all under /proc/sys/debug/test_sysctl/ */
static struct ctl_table test_table[] = {
	{
		.procname	= "int_0001",
		.data		= &test_data.int_0001,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	},
	{
		.procname	= "int_0002",
		.data		= &test_data.int_0002,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "int_0003",
		.data		= &test_data.int_0003,
		.maxlen		= sizeof(test_data.int_0003),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "match_int",
		.data		= &match_int_ok,
		.maxlen		= sizeof(match_int_ok),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "boot_int",
		.data		= &test_data.boot_int,
		.maxlen		= sizeof(test_data.boot_int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{
		.procname	= "uint_0001",
		.data		= &test_data.uint_0001,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec,
	},
	{
		.procname	= "string_0001",
		.data		= &test_data.string_0001,
		.maxlen		= sizeof(test_data.string_0001),
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "bitmap_0001",
		.data		= &test_data.bitmap_0001,
		.maxlen		= SYSCTL_TEST_BITMAP_SIZE,
		.mode		= 0644,
		.proc_handler	= proc_do_large_bitmap,
	},
};

static void test_sysctl_calc_match_int_ok(void)
{
	int i;

	struct {
		int defined;
		int wanted;
	} match_int[] = {
		{.defined = *(int *)SYSCTL_ZERO,	.wanted = 0},
		{.defined = *(int *)SYSCTL_ONE,		.wanted = 1},
		{.defined = *(int *)SYSCTL_TWO,		.wanted = 2},
		{.defined = *(int *)SYSCTL_THREE,	.wanted = 3},
		{.defined = *(int *)SYSCTL_FOUR,	.wanted = 4},
		{.defined = *(int *)SYSCTL_ONE_HUNDRED, .wanted = 100},
		{.defined = *(int *)SYSCTL_TWO_HUNDRED,	.wanted = 200},
		{.defined = *(int *)SYSCTL_ONE_THOUSAND, .wanted = 1000},
		{.defined = *(int *)SYSCTL_THREE_THOUSAND, .wanted = 3000},
		{.defined = *(int *)SYSCTL_INT_MAX,	.wanted = INT_MAX},
		{.defined = *(int *)SYSCTL_MAXOLDUID,	.wanted = 65535},
		{.defined = *(int *)SYSCTL_NEG_ONE,	.wanted = -1},
	};

	for (i = 0; i < ARRAY_SIZE(match_int); i++)
		if (match_int[i].defined != match_int[i].wanted)
			match_int_ok = 0;
}

static int test_sysctl_setup_node_tests(void)
{
	test_sysctl_calc_match_int_ok();
	test_data.bitmap_0001 = kzalloc(SYSCTL_TEST_BITMAP_SIZE/8, GFP_KERNEL);
	if (!test_data.bitmap_0001)
		return -ENOMEM;
	sysctl_test_headers.test_h_setup_node = register_sysctl("debug/test_sysctl", test_table);
	if (!sysctl_test_headers.test_h_setup_node) {
		kfree(test_data.bitmap_0001);
		return -ENOMEM;
	}

	return 0;
}

/* Used to test that unregister actually removes the directory */
static struct ctl_table test_table_unregister[] = {
	{
		.procname	= "unregister_error",
		.data		= &test_data.int_0001,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
	},
};

static int test_sysctl_run_unregister_nested(void)
{
	struct ctl_table_header *unregister;

	unregister = register_sysctl("debug/test_sysctl/unregister_error",
				   test_table_unregister);
	if (!unregister)
		return -ENOMEM;

	unregister_sysctl_table(unregister);
	return 0;
}

static int test_sysctl_run_register_mount_point(void)
{
	sysctl_test_headers.test_h_mnt
		= register_sysctl_mount_point("debug/test_sysctl/mnt");
	if (!sysctl_test_headers.test_h_mnt)
		return -ENOMEM;

	sysctl_test_headers.test_h_mnterror
		= register_sysctl("debug/test_sysctl/mnt/mnt_error",
				  test_table_unregister);
	/*
	 * Don't check the result.:
	 * If it fails (expected behavior), return 0.
	 * If successful (missbehavior of register mount point), we want to see
	 * mnt_error when we run the sysctl test script
	 */

	return 0;
}

static struct ctl_table test_table_empty[] = { };

static int test_sysctl_run_register_empty(void)
{
	/* Tets that an empty dir can be created */
	sysctl_test_headers.empty_add
		= register_sysctl("debug/test_sysctl/empty_add", test_table_empty);
	if (!sysctl_test_headers.empty_add)
		return -ENOMEM;

	/* Test that register on top of an empty dir works */
	sysctl_test_headers.empty
		= register_sysctl("debug/test_sysctl/empty_add/empty", test_table_empty);
	if (!sysctl_test_headers.empty)
		return -ENOMEM;

	return 0;
}

static int __init test_sysctl_init(void)
{
	int err;

	err = test_sysctl_setup_node_tests();
	if (err)
		goto out;

	err = test_sysctl_run_unregister_nested();
	if (err)
		goto out;

	err = test_sysctl_run_register_mount_point();
	if (err)
		goto out;

	err = test_sysctl_run_register_empty();

out:
	return err;
}
module_init(test_sysctl_init);

static void __exit test_sysctl_exit(void)
{
	kfree(test_data.bitmap_0001);
	if (sysctl_test_headers.test_h_setup_node)
		unregister_sysctl_table(sysctl_test_headers.test_h_setup_node);
	if (sysctl_test_headers.test_h_mnt)
		unregister_sysctl_table(sysctl_test_headers.test_h_mnt);
	if (sysctl_test_headers.test_h_mnterror)
		unregister_sysctl_table(sysctl_test_headers.test_h_mnterror);
	if (sysctl_test_headers.empty)
		unregister_sysctl_table(sysctl_test_headers.empty);
	if (sysctl_test_headers.empty_add)
		unregister_sysctl_table(sysctl_test_headers.empty_add);
}

module_exit(test_sysctl_exit);

MODULE_AUTHOR("Luis R. Rodriguez <mcgrof@kernel.org>");
MODULE_DESCRIPTION("proc sysctl test driver");
MODULE_LICENSE("GPL");
