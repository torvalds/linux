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

#include <linux/netfilter_ipv4/ipt_mac.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("iptables mac matching module");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
    const struct ipt_mac_info *info = matchinfo;

    /* Is mac pointer valid? */
    return (skb->mac.raw >= skb->head
	    && (skb->mac.raw + ETH_HLEN) <= skb->data
	    /* If so, compare... */
	    && ((memcmp(eth_hdr(skb)->h_source, info->srcaddr, ETH_ALEN)
		== 0) ^ info->invert));
}

static int
ipt_mac_checkentry(const char *tablename,
		   const struct ipt_ip *ip,
		   void *matchinfo,
		   unsigned int matchsize,
		   unsigned int hook_mask)
{
	/* FORWARD isn't always valid, but it's nice to be able to do --RR */
	if (hook_mask
	    & ~((1 << NF_IP_PRE_ROUTING) | (1 << NF_IP_LOCAL_IN)
		| (1 << NF_IP_FORWARD))) {
		printk("ipt_mac: only valid for PRE_ROUTING, LOCAL_IN or FORWARD.\n");
		return 0;
	}

	if (matchsize != IPT_ALIGN(sizeof(struct ipt_mac_info)))
		return 0;

	return 1;
}

static struct ipt_match mac_match = {
	.name		= "mac",
	.match		= &match,
	.checkentry	= &ipt_mac_checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&mac_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&mac_match);
}

module_init(init);
module_exit(fini);
