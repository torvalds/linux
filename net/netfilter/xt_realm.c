/* IP tables module for matching the routing realm
 *
 * $Id: ipt_realm.c,v 1.3 2004/03/05 13:25:40 laforge Exp $
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

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_realm_info *info = matchinfo;
	struct dst_entry *dst = skb->dst;
    
	return (info->id == (dst->tclassid & info->mask)) ^ info->invert;
}

static int check(const char *tablename,
                 const void *ip,
                 void *matchinfo,
                 unsigned int matchsize,
                 unsigned int hook_mask)
{
	if (hook_mask
	    & ~((1 << NF_IP_POST_ROUTING) | (1 << NF_IP_FORWARD) |
	        (1 << NF_IP_LOCAL_OUT) | (1 << NF_IP_LOCAL_IN))) {
		printk("xt_realm: only valid for POST_ROUTING, LOCAL_OUT, "
		       "LOCAL_IN or FORWARD.\n");
		return 0;
	}
	if (matchsize != XT_ALIGN(sizeof(struct xt_realm_info))) {
		printk("xt_realm: invalid matchsize.\n");
		return 0;
	}
	return 1;
}

static struct xt_match realm_match = {
	.name		= "realm",
	.match		= match, 
	.checkentry	= check,
	.me		= THIS_MODULE
};

static int __init init(void)
{
	return xt_register_match(AF_INET, &realm_match);
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET, &realm_match);
}

module_init(init);
module_exit(fini);
