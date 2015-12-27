#undef TRACE_SYSTEM
#define TRACE_SYSTEM tlb

#if !defined(_TRACE_TLB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TLB_H

#include <linux/mm_types.h>
#include <linux/tracepoint.h>

#define TLB_FLUSH_REASON						\
	EM(  TLB_FLUSH_ON_TASK_SWITCH,	"flush on task switch" )	\
	EM(  TLB_REMOTE_SHOOTDOWN,	"remote shootdown" )		\
	EM(  TLB_LOCAL_SHOOTDOWN,	"local shootdown" )		\
	EM(  TLB_LOCAL_MM_SHOOTDOWN,	"local mm shootdown" )		\
	EMe( TLB_REMOTE_SEND_IPI,	"remote ipi send" )

/*
 * First define the enums in TLB_FLUSH_REASON to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a,b)		TRACE_DEFINE_ENUM(a);
#define EMe(a,b)	TRACE_DEFINE_ENUM(a);

TLB_FLUSH_REASON

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a,b)		{ a, b },
#define EMe(a,b)	{ a, b }

TRACE_EVENT_CONDITION(tlb_flush,

	TP_PROTO(int reason, unsigned long pages),
	TP_ARGS(reason, pages),

	TP_CONDITION(cpu_online(smp_processor_id())),

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
