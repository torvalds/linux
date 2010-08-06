/*
 * trace event based perf event profiling/tracing
 *
 * Copyright (C) 2009 Red Hat Inc, Peter Zijlstra <pzijlstr@redhat.com>
 * Copyright (C) 2009-2010 Frederic Weisbecker <fweisbec@gmail.com>
 */

#include <linux/module.h>
#include <linux/kprobes.h>
#include "trace.h"

EXPORT_SYMBOL_GPL(perf_arch_fetch_caller_regs);

static char *perf_trace_buf[4];

/*
 * Force it to be aligned to unsigned long to avoid misaligned accesses
 * suprises
 */
typedef typeof(unsigned long [PERF_MAX_TRACE_SIZE / sizeof(unsigned long)])
	perf_trace_t;

/* Count the events in use (per event id, not per instance) */
static int	total_ref_count;

static int perf_trace_event_init(struct ftrace_event_call *tp_event,
				 struct perf_event *p_event)
{
	struct hlist_head *list;
	int ret = -ENOMEM;
	int cpu;

	p_event->tp_event = tp_event;
	if (tp_event->perf_refcount++ > 0)
		return 0;

	list = alloc_percpu(struct hlist_head);
	if (!list)
		goto fail;

	for_each_possible_cpu(cpu)
		INIT_HLIST_HEAD(per_cpu_ptr(list, cpu));

	tp_event->perf_events = list;

	if (!total_ref_count) {
		char *buf;
		int i;

		for (i = 0; i < 4; i++) {
			buf = (char *)alloc_percpu(perf_trace_t);
			if (!buf)
				goto fail;

			perf_trace_buf[i] = buf;
		}
	}

	if (tp_event->class->reg)
		ret = tp_event->class->reg(tp_event, TRACE_REG_PERF_REGISTER);
	else
		ret = tracepoint_probe_register(tp_event->name,
						tp_event->class->perf_probe,
						tp_event);

	if (ret)
		goto fail;

	total_ref_count++;
	return 0;

fail:
	if (!total_ref_count) {
		int i;

		for (i = 0; i < 4; i++) {
			free_percpu(perf_trace_buf[i]);
			perf_trace_buf[i] = NULL;
		}
	}

	if (!--tp_event->perf_refcount) {
		free_percpu(tp_event->perf_events);
		tp_event->perf_events = NULL;
	}

	return ret;
}

int perf_trace_init(struct perf_event *p_event)
{
	struct ftrace_event_call *tp_event;
	int event_id = p_event->attr.config;
	int ret = -EINVAL;

	mutex_lock(&event_mutex);
	list_for_each_entry(tp_event, &ftrace_events, list) {
		if (tp_event->event.type == event_id &&
		    tp_event->class &&
		    (tp_event->class->perf_probe ||
		     tp_event->class->reg) &&
		    try_module_get(tp_event->mod)) {
			ret = perf_trace_event_init(tp_event, p_event);
			break;
		}
	}
	mutex_unlock(&event_mutex);

	return ret;
}

int perf_trace_enable(struct perf_event *p_event)
{
	struct ftrace_event_call *tp_event = p_event->tp_event;
	struct hlist_head *list;

	list = tp_event->perf_events;
	if (WARN_ON_ONCE(!list))
		return -EINVAL;

	list = this_cpu_ptr(list);
	hlist_add_head_rcu(&p_event->hlist_entry, list);

	return 0;
}

void perf_trace_disable(struct perf_event *p_event)
{
	hlist_del_rcu(&p_event->hlist_entry);
}

void perf_trace_destroy(struct perf_event *p_event)
{
	struct ftrace_event_call *tp_event = p_event->tp_event;
	int i;

	mutex_lock(&event_mutex);
	if (--tp_event->perf_refcount > 0)
		goto out;

	if (tp_event->class->reg)
		tp_event->class->reg(tp_event, TRACE_REG_PERF_UNREGISTER);
	else
		tracepoint_probe_unregister(tp_event->name,
					    tp_event->class->perf_probe,
					    tp_event);

	/*
	 * Ensure our callback won't be called anymore. See
	 * tracepoint_probe_unregister() and __DO_TRACE().
	 */
	synchronize_sched();

	free_percpu(tp_event->perf_events);
	tp_event->perf_events = NULL;

	if (!--total_ref_count) {
		for (i = 0; i < 4; i++) {
			free_percpu(perf_trace_buf[i]);
			perf_trace_buf[i] = NULL;
		}
	}
out:
	mutex_unlock(&event_mutex);
}

__kprobes void *perf_trace_buf_prepare(int size, unsigned short type,
				       struct pt_regs *regs, int *rctxp)
{
	struct trace_entry *entry;
	unsigned long flags;
	char *raw_data;
	int pc;

	BUILD_BUG_ON(PERF_MAX_TRACE_SIZE % sizeof(unsigned long));

	pc = preempt_count();

	*rctxp = perf_swevent_get_recursion_context();
	if (*rctxp < 0)
		return NULL;

	raw_data = this_cpu_ptr(perf_trace_buf[*rctxp]);

	/* zero the dead bytes from align to not leak stack to user */
	memset(&raw_data[size - sizeof(u64)], 0, sizeof(u64));

	entry = (struct trace_entry *)raw_data;
	local_save_flags(flags);
	tracing_generic_entry_update(entry, flags, pc);
	entry->type = type;

	return raw_data;
}
EXPORT_SYMBOL_GPL(perf_trace_buf_prepare);
