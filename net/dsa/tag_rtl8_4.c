// SPDX-License-Identifier: GPL-2.0
/*
 * Handler for Realtek 8 byte switch tags
 *
 * Copyright (C) 2021 Alvin Šipraga <alsi@bang-olufsen.dk>
 *
 * NOTE: Currently only supports protocol "4" found in the RTL8365MB, hence
 * named tag_rtl8_4.
 *
 * This tag has the following format:
 *
 *  0                                  7|8                                 15
 *  |-----------------------------------+-----------------------------------|---
 *  |                               (16-bit)                                | ^
 *  |                       Realtek EtherType [0x8899]                      | |
 *  |-----------------------------------+-----------------------------------| 8
 *  |              (8-bit)              |              (8-bit)              |
 *  |          Protocol [0x04]          |              REASON               | b
 *  |-----------------------------------+-----------------------------------| y
 *  |   (1)  | (1) | (2) |   (1)  | (3) | (1)  | (1) |    (1)    |   (5)    | t
 *  | FID_EN |  X  | FID | PRI_EN | PRI | KEEP |  X  | LEARN_DIS |    X     | e
 *  |-----------------------------------+-----------------------------------| s
 *  |   (1)  |                       (15-bit)                               | |
 *  |  ALLOW |                        TX/RX                                 | v
 *  |-----------------------------------+-----------------------------------|---
 *
 * With the following field descriptions:
 *
 *    field      | description
 *   ------------+-------------
 *    Realtek    | 0x8899: indicates that this is a proprietary Realtek tag;
 *     EtherType |         note that Realtek uses the same EtherType for
 *               |         other incompatible tag formats (e.g. tag_rtl4_a.c)
 *    Protocol   | 0x04: indicates that this tag conforms to this format
 *    X          | reserved
 *   ------------+-------------
 *    REASON     | reason for forwarding packet to CPU
 *               | 0: packet was forwarded or flooded to CPU
 *               | 80: packet was trapped to CPU
 *    FID_EN     | 1: packet has an FID
 *               | 0: no FID
 *    FID        | FID of packet (if FID_EN=1)
 *    PRI_EN     | 1: force priority of packet
 *               | 0: don't force priority
 *    PRI        | priority of packet (if PRI_EN=1)
 *    KEEP       | preserve packet VLAN tag format
 *    LEARN_DIS  | don't learn the source MAC address of the packet
 *    ALLOW      | 1: treat TX/RX field as an allowance port mask, meaning the
 *               |    packet may only be forwarded to ports specified in the
 *               |    mask
 *               | 0: no allowance port mask, TX/RX field is the forwarding
 *               |    port mask
 *    TX/RX      | TX (switch->CPU): port number the packet was received on
 *               | RX (CPU->switch): forwarding port mask (if ALLOW=0)
 *               |                   allowance port mask (if ALLOW=1)
 *
 * The tag can be positioned before Ethertype, using tag "rtl8_4":
 *
 *  +--------+--------+------------+------+-----
 *  | MAC DA | MAC SA | 8 byte tag | Type | ...
 *  +--------+--------+------------+------+-----
 *
 * The tag can also appear between the end of the payload and before the CRC,
 * using tag "rtl8_4t":
 *
 * +--------+--------+------+-----+---------+------------+-----+
 * | MAC DA | MAC SA | TYPE | ... | payload | 8-byte tag | CRC |
 * +--------+--------+------+-----+---------+------------+-----+
 *
 * The added bytes after the payload will break most checksums, either in
 * software or hardware. To avoid this issue, if the checksum is still pending,
 * this tagger checksums the packet in software before adding the tag.
 *
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/etherdevice.h>

#include "tag.h"

/* Protocols supported:
 *
 * 0x04 = RTL8365MB DSA protocol
 */

#define RTL8_4_NAME			"rtl8_4"
#define RTL8_4T_NAME			"rtl8_4t"

#define RTL8_4_TAG_LEN			8

#define RTL8_4_PROTOCOL			GENMASK(15, 8)
#define   RTL8_4_PROTOCOL_RTL8365MB	0x04
#define RTL8_4_REASON			GENMASK(7, 0)
#define   RTL8_4_REASON_FORWARD		0
#define   RTL8_4_REASON_TRAP		80

#define RTL8_4_LEARN_DIS		BIT(5)

#define RTL8_4_TX			GENMASK(3, 0)
#define RTL8_4_RX			GENMASK(10, 0)

static void rtl8_4_write_tag(struct sk_buff *skb, struct net_device *dev,
			     void *tag)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	__be16 tag16[RTL8_4_TAG_LEN / 2];

	/* Set Realtek EtherType */
	tag16[0] = htons(ETH_P_REALTEK);

	/* Set Protocol; zero REASON */
	tag16[1] = htons(FIELD_PREP(RTL8_4_PROTOCOL, RTL8_4_PROTOCOL_RTL8365MB));

	/* Zero FID_EN, FID, PRI_EN, PRI, KEEP; set LEARN_DIS */
	tag16[2] = htons(FIELD_PREP(RTL8_4_LEARN_DIS, 1));

	/* Zero ALLOW; set RX (CPU->switch) forwarding port mask */
	tag16[3] = htons(FIELD_PREP(RTL8_4_RX, BIT(dp->index)));

	memcpy(tag, tag16, RTL8_4_TAG_LEN);
}

static struct sk_buff *rtl8_4_tag_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	skb_push(skb, RTL8_4_TAG_LEN);

	dsa_alloc_etype_header(skb, RTL8_4_TAG_LEN);

	rtl8_4_write_tag(skb, dev, dsa_etype_header_pos_tx(skb));

	return skb;
}

static struct sk_buff *rtl8_4t_tag_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	/* Calculate the checksum here if not done yet as trailing tags will
	 * break either software or hardware based checksum
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_checksum_help(skb))
		return NULL;

	rtl8_4_write_tag(skb, dev, skb_put(skb, RTL8_4_TAG_LEN));

	return skb;
}

static int rtl8_4_read_tag(struct sk_buff *skb, struct net_device *dev,
			   void *tag)
{
	__be16 tag16[RTL8_4_TAG_LEN / 2];
	u16 etype;
	u8 reason;
	u8 proto;
	u8 port;

	memcpy(tag16, tag, RTL8_4_TAG_LEN);

	/* Parse Realtek EtherType */
	etype = ntohs(tag16[0]);
	if (unlikely(etype != ETH_P_REALTEK)) {
		dev_warn_ratelimited(&dev->dev,
				     "non-realtek ethertype 0x%04x\n", etype);
		return -EPROTO;
	}

	/* Parse Protocol */
	proto = FIELD_GET(RTL8_4_PROTOCOL, ntohs(tag16[1]));
	if (unlikely(proto != RTL8_4_PROTOCOL_RTL8365MB)) {
		dev_warn_ratelimited(&dev->dev,
				     "unknown realtek protocol 0x%02x\n",
				     proto);
		return -EPROTO;
	}

	/* Parse REASON */
	reason = FIELD_GET(RTL8_4_REASON, ntohs(tag16[1]));

	/* Parse TX (switch->CPU) */
	port = FIELD_GET(RTL8_4_TX, ntohs(tag16[3]));
	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev) {
		dev_warn_ratelimited(&dev->dev,
				     "could not find user for port %d\n",
				     port);
		return -ENOENT;
	}

	if (reason != RTL8_4_REASON_TRAP)
		dsa_default_offload_fwd_mark(skb);

	return 0;
}

static struct sk_buff *rtl8_4_tag_rcv(struct sk_buff *skb,
				      struct net_device *dev)
{
	if (unlikely(!pskb_may_pull(skb, RTL8_4_TAG_LEN)))
		return NULL;

	if (unlikely(rtl8_4_read_tag(skb, dev, dsa_etype_header_pos_rx(skb))))
		return NULL;

	/* Remove tag and recalculate checksum */
	skb_pull_rcsum(skb, RTL8_4_TAG_LEN);

	dsa_strip_etype_header(skb, RTL8_4_TAG_LEN);

	return skb;
}

static struct sk_buff *rtl8_4t_tag_rcv(struct sk_buff *skb,
				       struct net_device *dev)
{
	if (skb_linearize(skb))
		return NULL;

	if (unlikely(rtl8_4_read_tag(skb, dev, skb_tail_pointer(skb) - RTL8_4_TAG_LEN)))
		return NULL;

	if (pskb_trim_rcsum(skb, skb->len - RTL8_4_TAG_LEN))
		return NULL;

	return skb;
}

/* Ethertype version */
static const struct dsa_device_ops rtl8_4_netdev_ops = {
	.name = "rtl8_4",
	.proto = DSA_TAG_PROTO_RTL8_4,
	.xmit = rtl8_4_tag_xmit,
	.rcv = rtl8_4_tag_rcv,
	.needed_headroom = RTL8_4_TAG_LEN,
};

DSA_TAG_DRIVER(rtl8_4_netdev_ops);

MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_RTL8_4, RTL8_4_NAME);

/* Tail version */
static const struct dsa_device_ops rtl8_4t_netdev_ops = {
	.name = "rtl8_4t",
	.proto = DSA_TAG_PROTO_RTL8_4T,
	.xmit = rtl8_4t_tag_xmit,
	.rcv = rtl8_4t_tag_rcv,
	.needed_tailroom = RTL8_4_TAG_LEN,
};

DSA_TAG_DRIVER(rtl8_4t_netdev_ops);

MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_RTL8_4T, RTL8_4T_NAME);

static struct dsa_tag_driver *dsa_tag_drivers[] = {
	&DSA_TAG_DRIVER_NAME(rtl8_4_netdev_ops),
	&DSA_TAG_DRIVER_NAME(rtl8_4t_netdev_ops),
};
module_dsa_tag_drivers(dsa_tag_drivers);

MODULE_DESCRIPTION("DSA tag driver for Realtek 8 byte protocol 4 tags");
MODULE_LICENSE("GPL");
