/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM test

#if !defined(_TRACE_TEST_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TEST_H

#include <linux/tracepoint.h>

TRACE_EVENT(ftrace_test_filter,

	TP_PROTO(int a, int b, int c, int d, int e, int f, int g, int h),

	TP_ARGS(a, b, c, d, e, f, g, h),

	TP_STRUCT__entry(
		__field(int, a)
		__field(int, b)
		__field(int, c)
		__field(int, d)
		__field(int, e)
		__field(int, f)
		__field(int, g)
		__field(int, h)
	),

	TP_fast_assign(
		__entry->a = a;
		__entry->b = b;
		__entry->c = c;
		__entry->d = d;
		__entry->e = e;
		__entry->f = f;
		__entry->g = g;
		__entry->h = h;
	),

	TP_printk("a %d, b %d, c %d, d %d, e %d, f %d, g %d, h %d",
		  __entry->a, __entry->b, __entry->c, __entry->d,
		  __entry->e, __entry->f, __entry->g, __entry->h)
);

#endif /* _TRACE_TEST_H || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_events_filter_test

/* This part must be outside protection */
#include <trace/define_trace.h>
