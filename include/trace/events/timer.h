#undef TRACE_SYSTEM
#define TRACE_SYSTEM timer

#if !defined(_TRACE_TIMER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TIMER_H

#include <linux/tracepoint.h>
#include <linux/hrtimer.h>
#include <linux/timer.h>

DECLARE_EVENT_CLASS(timer_class,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
	),

	TP_printk("timer=%p", __entry->timer)
);

/**
 * timer_init - called when the timer is initialized
 * @timer:	pointer to struct timer_list
 */
DEFINE_EVENT(timer_class, timer_init,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer)
);

/**
 * timer_start - called when the timer is started
 * @timer:	pointer to struct timer_list
 * @expires:	the timers expiry time
 */
TRACE_EVENT(timer_start,

	TP_PROTO(struct timer_list *timer,
		unsigned long expires,
		unsigned int flags),

	TP_ARGS(timer, expires, flags),

	TP_STRUCT__entry(
		__field( void *,	timer		)
		__field( void *,	function	)
		__field( unsigned long,	expires		)
		__field( unsigned long,	now		)
		__field( unsigned int,	flags		)
	),

	TP_fast_assign(
		__entry->timer		= timer;
		__entry->function	= timer->function;
		__entry->expires	= expires;
		__entry->now		= jiffies;
		__entry->flags		= flags;
	),

	TP_printk("timer=%p function=%pf expires=%lu [timeout=%ld] flags=0x%08x",
		  __entry->timer, __entry->function, __entry->expires,
		  (long)__entry->expires - __entry->now, __entry->flags)
);

/**
 * timer_expire_entry - called immediately before the timer callback
 * @timer:	pointer to struct timer_list
 *
 * Allows to determine the timer latency.
 */
TRACE_EVENT(timer_expire_entry,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer	)
		__field( unsigned long,	now	)
		__field( void *,	function)
	),

	TP_fast_assign(
		__entry->timer		= timer;
		__entry->now		= jiffies;
		__entry->function	= timer->function;
	),

	TP_printk("timer=%p function=%pf now=%lu", __entry->timer, __entry->function,__entry->now)
);

/**
 * timer_expire_exit - called immediately after the timer callback returns
 * @timer:	pointer to struct timer_list
 *
 * When used in combination with the timer_expire_entry tracepoint we can
 * determine the runtime of the timer callback function.
 *
 * NOTE: Do NOT derefernce timer in TP_fast_assign. The pointer might
 * be invalid. We solely track the pointer.
 */
DEFINE_EVENT(timer_class, timer_expire_exit,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer)
);

/**
 * timer_cancel - called when the timer is canceled
 * @timer:	pointer to struct timer_list
 */
DEFINE_EVENT(timer_class, timer_cancel,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer)
);

/**
 * hrtimer_init - called when the hrtimer is initialized
 * @hrtimer:	pointer to struct hrtimer
 * @clockid:	the hrtimers clock
 * @mode:	the hrtimers mode
 */
TRACE_EVENT(hrtimer_init,

	TP_PROTO(struct hrtimer *hrtimer, clockid_t clockid,
		 enum hrtimer_mode mode),

	TP_ARGS(hrtimer, clockid, mode),

	TP_STRUCT__entry(
		__field( void *,		hrtimer		)
		__field( clockid_t,		clockid		)
		__field( enum hrtimer_mode,	mode		)
	),

	TP_fast_assign(
		__entry->hrtimer	= hrtimer;
		__entry->clockid	= clockid;
		__entry->mode		= mode;
	),

	TP_printk("hrtimer=%p clockid=%s mode=%s", __entry->hrtimer,
		  __entry->clockid == CLOCK_REALTIME ?
			"CLOCK_REALTIME" : "CLOCK_MONOTONIC",
		  __entry->mode == HRTIMER_MODE_ABS ?
			"HRTIMER_MODE_ABS" : "HRTIMER_MODE_REL")
);

/**
 * hrtimer_start - called when the hrtimer is started
 * @hrtimer: pointer to struct hrtimer
 */
TRACE_EVENT(hrtimer_start,

	TP_PROTO(struct hrtimer *hrtimer),

	TP_ARGS(hrtimer),

	TP_STRUCT__entry(
		__field( void *,	hrtimer		)
		__field( void *,	function	)
		__field( s64,		expires		)
		__field( s64,		softexpires	)
	),

	TP_fast_assign(
		__entry->hrtimer	= hrtimer;
		__entry->function	= hrtimer->function;
		__entry->expires	= hrtimer_get_expires(hrtimer).tv64;
		__entry->softexpires	= hrtimer_get_softexpires(hrtimer).tv64;
	),

	TP_printk("hrtimer=%p function=%pf expires=%llu softexpires=%llu",
		  __entry->hrtimer, __entry->function,
		  (unsigned long long)ktime_to_ns((ktime_t) {
				  .tv64 = __entry->expires }),
		  (unsigned long long)ktime_to_ns((ktime_t) {
				  .tv64 = __entry->softexpires }))
);

/**
 * hrtimer_expire_entry - called immediately before the hrtimer callback
 * @hrtimer:	pointer to struct hrtimer
 * @now:	pointer to variable which contains current time of the
 *		timers base.
 *
 * Allows to determine the timer latency.
 */
TRACE_EVENT(hrtimer_expire_entry,

	TP_PROTO(struct hrtimer *hrtimer, ktime_t *now),

	TP_ARGS(hrtimer, now),

	TP_STRUCT__entry(
		__field( void *,	hrtimer	)
		__field( s64,		now	)
		__field( void *,	function)
	),

	TP_fast_assign(
		__entry->hrtimer	= hrtimer;
		__entry->now		= now->tv64;
		__entry->function	= hrtimer->function;
	),

	TP_printk("hrtimer=%p function=%pf now=%llu", __entry->hrtimer, __entry->function,
		  (unsigned long long)ktime_to_ns((ktime_t) { .tv64 = __entry->now }))
 );

DECLARE_EVENT_CLASS(hrtimer_class,

	TP_PROTO(struct hrtimer *hrtimer),

	TP_ARGS(hrtimer),

	TP_STRUCT__entry(
		__field( void *,	hrtimer	)
	),

	TP_fast_assign(
		__entry->hrtimer	= hrtimer;
	),

	TP_printk("hrtimer=%p", __entry->hrtimer)
);

/**
 * hrtimer_expire_exit - called immediately after the hrtimer callback returns
 * @hrtimer:	pointer to struct hrtimer
 *
 * When used in combination with the hrtimer_expire_entry tracepoint we can
 * determine the runtime of the callback function.
 */
DEFINE_EVENT(hrtimer_class, hrtimer_expire_exit,

	TP_PROTO(struct hrtimer *hrtimer),

	TP_ARGS(hrtimer)
);

/**
 * hrtimer_cancel - called when the hrtimer is canceled
 * @hrtimer:	pointer to struct hrtimer
 */
DEFINE_EVENT(hrtimer_class, hrtimer_cancel,

	TP_PROTO(struct hrtimer *hrtimer),

	TP_ARGS(hrtimer)
);

/**
 * itimer_state - called when itimer is started or canceled
 * @which:	name of the interval timer
 * @value:	the itimers value, itimer is canceled if value->it_value is
 *		zero, otherwise it is started
 * @expires:	the itimers expiry time
 */
TRACE_EVENT(itimer_state,

	TP_PROTO(int which, const struct itimerval *const value,
		 cputime_t expires),

	TP_ARGS(which, value, expires),

	TP_STRUCT__entry(
		__field(	int,		which		)
		__field(	cputime_t,	expires		)
		__field(	long,		value_sec	)
		__field(	long,		value_usec	)
		__field(	long,		interval_sec	)
		__field(	long,		interval_usec	)
	),

	TP_fast_assign(
		__entry->which		= which;
		__entry->expires	= expires;
		__entry->value_sec	= value->it_value.tv_sec;
		__entry->value_usec	= value->it_value.tv_usec;
		__entry->interval_sec	= value->it_interval.tv_sec;
		__entry->interval_usec	= value->it_interval.tv_usec;
	),

	TP_printk("which=%d expires=%llu it_value=%ld.%ld it_interval=%ld.%ld",
		  __entry->which, (unsigned long long)__entry->expires,
		  __entry->value_sec, __entry->value_usec,
		  __entry->interval_sec, __entry->interval_usec)
);

/**
 * itimer_expire - called when itimer expires
 * @which:	type of the interval timer
 * @pid:	pid of the process which owns the timer
 * @now:	current time, used to calculate the latency of itimer
 */
TRACE_EVENT(itimer_expire,

	TP_PROTO(int which, struct pid *pid, cputime_t now),

	TP_ARGS(which, pid, now),

	TP_STRUCT__entry(
		__field( int ,		which	)
		__field( pid_t,		pid	)
		__field( cputime_t,	now	)
	),

	TP_fast_assign(
		__entry->which	= which;
		__entry->now	= now;
		__entry->pid	= pid_nr(pid);
	),

	TP_printk("which=%d pid=%d now=%llu", __entry->which,
		  (int) __entry->pid, (unsigned long long)__entry->now)
);

#ifdef CONFIG_NO_HZ_COMMON

#define TICK_DEP_NAMES					\
		tick_dep_mask_name(NONE)		\
		tick_dep_name(POSIX_TIMER)		\
		tick_dep_name(PERF_EVENTS)		\
		tick_dep_name(SCHED)			\
		tick_dep_name_end(CLOCK_UNSTABLE)

#undef tick_dep_name
#undef tick_dep_mask_name
#undef tick_dep_name_end

/* The MASK will convert to their bits and they need to be processed too */
#define tick_dep_name(sdep) TRACE_DEFINE_ENUM(TICK_DEP_BIT_##sdep); \
	TRACE_DEFINE_ENUM(TICK_DEP_MASK_##sdep);
#define tick_dep_name_end(sdep)  TRACE_DEFINE_ENUM(TICK_DEP_BIT_##sdep); \
	TRACE_DEFINE_ENUM(TICK_DEP_MASK_##sdep);
/* NONE only has a mask defined for it */
#define tick_dep_mask_name(sdep) TRACE_DEFINE_ENUM(TICK_DEP_MASK_##sdep);

TICK_DEP_NAMES

#undef tick_dep_name
#undef tick_dep_mask_name
#undef tick_dep_name_end

#define tick_dep_name(sdep) { TICK_DEP_MASK_##sdep, #sdep },
#define tick_dep_mask_name(sdep) { TICK_DEP_MASK_##sdep, #sdep },
#define tick_dep_name_end(sdep) { TICK_DEP_MASK_##sdep, #sdep }

#define show_tick_dep_name(val)				\
	__print_symbolic(val, TICK_DEP_NAMES)

TRACE_EVENT(tick_stop,

	TP_PROTO(int success, int dependency),

	TP_ARGS(success, dependency),

	TP_STRUCT__entry(
		__field( int ,		success	)
		__field( int ,		dependency )
	),

	TP_fast_assign(
		__entry->success	= success;
		__entry->dependency	= dependency;
	),

	TP_printk("success=%d dependency=%s",  __entry->success, \
			show_tick_dep_name(__entry->dependency))
);
#endif

#endif /*  _TRACE_TIMER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
