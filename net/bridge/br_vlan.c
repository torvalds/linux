#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "br_private.h"

static int __vlan_add(struct net_port_vlans *v, u16 vid)
{
	int err;

	if (test_bit(vid, v->vlan_bitmap))
		return -EEXIST;

	if (v->port_idx && vid) {
		struct net_device *dev = v->parent.port->dev;

		/* Add VLAN to the device filter if it is supported.
		 * Stricly speaking, this is not necessary now, since devices
		 * are made promiscuous by the bridge, but if that ever changes
		 * this code will allow tagged traffic to enter the bridge.
		 */
		if (dev->features & NETIF_F_HW_VLAN_FILTER) {
			err = dev->netdev_ops->ndo_vlan_rx_add_vid(dev, vid);
			if (err)
				return err;
		}
	}

	set_bit(vid, v->vlan_bitmap);
	return 0;
}

static int __vlan_del(struct net_port_vlans *v, u16 vid)
{
	if (!test_bit(vid, v->vlan_bitmap))
		return -EINVAL;

	if (v->port_idx && vid) {
		struct net_device *dev = v->parent.port->dev;

		if (dev->features & NETIF_F_HW_VLAN_FILTER)
			dev->netdev_ops->ndo_vlan_rx_kill_vid(dev, vid);
	}

	clear_bit(vid, v->vlan_bitmap);
	if (bitmap_empty(v->vlan_bitmap, BR_VLAN_BITMAP_LEN)) {
		if (v->port_idx)
			rcu_assign_pointer(v->parent.port->vlan_info, NULL);
		else
			rcu_assign_pointer(v->parent.br->vlan_info, NULL);
		kfree_rcu(v, rcu);
	}
	return 0;
}

static void __vlan_flush(struct net_port_vlans *v)
{
	bitmap_zero(v->vlan_bitmap, BR_VLAN_BITMAP_LEN);
	if (v->port_idx)
		rcu_assign_pointer(v->parent.port->vlan_info, NULL);
	else
		rcu_assign_pointer(v->parent.br->vlan_info, NULL);
	kfree_rcu(v, rcu);
}

/* Must be protected by RTNL */
int br_vlan_add(struct net_bridge *br, u16 vid)
{
	struct net_port_vlans *pv = NULL;
	int err;

	ASSERT_RTNL();

	pv = rtnl_dereference(br->vlan_info);
	if (pv)
		return __vlan_add(pv, vid);

	/* Create port vlan infomration
	 */
	pv = kzalloc(sizeof(*pv), GFP_KERNEL);
	if (!pv)
		return -ENOMEM;

	pv->parent.br = br;
	err = __vlan_add(pv, vid);
	if (err)
		goto out;

	rcu_assign_pointer(br->vlan_info, pv);
	return 0;
out:
	kfree(pv);
	return err;
}

/* Must be protected by RTNL */
int br_vlan_delete(struct net_bridge *br, u16 vid)
{
	struct net_port_vlans *pv;

	ASSERT_RTNL();

	pv = rtnl_dereference(br->vlan_info);
	if (!pv)
		return -EINVAL;

	__vlan_del(pv, vid);
	return 0;
}

void br_vlan_flush(struct net_bridge *br)
{
	struct net_port_vlans *pv;

	ASSERT_RTNL();

	pv = rtnl_dereference(br->vlan_info);
	if (!pv)
		return;

	__vlan_flush(pv);
}

int br_vlan_filter_toggle(struct net_bridge *br, unsigned long val)
{
	if (!rtnl_trylock())
		return restart_syscall();

	if (br->vlan_enabled == val)
		goto unlock;

	br->vlan_enabled = val;

unlock:
	rtnl_unlock();
	return 0;
}

/* Must be protected by RTNL */
int nbp_vlan_add(struct net_bridge_port *port, u16 vid)
{
	struct net_port_vlans *pv = NULL;
	int err;

	ASSERT_RTNL();

	pv = rtnl_dereference(port->vlan_info);
	if (pv)
		return __vlan_add(pv, vid);

	/* Create port vlan infomration
	 */
	pv = kzalloc(sizeof(*pv), GFP_KERNEL);
	if (!pv) {
		err = -ENOMEM;
		goto clean_up;
	}

	pv->port_idx = port->port_no;
	pv->parent.port = port;
	err = __vlan_add(pv, vid);
	if (err)
		goto clean_up;

	rcu_assign_pointer(port->vlan_info, pv);
	return 0;

clean_up:
	kfree(pv);
	return err;
}

/* Must be protected by RTNL */
int nbp_vlan_delete(struct net_bridge_port *port, u16 vid)
{
	struct net_port_vlans *pv;

	ASSERT_RTNL();

	pv = rtnl_dereference(port->vlan_info);
	if (!pv)
		return -EINVAL;

	return __vlan_del(pv, vid);
}

void nbp_vlan_flush(struct net_bridge_port *port)
{
	struct net_port_vlans *pv;

	ASSERT_RTNL();

	pv = rtnl_dereference(port->vlan_info);
	if (!pv)
		return;

	__vlan_flush(pv);
}
