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
#include "ieee80211_i.h"
#include "mesh.h"

#ifdef CONFIG_MAC80211_VERBOSE_MPATH_DEBUG
#define mpath_dbg(fmt, args...)	printk(KERN_DEBUG fmt, ##args)
#else
#define mpath_dbg(fmt, args...)	do { (void)(0); } while (0)
#endif

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

static struct mesh_table __rcu *mesh_paths;
static struct mesh_table __rcu *mpp_paths; /* Store paths for MPP&MAP */

int mesh_paths_generation;

/* This lock will have the grow table function as writer and add / delete nodes
 * as readers. When reading the table (i.e. doing lookups) we are well protected
 * by RCU
 */
static DEFINE_RWLOCK(pathtbl_resize_lock);


static inline struct mesh_table *resize_dereference_mesh_paths(void)
{
	return rcu_dereference_protected(mesh_paths,
		lockdep_is_held(&pathtbl_resize_lock));
}

static inline struct mesh_table *resize_dereference_mpp_paths(void)
{
	return rcu_dereference_protected(mpp_paths,
		lockdep_is_held(&pathtbl_resize_lock));
}

/*
 * CAREFUL -- "tbl" must not be an expression,
 * in particular not an rcu_dereference(), since
 * it's used twice. So it is illegal to do
 *	for_each_mesh_entry(rcu_dereference(...), ...)
 */
#define for_each_mesh_entry(tbl, p, node, i) \
	for (i = 0; i <= tbl->hash_mask; i++) \
		hlist_for_each_entry_rcu(node, p, &tbl->hash_buckets[i], list)


static struct mesh_table *mesh_table_alloc(int size_order)
{
	int i;
	struct mesh_table *newtbl;

	newtbl = kmalloc(sizeof(struct mesh_table), GFP_ATOMIC);
	if (!newtbl)
		return NULL;

	newtbl->hash_buckets = kzalloc(sizeof(struct hlist_head) *
			(1 << size_order), GFP_ATOMIC);

	if (!newtbl->hash_buckets) {
		kfree(newtbl);
		return NULL;
	}

	newtbl->hashwlock = kmalloc(sizeof(spinlock_t) *
			(1 << size_order), GFP_ATOMIC);
	if (!newtbl->hashwlock) {
		kfree(newtbl->hash_buckets);
		kfree(newtbl);
		return NULL;
	}

	newtbl->size_order = size_order;
	newtbl->hash_mask = (1 << size_order) - 1;
	atomic_set(&newtbl->entries,  0);
	get_random_bytes(&newtbl->hash_rnd,
			sizeof(newtbl->hash_rnd));
	for (i = 0; i <= newtbl->hash_mask; i++)
		spin_lock_init(&newtbl->hashwlock[i]);

	return newtbl;
}

static void __mesh_table_free(struct mesh_table *tbl)
{
	kfree(tbl->hash_buckets);
	kfree(tbl->hashwlock);
	kfree(tbl);
}

static void mesh_table_free(struct mesh_table *tbl, bool free_leafs)
{
	struct hlist_head *mesh_hash;
	struct hlist_node *p, *q;
	int i;

	mesh_hash = tbl->hash_buckets;
	for (i = 0; i <= tbl->hash_mask; i++) {
		spin_lock_bh(&tbl->hashwlock[i]);
		hlist_for_each_safe(p, q, &mesh_hash[i]) {
			tbl->free_node(p, free_leafs);
			atomic_dec(&tbl->entries);
		}
		spin_unlock_bh(&tbl->hashwlock[i]);
	}
	__mesh_table_free(tbl);
}

static int mesh_table_grow(struct mesh_table *oldtbl,
			   struct mesh_table *newtbl)
{
	struct hlist_head *oldhash;
	struct hlist_node *p, *q;
	int i;

	if (atomic_read(&oldtbl->entries)
			< oldtbl->mean_chain_len * (oldtbl->hash_mask + 1))
		return -EAGAIN;

	newtbl->free_node = oldtbl->free_node;
	newtbl->mean_chain_len = oldtbl->mean_chain_len;
	newtbl->copy_node = oldtbl->copy_node;
	atomic_set(&newtbl->entries, atomic_read(&oldtbl->entries));

	oldhash = oldtbl->hash_buckets;
	for (i = 0; i <= oldtbl->hash_mask; i++)
		hlist_for_each(p, &oldhash[i])
			if (oldtbl->copy_node(p, newtbl) < 0)
				goto errcopy;

	return 0;

errcopy:
	for (i = 0; i <= newtbl->hash_mask; i++) {
		hlist_for_each_safe(p, q, &newtbl->hash_buckets[i])
			oldtbl->free_node(p, 0);
	}
	return -ENOMEM;
}

static u32 mesh_table_hash(u8 *addr, struct ieee80211_sub_if_data *sdata,
			   struct mesh_table *tbl)
{
	/* Use last four bytes of hw addr and interface index as hash index */
	return jhash_2words(*(u32 *)(addr+2), sdata->dev->ifindex, tbl->hash_rnd)
		& tbl->hash_mask;
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
	struct sk_buff_head tmpq;
	unsigned long flags;

	rcu_assign_pointer(mpath->next_hop, sta);

	__skb_queue_head_init(&tmpq);

	spin_lock_irqsave(&mpath->frame_queue.lock, flags);

	while ((skb = __skb_dequeue(&mpath->frame_queue)) != NULL) {
		hdr = (struct ieee80211_hdr *) skb->data;
		memcpy(hdr->addr1, sta->sta.addr, ETH_ALEN);
		__skb_queue_tail(&tmpq, skb);
	}

	skb_queue_splice(&tmpq, &mpath->frame_queue);
	spin_unlock_irqrestore(&mpath->frame_queue.lock, flags);
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

struct mesh_path *mpp_path_lookup(u8 *dst, struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct hlist_node *n;
	struct hlist_head *bucket;
	struct mesh_table *tbl;
	struct mpath_node *node;

	tbl = rcu_dereference(mpp_paths);

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
	struct mesh_table *tbl = rcu_dereference(mesh_paths);
	struct mpath_node *node;
	struct hlist_node *p;
	int i;
	int j = 0;

	for_each_mesh_entry(tbl, p, node, i) {
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
 * Returns: 0 on success
 *
 * State: the initial state of the new path is set to 0
 */
int mesh_path_add(u8 *dst, struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_local *local = sdata->local;
	struct mesh_table *tbl;
	struct mesh_path *mpath, *new_mpath;
	struct mpath_node *node, *new_node;
	struct hlist_head *bucket;
	struct hlist_node *n;
	int grow = 0;
	int err = 0;
	u32 hash_idx;

	if (memcmp(dst, sdata->vif.addr, ETH_ALEN) == 0)
		/* never add ourselves as neighbours */
		return -ENOTSUPP;

	if (is_multicast_ether_addr(dst))
		return -ENOTSUPP;

	if (atomic_add_unless(&sdata->u.mesh.mpaths, 1, MESH_MAX_MPATHS) == 0)
		return -ENOSPC;

	err = -ENOMEM;
	new_mpath = kzalloc(sizeof(struct mesh_path), GFP_ATOMIC);
	if (!new_mpath)
		goto err_path_alloc;

	new_node = kmalloc(sizeof(struct mpath_node), GFP_ATOMIC);
	if (!new_node)
		goto err_node_alloc;

	read_lock_bh(&pathtbl_resize_lock);
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

	tbl = resize_dereference_mesh_paths();

	hash_idx = mesh_table_hash(dst, sdata, tbl);
	bucket = &tbl->hash_buckets[hash_idx];

	spin_lock_bh(&tbl->hashwlock[hash_idx]);

	err = -EEXIST;
	hlist_for_each_entry(node, n, bucket, list) {
		mpath = node->mpath;
		if (mpath->sdata == sdata && memcmp(dst, mpath->dst, ETH_ALEN) == 0)
			goto err_exists;
	}

	hlist_add_head_rcu(&new_node->list, bucket);
	if (atomic_inc_return(&tbl->entries) >=
	    tbl->mean_chain_len * (tbl->hash_mask + 1))
		grow = 1;

	mesh_paths_generation++;

	spin_unlock_bh(&tbl->hashwlock[hash_idx]);
	read_unlock_bh(&pathtbl_resize_lock);
	if (grow) {
		set_bit(MESH_WORK_GROW_MPATH_TABLE,  &ifmsh->wrkq_flags);
		ieee80211_queue_work(&local->hw, &sdata->work);
	}
	return 0;

err_exists:
	spin_unlock_bh(&tbl->hashwlock[hash_idx]);
	read_unlock_bh(&pathtbl_resize_lock);
	kfree(new_node);
err_node_alloc:
	kfree(new_mpath);
err_path_alloc:
	atomic_dec(&sdata->u.mesh.mpaths);
	return err;
}

static void mesh_table_free_rcu(struct rcu_head *rcu)
{
	struct mesh_table *tbl = container_of(rcu, struct mesh_table, rcu_head);

	mesh_table_free(tbl, false);
}

void mesh_mpath_table_grow(void)
{
	struct mesh_table *oldtbl, *newtbl;

	write_lock_bh(&pathtbl_resize_lock);
	oldtbl = resize_dereference_mesh_paths();
	newtbl = mesh_table_alloc(oldtbl->size_order + 1);
	if (!newtbl)
		goto out;
	if (mesh_table_grow(oldtbl, newtbl) < 0) {
		__mesh_table_free(newtbl);
		goto out;
	}
	rcu_assign_pointer(mesh_paths, newtbl);

	call_rcu(&oldtbl->rcu_head, mesh_table_free_rcu);

 out:
	write_unlock_bh(&pathtbl_resize_lock);
}

void mesh_mpp_table_grow(void)
{
	struct mesh_table *oldtbl, *newtbl;

	write_lock_bh(&pathtbl_resize_lock);
	oldtbl = resize_dereference_mpp_paths();
	newtbl = mesh_table_alloc(oldtbl->size_order + 1);
	if (!newtbl)
		goto out;
	if (mesh_table_grow(oldtbl, newtbl) < 0) {
		__mesh_table_free(newtbl);
		goto out;
	}
	rcu_assign_pointer(mpp_paths, newtbl);
	call_rcu(&oldtbl->rcu_head, mesh_table_free_rcu);

 out:
	write_unlock_bh(&pathtbl_resize_lock);
}

int mpp_path_add(u8 *dst, u8 *mpp, struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	struct ieee80211_local *local = sdata->local;
	struct mesh_table *tbl;
	struct mesh_path *mpath, *new_mpath;
	struct mpath_node *node, *new_node;
	struct hlist_head *bucket;
	struct hlist_node *n;
	int grow = 0;
	int err = 0;
	u32 hash_idx;

	if (memcmp(dst, sdata->vif.addr, ETH_ALEN) == 0)
		/* never add ourselves as neighbours */
		return -ENOTSUPP;

	if (is_multicast_ether_addr(dst))
		return -ENOTSUPP;

	err = -ENOMEM;
	new_mpath = kzalloc(sizeof(struct mesh_path), GFP_ATOMIC);
	if (!new_mpath)
		goto err_path_alloc;

	new_node = kmalloc(sizeof(struct mpath_node), GFP_ATOMIC);
	if (!new_node)
		goto err_node_alloc;

	read_lock_bh(&pathtbl_resize_lock);
	memcpy(new_mpath->dst, dst, ETH_ALEN);
	memcpy(new_mpath->mpp, mpp, ETH_ALEN);
	new_mpath->sdata = sdata;
	new_mpath->flags = 0;
	skb_queue_head_init(&new_mpath->frame_queue);
	new_node->mpath = new_mpath;
	new_mpath->exp_time = jiffies;
	spin_lock_init(&new_mpath->state_lock);

	tbl = resize_dereference_mpp_paths();

	hash_idx = mesh_table_hash(dst, sdata, tbl);
	bucket = &tbl->hash_buckets[hash_idx];

	spin_lock_bh(&tbl->hashwlock[hash_idx]);

	err = -EEXIST;
	hlist_for_each_entry(node, n, bucket, list) {
		mpath = node->mpath;
		if (mpath->sdata == sdata && memcmp(dst, mpath->dst, ETH_ALEN) == 0)
			goto err_exists;
	}

	hlist_add_head_rcu(&new_node->list, bucket);
	if (atomic_inc_return(&tbl->entries) >=
	    tbl->mean_chain_len * (tbl->hash_mask + 1))
		grow = 1;

	spin_unlock_bh(&tbl->hashwlock[hash_idx]);
	read_unlock_bh(&pathtbl_resize_lock);
	if (grow) {
		set_bit(MESH_WORK_GROW_MPP_TABLE,  &ifmsh->wrkq_flags);
		ieee80211_queue_work(&local->hw, &sdata->work);
	}
	return 0;

err_exists:
	spin_unlock_bh(&tbl->hashwlock[hash_idx]);
	read_unlock_bh(&pathtbl_resize_lock);
	kfree(new_node);
err_node_alloc:
	kfree(new_mpath);
err_path_alloc:
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
	struct mesh_table *tbl;
	static const u8 bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	int i;
	__le16 reason = cpu_to_le16(WLAN_REASON_MESH_PATH_DEST_UNREACHABLE);

	rcu_read_lock();
	tbl = rcu_dereference(mesh_paths);
	for_each_mesh_entry(tbl, p, node, i) {
		mpath = node->mpath;
		spin_lock_bh(&mpath->state_lock);
		if (rcu_dereference(mpath->next_hop) == sta &&
		    mpath->flags & MESH_PATH_ACTIVE &&
		    !(mpath->flags & MESH_PATH_FIXED)) {
			mpath->flags &= ~MESH_PATH_ACTIVE;
			++mpath->sn;
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_error_tx(sdata->u.mesh.mshcfg.element_ttl,
					mpath->dst, cpu_to_le32(mpath->sn),
					reason, bcast, sdata);
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
	struct mesh_table *tbl;
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	rcu_read_lock();
	tbl = rcu_dereference(mesh_paths);
	for_each_mesh_entry(tbl, p, node, i) {
		mpath = node->mpath;
		if (rcu_dereference(mpath->next_hop) == sta)
			mesh_path_del(mpath->dst, mpath->sdata);
	}
	rcu_read_unlock();
}

void mesh_path_flush(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_table *tbl;
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	rcu_read_lock();
	tbl = rcu_dereference(mesh_paths);
	for_each_mesh_entry(tbl, p, node, i) {
		mpath = node->mpath;
		if (mpath->sdata == sdata)
			mesh_path_del(mpath->dst, mpath->sdata);
	}
	rcu_read_unlock();
}

static void mesh_path_node_reclaim(struct rcu_head *rp)
{
	struct mpath_node *node = container_of(rp, struct mpath_node, rcu);
	struct ieee80211_sub_if_data *sdata = node->mpath->sdata;

	if (node->mpath->timer.function)
		del_timer_sync(&node->mpath->timer);
	atomic_dec(&sdata->u.mesh.mpaths);
	kfree(node->mpath);
	kfree(node);
}

/**
 * mesh_path_del - delete a mesh path from the table
 *
 * @addr: dst address (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 if successful
 */
int mesh_path_del(u8 *addr, struct ieee80211_sub_if_data *sdata)
{
	struct mesh_table *tbl;
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_head *bucket;
	struct hlist_node *n;
	int hash_idx;
	int err = 0;

	read_lock_bh(&pathtbl_resize_lock);
	tbl = resize_dereference_mesh_paths();
	hash_idx = mesh_table_hash(addr, sdata, tbl);
	bucket = &tbl->hash_buckets[hash_idx];

	spin_lock_bh(&tbl->hashwlock[hash_idx]);
	hlist_for_each_entry(node, n, bucket, list) {
		mpath = node->mpath;
		if (mpath->sdata == sdata &&
		    memcmp(addr, mpath->dst, ETH_ALEN) == 0) {
			spin_lock(&mpath->state_lock);
			mpath->flags |= MESH_PATH_RESOLVING;
			hlist_del_rcu(&node->list);
			call_rcu(&node->rcu, mesh_path_node_reclaim);
			atomic_dec(&tbl->entries);
			spin_unlock(&mpath->state_lock);
			goto enddel;
		}
	}

	err = -ENXIO;
enddel:
	mesh_paths_generation++;
	spin_unlock_bh(&tbl->hashwlock[hash_idx]);
	read_unlock_bh(&pathtbl_resize_lock);
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
 * mesh_path_discard_frame - discard a frame whose path could not be resolved
 *
 * @skb: frame to discard
 * @sdata: network subif the frame was to be sent through
 *
 * If the frame was being forwarded from another MP, a PERR frame will be sent
 * to the precursor.  The precursor's address (i.e. the previous hop) was saved
 * in addr1 of the frame-to-be-forwarded, and would only be overwritten once
 * the destination is successfully resolved.
 *
 * Locking: the function must me called within a rcu_read_lock region
 */
void mesh_path_discard_frame(struct sk_buff *skb,
			     struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct mesh_path *mpath;
	u32 sn = 0;
	__le16 reason = cpu_to_le16(WLAN_REASON_MESH_PATH_NOFORWARD);

	if (memcmp(hdr->addr4, sdata->vif.addr, ETH_ALEN) != 0) {
		u8 *ra, *da;

		da = hdr->addr3;
		ra = hdr->addr1;
		mpath = mesh_path_lookup(da, sdata);
		if (mpath)
			sn = ++mpath->sn;
		mesh_path_error_tx(sdata->u.mesh.mshcfg.element_ttl, skb->data,
				   cpu_to_le32(sn), reason, ra, sdata);
	}

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
	mpath->sn = 0xffff;
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
	if (free_leafs) {
		if (mpath->timer.function)
			del_timer_sync(&mpath->timer);
		kfree(mpath);
	}
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
	struct mesh_table *tbl_path, *tbl_mpp;

	tbl_path = mesh_table_alloc(INIT_PATHS_SIZE_ORDER);
	if (!tbl_path)
		return -ENOMEM;
	tbl_path->free_node = &mesh_path_node_free;
	tbl_path->copy_node = &mesh_path_node_copy;
	tbl_path->mean_chain_len = MEAN_CHAIN_LEN;

	tbl_mpp = mesh_table_alloc(INIT_PATHS_SIZE_ORDER);
	if (!tbl_mpp) {
		mesh_table_free(tbl_path, true);
		return -ENOMEM;
	}
	tbl_mpp->free_node = &mesh_path_node_free;
	tbl_mpp->copy_node = &mesh_path_node_copy;
	tbl_mpp->mean_chain_len = MEAN_CHAIN_LEN;

	/* Need no locking since this is during init */
	RCU_INIT_POINTER(mesh_paths, tbl_path);
	RCU_INIT_POINTER(mpp_paths, tbl_mpp);

	return 0;
}

void mesh_path_expire(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_table *tbl;
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	rcu_read_lock();
	tbl = rcu_dereference(mesh_paths);
	for_each_mesh_entry(tbl, p, node, i) {
		if (node->mpath->sdata != sdata)
			continue;
		mpath = node->mpath;
		spin_lock_bh(&mpath->state_lock);
		if ((!(mpath->flags & MESH_PATH_RESOLVING)) &&
		    (!(mpath->flags & MESH_PATH_FIXED)) &&
		     time_after(jiffies, mpath->exp_time + MESH_PATH_EXPIRE)) {
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_del(mpath->dst, mpath->sdata);
		} else
			spin_unlock_bh(&mpath->state_lock);
	}
	rcu_read_unlock();
}

void mesh_pathtbl_unregister(void)
{
	/* no need for locking during exit path */
	mesh_table_free(rcu_dereference_raw(mesh_paths), true);
	mesh_table_free(rcu_dereference_raw(mpp_paths), true);
}
