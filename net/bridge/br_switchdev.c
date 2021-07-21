// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "br_private.h"

void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
			      struct sk_buff *skb)
{
	if (p->hwdom)
		BR_INPUT_SKB_CB(skb)->src_hwdom = p->hwdom;
}

bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
				  const struct sk_buff *skb)
{
	return !skb->offload_fwd_mark ||
	       BR_INPUT_SKB_CB(skb)->src_hwdom != p->hwdom;
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
			     struct netlink_ext_ack *extack)
{
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

	return nbp_switchdev_hwdom_set(p);
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
}

/* Let the bridge know that this port is offloaded, so that it can assign a
 * switchdev hardware domain to it.
 */
int switchdev_bridge_port_offload(struct net_device *brport_dev,
				  struct net_device *dev,
				  struct netlink_ext_ack *extack)
{
	struct netdev_phys_item_id ppid;
	struct net_bridge_port *p;
	int err;

	ASSERT_RTNL();

	p = br_port_get_rtnl(brport_dev);
	if (!p)
		return -ENODEV;

	err = dev_get_port_parent_id(dev, &ppid, false);
	if (err)
		return err;

	return nbp_switchdev_add(p, ppid, extack);
}
EXPORT_SYMBOL_GPL(switchdev_bridge_port_offload);

void switchdev_bridge_port_unoffload(struct net_device *brport_dev)
{
	struct net_bridge_port *p;

	ASSERT_RTNL();

	p = br_port_get_rtnl(brport_dev);
	if (!p)
		return;

	nbp_switchdev_del(p);
}
EXPORT_SYMBOL_GPL(switchdev_bridge_port_unoffload);
