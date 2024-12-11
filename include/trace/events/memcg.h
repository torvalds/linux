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

TRACE_EVENT(memcg_flush_stats,

	TP_PROTO(struct mem_cgroup *memcg, s64 stats_updates,
		bool force, bool needs_flush),

	TP_ARGS(memcg, stats_updates, force, needs_flush),

	TP_STRUCT__entry(
		__field(u64, id)
		__field(s64, stats_updates)
		__field(bool, force)
		__field(bool, needs_flush)
	),

	TP_fast_assign(
		__entry->id = cgroup_id(memcg->css.cgroup);
		__entry->stats_updates = stats_updates;
		__entry->force = force;
		__entry->needs_flush = needs_flush;
	),

	TP_printk("memcg_id=%llu stats_updates=%lld force=%d needs_flush=%d",
		__entry->id, __entry->stats_updates,
		__entry->force, __entry->needs_flush)
);

#endif /* _TRACE_MEMCG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
