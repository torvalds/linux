/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM page_isolation

#if !defined(_TRACE_PAGE_ISOLATION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PAGE_ISOLATION_H

#include <linux/tracepoint.h>

TRACE_EVENT(test_pages_isolated,

	TP_PROTO(
		unsigned long start_pfn,
		unsigned long end_pfn,
		unsigned long fin_pfn),

	TP_ARGS(start_pfn, end_pfn, fin_pfn),

	TP_STRUCT__entry(
		__field(unsigned long, start_pfn)
		__field(unsigned long, end_pfn)
		__field(unsigned long, fin_pfn)
	),

	TP_fast_assign(
		__entry->start_pfn = start_pfn;
		__entry->end_pfn = end_pfn;
		__entry->fin_pfn = fin_pfn;
	),

	TP_printk("start_pfn=0x%lx end_pfn=0x%lx fin_pfn=0x%lx ret=%s",
		__entry->start_pfn, __entry->end_pfn, __entry->fin_pfn,
		__entry->end_pfn <= __entry->fin_pfn ? "success" : "fail")
);

#endif /* _TRACE_PAGE_ISOLATION_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
