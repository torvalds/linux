/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM memcg

#if !defined(_TRACE_MEMCG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MEMCG_H

#include <linux/memcontrol.h>
#include <linux/tracepoint.h>


DECLARE_EVENT_CLASS(memcg_rstat_stats,

	TP_PROTO(struct mem_cgroup *memcg, int item, int val),

	TP_ARGS(memcg, item, val),

	TP_STRUCT__entry(
		__field(u64, id)
		__field(int, item)
		__field(int, val)
	),

	TP_fast_assign(
		__entry->id = cgroup_id(memcg->css.cgroup);
		__entry->item = item;
		__entry->val = val;
	),

	TP_printk("memcg_id=%llu item=%d val=%d",
		  __entry->id, __entry->item, __entry->val)
);

DEFINE_EVENT(memcg_rstat_stats, mod_memcg_state,

	TP_PROTO(struct mem_cgroup *memcg, int item, int val),

	TP_ARGS(memcg, item, val)
);

DEFINE_EVENT(memcg_rstat_stats, mod_memcg_lruvec_state,

	TP_PROTO(struct mem_cgroup *memcg, int item, int val),

	TP_ARGS(memcg, item, val)
);

DECLARE_EVENT_CLASS(memcg_rstat_events,

	TP_PROTO(struct mem_cgroup *memcg, int item, unsigned long val),

	TP_ARGS(memcg, item, val),

	TP_STRUCT__entry(
		__field(u64, id)
		__field(int, item)
		__field(unsigned long, val)
	),

	TP_fast_assign(
		__entry->id = cgroup_id(memcg->css.cgroup);
		__entry->item = item;
		__entry->val = val;
	),

	TP_printk("memcg_id=%llu item=%d val=%lu",
		  __entry->id, __entry->item, __entry->val)
);

DEFINE_EVENT(memcg_rstat_events, count_memcg_events,

	TP_PROTO(struct mem_cgroup *memcg, int item, unsigned long val),

	TP_ARGS(memcg, item, val)
);


#endif /* _TRACE_MEMCG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
