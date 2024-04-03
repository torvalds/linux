// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */


#include <linux/bitfield.h>
#include <linux/etherdevice.h>

#include "tag.h"

#define AR9331_NAME			"ar9331"

#define AR9331_HDR_LEN			2
#define AR9331_HDR_VERSION		1

#define AR9331_HDR_VERSION_MASK		GENMASK(15, 14)
#define AR9331_HDR_PRIORITY_MASK	GENMASK(13, 12)
#define AR9331_HDR_TYPE_MASK		GENMASK(10, 8)
#define AR9331_HDR_BROADCAST		BIT(7)
#define AR9331_HDR_FROM_CPU		BIT(6)
/* AR9331_HDR_RESERVED - not used or may be version field.
 * According to the AR8216 doc it should 0b10. On AR9331 it is 0b11 on RX path
 * and should be set to 0b11 to make it work.
 */
#define AR9331_HDR_RESERVED_MASK	GENMASK(5, 4)
#define AR9331_HDR_PORT_NUM_MASK	GENMASK(3, 0)

static struct sk_buff *ar9331_tag_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	__le16 *phdr;
	u16 hdr;

	phdr = skb_push(skb, AR9331_HDR_LEN);

	hdr = FIELD_PREP(AR9331_HDR_VERSION_MASK, AR9331_HDR_VERSION);
	hdr |= AR9331_HDR_FROM_CPU | dp->index;
	/* 0b10 for AR8216 and 0b11 for AR9331 */
	hdr |= AR9331_HDR_RESERVED_MASK;

	phdr[0] = cpu_to_le16(hdr);

	return skb;
}

static struct sk_buff *ar9331_tag_rcv(struct sk_buff *skb,
				      struct net_device *ndev)
{
	u8 ver, port;
	u16 hdr;

	if (unlikely(!pskb_may_pull(skb, AR9331_HDR_LEN)))
		return NULL;

	hdr = le16_to_cpu(*(__le16 *)skb_mac_header(skb));

	ver = FIELD_GET(AR9331_HDR_VERSION_MASK, hdr);
	if (unlikely(ver != AR9331_HDR_VERSION)) {
		netdev_warn_once(ndev, "%s:%i wrong header version 0x%2x\n",
				 __func__, __LINE__, hdr);
		return NULL;
	}

	if (unlikely(hdr & AR9331_HDR_FROM_CPU)) {
		netdev_warn_once(ndev, "%s:%i packet should not be from cpu 0x%2x\n",
				 __func__, __LINE__, hdr);
		return NULL;
	}

	skb_pull_rcsum(skb, AR9331_HDR_LEN);

	/* Get source port information */
	port = FIELD_GET(AR9331_HDR_PORT_NUM_MASK, hdr);

	skb->dev = dsa_conduit_find_user(ndev, 0, port);
	if (!skb->dev)
		return NULL;

	return skb;
}

static const struct dsa_device_ops ar9331_netdev_ops = {
	.name	= AR9331_NAME,
	.proto	= DSA_TAG_PROTO_AR9331,
	.xmit	= ar9331_tag_xmit,
	.rcv	= ar9331_tag_rcv,
	.needed_headroom = AR9331_HDR_LEN,
};

MODULE_DESCRIPTION("DSA tag driver for Atheros AR9331 SoC with built-in switch");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_AR9331, AR9331_NAME);
module_dsa_tag_driver(ar9331_netdev_ops);
