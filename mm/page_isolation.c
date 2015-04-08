/*
 * linux/mm/page_isolation.c
 */

#include <linux/mm.h>
#include <linux/page-isolation.h>
#include <linux/pageblock-flags.h>
#include <linux/memory.h>
#include <linux/hugetlb.h>
#include "internal.h"

int set_migratetype_isolate(struct page *page, bool skip_hwpoisoned_pages)
{
	struct zone *zone;
	unsigned long flags, pfn;
	struct memory_isolate_notify arg;
	int notifier_ret;
	int ret = -EBUSY;

	zone = page_zone(page);

	spin_lock_irqsave(&zone->lock, flags);

	pfn = page_to_pfn(page);
	arg.start_pfn = pfn;
	arg.nr_pages = pageblock_nr_pages;
	arg.pages_found = 0;

	/*
	 * It may be possible to isolate a pageblock even if the
	 * migratetype is not MIGRATE_MOVABLE. The memory isolation
	 * notifier chain is used by balloon drivers to return the
	 * number of pages in a range that are held by the balloon
	 * driver to shrink memory. If all the pages are accounted for
	 * by balloons, are free, or on the LRU, isolation can continue.
	 * Later, for example, when memory hotplug notifier runs, these
	 * pages reported as "can be isolated" should be isolated(freed)
	 * by the balloon driver through the memory notifier chain.
	 */
	notifier_ret = memory_isolate_notify(MEM_ISOLATE_COUNT, &arg);
	notifier_ret = notifier_to_errno(notifier_ret);
	if (notifier_ret)
		goto out;
	/*
	 * FIXME: Now, memory hotplug doesn't call shrink_slab() by itself.
	 * We just check MOVABLE pages.
	 */
	if (!has_unmovable_pages(zone, page, arg.pages_found,
				 skip_hwpoisoned_pages))
		ret = 0;

	/*
	 * immobile means "not-on-lru" paes. If immobile is larger than
	 * removable-by-driver pages reported by notifier, we'll fail.
	 */

out:
	if (!ret) {
		unsigned long nr_pages;
		int migratetype = get_pageblock_migratetype(page);

		set_pageblock_migratetype(page, MIGRATE_ISOLATE);
		zone->nr_isolate_pageblock++;
		nr_pages = move_freepages_block(zone, page, MIGRATE_ISOLATE);

		__mod_zone_freepage_state(zone, -nr_pages, migratetype);
	}

	spin_unlock_irqrestore(&zone->lock, flags);
	if (!ret)
		drain_all_pages(zone);
	return ret;
}

void unset_migratetype_isolate(struct page *page, unsigned migratetype)
{
	struct zone *zone;
	unsigned long flags, nr_pages;
	struct page *isolated_page = NULL;
	unsigned int order;
	unsigned long page_idx, buddy_idx;
	struct page *buddy;

	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	if (get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
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
		order = page_order(page);
		if (order >= pageblock_order) {
			page_idx = page_to_pfn(page) & ((1 << MAX_ORDER) - 1);
			buddy_idx = __find_buddy_index(page_idx, order);
			buddy = page + (buddy_idx - page_idx);

			if (!is_migrate_isolate_page(buddy)) {
				__isolate_free_page(page, order);
				kernel_map_pages(page, (1 << order), 1);
				set_page_refcounted(page);
				isolated_page = page;
			}
		}
	}

	/*
	 * If we isolate freepage with more than pageblock_order, there
	 * should be no freepage in the range, so we could avoid costly
	 * pageblock scanning for freepage moving.
	 */
	if (!isolated_page) {
		nr_pages = move_freepages_block(zone, page, migratetype);
		__mod_zone_freepage_state(zone, nr_pages, migratetype);
	}
	set_pageblock_migratetype(page, migratetype);
	zone->nr_isolate_pageblock--;
out:
	spin_unlock_irqrestore(&zone->lock, flags);
	if (isolated_page)
		__free_pages(isolated_page, order);
}

static inline struct page *
__first_valid_page(unsigned long pfn, unsigned long nr_pages)
{
	int i;
	for (i = 0; i < nr_pages; i++)
		if (pfn_valid_within(pfn + i))
			break;
	if (unlikely(i == nr_pages))
		return NULL;
	return pfn_to_page(pfn + i);
}

/*
 * start_isolate_page_range() -- make page-allocation-type of range of pages
 * to be MIGRATE_ISOLATE.
 * @start_pfn: The lower PFN of the range to be isolated.
 * @end_pfn: The upper PFN of the range to be isolated.
 * @migratetype: migrate type to set in error recovery.
 *
 * Making page-allocation-type to be MIGRATE_ISOLATE means free pages in
 * the range will never be allocated. Any free pages and pages freed in the
 * future will not be allocated again.
 *
 * start_pfn/end_pfn must be aligned to pageblock_order.
 * Returns 0 on success and -EBUSY if any part of range cannot be isolated.
 */
int start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			     unsigned migratetype, bool skip_hwpoisoned_pages)
{
	unsigned long pfn;
	unsigned long undo_pfn;
	struct page *page;

	BUG_ON((start_pfn) & (pageblock_nr_pages - 1));
	BUG_ON((end_pfn) & (pageblock_nr_pages - 1));

	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (page &&
		    set_migratetype_isolate(page, skip_hwpoisoned_pages)) {
			undo_pfn = pfn;
			goto undo;
		}
	}
	return 0;
undo:
	for (pfn = start_pfn;
	     pfn < undo_pfn;
	     pfn += pageblock_nr_pages)
		unset_migratetype_isolate(pfn_to_page(pfn), migratetype);

	return -EBUSY;
}

/*
 * Make isolated pages available again.
 */
int undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			    unsigned migratetype)
{
	unsigned long pfn;
	struct page *page;
	BUG_ON((start_pfn) & (pageblock_nr_pages - 1));
	BUG_ON((end_pfn) & (pageblock_nr_pages - 1));
	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += pageblock_nr_pages) {
		page = __first_valid_page(pfn, pageblock_nr_pages);
		if (!page || get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
			continue;
		unset_migratetype_isolate(page, migratetype);
	}
	return 0;
}
/*
 * Test all pages in the range is free(means isolated) or not.
 * all pages in [start_pfn...end_pfn) must be in the same zone.
 * zone->lock must be held before call this.
 *
 * Returns 1 if all pages in the range are isolated.
 */
static int
__test_page_isolated_in_pageblock(unsigned long pfn, unsigned long end_pfn,
				  bool skip_hwpoisoned_pages)
{
	struct page *page;

	while (pfn < end_pfn) {
		if (!pfn_valid_within(pfn)) {
			pfn++;
			continue;
		}
		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			/*
			 * If race between isolatation and allocation happens,
			 * some free pages could be in MIGRATE_MOVABLE list
			 * although pageblock's migratation type of the page
			 * is MIGRATE_ISOLATE. Catch it and move the page into
			 * MIGRATE_ISOLATE list.
			 */
			if (get_freepage_migratetype(page) != MIGRATE_ISOLATE) {
				struct page *end_page;

				end_page = page + (1 << page_order(page)) - 1;
				move_freepages(page_zone(page), page, end_page,
						MIGRATE_ISOLATE);
			}
			pfn += 1 << page_order(page);
		}
		else if (page_count(page) == 0 &&
			get_freepage_migratetype(page) == MIGRATE_ISOLATE)
			pfn += 1;
		else if (skip_hwpoisoned_pages && PageHWPoison(page)) {
			/*
			 * The HWPoisoned page may be not in buddy
			 * system, and page_count() is not 0.
			 */
			pfn++;
			continue;
		}
		else
			break;
	}
	if (pfn < end_pfn)
		return 0;
	return 1;
}

int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn,
			bool skip_hwpoisoned_pages)
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
		if (page && get_pageblock_migratetype(page) != MIGRATE_ISOLATE)
			break;
	}
	page = __first_valid_page(start_pfn, end_pfn - start_pfn);
	if ((pfn < end_pfn) || !page)
		return -EBUSY;
	/* Check all pages are free or marked as ISOLATED */
	zone = page_zone(page);
	spin_lock_irqsave(&zone->lock, flags);
	ret = __test_page_isolated_in_pageblock(start_pfn, end_pfn,
						skip_hwpoisoned_pages);
	spin_unlock_irqrestore(&zone->lock, flags);
	return ret ? 0 : -EBUSY;
}

struct page *alloc_migrate_target(struct page *page, unsigned long private,
				  int **resultp)
{
	gfp_t gfp_mask = GFP_USER | __GFP_MOVABLE;

	/*
	 * TODO: allocate a destination hugepage from a nearest neighbor node,
	 * accordance with memory policy of the user process if possible. For
	 * now as a simple work-around, we use the next node for destination.
	 */
	if (PageHuge(page)) {
		nodemask_t src = nodemask_of_node(page_to_nid(page));
		nodemask_t dst;
		nodes_complement(dst, src);
		return alloc_huge_page_node(page_hstate(compound_head(page)),
					    next_node(page_to_nid(page), dst));
	}

	if (PageHighMem(page))
		gfp_mask |= __GFP_HIGHMEM;

	return alloc_page(gfp_mask);
}
