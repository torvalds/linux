/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sys
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SYS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SYS_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_syscall_prctl_finished,
	TP_PROTO(int option, struct task_struct *task),
	TP_ARGS(option, task));
#endif

#include <trace/define_trace.h>
