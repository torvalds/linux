/* IP tables module for matching the routing realm
 *
 * (C) 2003 by Sampsa Ranta <sampsa@netsonic.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/route.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/xt_realm.h>
#include <linux/netfilter/x_tables.h>

MODULE_AUTHOR("Sampsa Ranta <sampsa@netsonic.fi>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: Routing realm match");
MODULE_ALIAS("ipt_realm");

static bool
realm_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_realm_info *info = par->matchinfo;
	const struct dst_entry *dst = skb_dst(skb);

	return (info->id == (dst->tclassid & info->mask)) ^ info->invert;
}

static struct xt_match realm_mt_reg __read_mostly = {
	.name		= "realm",
	.match		= realm_mt,
	.matchsize	= sizeof(struct xt_realm_info),
	.hooks		= (1 << NF_INET_POST_ROUTING) | (1 << NF_INET_FORWARD) |
			  (1 << NF_INET_LOCAL_OUT) | (1 << NF_INET_LOCAL_IN),
	.family		= NFPROTO_UNSPEC,
	.me		= THIS_MODULE
};

static int __init realm_mt_init(void)
{
	return xt_register_match(&realm_mt_reg);
}

static void __exit realm_mt_exit(void)
{
	xt_unregister_match(&realm_mt_reg);
}

module_init(realm_mt_init);
module_exit(realm_mt_exit);
