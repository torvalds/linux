/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hung_task

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_HUNG_TASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_HUNG_TASK_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_check_uninterruptible_tasks,
	TP_PROTO(struct task_struct *t, unsigned long timeout,
		bool *need_check),
	TP_ARGS(t, timeout, need_check));

DECLARE_HOOK(android_vh_check_uninterruptible_tasks_dn,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

#endif /* _TRACE_HOOK_HUNG_TASK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
