// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple stack backtrace regression test module
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>

static void backtrace_test_normal(void)
{
	pr_info("Testing a backtrace from process context.\n");
	pr_info("The following trace is a kernel self test and not a bug!\n");

	dump_stack();
}

static DECLARE_COMPLETION(backtrace_work);

static void backtrace_test_irq_callback(unsigned long data)
{
	dump_stack();
	complete(&backtrace_work);
}

static DECLARE_TASKLET(backtrace_tasklet, &backtrace_test_irq_callback, 0);

static void backtrace_test_irq(void)
{
	pr_info("Testing a backtrace from irq context.\n");
	pr_info("The following trace is a kernel self test and not a bug!\n");

	init_completion(&backtrace_work);
	tasklet_schedule(&backtrace_tasklet);
	wait_for_completion(&backtrace_work);
}

#ifdef CONFIG_STACKTRACE
static void backtrace_test_saved(void)
{
	unsigned long entries[8];
	unsigned int nr_entries;

	pr_info("Testing a saved backtrace.\n");
	pr_info("The following trace is a kernel self test and not a bug!\n");

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 0);
	stack_trace_print(entries, nr_entries, 0);
}
#else
static void backtrace_test_saved(void)
{
	pr_info("Saved backtrace test skipped.\n");
}
#endif

static int backtrace_regression_test(void)
{
	pr_info("====[ backtrace testing ]===========\n");

	backtrace_test_normal();
	backtrace_test_irq();
	backtrace_test_saved();

	pr_info("====[ end of backtrace testing ]====\n");
	return 0;
}

static void exitf(void)
{
}

module_init(backtrace_regression_test);
module_exit(exitf);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
