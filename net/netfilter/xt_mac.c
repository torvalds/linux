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
#include <linux/if_arp.h>
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

static bool mac_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_mac_info *info = par->matchinfo;
	bool ret;

	if (skb->dev == NULL || skb->dev->type != ARPHRD_ETHER)
		return false;
	if (skb_mac_header(skb) < skb->head)
		return false;
	if (skb_mac_header(skb) + ETH_HLEN > skb->data)
		return false;
	ret  = ether_addr_equal(eth_hdr(skb)->h_source, info->srcaddr);
	ret ^= info->invert;
	return ret;
}

static struct xt_match mac_mt_reg __read_mostly = {
	.name      = "mac",
	.revision  = 0,
	.family    = NFPROTO_UNSPEC,
	.match     = mac_mt,
	.matchsize = sizeof(struct xt_mac_info),
	.hooks     = (1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_IN) |
	             (1 << NF_INET_FORWARD),
	.me        = THIS_MODULE,
};

static int __init mac_mt_init(void)
{
	return xt_register_match(&mac_mt_reg);
}

static void __exit mac_mt_exit(void)
{
	xt_unregister_match(&mac_mt_reg);
}

module_init(mac_mt_init);
module_exit(mac_mt_exit);
