/*
 * trace irqs off criticall timings
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * From code in the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */
#include <linux/kallsyms.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/fs.h>

#include "trace.h"

static struct trace_array		*irqsoff_trace __read_mostly;
static int				tracer_enabled __read_mostly;

/*
 * Sequence count - we record it when starting a measurement and
 * skip the latency if the sequence has changed - some other section
 * did a maximum and could disturb our measurement with serial console
 * printouts, etc. Truly coinciding maximum latencies should be rare
 * and what happens together happens separately as well, so this doesnt
 * decrease the validity of the maximum found:
 */
static __cacheline_aligned_in_smp	unsigned long max_sequence;

#ifdef CONFIG_FTRACE
/*
 * irqsoff uses its own tracer function to keep the overhead down:
 */
static void notrace
irqsoff_tracer_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;

	if (likely(!tracer_enabled))
		return;

	local_save_flags(flags);

	if (!irqs_disabled_flags(flags))
		return;

	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		ftrace(tr, data, ip, parent_ip, flags);

	atomic_dec(&data->disabled);
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = irqsoff_tracer_call,
};
#endif /* CONFIG_FTRACE */

/*
 * Should this new latency be reported/recorded?
 */
static int notrace report_latency(cycle_t delta)
{
	if (tracing_thresh) {
		if (delta < tracing_thresh)
			return 0;
	} else {
		if (delta <= tracing_max_latency)
			return 0;
	}
	return 1;
}

static void notrace
check_critical_timing(struct trace_array *tr,
		      struct trace_array_cpu *data,
		      unsigned long parent_ip,
		      int cpu)
{
	unsigned long latency, t0, t1;
	cycle_t T0, T1, T2, delta;
	unsigned long flags;

	/*
	 * usecs conversion is slow so we try to delay the conversion
	 * as long as possible:
	 */
	T0 = data->preempt_timestamp;
	T1 = now(cpu);
	delta = T1-T0;

	local_save_flags(flags);

	if (!report_latency(delta))
		goto out;

	ftrace(tr, data, CALLER_ADDR0, parent_ip, flags);
	/*
	 * Update the timestamp, because the trace entry above
	 * might change it (it can only get larger so the latency
	 * is fair to be reported):
	 */
	T2 = now(cpu);

	delta = T2-T0;

	latency = nsecs_to_usecs(delta);

	if (data->critical_sequence != max_sequence)
		goto out;

	tracing_max_latency = delta;
	t0 = nsecs_to_usecs(T0);
	t1 = nsecs_to_usecs(T1);

	data->critical_end = parent_ip;

	update_max_tr_single(tr, current, cpu);

	if (tracing_thresh)
		printk(KERN_INFO "(%16s-%-5d|#%d): %lu us critical section "
		       "violates %lu us threshold.\n"
		       " => started at timestamp %lu: ",
				current->comm, current->pid,
				raw_smp_processor_id(),
				latency, nsecs_to_usecs(tracing_thresh), t0);
	else
		printk(KERN_INFO "(%16s-%-5d|#%d):"
		       " new %lu us maximum-latency "
		       "critical section.\n => started at timestamp %lu: ",
				current->comm, current->pid,
				raw_smp_processor_id(),
				latency, t0);

	print_symbol(KERN_CONT "<%s>\n", data->critical_start);
	printk(KERN_CONT " =>   ended at timestamp %lu: ", t1);
	print_symbol(KERN_CONT "<%s>\n", data->critical_end);
	dump_stack();
	t1 = nsecs_to_usecs(now(cpu));
	printk(KERN_CONT " =>   dump-end timestamp %lu\n\n", t1);

	max_sequence++;

out:
	data->critical_sequence = max_sequence;
	data->preempt_timestamp = now(cpu);
	tracing_reset(data);
	ftrace(tr, data, CALLER_ADDR0, parent_ip, flags);
}

static inline void notrace
start_critical_timing(unsigned long ip, unsigned long parent_ip)
{
	int cpu;
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;

	if (likely(!tracer_enabled))
		return;

	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (unlikely(!data) || unlikely(!data->trace) ||
	    data->critical_start || atomic_read(&data->disabled))
		return;

	atomic_inc(&data->disabled);

	data->critical_sequence = max_sequence;
	data->preempt_timestamp = now(cpu);
	data->critical_start = parent_ip;
	tracing_reset(data);

	local_save_flags(flags);
	ftrace(tr, data, ip, parent_ip, flags);

	atomic_dec(&data->disabled);
}

static inline void notrace
stop_critical_timing(unsigned long ip, unsigned long parent_ip)
{
	int cpu;
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;

	if (likely(!tracer_enabled))
		return;

	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (unlikely(!data) || unlikely(!data->trace) ||
	    !data->critical_start || atomic_read(&data->disabled))
		return;

	atomic_inc(&data->disabled);
	local_save_flags(flags);
	ftrace(tr, data, ip, parent_ip, flags);
	check_critical_timing(tr, data, parent_ip, cpu);
	data->critical_start = 0;
	atomic_dec(&data->disabled);
}

void notrace start_critical_timings(void)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}

void notrace stop_critical_timings(void)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}

#ifdef CONFIG_PROVE_LOCKING
void notrace time_hardirqs_on(unsigned long a0, unsigned long a1)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		stop_critical_timing(a0, a1);
}

void notrace time_hardirqs_off(unsigned long a0, unsigned long a1)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		start_critical_timing(a0, a1);
}

#else /* !CONFIG_PROVE_LOCKING */

/*
 * Stubs:
 */

void early_boot_irqs_off(void)
{
}

void early_boot_irqs_on(void)
{
}

void trace_softirqs_on(unsigned long ip)
{
}

void trace_softirqs_off(unsigned long ip)
{
}

inline void print_irqtrace_events(struct task_struct *curr)
{
}

/*
 * We are only interested in hardirq on/off events:
 */
void notrace trace_hardirqs_on(void)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}
EXPORT_SYMBOL(trace_hardirqs_on);

void notrace trace_hardirqs_off(void)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}
EXPORT_SYMBOL(trace_hardirqs_off);

void notrace trace_hardirqs_on_caller(unsigned long caller_addr)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		stop_critical_timing(CALLER_ADDR0, caller_addr);
}
EXPORT_SYMBOL(trace_hardirqs_on_caller);

void notrace trace_hardirqs_off_caller(unsigned long caller_addr)
{
	unsigned long flags;

	local_save_flags(flags);

	if (irqs_disabled_flags(flags))
		start_critical_timing(CALLER_ADDR0, caller_addr);
}
EXPORT_SYMBOL(trace_hardirqs_off_caller);

#endif /* CONFIG_PROVE_LOCKING */

static void start_irqsoff_tracer(struct trace_array *tr)
{
	tracer_enabled = 1;
	register_ftrace_function(&trace_ops);
}

static void stop_irqsoff_tracer(struct trace_array *tr)
{
	unregister_ftrace_function(&trace_ops);
	tracer_enabled = 0;
}

static void irqsoff_tracer_init(struct trace_array *tr)
{
	irqsoff_trace = tr;
	/* make sure that the tracer is visibel */
	smp_wmb();

	if (tr->ctrl)
		start_irqsoff_tracer(tr);
}

static void irqsoff_tracer_reset(struct trace_array *tr)
{
	if (tr->ctrl)
		stop_irqsoff_tracer(tr);
}

static void irqsoff_tracer_ctrl_update(struct trace_array *tr)
{
	if (tr->ctrl)
		start_irqsoff_tracer(tr);
	else
		stop_irqsoff_tracer(tr);
}

static void notrace irqsoff_tracer_open(struct trace_iterator *iter)
{
	/* stop the trace while dumping */
	if (iter->tr->ctrl)
		stop_irqsoff_tracer(iter->tr);
}

static void notrace irqsoff_tracer_close(struct trace_iterator *iter)
{
	if (iter->tr->ctrl)
		start_irqsoff_tracer(iter->tr);
}

static struct tracer irqsoff_tracer __read_mostly =
{
	.name		= "irqsoff",
	.init		= irqsoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.open		= irqsoff_tracer_open,
	.close		= irqsoff_tracer_close,
	.ctrl_update	= irqsoff_tracer_ctrl_update,
	.print_max	= 1,
};

__init static int init_irqsoff_tracer(void)
{
	register_tracer(&irqsoff_tracer);

	return 0;
}
device_initcall(init_irqsoff_tracer);
