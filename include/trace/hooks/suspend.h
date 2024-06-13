/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM suspend

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SUSPEND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SUSPEND_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_resume_begin,
	TP_PROTO(void *unused),
	TP_ARGS(unused))
DECLARE_HOOK(android_vh_resume_end,
	TP_PROTO(void *unused),
	TP_ARGS(unused))
DECLARE_HOOK(android_vh_early_resume_begin,
	TP_PROTO(void *unused),
	TP_ARGS(unused))

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_SUSPEND_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
