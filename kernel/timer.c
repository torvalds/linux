/*
 *  linux/kernel/timer.c
 *
 *  Kernel internal timers, basic process system calls
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
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

#define CREATE_TRACE_POINTS
#include <trace/events/timer.h>

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

struct tvec {
	struct list_head vec[TVN_SIZE];
};

struct tvec_root {
	struct list_head vec[TVR_SIZE];
};

struct tvec_base {
	spinlock_t lock;
	struct timer_list *running_timer;
	unsigned long timer_jiffies;
	unsigned long next_timer;
	struct tvec_root tv1;
	struct tvec tv2;
	struct tvec tv3;
	struct tvec tv4;
	struct tvec tv5;
} ____cacheline_aligned;

struct tvec_base boot_tvec_bases;
EXPORT_SYMBOL(boot_tvec_bases);
static DEFINE_PER_CPU(struct tvec_base *, tvec_bases) = &boot_tvec_bases;

/* Functions below help us manage 'deferrable' flag */
static inline unsigned int tbase_get_deferrable(struct tvec_base *base)
{
	return ((unsigned int)(unsigned long)base & TBASE_DEFERRABLE_FLAG);
}

static inline struct tvec_base *tbase_get_base(struct tvec_base *base)
{
	return ((struct tvec_base *)((unsigned long)base & ~TBASE_DEFERRABLE_FLAG));
}

static inline void timer_set_deferrable(struct timer_list *timer)
{
	timer->base = TBASE_MAKE_DEFERRED(timer->base);
}

static inline void
timer_set_base(struct timer_list *timer, struct tvec_base *new_base)
{
	timer->base = (struct tvec_base *)((unsigned long)(new_base) |
				      tbase_get_deferrable(timer->base));
}

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
	 * If the target jiffie is just after a whole second (which can happen
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

	if (j <= jiffies) /* rounding ate our timeout entirely; */
		return original;
	return j;
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

/**
 * set_timer_slack - set the allowed slack for a timer
 * @timer: the timer to be modified
 * @slack_hz: the amount of time (in jiffies) allowed for rounding
 *
 * Set the amount of time, in jiffies, that a certain timer has
 * in terms of slack. By setting this value, the timer subsystem
 * will schedule the actual timer somewhere between
 * the time mod_timer() asks for, and that time plus the slack.
 *
 * By setting the slack to -1, a percentage of the delay is used
 * instead.
 */
void set_timer_slack(struct timer_list *timer, int slack_hz)
{
	timer->slack = slack_hz;
}
EXPORT_SYMBOL_GPL(set_timer_slack);

static void internal_add_timer(struct tvec_base *base, struct timer_list *timer)
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

static void timer_stats_account_timer(struct timer_list *timer)
{
	unsigned int flag = 0;

	if (likely(!timer->start_site))
		return;
	if (unlikely(tbase_get_deferrable(timer->base)))
		flag |= TIMER_STATS_FLAG_DEFERRABLE;

	timer_stats_update_stats(timer, timer->start_pid, timer->start_site,
				 timer->function, timer->start_comm, flag);
}

#else
static void timer_stats_account_timer(struct timer_list *timer) {}
#endif

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS

static struct debug_obj_descr timer_debug_descr;

static void *timer_debug_hint(void *addr)
{
	return ((struct timer_list *) addr)->function;
}

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static int timer_fixup_init(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		del_timer_sync(timer);
		debug_object_init(timer, &timer_debug_descr);
		return 1;
	default:
		return 0;
	}
}

/* Stub timer callback for improperly used timers. */
static void stub_timer(unsigned long data)
{
	WARN_ON(1);
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown object is activated (might be a statically initialized object)
 */
static int timer_fixup_activate(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {

	case ODEBUG_STATE_NOTAVAILABLE:
		/*
		 * This is not really a fixup. The timer was
		 * statically initialized. We just make sure that it
		 * is tracked in the object tracker.
		 */
		if (timer->entry.next == NULL &&
		    timer->entry.prev == TIMER_ENTRY_STATIC) {
			debug_object_init(timer, &timer_debug_descr);
			debug_object_activate(timer, &timer_debug_descr);
			return 0;
		} else {
			setup_timer(timer, stub_timer, 0);
			return 1;
		}
		return 0;

	case ODEBUG_STATE_ACTIVE:
		WARN_ON(1);

	default:
		return 0;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static int timer_fixup_free(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		del_timer_sync(timer);
		debug_object_free(timer, &timer_debug_descr);
		return 1;
	default:
		return 0;
	}
}

/*
 * fixup_assert_init is called when:
 * - an untracked/uninit-ed object is found
 */
static int timer_fixup_assert_init(void *addr, enum debug_obj_state state)
{
	struct timer_list *timer = addr;

	switch (state) {
	case ODEBUG_STATE_NOTAVAILABLE:
		if (timer->entry.prev == TIMER_ENTRY_STATIC) {
			/*
			 * This is not really a fixup. The timer was
			 * statically initialized. We just make sure that it
			 * is tracked in the object tracker.
			 */
			debug_object_init(timer, &timer_debug_descr);
			return 0;
		} else {
			setup_timer(timer, stub_timer, 0);
			return 1;
		}
	default:
		return 0;
	}
}

static struct debug_obj_descr timer_debug_descr = {
	.name			= "timer_list",
	.debug_hint		= timer_debug_hint,
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

static inline void debug_timer_free(struct timer_list *timer)
{
	debug_object_free(timer, &timer_debug_descr);
}

static inline void debug_timer_assert_init(struct timer_list *timer)
{
	debug_object_assert_init(timer, &timer_debug_descr);
}

static void __init_timer(struct timer_list *timer,
			 const char *name,
			 struct lock_class_key *key);

void init_timer_on_stack_key(struct timer_list *timer,
			     const char *name,
			     struct lock_class_key *key)
{
	debug_object_init_on_stack(timer, &timer_debug_descr);
	__init_timer(timer, name, key);
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

static inline void
debug_activate(struct timer_list *timer, unsigned long expires)
{
	debug_timer_activate(timer);
	trace_timer_start(timer, expires);
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

static void __init_timer(struct timer_list *timer,
			 const char *name,
			 struct lock_class_key *key)
{
	timer->entry.next = NULL;
	timer->base = __raw_get_cpu_var(tvec_bases);
	timer->slack = -1;
#ifdef CONFIG_TIMER_STATS
	timer->start_site = NULL;
	timer->start_pid = -1;
	memset(timer->start_comm, 0, TASK_COMM_LEN);
#endif
	lockdep_init_map(&timer->lockdep_map, name, key, 0);
}

void setup_deferrable_timer_on_stack_key(struct timer_list *timer,
					 const char *name,
					 struct lock_class_key *key,
					 void (*function)(unsigned long),
					 unsigned long data)
{
	timer->function = function;
	timer->data = data;
	init_timer_on_stack_key(timer, name, key);
	timer_set_deferrable(timer);
}
EXPORT_SYMBOL_GPL(setup_deferrable_timer_on_stack_key);

/**
 * init_timer_key - initialize a timer
 * @timer: the timer to be initialized
 * @name: name of the timer
 * @key: lockdep class key of the fake lock used for tracking timer
 *       sync lock dependencies
 *
 * init_timer_key() must be done to a timer prior calling *any* of the
 * other timer functions.
 */
void init_timer_key(struct timer_list *timer,
		    const char *name,
		    struct lock_class_key *key)
{
	debug_init(timer);
	__init_timer(timer, name, key);
}
EXPORT_SYMBOL(init_timer_key);

void init_timer_deferrable_key(struct timer_list *timer,
			       const char *name,
			       struct lock_class_key *key)
{
	init_timer_key(timer, name, key);
	timer_set_deferrable(timer);
}
EXPORT_SYMBOL(init_timer_deferrable_key);

static inline void detach_timer(struct timer_list *timer,
				int clear_pending)
{
	struct list_head *entry = &timer->entry;

	debug_deactivate(timer);

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
static struct tvec_base *lock_timer_base(struct timer_list *timer,
					unsigned long *flags)
	__acquires(timer->base->lock)
{
	struct tvec_base *base;

	for (;;) {
		struct tvec_base *prelock_base = timer->base;
		base = tbase_get_base(prelock_base);
		if (likely(base != NULL)) {
			spin_lock_irqsave(&base->lock, *flags);
			if (likely(prelock_base == timer->base))
				return base;
			/* The timer has migrated to another CPU */
			spin_unlock_irqrestore(&base->lock, *flags);
		}
		cpu_relax();
	}
}

static inline int
__mod_timer(struct timer_list *timer, unsigned long expires,
						bool pending_only, int pinned)
{
	struct tvec_base *base, *new_base;
	unsigned long flags;
	int ret = 0 , cpu;

	timer_stats_timer_set_start_info(timer);
	BUG_ON(!timer->function);

	base = lock_timer_base(timer, &flags);

	if (timer_pending(timer)) {
		detach_timer(timer, 0);
		if (timer->expires == base->next_timer &&
		    !tbase_get_deferrable(timer->base))
			base->next_timer = base->timer_jiffies;
		ret = 1;
	} else {
		if (pending_only)
			goto out_unlock;
	}

	debug_activate(timer, expires);

	cpu = smp_processor_id();

#if defined(CONFIG_NO_HZ) && defined(CONFIG_SMP)
	if (!pinned && get_sysctl_timer_migration() && idle_cpu(cpu))
		cpu = get_nohz_timer_target();
#endif
	new_base = per_cpu(tvec_bases, cpu);

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
			timer_set_base(timer, NULL);
			spin_unlock(&base->lock);
			base = new_base;
			spin_lock(&base->lock);
			timer_set_base(timer, base);
		}
	}

	timer->expires = expires;
	if (time_before(timer->expires, base->next_timer) &&
	    !tbase_get_deferrable(timer->base))
		base->next_timer = timer->expires;
	internal_add_timer(base, timer);

out_unlock:
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

/**
 * mod_timer_pending - modify a pending timer's timeout
 * @timer: the pending timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer_pending() is the same for pending timers as mod_timer(),
 * but will not re-activate and modify already deleted timers.
 *
 * It is useful for unserialized use of timers.
 */
int mod_timer_pending(struct timer_list *timer, unsigned long expires)
{
	return __mod_timer(timer, expires, true, TIMER_NOT_PINNED);
}
EXPORT_SYMBOL(mod_timer_pending);

/*
 * Decide where to put the timer while taking the slack into account
 *
 * Algorithm:
 *   1) calculate the maximum (absolute) time
 *   2) calculate the highest bit where the expires and new max are different
 *   3) use this bit to make a mask
 *   4) use the bitmask to round down the maximum time, so that all last
 *      bits are zeros
 */
static inline
unsigned long apply_slack(struct timer_list *timer, unsigned long expires)
{
	unsigned long expires_limit, mask;
	int bit;

	if (timer->slack >= 0) {
		expires_limit = expires + timer->slack;
	} else {
		long delta = expires - jiffies;

		if (delta < 256)
			return expires;

		expires_limit = expires + delta / 256;
	}
	mask = expires ^ expires_limit;
	if (mask == 0)
		return expires;

	bit = find_last_bit(&mask, BITS_PER_LONG);

	mask = (1 << bit) - 1;

	expires_limit = expires_limit & ~(mask);

	return expires_limit;
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
	expires = apply_slack(timer, expires);

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer_pending(timer) && timer->expires == expires)
		return 1;

	return __mod_timer(timer, expires, false, TIMER_NOT_PINNED);
}
EXPORT_SYMBOL(mod_timer);

/**
 * mod_timer_pinned - modify a timer's timeout
 * @timer: the timer to be modified
 * @expires: new timeout in jiffies
 *
 * mod_timer_pinned() is a way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 * and not allow the timer to be migrated to a different CPU.
 *
 * mod_timer_pinned(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 */
int mod_timer_pinned(struct timer_list *timer, unsigned long expires)
{
	if (timer->expires == expires && timer_pending(timer))
		return 1;

	return __mod_timer(timer, expires, false, TIMER_PINNED);
}
EXPORT_SYMBOL(mod_timer_pinned);

/**
 * add_timer - start a timer
 * @timer: the timer to be added
 *
 * The kernel will do a ->function(->data) callback from the
 * timer interrupt at the ->expires point in the future. The
 * current time is 'jiffies'.
 *
 * The timer's ->expires, ->function (and if the handler uses it, ->data)
 * fields must be set prior calling this function.
 *
 * Timers with an ->expires field in the past will be executed in the next
 * timer tick.
 */
void add_timer(struct timer_list *timer)
{
	BUG_ON(timer_pending(timer));
	mod_timer(timer, timer->expires);
}
EXPORT_SYMBOL(add_timer);

/**
 * add_timer_on - start a timer on a particular CPU
 * @timer: the timer to be added
 * @cpu: the CPU to start it on
 *
 * This is not very scalable on SMP. Double adds are not possible.
 */
void add_timer_on(struct timer_list *timer, int cpu)
{
	struct tvec_base *base = per_cpu(tvec_bases, cpu);
	unsigned long flags;

	timer_stats_timer_set_start_info(timer);
	BUG_ON(timer_pending(timer) || !timer->function);
	spin_lock_irqsave(&base->lock, flags);
	timer_set_base(timer, base);
	debug_activate(timer, timer->expires);
	if (time_before(timer->expires, base->next_timer) &&
	    !tbase_get_deferrable(timer->base))
		base->next_timer = timer->expires;
	internal_add_timer(base, timer);
	/*
	 * Check whether the other CPU is idle and needs to be
	 * triggered to reevaluate the timer wheel when nohz is
	 * active. We are protected against the other CPU fiddling
	 * with the timer by holding the timer base lock. This also
	 * makes sure that a CPU on the way to idle can not evaluate
	 * the timer wheel.
	 */
	wake_up_idle_cpu(cpu);
	spin_unlock_irqrestore(&base->lock, flags);
}
EXPORT_SYMBOL_GPL(add_timer_on);

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
	struct tvec_base *base;
	unsigned long flags;
	int ret = 0;

	debug_assert_init(timer);

	timer_stats_timer_clear_start_info(timer);
	if (timer_pending(timer)) {
		base = lock_timer_base(timer, &flags);
		if (timer_pending(timer)) {
			detach_timer(timer, 1);
			if (timer->expires == base->next_timer &&
			    !tbase_get_deferrable(timer->base))
				base->next_timer = base->timer_jiffies;
			ret = 1;
		}
		spin_unlock_irqrestore(&base->lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL(del_timer);

/**
 * try_to_del_timer_sync - Try to deactivate a timer
 * @timer: timer do del
 *
 * This function tries to deactivate a timer. Upon successful (ret >= 0)
 * exit the timer is not queued and the handler is not running on any CPU.
 */
int try_to_del_timer_sync(struct timer_list *timer)
{
	struct tvec_base *base;
	unsigned long flags;
	int ret = -1;

	debug_assert_init(timer);

	base = lock_timer_base(timer, &flags);

	if (base->running_timer == timer)
		goto out;

	timer_stats_timer_clear_start_info(timer);
	ret = 0;
	if (timer_pending(timer)) {
		detach_timer(timer, 1);
		if (timer->expires == base->next_timer &&
		    !tbase_get_deferrable(timer->base))
			base->next_timer = base->timer_jiffies;
		ret = 1;
	}
out:
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}
EXPORT_SYMBOL(try_to_del_timer_sync);

#ifdef CONFIG_SMP
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
 * Note: You must not hold locks that are held in interrupt context
 *   while calling this function. Even if the lock has nothing to do
 *   with the timer in question.  Here's why:
 *
 *    CPU0                             CPU1
 *    ----                             ----
 *                                   <SOFTIRQ>
 *                                   call_timer_fn();
 *                                     base->running_timer = mytimer;
 *  spin_lock_irq(somelock);
 *                                     <IRQ>
 *                                        spin_lock(somelock);
 *  del_timer_sync(mytimer);
 *   while (base->running_timer == mytimer);
 *
 * Now del_timer_sync() will never return and never release somelock.
 * The interrupt on the other CPU is waiting to grab somelock but
 * it has interrupted the softirq that CPU0 is waiting to finish.
 *
 * The function returns whether it has deactivated a pending timer or not.
 */
int del_timer_sync(struct timer_list *timer)
{
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
	WARN_ON(in_irq());
	for (;;) {
		int ret = try_to_del_timer_sync(timer);
		if (ret >= 0)
			return ret;
		cpu_relax();
	}
}
EXPORT_SYMBOL(del_timer_sync);
#endif

static int cascade(struct tvec_base *base, struct tvec *tv, int index)
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
		BUG_ON(tbase_get_base(timer->base) != base);
		internal_add_timer(base, timer);
	}

	return index;
}

static void call_timer_fn(struct timer_list *timer, void (*fn)(unsigned long),
			  unsigned long data)
{
	int preempt_count = preempt_count();

#ifdef CONFIG_LOCKDEP
	/*
	 * It is permissible to free the timer from inside the
	 * function that is called from it, this we need to take into
	 * account for lockdep too. To avoid bogus "held lock freed"
	 * warnings as well as problems when looking into
	 * timer->lockdep_map, make a copy and use that here.
	 */
	struct lockdep_map lockdep_map = timer->lockdep_map;
#endif
	/*
	 * Couple the lock chain with the lock chain at
	 * del_timer_sync() by acquiring the lock_map around the fn()
	 * call here and in del_timer_sync().
	 */
	lock_map_acquire(&lockdep_map);

	trace_timer_expire_entry(timer);
	fn(data);
	trace_timer_expire_exit(timer);

	lock_map_release(&lockdep_map);

	if (preempt_count != preempt_count()) {
		WARN_ONCE(1, "timer: %pF preempt leak: %08x -> %08x\n",
			  fn, preempt_count, preempt_count());
		/*
		 * Restore the preempt count. That gives us a decent
		 * chance to survive and extract information. If the
		 * callback kept a lock held, bad luck, but not worse
		 * than the BUG() we had.
		 */
		preempt_count() = preempt_count;
	}
}

#define INDEX(N) ((base->timer_jiffies >> (TVR_BITS + (N) * TVN_BITS)) & TVN_MASK)

/**
 * __run_timers - run all expired timers (if any) on this CPU.
 * @base: the timer vector to be processed.
 *
 * This function cascades all vectors and executes all expired timer
 * vectors.
 */
static inline void __run_timers(struct tvec_base *base)
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

			timer = list_first_entry(head, struct timer_list,entry);
			fn = timer->function;
			data = timer->data;

			timer_stats_account_timer(timer);

			base->running_timer = timer;
			detach_timer(timer, 1);

			spin_unlock_irq(&base->lock);
			call_timer_fn(timer, fn, data);
			spin_lock_irq(&base->lock);
		}
	}
	base->running_timer = NULL;
	spin_unlock_irq(&base->lock);
}

#ifdef CONFIG_NO_HZ
/*
 * Find out when the next timer event is due to happen. This
 * is used on S/390 to stop all activity when a CPU is idle.
 * This function needs to be called with interrupts disabled.
 */
static unsigned long __next_timer_interrupt(struct tvec_base *base)
{
	unsigned long timer_jiffies = base->timer_jiffies;
	unsigned long expires = timer_jiffies + NEXT_TIMER_MAX_DELTA;
	int index, slot, array, found = 0;
	struct timer_list *nte;
	struct tvec *varray[4];

	/* Look for timer events in tv1. */
	index = slot = timer_jiffies & TVR_MASK;
	do {
		list_for_each_entry(nte, base->tv1.vec + slot, entry) {
			if (tbase_get_deferrable(nte->base))
				continue;

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
		struct tvec *varp = varray[array];

		index = slot = timer_jiffies & TVN_MASK;
		do {
			list_for_each_entry(nte, varp->vec + slot, entry) {
				if (tbase_get_deferrable(nte->base))
					continue;

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
	 * Limit the delta to the max value, which is checked in
	 * tick_nohz_stop_sched_tick():
	 */
	if (delta > NEXT_TIMER_MAX_DELTA)
		delta = NEXT_TIMER_MAX_DELTA;

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
 * get_next_timer_interrupt - return the jiffy of the next pending timer
 * @now: current time (in jiffies)
 */
unsigned long get_next_timer_interrupt(unsigned long now)
{
	struct tvec_base *base = __this_cpu_read(tvec_bases);
	unsigned long expires;

	/*
	 * Pretend that there is no timer pending if the cpu is offline.
	 * Possible pending timers will be migrated later to an active cpu.
	 */
	if (cpu_is_offline(smp_processor_id()))
		return now + NEXT_TIMER_MAX_DELTA;
	spin_lock(&base->lock);
	if (time_before_eq(base->next_timer, base->timer_jiffies))
		base->next_timer = __next_timer_interrupt(base);
	expires = base->next_timer;
	spin_unlock(&base->lock);

	if (time_before_eq(expires, now))
		return now;

	return cmp_next_hrtimer_event(now, expires);
}
#endif

/*
 * Called from the timer interrupt handler to charge one tick to the current
 * process.  user_tick is 1 if the tick is user time, 0 for system.
 */
void update_process_times(int user_tick)
{
	struct task_struct *p = current;
	int cpu = smp_processor_id();

	/* Note: this timer irq context must be accounted for as well. */
	account_process_tick(p, user_tick);
	run_local_timers();
	rcu_check_callbacks(cpu, user_tick);
	printk_tick();
#ifdef CONFIG_IRQ_WORK
	if (in_irq())
		irq_work_run();
#endif
	scheduler_tick();
	run_posix_cpu_timers(p);
}

/*
 * This function runs timers and the timer-tq in bottom half context.
 */
static void run_timer_softirq(struct softirq_action *h)
{
	struct tvec_base *base = __this_cpu_read(tvec_bases);

	hrtimer_run_pending();

	if (time_after_eq(jiffies, base->timer_jiffies))
		__run_timers(base);
}

/*
 * Called by the local, per-CPU timer interrupt on SMP.
 */
void run_local_timers(void)
{
	hrtimer_run_queues();
	raise_softirq(TIMER_SOFTIRQ);
}

#ifdef __ARCH_WANT_SYS_ALARM

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
SYSCALL_DEFINE1(alarm, unsigned int, seconds)
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
SYSCALL_DEFINE0(getpid)
{
	return task_tgid_vnr(current);
}

/*
 * Accessing ->real_parent is not SMP-safe, it could
 * change from under us. However, we can use a stale
 * value of ->real_parent under rcu_read_lock(), see
 * release_task()->call_rcu(delayed_put_task_struct).
 */
SYSCALL_DEFINE0(getppid)
{
	int pid;

	rcu_read_lock();
	pid = task_tgid_vnr(rcu_dereference(current->real_parent));
	rcu_read_unlock();

	return pid;
}

SYSCALL_DEFINE0(getuid)
{
	/* Only we change this so SMP safe */
	return current_uid();
}

SYSCALL_DEFINE0(geteuid)
{
	/* Only we change this so SMP safe */
	return current_euid();
}

SYSCALL_DEFINE0(getgid)
{
	/* Only we change this so SMP safe */
	return current_gid();
}

SYSCALL_DEFINE0(getegid)
{
	/* Only we change this so SMP safe */
	return  current_egid();
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
signed long __sched schedule_timeout(signed long timeout)
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

	setup_timer_on_stack(&timer, process_timeout, (unsigned long)current);
	__mod_timer(&timer, expire, false, TIMER_NOT_PINNED);
	schedule();
	del_singleshot_timer_sync(&timer);

	/* Remove the timer from the object tracker */
	destroy_timer_on_stack(&timer);

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

/* Thread ID - the internal kernel "pid" */
SYSCALL_DEFINE0(gettid)
{
	return task_pid_vnr(current);
}

/**
 * do_sysinfo - fill in sysinfo struct
 * @info: pointer to buffer to fill
 */
int do_sysinfo(struct sysinfo *info)
{
	unsigned long mem_total, sav_total;
	unsigned int mem_unit, bitcount;
	struct timespec tp;

	memset(info, 0, sizeof(struct sysinfo));

	ktime_get_ts(&tp);
	monotonic_to_bootbased(&tp);
	info->uptime = tp.tv_sec + (tp.tv_nsec ? 1 : 0);

	get_avenrun(info->loads, 0, SI_LOAD_SHIFT - FSHIFT);

	info->procs = nr_threads;

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

SYSCALL_DEFINE1(sysinfo, struct sysinfo __user *, info)
{
	struct sysinfo val;

	do_sysinfo(&val);

	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

static int __cpuinit init_timers_cpu(int cpu)
{
	int j;
	struct tvec_base *base;
	static char __cpuinitdata tvec_base_done[NR_CPUS];

	if (!tvec_base_done[cpu]) {
		static char boot_done;

		if (boot_done) {
			/*
			 * The APs use this path later in boot
			 */
			base = kmalloc_node(sizeof(*base),
						GFP_KERNEL | __GFP_ZERO,
						cpu_to_node(cpu));
			if (!base)
				return -ENOMEM;

			/* Make sure that tvec_base is 2 byte aligned */
			if (tbase_get_deferrable(base)) {
				WARN_ON(1);
				kfree(base);
				return -ENOMEM;
			}
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

	for (j = 0; j < TVN_SIZE; j++) {
		INIT_LIST_HEAD(base->tv5.vec + j);
		INIT_LIST_HEAD(base->tv4.vec + j);
		INIT_LIST_HEAD(base->tv3.vec + j);
		INIT_LIST_HEAD(base->tv2.vec + j);
	}
	for (j = 0; j < TVR_SIZE; j++)
		INIT_LIST_HEAD(base->tv1.vec + j);

	base->timer_jiffies = jiffies;
	base->next_timer = base->timer_jiffies;
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void migrate_timer_list(struct tvec_base *new_base, struct list_head *head)
{
	struct timer_list *timer;

	while (!list_empty(head)) {
		timer = list_first_entry(head, struct timer_list, entry);
		detach_timer(timer, 0);
		timer_set_base(timer, new_base);
		if (time_before(timer->expires, new_base->next_timer) &&
		    !tbase_get_deferrable(timer->base))
			new_base->next_timer = timer->expires;
		internal_add_timer(new_base, timer);
	}
}

static void __cpuinit migrate_timers(int cpu)
{
	struct tvec_base *old_base;
	struct tvec_base *new_base;
	int i;

	BUG_ON(cpu_online(cpu));
	old_base = per_cpu(tvec_bases, cpu);
	new_base = get_cpu_var(tvec_bases);
	/*
	 * The caller is globally serialized and nobody else
	 * takes two locks at once, deadlock is not possible.
	 */
	spin_lock_irq(&new_base->lock);
	spin_lock_nested(&old_base->lock, SINGLE_DEPTH_NESTING);

	BUG_ON(old_base->running_timer);

	for (i = 0; i < TVR_SIZE; i++)
		migrate_timer_list(new_base, old_base->tv1.vec + i);
	for (i = 0; i < TVN_SIZE; i++) {
		migrate_timer_list(new_base, old_base->tv2.vec + i);
		migrate_timer_list(new_base, old_base->tv3.vec + i);
		migrate_timer_list(new_base, old_base->tv4.vec + i);
		migrate_timer_list(new_base, old_base->tv5.vec + i);
	}

	spin_unlock(&old_base->lock);
	spin_unlock_irq(&new_base->lock);
	put_cpu_var(tvec_bases);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __cpuinit timer_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	int err;

	switch(action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		err = init_timers_cpu(cpu);
		if (err < 0)
			return notifier_from_errno(err);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
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

	BUG_ON(err != NOTIFY_OK);
	register_cpu_notifier(&timers_nb);
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
}

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

static int __sched do_usleep_range(unsigned long min, unsigned long max)
{
	ktime_t kmin;
	unsigned long delta;

	kmin = ktime_set(0, min * NSEC_PER_USEC);
	delta = (max - min) * NSEC_PER_USEC;
	return schedule_hrtimeout_range(&kmin, delta, HRTIMER_MODE_REL);
}

/**
 * usleep_range - Drop in replacement for udelay where wakeup is flexible
 * @min: Minimum time in usecs to sleep
 * @max: Maximum time in usecs to sleep
 */
void usleep_range(unsigned long min, unsigned long max)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	do_usleep_range(min, max);
}
EXPORT_SYMBOL(usleep_range);
