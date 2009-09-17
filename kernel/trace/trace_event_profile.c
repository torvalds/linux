/*
 * trace event based perf counter profiling
 *
 * Copyright (C) 2009 Red Hat Inc, Peter Zijlstra <pzijlstr@redhat.com>
 *
 */

#include <linux/module.h>
#include "trace.h"

static int ftrace_profile_enable_event(struct ftrace_event_call *event)
{
	if (atomic_inc_return(&event->profile_count))
		return 0;

	return event->profile_enable();
}

int ftrace_profile_enable(int event_id)
{
	struct ftrace_event_call *event;
	int ret = -EINVAL;

	mutex_lock(&event_mutex);
	list_for_each_entry(event, &ftrace_events, list) {
		if (event->id == event_id && event->profile_enable &&
		    try_module_get(event->mod)) {
			ret = ftrace_profile_enable_event(event);
			break;
		}
	}
	mutex_unlock(&event_mutex);

	return ret;
}

static void ftrace_profile_disable_event(struct ftrace_event_call *event)
{
	if (!atomic_add_negative(-1, &event->profile_count))
		return;

	event->profile_disable();
}

void ftrace_profile_disable(int event_id)
{
	struct ftrace_event_call *event;

	mutex_lock(&event_mutex);
	list_for_each_entry(event, &ftrace_events, list) {
		if (event->id == event_id) {
			ftrace_profile_disable_event(event);
			module_put(event->mod);
			break;
		}
	}
	mutex_unlock(&event_mutex);
}
