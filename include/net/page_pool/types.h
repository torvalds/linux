/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _NET_PAGE_POOL_TYPES_H
#define _NET_PAGE_POOL_TYPES_H

#include <linux/dma-direction.h>
#include <linux/ptr_ring.h>
#include <linux/types.h>
#include <net/netmem.h>

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
#define PP_FLAG_SYSTEM_POOL	BIT(2) /* Global system page_pool */

/* Allow unreadable (net_iov backed) netmem in this page_pool. Drivers setting
 * this must be able to support unreadable netmem, where netmem_address() would
 * return NULL. This flag should not be set for header page_pools.
 *
 * If the driver sets PP_FLAG_ALLOW_UNREADABLE_NETMEM, it should also set
 * page_pool_params.slow.queue_idx.
 */
#define PP_FLAG_ALLOW_UNREADABLE_NETMEM BIT(3)

#define PP_FLAG_ALL		(PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV | \
				 PP_FLAG_SYSTEM_POOL | PP_FLAG_ALLOW_UNREADABLE_NETMEM)

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
	netmem_ref cache[PP_ALLOC_CACHE_SIZE];
};

/**
 * struct page_pool_params - page pool parameters
 * @fast:	params accessed frequently on hotpath
 * @order:	2^order pages on allocation
 * @pool_size:	size of the ptr_ring
 * @nid:	NUMA node id to allocate from pages from
 * @dev:	device, for DMA pre-mapping purposes
 * @napi:	NAPI which is the sole consumer of pages, otherwise NULL
 * @dma_dir:	DMA mapping direction
 * @max_len:	max DMA sync memory size for PP_FLAG_DMA_SYNC_DEV
 * @offset:	DMA sync address offset for PP_FLAG_DMA_SYNC_DEV
 * @slow:	params with slowpath access only (initialization and Netlink)
 * @netdev:	netdev this pool will serve (leave as NULL if none or multiple)
 * @queue_idx:	queue idx this page_pool is being created for.
 * @flags:	PP_FLAG_DMA_MAP, PP_FLAG_DMA_SYNC_DEV, PP_FLAG_SYSTEM_POOL,
 *		PP_FLAG_ALLOW_UNREADABLE_NETMEM.
 */
struct page_pool_params {
	struct_group_tagged(page_pool_params_fast, fast,
		unsigned int	order;
		unsigned int	pool_size;
		int		nid;
		struct device	*dev;
		struct napi_struct *napi;
		enum dma_data_direction dma_dir;
		unsigned int	max_len;
		unsigned int	offset;
	);
	struct_group_tagged(page_pool_params_slow, slow,
		struct net_device *netdev;
		unsigned int queue_idx;
		unsigned int	flags;
/* private: used by test code only */
		void (*init_callback)(netmem_ref netmem, void *arg);
		void *init_arg;
	);
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
#endif

/* The whole frag API block must stay within one cacheline. On 32-bit systems,
 * sizeof(long) == sizeof(int), so that the block size is ``3 * sizeof(long)``.
 * On 64-bit systems, the actual size is ``2 * sizeof(long) + sizeof(int)``.
 * The closest pow-2 to both of them is ``4 * sizeof(long)``, so just use that
 * one for simplicity.
 * Having it aligned to a cacheline boundary may be excessive and doesn't bring
 * any good.
 */
#define PAGE_POOL_FRAG_GROUP_ALIGN	(4 * sizeof(long))

struct pp_memory_provider_params {
	void *mp_priv;
};

struct page_pool {
	struct page_pool_params_fast p;

	int cpuid;
	u32 pages_state_hold_cnt;

	bool has_init_callback:1;	/* slow::init_callback is set */
	bool dma_map:1;			/* Perform DMA mapping */
	bool dma_sync:1;		/* Perform DMA sync for device */
	bool dma_sync_for_cpu:1;	/* Perform DMA sync for cpu */
#ifdef CONFIG_PAGE_POOL_STATS
	bool system:1;			/* This is a global percpu pool */
#endif

	__cacheline_group_begin_aligned(frag, PAGE_POOL_FRAG_GROUP_ALIGN);
	long frag_users;
	netmem_ref frag_page;
	unsigned int frag_offset;
	__cacheline_group_end_aligned(frag, PAGE_POOL_FRAG_GROUP_ALIGN);

	struct delayed_work release_dw;
	void (*disconnect)(void *pool);
	unsigned long defer_start;
	unsigned long defer_warn;

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
	 * efficiently, it a way that doesn't bounce cache-lines.
	 *
	 * TODO: Implement bulk return pages into this structure.
	 */
	struct ptr_ring ring;

	void *mp_priv;

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

	/* Slow/Control-path information follows */
	struct page_pool_params_slow slow;
	/* User-facing fields, protected by page_pools_lock */
	struct {
		struct hlist_node list;
		u64 detach_time;
		u32 napi_id;
		u32 id;
	} user;
};

struct page *page_pool_alloc_pages(struct page_pool *pool, gfp_t gfp);
netmem_ref page_pool_alloc_netmems(struct page_pool *pool, gfp_t gfp);
struct page *page_pool_alloc_frag(struct page_pool *pool, unsigned int *offset,
				  unsigned int size, gfp_t gfp);
netmem_ref page_pool_alloc_frag_netmem(struct page_pool *pool,
				       unsigned int *offset, unsigned int size,
				       gfp_t gfp);
struct page_pool *page_pool_create(const struct page_pool_params *params);
struct page_pool *page_pool_create_percpu(const struct page_pool_params *params,
					  int cpuid);

struct xdp_mem_info;

#ifdef CONFIG_PAGE_POOL
void page_pool_disable_direct_recycling(struct page_pool *pool);
void page_pool_destroy(struct page_pool *pool);
void page_pool_use_xdp_mem(struct page_pool *pool, void (*disconnect)(void *),
			   const struct xdp_mem_info *mem);
void page_pool_put_netmem_bulk(netmem_ref *data, u32 count);
#else
static inline void page_pool_destroy(struct page_pool *pool)
{
}

static inline void page_pool_use_xdp_mem(struct page_pool *pool,
					 void (*disconnect)(void *),
					 const struct xdp_mem_info *mem)
{
}

static inline void page_pool_put_netmem_bulk(netmem_ref *data, u32 count)
{
}
#endif

void page_pool_put_unrefed_netmem(struct page_pool *pool, netmem_ref netmem,
				  unsigned int dma_sync_size,
				  bool allow_direct);
void page_pool_put_unrefed_page(struct page_pool *pool, struct page *page,
				unsigned int dma_sync_size,
				bool allow_direct);

static inline bool is_page_pool_compiled_in(void)
{
#ifdef CONFIG_PAGE_POOL
	return true;
#else
	return false;
#endif
}

/* Caller must provide appropriate safe context, e.g. NAPI. */
void page_pool_update_nid(struct page_pool *pool, int new_nid);

#endif /* _NET_PAGE_POOL_H */
