/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sctp

#if !defined(_TRACE_SCTP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SCTP_H

#include <net/sctp/structs.h>
#include <linux/tracepoint.h>

TRACE_EVENT(sctp_probe_path,

	TP_PROTO(struct sctp_transport *sp,
		 const struct sctp_association *asoc),

	TP_ARGS(sp, asoc),

	TP_STRUCT__entry(
		__field(__u64, asoc)
		__field(__u32, primary)
		__array(__u8, ipaddr, sizeof(union sctp_addr))
		__field(__u32, state)
		__field(__u32, cwnd)
		__field(__u32, ssthresh)
		__field(__u32, flight_size)
		__field(__u32, partial_bytes_acked)
		__field(__u32, pathmtu)
	),

	TP_fast_assign(
		__entry->asoc = (unsigned long)asoc;
		__entry->primary = (sp == asoc->peer.primary_path);
		memcpy(__entry->ipaddr, &sp->ipaddr, sizeof(union sctp_addr));
		__entry->state = sp->state;
		__entry->cwnd = sp->cwnd;
		__entry->ssthresh = sp->ssthresh;
		__entry->flight_size = sp->flight_size;
		__entry->partial_bytes_acked = sp->partial_bytes_acked;
		__entry->pathmtu = sp->pathmtu;
	),

	TP_printk("asoc=%#llx%s ipaddr=%pISpc state=%u cwnd=%u ssthresh=%u "
		  "flight_size=%u partial_bytes_acked=%u pathmtu=%u",
		  __entry->asoc, __entry->primary ? "(*)" : "",
		  __entry->ipaddr, __entry->state, __entry->cwnd,
		  __entry->ssthresh, __entry->flight_size,
		  __entry->partial_bytes_acked, __entry->pathmtu)
);

TRACE_EVENT(sctp_probe,

	TP_PROTO(const struct sctp_endpoint *ep,
		 const struct sctp_association *asoc,
		 struct sctp_chunk *chunk),

	TP_ARGS(ep, asoc, chunk),

	TP_STRUCT__entry(
		__field(__u64, asoc)
		__field(__u32, mark)
		__field(__u16, bind_port)
		__field(__u16, peer_port)
		__field(__u32, pathmtu)
		__field(__u32, rwnd)
		__field(__u16, unack_data)
	),

	TP_fast_assign(
		struct sk_buff *skb = chunk->skb;

		__entry->asoc = (unsigned long)asoc;
		__entry->mark = skb->mark;
		__entry->bind_port = ep->base.bind_addr.port;
		__entry->peer_port = asoc->peer.port;
		__entry->pathmtu = asoc->pathmtu;
		__entry->rwnd = asoc->peer.rwnd;
		__entry->unack_data = asoc->unack_data;

		if (trace_sctp_probe_path_enabled()) {
			struct sctp_transport *sp;

			list_for_each_entry(sp, &asoc->peer.transport_addr_list,
					    transports) {
				trace_sctp_probe_path(sp, asoc);
			}
		}
	),

	TP_printk("asoc=%#llx mark=%#x bind_port=%d peer_port=%d pathmtu=%d "
		  "rwnd=%u unack_data=%d",
		  __entry->asoc, __entry->mark, __entry->bind_port,
		  __entry->peer_port, __entry->pathmtu, __entry->rwnd,
		  __entry->unack_data)
);

#endif /* _TRACE_SCTP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
