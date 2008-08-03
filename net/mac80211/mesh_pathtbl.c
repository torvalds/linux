/*
 * Copyright (c) 2008 open80211s Ltd.
 * Author:     Luis Carlos Cobo <luisca@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "mesh.h"

/* There will be initially 2^INIT_PATHS_SIZE_ORDER buckets */
#define INIT_PATHS_SIZE_ORDER	2

/* Keep the mean chain length below this constant */
#define MEAN_CHAIN_LEN		2

#define MPATH_EXPIRED(mpath) ((mpath->flags & MESH_PATH_ACTIVE) && \
				time_after(jiffies, mpath->exp_time) && \
				!(mpath->flags & MESH_PATH_FIXED))

struct mpath_node {
	struct hlist_node list;
	struct rcu_head rcu;
	/* This indirection allows two different tables to point to the same
	 * mesh_path structure, useful when resizing
	 */
	struct mesh_path *mpath;
};

static struct mesh_table *mesh_paths;

/* This lock will have the grow table function as writer and add / delete nodes
 * as readers. When reading the table (i.e. doing lookups) we are well protected
 * by RCU
 */
static DEFINE_RWLOCK(pathtbl_resize_lock);

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
	rcu_assign_pointer(mpath->next_hop, sta);
}


/**
 * mesh_path_lookup - look up a path in the mesh path table
 * @dst: hardware address (ETH_ALEN length) of destination
 * @sdata: local subif
 *
 * Returns: pointer to the mesh path structure, or NULL if not found
 *
 * Locking: must be called within a read rcu section.
 */
struct mesh_path *mesh_path_lookup(u8 *dst, struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct hlist_node *n;
	struct hlist_head *bucket;
	struct mesh_table *tbl;
	struct mpath_node *node;

	tbl = rcu_dereference(mesh_paths);

	bucket = &tbl->hash_buckets[mesh_table_hash(dst, sdata, tbl)];
	hlist_for_each_entry_rcu(node, n, bucket, list) {
		mpath = node->mpath;
		if (mpath->sdata == sdata &&
				memcmp(dst, mpath->dst, ETH_ALEN) == 0) {
			if (MPATH_EXPIRED(mpath)) {
				spin_lock_bh(&mpath->state_lock);
				if (MPATH_EXPIRED(mpath))
					mpath->flags &= ~MESH_PATH_ACTIVE;
				spin_unlock_bh(&mpath->state_lock);
			}
			return mpath;
		}
	}
	return NULL;
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
struct mesh_path *mesh_path_lookup_by_idx(int idx, struct ieee80211_sub_if_data *sdata)
{
	struct mpath_node *node;
	struct hlist_node *p;
	int i;
	int j = 0;

	for_each_mesh_entry(mesh_paths, p, node, i) {
		if (sdata && node->mpath->sdata != sdata)
			continue;
		if (j++ == idx) {
			if (MPATH_EXPIRED(node->mpath)) {
				spin_lock_bh(&node->mpath->state_lock);
				if (MPATH_EXPIRED(node->mpath))
					node->mpath->flags &= ~MESH_PATH_ACTIVE;
				spin_unlock_bh(&node->mpath->state_lock);
			}
			return node->mpath;
		}
	}

	return NULL;
}

/**
 * mesh_path_add - allocate and add a new path to the mesh path table
 * @addr: destination address of the path (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 on sucess
 *
 * State: the initial state of the new path is set to 0
 */
int mesh_path_add(u8 *dst, struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath, *new_mpath;
	struct mpath_node *node, *new_node;
	struct hlist_head *bucket;
	struct hlist_node *n;
	int grow = 0;
	int err = 0;
	u32 hash_idx;

	if (memcmp(dst, sdata->dev->dev_addr, ETH_ALEN) == 0)
		/* never add ourselves as neighbours */
		return -ENOTSUPP;

	if (is_multicast_ether_addr(dst))
		return -ENOTSUPP;

	if (atomic_add_unless(&sdata->u.sta.mpaths, 1, MESH_MAX_MPATHS) == 0)
		return -ENOSPC;

	err = -ENOMEM;
	new_mpath = kzalloc(sizeof(struct mesh_path), GFP_KERNEL);
	if (!new_mpath)
		goto err_path_alloc;

	new_node = kmalloc(sizeof(struct mpath_node), GFP_KERNEL);
	if (!new_node)
		goto err_node_alloc;

	read_lock(&pathtbl_resize_lock);
	memcpy(new_mpath->dst, dst, ETH_ALEN);
	new_mpath->sdata = sdata;
	new_mpath->flags = 0;
	skb_queue_head_init(&new_mpath->frame_queue);
	new_node->mpath = new_mpath;
	new_mpath->timer.data = (unsigned long) new_mpath;
	new_mpath->timer.function = mesh_path_timer;
	new_mpath->exp_time = jiffies;
	spin_lock_init(&new_mpath->state_lock);
	init_timer(&new_mpath->timer);

	hash_idx = mesh_table_hash(dst, sdata, mesh_paths);
	bucket = &mesh_paths->hash_buckets[hash_idx];

	spin_lock(&mesh_paths->hashwlock[hash_idx]);

	err = -EEXIST;
	hlist_for_each_entry(node, n, bucket, list) {
		mpath = node->mpath;
		if (mpath->sdata == sdata && memcmp(dst, mpath->dst, ETH_ALEN) == 0)
			goto err_exists;
	}

	hlist_add_head_rcu(&new_node->list, bucket);
	if (atomic_inc_return(&mesh_paths->entries) >=
		mesh_paths->mean_chain_len * (mesh_paths->hash_mask + 1))
		grow = 1;

	spin_unlock(&mesh_paths->hashwlock[hash_idx]);
	read_unlock(&pathtbl_resize_lock);
	if (grow) {
		struct mesh_table *oldtbl, *newtbl;

		write_lock(&pathtbl_resize_lock);
		oldtbl = mesh_paths;
		newtbl = mesh_table_grow(mesh_paths);
		if (!newtbl) {
			write_unlock(&pathtbl_resize_lock);
			return 0;
		}
		rcu_assign_pointer(mesh_paths, newtbl);
		write_unlock(&pathtbl_resize_lock);

		synchronize_rcu();
		mesh_table_free(oldtbl, false);
	}
	return 0;

err_exists:
	spin_unlock(&mesh_paths->hashwlock[hash_idx]);
	read_unlock(&pathtbl_resize_lock);
	kfree(new_node);
err_node_alloc:
	kfree(new_mpath);
err_path_alloc:
	atomic_dec(&sdata->u.sta.mpaths);
	return err;
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
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	int i;

	rcu_read_lock();
	for_each_mesh_entry(mesh_paths, p, node, i) {
		mpath = node->mpath;
		spin_lock_bh(&mpath->state_lock);
		if (mpath->next_hop == sta &&
		    mpath->flags & MESH_PATH_ACTIVE &&
		    !(mpath->flags & MESH_PATH_FIXED)) {
			mpath->flags &= ~MESH_PATH_ACTIVE;
			++mpath->dsn;
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_error_tx(mpath->dst,
					cpu_to_le32(mpath->dsn),
					sdata->dev->broadcast, sdata);
		} else
		spin_unlock_bh(&mpath->state_lock);
	}
	rcu_read_unlock();
}

/**
 * mesh_path_flush_by_nexthop - Deletes mesh paths if their next hop matches
 *
 * @sta - mesh peer to match
 *
 * RCU notes: this function is called when a mesh plink transitions from
 * PLINK_ESTAB to any other state, since PLINK_ESTAB state is the only one that
 * allows path creation. This will happen before the sta can be freed (because
 * sta_info_destroy() calls this) so any reader in a rcu read block will be
 * protected against the plink disappearing.
 */
void mesh_path_flush_by_nexthop(struct sta_info *sta)
{
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	for_each_mesh_entry(mesh_paths, p, node, i) {
		mpath = node->mpath;
		if (mpath->next_hop == sta)
			mesh_path_del(mpath->dst, mpath->sdata);
	}
}

void mesh_path_flush(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	for_each_mesh_entry(mesh_paths, p, node, i) {
		mpath = node->mpath;
		if (mpath->sdata == sdata)
			mesh_path_del(mpath->dst, mpath->sdata);
	}
}

static void mesh_path_node_reclaim(struct rcu_head *rp)
{
	struct mpath_node *node = container_of(rp, struct mpath_node, rcu);
	struct ieee80211_sub_if_data *sdata = node->mpath->sdata;

	del_timer_sync(&node->mpath->timer);
	atomic_dec(&sdata->u.sta.mpaths);
	kfree(node->mpath);
	kfree(node);
}

/**
 * mesh_path_del - delete a mesh path from the table
 *
 * @addr: dst address (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 if succesful
 */
int mesh_path_del(u8 *addr, struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_head *bucket;
	struct hlist_node *n;
	int hash_idx;
	int err = 0;

	read_lock(&pathtbl_resize_lock);
	hash_idx = mesh_table_hash(addr, sdata, mesh_paths);
	bucket = &mesh_paths->hash_buckets[hash_idx];

	spin_lock(&mesh_paths->hashwlock[hash_idx]);
	hlist_for_each_entry(node, n, bucket, list) {
		mpath = node->mpath;
		if (mpath->sdata == sdata &&
				memcmp(addr, mpath->dst, ETH_ALEN) == 0) {
			spin_lock_bh(&mpath->state_lock);
			mpath->flags |= MESH_PATH_RESOLVING;
			hlist_del_rcu(&node->list);
			call_rcu(&node->rcu, mesh_path_node_reclaim);
			atomic_dec(&mesh_paths->entries);
			spin_unlock_bh(&mpath->state_lock);
			goto enddel;
		}
	}

	err = -ENXIO;
enddel:
	spin_unlock(&mesh_paths->hashwlock[hash_idx]);
	read_unlock(&pathtbl_resize_lock);
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
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&mpath->frame_queue)) &&
			(mpath->flags & MESH_PATH_ACTIVE))
		dev_queue_xmit(skb);
}

/**
 * mesh_path_discard_frame - discard a frame whose path could not be resolved
 *
 * @skb: frame to discard
 * @sdata: network subif the frame was to be sent through
 *
 * If the frame was beign forwarded from another MP, a PERR frame will be sent
 * to the precursor.
 *
 * Locking: the function must me called within a rcu_read_lock region
 */
void mesh_path_discard_frame(struct sk_buff *skb,
			     struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct mesh_path *mpath;
	u32 dsn = 0;

	if (memcmp(hdr->addr4, sdata->dev->dev_addr, ETH_ALEN) != 0) {
		u8 *ra, *da;

		da = hdr->addr3;
		ra = hdr->addr2;
		mpath = mesh_path_lookup(da, sdata);
		if (mpath)
			dsn = ++mpath->dsn;
		mesh_path_error_tx(skb->data, cpu_to_le32(dsn), ra, sdata);
	}

	kfree_skb(skb);
	sdata->u.sta.mshstats.dropped_frames_no_route++;
}

/**
 * mesh_path_flush_pending - free the pending queue of a mesh path
 *
 * @mpath: mesh path whose queue has to be freed
 *
 * Locking: the function must me called withing a rcu_read_lock region
 */
void mesh_path_flush_pending(struct mesh_path *mpath)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&mpath->frame_queue)) &&
			(mpath->flags & MESH_PATH_ACTIVE))
		mesh_path_discard_frame(skb, mpath->sdata);
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
	mpath->dsn = 0xffff;
	mpath->metric = 0;
	mpath->hop_count = 0;
	mpath->exp_time = 0;
	mpath->flags |= MESH_PATH_FIXED;
	mesh_path_activate(mpath);
	spin_unlock_bh(&mpath->state_lock);
	mesh_path_tx_pending(mpath);
}

static void mesh_path_node_free(struct hlist_node *p, bool free_leafs)
{
	struct mesh_path *mpath;
	struct mpath_node *node = hlist_entry(p, struct mpath_node, list);
	mpath = node->mpath;
	hlist_del_rcu(p);
	if (free_leafs)
		kfree(mpath);
	kfree(node);
}

static int mesh_path_node_copy(struct hlist_node *p, struct mesh_table *newtbl)
{
	struct mesh_path *mpath;
	struct mpath_node *node, *new_node;
	u32 hash_idx;

	new_node = kmalloc(sizeof(struct mpath_node), GFP_ATOMIC);
	if (new_node == NULL)
		return -ENOMEM;

	node = hlist_entry(p, struct mpath_node, list);
	mpath = node->mpath;
	new_node->mpath = mpath;
	hash_idx = mesh_table_hash(mpath->dst, mpath->sdata, newtbl);
	hlist_add_head(&new_node->list,
			&newtbl->hash_buckets[hash_idx]);
	return 0;
}

int mesh_pathtbl_init(void)
{
	mesh_paths = mesh_table_alloc(INIT_PATHS_SIZE_ORDER);
	mesh_paths->free_node = &mesh_path_node_free;
	mesh_paths->copy_node = &mesh_path_node_copy;
	mesh_paths->mean_chain_len = MEAN_CHAIN_LEN;
	if (!mesh_paths)
		return -ENOMEM;
	return 0;
}

void mesh_path_expire(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	read_lock(&pathtbl_resize_lock);
	for_each_mesh_entry(mesh_paths, p, node, i) {
		if (node->mpath->sdata != sdata)
			continue;
		mpath = node->mpath;
		spin_lock_bh(&mpath->state_lock);
		if ((!(mpath->flags & MESH_PATH_RESOLVING)) &&
		    (!(mpath->flags & MESH_PATH_FIXED)) &&
			time_after(jiffies,
			 mpath->exp_time + MESH_PATH_EXPIRE)) {
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_del(mpath->dst, mpath->sdata);
		} else
			spin_unlock_bh(&mpath->state_lock);
	}
	read_unlock(&pathtbl_resize_lock);
}

void mesh_pathtbl_unregister(void)
{
	mesh_table_free(mesh_paths, true);
}
