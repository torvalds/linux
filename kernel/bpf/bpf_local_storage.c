// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <linux/bpf_local_storage.h>
#include <net/sock.h>
#include <uapi/linux/sock_diag.h>
#include <uapi/linux/btf.h>
#include <linux/rcupdate.h>
#include <linux/rcupdate_trace.h>
#include <linux/rcupdate_wait.h>

#define BPF_LOCAL_STORAGE_CREATE_FLAG_MASK (BPF_F_NO_PREALLOC | BPF_F_CLONE)

static struct bpf_local_storage_map_bucket *
select_bucket(struct bpf_local_storage_map *smap,
	      struct bpf_local_storage_elem *selem)
{
	return &smap->buckets[hash_ptr(selem, smap->bucket_log)];
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

static bool selem_linked_to_storage_lockless(const struct bpf_local_storage_elem *selem)
{
	return !hlist_unhashed_lockless(&selem->snode);
}

static bool selem_linked_to_storage(const struct bpf_local_storage_elem *selem)
{
	return !hlist_unhashed(&selem->snode);
}

static bool selem_linked_to_map_lockless(const struct bpf_local_storage_elem *selem)
{
	return !hlist_unhashed_lockless(&selem->map_node);
}

static bool selem_linked_to_map(const struct bpf_local_storage_elem *selem)
{
	return !hlist_unhashed(&selem->map_node);
}

struct bpf_local_storage_elem *
bpf_selem_alloc(struct bpf_local_storage_map *smap, void *owner,
		void *value, bool swap_uptrs, gfp_t gfp_flags)
{
	struct bpf_local_storage_elem *selem;

	if (mem_charge(smap, owner, smap->elem_size))
		return NULL;

	if (smap->use_kmalloc_nolock) {
		selem = bpf_map_kmalloc_nolock(&smap->map, smap->elem_size,
					       __GFP_ZERO, NUMA_NO_NODE);
	} else {
		selem = bpf_map_kzalloc(&smap->map, smap->elem_size,
					gfp_flags | __GFP_NOWARN);
	}

	if (selem) {
		RCU_INIT_POINTER(SDATA(selem)->smap, smap);

		if (value) {
			/* No need to call check_and_init_map_value as memory is zero init */
			copy_map_value(&smap->map, SDATA(selem)->data, value);
			if (swap_uptrs)
				bpf_obj_swap_uptrs(smap->map.record, SDATA(selem)->data, value);
		}
		return selem;
	}

	mem_uncharge(smap, owner, smap->elem_size);

	return NULL;
}

/* rcu tasks trace callback for use_kmalloc_nolock == false */
static void __bpf_local_storage_free_trace_rcu(struct rcu_head *rcu)
{
	struct bpf_local_storage *local_storage;

	/* If RCU Tasks Trace grace period implies RCU grace period, do
	 * kfree(), else do kfree_rcu().
	 */
	local_storage = container_of(rcu, struct bpf_local_storage, rcu);
	if (rcu_trace_implies_rcu_gp())
		kfree(local_storage);
	else
		kfree_rcu(local_storage, rcu);
}

/* Handle use_kmalloc_nolock == false */
static void __bpf_local_storage_free(struct bpf_local_storage *local_storage,
				     bool vanilla_rcu)
{
	if (vanilla_rcu)
		kfree_rcu(local_storage, rcu);
	else
		call_rcu_tasks_trace(&local_storage->rcu,
				     __bpf_local_storage_free_trace_rcu);
}

static void bpf_local_storage_free_rcu(struct rcu_head *rcu)
{
	struct bpf_local_storage *local_storage;

	local_storage = container_of(rcu, struct bpf_local_storage, rcu);
	kfree_nolock(local_storage);
}

static void bpf_local_storage_free_trace_rcu(struct rcu_head *rcu)
{
	if (rcu_trace_implies_rcu_gp())
		bpf_local_storage_free_rcu(rcu);
	else
		call_rcu(rcu, bpf_local_storage_free_rcu);
}

static void bpf_local_storage_free(struct bpf_local_storage *local_storage,
				   bool reuse_now)
{
	if (!local_storage)
		return;

	if (!local_storage->use_kmalloc_nolock) {
		__bpf_local_storage_free(local_storage, reuse_now);
		return;
	}

	if (reuse_now) {
		call_rcu(&local_storage->rcu, bpf_local_storage_free_rcu);
		return;
	}

	call_rcu_tasks_trace(&local_storage->rcu,
			     bpf_local_storage_free_trace_rcu);
}

/* rcu tasks trace callback for use_kmalloc_nolock == false */
static void __bpf_selem_free_trace_rcu(struct rcu_head *rcu)
{
	struct bpf_local_storage_elem *selem;

	selem = container_of(rcu, struct bpf_local_storage_elem, rcu);
	if (rcu_trace_implies_rcu_gp())
		kfree(selem);
	else
		kfree_rcu(selem, rcu);
}

/* Handle use_kmalloc_nolock == false */
static void __bpf_selem_free(struct bpf_local_storage_elem *selem,
			     bool vanilla_rcu)
{
	if (vanilla_rcu)
		kfree_rcu(selem, rcu);
	else
		call_rcu_tasks_trace(&selem->rcu, __bpf_selem_free_trace_rcu);
}

static void bpf_selem_free_rcu(struct rcu_head *rcu)
{
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage_map *smap;

	selem = container_of(rcu, struct bpf_local_storage_elem, rcu);
	/* The bpf_local_storage_map_free will wait for rcu_barrier */
	smap = rcu_dereference_check(SDATA(selem)->smap, 1);

	migrate_disable();
	bpf_obj_free_fields(smap->map.record, SDATA(selem)->data);
	migrate_enable();
	kfree_nolock(selem);
}

static void bpf_selem_free_trace_rcu(struct rcu_head *rcu)
{
	if (rcu_trace_implies_rcu_gp())
		bpf_selem_free_rcu(rcu);
	else
		call_rcu(rcu, bpf_selem_free_rcu);
}

void bpf_selem_free(struct bpf_local_storage_elem *selem,
		    bool reuse_now)
{
	struct bpf_local_storage_map *smap;

	smap = rcu_dereference_check(SDATA(selem)->smap, bpf_rcu_lock_held());

	if (!smap->use_kmalloc_nolock) {
		/*
		 * No uptr will be unpin even when reuse_now == false since uptr
		 * is only supported in task local storage, where
		 * smap->use_kmalloc_nolock == true.
		 */
		bpf_obj_free_fields(smap->map.record, SDATA(selem)->data);
		__bpf_selem_free(selem, reuse_now);
		return;
	}

	if (reuse_now) {
		/*
		 * While it is okay to call bpf_obj_free_fields() that unpins uptr when
		 * reuse_now == true, keep it in bpf_selem_free_rcu() for simplicity.
		 */
		call_rcu(&selem->rcu, bpf_selem_free_rcu);
		return;
	}

	call_rcu_tasks_trace(&selem->rcu, bpf_selem_free_trace_rcu);
}

static void bpf_selem_free_list(struct hlist_head *list, bool reuse_now)
{
	struct bpf_local_storage_elem *selem;
	struct hlist_node *n;

	/* The "_safe" iteration is needed.
	 * The loop is not removing the selem from the list
	 * but bpf_selem_free will use the selem->rcu_head
	 * which is union-ized with the selem->free_node.
	 */
	hlist_for_each_entry_safe(selem, n, list, free_node)
		bpf_selem_free(selem, reuse_now);
}

/* local_storage->lock must be held and selem->local_storage == local_storage.
 * The caller must ensure selem->smap is still valid to be
 * dereferenced for its smap->elem_size and smap->cache_idx.
 */
static bool bpf_selem_unlink_storage_nolock(struct bpf_local_storage *local_storage,
					    struct bpf_local_storage_elem *selem,
					    struct hlist_head *free_selem_list)
{
	struct bpf_local_storage_map *smap;
	bool free_local_storage;
	void *owner;

	smap = rcu_dereference_check(SDATA(selem)->smap, bpf_rcu_lock_held());
	owner = local_storage->owner;

	/* All uncharging on the owner must be done first.
	 * The owner may be freed once the last selem is unlinked
	 * from local_storage.
	 */
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
		 * rcu_read_lock(),  it is more intuitive to
		 * read if the freeing of the storage is done
		 * after the raw_spin_unlock_bh(&local_storage->lock).
		 *
		 * Hence, a "bool free_local_storage" is returned
		 * to the caller which then calls then frees the storage after
		 * all the RCU grace periods have expired.
		 */
	}
	hlist_del_init_rcu(&selem->snode);
	if (rcu_access_pointer(local_storage->cache[smap->cache_idx]) ==
	    SDATA(selem))
		RCU_INIT_POINTER(local_storage->cache[smap->cache_idx], NULL);

	hlist_add_head(&selem->free_node, free_selem_list);

	if (rcu_access_pointer(local_storage->smap) == smap)
		RCU_INIT_POINTER(local_storage->smap, NULL);

	return free_local_storage;
}

static void bpf_selem_unlink_storage(struct bpf_local_storage_elem *selem,
				     bool reuse_now)
{
	struct bpf_local_storage *local_storage;
	bool free_local_storage = false;
	HLIST_HEAD(selem_free_list);
	unsigned long flags;

	if (unlikely(!selem_linked_to_storage_lockless(selem)))
		/* selem has already been unlinked from sk */
		return;

	local_storage = rcu_dereference_check(selem->local_storage,
					      bpf_rcu_lock_held());

	raw_spin_lock_irqsave(&local_storage->lock, flags);
	if (likely(selem_linked_to_storage(selem)))
		free_local_storage = bpf_selem_unlink_storage_nolock(
			local_storage, selem, &selem_free_list);
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);

	bpf_selem_free_list(&selem_free_list, reuse_now);

	if (free_local_storage)
		bpf_local_storage_free(local_storage, reuse_now);
}

void bpf_selem_link_storage_nolock(struct bpf_local_storage *local_storage,
				   struct bpf_local_storage_elem *selem)
{
	RCU_INIT_POINTER(selem->local_storage, local_storage);
	hlist_add_head_rcu(&selem->snode, &local_storage->list);
}

static void bpf_selem_unlink_map(struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_map *smap;
	struct bpf_local_storage_map_bucket *b;
	unsigned long flags;

	if (unlikely(!selem_linked_to_map_lockless(selem)))
		/* selem has already be unlinked from smap */
		return;

	smap = rcu_dereference_check(SDATA(selem)->smap, bpf_rcu_lock_held());
	b = select_bucket(smap, selem);
	raw_spin_lock_irqsave(&b->lock, flags);
	if (likely(selem_linked_to_map(selem)))
		hlist_del_init_rcu(&selem->map_node);
	raw_spin_unlock_irqrestore(&b->lock, flags);
}

void bpf_selem_link_map(struct bpf_local_storage_map *smap,
			struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_map_bucket *b = select_bucket(smap, selem);
	unsigned long flags;

	raw_spin_lock_irqsave(&b->lock, flags);
	hlist_add_head_rcu(&selem->map_node, &b->list);
	raw_spin_unlock_irqrestore(&b->lock, flags);
}

void bpf_selem_unlink(struct bpf_local_storage_elem *selem, bool reuse_now)
{
	/* Always unlink from map before unlinking from local_storage
	 * because selem will be freed after successfully unlinked from
	 * the local_storage.
	 */
	bpf_selem_unlink_map(selem);
	bpf_selem_unlink_storage(selem, reuse_now);
}

void __bpf_local_storage_insert_cache(struct bpf_local_storage *local_storage,
				      struct bpf_local_storage_map *smap,
				      struct bpf_local_storage_elem *selem)
{
	unsigned long flags;

	/* spinlock is needed to avoid racing with the
	 * parallel delete.  Otherwise, publishing an already
	 * deleted sdata to the cache will become a use-after-free
	 * problem in the next bpf_local_storage_lookup().
	 */
	raw_spin_lock_irqsave(&local_storage->lock, flags);
	if (selem_linked_to_storage(selem))
		rcu_assign_pointer(local_storage->cache[smap->cache_idx], SDATA(selem));
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);
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
			    struct bpf_local_storage_elem *first_selem,
			    gfp_t gfp_flags)
{
	struct bpf_local_storage *prev_storage, *storage;
	struct bpf_local_storage **owner_storage_ptr;
	int err;

	err = mem_charge(smap, owner, sizeof(*storage));
	if (err)
		return err;

	if (smap->use_kmalloc_nolock)
		storage = bpf_map_kmalloc_nolock(&smap->map, sizeof(*storage),
						 __GFP_ZERO, NUMA_NO_NODE);
	else
		storage = bpf_map_kzalloc(&smap->map, sizeof(*storage),
					  gfp_flags | __GFP_NOWARN);
	if (!storage) {
		err = -ENOMEM;
		goto uncharge;
	}

	RCU_INIT_POINTER(storage->smap, smap);
	INIT_HLIST_HEAD(&storage->list);
	raw_spin_lock_init(&storage->lock);
	storage->owner = owner;
	storage->use_kmalloc_nolock = smap->use_kmalloc_nolock;

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
	}

	return 0;

uncharge:
	bpf_local_storage_free(storage, true);
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
			 void *value, u64 map_flags, bool swap_uptrs, gfp_t gfp_flags)
{
	struct bpf_local_storage_data *old_sdata = NULL;
	struct bpf_local_storage_elem *alloc_selem, *selem = NULL;
	struct bpf_local_storage *local_storage;
	HLIST_HEAD(old_selem_free_list);
	unsigned long flags;
	int err;

	/* BPF_EXIST and BPF_NOEXIST cannot be both set */
	if (unlikely((map_flags & ~BPF_F_LOCK) > BPF_EXIST) ||
	    /* BPF_F_LOCK can only be used in a value with spin_lock */
	    unlikely((map_flags & BPF_F_LOCK) &&
		     !btf_record_has_field(smap->map.record, BPF_SPIN_LOCK)))
		return ERR_PTR(-EINVAL);

	if (gfp_flags == GFP_KERNEL && (map_flags & ~BPF_F_LOCK) != BPF_NOEXIST)
		return ERR_PTR(-EINVAL);

	local_storage = rcu_dereference_check(*owner_storage(smap, owner),
					      bpf_rcu_lock_held());
	if (!local_storage || hlist_empty(&local_storage->list)) {
		/* Very first elem for the owner */
		err = check_flags(NULL, map_flags);
		if (err)
			return ERR_PTR(err);

		selem = bpf_selem_alloc(smap, owner, value, swap_uptrs, gfp_flags);
		if (!selem)
			return ERR_PTR(-ENOMEM);

		err = bpf_local_storage_alloc(owner, smap, selem, gfp_flags);
		if (err) {
			bpf_selem_free(selem, true);
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
		if (old_sdata && selem_linked_to_storage_lockless(SELEM(old_sdata))) {
			copy_map_value_locked(&smap->map, old_sdata->data,
					      value, false);
			return old_sdata;
		}
	}

	/* A lookup has just been done before and concluded a new selem is
	 * needed. The chance of an unnecessary alloc is unlikely.
	 */
	alloc_selem = selem = bpf_selem_alloc(smap, owner, value, swap_uptrs, gfp_flags);
	if (!alloc_selem)
		return ERR_PTR(-ENOMEM);

	raw_spin_lock_irqsave(&local_storage->lock, flags);

	/* Recheck local_storage->list under local_storage->lock */
	if (unlikely(hlist_empty(&local_storage->list))) {
		/* A parallel del is happening and local_storage is going
		 * away.  It has just been checked before, so very
		 * unlikely.  Return instead of retry to keep things
		 * simple.
		 */
		err = -EAGAIN;
		goto unlock;
	}

	old_sdata = bpf_local_storage_lookup(local_storage, smap, false);
	err = check_flags(old_sdata, map_flags);
	if (err)
		goto unlock;

	if (old_sdata && (map_flags & BPF_F_LOCK)) {
		copy_map_value_locked(&smap->map, old_sdata->data, value,
				      false);
		selem = SELEM(old_sdata);
		goto unlock;
	}

	alloc_selem = NULL;
	/* First, link the new selem to the map */
	bpf_selem_link_map(smap, selem);

	/* Second, link (and publish) the new selem to local_storage */
	bpf_selem_link_storage_nolock(local_storage, selem);

	/* Third, remove old selem, SELEM(old_sdata) */
	if (old_sdata) {
		bpf_selem_unlink_map(SELEM(old_sdata));
		bpf_selem_unlink_storage_nolock(local_storage, SELEM(old_sdata),
						&old_selem_free_list);
	}

unlock:
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);
	bpf_selem_free_list(&old_selem_free_list, false);
	if (alloc_selem) {
		mem_uncharge(smap, owner, smap->elem_size);
		bpf_selem_free(alloc_selem, true);
	}
	return err ? ERR_PTR(err) : SDATA(selem);
}

static u16 bpf_local_storage_cache_idx_get(struct bpf_local_storage_cache *cache)
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

static void bpf_local_storage_cache_idx_free(struct bpf_local_storage_cache *cache,
					     u16 idx)
{
	spin_lock(&cache->idx_lock);
	cache->idx_usage_counts[idx]--;
	spin_unlock(&cache->idx_lock);
}

int bpf_local_storage_map_alloc_check(union bpf_attr *attr)
{
	if (attr->map_flags & ~BPF_LOCAL_STORAGE_CREATE_FLAG_MASK ||
	    !(attr->map_flags & BPF_F_NO_PREALLOC) ||
	    attr->max_entries ||
	    attr->key_size != sizeof(int) || !attr->value_size ||
	    /* Enforce BTF for userspace sk dumping */
	    !attr->btf_key_type_id || !attr->btf_value_type_id)
		return -EINVAL;

	if (attr->value_size > BPF_LOCAL_STORAGE_MAX_VALUE_SIZE)
		return -E2BIG;

	return 0;
}

int bpf_local_storage_map_check_btf(const struct bpf_map *map,
				    const struct btf *btf,
				    const struct btf_type *key_type,
				    const struct btf_type *value_type)
{
	if (!btf_type_is_i32(key_type))
		return -EINVAL;

	return 0;
}

void bpf_local_storage_destroy(struct bpf_local_storage *local_storage)
{
	struct bpf_local_storage_elem *selem;
	bool free_storage = false;
	HLIST_HEAD(free_selem_list);
	struct hlist_node *n;
	unsigned long flags;

	/* Neither the bpf_prog nor the bpf_map's syscall
	 * could be modifying the local_storage->list now.
	 * Thus, no elem can be added to or deleted from the
	 * local_storage->list by the bpf_prog or by the bpf_map's syscall.
	 *
	 * It is racing with bpf_local_storage_map_free() alone
	 * when unlinking elem from the local_storage->list and
	 * the map's bucket->list.
	 */
	raw_spin_lock_irqsave(&local_storage->lock, flags);
	hlist_for_each_entry_safe(selem, n, &local_storage->list, snode) {
		/* Always unlink from map before unlinking from
		 * local_storage.
		 */
		bpf_selem_unlink_map(selem);
		/* If local_storage list has only one element, the
		 * bpf_selem_unlink_storage_nolock() will return true.
		 * Otherwise, it will return false. The current loop iteration
		 * intends to remove all local storage. So the last iteration
		 * of the loop will set the free_cgroup_storage to true.
		 */
		free_storage = bpf_selem_unlink_storage_nolock(
			local_storage, selem, &free_selem_list);
	}
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);

	bpf_selem_free_list(&free_selem_list, true);

	if (free_storage)
		bpf_local_storage_free(local_storage, true);
}

u64 bpf_local_storage_map_mem_usage(const struct bpf_map *map)
{
	struct bpf_local_storage_map *smap = (struct bpf_local_storage_map *)map;
	u64 usage = sizeof(*smap);

	/* The dynamically callocated selems are not counted currently. */
	usage += sizeof(*smap->buckets) * (1ULL << smap->bucket_log);
	return usage;
}

struct bpf_map *
bpf_local_storage_map_alloc(union bpf_attr *attr,
			    struct bpf_local_storage_cache *cache,
			    bool use_kmalloc_nolock)
{
	struct bpf_local_storage_map *smap;
	unsigned int i;
	u32 nbuckets;
	int err;

	smap = bpf_map_area_alloc(sizeof(*smap), NUMA_NO_NODE);
	if (!smap)
		return ERR_PTR(-ENOMEM);
	bpf_map_init_from_attr(&smap->map, attr);

	nbuckets = roundup_pow_of_two(num_possible_cpus());
	/* Use at least 2 buckets, select_bucket() is undefined behavior with 1 bucket */
	nbuckets = max_t(u32, 2, nbuckets);
	smap->bucket_log = ilog2(nbuckets);

	smap->buckets = bpf_map_kvcalloc(&smap->map, nbuckets,
					 sizeof(*smap->buckets), GFP_USER | __GFP_NOWARN);
	if (!smap->buckets) {
		err = -ENOMEM;
		goto free_smap;
	}

	for (i = 0; i < nbuckets; i++) {
		INIT_HLIST_HEAD(&smap->buckets[i].list);
		raw_spin_lock_init(&smap->buckets[i].lock);
	}

	smap->elem_size = offsetof(struct bpf_local_storage_elem,
				   sdata.data[attr->value_size]);

	/* In PREEMPT_RT, kmalloc(GFP_ATOMIC) is still not safe in non
	 * preemptible context. Thus, enforce all storages to use
	 * kmalloc_nolock() when CONFIG_PREEMPT_RT is enabled.
	 */
	smap->use_kmalloc_nolock = IS_ENABLED(CONFIG_PREEMPT_RT) ? true : use_kmalloc_nolock;

	smap->cache_idx = bpf_local_storage_cache_idx_get(cache);
	return &smap->map;

free_smap:
	kvfree(smap->buckets);
	bpf_map_area_free(smap);
	return ERR_PTR(err);
}

void bpf_local_storage_map_free(struct bpf_map *map,
				struct bpf_local_storage_cache *cache,
				int __percpu *busy_counter)
{
	struct bpf_local_storage_map_bucket *b;
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage_map *smap;
	unsigned int i;

	smap = (struct bpf_local_storage_map *)map;
	bpf_local_storage_cache_idx_free(cache, smap->cache_idx);

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
			if (busy_counter)
				this_cpu_inc(*busy_counter);
			bpf_selem_unlink(selem, true);
			if (busy_counter)
				this_cpu_dec(*busy_counter);
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

	if (smap->use_kmalloc_nolock) {
		rcu_barrier_tasks_trace();
		rcu_barrier();
	}
	kvfree(smap->buckets);
	bpf_map_area_free(smap);
}
