// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * zswap.c - zswap driver file
 *
 * zswap is a cache that takes pages that are in the process
 * of being swapped out and attempts to compress and store them in a
 * RAM-based memory pool.  This can result in a significant I/O reduction on
 * the swap device and, in the case where decompressing from RAM is faster
 * than reading from the swap device, can also improve workload performance.
 *
 * Copyright (C) 2012  Seth Jennings <sjenning@linux.vnet.ibm.com>
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/rbtree.h>
#include <linux/swap.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/mempolicy.h>
#include <linux/mempool.h>
#include <linux/zpool.h>
#include <crypto/acompress.h>
#include <linux/zswap.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/swapops.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/workqueue.h>
#include <linux/list_lru.h>

#include "swap.h"
#include "internal.h"

/*********************************
* statistics
**********************************/
/* Total bytes used by the compressed storage */
u64 zswap_pool_total_size;
/* The number of compressed pages currently stored in zswap */
atomic_t zswap_stored_pages = ATOMIC_INIT(0);
/* The number of same-value filled pages currently stored in zswap */
static atomic_t zswap_same_filled_pages = ATOMIC_INIT(0);

/*
 * The statistics below are not protected from concurrent access for
 * performance reasons so they may not be a 100% accurate.  However,
 * they do provide useful information on roughly how many times a
 * certain event is occurring.
*/

/* Pool limit was hit (see zswap_max_pool_percent) */
static u64 zswap_pool_limit_hit;
/* Pages written back when pool limit was reached */
static u64 zswap_written_back_pages;
/* Store failed due to a reclaim failure after pool limit was reached */
static u64 zswap_reject_reclaim_fail;
/* Store failed due to compression algorithm failure */
static u64 zswap_reject_compress_fail;
/* Compressed page was too big for the allocator to (optimally) store */
static u64 zswap_reject_compress_poor;
/* Store failed because underlying allocator could not get memory */
static u64 zswap_reject_alloc_fail;
/* Store failed because the entry metadata could not be allocated (rare) */
static u64 zswap_reject_kmemcache_fail;
/* Duplicate store was encountered (rare) */
static u64 zswap_duplicate_entry;

/* Shrinker work queue */
static struct workqueue_struct *shrink_wq;
/* Pool limit was hit, we need to calm down */
static bool zswap_pool_reached_full;

/*********************************
* tunables
**********************************/

#define ZSWAP_PARAM_UNSET ""

static int zswap_setup(void);

/* Enable/disable zswap */
static bool zswap_enabled = IS_ENABLED(CONFIG_ZSWAP_DEFAULT_ON);
static int zswap_enabled_param_set(const char *,
				   const struct kernel_param *);
static const struct kernel_param_ops zswap_enabled_param_ops = {
	.set =		zswap_enabled_param_set,
	.get =		param_get_bool,
};
module_param_cb(enabled, &zswap_enabled_param_ops, &zswap_enabled, 0644);

/* Crypto compressor to use */
static char *zswap_compressor = CONFIG_ZSWAP_COMPRESSOR_DEFAULT;
static int zswap_compressor_param_set(const char *,
				      const struct kernel_param *);
static const struct kernel_param_ops zswap_compressor_param_ops = {
	.set =		zswap_compressor_param_set,
	.get =		param_get_charp,
	.free =		param_free_charp,
};
module_param_cb(compressor, &zswap_compressor_param_ops,
		&zswap_compressor, 0644);

/* Compressed storage zpool to use */
static char *zswap_zpool_type = CONFIG_ZSWAP_ZPOOL_DEFAULT;
static int zswap_zpool_param_set(const char *, const struct kernel_param *);
static const struct kernel_param_ops zswap_zpool_param_ops = {
	.set =		zswap_zpool_param_set,
	.get =		param_get_charp,
	.free =		param_free_charp,
};
module_param_cb(zpool, &zswap_zpool_param_ops, &zswap_zpool_type, 0644);

/* The maximum percentage of memory that the compressed pool can occupy */
static unsigned int zswap_max_pool_percent = 20;
module_param_named(max_pool_percent, zswap_max_pool_percent, uint, 0644);

/* The threshold for accepting new pages after the max_pool_percent was hit */
static unsigned int zswap_accept_thr_percent = 90; /* of max pool size */
module_param_named(accept_threshold_percent, zswap_accept_thr_percent,
		   uint, 0644);

/*
 * Enable/disable handling same-value filled pages (enabled by default).
 * If disabled every page is considered non-same-value filled.
 */
static bool zswap_same_filled_pages_enabled = true;
module_param_named(same_filled_pages_enabled, zswap_same_filled_pages_enabled,
		   bool, 0644);

/* Enable/disable handling non-same-value filled pages (enabled by default) */
static bool zswap_non_same_filled_pages_enabled = true;
module_param_named(non_same_filled_pages_enabled, zswap_non_same_filled_pages_enabled,
		   bool, 0644);

static bool zswap_exclusive_loads_enabled = IS_ENABLED(
		CONFIG_ZSWAP_EXCLUSIVE_LOADS_DEFAULT_ON);
module_param_named(exclusive_loads, zswap_exclusive_loads_enabled, bool, 0644);

/* Number of zpools in zswap_pool (empirically determined for scalability) */
#define ZSWAP_NR_ZPOOLS 32

/* Enable/disable memory pressure-based shrinker. */
static bool zswap_shrinker_enabled = IS_ENABLED(
		CONFIG_ZSWAP_SHRINKER_DEFAULT_ON);
module_param_named(shrinker_enabled, zswap_shrinker_enabled, bool, 0644);

bool is_zswap_enabled(void)
{
	return zswap_enabled;
}

/*********************************
* data structures
**********************************/

struct crypto_acomp_ctx {
	struct crypto_acomp *acomp;
	struct acomp_req *req;
	struct crypto_wait wait;
	u8 *buffer;
	struct mutex mutex;
};

/*
 * The lock ordering is zswap_tree.lock -> zswap_pool.lru_lock.
 * The only case where lru_lock is not acquired while holding tree.lock is
 * when a zswap_entry is taken off the lru for writeback, in that case it
 * needs to be verified that it's still valid in the tree.
 */
struct zswap_pool {
	struct zpool *zpools[ZSWAP_NR_ZPOOLS];
	struct crypto_acomp_ctx __percpu *acomp_ctx;
	struct kref kref;
	struct list_head list;
	struct work_struct release_work;
	struct work_struct shrink_work;
	struct hlist_node node;
	char tfm_name[CRYPTO_MAX_ALG_NAME];
	struct list_lru list_lru;
	struct mem_cgroup *next_shrink;
	struct shrinker *shrinker;
	atomic_t nr_stored;
};

/*
 * struct zswap_entry
 *
 * This structure contains the metadata for tracking a single compressed
 * page within zswap.
 *
 * rbnode - links the entry into red-black tree for the appropriate swap type
 * swpentry - associated swap entry, the offset indexes into the red-black tree
 * refcount - the number of outstanding reference to the entry. This is needed
 *            to protect against premature freeing of the entry by code
 *            concurrent calls to load, invalidate, and writeback.  The lock
 *            for the zswap_tree structure that contains the entry must
 *            be held while changing the refcount.  Since the lock must
 *            be held, there is no reason to also make refcount atomic.
 * length - the length in bytes of the compressed page data.  Needed during
 *          decompression. For a same value filled page length is 0, and both
 *          pool and lru are invalid and must be ignored.
 * pool - the zswap_pool the entry's data is in
 * handle - zpool allocation handle that stores the compressed page data
 * value - value of the same-value filled pages which have same content
 * objcg - the obj_cgroup that the compressed memory is charged to
 * lru - handle to the pool's lru used to evict pages.
 */
struct zswap_entry {
	struct rb_node rbnode;
	swp_entry_t swpentry;
	int refcount;
	unsigned int length;
	struct zswap_pool *pool;
	union {
		unsigned long handle;
		unsigned long value;
	};
	struct obj_cgroup *objcg;
	struct list_head lru;
};

/*
 * The tree lock in the zswap_tree struct protects a few things:
 * - the rbtree
 * - the refcount field of each entry in the tree
 */
struct zswap_tree {
	struct rb_root rbroot;
	spinlock_t lock;
};

static struct zswap_tree *zswap_trees[MAX_SWAPFILES];

/* RCU-protected iteration */
static LIST_HEAD(zswap_pools);
/* protects zswap_pools list modification */
static DEFINE_SPINLOCK(zswap_pools_lock);
/* pool counter to provide unique names to zpool */
static atomic_t zswap_pools_count = ATOMIC_INIT(0);

enum zswap_init_type {
	ZSWAP_UNINIT,
	ZSWAP_INIT_SUCCEED,
	ZSWAP_INIT_FAILED
};

static enum zswap_init_type zswap_init_state;

/* used to ensure the integrity of initialization */
static DEFINE_MUTEX(zswap_init_lock);

/* init completed, but couldn't create the initial pool */
static bool zswap_has_pool;

/*********************************
* helpers and fwd declarations
**********************************/

#define zswap_pool_debug(msg, p)				\
	pr_debug("%s pool %s/%s\n", msg, (p)->tfm_name,		\
		 zpool_get_type((p)->zpools[0]))

static int zswap_writeback_entry(struct zswap_entry *entry,
				 struct zswap_tree *tree);
static int zswap_pool_get(struct zswap_pool *pool);
static void zswap_pool_put(struct zswap_pool *pool);

static bool zswap_is_full(void)
{
	return totalram_pages() * zswap_max_pool_percent / 100 <
			DIV_ROUND_UP(zswap_pool_total_size, PAGE_SIZE);
}

static bool zswap_can_accept(void)
{
	return totalram_pages() * zswap_accept_thr_percent / 100 *
				zswap_max_pool_percent / 100 >
			DIV_ROUND_UP(zswap_pool_total_size, PAGE_SIZE);
}

static u64 get_zswap_pool_size(struct zswap_pool *pool)
{
	u64 pool_size = 0;
	int i;

	for (i = 0; i < ZSWAP_NR_ZPOOLS; i++)
		pool_size += zpool_get_total_size(pool->zpools[i]);

	return pool_size;
}

static void zswap_update_total_size(void)
{
	struct zswap_pool *pool;
	u64 total = 0;

	rcu_read_lock();

	list_for_each_entry_rcu(pool, &zswap_pools, list)
		total += get_zswap_pool_size(pool);

	rcu_read_unlock();

	zswap_pool_total_size = total;
}

/* should be called under RCU */
#ifdef CONFIG_MEMCG
static inline struct mem_cgroup *mem_cgroup_from_entry(struct zswap_entry *entry)
{
	return entry->objcg ? obj_cgroup_memcg(entry->objcg) : NULL;
}
#else
static inline struct mem_cgroup *mem_cgroup_from_entry(struct zswap_entry *entry)
{
	return NULL;
}
#endif

static inline int entry_to_nid(struct zswap_entry *entry)
{
	return page_to_nid(virt_to_page(entry));
}

void zswap_memcg_offline_cleanup(struct mem_cgroup *memcg)
{
	struct zswap_pool *pool;

	/* lock out zswap pools list modification */
	spin_lock(&zswap_pools_lock);
	list_for_each_entry(pool, &zswap_pools, list) {
		if (pool->next_shrink == memcg)
			pool->next_shrink = mem_cgroup_iter(NULL, pool->next_shrink, NULL);
	}
	spin_unlock(&zswap_pools_lock);
}

/*********************************
* zswap entry functions
**********************************/
static struct kmem_cache *zswap_entry_cache;

static struct zswap_entry *zswap_entry_cache_alloc(gfp_t gfp, int nid)
{
	struct zswap_entry *entry;
	entry = kmem_cache_alloc_node(zswap_entry_cache, gfp, nid);
	if (!entry)
		return NULL;
	entry->refcount = 1;
	RB_CLEAR_NODE(&entry->rbnode);
	return entry;
}

static void zswap_entry_cache_free(struct zswap_entry *entry)
{
	kmem_cache_free(zswap_entry_cache, entry);
}

/*********************************
* zswap lruvec functions
**********************************/
void zswap_lruvec_state_init(struct lruvec *lruvec)
{
	atomic_long_set(&lruvec->zswap_lruvec_state.nr_zswap_protected, 0);
}

void zswap_folio_swapin(struct folio *folio)
{
	struct lruvec *lruvec;

	VM_WARN_ON_ONCE(!folio_test_locked(folio));
	lruvec = folio_lruvec(folio);
	atomic_long_inc(&lruvec->zswap_lruvec_state.nr_zswap_protected);
}

/*********************************
* lru functions
**********************************/
static void zswap_lru_add(struct list_lru *list_lru, struct zswap_entry *entry)
{
	atomic_long_t *nr_zswap_protected;
	unsigned long lru_size, old, new;
	int nid = entry_to_nid(entry);
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	/*
	 * Note that it is safe to use rcu_read_lock() here, even in the face of
	 * concurrent memcg offlining. Thanks to the memcg->kmemcg_id indirection
	 * used in list_lru lookup, only two scenarios are possible:
	 *
	 * 1. list_lru_add() is called before memcg->kmemcg_id is updated. The
	 *    new entry will be reparented to memcg's parent's list_lru.
	 * 2. list_lru_add() is called after memcg->kmemcg_id is updated. The
	 *    new entry will be added directly to memcg's parent's list_lru.
	 *
	 * Similar reasoning holds for list_lru_del() and list_lru_putback().
	 */
	rcu_read_lock();
	memcg = mem_cgroup_from_entry(entry);
	/* will always succeed */
	list_lru_add(list_lru, &entry->lru, nid, memcg);

	/* Update the protection area */
	lru_size = list_lru_count_one(list_lru, nid, memcg);
	lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(nid));
	nr_zswap_protected = &lruvec->zswap_lruvec_state.nr_zswap_protected;
	old = atomic_long_inc_return(nr_zswap_protected);
	/*
	 * Decay to avoid overflow and adapt to changing workloads.
	 * This is based on LRU reclaim cost decaying heuristics.
	 */
	do {
		new = old > lru_size / 4 ? old / 2 : old;
	} while (!atomic_long_try_cmpxchg(nr_zswap_protected, &old, new));
	rcu_read_unlock();
}

static void zswap_lru_del(struct list_lru *list_lru, struct zswap_entry *entry)
{
	int nid = entry_to_nid(entry);
	struct mem_cgroup *memcg;

	rcu_read_lock();
	memcg = mem_cgroup_from_entry(entry);
	/* will always succeed */
	list_lru_del(list_lru, &entry->lru, nid, memcg);
	rcu_read_unlock();
}

static void zswap_lru_putback(struct list_lru *list_lru,
		struct zswap_entry *entry)
{
	int nid = entry_to_nid(entry);
	spinlock_t *lock = &list_lru->node[nid].lock;
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	rcu_read_lock();
	memcg = mem_cgroup_from_entry(entry);
	spin_lock(lock);
	/* we cannot use list_lru_add here, because it increments node's lru count */
	list_lru_putback(list_lru, &entry->lru, nid, memcg);
	spin_unlock(lock);

	lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(entry_to_nid(entry)));
	/* increment the protection area to account for the LRU rotation. */
	atomic_long_inc(&lruvec->zswap_lruvec_state.nr_zswap_protected);
	rcu_read_unlock();
}

/*********************************
* rbtree functions
**********************************/
static struct zswap_entry *zswap_rb_search(struct rb_root *root, pgoff_t offset)
{
	struct rb_node *node = root->rb_node;
	struct zswap_entry *entry;
	pgoff_t entry_offset;

	while (node) {
		entry = rb_entry(node, struct zswap_entry, rbnode);
		entry_offset = swp_offset(entry->swpentry);
		if (entry_offset > offset)
			node = node->rb_left;
		else if (entry_offset < offset)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

/*
 * In the case that a entry with the same offset is found, a pointer to
 * the existing entry is stored in dupentry and the function returns -EEXIST
 */
static int zswap_rb_insert(struct rb_root *root, struct zswap_entry *entry,
			struct zswap_entry **dupentry)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;
	struct zswap_entry *myentry;
	pgoff_t myentry_offset, entry_offset = swp_offset(entry->swpentry);

	while (*link) {
		parent = *link;
		myentry = rb_entry(parent, struct zswap_entry, rbnode);
		myentry_offset = swp_offset(myentry->swpentry);
		if (myentry_offset > entry_offset)
			link = &(*link)->rb_left;
		else if (myentry_offset < entry_offset)
			link = &(*link)->rb_right;
		else {
			*dupentry = myentry;
			return -EEXIST;
		}
	}
	rb_link_node(&entry->rbnode, parent, link);
	rb_insert_color(&entry->rbnode, root);
	return 0;
}

static bool zswap_rb_erase(struct rb_root *root, struct zswap_entry *entry)
{
	if (!RB_EMPTY_NODE(&entry->rbnode)) {
		rb_erase(&entry->rbnode, root);
		RB_CLEAR_NODE(&entry->rbnode);
		return true;
	}
	return false;
}

static struct zpool *zswap_find_zpool(struct zswap_entry *entry)
{
	int i = 0;

	if (ZSWAP_NR_ZPOOLS > 1)
		i = hash_ptr(entry, ilog2(ZSWAP_NR_ZPOOLS));

	return entry->pool->zpools[i];
}

/*
 * Carries out the common pattern of freeing and entry's zpool allocation,
 * freeing the entry itself, and decrementing the number of stored pages.
 */
static void zswap_free_entry(struct zswap_entry *entry)
{
	if (!entry->length)
		atomic_dec(&zswap_same_filled_pages);
	else {
		zswap_lru_del(&entry->pool->list_lru, entry);
		zpool_free(zswap_find_zpool(entry), entry->handle);
		atomic_dec(&entry->pool->nr_stored);
		zswap_pool_put(entry->pool);
	}
	if (entry->objcg) {
		obj_cgroup_uncharge_zswap(entry->objcg, entry->length);
		obj_cgroup_put(entry->objcg);
	}
	zswap_entry_cache_free(entry);
	atomic_dec(&zswap_stored_pages);
	zswap_update_total_size();
}

/* caller must hold the tree lock */
static void zswap_entry_get(struct zswap_entry *entry)
{
	entry->refcount++;
}

/* caller must hold the tree lock
* remove from the tree and free it, if nobody reference the entry
*/
static void zswap_entry_put(struct zswap_tree *tree,
			struct zswap_entry *entry)
{
	int refcount = --entry->refcount;

	WARN_ON_ONCE(refcount < 0);
	if (refcount == 0) {
		WARN_ON_ONCE(!RB_EMPTY_NODE(&entry->rbnode));
		zswap_free_entry(entry);
	}
}

/* caller must hold the tree lock */
static struct zswap_entry *zswap_entry_find_get(struct rb_root *root,
				pgoff_t offset)
{
	struct zswap_entry *entry;

	entry = zswap_rb_search(root, offset);
	if (entry)
		zswap_entry_get(entry);

	return entry;
}

/*********************************
* shrinker functions
**********************************/
static enum lru_status shrink_memcg_cb(struct list_head *item, struct list_lru_one *l,
				       spinlock_t *lock, void *arg);

static unsigned long zswap_shrinker_scan(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	struct lruvec *lruvec = mem_cgroup_lruvec(sc->memcg, NODE_DATA(sc->nid));
	unsigned long shrink_ret, nr_protected, lru_size;
	struct zswap_pool *pool = shrinker->private_data;
	bool encountered_page_in_swapcache = false;

	if (!zswap_shrinker_enabled ||
			!mem_cgroup_zswap_writeback_enabled(sc->memcg)) {
		sc->nr_scanned = 0;
		return SHRINK_STOP;
	}

	nr_protected =
		atomic_long_read(&lruvec->zswap_lruvec_state.nr_zswap_protected);
	lru_size = list_lru_shrink_count(&pool->list_lru, sc);

	/*
	 * Abort if we are shrinking into the protected region.
	 *
	 * This short-circuiting is necessary because if we have too many multiple
	 * concurrent reclaimers getting the freeable zswap object counts at the
	 * same time (before any of them made reasonable progress), the total
	 * number of reclaimed objects might be more than the number of unprotected
	 * objects (i.e the reclaimers will reclaim into the protected area of the
	 * zswap LRU).
	 */
	if (nr_protected >= lru_size - sc->nr_to_scan) {
		sc->nr_scanned = 0;
		return SHRINK_STOP;
	}

	shrink_ret = list_lru_shrink_walk(&pool->list_lru, sc, &shrink_memcg_cb,
		&encountered_page_in_swapcache);

	if (encountered_page_in_swapcache)
		return SHRINK_STOP;

	return shrink_ret ? shrink_ret : SHRINK_STOP;
}

static unsigned long zswap_shrinker_count(struct shrinker *shrinker,
		struct shrink_control *sc)
{
	struct zswap_pool *pool = shrinker->private_data;
	struct mem_cgroup *memcg = sc->memcg;
	struct lruvec *lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(sc->nid));
	unsigned long nr_backing, nr_stored, nr_freeable, nr_protected;

	if (!zswap_shrinker_enabled || !mem_cgroup_zswap_writeback_enabled(memcg))
		return 0;

#ifdef CONFIG_MEMCG_KMEM
	mem_cgroup_flush_stats(memcg);
	nr_backing = memcg_page_state(memcg, MEMCG_ZSWAP_B) >> PAGE_SHIFT;
	nr_stored = memcg_page_state(memcg, MEMCG_ZSWAPPED);
#else
	/* use pool stats instead of memcg stats */
	nr_backing = get_zswap_pool_size(pool) >> PAGE_SHIFT;
	nr_stored = atomic_read(&pool->nr_stored);
#endif

	if (!nr_stored)
		return 0;

	nr_protected =
		atomic_long_read(&lruvec->zswap_lruvec_state.nr_zswap_protected);
	nr_freeable = list_lru_shrink_count(&pool->list_lru, sc);
	/*
	 * Subtract the lru size by an estimate of the number of pages
	 * that should be protected.
	 */
	nr_freeable = nr_freeable > nr_protected ? nr_freeable - nr_protected : 0;

	/*
	 * Scale the number of freeable pages by the memory saving factor.
	 * This ensures that the better zswap compresses memory, the fewer
	 * pages we will evict to swap (as it will otherwise incur IO for
	 * relatively small memory saving).
	 */
	return mult_frac(nr_freeable, nr_backing, nr_stored);
}

static void zswap_alloc_shrinker(struct zswap_pool *pool)
{
	pool->shrinker =
		shrinker_alloc(SHRINKER_NUMA_AWARE | SHRINKER_MEMCG_AWARE, "mm-zswap");
	if (!pool->shrinker)
		return;

	pool->shrinker->private_data = pool;
	pool->shrinker->scan_objects = zswap_shrinker_scan;
	pool->shrinker->count_objects = zswap_shrinker_count;
	pool->shrinker->batch = 0;
	pool->shrinker->seeks = DEFAULT_SEEKS;
}

/*********************************
* per-cpu code
**********************************/
static int zswap_cpu_comp_prepare(unsigned int cpu, struct hlist_node *node)
{
	struct zswap_pool *pool = hlist_entry(node, struct zswap_pool, node);
	struct crypto_acomp_ctx *acomp_ctx = per_cpu_ptr(pool->acomp_ctx, cpu);
	struct crypto_acomp *acomp;
	struct acomp_req *req;
	int ret;

	mutex_init(&acomp_ctx->mutex);

	acomp_ctx->buffer = kmalloc_node(PAGE_SIZE * 2, GFP_KERNEL, cpu_to_node(cpu));
	if (!acomp_ctx->buffer)
		return -ENOMEM;

	acomp = crypto_alloc_acomp_node(pool->tfm_name, 0, 0, cpu_to_node(cpu));
	if (IS_ERR(acomp)) {
		pr_err("could not alloc crypto acomp %s : %ld\n",
				pool->tfm_name, PTR_ERR(acomp));
		ret = PTR_ERR(acomp);
		goto acomp_fail;
	}
	acomp_ctx->acomp = acomp;

	req = acomp_request_alloc(acomp_ctx->acomp);
	if (!req) {
		pr_err("could not alloc crypto acomp_request %s\n",
		       pool->tfm_name);
		ret = -ENOMEM;
		goto req_fail;
	}
	acomp_ctx->req = req;

	crypto_init_wait(&acomp_ctx->wait);
	/*
	 * if the backend of acomp is async zip, crypto_req_done() will wakeup
	 * crypto_wait_req(); if the backend of acomp is scomp, the callback
	 * won't be called, crypto_wait_req() will return without blocking.
	 */
	acomp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &acomp_ctx->wait);

	return 0;

req_fail:
	crypto_free_acomp(acomp_ctx->acomp);
acomp_fail:
	kfree(acomp_ctx->buffer);
	return ret;
}

static int zswap_cpu_comp_dead(unsigned int cpu, struct hlist_node *node)
{
	struct zswap_pool *pool = hlist_entry(node, struct zswap_pool, node);
	struct crypto_acomp_ctx *acomp_ctx = per_cpu_ptr(pool->acomp_ctx, cpu);

	if (!IS_ERR_OR_NULL(acomp_ctx)) {
		if (!IS_ERR_OR_NULL(acomp_ctx->req))
			acomp_request_free(acomp_ctx->req);
		if (!IS_ERR_OR_NULL(acomp_ctx->acomp))
			crypto_free_acomp(acomp_ctx->acomp);
		kfree(acomp_ctx->buffer);
	}

	return 0;
}

/*********************************
* pool functions
**********************************/

static struct zswap_pool *__zswap_pool_current(void)
{
	struct zswap_pool *pool;

	pool = list_first_or_null_rcu(&zswap_pools, typeof(*pool), list);
	WARN_ONCE(!pool && zswap_has_pool,
		  "%s: no page storage pool!\n", __func__);

	return pool;
}

static struct zswap_pool *zswap_pool_current(void)
{
	assert_spin_locked(&zswap_pools_lock);

	return __zswap_pool_current();
}

static struct zswap_pool *zswap_pool_current_get(void)
{
	struct zswap_pool *pool;

	rcu_read_lock();

	pool = __zswap_pool_current();
	if (!zswap_pool_get(pool))
		pool = NULL;

	rcu_read_unlock();

	return pool;
}

static struct zswap_pool *zswap_pool_last_get(void)
{
	struct zswap_pool *pool, *last = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(pool, &zswap_pools, list)
		last = pool;
	WARN_ONCE(!last && zswap_has_pool,
		  "%s: no page storage pool!\n", __func__);
	if (!zswap_pool_get(last))
		last = NULL;

	rcu_read_unlock();

	return last;
}

/* type and compressor must be null-terminated */
static struct zswap_pool *zswap_pool_find_get(char *type, char *compressor)
{
	struct zswap_pool *pool;

	assert_spin_locked(&zswap_pools_lock);

	list_for_each_entry_rcu(pool, &zswap_pools, list) {
		if (strcmp(pool->tfm_name, compressor))
			continue;
		/* all zpools share the same type */
		if (strcmp(zpool_get_type(pool->zpools[0]), type))
			continue;
		/* if we can't get it, it's about to be destroyed */
		if (!zswap_pool_get(pool))
			continue;
		return pool;
	}

	return NULL;
}

/*
 * If the entry is still valid in the tree, drop the initial ref and remove it
 * from the tree. This function must be called with an additional ref held,
 * otherwise it may race with another invalidation freeing the entry.
 */
static void zswap_invalidate_entry(struct zswap_tree *tree,
				   struct zswap_entry *entry)
{
	if (zswap_rb_erase(&tree->rbroot, entry))
		zswap_entry_put(tree, entry);
}

static enum lru_status shrink_memcg_cb(struct list_head *item, struct list_lru_one *l,
				       spinlock_t *lock, void *arg)
{
	struct zswap_entry *entry = container_of(item, struct zswap_entry, lru);
	bool *encountered_page_in_swapcache = (bool *)arg;
	struct zswap_tree *tree;
	pgoff_t swpoffset;
	enum lru_status ret = LRU_REMOVED_RETRY;
	int writeback_result;

	/*
	 * Once the lru lock is dropped, the entry might get freed. The
	 * swpoffset is copied to the stack, and entry isn't deref'd again
	 * until the entry is verified to still be alive in the tree.
	 */
	swpoffset = swp_offset(entry->swpentry);
	tree = zswap_trees[swp_type(entry->swpentry)];
	list_lru_isolate(l, item);
	/*
	 * It's safe to drop the lock here because we return either
	 * LRU_REMOVED_RETRY or LRU_RETRY.
	 */
	spin_unlock(lock);

	/* Check for invalidate() race */
	spin_lock(&tree->lock);
	if (entry != zswap_rb_search(&tree->rbroot, swpoffset))
		goto unlock;

	/* Hold a reference to prevent a free during writeback */
	zswap_entry_get(entry);
	spin_unlock(&tree->lock);

	writeback_result = zswap_writeback_entry(entry, tree);

	spin_lock(&tree->lock);
	if (writeback_result) {
		zswap_reject_reclaim_fail++;
		zswap_lru_putback(&entry->pool->list_lru, entry);
		ret = LRU_RETRY;

		/*
		 * Encountering a page already in swap cache is a sign that we are shrinking
		 * into the warmer region. We should terminate shrinking (if we're in the dynamic
		 * shrinker context).
		 */
		if (writeback_result == -EEXIST && encountered_page_in_swapcache)
			*encountered_page_in_swapcache = true;

		goto put_unlock;
	}
	zswap_written_back_pages++;

	if (entry->objcg)
		count_objcg_event(entry->objcg, ZSWPWB);

	count_vm_event(ZSWPWB);
	/*
	 * Writeback started successfully, the page now belongs to the
	 * swapcache. Drop the entry from zswap - unless invalidate already
	 * took it out while we had the tree->lock released for IO.
	 */
	zswap_invalidate_entry(tree, entry);

put_unlock:
	/* Drop local reference */
	zswap_entry_put(tree, entry);
unlock:
	spin_unlock(&tree->lock);
	spin_lock(lock);
	return ret;
}

static int shrink_memcg(struct mem_cgroup *memcg)
{
	struct zswap_pool *pool;
	int nid, shrunk = 0;

	if (!mem_cgroup_zswap_writeback_enabled(memcg))
		return -EINVAL;

	/*
	 * Skip zombies because their LRUs are reparented and we would be
	 * reclaiming from the parent instead of the dead memcg.
	 */
	if (memcg && !mem_cgroup_online(memcg))
		return -ENOENT;

	pool = zswap_pool_current_get();
	if (!pool)
		return -EINVAL;

	for_each_node_state(nid, N_NORMAL_MEMORY) {
		unsigned long nr_to_walk = 1;

		shrunk += list_lru_walk_one(&pool->list_lru, nid, memcg,
					    &shrink_memcg_cb, NULL, &nr_to_walk);
	}
	zswap_pool_put(pool);
	return shrunk ? 0 : -EAGAIN;
}

static void shrink_worker(struct work_struct *w)
{
	struct zswap_pool *pool = container_of(w, typeof(*pool),
						shrink_work);
	struct mem_cgroup *memcg;
	int ret, failures = 0;

	/* global reclaim will select cgroup in a round-robin fashion. */
	do {
		spin_lock(&zswap_pools_lock);
		pool->next_shrink = mem_cgroup_iter(NULL, pool->next_shrink, NULL);
		memcg = pool->next_shrink;

		/*
		 * We need to retry if we have gone through a full round trip, or if we
		 * got an offline memcg (or else we risk undoing the effect of the
		 * zswap memcg offlining cleanup callback). This is not catastrophic
		 * per se, but it will keep the now offlined memcg hostage for a while.
		 *
		 * Note that if we got an online memcg, we will keep the extra
		 * reference in case the original reference obtained by mem_cgroup_iter
		 * is dropped by the zswap memcg offlining callback, ensuring that the
		 * memcg is not killed when we are reclaiming.
		 */
		if (!memcg) {
			spin_unlock(&zswap_pools_lock);
			if (++failures == MAX_RECLAIM_RETRIES)
				break;

			goto resched;
		}

		if (!mem_cgroup_tryget_online(memcg)) {
			/* drop the reference from mem_cgroup_iter() */
			mem_cgroup_iter_break(NULL, memcg);
			pool->next_shrink = NULL;
			spin_unlock(&zswap_pools_lock);

			if (++failures == MAX_RECLAIM_RETRIES)
				break;

			goto resched;
		}
		spin_unlock(&zswap_pools_lock);

		ret = shrink_memcg(memcg);
		/* drop the extra reference */
		mem_cgroup_put(memcg);

		if (ret == -EINVAL)
			break;
		if (ret && ++failures == MAX_RECLAIM_RETRIES)
			break;

resched:
		cond_resched();
	} while (!zswap_can_accept());
	zswap_pool_put(pool);
}

static struct zswap_pool *zswap_pool_create(char *type, char *compressor)
{
	int i;
	struct zswap_pool *pool;
	char name[38]; /* 'zswap' + 32 char (max) num + \0 */
	gfp_t gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM;
	int ret;

	if (!zswap_has_pool) {
		/* if either are unset, pool initialization failed, and we
		 * need both params to be set correctly before trying to
		 * create a pool.
		 */
		if (!strcmp(type, ZSWAP_PARAM_UNSET))
			return NULL;
		if (!strcmp(compressor, ZSWAP_PARAM_UNSET))
			return NULL;
	}

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	for (i = 0; i < ZSWAP_NR_ZPOOLS; i++) {
		/* unique name for each pool specifically required by zsmalloc */
		snprintf(name, 38, "zswap%x",
			 atomic_inc_return(&zswap_pools_count));

		pool->zpools[i] = zpool_create_pool(type, name, gfp);
		if (!pool->zpools[i]) {
			pr_err("%s zpool not available\n", type);
			goto error;
		}
	}
	pr_debug("using %s zpool\n", zpool_get_type(pool->zpools[0]));

	strscpy(pool->tfm_name, compressor, sizeof(pool->tfm_name));

	pool->acomp_ctx = alloc_percpu(*pool->acomp_ctx);
	if (!pool->acomp_ctx) {
		pr_err("percpu alloc failed\n");
		goto error;
	}

	ret = cpuhp_state_add_instance(CPUHP_MM_ZSWP_POOL_PREPARE,
				       &pool->node);
	if (ret)
		goto error;

	zswap_alloc_shrinker(pool);
	if (!pool->shrinker)
		goto error;

	pr_debug("using %s compressor\n", pool->tfm_name);

	/* being the current pool takes 1 ref; this func expects the
	 * caller to always add the new pool as the current pool
	 */
	kref_init(&pool->kref);
	INIT_LIST_HEAD(&pool->list);
	if (list_lru_init_memcg(&pool->list_lru, pool->shrinker))
		goto lru_fail;
	shrinker_register(pool->shrinker);
	INIT_WORK(&pool->shrink_work, shrink_worker);
	atomic_set(&pool->nr_stored, 0);

	zswap_pool_debug("created", pool);

	return pool;

lru_fail:
	list_lru_destroy(&pool->list_lru);
	shrinker_free(pool->shrinker);
error:
	if (pool->acomp_ctx)
		free_percpu(pool->acomp_ctx);
	while (i--)
		zpool_destroy_pool(pool->zpools[i]);
	kfree(pool);
	return NULL;
}

static struct zswap_pool *__zswap_pool_create_fallback(void)
{
	bool has_comp, has_zpool;

	has_comp = crypto_has_acomp(zswap_compressor, 0, 0);
	if (!has_comp && strcmp(zswap_compressor,
				CONFIG_ZSWAP_COMPRESSOR_DEFAULT)) {
		pr_err("compressor %s not available, using default %s\n",
		       zswap_compressor, CONFIG_ZSWAP_COMPRESSOR_DEFAULT);
		param_free_charp(&zswap_compressor);
		zswap_compressor = CONFIG_ZSWAP_COMPRESSOR_DEFAULT;
		has_comp = crypto_has_acomp(zswap_compressor, 0, 0);
	}
	if (!has_comp) {
		pr_err("default compressor %s not available\n",
		       zswap_compressor);
		param_free_charp(&zswap_compressor);
		zswap_compressor = ZSWAP_PARAM_UNSET;
	}

	has_zpool = zpool_has_pool(zswap_zpool_type);
	if (!has_zpool && strcmp(zswap_zpool_type,
				 CONFIG_ZSWAP_ZPOOL_DEFAULT)) {
		pr_err("zpool %s not available, using default %s\n",
		       zswap_zpool_type, CONFIG_ZSWAP_ZPOOL_DEFAULT);
		param_free_charp(&zswap_zpool_type);
		zswap_zpool_type = CONFIG_ZSWAP_ZPOOL_DEFAULT;
		has_zpool = zpool_has_pool(zswap_zpool_type);
	}
	if (!has_zpool) {
		pr_err("default zpool %s not available\n",
		       zswap_zpool_type);
		param_free_charp(&zswap_zpool_type);
		zswap_zpool_type = ZSWAP_PARAM_UNSET;
	}

	if (!has_comp || !has_zpool)
		return NULL;

	return zswap_pool_create(zswap_zpool_type, zswap_compressor);
}

static void zswap_pool_destroy(struct zswap_pool *pool)
{
	int i;

	zswap_pool_debug("destroying", pool);

	shrinker_free(pool->shrinker);
	cpuhp_state_remove_instance(CPUHP_MM_ZSWP_POOL_PREPARE, &pool->node);
	free_percpu(pool->acomp_ctx);
	list_lru_destroy(&pool->list_lru);

	spin_lock(&zswap_pools_lock);
	mem_cgroup_iter_break(NULL, pool->next_shrink);
	pool->next_shrink = NULL;
	spin_unlock(&zswap_pools_lock);

	for (i = 0; i < ZSWAP_NR_ZPOOLS; i++)
		zpool_destroy_pool(pool->zpools[i]);
	kfree(pool);
}

static int __must_check zswap_pool_get(struct zswap_pool *pool)
{
	if (!pool)
		return 0;

	return kref_get_unless_zero(&pool->kref);
}

static void __zswap_pool_release(struct work_struct *work)
{
	struct zswap_pool *pool = container_of(work, typeof(*pool),
						release_work);

	synchronize_rcu();

	/* nobody should have been able to get a kref... */
	WARN_ON(kref_get_unless_zero(&pool->kref));

	/* pool is now off zswap_pools list and has no references. */
	zswap_pool_destroy(pool);
}

static void __zswap_pool_empty(struct kref *kref)
{
	struct zswap_pool *pool;

	pool = container_of(kref, typeof(*pool), kref);

	spin_lock(&zswap_pools_lock);

	WARN_ON(pool == zswap_pool_current());

	list_del_rcu(&pool->list);

	INIT_WORK(&pool->release_work, __zswap_pool_release);
	schedule_work(&pool->release_work);

	spin_unlock(&zswap_pools_lock);
}

static void zswap_pool_put(struct zswap_pool *pool)
{
	kref_put(&pool->kref, __zswap_pool_empty);
}

/*********************************
* param callbacks
**********************************/

static bool zswap_pool_changed(const char *s, const struct kernel_param *kp)
{
	/* no change required */
	if (!strcmp(s, *(char **)kp->arg) && zswap_has_pool)
		return false;
	return true;
}

/* val must be a null-terminated string */
static int __zswap_param_set(const char *val, const struct kernel_param *kp,
			     char *type, char *compressor)
{
	struct zswap_pool *pool, *put_pool = NULL;
	char *s = strstrip((char *)val);
	int ret = 0;
	bool new_pool = false;

	mutex_lock(&zswap_init_lock);
	switch (zswap_init_state) {
	case ZSWAP_UNINIT:
		/* if this is load-time (pre-init) param setting,
		 * don't create a pool; that's done during init.
		 */
		ret = param_set_charp(s, kp);
		break;
	case ZSWAP_INIT_SUCCEED:
		new_pool = zswap_pool_changed(s, kp);
		break;
	case ZSWAP_INIT_FAILED:
		pr_err("can't set param, initialization failed\n");
		ret = -ENODEV;
	}
	mutex_unlock(&zswap_init_lock);

	/* no need to create a new pool, return directly */
	if (!new_pool)
		return ret;

	if (!type) {
		if (!zpool_has_pool(s)) {
			pr_err("zpool %s not available\n", s);
			return -ENOENT;
		}
		type = s;
	} else if (!compressor) {
		if (!crypto_has_acomp(s, 0, 0)) {
			pr_err("compressor %s not available\n", s);
			return -ENOENT;
		}
		compressor = s;
	} else {
		WARN_ON(1);
		return -EINVAL;
	}

	spin_lock(&zswap_pools_lock);

	pool = zswap_pool_find_get(type, compressor);
	if (pool) {
		zswap_pool_debug("using existing", pool);
		WARN_ON(pool == zswap_pool_current());
		list_del_rcu(&pool->list);
	}

	spin_unlock(&zswap_pools_lock);

	if (!pool)
		pool = zswap_pool_create(type, compressor);

	if (pool)
		ret = param_set_charp(s, kp);
	else
		ret = -EINVAL;

	spin_lock(&zswap_pools_lock);

	if (!ret) {
		put_pool = zswap_pool_current();
		list_add_rcu(&pool->list, &zswap_pools);
		zswap_has_pool = true;
	} else if (pool) {
		/* add the possibly pre-existing pool to the end of the pools
		 * list; if it's new (and empty) then it'll be removed and
		 * destroyed by the put after we drop the lock
		 */
		list_add_tail_rcu(&pool->list, &zswap_pools);
		put_pool = pool;
	}

	spin_unlock(&zswap_pools_lock);

	if (!zswap_has_pool && !pool) {
		/* if initial pool creation failed, and this pool creation also
		 * failed, maybe both compressor and zpool params were bad.
		 * Allow changing this param, so pool creation will succeed
		 * when the other param is changed. We already verified this
		 * param is ok in the zpool_has_pool() or crypto_has_acomp()
		 * checks above.
		 */
		ret = param_set_charp(s, kp);
	}

	/* drop the ref from either the old current pool,
	 * or the new pool we failed to add
	 */
	if (put_pool)
		zswap_pool_put(put_pool);

	return ret;
}

static int zswap_compressor_param_set(const char *val,
				      const struct kernel_param *kp)
{
	return __zswap_param_set(val, kp, zswap_zpool_type, NULL);
}

static int zswap_zpool_param_set(const char *val,
				 const struct kernel_param *kp)
{
	return __zswap_param_set(val, kp, NULL, zswap_compressor);
}

static int zswap_enabled_param_set(const char *val,
				   const struct kernel_param *kp)
{
	int ret = -ENODEV;

	/* if this is load-time (pre-init) param setting, only set param. */
	if (system_state != SYSTEM_RUNNING)
		return param_set_bool(val, kp);

	mutex_lock(&zswap_init_lock);
	switch (zswap_init_state) {
	case ZSWAP_UNINIT:
		if (zswap_setup())
			break;
		fallthrough;
	case ZSWAP_INIT_SUCCEED:
		if (!zswap_has_pool)
			pr_err("can't enable, no pool configured\n");
		else
			ret = param_set_bool(val, kp);
		break;
	case ZSWAP_INIT_FAILED:
		pr_err("can't enable, initialization failed\n");
	}
	mutex_unlock(&zswap_init_lock);

	return ret;
}

static void __zswap_load(struct zswap_entry *entry, struct page *page)
{
	struct zpool *zpool = zswap_find_zpool(entry);
	struct scatterlist input, output;
	struct crypto_acomp_ctx *acomp_ctx;
	u8 *src;

	acomp_ctx = raw_cpu_ptr(entry->pool->acomp_ctx);
	mutex_lock(&acomp_ctx->mutex);

	src = zpool_map_handle(zpool, entry->handle, ZPOOL_MM_RO);
	if (!zpool_can_sleep_mapped(zpool)) {
		memcpy(acomp_ctx->buffer, src, entry->length);
		src = acomp_ctx->buffer;
		zpool_unmap_handle(zpool, entry->handle);
	}

	sg_init_one(&input, src, entry->length);
	sg_init_table(&output, 1);
	sg_set_page(&output, page, PAGE_SIZE, 0);
	acomp_request_set_params(acomp_ctx->req, &input, &output, entry->length, PAGE_SIZE);
	BUG_ON(crypto_wait_req(crypto_acomp_decompress(acomp_ctx->req), &acomp_ctx->wait));
	BUG_ON(acomp_ctx->req->dlen != PAGE_SIZE);
	mutex_unlock(&acomp_ctx->mutex);

	if (zpool_can_sleep_mapped(zpool))
		zpool_unmap_handle(zpool, entry->handle);
}

/*********************************
* writeback code
**********************************/
/*
 * Attempts to free an entry by adding a folio to the swap cache,
 * decompressing the entry data into the folio, and issuing a
 * bio write to write the folio back to the swap device.
 *
 * This can be thought of as a "resumed writeback" of the folio
 * to the swap device.  We are basically resuming the same swap
 * writeback path that was intercepted with the zswap_store()
 * in the first place.  After the folio has been decompressed into
 * the swap cache, the compressed version stored by zswap can be
 * freed.
 */
static int zswap_writeback_entry(struct zswap_entry *entry,
				 struct zswap_tree *tree)
{
	swp_entry_t swpentry = entry->swpentry;
	struct folio *folio;
	struct mempolicy *mpol;
	bool folio_was_allocated;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
	};

	/* try to allocate swap cache folio */
	mpol = get_task_policy(current);
	folio = __read_swap_cache_async(swpentry, GFP_KERNEL, mpol,
				NO_INTERLEAVE_INDEX, &folio_was_allocated, true);
	if (!folio)
		return -ENOMEM;

	/*
	 * Found an existing folio, we raced with load/swapin. We generally
	 * writeback cold folios from zswap, and swapin means the folio just
	 * became hot. Skip this folio and let the caller find another one.
	 */
	if (!folio_was_allocated) {
		folio_put(folio);
		return -EEXIST;
	}

	/*
	 * folio is locked, and the swapcache is now secured against
	 * concurrent swapping to and from the slot. Verify that the
	 * swap entry hasn't been invalidated and recycled behind our
	 * backs (our zswap_entry reference doesn't prevent that), to
	 * avoid overwriting a new swap folio with old compressed data.
	 */
	spin_lock(&tree->lock);
	if (zswap_rb_search(&tree->rbroot, swp_offset(entry->swpentry)) != entry) {
		spin_unlock(&tree->lock);
		delete_from_swap_cache(folio);
		folio_unlock(folio);
		folio_put(folio);
		return -ENOMEM;
	}
	spin_unlock(&tree->lock);

	__zswap_load(entry, &folio->page);

	/* folio is up to date */
	folio_mark_uptodate(folio);

	/* move it to the tail of the inactive list after end_writeback */
	folio_set_reclaim(folio);

	/* start writeback */
	__swap_writepage(folio, &wbc);
	folio_put(folio);

	return 0;
}

static int zswap_is_page_same_filled(void *ptr, unsigned long *value)
{
	unsigned long *page;
	unsigned long val;
	unsigned int pos, last_pos = PAGE_SIZE / sizeof(*page) - 1;

	page = (unsigned long *)ptr;
	val = page[0];

	if (val != page[last_pos])
		return 0;

	for (pos = 1; pos < last_pos; pos++) {
		if (val != page[pos])
			return 0;
	}

	*value = val;

	return 1;
}

static void zswap_fill_page(void *ptr, unsigned long value)
{
	unsigned long *page;

	page = (unsigned long *)ptr;
	memset_l(page, value, PAGE_SIZE / sizeof(unsigned long));
}

bool zswap_store(struct folio *folio)
{
	swp_entry_t swp = folio->swap;
	int type = swp_type(swp);
	pgoff_t offset = swp_offset(swp);
	struct page *page = &folio->page;
	struct zswap_tree *tree = zswap_trees[type];
	struct zswap_entry *entry, *dupentry;
	struct scatterlist input, output;
	struct crypto_acomp_ctx *acomp_ctx;
	struct obj_cgroup *objcg = NULL;
	struct mem_cgroup *memcg = NULL;
	struct zswap_pool *pool;
	struct zpool *zpool;
	unsigned int dlen = PAGE_SIZE;
	unsigned long handle, value;
	char *buf;
	u8 *src, *dst;
	gfp_t gfp;
	int ret;

	VM_WARN_ON_ONCE(!folio_test_locked(folio));
	VM_WARN_ON_ONCE(!folio_test_swapcache(folio));

	/* Large folios aren't supported */
	if (folio_test_large(folio))
		return false;

	if (!tree)
		return false;

	/*
	 * If this is a duplicate, it must be removed before attempting to store
	 * it, otherwise, if the store fails the old page won't be removed from
	 * the tree, and it might be written back overriding the new data.
	 */
	spin_lock(&tree->lock);
	dupentry = zswap_rb_search(&tree->rbroot, offset);
	if (dupentry) {
		zswap_duplicate_entry++;
		zswap_invalidate_entry(tree, dupentry);
	}
	spin_unlock(&tree->lock);

	if (!zswap_enabled)
		return false;

	objcg = get_obj_cgroup_from_folio(folio);
	if (objcg && !obj_cgroup_may_zswap(objcg)) {
		memcg = get_mem_cgroup_from_objcg(objcg);
		if (shrink_memcg(memcg)) {
			mem_cgroup_put(memcg);
			goto reject;
		}
		mem_cgroup_put(memcg);
	}

	/* reclaim space if needed */
	if (zswap_is_full()) {
		zswap_pool_limit_hit++;
		zswap_pool_reached_full = true;
		goto shrink;
	}

	if (zswap_pool_reached_full) {
	       if (!zswap_can_accept())
			goto shrink;
		else
			zswap_pool_reached_full = false;
	}

	/* allocate entry */
	entry = zswap_entry_cache_alloc(GFP_KERNEL, page_to_nid(page));
	if (!entry) {
		zswap_reject_kmemcache_fail++;
		goto reject;
	}

	if (zswap_same_filled_pages_enabled) {
		src = kmap_local_page(page);
		if (zswap_is_page_same_filled(src, &value)) {
			kunmap_local(src);
			entry->swpentry = swp_entry(type, offset);
			entry->length = 0;
			entry->value = value;
			atomic_inc(&zswap_same_filled_pages);
			goto insert_entry;
		}
		kunmap_local(src);
	}

	if (!zswap_non_same_filled_pages_enabled)
		goto freepage;

	/* if entry is successfully added, it keeps the reference */
	entry->pool = zswap_pool_current_get();
	if (!entry->pool)
		goto freepage;

	if (objcg) {
		memcg = get_mem_cgroup_from_objcg(objcg);
		if (memcg_list_lru_alloc(memcg, &entry->pool->list_lru, GFP_KERNEL)) {
			mem_cgroup_put(memcg);
			goto put_pool;
		}
		mem_cgroup_put(memcg);
	}

	/* compress */
	acomp_ctx = raw_cpu_ptr(entry->pool->acomp_ctx);

	mutex_lock(&acomp_ctx->mutex);

	dst = acomp_ctx->buffer;
	sg_init_table(&input, 1);
	sg_set_page(&input, &folio->page, PAGE_SIZE, 0);

	/*
	 * We need PAGE_SIZE * 2 here since there maybe over-compression case,
	 * and hardware-accelerators may won't check the dst buffer size, so
	 * giving the dst buffer with enough length to avoid buffer overflow.
	 */
	sg_init_one(&output, dst, PAGE_SIZE * 2);
	acomp_request_set_params(acomp_ctx->req, &input, &output, PAGE_SIZE, dlen);
	/*
	 * it maybe looks a little bit silly that we send an asynchronous request,
	 * then wait for its completion synchronously. This makes the process look
	 * synchronous in fact.
	 * Theoretically, acomp supports users send multiple acomp requests in one
	 * acomp instance, then get those requests done simultaneously. but in this
	 * case, zswap actually does store and load page by page, there is no
	 * existing method to send the second page before the first page is done
	 * in one thread doing zwap.
	 * but in different threads running on different cpu, we have different
	 * acomp instance, so multiple threads can do (de)compression in parallel.
	 */
	ret = crypto_wait_req(crypto_acomp_compress(acomp_ctx->req), &acomp_ctx->wait);
	dlen = acomp_ctx->req->dlen;

	if (ret) {
		zswap_reject_compress_fail++;
		goto put_dstmem;
	}

	/* store */
	zpool = zswap_find_zpool(entry);
	gfp = __GFP_NORETRY | __GFP_NOWARN | __GFP_KSWAPD_RECLAIM;
	if (zpool_malloc_support_movable(zpool))
		gfp |= __GFP_HIGHMEM | __GFP_MOVABLE;
	ret = zpool_malloc(zpool, dlen, gfp, &handle);
	if (ret == -ENOSPC) {
		zswap_reject_compress_poor++;
		goto put_dstmem;
	}
	if (ret) {
		zswap_reject_alloc_fail++;
		goto put_dstmem;
	}
	buf = zpool_map_handle(zpool, handle, ZPOOL_MM_WO);
	memcpy(buf, dst, dlen);
	zpool_unmap_handle(zpool, handle);
	mutex_unlock(&acomp_ctx->mutex);

	/* populate entry */
	entry->swpentry = swp_entry(type, offset);
	entry->handle = handle;
	entry->length = dlen;

insert_entry:
	entry->objcg = objcg;
	if (objcg) {
		obj_cgroup_charge_zswap(objcg, entry->length);
		/* Account before objcg ref is moved to tree */
		count_objcg_event(objcg, ZSWPOUT);
	}

	/* map */
	spin_lock(&tree->lock);
	/*
	 * A duplicate entry should have been removed at the beginning of this
	 * function. Since the swap entry should be pinned, if a duplicate is
	 * found again here it means that something went wrong in the swap
	 * cache.
	 */
	while (zswap_rb_insert(&tree->rbroot, entry, &dupentry) == -EEXIST) {
		WARN_ON(1);
		zswap_duplicate_entry++;
		zswap_invalidate_entry(tree, dupentry);
	}
	if (entry->length) {
		INIT_LIST_HEAD(&entry->lru);
		zswap_lru_add(&entry->pool->list_lru, entry);
		atomic_inc(&entry->pool->nr_stored);
	}
	spin_unlock(&tree->lock);

	/* update stats */
	atomic_inc(&zswap_stored_pages);
	zswap_update_total_size();
	count_vm_event(ZSWPOUT);

	return true;

put_dstmem:
	mutex_unlock(&acomp_ctx->mutex);
put_pool:
	zswap_pool_put(entry->pool);
freepage:
	zswap_entry_cache_free(entry);
reject:
	if (objcg)
		obj_cgroup_put(objcg);
	return false;

shrink:
	pool = zswap_pool_last_get();
	if (pool && !queue_work(shrink_wq, &pool->shrink_work))
		zswap_pool_put(pool);
	goto reject;
}

bool zswap_load(struct folio *folio)
{
	swp_entry_t swp = folio->swap;
	int type = swp_type(swp);
	pgoff_t offset = swp_offset(swp);
	struct page *page = &folio->page;
	struct zswap_tree *tree = zswap_trees[type];
	struct zswap_entry *entry;
	u8 *dst;

	VM_WARN_ON_ONCE(!folio_test_locked(folio));

	/* find */
	spin_lock(&tree->lock);
	entry = zswap_entry_find_get(&tree->rbroot, offset);
	if (!entry) {
		spin_unlock(&tree->lock);
		return false;
	}
	spin_unlock(&tree->lock);

	if (entry->length)
		__zswap_load(entry, page);
	else {
		dst = kmap_local_page(page);
		zswap_fill_page(dst, entry->value);
		kunmap_local(dst);
	}

	count_vm_event(ZSWPIN);
	if (entry->objcg)
		count_objcg_event(entry->objcg, ZSWPIN);

	spin_lock(&tree->lock);
	if (zswap_exclusive_loads_enabled) {
		zswap_invalidate_entry(tree, entry);
		folio_mark_dirty(folio);
	} else if (entry->length) {
		zswap_lru_del(&entry->pool->list_lru, entry);
		zswap_lru_add(&entry->pool->list_lru, entry);
	}
	zswap_entry_put(tree, entry);
	spin_unlock(&tree->lock);

	return true;
}

void zswap_invalidate(int type, pgoff_t offset)
{
	struct zswap_tree *tree = zswap_trees[type];
	struct zswap_entry *entry;

	/* find */
	spin_lock(&tree->lock);
	entry = zswap_rb_search(&tree->rbroot, offset);
	if (!entry) {
		/* entry was written back */
		spin_unlock(&tree->lock);
		return;
	}
	zswap_invalidate_entry(tree, entry);
	spin_unlock(&tree->lock);
}

void zswap_swapon(int type)
{
	struct zswap_tree *tree;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		pr_err("alloc failed, zswap disabled for swap type %d\n", type);
		return;
	}

	tree->rbroot = RB_ROOT;
	spin_lock_init(&tree->lock);
	zswap_trees[type] = tree;
}

void zswap_swapoff(int type)
{
	struct zswap_tree *tree = zswap_trees[type];
	struct zswap_entry *entry, *n;

	if (!tree)
		return;

	/* walk the tree and free everything */
	spin_lock(&tree->lock);
	rbtree_postorder_for_each_entry_safe(entry, n, &tree->rbroot, rbnode)
		zswap_free_entry(entry);
	tree->rbroot = RB_ROOT;
	spin_unlock(&tree->lock);
	kfree(tree);
	zswap_trees[type] = NULL;
}

/*********************************
* debugfs functions
**********************************/
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

static struct dentry *zswap_debugfs_root;

static int zswap_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	zswap_debugfs_root = debugfs_create_dir("zswap", NULL);

	debugfs_create_u64("pool_limit_hit", 0444,
			   zswap_debugfs_root, &zswap_pool_limit_hit);
	debugfs_create_u64("reject_reclaim_fail", 0444,
			   zswap_debugfs_root, &zswap_reject_reclaim_fail);
	debugfs_create_u64("reject_alloc_fail", 0444,
			   zswap_debugfs_root, &zswap_reject_alloc_fail);
	debugfs_create_u64("reject_kmemcache_fail", 0444,
			   zswap_debugfs_root, &zswap_reject_kmemcache_fail);
	debugfs_create_u64("reject_compress_fail", 0444,
			   zswap_debugfs_root, &zswap_reject_compress_fail);
	debugfs_create_u64("reject_compress_poor", 0444,
			   zswap_debugfs_root, &zswap_reject_compress_poor);
	debugfs_create_u64("written_back_pages", 0444,
			   zswap_debugfs_root, &zswap_written_back_pages);
	debugfs_create_u64("duplicate_entry", 0444,
			   zswap_debugfs_root, &zswap_duplicate_entry);
	debugfs_create_u64("pool_total_size", 0444,
			   zswap_debugfs_root, &zswap_pool_total_size);
	debugfs_create_atomic_t("stored_pages", 0444,
				zswap_debugfs_root, &zswap_stored_pages);
	debugfs_create_atomic_t("same_filled_pages", 0444,
				zswap_debugfs_root, &zswap_same_filled_pages);

	return 0;
}
#else
static int zswap_debugfs_init(void)
{
	return 0;
}
#endif

/*********************************
* module init and exit
**********************************/
static int zswap_setup(void)
{
	struct zswap_pool *pool;
	int ret;

	zswap_entry_cache = KMEM_CACHE(zswap_entry, 0);
	if (!zswap_entry_cache) {
		pr_err("entry cache creation failed\n");
		goto cache_fail;
	}

	ret = cpuhp_setup_state_multi(CPUHP_MM_ZSWP_POOL_PREPARE,
				      "mm/zswap_pool:prepare",
				      zswap_cpu_comp_prepare,
				      zswap_cpu_comp_dead);
	if (ret)
		goto hp_fail;

	pool = __zswap_pool_create_fallback();
	if (pool) {
		pr_info("loaded using pool %s/%s\n", pool->tfm_name,
			zpool_get_type(pool->zpools[0]));
		list_add(&pool->list, &zswap_pools);
		zswap_has_pool = true;
	} else {
		pr_err("pool creation failed\n");
		zswap_enabled = false;
	}

	shrink_wq = create_workqueue("zswap-shrink");
	if (!shrink_wq)
		goto fallback_fail;

	if (zswap_debugfs_init())
		pr_warn("debugfs initialization failed\n");
	zswap_init_state = ZSWAP_INIT_SUCCEED;
	return 0;

fallback_fail:
	if (pool)
		zswap_pool_destroy(pool);
hp_fail:
	kmem_cache_destroy(zswap_entry_cache);
cache_fail:
	/* if built-in, we aren't unloaded on failure; don't allow use */
	zswap_init_state = ZSWAP_INIT_FAILED;
	zswap_enabled = false;
	return -ENOMEM;
}

static int __init zswap_init(void)
{
	if (!zswap_enabled)
		return 0;
	return zswap_setup();
}
/* must be late so crypto has time to come up */
late_initcall(zswap_init);

MODULE_AUTHOR("Seth Jennings <sjennings@variantweb.net>");
MODULE_DESCRIPTION("Compressed cache for swap pages");
