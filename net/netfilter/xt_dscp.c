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

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_dscp.h>
#include <linux/netfilter_ipv4/ipt_tos.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: DSCP/TOS field match");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_dscp");
MODULE_ALIAS("ip6t_dscp");
MODULE_ALIAS("ipt_tos");
MODULE_ALIAS("ip6t_tos");

static bool
dscp_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_dscp_info *info = par->matchinfo;
	u_int8_t dscp = ipv4_get_dsfield(ip_hdr(skb)) >> XT_DSCP_SHIFT;

	return (dscp == info->dscp) ^ !!info->invert;
}

static bool
dscp_mt6(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_dscp_info *info = par->matchinfo;
	u_int8_t dscp = ipv6_get_dsfield(ipv6_hdr(skb)) >> XT_DSCP_SHIFT;

	return (dscp == info->dscp) ^ !!info->invert;
}

static bool
dscp_mt_check(const char *tablename, const void *info,
              const struct xt_match *match, void *matchinfo,
              unsigned int hook_mask)
{
	const u_int8_t dscp = ((struct xt_dscp_info *)matchinfo)->dscp;

	if (dscp > XT_DSCP_MAX) {
		printk(KERN_ERR "xt_dscp: dscp %x out of range\n", dscp);
		return false;
	}

	return true;
}

static bool
tos_mt_v0(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct ipt_tos_info *info = par->matchinfo;

	return (ip_hdr(skb)->tos == info->tos) ^ info->invert;
}

static bool tos_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_tos_match_info *info = par->matchinfo;

	if (par->match->family == NFPROTO_IPV4)
		return ((ip_hdr(skb)->tos & info->tos_mask) ==
		       info->tos_value) ^ !!info->invert;
	else
		return ((ipv6_get_dsfield(ipv6_hdr(skb)) & info->tos_mask) ==
		       info->tos_value) ^ !!info->invert;
}

static struct xt_match dscp_mt_reg[] __read_mostly = {
	{
		.name		= "dscp",
		.family		= NFPROTO_IPV4,
		.checkentry	= dscp_mt_check,
		.match		= dscp_mt,
		.matchsize	= sizeof(struct xt_dscp_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "dscp",
		.family		= NFPROTO_IPV6,
		.checkentry	= dscp_mt_check,
		.match		= dscp_mt6,
		.matchsize	= sizeof(struct xt_dscp_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "tos",
		.revision	= 0,
		.family		= NFPROTO_IPV4,
		.match		= tos_mt_v0,
		.matchsize	= sizeof(struct ipt_tos_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "tos",
		.revision	= 1,
		.family		= NFPROTO_IPV4,
		.match		= tos_mt,
		.matchsize	= sizeof(struct xt_tos_match_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "tos",
		.revision	= 1,
		.family		= NFPROTO_IPV6,
		.match		= tos_mt,
		.matchsize	= sizeof(struct xt_tos_match_info),
		.me		= THIS_MODULE,
	},
};

static int __init dscp_mt_init(void)
{
	return xt_register_matches(dscp_mt_reg, ARRAY_SIZE(dscp_mt_reg));
}

static void __exit dscp_mt_exit(void)
{
	xt_unregister_matches(dscp_mt_reg, ARRAY_SIZE(dscp_mt_reg));
}

module_init(dscp_mt_init);
module_exit(dscp_mt_exit);
