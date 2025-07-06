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
 * This function checks whether the range [start_pfn, end_pfn) includes
 * unmovable pages or not. The range must fall into a single pageblock and
 * consequently belong to a single zone.
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
static struct page *has_unmovable_pages(unsigned long start_pfn, unsigned long end_pfn,
				int migratetype, int flags)
{
	struct page *page = pfn_to_page(start_pfn);
	struct zone *zone = page_zone(page);
	unsigned long pfn;

	VM_BUG_ON(pageblock_start_pfn(start_pfn) !=
		  pageblock_start_pfn(end_pfn - 1));

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

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		page = pfn_to_page(pfn);

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
			struct folio *folio = page_folio(page);
			unsigned int skip_pages;

			if (PageHuge(page)) {
				struct hstate *h;

				/*
				 * The huge page may be freed so can not
				 * use folio_hstate() directly.
				 */
				h = size_to_hstate(folio_size(folio));
				if (h && !hugepage_migration_supported(h))
					return page;
			} else if (!folio_test_lru(folio) && !__folio_test_movable(folio)) {
				return page;
			}

			skip_pages = folio_nr_pages(folio) - folio_page_idx(folio, page);
			pfn += skip_pages - 1;
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
				pfn += (1 << buddy_order(page)) - 1;
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

/*
 * This function set pageblock migratetype to isolate if no unmovable page is
 * present in [start_pfn, end_pfn). The pageblock must intersect with
 * [start_pfn, end_pfn).
 */
static int set_migratetype_isolate(struct page *page, int migratetype, int isol_flags,
			unsigned long start_pfn, unsigned long end_pfn)
{
	struct zone *zone = page_zone(page);
	struct page *unmovable;
	unsigned long flags;
	unsigned long check_unmovable_start, check_unmovable_end;

	if (PageUnaccepted(page))
		accept_page(page);

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
	 *
	 * Pass the intersection of [start_pfn, end_pfn) and the page's pageblock
	 * to avoid redundant checks.
	 */
	check_unmovable_start = max(page_to_pfn(page), start_pfn);
	check_unmovable_end = min(pageblock_end_pfn(page_to_pfn(page)),
				  end_pfn);

	unmovable = has_unmovable_pages(check_unmovable_start, check_unmovable_end,
			migratetype, isol_flags);
	if (!unmovable) {
		if (!move_freepages_block_isolate(zone, page, MIGRATE_ISOLATE)) {
			spin_unlock_irqrestore(&zone->lock, flags);
			return -EBUSY;
		}
		zone->nr_isolate_pageblock++;
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

static void unset_migratetype_isolate(struct page *page, int migratetype)
{
	struct zone *zone;
	unsigned long flags;
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
		if (order >= pageblock_order && order < MAX_PAGE_ORDER) {
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
		/*
		 * Isolating this block already succeeded, so this
		 * should not fail on zone boundaries.
		 */
		WARN_ON_ONCE(!move_freepages_block_isolate(zone, page, migratetype));
	} else {
		set_pageblock_migratetype(page, migratetype);
		__putback_isolated_page(page, order, migratetype);
	}
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
 * isolate_single_pageblock() -- tries to isolate a pageblock that might be
 * within a free or in-use page.
 * @boundary_pfn:		pageblock-aligned pfn that a page might cross
 * @flags:			isolation flags
 * @isolate_before:	isolate the pageblock before the boundary_pfn
 * @skip_isolation:	the flag to skip the pageblock isolation in second
 *			isolate_single_pageblock()
 * @migratetype:	migrate type to set in error recovery.
 *
 * Free and in-use pages can be as big as MAX_PAGE_ORDER and contain more than one
 * pageblock. When not all pageblocks within a page are isolated at the same
 * time, free page accounting can go wrong. For example, in the case of
 * MAX_PAGE_ORDER = pageblock_order + 1, a MAX_PAGE_ORDER page has two
 * pagelbocks.
 * [      MAX_PAGE_ORDER         ]
 * [  pageblock0  |  pageblock1  ]
 * When either pageblock is isolated, if it is a free page, the page is not
 * split into separate migratetype lists, which is supposed to; if it is an
 * in-use page and freed later, __free_one_page() does not split the free page
 * either. The function handles this by splitting the free page or migrating
 * the in-use page then splitting the free page.
 */
static int isolate_single_pageblock(unsigned long boundary_pfn, int flags,
		bool isolate_before, bool skip_isolation, int migratetype)
{
	unsigned long start_pfn;
	unsigned long isolate_pageblock;
	unsigned long pfn;
	struct zone *zone;
	int ret;

	VM_BUG_ON(!pageblock_aligned(boundary_pfn));

	if (isolate_before)
		isolate_pageblock = boundary_pfn - pageblock_nr_pages;
	else
		isolate_pageblock = boundary_pfn;

	/*
	 * scan at the beginning of MAX_ORDER_NR_PAGES aligned range to avoid
	 * only isolating a subset of pageblocks from a bigger than pageblock
	 * free or in-use page. Also make sure all to-be-isolated pageblocks
	 * are within the same zone.
	 */
	zone  = page_zone(pfn_to_page(isolate_pageblock));
	start_pfn  = max(ALIGN_DOWN(isolate_pageblock, MAX_ORDER_NR_PAGES),
				      zone->zone_start_pfn);

	if (skip_isolation) {
		int mt __maybe_unused = get_pageblock_migratetype(pfn_to_page(isolate_pageblock));

		VM_BUG_ON(!is_migrate_isolate(mt));
	} else {
		ret = set_migratetype_isolate(pfn_to_page(isolate_pageblock), migratetype,
				flags, isolate_pageblock, isolate_pageblock + pageblock_nr_pages);

		if (ret)
			return ret;
	}

	/*
	 * Bail out early when the to-be-isolated pageblock does not form
	 * a free or in-use page across boundary_pfn:
	 *
	 * 1. isolate before boundary_pfn: the page after is not online
	 * 2. isolate after boundary_pfn: the page before is not online
	 *
	 * This also ensures correctness. Without it, when isolate after
	 * boundary_pfn and [start_pfn, boundary_pfn) are not online,
	 * __first_valid_page() will return unexpected NULL in the for loop
	 * below.
	 */
	if (isolate_before) {
		if (!pfn_to_online_page(boundary_pfn))
			return 0;
	} else {
		if (!pfn_to_online_page(boundary_pfn - 1))
			return 0;
	}

	for (pfn = start_pfn; pfn < boundary_pfn;) {
		struct page *page = __first_valid_page(pfn, boundary_pfn - pfn);

		VM_BUG_ON(!page);
		pfn = page_to_pfn(page);

		if (PageUnaccepted(page)) {
			pfn += MAX_ORDER_NR_PAGES;
			continue;
		}

		if (PageBuddy(page)) {
			int order = buddy_order(page);

			/* move_freepages_block_isolate() handled this */
			VM_WARN_ON_ONCE(pfn + (1 << order) > boundary_pfn);

			pfn += 1UL << order;
			continue;
		}

		/*
		 * If a compound page is straddling our block, attempt
		 * to migrate it out of the way.
		 *
		 * We don't have to worry about this creating a large
		 * free page that straddles into our block: gigantic
		 * pages are freed as order-0 chunks, and LRU pages
		 * (currently) do not exceed pageblock_order.
		 *
		 * The block of interest has already been marked
		 * MIGRATE_ISOLATE above, so when migration is done it
		 * will free its pages onto the correct freelists.
		 */
		if (PageCompound(page)) {
			struct page *head = compound_head(page);
			unsigned long head_pfn = page_to_pfn(head);
			unsigned long nr_pages = compound_nr(head);

			if (head_pfn + nr_pages <= boundary_pfn ||
			    PageHuge(page)) {
				pfn = head_pfn + nr_pages;
				continue;
			}

			/*
			 * These pages are movable too, but they're
			 * not expected to exceed pageblock_order.
			 *
			 * Let us know when they do, so we can add
			 * proper free and split handling for them.
			 */
			VM_WARN_ON_ONCE_PAGE(PageLRU(page), page);
			VM_WARN_ON_ONCE_PAGE(__PageMovable(page), page);

			goto failed;
		}

		pfn++;
	}
	return 0;
failed:
	/* restore the original migratetype */
	if (!skip_isolation)
		unset_migratetype_isolate(pfn_to_page(isolate_pageblock), migratetype);
	return -EBUSY;
}

/**
 * start_isolate_page_range() - mark page range MIGRATE_ISOLATE
 * @start_pfn:		The first PFN of the range to be isolated.
 * @end_pfn:		The last PFN of the range to be isolated.
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
 * The function first tries to isolate the pageblocks at the beginning and end
 * of the range, since there might be pages across the range boundaries.
 * Afterwards, it isolates the rest of the range.
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
			     int migratetype, int flags)
{
	unsigned long pfn;
	struct page *page;
	/* isolation is done at page block granularity */
	unsigned long isolate_start = pageblock_start_pfn(start_pfn);
	unsigned long isolate_end = pageblock_align(end_pfn);
	int ret;
	bool skip_isolation = false;

	/* isolate [isolate_start, isolate_start + pageblock_nr_pages) pageblock */
	ret = isolate_single_pageblock(isolate_start, flags, false,
			skip_isolation, migratetype);
	if (ret)
		return ret;

	if (isolate_start == isolate_end - pageblock_nr_pages)
		skip_isolation = true;

	/* isolate [isolate_end - pageblock_nr_pages, isolate_end) pageblock */
	ret = isolate_single_pageblock(isolate_end, flags, true,
			skip_isolation, migratetype);
	if (ret) {
		unset_migratetype_isolate(pfn_to_page(isolate_start), migratetype);
		return ret;
	}

	/* skip isolated pageblocks at the beginning and end */
	for (pfn = isolate_start + pageblock_nr_pages;
	     pfn < isolate_end - pageblock_nr_pages;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page && set_migratetype_isolate(page, migratetype, flags,
					start_pfn, end_pfn)) {
			undo_isolate_page_range(isolate_start, pfn, migratetype);
			unset_migratetype_isolate(
				pfn_to_page(isolate_end - pageblock_nr_pages),
				migratetype);
			return -EBUSY;
		}
	}
	return 0;
}

/**
 * undo_isolate_page_range - undo effects of start_isolate_page_range()
 * @start_pfn:		The first PFN of the isolated range
 * @end_pfn:		The last PFN of the isolated range
 * @migratetype:	New migrate type to set on the range
 *
 * This finds every MIGRATE_ISOLATE page block in the given range
 * and switches it to @migratetype.
 */
void undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			    int migratetype)
{
	unsigned long pfn;
	struct page *page;
	unsigned long isolate_start = pageblock_start_pfn(start_pfn);
	unsigned long isolate_end = pageblock_align(end_pfn);

	for (pfn = isolate_start;
	     pfn < isolate_end;
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

/**
 * test_pages_isolated - check if pageblocks in range are isolated
 * @start_pfn:		The first PFN of the isolated range
 * @end_pfn:		The first PFN *after* the isolated range
 * @isol_flags:		Testing mode flags
 *
 * This tests if all in the specified range are free.
 *
 * If %MEMORY_OFFLINE is specified in @flags, it will consider
 * poisoned and offlined pages free as well.
 *
 * Caller must ensure the requested range doesn't span zones.
 *
 * Returns 0 if true, -EBUSY if one or more pages are in use.
 */
int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn,
			int isol_flags)
{
	unsigned long pfn, flags;
	struct page *page;
	struct zone *zone;
	int ret;

	/*
	 * Due to the deferred freeing of hugetlb folios, the hugepage folios may
	 * not immediately release to the buddy system. This can cause PageBuddy()
	 * to fail in __test_page_isolated_in_pageblock(). To ensure that the
	 * hugetlb folios are properly released back to the buddy system, we
	 * invoke the wait_for_freed_hugetlb_folios() function to wait for the
	 * release to complete.
	 */
	wait_for_freed_hugetlb_folios();

	/*
	 * Note: pageblock_nr_pages != MAX_PAGE_ORDER. Then, chunks of free
	 * pages are not aligned to pageblock_nr_pages.
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
