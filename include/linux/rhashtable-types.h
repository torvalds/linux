/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Simple structures that might be needed in include
 * files.
 */

#ifndef _LINUX_RHASHTABLE_TYPES_H
#define _LINUX_RHASHTABLE_TYPES_H

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

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
	bool			automatic_shrinking;
	rht_hashfn_t		hashfn;
	rht_obj_hashfn_t	obj_hashfn;
	rht_obj_cmpfn_t		obj_cmpfn;
};

/**
 * struct rhashtable - Hash table handle
 * @tbl: Bucket table
 * @key_len: Key length for hashfn
 * @max_elems: Maximum number of elements in table
 * @p: Configuration parameters
 * @rhlist: True if this is an rhltable
 * @run_work: Deferred worker to expand/shrink asynchronously
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
	struct mutex                    mutex;
	spinlock_t			lock;
	atomic_t			nelems;
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

int rhashtable_init(struct rhashtable *ht,
		    const struct rhashtable_params *params);
int rhltable_init(struct rhltable *hlt,
		  const struct rhashtable_params *params);

#endif /* _LINUX_RHASHTABLE_TYPES_H */
