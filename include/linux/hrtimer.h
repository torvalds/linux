/*
 *  include/linux/hrtimer.h
 *
 *  hrtimers - High-resolution kernel timers
 *
 *   Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 *   Copyright(C) 2005, Red Hat, Inc., Ingo Molnar
 *
 *  data type definitions, declarations, prototypes
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  For licencing details see kernel-base/COPYING
 */
#ifndef _LINUX_HRTIMER_H
#define _LINUX_HRTIMER_H

#include <linux/rbtree.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/wait.h>

struct hrtimer_clock_base;
struct hrtimer_cpu_base;

/*
 * Mode arguments of xxx_hrtimer functions:
 */
enum hrtimer_mode {
	HRTIMER_MODE_ABS,	/* Time value is absolute */
	HRTIMER_MODE_REL,	/* Time value is relative to now */
};

/*
 * Return values for the callback function
 */
enum hrtimer_restart {
	HRTIMER_NORESTART,	/* Timer is not restarted */
	HRTIMER_RESTART,	/* Timer must be restarted */
};

/*
 * Bit values to track state of the timer
 *
 * Possible states:
 *
 * 0x00		inactive
 * 0x01		enqueued into rbtree
 * 0x02		callback function running
 * 0x03		callback function running and enqueued
 *		(was requeued on another CPU)
 *
 * The "callback function running and enqueued" status is only possible on
 * SMP. It happens for example when a posix timer expired and the callback
 * queued a signal. Between dropping the lock which protects the posix timer
 * and reacquiring the base lock of the hrtimer, another CPU can deliver the
 * signal and rearm the timer. We have to preserve the callback running state,
 * as otherwise the timer could be removed before the softirq code finishes the
 * the handling of the timer.
 *
 * The HRTIMER_STATE_ENQUEUE bit is always or'ed to the current state to
 * preserve the HRTIMER_STATE_CALLBACK bit in the above scenario.
 *
 * All state transitions are protected by cpu_base->lock.
 */
#define HRTIMER_STATE_INACTIVE	0x00
#define HRTIMER_STATE_ENQUEUED	0x01
#define HRTIMER_STATE_CALLBACK	0x02

/**
 * struct hrtimer - the basic hrtimer structure
 * @node:	red black tree node for time ordered insertion
 * @expires:	the absolute expiry time in the hrtimers internal
 *		representation. The time is related to the clock on
 *		which the timer is based.
 * @function:	timer expiry callback function
 * @base:	pointer to the timer base (per cpu and per clock)
 * @state:	state information (See bit values above)
 *
 * The hrtimer structure must be initialized by init_hrtimer_#CLOCKTYPE()
 */
struct hrtimer {
	struct rb_node			node;
	ktime_t				expires;
	enum hrtimer_restart		(*function)(struct hrtimer *);
	struct hrtimer_clock_base	*base;
	unsigned long			state;
};

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

/**
 * struct hrtimer_base - the timer base for a specific clock
 * @index:		clock type index for per_cpu support when moving a
 *			timer to a base on another cpu.
 * @active:		red black tree root node for the active timers
 * @first:		pointer to the timer node which expires first
 * @resolution:		the resolution of the clock, in nanoseconds
 * @get_time:		function to retrieve the current time of the clock
 * @get_softirq_time:	function to retrieve the current time from the softirq
 * @softirq_time:	the time when running the hrtimer queue in the softirq
 */
struct hrtimer_clock_base {
	struct hrtimer_cpu_base	*cpu_base;
	clockid_t		index;
	struct rb_root		active;
	struct rb_node		*first;
	ktime_t			resolution;
	ktime_t			(*get_time)(void);
	ktime_t			(*get_softirq_time)(void);
	ktime_t			softirq_time;
};

#define HRTIMER_MAX_CLOCK_BASES 2

/*
 * struct hrtimer_cpu_base - the per cpu clock bases
 * @lock:		lock protecting the base and associated clock bases
 *			and timers
 * @lock_key:		the lock_class_key for use with lockdep
 * @clock_base:		array of clock bases for this cpu
 * @curr_timer:		the timer which is executing a callback right now
 */
struct hrtimer_cpu_base {
	spinlock_t			lock;
	struct lock_class_key		lock_key;
	struct hrtimer_clock_base	clock_base[HRTIMER_MAX_CLOCK_BASES];
};

/*
 * clock_was_set() is a NOP for non- high-resolution systems. The
 * time-sorted order guarantees that a timer does not expire early and
 * is expired in the next softirq when the clock was advanced.
 */
#define clock_was_set()		do { } while (0)

/* Exported timer functions: */

/* Initialize timers: */
extern void hrtimer_init(struct hrtimer *timer, clockid_t which_clock,
			 enum hrtimer_mode mode);

/* Basic timer operations: */
extern int hrtimer_start(struct hrtimer *timer, ktime_t tim,
			 const enum hrtimer_mode mode);
extern int hrtimer_cancel(struct hrtimer *timer);
extern int hrtimer_try_to_cancel(struct hrtimer *timer);

static inline int hrtimer_restart(struct hrtimer *timer)
{
	return hrtimer_start(timer, timer->expires, HRTIMER_MODE_ABS);
}

/* Query timers: */
extern ktime_t hrtimer_get_remaining(const struct hrtimer *timer);
extern int hrtimer_get_res(const clockid_t which_clock, struct timespec *tp);

#ifdef CONFIG_NO_IDLE_HZ
extern ktime_t hrtimer_get_next_event(void);
#endif

/*
 * A timer is active, when it is enqueued into the rbtree or the callback
 * function is running.
 */
static inline int hrtimer_active(const struct hrtimer *timer)
{
	return timer->state != HRTIMER_STATE_INACTIVE;
}

/* Forward a hrtimer so it expires after now: */
extern unsigned long
hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval);

/* Precise sleep: */
extern long hrtimer_nanosleep(struct timespec *rqtp,
			      struct timespec __user *rmtp,
			      const enum hrtimer_mode mode,
			      const clockid_t clockid);
extern long hrtimer_nanosleep_restart(struct restart_block *restart_block);

extern void hrtimer_init_sleeper(struct hrtimer_sleeper *sl,
				 struct task_struct *tsk);

/* Soft interrupt function to run the hrtimer queues: */
extern void hrtimer_run_queues(void);

/* Resume notification */
void hrtimer_notify_resume(void);

/* Bootup initialization: */
extern void __init hrtimers_init(void);

#endif
