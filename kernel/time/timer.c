// SPDX-License-Identifier: GPL-2.0
/*
 *  Kernel internal timers
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-01-28  Modified by Finn Arne Gangstad to make timers scale better.
 *
 *  1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *              "A Kernel Model for Precision Timekeeping" by Dave Mills
 *  1998-12-24  Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *              serialize accesses to xtime/lost_ticks).
 *                              Copyright (C) 1998  Andrea Arcangeli
 *  1999-03-10  Improved NTP compatibility by Ulrich Windl
 *  2002-05-31	Move sys_sysinfo here and make its locking sane, Robert Love
 *  2000-10-05  Implemented scalable SMP per-CPU timer handling.
 *                              Copyright (C) 2000, 2001, 2002  Ingo Molnar
 *              Designed by David S. Miller, Alexey Kuznetsov and Ingo Molnar
 */

#include <linux/kernel_stat.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pid_namespace.h>
#include <linux/notifier.h>
#include <linux/thread_info.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/posix-timers.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/tick.h>
#include <linux/kallsyms.h>
#include <linux/irq_work.h>
#include <linux/sched/signal.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/nohz.h>
#include <linux/sched/debug.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/random.h>
#include <linux/sysctl.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

#include "tick-internal.h"
#include "timer_migration.h"

#define CREATE_TRACE_POINTS
#include <trace/events/timer.h>

__visible u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

/*
 * The timer wheel has LVL_DEPTH array levels. Each level provides an array of
 * LVL_SIZE buckets. Each level is driven by its own clock and therefore each
 * level has a different granularity.
 *
 * The level granularity is:		LVL_CLK_DIV ^ level
 * The level clock frequency is:	HZ / (LVL_CLK_DIV ^ level)
 *
 * The array level of a newly armed timer depends on the relative expiry
 * time. The farther the expiry time is away the higher the array level and
 * therefore the granularity becomes.
 *
 * Contrary to the original timer wheel implementation, which aims for 'exact'
 * expiry of the timers, this implementation removes the need for recascading
 * the timers into the lower array levels. The previous 'classic' timer wheel
 * implementation of the kernel already violated the 'exact' expiry by adding
 * slack to the expiry time to provide batched expiration. The granularity
 * levels provide implicit batching.
 *
 * This is an optimization of the original timer wheel implementation for the
 * majority of the timer wheel use cases: timeouts. The vast majority of
 * timeout timers (networking, disk I/O ...) are canceled before expiry. If
 * the timeout expires it indicates that normal operation is disturbed, so it
 * does not matter much whether the timeout comes with a slight delay.
 *
 * The only exception to this are networking timers with a small expiry
 * time. They rely on the granularity. Those fit into the first wheel level,
 * which has HZ granularity.
 *
 * We don't have cascading anymore. timers with a expiry time above the
 * capacity of the last wheel level are force expired at the maximum timeout
 * value of the last wheel level. From data sampling we know that the maximum
 * value observed is 5 days (network connection tracking), so this should not
 * be an issue.
 *
 * The currently chosen array constants values are a good compromise between
 * array size and granularity.
 *
 * This results in the following granularity and range levels:
 *
 * HZ 1000 steps
 * Level Offset  Granularity            Range
 *  0      0         1 ms                0 ms -         63 ms
 *  1     64         8 ms               64 ms -        511 ms
 *  2    128        64 ms              512 ms -       4095 ms (512ms - ~4s)
 *  3    192       512 ms             4096 ms -      32767 ms (~4s - ~32s)
 *  4    256      4096 ms (~4s)      32768 ms -     262143 ms (~32s - ~4m)
 *  5    320     32768 ms (~32s)    262144 ms -    2097151 ms (~4m - ~34m)
 *  6    384    262144 ms (~4m)    2097152 ms -   16777215 ms (~34m - ~4h)
 *  7    448   2097152 ms (~34m)  16777216 ms -  134217727 ms (~4h - ~1d)
 *  8    512  16777216 ms (~4h)  134217728 ms - 1073741822 ms (~1d - ~12d)
 *
 * HZ  300
 * Level Offset  Granularity            Range
 *  0	   0         3 ms                0 ms -        210 ms
 *  1	  64        26 ms              213 ms -       1703 ms (213ms - ~1s)
 *  2	 128       213 ms             1706 ms -      13650 ms (~1s - ~13s)
 *  3	 192      1706 ms (~1s)      13653 ms -     109223 ms (~13s - ~1m)
 *  4	 256     13653 ms (~13s)    109226 ms -     873810 ms (~1m - ~14m)
 *  5	 320    109226 ms (~1m)     873813 ms -    6990503 ms (~14m - ~1h)
 *  6	 384    873813 ms (~14m)   6990506 ms -   55924050 ms (~1h - ~15h)
 *  7	 448   6990506 ms (~1h)   55924053 ms -  447392423 ms (~15h - ~5d)
 *  8    512  55924053 ms (~15h) 447392426 ms - 3579139406 ms (~5d - ~41d)
 *
 * HZ  250
 * Level Offset  Granularity            Range
 *  0	   0         4 ms                0 ms -        255 ms
 *  1	  64        32 ms              256 ms -       2047 ms (256ms - ~2s)
 *  2	 128       256 ms             2048 ms -      16383 ms (~2s - ~16s)
 *  3	 192      2048 ms (~2s)      16384 ms -     131071 ms (~16s - ~2m)
 *  4	 256     16384 ms (~16s)    131072 ms -    1048575 ms (~2m - ~17m)
 *  5	 320    131072 ms (~2m)    1048576 ms -    8388607 ms (~17m - ~2h)
 *  6	 384   1048576 ms (~17m)   8388608 ms -   67108863 ms (~2h - ~18h)
 *  7	 448   8388608 ms (~2h)   67108864 ms -  536870911 ms (~18h - ~6d)
 *  8    512  67108864 ms (~18h) 536870912 ms - 4294967288 ms (~6d - ~49d)
 *
 * HZ  100
 * Level Offset  Granularity            Range
 *  0	   0         10 ms               0 ms -        630 ms
 *  1	  64         80 ms             640 ms -       5110 ms (640ms - ~5s)
 *  2	 128        640 ms            5120 ms -      40950 ms (~5s - ~40s)
 *  3	 192       5120 ms (~5s)     40960 ms -     327670 ms (~40s - ~5m)
 *  4	 256      40960 ms (~40s)   327680 ms -    2621430 ms (~5m - ~43m)
 *  5	 320     327680 ms (~5m)   2621440 ms -   20971510 ms (~43m - ~5h)
 *  6	 384    2621440 ms (~43m) 20971520 ms -  167772150 ms (~5h - ~1d)
 *  7	 448   20971520 ms (~5h) 167772160 ms - 1342177270 ms (~1d - ~15d)
 */

/* Clock divisor for the next level */
#define LVL_CLK_SHIFT	3
#define LVL_CLK_DIV	(1UL << LVL_CLK_SHIFT)
#define LVL_CLK_MASK	(LVL_CLK_DIV - 1)
#define LVL_SHIFT(n)	((n) * LVL_CLK_SHIFT)
#define LVL_GRAN(n)	(1UL << LVL_SHIFT(n))

/*
 * The time start value for each level to select the bucket at enqueue
 * time. We start from the last possible delta of the previous level
 * so that we can later add an extra LVL_GRAN(n) to n (see calc_index()).
 */
#define LVL_START(n)	((LVL_SIZE - 1) << (((n) - 1) * LVL_CLK_SHIFT))

/* Size of each clock level */
#define LVL_BITS	6
#define LVL_SIZE	(1UL << LVL_BITS)
#define LVL_MASK	(LVL_SIZE - 1)
#define LVL_OFFS(n)	((n) * LVL_SIZE)

/* Level depth */
#if HZ > 100
# define LVL_DEPTH	9
# else
# define LVL_DEPTH	8
#endif

/* The cutoff (max. capacity of the wheel) */
#define WHEEL_TIMEOUT_CUTOFF	(LVL_START(LVL_DEPTH))
#define WHEEL_TIMEOUT_MAX	(WHEEL_TIMEOUT_CUTOFF - LVL_GRAN(LVL_DEPTH - 1))

/*
 * The resulting wheel size. If NOHZ is configured we allocate two
 * wheels so we have a separate storage for the deferrable timers.
 */
#define WHEEL_SIZE	(LVL_SIZE * LVL_DEPTH)

#ifdef CONFIG_NO_HZ_COMMON
/*
 * If multiple bases need to be locked, use the base ordering for lock
 * nesting, i.e. lowest number first.
 */
# define NR_BASES	3
# define BASE_LOCAL	0
# define BASE_GLOBAL	1
# define BASE_DEF	2
#else
# define NR_BASES	1
# define BASE_LOCAL	0
# define BASE_GLOBAL	0
# define BASE_DEF	0
#endif

/**
 * struct timer_base - Per CPU timer base (number of base depends on config)
 * @lock:		Lock protecting the timer_base
 * @running_timer:	When expiring timers, the lock is dropped. To make
 *			sure not to race against deleting/modifying a
 *			currently running timer, the pointer is set to the
 *			timer, which expires at the moment. If no timer is
 *			running, the pointer is NULL.
 * @expiry_lock:	PREEMPT_RT only: Lock is taken in softirq around
 *			timer expiry callback execution and when trying to
 *			delete a running timer and it wasn't successful in
 *			the first glance. It prevents priority inversion
 *			when callback was preempted on a remote CPU and a
 *			caller tries to delete the running timer. It also
 *			prevents a life lock, when the task which tries to
 *			delete a timer preempted the softirq thread which
 *			is running the timer callback function.
 * @timer_waiters:	PREEMPT_RT only: Tells, if there is a waiter
 *			waiting for the end of the timer callback function
 *			execution.
 * @clk:		clock of the timer base; is updated before enqueue
 *			of a timer; during expiry, it is 1 offset ahead of
 *			jiffies to avoid endless requeuing to current
 *			jiffies
 * @next_expiry:	expiry value of the first timer; it is updated when
 *			finding the next timer and during enqueue; the
 *			value is not valid, when next_expiry_recalc is set
 * @cpu:		Number of CPU the timer base belongs to
 * @next_expiry_recalc: States, whether a recalculation of next_expiry is
 *			required. Value is set true, when a timer was
 *			deleted.
 * @is_idle:		Is set, when timer_base is idle. It is triggered by NOHZ
 *			code. This state is only used in standard
 *			base. Deferrable timers, which are enqueued remotely
 *			never wake up an idle CPU. So no matter of supporting it
 *			for this base.
 * @timers_pending:	Is set, when a timer is pending in the base. It is only
 *			reliable when next_expiry_recalc is not set.
 * @pending_map:	bitmap of the timer wheel; each bit reflects a
 *			bucket of the wheel. When a bit is set, at least a
 *			single timer is enqueued in the related bucket.
 * @vectors:		Array of lists; Each array member reflects a bucket
 *			of the timer wheel. The list contains all timers
 *			which are enqueued into a specific bucket.
 */
struct timer_base {
	raw_spinlock_t		lock;
	struct timer_list	*running_timer;
#ifdef CONFIG_PREEMPT_RT
	spinlock_t		expiry_lock;
	atomic_t		timer_waiters;
#endif
	unsigned long		clk;
	unsigned long		next_expiry;
	unsigned int		cpu;
	bool			next_expiry_recalc;
	bool			is_idle;
	bool			timers_pending;
	DECLARE_BITMAP(pending_map, WHEEL_SIZE);
	struct hlist_head	vectors[WHEEL_SIZE];
} ____cacheline_aligned;

static DEFINE_PER_CPU(struct timer_base, timer_bases[NR_BASES]);

#ifdef CONFIG_NO_HZ_COMMON

static DEFINE_STATIC_KEY_FALSE(timers_nohz_active);
static DEFINE_MUTEX(timer_keys_mutex);

static void timer_update_keys(struct work_struct *work);
static DECLARE_WORK(timer_update_work, timer_update_keys);

#ifdef CONFIG_SMP
static unsigned int sysctl_timer_migration = 1;

DEFINE_STATIC_KEY_FALSE(timers_migration_enabled);

static void timers_update_migration(void)
{
	if (sysctl_timer_migration && tick_nohz_active)
		static_branch_enable(&timers_migration_enabled);
	else
		static_branch_disable(&timers_migration_enabled);
}

#ifdef CONFIG_SYSCTL
static int timer_migration_handler(const struct ctl_table *table, int write,
			    void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	mutex_lock(&timer_keys_mutex);
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (!ret && write)
		timers_update_migration();
	mutex_unlock(&timer_keys_mutex);
	return ret;
}

static struct ctl_table timer_sysctl[] = {
	{
		.procname	= "timer_migration",
		.data		= &sysctl_timer_migration,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= timer_migration_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

static int __init timer_sysctl_init(void)
{
	register_sysctl("kernel", timer_sysctl);
	return 0;
}
device_initcall(timer_sysctl_init);
#endif /* CONFIG_SYSCTL */
#else /* CONFIG_SMP */
static inline void timers_update_migration(void) { }
#endif /* !CONFIG_SMP */

static void timer_update_keys(struct work_struct *work)
{
	mutex_lock(&timer_keys_mutex);
	timers_update_migration();
	static_branch_enable(&timers_nohz_active);
	mutex_unlock(&timer_keys_mutex);
}

void timers_update_nohz(void)
{
	schedule_work(&timer_update_work);
}

static inline bool is_timers_nohz_active(void)
{
	return static_branch_unlikely(&timers_nohz_active);
}
#else
static inline bool is_timers_nohz_active(void) { return false; }
#endif /* NO_HZ_COMMON */

static unsigned long round_jiffies_common(unsigned long j, int cpu,
		bool force_up)
{
	int rem;
	unsigned long original = j;

	/*
	 * We don't want all cpus firing their timers at once hitting the
	 * same lock or cachelines, so we skew each extra cpu with an extra
	 * 3 jiffies. This 3 jiffies came originally from the mm/ code which
	 * already did this.
	 * The skew is done by adding 3*cpunr, then round, then subtract this
	 * extra offset again.
	 */
	j += cpu * 3;

	rem = j % HZ;

	/*
	 * If the target jiffy is just after a whole second (which can happen
	 * due to delays of the timer irq, long irq off times etc etc) then
	 * we should round down to the whole second, not up. Use 1/4th second
	 * as cutoff for this rounding as an extreme upper bound for this.
	 * But never round down if @force_up is set.
	 */
	if (rem < HZ/4 && !force_up) /* round down */
		j = j - rem;
	else /* round up */
		j = j - rem + HZ;

	/* now that we have rounded, subtract the extra skew again */
	j -= cpu * 3;

	/*
	 * Make sure j is still in the future. Otherwise return the
	 * unmodified value.
	 */
	return time_is_after_jiffies(j) ? j : original;
}

/**
 * __round_jiffies - function to round jiffies to a full second
 * @j: the time in (absolute) jiffies that should be rounded
 * @cpu: the processor number on which the timeout will happen
 *
 * __round_jiffies() rounds an absolute time in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * The exact rounding is skewed for each processor to avoid all
 * processors firing at the exact same time, which could lead
 * to lock contention or spurious cache line bouncing.
 *
 * The return value is the rounded version of the @j parameter.
 */
unsigned long __round_jiffies(unsigned long j, int cpu)
{
	return round_jiffies_common(j, cpu, false);
}
EXPORT_SYMBOL_GPL(__round_jiffies);

/**
 * __round_jiffies_relative - function to round jiffies to a full second
 * @j: the time in (relative) jiffies that should be rounded
 * @cpu: the processor number on which the timeout will happen
 *
 * __round_jiffies_relative() rounds a time delta  in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * The exact rounding is skewed for each processor to avoid all
 * processors firing at the exact same time, which could lead
 * to lock contention or spurious cache line bouncing.
 *
 * The return value is the rounded version of the @j parameter.
 */
unsigned long __round_jiffies_relative(unsigned long j, int cpu)
{
	unsigned long j0 = jiffies;

	/* Use j0 because jiffies might change while we run */
	return round_jiffies_common(j + j0, cpu, false) - j0;
}
EXPORT_SYMBOL_GPL(__round_jiffies_relative);

/**
 * round_jiffies - function to round jiffies to a full second
 * @j: the time in (absolute) jiffies that should be rounded
 *
 * round_jiffies() rounds an absolute time in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * The return value is the rounded version of the @j parameter.
 */
unsigned long round_jiffies(unsigned long j)
{
	return round_jiffies_common(j, raw_smp_processor_id(), false);
}
EXPORT_SYMBOL_GPL(round_jiffies);

/**
 * round_jiffies_relative - function to round jiffies to a full second
 * @j: the time in (relative) jiffies that should be rounded
 *
 * round_jiffies_relative() rounds a time delta  in the future (in jiffies)
 * up or down to (approximately) full seconds. This is useful for timers
 * for which the exact time they fire does not matter too much, as long as
 * they fire approximately every X seconds.
 *
 * By rounding these timers to whole seconds, all such timers will fire
 * at the same time, rather than at various times spread out. The goal
 * of this is to have the CPU wake up less, which saves power.
 *
 * The return value is the rounded version of the @j parameter.
 */
unsigned long round_jiffies_relative(unsigned long j)
{
	return __round_jiffies_relative(j, raw_smp_processor_id());
}
EXPORT_SYMBOL_GPL(round_jiffies_relative);

/**
 * __round_jiffies_up - function to round jiffies up to a full second
 * @j: the time in (absolute) jiffies that should be rounded
 * @cpu: the processor number on which the timeout will happen
 *
 * This is the same as __round_jiffies() except that it will never
 * round down.  This is useful for timeouts for which the exact time
 * of firing does not matter too much, as long as they don't fire too
 * early.
 */
unsigned long __round_jiffies_up(unsigned long j, int cpu)
{
	return round_jiffies_common(j, cpu, true);
}
EXPORT_SYMBOL_GPL(__round_jiffies_up);

/**
 * __round_jiffies_up_relative - function to round jiffies up to a full second
 * @j: the time in (relative) jiffies that should be rounded
 * @cpu: the processor number on which the timeout will happen
 *
 * This is the same as __round_jiffies_relative() except that it will never
 * round down.  This is useful for timeouts for which the exact time
 * of firing does not matter too much, as long as they don't fire too
 * early.
 */
unsigned long __round_jiffies_up_relative(unsigned long j, int cpu)
{
	unsigned long j0 = jiffies;

	/* Use j0 because jiffies might change while we run */
	return round_jiffies_common(j + j0, cpu, true) - j0;
}
EXPORT_SYMBOL_GPL(__round_jiffies_up_relative);

/**
 * round_jiffies_up - function to round jiffies up to a full second
 * @j: the time in (absolute) jiffies that should be rounded
 *
 * This is the same as round_jiffies() except that it will never
 * round down.  This is useful for timeouts for which the exact time
 * of firing does not matter too much, as long as they don't fire too
 * early.
 */
unsigned long round_jiffies_up(unsigned long j)
{
	return round_jiffies_common(j, raw_smp_processor_id(), true);
}
EXPORT_SYMBOL_GPL(round_jiffies_up);

/**
 * round_jiffies_up_relative - function to round jiffies up to a full second
 * @j: the time in (relative) jiffies that should be rounded
 *
 * This is the same as round_jiffies_relative() except that it will never
 * round down.  This is useful for timeouts for which the exact time
 * of firing does not matter too much, as long as they don't fire too
 * early.
 */
unsigned long round_jiffies_up_relative(unsigned long j)
{
	return __round_jiffies_up_relative(j, raw_smp_processor_id());
}
EXPORT_SYMBOL_GPL(round_jiffies_up_relative);


static inline unsigned int timer_get_idx(struct timer_list *timer)
{
	return (timer->flags & TIMER_ARRAYMASK) >> TIMER_ARRAYSHIFT;
}

static inline void timer_set_idx(struct timer_list *timer, unsigned int idx)
{
	timer->flags = (timer->flags & ~TIMER_ARRAYMASK) |
			idx << TIMER_ARRAYSHIFT;
}

/*
 * Helper function to calculate the array index for a given expiry
 * time.
 */
static inline unsigned calc_index(unsigned long expires, unsigned lvl,
				  unsigned long *bucket_expiry)
{

	/*
	 * The timer wheel has to guarantee that a timer does not fire
	 * early. Early expiry can happen due to:
	 * - Timer is armed at the edge of a tick
	 * - Truncation of the expiry time in the outer wheel levels
	 *
	 * Round up with level granularity to prevent this.
	 */
	expires = (expires >> LVL_SHIFT(lvl)) + 1;
	*bucket_expiry = expires << LVL_SHIFT(lvl);
	return LVL_OFFS(lvl) + (expires & LVL_MASK);
}

static int calc_wheel_index(unsigned long expires, unsigned long clk,
			    unsigned long *bucket_expiry)
{
	unsigned long delta = expires - clk;
	unsigned int idx;

	if (delta < LVL_START(1)) {
		idx = calc_index(expires, 0, bucket_expiry);
	} else if (delta < LVL_START(2)) {
		idx = calc_index(expires, 1, bucket_expiry);
	} else if (delta < LVL_START(3)) {
		idx = calc_index(expires, 2, bucket_expiry);
	} else if (delta < LVL_START(4)) {
		idx = calc_index(expires, 3, bucket_expiry);
	} else if (delta < LVL_START(5)) {
		idx = calc_index(expires, 4, bucket_expiry);
	} else if (delta < LVL_START(6)) {
		idx = calc_index(expires, 5, bucket_expiry);
	} else if (delta < LVL_START(7)) {
		idx = calc_index(expires, 6, bucket_expiry);
	} else if (LVL_DEPTH > 8 && delta < LVL_START(8)) {
		idx = calc_index(expires, 7, bucket_expiry);
	} else if ((long) delta < 0) {
		idx = clk & LVL_MASK;
		*bucket_expiry = clk;
	} else {
		/*
		 * Force expire obscene large timeouts to expire at the
		 * capacity limit of the wheel.
		 */
		if (delta >= WHEEL_TIMEOUT_CUTOFF)
			expires = clk + WHEEL_TIMEOUT_MAX;

		idx = calc_index(expires, LVL_DEPTH - 1, bucket_expiry);
	}
	return idx;
}

static void
trigger_dyntick_cpu(struct timer_base *base, struct timer_list *timer)
{
	/*
	 * Deferrable timers do not prevent the CPU from entering dynticks and
	 * are not taken into account on the idle/nohz_full path. An IPI when a
	 * new deferrable timer is enqueued will wake up the remote CPU but
	 * nothing will be done with the deferrable timer base. Therefore skip
	 * the remote IPI for deferrable timers completely.
	 */
	if (!is_timers_nohz_active() || timer->flags & TIMER_DEFERRABLE)
		return;

	/*
	 * We might have to IPI the remote CPU if the base is idle and the
	 * timer is pinned. If it is a non pinned timer, it is only queued
	 * on the remote CPU, when timer was running during queueing. Then
	 * everything is handled by remote CPU anyway. If the other CPU is
	 * on the way to idle then it can't set base->is_idle as we hold
	 * the base lock:
	 */
	if (base->is_idle) {
		WARN_ON_ONCE(!(timer->flags & TIMER_PINNED ||
			       tick_nohz_full_cpu(base->cpu)));
		wake_up_nohz_cpu(base->cpu);
	}
}

/*
 * Enqueue the timer into the hash bucket, mark it pending in
 * the bitmap, store the index in the timer flags then wake up
 * the target CPU if needed.
 */
static void enqueue_timer(struct timer_base *base, struct timer_list *timer,
			  unsigned int idx, unsigned long bucket_expiry)
{

	hlist_add_head(&timer->entry, base->vectors + idx);
	__set_bit(idx, base->pending_map);
	timer_set_idx(timer, idx);

	trace_timer_start(timer, bucket_expiry);

	/*
	 * Check whether this is the new first expiring timer. The
	 * effective expiry time of the timer is required here
	 * (bucket_expiry) instead of timer->expires.
	 */
	if (time_before(bucket_expiry, base->next_expiry)) {
		/*
		 * Set the next expiry time and kick the CPU so it
		 * can reevaluate the wheel:
		 */
		WRITE_ONCE(base->next_expiry, bucket_expiry);
		base->timers_pending = true;
		base->next_expiry_recalc = false;
		trigger_dyntick_cpu(base, timer);
	}
}

static void internal_add_timer(struct timer_base *base, struct timer_list *timer)
{
	unsigned long bucket_expiry;
	unsigned int idx;

	idx = calc_wheel_index(timer->expires, base->clk, &bucket_expiry);
	enqueue_timer(base, timer, idx, bucket_expiry);
}

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS

static const struct debug_obj_descr timer_debug_descr;

struct timer_hint {
	void	(*function)(struct timer_list *t);
	long	offset;
};

#define TIMER_HINT(fn, container, timr, hintfn)			\
	{							\
		.function = fn,					\
		.offset	  = offsetof(container, hintfn) -	\
			    offsetof(container, timr)		\
	}

static const struct timer_hint timer_hints[] = {
	TIMER_HINT(delayed_work_timer_fn,
		   struct delayed_work, timer, work.func),
	TIMER_HINT(kthread_delayed_work_timer_fn,
		   struct kthread_delayed_work, timer, work.func),
};

static void *timer_debug_hint(void *addr)
{
	struct timer_list *timer = addr;
	int i;

	for (i = 0; i < ARRAY_SIZE(timer_hints); i++) {
		if (timer_hints[i].function == timer->function) {
			void (**fn)(void) = addr + timer_hints[i].offset;

			return *fn;
		}
	}

	return timer->function;
}

static bool timer_is_static_object(void *addr)
{
	struct timer_list *timer = addr;

	return (timer->entry.pprev == NULL &&
		timer->entry.next == TIMER_ENTRY_STATIC);
}

/*
 * timer_fixup_init is called when:
 * - an active object is initialized
 */
static bool timer_fixup_init(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		del_timer_sync(timer);
		debug_object_init(timer, &timer_debug_descr);
		return true;
	default:
		return false;
	}
}

/* Stub timer callback for improperly used timers. */
static void stub_timer(struct timer_list *unused)
{
	WARN_ON(1);
}

/*
 * timer_fixup_activate is called when:
 * - an active object is activated
 * - an unknown non-static object is activated
 */
static bool timer_fixup_activate(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_NOTAVAILABLE:
		timer_setup(timer, stub_timer, 0);
		return true;

	case ODEBUG_STATE_ACTIVE:
		WARN_ON(1);
		fallthrough;
	default:
		return false;
	}
}

/*
 * timer_fixup_free is called when:
 * - an active object is freed
 */
static bool timer_fixup_free(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		del_timer_sync(timer);
		debug_object_free(timer, &timer_debug_descr);
		return true;
	default:
		return false;
	}
}

/*
 * timer_fixup_assert_init is called when:
 * - an untracked/uninit-ed object is found
 */
static bool timer_fixup_assert_init(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_NOTAVAILABLE:
		timer_setup(timer, stub_timer, 0);
		return true;
	default:
		return false;
	}
}

static const struct debug_obj_descr timer_debug_descr = {
	.name			= "timer_list",
	.debug_hint		= timer_debug_hint,
	.is_static_object	= timer_is_static_object,
	.fixup_init		= timer_fixup_init,
	.fixup_activate		= timer_fixup_activate,
	.fixup_free		= timer_fixup_free,
	.fixup_assert_init	= timer_fixup_assert_init,
};

static inline void debug_timer_init(struct timer_list *timer)
{
	debug_object_init(timer, &timer_debug_descr);
}

static inline void debug_timer_activate(struct timer_list *timer)
{
	debug_object_activate(timer, &timer_debug_descr);
}

static inline void debug_timer_deactivate(struct timer_list *timer)
{
	debug_object_deactivate(timer, &timer_debug_descr);
}

static inline void debug_timer_assert_init(struct timer_list *timer)
{
	debug_object_assert_init(timer, &timer_debug_descr);
}

static void do_init_timer(struct timer_list *timer,
			  void (*func)(struct timer_list *),
			  unsigned int flags,
			  const char *name, struct lock_class_key *key);

void init_timer_on_stack_key(struct timer_list *timer,
			     void (*func)(struct timer_list *),
			     unsigned int flags,
			     const char *name, struct lock_class_key *key)
{
	debug_object_init_on_stack(timer, &timer_debug_descr);
	do_init_timer(timer, func, flags, name, key);
}
EXPORT_SYMBOL_GPL(init_timer_on_stack_key);

void destroy_timer_on_stack(struct timer_list *timer)
{
	debug_object_free(timer, &timer_debug_descr);
}
EXPORT_SYMBOL_GPL(destroy_timer_on_stack);

#else
static inline void debug_timer_init(struct timer_list *timer) { }
static inline void debug_timer_activate(struct timer_list *timer) { }
static inline void debug_timer_deactivate(struct timer_list *timer) { }
static inline void debug_timer_assert_init(struct timer_list *timer) { }
#endif

static inline void debug_init(struct timer_list *timer)
{
	debug_timer_init(timer);
	trace_timer_init(timer);
}

static inline void debug_deactivate(struct timer_list *timer)
{
	debug_timer_deactivate(timer);
	trace_timer_cancel(timer);
}

static inline void debug_assert_init(struct timer_list *timer)
{
	debug_timer_assert_init(timer);
}

static void do_init_timer(struct timer_list *timer,
			  void (*func)(struct timer_list *),
			  unsigned int flags,
			  const char *name, struct lock_class_key *key)
{
	timer->entry.pprev = NULL;
	timer->function = func;
	if (WARN_ON_ONCE(flags & ~TIMER_INIT_FLAGS))
		flags &= TIMER_INIT_FLAGS;
	timer->flags = flags | raw_smp_processor_id();
	lockdep_init_map(&timer->lockdep_map, name, key, 0);
}

/**
 * init_timer_key - initialize a timer
 * @timer: the timer to be initialized
 * @func: timer callback function
 * @flags: timer flags
 * @name: name of the timer
 * @key: lockdep class key of the fake lock used for tracking timer
 *       sync lock dependencies
 *
 * init_timer_key() must be done to a timer prior to calling *any* of the
 * other timer functions.
 */
void init_timer_key(struct timer_list *timer,
		    void (*func)(struct timer_list *), unsigned int flags,
		    const char *name, struct lock_class_key *key)
{
	debug_init(timer);
	do_init_timer(timer, func, flags, name, key);
}
EXPORT_SYMBOL(init_timer_key);

static inline void detach_timer(struct timer_list *timer, bool clear_pending)
{
	struct hlist_node *entry = &timer->entry;

	debug_deactivate(timer);

	__hlist_del(entry);
	if (clear_pending)
		entry->pprev = NULL;
	entry->next = LIST_POISON2;
}

static int detach_if_pending(struct timer_list *timer, struct timer_base *base,
			     bool clear_pending)
{
	unsigned idx = timer_get_idx(timer);

	if (!timer_pending(timer))
		return 0;

	if (hlist_is_singular_node(&timer->entry, base->vectors + idx)) {
		__clear_bit(idx, base->pending_map);
		base->next_expiry_recalc = true;
	}

	detach_timer(timer, clear_pending);
	return 1;
}

static inline struct timer_base *get_timer_cpu_base(u32 tflags, u32 cpu)
{
	int index = tflags & TIMER_PINNED ? BASE_LOCAL : BASE_GLOBAL;
	struct timer_base *base;

	base = per_cpu_ptr(&timer_bases[index], cpu);

	/*
	 * If the timer is deferrable and NO_HZ_COMMON is set then we need
	 * to use the deferrable base.
	 */
	if (IS_ENABLED(CONFIG_NO_HZ_COMMON) && (tflags & TIMER_DEFERRABLE))
		base = per_cpu_ptr(&timer_bases[BASE_DEF], cpu);
	return base;
}

static inline struct timer_base *get_timer_this_cpu_base(u32 tflags)
{
	int index = tflags & TIMER_PINNED ? BASE_LOCAL : BASE_GLOBAL;
	struct timer_base *base;

	base = this_cpu_ptr(&timer_bases[index]);

	/*
	 * If the timer is deferrable and NO_HZ_COMMON is set then we need
	 * to use the deferrable base.
	 */
	if (IS_ENABLED(CONFIG_NO_HZ_COMMON) && (tflags & TIMER_DEFERRABLE))
		base = this_cpu_ptr(&timer_bases[BASE_DEF]);
	return base;
}

static inline struct timer_base *get_timer_base(u32 tflags)
{
	return get_timer_cpu_base(tflags, tflags & TIMER_CPUMASK);
}

static inline void __forward_timer_base(struct timer_base *base,
					unsigned long basej)
{
	/*
	 * Check whether we can forward the base. We can only do that when
	 * @basej is past base->clk otherwise we might rewind base->clk.
	 */
	if (time_before_eq(basej, base->clk))
		return;

	/*
	 * If the next expiry value is > jiffies, then we fast forward to
	 * jiffies otherwise we forward to the next expiry value.
	 */
	if (time_after(base->next_expiry, basej)) {
		base->clk = basej;
	} else {
		if (WARN_ON_ONCE(time_before(base->next_expiry, base->clk)))
			return;
		base->clk = base->next_expiry;
	}

}

static inline void forward_timer_base(struct timer_base *base)
{
	__forward_timer_base(base, READ_ONCE(jiffies));
}

/*
 * We are using hashed locking: Holding per_cpu(timer_bases[x]).lock means
 * that all timers which are tied to this base are locked, and the base itself
 * is locked too.
 *
 * So __run_timers/migrate_timers can safely modify all timers which could
 * be found in the base->vectors array.
 *
 * When a timer is migrating then the TIMER_MIGRATING flag is set and we need
 * to wait until the migration is done.
 */
static struct timer_base *lock_timer_base(struct timer_list *timer,
					  unsigned long *flags)
	__acquires(timer->base->lock)
{
	for (;;) {
		struct timer_base *base;
		u32 tf;

		/*
		 * We need to use READ_ONCE() here, otherwise the compiler
		 * might re-read @tf between the check for TIMER_MIGRATING
		 * and spin_lock().
		 */
		tf = READ_ONCE(timer->flags);

		if (!(tf & TIMER_MIGRATING)) {
			base = get_timer_base(tf);
			raw_spin_lock_irqsave(&base->lock, *flags);
			if (timer->flags == tf)
				return base;
			raw_spin_unlock_irqrestore(&base->lock, *flags);
		}
		cpu_relax();
	}
}

#define MOD_TIMER_PENDING_ONLY		0x01
#define MOD_TIMER_REDUCE		0x02
#define MOD_TIMER_NOTPENDING		0x04

static inline int
__mod_timer(struct timer_list *timer, unsigned long expires, unsigned int options)
{
	unsigned long clk = 0, flags, bucket_expiry;
	struct timer_base *base, *new_base;
	unsigned int idx = UINT_MAX;
	int ret = 0;

	debug_assert_init(timer);

	/*
	 * This is a common optimization triggered by the networking code - if
	 * the timer is re-modified to have the same timeout or ends up in the
	 * same array bucket then just return:
	 */
	if (!(options & MOD_TIMER_NOTPENDING) && timer_pending(timer)) {
		/*
		 * The downside of this optimization is that it can result in
		 * larger granularity than you would get from adding a new
		 * timer with this expiry.
		 */
		long diff = timer->expires - expires;

		if (!diff)
			return 1;
		if (options & MOD_TIMER_REDUCE && diff <= 0)
			return 1;

		/*
		 * We lock timer base and calculate the bucket index right
		 * here. If the timer ends up in the same bucket, then we
		 * just update the expiry time and avoid the whole
		 * dequeue/enqueue dance.
		 */
		base = lock_timer_base(timer, &flags);
		/*
		 * Has @timer been shutdown? This needs to be evaluated
		 * while holding base lock to prevent a race against the
		 * shutdown code.
		 */
		if (!timer->function)
			goto out_unlock;

		forward_timer_base(base);

		if (timer_pending(timer) && (options & MOD_TIMER_REDUCE) &&
		    time_before_eq(timer->expires, expires)) {
			ret = 1;
			goto out_unlock;
		}

		clk = base->clk;
		idx = calc_wheel_index(expires, clk, &bucket_expiry);

		/*
		 * Retrieve and compare the array index of the pending
		 * timer. If it matches set the expiry to the new value so a
		 * subsequent call will exit in the expires check above.
		 */
		if (idx == timer_get_idx(timer)) {
			if (!(options & MOD_TIMER_REDUCE))
				timer->expires = expires;
			else if (time_after(timer->expires, expires))
				timer->expires = expires;
			ret = 1;
			goto out_unlock;
		}
	} else {
		base = lock_timer_base(timer, &flags);
		/*
		 * Has @timer been shutdown? This needs to be evaluated
		 * while holding base lock to prevent a race against the
		 * shutdown code.
		 */
		if (!timer->function)
			goto out_unlock;

		forward_timer_base(base);
	}

	ret = detach_if_pending(timer, base, false);
	if (!ret && (options & MOD_TIMER_PENDING_ONLY))
		goto out_unlock;

	new_base = get_timer_this_cpu_base(timer->flags);

	if (base != new_base) {
		/*
		 * We are trying to schedule the timer on the new base.
		 * However we can't change timer's base while it is running,
		 * otherwise timer_delete_sync() can't detect that the timer's
		 * handler yet has not finished. This also guarantees that the
		 * timer is serialized wrt itself.
		 */
		if (likely(base->running_timer != timer)) {
			/* See the comment in lock_timer_base() */
			timer->flags |= TIMER_MIGRATING;

			raw_spin_unlock(&base->lock);
			base = new_base;
			raw_spin_lock(&base->lock);
			WRITE_ONCE(timer->flags,
				   (timer->flags & ~TIMER_BASEMASK) | base->cpu);
			forward_timer_base(base);
		}
	}

	debug_timer_activate(timer);

	timer->expires = expires;
	/*
	 * If 'idx' was calculated above and the base time did not advance
	 * between calculating 'idx' and possibly switching the base, only
	 * enqueue_timer() is required. Otherwise we need to (re)calculate
	 * the wheel index via internal_add_timer().
	 */
	if (idx != UINT_MAX && clk == base->clk)
		enqueue_timer(base, timer, idx, bucket_expiry);
	else
		internal_add_timer(base, timer);

out_unlock:
	raw_spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

/**
 * mod_timer_pending - Modify a pending timer's timeout
 * @timer:	The pending timer to be modified
 * @expires:	New absolute timeout in jiffies
 *
 * mod_timer_pending() is the same for pending timers as mod_timer(), but
 * will not activate inactive timers.
 *
 * If @timer->function == NULL then the start operation is silently
 * discarded.
 *
 * Return:
 * * %0 - The timer was inactive and not modified or was in
 *	  shutdown state and the operation was discarded
 * * %1 - The timer was active and requeued to expire at @expires
 */
int mod_timer_pending(struct timer_list *timer, unsigned long expires)
{
	return __mod_timer(timer, expires, MOD_TIMER_PENDING_ONLY);
}
EXPORT_SYMBOL(mod_timer_pending);

/**
 * mod_timer - Modify a timer's timeout
 * @timer:	The timer to be modified
 * @expires:	New absolute timeout in jiffies
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * mod_timer() is more efficient than the above open coded sequence. In
 * case that the timer is inactive, the del_timer() part is a NOP. The
 * timer is in any case activated with the new expiry time @expires.
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * If @timer->function == NULL then the start operation is silently
 * discarded. In this case the return value is 0 and meaningless.
 *
 * Return:
 * * %0 - The timer was inactive and started or was in shutdown
 *	  state and the operation was discarded
 * * %1 - The timer was active and requeued to expire at @expires or
 *	  the timer was active and not modified because @expires did
 *	  not change the effective expiry time
 */
int mod_timer(struct timer_list *timer, unsigned long expires)
{
	return __mod_timer(timer, expires, 0);
}
EXPORT_SYMBOL(mod_timer);

/**
 * timer_reduce - Modify a timer's timeout if it would reduce the timeout
 * @timer:	The timer to be modified
 * @expires:	New absolute timeout in jiffies
 *
 * timer_reduce() is very similar to mod_timer(), except that it will only
 * modify an enqueued timer if that would reduce the expiration time. If
 * @timer is not enqueued it starts the timer.
 *
 * If @timer->function == NULL then the start operation is silently
 * discarded.
 *
 * Return:
 * * %0 - The timer was inactive and started or was in shutdown
 *	  state and the operation was discarded
 * * %1 - The timer was active and requeued to expire at @expires or
 *	  the timer was active and not modified because @expires
 *	  did not change the effective expiry time such that the
 *	  timer would expire earlier than already scheduled
 */
int timer_reduce(struct timer_list *timer, unsigned long expires)
{
	return __mod_timer(timer, expires, MOD_TIMER_REDUCE);
}
EXPORT_SYMBOL(timer_reduce);

/**
 * add_timer - Start a timer
 * @timer:	The timer to be started
 *
 * Start @timer to expire at @timer->expires in the future. @timer->expires
 * is the absolute expiry time measured in 'jiffies'. When the timer expires
 * timer->function(timer) will be invoked from soft interrupt context.
 *
 * The @timer->expires and @timer->function fields must be set prior
 * to calling this function.
 *
 * If @timer->function == NULL then the start operation is silently
 * discarded.
 *
 * If @timer->expires is already in the past @timer will be queued to
 * expire at the next timer tick.
 *
 * This can only operate on an inactive timer. Attempts to invoke this on
 * an active timer are rejected with a warning.
 */
void add_timer(struct timer_list *timer)
{
	if (WARN_ON_ONCE(timer_pending(timer)))
		return;
	__mod_timer(timer, timer->expires, MOD_TIMER_NOTPENDING);
}
EXPORT_SYMBOL(add_timer);

/**
 * add_timer_local() - Start a timer on the local CPU
 * @timer:	The timer to be started
 *
 * Same as add_timer() except that the timer flag TIMER_PINNED is set.
 *
 * See add_timer() for further details.
 */
void add_timer_local(struct timer_list *timer)
{
	if (WARN_ON_ONCE(timer_pending(timer)))
		return;
	timer->flags |= TIMER_PINNED;
	__mod_timer(timer, timer->expires, MOD_TIMER_NOTPENDING);
}
EXPORT_SYMBOL(add_timer_local);

/**
 * add_timer_global() - Start a timer without TIMER_PINNED flag set
 * @timer:	The timer to be started
 *
 * Same as add_timer() except that the timer flag TIMER_PINNED is unset.
 *
 * See add_timer() for further details.
 */
void add_timer_global(struct timer_list *timer)
{
	if (WARN_ON_ONCE(timer_pending(timer)))
		return;
	timer->flags &= ~TIMER_PINNED;
	__mod_timer(timer, timer->expires, MOD_TIMER_NOTPENDING);
}
EXPORT_SYMBOL(add_timer_global);

/**
 * add_timer_on - Start a timer on a particular CPU
 * @timer:	The timer to be started
 * @cpu:	The CPU to start it on
 *
 * Same as add_timer() except that it starts the timer on the given CPU and
 * the TIMER_PINNED flag is set. When timer shouldn't be a pinned timer in
 * the next round, add_timer_global() should be used instead as it unsets
 * the TIMER_PINNED flag.
 *
 * See add_timer() for further details.
 */
void add_timer_on(struct timer_list *timer, int cpu)
{
	struct timer_base *new_base, *base;
	unsigned long flags;

	debug_assert_init(timer);

	if (WARN_ON_ONCE(timer_pending(timer)))
		return;

	/* Make sure timer flags have TIMER_PINNED flag set */
	timer->flags |= TIMER_PINNED;

	new_base = get_timer_cpu_base(timer->flags, cpu);

	/*
	 * If @timer was on a different CPU, it should be migrated with the
	 * old base locked to prevent other operations proceeding with the
	 * wrong base locked.  See lock_timer_base().
	 */
	base = lock_timer_base(timer, &flags);
	/*
	 * Has @timer been shutdown? This needs to be evaluated while
	 * holding base lock to prevent a race against the shutdown code.
	 */
	if (!timer->function)
		goto out_unlock;

	if (base != new_base) {
		timer->flags |= TIMER_MIGRATING;

		raw_spin_unlock(&base->lock);
		base = new_base;
		raw_spin_lock(&base->lock);
		WRITE_ONCE(timer->flags,
			   (timer->flags & ~TIMER_BASEMASK) | cpu);
	}
	forward_timer_base(base);

	debug_timer_activate(timer);
	internal_add_timer(base, timer);
out_unlock:
	raw_spin_unlock_irqrestore(&base->lock, flags);
}
EXPORT_SYMBOL_GPL(add_timer_on);

/**
 * __timer_delete - Internal function: Deactivate a timer
 * @timer:	The timer to be deactivated
 * @shutdown:	If true, this indicates that the timer is about to be
 *		shutdown permanently.
 *
 * If @shutdown is true then @timer->function is set to NULL under the
 * timer base lock which prevents further rearming of the time. In that
 * case any attempt to rearm @timer after this function returns will be
 * silently ignored.
 *
 * Return:
 * * %0 - The timer was not pending
 * * %1 - The timer was pending and deactivated
 */
static int __timer_delete(struct timer_list *timer, bool shutdown)
{
	struct timer_base *base;
	unsigned long flags;
	int ret = 0;

	debug_assert_init(timer);

	/*
	 * If @shutdown is set then the lock has to be taken whether the
	 * timer is pending or not to protect against a concurrent rearm
	 * which might hit between the lockless pending check and the lock
	 * acquisition. By taking the lock it is ensured that such a newly
	 * enqueued timer is dequeued and cannot end up with
	 * timer->function == NULL in the expiry code.
	 *
	 * If timer->function is currently executed, then this makes sure
	 * that the callback cannot requeue the timer.
	 */
	if (timer_pending(timer) || shutdown) {
		base = lock_timer_base(timer, &flags);
		ret = detach_if_pending(timer, base, true);
		if (shutdown)
			timer->function = NULL;
		raw_spin_unlock_irqrestore(&base->lock, flags);
	}

	return ret;
}

/**
 * timer_delete - Deactivate a timer
 * @timer:	The timer to be deactivated
 *
 * The function only deactivates a pending timer, but contrary to
 * timer_delete_sync() it does not take into account whether the timer's
 * callback function is concurrently executed on a different CPU or not.
 * It neither prevents rearming of the timer.  If @timer can be rearmed
 * concurrently then the return value of this function is meaningless.
 *
 * Return:
 * * %0 - The timer was not pending
 * * %1 - The timer was pending and deactivated
 */
int timer_delete(struct timer_list *timer)
{
	return __timer_delete(timer, false);
}
EXPORT_SYMBOL(timer_delete);

/**
 * timer_shutdown - Deactivate a timer and prevent rearming
 * @timer:	The timer to be deactivated
 *
 * The function does not wait for an eventually running timer callback on a
 * different CPU but it prevents rearming of the timer. Any attempt to arm
 * @timer after this function returns will be silently ignored.
 *
 * This function is useful for teardown code and should only be used when
 * timer_shutdown_sync() cannot be invoked due to locking or context constraints.
 *
 * Return:
 * * %0 - The timer was not pending
 * * %1 - The timer was pending
 */
int timer_shutdown(struct timer_list *timer)
{
	return __timer_delete(timer, true);
}
EXPORT_SYMBOL_GPL(timer_shutdown);

/**
 * __try_to_del_timer_sync - Internal function: Try to deactivate a timer
 * @timer:	Timer to deactivate
 * @shutdown:	If true, this indicates that the timer is about to be
 *		shutdown permanently.
 *
 * If @shutdown is true then @timer->function is set to NULL under the
 * timer base lock which prevents further rearming of the timer. Any
 * attempt to rearm @timer after this function returns will be silently
 * ignored.
 *
 * This function cannot guarantee that the timer cannot be rearmed
 * right after dropping the base lock if @shutdown is false. That
 * needs to be prevented by the calling code if necessary.
 *
 * Return:
 * * %0  - The timer was not pending
 * * %1  - The timer was pending and deactivated
 * * %-1 - The timer callback function is running on a different CPU
 */
static int __try_to_del_timer_sync(struct timer_list *timer, bool shutdown)
{
	struct timer_base *base;
	unsigned long flags;
	int ret = -1;

	debug_assert_init(timer);

	base = lock_timer_base(timer, &flags);

	if (base->running_timer != timer)
		ret = detach_if_pending(timer, base, true);
	if (shutdown)
		timer->function = NULL;

	raw_spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

/**
 * try_to_del_timer_sync - Try to deactivate a timer
 * @timer:	Timer to deactivate
 *
 * This function tries to deactivate a timer. On success the timer is not
 * queued and the timer callback function is not running on any CPU.
 *
 * This function does not guarantee that the timer cannot be rearmed right
 * after dropping the base lock. That needs to be prevented by the calling
 * code if necessary.
 *
 * Return:
 * * %0  - The timer was not pending
 * * %1  - The timer was pending and deactivated
 * * %-1 - The timer callback function is running on a different CPU
 */
int try_to_del_timer_sync(struct timer_list *timer)
{
	return __try_to_del_timer_sync(timer, false);
}
EXPORT_SYMBOL(try_to_del_timer_sync);

#ifdef CONFIG_PREEMPT_RT
static __init void timer_base_init_expiry_lock(struct timer_base *base)
{
	spin_lock_init(&base->expiry_lock);
}

static inline void timer_base_lock_expiry(struct timer_base *base)
{
	spin_lock(&base->expiry_lock);
}

static inline void timer_base_unlock_expiry(struct timer_base *base)
{
	spin_unlock(&base->expiry_lock);
}

/*
 * The counterpart to del_timer_wait_running().
 *
 * If there is a waiter for base->expiry_lock, then it was waiting for the
 * timer callback to finish. Drop expiry_lock and reacquire it. That allows
 * the waiter to acquire the lock and make progress.
 */
static void timer_sync_wait_running(struct timer_base *base)
	__releases(&base->lock) __releases(&base->expiry_lock)
	__acquires(&base->expiry_lock) __acquires(&base->lock)
{
	if (atomic_read(&base->timer_waiters)) {
		raw_spin_unlock_irq(&base->lock);
		spin_unlock(&base->expiry_lock);
		spin_lock(&base->expiry_lock);
		raw_spin_lock_irq(&base->lock);
	}
}

/*
 * This function is called on PREEMPT_RT kernels when the fast path
 * deletion of a timer failed because the timer callback function was
 * running.
 *
 * This prevents priority inversion, if the softirq thread on a remote CPU
 * got preempted, and it prevents a life lock when the task which tries to
 * delete a timer preempted the softirq thread running the timer callback
 * function.
 */
static void del_timer_wait_running(struct timer_list *timer)
{
	u32 tf;

	tf = READ_ONCE(timer->flags);
	if (!(tf & (TIMER_MIGRATING | TIMER_IRQSAFE))) {
		struct timer_base *base = get_timer_base(tf);

		/*
		 * Mark the base as contended and grab the expiry lock,
		 * which is held by the softirq across the timer
		 * callback. Drop the lock immediately so the softirq can
		 * expire the next timer. In theory the timer could already
		 * be running again, but that's more than unlikely and just
		 * causes another wait loop.
		 */
		atomic_inc(&base->timer_waiters);
		spin_lock_bh(&base->expiry_lock);
		atomic_dec(&base->timer_waiters);
		spin_unlock_bh(&base->expiry_lock);
	}
}
#else
static inline void timer_base_init_expiry_lock(struct timer_base *base) { }
static inline void timer_base_lock_expiry(struct timer_base *base) { }
static inline void timer_base_unlock_expiry(struct timer_base *base) { }
static inline void timer_sync_wait_running(struct timer_base *base) { }
static inline void del_timer_wait_running(struct timer_list *timer) { }
#endif

/**
 * __timer_delete_sync - Internal function: Deactivate a timer and wait
 *			 for the handler to finish.
 * @timer:	The timer to be deactivated
 * @shutdown:	If true, @timer->function will be set to NULL under the
 *		timer base lock which prevents rearming of @timer
 *
 * If @shutdown is not set the timer can be rearmed later. If the timer can
 * be rearmed concurrently, i.e. after dropping the base lock then the
 * return value is meaningless.
 *
 * If @shutdown is set then @timer->function is set to NULL under timer
 * base lock which prevents rearming of the timer. Any attempt to rearm
 * a shutdown timer is silently ignored.
 *
 * If the timer should be reused after shutdown it has to be initialized
 * again.
 *
 * Return:
 * * %0	- The timer was not pending
 * * %1	- The timer was pending and deactivated
 */
static int __timer_delete_sync(struct timer_list *timer, bool shutdown)
{
	int ret;

#ifdef CONFIG_LOCKDEP
	unsigned long flags;

	/*
	 * If lockdep gives a backtrace here, please reference
	 * the synchronization rules above.
	 */
	local_irq_save(flags);
	lock_map_acquire(&timer->lockdep_map);
	lock_map_release(&timer->lockdep_map);
	local_irq_restore(flags);
#endif
	/*
	 * don't use it in hardirq context, because it
	 * could lead to deadlock.
	 */
	WARN_ON(in_hardirq() && !(timer->flags & TIMER_IRQSAFE));

	/*
	 * Must be able to sleep on PREEMPT_RT because of the slowpath in
	 * del_timer_wait_running().
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && !(timer->flags & TIMER_IRQSAFE))
		lockdep_assert_preemption_enabled();

	do {
		ret = __try_to_del_timer_sync(timer, shutdown);

		if (unlikely(ret < 0)) {
			del_timer_wait_running(timer);
			cpu_relax();
		}
	} while (ret < 0);

	return ret;
}

/**
 * timer_delete_sync - Deactivate a timer and wait for the handler to finish.
 * @timer:	The timer to be deactivated
 *
 * Synchronization rules: Callers must prevent restarting of the timer,
 * otherwise this function is meaningless. It must not be called from
 * interrupt contexts unless the timer is an irqsafe one. The caller must
 * not hold locks which would prevent completion of the timer's callback
 * function. The timer's handler must not call add_timer_on(). Upon exit
 * the timer is not queued and the handler is not running on any CPU.
 *
 * For !irqsafe timers, the caller must not hold locks that are held in
 * interrupt context. Even if the lock has nothing to do with the timer in
 * question.  Here's why::
 *
 *    CPU0                             CPU1
 *    ----                             ----
 *                                     <SOFTIRQ>
 *                                       call_timer_fn();
 *                                       base->running_timer = mytimer;
 *    spin_lock_irq(somelock);
 *                                     <IRQ>
 *                                        spin_lock(somelock);
 *    timer_delete_sync(mytimer);
 *    while (base->running_timer == mytimer);
 *
 * Now timer_delete_sync() will never return and never release somelock.
 * The interrupt on the other CPU is waiting to grab somelock but it has
 * interrupted the softirq that CPU0 is waiting to finish.
 *
 * This function cannot guarantee that the timer is not rearmed again by
 * some concurrent or preempting code, right after it dropped the base
 * lock. If there is the possibility of a concurrent rearm then the return
 * value of the function is meaningless.
 *
 * If such a guarantee is needed, e.g. for teardown situations then use
 * timer_shutdown_sync() instead.
 *
 * Return:
 * * %0	- The timer was not pending
 * * %1	- The timer was pending and deactivated
 */
int timer_delete_sync(struct timer_list *timer)
{
	return __timer_delete_sync(timer, false);
}
EXPORT_SYMBOL(timer_delete_sync);

/**
 * timer_shutdown_sync - Shutdown a timer and prevent rearming
 * @timer: The timer to be shutdown
 *
 * When the function returns it is guaranteed that:
 *   - @timer is not queued
 *   - The callback function of @timer is not running
 *   - @timer cannot be enqueued again. Any attempt to rearm
 *     @timer is silently ignored.
 *
 * See timer_delete_sync() for synchronization rules.
 *
 * This function is useful for final teardown of an infrastructure where
 * the timer is subject to a circular dependency problem.
 *
 * A common pattern for this is a timer and a workqueue where the timer can
 * schedule work and work can arm the timer. On shutdown the workqueue must
 * be destroyed and the timer must be prevented from rearming. Unless the
 * code has conditionals like 'if (mything->in_shutdown)' to prevent that
 * there is no way to get this correct with timer_delete_sync().
 *
 * timer_shutdown_sync() is solving the problem. The correct ordering of
 * calls in this case is:
 *
 *	timer_shutdown_sync(&mything->timer);
 *	workqueue_destroy(&mything->workqueue);
 *
 * After this 'mything' can be safely freed.
 *
 * This obviously implies that the timer is not required to be functional
 * for the rest of the shutdown operation.
 *
 * Return:
 * * %0 - The timer was not pending
 * * %1 - The timer was pending
 */
int timer_shutdown_sync(struct timer_list *timer)
{
	return __timer_delete_sync(timer, true);
}
EXPORT_SYMBOL_GPL(timer_shutdown_sync);

static void call_timer_fn(struct timer_list *timer,
			  void (*fn)(struct timer_list *),
			  unsigned long baseclk)
{
	int count = preempt_count();

#ifdef CONFIG_LOCKDEP
	/*
	 * It is permissible to free the timer from inside the
	 * function that is called from it, this we need to take into
	 * account for lockdep too. To avoid bogus "held lock freed"
	 * warnings as well as problems when looking into
	 * timer->lockdep_map, make a copy and use that here.
	 */
	struct lockdep_map lockdep_map;

	lockdep_copy_map(&lockdep_map, &timer->lockdep_map);
#endif
	/*
	 * Couple the lock chain with the lock chain at
	 * timer_delete_sync() by acquiring the lock_map around the fn()
	 * call here and in timer_delete_sync().
	 */
	lock_map_acquire(&lockdep_map);

	trace_timer_expire_entry(timer, baseclk);
	fn(timer);
	trace_timer_expire_exit(timer);

	lock_map_release(&lockdep_map);

	if (count != preempt_count()) {
		WARN_ONCE(1, "timer: %pS preempt leak: %08x -> %08x\n",
			  fn, count, preempt_count());
		/*
		 * Restore the preempt count. That gives us a decent
		 * chance to survive and extract information. If the
		 * callback kept a lock held, bad luck, but not worse
		 * than the BUG() we had.
		 */
		preempt_count_set(count);
	}
}

static void expire_timers(struct timer_base *base, struct hlist_head *head)
{
	/*
	 * This value is required only for tracing. base->clk was
	 * incremented directly before expire_timers was called. But expiry
	 * is related to the old base->clk value.
	 */
	unsigned long baseclk = base->clk - 1;

	while (!hlist_empty(head)) {
		struct timer_list *timer;
		void (*fn)(struct timer_list *);

		timer = hlist_entry(head->first, struct timer_list, entry);

		base->running_timer = timer;
		detach_timer(timer, true);

		fn = timer->function;

		if (WARN_ON_ONCE(!fn)) {
			/* Should never happen. Emphasis on should! */
			base->running_timer = NULL;
			continue;
		}

		if (timer->flags & TIMER_IRQSAFE) {
			raw_spin_unlock(&base->lock);
			call_timer_fn(timer, fn, baseclk);
			raw_spin_lock(&base->lock);
			base->running_timer = NULL;
		} else {
			raw_spin_unlock_irq(&base->lock);
			call_timer_fn(timer, fn, baseclk);
			raw_spin_lock_irq(&base->lock);
			base->running_timer = NULL;
			timer_sync_wait_running(base);
		}
	}
}

static int collect_expired_timers(struct timer_base *base,
				  struct hlist_head *heads)
{
	unsigned long clk = base->clk = base->next_expiry;
	struct hlist_head *vec;
	int i, levels = 0;
	unsigned int idx;

	for (i = 0; i < LVL_DEPTH; i++) {
		idx = (clk & LVL_MASK) + i * LVL_SIZE;

		if (__test_and_clear_bit(idx, base->pending_map)) {
			vec = base->vectors + idx;
			hlist_move_list(vec, heads++);
			levels++;
		}
		/* Is it time to look at the next level? */
		if (clk & LVL_CLK_MASK)
			break;
		/* Shift clock for the next level granularity */
		clk >>= LVL_CLK_SHIFT;
	}
	return levels;
}

/*
 * Find the next pending bucket of a level. Search from level start (@offset)
 * + @clk upwards and if nothing there, search from start of the level
 * (@offset) up to @offset + clk.
 */
static int next_pending_bucket(struct timer_base *base, unsigned offset,
			       unsigned clk)
{
	unsigned pos, start = offset + clk;
	unsigned end = offset + LVL_SIZE;

	pos = find_next_bit(base->pending_map, end, start);
	if (pos < end)
		return pos - start;

	pos = find_next_bit(base->pending_map, start, offset);
	return pos < start ? pos + LVL_SIZE - start : -1;
}

/*
 * Search the first expiring timer in the various clock levels. Caller must
 * hold base->lock.
 *
 * Store next expiry time in base->next_expiry.
 */
static void timer_recalc_next_expiry(struct timer_base *base)
{
	unsigned long clk, next, adj;
	unsigned lvl, offset = 0;

	next = base->clk + NEXT_TIMER_MAX_DELTA;
	clk = base->clk;
	for (lvl = 0; lvl < LVL_DEPTH; lvl++, offset += LVL_SIZE) {
		int pos = next_pending_bucket(base, offset, clk & LVL_MASK);
		unsigned long lvl_clk = clk & LVL_CLK_MASK;

		if (pos >= 0) {
			unsigned long tmp = clk + (unsigned long) pos;

			tmp <<= LVL_SHIFT(lvl);
			if (time_before(tmp, next))
				next = tmp;

			/*
			 * If the next expiration happens before we reach
			 * the next level, no need to check further.
			 */
			if (pos <= ((LVL_CLK_DIV - lvl_clk) & LVL_CLK_MASK))
				break;
		}
		/*
		 * Clock for the next level. If the current level clock lower
		 * bits are zero, we look at the next level as is. If not we
		 * need to advance it by one because that's going to be the
		 * next expiring bucket in that level. base->clk is the next
		 * expiring jiffy. So in case of:
		 *
		 * LVL5 LVL4 LVL3 LVL2 LVL1 LVL0
		 *  0    0    0    0    0    0
		 *
		 * we have to look at all levels @index 0. With
		 *
		 * LVL5 LVL4 LVL3 LVL2 LVL1 LVL0
		 *  0    0    0    0    0    2
		 *
		 * LVL0 has the next expiring bucket @index 2. The upper
		 * levels have the next expiring bucket @index 1.
		 *
		 * In case that the propagation wraps the next level the same
		 * rules apply:
		 *
		 * LVL5 LVL4 LVL3 LVL2 LVL1 LVL0
		 *  0    0    0    0    F    2
		 *
		 * So after looking at LVL0 we get:
		 *
		 * LVL5 LVL4 LVL3 LVL2 LVL1
		 *  0    0    0    1    0
		 *
		 * So no propagation from LVL1 to LVL2 because that happened
		 * with the add already, but then we need to propagate further
		 * from LVL2 to LVL3.
		 *
		 * So the simple check whether the lower bits of the current
		 * level are 0 or not is sufficient for all cases.
		 */
		adj = lvl_clk ? 1 : 0;
		clk >>= LVL_CLK_SHIFT;
		clk += adj;
	}

	WRITE_ONCE(base->next_expiry, next);
	base->next_expiry_recalc = false;
	base->timers_pending = !(next == base->clk + NEXT_TIMER_MAX_DELTA);
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * Check, if the next hrtimer event is before the next timer wheel
 * event:
 */
static u64 cmp_next_hrtimer_event(u64 basem, u64 expires)
{
	u64 nextevt = hrtimer_get_next_event();

	/*
	 * If high resolution timers are enabled
	 * hrtimer_get_next_event() returns KTIME_MAX.
	 */
	if (expires <= nextevt)
		return expires;

	/*
	 * If the next timer is already expired, return the tick base
	 * time so the tick is fired immediately.
	 */
	if (nextevt <= basem)
		return basem;

	/*
	 * Round up to the next jiffy. High resolution timers are
	 * off, so the hrtimers are expired in the tick and we need to
	 * make sure that this tick really expires the timer to avoid
	 * a ping pong of the nohz stop code.
	 *
	 * Use DIV_ROUND_UP_ULL to prevent gcc calling __divdi3
	 */
	return DIV_ROUND_UP_ULL(nextevt, TICK_NSEC) * TICK_NSEC;
}

static unsigned long next_timer_interrupt(struct timer_base *base,
					  unsigned long basej)
{
	if (base->next_expiry_recalc)
		timer_recalc_next_expiry(base);

	/*
	 * Move next_expiry for the empty base into the future to prevent an
	 * unnecessary raise of the timer softirq when the next_expiry value
	 * will be reached even if there is no timer pending.
	 *
	 * This update is also required to make timer_base::next_expiry values
	 * easy comparable to find out which base holds the first pending timer.
	 */
	if (!base->timers_pending)
		WRITE_ONCE(base->next_expiry, basej + NEXT_TIMER_MAX_DELTA);

	return base->next_expiry;
}

static unsigned long fetch_next_timer_interrupt(unsigned long basej, u64 basem,
						struct timer_base *base_local,
						struct timer_base *base_global,
						struct timer_events *tevt)
{
	unsigned long nextevt, nextevt_local, nextevt_global;
	bool local_first;

	nextevt_local = next_timer_interrupt(base_local, basej);
	nextevt_global = next_timer_interrupt(base_global, basej);

	local_first = time_before_eq(nextevt_local, nextevt_global);

	nextevt = local_first ? nextevt_local : nextevt_global;

	/*
	 * If the @nextevt is at max. one tick away, use @nextevt and store
	 * it in the local expiry value. The next global event is irrelevant in
	 * this case and can be left as KTIME_MAX.
	 */
	if (time_before_eq(nextevt, basej + 1)) {
		/* If we missed a tick already, force 0 delta */
		if (time_before(nextevt, basej))
			nextevt = basej;
		tevt->local = basem + (u64)(nextevt - basej) * TICK_NSEC;

		/*
		 * This is required for the remote check only but it doesn't
		 * hurt, when it is done for both call sites:
		 *
		 * * The remote callers will only take care of the global timers
		 *   as local timers will be handled by CPU itself. When not
		 *   updating tevt->global with the already missed first global
		 *   timer, it is possible that it will be missed completely.
		 *
		 * * The local callers will ignore the tevt->global anyway, when
		 *   nextevt is max. one tick away.
		 */
		if (!local_first)
			tevt->global = tevt->local;
		return nextevt;
	}

	/*
	 * Update tevt.* values:
	 *
	 * If the local queue expires first, then the global event can be
	 * ignored. If the global queue is empty, nothing to do either.
	 */
	if (!local_first && base_global->timers_pending)
		tevt->global = basem + (u64)(nextevt_global - basej) * TICK_NSEC;

	if (base_local->timers_pending)
		tevt->local = basem + (u64)(nextevt_local - basej) * TICK_NSEC;

	return nextevt;
}

# ifdef CONFIG_SMP
/**
 * fetch_next_timer_interrupt_remote() - Store next timers into @tevt
 * @basej:	base time jiffies
 * @basem:	base time clock monotonic
 * @tevt:	Pointer to the storage for the expiry values
 * @cpu:	Remote CPU
 *
 * Stores the next pending local and global timer expiry values in the
 * struct pointed to by @tevt. If a queue is empty the corresponding
 * field is set to KTIME_MAX. If local event expires before global
 * event, global event is set to KTIME_MAX as well.
 *
 * Caller needs to make sure timer base locks are held (use
 * timer_lock_remote_bases() for this purpose).
 */
void fetch_next_timer_interrupt_remote(unsigned long basej, u64 basem,
				       struct timer_events *tevt,
				       unsigned int cpu)
{
	struct timer_base *base_local, *base_global;

	/* Preset local / global events */
	tevt->local = tevt->global = KTIME_MAX;

	base_local = per_cpu_ptr(&timer_bases[BASE_LOCAL], cpu);
	base_global = per_cpu_ptr(&timer_bases[BASE_GLOBAL], cpu);

	lockdep_assert_held(&base_local->lock);
	lockdep_assert_held(&base_global->lock);

	fetch_next_timer_interrupt(basej, basem, base_local, base_global, tevt);
}

/**
 * timer_unlock_remote_bases - unlock timer bases of cpu
 * @cpu:	Remote CPU
 *
 * Unlocks the remote timer bases.
 */
void timer_unlock_remote_bases(unsigned int cpu)
	__releases(timer_bases[BASE_LOCAL]->lock)
	__releases(timer_bases[BASE_GLOBAL]->lock)
{
	struct timer_base *base_local, *base_global;

	base_local = per_cpu_ptr(&timer_bases[BASE_LOCAL], cpu);
	base_global = per_cpu_ptr(&timer_bases[BASE_GLOBAL], cpu);

	raw_spin_unlock(&base_global->lock);
	raw_spin_unlock(&base_local->lock);
}

/**
 * timer_lock_remote_bases - lock timer bases of cpu
 * @cpu:	Remote CPU
 *
 * Locks the remote timer bases.
 */
void timer_lock_remote_bases(unsigned int cpu)
	__acquires(timer_bases[BASE_LOCAL]->lock)
	__acquires(timer_bases[BASE_GLOBAL]->lock)
{
	struct timer_base *base_local, *base_global;

	base_local = per_cpu_ptr(&timer_bases[BASE_LOCAL], cpu);
	base_global = per_cpu_ptr(&timer_bases[BASE_GLOBAL], cpu);

	lockdep_assert_irqs_disabled();

	raw_spin_lock(&base_local->lock);
	raw_spin_lock_nested(&base_global->lock, SINGLE_DEPTH_NESTING);
}

/**
 * timer_base_is_idle() - Return whether timer base is set idle
 *
 * Returns value of local timer base is_idle value.
 */
bool timer_base_is_idle(void)
{
	return __this_cpu_read(timer_bases[BASE_LOCAL].is_idle);
}

static void __run_timer_base(struct timer_base *base);

/**
 * timer_expire_remote() - expire global timers of cpu
 * @cpu:	Remote CPU
 *
 * Expire timers of global base of remote CPU.
 */
void timer_expire_remote(unsigned int cpu)
{
	struct timer_base *base = per_cpu_ptr(&timer_bases[BASE_GLOBAL], cpu);

	__run_timer_base(base);
}

static void timer_use_tmigr(unsigned long basej, u64 basem,
			    unsigned long *nextevt, bool *tick_stop_path,
			    bool timer_base_idle, struct timer_events *tevt)
{
	u64 next_tmigr;

	if (timer_base_idle)
		next_tmigr = tmigr_cpu_new_timer(tevt->global);
	else if (tick_stop_path)
		next_tmigr = tmigr_cpu_deactivate(tevt->global);
	else
		next_tmigr = tmigr_quick_check(tevt->global);

	/*
	 * If the CPU is the last going idle in timer migration hierarchy, make
	 * sure the CPU will wake up in time to handle remote timers.
	 * next_tmigr == KTIME_MAX if other CPUs are still active.
	 */
	if (next_tmigr < tevt->local) {
		u64 tmp;

		/* If we missed a tick already, force 0 delta */
		if (next_tmigr < basem)
			next_tmigr = basem;

		tmp = div_u64(next_tmigr - basem, TICK_NSEC);

		*nextevt = basej + (unsigned long)tmp;
		tevt->local = next_tmigr;
	}
}
# else
static void timer_use_tmigr(unsigned long basej, u64 basem,
			    unsigned long *nextevt, bool *tick_stop_path,
			    bool timer_base_idle, struct timer_events *tevt)
{
	/*
	 * Make sure first event is written into tevt->local to not miss a
	 * timer on !SMP systems.
	 */
	tevt->local = min_t(u64, tevt->local, tevt->global);
}
# endif /* CONFIG_SMP */

static inline u64 __get_next_timer_interrupt(unsigned long basej, u64 basem,
					     bool *idle)
{
	struct timer_events tevt = { .local = KTIME_MAX, .global = KTIME_MAX };
	struct timer_base *base_local, *base_global;
	unsigned long nextevt;
	bool idle_is_possible;

	/*
	 * When the CPU is offline, the tick is cancelled and nothing is supposed
	 * to try to stop it.
	 */
	if (WARN_ON_ONCE(cpu_is_offline(smp_processor_id()))) {
		if (idle)
			*idle = true;
		return tevt.local;
	}

	base_local = this_cpu_ptr(&timer_bases[BASE_LOCAL]);
	base_global = this_cpu_ptr(&timer_bases[BASE_GLOBAL]);

	raw_spin_lock(&base_local->lock);
	raw_spin_lock_nested(&base_global->lock, SINGLE_DEPTH_NESTING);

	nextevt = fetch_next_timer_interrupt(basej, basem, base_local,
					     base_global, &tevt);

	/*
	 * If the next event is only one jiffy ahead there is no need to call
	 * timer migration hierarchy related functions. The value for the next
	 * global timer in @tevt struct equals then KTIME_MAX. This is also
	 * true, when the timer base is idle.
	 *
	 * The proper timer migration hierarchy function depends on the callsite
	 * and whether timer base is idle or not. @nextevt will be updated when
	 * this CPU needs to handle the first timer migration hierarchy
	 * event. See timer_use_tmigr() for detailed information.
	 */
	idle_is_possible = time_after(nextevt, basej + 1);
	if (idle_is_possible)
		timer_use_tmigr(basej, basem, &nextevt, idle,
				base_local->is_idle, &tevt);

	/*
	 * We have a fresh next event. Check whether we can forward the
	 * base.
	 */
	__forward_timer_base(base_local, basej);
	__forward_timer_base(base_global, basej);

	/*
	 * Set base->is_idle only when caller is timer_base_try_to_set_idle()
	 */
	if (idle) {
		/*
		 * Bases are idle if the next event is more than a tick
		 * away. Caution: @nextevt could have changed by enqueueing a
		 * global timer into timer migration hierarchy. Therefore a new
		 * check is required here.
		 *
		 * If the base is marked idle then any timer add operation must
		 * forward the base clk itself to keep granularity small. This
		 * idle logic is only maintained for the BASE_LOCAL and
		 * BASE_GLOBAL base, deferrable timers may still see large
		 * granularity skew (by design).
		 */
		if (!base_local->is_idle && time_after(nextevt, basej + 1)) {
			base_local->is_idle = true;
			/*
			 * Global timers queued locally while running in a task
			 * in nohz_full mode need a self-IPI to kick reprogramming
			 * in IRQ tail.
			 */
			if (tick_nohz_full_cpu(base_local->cpu))
				base_global->is_idle = true;
			trace_timer_base_idle(true, base_local->cpu);
		}
		*idle = base_local->is_idle;

		/*
		 * When timer base is not set idle, undo the effect of
		 * tmigr_cpu_deactivate() to prevent inconsistent states - active
		 * timer base but inactive timer migration hierarchy.
		 *
		 * When timer base was already marked idle, nothing will be
		 * changed here.
		 */
		if (!base_local->is_idle && idle_is_possible)
			tmigr_cpu_activate();
	}

	raw_spin_unlock(&base_global->lock);
	raw_spin_unlock(&base_local->lock);

	return cmp_next_hrtimer_event(basem, tevt.local);
}

/**
 * get_next_timer_interrupt() - return the time (clock mono) of the next timer
 * @basej:	base time jiffies
 * @basem:	base time clock monotonic
 *
 * Returns the tick aligned clock monotonic time of the next pending timer or
 * KTIME_MAX if no timer is pending. If timer of global base was queued into
 * timer migration hierarchy, first global timer is not taken into account. If
 * it was the last CPU of timer migration hierarchy going idle, first global
 * event is taken into account.
 */
u64 get_next_timer_interrupt(unsigned long basej, u64 basem)
{
	return __get_next_timer_interrupt(basej, basem, NULL);
}

/**
 * timer_base_try_to_set_idle() - Try to set the idle state of the timer bases
 * @basej:	base time jiffies
 * @basem:	base time clock monotonic
 * @idle:	pointer to store the value of timer_base->is_idle on return;
 *		*idle contains the information whether tick was already stopped
 *
 * Returns the tick aligned clock monotonic time of the next pending timer or
 * KTIME_MAX if no timer is pending. When tick was already stopped KTIME_MAX is
 * returned as well.
 */
u64 timer_base_try_to_set_idle(unsigned long basej, u64 basem, bool *idle)
{
	if (*idle)
		return KTIME_MAX;

	return __get_next_timer_interrupt(basej, basem, idle);
}

/**
 * timer_clear_idle - Clear the idle state of the timer base
 *
 * Called with interrupts disabled
 */
void timer_clear_idle(void)
{
	/*
	 * We do this unlocked. The worst outcome is a remote pinned timer
	 * enqueue sending a pointless IPI, but taking the lock would just
	 * make the window for sending the IPI a few instructions smaller
	 * for the cost of taking the lock in the exit from idle
	 * path. Required for BASE_LOCAL only.
	 */
	__this_cpu_write(timer_bases[BASE_LOCAL].is_idle, false);
	if (tick_nohz_full_cpu(smp_processor_id()))
		__this_cpu_write(timer_bases[BASE_GLOBAL].is_idle, false);
	trace_timer_base_idle(false, smp_processor_id());

	/* Activate without holding the timer_base->lock */
	tmigr_cpu_activate();
}
#endif

/**
 * __run_timers - run all expired timers (if any) on this CPU.
 * @base: the timer vector to be processed.
 */
static inline void __run_timers(struct timer_base *base)
{
	struct hlist_head heads[LVL_DEPTH];
	int levels;

	lockdep_assert_held(&base->lock);

	if (base->running_timer)
		return;

	while (time_after_eq(jiffies, base->clk) &&
	       time_after_eq(jiffies, base->next_expiry)) {
		levels = collect_expired_timers(base, heads);
		/*
		 * The two possible reasons for not finding any expired
		 * timer at this clk are that all matching timers have been
		 * dequeued or no timer has been queued since
		 * base::next_expiry was set to base::clk +
		 * NEXT_TIMER_MAX_DELTA.
		 */
		WARN_ON_ONCE(!levels && !base->next_expiry_recalc
			     && base->timers_pending);
		/*
		 * While executing timers, base->clk is set 1 offset ahead of
		 * jiffies to avoid endless requeuing to current jiffies.
		 */
		base->clk++;
		timer_recalc_next_expiry(base);

		while (levels--)
			expire_timers(base, heads + levels);
	}
}

static void __run_timer_base(struct timer_base *base)
{
	if (time_before(jiffies, base->next_expiry))
		return;

	timer_base_lock_expiry(base);
	raw_spin_lock_irq(&base->lock);
	__run_timers(base);
	raw_spin_unlock_irq(&base->lock);
	timer_base_unlock_expiry(base);
}

static void run_timer_base(int index)
{
	struct timer_base *base = this_cpu_ptr(&timer_bases[index]);

	__run_timer_base(base);
}

/*
 * This function runs timers and the timer-tq in bottom half context.
 */
static __latent_entropy void run_timer_softirq(void)
{
	run_timer_base(BASE_LOCAL);
	if (IS_ENABLED(CONFIG_NO_HZ_COMMON)) {
		run_timer_base(BASE_GLOBAL);
		run_timer_base(BASE_DEF);

		if (is_timers_nohz_active())
			tmigr_handle_remote();
	}
}

/*
 * Called by the local, per-CPU timer interrupt on SMP.
 */
static void run_local_timers(void)
{
	struct timer_base *base = this_cpu_ptr(&timer_bases[BASE_LOCAL]);

	hrtimer_run_queues();

	for (int i = 0; i < NR_BASES; i++, base++) {
		/*
		 * Raise the softirq only if required.
		 *
		 * timer_base::next_expiry can be written by a remote CPU while
		 * holding the lock. If this write happens at the same time than
		 * the lockless local read, sanity checker could complain about
		 * data corruption.
		 *
		 * There are two possible situations where
		 * timer_base::next_expiry is written by a remote CPU:
		 *
		 * 1. Remote CPU expires global timers of this CPU and updates
		 * timer_base::next_expiry of BASE_GLOBAL afterwards in
		 * next_timer_interrupt() or timer_recalc_next_expiry(). The
		 * worst outcome is a superfluous raise of the timer softirq
		 * when the not yet updated value is read.
		 *
		 * 2. A new first pinned timer is enqueued by a remote CPU
		 * and therefore timer_base::next_expiry of BASE_LOCAL is
		 * updated. When this update is missed, this isn't a
		 * problem, as an IPI is executed nevertheless when the CPU
		 * was idle before. When the CPU wasn't idle but the update
		 * is missed, then the timer would expire one jiffy late -
		 * bad luck.
		 *
		 * Those unlikely corner cases where the worst outcome is only a
		 * one jiffy delay or a superfluous raise of the softirq are
		 * not that expensive as doing the check always while holding
		 * the lock.
		 *
		 * Possible remote writers are using WRITE_ONCE(). Local reader
		 * uses therefore READ_ONCE().
		 */
		if (time_after_eq(jiffies, READ_ONCE(base->next_expiry)) ||
		    (i == BASE_DEF && tmigr_requires_handle_remote())) {
			raise_softirq(TIMER_SOFTIRQ);
			return;
		}
	}
}

/*
 * Called from the timer interrupt handler to charge one tick to the current
 * process.  user_tick is 1 if the tick is user time, 0 for system.
 */
void update_process_times(int user_tick)
{
	struct task_struct *p = current;

	/* Note: this timer irq context must be accounted for as well. */
	account_process_tick(p, user_tick);
	run_local_timers();
	rcu_sched_clock_irq(user_tick);
#ifdef CONFIG_IRQ_WORK
	if (in_irq())
		irq_work_tick();
#endif
	sched_tick();
	if (IS_ENABLED(CONFIG_POSIX_TIMERS))
		run_posix_cpu_timers();
}

/*
 * Since schedule_timeout()'s timer is defined on the stack, it must store
 * the target task on the stack as well.
 */
struct process_timer {
	struct timer_list timer;
	struct task_struct *task;
};

static void process_timeout(struct timer_list *t)
{
	struct process_timer *timeout = from_timer(timeout, t, timer);

	wake_up_process(timeout->task);
}

/**
 * schedule_timeout - sleep until timeout
 * @timeout: timeout value in jiffies
 *
 * Make the current task sleep until @timeout jiffies have elapsed.
 * The function behavior depends on the current task state
 * (see also set_current_state() description):
 *
 * %TASK_RUNNING - the scheduler is called, but the task does not sleep
 * at all. That happens because sched_submit_work() does nothing for
 * tasks in %TASK_RUNNING state.
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout jiffies are guaranteed to
 * pass before the routine returns unless the current task is explicitly
 * woken up, (e.g. by wake_up_process()).
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task or the current task is explicitly woken
 * up.
 *
 * The current task state is guaranteed to be %TASK_RUNNING when this
 * routine returns.
 *
 * Specifying a @timeout value of %MAX_SCHEDULE_TIMEOUT will schedule
 * the CPU away without a bound on the timeout. In this case the return
 * value will be %MAX_SCHEDULE_TIMEOUT.
 *
 * Returns 0 when the timer has expired otherwise the remaining time in
 * jiffies will be returned. In all cases the return value is guaranteed
 * to be non-negative.
 */
signed long __sched schedule_timeout(signed long timeout)
{
	struct process_timer timer;
	unsigned long expire;

	switch (timeout)
	{
	case MAX_SCHEDULE_TIMEOUT:
		/*
		 * These two special cases are useful to be comfortable
		 * in the caller. Nothing more. We could take
		 * MAX_SCHEDULE_TIMEOUT from one of the negative value
		 * but I' d like to return a valid offset (>=0) to allow
		 * the caller to do everything it want with the retval.
		 */
		schedule();
		goto out;
	default:
		/*
		 * Another bit of PARANOID. Note that the retval will be
		 * 0 since no piece of kernel is supposed to do a check
		 * for a negative retval of schedule_timeout() (since it
		 * should never happens anyway). You just have the printk()
		 * that will tell you if something is gone wrong and where.
		 */
		if (timeout < 0) {
			printk(KERN_ERR "schedule_timeout: wrong timeout "
				"value %lx\n", timeout);
			dump_stack();
			__set_current_state(TASK_RUNNING);
			goto out;
		}
	}

	expire = timeout + jiffies;

	timer.task = current;
	timer_setup_on_stack(&timer.timer, process_timeout, 0);
	__mod_timer(&timer.timer, expire, MOD_TIMER_NOTPENDING);
	schedule();
	del_timer_sync(&timer.timer);

	/* Remove the timer from the object tracker */
	destroy_timer_on_stack(&timer.timer);

	timeout = expire - jiffies;

 out:
	return timeout < 0 ? 0 : timeout;
}
EXPORT_SYMBOL(schedule_timeout);

/*
 * We can use __set_current_state() here because schedule_timeout() calls
 * schedule() unconditionally.
 */
signed long __sched schedule_timeout_interruptible(signed long timeout)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_interruptible);

signed long __sched schedule_timeout_killable(signed long timeout)
{
	__set_current_state(TASK_KILLABLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_killable);

signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_uninterruptible);

/*
 * Like schedule_timeout_uninterruptible(), except this task will not contribute
 * to load average.
 */
signed long __sched schedule_timeout_idle(signed long timeout)
{
	__set_current_state(TASK_IDLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_idle);

#ifdef CONFIG_HOTPLUG_CPU
static void migrate_timer_list(struct timer_base *new_base, struct hlist_head *head)
{
	struct timer_list *timer;
	int cpu = new_base->cpu;

	while (!hlist_empty(head)) {
		timer = hlist_entry(head->first, struct timer_list, entry);
		detach_timer(timer, false);
		timer->flags = (timer->flags & ~TIMER_BASEMASK) | cpu;
		internal_add_timer(new_base, timer);
	}
}

int timers_prepare_cpu(unsigned int cpu)
{
	struct timer_base *base;
	int b;

	for (b = 0; b < NR_BASES; b++) {
		base = per_cpu_ptr(&timer_bases[b], cpu);
		base->clk = jiffies;
		base->next_expiry = base->clk + NEXT_TIMER_MAX_DELTA;
		base->next_expiry_recalc = false;
		base->timers_pending = false;
		base->is_idle = false;
	}
	return 0;
}

int timers_dead_cpu(unsigned int cpu)
{
	struct timer_base *old_base;
	struct timer_base *new_base;
	int b, i;

	for (b = 0; b < NR_BASES; b++) {
		old_base = per_cpu_ptr(&timer_bases[b], cpu);
		new_base = get_cpu_ptr(&timer_bases[b]);
		/*
		 * The caller is globally serialized and nobody else
		 * takes two locks at once, deadlock is not possible.
		 */
		raw_spin_lock_irq(&new_base->lock);
		raw_spin_lock_nested(&old_base->lock, SINGLE_DEPTH_NESTING);

		/*
		 * The current CPUs base clock might be stale. Update it
		 * before moving the timers over.
		 */
		forward_timer_base(new_base);

		WARN_ON_ONCE(old_base->running_timer);
		old_base->running_timer = NULL;

		for (i = 0; i < WHEEL_SIZE; i++)
			migrate_timer_list(new_base, old_base->vectors + i);

		raw_spin_unlock(&old_base->lock);
		raw_spin_unlock_irq(&new_base->lock);
		put_cpu_ptr(&timer_bases);
	}
	return 0;
}

#endif /* CONFIG_HOTPLUG_CPU */

static void __init init_timer_cpu(int cpu)
{
	struct timer_base *base;
	int i;

	for (i = 0; i < NR_BASES; i++) {
		base = per_cpu_ptr(&timer_bases[i], cpu);
		base->cpu = cpu;
		raw_spin_lock_init(&base->lock);
		base->clk = jiffies;
		base->next_expiry = base->clk + NEXT_TIMER_MAX_DELTA;
		timer_base_init_expiry_lock(base);
	}
}

static void __init init_timer_cpus(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		init_timer_cpu(cpu);
}

void __init init_timers(void)
{
	init_timer_cpus();
	posix_cputimers_init_work();
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
}

/**
 * msleep - sleep safely even with waitqueue interruptions
 * @msecs: Time in milliseconds to sleep for
 */
void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	while (timeout)
		timeout = schedule_timeout_uninterruptible(timeout);
}

EXPORT_SYMBOL(msleep);

/**
 * msleep_interruptible - sleep waiting for signals
 * @msecs: Time in milliseconds to sleep for
 */
unsigned long msleep_interruptible(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	while (timeout && !signal_pending(current))
		timeout = schedule_timeout_interruptible(timeout);
	return jiffies_to_msecs(timeout);
}

EXPORT_SYMBOL(msleep_interruptible);

/**
 * usleep_range_state - Sleep for an approximate time in a given state
 * @min:	Minimum time in usecs to sleep
 * @max:	Maximum time in usecs to sleep
 * @state:	State of the current task that will be while sleeping
 *
 * In non-atomic context where the exact wakeup time is flexible, use
 * usleep_range_state() instead of udelay().  The sleep improves responsiveness
 * by avoiding the CPU-hogging busy-wait of udelay(), and the range reduces
 * power usage by allowing hrtimers to take advantage of an already-
 * scheduled interrupt instead of scheduling a new one just for this sleep.
 */
void __sched usleep_range_state(unsigned long min, unsigned long max,
				unsigned int state)
{
	ktime_t exp = ktime_add_us(ktime_get(), min);
	u64 delta = (u64)(max - min) * NSEC_PER_USEC;

	for (;;) {
		__set_current_state(state);
		/* Do not return before the requested sleep time has elapsed */
		if (!schedule_hrtimeout_range(&exp, delta, HRTIMER_MODE_ABS))
			break;
	}
}
EXPORT_SYMBOL(usleep_range_state);
