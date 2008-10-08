/* Kernel module to match MAC address parameters. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>

#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/xt_mac.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("Xtables: MAC address match");
MODULE_ALIAS("ipt_mac");
MODULE_ALIAS("ip6t_mac");

static bool
mac_mt(const struct sk_buff *skb, const struct net_device *in,
       const struct net_device *out, const struct xt_match *match,
       const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
    const struct xt_mac_info *info = matchinfo;

    /* Is mac pointer valid? */
    return skb_mac_header(skb) >= skb->head &&
	   skb_mac_header(skb) + ETH_HLEN <= skb->data
	   /* If so, compare... */
	   && ((!compare_ether_addr(eth_hdr(skb)->h_source, info->srcaddr))
		^ info->invert);
}

static struct xt_match mac_mt_reg[] __read_mostly = {
	{
		.name		= "mac",
		.family		= NFPROTO_IPV4,
		.match		= mac_mt,
		.matchsize	= sizeof(struct xt_mac_info),
		.hooks		= (1 << NF_INET_PRE_ROUTING) |
				  (1 << NF_INET_LOCAL_IN) |
				  (1 << NF_INET_FORWARD),
		.me		= THIS_MODULE,
	},
	{
		.name		= "mac",
		.family		= NFPROTO_IPV6,
		.match		= mac_mt,
		.matchsize	= sizeof(struct xt_mac_info),
		.hooks		= (1 << NF_INET_PRE_ROUTING) |
				  (1 << NF_INET_LOCAL_IN) |
				  (1 << NF_INET_FORWARD),
		.me		= THIS_MODULE,
	},
};

static int __init mac_mt_init(void)
{
	return xt_register_matches(mac_mt_reg, ARRAY_SIZE(mac_mt_reg));
}

static void __exit mac_mt_exit(void)
{
	xt_unregister_matches(mac_mt_reg, ARRAY_SIZE(mac_mt_reg));
}

module_init(mac_mt_init);
module_exit(mac_mt_exit);
