#ifndef _TRACE_SKB_H_
#define _TRACE_SKB_H_

DECLARE_TRACE(kfree_skb,
	TPPROTO(struct sk_buff *skb, void *location),
	TPARGS(skb, location));

#endif
