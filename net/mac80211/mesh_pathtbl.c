// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008, 2009 open80211s Ltd.
 * Copyright (C) 2023 Intel Corporation
 * Author:     Luis Carlos Cobo <luisca@cozybit.com>
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
#include <linux/rhashtable.h>

static void mesh_path_free_rcu(struct mesh_table *tbl, struct mesh_path *mpath);

static u32 mesh_table_hash(const void *addr, u32 len, u32 seed)
{
	/* Use last four bytes of hw addr as hash index */
	return jhash_1word(__get_unaligned_cpu32((u8 *)addr + 2), seed);
}

static const struct rhashtable_params mesh_rht_params = {
	.nelem_hint = 2,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct mesh_path, dst),
	.head_offset = offsetof(struct mesh_path, rhash),
	.hashfn = mesh_table_hash,
};

static const struct rhashtable_params fast_tx_rht_params = {
	.nelem_hint = 10,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct ieee80211_mesh_fast_tx, addr_key),
	.head_offset = offsetof(struct ieee80211_mesh_fast_tx, rhash),
	.hashfn = mesh_table_hash,
};

static void __mesh_fast_tx_entry_free(void *ptr, void *tblptr)
{
	struct ieee80211_mesh_fast_tx *entry = ptr;

	kfree_rcu(entry, fast_tx.rcu_head);
}

static void mesh_fast_tx_deinit(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_tx_cache *cache;

	cache = &sdata->u.mesh.tx_cache;
	rhashtable_free_and_destroy(&cache->rht,
				    __mesh_fast_tx_entry_free, NULL);
}

static void mesh_fast_tx_init(struct ieee80211_sub_if_data *sdata)
{
	struct mesh_tx_cache *cache;

	cache = &sdata->u.mesh.tx_cache;
	rhashtable_init(&cache->rht, &fast_tx_rht_params);
	INIT_HLIST_HEAD(&cache->walk_head);
	spin_lock_init(&cache->walk_lock);
}

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

static void mesh_table_init(struct mesh_table *tbl)
{
	INIT_HLIST_HEAD(&tbl->known_gates);
	INIT_HLIST_HEAD(&tbl->walk_head);
	atomic_set(&tbl->entries,  0);
	spin_lock_init(&tbl->gates_lock);
	spin_lock_init(&tbl->walk_lock);

	/* rhashtable_init() may fail only in case of wrong
	 * mesh_rht_params
	 */
	WARN_ON(rhashtable_init(&tbl->rhead, &mesh_rht_params));
}

static void mesh_table_free(struct mesh_table *tbl)
{
	rhashtable_free_and_destroy(&tbl->rhead,
				    mesh_path_rht_free, tbl);
}

/**
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
		 * the new addresses */
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
 * mesh_path_move_to_queue - Move or copy frames from one mpath queue to another
 *
 * @gate_mpath: An active mpath the frames will be sent to (i.e. the gate)
 * @from_mpath: The failed mpath
 * @copy: When true, copy all the frames to the new mpath queue.  When false,
 * move them.
 *
 * This function is used to transfer or copy frames from an unresolved mpath to
 * a gate mpath.  The function also adds the Address Extension field and
 * updates the next hop.
 *
 * If a frame already has an Address Extension field, only the next hop and
 * destination addresses are updated.
 *
 * The gate mpath must be an active mpath with a valid mpath->next_hop.
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

	mpath = rhashtable_lookup(&tbl->rhead, dst, mesh_rht_params);

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
	return mpath_lookup(&sdata->u.mesh.mesh_paths, dst, sdata);
}

struct mesh_path *
mpp_path_lookup(struct ieee80211_sub_if_data *sdata, const u8 *dst)
{
	return mpath_lookup(&sdata->u.mesh.mpp_paths, dst, sdata);
}

static struct mesh_path *
__mesh_path_lookup_by_idx(struct mesh_table *tbl, int idx)
{
	int i = 0;
	struct mesh_path *mpath;

	hlist_for_each_entry_rcu(mpath, &tbl->walk_head, walk_list) {
		if (i++ == idx)
			break;
	}

	if (!mpath)
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
	return __mesh_path_lookup_by_idx(&sdata->u.mesh.mesh_paths, idx);
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
	return __mesh_path_lookup_by_idx(&sdata->u.mesh.mpp_paths, idx);
}

/**
 * mesh_path_add_gate - add the given mpath to a mesh gate to our path table
 * @mpath: gate path to add to table
 *
 * Returns: 0 on success, -EEXIST
 */
int mesh_path_add_gate(struct mesh_path *mpath)
{
	struct mesh_table *tbl;
	int err;

	rcu_read_lock();
	tbl = &mpath->sdata->u.mesh.mesh_paths;

	spin_lock_bh(&mpath->state_lock);
	if (mpath->is_gate) {
		err = -EEXIST;
		spin_unlock_bh(&mpath->state_lock);
		goto err_rcu;
	}
	mpath->is_gate = true;
	mpath->sdata->u.mesh.num_gates++;

	spin_lock(&tbl->gates_lock);
	hlist_add_head_rcu(&mpath->gate_list, &tbl->known_gates);
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
 *
 * Returns: The number of gates
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
	new_mpath->exp_time = jiffies;
	spin_lock_init(&new_mpath->state_lock);
	timer_setup(&new_mpath->timer, mesh_path_timer, 0);

	return new_mpath;
}

static void mesh_fast_tx_entry_free(struct mesh_tx_cache *cache,
				    struct ieee80211_mesh_fast_tx *entry)
{
	hlist_del_rcu(&entry->walk_list);
	rhashtable_remove_fast(&cache->rht, &entry->rhash, fast_tx_rht_params);
	kfree_rcu(entry, fast_tx.rcu_head);
}

struct ieee80211_mesh_fast_tx *
mesh_fast_tx_get(struct ieee80211_sub_if_data *sdata, const u8 *addr)
{
	struct ieee80211_mesh_fast_tx *entry;
	struct mesh_tx_cache *cache;

	cache = &sdata->u.mesh.tx_cache;
	entry = rhashtable_lookup(&cache->rht, addr, fast_tx_rht_params);
	if (!entry)
		return NULL;

	if (!(entry->mpath->flags & MESH_PATH_ACTIVE) ||
	    mpath_expired(entry->mpath)) {
		spin_lock_bh(&cache->walk_lock);
		entry = rhashtable_lookup(&cache->rht, addr, fast_tx_rht_params);
		if (entry)
		    mesh_fast_tx_entry_free(cache, entry);
		spin_unlock_bh(&cache->walk_lock);
		return NULL;
	}

	mesh_path_refresh(sdata, entry->mpath, NULL);
	if (entry->mppath)
		entry->mppath->exp_time = jiffies;
	entry->timestamp = jiffies;

	return entry;
}

void mesh_fast_tx_cache(struct ieee80211_sub_if_data *sdata,
			struct sk_buff *skb, struct mesh_path *mpath)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_mesh_fast_tx *entry, *prev;
	struct ieee80211_mesh_fast_tx build = {};
	struct ieee80211s_hdr *meshhdr;
	struct mesh_tx_cache *cache;
	struct ieee80211_key *key;
	struct mesh_path *mppath;
	struct sta_info *sta;
	u8 *qc;

	if (sdata->noack_map ||
	    !ieee80211_is_data_qos(hdr->frame_control))
		return;

	build.fast_tx.hdr_len = ieee80211_hdrlen(hdr->frame_control);
	meshhdr = (struct ieee80211s_hdr *)(skb->data + build.fast_tx.hdr_len);
	build.hdrlen = ieee80211_get_mesh_hdrlen(meshhdr);

	cache = &sdata->u.mesh.tx_cache;
	if (atomic_read(&cache->rht.nelems) >= MESH_FAST_TX_CACHE_MAX_SIZE)
		return;

	sta = rcu_dereference(mpath->next_hop);
	if (!sta)
		return;

	if ((meshhdr->flags & MESH_FLAGS_AE) == MESH_FLAGS_AE_A5_A6) {
		/* This is required to keep the mppath alive */
		mppath = mpp_path_lookup(sdata, meshhdr->eaddr1);
		if (!mppath)
			return;
		build.mppath = mppath;
	} else if (ieee80211_has_a4(hdr->frame_control)) {
		mppath = mpath;
	} else {
		return;
	}

	/* rate limit, in case fast xmit can't be enabled */
	if (mppath->fast_tx_check == jiffies)
		return;

	mppath->fast_tx_check = jiffies;

	/*
	 * Same use of the sta lock as in ieee80211_check_fast_xmit, in order
	 * to protect against concurrent sta key updates.
	 */
	spin_lock_bh(&sta->lock);
	key = rcu_access_pointer(sta->ptk[sta->ptk_idx]);
	if (!key)
		key = rcu_access_pointer(sdata->default_unicast_key);
	build.fast_tx.key = key;

	if (key) {
		bool gen_iv, iv_spc;

		gen_iv = key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV;
		iv_spc = key->conf.flags & IEEE80211_KEY_FLAG_PUT_IV_SPACE;

		if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE) ||
		    (key->flags & KEY_FLAG_TAINTED))
			goto unlock_sta;

		switch (key->conf.cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
			if (gen_iv)
				build.fast_tx.pn_offs = build.fast_tx.hdr_len;
			if (gen_iv || iv_spc)
				build.fast_tx.hdr_len += IEEE80211_CCMP_HDR_LEN;
			break;
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			if (gen_iv)
				build.fast_tx.pn_offs = build.fast_tx.hdr_len;
			if (gen_iv || iv_spc)
				build.fast_tx.hdr_len += IEEE80211_GCMP_HDR_LEN;
			break;
		default:
			goto unlock_sta;
		}
	}

	memcpy(build.addr_key, mppath->dst, ETH_ALEN);
	build.timestamp = jiffies;
	build.fast_tx.band = info->band;
	build.fast_tx.da_offs = offsetof(struct ieee80211_hdr, addr3);
	build.fast_tx.sa_offs = offsetof(struct ieee80211_hdr, addr4);
	build.mpath = mpath;
	memcpy(build.hdr, meshhdr, build.hdrlen);
	memcpy(build.hdr + build.hdrlen, rfc1042_header, sizeof(rfc1042_header));
	build.hdrlen += sizeof(rfc1042_header);
	memcpy(build.fast_tx.hdr, hdr, build.fast_tx.hdr_len);

	hdr = (struct ieee80211_hdr *)build.fast_tx.hdr;
	if (build.fast_tx.key)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);

	qc = ieee80211_get_qos_ctl(hdr);
	qc[1] |= IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT >> 8;

	entry = kmemdup(&build, sizeof(build), GFP_ATOMIC);
	if (!entry)
		goto unlock_sta;

	spin_lock(&cache->walk_lock);
	prev = rhashtable_lookup_get_insert_fast(&cache->rht,
						 &entry->rhash,
						 fast_tx_rht_params);
	if (unlikely(IS_ERR(prev))) {
		kfree(entry);
		goto unlock_cache;
	}

	/*
	 * replace any previous entry in the hash table, in case we're
	 * replacing it with a different type (e.g. mpath -> mpp)
	 */
	if (unlikely(prev)) {
		rhashtable_replace_fast(&cache->rht, &prev->rhash,
					&entry->rhash, fast_tx_rht_params);
		hlist_del_rcu(&prev->walk_list);
		kfree_rcu(prev, fast_tx.rcu_head);
	}

	hlist_add_head(&entry->walk_list, &cache->walk_head);

unlock_cache:
	spin_unlock(&cache->walk_lock);
unlock_sta:
	spin_unlock_bh(&sta->lock);
}

void mesh_fast_tx_gc(struct ieee80211_sub_if_data *sdata)
{
	unsigned long timeout = msecs_to_jiffies(MESH_FAST_TX_CACHE_TIMEOUT);
	struct mesh_tx_cache *cache;
	struct ieee80211_mesh_fast_tx *entry;
	struct hlist_node *n;

	cache = &sdata->u.mesh.tx_cache;
	if (atomic_read(&cache->rht.nelems) < MESH_FAST_TX_CACHE_THRESHOLD_SIZE)
		return;

	spin_lock_bh(&cache->walk_lock);
	hlist_for_each_entry_safe(entry, n, &cache->walk_head, walk_list)
		if (!time_is_after_jiffies(entry->timestamp + timeout))
			mesh_fast_tx_entry_free(cache, entry);
	spin_unlock_bh(&cache->walk_lock);
}

void mesh_fast_tx_flush_mpath(struct mesh_path *mpath)
{
	struct ieee80211_sub_if_data *sdata = mpath->sdata;
	struct mesh_tx_cache *cache = &sdata->u.mesh.tx_cache;
	struct ieee80211_mesh_fast_tx *entry;
	struct hlist_node *n;

	cache = &sdata->u.mesh.tx_cache;
	spin_lock_bh(&cache->walk_lock);
	hlist_for_each_entry_safe(entry, n, &cache->walk_head, walk_list)
		if (entry->mpath == mpath)
			mesh_fast_tx_entry_free(cache, entry);
	spin_unlock_bh(&cache->walk_lock);
}

void mesh_fast_tx_flush_sta(struct ieee80211_sub_if_data *sdata,
			    struct sta_info *sta)
{
	struct mesh_tx_cache *cache = &sdata->u.mesh.tx_cache;
	struct ieee80211_mesh_fast_tx *entry;
	struct hlist_node *n;

	cache = &sdata->u.mesh.tx_cache;
	spin_lock_bh(&cache->walk_lock);
	hlist_for_each_entry_safe(entry, n, &cache->walk_head, walk_list)
		if (rcu_access_pointer(entry->mpath->next_hop) == sta)
			mesh_fast_tx_entry_free(cache, entry);
	spin_unlock_bh(&cache->walk_lock);
}

void mesh_fast_tx_flush_addr(struct ieee80211_sub_if_data *sdata,
			     const u8 *addr)
{
	struct mesh_tx_cache *cache = &sdata->u.mesh.tx_cache;
	struct ieee80211_mesh_fast_tx *entry;

	cache = &sdata->u.mesh.tx_cache;
	spin_lock_bh(&cache->walk_lock);
	entry = rhashtable_lookup_fast(&cache->rht, addr, fast_tx_rht_params);
	if (entry)
		mesh_fast_tx_entry_free(cache, entry);
	spin_unlock_bh(&cache->walk_lock);
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

	if (ether_addr_equal(dst, sdata->vif.addr))
		/* never add ourselves as neighbours */
		return ERR_PTR(-EOPNOTSUPP);

	if (is_multicast_ether_addr(dst))
		return ERR_PTR(-EOPNOTSUPP);

	if (atomic_add_unless(&sdata->u.mesh.mpaths, 1, MESH_MAX_MPATHS) == 0)
		return ERR_PTR(-ENOSPC);

	new_mpath = mesh_path_new(sdata, dst, GFP_ATOMIC);
	if (!new_mpath)
		return ERR_PTR(-ENOMEM);

	tbl = &sdata->u.mesh.mesh_paths;
	spin_lock_bh(&tbl->walk_lock);
	mpath = rhashtable_lookup_get_insert_fast(&tbl->rhead,
						  &new_mpath->rhash,
						  mesh_rht_params);
	if (!mpath)
		hlist_add_head(&new_mpath->walk_list, &tbl->walk_head);
	spin_unlock_bh(&tbl->walk_lock);

	if (mpath) {
		kfree(new_mpath);

		if (IS_ERR(mpath))
			return mpath;

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
		return -EOPNOTSUPP;

	if (is_multicast_ether_addr(dst))
		return -EOPNOTSUPP;

	new_mpath = mesh_path_new(sdata, dst, GFP_ATOMIC);

	if (!new_mpath)
		return -ENOMEM;

	memcpy(new_mpath->mpp, mpp, ETH_ALEN);
	tbl = &sdata->u.mesh.mpp_paths;

	spin_lock_bh(&tbl->walk_lock);
	ret = rhashtable_lookup_insert_fast(&tbl->rhead,
					    &new_mpath->rhash,
					    mesh_rht_params);
	if (!ret)
		hlist_add_head_rcu(&new_mpath->walk_list, &tbl->walk_head);
	spin_unlock_bh(&tbl->walk_lock);

	if (ret)
		kfree(new_mpath);
	else
		mesh_fast_tx_flush_addr(sdata, dst);

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
	struct mesh_table *tbl = &sdata->u.mesh.mesh_paths;
	static const u8 bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct mesh_path *mpath;

	rcu_read_lock();
	hlist_for_each_entry_rcu(mpath, &tbl->walk_head, walk_list) {
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
	rcu_read_unlock();
}

static void mesh_path_free_rcu(struct mesh_table *tbl,
			       struct mesh_path *mpath)
{
	struct ieee80211_sub_if_data *sdata = mpath->sdata;

	spin_lock_bh(&mpath->state_lock);
	mpath->flags |= MESH_PATH_RESOLVING | MESH_PATH_DELETED;
	mesh_gate_del(tbl, mpath);
	spin_unlock_bh(&mpath->state_lock);
	timer_shutdown_sync(&mpath->timer);
	atomic_dec(&sdata->u.mesh.mpaths);
	atomic_dec(&tbl->entries);
	mesh_path_flush_pending(mpath);
	kfree_rcu(mpath, rcu);
}

static void __mesh_path_del(struct mesh_table *tbl, struct mesh_path *mpath)
{
	hlist_del_rcu(&mpath->walk_list);
	rhashtable_remove_fast(&tbl->rhead, &mpath->rhash, mesh_rht_params);
	if (tbl == &mpath->sdata->u.mesh.mpp_paths)
		mesh_fast_tx_flush_addr(mpath->sdata, mpath->dst);
	else
		mesh_fast_tx_flush_mpath(mpath);
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
	struct mesh_table *tbl = &sdata->u.mesh.mesh_paths;
	struct mesh_path *mpath;
	struct hlist_node *n;

	spin_lock_bh(&tbl->walk_lock);
	hlist_for_each_entry_safe(mpath, n, &tbl->walk_head, walk_list) {
		if (rcu_access_pointer(mpath->next_hop) == sta)
			__mesh_path_del(tbl, mpath);
	}
	spin_unlock_bh(&tbl->walk_lock);
}

static void mpp_flush_by_proxy(struct ieee80211_sub_if_data *sdata,
			       const u8 *proxy)
{
	struct mesh_table *tbl = &sdata->u.mesh.mpp_paths;
	struct mesh_path *mpath;
	struct hlist_node *n;

	spin_lock_bh(&tbl->walk_lock);
	hlist_for_each_entry_safe(mpath, n, &tbl->walk_head, walk_list) {
		if (ether_addr_equal(mpath->mpp, proxy))
			__mesh_path_del(tbl, mpath);
	}
	spin_unlock_bh(&tbl->walk_lock);
}

static void table_flush_by_iface(struct mesh_table *tbl)
{
	struct mesh_path *mpath;
	struct hlist_node *n;

	spin_lock_bh(&tbl->walk_lock);
	hlist_for_each_entry_safe(mpath, n, &tbl->walk_head, walk_list) {
		__mesh_path_del(tbl, mpath);
	}
	spin_unlock_bh(&tbl->walk_lock);
}

/**
 * mesh_path_flush_by_iface - Deletes all mesh paths associated with a given iface
 *
 * @sdata: interface data to match
 *
 * This function deletes both mesh paths as well as mesh portal paths.
 */
void mesh_path_flush_by_iface(struct ieee80211_sub_if_data *sdata)
{
	table_flush_by_iface(&sdata->u.mesh.mesh_paths);
	table_flush_by_iface(&sdata->u.mesh.mpp_paths);
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

	spin_lock_bh(&tbl->walk_lock);
	mpath = rhashtable_lookup_fast(&tbl->rhead, addr, mesh_rht_params);
	if (!mpath) {
		spin_unlock_bh(&tbl->walk_lock);
		return -ENXIO;
	}

	__mesh_path_del(tbl, mpath);
	spin_unlock_bh(&tbl->walk_lock);
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

	err = table_path_del(&sdata->u.mesh.mesh_paths, sdata, addr);
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
 *
 * Returns: 0 on success, -EHOSTUNREACH
 */
int mesh_path_send_to_gates(struct mesh_path *mpath)
{
	struct ieee80211_sub_if_data *sdata = mpath->sdata;
	struct mesh_table *tbl;
	struct mesh_path *from_mpath = mpath;
	struct mesh_path *gate;
	bool copy = false;

	tbl = &sdata->u.mesh.mesh_paths;

	rcu_read_lock();
	hlist_for_each_entry_rcu(gate, &tbl->known_gates, gate_list) {
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

	hlist_for_each_entry_rcu(gate, &tbl->known_gates, gate_list) {
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
	ieee80211_free_txskb(&sdata->local->hw, skb);
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
	mpath->flags = MESH_PATH_FIXED | MESH_PATH_SN_VALID;
	mesh_path_activate(mpath);
	mesh_fast_tx_flush_mpath(mpath);
	spin_unlock_bh(&mpath->state_lock);
	ewma_mesh_fail_avg_init(&next_hop->mesh->fail_avg);
	/* init it at a low value - 0 start is tricky */
	ewma_mesh_fail_avg_add(&next_hop->mesh->fail_avg, 1);
	mesh_path_tx_pending(mpath);
}

void mesh_pathtbl_init(struct ieee80211_sub_if_data *sdata)
{
	mesh_table_init(&sdata->u.mesh.mesh_paths);
	mesh_table_init(&sdata->u.mesh.mpp_paths);
	mesh_fast_tx_init(sdata);
}

static
void mesh_path_tbl_expire(struct ieee80211_sub_if_data *sdata,
			  struct mesh_table *tbl)
{
	struct mesh_path *mpath;
	struct hlist_node *n;

	spin_lock_bh(&tbl->walk_lock);
	hlist_for_each_entry_safe(mpath, n, &tbl->walk_head, walk_list) {
		if ((!(mpath->flags & MESH_PATH_RESOLVING)) &&
		    (!(mpath->flags & MESH_PATH_FIXED)) &&
		     time_after(jiffies, mpath->exp_time + MESH_PATH_EXPIRE))
			__mesh_path_del(tbl, mpath);
	}
	spin_unlock_bh(&tbl->walk_lock);
}

void mesh_path_expire(struct ieee80211_sub_if_data *sdata)
{
	mesh_path_tbl_expire(sdata, &sdata->u.mesh.mesh_paths);
	mesh_path_tbl_expire(sdata, &sdata->u.mesh.mpp_paths);
}

void mesh_pathtbl_unregister(struct ieee80211_sub_if_data *sdata)
{
	mesh_fast_tx_deinit(sdata);
	mesh_table_free(&sdata->u.mesh.mesh_paths);
	mesh_table_free(&sdata->u.mesh.mpp_paths);
}
