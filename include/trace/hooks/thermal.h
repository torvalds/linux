/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_THERMAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_THERMAL_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_enable_thermal_genl_check,
	TP_PROTO(int event, int *enable_thermal_genl),
	TP_ARGS(event, enable_thermal_genl));

#endif /* _TRACE_HOOK_THERMAL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

