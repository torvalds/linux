/* x_tables module for setting the IPv4/IPv6 DSCP field, Version 1.8
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 * based on ipt_FTOS.c (C) 2000 by Matthew G. Marsh <mgm@paktronix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * See RFC2474 for a description of the DSCP field within the IP Header.
*/

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/dsfield.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_DSCP.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("x_tables DSCP modification module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_DSCP");
MODULE_ALIAS("ip6t_DSCP");

static unsigned int target(struct sk_buff *skb,
			   const struct net_device *in,
			   const struct net_device *out,
			   unsigned int hooknum,
			   const struct xt_target *target,
			   const void *targinfo)
{
	const struct xt_DSCP_info *dinfo = targinfo;
	u_int8_t dscp = ipv4_get_dsfield(ip_hdr(skb)) >> XT_DSCP_SHIFT;

	if (dscp != dinfo->dscp) {
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return NF_DROP;

		ipv4_change_dsfield(ip_hdr(skb), (__u8)(~XT_DSCP_MASK),
				    dinfo->dscp << XT_DSCP_SHIFT);

	}
	return XT_CONTINUE;
}

static unsigned int target6(struct sk_buff *skb,
			    const struct net_device *in,
			    const struct net_device *out,
			    unsigned int hooknum,
			    const struct xt_target *target,
			    const void *targinfo)
{
	const struct xt_DSCP_info *dinfo = targinfo;
	u_int8_t dscp = ipv6_get_dsfield(ipv6_hdr(skb)) >> XT_DSCP_SHIFT;

	if (dscp != dinfo->dscp) {
		if (!skb_make_writable(skb, sizeof(struct ipv6hdr)))
			return NF_DROP;

		ipv6_change_dsfield(ipv6_hdr(skb), (__u8)(~XT_DSCP_MASK),
				    dinfo->dscp << XT_DSCP_SHIFT);
	}
	return XT_CONTINUE;
}

static bool checkentry(const char *tablename,
		       const void *e_void,
		       const struct xt_target *target,
		       void *targinfo,
		       unsigned int hook_mask)
{
	const u_int8_t dscp = ((struct xt_DSCP_info *)targinfo)->dscp;

	if (dscp > XT_DSCP_MAX) {
		printk(KERN_WARNING "DSCP: dscp %x out of range\n", dscp);
		return false;
	}
	return true;
}

static struct xt_target xt_dscp_target[] __read_mostly = {
	{
		.name		= "DSCP",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.target		= target,
		.targetsize	= sizeof(struct xt_DSCP_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
	{
		.name		= "DSCP",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.target		= target6,
		.targetsize	= sizeof(struct xt_DSCP_info),
		.table		= "mangle",
		.me		= THIS_MODULE,
	},
};

static int __init xt_dscp_target_init(void)
{
	return xt_register_targets(xt_dscp_target, ARRAY_SIZE(xt_dscp_target));
}

static void __exit xt_dscp_target_fini(void)
{
	xt_unregister_targets(xt_dscp_target, ARRAY_SIZE(xt_dscp_target));
}

module_init(xt_dscp_target_init);
module_exit(xt_dscp_target_fini);
