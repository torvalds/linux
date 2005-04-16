/* Kernel module to match NFMARK values. */

/* (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv6/ip6t_mark.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("ip6tables mark match");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct ip6t_mark_info *info = matchinfo;

	return ((skb->nfmark & info->mask) == info->mark) ^ info->invert;
}

static int
checkentry(const char *tablename,
           const struct ip6t_ip6 *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	if (matchsize != IP6T_ALIGN(sizeof(struct ip6t_mark_info)))
		return 0;

	return 1;
}

static struct ip6t_match mark_match = {
	.name		= "mark",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_match(&mark_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&mark_match);
}

module_init(init);
module_exit(fini);
