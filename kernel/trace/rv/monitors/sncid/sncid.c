// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "sncid"

#include <trace/events/sched.h>
#include <trace/events/preemptirq.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "sncid.h"

static struct rv_monitor rv_sncid;
DECLARE_DA_MON_PER_CPU(sncid, unsigned char);

static void handle_irq_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_sncid(irq_disable_sncid);
}

static void handle_irq_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_start_event_sncid(irq_enable_sncid);
}

static void handle_schedule_entry(void *data, bool preempt, unsigned long ip)
{
	da_handle_start_event_sncid(schedule_entry_sncid);
}

static void handle_schedule_exit(void *data, bool is_switch, unsigned long ip)
{
	da_handle_start_event_sncid(schedule_exit_sncid);
}

static int enable_sncid(void)
{
	int retval;

	retval = da_monitor_init_sncid();
	if (retval)
		return retval;

	rv_attach_trace_probe("sncid", irq_disable, handle_irq_disable);
	rv_attach_trace_probe("sncid", irq_enable, handle_irq_enable);
	rv_attach_trace_probe("sncid", sched_entry_tp, handle_schedule_entry);
	rv_attach_trace_probe("sncid", sched_exit_tp, handle_schedule_exit);

	return 0;
}

static void disable_sncid(void)
{
	rv_sncid.enabled = 0;

	rv_detach_trace_probe("sncid", irq_disable, handle_irq_disable);
	rv_detach_trace_probe("sncid", irq_enable, handle_irq_enable);
	rv_detach_trace_probe("sncid", sched_entry_tp, handle_schedule_entry);
	rv_detach_trace_probe("sncid", sched_exit_tp, handle_schedule_exit);

	da_monitor_destroy_sncid();
}

static struct rv_monitor rv_sncid = {
	.name = "sncid",
	.description = "schedule not called with interrupt disabled.",
	.enable = enable_sncid,
	.disable = disable_sncid,
	.reset = da_monitor_reset_all_sncid,
	.enabled = 0,
};

static int __init register_sncid(void)
{
	rv_register_monitor(&rv_sncid, &rv_sched);
	return 0;
}

static void __exit unregister_sncid(void)
{
	rv_unregister_monitor(&rv_sncid);
}

module_init(register_sncid);
module_exit(unregister_sncid);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("sncid: schedule not called with interrupt disabled.");
