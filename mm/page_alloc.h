/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mm-internal API for the page (buddy) allocator. Public API lives in
 * include/linux/gfp.h.
 */
#ifndef __MM_PAGE_ALLOC_H
#define __MM_PAGE_ALLOC_H

#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/nodemask.h>
#include <linux/types.h>

#define ALLOC_DEFAULT		0
/* The ALLOC_WMARK bits are used as an index to zone->watermark */
#define ALLOC_WMARK_MIN		WMARK_MIN
#define ALLOC_WMARK_LOW		WMARK_LOW
#define ALLOC_WMARK_HIGH	WMARK_HIGH
#define ALLOC_NO_WATERMARKS	0x04 /* don't check watermarks at all */

/* Mask to get the watermark bits */
#define ALLOC_WMARK_MASK	(ALLOC_NO_WATERMARKS-1)

/*
 * Only MMU archs have async oom victim reclaim - aka oom_reaper so we
 * cannot assume a reduced access to memory reserves is sufficient for
 * !MMU
 */
#ifdef CONFIG_MMU
#define ALLOC_OOM		0x08
#else
#define ALLOC_OOM		ALLOC_NO_WATERMARKS
#endif

#define ALLOC_NON_BLOCK		 0x10 /* Caller cannot block. Allow access
				       * to 25% of the min watermark or
				       * 62.5% if __GFP_HIGH is set.
				       */
#define ALLOC_MIN_RESERVE	 0x20 /* __GFP_HIGH set. Allow access to 50%
				       * of the min watermark.
				       */
#define ALLOC_CPUSET		 0x40 /* check for correct cpuset */
#define ALLOC_CMA		 0x80 /* allow allocations from CMA areas */
#ifdef CONFIG_ZONE_DMA32
#define ALLOC_NOFRAGMENT	0x100 /* avoid mixing pageblock types */
#else
#define ALLOC_NOFRAGMENT	  0x0
#endif
#define ALLOC_HIGHATOMIC	0x200 /* Allows access to MIGRATE_HIGHATOMIC */
#define ALLOC_NOLOCK		0x400 /* Only use spin_trylock in allocation path */
#define ALLOC_KSWAPD		0x800 /* allow waking of kswapd, __GFP_KSWAPD_RECLAIM set */

/* Flags that allow allocations below the min watermark. */
#define ALLOC_RESERVES (ALLOC_NON_BLOCK|ALLOC_MIN_RESERVE|ALLOC_HIGHATOMIC|ALLOC_OOM)

/*
 * Structure for holding the mostly immutable allocation parameters passed
 * between functions involved in allocations, including the alloc_pages*
 * family of functions.
 *
 * nodemask, migratetype and highest_zoneidx are initialized only once in
 * __alloc_pages() and then never change.
 *
 * zonelist, preferred_zone and highest_zoneidx are set first in
 * __alloc_pages() for the fast path, and might be later changed
 * in __alloc_pages_slowpath(). All other functions pass the whole structure
 * by a const pointer.
 */
struct alloc_context {
	struct zonelist *zonelist;
	const nodemask_t *nodemask;
	struct zoneref *preferred_zoneref;
	int migratetype;

	/*
	 * highest_zoneidx represents highest usable zone index of
	 * the allocation request. Due to the nature of the zone,
	 * memory on lower zone than the highest_zoneidx will be
	 * protected by lowmem_reserve[highest_zoneidx].
	 *
	 * highest_zoneidx is also used by reclaim/compaction to limit
	 * the target zone since higher zone than this index cannot be
	 * usable for this allocation request.
	 */
	enum zone_type highest_zoneidx;
	bool spread_dirty_pages;
};

/*
 * This function returns the order of a free page in the buddy system. In
 * general, page_zone(page)->lock must be held by the caller to prevent the
 * page from being allocated in parallel and returning garbage as the order.
 * If a caller does not hold page_zone(page)->lock, it must guarantee that the
 * page cannot be allocated or merged in parallel. Alternatively, it must
 * handle invalid values gracefully, and use buddy_order_unsafe() below.
 */
static inline unsigned int buddy_order(struct page *page)
{
	/* PageBuddy() must be checked by the caller */
	return page_private(page);
}

/*
 * Like buddy_order(), but for callers who cannot afford to hold the zone lock.
 * PageBuddy() should be checked first by the caller to minimize race window,
 * and invalid values must be handled gracefully.
 *
 * READ_ONCE is used so that if the caller assigns the result into a local
 * variable and e.g. tests it for valid range before using, the compiler cannot
 * decide to remove the variable and inline the page_private(page) multiple
 * times, potentially observing different values in the tests and the actual
 * use of the result.
 */
#define buddy_order_unsafe(page)	READ_ONCE(page_private(page))

/*
 * This function checks whether a page is free && is the buddy
 * we can coalesce a page and its buddy if
 * (a) the buddy is not in a hole (check before calling!) &&
 * (b) the buddy is in the buddy system &&
 * (c) a page and its buddy have the same order &&
 * (d) a page and its buddy are in the same zone.
 *
 * For recording whether a page is in the buddy system, we set PageBuddy.
 * Setting, clearing, and testing PageBuddy is serialized by zone->lock.
 *
 * For recording page's order, we use page_private(page).
 */
static inline bool page_is_buddy(struct page *page, struct page *buddy,
				 unsigned int order)
{
	if (!page_is_guard(buddy) && !PageBuddy(buddy))
		return false;

	if (buddy_order(buddy) != order)
		return false;

	/*
	 * zone check is done late to avoid uselessly calculating
	 * zone/node ids for pages that could never merge.
	 */
	if (page_zone_id(page) != page_zone_id(buddy))
		return false;

	VM_BUG_ON_PAGE(page_count(buddy) != 0, buddy);

	return true;
}

/*
 * Locate the struct page for both the matching buddy in our
 * pair (buddy1) and the combined O(n+1) page they form (page).
 *
 * 1) Any buddy B1 will have an order O twin B2 which satisfies
 * the following equation:
 *     B2 = B1 ^ (1 << O)
 * For example, if the starting buddy (buddy2) is #8 its order
 * 1 buddy is #10:
 *     B2 = 8 ^ (1 << 1) = 8 ^ 2 = 10
 *
 * 2) Any buddy B will have an order O+1 parent P which
 * satisfies the following equation:
 *     P = B & ~(1 << O)
 *
 * Assumption: *_mem_map is contiguous at least up to MAX_PAGE_ORDER
 */
static inline unsigned long
__find_buddy_pfn(unsigned long page_pfn, unsigned int order)
{
	return page_pfn ^ (1 << order);
}

/*
 * Find the buddy of @page and validate it.
 * @page: The input page
 * @pfn: The pfn of the page, it saves a call to page_to_pfn() when the
 *       function is used in the performance-critical __free_one_page().
 * @order: The order of the page
 * @buddy_pfn: The output pointer to the buddy pfn, it also saves a call to
 *             page_to_pfn().
 *
 * The found buddy can be a non PageBuddy, out of @page's zone, or its order is
 * not the same as @page. The validation is necessary before use it.
 *
 * Return: the found buddy page or NULL if not found.
 */
static inline struct page *find_buddy_page_pfn(struct page *page,
			unsigned long pfn, unsigned int order, unsigned long *buddy_pfn)
{
	unsigned long __buddy_pfn = __find_buddy_pfn(pfn, order);
	struct page *buddy;

	buddy = page + (__buddy_pfn - pfn);
	if (buddy_pfn)
		*buddy_pfn = __buddy_pfn;

	if (page_is_buddy(page, buddy, order))
		return buddy;
	return NULL;
}

extern struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone);

static inline struct page *pageblock_pfn_to_page(unsigned long start_pfn,
				unsigned long end_pfn, struct zone *zone)
{
	if (zone->contiguous)
		return pfn_to_page(start_pfn);

	return __pageblock_pfn_to_page(start_pfn, end_pfn, zone);
}

extern void __free_pages_core(struct page *page, unsigned int order,
		enum meminit_context context);

void post_alloc_hook(struct page *page, unsigned int order, gfp_t gfp_flags);
extern bool free_pages_prepare(struct page *page, unsigned int order);

extern int user_min_free_kbytes;

struct page *__alloc_frozen_pages_noprof(gfp_t gfp, unsigned int order, int nid,
		nodemask_t *nodemask, unsigned int alloc_flags);
#define __alloc_frozen_pages(...) \
	alloc_hooks(__alloc_frozen_pages_noprof(__VA_ARGS__))
void free_frozen_pages(struct page *page, unsigned int order);
void free_unref_folios(struct folio_batch *fbatch);

#ifdef CONFIG_NUMA
struct page *alloc_frozen_pages_noprof(gfp_t, unsigned int order);
#else
static inline struct page *alloc_frozen_pages_noprof(gfp_t gfp, unsigned int order)
{
	return __alloc_frozen_pages_noprof(gfp, order, numa_node_id(), NULL,
					   ALLOC_DEFAULT);
}
#endif

#define alloc_frozen_pages(...) \
	alloc_hooks(alloc_frozen_pages_noprof(__VA_ARGS__))

struct page *alloc_frozen_pages_nolock_noprof(gfp_t gfp_flags, int nid, unsigned int order);
#define alloc_frozen_pages_nolock(...) \
	alloc_hooks(alloc_frozen_pages_nolock_noprof(__VA_ARGS__))
void free_frozen_pages_nolock(struct page *page, unsigned int order);

extern void zone_pcp_reset(struct zone *zone);
extern void zone_pcp_disable(struct zone *zone);
extern void zone_pcp_enable(struct zone *zone);
extern void zone_pcp_init(struct zone *zone);

enum fallback_result {
	/* Found suitable migratetype, *mt_out is valid. */
	FALLBACK_FOUND,
	/* No fallback found in requested order. */
	FALLBACK_EMPTY,
	/* Passed @claimable, but claiming whole block is a bad idea. */
	FALLBACK_NOCLAIM,
};
enum fallback_result
find_suitable_fallback(struct free_area *area, unsigned int order,
		       int migratetype, bool claimable, int *mt_out);

static inline bool free_area_empty(struct free_area *area, int migratetype)
{
	return list_empty(&area->free_list[migratetype]);
}

/* Convert GFP flags to their corresponding migrate type */
#define GFP_MOVABLE_MASK (__GFP_RECLAIMABLE|__GFP_MOVABLE)
#define GFP_MOVABLE_SHIFT 3

static inline int gfp_migratetype(const gfp_t gfp_flags)
{
	VM_WARN_ON((gfp_flags & GFP_MOVABLE_MASK) == GFP_MOVABLE_MASK);
	BUILD_BUG_ON((1UL << GFP_MOVABLE_SHIFT) != ___GFP_MOVABLE);
	BUILD_BUG_ON((___GFP_MOVABLE >> GFP_MOVABLE_SHIFT) != MIGRATE_MOVABLE);
	BUILD_BUG_ON((___GFP_RECLAIMABLE >> GFP_MOVABLE_SHIFT) != MIGRATE_RECLAIMABLE);
	BUILD_BUG_ON(((___GFP_MOVABLE | ___GFP_RECLAIMABLE) >>
		      GFP_MOVABLE_SHIFT) != MIGRATE_HIGHATOMIC);

	if (unlikely(page_group_by_mobility_disabled))
		return MIGRATE_UNMOVABLE;

	/* Group based on mobility */
	return (__force unsigned long)(gfp_flags & GFP_MOVABLE_MASK) >> GFP_MOVABLE_SHIFT;
}
#undef GFP_MOVABLE_MASK
#undef GFP_MOVABLE_SHIFT

bool decay_pcp_high(struct zone *zone, struct per_cpu_pages *pcp);
void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp);
void drain_all_pages(struct zone *zone);

void page_alloc_init_cpuhp(void);
void page_alloc_sysctl_init(void);

#endif /* __MM_PAGE_ALLOC_H */
