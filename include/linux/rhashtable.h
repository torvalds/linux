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

#include <linux/rculist.h>

struct rhash_head {
	struct rhash_head		*next;
};

#define INIT_HASH_HEAD(ptr) ((ptr)->next = NULL)

struct bucket_table {
	size_t				size;
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
 * @hashfn: Function to hash key
 * @obj_hashfn: Function to hash object
 * @grow_decision: If defined, may return true if table should expand
 * @shrink_decision: If defined, may return true if table should shrink
 * @mutex_is_held: Must return true if protecting mutex is held
 */
struct rhashtable_params {
	size_t			nelem_hint;
	size_t			key_len;
	size_t			key_offset;
	size_t			head_offset;
	u32			hash_rnd;
	size_t			max_shift;
	rht_hashfn_t		hashfn;
	rht_obj_hashfn_t	obj_hashfn;
	bool			(*grow_decision)(const struct rhashtable *ht,
						 size_t new_size);
	bool			(*shrink_decision)(const struct rhashtable *ht,
						   size_t new_size);
	int			(*mutex_is_held)(void);
};

/**
 * struct rhashtable - Hash table handle
 * @tbl: Bucket table
 * @nelems: Number of elements in table
 * @shift: Current size (1 << shift)
 * @p: Configuration parameters
 */
struct rhashtable {
	struct bucket_table __rcu	*tbl;
	size_t				nelems;
	size_t				shift;
	struct rhashtable_params	p;
};

#ifdef CONFIG_PROVE_LOCKING
int lockdep_rht_mutex_is_held(const struct rhashtable *ht);
#else
static inline int lockdep_rht_mutex_is_held(const struct rhashtable *ht)
{
	return 1;
}
#endif /* CONFIG_PROVE_LOCKING */

int rhashtable_init(struct rhashtable *ht, struct rhashtable_params *params);

u32 rhashtable_hashfn(const struct rhashtable *ht, const void *key, u32 len);
u32 rhashtable_obj_hashfn(const struct rhashtable *ht, void *ptr);

void rhashtable_insert(struct rhashtable *ht, struct rhash_head *node, gfp_t);
bool rhashtable_remove(struct rhashtable *ht, struct rhash_head *node, gfp_t);
void rhashtable_remove_pprev(struct rhashtable *ht, struct rhash_head *obj,
			     struct rhash_head **pprev, gfp_t flags);

bool rht_grow_above_75(const struct rhashtable *ht, size_t new_size);
bool rht_shrink_below_30(const struct rhashtable *ht, size_t new_size);

int rhashtable_expand(struct rhashtable *ht, gfp_t flags);
int rhashtable_shrink(struct rhashtable *ht, gfp_t flags);

void *rhashtable_lookup(const struct rhashtable *ht, const void *key);
void *rhashtable_lookup_compare(const struct rhashtable *ht, u32 hash,
				bool (*compare)(void *, void *), void *arg);

void rhashtable_destroy(const struct rhashtable *ht);

#define rht_dereference(p, ht) \
	rcu_dereference_protected(p, lockdep_rht_mutex_is_held(ht))

#define rht_dereference_rcu(p, ht) \
	rcu_dereference_check(p, lockdep_rht_mutex_is_held(ht))

/* Internal, use rht_obj() instead */
#define rht_entry(ptr, type, member) container_of(ptr, type, member)
#define rht_entry_safe(ptr, type, member) \
({ \
	typeof(ptr) __ptr = (ptr); \
	   __ptr ? rht_entry(__ptr, type, member) : NULL; \
})
#define rht_entry_safe_rcu(ptr, type, member) \
({ \
	typeof(*ptr) __rcu *__ptr = (typeof(*ptr) __rcu __force *)ptr; \
	__ptr ? container_of((typeof(ptr))rcu_dereference_raw(__ptr), type, member) : NULL; \
})

#define rht_next_entry_safe(pos, ht, member) \
({ \
	pos ? rht_entry_safe(rht_dereference((pos)->member.next, ht), \
			     typeof(*(pos)), member) : NULL; \
})

/**
 * rht_for_each - iterate over hash chain
 * @pos:	&struct rhash_head to use as a loop cursor.
 * @head:	head of the hash chain (struct rhash_head *)
 * @ht:		pointer to your struct rhashtable
 */
#define rht_for_each(pos, head, ht) \
	for (pos = rht_dereference(head, ht); \
	     pos; \
	     pos = rht_dereference((pos)->next, ht))

/**
 * rht_for_each_entry - iterate over hash chain of given type
 * @pos:	type * to use as a loop cursor.
 * @head:	head of the hash chain (struct rhash_head *)
 * @ht:		pointer to your struct rhashtable
 * @member:	name of the rhash_head within the hashable struct.
 */
#define rht_for_each_entry(pos, head, ht, member) \
	for (pos = rht_entry_safe(rht_dereference(head, ht), \
				   typeof(*(pos)), member); \
	     pos; \
	     pos = rht_next_entry_safe(pos, ht, member))

/**
 * rht_for_each_entry_safe - safely iterate over hash chain of given type
 * @pos:	type * to use as a loop cursor.
 * @n:		type * to use for temporary next object storage
 * @head:	head of the hash chain (struct rhash_head *)
 * @ht:		pointer to your struct rhashtable
 * @member:	name of the rhash_head within the hashable struct.
 *
 * This hash chain list-traversal primitive allows for the looped code to
 * remove the loop cursor from the list.
 */
#define rht_for_each_entry_safe(pos, n, head, ht, member)		\
	for (pos = rht_entry_safe(rht_dereference(head, ht), \
				  typeof(*(pos)), member), \
	     n = rht_next_entry_safe(pos, ht, member); \
	     pos; \
	     pos = n, \
	     n = rht_next_entry_safe(pos, ht, member))

/**
 * rht_for_each_rcu - iterate over rcu hash chain
 * @pos:	&struct rhash_head to use as a loop cursor.
 * @head:	head of the hash chain (struct rhash_head *)
 * @ht:		pointer to your struct rhashtable
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu fkht mutation primitives such as rht_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_rcu(pos, head, ht) \
	for (pos = rht_dereference_rcu(head, ht); \
	     pos; \
	     pos = rht_dereference_rcu((pos)->next, ht))

/**
 * rht_for_each_entry_rcu - iterate over rcu hash chain of given type
 * @pos:	type * to use as a loop cursor.
 * @head:	head of the hash chain (struct rhash_head *)
 * @member:	name of the rhash_head within the hashable struct.
 *
 * This hash chain list-traversal primitive may safely run concurrently with
 * the _rcu fkht mutation primitives such as rht_insert() as long as the
 * traversal is guarded by rcu_read_lock().
 */
#define rht_for_each_entry_rcu(pos, head, member) \
	for (pos = rht_entry_safe_rcu(head, typeof(*(pos)), member); \
	     pos; \
	     pos = rht_entry_safe_rcu((pos)->member.next, \
				      typeof(*(pos)), member))

#endif /* _LINUX_RHASHTABLE_H */
