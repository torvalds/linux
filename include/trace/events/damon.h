/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM damon

#if !defined(_TRACE_DAMON_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DAMON_H

#include <linux/damon.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(damon_aggregated,

	TP_PROTO(struct damon_target *t, struct damon_region *r,
		unsigned int nr_regions),

	TP_ARGS(t, r, nr_regions),

	TP_STRUCT__entry(
		__field(unsigned long, target_id)
		__field(unsigned int, nr_regions)
		__field(unsigned long, start)
		__field(unsigned long, end)
		__field(unsigned int, nr_accesses)
	),

	TP_fast_assign(
		__entry->target_id = t->id;
		__entry->nr_regions = nr_regions;
		__entry->start = r->ar.start;
		__entry->end = r->ar.end;
		__entry->nr_accesses = r->nr_accesses;
	),

	TP_printk("target_id=%lu nr_regions=%u %lu-%lu: %u",
			__entry->target_id, __entry->nr_regions,
			__entry->start, __entry->end, __entry->nr_accesses)
);

#endif /* _TRACE_DAMON_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
