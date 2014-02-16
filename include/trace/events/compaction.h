#undef TRACE_SYSTEM
#define TRACE_SYSTEM compaction

#if !defined(_TRACE_COMPACTION_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COMPACTION_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <trace/events/gfpflags.h>

DECLARE_EVENT_CLASS(mm_compaction_isolate_template,

	TP_PROTO(unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(nr_scanned, nr_taken),

	TP_STRUCT__entry(
		__field(unsigned long, nr_scanned)
		__field(unsigned long, nr_taken)
	),

	TP_fast_assign(
		__entry->nr_scanned = nr_scanned;
		__entry->nr_taken = nr_taken;
	),

	TP_printk("nr_scanned=%lu nr_taken=%lu",
		__entry->nr_scanned,
		__entry->nr_taken)
);

DEFINE_EVENT(mm_compaction_isolate_template, mm_compaction_isolate_migratepages,

	TP_PROTO(unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(nr_scanned, nr_taken)
);

DEFINE_EVENT(mm_compaction_isolate_template, mm_compaction_isolate_freepages,
	TP_PROTO(unsigned long nr_scanned,
		unsigned long nr_taken),

	TP_ARGS(nr_scanned, nr_taken)
);

TRACE_EVENT(mm_compaction_migratepages,

	TP_PROTO(unsigned long nr_migrated,
		unsigned long nr_failed),

	TP_ARGS(nr_migrated, nr_failed),

	TP_STRUCT__entry(
		__field(unsigned long, nr_migrated)
		__field(unsigned long, nr_failed)
	),

	TP_fast_assign(
		__entry->nr_migrated = nr_migrated;
		__entry->nr_failed = nr_failed;
	),

	TP_printk("nr_migrated=%lu nr_failed=%lu",
		__entry->nr_migrated,
		__entry->nr_failed)
);

TRACE_EVENT(mm_compaction_begin,
	TP_PROTO(unsigned long zone_start, unsigned long migrate_start,
		unsigned long free_start, unsigned long zone_end),

	TP_ARGS(zone_start, migrate_start, free_start, zone_end),

	TP_STRUCT__entry(
		__field(unsigned long, zone_start)
		__field(unsigned long, migrate_start)
		__field(unsigned long, free_start)
		__field(unsigned long, zone_end)
	),

	TP_fast_assign(
		__entry->zone_start = zone_start;
		__entry->migrate_start = migrate_start;
		__entry->free_start = free_start;
		__entry->zone_end = zone_end;
	),

	TP_printk("zone_start=%lu migrate_start=%lu free_start=%lu zone_end=%lu",
		__entry->zone_start,
		__entry->migrate_start,
		__entry->free_start,
		__entry->zone_end)
);

TRACE_EVENT(mm_compaction_end,
	TP_PROTO(int status),

	TP_ARGS(status),

	TP_STRUCT__entry(
		__field(int, status)
	),

	TP_fast_assign(
		__entry->status = status;
	),

	TP_printk("status=%d", __entry->status)
);

#endif /* _TRACE_COMPACTION_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
