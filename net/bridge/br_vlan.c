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

static void __vlan_add_pvid(struct net_bridge_vlan_group *vg,
			    const struct net_bridge_vlan *v)
{
	if (vg->pvid == v->vid)
		return;

	smp_wmb();
	br_vlan_set_pvid_state(vg, v->state);
	vg->pvid = v->vid;
}

static void __vlan_delete_pvid(struct net_bridge_vlan_group *vg, u16 vid)
{
	if (vg->pvid != vid)
		return;

	smp_wmb();
	vg->pvid = 0;
}

/* Update the BRIDGE_VLAN_INFO_PVID and BRIDGE_VLAN_INFO_UNTAGGED flags of @v.
 * If @commit is false, return just whether the BRIDGE_VLAN_INFO_PVID and
 * BRIDGE_VLAN_INFO_UNTAGGED bits of @flags would produce any change onto @v.
 */
static bool __vlan_flags_update(struct net_bridge_vlan *v, u16 flags,
				bool commit)
{
	struct net_bridge_vlan_group *vg;
	bool change;

	if (br_vlan_is_master(v))
		vg = br_vlan_group(v->br);
	else
		vg = nbp_vlan_group(v->port);

	/* check if anything would be changed on commit */
	change = !!(flags & BRIDGE_VLAN_INFO_PVID) == !!(vg->pvid != v->vid) ||
		 ((flags ^ v->flags) & BRIDGE_VLAN_INFO_UNTAGGED);

	if (!commit)
		goto out;

	if (flags & BRIDGE_VLAN_INFO_PVID)
		__vlan_add_pvid(vg, v);
	else
		__vlan_delete_pvid(vg, v->vid);

	if (flags & BRIDGE_VLAN_INFO_UNTAGGED)
		v->flags |= BRIDGE_VLAN_INFO_UNTAGGED;
	else
		v->flags &= ~BRIDGE_VLAN_INFO_UNTAGGED;

out:
	return change;
}

static bool __vlan_flags_would_change(struct net_bridge_vlan *v, u16 flags)
{
	return __vlan_flags_update(v, flags, false);
}

static void __vlan_flags_commit(struct net_bridge_vlan *v, u16 flags)
{
	__vlan_flags_update(v, flags, true);
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
		if (v->vid >= vent->vid)
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

/* Returns a master vlan, if it didn't exist it gets created. In all cases
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
		br_multicast_toggle_one_vlan(masterv, false);
		br_multicast_ctx_deinit(&masterv->br_mcast_ctx);
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

			if (changed)
				br_vlan_notify(br, NULL, v->vid, 0,
					       RTM_NEWVLAN);
		}

		masterv = br_vlan_get_master(br, v->vid, extack);
		if (!masterv) {
			err = -ENOMEM;
			goto out_filt;
		}
		v->brvlan = masterv;
		if (br_opt_get(br, BROPT_VLAN_STATS_PER_PORT)) {
			v->stats =
			     netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
			if (!v->stats) {
				err = -ENOMEM;
				goto out_filt;
			}
			v->priv_flags |= BR_VLFLAG_PER_PORT_STATS;
		} else {
			v->stats = masterv->stats;
		}
		br_multicast_port_ctx_init(p, v, &v->port_mcast_ctx);
	} else {
		if (br_vlan_should_use(v)) {
			err = br_switchdev_port_vlan_add(dev, v->vid, flags,
							 extack);
			if (err && err != -EOPNOTSUPP)
				goto out;
		}
		br_multicast_ctx_init(br, v, &v->br_mcast_ctx);
		v->priv_flags |= BR_VLFLAG_GLOBAL_MCAST_ENABLED;
	}

	/* Add the dev mac and count the vlan only if it's usable */
	if (br_vlan_should_use(v)) {
		err = br_fdb_add_local(br, p, dev->dev_addr, v->vid);
		if (err) {
			br_err(br, "failed insert local address into bridge forwarding table\n");
			goto out_filt;
		}
		vg->num_vlans++;
	}

	/* set the state before publishing */
	v->state = BR_STATE_FORWARDING;

	err = rhashtable_lookup_insert_fast(&vg->vlan_hash, &v->vnode,
					    br_vlan_rht_params);
	if (err)
		goto out_fdb_insert;

	__vlan_add_list(v);
	__vlan_flags_commit(v, flags);
	br_multicast_toggle_one_vlan(v, true);

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
		br_multicast_toggle_one_vlan(v, false);
		br_multicast_port_ctx_deinit(&v->port_mcast_ctx);
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

static void __vlan_flush(const struct net_bridge *br,
			 const struct net_bridge_port *p,
			 struct net_bridge_vlan_group *vg)
{
	struct net_bridge_vlan *vlan, *tmp;
	u16 v_start = 0, v_end = 0;
	int err;

	__vlan_delete_pvid(vg, vg->pvid);
	list_for_each_entry_safe(vlan, tmp, &vg->vlan_list, vlist) {
		/* take care of disjoint ranges */
		if (!v_start) {
			v_start = vlan->vid;
		} else if (vlan->vid - v_end != 1) {
			/* found range end, notify and start next one */
			br_vlan_notify(br, p, v_start, v_end, RTM_DELVLAN);
			v_start = vlan->vid;
		}
		v_end = vlan->vid;

		err = __vlan_del(vlan);
		if (err) {
			br_err(br,
			       "port %u(%s) failed to delete vlan %d: %pe\n",
			       (unsigned int) p->port_no, p->dev->name,
			       vlan->vid, ERR_PTR(err));
		}
	}

	/* notify about the last/whole vlan range */
	if (v_start)
		br_vlan_notify(br, p, v_start, v_end, RTM_DELVLAN);
}

struct sk_buff *br_handle_vlan(struct net_bridge *br,
			       const struct net_bridge_port *p,
			       struct net_bridge_vlan_group *vg,
			       struct sk_buff *skb)
{
	struct pcpu_sw_netstats *stats;
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

	/* If the skb will be sent using forwarding offload, the assumption is
	 * that the switchdev will inject the packet into hardware together
	 * with the bridge VLAN, so that it can be forwarded according to that
	 * VLAN. The switchdev should deal with popping the VLAN header in
	 * hardware on each egress port as appropriate. So only strip the VLAN
	 * header if forwarding offload is not being used.
	 */
	if (v->flags & BRIDGE_VLAN_INFO_UNTAGGED &&
	    !br_switchdev_frame_uses_tx_fwd_offload(skb))
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
			      struct sk_buff *skb, u16 *vid,
			      u8 *state,
			      struct net_bridge_vlan **vlan)
{
	struct pcpu_sw_netstats *stats;
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

		/* if snooping and stats are disabled we can avoid the lookup */
		if (!br_opt_get(br, BROPT_MCAST_VLAN_SNOOPING_ENABLED) &&
		    !br_opt_get(br, BROPT_VLAN_STATS_ENABLED)) {
			if (*state == BR_STATE_FORWARDING) {
				*state = br_vlan_get_pvid_state(vg);
				if (!br_vlan_state_allowed(*state, true))
					goto drop;
			}
			return true;
		}
	}
	v = br_vlan_find(vg, *vid);
	if (!v || !br_vlan_should_use(v))
		goto drop;

	if (*state == BR_STATE_FORWARDING) {
		*state = br_vlan_get_state(v);
		if (!br_vlan_state_allowed(*state, true))
			goto drop;
	}

	if (br_opt_get(br, BROPT_VLAN_STATS_ENABLED)) {
		stats = this_cpu_ptr(v->stats);
		u64_stats_update_begin(&stats->syncp);
		stats->rx_bytes += skb->len;
		stats->rx_packets++;
		u64_stats_update_end(&stats->syncp);
	}

	*vlan = v;

	return true;

drop:
	kfree_skb(skb);
	return false;
}

bool br_allowed_ingress(const struct net_bridge *br,
			struct net_bridge_vlan_group *vg, struct sk_buff *skb,
			u16 *vid, u8 *state,
			struct net_bridge_vlan **vlan)
{
	/* If VLAN filtering is disabled on the bridge, all packets are
	 * permitted.
	 */
	*vlan = NULL;
	if (!br_opt_get(br, BROPT_VLAN_ENABLED)) {
		BR_INPUT_SKB_CB(skb)->vlan_filtered = false;
		return true;
	}

	return __allowed_ingress(br, vg, skb, vid, state, vlan);
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
	if (v && br_vlan_should_use(v) &&
	    br_vlan_state_allowed(br_vlan_get_state(v), false))
		return true;

	return false;
}

/* Called under RCU */
bool br_should_learn(struct net_bridge_port *p, struct sk_buff *skb, u16 *vid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge *br = p->br;
	struct net_bridge_vlan *v;

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
		if (!*vid ||
		    !br_vlan_state_allowed(br_vlan_get_pvid_state(vg), true))
			return false;

		return true;
	}

	v = br_vlan_find(vg, *vid);
	if (v && br_vlan_state_allowed(br_vlan_get_state(v), true))
		return true;

	return false;
}

static int br_vlan_add_existing(struct net_bridge *br,
				struct net_bridge_vlan_group *vg,
				struct net_bridge_vlan *vlan,
				u16 flags, bool *changed,
				struct netlink_ext_ack *extack)
{
	bool would_change = __vlan_flags_would_change(vlan, flags);
	bool becomes_brentry = false;
	int err;

	if (!br_vlan_is_brentry(vlan)) {
		/* Trying to change flags of non-existent bridge vlan */
		if (!(flags & BRIDGE_VLAN_INFO_BRENTRY))
			return -EINVAL;

		becomes_brentry = true;
	}

	/* Master VLANs that aren't brentries weren't notified before,
	 * time to notify them now.
	 */
	if (becomes_brentry || would_change) {
		err = br_switchdev_port_vlan_add(br->dev, vlan->vid, flags,
						 extack);
		if (err && err != -EOPNOTSUPP)
			return err;
	}

	if (becomes_brentry) {
		/* It was only kept for port vlans, now make it real */
		err = br_fdb_add_local(br, NULL, br->dev->dev_addr, vlan->vid);
		if (err) {
			br_err(br, "failed to insert local address into bridge forwarding table\n");
			goto err_fdb_insert;
		}

		refcount_inc(&vlan->refcnt);
		vlan->flags |= BRIDGE_VLAN_INFO_BRENTRY;
		vg->num_vlans++;
		*changed = true;
		br_multicast_toggle_one_vlan(vlan, true);
	}

	__vlan_flags_commit(vlan, flags);
	if (would_change)
		*changed = true;

	return 0;

err_fdb_insert:
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

	vlan->stats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
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
	__vlan_flush(br, NULL, vg);
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

int br_vlan_filter_toggle(struct net_bridge *br, unsigned long val,
			  struct netlink_ext_ack *extack)
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

	br_opt_toggle(br, BROPT_VLAN_ENABLED, !!val);

	err = switchdev_port_attr_set(br->dev, &attr, extack);
	if (err && err != -EOPNOTSUPP) {
		br_opt_toggle(br, BROPT_VLAN_ENABLED, !val);
		return err;
	}

	br_manage_promisc(br);
	recalculate_group_addr(br);
	br_recalculate_fwd_mask(br);
	if (!val && br_opt_get(br, BROPT_MCAST_VLAN_SNOOPING_ENABLED)) {
		br_info(br, "vlan filtering disabled, automatically disabling multicast vlan snooping\n");
		br_multicast_toggle_vlan_snooping(br, false, NULL);
	}

	return 0;
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

int __br_vlan_set_proto(struct net_bridge *br, __be16 proto,
			struct netlink_ext_ack *extack)
{
	struct switchdev_attr attr = {
		.orig_dev = br->dev,
		.id = SWITCHDEV_ATTR_ID_BRIDGE_VLAN_PROTOCOL,
		.flags = SWITCHDEV_F_SKIP_EOPNOTSUPP,
		.u.vlan_protocol = ntohs(proto),
	};
	int err = 0;
	struct net_bridge_port *p;
	struct net_bridge_vlan *vlan;
	struct net_bridge_vlan_group *vg;
	__be16 oldproto = br->vlan_proto;

	if (br->vlan_proto == proto)
		return 0;

	err = switchdev_port_attr_set(br->dev, &attr, extack);
	if (err && err != -EOPNOTSUPP)
		return err;

	/* Add VLANs for the new proto to the device filter. */
	list_for_each_entry(p, &br->port_list, list) {
		vg = nbp_vlan_group(p);
		list_for_each_entry(vlan, &vg->vlan_list, vlist) {
			err = vlan_vid_add(p->dev, proto, vlan->vid);
			if (err)
				goto err_filt;
		}
	}

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
	attr.u.vlan_protocol = ntohs(oldproto);
	switchdev_port_attr_set(br->dev, &attr, NULL);

	list_for_each_entry_continue_reverse(vlan, &vg->vlan_list, vlist)
		vlan_vid_del(p->dev, proto, vlan->vid);

	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		vg = nbp_vlan_group(p);
		list_for_each_entry(vlan, &vg->vlan_list, vlist)
			vlan_vid_del(p->dev, proto, vlan->vid);
	}

	return err;
}

int br_vlan_set_proto(struct net_bridge *br, unsigned long val,
		      struct netlink_ext_ack *extack)
{
	if (!eth_type_vlan(htons(val)))
		return -EPROTONOSUPPORT;

	return __br_vlan_set_proto(br, htons(val), extack);
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
	if (vlan_default_pvid(br_vlan_group(br), pvid)) {
		if (!br_vlan_delete(br, pvid))
			br_vlan_notify(br, NULL, pvid, 0, RTM_DELVLAN);
	}

	list_for_each_entry(p, &br->port_list, list) {
		if (vlan_default_pvid(nbp_vlan_group(p), pvid) &&
		    !nbp_vlan_delete(p, pvid))
			br_vlan_notify(br, p, pvid, 0, RTM_DELVLAN);
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

		if (br_vlan_delete(br, old_pvid))
			br_vlan_notify(br, NULL, old_pvid, 0, RTM_DELVLAN);
		br_vlan_notify(br, NULL, pvid, 0, RTM_NEWVLAN);
		__set_bit(0, changed);
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
		if (nbp_vlan_delete(p, old_pvid))
			br_vlan_notify(br, p, old_pvid, 0, RTM_DELVLAN);
		br_vlan_notify(p->br, p, pvid, 0, RTM_NEWVLAN);
		__set_bit(p->port_no, changed);
	}

	br->default_pvid = pvid;

out:
	bitmap_free(changed);
	return err;

err_port:
	list_for_each_entry_continue_reverse(p, &br->port_list, list) {
		if (!test_bit(p->port_no, changed))
			continue;

		if (old_pvid) {
			nbp_vlan_add(p, old_pvid,
				     BRIDGE_VLAN_INFO_PVID |
				     BRIDGE_VLAN_INFO_UNTAGGED,
				     &vlchange, NULL);
			br_vlan_notify(p->br, p, old_pvid, 0, RTM_NEWVLAN);
		}
		nbp_vlan_delete(p, pvid);
		br_vlan_notify(br, p, pvid, 0, RTM_DELVLAN);
	}

	if (test_bit(0, changed)) {
		if (old_pvid) {
			br_vlan_add(br, old_pvid,
				    BRIDGE_VLAN_INFO_PVID |
				    BRIDGE_VLAN_INFO_UNTAGGED |
				    BRIDGE_VLAN_INFO_BRENTRY,
				    &vlchange, NULL);
			br_vlan_notify(br, NULL, old_pvid, 0, RTM_NEWVLAN);
		}
		br_vlan_delete(br, pvid);
		br_vlan_notify(br, NULL, pvid, 0, RTM_DELVLAN);
	}
	goto out;
}

int br_vlan_set_default_pvid(struct net_bridge *br, unsigned long val,
			     struct netlink_ext_ack *extack)
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
	err = __br_vlan_set_default_pvid(br, pvid, extack);
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

	ret = switchdev_port_attr_set(p->dev, &attr, extack);
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
		br_vlan_notify(p->br, p, p->br->default_pvid, 0, RTM_NEWVLAN);
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
		bool would_change = __vlan_flags_would_change(vlan, flags);

		if (would_change) {
			/* Pass the flags to the hardware bridge */
			ret = br_switchdev_port_vlan_add(port->dev, vid,
							 flags, extack);
			if (ret && ret != -EOPNOTSUPP)
				return ret;
		}

		__vlan_flags_commit(vlan, flags);
		*changed = would_change;

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
	__vlan_flush(port->br, port, vg);
	RCU_INIT_POINTER(port->vlgrp, NULL);
	synchronize_rcu();
	__vlan_group_free(vg);
}

void br_vlan_get_stats(const struct net_bridge_vlan *v,
		       struct pcpu_sw_netstats *stats)
{
	int i;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(i) {
		u64 rxpackets, rxbytes, txpackets, txbytes;
		struct pcpu_sw_netstats *cpu_stats;
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

int br_vlan_get_pvid(const struct net_device *dev, u16 *p_pvid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;

	ASSERT_RTNL();
	p = br_port_get_check_rtnl(dev);
	if (p)
		vg = nbp_vlan_group(p);
	else if (netif_is_bridge_master(dev))
		vg = br_vlan_group(netdev_priv(dev));
	else
		return -EINVAL;

	*p_pvid = br_get_pvid(vg);
	return 0;
}
EXPORT_SYMBOL_GPL(br_vlan_get_pvid);

int br_vlan_get_pvid_rcu(const struct net_device *dev, u16 *p_pvid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p;

	p = br_port_get_check_rcu(dev);
	if (p)
		vg = nbp_vlan_group_rcu(p);
	else if (netif_is_bridge_master(dev))
		vg = br_vlan_group_rcu(netdev_priv(dev));
	else
		return -EINVAL;

	*p_pvid = br_get_pvid(vg);
	return 0;
}
EXPORT_SYMBOL_GPL(br_vlan_get_pvid_rcu);

void br_vlan_fill_forward_path_pvid(struct net_bridge *br,
				    struct net_device_path_ctx *ctx,
				    struct net_device_path *path)
{
	struct net_bridge_vlan_group *vg;
	int idx = ctx->num_vlans - 1;
	u16 vid;

	path->bridge.vlan_mode = DEV_PATH_BR_VLAN_KEEP;

	if (!br_opt_get(br, BROPT_VLAN_ENABLED))
		return;

	vg = br_vlan_group(br);

	if (idx >= 0 &&
	    ctx->vlan[idx].proto == br->vlan_proto) {
		vid = ctx->vlan[idx].id;
	} else {
		path->bridge.vlan_mode = DEV_PATH_BR_VLAN_TAG;
		vid = br_get_pvid(vg);
	}

	path->bridge.vlan_id = vid;
	path->bridge.vlan_proto = br->vlan_proto;
}

int br_vlan_fill_forward_path_mode(struct net_bridge *br,
				   struct net_bridge_port *dst,
				   struct net_device_path *path)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;

	if (!br_opt_get(br, BROPT_VLAN_ENABLED))
		return 0;

	vg = nbp_vlan_group_rcu(dst);
	v = br_vlan_find(vg, path->bridge.vlan_id);
	if (!v || !br_vlan_should_use(v))
		return -EINVAL;

	if (!(v->flags & BRIDGE_VLAN_INFO_UNTAGGED))
		return 0;

	if (path->bridge.vlan_mode == DEV_PATH_BR_VLAN_TAG)
		path->bridge.vlan_mode = DEV_PATH_BR_VLAN_KEEP;
	else if (v->priv_flags & BR_VLFLAG_ADDED_BY_SWITCHDEV)
		path->bridge.vlan_mode = DEV_PATH_BR_VLAN_UNTAG_HW;
	else
		path->bridge.vlan_mode = DEV_PATH_BR_VLAN_UNTAG;

	return 0;
}

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

int br_vlan_get_info_rcu(const struct net_device *dev, u16 vid,
			 struct bridge_vlan_info *p_vinfo)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	struct net_bridge_port *p;

	p = br_port_get_check_rcu(dev);
	if (p)
		vg = nbp_vlan_group_rcu(p);
	else if (netif_is_bridge_master(dev))
		vg = br_vlan_group_rcu(netdev_priv(dev));
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
EXPORT_SYMBOL_GPL(br_vlan_get_info_rcu);

static int br_vlan_is_bind_vlan_dev(const struct net_device *dev)
{
	return is_vlan_dev(dev) &&
		!!(vlan_dev_priv(dev)->flags & VLAN_FLAG_BRIDGE_BINDING);
}

static int br_vlan_is_bind_vlan_dev_fn(struct net_device *dev,
			       __always_unused struct netdev_nested_priv *priv)
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
					  struct netdev_nested_priv *priv)
{
	struct br_vlan_bind_walk_data *data = priv->data;
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
	struct netdev_nested_priv priv = {
		.data = (void *)&data,
	};

	rcu_read_lock();
	netdev_walk_all_upper_dev_rcu(dev, br_vlan_match_bind_vlan_dev_fn,
				      &priv);
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
					struct netdev_nested_priv *priv)
{
	struct br_vlan_link_state_walk_data *data = priv->data;

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
	struct netdev_nested_priv priv = {
		.data = (void *)&data,
	};

	rcu_read_lock();
	netdev_walk_all_upper_dev_rcu(dev, br_vlan_link_state_change_fn,
				      &priv);
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
	int vlcmd = 0, ret = 0;
	bool changed = false;

	switch (event) {
	case NETDEV_REGISTER:
		ret = br_vlan_add(br, br->default_pvid,
				  BRIDGE_VLAN_INFO_PVID |
				  BRIDGE_VLAN_INFO_UNTAGGED |
				  BRIDGE_VLAN_INFO_BRENTRY, &changed, NULL);
		vlcmd = RTM_NEWVLAN;
		break;
	case NETDEV_UNREGISTER:
		changed = !br_vlan_delete(br, br->default_pvid);
		vlcmd = RTM_DELVLAN;
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
	if (changed)
		br_vlan_notify(br, NULL, br->default_pvid, 0, vlcmd);

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

static bool br_vlan_stats_fill(struct sk_buff *skb,
			       const struct net_bridge_vlan *v)
{
	struct pcpu_sw_netstats stats;
	struct nlattr *nest;

	nest = nla_nest_start(skb, BRIDGE_VLANDB_ENTRY_STATS);
	if (!nest)
		return false;

	br_vlan_get_stats(v, &stats);
	if (nla_put_u64_64bit(skb, BRIDGE_VLANDB_STATS_RX_BYTES, stats.rx_bytes,
			      BRIDGE_VLANDB_STATS_PAD) ||
	    nla_put_u64_64bit(skb, BRIDGE_VLANDB_STATS_RX_PACKETS,
			      stats.rx_packets, BRIDGE_VLANDB_STATS_PAD) ||
	    nla_put_u64_64bit(skb, BRIDGE_VLANDB_STATS_TX_BYTES, stats.tx_bytes,
			      BRIDGE_VLANDB_STATS_PAD) ||
	    nla_put_u64_64bit(skb, BRIDGE_VLANDB_STATS_TX_PACKETS,
			      stats.tx_packets, BRIDGE_VLANDB_STATS_PAD))
		goto out_err;

	nla_nest_end(skb, nest);

	return true;

out_err:
	nla_nest_cancel(skb, nest);
	return false;
}

/* v_opts is used to dump the options which must be equal in the whole range */
static bool br_vlan_fill_vids(struct sk_buff *skb, u16 vid, u16 vid_range,
			      const struct net_bridge_vlan *v_opts,
			      u16 flags,
			      bool dump_stats)
{
	struct bridge_vlan_info info;
	struct nlattr *nest;

	nest = nla_nest_start(skb, BRIDGE_VLANDB_ENTRY);
	if (!nest)
		return false;

	memset(&info, 0, sizeof(info));
	info.vid = vid;
	if (flags & BRIDGE_VLAN_INFO_UNTAGGED)
		info.flags |= BRIDGE_VLAN_INFO_UNTAGGED;
	if (flags & BRIDGE_VLAN_INFO_PVID)
		info.flags |= BRIDGE_VLAN_INFO_PVID;

	if (nla_put(skb, BRIDGE_VLANDB_ENTRY_INFO, sizeof(info), &info))
		goto out_err;

	if (vid_range && vid < vid_range &&
	    !(flags & BRIDGE_VLAN_INFO_PVID) &&
	    nla_put_u16(skb, BRIDGE_VLANDB_ENTRY_RANGE, vid_range))
		goto out_err;

	if (v_opts) {
		if (!br_vlan_opts_fill(skb, v_opts))
			goto out_err;

		if (dump_stats && !br_vlan_stats_fill(skb, v_opts))
			goto out_err;
	}

	nla_nest_end(skb, nest);

	return true;

out_err:
	nla_nest_cancel(skb, nest);
	return false;
}

static size_t rtnl_vlan_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct br_vlan_msg))
		+ nla_total_size(0) /* BRIDGE_VLANDB_ENTRY */
		+ nla_total_size(sizeof(u16)) /* BRIDGE_VLANDB_ENTRY_RANGE */
		+ nla_total_size(sizeof(struct bridge_vlan_info)) /* BRIDGE_VLANDB_ENTRY_INFO */
		+ br_vlan_opts_nl_size(); /* bridge vlan options */
}

void br_vlan_notify(const struct net_bridge *br,
		    const struct net_bridge_port *p,
		    u16 vid, u16 vid_range,
		    int cmd)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v = NULL;
	struct br_vlan_msg *bvm;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int err = -ENOBUFS;
	struct net *net;
	u16 flags = 0;
	int ifindex;

	/* right now notifications are done only with rtnl held */
	ASSERT_RTNL();

	if (p) {
		ifindex = p->dev->ifindex;
		vg = nbp_vlan_group(p);
		net = dev_net(p->dev);
	} else {
		ifindex = br->dev->ifindex;
		vg = br_vlan_group(br);
		net = dev_net(br->dev);
	}

	skb = nlmsg_new(rtnl_vlan_nlmsg_size(), GFP_KERNEL);
	if (!skb)
		goto out_err;

	err = -EMSGSIZE;
	nlh = nlmsg_put(skb, 0, 0, cmd, sizeof(*bvm), 0);
	if (!nlh)
		goto out_err;
	bvm = nlmsg_data(nlh);
	memset(bvm, 0, sizeof(*bvm));
	bvm->family = AF_BRIDGE;
	bvm->ifindex = ifindex;

	switch (cmd) {
	case RTM_NEWVLAN:
		/* need to find the vlan due to flags/options */
		v = br_vlan_find(vg, vid);
		if (!v || !br_vlan_should_use(v))
			goto out_kfree;

		flags = v->flags;
		if (br_get_pvid(vg) == v->vid)
			flags |= BRIDGE_VLAN_INFO_PVID;
		break;
	case RTM_DELVLAN:
		break;
	default:
		goto out_kfree;
	}

	if (!br_vlan_fill_vids(skb, vid, vid_range, v, flags, false))
		goto out_err;

	nlmsg_end(skb, nlh);
	rtnl_notify(skb, net, 0, RTNLGRP_BRVLAN, NULL, GFP_KERNEL);
	return;

out_err:
	rtnl_set_sk_err(net, RTNLGRP_BRVLAN, err);
out_kfree:
	kfree_skb(skb);
}

/* check if v_curr can enter a range ending in range_end */
bool br_vlan_can_enter_range(const struct net_bridge_vlan *v_curr,
			     const struct net_bridge_vlan *range_end)
{
	return v_curr->vid - range_end->vid == 1 &&
	       range_end->flags == v_curr->flags &&
	       br_vlan_opts_eq_range(v_curr, range_end);
}

static int br_vlan_dump_dev(const struct net_device *dev,
			    struct sk_buff *skb,
			    struct netlink_callback *cb,
			    u32 dump_flags)
{
	struct net_bridge_vlan *v, *range_start = NULL, *range_end = NULL;
	bool dump_global = !!(dump_flags & BRIDGE_VLANDB_DUMPF_GLOBAL);
	bool dump_stats = !!(dump_flags & BRIDGE_VLANDB_DUMPF_STATS);
	struct net_bridge_vlan_group *vg;
	int idx = 0, s_idx = cb->args[1];
	struct nlmsghdr *nlh = NULL;
	struct net_bridge_port *p;
	struct br_vlan_msg *bvm;
	struct net_bridge *br;
	int err = 0;
	u16 pvid;

	if (!netif_is_bridge_master(dev) && !netif_is_bridge_port(dev))
		return -EINVAL;

	if (netif_is_bridge_master(dev)) {
		br = netdev_priv(dev);
		vg = br_vlan_group_rcu(br);
		p = NULL;
	} else {
		/* global options are dumped only for bridge devices */
		if (dump_global)
			return 0;

		p = br_port_get_rcu(dev);
		if (WARN_ON(!p))
			return -EINVAL;
		vg = nbp_vlan_group_rcu(p);
		br = p->br;
	}

	if (!vg)
		return 0;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RTM_NEWVLAN, sizeof(*bvm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;
	bvm = nlmsg_data(nlh);
	memset(bvm, 0, sizeof(*bvm));
	bvm->family = PF_BRIDGE;
	bvm->ifindex = dev->ifindex;
	pvid = br_get_pvid(vg);

	/* idx must stay at range's beginning until it is filled in */
	list_for_each_entry_rcu(v, &vg->vlan_list, vlist) {
		if (!dump_global && !br_vlan_should_use(v))
			continue;
		if (idx < s_idx) {
			idx++;
			continue;
		}

		if (!range_start) {
			range_start = v;
			range_end = v;
			continue;
		}

		if (dump_global) {
			if (br_vlan_global_opts_can_enter_range(v, range_end))
				goto update_end;
			if (!br_vlan_global_opts_fill(skb, range_start->vid,
						      range_end->vid,
						      range_start)) {
				err = -EMSGSIZE;
				break;
			}
			/* advance number of filled vlans */
			idx += range_end->vid - range_start->vid + 1;

			range_start = v;
		} else if (dump_stats || v->vid == pvid ||
			   !br_vlan_can_enter_range(v, range_end)) {
			u16 vlan_flags = br_vlan_flags(range_start, pvid);

			if (!br_vlan_fill_vids(skb, range_start->vid,
					       range_end->vid, range_start,
					       vlan_flags, dump_stats)) {
				err = -EMSGSIZE;
				break;
			}
			/* advance number of filled vlans */
			idx += range_end->vid - range_start->vid + 1;

			range_start = v;
		}
update_end:
		range_end = v;
	}

	/* err will be 0 and range_start will be set in 3 cases here:
	 * - first vlan (range_start == range_end)
	 * - last vlan (range_start == range_end, not in range)
	 * - last vlan range (range_start != range_end, in range)
	 */
	if (!err && range_start) {
		if (dump_global &&
		    !br_vlan_global_opts_fill(skb, range_start->vid,
					      range_end->vid, range_start))
			err = -EMSGSIZE;
		else if (!dump_global &&
			 !br_vlan_fill_vids(skb, range_start->vid,
					    range_end->vid, range_start,
					    br_vlan_flags(range_start, pvid),
					    dump_stats))
			err = -EMSGSIZE;
	}

	cb->args[1] = err ? idx : 0;

	nlmsg_end(skb, nlh);

	return err;
}

static const struct nla_policy br_vlan_db_dump_pol[BRIDGE_VLANDB_DUMP_MAX + 1] = {
	[BRIDGE_VLANDB_DUMP_FLAGS] = { .type = NLA_U32 },
};

static int br_vlan_rtm_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlattr *dtb[BRIDGE_VLANDB_DUMP_MAX + 1];
	int idx = 0, err = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	struct br_vlan_msg *bvm;
	struct net_device *dev;
	u32 dump_flags = 0;

	err = nlmsg_parse(cb->nlh, sizeof(*bvm), dtb, BRIDGE_VLANDB_DUMP_MAX,
			  br_vlan_db_dump_pol, cb->extack);
	if (err < 0)
		return err;

	bvm = nlmsg_data(cb->nlh);
	if (dtb[BRIDGE_VLANDB_DUMP_FLAGS])
		dump_flags = nla_get_u32(dtb[BRIDGE_VLANDB_DUMP_FLAGS]);

	rcu_read_lock();
	if (bvm->ifindex) {
		dev = dev_get_by_index_rcu(net, bvm->ifindex);
		if (!dev) {
			err = -ENODEV;
			goto out_err;
		}
		err = br_vlan_dump_dev(dev, skb, cb, dump_flags);
		/* if the dump completed without an error we return 0 here */
		if (err != -EMSGSIZE)
			goto out_err;
	} else {
		for_each_netdev_rcu(net, dev) {
			if (idx < s_idx)
				goto skip;

			err = br_vlan_dump_dev(dev, skb, cb, dump_flags);
			if (err == -EMSGSIZE)
				break;
skip:
			idx++;
		}
	}
	cb->args[0] = idx;
	rcu_read_unlock();

	return skb->len;

out_err:
	rcu_read_unlock();

	return err;
}

static const struct nla_policy br_vlan_db_policy[BRIDGE_VLANDB_ENTRY_MAX + 1] = {
	[BRIDGE_VLANDB_ENTRY_INFO]	=
		NLA_POLICY_EXACT_LEN(sizeof(struct bridge_vlan_info)),
	[BRIDGE_VLANDB_ENTRY_RANGE]	= { .type = NLA_U16 },
	[BRIDGE_VLANDB_ENTRY_STATE]	= { .type = NLA_U8 },
	[BRIDGE_VLANDB_ENTRY_TUNNEL_INFO] = { .type = NLA_NESTED },
	[BRIDGE_VLANDB_ENTRY_MCAST_ROUTER]	= { .type = NLA_U8 },
};

static int br_vlan_rtm_process_one(struct net_device *dev,
				   const struct nlattr *attr,
				   int cmd, struct netlink_ext_ack *extack)
{
	struct bridge_vlan_info *vinfo, vrange_end, *vinfo_last = NULL;
	struct nlattr *tb[BRIDGE_VLANDB_ENTRY_MAX + 1];
	bool changed = false, skip_processing = false;
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p = NULL;
	int err = 0, cmdmap = 0;
	struct net_bridge *br;

	if (netif_is_bridge_master(dev)) {
		br = netdev_priv(dev);
		vg = br_vlan_group(br);
	} else {
		p = br_port_get_rtnl(dev);
		if (WARN_ON(!p))
			return -ENODEV;
		br = p->br;
		vg = nbp_vlan_group(p);
	}

	if (WARN_ON(!vg))
		return -ENODEV;

	err = nla_parse_nested(tb, BRIDGE_VLANDB_ENTRY_MAX, attr,
			       br_vlan_db_policy, extack);
	if (err)
		return err;

	if (!tb[BRIDGE_VLANDB_ENTRY_INFO]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing vlan entry info");
		return -EINVAL;
	}
	memset(&vrange_end, 0, sizeof(vrange_end));

	vinfo = nla_data(tb[BRIDGE_VLANDB_ENTRY_INFO]);
	if (vinfo->flags & (BRIDGE_VLAN_INFO_RANGE_BEGIN |
			    BRIDGE_VLAN_INFO_RANGE_END)) {
		NL_SET_ERR_MSG_MOD(extack, "Old-style vlan ranges are not allowed when using RTM vlan calls");
		return -EINVAL;
	}
	if (!br_vlan_valid_id(vinfo->vid, extack))
		return -EINVAL;

	if (tb[BRIDGE_VLANDB_ENTRY_RANGE]) {
		vrange_end.vid = nla_get_u16(tb[BRIDGE_VLANDB_ENTRY_RANGE]);
		/* validate user-provided flags without RANGE_BEGIN */
		vrange_end.flags = BRIDGE_VLAN_INFO_RANGE_END | vinfo->flags;
		vinfo->flags |= BRIDGE_VLAN_INFO_RANGE_BEGIN;

		/* vinfo_last is the range start, vinfo the range end */
		vinfo_last = vinfo;
		vinfo = &vrange_end;

		if (!br_vlan_valid_id(vinfo->vid, extack) ||
		    !br_vlan_valid_range(vinfo, vinfo_last, extack))
			return -EINVAL;
	}

	switch (cmd) {
	case RTM_NEWVLAN:
		cmdmap = RTM_SETLINK;
		skip_processing = !!(vinfo->flags & BRIDGE_VLAN_INFO_ONLY_OPTS);
		break;
	case RTM_DELVLAN:
		cmdmap = RTM_DELLINK;
		break;
	}

	if (!skip_processing) {
		struct bridge_vlan_info *tmp_last = vinfo_last;

		/* br_process_vlan_info may overwrite vinfo_last */
		err = br_process_vlan_info(br, p, cmdmap, vinfo, &tmp_last,
					   &changed, extack);

		/* notify first if anything changed */
		if (changed)
			br_ifinfo_notify(cmdmap, br, p);

		if (err)
			return err;
	}

	/* deal with options */
	if (cmd == RTM_NEWVLAN) {
		struct net_bridge_vlan *range_start, *range_end;

		if (vinfo_last) {
			range_start = br_vlan_find(vg, vinfo_last->vid);
			range_end = br_vlan_find(vg, vinfo->vid);
		} else {
			range_start = br_vlan_find(vg, vinfo->vid);
			range_end = range_start;
		}

		err = br_vlan_process_options(br, p, range_start, range_end,
					      tb, extack);
	}

	return err;
}

static int br_vlan_rtm_process(struct sk_buff *skb, struct nlmsghdr *nlh,
			       struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct br_vlan_msg *bvm;
	struct net_device *dev;
	struct nlattr *attr;
	int err, vlans = 0;
	int rem;

	/* this should validate the header and check for remaining bytes */
	err = nlmsg_parse(nlh, sizeof(*bvm), NULL, BRIDGE_VLANDB_MAX, NULL,
			  extack);
	if (err < 0)
		return err;

	bvm = nlmsg_data(nlh);
	dev = __dev_get_by_index(net, bvm->ifindex);
	if (!dev)
		return -ENODEV;

	if (!netif_is_bridge_master(dev) && !netif_is_bridge_port(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "The device is not a valid bridge or bridge port");
		return -EINVAL;
	}

	nlmsg_for_each_attr(attr, nlh, sizeof(*bvm), rem) {
		switch (nla_type(attr)) {
		case BRIDGE_VLANDB_ENTRY:
			err = br_vlan_rtm_process_one(dev, attr,
						      nlh->nlmsg_type,
						      extack);
			break;
		case BRIDGE_VLANDB_GLOBAL_OPTIONS:
			err = br_vlan_rtm_process_global_options(dev, attr,
								 nlh->nlmsg_type,
								 extack);
			break;
		default:
			continue;
		}

		vlans++;
		if (err)
			break;
	}
	if (!vlans) {
		NL_SET_ERR_MSG_MOD(extack, "No vlans found to process");
		err = -EINVAL;
	}

	return err;
}

void br_vlan_rtnl_init(void)
{
	rtnl_register_module(THIS_MODULE, PF_BRIDGE, RTM_GETVLAN, NULL,
			     br_vlan_rtm_dump, 0);
	rtnl_register_module(THIS_MODULE, PF_BRIDGE, RTM_NEWVLAN,
			     br_vlan_rtm_process, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_BRIDGE, RTM_DELVLAN,
			     br_vlan_rtm_process, NULL, 0);
}

void br_vlan_rtnl_uninit(void)
{
	rtnl_unregister(PF_BRIDGE, RTM_GETVLAN);
	rtnl_unregister(PF_BRIDGE, RTM_NEWVLAN);
	rtnl_unregister(PF_BRIDGE, RTM_DELVLAN);
}
