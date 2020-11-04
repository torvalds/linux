/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM debug

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DEBUG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DEBUG_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct pt_regs;

DECLARE_HOOK(android_vh_ipi_stop,
	TP_PROTO(struct pt_regs *regs),
	TP_ARGS(regs))
#else
#define trace_android_vh_ipi_stop(regs)
#define trace_android_vh_ipi_stop_rcuidle(regs)
#endif

#endif /* _TRACE_HOOK_DEBUG_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
