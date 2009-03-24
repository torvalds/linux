#ifndef _TRACE_SKB_H_
#define _TRACE_SKB_H_

#include <linux/skbuff.h>
#include <linux/tracepoint.h>

DECLARE_TRACE(kfree_skb,
	TPPROTO(struct sk_buff *skb, void *location),
	TPARGS(skb, location));

#endif
