// SPDX-License-Identifier: GPL-2.0
/*
 * OS Noise Tracer: computes the OS Noise suffered by a running thread.
 * Timerlat Tracer: measures the wakeup latency of a timer triggered IRQ and thread.
 *
 * Based on "hwlat_detector" tracer by:
 *   Copyright (C) 2008-2009 Jon Masters, Red Hat, Inc. <jcm@redhat.com>
 *   Copyright (C) 2013-2016 Steven Rostedt, Red Hat, Inc. <srostedt@redhat.com>
 *   With feedback from Clark Williams <williams@redhat.com>
 *
 * And also based on the rtsl tracer presented on:
 *  DE OLIVEIRA, Daniel Bristot, et al. Demystifying the real-time linux
 *  scheduling latency. In: 32nd Euromicro Conference on Real-Time Systems
 *  (ECRTS 2020). Schloss Dagstuhl-Leibniz-Zentrum fur Informatik, 2020.
 *
 * Copyright (C) 2021 Daniel Bristot de Oliveira, Red Hat, Inc. <bristot@redhat.com>
 */

#include <linux/kthread.h>
#include <linux/tracefs.h>
#include <linux/uaccess.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/sched/clock.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include "trace.h"

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/trace/irq_vectors.h>
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#endif /* CONFIG_X86_LOCAL_APIC */

#include <trace/events/irq.h>
#include <trace/events/sched.h>

#define CREATE_TRACE_POINTS
#include <trace/events/osnoise.h>

/*
 * Default values.
 */
#define BANNER			"osnoise: "
#define DEFAULT_SAMPLE_PERIOD	1000000			/* 1s */
#define DEFAULT_SAMPLE_RUNTIME	1000000			/* 1s */

#define DEFAULT_TIMERLAT_PERIOD	1000			/* 1ms */
#define DEFAULT_TIMERLAT_PRIO	95			/* FIFO 95 */

/*
 * trace_array of the enabled osnoise/timerlat instances.
 */
struct osnoise_instance {
	struct list_head	list;
	struct trace_array	*tr;
};

static struct list_head osnoise_instances;

static bool osnoise_has_registered_instances(void)
{
	return !!list_first_or_null_rcu(&osnoise_instances,
					struct osnoise_instance,
					list);
}

/*
 * osnoise_instance_registered - check if a tr is already registered
 */
static int osnoise_instance_registered(struct trace_array *tr)
{
	struct osnoise_instance *inst;
	int found = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		if (inst->tr == tr)
			found = 1;
	}
	rcu_read_unlock();

	return found;
}

/*
 * osnoise_register_instance - register a new trace instance
 *
 * Register a trace_array *tr in the list of instances running
 * osnoise/timerlat tracers.
 */
static int osnoise_register_instance(struct trace_array *tr)
{
	struct osnoise_instance *inst;

	/*
	 * register/unregister serialization is provided by trace's
	 * trace_types_lock.
	 */
	lockdep_assert_held(&trace_types_lock);

	inst = kmalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	INIT_LIST_HEAD_RCU(&inst->list);
	inst->tr = tr;
	list_add_tail_rcu(&inst->list, &osnoise_instances);

	return 0;
}

/*
 *  osnoise_unregister_instance - unregister a registered trace instance
 *
 * Remove the trace_array *tr from the list of instances running
 * osnoise/timerlat tracers.
 */
static void osnoise_unregister_instance(struct trace_array *tr)
{
	struct osnoise_instance *inst;
	int found = 0;

	/*
	 * register/unregister serialization is provided by trace's
	 * trace_types_lock.
	 */
	lockdep_assert_held(&trace_types_lock);

	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		if (inst->tr == tr) {
			list_del_rcu(&inst->list);
			found = 1;
			break;
		}
	}

	if (!found)
		return;

	synchronize_rcu();
	kfree(inst);
}

/*
 * NMI runtime info.
 */
struct osn_nmi {
	u64	count;
	u64	delta_start;
};

/*
 * IRQ runtime info.
 */
struct osn_irq {
	u64	count;
	u64	arrival_time;
	u64	delta_start;
};

#define IRQ_CONTEXT	0
#define THREAD_CONTEXT	1
/*
 * sofirq runtime info.
 */
struct osn_softirq {
	u64	count;
	u64	arrival_time;
	u64	delta_start;
};

/*
 * thread runtime info.
 */
struct osn_thread {
	u64	count;
	u64	arrival_time;
	u64	delta_start;
};

/*
 * Runtime information: this structure saves the runtime information used by
 * one sampling thread.
 */
struct osnoise_variables {
	struct task_struct	*kthread;
	bool			sampling;
	pid_t			pid;
	struct osn_nmi		nmi;
	struct osn_irq		irq;
	struct osn_softirq	softirq;
	struct osn_thread	thread;
	local_t			int_counter;
};

/*
 * Per-cpu runtime information.
 */
DEFINE_PER_CPU(struct osnoise_variables, per_cpu_osnoise_var);

/*
 * this_cpu_osn_var - Return the per-cpu osnoise_variables on its relative CPU
 */
static inline struct osnoise_variables *this_cpu_osn_var(void)
{
	return this_cpu_ptr(&per_cpu_osnoise_var);
}

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * Runtime information for the timer mode.
 */
struct timerlat_variables {
	struct task_struct	*kthread;
	struct hrtimer		timer;
	u64			rel_period;
	u64			abs_period;
	bool			tracing_thread;
	u64			count;
};

DEFINE_PER_CPU(struct timerlat_variables, per_cpu_timerlat_var);

/*
 * this_cpu_tmr_var - Return the per-cpu timerlat_variables on its relative CPU
 */
static inline struct timerlat_variables *this_cpu_tmr_var(void)
{
	return this_cpu_ptr(&per_cpu_timerlat_var);
}

/*
 * tlat_var_reset - Reset the values of the given timerlat_variables
 */
static inline void tlat_var_reset(void)
{
	struct timerlat_variables *tlat_var;
	int cpu;
	/*
	 * So far, all the values are initialized as 0, so
	 * zeroing the structure is perfect.
	 */
	for_each_cpu(cpu, cpu_online_mask) {
		tlat_var = per_cpu_ptr(&per_cpu_timerlat_var, cpu);
		memset(tlat_var, 0, sizeof(*tlat_var));
	}
}
#else /* CONFIG_TIMERLAT_TRACER */
#define tlat_var_reset()	do {} while (0)
#endif /* CONFIG_TIMERLAT_TRACER */

/*
 * osn_var_reset - Reset the values of the given osnoise_variables
 */
static inline void osn_var_reset(void)
{
	struct osnoise_variables *osn_var;
	int cpu;

	/*
	 * So far, all the values are initialized as 0, so
	 * zeroing the structure is perfect.
	 */
	for_each_cpu(cpu, cpu_online_mask) {
		osn_var = per_cpu_ptr(&per_cpu_osnoise_var, cpu);
		memset(osn_var, 0, sizeof(*osn_var));
	}
}

/*
 * osn_var_reset_all - Reset the value of all per-cpu osnoise_variables
 */
static inline void osn_var_reset_all(void)
{
	osn_var_reset();
	tlat_var_reset();
}

/*
 * Tells NMIs to call back to the osnoise tracer to record timestamps.
 */
bool trace_osnoise_callback_enabled;

/*
 * osnoise sample structure definition. Used to store the statistics of a
 * sample run.
 */
struct osnoise_sample {
	u64			runtime;	/* runtime */
	u64			noise;		/* noise */
	u64			max_sample;	/* max single noise sample */
	int			hw_count;	/* # HW (incl. hypervisor) interference */
	int			nmi_count;	/* # NMIs during this sample */
	int			irq_count;	/* # IRQs during this sample */
	int			softirq_count;	/* # softirqs during this sample */
	int			thread_count;	/* # threads during this sample */
};

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * timerlat sample structure definition. Used to store the statistics of
 * a sample run.
 */
struct timerlat_sample {
	u64			timer_latency;	/* timer_latency */
	unsigned int		seqnum;		/* unique sequence */
	int			context;	/* timer context */
};
#endif

/*
 * Protect the interface.
 */
struct mutex interface_lock;

/*
 * Tracer data.
 */
static struct osnoise_data {
	u64	sample_period;		/* total sampling period */
	u64	sample_runtime;		/* active sampling portion of period */
	u64	stop_tracing;		/* stop trace in the internal operation (loop/irq) */
	u64	stop_tracing_total;	/* stop trace in the final operation (report/thread) */
#ifdef CONFIG_TIMERLAT_TRACER
	u64	timerlat_period;	/* timerlat period */
	u64	print_stack;		/* print IRQ stack if total > */
	int	timerlat_tracer;	/* timerlat tracer */
#endif
	bool	tainted;		/* infor users and developers about a problem */
} osnoise_data = {
	.sample_period			= DEFAULT_SAMPLE_PERIOD,
	.sample_runtime			= DEFAULT_SAMPLE_RUNTIME,
	.stop_tracing			= 0,
	.stop_tracing_total		= 0,
#ifdef CONFIG_TIMERLAT_TRACER
	.print_stack			= 0,
	.timerlat_period		= DEFAULT_TIMERLAT_PERIOD,
	.timerlat_tracer		= 0,
#endif
};

#ifdef CONFIG_TIMERLAT_TRACER
static inline bool timerlat_enabled(void)
{
	return osnoise_data.timerlat_tracer;
}

static inline int timerlat_softirq_exit(struct osnoise_variables *osn_var)
{
	struct timerlat_variables *tlat_var = this_cpu_tmr_var();
	/*
	 * If the timerlat is enabled, but the irq handler did
	 * not run yet enabling timerlat_tracer, do not trace.
	 */
	if (!tlat_var->tracing_thread) {
		osn_var->softirq.arrival_time = 0;
		osn_var->softirq.delta_start = 0;
		return 0;
	}
	return 1;
}

static inline int timerlat_thread_exit(struct osnoise_variables *osn_var)
{
	struct timerlat_variables *tlat_var = this_cpu_tmr_var();
	/*
	 * If the timerlat is enabled, but the irq handler did
	 * not run yet enabling timerlat_tracer, do not trace.
	 */
	if (!tlat_var->tracing_thread) {
		osn_var->thread.delta_start = 0;
		osn_var->thread.arrival_time = 0;
		return 0;
	}
	return 1;
}
#else /* CONFIG_TIMERLAT_TRACER */
static inline bool timerlat_enabled(void)
{
	return false;
}

static inline int timerlat_softirq_exit(struct osnoise_variables *osn_var)
{
	return 1;
}
static inline int timerlat_thread_exit(struct osnoise_variables *osn_var)
{
	return 1;
}
#endif

#ifdef CONFIG_PREEMPT_RT
/*
 * Print the osnoise header info.
 */
static void print_osnoise_headers(struct seq_file *s)
{
	if (osnoise_data.tainted)
		seq_puts(s, "# osnoise is tainted!\n");

	seq_puts(s, "#                                _-------=> irqs-off\n");
	seq_puts(s, "#                               / _------=> need-resched\n");
	seq_puts(s, "#                              | / _-----=> need-resched-lazy\n");
	seq_puts(s, "#                              || / _----=> hardirq/softirq\n");
	seq_puts(s, "#                              ||| / _---=> preempt-depth\n");
	seq_puts(s, "#                              |||| / _--=> preempt-lazy-depth\n");
	seq_puts(s, "#                              ||||| / _-=> migrate-disable\n");

	seq_puts(s, "#                              |||||| /          ");
	seq_puts(s, "                                     MAX\n");

	seq_puts(s, "#                              ||||| /                         ");
	seq_puts(s, "                    SINGLE      Interference counters:\n");

	seq_puts(s, "#                              |||||||               RUNTIME   ");
	seq_puts(s, "   NOISE  %% OF CPU  NOISE    +-----------------------------+\n");

	seq_puts(s, "#           TASK-PID      CPU# |||||||   TIMESTAMP    IN US    ");
	seq_puts(s, "   IN US  AVAILABLE  IN US     HW    NMI    IRQ   SIRQ THREAD\n");

	seq_puts(s, "#              | |         |   |||||||      |           |      ");
	seq_puts(s, "       |    |            |      |      |      |      |      |\n");
}
#else /* CONFIG_PREEMPT_RT */
static void print_osnoise_headers(struct seq_file *s)
{
	if (osnoise_data.tainted)
		seq_puts(s, "# osnoise is tainted!\n");

	seq_puts(s, "#                                _-----=> irqs-off\n");
	seq_puts(s, "#                               / _----=> need-resched\n");
	seq_puts(s, "#                              | / _---=> hardirq/softirq\n");
	seq_puts(s, "#                              || / _--=> preempt-depth\n");
	seq_puts(s, "#                              ||| / _-=> migrate-disable     ");
	seq_puts(s, "                    MAX\n");
	seq_puts(s, "#                              |||| /     delay               ");
	seq_puts(s, "                    SINGLE      Interference counters:\n");

	seq_puts(s, "#                              |||||               RUNTIME   ");
	seq_puts(s, "   NOISE  %% OF CPU  NOISE    +-----------------------------+\n");

	seq_puts(s, "#           TASK-PID      CPU# |||||   TIMESTAMP    IN US    ");
	seq_puts(s, "   IN US  AVAILABLE  IN US     HW    NMI    IRQ   SIRQ THREAD\n");

	seq_puts(s, "#              | |         |   |||||      |           |      ");
	seq_puts(s, "       |    |            |      |      |      |      |      |\n");
}
#endif /* CONFIG_PREEMPT_RT */

/*
 * osnoise_taint - report an osnoise error.
 */
#define osnoise_taint(msg) ({							\
	struct osnoise_instance *inst;						\
	struct trace_buffer *buffer;						\
										\
	rcu_read_lock();							\
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {		\
		buffer = inst->tr->array_buffer.buffer;				\
		trace_array_printk_buf(buffer, _THIS_IP_, msg);			\
	}									\
	rcu_read_unlock();							\
	osnoise_data.tainted = true;						\
})

/*
 * Record an osnoise_sample into the tracer buffer.
 */
static void
__trace_osnoise_sample(struct osnoise_sample *sample, struct trace_buffer *buffer)
{
	struct trace_event_call *call = &event_osnoise;
	struct ring_buffer_event *event;
	struct osnoise_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_OSNOISE, sizeof(*entry),
					  tracing_gen_ctx());
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->runtime		= sample->runtime;
	entry->noise		= sample->noise;
	entry->max_sample	= sample->max_sample;
	entry->hw_count		= sample->hw_count;
	entry->nmi_count	= sample->nmi_count;
	entry->irq_count	= sample->irq_count;
	entry->softirq_count	= sample->softirq_count;
	entry->thread_count	= sample->thread_count;

	if (!call_filter_check_discard(call, entry, buffer, event))
		trace_buffer_unlock_commit_nostack(buffer, event);
}

/*
 * Record an osnoise_sample on all osnoise instances.
 */
static void trace_osnoise_sample(struct osnoise_sample *sample)
{
	struct osnoise_instance *inst;
	struct trace_buffer *buffer;

	rcu_read_lock();
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		buffer = inst->tr->array_buffer.buffer;
		__trace_osnoise_sample(sample, buffer);
	}
	rcu_read_unlock();
}

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * Print the timerlat header info.
 */
#ifdef CONFIG_PREEMPT_RT
static void print_timerlat_headers(struct seq_file *s)
{
	seq_puts(s, "#                                _-------=> irqs-off\n");
	seq_puts(s, "#                               / _------=> need-resched\n");
	seq_puts(s, "#                              | / _-----=> need-resched-lazy\n");
	seq_puts(s, "#                              || / _----=> hardirq/softirq\n");
	seq_puts(s, "#                              ||| / _---=> preempt-depth\n");
	seq_puts(s, "#                              |||| / _--=> preempt-lazy-depth\n");
	seq_puts(s, "#                              ||||| / _-=> migrate-disable\n");
	seq_puts(s, "#                              |||||| /\n");
	seq_puts(s, "#                              |||||||             ACTIVATION\n");
	seq_puts(s, "#           TASK-PID      CPU# |||||||   TIMESTAMP    ID     ");
	seq_puts(s, "       CONTEXT                LATENCY\n");
	seq_puts(s, "#              | |         |   |||||||      |         |      ");
	seq_puts(s, "            |                       |\n");
}
#else /* CONFIG_PREEMPT_RT */
static void print_timerlat_headers(struct seq_file *s)
{
	seq_puts(s, "#                                _-----=> irqs-off\n");
	seq_puts(s, "#                               / _----=> need-resched\n");
	seq_puts(s, "#                              | / _---=> hardirq/softirq\n");
	seq_puts(s, "#                              || / _--=> preempt-depth\n");
	seq_puts(s, "#                              ||| / _-=> migrate-disable\n");
	seq_puts(s, "#                              |||| /     delay\n");
	seq_puts(s, "#                              |||||            ACTIVATION\n");
	seq_puts(s, "#           TASK-PID      CPU# |||||   TIMESTAMP   ID      ");
	seq_puts(s, "      CONTEXT                 LATENCY\n");
	seq_puts(s, "#              | |         |   |||||      |         |      ");
	seq_puts(s, "            |                       |\n");
}
#endif /* CONFIG_PREEMPT_RT */

static void
__trace_timerlat_sample(struct timerlat_sample *sample, struct trace_buffer *buffer)
{
	struct trace_event_call *call = &event_osnoise;
	struct ring_buffer_event *event;
	struct timerlat_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_TIMERLAT, sizeof(*entry),
					  tracing_gen_ctx());
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->seqnum			= sample->seqnum;
	entry->context			= sample->context;
	entry->timer_latency		= sample->timer_latency;

	if (!call_filter_check_discard(call, entry, buffer, event))
		trace_buffer_unlock_commit_nostack(buffer, event);
}

/*
 * Record an timerlat_sample into the tracer buffer.
 */
static void trace_timerlat_sample(struct timerlat_sample *sample)
{
	struct osnoise_instance *inst;
	struct trace_buffer *buffer;

	rcu_read_lock();
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		buffer = inst->tr->array_buffer.buffer;
		__trace_timerlat_sample(sample, buffer);
	}
	rcu_read_unlock();
}

#ifdef CONFIG_STACKTRACE

#define	MAX_CALLS	256

/*
 * Stack trace will take place only at IRQ level, so, no need
 * to control nesting here.
 */
struct trace_stack {
	int		stack_size;
	int		nr_entries;
	unsigned long	calls[MAX_CALLS];
};

static DEFINE_PER_CPU(struct trace_stack, trace_stack);

/*
 * timerlat_save_stack - save a stack trace without printing
 *
 * Save the current stack trace without printing. The
 * stack will be printed later, after the end of the measurement.
 */
static void timerlat_save_stack(int skip)
{
	unsigned int size, nr_entries;
	struct trace_stack *fstack;

	fstack = this_cpu_ptr(&trace_stack);

	size = ARRAY_SIZE(fstack->calls);

	nr_entries = stack_trace_save(fstack->calls, size, skip);

	fstack->stack_size = nr_entries * sizeof(unsigned long);
	fstack->nr_entries = nr_entries;

	return;

}

static void
__timerlat_dump_stack(struct trace_buffer *buffer, struct trace_stack *fstack, unsigned int size)
{
	struct trace_event_call *call = &event_osnoise;
	struct ring_buffer_event *event;
	struct stack_entry *entry;

	event = trace_buffer_lock_reserve(buffer, TRACE_STACK, sizeof(*entry) + size,
					  tracing_gen_ctx());
	if (!event)
		return;

	entry = ring_buffer_event_data(event);

	memcpy(&entry->caller, fstack->calls, size);
	entry->size = fstack->nr_entries;

	if (!call_filter_check_discard(call, entry, buffer, event))
		trace_buffer_unlock_commit_nostack(buffer, event);
}

/*
 * timerlat_dump_stack - dump a stack trace previously saved
 */
static void timerlat_dump_stack(u64 latency)
{
	struct osnoise_instance *inst;
	struct trace_buffer *buffer;
	struct trace_stack *fstack;
	unsigned int size;

	/*
	 * trace only if latency > print_stack config, if enabled.
	 */
	if (!osnoise_data.print_stack || osnoise_data.print_stack > latency)
		return;

	preempt_disable_notrace();
	fstack = this_cpu_ptr(&trace_stack);
	size = fstack->stack_size;

	rcu_read_lock();
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		buffer = inst->tr->array_buffer.buffer;
		__timerlat_dump_stack(buffer, fstack, size);

	}
	rcu_read_unlock();
	preempt_enable_notrace();
}
#else /* CONFIG_STACKTRACE */
#define timerlat_dump_stack(u64 latency) do {} while (0)
#define timerlat_save_stack(a) do {} while (0)
#endif /* CONFIG_STACKTRACE */
#endif /* CONFIG_TIMERLAT_TRACER */

/*
 * Macros to encapsulate the time capturing infrastructure.
 */
#define time_get()	trace_clock_local()
#define time_to_us(x)	div_u64(x, 1000)
#define time_sub(a, b)	((a) - (b))

/*
 * cond_move_irq_delta_start - Forward the delta_start of a running IRQ
 *
 * If an IRQ is preempted by an NMI, its delta_start is pushed forward
 * to discount the NMI interference.
 *
 * See get_int_safe_duration().
 */
static inline void
cond_move_irq_delta_start(struct osnoise_variables *osn_var, u64 duration)
{
	if (osn_var->irq.delta_start)
		osn_var->irq.delta_start += duration;
}

#ifndef CONFIG_PREEMPT_RT
/*
 * cond_move_softirq_delta_start - Forward the delta_start of a running softirq.
 *
 * If a softirq is preempted by an IRQ or NMI, its delta_start is pushed
 * forward to discount the interference.
 *
 * See get_int_safe_duration().
 */
static inline void
cond_move_softirq_delta_start(struct osnoise_variables *osn_var, u64 duration)
{
	if (osn_var->softirq.delta_start)
		osn_var->softirq.delta_start += duration;
}
#else /* CONFIG_PREEMPT_RT */
#define cond_move_softirq_delta_start(osn_var, duration) do {} while (0)
#endif

/*
 * cond_move_thread_delta_start - Forward the delta_start of a running thread
 *
 * If a noisy thread is preempted by an softirq, IRQ or NMI, its delta_start
 * is pushed forward to discount the interference.
 *
 * See get_int_safe_duration().
 */
static inline void
cond_move_thread_delta_start(struct osnoise_variables *osn_var, u64 duration)
{
	if (osn_var->thread.delta_start)
		osn_var->thread.delta_start += duration;
}

/*
 * get_int_safe_duration - Get the duration of a window
 *
 * The irq, softirq and thread varaibles need to have its duration without
 * the interference from higher priority interrupts. Instead of keeping a
 * variable to discount the interrupt interference from these variables, the
 * starting time of these variables are pushed forward with the interrupt's
 * duration. In this way, a single variable is used to:
 *
 *   - Know if a given window is being measured.
 *   - Account its duration.
 *   - Discount the interference.
 *
 * To avoid getting inconsistent values, e.g.,:
 *
 *	now = time_get()
 *		--->	interrupt!
 *			delta_start -= int duration;
 *		<---
 *	duration = now - delta_start;
 *
 *	result: negative duration if the variable duration before the
 *	interrupt was smaller than the interrupt execution.
 *
 * A counter of interrupts is used. If the counter increased, try
 * to capture an interference safe duration.
 */
static inline s64
get_int_safe_duration(struct osnoise_variables *osn_var, u64 *delta_start)
{
	u64 int_counter, now;
	s64 duration;

	do {
		int_counter = local_read(&osn_var->int_counter);
		/* synchronize with interrupts */
		barrier();

		now = time_get();
		duration = (now - *delta_start);

		/* synchronize with interrupts */
		barrier();
	} while (int_counter != local_read(&osn_var->int_counter));

	/*
	 * This is an evidence of race conditions that cause
	 * a value to be "discounted" too much.
	 */
	if (duration < 0)
		osnoise_taint("Negative duration!\n");

	*delta_start = 0;

	return duration;
}

/*
 *
 * set_int_safe_time - Save the current time on *time, aware of interference
 *
 * Get the time, taking into consideration a possible interference from
 * higher priority interrupts.
 *
 * See get_int_safe_duration() for an explanation.
 */
static u64
set_int_safe_time(struct osnoise_variables *osn_var, u64 *time)
{
	u64 int_counter;

	do {
		int_counter = local_read(&osn_var->int_counter);
		/* synchronize with interrupts */
		barrier();

		*time = time_get();

		/* synchronize with interrupts */
		barrier();
	} while (int_counter != local_read(&osn_var->int_counter));

	return int_counter;
}

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * copy_int_safe_time - Copy *src into *desc aware of interference
 */
static u64
copy_int_safe_time(struct osnoise_variables *osn_var, u64 *dst, u64 *src)
{
	u64 int_counter;

	do {
		int_counter = local_read(&osn_var->int_counter);
		/* synchronize with interrupts */
		barrier();

		*dst = *src;

		/* synchronize with interrupts */
		barrier();
	} while (int_counter != local_read(&osn_var->int_counter));

	return int_counter;
}
#endif /* CONFIG_TIMERLAT_TRACER */

/*
 * trace_osnoise_callback - NMI entry/exit callback
 *
 * This function is called at the entry and exit NMI code. The bool enter
 * distinguishes between either case. This function is used to note a NMI
 * occurrence, compute the noise caused by the NMI, and to remove the noise
 * it is potentially causing on other interference variables.
 */
void trace_osnoise_callback(bool enter)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();
	u64 duration;

	if (!osn_var->sampling)
		return;

	/*
	 * Currently trace_clock_local() calls sched_clock() and the
	 * generic version is not NMI safe.
	 */
	if (!IS_ENABLED(CONFIG_GENERIC_SCHED_CLOCK)) {
		if (enter) {
			osn_var->nmi.delta_start = time_get();
			local_inc(&osn_var->int_counter);
		} else {
			duration = time_get() - osn_var->nmi.delta_start;

			trace_nmi_noise(osn_var->nmi.delta_start, duration);

			cond_move_irq_delta_start(osn_var, duration);
			cond_move_softirq_delta_start(osn_var, duration);
			cond_move_thread_delta_start(osn_var, duration);
		}
	}

	if (enter)
		osn_var->nmi.count++;
}

/*
 * osnoise_trace_irq_entry - Note the starting of an IRQ
 *
 * Save the starting time of an IRQ. As IRQs are non-preemptive to other IRQs,
 * it is safe to use a single variable (ons_var->irq) to save the statistics.
 * The arrival_time is used to report... the arrival time. The delta_start
 * is used to compute the duration at the IRQ exit handler. See
 * cond_move_irq_delta_start().
 */
void osnoise_trace_irq_entry(int id)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();

	if (!osn_var->sampling)
		return;
	/*
	 * This value will be used in the report, but not to compute
	 * the execution time, so it is safe to get it unsafe.
	 */
	osn_var->irq.arrival_time = time_get();
	set_int_safe_time(osn_var, &osn_var->irq.delta_start);
	osn_var->irq.count++;

	local_inc(&osn_var->int_counter);
}

/*
 * osnoise_irq_exit - Note the end of an IRQ, sava data and trace
 *
 * Computes the duration of the IRQ noise, and trace it. Also discounts the
 * interference from other sources of noise could be currently being accounted.
 */
void osnoise_trace_irq_exit(int id, const char *desc)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();
	int duration;

	if (!osn_var->sampling)
		return;

	duration = get_int_safe_duration(osn_var, &osn_var->irq.delta_start);
	trace_irq_noise(id, desc, osn_var->irq.arrival_time, duration);
	osn_var->irq.arrival_time = 0;
	cond_move_softirq_delta_start(osn_var, duration);
	cond_move_thread_delta_start(osn_var, duration);
}

/*
 * trace_irqentry_callback - Callback to the irq:irq_entry traceevent
 *
 * Used to note the starting of an IRQ occurece.
 */
static void trace_irqentry_callback(void *data, int irq,
				    struct irqaction *action)
{
	osnoise_trace_irq_entry(irq);
}

/*
 * trace_irqexit_callback - Callback to the irq:irq_exit traceevent
 *
 * Used to note the end of an IRQ occurece.
 */
static void trace_irqexit_callback(void *data, int irq,
				   struct irqaction *action, int ret)
{
	osnoise_trace_irq_exit(irq, action->name);
}

/*
 * arch specific register function.
 */
int __weak osnoise_arch_register(void)
{
	return 0;
}

/*
 * arch specific unregister function.
 */
void __weak osnoise_arch_unregister(void)
{
	return;
}

/*
 * hook_irq_events - Hook IRQ handling events
 *
 * This function hooks the IRQ related callbacks to the respective trace
 * events.
 */
static int hook_irq_events(void)
{
	int ret;

	ret = register_trace_irq_handler_entry(trace_irqentry_callback, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_irq_handler_exit(trace_irqexit_callback, NULL);
	if (ret)
		goto out_unregister_entry;

	ret = osnoise_arch_register();
	if (ret)
		goto out_irq_exit;

	return 0;

out_irq_exit:
	unregister_trace_irq_handler_exit(trace_irqexit_callback, NULL);
out_unregister_entry:
	unregister_trace_irq_handler_entry(trace_irqentry_callback, NULL);
out_err:
	return -EINVAL;
}

/*
 * unhook_irq_events - Unhook IRQ handling events
 *
 * This function unhooks the IRQ related callbacks to the respective trace
 * events.
 */
static void unhook_irq_events(void)
{
	osnoise_arch_unregister();
	unregister_trace_irq_handler_exit(trace_irqexit_callback, NULL);
	unregister_trace_irq_handler_entry(trace_irqentry_callback, NULL);
}

#ifndef CONFIG_PREEMPT_RT
/*
 * trace_softirq_entry_callback - Note the starting of a softirq
 *
 * Save the starting time of a softirq. As softirqs are non-preemptive to
 * other softirqs, it is safe to use a single variable (ons_var->softirq)
 * to save the statistics. The arrival_time is used to report... the
 * arrival time. The delta_start is used to compute the duration at the
 * softirq exit handler. See cond_move_softirq_delta_start().
 */
static void trace_softirq_entry_callback(void *data, unsigned int vec_nr)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();

	if (!osn_var->sampling)
		return;
	/*
	 * This value will be used in the report, but not to compute
	 * the execution time, so it is safe to get it unsafe.
	 */
	osn_var->softirq.arrival_time = time_get();
	set_int_safe_time(osn_var, &osn_var->softirq.delta_start);
	osn_var->softirq.count++;

	local_inc(&osn_var->int_counter);
}

/*
 * trace_softirq_exit_callback - Note the end of an softirq
 *
 * Computes the duration of the softirq noise, and trace it. Also discounts the
 * interference from other sources of noise could be currently being accounted.
 */
static void trace_softirq_exit_callback(void *data, unsigned int vec_nr)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();
	int duration;

	if (!osn_var->sampling)
		return;

	if (unlikely(timerlat_enabled()))
		if (!timerlat_softirq_exit(osn_var))
			return;

	duration = get_int_safe_duration(osn_var, &osn_var->softirq.delta_start);
	trace_softirq_noise(vec_nr, osn_var->softirq.arrival_time, duration);
	cond_move_thread_delta_start(osn_var, duration);
	osn_var->softirq.arrival_time = 0;
}

/*
 * hook_softirq_events - Hook softirq handling events
 *
 * This function hooks the softirq related callbacks to the respective trace
 * events.
 */
static int hook_softirq_events(void)
{
	int ret;

	ret = register_trace_softirq_entry(trace_softirq_entry_callback, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_softirq_exit(trace_softirq_exit_callback, NULL);
	if (ret)
		goto out_unreg_entry;

	return 0;

out_unreg_entry:
	unregister_trace_softirq_entry(trace_softirq_entry_callback, NULL);
out_err:
	return -EINVAL;
}

/*
 * unhook_softirq_events - Unhook softirq handling events
 *
 * This function hooks the softirq related callbacks to the respective trace
 * events.
 */
static void unhook_softirq_events(void)
{
	unregister_trace_softirq_entry(trace_softirq_entry_callback, NULL);
	unregister_trace_softirq_exit(trace_softirq_exit_callback, NULL);
}
#else /* CONFIG_PREEMPT_RT */
/*
 * softirq are threads on the PREEMPT_RT mode.
 */
static int hook_softirq_events(void)
{
	return 0;
}
static void unhook_softirq_events(void)
{
}
#endif

/*
 * thread_entry - Record the starting of a thread noise window
 *
 * It saves the context switch time for a noisy thread, and increments
 * the interference counters.
 */
static void
thread_entry(struct osnoise_variables *osn_var, struct task_struct *t)
{
	if (!osn_var->sampling)
		return;
	/*
	 * The arrival time will be used in the report, but not to compute
	 * the execution time, so it is safe to get it unsafe.
	 */
	osn_var->thread.arrival_time = time_get();

	set_int_safe_time(osn_var, &osn_var->thread.delta_start);

	osn_var->thread.count++;
	local_inc(&osn_var->int_counter);
}

/*
 * thread_exit - Report the end of a thread noise window
 *
 * It computes the total noise from a thread, tracing if needed.
 */
static void
thread_exit(struct osnoise_variables *osn_var, struct task_struct *t)
{
	int duration;

	if (!osn_var->sampling)
		return;

	if (unlikely(timerlat_enabled()))
		if (!timerlat_thread_exit(osn_var))
			return;

	duration = get_int_safe_duration(osn_var, &osn_var->thread.delta_start);

	trace_thread_noise(t, osn_var->thread.arrival_time, duration);

	osn_var->thread.arrival_time = 0;
}

/*
 * trace_sched_switch - sched:sched_switch trace event handler
 *
 * This function is hooked to the sched:sched_switch trace event, and it is
 * used to record the beginning and to report the end of a thread noise window.
 */
static void
trace_sched_switch_callback(void *data, bool preempt, struct task_struct *p,
			    struct task_struct *n)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();

	if (p->pid != osn_var->pid)
		thread_exit(osn_var, p);

	if (n->pid != osn_var->pid)
		thread_entry(osn_var, n);
}

/*
 * hook_thread_events - Hook the insturmentation for thread noise
 *
 * Hook the osnoise tracer callbacks to handle the noise from other
 * threads on the necessary kernel events.
 */
static int hook_thread_events(void)
{
	int ret;

	ret = register_trace_sched_switch(trace_sched_switch_callback, NULL);
	if (ret)
		return -EINVAL;

	return 0;
}

/*
 * unhook_thread_events - *nhook the insturmentation for thread noise
 *
 * Unook the osnoise tracer callbacks to handle the noise from other
 * threads on the necessary kernel events.
 */
static void unhook_thread_events(void)
{
	unregister_trace_sched_switch(trace_sched_switch_callback, NULL);
}

/*
 * save_osn_sample_stats - Save the osnoise_sample statistics
 *
 * Save the osnoise_sample statistics before the sampling phase. These
 * values will be used later to compute the diff betwneen the statistics
 * before and after the osnoise sampling.
 */
static void
save_osn_sample_stats(struct osnoise_variables *osn_var, struct osnoise_sample *s)
{
	s->nmi_count = osn_var->nmi.count;
	s->irq_count = osn_var->irq.count;
	s->softirq_count = osn_var->softirq.count;
	s->thread_count = osn_var->thread.count;
}

/*
 * diff_osn_sample_stats - Compute the osnoise_sample statistics
 *
 * After a sample period, compute the difference on the osnoise_sample
 * statistics. The struct osnoise_sample *s contains the statistics saved via
 * save_osn_sample_stats() before the osnoise sampling.
 */
static void
diff_osn_sample_stats(struct osnoise_variables *osn_var, struct osnoise_sample *s)
{
	s->nmi_count = osn_var->nmi.count - s->nmi_count;
	s->irq_count = osn_var->irq.count - s->irq_count;
	s->softirq_count = osn_var->softirq.count - s->softirq_count;
	s->thread_count = osn_var->thread.count - s->thread_count;
}

/*
 * osnoise_stop_tracing - Stop tracing and the tracer.
 */
static __always_inline void osnoise_stop_tracing(void)
{
	struct osnoise_instance *inst;
	struct trace_array *tr;

	rcu_read_lock();
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		tr = inst->tr;
		trace_array_printk_buf(tr->array_buffer.buffer, _THIS_IP_,
				"stop tracing hit on cpu %d\n", smp_processor_id());

		tracer_tracing_off(tr);
	}
	rcu_read_unlock();
}

/*
 * notify_new_max_latency - Notify a new max latency via fsnotify interface.
 */
static void notify_new_max_latency(u64 latency)
{
	struct osnoise_instance *inst;
	struct trace_array *tr;

	rcu_read_lock();
	list_for_each_entry_rcu(inst, &osnoise_instances, list) {
		tr = inst->tr;
		if (tr->max_latency < latency) {
			tr->max_latency = latency;
			latency_fsnotify(tr);
		}
	}
	rcu_read_unlock();
}

/*
 * run_osnoise - Sample the time and look for osnoise
 *
 * Used to capture the time, looking for potential osnoise latency repeatedly.
 * Different from hwlat_detector, it is called with preemption and interrupts
 * enabled. This allows irqs, softirqs and threads to run, interfering on the
 * osnoise sampling thread, as they would do with a regular thread.
 */
static int run_osnoise(void)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();
	u64 start, sample, last_sample;
	u64 last_int_count, int_count;
	s64 noise = 0, max_noise = 0;
	s64 total, last_total = 0;
	struct osnoise_sample s;
	unsigned int threshold;
	u64 runtime, stop_in;
	u64 sum_noise = 0;
	int hw_count = 0;
	int ret = -1;

	/*
	 * Considers the current thread as the workload.
	 */
	osn_var->pid = current->pid;

	/*
	 * Save the current stats for the diff
	 */
	save_osn_sample_stats(osn_var, &s);

	/*
	 * if threshold is 0, use the default value of 5 us.
	 */
	threshold = tracing_thresh ? : 5000;

	/*
	 * Make sure NMIs see sampling first
	 */
	osn_var->sampling = true;
	barrier();

	/*
	 * Transform the *_us config to nanoseconds to avoid the
	 * division on the main loop.
	 */
	runtime = osnoise_data.sample_runtime * NSEC_PER_USEC;
	stop_in = osnoise_data.stop_tracing * NSEC_PER_USEC;

	/*
	 * Start timestemp
	 */
	start = time_get();

	/*
	 * "previous" loop.
	 */
	last_int_count = set_int_safe_time(osn_var, &last_sample);

	do {
		/*
		 * Get sample!
		 */
		int_count = set_int_safe_time(osn_var, &sample);

		noise = time_sub(sample, last_sample);

		/*
		 * This shouldn't happen.
		 */
		if (noise < 0) {
			osnoise_taint("negative noise!");
			goto out;
		}

		/*
		 * Sample runtime.
		 */
		total = time_sub(sample, start);

		/*
		 * Check for possible overflows.
		 */
		if (total < last_total) {
			osnoise_taint("total overflow!");
			break;
		}

		last_total = total;

		if (noise >= threshold) {
			int interference = int_count - last_int_count;

			if (noise > max_noise)
				max_noise = noise;

			if (!interference)
				hw_count++;

			sum_noise += noise;

			trace_sample_threshold(last_sample, noise, interference);

			if (osnoise_data.stop_tracing)
				if (noise > stop_in)
					osnoise_stop_tracing();
		}

		/*
		 * For the non-preemptive kernel config: let threads runs, if
		 * they so wish.
		 */
		cond_resched();

		last_sample = sample;
		last_int_count = int_count;

	} while (total < runtime && !kthread_should_stop());

	/*
	 * Finish the above in the view for interrupts.
	 */
	barrier();

	osn_var->sampling = false;

	/*
	 * Make sure sampling data is no longer updated.
	 */
	barrier();

	/*
	 * Save noise info.
	 */
	s.noise = time_to_us(sum_noise);
	s.runtime = time_to_us(total);
	s.max_sample = time_to_us(max_noise);
	s.hw_count = hw_count;

	/* Save interference stats info */
	diff_osn_sample_stats(osn_var, &s);

	trace_osnoise_sample(&s);

	notify_new_max_latency(max_noise);

	if (osnoise_data.stop_tracing_total)
		if (s.noise > osnoise_data.stop_tracing_total)
			osnoise_stop_tracing();

	return 0;
out:
	return ret;
}

static struct cpumask osnoise_cpumask;
static struct cpumask save_cpumask;

/*
 * osnoise_main - The osnoise detection kernel thread
 *
 * Calls run_osnoise() function to measure the osnoise for the configured runtime,
 * every period.
 */
static int osnoise_main(void *data)
{
	u64 interval;

	while (!kthread_should_stop()) {

		run_osnoise();

		mutex_lock(&interface_lock);
		interval = osnoise_data.sample_period - osnoise_data.sample_runtime;
		mutex_unlock(&interface_lock);

		do_div(interval, USEC_PER_MSEC);

		/*
		 * differently from hwlat_detector, the osnoise tracer can run
		 * without a pause because preemption is on.
		 */
		if (interval < 1) {
			/* Let synchronize_rcu_tasks() make progress */
			cond_resched_tasks_rcu_qs();
			continue;
		}

		if (msleep_interruptible(interval))
			break;
	}

	return 0;
}

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * timerlat_irq - hrtimer handler for timerlat.
 */
static enum hrtimer_restart timerlat_irq(struct hrtimer *timer)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();
	struct timerlat_variables *tlat;
	struct timerlat_sample s;
	u64 now;
	u64 diff;

	/*
	 * I am not sure if the timer was armed for this CPU. So, get
	 * the timerlat struct from the timer itself, not from this
	 * CPU.
	 */
	tlat = container_of(timer, struct timerlat_variables, timer);

	now = ktime_to_ns(hrtimer_cb_get_time(&tlat->timer));

	/*
	 * Enable the osnoise: events for thread an softirq.
	 */
	tlat->tracing_thread = true;

	osn_var->thread.arrival_time = time_get();

	/*
	 * A hardirq is running: the timer IRQ. It is for sure preempting
	 * a thread, and potentially preempting a softirq.
	 *
	 * At this point, it is not interesting to know the duration of the
	 * preempted thread (and maybe softirq), but how much time they will
	 * delay the beginning of the execution of the timer thread.
	 *
	 * To get the correct (net) delay added by the softirq, its delta_start
	 * is set as the IRQ one. In this way, at the return of the IRQ, the delta
	 * start of the sofitrq will be zeroed, accounting then only the time
	 * after that.
	 *
	 * The thread follows the same principle. However, if a softirq is
	 * running, the thread needs to receive the softirq delta_start. The
	 * reason being is that the softirq will be the last to be unfolded,
	 * resseting the thread delay to zero.
	 *
	 * The PREEMPT_RT is a special case, though. As softirqs run as threads
	 * on RT, moving the thread is enough.
	 */
	if (!IS_ENABLED(CONFIG_PREEMPT_RT) && osn_var->softirq.delta_start) {
		copy_int_safe_time(osn_var, &osn_var->thread.delta_start,
				   &osn_var->softirq.delta_start);

		copy_int_safe_time(osn_var, &osn_var->softirq.delta_start,
				    &osn_var->irq.delta_start);
	} else {
		copy_int_safe_time(osn_var, &osn_var->thread.delta_start,
				    &osn_var->irq.delta_start);
	}

	/*
	 * Compute the current time with the expected time.
	 */
	diff = now - tlat->abs_period;

	tlat->count++;
	s.seqnum = tlat->count;
	s.timer_latency = diff;
	s.context = IRQ_CONTEXT;

	trace_timerlat_sample(&s);

	notify_new_max_latency(diff);

	if (osnoise_data.stop_tracing)
		if (time_to_us(diff) >= osnoise_data.stop_tracing)
			osnoise_stop_tracing();

	wake_up_process(tlat->kthread);

	if (osnoise_data.print_stack)
		timerlat_save_stack(0);

	return HRTIMER_NORESTART;
}

/*
 * wait_next_period - Wait for the next period for timerlat
 */
static int wait_next_period(struct timerlat_variables *tlat)
{
	ktime_t next_abs_period, now;
	u64 rel_period = osnoise_data.timerlat_period * 1000;

	now = hrtimer_cb_get_time(&tlat->timer);
	next_abs_period = ns_to_ktime(tlat->abs_period + rel_period);

	/*
	 * Save the next abs_period.
	 */
	tlat->abs_period = (u64) ktime_to_ns(next_abs_period);

	/*
	 * If the new abs_period is in the past, skip the activation.
	 */
	while (ktime_compare(now, next_abs_period) > 0) {
		next_abs_period = ns_to_ktime(tlat->abs_period + rel_period);
		tlat->abs_period = (u64) ktime_to_ns(next_abs_period);
	}

	set_current_state(TASK_INTERRUPTIBLE);

	hrtimer_start(&tlat->timer, next_abs_period, HRTIMER_MODE_ABS_PINNED_HARD);
	schedule();
	return 1;
}

/*
 * timerlat_main- Timerlat main
 */
static int timerlat_main(void *data)
{
	struct osnoise_variables *osn_var = this_cpu_osn_var();
	struct timerlat_variables *tlat = this_cpu_tmr_var();
	struct timerlat_sample s;
	struct sched_param sp;
	u64 now, diff;

	/*
	 * Make the thread RT, that is how cyclictest is usually used.
	 */
	sp.sched_priority = DEFAULT_TIMERLAT_PRIO;
	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);

	tlat->count = 0;
	tlat->tracing_thread = false;

	hrtimer_init(&tlat->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED_HARD);
	tlat->timer.function = timerlat_irq;
	tlat->kthread = current;
	osn_var->pid = current->pid;
	/*
	 * Anotate the arrival time.
	 */
	tlat->abs_period = hrtimer_cb_get_time(&tlat->timer);

	wait_next_period(tlat);

	osn_var->sampling = 1;

	while (!kthread_should_stop()) {
		now = ktime_to_ns(hrtimer_cb_get_time(&tlat->timer));
		diff = now - tlat->abs_period;

		s.seqnum = tlat->count;
		s.timer_latency = diff;
		s.context = THREAD_CONTEXT;

		trace_timerlat_sample(&s);

		timerlat_dump_stack(time_to_us(diff));

		tlat->tracing_thread = false;
		if (osnoise_data.stop_tracing_total)
			if (time_to_us(diff) >= osnoise_data.stop_tracing_total)
				osnoise_stop_tracing();

		wait_next_period(tlat);
	}

	hrtimer_cancel(&tlat->timer);
	return 0;
}
#else /* CONFIG_TIMERLAT_TRACER */
static int timerlat_main(void *data)
{
	return 0;
}
#endif /* CONFIG_TIMERLAT_TRACER */

/*
 * stop_kthread - stop a workload thread
 */
static void stop_kthread(unsigned int cpu)
{
	struct task_struct *kthread;

	kthread = per_cpu(per_cpu_osnoise_var, cpu).kthread;
	if (kthread)
		kthread_stop(kthread);
	per_cpu(per_cpu_osnoise_var, cpu).kthread = NULL;
}

/*
 * stop_per_cpu_kthread - Stop per-cpu threads
 *
 * Stop the osnoise sampling htread. Use this on unload and at system
 * shutdown.
 */
static void stop_per_cpu_kthreads(void)
{
	int cpu;

	cpus_read_lock();

	for_each_online_cpu(cpu)
		stop_kthread(cpu);

	cpus_read_unlock();
}

/*
 * start_kthread - Start a workload tread
 */
static int start_kthread(unsigned int cpu)
{
	struct task_struct *kthread;
	void *main = osnoise_main;
	char comm[24];

	if (timerlat_enabled()) {
		snprintf(comm, 24, "timerlat/%d", cpu);
		main = timerlat_main;
	} else {
		snprintf(comm, 24, "osnoise/%d", cpu);
	}

	kthread = kthread_create_on_cpu(main, NULL, cpu, comm);

	if (IS_ERR(kthread)) {
		pr_err(BANNER "could not start sampling thread\n");
		stop_per_cpu_kthreads();
		return -ENOMEM;
	}

	per_cpu(per_cpu_osnoise_var, cpu).kthread = kthread;
	wake_up_process(kthread);

	return 0;
}

/*
 * start_per_cpu_kthread - Kick off per-cpu osnoise sampling kthreads
 *
 * This starts the kernel thread that will look for osnoise on many
 * cpus.
 */
static int start_per_cpu_kthreads(void)
{
	struct cpumask *current_mask = &save_cpumask;
	int retval = 0;
	int cpu;

	cpus_read_lock();
	/*
	 * Run only on online CPUs in which osnoise is allowed to run.
	 */
	cpumask_and(current_mask, cpu_online_mask, &osnoise_cpumask);

	for_each_possible_cpu(cpu)
		per_cpu(per_cpu_osnoise_var, cpu).kthread = NULL;

	for_each_cpu(cpu, current_mask) {
		retval = start_kthread(cpu);
		if (retval) {
			stop_per_cpu_kthreads();
			break;
		}
	}

	cpus_read_unlock();

	return retval;
}

#ifdef CONFIG_HOTPLUG_CPU
static void osnoise_hotplug_workfn(struct work_struct *dummy)
{
	unsigned int cpu = smp_processor_id();

	mutex_lock(&trace_types_lock);

	if (!osnoise_has_registered_instances())
		goto out_unlock_trace;

	mutex_lock(&interface_lock);
	cpus_read_lock();

	if (!cpumask_test_cpu(cpu, &osnoise_cpumask))
		goto out_unlock;

	start_kthread(cpu);

out_unlock:
	cpus_read_unlock();
	mutex_unlock(&interface_lock);
out_unlock_trace:
	mutex_unlock(&trace_types_lock);
}

static DECLARE_WORK(osnoise_hotplug_work, osnoise_hotplug_workfn);

/*
 * osnoise_cpu_init - CPU hotplug online callback function
 */
static int osnoise_cpu_init(unsigned int cpu)
{
	schedule_work_on(cpu, &osnoise_hotplug_work);
	return 0;
}

/*
 * osnoise_cpu_die - CPU hotplug offline callback function
 */
static int osnoise_cpu_die(unsigned int cpu)
{
	stop_kthread(cpu);
	return 0;
}

static void osnoise_init_hotplug_support(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "trace/osnoise:online",
				osnoise_cpu_init, osnoise_cpu_die);
	if (ret < 0)
		pr_warn(BANNER "Error to init cpu hotplug support\n");

	return;
}
#else /* CONFIG_HOTPLUG_CPU */
static void osnoise_init_hotplug_support(void)
{
	return;
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * osnoise_cpus_read - Read function for reading the "cpus" file
 * @filp: The active open file structure
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * Prints the "cpus" output into the user-provided buffer.
 */
static ssize_t
osnoise_cpus_read(struct file *filp, char __user *ubuf, size_t count,
		  loff_t *ppos)
{
	char *mask_str;
	int len;

	mutex_lock(&interface_lock);

	len = snprintf(NULL, 0, "%*pbl\n", cpumask_pr_args(&osnoise_cpumask)) + 1;
	mask_str = kmalloc(len, GFP_KERNEL);
	if (!mask_str) {
		count = -ENOMEM;
		goto out_unlock;
	}

	len = snprintf(mask_str, len, "%*pbl\n", cpumask_pr_args(&osnoise_cpumask));
	if (len >= count) {
		count = -EINVAL;
		goto out_free;
	}

	count = simple_read_from_buffer(ubuf, count, ppos, mask_str, len);

out_free:
	kfree(mask_str);
out_unlock:
	mutex_unlock(&interface_lock);

	return count;
}

/*
 * osnoise_cpus_write - Write function for "cpus" entry
 * @filp: The active open file structure
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in @file
 *
 * This function provides a write implementation for the "cpus"
 * interface to the osnoise trace. By default, it lists all  CPUs,
 * in this way, allowing osnoise threads to run on any online CPU
 * of the system. It serves to restrict the execution of osnoise to the
 * set of CPUs writing via this interface. Why not use "tracing_cpumask"?
 * Because the user might be interested in tracing what is running on
 * other CPUs. For instance, one might run osnoise in one HT CPU
 * while observing what is running on the sibling HT CPU.
 */
static ssize_t
osnoise_cpus_write(struct file *filp, const char __user *ubuf, size_t count,
		   loff_t *ppos)
{
	cpumask_var_t osnoise_cpumask_new;
	int running, err;
	char buf[256];

	if (count >= 256)
		return -EINVAL;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	if (!zalloc_cpumask_var(&osnoise_cpumask_new, GFP_KERNEL))
		return -ENOMEM;

	err = cpulist_parse(buf, osnoise_cpumask_new);
	if (err)
		goto err_free;

	/*
	 * trace_types_lock is taken to avoid concurrency on start/stop.
	 */
	mutex_lock(&trace_types_lock);
	running = osnoise_has_registered_instances();
	if (running)
		stop_per_cpu_kthreads();

	mutex_lock(&interface_lock);
	/*
	 * osnoise_cpumask is read by CPU hotplug operations.
	 */
	cpus_read_lock();

	cpumask_copy(&osnoise_cpumask, osnoise_cpumask_new);

	cpus_read_unlock();
	mutex_unlock(&interface_lock);

	if (running)
		start_per_cpu_kthreads();
	mutex_unlock(&trace_types_lock);

	free_cpumask_var(osnoise_cpumask_new);
	return count;

err_free:
	free_cpumask_var(osnoise_cpumask_new);

	return err;
}

/*
 * osnoise/runtime_us: cannot be greater than the period.
 */
static struct trace_min_max_param osnoise_runtime = {
	.lock	= &interface_lock,
	.val	= &osnoise_data.sample_runtime,
	.max	= &osnoise_data.sample_period,
	.min	= NULL,
};

/*
 * osnoise/period_us: cannot be smaller than the runtime.
 */
static struct trace_min_max_param osnoise_period = {
	.lock	= &interface_lock,
	.val	= &osnoise_data.sample_period,
	.max	= NULL,
	.min	= &osnoise_data.sample_runtime,
};

/*
 * osnoise/stop_tracing_us: no limit.
 */
static struct trace_min_max_param osnoise_stop_tracing_in = {
	.lock	= &interface_lock,
	.val	= &osnoise_data.stop_tracing,
	.max	= NULL,
	.min	= NULL,
};

/*
 * osnoise/stop_tracing_total_us: no limit.
 */
static struct trace_min_max_param osnoise_stop_tracing_total = {
	.lock	= &interface_lock,
	.val	= &osnoise_data.stop_tracing_total,
	.max	= NULL,
	.min	= NULL,
};

#ifdef CONFIG_TIMERLAT_TRACER
/*
 * osnoise/print_stack: print the stacktrace of the IRQ handler if the total
 * latency is higher than val.
 */
static struct trace_min_max_param osnoise_print_stack = {
	.lock	= &interface_lock,
	.val	= &osnoise_data.print_stack,
	.max	= NULL,
	.min	= NULL,
};

/*
 * osnoise/timerlat_period: min 100 us, max 1 s
 */
u64 timerlat_min_period = 100;
u64 timerlat_max_period = 1000000;
static struct trace_min_max_param timerlat_period = {
	.lock	= &interface_lock,
	.val	= &osnoise_data.timerlat_period,
	.max	= &timerlat_max_period,
	.min	= &timerlat_min_period,
};
#endif

static const struct file_operations cpus_fops = {
	.open		= tracing_open_generic,
	.read		= osnoise_cpus_read,
	.write		= osnoise_cpus_write,
	.llseek		= generic_file_llseek,
};

#ifdef CONFIG_TIMERLAT_TRACER
#ifdef CONFIG_STACKTRACE
static int init_timerlat_stack_tracefs(struct dentry *top_dir)
{
	struct dentry *tmp;

	tmp = tracefs_create_file("print_stack", TRACE_MODE_WRITE, top_dir,
				  &osnoise_print_stack, &trace_min_max_fops);
	if (!tmp)
		return -ENOMEM;

	return 0;
}
#else /* CONFIG_STACKTRACE */
static int init_timerlat_stack_tracefs(struct dentry *top_dir)
{
	return 0;
}
#endif /* CONFIG_STACKTRACE */

/*
 * init_timerlat_tracefs - A function to initialize the timerlat interface files
 */
static int init_timerlat_tracefs(struct dentry *top_dir)
{
	struct dentry *tmp;

	tmp = tracefs_create_file("timerlat_period_us", TRACE_MODE_WRITE, top_dir,
				  &timerlat_period, &trace_min_max_fops);
	if (!tmp)
		return -ENOMEM;

	return init_timerlat_stack_tracefs(top_dir);
}
#else /* CONFIG_TIMERLAT_TRACER */
static int init_timerlat_tracefs(struct dentry *top_dir)
{
	return 0;
}
#endif /* CONFIG_TIMERLAT_TRACER */

/*
 * init_tracefs - A function to initialize the tracefs interface files
 *
 * This function creates entries in tracefs for "osnoise" and "timerlat".
 * It creates these directories in the tracing directory, and within that
 * directory the use can change and view the configs.
 */
static int init_tracefs(void)
{
	struct dentry *top_dir;
	struct dentry *tmp;
	int ret;

	ret = tracing_init_dentry();
	if (ret)
		return -ENOMEM;

	top_dir = tracefs_create_dir("osnoise", NULL);
	if (!top_dir)
		return 0;

	tmp = tracefs_create_file("period_us", TRACE_MODE_WRITE, top_dir,
				  &osnoise_period, &trace_min_max_fops);
	if (!tmp)
		goto err;

	tmp = tracefs_create_file("runtime_us", TRACE_MODE_WRITE, top_dir,
				  &osnoise_runtime, &trace_min_max_fops);
	if (!tmp)
		goto err;

	tmp = tracefs_create_file("stop_tracing_us", TRACE_MODE_WRITE, top_dir,
				  &osnoise_stop_tracing_in, &trace_min_max_fops);
	if (!tmp)
		goto err;

	tmp = tracefs_create_file("stop_tracing_total_us", TRACE_MODE_WRITE, top_dir,
				  &osnoise_stop_tracing_total, &trace_min_max_fops);
	if (!tmp)
		goto err;

	tmp = trace_create_file("cpus", TRACE_MODE_WRITE, top_dir, NULL, &cpus_fops);
	if (!tmp)
		goto err;

	ret = init_timerlat_tracefs(top_dir);
	if (ret)
		goto err;

	return 0;

err:
	tracefs_remove(top_dir);
	return -ENOMEM;
}

static int osnoise_hook_events(void)
{
	int retval;

	/*
	 * Trace is already hooked, we are re-enabling from
	 * a stop_tracing_*.
	 */
	if (trace_osnoise_callback_enabled)
		return 0;

	retval = hook_irq_events();
	if (retval)
		return -EINVAL;

	retval = hook_softirq_events();
	if (retval)
		goto out_unhook_irq;

	retval = hook_thread_events();
	/*
	 * All fine!
	 */
	if (!retval)
		return 0;

	unhook_softirq_events();
out_unhook_irq:
	unhook_irq_events();
	return -EINVAL;
}

/*
 * osnoise_workload_start - start the workload and hook to events
 */
static int osnoise_workload_start(void)
{
	int retval;

	/*
	 * Instances need to be registered after calling workload
	 * start. Hence, if there is already an instance, the
	 * workload was already registered. Otherwise, this
	 * code is on the way to register the first instance,
	 * and the workload will start.
	 */
	if (osnoise_has_registered_instances())
		return 0;

	osn_var_reset_all();

	retval = osnoise_hook_events();
	if (retval)
		return retval;

	/*
	 * Make sure that ftrace_nmi_enter/exit() see reset values
	 * before enabling trace_osnoise_callback_enabled.
	 */
	barrier();
	trace_osnoise_callback_enabled = true;

	retval = start_per_cpu_kthreads();
	if (retval) {
		unhook_irq_events();
		return retval;
	}

	return 0;
}

/*
 * osnoise_workload_stop - stop the workload and unhook the events
 */
static void osnoise_workload_stop(void)
{
	/*
	 * Instances need to be unregistered before calling
	 * stop. Hence, if there is a registered instance, more
	 * than one instance is running, and the workload will not
	 * yet stop. Otherwise, this code is on the way to disable
	 * the last instance, and the workload can stop.
	 */
	if (osnoise_has_registered_instances())
		return;

	trace_osnoise_callback_enabled = false;
	/*
	 * Make sure that ftrace_nmi_enter/exit() see
	 * trace_osnoise_callback_enabled as false before continuing.
	 */
	barrier();

	stop_per_cpu_kthreads();

	unhook_irq_events();
	unhook_softirq_events();
	unhook_thread_events();
}

static void osnoise_tracer_start(struct trace_array *tr)
{
	int retval;

	/*
	 * If the instance is already registered, there is no need to
	 * register it again.
	 */
	if (osnoise_instance_registered(tr))
		return;

	retval = osnoise_workload_start();
	if (retval)
		pr_err(BANNER "Error starting osnoise tracer\n");

	osnoise_register_instance(tr);
}

static void osnoise_tracer_stop(struct trace_array *tr)
{
	osnoise_unregister_instance(tr);
	osnoise_workload_stop();
}

static int osnoise_tracer_init(struct trace_array *tr)
{
	/*
	 * Only allow osnoise tracer if timerlat tracer is not running
	 * already.
	 */
	if (timerlat_enabled())
		return -EBUSY;

	tr->max_latency = 0;

	osnoise_tracer_start(tr);
	return 0;
}

static void osnoise_tracer_reset(struct trace_array *tr)
{
	osnoise_tracer_stop(tr);
}

static struct tracer osnoise_tracer __read_mostly = {
	.name		= "osnoise",
	.init		= osnoise_tracer_init,
	.reset		= osnoise_tracer_reset,
	.start		= osnoise_tracer_start,
	.stop		= osnoise_tracer_stop,
	.print_header	= print_osnoise_headers,
	.allow_instances = true,
};

#ifdef CONFIG_TIMERLAT_TRACER
static void timerlat_tracer_start(struct trace_array *tr)
{
	int retval;

	/*
	 * If the instance is already registered, there is no need to
	 * register it again.
	 */
	if (osnoise_instance_registered(tr))
		return;

	retval = osnoise_workload_start();
	if (retval)
		pr_err(BANNER "Error starting timerlat tracer\n");

	osnoise_register_instance(tr);

	return;
}

static void timerlat_tracer_stop(struct trace_array *tr)
{
	int cpu;

	osnoise_unregister_instance(tr);

	/*
	 * Instruct the threads to stop only if this is the last instance.
	 */
	if (!osnoise_has_registered_instances()) {
		for_each_online_cpu(cpu)
			per_cpu(per_cpu_osnoise_var, cpu).sampling = 0;
	}

	osnoise_workload_stop();
}

static int timerlat_tracer_init(struct trace_array *tr)
{
	/*
	 * Only allow timerlat tracer if osnoise tracer is not running already.
	 */
	if (osnoise_has_registered_instances() && !osnoise_data.timerlat_tracer)
		return -EBUSY;

	/*
	 * If this is the first instance, set timerlat_tracer to block
	 * osnoise tracer start.
	 */
	if (!osnoise_has_registered_instances())
		osnoise_data.timerlat_tracer = 1;

	tr->max_latency = 0;
	timerlat_tracer_start(tr);

	return 0;
}

static void timerlat_tracer_reset(struct trace_array *tr)
{
	timerlat_tracer_stop(tr);

	/*
	 * If this is the last instance, reset timerlat_tracer allowing
	 * osnoise to be started.
	 */
	if (!osnoise_has_registered_instances())
		osnoise_data.timerlat_tracer = 0;
}

static struct tracer timerlat_tracer __read_mostly = {
	.name		= "timerlat",
	.init		= timerlat_tracer_init,
	.reset		= timerlat_tracer_reset,
	.start		= timerlat_tracer_start,
	.stop		= timerlat_tracer_stop,
	.print_header	= print_timerlat_headers,
	.allow_instances = true,
};

__init static int init_timerlat_tracer(void)
{
	return register_tracer(&timerlat_tracer);
}
#else /* CONFIG_TIMERLAT_TRACER */
__init static int init_timerlat_tracer(void)
{
	return 0;
}
#endif /* CONFIG_TIMERLAT_TRACER */

__init static int init_osnoise_tracer(void)
{
	int ret;

	mutex_init(&interface_lock);

	cpumask_copy(&osnoise_cpumask, cpu_all_mask);

	ret = register_tracer(&osnoise_tracer);
	if (ret) {
		pr_err(BANNER "Error registering osnoise!\n");
		return ret;
	}

	ret = init_timerlat_tracer();
	if (ret) {
		pr_err(BANNER "Error registering timerlat!\n");
		return ret;
	}

	osnoise_init_hotplug_support();

	INIT_LIST_HEAD_RCU(&osnoise_instances);

	init_tracefs();

	return 0;
}
late_initcall(init_osnoise_tracer);
