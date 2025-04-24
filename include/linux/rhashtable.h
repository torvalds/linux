/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2015-2016 Herbert Xu <herbert@gondor.apana.org.au>
 * Copyright (c) 2014-2015 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * Code partially derived from nft_hash
 * Rewritten with rehash code from br_multicast plus single list
 * pointer as suggested by Josh Triplett
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_RHASHTABLE_H
#define _LINUX_RHASHTABLE_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jhash.h>
#include <linux/list_nulls.h>
#include <linux/workqueue.h>
#include <linux/rculist.h>
#include <linux/bit_spinlock.h>

#include <linux/rhashtable-types.h>
/*
 * Objects in an rhashtable have an embedded struct rhash_head
 * which is linked into as hash chain from the hash table - or one
 * of two or more hash tables when the rhashtable is being resized.
 * The end of the chain is marked with a special nulls marks which has
 * the least significant bit set but otherwise stores the address of
 * the hash bucket.  This allows us to be sure we've found the end
 * of the right list.
 * The value stored in the hash bucket has BIT(0) used as a lock bit.
 * This bit must be atomically set before any changes are made to
 * the chain.  To avoid dereferencing this pointer without clearing
 * the bit first, we use an opaque 'struct rhash_lock_head *' for the
 * pointer stored in the bucket.  This struct needs to be defined so
 * that rcu_dereference() works on it, but it has no content so a
 * cast is needed for it to be useful.  This ensures it isn't
 * used by mistake with clearing the lock bit first.
 */
struct rhash_lock_head {};

/* Maximum chain length before rehash
 *
 * The maximum (not average) chain length grows with the size of the hash
 * table, at a rate of (log N)/(log log N).
 *
 * The value of 16 is selected so that even if the hash table grew to
 * 2^32 you would not expect the maximum chain length to exceed it
 * unless we are under attack (or extremely unlucky).
 *
 * As this limit is only to detect attacks, we don't need to set it to a
 * lower value as you'd need the chain length to vastly exceed 16 to have
 * any real effect on the system.
 */
#define RHT_ELASTICITY	16u

/**
 * struct bucket_table - Table of hash buckets
 * @size: Number of hash buckets
 * @nest: Number of bits of first-level nested table.
 * @rehash: Current bucket being rehashed
 * @hash_rnd: Random seed to fold into hash
 * @walkers: List of active walkers
 * @rcu: RCU structure for freeing the table
 * @future_tbl: Table under construction during rehashing
 * @ntbl: Nested table used when out of memory.
 * @buckets: size * hash buckets
 */
struct bucket_table {
	unsigned int		size;
	unsigned int		nest;
	u32			hash_rnd;
	struct list_head	walkers;
	struct rcu_head		rcu;

	struct bucket_table __rcu *future_tbl;

	struct lockdep_map	dep_map;

	struct rhash_lock_head __rcu *buckets[] ____cacheline_aligned_in_smp;
};

/*
 * NULLS_MARKER() expects a hash value with the low
 * bits mostly likely to be significant, and it discards
 * the msb.
 * We give it an address, in which the bottom bit is
 * always 0, and the msb might be significant.
 * So we shift the address down one bit to align with
 * expectations and avoid losing a significant bit.
 *
 * We never store the NULLS_MARKER in the hash table
 * itself as we need the lsb for locking.
 * Instead we store a NULL
 */
#define	RHT_NULLS_MARKER(ptr)	\
	((void *)NULLS_MARKER(((unsigned long) (ptr)) >> 1))
#define INIT_RHT_NULLS_HEAD(ptr)	\
	((ptr) = NULL)

static inline bool rht_is_a_nulls(const struct rhash_head *ptr)
{
	return ((unsigned long) ptr & 1);
}

static inline void *rht_obj(const struct rhashtable *ht,
			    const struct rhash_head *he)
{
	return (char *)he - ht->p.head_offset;
}

static inline unsigned int rht_bucket_index(const struct bucket_table *tbl,
					    unsigned int hash)
{
	return hash & (tbl->size - 1);
}

static inline unsigned int rht_key_get_hash(struct rhashtable *ht,
	const void *key, const struct rhashtable_params params,
	unsigned int hash_rnd)
{
	unsigned int hash;

	/* params must be equal to ht->p if it isn't constant. */
	if (!__builtin_constant_p(params.key_len))
		hash = ht->p.hashfn(key, ht->key_len, hash_rnd);
	else if (params.key_len) {
		unsigned int key_len = params.key_len;

		if (params.hashfn)
			hash = params.hashfn(key, key_len, hash_rnd);
		else if (key_len & (sizeof(u32) - 1))
			hash = jhash(key, key_len, hash_rnd);
		else
			hash = jhash2(key, key_len / sizeof(u32), hash_rnd);
	} else {
		unsigned int key_len = ht->p.key_len;

		if (params.hashfn)
			hash = params.hashfn(key, key_len, hash_rnd);
		else
			hash = jhash(key, key_len, hash_rnd);
	}

	return hash;
}

static inline unsigned int rht_key_hashfn(
	struct rhashtable *ht, const struct bucket_table *tbl,
	const void *key, const struct rhashtable_params params)
{
	unsigned int hash = rht_key_get_hash(ht, key, params, tbl->hash_rnd);

	return rht_bucket_index(tbl, hash);
}

static inline unsigned int rht_head_hashfn(
	struct rhashtable *ht, const struct bucket_table *tbl,
	const struct rhash_head *he, const struct rhashtable_params params)
{
	const char *ptr = rht_obj(ht, he);

	return likely(params.obj_hashfn) ?
	       rht_bucket_index(tbl, params.obj_hashfn(ptr, params.key_len ?:
							    ht->p.key_len,
						       tbl->hash_rnd)) :
	       rht_key_hashfn(ht, tbl, ptr + params.key_offset, params);
}

/**
 * rht_grow_above_75 - returns true if nelems > 0.75 * table-size
 * @ht:		hash table
 * @tbl:	current table
 */
static inline bool rht_grow_above_75(const struct rhashtable *ht,
				     const struct bucket_table *tbl)
{
	/* Expand table when exceeding 75% load */
	return atomic_read(&ht->nelems) > (tbl->size / 4 * 3) &&
	       (!ht->p.max_size || tbl->size < ht->p.max_size);
}

/**
 * rht_shrink_below_30 - returns true if nelems < 0.3 * table-size
 * @ht:		hash table
 * @tbl:	current table
 */
static inline bool rht_shrink_below_30(const struct rhashtable *ht,
				       const struct bucket_table *tbl)
{
	/* Shrink table beneath 30% load */
	return atomic_read(&ht->nelems) < (tbl->size * 3 / 10) &&
	       tbl->size > ht->p.min_size;
}

/**
 * rht_grow_above_100 - returns true if nelems > table-size
 * @ht:		hash table
 * @tbl:	current table
 */
static inline bool rht_grow_above_100(const struct rhashtable *ht,
				      const struct bucket_table *tbl)
{
	return atomic_read(&ht->nelems) > tbl->size &&
		(!ht->p.max_size || tbl->size < ht->p.max_size);
}

/**
 * rht_grow_above_max - returns true if table is above maximum
 * @ht:		hash table
 * @tbl:	current table
 */
static inline bool rht_grow_above_max(const struct rhashtable *ht,
				      const struct bucket_table *tbl)
{
	return atomic_read(&ht->nelems) >= ht->max_elems;
}

#ifdef CONFIG_PROVE_LOCKING
int lockdep_rht_mutex_is_held(struct rhashtable *ht);
int lockdep_rht_bucket_is_held(const struct bucket_table *tbl, u32 hash);
#else
static inline int lockdep_rht_mutex_is_held(struct rhashtable *ht)
{
	return 1;
}

static inline int lockdep_rht_bucket_is_held(const struct bucket_table *tbl,
					     u32 hash)
{
	return 1;
}
#endif /* CONFIG_PROVE_LOCKING */

void *rhashtable_insert_slow(struct rhashtable *ht, const void *key,
			     struct rhash_head *obj);

void rhashtable_walk_enter(struct rhashtable *ht,
			   struct rhashtable_iter *iter);
void rhashtable_walk_exit(struct rhashtable_iter *iter);
int rhashtable_walk_start_check(struct rhashtable_iter *iter) __acquires(RCU);

static inline void rhashtable_walk_start(struct rhashtable_iter *iter)
{
	(void)rhashtable_walk_start_check(iter);
}

void *rhashtable_walk_next(struct rhashtable_iter *iter);
void *rhashtable_walk_peek(struct rhashtable_iter *iter);
void rhashtable_walk_stop(struct rhashtable_iter *iter) __releases(RCU);

void rhashtable_free_and_destroy(struct rhashtable *ht,
				 void (*free_fn)(void *ptr, void *arg),
				 void *arg);
void rhashtable_destroy(struct rhashtable *ht);

struct rhash_lock_head __rcu **rht_bucket_nested(
	const struct bucket_table *tbl, unsigned int hash);
struct rhash_lock_head __rcu **__rht_bucket_nested(
	const struct bucket_table *tbl, unsigned int hash);
struct rhash_lock_head __rcu **rht_bucket_nested_insert(
	struct rhashtable *ht, struct bucket_table *tbl, unsigned int hash);

#define rht_dereference(p, ht) \
	rcu_dereference_protected(p, lockdep_rht_mutex_is_held(ht))

#define rht_dereference_rcu(p, ht) \
	rcu_dereference_check(p, lockdep_rht_mutex_is_held(ht))

#define rht_dereference_bucket(p, tbl, hash) \
	rcu_dereference_protected(p, lockdep_rht_bucket_is_held(tbl, hash))

#define rht_dereference_bucket_rcu(p, tbl, hash) \
	rcu_dereference_check(p, lockdep_rht_bucket_is_held(tbl, hash))

#define rht_entry(tpos, pos, member) \
	({ tpos = container_of(pos, typeof(*tpos), member); 1; })

static inline struct rhash_lock_head __rcu *const *rht_bucket(
	const struct bucket_table *tbl, unsigned int hash)
{
	return unlikely(tbl->nest) ? rht_bucket_nested(tbl, hash) :
				     &tbl->buckets[hash];
}

static inline struct rhash_lock_head __rcu **rht_bucket_var(
	struct bucket_table *tbl, unsigned int hash)
{
	return unlikely(tbl->nest) ? __rht_bucket_nested(tbl, hash) :
				     &tbl->buckets[hash];
}

static inline struct rhash_lock_head __rcu **rht_bucket_insert(
	struct rhashtable *ht, struct bucket_table *tbl, unsigned int hash)
{
	return unlikely(tbl->nest) ? rht_bucket_nested_insert(ht, tbl, hash) :
				     &tbl->buckets[hash];
}

/*
 * We lock a bucket by setting BIT(0) in the pointer - this is always
 * zero in real pointers.  The NULLS mark is never stored in the bucket,
 * rather we store NULL if the bucket is empty.
 * bit_spin_locks do not handle contention well, but the whole point
 * of the hashtable design is to achieve minimum per-bucket contention.
 * A nested hash table might not have a bucket pointer.  In that case
 * we cannot get a lock.  For remove and replace the bucket cannot be
 * interesting and doesn't need locking.
 * For insert we allocate the bucket if this is the last bucket_table,
 * and then take the lock.
 * Sometimes we unlock a bucket by writing a new pointer there.  In that
 * case we don't need to unlock, but we do need to reset state such as
 * local_bh. For that we have rht_assign_unlock().  As rcu_assign_pointer()
 * provides the same release semantics that bit_spin_unlock() provides,
 * this is safe.
 * When we write to a bucket without unlocking, we use rht_assign_locked().
 */

static inline unsigned long rht_lock(struct bucket_table *tbl,
				     struct rhash_lock_head __rcu **bkt)
{
	unsigned long flags;

	local_irq_save(flags);
	bit_spin_lock(0, (unsigned long *)bkt);
	lock_map_acquire(&tbl->dep_map);
	return flags;
}

static inline unsigned long rht_lock_nested(struct bucket_table *tbl,
					struct rhash_lock_head __rcu **bucket,
					unsigned int subclass)
{
	unsigned long flags;

	local_irq_save(flags);
	bit_spin_lock(0, (unsigned long *)bucket);
	lock_acquire_exclusive(&tbl->dep_map, subclass, 0, NULL, _THIS_IP_);
	return flags;
}

static inline void rht_unlock(struct bucket_table *tbl,
			      struct rhash_lock_head __rcu **bkt,
			      unsigned long flags)
{
	lock_map_release(&tbl->dep_map);
	bit_spin_unlock(0, (unsigned long *)bkt);
	local_irq_restore(flags);
}

static inline struct rhash_head *__rht_ptr(
	struct rhash_lock_head *p, struct rhash_lock_head __rcu *const *bkt)
{
	return (struct rhash_head *)
		((unsigned long)p & ~BIT(0) ?:
		 (unsigned long)RHT_NULLS_MARKER(bkt));
}

/*
 * Where 'bkt' is a bucket and might be locked:
 *   rht_ptr_rcu() dereferences that pointer and clears the lock bit.
 *   rht_ptr() dereferences in a context where the bucket is locked.
 *   rht_ptr_exclusive() dereferences in a context where exclusive
 *            access is guaranteed, such as when destroying the table.
 */
static inline struct rhash_head *rht_ptr_rcu(
	struct rhash_lock_head __rcu *const *bkt)
{
	return __rht_ptr(rcu_dereference(*bkt), bkt);
}

static inline struct rhash_head *rht_ptr(
	struct rhash_lock_head __rcu *const *bkt,
	struct bucket_table *tbl,
	unsigned int hash)
{
	return __rht_ptr(rht_dereference_bucket(*bkt, tbl, hash), bkt);
}

static inline struct rhash_head *rht_ptr_exclusive(
	struct rhash_lock_head __rcu *const *bkt)
{
	return __rht_ptr(rcu_dereference_protected(*bkt, 1), bkt);
}

static inline void rht_assign_locked(struct rhash_lock_head __rcu **bkt,
				     struct rhash_head *obj)
{
	if (rht_is_a_nulls(obj))
		obj = NULL;
	rcu_assign_pointer(*bkt, (void *)((unsigned long)obj | BIT(0)));
}

static inline void rht_assign_unlock(struct bucket_table *tbl,
				     struct rhash_lock_head __rcu **bkt,
				     struct rhash_head *obj,
				     unsigned long flags)
{
	if (rht_is_a_nulls(obj))
		obj = NULL;
	lock_map_release(&tbl->dep_map);
	rcu_assign_pointer(*bkt, (void *)obj);
	preempt_enable();
	__release(bitlock);
	local_irq_restore(flags);
}

/**
 * rht_for_each_from - iterate over hash chain from given head
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the &struct rhash_head to start from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 */
#define rht_for_each_from(pos, head, tbl, hash) \
	for (pos = head;			\
	     !rht_is_a_nulls(pos);		\
	     pos = rht_dereference_bucket((pos)->next, tbl, hash))

/**
 * rht_for_each - iterate over hash chain
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 */
#define rht_for_each(pos, tbl, hash) \
	rht_for_each_from(pos, rht_ptr(rht_bucket(tbl, hash), tbl, hash),  \
			  tbl, hash)

/**
 * rht_for_each_entry_from - iterate over hash chain from given head
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the &struct rhash_head to start from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 */
#define rht_for_each_entry_from(tpos, pos, head, tbl, hash, member)	\
	for (pos = head;						\
	     (!rht_is_a_nulls(pos)) && rht_entry(tpos, pos, member);	\
	     pos = rht_dereference_bucket((pos)->next, tbl, hash))

/**
 * rht_for_each_entry - iterate over hash chain of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 */
#define rht_for_each_entry(tpos, pos, tbl, hash, member)		\
	rht_for_each_entry_from(tpos, pos,				\
				rht_ptr(rht_bucket(tbl, hash), tbl, hash), \
				tbl, hash, member)

/**
 * rht_for_each_entry_safe - safely iterate over hash chain of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @next:	the &struct rhash_head to use as next in loop cursor.
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 *
 * This hash chain list-traversal primitive allows for the looped code to
 * remove the loop cursor from the list.
 */
#define rht_for_each_entry_safe(tpos, pos, next, tbl, hash, member)	      \
	for (pos = rht_ptr(rht_bucket(tbl, hash), tbl, hash),		      \
	     next = !rht_is_a_nulls(pos) ?				      \
		       rht_dereference_bucket(pos->next, tbl, hash) : NULL;   \
	     (!rht_is_a_nulls(pos)) && rht_entry(tpos, pos, member);	      \
	     pos = next,						      \
	     next = !rht_is_a_nulls(pos) ?				      \
		       rht_dereference_bucket(pos->next, tbl, hash) : NULL)

/**
 * rht_for_each_rcu_from - iterate over rcu hash chain from given head
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the &struct rhash_head to start from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu mutation primitives such as rhashtable_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_rcu_from(pos, head, tbl, hash)			\
	for (({barrier(); }),						\
	     pos = head;						\
	     !rht_is_a_nulls(pos);					\
	     pos = rcu_dereference_raw(pos->next))

/**
 * rht_for_each_rcu - iterate over rcu hash chain
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu mutation primitives such as rhashtable_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_rcu(pos, tbl, hash)			\
	for (({barrier(); }),					\
	     pos = rht_ptr_rcu(rht_bucket(tbl, hash));		\
	     !rht_is_a_nulls(pos);				\
	     pos = rcu_dereference_raw(pos->next))

/**
 * rht_for_each_entry_rcu_from - iterated over rcu hash chain from given head
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the &struct rhash_head to start from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu mutation primitives such as rhashtable_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_entry_rcu_from(tpos, pos, head, tbl, hash, member) \
	for (({barrier(); }),						    \
	     pos = head;						    \
	     (!rht_is_a_nulls(pos)) && rht_entry(tpos, pos, member);	    \
	     pos = rht_dereference_bucket_rcu(pos->next, tbl, hash))

/**
 * rht_for_each_entry_rcu - iterate over rcu hash chain of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu mutation primitives such as rhashtable_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_entry_rcu(tpos, pos, tbl, hash, member)		   \
	rht_for_each_entry_rcu_from(tpos, pos,				   \
				    rht_ptr_rcu(rht_bucket(tbl, hash)),	   \
				    tbl, hash, member)

/**
 * rhl_for_each_rcu - iterate over rcu hash table list
 * @pos:	the &struct rlist_head to use as a loop cursor.
 * @list:	the head of the list
 *
 * This hash chain list-traversal primitive should be used on the
 * list returned by rhltable_lookup.
 */
#define rhl_for_each_rcu(pos, list)					\
	for (pos = list; pos; pos = rcu_dereference_raw(pos->next))

/**
 * rhl_for_each_entry_rcu - iterate over rcu hash table list of given type
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rlist_head to use as a loop cursor.
 * @list:	the head of the list
 * @member:	name of the &struct rlist_head within the hashable struct.
 *
 * This hash chain list-traversal primitive should be used on the
 * list returned by rhltable_lookup.
 */
#define rhl_for_each_entry_rcu(tpos, pos, list, member)			\
	for (pos = list; pos && rht_entry(tpos, pos, member);		\
	     pos = rcu_dereference_raw(pos->next))

static inline int rhashtable_compare(struct rhashtable_compare_arg *arg,
				     const void *obj)
{
	struct rhashtable *ht = arg->ht;
	const char *ptr = obj;

	return memcmp(ptr + ht->p.key_offset, arg->key, ht->p.key_len);
}

/* Internal function, do not use. */
static inline struct rhash_head *__rhashtable_lookup(
	struct rhashtable *ht, const void *key,
	const struct rhashtable_params params)
{
	struct rhashtable_compare_arg arg = {
		.ht = ht,
		.key = key,
	};
	struct rhash_lock_head __rcu *const *bkt;
	struct bucket_table *tbl;
	struct rhash_head *he;
	unsigned int hash;

	tbl = rht_dereference_rcu(ht->tbl, ht);
restart:
	hash = rht_key_hashfn(ht, tbl, key, params);
	bkt = rht_bucket(tbl, hash);
	do {
		rht_for_each_rcu_from(he, rht_ptr_rcu(bkt), tbl, hash) {
			if (params.obj_cmpfn ?
			    params.obj_cmpfn(&arg, rht_obj(ht, he)) :
			    rhashtable_compare(&arg, rht_obj(ht, he)))
				continue;
			return he;
		}
		/* An object might have been moved to a different hash chain,
		 * while we walk along it - better check and retry.
		 */
	} while (he != RHT_NULLS_MARKER(bkt));

	/* Ensure we see any new tables. */
	smp_rmb();

	tbl = rht_dereference_rcu(tbl->future_tbl, ht);
	if (unlikely(tbl))
		goto restart;

	return NULL;
}

/**
 * rhashtable_lookup - search hash table
 * @ht:		hash table
 * @key:	the pointer to the key
 * @params:	hash table parameters
 *
 * Computes the hash value for the key and traverses the bucket chain looking
 * for an entry with an identical key. The first matching entry is returned.
 *
 * This must only be called under the RCU read lock.
 *
 * Returns the first entry on which the compare function returned true.
 */
static inline void *rhashtable_lookup(
	struct rhashtable *ht, const void *key,
	const struct rhashtable_params params)
{
	struct rhash_head *he = __rhashtable_lookup(ht, key, params);

	return he ? rht_obj(ht, he) : NULL;
}

/**
 * rhashtable_lookup_fast - search hash table, without RCU read lock
 * @ht:		hash table
 * @key:	the pointer to the key
 * @params:	hash table parameters
 *
 * Computes the hash value for the key and traverses the bucket chain looking
 * for an entry with an identical key. The first matching entry is returned.
 *
 * Only use this function when you have other mechanisms guaranteeing
 * that the object won't go away after the RCU read lock is released.
 *
 * Returns the first entry on which the compare function returned true.
 */
static inline void *rhashtable_lookup_fast(
	struct rhashtable *ht, const void *key,
	const struct rhashtable_params params)
{
	void *obj;

	rcu_read_lock();
	obj = rhashtable_lookup(ht, key, params);
	rcu_read_unlock();

	return obj;
}

/**
 * rhltable_lookup - search hash list table
 * @hlt:	hash table
 * @key:	the pointer to the key
 * @params:	hash table parameters
 *
 * Computes the hash value for the key and traverses the bucket chain looking
 * for an entry with an identical key.  All matching entries are returned
 * in a list.
 *
 * This must only be called under the RCU read lock.
 *
 * Returns the list of entries that match the given key.
 */
static inline struct rhlist_head *rhltable_lookup(
	struct rhltable *hlt, const void *key,
	const struct rhashtable_params params)
{
	struct rhash_head *he = __rhashtable_lookup(&hlt->ht, key, params);

	return he ? container_of(he, struct rhlist_head, rhead) : NULL;
}

/* Internal function, please use rhashtable_insert_fast() instead. This
 * function returns the existing element already in hashes if there is a clash,
 * otherwise it returns an error via ERR_PTR().
 */
static inline void *__rhashtable_insert_fast(
	struct rhashtable *ht, const void *key, struct rhash_head *obj,
	const struct rhashtable_params params, bool rhlist)
{
	struct rhashtable_compare_arg arg = {
		.ht = ht,
		.key = key,
	};
	struct rhash_lock_head __rcu **bkt;
	struct rhash_head __rcu **pprev;
	struct bucket_table *tbl;
	struct rhash_head *head;
	unsigned long flags;
	unsigned int hash;
	int elasticity;
	void *data;

	rcu_read_lock();

	tbl = rht_dereference_rcu(ht->tbl, ht);
	hash = rht_head_hashfn(ht, tbl, obj, params);
	elasticity = RHT_ELASTICITY;
	bkt = rht_bucket_insert(ht, tbl, hash);
	data = ERR_PTR(-ENOMEM);
	if (!bkt)
		goto out;
	pprev = NULL;
	flags = rht_lock(tbl, bkt);

	if (unlikely(rcu_access_pointer(tbl->future_tbl))) {
slow_path:
		rht_unlock(tbl, bkt, flags);
		rcu_read_unlock();
		return rhashtable_insert_slow(ht, key, obj);
	}

	rht_for_each_from(head, rht_ptr(bkt, tbl, hash), tbl, hash) {
		struct rhlist_head *plist;
		struct rhlist_head *list;

		elasticity--;
		if (!key ||
		    (params.obj_cmpfn ?
		     params.obj_cmpfn(&arg, rht_obj(ht, head)) :
		     rhashtable_compare(&arg, rht_obj(ht, head)))) {
			pprev = &head->next;
			continue;
		}

		data = rht_obj(ht, head);

		if (!rhlist)
			goto out_unlock;


		list = container_of(obj, struct rhlist_head, rhead);
		plist = container_of(head, struct rhlist_head, rhead);

		RCU_INIT_POINTER(list->next, plist);
		head = rht_dereference_bucket(head->next, tbl, hash);
		RCU_INIT_POINTER(list->rhead.next, head);
		if (pprev) {
			rcu_assign_pointer(*pprev, obj);
			rht_unlock(tbl, bkt, flags);
		} else
			rht_assign_unlock(tbl, bkt, obj, flags);
		data = NULL;
		goto out;
	}

	if (elasticity <= 0)
		goto slow_path;

	data = ERR_PTR(-E2BIG);
	if (unlikely(rht_grow_above_max(ht, tbl)))
		goto out_unlock;

	if (unlikely(rht_grow_above_100(ht, tbl)))
		goto slow_path;

	/* Inserting at head of list makes unlocking free. */
	head = rht_ptr(bkt, tbl, hash);

	RCU_INIT_POINTER(obj->next, head);
	if (rhlist) {
		struct rhlist_head *list;

		list = container_of(obj, struct rhlist_head, rhead);
		RCU_INIT_POINTER(list->next, NULL);
	}

	atomic_inc(&ht->nelems);
	rht_assign_unlock(tbl, bkt, obj, flags);

	if (rht_grow_above_75(ht, tbl))
		schedule_work(&ht->run_work);

	data = NULL;
out:
	rcu_read_unlock();

	return data;

out_unlock:
	rht_unlock(tbl, bkt, flags);
	goto out;
}

/**
 * rhashtable_insert_fast - insert object into hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @params:	hash table parameters
 *
 * Will take the per bucket bitlock to protect against mutual mutations
 * on the same bucket. Multiple insertions may occur in parallel unless
 * they map to the same bucket.
 *
 * It is safe to call this function from atomic context.
 *
 * Will trigger an automatic deferred table resizing if residency in the
 * table grows beyond 70%.
 */
static inline int rhashtable_insert_fast(
	struct rhashtable *ht, struct rhash_head *obj,
	const struct rhashtable_params params)
{
	void *ret;

	ret = __rhashtable_insert_fast(ht, NULL, obj, params, false);
	if (IS_ERR(ret))
		return PTR_ERR(ret);

	return ret == NULL ? 0 : -EEXIST;
}

/**
 * rhltable_insert_key - insert object into hash list table
 * @hlt:	hash list table
 * @key:	the pointer to the key
 * @list:	pointer to hash list head inside object
 * @params:	hash table parameters
 *
 * Will take the per bucket bitlock to protect against mutual mutations
 * on the same bucket. Multiple insertions may occur in parallel unless
 * they map to the same bucket.
 *
 * It is safe to call this function from atomic context.
 *
 * Will trigger an automatic deferred table resizing if residency in the
 * table grows beyond 70%.
 */
static inline int rhltable_insert_key(
	struct rhltable *hlt, const void *key, struct rhlist_head *list,
	const struct rhashtable_params params)
{
	return PTR_ERR(__rhashtable_insert_fast(&hlt->ht, key, &list->rhead,
						params, true));
}

/**
 * rhltable_insert - insert object into hash list table
 * @hlt:	hash list table
 * @list:	pointer to hash list head inside object
 * @params:	hash table parameters
 *
 * Will take the per bucket bitlock to protect against mutual mutations
 * on the same bucket. Multiple insertions may occur in parallel unless
 * they map to the same bucket.
 *
 * It is safe to call this function from atomic context.
 *
 * Will trigger an automatic deferred table resizing if residency in the
 * table grows beyond 70%.
 */
static inline int rhltable_insert(
	struct rhltable *hlt, struct rhlist_head *list,
	const struct rhashtable_params params)
{
	const char *key = rht_obj(&hlt->ht, &list->rhead);

	key += params.key_offset;

	return rhltable_insert_key(hlt, key, list, params);
}

/**
 * rhashtable_lookup_insert_fast - lookup and insert object into hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @params:	hash table parameters
 *
 * This lookup function may only be used for fixed key hash table (key_len
 * parameter set). It will BUG() if used inappropriately.
 *
 * It is safe to call this function from atomic context.
 *
 * Will trigger an automatic deferred table resizing if residency in the
 * table grows beyond 70%.
 */
static inline int rhashtable_lookup_insert_fast(
	struct rhashtable *ht, struct rhash_head *obj,
	const struct rhashtable_params params)
{
	const char *key = rht_obj(ht, obj);
	void *ret;

	BUG_ON(ht->p.obj_hashfn);

	ret = __rhashtable_insert_fast(ht, key + ht->p.key_offset, obj, params,
				       false);
	if (IS_ERR(ret))
		return PTR_ERR(ret);

	return ret == NULL ? 0 : -EEXIST;
}

/**
 * rhashtable_lookup_get_insert_fast - lookup and insert object into hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @params:	hash table parameters
 *
 * Just like rhashtable_lookup_insert_fast(), but this function returns the
 * object if it exists, NULL if it did not and the insertion was successful,
 * and an ERR_PTR otherwise.
 */
static inline void *rhashtable_lookup_get_insert_fast(
	struct rhashtable *ht, struct rhash_head *obj,
	const struct rhashtable_params params)
{
	const char *key = rht_obj(ht, obj);

	BUG_ON(ht->p.obj_hashfn);

	return __rhashtable_insert_fast(ht, key + ht->p.key_offset, obj, params,
					false);
}

/**
 * rhashtable_lookup_insert_key - search and insert object to hash table
 *				  with explicit key
 * @ht:		hash table
 * @key:	key
 * @obj:	pointer to hash head inside object
 * @params:	hash table parameters
 *
 * Lookups may occur in parallel with hashtable mutations and resizing.
 *
 * Will trigger an automatic deferred table resizing if residency in the
 * table grows beyond 70%.
 *
 * Returns zero on success.
 */
static inline int rhashtable_lookup_insert_key(
	struct rhashtable *ht, const void *key, struct rhash_head *obj,
	const struct rhashtable_params params)
{
	void *ret;

	BUG_ON(!ht->p.obj_hashfn || !key);

	ret = __rhashtable_insert_fast(ht, key, obj, params, false);
	if (IS_ERR(ret))
		return PTR_ERR(ret);

	return ret == NULL ? 0 : -EEXIST;
}

/**
 * rhashtable_lookup_get_insert_key - lookup and insert object into hash table
 * @ht:		hash table
 * @key:	key
 * @obj:	pointer to hash head inside object
 * @params:	hash table parameters
 *
 * Just like rhashtable_lookup_insert_key(), but this function returns the
 * object if it exists, NULL if it does not and the insertion was successful,
 * and an ERR_PTR otherwise.
 */
static inline void *rhashtable_lookup_get_insert_key(
	struct rhashtable *ht, const void *key, struct rhash_head *obj,
	const struct rhashtable_params params)
{
	BUG_ON(!ht->p.obj_hashfn || !key);

	return __rhashtable_insert_fast(ht, key, obj, params, false);
}

/* Internal function, please use rhashtable_remove_fast() instead */
static inline int __rhashtable_remove_fast_one(
	struct rhashtable *ht, struct bucket_table *tbl,
	struct rhash_head *obj, const struct rhashtable_params params,
	bool rhlist)
{
	struct rhash_lock_head __rcu **bkt;
	struct rhash_head __rcu **pprev;
	struct rhash_head *he;
	unsigned long flags;
	unsigned int hash;
	int err = -ENOENT;

	hash = rht_head_hashfn(ht, tbl, obj, params);
	bkt = rht_bucket_var(tbl, hash);
	if (!bkt)
		return -ENOENT;
	pprev = NULL;
	flags = rht_lock(tbl, bkt);

	rht_for_each_from(he, rht_ptr(bkt, tbl, hash), tbl, hash) {
		struct rhlist_head *list;

		list = container_of(he, struct rhlist_head, rhead);

		if (he != obj) {
			struct rhlist_head __rcu **lpprev;

			pprev = &he->next;

			if (!rhlist)
				continue;

			do {
				lpprev = &list->next;
				list = rht_dereference_bucket(list->next,
							      tbl, hash);
			} while (list && obj != &list->rhead);

			if (!list)
				continue;

			list = rht_dereference_bucket(list->next, tbl, hash);
			RCU_INIT_POINTER(*lpprev, list);
			err = 0;
			break;
		}

		obj = rht_dereference_bucket(obj->next, tbl, hash);
		err = 1;

		if (rhlist) {
			list = rht_dereference_bucket(list->next, tbl, hash);
			if (list) {
				RCU_INIT_POINTER(list->rhead.next, obj);
				obj = &list->rhead;
				err = 0;
			}
		}

		if (pprev) {
			rcu_assign_pointer(*pprev, obj);
			rht_unlock(tbl, bkt, flags);
		} else {
			rht_assign_unlock(tbl, bkt, obj, flags);
		}
		goto unlocked;
	}

	rht_unlock(tbl, bkt, flags);
unlocked:
	if (err > 0) {
		atomic_dec(&ht->nelems);
		if (unlikely(ht->p.automatic_shrinking &&
			     rht_shrink_below_30(ht, tbl)))
			schedule_work(&ht->run_work);
		err = 0;
	}

	return err;
}

/* Internal function, please use rhashtable_remove_fast() instead */
static inline int __rhashtable_remove_fast(
	struct rhashtable *ht, struct rhash_head *obj,
	const struct rhashtable_params params, bool rhlist)
{
	struct bucket_table *tbl;
	int err;

	rcu_read_lock();

	tbl = rht_dereference_rcu(ht->tbl, ht);

	/* Because we have already taken (and released) the bucket
	 * lock in old_tbl, if we find that future_tbl is not yet
	 * visible then that guarantees the entry to still be in
	 * the old tbl if it exists.
	 */
	while ((err = __rhashtable_remove_fast_one(ht, tbl, obj, params,
						   rhlist)) &&
	       (tbl = rht_dereference_rcu(tbl->future_tbl, ht)))
		;

	rcu_read_unlock();

	return err;
}

/**
 * rhashtable_remove_fast - remove object from hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @params:	hash table parameters
 *
 * Since the hash chain is single linked, the removal operation needs to
 * walk the bucket chain upon removal. The removal operation is thus
 * considerable slow if the hash table is not correctly sized.
 *
 * Will automatically shrink the table if permitted when residency drops
 * below 30%.
 *
 * Returns zero on success, -ENOENT if the entry could not be found.
 */
static inline int rhashtable_remove_fast(
	struct rhashtable *ht, struct rhash_head *obj,
	const struct rhashtable_params params)
{
	return __rhashtable_remove_fast(ht, obj, params, false);
}

/**
 * rhltable_remove - remove object from hash list table
 * @hlt:	hash list table
 * @list:	pointer to hash list head inside object
 * @params:	hash table parameters
 *
 * Since the hash chain is single linked, the removal operation needs to
 * walk the bucket chain upon removal. The removal operation is thus
 * considerably slower if the hash table is not correctly sized.
 *
 * Will automatically shrink the table if permitted when residency drops
 * below 30%
 *
 * Returns zero on success, -ENOENT if the entry could not be found.
 */
static inline int rhltable_remove(
	struct rhltable *hlt, struct rhlist_head *list,
	const struct rhashtable_params params)
{
	return __rhashtable_remove_fast(&hlt->ht, &list->rhead, params, true);
}

/* Internal function, please use rhashtable_replace_fast() instead */
static inline int __rhashtable_replace_fast(
	struct rhashtable *ht, struct bucket_table *tbl,
	struct rhash_head *obj_old, struct rhash_head *obj_new,
	const struct rhashtable_params params)
{
	struct rhash_lock_head __rcu **bkt;
	struct rhash_head __rcu **pprev;
	struct rhash_head *he;
	unsigned long flags;
	unsigned int hash;
	int err = -ENOENT;

	/* Minimally, the old and new objects must have same hash
	 * (which should mean identifiers are the same).
	 */
	hash = rht_head_hashfn(ht, tbl, obj_old, params);
	if (hash != rht_head_hashfn(ht, tbl, obj_new, params))
		return -EINVAL;

	bkt = rht_bucket_var(tbl, hash);
	if (!bkt)
		return -ENOENT;

	pprev = NULL;
	flags = rht_lock(tbl, bkt);

	rht_for_each_from(he, rht_ptr(bkt, tbl, hash), tbl, hash) {
		if (he != obj_old) {
			pprev = &he->next;
			continue;
		}

		rcu_assign_pointer(obj_new->next, obj_old->next);
		if (pprev) {
			rcu_assign_pointer(*pprev, obj_new);
			rht_unlock(tbl, bkt, flags);
		} else {
			rht_assign_unlock(tbl, bkt, obj_new, flags);
		}
		err = 0;
		goto unlocked;
	}

	rht_unlock(tbl, bkt, flags);

unlocked:
	return err;
}

/**
 * rhashtable_replace_fast - replace an object in hash table
 * @ht:		hash table
 * @obj_old:	pointer to hash head inside object being replaced
 * @obj_new:	pointer to hash head inside object which is new
 * @params:	hash table parameters
 *
 * Replacing an object doesn't affect the number of elements in the hash table
 * or bucket, so we don't need to worry about shrinking or expanding the
 * table here.
 *
 * Returns zero on success, -ENOENT if the entry could not be found,
 * -EINVAL if hash is not the same for the old and new objects.
 */
static inline int rhashtable_replace_fast(
	struct rhashtable *ht, struct rhash_head *obj_old,
	struct rhash_head *obj_new,
	const struct rhashtable_params params)
{
	struct bucket_table *tbl;
	int err;

	rcu_read_lock();

	tbl = rht_dereference_rcu(ht->tbl, ht);

	/* Because we have already taken (and released) the bucket
	 * lock in old_tbl, if we find that future_tbl is not yet
	 * visible then that guarantees the entry to still be in
	 * the old tbl if it exists.
	 */
	while ((err = __rhashtable_replace_fast(ht, tbl, obj_old,
						obj_new, params)) &&
	       (tbl = rht_dereference_rcu(tbl->future_tbl, ht)))
		;

	rcu_read_unlock();

	return err;
}

/**
 * rhltable_walk_enter - Initialise an iterator
 * @hlt:	Table to walk over
 * @iter:	Hash table Iterator
 *
 * This function prepares a hash table walk.
 *
 * Note that if you restart a walk after rhashtable_walk_stop you
 * may see the same object twice.  Also, you may miss objects if
 * there are removals in between rhashtable_walk_stop and the next
 * call to rhashtable_walk_start.
 *
 * For a completely stable walk you should construct your own data
 * structure outside the hash table.
 *
 * This function may be called from any process context, including
 * non-preemptable context, but cannot be called from softirq or
 * hardirq context.
 *
 * You must call rhashtable_walk_exit after this function returns.
 */
static inline void rhltable_walk_enter(struct rhltable *hlt,
				       struct rhashtable_iter *iter)
{
	rhashtable_walk_enter(&hlt->ht, iter);
}

/**
 * rhltable_free_and_destroy - free elements and destroy hash list table
 * @hlt:	the hash list table to destroy
 * @free_fn:	callback to release resources of element
 * @arg:	pointer passed to free_fn
 *
 * See documentation for rhashtable_free_and_destroy.
 */
static inline void rhltable_free_and_destroy(struct rhltable *hlt,
					     void (*free_fn)(void *ptr,
							     void *arg),
					     void *arg)
{
	rhashtable_free_and_destroy(&hlt->ht, free_fn, arg);
}

static inline void rhltable_destroy(struct rhltable *hlt)
{
	rhltable_free_and_destroy(hlt, NULL, NULL);
}

#endif /* _LINUX_RHASHTABLE_H */
