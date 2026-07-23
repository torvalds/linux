// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "da_global"

/*
 * XXX: include required tracepoint headers, e.g.,
 * #include <trace/events/sched.h>
 */
#include <rv_trace.h>

/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#define RV_MON_TYPE RV_MON_GLOBAL
#include "da_global.h"
#include <rv/da_monitor.h>

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 *
 */
static void handle_event_1(void *data, /* XXX: fill header */)
{
	da_handle_event(event_1_da_global);
}

static void handle_event_2(void *data, /* XXX: fill header */)
{
	/* XXX: validate that this event always leads to the initial state */
	da_handle_start_event(event_2_da_global);
}

static int enable_da_global(void)
{
	int retval;

	retval = da_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("da_global", /* XXX: tracepoint */, handle_event_1);
	rv_attach_trace_probe("da_global", /* XXX: tracepoint */, handle_event_2);

	return 0;
}

static void disable_da_global(void)
{
	rv_this.enabled = 0;

	rv_detach_trace_probe("da_global", /* XXX: tracepoint */, handle_event_1);
	rv_detach_trace_probe("da_global", /* XXX: tracepoint */, handle_event_2);

	da_monitor_destroy();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_this = {
	.name = "da_global",
	.description = "auto-generated",
	.enable = enable_da_global,
	.disable = disable_da_global,
	.reset = da_monitor_reset_all,
	.enabled = 0,
};

static int __init register_da_global(void)
{
	return rv_register_monitor(&rv_this, NULL);
}

static void __exit unregister_da_global(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_da_global);
module_exit(unregister_da_global);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rvgen: auto-generated");
MODULE_DESCRIPTION("da_global: auto-generated");
