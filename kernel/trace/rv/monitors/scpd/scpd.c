// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "scpd"

#include <trace/events/sched.h>
#include <trace/events/preemptirq.h>
#include <rv_trace.h>
#include <monitors/sched/sched.h>

#include "scpd.h"

static struct rv_monitor rv_scpd;
DECLARE_DA_MON_PER_CPU(scpd, unsigned char);

static void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_scpd(preempt_disable_scpd);
}

static void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_start_event_scpd(preempt_enable_scpd);
}

static void handle_schedule_entry(void *data, bool preempt, unsigned long ip)
{
	da_handle_event_scpd(schedule_entry_scpd);
}

static void handle_schedule_exit(void *data, bool is_switch, unsigned long ip)
{
	da_handle_event_scpd(schedule_exit_scpd);
}

static int enable_scpd(void)
{
	int retval;

	retval = da_monitor_init_scpd();
	if (retval)
		return retval;

	rv_attach_trace_probe("scpd", preempt_disable, handle_preempt_disable);
	rv_attach_trace_probe("scpd", preempt_enable, handle_preempt_enable);
	rv_attach_trace_probe("scpd", sched_entry_tp, handle_schedule_entry);
	rv_attach_trace_probe("scpd", sched_exit_tp, handle_schedule_exit);

	return 0;
}

static void disable_scpd(void)
{
	rv_scpd.enabled = 0;

	rv_detach_trace_probe("scpd", preempt_disable, handle_preempt_disable);
	rv_detach_trace_probe("scpd", preempt_enable, handle_preempt_enable);
	rv_detach_trace_probe("scpd", sched_entry_tp, handle_schedule_entry);
	rv_detach_trace_probe("scpd", sched_exit_tp, handle_schedule_exit);

	da_monitor_destroy_scpd();
}

static struct rv_monitor rv_scpd = {
	.name = "scpd",
	.description = "schedule called with preemption disabled.",
	.enable = enable_scpd,
	.disable = disable_scpd,
	.reset = da_monitor_reset_all_scpd,
	.enabled = 0,
};

static int __init register_scpd(void)
{
	rv_register_monitor(&rv_scpd, &rv_sched);
	return 0;
}

static void __exit unregister_scpd(void)
{
	rv_unregister_monitor(&rv_scpd);
}

module_init(register_scpd);
module_exit(unregister_scpd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gabriele Monaco <gmonaco@redhat.com>");
MODULE_DESCRIPTION("scpd: schedule called with preemption disabled.");
