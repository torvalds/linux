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
	v->num_vlans++;
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
	v->num_vlans--;
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

/* Strip the tag from the packet.  Will return skb with tci set 0.  */
static struct sk_buff *br_vlan_untag(struct sk_buff *skb)
{
	if (skb->protocol != htons(ETH_P_8021Q)) {
		skb->vlan_tci = 0;
		return skb;
	}

	skb->vlan_tci = 0;
	skb = vlan_untag(skb);
	if (skb)
		skb->vlan_tci = 0;

	return skb;
}

struct sk_buff *br_handle_vlan(struct net_bridge *br,
			       const struct net_port_vlans *pv,
			       struct sk_buff *skb)
{
	u16 vid;

	if (!br->vlan_enabled)
		goto out;

	/* At this point, we know that the frame was filtered and contains
	 * a valid vlan id.  If the vlan id matches the pvid of current port
	 * send untagged; otherwise, send taged.
	 */
	br_vlan_get_tag(skb, &vid);
	if (vid == br_get_pvid(pv))
		skb = br_vlan_untag(skb);
	else {
		/* Egress policy says "send tagged".  If output device
		 * is the  bridge, we need to add the VLAN header
		 * ourselves since we'll be going through the RX path.
		 * Sending to ports puts the frame on the TX path and
		 * we let dev_hard_start_xmit() add the header.
		 */
		if (skb->protocol != htons(ETH_P_8021Q) &&
		    pv->port_idx == 0) {
			/* vlan_put_tag expects skb->data to point to
			 * mac header.
			 */
			skb_push(skb, ETH_HLEN);
			skb = __vlan_put_tag(skb, skb->vlan_tci);
			if (!skb)
				goto out;
			/* put skb->data back to where it was */
			skb_pull(skb, ETH_HLEN);
			skb->vlan_tci = 0;
		}
	}

out:
	return skb;
}

/* Called under RCU */
bool br_allowed_ingress(struct net_bridge *br, struct net_port_vlans *v,
			struct sk_buff *skb, u16 *vid)
{
	/* If VLAN filtering is disabled on the bridge, all packets are
	 * permitted.
	 */
	if (!br->vlan_enabled)
		return true;

	/* If there are no vlan in the permitted list, all packets are
	 * rejected.
	 */
	if (!v)
		return false;

	if (br_vlan_get_tag(skb, vid)) {
		u16 pvid = br_get_pvid(v);

		/* Frame did not have a tag.  See if pvid is set
		 * on this port.  That tells us which vlan untagged
		 * traffic belongs to.
		 */
		if (pvid == VLAN_N_VID)
			return false;

		/* PVID is set on this port.  Any untagged ingress
		 * frame is considered to belong to this vlan.
		 */
		__vlan_hwaccel_put_tag(skb, pvid);
		return true;
	}

	/* Frame had a valid vlan tag.  See if vlan is allowed */
	if (test_bit(*vid, v->vlan_bitmap))
		return true;

	return false;
}

/* Called under RCU. */
bool br_allowed_egress(struct net_bridge *br,
		       const struct net_port_vlans *v,
		       const struct sk_buff *skb)
{
	u16 vid;

	if (!br->vlan_enabled)
		return true;

	if (!v)
		return false;

	br_vlan_get_tag(skb, &vid);
	if (test_bit(vid, v->vlan_bitmap))
		return true;

	return false;
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
