// SPDX-License-Identifier: GPL-2.0
/*
 * Intel / Lantiq GSWIP V2.0 PMAC tag support
 *
 * Copyright (C) 2017 - 2018 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/dsa.h>

#include "dsa_priv.h"

#define GSWIP_TX_HEADER_LEN		4

/* special tag in TX path header */
/* Byte 0 */
#define GSWIP_TX_SLPID_SHIFT		0	/* source port ID */
#define  GSWIP_TX_SLPID_CPU		2
#define  GSWIP_TX_SLPID_APP1		3
#define  GSWIP_TX_SLPID_APP2		4
#define  GSWIP_TX_SLPID_APP3		5
#define  GSWIP_TX_SLPID_APP4		6
#define  GSWIP_TX_SLPID_APP5		7

/* Byte 1 */
#define GSWIP_TX_CRCGEN_DIS		BIT(7)
#define GSWIP_TX_DPID_SHIFT		0	/* destination group ID */
#define  GSWIP_TX_DPID_ELAN		0
#define  GSWIP_TX_DPID_EWAN		1
#define  GSWIP_TX_DPID_CPU		2
#define  GSWIP_TX_DPID_APP1		3
#define  GSWIP_TX_DPID_APP2		4
#define  GSWIP_TX_DPID_APP3		5
#define  GSWIP_TX_DPID_APP4		6
#define  GSWIP_TX_DPID_APP5		7

/* Byte 2 */
#define GSWIP_TX_PORT_MAP_EN		BIT(7)
#define GSWIP_TX_PORT_MAP_SEL		BIT(6)
#define GSWIP_TX_LRN_DIS		BIT(5)
#define GSWIP_TX_CLASS_EN		BIT(4)
#define GSWIP_TX_CLASS_SHIFT		0
#define GSWIP_TX_CLASS_MASK		GENMASK(3, 0)

/* Byte 3 */
#define GSWIP_TX_DPID_EN		BIT(0)
#define GSWIP_TX_PORT_MAP_SHIFT		1
#define GSWIP_TX_PORT_MAP_MASK		GENMASK(6, 1)

#define GSWIP_RX_HEADER_LEN	8

/* special tag in RX path header */
/* Byte 7 */
#define GSWIP_RX_SPPID_SHIFT		4
#define GSWIP_RX_SPPID_MASK		GENMASK(6, 4)

static struct sk_buff *gswip_tag_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	int err;
	u8 *gswip_tag;

	err = skb_cow_head(skb, GSWIP_TX_HEADER_LEN);
	if (err)
		return NULL;

	skb_push(skb, GSWIP_TX_HEADER_LEN);

	gswip_tag = skb->data;
	gswip_tag[0] = GSWIP_TX_SLPID_CPU;
	gswip_tag[1] = GSWIP_TX_DPID_ELAN;
	gswip_tag[2] = GSWIP_TX_PORT_MAP_EN | GSWIP_TX_PORT_MAP_SEL;
	gswip_tag[3] = BIT(dp->index + GSWIP_TX_PORT_MAP_SHIFT) & GSWIP_TX_PORT_MAP_MASK;
	gswip_tag[3] |= GSWIP_TX_DPID_EN;

	return skb;
}

static struct sk_buff *gswip_tag_rcv(struct sk_buff *skb,
				     struct net_device *dev,
				     struct packet_type *pt)
{
	int port;
	u8 *gswip_tag;

	if (unlikely(!pskb_may_pull(skb, GSWIP_RX_HEADER_LEN)))
		return NULL;

	gswip_tag = skb->data - ETH_HLEN;

	/* Get source port information */
	port = (gswip_tag[7] & GSWIP_RX_SPPID_MASK) >> GSWIP_RX_SPPID_SHIFT;
	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev)
		return NULL;

	/* remove GSWIP tag */
	skb_pull_rcsum(skb, GSWIP_RX_HEADER_LEN);

	return skb;
}

const struct dsa_device_ops gswip_netdev_ops = {
	.xmit = gswip_tag_xmit,
	.rcv = gswip_tag_rcv,
	.overhead = GSWIP_RX_HEADER_LEN,
};
