/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
 */
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/pagevec.h>
#include <linux/migrate.h>
#include <linux/page_cgroup.h>

#include <asm/pgtable.h>

/*
 * swapper_space is a fiction, retained to simplify the path through
 * vmscan's shrink_page_list.
 */
static const struct address_space_operations swap_aops = {
	.writepage	= swap_writepage,
	.set_page_dirty	= swap_set_page_dirty,
	.migratepage	= migrate_page,
};

static struct backing_dev_info swap_backing_dev_info = {
	.name		= "swap",
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK | BDI_CAP_SWAP_BACKED,
};

struct address_space swapper_spaces[MAX_SWAPFILES] = {
	[0 ... MAX_SWAPFILES - 1] = {
		.page_tree	= RADIX_TREE_INIT(GFP_ATOMIC|__GFP_NOWARN),
		.a_ops		= &swap_aops,
		.backing_dev_info = &swap_backing_dev_info,
	}
};

#define INC_CACHE_INFO(x)	do { swap_cache_info.x++; } while (0)

static struct {
	unsigned long add_total;
	unsigned long del_total;
	unsigned long find_success;
	unsigned long find_total;
} swap_cache_info;

unsigned long total_swapcache_pages(void)
{
	int i;
	unsigned long ret = 0;

	for (i = 0; i < MAX_SWAPFILES; i++)
		ret += swapper_spaces[i].nrpages;
	return ret;
}

static atomic_t swapin_readahead_hits = ATOMIC_INIT(4);

void show_swap_cache_info(void)
{
	printk("%lu pages in swap cache\n", total_swapcache_pages());
	printk("Swap cache stats: add %lu, delete %lu, find %lu/%lu\n",
		swap_cache_info.add_total, swap_cache_info.del_total,
		swap_cache_info.find_success, swap_cache_info.find_total);
	printk("Free swap  = %ldkB\n",
		get_nr_swap_pages() << (PAGE_SHIFT - 10));
	printk("Total swap = %lukB\n", total_swap_pages << (PAGE_SHIFT - 10));
}

/*
 * __add_to_swap_cache resembles add_to_page_cache_locked on swapper_space,
 * but sets SwapCache flag and private instead of mapping and index.
 */
int __add_to_swap_cache(struct page *page, swp_entry_t entry)
{
	int error;
	struct address_space *address_space;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(PageSwapCache(page), page);
	VM_BUG_ON_PAGE(!PageSwapBacked(page), page);

	page_cache_get(page);
	SetPageSwapCache(page);
	set_page_private(page, entry.val);

	address_space = swap_address_space(entry);
	spin_lock_irq(&address_space->tree_lock);
	error = radix_tree_insert(&address_space->page_tree,
					entry.val, page);
	if (likely(!error)) {
		address_space->nrpages++;
		__inc_zone_page_state(page, NR_FILE_PAGES);
		INC_CACHE_INFO(add_total);
	}
	spin_unlock_irq(&address_space->tree_lock);

	if (unlikely(error)) {
		/*
		 * Only the context which have set SWAP_HAS_CACHE flag
		 * would call add_to_swap_cache().
		 * So add_to_swap_cache() doesn't returns -EEXIST.
		 */
		VM_BUG_ON(error == -EEXIST);
		set_page_private(page, 0UL);
		ClearPageSwapCache(page);
		page_cache_release(page);
	}

	return error;
}


int add_to_swap_cache(struct page *page, swp_entry_t entry, gfp_t gfp_mask)
{
	int error;

	error = radix_tree_maybe_preload(gfp_mask);
	if (!error) {
		error = __add_to_swap_cache(page, entry);
		radix_tree_preload_end();
	}
	return error;
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache.
 */
void __delete_from_swap_cache(struct page *page)
{
	swp_entry_t entry;
	struct address_space *address_space;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageSwapCache(page), page);
	VM_BUG_ON_PAGE(PageWriteback(page), page);

	entry.val = page_private(page);
	address_space = swap_address_space(entry);
	radix_tree_delete(&address_space->page_tree, page_private(page));
	set_page_private(page, 0);
	ClearPageSwapCache(page);
	address_space->nrpages--;
	__dec_zone_page_state(page, NR_FILE_PAGES);
	INC_CACHE_INFO(del_total);
}

/**
 * add_to_swap - allocate swap space for a page
 * @page: page we want to move to swap
 *
 * Allocate swap space for the page and add the page to the
 * swap cache.  Caller needs to hold the page lock. 
 */
int add_to_swap(struct page *page, struct list_head *list)
{
	swp_entry_t entry;
	int err;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageUptodate(page), page);

	entry = get_swap_page();
	if (!entry.val)
		return 0;

	if (unlikely(PageTransHuge(page)))
		if (unlikely(split_huge_page_to_list(page, list))) {
			swapcache_free(entry);
			return 0;
		}

	/*
	 * Radix-tree node allocations from PF_MEMALLOC contexts could
	 * completely exhaust the page allocator. __GFP_NOMEMALLOC
	 * stops emergency reserves from being allocated.
	 *
	 * TODO: this could cause a theoretical memory reclaim
	 * deadlock in the swap out path.
	 */
	/*
	 * Add it to the swap cache and mark it dirty
	 */
	err = add_to_swap_cache(page, entry,
			__GFP_HIGH|__GFP_NOMEMALLOC|__GFP_NOWARN);

	if (!err) {	/* Success */
		SetPageDirty(page);
		return 1;
	} else {	/* -ENOMEM radix-tree allocation failure */
		/*
		 * add_to_swap_cache() doesn't return -EEXIST, so we can safely
		 * clear SWAP_HAS_CACHE flag.
		 */
		swapcache_free(entry);
		return 0;
	}
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache and locked.
 * It will never put the page into the free list,
 * the caller has a reference on the page.
 */
void delete_from_swap_cache(struct page *page)
{
	swp_entry_t entry;
	struct address_space *address_space;

	entry.val = page_private(page);

	address_space = swap_address_space(entry);
	spin_lock_irq(&address_space->tree_lock);
	__delete_from_swap_cache(page);
	spin_unlock_irq(&address_space->tree_lock);

	swapcache_free(entry);
	page_cache_release(page);
}

/* 
 * If we are the only user, then try to free up the swap cache. 
 * 
 * Its ok to check for PageSwapCache without the page lock
 * here because we are going to recheck again inside
 * try_to_free_swap() _with_ the lock.
 * 					- Marcelo
 */
static inline void free_swap_cache(struct page *page)
{
	if (PageSwapCache(page) && !page_mapped(page) && trylock_page(page)) {
		try_to_free_swap(page);
		unlock_page(page);
	}
}

/* 
 * Perform a free_page(), also freeing any swap cache associated with
 * this page if it is the last user of the page.
 */
void free_page_and_swap_cache(struct page *page)
{
	free_swap_cache(page);
	page_cache_release(page);
}

/*
 * Passed an array of pages, drop them all from swapcache and then release
 * them.  They are removed from the LRU and freed if this is their last use.
 */
void free_pages_and_swap_cache(struct page **pages, int nr)
{
	struct page **pagep = pages;

	lru_add_drain();
	while (nr) {
		int todo = min(nr, PAGEVEC_SIZE);
		int i;

		for (i = 0; i < todo; i++)
			free_swap_cache(pagep[i]);
		release_pages(pagep, todo, false);
		pagep += todo;
		nr -= todo;
	}
}

/*
 * Lookup a swap entry in the swap cache. A found page will be returned
 * unlocked and with its refcount incremented - we rely on the kernel
 * lock getting page table operations atomic even if we drop the page
 * lock before returning.
 */
struct page * lookup_swap_cache(swp_entry_t entry)
{
	struct page *page;

	page = find_get_page(swap_address_space(entry), entry.val);

	if (page) {
		INC_CACHE_INFO(find_success);
		if (TestClearPageReadahead(page))
			atomic_inc(&swapin_readahead_hits);
	}

	INC_CACHE_INFO(find_total);
	return page;
}

/* 
 * Locate a page of swap in physical memory, reserving swap cache space
 * and reading the disk if it is not already cached.
 * A failure return means that either the page allocation failed or that
 * the swap entry is no longer in use.
 */
struct page *read_swap_cache_async(swp_entry_t entry, gfp_t gfp_mask,
			struct vm_area_struct *vma, unsigned long addr)
{
	struct page *found_page, *new_page = NULL;
	int err;

	do {
		/*
		 * First check the swap cache.  Since this is normally
		 * called after lookup_swap_cache() failed, re-calling
		 * that would confuse statistics.
		 */
		found_page = find_get_page(swap_address_space(entry),
					entry.val);
		if (found_page)
			break;

		/*
		 * Get a new page to read into from swap.
		 */
		if (!new_page) {
			new_page = alloc_page_vma(gfp_mask, vma, addr);
			if (!new_page)
				break;		/* Out of memory */
		}

		/*
		 * call radix_tree_preload() while we can wait.
		 */
		err = radix_tree_maybe_preload(gfp_mask & GFP_KERNEL);
		if (err)
			break;

		/*
		 * Swap entry may have been freed since our caller observed it.
		 */
		err = swapcache_prepare(entry);
		if (err == -EEXIST) {
			radix_tree_preload_end();
			/*
			 * We might race against get_swap_page() and stumble
			 * across a SWAP_HAS_CACHE swap_map entry whose page
			 * has not been brought into the swapcache yet, while
			 * the other end is scheduled away waiting on discard
			 * I/O completion at scan_swap_map().
			 *
			 * In order to avoid turning this transitory state
			 * into a permanent loop around this -EEXIST case
			 * if !CONFIG_PREEMPT and the I/O completion happens
			 * to be waiting on the CPU waitqueue where we are now
			 * busy looping, we just conditionally invoke the
			 * scheduler here, if there are some more important
			 * tasks to run.
			 */
			cond_resched();
			continue;
		}
		if (err) {		/* swp entry is obsolete ? */
			radix_tree_preload_end();
			break;
		}

		/* May fail (-ENOMEM) if radix-tree node allocation failed. */
		__set_page_locked(new_page);
		SetPageSwapBacked(new_page);
		err = __add_to_swap_cache(new_page, entry);
		if (likely(!err)) {
			radix_tree_preload_end();
			/*
			 * Initiate read into locked page and return.
			 */
			lru_cache_add_anon(new_page);
			swap_readpage(new_page);
			return new_page;
		}
		radix_tree_preload_end();
		ClearPageSwapBacked(new_page);
		__clear_page_locked(new_page);
		/*
		 * add_to_swap_cache() doesn't return -EEXIST, so we can safely
		 * clear SWAP_HAS_CACHE flag.
		 */
		swapcache_free(entry);
	} while (err != -ENOMEM);

	if (new_page)
		page_cache_release(new_page);
	return found_page;
}

static unsigned long swapin_nr_pages(unsigned long offset)
{
	static unsigned long prev_offset;
	unsigned int pages, max_pages, last_ra;
	static atomic_t last_readahead_pages;

	max_pages = 1 << ACCESS_ONCE(page_cluster);
	if (max_pages <= 1)
		return 1;

	/*
	 * This heuristic has been found to work well on both sequential and
	 * random loads, swapping to hard disk or to SSD: please don't ask
	 * what the "+ 2" means, it just happens to work well, that's all.
	 */
	pages = atomic_xchg(&swapin_readahead_hits, 0) + 2;
	if (pages == 2) {
		/*
		 * We can have no readahead hits to judge by: but must not get
		 * stuck here forever, so check for an adjacent offset instead
		 * (and don't even bother to check whether swap type is same).
		 */
		if (offset != prev_offset + 1 && offset != prev_offset - 1)
			pages = 1;
		prev_offset = offset;
	} else {
		unsigned int roundup = 4;
		while (roundup < pages)
			roundup <<= 1;
		pages = roundup;
	}

	if (pages > max_pages)
		pages = max_pages;

	/* Don't shrink readahead too fast */
	last_ra = atomic_read(&last_readahead_pages) / 2;
	if (pages < last_ra)
		pages = last_ra;
	atomic_set(&last_readahead_pages, pages);

	return pages;
}

/**
 * swapin_readahead - swap in pages in hope we need them soon
 * @entry: swap entry of this memory
 * @gfp_mask: memory allocation flags
 * @vma: user vma this address belongs to
 * @addr: target address for mempolicy
 *
 * Returns the struct page for entry and addr, after queueing swapin.
 *
 * Primitive swap readahead code. We simply read an aligned block of
 * (1 << page_cluster) entries in the swap area. This method is chosen
 * because it doesn't cost us any seek time.  We also make sure to queue
 * the 'original' request together with the readahead ones...
 *
 * This has been extended to use the NUMA policies from the mm triggering
 * the readahead.
 *
 * Caller must hold down_read on the vma->vm_mm if vma is not NULL.
 */
struct page *swapin_readahead(swp_entry_t entry, gfp_t gfp_mask,
			struct vm_area_struct *vma, unsigned long addr)
{
	struct page *page;
	unsigned long entry_offset = swp_offset(entry);
	unsigned long offset = entry_offset;
	unsigned long start_offset, end_offset;
	unsigned long mask;
	struct blk_plug plug;

	mask = swapin_nr_pages(offset) - 1;
	if (!mask)
		goto skip;

	/* Read a page_cluster sized and aligned cluster around offset. */
	start_offset = offset & ~mask;
	end_offset = offset | mask;
	if (!start_offset)	/* First page is swap header. */
		start_offset++;

	blk_start_plug(&plug);
	for (offset = start_offset; offset <= end_offset ; offset++) {
		/* Ok, do the async read-ahead now */
		page = read_swap_cache_async(swp_entry(swp_type(entry), offset),
						gfp_mask, vma, addr);
		if (!page)
			continue;
		if (offset != entry_offset)
			SetPageReadahead(page);
		page_cache_release(page);
	}
	blk_finish_plug(&plug);

	lru_add_drain();	/* Push any new pages onto the LRU now */
skip:
	return read_swap_cache_async(entry, gfp_mask, vma, addr);
}
