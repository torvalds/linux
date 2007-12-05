/*
 *  iptables module to match inet_addr_type() of an ip.
 *
 *  Copyright (c) 2004 Patrick McHardy <kaber@trash.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <net/route.h>

#include <linux/netfilter_ipv4/ipt_addrtype.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("iptables addrtype match");

static inline bool match_type(__be32 addr, u_int16_t mask)
{
	return !!(mask & (1 << inet_addr_type(addr)));
}

static bool
addrtype_mt(const struct sk_buff *skb, const struct net_device *in,
            const struct net_device *out, const struct xt_match *match,
            const void *matchinfo, int offset, unsigned int protoff,
            bool *hotdrop)
{
	const struct ipt_addrtype_info *info = matchinfo;
	const struct iphdr *iph = ip_hdr(skb);
	bool ret = true;

	if (info->source)
		ret &= match_type(iph->saddr, info->source)^info->invert_source;
	if (info->dest)
		ret &= match_type(iph->daddr, info->dest)^info->invert_dest;

	return ret;
}

static struct xt_match addrtype_mt_reg __read_mostly = {
	.name		= "addrtype",
	.family		= AF_INET,
	.match		= addrtype_mt,
	.matchsize	= sizeof(struct ipt_addrtype_info),
	.me		= THIS_MODULE
};

static int __init addrtype_mt_init(void)
{
	return xt_register_match(&addrtype_mt_reg);
}

static void __exit addrtype_mt_exit(void)
{
	xt_unregister_match(&addrtype_mt_reg);
}

module_init(addrtype_mt_init);
module_exit(addrtype_mt_exit);
