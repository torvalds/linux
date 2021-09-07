// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "br_private.h"

static struct static_key_false br_switchdev_tx_fwd_offload;

static bool nbp_switchdev_can_offload_tx_fwd(const struct net_bridge_port *p,
					     const struct sk_buff *skb)
{
	if (!static_branch_unlikely(&br_switchdev_tx_fwd_offload))
		return false;

	return (p->flags & BR_TX_FWD_OFFLOAD) &&
	       (p->hwdom != BR_INPUT_SKB_CB(skb)->src_hwdom);
}

bool br_switchdev_frame_uses_tx_fwd_offload(struct sk_buff *skb)
{
	if (!static_branch_unlikely(&br_switchdev_tx_fwd_offload))
		return false;

	return BR_INPUT_SKB_CB(skb)->tx_fwd_offload;
}

void br_switchdev_frame_set_offload_fwd_mark(struct sk_buff *skb)
{
	skb->offload_fwd_mark = br_switchdev_frame_uses_tx_fwd_offload(skb);
}

/* Mark the frame for TX forwarding offload if this egress port supports it */
void nbp_switchdev_frame_mark_tx_fwd_offload(const struct net_bridge_port *p,
					     struct sk_buff *skb)
{
	if (nbp_switchdev_can_offload_tx_fwd(p, skb))
		BR_INPUT_SKB_CB(skb)->tx_fwd_offload = true;
}

/* Lazily adds the hwdom of the egress bridge port to the bit mask of hwdoms
 * that the skb has been already forwarded to, to avoid further cloning to
 * other ports in the same hwdom by making nbp_switchdev_allowed_egress()
 * return false.
 */
void nbp_switchdev_frame_mark_tx_fwd_to_hwdom(const struct net_bridge_port *p,
					      struct sk_buff *skb)
{
	if (nbp_switchdev_can_offload_tx_fwd(p, skb))
		set_bit(p->hwdom, &BR_INPUT_SKB_CB(skb)->fwd_hwdoms);
}

void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
			      struct sk_buff *skb)
{
	if (p->hwdom)
		BR_INPUT_SKB_CB(skb)->src_hwdom = p->hwdom;
}

bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
				  const struct sk_buff *skb)
{
	struct br_input_skb_cb *cb = BR_INPUT_SKB_CB(skb);

	return !test_bit(p->hwdom, &cb->fwd_hwdoms) &&
		(!skb->offload_fwd_mark || cb->src_hwdom != p->hwdom);
}

/* Flags that can be offloaded to hardware */
#define BR_PORT_FLAGS_HW_OFFLOAD (BR_LEARNING | BR_FLOOD | \
				  BR_MCAST_FLOOD | BR_BCAST_FLOOD)

int br_switchdev_set_port_flag(struct net_bridge_port *p,
			       unsigned long flags,
			       unsigned long mask,
			       struct netlink_ext_ack *extack)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
	};
	struct switchdev_notifier_port_attr_info info = {
		.attr = &attr,
	};
	int err;

	mask &= BR_PORT_FLAGS_HW_OFFLOAD;
	if (!mask)
		return 0;

	attr.id = SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS;
	attr.u.brport_flags.val = flags;
	attr.u.brport_flags.mask = mask;

	/* We run from atomic context here */
	err = call_switchdev_notifiers(SWITCHDEV_PORT_ATTR_SET, p->dev,
				       &info.info, extack);
	err = notifier_to_errno(err);
	if (err == -EOPNOTSUPP)
		return 0;

	if (err) {
		if (extack && !extack->_msg)
			NL_SET_ERR_MSG_MOD(extack,
					   "bridge flag offload is not supported");
		return -EOPNOTSUPP;
	}

	attr.id = SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS;
	attr.flags = SWITCHDEV_F_DEFER;

	err = switchdev_port_attr_set(p->dev, &attr, extack);
	if (err) {
		if (extack && !extack->_msg)
			NL_SET_ERR_MSG_MOD(extack,
					   "error setting offload flag on port");
		return err;
	}

	return 0;
}

void
br_switchdev_fdb_notify(struct net_bridge *br,
			const struct net_bridge_fdb_entry *fdb, int type)
{
	const struct net_bridge_port *dst = READ_ONCE(fdb->dst);
	struct switchdev_notifier_fdb_info info = {
		.addr = fdb->key.addr.addr,
		.vid = fdb->key.vlan_id,
		.added_by_user = test_bit(BR_FDB_ADDED_BY_USER, &fdb->flags),
		.is_local = test_bit(BR_FDB_LOCAL, &fdb->flags),
		.offloaded = test_bit(BR_FDB_OFFLOADED, &fdb->flags),
	};
	struct net_device *dev = (!dst || info.is_local) ? br->dev : dst->dev;

	switch (type) {
	case RTM_DELNEIGH:
		call_switchdev_notifiers(SWITCHDEV_FDB_DEL_TO_DEVICE,
					 dev, &info.info, NULL);
		break;
	case RTM_NEWNEIGH:
		call_switchdev_notifiers(SWITCHDEV_FDB_ADD_TO_DEVICE,
					 dev, &info.info, NULL);
		break;
	}
}

int br_switchdev_port_vlan_add(struct net_device *dev, u16 vid, u16 flags,
			       struct netlink_ext_ack *extack)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.flags = flags,
		.vid = vid,
	};

	return switchdev_port_obj_add(dev, &v.obj, extack);
}

int br_switchdev_port_vlan_del(struct net_device *dev, u16 vid)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid = vid,
	};

	return switchdev_port_obj_del(dev, &v.obj);
}

static int nbp_switchdev_hwdom_set(struct net_bridge_port *joining)
{
	struct net_bridge *br = joining->br;
	struct net_bridge_port *p;
	int hwdom;

	/* joining is yet to be added to the port list. */
	list_for_each_entry(p, &br->port_list, list) {
		if (netdev_phys_item_id_same(&joining->ppid, &p->ppid)) {
			joining->hwdom = p->hwdom;
			return 0;
		}
	}

	hwdom = find_next_zero_bit(&br->busy_hwdoms, BR_HWDOM_MAX, 1);
	if (hwdom >= BR_HWDOM_MAX)
		return -EBUSY;

	set_bit(hwdom, &br->busy_hwdoms);
	joining->hwdom = hwdom;
	return 0;
}

static void nbp_switchdev_hwdom_put(struct net_bridge_port *leaving)
{
	struct net_bridge *br = leaving->br;
	struct net_bridge_port *p;

	/* leaving is no longer in the port list. */
	list_for_each_entry(p, &br->port_list, list) {
		if (p->hwdom == leaving->hwdom)
			return;
	}

	clear_bit(leaving->hwdom, &br->busy_hwdoms);
}

static int nbp_switchdev_add(struct net_bridge_port *p,
			     struct netdev_phys_item_id ppid,
			     bool tx_fwd_offload,
			     struct netlink_ext_ack *extack)
{
	int err;

	if (p->offload_count) {
		/* Prevent unsupported configurations such as a bridge port
		 * which is a bonding interface, and the member ports are from
		 * different hardware switches.
		 */
		if (!netdev_phys_item_id_same(&p->ppid, &ppid)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Same bridge port cannot be offloaded by two physical switches");
			return -EBUSY;
		}

		/* Tolerate drivers that call switchdev_bridge_port_offload()
		 * more than once for the same bridge port, such as when the
		 * bridge port is an offloaded bonding/team interface.
		 */
		p->offload_count++;

		return 0;
	}

	p->ppid = ppid;
	p->offload_count = 1;

	err = nbp_switchdev_hwdom_set(p);
	if (err)
		return err;

	if (tx_fwd_offload) {
		p->flags |= BR_TX_FWD_OFFLOAD;
		static_branch_inc(&br_switchdev_tx_fwd_offload);
	}

	return 0;
}

static void nbp_switchdev_del(struct net_bridge_port *p)
{
	if (WARN_ON(!p->offload_count))
		return;

	p->offload_count--;

	if (p->offload_count)
		return;

	if (p->hwdom)
		nbp_switchdev_hwdom_put(p);

	if (p->flags & BR_TX_FWD_OFFLOAD) {
		p->flags &= ~BR_TX_FWD_OFFLOAD;
		static_branch_dec(&br_switchdev_tx_fwd_offload);
	}
}

static int nbp_switchdev_sync_objs(struct net_bridge_port *p, const void *ctx,
				   struct notifier_block *atomic_nb,
				   struct notifier_block *blocking_nb,
				   struct netlink_ext_ack *extack)
{
	struct net_device *br_dev = p->br->dev;
	struct net_device *dev = p->dev;
	int err;

	err = br_vlan_replay(br_dev, dev, ctx, true, blocking_nb, extack);
	if (err && err != -EOPNOTSUPP)
		return err;

	err = br_mdb_replay(br_dev, dev, ctx, true, blocking_nb, extack);
	if (err && err != -EOPNOTSUPP)
		return err;

	err = br_fdb_replay(br_dev, ctx, true, atomic_nb);
	if (err && err != -EOPNOTSUPP)
		return err;

	return 0;
}

static void nbp_switchdev_unsync_objs(struct net_bridge_port *p,
				      const void *ctx,
				      struct notifier_block *atomic_nb,
				      struct notifier_block *blocking_nb)
{
	struct net_device *br_dev = p->br->dev;
	struct net_device *dev = p->dev;

	br_vlan_replay(br_dev, dev, ctx, false, blocking_nb, NULL);

	br_mdb_replay(br_dev, dev, ctx, false, blocking_nb, NULL);

	br_fdb_replay(br_dev, ctx, false, atomic_nb);
}

/* Let the bridge know that this port is offloaded, so that it can assign a
 * switchdev hardware domain to it.
 */
int br_switchdev_port_offload(struct net_bridge_port *p,
			      struct net_device *dev, const void *ctx,
			      struct notifier_block *atomic_nb,
			      struct notifier_block *blocking_nb,
			      bool tx_fwd_offload,
			      struct netlink_ext_ack *extack)
{
	struct netdev_phys_item_id ppid;
	int err;

	err = dev_get_port_parent_id(dev, &ppid, false);
	if (err)
		return err;

	err = nbp_switchdev_add(p, ppid, tx_fwd_offload, extack);
	if (err)
		return err;

	err = nbp_switchdev_sync_objs(p, ctx, atomic_nb, blocking_nb, extack);
	if (err)
		goto out_switchdev_del;

	return 0;

out_switchdev_del:
	nbp_switchdev_del(p);

	return err;
}

void br_switchdev_port_unoffload(struct net_bridge_port *p, const void *ctx,
				 struct notifier_block *atomic_nb,
				 struct notifier_block *blocking_nb)
{
	nbp_switchdev_unsync_objs(p, ctx, atomic_nb, blocking_nb);

	nbp_switchdev_del(p);
}
