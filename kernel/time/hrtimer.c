/*
 *  linux/kernel/hrtimer.c
 *
 *  Copyright(C) 2005-2006, Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2005-2007, Red Hat, Inc., Ingo Molnar
 *  Copyright(C) 2006-2007  Timesys Corp., Thomas Gleixner
 *
 *  High-resolution kernel timers
 *
 *  In contrast to the low-resolution timeout API implemented in
 *  kernel/timer.c, hrtimers provide finer resolution and accuracy
 *  depending on system configuration and capabilities.
 *
 *  These timers are currently used for:
 *   - itimers
 *   - POSIX timers
 *   - nanosleep
 *   - precise in-kernel timing
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  Credits:
 *	based on kernel/timer.c
 *
 *	Help, testing, suggestions, bugfixes, improvements were
 *	provided by:
 *
 *	George Anzinger, Andrew Morton, Steven Rostedt, Roman Zippel
 *	et. al.
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/tick.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/debugobjects.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/deadline.h>
#include <linux/timer.h>
#include <linux/freezer.h>

#include <asm/uaccess.h>

#include <trace/events/timer.h>

#include "tick-internal.h"

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
	.seq = SEQCNT_ZERO(hrtimer_bases.seq),
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
	}
};

static const int hrtimer_clock_to_base_table[MAX_CLOCKS] = {
	[CLOCK_REALTIME]	= HRTIMER_BASE_REALTIME,
	[CLOCK_MONOTONIC]	= HRTIMER_BASE_MONOTONIC,
	[CLOCK_BOOTTIME]	= HRTIMER_BASE_BOOTTIME,
	[CLOCK_TAI]		= HRTIMER_BASE_TAI,
};

static inline int hrtimer_clockid_to_base(clockid_t clock_id)
{
	return hrtimer_clock_to_base_table[clock_id];
}

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
	.seq = SEQCNT_ZERO(migration_cpu_base),
	.clock_base = { { .cpu_base = &migration_cpu_base, }, },
};

#define migration_base	migration_cpu_base.clock_base[0]

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
		base = timer->base;
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
 * With HIGHRES=y we do not migrate the timer when it is expiring
 * before the next event on the target cpu because we cannot reprogram
 * the target cpu hardware and we would cause it to fire late.
 *
 * Called with cpu_base->lock of target cpu held.
 */
static int
hrtimer_check_target(struct hrtimer *timer, struct hrtimer_clock_base *new_base)
{
#ifdef CONFIG_HIGH_RES_TIMERS
	ktime_t expires;

	if (!new_base->cpu_base->hres_active)
		return 0;

	expires = ktime_sub(hrtimer_get_expires(timer), new_base->offset);
	return expires.tv64 <= new_base->cpu_base->expires_next.tv64;
#else
	return 0;
#endif
}

#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
static inline
struct hrtimer_cpu_base *get_target_base(struct hrtimer_cpu_base *base,
					 int pinned)
{
	if (pinned || !base->migration_enabled)
		return base;
	return &per_cpu(hrtimer_bases, get_nohz_timer_target());
}
#else
static inline
struct hrtimer_cpu_base *get_target_base(struct hrtimer_cpu_base *base,
					 int pinned)
{
	return base;
}
#endif

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
		timer->base = &migration_base;
		raw_spin_unlock(&base->cpu_base->lock);
		raw_spin_lock(&new_base->cpu_base->lock);

		if (new_cpu_base != this_cpu_base &&
		    hrtimer_check_target(timer, new_base)) {
			raw_spin_unlock(&new_base->cpu_base->lock);
			raw_spin_lock(&base->cpu_base->lock);
			new_cpu_base = this_cpu_base;
			timer->base = base;
			goto again;
		}
		timer->base = new_base;
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
	do_div(tmp, (unsigned long) div);
	return dclc < 0 ? -tmp : tmp;
}
EXPORT_SYMBOL_GPL(__ktime_divns);
#endif /* BITS_PER_LONG >= 64 */

/*
 * Add two ktime values and do a safety check for overflow:
 */
ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res = ktime_add(lhs, rhs);

	/*
	 * We use KTIME_SEC_MAX here, the maximum timeout which we can
	 * return to user space in a timespec:
	 */
	if (res.tv64 < 0 || res.tv64 < lhs.tv64 || res.tv64 < rhs.tv64)
		res = ktime_set(KTIME_SEC_MAX, 0);

	return res;
}

EXPORT_SYMBOL_GPL(ktime_add_safe);

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS

static struct debug_obj_descr hrtimer_debug_descr;

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
 * - an unknown object is activated (might be a statically initialized object)
 */
static bool hrtimer_fixup_activate(void *addr, enum debug_obj_state state)
{
	switch (state) {

	case ODEBUG_STATE_NOTAVAILABLE:
		WARN_ON_ONCE(1);
		return false;

	case ODEBUG_STATE_ACTIVE:
		WARN_ON(1);

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

static struct debug_obj_descr hrtimer_debug_descr = {
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

static inline void debug_hrtimer_activate(struct hrtimer *timer)
{
	debug_object_activate(timer, &hrtimer_debug_descr);
}

static inline void debug_hrtimer_deactivate(struct hrtimer *timer)
{
	debug_object_deactivate(timer, &hrtimer_debug_descr);
}

static inline void debug_hrtimer_free(struct hrtimer *timer)
{
	debug_object_free(timer, &hrtimer_debug_descr);
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

void destroy_hrtimer_on_stack(struct hrtimer *timer)
{
	debug_object_free(timer, &hrtimer_debug_descr);
}

#else
static inline void debug_hrtimer_init(struct hrtimer *timer) { }
static inline void debug_hrtimer_activate(struct hrtimer *timer) { }
static inline void debug_hrtimer_deactivate(struct hrtimer *timer) { }
#endif

static inline void
debug_init(struct hrtimer *timer, clockid_t clockid,
	   enum hrtimer_mode mode)
{
	debug_hrtimer_init(timer);
	trace_hrtimer_init(timer, clockid, mode);
}

static inline void debug_activate(struct hrtimer *timer)
{
	debug_hrtimer_activate(timer);
	trace_hrtimer_start(timer);
}

static inline void debug_deactivate(struct hrtimer *timer)
{
	debug_hrtimer_deactivate(timer);
	trace_hrtimer_cancel(timer);
}

#if defined(CONFIG_NO_HZ_COMMON) || defined(CONFIG_HIGH_RES_TIMERS)
static inline void hrtimer_update_next_timer(struct hrtimer_cpu_base *cpu_base,
					     struct hrtimer *timer)
{
#ifdef CONFIG_HIGH_RES_TIMERS
	cpu_base->next_timer = timer;
#endif
}

static ktime_t __hrtimer_get_next_event(struct hrtimer_cpu_base *cpu_base)
{
	struct hrtimer_clock_base *base = cpu_base->clock_base;
	ktime_t expires, expires_next = { .tv64 = KTIME_MAX };
	unsigned int active = cpu_base->active_bases;

	hrtimer_update_next_timer(cpu_base, NULL);
	for (; active; base++, active >>= 1) {
		struct timerqueue_node *next;
		struct hrtimer *timer;

		if (!(active & 0x01))
			continue;

		next = timerqueue_getnext(&base->active);
		timer = container_of(next, struct hrtimer, node);
		expires = ktime_sub(hrtimer_get_expires(timer), base->offset);
		if (expires.tv64 < expires_next.tv64) {
			expires_next = expires;
			hrtimer_update_next_timer(cpu_base, timer);
		}
	}
	/*
	 * clock_was_set() might have changed base->offset of any of
	 * the clock bases so the result might be negative. Fix it up
	 * to prevent a false positive in clockevents_program_event().
	 */
	if (expires_next.tv64 < 0)
		expires_next.tv64 = 0;
	return expires_next;
}
#endif

static inline ktime_t hrtimer_update_base(struct hrtimer_cpu_base *base)
{
	ktime_t *offs_real = &base->clock_base[HRTIMER_BASE_REALTIME].offset;
	ktime_t *offs_boot = &base->clock_base[HRTIMER_BASE_BOOTTIME].offset;
	ktime_t *offs_tai = &base->clock_base[HRTIMER_BASE_TAI].offset;

	return ktime_get_update_offsets_now(&base->clock_was_set_seq,
					    offs_real, offs_boot, offs_tai);
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

/*
 * Is the high resolution mode active ?
 */
static inline int __hrtimer_hres_active(struct hrtimer_cpu_base *cpu_base)
{
	return cpu_base->hres_active;
}

static inline int hrtimer_hres_active(void)
{
	return __hrtimer_hres_active(this_cpu_ptr(&hrtimer_bases));
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

	if (!cpu_base->hres_active)
		return;

	expires_next = __hrtimer_get_next_event(cpu_base);

	if (skip_equal && expires_next.tv64 == cpu_base->expires_next.tv64)
		return;

	cpu_base->expires_next.tv64 = expires_next.tv64;

	/*
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
	 * set. So we'd effectivly block all timers until the T2 event
	 * fires.
	 */
	if (cpu_base->hang_detected)
		return;

	tick_program_event(cpu_base->expires_next, 1);
}

/*
 * When a timer is enqueued and expires earlier than the already enqueued
 * timers, we have to check, whether it expires earlier than the timer for
 * which the clock event device was armed.
 *
 * Called with interrupts disabled and base->cpu_base.lock held
 */
static void hrtimer_reprogram(struct hrtimer *timer,
			      struct hrtimer_clock_base *base)
{
	struct hrtimer_cpu_base *cpu_base = this_cpu_ptr(&hrtimer_bases);
	ktime_t expires = ktime_sub(hrtimer_get_expires(timer), base->offset);

	WARN_ON_ONCE(hrtimer_get_expires_tv64(timer) < 0);

	/*
	 * If the timer is not on the current cpu, we cannot reprogram
	 * the other cpus clock event device.
	 */
	if (base->cpu_base != cpu_base)
		return;

	/*
	 * If the hrtimer interrupt is running, then it will
	 * reevaluate the clock bases and reprogram the clock event
	 * device. The callbacks are always executed in hard interrupt
	 * context so we don't need an extra check for a running
	 * callback.
	 */
	if (cpu_base->in_hrtirq)
		return;

	/*
	 * CLOCK_REALTIME timer might be requested with an absolute
	 * expiry time which is less than base->offset. Set it to 0.
	 */
	if (expires.tv64 < 0)
		expires.tv64 = 0;

	if (expires.tv64 >= cpu_base->expires_next.tv64)
		return;

	/* Update the pointer to the next expiring timer */
	cpu_base->next_timer = timer;

	/*
	 * If a hang was detected in the last timer interrupt then we
	 * do not schedule a timer which is earlier than the expiry
	 * which we enforced in the hang detection. We want the system
	 * to make progress.
	 */
	if (cpu_base->hang_detected)
		return;

	/*
	 * Program the timer hardware. We enforce the expiry for
	 * events which are already in the past.
	 */
	cpu_base->expires_next = expires;
	tick_program_event(expires, 1);
}

/*
 * Initialize the high resolution related parts of cpu_base
 */
static inline void hrtimer_init_hres(struct hrtimer_cpu_base *base)
{
	base->expires_next.tv64 = KTIME_MAX;
	base->hres_active = 0;
}

/*
 * Retrigger next event is called after clock was set
 *
 * Called with interrupts disabled via on_each_cpu()
 */
static void retrigger_next_event(void *arg)
{
	struct hrtimer_cpu_base *base = this_cpu_ptr(&hrtimer_bases);

	if (!base->hres_active)
		return;

	raw_spin_lock(&base->lock);
	hrtimer_update_base(base);
	hrtimer_force_reprogram(base, 0);
	raw_spin_unlock(&base->lock);
}

/*
 * Switch to high resolution mode
 */
static void hrtimer_switch_to_hres(void)
{
	struct hrtimer_cpu_base *base = this_cpu_ptr(&hrtimer_bases);

	if (tick_init_highres()) {
		printk(KERN_WARNING "Could not switch to high resolution "
				    "mode on CPU %d\n", base->cpu);
		return;
	}
	base->hres_active = 1;
	hrtimer_resolution = HIGH_RES_NSEC;

	tick_setup_sched_timer();
	/* "Retrigger" the interrupt to get things going */
	retrigger_next_event(NULL);
}

static void clock_was_set_work(struct work_struct *work)
{
	clock_was_set();
}

static DECLARE_WORK(hrtimer_work, clock_was_set_work);

/*
 * Called from timekeeping and resume code to reprogramm the hrtimer
 * interrupt device on all cpus.
 */
void clock_was_set_delayed(void)
{
	schedule_work(&hrtimer_work);
}

#else

static inline int __hrtimer_hres_active(struct hrtimer_cpu_base *b) { return 0; }
static inline int hrtimer_hres_active(void) { return 0; }
static inline int hrtimer_is_hres_enabled(void) { return 0; }
static inline void hrtimer_switch_to_hres(void) { }
static inline void
hrtimer_force_reprogram(struct hrtimer_cpu_base *base, int skip_equal) { }
static inline int hrtimer_reprogram(struct hrtimer *timer,
				    struct hrtimer_clock_base *base)
{
	return 0;
}
static inline void hrtimer_init_hres(struct hrtimer_cpu_base *base) { }
static inline void retrigger_next_event(void *arg) { }

#endif /* CONFIG_HIGH_RES_TIMERS */

/*
 * Clock realtime was set
 *
 * Change the offset of the realtime clock vs. the monotonic
 * clock.
 *
 * We might have to reprogram the high resolution timer interrupt. On
 * SMP we call the architecture specific code to retrigger _all_ high
 * resolution timer interrupts. On UP we just disable interrupts and
 * call the high resolution interrupt code.
 */
void clock_was_set(void)
{
#ifdef CONFIG_HIGH_RES_TIMERS
	/* Retrigger the CPU local events everywhere */
	on_each_cpu(retrigger_next_event, NULL, 1);
#endif
	timerfd_clock_was_set();
}

/*
 * During resume we might have to reprogram the high resolution timer
 * interrupt on all online CPUs.  However, all other CPUs will be
 * stopped with IRQs interrupts disabled so the clock_was_set() call
 * must be deferred.
 */
void hrtimers_resume(void)
{
	WARN_ONCE(!irqs_disabled(),
		  KERN_INFO "hrtimers_resume() called with IRQs enabled!");

	/* Retrigger on the local CPU */
	retrigger_next_event(NULL);
	/* And schedule a retrigger for all others */
	clock_was_set_delayed();
}

static inline void timer_stats_hrtimer_set_start_info(struct hrtimer *timer)
{
#ifdef CONFIG_TIMER_STATS
	if (timer->start_site)
		return;
	timer->start_site = __builtin_return_address(0);
	memcpy(timer->start_comm, current->comm, TASK_COMM_LEN);
	timer->start_pid = current->pid;
#endif
}

static inline void timer_stats_hrtimer_clear_start_info(struct hrtimer *timer)
{
#ifdef CONFIG_TIMER_STATS
	timer->start_site = NULL;
#endif
}

static inline void timer_stats_account_hrtimer(struct hrtimer *timer)
{
#ifdef CONFIG_TIMER_STATS
	if (likely(!timer_stats_active))
		return;
	timer_stats_update_stats(timer, timer->start_pid, timer->start_site,
				 timer->function, timer->start_comm, 0);
#endif
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

	if (delta.tv64 < 0)
		return 0;

	if (WARN_ON(timer->state & HRTIMER_STATE_ENQUEUED))
		return 0;

	if (interval.tv64 < hrtimer_resolution)
		interval.tv64 = hrtimer_resolution;

	if (unlikely(delta.tv64 >= interval.tv64)) {
		s64 incr = ktime_to_ns(interval);

		orun = ktime_divns(delta, incr);
		hrtimer_add_expires_ns(timer, incr * orun);
		if (hrtimer_get_expires_tv64(timer) > now.tv64)
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
			   struct hrtimer_clock_base *base)
{
	debug_activate(timer);

	base->cpu_base->active_bases |= 1 << base->index;

	timer->state = HRTIMER_STATE_ENQUEUED;

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

	timer->state = newstate;
	if (!(state & HRTIMER_STATE_ENQUEUED))
		return;

	if (!timerqueue_del(&base->active, &timer->node))
		cpu_base->active_bases &= ~(1 << base->index);

#ifdef CONFIG_HIGH_RES_TIMERS
	/*
	 * Note: If reprogram is false we do not update
	 * cpu_base->next_timer. This happens when we remove the first
	 * timer on a remote cpu. No harm as we never dereference
	 * cpu_base->next_timer. So the worst thing what can happen is
	 * an superflous call to hrtimer_force_reprogram() on the
	 * remote cpu later on if the same timer gets enqueued again.
	 */
	if (reprogram && timer == cpu_base->next_timer)
		hrtimer_force_reprogram(cpu_base, 1);
#endif
}

/*
 * remove hrtimer, called with base lock held
 */
static inline int
remove_hrtimer(struct hrtimer *timer, struct hrtimer_clock_base *base, bool restart)
{
	if (hrtimer_is_queued(timer)) {
		u8 state = timer->state;
		int reprogram;

		/*
		 * Remove the timer and force reprogramming when high
		 * resolution mode is active and the timer is on the current
		 * CPU. If we remove a timer on another CPU, reprogramming is
		 * skipped. The interrupt event on this CPU is fired and
		 * reprogramming happens in the interrupt handler. This is a
		 * rare case and less expensive than a smp call.
		 */
		debug_deactivate(timer);
		timer_stats_hrtimer_clear_start_info(timer);
		reprogram = base->cpu_base == this_cpu_ptr(&hrtimer_bases);

		if (!restart)
			state = HRTIMER_STATE_INACTIVE;

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
		tim = ktime_add_safe(tim, ktime_set(0, hrtimer_resolution));
#endif
	return tim;
}

/**
 * hrtimer_start_range_ns - (re)start an hrtimer on the current CPU
 * @timer:	the timer to be added
 * @tim:	expiry time
 * @delta_ns:	"slack" range for the timer
 * @mode:	expiry mode: absolute (HRTIMER_MODE_ABS) or
 *		relative (HRTIMER_MODE_REL)
 */
void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
			    u64 delta_ns, const enum hrtimer_mode mode)
{
	struct hrtimer_clock_base *base, *new_base;
	unsigned long flags;
	int leftmost;

	base = lock_hrtimer_base(timer, &flags);

	/* Remove an active timer from the queue: */
	remove_hrtimer(timer, base, true);

	if (mode & HRTIMER_MODE_REL)
		tim = ktime_add_safe(tim, base->get_time());

	tim = hrtimer_update_lowres(timer, tim, mode);

	hrtimer_set_expires_range_ns(timer, tim, delta_ns);

	/* Switch the timer base, if necessary: */
	new_base = switch_hrtimer_base(timer, base, mode & HRTIMER_MODE_PINNED);

	timer_stats_hrtimer_set_start_info(timer);

	leftmost = enqueue_hrtimer(timer, new_base);
	if (!leftmost)
		goto unlock;

	if (!hrtimer_is_hres_active(timer)) {
		/*
		 * Kick to reschedule the next tick to handle the new timer
		 * on dynticks target.
		 */
		if (new_base->cpu_base->nohz_active)
			wake_up_nohz_cpu(new_base->cpu_base->cpu);
	} else {
		hrtimer_reprogram(timer, new_base);
	}
unlock:
	unlock_hrtimer_base(timer, &flags);
}
EXPORT_SYMBOL_GPL(hrtimer_start_range_ns);

/**
 * hrtimer_try_to_cancel - try to deactivate a timer
 * @timer:	hrtimer to stop
 *
 * Returns:
 *  0 when the timer was not active
 *  1 when the timer was active
 * -1 when the timer is currently excuting the callback function and
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
		ret = remove_hrtimer(timer, base, false);

	unlock_hrtimer_base(timer, &flags);

	return ret;

}
EXPORT_SYMBOL_GPL(hrtimer_try_to_cancel);

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
	for (;;) {
		int ret = hrtimer_try_to_cancel(timer);

		if (ret >= 0)
			return ret;
		cpu_relax();
	}
}
EXPORT_SYMBOL_GPL(hrtimer_cancel);

/**
 * hrtimer_get_remaining - get remaining time for the timer
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
		expires = __hrtimer_get_next_event(cpu_base).tv64;

	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	return expires;
}
#endif

static void __hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
			   enum hrtimer_mode mode)
{
	struct hrtimer_cpu_base *cpu_base;
	int base;

	memset(timer, 0, sizeof(struct hrtimer));

	cpu_base = raw_cpu_ptr(&hrtimer_bases);

	if (clock_id == CLOCK_REALTIME && mode != HRTIMER_MODE_ABS)
		clock_id = CLOCK_MONOTONIC;

	base = hrtimer_clockid_to_base(clock_id);
	timer->base = &cpu_base->clock_base[base];
	timerqueue_init(&timer->node);

#ifdef CONFIG_TIMER_STATS
	timer->start_site = NULL;
	timer->start_pid = -1;
	memset(timer->start_comm, 0, TASK_COMM_LEN);
#endif
}

/**
 * hrtimer_init - initialize a timer to the given clock
 * @timer:	the timer to be initialized
 * @clock_id:	the clock to be used
 * @mode:	timer mode abs/rel
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
	struct hrtimer_cpu_base *cpu_base;
	unsigned int seq;

	do {
		cpu_base = READ_ONCE(timer->base->cpu_base);
		seq = raw_read_seqcount_begin(&cpu_base->seq);

		if (timer->state != HRTIMER_STATE_INACTIVE ||
		    cpu_base->running == timer)
			return true;

	} while (read_seqcount_retry(&cpu_base->seq, seq) ||
		 cpu_base != READ_ONCE(timer->base->cpu_base));

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
 * a false negative if the read side got smeared over multiple consequtive
 * __run_hrtimer() invocations.
 */

static void __run_hrtimer(struct hrtimer_cpu_base *cpu_base,
			  struct hrtimer_clock_base *base,
			  struct hrtimer *timer, ktime_t *now)
{
	enum hrtimer_restart (*fn)(struct hrtimer *);
	int restart;

	lockdep_assert_held(&cpu_base->lock);

	debug_deactivate(timer);
	cpu_base->running = timer;

	/*
	 * Separate the ->running assignment from the ->state assignment.
	 *
	 * As with a regular write barrier, this ensures the read side in
	 * hrtimer_active() cannot observe cpu_base->running == NULL &&
	 * timer->state == INACTIVE.
	 */
	raw_write_seqcount_barrier(&cpu_base->seq);

	__remove_hrtimer(timer, base, HRTIMER_STATE_INACTIVE, 0);
	timer_stats_account_hrtimer(timer);
	fn = timer->function;

	/*
	 * Clear the 'is relative' flag for the TIME_LOW_RES case. If the
	 * timer is restarted with a period then it becomes an absolute
	 * timer. If its not restarted it does not matter.
	 */
	if (IS_ENABLED(CONFIG_TIME_LOW_RES))
		timer->is_rel = false;

	/*
	 * Because we run timers from hardirq context, there is no chance
	 * they get migrated to another cpu, therefore its safe to unlock
	 * the timer base.
	 */
	raw_spin_unlock(&cpu_base->lock);
	trace_hrtimer_expire_entry(timer, now);
	restart = fn(timer);
	trace_hrtimer_expire_exit(timer);
	raw_spin_lock(&cpu_base->lock);

	/*
	 * Note: We clear the running state after enqueue_hrtimer and
	 * we do not reprogramm the event hardware. Happens either in
	 * hrtimer_start_range_ns() or in hrtimer_interrupt()
	 *
	 * Note: Because we dropped the cpu_base->lock above,
	 * hrtimer_start_range_ns() can have popped in and enqueued the timer
	 * for us already.
	 */
	if (restart != HRTIMER_NORESTART &&
	    !(timer->state & HRTIMER_STATE_ENQUEUED))
		enqueue_hrtimer(timer, base);

	/*
	 * Separate the ->running assignment from the ->state assignment.
	 *
	 * As with a regular write barrier, this ensures the read side in
	 * hrtimer_active() cannot observe cpu_base->running == NULL &&
	 * timer->state == INACTIVE.
	 */
	raw_write_seqcount_barrier(&cpu_base->seq);

	WARN_ON_ONCE(cpu_base->running != timer);
	cpu_base->running = NULL;
}

static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now)
{
	struct hrtimer_clock_base *base = cpu_base->clock_base;
	unsigned int active = cpu_base->active_bases;

	for (; active; base++, active >>= 1) {
		struct timerqueue_node *node;
		ktime_t basenow;

		if (!(active & 0x01))
			continue;

		basenow = ktime_add(now, base->offset);

		while ((node = timerqueue_getnext(&base->active))) {
			struct hrtimer *timer;

			timer = container_of(node, struct hrtimer, node);

			/*
			 * The immediate goal for using the softexpires is
			 * minimizing wakeups, not running timers at the
			 * earliest interrupt after their soft expiration.
			 * This allows us to avoid using a Priority Search
			 * Tree, which can answer a stabbing querry for
			 * overlapping intervals and instead use the simple
			 * BST we already have.
			 * We don't add extra wakeups by delaying timers that
			 * are right-of a not yet expired timer, because that
			 * timer will have to trigger a wakeup anyway.
			 */
			if (basenow.tv64 < hrtimer_get_softexpires_tv64(timer))
				break;

			__run_hrtimer(cpu_base, base, timer, &basenow);
		}
	}
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
	int retries = 0;

	BUG_ON(!cpu_base->hres_active);
	cpu_base->nr_events++;
	dev->next_event.tv64 = KTIME_MAX;

	raw_spin_lock(&cpu_base->lock);
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
	cpu_base->expires_next.tv64 = KTIME_MAX;

	__hrtimer_run_queues(cpu_base, now);

	/* Reevaluate the clock bases for the next expiry */
	expires_next = __hrtimer_get_next_event(cpu_base);
	/*
	 * Store the new expiry value so the migration code can verify
	 * against it.
	 */
	cpu_base->expires_next = expires_next;
	cpu_base->in_hrtirq = 0;
	raw_spin_unlock(&cpu_base->lock);

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
	raw_spin_lock(&cpu_base->lock);
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
	raw_spin_unlock(&cpu_base->lock);
	delta = ktime_sub(now, entry_time);
	if ((unsigned int)delta.tv64 > cpu_base->max_hang_time)
		cpu_base->max_hang_time = (unsigned int) delta.tv64;
	/*
	 * Limit it to a sensible value as we enforce a longer
	 * delay. Give the CPU at least 100ms to catch up.
	 */
	if (delta.tv64 > 100 * NSEC_PER_MSEC)
		expires_next = ktime_add_ns(now, 100 * NSEC_PER_MSEC);
	else
		expires_next = ktime_add(now, delta);
	tick_program_event(expires_next, 1);
	printk_once(KERN_WARNING "hrtimer: interrupt took %llu ns\n",
		    ktime_to_ns(delta));
}

/*
 * local version of hrtimer_peek_ahead_timers() called with interrupts
 * disabled.
 */
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

	raw_spin_lock(&cpu_base->lock);
	now = hrtimer_update_base(cpu_base);
	__hrtimer_run_queues(cpu_base, now);
	raw_spin_unlock(&cpu_base->lock);
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

void hrtimer_init_sleeper(struct hrtimer_sleeper *sl, struct task_struct *task)
{
	sl->timer.function = hrtimer_wakeup;
	sl->task = task;
}
EXPORT_SYMBOL_GPL(hrtimer_init_sleeper);

static int __sched do_nanosleep(struct hrtimer_sleeper *t, enum hrtimer_mode mode)
{
	hrtimer_init_sleeper(t, current);

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		hrtimer_start_expires(&t->timer, mode);

		if (likely(t->task))
			freezable_schedule();

		hrtimer_cancel(&t->timer);
		mode = HRTIMER_MODE_ABS;

	} while (t->task && !signal_pending(current));

	__set_current_state(TASK_RUNNING);

	return t->task == NULL;
}

static int update_rmtp(struct hrtimer *timer, struct timespec __user *rmtp)
{
	struct timespec rmt;
	ktime_t rem;

	rem = hrtimer_expires_remaining(timer);
	if (rem.tv64 <= 0)
		return 0;
	rmt = ktime_to_timespec(rem);

	if (copy_to_user(rmtp, &rmt, sizeof(*rmtp)))
		return -EFAULT;

	return 1;
}

long __sched hrtimer_nanosleep_restart(struct restart_block *restart)
{
	struct hrtimer_sleeper t;
	struct timespec __user  *rmtp;
	int ret = 0;

	hrtimer_init_on_stack(&t.timer, restart->nanosleep.clockid,
				HRTIMER_MODE_ABS);
	hrtimer_set_expires_tv64(&t.timer, restart->nanosleep.expires);

	if (do_nanosleep(&t, HRTIMER_MODE_ABS))
		goto out;

	rmtp = restart->nanosleep.rmtp;
	if (rmtp) {
		ret = update_rmtp(&t.timer, rmtp);
		if (ret <= 0)
			goto out;
	}

	/* The other values in restart are already filled in */
	ret = -ERESTART_RESTARTBLOCK;
out:
	destroy_hrtimer_on_stack(&t.timer);
	return ret;
}

long hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
		       const enum hrtimer_mode mode, const clockid_t clockid)
{
	struct restart_block *restart;
	struct hrtimer_sleeper t;
	int ret = 0;
	u64 slack;

	slack = current->timer_slack_ns;
	if (dl_task(current) || rt_task(current))
		slack = 0;

	hrtimer_init_on_stack(&t.timer, clockid, mode);
	hrtimer_set_expires_range_ns(&t.timer, timespec_to_ktime(*rqtp), slack);
	if (do_nanosleep(&t, mode))
		goto out;

	/* Absolute timers do not update the rmtp value and restart: */
	if (mode == HRTIMER_MODE_ABS) {
		ret = -ERESTARTNOHAND;
		goto out;
	}

	if (rmtp) {
		ret = update_rmtp(&t.timer, rmtp);
		if (ret <= 0)
			goto out;
	}

	restart = &current->restart_block;
	restart->fn = hrtimer_nanosleep_restart;
	restart->nanosleep.clockid = t.timer.base->clockid;
	restart->nanosleep.rmtp = rmtp;
	restart->nanosleep.expires = hrtimer_get_expires_tv64(&t.timer);

	ret = -ERESTART_RESTARTBLOCK;
out:
	destroy_hrtimer_on_stack(&t.timer);
	return ret;
}

SYSCALL_DEFINE2(nanosleep, struct timespec __user *, rqtp,
		struct timespec __user *, rmtp)
{
	struct timespec tu;

	if (copy_from_user(&tu, rqtp, sizeof(tu)))
		return -EFAULT;

	if (!timespec_valid(&tu))
		return -EINVAL;

	return hrtimer_nanosleep(&tu, rmtp, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
}

/*
 * Functions related to boot-time initialization:
 */
static void init_hrtimers_cpu(int cpu)
{
	struct hrtimer_cpu_base *cpu_base = &per_cpu(hrtimer_bases, cpu);
	int i;

	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
		cpu_base->clock_base[i].cpu_base = cpu_base;
		timerqueue_init_head(&cpu_base->clock_base[i].active);
	}

	cpu_base->cpu = cpu;
	hrtimer_init_hres(cpu_base);
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
		enqueue_hrtimer(timer, new_base);
	}
}

static void migrate_hrtimers(int scpu)
{
	struct hrtimer_cpu_base *old_base, *new_base;
	int i;

	BUG_ON(cpu_online(scpu));
	tick_cancel_sched_timer(scpu);

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

	raw_spin_unlock(&old_base->lock);
	raw_spin_unlock(&new_base->lock);

	/* Check, if we got expired work to do */
	__hrtimer_peek_ahead_timers();
	local_irq_enable();
}

#endif /* CONFIG_HOTPLUG_CPU */

static int hrtimer_cpu_notify(struct notifier_block *self,
					unsigned long action, void *hcpu)
{
	int scpu = (long)hcpu;

	switch (action) {

	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		init_hrtimers_cpu(scpu);
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		migrate_hrtimers(scpu);
		break;
#endif

	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block hrtimers_nb = {
	.notifier_call = hrtimer_cpu_notify,
};

void __init hrtimers_init(void)
{
	hrtimer_cpu_notify(&hrtimers_nb, (unsigned long)CPU_UP_PREPARE,
			  (void *)(long)smp_processor_id());
	register_cpu_notifier(&hrtimers_nb);
}

/**
 * schedule_hrtimeout_range_clock - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @delta:	slack in expires timeout (ktime_t)
 * @mode:	timer mode, HRTIMER_MODE_ABS or HRTIMER_MODE_REL
 * @clock:	timer clock, CLOCK_MONOTONIC or CLOCK_REALTIME
 */
int __sched
schedule_hrtimeout_range_clock(ktime_t *expires, u64 delta,
			       const enum hrtimer_mode mode, int clock)
{
	struct hrtimer_sleeper t;

	/*
	 * Optimize when a zero timeout value is given. It does not
	 * matter whether this is an absolute or a relative time.
	 */
	if (expires && !expires->tv64) {
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

	hrtimer_init_on_stack(&t.timer, clock, mode);
	hrtimer_set_expires_range_ns(&t.timer, *expires, delta);

	hrtimer_init_sleeper(&t, current);

	hrtimer_start_expires(&t.timer, mode);

	if (likely(t.task))
		schedule();

	hrtimer_cancel(&t.timer);
	destroy_hrtimer_on_stack(&t.timer);

	__set_current_state(TASK_RUNNING);

	return !t.task ? 0 : -EINTR;
}

/**
 * schedule_hrtimeout_range - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @delta:	slack in expires timeout (ktime_t)
 * @mode:	timer mode, HRTIMER_MODE_ABS or HRTIMER_MODE_REL
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
 * pass before the routine returns.
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task.
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Returns 0 when the timer has expired otherwise -EINTR
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
 * @mode:	timer mode, HRTIMER_MODE_ABS or HRTIMER_MODE_REL
 *
 * Make the current task sleep until the given expiry time has
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout time is guaranteed to
 * pass before the routine returns.
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task.
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Returns 0 when the timer has expired otherwise -EINTR
 */
int __sched schedule_hrtimeout(ktime_t *expires,
			       const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range(expires, 0, mode);
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout);
