/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sunvnet

#if !defined(_TRACE_SUNVNET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SUNVNET_H

#include <linux/tracepoint.h>

TRACE_EVENT(vnet_rx_one,

	TP_PROTO(int lsid, int rsid, int index, int needs_ack),

	TP_ARGS(lsid, rsid, index, needs_ack),

	TP_STRUCT__entry(
		__field(int, lsid)
		__field(int, rsid)
		__field(int, index)
		__field(int, needs_ack)
	),

	TP_fast_assign(
		__entry->lsid = lsid;
		__entry->rsid = rsid;
		__entry->index = index;
		__entry->needs_ack = needs_ack;
	),

	TP_printk("(%x:%x) walk_rx_one index %d; needs_ack %d",
		__entry->lsid, __entry->rsid,
		__entry->index, __entry->needs_ack)
);

DECLARE_EVENT_CLASS(vnet_tx_stopped_ack_template,

	TP_PROTO(int lsid, int rsid, int ack_end, int npkts),

	TP_ARGS(lsid, rsid, ack_end, npkts),

	TP_STRUCT__entry(
		__field(int, lsid)
		__field(int, rsid)
		__field(int, ack_end)
		__field(int, npkts)
	),

	TP_fast_assign(
		__entry->lsid = lsid;
		__entry->rsid = rsid;
		__entry->ack_end = ack_end;
		__entry->npkts = npkts;
	),

	TP_printk("(%x:%x) stopped ack for %d; npkts %d",
		__entry->lsid, __entry->rsid,
		__entry->ack_end, __entry->npkts)
);
DEFINE_EVENT(vnet_tx_stopped_ack_template, vnet_tx_send_stopped_ack,
	     TP_PROTO(int lsid, int rsid, int ack_end, int npkts),
	     TP_ARGS(lsid, rsid, ack_end, npkts));
DEFINE_EVENT(vnet_tx_stopped_ack_template, vnet_tx_defer_stopped_ack,
	     TP_PROTO(int lsid, int rsid, int ack_end, int npkts),
	     TP_ARGS(lsid, rsid, ack_end, npkts));
DEFINE_EVENT(vnet_tx_stopped_ack_template, vnet_tx_pending_stopped_ack,
	     TP_PROTO(int lsid, int rsid, int ack_end, int npkts),
	     TP_ARGS(lsid, rsid, ack_end, npkts));

TRACE_EVENT(vnet_rx_stopped_ack,

	TP_PROTO(int lsid, int rsid, int end),

	TP_ARGS(lsid, rsid, end),

	TP_STRUCT__entry(
		__field(int, lsid)
		__field(int, rsid)
		__field(int, end)
	),

	TP_fast_assign(
		__entry->lsid = lsid;
		__entry->rsid = rsid;
		__entry->end = end;
	),

	TP_printk("(%x:%x) stopped ack for index %d",
		__entry->lsid, __entry->rsid, __entry->end)
);

TRACE_EVENT(vnet_tx_trigger,

	TP_PROTO(int lsid, int rsid, int start, int err),

	TP_ARGS(lsid, rsid, start, err),

	TP_STRUCT__entry(
		__field(int, lsid)
		__field(int, rsid)
		__field(int, start)
		__field(int, err)
	),

	TP_fast_assign(
		__entry->lsid = lsid;
		__entry->rsid = rsid;
		__entry->start = start;
		__entry->err = err;
	),

	TP_printk("(%x:%x) Tx trigger for %d sent with err %d %s",
		__entry->lsid, __entry->rsid, __entry->start,
		__entry->err, __entry->err > 0 ? "(ok)" : " ")
);

TRACE_EVENT(vnet_skip_tx_trigger,

	TP_PROTO(int lsid, int rsid, int last),

	TP_ARGS(lsid, rsid, last),

	TP_STRUCT__entry(
		__field(int, lsid)
		__field(int, rsid)
		__field(int, last)
	),

	TP_fast_assign(
		__entry->lsid = lsid;
		__entry->rsid = rsid;
		__entry->last = last;
	),

	TP_printk("(%x:%x) Skip Tx trigger. Last trigger sent was %d",
		__entry->lsid, __entry->rsid, __entry->last)
);
#endif /* _TRACE_SOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
