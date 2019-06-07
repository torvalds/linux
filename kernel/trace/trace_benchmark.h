// SPDX-License-Identifier: GPL-2.0
#undef TRACE_SYSTEM
#define TRACE_SYSTEM benchmark

#if !defined(_TRACE_BENCHMARK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BENCHMARK_H

#include <linux/tracepoint.h>

extern int trace_benchmark_reg(void);
extern void trace_benchmark_unreg(void);

#define BENCHMARK_EVENT_STRLEN		128

TRACE_EVENT_FN(benchmark_event,

	TP_PROTO(const char *str),

	TP_ARGS(str),

	TP_STRUCT__entry(
		__array(	char,	str,	BENCHMARK_EVENT_STRLEN	)
	),

	TP_fast_assign(
		memcpy(__entry->str, str, BENCHMARK_EVENT_STRLEN);
	),

	TP_printk("%s", __entry->str),

	trace_benchmark_reg, trace_benchmark_unreg
);

#endif /* _TRACE_BENCHMARK_H */

#undef TRACE_INCLUDE_FILE
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_benchmark

/* This part must be outside protection */
#include <trace/define_trace.h>
