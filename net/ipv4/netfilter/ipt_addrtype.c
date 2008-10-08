/*
 *  iptables module to match inet_addr_type() of an ip.
 *
 *  Copyright (c) 2004 Patrick McHardy <kaber@trash.net>
 *  (C) 2007 Laszlo Attila Toth <panther@balabit.hu>
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
MODULE_DESCRIPTION("Xtables: address type match for IPv4");

static inline bool match_type(const struct net_device *dev, __be32 addr,
			      u_int16_t mask)
{
	return !!(mask & (1 << inet_dev_addr_type(&init_net, dev, addr)));
}

static bool
addrtype_mt_v0(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct ipt_addrtype_info *info = par->matchinfo;
	const struct iphdr *iph = ip_hdr(skb);
	bool ret = true;

	if (info->source)
		ret &= match_type(NULL, iph->saddr, info->source) ^
		       info->invert_source;
	if (info->dest)
		ret &= match_type(NULL, iph->daddr, info->dest) ^
		       info->invert_dest;

	return ret;
}

static bool
addrtype_mt_v1(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct ipt_addrtype_info_v1 *info = par->matchinfo;
	const struct iphdr *iph = ip_hdr(skb);
	const struct net_device *dev = NULL;
	bool ret = true;

	if (info->flags & IPT_ADDRTYPE_LIMIT_IFACE_IN)
		dev = par->in;
	else if (info->flags & IPT_ADDRTYPE_LIMIT_IFACE_OUT)
		dev = par->out;

	if (info->source)
		ret &= match_type(dev, iph->saddr, info->source) ^
		       (info->flags & IPT_ADDRTYPE_INVERT_SOURCE);
	if (ret && info->dest)
		ret &= match_type(dev, iph->daddr, info->dest) ^
		       !!(info->flags & IPT_ADDRTYPE_INVERT_DEST);
	return ret;
}

static bool
addrtype_mt_checkentry_v1(const char *tablename, const void *ip_void,
			  const struct xt_match *match, void *matchinfo,
			  unsigned int hook_mask)
{
	struct ipt_addrtype_info_v1 *info = matchinfo;

	if (info->flags & IPT_ADDRTYPE_LIMIT_IFACE_IN &&
	    info->flags & IPT_ADDRTYPE_LIMIT_IFACE_OUT) {
		printk(KERN_ERR "ipt_addrtype: both incoming and outgoing "
				"interface limitation cannot be selected\n");
		return false;
	}

	if (hook_mask & (1 << NF_INET_PRE_ROUTING | 1 << NF_INET_LOCAL_IN) &&
	    info->flags & IPT_ADDRTYPE_LIMIT_IFACE_OUT) {
		printk(KERN_ERR "ipt_addrtype: output interface limitation "
				"not valid in PRE_ROUTING and INPUT\n");
		return false;
	}

	if (hook_mask & (1 << NF_INET_POST_ROUTING | 1 << NF_INET_LOCAL_OUT) &&
	    info->flags & IPT_ADDRTYPE_LIMIT_IFACE_IN) {
		printk(KERN_ERR "ipt_addrtype: input interface limitation "
				"not valid in POST_ROUTING and OUTPUT\n");
		return false;
	}

	return true;
}

static struct xt_match addrtype_mt_reg[] __read_mostly = {
	{
		.name		= "addrtype",
		.family		= NFPROTO_IPV4,
		.match		= addrtype_mt_v0,
		.matchsize	= sizeof(struct ipt_addrtype_info),
		.me		= THIS_MODULE
	},
	{
		.name		= "addrtype",
		.family		= NFPROTO_IPV4,
		.revision	= 1,
		.match		= addrtype_mt_v1,
		.checkentry	= addrtype_mt_checkentry_v1,
		.matchsize	= sizeof(struct ipt_addrtype_info_v1),
		.me		= THIS_MODULE
	}
};

static int __init addrtype_mt_init(void)
{
	return xt_register_matches(addrtype_mt_reg,
				   ARRAY_SIZE(addrtype_mt_reg));
}

static void __exit addrtype_mt_exit(void)
{
	xt_unregister_matches(addrtype_mt_reg, ARRAY_SIZE(addrtype_mt_reg));
}

module_init(addrtype_mt_init);
module_exit(addrtype_mt_exit);
