// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "snroc"

#include <trace/events/sched.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "snroc.h"

static struct rv_monitor rv_snroc;
DECLARE_DA_MON_PER_TASK(snroc, unsigned char);

static void handle_sched_set_state(void *data, struct task_struct *tsk, int state)
{
	da_handle_event_snroc(tsk, sched_set_state_snroc);
}

static void handle_sched_switch(void *data, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	da_handle_start_event_snroc(prev, sched_switch_out_snroc);
	da_handle_event_snroc(next, sched_switch_in_snroc);
}

static int enable_snroc(void)
{
	int retval;

	retval = da_monitor_init_snroc();
	if (retval)
		return retval;

	rv_attach_trace_probe("snroc", sched_set_state_tp, handle_sched_set_state);
	rv_attach_trace_probe("snroc", sched_switch, handle_sched_switch);

	return 0;
}

static void disable_snroc(void)
{
	rv_snroc.enabled = 0;

	rv_detach_trace_probe("snroc", sched_set_state_tp, handle_sched_set_state);
	rv_detach_trace_probe("snroc", sched_switch, handle_sched_switch);

	da_monitor_destroy_snroc();
}

static struct rv_monitor rv_snroc = {
	.name = "snroc",
	.description = "set non runnable on its own context.",
	.enable = enable_snroc,
	.disable = disable_snroc,
	.reset = da_monitor_reset_all_snroc,
	.enabled = 0,
};

static int __init register_snroc(void)
{
	return rv_register_monitor(&rv_snroc, &rv_sched);
}

static void __exit unregister_snroc(void)
{
	rv_unregister_monitor(&rv_snroc);
}

module_init(register_snroc);
module_exit(unregister_snroc);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("snroc: set non runnable on its own context.");
