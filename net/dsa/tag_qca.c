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
		return NULL;

	skb_push(skb, QCA_HDR_LEN);

	memmove(skb->data, skb->data + QCA_HDR_LEN, 2 * ETH_ALEN);
	phdr = (u16 *)(skb->data + 2 * ETH_ALEN);

	/* Set the version field, and set destination port information */
	hdr = QCA_HDR_VERSION << QCA_HDR_XMIT_VERSION_S |
		QCA_HDR_XMIT_FROM_CPU |
		BIT(p->dp->index);

	*phdr = htons(hdr);

	return skb;
}

static struct sk_buff *qca_tag_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt,
				   struct net_device *orig_dev)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds;
	u8 ver;
	int port;
	__be16 *phdr, hdr;

	if (unlikely(!pskb_may_pull(skb, QCA_HDR_LEN)))
		return NULL;

	/* The QCA header is added by the switch between src addr and Ethertype
	 * At this point, skb->data points to ethertype so header should be
	 * right before
	 */
	phdr = (__be16 *)(skb->data - 2);
	hdr = ntohs(*phdr);

	/* Make sure the version is correct */
	ver = (hdr & QCA_HDR_RECV_VERSION_MASK) >> QCA_HDR_RECV_VERSION_S;
	if (unlikely(ver != QCA_HDR_VERSION))
		return NULL;

	/* Remove QCA tag and recalculate checksum */
	skb_pull_rcsum(skb, QCA_HDR_LEN);
	memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - QCA_HDR_LEN,
		ETH_HLEN - QCA_HDR_LEN);

	/* This protocol doesn't support cascading multiple switches so it's
	 * safe to assume the switch is first in the tree
	 */
	ds = dst->cpu_dp->ds;
	if (!ds)
		return NULL;

	/* Get source port information */
	port = (hdr & QCA_HDR_RECV_SOURCE_PORT_MASK);
	if (!ds->ports[port].netdev)
		return NULL;

	/* Update skb & forward the frame accordingly */
	skb->dev = ds->ports[port].netdev;

	return skb;
}

const struct dsa_device_ops qca_netdev_ops = {
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
};
