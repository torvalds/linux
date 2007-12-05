/* Kernel module to match TOS values. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ip.h>
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ipt_tos.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iptables TOS match module");

static bool
tos_mt(const struct sk_buff *skb, const struct net_device *in,
       const struct net_device *out, const struct xt_match *match,
       const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct ipt_tos_info *info = matchinfo;

	return (ip_hdr(skb)->tos == info->tos) ^ info->invert;
}

static struct xt_match tos_mt_reg __read_mostly = {
	.name		= "tos",
	.family		= AF_INET,
	.match		= tos_mt,
	.matchsize	= sizeof(struct ipt_tos_info),
	.me		= THIS_MODULE,
};

static int __init tos_mt_init(void)
{
	return xt_register_match(&tos_mt_reg);
}

static void __exit tos_mt_exit(void)
{
	xt_unregister_match(&tos_mt_reg);
}

module_init(tos_mt_init);
module_exit(tos_mt_exit);
