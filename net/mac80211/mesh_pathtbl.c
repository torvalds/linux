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
 * by RCU.  We need to take this lock when modying the number of buckets
 * on one of the path tables but we don't need to if adding or removing mpaths
 * from hash buckets.
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

static int mesh_gate_add(struct mesh_table *tbl, struct mesh_path *mpath);

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
	spin_lock_init(&newtbl->gates_lock);

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
	struct mpath_node *gate;
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
	if (free_leafs) {
		spin_lock_bh(&tbl->gates_lock);
		hlist_for_each_entry_safe(gate, p, q,
					 tbl->known_gates, list) {
			hlist_del(&gate->list);
			kfree(gate);
		}
		kfree(tbl->known_gates);
		spin_unlock_bh(&tbl->gates_lock);
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
	newtbl->known_gates = oldtbl->known_gates;
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
	struct sk_buff *skb, *cp_skb = NULL;
	struct sk_buff_head gateq, failq;
	unsigned long flags;
	int num_skbs;

	BUG_ON(gate_mpath == from_mpath);
	BUG_ON(!gate_mpath->next_hop);

	__skb_queue_head_init(&gateq);
	__skb_queue_head_init(&failq);

	spin_lock_irqsave(&from_mpath->frame_queue.lock, flags);
	skb_queue_splice_init(&from_mpath->frame_queue, &failq);
	spin_unlock_irqrestore(&from_mpath->frame_queue.lock, flags);

	num_skbs = skb_queue_len(&failq);

	while (num_skbs--) {
		skb = __skb_dequeue(&failq);
		if (copy) {
			cp_skb = skb_copy(skb, GFP_ATOMIC);
			if (cp_skb)
				__skb_queue_tail(&failq, cp_skb);
		}

		prepare_for_gate(skb, gate_mpath->dst, gate_mpath);
		__skb_queue_tail(&gateq, skb);
	}

	spin_lock_irqsave(&gate_mpath->frame_queue.lock, flags);
	skb_queue_splice(&gateq, &gate_mpath->frame_queue);
	mpath_dbg("Mpath queue for gate %pM has %d frames\n",
			gate_mpath->dst,
			skb_queue_len(&gate_mpath->frame_queue));
	spin_unlock_irqrestore(&gate_mpath->frame_queue.lock, flags);

	if (!copy)
		return;

	spin_lock_irqsave(&from_mpath->frame_queue.lock, flags);
	skb_queue_splice(&failq, &from_mpath->frame_queue);
	spin_unlock_irqrestore(&from_mpath->frame_queue.lock, flags);
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
				node->mpath->flags &= ~MESH_PATH_ACTIVE;
				spin_unlock_bh(&node->mpath->state_lock);
			}
			return node->mpath;
		}
	}

	return NULL;
}

static void mesh_gate_node_reclaim(struct rcu_head *rp)
{
	struct mpath_node *node = container_of(rp, struct mpath_node, rcu);
	kfree(node);
}

/**
 * mesh_gate_add - mark mpath as path to a mesh gate and add to known_gates
 * @mesh_tbl: table which contains known_gates list
 * @mpath: mpath to known mesh gate
 *
 * Returns: 0 on success
 *
 */
static int mesh_gate_add(struct mesh_table *tbl, struct mesh_path *mpath)
{
	struct mpath_node *gate, *new_gate;
	struct hlist_node *n;
	int err;

	rcu_read_lock();
	tbl = rcu_dereference(tbl);

	hlist_for_each_entry_rcu(gate, n, tbl->known_gates, list)
		if (gate->mpath == mpath) {
			err = -EEXIST;
			goto err_rcu;
		}

	new_gate = kzalloc(sizeof(struct mpath_node), GFP_ATOMIC);
	if (!new_gate) {
		err = -ENOMEM;
		goto err_rcu;
	}

	mpath->is_gate = true;
	mpath->sdata->u.mesh.num_gates++;
	new_gate->mpath = mpath;
	spin_lock_bh(&tbl->gates_lock);
	hlist_add_head_rcu(&new_gate->list, tbl->known_gates);
	spin_unlock_bh(&tbl->gates_lock);
	rcu_read_unlock();
	mpath_dbg("Mesh path (%s): Recorded new gate: %pM. %d known gates\n",
		  mpath->sdata->name, mpath->dst,
		  mpath->sdata->u.mesh.num_gates);
	return 0;
err_rcu:
	rcu_read_unlock();
	return err;
}

/**
 * mesh_gate_del - remove a mesh gate from the list of known gates
 * @tbl: table which holds our list of known gates
 * @mpath: gate mpath
 *
 * Returns: 0 on success
 *
 * Locking: must be called inside rcu_read_lock() section
 */
static int mesh_gate_del(struct mesh_table *tbl, struct mesh_path *mpath)
{
	struct mpath_node *gate;
	struct hlist_node *p, *q;

	tbl = rcu_dereference(tbl);

	hlist_for_each_entry_safe(gate, p, q, tbl->known_gates, list)
		if (gate->mpath == mpath) {
			spin_lock_bh(&tbl->gates_lock);
			hlist_del_rcu(&gate->list);
			call_rcu(&gate->rcu, mesh_gate_node_reclaim);
			spin_unlock_bh(&tbl->gates_lock);
			mpath->sdata->u.mesh.num_gates--;
			mpath->is_gate = false;
			mpath_dbg("Mesh path (%s): Deleted gate: %pM. "
				  "%d known gates\n", mpath->sdata->name,
				  mpath->dst, mpath->sdata->u.mesh.num_gates);
			break;
		}

	return 0;
}

/**
 *
 * mesh_path_add_gate - add the given mpath to a mesh gate to our path table
 * @mpath: gate path to add to table
 */
int mesh_path_add_gate(struct mesh_path *mpath)
{
	return mesh_gate_add(mesh_paths, mpath);
}

/**
 * mesh_gate_num - number of gates known to this interface
 * @sdata: subif data
 */
int mesh_gate_num(struct ieee80211_sub_if_data *sdata)
{
	return sdata->u.mesh.num_gates;
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
	init_timer(&new_mpath->timer);
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
		if (rcu_dereference(mpath->next_hop) == sta &&
		    mpath->flags & MESH_PATH_ACTIVE &&
		    !(mpath->flags & MESH_PATH_FIXED)) {
			spin_lock_bh(&mpath->state_lock);
			mpath->flags &= ~MESH_PATH_ACTIVE;
			++mpath->sn;
			spin_unlock_bh(&mpath->state_lock);
			mesh_path_error_tx(sdata->u.mesh.mshcfg.element_ttl,
					mpath->dst, cpu_to_le32(mpath->sn),
					reason, bcast, sdata);
		}
	}
	rcu_read_unlock();
}

static void mesh_path_node_reclaim(struct rcu_head *rp)
{
	struct mpath_node *node = container_of(rp, struct mpath_node, rcu);
	struct ieee80211_sub_if_data *sdata = node->mpath->sdata;

	del_timer_sync(&node->mpath->timer);
	atomic_dec(&sdata->u.mesh.mpaths);
	kfree(node->mpath);
	kfree(node);
}

/* needs to be called with the corresponding hashwlock taken */
static void __mesh_path_del(struct mesh_table *tbl, struct mpath_node *node)
{
	struct mesh_path *mpath;
	mpath = node->mpath;
	spin_lock(&mpath->state_lock);
	mpath->flags |= MESH_PATH_RESOLVING;
	if (mpath->is_gate)
		mesh_gate_del(tbl, mpath);
	hlist_del_rcu(&node->list);
	call_rcu(&node->rcu, mesh_path_node_reclaim);
	spin_unlock(&mpath->state_lock);
	atomic_dec(&tbl->entries);
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
		if (rcu_dereference(mpath->next_hop) == sta) {
			spin_lock_bh(&tbl->hashwlock[i]);
			__mesh_path_del(tbl, node);
			spin_unlock_bh(&tbl->hashwlock[i]);
		}
	}
	rcu_read_unlock();
}

static void table_flush_by_iface(struct mesh_table *tbl,
				 struct ieee80211_sub_if_data *sdata)
{
	struct mesh_path *mpath;
	struct mpath_node *node;
	struct hlist_node *p;
	int i;

	WARN_ON(!rcu_read_lock_held());
	for_each_mesh_entry(tbl, p, node, i) {
		mpath = node->mpath;
		if (mpath->sdata != sdata)
			continue;
		spin_lock_bh(&tbl->hashwlock[i]);
		__mesh_path_del(tbl, node);
		spin_unlock_bh(&tbl->hashwlock[i]);
	}
}

/**
 * mesh_path_flush_by_iface - Deletes all mesh paths associated with a given iface
 *
 * This function deletes both mesh paths as well as mesh portal paths.
 *
 * @sdata - interface data to match
 *
 */
void mesh_path_flush_by_iface(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_table *tbl;

	rcu_read_lock();
	tbl = rcu_dereference(mesh_paths);
	table_flush_by_iface(tbl, sdata);
	tbl = rcu_dereference(mpp_paths);
	table_flush_by_iface(tbl, sdata);
	rcu_read_unlock();
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
			__mesh_path_del(tbl, node);
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
	struct hlist_node *n;
	struct mesh_table *tbl;
	struct mesh_path *from_mpath = mpath;
	struct mpath_node *gate = NULL;
	bool copy = false;
	struct hlist_head *known_gates;

	rcu_read_lock();
	tbl = rcu_dereference(mesh_paths);
	known_gates = tbl->known_gates;
	rcu_read_unlock();

	if (!known_gates)
		return -EHOSTUNREACH;

	hlist_for_each_entry_rcu(gate, n, known_gates, list) {
		if (gate->mpath->sdata != sdata)
			continue;

		if (gate->mpath->flags & MESH_PATH_ACTIVE) {
			mpath_dbg("Forwarding to %pM\n", gate->mpath->dst);
			mesh_path_move_to_queue(gate->mpath, from_mpath, copy);
			from_mpath = gate->mpath;
			copy = true;
		} else {
			mpath_dbg("Not forwarding %p\n", gate->mpath);
			mpath_dbg("flags %x\n", gate->mpath->flags);
		}
	}

	hlist_for_each_entry_rcu(gate, n, known_gates, list)
		if (gate->mpath->sdata == sdata) {
			mpath_dbg("Sending to %pM\n", gate->mpath->dst);
			mesh_path_tx_pending(gate->mpath);
		}

	return (from_mpath == mpath) ? -EHOSTUNREACH : 0;
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
		rcu_read_lock();
		mpath = mesh_path_lookup(da, sdata);
		if (mpath) {
			spin_lock_bh(&mpath->state_lock);
			sn = ++mpath->sn;
			spin_unlock_bh(&mpath->state_lock);
		}
		rcu_read_unlock();
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

	while ((skb = skb_dequeue(&mpath->frame_queue)) != NULL)
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
	tbl_path->known_gates = kzalloc(sizeof(struct hlist_head), GFP_ATOMIC);
	INIT_HLIST_HEAD(tbl_path->known_gates);


	tbl_mpp = mesh_table_alloc(INIT_PATHS_SIZE_ORDER);
	if (!tbl_mpp) {
		mesh_table_free(tbl_path, true);
		return -ENOMEM;
	}
	tbl_mpp->free_node = &mesh_path_node_free;
	tbl_mpp->copy_node = &mesh_path_node_copy;
	tbl_mpp->mean_chain_len = MEAN_CHAIN_LEN;
	tbl_mpp->known_gates = kzalloc(sizeof(struct hlist_head), GFP_ATOMIC);
	INIT_HLIST_HEAD(tbl_mpp->known_gates);

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
		if ((!(mpath->flags & MESH_PATH_RESOLVING)) &&
		    (!(mpath->flags & MESH_PATH_FIXED)) &&
		     time_after(jiffies, mpath->exp_time + MESH_PATH_EXPIRE))
			mesh_path_del(mpath->dst, mpath->sdata);
	}
	rcu_read_unlock();
}

void mesh_pathtbl_unregister(void)
{
	/* no need for locking during exit path */
	mesh_table_free(rcu_dereference_raw(mesh_paths), true);
	mesh_table_free(rcu_dereference_raw(mpp_paths), true);
}
