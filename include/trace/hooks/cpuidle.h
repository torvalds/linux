/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpuidle

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CPUIDLE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CPUIDLE_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_cpu_idle,
	TP_PROTO(int event, int state, int cpu),
	TP_ARGS(event, state, cpu))
#else
#define trace_android_vh_cpu_idle(event, state, cpu)
#endif

#endif /* _TRACE_HOOK_CPUIDLE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

