/*
 * trace context switch
 *
 * Copyright (C) 2007 Steven Rostedt <srostedt@redhat.com>
 *
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <trace/events/sched.h>

#include "trace.h"

static struct trace_array	*ctx_trace;
static int __read_mostly	tracer_enabled;
static int			sched_ref;
static DEFINE_MUTEX(sched_register_mutex);
static int			sched_stopped;


void
tracing_sched_switch_trace(struct trace_array *tr,
			   struct task_struct *prev,
			   struct task_struct *next,
			   unsigned long flags, int pc)
{
	struct ftrace_event_call *call = &event_context_switch;
	struct ring_buffer *buffer = tr->buffer;
	struct ring_buffer_event *event;
	struct ctx_switch_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_CTX,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->prev_pid			= prev->pid;
	entry->prev_prio		= prev->prio;
	entry->prev_state		= prev->state;
	entry->next_pid			= next->pid;
	entry->next_prio		= next->prio;
	entry->next_state		= next->state;
	entry->next_cpu	= task_cpu(next);

	if (!filter_check_discard(call, entry, buffer, event))
		trace_buffer_unlock_commit(buffer, event, flags, pc);
}

static void
probe_sched_switch(void *ignore, struct task_struct *prev, struct task_struct *next)
{
	struct trace_array_cpu *data;
	unsigned long flags;
	int cpu;
	int pc;

	if (unlikely(!sched_ref))
		return;

	tracing_record_cmdline(prev);
	tracing_record_cmdline(next);

	if (!tracer_enabled || sched_stopped)
		return;

	pc = preempt_count();
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = ctx_trace->data[cpu];

	if (likely(!atomic_read(&data->disabled)))
		tracing_sched_switch_trace(ctx_trace, prev, next, flags, pc);

	local_irq_restore(flags);
}

void
tracing_sched_wakeup_trace(struct trace_array *tr,
			   struct task_struct *wakee,
			   struct task_struct *curr,
			   unsigned long flags, int pc)
{
	struct ftrace_event_call *call = &event_wakeup;
	struct ring_buffer_event *event;
	struct ctx_switch_entry *entry;
	struct ring_buffer *buffer = tr->buffer;

	event = trace_buffer_lock_reserve(buffer, TRACE_WAKE,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->prev_pid			= curr->pid;
	entry->prev_prio		= curr->prio;
	entry->prev_state		= curr->state;
	entry->next_pid			= wakee->pid;
	entry->next_prio		= wakee->prio;
	entry->next_state		= wakee->state;
	entry->next_cpu			= task_cpu(wakee);

	if (!filter_check_discard(call, entry, buffer, event))
		ring_buffer_unlock_commit(buffer, event);
	ftrace_trace_stack(tr->buffer, flags, 6, pc);
	ftrace_trace_userstack(tr->buffer, flags, pc);
}

static void
probe_sched_wakeup(void *ignore, struct task_struct *wakee, int success)
{
	struct trace_array_cpu *data;
	unsigned long flags;
	int cpu, pc;

	if (unlikely(!sched_ref))
		return;

	tracing_record_cmdline(current);

	if (!tracer_enabled || sched_stopped)
		return;

	pc = preempt_count();
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = ctx_trace->data[cpu];

	if (likely(!atomic_read(&data->disabled)))
		tracing_sched_wakeup_trace(ctx_trace, wakee, current,
					   flags, pc);

	local_irq_restore(flags);
}

static int tracing_sched_register(void)
{
	int ret;

	ret = register_trace_sched_wakeup(probe_sched_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup\n");
		return ret;
	}

	ret = register_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
	if (ret) {
		pr_info("wakeup trace: Couldn't activate tracepoint"
			" probe to kernel_sched_wakeup_new\n");
		goto fail_deprobe;
	}

	ret = register_trace_sched_switch(probe_sched_switch, NULL);
	if (ret) {
		pr_info("sched trace: Couldn't activate tracepoint"
			" probe to kernel_sched_switch\n");
		goto fail_deprobe_wake_new;
	}

	return ret;
fail_deprobe_wake_new:
	unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
fail_deprobe:
	unregister_trace_sched_wakeup(probe_sched_wakeup, NULL);
	return ret;
}

static void tracing_sched_unregister(void)
{
	unregister_trace_sched_switch(probe_sched_switch, NULL);
	unregister_trace_sched_wakeup_new(probe_sched_wakeup, NULL);
	unregister_trace_sched_wakeup(probe_sched_wakeup, NULL);
}

static void tracing_start_sched_switch(void)
{
	mutex_lock(&sched_register_mutex);
	if (!(sched_ref++))
		tracing_sched_register();
	mutex_unlock(&sched_register_mutex);
}

static void tracing_stop_sched_switch(void)
{
	mutex_lock(&sched_register_mutex);
	if (!(--sched_ref))
		tracing_sched_unregister();
	mutex_unlock(&sched_register_mutex);
}

void tracing_start_cmdline_record(void)
{
	tracing_start_sched_switch();
}

void tracing_stop_cmdline_record(void)
{
	tracing_stop_sched_switch();
}

/**
 * tracing_start_sched_switch_record - start tracing context switches
 *
 * Turns on context switch tracing for a tracer.
 */
void tracing_start_sched_switch_record(void)
{
	if (unlikely(!ctx_trace)) {
		WARN_ON(1);
		return;
	}

	tracing_start_sched_switch();

	mutex_lock(&sched_register_mutex);
	tracer_enabled++;
	mutex_unlock(&sched_register_mutex);
}

/**
 * tracing_stop_sched_switch_record - start tracing context switches
 *
 * Turns off context switch tracing for a tracer.
 */
void tracing_stop_sched_switch_record(void)
{
	mutex_lock(&sched_register_mutex);
	tracer_enabled--;
	WARN_ON(tracer_enabled < 0);
	mutex_unlock(&sched_register_mutex);

	tracing_stop_sched_switch();
}

/**
 * tracing_sched_switch_assign_trace - assign a trace array for ctx switch
 * @tr: trace array pointer to assign
 *
 * Some tracers might want to record the context switches in their
 * trace. This function lets those tracers assign the trace array
 * to use.
 */
void tracing_sched_switch_assign_trace(struct trace_array *tr)
{
	ctx_trace = tr;
}

