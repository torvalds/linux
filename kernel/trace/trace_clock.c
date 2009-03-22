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
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/sched.h>
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
	unsigned long flags;
	u64 clock;

	/*
	 * sched_clock() is an architecture implemented, fast, scalable,
	 * lockless clock. It is not guaranteed to be coherent across
	 * CPUs, nor across CPU idle events.
	 */
	raw_local_irq_save(flags);
	clock = sched_clock();
	raw_local_irq_restore(flags);

	return clock;
}

/*
 * trace_clock(): 'inbetween' trace clock. Not completely serialized,
 * but not completely incorrect when crossing CPUs either.
 *
 * This is based on cpu_clock(), which will allow at most ~1 jiffy of
 * jitter between CPUs. So it's a pretty scalable clock, but there
 * can be offsets in the trace data.
 */
u64 notrace trace_clock(void)
{
	return cpu_clock(raw_smp_processor_id());
}


/*
 * trace_clock_global(): special globally coherent trace clock
 *
 * It has higher overhead than the other trace clocks but is still
 * an order of magnitude faster than GTOD derived hardware clocks.
 *
 * Used by plugins that need globally coherent timestamps.
 */

static u64 prev_trace_clock_time;

static raw_spinlock_t trace_clock_lock ____cacheline_aligned_in_smp =
	(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

u64 notrace trace_clock_global(void)
{
	unsigned long flags;
	int this_cpu;
	u64 now;

	raw_local_irq_save(flags);

	this_cpu = raw_smp_processor_id();
	now = cpu_clock(this_cpu);
	/*
	 * If in an NMI context then dont risk lockups and return the
	 * cpu_clock() time:
	 */
	if (unlikely(in_nmi()))
		goto out;

	__raw_spin_lock(&trace_clock_lock);

	/*
	 * TODO: if this happens often then maybe we should reset
	 * my_scd->clock to prev_trace_clock_time+1, to make sure
	 * we start ticking with the local clock from now on?
	 */
	if ((s64)(now - prev_trace_clock_time) < 0)
		now = prev_trace_clock_time + 1;

	prev_trace_clock_time = now;

	__raw_spin_unlock(&trace_clock_lock);

 out:
	raw_local_irq_restore(flags);

	return now;
}
