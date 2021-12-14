/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpu
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CPU_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_cpu_up,
	TP_PROTO(unsigned int cpu),
	TP_ARGS(cpu));

DECLARE_HOOK(android_vh_cpu_down,
	TP_PROTO(unsigned int cpu),
	TP_ARGS(cpu));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_CPU_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
