/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/vmstat.h>
#include <linux/file.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>	/* for try_to_release_page(),
					buffer_heads_over_limit */
#include <linux/mm_inline.h>
#include <linux/pagevec.h>
#include <linux/backing-dev.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/compaction.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/memcontrol.h>
#include <linux/delayacct.h>
#include <linux/sysctl.h>
#include <linux/oom.h>
#include <linux/prefetch.h>

#include <asm/tlbflush.h>
#include <asm/div64.h>

#include <linux/swapops.h>

#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/vmscan.h>

/*
 * reclaim_mode determines how the inactive list is shrunk
 * RECLAIM_MODE_SINGLE: Reclaim only order-0 pages
 * RECLAIM_MODE_ASYNC:  Do not block
 * RECLAIM_MODE_SYNC:   Allow blocking e.g. call wait_on_page_writeback
 * RECLAIM_MODE_LUMPYRECLAIM: For high-order allocations, take a reference
 *			page from the LRU and reclaim all pages within a
 *			naturally aligned range
 * RECLAIM_MODE_COMPACTION: For high-order allocations, reclaim a number of
 *			order-0 pages and then compact the zone
 */
typedef unsigned __bitwise__ reclaim_mode_t;
#define RECLAIM_MODE_SINGLE		((__force reclaim_mode_t)0x01u)
#define RECLAIM_MODE_ASYNC		((__force reclaim_mode_t)0x02u)
#define RECLAIM_MODE_SYNC		((__force reclaim_mode_t)0x04u)
#define RECLAIM_MODE_LUMPYRECLAIM	((__force reclaim_mode_t)0x08u)
#define RECLAIM_MODE_COMPACTION		((__force reclaim_mode_t)0x10u)

struct scan_control {
	/* Incremented by the number of inactive pages that were scanned */
	unsigned long nr_scanned;

	/* Number of pages freed so far during a call to shrink_zones() */
	unsigned long nr_reclaimed;

	/* How many pages shrink_list() should reclaim */
	unsigned long nr_to_reclaim;

	unsigned long hibernation_mode;

	/* This context's GFP mask */
	gfp_t gfp_mask;

	int may_writepage;

	/* Can mapped pages be reclaimed? */
	int may_unmap;

	/* Can pages be swapped as part of reclaim? */
	int may_swap;

	int swappiness;

	int order;

	/*
	 * Intend to reclaim enough continuous memory rather than reclaim
	 * enough amount of memory. i.e, mode for high order allocation.
	 */
	reclaim_mode_t reclaim_mode;

	/* Which cgroup do we reclaim from */
	struct mem_cgroup *mem_cgroup;

	/*
	 * Nodemask of nodes allowed by the caller. If NULL, all nodes
	 * are scanned.
	 */
	nodemask_t	*nodemask;
};

#define lru_to_page(_head) (list_entry((_head)->prev, struct page, lru))

#ifdef ARCH_HAS_PREFETCH
#define prefetch_prev_lru_page(_page, _base, _field)			\
	do {								\
		if ((_page)->lru.prev != _base) {			\
			struct page *prev;				\
									\
			prev = lru_to_page(&(_page->lru));		\
			prefetch(&prev->_field);			\
		}							\
	} while (0)
#else
#define prefetch_prev_lru_page(_page, _base, _field) do { } while (0)
#endif

#ifdef ARCH_HAS_PREFETCHW
#define prefetchw_prev_lru_page(_page, _base, _field)			\
	do {								\
		if ((_page)->lru.prev != _base) {			\
			struct page *prev;				\
									\
			prev = lru_to_page(&(_page->lru));		\
			prefetchw(&prev->_field);			\
		}							\
	} while (0)
#else
#define prefetchw_prev_lru_page(_page, _base, _field) do { } while (0)
#endif

/*
 * From 0 .. 100.  Higher means more swappy.
 */
int vm_swappiness = 60;
long vm_total_pages;	/* The total number of pages which the VM controls */

static LIST_HEAD(shrinker_list);
static DECLARE_RWSEM(shrinker_rwsem);

#ifdef CONFIG_CGROUP_MEM_RES_CTLR
#define scanning_global_lru(sc)	(!(sc)->mem_cgroup)
#else
#define scanning_global_lru(sc)	(1)
#endif

static struct zone_reclaim_stat *get_reclaim_stat(struct zone *zone,
						  struct scan_control *sc)
{
	if (!scanning_global_lru(sc))
		return mem_cgroup_get_reclaim_stat(sc->mem_cgroup, zone);

	return &zone->reclaim_stat;
}

static unsigned long zone_nr_lru_pages(struct zone *zone,
				struct scan_control *sc, enum lru_list lru)
{
	if (!scanning_global_lru(sc))
		return mem_cgroup_zone_nr_lru_pages(sc->mem_cgroup, zone, lru);

	return zone_page_state(zone, NR_LRU_BASE + lru);
}


/*
 * Add a shrinker callback to be called from the vm
 */
void register_shrinker(struct shrinker *shrinker)
{
	shrinker->nr = 0;
	down_write(&shrinker_rwsem);
	list_add_tail(&shrinker->list, &shrinker_list);
	up_write(&shrinker_rwsem);
}
EXPORT_SYMBOL(register_shrinker);

/*
 * Remove one
 */
void unregister_shrinker(struct shrinker *shrinker)
{
	down_write(&shrinker_rwsem);
	list_del(&shrinker->list);
	up_write(&shrinker_rwsem);
}
EXPORT_SYMBOL(unregister_shrinker);

static inline int do_shrinker_shrink(struct shrinker *shrinker,
				     struct shrink_control *sc,
				     unsigned long nr_to_scan)
{
	sc->nr_to_scan = nr_to_scan;
	return (*shrinker->shrink)(shrinker, sc);
}

#define SHRINK_BATCH 128
/*
 * Call the shrink functions to age shrinkable caches
 *
 * Here we assume it costs one seek to replace a lru page and that it also
 * takes a seek to recreate a cache object.  With this in mind we age equal
 * percentages of the lru and ageable caches.  This should balance the seeks
 * generated by these structures.
 *
 * If the vm encountered mapped pages on the LRU it increase the pressure on
 * slab to avoid swapping.
 *
 * We do weird things to avoid (scanned*seeks*entries) overflowing 32 bits.
 *
 * `lru_pages' represents the number of on-LRU pages in all the zones which
 * are eligible for the caller's allocation attempt.  It is used for balancing
 * slab reclaim versus page reclaim.
 *
 * Returns the number of slab objects which we shrunk.
 */
unsigned long shrink_slab(struct shrink_control *shrink,
			  unsigned long nr_pages_scanned,
			  unsigned long lru_pages)
{
	struct shrinker *shrinker;
	unsigned long ret = 0;

	if (nr_pages_scanned == 0)
		nr_pages_scanned = SWAP_CLUSTER_MAX;

	if (!down_read_trylock(&shrinker_rwsem)) {
		/* Assume we'll be able to shrink next time */
		ret = 1;
		goto out;
	}

	list_for_each_entry(shrinker, &shrinker_list, list) {
		unsigned long long delta;
		long total_scan;
		long max_pass;
		int shrink_ret = 0;
		long nr;
		long new_nr;

		max_pass = do_shrinker_shrink(shrinker, shrink, 0);
		if (max_pass <= 0)
			continue;

		/*
		 * copy the current shrinker scan count into a local variable
		 * and zero it so that other concurrent shrinker invocations
		 * don't also do this scanning work.
		 */
		do {
			nr = shrinker->nr;
		} while (cmpxchg(&shrinker->nr, nr, 0) != nr);

		total_scan = nr;
		delta = (4 * nr_pages_scanned) / shrinker->seeks;
		delta *= max_pass;
		do_div(delta, lru_pages + 1);
		total_scan += delta;
		if (total_scan < 0) {
			printk(KERN_ERR "shrink_slab: %pF negative objects to "
			       "delete nr=%ld\n",
			       shrinker->shrink, total_scan);
			total_scan = max_pass;
		}

		/*
		 * We need to avoid excessive windup on filesystem shrinkers
		 * due to large numbers of GFP_NOFS allocations causing the
		 * shrinkers to return -1 all the time. This results in a large
		 * nr being built up so when a shrink that can do some work
		 * comes along it empties the entire cache due to nr >>>
		 * max_pass.  This is bad for sustaining a working set in
		 * memory.
		 *
		 * Hence only allow the shrinker to scan the entire cache when
		 * a large delta change is calculated directly.
		 */
		if (delta < max_pass / 4)
			total_scan = min(total_scan, max_pass / 2);

		/*
		 * Avoid risking looping forever due to too large nr value:
		 * never try to free more than twice the estimate number of
		 * freeable entries.
		 */
		if (total_scan > max_pass * 2)
			total_scan = max_pass * 2;

		trace_mm_shrink_slab_start(shrinker, shrink, nr,
					nr_pages_scanned, lru_pages,
					max_pass, delta, total_scan);

		while (total_scan >= SHRINK_BATCH) {
			long this_scan = SHRINK_BATCH;
			int nr_before;

			nr_before = do_shrinker_shrink(shrinker, shrink, 0);
			shrink_ret = do_shrinker_shrink(shrinker, shrink,
							this_scan);
			if (shrink_ret == -1)
				break;
			if (shrink_ret < nr_before)
				ret += nr_before - shrink_ret;
			count_vm_events(SLABS_SCANNED, this_scan);
			total_scan -= this_scan;

			cond_resched();
		}

		/*
		 * move the unused scan count back into the shrinker in a
		 * manner that handles concurrent updates. If we exhausted the
		 * scan, there is no need to do an update.
		 */
		do {
			nr = shrinker->nr;
			new_nr = total_scan + nr;
			if (total_scan <= 0)
				break;
		} while (cmpxchg(&shrinker->nr, nr, new_nr) != nr);

		trace_mm_shrink_slab_end(shrinker, shrink_ret, nr, new_nr);
	}
	up_read(&shrinker_rwsem);
out:
	cond_resched();
	return ret;
}

static void set_reclaim_mode(int priority, struct scan_control *sc,
				   bool sync)
{
	reclaim_mode_t syncmode = sync ? RECLAIM_MODE_SYNC : RECLAIM_MODE_ASYNC;

	/*
	 * Initially assume we are entering either lumpy reclaim or
	 * reclaim/compaction.Depending on the order, we will either set the
	 * sync mode or just reclaim order-0 pages later.
	 */
	if (COMPACTION_BUILD)
		sc->reclaim_mode = RECLAIM_MODE_COMPACTION;
	else
		sc->reclaim_mode = RECLAIM_MODE_LUMPYRECLAIM;

	/*
	 * Avoid using lumpy reclaim or reclaim/compaction if possible by
	 * restricting when its set to either costly allocations or when
	 * under memory pressure
	 */
	if (sc->order > PAGE_ALLOC_COSTLY_ORDER)
		sc->reclaim_mode |= syncmode;
	else if (sc->order && priority < DEF_PRIORITY - 2)
		sc->reclaim_mode |= syncmode;
	else
		sc->reclaim_mode = RECLAIM_MODE_SINGLE | RECLAIM_MODE_ASYNC;
}

static void reset_reclaim_mode(struct scan_control *sc)
{
	sc->reclaim_mode = RECLAIM_MODE_SINGLE | RECLAIM_MODE_ASYNC;
}

static inline int is_page_cache_freeable(struct page *page)
{
	/*
	 * A freeable page cache page is referenced only by the caller
	 * that isolated the page, the page cache radix tree and
	 * optional buffer heads at page->private.
	 */
	return page_count(page) - page_has_private(page) == 2;
}

static int may_write_to_queue(struct backing_dev_info *bdi,
			      struct scan_control *sc)
{
	if (current->flags & PF_SWAPWRITE)
		return 1;
	if (!bdi_write_congested(bdi))
		return 1;
	if (bdi == current->backing_dev_info)
		return 1;

	/* lumpy reclaim for hugepage often need a lot of write */
	if (sc->order > PAGE_ALLOC_COSTLY_ORDER)
		return 1;
	return 0;
}

/*
 * We detected a synchronous write error writing a page out.  Probably
 * -ENOSPC.  We need to propagate that into the address_space for a subsequent
 * fsync(), msync() or close().
 *
 * The tricky part is that after writepage we cannot touch the mapping: nothing
 * prevents it from being freed up.  But we have a ref on the page and once
 * that page is locked, the mapping is pinned.
 *
 * We're allowed to run sleeping lock_page() here because we know the caller has
 * __GFP_FS.
 */
static void handle_write_error(struct address_space *mapping,
				struct page *page, int error)
{
	lock_page(page);
	if (page_mapping(page) == mapping)
		mapping_set_error(mapping, error);
	unlock_page(page);
}

/* possible outcome of pageout() */
typedef enum {
	/* failed to write page out, page is locked */
	PAGE_KEEP,
	/* move page to the active list, page is locked */
	PAGE_ACTIVATE,
	/* page has been sent to the disk successfully, page is unlocked */
	PAGE_SUCCESS,
	/* page is clean and locked */
	PAGE_CLEAN,
} pageout_t;

/*
 * pageout is called by shrink_page_list() for each dirty page.
 * Calls ->writepage().
 */
static pageout_t pageout(struct page *page, struct address_space *mapping,
			 struct scan_control *sc)
{
	/*
	 * If the page is dirty, only perform writeback if that write
	 * will be non-blocking.  To prevent this allocation from being
	 * stalled by pagecache activity.  But note that there may be
	 * stalls if we need to run get_block().  We could test
	 * PagePrivate for that.
	 *
	 * If this process is currently in __generic_file_aio_write() against
	 * this page's queue, we can perform writeback even if that
	 * will block.
	 *
	 * If the page is swapcache, write it back even if that would
	 * block, for some throttling. This happens by accident, because
	 * swap_backing_dev_info is bust: it doesn't reflect the
	 * congestion state of the swapdevs.  Easy to fix, if needed.
	 */
	if (!is_page_cache_freeable(page))
		return PAGE_KEEP;
	if (!mapping) {
		/*
		 * Some data journaling orphaned pages can have
		 * page->mapping == NULL while being dirty with clean buffers.
		 */
		if (page_has_private(page)) {
			if (try_to_free_buffers(page)) {
				ClearPageDirty(page);
				printk("%s: orphaned page\n", __func__);
				return PAGE_CLEAN;
			}
		}
		return PAGE_KEEP;
	}
	if (mapping->a_ops->writepage == NULL)
		return PAGE_ACTIVATE;
	if (!may_write_to_queue(mapping->backing_dev_info, sc))
		return PAGE_KEEP;

	if (clear_page_dirty_for_io(page)) {
		int res;
		struct writeback_control wbc = {
			.sync_mode = WB_SYNC_NONE,
			.nr_to_write = SWAP_CLUSTER_MAX,
			.range_start = 0,
			.range_end = LLONG_MAX,
			.for_reclaim = 1,
		};

		SetPageReclaim(page);
		res = mapping->a_ops->writepage(page, &wbc);
		if (res < 0)
			handle_write_error(mapping, page, res);
		if (res == AOP_WRITEPAGE_ACTIVATE) {
			ClearPageReclaim(page);
			return PAGE_ACTIVATE;
		}

		/*
		 * Wait on writeback if requested to. This happens when
		 * direct reclaiming a large contiguous area and the
		 * first attempt to free a range of pages fails.
		 */
		if (PageWriteback(page) &&
		    (sc->reclaim_mode & RECLAIM_MODE_SYNC))
			wait_on_page_writeback(page);

		if (!PageWriteback(page)) {
			/* synchronous write or broken a_ops? */
			ClearPageReclaim(page);
		}
		trace_mm_vmscan_writepage(page,
			trace_reclaim_flags(page, sc->reclaim_mode));
		inc_zone_page_state(page, NR_VMSCAN_WRITE);
		return PAGE_SUCCESS;
	}

	return PAGE_CLEAN;
}

/*
 * Same as remove_mapping, but if the page is removed from the mapping, it
 * gets returned with a refcount of 0.
 */
static int __remove_mapping(struct address_space *mapping, struct page *page)
{
	BUG_ON(!PageLocked(page));
	BUG_ON(mapping != page_mapping(page));

	spin_lock_irq(&mapping->tree_lock);
	/*
	 * The non racy check for a busy page.
	 *
	 * Must be careful with the order of the tests. When someone has
	 * a ref to the page, it may be possible that they dirty it then
	 * drop the reference. So if PageDirty is tested before page_count
	 * here, then the following race may occur:
	 *
	 * get_user_pages(&page);
	 * [user mapping goes away]
	 * write_to(page);
	 *				!PageDirty(page)    [good]
	 * SetPageDirty(page);
	 * put_page(page);
	 *				!page_count(page)   [good, discard it]
	 *
	 * [oops, our write_to data is lost]
	 *
	 * Reversing the order of the tests ensures such a situation cannot
	 * escape unnoticed. The smp_rmb is needed to ensure the page->flags
	 * load is not satisfied before that of page->_count.
	 *
	 * Note that if SetPageDirty is always performed via set_page_dirty,
	 * and thus under tree_lock, then this ordering is not required.
	 */
	if (!page_freeze_refs(page, 2))
		goto cannot_free;
	/* note: atomic_cmpxchg in page_freeze_refs provides the smp_rmb */
	if (unlikely(PageDirty(page))) {
		page_unfreeze_refs(page, 2);
		goto cannot_free;
	}

	if (PageSwapCache(page)) {
		swp_entry_t swap = { .val = page_private(page) };
		__delete_from_swap_cache(page);
		spin_unlock_irq(&mapping->tree_lock);
		swapcache_free(swap, page);
	} else {
		void (*freepage)(struct page *);

		freepage = mapping->a_ops->freepage;

		__delete_from_page_cache(page);
		spin_unlock_irq(&mapping->tree_lock);
		mem_cgroup_uncharge_cache_page(page);

		if (freepage != NULL)
			freepage(page);
	}

	return 1;

cannot_free:
	spin_unlock_irq(&mapping->tree_lock);
	return 0;
}

/*
 * Attempt to detach a locked page from its ->mapping.  If it is dirty or if
 * someone else has a ref on the page, abort and return 0.  If it was
 * successfully detached, return 1.  Assumes the caller has a single ref on
 * this page.
 */
int remove_mapping(struct address_space *mapping, struct page *page)
{
	if (__remove_mapping(mapping, page)) {
		/*
		 * Unfreezing the refcount with 1 rather than 2 effectively
		 * drops the pagecache ref for us without requiring another
		 * atomic operation.
		 */
		page_unfreeze_refs(page, 1);
		return 1;
	}
	return 0;
}

/**
 * putback_lru_page - put previously isolated page onto appropriate LRU list
 * @page: page to be put back to appropriate lru list
 *
 * Add previously isolated @page to appropriate LRU list.
 * Page may still be unevictable for other reasons.
 *
 * lru_lock must not be held, interrupts must be enabled.
 */
void putback_lru_page(struct page *page)
{
	int lru;
	int active = !!TestClearPageActive(page);
	int was_unevictable = PageUnevictable(page);

	VM_BUG_ON(PageLRU(page));

redo:
	ClearPageUnevictable(page);

	if (page_evictable(page, NULL)) {
		/*
		 * For evictable pages, we can use the cache.
		 * In event of a race, worst case is we end up with an
		 * unevictable page on [in]active list.
		 * We know how to handle that.
		 */
		lru = active + page_lru_base_type(page);
		lru_cache_add_lru(page, lru);
	} else {
		/*
		 * Put unevictable pages directly on zone's unevictable
		 * list.
		 */
		lru = LRU_UNEVICTABLE;
		add_page_to_unevictable_list(page);
		/*
		 * When racing with an mlock clearing (page is
		 * unlocked), make sure that if the other thread does
		 * not observe our setting of PG_lru and fails
		 * isolation, we see PG_mlocked cleared below and move
		 * the page back to the evictable list.
		 *
		 * The other side is TestClearPageMlocked().
		 */
		smp_mb();
	}

	/*
	 * page's status can change while we move it among lru. If an evictable
	 * page is on unevictable list, it never be freed. To avoid that,
	 * check after we added it to the list, again.
	 */
	if (lru == LRU_UNEVICTABLE && page_evictable(page, NULL)) {
		if (!isolate_lru_page(page)) {
			put_page(page);
			goto redo;
		}
		/* This means someone else dropped this page from LRU
		 * So, it will be freed or putback to LRU again. There is
		 * nothing to do here.
		 */
	}

	if (was_unevictable && lru != LRU_UNEVICTABLE)
		count_vm_event(UNEVICTABLE_PGRESCUED);
	else if (!was_unevictable && lru == LRU_UNEVICTABLE)
		count_vm_event(UNEVICTABLE_PGCULLED);

	put_page(page);		/* drop ref from isolate */
}

enum page_references {
	PAGEREF_RECLAIM,
	PAGEREF_RECLAIM_CLEAN,
	PAGEREF_KEEP,
	PAGEREF_ACTIVATE,
};

static enum page_references page_check_references(struct page *page,
						  struct scan_control *sc)
{
	int referenced_ptes, referenced_page;
	unsigned long vm_flags;

	referenced_ptes = page_referenced(page, 1, sc->mem_cgroup, &vm_flags);
	referenced_page = TestClearPageReferenced(page);

	/* Lumpy reclaim - ignore references */
	if (sc->reclaim_mode & RECLAIM_MODE_LUMPYRECLAIM)
		return PAGEREF_RECLAIM;

	/*
	 * Mlock lost the isolation race with us.  Let try_to_unmap()
	 * move the page to the unevictable list.
	 */
	if (vm_flags & VM_LOCKED)
		return PAGEREF_RECLAIM;

	if (referenced_ptes) {
		if (PageSwapBacked(page))
			return PAGEREF_ACTIVATE;
		/*
		 * All mapped pages start out with page table
		 * references from the instantiating fault, so we need
		 * to look twice if a mapped file page is used more
		 * than once.
		 *
		 * Mark it and spare it for another trip around the
		 * inactive list.  Another page table reference will
		 * lead to its activation.
		 *
		 * Note: the mark is set for activated pages as well
		 * so that recently deactivated but used pages are
		 * quickly recovered.
		 */
		SetPageReferenced(page);

		if (referenced_page || referenced_ptes > 1)
			return PAGEREF_ACTIVATE;

		/*
		 * Activate file-backed executable pages after first usage.
		 */
		if (vm_flags & VM_EXEC)
			return PAGEREF_ACTIVATE;

		return PAGEREF_KEEP;
	}

	/* Reclaim if clean, defer dirty pages to writeback */
	if (referenced_page && !PageSwapBacked(page))
		return PAGEREF_RECLAIM_CLEAN;

	return PAGEREF_RECLAIM;
}

static noinline_for_stack void free_page_list(struct list_head *free_pages)
{
	struct pagevec freed_pvec;
	struct page *page, *tmp;

	pagevec_init(&freed_pvec, 1);

	list_for_each_entry_safe(page, tmp, free_pages, lru) {
		list_del(&page->lru);
		if (!pagevec_add(&freed_pvec, page)) {
			__pagevec_free(&freed_pvec);
			pagevec_reinit(&freed_pvec);
		}
	}

	pagevec_free(&freed_pvec);
}

/*
 * shrink_page_list() returns the number of reclaimed pages
 */
static unsigned long shrink_page_list(struct list_head *page_list,
				      struct zone *zone,
				      struct scan_control *sc)
{
	LIST_HEAD(ret_pages);
	LIST_HEAD(free_pages);
	int pgactivate = 0;
	unsigned long nr_dirty = 0;
	unsigned long nr_congested = 0;
	unsigned long nr_reclaimed = 0;

	cond_resched();

	while (!list_empty(page_list)) {
		enum page_references references;
		struct address_space *mapping;
		struct page *page;
		int may_enter_fs;

		cond_resched();

		page = lru_to_page(page_list);
		list_del(&page->lru);

		if (!trylock_page(page))
			goto keep;

		VM_BUG_ON(PageActive(page));
		VM_BUG_ON(page_zone(page) != zone);

		sc->nr_scanned++;

		if (unlikely(!page_evictable(page, NULL)))
			goto cull_mlocked;

		if (!sc->may_unmap && page_mapped(page))
			goto keep_locked;

		/* Double the slab pressure for mapped and swapcache pages */
		if (page_mapped(page) || PageSwapCache(page))
			sc->nr_scanned++;

		may_enter_fs = (sc->gfp_mask & __GFP_FS) ||
			(PageSwapCache(page) && (sc->gfp_mask & __GFP_IO));

		if (PageWriteback(page)) {
			/*
			 * Synchronous reclaim is performed in two passes,
			 * first an asynchronous pass over the list to
			 * start parallel writeback, and a second synchronous
			 * pass to wait for the IO to complete.  Wait here
			 * for any page for which writeback has already
			 * started.
			 */
			if ((sc->reclaim_mode & RECLAIM_MODE_SYNC) &&
			    may_enter_fs)
				wait_on_page_writeback(page);
			else {
				unlock_page(page);
				goto keep_lumpy;
			}
		}

		references = page_check_references(page, sc);
		switch (references) {
		case PAGEREF_ACTIVATE:
			goto activate_locked;
		case PAGEREF_KEEP:
			goto keep_locked;
		case PAGEREF_RECLAIM:
		case PAGEREF_RECLAIM_CLEAN:
			; /* try to reclaim the page below */
		}

		/*
		 * Anonymous process memory has backing store?
		 * Try to allocate it some swap space here.
		 */
		if (PageAnon(page) && !PageSwapCache(page)) {
			if (!(sc->gfp_mask & __GFP_IO))
				goto keep_locked;
			if (!add_to_swap(page))
				goto activate_locked;
			may_enter_fs = 1;
		}

		mapping = page_mapping(page);

		/*
		 * The page is mapped into the page tables of one or more
		 * processes. Try to unmap it here.
		 */
		if (page_mapped(page) && mapping) {
			switch (try_to_unmap(page, TTU_UNMAP)) {
			case SWAP_FAIL:
				goto activate_locked;
			case SWAP_AGAIN:
				goto keep_locked;
			case SWAP_MLOCK:
				goto cull_mlocked;
			case SWAP_SUCCESS:
				; /* try to free the page below */
			}
		}

		if (PageDirty(page)) {
			nr_dirty++;

			if (references == PAGEREF_RECLAIM_CLEAN)
				goto keep_locked;
			if (!may_enter_fs)
				goto keep_locked;
			if (!sc->may_writepage)
				goto keep_locked;

			/* Page is dirty, try to write it out here */
			switch (pageout(page, mapping, sc)) {
			case PAGE_KEEP:
				nr_congested++;
				goto keep_locked;
			case PAGE_ACTIVATE:
				goto activate_locked;
			case PAGE_SUCCESS:
				if (PageWriteback(page))
					goto keep_lumpy;
				if (PageDirty(page))
					goto keep;

				/*
				 * A synchronous write - probably a ramdisk.  Go
				 * ahead and try to reclaim the page.
				 */
				if (!trylock_page(page))
					goto keep;
				if (PageDirty(page) || PageWriteback(page))
					goto keep_locked;
				mapping = page_mapping(page);
			case PAGE_CLEAN:
				; /* try to free the page below */
			}
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we try to free
		 * the page as well.
		 *
		 * We do this even if the page is PageDirty().
		 * try_to_release_page() does not perform I/O, but it is
		 * possible for a page to have PageDirty set, but it is actually
		 * clean (all its buffers are clean).  This happens if the
		 * buffers were written out directly, with submit_bh(). ext3
		 * will do this, as well as the blockdev mapping.
		 * try_to_release_page() will discover that cleanness and will
		 * drop the buffers and mark the page clean - it can be freed.
		 *
		 * Rarely, pages can have buffers and no ->mapping.  These are
		 * the pages which were not successfully invalidated in
		 * truncate_complete_page().  We try to drop those buffers here
		 * and if that worked, and the page is no longer mapped into
		 * process address space (page_count == 1) it can be freed.
		 * Otherwise, leave the page on the LRU so it is swappable.
		 */
		if (page_has_private(page)) {
			if (!try_to_release_page(page, sc->gfp_mask))
				goto activate_locked;
			if (!mapping && page_count(page) == 1) {
				unlock_page(page);
				if (put_page_testzero(page))
					goto free_it;
				else {
					/*
					 * rare race with speculative reference.
					 * the speculative reference will free
					 * this page shortly, so we may
					 * increment nr_reclaimed here (and
					 * leave it off the LRU).
					 */
					nr_reclaimed++;
					continue;
				}
			}
		}

		if (!mapping || !__remove_mapping(mapping, page))
			goto keep_locked;

		/*
		 * At this point, we have no other references and there is
		 * no way to pick any more up (removed from LRU, removed
		 * from pagecache). Can use non-atomic bitops now (and
		 * we obviously don't have to worry about waking up a process
		 * waiting on the page lock, because there are no references.
		 */
		__clear_page_locked(page);
free_it:
		nr_reclaimed++;

		/*
		 * Is there need to periodically free_page_list? It would
		 * appear not as the counts should be low
		 */
		list_add(&page->lru, &free_pages);
		continue;

cull_mlocked:
		if (PageSwapCache(page))
			try_to_free_swap(page);
		unlock_page(page);
		putback_lru_page(page);
		reset_reclaim_mode(sc);
		continue;

activate_locked:
		/* Not a candidate for swapping, so reclaim swap space. */
		if (PageSwapCache(page) && vm_swap_full())
			try_to_free_swap(page);
		VM_BUG_ON(PageActive(page));
		SetPageActive(page);
		pgactivate++;
keep_locked:
		unlock_page(page);
keep:
		reset_reclaim_mode(sc);
keep_lumpy:
		list_add(&page->lru, &ret_pages);
		VM_BUG_ON(PageLRU(page) || PageUnevictable(page));
	}

	/*
	 * Tag a zone as congested if all the dirty pages encountered were
	 * backed by a congested BDI. In this case, reclaimers should just
	 * back off and wait for congestion to clear because further reclaim
	 * will encounter the same problem
	 */
	if (nr_dirty && nr_dirty == nr_congested && scanning_global_lru(sc))
		zone_set_flag(zone, ZONE_CONGESTED);

	free_page_list(&free_pages);

	list_splice(&ret_pages, page_list);
	count_vm_events(PGACTIVATE, pgactivate);
	return nr_reclaimed;
}

/*
 * Attempt to remove the specified page from its LRU.  Only take this page
 * if it is of the appropriate PageActive status.  Pages which are being
 * freed elsewhere are also ignored.
 *
 * page:	page to consider
 * mode:	one of the LRU isolation modes defined above
 *
 * returns 0 on success, -ve errno on failure.
 */
int __isolate_lru_page(struct page *page, isolate_mode_t mode, int file)
{
	bool all_lru_mode;
	int ret = -EINVAL;

	/* Only take pages on the LRU. */
	if (!PageLRU(page))
		return ret;

	all_lru_mode = (mode & (ISOLATE_ACTIVE|ISOLATE_INACTIVE)) ==
		(ISOLATE_ACTIVE|ISOLATE_INACTIVE);

	/*
	 * When checking the active state, we need to be sure we are
	 * dealing with comparible boolean values.  Take the logical not
	 * of each.
	 */
	if (!all_lru_mode && !PageActive(page) != !(mode & ISOLATE_ACTIVE))
		return ret;

	if (!all_lru_mode && !!page_is_file_cache(page) != file)
		return ret;

	/*
	 * When this function is being called for lumpy reclaim, we
	 * initially look into all LRU pages, active, inactive and
	 * unevictable; only give shrink_page_list evictable pages.
	 */
	if (PageUnevictable(page))
		return ret;

	ret = -EBUSY;

	/*
	 * To minimise LRU disruption, the caller can indicate that it only
	 * wants to isolate pages it will be able to operate on without
	 * blocking - clean pages for the most part.
	 *
	 * ISOLATE_CLEAN means that only clean pages should be isolated. This
	 * is used by reclaim when it is cannot write to backing storage
	 *
	 * ISOLATE_ASYNC_MIGRATE is used to indicate that it only wants to pages
	 * that it is possible to migrate without blocking
	 */
	if (mode & (ISOLATE_CLEAN|ISOLATE_ASYNC_MIGRATE)) {
		/* All the caller can do on PageWriteback is block */
		if (PageWriteback(page))
			return ret;

		if (PageDirty(page)) {
			struct address_space *mapping;

			/* ISOLATE_CLEAN means only clean pages */
			if (mode & ISOLATE_CLEAN)
				return ret;

			/*
			 * Only pages without mappings or that have a
			 * ->migratepage callback are possible to migrate
			 * without blocking
			 */
			mapping = page_mapping(page);
			if (mapping && !mapping->a_ops->migratepage)
				return ret;
		}
	}

	if ((mode & ISOLATE_UNMAPPED) && page_mapped(page))
		return ret;

	if (likely(get_page_unless_zero(page))) {
		/*
		 * Be careful not to clear PageLRU until after we're
		 * sure the page is not being freed elsewhere -- the
		 * page release code relies on it.
		 */
		ClearPageLRU(page);
		ret = 0;
	}

	return ret;
}

/*
 * zone->lru_lock is heavily contended.  Some of the functions that
 * shrink the lists perform better by taking out a batch of pages
 * and working on them outside the LRU lock.
 *
 * For pagecache intensive workloads, this function is the hottest
 * spot in the kernel (apart from copy_*_user functions).
 *
 * Appropriate locks must be held before calling this function.
 *
 * @nr_to_scan:	The number of pages to look through on the list.
 * @src:	The LRU list to pull pages off.
 * @dst:	The temp list to put pages on to.
 * @scanned:	The number of pages that were scanned.
 * @order:	The caller's attempted allocation order
 * @mode:	One of the LRU isolation modes
 * @file:	True [1] if isolating file [!anon] pages
 *
 * returns how many pages were moved onto *@dst.
 */
static unsigned long isolate_lru_pages(unsigned long nr_to_scan,
		struct list_head *src, struct list_head *dst,
		unsigned long *scanned, int order, isolate_mode_t mode,
		int file)
{
	unsigned long nr_taken = 0;
	unsigned long nr_lumpy_taken = 0;
	unsigned long nr_lumpy_dirty = 0;
	unsigned long nr_lumpy_failed = 0;
	unsigned long scan;

	for (scan = 0; scan < nr_to_scan && !list_empty(src); scan++) {
		struct page *page;
		unsigned long pfn;
		unsigned long end_pfn;
		unsigned long page_pfn;
		int zone_id;

		page = lru_to_page(src);
		prefetchw_prev_lru_page(page, src, flags);

		VM_BUG_ON(!PageLRU(page));

		switch (__isolate_lru_page(page, mode, file)) {
		case 0:
			list_move(&page->lru, dst);
			mem_cgroup_del_lru(page);
			nr_taken += hpage_nr_pages(page);
			break;

		case -EBUSY:
			/* else it is being freed elsewhere */
			list_move(&page->lru, src);
			mem_cgroup_rotate_lru_list(page, page_lru(page));
			continue;

		default:
			BUG();
		}

		if (!order)
			continue;

		/*
		 * Attempt to take all pages in the order aligned region
		 * surrounding the tag page.  Only take those pages of
		 * the same active state as that tag page.  We may safely
		 * round the target page pfn down to the requested order
		 * as the mem_map is guaranteed valid out to MAX_ORDER,
		 * where that page is in a different zone we will detect
		 * it from its zone id and abort this block scan.
		 */
		zone_id = page_zone_id(page);
		page_pfn = page_to_pfn(page);
		pfn = page_pfn & ~((1 << order) - 1);
		end_pfn = pfn + (1 << order);
		for (; pfn < end_pfn; pfn++) {
			struct page *cursor_page;

			/* The target page is in the block, ignore it. */
			if (unlikely(pfn == page_pfn))
				continue;

			/* Avoid holes within the zone. */
			if (unlikely(!pfn_valid_within(pfn)))
				break;

			cursor_page = pfn_to_page(pfn);

			/* Check that we have not crossed a zone boundary. */
			if (unlikely(page_zone_id(cursor_page) != zone_id))
				break;

			/*
			 * If we don't have enough swap space, reclaiming of
			 * anon page which don't already have a swap slot is
			 * pointless.
			 */
			if (nr_swap_pages <= 0 && PageSwapBacked(cursor_page) &&
			    !PageSwapCache(cursor_page))
				break;

			if (__isolate_lru_page(cursor_page, mode, file) == 0) {
				list_move(&cursor_page->lru, dst);
				mem_cgroup_del_lru(cursor_page);
				nr_taken += hpage_nr_pages(page);
				nr_lumpy_taken++;
				if (PageDirty(cursor_page))
					nr_lumpy_dirty++;
				scan++;
			} else {
				/*
				 * Check if the page is freed already.
				 *
				 * We can't use page_count() as that
				 * requires compound_head and we don't
				 * have a pin on the page here. If a
				 * page is tail, we may or may not
				 * have isolated the head, so assume
				 * it's not free, it'd be tricky to
				 * track the head status without a
				 * page pin.
				 */
				if (!PageTail(cursor_page) &&
				    !atomic_read(&cursor_page->_count))
					continue;
				break;
			}
		}

		/* If we break out of the loop above, lumpy reclaim failed */
		if (pfn < end_pfn)
			nr_lumpy_failed++;
	}

	*scanned = scan;

	trace_mm_vmscan_lru_isolate(order,
			nr_to_scan, scan,
			nr_taken,
			nr_lumpy_taken, nr_lumpy_dirty, nr_lumpy_failed,
			mode);
	return nr_taken;
}

static unsigned long isolate_pages_global(unsigned long nr,
					struct list_head *dst,
					unsigned long *scanned, int order,
					isolate_mode_t mode,
					struct zone *z,	int active, int file)
{
	int lru = LRU_BASE;
	if (active)
		lru += LRU_ACTIVE;
	if (file)
		lru += LRU_FILE;
	return isolate_lru_pages(nr, &z->lru[lru].list, dst, scanned, order,
								mode, file);
}

/*
 * clear_active_flags() is a helper for shrink_active_list(), clearing
 * any active bits from the pages in the list.
 */
static unsigned long clear_active_flags(struct list_head *page_list,
					unsigned int *count)
{
	int nr_active = 0;
	int lru;
	struct page *page;

	list_for_each_entry(page, page_list, lru) {
		int numpages = hpage_nr_pages(page);
		lru = page_lru_base_type(page);
		if (PageActive(page)) {
			lru += LRU_ACTIVE;
			ClearPageActive(page);
			nr_active += numpages;
		}
		if (count)
			count[lru] += numpages;
	}

	return nr_active;
}

/**
 * isolate_lru_page - tries to isolate a page from its LRU list
 * @page: page to isolate from its LRU list
 *
 * Isolates a @page from an LRU list, clears PageLRU and adjusts the
 * vmstat statistic corresponding to whatever LRU list the page was on.
 *
 * Returns 0 if the page was removed from an LRU list.
 * Returns -EBUSY if the page was not on an LRU list.
 *
 * The returned page will have PageLRU() cleared.  If it was found on
 * the active list, it will have PageActive set.  If it was found on
 * the unevictable list, it will have the PageUnevictable bit set. That flag
 * may need to be cleared by the caller before letting the page go.
 *
 * The vmstat statistic corresponding to the list on which the page was
 * found will be decremented.
 *
 * Restrictions:
 * (1) Must be called with an elevated refcount on the page. This is a
 *     fundamentnal difference from isolate_lru_pages (which is called
 *     without a stable reference).
 * (2) the lru_lock must not be held.
 * (3) interrupts must be enabled.
 */
int isolate_lru_page(struct page *page)
{
	int ret = -EBUSY;

	VM_BUG_ON(!page_count(page));

	if (PageLRU(page)) {
		struct zone *zone = page_zone(page);

		spin_lock_irq(&zone->lru_lock);
		if (PageLRU(page)) {
			int lru = page_lru(page);
			ret = 0;
			get_page(page);
			ClearPageLRU(page);

			del_page_from_lru_list(zone, page, lru);
		}
		spin_unlock_irq(&zone->lru_lock);
	}
	return ret;
}

/*
 * Are there way too many processes in the direct reclaim path already?
 */
static int too_many_isolated(struct zone *zone, int file,
		struct scan_control *sc)
{
	unsigned long inactive, isolated;

	if (current_is_kswapd())
		return 0;

	if (!scanning_global_lru(sc))
		return 0;

	if (file) {
		inactive = zone_page_state(zone, NR_INACTIVE_FILE);
		isolated = zone_page_state(zone, NR_ISOLATED_FILE);
	} else {
		inactive = zone_page_state(zone, NR_INACTIVE_ANON);
		isolated = zone_page_state(zone, NR_ISOLATED_ANON);
	}

	return isolated > inactive;
}

/*
 * TODO: Try merging with migrations version of putback_lru_pages
 */
static noinline_for_stack void
putback_lru_pages(struct zone *zone, struct scan_control *sc,
				unsigned long nr_anon, unsigned long nr_file,
				struct list_head *page_list)
{
	struct page *page;
	struct pagevec pvec;
	struct zone_reclaim_stat *reclaim_stat = get_reclaim_stat(zone, sc);

	pagevec_init(&pvec, 1);

	/*
	 * Put back any unfreeable pages.
	 */
	spin_lock(&zone->lru_lock);
	while (!list_empty(page_list)) {
		int lru;
		page = lru_to_page(page_list);
		VM_BUG_ON(PageLRU(page));
		list_del(&page->lru);
		if (unlikely(!page_evictable(page, NULL))) {
			spin_unlock_irq(&zone->lru_lock);
			putback_lru_page(page);
			spin_lock_irq(&zone->lru_lock);
			continue;
		}
		SetPageLRU(page);
		lru = page_lru(page);
		add_page_to_lru_list(zone, page, lru);
		if (is_active_lru(lru)) {
			int file = is_file_lru(lru);
			int numpages = hpage_nr_pages(page);
			reclaim_stat->recent_rotated[file] += numpages;
		}
		if (!pagevec_add(&pvec, page)) {
			spin_unlock_irq(&zone->lru_lock);
			__pagevec_release(&pvec);
			spin_lock_irq(&zone->lru_lock);
		}
	}
	__mod_zone_page_state(zone, NR_ISOLATED_ANON, -nr_anon);
	__mod_zone_page_state(zone, NR_ISOLATED_FILE, -nr_file);

	spin_unlock_irq(&zone->lru_lock);
	pagevec_release(&pvec);
}

static noinline_for_stack void update_isolated_counts(struct zone *zone,
					struct scan_control *sc,
					unsigned long *nr_anon,
					unsigned long *nr_file,
					struct list_head *isolated_list)
{
	unsigned long nr_active;
	unsigned int count[NR_LRU_LISTS] = { 0, };
	struct zone_reclaim_stat *reclaim_stat = get_reclaim_stat(zone, sc);

	nr_active = clear_active_flags(isolated_list, count);
	__count_vm_events(PGDEACTIVATE, nr_active);

	__mod_zone_page_state(zone, NR_ACTIVE_FILE,
			      -count[LRU_ACTIVE_FILE]);
	__mod_zone_page_state(zone, NR_INACTIVE_FILE,
			      -count[LRU_INACTIVE_FILE]);
	__mod_zone_page_state(zone, NR_ACTIVE_ANON,
			      -count[LRU_ACTIVE_ANON]);
	__mod_zone_page_state(zone, NR_INACTIVE_ANON,
			      -count[LRU_INACTIVE_ANON]);

	*nr_anon = count[LRU_ACTIVE_ANON] + count[LRU_INACTIVE_ANON];
	*nr_file = count[LRU_ACTIVE_FILE] + count[LRU_INACTIVE_FILE];
	__mod_zone_page_state(zone, NR_ISOLATED_ANON, *nr_anon);
	__mod_zone_page_state(zone, NR_ISOLATED_FILE, *nr_file);

	reclaim_stat->recent_scanned[0] += *nr_anon;
	reclaim_stat->recent_scanned[1] += *nr_file;
}

/*
 * Returns true if the caller should wait to clean dirty/writeback pages.
 *
 * If we are direct reclaiming for contiguous pages and we do not reclaim
 * everything in the list, try again and wait for writeback IO to complete.
 * This will stall high-order allocations noticeably. Only do that when really
 * need to free the pages under high memory pressure.
 */
static inline bool should_reclaim_stall(unsigned long nr_taken,
					unsigned long nr_freed,
					int priority,
					struct scan_control *sc)
{
	int lumpy_stall_priority;

	/* kswapd should not stall on sync IO */
	if (current_is_kswapd())
		return false;

	/* Only stall on lumpy reclaim */
	if (sc->reclaim_mode & RECLAIM_MODE_SINGLE)
		return false;

	/* If we have relaimed everything on the isolated list, no stall */
	if (nr_freed == nr_taken)
		return false;

	/*
	 * For high-order allocations, there are two stall thresholds.
	 * High-cost allocations stall immediately where as lower
	 * order allocations such as stacks require the scanning
	 * priority to be much higher before stalling.
	 */
	if (sc->order > PAGE_ALLOC_COSTLY_ORDER)
		lumpy_stall_priority = DEF_PRIORITY;
	else
		lumpy_stall_priority = DEF_PRIORITY / 3;

	return priority <= lumpy_stall_priority;
}

/*
 * shrink_inactive_list() is a helper for shrink_zone().  It returns the number
 * of reclaimed pages
 */
static noinline_for_stack unsigned long
shrink_inactive_list(unsigned long nr_to_scan, struct zone *zone,
			struct scan_control *sc, int priority, int file)
{
	LIST_HEAD(page_list);
	unsigned long nr_scanned;
	unsigned long nr_reclaimed = 0;
	unsigned long nr_taken;
	unsigned long nr_anon;
	unsigned long nr_file;
	isolate_mode_t reclaim_mode = ISOLATE_INACTIVE;

	while (unlikely(too_many_isolated(zone, file, sc))) {
		congestion_wait(BLK_RW_ASYNC, HZ/10);

		/* We are about to die and free our memory. Return now. */
		if (fatal_signal_pending(current))
			return SWAP_CLUSTER_MAX;
	}

	set_reclaim_mode(priority, sc, false);
	if (sc->reclaim_mode & RECLAIM_MODE_LUMPYRECLAIM)
		reclaim_mode |= ISOLATE_ACTIVE;

	lru_add_drain();

	if (!sc->may_unmap)
		reclaim_mode |= ISOLATE_UNMAPPED;
	if (!sc->may_writepage)
		reclaim_mode |= ISOLATE_CLEAN;

	spin_lock_irq(&zone->lru_lock);

	if (scanning_global_lru(sc)) {
		nr_taken = isolate_pages_global(nr_to_scan, &page_list,
			&nr_scanned, sc->order, reclaim_mode, zone, 0, file);
		zone->pages_scanned += nr_scanned;
		if (current_is_kswapd())
			__count_zone_vm_events(PGSCAN_KSWAPD, zone,
					       nr_scanned);
		else
			__count_zone_vm_events(PGSCAN_DIRECT, zone,
					       nr_scanned);
	} else {
		nr_taken = mem_cgroup_isolate_pages(nr_to_scan, &page_list,
			&nr_scanned, sc->order, reclaim_mode, zone,
			sc->mem_cgroup, 0, file);
		/*
		 * mem_cgroup_isolate_pages() keeps track of
		 * scanned pages on its own.
		 */
	}

	if (nr_taken == 0) {
		spin_unlock_irq(&zone->lru_lock);
		return 0;
	}

	update_isolated_counts(zone, sc, &nr_anon, &nr_file, &page_list);

	spin_unlock_irq(&zone->lru_lock);

	nr_reclaimed = shrink_page_list(&page_list, zone, sc);

	/* Check if we should syncronously wait for writeback */
	if (should_reclaim_stall(nr_taken, nr_reclaimed, priority, sc)) {
		set_reclaim_mode(priority, sc, true);
		nr_reclaimed += shrink_page_list(&page_list, zone, sc);
	}

	local_irq_disable();
	if (current_is_kswapd())
		__count_vm_events(KSWAPD_STEAL, nr_reclaimed);
	__count_zone_vm_events(PGSTEAL, zone, nr_reclaimed);

	putback_lru_pages(zone, sc, nr_anon, nr_file, &page_list);

	trace_mm_vmscan_lru_shrink_inactive(zone->zone_pgdat->node_id,
		zone_idx(zone),
		nr_scanned, nr_reclaimed,
		priority,
		trace_shrink_flags(file, sc->reclaim_mode));
	return nr_reclaimed;
}

/*
 * This moves pages from the active list to the inactive list.
 *
 * We move them the other way if the page is referenced by one or more
 * processes, from rmap.
 *
 * If the pages are mostly unmapped, the processing is fast and it is
 * appropriate to hold zone->lru_lock across the whole operation.  But if
 * the pages are mapped, the processing is slow (page_referenced()) so we
 * should drop zone->lru_lock around each page.  It's impossible to balance
 * this, so instead we remove the pages from the LRU while processing them.
 * It is safe to rely on PG_active against the non-LRU pages in here because
 * nobody will play with that bit on a non-LRU page.
 *
 * The downside is that we have to touch page->_count against each page.
 * But we had to alter page->flags anyway.
 */

static void move_active_pages_to_lru(struct zone *zone,
				     struct list_head *list,
				     enum lru_list lru)
{
	unsigned long pgmoved = 0;
	struct pagevec pvec;
	struct page *page;

	pagevec_init(&pvec, 1);

	while (!list_empty(list)) {
		page = lru_to_page(list);

		VM_BUG_ON(PageLRU(page));
		SetPageLRU(page);

		list_move(&page->lru, &zone->lru[lru].list);
		mem_cgroup_add_lru_list(page, lru);
		pgmoved += hpage_nr_pages(page);

		if (!pagevec_add(&pvec, page) || list_empty(list)) {
			spin_unlock_irq(&zone->lru_lock);
			if (buffer_heads_over_limit)
				pagevec_strip(&pvec);
			__pagevec_release(&pvec);
			spin_lock_irq(&zone->lru_lock);
		}
	}
	__mod_zone_page_state(zone, NR_LRU_BASE + lru, pgmoved);
	if (!is_active_lru(lru))
		__count_vm_events(PGDEACTIVATE, pgmoved);
}

static void shrink_active_list(unsigned long nr_pages, struct zone *zone,
			struct scan_control *sc, int priority, int file)
{
	unsigned long nr_taken;
	unsigned long pgscanned;
	unsigned long vm_flags;
	LIST_HEAD(l_hold);	/* The pages which were snipped off */
	LIST_HEAD(l_active);
	LIST_HEAD(l_inactive);
	struct page *page;
	struct zone_reclaim_stat *reclaim_stat = get_reclaim_stat(zone, sc);
	unsigned long nr_rotated = 0;
	isolate_mode_t reclaim_mode = ISOLATE_ACTIVE;

	lru_add_drain();

	if (!sc->may_unmap)
		reclaim_mode |= ISOLATE_UNMAPPED;
	if (!sc->may_writepage)
		reclaim_mode |= ISOLATE_CLEAN;

	spin_lock_irq(&zone->lru_lock);
	if (scanning_global_lru(sc)) {
		nr_taken = isolate_pages_global(nr_pages, &l_hold,
						&pgscanned, sc->order,
						reclaim_mode, zone,
						1, file);
		zone->pages_scanned += pgscanned;
	} else {
		nr_taken = mem_cgroup_isolate_pages(nr_pages, &l_hold,
						&pgscanned, sc->order,
						reclaim_mode, zone,
						sc->mem_cgroup, 1, file);
		/*
		 * mem_cgroup_isolate_pages() keeps track of
		 * scanned pages on its own.
		 */
	}

	reclaim_stat->recent_scanned[file] += nr_taken;

	__count_zone_vm_events(PGREFILL, zone, pgscanned);
	if (file)
		__mod_zone_page_state(zone, NR_ACTIVE_FILE, -nr_taken);
	else
		__mod_zone_page_state(zone, NR_ACTIVE_ANON, -nr_taken);
	__mod_zone_page_state(zone, NR_ISOLATED_ANON + file, nr_taken);
	spin_unlock_irq(&zone->lru_lock);

	while (!list_empty(&l_hold)) {
		cond_resched();
		page = lru_to_page(&l_hold);
		list_del(&page->lru);

		if (unlikely(!page_evictable(page, NULL))) {
			putback_lru_page(page);
			continue;
		}

		if (page_referenced(page, 0, sc->mem_cgroup, &vm_flags)) {
			nr_rotated += hpage_nr_pages(page);
			/*
			 * Identify referenced, file-backed active pages and
			 * give them one more trip around the active list. So
			 * that executable code get better chances to stay in
			 * memory under moderate memory pressure.  Anon pages
			 * are not likely to be evicted by use-once streaming
			 * IO, plus JVM can create lots of anon VM_EXEC pages,
			 * so we ignore them here.
			 */
			if ((vm_flags & VM_EXEC) && page_is_file_cache(page)) {
				list_add(&page->lru, &l_active);
				continue;
			}
		}

		ClearPageActive(page);	/* we are de-activating */
		list_add(&page->lru, &l_inactive);
	}

	/*
	 * Move pages back to the lru list.
	 */
	spin_lock_irq(&zone->lru_lock);
	/*
	 * Count referenced pages from currently used mappings as rotated,
	 * even though only some of them are actually re-activated.  This
	 * helps balance scan pressure between file and anonymous pages in
	 * get_scan_ratio.
	 */
	reclaim_stat->recent_rotated[file] += nr_rotated;

	move_active_pages_to_lru(zone, &l_active,
						LRU_ACTIVE + file * LRU_FILE);
	move_active_pages_to_lru(zone, &l_inactive,
						LRU_BASE   + file * LRU_FILE);
	__mod_zone_page_state(zone, NR_ISOLATED_ANON + file, -nr_taken);
	spin_unlock_irq(&zone->lru_lock);
}

#ifdef CONFIG_SWAP
static int inactive_anon_is_low_global(struct zone *zone)
{
	unsigned long active, inactive;

	active = zone_page_state(zone, NR_ACTIVE_ANON);
	inactive = zone_page_state(zone, NR_INACTIVE_ANON);

	if (inactive * zone->inactive_ratio < active)
		return 1;

	return 0;
}

/**
 * inactive_anon_is_low - check if anonymous pages need to be deactivated
 * @zone: zone to check
 * @sc:   scan control of this context
 *
 * Returns true if the zone does not have enough inactive anon pages,
 * meaning some active anon pages need to be deactivated.
 */
static int inactive_anon_is_low(struct zone *zone, struct scan_control *sc)
{
	int low;

	/*
	 * If we don't have swap space, anonymous page deactivation
	 * is pointless.
	 */
	if (!total_swap_pages)
		return 0;

	if (scanning_global_lru(sc))
		low = inactive_anon_is_low_global(zone);
	else
		low = mem_cgroup_inactive_anon_is_low(sc->mem_cgroup);
	return low;
}
#else
static inline int inactive_anon_is_low(struct zone *zone,
					struct scan_control *sc)
{
	return 0;
}
#endif

static int inactive_file_is_low_global(struct zone *zone)
{
	unsigned long active, inactive;

	active = zone_page_state(zone, NR_ACTIVE_FILE);
	inactive = zone_page_state(zone, NR_INACTIVE_FILE);

	return (active > inactive);
}

/**
 * inactive_file_is_low - check if file pages need to be deactivated
 * @zone: zone to check
 * @sc:   scan control of this context
 *
 * When the system is doing streaming IO, memory pressure here
 * ensures that active file pages get deactivated, until more
 * than half of the file pages are on the inactive list.
 *
 * Once we get to that situation, protect the system's working
 * set from being evicted by disabling active file page aging.
 *
 * This uses a different ratio than the anonymous pages, because
 * the page cache uses a use-once replacement algorithm.
 */
static int inactive_file_is_low(struct zone *zone, struct scan_control *sc)
{
	int low;

	if (scanning_global_lru(sc))
		low = inactive_file_is_low_global(zone);
	else
		low = mem_cgroup_inactive_file_is_low(sc->mem_cgroup);
	return low;
}

static int inactive_list_is_low(struct zone *zone, struct scan_control *sc,
				int file)
{
	if (file)
		return inactive_file_is_low(zone, sc);
	else
		return inactive_anon_is_low(zone, sc);
}

static unsigned long shrink_list(enum lru_list lru, unsigned long nr_to_scan,
	struct zone *zone, struct scan_control *sc, int priority)
{
	int file = is_file_lru(lru);

	if (is_active_lru(lru)) {
		if (inactive_list_is_low(zone, sc, file))
		    shrink_active_list(nr_to_scan, zone, sc, priority, file);
		return 0;
	}

	return shrink_inactive_list(nr_to_scan, zone, sc, priority, file);
}

/*
 * Determine how aggressively the anon and file LRU lists should be
 * scanned.  The relative value of each set of LRU lists is determined
 * by looking at the fraction of the pages scanned we did rotate back
 * onto the active list instead of evict.
 *
 * nr[0] = anon pages to scan; nr[1] = file pages to scan
 */
static void get_scan_count(struct zone *zone, struct scan_control *sc,
					unsigned long *nr, int priority)
{
	unsigned long anon, file, free;
	unsigned long anon_prio, file_prio;
	unsigned long ap, fp;
	struct zone_reclaim_stat *reclaim_stat = get_reclaim_stat(zone, sc);
	u64 fraction[2], denominator;
	enum lru_list l;
	int noswap = 0;
	bool force_scan = false;
	unsigned long nr_force_scan[2];

	/* kswapd does zone balancing and needs to scan this zone */
	if (scanning_global_lru(sc) && current_is_kswapd() &&
	    zone->all_unreclaimable)
		force_scan = true;
	/* memcg may have small limit and need to avoid priority drop */
	if (!scanning_global_lru(sc))
		force_scan = true;

	/* If we have no swap space, do not bother scanning anon pages. */
	if (!sc->may_swap || (nr_swap_pages <= 0)) {
		noswap = 1;
		fraction[0] = 0;
		fraction[1] = 1;
		denominator = 1;
		nr_force_scan[0] = 0;
		nr_force_scan[1] = SWAP_CLUSTER_MAX;
		goto out;
	}

	anon  = zone_nr_lru_pages(zone, sc, LRU_ACTIVE_ANON) +
		zone_nr_lru_pages(zone, sc, LRU_INACTIVE_ANON);
	file  = zone_nr_lru_pages(zone, sc, LRU_ACTIVE_FILE) +
		zone_nr_lru_pages(zone, sc, LRU_INACTIVE_FILE);

	if (scanning_global_lru(sc)) {
		free  = zone_page_state(zone, NR_FREE_PAGES);
		/* If we have very few page cache pages,
		   force-scan anon pages. */
		if (unlikely(file + free <= high_wmark_pages(zone))) {
			fraction[0] = 1;
			fraction[1] = 0;
			denominator = 1;
			nr_force_scan[0] = SWAP_CLUSTER_MAX;
			nr_force_scan[1] = 0;
			goto out;
		}
	}

	/*
	 * With swappiness at 100, anonymous and file have the same priority.
	 * This scanning priority is essentially the inverse of IO cost.
	 */
	anon_prio = sc->swappiness;
	file_prio = 200 - sc->swappiness;

	/*
	 * OK, so we have swap space and a fair amount of page cache
	 * pages.  We use the recently rotated / recently scanned
	 * ratios to determine how valuable each cache is.
	 *
	 * Because workloads change over time (and to avoid overflow)
	 * we keep these statistics as a floating average, which ends
	 * up weighing recent references more than old ones.
	 *
	 * anon in [0], file in [1]
	 */
	spin_lock_irq(&zone->lru_lock);
	if (unlikely(reclaim_stat->recent_scanned[0] > anon / 4)) {
		reclaim_stat->recent_scanned[0] /= 2;
		reclaim_stat->recent_rotated[0] /= 2;
	}

	if (unlikely(reclaim_stat->recent_scanned[1] > file / 4)) {
		reclaim_stat->recent_scanned[1] /= 2;
		reclaim_stat->recent_rotated[1] /= 2;
	}

	/*
	 * The amount of pressure on anon vs file pages is inversely
	 * proportional to the fraction of recently scanned pages on
	 * each list that were recently referenced and in active use.
	 */
	ap = (anon_prio + 1) * (reclaim_stat->recent_scanned[0] + 1);
	ap /= reclaim_stat->recent_rotated[0] + 1;

	fp = (file_prio + 1) * (reclaim_stat->recent_scanned[1] + 1);
	fp /= reclaim_stat->recent_rotated[1] + 1;
	spin_unlock_irq(&zone->lru_lock);

	fraction[0] = ap;
	fraction[1] = fp;
	denominator = ap + fp + 1;
	if (force_scan) {
		unsigned long scan = SWAP_CLUSTER_MAX;
		nr_force_scan[0] = div64_u64(scan * ap, denominator);
		nr_force_scan[1] = div64_u64(scan * fp, denominator);
	}
out:
	for_each_evictable_lru(l) {
		int file = is_file_lru(l);
		unsigned long scan;

		scan = zone_nr_lru_pages(zone, sc, l);
		if (priority || noswap) {
			scan >>= priority;
			scan = div64_u64(scan * fraction[file], denominator);
		}

		/*
		 * If zone is small or memcg is small, nr[l] can be 0.
		 * This results no-scan on this priority and priority drop down.
		 * For global direct reclaim, it can visit next zone and tend
		 * not to have problems. For global kswapd, it's for zone
		 * balancing and it need to scan a small amounts. When using
		 * memcg, priority drop can cause big latency. So, it's better
		 * to scan small amount. See may_noscan above.
		 */
		if (!scan && force_scan)
			scan = nr_force_scan[file];
		nr[l] = scan;
	}
}

/*
 * Reclaim/compaction depends on a number of pages being freed. To avoid
 * disruption to the system, a small number of order-0 pages continue to be
 * rotated and reclaimed in the normal fashion. However, by the time we get
 * back to the allocator and call try_to_compact_zone(), we ensure that
 * there are enough free pages for it to be likely successful
 */
static inline bool should_continue_reclaim(struct zone *zone,
					unsigned long nr_reclaimed,
					unsigned long nr_scanned,
					struct scan_control *sc)
{
	unsigned long pages_for_compaction;
	unsigned long inactive_lru_pages;

	/* If not in reclaim/compaction mode, stop */
	if (!(sc->reclaim_mode & RECLAIM_MODE_COMPACTION))
		return false;

	/* Consider stopping depending on scan and reclaim activity */
	if (sc->gfp_mask & __GFP_REPEAT) {
		/*
		 * For __GFP_REPEAT allocations, stop reclaiming if the
		 * full LRU list has been scanned and we are still failing
		 * to reclaim pages. This full LRU scan is potentially
		 * expensive but a __GFP_REPEAT caller really wants to succeed
		 */
		if (!nr_reclaimed && !nr_scanned)
			return false;
	} else {
		/*
		 * For non-__GFP_REPEAT allocations which can presumably
		 * fail without consequence, stop if we failed to reclaim
		 * any pages from the last SWAP_CLUSTER_MAX number of
		 * pages that were scanned. This will return to the
		 * caller faster at the risk reclaim/compaction and
		 * the resulting allocation attempt fails
		 */
		if (!nr_reclaimed)
			return false;
	}

	/*
	 * If we have not reclaimed enough pages for compaction and the
	 * inactive lists are large enough, continue reclaiming
	 */
	pages_for_compaction = (2UL << sc->order);
	inactive_lru_pages = zone_nr_lru_pages(zone, sc, LRU_INACTIVE_FILE);
	if (nr_swap_pages > 0)
		inactive_lru_pages += zone_nr_lru_pages(zone, sc, LRU_INACTIVE_ANON);
	if (sc->nr_reclaimed < pages_for_compaction &&
			inactive_lru_pages > pages_for_compaction)
		return true;

	/* If compaction would go ahead or the allocation would succeed, stop */
	switch (compaction_suitable(zone, sc->order)) {
	case COMPACT_PARTIAL:
	case COMPACT_CONTINUE:
		return false;
	default:
		return true;
	}
}

/*
 * This is a basic per-zone page freer.  Used by both kswapd and direct reclaim.
 */
static void shrink_zone(int priority, struct zone *zone,
				struct scan_control *sc)
{
	unsigned long nr[NR_LRU_LISTS];
	unsigned long nr_to_scan;
	enum lru_list l;
	unsigned long nr_reclaimed, nr_scanned;
	unsigned long nr_to_reclaim = sc->nr_to_reclaim;

restart:
	nr_reclaimed = 0;
	nr_scanned = sc->nr_scanned;
	get_scan_count(zone, sc, nr, priority);

	while (nr[LRU_INACTIVE_ANON] || nr[LRU_ACTIVE_FILE] ||
					nr[LRU_INACTIVE_FILE]) {
		for_each_evictable_lru(l) {
			if (nr[l]) {
				nr_to_scan = min_t(unsigned long,
						   nr[l], SWAP_CLUSTER_MAX);
				nr[l] -= nr_to_scan;

				nr_reclaimed += shrink_list(l, nr_to_scan,
							    zone, sc, priority);
			}
		}
		/*
		 * On large memory systems, scan >> priority can become
		 * really large. This is fine for the starting priority;
		 * we want to put equal scanning pressure on each zone.
		 * However, if the VM has a harder time of freeing pages,
		 * with multiple processes reclaiming pages, the total
		 * freeing target can get unreasonably large.
		 */
		if (nr_reclaimed >= nr_to_reclaim && priority < DEF_PRIORITY)
			break;
	}
	sc->nr_reclaimed += nr_reclaimed;

	/*
	 * Even if we did not try to evict anon pages at all, we want to
	 * rebalance the anon lru active/inactive ratio.
	 */
	if (inactive_anon_is_low(zone, sc))
		shrink_active_list(SWAP_CLUSTER_MAX, zone, sc, priority, 0);

	/* reclaim/compaction might need reclaim to continue */
	if (should_continue_reclaim(zone, nr_reclaimed,
					sc->nr_scanned - nr_scanned, sc))
		goto restart;

	throttle_vm_writeout(sc->gfp_mask);
}

/* Returns true if compaction should go ahead for a high-order request */
static inline bool compaction_ready(struct zone *zone, struct scan_control *sc)
{
	unsigned long balance_gap, watermark;
	bool watermark_ok;

	/* Do not consider compaction for orders reclaim is meant to satisfy */
	if (sc->order <= PAGE_ALLOC_COSTLY_ORDER)
		return false;

	/*
	 * Compaction takes time to run and there are potentially other
	 * callers using the pages just freed. Continue reclaiming until
	 * there is a buffer of free pages available to give compaction
	 * a reasonable chance of completing and allocating the page
	 */
	balance_gap = min(low_wmark_pages(zone),
		(zone->present_pages + KSWAPD_ZONE_BALANCE_GAP_RATIO-1) /
			KSWAPD_ZONE_BALANCE_GAP_RATIO);
	watermark = high_wmark_pages(zone) + balance_gap + (2UL << sc->order);
	watermark_ok = zone_watermark_ok_safe(zone, 0, watermark, 0, 0);

	/*
	 * If compaction is deferred, reclaim up to a point where
	 * compaction will have a chance of success when re-enabled
	 */
	if (compaction_deferred(zone))
		return watermark_ok;

	/* If compaction is not ready to start, keep reclaiming */
	if (!compaction_suitable(zone, sc->order))
		return false;

	return watermark_ok;
}

/*
 * This is the direct reclaim path, for page-allocating processes.  We only
 * try to reclaim pages from zones which will satisfy the caller's allocation
 * request.
 *
 * We reclaim from a zone even if that zone is over high_wmark_pages(zone).
 * Because:
 * a) The caller may be trying to free *extra* pages to satisfy a higher-order
 *    allocation or
 * b) The target zone may be at high_wmark_pages(zone) but the lower zones
 *    must go *over* high_wmark_pages(zone) to satisfy the `incremental min'
 *    zone defense algorithm.
 *
 * If a zone is deemed to be full of pinned pages then just give it a light
 * scan then give up on it.
 *
 * This function returns true if a zone is being reclaimed for a costly
 * high-order allocation and compaction is ready to begin. This indicates to
 * the caller that it should consider retrying the allocation instead of
 * further reclaim.
 */
static bool shrink_zones(int priority, struct zonelist *zonelist,
					struct scan_control *sc)
{
	struct zoneref *z;
	struct zone *zone;
	unsigned long nr_soft_reclaimed;
	unsigned long nr_soft_scanned;
	bool aborted_reclaim = false;

	for_each_zone_zonelist_nodemask(zone, z, zonelist,
					gfp_zone(sc->gfp_mask), sc->nodemask) {
		if (!populated_zone(zone))
			continue;
		/*
		 * Take care memory controller reclaiming has small influence
		 * to global LRU.
		 */
		if (scanning_global_lru(sc)) {
			if (!cpuset_zone_allowed_hardwall(zone, GFP_KERNEL))
				continue;
			if (zone->all_unreclaimable && priority != DEF_PRIORITY)
				continue;	/* Let kswapd poll it */
			if (COMPACTION_BUILD) {
				/*
				 * If we already have plenty of memory free for
				 * compaction in this zone, don't free any more.
				 * Even though compaction is invoked for any
				 * non-zero order, only frequent costly order
				 * reclamation is disruptive enough to become a
				 * noticable problem, like transparent huge page
				 * allocations.
				 */
				if (compaction_ready(zone, sc)) {
					aborted_reclaim = true;
					continue;
				}
			}
			/*
			 * This steals pages from memory cgroups over softlimit
			 * and returns the number of reclaimed pages and
			 * scanned pages. This works for global memory pressure
			 * and balancing, not for a memcg's limit.
			 */
			nr_soft_scanned = 0;
			nr_soft_reclaimed = mem_cgroup_soft_limit_reclaim(zone,
						sc->order, sc->gfp_mask,
						&nr_soft_scanned);
			sc->nr_reclaimed += nr_soft_reclaimed;
			sc->nr_scanned += nr_soft_scanned;
			/* need some check for avoid more shrink_zone() */
		}

		shrink_zone(priority, zone, sc);
	}

	return aborted_reclaim;
}

static bool zone_reclaimable(struct zone *zone)
{
	return zone->pages_scanned < zone_reclaimable_pages(zone) * 6;
}

/* All zones in zonelist are unreclaimable? */
static bool all_unreclaimable(struct zonelist *zonelist,
		struct scan_control *sc)
{
	struct zoneref *z;
	struct zone *zone;

	for_each_zone_zonelist_nodemask(zone, z, zonelist,
			gfp_zone(sc->gfp_mask), sc->nodemask) {
		if (!populated_zone(zone))
			continue;
		if (!cpuset_zone_allowed_hardwall(zone, GFP_KERNEL))
			continue;
		if (!zone->all_unreclaimable)
			return false;
	}

	return true;
}

/*
 * This is the main entry point to direct page reclaim.
 *
 * If a full scan of the inactive list fails to free enough memory then we
 * are "out of memory" and something needs to be killed.
 *
 * If the caller is !__GFP_FS then the probability of a failure is reasonably
 * high - the zone may be full of dirty or under-writeback pages, which this
 * caller can't do much about.  We kick the writeback threads and take explicit
 * naps in the hope that some of these pages can be written.  But if the
 * allocating task holds filesystem locks which prevent writeout this might not
 * work, and the allocation attempt will fail.
 *
 * returns:	0, if no pages reclaimed
 * 		else, the number of pages reclaimed
 */
static unsigned long do_try_to_free_pages(struct zonelist *zonelist,
					struct scan_control *sc,
					struct shrink_control *shrink)
{
	int priority;
	unsigned long total_scanned = 0;
	struct reclaim_state *reclaim_state = current->reclaim_state;
	struct zoneref *z;
	struct zone *zone;
	unsigned long writeback_threshold;
	bool aborted_reclaim;

	delayacct_freepages_start();

	if (scanning_global_lru(sc))
		count_vm_event(ALLOCSTALL);

	for (priority = DEF_PRIORITY; priority >= 0; priority--) {
		sc->nr_scanned = 0;
		if (!priority)
			disable_swap_token(sc->mem_cgroup);
		aborted_reclaim = shrink_zones(priority, zonelist, sc);

		/*
		 * Don't shrink slabs when reclaiming memory from
		 * over limit cgroups
		 */
		if (scanning_global_lru(sc)) {
			unsigned long lru_pages = 0;
			for_each_zone_zonelist(zone, z, zonelist,
					gfp_zone(sc->gfp_mask)) {
				if (!cpuset_zone_allowed_hardwall(zone, GFP_KERNEL))
					continue;

				lru_pages += zone_reclaimable_pages(zone);
			}

			shrink_slab(shrink, sc->nr_scanned, lru_pages);
			if (reclaim_state) {
				sc->nr_reclaimed += reclaim_state->reclaimed_slab;
				reclaim_state->reclaimed_slab = 0;
			}
		}
		total_scanned += sc->nr_scanned;
		if (sc->nr_reclaimed >= sc->nr_to_reclaim)
			goto out;

		/*
		 * Try to write back as many pages as we just scanned.  This
		 * tends to cause slow streaming writers to write data to the
		 * disk smoothly, at the dirtying rate, which is nice.   But
		 * that's undesirable in laptop mode, where we *want* lumpy
		 * writeout.  So in laptop mode, write out the whole world.
		 */
		writeback_threshold = sc->nr_to_reclaim + sc->nr_to_reclaim / 2;
		if (total_scanned > writeback_threshold) {
			wakeup_flusher_threads(laptop_mode ? 0 : total_scanned);
			sc->may_writepage = 1;
		}

		/* Take a nap, wait for some writeback to complete */
		if (!sc->hibernation_mode && sc->nr_scanned &&
		    priority < DEF_PRIORITY - 2) {
			struct zone *preferred_zone;

			first_zones_zonelist(zonelist, gfp_zone(sc->gfp_mask),
						&cpuset_current_mems_allowed,
						&preferred_zone);
			wait_iff_congested(preferred_zone, BLK_RW_ASYNC, HZ/10);
		}
	}

out:
	delayacct_freepages_end();

	if (sc->nr_reclaimed)
		return sc->nr_reclaimed;

	/*
	 * As hibernation is going on, kswapd is freezed so that it can't mark
	 * the zone into all_unreclaimable. Thus bypassing all_unreclaimable
	 * check.
	 */
	if (oom_killer_disabled)
		return 0;

	/* Aborted reclaim to try compaction? don't OOM, then */
	if (aborted_reclaim)
		return 1;

	/* top priority shrink_zones still had more to do? don't OOM, then */
	if (scanning_global_lru(sc) && !all_unreclaimable(zonelist, sc))
		return 1;

	return 0;
}

unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
				gfp_t gfp_mask, nodemask_t *nodemask)
{
	unsigned long nr_reclaimed;
	struct scan_control sc = {
		.gfp_mask = gfp_mask,
		.may_writepage = !laptop_mode,
		.nr_to_reclaim = SWAP_CLUSTER_MAX,
		.may_unmap = 1,
		.may_swap = 1,
		.swappiness = vm_swappiness,
		.order = order,
		.mem_cgroup = NULL,
		.nodemask = nodemask,
	};
	struct shrink_control shrink = {
		.gfp_mask = sc.gfp_mask,
	};

	trace_mm_vmscan_direct_reclaim_begin(order,
				sc.may_writepage,
				gfp_mask);

	nr_reclaimed = do_try_to_free_pages(zonelist, &sc, &shrink);

	trace_mm_vmscan_direct_reclaim_end(nr_reclaimed);

	return nr_reclaimed;
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR

unsigned long mem_cgroup_shrink_node_zone(struct mem_cgroup *mem,
						gfp_t gfp_mask, bool noswap,
						unsigned int swappiness,
						struct zone *zone,
						unsigned long *nr_scanned)
{
	struct scan_control sc = {
		.nr_scanned = 0,
		.nr_to_reclaim = SWAP_CLUSTER_MAX,
		.may_writepage = !laptop_mode,
		.may_unmap = 1,
		.may_swap = !noswap,
		.swappiness = swappiness,
		.order = 0,
		.mem_cgroup = mem,
	};

	sc.gfp_mask = (gfp_mask & GFP_RECLAIM_MASK) |
			(GFP_HIGHUSER_MOVABLE & ~GFP_RECLAIM_MASK);

	trace_mm_vmscan_memcg_softlimit_reclaim_begin(0,
						      sc.may_writepage,
						      sc.gfp_mask);

	/*
	 * NOTE: Although we can get the priority field, using it
	 * here is not a good idea, since it limits the pages we can scan.
	 * if we don't reclaim here, the shrink_zone from balance_pgdat
	 * will pick up pages from other mem cgroup's as well. We hack
	 * the priority and make it zero.
	 */
	shrink_zone(0, zone, &sc);

	trace_mm_vmscan_memcg_softlimit_reclaim_end(sc.nr_reclaimed);

	*nr_scanned = sc.nr_scanned;
	return sc.nr_reclaimed;
}

unsigned long try_to_free_mem_cgroup_pages(struct mem_cgroup *mem_cont,
					   gfp_t gfp_mask,
					   bool noswap,
					   unsigned int swappiness)
{
	struct zonelist *zonelist;
	unsigned long nr_reclaimed;
	int nid;
	struct scan_control sc = {
		.may_writepage = !laptop_mode,
		.may_unmap = 1,
		.may_swap = !noswap,
		.nr_to_reclaim = SWAP_CLUSTER_MAX,
		.swappiness = swappiness,
		.order = 0,
		.mem_cgroup = mem_cont,
		.nodemask = NULL, /* we don't care the placement */
		.gfp_mask = (gfp_mask & GFP_RECLAIM_MASK) |
				(GFP_HIGHUSER_MOVABLE & ~GFP_RECLAIM_MASK),
	};
	struct shrink_control shrink = {
		.gfp_mask = sc.gfp_mask,
	};

	/*
	 * Unlike direct reclaim via alloc_pages(), memcg's reclaim doesn't
	 * take care of from where we get pages. So the node where we start the
	 * scan does not need to be the current node.
	 */
	nid = mem_cgroup_select_victim_node(mem_cont);

	zonelist = NODE_DATA(nid)->node_zonelists;

	trace_mm_vmscan_memcg_reclaim_begin(0,
					    sc.may_writepage,
					    sc.gfp_mask);

	nr_reclaimed = do_try_to_free_pages(zonelist, &sc, &shrink);

	trace_mm_vmscan_memcg_reclaim_end(nr_reclaimed);

	return nr_reclaimed;
}
#endif

/*
 * pgdat_balanced is used when checking if a node is balanced for high-order
 * allocations. Only zones that meet watermarks and are in a zone allowed
 * by the callers classzone_idx are added to balanced_pages. The total of
 * balanced pages must be at least 25% of the zones allowed by classzone_idx
 * for the node to be considered balanced. Forcing all zones to be balanced
 * for high orders can cause excessive reclaim when there are imbalanced zones.
 * The choice of 25% is due to
 *   o a 16M DMA zone that is balanced will not balance a zone on any
 *     reasonable sized machine
 *   o On all other machines, the top zone must be at least a reasonable
 *     percentage of the middle zones. For example, on 32-bit x86, highmem
 *     would need to be at least 256M for it to be balance a whole node.
 *     Similarly, on x86-64 the Normal zone would need to be at least 1G
 *     to balance a node on its own. These seemed like reasonable ratios.
 */
static bool pgdat_balanced(pg_data_t *pgdat, unsigned long balanced_pages,
						int classzone_idx)
{
	unsigned long present_pages = 0;
	int i;

	for (i = 0; i <= classzone_idx; i++)
		present_pages += pgdat->node_zones[i].present_pages;

	/* A special case here: if zone has no page, we think it's balanced */
	return balanced_pages >= (present_pages >> 2);
}

/* is kswapd sleeping prematurely? */
static bool sleeping_prematurely(pg_data_t *pgdat, int order, long remaining,
					int classzone_idx)
{
	int i;
	unsigned long balanced = 0;
	bool all_zones_ok = true;

	/* If a direct reclaimer woke kswapd within HZ/10, it's premature */
	if (remaining)
		return true;

	/* Check the watermark levels */
	for (i = 0; i <= classzone_idx; i++) {
		struct zone *zone = pgdat->node_zones + i;

		if (!populated_zone(zone))
			continue;

		/*
		 * balance_pgdat() skips over all_unreclaimable after
		 * DEF_PRIORITY. Effectively, it considers them balanced so
		 * they must be considered balanced here as well if kswapd
		 * is to sleep
		 */
		if (zone->all_unreclaimable) {
			balanced += zone->present_pages;
			continue;
		}

		if (!zone_watermark_ok_safe(zone, order, high_wmark_pages(zone),
							i, 0))
			all_zones_ok = false;
		else
			balanced += zone->present_pages;
	}

	/*
	 * For high-order requests, the balanced zones must contain at least
	 * 25% of the nodes pages for kswapd to sleep. For order-0, all zones
	 * must be balanced
	 */
	if (order)
		return !pgdat_balanced(pgdat, balanced, classzone_idx);
	else
		return !all_zones_ok;
}

/*
 * For kswapd, balance_pgdat() will work across all this node's zones until
 * they are all at high_wmark_pages(zone).
 *
 * Returns the final order kswapd was reclaiming at
 *
 * There is special handling here for zones which are full of pinned pages.
 * This can happen if the pages are all mlocked, or if they are all used by
 * device drivers (say, ZONE_DMA).  Or if they are all in use by hugetlb.
 * What we do is to detect the case where all pages in the zone have been
 * scanned twice and there has been zero successful reclaim.  Mark the zone as
 * dead and from now on, only perform a short scan.  Basically we're polling
 * the zone for when the problem goes away.
 *
 * kswapd scans the zones in the highmem->normal->dma direction.  It skips
 * zones which have free_pages > high_wmark_pages(zone), but once a zone is
 * found to have free_pages <= high_wmark_pages(zone), we scan that zone and the
 * lower zones regardless of the number of free pages in the lower zones. This
 * interoperates with the page allocator fallback scheme to ensure that aging
 * of pages is balanced across the zones.
 */
static unsigned long balance_pgdat(pg_data_t *pgdat, int order,
							int *classzone_idx)
{
	int all_zones_ok;
	unsigned long balanced;
	int priority;
	int i;
	int end_zone = 0;	/* Inclusive.  0 = ZONE_DMA */
	unsigned long total_scanned;
	struct reclaim_state *reclaim_state = current->reclaim_state;
	unsigned long nr_soft_reclaimed;
	unsigned long nr_soft_scanned;
	struct scan_control sc = {
		.gfp_mask = GFP_KERNEL,
		.may_unmap = 1,
		.may_swap = 1,
		/*
		 * kswapd doesn't want to be bailed out while reclaim. because
		 * we want to put equal scanning pressure on each zone.
		 */
		.nr_to_reclaim = ULONG_MAX,
		.swappiness = vm_swappiness,
		.order = order,
		.mem_cgroup = NULL,
	};
	struct shrink_control shrink = {
		.gfp_mask = sc.gfp_mask,
	};
loop_again:
	total_scanned = 0;
	sc.nr_reclaimed = 0;
	sc.may_writepage = !laptop_mode;
	count_vm_event(PAGEOUTRUN);

	for (priority = DEF_PRIORITY; priority >= 0; priority--) {
		unsigned long lru_pages = 0;
		int has_under_min_watermark_zone = 0;

		/* The swap token gets in the way of swapout... */
		if (!priority)
			disable_swap_token(NULL);

		all_zones_ok = 1;
		balanced = 0;

		/*
		 * Scan in the highmem->dma direction for the highest
		 * zone which needs scanning
		 */
		for (i = pgdat->nr_zones - 1; i >= 0; i--) {
			struct zone *zone = pgdat->node_zones + i;

			if (!populated_zone(zone))
				continue;

			if (zone->all_unreclaimable && priority != DEF_PRIORITY)
				continue;

			/*
			 * Do some background aging of the anon list, to give
			 * pages a chance to be referenced before reclaiming.
			 */
			if (inactive_anon_is_low(zone, &sc))
				shrink_active_list(SWAP_CLUSTER_MAX, zone,
							&sc, priority, 0);

			if (!zone_watermark_ok_safe(zone, order,
					high_wmark_pages(zone), 0, 0)) {
				end_zone = i;
				break;
			} else {
				/* If balanced, clear the congested flag */
				zone_clear_flag(zone, ZONE_CONGESTED);
			}
		}
		if (i < 0)
			goto out;

		for (i = 0; i <= end_zone; i++) {
			struct zone *zone = pgdat->node_zones + i;

			lru_pages += zone_reclaimable_pages(zone);
		}

		/*
		 * Now scan the zone in the dma->highmem direction, stopping
		 * at the last zone which needs scanning.
		 *
		 * We do this because the page allocator works in the opposite
		 * direction.  This prevents the page allocator from allocating
		 * pages behind kswapd's direction of progress, which would
		 * cause too much scanning of the lower zones.
		 */
		for (i = 0; i <= end_zone; i++) {
			struct zone *zone = pgdat->node_zones + i;
			int nr_slab;
			unsigned long balance_gap;

			if (!populated_zone(zone))
				continue;

			if (zone->all_unreclaimable && priority != DEF_PRIORITY)
				continue;

			sc.nr_scanned = 0;

			nr_soft_scanned = 0;
			/*
			 * Call soft limit reclaim before calling shrink_zone.
			 */
			nr_soft_reclaimed = mem_cgroup_soft_limit_reclaim(zone,
							order, sc.gfp_mask,
							&nr_soft_scanned);
			sc.nr_reclaimed += nr_soft_reclaimed;
			total_scanned += nr_soft_scanned;

			/*
			 * We put equal pressure on every zone, unless
			 * one zone has way too many pages free
			 * already. The "too many pages" is defined
			 * as the high wmark plus a "gap" where the
			 * gap is either the low watermark or 1%
			 * of the zone, whichever is smaller.
			 */
			balance_gap = min(low_wmark_pages(zone),
				(zone->present_pages +
					KSWAPD_ZONE_BALANCE_GAP_RATIO-1) /
				KSWAPD_ZONE_BALANCE_GAP_RATIO);
			if (!zone_watermark_ok_safe(zone, order,
					high_wmark_pages(zone) + balance_gap,
					end_zone, 0)) {
				shrink_zone(priority, zone, &sc);

				reclaim_state->reclaimed_slab = 0;
				nr_slab = shrink_slab(&shrink, sc.nr_scanned, lru_pages);
				sc.nr_reclaimed += reclaim_state->reclaimed_slab;
				total_scanned += sc.nr_scanned;

				if (nr_slab == 0 && !zone_reclaimable(zone))
					zone->all_unreclaimable = 1;
			}

			/*
			 * If we've done a decent amount of scanning and
			 * the reclaim ratio is low, start doing writepage
			 * even in laptop mode
			 */
			if (total_scanned > SWAP_CLUSTER_MAX * 2 &&
			    total_scanned > sc.nr_reclaimed + sc.nr_reclaimed / 2)
				sc.may_writepage = 1;

			if (zone->all_unreclaimable) {
				if (end_zone && end_zone == i)
					end_zone--;
				continue;
			}

			if (!zone_watermark_ok_safe(zone, order,
					high_wmark_pages(zone), end_zone, 0)) {
				all_zones_ok = 0;
				/*
				 * We are still under min water mark.  This
				 * means that we have a GFP_ATOMIC allocation
				 * failure risk. Hurry up!
				 */
				if (!zone_watermark_ok_safe(zone, order,
					    min_wmark_pages(zone), end_zone, 0))
					has_under_min_watermark_zone = 1;
			} else {
				/*
				 * If a zone reaches its high watermark,
				 * consider it to be no longer congested. It's
				 * possible there are dirty pages backed by
				 * congested BDIs but as pressure is relieved,
				 * spectulatively avoid congestion waits
				 */
				zone_clear_flag(zone, ZONE_CONGESTED);
				if (i <= *classzone_idx)
					balanced += zone->present_pages;
			}

		}
		if (all_zones_ok || (order && pgdat_balanced(pgdat, balanced, *classzone_idx)))
			break;		/* kswapd: all done */
		/*
		 * OK, kswapd is getting into trouble.  Take a nap, then take
		 * another pass across the zones.
		 */
		if (total_scanned && (priority < DEF_PRIORITY - 2)) {
			if (has_under_min_watermark_zone)
				count_vm_event(KSWAPD_SKIP_CONGESTION_WAIT);
			else
				congestion_wait(BLK_RW_ASYNC, HZ/10);
		}

		/*
		 * We do this so kswapd doesn't build up large priorities for
		 * example when it is freeing in parallel with allocators. It
		 * matches the direct reclaim path behaviour in terms of impact
		 * on zone->*_priority.
		 */
		if (sc.nr_reclaimed >= SWAP_CLUSTER_MAX)
			break;
	}
out:

	/*
	 * order-0: All zones must meet high watermark for a balanced node
	 * high-order: Balanced zones must make up at least 25% of the node
	 *             for the node to be balanced
	 */
	if (!(all_zones_ok || (order && pgdat_balanced(pgdat, balanced, *classzone_idx)))) {
		cond_resched();

		try_to_freeze();

		/*
		 * Fragmentation may mean that the system cannot be
		 * rebalanced for high-order allocations in all zones.
		 * At this point, if nr_reclaimed < SWAP_CLUSTER_MAX,
		 * it means the zones have been fully scanned and are still
		 * not balanced. For high-order allocations, there is
		 * little point trying all over again as kswapd may
		 * infinite loop.
		 *
		 * Instead, recheck all watermarks at order-0 as they
		 * are the most important. If watermarks are ok, kswapd will go
		 * back to sleep. High-order users can still perform direct
		 * reclaim if they wish.
		 */
		if (sc.nr_reclaimed < SWAP_CLUSTER_MAX)
			order = sc.order = 0;

		goto loop_again;
	}

	/*
	 * If kswapd was reclaiming at a higher order, it has the option of
	 * sleeping without all zones being balanced. Before it does, it must
	 * ensure that the watermarks for order-0 on *all* zones are met and
	 * that the congestion flags are cleared. The congestion flag must
	 * be cleared as kswapd is the only mechanism that clears the flag
	 * and it is potentially going to sleep here.
	 */
	if (order) {
		for (i = 0; i <= end_zone; i++) {
			struct zone *zone = pgdat->node_zones + i;

			if (!populated_zone(zone))
				continue;

			if (zone->all_unreclaimable && priority != DEF_PRIORITY)
				continue;

			/* Confirm the zone is balanced for order-0 */
			if (!zone_watermark_ok(zone, 0,
					high_wmark_pages(zone), 0, 0)) {
				order = sc.order = 0;
				goto loop_again;
			}

			/* If balanced, clear the congested flag */
			zone_clear_flag(zone, ZONE_CONGESTED);
		}
	}

	/*
	 * Return the order we were reclaiming at so sleeping_prematurely()
	 * makes a decision on the order we were last reclaiming at. However,
	 * if another caller entered the allocator slow path while kswapd
	 * was awake, order will remain at the higher level
	 */
	*classzone_idx = end_zone;
	return order;
}

static void kswapd_try_to_sleep(pg_data_t *pgdat, int order, int classzone_idx)
{
	long remaining = 0;
	DEFINE_WAIT(wait);

	if (freezing(current) || kthread_should_stop())
		return;

	prepare_to_wait(&pgdat->kswapd_wait, &wait, TASK_INTERRUPTIBLE);

	/* Try to sleep for a short interval */
	if (!sleeping_prematurely(pgdat, order, remaining, classzone_idx)) {
		remaining = schedule_timeout(HZ/10);
		finish_wait(&pgdat->kswapd_wait, &wait);
		prepare_to_wait(&pgdat->kswapd_wait, &wait, TASK_INTERRUPTIBLE);
	}

	/*
	 * After a short sleep, check if it was a premature sleep. If not, then
	 * go fully to sleep until explicitly woken up.
	 */
	if (!sleeping_prematurely(pgdat, order, remaining, classzone_idx)) {
		trace_mm_vmscan_kswapd_sleep(pgdat->node_id);

		/*
		 * vmstat counters are not perfectly accurate and the estimated
		 * value for counters such as NR_FREE_PAGES can deviate from the
		 * true value by nr_online_cpus * threshold. To avoid the zone
		 * watermarks being breached while under pressure, we reduce the
		 * per-cpu vmstat threshold while kswapd is awake and restore
		 * them before going back to sleep.
		 */
		set_pgdat_percpu_threshold(pgdat, calculate_normal_threshold);

		if (!kthread_should_stop())
			schedule();

		set_pgdat_percpu_threshold(pgdat, calculate_pressure_threshold);
	} else {
		if (remaining)
			count_vm_event(KSWAPD_LOW_WMARK_HIT_QUICKLY);
		else
			count_vm_event(KSWAPD_HIGH_WMARK_HIT_QUICKLY);
	}
	finish_wait(&pgdat->kswapd_wait, &wait);
}

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process.
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
static int kswapd(void *p)
{
	unsigned long order, new_order;
	unsigned balanced_order;
	int classzone_idx, new_classzone_idx;
	int balanced_classzone_idx;
	pg_data_t *pgdat = (pg_data_t*)p;
	struct task_struct *tsk = current;

	struct reclaim_state reclaim_state = {
		.reclaimed_slab = 0,
	};
	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	lockdep_set_current_reclaim_state(GFP_KERNEL);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);
	current->reclaim_state = &reclaim_state;

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
	set_freezable();

	order = new_order = 0;
	balanced_order = 0;
	classzone_idx = new_classzone_idx = pgdat->nr_zones - 1;
	balanced_classzone_idx = classzone_idx;
	for ( ; ; ) {
		int ret;

		/*
		 * If the last balance_pgdat was unsuccessful it's unlikely a
		 * new request of a similar or harder type will succeed soon
		 * so consider going to sleep on the basis we reclaimed at
		 */
		if (balanced_classzone_idx >= new_classzone_idx &&
					balanced_order == new_order) {
			new_order = pgdat->kswapd_max_order;
			new_classzone_idx = pgdat->classzone_idx;
			pgdat->kswapd_max_order =  0;
			pgdat->classzone_idx = pgdat->nr_zones - 1;
		}

		if (order < new_order || classzone_idx > new_classzone_idx) {
			/*
			 * Don't sleep if someone wants a larger 'order'
			 * allocation or has tigher zone constraints
			 */
			order = new_order;
			classzone_idx = new_classzone_idx;
		} else {
			kswapd_try_to_sleep(pgdat, balanced_order,
						balanced_classzone_idx);
			order = pgdat->kswapd_max_order;
			classzone_idx = pgdat->classzone_idx;
			new_order = order;
			new_classzone_idx = classzone_idx;
			pgdat->kswapd_max_order = 0;
			pgdat->classzone_idx = pgdat->nr_zones - 1;
		}

		ret = try_to_freeze();
		if (kthread_should_stop())
			break;

		/*
		 * We can speed up thawing tasks if we don't call balance_pgdat
		 * after returning from the refrigerator
		 */
		if (!ret) {
			trace_mm_vmscan_kswapd_wake(pgdat->node_id, order);
			balanced_classzone_idx = classzone_idx;
			balanced_order = balance_pgdat(pgdat, order,
						&balanced_classzone_idx);
		}
	}

	current->reclaim_state = NULL;
	return 0;
}

/*
 * A zone is low on free memory, so wake its kswapd task to service it.
 */
void wakeup_kswapd(struct zone *zone, int order, enum zone_type classzone_idx)
{
	pg_data_t *pgdat;

	if (!populated_zone(zone))
		return;

	if (!cpuset_zone_allowed_hardwall(zone, GFP_KERNEL))
		return;
	pgdat = zone->zone_pgdat;
	if (pgdat->kswapd_max_order < order) {
		pgdat->kswapd_max_order = order;
		pgdat->classzone_idx = min(pgdat->classzone_idx, classzone_idx);
	}
	if (!waitqueue_active(&pgdat->kswapd_wait))
		return;
	if (zone_watermark_ok_safe(zone, order, low_wmark_pages(zone), 0, 0))
		return;

	trace_mm_vmscan_wakeup_kswapd(pgdat->node_id, zone_idx(zone), order);
	wake_up_interruptible(&pgdat->kswapd_wait);
}

/*
 * The reclaimable count would be mostly accurate.
 * The less reclaimable pages may be
 * - mlocked pages, which will be moved to unevictable list when encountered
 * - mapped pages, which may require several travels to be reclaimed
 * - dirty pages, which is not "instantly" reclaimable
 */
unsigned long global_reclaimable_pages(void)
{
	int nr;

	nr = global_page_state(NR_ACTIVE_FILE) +
	     global_page_state(NR_INACTIVE_FILE);

	if (nr_swap_pages > 0)
		nr += global_page_state(NR_ACTIVE_ANON) +
		      global_page_state(NR_INACTIVE_ANON);

	return nr;
}

unsigned long zone_reclaimable_pages(struct zone *zone)
{
	int nr;

	nr = zone_page_state(zone, NR_ACTIVE_FILE) +
	     zone_page_state(zone, NR_INACTIVE_FILE);

	if (nr_swap_pages > 0)
		nr += zone_page_state(zone, NR_ACTIVE_ANON) +
		      zone_page_state(zone, NR_INACTIVE_ANON);

	return nr;
}

#ifdef CONFIG_HIBERNATION
/*
 * Try to free `nr_to_reclaim' of memory, system-wide, and return the number of
 * freed pages.
 *
 * Rather than trying to age LRUs the aim is to preserve the overall
 * LRU order by reclaiming preferentially
 * inactive > active > active referenced > active mapped
 */
unsigned long shrink_all_memory(unsigned long nr_to_reclaim)
{
	struct reclaim_state reclaim_state;
	struct scan_control sc = {
		.gfp_mask = GFP_HIGHUSER_MOVABLE,
		.may_swap = 1,
		.may_unmap = 1,
		.may_writepage = 1,
		.nr_to_reclaim = nr_to_reclaim,
		.hibernation_mode = 1,
		.swappiness = vm_swappiness,
		.order = 0,
	};
	struct shrink_control shrink = {
		.gfp_mask = sc.gfp_mask,
	};
	struct zonelist *zonelist = node_zonelist(numa_node_id(), sc.gfp_mask);
	struct task_struct *p = current;
	unsigned long nr_reclaimed;

	p->flags |= PF_MEMALLOC;
	lockdep_set_current_reclaim_state(sc.gfp_mask);
	reclaim_state.reclaimed_slab = 0;
	p->reclaim_state = &reclaim_state;

	nr_reclaimed = do_try_to_free_pages(zonelist, &sc, &shrink);

	p->reclaim_state = NULL;
	lockdep_clear_current_reclaim_state();
	p->flags &= ~PF_MEMALLOC;

	return nr_reclaimed;
}
#endif /* CONFIG_HIBERNATION */

/* It's optimal to keep kswapds on the same CPUs as their memory, but
   not required for correctness.  So if the last cpu in a node goes
   away, we get changed to run anywhere: as the first one comes back,
   restore their cpu bindings. */
static int __devinit cpu_callback(struct notifier_block *nfb,
				  unsigned long action, void *hcpu)
{
	int nid;

	if (action == CPU_ONLINE || action == CPU_ONLINE_FROZEN) {
		for_each_node_state(nid, N_HIGH_MEMORY) {
			pg_data_t *pgdat = NODE_DATA(nid);
			const struct cpumask *mask;

			mask = cpumask_of_node(pgdat->node_id);

			if (cpumask_any_and(cpu_online_mask, mask) < nr_cpu_ids)
				/* One of our CPUs online: restore mask */
				set_cpus_allowed_ptr(pgdat->kswapd, mask);
		}
	}
	return NOTIFY_OK;
}

/*
 * This kswapd start function will be called by init and node-hot-add.
 * On node-hot-add, kswapd will moved to proper cpus if cpus are hot-added.
 */
int kswapd_run(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	int ret = 0;

	if (pgdat->kswapd)
		return 0;

	pgdat->kswapd = kthread_run(kswapd, pgdat, "kswapd%d", nid);
	if (IS_ERR(pgdat->kswapd)) {
		/* failure at boot is fatal */
		BUG_ON(system_state == SYSTEM_BOOTING);
		printk("Failed to start kswapd on node %d\n",nid);
		ret = -1;
	}
	return ret;
}

/*
 * Called by memory hotplug when all memory in a node is offlined.  Caller must
 * hold lock_memory_hotplug().
 */
void kswapd_stop(int nid)
{
	struct task_struct *kswapd = NODE_DATA(nid)->kswapd;

	if (kswapd) {
		kthread_stop(kswapd);
		NODE_DATA(nid)->kswapd = NULL;
	}
}

static int __init kswapd_init(void)
{
	int nid;

	swap_setup();
	for_each_node_state(nid, N_HIGH_MEMORY)
 		kswapd_run(nid);
	hotcpu_notifier(cpu_callback, 0);
	return 0;
}

module_init(kswapd_init)

#ifdef CONFIG_NUMA
/*
 * Zone reclaim mode
 *
 * If non-zero call zone_reclaim when the number of free pages falls below
 * the watermarks.
 */
int zone_reclaim_mode __read_mostly;

#define RECLAIM_OFF 0
#define RECLAIM_ZONE (1<<0)	/* Run shrink_inactive_list on the zone */
#define RECLAIM_WRITE (1<<1)	/* Writeout pages during reclaim */
#define RECLAIM_SWAP (1<<2)	/* Swap pages out during reclaim */

/*
 * Priority for ZONE_RECLAIM. This determines the fraction of pages
 * of a node considered for each zone_reclaim. 4 scans 1/16th of
 * a zone.
 */
#define ZONE_RECLAIM_PRIORITY 4

/*
 * Percentage of pages in a zone that must be unmapped for zone_reclaim to
 * occur.
 */
int sysctl_min_unmapped_ratio = 1;

/*
 * If the number of slab pages in a zone grows beyond this percentage then
 * slab reclaim needs to occur.
 */
int sysctl_min_slab_ratio = 5;

static inline unsigned long zone_unmapped_file_pages(struct zone *zone)
{
	unsigned long file_mapped = zone_page_state(zone, NR_FILE_MAPPED);
	unsigned long file_lru = zone_page_state(zone, NR_INACTIVE_FILE) +
		zone_page_state(zone, NR_ACTIVE_FILE);

	/*
	 * It's possible for there to be more file mapped pages than
	 * accounted for by the pages on the file LRU lists because
	 * tmpfs pages accounted for as ANON can also be FILE_MAPPED
	 */
	return (file_lru > file_mapped) ? (file_lru - file_mapped) : 0;
}

/* Work out how many page cache pages we can reclaim in this reclaim_mode */
static long zone_pagecache_reclaimable(struct zone *zone)
{
	long nr_pagecache_reclaimable;
	long delta = 0;

	/*
	 * If RECLAIM_SWAP is set, then all file pages are considered
	 * potentially reclaimable. Otherwise, we have to worry about
	 * pages like swapcache and zone_unmapped_file_pages() provides
	 * a better estimate
	 */
	if (zone_reclaim_mode & RECLAIM_SWAP)
		nr_pagecache_reclaimable = zone_page_state(zone, NR_FILE_PAGES);
	else
		nr_pagecache_reclaimable = zone_unmapped_file_pages(zone);

	/* If we can't clean pages, remove dirty pages from consideration */
	if (!(zone_reclaim_mode & RECLAIM_WRITE))
		delta += zone_page_state(zone, NR_FILE_DIRTY);

	/* Watch for any possible underflows due to delta */
	if (unlikely(delta > nr_pagecache_reclaimable))
		delta = nr_pagecache_reclaimable;

	return nr_pagecache_reclaimable - delta;
}

/*
 * Try to free up some pages from this zone through reclaim.
 */
static int __zone_reclaim(struct zone *zone, gfp_t gfp_mask, unsigned int order)
{
	/* Minimum pages needed in order to stay on node */
	const unsigned long nr_pages = 1 << order;
	struct task_struct *p = current;
	struct reclaim_state reclaim_state;
	int priority;
	struct scan_control sc = {
		.may_writepage = !!(zone_reclaim_mode & RECLAIM_WRITE),
		.may_unmap = !!(zone_reclaim_mode & RECLAIM_SWAP),
		.may_swap = 1,
		.nr_to_reclaim = max_t(unsigned long, nr_pages,
				       SWAP_CLUSTER_MAX),
		.gfp_mask = gfp_mask,
		.swappiness = vm_swappiness,
		.order = order,
	};
	struct shrink_control shrink = {
		.gfp_mask = sc.gfp_mask,
	};
	unsigned long nr_slab_pages0, nr_slab_pages1;

	cond_resched();
	/*
	 * We need to be able to allocate from the reserves for RECLAIM_SWAP
	 * and we also need to be able to write out pages for RECLAIM_WRITE
	 * and RECLAIM_SWAP.
	 */
	p->flags |= PF_MEMALLOC | PF_SWAPWRITE;
	lockdep_set_current_reclaim_state(gfp_mask);
	reclaim_state.reclaimed_slab = 0;
	p->reclaim_state = &reclaim_state;

	if (zone_pagecache_reclaimable(zone) > zone->min_unmapped_pages) {
		/*
		 * Free memory by calling shrink zone with increasing
		 * priorities until we have enough memory freed.
		 */
		priority = ZONE_RECLAIM_PRIORITY;
		do {
			shrink_zone(priority, zone, &sc);
			priority--;
		} while (priority >= 0 && sc.nr_reclaimed < nr_pages);
	}

	nr_slab_pages0 = zone_page_state(zone, NR_SLAB_RECLAIMABLE);
	if (nr_slab_pages0 > zone->min_slab_pages) {
		/*
		 * shrink_slab() does not currently allow us to determine how
		 * many pages were freed in this zone. So we take the current
		 * number of slab pages and shake the slab until it is reduced
		 * by the same nr_pages that we used for reclaiming unmapped
		 * pages.
		 *
		 * Note that shrink_slab will free memory on all zones and may
		 * take a long time.
		 */
		for (;;) {
			unsigned long lru_pages = zone_reclaimable_pages(zone);

			/* No reclaimable slab or very low memory pressure */
			if (!shrink_slab(&shrink, sc.nr_scanned, lru_pages))
				break;

			/* Freed enough memory */
			nr_slab_pages1 = zone_page_state(zone,
							NR_SLAB_RECLAIMABLE);
			if (nr_slab_pages1 + nr_pages <= nr_slab_pages0)
				break;
		}

		/*
		 * Update nr_reclaimed by the number of slab pages we
		 * reclaimed from this zone.
		 */
		nr_slab_pages1 = zone_page_state(zone, NR_SLAB_RECLAIMABLE);
		if (nr_slab_pages1 < nr_slab_pages0)
			sc.nr_reclaimed += nr_slab_pages0 - nr_slab_pages1;
	}

	p->reclaim_state = NULL;
	current->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE);
	lockdep_clear_current_reclaim_state();
	return sc.nr_reclaimed >= nr_pages;
}

int zone_reclaim(struct zone *zone, gfp_t gfp_mask, unsigned int order)
{
	int node_id;
	int ret;

	/*
	 * Zone reclaim reclaims unmapped file backed pages and
	 * slab pages if we are over the defined limits.
	 *
	 * A small portion of unmapped file backed pages is needed for
	 * file I/O otherwise pages read by file I/O will be immediately
	 * thrown out if the zone is overallocated. So we do not reclaim
	 * if less than a specified percentage of the zone is used by
	 * unmapped file backed pages.
	 */
	if (zone_pagecache_reclaimable(zone) <= zone->min_unmapped_pages &&
	    zone_page_state(zone, NR_SLAB_RECLAIMABLE) <= zone->min_slab_pages)
		return ZONE_RECLAIM_FULL;

	if (zone->all_unreclaimable)
		return ZONE_RECLAIM_FULL;

	/*
	 * Do not scan if the allocation should not be delayed.
	 */
	if (!(gfp_mask & __GFP_WAIT) || (current->flags & PF_MEMALLOC))
		return ZONE_RECLAIM_NOSCAN;

	/*
	 * Only run zone reclaim on the local zone or on zones that do not
	 * have associated processors. This will favor the local processor
	 * over remote processors and spread off node memory allocations
	 * as wide as possible.
	 */
	node_id = zone_to_nid(zone);
	if (node_state(node_id, N_CPU) && node_id != numa_node_id())
		return ZONE_RECLAIM_NOSCAN;

	if (zone_test_and_set_flag(zone, ZONE_RECLAIM_LOCKED))
		return ZONE_RECLAIM_NOSCAN;

	ret = __zone_reclaim(zone, gfp_mask, order);
	zone_clear_flag(zone, ZONE_RECLAIM_LOCKED);

	if (!ret)
		count_vm_event(PGSCAN_ZONE_RECLAIM_FAILED);

	return ret;
}
#endif

/*
 * page_evictable - test whether a page is evictable
 * @page: the page to test
 * @vma: the VMA in which the page is or will be mapped, may be NULL
 *
 * Test whether page is evictable--i.e., should be placed on active/inactive
 * lists vs unevictable list.  The vma argument is !NULL when called from the
 * fault path to determine how to instantate a new page.
 *
 * Reasons page might not be evictable:
 * (1) page's mapping marked unevictable
 * (2) page is part of an mlocked VMA
 *
 */
int page_evictable(struct page *page, struct vm_area_struct *vma)
{

	if (mapping_unevictable(page_mapping(page)))
		return 0;

	if (PageMlocked(page) || (vma && is_mlocked_vma(vma, page)))
		return 0;

	return 1;
}

/**
 * check_move_unevictable_page - check page for evictability and move to appropriate zone lru list
 * @page: page to check evictability and move to appropriate lru list
 * @zone: zone page is in
 *
 * Checks a page for evictability and moves the page to the appropriate
 * zone lru list.
 *
 * Restrictions: zone->lru_lock must be held, page must be on LRU and must
 * have PageUnevictable set.
 */
static void check_move_unevictable_page(struct page *page, struct zone *zone)
{
	VM_BUG_ON(PageActive(page));

retry:
	ClearPageUnevictable(page);
	if (page_evictable(page, NULL)) {
		enum lru_list l = page_lru_base_type(page);

		__dec_zone_state(zone, NR_UNEVICTABLE);
		list_move(&page->lru, &zone->lru[l].list);
		mem_cgroup_move_lists(page, LRU_UNEVICTABLE, l);
		__inc_zone_state(zone, NR_INACTIVE_ANON + l);
		__count_vm_event(UNEVICTABLE_PGRESCUED);
	} else {
		/*
		 * rotate unevictable list
		 */
		SetPageUnevictable(page);
		list_move(&page->lru, &zone->lru[LRU_UNEVICTABLE].list);
		mem_cgroup_rotate_lru_list(page, LRU_UNEVICTABLE);
		if (page_evictable(page, NULL))
			goto retry;
	}
}

/**
 * scan_mapping_unevictable_pages - scan an address space for evictable pages
 * @mapping: struct address_space to scan for evictable pages
 *
 * Scan all pages in mapping.  Check unevictable pages for
 * evictability and move them to the appropriate zone lru list.
 */
void scan_mapping_unevictable_pages(struct address_space *mapping)
{
	pgoff_t next = 0;
	pgoff_t end   = (i_size_read(mapping->host) + PAGE_CACHE_SIZE - 1) >>
			 PAGE_CACHE_SHIFT;
	struct zone *zone;
	struct pagevec pvec;

	if (mapping->nrpages == 0)
		return;

	pagevec_init(&pvec, 0);
	while (next < end &&
		pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		int i;
		int pg_scanned = 0;

		zone = NULL;

		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			pgoff_t page_index = page->index;
			struct zone *pagezone = page_zone(page);

			pg_scanned++;
			if (page_index > next)
				next = page_index;
			next++;

			if (pagezone != zone) {
				if (zone)
					spin_unlock_irq(&zone->lru_lock);
				zone = pagezone;
				spin_lock_irq(&zone->lru_lock);
			}

			if (PageLRU(page) && PageUnevictable(page))
				check_move_unevictable_page(page, zone);
		}
		if (zone)
			spin_unlock_irq(&zone->lru_lock);
		pagevec_release(&pvec);

		count_vm_events(UNEVICTABLE_PGSCANNED, pg_scanned);
	}

}

/**
 * scan_zone_unevictable_pages - check unevictable list for evictable pages
 * @zone - zone of which to scan the unevictable list
 *
 * Scan @zone's unevictable LRU lists to check for pages that have become
 * evictable.  Move those that have to @zone's inactive list where they
 * become candidates for reclaim, unless shrink_inactive_zone() decides
 * to reactivate them.  Pages that are still unevictable are rotated
 * back onto @zone's unevictable list.
 */
#define SCAN_UNEVICTABLE_BATCH_SIZE 16UL /* arbitrary lock hold batch size */
static void scan_zone_unevictable_pages(struct zone *zone)
{
	struct list_head *l_unevictable = &zone->lru[LRU_UNEVICTABLE].list;
	unsigned long scan;
	unsigned long nr_to_scan = zone_page_state(zone, NR_UNEVICTABLE);

	while (nr_to_scan > 0) {
		unsigned long batch_size = min(nr_to_scan,
						SCAN_UNEVICTABLE_BATCH_SIZE);

		spin_lock_irq(&zone->lru_lock);
		for (scan = 0;  scan < batch_size; scan++) {
			struct page *page = lru_to_page(l_unevictable);

			if (!trylock_page(page))
				continue;

			prefetchw_prev_lru_page(page, l_unevictable, flags);

			if (likely(PageLRU(page) && PageUnevictable(page)))
				check_move_unevictable_page(page, zone);

			unlock_page(page);
		}
		spin_unlock_irq(&zone->lru_lock);

		nr_to_scan -= batch_size;
	}
}


/**
 * scan_all_zones_unevictable_pages - scan all unevictable lists for evictable pages
 *
 * A really big hammer:  scan all zones' unevictable LRU lists to check for
 * pages that have become evictable.  Move those back to the zones'
 * inactive list where they become candidates for reclaim.
 * This occurs when, e.g., we have unswappable pages on the unevictable lists,
 * and we add swap to the system.  As such, it runs in the context of a task
 * that has possibly/probably made some previously unevictable pages
 * evictable.
 */
static void scan_all_zones_unevictable_pages(void)
{
	struct zone *zone;

	for_each_zone(zone) {
		scan_zone_unevictable_pages(zone);
	}
}

/*
 * scan_unevictable_pages [vm] sysctl handler.  On demand re-scan of
 * all nodes' unevictable lists for evictable pages
 */
unsigned long scan_unevictable_pages;

int scan_unevictable_handler(struct ctl_table *table, int write,
			   void __user *buffer,
			   size_t *length, loff_t *ppos)
{
	proc_doulongvec_minmax(table, write, buffer, length, ppos);

	if (write && *(unsigned long *)table->data)
		scan_all_zones_unevictable_pages();

	scan_unevictable_pages = 0;
	return 0;
}

#ifdef CONFIG_NUMA
/*
 * per node 'scan_unevictable_pages' attribute.  On demand re-scan of
 * a specified node's per zone unevictable lists for evictable pages.
 */

static ssize_t read_scan_unevictable_node(struct sys_device *dev,
					  struct sysdev_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "0\n");	/* always zero; should fit... */
}

static ssize_t write_scan_unevictable_node(struct sys_device *dev,
					   struct sysdev_attribute *attr,
					const char *buf, size_t count)
{
	struct zone *node_zones = NODE_DATA(dev->id)->node_zones;
	struct zone *zone;
	unsigned long res;
	unsigned long req = strict_strtoul(buf, 10, &res);

	if (!req)
		return 1;	/* zero is no-op */

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!populated_zone(zone))
			continue;
		scan_zone_unevictable_pages(zone);
	}
	return 1;
}


static SYSDEV_ATTR(scan_unevictable_pages, S_IRUGO | S_IWUSR,
			read_scan_unevictable_node,
			write_scan_unevictable_node);

int scan_unevictable_register_node(struct node *node)
{
	return sysdev_create_file(&node->sysdev, &attr_scan_unevictable_pages);
}

void scan_unevictable_unregister_node(struct node *node)
{
	sysdev_remove_file(&node->sysdev, &attr_scan_unevictable_pages);
}
#endif
