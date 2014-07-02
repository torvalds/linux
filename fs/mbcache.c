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

/*
 * Lock descriptions and usage:
 *
 * Each hash chain of both the block and index hash tables now contains
 * a built-in lock used to serialize accesses to the hash chain.
 *
 * Accesses to global data structures mb_cache_list and mb_cache_lru_list
 * are serialized via the global spinlock mb_cache_spinlock.
 *
 * Each mb_cache_entry contains a spinlock, e_entry_lock, to serialize
 * accesses to its local data, such as e_used and e_queued.
 *
 * Lock ordering:
 *
 * Each block hash chain's lock has the highest lock order, followed by an
 * index hash chain's lock, mb_cache_bg_lock (used to implement mb_cache_entry's
 * lock), and mb_cach_spinlock, with the lowest order.  While holding
 * either a block or index hash chain lock, a thread can acquire an
 * mc_cache_bg_lock, which in turn can also acquire mb_cache_spinlock.
 *
 * Synchronization:
 *
 * Since both mb_cache_entry_get and mb_cache_entry_find scan the block and
 * index hash chian, it needs to lock the corresponding hash chain.  For each
 * mb_cache_entry within the chain, it needs to lock the mb_cache_entry to
 * prevent either any simultaneous release or free on the entry and also
 * to serialize accesses to either the e_used or e_queued member of the entry.
 *
 * To avoid having a dangling reference to an already freed
 * mb_cache_entry, an mb_cache_entry is only freed when it is not on a
 * block hash chain and also no longer being referenced, both e_used,
 * and e_queued are 0's.  When an mb_cache_entry is explicitly freed it is
 * first removed from a block hash chain.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/hash.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/list_bl.h>
#include <linux/mbcache.h>
#include <linux/init.h>
#include <linux/blockgroup_lock.h>
#include <linux/log2.h>

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

#define MB_CACHE_ENTRY_LOCK_BITS	ilog2(NR_BG_LOCKS)
#define	MB_CACHE_ENTRY_LOCK_INDEX(ce)			\
	(hash_long((unsigned long)ce, MB_CACHE_ENTRY_LOCK_BITS))

static DECLARE_WAIT_QUEUE_HEAD(mb_cache_queue);
static struct blockgroup_lock *mb_cache_bg_lock;
static struct kmem_cache *mb_cache_kmem_cache;

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

/*
 * Global data: list of all mbcache's, lru list, and a spinlock for
 * accessing cache data structures on SMP machines. The lru list is
 * global across all mbcaches.
 */

static LIST_HEAD(mb_cache_list);
static LIST_HEAD(mb_cache_lru_list);
static DEFINE_SPINLOCK(mb_cache_spinlock);

static inline void
__spin_lock_mb_cache_entry(struct mb_cache_entry *ce)
{
	spin_lock(bgl_lock_ptr(mb_cache_bg_lock,
		MB_CACHE_ENTRY_LOCK_INDEX(ce)));
}

static inline void
__spin_unlock_mb_cache_entry(struct mb_cache_entry *ce)
{
	spin_unlock(bgl_lock_ptr(mb_cache_bg_lock,
		MB_CACHE_ENTRY_LOCK_INDEX(ce)));
}

static inline int
__mb_cache_entry_is_block_hashed(struct mb_cache_entry *ce)
{
	return !hlist_bl_unhashed(&ce->e_block_list);
}


static inline void
__mb_cache_entry_unhash_block(struct mb_cache_entry *ce)
{
	if (__mb_cache_entry_is_block_hashed(ce))
		hlist_bl_del_init(&ce->e_block_list);
}

static inline int
__mb_cache_entry_is_index_hashed(struct mb_cache_entry *ce)
{
	return !hlist_bl_unhashed(&ce->e_index.o_list);
}

static inline void
__mb_cache_entry_unhash_index(struct mb_cache_entry *ce)
{
	if (__mb_cache_entry_is_index_hashed(ce))
		hlist_bl_del_init(&ce->e_index.o_list);
}

/*
 * __mb_cache_entry_unhash_unlock()
 *
 * This function is called to unhash both the block and index hash
 * chain.
 * It assumes both the block and index hash chain is locked upon entry.
 * It also unlock both hash chains both exit
 */
static inline void
__mb_cache_entry_unhash_unlock(struct mb_cache_entry *ce)
{
	__mb_cache_entry_unhash_index(ce);
	hlist_bl_unlock(ce->e_index_hash_p);
	__mb_cache_entry_unhash_block(ce);
	hlist_bl_unlock(ce->e_block_hash_p);
}

static void
__mb_cache_entry_forget(struct mb_cache_entry *ce, gfp_t gfp_mask)
{
	struct mb_cache *cache = ce->e_cache;

	mb_assert(!(ce->e_used || ce->e_queued || atomic_read(&ce->e_refcnt)));
	kmem_cache_free(cache->c_entry_cache, ce);
	atomic_dec(&cache->c_entry_count);
}

static void
__mb_cache_entry_release(struct mb_cache_entry *ce)
{
	/* First lock the entry to serialize access to its local data. */
	__spin_lock_mb_cache_entry(ce);
	/* Wake up all processes queuing for this cache entry. */
	if (ce->e_queued)
		wake_up_all(&mb_cache_queue);
	if (ce->e_used >= MB_CACHE_WRITER)
		ce->e_used -= MB_CACHE_WRITER;
	/*
	 * Make sure that all cache entries on lru_list have
	 * both e_used and e_qued of 0s.
	 */
	ce->e_used--;
	if (!(ce->e_used || ce->e_queued || atomic_read(&ce->e_refcnt))) {
		if (!__mb_cache_entry_is_block_hashed(ce)) {
			__spin_unlock_mb_cache_entry(ce);
			goto forget;
		}
		/*
		 * Need access to lru list, first drop entry lock,
		 * then reacquire the lock in the proper order.
		 */
		spin_lock(&mb_cache_spinlock);
		if (list_empty(&ce->e_lru_list))
			list_add_tail(&ce->e_lru_list, &mb_cache_lru_list);
		spin_unlock(&mb_cache_spinlock);
	}
	__spin_unlock_mb_cache_entry(ce);
	return;
forget:
	mb_assert(list_empty(&ce->e_lru_list));
	__mb_cache_entry_forget(ce, GFP_KERNEL);
}

/*
 * mb_cache_shrink_scan()  memory pressure callback
 *
 * This function is called by the kernel memory management when memory
 * gets low.
 *
 * @shrink: (ignored)
 * @sc: shrink_control passed from reclaim
 *
 * Returns the number of objects freed.
 */
static unsigned long
mb_cache_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	LIST_HEAD(free_list);
	struct mb_cache_entry *entry, *tmp;
	int nr_to_scan = sc->nr_to_scan;
	gfp_t gfp_mask = sc->gfp_mask;
	unsigned long freed = 0;

	mb_debug("trying to free %d entries", nr_to_scan);
	spin_lock(&mb_cache_spinlock);
	while ((nr_to_scan-- > 0) && !list_empty(&mb_cache_lru_list)) {
		struct mb_cache_entry *ce =
			list_entry(mb_cache_lru_list.next,
				struct mb_cache_entry, e_lru_list);
		list_del_init(&ce->e_lru_list);
		if (ce->e_used || ce->e_queued || atomic_read(&ce->e_refcnt))
			continue;
		spin_unlock(&mb_cache_spinlock);
		/* Prevent any find or get operation on the entry */
		hlist_bl_lock(ce->e_block_hash_p);
		hlist_bl_lock(ce->e_index_hash_p);
		/* Ignore if it is touched by a find/get */
		if (ce->e_used || ce->e_queued || atomic_read(&ce->e_refcnt) ||
			!list_empty(&ce->e_lru_list)) {
			hlist_bl_unlock(ce->e_index_hash_p);
			hlist_bl_unlock(ce->e_block_hash_p);
			spin_lock(&mb_cache_spinlock);
			continue;
		}
		__mb_cache_entry_unhash_unlock(ce);
		list_add_tail(&ce->e_lru_list, &free_list);
		spin_lock(&mb_cache_spinlock);
	}
	spin_unlock(&mb_cache_spinlock);

	list_for_each_entry_safe(entry, tmp, &free_list, e_lru_list) {
		__mb_cache_entry_forget(entry, gfp_mask);
		freed++;
	}
	return freed;
}

static unsigned long
mb_cache_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct mb_cache *cache;
	unsigned long count = 0;

	spin_lock(&mb_cache_spinlock);
	list_for_each_entry(cache, &mb_cache_list, c_cache_list) {
		mb_debug("cache %s (%d)", cache->c_name,
			  atomic_read(&cache->c_entry_count));
		count += atomic_read(&cache->c_entry_count);
	}
	spin_unlock(&mb_cache_spinlock);

	return vfs_pressure_ratio(count);
}

static struct shrinker mb_cache_shrinker = {
	.count_objects = mb_cache_shrink_count,
	.scan_objects = mb_cache_shrink_scan,
	.seeks = DEFAULT_SEEKS,
};

/*
 * mb_cache_create()  create a new cache
 *
 * All entries in one cache are equal size. Cache entries may be from
 * multiple devices. If this is the first mbcache created, registers
 * the cache with kernel memory management. Returns NULL if no more
 * memory was available.
 *
 * @name: name of the cache (informal)
 * @bucket_bits: log2(number of hash buckets)
 */
struct mb_cache *
mb_cache_create(const char *name, int bucket_bits)
{
	int n, bucket_count = 1 << bucket_bits;
	struct mb_cache *cache = NULL;

	if (!mb_cache_bg_lock) {
		mb_cache_bg_lock = kmalloc(sizeof(struct blockgroup_lock),
			GFP_KERNEL);
		if (!mb_cache_bg_lock)
			return NULL;
		bgl_lock_init(mb_cache_bg_lock);
	}

	cache = kmalloc(sizeof(struct mb_cache), GFP_KERNEL);
	if (!cache)
		return NULL;
	cache->c_name = name;
	atomic_set(&cache->c_entry_count, 0);
	cache->c_bucket_bits = bucket_bits;
	cache->c_block_hash = kmalloc(bucket_count *
		sizeof(struct hlist_bl_head), GFP_KERNEL);
	if (!cache->c_block_hash)
		goto fail;
	for (n=0; n<bucket_count; n++)
		INIT_HLIST_BL_HEAD(&cache->c_block_hash[n]);
	cache->c_index_hash = kmalloc(bucket_count *
		sizeof(struct hlist_bl_head), GFP_KERNEL);
	if (!cache->c_index_hash)
		goto fail;
	for (n=0; n<bucket_count; n++)
		INIT_HLIST_BL_HEAD(&cache->c_index_hash[n]);
	if (!mb_cache_kmem_cache) {
		mb_cache_kmem_cache = kmem_cache_create(name,
			sizeof(struct mb_cache_entry), 0,
			SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD, NULL);
		if (!mb_cache_kmem_cache)
			goto fail2;
	}
	cache->c_entry_cache = mb_cache_kmem_cache;

	/*
	 * Set an upper limit on the number of cache entries so that the hash
	 * chains won't grow too long.
	 */
	cache->c_max_entries = bucket_count << 4;

	spin_lock(&mb_cache_spinlock);
	list_add(&cache->c_cache_list, &mb_cache_list);
	spin_unlock(&mb_cache_spinlock);
	return cache;

fail2:
	kfree(cache->c_index_hash);

fail:
	kfree(cache->c_block_hash);
	kfree(cache);
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
	struct list_head *l;
	struct mb_cache_entry *ce, *tmp;

	l = &mb_cache_lru_list;
	spin_lock(&mb_cache_spinlock);
	while (!list_is_last(l, &mb_cache_lru_list)) {
		l = l->next;
		ce = list_entry(l, struct mb_cache_entry, e_lru_list);
		if (ce->e_bdev == bdev) {
			list_del_init(&ce->e_lru_list);
			if (ce->e_used || ce->e_queued ||
				atomic_read(&ce->e_refcnt))
				continue;
			spin_unlock(&mb_cache_spinlock);
			/*
			 * Prevent any find or get operation on the entry.
			 */
			hlist_bl_lock(ce->e_block_hash_p);
			hlist_bl_lock(ce->e_index_hash_p);
			/* Ignore if it is touched by a find/get */
			if (ce->e_used || ce->e_queued ||
				atomic_read(&ce->e_refcnt) ||
				!list_empty(&ce->e_lru_list)) {
				hlist_bl_unlock(ce->e_index_hash_p);
				hlist_bl_unlock(ce->e_block_hash_p);
				l = &mb_cache_lru_list;
				spin_lock(&mb_cache_spinlock);
				continue;
			}
			__mb_cache_entry_unhash_unlock(ce);
			mb_assert(!(ce->e_used || ce->e_queued ||
				atomic_read(&ce->e_refcnt)));
			list_add_tail(&ce->e_lru_list, &free_list);
			l = &mb_cache_lru_list;
			spin_lock(&mb_cache_spinlock);
		}
	}
	spin_unlock(&mb_cache_spinlock);

	list_for_each_entry_safe(ce, tmp, &free_list, e_lru_list) {
		__mb_cache_entry_forget(ce, GFP_KERNEL);
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
	struct mb_cache_entry *ce, *tmp;

	spin_lock(&mb_cache_spinlock);
	list_for_each_entry_safe(ce, tmp, &mb_cache_lru_list, e_lru_list) {
		if (ce->e_cache == cache)
			list_move_tail(&ce->e_lru_list, &free_list);
	}
	list_del(&cache->c_cache_list);
	spin_unlock(&mb_cache_spinlock);

	list_for_each_entry_safe(ce, tmp, &free_list, e_lru_list) {
		list_del_init(&ce->e_lru_list);
		/*
		 * Prevent any find or get operation on the entry.
		 */
		hlist_bl_lock(ce->e_block_hash_p);
		hlist_bl_lock(ce->e_index_hash_p);
		mb_assert(!(ce->e_used || ce->e_queued ||
			atomic_read(&ce->e_refcnt)));
		__mb_cache_entry_unhash_unlock(ce);
		__mb_cache_entry_forget(ce, GFP_KERNEL);
	}

	if (atomic_read(&cache->c_entry_count) > 0) {
		mb_error("cache %s: %d orphaned entries",
			  cache->c_name,
			  atomic_read(&cache->c_entry_count));
	}

	if (list_empty(&mb_cache_list)) {
		kmem_cache_destroy(mb_cache_kmem_cache);
		mb_cache_kmem_cache = NULL;
	}
	kfree(cache->c_index_hash);
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

	if (atomic_read(&cache->c_entry_count) >= cache->c_max_entries) {
		struct list_head *l;

		l = &mb_cache_lru_list;
		spin_lock(&mb_cache_spinlock);
		while (!list_is_last(l, &mb_cache_lru_list)) {
			l = l->next;
			ce = list_entry(l, struct mb_cache_entry, e_lru_list);
			if (ce->e_cache == cache) {
				list_del_init(&ce->e_lru_list);
				if (ce->e_used || ce->e_queued ||
					atomic_read(&ce->e_refcnt))
					continue;
				spin_unlock(&mb_cache_spinlock);
				/*
				 * Prevent any find or get operation on the
				 * entry.
				 */
				hlist_bl_lock(ce->e_block_hash_p);
				hlist_bl_lock(ce->e_index_hash_p);
				/* Ignore if it is touched by a find/get */
				if (ce->e_used || ce->e_queued ||
					atomic_read(&ce->e_refcnt) ||
					!list_empty(&ce->e_lru_list)) {
					hlist_bl_unlock(ce->e_index_hash_p);
					hlist_bl_unlock(ce->e_block_hash_p);
					l = &mb_cache_lru_list;
					spin_lock(&mb_cache_spinlock);
					continue;
				}
				mb_assert(list_empty(&ce->e_lru_list));
				mb_assert(!(ce->e_used || ce->e_queued ||
					atomic_read(&ce->e_refcnt)));
				__mb_cache_entry_unhash_unlock(ce);
				goto found;
			}
		}
		spin_unlock(&mb_cache_spinlock);
	}

	ce = kmem_cache_alloc(cache->c_entry_cache, gfp_flags);
	if (!ce)
		return NULL;
	atomic_inc(&cache->c_entry_count);
	INIT_LIST_HEAD(&ce->e_lru_list);
	INIT_HLIST_BL_NODE(&ce->e_block_list);
	INIT_HLIST_BL_NODE(&ce->e_index.o_list);
	ce->e_cache = cache;
	ce->e_queued = 0;
	atomic_set(&ce->e_refcnt, 0);
found:
	ce->e_block_hash_p = &cache->c_block_hash[0];
	ce->e_index_hash_p = &cache->c_index_hash[0];
	ce->e_used = 1 + MB_CACHE_WRITER;
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
 * @key: lookup key
 */
int
mb_cache_entry_insert(struct mb_cache_entry *ce, struct block_device *bdev,
		      sector_t block, unsigned int key)
{
	struct mb_cache *cache = ce->e_cache;
	unsigned int bucket;
	struct hlist_bl_node *l;
	struct hlist_bl_head *block_hash_p;
	struct hlist_bl_head *index_hash_p;
	struct mb_cache_entry *lce;

	mb_assert(ce);
	bucket = hash_long((unsigned long)bdev + (block & 0xffffffff), 
			   cache->c_bucket_bits);
	block_hash_p = &cache->c_block_hash[bucket];
	hlist_bl_lock(block_hash_p);
	hlist_bl_for_each_entry(lce, l, block_hash_p, e_block_list) {
		if (lce->e_bdev == bdev && lce->e_block == block) {
			hlist_bl_unlock(block_hash_p);
			return -EBUSY;
		}
	}
	mb_assert(!__mb_cache_entry_is_block_hashed(ce));
	__mb_cache_entry_unhash_block(ce);
	__mb_cache_entry_unhash_index(ce);
	ce->e_bdev = bdev;
	ce->e_block = block;
	ce->e_block_hash_p = block_hash_p;
	ce->e_index.o_key = key;
	hlist_bl_add_head(&ce->e_block_list, block_hash_p);
	hlist_bl_unlock(block_hash_p);
	bucket = hash_long(key, cache->c_bucket_bits);
	index_hash_p = &cache->c_index_hash[bucket];
	hlist_bl_lock(index_hash_p);
	ce->e_index_hash_p = index_hash_p;
	hlist_bl_add_head(&ce->e_index.o_list, index_hash_p);
	hlist_bl_unlock(index_hash_p);
	return 0;
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
	__mb_cache_entry_release(ce);
}


/*
 * mb_cache_entry_free()
 *
 */
void
mb_cache_entry_free(struct mb_cache_entry *ce)
{
	mb_assert(ce);
	mb_assert(list_empty(&ce->e_lru_list));
	hlist_bl_lock(ce->e_index_hash_p);
	__mb_cache_entry_unhash_index(ce);
	hlist_bl_unlock(ce->e_index_hash_p);
	hlist_bl_lock(ce->e_block_hash_p);
	__mb_cache_entry_unhash_block(ce);
	hlist_bl_unlock(ce->e_block_hash_p);
	__mb_cache_entry_release(ce);
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
	struct hlist_bl_node *l;
	struct mb_cache_entry *ce;
	struct hlist_bl_head *block_hash_p;

	bucket = hash_long((unsigned long)bdev + (block & 0xffffffff),
			   cache->c_bucket_bits);
	block_hash_p = &cache->c_block_hash[bucket];
	/* First serialize access to the block corresponding hash chain. */
	hlist_bl_lock(block_hash_p);
	hlist_bl_for_each_entry(ce, l, block_hash_p, e_block_list) {
		mb_assert(ce->e_block_hash_p == block_hash_p);
		if (ce->e_bdev == bdev && ce->e_block == block) {
			/*
			 * Prevent a free from removing the entry.
			 */
			atomic_inc(&ce->e_refcnt);
			hlist_bl_unlock(block_hash_p);
			__spin_lock_mb_cache_entry(ce);
			atomic_dec(&ce->e_refcnt);
			if (ce->e_used > 0) {
				DEFINE_WAIT(wait);
				while (ce->e_used > 0) {
					ce->e_queued++;
					prepare_to_wait(&mb_cache_queue, &wait,
							TASK_UNINTERRUPTIBLE);
					__spin_unlock_mb_cache_entry(ce);
					schedule();
					__spin_lock_mb_cache_entry(ce);
					ce->e_queued--;
				}
				finish_wait(&mb_cache_queue, &wait);
			}
			ce->e_used += 1 + MB_CACHE_WRITER;
			__spin_unlock_mb_cache_entry(ce);

			if (!list_empty(&ce->e_lru_list)) {
				spin_lock(&mb_cache_spinlock);
				list_del_init(&ce->e_lru_list);
				spin_unlock(&mb_cache_spinlock);
			}
			if (!__mb_cache_entry_is_block_hashed(ce)) {
				__mb_cache_entry_release(ce);
				return NULL;
			}
			return ce;
		}
	}
	hlist_bl_unlock(block_hash_p);
	return NULL;
}

#if !defined(MB_CACHE_INDEXES_COUNT) || (MB_CACHE_INDEXES_COUNT > 0)

static struct mb_cache_entry *
__mb_cache_entry_find(struct hlist_bl_node *l, struct hlist_bl_head *head,
		      struct block_device *bdev, unsigned int key)
{

	/* The index hash chain is alredy acquire by caller. */
	while (l != NULL) {
		struct mb_cache_entry *ce =
			hlist_bl_entry(l, struct mb_cache_entry,
				e_index.o_list);
		mb_assert(ce->e_index_hash_p == head);
		if (ce->e_bdev == bdev && ce->e_index.o_key == key) {
			/*
			 * Prevent a free from removing the entry.
			 */
			atomic_inc(&ce->e_refcnt);
			hlist_bl_unlock(head);
			__spin_lock_mb_cache_entry(ce);
			atomic_dec(&ce->e_refcnt);
			ce->e_used++;
			/* Incrementing before holding the lock gives readers
			   priority over writers. */
			if (ce->e_used >= MB_CACHE_WRITER) {
				DEFINE_WAIT(wait);

				while (ce->e_used >= MB_CACHE_WRITER) {
					ce->e_queued++;
					prepare_to_wait(&mb_cache_queue, &wait,
							TASK_UNINTERRUPTIBLE);
					__spin_unlock_mb_cache_entry(ce);
					schedule();
					__spin_lock_mb_cache_entry(ce);
					ce->e_queued--;
				}
				finish_wait(&mb_cache_queue, &wait);
			}
			__spin_unlock_mb_cache_entry(ce);
			if (!list_empty(&ce->e_lru_list)) {
				spin_lock(&mb_cache_spinlock);
				list_del_init(&ce->e_lru_list);
				spin_unlock(&mb_cache_spinlock);
			}
			if (!__mb_cache_entry_is_block_hashed(ce)) {
				__mb_cache_entry_release(ce);
				return ERR_PTR(-EAGAIN);
			}
			return ce;
		}
		l = l->next;
	}
	hlist_bl_unlock(head);
	return NULL;
}


/*
 * mb_cache_entry_find_first()
 *
 * Find the first cache entry on a given device with a certain key in
 * an additional index. Additional matches can be found with
 * mb_cache_entry_find_next(). Returns NULL if no match was found. The
 * returned cache entry is locked for shared access ("multiple readers").
 *
 * @cache: the cache to search
 * @bdev: the device the cache entry should belong to
 * @key: the key in the index
 */
struct mb_cache_entry *
mb_cache_entry_find_first(struct mb_cache *cache, struct block_device *bdev,
			  unsigned int key)
{
	unsigned int bucket = hash_long(key, cache->c_bucket_bits);
	struct hlist_bl_node *l;
	struct mb_cache_entry *ce = NULL;
	struct hlist_bl_head *index_hash_p;

	index_hash_p = &cache->c_index_hash[bucket];
	hlist_bl_lock(index_hash_p);
	if (!hlist_bl_empty(index_hash_p)) {
		l = hlist_bl_first(index_hash_p);
		ce = __mb_cache_entry_find(l, index_hash_p, bdev, key);
	} else
		hlist_bl_unlock(index_hash_p);
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
 * @bdev: the device the cache entry should belong to
 * @key: the key in the index
 */
struct mb_cache_entry *
mb_cache_entry_find_next(struct mb_cache_entry *prev,
			 struct block_device *bdev, unsigned int key)
{
	struct mb_cache *cache = prev->e_cache;
	unsigned int bucket = hash_long(key, cache->c_bucket_bits);
	struct hlist_bl_node *l;
	struct mb_cache_entry *ce;
	struct hlist_bl_head *index_hash_p;

	index_hash_p = &cache->c_index_hash[bucket];
	mb_assert(prev->e_index_hash_p == index_hash_p);
	hlist_bl_lock(index_hash_p);
	mb_assert(!hlist_bl_empty(index_hash_p));
	l = prev->e_index.o_list.next;
	ce = __mb_cache_entry_find(l, index_hash_p, bdev, key);
	__mb_cache_entry_release(prev);
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

