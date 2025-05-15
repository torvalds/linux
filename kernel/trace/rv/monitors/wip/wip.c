// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

#define MODULE_NAME "wip"

#include <rv_trace.h>
#include <trace/events/sched.h>
#include <trace/events/preemptirq.h>

#include "wip.h"

static struct rv_monitor rv_wip;
DECLARE_DA_MON_PER_CPU(wip, unsigned char);

static void handle_preempt_disable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_event_wip(preempt_disable_wip);
}

static void handle_preempt_enable(void *data, unsigned long ip, unsigned long parent_ip)
{
	da_handle_start_event_wip(preempt_enable_wip);
}

static void handle_sched_waking(void *data, struct task_struct *task)
{
	da_handle_event_wip(sched_waking_wip);
}

static int enable_wip(void)
{
	int retval;

	retval = da_monitor_init_wip();
	if (retval)
		return retval;

	rv_attach_trace_probe("wip", preempt_enable, handle_preempt_enable);
	rv_attach_trace_probe("wip", sched_waking, handle_sched_waking);
	rv_attach_trace_probe("wip", preempt_disable, handle_preempt_disable);

	return 0;
}

static void disable_wip(void)
{
	rv_wip.enabled = 0;

	rv_detach_trace_probe("wip", preempt_disable, handle_preempt_disable);
	rv_detach_trace_probe("wip", preempt_enable, handle_preempt_enable);
	rv_detach_trace_probe("wip", sched_waking, handle_sched_waking);

	da_monitor_destroy_wip();
}

static struct rv_monitor rv_wip = {
	.name = "wip",
	.description = "wakeup in preemptive per-cpu testing monitor.",
	.enable = enable_wip,
	.disable = disable_wip,
	.reset = da_monitor_reset_all_wip,
	.enabled = 0,
};

static int __init register_wip(void)
{
	rv_register_monitor(&rv_wip, NULL);
	return 0;
}

static void __exit unregister_wip(void)
{
	rv_unregister_monitor(&rv_wip);
}

module_init(register_wip);
module_exit(unregister_wip);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Bristot de Oliveira <bristot@kernel.org>");
MODULE_DESCRIPTION("wip: wakeup in preemptive - per-cpu sample monitor.");
