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
