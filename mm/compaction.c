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
#include <linux/backing-dev.h>
#include <linux/sysctl.h>
#include <linux/sysfs.h>
#include <linux/balloon_compaction.h>
#include <linux/page-isolation.h>
#include <linux/kasan.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
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

static void map_pages(struct list_head *list)
{
	struct page *page;

	list_for_each_entry(page, list, lru) {
		arch_alloc_page(page, 0);
		kernel_map_pages(page, 1, 1);
		kasan_alloc_pages(page, 0);
	}
}

static inline bool migrate_async_suitable(int migratetype)
{
	return is_migrate_cma(migratetype) || migratetype == MIGRATE_MOVABLE;
}

#ifdef CONFIG_COMPACTION

/* Do not skip compaction more than 64 times */
#define COMPACT_MAX_DEFER_SHIFT 6

/*
 * Compaction is deferred when compaction fails to result in a page
 * allocation success. 1 << compact_defer_limit compactions are skipped up
 * to a limit of 1 << COMPACT_MAX_DEFER_SHIFT
 */
void defer_compaction(struct zone *zone, int order)
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
bool compaction_deferred(struct zone *zone, int order)
{
	unsigned long defer_limit = 1UL << zone->compact_defer_shift;

	if (order < zone->compact_order_failed)
		return false;

	/* Avoid possible overflow */
	if (++zone->compact_considered > defer_limit)
		zone->compact_considered = defer_limit;

	if (zone->compact_considered >= defer_limit)
		return false;

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
bool compaction_restarting(struct zone *zone, int order)
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
 * This function is called to clear all cached information on pageblocks that
 * should be skipped for page isolation when the migrate and free page scanner
 * meet.
 */
static void __reset_isolation_suitable(struct zone *zone)
{
	unsigned long start_pfn = zone->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(zone);
	unsigned long pfn;

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

	reset_cached_positions(zone);
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
	unsigned long pfn;

	if (cc->ignore_skip_hint)
		return;

	if (!page)
		return;

	if (nr_isolated)
		return;

	set_pageblock_skip(page);

	pfn = page_to_pfn(page);

	/* Update where async and sync compaction should restart */
	if (migrate_scanner) {
		if (pfn > zone->compact_cached_migrate_pfn[0])
			zone->compact_cached_migrate_pfn[0] = pfn;
		if (cc->mode != MIGRATE_ASYNC &&
		    pfn > zone->compact_cached_migrate_pfn[1])
			zone->compact_cached_migrate_pfn[1] = pfn;
	} else {
		if (pfn < zone->compact_cached_free_pfn)
			zone->compact_cached_free_pfn = pfn;
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

/*
 * Compaction requires the taking of some coarse locks that are potentially
 * very heavily contended. For async compaction, back out if the lock cannot
 * be taken immediately. For sync compaction, spin on the lock if needed.
 *
 * Returns true if the lock is held
 * Returns false if the lock is not held and compaction should abort
 */
static bool compact_trylock_irqsave(spinlock_t *lock, unsigned long *flags,
						struct compact_control *cc)
{
	if (cc->mode == MIGRATE_ASYNC) {
		if (!spin_trylock_irqsave(lock, *flags)) {
			cc->contended = COMPACT_CONTENDED_LOCK;
			return false;
		}
	} else {
		spin_lock_irqsave(lock, *flags);
	}

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
		cc->contended = COMPACT_CONTENDED_SCHED;
		return true;
	}

	if (need_resched()) {
		if (cc->mode == MIGRATE_ASYNC) {
			cc->contended = COMPACT_CONTENDED_SCHED;
			return true;
		}
		cond_resched();
	}

	return false;
}

/*
 * Aside from avoiding lock contention, compaction also periodically checks
 * need_resched() and either schedules in sync compaction or aborts async
 * compaction. This is similar to what compact_unlock_should_abort() does, but
 * is used where no lock is concerned.
 *
 * Returns false when no scheduling was needed, or sync compaction scheduled.
 * Returns true when async compaction should abort.
 */
static inline bool compact_should_abort(struct compact_control *cc)
{
	/* async compaction aborts if contended */
	if (need_resched()) {
		if (cc->mode == MIGRATE_ASYNC) {
			cc->contended = COMPACT_CONTENDED_SCHED;
			return true;
		}

		cond_resched();
	}

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
				bool strict)
{
	int nr_scanned = 0, total_isolated = 0;
	struct page *cursor, *valid_page = NULL;
	unsigned long flags = 0;
	bool locked = false;
	unsigned long blockpfn = *start_pfn;

	cursor = pfn_to_page(blockpfn);

	/* Isolate free pages. */
	for (; blockpfn < end_pfn; blockpfn++, cursor++) {
		int isolated, i;
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
		if (!pfn_valid_within(blockpfn))
			goto isolate_fail;

		if (!valid_page)
			valid_page = page;

		/*
		 * For compound pages such as THP and hugetlbfs, we can save
		 * potentially a lot of iterations if we skip them at once.
		 * The check is racy, but we can consider only valid values
		 * and the only danger is skipping too much.
		 */
		if (PageCompound(page)) {
			unsigned int comp_order = compound_order(page);

			if (likely(comp_order < MAX_ORDER)) {
				blockpfn += (1UL << comp_order) - 1;
				cursor += (1UL << comp_order) - 1;
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
			/*
			 * The zone lock must be held to isolate freepages.
			 * Unfortunately this is a very coarse lock and can be
			 * heavily contended if there are parallel allocations
			 * or parallel compactions. For async compaction do not
			 * spin on the lock and we acquire the lock as late as
			 * possible.
			 */
			locked = compact_trylock_irqsave(&cc->zone->lock,
								&flags, cc);
			if (!locked)
				break;

			/* Recheck this is a buddy page under lock */
			if (!PageBuddy(page))
				goto isolate_fail;
		}

		/* Found a free page, break it into order-0 pages */
		isolated = split_free_page(page);
		total_isolated += isolated;
		for (i = 0; i < isolated; i++) {
			list_add(&page->lru, freelist);
			page++;
		}

		/* If a page was split, advance to the end of it */
		if (isolated) {
			cc->nr_freepages += isolated;
			if (!strict &&
				cc->nr_migratepages <= cc->nr_freepages) {
				blockpfn += isolated;
				break;
			}

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
						block_end_pfn, &freelist, true);

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
static void acct_isolated(struct zone *zone, struct compact_control *cc)
{
	struct page *page;
	unsigned int count[2] = { 0, };

	if (list_empty(&cc->migratepages))
		return;

	list_for_each_entry(page, &cc->migratepages, lru)
		count[!!page_is_file_cache(page)]++;

	mod_zone_page_state(zone, NR_ISOLATED_ANON, count[0]);
	mod_zone_page_state(zone, NR_ISOLATED_FILE, count[1]);
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
 * isolate_migratepages_block() - isolate all migrate-able pages within
 *				  a single pageblock
 * @cc:		Compaction control structure.
 * @low_pfn:	The first PFN to isolate
 * @end_pfn:	The one-past-the-last PFN to isolate, within same pageblock
 * @isolate_mode: Isolation mode to be used.
 *
 * Isolate all pages that can be migrated from the range specified by
 * [low_pfn, end_pfn). The range is expected to be within same pageblock.
 * Returns zero if there is a fatal signal pending, otherwise PFN of the
 * first page that was not scanned (which may be both less, equal to or more
 * than end_pfn).
 *
 * The pages are isolated on cc->migratepages list (not required to be empty),
 * and cc->nr_migratepages is updated accordingly. The cc->migrate_pfn field
 * is neither read nor updated.
 */
static unsigned long
isolate_migratepages_block(struct compact_control *cc, unsigned long low_pfn,
			unsigned long end_pfn, isolate_mode_t isolate_mode)
{
	struct zone *zone = cc->zone;
	unsigned long nr_scanned = 0, nr_isolated = 0;
	struct lruvec *lruvec;
	unsigned long flags = 0;
	bool locked = false;
	struct page *page = NULL, *valid_page = NULL;
	unsigned long start_pfn = low_pfn;
	bool skip_on_failure = false;
	unsigned long next_skip_pfn = 0;

	/*
	 * Ensure that there are not too many pages isolated from the LRU
	 * list by either parallel reclaimers or compaction. If there are,
	 * delay for some time until fewer pages are isolated
	 */
	while (unlikely(too_many_isolated(zone))) {
		/* async migration should just abort */
		if (cc->mode == MIGRATE_ASYNC)
			return 0;

		congestion_wait(BLK_RW_ASYNC, HZ/10);

		if (fatal_signal_pending(current))
			return 0;
	}

	if (compact_should_abort(cc))
		return 0;

	if (cc->direct_compaction && (cc->mode == MIGRATE_ASYNC)) {
		skip_on_failure = true;
		next_skip_pfn = block_end_pfn(low_pfn, cc->order);
	}

	/* Time to isolate some pages for migration */
	for (; low_pfn < end_pfn; low_pfn++) {
		bool is_lru;

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
		 * contention, to give chance to IRQs. Abort async compaction
		 * if contended.
		 */
		if (!(low_pfn % SWAP_CLUSTER_MAX)
		    && compact_unlock_should_abort(&zone->lru_lock, flags,
								&locked, cc))
			break;

		if (!pfn_valid_within(low_pfn))
			goto isolate_fail;
		nr_scanned++;

		page = pfn_to_page(low_pfn);

		if (!valid_page)
			valid_page = page;

		/*
		 * Skip if free. We read page order here without zone lock
		 * which is generally unsafe, but the race window is small and
		 * the worst thing that can happen is that we skip some
		 * potential isolation targets.
		 */
		if (PageBuddy(page)) {
			unsigned long freepage_order = page_order_unsafe(page);

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
		 * Check may be lockless but that's ok as we recheck later.
		 * It's possible to migrate LRU pages and balloon pages
		 * Skip any other type of page
		 */
		is_lru = PageLRU(page);
		if (!is_lru) {
			if (unlikely(balloon_page_movable(page))) {
				if (balloon_page_isolate(page)) {
					/* Successfully isolated */
					goto isolate_success;
				}
			}
		}

		/*
		 * Regardless of being on LRU, compound pages such as THP and
		 * hugetlbfs are not to be compacted. We can potentially save
		 * a lot of iterations if we skip them at once. The check is
		 * racy, but we can consider only valid values and the only
		 * danger is skipping too much.
		 */
		if (PageCompound(page)) {
			unsigned int comp_order = compound_order(page);

			if (likely(comp_order < MAX_ORDER))
				low_pfn += (1UL << comp_order) - 1;

			goto isolate_fail;
		}

		if (!is_lru)
			goto isolate_fail;

		/*
		 * Migration will fail if an anonymous page is pinned in memory,
		 * so avoid taking lru_lock and isolating it unnecessarily in an
		 * admittedly racy check.
		 */
		if (!page_mapping(page) &&
		    page_count(page) > page_mapcount(page))
			goto isolate_fail;

		/* If we already hold the lock, we can skip some rechecking */
		if (!locked) {
			locked = compact_trylock_irqsave(&zone->lru_lock,
								&flags, cc);
			if (!locked)
				break;

			/* Recheck PageLRU and PageCompound under lock */
			if (!PageLRU(page))
				goto isolate_fail;

			/*
			 * Page become compound since the non-locked check,
			 * and it's on LRU. It can only be a THP so the order
			 * is safe to read and it's 0 for tail pages.
			 */
			if (unlikely(PageCompound(page))) {
				low_pfn += (1UL << compound_order(page)) - 1;
				goto isolate_fail;
			}
		}

		lruvec = mem_cgroup_page_lruvec(page, zone);

		/* Try isolate the page */
		if (__isolate_lru_page(page, isolate_mode) != 0)
			goto isolate_fail;

		VM_BUG_ON_PAGE(PageCompound(page), page);

		/* Successfully isolated */
		del_page_from_lru_list(page, lruvec, page_lru(page));

isolate_success:
		list_add(&page->lru, &cc->migratepages);
		cc->nr_migratepages++;
		nr_isolated++;

		/*
		 * Record where we could have freed pages by migration and not
		 * yet flushed them to buddy allocator.
		 * - this is the lowest page that was isolated and likely be
		 * then freed by migration.
		 */
		if (!cc->last_migrated_pfn)
			cc->last_migrated_pfn = low_pfn;

		/* Avoid isolating too much */
		if (cc->nr_migratepages == COMPACT_CLUSTER_MAX) {
			++low_pfn;
			break;
		}

		continue;
isolate_fail:
		if (!skip_on_failure)
			continue;

		/*
		 * We have isolated some pages, but then failed. Release them
		 * instead of migrating, as we cannot form the cc->order buddy
		 * page anyway.
		 */
		if (nr_isolated) {
			if (locked) {
				spin_unlock_irqrestore(&zone->lru_lock,	flags);
				locked = false;
			}
			acct_isolated(zone, cc);
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			cc->last_migrated_pfn = 0;
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
	}

	/*
	 * The PageBuddy() check could have potentially brought us outside
	 * the range to be scanned.
	 */
	if (unlikely(low_pfn > end_pfn))
		low_pfn = end_pfn;

	if (locked)
		spin_unlock_irqrestore(&zone->lru_lock, flags);

	/*
	 * Update the pageblock-skip information and cached scanner pfn,
	 * if the whole pageblock was scanned without isolating any page.
	 */
	if (low_pfn == end_pfn)
		update_pageblock_skip(cc, valid_page, nr_isolated, true);

	trace_mm_compaction_isolate_migratepages(start_pfn, low_pfn,
						nr_scanned, nr_isolated);

	count_compact_events(COMPACTMIGRATE_SCANNED, nr_scanned);
	if (nr_isolated)
		count_compact_events(COMPACTISOLATED, nr_isolated);

	return low_pfn;
}

/**
 * isolate_migratepages_range() - isolate migrate-able pages in a PFN range
 * @cc:        Compaction control structure.
 * @start_pfn: The first PFN to start isolating.
 * @end_pfn:   The one-past-last PFN.
 *
 * Returns zero if isolation fails fatally due to e.g. pending signal.
 * Otherwise, function returns one-past-the-last PFN of isolated page
 * (which may be greater than end_pfn if end fell in a middle of a THP page).
 */
unsigned long
isolate_migratepages_range(struct compact_control *cc, unsigned long start_pfn,
							unsigned long end_pfn)
{
	unsigned long pfn, block_start_pfn, block_end_pfn;

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

		pfn = isolate_migratepages_block(cc, pfn, block_end_pfn,
							ISOLATE_UNEVICTABLE);

		if (!pfn)
			break;

		if (cc->nr_migratepages == COMPACT_CLUSTER_MAX)
			break;
	}
	acct_isolated(cc->zone, cc);

	return pfn;
}

#endif /* CONFIG_COMPACTION || CONFIG_CMA */
#ifdef CONFIG_COMPACTION

/* Returns true if the page is within a block suitable for migration to */
static bool suitable_migration_target(struct page *page)
{
	/* If the page is a large free page, then disallow migration */
	if (PageBuddy(page)) {
		/*
		 * We are checking page_order without zone->lock taken. But
		 * the only small danger is that we skip a potentially suitable
		 * pageblock, so it's not worth to check order for valid range.
		 */
		if (page_order_unsafe(page) >= pageblock_order)
			return false;
	}

	/* If the block is MIGRATE_MOVABLE or MIGRATE_CMA, allow migration */
	if (migrate_async_suitable(get_pageblock_migratetype(page)))
		return true;

	/* Otherwise skip the block */
	return false;
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

	/*
	 * Initialise the free scanner. The starting point is where we last
	 * successfully isolated from, zone-cached value, or the end of the
	 * zone when isolating for the first time. For looping we also need
	 * this pfn aligned down to the pageblock boundary, because we do
	 * block_start_pfn -= pageblock_nr_pages in the for loop.
	 * For ending point, take care when isolating in last pageblock of a
	 * a zone which ends in the middle of a pageblock.
	 * The low boundary is the end of the pageblock the migration scanner
	 * is using.
	 */
	isolate_start_pfn = cc->free_pfn;
	block_start_pfn = pageblock_start_pfn(cc->free_pfn);
	block_end_pfn = min(block_start_pfn + pageblock_nr_pages,
						zone_end_pfn(zone));
	low_pfn = pageblock_end_pfn(cc->migrate_pfn);

	/*
	 * Isolate free pages until enough are available to migrate the
	 * pages on cc->migratepages. We stop searching if the migrate
	 * and free page scanners meet or enough free pages are isolated.
	 */
	for (; block_start_pfn >= low_pfn;
				block_end_pfn = block_start_pfn,
				block_start_pfn -= pageblock_nr_pages,
				isolate_start_pfn = block_start_pfn) {

		/*
		 * This can iterate a massively long zone without finding any
		 * suitable migration targets, so periodically check if we need
		 * to schedule, or even abort async compaction.
		 */
		if (!(block_start_pfn % (SWAP_CLUSTER_MAX * pageblock_nr_pages))
						&& compact_should_abort(cc))
			break;

		page = pageblock_pfn_to_page(block_start_pfn, block_end_pfn,
									zone);
		if (!page)
			continue;

		/* Check the block is suitable for migration */
		if (!suitable_migration_target(page))
			continue;

		/* If isolation recently failed, do not retry */
		if (!isolation_suitable(cc, page))
			continue;

		/* Found a block suitable for isolating free pages from. */
		isolate_freepages_block(cc, &isolate_start_pfn,
					block_end_pfn, freelist, false);

		/*
		 * If we isolated enough freepages, or aborted due to async
		 * compaction being contended, terminate the loop.
		 * Remember where the free scanner should restart next time,
		 * which is where isolate_freepages_block() left off.
		 * But if it scanned the whole pageblock, isolate_start_pfn
		 * now points at block_end_pfn, which is the start of the next
		 * pageblock.
		 * In that case we will however want to restart at the start
		 * of the previous pageblock.
		 */
		if ((cc->nr_freepages >= cc->nr_migratepages)
							|| cc->contended) {
			if (isolate_start_pfn >= block_end_pfn)
				isolate_start_pfn =
					block_start_pfn - pageblock_nr_pages;
			break;
		} else {
			/*
			 * isolate_freepages_block() should not terminate
			 * prematurely unless contended, or isolated enough
			 */
			VM_BUG_ON(isolate_start_pfn < block_end_pfn);
		}
	}

	/* split_free_page does not map the pages */
	map_pages(freelist);

	/*
	 * Record where the free scanner will restart next time. Either we
	 * broke from the loop and set isolate_start_pfn based on the last
	 * call to isolate_freepages_block(), or we met the migration scanner
	 * and the loop terminated due to isolate_start_pfn < low_pfn
	 */
	cc->free_pfn = isolate_start_pfn;
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

	/*
	 * Isolate free pages if necessary, and if we are not aborting due to
	 * contention.
	 */
	if (list_empty(&cc->freepages)) {
		if (!cc->contended)
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
int sysctl_compact_unevictable_allowed __read_mostly = 1;

/*
 * Isolate all pages that can be migrated from the first suitable block,
 * starting at the block pointed to by the migrate scanner pfn within
 * compact_control.
 */
static isolate_migrate_t isolate_migratepages(struct zone *zone,
					struct compact_control *cc)
{
	unsigned long block_start_pfn;
	unsigned long block_end_pfn;
	unsigned long low_pfn;
	struct page *page;
	const isolate_mode_t isolate_mode =
		(sysctl_compact_unevictable_allowed ? ISOLATE_UNEVICTABLE : 0) |
		(cc->mode == MIGRATE_ASYNC ? ISOLATE_ASYNC_MIGRATE : 0);

	/*
	 * Start at where we last stopped, or beginning of the zone as
	 * initialized by compact_zone()
	 */
	low_pfn = cc->migrate_pfn;
	block_start_pfn = pageblock_start_pfn(low_pfn);
	if (block_start_pfn < zone->zone_start_pfn)
		block_start_pfn = zone->zone_start_pfn;

	/* Only scan within a pageblock boundary */
	block_end_pfn = pageblock_end_pfn(low_pfn);

	/*
	 * Iterate over whole pageblocks until we find the first suitable.
	 * Do not cross the free scanner.
	 */
	for (; block_end_pfn <= cc->free_pfn;
			low_pfn = block_end_pfn,
			block_start_pfn = block_end_pfn,
			block_end_pfn += pageblock_nr_pages) {

		/*
		 * This can potentially iterate a massively long zone with
		 * many pageblocks unsuitable, so periodically check if we
		 * need to schedule, or even abort async compaction.
		 */
		if (!(low_pfn % (SWAP_CLUSTER_MAX * pageblock_nr_pages))
						&& compact_should_abort(cc))
			break;

		page = pageblock_pfn_to_page(block_start_pfn, block_end_pfn,
									zone);
		if (!page)
			continue;

		/* If isolation recently failed, do not retry */
		if (!isolation_suitable(cc, page))
			continue;

		/*
		 * For async compaction, also only scan in MOVABLE blocks.
		 * Async compaction is optimistic to see if the minimum amount
		 * of work satisfies the allocation.
		 */
		if (cc->mode == MIGRATE_ASYNC &&
		    !migrate_async_suitable(get_pageblock_migratetype(page)))
			continue;

		/* Perform the isolation */
		low_pfn = isolate_migratepages_block(cc, low_pfn,
						block_end_pfn, isolate_mode);

		if (!low_pfn || cc->contended) {
			acct_isolated(zone, cc);
			return ISOLATE_ABORT;
		}

		/*
		 * Either we isolated something and proceed with migration. Or
		 * we failed and compact_zone should decide if we should
		 * continue or not.
		 */
		break;
	}

	acct_isolated(zone, cc);
	/* Record where migration scanner will be restarted. */
	cc->migrate_pfn = low_pfn;

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

static enum compact_result __compact_finished(struct zone *zone, struct compact_control *cc,
			    const int migratetype)
{
	unsigned int order;
	unsigned long watermark;

	if (cc->contended || fatal_signal_pending(current))
		return COMPACT_CONTENDED;

	/* Compaction run completes if the migrate and free scanner meet */
	if (compact_scanners_met(cc)) {
		/* Let the next compaction start anew. */
		reset_cached_positions(zone);

		/*
		 * Mark that the PG_migrate_skip information should be cleared
		 * by kswapd when it goes to sleep. kcompactd does not set the
		 * flag itself as the decision to be clear should be directly
		 * based on an allocation request.
		 */
		if (cc->direct_compaction)
			zone->compact_blockskip_flush = true;

		return COMPACT_COMPLETE;
	}

	if (is_via_compact_memory(cc->order))
		return COMPACT_CONTINUE;

	/* Compaction run is not finished if the watermark is not met */
	watermark = low_wmark_pages(zone);

	if (!zone_watermark_ok(zone, cc->order, watermark, cc->classzone_idx,
							cc->alloc_flags))
		return COMPACT_CONTINUE;

	/* Direct compactor: Is a suitable page free? */
	for (order = cc->order; order < MAX_ORDER; order++) {
		struct free_area *area = &zone->free_area[order];
		bool can_steal;

		/* Job done if page is free of the right migratetype */
		if (!list_empty(&area->free_list[migratetype]))
			return COMPACT_PARTIAL;

#ifdef CONFIG_CMA
		/* MIGRATE_MOVABLE can fallback on MIGRATE_CMA */
		if (migratetype == MIGRATE_MOVABLE &&
			!list_empty(&area->free_list[MIGRATE_CMA]))
			return COMPACT_PARTIAL;
#endif
		/*
		 * Job done if allocation would steal freepages from
		 * other migratetype buddy lists.
		 */
		if (find_suitable_fallback(area, order, migratetype,
						true, &can_steal) != -1)
			return COMPACT_PARTIAL;
	}

	return COMPACT_NO_SUITABLE_PAGE;
}

static enum compact_result compact_finished(struct zone *zone,
			struct compact_control *cc,
			const int migratetype)
{
	int ret;

	ret = __compact_finished(zone, cc, migratetype);
	trace_mm_compaction_finished(zone, cc->order, ret);
	if (ret == COMPACT_NO_SUITABLE_PAGE)
		ret = COMPACT_CONTINUE;

	return ret;
}

/*
 * compaction_suitable: Is this suitable to run compaction on this zone now?
 * Returns
 *   COMPACT_SKIPPED  - If there are too few free pages for compaction
 *   COMPACT_PARTIAL  - If the allocation would succeed without compaction
 *   COMPACT_CONTINUE - If compaction should run now
 */
static enum compact_result __compaction_suitable(struct zone *zone, int order,
					unsigned int alloc_flags,
					int classzone_idx)
{
	int fragindex;
	unsigned long watermark;

	if (is_via_compact_memory(order))
		return COMPACT_CONTINUE;

	watermark = low_wmark_pages(zone);
	/*
	 * If watermarks for high-order allocation are already met, there
	 * should be no need for compaction at all.
	 */
	if (zone_watermark_ok(zone, order, watermark, classzone_idx,
								alloc_flags))
		return COMPACT_PARTIAL;

	/*
	 * Watermarks for order-0 must be met for compaction. Note the 2UL.
	 * This is because during migration, copies of pages need to be
	 * allocated and for a short time, the footprint is higher
	 */
	watermark += (2UL << order);
	if (!zone_watermark_ok(zone, 0, watermark, classzone_idx, alloc_flags))
		return COMPACT_SKIPPED;

	/*
	 * fragmentation index determines if allocation failures are due to
	 * low memory or external fragmentation
	 *
	 * index of -1000 would imply allocations might succeed depending on
	 * watermarks, but we already failed the high-order watermark check
	 * index towards 0 implies failure is due to lack of memory
	 * index towards 1000 implies failure is due to fragmentation
	 *
	 * Only compact if a failure would be due to fragmentation.
	 */
	fragindex = fragmentation_index(zone, order);
	if (fragindex >= 0 && fragindex <= sysctl_extfrag_threshold)
		return COMPACT_NOT_SUITABLE_ZONE;

	return COMPACT_CONTINUE;
}

enum compact_result compaction_suitable(struct zone *zone, int order,
					unsigned int alloc_flags,
					int classzone_idx)
{
	enum compact_result ret;

	ret = __compaction_suitable(zone, order, alloc_flags, classzone_idx);
	trace_mm_compaction_suitable(zone, order, ret);
	if (ret == COMPACT_NOT_SUITABLE_ZONE)
		ret = COMPACT_SKIPPED;

	return ret;
}

static enum compact_result compact_zone(struct zone *zone, struct compact_control *cc)
{
	enum compact_result ret;
	unsigned long start_pfn = zone->zone_start_pfn;
	unsigned long end_pfn = zone_end_pfn(zone);
	const int migratetype = gfpflags_to_migratetype(cc->gfp_mask);
	const bool sync = cc->mode != MIGRATE_ASYNC;

	ret = compaction_suitable(zone, cc->order, cc->alloc_flags,
							cc->classzone_idx);
	/* Compaction is likely to fail */
	if (ret == COMPACT_PARTIAL || ret == COMPACT_SKIPPED)
		return ret;

	/* huh, compaction_suitable is returning something unexpected */
	VM_BUG_ON(ret != COMPACT_CONTINUE);

	/*
	 * Clear pageblock skip if there were failures recently and compaction
	 * is about to be retried after being deferred.
	 */
	if (compaction_restarting(zone, cc->order))
		__reset_isolation_suitable(zone);

	/*
	 * Setup to move all movable pages to the end of the zone. Used cached
	 * information on where the scanners should start but check that it
	 * is initialised by ensuring the values are within zone boundaries.
	 */
	cc->migrate_pfn = zone->compact_cached_migrate_pfn[sync];
	cc->free_pfn = zone->compact_cached_free_pfn;
	if (cc->free_pfn < start_pfn || cc->free_pfn >= end_pfn) {
		cc->free_pfn = pageblock_start_pfn(end_pfn - 1);
		zone->compact_cached_free_pfn = cc->free_pfn;
	}
	if (cc->migrate_pfn < start_pfn || cc->migrate_pfn >= end_pfn) {
		cc->migrate_pfn = start_pfn;
		zone->compact_cached_migrate_pfn[0] = cc->migrate_pfn;
		zone->compact_cached_migrate_pfn[1] = cc->migrate_pfn;
	}
	cc->last_migrated_pfn = 0;

	trace_mm_compaction_begin(start_pfn, cc->migrate_pfn,
				cc->free_pfn, end_pfn, sync);

	migrate_prep_local();

	while ((ret = compact_finished(zone, cc, migratetype)) ==
						COMPACT_CONTINUE) {
		int err;

		switch (isolate_migratepages(zone, cc)) {
		case ISOLATE_ABORT:
			ret = COMPACT_CONTENDED;
			putback_movable_pages(&cc->migratepages);
			cc->nr_migratepages = 0;
			goto out;
		case ISOLATE_NONE:
			/*
			 * We haven't isolated and migrated anything, but
			 * there might still be unflushed migrations from
			 * previous cc->order aligned block.
			 */
			goto check_drain;
		case ISOLATE_SUCCESS:
			;
		}

		err = migrate_pages(&cc->migratepages, compaction_alloc,
				compaction_free, (unsigned long)cc, cc->mode,
				MR_COMPACTION);

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
				cc->last_migrated_pfn = 0;

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
		if (cc->order > 0 && cc->last_migrated_pfn) {
			int cpu;
			unsigned long current_block_start =
				block_start_pfn(cc->migrate_pfn, cc->order);

			if (cc->last_migrated_pfn < current_block_start) {
				cpu = get_cpu();
				lru_add_drain_cpu(cpu);
				drain_local_pages(zone);
				put_cpu();
				/* No more flushing until we migrate again */
				cc->last_migrated_pfn = 0;
			}
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
		if (free_pfn > zone->compact_cached_free_pfn)
			zone->compact_cached_free_pfn = free_pfn;
	}

	trace_mm_compaction_end(start_pfn, cc->migrate_pfn,
				cc->free_pfn, end_pfn, sync, ret);

	if (ret == COMPACT_CONTENDED)
		ret = COMPACT_PARTIAL;

	return ret;
}

static enum compact_result compact_zone_order(struct zone *zone, int order,
		gfp_t gfp_mask, enum migrate_mode mode, int *contended,
		unsigned int alloc_flags, int classzone_idx)
{
	enum compact_result ret;
	struct compact_control cc = {
		.nr_freepages = 0,
		.nr_migratepages = 0,
		.order = order,
		.gfp_mask = gfp_mask,
		.zone = zone,
		.mode = mode,
		.alloc_flags = alloc_flags,
		.classzone_idx = classzone_idx,
		.direct_compaction = true,
	};
	INIT_LIST_HEAD(&cc.freepages);
	INIT_LIST_HEAD(&cc.migratepages);

	ret = compact_zone(zone, &cc);

	VM_BUG_ON(!list_empty(&cc.freepages));
	VM_BUG_ON(!list_empty(&cc.migratepages));

	*contended = cc.contended;
	return ret;
}

int sysctl_extfrag_threshold = 500;

/**
 * try_to_compact_pages - Direct compact to satisfy a high-order allocation
 * @gfp_mask: The GFP mask of the current allocation
 * @order: The order of the current allocation
 * @alloc_flags: The allocation flags of the current allocation
 * @ac: The context of current allocation
 * @mode: The migration mode for async, sync light, or sync migration
 * @contended: Return value that determines if compaction was aborted due to
 *	       need_resched() or lock contention
 *
 * This is the main entry point for direct page compaction.
 */
enum compact_result try_to_compact_pages(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum migrate_mode mode, int *contended)
{
	int may_enter_fs = gfp_mask & __GFP_FS;
	int may_perform_io = gfp_mask & __GFP_IO;
	struct zoneref *z;
	struct zone *zone;
	enum compact_result rc = COMPACT_DEFERRED;
	int all_zones_contended = COMPACT_CONTENDED_LOCK; /* init for &= op */

	*contended = COMPACT_CONTENDED_NONE;

	/* Check if the GFP flags allow compaction */
	if (!order || !may_enter_fs || !may_perform_io)
		return COMPACT_SKIPPED;

	trace_mm_compaction_try_to_compact_pages(order, gfp_mask, mode);

	/* Compact each zone in the list */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist, ac->high_zoneidx,
								ac->nodemask) {
		enum compact_result status;
		int zone_contended;

		if (compaction_deferred(zone, order))
			continue;

		status = compact_zone_order(zone, order, gfp_mask, mode,
				&zone_contended, alloc_flags,
				ac_classzone_idx(ac));
		rc = max(status, rc);
		/*
		 * It takes at least one zone that wasn't lock contended
		 * to clear all_zones_contended.
		 */
		all_zones_contended &= zone_contended;

		/* If a normal allocation would succeed, stop compacting */
		if (zone_watermark_ok(zone, order, low_wmark_pages(zone),
					ac_classzone_idx(ac), alloc_flags)) {
			/*
			 * We think the allocation will succeed in this zone,
			 * but it is not certain, hence the false. The caller
			 * will repeat this with true if allocation indeed
			 * succeeds in this zone.
			 */
			compaction_defer_reset(zone, order, false);
			/*
			 * It is possible that async compaction aborted due to
			 * need_resched() and the watermarks were ok thanks to
			 * somebody else freeing memory. The allocation can
			 * however still fail so we better signal the
			 * need_resched() contention anyway (this will not
			 * prevent the allocation attempt).
			 */
			if (zone_contended == COMPACT_CONTENDED_SCHED)
				*contended = COMPACT_CONTENDED_SCHED;

			goto break_loop;
		}

		if (mode != MIGRATE_ASYNC && status == COMPACT_COMPLETE) {
			/*
			 * We think that allocation won't succeed in this zone
			 * so we defer compaction there. If it ends up
			 * succeeding after all, it will be reset.
			 */
			defer_compaction(zone, order);
		}

		/*
		 * We might have stopped compacting due to need_resched() in
		 * async compaction, or due to a fatal signal detected. In that
		 * case do not try further zones and signal need_resched()
		 * contention.
		 */
		if ((zone_contended == COMPACT_CONTENDED_SCHED)
					|| fatal_signal_pending(current)) {
			*contended = COMPACT_CONTENDED_SCHED;
			goto break_loop;
		}

		continue;
break_loop:
		/*
		 * We might not have tried all the zones, so  be conservative
		 * and assume they are not all lock contended.
		 */
		all_zones_contended = 0;
		break;
	}

	/*
	 * If at least one zone wasn't deferred or skipped, we report if all
	 * zones that were tried were lock contended.
	 */
	if (rc > COMPACT_SKIPPED && all_zones_contended)
		*contended = COMPACT_CONTENDED_LOCK;

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

		/*
		 * When called via /proc/sys/vm/compact_memory
		 * this makes sure we compact the whole zone regardless of
		 * cached scanner positions.
		 */
		if (is_via_compact_memory(cc->order))
			__reset_isolation_suitable(zone);

		if (is_via_compact_memory(cc->order) ||
				!compaction_deferred(zone, cc->order))
			compact_zone(zone, cc);

		VM_BUG_ON(!list_empty(&cc->freepages));
		VM_BUG_ON(!list_empty(&cc->migratepages));

		if (is_via_compact_memory(cc->order))
			continue;

		if (zone_watermark_ok(zone, cc->order,
				low_wmark_pages(zone), 0, 0))
			compaction_defer_reset(zone, cc->order, false);
	}
}

void compact_pgdat(pg_data_t *pgdat, int order)
{
	struct compact_control cc = {
		.order = order,
		.mode = MIGRATE_ASYNC,
	};

	if (!order)
		return;

	__compact_pgdat(pgdat, &cc);
}

static void compact_node(int nid)
{
	struct compact_control cc = {
		.order = -1,
		.mode = MIGRATE_SYNC,
		.ignore_skip_hint = true,
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

/*
 * This is the entry point for compacting all nodes via
 * /proc/sys/vm/compact_memory
 */
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
static ssize_t sysfs_compact_node(struct device *dev,
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

static inline bool kcompactd_work_requested(pg_data_t *pgdat)
{
	return pgdat->kcompactd_max_order > 0 || kthread_should_stop();
}

static bool kcompactd_node_suitable(pg_data_t *pgdat)
{
	int zoneid;
	struct zone *zone;
	enum zone_type classzone_idx = pgdat->kcompactd_classzone_idx;

	for (zoneid = 0; zoneid < classzone_idx; zoneid++) {
		zone = &pgdat->node_zones[zoneid];

		if (!populated_zone(zone))
			continue;

		if (compaction_suitable(zone, pgdat->kcompactd_max_order, 0,
					classzone_idx) == COMPACT_CONTINUE)
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
		.classzone_idx = pgdat->kcompactd_classzone_idx,
		.mode = MIGRATE_SYNC_LIGHT,
		.ignore_skip_hint = true,

	};
	bool success = false;

	trace_mm_compaction_kcompactd_wake(pgdat->node_id, cc.order,
							cc.classzone_idx);
	count_vm_event(KCOMPACTD_WAKE);

	for (zoneid = 0; zoneid < cc.classzone_idx; zoneid++) {
		int status;

		zone = &pgdat->node_zones[zoneid];
		if (!populated_zone(zone))
			continue;

		if (compaction_deferred(zone, cc.order))
			continue;

		if (compaction_suitable(zone, cc.order, 0, zoneid) !=
							COMPACT_CONTINUE)
			continue;

		cc.nr_freepages = 0;
		cc.nr_migratepages = 0;
		cc.zone = zone;
		INIT_LIST_HEAD(&cc.freepages);
		INIT_LIST_HEAD(&cc.migratepages);

		if (kthread_should_stop())
			return;
		status = compact_zone(zone, &cc);

		if (zone_watermark_ok(zone, cc.order, low_wmark_pages(zone),
						cc.classzone_idx, 0)) {
			success = true;
			compaction_defer_reset(zone, cc.order, false);
		} else if (status == COMPACT_COMPLETE) {
			/*
			 * We use sync migration mode here, so we defer like
			 * sync direct compaction does.
			 */
			defer_compaction(zone, cc.order);
		}

		VM_BUG_ON(!list_empty(&cc.freepages));
		VM_BUG_ON(!list_empty(&cc.migratepages));
	}

	/*
	 * Regardless of success, we are done until woken up next. But remember
	 * the requested order/classzone_idx in case it was higher/tighter than
	 * our current ones
	 */
	if (pgdat->kcompactd_max_order <= cc.order)
		pgdat->kcompactd_max_order = 0;
	if (pgdat->kcompactd_classzone_idx >= cc.classzone_idx)
		pgdat->kcompactd_classzone_idx = pgdat->nr_zones - 1;
}

void wakeup_kcompactd(pg_data_t *pgdat, int order, int classzone_idx)
{
	if (!order)
		return;

	if (pgdat->kcompactd_max_order < order)
		pgdat->kcompactd_max_order = order;

	if (pgdat->kcompactd_classzone_idx > classzone_idx)
		pgdat->kcompactd_classzone_idx = classzone_idx;

	if (!waitqueue_active(&pgdat->kcompactd_wait))
		return;

	if (!kcompactd_node_suitable(pgdat))
		return;

	trace_mm_compaction_wakeup_kcompactd(pgdat->node_id, order,
							classzone_idx);
	wake_up_interruptible(&pgdat->kcompactd_wait);
}

/*
 * The background compaction daemon, started as a kernel thread
 * from the init process.
 */
static int kcompactd(void *p)
{
	pg_data_t *pgdat = (pg_data_t*)p;
	struct task_struct *tsk = current;

	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);

	set_freezable();

	pgdat->kcompactd_max_order = 0;
	pgdat->kcompactd_classzone_idx = pgdat->nr_zones - 1;

	while (!kthread_should_stop()) {
		trace_mm_compaction_kcompactd_sleep(pgdat->node_id);
		wait_event_freezable(pgdat->kcompactd_wait,
				kcompactd_work_requested(pgdat));

		kcompactd_do_work(pgdat);
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
static int cpu_callback(struct notifier_block *nfb, unsigned long action,
			void *hcpu)
{
	int nid;

	if (action == CPU_ONLINE || action == CPU_ONLINE_FROZEN) {
		for_each_node_state(nid, N_MEMORY) {
			pg_data_t *pgdat = NODE_DATA(nid);
			const struct cpumask *mask;

			mask = cpumask_of_node(pgdat->node_id);

			if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids)
				/* One of our CPUs online: restore mask */
				set_cpus_allowed_ptr(pgdat->kcompactd, mask);
		}
	}
	return NOTIFY_OK;
}

static int __init kcompactd_init(void)
{
	int nid;

	for_each_node_state(nid, N_MEMORY)
		kcompactd_run(nid);
	hotcpu_notifier(cpu_callback, 0);
	return 0;
}
subsys_initcall(kcompactd_init)

#endif /* CONFIG_COMPACTION */
