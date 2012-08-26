/*
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Svenning Soerensen's IPv4 NETMAP target. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
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

	if (par->hooknum == NF_INET_PRE_ROUTING ||
	    par->hooknum == NF_INET_LOCAL_OUT)
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

	return nf_nat_setup_info(ct, &newrange, HOOK2MANIP(par->hooknum));
}

static int netmap_tg6_checkentry(const struct xt_tgchk_param *par)
{
	const struct nf_nat_range *range = par->targinfo;

	if (!(range->flags & NF_NAT_RANGE_MAP_IPS))
		return -EINVAL;
	return 0;
}

static struct xt_target netmap_tg6_reg __read_mostly = {
	.name		= "NETMAP",
	.family		= NFPROTO_IPV6,
	.target		= netmap_tg6,
	.targetsize	= sizeof(struct nf_nat_range),
	.table		= "nat",
	.hooks		= (1 << NF_INET_PRE_ROUTING) |
			  (1 << NF_INET_POST_ROUTING) |
			  (1 << NF_INET_LOCAL_OUT) |
			  (1 << NF_INET_LOCAL_IN),
	.checkentry	= netmap_tg6_checkentry,
	.me		= THIS_MODULE,
};

static int __init netmap_tg6_init(void)
{
	return xt_register_target(&netmap_tg6_reg);
}

static void netmap_tg6_exit(void)
{
	xt_unregister_target(&netmap_tg6_reg);
}

module_init(netmap_tg6_init);
module_exit(netmap_tg6_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: 1:1 NAT mapping of IPv6 subnets");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
