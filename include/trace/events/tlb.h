#undef TRACE_SYSTEM
#define TRACE_SYSTEM tlb

#if !defined(_TRACE_TLB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TLB_H

#include <linux/mm_types.h>
#include <linux/tracepoint.h>

#define TLB_FLUSH_REASON	\
	{ TLB_FLUSH_ON_TASK_SWITCH,	"flush on task switch" },	\
	{ TLB_REMOTE_SHOOTDOWN,		"remote shootdown" },		\
	{ TLB_LOCAL_SHOOTDOWN,		"local shootdown" },		\
	{ TLB_LOCAL_MM_SHOOTDOWN,	"local mm shootdown" }

TRACE_EVENT(tlb_flush,

	TP_PROTO(int reason, unsigned long pages),
	TP_ARGS(reason, pages),

	TP_STRUCT__entry(
		__field(	  int, reason)
		__field(unsigned long,  pages)
	),

	TP_fast_assign(
		__entry->reason = reason;
		__entry->pages  = pages;
	),

	TP_printk("pages:%ld reason:%s (%d)",
		__entry->pages,
		__print_symbolic(__entry->reason, TLB_FLUSH_REASON),
		__entry->reason)
);

#endif /* _TRACE_TLB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
