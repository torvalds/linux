// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include "lru_cache.h"
#include "messages.h"

/*
 * Initialize a cache object.
 *
 * @cache:      The cache.
 * @max_size:   Maximum size (number of entries) for the cache.
 */
void btrfs_lru_cache_init(struct btrfs_lru_cache *cache, unsigned int max_size)
{
	INIT_LIST_HEAD(&cache->lru_list);
	mt_init(&cache->entries);
	cache->size = 0;
	cache->max_size = max_size;
}

/*
 * Lookup for an entry in the cache.
 *
 * @cache:      The cache.
 * @key:        The key of the entry we are looking for.
 *
 * Returns the entry associated with the key or NULL if none found.
 */
struct btrfs_lru_cache_entry *btrfs_lru_cache_lookup(struct btrfs_lru_cache *cache,
						     u64 key)
{
	struct btrfs_lru_cache_entry *entry;

	entry = mtree_load(&cache->entries, key);
	if (entry)
		list_move_tail(&entry->lru_list, &cache->lru_list);

	return entry;
}

/*
 * Store an entry in the cache.
 *
 * @cache:      The cache.
 * @entry:      The entry to store.
 *
 * Returns 0 on success and < 0 on error.
 */
int btrfs_lru_cache_store(struct btrfs_lru_cache *cache,
			  struct btrfs_lru_cache_entry *new_entry,
			  gfp_t gfp)
{
	int ret;

	if (cache->size == cache->max_size) {
		struct btrfs_lru_cache_entry *lru_entry;
		struct btrfs_lru_cache_entry *mt_entry;

		lru_entry = list_first_entry(&cache->lru_list,
					     struct btrfs_lru_cache_entry,
					     lru_list);
		mt_entry = mtree_erase(&cache->entries, lru_entry->key);
		ASSERT(mt_entry == lru_entry);
		list_del(&mt_entry->lru_list);
		kfree(mt_entry);
		cache->size--;
	}

	ret = mtree_insert(&cache->entries, new_entry->key, new_entry, gfp);
	if (ret < 0)
		return ret;

	list_add_tail(&new_entry->lru_list, &cache->lru_list);
	cache->size++;

	return 0;
}

/*
 * Empty a cache.
 *
 * @cache:     The cache to empty.
 *
 * Removes all entries from the cache.
 */
void btrfs_lru_cache_clear(struct btrfs_lru_cache *cache)
{
	struct btrfs_lru_cache_entry *entry;
	struct btrfs_lru_cache_entry *tmp;

	list_for_each_entry_safe(entry, tmp, &cache->lru_list, lru_list)
		kfree(entry);

	INIT_LIST_HEAD(&cache->lru_list);
	mtree_destroy(&cache->entries);
	cache->size = 0;
}
