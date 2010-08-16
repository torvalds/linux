/*
 * linux/fs/mbcache.c
 * (C) 2001-2002 Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

/*
 * Filesystem Meta Information Block Cache (mbcache)
 *
 * The mbcache caches blocks of block devices that need to be located
 * by their device/block number, as well as by other criteria (such
 * as the block's contents).
 *
 * There can only be one cache entry in a cache per device and block number.
 * Additional indexes need not be unique in this sense. The number of
 * additional indexes (=other criteria) can be hardwired at compile time
 * or specified at cache create time.
 *
 * Each cache entry is of fixed size. An entry may be `valid' or `invalid'
 * in the cache. A valid entry is in the main hash tables of the cache,
 * and may also be in the lru list. An invalid entry is not in any hashes
 * or lists.
 *
 * A valid cache entry is only in the lru list if no handles refer to it.
 * Invalid cache entries will be freed when the last handle to the cache
 * entry is released. Entries that cannot be freed immediately are put
 * back on the lru list.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/hash.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/mbcache.h>


#ifdef MB_CACHE_DEBUG
# define mb_debug(f...) do { \
		printk(KERN_DEBUG f); \
		printk("\n"); \
	} while (0)
#define mb_assert(c) do { if (!(c)) \
		printk(KERN_ERR "assertion " #c " failed\n"); \
	} while(0)
#else
# define mb_debug(f...) do { } while(0)
# define mb_assert(c) do { } while(0)
#endif
#define mb_error(f...) do { \
		printk(KERN_ERR f); \
		printk("\n"); \
	} while(0)

#define MB_CACHE_WRITER ((unsigned short)~0U >> 1)

static DECLARE_WAIT_QUEUE_HEAD(mb_cache_queue);
		
MODULE_AUTHOR("Andreas Gruenbacher <a.gruenbacher@computer.org>");
MODULE_DESCRIPTION("Meta block cache (for extended attributes)");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(mb_cache_create);
EXPORT_SYMBOL(mb_cache_shrink);
EXPORT_SYMBOL(mb_cache_destroy);
EXPORT_SYMBOL(mb_cache_entry_alloc);
EXPORT_SYMBOL(mb_cache_entry_insert);
EXPORT_SYMBOL(mb_cache_entry_release);
EXPORT_SYMBOL(mb_cache_entry_free);
EXPORT_SYMBOL(mb_cache_entry_get);
#if !defined(MB_CACHE_INDEXES_COUNT) || (MB_CACHE_INDEXES_COUNT > 0)
EXPORT_SYMBOL(mb_cache_entry_find_first);
EXPORT_SYMBOL(mb_cache_entry_find_next);
#endif

struct mb_cache {
	struct list_head		c_cache_list;
	const char			*c_name;
	struct mb_cache_op		c_op;
	atomic_t			c_entry_count;
	int				c_bucket_bits;
#ifndef MB_CACHE_INDEXES_COUNT
	int				c_indexes_count;
#endif
	struct kmem_cache			*c_entry_cache;
	struct list_head		*c_block_hash;
	struct list_head		*c_indexes_hash[0];
};


/*
 * Global data: list of all mbcache's, lru list, and a spinlock for
 * accessing cache data structures on SMP machines. The lru list is
 * global across all mbcaches.
 */

static LIST_HEAD(mb_cache_list);
static LIST_HEAD(mb_cache_lru_list);
static DEFINE_SPINLOCK(mb_cache_spinlock);

static inline int
mb_cache_indexes(struct mb_cache *cache)
{
#ifdef MB_CACHE_INDEXES_COUNT
	return MB_CACHE_INDEXES_COUNT;
#else
	return cache->c_indexes_count;
#endif
}

/*
 * What the mbcache registers as to get shrunk dynamically.
 */

static int mb_cache_shrink_fn(struct shrinker *shrink, int nr_to_scan, gfp_t gfp_mask);

static struct shrinker mb_cache_shrinker = {
	.shrink = mb_cache_shrink_fn,
	.seeks = DEFAULT_SEEKS,
};

static inline int
__mb_cache_entry_is_hashed(struct mb_cache_entry *ce)
{
	return !list_empty(&ce->e_block_list);
}


static void
__mb_cache_entry_unhash(struct mb_cache_entry *ce)
{
	int n;

	if (__mb_cache_entry_is_hashed(ce)) {
		list_del_init(&ce->e_block_list);
		for (n=0; n<mb_cache_indexes(ce->e_cache); n++)
			list_del(&ce->e_indexes[n].o_list);
	}
}


static void
__mb_cache_entry_forget(struct mb_cache_entry *ce, gfp_t gfp_mask)
{
	struct mb_cache *cache = ce->e_cache;

	mb_assert(!(ce->e_used || ce->e_queued));
	if (cache->c_op.free && cache->c_op.free(ce, gfp_mask)) {
		/* free failed -- put back on the lru list
		   for freeing later. */
		spin_lock(&mb_cache_spinlock);
		list_add(&ce->e_lru_list, &mb_cache_lru_list);
		spin_unlock(&mb_cache_spinlock);
	} else {
		kmem_cache_free(cache->c_entry_cache, ce);
		atomic_dec(&cache->c_entry_count);
	}
}


static void
__mb_cache_entry_release_unlock(struct mb_cache_entry *ce)
	__releases(mb_cache_spinlock)
{
	/* Wake up all processes queuing for this cache entry. */
	if (ce->e_queued)
		wake_up_all(&mb_cache_queue);
	if (ce->e_used >= MB_CACHE_WRITER)
		ce->e_used -= MB_CACHE_WRITER;
	ce->e_used--;
	if (!(ce->e_used || ce->e_queued)) {
		if (!__mb_cache_entry_is_hashed(ce))
			goto forget;
		mb_assert(list_empty(&ce->e_lru_list));
		list_add_tail(&ce->e_lru_list, &mb_cache_lru_list);
	}
	spin_unlock(&mb_cache_spinlock);
	return;
forget:
	spin_unlock(&mb_cache_spinlock);
	__mb_cache_entry_forget(ce, GFP_KERNEL);
}


/*
 * mb_cache_shrink_fn()  memory pressure callback
 *
 * This function is called by the kernel memory management when memory
 * gets low.
 *
 * @shrink: (ignored)
 * @nr_to_scan: Number of objects to scan
 * @gfp_mask: (ignored)
 *
 * Returns the number of objects which are present in the cache.
 */
static int
mb_cache_shrink_fn(struct shrinker *shrink, int nr_to_scan, gfp_t gfp_mask)
{
	LIST_HEAD(free_list);
	struct list_head *l, *ltmp;
	int count = 0;

	spin_lock(&mb_cache_spinlock);
	list_for_each(l, &mb_cache_list) {
		struct mb_cache *cache =
			list_entry(l, struct mb_cache, c_cache_list);
		mb_debug("cache %s (%d)", cache->c_name,
			  atomic_read(&cache->c_entry_count));
		count += atomic_read(&cache->c_entry_count);
	}
	mb_debug("trying to free %d entries", nr_to_scan);
	if (nr_to_scan == 0) {
		spin_unlock(&mb_cache_spinlock);
		goto out;
	}
	while (nr_to_scan-- && !list_empty(&mb_cache_lru_list)) {
		struct mb_cache_entry *ce =
			list_entry(mb_cache_lru_list.next,
				   struct mb_cache_entry, e_lru_list);
		list_move_tail(&ce->e_lru_list, &free_list);
		__mb_cache_entry_unhash(ce);
	}
	spin_unlock(&mb_cache_spinlock);
	list_for_each_safe(l, ltmp, &free_list) {
		__mb_cache_entry_forget(list_entry(l, struct mb_cache_entry,
						   e_lru_list), gfp_mask);
	}
out:
	return (count / 100) * sysctl_vfs_cache_pressure;
}


/*
 * mb_cache_create()  create a new cache
 *
 * All entries in one cache are equal size. Cache entries may be from
 * multiple devices. If this is the first mbcache created, registers
 * the cache with kernel memory management. Returns NULL if no more
 * memory was available.
 *
 * @name: name of the cache (informal)
 * @cache_op: contains the callback called when freeing a cache entry
 * @entry_size: The size of a cache entry, including
 *              struct mb_cache_entry
 * @indexes_count: number of additional indexes in the cache. Must equal
 *                 MB_CACHE_INDEXES_COUNT if the number of indexes is
 *                 hardwired.
 * @bucket_bits: log2(number of hash buckets)
 */
struct mb_cache *
mb_cache_create(const char *name, struct mb_cache_op *cache_op,
		size_t entry_size, int indexes_count, int bucket_bits)
{
	int m=0, n, bucket_count = 1 << bucket_bits;
	struct mb_cache *cache = NULL;

	if(entry_size < sizeof(struct mb_cache_entry) +
	   indexes_count * sizeof(((struct mb_cache_entry *) 0)->e_indexes[0]))
		return NULL;

	cache = kmalloc(sizeof(struct mb_cache) +
	                indexes_count * sizeof(struct list_head), GFP_KERNEL);
	if (!cache)
		goto fail;
	cache->c_name = name;
	cache->c_op.free = NULL;
	if (cache_op)
		cache->c_op.free = cache_op->free;
	atomic_set(&cache->c_entry_count, 0);
	cache->c_bucket_bits = bucket_bits;
#ifdef MB_CACHE_INDEXES_COUNT
	mb_assert(indexes_count == MB_CACHE_INDEXES_COUNT);
#else
	cache->c_indexes_count = indexes_count;
#endif
	cache->c_block_hash = kmalloc(bucket_count * sizeof(struct list_head),
	                              GFP_KERNEL);
	if (!cache->c_block_hash)
		goto fail;
	for (n=0; n<bucket_count; n++)
		INIT_LIST_HEAD(&cache->c_block_hash[n]);
	for (m=0; m<indexes_count; m++) {
		cache->c_indexes_hash[m] = kmalloc(bucket_count *
		                                 sizeof(struct list_head),
		                                 GFP_KERNEL);
		if (!cache->c_indexes_hash[m])
			goto fail;
		for (n=0; n<bucket_count; n++)
			INIT_LIST_HEAD(&cache->c_indexes_hash[m][n]);
	}
	cache->c_entry_cache = kmem_cache_create(name, entry_size, 0,
		SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD, NULL);
	if (!cache->c_entry_cache)
		goto fail;

	spin_lock(&mb_cache_spinlock);
	list_add(&cache->c_cache_list, &mb_cache_list);
	spin_unlock(&mb_cache_spinlock);
	return cache;

fail:
	if (cache) {
		while (--m >= 0)
			kfree(cache->c_indexes_hash[m]);
		kfree(cache->c_block_hash);
		kfree(cache);
	}
	return NULL;
}


/*
 * mb_cache_shrink()
 *
 * Removes all cache entries of a device from the cache. All cache entries
 * currently in use cannot be freed, and thus remain in the cache. All others
 * are freed.
 *
 * @bdev: which device's cache entries to shrink
 */
void
mb_cache_shrink(struct block_device *bdev)
{
	LIST_HEAD(free_list);
	struct list_head *l, *ltmp;

	spin_lock(&mb_cache_spinlock);
	list_for_each_safe(l, ltmp, &mb_cache_lru_list) {
		struct mb_cache_entry *ce =
			list_entry(l, struct mb_cache_entry, e_lru_list);
		if (ce->e_bdev == bdev) {
			list_move_tail(&ce->e_lru_list, &free_list);
			__mb_cache_entry_unhash(ce);
		}
	}
	spin_unlock(&mb_cache_spinlock);
	list_for_each_safe(l, ltmp, &free_list) {
		__mb_cache_entry_forget(list_entry(l, struct mb_cache_entry,
						   e_lru_list), GFP_KERNEL);
	}
}


/*
 * mb_cache_destroy()
 *
 * Shrinks the cache to its minimum possible size (hopefully 0 entries),
 * and then destroys it. If this was the last mbcache, un-registers the
 * mbcache from kernel memory management.
 */
void
mb_cache_destroy(struct mb_cache *cache)
{
	LIST_HEAD(free_list);
	struct list_head *l, *ltmp;
	int n;

	spin_lock(&mb_cache_spinlock);
	list_for_each_safe(l, ltmp, &mb_cache_lru_list) {
		struct mb_cache_entry *ce =
			list_entry(l, struct mb_cache_entry, e_lru_list);
		if (ce->e_cache == cache) {
			list_move_tail(&ce->e_lru_list, &free_list);
			__mb_cache_entry_unhash(ce);
		}
	}
	list_del(&cache->c_cache_list);
	spin_unlock(&mb_cache_spinlock);

	list_for_each_safe(l, ltmp, &free_list) {
		__mb_cache_entry_forget(list_entry(l, struct mb_cache_entry,
						   e_lru_list), GFP_KERNEL);
	}

	if (atomic_read(&cache->c_entry_count) > 0) {
		mb_error("cache %s: %d orphaned entries",
			  cache->c_name,
			  atomic_read(&cache->c_entry_count));
	}

	kmem_cache_destroy(cache->c_entry_cache);

	for (n=0; n < mb_cache_indexes(cache); n++)
		kfree(cache->c_indexes_hash[n]);
	kfree(cache->c_block_hash);
	kfree(cache);
}


/*
 * mb_cache_entry_alloc()
 *
 * Allocates a new cache entry. The new entry will not be valid initially,
 * and thus cannot be looked up yet. It should be filled with data, and
 * then inserted into the cache using mb_cache_entry_insert(). Returns NULL
 * if no more memory was available.
 */
struct mb_cache_entry *
mb_cache_entry_alloc(struct mb_cache *cache, gfp_t gfp_flags)
{
	struct mb_cache_entry *ce;

	ce = kmem_cache_alloc(cache->c_entry_cache, gfp_flags);
	if (ce) {
		atomic_inc(&cache->c_entry_count);
		INIT_LIST_HEAD(&ce->e_lru_list);
		INIT_LIST_HEAD(&ce->e_block_list);
		ce->e_cache = cache;
		ce->e_used = 1 + MB_CACHE_WRITER;
		ce->e_queued = 0;
	}
	return ce;
}


/*
 * mb_cache_entry_insert()
 *
 * Inserts an entry that was allocated using mb_cache_entry_alloc() into
 * the cache. After this, the cache entry can be looked up, but is not yet
 * in the lru list as the caller still holds a handle to it. Returns 0 on
 * success, or -EBUSY if a cache entry for that device + inode exists
 * already (this may happen after a failed lookup, but when another process
 * has inserted the same cache entry in the meantime).
 *
 * @bdev: device the cache entry belongs to
 * @block: block number
 * @keys: array of additional keys. There must be indexes_count entries
 *        in the array (as specified when creating the cache).
 */
int
mb_cache_entry_insert(struct mb_cache_entry *ce, struct block_device *bdev,
		      sector_t block, unsigned int keys[])
{
	struct mb_cache *cache = ce->e_cache;
	unsigned int bucket;
	struct list_head *l;
	int error = -EBUSY, n;

	bucket = hash_long((unsigned long)bdev + (block & 0xffffffff), 
			   cache->c_bucket_bits);
	spin_lock(&mb_cache_spinlock);
	list_for_each_prev(l, &cache->c_block_hash[bucket]) {
		struct mb_cache_entry *ce =
			list_entry(l, struct mb_cache_entry, e_block_list);
		if (ce->e_bdev == bdev && ce->e_block == block)
			goto out;
	}
	__mb_cache_entry_unhash(ce);
	ce->e_bdev = bdev;
	ce->e_block = block;
	list_add(&ce->e_block_list, &cache->c_block_hash[bucket]);
	for (n=0; n<mb_cache_indexes(cache); n++) {
		ce->e_indexes[n].o_key = keys[n];
		bucket = hash_long(keys[n], cache->c_bucket_bits);
		list_add(&ce->e_indexes[n].o_list,
			 &cache->c_indexes_hash[n][bucket]);
	}
	error = 0;
out:
	spin_unlock(&mb_cache_spinlock);
	return error;
}


/*
 * mb_cache_entry_release()
 *
 * Release a handle to a cache entry. When the last handle to a cache entry
 * is released it is either freed (if it is invalid) or otherwise inserted
 * in to the lru list.
 */
void
mb_cache_entry_release(struct mb_cache_entry *ce)
{
	spin_lock(&mb_cache_spinlock);
	__mb_cache_entry_release_unlock(ce);
}


/*
 * mb_cache_entry_free()
 *
 * This is equivalent to the sequence mb_cache_entry_takeout() --
 * mb_cache_entry_release().
 */
void
mb_cache_entry_free(struct mb_cache_entry *ce)
{
	spin_lock(&mb_cache_spinlock);
	mb_assert(list_empty(&ce->e_lru_list));
	__mb_cache_entry_unhash(ce);
	__mb_cache_entry_release_unlock(ce);
}


/*
 * mb_cache_entry_get()
 *
 * Get a cache entry  by device / block number. (There can only be one entry
 * in the cache per device and block.) Returns NULL if no such cache entry
 * exists. The returned cache entry is locked for exclusive access ("single
 * writer").
 */
struct mb_cache_entry *
mb_cache_entry_get(struct mb_cache *cache, struct block_device *bdev,
		   sector_t block)
{
	unsigned int bucket;
	struct list_head *l;
	struct mb_cache_entry *ce;

	bucket = hash_long((unsigned long)bdev + (block & 0xffffffff),
			   cache->c_bucket_bits);
	spin_lock(&mb_cache_spinlock);
	list_for_each(l, &cache->c_block_hash[bucket]) {
		ce = list_entry(l, struct mb_cache_entry, e_block_list);
		if (ce->e_bdev == bdev && ce->e_block == block) {
			DEFINE_WAIT(wait);

			if (!list_empty(&ce->e_lru_list))
				list_del_init(&ce->e_lru_list);

			while (ce->e_used > 0) {
				ce->e_queued++;
				prepare_to_wait(&mb_cache_queue, &wait,
						TASK_UNINTERRUPTIBLE);
				spin_unlock(&mb_cache_spinlock);
				schedule();
				spin_lock(&mb_cache_spinlock);
				ce->e_queued--;
			}
			finish_wait(&mb_cache_queue, &wait);
			ce->e_used += 1 + MB_CACHE_WRITER;

			if (!__mb_cache_entry_is_hashed(ce)) {
				__mb_cache_entry_release_unlock(ce);
				return NULL;
			}
			goto cleanup;
		}
	}
	ce = NULL;

cleanup:
	spin_unlock(&mb_cache_spinlock);
	return ce;
}

#if !defined(MB_CACHE_INDEXES_COUNT) || (MB_CACHE_INDEXES_COUNT > 0)

static struct mb_cache_entry *
__mb_cache_entry_find(struct list_head *l, struct list_head *head,
		      int index, struct block_device *bdev, unsigned int key)
{
	while (l != head) {
		struct mb_cache_entry *ce =
			list_entry(l, struct mb_cache_entry,
			           e_indexes[index].o_list);
		if (ce->e_bdev == bdev && ce->e_indexes[index].o_key == key) {
			DEFINE_WAIT(wait);

			if (!list_empty(&ce->e_lru_list))
				list_del_init(&ce->e_lru_list);

			/* Incrementing before holding the lock gives readers
			   priority over writers. */
			ce->e_used++;
			while (ce->e_used >= MB_CACHE_WRITER) {
				ce->e_queued++;
				prepare_to_wait(&mb_cache_queue, &wait,
						TASK_UNINTERRUPTIBLE);
				spin_unlock(&mb_cache_spinlock);
				schedule();
				spin_lock(&mb_cache_spinlock);
				ce->e_queued--;
			}
			finish_wait(&mb_cache_queue, &wait);

			if (!__mb_cache_entry_is_hashed(ce)) {
				__mb_cache_entry_release_unlock(ce);
				spin_lock(&mb_cache_spinlock);
				return ERR_PTR(-EAGAIN);
			}
			return ce;
		}
		l = l->next;
	}
	return NULL;
}


/*
 * mb_cache_entry_find_first()
 *
 * Find the first cache entry on a given device with a certain key in
 * an additional index. Additonal matches can be found with
 * mb_cache_entry_find_next(). Returns NULL if no match was found. The
 * returned cache entry is locked for shared access ("multiple readers").
 *
 * @cache: the cache to search
 * @index: the number of the additonal index to search (0<=index<indexes_count)
 * @bdev: the device the cache entry should belong to
 * @key: the key in the index
 */
struct mb_cache_entry *
mb_cache_entry_find_first(struct mb_cache *cache, int index,
			  struct block_device *bdev, unsigned int key)
{
	unsigned int bucket = hash_long(key, cache->c_bucket_bits);
	struct list_head *l;
	struct mb_cache_entry *ce;

	mb_assert(index < mb_cache_indexes(cache));
	spin_lock(&mb_cache_spinlock);
	l = cache->c_indexes_hash[index][bucket].next;
	ce = __mb_cache_entry_find(l, &cache->c_indexes_hash[index][bucket],
	                           index, bdev, key);
	spin_unlock(&mb_cache_spinlock);
	return ce;
}


/*
 * mb_cache_entry_find_next()
 *
 * Find the next cache entry on a given device with a certain key in an
 * additional index. Returns NULL if no match could be found. The previous
 * entry is atomatically released, so that mb_cache_entry_find_next() can
 * be called like this:
 *
 * entry = mb_cache_entry_find_first();
 * while (entry) {
 * 	...
 *	entry = mb_cache_entry_find_next(entry, ...);
 * }
 *
 * @prev: The previous match
 * @index: the number of the additonal index to search (0<=index<indexes_count)
 * @bdev: the device the cache entry should belong to
 * @key: the key in the index
 */
struct mb_cache_entry *
mb_cache_entry_find_next(struct mb_cache_entry *prev, int index,
			 struct block_device *bdev, unsigned int key)
{
	struct mb_cache *cache = prev->e_cache;
	unsigned int bucket = hash_long(key, cache->c_bucket_bits);
	struct list_head *l;
	struct mb_cache_entry *ce;

	mb_assert(index < mb_cache_indexes(cache));
	spin_lock(&mb_cache_spinlock);
	l = prev->e_indexes[index].o_list.next;
	ce = __mb_cache_entry_find(l, &cache->c_indexes_hash[index][bucket],
	                           index, bdev, key);
	__mb_cache_entry_release_unlock(prev);
	return ce;
}

#endif  /* !defined(MB_CACHE_INDEXES_COUNT) || (MB_CACHE_INDEXES_COUNT > 0) */

static int __init init_mbcache(void)
{
	register_shrinker(&mb_cache_shrinker);
	return 0;
}

static void __exit exit_mbcache(void)
{
	unregister_shrinker(&mb_cache_shrinker);
}

module_init(init_mbcache)
module_exit(exit_mbcache)

