/* (C) 1999-2001 Michal Ludvig <michal@logix.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <linux/netfilter/xt_pkttype.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig <michal@logix.cz>");
MODULE_DESCRIPTION("IP tables match to match on linklayer packet type");
MODULE_ALIAS("ipt_pkttype");
MODULE_ALIAS("ip6t_pkttype");

static int match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_pkttype_info *info = matchinfo;

	return (skb->pkt_type == info->pkttype) ^ info->invert;
}

static int checkentry(const char *tablename,
		   const void *ip,
		   void *matchinfo,
		   unsigned int matchsize,
		   unsigned int hook_mask)
{
	if (matchsize != XT_ALIGN(sizeof(struct xt_pkttype_info)))
		return 0;

	return 1;
}

static struct xt_match pkttype_match = {
	.name		= "pkttype",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};
static struct xt_match pkttype6_match = {
	.name		= "pkttype",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};


static int __init init(void)
{
	int ret;
	ret = xt_register_match(AF_INET, &pkttype_match);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &pkttype6_match);
	if (ret)
		xt_unregister_match(AF_INET, &pkttype_match);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET, &pkttype_match);
	xt_unregister_match(AF_INET6, &pkttype6_match);
}

module_init(init);
module_exit(fini);
