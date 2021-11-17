/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _NET_IPV6_GRO_H
#define _NET_IPV6_GRO_H

#include <linux/indirect_call_wrapper.h>

struct list_head;
struct sk_buff;

INDIRECT_CALLABLE_DECLARE(struct sk_buff *ipv6_gro_receive(struct list_head *,
							   struct sk_buff *));
INDIRECT_CALLABLE_DECLARE(int ipv6_gro_complete(struct sk_buff *, int));
INDIRECT_CALLABLE_DECLARE(struct sk_buff *inet_gro_receive(struct list_head *,
							   struct sk_buff *));
INDIRECT_CALLABLE_DECLARE(int inet_gro_complete(struct sk_buff *, int));

#define indirect_call_gro_receive_inet(cb, f2, f1, head, skb)	\
({								\
	unlikely(gro_recursion_inc_test(skb)) ?			\
		NAPI_GRO_CB(skb)->flush |= 1, NULL :		\
		INDIRECT_CALL_INET(cb, f2, f1, head, skb);	\
})

#endif /* _NET_IPV6_GRO_H */
