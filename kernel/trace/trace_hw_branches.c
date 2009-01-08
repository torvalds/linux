/*
 * h/w branch tracer for x86 based on bts
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


static void bts_trace_start_cpu(void *arg)
{
	if (this_tracer)
		ds_release_bts(this_tracer);

	this_tracer =
		ds_request_bts(/* task = */ NULL, this_buffer, SIZEOF_BTS,
			       /* ovfl = */ NULL, /* th = */ (size_t)-1,
			       BTS_KERNEL);
	if (IS_ERR(this_tracer)) {
		this_tracer = NULL;
		return;
	}
}

static void bts_trace_start(struct trace_array *tr)
{
	int cpu;

	tracing_reset_online_cpus(tr);

	for_each_cpu(cpu, cpu_possible_mask)
		smp_call_function_single(cpu, bts_trace_start_cpu, NULL, 1);
}

static void bts_trace_stop_cpu(void *arg)
{
	if (this_tracer) {
		ds_release_bts(this_tracer);
		this_tracer = NULL;
	}
}

static void bts_trace_stop(struct trace_array *tr)
{
	int cpu;

	for_each_cpu(cpu, cpu_possible_mask)
		smp_call_function_single(cpu, bts_trace_stop_cpu, NULL, 1);
}

static int bts_trace_init(struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
	bts_trace_start(tr);

	return 0;
}

static void bts_trace_print_header(struct seq_file *m)
{
	seq_puts(m,
		 "# CPU#        FROM                   TO         FUNCTION\n");
	seq_puts(m,
		 "#  |           |                     |             |\n");
}

static enum print_line_t bts_trace_print_line(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *seq = &iter->seq;
	struct hw_branch_entry *it;

	trace_assign_type(it, entry);

	if (entry->type == TRACE_HW_BRANCHES) {
		if (trace_seq_printf(seq, "%4d  ", entry->cpu) &&
		    trace_seq_printf(seq, "0x%016llx -> 0x%016llx ",
				     it->from, it->to) &&
		    (!it->from ||
		     seq_print_ip_sym(seq, it->from, /* sym_flags = */ 0)) &&
		    trace_seq_printf(seq, "\n"))
			return TRACE_TYPE_HANDLED;
		return TRACE_TYPE_PARTIAL_LINE;;
	}
	return TRACE_TYPE_UNHANDLED;
}

void trace_hw_branch(struct trace_array *tr, u64 from, u64 to)
{
	struct ring_buffer_event *event;
	struct hw_branch_entry *entry;
	unsigned long irq;

	event = ring_buffer_lock_reserve(tr->buffer, sizeof(*entry), &irq);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, from);
	entry->ent.type = TRACE_HW_BRANCHES;
	entry->ent.cpu = smp_processor_id();
	entry->from = from;
	entry->to   = to;
	ring_buffer_unlock_commit(tr->buffer, event, irq);
}

static void trace_bts_at(struct trace_array *tr,
			 const struct bts_trace *trace, void *at)
{
	struct bts_struct bts;
	int err = 0;

	WARN_ON_ONCE(!trace->read);
	if (!trace->read)
		return;

	err = trace->read(this_tracer, at, &bts);
	if (err < 0)
		return;

	switch (bts.qualifier) {
	case BTS_BRANCH:
		trace_hw_branch(tr, bts.variant.lbr.from, bts.variant.lbr.to);
		break;
	}
}

static void trace_bts_cpu(void *arg)
{
	struct trace_array *tr = (struct trace_array *) arg;
	const struct bts_trace *trace;
	unsigned char *at;

	if (!this_tracer)
		return;

	ds_suspend_bts(this_tracer);
	trace = ds_read_bts(this_tracer);
	if (!trace)
		goto out;

	for (at = trace->ds.top; (void *)at < trace->ds.end;
	     at += trace->ds.size)
		trace_bts_at(tr, trace, at);

	for (at = trace->ds.begin; (void *)at < trace->ds.top;
	     at += trace->ds.size)
		trace_bts_at(tr, trace, at);

out:
	ds_resume_bts(this_tracer);
}

static void trace_bts_prepare(struct trace_iterator *iter)
{
	int cpu;

	for_each_cpu(cpu, cpu_possible_mask)
		smp_call_function_single(cpu, trace_bts_cpu, iter->tr, 1);
}

struct tracer bts_tracer __read_mostly =
{
	.name		= "hw-branch-tracer",
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
