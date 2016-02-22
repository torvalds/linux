#ifndef _LINUX_MB2CACHE_H
#define _LINUX_MB2CACHE_H

#include <linux/hash.h>
#include <linux/list_bl.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/fs.h>

struct mb2_cache;

struct mb2_cache_entry {
	/* LRU list - protected by cache->c_lru_list_lock */
	struct list_head	e_lru_list;
	/* Hash table list - protected by bitlock in e_hash_list_head */
	struct hlist_bl_node	e_hash_list;
	atomic_t		e_refcnt;
	/* Key in hash - stable during lifetime of the entry */
	u32			e_key;
	/* Block number of hashed block - stable during lifetime of the entry */
	sector_t		e_block;
	/* Head of hash list (for list bit lock) - stable */
	struct hlist_bl_head	*e_hash_list_head;
};

struct mb2_cache *mb2_cache_create(int bucket_bits);
void mb2_cache_destroy(struct mb2_cache *cache);

int mb2_cache_entry_create(struct mb2_cache *cache, gfp_t mask, u32 key,
			   sector_t block);
void __mb2_cache_entry_free(struct mb2_cache_entry *entry);
static inline int mb2_cache_entry_put(struct mb2_cache *cache,
				      struct mb2_cache_entry *entry)
{
	if (!atomic_dec_and_test(&entry->e_refcnt))
		return 0;
	__mb2_cache_entry_free(entry);
	return 1;
}

void mb2_cache_entry_delete_block(struct mb2_cache *cache, u32 key,
				  sector_t block);
struct mb2_cache_entry *mb2_cache_entry_find_first(struct mb2_cache *cache,
						   u32 key);
struct mb2_cache_entry *mb2_cache_entry_find_next(struct mb2_cache *cache,
						  struct mb2_cache_entry *entry);
void mb2_cache_entry_touch(struct mb2_cache *cache,
			   struct mb2_cache_entry *entry);

#endif	/* _LINUX_MB2CACHE_H */
