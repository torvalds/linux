/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 Facebook
 * Copyright 2020 Google LLC.
 */

#ifndef _BPF_LOCAL_STORAGE_H
#define _BPF_LOCAL_STORAGE_H

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <uapi/linux/btf.h>

#define BPF_LOCAL_STORAGE_CACHE_SIZE	16

#define bpf_rcu_lock_held()                                                    \
	(rcu_read_lock_held() || rcu_read_lock_trace_held() ||                 \
	 rcu_read_lock_bh_held())
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
	 * the number of cachelines accessed during the cache hit case.
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
	/* The data is stored in another cacheline to minimize
	 * the number of cachelines access during a cache hit.
	 */
	struct bpf_local_storage_data sdata ____cacheline_aligned;
};

struct bpf_local_storage {
	struct bpf_local_storage_data __rcu *cache[BPF_LOCAL_STORAGE_CACHE_SIZE];
	struct hlist_head list; /* List of bpf_local_storage_elem */
	void *owner;		/* The object that owns the above "list" of
				 * bpf_local_storage_elem.
				 */
	struct rcu_head rcu;
	raw_spinlock_t lock;	/* Protect adding/removing from the "list" */
};

/* U16_MAX is much more than enough for sk local storage
 * considering a tcp_sock is ~2k.
 */
#define BPF_LOCAL_STORAGE_MAX_VALUE_SIZE				       \
	min_t(u32,                                                             \
	      (KMALLOC_MAX_SIZE - MAX_BPF_STACK -                              \
	       sizeof(struct bpf_local_storage_elem)),                         \
	      (U16_MAX - sizeof(struct bpf_local_storage_elem)))

#define SELEM(_SDATA)                                                          \
	container_of((_SDATA), struct bpf_local_storage_elem, sdata)
#define SDATA(_SELEM) (&(_SELEM)->sdata)

#define BPF_LOCAL_STORAGE_CACHE_SIZE	16

struct bpf_local_storage_cache {
	spinlock_t idx_lock;
	u64 idx_usage_counts[BPF_LOCAL_STORAGE_CACHE_SIZE];
};

#define DEFINE_BPF_STORAGE_CACHE(name)				\
static struct bpf_local_storage_cache name = {			\
	.idx_lock = __SPIN_LOCK_UNLOCKED(name.idx_lock),	\
}

u16 bpf_local_storage_cache_idx_get(struct bpf_local_storage_cache *cache);
void bpf_local_storage_cache_idx_free(struct bpf_local_storage_cache *cache,
				      u16 idx);

/* Helper functions for bpf_local_storage */
int bpf_local_storage_map_alloc_check(union bpf_attr *attr);

struct bpf_local_storage_map *bpf_local_storage_map_alloc(union bpf_attr *attr);

struct bpf_local_storage_data *
bpf_local_storage_lookup(struct bpf_local_storage *local_storage,
			 struct bpf_local_storage_map *smap,
			 bool cacheit_lockit);

void bpf_local_storage_map_free(struct bpf_local_storage_map *smap,
				int __percpu *busy_counter);

int bpf_local_storage_map_check_btf(const struct bpf_map *map,
				    const struct btf *btf,
				    const struct btf_type *key_type,
				    const struct btf_type *value_type);

void bpf_selem_link_storage_nolock(struct bpf_local_storage *local_storage,
				   struct bpf_local_storage_elem *selem);

bool bpf_selem_unlink_storage_nolock(struct bpf_local_storage *local_storage,
				     struct bpf_local_storage_elem *selem,
				     bool uncharge_omem, bool use_trace_rcu);

void bpf_selem_unlink(struct bpf_local_storage_elem *selem, bool use_trace_rcu);

void bpf_selem_link_map(struct bpf_local_storage_map *smap,
			struct bpf_local_storage_elem *selem);

void bpf_selem_unlink_map(struct bpf_local_storage_elem *selem);

struct bpf_local_storage_elem *
bpf_selem_alloc(struct bpf_local_storage_map *smap, void *owner, void *value,
		bool charge_mem, gfp_t gfp_flags);

int
bpf_local_storage_alloc(void *owner,
			struct bpf_local_storage_map *smap,
			struct bpf_local_storage_elem *first_selem,
			gfp_t gfp_flags);

struct bpf_local_storage_data *
bpf_local_storage_update(void *owner, struct bpf_local_storage_map *smap,
			 void *value, u64 map_flags, gfp_t gfp_flags);

void bpf_local_storage_free_rcu(struct rcu_head *rcu);

#endif /* _BPF_LOCAL_STORAGE_H */
