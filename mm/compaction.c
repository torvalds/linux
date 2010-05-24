/*
 * linux/mm/compaction.c
 *
 * Memory compaction for the reduction of external fragmentation. Note that
 * this heavily depends upon page migration to do all the real heavy
 * lifting
 *
 * Copyright IBM Corp. 2007-2010 Mel Gorman <mel@csn.ul.ie>
 */
#include <linux/swap.h>
#include <linux/migrate.h>
#include <linux/compaction.h>
#include <linux/mm_inline.h>
#include <linux/backing-dev.h>
#include "internal.h"

/*
 * compact_control is used to track pages being migrated and the free pages
 * they are being migrated to during memory compaction. The free_pfn starts
 * at the end of a zone and migrate_pfn begins at the start. Movable pages
 * are moved to the end of a zone during a compaction run and the run
 * completes when free_pfn <= migrate_pfn
 */
struct compact_control {
	struct list_head freepages;	/* List of free pages to migrate to */
	struct list_head migratepages;	/* List of pages being migrated */
	unsigned long nr_freepages;	/* Number of isolated free pages */
	unsigned long nr_migratepages;	/* Number of pages to migrate */
	unsigned long free_pfn;		/* isolate_freepages search base */
	unsigned long migrate_pfn;	/* isolate_migratepages search base */

	/* Account for isolated anon and file pages */
	unsigned long nr_anon;
	unsigned long nr_file;

	struct zone *zone;
};

static unsigned long release_freepages(struct list_head *freelist)
{
	struct page *page, *next;
	unsigned long count = 0;

	list_for_each_entry_safe(page, next, freelist, lru) {
		list_del(&page->lru);
		__free_page(page);
		count++;
	}

	return count;
}

/* Isolate free pages onto a private freelist. Must hold zone->lock */
static unsigned long isolate_freepages_block(struct zone *zone,
				unsigned long blockpfn,
				struct list_head *freelist)
{
	unsigned long zone_end_pfn, end_pfn;
	int total_isolated = 0;
	struct page *cursor;

	/* Get the last PFN we should scan for free pages at */
	zone_end_pfn = zone->zone_start_pfn + zone->spanned_pages;
	end_pfn = min(blockpfn + pageblock_nr_pages, zone_end_pfn);

	/* Find the first usable PFN in the block to initialse page cursor */
	for (; blockpfn < end_pfn; blockpfn++) {
		if (pfn_valid_within(blockpfn))
			break;
	}
	cursor = pfn_to_page(blockpfn);

	/* Isolate free pages. This assumes the block is valid */
	for (; blockpfn < end_pfn; blockpfn++, cursor++) {
		int isolated, i;
		struct page *page = cursor;

		if (!pfn_valid_within(blockpfn))
			continue;

		if (!PageBuddy(page))
			continue;

		/* Found a free page, break it into order-0 pages */
		isolated = split_free_page(page);
		total_isolated += isolated;
		for (i = 0; i < isolated; i++) {
			list_add(&page->lru, freelist);
			page++;
		}

		/* If a page was split, advance to the end of it */
		if (isolated) {
			blockpfn += isolated - 1;
			cursor += isolated - 1;
		}
	}

	return total_isolated;
}

/* Returns true if the page is within a block suitable for migration to */
static bool suitable_migration_target(struct page *page)
{

	int migratetype = get_pageblock_migratetype(page);

	/* Don't interfere with memory hot-remove or the min_free_kbytes blocks */
	if (migratetype == MIGRATE_ISOLATE || migratetype == MIGRATE_RESERVE)
		return false;

	/* If the page is a large free page, then allow migration */
	if (PageBuddy(page) && page_order(page) >= pageblock_order)
		return true;

	/* If the block is MIGRATE_MOVABLE, allow migration */
	if (migratetype == MIGRATE_MOVABLE)
		return true;

	/* Otherwise skip the block */
	return false;
}

/*
 * Based on information in the current compact_control, find blocks
 * suitable for isolating free pages from and then isolate them.
 */
static void isolate_freepages(struct zone *zone,
				struct compact_control *cc)
{
	struct page *page;
	unsigned long high_pfn, low_pfn, pfn;
	unsigned long flags;
	int nr_freepages = cc->nr_freepages;
	struct list_head *freelist = &cc->freepages;

	pfn = cc->free_pfn;
	low_pfn = cc->migrate_pfn + pageblock_nr_pages;
	high_pfn = low_pfn;

	/*
	 * Isolate free pages until enough are available to migrate the
	 * pages on cc->migratepages. We stop searching if the migrate
	 * and free page scanners meet or enough free pages are isolated.
	 */
	spin_lock_irqsave(&zone->lock, flags);
	for (; pfn > low_pfn && cc->nr_migratepages > nr_freepages;
					pfn -= pageblock_nr_pages) {
		unsigned long isolated;

		if (!pfn_valid(pfn))
			continue;

		/*
		 * Check for overlapping nodes/zones. It's possible on some
		 * configurations to have a setup like
		 * node0 node1 node0
		 * i.e. it's possible that all pages within a zones range of
		 * pages do not belong to a single zone.
		 */
		page = pfn_to_page(pfn);
		if (page_zone(page) != zone)
			continue;

		/* Check the block is suitable for migration */
		if (!suitable_migration_target(page))
			continue;

		/* Found a block suitable for isolating free pages from */
		isolated = isolate_freepages_block(zone, pfn, freelist);
		nr_freepages += isolated;

		/*
		 * Record the highest PFN we isolated pages from. When next
		 * looking for free pages, the search will restart here as
		 * page migration may have returned some pages to the allocator
		 */
		if (isolated)
			high_pfn = max(high_pfn, pfn);
	}
	spin_unlock_irqrestore(&zone->lock, flags);

	/* split_free_page does not map the pages */
	list_for_each_entry(page, freelist, lru) {
		arch_alloc_page(page, 0);
		kernel_map_pages(page, 1, 1);
	}

	cc->free_pfn = high_pfn;
	cc->nr_freepages = nr_freepages;
}

/* Update the number of anon and file isolated pages in the zone */
static void acct_isolated(struct zone *zone, struct compact_control *cc)
{
	struct page *page;
	unsigned int count[NR_LRU_LISTS] = { 0, };

	list_for_each_entry(page, &cc->migratepages, lru) {
		int lru = page_lru_base_type(page);
		count[lru]++;
	}

	cc->nr_anon = count[LRU_ACTIVE_ANON] + count[LRU_INACTIVE_ANON];
	cc->nr_file = count[LRU_ACTIVE_FILE] + count[LRU_INACTIVE_FILE];
	__mod_zone_page_state(zone, NR_ISOLATED_ANON, cc->nr_anon);
	__mod_zone_page_state(zone, NR_ISOLATED_FILE, cc->nr_file);
}

/* Similar to reclaim, but different enough that they don't share logic */
static bool too_many_isolated(struct zone *zone)
{

	unsigned long inactive, isolated;

	inactive = zone_page_state(zone, NR_INACTIVE_FILE) +
					zone_page_state(zone, NR_INACTIVE_ANON);
	isolated = zone_page_state(zone, NR_ISOLATED_FILE) +
					zone_page_state(zone, NR_ISOLATED_ANON);

	return isolated > inactive;
}

/*
 * Isolate all pages that can be migrated from the block pointed to by
 * the migrate scanner within compact_control.
 */
static unsigned long isolate_migratepages(struct zone *zone,
					struct compact_control *cc)
{
	unsigned long low_pfn, end_pfn;
	struct list_head *migratelist = &cc->migratepages;

	/* Do not scan outside zone boundaries */
	low_pfn = max(cc->migrate_pfn, zone->zone_start_pfn);

	/* Only scan within a pageblock boundary */
	end_pfn = ALIGN(low_pfn + pageblock_nr_pages, pageblock_nr_pages);

	/* Do not cross the free scanner or scan within a memory hole */
	if (end_pfn > cc->free_pfn || !pfn_valid(low_pfn)) {
		cc->migrate_pfn = end_pfn;
		return 0;
	}

	/*
	 * Ensure that there are not too many pages isolated from the LRU
	 * list by either parallel reclaimers or compaction. If there are,
	 * delay for some time until fewer pages are isolated
	 */
	while (unlikely(too_many_isolated(zone))) {
		congestion_wait(BLK_RW_ASYNC, HZ/10);

		if (fatal_signal_pending(current))
			return 0;
	}

	/* Time to isolate some pages for migration */
	spin_lock_irq(&zone->lru_lock);
	for (; low_pfn < end_pfn; low_pfn++) {
		struct page *page;
		if (!pfn_valid_within(low_pfn))
			continue;

		/* Get the page and skip if free */
		page = pfn_to_page(low_pfn);
		if (PageBuddy(page))
			continue;

		/* Try isolate the page */
		if (__isolate_lru_page(page, ISOLATE_BOTH, 0) != 0)
			continue;

		/* Successfully isolated */
		del_page_from_lru_list(zone, page, page_lru(page));
		list_add(&page->lru, migratelist);
		mem_cgroup_del_lru(page);
		cc->nr_migratepages++;

		/* Avoid isolating too much */
		if (cc->nr_migratepages == COMPACT_CLUSTER_MAX)
			break;
	}

	acct_isolated(zone, cc);

	spin_unlock_irq(&zone->lru_lock);
	cc->migrate_pfn = low_pfn;

	return cc->nr_migratepages;
}

/*
 * This is a migrate-callback that "allocates" freepages by taking pages
 * from the isolated freelists in the block we are migrating to.
 */
static struct page *compaction_alloc(struct page *migratepage,
					unsigned long data,
					int **result)
{
	struct compact_control *cc = (struct compact_control *)data;
	struct page *freepage;

	/* Isolate free pages if necessary */
	if (list_empty(&cc->freepages)) {
		isolate_freepages(cc->zone, cc);

		if (list_empty(&cc->freepages))
			return NULL;
	}

	freepage = list_entry(cc->freepages.next, struct page, lru);
	list_del(&freepage->lru);
	cc->nr_freepages--;

	return freepage;
}

/*
 * We cannot control nr_migratepages and nr_freepages fully when migration is
 * running as migrate_pages() has no knowledge of compact_control. When
 * migration is complete, we count the number of pages on the lists by hand.
 */
static void update_nr_listpages(struct compact_control *cc)
{
	int nr_migratepages = 0;
	int nr_freepages = 0;
	struct page *page;

	list_for_each_entry(page, &cc->migratepages, lru)
		nr_migratepages++;
	list_for_each_entry(page, &cc->freepages, lru)
		nr_freepages++;

	cc->nr_migratepages = nr_migratepages;
	cc->nr_freepages = nr_freepages;
}

static int compact_finished(struct zone *zone,
						struct compact_control *cc)
{
	if (fatal_signal_pending(current))
		return COMPACT_PARTIAL;

	/* Compaction run completes if the migrate and free scanner meet */
	if (cc->free_pfn <= cc->migrate_pfn)
		return COMPACT_COMPLETE;

	return COMPACT_CONTINUE;
}

static int compact_zone(struct zone *zone, struct compact_control *cc)
{
	int ret;

	/* Setup to move all movable pages to the end of the zone */
	cc->migrate_pfn = zone->zone_start_pfn;
	cc->free_pfn = cc->migrate_pfn + zone->spanned_pages;
	cc->free_pfn &= ~(pageblock_nr_pages-1);

	migrate_prep_local();

	while ((ret = compact_finished(zone, cc)) == COMPACT_CONTINUE) {
		unsigned long nr_migrate, nr_remaining;

		if (!isolate_migratepages(zone, cc))
			continue;

		nr_migrate = cc->nr_migratepages;
		migrate_pages(&cc->migratepages, compaction_alloc,
						(unsigned long)cc, 0);
		update_nr_listpages(cc);
		nr_remaining = cc->nr_migratepages;

		count_vm_event(COMPACTBLOCKS);
		count_vm_events(COMPACTPAGES, nr_migrate - nr_remaining);
		if (nr_remaining)
			count_vm_events(COMPACTPAGEFAILED, nr_remaining);

		/* Release LRU pages not migrated */
		if (!list_empty(&cc->migratepages)) {
			putback_lru_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
		}

	}

	/* Release free pages and check accounting */
	cc->nr_freepages -= release_freepages(&cc->freepages);
	VM_BUG_ON(cc->nr_freepages != 0);

	return ret;
}
