/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_KEY_CACHE_TYPES_H
#define _BCACHEFS_BTREE_KEY_CACHE_TYPES_H

struct btree_key_cache_freelist {
	struct bkey_cached	*objs[16];
	unsigned		nr;
};

struct btree_key_cache {
	struct mutex		lock;
	struct rhashtable	table;
	bool			table_init_done;

	struct list_head	freed_pcpu;
	size_t			nr_freed_pcpu;
	struct list_head	freed_nonpcpu;
	size_t			nr_freed_nonpcpu;

	struct shrinker		*shrink;
	unsigned		shrink_iter;
	struct btree_key_cache_freelist __percpu *pcpu_freed;

	atomic_long_t		nr_freed;
	atomic_long_t		nr_keys;
	atomic_long_t		nr_dirty;
};

struct bkey_cached_key {
	u32			btree_id;
	struct bpos		pos;
} __packed __aligned(4);

#endif /* _BCACHEFS_BTREE_KEY_CACHE_TYPES_H */
