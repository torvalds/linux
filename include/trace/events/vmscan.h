#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#if !defined(_TRACE_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VMSCAN_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include "gfpflags.h"

TRACE_EVENT(mm_vmscan_kswapd_sleep,

	TP_PROTO(int nid),

	TP_ARGS(nid),

	TP_STRUCT__entry(
		__field(	int,	nid	)
	),

	TP_fast_assign(
		__entry->nid	= nid;
	),

	TP_printk("nid=%d", __entry->nid)
);

TRACE_EVENT(mm_vmscan_kswapd_wake,

	TP_PROTO(int nid, int order),

	TP_ARGS(nid, order),

	TP_STRUCT__entry(
		__field(	int,	nid	)
		__field(	int,	order	)
	),

	TP_fast_assign(
		__entry->nid	= nid;
		__entry->order	= order;
	),

	TP_printk("nid=%d order=%d", __entry->nid, __entry->order)
);

TRACE_EVENT(mm_vmscan_wakeup_kswapd,

	TP_PROTO(int nid, int zid, int order),

	TP_ARGS(nid, zid, order),

	TP_STRUCT__entry(
		__field(	int,		nid	)
		__field(	int,		zid	)
		__field(	int,		order	)
	),

	TP_fast_assign(
		__entry->nid		= nid;
		__entry->zid		= zid;
		__entry->order		= order;
	),

	TP_printk("nid=%d zid=%d order=%d",
		__entry->nid,
		__entry->zid,
		__entry->order)
);

TRACE_EVENT(mm_vmscan_direct_reclaim_begin,

	TP_PROTO(int order, int may_writepage, gfp_t gfp_flags),

	TP_ARGS(order, may_writepage, gfp_flags),

	TP_STRUCT__entry(
		__field(	int,	order		)
		__field(	int,	may_writepage	)
		__field(	gfp_t,	gfp_flags	)
	),

	TP_fast_assign(
		__entry->order		= order;
		__entry->may_writepage	= may_writepage;
		__entry->gfp_flags	= gfp_flags;
	),

	TP_printk("order=%d may_writepage=%d gfp_flags=%s",
		__entry->order,
		__entry->may_writepage,
		show_gfp_flags(__entry->gfp_flags))
);

TRACE_EVENT(mm_vmscan_direct_reclaim_end,

	TP_PROTO(unsigned long nr_reclaimed),

	TP_ARGS(nr_reclaimed),

	TP_STRUCT__entry(
		__field(	unsigned long,	nr_reclaimed	)
	),

	TP_fast_assign(
		__entry->nr_reclaimed	= nr_reclaimed;
	),

	TP_printk("nr_reclaimed=%lu", __entry->nr_reclaimed)
);

#endif /* _TRACE_VMSCAN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
