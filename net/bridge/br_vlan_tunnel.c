// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Bridge per vlan tunnel port dst_metadata handling code
 *
 *	Authors:
 *	Roopa Prabhu		<roopa@cumulusnetworks.com>
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/switchdev.h>
#include <net/dst_metadata.h>

#include "br_private.h"
#include "br_private_tunnel.h"

static inline int br_vlan_tunid_cmp(struct rhashtable_compare_arg *arg,
				    const void *ptr)
{
	const struct net_bridge_vlan *vle = ptr;
	__be64 tunid = *(__be64 *)arg->key;

	return vle->tinfo.tunnel_id != tunid;
}

static const struct rhashtable_params br_vlan_tunnel_rht_params = {
	.head_offset = offsetof(struct net_bridge_vlan, tnode),
	.key_offset = offsetof(struct net_bridge_vlan, tinfo.tunnel_id),
	.key_len = sizeof(__be64),
	.nelem_hint = 3,
	.obj_cmpfn = br_vlan_tunid_cmp,
	.automatic_shrinking = true,
};

static struct net_bridge_vlan *br_vlan_tunnel_lookup(struct rhashtable *tbl,
						     u64 tunnel_id)
{
	return rhashtable_lookup_fast(tbl, &tunnel_id,
				      br_vlan_tunnel_rht_params);
}

static void vlan_tunnel_info_release(struct net_bridge_vlan *vlan)
{
	struct metadata_dst *tdst = rtnl_dereference(vlan->tinfo.tunnel_dst);

	WRITE_ONCE(vlan->tinfo.tunnel_id, 0);
	RCU_INIT_POINTER(vlan->tinfo.tunnel_dst, NULL);
	dst_release(&tdst->dst);
}

void vlan_tunnel_info_del(struct net_bridge_vlan_group *vg,
			  struct net_bridge_vlan *vlan)
{
	if (!rcu_access_pointer(vlan->tinfo.tunnel_dst))
		return;
	rhashtable_remove_fast(&vg->tunnel_hash, &vlan->tnode,
			       br_vlan_tunnel_rht_params);
	vlan_tunnel_info_release(vlan);
}

static int __vlan_tunnel_info_add(struct net_bridge_vlan_group *vg,
				  struct net_bridge_vlan *vlan, u32 tun_id)
{
	struct metadata_dst *metadata = rtnl_dereference(vlan->tinfo.tunnel_dst);
	__be64 key = key32_to_tunnel_id(cpu_to_be32(tun_id));
	int err;

	if (metadata)
		return -EEXIST;

	metadata = __ip_tun_set_dst(0, 0, 0, 0, 0, TUNNEL_KEY,
				    key, 0);
	if (!metadata)
		return -EINVAL;

	metadata->u.tun_info.mode |= IP_TUNNEL_INFO_TX | IP_TUNNEL_INFO_BRIDGE;
	rcu_assign_pointer(vlan->tinfo.tunnel_dst, metadata);
	WRITE_ONCE(vlan->tinfo.tunnel_id, key);

	err = rhashtable_lookup_insert_fast(&vg->tunnel_hash, &vlan->tnode,
					    br_vlan_tunnel_rht_params);
	if (err)
		goto out;

	return 0;
out:
	vlan_tunnel_info_release(vlan);

	return err;
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int nbp_vlan_tunnel_info_add(const struct net_bridge_port *port, u16 vid,
			     u32 tun_id)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *vlan;

	ASSERT_RTNL();

	vg = nbp_vlan_group(port);
	vlan = br_vlan_find(vg, vid);
	if (!vlan)
		return -EINVAL;

	return __vlan_tunnel_info_add(vg, vlan, tun_id);
}

/* Must be protected by RTNL.
 * Must be called with vid in range from 1 to 4094 inclusive.
 */
int nbp_vlan_tunnel_info_delete(const struct net_bridge_port *port, u16 vid)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;

	ASSERT_RTNL();

	vg = nbp_vlan_group(port);
	v = br_vlan_find(vg, vid);
	if (!v)
		return -ENOENT;

	vlan_tunnel_info_del(vg, v);

	return 0;
}

static void __vlan_tunnel_info_flush(struct net_bridge_vlan_group *vg)
{
	struct net_bridge_vlan *vlan, *tmp;

	list_for_each_entry_safe(vlan, tmp, &vg->vlan_list, vlist)
		vlan_tunnel_info_del(vg, vlan);
}

void nbp_vlan_tunnel_info_flush(struct net_bridge_port *port)
{
	struct net_bridge_vlan_group *vg;

	ASSERT_RTNL();

	vg = nbp_vlan_group(port);
	__vlan_tunnel_info_flush(vg);
}

int vlan_tunnel_init(struct net_bridge_vlan_group *vg)
{
	return rhashtable_init(&vg->tunnel_hash, &br_vlan_tunnel_rht_params);
}

void vlan_tunnel_deinit(struct net_bridge_vlan_group *vg)
{
	rhashtable_destroy(&vg->tunnel_hash);
}

int br_handle_ingress_vlan_tunnel(struct sk_buff *skb,
				  struct net_bridge_port *p,
				  struct net_bridge_vlan_group *vg)
{
	struct ip_tunnel_info *tinfo = skb_tunnel_info(skb);
	struct net_bridge_vlan *vlan;

	if (!vg || !tinfo)
		return 0;

	/* if already tagged, ignore */
	if (skb_vlan_tagged(skb))
		return 0;

	/* lookup vid, given tunnel id */
	vlan = br_vlan_tunnel_lookup(&vg->tunnel_hash, tinfo->key.tun_id);
	if (!vlan)
		return 0;

	skb_dst_drop(skb);

	__vlan_hwaccel_put_tag(skb, p->br->vlan_proto, vlan->vid);

	return 0;
}

int br_handle_egress_vlan_tunnel(struct sk_buff *skb,
				 struct net_bridge_vlan *vlan)
{
	struct metadata_dst *tunnel_dst;
	__be64 tunnel_id;
	int err;

	if (!vlan)
		return 0;

	tunnel_id = READ_ONCE(vlan->tinfo.tunnel_id);
	if (!tunnel_id || unlikely(!skb_vlan_tag_present(skb)))
		return 0;

	skb_dst_drop(skb);
	err = skb_vlan_pop(skb);
	if (err)
		return err;

	tunnel_dst = rcu_dereference(vlan->tinfo.tunnel_dst);
	if (tunnel_dst)
		skb_dst_set(skb, dst_clone(&tunnel_dst->dst));

	return 0;
}
