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

static void backtrace_test_bh_workfn(struct work_struct *work)
{
	dump_stack();
}

static DECLARE_WORK(backtrace_bh_work, &backtrace_test_bh_workfn);

static void backtrace_test_bh(void)
{
	pr_info("Testing a backtrace from BH context.\n");
	pr_info("The following trace is a kernel self test and not a bug!\n");

	queue_work(system_bh_wq, &backtrace_bh_work);
	flush_work(&backtrace_bh_work);
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
	backtrace_test_bh();
	backtrace_test_saved();

	pr_info("====[ end of backtrace testing ]====\n");
	return 0;
}

static void exitf(void)
{
}

module_init(backtrace_regression_test);
module_exit(exitf);
MODULE_DESCRIPTION("Simple stack backtrace regression test module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arjan van de Ven <arjan@linux.intel.com>");
