/*
 * IP tables module for matching the value of the TTL
 * (C) 2000,2001 by Harald Welte <laforge@netfilter.org>
 *
 * Hop Limit matching module
 * (C) 2001-2002 Maciej Soltysiak <solt@dns.toxicfilms.tv>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ipt_ttl.h>
#include <linux/netfilter_ipv6/ip6t_hl.h>

MODULE_AUTHOR("Maciej Soltysiak <solt@dns.toxicfilms.tv>");
MODULE_DESCRIPTION("Xtables: Hoplimit/TTL field match");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_ttl");
MODULE_ALIAS("ip6t_hl");

static bool ttl_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ipt_ttl_info *info = par->matchinfo;
	const u8 ttl = ip_hdr(skb)->ttl;

	switch (info->mode) {
	case IPT_TTL_EQ:
		return ttl == info->ttl;
	case IPT_TTL_NE:
		return ttl != info->ttl;
	case IPT_TTL_LT:
		return ttl < info->ttl;
	case IPT_TTL_GT:
		return ttl > info->ttl;
	}

	return false;
}

static bool hl_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ip6t_hl_info *info = par->matchinfo;
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);

	switch (info->mode) {
	case IP6T_HL_EQ:
		return ip6h->hop_limit == info->hop_limit;
	case IP6T_HL_NE:
		return ip6h->hop_limit != info->hop_limit;
	case IP6T_HL_LT:
		return ip6h->hop_limit < info->hop_limit;
	case IP6T_HL_GT:
		return ip6h->hop_limit > info->hop_limit;
	}

	return false;
}

static struct xt_match hl_mt_reg[] __read_mostly = {
	{
		.name       = "ttl",
		.revision   = 0,
		.family     = NFPROTO_IPV4,
		.match      = ttl_mt,
		.matchsize  = sizeof(struct ipt_ttl_info),
		.me         = THIS_MODULE,
	},
	{
		.name       = "hl",
		.revision   = 0,
		.family     = NFPROTO_IPV6,
		.match      = hl_mt6,
		.matchsize  = sizeof(struct ip6t_hl_info),
		.me         = THIS_MODULE,
	},
};

static int __init hl_mt_init(void)
{
	return xt_register_matches(hl_mt_reg, ARRAY_SIZE(hl_mt_reg));
}

static void __exit hl_mt_exit(void)
{
	xt_unregister_matches(hl_mt_reg, ARRAY_SIZE(hl_mt_reg));
}

module_init(hl_mt_init);
module_exit(hl_mt_exit);
