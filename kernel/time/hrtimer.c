// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright(C) 2005-2006, Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2005-2007, Red Hat, Inc., Ingo Molnar
 *  Copyright(C) 2006-2007  Timesys Corp., Thomas Gleixner
 *
 *  High-resolution kernel timers
 *
 *  In contrast to the low-resolution timeout API, aka timer wheel,
 *  hrtimers provide finer resolution and accuracy depending on system
 *  configuration and capabilities.
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  Credits:
 *	Based on the original timer wheel code
 *
 *	Help, testing, suggestions, bugfixes, improvements were
 *	provided by:
 *
 *	George Anzinger, Andrew Morton, Steven Rostedt, Roman Zippel
 *	et. al.
 */

#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/tick.h>
#include <linux/err.h>
#include <linux/debugobjects.h>
#include <linux/sched/signal.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/deadline.h>
#include <linux/sched/nohz.h>
#include <linux/sched/debug.h>
#include <linux/timer.h>
#include <linux/freezer.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

#include <trace/events/timer.h>

#include "tick-internal.h"

/*
 * Masks for selecting the soft and hard context timers from
 * cpu_base->active
 */
#define MASK_SHIFT		(HRTIMER_BASE_MONOTONIC_SOFT)
#define HRTIMER_ACTIVE_HARD	((1U << MASK_SHIFT) - 1)
#define HRTIMER_ACTIVE_SOFT	(HRTIMER_ACTIVE_HARD << MASK_SHIFT)
#define HRTIMER_ACTIVE_ALL	(HRTIMER_ACTIVE_SOFT | HRTIMER_ACTIVE_HARD)

/*
 * The timer bases:
 *
 * There are more clockids than hrtimer bases. Thus, we index
 * into the timer bases by the hrtimer_base_type enum. When trying
 * to reach a base using a clockid, hrtimer_clockid_to_base()
 * is used to convert from clockid to the proper hrtimer_base_type.
 */
DEFINE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases) =
{
	.lock = __RAW_SPIN_LOCK_UNLOCKED(hrtimer_bases.lock),
	.clock_base =
	{
		{
			.index = HRTIMER_BASE_MONOTONIC,
			.clockid = CLOCK_MONOTONIC,
			.get_time = &ktime_get,
		},
		{
			.index = HRTIMER_BASE_REALTIME,
			.clockid = CLOCK_REALTIME,
			.get_time = &ktime_get_real,
		},
		{
			.index = HRTIMER_BASE_BOOTTIME,
			.clockid = CLOCK_BOOTTIME,
			.get_time = &ktime_get_boottime,
		},
		{
			.index = HRTIMER_BASE_TAI,
			.clockid = CLOCK_TAI,
			.get_time = &ktime_get_clocktai,
		},
		{
			.index = HRTIMER_BASE_MONOTONIC_SOFT,
			.clockid = CLOCK_MONOTONIC,
			.get_time = &ktime_get,
		},
		{
			.index = HRTIMER_BASE_REALTIME_SOFT,
			.clockid = CLOCK_REALTIME,
			.get_time = &ktime_get_real,
		},
		{
			.index = HRTIMER_BASE_BOOTTIME_SOFT,
			.clockid = CLOCK_BOOTTIME,
			.get_time = &ktime_get_boottime,
		},
		{
			.index = HRTIMER_BASE_TAI_SOFT,
			.clockid = CLOCK_TAI,
			.get_time = &ktime_get_clocktai,
		},
	}
};

static const int hrtimer_clock_to_base_table[MAX_CLOCKS] = {
	/* Make sure we catch unsupported clockids */
	[0 ... MAX_CLOCKS - 1]	= HRTIMER_MAX_CLOCK_BASES,

	[CLOCK_REALTIME]	= HRTIMER_BASE_REALTIME,
	[CLOCK_MONOTONIC]	= HRTIMER_BASE_MONOTONIC,
	[CLOCK_BOOTTIME]	= HRTIMER_BASE_BOOTTIME,
	[CLOCK_TAI]		= HRTIMER_BASE_TAI,
};

/*
 * Functions and macros which are different for UP/SMP systems are kept in a
 * single place
 */
#ifdef CONFIG_SMP

/*
 * We require the migration_base for lock_hrtimer_base()/switch_hrtimer_base()
 * such that hrtimer_callback_running() can unconditionally dereference
 * timer->base->cpu_base
 */
static struct hrtimer_cpu_base migration_cpu_base = {
	.clock_base = { {
		.cpu_base = &migration_cpu_base,
		.seq      = SEQCNT_RAW_SPINLOCK_ZERO(migration_cpu_base.seq,
						     &migration_cpu_base.lock),
	}, },
};

#define migration_base	migration_cpu_base.clock_base[0]

static inline bool is_migration_base(struct hrtimer_clock_base *base)
{
	return base == &migration_base;
}

/*
 * We are using hashed locking: holding per_cpu(hrtimer_bases)[n].lock
 * means that all timers which are tied to this base via timer->base are
 * locked, and the base itself is locked too.
 *
 * So __run_timers/migrate_timers can safely modify all timers which could
 * be found on the lists/queues.
 *
 * When the timer's base is locked, and the timer removed from list, it is
 * possible to set timer->base = &migration_base and drop the lock: the timer
 * remains locked.
 */
static
struct hrtimer_clock_base *lock_hrtimer_base(const struct hrtimer *timer,
					     unsigned long *flags)
{
	struct hrtimer_clock_base *base;

	for (;;) {
		base = READ_ONCE(timer->base);
		if (likely(base != &migration_base)) {
			raw_spin_lock_irqsave(&base->cpu_base->lock, *flags);
			if (likely(base == timer->base))
				return base;
			/* The timer has migrated to another CPU: */
			raw_spin_unlock_irqrestore(&base->cpu_base->lock, *flags);
		}
		cpu_relax();
	}
}

/*
 * We do not migrate the timer when it is expiring before the next
 * event on the target cpu. When high resolution is enabled, we cannot
 * reprogram the target cpu hardware and we would cause it to fire
 * late. To keep it simple, we handle the high resolution enabled and
 * disabled case similar.
 *
 * Called with cpu_base->lock of target cpu held.
 */
static int
hrtimer_check_target(struct hrtimer *timer, struct hrtimer_clock_base *new_base)
{
	ktime_t expires;

	expires = ktime_sub(hrtimer_get_expires(timer), new_base->offset);
	return expires < new_base->cpu_base->expires_next;
}

static inline
struct hrtimer_cpu_base *get_target_base(struct hrtimer_cpu_base *base,
					 int pinned)
{
#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
	if (static_branch_likely(&timers_migration_enabled) && !pinned)
		return &per_cpu(hrtimer_bases, get_nohz_timer_target());
#endif
	return base;
}

/*
 * We switch the timer base to a power-optimized selected CPU target,
 * if:
 *	- NO_HZ_COMMON is enabled
 *	- timer migration is enabled
 *	- the timer callback is not running
 *	- the timer is not the first expiring timer on the new target
 *
 * If one of the above requirements is not fulfilled we move the timer
 * to the current CPU or leave it on the previously assigned CPU if
 * the timer callback is currently running.
 */
static inline struct hrtimer_clock_base *
switch_hrtimer_base(struct hrtimer *timer, struct hrtimer_clock_base *base,
		    int pinned)
{
	struct hrtimer_cpu_base *new_cpu_base, *this_cpu_base;
	struct hrtimer_clock_base *new_base;
	int basenum = base->index;

	this_cpu_base = this_cpu_ptr(&hrtimer_bases);
	new_cpu_base = get_target_base(this_cpu_base, pinned);
again:
	new_base = &new_cpu_base->clock_base[basenum];

	if (base != new_base) {
		/*
		 * We are trying to move timer to new_base.
		 * However we can't change timer's base while it is running,
		 * so we keep it on the same CPU. No hassle vs. reprogramming
		 * the event source in the high resolution case. The softirq
		 * code will take care of this when the timer function has
		 * completed. There is no conflict as we hold the lock until
		 * the timer is enqueued.
		 */
		if (unlikely(hrtimer_callback_running(timer)))
			return base;

		/* See the comment in lock_hrtimer_base() */
		WRITE_ONCE(timer->base, &migration_base);
		raw_spin_unlock(&base->cpu_base->lock);
		raw_spin_lock(&new_base->cpu_base->lock);

		if (new_cpu_base != this_cpu_base &&
		    hrtimer_check_target(timer, new_base)) {
			raw_spin_unlock(&new_base->cpu_base->lock);
			raw_spin_lock(&base->cpu_base->lock);
			new_cpu_base = this_cpu_base;
			WRITE_ONCE(timer->base, base);
			goto again;
		}
		WRITE_ONCE(timer->base, new_base);
	} else {
		if (new_cpu_base != this_cpu_base &&
		    hrtimer_check_target(timer, new_base)) {
			new_cpu_base = this_cpu_base;
			goto again;
		}
	}
	return new_base;
}

#else /* CONFIG_SMP */

static inline bool is_migration_base(struct hrtimer_clock_base *base)
{
	return false;
}

static inline struct hrtimer_clock_base *
lock_hrtimer_base(const struct hrtimer *timer, unsigned long *flags)
{
	struct hrtimer_clock_base *base = timer->base;

	raw_spin_lock_irqsave(&base->cpu_base->lock, *flags);

	return base;
}

# define switch_hrtimer_base(t, b, p)	(b)

#endif	/* !CONFIG_SMP */

/*
 * Functions for the union type storage format of ktime_t which are
 * too large for inlining:
 */
#if BITS_PER_LONG < 64
/*
 * Divide a ktime value by a nanosecond value
 */
s64 __ktime_divns(const ktime_t kt, s64 div)
{
	int sft = 0;
	s64 dclc;
	u64 tmp;

	dclc = ktime_to_ns(kt);
	tmp = dclc < 0 ? -dclc : dclc;

	/* Make sure the divisor is less than 2^32: */
	while (div >> 32) {
		sft++;
		div >>= 1;
	}
	tmp >>= sft;
	do_div(tmp, (u32) div);
	return dclc < 0 ? -tmp : tmp;
}
EXPORT_SYMBOL_GPL(__ktime_divns);
#endif /* BITS_PER_LONG >= 64 */

/*
 * Add two ktime values and do a safety check for overflow:
 */
ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res = ktime_add_unsafe(lhs, rhs);

	/*
	 * We use KTIME_SEC_MAX here, the maximum timeout which we can
	 * return to user space in a timespec:
	 */
	if (res < 0 || res < lhs || res < rhs)
		res = ktime_set(KTIME_SEC_MAX, 0);

	return res;
}

EXPORT_SYMBOL_GPL(ktime_add_safe);

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS

static const struct debug_obj_descr hrtimer_debug_descr;

static void *hrtimer_debug_hint(void *addr)
{
	return ((struct hrtimer *) addr)->function;
}

/*
 * fixup_init is called when:
 * - an active object is initialized
 */
static bool hrtimer_fixup_init(void *addr, enum debug_obj_state state)
{
	struct hrtimer *timer = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		hrtimer_cancel(timer);
		debug_object_init(timer, &hrtimer_debug_descr);
		return true;
	default:
		return false;
	}
}

/*
 * fixup_activate is called when:
 * - an active object is activated
 * - an unknown non-static object is activated
 */
static bool hrtimer_fixup_activate(void *addr, enum debug_obj_state state)
{
	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		WARN_ON(1);
		fallthrough;
	default:
		return false;
	}
}

/*
 * fixup_free is called when:
 * - an active object is freed
 */
static bool hrtimer_fixup_free(void *addr, enum debug_obj_state state)
{
	struct hrtimer *timer = addr;

	switch (state) {
	case ODEBUG_STATE_ACTIVE:
		hrtimer_cancel(timer);
		debug_object_free(timer, &hrtimer_debug_descr);
		return true;
	default:
		return false;
	}
}

static const struct debug_obj_descr hrtimer_debug_descr = {
	.name		= "hrtimer",
	.debug_hint	= hrtimer_debug_hint,
	.fixup_init	= hrtimer_fixup_init,
	.fixup_activate	= hrtimer_fixup_activate,
	.fixup_free	= hrtimer_fixup_free,
};

static inline void debug_hrtimer_init(struct hrtimer *timer)
{
	debug_object_init(timer, &hrtimer_debug_descr);
}

static inline void debug_hrtimer_activate(struct hrtimer *timer,
					  enum hrtimer_mode mode)
{
	debug_object_activate(timer, &hrtimer_debug_descr);
}

static inline void debug_hrtimer_deactivate(struct hrtimer *timer)
{
	debug_object_deactivate(timer, &hrtimer_debug_descr);
}

static void __hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode);

void hrtimer_init_on_stack(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	debug_object_init_on_stack(timer, &hrtimer_debug_descr);
	__hrtimer_init(timer, clock_id, mode);
}
EXPORT_SYMBOL_GPL(hrtimer_init_on_stack);

static void __hrtimer_init_sleeper(struct hrtimer_sleeper *sl,
				   clockid_t clock_id, enum hrtimer_mode mode);

void hrtimer_init_sleeper_on_stack(struct hrtimer_sleeper *sl,
				   clockid_t clock_id, enum hrtimer_mode mode)
{
	debug_object_init_on_stack(&sl->timer, &hrtimer_debug_descr);
	__hrtimer_init_sleeper(sl, clock_id, mode);
}
EXPORT_SYMBOL_GPL(hrtimer_init_sleeper_on_stack);

void destroy_hrtimer_on_stack(struct hrtimer *timer)
{
	debug_object_free(timer, &hrtimer_debug_descr);
}
EXPORT_SYMBOL_GPL(destroy_hrtimer_on_stack);

#else

static inline void debug_hrtimer_init(struct hrtimer *timer) { }
static inline void debug_hrtimer_activate(struct hrtimer *timer,
					  enum hrtimer_mode mode) { }
static inline void debug_hrtimer_deactivate(struct hrtimer *timer) { }
#endif

static inline void
debug_init(struct hrtimer *timer, clockid_t clockid,
	   enum hrtimer_mode mode)
{
	debug_hrtimer_init(timer);
	trace_hrtimer_init(timer, clockid, mode);
}

static inline void debug_activate(struct hrtimer *timer,
				  enum hrtimer_mode mode)
{
	debug_hrtimer_activate(timer, mode);
	trace_hrtimer_start(timer, mode);
}

static inline void debug_deactivate(struct hrtimer *timer)
{
	debug_hrtimer_deactivate(timer);
	trace_hrtimer_cancel(timer);
}

static struct hrtimer_clock_base *
__next_base(struct hrtimer_cpu_base *cpu_base, unsigned int *active)
{
	unsigned int idx;

	if (!*active)
		return NULL;

	idx = __ffs(*active);
	*active &= ~(1U << idx);

	return &cpu_base->clock_base[idx];
}

#define for_each_active_base(base, cpu_base, active)	\
	while ((base = __next_base((cpu_base), &(active))))

static ktime_t __hrtimer_next_event_base(struct hrtimer_cpu_base *cpu_base,
					 const struct hrtimer *exclude,
					 unsigned int active,
					 ktime_t expires_next)
{
	struct hrtimer_clock_base *base;
	ktime_t expires;

	for_each_active_base(base, cpu_base, active) {
		struct timerqueue_node *next;
		struct hrtimer *timer;

		next = timerqueue_getnext(&base->active);
		timer = container_of(next, struct hrtimer, node);
		if (timer == exclude) {
			/* Get to the next timer in the queue. */
			next = timerqueue_iterate_next(next);
			if (!next)
				continue;

			timer = container_of(next, struct hrtimer, node);
		}
		expires = ktime_sub(hrtimer_get_expires(timer), base->offset);
		if (expires < expires_next) {
			expires_next = expires;

			/* Skip cpu_base update if a timer is being excluded. */
			if (exclude)
				continue;

			if (timer->is_soft)
				cpu_base->softirq_next_timer = timer;
			else
				cpu_base->next_timer = timer;
		}
	}
	/*
	 * clock_was_set() might have changed base->offset of any of
	 * the clock bases so the result might be negative. Fix it up
	 * to prevent a false positive in clockevents_program_event().
	 */
	if (expires_next < 0)
		expires_next = 0;
	return expires_next;
}

/*
 * Recomputes cpu_base::*next_timer and returns the earliest expires_next
 * but does not set cpu_base::*expires_next, that is done by
 * hrtimer[_force]_reprogram and hrtimer_interrupt only. When updating
 * cpu_base::*expires_next right away, reprogramming logic would no longer
 * work.
 *
 * When a softirq is pending, we can ignore the HRTIMER_ACTIVE_SOFT bases,
 * those timers will get run whenever the softirq gets handled, at the end of
 * hrtimer_run_softirq(), hrtimer_update_softirq_timer() will re-add these bases.
 *
 * Therefore softirq values are those from the HRTIMER_ACTIVE_SOFT clock bases.
 * The !softirq values are the minima across HRTIMER_ACTIVE_ALL, unless an actual
 * softirq is pending, in which case they're the minima of HRTIMER_ACTIVE_HARD.
 *
 * @active_mask must be one of:
 *  - HRTIMER_ACTIVE_ALL,
 *  - HRTIMER_ACTIVE_SOFT, or
 *  - HRTIMER_ACTIVE_HARD.
 */
static ktime_t
__hrtimer_get_next_event(struct hrtimer_cpu_base *cpu_base, unsigned int active_mask)
{
	unsigned int active;
	struct hrtimer *next_timer = NULL;
	ktime_t expires_next = KTIME_MAX;

	if (!cpu_base->softirq_activated && (active_mask & HRTIMER_ACTIVE_SOFT)) {
		active = cpu_base->active_bases & HRTIMER_ACTIVE_SOFT;
		cpu_base->softirq_next_timer = NULL;
		expires_next = __hrtimer_next_event_base(cpu_base, NULL,
							 active, KTIME_MAX);

		next_timer = cpu_base->softirq_next_timer;
	}

	if (active_mask & HRTIMER_ACTIVE_HARD) {
		active = cpu_base->active_bases & HRTIMER_ACTIVE_HARD;
		cpu_base->next_timer = next_timer;
		expires_next = __hrtimer_next_event_base(cpu_base, NULL, active,
							 expires_next);
	}

	return expires_next;
}

static ktime_t hrtimer_update_next_event(struct hrtimer_cpu_base *cpu_base)
{
	ktime_t expires_next, soft = KTIME_MAX;

	/*
	 * If the soft interrupt has already been activated, ignore the
	 * soft bases. They will be handled in the already raised soft
	 * interrupt.
	 */
	if (!cpu_base->softirq_activated) {
		soft = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_SOFT);
		/*
		 * Update the soft expiry time. clock_settime() might have
		 * affected it.
		 */
		cpu_base->softirq_expires_next = soft;
	}

	expires_next = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_HARD);
	/*
	 * If a softirq timer is expiring first, update cpu_base->next_timer
	 * and program the hardware with the soft expiry time.
	 */
	if (expires_next > soft) {
		cpu_base->next_timer = cpu_base->softirq_next_timer;
		expires_next = soft;
	}

	return expires_next;
}

static inline ktime_t hrtimer_update_base(struct hrtimer_cpu_base *base)
{
	ktime_t *offs_real = &base->clock_base[HRTIMER_BASE_REALTIME].offset;
	ktime_t *offs_boot = &base->clock_base[HRTIMER_BASE_BOOTTIME].offset;
	ktime_t *offs_tai = &base->clock_base[HRTIMER_BASE_TAI].offset;

	ktime_t now = ktime_get_update_offsets_now(&base->clock_was_set_seq,
					    offs_real, offs_boot, offs_tai);

	base->clock_base[HRTIMER_BASE_REALTIME_SOFT].offset = *offs_real;
	base->clock_base[HRTIMER_BASE_BOOTTIME_SOFT].offset = *offs_boot;
	base->clock_base[HRTIMER_BASE_TAI_SOFT].offset = *offs_tai;

	return now;
}

/*
 * Is the high resolution mode active ?
 */
static inline int __hrtimer_hres_active(struct hrtimer_cpu_base *cpu_base)
{
	return IS_ENABLED(CONFIG_HIGH_RES_TIMERS) ?
		cpu_base->hres_active : 0;
}

static inline int hrtimer_hres_active(void)
{
	return __hrtimer_hres_active(this_cpu_ptr(&hrtimer_bases));
}

static void __hrtimer_reprogram(struct hrtimer_cpu_base *cpu_base,
				struct hrtimer *next_timer,
				ktime_t expires_next)
{
	cpu_base->expires_next = expires_next;

	/*
	 * If hres is not active, hardware does not have to be
	 * reprogrammed yet.
	 *
	 * If a hang was detected in the last timer interrupt then we
	 * leave the hang delay active in the hardware. We want the
	 * system to make progress. That also prevents the following
	 * scenario:
	 * T1 expires 50ms from now
	 * T2 expires 5s from now
	 *
	 * T1 is removed, so this code is called and would reprogram
	 * the hardware to 5s from now. Any hrtimer_start after that
	 * will not reprogram the hardware due to hang_detected being
	 * set. So we'd effectively block all timers until the T2 event
	 * fires.
	 */
	if (!__hrtimer_hres_active(cpu_base) || cpu_base->hang_detected)
		return;

	tick_program_event(expires_next, 1);
}

/*
 * Reprogram the event source with checking both queues for the
 * next event
 * Called with interrupts disabled and base->lock held
 */
static void
hrtimer_force_reprogram(struct hrtimer_cpu_base *cpu_base, int skip_equal)
{
	ktime_t expires_next;

	expires_next = hrtimer_update_next_event(cpu_base);

	if (skip_equal && expires_next == cpu_base->expires_next)
		return;

	__hrtimer_reprogram(cpu_base, cpu_base->next_timer, expires_next);
}

/* High resolution timer related functions */
#ifdef CONFIG_HIGH_RES_TIMERS

/*
 * High resolution timer enabled ?
 */
static bool hrtimer_hres_enabled __read_mostly  = true;
unsigned int hrtimer_resolution __read_mostly = LOW_RES_NSEC;
EXPORT_SYMBOL_GPL(hrtimer_resolution);

/*
 * Enable / Disable high resolution mode
 */
static int __init setup_hrtimer_hres(char *str)
{
	return (kstrtobool(str, &hrtimer_hres_enabled) == 0);
}

__setup("highres=", setup_hrtimer_hres);

/*
 * hrtimer_high_res_enabled - query, if the highres mode is enabled
 */
static inline int hrtimer_is_hres_enabled(void)
{
	return hrtimer_hres_enabled;
}

static void retrigger_next_event(void *arg);

/*
 * Switch to high resolution mode
 */
static void hrtimer_switch_to_hres(void)
{
	struct hrtimer_cpu_base *base = this_cpu_ptr(&hrtimer_bases);

	if (tick_init_highres()) {
		pr_warn("Could not switch to high resolution mode on CPU %u\n",
			base->cpu);
		return;
	}
	base->hres_active = 1;
	hrtimer_resolution = HIGH_RES_NSEC;

	tick_setup_sched_timer();
	/* "Retrigger" the interrupt to get things going */
	retrigger_next_event(NULL);
}

#else

static inline int hrtimer_is_hres_enabled(void) { return 0; }
static inline void hrtimer_switch_to_hres(void) { }

#endif /* CONFIG_HIGH_RES_TIMERS */
/*
 * Retrigger next event is called after clock was set with interrupts
 * disabled through an SMP function call or directly from low level
 * resume code.
 *
 * This is only invoked when:
 *	- CONFIG_HIGH_RES_TIMERS is enabled.
 *	- CONFIG_NOHZ_COMMON is enabled
 *
 * For the other cases this function is empty and because the call sites
 * are optimized out it vanishes as well, i.e. no need for lots of
 * #ifdeffery.
 */
static void retrigger_next_event(void *arg)
{
	struct hrtimer_cpu_base *base = this_cpu_ptr(&hrtimer_bases);

	/*
	 * When high resolution mode or nohz is active, then the offsets of
	 * CLOCK_REALTIME/TAI/BOOTTIME have to be updated. Otherwise the
	 * next tick will take care of that.
	 *
	 * If high resolution mode is active then the next expiring timer
	 * must be reevaluated and the clock event device reprogrammed if
	 * necessary.
	 *
	 * In the NOHZ case the update of the offset and the reevaluation
	 * of the next expiring timer is enough. The return from the SMP
	 * function call will take care of the reprogramming in case the
	 * CPU was in a NOHZ idle sleep.
	 */
	if (!__hrtimer_hres_active(base) && !tick_nohz_active)
		return;

	raw_spin_lock(&base->lock);
	hrtimer_update_base(base);
	if (__hrtimer_hres_active(base))
		hrtimer_force_reprogram(base, 0);
	else
		hrtimer_update_next_event(base);
	raw_spin_unlock(&base->lock);
}

/*
 * When a timer is enqueued and expires earlier than the already enqueued
 * timers, we have to check, whether it expires earlier than the timer for
 * which the clock event device was armed.
 *
 * Called with interrupts disabled and base->cpu_base.lock held
 */
static void hrtimer_reprogram(struct hrtimer *timer, bool reprogram)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	struct hrtimer_clock_base *base = timer->base;
	ktime_t expires = ktime_sub(hrtimer_get_expires(timer), base->offset);

	WARN_ON_ONCE(hrtimer_get_expires_tv64(timer) < 0);

	/*
	 * CLOCK_REALTIME timer might be requested with an absolute
	 * expiry time which is less than base->offset. Set it to 0.
	 */
	if (expires < 0)
		expires = 0;

	if (timer->is_soft) {
		/*
		 * soft hrtimer could be started on a remote CPU. In this
		 * case softirq_expires_next needs to be updated on the
		 * remote CPU. The soft hrtimer will not expire before the
		 * first hard hrtimer on the remote CPU -
		 * hrtimer_check_target() prevents this case.
		 */
		struct hrtimer_cpu_base *timer_cpu_base = base->cpu_base;

		if (timer_cpu_base->softirq_activated)
			return;

		if (!ktime_before(expires, timer_cpu_base->softirq_expires_next))
			return;

		timer_cpu_base->softirq_next_timer = timer;
		timer_cpu_base->softirq_expires_next = expires;

		if (!ktime_before(expires, timer_cpu_base->expires_next) ||
		    !reprogram)
			return;
	}

	/*
	 * If the timer is not on the current cpu, we cannot reprogram
	 * the other cpus clock event device.
	 */
	if (base->cpu_base != cpu_base)
		return;

	if (expires >= cpu_base->expires_next)
		return;

	/*
	 * If the hrtimer interrupt is running, then it will reevaluate the
	 * clock bases and reprogram the clock event device.
	 */
	if (cpu_base->in_hrtirq)
		return;

	cpu_base->next_timer = timer;

	__hrtimer_reprogram(cpu_base, timer, expires);
}

static bool update_needs_ipi(struct hrtimer_cpu_base *cpu_base,
			     unsigned int active)
{
	struct hrtimer_clock_base *base;
	unsigned int seq;
	ktime_t expires;

	/*
	 * Update the base offsets unconditionally so the following
	 * checks whether the SMP function call is required works.
	 *
	 * The update is safe even when the remote CPU is in the hrtimer
	 * interrupt or the hrtimer soft interrupt and expiring affected
	 * bases. Either it will see the update before handling a base or
	 * it will see it when it finishes the processing and reevaluates
	 * the next expiring timer.
	 */
	seq = cpu_base->clock_was_set_seq;
	hrtimer_update_base(cpu_base);

	/*
	 * If the sequence did not change over the update then the
	 * remote CPU already handled it.
	 */
	if (seq == cpu_base->clock_was_set_seq)
		return false;

	/*
	 * If the remote CPU is currently handling an hrtimer interrupt, it
	 * will reevaluate the first expiring timer of all clock bases
	 * before reprogramming. Nothing to do here.
	 */
	if (cpu_base->in_hrtirq)
		return false;

	/*
	 * Walk the affected clock bases and check whether the first expiring
	 * timer in a clock base is moving ahead of the first expiring timer of
	 * @cpu_base. If so, the IPI must be invoked because per CPU clock
	 * event devices cannot be remotely reprogrammed.
	 */
	active &= cpu_base->active_bases;

	for_each_active_base(base, cpu_base, active) {
		struct timerqueue_node *next;

		next = timerqueue_getnext(&base->active);
		expires = ktime_sub(next->expires, base->offset);
		if (expires < cpu_base->expires_next)
			return true;

		/* Extra check for softirq clock bases */
		if (base->clockid < HRTIMER_BASE_MONOTONIC_SOFT)
			continue;
		if (cpu_base->softirq_activated)
			continue;
		if (expires < cpu_base->softirq_expires_next)
			return true;
	}
	return false;
}

/*
 * Clock was set. This might affect CLOCK_REALTIME, CLOCK_TAI and
 * CLOCK_BOOTTIME (for late sleep time injection).
 *
 * This requires to update the offsets for these clocks
 * vs. CLOCK_MONOTONIC. When high resolution timers are enabled, then this
 * also requires to eventually reprogram the per CPU clock event devices
 * when the change moves an affected timer ahead of the first expiring
 * timer on that CPU. Obviously remote per CPU clock event devices cannot
 * be reprogrammed. The other reason why an IPI has to be sent is when the
 * system is in !HIGH_RES and NOHZ mode. The NOHZ mode updates the offsets
 * in the tick, which obviously might be stopped, so this has to bring out
 * the remote CPU which might sleep in idle to get this sorted.
 */
void clock_was_set(unsigned int bases)
{
	struct hrtimer_cpu_base *cpu_base = raw_cpu_ptr(&hrtimer_bases);
	cpumask_var_t mask;
	int cpu;

	if (!__hrtimer_hres_active(cpu_base) && !tick_nohz_active)
		goto out_timerfd;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		on_each_cpu(retrigger_next_event, NULL, 1);
		goto out_timerfd;
	}

	/* Avoid interrupting CPUs if possible */
	cpus_read_lock();
	for_each_online_cpu(cpu) {
		unsigned long flags;

		cpu_base = &per_cpu(hrtimer_bases, cpu);
		raw_spin_lock_irqsave(&cpu_base->lock, flags);

		if (update_needs_ipi(cpu_base, bases))
			cpumask_set_cpu(cpu, mask);

		raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	}

	preempt_disable();
	smp_call_function_many(mask, retrigger_next_event, NULL, 1);
	preempt_enable();
	cpus_read_unlock();
	free_cpumask_var(mask);

out_timerfd:
	timerfd_clock_was_set();
}

static void clock_was_set_work(struct work_struct *work)
{
	clock_was_set(CLOCK_SET_WALL);
}

static DECLARE_WORK(hrtimer_work, clock_was_set_work);

/*
 * Called from timekeeping code to reprogram the hrtimer interrupt device
 * on all cpus and to notify timerfd.
 */
void clock_was_set_delayed(void)
{
	schedule_work(&hrtimer_work);
}

/*
 * Called during resume either directly from via timekeeping_resume()
 * or in the case of s2idle from tick_unfreeze() to ensure that the
 * hrtimers are up to date.
 */
void hrtimers_resume_local(void)
{
	lockdep_assert_irqs_disabled();
	/* Retrigger on the local CPU */
	retrigger_next_event(NULL);
}

/*
 * Counterpart to lock_hrtimer_base above:
 */
static inline
void unlock_hrtimer_base(const struct hrtimer *timer, unsigned long *flags)
{
	raw_spin_unlock_irqrestore(&timer->base->cpu_base->lock, *flags);
}

/**
 * hrtimer_forward - forward the timer expiry
 * @timer:	hrtimer to forward
 * @now:	forward past this time
 * @interval:	the interval to forward
 *
 * Forward the timer expiry so it will expire in the future.
 * Returns the number of overruns.
 *
 * Can be safely called from the callback function of @timer. If
 * called from other contexts @timer must neither be enqueued nor
 * running the callback and the caller needs to take care of
 * serialization.
 *
 * Note: This only updates the timer expiry value and does not requeue
 * the timer.
 */
u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval)
{
	u64 orun = 1;
	ktime_t delta;

	delta = ktime_sub(now, hrtimer_get_expires(timer));

	if (delta < 0)
		return 0;

	if (WARN_ON(timer->state & HRTIMER_STATE_ENQUEUED))
		return 0;

	if (interval < hrtimer_resolution)
		interval = hrtimer_resolution;

	if (unlikely(delta >= interval)) {
		s64 incr = ktime_to_ns(interval);

		orun = ktime_divns(delta, incr);
		hrtimer_add_expires_ns(timer, incr * orun);
		if (hrtimer_get_expires_tv64(timer) > now)
			return orun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		orun++;
	}
	hrtimer_add_expires(timer, interval);

	return orun;
}
EXPORT_SYMBOL_GPL(hrtimer_forward);

/*
 * enqueue_hrtimer - internal function to (re)start a timer
 *
 * The timer is inserted in expiry order. Insertion into the
 * red black tree is O(log(n)). Must hold the base lock.
 *
 * Returns 1 when the new timer is the leftmost timer in the tree.
 */
static int enqueue_hrtimer(struct hrtimer *timer,
			   struct hrtimer_clock_base *base,
			   enum hrtimer_mode mode)
{
	debug_activate(timer, mode);

	base->cpu_base->active_bases |= 1 << base->index;

	/* Pairs with the lockless read in hrtimer_is_queued() */
	WRITE_ONCE(timer->state, HRTIMER_STATE_ENQUEUED);

	return timerqueue_add(&base->active, &timer->node);
}

/*
 * __remove_hrtimer - internal function to remove a timer
 *
 * Caller must hold the base lock.
 *
 * High resolution timer mode reprograms the clock event device when the
 * timer is the one which expires next. The caller can disable this by setting
 * reprogram to zero. This is useful, when the context does a reprogramming
 * anyway (e.g. timer interrupt)
 */
static void __remove_hrtimer(struct hrtimer *timer,
			     struct hrtimer_clock_base *base,
			     u8 newstate, int reprogram)
{
	struct hrtimer_cpu_base *cpu_base = base->cpu_base;
	u8 state = timer->state;

	/* Pairs with the lockless read in hrtimer_is_queued() */
	WRITE_ONCE(timer->state, newstate);
	if (!(state & HRTIMER_STATE_ENQUEUED))
		return;

	if (!timerqueue_del(&base->active, &timer->node))
		cpu_base->active_bases &= ~(1 << base->index);

	/*
	 * Note: If reprogram is false we do not update
	 * cpu_base->next_timer. This happens when we remove the first
	 * timer on a remote cpu. No harm as we never dereference
	 * cpu_base->next_timer. So the worst thing what can happen is
	 * an superfluous call to hrtimer_force_reprogram() on the
	 * remote cpu later on if the same timer gets enqueued again.
	 */
	if (reprogram && timer == cpu_base->next_timer)
		hrtimer_force_reprogram(cpu_base, 1);
}

/*
 * remove hrtimer, called with base lock held
 */
static inline int
remove_hrtimer(struct hrtimer *timer, struct hrtimer_clock_base *base,
	       bool restart, bool keep_local)
{
	u8 state = timer->state;

	if (state & HRTIMER_STATE_ENQUEUED) {
		bool reprogram;

		/*
		 * Remove the timer and force reprogramming when high
		 * resolution mode is active and the timer is on the current
		 * CPU. If we remove a timer on another CPU, reprogramming is
		 * skipped. The interrupt event on this CPU is fired and
		 * reprogramming happens in the interrupt handler. This is a
		 * rare case and less expensive than a smp call.
		 */
		debug_deactivate(timer);
		reprogram = base->cpu_base == this_cpu_ptr(&hrtimer_bases);

		/*
		 * If the timer is not restarted then reprogramming is
		 * required if the timer is local. If it is local and about
		 * to be restarted, avoid programming it twice (on removal
		 * and a moment later when it's requeued).
		 */
		if (!restart)
			state = HRTIMER_STATE_INACTIVE;
		else
			reprogram &= !keep_local;

		__remove_hrtimer(timer, base, state, reprogram);
		return 1;
	}
	return 0;
}

static inline ktime_t hrtimer_update_lowres(struct hrtimer *timer, ktime_t tim,
					    const enum hrtimer_mode mode)
{
#ifdef CONFIG_TIME_LOW_RES
	/*
	 * CONFIG_TIME_LOW_RES indicates that the system has no way to return
	 * granular time values. For relative timers we add hrtimer_resolution
	 * (i.e. one jiffie) to prevent short timeouts.
	 */
	timer->is_rel = mode & HRTIMER_MODE_REL;
	if (timer->is_rel)
		tim = ktime_add_safe(tim, hrtimer_resolution);
#endif
	return tim;
}

static void
hrtimer_update_softirq_timer(struct hrtimer_cpu_base *cpu_base, bool reprogram)
{
	ktime_t expires;

	/*
	 * Find the next SOFT expiration.
	 */
	expires = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_SOFT);

	/*
	 * reprogramming needs to be triggered, even if the next soft
	 * hrtimer expires at the same time than the next hard
	 * hrtimer. cpu_base->softirq_expires_next needs to be updated!
	 */
	if (expires == KTIME_MAX)
		return;

	/*
	 * cpu_base->*next_timer is recomputed by __hrtimer_get_next_event()
	 * cpu_base->*expires_next is only set by hrtimer_reprogram()
	 */
	hrtimer_reprogram(cpu_base->softirq_next_timer, reprogram);
}

static int __hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
				    u64 delta_ns, const enum hrtimer_mode mode,
				    struct hrtimer_clock_base *base)
{
	struct hrtimer_clock_base *new_base;
	bool force_local, first;

	/*
	 * If the timer is on the local cpu base and is the first expiring
	 * timer then this might end up reprogramming the hardware twice
	 * (on removal and on enqueue). To avoid that by prevent the
	 * reprogram on removal, keep the timer local to the current CPU
	 * and enforce reprogramming after it is queued no matter whether
	 * it is the new first expiring timer again or not.
	 */
	force_local = base->cpu_base == this_cpu_ptr(&hrtimer_bases);
	force_local &= base->cpu_base->next_timer == timer;

	/*
	 * Remove an active timer from the queue. In case it is not queued
	 * on the current CPU, make sure that remove_hrtimer() updates the
	 * remote data correctly.
	 *
	 * If it's on the current CPU and the first expiring timer, then
	 * skip reprogramming, keep the timer local and enforce
	 * reprogramming later if it was the first expiring timer.  This
	 * avoids programming the underlying clock event twice (once at
	 * removal and once after enqueue).
	 */
	remove_hrtimer(timer, base, true, force_local);

	if (mode & HRTIMER_MODE_REL)
		tim = ktime_add_safe(tim, base->get_time());

	tim = hrtimer_update_lowres(timer, tim, mode);

	hrtimer_set_expires_range_ns(timer, tim, delta_ns);

	/* Switch the timer base, if necessary: */
	if (!force_local) {
		new_base = switch_hrtimer_base(timer, base,
					       mode & HRTIMER_MODE_PINNED);
	} else {
		new_base = base;
	}

	first = enqueue_hrtimer(timer, new_base, mode);
	if (!force_local)
		return first;

	/*
	 * Timer was forced to stay on the current CPU to avoid
	 * reprogramming on removal and enqueue. Force reprogram the
	 * hardware by evaluating the new first expiring timer.
	 */
	hrtimer_force_reprogram(new_base->cpu_base, 1);
	return 0;
}

/**
 * hrtimer_start_range_ns - (re)start an hrtimer
 * @timer:	the timer to be added
 * @tim:	expiry time
 * @delta_ns:	"slack" range for the timer
 * @mode:	timer mode: absolute (HRTIMER_MODE_ABS) or
 *		relative (HRTIMER_MODE_REL), and pinned (HRTIMER_MODE_PINNED);
 *		softirq based mode is considered for debug purpose only!
 */
void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
			    u64 delta_ns, const enum hrtimer_mode mode)
{
	struct hrtimer_clock_base *base;
	unsigned long flags;

	/*
	 * Check whether the HRTIMER_MODE_SOFT bit and hrtimer.is_soft
	 * match on CONFIG_PREEMPT_RT = n. With PREEMPT_RT check the hard
	 * expiry mode because unmarked timers are moved to softirq expiry.
	 */
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		WARN_ON_ONCE(!(mode & HRTIMER_MODE_SOFT) ^ !timer->is_soft);
	else
		WARN_ON_ONCE(!(mode & HRTIMER_MODE_HARD) ^ !timer->is_hard);

	base = lock_hrtimer_base(timer, &flags);

	if (__hrtimer_start_range_ns(timer, tim, delta_ns, mode, base))
		hrtimer_reprogram(timer, true);

	unlock_hrtimer_base(timer, &flags);
}
EXPORT_SYMBOL_GPL(hrtimer_start_range_ns);

/**
 * hrtimer_try_to_cancel - try to deactivate a timer
 * @timer:	hrtimer to stop
 *
 * Returns:
 *
 *  *  0 when the timer was not active
 *  *  1 when the timer was active
 *  * -1 when the timer is currently executing the callback function and
 *    cannot be stopped
 */
int hrtimer_try_to_cancel(struct hrtimer *timer)
{
	struct hrtimer_clock_base *base;
	unsigned long flags;
	int ret = -1;

	/*
	 * Check lockless first. If the timer is not active (neither
	 * enqueued nor running the callback, nothing to do here.  The
	 * base lock does not serialize against a concurrent enqueue,
	 * so we can avoid taking it.
	 */
	if (!hrtimer_active(timer))
		return 0;

	base = lock_hrtimer_base(timer, &flags);

	if (!hrtimer_callback_running(timer))
		ret = remove_hrtimer(timer, base, false, false);

	unlock_hrtimer_base(timer, &flags);

	return ret;

}
EXPORT_SYMBOL_GPL(hrtimer_try_to_cancel);

#ifdef CONFIG_PREEMPT_RT
static void hrtimer_cpu_base_init_expiry_lock(struct hrtimer_cpu_base *base)
{
	spin_lock_init(&base->softirq_expiry_lock);
}

static void hrtimer_cpu_base_lock_expiry(struct hrtimer_cpu_base *base)
{
	spin_lock(&base->softirq_expiry_lock);
}

static void hrtimer_cpu_base_unlock_expiry(struct hrtimer_cpu_base *base)
{
	spin_unlock(&base->softirq_expiry_lock);
}

/*
 * The counterpart to hrtimer_cancel_wait_running().
 *
 * If there is a waiter for cpu_base->expiry_lock, then it was waiting for
 * the timer callback to finish. Drop expiry_lock and reacquire it. That
 * allows the waiter to acquire the lock and make progress.
 */
static void hrtimer_sync_wait_running(struct hrtimer_cpu_base *cpu_base,
				      unsigned long flags)
{
	if (atomic_read(&cpu_base->timer_waiters)) {
		raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
		spin_unlock(&cpu_base->softirq_expiry_lock);
		spin_lock(&cpu_base->softirq_expiry_lock);
		raw_spin_lock_irq(&cpu_base->lock);
	}
}

/*
 * This function is called on PREEMPT_RT kernels when the fast path
 * deletion of a timer failed because the timer callback function was
 * running.
 *
 * This prevents priority inversion: if the soft irq thread is preempted
 * in the middle of a timer callback, then calling del_timer_sync() can
 * lead to two issues:
 *
 *  - If the caller is on a remote CPU then it has to spin wait for the timer
 *    handler to complete. This can result in unbound priority inversion.
 *
 *  - If the caller originates from the task which preempted the timer
 *    handler on the same CPU, then spin waiting for the timer handler to
 *    complete is never going to end.
 */
void hrtimer_cancel_wait_running(const struct hrtimer *timer)
{
	/* Lockless read. Prevent the compiler from reloading it below */
	struct hrtimer_clock_base *base = READ_ONCE(timer->base);

	/*
	 * Just relax if the timer expires in hard interrupt context or if
	 * it is currently on the migration base.
	 */
	if (!timer->is_soft || is_migration_base(base)) {
		cpu_relax();
		return;
	}

	/*
	 * Mark the base as contended and grab the expiry lock, which is
	 * held by the softirq across the timer callback. Drop the lock
	 * immediately so the softirq can expire the next timer. In theory
	 * the timer could already be running again, but that's more than
	 * unlikely and just causes another wait loop.
	 */
	atomic_inc(&base->cpu_base->timer_waiters);
	spin_lock_bh(&base->cpu_base->softirq_expiry_lock);
	atomic_dec(&base->cpu_base->timer_waiters);
	spin_unlock_bh(&base->cpu_base->softirq_expiry_lock);
}
#else
static inline void
hrtimer_cpu_base_init_expiry_lock(struct hrtimer_cpu_base *base) { }
static inline void
hrtimer_cpu_base_lock_expiry(struct hrtimer_cpu_base *base) { }
static inline void
hrtimer_cpu_base_unlock_expiry(struct hrtimer_cpu_base *base) { }
static inline void hrtimer_sync_wait_running(struct hrtimer_cpu_base *base,
					     unsigned long flags) { }
#endif

/**
 * hrtimer_cancel - cancel a timer and wait for the handler to finish.
 * @timer:	the timer to be cancelled
 *
 * Returns:
 *  0 when the timer was not active
 *  1 when the timer was active
 */
int hrtimer_cancel(struct hrtimer *timer)
{
	int ret;

	do {
		ret = hrtimer_try_to_cancel(timer);

		if (ret < 0)
			hrtimer_cancel_wait_running(timer);
	} while (ret < 0);
	return ret;
}
EXPORT_SYMBOL_GPL(hrtimer_cancel);

/**
 * __hrtimer_get_remaining - get remaining time for the timer
 * @timer:	the timer to read
 * @adjust:	adjust relative timers when CONFIG_TIME_LOW_RES=y
 */
ktime_t __hrtimer_get_remaining(const struct hrtimer *timer, bool adjust)
{
	unsigned long flags;
	ktime_t rem;

	lock_hrtimer_base(timer, &flags);
	if (IS_ENABLED(CONFIG_TIME_LOW_RES) && adjust)
		rem = hrtimer_expires_remaining_adjusted(timer);
	else
		rem = hrtimer_expires_remaining(timer);
	unlock_hrtimer_base(timer, &flags);

	return rem;
}
EXPORT_SYMBOL_GPL(__hrtimer_get_remaining);

#ifdef CONFIG_NO_HZ_COMMON
/**
 * hrtimer_get_next_event - get the time until next expiry event
 *
 * Returns the next expiry time or KTIME_MAX if no timer is pending.
 */
u64 hrtimer_get_next_event(void)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	u64 expires = KTIME_MAX;
	unsigned long flags;

	raw_spin_lock_irqsave(&cpu_base->lock, flags);

	if (!__hrtimer_hres_active(cpu_base))
		expires = __hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_ALL);

	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	return expires;
}

/**
 * hrtimer_next_event_without - time until next expiry event w/o one timer
 * @exclude:	timer to exclude
 *
 * Returns the next expiry time over all timers except for the @exclude one or
 * KTIME_MAX if none of them is pending.
 */
u64 hrtimer_next_event_without(const struct hrtimer *exclude)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	u64 expires = KTIME_MAX;
	unsigned long flags;

	raw_spin_lock_irqsave(&cpu_base->lock, flags);

	if (__hrtimer_hres_active(cpu_base)) {
		unsigned int active;

		if (!cpu_base->softirq_activated) {
			active = cpu_base->active_bases & HRTIMER_ACTIVE_SOFT;
			expires = __hrtimer_next_event_base(cpu_base, exclude,
							    active, KTIME_MAX);
		}
		active = cpu_base->active_bases & HRTIMER_ACTIVE_HARD;
		expires = __hrtimer_next_event_base(cpu_base, exclude, active,
						    expires);
	}

	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	return expires;
}
#endif

static inline int hrtimer_clockid_to_base(clockid_t clock_id)
{
	if (likely(clock_id < MAX_CLOCKS)) {
		int base = hrtimer_clock_to_base_table[clock_id];

		if (likely(base != HRTIMER_MAX_CLOCK_BASES))
			return base;
	}
	WARN(1, "Invalid clockid %d. Using MONOTONIC\n", clock_id);
	return HRTIMER_BASE_MONOTONIC;
}

static void __hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	bool softtimer = !!(mode & HRTIMER_MODE_SOFT);
	struct hrtimer_cpu_base *cpu_base;
	int base;

	/*
	 * On PREEMPT_RT enabled kernels hrtimers which are not explicitly
	 * marked for hard interrupt expiry mode are moved into soft
	 * interrupt context for latency reasons and because the callbacks
	 * can invoke functions which might sleep on RT, e.g. spin_lock().
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && !(mode & HRTIMER_MODE_HARD))
		softtimer = true;

	memset(timer, 0, sizeof(struct hrtimer));

	cpu_base = raw_cpu_ptr(&hrtimer_bases);

	/*
	 * POSIX magic: Relative CLOCK_REALTIME timers are not affected by
	 * clock modifications, so they needs to become CLOCK_MONOTONIC to
	 * ensure POSIX compliance.
	 */
	if (clock_id == CLOCK_REALTIME && mode & HRTIMER_MODE_REL)
		clock_id = CLOCK_MONOTONIC;

	base = softtimer ? HRTIMER_MAX_CLOCK_BASES / 2 : 0;
	base += hrtimer_clockid_to_base(clock_id);
	timer->is_soft = softtimer;
	timer->is_hard = !!(mode & HRTIMER_MODE_HARD);
	timer->base = &cpu_base->clock_base[base];
	timerqueue_init(&timer->node);
}

/**
 * hrtimer_init - initialize a timer to the given clock
 * @timer:	the timer to be initialized
 * @clock_id:	the clock to be used
 * @mode:       The modes which are relevant for initialization:
 *              HRTIMER_MODE_ABS, HRTIMER_MODE_REL, HRTIMER_MODE_ABS_SOFT,
 *              HRTIMER_MODE_REL_SOFT
 *
 *              The PINNED variants of the above can be handed in,
 *              but the PINNED bit is ignored as pinning happens
 *              when the hrtimer is started
 */
void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
		  enum hrtimer_mode mode)
{
	debug_init(timer, clock_id, mode);
	__hrtimer_init(timer, clock_id, mode);
}
EXPORT_SYMBOL_GPL(hrtimer_init);

/*
 * A timer is active, when it is enqueued into the rbtree or the
 * callback function is running or it's in the state of being migrated
 * to another cpu.
 *
 * It is important for this function to not return a false negative.
 */
bool hrtimer_active(const struct hrtimer *timer)
{
	struct hrtimer_clock_base *base;
	unsigned int seq;

	do {
		base = READ_ONCE(timer->base);
		seq = raw_read_seqcount_begin(&base->seq);

		if (timer->state != HRTIMER_STATE_INACTIVE ||
		    base->running == timer)
			return true;

	} while (read_seqcount_retry(&base->seq, seq) ||
		 base != READ_ONCE(timer->base));

	return false;
}
EXPORT_SYMBOL_GPL(hrtimer_active);

/*
 * The write_seqcount_barrier()s in __run_hrtimer() split the thing into 3
 * distinct sections:
 *
 *  - queued:	the timer is queued
 *  - callback:	the timer is being ran
 *  - post:	the timer is inactive or (re)queued
 *
 * On the read side we ensure we observe timer->state and cpu_base->running
 * from the same section, if anything changed while we looked at it, we retry.
 * This includes timer->base changing because sequence numbers alone are
 * insufficient for that.
 *
 * The sequence numbers are required because otherwise we could still observe
 * a false negative if the read side got smeared over multiple consecutive
 * __run_hrtimer() invocations.
 */

static void __run_hrtimer(struct hrtimer_cpu_base *cpu_base,
			  struct hrtimer_clock_base *base,
			  struct hrtimer *timer, ktime_t *now,
			  unsigned long flags) __must_hold(&cpu_base->lock)
{
	enum hrtimer_restart (*fn)(struct hrtimer *);
	bool expires_in_hardirq;
	int restart;

	lockdep_assert_held(&cpu_base->lock);

	debug_deactivate(timer);
	base->running = timer;

	/*
	 * Separate the ->running assignment from the ->state assignment.
	 *
	 * As with a regular write barrier, this ensures the read side in
	 * hrtimer_active() cannot observe base->running == NULL &&
	 * timer->state == INACTIVE.
	 */
	raw_write_seqcount_barrier(&base->seq);

	__remove_hrtimer(timer, base, HRTIMER_STATE_INACTIVE, 0);
	fn = timer->function;

	/*
	 * Clear the 'is relative' flag for the TIME_LOW_RES case. If the
	 * timer is restarted with a period then it becomes an absolute
	 * timer. If its not restarted it does not matter.
	 */
	if (IS_ENABLED(CONFIG_TIME_LOW_RES))
		timer->is_rel = false;

	/*
	 * The timer is marked as running in the CPU base, so it is
	 * protected against migration to a different CPU even if the lock
	 * is dropped.
	 */
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	trace_hrtimer_expire_entry(timer, now);
	expires_in_hardirq = lockdep_hrtimer_enter(timer);

	restart = fn(timer);

	lockdep_hrtimer_exit(expires_in_hardirq);
	trace_hrtimer_expire_exit(timer);
	raw_spin_lock_irq(&cpu_base->lock);

	/*
	 * Note: We clear the running state after enqueue_hrtimer and
	 * we do not reprogram the event hardware. Happens either in
	 * hrtimer_start_range_ns() or in hrtimer_interrupt()
	 *
	 * Note: Because we dropped the cpu_base->lock above,
	 * hrtimer_start_range_ns() can have popped in and enqueued the timer
	 * for us already.
	 */
	if (restart != HRTIMER_NORESTART &&
	    !(timer->state & HRTIMER_STATE_ENQUEUED))
		enqueue_hrtimer(timer, base, HRTIMER_MODE_ABS);

	/*
	 * Separate the ->running assignment from the ->state assignment.
	 *
	 * As with a regular write barrier, this ensures the read side in
	 * hrtimer_active() cannot observe base->running.timer == NULL &&
	 * timer->state == INACTIVE.
	 */
	raw_write_seqcount_barrier(&base->seq);

	WARN_ON_ONCE(base->running != timer);
	base->running = NULL;
}

static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now,
				 unsigned long flags, unsigned int active_mask)
{
	struct hrtimer_clock_base *base;
	unsigned int active = cpu_base->active_bases & active_mask;

	for_each_active_base(base, cpu_base, active) {
		struct timerqueue_node *node;
		ktime_t basenow;

		basenow = ktime_add(now, base->offset);

		while ((node = timerqueue_getnext(&base->active))) {
			struct hrtimer *timer;

			timer = container_of(node, struct hrtimer, node);

			/*
			 * The immediate goal for using the softexpires is
			 * minimizing wakeups, not running timers at the
			 * earliest interrupt after their soft expiration.
			 * This allows us to avoid using a Priority Search
			 * Tree, which can answer a stabbing query for
			 * overlapping intervals and instead use the simple
			 * BST we already have.
			 * We don't add extra wakeups by delaying timers that
			 * are right-of a not yet expired timer, because that
			 * timer will have to trigger a wakeup anyway.
			 */
			if (basenow < hrtimer_get_softexpires_tv64(timer))
				break;

			__run_hrtimer(cpu_base, base, timer, &basenow, flags);
			if (active_mask == HRTIMER_ACTIVE_SOFT)
				hrtimer_sync_wait_running(cpu_base, flags);
		}
	}
}

static __latent_entropy void hrtimer_run_softirq(struct softirq_action *h)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	unsigned long flags;
	ktime_t now;

	hrtimer_cpu_base_lock_expiry(cpu_base);
	raw_spin_lock_irqsave(&cpu_base->lock, flags);

	now = hrtimer_update_base(cpu_base);
	__hrtimer_run_queues(cpu_base, now, flags, HRTIMER_ACTIVE_SOFT);

	cpu_base->softirq_activated = 0;
	hrtimer_update_softirq_timer(cpu_base, true);

	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	hrtimer_cpu_base_unlock_expiry(cpu_base);
}

#ifdef CONFIG_HIGH_RES_TIMERS

/*
 * High resolution timer interrupt
 * Called with interrupts disabled
 */
void hrtimer_interrupt(struct clock_event_device *dev)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	ktime_t expires_next, now, entry_time, delta;
	unsigned long flags;
	int retries = 0;

	BUG_ON(!cpu_base->hres_active);
	cpu_base->nr_events++;
	dev->next_event = KTIME_MAX;

	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	entry_time = now = hrtimer_update_base(cpu_base);
retry:
	cpu_base->in_hrtirq = 1;
	/*
	 * We set expires_next to KTIME_MAX here with cpu_base->lock
	 * held to prevent that a timer is enqueued in our queue via
	 * the migration code. This does not affect enqueueing of
	 * timers which run their callback and need to be requeued on
	 * this CPU.
	 */
	cpu_base->expires_next = KTIME_MAX;

	if (!ktime_before(now, cpu_base->softirq_expires_next)) {
		cpu_base->softirq_expires_next = KTIME_MAX;
		cpu_base->softirq_activated = 1;
		raise_softirq_irqoff(HRTIMER_SOFTIRQ);
	}

	__hrtimer_run_queues(cpu_base, now, flags, HRTIMER_ACTIVE_HARD);

	/* Reevaluate the clock bases for the [soft] next expiry */
	expires_next = hrtimer_update_next_event(cpu_base);
	/*
	 * Store the new expiry value so the migration code can verify
	 * against it.
	 */
	cpu_base->expires_next = expires_next;
	cpu_base->in_hrtirq = 0;
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	/* Reprogramming necessary ? */
	if (!tick_program_event(expires_next, 0)) {
		cpu_base->hang_detected = 0;
		return;
	}

	/*
	 * The next timer was already expired due to:
	 * - tracing
	 * - long lasting callbacks
	 * - being scheduled away when running in a VM
	 *
	 * We need to prevent that we loop forever in the hrtimer
	 * interrupt routine. We give it 3 attempts to avoid
	 * overreacting on some spurious event.
	 *
	 * Acquire base lock for updating the offsets and retrieving
	 * the current time.
	 */
	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	now = hrtimer_update_base(cpu_base);
	cpu_base->nr_retries++;
	if (++retries < 3)
		goto retry;
	/*
	 * Give the system a chance to do something else than looping
	 * here. We stored the entry time, so we know exactly how long
	 * we spent here. We schedule the next event this amount of
	 * time away.
	 */
	cpu_base->nr_hangs++;
	cpu_base->hang_detected = 1;
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	delta = ktime_sub(now, entry_time);
	if ((unsigned int)delta > cpu_base->max_hang_time)
		cpu_base->max_hang_time = (unsigned int) delta;
	/*
	 * Limit it to a sensible value as we enforce a longer
	 * delay. Give the CPU at least 100ms to catch up.
	 */
	if (delta > 100 * NSEC_PER_MSEC)
		expires_next = ktime_add_ns(now, 100 * NSEC_PER_MSEC);
	else
		expires_next = ktime_add(now, delta);
	tick_program_event(expires_next, 1);
	pr_warn_once("hrtimer: interrupt took %llu ns\n", ktime_to_ns(delta));
}

/* called with interrupts disabled */
static inline void __hrtimer_peek_ahead_timers(void)
{
	struct tick_device *td;

	if (!hrtimer_hres_active())
		return;

	td = this_cpu_ptr(&tick_cpu_device);
	if (td && td->evtdev)
		hrtimer_interrupt(td->evtdev);
}

#else /* CONFIG_HIGH_RES_TIMERS */

static inline void __hrtimer_peek_ahead_timers(void) { }

#endif	/* !CONFIG_HIGH_RES_TIMERS */

/*
 * Called from run_local_timers in hardirq context every jiffy
 */
void hrtimer_run_queues(void)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	unsigned long flags;
	ktime_t now;

	if (__hrtimer_hres_active(cpu_base))
		return;

	/*
	 * This _is_ ugly: We have to check periodically, whether we
	 * can switch to highres and / or nohz mode. The clocksource
	 * switch happens with xtime_lock held. Notification from
	 * there only sets the check bit in the tick_oneshot code,
	 * otherwise we might deadlock vs. xtime_lock.
	 */
	if (tick_check_oneshot_change(!hrtimer_is_hres_enabled())) {
		hrtimer_switch_to_hres();
		return;
	}

	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	now = hrtimer_update_base(cpu_base);

	if (!ktime_before(now, cpu_base->softirq_expires_next)) {
		cpu_base->softirq_expires_next = KTIME_MAX;
		cpu_base->softirq_activated = 1;
		raise_softirq_irqoff(HRTIMER_SOFTIRQ);
	}

	__hrtimer_run_queues(cpu_base, now, flags, HRTIMER_ACTIVE_HARD);
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
}

/*
 * Sleep related functions:
 */
static enum hrtimer_restart hrtimer_wakeup(struct hrtimer *timer)
{
	struct hrtimer_sleeper *t =
		container_of(timer, struct hrtimer_sleeper, timer);
	struct task_struct *task = t->task;

	t->task = NULL;
	if (task)
		wake_up_process(task);

	return HRTIMER_NORESTART;
}

/**
 * hrtimer_sleeper_start_expires - Start a hrtimer sleeper timer
 * @sl:		sleeper to be started
 * @mode:	timer mode abs/rel
 *
 * Wrapper around hrtimer_start_expires() for hrtimer_sleeper based timers
 * to allow PREEMPT_RT to tweak the delivery mode (soft/hardirq context)
 */
void hrtimer_sleeper_start_expires(struct hrtimer_sleeper *sl,
				   enum hrtimer_mode mode)
{
	/*
	 * Make the enqueue delivery mode check work on RT. If the sleeper
	 * was initialized for hard interrupt delivery, force the mode bit.
	 * This is a special case for hrtimer_sleepers because
	 * hrtimer_init_sleeper() determines the delivery mode on RT so the
	 * fiddling with this decision is avoided at the call sites.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && sl->timer.is_hard)
		mode |= HRTIMER_MODE_HARD;

	hrtimer_start_expires(&sl->timer, mode);
}
EXPORT_SYMBOL_GPL(hrtimer_sleeper_start_expires);

static void __hrtimer_init_sleeper(struct hrtimer_sleeper *sl,
				   clockid_t clock_id, enum hrtimer_mode mode)
{
	/*
	 * On PREEMPT_RT enabled kernels hrtimers which are not explicitly
	 * marked for hard interrupt expiry mode are moved into soft
	 * interrupt context either for latency reasons or because the
	 * hrtimer callback takes regular spinlocks or invokes other
	 * functions which are not suitable for hard interrupt context on
	 * PREEMPT_RT.
	 *
	 * The hrtimer_sleeper callback is RT compatible in hard interrupt
	 * context, but there is a latency concern: Untrusted userspace can
	 * spawn many threads which arm timers for the same expiry time on
	 * the same CPU. That causes a latency spike due to the wakeup of
	 * a gazillion threads.
	 *
	 * OTOH, privileged real-time user space applications rely on the
	 * low latency of hard interrupt wakeups. If the current task is in
	 * a real-time scheduling class, mark the mode for hard interrupt
	 * expiry.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
		if (task_is_realtime(current) && !(mode & HRTIMER_MODE_SOFT))
			mode |= HRTIMER_MODE_HARD;
	}

	__hrtimer_init(&sl->timer, clock_id, mode);
	sl->timer.function = hrtimer_wakeup;
	sl->task = current;
}

/**
 * hrtimer_init_sleeper - initialize sleeper to the given clock
 * @sl:		sleeper to be initialized
 * @clock_id:	the clock to be used
 * @mode:	timer mode abs/rel
 */
void hrtimer_init_sleeper(struct hrtimer_sleeper *sl, clockid_t clock_id,
			  enum hrtimer_mode mode)
{
	debug_init(&sl->timer, clock_id, mode);
	__hrtimer_init_sleeper(sl, clock_id, mode);

}
EXPORT_SYMBOL_GPL(hrtimer_init_sleeper);

int nanosleep_copyout(struct restart_block *restart, struct timespec64 *ts)
{
	switch(restart->nanosleep.type) {
#ifdef CONFIG_COMPAT_32BIT_TIME
	case TT_COMPAT:
		if (put_old_timespec32(ts, restart->nanosleep.compat_rmtp))
			return -EFAULT;
		break;
#endif
	case TT_NATIVE:
		if (put_timespec64(ts, restart->nanosleep.rmtp))
			return -EFAULT;
		break;
	default:
		BUG();
	}
	return -ERESTART_RESTARTBLOCK;
}

static int __sched do_nanosleep(struct hrtimer_sleeper *t, enum hrtimer_mode mode)
{
	struct restart_block *restart;

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		hrtimer_sleeper_start_expires(t, mode);

		if (likely(t->task))
			freezable_schedule();

		hrtimer_cancel(&t->timer);
		mode = HRTIMER_MODE_ABS;

	} while (t->task && !signal_pending(current));

	__set_current_state(TASK_RUNNING);

	if (!t->task)
		return 0;

	restart = &current->restart_block;
	if (restart->nanosleep.type != TT_NONE) {
		ktime_t rem = hrtimer_expires_remaining(&t->timer);
		struct timespec64 rmt;

		if (rem <= 0)
			return 0;
		rmt = ktime_to_timespec64(rem);

		return nanosleep_copyout(restart, &rmt);
	}
	return -ERESTART_RESTARTBLOCK;
}

static long __sched hrtimer_nanosleep_restart(struct restart_block *restart)
{
	struct hrtimer_sleeper t;
	int ret;

	hrtimer_init_sleeper_on_stack(&t, restart->nanosleep.clockid,
				      HRTIMER_MODE_ABS);
	hrtimer_set_expires_tv64(&t.timer, restart->nanosleep.expires);
	ret = do_nanosleep(&t, HRTIMER_MODE_ABS);
	destroy_hrtimer_on_stack(&t.timer);
	return ret;
}

long hrtimer_nanosleep(ktime_t rqtp, const enum hrtimer_mode mode,
		       const clockid_t clockid)
{
	struct restart_block *restart;
	struct hrtimer_sleeper t;
	int ret = 0;
	u64 slack;

	slack = current->timer_slack_ns;
	if (dl_task(current) || rt_task(current))
		slack = 0;

	hrtimer_init_sleeper_on_stack(&t, clockid, mode);
	hrtimer_set_expires_range_ns(&t.timer, rqtp, slack);
	ret = do_nanosleep(&t, mode);
	if (ret != -ERESTART_RESTARTBLOCK)
		goto out;

	/* Absolute timers do not update the rmtp value and restart: */
	if (mode == HRTIMER_MODE_ABS) {
		ret = -ERESTARTNOHAND;
		goto out;
	}

	restart = &current->restart_block;
	restart->nanosleep.clockid = t.timer.base->clockid;
	restart->nanosleep.expires = hrtimer_get_expires_tv64(&t.timer);
	set_restart_fn(restart, hrtimer_nanosleep_restart);
out:
	destroy_hrtimer_on_stack(&t.timer);
	return ret;
}

#ifdef CONFIG_64BIT

SYSCALL_DEFINE2(nanosleep, struct __kernel_timespec __user *, rqtp,
		struct __kernel_timespec __user *, rmtp)
{
	struct timespec64 tu;

	if (get_timespec64(&tu, rqtp))
		return -EFAULT;

	if (!timespec64_valid(&tu))
		return -EINVAL;

	current->restart_block.nanosleep.type = rmtp ? TT_NATIVE : TT_NONE;
	current->restart_block.nanosleep.rmtp = rmtp;
	return hrtimer_nanosleep(timespec64_to_ktime(tu), HRTIMER_MODE_REL,
				 CLOCK_MONOTONIC);
}

#endif

#ifdef CONFIG_COMPAT_32BIT_TIME

SYSCALL_DEFINE2(nanosleep_time32, struct old_timespec32 __user *, rqtp,
		       struct old_timespec32 __user *, rmtp)
{
	struct timespec64 tu;

	if (get_old_timespec32(&tu, rqtp))
		return -EFAULT;

	if (!timespec64_valid(&tu))
		return -EINVAL;

	current->restart_block.nanosleep.type = rmtp ? TT_COMPAT : TT_NONE;
	current->restart_block.nanosleep.compat_rmtp = rmtp;
	return hrtimer_nanosleep(timespec64_to_ktime(tu), HRTIMER_MODE_REL,
				 CLOCK_MONOTONIC);
}
#endif

/*
 * Functions related to boot-time initialization:
 */
int hrtimers_prepare_cpu(unsigned int cpu)
{
	struct hrtimer_cpu_base *cpu_base = &per_cpu(hrtimer_bases, cpu);
	int i;

	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
		struct hrtimer_clock_base *clock_b = &cpu_base->clock_base[i];

		clock_b->cpu_base = cpu_base;
		seqcount_raw_spinlock_init(&clock_b->seq, &cpu_base->lock);
		timerqueue_init_head(&clock_b->active);
	}

	cpu_base->cpu = cpu;
	cpu_base->active_bases = 0;
	cpu_base->hres_active = 0;
	cpu_base->hang_detected = 0;
	cpu_base->next_timer = NULL;
	cpu_base->softirq_next_timer = NULL;
	cpu_base->expires_next = KTIME_MAX;
	cpu_base->softirq_expires_next = KTIME_MAX;
	hrtimer_cpu_base_init_expiry_lock(cpu_base);
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU

static void migrate_hrtimer_list(struct hrtimer_clock_base *old_base,
				struct hrtimer_clock_base *new_base)
{
	struct hrtimer *timer;
	struct timerqueue_node *node;

	while ((node = timerqueue_getnext(&old_base->active))) {
		timer = container_of(node, struct hrtimer, node);
		BUG_ON(hrtimer_callback_running(timer));
		debug_deactivate(timer);

		/*
		 * Mark it as ENQUEUED not INACTIVE otherwise the
		 * timer could be seen as !active and just vanish away
		 * under us on another CPU
		 */
		__remove_hrtimer(timer, old_base, HRTIMER_STATE_ENQUEUED, 0);
		timer->base = new_base;
		/*
		 * Enqueue the timers on the new cpu. This does not
		 * reprogram the event device in case the timer
		 * expires before the earliest on this CPU, but we run
		 * hrtimer_interrupt after we migrated everything to
		 * sort out already expired timers and reprogram the
		 * event device.
		 */
		enqueue_hrtimer(timer, new_base, HRTIMER_MODE_ABS);
	}
}

int hrtimers_dead_cpu(unsigned int scpu)
{
	struct hrtimer_cpu_base *old_base, *new_base;
	int i;

	BUG_ON(cpu_online(scpu));
	tick_cancel_sched_timer(scpu);

	/*
	 * this BH disable ensures that raise_softirq_irqoff() does
	 * not wakeup ksoftirqd (and acquire the pi-lock) while
	 * holding the cpu_base lock
	 */
	local_bh_disable();
	local_irq_disable();
	old_base = &per_cpu(hrtimer_bases, scpu);
	new_base = this_cpu_ptr(&hrtimer_bases);
	/*
	 * The caller is globally serialized and nobody else
	 * takes two locks at once, deadlock is not possible.
	 */
	raw_spin_lock(&new_base->lock);
	raw_spin_lock_nested(&old_base->lock, SINGLE_DEPTH_NESTING);

	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
		migrate_hrtimer_list(&old_base->clock_base[i],
				     &new_base->clock_base[i]);
	}

	/*
	 * The migration might have changed the first expiring softirq
	 * timer on this CPU. Update it.
	 */
	hrtimer_update_softirq_timer(new_base, false);

	raw_spin_unlock(&old_base->lock);
	raw_spin_unlock(&new_base->lock);

	/* Check, if we got expired work to do */
	__hrtimer_peek_ahead_timers();
	local_irq_enable();
	local_bh_enable();
	return 0;
}

#endif /* CONFIG_HOTPLUG_CPU */

void __init hrtimers_init(void)
{
	hrtimers_prepare_cpu(smp_processor_id());
	open_softirq(HRTIMER_SOFTIRQ, hrtimer_run_softirq);
}

/**
 * schedule_hrtimeout_range_clock - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @delta:	slack in expires timeout (ktime_t)
 * @mode:	timer mode
 * @clock_id:	timer clock to be used
 */
int __sched
schedule_hrtimeout_range_clock(ktime_t *expires, u64 delta,
			       const enum hrtimer_mode mode, clockid_t clock_id)
{
	struct hrtimer_sleeper t;

	/*
	 * Optimize when a zero timeout value is given. It does not
	 * matter whether this is an absolute or a relative time.
	 */
	if (expires && *expires == 0) {
		__set_current_state(TASK_RUNNING);
		return 0;
	}

	/*
	 * A NULL parameter means "infinite"
	 */
	if (!expires) {
		schedule();
		return -EINTR;
	}

	hrtimer_init_sleeper_on_stack(&t, clock_id, mode);
	hrtimer_set_expires_range_ns(&t.timer, *expires, delta);
	hrtimer_sleeper_start_expires(&t, mode);

	if (likely(t.task))
		schedule();

	hrtimer_cancel(&t.timer);
	destroy_hrtimer_on_stack(&t.timer);

	__set_current_state(TASK_RUNNING);

	return !t.task ? 0 : -EINTR;
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout_range_clock);

/**
 * schedule_hrtimeout_range - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @delta:	slack in expires timeout (ktime_t)
 * @mode:	timer mode
 *
 * Make the current task sleep until the given expiry time has
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * The @delta argument gives the kernel the freedom to schedule the
 * actual wakeup to a time that is both power and performance friendly.
 * The kernel give the normal best effort behavior for "@expires+@delta",
 * but may decide to fire the timer earlier, but no earlier than @expires.
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout time is guaranteed to
 * pass before the routine returns unless the current task is explicitly
 * woken up, (e.g. by wake_up_process()).
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task or the current task is explicitly woken
 * up.
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Returns 0 when the timer has expired. If the task was woken before the
 * timer expired by a signal (only possible in state TASK_INTERRUPTIBLE) or
 * by an explicit wakeup, it returns -EINTR.
 */
int __sched schedule_hrtimeout_range(ktime_t *expires, u64 delta,
				     const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range_clock(expires, delta, mode,
					      CLOCK_MONOTONIC);
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout_range);

/**
 * schedule_hrtimeout - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @mode:	timer mode
 *
 * Make the current task sleep until the given expiry time has
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout time is guaranteed to
 * pass before the routine returns unless the current task is explicitly
 * woken up, (e.g. by wake_up_process()).
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task or the current task is explicitly woken
 * up.
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Returns 0 when the timer has expired. If the task was woken before the
 * timer expired by a signal (only possible in state TASK_INTERRUPTIBLE) or
 * by an explicit wakeup, it returns -EINTR.
 */
int __sched schedule_hrtimeout(ktime_t *expires,
			       const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range(expires, 0, mode);
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout);
