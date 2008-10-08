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
#include <linux/ipv6.h>

#include <linux/netfilter/xt_pkttype.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Ludvig <michal@logix.cz>");
MODULE_DESCRIPTION("Xtables: link layer packet type match");
MODULE_ALIAS("ipt_pkttype");
MODULE_ALIAS("ip6t_pkttype");

static bool
pkttype_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_pkttype_info *info = par->matchinfo;
	u_int8_t type;

	if (skb->pkt_type != PACKET_LOOPBACK)
		type = skb->pkt_type;
	else if (par->match->family == NFPROTO_IPV4 &&
	    ipv4_is_multicast(ip_hdr(skb)->daddr))
		type = PACKET_MULTICAST;
	else if (par->match->family == NFPROTO_IPV6 &&
	    ipv6_hdr(skb)->daddr.s6_addr[0] == 0xFF)
		type = PACKET_MULTICAST;
	else
		type = PACKET_BROADCAST;

	return (type == info->pkttype) ^ info->invert;
}

static struct xt_match pkttype_mt_reg[] __read_mostly = {
	{
		.name		= "pkttype",
		.family		= NFPROTO_IPV4,
		.match		= pkttype_mt,
		.matchsize	= sizeof(struct xt_pkttype_info),
		.me		= THIS_MODULE,
	},
	{
		.name		= "pkttype",
		.family		= NFPROTO_IPV6,
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
