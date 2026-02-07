// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DSA Special Tag for MaxLinear 862xx switch chips
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 * Copyright (C) 2024 MaxLinear Inc.
 */

#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/dsa.h>
#include "tag.h"

#define MXL862_NAME	"mxl862xx"

#define MXL862_HEADER_LEN	8

/* Word 0 -> EtherType */

/* Word 2 */
#define MXL862_SUBIF_ID		GENMASK(4, 0)

/* Word 3 */
#define MXL862_IGP_EGP		GENMASK(3, 0)

static struct sk_buff *mxl862_tag_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	struct dsa_port *cpu_dp = dp->cpu_dp;
	unsigned int cpu_port, sub_interface;
	__be16 *mxl862_tag;

	cpu_port = cpu_dp->index;

	/* target port sub-interface ID relative to the CPU port */
	sub_interface = dp->index + 16 - cpu_port;

	/* provide additional space 'MXL862_HEADER_LEN' bytes */
	skb_push(skb, MXL862_HEADER_LEN);

	/* shift MAC address to the beginning of the enlarged buffer,
	 * releasing the space required for DSA tag (between MAC address and
	 * Ethertype)
	 */
	dsa_alloc_etype_header(skb, MXL862_HEADER_LEN);

	/* special tag ingress (from the perspective of the switch) */
	mxl862_tag = dsa_etype_header_pos_tx(skb);
	mxl862_tag[0] = htons(ETH_P_MXLGSW);
	mxl862_tag[1] = 0;
	mxl862_tag[2] = htons(FIELD_PREP(MXL862_SUBIF_ID, sub_interface));
	mxl862_tag[3] = htons(FIELD_PREP(MXL862_IGP_EGP, cpu_port));

	return skb;
}

static struct sk_buff *mxl862_tag_rcv(struct sk_buff *skb,
				      struct net_device *dev)
{
	__be16 *mxl862_tag;
	int port;

	if (unlikely(!pskb_may_pull(skb, MXL862_HEADER_LEN))) {
		dev_warn_ratelimited(&dev->dev, "Cannot pull SKB, packet dropped\n");
		return NULL;
	}

	mxl862_tag = dsa_etype_header_pos_rx(skb);

	if (unlikely(mxl862_tag[0] != htons(ETH_P_MXLGSW))) {
		dev_warn_ratelimited(&dev->dev,
				     "Invalid special tag marker, packet dropped, tag: %8ph\n",
				     mxl862_tag);
		return NULL;
	}

	/* Get source port information */
	port = FIELD_GET(MXL862_IGP_EGP, ntohs(mxl862_tag[3]));
	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (unlikely(!skb->dev)) {
		dev_warn_ratelimited(&dev->dev,
				     "Invalid source port, packet dropped, tag: %8ph\n",
				     mxl862_tag);
		return NULL;
	}

	/* remove the MxL862xx special tag between the MAC addresses and the
	 * current ethertype field.
	 */
	skb_pull_rcsum(skb, MXL862_HEADER_LEN);
	dsa_strip_etype_header(skb, MXL862_HEADER_LEN);

	return skb;
}

static const struct dsa_device_ops mxl862_netdev_ops = {
	.name = MXL862_NAME,
	.proto = DSA_TAG_PROTO_MXL862,
	.xmit = mxl862_tag_xmit,
	.rcv = mxl862_tag_rcv,
	.needed_headroom = MXL862_HEADER_LEN,
};

MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_MXL862, MXL862_NAME);
MODULE_DESCRIPTION("DSA tag driver for MaxLinear MxL862xx switches");
MODULE_LICENSE("GPL");

module_dsa_tag_driver(mxl862_netdev_ops);
