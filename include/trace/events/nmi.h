#undef TRACE_SYSTEM
#define TRACE_SYSTEM nmi

#if !defined(_TRACE_NMI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NMI_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

TRACE_EVENT(nmi_handler,

	TP_PROTO(void *handler, s64 delta_ns, int handled),

	TP_ARGS(handler, delta_ns, handled),

	TP_STRUCT__entry(
		__field(	void *,		handler	)
		__field(	s64,		delta_ns)
		__field(	int,		handled	)
	),

	TP_fast_assign(
		__entry->handler = handler;
		__entry->delta_ns = delta_ns;
		__entry->handled = handled;
	),

	TP_printk("%ps() delta_ns: %lld handled: %d",
		__entry->handler,
		__entry->delta_ns,
		__entry->handled)
);

#endif /* _TRACE_NMI_H */

/* This part ust be outside protection */
#include <trace/define_trace.h>
