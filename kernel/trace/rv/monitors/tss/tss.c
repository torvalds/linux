// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "tss"

#include <trace/events/sched.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "tss.h"

static struct rv_monitor rv_tss;
DECLARE_DA_MON_PER_CPU(tss, unsigned char);

static void handle_sched_switch(void *data, bool preempt,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned int prev_state)
{
	da_handle_event_tss(sched_switch_tss);
}

static void handle_schedule_entry(void *data, bool preempt, unsigned long ip)
{
	da_handle_event_tss(schedule_entry_tss);
}

static void handle_schedule_exit(void *data, bool is_switch, unsigned long ip)
{
	da_handle_start_event_tss(schedule_exit_tss);
}

static int enable_tss(void)
{
	int retval;

	retval = da_monitor_init_tss();
	if (retval)
		return retval;

	rv_attach_trace_probe("tss", sched_switch, handle_sched_switch);
	rv_attach_trace_probe("tss", sched_entry_tp, handle_schedule_entry);
	rv_attach_trace_probe("tss", sched_exit_tp, handle_schedule_exit);

	return 0;
}

static void disable_tss(void)
{
	rv_tss.enabled = 0;

	rv_detach_trace_probe("tss", sched_switch, handle_sched_switch);
	rv_detach_trace_probe("tss", sched_entry_tp, handle_schedule_entry);
	rv_detach_trace_probe("tss", sched_exit_tp, handle_schedule_exit);

	da_monitor_destroy_tss();
}

static struct rv_monitor rv_tss = {
	.name = "tss",
	.description = "task switch while scheduling.",
	.enable = enable_tss,
	.disable = disable_tss,
	.reset = da_monitor_reset_all_tss,
	.enabled = 0,
};

static int __init register_tss(void)
{
	rv_register_monitor(&rv_tss, &rv_sched);
	return 0;
}

static void __exit unregister_tss(void)
{
	rv_unregister_monitor(&rv_tss);
}

module_init(register_tss);
module_exit(unregister_tss);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("tss: task switch while scheduling.");
