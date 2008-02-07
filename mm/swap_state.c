/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/migrate.h>
#include <linux/memcontrol.h>

#include <asm/pgtable.h>

/*
 * swapper_space is a fiction, retained to simplify the path through
 * vmscan's shrink_page_list, to make sync_page look nicer, and to allow
 * future use of radix_tree tags in the swap cache.
 */
static const struct address_space_operations swap_aops = {
	.writepage	= swap_writepage,
	.sync_page	= block_sync_page,
	.set_page_dirty	= __set_page_dirty_nobuffers,
	.migratepage	= migrate_page,
};

static struct backing_dev_info swap_backing_dev_info = {
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
	.unplug_io_fn	= swap_unplug_io_fn,
};

struct address_space swapper_space = {
	.page_tree	= RADIX_TREE_INIT(GFP_ATOMIC|__GFP_NOWARN),
	.tree_lock	= __RW_LOCK_UNLOCKED(swapper_space.tree_lock),
	.a_ops		= &swap_aops,
	.i_mmap_nonlinear = LIST_HEAD_INIT(swapper_space.i_mmap_nonlinear),
	.backing_dev_info = &swap_backing_dev_info,
};

#define INC_CACHE_INFO(x)	do { swap_cache_info.x++; } while (0)

static struct {
	unsigned long add_total;
	unsigned long del_total;
	unsigned long find_success;
	unsigned long find_total;
} swap_cache_info;

void show_swap_cache_info(void)
{
	printk("Swap cache: add %lu, delete %lu, find %lu/%lu\n",
		swap_cache_info.add_total, swap_cache_info.del_total,
		swap_cache_info.find_success, swap_cache_info.find_total);
	printk("Free swap  = %lukB\n", nr_swap_pages << (PAGE_SHIFT - 10));
	printk("Total swap = %lukB\n", total_swap_pages << (PAGE_SHIFT - 10));
}

/*
 * add_to_swap_cache resembles add_to_page_cache on swapper_space,
 * but sets SwapCache flag and private instead of mapping and index.
 */
int add_to_swap_cache(struct page *page, swp_entry_t entry, gfp_t gfp_mask)
{
	int error;

	BUG_ON(!PageLocked(page));
	BUG_ON(PageSwapCache(page));
	BUG_ON(PagePrivate(page));
	error = radix_tree_preload(gfp_mask);
	if (!error) {

		error = mem_cgroup_cache_charge(page, current->mm, gfp_mask);
		if (error)
			goto out;

		write_lock_irq(&swapper_space.tree_lock);
		error = radix_tree_insert(&swapper_space.page_tree,
						entry.val, page);
		if (!error) {
			page_cache_get(page);
			SetPageSwapCache(page);
			set_page_private(page, entry.val);
			total_swapcache_pages++;
			__inc_zone_page_state(page, NR_FILE_PAGES);
			INC_CACHE_INFO(add_total);
		} else {
			mem_cgroup_uncharge_page(page);
		}
		write_unlock_irq(&swapper_space.tree_lock);
		radix_tree_preload_end();
	}
out:
	return error;
}

/*
 * This must be called only on pages that have
 * been verified to be in the swap cache.
 */
void __delete_from_swap_cache(struct page *page)
{
	BUG_ON(!PageLocked(page));
	BUG_ON(!PageSwapCache(page));
	BUG_ON(PageWriteback(page));
	BUG_ON(PagePrivate(page));

	mem_cgroup_uncharge_page(page);
	radix_tree_delete(&swapper_space.page_tree, page_private(page));
	set_page_private(page, 0);
	ClearPageSwapCache(page);
	total_swapcache_pages--;
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
int add_to_swap(struct page * page, gfp_t gfp_mask)
{
	swp_entry_t entry;
	int err;

	BUG_ON(!PageLocked(page));
	BUG_ON(!PageUptodate(page));

	for (;;) {
		entry = get_swap_page();
		if (!entry.val)
			return 0;

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
				gfp_mask|__GFP_NOMEMALLOC|__GFP_NOWARN);

		switch (err) {
		case 0:				/* Success */
			SetPageDirty(page);
			return 1;
		case -EEXIST:
			/* Raced with "speculative" read_swap_cache_async */
			swap_free(entry);
			continue;
		default:
			/* -ENOMEM radix-tree allocation failure */
			swap_free(entry);
			return 0;
		}
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

	entry.val = page_private(page);

	write_lock_irq(&swapper_space.tree_lock);
	__delete_from_swap_cache(page);
	write_unlock_irq(&swapper_space.tree_lock);

	swap_free(entry);
	page_cache_release(page);
}

/* 
 * If we are the only user, then try to free up the swap cache. 
 * 
 * Its ok to check for PageSwapCache without the page lock
 * here because we are going to recheck again inside 
 * exclusive_swap_page() _with_ the lock. 
 * 					- Marcelo
 */
static inline void free_swap_cache(struct page *page)
{
	if (PageSwapCache(page) && !TestSetPageLocked(page)) {
		remove_exclusive_swap_page(page);
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
		release_pages(pagep, todo, 0);
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

	page = find_get_page(&swapper_space, entry.val);

	if (page)
		INC_CACHE_INFO(find_success);

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
		found_page = find_get_page(&swapper_space, entry.val);
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
		 * Swap entry may have been freed since our caller observed it.
		 */
		if (!swap_duplicate(entry))
			break;

		/*
		 * Associate the page with swap entry in the swap cache.
		 * May fail (-EEXIST) if there is already a page associated
		 * with this entry in the swap cache: added by a racing
		 * read_swap_cache_async, or add_to_swap or shmem_writepage
		 * re-using the just freed swap entry for an existing page.
		 * May fail (-ENOMEM) if radix-tree node allocation failed.
		 */
		SetPageLocked(new_page);
		err = add_to_swap_cache(new_page, entry, gfp_mask & GFP_KERNEL);
		if (!err) {
			/*
			 * Initiate read into locked page and return.
			 */
			lru_cache_add_active(new_page);
			swap_readpage(NULL, new_page);
			return new_page;
		}
		ClearPageLocked(new_page);
		swap_free(entry);
	} while (err != -ENOMEM);

	if (new_page)
		page_cache_release(new_page);
	return found_page;
}

/**
 * swapin_readahead - swap in pages in hope we need them soon
 * @entry: swap entry of this memory
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
	int nr_pages;
	struct page *page;
	unsigned long offset;
	unsigned long end_offset;

	/*
	 * Get starting offset for readaround, and number of pages to read.
	 * Adjust starting address by readbehind (for NUMA interleave case)?
	 * No, it's very unlikely that swap layout would follow vma layout,
	 * more likely that neighbouring swap pages came from the same node:
	 * so use the same "addr" to choose the same node for each swap read.
	 */
	nr_pages = valid_swaphandles(entry, &offset);
	for (end_offset = offset + nr_pages; offset < end_offset; offset++) {
		/* Ok, do the async read-ahead now */
		page = read_swap_cache_async(swp_entry(swp_type(entry), offset),
						gfp_mask, vma, addr);
		if (!page)
			break;
		page_cache_release(page);
	}
	lru_add_drain();	/* Push any new pages onto the LRU now */
	return read_swap_cache_async(entry, gfp_mask, vma, addr);
}
