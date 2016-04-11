/*
 * sched_clock for unstable cpu clocks
 *
 *  Copyright (C) 2008 Red Hat, Inc., Peter Zijlstra
 *
 *  Updates and enhancements:
 *    Copyright (C) 2008 Red Hat, Inc. Steven Rostedt <srostedt@redhat.com>
 *
 * Based on code by:
 *   Ingo Molnar <mingo@redhat.com>
 *   Guillaume Chazarain <guichaz@gmail.com>
 *
 *
 * What:
 *
 * cpu_clock(i) provides a fast (execution time) high resolution
 * clock with bounded drift between CPUs. The value of cpu_clock(i)
 * is monotonic for constant i. The timestamp returned is in nanoseconds.
 *
 * ######################### BIG FAT WARNING ##########################
 * # when comparing cpu_clock(i) to cpu_clock(j) for i != j, time can #
 * # go backwards !!                                                  #
 * ####################################################################
 *
 * There is no strict promise about the base, although it tends to start
 * at 0 on boot (but people really shouldn't rely on that).
 *
 * cpu_clock(i)       -- can be used from any context, including NMI.
 * local_clock()      -- is cpu_clock() on the current cpu.
 *
 * sched_clock_cpu(i)
 *
 * How:
 *
 * The implementation either uses sched_clock() when
 * !CONFIG_HAVE_UNSTABLE_SCHED_CLOCK, which means in that case the
 * sched_clock() is assumed to provide these properties (mostly it means
 * the architecture provides a globally synchronized highres time source).
 *
 * Otherwise it tries to create a semi stable clock from a mixture of other
 * clocks, including:
 *
 *  - GTOD (clock monotomic)
 *  - sched_clock()
 *  - explicit idle events
 *
 * We use GTOD as base and use sched_clock() deltas to improve resolution. The
 * deltas are filtered to provide monotonicity and keeping it within an
 * expected window.
 *
 * Furthermore, explicit sleep and wakeup hooks allow us to account for time
 * that is otherwise invisible (TSC gets stopped).
 *
 */
#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/static_key.h>
#include <linux/workqueue.h>
#include <linux/compiler.h>
#include <linux/tick.h>

/*
 * Scheduler clock - returns current time in nanosec units.
 * This is default implementation.
 * Architectures and sub-architectures can override this.
 */
unsigned long long __weak sched_clock(void)
{
	return (unsigned long long)(jiffies - INITIAL_JIFFIES)
					* (NSEC_PER_SEC / HZ);
}
EXPORT_SYMBOL_GPL(sched_clock);

__read_mostly int sched_clock_running;

#ifdef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
static struct static_key __sched_clock_stable = STATIC_KEY_INIT;
static int __sched_clock_stable_early;

int sched_clock_stable(void)
{
	return static_key_false(&__sched_clock_stable);
}

static void __set_sched_clock_stable(void)
{
	if (!sched_clock_stable())
		static_key_slow_inc(&__sched_clock_stable);

	tick_dep_clear(TICK_DEP_BIT_CLOCK_UNSTABLE);
}

void set_sched_clock_stable(void)
{
	__sched_clock_stable_early = 1;

	smp_mb(); /* matches sched_clock_init() */

	if (!sched_clock_running)
		return;

	__set_sched_clock_stable();
}

static void __clear_sched_clock_stable(struct work_struct *work)
{
	/* XXX worry about clock continuity */
	if (sched_clock_stable())
		static_key_slow_dec(&__sched_clock_stable);

	tick_dep_set(TICK_DEP_BIT_CLOCK_UNSTABLE);
}

static DECLARE_WORK(sched_clock_work, __clear_sched_clock_stable);

void clear_sched_clock_stable(void)
{
	__sched_clock_stable_early = 0;

	smp_mb(); /* matches sched_clock_init() */

	if (!sched_clock_running)
		return;

	schedule_work(&sched_clock_work);
}

struct sched_clock_data {
	u64			tick_raw;
	u64			tick_gtod;
	u64			clock;
};

static DEFINE_PER_CPU_SHARED_ALIGNED(struct sched_clock_data, sched_clock_data);

static inline struct sched_clock_data *this_scd(void)
{
	return this_cpu_ptr(&sched_clock_data);
}

static inline struct sched_clock_data *cpu_sdc(int cpu)
{
	return &per_cpu(sched_clock_data, cpu);
}

void sched_clock_init(void)
{
	u64 ktime_now = ktime_to_ns(ktime_get());
	int cpu;

	for_each_possible_cpu(cpu) {
		struct sched_clock_data *scd = cpu_sdc(cpu);

		scd->tick_raw = 0;
		scd->tick_gtod = ktime_now;
		scd->clock = ktime_now;
	}

	sched_clock_running = 1;

	/*
	 * Ensure that it is impossible to not do a static_key update.
	 *
	 * Either {set,clear}_sched_clock_stable() must see sched_clock_running
	 * and do the update, or we must see their __sched_clock_stable_early
	 * and do the update, or both.
	 */
	smp_mb(); /* matches {set,clear}_sched_clock_stable() */

	if (__sched_clock_stable_early)
		__set_sched_clock_stable();
	else
		__clear_sched_clock_stable(NULL);
}

/*
 * min, max except they take wrapping into account
 */

static inline u64 wrap_min(u64 x, u64 y)
{
	return (s64)(x - y) < 0 ? x : y;
}

static inline u64 wrap_max(u64 x, u64 y)
{
	return (s64)(x - y) > 0 ? x : y;
}

/*
 * update the percpu scd from the raw @now value
 *
 *  - filter out backward motion
 *  - use the GTOD tick value to create a window to filter crazy TSC values
 */
static u64 sched_clock_local(struct sched_clock_data *scd)
{
	u64 now, clock, old_clock, min_clock, max_clock;
	s64 delta;

again:
	now = sched_clock();
	delta = now - scd->tick_raw;
	if (unlikely(delta < 0))
		delta = 0;

	old_clock = scd->clock;

	/*
	 * scd->clock = clamp(scd->tick_gtod + delta,
	 *		      max(scd->tick_gtod, scd->clock),
	 *		      scd->tick_gtod + TICK_NSEC);
	 */

	clock = scd->tick_gtod + delta;
	min_clock = wrap_max(scd->tick_gtod, old_clock);
	max_clock = wrap_max(old_clock, scd->tick_gtod + TICK_NSEC);

	clock = wrap_max(clock, min_clock);
	clock = wrap_min(clock, max_clock);

	if (cmpxchg64(&scd->clock, old_clock, clock) != old_clock)
		goto again;

	return clock;
}

static u64 sched_clock_remote(struct sched_clock_data *scd)
{
	struct sched_clock_data *my_scd = this_scd();
	u64 this_clock, remote_clock;
	u64 *ptr, old_val, val;

#if BITS_PER_LONG != 64
again:
	/*
	 * Careful here: The local and the remote clock values need to
	 * be read out atomic as we need to compare the values and
	 * then update either the local or the remote side. So the
	 * cmpxchg64 below only protects one readout.
	 *
	 * We must reread via sched_clock_local() in the retry case on
	 * 32bit as an NMI could use sched_clock_local() via the
	 * tracer and hit between the readout of
	 * the low32bit and the high 32bit portion.
	 */
	this_clock = sched_clock_local(my_scd);
	/*
	 * We must enforce atomic readout on 32bit, otherwise the
	 * update on the remote cpu can hit inbetween the readout of
	 * the low32bit and the high 32bit portion.
	 */
	remote_clock = cmpxchg64(&scd->clock, 0, 0);
#else
	/*
	 * On 64bit the read of [my]scd->clock is atomic versus the
	 * update, so we can avoid the above 32bit dance.
	 */
	sched_clock_local(my_scd);
again:
	this_clock = my_scd->clock;
	remote_clock = scd->clock;
#endif

	/*
	 * Use the opportunity that we have both locks
	 * taken to couple the two clocks: we take the
	 * larger time as the latest time for both
	 * runqueues. (this creates monotonic movement)
	 */
	if (likely((s64)(remote_clock - this_clock) < 0)) {
		ptr = &scd->clock;
		old_val = remote_clock;
		val = this_clock;
	} else {
		/*
		 * Should be rare, but possible:
		 */
		ptr = &my_scd->clock;
		old_val = this_clock;
		val = remote_clock;
	}

	if (cmpxchg64(ptr, old_val, val) != old_val)
		goto again;

	return val;
}

/*
 * Similar to cpu_clock(), but requires local IRQs to be disabled.
 *
 * See cpu_clock().
 */
u64 sched_clock_cpu(int cpu)
{
	struct sched_clock_data *scd;
	u64 clock;

	if (sched_clock_stable())
		return sched_clock();

	if (unlikely(!sched_clock_running))
		return 0ull;

	preempt_disable_notrace();
	scd = cpu_sdc(cpu);

	if (cpu != smp_processor_id())
		clock = sched_clock_remote(scd);
	else
		clock = sched_clock_local(scd);
	preempt_enable_notrace();

	return clock;
}
EXPORT_SYMBOL_GPL(sched_clock_cpu);

void sched_clock_tick(void)
{
	struct sched_clock_data *scd;
	u64 now, now_gtod;

	if (sched_clock_stable())
		return;

	if (unlikely(!sched_clock_running))
		return;

	WARN_ON_ONCE(!irqs_disabled());

	scd = this_scd();
	now_gtod = ktime_to_ns(ktime_get());
	now = sched_clock();

	scd->tick_raw = now;
	scd->tick_gtod = now_gtod;
	sched_clock_local(scd);
}

/*
 * We are going deep-idle (irqs are disabled):
 */
void sched_clock_idle_sleep_event(void)
{
	sched_clock_cpu(smp_processor_id());
}
EXPORT_SYMBOL_GPL(sched_clock_idle_sleep_event);

/*
 * We just idled delta nanoseconds (called with irqs disabled):
 */
void sched_clock_idle_wakeup_event(u64 delta_ns)
{
	if (timekeeping_suspended)
		return;

	sched_clock_tick();
	touch_softlockup_watchdog_sched();
}
EXPORT_SYMBOL_GPL(sched_clock_idle_wakeup_event);

#else /* CONFIG_HAVE_UNSTABLE_SCHED_CLOCK */

void sched_clock_init(void)
{
	sched_clock_running = 1;
}

u64 sched_clock_cpu(int cpu)
{
	if (unlikely(!sched_clock_running))
		return 0;

	return sched_clock();
}
#endif /* CONFIG_HAVE_UNSTABLE_SCHED_CLOCK */

/*
 * Running clock - returns the time that has elapsed while a guest has been
 * running.
 * On a guest this value should be local_clock minus the time the guest was
 * suspended by the hypervisor (for any reason).
 * On bare metal this function should return the same as local_clock.
 * Architectures and sub-architectures can override this.
 */
u64 __weak running_clock(void)
{
	return local_clock();
}
