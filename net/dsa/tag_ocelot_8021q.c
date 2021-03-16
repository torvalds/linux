// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020-2021 NXP Semiconductors
 *
 * An implementation of the software-defined tag_8021q.c tagger format, which
 * also preserves full functionality under a vlan_filtering bridge. It does
 * this by using the TCAM engines for:
 * - pushing the RX VLAN as a second, outer tag, on egress towards the CPU port
 * - redirecting towards the correct front port based on TX VLAN and popping
 *   that on egress
 */
#include <linux/dsa/8021q.h>
#include <soc/mscc/ocelot.h>
#include <soc/mscc/ocelot_ptp.h>
#include "dsa_priv.h"

static struct sk_buff *ocelot_xmit_ptp(struct dsa_port *dp,
				       struct sk_buff *skb,
				       struct sk_buff *clone)
{
	struct ocelot *ocelot = dp->ds->priv;
	struct ocelot_port *ocelot_port;
	int port = dp->index;
	u32 rew_op;

	if (!ocelot_can_inject(ocelot, 0))
		return NULL;

	ocelot_port = ocelot->ports[port];
	rew_op = ocelot_port->ptp_cmd;

	/* Retrieve timestamp ID populated inside skb->cb[0] of the
	 * clone by ocelot_port_add_txtstamp_skb
	 */
	if (ocelot_port->ptp_cmd == IFH_REW_OP_TWO_STEP_PTP)
		rew_op |= clone->cb[0] << 3;

	ocelot_port_inject_frame(ocelot, port, 0, rew_op, skb);

	return NULL;
}

static struct sk_buff *ocelot_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct dsa_port *dp = dsa_slave_to_port(netdev);
	u16 tx_vid = dsa_8021q_tx_vid(dp->ds, dp->index);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 pcp = netdev_txq_to_tc(netdev, queue_mapping);
	struct sk_buff *clone = DSA_SKB_CB(skb)->clone;

	/* TX timestamping was requested, so inject through MMIO */
	if (clone)
		return ocelot_xmit_ptp(dp, skb, clone);

	return dsa_8021q_xmit(skb, netdev, ETH_P_8021Q,
			      ((pcp << VLAN_PRIO_SHIFT) | tx_vid));
}

static struct sk_buff *ocelot_rcv(struct sk_buff *skb,
				  struct net_device *netdev,
				  struct packet_type *pt)
{
	int src_port, switch_id, qos_class;
	u16 vid, tci;

	skb_push_rcsum(skb, ETH_HLEN);
	if (skb_vlan_tag_present(skb)) {
		tci = skb_vlan_tag_get(skb);
		__vlan_hwaccel_clear_tag(skb);
	} else {
		__skb_vlan_pop(skb, &tci);
	}
	skb_pull_rcsum(skb, ETH_HLEN);

	vid = tci & VLAN_VID_MASK;
	src_port = dsa_8021q_rx_source_port(vid);
	switch_id = dsa_8021q_rx_switch_id(vid);
	qos_class = (tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;

	skb->dev = dsa_master_find_slave(netdev, switch_id, src_port);
	if (!skb->dev)
		return NULL;

	skb->offload_fwd_mark = 1;
	skb->priority = qos_class;

	return skb;
}

static const struct dsa_device_ops ocelot_8021q_netdev_ops = {
	.name			= "ocelot-8021q",
	.proto			= DSA_TAG_PROTO_OCELOT_8021Q,
	.xmit			= ocelot_xmit,
	.rcv			= ocelot_rcv,
	.overhead		= VLAN_HLEN,
	.promisc_on_master	= true,
};

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_OCELOT_8021Q);

module_dsa_tag_driver(ocelot_8021q_netdev_ops);
