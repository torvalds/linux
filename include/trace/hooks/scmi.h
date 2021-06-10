/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scmi
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SCMI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCMI_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_scmi_timeout_sync,
	TP_PROTO(int *timeout),
	TP_ARGS(timeout));

#endif /* _TRACE_HOOK_SCMI_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
