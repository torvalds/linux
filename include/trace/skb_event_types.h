
/* use <trace/skb.h> instead */
#ifndef TRACE_EVENT
# error Do not include this file directly.
# error Unless you know what you are doing.
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM skb

/*
 * Tracepoint for free an sk_buff:
 */
TRACE_EVENT(kfree_skb,

	TP_PROTO(struct sk_buff *skb, void *location),

	TP_ARGS(skb, location),

	TP_STRUCT__entry(
		__field(	void *,		skbaddr		)
		__field(	unsigned short,	protocol	)
		__field(	void *,		location	)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
		if (skb) {
			__entry->protocol = ntohs(skb->protocol);
		}
		__entry->location = location;
	),

	TP_printk("skbaddr=%p protocol=%u location=%p",
		__entry->skbaddr, __entry->protocol, __entry->location)
);

#undef TRACE_SYSTEM
