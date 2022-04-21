/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM binder
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_BINDER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BINDER_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct binder_transaction;
struct task_struct;
DECLARE_HOOK(android_vh_binder_transaction_init,
	TP_PROTO(struct binder_transaction *t),
	TP_ARGS(t));
DECLARE_HOOK(android_vh_binder_set_priority,
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
DECLARE_HOOK(android_vh_binder_restore_priority,
	TP_PROTO(struct binder_transaction *t, struct task_struct *task),
	TP_ARGS(t, task));
struct binder_proc;
struct binder_thread;
DECLARE_HOOK(android_vh_binder_wakeup_ilocked,
	TP_PROTO(struct task_struct *task, bool sync, struct binder_proc *proc),
	TP_ARGS(task, sync, proc));
DECLARE_HOOK(android_vh_binder_wait_for_work,
	TP_PROTO(bool do_proc_work, struct binder_thread *tsk, struct binder_proc *proc),
	TP_ARGS(do_proc_work, tsk, proc));
DECLARE_HOOK(android_vh_sync_txn_recvd,
	TP_PROTO(struct task_struct *tsk, struct task_struct *from),
	TP_ARGS(tsk, from));
#endif /* _TRACE_HOOK_BINDER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
