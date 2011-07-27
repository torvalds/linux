/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the operation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * Documentation/sysctl/vm.txt.
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
#include <linux/module.h>
#include <linux/mm_inline.h>
#include <linux/buffer_head.h>	/* for try_to_release_page() */
#include <linux/percpu_counter.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/backing-dev.h>
#include <linux/memcontrol.h>
#include <linux/gfp.h>

#include "internal.h"

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

static DEFINE_PER_CPU(struct pagevec[NR_LRU_LISTS], lru_add_pvecs);
static DEFINE_PER_CPU(struct pagevec, lru_rotate_pvecs);
static DEFINE_PER_CPU(struct pagevec, lru_deactivate_pvecs);

/*
 * This path almost never happens for VM activity - pages are normally
 * freed via pagevecs.  But it gets used by networking.
 */
static void __page_cache_release(struct page *page)
{
	if (PageLRU(page)) {
		unsigned long flags;
		struct zone *zone = page_zone(page);

		spin_lock_irqsave(&zone->lru_lock, flags);
		VM_BUG_ON(!PageLRU(page));
		__ClearPageLRU(page);
		del_page_from_lru(zone, page);
		spin_unlock_irqrestore(&zone->lru_lock, flags);
	}
}

static void __put_single_page(struct page *page)
{
	__page_cache_release(page);
	free_hot_cold_page(page, 0);
}

static void __put_compound_page(struct page *page)
{
	compound_page_dtor *dtor;

	__page_cache_release(page);
	dtor = get_compound_page_dtor(page);
	(*dtor)(page);
}

static void put_compound_page(struct page *page)
{
	if (unlikely(PageTail(page))) {
		/* __split_huge_page_refcount can run under us */
		struct page *page_head = page->first_page;
		smp_rmb();
		/*
		 * If PageTail is still set after smp_rmb() we can be sure
		 * that the page->first_page we read wasn't a dangling pointer.
		 * See __split_huge_page_refcount() smp_wmb().
		 */
		if (likely(PageTail(page) && get_page_unless_zero(page_head))) {
			unsigned long flags;
			/*
			 * Verify that our page_head wasn't converted
			 * to a a regular page before we got a
			 * reference on it.
			 */
			if (unlikely(!PageHead(page_head))) {
				/* PageHead is cleared after PageTail */
				smp_rmb();
				VM_BUG_ON(PageTail(page));
				goto out_put_head;
			}
			/*
			 * Only run compound_lock on a valid PageHead,
			 * after having it pinned with
			 * get_page_unless_zero() above.
			 */
			smp_mb();
			/* page_head wasn't a dangling pointer */
			flags = compound_lock_irqsave(page_head);
			if (unlikely(!PageTail(page))) {
				/* __split_huge_page_refcount run before us */
				compound_unlock_irqrestore(page_head, flags);
				VM_BUG_ON(PageHead(page_head));
			out_put_head:
				if (put_page_testzero(page_head))
					__put_single_page(page_head);
			out_put_single:
				if (put_page_testzero(page))
					__put_single_page(page);
				return;
			}
			VM_BUG_ON(page_head != page->first_page);
			/*
			 * We can release the refcount taken by
			 * get_page_unless_zero now that
			 * split_huge_page_refcount is blocked on the
			 * compound_lock.
			 */
			if (put_page_testzero(page_head))
				VM_BUG_ON(1);
			/* __split_huge_page_refcount will wait now */
			VM_BUG_ON(atomic_read(&page->_count) <= 0);
			atomic_dec(&page->_count);
			VM_BUG_ON(atomic_read(&page_head->_count) <= 0);
			compound_unlock_irqrestore(page_head, flags);
			if (put_page_testzero(page_head)) {
				if (PageHead(page_head))
					__put_compound_page(page_head);
				else
					__put_single_page(page_head);
			}
		} else {
			/* page_head is a dangling pointer */
			VM_BUG_ON(PageTail(page));
			goto out_put_single;
		}
	} else if (put_page_testzero(page)) {
		if (PageHead(page))
			__put_compound_page(page);
		else
			__put_single_page(page);
	}
}

void put_page(struct page *page)
{
	if (unlikely(PageCompound(page)))
		put_compound_page(page);
	else if (put_page_testzero(page))
		__put_single_page(page);
}
EXPORT_SYMBOL(put_page);

/**
 * put_pages_list() - release a list of pages
 * @pages: list of pages threaded on page->lru
 *
 * Release a list of pages which are strung together on page.lru.  Currently
 * used by read_cache_pages() and related error recovery code.
 */
void put_pages_list(struct list_head *pages)
{
	while (!list_empty(pages)) {
		struct page *victim;

		victim = list_entry(pages->prev, struct page, lru);
		list_del(&victim->lru);
		page_cache_release(victim);
	}
}
EXPORT_SYMBOL(put_pages_list);

static void pagevec_lru_move_fn(struct pagevec *pvec,
				void (*move_fn)(struct page *page, void *arg),
				void *arg)
{
	int i;
	struct zone *zone = NULL;
	unsigned long flags = 0;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		if (pagezone != zone) {
			if (zone)
				spin_unlock_irqrestore(&zone->lru_lock, flags);
			zone = pagezone;
			spin_lock_irqsave(&zone->lru_lock, flags);
		}

		(*move_fn)(page, arg);
	}
	if (zone)
		spin_unlock_irqrestore(&zone->lru_lock, flags);
	release_pages(pvec->pages, pvec->nr, pvec->cold);
	pagevec_reinit(pvec);
}

static void pagevec_move_tail_fn(struct page *page, void *arg)
{
	int *pgmoved = arg;
	struct zone *zone = page_zone(page);

	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
		enum lru_list lru = page_lru_base_type(page);
		list_move_tail(&page->lru, &zone->lru[lru].list);
		mem_cgroup_rotate_reclaimable_page(page);
		(*pgmoved)++;
	}
}

/*
 * pagevec_move_tail() must be called with IRQ disabled.
 * Otherwise this may cause nasty races.
 */
static void pagevec_move_tail(struct pagevec *pvec)
{
	int pgmoved = 0;

	pagevec_lru_move_fn(pvec, pagevec_move_tail_fn, &pgmoved);
	__count_vm_events(PGROTATED, pgmoved);
}

/*
 * Writeback is about to end against a page which has been marked for immediate
 * reclaim.  If it still appears to be reclaimable, move it to the tail of the
 * inactive list.
 */
void rotate_reclaimable_page(struct page *page)
{
	if (!PageLocked(page) && !PageDirty(page) && !PageActive(page) &&
	    !PageUnevictable(page) && PageLRU(page)) {
		struct pagevec *pvec;
		unsigned long flags;

		page_cache_get(page);
		local_irq_save(flags);
		pvec = &__get_cpu_var(lru_rotate_pvecs);
		if (!pagevec_add(pvec, page))
			pagevec_move_tail(pvec);
		local_irq_restore(flags);
	}
}

static void update_page_reclaim_stat(struct zone *zone, struct page *page,
				     int file, int rotated)
{
	struct zone_reclaim_stat *reclaim_stat = &zone->reclaim_stat;
	struct zone_reclaim_stat *memcg_reclaim_stat;

	memcg_reclaim_stat = mem_cgroup_get_reclaim_stat_from_page(page);

	reclaim_stat->recent_scanned[file]++;
	if (rotated)
		reclaim_stat->recent_rotated[file]++;

	if (!memcg_reclaim_stat)
		return;

	memcg_reclaim_stat->recent_scanned[file]++;
	if (rotated)
		memcg_reclaim_stat->recent_rotated[file]++;
}

static void __activate_page(struct page *page, void *arg)
{
	struct zone *zone = page_zone(page);

	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
		int file = page_is_file_cache(page);
		int lru = page_lru_base_type(page);
		del_page_from_lru_list(zone, page, lru);

		SetPageActive(page);
		lru += LRU_ACTIVE;
		add_page_to_lru_list(zone, page, lru);
		__count_vm_event(PGACTIVATE);

		update_page_reclaim_stat(zone, page, file, 1);
	}
}

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct pagevec, activate_page_pvecs);

static void activate_page_drain(int cpu)
{
	struct pagevec *pvec = &per_cpu(activate_page_pvecs, cpu);

	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, __activate_page, NULL);
}

void activate_page(struct page *page)
{
	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
		struct pagevec *pvec = &get_cpu_var(activate_page_pvecs);

		page_cache_get(page);
		if (!pagevec_add(pvec, page))
			pagevec_lru_move_fn(pvec, __activate_page, NULL);
		put_cpu_var(activate_page_pvecs);
	}
}

#else
static inline void activate_page_drain(int cpu)
{
}

void activate_page(struct page *page)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(&zone->lru_lock);
	__activate_page(page, NULL);
	spin_unlock_irq(&zone->lru_lock);
}
#endif

/*
 * Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 */
void mark_page_accessed(struct page *page)
{
	if (!PageActive(page) && !PageUnevictable(page) &&
			PageReferenced(page) && PageLRU(page)) {
		activate_page(page);
		ClearPageReferenced(page);
	} else if (!PageReferenced(page)) {
		SetPageReferenced(page);
	}
}

EXPORT_SYMBOL(mark_page_accessed);

void __lru_cache_add(struct page *page, enum lru_list lru)
{
	struct pagevec *pvec = &get_cpu_var(lru_add_pvecs)[lru];

	page_cache_get(page);
	if (!pagevec_add(pvec, page))
		____pagevec_lru_add(pvec, lru);
	put_cpu_var(lru_add_pvecs);
}
EXPORT_SYMBOL(__lru_cache_add);

/**
 * lru_cache_add_lru - add a page to a page list
 * @page: the page to be added to the LRU.
 * @lru: the LRU list to which the page is added.
 */
void lru_cache_add_lru(struct page *page, enum lru_list lru)
{
	if (PageActive(page)) {
		VM_BUG_ON(PageUnevictable(page));
		ClearPageActive(page);
	} else if (PageUnevictable(page)) {
		VM_BUG_ON(PageActive(page));
		ClearPageUnevictable(page);
	}

	VM_BUG_ON(PageLRU(page) || PageActive(page) || PageUnevictable(page));
	__lru_cache_add(page, lru);
}

/**
 * add_page_to_unevictable_list - add a page to the unevictable list
 * @page:  the page to be added to the unevictable list
 *
 * Add page directly to its zone's unevictable list.  To avoid races with
 * tasks that might be making the page evictable, through eg. munlock,
 * munmap or exit, while it's not on the lru, we want to add the page
 * while it's locked or otherwise "invisible" to other tasks.  This is
 * difficult to do when using the pagevec cache, so bypass that.
 */
void add_page_to_unevictable_list(struct page *page)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(&zone->lru_lock);
	SetPageUnevictable(page);
	SetPageLRU(page);
	add_page_to_lru_list(zone, page, LRU_UNEVICTABLE);
	spin_unlock_irq(&zone->lru_lock);
}

/*
 * If the page can not be invalidated, it is moved to the
 * inactive list to speed up its reclaim.  It is moved to the
 * head of the list, rather than the tail, to give the flusher
 * threads some time to write it out, as this is much more
 * effective than the single-page writeout from reclaim.
 *
 * If the page isn't page_mapped and dirty/writeback, the page
 * could reclaim asap using PG_reclaim.
 *
 * 1. active, mapped page -> none
 * 2. active, dirty/writeback page -> inactive, head, PG_reclaim
 * 3. inactive, mapped page -> none
 * 4. inactive, dirty/writeback page -> inactive, head, PG_reclaim
 * 5. inactive, clean -> inactive, tail
 * 6. Others -> none
 *
 * In 4, why it moves inactive's head, the VM expects the page would
 * be write it out by flusher threads as this is much more effective
 * than the single-page writeout from reclaim.
 */
static void lru_deactivate_fn(struct page *page, void *arg)
{
	int lru, file;
	bool active;
	struct zone *zone = page_zone(page);

	if (!PageLRU(page))
		return;

	if (PageUnevictable(page))
		return;

	/* Some processes are using the page */
	if (page_mapped(page))
		return;

	active = PageActive(page);

	file = page_is_file_cache(page);
	lru = page_lru_base_type(page);
	del_page_from_lru_list(zone, page, lru + active);
	ClearPageActive(page);
	ClearPageReferenced(page);
	add_page_to_lru_list(zone, page, lru);

	if (PageWriteback(page) || PageDirty(page)) {
		/*
		 * PG_reclaim could be raced with end_page_writeback
		 * It can make readahead confusing.  But race window
		 * is _really_ small and  it's non-critical problem.
		 */
		SetPageReclaim(page);
	} else {
		/*
		 * The page's writeback ends up during pagevec
		 * We moves tha page into tail of inactive.
		 */
		list_move_tail(&page->lru, &zone->lru[lru].list);
		mem_cgroup_rotate_reclaimable_page(page);
		__count_vm_event(PGROTATED);
	}

	if (active)
		__count_vm_event(PGDEACTIVATE);
	update_page_reclaim_stat(zone, page, file, 0);
}

/*
 * Drain pages out of the cpu's pagevecs.
 * Either "cpu" is the current CPU, and preemption has already been
 * disabled; or "cpu" is being hot-unplugged, and is already dead.
 */
static void drain_cpu_pagevecs(int cpu)
{
	struct pagevec *pvecs = per_cpu(lru_add_pvecs, cpu);
	struct pagevec *pvec;
	int lru;

	for_each_lru(lru) {
		pvec = &pvecs[lru - LRU_BASE];
		if (pagevec_count(pvec))
			____pagevec_lru_add(pvec, lru);
	}

	pvec = &per_cpu(lru_rotate_pvecs, cpu);
	if (pagevec_count(pvec)) {
		unsigned long flags;

		/* No harm done if a racing interrupt already did this */
		local_irq_save(flags);
		pagevec_move_tail(pvec);
		local_irq_restore(flags);
	}

	pvec = &per_cpu(lru_deactivate_pvecs, cpu);
	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, lru_deactivate_fn, NULL);

	activate_page_drain(cpu);
}

/**
 * deactivate_page - forcefully deactivate a page
 * @page: page to deactivate
 *
 * This function hints the VM that @page is a good reclaim candidate,
 * for example if its invalidation fails due to the page being dirty
 * or under writeback.
 */
void deactivate_page(struct page *page)
{
	/*
	 * In a workload with many unevictable page such as mprotect, unevictable
	 * page deactivation for accelerating reclaim is pointless.
	 */
	if (PageUnevictable(page))
		return;

	if (likely(get_page_unless_zero(page))) {
		struct pagevec *pvec = &get_cpu_var(lru_deactivate_pvecs);

		if (!pagevec_add(pvec, page))
			pagevec_lru_move_fn(pvec, lru_deactivate_fn, NULL);
		put_cpu_var(lru_deactivate_pvecs);
	}
}

void lru_add_drain(void)
{
	drain_cpu_pagevecs(get_cpu());
	put_cpu();
}

static void lru_add_drain_per_cpu(struct work_struct *dummy)
{
	lru_add_drain();
}

/*
 * Returns 0 for success
 */
int lru_add_drain_all(void)
{
	return schedule_on_each_cpu(lru_add_drain_per_cpu);
}

/*
 * Batched page_cache_release().  Decrement the reference count on all the
 * passed pages.  If it fell to zero then remove the page from the LRU and
 * free it.
 *
 * Avoid taking zone->lru_lock if possible, but if it is taken, retain it
 * for the remainder of the operation.
 *
 * The locking in this function is against shrink_inactive_list(): we recheck
 * the page count inside the lock to see whether shrink_inactive_list()
 * grabbed the page via the LRU.  If it did, give up: shrink_inactive_list()
 * will free it.
 */
void release_pages(struct page **pages, int nr, int cold)
{
	int i;
	struct pagevec pages_to_free;
	struct zone *zone = NULL;
	unsigned long uninitialized_var(flags);

	pagevec_init(&pages_to_free, cold);
	for (i = 0; i < nr; i++) {
		struct page *page = pages[i];

		if (unlikely(PageCompound(page))) {
			if (zone) {
				spin_unlock_irqrestore(&zone->lru_lock, flags);
				zone = NULL;
			}
			put_compound_page(page);
			continue;
		}

		if (!put_page_testzero(page))
			continue;

		if (PageLRU(page)) {
			struct zone *pagezone = page_zone(page);

			if (pagezone != zone) {
				if (zone)
					spin_unlock_irqrestore(&zone->lru_lock,
									flags);
				zone = pagezone;
				spin_lock_irqsave(&zone->lru_lock, flags);
			}
			VM_BUG_ON(!PageLRU(page));
			__ClearPageLRU(page);
			del_page_from_lru(zone, page);
		}

		if (!pagevec_add(&pages_to_free, page)) {
			if (zone) {
				spin_unlock_irqrestore(&zone->lru_lock, flags);
				zone = NULL;
			}
			__pagevec_free(&pages_to_free);
			pagevec_reinit(&pages_to_free);
  		}
	}
	if (zone)
		spin_unlock_irqrestore(&zone->lru_lock, flags);

	pagevec_free(&pages_to_free);
}
EXPORT_SYMBOL(release_pages);

/*
 * The pages which we're about to release may be in the deferred lru-addition
 * queues.  That would prevent them from really being freed right now.  That's
 * OK from a correctness point of view but is inefficient - those pages may be
 * cache-warm and we want to give them back to the page allocator ASAP.
 *
 * So __pagevec_release() will drain those queues here.  __pagevec_lru_add()
 * and __pagevec_lru_add_active() call release_pages() directly to avoid
 * mutual recursion.
 */
void __pagevec_release(struct pagevec *pvec)
{
	lru_add_drain();
	release_pages(pvec->pages, pagevec_count(pvec), pvec->cold);
	pagevec_reinit(pvec);
}

EXPORT_SYMBOL(__pagevec_release);

/* used by __split_huge_page_refcount() */
void lru_add_page_tail(struct zone* zone,
		       struct page *page, struct page *page_tail)
{
	int active;
	enum lru_list lru;
	const int file = 0;
	struct list_head *head;

	VM_BUG_ON(!PageHead(page));
	VM_BUG_ON(PageCompound(page_tail));
	VM_BUG_ON(PageLRU(page_tail));
	VM_BUG_ON(!spin_is_locked(&zone->lru_lock));

	SetPageLRU(page_tail);

	if (page_evictable(page_tail, NULL)) {
		if (PageActive(page)) {
			SetPageActive(page_tail);
			active = 1;
			lru = LRU_ACTIVE_ANON;
		} else {
			active = 0;
			lru = LRU_INACTIVE_ANON;
		}
		update_page_reclaim_stat(zone, page_tail, file, active);
		if (likely(PageLRU(page)))
			head = page->lru.prev;
		else
			head = &zone->lru[lru].list;
		__add_page_to_lru_list(zone, page_tail, lru, head);
	} else {
		SetPageUnevictable(page_tail);
		add_page_to_lru_list(zone, page_tail, LRU_UNEVICTABLE);
	}
}

static void ____pagevec_lru_add_fn(struct page *page, void *arg)
{
	enum lru_list lru = (enum lru_list)arg;
	struct zone *zone = page_zone(page);
	int file = is_file_lru(lru);
	int active = is_active_lru(lru);

	VM_BUG_ON(PageActive(page));
	VM_BUG_ON(PageUnevictable(page));
	VM_BUG_ON(PageLRU(page));

	SetPageLRU(page);
	if (active)
		SetPageActive(page);
	update_page_reclaim_stat(zone, page, file, active);
	add_page_to_lru_list(zone, page, lru);
}

/*
 * Add the passed pages to the LRU, then drop the caller's refcount
 * on them.  Reinitialises the caller's pagevec.
 */
void ____pagevec_lru_add(struct pagevec *pvec, enum lru_list lru)
{
	VM_BUG_ON(is_unevictable_lru(lru));

	pagevec_lru_move_fn(pvec, ____pagevec_lru_add_fn, (void *)lru);
}

EXPORT_SYMBOL(____pagevec_lru_add);

/*
 * Try to drop buffers from the pages in a pagevec
 */
void pagevec_strip(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (page_has_private(page) && trylock_page(page)) {
			if (page_has_private(page))
				try_to_release_page(page, 0);
			unlock_page(page);
		}
	}
}

/**
 * pagevec_lookup - gang pagecache lookup
 * @pvec:	Where the resulting pages are placed
 * @mapping:	The address_space to search
 * @start:	The starting page index
 * @nr_pages:	The maximum number of pages
 *
 * pagevec_lookup() will search for and return a group of up to @nr_pages pages
 * in the mapping.  The pages are placed in @pvec.  pagevec_lookup() takes a
 * reference against the pages in @pvec.
 *
 * The search returns a group of mapping-contiguous pages with ascending
 * indexes.  There may be holes in the indices due to not-present pages.
 *
 * pagevec_lookup() returns the number of pages which were found.
 */
unsigned pagevec_lookup(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t start, unsigned nr_pages)
{
	pvec->nr = find_get_pages(mapping, start, nr_pages, pvec->pages);
	return pagevec_count(pvec);
}

EXPORT_SYMBOL(pagevec_lookup);

unsigned pagevec_lookup_tag(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t *index, int tag, unsigned nr_pages)
{
	pvec->nr = find_get_pages_tag(mapping, index, tag,
					nr_pages, pvec->pages);
	return pagevec_count(pvec);
}

EXPORT_SYMBOL(pagevec_lookup_tag);

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = totalram_pages >> (20 - PAGE_SHIFT);

#ifdef CONFIG_SWAP
	bdi_init(swapper_space.backing_dev_info);
#endif

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
