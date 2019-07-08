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
 * must make sure to call page_pool_release_page() when a page is
 * "leaving" the page_pool.  Or call page_pool_put_page() where
 * appropiate.  For maintaining correct accounting.
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

#define PP_FLAG_DMA_MAP 1 /* Should page_pool do the DMA map/unmap */
#define PP_FLAG_ALL	PP_FLAG_DMA_MAP

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
	void *cache[PP_ALLOC_CACHE_SIZE];
};

struct page_pool_params {
	unsigned int	flags;
	unsigned int	order;
	unsigned int	pool_size;
	int		nid;  /* Numa node id to allocate from pages from */
	struct device	*dev; /* device, for DMA pre-mapping purposes */
	enum dma_data_direction dma_dir; /* DMA mapping direction */
};

struct page_pool {
	struct page_pool_params p;

        u32 pages_state_hold_cnt;

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

	atomic_t pages_state_release_cnt;

	/* A page_pool is strictly tied to a single RX-queue being
	 * protected by NAPI, due to above pp_alloc_cache. This
	 * refcnt serves purpose is to simplify drivers error handling.
	 */
	refcount_t user_cnt;
};

struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp);

static inline struct page *page_pool_dev_alloc_pages(struct page_pool *pool)
{
	gfp_t gfp = (GFP_ATOMIC | __GFP_NOWARN);

	return page_pool_alloc_pages(pool, gfp);
}

/* get the stored dma direction. A driver might decide to treat this locally and
 * avoid the extra cache line from page_pool to determine the direction
 */
static
inline enum dma_data_direction page_pool_get_dma_dir(struct page_pool *pool)
{
	return pool->p.dma_dir;
}

struct page_pool *page_pool_create(const struct page_pool_params *params);

void __page_pool_free(struct page_pool *pool);
static inline void page_pool_free(struct page_pool *pool)
{
	/* When page_pool isn't compiled-in, net/core/xdp.c doesn't
	 * allow registering MEM_TYPE_PAGE_POOL, but shield linker.
	 */
#ifdef CONFIG_PAGE_POOL
	__page_pool_free(pool);
#endif
}

/* Drivers use this instead of page_pool_free */
static inline void page_pool_destroy(struct page_pool *pool)
{
	if (!pool)
		return;

	page_pool_free(pool);
}

/* Never call this directly, use helpers below */
void __page_pool_put_page(struct page_pool *pool,
			  struct page *page, bool allow_direct);

static inline void page_pool_put_page(struct page_pool *pool,
				      struct page *page, bool allow_direct)
{
	/* When page_pool isn't compiled-in, net/core/xdp.c doesn't
	 * allow registering MEM_TYPE_PAGE_POOL, but shield linker.
	 */
#ifdef CONFIG_PAGE_POOL
	__page_pool_put_page(pool, page, allow_direct);
#endif
}
/* Very limited use-cases allow recycle direct */
static inline void page_pool_recycle_direct(struct page_pool *pool,
					    struct page *page)
{
	__page_pool_put_page(pool, page, true);
}

/* API user MUST have disconnected alloc-side (not allowed to call
 * page_pool_alloc_pages()) before calling this.  The free-side can
 * still run concurrently, to handle in-flight packet-pages.
 *
 * A request to shutdown can fail (with false) if there are still
 * in-flight packet-pages.
 */
bool __page_pool_request_shutdown(struct page_pool *pool);
static inline bool page_pool_request_shutdown(struct page_pool *pool)
{
	bool safe_to_remove = false;

#ifdef CONFIG_PAGE_POOL
	safe_to_remove = __page_pool_request_shutdown(pool);
#endif
	return safe_to_remove;
}

/* Disconnects a page (from a page_pool).  API users can have a need
 * to disconnect a page (from a page_pool), to allow it to be used as
 * a regular page (that will eventually be returned to the normal
 * page-allocator via put_page).
 */
void page_pool_unmap_page(struct page_pool *pool, struct page *page);
static inline void page_pool_release_page(struct page_pool *pool,
					  struct page *page)
{
#ifdef CONFIG_PAGE_POOL
	page_pool_unmap_page(pool, page);
#endif
}

static inline dma_addr_t page_pool_get_dma_addr(struct page *page)
{
	return page->dma_addr;
}

static inline bool is_page_pool_compiled_in(void)
{
#ifdef CONFIG_PAGE_POOL
	return true;
#else
	return false;
#endif
}

static inline void page_pool_get(struct page_pool *pool)
{
	refcount_inc(&pool->user_cnt);
}

static inline bool page_pool_put(struct page_pool *pool)
{
	return refcount_dec_and_test(&pool->user_cnt);
}

#endif /* _NET_PAGE_POOL_H */
