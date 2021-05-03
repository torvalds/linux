/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM workqueue
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_WORKQUEUE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_WORKQUEUE_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct worker;
DECLARE_HOOK(android_vh_create_worker,
	TP_PROTO(struct worker *worker, struct workqueue_attrs *attrs),
	TP_ARGS(worker, attrs));
/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_WORKQUEUE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
