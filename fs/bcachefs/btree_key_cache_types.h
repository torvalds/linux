/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_KEY_CACHE_TYPES_H
#define _BCACHEFS_BTREE_KEY_CACHE_TYPES_H

#include "rcu_pending.h"

struct btree_key_cache {
	struct rhashtable	table;
	bool			table_init_done;

	struct shrinker		*shrink;
	unsigned		shrink_iter;

	/* 0: non pcpu reader locks, 1: pcpu reader locks */
	struct rcu_pending	pending[2];
	size_t __percpu		*nr_pending;

	atomic_long_t		nr_keys;
	atomic_long_t		nr_dirty;

	/* shrinker stats */
	unsigned long		requested_to_free;
	unsigned long		freed;
	unsigned long		skipped_dirty;
	unsigned long		skipped_accessed;
	unsigned long		skipped_lock_fail;
};

struct bkey_cached_key {
	u32			btree_id;
	struct bpos		pos;
} __packed __aligned(4);

#endif /* _BCACHEFS_BTREE_KEY_CACHE_TYPES_H */
