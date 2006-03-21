/* Hop Limit matching module */

/* (C) 2001-2002 Maciej Soltysiak <solt@dns.toxicfilms.tv>
 * Based on HW's ttl module
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv6/ip6t_hl.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_AUTHOR("Maciej Soltysiak <solt@dns.toxicfilms.tv>");
MODULE_DESCRIPTION("IP tables Hop Limit matching module");
MODULE_LICENSE("GPL");

static int match(const struct sk_buff *skb,
		 const struct net_device *in, const struct net_device *out,
		 const struct xt_match *match, const void *matchinfo,
		 int offset, unsigned int protoff, int *hotdrop)
{
	const struct ip6t_hl_info *info = matchinfo;
	const struct ipv6hdr *ip6h = skb->nh.ipv6h;

	switch (info->mode) {
		case IP6T_HL_EQ:
			return (ip6h->hop_limit == info->hop_limit);
			break;
		case IP6T_HL_NE:
			return (!(ip6h->hop_limit == info->hop_limit));
			break;
		case IP6T_HL_LT:
			return (ip6h->hop_limit < info->hop_limit);
			break;
		case IP6T_HL_GT:
			return (ip6h->hop_limit > info->hop_limit);
			break;
		default:
			printk(KERN_WARNING "ip6t_hl: unknown mode %d\n", 
				info->mode);
			return 0;
	}

	return 0;
}

static struct ip6t_match hl_match = {
	.name		= "hl",
	.match		= match,
	.matchsize	= sizeof(struct ip6t_hl_info),
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ip6t_register_match(&hl_match);
}

static void __exit fini(void)
{
	ip6t_unregister_match(&hl_match);

}

module_init(init);
module_exit(fini);
