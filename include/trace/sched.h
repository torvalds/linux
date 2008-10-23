#ifndef _TRACE_SCHED_H
#define _TRACE_SCHED_H

#include <linux/sched.h>
#include <linux/tracepoint.h>

DEFINE_TRACE(sched_kthread_stop,
	TPPROTO(struct task_struct *t),
		TPARGS(t));

DEFINE_TRACE(sched_kthread_stop_ret,
	TPPROTO(int ret),
		TPARGS(ret));

DEFINE_TRACE(sched_wait_task,
	TPPROTO(struct rq *rq, struct task_struct *p),
		TPARGS(rq, p));

DEFINE_TRACE(sched_wakeup,
	TPPROTO(struct rq *rq, struct task_struct *p),
		TPARGS(rq, p));

DEFINE_TRACE(sched_wakeup_new,
	TPPROTO(struct rq *rq, struct task_struct *p),
		TPARGS(rq, p));

DEFINE_TRACE(sched_switch,
	TPPROTO(struct rq *rq, struct task_struct *prev,
		struct task_struct *next),
		TPARGS(rq, prev, next));

DEFINE_TRACE(sched_migrate_task,
	TPPROTO(struct rq *rq, struct task_struct *p, int dest_cpu),
		TPARGS(rq, p, dest_cpu));

DEFINE_TRACE(sched_process_free,
	TPPROTO(struct task_struct *p),
		TPARGS(p));

DEFINE_TRACE(sched_process_exit,
	TPPROTO(struct task_struct *p),
		TPARGS(p));

DEFINE_TRACE(sched_process_wait,
	TPPROTO(struct pid *pid),
		TPARGS(pid));

DEFINE_TRACE(sched_process_fork,
	TPPROTO(struct task_struct *parent, struct task_struct *child),
		TPARGS(parent, child));

DEFINE_TRACE(sched_signal_send,
	TPPROTO(int sig, struct task_struct *p),
		TPARGS(sig, p));

#endif
