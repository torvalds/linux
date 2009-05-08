
/* use <trace/sched.h> instead */
#ifndef TRACE_EVENT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

/*
 * Tracepoint for calling kthread_stop, performed to end a kthread:
 */
TRACE_EVENT(sched_kthread_stop,

	TP_PROTO(struct task_struct *t),

	TP_ARGS(t),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, t->comm, TASK_COMM_LEN);
		__entry->pid	= t->pid;
	),

	TP_printk("task %s:%d", __entry->comm, __entry->pid)
);

/*
 * Tracepoint for the return value of the kthread stopping:
 */
TRACE_EVENT(sched_kthread_stop_ret,

	TP_PROTO(int ret),

	TP_ARGS(ret),

	TP_STRUCT__entry(
		__field(	int,	ret	)
	),

	TP_fast_assign(
		__entry->ret	= ret;
	),

	TP_printk("ret %d", __entry->ret)
);

/*
 * Tracepoint for waiting on task to unschedule:
 *
 * (NOTE: the 'rq' argument is not used by generic trace events,
 *        but used by the latency tracer plugin. )
 */
TRACE_EVENT(sched_wait_task,

	TP_PROTO(struct rq *rq, struct task_struct *p),

	TP_ARGS(rq, p),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid	= p->pid;
		__entry->prio	= p->prio;
	),

	TP_printk("task %s:%d [%d]",
		  __entry->comm, __entry->pid, __entry->prio)
);

/*
 * Tracepoint for waking up a task:
 *
 * (NOTE: the 'rq' argument is not used by generic trace events,
 *        but used by the latency tracer plugin. )
 */
TRACE_EVENT(sched_wakeup,

	TP_PROTO(struct rq *rq, struct task_struct *p, int success),

	TP_ARGS(rq, p, success),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
		__field(	int,	success			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
		__entry->success	= success;
	),

	TP_printk("task %s:%d [%d] success=%d",
		  __entry->comm, __entry->pid, __entry->prio,
		  __entry->success)
);

/*
 * Tracepoint for waking up a new task:
 *
 * (NOTE: the 'rq' argument is not used by generic trace events,
 *        but used by the latency tracer plugin. )
 */
TRACE_EVENT(sched_wakeup_new,

	TP_PROTO(struct rq *rq, struct task_struct *p, int success),

	TP_ARGS(rq, p, success),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
		__field(	int,	success			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
		__entry->success	= success;
	),

	TP_printk("task %s:%d [%d] success=%d",
		  __entry->comm, __entry->pid, __entry->prio,
		  __entry->success)
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

	TP_fast_assign(
		memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
		__entry->prev_pid	= prev->pid;
		__entry->prev_prio	= prev->prio;
		memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
		__entry->next_pid	= next->pid;
		__entry->next_prio	= next->prio;
	),

	TP_printk("task %s:%d [%d] ==> %s:%d [%d]",
		__entry->prev_comm, __entry->prev_pid, __entry->prev_prio,
		__entry->next_comm, __entry->next_pid, __entry->next_prio)
);

/*
 * Tracepoint for a task being migrated:
 */
TRACE_EVENT(sched_migrate_task,

	TP_PROTO(struct task_struct *p, int orig_cpu, int dest_cpu),

	TP_ARGS(p, orig_cpu, dest_cpu),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
		__field(	int,	orig_cpu		)
		__field(	int,	dest_cpu		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
		__entry->orig_cpu	= orig_cpu;
		__entry->dest_cpu	= dest_cpu;
	),

	TP_printk("task %s:%d [%d] from: %d  to: %d",
		  __entry->comm, __entry->pid, __entry->prio,
		  __entry->orig_cpu, __entry->dest_cpu)
);

/*
 * Tracepoint for freeing a task:
 */
TRACE_EVENT(sched_process_free,

	TP_PROTO(struct task_struct *p),

	TP_ARGS(p),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
	),

	TP_printk("task %s:%d [%d]",
		  __entry->comm, __entry->pid, __entry->prio)
);

/*
 * Tracepoint for a task exiting:
 */
TRACE_EVENT(sched_process_exit,

	TP_PROTO(struct task_struct *p),

	TP_ARGS(p),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
	),

	TP_printk("task %s:%d [%d]",
		  __entry->comm, __entry->pid, __entry->prio)
);

/*
 * Tracepoint for a waiting task:
 */
TRACE_EVENT(sched_process_wait,

	TP_PROTO(struct pid *pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
		__field(	int,	prio			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, current->comm, TASK_COMM_LEN);
		__entry->pid		= pid_nr(pid);
		__entry->prio		= current->prio;
	),

	TP_printk("task %s:%d [%d]",
		  __entry->comm, __entry->pid, __entry->prio)
);

/*
 * Tracepoint for do_fork:
 */
TRACE_EVENT(sched_process_fork,

	TP_PROTO(struct task_struct *parent, struct task_struct *child),

	TP_ARGS(parent, child),

	TP_STRUCT__entry(
		__array(	char,	parent_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	parent_pid			)
		__array(	char,	child_comm,	TASK_COMM_LEN	)
		__field(	pid_t,	child_pid			)
	),

	TP_fast_assign(
		memcpy(__entry->parent_comm, parent->comm, TASK_COMM_LEN);
		__entry->parent_pid	= parent->pid;
		memcpy(__entry->child_comm, child->comm, TASK_COMM_LEN);
		__entry->child_pid	= child->pid;
	),

	TP_printk("parent %s:%d  child %s:%d",
		__entry->parent_comm, __entry->parent_pid,
		__entry->child_comm, __entry->child_pid)
);

/*
 * Tracepoint for sending a signal:
 */
TRACE_EVENT(sched_signal_send,

	TP_PROTO(int sig, struct task_struct *p),

	TP_ARGS(sig, p),

	TP_STRUCT__entry(
		__field(	int,	sig			)
		__array(	char,	comm,	TASK_COMM_LEN	)
		__field(	pid_t,	pid			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid	= p->pid;
		__entry->sig	= sig;
	),

	TP_printk("sig: %d  task %s:%d",
		  __entry->sig, __entry->comm, __entry->pid)
);

#undef TRACE_SYSTEM
