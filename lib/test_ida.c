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

static int ida_checks(void)
{
	DEFINE_IDA(ida);

	IDA_BUG_ON(&ida, !ida_is_empty(&ida));
	ida_check_leaf(&ida, 0);
	ida_check_leaf(&ida, 1024);
	ida_check_leaf(&ida, 1024 * 64);

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
