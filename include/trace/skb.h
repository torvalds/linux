#ifndef _TRACE_SKB_H_
#define _TRACE_SKB_H_

#include <linux/skbuff.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(kfree_skb,
	TP_PROTO(struct sk_buff *skb, void *location),
	TP_ARGS(skb, location));

#endif
