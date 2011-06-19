#undef TRACE_SYSTEM
#define TRACE_SYSTEM rcu

#if !defined(_TRACE_RCU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RCU_H

#include <linux/tracepoint.h>

/*
 * Tracepoint for start/end markers used for utilization calculations.
 * By convention, the string is of the following forms:
 *
 * "Start <activity>" -- Mark the start of the specified activity,
 *			 such as "context switch".  Nesting is permitted.
 * "End <activity>" -- Mark the end of the specified activity.
 */
TRACE_EVENT(rcu_utilization,

	TP_PROTO(char *s),

	TP_ARGS(s),

	TP_STRUCT__entry(
		__field(char *,	s)
	),

	TP_fast_assign(
		__entry->s = s;
	),

	TP_printk("%s", __entry->s)
);

/*
 * Tracepoint for marking the beginning rcu_do_batch, performed to start
 * RCU callback invocation.  The first argument is the total number of
 * callbacks (including those that are not yet ready to be invoked),
 * and the second argument is the current RCU-callback batch limit.
 */
TRACE_EVENT(rcu_batch_start,

	TP_PROTO(long qlen, int blimit),

	TP_ARGS(qlen, blimit),

	TP_STRUCT__entry(
		__field(long, qlen)
		__field(int, blimit)
	),

	TP_fast_assign(
		__entry->qlen = qlen;
		__entry->blimit = blimit;
	),

	TP_printk("CBs=%ld bl=%d", __entry->qlen, __entry->blimit)
);

/*
 * Tracepoint for the invocation of a single RCU callback function.
 * The argument is a pointer to the RCU callback itself.
 */
TRACE_EVENT(rcu_invoke_callback,

	TP_PROTO(struct rcu_head *rhp),

	TP_ARGS(rhp),

	TP_STRUCT__entry(
		__field(void *,	rhp)
		__field(void *,	func)
	),

	TP_fast_assign(
		__entry->rhp = rhp;
		__entry->func = rhp->func;
	),

	TP_printk("rhp=%p func=%pf", __entry->rhp, __entry->func)
);

/*
 * Tracepoint for the invocation of a single RCU callback of the special
 * kfree() form.  The first argument is a pointer to the RCU callback
 * and the second argument is the offset of the callback within the
 * enclosing RCU-protected data structure.
 */
TRACE_EVENT(rcu_invoke_kfree_callback,

	TP_PROTO(struct rcu_head *rhp, unsigned long offset),

	TP_ARGS(rhp, offset),

	TP_STRUCT__entry(
		__field(void *,	rhp)
		__field(unsigned long, offset)
	),

	TP_fast_assign(
		__entry->rhp = rhp;
		__entry->offset	= offset;
	),

	TP_printk("rhp=%p func=%ld", __entry->rhp, __entry->offset)
);

/*
 * Tracepoint for exiting rcu_do_batch after RCU callbacks have been
 * invoked.  The first argument is the number of callbacks actually invoked.
 */
TRACE_EVENT(rcu_batch_end,

	TP_PROTO(int callbacks_invoked),

	TP_ARGS(callbacks_invoked),

	TP_STRUCT__entry(
		__field(int, callbacks_invoked)
	),

	TP_fast_assign(
		__entry->callbacks_invoked = callbacks_invoked;
	),

	TP_printk("CBs-invoked=%d", __entry->callbacks_invoked)
);

#endif /* _TRACE_RCU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
