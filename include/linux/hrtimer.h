// SPDX-License-Identifier: GPL-2.0
/*
 *  hrtimers - High-resolution kernel timers
 *
 *   Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 *   Copyright(C) 2005, Red Hat, Inc., Ingo Molnar
 *
 *  data type definitions, declarations, prototypes
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 */
#ifndef _LINUX_HRTIMER_H
#define _LINUX_HRTIMER_H

#include <linux/hrtimer_defs.h>
#include <linux/hrtimer_types.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/percpu-defs.h>
#include <linux/rbtree.h>
#include <linux/timer.h>

/*
 * Mode arguments of xxx_hrtimer functions:
 *
 * HRTIMER_MODE_ABS		- Time value is absolute
 * HRTIMER_MODE_REL		- Time value is relative to now
 * HRTIMER_MODE_PINNED		- Timer is bound to CPU (is only considered
 *				  when starting the timer)
 * HRTIMER_MODE_SOFT		- Timer callback function will be executed in
 *				  soft irq context
 * HRTIMER_MODE_HARD		- Timer callback function will be executed in
 *				  hard irq context even on PREEMPT_RT.
 */
enum hrtimer_mode {
	HRTIMER_MODE_ABS	= 0x00,
	HRTIMER_MODE_REL	= 0x01,
	HRTIMER_MODE_PINNED	= 0x02,
	HRTIMER_MODE_SOFT	= 0x04,
	HRTIMER_MODE_HARD	= 0x08,

	HRTIMER_MODE_ABS_PINNED = HRTIMER_MODE_ABS | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_REL_PINNED = HRTIMER_MODE_REL | HRTIMER_MODE_PINNED,

	HRTIMER_MODE_ABS_SOFT	= HRTIMER_MODE_ABS | HRTIMER_MODE_SOFT,
	HRTIMER_MODE_REL_SOFT	= HRTIMER_MODE_REL | HRTIMER_MODE_SOFT,

	HRTIMER_MODE_ABS_PINNED_SOFT = HRTIMER_MODE_ABS_PINNED | HRTIMER_MODE_SOFT,
	HRTIMER_MODE_REL_PINNED_SOFT = HRTIMER_MODE_REL_PINNED | HRTIMER_MODE_SOFT,

	HRTIMER_MODE_ABS_HARD	= HRTIMER_MODE_ABS | HRTIMER_MODE_HARD,
	HRTIMER_MODE_REL_HARD	= HRTIMER_MODE_REL | HRTIMER_MODE_HARD,

	HRTIMER_MODE_ABS_PINNED_HARD = HRTIMER_MODE_ABS_PINNED | HRTIMER_MODE_HARD,
	HRTIMER_MODE_REL_PINNED_HARD = HRTIMER_MODE_REL_PINNED | HRTIMER_MODE_HARD,
};

/*
 * Values to track state of the timer
 *
 * Possible states:
 *
 * 0x00		inactive
 * 0x01		enqueued into rbtree
 *
 * The callback state is not part of the timer->state because clearing it would
 * mean touching the timer after the callback, this makes it impossible to free
 * the timer from the callback function.
 *
 * Therefore we track the callback state in:
 *
 *	timer->base->cpu_base->running == timer
 *
 * On SMP it is possible to have a "callback function running and enqueued"
 * status. It happens for example when a posix timer expired and the callback
 * queued a signal. Between dropping the lock which protects the posix timer
 * and reacquiring the base lock of the hrtimer, another CPU can deliver the
 * signal and rearm the timer.
 *
 * All state transitions are protected by cpu_base->lock.
 */
#define HRTIMER_STATE_INACTIVE	0x00
#define HRTIMER_STATE_ENQUEUED	0x01

/**
 * struct hrtimer_sleeper - simple sleeper structure
 * @timer:	embedded timer structure
 * @task:	task to wake up
 *
 * task is set to NULL, when the timer expires.
 */
struct hrtimer_sleeper {
	struct hrtimer timer;
	struct task_struct *task;
};

static inline void hrtimer_set_expires(struct hrtimer *timer, ktime_t time)
{
	timer->node.expires = time;
	timer->_softexpires = time;
}

static inline void hrtimer_set_expires_range(struct hrtimer *timer, ktime_t time, ktime_t delta)
{
	timer->_softexpires = time;
	timer->node.expires = ktime_add_safe(time, delta);
}

static inline void hrtimer_set_expires_range_ns(struct hrtimer *timer, ktime_t time, u64 delta)
{
	timer->_softexpires = time;
	timer->node.expires = ktime_add_safe(time, ns_to_ktime(delta));
}

static inline void hrtimer_set_expires_tv64(struct hrtimer *timer, s64 tv64)
{
	timer->node.expires = tv64;
	timer->_softexpires = tv64;
}

static inline void hrtimer_add_expires(struct hrtimer *timer, ktime_t time)
{
	timer->node.expires = ktime_add_safe(timer->node.expires, time);
	timer->_softexpires = ktime_add_safe(timer->_softexpires, time);
}

static inline void hrtimer_add_expires_ns(struct hrtimer *timer, u64 ns)
{
	timer->node.expires = ktime_add_ns(timer->node.expires, ns);
	timer->_softexpires = ktime_add_ns(timer->_softexpires, ns);
}

static inline ktime_t hrtimer_get_expires(const struct hrtimer *timer)
{
	return timer->node.expires;
}

static inline ktime_t hrtimer_get_softexpires(const struct hrtimer *timer)
{
	return timer->_softexpires;
}

static inline s64 hrtimer_get_expires_tv64(const struct hrtimer *timer)
{
	return timer->node.expires;
}
static inline s64 hrtimer_get_softexpires_tv64(const struct hrtimer *timer)
{
	return timer->_softexpires;
}

static inline s64 hrtimer_get_expires_ns(const struct hrtimer *timer)
{
	return ktime_to_ns(timer->node.expires);
}

static inline ktime_t hrtimer_expires_remaining(const struct hrtimer *timer)
{
	return ktime_sub(timer->node.expires, timer->base->get_time());
}

static inline ktime_t hrtimer_cb_get_time(struct hrtimer *timer)
{
	return timer->base->get_time();
}

static inline int hrtimer_is_hres_active(struct hrtimer *timer)
{
	return IS_ENABLED(CONFIG_HIGH_RES_TIMERS) ?
		timer->base->cpu_base->hres_active : 0;
}

#ifdef CONFIG_HIGH_RES_TIMERS
struct clock_event_device;

extern void hrtimer_interrupt(struct clock_event_device *dev);

extern unsigned int hrtimer_resolution;

#else

#define hrtimer_resolution	(unsigned int)LOW_RES_NSEC

#endif

static inline ktime_t
__hrtimer_expires_remaining_adjusted(const struct hrtimer *timer, ktime_t now)
{
	ktime_t rem = ktime_sub(timer->node.expires, now);

	/*
	 * Adjust relative timers for the extra we added in
	 * hrtimer_start_range_ns() to prevent short timeouts.
	 */
	if (IS_ENABLED(CONFIG_TIME_LOW_RES) && timer->is_rel)
		rem -= hrtimer_resolution;
	return rem;
}

static inline ktime_t
hrtimer_expires_remaining_adjusted(const struct hrtimer *timer)
{
	return __hrtimer_expires_remaining_adjusted(timer,
						    timer->base->get_time());
}

#ifdef CONFIG_TIMERFD
extern void timerfd_clock_was_set(void);
extern void timerfd_resume(void);
#else
static inline void timerfd_clock_was_set(void) { }
static inline void timerfd_resume(void) { }
#endif

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);

#ifdef CONFIG_PREEMPT_RT
void hrtimer_cancel_wait_running(const struct hrtimer *timer);
#else
static inline void hrtimer_cancel_wait_running(struct hrtimer *timer)
{
	cpu_relax();
}
#endif

static inline enum hrtimer_restart hrtimer_dummy_timeout(struct hrtimer *unused)
{
	return HRTIMER_NORESTART;
}

/* Exported timer functions: */

/* Initialize timers: */
extern void hrtimer_setup(struct hrtimer *timer, enum hrtimer_restart (*function)(struct hrtimer *),
			  clockid_t clock_id, enum hrtimer_mode mode);
extern void hrtimer_setup_on_stack(struct hrtimer *timer,
				   enum hrtimer_restart (*function)(struct hrtimer *),
				   clockid_t clock_id, enum hrtimer_mode mode);
extern void hrtimer_setup_sleeper_on_stack(struct hrtimer_sleeper *sl, clockid_t clock_id,
					   enum hrtimer_mode mode);

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS
extern void destroy_hrtimer_on_stack(struct hrtimer *timer);
#else
static inline void destroy_hrtimer_on_stack(struct hrtimer *timer) { }
#endif

/* Basic timer operations: */
extern void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
				   u64 range_ns, const enum hrtimer_mode mode);

/**
 * hrtimer_start - (re)start an hrtimer
 * @timer:	the timer to be added
 * @tim:	expiry time
 * @mode:	timer mode: absolute (HRTIMER_MODE_ABS) or
 *		relative (HRTIMER_MODE_REL), and pinned (HRTIMER_MODE_PINNED);
 *		softirq based mode is considered for debug purpose only!
 */
static inline void hrtimer_start(struct hrtimer *timer, ktime_t tim,
				 const enum hrtimer_mode mode)
{
	hrtimer_start_range_ns(timer, tim, 0, mode);
}

extern int hrtimer_cancel(struct hrtimer *timer);
extern int hrtimer_try_to_cancel(struct hrtimer *timer);

static inline void hrtimer_start_expires(struct hrtimer *timer,
					 enum hrtimer_mode mode)
{
	u64 delta;
	ktime_t soft, hard;
	soft = hrtimer_get_softexpires(timer);
	hard = hrtimer_get_expires(timer);
	delta = ktime_to_ns(ktime_sub(hard, soft));
	hrtimer_start_range_ns(timer, soft, delta, mode);
}

void hrtimer_sleeper_start_expires(struct hrtimer_sleeper *sl,
				   enum hrtimer_mode mode);

static inline void hrtimer_restart(struct hrtimer *timer)
{
	hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
}

/* Query timers: */
extern ktime_t __hrtimer_get_remaining(const struct hrtimer *timer, bool adjust);

/**
 * hrtimer_get_remaining - get remaining time for the timer
 * @timer:	the timer to read
 */
static inline ktime_t hrtimer_get_remaining(const struct hrtimer *timer)
{
	return __hrtimer_get_remaining(timer, false);
}

extern u64 hrtimer_get_next_event(void);
extern u64 hrtimer_next_event_without(const struct hrtimer *exclude);

extern bool hrtimer_active(const struct hrtimer *timer);

/**
 * hrtimer_is_queued - check, whether the timer is on one of the queues
 * @timer:	Timer to check
 *
 * Returns: True if the timer is queued, false otherwise
 *
 * The function can be used lockless, but it gives only a current snapshot.
 */
static inline bool hrtimer_is_queued(struct hrtimer *timer)
{
	/* The READ_ONCE pairs with the update functions of timer->state */
	return !!(READ_ONCE(timer->state) & HRTIMER_STATE_ENQUEUED);
}

/*
 * Helper function to check, whether the timer is running the callback
 * function
 */
static inline int hrtimer_callback_running(struct hrtimer *timer)
{
	return timer->base->running == timer;
}

/**
 * hrtimer_update_function - Update the timer's callback function
 * @timer:	Timer to update
 * @function:	New callback function
 *
 * Only safe to call if the timer is not enqueued. Can be called in the callback function if the
 * timer is not enqueued at the same time (see the comments above HRTIMER_STATE_ENQUEUED).
 */
static inline void hrtimer_update_function(struct hrtimer *timer,
					   enum hrtimer_restart (*function)(struct hrtimer *))
{
#ifdef CONFIG_PROVE_LOCKING
	guard(raw_spinlock_irqsave)(&timer->base->cpu_base->lock);

	if (WARN_ON_ONCE(hrtimer_is_queued(timer)))
		return;

	if (WARN_ON_ONCE(!function))
		return;
#endif
	ACCESS_PRIVATE(timer, function) = function;
}

/* Forward a hrtimer so it expires after now: */
extern u64
hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval);

/**
 * hrtimer_forward_now() - forward the timer expiry so it expires after now
 * @timer:	hrtimer to forward
 * @interval:	the interval to forward
 *
 * It is a variant of hrtimer_forward(). The timer will expire after the current
 * time of the hrtimer clock base. See hrtimer_forward() for details.
 */
static inline u64 hrtimer_forward_now(struct hrtimer *timer,
				      ktime_t interval)
{
	return hrtimer_forward(timer, timer->base->get_time(), interval);
}

/* Precise sleep: */

extern int nanosleep_copyout(struct restart_block *, struct timespec64 *);
extern long hrtimer_nanosleep(ktime_t rqtp, const enum hrtimer_mode mode,
			      const clockid_t clockid);

extern int schedule_hrtimeout_range(ktime_t *expires, u64 delta,
				    const enum hrtimer_mode mode);
extern int schedule_hrtimeout_range_clock(ktime_t *expires,
					  u64 delta,
					  const enum hrtimer_mode mode,
					  clockid_t clock_id);
extern int schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode);

/* Soft interrupt function to run the hrtimer queues: */
extern void hrtimer_run_queues(void);

/* Bootup initialization: */
extern void __init hrtimers_init(void);

/* Show pending timers: */
extern void sysrq_timer_list_show(void);

int hrtimers_prepare_cpu(unsigned int cpu);
int hrtimers_cpu_starting(unsigned int cpu);
#ifdef CONFIG_HOTPLUG_CPU
int hrtimers_cpu_dying(unsigned int cpu);
#else
#define hrtimers_cpu_dying	NULL
#endif

#endif
