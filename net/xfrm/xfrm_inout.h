/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/ipv6.h>
#include <net/dsfield.h>
#include <net/xfrm.h>

#ifndef XFRM_INOUT_H
#define XFRM_INOUT_H 1

static inline void xfrm4_extract_header(struct sk_buff *skb)
{
	const struct iphdr *iph = ip_hdr(skb);

	XFRM_MODE_SKB_CB(skb)->ihl = sizeof(*iph);
	XFRM_MODE_SKB_CB(skb)->id = iph->id;
	XFRM_MODE_SKB_CB(skb)->frag_off = iph->frag_off;
	XFRM_MODE_SKB_CB(skb)->tos = iph->tos;
	XFRM_MODE_SKB_CB(skb)->ttl = iph->ttl;
	XFRM_MODE_SKB_CB(skb)->optlen = iph->ihl * 4 - sizeof(*iph);
	memset(XFRM_MODE_SKB_CB(skb)->flow_lbl, 0,
	       sizeof(XFRM_MODE_SKB_CB(skb)->flow_lbl));
}

static inline void xfrm6_extract_header(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *iph = ipv6_hdr(skb);

	XFRM_MODE_SKB_CB(skb)->ihl = sizeof(*iph);
	XFRM_MODE_SKB_CB(skb)->id = 0;
	XFRM_MODE_SKB_CB(skb)->frag_off = htons(IP_DF);
	XFRM_MODE_SKB_CB(skb)->tos = ipv6_get_dsfield(iph);
	XFRM_MODE_SKB_CB(skb)->ttl = iph->hop_limit;
	XFRM_MODE_SKB_CB(skb)->optlen = 0;
	memcpy(XFRM_MODE_SKB_CB(skb)->flow_lbl, iph->flow_lbl,
	       sizeof(XFRM_MODE_SKB_CB(skb)->flow_lbl));
#else
	WARN_ON_ONCE(1);
#endif
}

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
