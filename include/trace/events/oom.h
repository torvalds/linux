#undef TRACE_SYSTEM
#define TRACE_SYSTEM oom

#if !defined(_TRACE_OOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OOM_H
#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>

TRACE_EVENT(oom_score_adj_update,

	TP_PROTO(struct task_struct *task),

	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(	pid_t,	pid)
		__array(	char,	comm,	TASK_COMM_LEN )
		__field(	short,	oom_score_adj)
	),

	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->oom_score_adj = task->signal->oom_score_adj;
	),

	TP_printk("pid=%d comm=%s oom_score_adj=%hd",
		__entry->pid, __entry->comm, __entry->oom_score_adj)
);

TRACE_EVENT(reclaim_retry_zone,

	TP_PROTO(struct zoneref *zoneref,
		int order,
		unsigned long reclaimable,
		unsigned long available,
		unsigned long min_wmark,
		int no_progress_loops,
		bool wmark_check),

	TP_ARGS(zoneref, order, reclaimable, available, min_wmark, no_progress_loops, wmark_check),

	TP_STRUCT__entry(
		__field(	int, node)
		__field(	int, zone_idx)
		__field(	int,	order)
		__field(	unsigned long,	reclaimable)
		__field(	unsigned long,	available)
		__field(	unsigned long,	min_wmark)
		__field(	int,	no_progress_loops)
		__field(	bool,	wmark_check)
	),

	TP_fast_assign(
		__entry->node = zone_to_nid(zoneref->zone);
		__entry->zone_idx = zoneref->zone_idx;
		__entry->order = order;
		__entry->reclaimable = reclaimable;
		__entry->available = available;
		__entry->min_wmark = min_wmark;
		__entry->no_progress_loops = no_progress_loops;
		__entry->wmark_check = wmark_check;
	),

	TP_printk("node=%d zone=%-8s order=%d reclaimable=%lu available=%lu min_wmark=%lu no_progress_loops=%d wmark_check=%d",
			__entry->node, __print_symbolic(__entry->zone_idx, ZONE_TYPE),
			__entry->order,
			__entry->reclaimable, __entry->available, __entry->min_wmark,
			__entry->no_progress_loops,
			__entry->wmark_check)
);

TRACE_EVENT(mark_victim,
	TP_PROTO(int pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->pid = pid;
	),

	TP_printk("pid=%d", __entry->pid)
);

TRACE_EVENT(wake_reaper,
	TP_PROTO(int pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->pid = pid;
	),

	TP_printk("pid=%d", __entry->pid)
);

TRACE_EVENT(start_task_reaping,
	TP_PROTO(int pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->pid = pid;
	),

	TP_printk("pid=%d", __entry->pid)
);

TRACE_EVENT(finish_task_reaping,
	TP_PROTO(int pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->pid = pid;
	),

	TP_printk("pid=%d", __entry->pid)
);

TRACE_EVENT(skip_task_reaping,
	TP_PROTO(int pid),

	TP_ARGS(pid),

	TP_STRUCT__entry(
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->pid = pid;
	),

	TP_printk("pid=%d", __entry->pid)
);

#ifdef CONFIG_COMPACTION
TRACE_EVENT(compact_retry,

	TP_PROTO(int order,
		enum compact_priority priority,
		enum compact_result result,
		int retries,
		int max_retries,
		bool ret),

	TP_ARGS(order, priority, result, retries, max_retries, ret),

	TP_STRUCT__entry(
		__field(	int, order)
		__field(	int, priority)
		__field(	int, result)
		__field(	int, retries)
		__field(	int, max_retries)
		__field(	bool, ret)
	),

	TP_fast_assign(
		__entry->order = order;
		__entry->priority = priority;
		__entry->result = compact_result_to_feedback(result);
		__entry->retries = retries;
		__entry->max_retries = max_retries;
		__entry->ret = ret;
	),

	TP_printk("order=%d priority=%s compaction_result=%s retries=%d max_retries=%d should_retry=%d",
			__entry->order,
			__print_symbolic(__entry->priority, COMPACTION_PRIORITY),
			__print_symbolic(__entry->result, COMPACTION_FEEDBACK),
			__entry->retries, __entry->max_retries,
			__entry->ret)
);
#endif /* CONFIG_COMPACTION */
#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
