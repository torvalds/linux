// SPDX-License-Identifier: GPL-2.0-only

#include <linux/skbuff.h>

#include "tls.h"

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
