// SPDX-License-Identifier: GPL-2.0
/*
 * tracing clocks
 *
 *  Copyright (C) 2009 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Implements 3 trace clock variants, with differing scalability/precision
 * tradeoffs:
 *
 *  -   local: CPU-local trace clock
 *  -  medium: scalable global clock with some jitter
 *  -  global: globally monotonic, serialized clock
 *
 * Tracer plugins will chose a default from these clocks.
 */
#include <linux/spinlock.h>
#include <linux/irqflags.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/ktime.h>
#include <linux/trace_clock.h>

/*
 * trace_clock_local(): the simplest and least coherent tracing clock.
 *
 * Useful for tracing that does not cross to other CPUs nor
 * does it go through idle events.
 */
u64 notrace trace_clock_local(void)
{
	u64 clock;

	/*
	 * sched_clock() is an architecture implemented, fast, scalable,
	 * lockless clock. It is not guaranteed to be coherent across
	 * CPUs, nor across CPU idle events.
	 */
	preempt_disable_notrace();
	clock = sched_clock();
	preempt_enable_notrace();

	return clock;
}
EXPORT_SYMBOL_GPL(trace_clock_local);

/*
 * trace_clock(): 'between' trace clock. Not completely serialized,
 * but not completely incorrect when crossing CPUs either.
 *
 * This is based on cpu_clock(), which will allow at most ~1 jiffy of
 * jitter between CPUs. So it's a pretty scalable clock, but there
 * can be offsets in the trace data.
 */
u64 notrace trace_clock(void)
{
	return local_clock();
}
EXPORT_SYMBOL_GPL(trace_clock);

/*
 * trace_jiffy_clock(): Simply use jiffies as a clock counter.
 * Note that this use of jiffies_64 is not completely safe on
 * 32-bit systems. But the window is tiny, and the effect if
 * we are affected is that we will have an obviously bogus
 * timestamp on a trace event - i.e. not life threatening.
 */
u64 notrace trace_clock_jiffies(void)
{
	return jiffies_64_to_clock_t(jiffies_64 - INITIAL_JIFFIES);
}
EXPORT_SYMBOL_GPL(trace_clock_jiffies);

/*
 * trace_clock_global(): special globally coherent trace clock
 *
 * It has higher overhead than the other trace clocks but is still
 * an order of magnitude faster than GTOD derived hardware clocks.
 *
 * Used by plugins that need globally coherent timestamps.
 */

/* keep prev_time and lock in the same cacheline. */
static struct {
	u64 prev_time;
	arch_spinlock_t lock;
} trace_clock_struct ____cacheline_aligned_in_smp =
	{
		.lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED,
	};

u64 notrace trace_clock_global(void)
{
	unsigned long flags;
	int this_cpu;
	u64 now, prev_time;

	raw_local_irq_save(flags);

	this_cpu = raw_smp_processor_id();

	/*
	 * The global clock "guarantees" that the events are ordered
	 * between CPUs. But if two events on two different CPUS call
	 * trace_clock_global at roughly the same time, it really does
	 * not matter which one gets the earlier time. Just make sure
	 * that the same CPU will always show a monotonic clock.
	 *
	 * Use a read memory barrier to get the latest written
	 * time that was recorded.
	 */
	smp_rmb();
	prev_time = READ_ONCE(trace_clock_struct.prev_time);
	now = sched_clock_cpu(this_cpu);

	/* Make sure that now is always greater than or equal to prev_time */
	if ((s64)(now - prev_time) < 0)
		now = prev_time;

	/*
	 * If in an NMI context then dont risk lockups and simply return
	 * the current time.
	 */
	if (unlikely(in_nmi()))
		goto out;

	/* Tracing can cause strange recursion, always use a try lock */
	if (arch_spin_trylock(&trace_clock_struct.lock)) {
		/* Reread prev_time in case it was already updated */
		prev_time = READ_ONCE(trace_clock_struct.prev_time);
		if ((s64)(now - prev_time) < 0)
			now = prev_time;

		trace_clock_struct.prev_time = now;

		/* The unlock acts as the wmb for the above rmb */
		arch_spin_unlock(&trace_clock_struct.lock);
	}
 out:
	raw_local_irq_restore(flags);

	return now;
}
EXPORT_SYMBOL_GPL(trace_clock_global);

static atomic64_t trace_counter;

/*
 * trace_clock_counter(): simply an atomic counter.
 * Use the trace_counter "counter" for cases where you do not care
 * about timings, but are interested in strict ordering.
 */
u64 notrace trace_clock_counter(void)
{
	return atomic64_add_return(1, &trace_counter);
}
