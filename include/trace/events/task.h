#undef TRACE_SYSTEM
#define TRACE_SYSTEM task

#if !defined(_TRACE_TASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TASK_H
#include <linux/tracepoint.h>

TRACE_EVENT(task_newtask,

	TP_PROTO(struct task_struct *task, unsigned long clone_flags),

	TP_ARGS(task, clone_flags),

	TP_STRUCT__entry(
		__field(	pid_t,	pid)
		__array(	char,	comm, TASK_COMM_LEN)
		__field( unsigned long, clone_flags)
		__field(	int,    oom_score_adj)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->clone_flags = clone_flags;
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("pid=%d comm=%s clone_flags=%lx oom_score_adj=%d",
		__entry->pid, __entry->comm,
		__entry->clone_flags, __entry->oom_score_adj)
);

TRACE_EVENT(task_rename,

	TP_PROTO(struct task_struct *task, char *comm),

	TP_ARGS(task, comm),

	TP_STRUCT__entry(
		__field(	pid_t,	pid)
		__array(	char, oldcomm,  TASK_COMM_LEN)
		__array(	char, newcomm,  TASK_COMM_LEN)
		__field(	int, oom_score_adj)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(entry->oldcomm, task->comm, TASK_COMM_LEN);
		memcpy(entry->newcomm, comm, TASK_COMM_LEN);
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("pid=%d oldcomm=%s newcomm=%s oom_score_adj=%d",
		__entry->pid, __entry->oldcomm,
		__entry->newcomm, __entry->oom_score_adj)
);

#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
