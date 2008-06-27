/*
 * Simple stack backtrace regression test module
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>

static struct timer_list backtrace_timer;

static void backtrace_test_timer(unsigned long data)
{
	printk("Testing a backtrace from irq context.\n");
	printk("The following trace is a kernel self test and not a bug!\n");
	dump_stack();
}

#ifdef CONFIG_STACKTRACE
static void backtrace_test_saved(void)
{
	struct stack_trace trace;
	unsigned long entries[8];

	printk("Testing a saved backtrace.\n");
	printk("The following trace is a kernel self test and not a bug!\n");

	trace.nr_entries = 0;
	trace.max_entries = ARRAY_SIZE(entries);
	trace.entries = entries;
	trace.skip = 0;

	save_stack_trace(&trace);
	print_stack_trace(&trace, 0);
}
#else
static void backtrace_test_saved(void)
{
	printk("Saved backtrace test skipped.\n");
}
#endif

static int backtrace_regression_test(void)
{
	printk("====[ backtrace testing ]===========\n");
	printk("Testing a backtrace from process context.\n");
	printk("The following trace is a kernel self test and not a bug!\n");
	dump_stack();

	backtrace_test_saved();

	init_timer(&backtrace_timer);
	backtrace_timer.function = backtrace_test_timer;
	mod_timer(&backtrace_timer, jiffies + 10);

	msleep(10);
	printk("====[ end of backtrace testing ]====\n");
	return 0;
}

static void exitf(void)
{
}

module_init(backtrace_regression_test);
module_exit(exitf);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
