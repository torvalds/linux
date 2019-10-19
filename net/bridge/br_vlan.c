// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/switchdev.h>

#include "br_private.h"
#include "br_private_tunnel.h"

static void nbp_vlan_set_vlan_dev_state(struct net_bridge_port *p, u16 vid);

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
	.max_size = VLAN_N_VID,
	.obj_cmpfn = br_vlan_cmp,
	.automatic_shrinking = true,
};

static struct net_bridge_vlan *br_vlan_lookup(struct rhashtable *tbl, u16 vid)
{
	return rhashtable_lookup_fast(tbl, &vid, br_vlan_rht_params);
}

static bool __vlan_add_pvid(struct net_bridge_vlan_group *vg, u16 vid)
{
	if (vg->pvid == vid)
		return false;

	smp_wmb();
	vg->pvid = vid;

	return true;
}

static bool __vlan_delete_pvid(struct net_bridge_vlan_group *vg, u16 vid)
{
	if (vg->pvid != vid)
		return false;

	smp_wmb();
	vg->pvid = 0;

	return true;
}

/* return true if anything changed, false otherwise */
static bool __vlan_add_flags(struct net_bridge_vlan *v, u16 flags)
{
	struct net_bridge_vlan_group *vg;
	u16 old_flags = v->flags;
	bool ret;

	if (br_vlan_is_master(v))
		vg = br_vlan_group(v->br);
	else
		vg = nbp_vlan_group(v->port);

	if (flags & BRIDGE_VLAN_INFO_PVID)
		ret = __vlan_add_pvid(vg, v->vid);
	else
		ret = __vlan_delete_pvid(vg, v->vid);

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED)
		v->flags |= BRIDGE_VLAN_INFO_UNTAGGED;
	else
		v->flags &= ~BRIDGE_VLAN_INFO_UNTAGGED;

	return ret || !!(old_flags ^ v->flags);
}

static int __vlan_vid_add(struct net_device *dev, struct net_bridge *br,
			  struct net_bridge_vlan *v, u16 flags,
			  struct netlink_ext_ack *extack)
{
	int err;

	/* Try switchdev op first. In case it is not supported, fallback to
	 * 8021q add.
	 */
	err = br_switchdev_port_vlan_add(dev, v->vid, flags, extack);
	if (err == -EOPNOTSUPP)
		return vlan_vid_add(dev, br->vlan_proto, v->vid);
	v->priv_flags |= BR_VLFLAG_ADDED_BY_SWITCHDEV;
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
			  const struct net_bridge_vlan *v)
{
	int err;

	/* Try switchdev op first. In case it is not supported, fallback to
	 * 8021q del.
	 */
	err = br_switchdev_port_vlan_del(dev, v->vid);
	if (!(v->priv_flags & BR_VLFLAG_ADDED_BY_SWITCHDEV))
		vlan_vid_del(dev, br->vlan_proto, v->vid);
	return err == -EOPNOTSUPP ? 0 : err;
}

/* Returns a master vlan, if it didn't exist it gets created. In all cases a
 * a reference is taken to the master vlan before returning.
 */
static struct net_bridge_vlan *
br_vlan_get_master(struct net_bridge *br, u16 vid,
		   struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *masterv;

	vg = br_vlan_group(br);
	masterv = br_vlan_find(vg, vid);
	if (!masterv) {
		bool changed;

		/* missing global ctx, create it now */
		if (br_vlan_add(br, vid, 0, &changed, extack))
			return NULL;
		masterv = br_vlan_find(vg, vid);
		if (WARN_ON(!masterv))
			return NULL;
		refcount_set(&masterv->refcnt, 1);
		return masterv;
	}
	refcount_inc(&masterv->refcnt);

	return masterv;
}

static void br_master_vlan_rcu_free(struct rcu_head *rcu)
{
	struct net_bridge_vlan *v;

	v = container_of(rcu, struct net_bridge_vlan, rcu);
	WARN_ON(!br_vlan_is_master(v));
	free_percpu(v->stats);
	v->stats = NULL;
	kfree(v);
}

static void br_vlan_put_master(struct net_bridge_vlan *masterv)
{
	struct net_bridge_vlan_group *vg;

	if (!br_vlan_is_master(masterv))
		return;

	vg = br_vlan_group(masterv->br);
	if (refcount_dec_and_test(&masterv->refcnt)) {
		rhashtable_remove_fast(&vg->vlan_hash,
				       &masterv->vnode, br_vlan_rht_params);
		__vlan_del_list(masterv);
		call_rcu(&masterv->rcu, br_master_vlan_rcu_free);
	}
}

static void nbp_vlan_rcu_free(struct rcu_head *rcu)
{
	struct net_bridge_vlan *v;

	v = container_of(rcu, struct net_bridge_vlan, rcu);
	WARN_ON(br_vlan_is_master(v));
	/* if we had per-port stats configured then free them here */
	if (v->priv_flags & BR_VLFLAG_PER_PORT_STATS)
		free_percpu(v->stats);
	v->stats = NULL;
	kfree(v);
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
static int __vlan_add(struct net_bridge_vlan *v, u16 flags,
		      struct netlink_ext_ack *extack)
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
		err = __vlan_vid_add(dev, br, v, flags, extack);
		if (err)
			goto out;

		/* need to work on the master vlan too */
		if (flags & BRIDGE_VLAN_INFO_MASTER) {
			bool changed;

			err = br_vlan_add(br, v->vid,
					  flags | BRIDGE_VLAN_INFO_BRENTRY,
					  &changed, extack);
			if (err)
				goto out_filt;
		}

		masterv = br_vlan_get_master(br, v->vid, extack);
		if (!masterv)
			goto out_filt;
		v->brvlan = masterv;
		if (br_opt_get(br, BROPT_VLAN_STATS_PER_PORT)) {
			v->stats = netdev_alloc_pcpu_stats(struct br_vlan_stats);
			if (!v->stats) {
				err = -ENOMEM;
				goto out_filt;
			}
			v->priv_flags |= BR_VLFLAG_PER_PORT_STATS;
		} else {
			v->stats = masterv->stats;
		}
	} else {
		err = br_switchdev_port_vlan_add(dev, v->vid, flags, extack);
		if (err && err != -EOPNOTSUPP)
			goto out;
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

	if (p)
		nbp_vlan_set_vlan_dev_state(p, v->vid);
out:
	return err;

out_fdb_insert:
	if (br_vlan_should_use(v)) {
		br_fdb_find_delete_local(br, p, dev->dev_addr, v->vid);
		vg->num_vlans--;
	}

out_filt:
	if (p) {
		__vlan_vid_del(dev, br, v);
		if (masterv) {
			if (v->stats && masterv->stats != v->stats)
				free_percpu(v->stats);
			v->stats = NULL;

			br_vlan_put_master(masterv);
			v->brvlan = NULL;
		}
	} else {
		br_switchdev_port_vlan_del(dev, v->vid);
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
		err = __vlan_vid_del(p->dev, p->br, v);
		if (err)
			goto out;
	} else {
		err = br_switchdev_port_vlan_del(v->br->dev, v->vid);
		if (err && err != -EOPNOTSUPP)
			goto out;
		err = 0;
	}

	if (br_vlan_should_use(v)) {
		v->flags &= ~BRIDGE_VLAN_INFO_BRENTRY;
		vg->num_vlans--;
	}

	if (masterv != v) {
		vlan_tunnel_info_del(vg, v);
		rhashtable_remove_fast(&vg->vlan_hash, &v->vnode,
				       br_vlan_rht_params);
		__vlan_del_list(v);
		nbp_vlan_set_vlan_dev_state(p, v->vid);
		call_rcu(&v->rcu, nbp_vlan_rcu_free);
	}

	br_vlan_put_master(masterv);
out:
	return err;
}

static void __vlan_group_free(struct net_bridge_vlan_group *vg)
{
	WARN_ON(!list_empty(&vg->vlan_list));
	rhashtable_destroy(&vg->vlan_hash);
	vlan_tunnel_deinit(vg);
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
			       const struct net_bridge_port *p,
			       struct net_bridge_vlan_group *vg,
			       struct sk_buff *skb)
{
	struct br_vlan_stats *stats;
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
	if (br_opt_get(br, BROPT_VLAN_STATS_ENABLED)) {
		stats = this_cpu_ptr(v->stats);
		u64_stats_update_begin(&stats->syncp);
		stats->tx_bytes += skb->len;
		stats->tx_packets++;
		u64_stats_update_end(&stats->syncp);
	}

	if (v->flags & BRIDGE_VLAN_INFO_UNTAGGED)
		__vlan_hwaccel_clear_tag(skb);

	if (p && (p->flags & BR_VLAN_TUNNEL) &&
	    br_handle_egress_vlan_tunnel(skb, v)) {
		kfree_skb(skb);
		return NULL;
	}
out:
	return skb;
}

/* Called under RCU */
static bool __allowed_ingress(const struct net_bridge *br,
			      struct net_bridge_vlan_group *vg,
			      struct sk_buff *skb, u16 *vid)
{
	struct br_vlan_stats *stats;
	struct net_bridge_vlan *v;
	bool tagged;

	BR_INPUT_SKB_CB(skb)->vlan_filtered = true;
	/* If vlan tx offload is disabled on bridge device and frame was
	 * sent from vlan device on the bridge device, it does not have
	 * HW accelerated vlan tag.
	 */
	if (unlikely(!skb_vlan_tag_present(skb) &&
		     skb->protocol == br->vlan_proto)) {
		skb = skb_vlan_untag(skb);
		if (unlikely(!skb))
			return false;
	}

	if (!br_vlan_get_tag(skb, vid)) {
		/* Tagged frame */
		if (skb->vlan_proto != br->vlan_proto) {
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
			__vlan_hwaccel_put_tag(skb, br->vlan_proto, pvid);
		else
			/* Priority-tagged Frame.
			 * At this point, we know that skb->vlan_tci VID
			 * field was 0.
			 * We update only VID field and preserve PCP field.
			 */
			skb->vlan_tci |= pvid;

		/* if stats are disabled we can avoid the lookup */
		if (!br_opt_get(br, BROPT_VLAN_STATS_ENABLED))
			return true;
	}
	v = br_vlan_find(vg, *vid);
	if (!v || !br_vlan_should_use(v))
		goto drop;

	if (br_opt_get(br, BROPT_VLAN_STATS_ENABLED)) {
		stats = this_cpu_ptr(v->stats);
		u64_stats_update_begin(&stats->syncp);
		stats->rx_bytes += skb->len;
		stats->rx_packets++;
		u64_stats_update_end(&stats->syncp);
	}

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
	if (!br_opt_get(br, BROPT_VLAN_ENABLED)) {
		BR_INPUT_SKB_CB(skb)->vlan_filtered = false;
		return true;
	}

	return __allowed_ingress(br, vg, skb, vid);
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
	if (!br_opt_get(br, BROPT_VLAN_ENABLED))
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

static int br_vlan_add_existing(struct net_bridge *br,
				struct net_bridge_vlan_group *vg,
				struct net_bridge_vlan *vlan,
				u16 flags, bool *changed,
				struct netlink_ext_ack *extack)
{
	int err;

	err = br_switchdev_port_vlan_add(br->dev, vlan->vid, flags, extack);
	if (err && err != -EOPNOTSUPP)
		return err;

	if (!br_vlan_is_brentry(vlan)) {
		/* Trying to change flags of non-existent bridge vlan */
		if (!(flags & BRIDGE_VLAN_INFO_BRENTRY)) {
			err = -EINVAL;
			goto err_flags;
		}
		/* It was only kept for port vlans, now make it real */
		err = br_fdb_insert(br, NULL, br->dev->dev_addr,
				    vlan->vid);
		if (err) {
			br_err(br, "failed to insert local address into bridge forwarding table\n");
			goto err_fdb_insert;
		}

		refcount_inc(&vlan->refcnt);
		vlan->flags |= BRIDGE_VLAN_INFO_BRENTRY;
		vg->num_vlans++;
		*changed = true;
	}

	if (__vlan_add_flags(vlan, flags))
		*changed = true;

	return 0;

err_fdb_insert:
err_flags:
	br_switchdev_port_vlan_del(br->dev, vlan->vid);
	return err;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 * changed must be true only if the vlan was created or updated
 */
int br_vlan_add(struct net_bridge *br, u16 vid, u16 flags, bool *changed,
		struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *vlan;
	int ret;

	ASSERT_RTNL();

	*changed = false;
	vg = br_vlan_group(br);
	vlan = br_vlan_find(vg, vid);
	if (vlan)
		return br_vlan_add_existing(br, vg, vlan, flags, changed,
					    extack);

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->stats = netdev_alloc_pcpu_stats(struct br_vlan_stats);
	if (!vlan->stats) {
		kfree(vlan);
		return -ENOMEM;
	}
	vlan->vid = vid;
	vlan->flags = flags | BRIDGE_VLAN_INFO_MASTER;
	vlan->flags &= ~BRIDGE_VLAN_INFO_PVID;
	vlan->br = br;
	if (flags & BRIDGE_VLAN_INFO_BRENTRY)
		refcount_set(&vlan->refcnt, 1);
	ret = __vlan_add(vlan, flags, extack);
	if (ret) {
		free_percpu(vlan->stats);
		kfree(vlan);
	} else {
		*changed = true;
	}

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

	vlan_tunnel_info_del(vg, v);

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
	if (br_opt_get(br, BROPT_GROUP_ADDR_SET))
		return;

	spin_lock_bh(&br->lock);
	if (!br_opt_get(br, BROPT_VLAN_ENABLED) ||
	    br->vlan_proto == htons(ETH_P_8021Q)) {
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
	if (!br_opt_get(br, BROPT_VLAN_ENABLED) ||
	    br->vlan_proto == htons(ETH_P_8021Q))
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

	if (br_opt_get(br, BROPT_VLAN_ENABLED) == !!val)
		return 0;

	err = switchdev_port_attr_set(br->dev, &attr);
	if (err && err != -EOPNOTSUPP)
		return err;

	br_opt_toggle(br, BROPT_VLAN_ENABLED, !!val);
	br_manage_promisc(br);
	recalculate_group_addr(br);
	br_recalculate_fwd_mask(br);

	return 0;
}

int br_vlan_filter_toggle(struct net_bridge *br, unsigned long val)
{
	return __br_vlan_filter_toggle(br, val);
}

bool br_vlan_enabled(const struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);

	return br_opt_get(br, BROPT_VLAN_ENABLED);
}
EXPORT_SYMBOL_GPL(br_vlan_enabled);

int br_vlan_get_proto(const struct net_device *dev, u16 *p_proto)
{
	struct net_bridge *br = netdev_priv(dev);

	*p_proto = ntohs(br->vlan_proto);

	return 0;
}
EXPORT_SYMBOL_GPL(br_vlan_get_proto);

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
	if (val != ETH_P_8021Q && val != ETH_P_8021AD)
		return -EPROTONOSUPPORT;

	return __br_vlan_set_proto(br, htons(val));
}

int br_vlan_set_stats(struct net_bridge *br, unsigned long val)
{
	switch (val) {
	case 0:
	case 1:
		br_opt_toggle(br, BROPT_VLAN_STATS_ENABLED, !!val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int br_vlan_set_stats_per_port(struct net_bridge *br, unsigned long val)
{
	struct net_bridge_port *p;

	/* allow to change the option if there are no port vlans configured */
	list_for_each_entry(p, &br->port_list, list) {
		struct net_bridge_vlan_group *vg = nbp_vlan_group(p);

		if (vg->num_vlans)
			return -EBUSY;
	}

	switch (val) {
	case 0:
	case 1:
		br_opt_toggle(br, BROPT_VLAN_STATS_PER_PORT, !!val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
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

int __br_vlan_set_default_pvid(struct net_bridge *br, u16 pvid,
			       struct netlink_ext_ack *extack)
{
	const struct net_bridge_vlan *pvent;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;
	unsigned long *changed;
	bool vlchange;
	u16 old_pvid;
	int err = 0;

	if (!pvid) {
		br_vlan_disable_default_pvid(br);
		return 0;
	}

	changed = bitmap_zalloc(BR_MAX_PORTS, GFP_KERNEL);
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
				  BRIDGE_VLAN_INFO_BRENTRY,
				  &vlchange, extack);
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
				   BRIDGE_VLAN_INFO_UNTAGGED,
				   &vlchange, extack);
		if (err)
			goto err_port;
		nbp_vlan_delete(p, old_pvid);
		set_bit(p->port_no, changed);
	}

	br->default_pvid = pvid;

out:
	bitmap_free(changed);
	return err;

err_port:
	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		if (!test_bit(p->port_no, changed))
			continue;

		if (old_pvid)
			nbp_vlan_add(p, old_pvid,
				     BRIDGE_VLAN_INFO_PVID |
				     BRIDGE_VLAN_INFO_UNTAGGED,
				     &vlchange, NULL);
		nbp_vlan_delete(p, pvid);
	}

	if (test_bit(0, changed)) {
		if (old_pvid)
			br_vlan_add(br, old_pvid,
				    BRIDGE_VLAN_INFO_PVID |
				    BRIDGE_VLAN_INFO_UNTAGGED |
				    BRIDGE_VLAN_INFO_BRENTRY,
				    &vlchange, NULL);
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

	if (pvid == br->default_pvid)
		goto out;

	/* Only allow default pvid change when filtering is disabled */
	if (br_opt_get(br, BROPT_VLAN_ENABLED)) {
		pr_info_once("Please disable vlan filtering to change default_pvid\n");
		err = -EPERM;
		goto out;
	}
	err = __br_vlan_set_default_pvid(br, pvid, NULL);
out:
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
	ret = vlan_tunnel_init(vg);
	if (ret)
		goto err_tunnel_init;
	INIT_LIST_HEAD(&vg->vlan_list);
	br->vlan_proto = htons(ETH_P_8021Q);
	br->default_pvid = 1;
	rcu_assign_pointer(br->vlgrp, vg);

out:
	return ret;

err_tunnel_init:
	rhashtable_destroy(&vg->vlan_hash);
err_rhtbl:
	kfree(vg);

	goto out;
}

int nbp_vlan_init(struct net_bridge_port *p, struct netlink_ext_ack *extack)
{
	struct switchdev_attr attr = {
		.orig_dev = p->br->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
		.flags = SWITCHDEV_F_SKIP_EOPNOTSUPP,
		.u.vlan_filtering = br_opt_get(p->br, BROPT_VLAN_ENABLED),
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
	ret = vlan_tunnel_init(vg);
	if (ret)
		goto err_tunnel_init;
	INIT_LIST_HEAD(&vg->vlan_list);
	rcu_assign_pointer(p->vlgrp, vg);
	if (p->br->default_pvid) {
		bool changed;

		ret = nbp_vlan_add(p, p->br->default_pvid,
				   BRIDGE_VLAN_INFO_PVID |
				   BRIDGE_VLAN_INFO_UNTAGGED,
				   &changed, extack);
		if (ret)
			goto err_vlan_add;
	}
out:
	return ret;

err_vlan_add:
	RCU_INIT_POINTER(p->vlgrp, NULL);
	synchronize_rcu();
	vlan_tunnel_deinit(vg);
err_tunnel_init:
	rhashtable_destroy(&vg->vlan_hash);
err_rhtbl:
err_vlan_enabled:
	kfree(vg);

	goto out;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 * changed must be true only if the vlan was created or updated
 */
int nbp_vlan_add(struct net_bridge_port *port, u16 vid, u16 flags,
		 bool *changed, struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan *vlan;
	int ret;

	ASSERT_RTNL();

	*changed = false;
	vlan = br_vlan_find(nbp_vlan_group(port), vid);
	if (vlan) {
		/* Pass the flags to the hardware bridge */
		ret = br_switchdev_port_vlan_add(port->dev, vid, flags, extack);
		if (ret && ret != -EOPNOTSUPP)
			return ret;
		*changed = __vlan_add_flags(vlan, flags);

		return 0;
	}

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->vid = vid;
	vlan->port = port;
	ret = __vlan_add(vlan, flags, extack);
	if (ret)
		kfree(vlan);
	else
		*changed = true;

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

void br_vlan_get_stats(const struct net_bridge_vlan *v,
		       struct br_vlan_stats *stats)
{
	int i;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(i) {
		u64 rxpackets, rxbytes, txpackets, txbytes;
		struct br_vlan_stats *cpu_stats;
		unsigned int start;

		cpu_stats = per_cpu_ptr(v->stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			rxpackets = cpu_stats->rx_packets;
			rxbytes = cpu_stats->rx_bytes;
			txbytes = cpu_stats->tx_bytes;
			txpackets = cpu_stats->tx_packets;
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, start));

		stats->rx_packets += rxpackets;
		stats->rx_bytes += rxbytes;
		stats->tx_bytes += txbytes;
		stats->tx_packets += txpackets;
	}
}

static int __br_vlan_get_pvid(const struct net_device *dev,
			      struct net_bridge_port *p, u16 *p_pvid)
{
	struct net_bridge_vlan_group *vg;

	if (p)
		vg = nbp_vlan_group(p);
	else if (netif_is_bridge_master(dev))
		vg = br_vlan_group(netdev_priv(dev));
	else
		return -EINVAL;

	*p_pvid = br_get_pvid(vg);
	return 0;
}

int br_vlan_get_pvid(const struct net_device *dev, u16 *p_pvid)
{
	ASSERT_RTNL();

	return __br_vlan_get_pvid(dev, br_port_get_check_rtnl(dev), p_pvid);
}
EXPORT_SYMBOL_GPL(br_vlan_get_pvid);

int br_vlan_get_pvid_rcu(const struct net_device *dev, u16 *p_pvid)
{
	return __br_vlan_get_pvid(dev, br_port_get_check_rcu(dev), p_pvid);
}
EXPORT_SYMBOL_GPL(br_vlan_get_pvid_rcu);

int br_vlan_get_info(const struct net_device *dev, u16 vid,
		     struct bridge_vlan_info *p_vinfo)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	struct net_bridge_port *p;

	ASSERT_RTNL();
	p = br_port_get_check_rtnl(dev);
	if (p)
		vg = nbp_vlan_group(p);
	else if (netif_is_bridge_master(dev))
		vg = br_vlan_group(netdev_priv(dev));
	else
		return -EINVAL;

	v = br_vlan_find(vg, vid);
	if (!v)
		return -ENOENT;

	p_vinfo->vid = vid;
	p_vinfo->flags = v->flags;
	if (vid == br_get_pvid(vg))
		p_vinfo->flags |= BRIDGE_VLAN_INFO_PVID;
	return 0;
}
EXPORT_SYMBOL_GPL(br_vlan_get_info);

static int br_vlan_is_bind_vlan_dev(const struct net_device *dev)
{
	return is_vlan_dev(dev) &&
		!!(vlan_dev_priv(dev)->flags & VLAN_FLAG_BRIDGE_BINDING);
}

static int br_vlan_is_bind_vlan_dev_fn(struct net_device *dev,
				       __always_unused void *data)
{
	return br_vlan_is_bind_vlan_dev(dev);
}

static bool br_vlan_has_upper_bind_vlan_dev(struct net_device *dev)
{
	int found;

	rcu_read_lock();
	found = netdev_walk_all_upper_dev_rcu(dev, br_vlan_is_bind_vlan_dev_fn,
					      NULL);
	rcu_read_unlock();

	return !!found;
}

struct br_vlan_bind_walk_data {
	u16 vid;
	struct net_device *result;
};

static int br_vlan_match_bind_vlan_dev_fn(struct net_device *dev,
					  void *data_in)
{
	struct br_vlan_bind_walk_data *data = data_in;
	int found = 0;

	if (br_vlan_is_bind_vlan_dev(dev) &&
	    vlan_dev_priv(dev)->vlan_id == data->vid) {
		data->result = dev;
		found = 1;
	}

	return found;
}

static struct net_device *
br_vlan_get_upper_bind_vlan_dev(struct net_device *dev, u16 vid)
{
	struct br_vlan_bind_walk_data data = {
		.vid = vid,
	};

	rcu_read_lock();
	netdev_walk_all_upper_dev_rcu(dev, br_vlan_match_bind_vlan_dev_fn,
				      &data);
	rcu_read_unlock();

	return data.result;
}

static bool br_vlan_is_dev_up(const struct net_device *dev)
{
	return  !!(dev->flags & IFF_UP) && netif_oper_up(dev);
}

static void br_vlan_set_vlan_dev_state(const struct net_bridge *br,
				       struct net_device *vlan_dev)
{
	u16 vid = vlan_dev_priv(vlan_dev)->vlan_id;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;
	bool has_carrier = false;

	if (!netif_carrier_ok(br->dev)) {
		netif_carrier_off(vlan_dev);
		return;
	}

	list_for_each_entry(p, &br->port_list, list) {
		vg = nbp_vlan_group(p);
		if (br_vlan_find(vg, vid) && br_vlan_is_dev_up(p->dev)) {
			has_carrier = true;
			break;
		}
	}

	if (has_carrier)
		netif_carrier_on(vlan_dev);
	else
		netif_carrier_off(vlan_dev);
}

static void br_vlan_set_all_vlan_dev_state(struct net_bridge_port *p)
{
	struct net_bridge_vlan_group *vg = nbp_vlan_group(p);
	struct net_bridge_vlan *vlan;
	struct net_device *vlan_dev;

	list_for_each_entry(vlan, &vg->vlan_list, vlist) {
		vlan_dev = br_vlan_get_upper_bind_vlan_dev(p->br->dev,
							   vlan->vid);
		if (vlan_dev) {
			if (br_vlan_is_dev_up(p->dev)) {
				if (netif_carrier_ok(p->br->dev))
					netif_carrier_on(vlan_dev);
			} else {
				br_vlan_set_vlan_dev_state(p->br, vlan_dev);
			}
		}
	}
}

static void br_vlan_upper_change(struct net_device *dev,
				 struct net_device *upper_dev,
				 bool linking)
{
	struct net_bridge *br = netdev_priv(dev);

	if (!br_vlan_is_bind_vlan_dev(upper_dev))
		return;

	if (linking) {
		br_vlan_set_vlan_dev_state(br, upper_dev);
		br_opt_toggle(br, BROPT_VLAN_BRIDGE_BINDING, true);
	} else {
		br_opt_toggle(br, BROPT_VLAN_BRIDGE_BINDING,
			      br_vlan_has_upper_bind_vlan_dev(dev));
	}
}

struct br_vlan_link_state_walk_data {
	struct net_bridge *br;
};

static int br_vlan_link_state_change_fn(struct net_device *vlan_dev,
					void *data_in)
{
	struct br_vlan_link_state_walk_data *data = data_in;

	if (br_vlan_is_bind_vlan_dev(vlan_dev))
		br_vlan_set_vlan_dev_state(data->br, vlan_dev);

	return 0;
}

static void br_vlan_link_state_change(struct net_device *dev,
				      struct net_bridge *br)
{
	struct br_vlan_link_state_walk_data data = {
		.br = br
	};

	rcu_read_lock();
	netdev_walk_all_upper_dev_rcu(dev, br_vlan_link_state_change_fn,
				      &data);
	rcu_read_unlock();
}

/* Must be protected by RTNL. */
static void nbp_vlan_set_vlan_dev_state(struct net_bridge_port *p, u16 vid)
{
	struct net_device *vlan_dev;

	if (!br_opt_get(p->br, BROPT_VLAN_BRIDGE_BINDING))
		return;

	vlan_dev = br_vlan_get_upper_bind_vlan_dev(p->br->dev, vid);
	if (vlan_dev)
		br_vlan_set_vlan_dev_state(p->br, vlan_dev);
}

/* Must be protected by RTNL. */
int br_vlan_bridge_event(struct net_device *dev, unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct net_bridge *br = netdev_priv(dev);
	bool changed;
	int ret = 0;

	switch (event) {
	case NETDEV_REGISTER:
		ret = br_vlan_add(br, br->default_pvid,
				  BRIDGE_VLAN_INFO_PVID |
				  BRIDGE_VLAN_INFO_UNTAGGED |
				  BRIDGE_VLAN_INFO_BRENTRY, &changed, NULL);
		break;
	case NETDEV_UNREGISTER:
		br_vlan_delete(br, br->default_pvid);
		break;
	case NETDEV_CHANGEUPPER:
		info = ptr;
		br_vlan_upper_change(dev, info->upper_dev, info->linking);
		break;

	case NETDEV_CHANGE:
	case NETDEV_UP:
		if (!br_opt_get(br, BROPT_VLAN_BRIDGE_BINDING))
			break;
		br_vlan_link_state_change(dev, br);
		break;
	}

	return ret;
}

/* Must be protected by RTNL. */
void br_vlan_port_event(struct net_bridge_port *p, unsigned long event)
{
	if (!br_opt_get(p->br, BROPT_VLAN_BRIDGE_BINDING))
		return;

	switch (event) {
	case NETDEV_CHANGE:
	case NETDEV_DOWN:
	case NETDEV_UP:
		br_vlan_set_all_vlan_dev_state(p);
		break;
	}
}
