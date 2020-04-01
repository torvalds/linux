// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <net/bpf_sk_storage.h>
#include <net/sock.h>
#include <uapi/linux/btf.h>

static atomic_t cache_idx;

#define SK_STORAGE_CREATE_FLAG_MASK					\
	(BPF_F_NO_PREALLOC | BPF_F_CLONE)

struct bucket {
	struct hlist_head list;
	raw_spinlock_t lock;
};

/* Thp map is not the primary owner of a bpf_sk_storage_elem.
 * Instead, the sk->sk_bpf_storage is.
 *
 * The map (bpf_sk_storage_map) is for two purposes
 * 1. Define the size of the "sk local storage".  It is
 *    the map's value_size.
 *
 * 2. Maintain a list to keep track of all elems such
 *    that they can be cleaned up during the map destruction.
 *
 * When a bpf local storage is being looked up for a
 * particular sk,  the "bpf_map" pointer is actually used
 * as the "key" to search in the list of elem in
 * sk->sk_bpf_storage.
 *
 * Hence, consider sk->sk_bpf_storage is the mini-map
 * with the "bpf_map" pointer as the searching key.
 */
struct bpf_sk_storage_map {
	struct bpf_map map;
	/* Lookup elem does not require accessing the map.
	 *
	 * Updating/Deleting requires a bucket lock to
	 * link/unlink the elem from the map.  Having
	 * multiple buckets to improve contention.
	 */
	struct bucket *buckets;
	u32 bucket_log;
	u16 elem_size;
	u16 cache_idx;
};

struct bpf_sk_storage_data {
	/* smap is used as the searching key when looking up
	 * from sk->sk_bpf_storage.
	 *
	 * Put it in the same cacheline as the data to minimize
	 * the number of cachelines access during the cache hit case.
	 */
	struct bpf_sk_storage_map __rcu *smap;
	u8 data[0] __aligned(8);
};

/* Linked to bpf_sk_storage and bpf_sk_storage_map */
struct bpf_sk_storage_elem {
	struct hlist_node map_node;	/* Linked to bpf_sk_storage_map */
	struct hlist_node snode;	/* Linked to bpf_sk_storage */
	struct bpf_sk_storage __rcu *sk_storage;
	struct rcu_head rcu;
	/* 8 bytes hole */
	/* The data is stored in aother cacheline to minimize
	 * the number of cachelines access during a cache hit.
	 */
	struct bpf_sk_storage_data sdata ____cacheline_aligned;
};

#define SELEM(_SDATA) container_of((_SDATA), struct bpf_sk_storage_elem, sdata)
#define SDATA(_SELEM) (&(_SELEM)->sdata)
#define BPF_SK_STORAGE_CACHE_SIZE	16

struct bpf_sk_storage {
	struct bpf_sk_storage_data __rcu *cache[BPF_SK_STORAGE_CACHE_SIZE];
	struct hlist_head list;	/* List of bpf_sk_storage_elem */
	struct sock *sk;	/* The sk that owns the the above "list" of
				 * bpf_sk_storage_elem.
				 */
	struct rcu_head rcu;
	raw_spinlock_t lock;	/* Protect adding/removing from the "list" */
};

static struct bucket *select_bucket(struct bpf_sk_storage_map *smap,
				    struct bpf_sk_storage_elem *selem)
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

static bool selem_linked_to_sk(const struct bpf_sk_storage_elem *selem)
{
	return !hlist_unhashed(&selem->snode);
}

static bool selem_linked_to_map(const struct bpf_sk_storage_elem *selem)
{
	return !hlist_unhashed(&selem->map_node);
}

static struct bpf_sk_storage_elem *selem_alloc(struct bpf_sk_storage_map *smap,
					       struct sock *sk, void *value,
					       bool charge_omem)
{
	struct bpf_sk_storage_elem *selem;

	if (charge_omem && omem_charge(sk, smap->elem_size))
		return NULL;

	selem = kzalloc(smap->elem_size, GFP_ATOMIC | __GFP_NOWARN);
	if (selem) {
		if (value)
			memcpy(SDATA(selem)->data, value, smap->map.value_size);
		return selem;
	}

	if (charge_omem)
		atomic_sub(smap->elem_size, &sk->sk_omem_alloc);

	return NULL;
}

/* sk_storage->lock must be held and selem->sk_storage == sk_storage.
 * The caller must ensure selem->smap is still valid to be
 * dereferenced for its smap->elem_size and smap->cache_idx.
 */
static bool __selem_unlink_sk(struct bpf_sk_storage *sk_storage,
			      struct bpf_sk_storage_elem *selem,
			      bool uncharge_omem)
{
	struct bpf_sk_storage_map *smap;
	bool free_sk_storage;
	struct sock *sk;

	smap = rcu_dereference(SDATA(selem)->smap);
	sk = sk_storage->sk;

	/* All uncharging on sk->sk_omem_alloc must be done first.
	 * sk may be freed once the last selem is unlinked from sk_storage.
	 */
	if (uncharge_omem)
		atomic_sub(smap->elem_size, &sk->sk_omem_alloc);

	free_sk_storage = hlist_is_singular_node(&selem->snode,
						 &sk_storage->list);
	if (free_sk_storage) {
		atomic_sub(sizeof(struct bpf_sk_storage), &sk->sk_omem_alloc);
		sk_storage->sk = NULL;
		/* After this RCU_INIT, sk may be freed and cannot be used */
		RCU_INIT_POINTER(sk->sk_bpf_storage, NULL);

		/* sk_storage is not freed now.  sk_storage->lock is
		 * still held and raw_spin_unlock_bh(&sk_storage->lock)
		 * will be done by the caller.
		 *
		 * Although the unlock will be done under
		 * rcu_read_lock(),  it is more intutivie to
		 * read if kfree_rcu(sk_storage, rcu) is done
		 * after the raw_spin_unlock_bh(&sk_storage->lock).
		 *
		 * Hence, a "bool free_sk_storage" is returned
		 * to the caller which then calls the kfree_rcu()
		 * after unlock.
		 */
	}
	hlist_del_init_rcu(&selem->snode);
	if (rcu_access_pointer(sk_storage->cache[smap->cache_idx]) ==
	    SDATA(selem))
		RCU_INIT_POINTER(sk_storage->cache[smap->cache_idx], NULL);

	kfree_rcu(selem, rcu);

	return free_sk_storage;
}

static void selem_unlink_sk(struct bpf_sk_storage_elem *selem)
{
	struct bpf_sk_storage *sk_storage;
	bool free_sk_storage = false;

	if (unlikely(!selem_linked_to_sk(selem)))
		/* selem has already been unlinked from sk */
		return;

	sk_storage = rcu_dereference(selem->sk_storage);
	raw_spin_lock_bh(&sk_storage->lock);
	if (likely(selem_linked_to_sk(selem)))
		free_sk_storage = __selem_unlink_sk(sk_storage, selem, true);
	raw_spin_unlock_bh(&sk_storage->lock);

	if (free_sk_storage)
		kfree_rcu(sk_storage, rcu);
}

static void __selem_link_sk(struct bpf_sk_storage *sk_storage,
			    struct bpf_sk_storage_elem *selem)
{
	RCU_INIT_POINTER(selem->sk_storage, sk_storage);
	hlist_add_head(&selem->snode, &sk_storage->list);
}

static void selem_unlink_map(struct bpf_sk_storage_elem *selem)
{
	struct bpf_sk_storage_map *smap;
	struct bucket *b;

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

static void selem_link_map(struct bpf_sk_storage_map *smap,
			   struct bpf_sk_storage_elem *selem)
{
	struct bucket *b = select_bucket(smap, selem);

	raw_spin_lock_bh(&b->lock);
	RCU_INIT_POINTER(SDATA(selem)->smap, smap);
	hlist_add_head_rcu(&selem->map_node, &b->list);
	raw_spin_unlock_bh(&b->lock);
}

static void selem_unlink(struct bpf_sk_storage_elem *selem)
{
	/* Always unlink from map before unlinking from sk_storage
	 * because selem will be freed after successfully unlinked from
	 * the sk_storage.
	 */
	selem_unlink_map(selem);
	selem_unlink_sk(selem);
}

static struct bpf_sk_storage_data *
__sk_storage_lookup(struct bpf_sk_storage *sk_storage,
		    struct bpf_sk_storage_map *smap,
		    bool cacheit_lockit)
{
	struct bpf_sk_storage_data *sdata;
	struct bpf_sk_storage_elem *selem;

	/* Fast path (cache hit) */
	sdata = rcu_dereference(sk_storage->cache[smap->cache_idx]);
	if (sdata && rcu_access_pointer(sdata->smap) == smap)
		return sdata;

	/* Slow path (cache miss) */
	hlist_for_each_entry_rcu(selem, &sk_storage->list, snode)
		if (rcu_access_pointer(SDATA(selem)->smap) == smap)
			break;

	if (!selem)
		return NULL;

	sdata = SDATA(selem);
	if (cacheit_lockit) {
		/* spinlock is needed to avoid racing with the
		 * parallel delete.  Otherwise, publishing an already
		 * deleted sdata to the cache will become a use-after-free
		 * problem in the next __sk_storage_lookup().
		 */
		raw_spin_lock_bh(&sk_storage->lock);
		if (selem_linked_to_sk(selem))
			rcu_assign_pointer(sk_storage->cache[smap->cache_idx],
					   sdata);
		raw_spin_unlock_bh(&sk_storage->lock);
	}

	return sdata;
}

static struct bpf_sk_storage_data *
sk_storage_lookup(struct sock *sk, struct bpf_map *map, bool cacheit_lockit)
{
	struct bpf_sk_storage *sk_storage;
	struct bpf_sk_storage_map *smap;

	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage)
		return NULL;

	smap = (struct bpf_sk_storage_map *)map;
	return __sk_storage_lookup(sk_storage, smap, cacheit_lockit);
}

static int check_flags(const struct bpf_sk_storage_data *old_sdata,
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

static int sk_storage_alloc(struct sock *sk,
			    struct bpf_sk_storage_map *smap,
			    struct bpf_sk_storage_elem *first_selem)
{
	struct bpf_sk_storage *prev_sk_storage, *sk_storage;
	int err;

	err = omem_charge(sk, sizeof(*sk_storage));
	if (err)
		return err;

	sk_storage = kzalloc(sizeof(*sk_storage), GFP_ATOMIC | __GFP_NOWARN);
	if (!sk_storage) {
		err = -ENOMEM;
		goto uncharge;
	}
	INIT_HLIST_HEAD(&sk_storage->list);
	raw_spin_lock_init(&sk_storage->lock);
	sk_storage->sk = sk;

	__selem_link_sk(sk_storage, first_selem);
	selem_link_map(smap, first_selem);
	/* Publish sk_storage to sk.  sk->sk_lock cannot be acquired.
	 * Hence, atomic ops is used to set sk->sk_bpf_storage
	 * from NULL to the newly allocated sk_storage ptr.
	 *
	 * From now on, the sk->sk_bpf_storage pointer is protected
	 * by the sk_storage->lock.  Hence,  when freeing
	 * the sk->sk_bpf_storage, the sk_storage->lock must
	 * be held before setting sk->sk_bpf_storage to NULL.
	 */
	prev_sk_storage = cmpxchg((struct bpf_sk_storage **)&sk->sk_bpf_storage,
				  NULL, sk_storage);
	if (unlikely(prev_sk_storage)) {
		selem_unlink_map(first_selem);
		err = -EAGAIN;
		goto uncharge;

		/* Note that even first_selem was linked to smap's
		 * bucket->list, first_selem can be freed immediately
		 * (instead of kfree_rcu) because
		 * bpf_sk_storage_map_free() does a
		 * synchronize_rcu() before walking the bucket->list.
		 * Hence, no one is accessing selem from the
		 * bucket->list under rcu_read_lock().
		 */
	}

	return 0;

uncharge:
	kfree(sk_storage);
	atomic_sub(sizeof(*sk_storage), &sk->sk_omem_alloc);
	return err;
}

/* sk cannot be going away because it is linking new elem
 * to sk->sk_bpf_storage. (i.e. sk->sk_refcnt cannot be 0).
 * Otherwise, it will become a leak (and other memory issues
 * during map destruction).
 */
static struct bpf_sk_storage_data *sk_storage_update(struct sock *sk,
						     struct bpf_map *map,
						     void *value,
						     u64 map_flags)
{
	struct bpf_sk_storage_data *old_sdata = NULL;
	struct bpf_sk_storage_elem *selem;
	struct bpf_sk_storage *sk_storage;
	struct bpf_sk_storage_map *smap;
	int err;

	/* BPF_EXIST and BPF_NOEXIST cannot be both set */
	if (unlikely((map_flags & ~BPF_F_LOCK) > BPF_EXIST) ||
	    /* BPF_F_LOCK can only be used in a value with spin_lock */
	    unlikely((map_flags & BPF_F_LOCK) && !map_value_has_spin_lock(map)))
		return ERR_PTR(-EINVAL);

	smap = (struct bpf_sk_storage_map *)map;
	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage || hlist_empty(&sk_storage->list)) {
		/* Very first elem for this sk */
		err = check_flags(NULL, map_flags);
		if (err)
			return ERR_PTR(err);

		selem = selem_alloc(smap, sk, value, true);
		if (!selem)
			return ERR_PTR(-ENOMEM);

		err = sk_storage_alloc(sk, smap, selem);
		if (err) {
			kfree(selem);
			atomic_sub(smap->elem_size, &sk->sk_omem_alloc);
			return ERR_PTR(err);
		}

		return SDATA(selem);
	}

	if ((map_flags & BPF_F_LOCK) && !(map_flags & BPF_NOEXIST)) {
		/* Hoping to find an old_sdata to do inline update
		 * such that it can avoid taking the sk_storage->lock
		 * and changing the lists.
		 */
		old_sdata = __sk_storage_lookup(sk_storage, smap, false);
		err = check_flags(old_sdata, map_flags);
		if (err)
			return ERR_PTR(err);
		if (old_sdata && selem_linked_to_sk(SELEM(old_sdata))) {
			copy_map_value_locked(map, old_sdata->data,
					      value, false);
			return old_sdata;
		}
	}

	raw_spin_lock_bh(&sk_storage->lock);

	/* Recheck sk_storage->list under sk_storage->lock */
	if (unlikely(hlist_empty(&sk_storage->list))) {
		/* A parallel del is happening and sk_storage is going
		 * away.  It has just been checked before, so very
		 * unlikely.  Return instead of retry to keep things
		 * simple.
		 */
		err = -EAGAIN;
		goto unlock_err;
	}

	old_sdata = __sk_storage_lookup(sk_storage, smap, false);
	err = check_flags(old_sdata, map_flags);
	if (err)
		goto unlock_err;

	if (old_sdata && (map_flags & BPF_F_LOCK)) {
		copy_map_value_locked(map, old_sdata->data, value, false);
		selem = SELEM(old_sdata);
		goto unlock;
	}

	/* sk_storage->lock is held.  Hence, we are sure
	 * we can unlink and uncharge the old_sdata successfully
	 * later.  Hence, instead of charging the new selem now
	 * and then uncharge the old selem later (which may cause
	 * a potential but unnecessary charge failure),  avoid taking
	 * a charge at all here (the "!old_sdata" check) and the
	 * old_sdata will not be uncharged later during __selem_unlink_sk().
	 */
	selem = selem_alloc(smap, sk, value, !old_sdata);
	if (!selem) {
		err = -ENOMEM;
		goto unlock_err;
	}

	/* First, link the new selem to the map */
	selem_link_map(smap, selem);

	/* Second, link (and publish) the new selem to sk_storage */
	__selem_link_sk(sk_storage, selem);

	/* Third, remove old selem, SELEM(old_sdata) */
	if (old_sdata) {
		selem_unlink_map(SELEM(old_sdata));
		__selem_unlink_sk(sk_storage, SELEM(old_sdata), false);
	}

unlock:
	raw_spin_unlock_bh(&sk_storage->lock);
	return SDATA(selem);

unlock_err:
	raw_spin_unlock_bh(&sk_storage->lock);
	return ERR_PTR(err);
}

static int sk_storage_delete(struct sock *sk, struct bpf_map *map)
{
	struct bpf_sk_storage_data *sdata;

	sdata = sk_storage_lookup(sk, map, false);
	if (!sdata)
		return -ENOENT;

	selem_unlink(SELEM(sdata));

	return 0;
}

/* Called by __sk_destruct() & bpf_sk_storage_clone() */
void bpf_sk_storage_free(struct sock *sk)
{
	struct bpf_sk_storage_elem *selem;
	struct bpf_sk_storage *sk_storage;
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
	 * It is racing with bpf_sk_storage_map_free() alone
	 * when unlinking elem from the sk_storage->list and
	 * the map's bucket->list.
	 */
	raw_spin_lock_bh(&sk_storage->lock);
	hlist_for_each_entry_safe(selem, n, &sk_storage->list, snode) {
		/* Always unlink from map before unlinking from
		 * sk_storage.
		 */
		selem_unlink_map(selem);
		free_sk_storage = __selem_unlink_sk(sk_storage, selem, true);
	}
	raw_spin_unlock_bh(&sk_storage->lock);
	rcu_read_unlock();

	if (free_sk_storage)
		kfree_rcu(sk_storage, rcu);
}

static void bpf_sk_storage_map_free(struct bpf_map *map)
{
	struct bpf_sk_storage_elem *selem;
	struct bpf_sk_storage_map *smap;
	struct bucket *b;
	unsigned int i;

	smap = (struct bpf_sk_storage_map *)map;

	/* Note that this map might be concurrently cloned from
	 * bpf_sk_storage_clone. Wait for any existing bpf_sk_storage_clone
	 * RCU read section to finish before proceeding. New RCU
	 * read sections should be prevented via bpf_map_inc_not_zero.
	 */
	synchronize_rcu();

	/* bpf prog and the userspace can no longer access this map
	 * now.  No new selem (of this map) can be added
	 * to the sk->sk_bpf_storage or to the map bucket's list.
	 *
	 * The elem of this map can be cleaned up here
	 * or
	 * by bpf_sk_storage_free() during __sk_destruct().
	 */
	for (i = 0; i < (1U << smap->bucket_log); i++) {
		b = &smap->buckets[i];

		rcu_read_lock();
		/* No one is adding to b->list now */
		while ((selem = hlist_entry_safe(rcu_dereference_raw(hlist_first_rcu(&b->list)),
						 struct bpf_sk_storage_elem,
						 map_node))) {
			selem_unlink(selem);
			cond_resched_rcu();
		}
		rcu_read_unlock();
	}

	/* bpf_sk_storage_free() may still need to access the map.
	 * e.g. bpf_sk_storage_free() has unlinked selem from the map
	 * which then made the above while((selem = ...)) loop
	 * exited immediately.
	 *
	 * However, the bpf_sk_storage_free() still needs to access
	 * the smap->elem_size to do the uncharging in
	 * __selem_unlink_sk().
	 *
	 * Hence, wait another rcu grace period for the
	 * bpf_sk_storage_free() to finish.
	 */
	synchronize_rcu();

	kvfree(smap->buckets);
	kfree(map);
}

static int bpf_sk_storage_map_alloc_check(union bpf_attr *attr)
{
	if (attr->map_flags & ~SK_STORAGE_CREATE_FLAG_MASK ||
	    !(attr->map_flags & BPF_F_NO_PREALLOC) ||
	    attr->max_entries ||
	    attr->key_size != sizeof(int) || !attr->value_size ||
	    /* Enforce BTF for userspace sk dumping */
	    !attr->btf_key_type_id || !attr->btf_value_type_id)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (attr->value_size >= KMALLOC_MAX_SIZE -
	    MAX_BPF_STACK - sizeof(struct bpf_sk_storage_elem) ||
	    /* U16_MAX is much more than enough for sk local storage
	     * considering a tcp_sock is ~2k.
	     */
	    attr->value_size > U16_MAX - sizeof(struct bpf_sk_storage_elem))
		return -E2BIG;

	return 0;
}

static struct bpf_map *bpf_sk_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_sk_storage_map *smap;
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

	smap->elem_size = sizeof(struct bpf_sk_storage_elem) + attr->value_size;
	smap->cache_idx = (unsigned int)atomic_inc_return(&cache_idx) %
		BPF_SK_STORAGE_CACHE_SIZE;

	return &smap->map;
}

static int notsupp_get_next_key(struct bpf_map *map, void *key,
				void *next_key)
{
	return -ENOTSUPP;
}

static int bpf_sk_storage_map_check_btf(const struct bpf_map *map,
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
	struct bpf_sk_storage_data *sdata;
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
	struct bpf_sk_storage_data *sdata;
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		sdata = sk_storage_update(sock->sk, map, value, map_flags);
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

static struct bpf_sk_storage_elem *
bpf_sk_storage_clone_elem(struct sock *newsk,
			  struct bpf_sk_storage_map *smap,
			  struct bpf_sk_storage_elem *selem)
{
	struct bpf_sk_storage_elem *copy_selem;

	copy_selem = selem_alloc(smap, newsk, NULL, true);
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
	struct bpf_sk_storage *new_sk_storage = NULL;
	struct bpf_sk_storage *sk_storage;
	struct bpf_sk_storage_elem *selem;
	int ret = 0;

	RCU_INIT_POINTER(newsk->sk_bpf_storage, NULL);

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);

	if (!sk_storage || hlist_empty(&sk_storage->list))
		goto out;

	hlist_for_each_entry_rcu(selem, &sk_storage->list, snode) {
		struct bpf_sk_storage_elem *copy_selem;
		struct bpf_sk_storage_map *smap;
		struct bpf_map *map;

		smap = rcu_dereference(SDATA(selem)->smap);
		if (!(smap->map.map_flags & BPF_F_CLONE))
			continue;

		/* Note that for lockless listeners adding new element
		 * here can race with cleanup in bpf_sk_storage_map_free.
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
			selem_link_map(smap, copy_selem);
			__selem_link_sk(new_sk_storage, copy_selem);
		} else {
			ret = sk_storage_alloc(newsk, smap, copy_selem);
			if (ret) {
				kfree(copy_selem);
				atomic_sub(smap->elem_size,
					   &newsk->sk_omem_alloc);
				bpf_map_put(map);
				goto out;
			}

			new_sk_storage = rcu_dereference(copy_selem->sk_storage);
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
	struct bpf_sk_storage_data *sdata;

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
		sdata = sk_storage_update(sk, map, value, BPF_NOEXIST);
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

const struct bpf_map_ops sk_storage_map_ops = {
	.map_alloc_check = bpf_sk_storage_map_alloc_check,
	.map_alloc = bpf_sk_storage_map_alloc,
	.map_free = bpf_sk_storage_map_free,
	.map_get_next_key = notsupp_get_next_key,
	.map_lookup_elem = bpf_fd_sk_storage_lookup_elem,
	.map_update_elem = bpf_fd_sk_storage_update_elem,
	.map_delete_elem = bpf_fd_sk_storage_delete_elem,
	.map_check_btf = bpf_sk_storage_map_check_btf,
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

const struct bpf_func_proto bpf_sk_storage_delete_proto = {
	.func		= bpf_sk_storage_delete,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_SOCKET,
};
