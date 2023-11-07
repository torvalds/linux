/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM wakeupbypass

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_WAKEUPBYPASS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_WAKEUPBYPASS_H
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_wakeup_bypass,
	TP_PROTO(int *is_wakeup_bypassed),
	TP_ARGS(is_wakeup_bypassed));

#endif /* _TRACE_HOOK_WAKEUPBYPASS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
