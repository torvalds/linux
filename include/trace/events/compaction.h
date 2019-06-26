/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM compaction

#if !defined(_TRACE_COMPACTION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COMPACTION_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>


DECLARE_EVENT_CLASS(mm_compaction_isolate_template,

	TP_PROTO(
		unsigned long start_pfn,
		unsigned long end_pfn,
		unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(start_pfn, end_pfn, nr_scanned, nr_taken),

	TP_STRUCT__entry(
		__field(unsigned long, start_pfn)
		__field(unsigned long, end_pfn)
		__field(unsigned long, nr_scanned)
		__field(unsigned long, nr_taken)
	),

	TP_fast_assign(
		__entry->start_pfn = start_pfn;
		__entry->end_pfn = end_pfn;
		__entry->nr_scanned = nr_scanned;
		__entry->nr_taken = nr_taken;
	),

	TP_printk("range=(0x%lx ~ 0x%lx) nr_scanned=%lu nr_taken=%lu",
		__entry->start_pfn,
		__entry->end_pfn,
		__entry->nr_scanned,
		__entry->nr_taken)
);

DEFINE_EVENT(mm_compaction_isolate_template, mm_compaction_isolate_migratepages,

	TP_PROTO(
		unsigned long start_pfn,
		unsigned long end_pfn,
		unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(start_pfn, end_pfn, nr_scanned, nr_taken)
);

DEFINE_EVENT(mm_compaction_isolate_template, mm_compaction_isolate_freepages,

	TP_PROTO(
		unsigned long start_pfn,
		unsigned long end_pfn,
		unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(start_pfn, end_pfn, nr_scanned, nr_taken)
);

#ifdef CONFIG_COMPACTION
TRACE_EVENT(mm_compaction_migratepages,

	TP_PROTO(unsigned long nr_all,
		int migrate_rc,
		struct list_head *migratepages),

	TP_ARGS(nr_all, migrate_rc, migratepages),

	TP_STRUCT__entry(
		__field(unsigned long, nr_migrated)
		__field(unsigned long, nr_failed)
	),

	TP_fast_assign(
		unsigned long nr_failed = 0;
		struct list_head *page_lru;

		/*
		 * migrate_pages() returns either a non-negative number
		 * with the number of pages that failed migration, or an
		 * error code, in which case we need to count the remaining
		 * pages manually
		 */
		if (migrate_rc >= 0)
			nr_failed = migrate_rc;
		else
			list_for_each(page_lru, migratepages)
				nr_failed++;

		__entry->nr_migrated = nr_all - nr_failed;
		__entry->nr_failed = nr_failed;
	),

	TP_printk("nr_migrated=%lu nr_failed=%lu",
		__entry->nr_migrated,
		__entry->nr_failed)
);

TRACE_EVENT(mm_compaction_begin,
	TP_PROTO(unsigned long zone_start, unsigned long migrate_pfn,
		unsigned long free_pfn, unsigned long zone_end, bool sync),

	TP_ARGS(zone_start, migrate_pfn, free_pfn, zone_end, sync),

	TP_STRUCT__entry(
		__field(unsigned long, zone_start)
		__field(unsigned long, migrate_pfn)
		__field(unsigned long, free_pfn)
		__field(unsigned long, zone_end)
		__field(bool, sync)
	),

	TP_fast_assign(
		__entry->zone_start = zone_start;
		__entry->migrate_pfn = migrate_pfn;
		__entry->free_pfn = free_pfn;
		__entry->zone_end = zone_end;
		__entry->sync = sync;
	),

	TP_printk("zone_start=0x%lx migrate_pfn=0x%lx free_pfn=0x%lx zone_end=0x%lx, mode=%s",
		__entry->zone_start,
		__entry->migrate_pfn,
		__entry->free_pfn,
		__entry->zone_end,
		__entry->sync ? "sync" : "async")
);

TRACE_EVENT(mm_compaction_end,
	TP_PROTO(unsigned long zone_start, unsigned long migrate_pfn,
		unsigned long free_pfn, unsigned long zone_end, bool sync,
		int status),

	TP_ARGS(zone_start, migrate_pfn, free_pfn, zone_end, sync, status),

	TP_STRUCT__entry(
		__field(unsigned long, zone_start)
		__field(unsigned long, migrate_pfn)
		__field(unsigned long, free_pfn)
		__field(unsigned long, zone_end)
		__field(bool, sync)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->zone_start = zone_start;
		__entry->migrate_pfn = migrate_pfn;
		__entry->free_pfn = free_pfn;
		__entry->zone_end = zone_end;
		__entry->sync = sync;
		__entry->status = status;
	),

	TP_printk("zone_start=0x%lx migrate_pfn=0x%lx free_pfn=0x%lx zone_end=0x%lx, mode=%s status=%s",
		__entry->zone_start,
		__entry->migrate_pfn,
		__entry->free_pfn,
		__entry->zone_end,
		__entry->sync ? "sync" : "async",
		__print_symbolic(__entry->status, COMPACTION_STATUS))
);

TRACE_EVENT(mm_compaction_try_to_compact_pages,

	TP_PROTO(
		int order,
		gfp_t gfp_mask,
		int prio),

	TP_ARGS(order, gfp_mask, prio),

	TP_STRUCT__entry(
		__field(int, order)
		__field(gfp_t, gfp_mask)
		__field(int, prio)
	),

	TP_fast_assign(
		__entry->order = order;
		__entry->gfp_mask = gfp_mask;
		__entry->prio = prio;
	),

	TP_printk("order=%d gfp_mask=%s priority=%d",
		__entry->order,
		show_gfp_flags(__entry->gfp_mask),
		__entry->prio)
);

DECLARE_EVENT_CLASS(mm_compaction_suitable_template,

	TP_PROTO(struct zone *zone,
		int order,
		int ret),

	TP_ARGS(zone, order, ret),

	TP_STRUCT__entry(
		__field(int, nid)
		__field(enum zone_type, idx)
		__field(int, order)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->nid = zone_to_nid(zone);
		__entry->idx = zone_idx(zone);
		__entry->order = order;
		__entry->ret = ret;
	),

	TP_printk("node=%d zone=%-8s order=%d ret=%s",
		__entry->nid,
		__print_symbolic(__entry->idx, ZONE_TYPE),
		__entry->order,
		__print_symbolic(__entry->ret, COMPACTION_STATUS))
);

DEFINE_EVENT(mm_compaction_suitable_template, mm_compaction_finished,

	TP_PROTO(struct zone *zone,
		int order,
		int ret),

	TP_ARGS(zone, order, ret)
);

DEFINE_EVENT(mm_compaction_suitable_template, mm_compaction_suitable,

	TP_PROTO(struct zone *zone,
		int order,
		int ret),

	TP_ARGS(zone, order, ret)
);

DECLARE_EVENT_CLASS(mm_compaction_defer_template,

	TP_PROTO(struct zone *zone, int order),

	TP_ARGS(zone, order),

	TP_STRUCT__entry(
		__field(int, nid)
		__field(enum zone_type, idx)
		__field(int, order)
		__field(unsigned int, considered)
		__field(unsigned int, defer_shift)
		__field(int, order_failed)
	),

	TP_fast_assign(
		__entry->nid = zone_to_nid(zone);
		__entry->idx = zone_idx(zone);
		__entry->order = order;
		__entry->considered = zone->compact_considered;
		__entry->defer_shift = zone->compact_defer_shift;
		__entry->order_failed = zone->compact_order_failed;
	),

	TP_printk("node=%d zone=%-8s order=%d order_failed=%d consider=%u limit=%lu",
		__entry->nid,
		__print_symbolic(__entry->idx, ZONE_TYPE),
		__entry->order,
		__entry->order_failed,
		__entry->considered,
		1UL << __entry->defer_shift)
);

DEFINE_EVENT(mm_compaction_defer_template, mm_compaction_deferred,

	TP_PROTO(struct zone *zone, int order),

	TP_ARGS(zone, order)
);

DEFINE_EVENT(mm_compaction_defer_template, mm_compaction_defer_compaction,

	TP_PROTO(struct zone *zone, int order),

	TP_ARGS(zone, order)
);

DEFINE_EVENT(mm_compaction_defer_template, mm_compaction_defer_reset,

	TP_PROTO(struct zone *zone, int order),

	TP_ARGS(zone, order)
);

TRACE_EVENT(mm_compaction_kcompactd_sleep,

	TP_PROTO(int nid),

	TP_ARGS(nid),

	TP_STRUCT__entry(
		__field(int, nid)
	),

	TP_fast_assign(
		__entry->nid = nid;
	),

	TP_printk("nid=%d", __entry->nid)
);

DECLARE_EVENT_CLASS(kcompactd_wake_template,

	TP_PROTO(int nid, int order, enum zone_type classzone_idx),

	TP_ARGS(nid, order, classzone_idx),

	TP_STRUCT__entry(
		__field(int, nid)
		__field(int, order)
		__field(enum zone_type, classzone_idx)
	),

	TP_fast_assign(
		__entry->nid = nid;
		__entry->order = order;
		__entry->classzone_idx = classzone_idx;
	),

	TP_printk("nid=%d order=%d classzone_idx=%-8s",
		__entry->nid,
		__entry->order,
		__print_symbolic(__entry->classzone_idx, ZONE_TYPE))
);

DEFINE_EVENT(kcompactd_wake_template, mm_compaction_wakeup_kcompactd,

	TP_PROTO(int nid, int order, enum zone_type classzone_idx),

	TP_ARGS(nid, order, classzone_idx)
);

DEFINE_EVENT(kcompactd_wake_template, mm_compaction_kcompactd_wake,

	TP_PROTO(int nid, int order, enum zone_type classzone_idx),

	TP_ARGS(nid, order, classzone_idx)
);
#endif

#endif /* _TRACE_COMPACTION_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
