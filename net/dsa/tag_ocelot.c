// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019 NXP Semiconductors
 */
#include <linux/dsa/ocelot.h>
#include <soc/mscc/ocelot.h>
#include "dsa_priv.h"

static void ocelot_xmit_ptp(struct dsa_port *dp, void *injection,
			    struct sk_buff *clone)
{
	struct ocelot *ocelot = dp->ds->priv;
	struct ocelot_port *ocelot_port;
	u64 rew_op;

	ocelot_port = ocelot->ports[dp->index];
	rew_op = ocelot_port->ptp_cmd;

	/* Retrieve timestamp ID populated inside skb->cb[0] of the
	 * clone by ocelot_port_add_txtstamp_skb
	 */
	if (ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP)
		rew_op |= clone->cb[0] << 3;

	ocelot_ifh_set_rew_op(injection, rew_op);
}

static struct sk_buff *ocelot_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct dsa_port *dp = dsa_slave_to_port(netdev);
	struct sk_buff *clone = DSA_SKB_CB(skb)->clone;
	struct dsa_switch *ds = dp->ds;
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port;
	u8 *prefix, *injection;

	ocelot_port = ocelot->ports[dp->index];

	injection = skb_push(skb, OCELOT_TAG_LEN);

	prefix = skb_push(skb, OCELOT_SHORT_PREFIX_LEN);

	memcpy(prefix, ocelot_port->xmit_template, OCELOT_TOTAL_TAG_LEN);

	/* Fix up the fields which are not statically determined
	 * in the template
	 */
	ocelot_ifh_set_qos_class(injection, skb->priority);

	/* TX timestamping was requested */
	if (clone)
		ocelot_xmit_ptp(dp, injection, clone);

	return skb;
}

static struct sk_buff *ocelot_rcv(struct sk_buff *skb,
				  struct net_device *netdev,
				  struct packet_type *pt)
{
	u64 src_port, qos_class;
	u64 vlan_tci, tag_type;
	u8 *start = skb->data;
	struct dsa_port *dp;
	u8 *extraction;
	u16 vlan_tpid;

	/* Revert skb->data by the amount consumed by the DSA master,
	 * so it points to the beginning of the frame.
	 */
	skb_push(skb, ETH_HLEN);
	/* We don't care about the short prefix, it is just for easy entrance
	 * into the DSA master's RX filter. Discard it now by moving it into
	 * the headroom.
	 */
	skb_pull(skb, OCELOT_SHORT_PREFIX_LEN);
	/* And skb->data now points to the extraction frame header.
	 * Keep a pointer to it.
	 */
	extraction = skb->data;
	/* Now the EFH is part of the headroom as well */
	skb_pull(skb, OCELOT_TAG_LEN);
	/* Reset the pointer to the real MAC header */
	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);
	/* And move skb->data to the correct location again */
	skb_pull(skb, ETH_HLEN);

	/* Remove from inet csum the extraction header */
	skb_postpull_rcsum(skb, start, OCELOT_TOTAL_TAG_LEN);

	ocelot_xfh_get_src_port(extraction, &src_port);
	ocelot_xfh_get_qos_class(extraction, &qos_class);
	ocelot_xfh_get_tag_type(extraction, &tag_type);
	ocelot_xfh_get_vlan_tci(extraction, &vlan_tci);

	skb->dev = dsa_master_find_slave(netdev, 0, src_port);
	if (!skb->dev)
		/* The switch will reflect back some frames sent through
		 * sockets opened on the bare DSA master. These will come back
		 * with src_port equal to the index of the CPU port, for which
		 * there is no slave registered. So don't print any error
		 * message here (ignore and drop those frames).
		 */
		return NULL;

	skb->offload_fwd_mark = 1;
	skb->priority = qos_class;

	/* Ocelot switches copy frames unmodified to the CPU. However, it is
	 * possible for the user to request a VLAN modification through
	 * VCAP_IS1_ACT_VID_REPLACE_ENA. In this case, what will happen is that
	 * the VLAN ID field from the Extraction Header gets updated, but the
	 * 802.1Q header does not (the classified VLAN only becomes visible on
	 * egress through the "port tag" of front-panel ports).
	 * So, for traffic extracted by the CPU, we want to pick up the
	 * classified VLAN and manually replace the existing 802.1Q header from
	 * the packet with it, so that the operating system is always up to
	 * date with the result of tc-vlan actions.
	 * NOTE: In VLAN-unaware mode, we don't want to do that, we want the
	 * frame to remain unmodified, because the classified VLAN is always
	 * equal to the pvid of the ingress port and should not be used for
	 * processing.
	 */
	dp = dsa_slave_to_port(skb->dev);
	vlan_tpid = tag_type ? ETH_P_8021AD : ETH_P_8021Q;

	if (dsa_port_is_vlan_filtering(dp) &&
	    eth_hdr(skb)->h_proto == htons(vlan_tpid)) {
		u16 dummy_vlan_tci;

		skb_push_rcsum(skb, ETH_HLEN);
		__skb_vlan_pop(skb, &dummy_vlan_tci);
		skb_pull_rcsum(skb, ETH_HLEN);
		__vlan_hwaccel_put_tag(skb, htons(vlan_tpid), vlan_tci);
	}

	return skb;
}

static const struct dsa_device_ops ocelot_netdev_ops = {
	.name			= "ocelot",
	.proto			= DSA_TAG_PROTO_OCELOT,
	.xmit			= ocelot_xmit,
	.rcv			= ocelot_rcv,
	.overhead		= OCELOT_TOTAL_TAG_LEN,
	.promisc_on_master	= true,
};

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_OCELOT);

module_dsa_tag_driver(ocelot_netdev_ops);
