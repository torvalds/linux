// SPDX-License-Identifier: GPL-2.0+
/*
 * net/dsa/tag_edsa.c - Ethertype DSA tagging
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "dsa_priv.h"

#define DSA_HLEN	4
#define EDSA_HLEN	8

#define FRAME_TYPE_TO_CPU	0x00
#define FRAME_TYPE_FORWARD	0x03

#define TO_CPU_CODE_MGMT_TRAP		0x00
#define TO_CPU_CODE_FRAME2REG		0x01
#define TO_CPU_CODE_IGMP_MLD_TRAP	0x02
#define TO_CPU_CODE_POLICY_TRAP		0x03
#define TO_CPU_CODE_ARP_MIRROR		0x04
#define TO_CPU_CODE_POLICY_MIRROR	0x05

static struct sk_buff *edsa_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);
	u8 *edsa_header;

	/*
	 * Convert the outermost 802.1q tag to a DSA tag and prepend
	 * a DSA ethertype field is the packet is tagged, or insert
	 * a DSA ethertype plus DSA tag between the addresses and the
	 * current ethertype field if the packet is untagged.
	 */
	if (skb->protocol == htons(ETH_P_8021Q)) {
		if (skb_cow_head(skb, DSA_HLEN) < 0)
			return NULL;
		skb_push(skb, DSA_HLEN);

		memmove(skb->data, skb->data + DSA_HLEN, 2 * ETH_ALEN);

		/*
		 * Construct tagged FROM_CPU DSA tag from 802.1q tag.
		 */
		edsa_header = skb->data + 2 * ETH_ALEN;
		edsa_header[0] = (ETH_P_EDSA >> 8) & 0xff;
		edsa_header[1] = ETH_P_EDSA & 0xff;
		edsa_header[2] = 0x00;
		edsa_header[3] = 0x00;
		edsa_header[4] = 0x60 | dp->ds->index;
		edsa_header[5] = dp->index << 3;

		/*
		 * Move CFI field from byte 6 to byte 5.
		 */
		if (edsa_header[6] & 0x10) {
			edsa_header[5] |= 0x01;
			edsa_header[6] &= ~0x10;
		}
	} else {
		if (skb_cow_head(skb, EDSA_HLEN) < 0)
			return NULL;
		skb_push(skb, EDSA_HLEN);

		memmove(skb->data, skb->data + EDSA_HLEN, 2 * ETH_ALEN);

		/*
		 * Construct untagged FROM_CPU DSA tag.
		 */
		edsa_header = skb->data + 2 * ETH_ALEN;
		edsa_header[0] = (ETH_P_EDSA >> 8) & 0xff;
		edsa_header[1] = ETH_P_EDSA & 0xff;
		edsa_header[2] = 0x00;
		edsa_header[3] = 0x00;
		edsa_header[4] = 0x40 | dp->ds->index;
		edsa_header[5] = dp->index << 3;
		edsa_header[6] = 0x00;
		edsa_header[7] = 0x00;
	}

	return skb;
}

static struct sk_buff *edsa_rcv(struct sk_buff *skb, struct net_device *dev,
				struct packet_type *pt)
{
	u8 *edsa_header;
	int frame_type;
	int code;
	int source_device;
	int source_port;

	if (unlikely(!pskb_may_pull(skb, EDSA_HLEN)))
		return NULL;

	/*
	 * Skip the two null bytes after the ethertype.
	 */
	edsa_header = skb->data + 2;

	/*
	 * Check that frame type is either TO_CPU or FORWARD.
	 */
	frame_type = edsa_header[0] >> 6;

	switch (frame_type) {
	case FRAME_TYPE_TO_CPU:
		code = (edsa_header[1] & 0x6) | ((edsa_header[2] >> 4) & 1);

		/*
		 * Mark the frame to never egress on any port of the same switch
		 * unless it's a trapped IGMP/MLD packet, in which case the
		 * bridge might want to forward it.
		 */
		if (code != TO_CPU_CODE_IGMP_MLD_TRAP)
			skb->offload_fwd_mark = 1;

		break;

	case FRAME_TYPE_FORWARD:
		skb->offload_fwd_mark = 1;
		break;

	default:
		return NULL;
	}

	/*
	 * Determine source device and port.
	 */
	source_device = edsa_header[0] & 0x1f;
	source_port = (edsa_header[1] >> 3) & 0x1f;

	skb->dev = dsa_master_find_slave(dev, source_device, source_port);
	if (!skb->dev)
		return NULL;

	/*
	 * If the 'tagged' bit is set, convert the DSA tag to a 802.1q
	 * tag and delete the ethertype part.  If the 'tagged' bit is
	 * clear, delete the ethertype and the DSA tag parts.
	 */
	if (edsa_header[0] & 0x20) {
		u8 new_header[4];

		/*
		 * Insert 802.1q ethertype and copy the VLAN-related
		 * fields, but clear the bit that will hold CFI (since
		 * DSA uses that bit location for another purpose).
		 */
		new_header[0] = (ETH_P_8021Q >> 8) & 0xff;
		new_header[1] = ETH_P_8021Q & 0xff;
		new_header[2] = edsa_header[2] & ~0x10;
		new_header[3] = edsa_header[3];

		/*
		 * Move CFI bit from its place in the DSA header to
		 * its 802.1q-designated place.
		 */
		if (edsa_header[1] & 0x01)
			new_header[2] |= 0x10;

		skb_pull_rcsum(skb, DSA_HLEN);

		/*
		 * Update packet checksum if skb is CHECKSUM_COMPLETE.
		 */
		if (skb->ip_summed == CHECKSUM_COMPLETE) {
			__wsum c = skb->csum;
			c = csum_add(c, csum_partial(new_header + 2, 2, 0));
			c = csum_sub(c, csum_partial(edsa_header + 2, 2, 0));
			skb->csum = c;
		}

		memcpy(edsa_header, new_header, DSA_HLEN);

		memmove(skb->data - ETH_HLEN,
			skb->data - ETH_HLEN - DSA_HLEN,
			2 * ETH_ALEN);
	} else {
		/*
		 * Remove DSA tag and update checksum.
		 */
		skb_pull_rcsum(skb, EDSA_HLEN);
		memmove(skb->data - ETH_HLEN,
			skb->data - ETH_HLEN - EDSA_HLEN,
			2 * ETH_ALEN);
	}

	return skb;
}

static void edsa_tag_flow_dissect(const struct sk_buff *skb, __be16 *proto,
				  int *offset)
{
	*offset = 8;
	*proto = ((__be16 *)skb->data)[3];
}

static const struct dsa_device_ops edsa_netdev_ops = {
	.name	= "edsa",
	.proto	= DSA_TAG_PROTO_EDSA,
	.xmit	= edsa_xmit,
	.rcv	= edsa_rcv,
	.flow_dissect   = edsa_tag_flow_dissect,
	.overhead = EDSA_HLEN,
};

MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_EDSA);

module_dsa_tag_driver(edsa_netdev_ops);
