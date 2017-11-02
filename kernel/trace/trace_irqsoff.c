/*
 * trace irqs off critical timings
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * From code in the latency_tracer, that is:
 *
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 Nadia Yvette Chambers
 */
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>

#include "trace.h"

#define CREATE_TRACE_POINTS
#include <trace/events/preemptirq.h>

#if defined(CONFIG_IRQSOFF_TRACER) || defined(CONFIG_PREEMPT_TRACER)
static struct trace_array		*irqsoff_trace __read_mostly;
static int				tracer_enabled __read_mostly;

static DEFINE_PER_CPU(int, tracing_cpu);

static DEFINE_RAW_SPINLOCK(max_trace_lock);

enum {
	TRACER_IRQS_OFF		= (1 << 1),
	TRACER_PREEMPT_OFF	= (1 << 2),
};

static int trace_type __read_mostly;

static int save_flags;

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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static int irqsoff_display_graph(struct trace_array *tr, int set);
# define is_graph(tr) ((tr)->trace_flags & TRACE_ITER_DISPLAY_GRAPH)
#else
static inline int irqsoff_display_graph(struct trace_array *tr, int set)
{
	return -EINVAL;
}
# define is_graph(tr) false
#endif

/*
 * Sequence count - we record it when starting a measurement and
 * skip the latency if the sequence has changed - some other section
 * did a maximum and could disturb our measurement with serial console
 * printouts, etc. Truly coinciding maximum latencies should be rare
 * and what happens together happens separately as well, so this doesn't
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
	/*
	 * Slight chance to get a false positive on tracing_cpu,
	 * although I'm starting to think there isn't a chance.
	 * Leave this for now just to be paranoid.
	 */
	if (!irqs_disabled_flags(*flags) && !preempt_count())
		return 0;

	*data = per_cpu_ptr(tr->trace_buffer.data, cpu);
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
irqsoff_tracer_call(unsigned long ip, unsigned long parent_ip,
		    struct ftrace_ops *op, struct pt_regs *pt_regs)
{
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;

	if (!func_prolog_dec(tr, &data, &flags))
		return;

	trace_function(tr, ip, parent_ip, flags, preempt_count());

	atomic_dec(&data->disabled);
}
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static int irqsoff_display_graph(struct trace_array *tr, int set)
{
	int cpu;

	if (!(is_graph(tr) ^ set))
		return 0;

	stop_irqsoff_tracer(irqsoff_trace, !set);

	for_each_possible_cpu(cpu)
		per_cpu(tracing_cpu, cpu) = 0;

	tr->max_latency = 0;
	tracing_reset_online_cpus(&irqsoff_trace->trace_buffer);

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
	if (is_graph(iter->tr))
		graph_trace_open(iter);

}

static void irqsoff_trace_close(struct trace_iterator *iter)
{
	if (iter->private)
		graph_trace_close(iter);
}

#define GRAPH_TRACER_FLAGS (TRACE_GRAPH_PRINT_CPU | \
			    TRACE_GRAPH_PRINT_PROC | \
			    TRACE_GRAPH_PRINT_ABS_TIME | \
			    TRACE_GRAPH_PRINT_DURATION)

static enum print_line_t irqsoff_print_line(struct trace_iterator *iter)
{
	/*
	 * In graph mode call the graph tracer output function,
	 * otherwise go with the TRACE_FN event handler
	 */
	if (is_graph(iter->tr))
		return print_graph_function_flags(iter, GRAPH_TRACER_FLAGS);

	return TRACE_TYPE_UNHANDLED;
}

static void irqsoff_print_header(struct seq_file *s)
{
	struct trace_array *tr = irqsoff_trace;

	if (is_graph(tr))
		print_graph_headers_flags(s, GRAPH_TRACER_FLAGS);
	else
		trace_default_header(s);
}

static void
__trace_function(struct trace_array *tr,
		 unsigned long ip, unsigned long parent_ip,
		 unsigned long flags, int pc)
{
	if (is_graph(tr))
		trace_graph_function(tr, ip, parent_ip, flags, pc);
	else
		trace_function(tr, ip, parent_ip, flags, pc);
}

#else
#define __trace_function trace_function

#ifdef CONFIG_FUNCTION_TRACER
static int irqsoff_graph_entry(struct ftrace_graph_ent *trace)
{
	return -1;
}
#endif

static enum print_line_t irqsoff_print_line(struct trace_iterator *iter)
{
	return TRACE_TYPE_UNHANDLED;
}

static void irqsoff_trace_open(struct trace_iterator *iter) { }
static void irqsoff_trace_close(struct trace_iterator *iter) { }

#ifdef CONFIG_FUNCTION_TRACER
static void irqsoff_graph_return(struct ftrace_graph_ret *trace) { }
static void irqsoff_print_header(struct seq_file *s)
{
	trace_default_header(s);
}
#else
static void irqsoff_print_header(struct seq_file *s)
{
	trace_latency_header(s);
}
#endif /* CONFIG_FUNCTION_TRACER */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

/*
 * Should this new latency be reported/recorded?
 */
static bool report_latency(struct trace_array *tr, cycle_t delta)
{
	if (tracing_thresh) {
		if (delta < tracing_thresh)
			return false;
	} else {
		if (delta <= tr->max_latency)
			return false;
	}
	return true;
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

	if (!report_latency(tr, delta))
		goto out;

	raw_spin_lock_irqsave(&max_trace_lock, flags);

	/* check if we are still the max latency */
	if (!report_latency(tr, delta))
		goto out_unlock;

	__trace_function(tr, CALLER_ADDR0, parent_ip, flags, pc);
	/* Skip 5 functions to get to the irq/preempt enable function */
	__trace_stack(tr, flags, 5, pc);

	if (data->critical_sequence != max_sequence)
		goto out_unlock;

	data->critical_end = parent_ip;

	if (likely(!is_tracing_stopped())) {
		tr->max_latency = delta;
		update_max_tr_single(tr, current, cpu);
	}

	max_sequence++;

out_unlock:
	raw_spin_unlock_irqrestore(&max_trace_lock, flags);

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

	if (!tracer_enabled || !tracing_is_enabled())
		return;

	cpu = raw_smp_processor_id();

	if (per_cpu(tracing_cpu, cpu))
		return;

	data = per_cpu_ptr(tr->trace_buffer.data, cpu);

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

	if (!tracer_enabled || !tracing_is_enabled())
		return;

	data = per_cpu_ptr(tr->trace_buffer.data, cpu);

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
 * We are only interested in hardirq on/off events:
 */
static inline void tracer_hardirqs_on(void)
{
	if (!preempt_trace() && irq_trace())
		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}

static inline void tracer_hardirqs_off(void)
{
	if (!preempt_trace() && irq_trace())
		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
}

static inline void tracer_hardirqs_on_caller(unsigned long caller_addr)
{
	if (!preempt_trace() && irq_trace())
		stop_critical_timing(CALLER_ADDR0, caller_addr);
}

static inline void tracer_hardirqs_off_caller(unsigned long caller_addr)
{
	if (!preempt_trace() && irq_trace())
		start_critical_timing(CALLER_ADDR0, caller_addr);
}

#endif /* CONFIG_PROVE_LOCKING */
#endif /*  CONFIG_IRQSOFF_TRACER */

#ifdef CONFIG_PREEMPT_TRACER
static inline void tracer_preempt_on(unsigned long a0, unsigned long a1)
{
	if (preempt_trace() && !irq_trace())
		stop_critical_timing(a0, a1);
}

static inline void tracer_preempt_off(unsigned long a0, unsigned long a1)
{
	if (preempt_trace() && !irq_trace())
		start_critical_timing(a0, a1);
}
#endif /* CONFIG_PREEMPT_TRACER */

#ifdef CONFIG_FUNCTION_TRACER
static bool function_enabled;

static int register_irqsoff_function(struct trace_array *tr, int graph, int set)
{
	int ret;

	/* 'set' is set if TRACE_ITER_FUNCTION is about to be set */
	if (function_enabled || (!set && !(tr->trace_flags & TRACE_ITER_FUNCTION)))
		return 0;

	if (graph)
		ret = register_ftrace_graph(&irqsoff_graph_return,
					    &irqsoff_graph_entry);
	else
		ret = register_ftrace_function(tr->ops);

	if (!ret)
		function_enabled = true;

	return ret;
}

static void unregister_irqsoff_function(struct trace_array *tr, int graph)
{
	if (!function_enabled)
		return;

	if (graph)
		unregister_ftrace_graph();
	else
		unregister_ftrace_function(tr->ops);

	function_enabled = false;
}

static int irqsoff_function_set(struct trace_array *tr, u32 mask, int set)
{
	if (!(mask & TRACE_ITER_FUNCTION))
		return 0;

	if (set)
		register_irqsoff_function(tr, is_graph(tr), 1);
	else
		unregister_irqsoff_function(tr, is_graph(tr));
	return 1;
}
#else
static int register_irqsoff_function(struct trace_array *tr, int graph, int set)
{
	return 0;
}
static void unregister_irqsoff_function(struct trace_array *tr, int graph) { }
static inline int irqsoff_function_set(struct trace_array *tr, u32 mask, int set)
{
	return 0;
}
#endif /* CONFIG_FUNCTION_TRACER */

static int irqsoff_flag_changed(struct trace_array *tr, u32 mask, int set)
{
	struct tracer *tracer = tr->current_trace;

	if (irqsoff_function_set(tr, mask, set))
		return 0;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (mask & TRACE_ITER_DISPLAY_GRAPH)
		return irqsoff_display_graph(tr, set);
#endif

	return trace_keep_overwrite(tracer, mask, set);
}

static int start_irqsoff_tracer(struct trace_array *tr, int graph)
{
	int ret;

	ret = register_irqsoff_function(tr, graph, 0);

	if (!ret && tracing_is_enabled())
		tracer_enabled = 1;
	else
		tracer_enabled = 0;

	return ret;
}

static void stop_irqsoff_tracer(struct trace_array *tr, int graph)
{
	tracer_enabled = 0;

	unregister_irqsoff_function(tr, graph);
}

static bool irqsoff_busy;

static int __irqsoff_tracer_init(struct trace_array *tr)
{
	if (irqsoff_busy)
		return -EBUSY;

	save_flags = tr->trace_flags;

	/* non overwrite screws up the latency tracers */
	set_tracer_flag(tr, TRACE_ITER_OVERWRITE, 1);
	set_tracer_flag(tr, TRACE_ITER_LATENCY_FMT, 1);

	tr->max_latency = 0;
	irqsoff_trace = tr;
	/* make sure that the tracer is visible */
	smp_wmb();
	tracing_reset_online_cpus(&tr->trace_buffer);

	ftrace_init_array_ops(tr, irqsoff_tracer_call);

	/* Only toplevel instance supports graph tracing */
	if (start_irqsoff_tracer(tr, (tr->flags & TRACE_ARRAY_FL_GLOBAL &&
				      is_graph(tr))))
		printk(KERN_ERR "failed to start irqsoff tracer\n");

	irqsoff_busy = true;
	return 0;
}

static void irqsoff_tracer_reset(struct trace_array *tr)
{
	int lat_flag = save_flags & TRACE_ITER_LATENCY_FMT;
	int overwrite_flag = save_flags & TRACE_ITER_OVERWRITE;

	stop_irqsoff_tracer(tr, is_graph(tr));

	set_tracer_flag(tr, TRACE_ITER_LATENCY_FMT, lat_flag);
	set_tracer_flag(tr, TRACE_ITER_OVERWRITE, overwrite_flag);
	ftrace_reset_array_ops(tr);

	irqsoff_busy = false;
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

	return __irqsoff_tracer_init(tr);
}
static struct tracer irqsoff_tracer __read_mostly =
{
	.name		= "irqsoff",
	.init		= irqsoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.start		= irqsoff_tracer_start,
	.stop		= irqsoff_tracer_stop,
	.print_max	= true,
	.print_header   = irqsoff_print_header,
	.print_line     = irqsoff_print_line,
	.flag_changed	= irqsoff_flag_changed,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_irqsoff,
#endif
	.open           = irqsoff_trace_open,
	.close          = irqsoff_trace_close,
	.allow_instances = true,
	.use_max_tr	= true,
};
# define register_irqsoff(trace) register_tracer(&trace)
#else
# define register_irqsoff(trace) do { } while (0)
#endif

#ifdef CONFIG_PREEMPT_TRACER
static int preemptoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_PREEMPT_OFF;

	return __irqsoff_tracer_init(tr);
}

static struct tracer preemptoff_tracer __read_mostly =
{
	.name		= "preemptoff",
	.init		= preemptoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.start		= irqsoff_tracer_start,
	.stop		= irqsoff_tracer_stop,
	.print_max	= true,
	.print_header   = irqsoff_print_header,
	.print_line     = irqsoff_print_line,
	.flag_changed	= irqsoff_flag_changed,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_preemptoff,
#endif
	.open		= irqsoff_trace_open,
	.close		= irqsoff_trace_close,
	.allow_instances = true,
	.use_max_tr	= true,
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

	return __irqsoff_tracer_init(tr);
}

static struct tracer preemptirqsoff_tracer __read_mostly =
{
	.name		= "preemptirqsoff",
	.init		= preemptirqsoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.start		= irqsoff_tracer_start,
	.stop		= irqsoff_tracer_stop,
	.print_max	= true,
	.print_header   = irqsoff_print_header,
	.print_line     = irqsoff_print_line,
	.flag_changed	= irqsoff_flag_changed,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_preemptirqsoff,
#endif
	.open		= irqsoff_trace_open,
	.close		= irqsoff_trace_close,
	.allow_instances = true,
	.use_max_tr	= true,
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
core_initcall(init_irqsoff_tracer);
#endif /* IRQSOFF_TRACER || PREEMPTOFF_TRACER */

#ifndef CONFIG_IRQSOFF_TRACER
static inline void tracer_hardirqs_on(void) { }
static inline void tracer_hardirqs_off(void) { }
static inline void tracer_hardirqs_on_caller(unsigned long caller_addr) { }
static inline void tracer_hardirqs_off_caller(unsigned long caller_addr) { }
#endif

#ifndef CONFIG_PREEMPT_TRACER
static inline void tracer_preempt_on(unsigned long a0, unsigned long a1) { }
static inline void tracer_preempt_off(unsigned long a0, unsigned long a1) { }
#endif

/* Per-cpu variable to prevent redundant calls when IRQs already off */
static DEFINE_PER_CPU(int, tracing_irq_cpu);

#if defined(CONFIG_TRACE_IRQFLAGS) && !defined(CONFIG_PROVE_LOCKING)
void trace_hardirqs_on(void)
{
	if (!this_cpu_read(tracing_irq_cpu))
		return;

	trace_irq_enable_rcuidle(CALLER_ADDR0, CALLER_ADDR1);
	tracer_hardirqs_on();

	this_cpu_write(tracing_irq_cpu, 0);
}
EXPORT_SYMBOL(trace_hardirqs_on);

void trace_hardirqs_off(void)
{
	if (this_cpu_read(tracing_irq_cpu))
		return;

	this_cpu_write(tracing_irq_cpu, 1);

	trace_irq_disable_rcuidle(CALLER_ADDR0, CALLER_ADDR1);
	tracer_hardirqs_off();
}
EXPORT_SYMBOL(trace_hardirqs_off);

__visible void trace_hardirqs_on_caller(unsigned long caller_addr)
{
	if (!this_cpu_read(tracing_irq_cpu))
		return;

	trace_irq_enable_rcuidle(CALLER_ADDR0, caller_addr);
	tracer_hardirqs_on_caller(caller_addr);

	this_cpu_write(tracing_irq_cpu, 0);
}
EXPORT_SYMBOL(trace_hardirqs_on_caller);

__visible void trace_hardirqs_off_caller(unsigned long caller_addr)
{
	if (this_cpu_read(tracing_irq_cpu))
		return;

	this_cpu_write(tracing_irq_cpu, 1);

	trace_irq_disable_rcuidle(CALLER_ADDR0, caller_addr);
	tracer_hardirqs_off_caller(caller_addr);
}
EXPORT_SYMBOL(trace_hardirqs_off_caller);

/*
 * Stubs:
 */

void trace_softirqs_on(unsigned long ip)
{
}

void trace_softirqs_off(unsigned long ip)
{
}

inline void print_irqtrace_events(struct task_struct *curr)
{
}
#endif

#if defined(CONFIG_PREEMPT_TRACER) || \
	(defined(CONFIG_DEBUG_PREEMPT) && defined(CONFIG_PREEMPTIRQ_EVENTS))
void trace_preempt_on(unsigned long a0, unsigned long a1)
{
	trace_preempt_enable_rcuidle(a0, a1);
	tracer_preempt_on(a0, a1);
}

void trace_preempt_off(unsigned long a0, unsigned long a1)
{
	trace_preempt_disable_rcuidle(a0, a1);
	tracer_preempt_off(a0, a1);
}
#endif
