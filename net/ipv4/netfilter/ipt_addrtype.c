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
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("iptables addrtype match");

static inline int match_type(u_int32_t addr, u_int16_t mask)
{
	return !!(mask & (1 << inet_addr_type(addr)));
}

static int match(const struct sk_buff *skb, const struct net_device *in,
		 const struct net_device *out, const void *matchinfo,
		 int offset, unsigned int protoff, int *hotdrop)
{
	const struct ipt_addrtype_info *info = matchinfo;
	const struct iphdr *iph = skb->nh.iph;
	int ret = 1;

	if (info->source)
		ret &= match_type(iph->saddr, info->source)^info->invert_source;
	if (info->dest)
		ret &= match_type(iph->daddr, info->dest)^info->invert_dest;
	
	return ret;
}

static int checkentry(const char *tablename, const void *ip,
		      void *matchinfo, unsigned int matchsize,
		      unsigned int hook_mask)
{
	if (matchsize != IPT_ALIGN(sizeof(struct ipt_addrtype_info))) {
		printk(KERN_ERR "ipt_addrtype: invalid size (%u != %Zu)\n",
		       matchsize, IPT_ALIGN(sizeof(struct ipt_addrtype_info)));
		return 0;
	}

	return 1;
}

static struct ipt_match addrtype_match = {
	.name		= "addrtype",
	.match		= match,
	.checkentry	= checkentry,
	.me		= THIS_MODULE
};

static int __init init(void)
{
	return ipt_register_match(&addrtype_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&addrtype_match);
}

module_init(init);
module_exit(fini);
