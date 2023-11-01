// SPDX-License-Identifier: GPL-2.0+
/*
 * Regular and Ethertype DSA tagging
 * Copyright (c) 2008-2009 Marvell Semiconductor
 *
 * Regular DSA
 * -----------

 * For untagged (in 802.1Q terms) packets, the switch will splice in
 * the tag between the SA and the ethertype of the original
 * packet. Tagged frames will instead have their outermost .1Q tag
 * converted to a DSA tag. It expects the same layout when receiving
 * packets from the CPU.
 *
 * Example:
 *
 *     .----.----.----.---------
 * Pu: | DA | SA | ET | Payload ...
 *     '----'----'----'---------
 *       6    6    2       N
 *     .----.----.--------.-----.----.---------
 * Pt: | DA | SA | 0x8100 | TCI | ET | Payload ...
 *     '----'----'--------'-----'----'---------
 *       6    6       2      2    2       N
 *     .----.----.-----.----.---------
 * Pd: | DA | SA | DSA | ET | Payload ...
 *     '----'----'-----'----'---------
 *       6    6     4    2       N
 *
 * No matter if a packet is received untagged (Pu) or tagged (Pt),
 * they will both have the same layout (Pd) when they are sent to the
 * CPU. This is done by ignoring 802.3, replacing the ethertype field
 * with more metadata, among which is a bit to signal if the original
 * packet was tagged or not.
 *
 * Ethertype DSA
 * -------------
 * Uses the exact same tag format as regular DSA, but also includes a
 * proper ethertype field (which the mv88e6xxx driver sets to
 * ETH_P_EDSA/0xdada) followed by two zero bytes:
 *
 * .----.----.--------.--------.-----.----.---------
 * | DA | SA | 0xdada | 0x0000 | DSA | ET | Payload ...
 * '----'----'--------'--------'-----'----'---------
 *   6    6       2        2      4    2       N
 */

#include <linux/dsa/mv88e6xxx.h>
#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "tag.h"

#define DSA_NAME	"dsa"
#define EDSA_NAME	"edsa"

#define DSA_HLEN	4

/**
 * enum dsa_cmd - DSA Command
 * @DSA_CMD_TO_CPU: Set on packets that were trapped or mirrored to
 *     the CPU port. This is needed to implement control protocols,
 *     e.g. STP and LLDP, that must not allow those control packets to
 *     be switched according to the normal rules.
 * @DSA_CMD_FROM_CPU: Used by the CPU to send a packet to a specific
 *     port, ignoring all the barriers that the switch normally
 *     enforces (VLANs, STP port states etc.). No source address
 *     learning takes place. "sudo send packet"
 * @DSA_CMD_TO_SNIFFER: Set on the copies of packets that matched some
 *     user configured ingress or egress monitor criteria. These are
 *     forwarded by the switch tree to the user configured ingress or
 *     egress monitor port, which can be set to the CPU port or a
 *     regular port. If the destination is a regular port, the tag
 *     will be removed before egressing the port. If the destination
 *     is the CPU port, the tag will not be removed.
 * @DSA_CMD_FORWARD: This tag is used on all bulk traffic passing
 *     through the switch tree, including the flows that are directed
 *     towards the CPU. Its device/port tuple encodes the original
 *     source port on which the packet ingressed. It can also be used
 *     on transmit by the CPU to defer the forwarding decision to the
 *     hardware, based on the current config of PVT/VTU/ATU
 *     etc. Source address learning takes places if enabled on the
 *     receiving DSA/CPU port.
 */
enum dsa_cmd {
	DSA_CMD_TO_CPU     = 0,
	DSA_CMD_FROM_CPU   = 1,
	DSA_CMD_TO_SNIFFER = 2,
	DSA_CMD_FORWARD    = 3
};

/**
 * enum dsa_code - TO_CPU Code
 *
 * @DSA_CODE_MGMT_TRAP: DA was classified as a management
 *     address. Typical examples include STP BPDUs and LLDP.
 * @DSA_CODE_FRAME2REG: Response to a "remote management" request.
 * @DSA_CODE_IGMP_MLD_TRAP: IGMP/MLD signaling.
 * @DSA_CODE_POLICY_TRAP: Frame matched some policy configuration on
 *     the device. Typical examples are matching on DA/SA/VID and DHCP
 *     snooping.
 * @DSA_CODE_ARP_MIRROR: The name says it all really.
 * @DSA_CODE_POLICY_MIRROR: Same as @DSA_CODE_POLICY_TRAP, but the
 *     particular policy was set to trigger a mirror instead of a
 *     trap.
 * @DSA_CODE_RESERVED_6: Unused on all devices up to at least 6393X.
 * @DSA_CODE_RESERVED_7: Unused on all devices up to at least 6393X.
 *
 * A 3-bit code is used to relay why a particular frame was sent to
 * the CPU. We only use this to determine if the packet was mirrored
 * or trapped, i.e. whether the packet has been forwarded by hardware
 * or not.
 *
 * This is the superset of all possible codes. Any particular device
 * may only implement a subset.
 */
enum dsa_code {
	DSA_CODE_MGMT_TRAP     = 0,
	DSA_CODE_FRAME2REG     = 1,
	DSA_CODE_IGMP_MLD_TRAP = 2,
	DSA_CODE_POLICY_TRAP   = 3,
	DSA_CODE_ARP_MIRROR    = 4,
	DSA_CODE_POLICY_MIRROR = 5,
	DSA_CODE_RESERVED_6    = 6,
	DSA_CODE_RESERVED_7    = 7
};

static struct sk_buff *dsa_xmit_ll(struct sk_buff *skb, struct net_device *dev,
				   u8 extra)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	struct net_device *br_dev;
	u8 tag_dev, tag_port;
	enum dsa_cmd cmd;
	u8 *dsa_header;

	if (skb->offload_fwd_mark) {
		unsigned int bridge_num = dsa_port_bridge_num_get(dp);
		struct dsa_switch_tree *dst = dp->ds->dst;

		cmd = DSA_CMD_FORWARD;

		/* When offloading forwarding for a bridge, inject FORWARD
		 * packets on behalf of a virtual switch device with an index
		 * past the physical switches.
		 */
		tag_dev = dst->last_switch + bridge_num;
		tag_port = 0;
	} else {
		cmd = DSA_CMD_FROM_CPU;
		tag_dev = dp->ds->index;
		tag_port = dp->index;
	}

	br_dev = dsa_port_bridge_dev_get(dp);

	/* If frame is already 802.1Q tagged, we can convert it to a DSA
	 * tag (avoiding a memmove), but only if the port is standalone
	 * (in which case we always send FROM_CPU) or if the port's
	 * bridge has VLAN filtering enabled (in which case the CPU port
	 * will be a member of the VLAN).
	 */
	if (skb->protocol == htons(ETH_P_8021Q) &&
	    (!br_dev || br_vlan_enabled(br_dev))) {
		if (extra) {
			skb_push(skb, extra);
			dsa_alloc_etype_header(skb, extra);
		}

		/* Construct tagged DSA tag from 802.1Q tag. */
		dsa_header = dsa_etype_header_pos_tx(skb) + extra;
		dsa_header[0] = (cmd << 6) | 0x20 | tag_dev;
		dsa_header[1] = tag_port << 3;

		/* Move CFI field from byte 2 to byte 1. */
		if (dsa_header[2] & 0x10) {
			dsa_header[1] |= 0x01;
			dsa_header[2] &= ~0x10;
		}
	} else {
		u16 vid;

		vid = br_dev ? MV88E6XXX_VID_BRIDGED : MV88E6XXX_VID_STANDALONE;

		skb_push(skb, DSA_HLEN + extra);
		dsa_alloc_etype_header(skb, DSA_HLEN + extra);

		/* Construct DSA header from untagged frame. */
		dsa_header = dsa_etype_header_pos_tx(skb) + extra;

		dsa_header[0] = (cmd << 6) | tag_dev;
		dsa_header[1] = tag_port << 3;
		dsa_header[2] = vid >> 8;
		dsa_header[3] = vid & 0xff;
	}

	return skb;
}

static struct sk_buff *dsa_rcv_ll(struct sk_buff *skb, struct net_device *dev,
				  u8 extra)
{
	bool trap = false, trunk = false;
	int source_device, source_port;
	enum dsa_code code;
	enum dsa_cmd cmd;
	u8 *dsa_header;

	/* The ethertype field is part of the DSA header. */
	dsa_header = dsa_etype_header_pos_rx(skb);

	cmd = dsa_header[0] >> 6;
	switch (cmd) {
	case DSA_CMD_FORWARD:
		trunk = !!(dsa_header[1] & 4);
		break;

	case DSA_CMD_TO_CPU:
		code = (dsa_header[1] & 0x6) | ((dsa_header[2] >> 4) & 1);

		switch (code) {
		case DSA_CODE_FRAME2REG:
			/* Remote management is not implemented yet,
			 * drop.
			 */
			return NULL;
		case DSA_CODE_ARP_MIRROR:
		case DSA_CODE_POLICY_MIRROR:
			/* Mark mirrored packets to notify any upper
			 * device (like a bridge) that forwarding has
			 * already been done by hardware.
			 */
			break;
		case DSA_CODE_MGMT_TRAP:
		case DSA_CODE_IGMP_MLD_TRAP:
		case DSA_CODE_POLICY_TRAP:
			/* Traps have, by definition, not been
			 * forwarded by hardware, so don't mark them.
			 */
			trap = true;
			break;
		default:
			/* Reserved code, this could be anything. Drop
			 * seems like the safest option.
			 */
			return NULL;
		}

		break;

	default:
		return NULL;
	}

	source_device = dsa_header[0] & 0x1f;
	source_port = (dsa_header[1] >> 3) & 0x1f;

	if (trunk) {
		struct dsa_port *cpu_dp = dev->dsa_ptr;
		struct dsa_lag *lag;

		/* The exact source port is not available in the tag,
		 * so we inject the frame directly on the upper
		 * team/bond.
		 */
		lag = dsa_lag_by_id(cpu_dp->dst, source_port + 1);
		skb->dev = lag ? lag->dev : NULL;
	} else {
		skb->dev = dsa_conduit_find_user(dev, source_device,
						 source_port);
	}

	if (!skb->dev)
		return NULL;

	/* When using LAG offload, skb->dev is not a DSA user interface,
	 * so we cannot call dsa_default_offload_fwd_mark and we need to
	 * special-case it.
	 */
	if (trunk)
		skb->offload_fwd_mark = true;
	else if (!trap)
		dsa_default_offload_fwd_mark(skb);

	/* If the 'tagged' bit is set; convert the DSA tag to a 802.1Q
	 * tag, and delete the ethertype (extra) if applicable. If the
	 * 'tagged' bit is cleared; delete the DSA tag, and ethertype
	 * if applicable.
	 */
	if (dsa_header[0] & 0x20) {
		u8 new_header[4];

		/* Insert 802.1Q ethertype and copy the VLAN-related
		 * fields, but clear the bit that will hold CFI (since
		 * DSA uses that bit location for another purpose).
		 */
		new_header[0] = (ETH_P_8021Q >> 8) & 0xff;
		new_header[1] = ETH_P_8021Q & 0xff;
		new_header[2] = dsa_header[2] & ~0x10;
		new_header[3] = dsa_header[3];

		/* Move CFI bit from its place in the DSA header to
		 * its 802.1Q-designated place.
		 */
		if (dsa_header[1] & 0x01)
			new_header[2] |= 0x10;

		/* Update packet checksum if skb is CHECKSUM_COMPLETE. */
		if (skb->ip_summed == CHECKSUM_COMPLETE) {
			__wsum c = skb->csum;
			c = csum_add(c, csum_partial(new_header + 2, 2, 0));
			c = csum_sub(c, csum_partial(dsa_header + 2, 2, 0));
			skb->csum = c;
		}

		memcpy(dsa_header, new_header, DSA_HLEN);

		if (extra)
			dsa_strip_etype_header(skb, extra);
	} else {
		skb_pull_rcsum(skb, DSA_HLEN);
		dsa_strip_etype_header(skb, DSA_HLEN + extra);
	}

	return skb;
}

#if IS_ENABLED(CONFIG_NET_DSA_TAG_DSA)

static struct sk_buff *dsa_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return dsa_xmit_ll(skb, dev, 0);
}

static struct sk_buff *dsa_rcv(struct sk_buff *skb, struct net_device *dev)
{
	if (unlikely(!pskb_may_pull(skb, DSA_HLEN)))
		return NULL;

	return dsa_rcv_ll(skb, dev, 0);
}

static const struct dsa_device_ops dsa_netdev_ops = {
	.name	  = DSA_NAME,
	.proto	  = DSA_TAG_PROTO_DSA,
	.xmit	  = dsa_xmit,
	.rcv	  = dsa_rcv,
	.needed_headroom = DSA_HLEN,
};

DSA_TAG_DRIVER(dsa_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_DSA, DSA_NAME);
#endif	/* CONFIG_NET_DSA_TAG_DSA */

#if IS_ENABLED(CONFIG_NET_DSA_TAG_EDSA)

#define EDSA_HLEN 8

static struct sk_buff *edsa_xmit(struct sk_buff *skb, struct net_device *dev)
{
	u8 *edsa_header;

	skb = dsa_xmit_ll(skb, dev, EDSA_HLEN - DSA_HLEN);
	if (!skb)
		return NULL;

	edsa_header = dsa_etype_header_pos_tx(skb);
	edsa_header[0] = (ETH_P_EDSA >> 8) & 0xff;
	edsa_header[1] = ETH_P_EDSA & 0xff;
	edsa_header[2] = 0x00;
	edsa_header[3] = 0x00;
	return skb;
}

static struct sk_buff *edsa_rcv(struct sk_buff *skb, struct net_device *dev)
{
	if (unlikely(!pskb_may_pull(skb, EDSA_HLEN)))
		return NULL;

	skb_pull_rcsum(skb, EDSA_HLEN - DSA_HLEN);

	return dsa_rcv_ll(skb, dev, EDSA_HLEN - DSA_HLEN);
}

static const struct dsa_device_ops edsa_netdev_ops = {
	.name	  = EDSA_NAME,
	.proto	  = DSA_TAG_PROTO_EDSA,
	.xmit	  = edsa_xmit,
	.rcv	  = edsa_rcv,
	.needed_headroom = EDSA_HLEN,
};

DSA_TAG_DRIVER(edsa_netdev_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_EDSA, EDSA_NAME);
#endif	/* CONFIG_NET_DSA_TAG_EDSA */

static struct dsa_tag_driver *dsa_tag_drivers[] = {
#if IS_ENABLED(CONFIG_NET_DSA_TAG_DSA)
	&DSA_TAG_DRIVER_NAME(dsa_netdev_ops),
#endif
#if IS_ENABLED(CONFIG_NET_DSA_TAG_EDSA)
	&DSA_TAG_DRIVER_NAME(edsa_netdev_ops),
#endif
};

module_dsa_tag_drivers(dsa_tag_drivers);

MODULE_LICENSE("GPL");
