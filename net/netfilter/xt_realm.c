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
MODULE_DESCRIPTION("X_tables realm match");
MODULE_ALIAS("ipt_realm");

static bool
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
{
	const struct xt_realm_info *info = matchinfo;
	const struct dst_entry *dst = skb->dst;

	return (info->id == (dst->tclassid & info->mask)) ^ info->invert;
}

static struct xt_match realm_match __read_mostly = {
	.name		= "realm",
	.match		= match,
	.matchsize	= sizeof(struct xt_realm_info),
	.hooks		= (1 << NF_IP_POST_ROUTING) | (1 << NF_IP_FORWARD) |
			  (1 << NF_IP_LOCAL_OUT) | (1 << NF_IP_LOCAL_IN),
	.family		= AF_INET,
	.me		= THIS_MODULE
};

static int __init xt_realm_init(void)
{
	return xt_register_match(&realm_match);
}

static void __exit xt_realm_fini(void)
{
	xt_unregister_match(&realm_match);
}

module_init(xt_realm_init);
module_exit(xt_realm_fini);
