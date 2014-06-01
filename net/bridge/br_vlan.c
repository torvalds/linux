#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "br_private.h"

static void __vlan_add_pvid(struct net_port_vlans *v, u16 vid)
{
	if (v->pvid == vid)
		return;

	smp_wmb();
	v->pvid = vid;
}

static void __vlan_delete_pvid(struct net_port_vlans *v, u16 vid)
{
	if (v->pvid != vid)
		return;

	smp_wmb();
	v->pvid = 0;
}

static void __vlan_add_flags(struct net_port_vlans *v, u16 vid, u16 flags)
{
	if (flags & BRIDGE_VLAN_INFO_PVID)
		__vlan_add_pvid(v, vid);

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED)
		set_bit(vid, v->untagged_bitmap);
}

static int __vlan_add(struct net_port_vlans *v, u16 vid, u16 flags)
{
	const struct net_device_ops *ops;
	struct net_bridge_port *p = NULL;
	struct net_bridge *br;
	struct net_device *dev;
	int err;

	if (test_bit(vid, v->vlan_bitmap)) {
		__vlan_add_flags(v, vid, flags);
		return 0;
	}

	if (vid) {
		if (v->port_idx) {
			p = v->parent.port;
			br = p->br;
			dev = p->dev;
		} else {
			br = v->parent.br;
			dev = br->dev;
		}
		ops = dev->netdev_ops;

		if (p && (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER)) {
			/* Add VLAN to the device filter if it is supported.
			 * Stricly speaking, this is not necessary now, since
			 * devices are made promiscuous by the bridge, but if
			 * that ever changes this code will allow tagged
			 * traffic to enter the bridge.
			 */
			err = ops->ndo_vlan_rx_add_vid(dev, htons(ETH_P_8021Q),
						       vid);
			if (err)
				return err;
		}

		err = br_fdb_insert(br, p, dev->dev_addr, vid);
		if (err) {
			br_err(br, "failed insert local address into bridge "
			       "forwarding table\n");
			goto out_filt;
		}

	}

	set_bit(vid, v->vlan_bitmap);
	v->num_vlans++;
	__vlan_add_flags(v, vid, flags);

	return 0;

out_filt:
	if (p && (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		ops->ndo_vlan_rx_kill_vid(dev, htons(ETH_P_8021Q), vid);
	return err;
}

static int __vlan_del(struct net_port_vlans *v, u16 vid)
{
	if (!test_bit(vid, v->vlan_bitmap))
		return -EINVAL;

	__vlan_delete_pvid(v, vid);
	clear_bit(vid, v->untagged_bitmap);

	if (v->port_idx && vid) {
		struct net_device *dev = v->parent.port->dev;
		const struct net_device_ops *ops = dev->netdev_ops;

		if (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER)
			ops->ndo_vlan_rx_kill_vid(dev, htons(ETH_P_8021Q), vid);
	}

	clear_bit(vid, v->vlan_bitmap);
	v->num_vlans--;
	if (bitmap_empty(v->vlan_bitmap, VLAN_N_VID)) {
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
	smp_wmb();
	v->pvid = 0;
	bitmap_zero(v->vlan_bitmap, VLAN_N_VID);
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
	 * a valid vlan id.  If the vlan id is set in the untagged bitmap,
	 * send untagged; otherwise, send taged.
	 */
	br_vlan_get_tag(skb, &vid);
	if (test_bit(vid, pv->untagged_bitmap))
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
			skb = __vlan_put_tag(skb, skb->vlan_proto, skb->vlan_tci);
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
		goto drop;

	if (br_vlan_get_tag(skb, vid)) {
		u16 pvid = br_get_pvid(v);

		/* Frame did not have a tag.  See if pvid is set
		 * on this port.  That tells us which vlan untagged
		 * traffic belongs to.
		 */
		if (pvid == VLAN_N_VID)
			goto drop;

		/* PVID is set on this port.  Any untagged ingress
		 * frame is considered to belong to this vlan.
		 */
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), pvid);
		return true;
	}

	/* Frame had a valid vlan tag.  See if vlan is allowed */
	if (test_bit(*vid, v->vlan_bitmap))
		return true;
drop:
	kfree_skb(skb);
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
int br_vlan_add(struct net_bridge *br, u16 vid, u16 flags)
{
	struct net_port_vlans *pv = NULL;
	int err;

	ASSERT_RTNL();

	pv = rtnl_dereference(br->vlan_info);
	if (pv)
		return __vlan_add(pv, vid, flags);

	/* Create port vlan infomration
	 */
	pv = kzalloc(sizeof(*pv), GFP_KERNEL);
	if (!pv)
		return -ENOMEM;

	pv->parent.br = br;
	err = __vlan_add(pv, vid, flags);
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

	if (vid) {
		/* If the VID !=0 remove fdb for this vid. VID 0 is special
		 * in that it's the default and is always there in the fdb.
		 */
		spin_lock_bh(&br->hash_lock);
		fdb_delete_by_addr(br, br->dev->dev_addr, vid);
		spin_unlock_bh(&br->hash_lock);
	}

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
int nbp_vlan_add(struct net_bridge_port *port, u16 vid, u16 flags)
{
	struct net_port_vlans *pv = NULL;
	int err;

	ASSERT_RTNL();

	pv = rtnl_dereference(port->vlan_info);
	if (pv)
		return __vlan_add(pv, vid, flags);

	/* Create port vlan infomration
	 */
	pv = kzalloc(sizeof(*pv), GFP_KERNEL);
	if (!pv) {
		err = -ENOMEM;
		goto clean_up;
	}

	pv->port_idx = port->port_no;
	pv->parent.port = port;
	err = __vlan_add(pv, vid, flags);
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

	if (vid) {
		/* If the VID !=0 remove fdb for this vid. VID 0 is special
		 * in that it's the default and is always there in the fdb.
		 */
		spin_lock_bh(&port->br->hash_lock);
		fdb_delete_by_addr(port->br, port->dev->dev_addr, vid);
		spin_unlock_bh(&port->br->hash_lock);
	}

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

bool nbp_vlan_find(struct net_bridge_port *port, u16 vid)
{
	struct net_port_vlans *pv;
	bool found = false;

	rcu_read_lock();
	pv = rcu_dereference(port->vlan_info);

	if (!pv)
		goto out;

	if (test_bit(vid, pv->vlan_bitmap))
		found = true;

out:
	rcu_read_unlock();
	return found;
}
