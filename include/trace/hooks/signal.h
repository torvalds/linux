/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM signal
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SIGNAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SIGNAL_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_do_send_sig_info,
	TP_PROTO(int sig, struct task_struct *killer, struct task_struct *dst),
	TP_ARGS(sig, killer, dst));
DECLARE_HOOK(android_vh_exit_signal,
	TP_PROTO(struct task_struct *task),
	TP_ARGS(task));
#endif /* _TRACE_HOOK_SIGNAL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
