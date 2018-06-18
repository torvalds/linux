// SPDX-License-Identifier: GPL-2.0+
/*
 * test_ida.c: Test the IDA API
 * Copyright (c) 2016-2018 Microsoft Corporation
 * Copyright (c) 2018 Oracle Corporation
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/idr.h>
#include <linux/module.h>

static unsigned int tests_run;
static unsigned int tests_passed;

#ifdef __KERNEL__
void ida_dump(struct ida *ida) { }
#endif
#define IDA_BUG_ON(ida, x) do {						\
	tests_run++;							\
	if (x) {							\
		ida_dump(ida);						\
		dump_stack();						\
	} else {							\
		tests_passed++;						\
	}								\
} while (0)

/*
 * Check what happens when we fill a leaf and then delete it.  This may
 * discover mishandling of IDR_FREE.
 */
static void ida_check_leaf(struct ida *ida, unsigned int base)
{
	unsigned long i;

	for (i = 0; i < IDA_BITMAP_BITS; i++) {
		IDA_BUG_ON(ida, ida_alloc_min(ida, base, GFP_KERNEL) !=
				base + i);
	}

	ida_destroy(ida);
	IDA_BUG_ON(ida, !ida_is_empty(ida));

	IDA_BUG_ON(ida, ida_alloc(ida, GFP_KERNEL) != 0);
	IDA_BUG_ON(ida, ida_is_empty(ida));
	ida_free(ida, 0);
	IDA_BUG_ON(ida, !ida_is_empty(ida));
}

/*
 * Check allocations up to and slightly above the maximum allowed (2^31-1) ID.
 * Allocating up to 2^31-1 should succeed, and then allocating the next one
 * should fail.
 */
static void ida_check_max(struct ida *ida)
{
	unsigned long i, j;

	for (j = 1; j < 65537; j *= 2) {
		unsigned long base = (1UL << 31) - j;
		for (i = 0; i < j; i++) {
			IDA_BUG_ON(ida, ida_alloc_min(ida, base, GFP_KERNEL) !=
					base + i);
		}
		IDA_BUG_ON(ida, ida_alloc_min(ida, base, GFP_KERNEL) !=
				-ENOSPC);
		ida_destroy(ida);
		IDA_BUG_ON(ida, !ida_is_empty(ida));
	}
}

/*
 * Check handling of conversions between exceptional entries and full bitmaps.
 */
static void ida_check_conv(struct ida *ida)
{
	unsigned long i;

	for (i = 0; i < IDA_BITMAP_BITS * 2; i += IDA_BITMAP_BITS) {
		IDA_BUG_ON(ida, ida_alloc_min(ida, i + 1, GFP_KERNEL) != i + 1);
		IDA_BUG_ON(ida, ida_alloc_min(ida, i + BITS_PER_LONG,
					GFP_KERNEL) != i + BITS_PER_LONG);
		ida_free(ida, i + 1);
		ida_free(ida, i + BITS_PER_LONG);
		IDA_BUG_ON(ida, !ida_is_empty(ida));
	}

	for (i = 0; i < IDA_BITMAP_BITS * 2; i++)
		IDA_BUG_ON(ida, ida_alloc(ida, GFP_KERNEL) != i);
	for (i = IDA_BITMAP_BITS * 2; i > 0; i--)
		ida_free(ida, i - 1);
	IDA_BUG_ON(ida, !ida_is_empty(ida));

	for (i = 0; i < IDA_BITMAP_BITS + BITS_PER_LONG - 4; i++)
		IDA_BUG_ON(ida, ida_alloc(ida, GFP_KERNEL) != i);
	for (i = IDA_BITMAP_BITS + BITS_PER_LONG - 4; i > 0; i--)
		ida_free(ida, i - 1);
	IDA_BUG_ON(ida, !ida_is_empty(ida));
}

static int ida_checks(void)
{
	DEFINE_IDA(ida);

	IDA_BUG_ON(&ida, !ida_is_empty(&ida));
	ida_check_leaf(&ida, 0);
	ida_check_leaf(&ida, 1024);
	ida_check_leaf(&ida, 1024 * 64);
	ida_check_max(&ida);
	ida_check_conv(&ida);

	printk("IDA: %u of %u tests passed\n", tests_passed, tests_run);
	return (tests_run != tests_passed) ? 0 : -EINVAL;
}

static void ida_exit(void)
{
}

module_init(ida_checks);
module_exit(ida_exit);
MODULE_AUTHOR("Matthew Wilcox <willy@infradead.org>");
MODULE_LICENSE("GPL");
