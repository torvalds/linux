/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_LRU_CACHE_H
#define BTRFS_LRU_CACHE_H

#include <linux/types.h>
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
	/*
	 * Optional generation associated to a key. Use 0 if not needed/used.
	 * Entries with the same key and different generations are stored in a
	 * linked list, so use this only for cases where there's a small number
	 * of different generations.
	 */
	u64 gen;
	/*
	 * The maple tree uses unsigned long type for the keys, which is 32 bits
	 * on 32 bits systems, and 64 bits on 64 bits systems. So if we want to
	 * use something like inode numbers as keys, which are always a u64, we
	 * have to deal with this in a special way - we store the key in the
	 * entry itself, as a u64, and the values inserted into the maple tree
	 * are linked lists of entries - so in case we are on a 64 bits system,
	 * that list always has a single entry, while on 32 bits systems it
	 * may have more than one, with each entry having the same value for
	 * their lower 32 bits of the u64 key.
	 */
	struct list_head list;
};

struct btrfs_lru_cache {
	struct list_head lru_list;
	struct maple_tree entries;
	/* Number of entries stored in the cache. */
	unsigned int size;
	/* Maximum number of entries the cache can have. */
	unsigned int max_size;
};

#define btrfs_lru_cache_for_each_entry_safe(cache, entry, tmp)		\
	list_for_each_entry_safe_reverse((entry), (tmp), &(cache)->lru_list, lru_list)

static inline struct btrfs_lru_cache_entry *btrfs_lru_cache_lru_entry(
					      struct btrfs_lru_cache *cache)
{
	return list_first_entry_or_null(&cache->lru_list,
					struct btrfs_lru_cache_entry, lru_list);
}

void btrfs_lru_cache_init(struct btrfs_lru_cache *cache, unsigned int max_size);
struct btrfs_lru_cache_entry *btrfs_lru_cache_lookup(struct btrfs_lru_cache *cache,
						     u64 key, u64 gen);
int btrfs_lru_cache_store(struct btrfs_lru_cache *cache,
			  struct btrfs_lru_cache_entry *new_entry,
			  gfp_t gfp);
void btrfs_lru_cache_remove(struct btrfs_lru_cache *cache,
			    struct btrfs_lru_cache_entry *entry);
void btrfs_lru_cache_clear(struct btrfs_lru_cache *cache);

#endif
