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

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

static DEFINE_PER_CPU(struct pagevec, lru_add_pvecs) = { 0, };
static DEFINE_PER_CPU(struct pagevec, lru_add_active_pvecs) = { 0, };
static DEFINE_PER_CPU(struct pagevec, lru_rotate_pvecs) = { 0, };

/*
 * This path almost never happens for VM activity - pages are normally
 * freed via pagevecs.  But it gets used by networking.
 */
static void fastcall __page_cache_release(struct page *page)
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
	free_hot_page(page);
}

static void put_compound_page(struct page *page)
{
	page = compound_head(page);
	if (put_page_testzero(page)) {
		compound_page_dtor *dtor;

		dtor = get_compound_page_dtor(page);
		(*dtor)(page);
	}
}

void put_page(struct page *page)
{
	if (unlikely(PageCompound(page)))
		put_compound_page(page);
	else if (put_page_testzero(page))
		__page_cache_release(page);
}
EXPORT_SYMBOL(put_page);

/**
 * put_pages_list(): release a list of pages
 *
 * Release a list of pages which are strung together on page.lru.  Currently
 * used by read_cache_pages() and related error recovery code.
 *
 * @pages: list of pages threaded on page->lru
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

/*
 * pagevec_move_tail() must be called with IRQ disabled.
 * Otherwise this may cause nasty races.
 */
static void pagevec_move_tail(struct pagevec *pvec)
{
	int i;
	int pgmoved = 0;
	struct zone *zone = NULL;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		if (pagezone != zone) {
			if (zone)
				spin_unlock(&zone->lru_lock);
			zone = pagezone;
			spin_lock(&zone->lru_lock);
		}
		if (PageLRU(page) && !PageActive(page)) {
			list_move_tail(&page->lru, &zone->inactive_list);
			pgmoved++;
		}
	}
	if (zone)
		spin_unlock(&zone->lru_lock);
	__count_vm_events(PGROTATED, pgmoved);
	release_pages(pvec->pages, pvec->nr, pvec->cold);
	pagevec_reinit(pvec);
}

/*
 * Writeback is about to end against a page which has been marked for immediate
 * reclaim.  If it still appears to be reclaimable, move it to the tail of the
 * inactive list.
 *
 * Returns zero if it cleared PG_writeback.
 */
int rotate_reclaimable_page(struct page *page)
{
	struct pagevec *pvec;
	unsigned long flags;

	if (PageLocked(page))
		return 1;
	if (PageDirty(page))
		return 1;
	if (PageActive(page))
		return 1;
	if (!PageLRU(page))
		return 1;

	page_cache_get(page);
	local_irq_save(flags);
	pvec = &__get_cpu_var(lru_rotate_pvecs);
	if (!pagevec_add(pvec, page))
		pagevec_move_tail(pvec);
	local_irq_restore(flags);

	if (!test_clear_page_writeback(page))
		BUG();

	return 0;
}

/*
 * FIXME: speed this up?
 */
void fastcall activate_page(struct page *page)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(&zone->lru_lock);
	if (PageLRU(page) && !PageActive(page)) {
		del_page_from_inactive_list(zone, page);
		SetPageActive(page);
		add_page_to_active_list(zone, page);
		__count_vm_event(PGACTIVATE);
	}
	spin_unlock_irq(&zone->lru_lock);
}

/*
 * Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 */
void fastcall mark_page_accessed(struct page *page)
{
	if (!PageActive(page) && PageReferenced(page) && PageLRU(page)) {
		activate_page(page);
		ClearPageReferenced(page);
	} else if (!PageReferenced(page)) {
		SetPageReferenced(page);
	}
}

EXPORT_SYMBOL(mark_page_accessed);

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
void fastcall lru_cache_add(struct page *page)
{
	struct pagevec *pvec = &get_cpu_var(lru_add_pvecs);

	page_cache_get(page);
	if (!pagevec_add(pvec, page))
		__pagevec_lru_add(pvec);
	put_cpu_var(lru_add_pvecs);
}

void fastcall lru_cache_add_active(struct page *page)
{
	struct pagevec *pvec = &get_cpu_var(lru_add_active_pvecs);

	page_cache_get(page);
	if (!pagevec_add(pvec, page))
		__pagevec_lru_add_active(pvec);
	put_cpu_var(lru_add_active_pvecs);
}

/*
 * Drain pages out of the cpu's pagevecs.
 * Either "cpu" is the current CPU, and preemption has already been
 * disabled; or "cpu" is being hot-unplugged, and is already dead.
 */
static void drain_cpu_pagevecs(int cpu)
{
	struct pagevec *pvec;

	pvec = &per_cpu(lru_add_pvecs, cpu);
	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);

	pvec = &per_cpu(lru_add_active_pvecs, cpu);
	if (pagevec_count(pvec))
		__pagevec_lru_add_active(pvec);

	pvec = &per_cpu(lru_rotate_pvecs, cpu);
	if (pagevec_count(pvec)) {
		unsigned long flags;

		/* No harm done if a racing interrupt already did this */
		local_irq_save(flags);
		pagevec_move_tail(pvec);
		local_irq_restore(flags);
	}
}

void lru_add_drain(void)
{
	drain_cpu_pagevecs(get_cpu());
	put_cpu();
}

#ifdef CONFIG_NUMA
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

#else

/*
 * Returns 0 for success
 */
int lru_add_drain_all(void)
{
	lru_add_drain();
	return 0;
}
#endif

/*
 * Batched page_cache_release().  Decrement the reference count on all the
 * passed pages.  If it fell to zero then remove the page from the LRU and
 * free it.
 *
 * Avoid taking zone->lru_lock if possible, but if it is taken, retain it
 * for the remainder of the operation.
 *
 * The locking in this function is against shrink_cache(): we recheck the
 * page count inside the lock to see whether shrink_cache grabbed the page
 * via the LRU.  If it did, give up: shrink_cache will free it.
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

/*
 * pagevec_release() for pages which are known to not be on the LRU
 *
 * This function reinitialises the caller's pagevec.
 */
void __pagevec_release_nonlru(struct pagevec *pvec)
{
	int i;
	struct pagevec pages_to_free;

	pagevec_init(&pages_to_free, pvec->cold);
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		VM_BUG_ON(PageLRU(page));
		if (put_page_testzero(page))
			pagevec_add(&pages_to_free, page);
	}
	pagevec_free(&pages_to_free);
	pagevec_reinit(pvec);
}

/*
 * Add the passed pages to the LRU, then drop the caller's refcount
 * on them.  Reinitialises the caller's pagevec.
 */
void __pagevec_lru_add(struct pagevec *pvec)
{
	int i;
	struct zone *zone = NULL;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		if (pagezone != zone) {
			if (zone)
				spin_unlock_irq(&zone->lru_lock);
			zone = pagezone;
			spin_lock_irq(&zone->lru_lock);
		}
		VM_BUG_ON(PageLRU(page));
		SetPageLRU(page);
		add_page_to_inactive_list(zone, page);
	}
	if (zone)
		spin_unlock_irq(&zone->lru_lock);
	release_pages(pvec->pages, pvec->nr, pvec->cold);
	pagevec_reinit(pvec);
}

EXPORT_SYMBOL(__pagevec_lru_add);

void __pagevec_lru_add_active(struct pagevec *pvec)
{
	int i;
	struct zone *zone = NULL;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		if (pagezone != zone) {
			if (zone)
				spin_unlock_irq(&zone->lru_lock);
			zone = pagezone;
			spin_lock_irq(&zone->lru_lock);
		}
		VM_BUG_ON(PageLRU(page));
		SetPageLRU(page);
		VM_BUG_ON(PageActive(page));
		SetPageActive(page);
		add_page_to_active_list(zone, page);
	}
	if (zone)
		spin_unlock_irq(&zone->lru_lock);
	release_pages(pvec->pages, pvec->nr, pvec->cold);
	pagevec_reinit(pvec);
}

/*
 * Try to drop buffers from the pages in a pagevec
 */
void pagevec_strip(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];

		if (PagePrivate(page) && !TestSetPageLocked(page)) {
			if (PagePrivate(page))
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

#ifdef CONFIG_SMP
/*
 * We tolerate a little inaccuracy to avoid ping-ponging the counter between
 * CPUs
 */
#define ACCT_THRESHOLD	max(16, NR_CPUS * 2)

static DEFINE_PER_CPU(long, committed_space) = 0;

void vm_acct_memory(long pages)
{
	long *local;

	preempt_disable();
	local = &__get_cpu_var(committed_space);
	*local += pages;
	if (*local > ACCT_THRESHOLD || *local < -ACCT_THRESHOLD) {
		atomic_add(*local, &vm_committed_space);
		*local = 0;
	}
	preempt_enable();
}

#ifdef CONFIG_HOTPLUG_CPU

/* Drop the CPU's cached committed space back into the central pool. */
static int cpu_swap_callback(struct notifier_block *nfb,
			     unsigned long action,
			     void *hcpu)
{
	long *committed;

	committed = &per_cpu(committed_space, (long)hcpu);
	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN) {
		atomic_add(*committed, &vm_committed_space);
		*committed = 0;
		drain_cpu_pagevecs((long)hcpu);
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */
#endif /* CONFIG_SMP */

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = num_physpages >> (20 - PAGE_SHIFT);

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
#ifdef CONFIG_HOTPLUG_CPU
	hotcpu_notifier(cpu_swap_callback, 0);
#endif
}
