// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the operation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * Documentation/admin-guide/sysctl/vm.rst.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/mm_inline.h>
#include <linux/percpu_counter.h>
#include <linux/memremap.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/backing-dev.h>
#include <linux/memcontrol.h>
#include <linux/gfp.h>
#include <linux/uio.h>
#include <linux/hugetlb.h>
#include <linux/page_idle.h>
#include <linux/local_lock.h>
#include <linux/buffer_head.h>

#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/pagemap.h>

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

/* Protecting only lru_rotate.fbatch which requires disabling interrupts */
struct lru_rotate {
	local_lock_t lock;
	struct folio_batch fbatch;
};
static DEFINE_PER_CPU(struct lru_rotate, lru_rotate) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

/*
 * The following struct pagevec are grouped together because they are protected
 * by disabling preemption (and interrupts remain enabled).
 */
struct lru_pvecs {
	local_lock_t lock;
	struct folio_batch lru_add;
	struct folio_batch lru_deactivate_file;
	struct folio_batch lru_deactivate;
	struct folio_batch lru_lazyfree;
#ifdef CONFIG_SMP
	struct folio_batch activate;
#endif
};
static DEFINE_PER_CPU(struct lru_pvecs, lru_pvecs) = {
	.lock = INIT_LOCAL_LOCK(lock),
};

/*
 * This path almost never happens for VM activity - pages are normally freed
 * via pagevecs.  But it gets used by networking - and for compound pages.
 */
static void __page_cache_release(struct page *page)
{
	if (PageLRU(page)) {
		struct folio *folio = page_folio(page);
		struct lruvec *lruvec;
		unsigned long flags;

		lruvec = folio_lruvec_lock_irqsave(folio, &flags);
		del_page_from_lru_list(page, lruvec);
		__clear_page_lru_flags(page);
		unlock_page_lruvec_irqrestore(lruvec, flags);
	}
	/* See comment on PageMlocked in release_pages() */
	if (unlikely(PageMlocked(page))) {
		int nr_pages = thp_nr_pages(page);

		__ClearPageMlocked(page);
		mod_zone_page_state(page_zone(page), NR_MLOCK, -nr_pages);
		count_vm_events(UNEVICTABLE_PGCLEARED, nr_pages);
	}
}

static void __put_single_page(struct page *page)
{
	__page_cache_release(page);
	mem_cgroup_uncharge(page_folio(page));
	free_unref_page(page, 0);
}

static void __put_compound_page(struct page *page)
{
	/*
	 * __page_cache_release() is supposed to be called for thp, not for
	 * hugetlb. This is because hugetlb page does never have PageLRU set
	 * (it's never listed to any LRU lists) and no memcg routines should
	 * be called for hugetlb (it has a separate hugetlb_cgroup.)
	 */
	if (!PageHuge(page))
		__page_cache_release(page);
	destroy_compound_page(page);
}

void __put_page(struct page *page)
{
	if (unlikely(is_zone_device_page(page)))
		free_zone_device_page(page);
	else if (unlikely(PageCompound(page)))
		__put_compound_page(page);
	else
		__put_single_page(page);
}
EXPORT_SYMBOL(__put_page);

/**
 * put_pages_list() - release a list of pages
 * @pages: list of pages threaded on page->lru
 *
 * Release a list of pages which are strung together on page.lru.
 */
void put_pages_list(struct list_head *pages)
{
	struct page *page, *next;

	list_for_each_entry_safe(page, next, pages, lru) {
		if (!put_page_testzero(page)) {
			list_del(&page->lru);
			continue;
		}
		if (PageHead(page)) {
			list_del(&page->lru);
			__put_compound_page(page);
			continue;
		}
		/* Cannot be PageLRU because it's passed to us using the lru */
	}

	free_unref_page_list(pages);
	INIT_LIST_HEAD(pages);
}
EXPORT_SYMBOL(put_pages_list);

/*
 * get_kernel_pages() - pin kernel pages in memory
 * @kiov:	An array of struct kvec structures
 * @nr_segs:	number of segments to pin
 * @write:	pinning for read/write, currently ignored
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_segs long.
 *
 * Returns number of pages pinned. This may be fewer than the number requested.
 * If nr_segs is 0 or negative, returns 0.  If no pages were pinned, returns 0.
 * Each page returned must be released with a put_page() call when it is
 * finished with.
 */
int get_kernel_pages(const struct kvec *kiov, int nr_segs, int write,
		struct page **pages)
{
	int seg;

	for (seg = 0; seg < nr_segs; seg++) {
		if (WARN_ON(kiov[seg].iov_len != PAGE_SIZE))
			return seg;

		pages[seg] = kmap_to_page(kiov[seg].iov_base);
		get_page(pages[seg]);
	}

	return seg;
}
EXPORT_SYMBOL_GPL(get_kernel_pages);

typedef void (*move_fn_t)(struct lruvec *lruvec, struct folio *folio);

static void lru_add_fn(struct lruvec *lruvec, struct folio *folio)
{
	int was_unevictable = folio_test_clear_unevictable(folio);
	long nr_pages = folio_nr_pages(folio);

	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

	/*
	 * Is an smp_mb__after_atomic() still required here, before
	 * folio_evictable() tests PageMlocked, to rule out the possibility
	 * of stranding an evictable folio on an unevictable LRU?  I think
	 * not, because __munlock_page() only clears PageMlocked while the LRU
	 * lock is held.
	 *
	 * (That is not true of __page_cache_release(), and not necessarily
	 * true of release_pages(): but those only clear PageMlocked after
	 * put_page_testzero() has excluded any other users of the page.)
	 */
	if (folio_evictable(folio)) {
		if (was_unevictable)
			__count_vm_events(UNEVICTABLE_PGRESCUED, nr_pages);
	} else {
		folio_clear_active(folio);
		folio_set_unevictable(folio);
		/*
		 * folio->mlock_count = !!folio_test_mlocked(folio)?
		 * But that leaves __mlock_page() in doubt whether another
		 * actor has already counted the mlock or not.  Err on the
		 * safe side, underestimate, let page reclaim fix it, rather
		 * than leaving a page on the unevictable LRU indefinitely.
		 */
		folio->mlock_count = 0;
		if (!was_unevictable)
			__count_vm_events(UNEVICTABLE_PGCULLED, nr_pages);
	}

	lruvec_add_folio(lruvec, folio);
	trace_mm_lru_insertion(folio);
}

static void folio_batch_move_lru(struct folio_batch *fbatch, move_fn_t move_fn)
{
	int i;
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;

	for (i = 0; i < folio_batch_count(fbatch); i++) {
		struct folio *folio = fbatch->folios[i];

		/* block memcg migration while the folio moves between lru */
		if (move_fn != lru_add_fn && !folio_test_clear_lru(folio))
			continue;

		lruvec = folio_lruvec_relock_irqsave(folio, lruvec, &flags);
		move_fn(lruvec, folio);

		folio_set_lru(folio);
	}

	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);
	folios_put(fbatch->folios, folio_batch_count(fbatch));
	folio_batch_init(fbatch);
}

static void folio_batch_add_and_move(struct folio_batch *fbatch,
		struct folio *folio, move_fn_t move_fn)
{
	if (folio_batch_add(fbatch, folio) && !folio_test_large(folio) &&
	    !lru_cache_disabled())
		return;
	folio_batch_move_lru(fbatch, move_fn);
}

static void lru_move_tail_fn(struct lruvec *lruvec, struct folio *folio)
{
	if (!folio_test_unevictable(folio)) {
		lruvec_del_folio(lruvec, folio);
		folio_clear_active(folio);
		lruvec_add_folio_tail(lruvec, folio);
		__count_vm_events(PGROTATED, folio_nr_pages(folio));
	}
}

/*
 * Writeback is about to end against a folio which has been marked for
 * immediate reclaim.  If it still appears to be reclaimable, move it
 * to the tail of the inactive list.
 *
 * folio_rotate_reclaimable() must disable IRQs, to prevent nasty races.
 */
void folio_rotate_reclaimable(struct folio *folio)
{
	if (!folio_test_locked(folio) && !folio_test_dirty(folio) &&
	    !folio_test_unevictable(folio) && folio_test_lru(folio)) {
		struct folio_batch *fbatch;
		unsigned long flags;

		folio_get(folio);
		local_lock_irqsave(&lru_rotate.lock, flags);
		fbatch = this_cpu_ptr(&lru_rotate.fbatch);
		folio_batch_add_and_move(fbatch, folio, lru_move_tail_fn);
		local_unlock_irqrestore(&lru_rotate.lock, flags);
	}
}

void lru_note_cost(struct lruvec *lruvec, bool file, unsigned int nr_pages)
{
	do {
		unsigned long lrusize;

		/*
		 * Hold lruvec->lru_lock is safe here, since
		 * 1) The pinned lruvec in reclaim, or
		 * 2) From a pre-LRU page during refault (which also holds the
		 *    rcu lock, so would be safe even if the page was on the LRU
		 *    and could move simultaneously to a new lruvec).
		 */
		spin_lock_irq(&lruvec->lru_lock);
		/* Record cost event */
		if (file)
			lruvec->file_cost += nr_pages;
		else
			lruvec->anon_cost += nr_pages;

		/*
		 * Decay previous events
		 *
		 * Because workloads change over time (and to avoid
		 * overflow) we keep these statistics as a floating
		 * average, which ends up weighing recent refaults
		 * more than old ones.
		 */
		lrusize = lruvec_page_state(lruvec, NR_INACTIVE_ANON) +
			  lruvec_page_state(lruvec, NR_ACTIVE_ANON) +
			  lruvec_page_state(lruvec, NR_INACTIVE_FILE) +
			  lruvec_page_state(lruvec, NR_ACTIVE_FILE);

		if (lruvec->file_cost + lruvec->anon_cost > lrusize / 4) {
			lruvec->file_cost /= 2;
			lruvec->anon_cost /= 2;
		}
		spin_unlock_irq(&lruvec->lru_lock);
	} while ((lruvec = parent_lruvec(lruvec)));
}

void lru_note_cost_folio(struct folio *folio)
{
	lru_note_cost(folio_lruvec(folio), folio_is_file_lru(folio),
			folio_nr_pages(folio));
}

static void folio_activate_fn(struct lruvec *lruvec, struct folio *folio)
{
	if (!folio_test_active(folio) && !folio_test_unevictable(folio)) {
		long nr_pages = folio_nr_pages(folio);

		lruvec_del_folio(lruvec, folio);
		folio_set_active(folio);
		lruvec_add_folio(lruvec, folio);
		trace_mm_lru_activate(folio);

		__count_vm_events(PGACTIVATE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGACTIVATE,
				     nr_pages);
	}
}

#ifdef CONFIG_SMP
static void folio_activate_drain(int cpu)
{
	struct folio_batch *fbatch = &per_cpu(lru_pvecs.activate, cpu);

	if (folio_batch_count(fbatch))
		folio_batch_move_lru(fbatch, folio_activate_fn);
}

static void folio_activate(struct folio *folio)
{
	if (folio_test_lru(folio) && !folio_test_active(folio) &&
	    !folio_test_unevictable(folio)) {
		struct folio_batch *fbatch;

		folio_get(folio);
		local_lock(&lru_pvecs.lock);
		fbatch = this_cpu_ptr(&lru_pvecs.activate);
		folio_batch_add_and_move(fbatch, folio, folio_activate_fn);
		local_unlock(&lru_pvecs.lock);
	}
}

#else
static inline void folio_activate_drain(int cpu)
{
}

static void folio_activate(struct folio *folio)
{
	struct lruvec *lruvec;

	if (folio_test_clear_lru(folio)) {
		lruvec = folio_lruvec_lock_irq(folio);
		folio_activate_fn(lruvec, folio);
		unlock_page_lruvec_irq(lruvec);
		folio_set_lru(folio);
	}
}
#endif

static void __lru_cache_activate_folio(struct folio *folio)
{
	struct folio_batch *fbatch;
	int i;

	local_lock(&lru_pvecs.lock);
	fbatch = this_cpu_ptr(&lru_pvecs.lru_add);

	/*
	 * Search backwards on the optimistic assumption that the folio being
	 * activated has just been added to this batch. Note that only
	 * the local batch is examined as a !LRU folio could be in the
	 * process of being released, reclaimed, migrated or on a remote
	 * batch that is currently being drained. Furthermore, marking
	 * a remote batch's folio active potentially hits a race where
	 * a folio is marked active just after it is added to the inactive
	 * list causing accounting errors and BUG_ON checks to trigger.
	 */
	for (i = folio_batch_count(fbatch) - 1; i >= 0; i--) {
		struct folio *batch_folio = fbatch->folios[i];

		if (batch_folio == folio) {
			folio_set_active(folio);
			break;
		}
	}

	local_unlock(&lru_pvecs.lock);
}

/*
 * Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 *
 * When a newly allocated page is not yet visible, so safe for non-atomic ops,
 * __SetPageReferenced(page) may be substituted for mark_page_accessed(page).
 */
void folio_mark_accessed(struct folio *folio)
{
	if (!folio_test_referenced(folio)) {
		folio_set_referenced(folio);
	} else if (folio_test_unevictable(folio)) {
		/*
		 * Unevictable pages are on the "LRU_UNEVICTABLE" list. But,
		 * this list is never rotated or maintained, so marking an
		 * unevictable page accessed has no effect.
		 */
	} else if (!folio_test_active(folio)) {
		/*
		 * If the folio is on the LRU, queue it for activation via
		 * lru_pvecs.activate. Otherwise, assume the folio is in a
		 * folio_batch, mark it active and it'll be moved to the active
		 * LRU on the next drain.
		 */
		if (folio_test_lru(folio))
			folio_activate(folio);
		else
			__lru_cache_activate_folio(folio);
		folio_clear_referenced(folio);
		workingset_activation(folio);
	}
	if (folio_test_idle(folio))
		folio_clear_idle(folio);
}
EXPORT_SYMBOL(folio_mark_accessed);

/**
 * folio_add_lru - Add a folio to an LRU list.
 * @folio: The folio to be added to the LRU.
 *
 * Queue the folio for addition to the LRU. The decision on whether
 * to add the page to the [in]active [file|anon] list is deferred until the
 * pagevec is drained. This gives a chance for the caller of folio_add_lru()
 * have the folio added to the active list using folio_mark_accessed().
 */
void folio_add_lru(struct folio *folio)
{
	struct folio_batch *fbatch;

	VM_BUG_ON_FOLIO(folio_test_active(folio) &&
			folio_test_unevictable(folio), folio);
	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

	folio_get(folio);
	local_lock(&lru_pvecs.lock);
	fbatch = this_cpu_ptr(&lru_pvecs.lru_add);
	folio_batch_add_and_move(fbatch, folio, lru_add_fn);
	local_unlock(&lru_pvecs.lock);
}
EXPORT_SYMBOL(folio_add_lru);

/**
 * lru_cache_add_inactive_or_unevictable
 * @page:  the page to be added to LRU
 * @vma:   vma in which page is mapped for determining reclaimability
 *
 * Place @page on the inactive or unevictable LRU list, depending on its
 * evictability.
 */
void lru_cache_add_inactive_or_unevictable(struct page *page,
					 struct vm_area_struct *vma)
{
	VM_BUG_ON_PAGE(PageLRU(page), page);

	if (unlikely((vma->vm_flags & (VM_LOCKED | VM_SPECIAL)) == VM_LOCKED))
		mlock_new_page(page);
	else
		lru_cache_add(page);
}

/*
 * If the folio cannot be invalidated, it is moved to the
 * inactive list to speed up its reclaim.  It is moved to the
 * head of the list, rather than the tail, to give the flusher
 * threads some time to write it out, as this is much more
 * effective than the single-page writeout from reclaim.
 *
 * If the folio isn't mapped and dirty/writeback, the folio
 * could be reclaimed asap using the reclaim flag.
 *
 * 1. active, mapped folio -> none
 * 2. active, dirty/writeback folio -> inactive, head, reclaim
 * 3. inactive, mapped folio -> none
 * 4. inactive, dirty/writeback folio -> inactive, head, reclaim
 * 5. inactive, clean -> inactive, tail
 * 6. Others -> none
 *
 * In 4, it moves to the head of the inactive list so the folio is
 * written out by flusher threads as this is much more efficient
 * than the single-page writeout from reclaim.
 */
static void lru_deactivate_file_fn(struct lruvec *lruvec, struct folio *folio)
{
	bool active = folio_test_active(folio);
	long nr_pages = folio_nr_pages(folio);

	if (folio_test_unevictable(folio))
		return;

	/* Some processes are using the folio */
	if (folio_mapped(folio))
		return;

	lruvec_del_folio(lruvec, folio);
	folio_clear_active(folio);
	folio_clear_referenced(folio);

	if (folio_test_writeback(folio) || folio_test_dirty(folio)) {
		/*
		 * Setting the reclaim flag could race with
		 * folio_end_writeback() and confuse readahead.  But the
		 * race window is _really_ small and  it's not a critical
		 * problem.
		 */
		lruvec_add_folio(lruvec, folio);
		folio_set_reclaim(folio);
	} else {
		/*
		 * The folio's writeback ended while it was in the batch.
		 * We move that folio to the tail of the inactive list.
		 */
		lruvec_add_folio_tail(lruvec, folio);
		__count_vm_events(PGROTATED, nr_pages);
	}

	if (active) {
		__count_vm_events(PGDEACTIVATE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGDEACTIVATE,
				     nr_pages);
	}
}

static void lru_deactivate_fn(struct lruvec *lruvec, struct folio *folio)
{
	if (folio_test_active(folio) && !folio_test_unevictable(folio)) {
		long nr_pages = folio_nr_pages(folio);

		lruvec_del_folio(lruvec, folio);
		folio_clear_active(folio);
		folio_clear_referenced(folio);
		lruvec_add_folio(lruvec, folio);

		__count_vm_events(PGDEACTIVATE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGDEACTIVATE,
				     nr_pages);
	}
}

static void lru_lazyfree_fn(struct lruvec *lruvec, struct folio *folio)
{
	if (folio_test_anon(folio) && folio_test_swapbacked(folio) &&
	    !folio_test_swapcache(folio) && !folio_test_unevictable(folio)) {
		long nr_pages = folio_nr_pages(folio);

		lruvec_del_folio(lruvec, folio);
		folio_clear_active(folio);
		folio_clear_referenced(folio);
		/*
		 * Lazyfree folios are clean anonymous folios.  They have
		 * the swapbacked flag cleared, to distinguish them from normal
		 * anonymous folios
		 */
		folio_clear_swapbacked(folio);
		lruvec_add_folio(lruvec, folio);

		__count_vm_events(PGLAZYFREE, nr_pages);
		__count_memcg_events(lruvec_memcg(lruvec), PGLAZYFREE,
				     nr_pages);
	}
}

/*
 * Drain pages out of the cpu's pagevecs.
 * Either "cpu" is the current CPU, and preemption has already been
 * disabled; or "cpu" is being hot-unplugged, and is already dead.
 */
void lru_add_drain_cpu(int cpu)
{
	struct folio_batch *fbatch = &per_cpu(lru_pvecs.lru_add, cpu);

	if (folio_batch_count(fbatch))
		folio_batch_move_lru(fbatch, lru_add_fn);

	fbatch = &per_cpu(lru_rotate.fbatch, cpu);
	/* Disabling interrupts below acts as a compiler barrier. */
	if (data_race(folio_batch_count(fbatch))) {
		unsigned long flags;

		/* No harm done if a racing interrupt already did this */
		local_lock_irqsave(&lru_rotate.lock, flags);
		folio_batch_move_lru(fbatch, lru_move_tail_fn);
		local_unlock_irqrestore(&lru_rotate.lock, flags);
	}

	fbatch = &per_cpu(lru_pvecs.lru_deactivate_file, cpu);
	if (folio_batch_count(fbatch))
		folio_batch_move_lru(fbatch, lru_deactivate_file_fn);

	fbatch = &per_cpu(lru_pvecs.lru_deactivate, cpu);
	if (folio_batch_count(fbatch))
		folio_batch_move_lru(fbatch, lru_deactivate_fn);

	fbatch = &per_cpu(lru_pvecs.lru_lazyfree, cpu);
	if (folio_batch_count(fbatch))
		folio_batch_move_lru(fbatch, lru_lazyfree_fn);

	folio_activate_drain(cpu);
}

/**
 * deactivate_file_folio() - Deactivate a file folio.
 * @folio: Folio to deactivate.
 *
 * This function hints to the VM that @folio is a good reclaim candidate,
 * for example if its invalidation fails due to the folio being dirty
 * or under writeback.
 *
 * Context: Caller holds a reference on the folio.
 */
void deactivate_file_folio(struct folio *folio)
{
	struct folio_batch *fbatch;

	/* Deactivating an unevictable folio will not accelerate reclaim */
	if (folio_test_unevictable(folio))
		return;

	folio_get(folio);
	local_lock(&lru_pvecs.lock);
	fbatch = this_cpu_ptr(&lru_pvecs.lru_deactivate_file);
	folio_batch_add_and_move(fbatch, folio, lru_deactivate_file_fn);
	local_unlock(&lru_pvecs.lock);
}

/*
 * deactivate_page - deactivate a page
 * @page: page to deactivate
 *
 * deactivate_page() moves @page to the inactive list if @page was on the active
 * list and was not an unevictable page.  This is done to accelerate the reclaim
 * of @page.
 */
void deactivate_page(struct page *page)
{
	struct folio *folio = page_folio(page);

	if (folio_test_lru(folio) && folio_test_active(folio) &&
	    !folio_test_unevictable(folio)) {
		struct folio_batch *fbatch;

		folio_get(folio);
		local_lock(&lru_pvecs.lock);
		fbatch = this_cpu_ptr(&lru_pvecs.lru_deactivate);
		folio_batch_add_and_move(fbatch, folio, lru_deactivate_fn);
		local_unlock(&lru_pvecs.lock);
	}
}

/**
 * mark_page_lazyfree - make an anon page lazyfree
 * @page: page to deactivate
 *
 * mark_page_lazyfree() moves @page to the inactive file list.
 * This is done to accelerate the reclaim of @page.
 */
void mark_page_lazyfree(struct page *page)
{
	struct folio *folio = page_folio(page);

	if (folio_test_lru(folio) && folio_test_anon(folio) &&
	    folio_test_swapbacked(folio) && !folio_test_swapcache(folio) &&
	    !folio_test_unevictable(folio)) {
		struct folio_batch *fbatch;

		folio_get(folio);
		local_lock(&lru_pvecs.lock);
		fbatch = this_cpu_ptr(&lru_pvecs.lru_lazyfree);
		folio_batch_add_and_move(fbatch, folio, lru_lazyfree_fn);
		local_unlock(&lru_pvecs.lock);
	}
}

void lru_add_drain(void)
{
	local_lock(&lru_pvecs.lock);
	lru_add_drain_cpu(smp_processor_id());
	local_unlock(&lru_pvecs.lock);
	mlock_page_drain_local();
}

/*
 * It's called from per-cpu workqueue context in SMP case so
 * lru_add_drain_cpu and invalidate_bh_lrus_cpu should run on
 * the same cpu. It shouldn't be a problem in !SMP case since
 * the core is only one and the locks will disable preemption.
 */
static void lru_add_and_bh_lrus_drain(void)
{
	local_lock(&lru_pvecs.lock);
	lru_add_drain_cpu(smp_processor_id());
	local_unlock(&lru_pvecs.lock);
	invalidate_bh_lrus_cpu();
	mlock_page_drain_local();
}

void lru_add_drain_cpu_zone(struct zone *zone)
{
	local_lock(&lru_pvecs.lock);
	lru_add_drain_cpu(smp_processor_id());
	drain_local_pages(zone);
	local_unlock(&lru_pvecs.lock);
	mlock_page_drain_local();
}

#ifdef CONFIG_SMP

static DEFINE_PER_CPU(struct work_struct, lru_add_drain_work);

static void lru_add_drain_per_cpu(struct work_struct *dummy)
{
	lru_add_and_bh_lrus_drain();
}

/*
 * Doesn't need any cpu hotplug locking because we do rely on per-cpu
 * kworkers being shut down before our page_alloc_cpu_dead callback is
 * executed on the offlined cpu.
 * Calling this function with cpu hotplug locks held can actually lead
 * to obscure indirect dependencies via WQ context.
 */
static inline void __lru_add_drain_all(bool force_all_cpus)
{
	/*
	 * lru_drain_gen - Global pages generation number
	 *
	 * (A) Definition: global lru_drain_gen = x implies that all generations
	 *     0 < n <= x are already *scheduled* for draining.
	 *
	 * This is an optimization for the highly-contended use case where a
	 * user space workload keeps constantly generating a flow of pages for
	 * each CPU.
	 */
	static unsigned int lru_drain_gen;
	static struct cpumask has_work;
	static DEFINE_MUTEX(lock);
	unsigned cpu, this_gen;

	/*
	 * Make sure nobody triggers this path before mm_percpu_wq is fully
	 * initialized.
	 */
	if (WARN_ON(!mm_percpu_wq))
		return;

	/*
	 * Guarantee pagevec counter stores visible by this CPU are visible to
	 * other CPUs before loading the current drain generation.
	 */
	smp_mb();

	/*
	 * (B) Locally cache global LRU draining generation number
	 *
	 * The read barrier ensures that the counter is loaded before the mutex
	 * is taken. It pairs with smp_mb() inside the mutex critical section
	 * at (D).
	 */
	this_gen = smp_load_acquire(&lru_drain_gen);

	mutex_lock(&lock);

	/*
	 * (C) Exit the draining operation if a newer generation, from another
	 * lru_add_drain_all(), was already scheduled for draining. Check (A).
	 */
	if (unlikely(this_gen != lru_drain_gen && !force_all_cpus))
		goto done;

	/*
	 * (D) Increment global generation number
	 *
	 * Pairs with smp_load_acquire() at (B), outside of the critical
	 * section. Use a full memory barrier to guarantee that the new global
	 * drain generation number is stored before loading pagevec counters.
	 *
	 * This pairing must be done here, before the for_each_online_cpu loop
	 * below which drains the page vectors.
	 *
	 * Let x, y, and z represent some system CPU numbers, where x < y < z.
	 * Assume CPU #z is in the middle of the for_each_online_cpu loop
	 * below and has already reached CPU #y's per-cpu data. CPU #x comes
	 * along, adds some pages to its per-cpu vectors, then calls
	 * lru_add_drain_all().
	 *
	 * If the paired barrier is done at any later step, e.g. after the
	 * loop, CPU #x will just exit at (C) and miss flushing out all of its
	 * added pages.
	 */
	WRITE_ONCE(lru_drain_gen, lru_drain_gen + 1);
	smp_mb();

	cpumask_clear(&has_work);
	for_each_online_cpu(cpu) {
		struct work_struct *work = &per_cpu(lru_add_drain_work, cpu);

		if (folio_batch_count(&per_cpu(lru_pvecs.lru_add, cpu)) ||
		    data_race(folio_batch_count(&per_cpu(lru_rotate.fbatch, cpu))) ||
		    folio_batch_count(&per_cpu(lru_pvecs.lru_deactivate_file, cpu)) ||
		    folio_batch_count(&per_cpu(lru_pvecs.lru_deactivate, cpu)) ||
		    folio_batch_count(&per_cpu(lru_pvecs.lru_lazyfree, cpu)) ||
		    folio_batch_count(&per_cpu(lru_pvecs.activate, cpu)) ||
		    need_mlock_page_drain(cpu) ||
		    has_bh_in_lru(cpu, NULL)) {
			INIT_WORK(work, lru_add_drain_per_cpu);
			queue_work_on(cpu, mm_percpu_wq, work);
			__cpumask_set_cpu(cpu, &has_work);
		}
	}

	for_each_cpu(cpu, &has_work)
		flush_work(&per_cpu(lru_add_drain_work, cpu));

done:
	mutex_unlock(&lock);
}

void lru_add_drain_all(void)
{
	__lru_add_drain_all(false);
}
#else
void lru_add_drain_all(void)
{
	lru_add_drain();
}
#endif /* CONFIG_SMP */

atomic_t lru_disable_count = ATOMIC_INIT(0);

/*
 * lru_cache_disable() needs to be called before we start compiling
 * a list of pages to be migrated using isolate_lru_page().
 * It drains pages on LRU cache and then disable on all cpus until
 * lru_cache_enable is called.
 *
 * Must be paired with a call to lru_cache_enable().
 */
void lru_cache_disable(void)
{
	atomic_inc(&lru_disable_count);
	/*
	 * Readers of lru_disable_count are protected by either disabling
	 * preemption or rcu_read_lock:
	 *
	 * preempt_disable, local_irq_disable  [bh_lru_lock()]
	 * rcu_read_lock		       [rt_spin_lock CONFIG_PREEMPT_RT]
	 * preempt_disable		       [local_lock !CONFIG_PREEMPT_RT]
	 *
	 * Since v5.1 kernel, synchronize_rcu() is guaranteed to wait on
	 * preempt_disable() regions of code. So any CPU which sees
	 * lru_disable_count = 0 will have exited the critical
	 * section when synchronize_rcu() returns.
	 */
	synchronize_rcu_expedited();
#ifdef CONFIG_SMP
	__lru_add_drain_all(true);
#else
	lru_add_and_bh_lrus_drain();
#endif
}

/**
 * release_pages - batched put_page()
 * @pages: array of pages to release
 * @nr: number of pages
 *
 * Decrement the reference count on all the pages in @pages.  If it
 * fell to zero, remove the page from the LRU and free it.
 */
void release_pages(struct page **pages, int nr)
{
	int i;
	LIST_HEAD(pages_to_free);
	struct lruvec *lruvec = NULL;
	unsigned long flags = 0;
	unsigned int lock_batch;

	for (i = 0; i < nr; i++) {
		struct page *page = pages[i];
		struct folio *folio = page_folio(page);

		/*
		 * Make sure the IRQ-safe lock-holding time does not get
		 * excessive with a continuous string of pages from the
		 * same lruvec. The lock is held only if lruvec != NULL.
		 */
		if (lruvec && ++lock_batch == SWAP_CLUSTER_MAX) {
			unlock_page_lruvec_irqrestore(lruvec, flags);
			lruvec = NULL;
		}

		page = &folio->page;
		if (is_huge_zero_page(page))
			continue;

		if (is_zone_device_page(page)) {
			if (lruvec) {
				unlock_page_lruvec_irqrestore(lruvec, flags);
				lruvec = NULL;
			}
			if (put_devmap_managed_page(page))
				continue;
			if (put_page_testzero(page))
				free_zone_device_page(page);
			continue;
		}

		if (!put_page_testzero(page))
			continue;

		if (PageCompound(page)) {
			if (lruvec) {
				unlock_page_lruvec_irqrestore(lruvec, flags);
				lruvec = NULL;
			}
			__put_compound_page(page);
			continue;
		}

		if (PageLRU(page)) {
			struct lruvec *prev_lruvec = lruvec;

			lruvec = folio_lruvec_relock_irqsave(folio, lruvec,
									&flags);
			if (prev_lruvec != lruvec)
				lock_batch = 0;

			del_page_from_lru_list(page, lruvec);
			__clear_page_lru_flags(page);
		}

		/*
		 * In rare cases, when truncation or holepunching raced with
		 * munlock after VM_LOCKED was cleared, Mlocked may still be
		 * found set here.  This does not indicate a problem, unless
		 * "unevictable_pgs_cleared" appears worryingly large.
		 */
		if (unlikely(PageMlocked(page))) {
			__ClearPageMlocked(page);
			dec_zone_page_state(page, NR_MLOCK);
			count_vm_event(UNEVICTABLE_PGCLEARED);
		}

		list_add(&page->lru, &pages_to_free);
	}
	if (lruvec)
		unlock_page_lruvec_irqrestore(lruvec, flags);

	mem_cgroup_uncharge_list(&pages_to_free);
	free_unref_page_list(&pages_to_free);
}
EXPORT_SYMBOL(release_pages);

/*
 * The pages which we're about to release may be in the deferred lru-addition
 * queues.  That would prevent them from really being freed right now.  That's
 * OK from a correctness point of view but is inefficient - those pages may be
 * cache-warm and we want to give them back to the page allocator ASAP.
 *
 * So __pagevec_release() will drain those queues here.
 * folio_batch_move_lru() calls folios_put() directly to avoid
 * mutual recursion.
 */
void __pagevec_release(struct pagevec *pvec)
{
	if (!pvec->percpu_pvec_drained) {
		lru_add_drain();
		pvec->percpu_pvec_drained = true;
	}
	release_pages(pvec->pages, pagevec_count(pvec));
	pagevec_reinit(pvec);
}
EXPORT_SYMBOL(__pagevec_release);

/**
 * folio_batch_remove_exceptionals() - Prune non-folios from a batch.
 * @fbatch: The batch to prune
 *
 * find_get_entries() fills a batch with both folios and shadow/swap/DAX
 * entries.  This function prunes all the non-folio entries from @fbatch
 * without leaving holes, so that it can be passed on to folio-only batch
 * operations.
 */
void folio_batch_remove_exceptionals(struct folio_batch *fbatch)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < folio_batch_count(fbatch); i++) {
		struct folio *folio = fbatch->folios[i];
		if (!xa_is_value(folio))
			fbatch->folios[j++] = folio;
	}
	fbatch->nr = j;
}

/**
 * pagevec_lookup_range - gang pagecache lookup
 * @pvec:	Where the resulting pages are placed
 * @mapping:	The address_space to search
 * @start:	The starting page index
 * @end:	The final page index
 *
 * pagevec_lookup_range() will search for & return a group of up to PAGEVEC_SIZE
 * pages in the mapping starting from index @start and upto index @end
 * (inclusive).  The pages are placed in @pvec.  pagevec_lookup() takes a
 * reference against the pages in @pvec.
 *
 * The search returns a group of mapping-contiguous pages with ascending
 * indexes.  There may be holes in the indices due to not-present pages. We
 * also update @start to index the next page for the traversal.
 *
 * pagevec_lookup_range() returns the number of pages which were found. If this
 * number is smaller than PAGEVEC_SIZE, the end of specified range has been
 * reached.
 */
unsigned pagevec_lookup_range(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *start, pgoff_t end)
{
	pvec->nr = find_get_pages_range(mapping, start, end, PAGEVEC_SIZE,
					pvec->pages);
	return pagevec_count(pvec);
}
EXPORT_SYMBOL(pagevec_lookup_range);

unsigned pagevec_lookup_range_tag(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *index, pgoff_t end,
		xa_mark_t tag)
{
	pvec->nr = find_get_pages_range_tag(mapping, index, end, tag,
					PAGEVEC_SIZE, pvec->pages);
	return pagevec_count(pvec);
}
EXPORT_SYMBOL(pagevec_lookup_range_tag);

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = totalram_pages() >> (20 - PAGE_SHIFT);

	/* Use a smaller cluster for small-memory machines */
	if (megs < 16)
		page_cluster = 2;
	else
		page_cluster = 3;
	/*
	 * Right now other parts of the system means that we
	 * _really_ don't want to cluster much more
	 */
}
