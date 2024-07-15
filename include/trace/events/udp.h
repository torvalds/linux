/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM udp

#if !defined(_TRACE_UDP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_UDP_H

#include <linux/udp.h>
#include <linux/tracepoint.h>
#include <trace/events/net_probe_common.h>

TRACE_EVENT(udp_fail_queue_rcv_skb,

	TP_PROTO(int rc, struct sock *sk, struct sk_buff *skb),

	TP_ARGS(rc, sk, skb),

	TP_STRUCT__entry(
		__field(int, rc)

		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u16, family)
		__array(__u8, saddr, sizeof(struct sockaddr_in6))
		__array(__u8, daddr, sizeof(struct sockaddr_in6))
	),

	TP_fast_assign(
		const struct udphdr *uh = (const struct udphdr *)udp_hdr(skb);

		__entry->rc = rc;

		/* for filtering use */
		__entry->sport = ntohs(uh->source);
		__entry->dport = ntohs(uh->dest);
		__entry->family = sk->sk_family;

		memset(__entry->saddr, 0, sizeof(struct sockaddr_in6));
		memset(__entry->daddr, 0, sizeof(struct sockaddr_in6));

		TP_STORE_ADDR_PORTS_SKB(skb, uh, __entry->saddr, __entry->daddr);
	),

	TP_printk("rc=%d family=%s src=%pISpc dest=%pISpc", __entry->rc,
		  show_family_name(__entry->family),
		  __entry->saddr, __entry->daddr)
);

#endif /* _TRACE_UDP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
