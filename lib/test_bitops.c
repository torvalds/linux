// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Intel Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

/* a tiny module only meant to test set/clear_bit */

/* use an enum because thats the most common BITMAP usage */
enum bitops_fun {
	BITOPS_4 = 4,
	BITOPS_7 = 7,
	BITOPS_11 = 11,
	BITOPS_31 = 31,
	BITOPS_88 = 88,
	BITOPS_LAST = 255,
	BITOPS_LENGTH = 256
};

static DECLARE_BITMAP(g_bitmap, BITOPS_LENGTH);

static int __init test_bitops_startup(void)
{
	pr_warn("Loaded test module\n");
	set_bit(BITOPS_4, g_bitmap);
	set_bit(BITOPS_7, g_bitmap);
	set_bit(BITOPS_11, g_bitmap);
	set_bit(BITOPS_31, g_bitmap);
	set_bit(BITOPS_88, g_bitmap);
	return 0;
}

static void __exit test_bitops_unstartup(void)
{
	int bit_set;

	clear_bit(BITOPS_4, g_bitmap);
	clear_bit(BITOPS_7, g_bitmap);
	clear_bit(BITOPS_11, g_bitmap);
	clear_bit(BITOPS_31, g_bitmap);
	clear_bit(BITOPS_88, g_bitmap);

	bit_set = find_first_bit(g_bitmap, BITOPS_LAST);
	if (bit_set != BITOPS_LAST)
		pr_err("ERROR: FOUND SET BIT %d\n", bit_set);

	pr_warn("Unloaded test module\n");
}

module_init(test_bitops_startup);
module_exit(test_bitops_unstartup);

MODULE_AUTHOR("Jesse Brandeburg <jesse.brandeburg@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bit testing module");
