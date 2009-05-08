/*
 * h/w branch tracer for x86 based on bts
 *
 * Copyright (C) 2008-2009 Intel Corporation.
 * Markus Metzger <markus.t.metzger@gmail.com>, 2008-2009
 */
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/fs.h>

#include <asm/ds.h>

#include "trace.h"
#include "trace_output.h"


#define SIZEOF_BTS (1 << 13)

/*
 * The tracer lock protects the below per-cpu tracer array.
 * It needs to be held to:
 * - start tracing on all cpus
 * - stop tracing on all cpus
 * - start tracing on a single hotplug cpu
 * - stop tracing on a single hotplug cpu
 * - read the trace from all cpus
 * - read the trace from a single cpu
 */
static DEFINE_SPINLOCK(bts_tracer_lock);
static DEFINE_PER_CPU(struct bts_tracer *, tracer);
static DEFINE_PER_CPU(unsigned char[SIZEOF_BTS], buffer);

#define this_tracer per_cpu(tracer, smp_processor_id())
#define this_buffer per_cpu(buffer, smp_processor_id())

static int __read_mostly trace_hw_branches_enabled;
static struct trace_array *hw_branch_trace __read_mostly;


/*
 * Start tracing on the current cpu.
 * The argument is ignored.
 *
 * pre: bts_tracer_lock must be locked.
 */
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
	spin_lock(&bts_tracer_lock);

	on_each_cpu(bts_trace_start_cpu, NULL, 1);
	trace_hw_branches_enabled = 1;

	spin_unlock(&bts_tracer_lock);
}

/*
 * Stop tracing on the current cpu.
 * The argument is ignored.
 *
 * pre: bts_tracer_lock must be locked.
 */
static void bts_trace_stop_cpu(void *arg)
{
	if (this_tracer) {
		ds_release_bts(this_tracer);
		this_tracer = NULL;
	}
}

static void bts_trace_stop(struct trace_array *tr)
{
	spin_lock(&bts_tracer_lock);

	trace_hw_branches_enabled = 0;
	on_each_cpu(bts_trace_stop_cpu, NULL, 1);

	spin_unlock(&bts_tracer_lock);
}

static int __cpuinit bts_hotcpu_handler(struct notifier_block *nfb,
				     unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	spin_lock(&bts_tracer_lock);

	if (!trace_hw_branches_enabled)
		goto out;

	switch (action) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		smp_call_function_single(cpu, bts_trace_start_cpu, NULL, 1);
		break;
	case CPU_DOWN_PREPARE:
		smp_call_function_single(cpu, bts_trace_stop_cpu, NULL, 1);
		break;
	}

 out:
	spin_unlock(&bts_tracer_lock);
	return NOTIFY_DONE;
}

static struct notifier_block bts_hotcpu_notifier __cpuinitdata = {
	.notifier_call = bts_hotcpu_handler
};

static int bts_trace_init(struct trace_array *tr)
{
	hw_branch_trace = tr;

	bts_trace_start(tr);

	return 0;
}

static void bts_trace_reset(struct trace_array *tr)
{
	bts_trace_stop(tr);
}

static void bts_trace_print_header(struct seq_file *m)
{
	seq_puts(m, "# CPU#        TO  <-  FROM\n");
}

static enum print_line_t bts_trace_print_line(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *seq = &iter->seq;
	struct hw_branch_entry *it;
	unsigned long symflags = TRACE_ITER_SYM_OFFSET;

	trace_assign_type(it, entry);

	if (entry->type == TRACE_HW_BRANCHES) {
		if (trace_seq_printf(seq, "%4d  ", iter->cpu) &&
		    seq_print_ip_sym(seq, it->to, symflags) &&
		    trace_seq_printf(seq, "\t  <-  ") &&
		    seq_print_ip_sym(seq, it->from, symflags) &&
		    trace_seq_printf(seq, "\n"))
			return TRACE_TYPE_HANDLED;
		return TRACE_TYPE_PARTIAL_LINE;;
	}
	return TRACE_TYPE_UNHANDLED;
}

void trace_hw_branch(u64 from, u64 to)
{
	struct trace_array *tr = hw_branch_trace;
	struct ring_buffer_event *event;
	struct hw_branch_entry *entry;
	unsigned long irq1;
	int cpu;

	if (unlikely(!tr))
		return;

	if (unlikely(!trace_hw_branches_enabled))
		return;

	local_irq_save(irq1);
	cpu = raw_smp_processor_id();
	if (atomic_inc_return(&tr->data[cpu]->disabled) != 1)
		goto out;

	event = trace_buffer_lock_reserve(tr, TRACE_HW_BRANCHES,
					  sizeof(*entry), 0, 0);
	if (!event)
		goto out;
	entry	= ring_buffer_event_data(event);
	tracing_generic_entry_update(&entry->ent, 0, from);
	entry->ent.type = TRACE_HW_BRANCHES;
	entry->from = from;
	entry->to   = to;
	trace_buffer_unlock_commit(tr, event, 0, 0);

 out:
	atomic_dec(&tr->data[cpu]->disabled);
	local_irq_restore(irq1);
}

static void trace_bts_at(const struct bts_trace *trace, void *at)
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
		trace_hw_branch(bts.variant.lbr.from, bts.variant.lbr.to);
		break;
	}
}

/*
 * Collect the trace on the current cpu and write it into the ftrace buffer.
 *
 * pre: bts_tracer_lock must be locked
 */
static void trace_bts_cpu(void *arg)
{
	struct trace_array *tr = (struct trace_array *) arg;
	const struct bts_trace *trace;
	unsigned char *at;

	if (unlikely(!tr))
		return;

	if (unlikely(atomic_read(&tr->data[raw_smp_processor_id()]->disabled)))
		return;

	if (unlikely(!this_tracer))
		return;

	ds_suspend_bts(this_tracer);
	trace = ds_read_bts(this_tracer);
	if (!trace)
		goto out;

	for (at = trace->ds.top; (void *)at < trace->ds.end;
	     at += trace->ds.size)
		trace_bts_at(trace, at);

	for (at = trace->ds.begin; (void *)at < trace->ds.top;
	     at += trace->ds.size)
		trace_bts_at(trace, at);

out:
	ds_resume_bts(this_tracer);
}

static void trace_bts_prepare(struct trace_iterator *iter)
{
	spin_lock(&bts_tracer_lock);

	on_each_cpu(trace_bts_cpu, iter->tr, 1);

	spin_unlock(&bts_tracer_lock);
}

static void trace_bts_close(struct trace_iterator *iter)
{
	tracing_reset_online_cpus(iter->tr);
}

void trace_hw_branch_oops(void)
{
	spin_lock(&bts_tracer_lock);

	trace_bts_cpu(hw_branch_trace);

	spin_unlock(&bts_tracer_lock);
}

struct tracer bts_tracer __read_mostly =
{
	.name		= "hw-branch-tracer",
	.init		= bts_trace_init,
	.reset		= bts_trace_reset,
	.print_header	= bts_trace_print_header,
	.print_line	= bts_trace_print_line,
	.start		= bts_trace_start,
	.stop		= bts_trace_stop,
	.open		= trace_bts_prepare,
	.close		= trace_bts_close
};

__init static int init_bts_trace(void)
{
	register_hotcpu_notifier(&bts_hotcpu_notifier);
	return register_tracer(&bts_tracer);
}
device_initcall(init_bts_trace);
