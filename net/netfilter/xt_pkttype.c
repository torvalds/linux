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
#include <linux/in.h>
#include <linux/ip.h>

#include <linux/netfilter/xt_pkttype.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig <michal@logix.cz>");
MODULE_DESCRIPTION("IP tables match to match on linklayer packet type");
MODULE_ALIAS("ipt_pkttype");
MODULE_ALIAS("ip6t_pkttype");

static bool
pkttype_mt(const struct sk_buff *skb, const struct net_device *in,
           const struct net_device *out, const struct xt_match *match,
           const void *matchinfo, int offset, unsigned int protoff,
           bool *hotdrop)
{
	u_int8_t type;
	const struct xt_pkttype_info *info = matchinfo;

	if (skb->pkt_type == PACKET_LOOPBACK)
		type = MULTICAST(ip_hdr(skb)->daddr)
			? PACKET_MULTICAST
			: PACKET_BROADCAST;
	else
		type = skb->pkt_type;

	return (type == info->pkttype) ^ info->invert;
}

static struct xt_match pkttype_mt_reg[] __read_mostly = {
	{
		.name		= "pkttype",
		.family		= AF_INET,
		.match		= pkttype_mt,
		.matchsize	= sizeof(struct xt_pkttype_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "pkttype",
		.family		= AF_INET6,
		.match		= pkttype_mt,
		.matchsize	= sizeof(struct xt_pkttype_info),
		.me		= THIS_MODULE,
	},
};

static int __init pkttype_mt_init(void)
{
	return xt_register_matches(pkttype_mt_reg, ARRAY_SIZE(pkttype_mt_reg));
}

static void __exit pkttype_mt_exit(void)
{
	xt_unregister_matches(pkttype_mt_reg, ARRAY_SIZE(pkttype_mt_reg));
}

module_init(pkttype_mt_init);
module_exit(pkttype_mt_exit);
