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
DECLARE_RESTRICTED_HOOK(android_rvh_pr_set_vma_name_bypass,
	TP_PROTO(struct mm_struct *mm, unsigned long addr, unsigned long size,
		      struct anon_vma_name *anon_name, int *error, bool *bypass),
	TP_ARGS(mm, addr, size, anon_name, error, bypass), 1);
#endif

#include <trace/define_trace.h>
