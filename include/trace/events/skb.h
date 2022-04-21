/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM skb

#if !defined(_TRACE_SKB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SKB_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/tracepoint.h>

#define TRACE_SKB_DROP_REASON					\
	EM(SKB_DROP_REASON_NOT_SPECIFIED, NOT_SPECIFIED)	\
	EM(SKB_DROP_REASON_NO_SOCKET, NO_SOCKET)		\
	EM(SKB_DROP_REASON_PKT_TOO_SMALL, PKT_TOO_SMALL)	\
	EM(SKB_DROP_REASON_TCP_CSUM, TCP_CSUM)			\
	EM(SKB_DROP_REASON_SOCKET_FILTER, SOCKET_FILTER)	\
	EM(SKB_DROP_REASON_UDP_CSUM, UDP_CSUM)			\
	EM(SKB_DROP_REASON_NETFILTER_DROP, NETFILTER_DROP)	\
	EM(SKB_DROP_REASON_OTHERHOST, OTHERHOST)		\
	EM(SKB_DROP_REASON_IP_CSUM, IP_CSUM)			\
	EM(SKB_DROP_REASON_IP_INHDR, IP_INHDR)			\
	EM(SKB_DROP_REASON_IP_RPFILTER, IP_RPFILTER)		\
	EM(SKB_DROP_REASON_UNICAST_IN_L2_MULTICAST,		\
	   UNICAST_IN_L2_MULTICAST)				\
	EM(SKB_DROP_REASON_XFRM_POLICY, XFRM_POLICY)		\
	EM(SKB_DROP_REASON_IP_NOPROTO, IP_NOPROTO)		\
	EM(SKB_DROP_REASON_SOCKET_RCVBUFF, SOCKET_RCVBUFF)	\
	EM(SKB_DROP_REASON_PROTO_MEM, PROTO_MEM)		\
	EM(SKB_DROP_REASON_TCP_MD5NOTFOUND, TCP_MD5NOTFOUND)	\
	EM(SKB_DROP_REASON_TCP_MD5UNEXPECTED,			\
	   TCP_MD5UNEXPECTED)					\
	EM(SKB_DROP_REASON_TCP_MD5FAILURE, TCP_MD5FAILURE)	\
	EM(SKB_DROP_REASON_SOCKET_BACKLOG, SOCKET_BACKLOG)	\
	EM(SKB_DROP_REASON_TCP_FLAGS, TCP_FLAGS)		\
	EM(SKB_DROP_REASON_TCP_ZEROWINDOW, TCP_ZEROWINDOW)	\
	EM(SKB_DROP_REASON_TCP_OLD_DATA, TCP_OLD_DATA)		\
	EM(SKB_DROP_REASON_TCP_OVERWINDOW, TCP_OVERWINDOW)	\
	EM(SKB_DROP_REASON_TCP_OFOMERGE, TCP_OFOMERGE)		\
	EM(SKB_DROP_REASON_IP_OUTNOROUTES, IP_OUTNOROUTES)	\
	EM(SKB_DROP_REASON_BPF_CGROUP_EGRESS,			\
	   BPF_CGROUP_EGRESS)					\
	EM(SKB_DROP_REASON_IPV6DISABLED, IPV6DISABLED)		\
	EM(SKB_DROP_REASON_NEIGH_CREATEFAIL, NEIGH_CREATEFAIL)	\
	EM(SKB_DROP_REASON_NEIGH_FAILED, NEIGH_FAILED)		\
	EM(SKB_DROP_REASON_NEIGH_QUEUEFULL, NEIGH_QUEUEFULL)	\
	EM(SKB_DROP_REASON_NEIGH_DEAD, NEIGH_DEAD)		\
	EM(SKB_DROP_REASON_TC_EGRESS, TC_EGRESS)		\
	EM(SKB_DROP_REASON_QDISC_DROP, QDISC_DROP)		\
	EM(SKB_DROP_REASON_CPU_BACKLOG, CPU_BACKLOG)		\
	EM(SKB_DROP_REASON_XDP, XDP)				\
	EM(SKB_DROP_REASON_TC_INGRESS, TC_INGRESS)		\
	EM(SKB_DROP_REASON_PTYPE_ABSENT, PTYPE_ABSENT)		\
	EM(SKB_DROP_REASON_SKB_CSUM, SKB_CSUM)			\
	EM(SKB_DROP_REASON_SKB_GSO_SEG, SKB_GSO_SEG)		\
	EM(SKB_DROP_REASON_SKB_UCOPY_FAULT, SKB_UCOPY_FAULT)	\
	EM(SKB_DROP_REASON_DEV_HDR, DEV_HDR)			\
	EM(SKB_DROP_REASON_DEV_READY, DEV_READY)		\
	EM(SKB_DROP_REASON_FULL_RING, FULL_RING)		\
	EM(SKB_DROP_REASON_NOMEM, NOMEM)			\
	EM(SKB_DROP_REASON_HDR_TRUNC, HDR_TRUNC)		\
	EM(SKB_DROP_REASON_TAP_FILTER, TAP_FILTER)		\
	EM(SKB_DROP_REASON_TAP_TXFILTER, TAP_TXFILTER)		\
	EMe(SKB_DROP_REASON_MAX, MAX)

#undef EM
#undef EMe

#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

TRACE_SKB_DROP_REASON

#undef EM
#undef EMe
#define EM(a, b)	{ a, #b },
#define EMe(a, b)	{ a, #b }

/*
 * Tracepoint for free an sk_buff:
 */
TRACE_EVENT(kfree_skb,

	TP_PROTO(struct sk_buff *skb, void *location,
		 enum skb_drop_reason reason),

	TP_ARGS(skb, location, reason),

	TP_STRUCT__entry(
		__field(void *,		skbaddr)
		__field(void *,		location)
		__field(unsigned short,	protocol)
		__field(enum skb_drop_reason,	reason)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
		__entry->location = location;
		__entry->protocol = ntohs(skb->protocol);
		__entry->reason = reason;
	),

	TP_printk("skbaddr=%p protocol=%u location=%p reason: %s",
		  __entry->skbaddr, __entry->protocol, __entry->location,
		  __print_symbolic(__entry->reason,
				   TRACE_SKB_DROP_REASON))
);

TRACE_EVENT(consume_skb,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(	void *,	skbaddr	)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
	),

	TP_printk("skbaddr=%p", __entry->skbaddr)
);

TRACE_EVENT(skb_copy_datagram_iovec,

	TP_PROTO(const struct sk_buff *skb, int len),

	TP_ARGS(skb, len),

	TP_STRUCT__entry(
		__field(	const void *,		skbaddr		)
		__field(	int,			len		)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
		__entry->len = len;
	),

	TP_printk("skbaddr=%p len=%d", __entry->skbaddr, __entry->len)
);

#endif /* _TRACE_SKB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
