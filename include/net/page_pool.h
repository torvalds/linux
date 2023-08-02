/* SPDX-License-Identifier: GPL-2.0
 *
 * page_pool.h
 *	Author:	Jesper Dangaard Brouer <netoptimizer@brouer.com>
 *	Copyright (C) 2016 Red Hat, Inc.
 */

/**
 * DOC: page_pool allocator
 *
 * This page_pool allocator is optimized for the XDP mode that
 * uses one-frame-per-page, but have fallbacks that act like the
 * regular page allocator APIs.
 *
 * Basic use involve replacing alloc_pages() calls with the
 * page_pool_alloc_pages() call.  Drivers should likely use
 * page_pool_dev_alloc_pages() replacing dev_alloc_pages().
 *
 * API keeps track of in-flight pages, in-order to let API user know
 * when it is safe to dealloactor page_pool object.  Thus, API users
 * must call page_pool_put_page() where appropriate and only attach
 * the page to a page_pool-aware objects, like skbs marked for recycling.
 *
 * API user must only call page_pool_put_page() once on a page, as it
 * will either recycle the page, or in case of elevated refcnt, it
 * will release the DMA mapping and in-flight state accounting.  We
 * hope to lift this requirement in the future.
 */
#ifndef _NET_PAGE_POOL_H
#define _NET_PAGE_POOL_H

#include <linux/mm.h> /* Needed by ptr_ring */
#include <linux/ptr_ring.h>
#include <linux/dma-direction.h>

#define PP_FLAG_DMA_MAP		BIT(0) /* Should page_pool do the DMA
					* map/unmap
					*/
#define PP_FLAG_DMA_SYNC_DEV	BIT(1) /* If set all pages that the driver gets
					* from page_pool will be
					* DMA-synced-for-device according to
					* the length provided by the device
					* driver.
					* Please note DMA-sync-for-CPU is still
					* device driver responsibility
					*/
#define PP_FLAG_PAGE_FRAG	BIT(2) /* for page frag feature */
#define PP_FLAG_ALL		(PP_FLAG_DMA_MAP |\
				 PP_FLAG_DMA_SYNC_DEV |\
				 PP_FLAG_PAGE_FRAG)

/*
 * Fast allocation side cache array/stack
 *
 * The cache size and refill watermark is related to the network
 * use-case.  The NAPI budget is 64 packets.  After a NAPI poll the RX
 * ring is usually refilled and the max consumed elements will be 64,
 * thus a natural max size of objects needed in the cache.
 *
 * Keeping room for more objects, is due to XDP_DROP use-case.  As
 * XDP_DROP allows the opportunity to recycle objects directly into
 * this array, as it shares the same softirq/NAPI protection.  If
 * cache is already full (or partly full) then the XDP_DROP recycles
 * would have to take a slower code path.
 */
#define PP_ALLOC_CACHE_SIZE	128
#define PP_ALLOC_CACHE_REFILL	64
struct pp_alloc_cache {
	u32 count;
	struct page *cache[PP_ALLOC_CACHE_SIZE];
};

/**
 * struct page_pool_params - page pool parameters
 * @flags:	PP_FLAG_DMA_MAP, PP_FLAG_DMA_SYNC_DEV, PP_FLAG_PAGE_FRAG
 * @order:	2^order pages on allocation
 * @pool_size:	size of the ptr_ring
 * @nid:	NUMA node id to allocate from pages from
 * @dev:	device, for DMA pre-mapping purposes
 * @napi:	NAPI which is the sole consumer of pages, otherwise NULL
 * @dma_dir:	DMA mapping direction
 * @max_len:	max DMA sync memory size for PP_FLAG_DMA_SYNC_DEV
 * @offset:	DMA sync address offset for PP_FLAG_DMA_SYNC_DEV
 */
struct page_pool_params {
	unsigned int	flags;
	unsigned int	order;
	unsigned int	pool_size;
	int		nid;
	struct device	*dev;
	struct napi_struct *napi;
	enum dma_data_direction dma_dir;
	unsigned int	max_len;
	unsigned int	offset;
/* private: used by test code only */
	void (*init_callback)(struct page *page, void *arg);
	void *init_arg;
};

#ifdef CONFIG_PAGE_POOL_STATS
/**
 * struct page_pool_alloc_stats - allocation statistics
 * @fast:	successful fast path allocations
 * @slow:	slow path order-0 allocations
 * @slow_high_order: slow path high order allocations
 * @empty:	ptr ring is empty, so a slow path allocation was forced
 * @refill:	an allocation which triggered a refill of the cache
 * @waive:	pages obtained from the ptr ring that cannot be added to
 *		the cache due to a NUMA mismatch
 */
struct page_pool_alloc_stats {
	u64 fast;
	u64 slow;
	u64 slow_high_order;
	u64 empty;
	u64 refill;
	u64 waive;
};

/**
 * struct page_pool_recycle_stats - recycling (freeing) statistics
 * @cached:	recycling placed page in the page pool cache
 * @cache_full:	page pool cache was full
 * @ring:	page placed into the ptr ring
 * @ring_full:	page released from page pool because the ptr ring was full
 * @released_refcnt:	page released (and not recycled) because refcnt > 1
 */
struct page_pool_recycle_stats {
	u64 cached;
	u64 cache_full;
	u64 ring;
	u64 ring_full;
	u64 released_refcnt;
};

/**
 * struct page_pool_stats - combined page pool use statistics
 * @alloc_stats:	see struct page_pool_alloc_stats
 * @recycle_stats:	see struct page_pool_recycle_stats
 *
 * Wrapper struct for combining page pool stats with different storage
 * requirements.
 */
struct page_pool_stats {
	struct page_pool_alloc_stats alloc_stats;
	struct page_pool_recycle_stats recycle_stats;
};

int page_pool_ethtool_stats_get_count(void);
u8 *page_pool_ethtool_stats_get_strings(u8 *data);
u64 *page_pool_ethtool_stats_get(u64 *data, void *stats);

/*
 * Drivers that wish to harvest page pool stats and report them to users
 * (perhaps via ethtool, debugfs, or another mechanism) can allocate a
 * struct page_pool_stats call page_pool_get_stats to get stats for the specified pool.
 */
bool page_pool_get_stats(struct page_pool *pool,
			 struct page_pool_stats *stats);
#else

static inline int page_pool_ethtool_stats_get_count(void)
{
	return 0;
}

static inline u8 *page_pool_ethtool_stats_get_strings(u8 *data)
{
	return data;
}

static inline u64 *page_pool_ethtool_stats_get(u64 *data, void *stats)
{
	return data;
}

#endif

struct page_pool {
	struct page_pool_params p;

	struct delayed_work release_dw;
	void (*disconnect)(void *);
	unsigned long defer_start;
	unsigned long defer_warn;

	u32 pages_state_hold_cnt;
	unsigned int frag_offset;
	struct page *frag_page;
	long frag_users;

#ifdef CONFIG_PAGE_POOL_STATS
	/* these stats are incremented while in softirq context */
	struct page_pool_alloc_stats alloc_stats;
#endif
	u32 xdp_mem_id;

	/*
	 * Data structure for allocation side
	 *
	 * Drivers allocation side usually already perform some kind
	 * of resource protection.  Piggyback on this protection, and
	 * require driver to protect allocation side.
	 *
	 * For NIC drivers this means, allocate a page_pool per
	 * RX-queue. As the RX-queue is already protected by
	 * Softirq/BH scheduling and napi_schedule. NAPI schedule
	 * guarantee that a single napi_struct will only be scheduled
	 * on a single CPU (see napi_schedule).
	 */
	struct pp_alloc_cache alloc ____cacheline_aligned_in_smp;

	/* Data structure for storing recycled pages.
	 *
	 * Returning/freeing pages is more complicated synchronization
	 * wise, because free's can happen on remote CPUs, with no
	 * association with allocation resource.
	 *
	 * Use ptr_ring, as it separates consumer and producer
	 * effeciently, it a way that doesn't bounce cache-lines.
	 *
	 * TODO: Implement bulk return pages into this structure.
	 */
	struct ptr_ring ring;

#ifdef CONFIG_PAGE_POOL_STATS
	/* recycle stats are per-cpu to avoid locking */
	struct page_pool_recycle_stats __percpu *recycle_stats;
#endif
	atomic_t pages_state_release_cnt;

	/* A page_pool is strictly tied to a single RX-queue being
	 * protected by NAPI, due to above pp_alloc_cache. This
	 * refcnt serves purpose is to simplify drivers error handling.
	 */
	refcount_t user_cnt;

	u64 destroy_cnt;
};

struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp);

/**
 * page_pool_dev_alloc_pages() - allocate a page.
 * @pool:	pool from which to allocate
 *
 * Get a page from the page allocator or page_pool caches.
 */
static inline struct page *page_pool_dev_alloc_pages(struct page_pool *pool)
{
	gfp_t gfp = (GFP_ATOMIC | __GFP_NOWARN);

	return page_pool_alloc_pages(pool, gfp);
}

struct page *page_pool_alloc_frag(struct page_pool *pool, unsigned int *offset,
				  unsigned int size, gfp_t gfp);

static inline struct page *page_pool_dev_alloc_frag(struct page_pool *pool,
						    unsigned int *offset,
						    unsigned int size)
{
	gfp_t gfp = (GFP_ATOMIC | __GFP_NOWARN);

	return page_pool_alloc_frag(pool, offset, size, gfp);
}

/**
 * page_pool_get_dma_dir() - Retrieve the stored DMA direction.
 * @pool:	pool from which page was allocated
 *
 * Get the stored dma direction. A driver might decide to store this locally
 * and avoid the extra cache line from page_pool to determine the direction.
 */
static
inline enum dma_data_direction page_pool_get_dma_dir(struct page_pool *pool)
{
	return pool->p.dma_dir;
}

bool page_pool_return_skb_page(struct page *page, bool napi_safe);

struct page_pool *page_pool_create(const struct page_pool_params *params);

struct xdp_mem_info;

#ifdef CONFIG_PAGE_POOL
void page_pool_unlink_napi(struct page_pool *pool);
void page_pool_destroy(struct page_pool *pool);
void page_pool_use_xdp_mem(struct page_pool *pool, void (*disconnect)(void *),
			   struct xdp_mem_info *mem);
void page_pool_put_page_bulk(struct page_pool *pool, void **data,
			     int count);
#else
static inline void page_pool_unlink_napi(struct page_pool *pool)
{
}

static inline void page_pool_destroy(struct page_pool *pool)
{
}

static inline void page_pool_use_xdp_mem(struct page_pool *pool,
					 void (*disconnect)(void *),
					 struct xdp_mem_info *mem)
{
}

static inline void page_pool_put_page_bulk(struct page_pool *pool, void **data,
					   int count)
{
}
#endif

void page_pool_put_defragged_page(struct page_pool *pool, struct page *page,
				  unsigned int dma_sync_size,
				  bool allow_direct);

/* pp_frag_count represents the number of writers who can update the page
 * either by updating skb->data or via DMA mappings for the device.
 * We can't rely on the page refcnt for that as we don't know who might be
 * holding page references and we can't reliably destroy or sync DMA mappings
 * of the fragments.
 *
 * When pp_frag_count reaches 0 we can either recycle the page if the page
 * refcnt is 1 or return it back to the memory allocator and destroy any
 * mappings we have.
 */
static inline void page_pool_fragment_page(struct page *page, long nr)
{
	atomic_long_set(&page->pp_frag_count, nr);
}

static inline long page_pool_defrag_page(struct page *page, long nr)
{
	long ret;

	/* If nr == pp_frag_count then we have cleared all remaining
	 * references to the page. No need to actually overwrite it, instead
	 * we can leave this to be overwritten by the calling function.
	 *
	 * The main advantage to doing this is that an atomic_read is
	 * generally a much cheaper operation than an atomic update,
	 * especially when dealing with a page that may be partitioned
	 * into only 2 or 3 pieces.
	 */
	if (atomic_long_read(&page->pp_frag_count) == nr)
		return 0;

	ret = atomic_long_sub_return(nr, &page->pp_frag_count);
	WARN_ON(ret < 0);
	return ret;
}

static inline bool page_pool_is_last_frag(struct page_pool *pool,
					  struct page *page)
{
	/* If fragments aren't enabled or count is 0 we were the last user */
	return !(pool->p.flags & PP_FLAG_PAGE_FRAG) ||
	       (page_pool_defrag_page(page, 1) == 0);
}

/**
 * page_pool_put_page() - release a reference to a page pool page
 * @pool:	pool from which page was allocated
 * @page:	page to release a reference on
 * @dma_sync_size: how much of the page may have been touched by the device
 * @allow_direct: released by the consumer, allow lockless caching
 *
 * The outcome of this depends on the page refcnt. If the driver bumps
 * the refcnt > 1 this will unmap the page. If the page refcnt is 1
 * the allocator owns the page and will try to recycle it in one of the pool
 * caches. If PP_FLAG_DMA_SYNC_DEV is set, the page will be synced for_device
 * using dma_sync_single_range_for_device().
 */
static inline void page_pool_put_page(struct page_pool *pool,
				      struct page *page,
				      unsigned int dma_sync_size,
				      bool allow_direct)
{
	/* When page_pool isn't compiled-in, net/core/xdp.c doesn't
	 * allow registering MEM_TYPE_PAGE_POOL, but shield linker.
	 */
#ifdef CONFIG_PAGE_POOL
	if (!page_pool_is_last_frag(pool, page))
		return;

	page_pool_put_defragged_page(pool, page, dma_sync_size, allow_direct);
#endif
}

/**
 * page_pool_put_full_page() - release a reference on a page pool page
 * @pool:	pool from which page was allocated
 * @page:	page to release a reference on
 * @allow_direct: released by the consumer, allow lockless caching
 *
 * Similar to page_pool_put_page(), but will DMA sync the entire memory area
 * as configured in &page_pool_params.max_len.
 */
static inline void page_pool_put_full_page(struct page_pool *pool,
					   struct page *page, bool allow_direct)
{
	page_pool_put_page(pool, page, -1, allow_direct);
}

/**
 * page_pool_recycle_direct() - release a reference on a page pool page
 * @pool:	pool from which page was allocated
 * @page:	page to release a reference on
 *
 * Similar to page_pool_put_full_page() but caller must guarantee safe context
 * (e.g NAPI), since it will recycle the page directly into the pool fast cache.
 */
static inline void page_pool_recycle_direct(struct page_pool *pool,
					    struct page *page)
{
	page_pool_put_full_page(pool, page, true);
}

#define PAGE_POOL_DMA_USE_PP_FRAG_COUNT	\
		(sizeof(dma_addr_t) > sizeof(unsigned long))

/**
 * page_pool_get_dma_addr() - Retrieve the stored DMA address.
 * @page:	page allocated from a page pool
 *
 * Fetch the DMA address of the page. The page pool to which the page belongs
 * must had been created with PP_FLAG_DMA_MAP.
 */
static inline dma_addr_t page_pool_get_dma_addr(struct page *page)
{
	dma_addr_t ret = page->dma_addr;

	if (PAGE_POOL_DMA_USE_PP_FRAG_COUNT)
		ret |= (dma_addr_t)page->dma_addr_upper << 16 << 16;

	return ret;
}

static inline void page_pool_set_dma_addr(struct page *page, dma_addr_t addr)
{
	page->dma_addr = addr;
	if (PAGE_POOL_DMA_USE_PP_FRAG_COUNT)
		page->dma_addr_upper = upper_32_bits(addr);
}

static inline bool is_page_pool_compiled_in(void)
{
#ifdef CONFIG_PAGE_POOL
	return true;
#else
	return false;
#endif
}

static inline bool page_pool_put(struct page_pool *pool)
{
	return refcount_dec_and_test(&pool->user_cnt);
}

/* Caller must provide appropriate safe context, e.g. NAPI. */
void page_pool_update_nid(struct page_pool *pool, int new_nid);
static inline void page_pool_nid_changed(struct page_pool *pool, int new_nid)
{
	if (unlikely(pool->p.nid != new_nid))
		page_pool_update_nid(pool, new_nid);
}

#endif /* _NET_PAGE_POOL_H */
