/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bug
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BUG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BUG_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_RESTRICTED_HOOK(android_rvh_report_bug,
	TP_PROTO(const char *file, unsigned int line, unsigned long bugaddr),
	TP_ARGS(file, line, bugaddr), 1);

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_BUG_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
