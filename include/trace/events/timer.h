#undef TRACE_SYSTEM
#define TRACE_SYSTEM timer

#if !defined(_TRACE_TIMER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TIMER_H

#include <linux/tracepoint.h>
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

#endif /*  _TRACE_TIMER_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
