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

	for_each_event(event) {
		if (event->id == event_id)
			return event->profile_enable(event);
	}

	return -EINVAL;
}

void ftrace_profile_disable(int event_id)
{
	struct ftrace_event_call *event;

	for_each_event(event) {
		if (event->id == event_id)
			return event->profile_disable(event);
	}
}

