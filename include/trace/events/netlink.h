#undef TRACE_SYSTEM
#define TRACE_SYSTEM netlink

#if !defined(_TRACE_NETLINK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NETLINK_H

#include <linux/tracepoint.h>

TRACE_EVENT(netlink_extack,

	TP_PROTO(const char *msg),

	TP_ARGS(msg),

	TP_STRUCT__entry(
		__string(	msg,	msg	)
	),

	TP_fast_assign(
		__assign_str(msg, msg);
	),

	TP_printk("msg=%s", __get_str(msg))
);

#endif /* _TRACE_NETLINK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
