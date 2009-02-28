
/* use <trace/sched.h> instead */
#ifndef TRACE_FORMAT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched

TRACE_FORMAT(sched_kthread_stop,
	TPPROTO(struct task_struct *t),
	TPARGS(t),
	TPFMT("task %s:%d", t->comm, t->pid));

TRACE_FORMAT(sched_kthread_stop_ret,
	TPPROTO(int ret),
	TPARGS(ret),
	TPFMT("ret=%d", ret));

TRACE_FORMAT(sched_wait_task,
	TPPROTO(struct rq *rq, struct task_struct *p),
	TPARGS(rq, p),
	TPFMT("task %s:%d", p->comm, p->pid));

TRACE_FORMAT(sched_wakeup,
	TPPROTO(struct rq *rq, struct task_struct *p, int success),
	TPARGS(rq, p, success),
	TPFMT("task %s:%d %s",
	      p->comm, p->pid, success?"succeeded":"failed"));

TRACE_FORMAT(sched_wakeup_new,
	TPPROTO(struct rq *rq, struct task_struct *p, int success),
	TPARGS(rq, p, success),
	TPFMT("task %s:%d",
	      p->comm, p->pid, success?"succeeded":"failed"));

TRACE_FORMAT(sched_switch,
	TPPROTO(struct rq *rq, struct task_struct *prev,
		struct task_struct *next),
	TPARGS(rq, prev, next),
	TPFMT("task %s:%d ==> %s:%d",
	      prev->comm, prev->pid, next->comm, next->pid));

TRACE_FORMAT(sched_migrate_task,
	TPPROTO(struct task_struct *p, int orig_cpu, int dest_cpu),
	TPARGS(p, orig_cpu, dest_cpu),
	TPFMT("task %s:%d from: %d  to: %d",
	      p->comm, p->pid, orig_cpu, dest_cpu));

TRACE_FORMAT(sched_process_free,
	TPPROTO(struct task_struct *p),
	TPARGS(p),
	TPFMT("task %s:%d", p->comm, p->pid));

TRACE_FORMAT(sched_process_exit,
	TPPROTO(struct task_struct *p),
	TPARGS(p),
	TPFMT("task %s:%d", p->comm, p->pid));

TRACE_FORMAT(sched_process_wait,
	TPPROTO(struct pid *pid),
	TPARGS(pid),
	TPFMT("pid %d", pid));

TRACE_FORMAT(sched_process_fork,
	TPPROTO(struct task_struct *parent, struct task_struct *child),
	TPARGS(parent, child),
	TPFMT("parent %s:%d  child %s:%d",
	      parent->comm, parent->pid, child->comm, child->pid));

TRACE_FORMAT(sched_signal_send,
	TPPROTO(int sig, struct task_struct *p),
	TPARGS(sig, p),
	TPFMT("sig: %d   task %s:%d", sig, p->comm, p->pid));

#undef TRACE_SYSTEM
