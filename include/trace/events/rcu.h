#undef TRACE_SYSTEM
#define TRACE_SYSTEM rcu

#if !defined(_TRACE_RCU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RCU_H

#include <linux/tracepoint.h>

/*
 * Tracepoint for calling rcu_do_batch, performed to start callback invocation:
 */
TRACE_EVENT(rcu_batch_start,

	TP_PROTO(long callbacks_ready, int blimit),

	TP_ARGS(callbacks_ready, blimit),

	TP_STRUCT__entry(
		__field(	long,	callbacks_ready		)
		__field(	int,	blimit			)
	),

	TP_fast_assign(
		__entry->callbacks_ready	= callbacks_ready;
		__entry->blimit			= blimit;
	),

	TP_printk("CBs=%ld bl=%d", __entry->callbacks_ready, __entry->blimit)
);

/*
 * Tracepoint for the invocation of a single RCU callback
 */
TRACE_EVENT(rcu_invoke_callback,

	TP_PROTO(struct rcu_head *rhp),

	TP_ARGS(rhp),

	TP_STRUCT__entry(
		__field(	void *,	rhp	)
		__field(	void *,	func	)
	),

	TP_fast_assign(
		__entry->rhp	= rhp;
		__entry->func	= rhp->func;
	),

	TP_printk("rhp=%p func=%pf", __entry->rhp, __entry->func)
);

/*
 * Tracepoint for the invocation of a single RCU kfree callback
 */
TRACE_EVENT(rcu_invoke_kfree_callback,

	TP_PROTO(struct rcu_head *rhp, unsigned long offset),

	TP_ARGS(rhp, offset),

	TP_STRUCT__entry(
		__field(void *,	rhp	)
		__field(unsigned long,	offset	)
	),

	TP_fast_assign(
		__entry->rhp	= rhp;
		__entry->offset	= offset;
	),

	TP_printk("rhp=%p func=%ld", __entry->rhp, __entry->offset)
);

/*
 * Tracepoint for leaving rcu_do_batch, performed after callback invocation:
 */
TRACE_EVENT(rcu_batch_end,

	TP_PROTO(int callbacks_invoked),

	TP_ARGS(callbacks_invoked),

	TP_STRUCT__entry(
		__field(	int,	callbacks_invoked		)
	),

	TP_fast_assign(
		__entry->callbacks_invoked	= callbacks_invoked;
	),

	TP_printk("CBs-invoked=%d", __entry->callbacks_invoked)
);

#endif /* _TRACE_RCU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
