/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bpf_trace

#if !defined(_TRACE_BPF_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)

#define _TRACE_BPF_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(bpf_trace_printk,

	TP_PROTO(const char *bpf_string),

	TP_ARGS(bpf_string),

	TP_STRUCT__entry(
		__string(bpf_string, bpf_string)
	),

	TP_fast_assign(
		__assign_str(bpf_string);
	),

	TP_printk("%s", __get_str(bpf_string))
);

#endif /* _TRACE_BPF_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE bpf_trace

#include <trace/define_trace.h>
