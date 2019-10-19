// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Pengutronix, Juergen Borleis <jbe@pengutronix.de>
 */
#include <linux/dsa/lan9303.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "dsa_priv.h"

/* To define the outgoing port and to discover the incoming port a regular
 * VLAN tag is used by the LAN9303. But its VID meaning is 'special':
 *
 *       Dest MAC       Src MAC        TAG    Type
 * ...| 1 2 3 4 5 6 | 1 2 3 4 5 6 | 1 2 3 4 | 1 2 |...
 *                                |<------->|
 * TAG:
 *    |<------------->|
 *    |  1  2 | 3  4  |
 *      TPID    VID
 *     0x8100
 *
 * VID bit 3 indicates a request for an ALR lookup.
 *
 * If VID bit 3 is zero, then bits 0 and 1 specify the destination port
 * (0, 1, 2) or broadcast (3) or the source port (1, 2).
 *
 * VID bit 4 is used to specify if the STP port state should be overridden.
 * Required when no forwarding between the external ports should happen.
 */

#define LAN9303_TAG_LEN 4
# define LAN9303_TAG_TX_USE_ALR BIT(3)
# define LAN9303_TAG_TX_STP_OVERRIDE BIT(4)
# define LAN9303_TAG_RX_IGMP BIT(3)
# define LAN9303_TAG_RX_STP BIT(4)
# define LAN9303_TAG_RX_TRAPPED_TO_CPU (LAN9303_TAG_RX_IGMP | \
					LAN9303_TAG_RX_STP)

/* Decide whether to transmit using ALR lookup, or transmit directly to
 * port using tag. ALR learning is performed only when using ALR lookup.
 * If the two external ports are bridged and the frame is unicast,
 * then use ALR lookup to allow ALR learning on CPU port.
 * Otherwise transmit directly to port with STP state override.
 * See also: lan9303_separate_ports() and lan9303.pdf 6.4.10.1
 */
static int lan9303_xmit_use_arl(struct dsa_port *dp, u8 *dest_addr)
{
	struct lan9303 *chip = dp->ds->priv;

	return chip->is_bridged && !is_multicast_ether_addr(dest_addr);
}

static struct sk_buff *lan9303_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	u16 *lan9303_tag;

	/* insert a special VLAN tag between the MAC addresses
	 * and the current ethertype field.
	 */
	if (skb_cow_head(skb, LAN9303_TAG_LEN) < 0) {
		dev_dbg(&dev->dev,
			"Cannot make room for the special tag. Dropping packet\n");
		return NULL;
	}

	/* provide 'LAN9303_TAG_LEN' bytes additional space */
	skb_push(skb, LAN9303_TAG_LEN);

	/* make room between MACs and Ether-Type */
	memmove(skb->data, skb->data + LAN9303_TAG_LEN, 2 * ETH_ALEN);

	lan9303_tag = (u16 *)(skb->data + 2 * ETH_ALEN);
	lan9303_tag[0] = htons(ETH_P_8021Q);
	lan9303_tag[1] = lan9303_xmit_use_arl(dp, skb->data) ?
				LAN9303_TAG_TX_USE_ALR :
				dp->index | LAN9303_TAG_TX_STP_OVERRIDE;
	lan9303_tag[1] = htons(lan9303_tag[1]);

	return skb;
}

static struct sk_buff *lan9303_rcv(struct sk_buff *skb, struct net_device *dev,
				   struct packet_type *pt)
{
	u16 *lan9303_tag;
	u16 lan9303_tag1;
	unsigned int source_port;

	if (unlikely(!pskb_may_pull(skb, LAN9303_TAG_LEN))) {
		dev_warn_ratelimited(&dev->dev,
				     "Dropping packet, cannot pull\n");
		return NULL;
	}

	/* '->data' points into the middle of our special VLAN tag information:
	 *
	 * ~ MAC src   | 0x81 | 0x00 | 0xyy | 0xzz | ether type
	 *                           ^
	 *                        ->data
	 */
	lan9303_tag = (u16 *)(skb->data - 2);

	if (lan9303_tag[0] != htons(ETH_P_8021Q)) {
		dev_warn_ratelimited(&dev->dev, "Dropping packet due to invalid VLAN marker\n");
		return NULL;
	}

	lan9303_tag1 = ntohs(lan9303_tag[1]);
	source_port = lan9303_tag1 & 0x3;

	skb->dev = dsa_master_find_slave(dev, 0, source_port);
	if (!skb->dev) {
		dev_warn_ratelimited(&dev->dev, "Dropping packet due to invalid source port\n");
		return NULL;
	}

	/* remove the special VLAN tag between the MAC addresses
	 * and the current ethertype field.
	 */
	skb_pull_rcsum(skb, 2 + 2);
	memmove(skb->data - ETH_HLEN, skb->data - (ETH_HLEN + LAN9303_TAG_LEN),
		2 * ETH_ALEN);
	skb->offload_fwd_mark = !(lan9303_tag1 & LAN9303_TAG_RX_TRAPPED_TO_CPU);

	return skb;
}

static const struct dsa_device_ops lan9303_netdev_ops = {
	.name = "lan9303",
	.proto	= DSA_TAG_PROTO_LAN9303,
	.xmit = lan9303_xmit,
	.rcv = lan9303_rcv,
	.overhead = LAN9303_TAG_LEN,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_LAN9303);

module_dsa_tag_driver(lan9303_netdev_ops);
