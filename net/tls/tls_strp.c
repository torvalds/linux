// SPDX-License-Identifier: GPL-2.0-only

#include <linux/skbuff.h>

#include "tls.h"

struct sk_buff *tls_strp_msg_detach(struct tls_sw_context_rx *ctx)
{
	struct sk_buff *skb;

	skb = ctx->recv_pkt;
	ctx->recv_pkt = NULL;
	return skb;
}

int tls_strp_msg_hold(struct sock *sk, struct sk_buff *skb,
		      struct sk_buff_head *dst)
{
	struct sk_buff *clone;

	clone = skb_clone(skb, sk->sk_allocation);
	if (!clone)
		return -ENOMEM;
	__skb_queue_tail(dst, clone);
	return 0;
}
