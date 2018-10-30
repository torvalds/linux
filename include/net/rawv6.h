/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_RAWV6_H
#define _NET_RAWV6_H

#include <net/protocol.h>

extern struct raw_hashinfo raw_v6_hashinfo;
struct sock *__raw_v6_lookup(struct net *net, struct sock *sk,
			     unsigned short num, const struct in6_addr *loc_addr,
			     const struct in6_addr *rmt_addr, int dif, int sdif);

int raw_abort(struct sock *sk, int err);

void raw6_icmp_error(struct sk_buff *, int nexthdr,
		u8 type, u8 code, int inner_offset, __be32);
bool raw6_local_deliver(struct sk_buff *, int);

int rawv6_rcv(struct sock *sk, struct sk_buff *skb);

#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
int rawv6_mh_filter_register(int (*filter)(struct sock *sock,
					   struct sk_buff *skb));
int rawv6_mh_filter_unregister(int (*filter)(struct sock *sock,
					     struct sk_buff *skb));
#endif

#endif
