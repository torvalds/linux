/*
 * ring buffer based C-state tracer
 *
 * Arjan van de Ven <arjan@linux.intel.com>
 * Copyright (C) 2008 Intel Corporation
 *
 * Much is borrowed from trace_boot.c which is
 * Copyright (C) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 *
 */

#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>
#include <linux/module.h>

#include "trace.h"

static struct trace_array *power_trace;
static int __read_mostly trace_power_enabled;


static void start_power_trace(struct trace_array *tr)
{
	trace_power_enabled = 1;
}

static void stop_power_trace(struct trace_array *tr)
{
	trace_power_enabled = 0;
}


static int power_trace_init(struct trace_array *tr)
{
	int cpu;
	power_trace = tr;

	trace_power_enabled = 1;

	for_each_cpu(cpu, cpu_possible_mask)
		tracing_reset(tr, cpu);
	return 0;
}

static enum print_line_t power_print_line(struct trace_iterator *iter)
{
	int ret = 0;
	struct trace_entry *entry = iter->ent;
	struct trace_power *field ;
	struct power_trace *it;
	struct trace_seq *s = &iter->seq;
	struct timespec stamp;
	struct timespec duration;

	trace_assign_type(field, entry);
	it = &field->state_data;
	stamp = ktime_to_timespec(it->stamp);
	duration = ktime_to_timespec(ktime_sub(it->end, it->stamp));

	if (entry->type == TRACE_POWER) {
		if (it->type == POWER_CSTATE)
			ret = trace_seq_printf(s, "[%5ld.%09ld] CSTATE: Going to C%i on cpu %i for %ld.%09ld\n",
					  stamp.tv_sec,
					  stamp.tv_nsec,
					  it->state, iter->cpu,
					  duration.tv_sec,
					  duration.tv_nsec);
		if (it->type == POWER_PSTATE)
			ret = trace_seq_printf(s, "[%5ld.%09ld] PSTATE: Going to P%i on cpu %i\n",
					  stamp.tv_sec,
					  stamp.tv_nsec,
					  it->state, iter->cpu);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		return TRACE_TYPE_HANDLED;
	}
	return TRACE_TYPE_UNHANDLED;
}

static struct tracer power_tracer __read_mostly =
{
	.name		= "power",
	.init		= power_trace_init,
	.start		= start_power_trace,
	.stop		= stop_power_trace,
	.reset		= stop_power_trace,
	.print_line	= power_print_line,
};

static int init_power_trace(void)
{
	return register_tracer(&power_tracer);
}
device_initcall(init_power_trace);

void trace_power_start(struct power_trace *it, unsigned int type,
			 unsigned int level)
{
	if (!trace_power_enabled)
		return;

	memset(it, 0, sizeof(struct power_trace));
	it->state = level;
	it->type = type;
	it->stamp = ktime_get();
}
EXPORT_SYMBOL_GPL(trace_power_start);


void trace_power_end(struct power_trace *it)
{
	struct ring_buffer_event *event;
	struct trace_power *entry;
	struct trace_array_cpu *data;
	unsigned long irq_flags;
	struct trace_array *tr = power_trace;

	if (!trace_power_enabled)
		return;

	preempt_disable();
	it->end = ktime_get();
	data = tr->data[smp_processor_id()];

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		goto out;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, 0);
	entry->ent.type = TRACE_POWER;
	entry->state_data = *it;
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);

	trace_wake_up();

 out:
	preempt_enable();
}
EXPORT_SYMBOL_GPL(trace_power_end);

void trace_power_mark(struct power_trace *it, unsigned int type,
			 unsigned int level)
{
	struct ring_buffer_event *event;
	struct trace_power *entry;
	struct trace_array_cpu *data;
	unsigned long irq_flags;
	struct trace_array *tr = power_trace;

	if (!trace_power_enabled)
		return;

	memset(it, 0, sizeof(struct power_trace));
	it->state = level;
	it->type = type;
	it->stamp = ktime_get();
	preempt_disable();
	it->end = it->stamp;
	data = tr->data[smp_processor_id()];

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		goto out;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, 0);
	entry->ent.type = TRACE_POWER;
	entry->state_data = *it;
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);

	trace_wake_up();

 out:
	preempt_enable();
}
EXPORT_SYMBOL_GPL(trace_power_mark);
