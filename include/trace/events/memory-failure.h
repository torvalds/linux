/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory_failure
#define TRACE_INCLUDE_FILE memory-failure

#if !defined(_TRACE_MEMORY_FAILURE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEMORY_FAILURE_H

#include <linux/tracepoint.h>
#include <linux/mm.h>

/*
 * memory-failure recovery action result event
 *
 * unsigned long pfn -	Page Frame Number of the corrupted page
 * int type	-	Page types of the corrupted page
 * int result	-	Result of recovery action
 */

#define MF_ACTION_RESULT	\
	EM ( MF_IGNORED, "Ignored" )	\
	EM ( MF_FAILED,  "Failed" )	\
	EM ( MF_DELAYED, "Delayed" )	\
	EMe ( MF_RECOVERED, "Recovered" )

#define MF_PAGE_TYPE		\
	EM ( MF_MSG_KERNEL, "reserved kernel page" )			\
	EM ( MF_MSG_KERNEL_HIGH_ORDER, "high-order kernel page" )	\
	EM ( MF_MSG_HUGE, "huge page" )					\
	EM ( MF_MSG_FREE_HUGE, "free huge page" )			\
	EM ( MF_MSG_GET_HWPOISON, "get hwpoison page" )			\
	EM ( MF_MSG_UNMAP_FAILED, "unmapping failed page" )		\
	EM ( MF_MSG_DIRTY_SWAPCACHE, "dirty swapcache page" )		\
	EM ( MF_MSG_CLEAN_SWAPCACHE, "clean swapcache page" )		\
	EM ( MF_MSG_DIRTY_MLOCKED_LRU, "dirty mlocked LRU page" )	\
	EM ( MF_MSG_CLEAN_MLOCKED_LRU, "clean mlocked LRU page" )	\
	EM ( MF_MSG_DIRTY_UNEVICTABLE_LRU, "dirty unevictable LRU page" )	\
	EM ( MF_MSG_CLEAN_UNEVICTABLE_LRU, "clean unevictable LRU page" )	\
	EM ( MF_MSG_DIRTY_LRU, "dirty LRU page" )			\
	EM ( MF_MSG_CLEAN_LRU, "clean LRU page" )			\
	EM ( MF_MSG_TRUNCATED_LRU, "already truncated LRU page" )	\
	EM ( MF_MSG_BUDDY, "free buddy page" )				\
	EM ( MF_MSG_DAX, "dax page" )					\
	EM ( MF_MSG_UNSPLIT_THP, "unsplit thp" )			\
	EM ( MF_MSG_ALREADY_POISONED, "already poisoned" )		\
	EM ( MF_MSG_PFN_MAP, "non struct page pfn" )                    \
	EMe ( MF_MSG_UNKNOWN, "unknown page" )

/*
 * First define the enums in MM_ACTION_RESULT to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

MF_ACTION_RESULT
MF_PAGE_TYPE

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)		{ a, b },
#define EMe(a, b)	{ a, b }

TRACE_EVENT(memory_failure_event,
	TP_PROTO(unsigned long pfn,
		 int type,
		 int result),

	TP_ARGS(pfn, type, result),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(int, type)
		__field(int, result)
	),

	TP_fast_assign(
		__entry->pfn	= pfn;
		__entry->type	= type;
		__entry->result	= result;
	),

	TP_printk("pfn %#lx: recovery action for %s: %s",
		__entry->pfn,
		__print_symbolic(__entry->type, MF_PAGE_TYPE),
		__print_symbolic(__entry->result, MF_ACTION_RESULT)
	)
);
#endif /* _TRACE_MEMORY_FAILURE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
