/*
 * ring buffer based initcalls tracer
 *
 * Copyright (C) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 *
 */

#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>

#include "trace.h"

static struct trace_array *boot_trace;
static int trace_boot_enabled;


/* Should be started after do_pre_smp_initcalls() in init/main.c */
void start_boot_trace(void)
{
	trace_boot_enabled = 1;
}

void stop_boot_trace(struct trace_array *tr)
{
	trace_boot_enabled = 0;
}

static void boot_trace_init(struct trace_array *tr)
{
	int cpu;
	boot_trace = tr;

	trace_boot_enabled = 0;

	for_each_cpu_mask(cpu, cpu_possible_map)
		tracing_reset(tr->data[cpu]);
}

static void boot_trace_ctrl_update(struct trace_array *tr)
{
	if (tr->ctrl)
		start_boot_trace();
	else
		stop_boot_trace(tr);
}

static int initcall_print_line(struct trace_iterator *iter)
{
	int ret = 1;
	struct trace_entry *entry = iter->ent;
	struct boot_trace *it = &entry->field.initcall;
	struct trace_seq *s = &iter->seq;

	if (iter->ent->type == TRACE_BOOT)
		ret = trace_seq_printf(s, "%pF called from %i "
				       "returned %d after %lld msecs\n",
				       it->func, it->caller, it->result,
				       it->duration);
	if (ret)
		return 1;
	return 0;
}

struct tracer boot_tracer __read_mostly =
{
	.name		= "initcall",
	.init		= boot_trace_init,
	.reset		= stop_boot_trace,
	.ctrl_update	= boot_trace_ctrl_update,
	.print_line	= initcall_print_line,
};


void trace_boot(struct boot_trace *it)
{
	struct trace_entry *entry;
	struct trace_array_cpu *data;
	unsigned long irq_flags;
	struct trace_array *tr = boot_trace;

	if (!trace_boot_enabled)
		return;

	preempt_disable();
	data = tr->data[smp_processor_id()];

	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&data->lock);

	entry = tracing_get_trace_entry(tr, data);
	tracing_generic_entry_update(entry, 0);
	entry->type = TRACE_BOOT;
	entry->field.initcall = *it;

	__raw_spin_unlock(&data->lock);
	raw_local_irq_restore(irq_flags);
	trace_wake_up();

	preempt_enable();
}
