/*
 * Copyright (c) 2011 Florian Westphal <fw@strlen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/route.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>

#include <linux/netfilter/xt_rpfilter.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_DESCRIPTION("Xtables: IPv6 reverse path filter match");

static bool rpfilter_addr_unicast(const struct in6_addr *addr)
{
	int addr_type = ipv6_addr_type(addr);
	return addr_type & IPV6_ADDR_UNICAST;
}

static bool rpfilter_lookup_reverse6(struct net *net, const struct sk_buff *skb,
				     const struct net_device *dev, u8 flags)
{
	struct rt6_info *rt;
	struct ipv6hdr *iph = ipv6_hdr(skb);
	bool ret = false;
	struct flowi6 fl6 = {
		.flowi6_iif = LOOPBACK_IFINDEX,
		.flowlabel = (* (__be32 *) iph) & IPV6_FLOWINFO_MASK,
		.flowi6_proto = iph->nexthdr,
		.daddr = iph->saddr,
	};
	int lookup_flags;

	if (rpfilter_addr_unicast(&iph->daddr)) {
		memcpy(&fl6.saddr, &iph->daddr, sizeof(struct in6_addr));
		lookup_flags = RT6_LOOKUP_F_HAS_SADDR;
	} else {
		lookup_flags = 0;
	}

	fl6.flowi6_mark = flags & XT_RPFILTER_VALID_MARK ? skb->mark : 0;
	if ((flags & XT_RPFILTER_LOOSE) == 0) {
		fl6.flowi6_oif = dev->ifindex;
		lookup_flags |= RT6_LOOKUP_F_IFACE;
	}

	rt = (void *) ip6_route_lookup(net, &fl6, lookup_flags);
	if (rt->dst.error)
		goto out;

	if (rt->rt6i_flags & (RTF_REJECT|RTF_ANYCAST))
		goto out;

	if (rt->rt6i_flags & RTF_LOCAL) {
		ret = flags & XT_RPFILTER_ACCEPT_LOCAL;
		goto out;
	}

	if (rt->rt6i_idev->dev == dev || (flags & XT_RPFILTER_LOOSE))
		ret = true;
 out:
	ip6_rt_put(rt);
	return ret;
}

static bool rpfilter_is_local(const struct sk_buff *skb)
{
	const struct rt6_info *rt = (const void *) skb_dst(skb);
	return rt && (rt->rt6i_flags & RTF_LOCAL);
}

static bool rpfilter_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_rpfilter_info *info = par->matchinfo;
	int saddrtype;
	struct ipv6hdr *iph;
	bool invert = info->flags & XT_RPFILTER_INVERT;

	if (rpfilter_is_local(skb))
		return true ^ invert;

	iph = ipv6_hdr(skb);
	saddrtype = ipv6_addr_type(&iph->saddr);
	if (unlikely(saddrtype == IPV6_ADDR_ANY))
		return true ^ invert; /* not routable: forward path will drop it */

	return rpfilter_lookup_reverse6(xt_net(par), skb, xt_in(par),
					info->flags) ^ invert;
}

static int rpfilter_check(const struct xt_mtchk_param *par)
{
	const struct xt_rpfilter_info *info = par->matchinfo;
	unsigned int options = ~XT_RPFILTER_OPTION_MASK;

	if (info->flags & options) {
		pr_info("unknown options encountered");
		return -EINVAL;
	}

	if (strcmp(par->table, "mangle") != 0 &&
	    strcmp(par->table, "raw") != 0) {
		pr_info("match only valid in the \'raw\' "
			"or \'mangle\' tables, not \'%s\'.\n", par->table);
		return -EINVAL;
	}

	return 0;
}

static struct xt_match rpfilter_mt_reg __read_mostly = {
	.name		= "rpfilter",
	.family		= NFPROTO_IPV6,
	.checkentry	= rpfilter_check,
	.match		= rpfilter_mt,
	.matchsize	= sizeof(struct xt_rpfilter_info),
	.hooks		= (1 << NF_INET_PRE_ROUTING),
	.me		= THIS_MODULE
};

static int __init rpfilter_mt_init(void)
{
	return xt_register_match(&rpfilter_mt_reg);
}

static void __exit rpfilter_mt_exit(void)
{
	xt_unregister_match(&rpfilter_mt_reg);
}

module_init(rpfilter_mt_init);
module_exit(rpfilter_mt_exit);
