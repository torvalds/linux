// SPDX-License-Identifier: GPL-2.0+
/*
 * net/dsa/tag_trailer.c - Trailer tag format handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "tag.h"

#define TRAILER_NAME "trailer"

static struct sk_buff *trailer_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	u8 *trailer;

	trailer = skb_put(skb, 4);
	trailer[0] = 0x80;
	trailer[1] = 1 << dp->index;
	trailer[2] = 0x10;
	trailer[3] = 0x00;

	return skb;
}

static struct sk_buff *trailer_rcv(struct sk_buff *skb, struct net_device *dev)
{
	u8 *trailer;
	int source_port;

	if (skb_linearize(skb))
		return NULL;

	trailer = skb_tail_pointer(skb) - 4;
	if (trailer[0] != 0x80 || (trailer[1] & 0xf8) != 0x00 ||
	    (trailer[2] & 0xef) != 0x00 || trailer[3] != 0x00)
		return NULL;

	source_port = trailer[1] & 7;

	skb->dev = dsa_conduit_find_user(dev, 0, source_port);
	if (!skb->dev)
		return NULL;

	if (pskb_trim_rcsum(skb, skb->len - 4))
		return NULL;

	return skb;
}

static const struct dsa_device_ops trailer_netdev_ops = {
	.name	= TRAILER_NAME,
	.proto	= DSA_TAG_PROTO_TRAILER,
	.xmit	= trailer_xmit,
	.rcv	= trailer_rcv,
	.needed_tailroom = 4,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_TRAILER, TRAILER_NAME);

module_dsa_tag_driver(trailer_netdev_ops);
