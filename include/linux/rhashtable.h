/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * Based on the following paper by Josh Triplett, Paul E. McKenney
 * and Jonathan Walpole:
 * https://www.usenix.org/legacy/event/atc11/tech/final_files/Triplett.pdf
 *
 * Code partially derived from nft_hash
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_RHASHTABLE_H
#define _LINUX_RHASHTABLE_H

#include <linux/list_nulls.h>
#include <linux/workqueue.h>

/*
 * The end of the chain is marked with a special nulls marks which has
 * the following format:
 *
 * +-------+-----------------------------------------------------+-+
 * | Base  |                      Hash                           |1|
 * +-------+-----------------------------------------------------+-+
 *
 * Base (4 bits) : Reserved to distinguish between multiple tables.
 *                 Specified via &struct rhashtable_params.nulls_base.
 * Hash (27 bits): Full hash (unmasked) of first element added to bucket
 * 1 (1 bit)     : Nulls marker (always set)
 *
 * The remaining bits of the next pointer remain unused for now.
 */
#define RHT_BASE_BITS		4
#define RHT_HASH_BITS		27
#define RHT_BASE_SHIFT		RHT_HASH_BITS

struct rhash_head {
	struct rhash_head __rcu		*next;
};

/**
 * struct bucket_table - Table of hash buckets
 * @size: Number of hash buckets
 * @locks_mask: Mask to apply before accessing locks[]
 * @locks: Array of spinlocks protecting individual buckets
 * @buckets: size * hash buckets
 */
struct bucket_table {
	size_t				size;
	unsigned int			locks_mask;
	spinlock_t			*locks;
	struct rhash_head __rcu		*buckets[];
};

typedef u32 (*rht_hashfn_t)(const void *data, u32 len, u32 seed);
typedef u32 (*rht_obj_hashfn_t)(const void *data, u32 seed);

struct rhashtable;

/**
 * struct rhashtable_params - Hash table construction parameters
 * @nelem_hint: Hint on number of elements, should be 75% of desired size
 * @key_len: Length of key
 * @key_offset: Offset of key in struct to be hashed
 * @head_offset: Offset of rhash_head in struct to be hashed
 * @hash_rnd: Seed to use while hashing
 * @max_shift: Maximum number of shifts while expanding
 * @min_shift: Minimum number of shifts while shrinking
 * @nulls_base: Base value to generate nulls marker
 * @locks_mul: Number of bucket locks to allocate per cpu (default: 128)
 * @hashfn: Function to hash key
 * @obj_hashfn: Function to hash object
 * @grow_decision: If defined, may return true if table should expand
 * @shrink_decision: If defined, may return true if table should shrink
 */
struct rhashtable_params {
	size_t			nelem_hint;
	size_t			key_len;
	size_t			key_offset;
	size_t			head_offset;
	u32			hash_rnd;
	size_t			max_shift;
	size_t			min_shift;
	u32			nulls_base;
	size_t			locks_mul;
	rht_hashfn_t		hashfn;
	rht_obj_hashfn_t	obj_hashfn;
	bool			(*grow_decision)(const struct rhashtable *ht,
						 size_t new_size);
	bool			(*shrink_decision)(const struct rhashtable *ht,
						   size_t new_size);
};

/**
 * struct rhashtable - Hash table handle
 * @tbl: Bucket table
 * @future_tbl: Table under construction during expansion/shrinking
 * @nelems: Number of elements in table
 * @shift: Current size (1 << shift)
 * @p: Configuration parameters
 * @run_work: Deferred worker to expand/shrink asynchronously
 * @mutex: Mutex to protect current/future table swapping
 * @being_destroyed: True if table is set up for destruction
 */
struct rhashtable {
	struct bucket_table __rcu	*tbl;
	struct bucket_table __rcu       *future_tbl;
	atomic_t			nelems;
	size_t				shift;
	struct rhashtable_params	p;
	struct delayed_work             run_work;
	struct mutex                    mutex;
	bool                            being_destroyed;
};

static inline unsigned long rht_marker(const struct rhashtable *ht, u32 hash)
{
	return NULLS_MARKER(ht->p.nulls_base + hash);
}

#define INIT_RHT_NULLS_HEAD(ptr, ht, hash) \
	((ptr) = (typeof(ptr)) rht_marker(ht, hash))

static inline bool rht_is_a_nulls(const struct rhash_head *ptr)
{
	return ((unsigned long) ptr & 1);
}

static inline unsigned long rht_get_nulls_value(const struct rhash_head *ptr)
{
	return ((unsigned long) ptr) >> 1;
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

int rhashtable_init(struct rhashtable *ht, struct rhashtable_params *params);

void rhashtable_insert(struct rhashtable *ht, struct rhash_head *node);
bool rhashtable_remove(struct rhashtable *ht, struct rhash_head *node);

bool rht_grow_above_75(const struct rhashtable *ht, size_t new_size);
bool rht_shrink_below_30(const struct rhashtable *ht, size_t new_size);

int rhashtable_expand(struct rhashtable *ht);
int rhashtable_shrink(struct rhashtable *ht);

void *rhashtable_lookup(struct rhashtable *ht, const void *key);
void *rhashtable_lookup_compare(struct rhashtable *ht, const void *key,
				bool (*compare)(void *, void *), void *arg);

void rhashtable_destroy(struct rhashtable *ht);

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

/**
 * rht_for_each_continue - continue iterating over hash chain
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the previous &struct rhash_head to continue from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 */
#define rht_for_each_continue(pos, head, tbl, hash) \
	for (pos = rht_dereference_bucket(head, tbl, hash); \
	     !rht_is_a_nulls(pos); \
	     pos = rht_dereference_bucket((pos)->next, tbl, hash))

/**
 * rht_for_each - iterate over hash chain
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 */
#define rht_for_each(pos, tbl, hash) \
	rht_for_each_continue(pos, (tbl)->buckets[hash], tbl, hash)

/**
 * rht_for_each_entry_continue - continue iterating over hash chain
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the previous &struct rhash_head to continue from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 */
#define rht_for_each_entry_continue(tpos, pos, head, tbl, hash, member)	\
	for (pos = rht_dereference_bucket(head, tbl, hash);		\
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
	rht_for_each_entry_continue(tpos, pos, (tbl)->buckets[hash],	\
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
#define rht_for_each_entry_safe(tpos, pos, next, tbl, hash, member)	    \
	for (pos = rht_dereference_bucket((tbl)->buckets[hash], tbl, hash), \
	     next = !rht_is_a_nulls(pos) ?				    \
		       rht_dereference_bucket(pos->next, tbl, hash) : NULL; \
	     (!rht_is_a_nulls(pos)) && rht_entry(tpos, pos, member);	    \
	     pos = next)

/**
 * rht_for_each_rcu_continue - continue iterating over rcu hash chain
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the previous &struct rhash_head to continue from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu mutation primitives such as rhashtable_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_rcu_continue(pos, head, tbl, hash)			\
	for (({barrier(); }),						\
	     pos = rht_dereference_bucket_rcu(head, tbl, hash);		\
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
#define rht_for_each_rcu(pos, tbl, hash)				\
	rht_for_each_rcu_continue(pos, (tbl)->buckets[hash], tbl, hash)

/**
 * rht_for_each_entry_rcu_continue - continue iterating over rcu hash chain
 * @tpos:	the type * to use as a loop cursor.
 * @pos:	the &struct rhash_head to use as a loop cursor.
 * @head:	the previous &struct rhash_head to continue from
 * @tbl:	the &struct bucket_table
 * @hash:	the hash value / bucket index
 * @member:	name of the &struct rhash_head within the hashable struct.
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu mutation primitives such as rhashtable_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_entry_rcu_continue(tpos, pos, head, tbl, hash, member) \
	for (({barrier(); }),						    \
	     pos = rht_dereference_bucket_rcu(head, tbl, hash);		    \
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
#define rht_for_each_entry_rcu(tpos, pos, tbl, hash, member)		\
	rht_for_each_entry_rcu_continue(tpos, pos, (tbl)->buckets[hash],\
					tbl, hash, member)

#endif /* _LINUX_RHASHTABLE_H */
