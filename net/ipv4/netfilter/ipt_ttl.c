/* IP tables module for matching the value of the TTL
 *
 * (C) 2000,2001 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ip.h>
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ipt_ttl.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: IPv4 TTL field match");
MODULE_LICENSE("GPL");

static bool
ttl_mt(const struct sk_buff *skb, const struct net_device *in,
       const struct net_device *out, const struct xt_match *match,
       const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct ipt_ttl_info *info = matchinfo;
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
		default:
			printk(KERN_WARNING "ipt_ttl: unknown mode %d\n",
				info->mode);
			return false;
	}

	return false;
}

static struct xt_match ttl_mt_reg __read_mostly = {
	.name		= "ttl",
	.family		= AF_INET,
	.match		= ttl_mt,
	.matchsize	= sizeof(struct ipt_ttl_info),
	.me		= THIS_MODULE,
};

static int __init ttl_mt_init(void)
{
	return xt_register_match(&ttl_mt_reg);
}

static void __exit ttl_mt_exit(void)
{
	xt_unregister_match(&ttl_mt_reg);
}

module_init(ttl_mt_init);
module_exit(ttl_mt_exit);
