/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fpsimd

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FPSIMD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FPSIMD_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#ifdef __GENKSYMS__
struct task_struct;
#else
/* struct task_struct */
#include <linux/sched.h>
#endif /* __GENKSYMS__ */

DECLARE_HOOK(android_vh_is_fpsimd_save,
	TP_PROTO(struct task_struct *prev, struct task_struct *next),
	TP_ARGS(prev, next))

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_FPSIMD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
