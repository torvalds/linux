// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "br_private.h"

static int br_switchdev_mark_get(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;

	/* dev is yet to be added to the port list. */
	list_for_each_entry(p, &br->port_list, list) {
		if (netdev_port_same_parent_id(dev, p->dev))
			return p->offload_fwd_mark;
	}

	return ++br->offload_fwd_mark;
}

int nbp_switchdev_mark_set(struct net_bridge_port *p)
{
	struct netdev_phys_item_id ppid = { };
	int err;

	ASSERT_RTNL();

	err = dev_get_port_parent_id(p->dev, &ppid, true);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	p->offload_fwd_mark = br_switchdev_mark_get(p->br, p->dev);

	return 0;
}

void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
			      struct sk_buff *skb)
{
	if (skb->offload_fwd_mark && !WARN_ON_ONCE(!p->offload_fwd_mark))
		BR_INPUT_SKB_CB(skb)->offload_fwd_mark = p->offload_fwd_mark;
}

bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
				  const struct sk_buff *skb)
{
	return !skb->offload_fwd_mark ||
	       BR_INPUT_SKB_CB(skb)->offload_fwd_mark != p->offload_fwd_mark;
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
	struct net_device *dev = dst ? dst->dev : br->dev;
	struct switchdev_notifier_fdb_info info = {
		.addr = fdb->key.addr.addr,
		.vid = fdb->key.vlan_id,
		.added_by_user = test_bit(BR_FDB_ADDED_BY_USER, &fdb->flags),
		.is_local = test_bit(BR_FDB_LOCAL, &fdb->flags),
		.offloaded = test_bit(BR_FDB_OFFLOADED, &fdb->flags),
	};

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
