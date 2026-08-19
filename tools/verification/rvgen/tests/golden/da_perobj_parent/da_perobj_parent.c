// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "da_perobj_parent"

/*
 * XXX: include required tracepoint headers, e.g.,
 * #include <trace/events/sched.h>
 */
#include <rv_trace.h>
#include <monitors/parent_mon/parent_mon.h>

/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#define RV_MON_TYPE RV_MON_PER_OBJ
typedef /* XXX: define the target type */ *monitor_target;
#include "da_perobj_parent.h"
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
	/* XXX: validate that this event is only valid in the initial state */
	int id = /* XXX: how do I get the id? */;
	monitor_target t = /* XXX: how do I get t? */;
	da_handle_start_run_event(id, t, event_1_da_perobj_parent);
}

static void handle_event_2(void *data, /* XXX: fill header */)
{
	int id = /* XXX: how do I get the id? */;
	monitor_target t = /* XXX: how do I get t? */;
	da_handle_event(id, t, event_2_da_perobj_parent);
}

static void handle_event_3(void *data, /* XXX: fill header */)
{
	int id = /* XXX: how do I get the id? */;
	monitor_target t = /* XXX: how do I get t? */;
	da_handle_event(id, t, event_3_da_perobj_parent);
}

/* XXX: obj is being destroyed, remove if not required (e.g. obj is static) */
static void handle_obj_cleanup(void *data, /* XXX: fill header */)
{
	int id = /* XXX: how do I get the id? */;
	da_destroy_storage(id);
}

static int enable_da_perobj_parent(void)
{
	int retval;

	retval = da_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("da_perobj_parent", /* XXX: tracepoint */, handle_event_1);
	rv_attach_trace_probe("da_perobj_parent", /* XXX: tracepoint */, handle_event_2);
	rv_attach_trace_probe("da_perobj_parent", /* XXX: tracepoint */, handle_event_3);
	rv_attach_trace_probe("da_perobj_parent", /* XXX: cleanup tracepoint */, handle_obj_cleanup);

	return 0;
}

static void disable_da_perobj_parent(void)
{
	rv_this.enabled = 0;

	rv_detach_trace_probe("da_perobj_parent", /* XXX: tracepoint */, handle_event_1);
	rv_detach_trace_probe("da_perobj_parent", /* XXX: tracepoint */, handle_event_2);
	rv_detach_trace_probe("da_perobj_parent", /* XXX: tracepoint */, handle_event_3);
	rv_detach_trace_probe("da_perobj_parent", /* XXX: cleanup tracepoint */, handle_obj_cleanup);

	da_monitor_destroy();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_this = {
	.name = "da_perobj_parent",
	.description = "auto-generated",
	.enable = enable_da_perobj_parent,
	.disable = disable_da_perobj_parent,
	.reset = da_monitor_reset_all,
	.enabled = 0,
};

static int __init register_da_perobj_parent(void)
{
	return rv_register_monitor(&rv_this, &rv_parent_mon);
}

static void __exit unregister_da_perobj_parent(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_da_perobj_parent);
module_exit(unregister_da_perobj_parent);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rvgen: auto-generated");
MODULE_DESCRIPTION("da_perobj_parent: auto-generated");
