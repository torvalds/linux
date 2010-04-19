/*
 *	"TEE" target extension for Xtables
 *	Copyright © Sebastian Claßen, 2007
 *	Jan Engelhardt, 2007-2010
 *
 *	based on ipt_ROUTE.c from Cédric de Launois
 *	<delaunois@info.ucl.be>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 or later, as published by the Free Software Foundation.
 */
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/route.h>
#include <linux/skbuff.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/route.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_TEE.h>

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#	define WITH_CONNTRACK 1
#	include <net/netfilter/nf_conntrack.h>
#endif
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#	define WITH_IPV6 1
#endif

static const union nf_inet_addr tee_zero_address;

static struct net *pick_net(struct sk_buff *skb)
{
#ifdef CONFIG_NET_NS
	const struct dst_entry *dst;

	if (skb->dev != NULL)
		return dev_net(skb->dev);
	dst = skb_dst(skb);
	if (dst != NULL && dst->dev != NULL)
		return dev_net(dst->dev);
#endif
	return &init_net;
}

static bool tee_tg_route_oif(struct flowi *f, struct net *net,
			     const struct xt_tee_tginfo *info)
{
	const struct net_device *dev;

	if (*info->oif != '\0')
		return true;
	dev = dev_get_by_name(net, info->oif);
	if (dev == NULL)
		return false;
	f->oif = dev->ifindex;
	return true;
}

static bool
tee_tg_route4(struct sk_buff *skb, const struct xt_tee_tginfo *info)
{
	const struct iphdr *iph = ip_hdr(skb);
	struct net *net = pick_net(skb);
	struct rtable *rt;
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));
	if (!tee_tg_route_oif(&fl, net, info))
		return false;
	fl.nl_u.ip4_u.daddr = info->gw.ip;
	fl.nl_u.ip4_u.tos   = RT_TOS(iph->tos);
	fl.nl_u.ip4_u.scope = RT_SCOPE_UNIVERSE;
	if (ip_route_output_key(net, &rt, &fl) != 0)
		return false;

	dst_release(skb_dst(skb));
	skb_dst_set(skb, &rt->u.dst);
	skb->dev      = rt->u.dst.dev;
	skb->protocol = htons(ETH_P_IP);
	return true;
}

static unsigned int
tee_tg4(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;
	struct iphdr *iph;

	/*
	 * Copy the skb, and route the copy. Will later return %XT_CONTINUE for
	 * the original skb, which should continue on its way as if nothing has
	 * happened. The copy should be independently delivered to the TEE
	 * --gateway.
	 */
	skb = pskb_copy(skb, GFP_ATOMIC);
	if (skb == NULL)
		return XT_CONTINUE;

#ifdef WITH_CONNTRACK
	/* Avoid counting cloned packets towards the original connection. */
	nf_conntrack_put(skb->nfct);
	skb->nfct     = &nf_conntrack_untracked.ct_general;
	skb->nfctinfo = IP_CT_NEW;
	nf_conntrack_get(skb->nfct);
#endif
	/*
	 * If we are in PREROUTING/INPUT, the checksum must be recalculated
	 * since the length could have changed as a result of defragmentation.
	 *
	 * We also decrease the TTL to mitigate potential TEE loops
	 * between two hosts.
	 *
	 * Set %IP_DF so that the original source is notified of a potentially
	 * decreased MTU on the clone route. IPv6 does this too.
	 */
	iph = ip_hdr(skb);
	iph->frag_off |= htons(IP_DF);
	if (par->hooknum == NF_INET_PRE_ROUTING ||
	    par->hooknum == NF_INET_LOCAL_IN)
		--iph->ttl;
	ip_send_check(iph);

	/*
	 * Xtables is not reentrant currently, so a choice has to be made:
	 * 1. return absolute verdict for the original and let the cloned
	 *    packet travel through the chains
	 * 2. let the original continue travelling and not pass the clone
	 *    to Xtables.
	 * #2 is chosen. Normally, we would use ip_local_out for the clone.
	 * Because iph->check is already correct and we don't pass it to
	 * Xtables anyway, a shortcut to dst_output [forwards to ip_output] can
	 * be taken. %IPSKB_REROUTED needs to be set so that ip_output does not
	 * invoke POSTROUTING on the cloned packet.
	 */
	IPCB(skb)->flags |= IPSKB_REROUTED;
	if (tee_tg_route4(skb, info))
		ip_output(skb);
	else
		kfree_skb(skb);

	return XT_CONTINUE;
}

#ifdef WITH_IPV6
static bool
tee_tg_route6(struct sk_buff *skb, const struct xt_tee_tginfo *info)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct net *net = pick_net(skb);
	struct dst_entry *dst;
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));
	if (!tee_tg_route_oif(&fl, net, info))
		return false;
	fl.nl_u.ip6_u.daddr = info->gw.in6;
	fl.nl_u.ip6_u.flowlabel = ((iph->flow_lbl[0] & 0xF) << 16) |
				  (iph->flow_lbl[1] << 8) | iph->flow_lbl[2];
	dst = ip6_route_output(net, NULL, &fl);
	if (dst == NULL)
		return false;

	dst_release(skb_dst(skb));
	skb_dst_set(skb, dst);
	skb->dev      = dst->dev;
	skb->protocol = htons(ETH_P_IPV6);
	return true;
}

static unsigned int
tee_tg6(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;

	skb = pskb_copy(skb, GFP_ATOMIC);
	if (skb == NULL)
		return XT_CONTINUE;

#ifdef WITH_CONNTRACK
	nf_conntrack_put(skb->nfct);
	skb->nfct     = &nf_conntrack_untracked.ct_general;
	skb->nfctinfo = IP_CT_NEW;
	nf_conntrack_get(skb->nfct);
#endif
	if (par->hooknum == NF_INET_PRE_ROUTING ||
	    par->hooknum == NF_INET_LOCAL_IN) {
		struct ipv6hdr *iph = ipv6_hdr(skb);
		--iph->hop_limit;
	}
	IP6CB(skb)->flags |= IP6SKB_REROUTED;
	if (tee_tg_route6(skb, info))
		ip6_output(skb);
	else
		kfree_skb(skb);

	return XT_CONTINUE;
}
#endif /* WITH_IPV6 */

static int tee_tg_check(const struct xt_tgchk_param *par)
{
	const struct xt_tee_tginfo *info = par->targinfo;

	if (info->oif[sizeof(info->oif)-1] != '\0')
		return -EINVAL;
	/* 0.0.0.0 and :: not allowed */
	return (memcmp(&info->gw, &tee_zero_address,
	       sizeof(tee_zero_address)) == 0) ? -EINVAL : 0;
}

static struct xt_target tee_tg_reg[] __read_mostly = {
	{
		.name       = "TEE",
		.revision   = 1,
		.family     = NFPROTO_IPV4,
		.target     = tee_tg4,
		.targetsize = sizeof(struct xt_tee_tginfo),
		.checkentry = tee_tg_check,
		.me         = THIS_MODULE,
	},
#ifdef WITH_IPV6
	{
		.name       = "TEE",
		.revision   = 1,
		.family     = NFPROTO_IPV6,
		.target     = tee_tg6,
		.targetsize = sizeof(struct xt_tee_tginfo),
		.checkentry = tee_tg_check,
		.me         = THIS_MODULE,
	},
#endif
};

static int __init tee_tg_init(void)
{
	return xt_register_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
}

static void __exit tee_tg_exit(void)
{
	xt_unregister_targets(tee_tg_reg, ARRAY_SIZE(tee_tg_reg));
}

module_init(tee_tg_init);
module_exit(tee_tg_exit);
MODULE_AUTHOR("Sebastian Claßen <sebastian.classen@freenet.ag>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: Reroute packet copy");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_TEE");
MODULE_ALIAS("ip6t_TEE");
