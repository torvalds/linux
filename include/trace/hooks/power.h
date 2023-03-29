/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM power
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_POWER_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_try_to_freeze_todo,
	TP_PROTO(unsigned int todo, unsigned int elapsed_msecs, bool wq_busy),
	TP_ARGS(todo, elapsed_msecs, wq_busy));

DECLARE_HOOK(android_vh_try_to_freeze_todo_unfrozen,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p));

#endif /* _TRACE_HOOK_POWER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
