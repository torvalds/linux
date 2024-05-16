// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/bitfield.h>
#include <net/dsa.h>
#include <linux/dsa/tag_qca.h>

#include "tag.h"

#define QCA_NAME "qca"

static struct sk_buff *qca_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
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
	struct qca_tagger_data *tagger_data;
	struct dsa_port *dp = dev->dsa_ptr;
	struct dsa_switch *ds = dp->ds;
	u8 ver, pk_type;
	__be16 *phdr;
	int port;
	u16 hdr;

	BUILD_BUG_ON(sizeof(struct qca_mgmt_ethhdr) != QCA_HDR_MGMT_HEADER_LEN + QCA_HDR_LEN);

	tagger_data = ds->tagger_data;

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

	/* Ethernet mgmt read/write packet */
	if (pk_type == QCA_HDR_RECV_TYPE_RW_REG_ACK) {
		if (likely(tagger_data->rw_reg_ack_handler))
			tagger_data->rw_reg_ack_handler(ds, skb);
		return NULL;
	}

	/* Ethernet MIB counter packet */
	if (pk_type == QCA_HDR_RECV_TYPE_MIB) {
		if (likely(tagger_data->mib_autocast_handler))
			tagger_data->mib_autocast_handler(ds, skb);
		return NULL;
	}

	/* Get source port information */
	port = FIELD_GET(QCA_HDR_RECV_SOURCE_PORT, hdr);

	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev)
		return NULL;

	/* Remove QCA tag and recalculate checksum */
	skb_pull_rcsum(skb, QCA_HDR_LEN);
	dsa_strip_etype_header(skb, QCA_HDR_LEN);

	return skb;
}

static int qca_tag_connect(struct dsa_switch *ds)
{
	struct qca_tagger_data *tagger_data;

	tagger_data = kzalloc(sizeof(*tagger_data), GFP_KERNEL);
	if (!tagger_data)
		return -ENOMEM;

	ds->tagger_data = tagger_data;

	return 0;
}

static void qca_tag_disconnect(struct dsa_switch *ds)
{
	kfree(ds->tagger_data);
	ds->tagger_data = NULL;
}

static const struct dsa_device_ops qca_netdev_ops = {
	.name	= QCA_NAME,
	.proto	= DSA_TAG_PROTO_QCA,
	.connect = qca_tag_connect,
	.disconnect = qca_tag_disconnect,
	.xmit	= qca_tag_xmit,
	.rcv	= qca_tag_rcv,
	.needed_headroom = QCA_HDR_LEN,
	.promisc_on_conduit = true,
};

MODULE_DESCRIPTION("DSA tag driver for Qualcomm Atheros QCA8K switches");
MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_QCA, QCA_NAME);

module_dsa_tag_driver(qca_netdev_ops);
