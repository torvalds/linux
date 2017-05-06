#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/list_bl.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mbcache2.h>

/*
 * Mbcache is a simple key-value store. Keys need not be unique, however
 * key-value pairs are expected to be unique (we use this fact in
 * mb2_cache_entry_delete_block()).
 *
 * Ext2 and ext4 use this cache for deduplication of extended attribute blocks.
 * They use hash of a block contents as a key and block number as a value.
 * That's why keys need not be unique (different xattr blocks may end up having
 * the same hash). However block number always uniquely identifies a cache
 * entry.
 *
 * We provide functions for creation and removal of entries, search by key,
 * and a special "delete entry with given key-value pair" operation. Fixed
 * size hash table is used for fast key lookups.
 */

struct mb2_cache {
	/* Hash table of entries */
	struct hlist_bl_head	*c_hash;
	/* log2 of hash table size */
	int			c_bucket_bits;
	/* Protects c_lru_list, c_entry_count */
	spinlock_t		c_lru_list_lock;
	struct list_head	c_lru_list;
	/* Number of entries in cache */
	unsigned long		c_entry_count;
	struct shrinker		c_shrink;
};

static struct kmem_cache *mb2_entry_cache;

/*
 * mb2_cache_entry_create - create entry in cache
 * @cache - cache where the entry should be created
 * @mask - gfp mask with which the entry should be allocated
 * @key - key of the entry
 * @block - block that contains data
 *
 * Creates entry in @cache with key @key and records that data is stored in
 * block @block. The function returns -EBUSY if entry with the same key
 * and for the same block already exists in cache. Otherwise 0 is returned.
 */
int mb2_cache_entry_create(struct mb2_cache *cache, gfp_t mask, u32 key,
			   sector_t block)
{
	struct mb2_cache_entry *entry, *dup;
	struct hlist_bl_node *dup_node;
	struct hlist_bl_head *head;

	entry = kmem_cache_alloc(mb2_entry_cache, mask);
	if (!entry)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->e_lru_list);
	/* One ref for hash, one ref returned */
	atomic_set(&entry->e_refcnt, 1);
	entry->e_key = key;
	entry->e_block = block;
	head = &cache->c_hash[hash_32(key, cache->c_bucket_bits)];
	entry->e_hash_list_head = head;
	hlist_bl_lock(head);
	hlist_bl_for_each_entry(dup, dup_node, head, e_hash_list) {
		if (dup->e_key == key && dup->e_block == block) {
			hlist_bl_unlock(head);
			kmem_cache_free(mb2_entry_cache, entry);
			return -EBUSY;
		}
	}
	hlist_bl_add_head(&entry->e_hash_list, head);
	hlist_bl_unlock(head);

	spin_lock(&cache->c_lru_list_lock);
	list_add_tail(&entry->e_lru_list, &cache->c_lru_list);
	/* Grab ref for LRU list */
	atomic_inc(&entry->e_refcnt);
	cache->c_entry_count++;
	spin_unlock(&cache->c_lru_list_lock);

	return 0;
}
EXPORT_SYMBOL(mb2_cache_entry_create);

void __mb2_cache_entry_free(struct mb2_cache_entry *entry)
{
	kmem_cache_free(mb2_entry_cache, entry);
}
EXPORT_SYMBOL(__mb2_cache_entry_free);

static struct mb2_cache_entry *__entry_find(struct mb2_cache *cache,
					    struct mb2_cache_entry *entry,
					    u32 key)
{
	struct mb2_cache_entry *old_entry = entry;
	struct hlist_bl_node *node;
	struct hlist_bl_head *head;

	if (entry)
		head = entry->e_hash_list_head;
	else
		head = &cache->c_hash[hash_32(key, cache->c_bucket_bits)];
	hlist_bl_lock(head);
	if (entry && !hlist_bl_unhashed(&entry->e_hash_list))
		node = entry->e_hash_list.next;
	else
		node = hlist_bl_first(head);
	while (node) {
		entry = hlist_bl_entry(node, struct mb2_cache_entry,
				       e_hash_list);
		if (entry->e_key == key) {
			atomic_inc(&entry->e_refcnt);
			goto out;
		}
		node = node->next;
	}
	entry = NULL;
out:
	hlist_bl_unlock(head);
	if (old_entry)
		mb2_cache_entry_put(cache, old_entry);

	return entry;
}

/*
 * mb2_cache_entry_find_first - find the first entry in cache with given key
 * @cache: cache where we should search
 * @key: key to look for
 *
 * Search in @cache for entry with key @key. Grabs reference to the first
 * entry found and returns the entry.
 */
struct mb2_cache_entry *mb2_cache_entry_find_first(struct mb2_cache *cache,
						   u32 key)
{
	return __entry_find(cache, NULL, key);
}
EXPORT_SYMBOL(mb2_cache_entry_find_first);

/*
 * mb2_cache_entry_find_next - find next entry in cache with the same
 * @cache: cache where we should search
 * @entry: entry to start search from
 *
 * Finds next entry in the hash chain which has the same key as @entry.
 * If @entry is unhashed (which can happen when deletion of entry races
 * with the search), finds the first entry in the hash chain. The function
 * drops reference to @entry and returns with a reference to the found entry.
 */
struct mb2_cache_entry *mb2_cache_entry_find_next(struct mb2_cache *cache,
						  struct mb2_cache_entry *entry)
{
	return __entry_find(cache, entry, entry->e_key);
}
EXPORT_SYMBOL(mb2_cache_entry_find_next);

/* mb2_cache_entry_delete_block - remove information about block from cache
 * @cache - cache we work with
 * @key - key of the entry to remove
 * @block - block containing data for @key
 *
 * Remove entry from cache @cache with key @key with data stored in @block.
 */
void mb2_cache_entry_delete_block(struct mb2_cache *cache, u32 key,
				  sector_t block)
{
	struct hlist_bl_node *node;
	struct hlist_bl_head *head;
	struct mb2_cache_entry *entry;

	head = &cache->c_hash[hash_32(key, cache->c_bucket_bits)];
	hlist_bl_lock(head);
	hlist_bl_for_each_entry(entry, node, head, e_hash_list) {
		if (entry->e_key == key && entry->e_block == block) {
			/* We keep hash list reference to keep entry alive */
			hlist_bl_del_init(&entry->e_hash_list);
			hlist_bl_unlock(head);
			spin_lock(&cache->c_lru_list_lock);
			if (!list_empty(&entry->e_lru_list)) {
				list_del_init(&entry->e_lru_list);
				cache->c_entry_count--;
				atomic_dec(&entry->e_refcnt);
			}
			spin_unlock(&cache->c_lru_list_lock);
			mb2_cache_entry_put(cache, entry);
			return;
		}
	}
	hlist_bl_unlock(head);
}
EXPORT_SYMBOL(mb2_cache_entry_delete_block);

/* mb2_cache_entry_touch - cache entry got used
 * @cache - cache the entry belongs to
 * @entry - entry that got used
 *
 * Move entry in lru list to reflect the fact that it was used.
 */
void mb2_cache_entry_touch(struct mb2_cache *cache,
			   struct mb2_cache_entry *entry)
{
	spin_lock(&cache->c_lru_list_lock);
	if (!list_empty(&entry->e_lru_list))
		list_move_tail(&cache->c_lru_list, &entry->e_lru_list);
	spin_unlock(&cache->c_lru_list_lock);
}
EXPORT_SYMBOL(mb2_cache_entry_touch);

static unsigned long mb2_cache_count(struct shrinker *shrink,
				     struct shrink_control *sc)
{
	struct mb2_cache *cache = container_of(shrink, struct mb2_cache,
					       c_shrink);

	return cache->c_entry_count;
}

/* Shrink number of entries in cache */
static unsigned long mb2_cache_scan(struct shrinker *shrink,
				    struct shrink_control *sc)
{
	int nr_to_scan = sc->nr_to_scan;
	struct mb2_cache *cache = container_of(shrink, struct mb2_cache,
					      c_shrink);
	struct mb2_cache_entry *entry;
	struct hlist_bl_head *head;
	unsigned int shrunk = 0;

	spin_lock(&cache->c_lru_list_lock);
	while (nr_to_scan-- && !list_empty(&cache->c_lru_list)) {
		entry = list_first_entry(&cache->c_lru_list,
					 struct mb2_cache_entry, e_lru_list);
		list_del_init(&entry->e_lru_list);
		cache->c_entry_count--;
		/*
		 * We keep LRU list reference so that entry doesn't go away
		 * from under us.
		 */
		spin_unlock(&cache->c_lru_list_lock);
		head = entry->e_hash_list_head;
		hlist_bl_lock(head);
		if (!hlist_bl_unhashed(&entry->e_hash_list)) {
			hlist_bl_del_init(&entry->e_hash_list);
			atomic_dec(&entry->e_refcnt);
		}
		hlist_bl_unlock(head);
		if (mb2_cache_entry_put(cache, entry))
			shrunk++;
		cond_resched();
		spin_lock(&cache->c_lru_list_lock);
	}
	spin_unlock(&cache->c_lru_list_lock);

	return shrunk;
}

/*
 * mb2_cache_create - create cache
 * @bucket_bits: log2 of the hash table size
 *
 * Create cache for keys with 2^bucket_bits hash entries.
 */
struct mb2_cache *mb2_cache_create(int bucket_bits)
{
	struct mb2_cache *cache;
	int bucket_count = 1 << bucket_bits;
	int i;

	if (!try_module_get(THIS_MODULE))
		return NULL;

	cache = kzalloc(sizeof(struct mb2_cache), GFP_KERNEL);
	if (!cache)
		goto err_out;
	cache->c_bucket_bits = bucket_bits;
	INIT_LIST_HEAD(&cache->c_lru_list);
	spin_lock_init(&cache->c_lru_list_lock);
	cache->c_hash = kmalloc(bucket_count * sizeof(struct hlist_bl_head),
				GFP_KERNEL);
	if (!cache->c_hash) {
		kfree(cache);
		goto err_out;
	}
	for (i = 0; i < bucket_count; i++)
		INIT_HLIST_BL_HEAD(&cache->c_hash[i]);

	cache->c_shrink.count_objects = mb2_cache_count;
	cache->c_shrink.scan_objects = mb2_cache_scan;
	cache->c_shrink.seeks = DEFAULT_SEEKS;
	register_shrinker(&cache->c_shrink);

	return cache;

err_out:
	module_put(THIS_MODULE);
	return NULL;
}
EXPORT_SYMBOL(mb2_cache_create);

/*
 * mb2_cache_destroy - destroy cache
 * @cache: the cache to destroy
 *
 * Free all entries in cache and cache itself. Caller must make sure nobody
 * (except shrinker) can reach @cache when calling this.
 */
void mb2_cache_destroy(struct mb2_cache *cache)
{
	struct mb2_cache_entry *entry, *next;

	unregister_shrinker(&cache->c_shrink);

	/*
	 * We don't bother with any locking. Cache must not be used at this
	 * point.
	 */
	list_for_each_entry_safe(entry, next, &cache->c_lru_list, e_lru_list) {
		if (!hlist_bl_unhashed(&entry->e_hash_list)) {
			hlist_bl_del_init(&entry->e_hash_list);
			atomic_dec(&entry->e_refcnt);
		} else
			WARN_ON(1);
		list_del(&entry->e_lru_list);
		WARN_ON(atomic_read(&entry->e_refcnt) != 1);
		mb2_cache_entry_put(cache, entry);
	}
	kfree(cache->c_hash);
	kfree(cache);
	module_put(THIS_MODULE);
}
EXPORT_SYMBOL(mb2_cache_destroy);

static int __init mb2cache_init(void)
{
	mb2_entry_cache = kmem_cache_create("mbcache",
				sizeof(struct mb2_cache_entry), 0,
				SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD, NULL);
	BUG_ON(!mb2_entry_cache);
	return 0;
}

static void __exit mb2cache_exit(void)
{
	kmem_cache_destroy(mb2_entry_cache);
}

module_init(mb2cache_init)
module_exit(mb2cache_exit)

MODULE_AUTHOR("Jan Kara <jack@suse.cz>");
MODULE_DESCRIPTION("Meta block cache (for extended attributes)");
MODULE_LICENSE("GPL");
