/*
 * ring buffer based initcalls tracer
 *
 * Copyright (C) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 *
 */

#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>

#include "trace.h"

static struct trace_array *boot_trace;
static int trace_boot_enabled;


/* Should be started after do_pre_smp_initcalls() in init/main.c */
void start_boot_trace(void)
{
	trace_boot_enabled = 1;
}

void stop_boot_trace(void)
{
	trace_boot_enabled = 0;
}

void reset_boot_trace(struct trace_array *tr)
{
	stop_boot_trace();
}

static void boot_trace_init(struct trace_array *tr)
{
	int cpu;
	boot_trace = tr;

	trace_boot_enabled = 0;

	for_each_cpu_mask(cpu, cpu_possible_map)
		tracing_reset(tr, cpu);
}

static void boot_trace_ctrl_update(struct trace_array *tr)
{
	if (tr->ctrl)
		start_boot_trace();
	else
		stop_boot_trace();
}

static enum print_line_t initcall_print_line(struct trace_iterator *iter)
{
	int ret;
	struct trace_entry *entry = iter->ent;
	struct trace_boot *field = (struct trace_boot *)entry;
	struct boot_trace *it = &field->initcall;
	struct trace_seq *s = &iter->seq;
	struct timespec calltime = ktime_to_timespec(it->calltime);
	struct timespec rettime = ktime_to_timespec(it->rettime);

	if (entry->type == TRACE_BOOT) {
		ret = trace_seq_printf(s, "[%5ld.%09ld] calling  %s @ %i\n",
					  calltime.tv_sec,
					  calltime.tv_nsec,
					  it->func, it->caller);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;

		ret = trace_seq_printf(s, "[%5ld.%09ld] initcall %s "
					  "returned %d after %lld msecs\n",
					  rettime.tv_sec,
					  rettime.tv_nsec,
					  it->func, it->result, it->duration);

		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;
		return TRACE_TYPE_HANDLED;
	}
	return TRACE_TYPE_UNHANDLED;
}

struct tracer boot_tracer __read_mostly =
{
	.name		= "initcall",
	.init		= boot_trace_init,
	.reset		= reset_boot_trace,
	.ctrl_update	= boot_trace_ctrl_update,
	.print_line	= initcall_print_line,
};

void trace_boot(struct boot_trace *it, initcall_t fn)
{
	struct ring_buffer_event *event;
	struct trace_boot *entry;
	struct trace_array_cpu *data;
	unsigned long irq_flags;
	struct trace_array *tr = boot_trace;

	if (!trace_boot_enabled)
		return;

	/* Get its name now since this function could
	 * disappear because it is in the .init section.
	 */
	sprint_symbol(it->func, (unsigned long)fn);
	preempt_disable();
	data = tr->data[smp_processor_id()];

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry),
					 &irq_flags);
	if (!event)
		goto out;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, 0);
	entry->ent.type = TRACE_BOOT;
	entry->initcall = *it;
	ring_buffer_unlock_commit(tr->buffer, event, irq_flags);

	trace_wake_up();

 out:
	preempt_enable();
}
