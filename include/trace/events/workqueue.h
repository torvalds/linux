/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM workqueue

#if !defined(_TRACE_WORKQUEUE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WORKQUEUE_H

#include <linux/tracepoint.h>
#include <linux/workqueue.h>

struct pool_workqueue;
struct worker_pool;

/**
 * workqueue_queue_work - called when a work gets queued
 * @req_cpu:	the requested cpu
 * @pwq:	pointer to struct pool_workqueue
 * @work:	pointer to struct work_struct
 *
 * This event occurs when a work is queued immediately or once a
 * delayed work is actually queued on a workqueue (ie: once the delay
 * has been reached).
 */
TRACE_EVENT(workqueue_queue_work,

	TP_PROTO(int req_cpu, struct pool_workqueue *pwq,
		 struct work_struct *work),

	TP_ARGS(req_cpu, pwq, work),

	TP_STRUCT__entry(
		__field( void *,	work	)
		__field( void *,	function)
		__string( workqueue,	pwq->wq->name)
		__field( int,	req_cpu	)
		__field( int,	cpu	)
	),

	TP_fast_assign(
		__entry->work		= work;
		__entry->function	= work->func;
		__assign_str(workqueue);
		__entry->req_cpu	= req_cpu;
		__entry->cpu		= pwq->pool->cpu;
	),

	TP_printk("work struct=%p function=%ps workqueue=%s req_cpu=%d cpu=%d",
		  __entry->work, __entry->function, __get_str(workqueue),
		  __entry->req_cpu, __entry->cpu)
);

/**
 * workqueue_activate_work - called when a work gets activated
 * @work:	pointer to struct work_struct
 *
 * This event occurs when a queued work is put on the active queue,
 * which happens immediately after queueing unless @max_active limit
 * is reached.
 */
TRACE_EVENT(workqueue_activate_work,

	TP_PROTO(struct work_struct *work),

	TP_ARGS(work),

	TP_STRUCT__entry(
		__field( void *,	work	)
		__field( void *,	function)
	),

	TP_fast_assign(
		__entry->work		= work;
		__entry->function	= work->func;
	),

	TP_printk("work struct %p function=%ps ", __entry->work, __entry->function)
);

/**
 * workqueue_execute_start - called immediately before the workqueue callback
 * @work:	pointer to struct work_struct
 *
 * Allows to track workqueue execution.
 */
TRACE_EVENT(workqueue_execute_start,

	TP_PROTO(struct work_struct *work),

	TP_ARGS(work),

	TP_STRUCT__entry(
		__field( void *,	work	)
		__field( void *,	function)
	),

	TP_fast_assign(
		__entry->work		= work;
		__entry->function	= work->func;
	),

	TP_printk("work struct %p: function %ps", __entry->work, __entry->function)
);

/**
 * workqueue_execute_end - called immediately after the workqueue callback
 * @work:	pointer to struct work_struct
 * @function:   pointer to worker function
 *
 * Allows to track workqueue execution.
 */
TRACE_EVENT(workqueue_execute_end,

	TP_PROTO(struct work_struct *work, work_func_t function),

	TP_ARGS(work, function),

	TP_STRUCT__entry(
		__field( void *,	work	)
		__field( void *,	function)
	),

	TP_fast_assign(
		__entry->work		= work;
		__entry->function	= function;
	),

	TP_printk("work struct %p: function %ps", __entry->work, __entry->function)
);

/**
 * workqueue_cpu_intensive - called when a work item exceeds cpu_intensive threshold
 * @pwq:	pointer to struct pool_workqueue
 * @work:	pointer to struct work_struct
 * @function:	pointer to worker function
 * @duration_us: CPU time consumed in microseconds
 *
 * This event occurs when a concurrency-managed work item runs for longer
 * than wq_cpu_intensive_thresh_us without sleeping and is excluded from
 * concurrency management to prevent stalling other work items.
 */
TRACE_EVENT(workqueue_cpu_intensive,

	TP_PROTO(struct pool_workqueue *pwq, struct work_struct *work,
		 work_func_t function, u64 duration_us),

	TP_ARGS(pwq, work, function, duration_us),

	TP_STRUCT__entry(
		__field( void *,	work		)
		__field( void *,	function	)
		__string( workqueue,	pwq->wq->name	)
		__field( int,		cpu		)
		__field( u64,		duration_us	)
	),

	TP_fast_assign(
		__entry->work		= work;
		__entry->function	= function;
		__assign_str(workqueue);
		__entry->cpu		= pwq->pool->cpu;
		__entry->duration_us	= duration_us;
	),

	TP_printk("work struct=%p function=%ps workqueue=%s cpu=%d duration_us=%llu",
		  __entry->work, __entry->function, __get_str(workqueue),
		  __entry->cpu, __entry->duration_us)
);

/**
 * workqueue_mayday - called when a pool_workqueue sends mayday to rescuer
 * @pwq:	pointer to struct pool_workqueue
 *
 * This event occurs when a worker pool fails to create a new worker and
 * requests the workqueue's rescuer thread to process pending works.
 */
TRACE_EVENT(workqueue_mayday,

	TP_PROTO(struct pool_workqueue *pwq),

	TP_ARGS(pwq),

	TP_STRUCT__entry(
		__string( workqueue,	pwq->wq->name	)
		__field( int,		pool_id		)
		__field( int,		cpu		)
		__field( int,		nr_active	)
	),

	TP_fast_assign(
		__assign_str(workqueue);
		__entry->pool_id	= pwq->pool->id;
		__entry->cpu		= pwq->pool->cpu;
		__entry->nr_active	= pwq->nr_active;
	),

	TP_printk("workqueue=%s pool_id=%d cpu=%d nr_active=%d",
		  __get_str(workqueue), __entry->pool_id, __entry->cpu,
		  __entry->nr_active)
);

/**
 * workqueue_rescued - called when a work item is assigned to a rescuer
 * @pwq:	pointer to struct pool_workqueue
 * @work:	pointer to struct work_struct
 * @function:	pointer to worker function
 *
 * This event occurs when a work item is claimed by a rescuer thread
 * to guarantee forward progress.
 */
TRACE_EVENT(workqueue_rescued,

	TP_PROTO(struct pool_workqueue *pwq, struct work_struct *work,
		 work_func_t function),

	TP_ARGS(pwq, work, function),

	TP_STRUCT__entry(
		__field( void *,	work		)
		__field( void *,	function	)
		__string( workqueue,	pwq->wq->name	)
		__field( int,		cpu		)
	),

	TP_fast_assign(
		__entry->work		= work;
		__entry->function	= function;
		__assign_str(workqueue);
		__entry->cpu		= pwq->pool->cpu;
	),

	TP_printk("work struct=%p function=%ps workqueue=%s cpu=%d",
		  __entry->work, __entry->function, __get_str(workqueue),
		  __entry->cpu)
);

/**
 * workqueue_bh_budget_yield - called when a BH worker yields due to budget exhaustion
 * @pool:	pointer to struct worker_pool
 * @restarts:	number of restarts executed
 * @timeout:	whether execution hit the time limit (BH_WORKER_JIFFIES)
 * @highpri:	whether this is a high-priority BH pool
 *
 * This event occurs when a bottom-half (BH) worker pool running in softirq
 * context exhausts its execution time slice or restart limit and must yield
 * execution.
 */
TRACE_EVENT(workqueue_bh_budget_yield,

	TP_PROTO(struct worker_pool *pool, int restarts, bool timeout, bool highpri),

	TP_ARGS(pool, restarts, timeout, highpri),

	TP_STRUCT__entry(
		__field( int,	pool_id		)
		__field( int,	cpu		)
		__field( int,	restarts	)
		__field( bool,	timeout		)
		__field( bool,	highpri		)
	),

	TP_fast_assign(
		__entry->pool_id	= pool->id;
		__entry->cpu		= pool->cpu;
		__entry->restarts	= restarts;
		__entry->timeout	= timeout;
		__entry->highpri	= highpri;
	),

	TP_printk("pool_id=%d cpu=%d restarts=%d timeout=%d highpri=%d",
		  __entry->pool_id, __entry->cpu, __entry->restarts,
		  __entry->timeout, __entry->highpri)
);

#endif /*  _TRACE_WORKQUEUE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

