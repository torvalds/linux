/*
 * h/w branch tracer for x86 based on BTS
 *
 * Copyright (C) 2008-2009 Intel Corporation.
 * Markus Metzger <markus.t.metzger@gmail.com>, 2008-2009
 */
#include <linux/kallsyms.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/fs.h>

#include <asm/ds.h>

#include "trace_output.h"
#include "trace.h"


#define BTS_BUFFER_SIZE (1 << 13)

static DEFINE_PER_CPU(struct bts_tracer *, tracer);
static DEFINE_PER_CPU(unsigned char[BTS_BUFFER_SIZE], buffer);

#define this_tracer per_cpu(tracer, smp_processor_id())

static int trace_hw_branches_enabled __read_mostly;
static int trace_hw_branches_suspended __read_mostly;
static struct trace_array *hw_branch_trace __read_mostly;


static void bts_trace_init_cpu(int cpu)
{
	per_cpu(tracer, cpu) =
		ds_request_bts_cpu(cpu, per_cpu(buffer, cpu), BTS_BUFFER_SIZE,
				   NULL, (size_t)-1, BTS_KERNEL);

	if (IS_ERR(per_cpu(tracer, cpu)))
		per_cpu(tracer, cpu) = NULL;
}

static int bts_trace_init(struct trace_array *tr)
{
	int cpu;

	hw_branch_trace = tr;
	trace_hw_branches_enabled = 0;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		bts_trace_init_cpu(cpu);

		if (likely(per_cpu(tracer, cpu)))
			trace_hw_branches_enabled = 1;
	}
	trace_hw_branches_suspended = 0;
	put_online_cpus();

	/* If we could not enable tracing on a single cpu, we fail. */
	return trace_hw_branches_enabled ? 0 : -EOPNOTSUPP;
}

static void bts_trace_reset(struct trace_array *tr)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		if (likely(per_cpu(tracer, cpu))) {
			ds_release_bts(per_cpu(tracer, cpu));
			per_cpu(tracer, cpu) = NULL;
		}
	}
	trace_hw_branches_enabled = 0;
	trace_hw_branches_suspended = 0;
	put_online_cpus();
}

static void bts_trace_start(struct trace_array *tr)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		if (likely(per_cpu(tracer, cpu)))
			ds_resume_bts(per_cpu(tracer, cpu));
	trace_hw_branches_suspended = 0;
	put_online_cpus();
}

static void bts_trace_stop(struct trace_array *tr)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		if (likely(per_cpu(tracer, cpu)))
			ds_suspend_bts(per_cpu(tracer, cpu));
	trace_hw_branches_suspended = 1;
	put_online_cpus();
}

static int __cpuinit bts_hotcpu_handler(struct notifier_block *nfb,
				     unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		/* The notification is sent with interrupts enabled. */
		if (trace_hw_branches_enabled) {
			bts_trace_init_cpu(cpu);

			if (trace_hw_branches_suspended &&
			    likely(per_cpu(tracer, cpu)))
				ds_suspend_bts(per_cpu(tracer, cpu));
		}
		break;

	case CPU_DOWN_PREPARE:
		/* The notification is sent with interrupts enabled. */
		if (likely(per_cpu(tracer, cpu))) {
			ds_release_bts(per_cpu(tracer, cpu));
			per_cpu(tracer, cpu) = NULL;
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block bts_hotcpu_notifier __cpuinitdata = {
	.notifier_call = bts_hotcpu_handler
};

static void bts_trace_print_header(struct seq_file *m)
{
	seq_puts(m, "# CPU#        TO  <-  FROM\n");
}

static enum print_line_t bts_trace_print_line(struct trace_iterator *iter)
{
	unsigned long symflags = TRACE_ITER_SYM_OFFSET;
	struct trace_entry *entry = iter->ent;
	struct trace_seq *seq = &iter->seq;
	struct hw_branch_entry *it;

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
	struct ftrace_event_call *call = &event_hw_branch;
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
	if (!filter_check_discard(call, entry, tr->buffer, event))
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
 * pre: tracing must be suspended on the current cpu
 */
static void trace_bts_cpu(void *arg)
{
	struct trace_array *tr = (struct trace_array *)arg;
	const struct bts_trace *trace;
	unsigned char *at;

	if (unlikely(!tr))
		return;

	if (unlikely(atomic_read(&tr->data[raw_smp_processor_id()]->disabled)))
		return;

	if (unlikely(!this_tracer))
		return;

	trace = ds_read_bts(this_tracer);
	if (!trace)
		return;

	for (at = trace->ds.top; (void *)at < trace->ds.end;
	     at += trace->ds.size)
		trace_bts_at(trace, at);

	for (at = trace->ds.begin; (void *)at < trace->ds.top;
	     at += trace->ds.size)
		trace_bts_at(trace, at);
}

static void trace_bts_prepare(struct trace_iterator *iter)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		if (likely(per_cpu(tracer, cpu)))
			ds_suspend_bts(per_cpu(tracer, cpu));
	/*
	 * We need to collect the trace on the respective cpu since ftrace
	 * implicitly adds the record for the current cpu.
	 * Once that is more flexible, we could collect the data from any cpu.
	 */
	on_each_cpu(trace_bts_cpu, iter->tr, 1);

	for_each_online_cpu(cpu)
		if (likely(per_cpu(tracer, cpu)))
			ds_resume_bts(per_cpu(tracer, cpu));
	put_online_cpus();
}

static void trace_bts_close(struct trace_iterator *iter)
{
	tracing_reset_online_cpus(iter->tr);
}

void trace_hw_branch_oops(void)
{
	if (this_tracer) {
		ds_suspend_bts_noirq(this_tracer);
		trace_bts_cpu(hw_branch_trace);
		ds_resume_bts_noirq(this_tracer);
	}
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
	.close		= trace_bts_close,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_hw_branches,
#endif /* CONFIG_FTRACE_SELFTEST */
};

__init static int init_bts_trace(void)
{
	register_hotcpu_notifier(&bts_hotcpu_notifier);
	return register_tracer(&bts_tracer);
}
device_initcall(init_bts_trace);
