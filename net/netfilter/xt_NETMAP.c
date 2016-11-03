/*
 * (C) 2000-2001 Svenning Soerensen <svenning@post5.tele.dk>
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat.h>

static unsigned int
netmap_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct nf_nat_range *range = par->targinfo;
	struct nf_nat_range newrange;
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	union nf_inet_addr new_addr, netmask;
	unsigned int i;

	ct = nf_ct_get(skb, &ctinfo);
	for (i = 0; i < ARRAY_SIZE(range->min_addr.ip6); i++)
		netmask.ip6[i] = ~(range->min_addr.ip6[i] ^
				   range->max_addr.ip6[i]);

	if (xt_hooknum(par) == NF_INET_PRE_ROUTING ||
	    xt_hooknum(par) == NF_INET_LOCAL_OUT)
		new_addr.in6 = ipv6_hdr(skb)->daddr;
	else
		new_addr.in6 = ipv6_hdr(skb)->saddr;

	for (i = 0; i < ARRAY_SIZE(new_addr.ip6); i++) {
		new_addr.ip6[i] &= ~netmask.ip6[i];
		new_addr.ip6[i] |= range->min_addr.ip6[i] &
				   netmask.ip6[i];
	}

	newrange.flags	= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr	= new_addr;
	newrange.max_addr	= new_addr;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, HOOK2MANIP(xt_hooknum(par)));
}

static int netmap_tg6_checkentry(const struct xt_tgchk_param *par)
{
	const struct nf_nat_range *range = par->targinfo;

	if (!(range->flags & NF_NAT_RANGE_MAP_IPS))
		return -EINVAL;
	return 0;
}

static unsigned int
netmap_tg4(struct sk_buff *skb, const struct xt_action_param *par)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	__be32 new_ip, netmask;
	const struct nf_nat_ipv4_multi_range_compat *mr = par->targinfo;
	struct nf_nat_range newrange;

	NF_CT_ASSERT(xt_hooknum(par) == NF_INET_PRE_ROUTING ||
		     xt_hooknum(par) == NF_INET_POST_ROUTING ||
		     xt_hooknum(par) == NF_INET_LOCAL_OUT ||
		     xt_hooknum(par) == NF_INET_LOCAL_IN);
	ct = nf_ct_get(skb, &ctinfo);

	netmask = ~(mr->range[0].min_ip ^ mr->range[0].max_ip);

	if (xt_hooknum(par) == NF_INET_PRE_ROUTING ||
	    xt_hooknum(par) == NF_INET_LOCAL_OUT)
		new_ip = ip_hdr(skb)->daddr & ~netmask;
	else
		new_ip = ip_hdr(skb)->saddr & ~netmask;
	new_ip |= mr->range[0].min_ip & netmask;

	memset(&newrange.min_addr, 0, sizeof(newrange.min_addr));
	memset(&newrange.max_addr, 0, sizeof(newrange.max_addr));
	newrange.flags	     = mr->range[0].flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.ip = new_ip;
	newrange.max_addr.ip = new_ip;
	newrange.min_proto   = mr->range[0].min;
	newrange.max_proto   = mr->range[0].max;

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, HOOK2MANIP(xt_hooknum(par)));
}

static int netmap_tg4_check(const struct xt_tgchk_param *par)
{
	const struct nf_nat_ipv4_multi_range_compat *mr = par->targinfo;

	if (!(mr->range[0].flags & NF_NAT_RANGE_MAP_IPS)) {
		pr_debug("bad MAP_IPS.\n");
		return -EINVAL;
	}
	if (mr->rangesize != 1) {
		pr_debug("bad rangesize %u.\n", mr->rangesize);
		return -EINVAL;
	}
	return 0;
}

static struct xt_target netmap_tg_reg[] __read_mostly = {
	{
		.name       = "NETMAP",
		.family     = NFPROTO_IPV6,
		.revision   = 0,
		.target     = netmap_tg6,
		.targetsize = sizeof(struct nf_nat_range),
		.table      = "nat",
		.hooks      = (1 << NF_INET_PRE_ROUTING) |
		              (1 << NF_INET_POST_ROUTING) |
		              (1 << NF_INET_LOCAL_OUT) |
		              (1 << NF_INET_LOCAL_IN),
		.checkentry = netmap_tg6_checkentry,
		.me         = THIS_MODULE,
	},
	{
		.name       = "NETMAP",
		.family     = NFPROTO_IPV4,
		.revision   = 0,
		.target     = netmap_tg4,
		.targetsize = sizeof(struct nf_nat_ipv4_multi_range_compat),
		.table      = "nat",
		.hooks      = (1 << NF_INET_PRE_ROUTING) |
		              (1 << NF_INET_POST_ROUTING) |
		              (1 << NF_INET_LOCAL_OUT) |
		              (1 << NF_INET_LOCAL_IN),
		.checkentry = netmap_tg4_check,
		.me         = THIS_MODULE,
	},
};

static int __init netmap_tg_init(void)
{
	return xt_register_targets(netmap_tg_reg, ARRAY_SIZE(netmap_tg_reg));
}

static void netmap_tg_exit(void)
{
	xt_unregister_targets(netmap_tg_reg, ARRAY_SIZE(netmap_tg_reg));
}

module_init(netmap_tg_init);
module_exit(netmap_tg_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: 1:1 NAT mapping of subnets");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_ALIAS("ip6t_NETMAP");
MODULE_ALIAS("ipt_NETMAP");
