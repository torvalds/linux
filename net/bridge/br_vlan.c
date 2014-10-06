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
	struct net_bridge_port *p = NULL;
	struct net_bridge *br;
	struct net_device *dev;
	int err;

	if (test_bit(vid, v->vlan_bitmap)) {
		__vlan_add_flags(v, vid, flags);
		return 0;
	}

	if (v->port_idx) {
		p = v->parent.port;
		br = p->br;
		dev = p->dev;
	} else {
		br = v->parent.br;
		dev = br->dev;
	}

	if (p) {
		/* Add VLAN to the device filter if it is supported.
		 * This ensures tagged traffic enters the bridge when
		 * promiscuous mode is disabled by br_manage_promisc().
		 */
		err = vlan_vid_add(dev, br->vlan_proto, vid);
		if (err)
			return err;
	}

	err = br_fdb_insert(br, p, dev->dev_addr, vid);
	if (err) {
		br_err(br, "failed insert local address into bridge "
		       "forwarding table\n");
		goto out_filt;
	}

	set_bit(vid, v->vlan_bitmap);
	v->num_vlans++;
	__vlan_add_flags(v, vid, flags);

	return 0;

out_filt:
	if (p)
		vlan_vid_del(dev, br->vlan_proto, vid);
	return err;
}

static int __vlan_del(struct net_port_vlans *v, u16 vid)
{
	if (!test_bit(vid, v->vlan_bitmap))
		return -EINVAL;

	__vlan_delete_pvid(v, vid);
	clear_bit(vid, v->untagged_bitmap);

	if (v->port_idx) {
		struct net_bridge_port *p = v->parent.port;
		vlan_vid_del(p->dev, p->br->vlan_proto, vid);
	}

	clear_bit(vid, v->vlan_bitmap);
	v->num_vlans--;
	if (bitmap_empty(v->vlan_bitmap, VLAN_N_VID)) {
		if (v->port_idx)
			RCU_INIT_POINTER(v->parent.port->vlan_info, NULL);
		else
			RCU_INIT_POINTER(v->parent.br->vlan_info, NULL);
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
		RCU_INIT_POINTER(v->parent.port->vlan_info, NULL);
	else
		RCU_INIT_POINTER(v->parent.br->vlan_info, NULL);
	kfree_rcu(v, rcu);
}

struct sk_buff *br_handle_vlan(struct net_bridge *br,
			       const struct net_port_vlans *pv,
			       struct sk_buff *skb)
{
	u16 vid;

	if (!br->vlan_enabled)
		goto out;

	/* Vlan filter table must be configured at this point.  The
	 * only exception is the bridge is set in promisc mode and the
	 * packet is destined for the bridge device.  In this case
	 * pass the packet as is.
	 */
	if (!pv) {
		if ((br->dev->flags & IFF_PROMISC) && skb->dev == br->dev) {
			goto out;
		} else {
			kfree_skb(skb);
			return NULL;
		}
	}

	/* At this point, we know that the frame was filtered and contains
	 * a valid vlan id.  If the vlan id is set in the untagged bitmap,
	 * send untagged; otherwise, send tagged.
	 */
	br_vlan_get_tag(skb, &vid);
	if (test_bit(vid, pv->untagged_bitmap))
		skb->vlan_tci = 0;

out:
	return skb;
}

/* Called under RCU */
bool br_allowed_ingress(struct net_bridge *br, struct net_port_vlans *v,
			struct sk_buff *skb, u16 *vid)
{
	bool tagged;
	__be16 proto;

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

	proto = br->vlan_proto;

	/* If vlan tx offload is disabled on bridge device and frame was
	 * sent from vlan device on the bridge device, it does not have
	 * HW accelerated vlan tag.
	 */
	if (unlikely(!vlan_tx_tag_present(skb) &&
		     skb->protocol == proto)) {
		skb = skb_vlan_untag(skb);
		if (unlikely(!skb))
			return false;
	}

	if (!br_vlan_get_tag(skb, vid)) {
		/* Tagged frame */
		if (skb->vlan_proto != proto) {
			/* Protocol-mismatch, empty out vlan_tci for new tag */
			skb_push(skb, ETH_HLEN);
			skb = __vlan_put_tag(skb, skb->vlan_proto,
					     vlan_tx_tag_get(skb));
			if (unlikely(!skb))
				return false;

			skb_pull(skb, ETH_HLEN);
			skb_reset_mac_len(skb);
			*vid = 0;
			tagged = false;
		} else {
			tagged = true;
		}
	} else {
		/* Untagged frame */
		tagged = false;
	}

	if (!*vid) {
		u16 pvid = br_get_pvid(v);

		/* Frame had a tag with VID 0 or did not have a tag.
		 * See if pvid is set on this port.  That tells us which
		 * vlan untagged or priority-tagged traffic belongs to.
		 */
		if (pvid == VLAN_N_VID)
			goto drop;

		/* PVID is set on this port.  Any untagged or priority-tagged
		 * ingress frame is considered to belong to this vlan.
		 */
		*vid = pvid;
		if (likely(!tagged))
			/* Untagged Frame. */
			__vlan_hwaccel_put_tag(skb, proto, pvid);
		else
			/* Priority-tagged Frame.
			 * At this point, We know that skb->vlan_tci had
			 * VLAN_TAG_PRESENT bit and its VID field was 0x000.
			 * We update only VID field and preserve PCP field.
			 */
			skb->vlan_tci |= pvid;

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

/* Called under RCU */
bool br_should_learn(struct net_bridge_port *p, struct sk_buff *skb, u16 *vid)
{
	struct net_bridge *br = p->br;
	struct net_port_vlans *v;

	if (!br->vlan_enabled)
		return true;

	v = rcu_dereference(p->vlan_info);
	if (!v)
		return false;

	if (!br_vlan_get_tag(skb, vid) && skb->vlan_proto != br->vlan_proto)
		*vid = 0;

	if (!*vid) {
		*vid = br_get_pvid(v);
		if (*vid == VLAN_N_VID)
			return false;

		return true;
	}

	if (test_bit(*vid, v->vlan_bitmap))
		return true;

	return false;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
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

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int br_vlan_delete(struct net_bridge *br, u16 vid)
{
	struct net_port_vlans *pv;

	ASSERT_RTNL();

	pv = rtnl_dereference(br->vlan_info);
	if (!pv)
		return -EINVAL;

	br_fdb_find_delete_local(br, NULL, br->dev->dev_addr, vid);

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

bool br_vlan_find(struct net_bridge *br, u16 vid)
{
	struct net_port_vlans *pv;
	bool found = false;

	rcu_read_lock();
	pv = rcu_dereference(br->vlan_info);

	if (!pv)
		goto out;

	if (test_bit(vid, pv->vlan_bitmap))
		found = true;

out:
	rcu_read_unlock();
	return found;
}

/* Must be protected by RTNL. */
static void recalculate_group_addr(struct net_bridge *br)
{
	if (br->group_addr_set)
		return;

	spin_lock_bh(&br->lock);
	if (!br->vlan_enabled || br->vlan_proto == htons(ETH_P_8021Q)) {
		/* Bridge Group Address */
		br->group_addr[5] = 0x00;
	} else { /* vlan_enabled && ETH_P_8021AD */
		/* Provider Bridge Group Address */
		br->group_addr[5] = 0x08;
	}
	spin_unlock_bh(&br->lock);
}

/* Must be protected by RTNL. */
void br_recalculate_fwd_mask(struct net_bridge *br)
{
	if (!br->vlan_enabled || br->vlan_proto == htons(ETH_P_8021Q))
		br->group_fwd_mask_required = BR_GROUPFWD_DEFAULT;
	else /* vlan_enabled && ETH_P_8021AD */
		br->group_fwd_mask_required = BR_GROUPFWD_8021AD &
					      ~(1u << br->group_addr[5]);
}

int br_vlan_filter_toggle(struct net_bridge *br, unsigned long val)
{
	if (!rtnl_trylock())
		return restart_syscall();

	if (br->vlan_enabled == val)
		goto unlock;

	br->vlan_enabled = val;
	br_manage_promisc(br);
	recalculate_group_addr(br);
	br_recalculate_fwd_mask(br);

unlock:
	rtnl_unlock();
	return 0;
}

int br_vlan_set_proto(struct net_bridge *br, unsigned long val)
{
	int err = 0;
	struct net_bridge_port *p;
	struct net_port_vlans *pv;
	__be16 proto, oldproto;
	u16 vid, errvid;

	if (val != ETH_P_8021Q && val != ETH_P_8021AD)
		return -EPROTONOSUPPORT;

	if (!rtnl_trylock())
		return restart_syscall();

	proto = htons(val);
	if (br->vlan_proto == proto)
		goto unlock;

	/* Add VLANs for the new proto to the device filter. */
	list_for_each_entry(p, &br->port_list, list) {
		pv = rtnl_dereference(p->vlan_info);
		if (!pv)
			continue;

		for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID) {
			err = vlan_vid_add(p->dev, proto, vid);
			if (err)
				goto err_filt;
		}
	}

	oldproto = br->vlan_proto;
	br->vlan_proto = proto;

	recalculate_group_addr(br);
	br_recalculate_fwd_mask(br);

	/* Delete VLANs for the old proto from the device filter. */
	list_for_each_entry(p, &br->port_list, list) {
		pv = rtnl_dereference(p->vlan_info);
		if (!pv)
			continue;

		for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID)
			vlan_vid_del(p->dev, oldproto, vid);
	}

unlock:
	rtnl_unlock();
	return err;

err_filt:
	errvid = vid;
	for_each_set_bit(vid, pv->vlan_bitmap, errvid)
		vlan_vid_del(p->dev, proto, vid);

	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		pv = rtnl_dereference(p->vlan_info);
		if (!pv)
			continue;

		for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID)
			vlan_vid_del(p->dev, proto, vid);
	}

	goto unlock;
}

void br_vlan_init(struct net_bridge *br)
{
	br->vlan_proto = htons(ETH_P_8021Q);
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
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

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int nbp_vlan_delete(struct net_bridge_port *port, u16 vid)
{
	struct net_port_vlans *pv;

	ASSERT_RTNL();

	pv = rtnl_dereference(port->vlan_info);
	if (!pv)
		return -EINVAL;

	br_fdb_find_delete_local(port->br, port, port->dev->dev_addr, vid);

	return __vlan_del(pv, vid);
}

void nbp_vlan_flush(struct net_bridge_port *port)
{
	struct net_port_vlans *pv;
	u16 vid;

	ASSERT_RTNL();

	pv = rtnl_dereference(port->vlan_info);
	if (!pv)
		return;

	for_each_set_bit(vid, pv->vlan_bitmap, VLAN_N_VID)
		vlan_vid_del(port->dev, port->br->vlan_proto, vid);

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
