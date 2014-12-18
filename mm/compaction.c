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
#include <linux/sysctl.h>
#include <linux/sysfs.h>
#include <linux/balloon_compaction.h>
#include <linux/page-isolation.h>
#include "internal.h"

#ifdef CONFIG_COMPACTION
static inline void count_compact_event(enum vm_event_item item)
{
	count_vm_event(item);
}

static inline void count_compact_events(enum vm_event_item item, long delta)
{
	count_vm_events(item, delta);
}
#else
#define count_compact_event(item) do { } while (0)
#define count_compact_events(item, delta) do { } while (0)
#endif

#if defined CONFIG_COMPACTION || defined CONFIG_CMA

#define CREATE_TRACE_POINTS
#include <trace/events/compaction.h>

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

static void map_pages(struct list_head *list)
{
	struct page *page;

	list_for_each_entry(page, list, lru) {
		arch_alloc_page(page, 0);
		kernel_map_pages(page, 1, 1);
	}
}

static inline bool migrate_async_suitable(int migratetype)
{
	return is_migrate_cma(migratetype) || migratetype == MIGRATE_MOVABLE;
}

#ifdef CONFIG_COMPACTION
/* Returns true if the pageblock should be scanned for pages to isolate. */
static inline bool isolation_suitable(struct compact_control *cc,
					struct page *page)
{
	if (cc->ignore_skip_hint)
		return true;

	return !get_pageblock_skip(page);
}

/*
 * This function is called to clear all cached information on pageblocks that
 * should be skipped for page isolation when the migrate and free page scanner
 * meet.
 */
static void __reset_isolation_suitable(struct zone *zone)
{
	unsigned long start_pfn = zone->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(zone);
	unsigned long pfn;

	zone->compact_cached_migrate_pfn = start_pfn;
	zone->compact_cached_free_pfn = end_pfn;
	zone->compact_blockskip_flush = false;

	/* Walk the zone and mark every pageblock as suitable for isolation */
	for (pfn = start_pfn; pfn < end_pfn; pfn += pageblock_nr_pages) {
		struct page *page;

		cond_resched();

		if (!pfn_valid(pfn))
			continue;

		page = pfn_to_page(pfn);
		if (zone != page_zone(page))
			continue;

		clear_pageblock_skip(page);
	}
}

void reset_isolation_suitable(pg_data_t *pgdat)
{
	int zoneid;

	for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
		struct zone *zone = &pgdat->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		/* Only flush if a full compaction finished recently */
		if (zone->compact_blockskip_flush)
			__reset_isolation_suitable(zone);
	}
}

/*
 * If no pages were isolated then mark this pageblock to be skipped in the
 * future. The information is later cleared by __reset_isolation_suitable().
 */
static void update_pageblock_skip(struct compact_control *cc,
			struct page *page, unsigned long nr_isolated,
			bool migrate_scanner)
{
	struct zone *zone = cc->zone;

	if (cc->ignore_skip_hint)
		return;

	if (!page)
		return;

	if (!nr_isolated) {
		unsigned long pfn = page_to_pfn(page);
		set_pageblock_skip(page);

		/* Update where compaction should restart */
		if (migrate_scanner) {
			if (!cc->finished_update_migrate &&
			    pfn > zone->compact_cached_migrate_pfn)
				zone->compact_cached_migrate_pfn = pfn;
		} else {
			if (!cc->finished_update_free &&
			    pfn < zone->compact_cached_free_pfn)
				zone->compact_cached_free_pfn = pfn;
		}
	}
}
#else
static inline bool isolation_suitable(struct compact_control *cc,
					struct page *page)
{
	return true;
}

static void update_pageblock_skip(struct compact_control *cc,
			struct page *page, unsigned long nr_isolated,
			bool migrate_scanner)
{
}
#endif /* CONFIG_COMPACTION */

static inline bool should_release_lock(spinlock_t *lock)
{
	return need_resched() || spin_is_contended(lock);
}

/*
 * Compaction requires the taking of some coarse locks that are potentially
 * very heavily contended. Check if the process needs to be scheduled or
 * if the lock is contended. For async compaction, back out in the event
 * if contention is severe. For sync compaction, schedule.
 *
 * Returns true if the lock is held.
 * Returns false if the lock is released and compaction should abort
 */
static bool compact_checklock_irqsave(spinlock_t *lock, unsigned long *flags,
				      bool locked, struct compact_control *cc)
{
	if (should_release_lock(lock)) {
		if (locked) {
			spin_unlock_irqrestore(lock, *flags);
			locked = false;
		}

		/* async aborts if taking too long or contended */
		if (!cc->sync) {
			cc->contended = true;
			return false;
		}

		cond_resched();
	}

	if (!locked)
		spin_lock_irqsave(lock, *flags);
	return true;
}

static inline bool compact_trylock_irqsave(spinlock_t *lock,
			unsigned long *flags, struct compact_control *cc)
{
	return compact_checklock_irqsave(lock, flags, false, cc);
}

/* Returns true if the page is within a block suitable for migration to */
static bool suitable_migration_target(struct page *page)
{
	int migratetype = get_pageblock_migratetype(page);

	/* Don't interfere with memory hot-remove or the min_free_kbytes blocks */
	if (migratetype == MIGRATE_RESERVE)
		return false;

	if (is_migrate_isolate(migratetype))
		return false;

	/* If the page is a large free page, then allow migration */
	if (PageBuddy(page) && page_order(page) >= pageblock_order)
		return true;

	/* If the block is MIGRATE_MOVABLE or MIGRATE_CMA, allow migration */
	if (migrate_async_suitable(migratetype))
		return true;

	/* Otherwise skip the block */
	return false;
}

/*
 * Isolate free pages onto a private freelist. Caller must hold zone->lock.
 * If @strict is true, will abort returning 0 on any invalid PFNs or non-free
 * pages inside of the pageblock (even though it may still end up isolating
 * some pages).
 */
static unsigned long isolate_freepages_block(struct compact_control *cc,
				unsigned long blockpfn,
				unsigned long end_pfn,
				struct list_head *freelist,
				bool strict)
{
	int nr_scanned = 0, total_isolated = 0;
	struct page *cursor, *valid_page = NULL;
	unsigned long flags;
	bool locked = false;

	cursor = pfn_to_page(blockpfn);

	/* Isolate free pages. */
	for (; blockpfn < end_pfn; blockpfn++, cursor++) {
		int isolated, i;
		struct page *page = cursor;

		nr_scanned++;
		if (!pfn_valid_within(blockpfn))
			goto isolate_fail;

		if (!valid_page)
			valid_page = page;
		if (!PageBuddy(page))
			goto isolate_fail;

		/*
		 * The zone lock must be held to isolate freepages.
		 * Unfortunately this is a very coarse lock and can be
		 * heavily contended if there are parallel allocations
		 * or parallel compactions. For async compaction do not
		 * spin on the lock and we acquire the lock as late as
		 * possible.
		 */
		locked = compact_checklock_irqsave(&cc->zone->lock, &flags,
								locked, cc);
		if (!locked)
			break;

		/* Recheck this is a suitable migration target under lock */
		if (!strict && !suitable_migration_target(page))
			break;

		/* Recheck this is a buddy page under lock */
		if (!PageBuddy(page))
			goto isolate_fail;

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
			continue;
		}

isolate_fail:
		if (strict)
			break;
		else
			continue;

	}

	trace_mm_compaction_isolate_freepages(nr_scanned, total_isolated);

	/*
	 * If strict isolation is requested by CMA then check that all the
	 * pages requested were isolated. If there were any failures, 0 is
	 * returned and CMA will fail.
	 */
	if (strict && blockpfn < end_pfn)
		total_isolated = 0;

	if (locked)
		spin_unlock_irqrestore(&cc->zone->lock, flags);

	/* Update the pageblock-skip if the whole pageblock was scanned */
	if (blockpfn == end_pfn)
		update_pageblock_skip(cc, valid_page, total_isolated, false);

	count_compact_events(COMPACTFREE_SCANNED, nr_scanned);
	if (total_isolated)
		count_compact_events(COMPACTISOLATED, total_isolated);
	return total_isolated;
}

/**
 * isolate_freepages_range() - isolate free pages.
 * @start_pfn: The first PFN to start isolating.
 * @end_pfn:   The one-past-last PFN.
 *
 * Non-free pages, invalid PFNs, or zone boundaries within the
 * [start_pfn, end_pfn) range are considered errors, cause function to
 * undo its actions and return zero.
 *
 * Otherwise, function returns one-past-the-last PFN of isolated page
 * (which may be greater then end_pfn if end fell in a middle of
 * a free page).
 */
unsigned long
isolate_freepages_range(struct compact_control *cc,
			unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long isolated, pfn, block_end_pfn;
	LIST_HEAD(freelist);

	for (pfn = start_pfn; pfn < end_pfn; pfn += isolated) {
		if (!pfn_valid(pfn) || cc->zone != page_zone(pfn_to_page(pfn)))
			break;

		/*
		 * On subsequent iterations ALIGN() is actually not needed,
		 * but we keep it that we not to complicate the code.
		 */
		block_end_pfn = ALIGN(pfn + 1, pageblock_nr_pages);
		block_end_pfn = min(block_end_pfn, end_pfn);

		isolated = isolate_freepages_block(cc, pfn, block_end_pfn,
						   &freelist, true);

		/*
		 * In strict mode, isolate_freepages_block() returns 0 if
		 * there are any holes in the block (ie. invalid PFNs or
		 * non-free pages).
		 */
		if (!isolated)
			break;

		/*
		 * If we managed to isolate pages, it is always (1 << n) *
		 * pageblock_nr_pages for some non-negative n.  (Max order
		 * page may span two pageblocks).
		 */
	}

	/* split_free_page does not map the pages */
	map_pages(&freelist);

	if (pfn < end_pfn) {
		/* Loop terminated early, cleanup. */
		release_freepages(&freelist);
		return 0;
	}

	/* We don't use freelists for anything. */
	return pfn;
}

/* Update the number of anon and file isolated pages in the zone */
static void acct_isolated(struct zone *zone, bool locked, struct compact_control *cc)
{
	struct page *page;
	unsigned int count[2] = { 0, };

	list_for_each_entry(page, &cc->migratepages, lru)
		count[!!page_is_file_cache(page)]++;

	/* If locked we can use the interrupt unsafe versions */
	if (locked) {
		__mod_zone_page_state(zone, NR_ISOLATED_ANON, count[0]);
		__mod_zone_page_state(zone, NR_ISOLATED_FILE, count[1]);
	} else {
		mod_zone_page_state(zone, NR_ISOLATED_ANON, count[0]);
		mod_zone_page_state(zone, NR_ISOLATED_FILE, count[1]);
	}
}

/* Similar to reclaim, but different enough that they don't share logic */
static bool too_many_isolated(struct zone *zone)
{
	unsigned long active, inactive, isolated;

	inactive = zone_page_state(zone, NR_INACTIVE_FILE) +
					zone_page_state(zone, NR_INACTIVE_ANON);
	active = zone_page_state(zone, NR_ACTIVE_FILE) +
					zone_page_state(zone, NR_ACTIVE_ANON);
	isolated = zone_page_state(zone, NR_ISOLATED_FILE) +
					zone_page_state(zone, NR_ISOLATED_ANON);

	return isolated > (inactive + active) / 2;
}

/**
 * isolate_migratepages_range() - isolate all migrate-able pages in range.
 * @zone:	Zone pages are in.
 * @cc:		Compaction control structure.
 * @low_pfn:	The first PFN of the range.
 * @end_pfn:	The one-past-the-last PFN of the range.
 * @unevictable: true if it allows to isolate unevictable pages
 *
 * Isolate all pages that can be migrated from the range specified by
 * [low_pfn, end_pfn).  Returns zero if there is a fatal signal
 * pending), otherwise PFN of the first page that was not scanned
 * (which may be both less, equal to or more then end_pfn).
 *
 * Assumes that cc->migratepages is empty and cc->nr_migratepages is
 * zero.
 *
 * Apart from cc->migratepages and cc->nr_migratetypes this function
 * does not modify any cc's fields, in particular it does not modify
 * (or read for that matter) cc->migrate_pfn.
 */
unsigned long
isolate_migratepages_range(struct zone *zone, struct compact_control *cc,
		unsigned long low_pfn, unsigned long end_pfn, bool unevictable)
{
	unsigned long last_pageblock_nr = 0, pageblock_nr;
	unsigned long nr_scanned = 0, nr_isolated = 0;
	struct list_head *migratelist = &cc->migratepages;
	isolate_mode_t mode = 0;
	struct lruvec *lruvec;
	unsigned long flags;
	bool locked = false;
	struct page *page = NULL, *valid_page = NULL;

	/*
	 * Ensure that there are not too many pages isolated from the LRU
	 * list by either parallel reclaimers or compaction. If there are,
	 * delay for some time until fewer pages are isolated
	 */
	while (unlikely(too_many_isolated(zone))) {
		/* async migration should just abort */
		if (!cc->sync)
			return 0;

		congestion_wait(BLK_RW_ASYNC, HZ/10);

		if (fatal_signal_pending(current))
			return 0;
	}

	/* Time to isolate some pages for migration */
	cond_resched();
	for (; low_pfn < end_pfn; low_pfn++) {
		/* give a chance to irqs before checking need_resched() */
		if (locked && !((low_pfn+1) % SWAP_CLUSTER_MAX)) {
			if (should_release_lock(&zone->lru_lock)) {
				spin_unlock_irqrestore(&zone->lru_lock, flags);
				locked = false;
			}
		}

		/*
		 * migrate_pfn does not necessarily start aligned to a
		 * pageblock. Ensure that pfn_valid is called when moving
		 * into a new MAX_ORDER_NR_PAGES range in case of large
		 * memory holes within the zone
		 */
		if ((low_pfn & (MAX_ORDER_NR_PAGES - 1)) == 0) {
			if (!pfn_valid(low_pfn)) {
				low_pfn += MAX_ORDER_NR_PAGES - 1;
				continue;
			}
		}

		if (!pfn_valid_within(low_pfn))
			continue;
		nr_scanned++;

		/*
		 * Get the page and ensure the page is within the same zone.
		 * See the comment in isolate_freepages about overlapping
		 * nodes. It is deliberate that the new zone lock is not taken
		 * as memory compaction should not move pages between nodes.
		 */
		page = pfn_to_page(low_pfn);
		if (page_zone(page) != zone)
			continue;

		if (!valid_page)
			valid_page = page;

		/* If isolation recently failed, do not retry */
		pageblock_nr = low_pfn >> pageblock_order;
		if (!isolation_suitable(cc, page))
			goto next_pageblock;

		/* Skip if free */
		if (PageBuddy(page))
			continue;

		/*
		 * For async migration, also only scan in MOVABLE blocks. Async
		 * migration is optimistic to see if the minimum amount of work
		 * satisfies the allocation
		 */
		if (!cc->sync && last_pageblock_nr != pageblock_nr &&
		    !migrate_async_suitable(get_pageblock_migratetype(page))) {
			cc->finished_update_migrate = true;
			goto next_pageblock;
		}

		/*
		 * Check may be lockless but that's ok as we recheck later.
		 * It's possible to migrate LRU pages and balloon pages
		 * Skip any other type of page
		 */
		if (!PageLRU(page)) {
			if (unlikely(balloon_page_movable(page))) {
				if (locked && balloon_page_isolate(page)) {
					/* Successfully isolated */
					cc->finished_update_migrate = true;
					list_add(&page->lru, migratelist);
					cc->nr_migratepages++;
					nr_isolated++;
					goto check_compact_cluster;
				}
			}
			continue;
		}

		/*
		 * PageLRU is set. lru_lock normally excludes isolation
		 * splitting and collapsing (collapsing has already happened
		 * if PageLRU is set) but the lock is not necessarily taken
		 * here and it is wasteful to take it just to check transhuge.
		 * Check TransHuge without lock and skip the whole pageblock if
		 * it's either a transhuge or hugetlbfs page, as calling
		 * compound_order() without preventing THP from splitting the
		 * page underneath us may return surprising results.
		 */
		if (PageTransHuge(page)) {
			if (!locked)
				goto next_pageblock;
			low_pfn += (1 << compound_order(page)) - 1;
			continue;
		}

		/* Check if it is ok to still hold the lock */
		locked = compact_checklock_irqsave(&zone->lru_lock, &flags,
								locked, cc);
		if (!locked || fatal_signal_pending(current))
			break;

		/* Recheck PageLRU and PageTransHuge under lock */
		if (!PageLRU(page))
			continue;
		if (PageTransHuge(page)) {
			low_pfn += (1 << compound_order(page)) - 1;
			continue;
		}

		if (!cc->sync)
			mode |= ISOLATE_ASYNC_MIGRATE;

		if (unevictable)
			mode |= ISOLATE_UNEVICTABLE;

		lruvec = mem_cgroup_page_lruvec(page, zone);

		/* Try isolate the page */
		if (__isolate_lru_page(page, mode, cc->migratetype) != 0)
			continue;

		VM_BUG_ON(PageTransCompound(page));

		/* Successfully isolated */
		cc->finished_update_migrate = true;
		del_page_from_lru_list(page, lruvec, page_lru(page));
		list_add(&page->lru, migratelist);
		cc->nr_migratepages++;
		nr_isolated++;

check_compact_cluster:
		/* Avoid isolating too much */
		if (cc->nr_migratepages == COMPACT_CLUSTER_MAX) {
			++low_pfn;
			break;
		}

		continue;

next_pageblock:
		low_pfn = ALIGN(low_pfn + 1, pageblock_nr_pages) - 1;
		last_pageblock_nr = pageblock_nr;
	}

	acct_isolated(zone, locked, cc);

	if (locked)
		spin_unlock_irqrestore(&zone->lru_lock, flags);

	/* Update the pageblock-skip if the whole pageblock was scanned */
	if (low_pfn == end_pfn)
		update_pageblock_skip(cc, valid_page, nr_isolated, true);

	trace_mm_compaction_isolate_migratepages(nr_scanned, nr_isolated);

	count_compact_events(COMPACTMIGRATE_SCANNED, nr_scanned);
	if (nr_isolated)
		count_compact_events(COMPACTISOLATED, nr_isolated);

	return low_pfn;
}

#endif /* CONFIG_COMPACTION || CONFIG_CMA */
#ifdef CONFIG_COMPACTION
/*
 * Based on information in the current compact_control, find blocks
 * suitable for isolating free pages from and then isolate them.
 */
static void isolate_freepages(struct zone *zone,
				struct compact_control *cc)
{
	struct page *page;
	unsigned long high_pfn, low_pfn, pfn, z_end_pfn, end_pfn;
	int nr_freepages = cc->nr_freepages;
	struct list_head *freelist = &cc->freepages;

	/*
	 * Initialise the free scanner. The starting point is where we last
	 * scanned from (or the end of the zone if starting). The low point
	 * is the end of the pageblock the migration scanner is using.
	 */
	pfn = cc->free_pfn;
	low_pfn = cc->migrate_pfn + pageblock_nr_pages;

	/*
	 * Take care that if the migration scanner is at the end of the zone
	 * that the free scanner does not accidentally move to the next zone
	 * in the next isolation cycle.
	 */
	high_pfn = min(low_pfn, pfn);

	z_end_pfn = zone_end_pfn(zone);

	/*
	 * Isolate free pages until enough are available to migrate the
	 * pages on cc->migratepages. We stop searching if the migrate
	 * and free page scanners meet or enough free pages are isolated.
	 */
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

		/* If isolation recently failed, do not retry */
		if (!isolation_suitable(cc, page))
			continue;

		/* Found a block suitable for isolating free pages from */
		isolated = 0;

		/*
		 * As pfn may not start aligned, pfn+pageblock_nr_page
		 * may cross a MAX_ORDER_NR_PAGES boundary and miss
		 * a pfn_valid check. Ensure isolate_freepages_block()
		 * only scans within a pageblock
		 */
		end_pfn = ALIGN(pfn + 1, pageblock_nr_pages);
		end_pfn = min(end_pfn, z_end_pfn);
		isolated = isolate_freepages_block(cc, pfn, end_pfn,
						   freelist, false);
		nr_freepages += isolated;

		/*
		 * Record the highest PFN we isolated pages from. When next
		 * looking for free pages, the search will restart here as
		 * page migration may have returned some pages to the allocator
		 */
		if (isolated) {
			cc->finished_update_free = true;
			high_pfn = max(high_pfn, pfn);
		}
	}

	/* split_free_page does not map the pages */
	map_pages(freelist);

	cc->free_pfn = high_pfn;
	cc->nr_freepages = nr_freepages;
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

/* possible outcome of isolate_migratepages */
typedef enum {
	ISOLATE_ABORT,		/* Abort compaction now */
	ISOLATE_NONE,		/* No pages isolated, continue scanning */
	ISOLATE_SUCCESS,	/* Pages isolated, migrate */
} isolate_migrate_t;

/*
 * Isolate all pages that can be migrated from the block pointed to by
 * the migrate scanner within compact_control.
 */
static isolate_migrate_t isolate_migratepages(struct zone *zone,
					struct compact_control *cc)
{
	unsigned long low_pfn, end_pfn;

	/* Do not scan outside zone boundaries */
	low_pfn = max(cc->migrate_pfn, zone->zone_start_pfn);

	/* Only scan within a pageblock boundary */
	end_pfn = ALIGN(low_pfn + 1, pageblock_nr_pages);

	/* Do not cross the free scanner or scan within a memory hole */
	if (end_pfn > cc->free_pfn || !pfn_valid(low_pfn)) {
		cc->migrate_pfn = end_pfn;
		return ISOLATE_NONE;
	}

	/* Perform the isolation */
	low_pfn = isolate_migratepages_range(zone, cc, low_pfn, end_pfn, false);
	if (!low_pfn || cc->contended)
		return ISOLATE_ABORT;

	cc->migrate_pfn = low_pfn;

	return ISOLATE_SUCCESS;
}

static int compact_finished(struct zone *zone,
			    struct compact_control *cc)
{
	unsigned int order;
	unsigned long watermark;

	if (fatal_signal_pending(current))
		return COMPACT_PARTIAL;

	/* Compaction run completes if the migrate and free scanner meet */
	if (cc->free_pfn <= cc->migrate_pfn) {
		/*
		 * Mark that the PG_migrate_skip information should be cleared
		 * by kswapd when it goes to sleep. kswapd does not set the
		 * flag itself as the decision to be clear should be directly
		 * based on an allocation request.
		 */
		if (!current_is_kswapd())
			zone->compact_blockskip_flush = true;

		return COMPACT_COMPLETE;
	}

	/*
	 * order == -1 is expected when compacting via
	 * /proc/sys/vm/compact_memory
	 */
	if (cc->order == -1)
		return COMPACT_CONTINUE;

	/* Compaction run is not finished if the watermark is not met */
	watermark = low_wmark_pages(zone);
	watermark += (1 << cc->order);

	if (!zone_watermark_ok(zone, cc->order, watermark, 0, 0))
		return COMPACT_CONTINUE;

	/* Direct compactor: Is a suitable page free? */
	for (order = cc->order; order < MAX_ORDER; order++) {
		struct free_area *area = &zone->free_area[order];

		/* Job done if page is free of the right migratetype */
		if (!list_empty(&area->free_list[cc->migratetype]))
			return COMPACT_PARTIAL;

		/* Job done if allocation would set block type */
		if (cc->order >= pageblock_order && area->nr_free)
			return COMPACT_PARTIAL;
	}

	return COMPACT_CONTINUE;
}

/*
 * compaction_suitable: Is this suitable to run compaction on this zone now?
 * Returns
 *   COMPACT_SKIPPED  - If there are too few free pages for compaction
 *   COMPACT_PARTIAL  - If the allocation would succeed without compaction
 *   COMPACT_CONTINUE - If compaction should run now
 */
unsigned long compaction_suitable(struct zone *zone, int order)
{
	int fragindex;
	unsigned long watermark;

	/*
	 * order == -1 is expected when compacting via
	 * /proc/sys/vm/compact_memory
	 */
	if (order == -1)
		return COMPACT_CONTINUE;

	/*
	 * Watermarks for order-0 must be met for compaction. Note the 2UL.
	 * This is because during migration, copies of pages need to be
	 * allocated and for a short time, the footprint is higher
	 */
	watermark = low_wmark_pages(zone) + (2UL << order);
	if (!zone_watermark_ok(zone, 0, watermark, 0, 0))
		return COMPACT_SKIPPED;

	/*
	 * fragmentation index determines if allocation failures are due to
	 * low memory or external fragmentation
	 *
	 * index of -1000 implies allocations might succeed depending on
	 * watermarks
	 * index towards 0 implies failure is due to lack of memory
	 * index towards 1000 implies failure is due to fragmentation
	 *
	 * Only compact if a failure would be due to fragmentation.
	 */
	fragindex = fragmentation_index(zone, order);
	if (fragindex >= 0 && fragindex <= sysctl_extfrag_threshold)
		return COMPACT_SKIPPED;

	if (fragindex == -1000 && zone_watermark_ok(zone, order, watermark,
	    0, 0))
		return COMPACT_PARTIAL;

	return COMPACT_CONTINUE;
}

static int compact_zone(struct zone *zone, struct compact_control *cc)
{
	int ret;
	unsigned long start_pfn = zone->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(zone);

	ret = compaction_suitable(zone, cc->order);
	switch (ret) {
	case COMPACT_PARTIAL:
	case COMPACT_SKIPPED:
		/* Compaction is likely to fail */
		return ret;
	case COMPACT_CONTINUE:
		/* Fall through to compaction */
		;
	}

	/*
	 * Setup to move all movable pages to the end of the zone. Used cached
	 * information on where the scanners should start but check that it
	 * is initialised by ensuring the values are within zone boundaries.
	 */
	cc->migrate_pfn = zone->compact_cached_migrate_pfn;
	cc->free_pfn = zone->compact_cached_free_pfn;
	if (cc->free_pfn < start_pfn || cc->free_pfn > end_pfn) {
		cc->free_pfn = end_pfn & ~(pageblock_nr_pages-1);
		zone->compact_cached_free_pfn = cc->free_pfn;
	}
	if (cc->migrate_pfn < start_pfn || cc->migrate_pfn > end_pfn) {
		cc->migrate_pfn = start_pfn;
		zone->compact_cached_migrate_pfn = cc->migrate_pfn;
	}

	/*
	 * Clear pageblock skip if there were failures recently and compaction
	 * is about to be retried after being deferred. kswapd does not do
	 * this reset as it'll reset the cached information when going to sleep.
	 */
	if (compaction_restarting(zone, cc->order) && !current_is_kswapd())
		__reset_isolation_suitable(zone);

	migrate_prep_local();

	while ((ret = compact_finished(zone, cc)) == COMPACT_CONTINUE) {
		unsigned long nr_migrate, nr_remaining;
		int err;

		switch (isolate_migratepages(zone, cc)) {
		case ISOLATE_ABORT:
			ret = COMPACT_PARTIAL;
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			goto out;
		case ISOLATE_NONE:
			continue;
		case ISOLATE_SUCCESS:
			;
		}

		nr_migrate = cc->nr_migratepages;
		err = migrate_pages(&cc->migratepages, compaction_alloc,
				(unsigned long)cc,
				cc->sync ? MIGRATE_SYNC_LIGHT : MIGRATE_ASYNC,
				MR_COMPACTION);
		update_nr_listpages(cc);
		nr_remaining = cc->nr_migratepages;

		trace_mm_compaction_migratepages(nr_migrate - nr_remaining,
						nr_remaining);

		/* Release isolated pages not migrated */
		if (err) {
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			if (err == -ENOMEM) {
				ret = COMPACT_PARTIAL;
				goto out;
			}
		}
	}

out:
	/* Release free pages and check accounting */
	cc->nr_freepages -= release_freepages(&cc->freepages);
	VM_BUG_ON(cc->nr_freepages != 0);

	return ret;
}

static unsigned long compact_zone_order(struct zone *zone,
				 int order, gfp_t gfp_mask,
				 bool sync, bool *contended)
{
	unsigned long ret;
	struct compact_control cc = {
		.nr_freepages = 0,
		.nr_migratepages = 0,
		.order = order,
		.migratetype = allocflags_to_migratetype(gfp_mask),
		.zone = zone,
		.sync = sync,
	};
	INIT_LIST_HEAD(&cc.freepages);
	INIT_LIST_HEAD(&cc.migratepages);

	ret = compact_zone(zone, &cc);

	VM_BUG_ON(!list_empty(&cc.freepages));
	VM_BUG_ON(!list_empty(&cc.migratepages));

	*contended = cc.contended;
	return ret;
}

int sysctl_extfrag_threshold = 200;

/**
 * try_to_compact_pages - Direct compact to satisfy a high-order allocation
 * @zonelist: The zonelist used for the current allocation
 * @order: The order of the current allocation
 * @gfp_mask: The GFP mask of the current allocation
 * @nodemask: The allowed nodes to allocate from
 * @sync: Whether migration is synchronous or not
 * @contended: Return value that is true if compaction was aborted due to lock contention
 * @page: Optionally capture a free page of the requested order during compaction
 *
 * This is the main entry point for direct page compaction.
 */
unsigned long try_to_compact_pages(struct zonelist *zonelist,
			int order, gfp_t gfp_mask, nodemask_t *nodemask,
			bool sync, bool *contended)
{
	enum zone_type high_zoneidx = gfp_zone(gfp_mask);
	int may_enter_fs = gfp_mask & __GFP_FS;
	int may_perform_io = gfp_mask & __GFP_IO;
	struct zoneref *z;
	struct zone *zone;
	int rc = COMPACT_SKIPPED;
	int alloc_flags = 0;

	/* Check if the GFP flags allow compaction */
	if (!order || !may_enter_fs || !may_perform_io)
		return rc;

	count_compact_event(COMPACTSTALL);

#ifdef CONFIG_CMA
	if (allocflags_to_migratetype(gfp_mask) == MIGRATE_MOVABLE)
		alloc_flags |= ALLOC_CMA;
#endif
	/* Compact each zone in the list */
	for_each_zone_zonelist_nodemask(zone, z, zonelist, high_zoneidx,
								nodemask) {
		int status;

		status = compact_zone_order(zone, order, gfp_mask, sync,
						contended);
		rc = max(status, rc);

		/* If a normal allocation would succeed, stop compacting */
		if (zone_watermark_ok(zone, order, low_wmark_pages(zone), 0,
				      alloc_flags))
			break;
	}

	return rc;
}


/* Compact all zones within a node */
static void __compact_pgdat(pg_data_t *pgdat, struct compact_control *cc)
{
	int zoneid;
	struct zone *zone;

	for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {

		zone = &pgdat->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		cc->nr_freepages = 0;
		cc->nr_migratepages = 0;
		cc->zone = zone;
		INIT_LIST_HEAD(&cc->freepages);
		INIT_LIST_HEAD(&cc->migratepages);

		if (cc->order == -1 || !compaction_deferred(zone, cc->order))
			compact_zone(zone, cc);

		if (cc->order > 0) {
			int ok = zone_watermark_ok(zone, cc->order,
						low_wmark_pages(zone), 0, 0);
			if (ok && cc->order >= zone->compact_order_failed)
				zone->compact_order_failed = cc->order + 1;
			/* Currently async compaction is never deferred. */
			else if (!ok && cc->sync)
				defer_compaction(zone, cc->order);
		}

		VM_BUG_ON(!list_empty(&cc->freepages));
		VM_BUG_ON(!list_empty(&cc->migratepages));
	}
}

void compact_pgdat(pg_data_t *pgdat, int order)
{
	struct compact_control cc = {
		.order = order,
		.sync = false,
	};

	__compact_pgdat(pgdat, &cc);
}

static void compact_node(int nid)
{
	struct compact_control cc = {
		.order = -1,
		.sync = true,
	};

	__compact_pgdat(NODE_DATA(nid), &cc);
}

/* Compact all nodes in the system */
static void compact_nodes(void)
{
	int nid;

	/* Flush pending updates to the LRU lists */
	lru_add_drain_all();

	for_each_online_node(nid)
		compact_node(nid);
}

/* The written value is actually unused, all memory is compacted */
int sysctl_compact_memory;

/* This is the entry point for compacting all nodes via /proc/sys/vm */
int sysctl_compaction_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *length, loff_t *ppos)
{
	if (write)
		compact_nodes();

	return 0;
}

int sysctl_extfrag_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec_minmax(table, write, buffer, length, ppos);

	return 0;
}

#if defined(CONFIG_SYSFS) && defined(CONFIG_NUMA)
ssize_t sysfs_compact_node(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int nid = dev->id;

	if (nid >= 0 && nid < nr_node_ids && node_online(nid)) {
		/* Flush pending updates to the LRU lists */
		lru_add_drain_all();

		compact_node(nid);
	}

	return count;
}
static DEVICE_ATTR(compact, S_IWUSR, NULL, sysfs_compact_node);

int compaction_register_node(struct node *node)
{
	return device_create_file(&node->dev, &dev_attr_compact);
}

void compaction_unregister_node(struct node *node)
{
	return device_remove_file(&node->dev, &dev_attr_compact);
}
#endif /* CONFIG_SYSFS && CONFIG_NUMA */

#endif /* CONFIG_COMPACTION */
