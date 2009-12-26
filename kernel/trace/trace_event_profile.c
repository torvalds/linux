/*
 * trace event based perf counter profiling
 *
 * Copyright (C) 2009 Red Hat Inc, Peter Zijlstra <pzijlstr@redhat.com>
 *
 */

#include <linux/module.h>
#include "trace.h"


char *perf_trace_buf;
EXPORT_SYMBOL_GPL(perf_trace_buf);

char *perf_trace_buf_nmi;
EXPORT_SYMBOL_GPL(perf_trace_buf_nmi);

typedef typeof(char [FTRACE_MAX_PROFILE_SIZE]) perf_trace_t ;

/* Count the events in use (per event id, not per instance) */
static int	total_profile_count;

static int ftrace_profile_enable_event(struct ftrace_event_call *event)
{
	char *buf;
	int ret = -ENOMEM;

	if (event->profile_count++ > 0)
		return 0;

	if (!total_profile_count) {
		buf = (char *)alloc_percpu(perf_trace_t);
		if (!buf)
			goto fail_buf;

		rcu_assign_pointer(perf_trace_buf, buf);

		buf = (char *)alloc_percpu(perf_trace_t);
		if (!buf)
			goto fail_buf_nmi;

		rcu_assign_pointer(perf_trace_buf_nmi, buf);
	}

	ret = event->profile_enable(event);
	if (!ret) {
		total_profile_count++;
		return 0;
	}

fail_buf_nmi:
	if (!total_profile_count) {
		free_percpu(perf_trace_buf_nmi);
		free_percpu(perf_trace_buf);
		perf_trace_buf_nmi = NULL;
		perf_trace_buf = NULL;
	}
fail_buf:
	event->profile_count--;

	return ret;
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
	char *buf, *nmi_buf;

	if (--event->profile_count > 0)
		return;

	event->profile_disable(event);

	if (!--total_profile_count) {
		buf = perf_trace_buf;
		rcu_assign_pointer(perf_trace_buf, NULL);

		nmi_buf = perf_trace_buf_nmi;
		rcu_assign_pointer(perf_trace_buf_nmi, NULL);

		/*
		 * Ensure every events in profiling have finished before
		 * releasing the buffers
		 */
		synchronize_sched();

		free_percpu(buf);
		free_percpu(nmi_buf);
	}
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
