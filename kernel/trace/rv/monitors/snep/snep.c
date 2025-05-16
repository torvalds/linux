// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "snep"

#include <trace/events/sched.h>
#include <trace/events/preemptirq.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "snep.h"

static struct rv_monitor rv_snep;
DECLARE_DA_MON_PER_CPU(snep, unsigned char);

static void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_start_event_snep(preempt_disable_snep);
}

static void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_start_event_snep(preempt_enable_snep);
}

static void handle_schedule_entry(void *data, bool preempt, unsigned long ip)
{
	da_handle_event_snep(schedule_entry_snep);
}

static void handle_schedule_exit(void *data, bool is_switch, unsigned long ip)
{
	da_handle_start_event_snep(schedule_exit_snep);
}

static int enable_snep(void)
{
	int retval;

	retval = da_monitor_init_snep();
	if (retval)
		return retval;

	rv_attach_trace_probe("snep", preempt_disable, handle_preempt_disable);
	rv_attach_trace_probe("snep", preempt_enable, handle_preempt_enable);
	rv_attach_trace_probe("snep", sched_entry_tp, handle_schedule_entry);
	rv_attach_trace_probe("snep", sched_exit_tp, handle_schedule_exit);

	return 0;
}

static void disable_snep(void)
{
	rv_snep.enabled = 0;

	rv_detach_trace_probe("snep", preempt_disable, handle_preempt_disable);
	rv_detach_trace_probe("snep", preempt_enable, handle_preempt_enable);
	rv_detach_trace_probe("snep", sched_entry_tp, handle_schedule_entry);
	rv_detach_trace_probe("snep", sched_exit_tp, handle_schedule_exit);

	da_monitor_destroy_snep();
}

static struct rv_monitor rv_snep = {
	.name = "snep",
	.description = "schedule does not enable preempt.",
	.enable = enable_snep,
	.disable = disable_snep,
	.reset = da_monitor_reset_all_snep,
	.enabled = 0,
};

static int __init register_snep(void)
{
	rv_register_monitor(&rv_snep, &rv_sched);
	return 0;
}

static void __exit unregister_snep(void)
{
	rv_unregister_monitor(&rv_snep);
}

module_init(register_snep);
module_exit(unregister_snep);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("snep: schedule does not enable preempt.");
