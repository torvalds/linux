#ifndef __TRACE_WORKQUEUE_H
#define __TRACE_WORKQUEUE_H

#include <linux/tracepoint.h>
#include <linux/workqueue.h>
#include <linux/sched.h>

DECLARE_TRACE(workqueue_insertion,
	   TPPROTO(struct task_struct *wq_thread, struct work_struct *work),
	   TPARGS(wq_thread, work));

DECLARE_TRACE(workqueue_execution,
	   TPPROTO(struct task_struct *wq_thread, struct work_struct *work),
	   TPARGS(wq_thread, work));

/* Trace the creation of one workqueue thread on a cpu */
DECLARE_TRACE(workqueue_creation,
	   TPPROTO(struct task_struct *wq_thread, int cpu),
	   TPARGS(wq_thread, cpu));

DECLARE_TRACE(workqueue_destruction,
	   TPPROTO(struct task_struct *wq_thread),
	   TPARGS(wq_thread));

#endif /* __TRACE_WORKQUEUE_H */
