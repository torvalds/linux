/* SPDX-License-Identifier: GPL-2.0
 *
 * page_pool/helpers.h
 *	Author:	Jesper Dangaard Brouer <netoptimizer@brouer.com>
 *	Copyright (C) 2016 Red Hat, Inc.
 */

/**
 * DOC: page_pool allocator
 *
 * The page_pool allocator is optimized for recycling page or page fragment used
 * by skb packet and xdp frame.
 *
 * Basic use involves replacing any alloc_pages() calls with page_pool_alloc(),
 * which allocate memory with or without page splitting depending on the
 * requested memory size.
 *
 * If the driver knows that it always requires full pages or its allocations are
 * always smaller than half a page, it can use one of the more specific API
 * calls:
 *
 * 1. page_pool_alloc_pages(): allocate memory without page splitting when
 * driver knows that the memory it need is always bigger than half of the page
 * allocated from page pool. There is no cache line dirtying for 'struct page'
 * when a page is recycled back to the page pool.
 *
 * 2. page_pool_alloc_frag(): allocate memory with page splitting when driver
 * knows that the memory it need is always smaller than or equal to half of the
 * page allocated from page pool. Page splitting enables memory saving and thus
 * avoids TLB/cache miss for data access, but there also is some cost to
 * implement page splitting, mainly some cache line dirtying/bouncing for
 * 'struct page' and atomic operation for page->pp_ref_count.
 *
 * The API keeps track of in-flight pages, in order to let API users know when
 * it is safe to free a page_pool object, the API users must call
 * page_pool_put_page() or page_pool_free_va() to free the page_pool object, or
 * attach the page_pool object to a page_pool-aware object like skbs marked with
 * skb_mark_for_recycle().
 *
 * page_pool_put_page() may be called multiple times on the same page if a page
 * is split into multiple fragments. For the last fragment, it will either
 * recycle the page, or in case of page->_refcount > 1, it will release the DMA
 * mapping and in-flight state accounting.
 *
 * dma_sync_single_range_for_device() is only called for the last fragment when
 * page_pool is created with PP_FLAG_DMA_SYNC_DEV flag, so it depends on the
 * last freed fragment to do the sync_for_device operation for all fragments in
 * the same page when a page is split. The API user must setup pool->p.max_len
 * and pool->p.offset correctly and ensure that page_pool_put_page() is called
 * with dma_sync_size being -1 for fragment API.
 */
#ifndef _NET_PAGE_POOL_HELPERS_H
#define _NET_PAGE_POOL_HELPERS_H

#include <linux/dma-mapping.h>

#include <net/page_pool/types.h>
#include <net/net_debug.h>
#include <net/netmem.h>

#ifdef CONFIG_PAGE_POOL_STATS
/* Deprecated driver-facing API, use netlink instead */
int page_pool_ethtool_stats_get_count(void);
u8 *page_pool_ethtool_stats_get_strings(u8 *data);
u64 *page_pool_ethtool_stats_get(u64 *data, const void *stats);

bool page_pool_get_stats(const struct page_pool *pool,
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

static inline u64 *page_pool_ethtool_stats_get(u64 *data, const void *stats)
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

/**
 * page_pool_dev_alloc_frag() - allocate a page fragment.
 * @pool: pool from which to allocate
 * @offset: offset to the allocated page
 * @size: requested size
 *
 * Get a page fragment from the page allocator or page_pool caches.
 *
 * Return:
 * Return allocated page fragment, otherwise return NULL.
 */
static inline struct page *page_pool_dev_alloc_frag(struct page_pool *pool,
						    unsigned int *offset,
						    unsigned int size)
{
	gfp_t gfp = (GFP_ATOMIC | __GFP_NOWARN);

	return page_pool_alloc_frag(pool, offset, size, gfp);
}

static inline struct page *page_pool_alloc(struct page_pool *pool,
					   unsigned int *offset,
					   unsigned int *size, gfp_t gfp)
{
	unsigned int max_size = PAGE_SIZE << pool->p.order;
	struct page *page;

	if ((*size << 1) > max_size) {
		*size = max_size;
		*offset = 0;
		return page_pool_alloc_pages(pool, gfp);
	}

	page = page_pool_alloc_frag(pool, offset, *size, gfp);
	if (unlikely(!page))
		return NULL;

	/* There is very likely not enough space for another fragment, so append
	 * the remaining size to the current fragment to avoid truesize
	 * underestimate problem.
	 */
	if (pool->frag_offset + *size > max_size) {
		*size = max_size - *offset;
		pool->frag_offset = max_size;
	}

	return page;
}

/**
 * page_pool_dev_alloc() - allocate a page or a page fragment.
 * @pool: pool from which to allocate
 * @offset: offset to the allocated page
 * @size: in as the requested size, out as the allocated size
 *
 * Get a page or a page fragment from the page allocator or page_pool caches
 * depending on the requested size in order to allocate memory with least memory
 * utilization and performance penalty.
 *
 * Return:
 * Return allocated page or page fragment, otherwise return NULL.
 */
static inline struct page *page_pool_dev_alloc(struct page_pool *pool,
					       unsigned int *offset,
					       unsigned int *size)
{
	gfp_t gfp = (GFP_ATOMIC | __GFP_NOWARN);

	return page_pool_alloc(pool, offset, size, gfp);
}

static inline void *page_pool_alloc_va(struct page_pool *pool,
				       unsigned int *size, gfp_t gfp)
{
	unsigned int offset;
	struct page *page;

	/* Mask off __GFP_HIGHMEM to ensure we can use page_address() */
	page = page_pool_alloc(pool, &offset, size, gfp & ~__GFP_HIGHMEM);
	if (unlikely(!page))
		return NULL;

	return page_address(page) + offset;
}

/**
 * page_pool_dev_alloc_va() - allocate a page or a page fragment and return its
 *			      va.
 * @pool: pool from which to allocate
 * @size: in as the requested size, out as the allocated size
 *
 * This is just a thin wrapper around the page_pool_alloc() API, and
 * it returns va of the allocated page or page fragment.
 *
 * Return:
 * Return the va for the allocated page or page fragment, otherwise return NULL.
 */
static inline void *page_pool_dev_alloc_va(struct page_pool *pool,
					   unsigned int *size)
{
	gfp_t gfp = (GFP_ATOMIC | __GFP_NOWARN);

	return page_pool_alloc_va(pool, size, gfp);
}

/**
 * page_pool_get_dma_dir() - Retrieve the stored DMA direction.
 * @pool:	pool from which page was allocated
 *
 * Get the stored dma direction. A driver might decide to store this locally
 * and avoid the extra cache line from page_pool to determine the direction.
 */
static inline enum dma_data_direction
page_pool_get_dma_dir(const struct page_pool *pool)
{
	return pool->p.dma_dir;
}

static inline void page_pool_fragment_netmem(netmem_ref netmem, long nr)
{
	atomic_long_set(netmem_get_pp_ref_count_ref(netmem), nr);
}

/**
 * page_pool_fragment_page() - split a fresh page into fragments
 * @page:	page to split
 * @nr:		references to set
 *
 * pp_ref_count represents the number of outstanding references to the page,
 * which will be freed using page_pool APIs (rather than page allocator APIs
 * like put_page()). Such references are usually held by page_pool-aware
 * objects like skbs marked for page pool recycling.
 *
 * This helper allows the caller to take (set) multiple references to a
 * freshly allocated page. The page must be freshly allocated (have a
 * pp_ref_count of 1). This is commonly done by drivers and
 * "fragment allocators" to save atomic operations - either when they know
 * upfront how many references they will need; or to take MAX references and
 * return the unused ones with a single atomic dec(), instead of performing
 * multiple atomic inc() operations.
 */
static inline void page_pool_fragment_page(struct page *page, long nr)
{
	page_pool_fragment_netmem(page_to_netmem(page), nr);
}

static inline long page_pool_unref_netmem(netmem_ref netmem, long nr)
{
	atomic_long_t *pp_ref_count = netmem_get_pp_ref_count_ref(netmem);
	long ret;

	/* If nr == pp_ref_count then we have cleared all remaining
	 * references to the page:
	 * 1. 'n == 1': no need to actually overwrite it.
	 * 2. 'n != 1': overwrite it with one, which is the rare case
	 *              for pp_ref_count draining.
	 *
	 * The main advantage to doing this is that not only we avoid a atomic
	 * update, as an atomic_read is generally a much cheaper operation than
	 * an atomic update, especially when dealing with a page that may be
	 * referenced by only 2 or 3 users; but also unify the pp_ref_count
	 * handling by ensuring all pages have partitioned into only 1 piece
	 * initially, and only overwrite it when the page is partitioned into
	 * more than one piece.
	 */
	if (atomic_long_read(pp_ref_count) == nr) {
		/* As we have ensured nr is always one for constant case using
		 * the BUILD_BUG_ON(), only need to handle the non-constant case
		 * here for pp_ref_count draining, which is a rare case.
		 */
		BUILD_BUG_ON(__builtin_constant_p(nr) && nr != 1);
		if (!__builtin_constant_p(nr))
			atomic_long_set(pp_ref_count, 1);

		return 0;
	}

	ret = atomic_long_sub_return(nr, pp_ref_count);
	WARN_ON(ret < 0);

	/* We are the last user here too, reset pp_ref_count back to 1 to
	 * ensure all pages have been partitioned into 1 piece initially,
	 * this should be the rare case when the last two fragment users call
	 * page_pool_unref_page() currently.
	 */
	if (unlikely(!ret))
		atomic_long_set(pp_ref_count, 1);

	return ret;
}

static inline long page_pool_unref_page(struct page *page, long nr)
{
	return page_pool_unref_netmem(page_to_netmem(page), nr);
}

static inline void page_pool_ref_netmem(netmem_ref netmem)
{
	atomic_long_inc(netmem_get_pp_ref_count_ref(netmem));
}

static inline void page_pool_ref_page(struct page *page)
{
	page_pool_ref_netmem(page_to_netmem(page));
}

static inline bool page_pool_is_last_ref(netmem_ref netmem)
{
	/* If page_pool_unref_page() returns 0, we were the last user */
	return page_pool_unref_netmem(netmem, 1) == 0;
}

static inline void page_pool_put_netmem(struct page_pool *pool,
					netmem_ref netmem,
					unsigned int dma_sync_size,
					bool allow_direct)
{
	/* When page_pool isn't compiled-in, net/core/xdp.c doesn't
	 * allow registering MEM_TYPE_PAGE_POOL, but shield linker.
	 */
#ifdef CONFIG_PAGE_POOL
	if (!page_pool_is_last_ref(netmem))
		return;

	page_pool_put_unrefed_netmem(pool, netmem, dma_sync_size, allow_direct);
#endif
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
	page_pool_put_netmem(pool, page_to_netmem(page), dma_sync_size,
			     allow_direct);
}

static inline void page_pool_put_full_netmem(struct page_pool *pool,
					     netmem_ref netmem,
					     bool allow_direct)
{
	page_pool_put_netmem(pool, netmem, -1, allow_direct);
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
	page_pool_put_netmem(pool, page_to_netmem(page), -1, allow_direct);
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

#define PAGE_POOL_32BIT_ARCH_WITH_64BIT_DMA	\
		(sizeof(dma_addr_t) > sizeof(unsigned long))

/**
 * page_pool_free_va() - free a va into the page_pool
 * @pool: pool from which va was allocated
 * @va: va to be freed
 * @allow_direct: freed by the consumer, allow lockless caching
 *
 * Free a va allocated from page_pool_allo_va().
 */
static inline void page_pool_free_va(struct page_pool *pool, void *va,
				     bool allow_direct)
{
	page_pool_put_page(pool, virt_to_head_page(va), -1, allow_direct);
}

static inline dma_addr_t page_pool_get_dma_addr_netmem(netmem_ref netmem)
{
	dma_addr_t ret = netmem_get_dma_addr(netmem);

	if (PAGE_POOL_32BIT_ARCH_WITH_64BIT_DMA)
		ret <<= PAGE_SHIFT;

	return ret;
}

/**
 * page_pool_get_dma_addr() - Retrieve the stored DMA address.
 * @page:	page allocated from a page pool
 *
 * Fetch the DMA address of the page. The page pool to which the page belongs
 * must had been created with PP_FLAG_DMA_MAP.
 */
static inline dma_addr_t page_pool_get_dma_addr(const struct page *page)
{
	return page_pool_get_dma_addr_netmem(page_to_netmem((struct page *)page));
}

/**
 * page_pool_dma_sync_for_cpu - sync Rx page for CPU after it's written by HW
 * @pool: &page_pool the @page belongs to
 * @page: page to sync
 * @offset: offset from page start to "hard" start if using PP frags
 * @dma_sync_size: size of the data written to the page
 *
 * Can be used as a shorthand to sync Rx pages before accessing them in the
 * driver. Caller must ensure the pool was created with ``PP_FLAG_DMA_MAP``.
 * Note that this version performs DMA sync unconditionally, even if the
 * associated PP doesn't perform sync-for-device.
 */
static inline void page_pool_dma_sync_for_cpu(const struct page_pool *pool,
					      const struct page *page,
					      u32 offset, u32 dma_sync_size)
{
	dma_sync_single_range_for_cpu(pool->p.dev,
				      page_pool_get_dma_addr(page),
				      offset + pool->p.offset, dma_sync_size,
				      page_pool_get_dma_dir(pool));
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
