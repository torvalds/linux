/*
 * trace irqs off critical timings
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

static DEFINE_PER_CPU(int, tracing_cpu);

static DEFINE_SPINLOCK(max_trace_lock);

enum {
	TRACER_IRQS_OFF		= (1 << 1),
	TRACER_PREEMPT_OFF	= (1 << 2),
};

static int trace_type __read_mostly;

static int save_lat_flag;

static void stop_irqsoff_tracer(struct trace_array *tr, int graph);
static int start_irqsoff_tracer(struct trace_array *tr, int graph);

#ifdef CONFIG_PREEMPT_TRACER
static inline int
preempt_trace(void)
{
	return ((trace_type & TRACER_PREEMPT_OFF) && preempt_count());
}
#else
# define preempt_trace() (0)
#endif

#ifdef CONFIG_IRQSOFF_TRACER
static inline int
irq_trace(void)
{
	return ((trace_type & TRACER_IRQS_OFF) &&
		irqs_disabled());
}
#else
# define irq_trace() (0)
#endif

#define TRACE_DISPLAY_GRAPH	1

static struct tracer_opt trace_opts[] = {
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* display latency trace as call graph */
	{ TRACER_OPT(display-graph, TRACE_DISPLAY_GRAPH) },
#endif
	{ } /* Empty entry */
};

static struct tracer_flags tracer_flags = {
	.val  = 0,
	.opts = trace_opts,
};

#define is_graph() (tracer_flags.val & TRACE_DISPLAY_GRAPH)

/*
 * Sequence count - we record it when starting a measurement and
 * skip the latency if the sequence has changed - some other section
 * did a maximum and could disturb our measurement with serial console
 * printouts, etc. Truly coinciding maximum latencies should be rare
 * and what happens together happens separately as well, so this doesnt
 * decrease the validity of the maximum found:
 */
static __cacheline_aligned_in_smp	unsigned long max_sequence;

#ifdef CONFIG_FUNCTION_TRACER
/*
 * Prologue for the preempt and irqs off function tracers.
 *
 * Returns 1 if it is OK to continue, and data->disabled is
 *            incremented.
 *         0 if the trace is to be ignored, and data->disabled
 *            is kept the same.
 *
 * Note, this function is also used outside this ifdef but
 *  inside the #ifdef of the function graph tracer below.
 *  This is OK, since the function graph tracer is
 *  dependent on the function tracer.
 */
static int func_prolog_dec(struct trace_array *tr,
			   struct trace_array_cpu **data,
			   unsigned long *flags)
{
	long disabled;
	int cpu;

	/*
	 * Does not matter if we preempt. We test the flags
	 * afterward, to see if irqs are disabled or not.
	 * If we preempt and get a false positive, the flags
	 * test will fail.
	 */
	cpu = raw_smp_processor_id();
	if (likely(!per_cpu(tracing_cpu, cpu)))
		return 0;

	local_save_flags(*flags);
	/* slight chance to get a false positive on tracing_cpu */
	if (!irqs_disabled_flags(*flags))
		return 0;

	*data = tr->data[cpu];
	disabled = atomic_inc_return(&(*data)->disabled);

	if (likely(disabled == 1))
		return 1;

	atomic_dec(&(*data)->disabled);

	return 0;
}

/*
 * irqsoff uses its own tracer function to keep the overhead down:
 */
static void
irqsoff_tracer_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;

	if (!func_prolog_dec(tr, &data, &flags))
		return;

	trace_function(tr, ip, parent_ip, flags, preempt_count());

	atomic_dec(&data->disabled);
}

static struct ftrace_ops trace_ops __read_mostly =
{
	.func = irqsoff_tracer_call,
};
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static int irqsoff_set_flag(u32 old_flags, u32 bit, int set)
{
	int cpu;

	if (!(bit & TRACE_DISPLAY_GRAPH))
		return -EINVAL;

	if (!(is_graph() ^ set))
		return 0;

	stop_irqsoff_tracer(irqsoff_trace, !set);

	for_each_possible_cpu(cpu)
		per_cpu(tracing_cpu, cpu) = 0;

	tracing_max_latency = 0;
	tracing_reset_online_cpus(irqsoff_trace);

	return start_irqsoff_tracer(irqsoff_trace, set);
}

static int irqsoff_graph_entry(struct ftrace_graph_ent *trace)
{
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int ret;
	int pc;

	if (!func_prolog_dec(tr, &data, &flags))
		return 0;

	pc = preempt_count();
	ret = __trace_graph_entry(tr, trace, flags, pc);
	atomic_dec(&data->disabled);

	return ret;
}

static void irqsoff_graph_return(struct ftrace_graph_ret *trace)
{
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int pc;

	if (!func_prolog_dec(tr, &data, &flags))
		return;

	pc = preempt_count();
	__trace_graph_return(tr, trace, flags, pc);
	atomic_dec(&data->disabled);
}

static void irqsoff_trace_open(struct trace_iterator *iter)
{
	if (is_graph())
		graph_trace_open(iter);

}

static void irqsoff_trace_close(struct trace_iterator *iter)
{
	if (iter->private)
		graph_trace_close(iter);
}

#define GRAPH_TRACER_FLAGS (TRACE_GRAPH_PRINT_CPU | \
			    TRACE_GRAPH_PRINT_PROC)

static enum print_line_t irqsoff_print_line(struct trace_iterator *iter)
{
	/*
	 * In graph mode call the graph tracer output function,
	 * otherwise go with the TRACE_FN event handler
	 */
	if (is_graph())
		return print_graph_function_flags(iter, GRAPH_TRACER_FLAGS);

	return TRACE_TYPE_UNHANDLED;
}

static void irqsoff_print_header(struct seq_file *s)
{
	if (is_graph())
		print_graph_headers_flags(s, GRAPH_TRACER_FLAGS);
	else
		trace_default_header(s);
}

static void
__trace_function(struct trace_array *tr,
		 unsigned long ip, unsigned long parent_ip,
		 unsigned long flags, int pc)
{
	if (is_graph())
		trace_graph_function(tr, ip, parent_ip, flags, pc);
	else
		trace_function(tr, ip, parent_ip, flags, pc);
}

#else
#define __trace_function trace_function

static int irqsoff_set_flag(u32 old_flags, u32 bit, int set)
{
	return -EINVAL;
}

static int irqsoff_graph_entry(struct ftrace_graph_ent *trace)
{
	return -1;
}

static enum print_line_t irqsoff_print_line(struct trace_iterator *iter)
{
	return TRACE_TYPE_UNHANDLED;
}

static void irqsoff_graph_return(struct ftrace_graph_ret *trace) { }
static void irqsoff_print_header(struct seq_file *s) { }
static void irqsoff_trace_open(struct trace_iterator *iter) { }
static void irqsoff_trace_close(struct trace_iterator *iter) { }
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

/*
 * Should this new latency be reported/recorded?
 */
static int report_latency(cycle_t delta)
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

static void
check_critical_timing(struct trace_array *tr,
		      struct trace_array_cpu *data,
		      unsigned long parent_ip,
		      int cpu)
{
	cycle_t T0, T1, delta;
	unsigned long flags;
	int pc;

	T0 = data->preempt_timestamp;
	T1 = ftrace_now(cpu);
	delta = T1-T0;

	local_save_flags(flags);

	pc = preempt_count();

	if (!report_latency(delta))
		goto out;

	spin_lock_irqsave(&max_trace_lock, flags);

	/* check if we are still the max latency */
	if (!report_latency(delta))
		goto out_unlock;

	__trace_function(tr, CALLER_ADDR0, parent_ip, flags, pc);
	/* Skip 5 functions to get to the irq/preempt enable function */
	__trace_stack(tr, flags, 5, pc);

	if (data->critical_sequence != max_sequence)
		goto out_unlock;

	data->critical_end = parent_ip;

	if (likely(!is_tracing_stopped())) {
		tracing_max_latency = delta;
		update_max_tr_single(tr, current, cpu);
	}

	max_sequence++;

out_unlock:
	spin_unlock_irqrestore(&max_trace_lock, flags);

out:
	data->critical_sequence = max_sequence;
	data->preempt_timestamp = ftrace_now(cpu);
	__trace_function(tr, CALLER_ADDR0, parent_ip, flags, pc);
}

static inline void
start_critical_timing(unsigned long ip, unsigned long parent_ip)
{
	int cpu;
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;

	if (likely(!tracer_enabled))
		return;

	cpu = raw_smp_processor_id();

	if (per_cpu(tracing_cpu, cpu))
		return;

	data = tr->data[cpu];

	if (unlikely(!data) || atomic_read(&data->disabled))
		return;

	atomic_inc(&data->disabled);

	data->critical_sequence = max_sequence;
	data->preempt_timestamp = ftrace_now(cpu);
	data->critical_start = parent_ip ? : ip;

	local_save_flags(flags);

	__trace_function(tr, ip, parent_ip, flags, preempt_count());

	per_cpu(tracing_cpu, cpu) = 1;

	atomic_dec(&data->disabled);
}

static inline void
stop_critical_timing(unsigned long ip, unsigned long parent_ip)
{
	int cpu;
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;

	cpu = raw_smp_processor_id();
	/* Always clear the tracing cpu on stopping the trace */
	if (unlikely(per_cpu(tracing_cpu, cpu)))
		per_cpu(tracing_cpu, cpu) = 0;
	else
		return;

	if (!tracer_enabled)
		return;

	data = tr->data[cpu];

	if (unlikely(!data) ||
	    !data->critical_start || atomic_read(&data->disabled))
		return;

	atomic_inc(&data->disabled);

	local_save_flags(flags);
	__trace_function(tr, ip, parent_ip, flags, preempt_count());
	check_critical_timing(tr, data, parent_ip ? : ip, cpu);
	data->critical_start = 0;
	atomic_dec(&data->disabled);
}

/* start and stop critical timings used to for stoppage (in idle) */
void start_critical_timings(void)
{
	if (preempt_trace() || irq_trace())
		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}
EXPORT_SYMBOL_GPL(start_critical_timings);

void stop_critical_timings(void)
{
	if (preempt_trace() || irq_trace())
		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}
EXPORT_SYMBOL_GPL(stop_critical_timings);

#ifdef CONFIG_IRQSOFF_TRACER
#ifdef CONFIG_PROVE_LOCKING
void time_hardirqs_on(unsigned long a0, unsigned long a1)
{
	if (!preempt_trace() && irq_trace())
		stop_critical_timing(a0, a1);
}

void time_hardirqs_off(unsigned long a0, unsigned long a1)
{
	if (!preempt_trace() && irq_trace())
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
void trace_hardirqs_on(void)
{
	if (!preempt_trace() && irq_trace())
		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}
EXPORT_SYMBOL(trace_hardirqs_on);

void trace_hardirqs_off(void)
{
	if (!preempt_trace() && irq_trace())
		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}
EXPORT_SYMBOL(trace_hardirqs_off);

void trace_hardirqs_on_caller(unsigned long caller_addr)
{
	if (!preempt_trace() && irq_trace())
		stop_critical_timing(CALLER_ADDR0, caller_addr);
}
EXPORT_SYMBOL(trace_hardirqs_on_caller);

void trace_hardirqs_off_caller(unsigned long caller_addr)
{
	if (!preempt_trace() && irq_trace())
		start_critical_timing(CALLER_ADDR0, caller_addr);
}
EXPORT_SYMBOL(trace_hardirqs_off_caller);

#endif /* CONFIG_PROVE_LOCKING */
#endif /*  CONFIG_IRQSOFF_TRACER */

#ifdef CONFIG_PREEMPT_TRACER
void trace_preempt_on(unsigned long a0, unsigned long a1)
{
	if (preempt_trace())
		stop_critical_timing(a0, a1);
}

void trace_preempt_off(unsigned long a0, unsigned long a1)
{
	if (preempt_trace())
		start_critical_timing(a0, a1);
}
#endif /* CONFIG_PREEMPT_TRACER */

static int start_irqsoff_tracer(struct trace_array *tr, int graph)
{
	int ret = 0;

	if (!graph)
		ret = register_ftrace_function(&trace_ops);
	else
		ret = register_ftrace_graph(&irqsoff_graph_return,
					    &irqsoff_graph_entry);

	if (!ret && tracing_is_enabled())
		tracer_enabled = 1;
	else
		tracer_enabled = 0;

	return ret;
}

static void stop_irqsoff_tracer(struct trace_array *tr, int graph)
{
	tracer_enabled = 0;

	if (!graph)
		unregister_ftrace_function(&trace_ops);
	else
		unregister_ftrace_graph();
}

static void __irqsoff_tracer_init(struct trace_array *tr)
{
	save_lat_flag = trace_flags & TRACE_ITER_LATENCY_FMT;
	trace_flags |= TRACE_ITER_LATENCY_FMT;

	tracing_max_latency = 0;
	irqsoff_trace = tr;
	/* make sure that the tracer is visible */
	smp_wmb();
	tracing_reset_online_cpus(tr);

	if (start_irqsoff_tracer(tr, is_graph()))
		printk(KERN_ERR "failed to start irqsoff tracer\n");
}

static void irqsoff_tracer_reset(struct trace_array *tr)
{
	stop_irqsoff_tracer(tr, is_graph());

	if (!save_lat_flag)
		trace_flags &= ~TRACE_ITER_LATENCY_FMT;
}

static void irqsoff_tracer_start(struct trace_array *tr)
{
	tracer_enabled = 1;
}

static void irqsoff_tracer_stop(struct trace_array *tr)
{
	tracer_enabled = 0;
}

#ifdef CONFIG_IRQSOFF_TRACER
static int irqsoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_IRQS_OFF;

	__irqsoff_tracer_init(tr);
	return 0;
}
static struct tracer irqsoff_tracer __read_mostly =
{
	.name		= "irqsoff",
	.init		= irqsoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.start		= irqsoff_tracer_start,
	.stop		= irqsoff_tracer_stop,
	.print_max	= 1,
	.print_header   = irqsoff_print_header,
	.print_line     = irqsoff_print_line,
	.flags		= &tracer_flags,
	.set_flag	= irqsoff_set_flag,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_irqsoff,
#endif
	.open           = irqsoff_trace_open,
	.close          = irqsoff_trace_close,
	.use_max_tr	= 1,
};
# define register_irqsoff(trace) register_tracer(&trace)
#else
# define register_irqsoff(trace) do { } while (0)
#endif

#ifdef CONFIG_PREEMPT_TRACER
static int preemptoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_PREEMPT_OFF;

	__irqsoff_tracer_init(tr);
	return 0;
}

static struct tracer preemptoff_tracer __read_mostly =
{
	.name		= "preemptoff",
	.init		= preemptoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.start		= irqsoff_tracer_start,
	.stop		= irqsoff_tracer_stop,
	.print_max	= 1,
	.print_header   = irqsoff_print_header,
	.print_line     = irqsoff_print_line,
	.flags		= &tracer_flags,
	.set_flag	= irqsoff_set_flag,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_preemptoff,
#endif
	.open		= irqsoff_trace_open,
	.close		= irqsoff_trace_close,
	.use_max_tr	= 1,
};
# define register_preemptoff(trace) register_tracer(&trace)
#else
# define register_preemptoff(trace) do { } while (0)
#endif

#if defined(CONFIG_IRQSOFF_TRACER) && \
	defined(CONFIG_PREEMPT_TRACER)

static int preemptirqsoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_IRQS_OFF | TRACER_PREEMPT_OFF;

	__irqsoff_tracer_init(tr);
	return 0;
}

static struct tracer preemptirqsoff_tracer __read_mostly =
{
	.name		= "preemptirqsoff",
	.init		= preemptirqsoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.start		= irqsoff_tracer_start,
	.stop		= irqsoff_tracer_stop,
	.print_max	= 1,
	.print_header   = irqsoff_print_header,
	.print_line     = irqsoff_print_line,
	.flags		= &tracer_flags,
	.set_flag	= irqsoff_set_flag,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_preemptirqsoff,
#endif
	.open		= irqsoff_trace_open,
	.close		= irqsoff_trace_close,
	.use_max_tr	= 1,
};

# define register_preemptirqsoff(trace) register_tracer(&trace)
#else
# define register_preemptirqsoff(trace) do { } while (0)
#endif

__init static int init_irqsoff_tracer(void)
{
	register_irqsoff(irqsoff_tracer);
	register_preemptoff(preemptoff_tracer);
	register_preemptirqsoff(preemptirqsoff_tracer);

	return 0;
}
device_initcall(init_irqsoff_tracer);
