/*
 * Copyright (c) 2015 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_dup_netdev.h>

static void nf_do_netdev_egress(struct sk_buff *skb, struct net_device *dev)
{
	if (skb_mac_header_was_set(skb))
		skb_push(skb, skb->mac_len);

	skb->dev = dev;
	skb->tstamp = 0;
	dev_queue_xmit(skb);
}

void nf_fwd_netdev_egress(const struct nft_pktinfo *pkt, int oif)
{
	struct net_device *dev;

	dev = dev_get_by_index_rcu(nft_net(pkt), oif);
	if (!dev) {
		kfree_skb(pkt->skb);
		return;
	}

	nf_do_netdev_egress(pkt->skb, dev);
}
EXPORT_SYMBOL_GPL(nf_fwd_netdev_egress);

void nf_dup_netdev_egress(const struct nft_pktinfo *pkt, int oif)
{
	struct net_device *dev;
	struct sk_buff *skb;

	dev = dev_get_by_index_rcu(nft_net(pkt), oif);
	if (dev == NULL)
		return;

	skb = skb_clone(pkt->skb, GFP_ATOMIC);
	if (skb)
		nf_do_netdev_egress(skb, dev);
}
EXPORT_SYMBOL_GPL(nf_dup_netdev_egress);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
