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
 * hrtimer callback modes:
 *
 *	HRTIMER_CB_SOFTIRQ:		Callback must run in softirq context
 *	HRTIMER_CB_IRQSAFE:		Callback may run in hardirq context
 *	HRTIMER_CB_IRQSAFE_NO_RESTART:	Callback may run in hardirq context and
 *					does not restart the timer
 *	HRTIMER_CB_IRQSAFE_NO_SOFTIRQ:	Callback must run in hardirq context
 *					Special mode for tick emultation
 */
enum hrtimer_cb_mode {
	HRTIMER_CB_SOFTIRQ,
	HRTIMER_CB_IRQSAFE,
	HRTIMER_CB_IRQSAFE_NO_RESTART,
	HRTIMER_CB_IRQSAFE_NO_SOFTIRQ,
};

/*
 * Values to track state of the timer
 *
 * Possible states:
 *
 * 0x00		inactive
 * 0x01		enqueued into rbtree
 * 0x02		callback function running
 * 0x04		callback pending (high resolution mode)
 *
 * Special case:
 * 0x03		callback function running and enqueued
 *		(was requeued on another CPU)
 * The "callback function running and enqueued" status is only possible on
 * SMP. It happens for example when a posix timer expired and the callback
 * queued a signal. Between dropping the lock which protects the posix timer
 * and reacquiring the base lock of the hrtimer, another CPU can deliver the
 * signal and rearm the timer. We have to preserve the callback running state,
 * as otherwise the timer could be removed before the softirq code finishes the
 * the handling of the timer.
 *
 * The HRTIMER_STATE_ENQUEUED bit is always or'ed to the current state to
 * preserve the HRTIMER_STATE_CALLBACK bit in the above scenario.
 *
 * All state transitions are protected by cpu_base->lock.
 */
#define HRTIMER_STATE_INACTIVE	0x00
#define HRTIMER_STATE_ENQUEUED	0x01
#define HRTIMER_STATE_CALLBACK	0x02
#define HRTIMER_STATE_PENDING	0x04

/**
 * struct hrtimer - the basic hrtimer structure
 * @node:	red black tree node for time ordered insertion
 * @expires:	the absolute expiry time in the hrtimers internal
 *		representation. The time is related to the clock on
 *		which the timer is based.
 * @function:	timer expiry callback function
 * @base:	pointer to the timer base (per cpu and per clock)
 * @state:	state information (See bit values above)
 * @cb_mode:	high resolution timer feature to select the callback execution
 *		 mode
 * @cb_entry:	list head to enqueue an expired timer into the callback list
 * @start_site:	timer statistics field to store the site where the timer
 *		was started
 * @start_comm: timer statistics field to store the name of the process which
 *		started the timer
 * @start_pid: timer statistics field to store the pid of the task which
 *		started the timer
 *
 * The hrtimer structure must be initialized by hrtimer_init()
 */
struct hrtimer {
	struct rb_node			node;
	ktime_t				expires;
	enum hrtimer_restart		(*function)(struct hrtimer *);
	struct hrtimer_clock_base	*base;
	unsigned long			state;
	enum hrtimer_cb_mode		cb_mode;
	struct list_head		cb_entry;
#ifdef CONFIG_TIMER_STATS
	void				*start_site;
	char				start_comm[16];
	int				start_pid;
#endif
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
 * struct hrtimer_clock_base - the timer base for a specific clock
 * @cpu_base:		per cpu clock base
 * @index:		clock type index for per_cpu support when moving a
 *			timer to a base on another cpu.
 * @active:		red black tree root node for the active timers
 * @first:		pointer to the timer node which expires first
 * @resolution:		the resolution of the clock, in nanoseconds
 * @get_time:		function to retrieve the current time of the clock
 * @get_softirq_time:	function to retrieve the current time from the softirq
 * @softirq_time:	the time when running the hrtimer queue in the softirq
 * @offset:		offset of this clock to the monotonic base
 * @reprogram:		function to reprogram the timer event
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
#ifdef CONFIG_HIGH_RES_TIMERS
	ktime_t			offset;
	int			(*reprogram)(struct hrtimer *t,
					     struct hrtimer_clock_base *b,
					     ktime_t n);
#endif
};

#define HRTIMER_MAX_CLOCK_BASES 2

/*
 * struct hrtimer_cpu_base - the per cpu clock bases
 * @lock:		lock protecting the base and associated clock bases
 *			and timers
 * @clock_base:		array of clock bases for this cpu
 * @curr_timer:		the timer which is executing a callback right now
 * @expires_next:	absolute time of the next event which was scheduled
 *			via clock_set_next_event()
 * @hres_active:	State of high resolution mode
 * @check_clocks:	Indictator, when set evaluate time source and clock
 *			event devices whether high resolution mode can be
 *			activated.
 * @cb_pending:		Expired timers are moved from the rbtree to this
 *			list in the timer interrupt. The list is processed
 *			in the softirq.
 * @nr_events:		Total number of timer interrupt events
 */
struct hrtimer_cpu_base {
	spinlock_t			lock;
	struct hrtimer_clock_base	clock_base[HRTIMER_MAX_CLOCK_BASES];
	struct list_head		cb_pending;
#ifdef CONFIG_HIGH_RES_TIMERS
	ktime_t				expires_next;
	int				hres_active;
	unsigned long			nr_events;
#endif
};

#ifdef CONFIG_HIGH_RES_TIMERS
struct clock_event_device;

extern void clock_was_set(void);
extern void hres_timers_resume(void);
extern void hrtimer_interrupt(struct clock_event_device *dev);

/*
 * In high resolution mode the time reference must be read accurate
 */
static inline ktime_t hrtimer_cb_get_time(struct hrtimer *timer)
{
	return timer->base->get_time();
}

static inline int hrtimer_is_hres_active(struct hrtimer *timer)
{
	return timer->base->cpu_base->hres_active;
}

/*
 * The resolution of the clocks. The resolution value is returned in
 * the clock_getres() system call to give application programmers an
 * idea of the (in)accuracy of timers. Timer values are rounded up to
 * this resolution values.
 */
# define HIGH_RES_NSEC		1
# define KTIME_HIGH_RES		(ktime_t) { .tv64 = HIGH_RES_NSEC }
# define MONOTONIC_RES_NSEC	HIGH_RES_NSEC
# define KTIME_MONOTONIC_RES	KTIME_HIGH_RES

#else

# define MONOTONIC_RES_NSEC	LOW_RES_NSEC
# define KTIME_MONOTONIC_RES	KTIME_LOW_RES

/*
 * clock_was_set() is a NOP for non- high-resolution systems. The
 * time-sorted order guarantees that a timer does not expire early and
 * is expired in the next softirq when the clock was advanced.
 */
static inline void clock_was_set(void) { }

static inline void hres_timers_resume(void) { }

/*
 * In non high resolution mode the time reference is taken from
 * the base softirq time variable.
 */
static inline ktime_t hrtimer_cb_get_time(struct hrtimer *timer)
{
	return timer->base->softirq_time;
}

static inline int hrtimer_is_hres_active(struct hrtimer *timer)
{
	return 0;
}
#endif

extern ktime_t ktime_get(void);
extern ktime_t ktime_get_real(void);

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

extern ktime_t hrtimer_get_next_event(void);

/*
 * A timer is active, when it is enqueued into the rbtree or the callback
 * function is running.
 */
static inline int hrtimer_active(const struct hrtimer *timer)
{
	return timer->state != HRTIMER_STATE_INACTIVE;
}

/*
 * Helper function to check, whether the timer is on one of the queues
 */
static inline int hrtimer_is_queued(struct hrtimer *timer)
{
	return timer->state &
		(HRTIMER_STATE_ENQUEUED | HRTIMER_STATE_PENDING);
}

/* Forward a hrtimer so it expires after now: */
extern u64
hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval);

/* Forward a hrtimer so it expires after the hrtimer's current now */
static inline u64 hrtimer_forward_now(struct hrtimer *timer,
				      ktime_t interval)
{
	return hrtimer_forward(timer, timer->base->get_time(), interval);
}

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
extern void hrtimer_run_pending(void);

/* Bootup initialization: */
extern void __init hrtimers_init(void);

#if BITS_PER_LONG < 64
extern u64 ktime_divns(const ktime_t kt, s64 div);
#else /* BITS_PER_LONG < 64 */
# define ktime_divns(kt, div)		(u64)((kt).tv64 / (div))
#endif

/* Show pending timers: */
extern void sysrq_timer_list_show(void);

/*
 * Timer-statistics info:
 */
#ifdef CONFIG_TIMER_STATS

extern void timer_stats_update_stats(void *timer, pid_t pid, void *startf,
				     void *timerf, char *comm,
				     unsigned int timer_flag);

static inline void timer_stats_account_hrtimer(struct hrtimer *timer)
{
	timer_stats_update_stats(timer, timer->start_pid, timer->start_site,
				 timer->function, timer->start_comm, 0);
}

extern void __timer_stats_hrtimer_set_start_info(struct hrtimer *timer,
						 void *addr);

static inline void timer_stats_hrtimer_set_start_info(struct hrtimer *timer)
{
	__timer_stats_hrtimer_set_start_info(timer, __builtin_return_address(0));
}

static inline void timer_stats_hrtimer_clear_start_info(struct hrtimer *timer)
{
	timer->start_site = NULL;
}
#else
static inline void timer_stats_account_hrtimer(struct hrtimer *timer)
{
}

static inline void timer_stats_hrtimer_set_start_info(struct hrtimer *timer)
{
}

static inline void timer_stats_hrtimer_clear_start_info(struct hrtimer *timer)
{
}
#endif

#endif
