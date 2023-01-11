/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_LRU_CACHE_H
#define BTRFS_LRU_CACHE_H

#include <linux/maple_tree.h>
#include <linux/list.h>

/*
 * A cache entry. This is meant to be embedded in a structure of a user of
 * this module. Similar to how struct list_head and struct rb_node are used.
 *
 * Note: it should be embedded as the first element in a struct (offset 0), and
 * this module assumes it was allocated with kmalloc(), so it calls kfree() when
 * it needs to free an entry.
 */
struct btrfs_lru_cache_entry {
	struct list_head lru_list;
	u64 key;
};

struct btrfs_lru_cache {
	struct list_head lru_list;
	struct maple_tree entries;
	/* Number of entries stored in the cache. */
	unsigned int size;
	/* Maximum number of entries the cache can have. */
	unsigned int max_size;
};

static inline unsigned int btrfs_lru_cache_size(const struct btrfs_lru_cache *cache)
{
	return cache->size;
}

void btrfs_lru_cache_init(struct btrfs_lru_cache *cache, unsigned int max_size);
struct btrfs_lru_cache_entry *btrfs_lru_cache_lookup(struct btrfs_lru_cache *cache,
						     u64 key);
int btrfs_lru_cache_store(struct btrfs_lru_cache *cache,
			  struct btrfs_lru_cache_entry *new_entry,
			  gfp_t gfp);
void btrfs_lru_cache_clear(struct btrfs_lru_cache *cache);

#endif
