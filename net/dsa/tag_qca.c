// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include <linux/dsa/tag_qca.h>

#include "dsa_priv.h"

static struct sk_buff *qca_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	__be16 *phdr;
	u16 hdr;

	skb_push(skb, QCA_HDR_LEN);

	dsa_alloc_etype_header(skb, QCA_HDR_LEN);
	phdr = dsa_etype_header_pos_tx(skb);

	/* Set the version field, and set destination port information */
	hdr = FIELD_PREP(QCA_HDR_XMIT_VERSION, QCA_HDR_VERSION);
	hdr |= QCA_HDR_XMIT_FROM_CPU;
	hdr |= FIELD_PREP(QCA_HDR_XMIT_DP_BIT, BIT(dp->index));

	*phdr = htons(hdr);

	return skb;
}

static struct sk_buff *qca_tag_rcv(struct sk_buff *skb, struct net_device *dev)
{
	u8 ver, pk_type;
	__be16 *phdr;
	int port;
	u16 hdr;

	BUILD_BUG_ON(sizeof(struct qca_mgmt_ethhdr) != QCA_HDR_MGMT_HEADER_LEN + QCA_HDR_LEN);

	if (unlikely(!pskb_may_pull(skb, QCA_HDR_LEN)))
		return NULL;

	phdr = dsa_etype_header_pos_rx(skb);
	hdr = ntohs(*phdr);

	/* Make sure the version is correct */
	ver = FIELD_GET(QCA_HDR_RECV_VERSION, hdr);
	if (unlikely(ver != QCA_HDR_VERSION))
		return NULL;

	/* Get pk type */
	pk_type = FIELD_GET(QCA_HDR_RECV_TYPE, hdr);

	/* Ethernet MDIO read/write packet */
	if (pk_type == QCA_HDR_RECV_TYPE_RW_REG_ACK)
		return NULL;

	/* Ethernet MIB counter packet */
	if (pk_type == QCA_HDR_RECV_TYPE_MIB)
		return NULL;

	/* Remove QCA tag and recalculate checksum */
	skb_pull_rcsum(skb, QCA_HDR_LEN);
	dsa_strip_etype_header(skb, QCA_HDR_LEN);

	/* Get source port information */
	port = FIELD_GET(QCA_HDR_RECV_SOURCE_PORT, hdr);

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	return skb;
}

static const struct dsa_device_ops qca_netdev_ops = {
	.name	= "qca",
	.proto	= DSA_TAG_PROTO_QCA,
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
	.needed_headroom = QCA_HDR_LEN,
	.promisc_on_master = true,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_QCA);

module_dsa_tag_driver(qca_netdev_ops);
