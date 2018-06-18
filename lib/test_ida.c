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

static int ida_checks(void)
{
	DEFINE_IDA(ida);

	IDA_BUG_ON(&ida, !ida_is_empty(&ida));

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
