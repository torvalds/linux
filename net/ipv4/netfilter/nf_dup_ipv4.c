// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) 2007 by Sebastian Claßen <sebastian.classen@freenet.ag>
 * (C) 2007-2010 by Jan Engelhardt <jengelh@medozas.de>
 *
 * Extracted from xt_TEE.c
 */
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/route.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/netfilter/ipv4/nf_dup_ipv4.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
#endif

static bool nf_dup_ipv4_route(struct net *net, struct sk_buff *skb,
			      const struct in_addr *gw, int oif)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct rtable *rt;
	struct flowi4 fl4;

	memset(&fl4, 0, sizeof(fl4));
	if (oif != -1)
		fl4.flowi4_oif = oif;

	fl4.daddr = gw->s_addr;
	fl4.flowi4_tos = RT_TOS(iph->tos);
	fl4.flowi4_scope = RT_SCOPE_UNIVERSE;
	fl4.flowi4_flags = FLOWI_FLAG_KNOWN_NH;
	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt))
		return false;

	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);
	skb->dev      = rt->dst.dev;
	skb->protocol = htons(ETH_P_IP);

	return true;
}

void nf_dup_ipv4(struct net *net, struct sk_buff *skb, unsigned int hooknum,
		 const struct in_addr *gw, int oif)
{
	struct iphdr *iph;

	local_bh_disable();
	if (this_cpu_read(nf_skb_duplicated))
		goto out;
	/*
	 * Copy the skb, and route the copy. Will later return %XT_CONTINUE for
	 * the original skb, which should continue on its way as if nothing has
	 * happened. The copy should be independently delivered to the gateway.
	 */
	skb = pskb_copy(skb, GFP_ATOMIC);
	if (skb == NULL)
		goto out;

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	/* Avoid counting cloned packets towards the original connection. */
	nf_reset_ct(skb);
	nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
#endif
	/*
	 * If we are in PREROUTING/INPUT, decrease the TTL to mitigate potential
	 * loops between two hosts.
	 *
	 * Set %IP_DF so that the original source is notified of a potentially
	 * decreased MTU on the clone route. IPv6 does this too.
	 *
	 * IP header checksum will be recalculated at ip_local_out.
	 */
	iph = ip_hdr(skb);
	iph->frag_off |= htons(IP_DF);
	if (hooknum == NF_INET_PRE_ROUTING ||
	    hooknum == NF_INET_LOCAL_IN)
		--iph->ttl;

	if (nf_dup_ipv4_route(net, skb, gw, oif)) {
		__this_cpu_write(nf_skb_duplicated, true);
		ip_local_out(net, skb->sk, skb);
		__this_cpu_write(nf_skb_duplicated, false);
	} else {
		kfree_skb(skb);
	}
out:
	local_bh_enable();
}
EXPORT_SYMBOL_GPL(nf_dup_ipv4);

MODULE_AUTHOR("Sebastian Claßen <sebastian.classen@freenet.ag>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("nf_dup_ipv4: Duplicate IPv4 packet");
MODULE_LICENSE("GPL");
