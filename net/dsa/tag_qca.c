// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
	struct dsa_port *dp = dsa_slave_to_port(dev);
	u16 *phdr, hdr;

	if (skb_cow_head(skb, 0) < 0)
		return NULL;

	skb_push(skb, QCA_HDR_LEN);

	memmove(skb->data, skb->data + QCA_HDR_LEN, 2 * ETH_ALEN);
	phdr = (u16 *)(skb->data + 2 * ETH_ALEN);

	/* Set the version field, and set destination port information */
	hdr = QCA_HDR_VERSION << QCA_HDR_XMIT_VERSION_S |
		QCA_HDR_XMIT_FROM_CPU | BIT(dp->index);

	*phdr = htons(hdr);

	return skb;
}

static struct sk_buff *qca_tag_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt)
{
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

	/* Get source port information */
	port = (hdr & QCA_HDR_RECV_SOURCE_PORT_MASK);

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	return skb;
}

static int qca_tag_flow_dissect(const struct sk_buff *skb, __be16 *proto,
                                int *offset)
{
	*offset = QCA_HDR_LEN;
	*proto = ((__be16 *)skb->data)[0];

	return 0;
}

static const struct dsa_device_ops qca_netdev_ops = {
	.name	= "qca",
	.proto	= DSA_TAG_PROTO_QCA,
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
	.flow_dissect = qca_tag_flow_dissect,
	.overhead = QCA_HDR_LEN,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_QCA);

module_dsa_tag_driver(qca_netdev_ops);
