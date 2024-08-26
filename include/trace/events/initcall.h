/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM initcall

#if !defined(_TRACE_INITCALL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INITCALL_H

#include <linux/tracepoint.h>

TRACE_EVENT(initcall_level,

	TP_PROTO(const char *level),

	TP_ARGS(level),

	TP_STRUCT__entry(
		__string(level, level)
	),

	TP_fast_assign(
		__assign_str(level);
	),

	TP_printk("level=%s", __get_str(level))
);

TRACE_EVENT(initcall_start,

	TP_PROTO(initcall_t func),

	TP_ARGS(func),

	TP_STRUCT__entry(
		/*
		 * Use field_struct to avoid is_signed_type()
		 * comparison of a function pointer
		 */
		__field_struct(initcall_t, func)
	),

	TP_fast_assign(
		__entry->func = func;
	),

	TP_printk("func=%pS", __entry->func)
);

TRACE_EVENT(initcall_finish,

	TP_PROTO(initcall_t func, int ret),

	TP_ARGS(func, ret),

	TP_STRUCT__entry(
		/*
		 * Use field_struct to avoid is_signed_type()
		 * comparison of a function pointer
		 */
		__field_struct(initcall_t,	func)
		__field(int,			ret)
	),

	TP_fast_assign(
		__entry->func = func;
		__entry->ret = ret;
	),

	TP_printk("func=%pS ret=%d", __entry->func, __entry->ret)
);

#endif /* if !defined(_TRACE_GPIO_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
