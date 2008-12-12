/*
 * BTS tracer
 *
 * Copyright (C) 2008 Markus Metzger <markus.t.metzger@gmail.com>
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>

#include <asm/ds.h>

#include "trace.h"


#define SIZEOF_BTS (1 << 13)

static DEFINE_PER_CPU(struct bts_tracer *, tracer);
static DEFINE_PER_CPU(unsigned char[SIZEOF_BTS], buffer);

#define this_tracer per_cpu(tracer, smp_processor_id())
#define this_buffer per_cpu(buffer, smp_processor_id())


/*
 * Information to interpret a BTS record.
 * This will go into an in-kernel BTS interface.
 */
static unsigned char sizeof_field;
static unsigned long debugctl_mask;

#define sizeof_bts (3 * sizeof_field)

static void bts_trace_cpuinit(struct cpuinfo_x86 *c)
{
	switch (c->x86) {
	case 0x6:
		switch (c->x86_model) {
		case 0x0 ... 0xC:
			break;
		case 0xD:
		case 0xE: /* Pentium M */
			sizeof_field = sizeof(long);
			debugctl_mask = (1<<6)|(1<<7);
			break;
		default:
			sizeof_field = 8;
			debugctl_mask = (1<<6)|(1<<7);
			break;
		}
		break;
	case 0xF:
		switch (c->x86_model) {
		case 0x0:
		case 0x1:
		case 0x2: /* Netburst */
			sizeof_field = sizeof(long);
			debugctl_mask = (1<<2)|(1<<3);
			break;
		default:
			/* sorry, don't know about them */
			break;
		}
		break;
	default:
		/* sorry, don't know about them */
		break;
	}
}

static inline void bts_enable(void)
{
	unsigned long debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl | debugctl_mask);
}

static inline void bts_disable(void)
{
	unsigned long debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl & ~debugctl_mask);
}

static void bts_trace_reset(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);
}

static void bts_trace_start_cpu(void *arg)
{
	this_tracer =
		ds_request_bts(/* task = */ NULL, this_buffer, SIZEOF_BTS,
			       /* ovfl = */ NULL, /* th = */ (size_t)-1);
	if (IS_ERR(this_tracer)) {
		this_tracer = NULL;
		return;
	}

	bts_enable();
}

static void bts_trace_start(struct trace_array *tr)
{
	int cpu;

	bts_trace_reset(tr);

	for_each_cpu_mask(cpu, cpu_possible_map)
		smp_call_function_single(cpu, bts_trace_start_cpu, NULL, 1);
}

static void bts_trace_stop_cpu(void *arg)
{
	if (this_tracer) {
		bts_disable();

		ds_release_bts(this_tracer);
		this_tracer = NULL;
	}
}

static void bts_trace_stop(struct trace_array *tr)
{
	int cpu;

	for_each_cpu_mask(cpu, cpu_possible_map)
		smp_call_function_single(cpu, bts_trace_stop_cpu, NULL, 1);
}

static int bts_trace_init(struct trace_array *tr)
{
	bts_trace_cpuinit(&boot_cpu_data);
	bts_trace_reset(tr);
	bts_trace_start(tr);

	return 0;
}

static void bts_trace_print_header(struct seq_file *m)
{
#ifdef __i386__
	seq_puts(m, "# CPU#    FROM           TO     FUNCTION\n");
	seq_puts(m, "#  |       |             |         |\n");
#else
	seq_puts(m,
		 "# CPU#        FROM                   TO         FUNCTION\n");
	seq_puts(m,
		 "#  |           |                     |             |\n");
#endif
}

static enum print_line_t bts_trace_print_line(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *seq = &iter->seq;
	struct bts_entry *it;

	trace_assign_type(it, entry);

	if (entry->type == TRACE_BTS) {
		int ret;
#ifdef CONFIG_KALLSYMS
		char function[KSYM_SYMBOL_LEN];
		sprint_symbol(function, it->from);
#else
		char *function = "<unknown>";
#endif

		ret = trace_seq_printf(seq, "%4d  0x%lx -> 0x%lx [%s]\n",
				       entry->cpu, it->from, it->to, function);
		if (!ret)
			return TRACE_TYPE_PARTIAL_LINE;;
		return TRACE_TYPE_HANDLED;
	}
	return TRACE_TYPE_UNHANDLED;
}

void trace_bts(struct trace_array *tr, unsigned long from, unsigned long to)
{
	struct ring_buffer_event *event;
	struct bts_entry *entry;
	unsigned long irq;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry), &irq);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, from);
	entry->ent.type = TRACE_BTS;
	entry->ent.cpu = smp_processor_id();
	entry->from = from;
	entry->to   = to;
	ring_buffer_unlock_commit(tr->buffer, event, irq);
}

static void trace_bts_at(struct trace_array *tr, size_t index)
{
	const void *raw = NULL;
	unsigned long from, to;
	int err;

	err = ds_access_bts(this_tracer, index, &raw);
	if (err < 0)
		return;

	from = *(const unsigned long *)raw;
	to = *(const unsigned long *)((const char *)raw + sizeof_field);

	trace_bts(tr, from, to);
}

static void trace_bts_cpu(void *arg)
{
	struct trace_array *tr = (struct trace_array *) arg;
	size_t index = 0, end = 0, i;
	int err;

	if (!this_tracer)
		return;

	bts_disable();

	err = ds_get_bts_index(this_tracer, &index);
	if (err < 0)
		goto out;

	err = ds_get_bts_end(this_tracer, &end);
	if (err < 0)
		goto out;

	for (i = index; i < end; i++)
		trace_bts_at(tr, i);

	for (i = 0; i < index; i++)
		trace_bts_at(tr, i);

out:
	bts_enable();
}

static void trace_bts_prepare(struct trace_iterator *iter)
{
	int cpu;

	for_each_cpu_mask(cpu, cpu_possible_map)
		smp_call_function_single(cpu, trace_bts_cpu, iter->tr, 1);
}

struct tracer bts_tracer __read_mostly =
{
	.name		= "bts",
	.init		= bts_trace_init,
	.reset		= bts_trace_stop,
	.print_header	= bts_trace_print_header,
	.print_line	= bts_trace_print_line,
	.start		= bts_trace_start,
	.stop		= bts_trace_stop,
	.open		= trace_bts_prepare
};

__init static int init_bts_trace(void)
{
	return register_tracer(&bts_tracer);
}
device_initcall(init_bts_trace);
