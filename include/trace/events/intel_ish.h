/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM intel_ish

#if !defined(_TRACE_INTEL_ISH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_INTEL_ISH_H

#include <linux/tracepoint.h>

TRACE_EVENT(ishtp_dump,

	TP_PROTO(const char *message),

	TP_ARGS(message),

	TP_STRUCT__entry(
		__string(message, message)
	),

	TP_fast_assign(
		__assign_str(message, message);
	),

	TP_printk("%s", __get_str(message))
);


#endif /* _TRACE_INTEL_ISH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
