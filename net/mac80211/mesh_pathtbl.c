/*
 * Copyright (c) 2008, 2009 open80211s Ltd.
 * Author:     Luis Carlos Cobo <luisca@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <net/mac80211.h>
#include "wme.h"
#include "ieee80211_i.h"
#include "mesh.h"

static void mesh_path_free_rcu(struct mesh_table *tbl, struct mesh_path *mpath);

static u32 mesh_table_hash(const void *addr, u32 len, u32 seed)
{
	/* Use last four bytes of hw addr as hash index */
	return jhash_1word(*(u32 *)(addr+2), seed);
}

static const struct rhashtable_params mesh_rht_params = {
	.nelem_hint = 2,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct mesh_path, dst),
	.head_offset = offsetof(struct mesh_path, rhash),
	.hashfn = mesh_table_hash,
};

static inline bool mpath_expired(struct mesh_path *mpath)
{
	return (mpath->flags & MESH_PATH_ACTIVE) &&
	       time_after(jiffies, mpath->exp_time) &&
	       !(mpath->flags & MESH_PATH_FIXED);
}

static void mesh_path_rht_free(void *ptr, void *tblptr)
{
	struct mesh_path *mpath = ptr;
	struct mesh_table *tbl = tblptr;

	mesh_path_free_rcu(tbl, mpath);
}

static struct mesh_table *mesh_table_alloc(void)
{
	struct mesh_table *newtbl;

	newtbl = kmalloc(sizeof(struct mesh_table), GFP_ATOMIC);
	if (!newtbl)
		return NULL;

	newtbl->known_gates = kzalloc(sizeof(struct hlist_head), GFP_ATOMIC);
	if (!newtbl->known_gates) {
		kfree(newtbl);
		return NULL;
	}
	INIT_HLIST_HEAD(newtbl->known_gates);
	atomic_set(&newtbl->entries,  0);
	spin_lock_init(&newtbl->gates_lock);

	return newtbl;
}

static void mesh_table_free(struct mesh_table *tbl)
{
	rhashtable_free_and_destroy(&tbl->rhead,
				    mesh_path_rht_free, tbl);
	kfree(tbl);
}

/**
 *
 * mesh_path_assign_nexthop - update mesh path next hop
 *
 * @mpath: mesh path to update
 * @sta: next hop to assign
 *
 * Locking: mpath->state_lock must be held when calling this function
 */
void mesh_path_assign_nexthop(struct mesh_path *mpath, struct sta_info *sta)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	unsigned long flags;

	rcu_assign_pointer(mpath->next_hop, sta);

	spin_lock_irqsave(&mpath->frame_queue.lock, flags);
	skb_queue_walk(&mpath->frame_queue, skb) {
		hdr = (struct ieee80211_hdr *) skb->data;
		memcpy(hdr->addr1, sta->sta.addr, ETH_ALEN);
		memcpy(hdr->addr2, mpath->sdata->vif.addr, ETH_ALEN);
		ieee80211_mps_set_frame_flags(sta->sdata, sta, hdr);
	}

	spin_unlock_irqrestore(&mpath->frame_queue.lock, flags);
}

static void prepare_for_gate(struct sk_buff *skb, char *dst_addr,
			     struct mesh_path *gate_mpath)
{
	struct ieee80211_hdr *hdr;
	struct ieee80211s_hdr *mshdr;
	int mesh_hdrlen, hdrlen;
	char *next_hop;

	hdr = (struct ieee80211_hdr *) skb->data;
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	mshdr = (struct ieee80211s_hdr *) (skb->data + hdrlen);

	if (!(mshdr->flags & MESH_FLAGS_AE)) {
		/* size of the fixed part of the mesh header */
		mesh_hdrlen = 6;

		/* make room for the two extended addresses */
		skb_push(skb, 2 * ETH_ALEN);
		memmove(skb->data, hdr, hdrlen + mesh_hdrlen);

		hdr = (struct ieee80211_hdr *) skb->data;

		/* we preserve the previous mesh header and only add
		 * the new addreses */
		mshdr = (struct ieee80211s_hdr *) (skb->data + hdrlen);
		mshdr->flags = MESH_FLAGS_AE_A5_A6;
		memcpy(mshdr->eaddr1, hdr->addr3, ETH_ALEN);
		memcpy(mshdr->eaddr2, hdr->addr4, ETH_ALEN);
	}

	/* update next hop */
	hdr = (struct ieee80211_hdr *) skb->data;
	rcu_read_lock();
	next_hop = rcu_dereference(gate_mpath->next_hop)->sta.addr;
	memcpy(hdr->addr1, next_hop, ETH_ALEN);
	rcu_read_unlock();
	memcpy(hdr->addr2, gate_mpath->sdata->vif.addr, ETH_ALEN);
	memcpy(hdr->addr3, dst_addr, ETH_ALEN);
}

/**
 *
 * mesh_path_move_to_queue - Move or copy frames from one mpath queue to another
 *
 * This function is used to transfer or copy frames from an unresolved mpath to
 * a gate mpath.  The function also adds the Address Extension field and
 * updates the next hop.
 *
 * If a frame already has an Address Extension field, only the next hop and
 * destination addresses are updated.
 *
 * The gate mpath must be an active mpath with a valid mpath->next_hop.
 *
 * @mpath: An active mpath the frames will be sent to (i.e. the gate)
 * @from_mpath: The failed mpath
 * @copy: When true, copy all the frames to the new mpath queue.  When false,
 * move them.
 */
static void mesh_path_move_to_queue(struct mesh_path *gate_mpath,
				    struct mesh_path *from_mpath,
				    bool copy)
{
	struct sk_buff *skb, *fskb, *tmp;
	struct sk_buff_head failq;
	unsigned long flags;

	if (WARN_ON(gate_mpath == from_mpath))
		return;
	if (WARN_ON(!gate_mpath->next_hop))
		return;

	__skb_queue_head_init(&failq);

	spin_lock_irqsave(&from_mpath->frame_queue.lock, flags);
	skb_queue_splice_init(&from_mpath->frame_queue, &failq);
	spin_unlock_irqrestore(&from_mpath->frame_queue.lock, flags);

	skb_queue_walk_safe(&failq, fskb, tmp) {
		if (skb_queue_len(&gate_mpath->frame_queue) >=
				  MESH_FRAME_QUEUE_LEN) {
			mpath_dbg(gate_mpath->sdata, "mpath queue full!\n");
			break;
		}

		skb = skb_copy(fskb, GFP_ATOMIC);
		if (WARN_ON(!skb))
			break;

		prepare_for_gate(skb, gate_mpath->dst, gate_mpath);
		skb_queue_tail(&gate_mpath->frame_queue, skb);

		if (copy)
			continue;

		__skb_unlink(fskb, &failq);
		kfree_skb(fskb);
	}

	mpath_dbg(gate_mpath->sdata, "Mpath queue for gate %pM has %d frames\n",
		  gate_mpath->dst, skb_queue_len(&gate_mpath->frame_queue));

	if (!copy)
		return;

	spin_lock_irqsave(&from_mpath->frame_queue.lock, flags);
	skb_queue_splice(&failq, &from_mpath->frame_queue);
	spin_unlock_irqrestore(&from_mpath->frame_queue.lock, flags);
}


static struct mesh_path *mpath_lookup(struct mesh_table *tbl, const u8 *dst,
				      struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;

	mpath = rhashtable_lookup_fast(&tbl->rhead, dst, mesh_rht_params);

	if (mpath && mpath_expired(mpath)) {
		spin_lock_bh(&mpath->state_lock);
		mpath->flags &= ~MESH_PATH_ACTIVE;
		spin_unlock_bh(&mpath->state_lock);
	}
	return mpath;
}

/**
 * mesh_path_lookup - look up a path in the mesh path table
 * @sdata: local subif
 * @dst: hardware address (ETH_ALEN length) of destination
 *
 * Returns: pointer to the mesh path structure, or NULL if not found
 *
 * Locking: must be called within a read rcu section.
 */
struct mesh_path *
mesh_path_lookup(struct ieee80211_sub_if_data *sdata, const u8 *dst)
{
	return mpath_lookup(sdata->u.mesh.mesh_paths, dst, sdata);
}

struct mesh_path *
mpp_path_lookup(struct ieee80211_sub_if_data *sdata, const u8 *dst)
{
	return mpath_lookup(sdata->u.mesh.mpp_paths, dst, sdata);
}

static struct mesh_path *
__mesh_path_lookup_by_idx(struct mesh_table *tbl, int idx)
{
	int i = 0, ret;
	struct mesh_path *mpath = NULL;
	struct rhashtable_iter iter;

	ret = rhashtable_walk_init(&tbl->rhead, &iter, GFP_ATOMIC);
	if (ret)
		return NULL;

	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto err;

	while ((mpath = rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		if (i++ == idx)
			break;
	}
err:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);

	if (IS_ERR(mpath) || !mpath)
		return NULL;

	if (mpath_expired(mpath)) {
		spin_lock_bh(&mpath->state_lock);
		mpath->flags &= ~MESH_PATH_ACTIVE;
		spin_unlock_bh(&mpath->state_lock);
	}
	return mpath;
}

/**
 * mesh_path_lookup_by_idx - look up a path in the mesh path table by its index
 * @idx: index
 * @sdata: local subif, or NULL for all entries
 *
 * Returns: pointer to the mesh path structure, or NULL if not found.
 *
 * Locking: must be called within a read rcu section.
 */
struct mesh_path *
mesh_path_lookup_by_idx(struct ieee80211_sub_if_data *sdata, int idx)
{
	return __mesh_path_lookup_by_idx(sdata->u.mesh.mesh_paths, idx);
}

/**
 * mpp_path_lookup_by_idx - look up a path in the proxy path table by its index
 * @idx: index
 * @sdata: local subif, or NULL for all entries
 *
 * Returns: pointer to the proxy path structure, or NULL if not found.
 *
 * Locking: must be called within a read rcu section.
 */
struct mesh_path *
mpp_path_lookup_by_idx(struct ieee80211_sub_if_data *sdata, int idx)
{
	return __mesh_path_lookup_by_idx(sdata->u.mesh.mpp_paths, idx);
}

/**
 * mesh_path_add_gate - add the given mpath to a mesh gate to our path table
 * @mpath: gate path to add to table
 */
int mesh_path_add_gate(struct mesh_path *mpath)
{
	struct mesh_table *tbl;
	int err;

	rcu_read_lock();
	tbl = mpath->sdata->u.mesh.mesh_paths;

	spin_lock_bh(&mpath->state_lock);
	if (mpath->is_gate) {
		err = -EEXIST;
		spin_unlock_bh(&mpath->state_lock);
		goto err_rcu;
	}
	mpath->is_gate = true;
	mpath->sdata->u.mesh.num_gates++;

	spin_lock(&tbl->gates_lock);
	hlist_add_head_rcu(&mpath->gate_list, tbl->known_gates);
	spin_unlock(&tbl->gates_lock);

	spin_unlock_bh(&mpath->state_lock);

	mpath_dbg(mpath->sdata,
		  "Mesh path: Recorded new gate: %pM. %d known gates\n",
		  mpath->dst, mpath->sdata->u.mesh.num_gates);
	err = 0;
err_rcu:
	rcu_read_unlock();
	return err;
}

/**
 * mesh_gate_del - remove a mesh gate from the list of known gates
 * @tbl: table which holds our list of known gates
 * @mpath: gate mpath
 */
static void mesh_gate_del(struct mesh_table *tbl, struct mesh_path *mpath)
{
	lockdep_assert_held(&mpath->state_lock);
	if (!mpath->is_gate)
		return;

	mpath->is_gate = false;
	spin_lock_bh(&tbl->gates_lock);
	hlist_del_rcu(&mpath->gate_list);
	mpath->sdata->u.mesh.num_gates--;
	spin_unlock_bh(&tbl->gates_lock);

	mpath_dbg(mpath->sdata,
		  "Mesh path: Deleted gate: %pM. %d known gates\n",
		  mpath->dst, mpath->sdata->u.mesh.num_gates);
}

/**
 * mesh_gate_num - number of gates known to this interface
 * @sdata: subif data
 */
int mesh_gate_num(struct ieee80211_sub_if_data *sdata)
{
	return sdata->u.mesh.num_gates;
}

static
struct mesh_path *mesh_path_new(struct ieee80211_sub_if_data *sdata,
				const u8 *dst, gfp_t gfp_flags)
{
	struct mesh_path *new_mpath;

	new_mpath = kzalloc(sizeof(struct mesh_path), gfp_flags);
	if (!new_mpath)
		return NULL;

	memcpy(new_mpath->dst, dst, ETH_ALEN);
	eth_broadcast_addr(new_mpath->rann_snd_addr);
	new_mpath->is_root = false;
	new_mpath->sdata = sdata;
	new_mpath->flags = 0;
	skb_queue_head_init(&new_mpath->frame_queue);
	new_mpath->timer.data = (unsigned long) new_mpath;
	new_mpath->timer.function = mesh_path_timer;
	new_mpath->exp_time = jiffies;
	spin_lock_init(&new_mpath->state_lock);
	init_timer(&new_mpath->timer);

	return new_mpath;
}

/**
 * mesh_path_add - allocate and add a new path to the mesh path table
 * @dst: destination address of the path (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 on success
 *
 * State: the initial state of the new path is set to 0
 */
struct mesh_path *mesh_path_add(struct ieee80211_sub_if_data *sdata,
				const u8 *dst)
{
	struct mesh_table *tbl;
	struct mesh_path *mpath, *new_mpath;
	int ret;

	if (ether_addr_equal(dst, sdata->vif.addr))
		/* never add ourselves as neighbours */
		return ERR_PTR(-ENOTSUPP);

	if (is_multicast_ether_addr(dst))
		return ERR_PTR(-ENOTSUPP);

	if (atomic_add_unless(&sdata->u.mesh.mpaths, 1, MESH_MAX_MPATHS) == 0)
		return ERR_PTR(-ENOSPC);

	new_mpath = mesh_path_new(sdata, dst, GFP_ATOMIC);
	if (!new_mpath)
		return ERR_PTR(-ENOMEM);

	tbl = sdata->u.mesh.mesh_paths;
	do {
		ret = rhashtable_lookup_insert_fast(&tbl->rhead,
						    &new_mpath->rhash,
						    mesh_rht_params);

		if (ret == -EEXIST)
			mpath = rhashtable_lookup_fast(&tbl->rhead,
						       dst,
						       mesh_rht_params);

	} while (unlikely(ret == -EEXIST && !mpath));

	if (ret && ret != -EEXIST)
		return ERR_PTR(ret);

	/* At this point either new_mpath was added, or we found a
	 * matching entry already in the table; in the latter case
	 * free the unnecessary new entry.
	 */
	if (ret == -EEXIST) {
		kfree(new_mpath);
		new_mpath = mpath;
	}
	sdata->u.mesh.mesh_paths_generation++;
	return new_mpath;
}

int mpp_path_add(struct ieee80211_sub_if_data *sdata,
		 const u8 *dst, const u8 *mpp)
{
	struct mesh_table *tbl;
	struct mesh_path *new_mpath;
	int ret;

	if (ether_addr_equal(dst, sdata->vif.addr))
		/* never add ourselves as neighbours */
		return -ENOTSUPP;

	if (is_multicast_ether_addr(dst))
		return -ENOTSUPP;

	new_mpath = mesh_path_new(sdata, dst, GFP_ATOMIC);

	if (!new_mpath)
		return -ENOMEM;

	memcpy(new_mpath->mpp, mpp, ETH_ALEN);
	tbl = sdata->u.mesh.mpp_paths;
	ret = rhashtable_lookup_insert_fast(&tbl->rhead,
					    &new_mpath->rhash,
					    mesh_rht_params);

	sdata->u.mesh.mpp_paths_generation++;
	return ret;
}


/**
 * mesh_plink_broken - deactivates paths and sends perr when a link breaks
 *
 * @sta: broken peer link
 *
 * This function must be called from the rate control algorithm if enough
 * delivery errors suggest that a peer link is no longer usable.
 */
void mesh_plink_broken(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct mesh_table *tbl = sdata->u.mesh.mesh_paths;
	static const u8 bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct mesh_path *mpath;
	struct rhashtable_iter iter;
	int ret;

	ret = rhashtable_walk_init(&tbl->rhead, &iter, GFP_ATOMIC);
	if (ret)
		return;

	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		if (rcu_access_pointer(mpath->next_hop) == sta &&
		    mpath->flags & MESH_PATH_ACTIVE &&
		    !(mpath->flags & MESH_PATH_FIXED)) {
			spin_lock_bh(&mpath->state_lock);
			mpath->flags &= ~MESH_PATH_ACTIVE;
			++mpath->sn;
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_error_tx(sdata,
				sdata->u.mesh.mshcfg.element_ttl,
				mpath->dst, mpath->sn,
				WLAN_REASON_MESH_PATH_DEST_UNREACHABLE, bcast);
		}
	}
out:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

static void mesh_path_free_rcu(struct mesh_table *tbl,
			       struct mesh_path *mpath)
{
	struct ieee80211_sub_if_data *sdata = mpath->sdata;

	spin_lock_bh(&mpath->state_lock);
	mpath->flags |= MESH_PATH_RESOLVING | MESH_PATH_DELETED;
	mesh_gate_del(tbl, mpath);
	spin_unlock_bh(&mpath->state_lock);
	del_timer_sync(&mpath->timer);
	atomic_dec(&sdata->u.mesh.mpaths);
	atomic_dec(&tbl->entries);
	kfree_rcu(mpath, rcu);
}

static void __mesh_path_del(struct mesh_table *tbl, struct mesh_path *mpath)
{
	rhashtable_remove_fast(&tbl->rhead, &mpath->rhash, mesh_rht_params);
	mesh_path_free_rcu(tbl, mpath);
}

/**
 * mesh_path_flush_by_nexthop - Deletes mesh paths if their next hop matches
 *
 * @sta: mesh peer to match
 *
 * RCU notes: this function is called when a mesh plink transitions from
 * PLINK_ESTAB to any other state, since PLINK_ESTAB state is the only one that
 * allows path creation. This will happen before the sta can be freed (because
 * sta_info_destroy() calls this) so any reader in a rcu read block will be
 * protected against the plink disappearing.
 */
void mesh_path_flush_by_nexthop(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct mesh_table *tbl = sdata->u.mesh.mesh_paths;
	struct mesh_path *mpath;
	struct rhashtable_iter iter;
	int ret;

	ret = rhashtable_walk_init(&tbl->rhead, &iter, GFP_ATOMIC);
	if (ret)
		return;

	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;

		if (rcu_access_pointer(mpath->next_hop) == sta)
			__mesh_path_del(tbl, mpath);
	}
out:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

static void mpp_flush_by_proxy(struct ieee80211_sub_if_data *sdata,
			       const u8 *proxy)
{
	struct mesh_table *tbl = sdata->u.mesh.mpp_paths;
	struct mesh_path *mpath;
	struct rhashtable_iter iter;
	int ret;

	ret = rhashtable_walk_init(&tbl->rhead, &iter, GFP_ATOMIC);
	if (ret)
		return;

	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;

		if (ether_addr_equal(mpath->mpp, proxy))
			__mesh_path_del(tbl, mpath);
	}
out:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

static void table_flush_by_iface(struct mesh_table *tbl)
{
	struct mesh_path *mpath;
	struct rhashtable_iter iter;
	int ret;

	ret = rhashtable_walk_init(&tbl->rhead, &iter, GFP_ATOMIC);
	if (ret)
		return;

	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		__mesh_path_del(tbl, mpath);
	}
out:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

/**
 * mesh_path_flush_by_iface - Deletes all mesh paths associated with a given iface
 *
 * This function deletes both mesh paths as well as mesh portal paths.
 *
 * @sdata: interface data to match
 *
 */
void mesh_path_flush_by_iface(struct ieee80211_sub_if_data *sdata)
{
	table_flush_by_iface(sdata->u.mesh.mesh_paths);
	table_flush_by_iface(sdata->u.mesh.mpp_paths);
}

/**
 * table_path_del - delete a path from the mesh or mpp table
 *
 * @tbl: mesh or mpp path table
 * @sdata: local subif
 * @addr: dst address (ETH_ALEN length)
 *
 * Returns: 0 if successful
 */
static int table_path_del(struct mesh_table *tbl,
			  struct ieee80211_sub_if_data *sdata,
			  const u8 *addr)
{
	struct mesh_path *mpath;

	rcu_read_lock();
	mpath = rhashtable_lookup_fast(&tbl->rhead, addr, mesh_rht_params);
	if (!mpath) {
		rcu_read_unlock();
		return -ENXIO;
	}

	__mesh_path_del(tbl, mpath);
	rcu_read_unlock();
	return 0;
}


/**
 * mesh_path_del - delete a mesh path from the table
 *
 * @addr: dst address (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 if successful
 */
int mesh_path_del(struct ieee80211_sub_if_data *sdata, const u8 *addr)
{
	int err;

	/* flush relevant mpp entries first */
	mpp_flush_by_proxy(sdata, addr);

	err = table_path_del(sdata->u.mesh.mesh_paths, sdata, addr);
	sdata->u.mesh.mesh_paths_generation++;
	return err;
}

/**
 * mesh_path_tx_pending - sends pending frames in a mesh path queue
 *
 * @mpath: mesh path to activate
 *
 * Locking: the state_lock of the mpath structure must NOT be held when calling
 * this function.
 */
void mesh_path_tx_pending(struct mesh_path *mpath)
{
	if (mpath->flags & MESH_PATH_ACTIVE)
		ieee80211_add_pending_skbs(mpath->sdata->local,
				&mpath->frame_queue);
}

/**
 * mesh_path_send_to_gates - sends pending frames to all known mesh gates
 *
 * @mpath: mesh path whose queue will be emptied
 *
 * If there is only one gate, the frames are transferred from the failed mpath
 * queue to that gate's queue.  If there are more than one gates, the frames
 * are copied from each gate to the next.  After frames are copied, the
 * mpath queues are emptied onto the transmission queue.
 */
int mesh_path_send_to_gates(struct mesh_path *mpath)
{
	struct ieee80211_sub_if_data *sdata = mpath->sdata;
	struct mesh_table *tbl;
	struct mesh_path *from_mpath = mpath;
	struct mesh_path *gate;
	bool copy = false;
	struct hlist_head *known_gates;

	tbl = sdata->u.mesh.mesh_paths;
	known_gates = tbl->known_gates;

	if (!known_gates)
		return -EHOSTUNREACH;

	rcu_read_lock();
	hlist_for_each_entry_rcu(gate, known_gates, gate_list) {
		if (gate->flags & MESH_PATH_ACTIVE) {
			mpath_dbg(sdata, "Forwarding to %pM\n", gate->dst);
			mesh_path_move_to_queue(gate, from_mpath, copy);
			from_mpath = gate;
			copy = true;
		} else {
			mpath_dbg(sdata,
				  "Not forwarding to %pM (flags %#x)\n",
				  gate->dst, gate->flags);
		}
	}

	hlist_for_each_entry_rcu(gate, known_gates, gate_list) {
		mpath_dbg(sdata, "Sending to %pM\n", gate->dst);
		mesh_path_tx_pending(gate);
	}
	rcu_read_unlock();

	return (from_mpath == mpath) ? -EHOSTUNREACH : 0;
}

/**
 * mesh_path_discard_frame - discard a frame whose path could not be resolved
 *
 * @skb: frame to discard
 * @sdata: network subif the frame was to be sent through
 *
 * Locking: the function must me called within a rcu_read_lock region
 */
void mesh_path_discard_frame(struct ieee80211_sub_if_data *sdata,
			     struct sk_buff *skb)
{
	kfree_skb(skb);
	sdata->u.mesh.mshstats.dropped_frames_no_route++;
}

/**
 * mesh_path_flush_pending - free the pending queue of a mesh path
 *
 * @mpath: mesh path whose queue has to be freed
 *
 * Locking: the function must me called within a rcu_read_lock region
 */
void mesh_path_flush_pending(struct mesh_path *mpath)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&mpath->frame_queue)) != NULL)
		mesh_path_discard_frame(mpath->sdata, skb);
}

/**
 * mesh_path_fix_nexthop - force a specific next hop for a mesh path
 *
 * @mpath: the mesh path to modify
 * @next_hop: the next hop to force
 *
 * Locking: this function must be called holding mpath->state_lock
 */
void mesh_path_fix_nexthop(struct mesh_path *mpath, struct sta_info *next_hop)
{
	spin_lock_bh(&mpath->state_lock);
	mesh_path_assign_nexthop(mpath, next_hop);
	mpath->sn = 0xffff;
	mpath->metric = 0;
	mpath->hop_count = 0;
	mpath->exp_time = 0;
	mpath->flags |= MESH_PATH_FIXED;
	mesh_path_activate(mpath);
	spin_unlock_bh(&mpath->state_lock);
	mesh_path_tx_pending(mpath);
}

int mesh_pathtbl_init(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_table *tbl_path, *tbl_mpp;
	int ret;

	tbl_path = mesh_table_alloc();
	if (!tbl_path)
		return -ENOMEM;

	tbl_mpp = mesh_table_alloc();
	if (!tbl_mpp) {
		ret = -ENOMEM;
		goto free_path;
	}

	rhashtable_init(&tbl_path->rhead, &mesh_rht_params);
	rhashtable_init(&tbl_mpp->rhead, &mesh_rht_params);

	sdata->u.mesh.mesh_paths = tbl_path;
	sdata->u.mesh.mpp_paths = tbl_mpp;

	return 0;

free_path:
	mesh_table_free(tbl_path);
	return ret;
}

static
void mesh_path_tbl_expire(struct ieee80211_sub_if_data *sdata,
			  struct mesh_table *tbl)
{
	struct mesh_path *mpath;
	struct rhashtable_iter iter;
	int ret;

	ret = rhashtable_walk_init(&tbl->rhead, &iter, GFP_KERNEL);
	if (ret)
		return;

	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		if ((!(mpath->flags & MESH_PATH_RESOLVING)) &&
		    (!(mpath->flags & MESH_PATH_FIXED)) &&
		     time_after(jiffies, mpath->exp_time + MESH_PATH_EXPIRE))
			__mesh_path_del(tbl, mpath);
	}
out:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

void mesh_path_expire(struct ieee80211_sub_if_data *sdata)
{
	mesh_path_tbl_expire(sdata, sdata->u.mesh.mesh_paths);
	mesh_path_tbl_expire(sdata, sdata->u.mesh.mpp_paths);
}

void mesh_pathtbl_unregister(struct ieee80211_sub_if_data *sdata)
{
	mesh_table_free(sdata->u.mesh.mesh_paths);
	mesh_table_free(sdata->u.mesh.mpp_paths);
}
