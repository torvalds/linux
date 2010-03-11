/*
 * trace event based perf event profiling/tracing
 *
 * Copyright (C) 2009 Red Hat Inc, Peter Zijlstra <pzijlstr@redhat.com>
 * Copyright (C) 2009-2010 Frederic Weisbecker <fweisbec@gmail.com>
 */

#include <linux/module.h>
#include <linux/kprobes.h>
#include "trace.h"

DEFINE_PER_CPU(struct pt_regs, perf_trace_regs);
EXPORT_PER_CPU_SYMBOL_GPL(perf_trace_regs);

static char *perf_trace_buf;
static char *perf_trace_buf_nmi;

typedef typeof(char [PERF_MAX_TRACE_SIZE]) perf_trace_t ;

/* Count the events in use (per event id, not per instance) */
static int	total_ref_count;

static int perf_trace_event_enable(struct ftrace_event_call *event)
{
	char *buf;
	int ret = -ENOMEM;

	if (event->perf_refcount++ > 0)
		return 0;

	if (!total_ref_count) {
		buf = (char *)alloc_percpu(perf_trace_t);
		if (!buf)
			goto fail_buf;

		rcu_assign_pointer(perf_trace_buf, buf);

		buf = (char *)alloc_percpu(perf_trace_t);
		if (!buf)
			goto fail_buf_nmi;

		rcu_assign_pointer(perf_trace_buf_nmi, buf);
	}

	ret = event->perf_event_enable(event);
	if (!ret) {
		total_ref_count++;
		return 0;
	}

fail_buf_nmi:
	if (!total_ref_count) {
		free_percpu(perf_trace_buf_nmi);
		free_percpu(perf_trace_buf);
		perf_trace_buf_nmi = NULL;
		perf_trace_buf = NULL;
	}
fail_buf:
	event->perf_refcount--;

	return ret;
}

int perf_trace_enable(int event_id)
{
	struct ftrace_event_call *event;
	int ret = -EINVAL;

	mutex_lock(&event_mutex);
	list_for_each_entry(event, &ftrace_events, list) {
		if (event->id == event_id && event->perf_event_enable &&
		    try_module_get(event->mod)) {
			ret = perf_trace_event_enable(event);
			break;
		}
	}
	mutex_unlock(&event_mutex);

	return ret;
}

static void perf_trace_event_disable(struct ftrace_event_call *event)
{
	char *buf, *nmi_buf;

	if (--event->perf_refcount > 0)
		return;

	event->perf_event_disable(event);

	if (!--total_ref_count) {
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

void perf_trace_disable(int event_id)
{
	struct ftrace_event_call *event;

	mutex_lock(&event_mutex);
	list_for_each_entry(event, &ftrace_events, list) {
		if (event->id == event_id) {
			perf_trace_event_disable(event);
			module_put(event->mod);
			break;
		}
	}
	mutex_unlock(&event_mutex);
}

__kprobes void *perf_trace_buf_prepare(int size, unsigned short type,
				       int *rctxp, unsigned long *irq_flags)
{
	struct trace_entry *entry;
	char *trace_buf, *raw_data;
	int pc, cpu;

	pc = preempt_count();

	/* Protect the per cpu buffer, begin the rcu read side */
	local_irq_save(*irq_flags);

	*rctxp = perf_swevent_get_recursion_context();
	if (*rctxp < 0)
		goto err_recursion;

	cpu = smp_processor_id();

	if (in_nmi())
		trace_buf = rcu_dereference(perf_trace_buf_nmi);
	else
		trace_buf = rcu_dereference(perf_trace_buf);

	if (!trace_buf)
		goto err;

	raw_data = per_cpu_ptr(trace_buf, cpu);

	/* zero the dead bytes from align to not leak stack to user */
	*(u64 *)(&raw_data[size - sizeof(u64)]) = 0ULL;

	entry = (struct trace_entry *)raw_data;
	tracing_generic_entry_update(entry, *irq_flags, pc);
	entry->type = type;

	return raw_data;
err:
	perf_swevent_put_recursion_context(*rctxp);
err_recursion:
	local_irq_restore(*irq_flags);
	return NULL;
}
EXPORT_SYMBOL_GPL(perf_trace_buf_prepare);
