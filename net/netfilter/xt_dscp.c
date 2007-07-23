/* IP tables module for matching the value of the IPv4/IPv6 DSCP field
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/dsfield.h>

#include <linux/netfilter/xt_dscp.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("x_tables DSCP matching module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_dscp");
MODULE_ALIAS("ip6t_dscp");

static bool match(const struct sk_buff *skb,
		  const struct net_device *in,
		  const struct net_device *out,
		  const struct xt_match *match,
		  const void *matchinfo,
		  int offset,
		  unsigned int protoff,
		  bool *hotdrop)
{
	const struct xt_dscp_info *info = matchinfo;
	u_int8_t dscp = ipv4_get_dsfield(ip_hdr(skb)) >> XT_DSCP_SHIFT;

	return (dscp == info->dscp) ^ !!info->invert;
}

static bool match6(const struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   const struct xt_match *match,
		   const void *matchinfo,
		   int offset,
		   unsigned int protoff,
		   bool *hotdrop)
{
	const struct xt_dscp_info *info = matchinfo;
	u_int8_t dscp = ipv6_get_dsfield(ipv6_hdr(skb)) >> XT_DSCP_SHIFT;

	return (dscp == info->dscp) ^ !!info->invert;
}

static bool checkentry(const char *tablename,
		       const void *info,
		       const struct xt_match *match,
		       void *matchinfo,
		       unsigned int hook_mask)
{
	const u_int8_t dscp = ((struct xt_dscp_info *)matchinfo)->dscp;

	if (dscp > XT_DSCP_MAX) {
		printk(KERN_ERR "xt_dscp: dscp %x out of range\n", dscp);
		return false;
	}

	return true;
}

static struct xt_match xt_dscp_match[] __read_mostly = {
	{
		.name		= "dscp",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_dscp_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "dscp",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.match		= match6,
		.matchsize	= sizeof(struct xt_dscp_info),
		.me		= THIS_MODULE,
	},
};

static int __init xt_dscp_match_init(void)
{
	return xt_register_matches(xt_dscp_match, ARRAY_SIZE(xt_dscp_match));
}

static void __exit xt_dscp_match_fini(void)
{
	xt_unregister_matches(xt_dscp_match, ARRAY_SIZE(xt_dscp_match));
}

module_init(xt_dscp_match_init);
module_exit(xt_dscp_match_fini);
