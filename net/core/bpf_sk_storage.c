// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <net/bpf_sk_storage.h>
#include <net/sock.h>
#include <uapi/linux/sock_diag.h>
#include <uapi/linux/btf.h>

#define BPF_LOCAL_STORAGE_CREATE_FLAG_MASK (BPF_F_NO_PREALLOC | BPF_F_CLONE)

DEFINE_BPF_STORAGE_CACHE(sk_cache);

struct bpf_local_storage_map_bucket {
	struct hlist_head list;
	raw_spinlock_t lock;
};

/* Thp map is not the primary owner of a bpf_local_storage_elem.
 * Instead, the container object (eg. sk->sk_bpf_storage) is.
 *
 * The map (bpf_local_storage_map) is for two purposes
 * 1. Define the size of the "local storage".  It is
 *    the map's value_size.
 *
 * 2. Maintain a list to keep track of all elems such
 *    that they can be cleaned up during the map destruction.
 *
 * When a bpf local storage is being looked up for a
 * particular object,  the "bpf_map" pointer is actually used
 * as the "key" to search in the list of elem in
 * the respective bpf_local_storage owned by the object.
 *
 * e.g. sk->sk_bpf_storage is the mini-map with the "bpf_map" pointer
 * as the searching key.
 */
struct bpf_local_storage_map {
	struct bpf_map map;
	/* Lookup elem does not require accessing the map.
	 *
	 * Updating/Deleting requires a bucket lock to
	 * link/unlink the elem from the map.  Having
	 * multiple buckets to improve contention.
	 */
	struct bpf_local_storage_map_bucket *buckets;
	u32 bucket_log;
	u16 elem_size;
	u16 cache_idx;
};

struct bpf_local_storage_data {
	/* smap is used as the searching key when looking up
	 * from the object's bpf_local_storage.
	 *
	 * Put it in the same cacheline as the data to minimize
	 * the number of cachelines access during the cache hit case.
	 */
	struct bpf_local_storage_map __rcu *smap;
	u8 data[] __aligned(8);
};

/* Linked to bpf_local_storage and bpf_local_storage_map */
struct bpf_local_storage_elem {
	struct hlist_node map_node;	/* Linked to bpf_local_storage_map */
	struct hlist_node snode;	/* Linked to bpf_local_storage */
	struct bpf_local_storage __rcu *local_storage;
	struct rcu_head rcu;
	/* 8 bytes hole */
	/* The data is stored in aother cacheline to minimize
	 * the number of cachelines access during a cache hit.
	 */
	struct bpf_local_storage_data sdata ____cacheline_aligned;
};

#define SELEM(_SDATA)							\
	container_of((_SDATA), struct bpf_local_storage_elem, sdata)
#define SDATA(_SELEM) (&(_SELEM)->sdata)

struct bpf_local_storage {
	struct bpf_local_storage_data __rcu *cache[BPF_LOCAL_STORAGE_CACHE_SIZE];
	struct hlist_head list; /* List of bpf_local_storage_elem */
	void *owner;		/* The object that owns the above "list" of
				 * bpf_local_storage_elem.
				 */
	struct rcu_head rcu;
	raw_spinlock_t lock;	/* Protect adding/removing from the "list" */
};

static struct bpf_local_storage_map_bucket *
select_bucket(struct bpf_local_storage_map *smap,
	      struct bpf_local_storage_elem *selem)
{
	return &smap->buckets[hash_ptr(selem, smap->bucket_log)];
}

static int omem_charge(struct sock *sk, unsigned int size)
{
	/* same check as in sock_kmalloc() */
	if (size <= sysctl_optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + size < sysctl_optmem_max) {
		atomic_add(size, &sk->sk_omem_alloc);
		return 0;
	}

	return -ENOMEM;
}

static int mem_charge(struct bpf_local_storage_map *smap, void *owner, u32 size)
{
	struct bpf_map *map = &smap->map;

	if (!map->ops->map_local_storage_charge)
		return 0;

	return map->ops->map_local_storage_charge(smap, owner, size);
}

static void mem_uncharge(struct bpf_local_storage_map *smap, void *owner,
			 u32 size)
{
	struct bpf_map *map = &smap->map;

	if (map->ops->map_local_storage_uncharge)
		map->ops->map_local_storage_uncharge(smap, owner, size);
}

static struct bpf_local_storage __rcu **
owner_storage(struct bpf_local_storage_map *smap, void *owner)
{
	struct bpf_map *map = &smap->map;

	return map->ops->map_owner_storage_ptr(owner);
}

static bool selem_linked_to_storage(const struct bpf_local_storage_elem *selem)
{
	return !hlist_unhashed(&selem->snode);
}

static bool selem_linked_to_map(const struct bpf_local_storage_elem *selem)
{
	return !hlist_unhashed(&selem->map_node);
}

struct bpf_local_storage_elem *
bpf_selem_alloc(struct bpf_local_storage_map *smap, void *owner,
		void *value, bool charge_mem)
{
	struct bpf_local_storage_elem *selem;

	if (charge_mem && mem_charge(smap, owner, smap->elem_size))
		return NULL;

	selem = kzalloc(smap->elem_size, GFP_ATOMIC | __GFP_NOWARN);
	if (selem) {
		if (value)
			memcpy(SDATA(selem)->data, value, smap->map.value_size);
		return selem;
	}

	if (charge_mem)
		mem_uncharge(smap, owner, smap->elem_size);

	return NULL;
}

/* local_storage->lock must be held and selem->local_storage == local_storage.
 * The caller must ensure selem->smap is still valid to be
 * dereferenced for its smap->elem_size and smap->cache_idx.
 */
bool bpf_selem_unlink_storage_nolock(struct bpf_local_storage *local_storage,
				     struct bpf_local_storage_elem *selem,
				     bool uncharge_mem)
{
	struct bpf_local_storage_map *smap;
	bool free_local_storage;
	void *owner;

	smap = rcu_dereference(SDATA(selem)->smap);
	owner = local_storage->owner;

	/* All uncharging on the owner must be done first.
	 * The owner may be freed once the last selem is unlinked
	 * from local_storage.
	 */
	if (uncharge_mem)
		mem_uncharge(smap, owner, smap->elem_size);

	free_local_storage = hlist_is_singular_node(&selem->snode,
						    &local_storage->list);
	if (free_local_storage) {
		mem_uncharge(smap, owner, sizeof(struct bpf_local_storage));
		local_storage->owner = NULL;

		/* After this RCU_INIT, owner may be freed and cannot be used */
		RCU_INIT_POINTER(*owner_storage(smap, owner), NULL);

		/* local_storage is not freed now.  local_storage->lock is
		 * still held and raw_spin_unlock_bh(&local_storage->lock)
		 * will be done by the caller.
		 *
		 * Although the unlock will be done under
		 * rcu_read_lock(),  it is more intutivie to
		 * read if kfree_rcu(local_storage, rcu) is done
		 * after the raw_spin_unlock_bh(&local_storage->lock).
		 *
		 * Hence, a "bool free_local_storage" is returned
		 * to the caller which then calls the kfree_rcu()
		 * after unlock.
		 */
	}
	hlist_del_init_rcu(&selem->snode);
	if (rcu_access_pointer(local_storage->cache[smap->cache_idx]) ==
	    SDATA(selem))
		RCU_INIT_POINTER(local_storage->cache[smap->cache_idx], NULL);

	kfree_rcu(selem, rcu);

	return free_local_storage;
}

static void __bpf_selem_unlink_storage(struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage *local_storage;
	bool free_local_storage = false;

	if (unlikely(!selem_linked_to_storage(selem)))
		/* selem has already been unlinked from sk */
		return;

	local_storage = rcu_dereference(selem->local_storage);
	raw_spin_lock_bh(&local_storage->lock);
	if (likely(selem_linked_to_storage(selem)))
		free_local_storage = bpf_selem_unlink_storage_nolock(
			local_storage, selem, true);
	raw_spin_unlock_bh(&local_storage->lock);

	if (free_local_storage)
		kfree_rcu(local_storage, rcu);
}

void bpf_selem_link_storage_nolock(struct bpf_local_storage *local_storage,
				   struct bpf_local_storage_elem *selem)
{
	RCU_INIT_POINTER(selem->local_storage, local_storage);
	hlist_add_head(&selem->snode, &local_storage->list);
}

void bpf_selem_unlink_map(struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_map *smap;
	struct bpf_local_storage_map_bucket *b;

	if (unlikely(!selem_linked_to_map(selem)))
		/* selem has already be unlinked from smap */
		return;

	smap = rcu_dereference(SDATA(selem)->smap);
	b = select_bucket(smap, selem);
	raw_spin_lock_bh(&b->lock);
	if (likely(selem_linked_to_map(selem)))
		hlist_del_init_rcu(&selem->map_node);
	raw_spin_unlock_bh(&b->lock);
}

void bpf_selem_link_map(struct bpf_local_storage_map *smap,
			struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_map_bucket *b = select_bucket(smap, selem);

	raw_spin_lock_bh(&b->lock);
	RCU_INIT_POINTER(SDATA(selem)->smap, smap);
	hlist_add_head_rcu(&selem->map_node, &b->list);
	raw_spin_unlock_bh(&b->lock);
}

void bpf_selem_unlink(struct bpf_local_storage_elem *selem)
{
	/* Always unlink from map before unlinking from local_storage
	 * because selem will be freed after successfully unlinked from
	 * the local_storage.
	 */
	bpf_selem_unlink_map(selem);
	__bpf_selem_unlink_storage(selem);
}

struct bpf_local_storage_data *
bpf_local_storage_lookup(struct bpf_local_storage *local_storage,
			 struct bpf_local_storage_map *smap,
			 bool cacheit_lockit)
{
	struct bpf_local_storage_data *sdata;
	struct bpf_local_storage_elem *selem;

	/* Fast path (cache hit) */
	sdata = rcu_dereference(local_storage->cache[smap->cache_idx]);
	if (sdata && rcu_access_pointer(sdata->smap) == smap)
		return sdata;

	/* Slow path (cache miss) */
	hlist_for_each_entry_rcu(selem, &local_storage->list, snode)
		if (rcu_access_pointer(SDATA(selem)->smap) == smap)
			break;

	if (!selem)
		return NULL;

	sdata = SDATA(selem);
	if (cacheit_lockit) {
		/* spinlock is needed to avoid racing with the
		 * parallel delete.  Otherwise, publishing an already
		 * deleted sdata to the cache will become a use-after-free
		 * problem in the next bpf_local_storage_lookup().
		 */
		raw_spin_lock_bh(&local_storage->lock);
		if (selem_linked_to_storage(selem))
			rcu_assign_pointer(local_storage->cache[smap->cache_idx],
					   sdata);
		raw_spin_unlock_bh(&local_storage->lock);
	}

	return sdata;
}

static struct bpf_local_storage_data *
sk_storage_lookup(struct sock *sk, struct bpf_map *map, bool cacheit_lockit)
{
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_map *smap;

	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage)
		return NULL;

	smap = (struct bpf_local_storage_map *)map;
	return bpf_local_storage_lookup(sk_storage, smap, cacheit_lockit);
}

static int check_flags(const struct bpf_local_storage_data *old_sdata,
		       u64 map_flags)
{
	if (old_sdata && (map_flags & ~BPF_F_LOCK) == BPF_NOEXIST)
		/* elem already exists */
		return -EEXIST;

	if (!old_sdata && (map_flags & ~BPF_F_LOCK) == BPF_EXIST)
		/* elem doesn't exist, cannot update it */
		return -ENOENT;

	return 0;
}

int bpf_local_storage_alloc(void *owner,
			    struct bpf_local_storage_map *smap,
			    struct bpf_local_storage_elem *first_selem)
{
	struct bpf_local_storage *prev_storage, *storage;
	struct bpf_local_storage **owner_storage_ptr;
	int err;

	err = mem_charge(smap, owner, sizeof(*storage));
	if (err)
		return err;

	storage = kzalloc(sizeof(*storage), GFP_ATOMIC | __GFP_NOWARN);
	if (!storage) {
		err = -ENOMEM;
		goto uncharge;
	}

	INIT_HLIST_HEAD(&storage->list);
	raw_spin_lock_init(&storage->lock);
	storage->owner = owner;

	bpf_selem_link_storage_nolock(storage, first_selem);
	bpf_selem_link_map(smap, first_selem);

	owner_storage_ptr =
		(struct bpf_local_storage **)owner_storage(smap, owner);
	/* Publish storage to the owner.
	 * Instead of using any lock of the kernel object (i.e. owner),
	 * cmpxchg will work with any kernel object regardless what
	 * the running context is, bh, irq...etc.
	 *
	 * From now on, the owner->storage pointer (e.g. sk->sk_bpf_storage)
	 * is protected by the storage->lock.  Hence, when freeing
	 * the owner->storage, the storage->lock must be held before
	 * setting owner->storage ptr to NULL.
	 */
	prev_storage = cmpxchg(owner_storage_ptr, NULL, storage);
	if (unlikely(prev_storage)) {
		bpf_selem_unlink_map(first_selem);
		err = -EAGAIN;
		goto uncharge;

		/* Note that even first_selem was linked to smap's
		 * bucket->list, first_selem can be freed immediately
		 * (instead of kfree_rcu) because
		 * bpf_local_storage_map_free() does a
		 * synchronize_rcu() before walking the bucket->list.
		 * Hence, no one is accessing selem from the
		 * bucket->list under rcu_read_lock().
		 */
	}

	return 0;

uncharge:
	kfree(storage);
	mem_uncharge(smap, owner, sizeof(*storage));
	return err;
}

/* sk cannot be going away because it is linking new elem
 * to sk->sk_bpf_storage. (i.e. sk->sk_refcnt cannot be 0).
 * Otherwise, it will become a leak (and other memory issues
 * during map destruction).
 */
struct bpf_local_storage_data *
bpf_local_storage_update(void *owner, struct bpf_local_storage_map *smap,
			 void *value, u64 map_flags)
{
	struct bpf_local_storage_data *old_sdata = NULL;
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage *local_storage;
	int err;

	/* BPF_EXIST and BPF_NOEXIST cannot be both set */
	if (unlikely((map_flags & ~BPF_F_LOCK) > BPF_EXIST) ||
	    /* BPF_F_LOCK can only be used in a value with spin_lock */
	    unlikely((map_flags & BPF_F_LOCK) &&
		     !map_value_has_spin_lock(&smap->map)))
		return ERR_PTR(-EINVAL);

	local_storage = rcu_dereference(*owner_storage(smap, owner));
	if (!local_storage || hlist_empty(&local_storage->list)) {
		/* Very first elem for the owner */
		err = check_flags(NULL, map_flags);
		if (err)
			return ERR_PTR(err);

		selem = bpf_selem_alloc(smap, owner, value, true);
		if (!selem)
			return ERR_PTR(-ENOMEM);

		err = bpf_local_storage_alloc(owner, smap, selem);
		if (err) {
			kfree(selem);
			mem_uncharge(smap, owner, smap->elem_size);
			return ERR_PTR(err);
		}

		return SDATA(selem);
	}

	if ((map_flags & BPF_F_LOCK) && !(map_flags & BPF_NOEXIST)) {
		/* Hoping to find an old_sdata to do inline update
		 * such that it can avoid taking the local_storage->lock
		 * and changing the lists.
		 */
		old_sdata =
			bpf_local_storage_lookup(local_storage, smap, false);
		err = check_flags(old_sdata, map_flags);
		if (err)
			return ERR_PTR(err);
		if (old_sdata && selem_linked_to_storage(SELEM(old_sdata))) {
			copy_map_value_locked(&smap->map, old_sdata->data,
					      value, false);
			return old_sdata;
		}
	}

	raw_spin_lock_bh(&local_storage->lock);

	/* Recheck local_storage->list under local_storage->lock */
	if (unlikely(hlist_empty(&local_storage->list))) {
		/* A parallel del is happening and local_storage is going
		 * away.  It has just been checked before, so very
		 * unlikely.  Return instead of retry to keep things
		 * simple.
		 */
		err = -EAGAIN;
		goto unlock_err;
	}

	old_sdata = bpf_local_storage_lookup(local_storage, smap, false);
	err = check_flags(old_sdata, map_flags);
	if (err)
		goto unlock_err;

	if (old_sdata && (map_flags & BPF_F_LOCK)) {
		copy_map_value_locked(&smap->map, old_sdata->data, value,
				      false);
		selem = SELEM(old_sdata);
		goto unlock;
	}

	/* local_storage->lock is held.  Hence, we are sure
	 * we can unlink and uncharge the old_sdata successfully
	 * later.  Hence, instead of charging the new selem now
	 * and then uncharge the old selem later (which may cause
	 * a potential but unnecessary charge failure),  avoid taking
	 * a charge at all here (the "!old_sdata" check) and the
	 * old_sdata will not be uncharged later during
	 * bpf_selem_unlink_storage_nolock().
	 */
	selem = bpf_selem_alloc(smap, owner, value, !old_sdata);
	if (!selem) {
		err = -ENOMEM;
		goto unlock_err;
	}

	/* First, link the new selem to the map */
	bpf_selem_link_map(smap, selem);

	/* Second, link (and publish) the new selem to local_storage */
	bpf_selem_link_storage_nolock(local_storage, selem);

	/* Third, remove old selem, SELEM(old_sdata) */
	if (old_sdata) {
		bpf_selem_unlink_map(SELEM(old_sdata));
		bpf_selem_unlink_storage_nolock(local_storage, SELEM(old_sdata),
						false);
	}

unlock:
	raw_spin_unlock_bh(&local_storage->lock);
	return SDATA(selem);

unlock_err:
	raw_spin_unlock_bh(&local_storage->lock);
	return ERR_PTR(err);
}

static int sk_storage_delete(struct sock *sk, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = sk_storage_lookup(sk, map, false);
	if (!sdata)
		return -ENOENT;

	bpf_selem_unlink(SELEM(sdata));

	return 0;
}

u16 bpf_local_storage_cache_idx_get(struct bpf_local_storage_cache *cache)
{
	u64 min_usage = U64_MAX;
	u16 i, res = 0;

	spin_lock(&cache->idx_lock);

	for (i = 0; i < BPF_LOCAL_STORAGE_CACHE_SIZE; i++) {
		if (cache->idx_usage_counts[i] < min_usage) {
			min_usage = cache->idx_usage_counts[i];
			res = i;

			/* Found a free cache_idx */
			if (!min_usage)
				break;
		}
	}
	cache->idx_usage_counts[res]++;

	spin_unlock(&cache->idx_lock);

	return res;
}

void bpf_local_storage_cache_idx_free(struct bpf_local_storage_cache *cache,
				      u16 idx)
{
	spin_lock(&cache->idx_lock);
	cache->idx_usage_counts[idx]--;
	spin_unlock(&cache->idx_lock);
}

/* Called by __sk_destruct() & bpf_sk_storage_clone() */
void bpf_sk_storage_free(struct sock *sk)
{
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage *sk_storage;
	bool free_sk_storage = false;
	struct hlist_node *n;

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage) {
		rcu_read_unlock();
		return;
	}

	/* Netiher the bpf_prog nor the bpf-map's syscall
	 * could be modifying the sk_storage->list now.
	 * Thus, no elem can be added-to or deleted-from the
	 * sk_storage->list by the bpf_prog or by the bpf-map's syscall.
	 *
	 * It is racing with bpf_local_storage_map_free() alone
	 * when unlinking elem from the sk_storage->list and
	 * the map's bucket->list.
	 */
	raw_spin_lock_bh(&sk_storage->lock);
	hlist_for_each_entry_safe(selem, n, &sk_storage->list, snode) {
		/* Always unlink from map before unlinking from
		 * sk_storage.
		 */
		bpf_selem_unlink_map(selem);
		free_sk_storage = bpf_selem_unlink_storage_nolock(sk_storage,
								  selem, true);
	}
	raw_spin_unlock_bh(&sk_storage->lock);
	rcu_read_unlock();

	if (free_sk_storage)
		kfree_rcu(sk_storage, rcu);
}

void bpf_local_storage_map_free(struct bpf_local_storage_map *smap)
{
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage_map_bucket *b;
	unsigned int i;

	/* Note that this map might be concurrently cloned from
	 * bpf_sk_storage_clone. Wait for any existing bpf_sk_storage_clone
	 * RCU read section to finish before proceeding. New RCU
	 * read sections should be prevented via bpf_map_inc_not_zero.
	 */
	synchronize_rcu();

	/* bpf prog and the userspace can no longer access this map
	 * now.  No new selem (of this map) can be added
	 * to the owner->storage or to the map bucket's list.
	 *
	 * The elem of this map can be cleaned up here
	 * or when the storage is freed e.g.
	 * by bpf_sk_storage_free() during __sk_destruct().
	 */
	for (i = 0; i < (1U << smap->bucket_log); i++) {
		b = &smap->buckets[i];

		rcu_read_lock();
		/* No one is adding to b->list now */
		while ((selem = hlist_entry_safe(
				rcu_dereference_raw(hlist_first_rcu(&b->list)),
				struct bpf_local_storage_elem, map_node))) {
			bpf_selem_unlink(selem);
			cond_resched_rcu();
		}
		rcu_read_unlock();
	}

	/* While freeing the storage we may still need to access the map.
	 *
	 * e.g. when bpf_sk_storage_free() has unlinked selem from the map
	 * which then made the above while((selem = ...)) loop
	 * exit immediately.
	 *
	 * However, while freeing the storage one still needs to access the
	 * smap->elem_size to do the uncharging in
	 * bpf_selem_unlink_storage_nolock().
	 *
	 * Hence, wait another rcu grace period for the storage to be freed.
	 */
	synchronize_rcu();

	kvfree(smap->buckets);
	kfree(smap);
}

static void sk_storage_map_free(struct bpf_map *map)
{
	struct bpf_local_storage_map *smap;

	smap = (struct bpf_local_storage_map *)map;
	bpf_local_storage_cache_idx_free(&sk_cache, smap->cache_idx);
	bpf_local_storage_map_free(smap);
}

/* U16_MAX is much more than enough for sk local storage
 * considering a tcp_sock is ~2k.
 */
#define BPF_LOCAL_STORAGE_MAX_VALUE_SIZE				\
	min_t(u32,							\
	      (KMALLOC_MAX_SIZE - MAX_BPF_STACK -			\
	       sizeof(struct bpf_local_storage_elem)),			\
	      (U16_MAX - sizeof(struct bpf_local_storage_elem)))

int bpf_local_storage_map_alloc_check(union bpf_attr *attr)
{
	if (attr->map_flags & ~BPF_LOCAL_STORAGE_CREATE_FLAG_MASK ||
	    !(attr->map_flags & BPF_F_NO_PREALLOC) ||
	    attr->max_entries ||
	    attr->key_size != sizeof(int) || !attr->value_size ||
	    /* Enforce BTF for userspace sk dumping */
	    !attr->btf_key_type_id || !attr->btf_value_type_id)
		return -EINVAL;

	if (!bpf_capable())
		return -EPERM;

	if (attr->value_size > BPF_LOCAL_STORAGE_MAX_VALUE_SIZE)
		return -E2BIG;

	return 0;
}

struct bpf_local_storage_map *bpf_local_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_local_storage_map *smap;
	unsigned int i;
	u32 nbuckets;
	u64 cost;
	int ret;

	smap = kzalloc(sizeof(*smap), GFP_USER | __GFP_NOWARN);
	if (!smap)
		return ERR_PTR(-ENOMEM);
	bpf_map_init_from_attr(&smap->map, attr);

	nbuckets = roundup_pow_of_two(num_possible_cpus());
	/* Use at least 2 buckets, select_bucket() is undefined behavior with 1 bucket */
	nbuckets = max_t(u32, 2, nbuckets);
	smap->bucket_log = ilog2(nbuckets);
	cost = sizeof(*smap->buckets) * nbuckets + sizeof(*smap);

	ret = bpf_map_charge_init(&smap->map.memory, cost);
	if (ret < 0) {
		kfree(smap);
		return ERR_PTR(ret);
	}

	smap->buckets = kvcalloc(sizeof(*smap->buckets), nbuckets,
				 GFP_USER | __GFP_NOWARN);
	if (!smap->buckets) {
		bpf_map_charge_finish(&smap->map.memory);
		kfree(smap);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < nbuckets; i++) {
		INIT_HLIST_HEAD(&smap->buckets[i].list);
		raw_spin_lock_init(&smap->buckets[i].lock);
	}

	smap->elem_size =
		sizeof(struct bpf_local_storage_elem) + attr->value_size;

	return smap;
}

static struct bpf_map *sk_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_local_storage_map *smap;

	smap = bpf_local_storage_map_alloc(attr);
	if (IS_ERR(smap))
		return ERR_CAST(smap);

	smap->cache_idx = bpf_local_storage_cache_idx_get(&sk_cache);
	return &smap->map;
}

static int notsupp_get_next_key(struct bpf_map *map, void *key,
				void *next_key)
{
	return -ENOTSUPP;
}

int bpf_local_storage_map_check_btf(const struct bpf_map *map,
				    const struct btf *btf,
				    const struct btf_type *key_type,
				    const struct btf_type *value_type)
{
	u32 int_data;

	if (BTF_INFO_KIND(key_type->info) != BTF_KIND_INT)
		return -EINVAL;

	int_data = *(u32 *)(key_type + 1);
	if (BTF_INT_BITS(int_data) != 32 || BTF_INT_OFFSET(int_data))
		return -EINVAL;

	return 0;
}

static void *bpf_fd_sk_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		sdata = sk_storage_lookup(sock->sk, map, true);
		sockfd_put(sock);
		return sdata ? sdata->data : NULL;
	}

	return ERR_PTR(err);
}

static int bpf_fd_sk_storage_update_elem(struct bpf_map *map, void *key,
					 void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		sdata = bpf_local_storage_update(
			sock->sk, (struct bpf_local_storage_map *)map, value,
			map_flags);
		sockfd_put(sock);
		return PTR_ERR_OR_ZERO(sdata);
	}

	return err;
}

static int bpf_fd_sk_storage_delete_elem(struct bpf_map *map, void *key)
{
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		err = sk_storage_delete(sock->sk, map);
		sockfd_put(sock);
		return err;
	}

	return err;
}

static struct bpf_local_storage_elem *
bpf_sk_storage_clone_elem(struct sock *newsk,
			  struct bpf_local_storage_map *smap,
			  struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_elem *copy_selem;

	copy_selem = bpf_selem_alloc(smap, newsk, NULL, true);
	if (!copy_selem)
		return NULL;

	if (map_value_has_spin_lock(&smap->map))
		copy_map_value_locked(&smap->map, SDATA(copy_selem)->data,
				      SDATA(selem)->data, true);
	else
		copy_map_value(&smap->map, SDATA(copy_selem)->data,
			       SDATA(selem)->data);

	return copy_selem;
}

int bpf_sk_storage_clone(const struct sock *sk, struct sock *newsk)
{
	struct bpf_local_storage *new_sk_storage = NULL;
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_elem *selem;
	int ret = 0;

	RCU_INIT_POINTER(newsk->sk_bpf_storage, NULL);

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);

	if (!sk_storage || hlist_empty(&sk_storage->list))
		goto out;

	hlist_for_each_entry_rcu(selem, &sk_storage->list, snode) {
		struct bpf_local_storage_elem *copy_selem;
		struct bpf_local_storage_map *smap;
		struct bpf_map *map;

		smap = rcu_dereference(SDATA(selem)->smap);
		if (!(smap->map.map_flags & BPF_F_CLONE))
			continue;

		/* Note that for lockless listeners adding new element
		 * here can race with cleanup in bpf_local_storage_map_free.
		 * Try to grab map refcnt to make sure that it's still
		 * alive and prevent concurrent removal.
		 */
		map = bpf_map_inc_not_zero(&smap->map);
		if (IS_ERR(map))
			continue;

		copy_selem = bpf_sk_storage_clone_elem(newsk, smap, selem);
		if (!copy_selem) {
			ret = -ENOMEM;
			bpf_map_put(map);
			goto out;
		}

		if (new_sk_storage) {
			bpf_selem_link_map(smap, copy_selem);
			bpf_selem_link_storage_nolock(new_sk_storage, copy_selem);
		} else {
			ret = bpf_local_storage_alloc(newsk, smap, copy_selem);
			if (ret) {
				kfree(copy_selem);
				atomic_sub(smap->elem_size,
					   &newsk->sk_omem_alloc);
				bpf_map_put(map);
				goto out;
			}

			new_sk_storage =
				rcu_dereference(copy_selem->local_storage);
		}
		bpf_map_put(map);
	}

out:
	rcu_read_unlock();

	/* In case of an error, don't free anything explicitly here, the
	 * caller is responsible to call bpf_sk_storage_free.
	 */

	return ret;
}

BPF_CALL_4(bpf_sk_storage_get, struct bpf_map *, map, struct sock *, sk,
	   void *, value, u64, flags)
{
	struct bpf_local_storage_data *sdata;

	if (flags > BPF_SK_STORAGE_GET_F_CREATE)
		return (unsigned long)NULL;

	sdata = sk_storage_lookup(sk, map, true);
	if (sdata)
		return (unsigned long)sdata->data;

	if (flags == BPF_SK_STORAGE_GET_F_CREATE &&
	    /* Cannot add new elem to a going away sk.
	     * Otherwise, the new elem may become a leak
	     * (and also other memory issues during map
	     *  destruction).
	     */
	    refcount_inc_not_zero(&sk->sk_refcnt)) {
		sdata = bpf_local_storage_update(
			sk, (struct bpf_local_storage_map *)map, value,
			BPF_NOEXIST);
		/* sk must be a fullsock (guaranteed by verifier),
		 * so sock_gen_put() is unnecessary.
		 */
		sock_put(sk);
		return IS_ERR(sdata) ?
			(unsigned long)NULL : (unsigned long)sdata->data;
	}

	return (unsigned long)NULL;
}

BPF_CALL_2(bpf_sk_storage_delete, struct bpf_map *, map, struct sock *, sk)
{
	if (refcount_inc_not_zero(&sk->sk_refcnt)) {
		int err;

		err = sk_storage_delete(sk, map);
		sock_put(sk);
		return err;
	}

	return -ENOENT;
}

static int sk_storage_charge(struct bpf_local_storage_map *smap,
			     void *owner, u32 size)
{
	return omem_charge(owner, size);
}

static void sk_storage_uncharge(struct bpf_local_storage_map *smap,
				void *owner, u32 size)
{
	struct sock *sk = owner;

	atomic_sub(size, &sk->sk_omem_alloc);
}

static struct bpf_local_storage __rcu **
sk_storage_ptr(void *owner)
{
	struct sock *sk = owner;

	return &sk->sk_bpf_storage;
}

static int sk_storage_map_btf_id;
const struct bpf_map_ops sk_storage_map_ops = {
	.map_alloc_check = bpf_local_storage_map_alloc_check,
	.map_alloc = sk_storage_map_alloc,
	.map_free = sk_storage_map_free,
	.map_get_next_key = notsupp_get_next_key,
	.map_lookup_elem = bpf_fd_sk_storage_lookup_elem,
	.map_update_elem = bpf_fd_sk_storage_update_elem,
	.map_delete_elem = bpf_fd_sk_storage_delete_elem,
	.map_check_btf = bpf_local_storage_map_check_btf,
	.map_btf_name = "bpf_local_storage_map",
	.map_btf_id = &sk_storage_map_btf_id,
	.map_local_storage_charge = sk_storage_charge,
	.map_local_storage_uncharge = sk_storage_uncharge,
	.map_owner_storage_ptr = sk_storage_ptr,
};

const struct bpf_func_proto bpf_sk_storage_get_proto = {
	.func		= bpf_sk_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_SOCKET,
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_sk_storage_get_cg_sock_proto = {
	.func		= bpf_sk_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_CTX, /* context is 'struct sock' */
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_sk_storage_delete_proto = {
	.func		= bpf_sk_storage_delete,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_SOCKET,
};

struct bpf_sk_storage_diag {
	u32 nr_maps;
	struct bpf_map *maps[];
};

/* The reply will be like:
 * INET_DIAG_BPF_SK_STORAGES (nla_nest)
 *	SK_DIAG_BPF_STORAGE (nla_nest)
 *		SK_DIAG_BPF_STORAGE_MAP_ID (nla_put_u32)
 *		SK_DIAG_BPF_STORAGE_MAP_VALUE (nla_reserve_64bit)
 *	SK_DIAG_BPF_STORAGE (nla_nest)
 *		SK_DIAG_BPF_STORAGE_MAP_ID (nla_put_u32)
 *		SK_DIAG_BPF_STORAGE_MAP_VALUE (nla_reserve_64bit)
 *	....
 */
static int nla_value_size(u32 value_size)
{
	/* SK_DIAG_BPF_STORAGE (nla_nest)
	 *	SK_DIAG_BPF_STORAGE_MAP_ID (nla_put_u32)
	 *	SK_DIAG_BPF_STORAGE_MAP_VALUE (nla_reserve_64bit)
	 */
	return nla_total_size(0) + nla_total_size(sizeof(u32)) +
		nla_total_size_64bit(value_size);
}

void bpf_sk_storage_diag_free(struct bpf_sk_storage_diag *diag)
{
	u32 i;

	if (!diag)
		return;

	for (i = 0; i < diag->nr_maps; i++)
		bpf_map_put(diag->maps[i]);

	kfree(diag);
}
EXPORT_SYMBOL_GPL(bpf_sk_storage_diag_free);

static bool diag_check_dup(const struct bpf_sk_storage_diag *diag,
			   const struct bpf_map *map)
{
	u32 i;

	for (i = 0; i < diag->nr_maps; i++) {
		if (diag->maps[i] == map)
			return true;
	}

	return false;
}

struct bpf_sk_storage_diag *
bpf_sk_storage_diag_alloc(const struct nlattr *nla_stgs)
{
	struct bpf_sk_storage_diag *diag;
	struct nlattr *nla;
	u32 nr_maps = 0;
	int rem, err;

	/* bpf_local_storage_map is currently limited to CAP_SYS_ADMIN as
	 * the map_alloc_check() side also does.
	 */
	if (!bpf_capable())
		return ERR_PTR(-EPERM);

	nla_for_each_nested(nla, nla_stgs, rem) {
		if (nla_type(nla) == SK_DIAG_BPF_STORAGE_REQ_MAP_FD)
			nr_maps++;
	}

	diag = kzalloc(sizeof(*diag) + sizeof(diag->maps[0]) * nr_maps,
		       GFP_KERNEL);
	if (!diag)
		return ERR_PTR(-ENOMEM);

	nla_for_each_nested(nla, nla_stgs, rem) {
		struct bpf_map *map;
		int map_fd;

		if (nla_type(nla) != SK_DIAG_BPF_STORAGE_REQ_MAP_FD)
			continue;

		map_fd = nla_get_u32(nla);
		map = bpf_map_get(map_fd);
		if (IS_ERR(map)) {
			err = PTR_ERR(map);
			goto err_free;
		}
		if (map->map_type != BPF_MAP_TYPE_SK_STORAGE) {
			bpf_map_put(map);
			err = -EINVAL;
			goto err_free;
		}
		if (diag_check_dup(diag, map)) {
			bpf_map_put(map);
			err = -EEXIST;
			goto err_free;
		}
		diag->maps[diag->nr_maps++] = map;
	}

	return diag;

err_free:
	bpf_sk_storage_diag_free(diag);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(bpf_sk_storage_diag_alloc);

static int diag_get(struct bpf_local_storage_data *sdata, struct sk_buff *skb)
{
	struct nlattr *nla_stg, *nla_value;
	struct bpf_local_storage_map *smap;

	/* It cannot exceed max nlattr's payload */
	BUILD_BUG_ON(U16_MAX - NLA_HDRLEN < BPF_LOCAL_STORAGE_MAX_VALUE_SIZE);

	nla_stg = nla_nest_start(skb, SK_DIAG_BPF_STORAGE);
	if (!nla_stg)
		return -EMSGSIZE;

	smap = rcu_dereference(sdata->smap);
	if (nla_put_u32(skb, SK_DIAG_BPF_STORAGE_MAP_ID, smap->map.id))
		goto errout;

	nla_value = nla_reserve_64bit(skb, SK_DIAG_BPF_STORAGE_MAP_VALUE,
				      smap->map.value_size,
				      SK_DIAG_BPF_STORAGE_PAD);
	if (!nla_value)
		goto errout;

	if (map_value_has_spin_lock(&smap->map))
		copy_map_value_locked(&smap->map, nla_data(nla_value),
				      sdata->data, true);
	else
		copy_map_value(&smap->map, nla_data(nla_value), sdata->data);

	nla_nest_end(skb, nla_stg);
	return 0;

errout:
	nla_nest_cancel(skb, nla_stg);
	return -EMSGSIZE;
}

static int bpf_sk_storage_diag_put_all(struct sock *sk, struct sk_buff *skb,
				       int stg_array_type,
				       unsigned int *res_diag_size)
{
	/* stg_array_type (e.g. INET_DIAG_BPF_SK_STORAGES) */
	unsigned int diag_size = nla_total_size(0);
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage_map *smap;
	struct nlattr *nla_stgs;
	unsigned int saved_len;
	int err = 0;

	rcu_read_lock();

	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage || hlist_empty(&sk_storage->list)) {
		rcu_read_unlock();
		return 0;
	}

	nla_stgs = nla_nest_start(skb, stg_array_type);
	if (!nla_stgs)
		/* Continue to learn diag_size */
		err = -EMSGSIZE;

	saved_len = skb->len;
	hlist_for_each_entry_rcu(selem, &sk_storage->list, snode) {
		smap = rcu_dereference(SDATA(selem)->smap);
		diag_size += nla_value_size(smap->map.value_size);

		if (nla_stgs && diag_get(SDATA(selem), skb))
			/* Continue to learn diag_size */
			err = -EMSGSIZE;
	}

	rcu_read_unlock();

	if (nla_stgs) {
		if (saved_len == skb->len)
			nla_nest_cancel(skb, nla_stgs);
		else
			nla_nest_end(skb, nla_stgs);
	}

	if (diag_size == nla_total_size(0)) {
		*res_diag_size = 0;
		return 0;
	}

	*res_diag_size = diag_size;
	return err;
}

int bpf_sk_storage_diag_put(struct bpf_sk_storage_diag *diag,
			    struct sock *sk, struct sk_buff *skb,
			    int stg_array_type,
			    unsigned int *res_diag_size)
{
	/* stg_array_type (e.g. INET_DIAG_BPF_SK_STORAGES) */
	unsigned int diag_size = nla_total_size(0);
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_data *sdata;
	struct nlattr *nla_stgs;
	unsigned int saved_len;
	int err = 0;
	u32 i;

	*res_diag_size = 0;

	/* No map has been specified.  Dump all. */
	if (!diag->nr_maps)
		return bpf_sk_storage_diag_put_all(sk, skb, stg_array_type,
						   res_diag_size);

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage || hlist_empty(&sk_storage->list)) {
		rcu_read_unlock();
		return 0;
	}

	nla_stgs = nla_nest_start(skb, stg_array_type);
	if (!nla_stgs)
		/* Continue to learn diag_size */
		err = -EMSGSIZE;

	saved_len = skb->len;
	for (i = 0; i < diag->nr_maps; i++) {
		sdata = bpf_local_storage_lookup(sk_storage,
				(struct bpf_local_storage_map *)diag->maps[i],
				false);

		if (!sdata)
			continue;

		diag_size += nla_value_size(diag->maps[i]->value_size);

		if (nla_stgs && diag_get(sdata, skb))
			/* Continue to learn diag_size */
			err = -EMSGSIZE;
	}
	rcu_read_unlock();

	if (nla_stgs) {
		if (saved_len == skb->len)
			nla_nest_cancel(skb, nla_stgs);
		else
			nla_nest_end(skb, nla_stgs);
	}

	if (diag_size == nla_total_size(0)) {
		*res_diag_size = 0;
		return 0;
	}

	*res_diag_size = diag_size;
	return err;
}
EXPORT_SYMBOL_GPL(bpf_sk_storage_diag_put);

struct bpf_iter_seq_sk_storage_map_info {
	struct bpf_map *map;
	unsigned int bucket_id;
	unsigned skip_elems;
};

static struct bpf_local_storage_elem *
bpf_sk_storage_map_seq_find_next(struct bpf_iter_seq_sk_storage_map_info *info,
				 struct bpf_local_storage_elem *prev_selem)
{
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_elem *selem;
	u32 skip_elems = info->skip_elems;
	struct bpf_local_storage_map *smap;
	u32 bucket_id = info->bucket_id;
	u32 i, count, n_buckets;
	struct bpf_local_storage_map_bucket *b;

	smap = (struct bpf_local_storage_map *)info->map;
	n_buckets = 1U << smap->bucket_log;
	if (bucket_id >= n_buckets)
		return NULL;

	/* try to find next selem in the same bucket */
	selem = prev_selem;
	count = 0;
	while (selem) {
		selem = hlist_entry_safe(selem->map_node.next,
					 struct bpf_local_storage_elem, map_node);
		if (!selem) {
			/* not found, unlock and go to the next bucket */
			b = &smap->buckets[bucket_id++];
			raw_spin_unlock_bh(&b->lock);
			skip_elems = 0;
			break;
		}
		sk_storage = rcu_dereference_raw(selem->local_storage);
		if (sk_storage) {
			info->skip_elems = skip_elems + count;
			return selem;
		}
		count++;
	}

	for (i = bucket_id; i < (1U << smap->bucket_log); i++) {
		b = &smap->buckets[i];
		raw_spin_lock_bh(&b->lock);
		count = 0;
		hlist_for_each_entry(selem, &b->list, map_node) {
			sk_storage = rcu_dereference_raw(selem->local_storage);
			if (sk_storage && count >= skip_elems) {
				info->bucket_id = i;
				info->skip_elems = count;
				return selem;
			}
			count++;
		}
		raw_spin_unlock_bh(&b->lock);
		skip_elems = 0;
	}

	info->bucket_id = i;
	info->skip_elems = 0;
	return NULL;
}

static void *bpf_sk_storage_map_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_local_storage_elem *selem;

	selem = bpf_sk_storage_map_seq_find_next(seq->private, NULL);
	if (!selem)
		return NULL;

	if (*pos == 0)
		++*pos;
	return selem;
}

static void *bpf_sk_storage_map_seq_next(struct seq_file *seq, void *v,
					 loff_t *pos)
{
	struct bpf_iter_seq_sk_storage_map_info *info = seq->private;

	++*pos;
	++info->skip_elems;
	return bpf_sk_storage_map_seq_find_next(seq->private, v);
}

struct bpf_iter__bpf_sk_storage_map {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_map *, map);
	__bpf_md_ptr(struct sock *, sk);
	__bpf_md_ptr(void *, value);
};

DEFINE_BPF_ITER_FUNC(bpf_sk_storage_map, struct bpf_iter_meta *meta,
		     struct bpf_map *map, struct sock *sk,
		     void *value)

static int __bpf_sk_storage_map_seq_show(struct seq_file *seq,
					 struct bpf_local_storage_elem *selem)
{
	struct bpf_iter_seq_sk_storage_map_info *info = seq->private;
	struct bpf_iter__bpf_sk_storage_map ctx = {};
	struct bpf_local_storage *sk_storage;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	int ret = 0;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, selem == NULL);
	if (prog) {
		ctx.meta = &meta;
		ctx.map = info->map;
		if (selem) {
			sk_storage = rcu_dereference_raw(selem->local_storage);
			ctx.sk = sk_storage->owner;
			ctx.value = SDATA(selem)->data;
		}
		ret = bpf_iter_run_prog(prog, &ctx);
	}

	return ret;
}

static int bpf_sk_storage_map_seq_show(struct seq_file *seq, void *v)
{
	return __bpf_sk_storage_map_seq_show(seq, v);
}

static void bpf_sk_storage_map_seq_stop(struct seq_file *seq, void *v)
{
	struct bpf_iter_seq_sk_storage_map_info *info = seq->private;
	struct bpf_local_storage_map *smap;
	struct bpf_local_storage_map_bucket *b;

	if (!v) {
		(void)__bpf_sk_storage_map_seq_show(seq, v);
	} else {
		smap = (struct bpf_local_storage_map *)info->map;
		b = &smap->buckets[info->bucket_id];
		raw_spin_unlock_bh(&b->lock);
	}
}

static int bpf_iter_init_sk_storage_map(void *priv_data,
					struct bpf_iter_aux_info *aux)
{
	struct bpf_iter_seq_sk_storage_map_info *seq_info = priv_data;

	seq_info->map = aux->map;
	return 0;
}

static int bpf_iter_attach_map(struct bpf_prog *prog,
			       union bpf_iter_link_info *linfo,
			       struct bpf_iter_aux_info *aux)
{
	struct bpf_map *map;
	int err = -EINVAL;

	if (!linfo->map.map_fd)
		return -EBADF;

	map = bpf_map_get_with_uref(linfo->map.map_fd);
	if (IS_ERR(map))
		return PTR_ERR(map);

	if (map->map_type != BPF_MAP_TYPE_SK_STORAGE)
		goto put_map;

	if (prog->aux->max_rdonly_access > map->value_size) {
		err = -EACCES;
		goto put_map;
	}

	aux->map = map;
	return 0;

put_map:
	bpf_map_put_with_uref(map);
	return err;
}

static void bpf_iter_detach_map(struct bpf_iter_aux_info *aux)
{
	bpf_map_put_with_uref(aux->map);
}

static const struct seq_operations bpf_sk_storage_map_seq_ops = {
	.start  = bpf_sk_storage_map_seq_start,
	.next   = bpf_sk_storage_map_seq_next,
	.stop   = bpf_sk_storage_map_seq_stop,
	.show   = bpf_sk_storage_map_seq_show,
};

static const struct bpf_iter_seq_info iter_seq_info = {
	.seq_ops		= &bpf_sk_storage_map_seq_ops,
	.init_seq_private	= bpf_iter_init_sk_storage_map,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_sk_storage_map_info),
};

static struct bpf_iter_reg bpf_sk_storage_map_reg_info = {
	.target			= "bpf_sk_storage_map",
	.attach_target		= bpf_iter_attach_map,
	.detach_target		= bpf_iter_detach_map,
	.show_fdinfo		= bpf_iter_map_show_fdinfo,
	.fill_link_info		= bpf_iter_map_fill_link_info,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_sk_storage_map, sk),
		  PTR_TO_BTF_ID_OR_NULL },
		{ offsetof(struct bpf_iter__bpf_sk_storage_map, value),
		  PTR_TO_RDWR_BUF_OR_NULL },
	},
	.seq_info		= &iter_seq_info,
};

static int __init bpf_sk_storage_map_iter_init(void)
{
	bpf_sk_storage_map_reg_info.ctx_arg_info[0].btf_id =
		btf_sock_ids[BTF_SOCK_TYPE_SOCK];
	return bpf_iter_reg_target(&bpf_sk_storage_map_reg_info);
}
late_initcall(bpf_sk_storage_map_iter_init);
