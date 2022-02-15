// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 *
 * This module is not a complete tagger implementation. It only provides
 * primitives for taggers that rely on 802.1Q VLAN tags to use. The
 * dsa_8021q_netdev_ops is registered for API compliance and not used
 * directly by callers.
 */
#include <linux/if_vlan.h>
#include <linux/dsa/8021q.h>

#include "dsa_priv.h"

/* Binary structure of the fake 12-bit VID field (when the TPID is
 * ETH_P_DSA_8021Q):
 *
 * | 11  | 10  |  9  |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
 * +-----------+-----+-----------------+-----------+-----------------------+
 * |    DIR    | VBID|    SWITCH_ID    |   VBID    |          PORT         |
 * +-----------+-----+-----------------+-----------+-----------------------+
 *
 * DIR - VID[11:10]:
 *	Direction flags.
 *	* 1 (0b01) for RX VLAN,
 *	* 2 (0b10) for TX VLAN.
 *	These values make the special VIDs of 0, 1 and 4095 to be left
 *	unused by this coding scheme.
 *
 * SWITCH_ID - VID[8:6]:
 *	Index of switch within DSA tree. Must be between 0 and 7.
 *
 * VBID - { VID[9], VID[5:4] }:
 *	Virtual bridge ID. If between 1 and 7, packet targets the broadcast
 *	domain of a bridge. If transmitted as zero, packet targets a single
 *	port. Field only valid on transmit, must be ignored on receive.
 *
 * PORT - VID[3:0]:
 *	Index of switch port. Must be between 0 and 15.
 */

#define DSA_8021Q_DIR_SHIFT		10
#define DSA_8021Q_DIR_MASK		GENMASK(11, 10)
#define DSA_8021Q_DIR(x)		(((x) << DSA_8021Q_DIR_SHIFT) & \
						 DSA_8021Q_DIR_MASK)
#define DSA_8021Q_DIR_RX		DSA_8021Q_DIR(1)
#define DSA_8021Q_DIR_TX		DSA_8021Q_DIR(2)

#define DSA_8021Q_SWITCH_ID_SHIFT	6
#define DSA_8021Q_SWITCH_ID_MASK	GENMASK(8, 6)
#define DSA_8021Q_SWITCH_ID(x)		(((x) << DSA_8021Q_SWITCH_ID_SHIFT) & \
						 DSA_8021Q_SWITCH_ID_MASK)

#define DSA_8021Q_VBID_HI_SHIFT		9
#define DSA_8021Q_VBID_HI_MASK		GENMASK(9, 9)
#define DSA_8021Q_VBID_LO_SHIFT		4
#define DSA_8021Q_VBID_LO_MASK		GENMASK(5, 4)
#define DSA_8021Q_VBID_HI(x)		(((x) & GENMASK(2, 2)) >> 2)
#define DSA_8021Q_VBID_LO(x)		((x) & GENMASK(1, 0))
#define DSA_8021Q_VBID(x)		\
		(((DSA_8021Q_VBID_LO(x) << DSA_8021Q_VBID_LO_SHIFT) & \
		  DSA_8021Q_VBID_LO_MASK) | \
		 ((DSA_8021Q_VBID_HI(x) << DSA_8021Q_VBID_HI_SHIFT) & \
		  DSA_8021Q_VBID_HI_MASK))

#define DSA_8021Q_PORT_SHIFT		0
#define DSA_8021Q_PORT_MASK		GENMASK(3, 0)
#define DSA_8021Q_PORT(x)		(((x) << DSA_8021Q_PORT_SHIFT) & \
						 DSA_8021Q_PORT_MASK)

u16 dsa_8021q_bridge_tx_fwd_offload_vid(unsigned int bridge_num)
{
	/* The VBID value of 0 is reserved for precise TX, but it is also
	 * reserved/invalid for the bridge_num, so all is well.
	 */
	return DSA_8021Q_DIR_TX | DSA_8021Q_VBID(bridge_num);
}
EXPORT_SYMBOL_GPL(dsa_8021q_bridge_tx_fwd_offload_vid);

/* Returns the VID to be inserted into the frame from xmit for switch steering
 * instructions on egress. Encodes switch ID and port ID.
 */
u16 dsa_tag_8021q_tx_vid(const struct dsa_port *dp)
{
	return DSA_8021Q_DIR_TX | DSA_8021Q_SWITCH_ID(dp->ds->index) |
	       DSA_8021Q_PORT(dp->index);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_tx_vid);

/* Returns the VID that will be installed as pvid for this switch port, sent as
 * tagged egress towards the CPU port and decoded by the rcv function.
 */
u16 dsa_tag_8021q_rx_vid(const struct dsa_port *dp)
{
	return DSA_8021Q_DIR_RX | DSA_8021Q_SWITCH_ID(dp->ds->index) |
	       DSA_8021Q_PORT(dp->index);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_rx_vid);

/* Returns the decoded switch ID from the RX VID. */
int dsa_8021q_rx_switch_id(u16 vid)
{
	return (vid & DSA_8021Q_SWITCH_ID_MASK) >> DSA_8021Q_SWITCH_ID_SHIFT;
}
EXPORT_SYMBOL_GPL(dsa_8021q_rx_switch_id);

/* Returns the decoded port ID from the RX VID. */
int dsa_8021q_rx_source_port(u16 vid)
{
	return (vid & DSA_8021Q_PORT_MASK) >> DSA_8021Q_PORT_SHIFT;
}
EXPORT_SYMBOL_GPL(dsa_8021q_rx_source_port);

bool vid_is_dsa_8021q_rxvlan(u16 vid)
{
	return (vid & DSA_8021Q_DIR_MASK) == DSA_8021Q_DIR_RX;
}
EXPORT_SYMBOL_GPL(vid_is_dsa_8021q_rxvlan);

bool vid_is_dsa_8021q_txvlan(u16 vid)
{
	return (vid & DSA_8021Q_DIR_MASK) == DSA_8021Q_DIR_TX;
}
EXPORT_SYMBOL_GPL(vid_is_dsa_8021q_txvlan);

bool vid_is_dsa_8021q(u16 vid)
{
	return vid_is_dsa_8021q_rxvlan(vid) || vid_is_dsa_8021q_txvlan(vid);
}
EXPORT_SYMBOL_GPL(vid_is_dsa_8021q);

static struct dsa_tag_8021q_vlan *
dsa_tag_8021q_vlan_find(struct dsa_8021q_context *ctx, int port, u16 vid)
{
	struct dsa_tag_8021q_vlan *v;

	list_for_each_entry(v, &ctx->vlans, list)
		if (v->vid == vid && v->port == port)
			return v;

	return NULL;
}

static int dsa_port_do_tag_8021q_vlan_add(struct dsa_port *dp, u16 vid,
					  u16 flags)
{
	struct dsa_8021q_context *ctx = dp->ds->tag_8021q_ctx;
	struct dsa_switch *ds = dp->ds;
	struct dsa_tag_8021q_vlan *v;
	int port = dp->index;
	int err;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->tag_8021q_vlan_add(ds, port, vid, flags);

	v = dsa_tag_8021q_vlan_find(ctx, port, vid);
	if (v) {
		refcount_inc(&v->refcount);
		return 0;
	}

	v = kzalloc(sizeof(*v), GFP_KERNEL);
	if (!v)
		return -ENOMEM;

	err = ds->ops->tag_8021q_vlan_add(ds, port, vid, flags);
	if (err) {
		kfree(v);
		return err;
	}

	v->vid = vid;
	v->port = port;
	refcount_set(&v->refcount, 1);
	list_add_tail(&v->list, &ctx->vlans);

	return 0;
}

static int dsa_port_do_tag_8021q_vlan_del(struct dsa_port *dp, u16 vid)
{
	struct dsa_8021q_context *ctx = dp->ds->tag_8021q_ctx;
	struct dsa_switch *ds = dp->ds;
	struct dsa_tag_8021q_vlan *v;
	int port = dp->index;
	int err;

	/* No need to bother with refcounting for user ports */
	if (!(dsa_port_is_cpu(dp) || dsa_port_is_dsa(dp)))
		return ds->ops->tag_8021q_vlan_del(ds, port, vid);

	v = dsa_tag_8021q_vlan_find(ctx, port, vid);
	if (!v)
		return -ENOENT;

	if (!refcount_dec_and_test(&v->refcount))
		return 0;

	err = ds->ops->tag_8021q_vlan_del(ds, port, vid);
	if (err) {
		refcount_inc(&v->refcount);
		return err;
	}

	list_del(&v->list);
	kfree(v);

	return 0;
}

static bool
dsa_port_tag_8021q_vlan_match(struct dsa_port *dp,
			      struct dsa_notifier_tag_8021q_vlan_info *info)
{
	struct dsa_switch *ds = dp->ds;

	if (dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp))
		return true;

	if (ds->dst->index == info->tree_index && ds->index == info->sw_index)
		return dp->index == info->port;

	return false;
}

int dsa_switch_tag_8021q_vlan_add(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info)
{
	struct dsa_port *dp;
	int err;

	/* Since we use dsa_broadcast(), there might be other switches in other
	 * trees which don't support tag_8021q, so don't return an error.
	 * Or they might even support tag_8021q but have not registered yet to
	 * use it (maybe they use another tagger currently).
	 */
	if (!ds->ops->tag_8021q_vlan_add || !ds->tag_8021q_ctx)
		return 0;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_tag_8021q_vlan_match(dp, info)) {
			u16 flags = 0;

			if (dsa_port_is_user(dp))
				flags |= BRIDGE_VLAN_INFO_UNTAGGED;

			if (vid_is_dsa_8021q_rxvlan(info->vid) &&
			    dsa_8021q_rx_switch_id(info->vid) == ds->index &&
			    dsa_8021q_rx_source_port(info->vid) == dp->index)
				flags |= BRIDGE_VLAN_INFO_PVID;

			err = dsa_port_do_tag_8021q_vlan_add(dp, info->vid,
							     flags);
			if (err)
				return err;
		}
	}

	return 0;
}

int dsa_switch_tag_8021q_vlan_del(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info)
{
	struct dsa_port *dp;
	int err;

	if (!ds->ops->tag_8021q_vlan_del || !ds->tag_8021q_ctx)
		return 0;

	dsa_switch_for_each_port(dp, ds) {
		if (dsa_port_tag_8021q_vlan_match(dp, info)) {
			err = dsa_port_do_tag_8021q_vlan_del(dp, info->vid);
			if (err)
				return err;
		}
	}

	return 0;
}

/* RX VLAN tagging (left) and TX VLAN tagging (right) setup shown for a single
 * front-panel switch port (here swp0).
 *
 * Port identification through VLAN (802.1Q) tags has different requirements
 * for it to work effectively:
 *  - On RX (ingress from network): each front-panel port must have a pvid
 *    that uniquely identifies it, and the egress of this pvid must be tagged
 *    towards the CPU port, so that software can recover the source port based
 *    on the VID in the frame. But this would only work for standalone ports;
 *    if bridged, this VLAN setup would break autonomous forwarding and would
 *    force all switched traffic to pass through the CPU. So we must also make
 *    the other front-panel ports members of this VID we're adding, albeit
 *    we're not making it their PVID (they'll still have their own).
 *  - On TX (ingress from CPU and towards network) we are faced with a problem.
 *    If we were to tag traffic (from within DSA) with the port's pvid, all
 *    would be well, assuming the switch ports were standalone. Frames would
 *    have no choice but to be directed towards the correct front-panel port.
 *    But because we also want the RX VLAN to not break bridging, then
 *    inevitably that means that we have to give them a choice (of what
 *    front-panel port to go out on), and therefore we cannot steer traffic
 *    based on the RX VID. So what we do is simply install one more VID on the
 *    front-panel and CPU ports, and profit off of the fact that steering will
 *    work just by virtue of the fact that there is only one other port that's
 *    a member of the VID we're tagging the traffic with - the desired one.
 *
 * So at the end, each front-panel port will have one RX VID (also the PVID),
 * the RX VID of all other front-panel ports that are in the same bridge, and
 * one TX VID. Whereas the CPU port will have the RX and TX VIDs of all
 * front-panel ports, and on top of that, is also tagged-input and
 * tagged-output (VLAN trunk).
 *
 *               CPU port                               CPU port
 * +-------------+-----+-------------+    +-------------+-----+-------------+
 * |  RX VID     |     |             |    |  TX VID     |     |             |
 * |  of swp0    |     |             |    |  of swp0    |     |             |
 * |             +-----+             |    |             +-----+             |
 * |                ^ T              |    |                | Tagged         |
 * |                |                |    |                | ingress        |
 * |    +-------+---+---+-------+    |    |    +-----------+                |
 * |    |       |       |       |    |    |    | Untagged                   |
 * |    |     U v     U v     U v    |    |    v egress                     |
 * | +-----+ +-----+ +-----+ +-----+ |    | +-----+ +-----+ +-----+ +-----+ |
 * | |     | |     | |     | |     | |    | |     | |     | |     | |     | |
 * | |PVID | |     | |     | |     | |    | |     | |     | |     | |     | |
 * +-+-----+-+-----+-+-----+-+-----+-+    +-+-----+-+-----+-+-----+-+-----+-+
 *   swp0    swp1    swp2    swp3           swp0    swp1    swp2    swp3
 */
static bool
dsa_port_tag_8021q_bridge_match(struct dsa_port *dp,
				struct dsa_notifier_bridge_info *info)
{
	/* Don't match on self */
	if (dp->ds->dst->index == info->tree_index &&
	    dp->ds->index == info->sw_index &&
	    dp->index == info->port)
		return false;

	if (dsa_port_is_user(dp))
		return dsa_port_offloads_bridge(dp, &info->bridge);

	return false;
}

int dsa_tag_8021q_bridge_join(struct dsa_switch *ds,
			      struct dsa_notifier_bridge_info *info)
{
	struct dsa_switch *targeted_ds;
	struct dsa_port *targeted_dp;
	struct dsa_port *dp;
	u16 targeted_rx_vid;
	int err;

	if (!ds->tag_8021q_ctx)
		return 0;

	targeted_ds = dsa_switch_find(info->tree_index, info->sw_index);
	targeted_dp = dsa_to_port(targeted_ds, info->port);
	targeted_rx_vid = dsa_tag_8021q_rx_vid(targeted_dp);

	dsa_switch_for_each_port(dp, ds) {
		u16 rx_vid = dsa_tag_8021q_rx_vid(dp);

		if (!dsa_port_tag_8021q_bridge_match(dp, info))
			continue;

		/* Install the RX VID of the targeted port in our VLAN table */
		err = dsa_port_tag_8021q_vlan_add(dp, targeted_rx_vid, true);
		if (err)
			return err;

		/* Install our RX VID into the targeted port's VLAN table */
		err = dsa_port_tag_8021q_vlan_add(targeted_dp, rx_vid, true);
		if (err)
			return err;
	}

	return 0;
}

int dsa_tag_8021q_bridge_leave(struct dsa_switch *ds,
			       struct dsa_notifier_bridge_info *info)
{
	struct dsa_switch *targeted_ds;
	struct dsa_port *targeted_dp;
	struct dsa_port *dp;
	u16 targeted_rx_vid;

	if (!ds->tag_8021q_ctx)
		return 0;

	targeted_ds = dsa_switch_find(info->tree_index, info->sw_index);
	targeted_dp = dsa_to_port(targeted_ds, info->port);
	targeted_rx_vid = dsa_tag_8021q_rx_vid(targeted_dp);

	dsa_switch_for_each_port(dp, ds) {
		u16 rx_vid = dsa_tag_8021q_rx_vid(dp);

		if (!dsa_port_tag_8021q_bridge_match(dp, info))
			continue;

		/* Remove the RX VID of the targeted port from our VLAN table */
		dsa_port_tag_8021q_vlan_del(dp, targeted_rx_vid, true);

		/* Remove our RX VID from the targeted port's VLAN table */
		dsa_port_tag_8021q_vlan_del(targeted_dp, rx_vid, true);
	}

	return 0;
}

int dsa_tag_8021q_bridge_tx_fwd_offload(struct dsa_switch *ds, int port,
					struct dsa_bridge bridge)
{
	u16 tx_vid = dsa_8021q_bridge_tx_fwd_offload_vid(bridge.num);

	return dsa_port_tag_8021q_vlan_add(dsa_to_port(ds, port), tx_vid,
					   true);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_bridge_tx_fwd_offload);

void dsa_tag_8021q_bridge_tx_fwd_unoffload(struct dsa_switch *ds, int port,
					   struct dsa_bridge bridge)
{
	u16 tx_vid = dsa_8021q_bridge_tx_fwd_offload_vid(bridge.num);

	dsa_port_tag_8021q_vlan_del(dsa_to_port(ds, port), tx_vid, true);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_bridge_tx_fwd_unoffload);

/* Set up a port's tag_8021q RX and TX VLAN for standalone mode operation */
static int dsa_tag_8021q_port_setup(struct dsa_switch *ds, int port)
{
	struct dsa_8021q_context *ctx = ds->tag_8021q_ctx;
	struct dsa_port *dp = dsa_to_port(ds, port);
	u16 rx_vid = dsa_tag_8021q_rx_vid(dp);
	u16 tx_vid = dsa_tag_8021q_tx_vid(dp);
	struct net_device *master;
	int err;

	/* The CPU port is implicitly configured by
	 * configuring the front-panel ports
	 */
	if (!dsa_port_is_user(dp))
		return 0;

	master = dp->cpu_dp->master;

	/* Add this user port's RX VID to the membership list of all others
	 * (including itself). This is so that bridging will not be hindered.
	 * L2 forwarding rules still take precedence when there are no VLAN
	 * restrictions, so there are no concerns about leaking traffic.
	 */
	err = dsa_port_tag_8021q_vlan_add(dp, rx_vid, false);
	if (err) {
		dev_err(ds->dev,
			"Failed to apply RX VID %d to port %d: %pe\n",
			rx_vid, port, ERR_PTR(err));
		return err;
	}

	/* Add @rx_vid to the master's RX filter. */
	vlan_vid_add(master, ctx->proto, rx_vid);

	/* Finally apply the TX VID on this port and on the CPU port */
	err = dsa_port_tag_8021q_vlan_add(dp, tx_vid, false);
	if (err) {
		dev_err(ds->dev,
			"Failed to apply TX VID %d on port %d: %pe\n",
			tx_vid, port, ERR_PTR(err));
		return err;
	}

	return err;
}

static void dsa_tag_8021q_port_teardown(struct dsa_switch *ds, int port)
{
	struct dsa_8021q_context *ctx = ds->tag_8021q_ctx;
	struct dsa_port *dp = dsa_to_port(ds, port);
	u16 rx_vid = dsa_tag_8021q_rx_vid(dp);
	u16 tx_vid = dsa_tag_8021q_tx_vid(dp);
	struct net_device *master;

	/* The CPU port is implicitly configured by
	 * configuring the front-panel ports
	 */
	if (!dsa_port_is_user(dp))
		return;

	master = dp->cpu_dp->master;

	dsa_port_tag_8021q_vlan_del(dp, rx_vid, false);

	vlan_vid_del(master, ctx->proto, rx_vid);

	dsa_port_tag_8021q_vlan_del(dp, tx_vid, false);
}

static int dsa_tag_8021q_setup(struct dsa_switch *ds)
{
	int err, port;

	ASSERT_RTNL();

	for (port = 0; port < ds->num_ports; port++) {
		err = dsa_tag_8021q_port_setup(ds, port);
		if (err < 0) {
			dev_err(ds->dev,
				"Failed to setup VLAN tagging for port %d: %pe\n",
				port, ERR_PTR(err));
			return err;
		}
	}

	return 0;
}

static void dsa_tag_8021q_teardown(struct dsa_switch *ds)
{
	int port;

	ASSERT_RTNL();

	for (port = 0; port < ds->num_ports; port++)
		dsa_tag_8021q_port_teardown(ds, port);
}

int dsa_tag_8021q_register(struct dsa_switch *ds, __be16 proto)
{
	struct dsa_8021q_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->proto = proto;
	ctx->ds = ds;

	INIT_LIST_HEAD(&ctx->vlans);

	ds->tag_8021q_ctx = ctx;

	return dsa_tag_8021q_setup(ds);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_register);

void dsa_tag_8021q_unregister(struct dsa_switch *ds)
{
	struct dsa_8021q_context *ctx = ds->tag_8021q_ctx;
	struct dsa_tag_8021q_vlan *v, *n;

	dsa_tag_8021q_teardown(ds);

	list_for_each_entry_safe(v, n, &ctx->vlans, list) {
		list_del(&v->list);
		kfree(v);
	}

	ds->tag_8021q_ctx = NULL;

	kfree(ctx);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_unregister);

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci)
{
	/* skb->data points at skb_mac_header, which
	 * is fine for vlan_insert_tag.
	 */
	return vlan_insert_tag(skb, htons(tpid), tci);
}
EXPORT_SYMBOL_GPL(dsa_8021q_xmit);

void dsa_8021q_rcv(struct sk_buff *skb, int *source_port, int *switch_id)
{
	u16 vid, tci;

	if (skb_vlan_tag_present(skb)) {
		tci = skb_vlan_tag_get(skb);
		__vlan_hwaccel_clear_tag(skb);
	} else {
		skb_push_rcsum(skb, ETH_HLEN);
		__skb_vlan_pop(skb, &tci);
		skb_pull_rcsum(skb, ETH_HLEN);
	}

	vid = tci & VLAN_VID_MASK;

	*source_port = dsa_8021q_rx_source_port(vid);
	*switch_id = dsa_8021q_rx_switch_id(vid);
	skb->priority = (tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
}
EXPORT_SYMBOL_GPL(dsa_8021q_rcv);
