/*
 * Copyright (c) 2011 Florian Westphal <fw@strlen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * based on fib_frontend.c; Author: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/ip_fib.h>
#include <net/route.h>

#include <linux/netfilter/xt_rpfilter.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_DESCRIPTION("iptables: ipv4 reverse path filter match");

/* don't try to find route from mcast/bcast/zeronet */
static __be32 rpfilter_get_saddr(__be32 addr)
{
	if (ipv4_is_multicast(addr) || ipv4_is_lbcast(addr) ||
	    ipv4_is_zeronet(addr))
		return 0;
	return addr;
}

static bool rpfilter_lookup_reverse(struct flowi4 *fl4,
				const struct net_device *dev, u8 flags)
{
	struct fib_result res;
	bool dev_match;
	struct net *net = dev_net(dev);
	int ret __maybe_unused;

	if (fib_lookup(net, fl4, &res))
		return false;

	if (res.type != RTN_UNICAST) {
		if (res.type != RTN_LOCAL || !(flags & XT_RPFILTER_ACCEPT_LOCAL))
			return false;
	}
	dev_match = false;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	for (ret = 0; ret < res.fi->fib_nhs; ret++) {
		struct fib_nh *nh = &res.fi->fib_nh[ret];

		if (nh->nh_dev == dev) {
			dev_match = true;
			break;
		}
	}
#else
	if (FIB_RES_DEV(res) == dev)
		dev_match = true;
#endif
	if (dev_match || flags & XT_RPFILTER_LOOSE)
		return FIB_RES_NH(res).nh_scope <= RT_SCOPE_HOST;
	return dev_match;
}

static bool rpfilter_is_local(const struct sk_buff *skb)
{
	const struct rtable *rt = skb_rtable(skb);
	return rt && (rt->rt_flags & RTCF_LOCAL);
}

static bool rpfilter_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_rpfilter_info *info;
	const struct iphdr *iph;
	struct flowi4 flow;
	bool invert;

	info = par->matchinfo;
	invert = info->flags & XT_RPFILTER_INVERT;

	if (rpfilter_is_local(skb))
		return true ^ invert;

	iph = ip_hdr(skb);
	if (ipv4_is_multicast(iph->daddr)) {
		if (ipv4_is_zeronet(iph->saddr))
			return ipv4_is_local_multicast(iph->daddr) ^ invert;
	}
	flow.flowi4_iif = LOOPBACK_IFINDEX;
	flow.daddr = iph->saddr;
	flow.saddr = rpfilter_get_saddr(iph->daddr);
	flow.flowi4_oif = 0;
	flow.flowi4_mark = info->flags & XT_RPFILTER_VALID_MARK ? skb->mark : 0;
	flow.flowi4_tos = RT_TOS(iph->tos);
	flow.flowi4_scope = RT_SCOPE_UNIVERSE;

	return rpfilter_lookup_reverse(&flow, par->in, info->flags) ^ invert;
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
	.family		= NFPROTO_IPV4,
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
