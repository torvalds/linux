/*
 * Mediatek DSA Tag support
 * Copyright (C) 2017 Landen Chao <landen.chao@mediatek.com>
 *		      Sean Wang <sean.wang@mediatek.com>
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

#define MTK_HDR_LEN		4
#define MTK_HDR_RECV_SOURCE_PORT_MASK	GENMASK(2, 0)
#define MTK_HDR_XMIT_DP_BIT_MASK	GENMASK(5, 0)

static struct sk_buff *mtk_tag_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);
	u8 *mtk_tag;

	if (skb_cow_head(skb, MTK_HDR_LEN) < 0)
		return NULL;

	skb_push(skb, MTK_HDR_LEN);

	memmove(skb->data, skb->data + MTK_HDR_LEN, 2 * ETH_ALEN);

	/* Build the tag after the MAC Source Address */
	mtk_tag = skb->data + 2 * ETH_ALEN;
	mtk_tag[0] = 0;
	mtk_tag[1] = (1 << p->dp->index) & MTK_HDR_XMIT_DP_BIT_MASK;
	mtk_tag[2] = 0;
	mtk_tag[3] = 0;

	return skb;
}

static struct sk_buff *mtk_tag_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt,
				   struct net_device *orig_dev)
{
	struct dsa_switch_tree *dst = dev->dsa_ptr;
	struct dsa_switch *ds;
	int port;
	__be16 *phdr, hdr;

	if (unlikely(!pskb_may_pull(skb, MTK_HDR_LEN)))
		return NULL;

	/* The MTK header is added by the switch between src addr
	 * and ethertype at this point, skb->data points to 2 bytes
	 * after src addr so header should be 2 bytes right before.
	 */
	phdr = (__be16 *)(skb->data - 2);
	hdr = ntohs(*phdr);

	/* Remove MTK tag and recalculate checksum. */
	skb_pull_rcsum(skb, MTK_HDR_LEN);

	memmove(skb->data - ETH_HLEN,
		skb->data - ETH_HLEN - MTK_HDR_LEN,
		2 * ETH_ALEN);

	/* This protocol doesn't support cascading multiple
	 * switches so it's safe to assume the switch is first
	 * in the tree.
	 */
	ds = dst->ds[0];
	if (!ds)
		return NULL;

	/* Get source port information */
	port = (hdr & MTK_HDR_RECV_SOURCE_PORT_MASK);
	if (!ds->ports[port].netdev)
		return NULL;

	skb->dev = ds->ports[port].netdev;

	return skb;
}

const struct dsa_device_ops mtk_netdev_ops = {
	.xmit	= mtk_tag_xmit,
	.rcv	= mtk_tag_rcv,
};
