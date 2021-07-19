/* SPDX-License-Identifier: GPL-2.0
 *
 * page_pool.c
 *	Author:	Jesper Dangaard Brouer <netoptimizer@brouer.com>
 *	Copyright (C) 2016 Red Hat, Inc.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <net/page_pool.h>
#include <net/xdp.h>

#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/page-flags.h>
#include <linux/mm.h> /* for __put_page() */

#include <trace/events/page_pool.h>

#define DEFER_TIME (msecs_to_jiffies(1000))
#define DEFER_WARN_INTERVAL (60 * HZ)

static int page_pool_init(struct page_pool *pool,
			  const struct page_pool_params *params)
{
	unsigned int ring_qsize = 1024; /* Default */

	memcpy(&pool->p, params, sizeof(pool->p));

	/* Validate only known flags were used */
	if (pool->p.flags & ~(PP_FLAG_ALL))
		return -EINVAL;

	if (pool->p.pool_size)
		ring_qsize = pool->p.pool_size;

	/* Sanity limit mem that can be pinned down */
	if (ring_qsize > 32768)
		return -E2BIG;

	/* DMA direction is either DMA_FROM_DEVICE or DMA_BIDIRECTIONAL.
	 * DMA_BIDIRECTIONAL is for allowing page used for DMA sending,
	 * which is the XDP_TX use-case.
	 */
	if (pool->p.flags & PP_FLAG_DMA_MAP) {
		if ((pool->p.dma_dir != DMA_FROM_DEVICE) &&
		    (pool->p.dma_dir != DMA_BIDIRECTIONAL))
			return -EINVAL;
	}

	if (pool->p.flags & PP_FLAG_DMA_SYNC_DEV) {
		/* In order to request DMA-sync-for-device the page
		 * needs to be mapped
		 */
		if (!(pool->p.flags & PP_FLAG_DMA_MAP))
			return -EINVAL;

		if (!pool->p.max_len)
			return -EINVAL;

		/* pool->p.offset has to be set according to the address
		 * offset used by the DMA engine to start copying rx data
		 */
	}

	if (ptr_ring_init(&pool->ring, ring_qsize, GFP_KERNEL) < 0)
		return -ENOMEM;

	atomic_set(&pool->pages_state_release_cnt, 0);

	/* Driver calling page_pool_create() also call page_pool_destroy() */
	refcount_set(&pool->user_cnt, 1);

	if (pool->p.flags & PP_FLAG_DMA_MAP)
		get_device(pool->p.dev);

	return 0;
}

struct page_pool *page_pool_create(const struct page_pool_params *params)
{
	struct page_pool *pool;
	int err;

	pool = kzalloc_node(sizeof(*pool), GFP_KERNEL, params->nid);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	err = page_pool_init(pool, params);
	if (err < 0) {
		pr_warn("%s() gave up with errno %d\n", __func__, err);
		kfree(pool);
		return ERR_PTR(err);
	}

	return pool;
}
EXPORT_SYMBOL(page_pool_create);

static void page_pool_return_page(struct page_pool *pool, struct page *page);

noinline
static struct page *page_pool_refill_alloc_cache(struct page_pool *pool)
{
	struct ptr_ring *r = &pool->ring;
	struct page *page;
	int pref_nid; /* preferred NUMA node */

	/* Quicker fallback, avoid locks when ring is empty */
	if (__ptr_ring_empty(r))
		return NULL;

	/* Softirq guarantee CPU and thus NUMA node is stable. This,
	 * assumes CPU refilling driver RX-ring will also run RX-NAPI.
	 */
#ifdef CONFIG_NUMA
	pref_nid = (pool->p.nid == NUMA_NO_NODE) ? numa_mem_id() : pool->p.nid;
#else
	/* Ignore pool->p.nid setting if !CONFIG_NUMA, helps compiler */
	pref_nid = numa_mem_id(); /* will be zero like page_to_nid() */
#endif

	/* Slower-path: Get pages from locked ring queue */
	spin_lock(&r->consumer_lock);

	/* Refill alloc array, but only if NUMA match */
	do {
		page = __ptr_ring_consume(r);
		if (unlikely(!page))
			break;

		if (likely(page_to_nid(page) == pref_nid)) {
			pool->alloc.cache[pool->alloc.count++] = page;
		} else {
			/* NUMA mismatch;
			 * (1) release 1 page to page-allocator and
			 * (2) break out to fallthrough to alloc_pages_node.
			 * This limit stress on page buddy alloactor.
			 */
			page_pool_return_page(pool, page);
			page = NULL;
			break;
		}
	} while (pool->alloc.count < PP_ALLOC_CACHE_REFILL);

	/* Return last page */
	if (likely(pool->alloc.count > 0))
		page = pool->alloc.cache[--pool->alloc.count];

	spin_unlock(&r->consumer_lock);
	return page;
}

/* fast path */
static struct page *__page_pool_get_cached(struct page_pool *pool)
{
	struct page *page;

	/* Caller MUST guarantee safe non-concurrent access, e.g. softirq */
	if (likely(pool->alloc.count)) {
		/* Fast-path */
		page = pool->alloc.cache[--pool->alloc.count];
	} else {
		page = page_pool_refill_alloc_cache(pool);
	}

	return page;
}

static void page_pool_dma_sync_for_device(struct page_pool *pool,
					  struct page *page,
					  unsigned int dma_sync_size)
{
	dma_addr_t dma_addr = page_pool_get_dma_addr(page);

	dma_sync_size = min(dma_sync_size, pool->p.max_len);
	dma_sync_single_range_for_device(pool->p.dev, dma_addr,
					 pool->p.offset, dma_sync_size,
					 pool->p.dma_dir);
}

static bool page_pool_dma_map(struct page_pool *pool, struct page *page)
{
	dma_addr_t dma;

	/* Setup DMA mapping: use 'struct page' area for storing DMA-addr
	 * since dma_addr_t can be either 32 or 64 bits and does not always fit
	 * into page private data (i.e 32bit cpu with 64bit DMA caps)
	 * This mapping is kept for lifetime of page, until leaving pool.
	 */
	dma = dma_map_page_attrs(pool->p.dev, page, 0,
				 (PAGE_SIZE << pool->p.order),
				 pool->p.dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(pool->p.dev, dma))
		return false;

	page_pool_set_dma_addr(page, dma);

	if (pool->p.flags & PP_FLAG_DMA_SYNC_DEV)
		page_pool_dma_sync_for_device(pool, page, pool->p.max_len);

	return true;
}

static struct page *__page_pool_alloc_page_order(struct page_pool *pool,
						 gfp_t gfp)
{
	struct page *page;

	gfp |= __GFP_COMP;
	page = alloc_pages_node(pool->p.nid, gfp, pool->p.order);
	if (unlikely(!page))
		return NULL;

	if ((pool->p.flags & PP_FLAG_DMA_MAP) &&
	    unlikely(!page_pool_dma_map(pool, page))) {
		put_page(page);
		return NULL;
	}

	/* Track how many pages are held 'in-flight' */
	pool->pages_state_hold_cnt++;
	trace_page_pool_state_hold(pool, page, pool->pages_state_hold_cnt);
	return page;
}

/* slow path */
noinline
static struct page *__page_pool_alloc_pages_slow(struct page_pool *pool,
						 gfp_t gfp)
{
	const int bulk = PP_ALLOC_CACHE_REFILL;
	unsigned int pp_flags = pool->p.flags;
	unsigned int pp_order = pool->p.order;
	struct page *page;
	int i, nr_pages;

	/* Don't support bulk alloc for high-order pages */
	if (unlikely(pp_order))
		return __page_pool_alloc_page_order(pool, gfp);

	/* Unnecessary as alloc cache is empty, but guarantees zero count */
	if (unlikely(pool->alloc.count > 0))
		return pool->alloc.cache[--pool->alloc.count];

	/* Mark empty alloc.cache slots "empty" for alloc_pages_bulk_array */
	memset(&pool->alloc.cache, 0, sizeof(void *) * bulk);

	nr_pages = alloc_pages_bulk_array(gfp, bulk, pool->alloc.cache);
	if (unlikely(!nr_pages))
		return NULL;

	/* Pages have been filled into alloc.cache array, but count is zero and
	 * page element have not been (possibly) DMA mapped.
	 */
	for (i = 0; i < nr_pages; i++) {
		page = pool->alloc.cache[i];
		if ((pp_flags & PP_FLAG_DMA_MAP) &&
		    unlikely(!page_pool_dma_map(pool, page))) {
			put_page(page);
			continue;
		}
		pool->alloc.cache[pool->alloc.count++] = page;
		/* Track how many pages are held 'in-flight' */
		pool->pages_state_hold_cnt++;
		trace_page_pool_state_hold(pool, page,
					   pool->pages_state_hold_cnt);
	}

	/* Return last page */
	if (likely(pool->alloc.count > 0))
		page = pool->alloc.cache[--pool->alloc.count];
	else
		page = NULL;

	/* When page just alloc'ed is should/must have refcnt 1. */
	return page;
}

/* For using page_pool replace: alloc_pages() API calls, but provide
 * synchronization guarantee for allocation side.
 */
struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp)
{
	struct page *page;

	/* Fast-path: Get a page from cache */
	page = __page_pool_get_cached(pool);
	if (page)
		return page;

	/* Slow-path: cache empty, do real allocation */
	page = __page_pool_alloc_pages_slow(pool, gfp);
	return page;
}
EXPORT_SYMBOL(page_pool_alloc_pages);

/* Calculate distance between two u32 values, valid if distance is below 2^(31)
 *  https://en.wikipedia.org/wiki/Serial_number_arithmetic#General_Solution
 */
#define _distance(a, b)	(s32)((a) - (b))

static s32 page_pool_inflight(struct page_pool *pool)
{
	u32 release_cnt = atomic_read(&pool->pages_state_release_cnt);
	u32 hold_cnt = READ_ONCE(pool->pages_state_hold_cnt);
	s32 inflight;

	inflight = _distance(hold_cnt, release_cnt);

	trace_page_pool_release(pool, inflight, hold_cnt, release_cnt);
	WARN(inflight < 0, "Negative(%d) inflight packet-pages", inflight);

	return inflight;
}

/* Disconnects a page (from a page_pool).  API users can have a need
 * to disconnect a page (from a page_pool), to allow it to be used as
 * a regular page (that will eventually be returned to the normal
 * page-allocator via put_page).
 */
void page_pool_release_page(struct page_pool *pool, struct page *page)
{
	dma_addr_t dma;
	int count;

	if (!(pool->p.flags & PP_FLAG_DMA_MAP))
		/* Always account for inflight pages, even if we didn't
		 * map them
		 */
		goto skip_dma_unmap;

	dma = page_pool_get_dma_addr(page);

	/* When page is unmapped, it cannot be returned to our pool */
	dma_unmap_page_attrs(pool->p.dev, dma,
			     PAGE_SIZE << pool->p.order, pool->p.dma_dir,
			     DMA_ATTR_SKIP_CPU_SYNC);
	page_pool_set_dma_addr(page, 0);
skip_dma_unmap:
	/* This may be the last page returned, releasing the pool, so
	 * it is not safe to reference pool afterwards.
	 */
	count = atomic_inc_return(&pool->pages_state_release_cnt);
	trace_page_pool_state_release(pool, page, count);
}
EXPORT_SYMBOL(page_pool_release_page);

/* Return a page to the page allocator, cleaning up our state */
static void page_pool_return_page(struct page_pool *pool, struct page *page)
{
	page_pool_release_page(pool, page);

	put_page(page);
	/* An optimization would be to call __free_pages(page, pool->p.order)
	 * knowing page is not part of page-cache (thus avoiding a
	 * __page_cache_release() call).
	 */
}

static bool page_pool_recycle_in_ring(struct page_pool *pool, struct page *page)
{
	int ret;
	/* BH protection not needed if current is serving softirq */
	if (in_serving_softirq())
		ret = ptr_ring_produce(&pool->ring, page);
	else
		ret = ptr_ring_produce_bh(&pool->ring, page);

	return (ret == 0) ? true : false;
}

/* Only allow direct recycling in special circumstances, into the
 * alloc side cache.  E.g. during RX-NAPI processing for XDP_DROP use-case.
 *
 * Caller must provide appropriate safe context.
 */
static bool page_pool_recycle_in_cache(struct page *page,
				       struct page_pool *pool)
{
	if (unlikely(pool->alloc.count == PP_ALLOC_CACHE_SIZE))
		return false;

	/* Caller MUST have verified/know (page_ref_count(page) == 1) */
	pool->alloc.cache[pool->alloc.count++] = page;
	return true;
}

/* If the page refcnt == 1, this will try to recycle the page.
 * if PP_FLAG_DMA_SYNC_DEV is set, we'll try to sync the DMA area for
 * the configured size min(dma_sync_size, pool->max_len).
 * If the page refcnt != 1, then the page will be returned to memory
 * subsystem.
 */
static __always_inline struct page *
__page_pool_put_page(struct page_pool *pool, struct page *page,
		     unsigned int dma_sync_size, bool allow_direct)
{
	/* This allocator is optimized for the XDP mode that uses
	 * one-frame-per-page, but have fallbacks that act like the
	 * regular page allocator APIs.
	 *
	 * refcnt == 1 means page_pool owns page, and can recycle it.
	 *
	 * page is NOT reusable when allocated when system is under
	 * some pressure. (page_is_pfmemalloc)
	 */
	if (likely(page_ref_count(page) == 1 && !page_is_pfmemalloc(page))) {
		/* Read barrier done in page_ref_count / READ_ONCE */

		if (pool->p.flags & PP_FLAG_DMA_SYNC_DEV)
			page_pool_dma_sync_for_device(pool, page,
						      dma_sync_size);

		if (allow_direct && in_serving_softirq() &&
		    page_pool_recycle_in_cache(page, pool))
			return NULL;

		/* Page found as candidate for recycling */
		return page;
	}
	/* Fallback/non-XDP mode: API user have elevated refcnt.
	 *
	 * Many drivers split up the page into fragments, and some
	 * want to keep doing this to save memory and do refcnt based
	 * recycling. Support this use case too, to ease drivers
	 * switching between XDP/non-XDP.
	 *
	 * In-case page_pool maintains the DMA mapping, API user must
	 * call page_pool_put_page once.  In this elevated refcnt
	 * case, the DMA is unmapped/released, as driver is likely
	 * doing refcnt based recycle tricks, meaning another process
	 * will be invoking put_page.
	 */
	/* Do not replace this with page_pool_return_page() */
	page_pool_release_page(pool, page);
	put_page(page);

	return NULL;
}

void page_pool_put_page(struct page_pool *pool, struct page *page,
			unsigned int dma_sync_size, bool allow_direct)
{
	page = __page_pool_put_page(pool, page, dma_sync_size, allow_direct);
	if (page && !page_pool_recycle_in_ring(pool, page)) {
		/* Cache full, fallback to free pages */
		page_pool_return_page(pool, page);
	}
}
EXPORT_SYMBOL(page_pool_put_page);

/* Caller must not use data area after call, as this function overwrites it */
void page_pool_put_page_bulk(struct page_pool *pool, void **data,
			     int count)
{
	int i, bulk_len = 0;

	for (i = 0; i < count; i++) {
		struct page *page = virt_to_head_page(data[i]);

		page = __page_pool_put_page(pool, page, -1, false);
		/* Approved for bulk recycling in ptr_ring cache */
		if (page)
			data[bulk_len++] = page;
	}

	if (unlikely(!bulk_len))
		return;

	/* Bulk producer into ptr_ring page_pool cache */
	page_pool_ring_lock(pool);
	for (i = 0; i < bulk_len; i++) {
		if (__ptr_ring_produce(&pool->ring, data[i]))
			break; /* ring full */
	}
	page_pool_ring_unlock(pool);

	/* Hopefully all pages was return into ptr_ring */
	if (likely(i == bulk_len))
		return;

	/* ptr_ring cache full, free remaining pages outside producer lock
	 * since put_page() with refcnt == 1 can be an expensive operation
	 */
	for (; i < bulk_len; i++)
		page_pool_return_page(pool, data[i]);
}
EXPORT_SYMBOL(page_pool_put_page_bulk);

static void page_pool_empty_ring(struct page_pool *pool)
{
	struct page *page;

	/* Empty recycle ring */
	while ((page = ptr_ring_consume_bh(&pool->ring))) {
		/* Verify the refcnt invariant of cached pages */
		if (!(page_ref_count(page) == 1))
			pr_crit("%s() page_pool refcnt %d violation\n",
				__func__, page_ref_count(page));

		page_pool_return_page(pool, page);
	}
}

static void page_pool_free(struct page_pool *pool)
{
	if (pool->disconnect)
		pool->disconnect(pool);

	ptr_ring_cleanup(&pool->ring, NULL);

	if (pool->p.flags & PP_FLAG_DMA_MAP)
		put_device(pool->p.dev);

	kfree(pool);
}

static void page_pool_empty_alloc_cache_once(struct page_pool *pool)
{
	struct page *page;

	if (pool->destroy_cnt)
		return;

	/* Empty alloc cache, assume caller made sure this is
	 * no-longer in use, and page_pool_alloc_pages() cannot be
	 * call concurrently.
	 */
	while (pool->alloc.count) {
		page = pool->alloc.cache[--pool->alloc.count];
		page_pool_return_page(pool, page);
	}
}

static void page_pool_scrub(struct page_pool *pool)
{
	page_pool_empty_alloc_cache_once(pool);
	pool->destroy_cnt++;

	/* No more consumers should exist, but producers could still
	 * be in-flight.
	 */
	page_pool_empty_ring(pool);
}

static int page_pool_release(struct page_pool *pool)
{
	int inflight;

	page_pool_scrub(pool);
	inflight = page_pool_inflight(pool);
	if (!inflight)
		page_pool_free(pool);

	return inflight;
}

static void page_pool_release_retry(struct work_struct *wq)
{
	struct delayed_work *dwq = to_delayed_work(wq);
	struct page_pool *pool = container_of(dwq, typeof(*pool), release_dw);
	int inflight;

	inflight = page_pool_release(pool);
	if (!inflight)
		return;

	/* Periodic warning */
	if (time_after_eq(jiffies, pool->defer_warn)) {
		int sec = (s32)((u32)jiffies - (u32)pool->defer_start) / HZ;

		pr_warn("%s() stalled pool shutdown %d inflight %d sec\n",
			__func__, inflight, sec);
		pool->defer_warn = jiffies + DEFER_WARN_INTERVAL;
	}

	/* Still not ready to be disconnected, retry later */
	schedule_delayed_work(&pool->release_dw, DEFER_TIME);
}

void page_pool_use_xdp_mem(struct page_pool *pool, void (*disconnect)(void *))
{
	refcount_inc(&pool->user_cnt);
	pool->disconnect = disconnect;
}

void page_pool_destroy(struct page_pool *pool)
{
	if (!pool)
		return;

	if (!page_pool_put(pool))
		return;

	if (!page_pool_release(pool))
		return;

	pool->defer_start = jiffies;
	pool->defer_warn  = jiffies + DEFER_WARN_INTERVAL;

	INIT_DELAYED_WORK(&pool->release_dw, page_pool_release_retry);
	schedule_delayed_work(&pool->release_dw, DEFER_TIME);
}
EXPORT_SYMBOL(page_pool_destroy);

/* Caller must provide appropriate safe context, e.g. NAPI. */
void page_pool_update_nid(struct page_pool *pool, int new_nid)
{
	struct page *page;

	trace_page_pool_update_nid(pool, new_nid);
	pool->p.nid = new_nid;

	/* Flush pool alloc cache, as refill will check NUMA node */
	while (pool->alloc.count) {
		page = pool->alloc.cache[--pool->alloc.count];
		page_pool_return_page(pool, page);
	}
}
EXPORT_SYMBOL(page_pool_update_nid);
