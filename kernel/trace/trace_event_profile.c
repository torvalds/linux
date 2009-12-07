/*
 * trace event based perf counter profiling
 *
 * Copyright (C) 2009 Red Hat Inc, Peter Zijlstra <pzijlstr@redhat.com>
 *
 */

#include <linux/module.h>
#include "trace.h"

/*
 * We can't use a size but a type in alloc_percpu()
 * So let's create a dummy type that matches the desired size
 */
typedef struct {char buf[FTRACE_MAX_PROFILE_SIZE];} profile_buf_t;

char		*trace_profile_buf;
EXPORT_SYMBOL_GPL(trace_profile_buf);

char		*trace_profile_buf_nmi;
EXPORT_SYMBOL_GPL(trace_profile_buf_nmi);

/* Count the events in use (per event id, not per instance) */
static int	total_profile_count;

static int ftrace_profile_enable_event(struct ftrace_event_call *event)
{
	char *buf;
	int ret = -ENOMEM;

	if (atomic_inc_return(&event->profile_count))
		return 0;

	if (!total_profile_count) {
		buf = (char *)alloc_percpu(profile_buf_t);
		if (!buf)
			goto fail_buf;

		rcu_assign_pointer(trace_profile_buf, buf);

		buf = (char *)alloc_percpu(profile_buf_t);
		if (!buf)
			goto fail_buf_nmi;

		rcu_assign_pointer(trace_profile_buf_nmi, buf);
	}

	ret = event->profile_enable();
	if (!ret) {
		total_profile_count++;
		return 0;
	}

fail_buf_nmi:
	if (!total_profile_count) {
		free_percpu(trace_profile_buf_nmi);
		free_percpu(trace_profile_buf);
		trace_profile_buf_nmi = NULL;
		trace_profile_buf = NULL;
	}
fail_buf:
	atomic_dec(&event->profile_count);

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

	if (!atomic_add_negative(-1, &event->profile_count))
		return;

	event->profile_disable();

	if (!--total_profile_count) {
		buf = trace_profile_buf;
		rcu_assign_pointer(trace_profile_buf, NULL);

		nmi_buf = trace_profile_buf_nmi;
		rcu_assign_pointer(trace_profile_buf_nmi, NULL);

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
