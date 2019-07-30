// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) 2007 by Sebastian Claßen <sebastian.classen@freenet.ag>
 * (C) 2007-2010 by Jan Engelhardt <jengelh@medozas.de>
 *
 * Extracted from xt_TEE.c
 */
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/netfilter/ipv6/nf_dup_ipv6.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#endif

static bool nf_dup_ipv6_route(struct net *net, struct sk_buff *skb,
			      const struct in6_addr *gw, int oif)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct dst_entry *dst;
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	if (oif != -1)
		fl6.flowi6_oif = oif;

	fl6.daddr = *gw;
	fl6.flowlabel = (__force __be32)(((iph->flow_lbl[0] & 0xF) << 16) |
			(iph->flow_lbl[1] << 8) | iph->flow_lbl[2]);
	fl6.flowi6_flags = FLOWI_FLAG_KNOWN_NH;
	dst = ip6_route_output(net, NULL, &fl6);
	if (dst->error) {
		dst_release(dst);
		return false;
	}
	skb_dst_drop(skb);
	skb_dst_set(skb, dst);
	skb->dev      = dst->dev;
	skb->protocol = htons(ETH_P_IPV6);

	return true;
}

void nf_dup_ipv6(struct net *net, struct sk_buff *skb, unsigned int hooknum,
		 const struct in6_addr *gw, int oif)
{
	if (this_cpu_read(nf_skb_duplicated))
		return;
	skb = pskb_copy(skb, GFP_ATOMIC);
	if (skb == NULL)
		return;

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	nf_reset(skb);
	nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
#endif
	if (hooknum == NF_INET_PRE_ROUTING ||
	    hooknum == NF_INET_LOCAL_IN) {
		struct ipv6hdr *iph = ipv6_hdr(skb);
		--iph->hop_limit;
	}
	if (nf_dup_ipv6_route(net, skb, gw, oif)) {
		__this_cpu_write(nf_skb_duplicated, true);
		ip6_local_out(net, skb->sk, skb);
		__this_cpu_write(nf_skb_duplicated, false);
	} else {
		kfree_skb(skb);
	}
}
EXPORT_SYMBOL_GPL(nf_dup_ipv6);

MODULE_AUTHOR("Sebastian Claßen <sebastian.classen@freenet.ag>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("nf_dup_ipv6: IPv6 packet duplication");
MODULE_LICENSE("GPL");
