// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Linus Walleij <linusw@kernel.org>
 */
#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/log2.h>
#include <linux/list.h>
#include <linux/net.h>
#include <linux/slab.h>

#include "tag.h"

/* The Micrel KS8995XA / Microchip KSZ8995XA Special Tag Packet ID (STPID)
 * pushes its tag in a modified VLAN (802.1Q) tag.
 * -----------------------------------------------------------
 * | MAC DA | MAC SA | 2 bytes tag | 2 bytes TCI | EtherType |
 * -----------------------------------------------------------
 * The tag is: 0x8100 |= BIT(port), ports 0,1,2,3
 */

#define KS8995_NAME "ks8995"

#define KS8995_TAG_LEN		VLAN_HLEN
/* Reserve room for the switch tag and a hardware-accelerated VLAN tag. */
#define KS8995_NEEDED_HEADROOM	(KS8995_TAG_LEN + VLAN_HLEN)

#define KS8995M_STPID_STD	GENMASK(15, 4)
#define KS8995M_STPID_PORTMASK	GENMASK(3, 0)
#define KS8995M_STPID(portmask)	htons(ETH_P_8021Q | FIELD_PREP(KS8995M_STPID_PORTMASK, portmask))

static struct sk_buff *ks8995_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vlan_ethhdr *hdr;
	u16 portmask;

	/* VLAN insertion requires a complete, linear Ethernet header. */
	if (unlikely(!pskb_may_pull(skb, ETH_HLEN))) {
		kfree_skb(skb);
		return NULL;
	}

	/* Prepare the special KS8995 tags */
	portmask = dsa_xmit_port_mask(skb, dev);

	/* The switch expects the special tag at offset 12. Move any hardware
	 * accelerated VLAN tag into the payload so the conduit cannot insert
	 * it outside the special tag.
	 */
	if (unlikely(skb_vlan_tag_present(skb))) {
		skb = __vlan_hwaccel_push_inside(skb);
		if (!skb)
			return NULL;
	}

	/* Always add a distinct outer tag. The user port removes this field on
	 * egress, so reusing an existing 802.1Q tag would consume that VLAN tag.
	 */
	skb = vlan_insert_tag(skb, KS8995M_STPID(portmask), 0);
	/* vlan_insert_tag() drops the skb on failure */
	if (!skb)
		return NULL;
	hdr = skb_vlan_eth_hdr(skb);
	netdev_dbg(dev, "%s: inserted VLAN TAG %04x TCI %04x\n",
		   __func__, ntohs(hdr->h_vlan_proto),
		   ntohs(hdr->h_vlan_TCI));

	return skb;
}

static struct sk_buff *ks8995_rcv(struct sk_buff *skb, struct net_device *dev)
{
	int portmask;
	int port;
	u16 etype;

	/* The special tag may be in the packet or VLAN metadata. In either
	 * case, its TPID must retain the source-port bits.
	 *
	 * Read the in-band TPID directly because skb->protocol contains
	 * ETH_P_XDSA.
	 */
	if (skb_vlan_tag_present(skb))
		etype = ntohs(skb->vlan_proto);
	else
		etype = ntohs(*(__be16 *)dsa_etype_header_pos_rx(skb));
	if ((etype & KS8995M_STPID_STD) != ETH_P_8021Q) {
		netdev_dbg(dev, "%s: dropped ethertype 0x%04x\n",
			   __func__, etype);
		kfree_skb(skb);
		return NULL;
	}

	portmask = FIELD_GET(KS8995M_STPID_PORTMASK, etype);
	if (unlikely(!is_power_of_2(portmask))) {
		netdev_dbg(dev, "%s: dropped invalid port mask 0x%04x\n",
			   __func__, portmask);
		kfree_skb(skb);
		return NULL;
	}
	port = ilog2(portmask);

	netdev_dbg(dev, "%s: received ethertype %04x\n",
		   __func__, etype);

	/* Move an in-band special tag into VLAN metadata. If already
	 * offloaded, its TPID was validated above.
	 */
	if (!skb_vlan_tag_present(skb)) {
		skb = skb_vlan_untag(skb);
		if (!skb) {
			/* skb_vlan_untag drops the skb on failure */
			if (net_ratelimit())
				netdev_err(dev, "%s: unable to untag skb\n", __func__);
			return NULL;
		}
	}

	netdev_dbg(dev, "%s: etype %04x portmask %04x (%d)\n",
		   __func__, etype, portmask, port);
	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev) {
		kfree_skb(skb);
		return NULL;
	}

	/* The special tag's TCI is now in the hardware-accelerated VLAN
	 * metadata. The switch preserves the TCI of tagged
	 * frames, but inserts the ingress port's default tag for untagged frames.
	 * Since the driver programs the default tag to 0, a zero TCI identifies
	 * an originally untagged frame. Preserve every non-zero TCI as an
	 * 802.1Q tag, including VID 0 frames with PCP or DEI set.
	 */
	if (!skb->vlan_tci) {
		netdev_dbg(dev, "%s: clear VLAN tag from frame\n", __func__);
		__vlan_hwaccel_clear_tag(skb);
	} else {
		skb->vlan_proto = htons(ETH_P_8021Q);
		netdev_dbg(dev, "%s: vlan_tci = 0x%04x VLAN frame\n",
			   __func__, skb->vlan_tci);
	}

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static void ks8995_flow_dissect(const struct sk_buff *skb, __be16 *proto,
				int *offset)
{
	const __be16 *encap_proto;
	__be16 buffer;

	*proto = 0;
	*offset = 0;

	/* Extra TX headroom does not increase the in-band RX tag length. */
	encap_proto = skb_header_pointer(skb, KS8995_TAG_LEN - sizeof(buffer),
					 sizeof(buffer), &buffer);
	if (!encap_proto)
		return;

	*proto = *encap_proto;
	*offset = KS8995_TAG_LEN;
}

static const struct dsa_device_ops ks8995_netdev_ops = {
	.name = KS8995_NAME,
	.proto	= DSA_TAG_PROTO_KS8995,
	.xmit = ks8995_xmit,
	.rcv = ks8995_rcv,
	.flow_dissect = ks8995_flow_dissect,
	.needed_headroom = KS8995_NEEDED_HEADROOM,
};

MODULE_DESCRIPTION("DSA tag driver for Micrel KS8995 family of switches");
MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_KS8995, KS8995_NAME);

module_dsa_tag_driver(ks8995_netdev_ops);
