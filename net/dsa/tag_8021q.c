// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Vladimir Oltean <olteanv@gmail.com>
 *
 * This module is not a complete tagger implementation. It only provides
 * primitives for taggers that rely on 802.1Q VLAN tags to use.
 */
#include <linux/if_vlan.h>
#include <linux/dsa/8021q.h>

#include "dsa_priv.h"

/* Binary structure of the fake 12-bit VID field (when the TPID is
 * ETH_P_DSA_8021Q):
 *
 * | 11  | 10  |  9  |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
 * +-----------+-----+-----------------+-----------+-----------------------+
 * |    RSV    | VBID|    SWITCH_ID    |   VBID    |          PORT         |
 * +-----------+-----+-----------------+-----------+-----------------------+
 *
 * RSV - VID[11:10]:
 *	Reserved. Must be set to 3 (0b11).
 *
 * SWITCH_ID - VID[8:6]:
 *	Index of switch within DSA tree. Must be between 0 and 7.
 *
 * VBID - { VID[9], VID[5:4] }:
 *	Virtual bridge ID. If between 1 and 7, packet targets the broadcast
 *	domain of a bridge. If transmitted as zero, packet targets a single
 *	port.
 *
 * PORT - VID[3:0]:
 *	Index of switch port. Must be between 0 and 15.
 */

#define DSA_8021Q_RSV_VAL		3
#define DSA_8021Q_RSV_SHIFT		10
#define DSA_8021Q_RSV_MASK		GENMASK(11, 10)
#define DSA_8021Q_RSV			((DSA_8021Q_RSV_VAL << DSA_8021Q_RSV_SHIFT) & \
							       DSA_8021Q_RSV_MASK)

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

u16 dsa_tag_8021q_bridge_vid(unsigned int bridge_num)
{
	/* The VBID value of 0 is reserved for precise TX, but it is also
	 * reserved/invalid for the bridge_num, so all is well.
	 */
	return DSA_8021Q_RSV | DSA_8021Q_VBID(bridge_num);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_bridge_vid);

/* Returns the VID that will be installed as pvid for this switch port, sent as
 * tagged egress towards the CPU port and decoded by the rcv function.
 */
u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp)
{
	return DSA_8021Q_RSV | DSA_8021Q_SWITCH_ID(dp->ds->index) |
	       DSA_8021Q_PORT(dp->index);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_standalone_vid);

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

/* Returns the decoded VBID from the RX VID. */
static int dsa_tag_8021q_rx_vbid(u16 vid)
{
	u16 vbid_hi = (vid & DSA_8021Q_VBID_HI_MASK) >> DSA_8021Q_VBID_HI_SHIFT;
	u16 vbid_lo = (vid & DSA_8021Q_VBID_LO_MASK) >> DSA_8021Q_VBID_LO_SHIFT;

	return (vbid_hi << 2) | vbid_lo;
}

bool vid_is_dsa_8021q(u16 vid)
{
	u16 rsv = (vid & DSA_8021Q_RSV_MASK) >> DSA_8021Q_RSV_SHIFT;

	return rsv == DSA_8021Q_RSV_VAL;
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
	return dsa_port_is_dsa(dp) || dsa_port_is_cpu(dp) || dp == info->dp;
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
				flags |= BRIDGE_VLAN_INFO_UNTAGGED |
					 BRIDGE_VLAN_INFO_PVID;

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

/* There are 2 ways of offloading tag_8021q VLANs.
 *
 * One is to use a hardware TCAM to push the port's standalone VLAN into the
 * frame when forwarding it to the CPU, as an egress modification rule on the
 * CPU port. This is preferable because it has no side effects for the
 * autonomous forwarding path, and accomplishes tag_8021q's primary goal of
 * identifying the source port of each packet based on VLAN ID.
 *
 * The other is to commit the tag_8021q VLAN as a PVID to the VLAN table, and
 * to configure the port as VLAN-unaware. This is less preferable because
 * unique source port identification can only be done for standalone ports;
 * under a VLAN-unaware bridge, all ports share the same tag_8021q VLAN as
 * PVID, and under a VLAN-aware bridge, packets received by software will not
 * have tag_8021q VLANs appended, just bridge VLANs.
 *
 * For tag_8021q implementations of the second type, this method is used to
 * replace the standalone tag_8021q VLAN of a port with the tag_8021q VLAN to
 * be used for VLAN-unaware bridging.
 */
int dsa_tag_8021q_bridge_join(struct dsa_switch *ds, int port,
			      struct dsa_bridge bridge)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	u16 standalone_vid, bridge_vid;
	int err;

	/* Delete the standalone VLAN of the port and replace it with a
	 * bridging VLAN
	 */
	standalone_vid = dsa_tag_8021q_standalone_vid(dp);
	bridge_vid = dsa_tag_8021q_bridge_vid(bridge.num);

	err = dsa_port_tag_8021q_vlan_add(dp, bridge_vid, true);
	if (err)
		return err;

	dsa_port_tag_8021q_vlan_del(dp, standalone_vid, false);

	return 0;
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_bridge_join);

void dsa_tag_8021q_bridge_leave(struct dsa_switch *ds, int port,
				struct dsa_bridge bridge)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	u16 standalone_vid, bridge_vid;
	int err;

	/* Delete the bridging VLAN of the port and replace it with a
	 * standalone VLAN
	 */
	standalone_vid = dsa_tag_8021q_standalone_vid(dp);
	bridge_vid = dsa_tag_8021q_bridge_vid(bridge.num);

	err = dsa_port_tag_8021q_vlan_add(dp, standalone_vid, false);
	if (err) {
		dev_err(ds->dev,
			"Failed to delete tag_8021q standalone VLAN %d from port %d: %pe\n",
			standalone_vid, port, ERR_PTR(err));
	}

	dsa_port_tag_8021q_vlan_del(dp, bridge_vid, true);
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_bridge_leave);

/* Set up a port's standalone tag_8021q VLAN */
static int dsa_tag_8021q_port_setup(struct dsa_switch *ds, int port)
{
	struct dsa_8021q_context *ctx = ds->tag_8021q_ctx;
	struct dsa_port *dp = dsa_to_port(ds, port);
	u16 vid = dsa_tag_8021q_standalone_vid(dp);
	struct net_device *master;
	int err;

	/* The CPU port is implicitly configured by
	 * configuring the front-panel ports
	 */
	if (!dsa_port_is_user(dp))
		return 0;

	master = dsa_port_to_master(dp);

	err = dsa_port_tag_8021q_vlan_add(dp, vid, false);
	if (err) {
		dev_err(ds->dev,
			"Failed to apply standalone VID %d to port %d: %pe\n",
			vid, port, ERR_PTR(err));
		return err;
	}

	/* Add the VLAN to the master's RX filter. */
	vlan_vid_add(master, ctx->proto, vid);

	return err;
}

static void dsa_tag_8021q_port_teardown(struct dsa_switch *ds, int port)
{
	struct dsa_8021q_context *ctx = ds->tag_8021q_ctx;
	struct dsa_port *dp = dsa_to_port(ds, port);
	u16 vid = dsa_tag_8021q_standalone_vid(dp);
	struct net_device *master;

	/* The CPU port is implicitly configured by
	 * configuring the front-panel ports
	 */
	if (!dsa_port_is_user(dp))
		return;

	master = dsa_port_to_master(dp);

	dsa_port_tag_8021q_vlan_del(dp, vid, false);

	vlan_vid_del(master, ctx->proto, vid);
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

struct net_device *dsa_tag_8021q_find_port_by_vbid(struct net_device *master,
						   int vbid)
{
	struct dsa_port *cpu_dp = master->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct dsa_port *dp;

	if (WARN_ON(!vbid))
		return NULL;

	dsa_tree_for_each_user_port(dp, dst) {
		if (!dp->bridge)
			continue;

		if (dp->stp_state != BR_STATE_LEARNING &&
		    dp->stp_state != BR_STATE_FORWARDING)
			continue;

		if (dp->cpu_dp != cpu_dp)
			continue;

		if (dsa_port_bridge_num_get(dp) == vbid)
			return dp->slave;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(dsa_tag_8021q_find_port_by_vbid);

void dsa_8021q_rcv(struct sk_buff *skb, int *source_port, int *switch_id,
		   int *vbid)
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

	if (vbid)
		*vbid = dsa_tag_8021q_rx_vbid(vid);

	skb->priority = (tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
}
EXPORT_SYMBOL_GPL(dsa_8021q_rcv);
