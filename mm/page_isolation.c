// SPDX-License-Identifier: GPL-2.0
/*
 * linux/mm/page_isolation.c
 */

#include <linux/mm.h>
#include <linux/page-isolation.h>
#include <linux/pageblock-flags.h>
#include <linux/memory.h>
#include <linux/hugetlb.h>
#include <linux/page_owner.h>
#include <linux/migrate.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/page_isolation.h>

/*
 * This function checks whether pageblock includes unmovable pages or not.
 *
 * PageLRU check without isolation or lru_lock could race so that
 * MIGRATE_MOVABLE block might include unmovable pages. And __PageMovable
 * check without lock_page also may miss some movable non-lru pages at
 * race condition. So you can't expect this function should be exact.
 *
 * Returns a page without holding a reference. If the caller wants to
 * dereference that page (e.g., dumping), it has to make sure that it
 * cannot get removed (e.g., via memory unplug) concurrently.
 *
 */
static struct page *has_unmovable_pages(struct zone *zone, struct page *page,
				 int migratetype, int flags)
{
	unsigned long iter = 0;
	unsigned long pfn = page_to_pfn(page);
	unsigned long offset = pfn % pageblock_nr_pages;

	if (is_migrate_cma_page(page)) {
		/*
		 * CMA allocations (alloc_contig_range) really need to mark
		 * isolate CMA pageblocks even when they are not movable in fact
		 * so consider them movable here.
		 */
		if (is_migrate_cma(migratetype))
			return NULL;

		return page;
	}

	for (; iter < pageblock_nr_pages - offset; iter++) {
		page = pfn_to_page(pfn + iter);

		/*
		 * Both, bootmem allocations and memory holes are marked
		 * PG_reserved and are unmovable. We can even have unmovable
		 * allocations inside ZONE_MOVABLE, for example when
		 * specifying "movablecore".
		 */
		if (PageReserved(page))
			return page;

		/*
		 * If the zone is movable and we have ruled out all reserved
		 * pages then it should be reasonably safe to assume the rest
		 * is movable.
		 */
		if (zone_idx(zone) == ZONE_MOVABLE)
			continue;

		/*
		 * Hugepages are not in LRU lists, but they're movable.
		 * THPs are on the LRU, but need to be counted as #small pages.
		 * We need not scan over tail pages because we don't
		 * handle each tail page individually in migration.
		 */
		if (PageHuge(page) || PageTransCompound(page)) {
			struct page *head = compound_head(page);
			unsigned int skip_pages;

			if (PageHuge(page)) {
				if (!hugepage_migration_supported(page_hstate(head)))
					return page;
			} else if (!PageLRU(head) && !__PageMovable(head)) {
				return page;
			}

			skip_pages = compound_nr(head) - (page - head);
			iter += skip_pages - 1;
			continue;
		}

		/*
		 * We can't use page_count without pin a page
		 * because another CPU can free compound page.
		 * This check already skips compound tails of THP
		 * because their page->_refcount is zero at all time.
		 */
		if (!page_ref_count(page)) {
			if (PageBuddy(page))
				iter += (1 << buddy_order(page)) - 1;
			continue;
		}

		/*
		 * The HWPoisoned page may be not in buddy system, and
		 * page_count() is not 0.
		 */
		if ((flags & MEMORY_OFFLINE) && PageHWPoison(page))
			continue;

		/*
		 * We treat all PageOffline() pages as movable when offlining
		 * to give drivers a chance to decrement their reference count
		 * in MEM_GOING_OFFLINE in order to indicate that these pages
		 * can be offlined as there are no direct references anymore.
		 * For actually unmovable PageOffline() where the driver does
		 * not support this, we will fail later when trying to actually
		 * move these pages that still have a reference count > 0.
		 * (false negatives in this function only)
		 */
		if ((flags & MEMORY_OFFLINE) && PageOffline(page))
			continue;

		if (__PageMovable(page) || PageLRU(page))
			continue;

		/*
		 * If there are RECLAIMABLE pages, we need to check
		 * it.  But now, memory offline itself doesn't call
		 * shrink_node_slabs() and it still to be fixed.
		 */
		return page;
	}
	return NULL;
}

static int set_migratetype_isolate(struct page *page, int migratetype, int isol_flags)
{
	struct zone *zone = page_zone(page);
	struct page *unmovable;
	unsigned long flags;

	spin_lock_irqsave(&zone->lock, flags);

	/*
	 * We assume the caller intended to SET migrate type to isolate.
	 * If it is already set, then someone else must have raced and
	 * set it before us.
	 */
	if (is_migrate_isolate_page(page)) {
		spin_unlock_irqrestore(&zone->lock, flags);
		return -EBUSY;
	}

	/*
	 * FIXME: Now, memory hotplug doesn't call shrink_slab() by itself.
	 * We just check MOVABLE pages.
	 */
	unmovable = has_unmovable_pages(zone, page, migratetype, isol_flags);
	if (!unmovable) {
		unsigned long nr_pages;
		int mt = get_pageblock_migratetype(page);

		set_pageblock_migratetype(page, MIGRATE_ISOLATE);
		zone->nr_isolate_pageblock++;
		nr_pages = move_freepages_block(zone, page, MIGRATE_ISOLATE,
									NULL);

		__mod_zone_freepage_state(zone, -nr_pages, mt);
		spin_unlock_irqrestore(&zone->lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&zone->lock, flags);
	if (isol_flags & REPORT_FAILURE) {
		/*
		 * printk() with zone->lock held will likely trigger a
		 * lockdep splat, so defer it here.
		 */
		dump_page(unmovable, "unmovable page");
	}

	return -EBUSY;
}

static void unset_migratetype_isolate(struct page *page, unsigned migratetype)
{
	struct zone *zone;
	unsigned long flags, nr_pages;
	bool isolated_page = false;
	unsigned int order;
	struct page *buddy;

	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	if (!is_migrate_isolate_page(page))
		goto out;

	/*
	 * Because freepage with more than pageblock_order on isolated
	 * pageblock is restricted to merge due to freepage counting problem,
	 * it is possible that there is free buddy page.
	 * move_freepages_block() doesn't care of merge so we need other
	 * approach in order to merge them. Isolation and free will make
	 * these pages to be merged.
	 */
	if (PageBuddy(page)) {
		order = buddy_order(page);
		if (order >= pageblock_order && order < MAX_ORDER - 1) {
			buddy = find_buddy_page_pfn(page, page_to_pfn(page),
						    order, NULL);
			if (buddy && !is_migrate_isolate_page(buddy)) {
				isolated_page = !!__isolate_free_page(page, order);
				/*
				 * Isolating a free page in an isolated pageblock
				 * is expected to always work as watermarks don't
				 * apply here.
				 */
				VM_WARN_ON(!isolated_page);
			}
		}
	}

	/*
	 * If we isolate freepage with more than pageblock_order, there
	 * should be no freepage in the range, so we could avoid costly
	 * pageblock scanning for freepage moving.
	 *
	 * We didn't actually touch any of the isolated pages, so place them
	 * to the tail of the freelist. This is an optimization for memory
	 * onlining - just onlined memory won't immediately be considered for
	 * allocation.
	 */
	if (!isolated_page) {
		nr_pages = move_freepages_block(zone, page, migratetype, NULL);
		__mod_zone_freepage_state(zone, nr_pages, migratetype);
	}
	set_pageblock_migratetype(page, migratetype);
	if (isolated_page)
		__putback_isolated_page(page, order, migratetype);
	zone->nr_isolate_pageblock--;
out:
	spin_unlock_irqrestore(&zone->lock, flags);
}

static inline struct page *
__first_valid_page(unsigned long pfn, unsigned long nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; i++) {
		struct page *page;

		page = pfn_to_online_page(pfn + i);
		if (!page)
			continue;
		return page;
	}
	return NULL;
}

/**
 * start_isolate_page_range() - make page-allocation-type of range of pages to
 * be MIGRATE_ISOLATE.
 * @start_pfn:		The lower PFN of the range to be isolated.
 * @end_pfn:		The upper PFN of the range to be isolated.
 *			start_pfn/end_pfn must be aligned to pageblock_order.
 * @migratetype:	Migrate type to set in error recovery.
 * @flags:		The following flags are allowed (they can be combined in
 *			a bit mask)
 *			MEMORY_OFFLINE - isolate to offline (!allocate) memory
 *					 e.g., skip over PageHWPoison() pages
 *					 and PageOffline() pages.
 *			REPORT_FAILURE - report details about the failure to
 *			isolate the range
 *
 * Making page-allocation-type to be MIGRATE_ISOLATE means free pages in
 * the range will never be allocated. Any free pages and pages freed in the
 * future will not be allocated again. If specified range includes migrate types
 * other than MOVABLE or CMA, this will fail with -EBUSY. For isolating all
 * pages in the range finally, the caller have to free all pages in the range.
 * test_page_isolated() can be used for test it.
 *
 * There is no high level synchronization mechanism that prevents two threads
 * from trying to isolate overlapping ranges. If this happens, one thread
 * will notice pageblocks in the overlapping range already set to isolate.
 * This happens in set_migratetype_isolate, and set_migratetype_isolate
 * returns an error. We then clean up by restoring the migration type on
 * pageblocks we may have modified and return -EBUSY to caller. This
 * prevents two threads from simultaneously working on overlapping ranges.
 *
 * Please note that there is no strong synchronization with the page allocator
 * either. Pages might be freed while their page blocks are marked ISOLATED.
 * A call to drain_all_pages() after isolation can flush most of them. However
 * in some cases pages might still end up on pcp lists and that would allow
 * for their allocation even when they are in fact isolated already. Depending
 * on how strong of a guarantee the caller needs, zone_pcp_disable/enable()
 * might be used to flush and disable pcplist before isolation and enable after
 * unisolation.
 *
 * Return: 0 on success and -EBUSY if any part of range cannot be isolated.
 */
int start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			     unsigned migratetype, int flags)
{
	unsigned long pfn;
	struct page *page;

	BUG_ON(!IS_ALIGNED(start_pfn, pageblock_nr_pages));
	BUG_ON(!IS_ALIGNED(end_pfn, pageblock_nr_pages));

	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && set_migratetype_isolate(page, migratetype, flags)) {
			undo_isolate_page_range(start_pfn, pfn, migratetype);
			return -EBUSY;
		}
	}
	return 0;
}

/*
 * Make isolated pages available again.
 */
void undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			    unsigned migratetype)
{
	unsigned long pfn;
	struct page *page;

	BUG_ON(!IS_ALIGNED(start_pfn, pageblock_nr_pages));
	BUG_ON(!IS_ALIGNED(end_pfn, pageblock_nr_pages));

	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (!page || !is_migrate_isolate_page(page))
			continue;
		unset_migratetype_isolate(page, migratetype);
	}
}
/*
 * Test all pages in the range is free(means isolated) or not.
 * all pages in [start_pfn...end_pfn) must be in the same zone.
 * zone->lock must be held before call this.
 *
 * Returns the last tested pfn.
 */
static unsigned long
__test_page_isolated_in_pageblock(unsigned long pfn, unsigned long end_pfn,
				  int flags)
{
	struct page *page;

	while (pfn < end_pfn) {
		page = pfn_to_page(pfn);
		if (PageBuddy(page))
			/*
			 * If the page is on a free list, it has to be on
			 * the correct MIGRATE_ISOLATE freelist. There is no
			 * simple way to verify that as VM_BUG_ON(), though.
			 */
			pfn += 1 << buddy_order(page);
		else if ((flags & MEMORY_OFFLINE) && PageHWPoison(page))
			/* A HWPoisoned page cannot be also PageBuddy */
			pfn++;
		else if ((flags & MEMORY_OFFLINE) && PageOffline(page) &&
			 !page_count(page))
			/*
			 * The responsible driver agreed to skip PageOffline()
			 * pages when offlining memory by dropping its
			 * reference in MEM_GOING_OFFLINE.
			 */
			pfn++;
		else
			break;
	}

	return pfn;
}

/* Caller should ensure that requested range is in a single zone */
int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn,
			int isol_flags)
{
	unsigned long pfn, flags;
	struct page *page;
	struct zone *zone;
	int ret;

	/*
	 * Note: pageblock_nr_pages != MAX_ORDER. Then, chunks of free pages
	 * are not aligned to pageblock_nr_pages.
	 * Then we just check migratetype first.
	 */
	for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && !is_migrate_isolate_page(page))
			break;
	}
	page = __first_valid_page(start_pfn, end_pfn - start_pfn);
	if ((pfn < end_pfn) || !page) {
		ret = -EBUSY;
		goto out;
	}

	/* Check all pages are free or marked as ISOLATED */
	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	pfn = __test_page_isolated_in_pageblock(start_pfn, end_pfn, isol_flags);
	spin_unlock_irqrestore(&zone->lock, flags);

	ret = pfn < end_pfn ? -EBUSY : 0;

out:
	trace_test_pages_isolated(start_pfn, end_pfn, pfn);

	return ret;
}
