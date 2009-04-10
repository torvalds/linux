/*
 * trace event based perf counter profiling
 *
 * Copyright (C) 2009 Red Hat Inc, Peter Zijlstra <pzijlstr@redhat.com>
 *
 */

#include "trace.h"

int ftrace_profile_enable(int event_id)
{
	struct ftrace_event_call *event;

	list_for_each_entry(event, &ftrace_events, list) {
		if (event->id == event_id)
			return event->profile_enable(event);
	}

	return -EINVAL;
}

void ftrace_profile_disable(int event_id)
{
	struct ftrace_event_call *event;

	list_for_each_entry(event, &ftrace_events, list) {
		if (event->id == event_id)
			return event->profile_disable(event);
	}
}
