/*
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * Copyright (c) 2011 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on Rusty Russell's IPv4 REDIRECT target. Development of IPv6
 * NAT funded by Astaro.
 */

#include <linux/if.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/types.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <net/addrconf.h>
#include <net/checksum.h>
#include <net/protocol.h>
#include <net/netfilter/nf_nat.h>

static const struct in6_addr loopback_addr = IN6ADDR_LOOPBACK_INIT;

static unsigned int
redirect_tg6(struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct nf_nat_range *range = par->targinfo;
	struct nf_nat_range newrange;
	struct in6_addr newdst;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (par->hooknum == NF_INET_LOCAL_OUT)
		newdst = loopback_addr;
	else {
		struct inet6_dev *idev;
		struct inet6_ifaddr *ifa;
		bool addr = false;

		rcu_read_lock();
		idev = __in6_dev_get(skb->dev);
		if (idev != NULL) {
			list_for_each_entry(ifa, &idev->addr_list, if_list) {
				newdst = ifa->addr;
				addr = true;
				break;
			}
		}
		rcu_read_unlock();

		if (!addr)
			return NF_DROP;
	}

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.in6	= newdst;
	newrange.max_addr.in6	= newdst;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
}

static int redirect_tg6_checkentry(const struct xt_tgchk_param *par)
{
	const struct nf_nat_range *range = par->targinfo;

	if (range->flags & NF_NAT_RANGE_MAP_IPS)
		return -EINVAL;
	return 0;
}

/* FIXME: Take multiple ranges --RR */
static int redirect_tg4_check(const struct xt_tgchk_param *par)
{
	const struct nf_nat_ipv4_multi_range_compat *mr = par->targinfo;

	if (mr->range[0].flags & NF_NAT_RANGE_MAP_IPS) {
		pr_debug("bad MAP_IPS.\n");
		return -EINVAL;
	}
	if (mr->rangesize != 1) {
		pr_debug("bad rangesize %u.\n", mr->rangesize);
		return -EINVAL;
	}
	return 0;
}

static unsigned int
redirect_tg4(struct sk_buff *skb, const struct xt_action_param *par)
{
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	__be32 newdst;
	const struct nf_nat_ipv4_multi_range_compat *mr = par->targinfo;
	struct nf_nat_range newrange;

	NF_CT_ASSERT(par->hooknum == NF_INET_PRE_ROUTING ||
		     par->hooknum == NF_INET_LOCAL_OUT);

	ct = nf_ct_get(skb, &ctinfo);
	NF_CT_ASSERT(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED));

	/* Local packets: make them go to loopback */
	if (par->hooknum == NF_INET_LOCAL_OUT)
		newdst = htonl(0x7F000001);
	else {
		struct in_device *indev;
		struct in_ifaddr *ifa;

		newdst = 0;

		rcu_read_lock();
		indev = __in_dev_get_rcu(skb->dev);
		if (indev && (ifa = indev->ifa_list))
			newdst = ifa->ifa_local;
		rcu_read_unlock();

		if (!newdst)
			return NF_DROP;
	}

	/* Transfer from original range. */
	memset(&newrange.min_addr, 0, sizeof(newrange.min_addr));
	memset(&newrange.max_addr, 0, sizeof(newrange.max_addr));
	newrange.flags	     = mr->range[0].flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.ip = newdst;
	newrange.max_addr.ip = newdst;
	newrange.min_proto   = mr->range[0].min;
	newrange.max_proto   = mr->range[0].max;

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_DST);
}

static struct xt_target redirect_tg_reg[] __read_mostly = {
	{
		.name       = "REDIRECT",
		.family     = NFPROTO_IPV6,
		.revision   = 0,
		.table      = "nat",
		.checkentry = redirect_tg6_checkentry,
		.target     = redirect_tg6,
		.targetsize = sizeof(struct nf_nat_range),
		.hooks      = (1 << NF_INET_PRE_ROUTING) |
		              (1 << NF_INET_LOCAL_OUT),
		.me         = THIS_MODULE,
	},
	{
		.name       = "REDIRECT",
		.family     = NFPROTO_IPV4,
		.revision   = 0,
		.table      = "nat",
		.target     = redirect_tg4,
		.checkentry = redirect_tg4_check,
		.targetsize = sizeof(struct nf_nat_ipv4_multi_range_compat),
		.hooks      = (1 << NF_INET_PRE_ROUTING) |
		              (1 << NF_INET_LOCAL_OUT),
		.me         = THIS_MODULE,
	},
};

static int __init redirect_tg_init(void)
{
	return xt_register_targets(redirect_tg_reg,
				   ARRAY_SIZE(redirect_tg_reg));
}

static void __exit redirect_tg_exit(void)
{
	xt_unregister_targets(redirect_tg_reg, ARRAY_SIZE(redirect_tg_reg));
}

module_init(redirect_tg_init);
module_exit(redirect_tg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: Connection redirection to localhost");
MODULE_ALIAS("ip6t_REDIRECT");
MODULE_ALIAS("ipt_REDIRECT");
