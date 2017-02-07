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
#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
