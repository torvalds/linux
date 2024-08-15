// SPDX-License-Identifier: GPL-2.0
/*
 * event tracer
 *
 * Copyright (C) 2022 Google Inc, Steven Rostedt <rostedt@goodmis.org>
 */

#define pr_fmt(fmt) fmt

#include <linux/trace_events.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched.h>

/*
 * Must include the event header that the custom event will attach to,
 * from the C file, and not in the custom header file.
 */
#include <trace/events/sched.h>

/* Declare CREATE_CUSTOM_TRACE_EVENTS before including custom header */
#define CREATE_CUSTOM_TRACE_EVENTS

#include "trace_custom_sched.h"

/*
 * As the trace events are not exported to modules, the use of
 * for_each_kernel_tracepoint() is needed to find the trace event
 * to attach to. The fct() function below, is a callback that
 * will be called for every event.
 *
 * Helper functions are created by the TRACE_CUSTOM_EVENT() macro
 * update the event. Those are of the form:
 *
 *    trace_custom_event_<event>_update()
 *
 * Where <event> is the event to attach.
 */
static void fct(struct tracepoint *tp, void *priv)
{
	trace_custom_event_sched_switch_update(tp);
	trace_custom_event_sched_waking_update(tp);
}

static int __init trace_sched_init(void)
{
	for_each_kernel_tracepoint(fct, NULL);
	return 0;
}

static void __exit trace_sched_exit(void)
{
}

module_init(trace_sched_init);
module_exit(trace_sched_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("Custom scheduling events");
MODULE_LICENSE("GPL");
