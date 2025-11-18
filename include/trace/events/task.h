/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM task

#if !defined(_TRACE_TASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TASK_H
#include <linux/tracepoint.h>

TRACE_EVENT(task_newtask,

	TP_PROTO(struct task_struct *task, u64 clone_flags),

	TP_ARGS(task, clone_flags),

	TP_STRUCT__entry(
		__field(	pid_t,	pid)
		__array(	char,	comm, TASK_COMM_LEN)
		__field(	u64,    clone_flags)
		__field(	short,	oom_score_adj)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->clone_flags = clone_flags;
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("pid=%d comm=%s clone_flags=%llx oom_score_adj=%hd",
		__entry->pid, __entry->comm,
		__entry->clone_flags, __entry->oom_score_adj)
);

TRACE_EVENT(task_rename,

	TP_PROTO(struct task_struct *task, const char *comm),

	TP_ARGS(task, comm),

	TP_STRUCT__entry(
		__array(	char, oldcomm,  TASK_COMM_LEN)
		__array(	char, newcomm,  TASK_COMM_LEN)
		__field(	short,	oom_score_adj)
	),

	TP_fast_assign(
		memcpy(entry->oldcomm, task->comm, TASK_COMM_LEN);
		strscpy(entry->newcomm, comm, TASK_COMM_LEN);
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("oldcomm=%s newcomm=%s oom_score_adj=%hd",
		  __entry->oldcomm, __entry->newcomm, __entry->oom_score_adj)
);

/**
 * task_prctl_unknown - called on unknown prctl() option
 * @option:	option passed
 * @arg2:	arg2 passed
 * @arg3:	arg3 passed
 * @arg4:	arg4 passed
 * @arg5:	arg5 passed
 *
 * Called on an unknown prctl() option.
 */
TRACE_EVENT(task_prctl_unknown,

	TP_PROTO(int option, unsigned long arg2, unsigned long arg3,
		 unsigned long arg4, unsigned long arg5),

	TP_ARGS(option, arg2, arg3, arg4, arg5),

	TP_STRUCT__entry(
		__field(	int,		option)
		__field(	unsigned long,	arg2)
		__field(	unsigned long,	arg3)
		__field(	unsigned long,	arg4)
		__field(	unsigned long,	arg5)
	),

	TP_fast_assign(
		__entry->option = option;
		__entry->arg2 = arg2;
		__entry->arg3 = arg3;
		__entry->arg4 = arg4;
		__entry->arg5 = arg5;
	),

	TP_printk("option=%d arg2=%ld arg3=%ld arg4=%ld arg5=%ld",
		  __entry->option, __entry->arg2, __entry->arg3, __entry->arg4, __entry->arg5)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
