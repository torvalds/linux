/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM signal
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SIGNAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SIGNAL_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#ifdef __GENKSYMS__
struct task_struct;
#else
/* struct task_struct */
#include <linux/sched.h>
#endif /* __GENKSYMS__ */
DECLARE_HOOK(android_vh_do_send_sig_info,
	TP_PROTO(int sig, struct task_struct *killer, struct task_struct *dst),
	TP_ARGS(sig, killer, dst));
DECLARE_HOOK(android_vh_process_killed,
	TP_PROTO(struct task_struct *task, bool *reap),
	TP_ARGS(task, reap));
DECLARE_HOOK(android_vh_killed_process,
	TP_PROTO(struct task_struct *killer, struct task_struct *dst, bool *reap),
	TP_ARGS(killer, dst, reap));
#endif /* _TRACE_HOOK_SIGNAL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
