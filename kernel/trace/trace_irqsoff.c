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

static DEFINE_PER_CPU(int, tracing_cpu);

static DEFINE_SPINLOCK(max_trace_lock);

enum {
	TRACER_IRQS_OFF		= (1 << 1),
	TRACER_PREEMPT_OFF	= (1 << 2),
};

static int trace_type __read_mostly;

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
static void
irqsoff_tracer_call(unsigned long ip, unsigned long parent_ip)
{
	struct trace_array *tr = irqsoff_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
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
		return;

	local_save_flags(flags);
	/* slight chance to get a false positive on tracing_cpu */
	if (!irqs_disabled_flags(flags))
		return;

	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);

	if (likely(disabled == 1))
		trace_function(tr, data, ip, parent_ip, flags);

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
	unsigned long latency, t0, t1;
	cycle_t T0, T1, delta;
	unsigned long flags;

	/*
	 * usecs conversion is slow so we try to delay the conversion
	 * as long as possible:
	 */
	T0 = data->preempt_timestamp;
	T1 = ftrace_now(cpu);
	delta = T1-T0;

	local_save_flags(flags);

	if (!report_latency(delta))
		goto out;

	spin_lock_irqsave(&max_trace_lock, flags);

	/* check if we are still the max latency */
	if (!report_latency(delta))
		goto out_unlock;

	trace_function(tr, data, CALLER_ADDR0, parent_ip, flags);

	latency = nsecs_to_usecs(delta);

	if (data->critical_sequence != max_sequence)
		goto out_unlock;

	tracing_max_latency = delta;
	t0 = nsecs_to_usecs(T0);
	t1 = nsecs_to_usecs(T1);

	data->critical_end = parent_ip;

	update_max_tr_single(tr, current, cpu);

	max_sequence++;

out_unlock:
	spin_unlock_irqrestore(&max_trace_lock, flags);

out:
	data->critical_sequence = max_sequence;
	data->preempt_timestamp = ftrace_now(cpu);
	tracing_reset(data);
	trace_function(tr, data, CALLER_ADDR0, parent_ip, flags);
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
	tracing_reset(data);

	local_save_flags(flags);

	trace_function(tr, data, ip, parent_ip, flags);

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

	if (unlikely(!data) || unlikely(!head_page(data)) ||
	    !data->critical_start || atomic_read(&data->disabled))
		return;

	atomic_inc(&data->disabled);

	local_save_flags(flags);
	trace_function(tr, data, ip, parent_ip, flags);
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

static void start_irqsoff_tracer(struct trace_array *tr)
{
	register_ftrace_function(&trace_ops);
	tracer_enabled = 1;
}

static void stop_irqsoff_tracer(struct trace_array *tr)
{
	tracer_enabled = 0;
	unregister_ftrace_function(&trace_ops);
}

static void __irqsoff_tracer_init(struct trace_array *tr)
{
	irqsoff_trace = tr;
	/* make sure that the tracer is visible */
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

static void irqsoff_tracer_open(struct trace_iterator *iter)
{
	/* stop the trace while dumping */
	if (iter->tr->ctrl)
		stop_irqsoff_tracer(iter->tr);
}

static void irqsoff_tracer_close(struct trace_iterator *iter)
{
	if (iter->tr->ctrl)
		start_irqsoff_tracer(iter->tr);
}

#ifdef CONFIG_IRQSOFF_TRACER
static void irqsoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_IRQS_OFF;

	__irqsoff_tracer_init(tr);
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
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_irqsoff,
#endif
};
# define register_irqsoff(trace) register_tracer(&trace)
#else
# define register_irqsoff(trace) do { } while (0)
#endif

#ifdef CONFIG_PREEMPT_TRACER
static void preemptoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_PREEMPT_OFF;

	__irqsoff_tracer_init(tr);
}

static struct tracer preemptoff_tracer __read_mostly =
{
	.name		= "preemptoff",
	.init		= preemptoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.open		= irqsoff_tracer_open,
	.close		= irqsoff_tracer_close,
	.ctrl_update	= irqsoff_tracer_ctrl_update,
	.print_max	= 1,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_preemptoff,
#endif
};
# define register_preemptoff(trace) register_tracer(&trace)
#else
# define register_preemptoff(trace) do { } while (0)
#endif

#if defined(CONFIG_IRQSOFF_TRACER) && \
	defined(CONFIG_PREEMPT_TRACER)

static void preemptirqsoff_tracer_init(struct trace_array *tr)
{
	trace_type = TRACER_IRQS_OFF | TRACER_PREEMPT_OFF;

	__irqsoff_tracer_init(tr);
}

static struct tracer preemptirqsoff_tracer __read_mostly =
{
	.name		= "preemptirqsoff",
	.init		= preemptirqsoff_tracer_init,
	.reset		= irqsoff_tracer_reset,
	.open		= irqsoff_tracer_open,
	.close		= irqsoff_tracer_close,
	.ctrl_update	= irqsoff_tracer_ctrl_update,
	.print_max	= 1,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest    = trace_selftest_startup_preemptirqsoff,
#endif
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
