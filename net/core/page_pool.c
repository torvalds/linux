/* SPDX-License-Identifier: GPL-2.0
 *
 * page_pool.c
 *	Author:	Jesper Dangaard Brouer <netoptimizer@brouer.com>
 *	Copyright (C) 2016 Red Hat, Inc.
 */

#include <linux/error-injection.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>
#include <net/xdp.h>

#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/page-flags.h>
#include <linux/mm.h> /* for put_page() */
#include <linux/poison.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>

#include <trace/events/page_pool.h>

#include "mp_dmabuf_devmem.h"
#include "netmem_priv.h"
#include "page_pool_priv.h"

DEFINE_STATIC_KEY_FALSE(page_pool_mem_providers);

#define DEFER_TIME (msecs_to_jiffies(1000))
#define DEFER_WARN_INTERVAL (60 * HZ)

#define BIAS_MAX	(LONG_MAX >> 1)

#ifdef CONFIG_PAGE_POOL_STATS
static DEFINE_PER_CPU(struct page_pool_recycle_stats, pp_system_recycle_stats);

/* alloc_stat_inc is intended to be used in softirq context */
#define alloc_stat_inc(pool, __stat)	(pool->alloc_stats.__stat++)
/* recycle_stat_inc is safe to use when preemption is possible. */
#define recycle_stat_inc(pool, __stat)							\
	do {										\
		struct page_pool_recycle_stats __percpu *s = pool->recycle_stats;	\
		this_cpu_inc(s->__stat);						\
	} while (0)

#define recycle_stat_add(pool, __stat, val)						\
	do {										\
		struct page_pool_recycle_stats __percpu *s = pool->recycle_stats;	\
		this_cpu_add(s->__stat, val);						\
	} while (0)

static const char pp_stats[][ETH_GSTRING_LEN] = {
	"rx_pp_alloc_fast",
	"rx_pp_alloc_slow",
	"rx_pp_alloc_slow_ho",
	"rx_pp_alloc_empty",
	"rx_pp_alloc_refill",
	"rx_pp_alloc_waive",
	"rx_pp_recycle_cached",
	"rx_pp_recycle_cache_full",
	"rx_pp_recycle_ring",
	"rx_pp_recycle_ring_full",
	"rx_pp_recycle_released_ref",
};

/**
 * page_pool_get_stats() - fetch page pool stats
 * @pool:	pool from which page was allocated
 * @stats:	struct page_pool_stats to fill in
 *
 * Retrieve statistics about the page_pool. This API is only available
 * if the kernel has been configured with ``CONFIG_PAGE_POOL_STATS=y``.
 * A pointer to a caller allocated struct page_pool_stats structure
 * is passed to this API which is filled in. The caller can then report
 * those stats to the user (perhaps via ethtool, debugfs, etc.).
 */
bool page_pool_get_stats(const struct page_pool *pool,
			 struct page_pool_stats *stats)
{
	int cpu = 0;

	if (!stats)
		return false;

	/* The caller is responsible to initialize stats. */
	stats->alloc_stats.fast += pool->alloc_stats.fast;
	stats->alloc_stats.slow += pool->alloc_stats.slow;
	stats->alloc_stats.slow_high_order += pool->alloc_stats.slow_high_order;
	stats->alloc_stats.empty += pool->alloc_stats.empty;
	stats->alloc_stats.refill += pool->alloc_stats.refill;
	stats->alloc_stats.waive += pool->alloc_stats.waive;

	for_each_possible_cpu(cpu) {
		const struct page_pool_recycle_stats *pcpu =
			per_cpu_ptr(pool->recycle_stats, cpu);

		stats->recycle_stats.cached += pcpu->cached;
		stats->recycle_stats.cache_full += pcpu->cache_full;
		stats->recycle_stats.ring += pcpu->ring;
		stats->recycle_stats.ring_full += pcpu->ring_full;
		stats->recycle_stats.released_refcnt += pcpu->released_refcnt;
	}

	return true;
}
EXPORT_SYMBOL(page_pool_get_stats);

u8 *page_pool_ethtool_stats_get_strings(u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pp_stats); i++) {
		memcpy(data, pp_stats[i], ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}

	return data;
}
EXPORT_SYMBOL(page_pool_ethtool_stats_get_strings);

int page_pool_ethtool_stats_get_count(void)
{
	return ARRAY_SIZE(pp_stats);
}
EXPORT_SYMBOL(page_pool_ethtool_stats_get_count);

u64 *page_pool_ethtool_stats_get(u64 *data, const void *stats)
{
	const struct page_pool_stats *pool_stats = stats;

	*data++ = pool_stats->alloc_stats.fast;
	*data++ = pool_stats->alloc_stats.slow;
	*data++ = pool_stats->alloc_stats.slow_high_order;
	*data++ = pool_stats->alloc_stats.empty;
	*data++ = pool_stats->alloc_stats.refill;
	*data++ = pool_stats->alloc_stats.waive;
	*data++ = pool_stats->recycle_stats.cached;
	*data++ = pool_stats->recycle_stats.cache_full;
	*data++ = pool_stats->recycle_stats.ring;
	*data++ = pool_stats->recycle_stats.ring_full;
	*data++ = pool_stats->recycle_stats.released_refcnt;

	return data;
}
EXPORT_SYMBOL(page_pool_ethtool_stats_get);

#else
#define alloc_stat_inc(pool, __stat)
#define recycle_stat_inc(pool, __stat)
#define recycle_stat_add(pool, __stat, val)
#endif

static bool page_pool_producer_lock(struct page_pool *pool)
	__acquires(&pool->ring.producer_lock)
{
	bool in_softirq = in_softirq();

	if (in_softirq)
		spin_lock(&pool->ring.producer_lock);
	else
		spin_lock_bh(&pool->ring.producer_lock);

	return in_softirq;
}

static void page_pool_producer_unlock(struct page_pool *pool,
				      bool in_softirq)
	__releases(&pool->ring.producer_lock)
{
	if (in_softirq)
		spin_unlock(&pool->ring.producer_lock);
	else
		spin_unlock_bh(&pool->ring.producer_lock);
}

static void page_pool_struct_check(void)
{
	CACHELINE_ASSERT_GROUP_MEMBER(struct page_pool, frag, frag_users);
	CACHELINE_ASSERT_GROUP_MEMBER(struct page_pool, frag, frag_page);
	CACHELINE_ASSERT_GROUP_MEMBER(struct page_pool, frag, frag_offset);
	CACHELINE_ASSERT_GROUP_SIZE(struct page_pool, frag,
				    PAGE_POOL_FRAG_GROUP_ALIGN);
}

static int page_pool_init(struct page_pool *pool,
			  const struct page_pool_params *params,
			  int cpuid)
{
	unsigned int ring_qsize = 1024; /* Default */
	struct netdev_rx_queue *rxq;
	int err;

	page_pool_struct_check();

	memcpy(&pool->p, &params->fast, sizeof(pool->p));
	memcpy(&pool->slow, &params->slow, sizeof(pool->slow));

	pool->cpuid = cpuid;
	pool->dma_sync_for_cpu = true;

	/* Validate only known flags were used */
	if (pool->slow.flags & ~PP_FLAG_ALL)
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
	if (pool->slow.flags & PP_FLAG_DMA_MAP) {
		if ((pool->p.dma_dir != DMA_FROM_DEVICE) &&
		    (pool->p.dma_dir != DMA_BIDIRECTIONAL))
			return -EINVAL;

		pool->dma_map = true;
	}

	if (pool->slow.flags & PP_FLAG_DMA_SYNC_DEV) {
		/* In order to request DMA-sync-for-device the page
		 * needs to be mapped
		 */
		if (!(pool->slow.flags & PP_FLAG_DMA_MAP))
			return -EINVAL;

		if (!pool->p.max_len)
			return -EINVAL;

		pool->dma_sync = true;

		/* pool->p.offset has to be set according to the address
		 * offset used by the DMA engine to start copying rx data
		 */
	}

	pool->has_init_callback = !!pool->slow.init_callback;

#ifdef CONFIG_PAGE_POOL_STATS
	if (!(pool->slow.flags & PP_FLAG_SYSTEM_POOL)) {
		pool->recycle_stats = alloc_percpu(struct page_pool_recycle_stats);
		if (!pool->recycle_stats)
			return -ENOMEM;
	} else {
		/* For system page pool instance we use a singular stats object
		 * instead of allocating a separate percpu variable for each
		 * (also percpu) page pool instance.
		 */
		pool->recycle_stats = &pp_system_recycle_stats;
		pool->system = true;
	}
#endif

	if (ptr_ring_init(&pool->ring, ring_qsize, GFP_KERNEL) < 0) {
#ifdef CONFIG_PAGE_POOL_STATS
		if (!pool->system)
			free_percpu(pool->recycle_stats);
#endif
		return -ENOMEM;
	}

	atomic_set(&pool->pages_state_release_cnt, 0);

	/* Driver calling page_pool_create() also call page_pool_destroy() */
	refcount_set(&pool->user_cnt, 1);

	if (pool->dma_map)
		get_device(pool->p.dev);

	if (pool->slow.flags & PP_FLAG_ALLOW_UNREADABLE_NETMEM) {
		/* We rely on rtnl_lock()ing to make sure netdev_rx_queue
		 * configuration doesn't change while we're initializing
		 * the page_pool.
		 */
		ASSERT_RTNL();
		rxq = __netif_get_rx_queue(pool->slow.netdev,
					   pool->slow.queue_idx);
		pool->mp_priv = rxq->mp_params.mp_priv;
	}

	if (pool->mp_priv) {
		if (!pool->dma_map || !pool->dma_sync)
			return -EOPNOTSUPP;

		err = mp_dmabuf_devmem_init(pool);
		if (err) {
			pr_warn("%s() mem-provider init failed %d\n", __func__,
				err);
			goto free_ptr_ring;
		}

		static_branch_inc(&page_pool_mem_providers);
	}

	return 0;

free_ptr_ring:
	ptr_ring_cleanup(&pool->ring, NULL);
#ifdef CONFIG_PAGE_POOL_STATS
	if (!pool->system)
		free_percpu(pool->recycle_stats);
#endif
	return err;
}

static void page_pool_uninit(struct page_pool *pool)
{
	ptr_ring_cleanup(&pool->ring, NULL);

	if (pool->dma_map)
		put_device(pool->p.dev);

#ifdef CONFIG_PAGE_POOL_STATS
	if (!pool->system)
		free_percpu(pool->recycle_stats);
#endif
}

/**
 * page_pool_create_percpu() - create a page pool for a given cpu.
 * @params: parameters, see struct page_pool_params
 * @cpuid: cpu identifier
 */
struct page_pool *
page_pool_create_percpu(const struct page_pool_params *params, int cpuid)
{
	struct page_pool *pool;
	int err;

	pool = kzalloc_node(sizeof(*pool), GFP_KERNEL, params->nid);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	err = page_pool_init(pool, params, cpuid);
	if (err < 0)
		goto err_free;

	err = page_pool_list(pool);
	if (err)
		goto err_uninit;

	return pool;

err_uninit:
	page_pool_uninit(pool);
err_free:
	pr_warn("%s() gave up with errno %d\n", __func__, err);
	kfree(pool);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(page_pool_create_percpu);

/**
 * page_pool_create() - create a page pool
 * @params: parameters, see struct page_pool_params
 */
struct page_pool *page_pool_create(const struct page_pool_params *params)
{
	return page_pool_create_percpu(params, -1);
}
EXPORT_SYMBOL(page_pool_create);

static void page_pool_return_page(struct page_pool *pool, netmem_ref netmem);

static noinline netmem_ref page_pool_refill_alloc_cache(struct page_pool *pool)
{
	struct ptr_ring *r = &pool->ring;
	netmem_ref netmem;
	int pref_nid; /* preferred NUMA node */

	/* Quicker fallback, avoid locks when ring is empty */
	if (__ptr_ring_empty(r)) {
		alloc_stat_inc(pool, empty);
		return 0;
	}

	/* Softirq guarantee CPU and thus NUMA node is stable. This,
	 * assumes CPU refilling driver RX-ring will also run RX-NAPI.
	 */
#ifdef CONFIG_NUMA
	pref_nid = (pool->p.nid == NUMA_NO_NODE) ? numa_mem_id() : pool->p.nid;
#else
	/* Ignore pool->p.nid setting if !CONFIG_NUMA, helps compiler */
	pref_nid = numa_mem_id(); /* will be zero like page_to_nid() */
#endif

	/* Refill alloc array, but only if NUMA match */
	do {
		netmem = (__force netmem_ref)__ptr_ring_consume(r);
		if (unlikely(!netmem))
			break;

		if (likely(netmem_is_pref_nid(netmem, pref_nid))) {
			pool->alloc.cache[pool->alloc.count++] = netmem;
		} else {
			/* NUMA mismatch;
			 * (1) release 1 page to page-allocator and
			 * (2) break out to fallthrough to alloc_pages_node.
			 * This limit stress on page buddy alloactor.
			 */
			page_pool_return_page(pool, netmem);
			alloc_stat_inc(pool, waive);
			netmem = 0;
			break;
		}
	} while (pool->alloc.count < PP_ALLOC_CACHE_REFILL);

	/* Return last page */
	if (likely(pool->alloc.count > 0)) {
		netmem = pool->alloc.cache[--pool->alloc.count];
		alloc_stat_inc(pool, refill);
	}

	return netmem;
}

/* fast path */
static netmem_ref __page_pool_get_cached(struct page_pool *pool)
{
	netmem_ref netmem;

	/* Caller MUST guarantee safe non-concurrent access, e.g. softirq */
	if (likely(pool->alloc.count)) {
		/* Fast-path */
		netmem = pool->alloc.cache[--pool->alloc.count];
		alloc_stat_inc(pool, fast);
	} else {
		netmem = page_pool_refill_alloc_cache(pool);
	}

	return netmem;
}

static void __page_pool_dma_sync_for_device(const struct page_pool *pool,
					    netmem_ref netmem,
					    u32 dma_sync_size)
{
#if defined(CONFIG_HAS_DMA) && defined(CONFIG_DMA_NEED_SYNC)
	dma_addr_t dma_addr = page_pool_get_dma_addr_netmem(netmem);

	dma_sync_size = min(dma_sync_size, pool->p.max_len);
	__dma_sync_single_for_device(pool->p.dev, dma_addr + pool->p.offset,
				     dma_sync_size, pool->p.dma_dir);
#endif
}

static __always_inline void
page_pool_dma_sync_for_device(const struct page_pool *pool,
			      netmem_ref netmem,
			      u32 dma_sync_size)
{
	if (pool->dma_sync && dma_dev_need_sync(pool->p.dev))
		__page_pool_dma_sync_for_device(pool, netmem, dma_sync_size);
}

static bool page_pool_dma_map(struct page_pool *pool, netmem_ref netmem)
{
	dma_addr_t dma;

	/* Setup DMA mapping: use 'struct page' area for storing DMA-addr
	 * since dma_addr_t can be either 32 or 64 bits and does not always fit
	 * into page private data (i.e 32bit cpu with 64bit DMA caps)
	 * This mapping is kept for lifetime of page, until leaving pool.
	 */
	dma = dma_map_page_attrs(pool->p.dev, netmem_to_page(netmem), 0,
				 (PAGE_SIZE << pool->p.order), pool->p.dma_dir,
				 DMA_ATTR_SKIP_CPU_SYNC |
					 DMA_ATTR_WEAK_ORDERING);
	if (dma_mapping_error(pool->p.dev, dma))
		return false;

	if (page_pool_set_dma_addr_netmem(netmem, dma))
		goto unmap_failed;

	page_pool_dma_sync_for_device(pool, netmem, pool->p.max_len);

	return true;

unmap_failed:
	WARN_ONCE(1, "unexpected DMA address, please report to netdev@");
	dma_unmap_page_attrs(pool->p.dev, dma,
			     PAGE_SIZE << pool->p.order, pool->p.dma_dir,
			     DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	return false;
}

static struct page *__page_pool_alloc_page_order(struct page_pool *pool,
						 gfp_t gfp)
{
	struct page *page;

	gfp |= __GFP_COMP;
	page = alloc_pages_node(pool->p.nid, gfp, pool->p.order);
	if (unlikely(!page))
		return NULL;

	if (pool->dma_map && unlikely(!page_pool_dma_map(pool, page_to_netmem(page)))) {
		put_page(page);
		return NULL;
	}

	alloc_stat_inc(pool, slow_high_order);
	page_pool_set_pp_info(pool, page_to_netmem(page));

	/* Track how many pages are held 'in-flight' */
	pool->pages_state_hold_cnt++;
	trace_page_pool_state_hold(pool, page_to_netmem(page),
				   pool->pages_state_hold_cnt);
	return page;
}

/* slow path */
static noinline netmem_ref __page_pool_alloc_pages_slow(struct page_pool *pool,
							gfp_t gfp)
{
	const int bulk = PP_ALLOC_CACHE_REFILL;
	unsigned int pp_order = pool->p.order;
	bool dma_map = pool->dma_map;
	netmem_ref netmem;
	int i, nr_pages;

	/* Don't support bulk alloc for high-order pages */
	if (unlikely(pp_order))
		return page_to_netmem(__page_pool_alloc_page_order(pool, gfp));

	/* Unnecessary as alloc cache is empty, but guarantees zero count */
	if (unlikely(pool->alloc.count > 0))
		return pool->alloc.cache[--pool->alloc.count];

	/* Mark empty alloc.cache slots "empty" for alloc_pages_bulk */
	memset(&pool->alloc.cache, 0, sizeof(void *) * bulk);

	nr_pages = alloc_pages_bulk_node(gfp, pool->p.nid, bulk,
					 (struct page **)pool->alloc.cache);
	if (unlikely(!nr_pages))
		return 0;

	/* Pages have been filled into alloc.cache array, but count is zero and
	 * page element have not been (possibly) DMA mapped.
	 */
	for (i = 0; i < nr_pages; i++) {
		netmem = pool->alloc.cache[i];
		if (dma_map && unlikely(!page_pool_dma_map(pool, netmem))) {
			put_page(netmem_to_page(netmem));
			continue;
		}

		page_pool_set_pp_info(pool, netmem);
		pool->alloc.cache[pool->alloc.count++] = netmem;
		/* Track how many pages are held 'in-flight' */
		pool->pages_state_hold_cnt++;
		trace_page_pool_state_hold(pool, netmem,
					   pool->pages_state_hold_cnt);
	}

	/* Return last page */
	if (likely(pool->alloc.count > 0)) {
		netmem = pool->alloc.cache[--pool->alloc.count];
		alloc_stat_inc(pool, slow);
	} else {
		netmem = 0;
	}

	/* When page just alloc'ed is should/must have refcnt 1. */
	return netmem;
}

/* For using page_pool replace: alloc_pages() API calls, but provide
 * synchronization guarantee for allocation side.
 */
netmem_ref page_pool_alloc_netmems(struct page_pool *pool, gfp_t gfp)
{
	netmem_ref netmem;

	/* Fast-path: Get a page from cache */
	netmem = __page_pool_get_cached(pool);
	if (netmem)
		return netmem;

	/* Slow-path: cache empty, do real allocation */
	if (static_branch_unlikely(&page_pool_mem_providers) && pool->mp_priv)
		netmem = mp_dmabuf_devmem_alloc_netmems(pool, gfp);
	else
		netmem = __page_pool_alloc_pages_slow(pool, gfp);
	return netmem;
}
EXPORT_SYMBOL(page_pool_alloc_netmems);
ALLOW_ERROR_INJECTION(page_pool_alloc_netmems, NULL);

struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp)
{
	return netmem_to_page(page_pool_alloc_netmems(pool, gfp));
}
EXPORT_SYMBOL(page_pool_alloc_pages);

/* Calculate distance between two u32 values, valid if distance is below 2^(31)
 *  https://en.wikipedia.org/wiki/Serial_number_arithmetic#General_Solution
 */
#define _distance(a, b)	(s32)((a) - (b))

s32 page_pool_inflight(const struct page_pool *pool, bool strict)
{
	u32 release_cnt = atomic_read(&pool->pages_state_release_cnt);
	u32 hold_cnt = READ_ONCE(pool->pages_state_hold_cnt);
	s32 inflight;

	inflight = _distance(hold_cnt, release_cnt);

	if (strict) {
		trace_page_pool_release(pool, inflight, hold_cnt, release_cnt);
		WARN(inflight < 0, "Negative(%d) inflight packet-pages",
		     inflight);
	} else {
		inflight = max(0, inflight);
	}

	return inflight;
}

void page_pool_set_pp_info(struct page_pool *pool, netmem_ref netmem)
{
	netmem_set_pp(netmem, pool);
	netmem_or_pp_magic(netmem, PP_SIGNATURE);

	/* Ensuring all pages have been split into one fragment initially:
	 * page_pool_set_pp_info() is only called once for every page when it
	 * is allocated from the page allocator and page_pool_fragment_page()
	 * is dirtying the same cache line as the page->pp_magic above, so
	 * the overhead is negligible.
	 */
	page_pool_fragment_netmem(netmem, 1);
	if (pool->has_init_callback)
		pool->slow.init_callback(netmem, pool->slow.init_arg);
}

void page_pool_clear_pp_info(netmem_ref netmem)
{
	netmem_clear_pp_magic(netmem);
	netmem_set_pp(netmem, NULL);
}

static __always_inline void __page_pool_release_page_dma(struct page_pool *pool,
							 netmem_ref netmem)
{
	dma_addr_t dma;

	if (!pool->dma_map)
		/* Always account for inflight pages, even if we didn't
		 * map them
		 */
		return;

	dma = page_pool_get_dma_addr_netmem(netmem);

	/* When page is unmapped, it cannot be returned to our pool */
	dma_unmap_page_attrs(pool->p.dev, dma,
			     PAGE_SIZE << pool->p.order, pool->p.dma_dir,
			     DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	page_pool_set_dma_addr_netmem(netmem, 0);
}

/* Disconnects a page (from a page_pool).  API users can have a need
 * to disconnect a page (from a page_pool), to allow it to be used as
 * a regular page (that will eventually be returned to the normal
 * page-allocator via put_page).
 */
void page_pool_return_page(struct page_pool *pool, netmem_ref netmem)
{
	int count;
	bool put;

	put = true;
	if (static_branch_unlikely(&page_pool_mem_providers) && pool->mp_priv)
		put = mp_dmabuf_devmem_release_page(pool, netmem);
	else
		__page_pool_release_page_dma(pool, netmem);

	/* This may be the last page returned, releasing the pool, so
	 * it is not safe to reference pool afterwards.
	 */
	count = atomic_inc_return_relaxed(&pool->pages_state_release_cnt);
	trace_page_pool_state_release(pool, netmem, count);

	if (put) {
		page_pool_clear_pp_info(netmem);
		put_page(netmem_to_page(netmem));
	}
	/* An optimization would be to call __free_pages(page, pool->p.order)
	 * knowing page is not part of page-cache (thus avoiding a
	 * __page_cache_release() call).
	 */
}

static bool page_pool_recycle_in_ring(struct page_pool *pool, netmem_ref netmem)
{
	int ret;
	/* BH protection not needed if current is softirq */
	if (in_softirq())
		ret = ptr_ring_produce(&pool->ring, (__force void *)netmem);
	else
		ret = ptr_ring_produce_bh(&pool->ring, (__force void *)netmem);

	if (!ret) {
		recycle_stat_inc(pool, ring);
		return true;
	}

	return false;
}

/* Only allow direct recycling in special circumstances, into the
 * alloc side cache.  E.g. during RX-NAPI processing for XDP_DROP use-case.
 *
 * Caller must provide appropriate safe context.
 */
static bool page_pool_recycle_in_cache(netmem_ref netmem,
				       struct page_pool *pool)
{
	if (unlikely(pool->alloc.count == PP_ALLOC_CACHE_SIZE)) {
		recycle_stat_inc(pool, cache_full);
		return false;
	}

	/* Caller MUST have verified/know (page_ref_count(page) == 1) */
	pool->alloc.cache[pool->alloc.count++] = netmem;
	recycle_stat_inc(pool, cached);
	return true;
}

static bool __page_pool_page_can_be_recycled(netmem_ref netmem)
{
	return netmem_is_net_iov(netmem) ||
	       (page_ref_count(netmem_to_page(netmem)) == 1 &&
		!page_is_pfmemalloc(netmem_to_page(netmem)));
}

/* If the page refcnt == 1, this will try to recycle the page.
 * If pool->dma_sync is set, we'll try to sync the DMA area for
 * the configured size min(dma_sync_size, pool->max_len).
 * If the page refcnt != 1, then the page will be returned to memory
 * subsystem.
 */
static __always_inline netmem_ref
__page_pool_put_page(struct page_pool *pool, netmem_ref netmem,
		     unsigned int dma_sync_size, bool allow_direct)
{
	lockdep_assert_no_hardirq();

	/* This allocator is optimized for the XDP mode that uses
	 * one-frame-per-page, but have fallbacks that act like the
	 * regular page allocator APIs.
	 *
	 * refcnt == 1 means page_pool owns page, and can recycle it.
	 *
	 * page is NOT reusable when allocated when system is under
	 * some pressure. (page_is_pfmemalloc)
	 */
	if (likely(__page_pool_page_can_be_recycled(netmem))) {
		/* Read barrier done in page_ref_count / READ_ONCE */

		page_pool_dma_sync_for_device(pool, netmem, dma_sync_size);

		if (allow_direct && page_pool_recycle_in_cache(netmem, pool))
			return 0;

		/* Page found as candidate for recycling */
		return netmem;
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
	recycle_stat_inc(pool, released_refcnt);
	page_pool_return_page(pool, netmem);

	return 0;
}

static bool page_pool_napi_local(const struct page_pool *pool)
{
	const struct napi_struct *napi;
	u32 cpuid;

	if (unlikely(!in_softirq()))
		return false;

	/* Allow direct recycle if we have reasons to believe that we are
	 * in the same context as the consumer would run, so there's
	 * no possible race.
	 * __page_pool_put_page() makes sure we're not in hardirq context
	 * and interrupts are enabled prior to accessing the cache.
	 */
	cpuid = smp_processor_id();
	if (READ_ONCE(pool->cpuid) == cpuid)
		return true;

	napi = READ_ONCE(pool->p.napi);

	return napi && READ_ONCE(napi->list_owner) == cpuid;
}

void page_pool_put_unrefed_netmem(struct page_pool *pool, netmem_ref netmem,
				  unsigned int dma_sync_size, bool allow_direct)
{
	if (!allow_direct)
		allow_direct = page_pool_napi_local(pool);

	netmem =
		__page_pool_put_page(pool, netmem, dma_sync_size, allow_direct);
	if (netmem && !page_pool_recycle_in_ring(pool, netmem)) {
		/* Cache full, fallback to free pages */
		recycle_stat_inc(pool, ring_full);
		page_pool_return_page(pool, netmem);
	}
}
EXPORT_SYMBOL(page_pool_put_unrefed_netmem);

void page_pool_put_unrefed_page(struct page_pool *pool, struct page *page,
				unsigned int dma_sync_size, bool allow_direct)
{
	page_pool_put_unrefed_netmem(pool, page_to_netmem(page), dma_sync_size,
				     allow_direct);
}
EXPORT_SYMBOL(page_pool_put_unrefed_page);

static void page_pool_recycle_ring_bulk(struct page_pool *pool,
					netmem_ref *bulk,
					u32 bulk_len)
{
	bool in_softirq;
	u32 i;

	/* Bulk produce into ptr_ring page_pool cache */
	in_softirq = page_pool_producer_lock(pool);

	for (i = 0; i < bulk_len; i++) {
		if (__ptr_ring_produce(&pool->ring, (__force void *)bulk[i])) {
			/* ring full */
			recycle_stat_inc(pool, ring_full);
			break;
		}
	}

	page_pool_producer_unlock(pool, in_softirq);
	recycle_stat_add(pool, ring, i);

	/* Hopefully all pages were returned into ptr_ring */
	if (likely(i == bulk_len))
		return;

	/*
	 * ptr_ring cache is full, free remaining pages outside producer lock
	 * since put_page() with refcnt == 1 can be an expensive operation.
	 */
	for (; i < bulk_len; i++)
		page_pool_return_page(pool, bulk[i]);
}

/**
 * page_pool_put_netmem_bulk() - release references on multiple netmems
 * @data:	array holding netmem references
 * @count:	number of entries in @data
 *
 * Tries to refill a number of netmems into the ptr_ring cache holding ptr_ring
 * producer lock. If the ptr_ring is full, page_pool_put_netmem_bulk()
 * will release leftover netmems to the memory provider.
 * page_pool_put_netmem_bulk() is suitable to be run inside the driver NAPI tx
 * completion loop for the XDP_REDIRECT use case.
 *
 * Please note the caller must not use data area after running
 * page_pool_put_netmem_bulk(), as this function overwrites it.
 */
void page_pool_put_netmem_bulk(netmem_ref *data, u32 count)
{
	u32 bulk_len = 0;

	for (u32 i = 0; i < count; i++) {
		netmem_ref netmem = netmem_compound_head(data[i]);

		if (page_pool_unref_and_test(netmem))
			data[bulk_len++] = netmem;
	}

	count = bulk_len;
	while (count) {
		netmem_ref bulk[XDP_BULK_QUEUE_SIZE];
		struct page_pool *pool = NULL;
		bool allow_direct;
		u32 foreign = 0;

		bulk_len = 0;

		for (u32 i = 0; i < count; i++) {
			struct page_pool *netmem_pp;
			netmem_ref netmem = data[i];

			netmem_pp = netmem_get_pp(netmem);
			if (unlikely(!pool)) {
				pool = netmem_pp;
				allow_direct = page_pool_napi_local(pool);
			} else if (netmem_pp != pool) {
				/*
				 * If the netmem belongs to a different
				 * page_pool, save it for another round.
				 */
				data[foreign++] = netmem;
				continue;
			}

			netmem = __page_pool_put_page(pool, netmem, -1,
						      allow_direct);
			/* Approved for bulk recycling in ptr_ring cache */
			if (netmem)
				bulk[bulk_len++] = netmem;
		}

		if (bulk_len)
			page_pool_recycle_ring_bulk(pool, bulk, bulk_len);

		count = foreign;
	}
}
EXPORT_SYMBOL(page_pool_put_netmem_bulk);

static netmem_ref page_pool_drain_frag(struct page_pool *pool,
				       netmem_ref netmem)
{
	long drain_count = BIAS_MAX - pool->frag_users;

	/* Some user is still using the page frag */
	if (likely(page_pool_unref_netmem(netmem, drain_count)))
		return 0;

	if (__page_pool_page_can_be_recycled(netmem)) {
		page_pool_dma_sync_for_device(pool, netmem, -1);
		return netmem;
	}

	page_pool_return_page(pool, netmem);
	return 0;
}

static void page_pool_free_frag(struct page_pool *pool)
{
	long drain_count = BIAS_MAX - pool->frag_users;
	netmem_ref netmem = pool->frag_page;

	pool->frag_page = 0;

	if (!netmem || page_pool_unref_netmem(netmem, drain_count))
		return;

	page_pool_return_page(pool, netmem);
}

netmem_ref page_pool_alloc_frag_netmem(struct page_pool *pool,
				       unsigned int *offset, unsigned int size,
				       gfp_t gfp)
{
	unsigned int max_size = PAGE_SIZE << pool->p.order;
	netmem_ref netmem = pool->frag_page;

	if (WARN_ON(size > max_size))
		return 0;

	size = ALIGN(size, dma_get_cache_alignment());
	*offset = pool->frag_offset;

	if (netmem && *offset + size > max_size) {
		netmem = page_pool_drain_frag(pool, netmem);
		if (netmem) {
			recycle_stat_inc(pool, cached);
			alloc_stat_inc(pool, fast);
			goto frag_reset;
		}
	}

	if (!netmem) {
		netmem = page_pool_alloc_netmems(pool, gfp);
		if (unlikely(!netmem)) {
			pool->frag_page = 0;
			return 0;
		}

		pool->frag_page = netmem;

frag_reset:
		pool->frag_users = 1;
		*offset = 0;
		pool->frag_offset = size;
		page_pool_fragment_netmem(netmem, BIAS_MAX);
		return netmem;
	}

	pool->frag_users++;
	pool->frag_offset = *offset + size;
	return netmem;
}
EXPORT_SYMBOL(page_pool_alloc_frag_netmem);

struct page *page_pool_alloc_frag(struct page_pool *pool, unsigned int *offset,
				  unsigned int size, gfp_t gfp)
{
	return netmem_to_page(page_pool_alloc_frag_netmem(pool, offset, size,
							  gfp));
}
EXPORT_SYMBOL(page_pool_alloc_frag);

static void page_pool_empty_ring(struct page_pool *pool)
{
	netmem_ref netmem;

	/* Empty recycle ring */
	while ((netmem = (__force netmem_ref)ptr_ring_consume_bh(&pool->ring))) {
		/* Verify the refcnt invariant of cached pages */
		if (!(netmem_ref_count(netmem) == 1))
			pr_crit("%s() page_pool refcnt %d violation\n",
				__func__, netmem_ref_count(netmem));

		page_pool_return_page(pool, netmem);
	}
}

static void __page_pool_destroy(struct page_pool *pool)
{
	if (pool->disconnect)
		pool->disconnect(pool);

	page_pool_unlist(pool);
	page_pool_uninit(pool);

	if (pool->mp_priv) {
		mp_dmabuf_devmem_destroy(pool);
		static_branch_dec(&page_pool_mem_providers);
	}

	kfree(pool);
}

static void page_pool_empty_alloc_cache_once(struct page_pool *pool)
{
	netmem_ref netmem;

	if (pool->destroy_cnt)
		return;

	/* Empty alloc cache, assume caller made sure this is
	 * no-longer in use, and page_pool_alloc_pages() cannot be
	 * call concurrently.
	 */
	while (pool->alloc.count) {
		netmem = pool->alloc.cache[--pool->alloc.count];
		page_pool_return_page(pool, netmem);
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
	inflight = page_pool_inflight(pool, true);
	if (!inflight)
		__page_pool_destroy(pool);

	return inflight;
}

static void page_pool_release_retry(struct work_struct *wq)
{
	struct delayed_work *dwq = to_delayed_work(wq);
	struct page_pool *pool = container_of(dwq, typeof(*pool), release_dw);
	void *netdev;
	int inflight;

	inflight = page_pool_release(pool);
	if (!inflight)
		return;

	/* Periodic warning for page pools the user can't see */
	netdev = READ_ONCE(pool->slow.netdev);
	if (time_after_eq(jiffies, pool->defer_warn) &&
	    (!netdev || netdev == NET_PTR_POISON)) {
		int sec = (s32)((u32)jiffies - (u32)pool->defer_start) / HZ;

		pr_warn("%s() stalled pool shutdown: id %u, %d inflight %d sec\n",
			__func__, pool->user.id, inflight, sec);
		pool->defer_warn = jiffies + DEFER_WARN_INTERVAL;
	}

	/* Still not ready to be disconnected, retry later */
	schedule_delayed_work(&pool->release_dw, DEFER_TIME);
}

void page_pool_use_xdp_mem(struct page_pool *pool, void (*disconnect)(void *),
			   const struct xdp_mem_info *mem)
{
	refcount_inc(&pool->user_cnt);
	pool->disconnect = disconnect;
	pool->xdp_mem_id = mem->id;
}

void page_pool_disable_direct_recycling(struct page_pool *pool)
{
	/* Disable direct recycling based on pool->cpuid.
	 * Paired with READ_ONCE() in page_pool_napi_local().
	 */
	WRITE_ONCE(pool->cpuid, -1);

	if (!pool->p.napi)
		return;

	/* To avoid races with recycling and additional barriers make sure
	 * pool and NAPI are unlinked when NAPI is disabled.
	 */
	WARN_ON(!test_bit(NAPI_STATE_SCHED, &pool->p.napi->state));
	WARN_ON(READ_ONCE(pool->p.napi->list_owner) != -1);

	mutex_lock(&page_pools_lock);
	WRITE_ONCE(pool->p.napi, NULL);
	mutex_unlock(&page_pools_lock);
}
EXPORT_SYMBOL(page_pool_disable_direct_recycling);

void page_pool_destroy(struct page_pool *pool)
{
	if (!pool)
		return;

	if (!page_pool_put(pool))
		return;

	page_pool_disable_direct_recycling(pool);
	page_pool_free_frag(pool);

	if (!page_pool_release(pool))
		return;

	page_pool_detached(pool);
	pool->defer_start = jiffies;
	pool->defer_warn  = jiffies + DEFER_WARN_INTERVAL;

	INIT_DELAYED_WORK(&pool->release_dw, page_pool_release_retry);
	schedule_delayed_work(&pool->release_dw, DEFER_TIME);
}
EXPORT_SYMBOL(page_pool_destroy);

/* Caller must provide appropriate safe context, e.g. NAPI. */
void page_pool_update_nid(struct page_pool *pool, int new_nid)
{
	netmem_ref netmem;

	trace_page_pool_update_nid(pool, new_nid);
	pool->p.nid = new_nid;

	/* Flush pool alloc cache, as refill will check NUMA node */
	while (pool->alloc.count) {
		netmem = pool->alloc.cache[--pool->alloc.count];
		page_pool_return_page(pool, netmem);
	}
}
EXPORT_SYMBOL(page_pool_update_nid);
