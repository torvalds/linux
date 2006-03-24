/* IP tables module for matching the value of the IPv4 DSCP field
 *
 * ipt_dscp.c,v 1.3 2002/08/05 19:00:21 laforge Exp
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ipt_dscp.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables DSCP matching module");
MODULE_LICENSE("GPL");

static int match(const struct sk_buff *skb,
		 const struct net_device *in, const struct net_device *out,
		 const struct xt_match *match, const void *matchinfo,
		 int offset, unsigned int protoff, int *hotdrop)
{
	const struct ipt_dscp_info *info = matchinfo;
	const struct iphdr *iph = skb->nh.iph;

	u_int8_t sh_dscp = ((info->dscp << IPT_DSCP_SHIFT) & IPT_DSCP_MASK);

	return ((iph->tos&IPT_DSCP_MASK) == sh_dscp) ^ info->invert;
}

static struct ipt_match dscp_match = {
	.name		= "dscp",
	.match		= match,
	.matchsize	= sizeof(struct ipt_dscp_info),
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&dscp_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&dscp_match);

}

module_init(init);
module_exit(fini);
