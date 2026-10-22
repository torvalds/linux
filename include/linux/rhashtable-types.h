/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Simple structures that might be needed in include
 * files.
 */

#ifndef _LINUX_RHASHTABLE_TYPES_H
#define _LINUX_RHASHTABLE_TYPES_H

#include <linux/alloc_tag.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/irq_work_types.h>
#include <linux/mutex.h>
#include <linux/workqueue_types.h>

struct rhash_head {
	struct rhash_head __rcu		*next;
};

struct rhlist_head {
	struct rhash_head		rhead;
	struct rhlist_head __rcu	*next;
};

struct bucket_table;

/**
 * struct rhashtable_compare_arg - Key for the function rhashtable_compare
 * @ht: Hash table
 * @key: Key to compare against
 */
struct rhashtable_compare_arg {
	struct rhashtable *ht;
	const void *key;
};

typedef u32 (*rht_hashfn_t)(const void *data, u32 len, u32 seed);
typedef u32 (*rht_obj_hashfn_t)(const void *data, u32 len, u32 seed);
typedef int (*rht_obj_cmpfn_t)(struct rhashtable_compare_arg *arg,
			       const void *obj);

/**
 * struct rhashtable_params - Hash table construction parameters
 * @nelem_hint: Hint on number of elements, should be 75% of desired size
 * @key_len: Length of key
 * @key_offset: Offset of key in struct to be hashed
 * @head_offset: Offset of rhash_head in struct to be hashed
 * @max_size: Maximum size while expanding
 * @min_size: Minimum size while shrinking
 * @insecure_elasticity: Set to true to disable chain length checks
 * @automatic_shrinking: Enable automatic shrinking of tables
 * @hashfn: Hash function (default: jhash2 if !(key_len % 4), or jhash)
 * @obj_hashfn: Function to hash object
 * @obj_cmpfn: Function to compare key with object
 */
struct rhashtable_params {
	u16			nelem_hint;
	u16			key_len;
	u16			key_offset;
	u16			head_offset;
	unsigned int		max_size;
	u16			min_size;
	bool			insecure_elasticity;
	bool			automatic_shrinking;
	rht_hashfn_t		hashfn;
	rht_obj_hashfn_t	obj_hashfn;
	rht_obj_cmpfn_t		obj_cmpfn;
};

struct rhashtable_lockdep_keys {
	struct lock_class_key lock_key;
	struct lock_class_key mutex_key;
	struct lock_class_key bucket_key;
};

/**
 * struct rhashtable - Hash table handle
 * @tbl: Bucket table
 * @key_len: Key length for hashfn
 * @max_elems: Maximum number of elements in table
 * @p: Configuration parameters
 * @rhlist: True if this is an rhltable
 * @run_work: Deferred worker to expand/shrink asynchronously
 * @run_irq_work: Bounces the @run_work kick through hard IRQ context.
 * @mutex: Mutex to protect current/future table swapping
 * @lock: Spin lock to protect walker list
 * @nelems: Number of elements in table
 */
struct rhashtable {
	struct bucket_table __rcu	*tbl;
	unsigned int			key_len;
	unsigned int			max_elems;
	struct rhashtable_params	p;
	bool				rhlist;
	struct work_struct		run_work;
	struct irq_work			run_irq_work;
	struct mutex                    mutex;
	spinlock_t			lock;
	atomic_t			nelems;
#ifdef CONFIG_MEM_ALLOC_PROFILING
	struct alloc_tag		*alloc_tag;
#endif
#ifdef CONFIG_LOCKDEP
	struct lock_class_key		*lockdep_key;
#endif
};

/**
 * struct rhltable - Hash table with duplicate objects in a list
 * @ht: Underlying rhtable
 */
struct rhltable {
	struct rhashtable ht;
};

/**
 * struct rhashtable_walker - Hash table walker
 * @list: List entry on list of walkers
 * @tbl: The table that we were walking over
 */
struct rhashtable_walker {
	struct list_head list;
	struct bucket_table *tbl;
};

/**
 * struct rhashtable_iter - Hash table iterator
 * @ht: Table to iterate through
 * @p: Current pointer
 * @list: Current hash list pointer
 * @walker: Associated rhashtable walker
 * @slot: Current slot
 * @skip: Number of entries to skip in slot
 */
struct rhashtable_iter {
	struct rhashtable *ht;
	struct rhash_head *p;
	struct rhlist_head *list;
	struct rhashtable_walker walker;
	unsigned int slot;
	unsigned int skip;
	bool end_of_table;
};

int __rhashtable_init_noprof(struct rhashtable *ht,
		    const struct rhashtable_params *params,
		    struct rhashtable_lockdep_keys *keys);
#define rhashtable_init_noprof(ht, params)				\
({									\
	static struct rhashtable_lockdep_keys __keys;			\
									\
	__rhashtable_init_noprof(ht, params, &__keys);			\
})

/**
 * rhashtable_init - initialize a new hash table
 * @ht:		hash table to be initialized
 * @params:	configuration parameters
 *
 * Initializes a new hash table based on the provided configuration
 * parameters. A table can be configured either with a variable or
 * fixed length key:
 *
 * Configuration Example 1: Fixed length keys
 * struct test_obj {
 *	int			key;
 *	void *			my_member;
 *	struct rhash_head	node;
 * };
 *
 * struct rhashtable_params params = {
 *	.head_offset = offsetof(struct test_obj, node),
 *	.key_offset = offsetof(struct test_obj, key),
 *	.key_len = sizeof(int),
 *	.hashfn = jhash,
 * };
 *
 * Configuration Example 2: Variable length keys
 * struct test_obj {
 *	[...]
 *	struct rhash_head	node;
 * };
 *
 * u32 my_hash_fn(const void *data, u32 len, u32 seed)
 * {
 *	struct test_obj *obj = data;
 *
 *	return [... hash ...];
 * }
 *
 * struct rhashtable_params params = {
 *	.head_offset = offsetof(struct test_obj, node),
 *	.hashfn = jhash,
 *	.obj_hashfn = my_hash_fn,
 * };
 */
#define rhashtable_init(...)	alloc_hooks(rhashtable_init_noprof(__VA_ARGS__))

int __rhltable_init_noprof(struct rhltable *hlt,
		  const struct rhashtable_params *params,
		  struct rhashtable_lockdep_keys *keys);
#define rhltable_init_noprof(hlt, params)				\
({									\
	static struct rhashtable_lockdep_keys __keys;			\
									\
	__rhltable_init_noprof(hlt, params, &__keys);			\
})

/**
 * rhltable_init - initialize a new hash list table
 * @hlt:	hash list table to be initialized
 * @params:	configuration parameters
 *
 * Initializes a new hash list table.
 *
 * See documentation for rhashtable_init.
 */
#define rhltable_init(...)	alloc_hooks(rhltable_init_noprof(__VA_ARGS__))

#endif /* _LINUX_RHASHTABLE_TYPES_H */
