#ifndef __TRACE_WORKQUEUE_H
#define __TRACE_WORKQUEUE_H

#include <linux/tracepoint.h>
#include <linux/workqueue.h>
#include <linux/sched.h>

DECLARE_TRACE(workqueue_insertion,
	   TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),
	   TP_ARGS(wq_thread, work));

DECLARE_TRACE(workqueue_execution,
	   TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),
	   TP_ARGS(wq_thread, work));

/* Trace the creation of one workqueue thread on a cpu */
DECLARE_TRACE(workqueue_creation,
	   TP_PROTO(struct task_struct *wq_thread, int cpu),
	   TP_ARGS(wq_thread, cpu));

DECLARE_TRACE(workqueue_destruction,
	   TP_PROTO(struct task_struct *wq_thread),
	   TP_ARGS(wq_thread));

#endif /* __TRACE_WORKQUEUE_H */
