#undef TRACE_SYSTEM
#define TRACE_SYSTEM timer

#if !defined(_TRACE_TIMER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TIMER_H

#include <linux/tracepoint.h>
#include <linux/hrtimer.h>
#include <linux/timer.h>

/**
 * timer_init - called when the timer is initialized
 * @timer:	pointer to struct timer_list
 */
TRACE_EVENT(timer_init,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
	),

	TP_printk("timer %p", __entry->timer)
);

/**
 * timer_start - called when the timer is started
 * @timer:	pointer to struct timer_list
 * @expires:	the timers expiry time
 */
TRACE_EVENT(timer_start,

	TP_PROTO(struct timer_list *timer, unsigned long expires),

	TP_ARGS(timer, expires),

	TP_STRUCT__entry(
		__field( void *,	timer		)
		__field( void *,	function	)
		__field( unsigned long,	expires		)
		__field( unsigned long,	now		)
	),

	TP_fast_assign(
		__entry->timer		= timer;
		__entry->function	= timer->function;
		__entry->expires	= expires;
		__entry->now		= jiffies;
	),

	TP_printk("timer %p: func %pf, expires %lu, timeout %ld",
		  __entry->timer, __entry->function, __entry->expires,
		  (long)__entry->expires - __entry->now)
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
	),

	TP_fast_assign(
		__entry->timer		= timer;
		__entry->now		= jiffies;
	),

	TP_printk("timer %p: now %lu", __entry->timer, __entry->now)
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
TRACE_EVENT(timer_expire_exit,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field(void *,	timer	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
	),

	TP_printk("timer %p", __entry->timer)
);

/**
 * timer_cancel - called when the timer is canceled
 * @timer:	pointer to struct timer_list
 */
TRACE_EVENT(timer_cancel,

	TP_PROTO(struct timer_list *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
	),

	TP_printk("timer %p", __entry->timer)
);

/**
 * hrtimer_init - called when the hrtimer is initialized
 * @timer:	pointer to struct hrtimer
 * @clockid:	the hrtimers clock
 * @mode:	the hrtimers mode
 */
TRACE_EVENT(hrtimer_init,

	TP_PROTO(struct hrtimer *timer, clockid_t clockid,
		 enum hrtimer_mode mode),

	TP_ARGS(timer, clockid, mode),

	TP_STRUCT__entry(
		__field( void *,		timer		)
		__field( clockid_t,		clockid		)
		__field( enum hrtimer_mode,	mode		)
	),

	TP_fast_assign(
		__entry->timer		= timer;
		__entry->clockid	= clockid;
		__entry->mode		= mode;
	),

	TP_printk("hrtimer %p, clockid %s, mode %s", __entry->timer,
		  __entry->clockid == CLOCK_REALTIME ?
			"CLOCK_REALTIME" : "CLOCK_MONOTONIC",
		  __entry->mode == HRTIMER_MODE_ABS ?
			"HRTIMER_MODE_ABS" : "HRTIMER_MODE_REL")
);

/**
 * hrtimer_start - called when the hrtimer is started
 * @timer: pointer to struct hrtimer
 */
TRACE_EVENT(hrtimer_start,

	TP_PROTO(struct hrtimer *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer		)
		__field( void *,	function	)
		__field( s64,		expires		)
		__field( s64,		softexpires	)
	),

	TP_fast_assign(
		__entry->timer		= timer;
		__entry->function	= timer->function;
		__entry->expires	= hrtimer_get_expires(timer).tv64;
		__entry->softexpires	= hrtimer_get_softexpires(timer).tv64;
	),

	TP_printk("hrtimer %p, func %pf, expires %llu, softexpires %llu",
		  __entry->timer, __entry->function,
		  (unsigned long long)ktime_to_ns((ktime_t) {
				  .tv64 = __entry->expires }),
		  (unsigned long long)ktime_to_ns((ktime_t) {
				  .tv64 = __entry->softexpires }))
);

/**
 * htimmer_expire_entry - called immediately before the hrtimer callback
 * @timer:	pointer to struct hrtimer
 * @now:	pointer to variable which contains current time of the
 *		timers base.
 *
 * Allows to determine the timer latency.
 */
TRACE_EVENT(hrtimer_expire_entry,

	TP_PROTO(struct hrtimer *timer, ktime_t *now),

	TP_ARGS(timer, now),

	TP_STRUCT__entry(
		__field( void *,	timer	)
		__field( s64,		now	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
		__entry->now	= now->tv64;
	),

	TP_printk("hrtimer %p, now %llu", __entry->timer,
		  (unsigned long long)ktime_to_ns((ktime_t) {
				  .tv64 = __entry->now }))
 );

/**
 * hrtimer_expire_exit - called immediately after the hrtimer callback returns
 * @timer:	pointer to struct hrtimer
 *
 * When used in combination with the hrtimer_expire_entry tracepoint we can
 * determine the runtime of the callback function.
 */
TRACE_EVENT(hrtimer_expire_exit,

	TP_PROTO(struct hrtimer *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
	),

	TP_printk("hrtimer %p", __entry->timer)
);

/**
 * hrtimer_cancel - called when the hrtimer is canceled
 * @timer:	pointer to struct hrtimer
 */
TRACE_EVENT(hrtimer_cancel,

	TP_PROTO(struct hrtimer *timer),

	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field( void *,	timer	)
	),

	TP_fast_assign(
		__entry->timer	= timer;
	),

	TP_printk("hrtimer %p", __entry->timer)
);

#endif /*  _TRACE_TIMER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
