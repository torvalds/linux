/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/etherdevice.h>
#include "dsa_priv.h"

#define QCA_HDR_LEN	2
#define QCA_HDR_VERSION	0x2

#define QCA_HDR_RECV_VERSION_MASK	GENMASK(15, 14)
#define QCA_HDR_RECV_VERSION_S		14
#define QCA_HDR_RECV_PRIORITY_MASK	GENMASK(13, 11)
#define QCA_HDR_RECV_PRIORITY_S		11
#define QCA_HDR_RECV_TYPE_MASK		GENMASK(10, 6)
#define QCA_HDR_RECV_TYPE_S		6
#define QCA_HDR_RECV_FRAME_IS_TAGGED	BIT(3)
#define QCA_HDR_RECV_SOURCE_PORT_MASK	GENMASK(2, 0)

#define QCA_HDR_XMIT_VERSION_MASK	GENMASK(15, 14)
#define QCA_HDR_XMIT_VERSION_S		14
#define QCA_HDR_XMIT_PRIORITY_MASK	GENMASK(13, 11)
#define QCA_HDR_XMIT_PRIORITY_S		11
#define QCA_HDR_XMIT_CONTROL_MASK	GENMASK(10, 8)
#define QCA_HDR_XMIT_CONTROL_S		8
#define QCA_HDR_XMIT_FROM_CPU		BIT(7)
#define QCA_HDR_XMIT_DP_BIT_MASK	GENMASK(6, 0)

static struct sk_buff *qca_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	u16 *phdr, hdr;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	if (skb_cow_head(skb, 0) < 0)
		goto out_free;

	skb_push(skb, QCA_HDR_LEN);

	memmove(skb->data, skb->data + QCA_HDR_LEN, 2 * ETH_ALEN);
	phdr = (u16 *)(skb->data + 2 * ETH_ALEN);

	/* Set the version field, and set destination port information */
	hdr = QCA_HDR_VERSION << QCA_HDR_XMIT_VERSION_S |
		QCA_HDR_XMIT_FROM_CPU |
		BIT(p->port);

	*phdr = htons(hdr);

	return skb;

out_free:
	kfree_skb(skb);
	return NULL;
}

static int qca_tag_rcv(struct sk_buff *skb, struct net_device *dev,
		       struct packet_type *pt, struct net_device *orig_dev)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds;
	u8 ver;
	int port;
	__be16 *phdr, hdr;

	if (unlikely(!dst))
		goto out_drop;

	skb = skb_unshare(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	if (unlikely(!pskb_may_pull(skb, QCA_HDR_LEN)))
		goto out_drop;

	/* The QCA header is added by the switch between src addr and Ethertype
	 * At this point, skb->data points to ethertype so header should be
	 * right before
	 */
	phdr = (__be16 *)(skb->data - 2);
	hdr = ntohs(*phdr);

	/* Make sure the version is correct */
	ver = (hdr & QCA_HDR_RECV_VERSION_MASK) >> QCA_HDR_RECV_VERSION_S;
	if (unlikely(ver != QCA_HDR_VERSION))
		goto out_drop;

	/* Remove QCA tag and recalculate checksum */
	skb_pull_rcsum(skb, QCA_HDR_LEN);
	memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - QCA_HDR_LEN,
		ETH_HLEN - QCA_HDR_LEN);

	/* This protocol doesn't support cascading multiple switches so it's
	 * safe to assume the switch is first in the tree
	 */
	ds = dst->ds[0];
	if (!ds)
		goto out_drop;

	/* Get source port information */
	port = (hdr & QCA_HDR_RECV_SOURCE_PORT_MASK);
	if (!ds->ports[port].netdev)
		goto out_drop;

	/* Update skb & forward the frame accordingly */
	skb_push(skb, ETH_HLEN);
	skb->pkt_type = PACKET_HOST;
	skb->dev = ds->ports[port].netdev;
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

const struct dsa_device_ops qca_netdev_ops = {
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
};
