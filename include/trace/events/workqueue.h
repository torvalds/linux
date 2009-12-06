#undef TRACE_SYSTEM
#define TRACE_SYSTEM workqueue

#if !defined(_TRACE_WORKQUEUE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WORKQUEUE_H

#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(workqueue,

	TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),

	TP_ARGS(wq_thread, work),

	TP_STRUCT__entry(
		__array(char,		thread_comm,	TASK_COMM_LEN)
		__field(pid_t,		thread_pid)
		__field(work_func_t,	func)
	),

	TP_fast_assign(
		memcpy(__entry->thread_comm, wq_thread->comm, TASK_COMM_LEN);
		__entry->thread_pid	= wq_thread->pid;
		__entry->func		= work->func;
	),

	TP_printk("thread=%s:%d func=%pf", __entry->thread_comm,
		__entry->thread_pid, __entry->func)
);

DEFINE_EVENT(workqueue, workqueue_insertion,

	TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),

	TP_ARGS(wq_thread, work)
);

DEFINE_EVENT(workqueue, workqueue_execution,

	TP_PROTO(struct task_struct *wq_thread, struct work_struct *work),

	TP_ARGS(wq_thread, work)
);

/* Trace the creation of one workqueue thread on a cpu */
TRACE_EVENT(workqueue_creation,

	TP_PROTO(struct task_struct *wq_thread, int cpu),

	TP_ARGS(wq_thread, cpu),

	TP_STRUCT__entry(
		__array(char,	thread_comm,	TASK_COMM_LEN)
		__field(pid_t,	thread_pid)
		__field(int,	cpu)
	),

	TP_fast_assign(
		memcpy(__entry->thread_comm, wq_thread->comm, TASK_COMM_LEN);
		__entry->thread_pid	= wq_thread->pid;
		__entry->cpu		= cpu;
	),

	TP_printk("thread=%s:%d cpu=%d", __entry->thread_comm,
		__entry->thread_pid, __entry->cpu)
);

TRACE_EVENT(workqueue_destruction,

	TP_PROTO(struct task_struct *wq_thread),

	TP_ARGS(wq_thread),

	TP_STRUCT__entry(
		__array(char,	thread_comm,	TASK_COMM_LEN)
		__field(pid_t,	thread_pid)
	),

	TP_fast_assign(
		memcpy(__entry->thread_comm, wq_thread->comm, TASK_COMM_LEN);
		__entry->thread_pid	= wq_thread->pid;
	),

	TP_printk("thread=%s:%d", __entry->thread_comm, __entry->thread_pid)
);

#endif /* _TRACE_WORKQUEUE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
