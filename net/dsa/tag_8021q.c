// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 *
 * This module is not a complete tagger implementation. It only provides
 * primitives for taggers that rely on 802.1Q VLAN tags to use. The
 * dsa_8021q_netdev_ops is registered for API compliance and not used
 * directly by callers.
 */
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/dsa/8021q.h>

#include "dsa_priv.h"

/* Binary structure of the fake 12-bit VID field (when the TPID is
 * ETH_P_DSA_8021Q):
 *
 * | 11  | 10  |  9  |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
 * +-----------+-----+-----------------+-----------+-----------------------+
 * |    DIR    | SVL |    SWITCH_ID    |  SUBVLAN  |          PORT         |
 * +-----------+-----+-----------------+-----------+-----------------------+
 *
 * DIR - VID[11:10]:
 *	Direction flags.
 *	* 1 (0b01) for RX VLAN,
 *	* 2 (0b10) for TX VLAN.
 *	These values make the special VIDs of 0, 1 and 4095 to be left
 *	unused by this coding scheme.
 *
 * SVL/SUBVLAN - { VID[9], VID[5:4] }:
 *	Sub-VLAN encoding. Valid only when DIR indicates an RX VLAN.
 *	* 0 (0b000): Field does not encode a sub-VLAN, either because
 *	received traffic is untagged, PVID-tagged or because a second
 *	VLAN tag is present after this tag and not inside of it.
 *	* 1 (0b001): Received traffic is tagged with a VID value private
 *	to the host. This field encodes the index in the host's lookup
 *	table through which the value of the ingress VLAN ID can be
 *	recovered.
 *	* 2 (0b010): Field encodes a sub-VLAN.
 *	...
 *	* 7 (0b111): Field encodes a sub-VLAN.
 *	When DIR indicates a TX VLAN, SUBVLAN must be transmitted as zero
 *	(by the host) and ignored on receive (by the switch).
 *
 * SWITCH_ID - VID[8:6]:
 *	Index of switch within DSA tree. Must be between 0 and 7.
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

#define DSA_8021Q_SUBVLAN_HI_SHIFT	9
#define DSA_8021Q_SUBVLAN_HI_MASK	GENMASK(9, 9)
#define DSA_8021Q_SUBVLAN_LO_SHIFT	4
#define DSA_8021Q_SUBVLAN_LO_MASK	GENMASK(5, 4)
#define DSA_8021Q_SUBVLAN_HI(x)		(((x) & GENMASK(2, 2)) >> 2)
#define DSA_8021Q_SUBVLAN_LO(x)		((x) & GENMASK(1, 0))
#define DSA_8021Q_SUBVLAN(x)		\
		(((DSA_8021Q_SUBVLAN_LO(x) << DSA_8021Q_SUBVLAN_LO_SHIFT) & \
		  DSA_8021Q_SUBVLAN_LO_MASK) | \
		 ((DSA_8021Q_SUBVLAN_HI(x) << DSA_8021Q_SUBVLAN_HI_SHIFT) & \
		  DSA_8021Q_SUBVLAN_HI_MASK))

#define DSA_8021Q_PORT_SHIFT		0
#define DSA_8021Q_PORT_MASK		GENMASK(3, 0)
#define DSA_8021Q_PORT(x)		(((x) << DSA_8021Q_PORT_SHIFT) & \
						 DSA_8021Q_PORT_MASK)

/* Returns the VID to be inserted into the frame from xmit for switch steering
 * instructions on egress. Encodes switch ID and port ID.
 */
u16 dsa_8021q_tx_vid(struct dsa_switch *ds, int port)
{
	return DSA_8021Q_DIR_TX | DSA_8021Q_SWITCH_ID(ds->index) |
	       DSA_8021Q_PORT(port);
}
EXPORT_SYMBOL_GPL(dsa_8021q_tx_vid);

/* Returns the VID that will be installed as pvid for this switch port, sent as
 * tagged egress towards the CPU port and decoded by the rcv function.
 */
u16 dsa_8021q_rx_vid(struct dsa_switch *ds, int port)
{
	return DSA_8021Q_DIR_RX | DSA_8021Q_SWITCH_ID(ds->index) |
	       DSA_8021Q_PORT(port);
}
EXPORT_SYMBOL_GPL(dsa_8021q_rx_vid);

u16 dsa_8021q_rx_vid_subvlan(struct dsa_switch *ds, int port, u16 subvlan)
{
	return DSA_8021Q_DIR_RX | DSA_8021Q_SWITCH_ID(ds->index) |
	       DSA_8021Q_PORT(port) | DSA_8021Q_SUBVLAN(subvlan);
}
EXPORT_SYMBOL_GPL(dsa_8021q_rx_vid_subvlan);

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

/* Returns the decoded subvlan from the RX VID. */
u16 dsa_8021q_rx_subvlan(u16 vid)
{
	u16 svl_hi, svl_lo;

	svl_hi = (vid & DSA_8021Q_SUBVLAN_HI_MASK) >>
		 DSA_8021Q_SUBVLAN_HI_SHIFT;
	svl_lo = (vid & DSA_8021Q_SUBVLAN_LO_MASK) >>
		 DSA_8021Q_SUBVLAN_LO_SHIFT;

	return (svl_hi << 2) | svl_lo;
}
EXPORT_SYMBOL_GPL(dsa_8021q_rx_subvlan);

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

/* If @enabled is true, installs @vid with @flags into the switch port's HW
 * filter.
 * If @enabled is false, deletes @vid (ignores @flags) from the port. Had the
 * user explicitly configured this @vid through the bridge core, then the @vid
 * is installed again, but this time with the flags from the bridge layer.
 */
static int dsa_8021q_vid_apply(struct dsa_8021q_context *ctx, int port, u16 vid,
			       u16 flags, bool enabled)
{
	struct dsa_port *dp = dsa_to_port(ctx->ds, port);

	if (enabled)
		return ctx->ops->vlan_add(ctx->ds, dp->index, vid, flags);

	return ctx->ops->vlan_del(ctx->ds, dp->index, vid);
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
 *    By the way - just because we're installing the same VID in multiple
 *    switch ports doesn't mean that they'll start to talk to one another, even
 *    while not bridged: the final forwarding decision is still an AND between
 *    the L2 forwarding information (which is limiting forwarding in this case)
 *    and the VLAN-based restrictions (of which there are none in this case,
 *    since all ports are members).
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
 * the RX VID of all other front-panel ports, and one TX VID. Whereas the CPU
 * port will have the RX and TX VIDs of all front-panel ports, and on top of
 * that, is also tagged-input and tagged-output (VLAN trunk).
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
static int dsa_8021q_setup_port(struct dsa_8021q_context *ctx, int port,
				bool enabled)
{
	int upstream = dsa_upstream_port(ctx->ds, port);
	u16 rx_vid = dsa_8021q_rx_vid(ctx->ds, port);
	u16 tx_vid = dsa_8021q_tx_vid(ctx->ds, port);
	struct net_device *master;
	int i, err, subvlan;

	/* The CPU port is implicitly configured by
	 * configuring the front-panel ports
	 */
	if (!dsa_is_user_port(ctx->ds, port))
		return 0;

	master = dsa_to_port(ctx->ds, port)->cpu_dp->master;

	/* Add this user port's RX VID to the membership list of all others
	 * (including itself). This is so that bridging will not be hindered.
	 * L2 forwarding rules still take precedence when there are no VLAN
	 * restrictions, so there are no concerns about leaking traffic.
	 */
	for (i = 0; i < ctx->ds->num_ports; i++) {
		u16 flags;

		if (i == upstream)
			continue;
		else if (i == port)
			/* The RX VID is pvid on this port */
			flags = BRIDGE_VLAN_INFO_UNTAGGED |
				BRIDGE_VLAN_INFO_PVID;
		else
			/* The RX VID is a regular VLAN on all others */
			flags = BRIDGE_VLAN_INFO_UNTAGGED;

		err = dsa_8021q_vid_apply(ctx, i, rx_vid, flags, enabled);
		if (err) {
			dev_err(ctx->ds->dev,
				"Failed to apply RX VID %d to port %d: %d\n",
				rx_vid, port, err);
			return err;
		}
	}

	/* CPU port needs to see this port's RX VID
	 * as tagged egress.
	 */
	err = dsa_8021q_vid_apply(ctx, upstream, rx_vid, 0, enabled);
	if (err) {
		dev_err(ctx->ds->dev,
			"Failed to apply RX VID %d to port %d: %d\n",
			rx_vid, port, err);
		return err;
	}

	/* Add to the master's RX filter not only @rx_vid, but in fact
	 * the entire subvlan range, just in case this DSA switch might
	 * want to use sub-VLANs.
	 */
	for (subvlan = 0; subvlan < DSA_8021Q_N_SUBVLAN; subvlan++) {
		u16 vid = dsa_8021q_rx_vid_subvlan(ctx->ds, port, subvlan);

		if (enabled)
			vlan_vid_add(master, ctx->proto, vid);
		else
			vlan_vid_del(master, ctx->proto, vid);
	}

	/* Finally apply the TX VID on this port and on the CPU port */
	err = dsa_8021q_vid_apply(ctx, port, tx_vid, BRIDGE_VLAN_INFO_UNTAGGED,
				  enabled);
	if (err) {
		dev_err(ctx->ds->dev,
			"Failed to apply TX VID %d on port %d: %d\n",
			tx_vid, port, err);
		return err;
	}
	err = dsa_8021q_vid_apply(ctx, upstream, tx_vid, 0, enabled);
	if (err) {
		dev_err(ctx->ds->dev,
			"Failed to apply TX VID %d on port %d: %d\n",
			tx_vid, upstream, err);
		return err;
	}

	return err;
}

int dsa_8021q_setup(struct dsa_8021q_context *ctx, bool enabled)
{
	int rc, port;

	ASSERT_RTNL();

	for (port = 0; port < ctx->ds->num_ports; port++) {
		rc = dsa_8021q_setup_port(ctx, port, enabled);
		if (rc < 0) {
			dev_err(ctx->ds->dev,
				"Failed to setup VLAN tagging for port %d: %d\n",
				port, rc);
			return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_8021q_setup);

static int dsa_8021q_crosschip_link_apply(struct dsa_8021q_context *ctx,
					  int port,
					  struct dsa_8021q_context *other_ctx,
					  int other_port, bool enabled)
{
	u16 rx_vid = dsa_8021q_rx_vid(ctx->ds, port);

	/* @rx_vid of local @ds port @port goes to @other_port of
	 * @other_ds
	 */
	return dsa_8021q_vid_apply(other_ctx, other_port, rx_vid,
				   BRIDGE_VLAN_INFO_UNTAGGED, enabled);
}

static int dsa_8021q_crosschip_link_add(struct dsa_8021q_context *ctx, int port,
					struct dsa_8021q_context *other_ctx,
					int other_port)
{
	struct dsa_8021q_crosschip_link *c;

	list_for_each_entry(c, &ctx->crosschip_links, list) {
		if (c->port == port && c->other_ctx == other_ctx &&
		    c->other_port == other_port) {
			refcount_inc(&c->refcount);
			return 0;
		}
	}

	dev_dbg(ctx->ds->dev,
		"adding crosschip link from port %d to %s port %d\n",
		port, dev_name(other_ctx->ds->dev), other_port);

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->port = port;
	c->other_ctx = other_ctx;
	c->other_port = other_port;
	refcount_set(&c->refcount, 1);

	list_add(&c->list, &ctx->crosschip_links);

	return 0;
}

static void dsa_8021q_crosschip_link_del(struct dsa_8021q_context *ctx,
					 struct dsa_8021q_crosschip_link *c,
					 bool *keep)
{
	*keep = !refcount_dec_and_test(&c->refcount);

	if (*keep)
		return;

	dev_dbg(ctx->ds->dev,
		"deleting crosschip link from port %d to %s port %d\n",
		c->port, dev_name(c->other_ctx->ds->dev), c->other_port);

	list_del(&c->list);
	kfree(c);
}

/* Make traffic from local port @port be received by remote port @other_port.
 * This means that our @rx_vid needs to be installed on @other_ds's upstream
 * and user ports. The user ports should be egress-untagged so that they can
 * pop the dsa_8021q VLAN. But the @other_upstream can be either egress-tagged
 * or untagged: it doesn't matter, since it should never egress a frame having
 * our @rx_vid.
 */
int dsa_8021q_crosschip_bridge_join(struct dsa_8021q_context *ctx, int port,
				    struct dsa_8021q_context *other_ctx,
				    int other_port)
{
	/* @other_upstream is how @other_ds reaches us. If we are part
	 * of disjoint trees, then we are probably connected through
	 * our CPU ports. If we're part of the same tree though, we should
	 * probably use dsa_towards_port.
	 */
	int other_upstream = dsa_upstream_port(other_ctx->ds, other_port);
	int rc;

	rc = dsa_8021q_crosschip_link_add(ctx, port, other_ctx, other_port);
	if (rc)
		return rc;

	rc = dsa_8021q_crosschip_link_apply(ctx, port, other_ctx,
					    other_port, true);
	if (rc)
		return rc;

	rc = dsa_8021q_crosschip_link_add(ctx, port, other_ctx, other_upstream);
	if (rc)
		return rc;

	return dsa_8021q_crosschip_link_apply(ctx, port, other_ctx,
					      other_upstream, true);
}
EXPORT_SYMBOL_GPL(dsa_8021q_crosschip_bridge_join);

int dsa_8021q_crosschip_bridge_leave(struct dsa_8021q_context *ctx, int port,
				     struct dsa_8021q_context *other_ctx,
				     int other_port)
{
	int other_upstream = dsa_upstream_port(other_ctx->ds, other_port);
	struct dsa_8021q_crosschip_link *c, *n;

	list_for_each_entry_safe(c, n, &ctx->crosschip_links, list) {
		if (c->port == port && c->other_ctx == other_ctx &&
		    (c->other_port == other_port ||
		     c->other_port == other_upstream)) {
			struct dsa_8021q_context *other_ctx = c->other_ctx;
			int other_port = c->other_port;
			bool keep;
			int rc;

			dsa_8021q_crosschip_link_del(ctx, c, &keep);
			if (keep)
				continue;

			rc = dsa_8021q_crosschip_link_apply(ctx, port,
							    other_ctx,
							    other_port,
							    false);
			if (rc)
				return rc;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_8021q_crosschip_bridge_leave);

struct sk_buff *dsa_8021q_xmit(struct sk_buff *skb, struct net_device *netdev,
			       u16 tpid, u16 tci)
{
	/* skb->data points at skb_mac_header, which
	 * is fine for vlan_insert_tag.
	 */
	return vlan_insert_tag(skb, htons(tpid), tci);
}
EXPORT_SYMBOL_GPL(dsa_8021q_xmit);

void dsa_8021q_rcv(struct sk_buff *skb, int *source_port, int *switch_id,
		   int *subvlan)
{
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

	*source_port = dsa_8021q_rx_source_port(vid);
	*switch_id = dsa_8021q_rx_switch_id(vid);
	*subvlan = dsa_8021q_rx_subvlan(vid);
	skb->priority = (tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
}
EXPORT_SYMBOL_GPL(dsa_8021q_rcv);

MODULE_LICENSE("GPL v2");
