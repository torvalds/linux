// SPDX-License-Identifier: GPL-2.0
/*
 * Handler for Realtek 4 byte DSA switch tags
 * Currently only supports protocol "A" found in RTL8366RB
 * Copyright (c) 2020 Linus Walleij <linus.walleij@linaro.org>
 *
 * This "proprietary tag" header looks like so:
 *
 * -------------------------------------------------
 * | MAC DA | MAC SA | 0x8899 | 2 bytes tag | Type |
 * -------------------------------------------------
 *
 * The 2 bytes tag form a 16 bit big endian word. The exact
 * meaning has been guessed from packet dumps from ingress
 * frames.
 */

#include <linux/etherdevice.h>
#include <linux/bits.h>

#include "dsa_priv.h"

#define RTL4_A_HDR_LEN		4
#define RTL4_A_ETHERTYPE	0x8899
#define RTL4_A_PROTOCOL_SHIFT	12
/*
 * 0x1 = Realtek Remote Control protocol (RRCP)
 * 0x2/0x3 seems to be used for loopback testing
 * 0x9 = RTL8306 DSA protocol
 * 0xa = RTL8366RB DSA protocol
 */
#define RTL4_A_PROTOCOL_RTL8366RB	0xa

static struct sk_buff *rtl4a_tag_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	__be16 *p;
	u8 *tag;
	u16 out;

	/* Pad out to at least 60 bytes */
	if (unlikely(__skb_put_padto(skb, ETH_ZLEN, false)))
		return NULL;

	netdev_dbg(dev, "add realtek tag to package to port %d\n",
		   dp->index);
	skb_push(skb, RTL4_A_HDR_LEN);

	memmove(skb->data, skb->data + RTL4_A_HDR_LEN, 2 * ETH_ALEN);
	tag = skb->data + 2 * ETH_ALEN;

	/* Set Ethertype */
	p = (__be16 *)tag;
	*p = htons(RTL4_A_ETHERTYPE);

	out = (RTL4_A_PROTOCOL_RTL8366RB << 12) | (2 << 8);
	/* The lower bits is the port number */
	out |= (u8)dp->index;
	p = (__be16 *)(tag + 2);
	*p = htons(out);

	return skb;
}

static struct sk_buff *rtl4a_tag_rcv(struct sk_buff *skb,
				     struct net_device *dev,
				     struct packet_type *pt)
{
	u16 protport;
	__be16 *p;
	u16 etype;
	u8 *tag;
	u8 prot;
	u8 port;

	if (unlikely(!pskb_may_pull(skb, RTL4_A_HDR_LEN)))
		return NULL;

	/* The RTL4 header has its own custom Ethertype 0x8899 and that
	 * starts right at the beginning of the packet, after the src
	 * ethernet addr. Apparently skb->data always points 2 bytes in,
	 * behind the Ethertype.
	 */
	tag = skb->data - 2;
	p = (__be16 *)tag;
	etype = ntohs(*p);
	if (etype != RTL4_A_ETHERTYPE) {
		/* Not custom, just pass through */
		netdev_dbg(dev, "non-realtek ethertype 0x%04x\n", etype);
		return skb;
	}
	p = (__be16 *)(tag + 2);
	protport = ntohs(*p);
	/* The 4 upper bits are the protocol */
	prot = (protport >> RTL4_A_PROTOCOL_SHIFT) & 0x0f;
	if (prot != RTL4_A_PROTOCOL_RTL8366RB) {
		netdev_err(dev, "unknown realtek protocol 0x%01x\n", prot);
		return NULL;
	}
	port = protport & 0xff;

	skb->dev = dsa_master_find_slave(dev, 0, port);
	if (!skb->dev) {
		netdev_dbg(dev, "could not find slave for port %d\n", port);
		return NULL;
	}

	/* Remove RTL4 tag and recalculate checksum */
	skb_pull_rcsum(skb, RTL4_A_HDR_LEN);

	/* Move ethernet DA and SA in front of the data */
	memmove(skb->data - ETH_HLEN,
		skb->data - ETH_HLEN - RTL4_A_HDR_LEN,
		2 * ETH_ALEN);

	skb->offload_fwd_mark = 1;

	return skb;
}

static const struct dsa_device_ops rtl4a_netdev_ops = {
	.name	= "rtl4a",
	.proto	= DSA_TAG_PROTO_RTL4_A,
	.xmit	= rtl4a_tag_xmit,
	.rcv	= rtl4a_tag_rcv,
	.overhead = RTL4_A_HDR_LEN,
};
module_dsa_tag_driver(rtl4a_netdev_ops);

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_RTL4_A);
