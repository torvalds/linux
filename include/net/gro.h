/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _NET_IPV6_GRO_H
#define _NET_IPV6_GRO_H

INDIRECT_CALLABLE_DECLARE(struct sk_buff *ipv6_gro_receive(struct list_head *,
							   struct sk_buff *));
INDIRECT_CALLABLE_DECLARE(int ipv6_gro_complete(struct sk_buff *, int));
INDIRECT_CALLABLE_DECLARE(struct sk_buff *inet_gro_receive(struct list_head *,
							   struct sk_buff *));
INDIRECT_CALLABLE_DECLARE(int inet_gro_complete(struct sk_buff *, int));
#endif /* _NET_IPV6_GRO_H */
