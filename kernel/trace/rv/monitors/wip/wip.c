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

/*
 * XXX: include required tracepoint headers, e.g.,
 * #include <linux/trace/events/sched.h>
 */
#include <trace/events/rv.h>

/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#include "wip.h"

/*
 * Declare the deterministic automata monitor.
 *
 * The rv monitor reference is needed for the monitor declaration.
 */
struct rv_monitor rv_wip;
DECLARE_DA_MON_PER_CPU(wip, unsigned char);

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 *
 */
static void handle_preempt_disable(void *data, /* XXX: fill header */)
{
	da_handle_event_wip(preempt_disable_wip);
}

static void handle_preempt_enable(void *data, /* XXX: fill header */)
{
	da_handle_event_wip(preempt_enable_wip);
}

static void handle_sched_waking(void *data, /* XXX: fill header */)
{
	da_handle_event_wip(sched_waking_wip);
}

static int enable_wip(void)
{
	int retval;

	retval = da_monitor_init_wip();
	if (retval)
		return retval;

	rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_disable);
	rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_enable);
	rv_attach_trace_probe("wip", /* XXX: tracepoint */, handle_sched_waking);

	return 0;
}

static void disable_wip(void)
{
	rv_wip.enabled = 0;

	rv_detach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_disable);
	rv_detach_trace_probe("wip", /* XXX: tracepoint */, handle_preempt_enable);
	rv_detach_trace_probe("wip", /* XXX: tracepoint */, handle_sched_waking);

	da_monitor_destroy_wip();
}

/*
 * This is the monitor register section.
 */
struct rv_monitor rv_wip = {
	.name = "wip",
	.description = "auto-generated wip",
	.enable = enable_wip,
	.disable = disable_wip,
	.reset = da_monitor_reset_all_wip,
	.enabled = 0,
};

static int register_wip(void)
{
	rv_register_monitor(&rv_wip);
	return 0;
}

static void unregister_wip(void)
{
	rv_unregister_monitor(&rv_wip);
}

module_init(register_wip);
module_exit(unregister_wip);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dot2k: auto-generated");
MODULE_DESCRIPTION("wip");
