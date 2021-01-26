/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM epoch

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_EPOCH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_EPOCH_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_show_suspend_epoch_val,
	TP_PROTO(u64 suspend_ns, u64 suspend_cycles),
	TP_ARGS(suspend_ns, suspend_cycles));

DECLARE_HOOK(android_vh_show_resume_epoch_val,
	TP_PROTO(u64 resume_cycles),
	TP_ARGS(resume_cycles));

#endif /* _TRACE_HOOK_EPOCH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
