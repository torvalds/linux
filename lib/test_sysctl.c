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
	{ }
};

static struct ctl_table_header *test_sysctl_header;

static int __init test_sysctl_init(void)
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

	test_data.bitmap_0001 = kzalloc(SYSCTL_TEST_BITMAP_SIZE/8, GFP_KERNEL);
	if (!test_data.bitmap_0001)
		return -ENOMEM;
	test_sysctl_header = register_sysctl("debug/test_sysctl", test_table);
	if (!test_sysctl_header) {
		kfree(test_data.bitmap_0001);
		return -ENOMEM;
	}
	return 0;
}
module_init(test_sysctl_init);

static void __exit test_sysctl_exit(void)
{
	kfree(test_data.bitmap_0001);
	if (test_sysctl_header)
		unregister_sysctl_table(test_sysctl_header);
}

module_exit(test_sysctl_exit);

MODULE_AUTHOR("Luis R. Rodriguez <mcgrof@kernel.org>");
MODULE_LICENSE("GPL");
