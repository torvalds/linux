/*
 *  linux/kernel/timer.c
 *
 *  Kernel internal timers, kernel timekeeping, basic process system calls
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
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
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

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

/*
 * per-CPU timer vector definitions:
 */
#define TVN_BITS (CONFIG_BASE_SMALL ? 4 : 6)
#define TVR_BITS (CONFIG_BASE_SMALL ? 6 : 8)
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

typedef struct tvec_s {
	struct list_head vec[TVN_SIZE];
} tvec_t;

typedef struct tvec_root_s {
	struct list_head vec[TVR_SIZE];
} tvec_root_t;

struct tvec_t_base_s {
	spinlock_t lock;
	struct timer_list *running_timer;
	unsigned long timer_jiffies;
	tvec_root_t tv1;
	tvec_t tv2;
	tvec_t tv3;
	tvec_t tv4;
	tvec_t tv5;
} ____cacheline_aligned_in_smp;

typedef struct tvec_t_base_s tvec_base_t;

tvec_base_t boot_tvec_bases;
EXPORT_SYMBOL(boot_tvec_bases);
static DEFINE_PER_CPU(tvec_base_t *, tvec_bases) = &boot_tvec_bases;

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
	 * If the target jiffie is just after a whole second (which can happen
	 * due to delays of the timer irq, long irq off times etc etc) then
	 * we should round down to the whole second, not up. Use 1/4th second
	 * as cutoff for this rounding as an extreme upper bound for this.
	 */
	if (rem < HZ/4) /* round down */
		j = j - rem;
	else /* round up */
		j = j - rem + HZ;

	/* now that we have rounded, subtract the extra skew again */
	j -= cpu * 3;

	if (j <= jiffies) /* rounding ate our timeout entirely; */
		return original;
	return j;
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
	/*
	 * In theory the following code can skip a jiffy in case jiffies
	 * increments right between the addition and the later subtraction.
	 * However since the entire point of this function is to use approximate
	 * timeouts, it's entirely ok to not handle that.
	 */
	return  __round_jiffies(j + jiffies, cpu) - jiffies;
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
	return __round_jiffies(j, raw_smp_processor_id());
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


static inline void set_running_timer(tvec_base_t *base,
					struct timer_list *timer)
{
#ifdef CONFIG_SMP
	base->running_timer = timer;
#endif
}

static void internal_add_timer(tvec_base_t *base, struct timer_list *timer)
{
	unsigned long expires = timer->expires;
	unsigned long idx = expires - base->timer_jiffies;
	struct list_head *vec;

	if (idx < TVR_SIZE) {
		int i = expires & TVR_MASK;
		vec = base->tv1.vec + i;
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
		int i = (expires >> TVR_BITS) & TVN_MASK;
		vec = base->tv2.vec + i;
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
		vec = base->tv3.vec + i;
	} else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
		vec = base->tv4.vec + i;
	} else if ((signed long) idx < 0) {
		/*
		 * Can happen if you add a timer with expires == jiffies,
		 * or you set a timer to go off in the past
		 */
		vec = base->tv1.vec + (base->timer_jiffies & TVR_MASK);
	} else {
		int i;
		/* If the timeout is larger than 0xffffffff on 64-bit
		 * architectures then we use the maximum timeout:
		 */
		if (idx > 0xffffffffUL) {
			idx = 0xffffffffUL;
			expires = idx + base->timer_jiffies;
		}
		i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
		vec = base->tv5.vec + i;
	}
	/*
	 * Timers are FIFO:
	 */
	list_add_tail(&timer->entry, vec);
}

#ifdef CONFIG_TIMER_STATS
void __timer_stats_timer_set_start_info(struct timer_list *timer, void *addr)
{
	if (timer->start_site)
		return;

	timer->start_site = addr;
	memcpy(timer->start_comm, current->comm, TASK_COMM_LEN);
	timer->start_pid = current->pid;
}
#endif

/**
 * init_timer - initialize a timer.
 * @timer: the timer to be initialized
 *
 * init_timer() must be done to a timer prior calling *any* of the
 * other timer functions.
 */
void fastcall init_timer(struct timer_list *timer)
{
	timer->entry.next = NULL;
	timer->base = __raw_get_cpu_var(tvec_bases);
#ifdef CONFIG_TIMER_STATS
	timer->start_site = NULL;
	timer->start_pid = -1;
	memset(timer->start_comm, 0, TASK_COMM_LEN);
#endif
}
EXPORT_SYMBOL(init_timer);

static inline void detach_timer(struct timer_list *timer,
				int clear_pending)
{
	struct list_head *entry = &timer->entry;

	__list_del(entry->prev, entry->next);
	if (clear_pending)
		entry->next = NULL;
	entry->prev = LIST_POISON2;
}

/*
 * We are using hashed locking: holding per_cpu(tvec_bases).lock
 * means that all timers which are tied to this base via timer->base are
 * locked, and the base itself is locked too.
 *
 * So __run_timers/migrate_timers can safely modify all timers which could
 * be found on ->tvX lists.
 *
 * When the timer's base is locked, and the timer removed from list, it is
 * possible to set timer->base = NULL and drop the lock: the timer remains
 * locked.
 */
static tvec_base_t *lock_timer_base(struct timer_list *timer,
					unsigned long *flags)
	__acquires(timer->base->lock)
{
	tvec_base_t *base;

	for (;;) {
		base = timer->base;
		if (likely(base != NULL)) {
			spin_lock_irqsave(&base->lock, *flags);
			if (likely(base == timer->base))
				return base;
			/* The timer has migrated to another CPU */
			spin_unlock_irqrestore(&base->lock, *flags);
		}
		cpu_relax();
	}
}

int __mod_timer(struct timer_list *timer, unsigned long expires)
{
	tvec_base_t *base, *new_base;
	unsigned long flags;
	int ret = 0;

	timer_stats_timer_set_start_info(timer);
	BUG_ON(!timer->function);

	base = lock_timer_base(timer, &flags);

	if (timer_pending(timer)) {
		detach_timer(timer, 0);
		ret = 1;
	}

	new_base = __get_cpu_var(tvec_bases);

	if (base != new_base) {
		/*
		 * We are trying to schedule the timer on the local CPU.
		 * However we can't change timer's base while it is running,
		 * otherwise del_timer_sync() can't detect that the timer's
		 * handler yet has not finished. This also guarantees that
		 * the timer is serialized wrt itself.
		 */
		if (likely(base->running_timer != timer)) {
			/* See the comment in lock_timer_base() */
			timer->base = NULL;
			spin_unlock(&base->lock);
			base = new_base;
			spin_lock(&base->lock);
			timer->base = base;
		}
	}

	timer->expires = expires;
	internal_add_timer(base, timer);
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

EXPORT_SYMBOL(__mod_timer);

/**
 * add_timer_on - start a timer on a particular CPU
 * @timer: the timer to be added
 * @cpu: the CPU to start it on
 *
 * This is not very scalable on SMP. Double adds are not possible.
 */
void add_timer_on(struct timer_list *timer, int cpu)
{
	tvec_base_t *base = per_cpu(tvec_bases, cpu);
  	unsigned long flags;

	timer_stats_timer_set_start_info(timer);
  	BUG_ON(timer_pending(timer) || !timer->function);
	spin_lock_irqsave(&base->lock, flags);
	timer->base = base;
	internal_add_timer(base, timer);
	spin_unlock_irqrestore(&base->lock, flags);
}


/**
 * mod_timer - modify a timer's timeout
 * @timer: the timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer() is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * The function returns whether it has modified a pending timer or not.
 * (ie. mod_timer() of an inactive timer returns 0, mod_timer() of an
 * active timer returns 1.)
 */
int mod_timer(struct timer_list *timer, unsigned long expires)
{
	BUG_ON(!timer->function);

	timer_stats_timer_set_start_info(timer);
	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && timer_pending(timer))
		return 1;

	return __mod_timer(timer, expires);
}

EXPORT_SYMBOL(mod_timer);

/**
 * del_timer - deactive a timer.
 * @timer: the timer to be deactivated
 *
 * del_timer() deactivates a timer - this works on both active and inactive
 * timers.
 *
 * The function returns whether it has deactivated a pending timer or not.
 * (ie. del_timer() of an inactive timer returns 0, del_timer() of an
 * active timer returns 1.)
 */
int del_timer(struct timer_list *timer)
{
	tvec_base_t *base;
	unsigned long flags;
	int ret = 0;

	timer_stats_timer_clear_start_info(timer);
	if (timer_pending(timer)) {
		base = lock_timer_base(timer, &flags);
		if (timer_pending(timer)) {
			detach_timer(timer, 1);
			ret = 1;
		}
		spin_unlock_irqrestore(&base->lock, flags);
	}

	return ret;
}

EXPORT_SYMBOL(del_timer);

#ifdef CONFIG_SMP
/**
 * try_to_del_timer_sync - Try to deactivate a timer
 * @timer: timer do del
 *
 * This function tries to deactivate a timer. Upon successful (ret >= 0)
 * exit the timer is not queued and the handler is not running on any CPU.
 *
 * It must not be called from interrupt contexts.
 */
int try_to_del_timer_sync(struct timer_list *timer)
{
	tvec_base_t *base;
	unsigned long flags;
	int ret = -1;

	base = lock_timer_base(timer, &flags);

	if (base->running_timer == timer)
		goto out;

	ret = 0;
	if (timer_pending(timer)) {
		detach_timer(timer, 1);
		ret = 1;
	}
out:
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

/**
 * del_timer_sync - deactivate a timer and wait for the handler to finish.
 * @timer: the timer to be deactivated
 *
 * This function only differs from del_timer() on SMP: besides deactivating
 * the timer it also makes sure the handler has finished executing on other
 * CPUs.
 *
 * Synchronization rules: Callers must prevent restarting of the timer,
 * otherwise this function is meaningless. It must not be called from
 * interrupt contexts. The caller must not hold locks which would prevent
 * completion of the timer's handler. The timer's handler must not call
 * add_timer_on(). Upon exit the timer is not queued and the handler is
 * not running on any CPU.
 *
 * The function returns whether it has deactivated a pending timer or not.
 */
int del_timer_sync(struct timer_list *timer)
{
	for (;;) {
		int ret = try_to_del_timer_sync(timer);
		if (ret >= 0)
			return ret;
		cpu_relax();
	}
}

EXPORT_SYMBOL(del_timer_sync);
#endif

static int cascade(tvec_base_t *base, tvec_t *tv, int index)
{
	/* cascade all the timers from tv up one level */
	struct timer_list *timer, *tmp;
	struct list_head tv_list;

	list_replace_init(tv->vec + index, &tv_list);

	/*
	 * We are removing _all_ timers from the list, so we
	 * don't have to detach them individually.
	 */
	list_for_each_entry_safe(timer, tmp, &tv_list, entry) {
		BUG_ON(timer->base != base);
		internal_add_timer(base, timer);
	}

	return index;
}

#define INDEX(N) ((base->timer_jiffies >> (TVR_BITS + (N) * TVN_BITS)) & TVN_MASK)

/**
 * __run_timers - run all expired timers (if any) on this CPU.
 * @base: the timer vector to be processed.
 *
 * This function cascades all vectors and executes all expired timer
 * vectors.
 */
static inline void __run_timers(tvec_base_t *base)
{
	struct timer_list *timer;

	spin_lock_irq(&base->lock);
	while (time_after_eq(jiffies, base->timer_jiffies)) {
		struct list_head work_list;
		struct list_head *head = &work_list;
 		int index = base->timer_jiffies & TVR_MASK;

		/*
		 * Cascade timers:
		 */
		if (!index &&
			(!cascade(base, &base->tv2, INDEX(0))) &&
				(!cascade(base, &base->tv3, INDEX(1))) &&
					!cascade(base, &base->tv4, INDEX(2)))
			cascade(base, &base->tv5, INDEX(3));
		++base->timer_jiffies;
		list_replace_init(base->tv1.vec + index, &work_list);
		while (!list_empty(head)) {
			void (*fn)(unsigned long);
			unsigned long data;

			timer = list_entry(head->next,struct timer_list,entry);
 			fn = timer->function;
 			data = timer->data;

			timer_stats_account_timer(timer);

			set_running_timer(base, timer);
			detach_timer(timer, 1);
			spin_unlock_irq(&base->lock);
			{
				int preempt_count = preempt_count();
				fn(data);
				if (preempt_count != preempt_count()) {
					printk(KERN_WARNING "huh, entered %p "
					       "with preempt_count %08x, exited"
					       " with %08x?\n",
					       fn, preempt_count,
					       preempt_count());
					BUG();
				}
			}
			spin_lock_irq(&base->lock);
		}
	}
	set_running_timer(base, NULL);
	spin_unlock_irq(&base->lock);
}

#if defined(CONFIG_NO_IDLE_HZ) || defined(CONFIG_NO_HZ)
/*
 * Find out when the next timer event is due to happen. This
 * is used on S/390 to stop all activity when a cpus is idle.
 * This functions needs to be called disabled.
 */
static unsigned long __next_timer_interrupt(tvec_base_t *base)
{
	unsigned long timer_jiffies = base->timer_jiffies;
	unsigned long expires = timer_jiffies + (LONG_MAX >> 1);
	int index, slot, array, found = 0;
	struct timer_list *nte;
	tvec_t *varray[4];

	/* Look for timer events in tv1. */
	index = slot = timer_jiffies & TVR_MASK;
	do {
		list_for_each_entry(nte, base->tv1.vec + slot, entry) {
			found = 1;
			expires = nte->expires;
			/* Look at the cascade bucket(s)? */
			if (!index || slot < index)
				goto cascade;
			return expires;
		}
		slot = (slot + 1) & TVR_MASK;
	} while (slot != index);

cascade:
	/* Calculate the next cascade event */
	if (index)
		timer_jiffies += TVR_SIZE - index;
	timer_jiffies >>= TVR_BITS;

	/* Check tv2-tv5. */
	varray[0] = &base->tv2;
	varray[1] = &base->tv3;
	varray[2] = &base->tv4;
	varray[3] = &base->tv5;

	for (array = 0; array < 4; array++) {
		tvec_t *varp = varray[array];

		index = slot = timer_jiffies & TVN_MASK;
		do {
			list_for_each_entry(nte, varp->vec + slot, entry) {
				found = 1;
				if (time_before(nte->expires, expires))
					expires = nte->expires;
			}
			/*
			 * Do we still search for the first timer or are
			 * we looking up the cascade buckets ?
			 */
			if (found) {
				/* Look at the cascade bucket(s)? */
				if (!index || slot < index)
					break;
				return expires;
			}
			slot = (slot + 1) & TVN_MASK;
		} while (slot != index);

		if (index)
			timer_jiffies += TVN_SIZE - index;
		timer_jiffies >>= TVN_BITS;
	}
	return expires;
}

/*
 * Check, if the next hrtimer event is before the next timer wheel
 * event:
 */
static unsigned long cmp_next_hrtimer_event(unsigned long now,
					    unsigned long expires)
{
	ktime_t hr_delta = hrtimer_get_next_event();
	struct timespec tsdelta;
	unsigned long delta;

	if (hr_delta.tv64 == KTIME_MAX)
		return expires;

	/*
	 * Expired timer available, let it expire in the next tick
	 */
	if (hr_delta.tv64 <= 0)
		return now + 1;

	tsdelta = ktime_to_timespec(hr_delta);
	delta = timespec_to_jiffies(&tsdelta);
	/*
	 * Take rounding errors in to account and make sure, that it
	 * expires in the next tick. Otherwise we go into an endless
	 * ping pong due to tick_nohz_stop_sched_tick() retriggering
	 * the timer softirq
	 */
	if (delta < 1)
		delta = 1;
	now += delta;
	if (time_before(now, expires))
		return now;
	return expires;
}

/**
 * next_timer_interrupt - return the jiffy of the next pending timer
 * @now: current time (in jiffies)
 */
unsigned long get_next_timer_interrupt(unsigned long now)
{
	tvec_base_t *base = __get_cpu_var(tvec_bases);
	unsigned long expires;

	spin_lock(&base->lock);
	expires = __next_timer_interrupt(base);
	spin_unlock(&base->lock);

	if (time_before_eq(expires, now))
		return now;

	return cmp_next_hrtimer_event(now, expires);
}

#ifdef CONFIG_NO_IDLE_HZ
unsigned long next_timer_interrupt(void)
{
	return get_next_timer_interrupt(jiffies);
}
#endif

#endif

/******************************************************************/

/* 
 * The current time 
 * wall_to_monotonic is what we need to add to xtime (or xtime corrected 
 * for sub jiffie times) to get to monotonic time.  Monotonic is pegged
 * at zero at system boot time, so wall_to_monotonic will be negative,
 * however, we will ALWAYS keep the tv_nsec part positive so we can use
 * the usual normalization.
 */
struct timespec xtime __attribute__ ((aligned (16)));
struct timespec wall_to_monotonic __attribute__ ((aligned (16)));

EXPORT_SYMBOL(xtime);


/* XXX - all of this timekeeping code should be later moved to time.c */
#include <linux/clocksource.h>
static struct clocksource *clock; /* pointer to current clocksource */

#ifdef CONFIG_GENERIC_TIME
/**
 * __get_nsec_offset - Returns nanoseconds since last call to periodic_hook
 *
 * private function, must hold xtime_lock lock when being
 * called. Returns the number of nanoseconds since the
 * last call to update_wall_time() (adjusted by NTP scaling)
 */
static inline s64 __get_nsec_offset(void)
{
	cycle_t cycle_now, cycle_delta;
	s64 ns_offset;

	/* read clocksource: */
	cycle_now = clocksource_read(clock);

	/* calculate the delta since the last update_wall_time: */
	cycle_delta = (cycle_now - clock->cycle_last) & clock->mask;

	/* convert to nanoseconds: */
	ns_offset = cyc2ns(clock, cycle_delta);

	return ns_offset;
}

/**
 * __get_realtime_clock_ts - Returns the time of day in a timespec
 * @ts:		pointer to the timespec to be set
 *
 * Returns the time of day in a timespec. Used by
 * do_gettimeofday() and get_realtime_clock_ts().
 */
static inline void __get_realtime_clock_ts(struct timespec *ts)
{
	unsigned long seq;
	s64 nsecs;

	do {
		seq = read_seqbegin(&xtime_lock);

		*ts = xtime;
		nsecs = __get_nsec_offset();

	} while (read_seqretry(&xtime_lock, seq));

	timespec_add_ns(ts, nsecs);
}

/**
 * getnstimeofday - Returns the time of day in a timespec
 * @ts:		pointer to the timespec to be set
 *
 * Returns the time of day in a timespec.
 */
void getnstimeofday(struct timespec *ts)
{
	__get_realtime_clock_ts(ts);
}

EXPORT_SYMBOL(getnstimeofday);

/**
 * do_gettimeofday - Returns the time of day in a timeval
 * @tv:		pointer to the timeval to be set
 *
 * NOTE: Users should be converted to using get_realtime_clock_ts()
 */
void do_gettimeofday(struct timeval *tv)
{
	struct timespec now;

	__get_realtime_clock_ts(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec/1000;
}

EXPORT_SYMBOL(do_gettimeofday);
/**
 * do_settimeofday - Sets the time of day
 * @tv:		pointer to the timespec variable containing the new time
 *
 * Sets the time of day to the new time and update NTP and notify hrtimers
 */
int do_settimeofday(struct timespec *tv)
{
	unsigned long flags;
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irqsave(&xtime_lock, flags);

	nsec -= __get_nsec_offset();

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	clock->error = 0;
	ntp_clear();

	update_vsyscall(&xtime, clock);

	write_sequnlock_irqrestore(&xtime_lock, flags);

	/* signal hrtimers about time change */
	clock_was_set();

	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

/**
 * change_clocksource - Swaps clocksources if a new one is available
 *
 * Accumulates current time interval and initializes new clocksource
 */
static void change_clocksource(void)
{
	struct clocksource *new;
	cycle_t now;
	u64 nsec;

	new = clocksource_get_next();

	if (clock == new)
		return;

	now = clocksource_read(new);
	nsec =  __get_nsec_offset();
	timespec_add_ns(&xtime, nsec);

	clock = new;
	clock->cycle_last = now;

	clock->error = 0;
	clock->xtime_nsec = 0;
	clocksource_calculate_interval(clock, NTP_INTERVAL_LENGTH);

	tick_clock_notify();

	printk(KERN_INFO "Time: %s clocksource has been installed.\n",
	       clock->name);
}
#else
static inline void change_clocksource(void) { }
#endif

/**
 * timekeeping_is_continuous - check to see if timekeeping is free running
 */
int timekeeping_is_continuous(void)
{
	unsigned long seq;
	int ret;

	do {
		seq = read_seqbegin(&xtime_lock);

		ret = clock->flags & CLOCK_SOURCE_VALID_FOR_HRES;

	} while (read_seqretry(&xtime_lock, seq));

	return ret;
}

/**
 * read_persistent_clock -  Return time in seconds from the persistent clock.
 *
 * Weak dummy function for arches that do not yet support it.
 * Returns seconds from epoch using the battery backed persistent clock.
 * Returns zero if unsupported.
 *
 *  XXX - Do be sure to remove it once all arches implement it.
 */
unsigned long __attribute__((weak)) read_persistent_clock(void)
{
	return 0;
}

/*
 * timekeeping_init - Initializes the clocksource and common timekeeping values
 */
void __init timekeeping_init(void)
{
	unsigned long flags;
	unsigned long sec = read_persistent_clock();

	write_seqlock_irqsave(&xtime_lock, flags);

	ntp_clear();

	clock = clocksource_get_next();
	clocksource_calculate_interval(clock, NTP_INTERVAL_LENGTH);
	clock->cycle_last = clocksource_read(clock);

	xtime.tv_sec = sec;
	xtime.tv_nsec = 0;
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	write_sequnlock_irqrestore(&xtime_lock, flags);
}

/* flag for if timekeeping is suspended */
static int timekeeping_suspended;
/* time in seconds when suspend began */
static unsigned long timekeeping_suspend_time;

/**
 * timekeeping_resume - Resumes the generic timekeeping subsystem.
 * @dev:	unused
 *
 * This is for the generic clocksource timekeeping.
 * xtime/wall_to_monotonic/jiffies/etc are
 * still managed by arch specific suspend/resume code.
 */
static int timekeeping_resume(struct sys_device *dev)
{
	unsigned long flags;
	unsigned long now = read_persistent_clock();

	write_seqlock_irqsave(&xtime_lock, flags);

	if (now && (now > timekeeping_suspend_time)) {
		unsigned long sleep_length = now - timekeeping_suspend_time;

		xtime.tv_sec += sleep_length;
		wall_to_monotonic.tv_sec -= sleep_length;
	}
	/* re-base the last cycle value */
	clock->cycle_last = clocksource_read(clock);
	clock->error = 0;
	timekeeping_suspended = 0;
	write_sequnlock_irqrestore(&xtime_lock, flags);

	touch_softlockup_watchdog();

	clockevents_notify(CLOCK_EVT_NOTIFY_RESUME, NULL);

	/* Resume hrtimers */
	hres_timers_resume();

	return 0;
}

static int timekeeping_suspend(struct sys_device *dev, pm_message_t state)
{
	unsigned long flags;

	write_seqlock_irqsave(&xtime_lock, flags);
	timekeeping_suspended = 1;
	timekeeping_suspend_time = read_persistent_clock();
	write_sequnlock_irqrestore(&xtime_lock, flags);

	clockevents_notify(CLOCK_EVT_NOTIFY_SUSPEND, NULL);

	return 0;
}

/* sysfs resume/suspend bits for timekeeping */
static struct sysdev_class timekeeping_sysclass = {
	.resume		= timekeeping_resume,
	.suspend	= timekeeping_suspend,
	set_kset_name("timekeeping"),
};

static struct sys_device device_timer = {
	.id		= 0,
	.cls		= &timekeeping_sysclass,
};

static int __init timekeeping_init_device(void)
{
	int error = sysdev_class_register(&timekeeping_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(timekeeping_init_device);

/*
 * If the error is already larger, we look ahead even further
 * to compensate for late or lost adjustments.
 */
static __always_inline int clocksource_bigadjust(s64 error, s64 *interval,
						 s64 *offset)
{
	s64 tick_error, i;
	u32 look_ahead, adj;
	s32 error2, mult;

	/*
	 * Use the current error value to determine how much to look ahead.
	 * The larger the error the slower we adjust for it to avoid problems
	 * with losing too many ticks, otherwise we would overadjust and
	 * produce an even larger error.  The smaller the adjustment the
	 * faster we try to adjust for it, as lost ticks can do less harm
	 * here.  This is tuned so that an error of about 1 msec is adusted
	 * within about 1 sec (or 2^20 nsec in 2^SHIFT_HZ ticks).
	 */
	error2 = clock->error >> (TICK_LENGTH_SHIFT + 22 - 2 * SHIFT_HZ);
	error2 = abs(error2);
	for (look_ahead = 0; error2 > 0; look_ahead++)
		error2 >>= 2;

	/*
	 * Now calculate the error in (1 << look_ahead) ticks, but first
	 * remove the single look ahead already included in the error.
	 */
	tick_error = current_tick_length() >>
		(TICK_LENGTH_SHIFT - clock->shift + 1);
	tick_error -= clock->xtime_interval >> 1;
	error = ((error - tick_error) >> look_ahead) + tick_error;

	/* Finally calculate the adjustment shift value.  */
	i = *interval;
	mult = 1;
	if (error < 0) {
		error = -error;
		*interval = -*interval;
		*offset = -*offset;
		mult = -1;
	}
	for (adj = 0; error > i; adj++)
		error >>= 1;

	*interval <<= adj;
	*offset <<= adj;
	return mult << adj;
}

/*
 * Adjust the multiplier to reduce the error value,
 * this is optimized for the most common adjustments of -1,0,1,
 * for other values we can do a bit more work.
 */
static void clocksource_adjust(struct clocksource *clock, s64 offset)
{
	s64 error, interval = clock->cycle_interval;
	int adj;

	error = clock->error >> (TICK_LENGTH_SHIFT - clock->shift - 1);
	if (error > interval) {
		error >>= 2;
		if (likely(error <= interval))
			adj = 1;
		else
			adj = clocksource_bigadjust(error, &interval, &offset);
	} else if (error < -interval) {
		error >>= 2;
		if (likely(error >= -interval)) {
			adj = -1;
			interval = -interval;
			offset = -offset;
		} else
			adj = clocksource_bigadjust(error, &interval, &offset);
	} else
		return;

	clock->mult += adj;
	clock->xtime_interval += interval;
	clock->xtime_nsec -= offset;
	clock->error -= (interval - offset) <<
			(TICK_LENGTH_SHIFT - clock->shift);
}

/**
 * update_wall_time - Uses the current clocksource to increment the wall time
 *
 * Called from the timer interrupt, must hold a write on xtime_lock.
 */
static void update_wall_time(void)
{
	cycle_t offset;

	/* Make sure we're fully resumed: */
	if (unlikely(timekeeping_suspended))
		return;

#ifdef CONFIG_GENERIC_TIME
	offset = (clocksource_read(clock) - clock->cycle_last) & clock->mask;
#else
	offset = clock->cycle_interval;
#endif
	clock->xtime_nsec += (s64)xtime.tv_nsec << clock->shift;

	/* normally this loop will run just once, however in the
	 * case of lost or late ticks, it will accumulate correctly.
	 */
	while (offset >= clock->cycle_interval) {
		/* accumulate one interval */
		clock->xtime_nsec += clock->xtime_interval;
		clock->cycle_last += clock->cycle_interval;
		offset -= clock->cycle_interval;

		if (clock->xtime_nsec >= (u64)NSEC_PER_SEC << clock->shift) {
			clock->xtime_nsec -= (u64)NSEC_PER_SEC << clock->shift;
			xtime.tv_sec++;
			second_overflow();
		}

		/* interpolator bits */
		time_interpolator_update(clock->xtime_interval
						>> clock->shift);

		/* accumulate error between NTP and clock interval */
		clock->error += current_tick_length();
		clock->error -= clock->xtime_interval << (TICK_LENGTH_SHIFT - clock->shift);
	}

	/* correct the clock when NTP error is too big */
	clocksource_adjust(clock, offset);

	/* store full nanoseconds into xtime */
	xtime.tv_nsec = (s64)clock->xtime_nsec >> clock->shift;
	clock->xtime_nsec -= (s64)xtime.tv_nsec << clock->shift;

	/* check to see if there is a new clocksource to use */
	change_clocksource();
	update_vsyscall(&xtime, clock);
}

/*
 * Called from the timer interrupt handler to charge one tick to the current 
 * process.  user_tick is 1 if the tick is user time, 0 for system.
 */
void update_process_times(int user_tick)
{
	struct task_struct *p = current;
	int cpu = smp_processor_id();

	/* Note: this timer irq context must be accounted for as well. */
	if (user_tick)
		account_user_time(p, jiffies_to_cputime(1));
	else
		account_system_time(p, HARDIRQ_OFFSET, jiffies_to_cputime(1));
	run_local_timers();
	if (rcu_pending(cpu))
		rcu_check_callbacks(cpu, user_tick);
	scheduler_tick();
 	run_posix_cpu_timers(p);
}

/*
 * Nr of active tasks - counted in fixed-point numbers
 */
static unsigned long count_active_tasks(void)
{
	return nr_active() * FIXED_1;
}

/*
 * Hmm.. Changed this, as the GNU make sources (load.c) seems to
 * imply that avenrun[] is the standard name for this kind of thing.
 * Nothing else seems to be standardized: the fractional size etc
 * all seem to differ on different machines.
 *
 * Requires xtime_lock to access.
 */
unsigned long avenrun[3];

EXPORT_SYMBOL(avenrun);

/*
 * calc_load - given tick count, update the avenrun load estimates.
 * This is called while holding a write_lock on xtime_lock.
 */
static inline void calc_load(unsigned long ticks)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;

	count -= ticks;
	if (unlikely(count < 0)) {
		active_tasks = count_active_tasks();
		do {
			CALC_LOAD(avenrun[0], EXP_1, active_tasks);
			CALC_LOAD(avenrun[1], EXP_5, active_tasks);
			CALC_LOAD(avenrun[2], EXP_15, active_tasks);
			count += LOAD_FREQ;
		} while (count < 0);
	}
}

/*
 * This read-write spinlock protects us from races in SMP while
 * playing with xtime and avenrun.
 */
__attribute__((weak)) __cacheline_aligned_in_smp DEFINE_SEQLOCK(xtime_lock);

EXPORT_SYMBOL(xtime_lock);

/*
 * This function runs timers and the timer-tq in bottom half context.
 */
static void run_timer_softirq(struct softirq_action *h)
{
	tvec_base_t *base = __get_cpu_var(tvec_bases);

	hrtimer_run_queues();

	if (time_after_eq(jiffies, base->timer_jiffies))
		__run_timers(base);
}

/*
 * Called by the local, per-CPU timer interrupt on SMP.
 */
void run_local_timers(void)
{
	raise_softirq(TIMER_SOFTIRQ);
	softlockup_tick();
}

/*
 * Called by the timer interrupt. xtime_lock must already be taken
 * by the timer IRQ!
 */
static inline void update_times(unsigned long ticks)
{
	update_wall_time();
	calc_load(ticks);
}
  
/*
 * The 64-bit jiffies value is not atomic - you MUST NOT read it
 * without sampling the sequence number in xtime_lock.
 * jiffies is defined in the linker script...
 */

void do_timer(unsigned long ticks)
{
	jiffies_64 += ticks;
	update_times(ticks);
}

#ifdef __ARCH_WANT_SYS_ALARM

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
asmlinkage unsigned long sys_alarm(unsigned int seconds)
{
	return alarm_setitimer(seconds);
}

#endif

#ifndef __alpha__

/*
 * The Alpha uses getxpid, getxuid, and getxgid instead.  Maybe this
 * should be moved into arch/i386 instead?
 */

/**
 * sys_getpid - return the thread group id of the current process
 *
 * Note, despite the name, this returns the tgid not the pid.  The tgid and
 * the pid are identical unless CLONE_THREAD was specified on clone() in
 * which case the tgid is the same in all threads of the same group.
 *
 * This is SMP safe as current->tgid does not change.
 */
asmlinkage long sys_getpid(void)
{
	return current->tgid;
}

/*
 * Accessing ->real_parent is not SMP-safe, it could
 * change from under us. However, we can use a stale
 * value of ->real_parent under rcu_read_lock(), see
 * release_task()->call_rcu(delayed_put_task_struct).
 */
asmlinkage long sys_getppid(void)
{
	int pid;

	rcu_read_lock();
	pid = rcu_dereference(current->real_parent)->tgid;
	rcu_read_unlock();

	return pid;
}

asmlinkage long sys_getuid(void)
{
	/* Only we change this so SMP safe */
	return current->uid;
}

asmlinkage long sys_geteuid(void)
{
	/* Only we change this so SMP safe */
	return current->euid;
}

asmlinkage long sys_getgid(void)
{
	/* Only we change this so SMP safe */
	return current->gid;
}

asmlinkage long sys_getegid(void)
{
	/* Only we change this so SMP safe */
	return  current->egid;
}

#endif

static void process_timeout(unsigned long __data)
{
	wake_up_process((struct task_struct *)__data);
}

/**
 * schedule_timeout - sleep until timeout
 * @timeout: timeout value in jiffies
 *
 * Make the current task sleep until @timeout jiffies have
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout jiffies are guaranteed to
 * pass before the routine returns. The routine will return 0
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task. In this case the remaining time
 * in jiffies will be returned, or 0 if the timer expired in time
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Specifying a @timeout value of %MAX_SCHEDULE_TIMEOUT will schedule
 * the CPU away without a bound on the timeout. In this case the return
 * value will be %MAX_SCHEDULE_TIMEOUT.
 *
 * In all cases the return value is guaranteed to be non-negative.
 */
fastcall signed long __sched schedule_timeout(signed long timeout)
{
	struct timer_list timer;
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
			current->state = TASK_RUNNING;
			goto out;
		}
	}

	expire = timeout + jiffies;

	setup_timer(&timer, process_timeout, (unsigned long)current);
	__mod_timer(&timer, expire);
	schedule();
	del_singleshot_timer_sync(&timer);

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

signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_uninterruptible);

/* Thread ID - the internal kernel "pid" */
asmlinkage long sys_gettid(void)
{
	return current->pid;
}

/**
 * do_sysinfo - fill in sysinfo struct
 * @info: pointer to buffer to fill
 */ 
int do_sysinfo(struct sysinfo *info)
{
	unsigned long mem_total, sav_total;
	unsigned int mem_unit, bitcount;
	unsigned long seq;

	memset(info, 0, sizeof(struct sysinfo));

	do {
		struct timespec tp;
		seq = read_seqbegin(&xtime_lock);

		/*
		 * This is annoying.  The below is the same thing
		 * posix_get_clock_monotonic() does, but it wants to
		 * take the lock which we want to cover the loads stuff
		 * too.
		 */

		getnstimeofday(&tp);
		tp.tv_sec += wall_to_monotonic.tv_sec;
		tp.tv_nsec += wall_to_monotonic.tv_nsec;
		if (tp.tv_nsec - NSEC_PER_SEC >= 0) {
			tp.tv_nsec = tp.tv_nsec - NSEC_PER_SEC;
			tp.tv_sec++;
		}
		info->uptime = tp.tv_sec + (tp.tv_nsec ? 1 : 0);

		info->loads[0] = avenrun[0] << (SI_LOAD_SHIFT - FSHIFT);
		info->loads[1] = avenrun[1] << (SI_LOAD_SHIFT - FSHIFT);
		info->loads[2] = avenrun[2] << (SI_LOAD_SHIFT - FSHIFT);

		info->procs = nr_threads;
	} while (read_seqretry(&xtime_lock, seq));

	si_meminfo(info);
	si_swapinfo(info);

	/*
	 * If the sum of all the available memory (i.e. ram + swap)
	 * is less than can be stored in a 32 bit unsigned long then
	 * we can be binary compatible with 2.2.x kernels.  If not,
	 * well, in that case 2.2.x was broken anyways...
	 *
	 *  -Erik Andersen <andersee@debian.org>
	 */

	mem_total = info->totalram + info->totalswap;
	if (mem_total < info->totalram || mem_total < info->totalswap)
		goto out;
	bitcount = 0;
	mem_unit = info->mem_unit;
	while (mem_unit > 1) {
		bitcount++;
		mem_unit >>= 1;
		sav_total = mem_total;
		mem_total <<= 1;
		if (mem_total < sav_total)
			goto out;
	}

	/*
	 * If mem_total did not overflow, multiply all memory values by
	 * info->mem_unit and set it to 1.  This leaves things compatible
	 * with 2.2.x, and also retains compatibility with earlier 2.4.x
	 * kernels...
	 */

	info->mem_unit = 1;
	info->totalram <<= bitcount;
	info->freeram <<= bitcount;
	info->sharedram <<= bitcount;
	info->bufferram <<= bitcount;
	info->totalswap <<= bitcount;
	info->freeswap <<= bitcount;
	info->totalhigh <<= bitcount;
	info->freehigh <<= bitcount;

out:
	return 0;
}

asmlinkage long sys_sysinfo(struct sysinfo __user *info)
{
	struct sysinfo val;

	do_sysinfo(&val);

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

/*
 * lockdep: we want to track each per-CPU base as a separate lock-class,
 * but timer-bases are kmalloc()-ed, so we need to attach separate
 * keys to them:
 */
static struct lock_class_key base_lock_keys[NR_CPUS];

static int __devinit init_timers_cpu(int cpu)
{
	int j;
	tvec_base_t *base;
	static char __devinitdata tvec_base_done[NR_CPUS];

	if (!tvec_base_done[cpu]) {
		static char boot_done;

		if (boot_done) {
			/*
			 * The APs use this path later in boot
			 */
			base = kmalloc_node(sizeof(*base), GFP_KERNEL,
						cpu_to_node(cpu));
			if (!base)
				return -ENOMEM;
			memset(base, 0, sizeof(*base));
			per_cpu(tvec_bases, cpu) = base;
		} else {
			/*
			 * This is for the boot CPU - we use compile-time
			 * static initialisation because per-cpu memory isn't
			 * ready yet and because the memory allocators are not
			 * initialised either.
			 */
			boot_done = 1;
			base = &boot_tvec_bases;
		}
		tvec_base_done[cpu] = 1;
	} else {
		base = per_cpu(tvec_bases, cpu);
	}

	spin_lock_init(&base->lock);
	lockdep_set_class(&base->lock, base_lock_keys + cpu);

	for (j = 0; j < TVN_SIZE; j++) {
		INIT_LIST_HEAD(base->tv5.vec + j);
		INIT_LIST_HEAD(base->tv4.vec + j);
		INIT_LIST_HEAD(base->tv3.vec + j);
		INIT_LIST_HEAD(base->tv2.vec + j);
	}
	for (j = 0; j < TVR_SIZE; j++)
		INIT_LIST_HEAD(base->tv1.vec + j);

	base->timer_jiffies = jiffies;
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void migrate_timer_list(tvec_base_t *new_base, struct list_head *head)
{
	struct timer_list *timer;

	while (!list_empty(head)) {
		timer = list_entry(head->next, struct timer_list, entry);
		detach_timer(timer, 0);
		timer->base = new_base;
		internal_add_timer(new_base, timer);
	}
}

static void __devinit migrate_timers(int cpu)
{
	tvec_base_t *old_base;
	tvec_base_t *new_base;
	int i;

	BUG_ON(cpu_online(cpu));
	old_base = per_cpu(tvec_bases, cpu);
	new_base = get_cpu_var(tvec_bases);

	local_irq_disable();
	double_spin_lock(&new_base->lock, &old_base->lock,
			 smp_processor_id() < cpu);

	BUG_ON(old_base->running_timer);

	for (i = 0; i < TVR_SIZE; i++)
		migrate_timer_list(new_base, old_base->tv1.vec + i);
	for (i = 0; i < TVN_SIZE; i++) {
		migrate_timer_list(new_base, old_base->tv2.vec + i);
		migrate_timer_list(new_base, old_base->tv3.vec + i);
		migrate_timer_list(new_base, old_base->tv4.vec + i);
		migrate_timer_list(new_base, old_base->tv5.vec + i);
	}

	double_spin_unlock(&new_base->lock, &old_base->lock,
			   smp_processor_id() < cpu);
	local_irq_enable();
	put_cpu_var(tvec_bases);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __cpuinit timer_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	switch(action) {
	case CPU_UP_PREPARE:
		if (init_timers_cpu(cpu) < 0)
			return NOTIFY_BAD;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		migrate_timers(cpu);
		break;
#endif
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata timers_nb = {
	.notifier_call	= timer_cpu_notify,
};


void __init init_timers(void)
{
	int err = timer_cpu_notify(&timers_nb, (unsigned long)CPU_UP_PREPARE,
				(void *)(long)smp_processor_id());

	init_timer_stats();

	BUG_ON(err == NOTIFY_BAD);
	register_cpu_notifier(&timers_nb);
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq, NULL);
}

#ifdef CONFIG_TIME_INTERPOLATION

struct time_interpolator *time_interpolator __read_mostly;
static struct time_interpolator *time_interpolator_list __read_mostly;
static DEFINE_SPINLOCK(time_interpolator_lock);

static inline cycles_t time_interpolator_get_cycles(unsigned int src)
{
	unsigned long (*x)(void);

	switch (src)
	{
		case TIME_SOURCE_FUNCTION:
			x = time_interpolator->addr;
			return x();

		case TIME_SOURCE_MMIO64	:
			return readq_relaxed((void __iomem *)time_interpolator->addr);

		case TIME_SOURCE_MMIO32	:
			return readl_relaxed((void __iomem *)time_interpolator->addr);

		default: return get_cycles();
	}
}

static inline u64 time_interpolator_get_counter(int writelock)
{
	unsigned int src = time_interpolator->source;

	if (time_interpolator->jitter)
	{
		cycles_t lcycle;
		cycles_t now;

		do {
			lcycle = time_interpolator->last_cycle;
			now = time_interpolator_get_cycles(src);
			if (lcycle && time_after(lcycle, now))
				return lcycle;

			/* When holding the xtime write lock, there's no need
			 * to add the overhead of the cmpxchg.  Readers are
			 * force to retry until the write lock is released.
			 */
			if (writelock) {
				time_interpolator->last_cycle = now;
				return now;
			}
			/* Keep track of the last timer value returned. The use of cmpxchg here
			 * will cause contention in an SMP environment.
			 */
		} while (unlikely(cmpxchg(&time_interpolator->last_cycle, lcycle, now) != lcycle));
		return now;
	}
	else
		return time_interpolator_get_cycles(src);
}

void time_interpolator_reset(void)
{
	time_interpolator->offset = 0;
	time_interpolator->last_counter = time_interpolator_get_counter(1);
}

#define GET_TI_NSECS(count,i) (((((count) - i->last_counter) & (i)->mask) * (i)->nsec_per_cyc) >> (i)->shift)

unsigned long time_interpolator_get_offset(void)
{
	/* If we do not have a time interpolator set up then just return zero */
	if (!time_interpolator)
		return 0;

	return time_interpolator->offset +
		GET_TI_NSECS(time_interpolator_get_counter(0), time_interpolator);
}

#define INTERPOLATOR_ADJUST 65536
#define INTERPOLATOR_MAX_SKIP 10*INTERPOLATOR_ADJUST

void time_interpolator_update(long delta_nsec)
{
	u64 counter;
	unsigned long offset;

	/* If there is no time interpolator set up then do nothing */
	if (!time_interpolator)
		return;

	/*
	 * The interpolator compensates for late ticks by accumulating the late
	 * time in time_interpolator->offset. A tick earlier than expected will
	 * lead to a reset of the offset and a corresponding jump of the clock
	 * forward. Again this only works if the interpolator clock is running
	 * slightly slower than the regular clock and the tuning logic insures
	 * that.
	 */

	counter = time_interpolator_get_counter(1);
	offset = time_interpolator->offset +
			GET_TI_NSECS(counter, time_interpolator);

	if (delta_nsec < 0 || (unsigned long) delta_nsec < offset)
		time_interpolator->offset = offset - delta_nsec;
	else {
		time_interpolator->skips++;
		time_interpolator->ns_skipped += delta_nsec - offset;
		time_interpolator->offset = 0;
	}
	time_interpolator->last_counter = counter;

	/* Tuning logic for time interpolator invoked every minute or so.
	 * Decrease interpolator clock speed if no skips occurred and an offset is carried.
	 * Increase interpolator clock speed if we skip too much time.
	 */
	if (jiffies % INTERPOLATOR_ADJUST == 0)
	{
		if (time_interpolator->skips == 0 && time_interpolator->offset > tick_nsec)
			time_interpolator->nsec_per_cyc--;
		if (time_interpolator->ns_skipped > INTERPOLATOR_MAX_SKIP && time_interpolator->offset == 0)
			time_interpolator->nsec_per_cyc++;
		time_interpolator->skips = 0;
		time_interpolator->ns_skipped = 0;
	}
}

static inline int
is_better_time_interpolator(struct time_interpolator *new)
{
	if (!time_interpolator)
		return 1;
	return new->frequency > 2*time_interpolator->frequency ||
	    (unsigned long)new->drift < (unsigned long)time_interpolator->drift;
}

void
register_time_interpolator(struct time_interpolator *ti)
{
	unsigned long flags;

	/* Sanity check */
	BUG_ON(ti->frequency == 0 || ti->mask == 0);

	ti->nsec_per_cyc = ((u64)NSEC_PER_SEC << ti->shift) / ti->frequency;
	spin_lock(&time_interpolator_lock);
	write_seqlock_irqsave(&xtime_lock, flags);
	if (is_better_time_interpolator(ti)) {
		time_interpolator = ti;
		time_interpolator_reset();
	}
	write_sequnlock_irqrestore(&xtime_lock, flags);

	ti->next = time_interpolator_list;
	time_interpolator_list = ti;
	spin_unlock(&time_interpolator_lock);
}

void
unregister_time_interpolator(struct time_interpolator *ti)
{
	struct time_interpolator *curr, **prev;
	unsigned long flags;

	spin_lock(&time_interpolator_lock);
	prev = &time_interpolator_list;
	for (curr = *prev; curr; curr = curr->next) {
		if (curr == ti) {
			*prev = curr->next;
			break;
		}
		prev = &curr->next;
	}

	write_seqlock_irqsave(&xtime_lock, flags);
	if (ti == time_interpolator) {
		/* we lost the best time-interpolator: */
		time_interpolator = NULL;
		/* find the next-best interpolator */
		for (curr = time_interpolator_list; curr; curr = curr->next)
			if (is_better_time_interpolator(curr))
				time_interpolator = curr;
		time_interpolator_reset();
	}
	write_sequnlock_irqrestore(&xtime_lock, flags);
	spin_unlock(&time_interpolator_lock);
}
#endif /* CONFIG_TIME_INTERPOLATION */

/**
 * msleep - sleep safely even with waitqueue interruptions
 * @msecs: Time in milliseconds to sleep for
 */
void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;

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
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;

	while (timeout && !signal_pending(current))
		timeout = schedule_timeout_interruptible(timeout);
	return jiffies_to_msecs(timeout);
}

EXPORT_SYMBOL(msleep_interruptible);
