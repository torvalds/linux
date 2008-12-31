/*
 * net/dsa/tag_trailer.c - Trailer tag format handling
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include "dsa_priv.h"

int trailer_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	struct sk_buff *nskb;
	int padlen;
	u8 *trailer;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/*
	 * We have to make sure that the trailer ends up as the very
	 * last 4 bytes of the packet.  This means that we have to pad
	 * the packet to the minimum ethernet frame size, if necessary,
	 * before adding the trailer.
	 */
	padlen = 0;
	if (skb->len < 60)
		padlen = 60 - skb->len;

	nskb = alloc_skb(NET_IP_ALIGN + skb->len + padlen + 4, GFP_ATOMIC);
	if (nskb == NULL) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	skb_reserve(nskb, NET_IP_ALIGN);

	skb_reset_mac_header(nskb);
	skb_set_network_header(nskb, skb_network_header(skb) - skb->head);
	skb_set_transport_header(nskb, skb_transport_header(skb) - skb->head);
	skb_copy_and_csum_dev(skb, skb_put(nskb, skb->len));
	kfree_skb(skb);

	if (padlen) {
		u8 *pad = skb_put(nskb, padlen);
		memset(pad, 0, padlen);
	}

	trailer = skb_put(nskb, 4);
	trailer[0] = 0x80;
	trailer[1] = 1 << p->port;
	trailer[2] = 0x10;
	trailer[3] = 0x00;

	nskb->protocol = htons(ETH_P_TRAILER);

	nskb->dev = p->parent->master_netdev;
	dev_queue_xmit(nskb);

	return NETDEV_TX_OK;
}

static int trailer_rcv(struct sk_buff *skb, struct net_device *dev,
		       struct packet_type *pt, struct net_device *orig_dev)
{
	struct dsa_switch *ds = dev->dsa_ptr;
	u8 *trailer;
	int source_port;

	if (unlikely(ds == NULL))
		goto out_drop;

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (skb == NULL)
		goto out;

	if (skb_linearize(skb))
		goto out_drop;

	trailer = skb_tail_pointer(skb) - 4;
	if (trailer[0] != 0x80 || (trailer[1] & 0xf8) != 0x00 ||
	    (trailer[3] & 0xef) != 0x00 || trailer[3] != 0x00)
		goto out_drop;

	source_port = trailer[1] & 7;
	if (source_port >= DSA_MAX_PORTS || ds->ports[source_port] == NULL)
		goto out_drop;

	pskb_trim_rcsum(skb, skb->len - 4);

	skb->dev = ds->ports[source_port];
	skb_push(skb, ETH_HLEN);
	skb->pkt_type = PACKET_HOST;
	skb->protocol = eth_type_trans(skb, skb->dev);

	skb->dev->stats.rx_packets++;
	skb->dev->stats.rx_bytes += skb->len;

	netif_receive_skb(skb);

	return 0;

out_drop:
	kfree_skb(skb);
out:
	return 0;
}

static struct packet_type trailer_packet_type = {
	.type	= __constant_htons(ETH_P_TRAILER),
	.func	= trailer_rcv,
};

static int __init trailer_init_module(void)
{
	dev_add_pack(&trailer_packet_type);
	return 0;
}
module_init(trailer_init_module);

static void __exit trailer_cleanup_module(void)
{
	dev_remove_pack(&trailer_packet_type);
}
module_exit(trailer_cleanup_module);
