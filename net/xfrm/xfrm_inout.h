/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/ipv6.h>
#include <net/dsfield.h>
#include <net/xfrm.h>

#ifndef XFRM_INOUT_H
#define XFRM_INOUT_H 1

static inline void xfrm6_beet_make_header(struct sk_buff *skb)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);

	iph->version = 6;

	memcpy(iph->flow_lbl, XFRM_MODE_SKB_CB(skb)->flow_lbl,
	       sizeof(iph->flow_lbl));
	iph->nexthdr = XFRM_MODE_SKB_CB(skb)->protocol;

	ipv6_change_dsfield(iph, 0, XFRM_MODE_SKB_CB(skb)->tos);
	iph->hop_limit = XFRM_MODE_SKB_CB(skb)->ttl;
}

static inline void xfrm4_beet_make_header(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);

	iph->ihl = 5;
	iph->version = 4;

	iph->protocol = XFRM_MODE_SKB_CB(skb)->protocol;
	iph->tos = XFRM_MODE_SKB_CB(skb)->tos;

	iph->id = XFRM_MODE_SKB_CB(skb)->id;
	iph->frag_off = XFRM_MODE_SKB_CB(skb)->frag_off;
	iph->ttl = XFRM_MODE_SKB_CB(skb)->ttl;
}

#endif
