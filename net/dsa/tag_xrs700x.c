// SPDX-License-Identifier: GPL-2.0+
/*
 * XRS700x tag format handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2020 NovaTech LLC
 */

#include <linux/bitops.h>

#include "dsa_priv.h"

static struct sk_buff *xrs700x_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *partner, *dp = dsa_slave_to_port(dev);
	u8 *trailer;

	trailer = skb_put(skb, 1);
	trailer[0] = BIT(dp->index);

	if (dp->hsr_dev)
		dsa_hsr_foreach_port(partner, dp->ds, dp->hsr_dev)
			if (partner != dp)
				trailer[0] |= BIT(partner->index);

	return skb;
}

static struct sk_buff *xrs700x_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt)
{
	int source_port;
	u8 *trailer;

	trailer = skb_tail_pointer(skb) - 1;

	source_port = ffs((int)trailer[0]) - 1;

	if (source_port < 0)
		return NULL;

	skb->dev = dsa_master_find_slave(dev, 0, source_port);
	if (!skb->dev)
		return NULL;

	if (pskb_trim_rcsum(skb, skb->len - 1))
		return NULL;

	/* Frame is forwarded by hardware, don't forward in software. */
	skb->offload_fwd_mark = 1;

	return skb;
}

static const struct dsa_device_ops xrs700x_netdev_ops = {
	.name	= "xrs700x",
	.proto	= DSA_TAG_PROTO_XRS700X,
	.xmit	= xrs700x_xmit,
	.rcv	= xrs700x_rcv,
	.needed_tailroom = 1,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_XRS700X);

module_dsa_tag_driver(xrs700x_netdev_ops);
