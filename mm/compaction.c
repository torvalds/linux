// SPDX-License-Identifier: GPL-2.0
/*
 * linux/mm/compaction.c
 *
 * Memory compaction for the reduction of external fragmentation. Note that
 * this heavily depends upon page migration to do all the real heavy
 * lifting
 *
 * Copyright IBM Corp. 2007-2010 Mel Gorman <mel@csn.ul.ie>
 */
#include <linux/cpu.h>
#include <linux/swap.h>
#include <linux/migrate.h>
#include <linux/compaction.h>
#include <linux/mm_inline.h>
#include <linux/sched/signal.h>
#include <linux/backing-dev.h>
#include <linux/sysctl.h>
#include <linux/sysfs.h>
#include <linux/page-isolation.h>
#include <linux/kasan.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/page_owner.h>
#include <linux/psi.h>
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

#define block_start_pfn(pfn, order)	round_down(pfn, 1UL << (order))
#define block_end_pfn(pfn, order)	ALIGN((pfn) + 1, 1UL << (order))
#define pageblock_start_pfn(pfn)	block_start_pfn(pfn, pageblock_order)
#define pageblock_end_pfn(pfn)		block_end_pfn(pfn, pageblock_order)

/*
 * Fragmentation score check interval for proactive compaction purposes.
 */
static const unsigned int HPAGE_FRAG_CHECK_INTERVAL_MSEC = 500;

/*
 * Page order with-respect-to which proactive compaction
 * calculates external fragmentation, which is used as
 * the "fragmentation score" of a node/zone.
 */
#if defined CONFIG_TRANSPARENT_HUGEPAGE
#define COMPACTION_HPAGE_ORDER	HPAGE_PMD_ORDER
#elif defined CONFIG_HUGETLBFS
#define COMPACTION_HPAGE_ORDER	HUGETLB_PAGE_ORDER
#else
#define COMPACTION_HPAGE_ORDER	(PMD_SHIFT - PAGE_SHIFT)
#endif

static unsigned long release_freepages(struct list_head *freelist)
{
	struct page *page, *next;
	unsigned long high_pfn = 0;

	list_for_each_entry_safe(page, next, freelist, lru) {
		unsigned long pfn = page_to_pfn(page);
		list_del(&page->lru);
		__free_page(page);
		if (pfn > high_pfn)
			high_pfn = pfn;
	}

	return high_pfn;
}

static void split_map_pages(struct list_head *list)
{
	unsigned int i, order, nr_pages;
	struct page *page, *next;
	LIST_HEAD(tmp_list);

	list_for_each_entry_safe(page, next, list, lru) {
		list_del(&page->lru);

		order = page_private(page);
		nr_pages = 1 << order;

		post_alloc_hook(page, order, __GFP_MOVABLE);
		if (order)
			split_page(page, order);

		for (i = 0; i < nr_pages; i++) {
			list_add(&page->lru, &tmp_list);
			page++;
		}
	}

	list_splice(&tmp_list, list);
}

#ifdef CONFIG_COMPACTION

int PageMovable(struct page *page)
{
	struct address_space *mapping;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	if (!__PageMovable(page))
		return 0;

	mapping = page_mapping(page);
	if (mapping && mapping->a_ops && mapping->a_ops->isolate_page)
		return 1;

	return 0;
}
EXPORT_SYMBOL(PageMovable);

void __SetPageMovable(struct page *page, struct address_space *mapping)
{
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE((unsigned long)mapping & PAGE_MAPPING_MOVABLE, page);
	page->mapping = (void *)((unsigned long)mapping | PAGE_MAPPING_MOVABLE);
}
EXPORT_SYMBOL(__SetPageMovable);

void __ClearPageMovable(struct page *page)
{
	VM_BUG_ON_PAGE(!PageMovable(page), page);
	/*
	 * Clear registered address_space val with keeping PAGE_MAPPING_MOVABLE
	 * flag so that VM can catch up released page by driver after isolation.
	 * With it, VM migration doesn't try to put it back.
	 */
	page->mapping = (void *)((unsigned long)page->mapping &
				PAGE_MAPPING_MOVABLE);
}
EXPORT_SYMBOL(__ClearPageMovable);

/* Do not skip compaction more than 64 times */
#define COMPACT_MAX_DEFER_SHIFT 6

/*
 * Compaction is deferred when compaction fails to result in a page
 * allocation success. 1 << compact_defer_shift, compactions are skipped up
 * to a limit of 1 << COMPACT_MAX_DEFER_SHIFT
 */
static void defer_compaction(struct zone *zone, int order)
{
	zone->compact_considered = 0;
	zone->compact_defer_shift++;

	if (order < zone->compact_order_failed)
		zone->compact_order_failed = order;

	if (zone->compact_defer_shift > COMPACT_MAX_DEFER_SHIFT)
		zone->compact_defer_shift = COMPACT_MAX_DEFER_SHIFT;

	trace_mm_compaction_defer_compaction(zone, order);
}

/* Returns true if compaction should be skipped this time */
static bool compaction_deferred(struct zone *zone, int order)
{
	unsigned long defer_limit = 1UL << zone->compact_defer_shift;

	if (order < zone->compact_order_failed)
		return false;

	/* Avoid possible overflow */
	if (++zone->compact_considered >= defer_limit) {
		zone->compact_considered = defer_limit;
		return false;
	}

	trace_mm_compaction_deferred(zone, order);

	return true;
}

/*
 * Update defer tracking counters after successful compaction of given order,
 * which means an allocation either succeeded (alloc_success == true) or is
 * expected to succeed.
 */
void compaction_defer_reset(struct zone *zone, int order,
		bool alloc_success)
{
	if (alloc_success) {
		zone->compact_considered = 0;
		zone->compact_defer_shift = 0;
	}
	if (order >= zone->compact_order_failed)
		zone->compact_order_failed = order + 1;

	trace_mm_compaction_defer_reset(zone, order);
}

/* Returns true if restarting compaction after many failures */
static bool compaction_restarting(struct zone *zone, int order)
{
	if (order < zone->compact_order_failed)
		return false;

	return zone->compact_defer_shift == COMPACT_MAX_DEFER_SHIFT &&
		zone->compact_considered >= 1UL << zone->compact_defer_shift;
}

/* Returns true if the pageblock should be scanned for pages to isolate. */
static inline bool isolation_suitable(struct compact_control *cc,
					struct page *page)
{
	if (cc->ignore_skip_hint)
		return true;

	return !get_pageblock_skip(page);
}

static void reset_cached_positions(struct zone *zone)
{
	zone->compact_cached_migrate_pfn[0] = zone->zone_start_pfn;
	zone->compact_cached_migrate_pfn[1] = zone->zone_start_pfn;
	zone->compact_cached_free_pfn =
				pageblock_start_pfn(zone_end_pfn(zone) - 1);
}

/*
 * Compound pages of >= pageblock_order should consistently be skipped until
 * released. It is always pointless to compact pages of such order (if they are
 * migratable), and the pageblocks they occupy cannot contain any free pages.
 */
static bool pageblock_skip_persistent(struct page *page)
{
	if (!PageCompound(page))
		return false;

	page = compound_head(page);

	if (compound_order(page) >= pageblock_order)
		return true;

	return false;
}

static bool
__reset_isolation_pfn(struct zone *zone, unsigned long pfn, bool check_source,
							bool check_target)
{
	struct page *page = pfn_to_online_page(pfn);
	struct page *block_page;
	struct page *end_page;
	unsigned long block_pfn;

	if (!page)
		return false;
	if (zone != page_zone(page))
		return false;
	if (pageblock_skip_persistent(page))
		return false;

	/*
	 * If skip is already cleared do no further checking once the
	 * restart points have been set.
	 */
	if (check_source && check_target && !get_pageblock_skip(page))
		return true;

	/*
	 * If clearing skip for the target scanner, do not select a
	 * non-movable pageblock as the starting point.
	 */
	if (!check_source && check_target &&
	    get_pageblock_migratetype(page) != MIGRATE_MOVABLE)
		return false;

	/* Ensure the start of the pageblock or zone is online and valid */
	block_pfn = pageblock_start_pfn(pfn);
	block_pfn = max(block_pfn, zone->zone_start_pfn);
	block_page = pfn_to_online_page(block_pfn);
	if (block_page) {
		page = block_page;
		pfn = block_pfn;
	}

	/* Ensure the end of the pageblock or zone is online and valid */
	block_pfn = pageblock_end_pfn(pfn) - 1;
	block_pfn = min(block_pfn, zone_end_pfn(zone) - 1);
	end_page = pfn_to_online_page(block_pfn);
	if (!end_page)
		return false;

	/*
	 * Only clear the hint if a sample indicates there is either a
	 * free page or an LRU page in the block. One or other condition
	 * is necessary for the block to be a migration source/target.
	 */
	do {
		if (check_source && PageLRU(page)) {
			clear_pageblock_skip(page);
			return true;
		}

		if (check_target && PageBuddy(page)) {
			clear_pageblock_skip(page);
			return true;
		}

		page += (1 << PAGE_ALLOC_COSTLY_ORDER);
		pfn += (1 << PAGE_ALLOC_COSTLY_ORDER);
	} while (page <= end_page);

	return false;
}

/*
 * This function is called to clear all cached information on pageblocks that
 * should be skipped for page isolation when the migrate and free page scanner
 * meet.
 */
static void __reset_isolation_suitable(struct zone *zone)
{
	unsigned long migrate_pfn = zone->zone_start_pfn;
	unsigned long free_pfn = zone_end_pfn(zone) - 1;
	unsigned long reset_migrate = free_pfn;
	unsigned long reset_free = migrate_pfn;
	bool source_set = false;
	bool free_set = false;

	if (!zone->compact_blockskip_flush)
		return;

	zone->compact_blockskip_flush = false;

	/*
	 * Walk the zone and update pageblock skip information. Source looks
	 * for PageLRU while target looks for PageBuddy. When the scanner
	 * is found, both PageBuddy and PageLRU are checked as the pageblock
	 * is suitable as both source and target.
	 */
	for (; migrate_pfn < free_pfn; migrate_pfn += pageblock_nr_pages,
					free_pfn -= pageblock_nr_pages) {
		cond_resched();

		/* Update the migrate PFN */
		if (__reset_isolation_pfn(zone, migrate_pfn, true, source_set) &&
		    migrate_pfn < reset_migrate) {
			source_set = true;
			reset_migrate = migrate_pfn;
			zone->compact_init_migrate_pfn = reset_migrate;
			zone->compact_cached_migrate_pfn[0] = reset_migrate;
			zone->compact_cached_migrate_pfn[1] = reset_migrate;
		}

		/* Update the free PFN */
		if (__reset_isolation_pfn(zone, free_pfn, free_set, true) &&
		    free_pfn > reset_free) {
			free_set = true;
			reset_free = free_pfn;
			zone->compact_init_free_pfn = reset_free;
			zone->compact_cached_free_pfn = reset_free;
		}
	}

	/* Leave no distance if no suitable block was reset */
	if (reset_migrate >= reset_free) {
		zone->compact_cached_migrate_pfn[0] = migrate_pfn;
		zone->compact_cached_migrate_pfn[1] = migrate_pfn;
		zone->compact_cached_free_pfn = free_pfn;
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
 * Sets the pageblock skip bit if it was clear. Note that this is a hint as
 * locks are not required for read/writers. Returns true if it was already set.
 */
static bool test_and_set_skip(struct compact_control *cc, struct page *page,
							unsigned long pfn)
{
	bool skip;

	/* Do no update if skip hint is being ignored */
	if (cc->ignore_skip_hint)
		return false;

	if (!IS_ALIGNED(pfn, pageblock_nr_pages))
		return false;

	skip = get_pageblock_skip(page);
	if (!skip && !cc->no_set_skip_hint)
		set_pageblock_skip(page);

	return skip;
}

static void update_cached_migrate(struct compact_control *cc, unsigned long pfn)
{
	struct zone *zone = cc->zone;

	pfn = pageblock_end_pfn(pfn);

	/* Set for isolation rather than compaction */
	if (cc->no_set_skip_hint)
		return;

	if (pfn > zone->compact_cached_migrate_pfn[0])
		zone->compact_cached_migrate_pfn[0] = pfn;
	if (cc->mode != MIGRATE_ASYNC &&
	    pfn > zone->compact_cached_migrate_pfn[1])
		zone->compact_cached_migrate_pfn[1] = pfn;
}

/*
 * If no pages were isolated then mark this pageblock to be skipped in the
 * future. The information is later cleared by __reset_isolation_suitable().
 */
static void update_pageblock_skip(struct compact_control *cc,
			struct page *page, unsigned long pfn)
{
	struct zone *zone = cc->zone;

	if (cc->no_set_skip_hint)
		return;

	if (!page)
		return;

	set_pageblock_skip(page);

	/* Update where async and sync compaction should restart */
	if (pfn < zone->compact_cached_free_pfn)
		zone->compact_cached_free_pfn = pfn;
}
#else
static inline bool isolation_suitable(struct compact_control *cc,
					struct page *page)
{
	return true;
}

static inline bool pageblock_skip_persistent(struct page *page)
{
	return false;
}

static inline void update_pageblock_skip(struct compact_control *cc,
			struct page *page, unsigned long pfn)
{
}

static void update_cached_migrate(struct compact_control *cc, unsigned long pfn)
{
}

static bool test_and_set_skip(struct compact_control *cc, struct page *page,
							unsigned long pfn)
{
	return false;
}
#endif /* CONFIG_COMPACTION */

/*
 * Compaction requires the taking of some coarse locks that are potentially
 * very heavily contended. For async compaction, trylock and record if the
 * lock is contended. The lock will still be acquired but compaction will
 * abort when the current block is finished regardless of success rate.
 * Sync compaction acquires the lock.
 *
 * Always returns true which makes it easier to track lock state in callers.
 */
static bool compact_lock_irqsave(spinlock_t *lock, unsigned long *flags,
						struct compact_control *cc)
	__acquires(lock)
{
	/* Track if the lock is contended in async mode */
	if (cc->mode == MIGRATE_ASYNC && !cc->contended) {
		if (spin_trylock_irqsave(lock, *flags))
			return true;

		cc->contended = true;
	}

	spin_lock_irqsave(lock, *flags);
	return true;
}

/*
 * Compaction requires the taking of some coarse locks that are potentially
 * very heavily contended. The lock should be periodically unlocked to avoid
 * having disabled IRQs for a long time, even when there is nobody waiting on
 * the lock. It might also be that allowing the IRQs will result in
 * need_resched() becoming true. If scheduling is needed, async compaction
 * aborts. Sync compaction schedules.
 * Either compaction type will also abort if a fatal signal is pending.
 * In either case if the lock was locked, it is dropped and not regained.
 *
 * Returns true if compaction should abort due to fatal signal pending, or
 *		async compaction due to need_resched()
 * Returns false when compaction can continue (sync compaction might have
 *		scheduled)
 */
static bool compact_unlock_should_abort(spinlock_t *lock,
		unsigned long flags, bool *locked, struct compact_control *cc)
{
	if (*locked) {
		spin_unlock_irqrestore(lock, flags);
		*locked = false;
	}

	if (fatal_signal_pending(current)) {
		cc->contended = true;
		return true;
	}

	cond_resched();

	return false;
}

/*
 * Isolate free pages onto a private freelist. If @strict is true, will abort
 * returning 0 on any invalid PFNs or non-free pages inside of the pageblock
 * (even though it may still end up isolating some pages).
 */
static unsigned long isolate_freepages_block(struct compact_control *cc,
				unsigned long *start_pfn,
				unsigned long end_pfn,
				struct list_head *freelist,
				unsigned int stride,
				bool strict)
{
	int nr_scanned = 0, total_isolated = 0;
	struct page *cursor;
	unsigned long flags = 0;
	bool locked = false;
	unsigned long blockpfn = *start_pfn;
	unsigned int order;

	/* Strict mode is for isolation, speed is secondary */
	if (strict)
		stride = 1;

	cursor = pfn_to_page(blockpfn);

	/* Isolate free pages. */
	for (; blockpfn < end_pfn; blockpfn += stride, cursor += stride) {
		int isolated;
		struct page *page = cursor;

		/*
		 * Periodically drop the lock (if held) regardless of its
		 * contention, to give chance to IRQs. Abort if fatal signal
		 * pending or async compaction detects need_resched()
		 */
		if (!(blockpfn % SWAP_CLUSTER_MAX)
		    && compact_unlock_should_abort(&cc->zone->lock, flags,
								&locked, cc))
			break;

		nr_scanned++;

		/*
		 * For compound pages such as THP and hugetlbfs, we can save
		 * potentially a lot of iterations if we skip them at once.
		 * The check is racy, but we can consider only valid values
		 * and the only danger is skipping too much.
		 */
		if (PageCompound(page)) {
			const unsigned int order = compound_order(page);

			if (likely(order < MAX_ORDER)) {
				blockpfn += (1UL << order) - 1;
				cursor += (1UL << order) - 1;
			}
			goto isolate_fail;
		}

		if (!PageBuddy(page))
			goto isolate_fail;

		/*
		 * If we already hold the lock, we can skip some rechecking.
		 * Note that if we hold the lock now, checked_pageblock was
		 * already set in some previous iteration (or strict is true),
		 * so it is correct to skip the suitable migration target
		 * recheck as well.
		 */
		if (!locked) {
			locked = compact_lock_irqsave(&cc->zone->lock,
								&flags, cc);

			/* Recheck this is a buddy page under lock */
			if (!PageBuddy(page))
				goto isolate_fail;
		}

		/* Found a free page, will break it into order-0 pages */
		order = buddy_order(page);
		isolated = __isolate_free_page(page, order);
		if (!isolated)
			break;
		set_page_private(page, order);

		total_isolated += isolated;
		cc->nr_freepages += isolated;
		list_add_tail(&page->lru, freelist);

		if (!strict && cc->nr_migratepages <= cc->nr_freepages) {
			blockpfn += isolated;
			break;
		}
		/* Advance to the end of split page */
		blockpfn += isolated - 1;
		cursor += isolated - 1;
		continue;

isolate_fail:
		if (strict)
			break;
		else
			continue;

	}

	if (locked)
		spin_unlock_irqrestore(&cc->zone->lock, flags);

	/*
	 * There is a tiny chance that we have read bogus compound_order(),
	 * so be careful to not go outside of the pageblock.
	 */
	if (unlikely(blockpfn > end_pfn))
		blockpfn = end_pfn;

	trace_mm_compaction_isolate_freepages(*start_pfn, blockpfn,
					nr_scanned, total_isolated);

	/* Record how far we have got within the block */
	*start_pfn = blockpfn;

	/*
	 * If strict isolation is requested by CMA then check that all the
	 * pages requested were isolated. If there were any failures, 0 is
	 * returned and CMA will fail.
	 */
	if (strict && blockpfn < end_pfn)
		total_isolated = 0;

	cc->total_free_scanned += nr_scanned;
	if (total_isolated)
		count_compact_events(COMPACTISOLATED, total_isolated);
	return total_isolated;
}

/**
 * isolate_freepages_range() - isolate free pages.
 * @cc:        Compaction control structure.
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
	unsigned long isolated, pfn, block_start_pfn, block_end_pfn;
	LIST_HEAD(freelist);

	pfn = start_pfn;
	block_start_pfn = pageblock_start_pfn(pfn);
	if (block_start_pfn < cc->zone->zone_start_pfn)
		block_start_pfn = cc->zone->zone_start_pfn;
	block_end_pfn = pageblock_end_pfn(pfn);

	for (; pfn < end_pfn; pfn += isolated,
				block_start_pfn = block_end_pfn,
				block_end_pfn += pageblock_nr_pages) {
		/* Protect pfn from changing by isolate_freepages_block */
		unsigned long isolate_start_pfn = pfn;

		block_end_pfn = min(block_end_pfn, end_pfn);

		/*
		 * pfn could pass the block_end_pfn if isolated freepage
		 * is more than pageblock order. In this case, we adjust
		 * scanning range to right one.
		 */
		if (pfn >= block_end_pfn) {
			block_start_pfn = pageblock_start_pfn(pfn);
			block_end_pfn = pageblock_end_pfn(pfn);
			block_end_pfn = min(block_end_pfn, end_pfn);
		}

		if (!pageblock_pfn_to_page(block_start_pfn,
					block_end_pfn, cc->zone))
			break;

		isolated = isolate_freepages_block(cc, &isolate_start_pfn,
					block_end_pfn, &freelist, 0, true);

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

	/* __isolate_free_page() does not map the pages */
	split_map_pages(&freelist);

	if (pfn < end_pfn) {
		/* Loop terminated early, cleanup. */
		release_freepages(&freelist);
		return 0;
	}

	/* We don't use freelists for anything. */
	return pfn;
}

/* Similar to reclaim, but different enough that they don't share logic */
static bool too_many_isolated(pg_data_t *pgdat)
{
	unsigned long active, inactive, isolated;

	inactive = node_page_state(pgdat, NR_INACTIVE_FILE) +
			node_page_state(pgdat, NR_INACTIVE_ANON);
	active = node_page_state(pgdat, NR_ACTIVE_FILE) +
			node_page_state(pgdat, NR_ACTIVE_ANON);
	isolated = node_page_state(pgdat, NR_ISOLATED_FILE) +
			node_page_state(pgdat, NR_ISOLATED_ANON);

	return isolated > (inactive + active) / 2;
}

/**
 * isolate_migratepages_block() - isolate all migrate-able pages within
 *				  a single pageblock
 * @cc:		Compaction control structure.
 * @low_pfn:	The first PFN to isolate
 * @end_pfn:	The one-past-the-last PFN to isolate, within same pageblock
 * @isolate_mode: Isolation mode to be used.
 *
 * Isolate all pages that can be migrated from the range specified by
 * [low_pfn, end_pfn). The range is expected to be within same pageblock.
 * Returns errno, like -EAGAIN or -EINTR in case e.g signal pending or congestion,
 * -ENOMEM in case we could not allocate a page, or 0.
 * cc->migrate_pfn will contain the next pfn to scan.
 *
 * The pages are isolated on cc->migratepages list (not required to be empty),
 * and cc->nr_migratepages is updated accordingly.
 */
static int
isolate_migratepages_block(struct compact_control *cc, unsigned long low_pfn,
			unsigned long end_pfn, isolate_mode_t isolate_mode)
{
	pg_data_t *pgdat = cc->zone->zone_pgdat;
	unsigned long nr_scanned = 0, nr_isolated = 0;
	struct lruvec *lruvec;
	unsigned long flags = 0;
	struct lruvec *locked = NULL;
	struct page *page = NULL, *valid_page = NULL;
	unsigned long start_pfn = low_pfn;
	bool skip_on_failure = false;
	unsigned long next_skip_pfn = 0;
	bool skip_updated = false;
	int ret = 0;

	cc->migrate_pfn = low_pfn;

	/*
	 * Ensure that there are not too many pages isolated from the LRU
	 * list by either parallel reclaimers or compaction. If there are,
	 * delay for some time until fewer pages are isolated
	 */
	while (unlikely(too_many_isolated(pgdat))) {
		/* stop isolation if there are still pages not migrated */
		if (cc->nr_migratepages)
			return -EAGAIN;

		/* async migration should just abort */
		if (cc->mode == MIGRATE_ASYNC)
			return -EAGAIN;

		congestion_wait(BLK_RW_ASYNC, HZ/10);

		if (fatal_signal_pending(current))
			return -EINTR;
	}

	cond_resched();

	if (cc->direct_compaction && (cc->mode == MIGRATE_ASYNC)) {
		skip_on_failure = true;
		next_skip_pfn = block_end_pfn(low_pfn, cc->order);
	}

	/* Time to isolate some pages for migration */
	for (; low_pfn < end_pfn; low_pfn++) {

		if (skip_on_failure && low_pfn >= next_skip_pfn) {
			/*
			 * We have isolated all migration candidates in the
			 * previous order-aligned block, and did not skip it due
			 * to failure. We should migrate the pages now and
			 * hopefully succeed compaction.
			 */
			if (nr_isolated)
				break;

			/*
			 * We failed to isolate in the previous order-aligned
			 * block. Set the new boundary to the end of the
			 * current block. Note we can't simply increase
			 * next_skip_pfn by 1 << order, as low_pfn might have
			 * been incremented by a higher number due to skipping
			 * a compound or a high-order buddy page in the
			 * previous loop iteration.
			 */
			next_skip_pfn = block_end_pfn(low_pfn, cc->order);
		}

		/*
		 * Periodically drop the lock (if held) regardless of its
		 * contention, to give chance to IRQs. Abort completely if
		 * a fatal signal is pending.
		 */
		if (!(low_pfn % SWAP_CLUSTER_MAX)) {
			if (locked) {
				unlock_page_lruvec_irqrestore(locked, flags);
				locked = NULL;
			}

			if (fatal_signal_pending(current)) {
				cc->contended = true;
				ret = -EINTR;

				goto fatal_pending;
			}

			cond_resched();
		}

		nr_scanned++;

		page = pfn_to_page(low_pfn);

		/*
		 * Check if the pageblock has already been marked skipped.
		 * Only the aligned PFN is checked as the caller isolates
		 * COMPACT_CLUSTER_MAX at a time so the second call must
		 * not falsely conclude that the block should be skipped.
		 */
		if (!valid_page && IS_ALIGNED(low_pfn, pageblock_nr_pages)) {
			if (!cc->ignore_skip_hint && get_pageblock_skip(page)) {
				low_pfn = end_pfn;
				page = NULL;
				goto isolate_abort;
			}
			valid_page = page;
		}

		if (PageHuge(page) && cc->alloc_contig) {
			ret = isolate_or_dissolve_huge_page(page, &cc->migratepages);

			/*
			 * Fail isolation in case isolate_or_dissolve_huge_page()
			 * reports an error. In case of -ENOMEM, abort right away.
			 */
			if (ret < 0) {
				 /* Do not report -EBUSY down the chain */
				if (ret == -EBUSY)
					ret = 0;
				low_pfn += (1UL << compound_order(page)) - 1;
				goto isolate_fail;
			}

			if (PageHuge(page)) {
				/*
				 * Hugepage was successfully isolated and placed
				 * on the cc->migratepages list.
				 */
				low_pfn += compound_nr(page) - 1;
				goto isolate_success_no_list;
			}

			/*
			 * Ok, the hugepage was dissolved. Now these pages are
			 * Buddy and cannot be re-allocated because they are
			 * isolated. Fall-through as the check below handles
			 * Buddy pages.
			 */
		}

		/*
		 * Skip if free. We read page order here without zone lock
		 * which is generally unsafe, but the race window is small and
		 * the worst thing that can happen is that we skip some
		 * potential isolation targets.
		 */
		if (PageBuddy(page)) {
			unsigned long freepage_order = buddy_order_unsafe(page);

			/*
			 * Without lock, we cannot be sure that what we got is
			 * a valid page order. Consider only values in the
			 * valid order range to prevent low_pfn overflow.
			 */
			if (freepage_order > 0 && freepage_order < MAX_ORDER)
				low_pfn += (1UL << freepage_order) - 1;
			continue;
		}

		/*
		 * Regardless of being on LRU, compound pages such as THP and
		 * hugetlbfs are not to be compacted unless we are attempting
		 * an allocation much larger than the huge page size (eg CMA).
		 * We can potentially save a lot of iterations if we skip them
		 * at once. The check is racy, but we can consider only valid
		 * values and the only danger is skipping too much.
		 */
		if (PageCompound(page) && !cc->alloc_contig) {
			const unsigned int order = compound_order(page);

			if (likely(order < MAX_ORDER))
				low_pfn += (1UL << order) - 1;
			goto isolate_fail;
		}

		/*
		 * Check may be lockless but that's ok as we recheck later.
		 * It's possible to migrate LRU and non-lru movable pages.
		 * Skip any other type of page
		 */
		if (!PageLRU(page)) {
			/*
			 * __PageMovable can return false positive so we need
			 * to verify it under page_lock.
			 */
			if (unlikely(__PageMovable(page)) &&
					!PageIsolated(page)) {
				if (locked) {
					unlock_page_lruvec_irqrestore(locked, flags);
					locked = NULL;
				}

				if (!isolate_movable_page(page, isolate_mode))
					goto isolate_success;
			}

			goto isolate_fail;
		}

		/*
		 * Migration will fail if an anonymous page is pinned in memory,
		 * so avoid taking lru_lock and isolating it unnecessarily in an
		 * admittedly racy check.
		 */
		if (!page_mapping(page) &&
		    page_count(page) > page_mapcount(page))
			goto isolate_fail;

		/*
		 * Only allow to migrate anonymous pages in GFP_NOFS context
		 * because those do not depend on fs locks.
		 */
		if (!(cc->gfp_mask & __GFP_FS) && page_mapping(page))
			goto isolate_fail;

		/*
		 * Be careful not to clear PageLRU until after we're
		 * sure the page is not being freed elsewhere -- the
		 * page release code relies on it.
		 */
		if (unlikely(!get_page_unless_zero(page)))
			goto isolate_fail;

		if (!__isolate_lru_page_prepare(page, isolate_mode))
			goto isolate_fail_put;

		/* Try isolate the page */
		if (!TestClearPageLRU(page))
			goto isolate_fail_put;

		lruvec = mem_cgroup_page_lruvec(page);

		/* If we already hold the lock, we can skip some rechecking */
		if (lruvec != locked) {
			if (locked)
				unlock_page_lruvec_irqrestore(locked, flags);

			compact_lock_irqsave(&lruvec->lru_lock, &flags, cc);
			locked = lruvec;

			lruvec_memcg_debug(lruvec, page);

			/* Try get exclusive access under lock */
			if (!skip_updated) {
				skip_updated = true;
				if (test_and_set_skip(cc, page, low_pfn))
					goto isolate_abort;
			}

			/*
			 * Page become compound since the non-locked check,
			 * and it's on LRU. It can only be a THP so the order
			 * is safe to read and it's 0 for tail pages.
			 */
			if (unlikely(PageCompound(page) && !cc->alloc_contig)) {
				low_pfn += compound_nr(page) - 1;
				SetPageLRU(page);
				goto isolate_fail_put;
			}
		}

		/* The whole page is taken off the LRU; skip the tail pages. */
		if (PageCompound(page))
			low_pfn += compound_nr(page) - 1;

		/* Successfully isolated */
		del_page_from_lru_list(page, lruvec);
		mod_node_page_state(page_pgdat(page),
				NR_ISOLATED_ANON + page_is_file_lru(page),
				thp_nr_pages(page));

isolate_success:
		list_add(&page->lru, &cc->migratepages);
isolate_success_no_list:
		cc->nr_migratepages += compound_nr(page);
		nr_isolated += compound_nr(page);

		/*
		 * Avoid isolating too much unless this block is being
		 * rescanned (e.g. dirty/writeback pages, parallel allocation)
		 * or a lock is contended. For contention, isolate quickly to
		 * potentially remove one source of contention.
		 */
		if (cc->nr_migratepages >= COMPACT_CLUSTER_MAX &&
		    !cc->rescan && !cc->contended) {
			++low_pfn;
			break;
		}

		continue;

isolate_fail_put:
		/* Avoid potential deadlock in freeing page under lru_lock */
		if (locked) {
			unlock_page_lruvec_irqrestore(locked, flags);
			locked = NULL;
		}
		put_page(page);

isolate_fail:
		if (!skip_on_failure && ret != -ENOMEM)
			continue;

		/*
		 * We have isolated some pages, but then failed. Release them
		 * instead of migrating, as we cannot form the cc->order buddy
		 * page anyway.
		 */
		if (nr_isolated) {
			if (locked) {
				unlock_page_lruvec_irqrestore(locked, flags);
				locked = NULL;
			}
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			nr_isolated = 0;
		}

		if (low_pfn < next_skip_pfn) {
			low_pfn = next_skip_pfn - 1;
			/*
			 * The check near the loop beginning would have updated
			 * next_skip_pfn too, but this is a bit simpler.
			 */
			next_skip_pfn += 1UL << cc->order;
		}

		if (ret == -ENOMEM)
			break;
	}

	/*
	 * The PageBuddy() check could have potentially brought us outside
	 * the range to be scanned.
	 */
	if (unlikely(low_pfn > end_pfn))
		low_pfn = end_pfn;

	page = NULL;

isolate_abort:
	if (locked)
		unlock_page_lruvec_irqrestore(locked, flags);
	if (page) {
		SetPageLRU(page);
		put_page(page);
	}

	/*
	 * Updated the cached scanner pfn once the pageblock has been scanned
	 * Pages will either be migrated in which case there is no point
	 * scanning in the near future or migration failed in which case the
	 * failure reason may persist. The block is marked for skipping if
	 * there were no pages isolated in the block or if the block is
	 * rescanned twice in a row.
	 */
	if (low_pfn == end_pfn && (!nr_isolated || cc->rescan)) {
		if (valid_page && !skip_updated)
			set_pageblock_skip(valid_page);
		update_cached_migrate(cc, low_pfn);
	}

	trace_mm_compaction_isolate_migratepages(start_pfn, low_pfn,
						nr_scanned, nr_isolated);

fatal_pending:
	cc->total_migrate_scanned += nr_scanned;
	if (nr_isolated)
		count_compact_events(COMPACTISOLATED, nr_isolated);

	cc->migrate_pfn = low_pfn;

	return ret;
}

/**
 * isolate_migratepages_range() - isolate migrate-able pages in a PFN range
 * @cc:        Compaction control structure.
 * @start_pfn: The first PFN to start isolating.
 * @end_pfn:   The one-past-last PFN.
 *
 * Returns -EAGAIN when contented, -EINTR in case of a signal pending, -ENOMEM
 * in case we could not allocate a page, or 0.
 */
int
isolate_migratepages_range(struct compact_control *cc, unsigned long start_pfn,
							unsigned long end_pfn)
{
	unsigned long pfn, block_start_pfn, block_end_pfn;
	int ret = 0;

	/* Scan block by block. First and last block may be incomplete */
	pfn = start_pfn;
	block_start_pfn = pageblock_start_pfn(pfn);
	if (block_start_pfn < cc->zone->zone_start_pfn)
		block_start_pfn = cc->zone->zone_start_pfn;
	block_end_pfn = pageblock_end_pfn(pfn);

	for (; pfn < end_pfn; pfn = block_end_pfn,
				block_start_pfn = block_end_pfn,
				block_end_pfn += pageblock_nr_pages) {

		block_end_pfn = min(block_end_pfn, end_pfn);

		if (!pageblock_pfn_to_page(block_start_pfn,
					block_end_pfn, cc->zone))
			continue;

		ret = isolate_migratepages_block(cc, pfn, block_end_pfn,
						 ISOLATE_UNEVICTABLE);

		if (ret)
			break;

		if (cc->nr_migratepages >= COMPACT_CLUSTER_MAX)
			break;
	}

	return ret;
}

#endif /* CONFIG_COMPACTION || CONFIG_CMA */
#ifdef CONFIG_COMPACTION

static bool suitable_migration_source(struct compact_control *cc,
							struct page *page)
{
	int block_mt;

	if (pageblock_skip_persistent(page))
		return false;

	if ((cc->mode != MIGRATE_ASYNC) || !cc->direct_compaction)
		return true;

	block_mt = get_pageblock_migratetype(page);

	if (cc->migratetype == MIGRATE_MOVABLE)
		return is_migrate_movable(block_mt);
	else
		return block_mt == cc->migratetype;
}

/* Returns true if the page is within a block suitable for migration to */
static bool suitable_migration_target(struct compact_control *cc,
							struct page *page)
{
	/* If the page is a large free page, then disallow migration */
	if (PageBuddy(page)) {
		/*
		 * We are checking page_order without zone->lock taken. But
		 * the only small danger is that we skip a potentially suitable
		 * pageblock, so it's not worth to check order for valid range.
		 */
		if (buddy_order_unsafe(page) >= pageblock_order)
			return false;
	}

	if (cc->ignore_block_suitable)
		return true;

	/* If the block is MIGRATE_MOVABLE or MIGRATE_CMA, allow migration */
	if (is_migrate_movable(get_pageblock_migratetype(page)))
		return true;

	/* Otherwise skip the block */
	return false;
}

static inline unsigned int
freelist_scan_limit(struct compact_control *cc)
{
	unsigned short shift = BITS_PER_LONG - 1;

	return (COMPACT_CLUSTER_MAX >> min(shift, cc->fast_search_fail)) + 1;
}

/*
 * Test whether the free scanner has reached the same or lower pageblock than
 * the migration scanner, and compaction should thus terminate.
 */
static inline bool compact_scanners_met(struct compact_control *cc)
{
	return (cc->free_pfn >> pageblock_order)
		<= (cc->migrate_pfn >> pageblock_order);
}

/*
 * Used when scanning for a suitable migration target which scans freelists
 * in reverse. Reorders the list such as the unscanned pages are scanned
 * first on the next iteration of the free scanner
 */
static void
move_freelist_head(struct list_head *freelist, struct page *freepage)
{
	LIST_HEAD(sublist);

	if (!list_is_last(freelist, &freepage->lru)) {
		list_cut_before(&sublist, freelist, &freepage->lru);
		list_splice_tail(&sublist, freelist);
	}
}

/*
 * Similar to move_freelist_head except used by the migration scanner
 * when scanning forward. It's possible for these list operations to
 * move against each other if they search the free list exactly in
 * lockstep.
 */
static void
move_freelist_tail(struct list_head *freelist, struct page *freepage)
{
	LIST_HEAD(sublist);

	if (!list_is_first(freelist, &freepage->lru)) {
		list_cut_position(&sublist, freelist, &freepage->lru);
		list_splice_tail(&sublist, freelist);
	}
}

static void
fast_isolate_around(struct compact_control *cc, unsigned long pfn, unsigned long nr_isolated)
{
	unsigned long start_pfn, end_pfn;
	struct page *page;

	/* Do not search around if there are enough pages already */
	if (cc->nr_freepages >= cc->nr_migratepages)
		return;

	/* Minimise scanning during async compaction */
	if (cc->direct_compaction && cc->mode == MIGRATE_ASYNC)
		return;

	/* Pageblock boundaries */
	start_pfn = max(pageblock_start_pfn(pfn), cc->zone->zone_start_pfn);
	end_pfn = min(pageblock_end_pfn(pfn), zone_end_pfn(cc->zone));

	page = pageblock_pfn_to_page(start_pfn, end_pfn, cc->zone);
	if (!page)
		return;

	/* Scan before */
	if (start_pfn != pfn) {
		isolate_freepages_block(cc, &start_pfn, pfn, &cc->freepages, 1, false);
		if (cc->nr_freepages >= cc->nr_migratepages)
			return;
	}

	/* Scan after */
	start_pfn = pfn + nr_isolated;
	if (start_pfn < end_pfn)
		isolate_freepages_block(cc, &start_pfn, end_pfn, &cc->freepages, 1, false);

	/* Skip this pageblock in the future as it's full or nearly full */
	if (cc->nr_freepages < cc->nr_migratepages)
		set_pageblock_skip(page);
}

/* Search orders in round-robin fashion */
static int next_search_order(struct compact_control *cc, int order)
{
	order--;
	if (order < 0)
		order = cc->order - 1;

	/* Search wrapped around? */
	if (order == cc->search_order) {
		cc->search_order--;
		if (cc->search_order < 0)
			cc->search_order = cc->order - 1;
		return -1;
	}

	return order;
}

static unsigned long
fast_isolate_freepages(struct compact_control *cc)
{
	unsigned int limit = max(1U, freelist_scan_limit(cc) >> 1);
	unsigned int nr_scanned = 0;
	unsigned long low_pfn, min_pfn, highest = 0;
	unsigned long nr_isolated = 0;
	unsigned long distance;
	struct page *page = NULL;
	bool scan_start = false;
	int order;

	/* Full compaction passes in a negative order */
	if (cc->order <= 0)
		return cc->free_pfn;

	/*
	 * If starting the scan, use a deeper search and use the highest
	 * PFN found if a suitable one is not found.
	 */
	if (cc->free_pfn >= cc->zone->compact_init_free_pfn) {
		limit = pageblock_nr_pages >> 1;
		scan_start = true;
	}

	/*
	 * Preferred point is in the top quarter of the scan space but take
	 * a pfn from the top half if the search is problematic.
	 */
	distance = (cc->free_pfn - cc->migrate_pfn);
	low_pfn = pageblock_start_pfn(cc->free_pfn - (distance >> 2));
	min_pfn = pageblock_start_pfn(cc->free_pfn - (distance >> 1));

	if (WARN_ON_ONCE(min_pfn > low_pfn))
		low_pfn = min_pfn;

	/*
	 * Search starts from the last successful isolation order or the next
	 * order to search after a previous failure
	 */
	cc->search_order = min_t(unsigned int, cc->order - 1, cc->search_order);

	for (order = cc->search_order;
	     !page && order >= 0;
	     order = next_search_order(cc, order)) {
		struct free_area *area = &cc->zone->free_area[order];
		struct list_head *freelist;
		struct page *freepage;
		unsigned long flags;
		unsigned int order_scanned = 0;
		unsigned long high_pfn = 0;

		if (!area->nr_free)
			continue;

		spin_lock_irqsave(&cc->zone->lock, flags);
		freelist = &area->free_list[MIGRATE_MOVABLE];
		list_for_each_entry_reverse(freepage, freelist, lru) {
			unsigned long pfn;

			order_scanned++;
			nr_scanned++;
			pfn = page_to_pfn(freepage);

			if (pfn >= highest)
				highest = max(pageblock_start_pfn(pfn),
					      cc->zone->zone_start_pfn);

			if (pfn >= low_pfn) {
				cc->fast_search_fail = 0;
				cc->search_order = order;
				page = freepage;
				break;
			}

			if (pfn >= min_pfn && pfn > high_pfn) {
				high_pfn = pfn;

				/* Shorten the scan if a candidate is found */
				limit >>= 1;
			}

			if (order_scanned >= limit)
				break;
		}

		/* Use a minimum pfn if a preferred one was not found */
		if (!page && high_pfn) {
			page = pfn_to_page(high_pfn);

			/* Update freepage for the list reorder below */
			freepage = page;
		}

		/* Reorder to so a future search skips recent pages */
		move_freelist_head(freelist, freepage);

		/* Isolate the page if available */
		if (page) {
			if (__isolate_free_page(page, order)) {
				set_page_private(page, order);
				nr_isolated = 1 << order;
				cc->nr_freepages += nr_isolated;
				list_add_tail(&page->lru, &cc->freepages);
				count_compact_events(COMPACTISOLATED, nr_isolated);
			} else {
				/* If isolation fails, abort the search */
				order = cc->search_order + 1;
				page = NULL;
			}
		}

		spin_unlock_irqrestore(&cc->zone->lock, flags);

		/*
		 * Smaller scan on next order so the total scan is related
		 * to freelist_scan_limit.
		 */
		if (order_scanned >= limit)
			limit = max(1U, limit >> 1);
	}

	if (!page) {
		cc->fast_search_fail++;
		if (scan_start) {
			/*
			 * Use the highest PFN found above min. If one was
			 * not found, be pessimistic for direct compaction
			 * and use the min mark.
			 */
			if (highest) {
				page = pfn_to_page(highest);
				cc->free_pfn = highest;
			} else {
				if (cc->direct_compaction && pfn_valid(min_pfn)) {
					page = pageblock_pfn_to_page(min_pfn,
						min(pageblock_end_pfn(min_pfn),
						    zone_end_pfn(cc->zone)),
						cc->zone);
					cc->free_pfn = min_pfn;
				}
			}
		}
	}

	if (highest && highest >= cc->zone->compact_cached_free_pfn) {
		highest -= pageblock_nr_pages;
		cc->zone->compact_cached_free_pfn = highest;
	}

	cc->total_free_scanned += nr_scanned;
	if (!page)
		return cc->free_pfn;

	low_pfn = page_to_pfn(page);
	fast_isolate_around(cc, low_pfn, nr_isolated);
	return low_pfn;
}

/*
 * Based on information in the current compact_control, find blocks
 * suitable for isolating free pages from and then isolate them.
 */
static void isolate_freepages(struct compact_control *cc)
{
	struct zone *zone = cc->zone;
	struct page *page;
	unsigned long block_start_pfn;	/* start of current pageblock */
	unsigned long isolate_start_pfn; /* exact pfn we start at */
	unsigned long block_end_pfn;	/* end of current pageblock */
	unsigned long low_pfn;	     /* lowest pfn scanner is able to scan */
	struct list_head *freelist = &cc->freepages;
	unsigned int stride;

	/* Try a small search of the free lists for a candidate */
	isolate_start_pfn = fast_isolate_freepages(cc);
	if (cc->nr_freepages)
		goto splitmap;

	/*
	 * Initialise the free scanner. The starting point is where we last
	 * successfully isolated from, zone-cached value, or the end of the
	 * zone when isolating for the first time. For looping we also need
	 * this pfn aligned down to the pageblock boundary, because we do
	 * block_start_pfn -= pageblock_nr_pages in the for loop.
	 * For ending point, take care when isolating in last pageblock of a
	 * zone which ends in the middle of a pageblock.
	 * The low boundary is the end of the pageblock the migration scanner
	 * is using.
	 */
	isolate_start_pfn = cc->free_pfn;
	block_start_pfn = pageblock_start_pfn(isolate_start_pfn);
	block_end_pfn = min(block_start_pfn + pageblock_nr_pages,
						zone_end_pfn(zone));
	low_pfn = pageblock_end_pfn(cc->migrate_pfn);
	stride = cc->mode == MIGRATE_ASYNC ? COMPACT_CLUSTER_MAX : 1;

	/*
	 * Isolate free pages until enough are available to migrate the
	 * pages on cc->migratepages. We stop searching if the migrate
	 * and free page scanners meet or enough free pages are isolated.
	 */
	for (; block_start_pfn >= low_pfn;
				block_end_pfn = block_start_pfn,
				block_start_pfn -= pageblock_nr_pages,
				isolate_start_pfn = block_start_pfn) {
		unsigned long nr_isolated;

		/*
		 * This can iterate a massively long zone without finding any
		 * suitable migration targets, so periodically check resched.
		 */
		if (!(block_start_pfn % (SWAP_CLUSTER_MAX * pageblock_nr_pages)))
			cond_resched();

		page = pageblock_pfn_to_page(block_start_pfn, block_end_pfn,
									zone);
		if (!page)
			continue;

		/* Check the block is suitable for migration */
		if (!suitable_migration_target(cc, page))
			continue;

		/* If isolation recently failed, do not retry */
		if (!isolation_suitable(cc, page))
			continue;

		/* Found a block suitable for isolating free pages from. */
		nr_isolated = isolate_freepages_block(cc, &isolate_start_pfn,
					block_end_pfn, freelist, stride, false);

		/* Update the skip hint if the full pageblock was scanned */
		if (isolate_start_pfn == block_end_pfn)
			update_pageblock_skip(cc, page, block_start_pfn);

		/* Are enough freepages isolated? */
		if (cc->nr_freepages >= cc->nr_migratepages) {
			if (isolate_start_pfn >= block_end_pfn) {
				/*
				 * Restart at previous pageblock if more
				 * freepages can be isolated next time.
				 */
				isolate_start_pfn =
					block_start_pfn - pageblock_nr_pages;
			}
			break;
		} else if (isolate_start_pfn < block_end_pfn) {
			/*
			 * If isolation failed early, do not continue
			 * needlessly.
			 */
			break;
		}

		/* Adjust stride depending on isolation */
		if (nr_isolated) {
			stride = 1;
			continue;
		}
		stride = min_t(unsigned int, COMPACT_CLUSTER_MAX, stride << 1);
	}

	/*
	 * Record where the free scanner will restart next time. Either we
	 * broke from the loop and set isolate_start_pfn based on the last
	 * call to isolate_freepages_block(), or we met the migration scanner
	 * and the loop terminated due to isolate_start_pfn < low_pfn
	 */
	cc->free_pfn = isolate_start_pfn;

splitmap:
	/* __isolate_free_page() does not map the pages */
	split_map_pages(freelist);
}

/*
 * This is a migrate-callback that "allocates" freepages by taking pages
 * from the isolated freelists in the block we are migrating to.
 */
static struct page *compaction_alloc(struct page *migratepage,
					unsigned long data)
{
	struct compact_control *cc = (struct compact_control *)data;
	struct page *freepage;

	if (list_empty(&cc->freepages)) {
		isolate_freepages(cc);

		if (list_empty(&cc->freepages))
			return NULL;
	}

	freepage = list_entry(cc->freepages.next, struct page, lru);
	list_del(&freepage->lru);
	cc->nr_freepages--;

	return freepage;
}

/*
 * This is a migrate-callback that "frees" freepages back to the isolated
 * freelist.  All pages on the freelist are from the same zone, so there is no
 * special handling needed for NUMA.
 */
static void compaction_free(struct page *page, unsigned long data)
{
	struct compact_control *cc = (struct compact_control *)data;

	list_add(&page->lru, &cc->freepages);
	cc->nr_freepages++;
}

/* possible outcome of isolate_migratepages */
typedef enum {
	ISOLATE_ABORT,		/* Abort compaction now */
	ISOLATE_NONE,		/* No pages isolated, continue scanning */
	ISOLATE_SUCCESS,	/* Pages isolated, migrate */
} isolate_migrate_t;

/*
 * Allow userspace to control policy on scanning the unevictable LRU for
 * compactable pages.
 */
#ifdef CONFIG_PREEMPT_RT
int sysctl_compact_unevictable_allowed __read_mostly = 0;
#else
int sysctl_compact_unevictable_allowed __read_mostly = 1;
#endif

static inline void
update_fast_start_pfn(struct compact_control *cc, unsigned long pfn)
{
	if (cc->fast_start_pfn == ULONG_MAX)
		return;

	if (!cc->fast_start_pfn)
		cc->fast_start_pfn = pfn;

	cc->fast_start_pfn = min(cc->fast_start_pfn, pfn);
}

static inline unsigned long
reinit_migrate_pfn(struct compact_control *cc)
{
	if (!cc->fast_start_pfn || cc->fast_start_pfn == ULONG_MAX)
		return cc->migrate_pfn;

	cc->migrate_pfn = cc->fast_start_pfn;
	cc->fast_start_pfn = ULONG_MAX;

	return cc->migrate_pfn;
}

/*
 * Briefly search the free lists for a migration source that already has
 * some free pages to reduce the number of pages that need migration
 * before a pageblock is free.
 */
static unsigned long fast_find_migrateblock(struct compact_control *cc)
{
	unsigned int limit = freelist_scan_limit(cc);
	unsigned int nr_scanned = 0;
	unsigned long distance;
	unsigned long pfn = cc->migrate_pfn;
	unsigned long high_pfn;
	int order;
	bool found_block = false;

	/* Skip hints are relied on to avoid repeats on the fast search */
	if (cc->ignore_skip_hint)
		return pfn;

	/*
	 * If the migrate_pfn is not at the start of a zone or the start
	 * of a pageblock then assume this is a continuation of a previous
	 * scan restarted due to COMPACT_CLUSTER_MAX.
	 */
	if (pfn != cc->zone->zone_start_pfn && pfn != pageblock_start_pfn(pfn))
		return pfn;

	/*
	 * For smaller orders, just linearly scan as the number of pages
	 * to migrate should be relatively small and does not necessarily
	 * justify freeing up a large block for a small allocation.
	 */
	if (cc->order <= PAGE_ALLOC_COSTLY_ORDER)
		return pfn;

	/*
	 * Only allow kcompactd and direct requests for movable pages to
	 * quickly clear out a MOVABLE pageblock for allocation. This
	 * reduces the risk that a large movable pageblock is freed for
	 * an unmovable/reclaimable small allocation.
	 */
	if (cc->direct_compaction && cc->migratetype != MIGRATE_MOVABLE)
		return pfn;

	/*
	 * When starting the migration scanner, pick any pageblock within the
	 * first half of the search space. Otherwise try and pick a pageblock
	 * within the first eighth to reduce the chances that a migration
	 * target later becomes a source.
	 */
	distance = (cc->free_pfn - cc->migrate_pfn) >> 1;
	if (cc->migrate_pfn != cc->zone->zone_start_pfn)
		distance >>= 2;
	high_pfn = pageblock_start_pfn(cc->migrate_pfn + distance);

	for (order = cc->order - 1;
	     order >= PAGE_ALLOC_COSTLY_ORDER && !found_block && nr_scanned < limit;
	     order--) {
		struct free_area *area = &cc->zone->free_area[order];
		struct list_head *freelist;
		unsigned long flags;
		struct page *freepage;

		if (!area->nr_free)
			continue;

		spin_lock_irqsave(&cc->zone->lock, flags);
		freelist = &area->free_list[MIGRATE_MOVABLE];
		list_for_each_entry(freepage, freelist, lru) {
			unsigned long free_pfn;

			if (nr_scanned++ >= limit) {
				move_freelist_tail(freelist, freepage);
				break;
			}

			free_pfn = page_to_pfn(freepage);
			if (free_pfn < high_pfn) {
				/*
				 * Avoid if skipped recently. Ideally it would
				 * move to the tail but even safe iteration of
				 * the list assumes an entry is deleted, not
				 * reordered.
				 */
				if (get_pageblock_skip(freepage))
					continue;

				/* Reorder to so a future search skips recent pages */
				move_freelist_tail(freelist, freepage);

				update_fast_start_pfn(cc, free_pfn);
				pfn = pageblock_start_pfn(free_pfn);
				cc->fast_search_fail = 0;
				found_block = true;
				set_pageblock_skip(freepage);
				break;
			}
		}
		spin_unlock_irqrestore(&cc->zone->lock, flags);
	}

	cc->total_migrate_scanned += nr_scanned;

	/*
	 * If fast scanning failed then use a cached entry for a page block
	 * that had free pages as the basis for starting a linear scan.
	 */
	if (!found_block) {
		cc->fast_search_fail++;
		pfn = reinit_migrate_pfn(cc);
	}
	return pfn;
}

/*
 * Isolate all pages that can be migrated from the first suitable block,
 * starting at the block pointed to by the migrate scanner pfn within
 * compact_control.
 */
static isolate_migrate_t isolate_migratepages(struct compact_control *cc)
{
	unsigned long block_start_pfn;
	unsigned long block_end_pfn;
	unsigned long low_pfn;
	struct page *page;
	const isolate_mode_t isolate_mode =
		(sysctl_compact_unevictable_allowed ? ISOLATE_UNEVICTABLE : 0) |
		(cc->mode != MIGRATE_SYNC ? ISOLATE_ASYNC_MIGRATE : 0);
	bool fast_find_block;

	/*
	 * Start at where we last stopped, or beginning of the zone as
	 * initialized by compact_zone(). The first failure will use
	 * the lowest PFN as the starting point for linear scanning.
	 */
	low_pfn = fast_find_migrateblock(cc);
	block_start_pfn = pageblock_start_pfn(low_pfn);
	if (block_start_pfn < cc->zone->zone_start_pfn)
		block_start_pfn = cc->zone->zone_start_pfn;

	/*
	 * fast_find_migrateblock marks a pageblock skipped so to avoid
	 * the isolation_suitable check below, check whether the fast
	 * search was successful.
	 */
	fast_find_block = low_pfn != cc->migrate_pfn && !cc->fast_search_fail;

	/* Only scan within a pageblock boundary */
	block_end_pfn = pageblock_end_pfn(low_pfn);

	/*
	 * Iterate over whole pageblocks until we find the first suitable.
	 * Do not cross the free scanner.
	 */
	for (; block_end_pfn <= cc->free_pfn;
			fast_find_block = false,
			cc->migrate_pfn = low_pfn = block_end_pfn,
			block_start_pfn = block_end_pfn,
			block_end_pfn += pageblock_nr_pages) {

		/*
		 * This can potentially iterate a massively long zone with
		 * many pageblocks unsuitable, so periodically check if we
		 * need to schedule.
		 */
		if (!(low_pfn % (SWAP_CLUSTER_MAX * pageblock_nr_pages)))
			cond_resched();

		page = pageblock_pfn_to_page(block_start_pfn,
						block_end_pfn, cc->zone);
		if (!page)
			continue;

		/*
		 * If isolation recently failed, do not retry. Only check the
		 * pageblock once. COMPACT_CLUSTER_MAX causes a pageblock
		 * to be visited multiple times. Assume skip was checked
		 * before making it "skip" so other compaction instances do
		 * not scan the same block.
		 */
		if (IS_ALIGNED(low_pfn, pageblock_nr_pages) &&
		    !fast_find_block && !isolation_suitable(cc, page))
			continue;

		/*
		 * For async compaction, also only scan in MOVABLE blocks
		 * without huge pages. Async compaction is optimistic to see
		 * if the minimum amount of work satisfies the allocation.
		 * The cached PFN is updated as it's possible that all
		 * remaining blocks between source and target are unsuitable
		 * and the compaction scanners fail to meet.
		 */
		if (!suitable_migration_source(cc, page)) {
			update_cached_migrate(cc, block_end_pfn);
			continue;
		}

		/* Perform the isolation */
		if (isolate_migratepages_block(cc, low_pfn, block_end_pfn,
						isolate_mode))
			return ISOLATE_ABORT;

		/*
		 * Either we isolated something and proceed with migration. Or
		 * we failed and compact_zone should decide if we should
		 * continue or not.
		 */
		break;
	}

	return cc->nr_migratepages ? ISOLATE_SUCCESS : ISOLATE_NONE;
}

/*
 * order == -1 is expected when compacting via
 * /proc/sys/vm/compact_memory
 */
static inline bool is_via_compact_memory(int order)
{
	return order == -1;
}

static bool kswapd_is_running(pg_data_t *pgdat)
{
	return pgdat->kswapd && task_is_running(pgdat->kswapd);
}

/*
 * A zone's fragmentation score is the external fragmentation wrt to the
 * COMPACTION_HPAGE_ORDER. It returns a value in the range [0, 100].
 */
static unsigned int fragmentation_score_zone(struct zone *zone)
{
	return extfrag_for_order(zone, COMPACTION_HPAGE_ORDER);
}

/*
 * A weighted zone's fragmentation score is the external fragmentation
 * wrt to the COMPACTION_HPAGE_ORDER scaled by the zone's size. It
 * returns a value in the range [0, 100].
 *
 * The scaling factor ensures that proactive compaction focuses on larger
 * zones like ZONE_NORMAL, rather than smaller, specialized zones like
 * ZONE_DMA32. For smaller zones, the score value remains close to zero,
 * and thus never exceeds the high threshold for proactive compaction.
 */
static unsigned int fragmentation_score_zone_weighted(struct zone *zone)
{
	unsigned long score;

	score = zone->present_pages * fragmentation_score_zone(zone);
	return div64_ul(score, zone->zone_pgdat->node_present_pages + 1);
}

/*
 * The per-node proactive (background) compaction process is started by its
 * corresponding kcompactd thread when the node's fragmentation score
 * exceeds the high threshold. The compaction process remains active till
 * the node's score falls below the low threshold, or one of the back-off
 * conditions is met.
 */
static unsigned int fragmentation_score_node(pg_data_t *pgdat)
{
	unsigned int score = 0;
	int zoneid;

	for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
		struct zone *zone;

		zone = &pgdat->node_zones[zoneid];
		score += fragmentation_score_zone_weighted(zone);
	}

	return score;
}

static unsigned int fragmentation_score_wmark(pg_data_t *pgdat, bool low)
{
	unsigned int wmark_low;

	/*
	 * Cap the low watermark to avoid excessive compaction
	 * activity in case a user sets the proactiveness tunable
	 * close to 100 (maximum).
	 */
	wmark_low = max(100U - sysctl_compaction_proactiveness, 5U);
	return low ? wmark_low : min(wmark_low + 10, 100U);
}

static bool should_proactive_compact_node(pg_data_t *pgdat)
{
	int wmark_high;

	if (!sysctl_compaction_proactiveness || kswapd_is_running(pgdat))
		return false;

	wmark_high = fragmentation_score_wmark(pgdat, false);
	return fragmentation_score_node(pgdat) > wmark_high;
}

static enum compact_result __compact_finished(struct compact_control *cc)
{
	unsigned int order;
	const int migratetype = cc->migratetype;
	int ret;

	/* Compaction run completes if the migrate and free scanner meet */
	if (compact_scanners_met(cc)) {
		/* Let the next compaction start anew. */
		reset_cached_positions(cc->zone);

		/*
		 * Mark that the PG_migrate_skip information should be cleared
		 * by kswapd when it goes to sleep. kcompactd does not set the
		 * flag itself as the decision to be clear should be directly
		 * based on an allocation request.
		 */
		if (cc->direct_compaction)
			cc->zone->compact_blockskip_flush = true;

		if (cc->whole_zone)
			return COMPACT_COMPLETE;
		else
			return COMPACT_PARTIAL_SKIPPED;
	}

	if (cc->proactive_compaction) {
		int score, wmark_low;
		pg_data_t *pgdat;

		pgdat = cc->zone->zone_pgdat;
		if (kswapd_is_running(pgdat))
			return COMPACT_PARTIAL_SKIPPED;

		score = fragmentation_score_zone(cc->zone);
		wmark_low = fragmentation_score_wmark(pgdat, true);

		if (score > wmark_low)
			ret = COMPACT_CONTINUE;
		else
			ret = COMPACT_SUCCESS;

		goto out;
	}

	if (is_via_compact_memory(cc->order))
		return COMPACT_CONTINUE;

	/*
	 * Always finish scanning a pageblock to reduce the possibility of
	 * fallbacks in the future. This is particularly important when
	 * migration source is unmovable/reclaimable but it's not worth
	 * special casing.
	 */
	if (!IS_ALIGNED(cc->migrate_pfn, pageblock_nr_pages))
		return COMPACT_CONTINUE;

	/* Direct compactor: Is a suitable page free? */
	ret = COMPACT_NO_SUITABLE_PAGE;
	for (order = cc->order; order < MAX_ORDER; order++) {
		struct free_area *area = &cc->zone->free_area[order];
		bool can_steal;

		/* Job done if page is free of the right migratetype */
		if (!free_area_empty(area, migratetype))
			return COMPACT_SUCCESS;

#ifdef CONFIG_CMA
		/* MIGRATE_MOVABLE can fallback on MIGRATE_CMA */
		if (migratetype == MIGRATE_MOVABLE &&
			!free_area_empty(area, MIGRATE_CMA))
			return COMPACT_SUCCESS;
#endif
		/*
		 * Job done if allocation would steal freepages from
		 * other migratetype buddy lists.
		 */
		if (find_suitable_fallback(area, order, migratetype,
						true, &can_steal) != -1) {

			/* movable pages are OK in any pageblock */
			if (migratetype == MIGRATE_MOVABLE)
				return COMPACT_SUCCESS;

			/*
			 * We are stealing for a non-movable allocation. Make
			 * sure we finish compacting the current pageblock
			 * first so it is as free as possible and we won't
			 * have to steal another one soon. This only applies
			 * to sync compaction, as async compaction operates
			 * on pageblocks of the same migratetype.
			 */
			if (cc->mode == MIGRATE_ASYNC ||
					IS_ALIGNED(cc->migrate_pfn,
							pageblock_nr_pages)) {
				return COMPACT_SUCCESS;
			}

			ret = COMPACT_CONTINUE;
			break;
		}
	}

out:
	if (cc->contended || fatal_signal_pending(current))
		ret = COMPACT_CONTENDED;

	return ret;
}

static enum compact_result compact_finished(struct compact_control *cc)
{
	int ret;

	ret = __compact_finished(cc);
	trace_mm_compaction_finished(cc->zone, cc->order, ret);
	if (ret == COMPACT_NO_SUITABLE_PAGE)
		ret = COMPACT_CONTINUE;

	return ret;
}

static enum compact_result __compaction_suitable(struct zone *zone, int order,
					unsigned int alloc_flags,
					int highest_zoneidx,
					unsigned long wmark_target)
{
	unsigned long watermark;

	if (is_via_compact_memory(order))
		return COMPACT_CONTINUE;

	watermark = wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK);
	/*
	 * If watermarks for high-order allocation are already met, there
	 * should be no need for compaction at all.
	 */
	if (zone_watermark_ok(zone, order, watermark, highest_zoneidx,
								alloc_flags))
		return COMPACT_SUCCESS;

	/*
	 * Watermarks for order-0 must be met for compaction to be able to
	 * isolate free pages for migration targets. This means that the
	 * watermark and alloc_flags have to match, or be more pessimistic than
	 * the check in __isolate_free_page(). We don't use the direct
	 * compactor's alloc_flags, as they are not relevant for freepage
	 * isolation. We however do use the direct compactor's highest_zoneidx
	 * to skip over zones where lowmem reserves would prevent allocation
	 * even if compaction succeeds.
	 * For costly orders, we require low watermark instead of min for
	 * compaction to proceed to increase its chances.
	 * ALLOC_CMA is used, as pages in CMA pageblocks are considered
	 * suitable migration targets
	 */
	watermark = (order > PAGE_ALLOC_COSTLY_ORDER) ?
				low_wmark_pages(zone) : min_wmark_pages(zone);
	watermark += compact_gap(order);
	if (!__zone_watermark_ok(zone, 0, watermark, highest_zoneidx,
						ALLOC_CMA, wmark_target))
		return COMPACT_SKIPPED;

	return COMPACT_CONTINUE;
}

/*
 * compaction_suitable: Is this suitable to run compaction on this zone now?
 * Returns
 *   COMPACT_SKIPPED  - If there are too few free pages for compaction
 *   COMPACT_SUCCESS  - If the allocation would succeed without compaction
 *   COMPACT_CONTINUE - If compaction should run now
 */
enum compact_result compaction_suitable(struct zone *zone, int order,
					unsigned int alloc_flags,
					int highest_zoneidx)
{
	enum compact_result ret;
	int fragindex;

	ret = __compaction_suitable(zone, order, alloc_flags, highest_zoneidx,
				    zone_page_state(zone, NR_FREE_PAGES));
	/*
	 * fragmentation index determines if allocation failures are due to
	 * low memory or external fragmentation
	 *
	 * index of -1000 would imply allocations might succeed depending on
	 * watermarks, but we already failed the high-order watermark check
	 * index towards 0 implies failure is due to lack of memory
	 * index towards 1000 implies failure is due to fragmentation
	 *
	 * Only compact if a failure would be due to fragmentation. Also
	 * ignore fragindex for non-costly orders where the alternative to
	 * a successful reclaim/compaction is OOM. Fragindex and the
	 * vm.extfrag_threshold sysctl is meant as a heuristic to prevent
	 * excessive compaction for costly orders, but it should not be at the
	 * expense of system stability.
	 */
	if (ret == COMPACT_CONTINUE && (order > PAGE_ALLOC_COSTLY_ORDER)) {
		fragindex = fragmentation_index(zone, order);
		if (fragindex >= 0 && fragindex <= sysctl_extfrag_threshold)
			ret = COMPACT_NOT_SUITABLE_ZONE;
	}

	trace_mm_compaction_suitable(zone, order, ret);
	if (ret == COMPACT_NOT_SUITABLE_ZONE)
		ret = COMPACT_SKIPPED;

	return ret;
}

bool compaction_zonelist_suitable(struct alloc_context *ac, int order,
		int alloc_flags)
{
	struct zone *zone;
	struct zoneref *z;

	/*
	 * Make sure at least one zone would pass __compaction_suitable if we continue
	 * retrying the reclaim.
	 */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist,
				ac->highest_zoneidx, ac->nodemask) {
		unsigned long available;
		enum compact_result compact_result;

		/*
		 * Do not consider all the reclaimable memory because we do not
		 * want to trash just for a single high order allocation which
		 * is even not guaranteed to appear even if __compaction_suitable
		 * is happy about the watermark check.
		 */
		available = zone_reclaimable_pages(zone) / order;
		available += zone_page_state_snapshot(zone, NR_FREE_PAGES);
		compact_result = __compaction_suitable(zone, order, alloc_flags,
				ac->highest_zoneidx, available);
		if (compact_result != COMPACT_SKIPPED)
			return true;
	}

	return false;
}

static enum compact_result
compact_zone(struct compact_control *cc, struct capture_control *capc)
{
	enum compact_result ret;
	unsigned long start_pfn = cc->zone->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(cc->zone);
	unsigned long last_migrated_pfn;
	const bool sync = cc->mode != MIGRATE_ASYNC;
	bool update_cached;

	/*
	 * These counters track activities during zone compaction.  Initialize
	 * them before compacting a new zone.
	 */
	cc->total_migrate_scanned = 0;
	cc->total_free_scanned = 0;
	cc->nr_migratepages = 0;
	cc->nr_freepages = 0;
	INIT_LIST_HEAD(&cc->freepages);
	INIT_LIST_HEAD(&cc->migratepages);

	cc->migratetype = gfp_migratetype(cc->gfp_mask);
	ret = compaction_suitable(cc->zone, cc->order, cc->alloc_flags,
							cc->highest_zoneidx);
	/* Compaction is likely to fail */
	if (ret == COMPACT_SUCCESS || ret == COMPACT_SKIPPED)
		return ret;

	/* huh, compaction_suitable is returning something unexpected */
	VM_BUG_ON(ret != COMPACT_CONTINUE);

	/*
	 * Clear pageblock skip if there were failures recently and compaction
	 * is about to be retried after being deferred.
	 */
	if (compaction_restarting(cc->zone, cc->order))
		__reset_isolation_suitable(cc->zone);

	/*
	 * Setup to move all movable pages to the end of the zone. Used cached
	 * information on where the scanners should start (unless we explicitly
	 * want to compact the whole zone), but check that it is initialised
	 * by ensuring the values are within zone boundaries.
	 */
	cc->fast_start_pfn = 0;
	if (cc->whole_zone) {
		cc->migrate_pfn = start_pfn;
		cc->free_pfn = pageblock_start_pfn(end_pfn - 1);
	} else {
		cc->migrate_pfn = cc->zone->compact_cached_migrate_pfn[sync];
		cc->free_pfn = cc->zone->compact_cached_free_pfn;
		if (cc->free_pfn < start_pfn || cc->free_pfn >= end_pfn) {
			cc->free_pfn = pageblock_start_pfn(end_pfn - 1);
			cc->zone->compact_cached_free_pfn = cc->free_pfn;
		}
		if (cc->migrate_pfn < start_pfn || cc->migrate_pfn >= end_pfn) {
			cc->migrate_pfn = start_pfn;
			cc->zone->compact_cached_migrate_pfn[0] = cc->migrate_pfn;
			cc->zone->compact_cached_migrate_pfn[1] = cc->migrate_pfn;
		}

		if (cc->migrate_pfn <= cc->zone->compact_init_migrate_pfn)
			cc->whole_zone = true;
	}

	last_migrated_pfn = 0;

	/*
	 * Migrate has separate cached PFNs for ASYNC and SYNC* migration on
	 * the basis that some migrations will fail in ASYNC mode. However,
	 * if the cached PFNs match and pageblocks are skipped due to having
	 * no isolation candidates, then the sync state does not matter.
	 * Until a pageblock with isolation candidates is found, keep the
	 * cached PFNs in sync to avoid revisiting the same blocks.
	 */
	update_cached = !sync &&
		cc->zone->compact_cached_migrate_pfn[0] == cc->zone->compact_cached_migrate_pfn[1];

	trace_mm_compaction_begin(start_pfn, cc->migrate_pfn,
				cc->free_pfn, end_pfn, sync);

	/* lru_add_drain_all could be expensive with involving other CPUs */
	lru_add_drain();

	while ((ret = compact_finished(cc)) == COMPACT_CONTINUE) {
		int err;
		unsigned long iteration_start_pfn = cc->migrate_pfn;

		/*
		 * Avoid multiple rescans which can happen if a page cannot be
		 * isolated (dirty/writeback in async mode) or if the migrated
		 * pages are being allocated before the pageblock is cleared.
		 * The first rescan will capture the entire pageblock for
		 * migration. If it fails, it'll be marked skip and scanning
		 * will proceed as normal.
		 */
		cc->rescan = false;
		if (pageblock_start_pfn(last_migrated_pfn) ==
		    pageblock_start_pfn(iteration_start_pfn)) {
			cc->rescan = true;
		}

		switch (isolate_migratepages(cc)) {
		case ISOLATE_ABORT:
			ret = COMPACT_CONTENDED;
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			goto out;
		case ISOLATE_NONE:
			if (update_cached) {
				cc->zone->compact_cached_migrate_pfn[1] =
					cc->zone->compact_cached_migrate_pfn[0];
			}

			/*
			 * We haven't isolated and migrated anything, but
			 * there might still be unflushed migrations from
			 * previous cc->order aligned block.
			 */
			goto check_drain;
		case ISOLATE_SUCCESS:
			update_cached = false;
			last_migrated_pfn = iteration_start_pfn;
		}

		err = migrate_pages(&cc->migratepages, compaction_alloc,
				compaction_free, (unsigned long)cc, cc->mode,
				MR_COMPACTION, NULL);

		trace_mm_compaction_migratepages(cc->nr_migratepages, err,
							&cc->migratepages);

		/* All pages were either migrated or will be released */
		cc->nr_migratepages = 0;
		if (err) {
			putback_movable_pages(&cc->migratepages);
			/*
			 * migrate_pages() may return -ENOMEM when scanners meet
			 * and we want compact_finished() to detect it
			 */
			if (err == -ENOMEM && !compact_scanners_met(cc)) {
				ret = COMPACT_CONTENDED;
				goto out;
			}
			/*
			 * We failed to migrate at least one page in the current
			 * order-aligned block, so skip the rest of it.
			 */
			if (cc->direct_compaction &&
						(cc->mode == MIGRATE_ASYNC)) {
				cc->migrate_pfn = block_end_pfn(
						cc->migrate_pfn - 1, cc->order);
				/* Draining pcplists is useless in this case */
				last_migrated_pfn = 0;
			}
		}

check_drain:
		/*
		 * Has the migration scanner moved away from the previous
		 * cc->order aligned block where we migrated from? If yes,
		 * flush the pages that were freed, so that they can merge and
		 * compact_finished() can detect immediately if allocation
		 * would succeed.
		 */
		if (cc->order > 0 && last_migrated_pfn) {
			unsigned long current_block_start =
				block_start_pfn(cc->migrate_pfn, cc->order);

			if (last_migrated_pfn < current_block_start) {
				lru_add_drain_cpu_zone(cc->zone);
				/* No more flushing until we migrate again */
				last_migrated_pfn = 0;
			}
		}

		/* Stop if a page has been captured */
		if (capc && capc->page) {
			ret = COMPACT_SUCCESS;
			break;
		}
	}

out:
	/*
	 * Release free pages and update where the free scanner should restart,
	 * so we don't leave any returned pages behind in the next attempt.
	 */
	if (cc->nr_freepages > 0) {
		unsigned long free_pfn = release_freepages(&cc->freepages);

		cc->nr_freepages = 0;
		VM_BUG_ON(free_pfn == 0);
		/* The cached pfn is always the first in a pageblock */
		free_pfn = pageblock_start_pfn(free_pfn);
		/*
		 * Only go back, not forward. The cached pfn might have been
		 * already reset to zone end in compact_finished()
		 */
		if (free_pfn > cc->zone->compact_cached_free_pfn)
			cc->zone->compact_cached_free_pfn = free_pfn;
	}

	count_compact_events(COMPACTMIGRATE_SCANNED, cc->total_migrate_scanned);
	count_compact_events(COMPACTFREE_SCANNED, cc->total_free_scanned);

	trace_mm_compaction_end(start_pfn, cc->migrate_pfn,
				cc->free_pfn, end_pfn, sync, ret);

	return ret;
}

static enum compact_result compact_zone_order(struct zone *zone, int order,
		gfp_t gfp_mask, enum compact_priority prio,
		unsigned int alloc_flags, int highest_zoneidx,
		struct page **capture)
{
	enum compact_result ret;
	struct compact_control cc = {
		.order = order,
		.search_order = order,
		.gfp_mask = gfp_mask,
		.zone = zone,
		.mode = (prio == COMPACT_PRIO_ASYNC) ?
					MIGRATE_ASYNC :	MIGRATE_SYNC_LIGHT,
		.alloc_flags = alloc_flags,
		.highest_zoneidx = highest_zoneidx,
		.direct_compaction = true,
		.whole_zone = (prio == MIN_COMPACT_PRIORITY),
		.ignore_skip_hint = (prio == MIN_COMPACT_PRIORITY),
		.ignore_block_suitable = (prio == MIN_COMPACT_PRIORITY)
	};
	struct capture_control capc = {
		.cc = &cc,
		.page = NULL,
	};

	/*
	 * Make sure the structs are really initialized before we expose the
	 * capture control, in case we are interrupted and the interrupt handler
	 * frees a page.
	 */
	barrier();
	WRITE_ONCE(current->capture_control, &capc);

	ret = compact_zone(&cc, &capc);

	VM_BUG_ON(!list_empty(&cc.freepages));
	VM_BUG_ON(!list_empty(&cc.migratepages));

	/*
	 * Make sure we hide capture control first before we read the captured
	 * page pointer, otherwise an interrupt could free and capture a page
	 * and we would leak it.
	 */
	WRITE_ONCE(current->capture_control, NULL);
	*capture = READ_ONCE(capc.page);
	/*
	 * Technically, it is also possible that compaction is skipped but
	 * the page is still captured out of luck(IRQ came and freed the page).
	 * Returning COMPACT_SUCCESS in such cases helps in properly accounting
	 * the COMPACT[STALL|FAIL] when compaction is skipped.
	 */
	if (*capture)
		ret = COMPACT_SUCCESS;

	return ret;
}

int sysctl_extfrag_threshold = 500;

/**
 * try_to_compact_pages - Direct compact to satisfy a high-order allocation
 * @gfp_mask: The GFP mask of the current allocation
 * @order: The order of the current allocation
 * @alloc_flags: The allocation flags of the current allocation
 * @ac: The context of current allocation
 * @prio: Determines how hard direct compaction should try to succeed
 * @capture: Pointer to free page created by compaction will be stored here
 *
 * This is the main entry point for direct page compaction.
 */
enum compact_result try_to_compact_pages(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, struct page **capture)
{
	int may_perform_io = gfp_mask & __GFP_IO;
	struct zoneref *z;
	struct zone *zone;
	enum compact_result rc = COMPACT_SKIPPED;

	/*
	 * Check if the GFP flags allow compaction - GFP_NOIO is really
	 * tricky context because the migration might require IO
	 */
	if (!may_perform_io)
		return COMPACT_SKIPPED;

	trace_mm_compaction_try_to_compact_pages(order, gfp_mask, prio);

	/* Compact each zone in the list */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist,
					ac->highest_zoneidx, ac->nodemask) {
		enum compact_result status;

		if (prio > MIN_COMPACT_PRIORITY
					&& compaction_deferred(zone, order)) {
			rc = max_t(enum compact_result, COMPACT_DEFERRED, rc);
			continue;
		}

		status = compact_zone_order(zone, order, gfp_mask, prio,
				alloc_flags, ac->highest_zoneidx, capture);
		rc = max(status, rc);

		/* The allocation should succeed, stop compacting */
		if (status == COMPACT_SUCCESS) {
			/*
			 * We think the allocation will succeed in this zone,
			 * but it is not certain, hence the false. The caller
			 * will repeat this with true if allocation indeed
			 * succeeds in this zone.
			 */
			compaction_defer_reset(zone, order, false);

			break;
		}

		if (prio != COMPACT_PRIO_ASYNC && (status == COMPACT_COMPLETE ||
					status == COMPACT_PARTIAL_SKIPPED))
			/*
			 * We think that allocation won't succeed in this zone
			 * so we defer compaction there. If it ends up
			 * succeeding after all, it will be reset.
			 */
			defer_compaction(zone, order);

		/*
		 * We might have stopped compacting due to need_resched() in
		 * async compaction, or due to a fatal signal detected. In that
		 * case do not try further zones
		 */
		if ((prio == COMPACT_PRIO_ASYNC && need_resched())
					|| fatal_signal_pending(current))
			break;
	}

	return rc;
}

/*
 * Compact all zones within a node till each zone's fragmentation score
 * reaches within proactive compaction thresholds (as determined by the
 * proactiveness tunable).
 *
 * It is possible that the function returns before reaching score targets
 * due to various back-off conditions, such as, contention on per-node or
 * per-zone locks.
 */
static void proactive_compact_node(pg_data_t *pgdat)
{
	int zoneid;
	struct zone *zone;
	struct compact_control cc = {
		.order = -1,
		.mode = MIGRATE_SYNC_LIGHT,
		.ignore_skip_hint = true,
		.whole_zone = true,
		.gfp_mask = GFP_KERNEL,
		.proactive_compaction = true,
	};

	for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
		zone = &pgdat->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		cc.zone = zone;

		compact_zone(&cc, NULL);

		VM_BUG_ON(!list_empty(&cc.freepages));
		VM_BUG_ON(!list_empty(&cc.migratepages));
	}
}

/* Compact all zones within a node */
static void compact_node(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int zoneid;
	struct zone *zone;
	struct compact_control cc = {
		.order = -1,
		.mode = MIGRATE_SYNC,
		.ignore_skip_hint = true,
		.whole_zone = true,
		.gfp_mask = GFP_KERNEL,
	};


	for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {

		zone = &pgdat->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		cc.zone = zone;

		compact_zone(&cc, NULL);

		VM_BUG_ON(!list_empty(&cc.freepages));
		VM_BUG_ON(!list_empty(&cc.migratepages));
	}
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

/*
 * Tunable for proactive compaction. It determines how
 * aggressively the kernel should compact memory in the
 * background. It takes values in the range [0, 100].
 */
unsigned int __read_mostly sysctl_compaction_proactiveness = 20;

int compaction_proactiveness_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int rc, nid;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	if (write && sysctl_compaction_proactiveness) {
		for_each_online_node(nid) {
			pg_data_t *pgdat = NODE_DATA(nid);

			if (pgdat->proactive_compact_trigger)
				continue;

			pgdat->proactive_compact_trigger = true;
			wake_up_interruptible(&pgdat->kcompactd_wait);
		}
	}

	return 0;
}

/*
 * This is the entry point for compacting all nodes via
 * /proc/sys/vm/compact_memory
 */
int sysctl_compaction_handler(struct ctl_table *table, int write,
			void *buffer, size_t *length, loff_t *ppos)
{
	if (write)
		compact_nodes();

	return 0;
}

#if defined(CONFIG_SYSFS) && defined(CONFIG_NUMA)
static ssize_t compact_store(struct device *dev,
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
static DEVICE_ATTR_WO(compact);

int compaction_register_node(struct node *node)
{
	return device_create_file(&node->dev, &dev_attr_compact);
}

void compaction_unregister_node(struct node *node)
{
	return device_remove_file(&node->dev, &dev_attr_compact);
}
#endif /* CONFIG_SYSFS && CONFIG_NUMA */

static inline bool kcompactd_work_requested(pg_data_t *pgdat)
{
	return pgdat->kcompactd_max_order > 0 || kthread_should_stop() ||
		pgdat->proactive_compact_trigger;
}

static bool kcompactd_node_suitable(pg_data_t *pgdat)
{
	int zoneid;
	struct zone *zone;
	enum zone_type highest_zoneidx = pgdat->kcompactd_highest_zoneidx;

	for (zoneid = 0; zoneid <= highest_zoneidx; zoneid++) {
		zone = &pgdat->node_zones[zoneid];

		if (!populated_zone(zone))
			continue;

		if (compaction_suitable(zone, pgdat->kcompactd_max_order, 0,
					highest_zoneidx) == COMPACT_CONTINUE)
			return true;
	}

	return false;
}

static void kcompactd_do_work(pg_data_t *pgdat)
{
	/*
	 * With no special task, compact all zones so that a page of requested
	 * order is allocatable.
	 */
	int zoneid;
	struct zone *zone;
	struct compact_control cc = {
		.order = pgdat->kcompactd_max_order,
		.search_order = pgdat->kcompactd_max_order,
		.highest_zoneidx = pgdat->kcompactd_highest_zoneidx,
		.mode = MIGRATE_SYNC_LIGHT,
		.ignore_skip_hint = false,
		.gfp_mask = GFP_KERNEL,
	};
	trace_mm_compaction_kcompactd_wake(pgdat->node_id, cc.order,
							cc.highest_zoneidx);
	count_compact_event(KCOMPACTD_WAKE);

	for (zoneid = 0; zoneid <= cc.highest_zoneidx; zoneid++) {
		int status;

		zone = &pgdat->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		if (compaction_deferred(zone, cc.order))
			continue;

		if (compaction_suitable(zone, cc.order, 0, zoneid) !=
							COMPACT_CONTINUE)
			continue;

		if (kthread_should_stop())
			return;

		cc.zone = zone;
		status = compact_zone(&cc, NULL);

		if (status == COMPACT_SUCCESS) {
			compaction_defer_reset(zone, cc.order, false);
		} else if (status == COMPACT_PARTIAL_SKIPPED || status == COMPACT_COMPLETE) {
			/*
			 * Buddy pages may become stranded on pcps that could
			 * otherwise coalesce on the zone's free area for
			 * order >= cc.order.  This is ratelimited by the
			 * upcoming deferral.
			 */
			drain_all_pages(zone);

			/*
			 * We use sync migration mode here, so we defer like
			 * sync direct compaction does.
			 */
			defer_compaction(zone, cc.order);
		}

		count_compact_events(KCOMPACTD_MIGRATE_SCANNED,
				     cc.total_migrate_scanned);
		count_compact_events(KCOMPACTD_FREE_SCANNED,
				     cc.total_free_scanned);

		VM_BUG_ON(!list_empty(&cc.freepages));
		VM_BUG_ON(!list_empty(&cc.migratepages));
	}

	/*
	 * Regardless of success, we are done until woken up next. But remember
	 * the requested order/highest_zoneidx in case it was higher/tighter
	 * than our current ones
	 */
	if (pgdat->kcompactd_max_order <= cc.order)
		pgdat->kcompactd_max_order = 0;
	if (pgdat->kcompactd_highest_zoneidx >= cc.highest_zoneidx)
		pgdat->kcompactd_highest_zoneidx = pgdat->nr_zones - 1;
}

void wakeup_kcompactd(pg_data_t *pgdat, int order, int highest_zoneidx)
{
	if (!order)
		return;

	if (pgdat->kcompactd_max_order < order)
		pgdat->kcompactd_max_order = order;

	if (pgdat->kcompactd_highest_zoneidx > highest_zoneidx)
		pgdat->kcompactd_highest_zoneidx = highest_zoneidx;

	/*
	 * Pairs with implicit barrier in wait_event_freezable()
	 * such that wakeups are not missed.
	 */
	if (!wq_has_sleeper(&pgdat->kcompactd_wait))
		return;

	if (!kcompactd_node_suitable(pgdat))
		return;

	trace_mm_compaction_wakeup_kcompactd(pgdat->node_id, order,
							highest_zoneidx);
	wake_up_interruptible(&pgdat->kcompactd_wait);
}

/*
 * The background compaction daemon, started as a kernel thread
 * from the init process.
 */
static int kcompactd(void *p)
{
	pg_data_t *pgdat = (pg_data_t *)p;
	struct task_struct *tsk = current;
	long default_timeout = msecs_to_jiffies(HPAGE_FRAG_CHECK_INTERVAL_MSEC);
	long timeout = default_timeout;

	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);

	set_freezable();

	pgdat->kcompactd_max_order = 0;
	pgdat->kcompactd_highest_zoneidx = pgdat->nr_zones - 1;

	while (!kthread_should_stop()) {
		unsigned long pflags;

		/*
		 * Avoid the unnecessary wakeup for proactive compaction
		 * when it is disabled.
		 */
		if (!sysctl_compaction_proactiveness)
			timeout = MAX_SCHEDULE_TIMEOUT;
		trace_mm_compaction_kcompactd_sleep(pgdat->node_id);
		if (wait_event_freezable_timeout(pgdat->kcompactd_wait,
			kcompactd_work_requested(pgdat), timeout) &&
			!pgdat->proactive_compact_trigger) {

			psi_memstall_enter(&pflags);
			kcompactd_do_work(pgdat);
			psi_memstall_leave(&pflags);
			/*
			 * Reset the timeout value. The defer timeout from
			 * proactive compaction is lost here but that is fine
			 * as the condition of the zone changing substantionally
			 * then carrying on with the previous defer interval is
			 * not useful.
			 */
			timeout = default_timeout;
			continue;
		}

		/*
		 * Start the proactive work with default timeout. Based
		 * on the fragmentation score, this timeout is updated.
		 */
		timeout = default_timeout;
		if (should_proactive_compact_node(pgdat)) {
			unsigned int prev_score, score;

			prev_score = fragmentation_score_node(pgdat);
			proactive_compact_node(pgdat);
			score = fragmentation_score_node(pgdat);
			/*
			 * Defer proactive compaction if the fragmentation
			 * score did not go down i.e. no progress made.
			 */
			if (unlikely(score >= prev_score))
				timeout =
				   default_timeout << COMPACT_MAX_DEFER_SHIFT;
		}
		if (unlikely(pgdat->proactive_compact_trigger))
			pgdat->proactive_compact_trigger = false;
	}

	return 0;
}

/*
 * This kcompactd start function will be called by init and node-hot-add.
 * On node-hot-add, kcompactd will moved to proper cpus if cpus are hot-added.
 */
int kcompactd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int ret = 0;

	if (pgdat->kcompactd)
		return 0;

	pgdat->kcompactd = kthread_run(kcompactd, pgdat, "kcompactd%d", nid);
	if (IS_ERR(pgdat->kcompactd)) {
		pr_err("Failed to start kcompactd on node %d\n", nid);
		ret = PTR_ERR(pgdat->kcompactd);
		pgdat->kcompactd = NULL;
	}
	return ret;
}

/*
 * Called by memory hotplug when all memory in a node is offlined. Caller must
 * hold mem_hotplug_begin/end().
 */
void kcompactd_stop(int nid)
{
	struct task_struct *kcompactd = NODE_DATA(nid)->kcompactd;

	if (kcompactd) {
		kthread_stop(kcompactd);
		NODE_DATA(nid)->kcompactd = NULL;
	}
}

/*
 * It's optimal to keep kcompactd on the same CPUs as their memory, but
 * not required for correctness. So if the last cpu in a node goes
 * away, we get changed to run anywhere: as the first one comes back,
 * restore their cpu bindings.
 */
static int kcompactd_cpu_online(unsigned int cpu)
{
	int nid;

	for_each_node_state(nid, N_MEMORY) {
		pg_data_t *pgdat = NODE_DATA(nid);
		const struct cpumask *mask;

		mask = cpumask_of_node(pgdat->node_id);

		if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids)
			/* One of our CPUs online: restore mask */
			set_cpus_allowed_ptr(pgdat->kcompactd, mask);
	}
	return 0;
}

static int __init kcompactd_init(void)
{
	int nid;
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"mm/compaction:online",
					kcompactd_cpu_online, NULL);
	if (ret < 0) {
		pr_err("kcompactd: failed to register hotplug callbacks.\n");
		return ret;
	}

	for_each_node_state(nid, N_MEMORY)
		kcompactd_run(nid);
	return 0;
}
subsys_initcall(kcompactd_init)

#endif /* CONFIG_COMPACTION */
