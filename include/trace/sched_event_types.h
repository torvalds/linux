
/* use <trace/sched.h> instead */
#ifndef TRACE_EVENT_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

TRACE_EVENT_FORMAT(sched_kthread_stop,
	TP_PROTO(struct task_struct *t),
	TP_ARGS(t),
	TP_FMT("task %s:%d", t->comm, t->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, t->pid)
	),
	TP_RAW_FMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_kthread_stop_ret,
	TP_PROTO(int ret),
	TP_ARGS(ret),
	TP_FMT("ret=%d", ret),
	TRACE_STRUCT(
		TRACE_FIELD(int, ret, ret)
	),
	TP_RAW_FMT("ret=%d")
	);

TRACE_EVENT_FORMAT(sched_wait_task,
	TP_PROTO(struct rq *rq, struct task_struct *p),
	TP_ARGS(rq, p),
	TP_FMT("task %s:%d", p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TP_RAW_FMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_wakeup,
	TP_PROTO(struct rq *rq, struct task_struct *p, int success),
	TP_ARGS(rq, p, success),
	TP_FMT("task %s:%d %s",
	      p->comm, p->pid, success ? "succeeded" : "failed"),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
		TRACE_FIELD(int, success, success)
	),
	TP_RAW_FMT("task %d success=%d")
	);

TRACE_EVENT_FORMAT(sched_wakeup_new,
	TP_PROTO(struct rq *rq, struct task_struct *p, int success),
	TP_ARGS(rq, p, success),
	TP_FMT("task %s:%d",
	      p->comm, p->pid, success ? "succeeded" : "failed"),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
		TRACE_FIELD(int, success, success)
	),
	TP_RAW_FMT("task %d success=%d")
	);

/*
 * Tracepoint for task switches, performed by the scheduler:
 *
 * (NOTE: the 'rq' argument is not used by generic trace events,
 *        but used by the latency tracer plugin. )
 */
TRACE_EVENT(sched_switch,

	TP_PROTO(struct rq *rq, struct task_struct *prev,
		 struct task_struct *next),

	TP_ARGS(rq, prev, next),

	TP_STRUCT__entry(
		__array(	char,	prev_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	prev_pid			)
		__field(	int,	prev_prio			)
		__array(	char,	next_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	next_pid			)
		__field(	int,	next_prio			)
	),

	TP_printk("task %s:%d [%d] ==> %s:%d [%d]",
		__entry->prev_comm, __entry->prev_pid, __entry->prev_prio,
		__entry->next_comm, __entry->next_pid, __entry->next_prio),

	TP_fast_assign(
		memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
		__entry->prev_pid	= prev->pid;
		__entry->prev_prio	= prev->prio;
		memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
		__entry->next_pid	= next->pid;
		__entry->next_prio	= next->prio;
	)
);

TRACE_EVENT_FORMAT(sched_migrate_task,
	TP_PROTO(struct task_struct *p, int orig_cpu, int dest_cpu),
	TP_ARGS(p, orig_cpu, dest_cpu),
	TP_FMT("task %s:%d from: %d  to: %d",
	      p->comm, p->pid, orig_cpu, dest_cpu),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
		TRACE_FIELD(int, orig_cpu, orig_cpu)
		TRACE_FIELD(int, dest_cpu, dest_cpu)
	),
	TP_RAW_FMT("task %d  from: %d to: %d")
	);

TRACE_EVENT_FORMAT(sched_process_free,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p),
	TP_FMT("task %s:%d", p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TP_RAW_FMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_process_exit,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p),
	TP_FMT("task %s:%d", p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TP_RAW_FMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_process_wait,
	TP_PROTO(struct pid *pid),
	TP_ARGS(pid),
	TP_FMT("pid %d", pid_nr(pid)),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, pid_nr(pid))
	),
	TP_RAW_FMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_process_fork,
	TP_PROTO(struct task_struct *parent, struct task_struct *child),
	TP_ARGS(parent, child),
	TP_FMT("parent %s:%d  child %s:%d",
	      parent->comm, parent->pid, child->comm, child->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, parent, parent->pid)
		TRACE_FIELD(pid_t, child, child->pid)
	),
	TP_RAW_FMT("parent %d  child %d")
	);

TRACE_EVENT_FORMAT(sched_signal_send,
	TP_PROTO(int sig, struct task_struct *p),
	TP_ARGS(sig, p),
	TP_FMT("sig: %d   task %s:%d", sig, p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(int, sig, sig)
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TP_RAW_FMT("sig: %d  task %d")
	);

#undef TRACE_SYSTEM
