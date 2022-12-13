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
		void *value, bool charge_mem, gfp_t gfp_flags)
{
	struct bpf_local_storage_elem *selem;

	if (charge_mem && mem_charge(smap, owner, smap->elem_size))
		return NULL;

	selem = bpf_map_kzalloc(&smap->map, smap->elem_size,
				gfp_flags | __GFP_NOWARN);
	if (selem) {
		if (value)
			copy_map_value(&smap->map, SDATA(selem)->data, value);
		return selem;
	}

	if (charge_mem)
		mem_uncharge(smap, owner, smap->elem_size);

	return NULL;
}

void bpf_local_storage_free_rcu(struct rcu_head *rcu)
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

static void bpf_selem_free_rcu(struct rcu_head *rcu)
{
	struct bpf_local_storage_elem *selem;

	selem = container_of(rcu, struct bpf_local_storage_elem, rcu);
	if (rcu_trace_implies_rcu_gp())
		kfree(selem);
	else
		kfree_rcu(selem, rcu);
}

/* local_storage->lock must be held and selem->local_storage == local_storage.
 * The caller must ensure selem->smap is still valid to be
 * dereferenced for its smap->elem_size and smap->cache_idx.
 */
static bool bpf_selem_unlink_storage_nolock(struct bpf_local_storage *local_storage,
					    struct bpf_local_storage_elem *selem,
					    bool uncharge_mem, bool use_trace_rcu)
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

	if (use_trace_rcu)
		call_rcu_tasks_trace(&selem->rcu, bpf_selem_free_rcu);
	else
		kfree_rcu(selem, rcu);

	return free_local_storage;
}

static void __bpf_selem_unlink_storage(struct bpf_local_storage_elem *selem,
				       bool use_trace_rcu)
{
	struct bpf_local_storage *local_storage;
	bool free_local_storage = false;
	unsigned long flags;

	if (unlikely(!selem_linked_to_storage(selem)))
		/* selem has already been unlinked from sk */
		return;

	local_storage = rcu_dereference_check(selem->local_storage,
					      bpf_rcu_lock_held());
	raw_spin_lock_irqsave(&local_storage->lock, flags);
	if (likely(selem_linked_to_storage(selem)))
		free_local_storage = bpf_selem_unlink_storage_nolock(
			local_storage, selem, true, use_trace_rcu);
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);

	if (free_local_storage) {
		if (use_trace_rcu)
			call_rcu_tasks_trace(&local_storage->rcu,
				     bpf_local_storage_free_rcu);
		else
			kfree_rcu(local_storage, rcu);
	}
}

void bpf_selem_link_storage_nolock(struct bpf_local_storage *local_storage,
				   struct bpf_local_storage_elem *selem)
{
	RCU_INIT_POINTER(selem->local_storage, local_storage);
	hlist_add_head_rcu(&selem->snode, &local_storage->list);
}

void bpf_selem_unlink_map(struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_map *smap;
	struct bpf_local_storage_map_bucket *b;
	unsigned long flags;

	if (unlikely(!selem_linked_to_map(selem)))
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
	RCU_INIT_POINTER(SDATA(selem)->smap, smap);
	hlist_add_head_rcu(&selem->map_node, &b->list);
	raw_spin_unlock_irqrestore(&b->lock, flags);
}

void bpf_selem_unlink(struct bpf_local_storage_elem *selem, bool use_trace_rcu)
{
	/* Always unlink from map before unlinking from local_storage
	 * because selem will be freed after successfully unlinked from
	 * the local_storage.
	 */
	bpf_selem_unlink_map(selem);
	__bpf_selem_unlink_storage(selem, use_trace_rcu);
}

/* If cacheit_lockit is false, this lookup function is lockless */
struct bpf_local_storage_data *
bpf_local_storage_lookup(struct bpf_local_storage *local_storage,
			 struct bpf_local_storage_map *smap,
			 bool cacheit_lockit)
{
	struct bpf_local_storage_data *sdata;
	struct bpf_local_storage_elem *selem;

	/* Fast path (cache hit) */
	sdata = rcu_dereference_check(local_storage->cache[smap->cache_idx],
				      bpf_rcu_lock_held());
	if (sdata && rcu_access_pointer(sdata->smap) == smap)
		return sdata;

	/* Slow path (cache miss) */
	hlist_for_each_entry_rcu(selem, &local_storage->list, snode,
				  rcu_read_lock_trace_held())
		if (rcu_access_pointer(SDATA(selem)->smap) == smap)
			break;

	if (!selem)
		return NULL;

	sdata = SDATA(selem);
	if (cacheit_lockit) {
		unsigned long flags;

		/* spinlock is needed to avoid racing with the
		 * parallel delete.  Otherwise, publishing an already
		 * deleted sdata to the cache will become a use-after-free
		 * problem in the next bpf_local_storage_lookup().
		 */
		raw_spin_lock_irqsave(&local_storage->lock, flags);
		if (selem_linked_to_storage(selem))
			rcu_assign_pointer(local_storage->cache[smap->cache_idx],
					   sdata);
		raw_spin_unlock_irqrestore(&local_storage->lock, flags);
	}

	return sdata;
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

	storage = bpf_map_kzalloc(&smap->map, sizeof(*storage),
				  gfp_flags | __GFP_NOWARN);
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
		 * synchronize_rcu_mult (waiting for both sleepable and
		 * normal programs) before walking the bucket->list.
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
			 void *value, u64 map_flags, gfp_t gfp_flags)
{
	struct bpf_local_storage_data *old_sdata = NULL;
	struct bpf_local_storage_elem *selem = NULL;
	struct bpf_local_storage *local_storage;
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

		selem = bpf_selem_alloc(smap, owner, value, true, gfp_flags);
		if (!selem)
			return ERR_PTR(-ENOMEM);

		err = bpf_local_storage_alloc(owner, smap, selem, gfp_flags);
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

	if (gfp_flags == GFP_KERNEL) {
		selem = bpf_selem_alloc(smap, owner, value, true, gfp_flags);
		if (!selem)
			return ERR_PTR(-ENOMEM);
	}

	raw_spin_lock_irqsave(&local_storage->lock, flags);

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

	if (gfp_flags != GFP_KERNEL) {
		/* local_storage->lock is held.  Hence, we are sure
		 * we can unlink and uncharge the old_sdata successfully
		 * later.  Hence, instead of charging the new selem now
		 * and then uncharge the old selem later (which may cause
		 * a potential but unnecessary charge failure),  avoid taking
		 * a charge at all here (the "!old_sdata" check) and the
		 * old_sdata will not be uncharged later during
		 * bpf_selem_unlink_storage_nolock().
		 */
		selem = bpf_selem_alloc(smap, owner, value, !old_sdata, gfp_flags);
		if (!selem) {
			err = -ENOMEM;
			goto unlock_err;
		}
	}

	/* First, link the new selem to the map */
	bpf_selem_link_map(smap, selem);

	/* Second, link (and publish) the new selem to local_storage */
	bpf_selem_link_storage_nolock(local_storage, selem);

	/* Third, remove old selem, SELEM(old_sdata) */
	if (old_sdata) {
		bpf_selem_unlink_map(SELEM(old_sdata));
		bpf_selem_unlink_storage_nolock(local_storage, SELEM(old_sdata),
						false, true);
	}

unlock:
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);
	return SDATA(selem);

unlock_err:
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);
	if (selem) {
		mem_uncharge(smap, owner, smap->elem_size);
		kfree(selem);
	}
	return ERR_PTR(err);
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

	if (!bpf_capable())
		return -EPERM;

	if (attr->value_size > BPF_LOCAL_STORAGE_MAX_VALUE_SIZE)
		return -E2BIG;

	return 0;
}

static struct bpf_local_storage_map *__bpf_local_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_local_storage_map *smap;
	unsigned int i;
	u32 nbuckets;

	smap = bpf_map_area_alloc(sizeof(*smap), NUMA_NO_NODE);
	if (!smap)
		return ERR_PTR(-ENOMEM);
	bpf_map_init_from_attr(&smap->map, attr);

	nbuckets = roundup_pow_of_two(num_possible_cpus());
	/* Use at least 2 buckets, select_bucket() is undefined behavior with 1 bucket */
	nbuckets = max_t(u32, 2, nbuckets);
	smap->bucket_log = ilog2(nbuckets);

	smap->buckets = kvcalloc(sizeof(*smap->buckets), nbuckets,
				 GFP_USER | __GFP_NOWARN | __GFP_ACCOUNT);
	if (!smap->buckets) {
		bpf_map_area_free(smap);
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

bool bpf_local_storage_unlink_nolock(struct bpf_local_storage *local_storage)
{
	struct bpf_local_storage_elem *selem;
	bool free_storage = false;
	struct hlist_node *n;

	/* Neither the bpf_prog nor the bpf_map's syscall
	 * could be modifying the local_storage->list now.
	 * Thus, no elem can be added to or deleted from the
	 * local_storage->list by the bpf_prog or by the bpf_map's syscall.
	 *
	 * It is racing with bpf_local_storage_map_free() alone
	 * when unlinking elem from the local_storage->list and
	 * the map's bucket->list.
	 */
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
			local_storage, selem, false, false);
	}

	return free_storage;
}

struct bpf_map *
bpf_local_storage_map_alloc(union bpf_attr *attr,
			    struct bpf_local_storage_cache *cache)
{
	struct bpf_local_storage_map *smap;

	smap = __bpf_local_storage_map_alloc(attr);
	if (IS_ERR(smap))
		return ERR_CAST(smap);

	smap->cache_idx = bpf_local_storage_cache_idx_get(cache);
	return &smap->map;
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
			if (busy_counter) {
				migrate_disable();
				this_cpu_inc(*busy_counter);
			}
			bpf_selem_unlink(selem, false);
			if (busy_counter) {
				this_cpu_dec(*busy_counter);
				migrate_enable();
			}
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
	bpf_map_area_free(smap);
}
