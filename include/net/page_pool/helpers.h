/* SPDX-License-Identifier: GPL-2.0
 *
 * page_pool/helpers.h
 *	Author:	Jesper Dangaard Brouer <netoptimizer@brouer.com>
 *	Copyright (C) 2016 Red Hat, Inc.
 */

/**
 * DOC: page_pool allocator
 *
 * The page_pool allocator is optimized for the XDP mode that
 * uses one frame per-page, but it can fallback on the
 * regular page allocator APIs.
 *
 * Basic use involves replacing alloc_pages() calls with the
 * page_pool_alloc_pages() call.  Drivers should use
 * page_pool_dev_alloc_pages() replacing dev_alloc_pages().
 *
 * The API keeps track of in-flight pages, in order to let API users know
 * when it is safe to free a page_pool object.  Thus, API users
 * must call page_pool_put_page() to free the page, or attach
 * the page to a page_pool-aware object like skbs marked with
 * skb_mark_for_recycle().
 *
 * API users must call page_pool_put_page() once on a page, as it
 * will either recycle the page, or in case of refcnt > 1, it will
 * release the DMA mapping and in-flight state accounting.
 */
#ifndef _NET_PAGE_POOL_HELPERS_H
#define _NET_PAGE_POOL_HELPERS_H

#include <net/page_pool/types.h>

#ifdef CONFIG_PAGE_POOL_STATS
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

static inline bool page_pool_put(struct page_pool *pool)
{
	return refcount_dec_and_test(&pool->user_cnt);
}

static inline void page_pool_nid_changed(struct page_pool *pool, int new_nid)
{
	if (unlikely(pool->p.nid != new_nid))
		page_pool_update_nid(pool, new_nid);
}

#endif /* _NET_PAGE_POOL_HELPERS_H */
