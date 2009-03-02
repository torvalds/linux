
/* use <trace/sched.h> instead */
#ifndef TRACE_EVENT_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

TRACE_EVENT_FORMAT(sched_kthread_stop,
	TPPROTO(struct task_struct *t),
	TPARGS(t),
	TPFMT("task %s:%d", t->comm, t->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, t->pid)
	),
	TPRAWFMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_kthread_stop_ret,
	TPPROTO(int ret),
	TPARGS(ret),
	TPFMT("ret=%d", ret),
	TRACE_STRUCT(
		TRACE_FIELD(int, ret, ret)
	),
	TPRAWFMT("ret=%d")
	);

TRACE_EVENT_FORMAT(sched_wait_task,
	TPPROTO(struct rq *rq, struct task_struct *p),
	TPARGS(rq, p),
	TPFMT("task %s:%d", p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TPRAWFMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_wakeup,
	TPPROTO(struct rq *rq, struct task_struct *p, int success),
	TPARGS(rq, p, success),
	TPFMT("task %s:%d %s",
	      p->comm, p->pid, success ? "succeeded" : "failed"),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
		TRACE_FIELD(int, success, success)
	),
	TPRAWFMT("task %d success=%d")
	);

TRACE_EVENT_FORMAT(sched_wakeup_new,
	TPPROTO(struct rq *rq, struct task_struct *p, int success),
	TPARGS(rq, p, success),
	TPFMT("task %s:%d",
	      p->comm, p->pid, success ? "succeeded" : "failed"),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
		TRACE_FIELD(int, success, success)
	),
	TPRAWFMT("task %d success=%d")
	);

TRACE_EVENT_FORMAT(sched_switch,
	TPPROTO(struct rq *rq, struct task_struct *prev,
		struct task_struct *next),
	TPARGS(rq, prev, next),
	TPFMT("task %s:%d ==> %s:%d",
	      prev->comm, prev->pid, next->comm, next->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, prev_pid, prev->pid)
		TRACE_FIELD(int, prev_prio, prev->prio)
		TRACE_FIELD_SPECIAL(char next_comm[TASK_COMM_LEN],
				    next_comm,
				    TPCMD(memcpy(TRACE_ENTRY->next_comm,
						 next->comm,
						 TASK_COMM_LEN)))
		TRACE_FIELD(pid_t, next_pid, next->pid)
		TRACE_FIELD(int, next_prio, next->prio)
	),
	TPRAWFMT("prev %d:%d ==> next %s:%d:%d")
	);

TRACE_EVENT_FORMAT(sched_migrate_task,
	TPPROTO(struct task_struct *p, int orig_cpu, int dest_cpu),
	TPARGS(p, orig_cpu, dest_cpu),
	TPFMT("task %s:%d from: %d  to: %d",
	      p->comm, p->pid, orig_cpu, dest_cpu),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
		TRACE_FIELD(int, orig_cpu, orig_cpu)
		TRACE_FIELD(int, dest_cpu, dest_cpu)
	),
	TPRAWFMT("task %d  from: %d to: %d")
	);

TRACE_EVENT_FORMAT(sched_process_free,
	TPPROTO(struct task_struct *p),
	TPARGS(p),
	TPFMT("task %s:%d", p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TPRAWFMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_process_exit,
	TPPROTO(struct task_struct *p),
	TPARGS(p),
	TPFMT("task %s:%d", p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TPRAWFMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_process_wait,
	TPPROTO(struct pid *pid),
	TPARGS(pid),
	TPFMT("pid %d", pid_nr(pid)),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, pid, pid_nr(pid))
	),
	TPRAWFMT("task %d")
	);

TRACE_EVENT_FORMAT(sched_process_fork,
	TPPROTO(struct task_struct *parent, struct task_struct *child),
	TPARGS(parent, child),
	TPFMT("parent %s:%d  child %s:%d",
	      parent->comm, parent->pid, child->comm, child->pid),
	TRACE_STRUCT(
		TRACE_FIELD(pid_t, parent, parent->pid)
		TRACE_FIELD(pid_t, child, child->pid)
	),
	TPRAWFMT("parent %d  child %d")
	);

TRACE_EVENT_FORMAT(sched_signal_send,
	TPPROTO(int sig, struct task_struct *p),
	TPARGS(sig, p),
	TPFMT("sig: %d   task %s:%d", sig, p->comm, p->pid),
	TRACE_STRUCT(
		TRACE_FIELD(int, sig, sig)
		TRACE_FIELD(pid_t, pid, p->pid)
	),
	TPRAWFMT("sig: %d  task %d")
	);

#undef TRACE_SYSTEM
