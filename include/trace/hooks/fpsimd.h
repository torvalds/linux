/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fpsimd

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FPSIMD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FPSIMD_H

#include <trace/hooks/vendor_hooks.h>

struct task_struct;

DECLARE_HOOK(android_vh_is_fpsimd_save,
	TP_PROTO(struct task_struct *prev, struct task_struct *next),
	TP_ARGS(prev, next))

#endif /* _TRACE_HOOK_FPSIMD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
