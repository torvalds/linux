// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rv.h>
#include <linux/sched/deadline.h>
#include <linux/sched/rt.h>
#include <linux/tracepoint.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "pagefault"

#include <rv_trace.h>
#include <trace/events/exceptions.h>
#include <monitors/rtapp/rtapp.h>

#include "pagefault.h"
#include <rv/ltl_monitor.h>

static void ltl_atoms_fetch(struct task_struct *task, struct ltl_monitor *mon)
{
	/*
	 * This includes "actual" real-time tasks and also PI-boosted
	 * tasks. A task being PI-boosted means it is blocking an "actual"
	 * real-task, therefore it should also obey the monitor's rule,
	 * otherwise the "actual" real-task may be delayed.
	 */
	ltl_atom_set(mon, LTL_RT, rt_or_dl_task(task));
}

static void ltl_atoms_init(struct task_struct *task, struct ltl_monitor *mon, bool task_creation)
{
	if (task_creation)
		ltl_atom_set(mon, LTL_PAGEFAULT, false);
}

static void handle_page_fault(void *data, unsigned long address, struct pt_regs *regs,
			      unsigned long error_code)
{
	ltl_atom_pulse(current, LTL_PAGEFAULT, true);
}

static int enable_pagefault(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("rtapp_pagefault", page_fault_kernel, handle_page_fault);
	rv_attach_trace_probe("rtapp_pagefault", page_fault_user, handle_page_fault);

	return 0;
}

static void disable_pagefault(void)
{
	rv_detach_trace_probe("rtapp_pagefault", page_fault_kernel, handle_page_fault);
	rv_detach_trace_probe("rtapp_pagefault", page_fault_user, handle_page_fault);

	ltl_monitor_destroy();
}

static struct rv_monitor rv_pagefault = {
	.name = "pagefault",
	.description = "Monitor that RT tasks do not raise page faults",
	.enable = enable_pagefault,
	.disable = disable_pagefault,
};

static int __init register_pagefault(void)
{
	return rv_register_monitor(&rv_pagefault, &rv_rtapp);
}

static void __exit unregister_pagefault(void)
{
	rv_unregister_monitor(&rv_pagefault);
}

module_init(register_pagefault);
module_exit(unregister_pagefault);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nam Cao <namcao@linutronix.de>");
MODULE_DESCRIPTION("pagefault: Monitor that RT tasks do not raise page faults");
