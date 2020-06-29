// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xfrm4_output.c - Common IPsec encapsulation code for IPv4.
 * Copyright (c) 2004 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/icmp.h>

static int __xfrm4_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
#ifdef CONFIG_NETFILTER
	struct xfrm_state *x = skb_dst(skb)->xfrm;

	if (!x) {
		IPCB(skb)->flags |= IPSKB_REROUTED;
		return dst_output(net, sk, skb);
	}
#endif

	return xfrm_output(sk, skb);
}

int xfrm4_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING,
			    net, sk, skb, skb->dev, skb_dst(skb)->dev,
			    __xfrm4_output,
			    !(IPCB(skb)->flags & IPSKB_REROUTED));
}

void xfrm4_local_error(struct sk_buff *skb, u32 mtu)
{
	struct iphdr *hdr;

	hdr = skb->encapsulation ? inner_ip_hdr(skb) : ip_hdr(skb);
	ip_local_error(skb->sk, EMSGSIZE, hdr->daddr,
		       inet_sk(skb->sk)->inet_dport, mtu);
}
