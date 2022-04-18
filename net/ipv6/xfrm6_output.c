// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xfrm6_output.c - Common IPsec encapsulation code for IPv6.
 * Copyright (C) 2002 USAGI/WIDE Project
 * Copyright (c) 2004 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/icmpv6.h>
#include <linux/netfilter_ipv6.h>
#include <net/dst.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/xfrm.h>

int xfrm6_find_1stfragopt(struct xfrm_state *x, struct sk_buff *skb,
			  u8 **prevhdr)
{
	return ip6_find_1stfragopt(skb, prevhdr);
}
EXPORT_SYMBOL(xfrm6_find_1stfragopt);

void xfrm6_local_rxpmtu(struct sk_buff *skb, u32 mtu)
{
	struct flowi6 fl6;
	struct sock *sk = skb->sk;

	fl6.flowi6_oif = sk->sk_bound_dev_if;
	fl6.daddr = ipv6_hdr(skb)->daddr;

	ipv6_local_rxpmtu(sk, &fl6, mtu);
}

void xfrm6_local_error(struct sk_buff *skb, u32 mtu)
{
	struct flowi6 fl6;
	const struct ipv6hdr *hdr;
	struct sock *sk = skb->sk;

	hdr = skb->encapsulation ? inner_ipv6_hdr(skb) : ipv6_hdr(skb);
	fl6.fl6_dport = inet_sk(sk)->inet_dport;
	fl6.daddr = hdr->daddr;

	ipv6_local_error(sk, EMSGSIZE, &fl6, mtu);
}

static int __xfrm6_output_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return xfrm_output(sk, skb);
}

static int xfrm6_noneed_fragment(struct sk_buff *skb)
{
	struct frag_hdr *fh;
	u8 prevhdr = ipv6_hdr(skb)->nexthdr;

	if (prevhdr != NEXTHDR_FRAGMENT)
		return 0;
	fh = (struct frag_hdr *)(skb->data + sizeof(struct ipv6hdr));
	if (fh->nexthdr == NEXTHDR_ESP || fh->nexthdr == NEXTHDR_AUTH)
		return 1;
	return 0;
}

static int __xfrm6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_state *x = dst->xfrm;
	unsigned int mtu;
	bool toobig;

#ifdef CONFIG_NETFILTER
	if (!x) {
		IP6CB(skb)->flags |= IP6SKB_REROUTED;
		return dst_output(net, sk, skb);
	}
#endif

	if (x->props.mode != XFRM_MODE_TUNNEL)
		goto skip_frag;

	if (skb->protocol == htons(ETH_P_IPV6))
		mtu = ip6_skb_dst_mtu(skb);
	else
		mtu = dst_mtu(skb_dst(skb));

	toobig = skb->len > mtu && !skb_is_gso(skb);

	if (toobig && xfrm6_local_dontfrag(skb->sk)) {
		xfrm6_local_rxpmtu(skb, mtu);
		kfree_skb(skb);
		return -EMSGSIZE;
	} else if (toobig && xfrm6_noneed_fragment(skb)) {
		skb->ignore_df = 1;
		goto skip_frag;
	} else if (!skb->ignore_df && toobig && skb->sk) {
		xfrm_local_error(skb, mtu);
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (toobig || dst_allfrag(skb_dst(skb)))
		return ip6_fragment(net, sk, skb,
				    __xfrm6_output_finish);

skip_frag:
	return xfrm_output(sk, skb);
}

int xfrm6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return NF_HOOK_COND(NFPROTO_IPV6, NF_INET_POST_ROUTING,
			    net, sk, skb,  skb->dev, skb_dst(skb)->dev,
			    __xfrm6_output,
			    !(IP6CB(skb)->flags & IP6SKB_REROUTED));
}
