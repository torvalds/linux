#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/switchdev.h>

#include "br_private.h"

static inline int br_vlan_cmp(struct rhashtable_compare_arg *arg,
			      const void *ptr)
{
	const struct net_bridge_vlan *vle = ptr;
	u16 vid = *(u16 *)arg->key;

	return vle->vid != vid;
}

static const struct rhashtable_params br_vlan_rht_params = {
	.head_offset = offsetof(struct net_bridge_vlan, vnode),
	.key_offset = offsetof(struct net_bridge_vlan, vid),
	.key_len = sizeof(u16),
	.nelem_hint = 3,
	.locks_mul = 1,
	.max_size = VLAN_N_VID,
	.obj_cmpfn = br_vlan_cmp,
	.automatic_shrinking = true,
};

static struct net_bridge_vlan *br_vlan_lookup(struct rhashtable *tbl, u16 vid)
{
	return rhashtable_lookup_fast(tbl, &vid, br_vlan_rht_params);
}

static void __vlan_add_pvid(struct net_bridge_vlan_group *vg, u16 vid)
{
	if (vg->pvid == vid)
		return;

	smp_wmb();
	vg->pvid = vid;
}

static void __vlan_delete_pvid(struct net_bridge_vlan_group *vg, u16 vid)
{
	if (vg->pvid != vid)
		return;

	smp_wmb();
	vg->pvid = 0;
}

static void __vlan_add_flags(struct net_bridge_vlan *v, u16 flags)
{
	struct net_bridge_vlan_group *vg;

	if (br_vlan_is_master(v))
		vg = br_vlan_group(v->br);
	else
		vg = nbp_vlan_group(v->port);

	if (flags & BRIDGE_VLAN_INFO_PVID)
		__vlan_add_pvid(vg, v->vid);
	else
		__vlan_delete_pvid(vg, v->vid);

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED)
		v->flags |= BRIDGE_VLAN_INFO_UNTAGGED;
	else
		v->flags &= ~BRIDGE_VLAN_INFO_UNTAGGED;
}

static int __vlan_vid_add(struct net_device *dev, struct net_bridge *br,
			  u16 vid, u16 flags)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.flags = flags,
		.vid_begin = vid,
		.vid_end = vid,
	};
	int err;

	/* Try switchdev op first. In case it is not supported, fallback to
	 * 8021q add.
	 */
	err = switchdev_port_obj_add(dev, &v.obj);
	if (err == -EOPNOTSUPP)
		return vlan_vid_add(dev, br->vlan_proto, vid);
	return err;
}

static void __vlan_add_list(struct net_bridge_vlan *v)
{
	struct net_bridge_vlan_group *vg;
	struct list_head *headp, *hpos;
	struct net_bridge_vlan *vent;

	if (br_vlan_is_master(v))
		vg = br_vlan_group(v->br);
	else
		vg = nbp_vlan_group(v->port);

	headp = &vg->vlan_list;
	list_for_each_prev(hpos, headp) {
		vent = list_entry(hpos, struct net_bridge_vlan, vlist);
		if (v->vid < vent->vid)
			continue;
		else
			break;
	}
	list_add_rcu(&v->vlist, hpos);
}

static void __vlan_del_list(struct net_bridge_vlan *v)
{
	list_del_rcu(&v->vlist);
}

static int __vlan_vid_del(struct net_device *dev, struct net_bridge *br,
			  u16 vid)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.vid_begin = vid,
		.vid_end = vid,
	};
	int err;

	/* Try switchdev op first. In case it is not supported, fallback to
	 * 8021q del.
	 */
	err = switchdev_port_obj_del(dev, &v.obj);
	if (err == -EOPNOTSUPP) {
		vlan_vid_del(dev, br->vlan_proto, vid);
		return 0;
	}
	return err;
}

/* Returns a master vlan, if it didn't exist it gets created. In all cases a
 * a reference is taken to the master vlan before returning.
 */
static struct net_bridge_vlan *br_vlan_get_master(struct net_bridge *br, u16 vid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *masterv;

	vg = br_vlan_group(br);
	masterv = br_vlan_find(vg, vid);
	if (!masterv) {
		/* missing global ctx, create it now */
		if (br_vlan_add(br, vid, 0))
			return NULL;
		masterv = br_vlan_find(vg, vid);
		if (WARN_ON(!masterv))
			return NULL;
	}
	atomic_inc(&masterv->refcnt);

	return masterv;
}

static void br_vlan_put_master(struct net_bridge_vlan *masterv)
{
	struct net_bridge_vlan_group *vg;

	if (!br_vlan_is_master(masterv))
		return;

	vg = br_vlan_group(masterv->br);
	if (atomic_dec_and_test(&masterv->refcnt)) {
		rhashtable_remove_fast(&vg->vlan_hash,
				       &masterv->vnode, br_vlan_rht_params);
		__vlan_del_list(masterv);
		kfree_rcu(masterv, rcu);
	}
}

/* This is the shared VLAN add function which works for both ports and bridge
 * devices. There are four possible calls to this function in terms of the
 * vlan entry type:
 * 1. vlan is being added on a port (no master flags, global entry exists)
 * 2. vlan is being added on a bridge (both master and brentry flags)
 * 3. vlan is being added on a port, but a global entry didn't exist which
 *    is being created right now (master flag set, brentry flag unset), the
 *    global entry is used for global per-vlan features, but not for filtering
 * 4. same as 3 but with both master and brentry flags set so the entry
 *    will be used for filtering in both the port and the bridge
 */
static int __vlan_add(struct net_bridge_vlan *v, u16 flags)
{
	struct net_bridge_vlan *masterv = NULL;
	struct net_bridge_port *p = NULL;
	struct net_bridge_vlan_group *vg;
	struct net_device *dev;
	struct net_bridge *br;
	int err;

	if (br_vlan_is_master(v)) {
		br = v->br;
		dev = br->dev;
		vg = br_vlan_group(br);
	} else {
		p = v->port;
		br = p->br;
		dev = p->dev;
		vg = nbp_vlan_group(p);
	}

	if (p) {
		/* Add VLAN to the device filter if it is supported.
		 * This ensures tagged traffic enters the bridge when
		 * promiscuous mode is disabled by br_manage_promisc().
		 */
		err = __vlan_vid_add(dev, br, v->vid, flags);
		if (err)
			goto out;

		/* need to work on the master vlan too */
		if (flags & BRIDGE_VLAN_INFO_MASTER) {
			err = br_vlan_add(br, v->vid, flags |
						      BRIDGE_VLAN_INFO_BRENTRY);
			if (err)
				goto out_filt;
		}

		masterv = br_vlan_get_master(br, v->vid);
		if (!masterv)
			goto out_filt;
		v->brvlan = masterv;
	}

	/* Add the dev mac and count the vlan only if it's usable */
	if (br_vlan_should_use(v)) {
		err = br_fdb_insert(br, p, dev->dev_addr, v->vid);
		if (err) {
			br_err(br, "failed insert local address into bridge forwarding table\n");
			goto out_filt;
		}
		vg->num_vlans++;
	}

	err = rhashtable_lookup_insert_fast(&vg->vlan_hash, &v->vnode,
					    br_vlan_rht_params);
	if (err)
		goto out_fdb_insert;

	__vlan_add_list(v);
	__vlan_add_flags(v, flags);
out:
	return err;

out_fdb_insert:
	if (br_vlan_should_use(v)) {
		br_fdb_find_delete_local(br, p, dev->dev_addr, v->vid);
		vg->num_vlans--;
	}

out_filt:
	if (p) {
		__vlan_vid_del(dev, br, v->vid);
		if (masterv) {
			br_vlan_put_master(masterv);
			v->brvlan = NULL;
		}
	}

	goto out;
}

static int __vlan_del(struct net_bridge_vlan *v)
{
	struct net_bridge_vlan *masterv = v;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p = NULL;
	int err = 0;

	if (br_vlan_is_master(v)) {
		vg = br_vlan_group(v->br);
	} else {
		p = v->port;
		vg = nbp_vlan_group(v->port);
		masterv = v->brvlan;
	}

	__vlan_delete_pvid(vg, v->vid);
	if (p) {
		err = __vlan_vid_del(p->dev, p->br, v->vid);
		if (err)
			goto out;
	}

	if (br_vlan_should_use(v)) {
		v->flags &= ~BRIDGE_VLAN_INFO_BRENTRY;
		vg->num_vlans--;
	}

	if (masterv != v) {
		rhashtable_remove_fast(&vg->vlan_hash, &v->vnode,
				       br_vlan_rht_params);
		__vlan_del_list(v);
		kfree_rcu(v, rcu);
	}

	br_vlan_put_master(masterv);
out:
	return err;
}

static void __vlan_group_free(struct net_bridge_vlan_group *vg)
{
	WARN_ON(!list_empty(&vg->vlan_list));
	rhashtable_destroy(&vg->vlan_hash);
	kfree(vg);
}

static void __vlan_flush(struct net_bridge_vlan_group *vg)
{
	struct net_bridge_vlan *vlan, *tmp;

	__vlan_delete_pvid(vg, vg->pvid);
	list_for_each_entry_safe(vlan, tmp, &vg->vlan_list, vlist)
		__vlan_del(vlan);
}

struct sk_buff *br_handle_vlan(struct net_bridge *br,
			       struct net_bridge_vlan_group *vg,
			       struct sk_buff *skb)
{
	struct net_bridge_vlan *v;
	u16 vid;

	/* If this packet was not filtered at input, let it pass */
	if (!BR_INPUT_SKB_CB(skb)->vlan_filtered)
		goto out;

	/* At this point, we know that the frame was filtered and contains
	 * a valid vlan id.  If the vlan id has untagged flag set,
	 * send untagged; otherwise, send tagged.
	 */
	br_vlan_get_tag(skb, &vid);
	v = br_vlan_find(vg, vid);
	/* Vlan entry must be configured at this point.  The
	 * only exception is the bridge is set in promisc mode and the
	 * packet is destined for the bridge device.  In this case
	 * pass the packet as is.
	 */
	if (!v || !br_vlan_should_use(v)) {
		if ((br->dev->flags & IFF_PROMISC) && skb->dev == br->dev) {
			goto out;
		} else {
			kfree_skb(skb);
			return NULL;
		}
	}
	if (v->flags & BRIDGE_VLAN_INFO_UNTAGGED)
		skb->vlan_tci = 0;

out:
	return skb;
}

/* Called under RCU */
static bool __allowed_ingress(struct net_bridge_vlan_group *vg, __be16 proto,
			      struct sk_buff *skb, u16 *vid)
{
	const struct net_bridge_vlan *v;
	bool tagged;

	BR_INPUT_SKB_CB(skb)->vlan_filtered = true;
	/* If vlan tx offload is disabled on bridge device and frame was
	 * sent from vlan device on the bridge device, it does not have
	 * HW accelerated vlan tag.
	 */
	if (unlikely(!skb_vlan_tag_present(skb) &&
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
			skb = vlan_insert_tag_set_proto(skb, skb->vlan_proto,
							skb_vlan_tag_get(skb));
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
		u16 pvid = br_get_pvid(vg);

		/* Frame had a tag with VID 0 or did not have a tag.
		 * See if pvid is set on this port.  That tells us which
		 * vlan untagged or priority-tagged traffic belongs to.
		 */
		if (!pvid)
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
	v = br_vlan_find(vg, *vid);
	if (v && br_vlan_should_use(v))
		return true;
drop:
	kfree_skb(skb);
	return false;
}

bool br_allowed_ingress(const struct net_bridge *br,
			struct net_bridge_vlan_group *vg, struct sk_buff *skb,
			u16 *vid)
{
	/* If VLAN filtering is disabled on the bridge, all packets are
	 * permitted.
	 */
	if (!br->vlan_enabled) {
		BR_INPUT_SKB_CB(skb)->vlan_filtered = false;
		return true;
	}

	return __allowed_ingress(vg, br->vlan_proto, skb, vid);
}

/* Called under RCU. */
bool br_allowed_egress(struct net_bridge_vlan_group *vg,
		       const struct sk_buff *skb)
{
	const struct net_bridge_vlan *v;
	u16 vid;

	/* If this packet was not filtered at input, let it pass */
	if (!BR_INPUT_SKB_CB(skb)->vlan_filtered)
		return true;

	br_vlan_get_tag(skb, &vid);
	v = br_vlan_find(vg, vid);
	if (v && br_vlan_should_use(v))
		return true;

	return false;
}

/* Called under RCU */
bool br_should_learn(struct net_bridge_port *p, struct sk_buff *skb, u16 *vid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge *br = p->br;

	/* If filtering was disabled at input, let it pass. */
	if (!br->vlan_enabled)
		return true;

	vg = nbp_vlan_group_rcu(p);
	if (!vg || !vg->num_vlans)
		return false;

	if (!br_vlan_get_tag(skb, vid) && skb->vlan_proto != br->vlan_proto)
		*vid = 0;

	if (!*vid) {
		*vid = br_get_pvid(vg);
		if (!*vid)
			return false;

		return true;
	}

	if (br_vlan_find(vg, *vid))
		return true;

	return false;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int br_vlan_add(struct net_bridge *br, u16 vid, u16 flags)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *vlan;
	int ret;

	ASSERT_RTNL();

	vg = br_vlan_group(br);
	vlan = br_vlan_find(vg, vid);
	if (vlan) {
		if (!br_vlan_is_brentry(vlan)) {
			/* Trying to change flags of non-existent bridge vlan */
			if (!(flags & BRIDGE_VLAN_INFO_BRENTRY))
				return -EINVAL;
			/* It was only kept for port vlans, now make it real */
			ret = br_fdb_insert(br, NULL, br->dev->dev_addr,
					    vlan->vid);
			if (ret) {
				br_err(br, "failed insert local address into bridge forwarding table\n");
				return ret;
			}
			atomic_inc(&vlan->refcnt);
			vlan->flags |= BRIDGE_VLAN_INFO_BRENTRY;
			vg->num_vlans++;
		}
		__vlan_add_flags(vlan, flags);
		return 0;
	}

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->vid = vid;
	vlan->flags = flags | BRIDGE_VLAN_INFO_MASTER;
	vlan->flags &= ~BRIDGE_VLAN_INFO_PVID;
	vlan->br = br;
	if (flags & BRIDGE_VLAN_INFO_BRENTRY)
		atomic_set(&vlan->refcnt, 1);
	ret = __vlan_add(vlan, flags);
	if (ret)
		kfree(vlan);

	return ret;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int br_vlan_delete(struct net_bridge *br, u16 vid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;

	ASSERT_RTNL();

	vg = br_vlan_group(br);
	v = br_vlan_find(vg, vid);
	if (!v || !br_vlan_is_brentry(v))
		return -ENOENT;

	br_fdb_find_delete_local(br, NULL, br->dev->dev_addr, vid);
	br_fdb_delete_by_port(br, NULL, vid, 0);

	return __vlan_del(v);
}

void br_vlan_flush(struct net_bridge *br)
{
	struct net_bridge_vlan_group *vg;

	ASSERT_RTNL();

	vg = br_vlan_group(br);
	__vlan_flush(vg);
	RCU_INIT_POINTER(br->vlgrp, NULL);
	synchronize_rcu();
	__vlan_group_free(vg);
}

struct net_bridge_vlan *br_vlan_find(struct net_bridge_vlan_group *vg, u16 vid)
{
	if (!vg)
		return NULL;

	return br_vlan_lookup(&vg->vlan_hash, vid);
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

int __br_vlan_filter_toggle(struct net_bridge *br, unsigned long val)
{
	struct switchdev_attr attr = {
		.orig_dev = br->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
		.flags = SWITCHDEV_F_SKIP_EOPNOTSUPP,
		.u.vlan_filtering = val,
	};
	int err;

	if (br->vlan_enabled == val)
		return 0;

	err = switchdev_port_attr_set(br->dev, &attr);
	if (err && err != -EOPNOTSUPP)
		return err;

	br->vlan_enabled = val;
	br_manage_promisc(br);
	recalculate_group_addr(br);
	br_recalculate_fwd_mask(br);

	return 0;
}

int br_vlan_filter_toggle(struct net_bridge *br, unsigned long val)
{
	int err;

	if (!rtnl_trylock())
		return restart_syscall();

	err = __br_vlan_filter_toggle(br, val);
	rtnl_unlock();

	return err;
}

int __br_vlan_set_proto(struct net_bridge *br, __be16 proto)
{
	int err = 0;
	struct net_bridge_port *p;
	struct net_bridge_vlan *vlan;
	struct net_bridge_vlan_group *vg;
	__be16 oldproto;

	if (br->vlan_proto == proto)
		return 0;

	/* Add VLANs for the new proto to the device filter. */
	list_for_each_entry(p, &br->port_list, list) {
		vg = nbp_vlan_group(p);
		list_for_each_entry(vlan, &vg->vlan_list, vlist) {
			err = vlan_vid_add(p->dev, proto, vlan->vid);
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
		vg = nbp_vlan_group(p);
		list_for_each_entry(vlan, &vg->vlan_list, vlist)
			vlan_vid_del(p->dev, oldproto, vlan->vid);
	}

	return 0;

err_filt:
	list_for_each_entry_continue_reverse(vlan, &vg->vlan_list, vlist)
		vlan_vid_del(p->dev, proto, vlan->vid);

	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		vg = nbp_vlan_group(p);
		list_for_each_entry(vlan, &vg->vlan_list, vlist)
			vlan_vid_del(p->dev, proto, vlan->vid);
	}

	return err;
}

int br_vlan_set_proto(struct net_bridge *br, unsigned long val)
{
	int err;

	if (val != ETH_P_8021Q && val != ETH_P_8021AD)
		return -EPROTONOSUPPORT;

	if (!rtnl_trylock())
		return restart_syscall();

	err = __br_vlan_set_proto(br, htons(val));
	rtnl_unlock();

	return err;
}

static bool vlan_default_pvid(struct net_bridge_vlan_group *vg, u16 vid)
{
	struct net_bridge_vlan *v;

	if (vid != vg->pvid)
		return false;

	v = br_vlan_lookup(&vg->vlan_hash, vid);
	if (v && br_vlan_should_use(v) &&
	    (v->flags & BRIDGE_VLAN_INFO_UNTAGGED))
		return true;

	return false;
}

static void br_vlan_disable_default_pvid(struct net_bridge *br)
{
	struct net_bridge_port *p;
	u16 pvid = br->default_pvid;

	/* Disable default_pvid on all ports where it is still
	 * configured.
	 */
	if (vlan_default_pvid(br_vlan_group(br), pvid))
		br_vlan_delete(br, pvid);

	list_for_each_entry(p, &br->port_list, list) {
		if (vlan_default_pvid(nbp_vlan_group(p), pvid))
			nbp_vlan_delete(p, pvid);
	}

	br->default_pvid = 0;
}

int __br_vlan_set_default_pvid(struct net_bridge *br, u16 pvid)
{
	const struct net_bridge_vlan *pvent;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;
	u16 old_pvid;
	int err = 0;
	unsigned long *changed;

	if (!pvid) {
		br_vlan_disable_default_pvid(br);
		return 0;
	}

	changed = kcalloc(BITS_TO_LONGS(BR_MAX_PORTS), sizeof(unsigned long),
			  GFP_KERNEL);
	if (!changed)
		return -ENOMEM;

	old_pvid = br->default_pvid;

	/* Update default_pvid config only if we do not conflict with
	 * user configuration.
	 */
	vg = br_vlan_group(br);
	pvent = br_vlan_find(vg, pvid);
	if ((!old_pvid || vlan_default_pvid(vg, old_pvid)) &&
	    (!pvent || !br_vlan_should_use(pvent))) {
		err = br_vlan_add(br, pvid,
				  BRIDGE_VLAN_INFO_PVID |
				  BRIDGE_VLAN_INFO_UNTAGGED |
				  BRIDGE_VLAN_INFO_BRENTRY);
		if (err)
			goto out;
		br_vlan_delete(br, old_pvid);
		set_bit(0, changed);
	}

	list_for_each_entry(p, &br->port_list, list) {
		/* Update default_pvid config only if we do not conflict with
		 * user configuration.
		 */
		vg = nbp_vlan_group(p);
		if ((old_pvid &&
		     !vlan_default_pvid(vg, old_pvid)) ||
		    br_vlan_find(vg, pvid))
			continue;

		err = nbp_vlan_add(p, pvid,
				   BRIDGE_VLAN_INFO_PVID |
				   BRIDGE_VLAN_INFO_UNTAGGED);
		if (err)
			goto err_port;
		nbp_vlan_delete(p, old_pvid);
		set_bit(p->port_no, changed);
	}

	br->default_pvid = pvid;

out:
	kfree(changed);
	return err;

err_port:
	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		if (!test_bit(p->port_no, changed))
			continue;

		if (old_pvid)
			nbp_vlan_add(p, old_pvid,
				     BRIDGE_VLAN_INFO_PVID |
				     BRIDGE_VLAN_INFO_UNTAGGED);
		nbp_vlan_delete(p, pvid);
	}

	if (test_bit(0, changed)) {
		if (old_pvid)
			br_vlan_add(br, old_pvid,
				    BRIDGE_VLAN_INFO_PVID |
				    BRIDGE_VLAN_INFO_UNTAGGED |
				    BRIDGE_VLAN_INFO_BRENTRY);
		br_vlan_delete(br, pvid);
	}
	goto out;
}

int br_vlan_set_default_pvid(struct net_bridge *br, unsigned long val)
{
	u16 pvid = val;
	int err = 0;

	if (val >= VLAN_VID_MASK)
		return -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (pvid == br->default_pvid)
		goto unlock;

	/* Only allow default pvid change when filtering is disabled */
	if (br->vlan_enabled) {
		pr_info_once("Please disable vlan filtering to change default_pvid\n");
		err = -EPERM;
		goto unlock;
	}
	err = __br_vlan_set_default_pvid(br, pvid);
unlock:
	rtnl_unlock();
	return err;
}

int br_vlan_init(struct net_bridge *br)
{
	struct net_bridge_vlan_group *vg;
	int ret = -ENOMEM;

	vg = kzalloc(sizeof(*vg), GFP_KERNEL);
	if (!vg)
		goto out;
	ret = rhashtable_init(&vg->vlan_hash, &br_vlan_rht_params);
	if (ret)
		goto err_rhtbl;
	INIT_LIST_HEAD(&vg->vlan_list);
	br->vlan_proto = htons(ETH_P_8021Q);
	br->default_pvid = 1;
	rcu_assign_pointer(br->vlgrp, vg);
	ret = br_vlan_add(br, 1,
			  BRIDGE_VLAN_INFO_PVID | BRIDGE_VLAN_INFO_UNTAGGED |
			  BRIDGE_VLAN_INFO_BRENTRY);
	if (ret)
		goto err_vlan_add;

out:
	return ret;

err_vlan_add:
	rhashtable_destroy(&vg->vlan_hash);
err_rhtbl:
	kfree(vg);

	goto out;
}

int nbp_vlan_init(struct net_bridge_port *p)
{
	struct switchdev_attr attr = {
		.orig_dev = p->br->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
		.flags = SWITCHDEV_F_SKIP_EOPNOTSUPP,
		.u.vlan_filtering = p->br->vlan_enabled,
	};
	struct net_bridge_vlan_group *vg;
	int ret = -ENOMEM;

	vg = kzalloc(sizeof(struct net_bridge_vlan_group), GFP_KERNEL);
	if (!vg)
		goto out;

	ret = switchdev_port_attr_set(p->dev, &attr);
	if (ret && ret != -EOPNOTSUPP)
		goto err_vlan_enabled;

	ret = rhashtable_init(&vg->vlan_hash, &br_vlan_rht_params);
	if (ret)
		goto err_rhtbl;
	INIT_LIST_HEAD(&vg->vlan_list);
	rcu_assign_pointer(p->vlgrp, vg);
	if (p->br->default_pvid) {
		ret = nbp_vlan_add(p, p->br->default_pvid,
				   BRIDGE_VLAN_INFO_PVID |
				   BRIDGE_VLAN_INFO_UNTAGGED);
		if (ret)
			goto err_vlan_add;
	}
out:
	return ret;

err_vlan_add:
	RCU_INIT_POINTER(p->vlgrp, NULL);
	synchronize_rcu();
	rhashtable_destroy(&vg->vlan_hash);
err_vlan_enabled:
err_rhtbl:
	kfree(vg);

	goto out;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int nbp_vlan_add(struct net_bridge_port *port, u16 vid, u16 flags)
{
	struct switchdev_obj_port_vlan v = {
		.obj.orig_dev = port->dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.flags = flags,
		.vid_begin = vid,
		.vid_end = vid,
	};
	struct net_bridge_vlan *vlan;
	int ret;

	ASSERT_RTNL();

	vlan = br_vlan_find(nbp_vlan_group(port), vid);
	if (vlan) {
		/* Pass the flags to the hardware bridge */
		ret = switchdev_port_obj_add(port->dev, &v.obj);
		if (ret && ret != -EOPNOTSUPP)
			return ret;
		__vlan_add_flags(vlan, flags);
		return 0;
	}

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->vid = vid;
	vlan->port = port;
	ret = __vlan_add(vlan, flags);
	if (ret)
		kfree(vlan);

	return ret;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int nbp_vlan_delete(struct net_bridge_port *port, u16 vid)
{
	struct net_bridge_vlan *v;

	ASSERT_RTNL();

	v = br_vlan_find(nbp_vlan_group(port), vid);
	if (!v)
		return -ENOENT;
	br_fdb_find_delete_local(port->br, port, port->dev->dev_addr, vid);
	br_fdb_delete_by_port(port->br, port, vid, 0);

	return __vlan_del(v);
}

void nbp_vlan_flush(struct net_bridge_port *port)
{
	struct net_bridge_vlan_group *vg;

	ASSERT_RTNL();

	vg = nbp_vlan_group(port);
	__vlan_flush(vg);
	RCU_INIT_POINTER(port->vlgrp, NULL);
	synchronize_rcu();
	__vlan_group_free(vg);
}
