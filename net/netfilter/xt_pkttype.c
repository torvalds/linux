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
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_pkttype_info *info = matchinfo;

	return (skb->pkt_type == info->pkttype) ^ info->invert;
}

static struct xt_match pkttype_match = {
	.name		= "pkttype",
	.match		= match,
	.matchsize	= sizeof(struct xt_pkttype_info),
	.family		= AF_INET,
	.me		= THIS_MODULE,
};

static struct xt_match pkttype6_match = {
	.name		= "pkttype",
	.match		= match,
	.matchsize	= sizeof(struct xt_pkttype_info),
	.family		= AF_INET6,
	.me		= THIS_MODULE,
};

static int __init xt_pkttype_init(void)
{
	int ret;
	ret = xt_register_match(&pkttype_match);
	if (ret)
		return ret;

	ret = xt_register_match(&pkttype6_match);
	if (ret)
		xt_unregister_match(&pkttype_match);

	return ret;
}

static void __exit xt_pkttype_fini(void)
{
	xt_unregister_match(&pkttype_match);
	xt_unregister_match(&pkttype6_match);
}

module_init(xt_pkttype_init);
module_exit(xt_pkttype_fini);
