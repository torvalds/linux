#ifndef _TRACE_SCHED_H
#define _TRACE_SCHED_H

#include <linux/sched.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(sched_kthread_stop,
	TPPROTO(struct task_struct *t),
		TPARGS(t));

DECLARE_TRACE(sched_kthread_stop_ret,
	TPPROTO(int ret),
		TPARGS(ret));

DECLARE_TRACE(sched_wait_task,
	TPPROTO(struct rq *rq, struct task_struct *p),
		TPARGS(rq, p));

DECLARE_TRACE(sched_wakeup,
	TPPROTO(struct rq *rq, struct task_struct *p, int success),
		TPARGS(rq, p, success));

DECLARE_TRACE(sched_wakeup_new,
	TPPROTO(struct rq *rq, struct task_struct *p, int success),
		TPARGS(rq, p, success));

DECLARE_TRACE(sched_switch,
	TPPROTO(struct rq *rq, struct task_struct *prev,
		struct task_struct *next),
		TPARGS(rq, prev, next));

DECLARE_TRACE(sched_migrate_task,
	TPPROTO(struct task_struct *p, int orig_cpu, int dest_cpu),
		TPARGS(p, orig_cpu, dest_cpu));

DECLARE_TRACE(sched_process_free,
	TPPROTO(struct task_struct *p),
		TPARGS(p));

DECLARE_TRACE(sched_process_exit,
	TPPROTO(struct task_struct *p),
		TPARGS(p));

DECLARE_TRACE(sched_process_wait,
	TPPROTO(struct pid *pid),
		TPARGS(pid));

DECLARE_TRACE(sched_process_fork,
	TPPROTO(struct task_struct *parent, struct task_struct *child),
		TPARGS(parent, child));

DECLARE_TRACE(sched_signal_send,
	TPPROTO(int sig, struct task_struct *p),
		TPARGS(sig, p));

#endif
