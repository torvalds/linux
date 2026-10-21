// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Joris Vaisvila <joey@tinyisr.com>
 * MT7628 switch tag support
 */

#include <linux/etherdevice.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>

#include "tag.h"

/*
 * The MT7628 tag is encoded in the VLAN TPID field.
 * On TX the lower 6 bits encode the destination port bitmask.
 * On RX the lower 3 bits encode the source port number.
 *
 * The switch hardware will not modify the TPID of an incoming packet if it is
 * already VLAN tagged. To work around this the switch is configured to always
 * append a tag_8021q standalone VLAN tag for each port. That means we can
 * safely strip the outer VLAN tag after parsing it.
 *
 * A VLAN tag is constructed on egress to target the standalone VLAN and
 * destination port.
 */

#define MT7628_TAG_NAME "mt7628"

#define MT7628_TAG_TX_PORT GENMASK(5, 0)
#define MT7628_TAG_RX_PORT GENMASK(2, 0)
#define MT7628_TAG_LEN 4

static struct sk_buff *mt7628_tag_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct dsa_port *dp;
	u16 xmit_vlan;
	__be16 *tag;

	dp = dsa_user_to_port(dev);
	xmit_vlan = dsa_tag_8021q_standalone_vid(dp);

	skb_push(skb, MT7628_TAG_LEN);
	dsa_alloc_etype_header(skb, MT7628_TAG_LEN);

	tag = dsa_etype_header_pos_tx(skb);

	tag[0] = htons(ETH_P_8021Q |
		       FIELD_PREP(MT7628_TAG_TX_PORT,
				  dsa_xmit_port_mask(skb, dev)));
	tag[1] = htons(xmit_vlan);

	return skb;
}

static struct sk_buff *mt7628_tag_rcv(struct sk_buff *skb,
				      struct net_device *dev)
{
	__be16 *phdr;

	if (unlikely(!pskb_may_pull(skb, MT7628_TAG_LEN))) {
		kfree_skb(skb);
		return NULL;
	}

	phdr = dsa_etype_header_pos_rx(skb);
	skb->dev =
	    dsa_conduit_find_user(dev, 0,
				  FIELD_GET(MT7628_TAG_RX_PORT, ntohs(*phdr)));
	if (!skb->dev) {
		kfree_skb(skb);
		return NULL;
	}

	skb_pull_rcsum(skb, MT7628_TAG_LEN);
	dsa_strip_etype_header(skb, MT7628_TAG_LEN);
	dsa_default_offload_fwd_mark(skb);
	return skb;
}

static const struct dsa_device_ops mt7628_tag_ops = {
	.name = MT7628_TAG_NAME,
	.proto = DSA_TAG_PROTO_MT7628,
	.xmit = mt7628_tag_xmit,
	.rcv = mt7628_tag_rcv,
	.needed_headroom = MT7628_TAG_LEN,
};

module_dsa_tag_driver(mt7628_tag_ops);

MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_MT7628, MT7628_TAG_NAME);
MODULE_DESCRIPTION("DSA tag driver for MT7628 switch");
MODULE_LICENSE("GPL");
