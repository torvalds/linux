/* Kernel module to match NFMARK values. */

/* (C) 1999-2001 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter/xt_mark.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables mark matching module");
MODULE_ALIAS("ipt_mark");
MODULE_ALIAS("ip6t_mark");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_mark_info *info = matchinfo;

	return ((skb->nfmark & info->mask) == info->mark) ^ info->invert;
}

static int
checkentry(const char *tablename,
           const void *entry,
	   const struct xt_match *match,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	const struct xt_mark_info *minfo = matchinfo;

	if (minfo->mark > 0xffffffff || minfo->mask > 0xffffffff) {
		printk(KERN_WARNING "mark: only supports 32bit mark\n");
		return 0;
	}
	return 1;
}

static struct xt_match xt_mark_match[] = {
	{
		.name		= "mark",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_mark_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "mark",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_mark_info),
		.me		= THIS_MODULE,
	},
};

static int __init xt_mark_init(void)
{
	return xt_register_matches(xt_mark_match, ARRAY_SIZE(xt_mark_match));
}

static void __exit xt_mark_fini(void)
{
	xt_unregister_matches(xt_mark_match, ARRAY_SIZE(xt_mark_match));
}

module_init(xt_mark_init);
module_exit(xt_mark_fini);
