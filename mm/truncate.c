/*
 * mm/truncate.c - code for taking down pages from address_spaces
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 10Sep2002	Andrew Morton
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/backing-dev.h>
#include <linux/dax.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/export.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/pagevec.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/buffer_head.h>	/* grr. try_to_release_page,
				   do_invalidatepage */
#include <linux/shmem_fs.h>
#include <linux/cleancache.h>
#include <linux/rmap.h>
#include "internal.h"

static void clear_shadow_entry(struct address_space *mapping, pgoff_t index,
			       void *entry)
{
	struct radix_tree_node *node;
	void **slot;

	spin_lock_irq(&mapping->tree_lock);
	/*
	 * Regular page slots are stabilized by the page lock even
	 * without the tree itself locked.  These unlocked entries
	 * need verification under the tree lock.
	 */
	if (!__radix_tree_lookup(&mapping->page_tree, index, &node, &slot))
		goto unlock;
	if (*slot != entry)
		goto unlock;
	__radix_tree_replace(&mapping->page_tree, node, slot, NULL,
			     workingset_update_node, mapping);
	mapping->nrexceptional--;
unlock:
	spin_unlock_irq(&mapping->tree_lock);
}

/*
 * Unconditionally remove exceptional entry. Usually called from truncate path.
 */
static void truncate_exceptional_entry(struct address_space *mapping,
				       pgoff_t index, void *entry)
{
	/* Handled by shmem itself */
	if (shmem_mapping(mapping))
		return;

	if (dax_mapping(mapping)) {
		dax_delete_mapping_entry(mapping, index);
		return;
	}
	clear_shadow_entry(mapping, index, entry);
}

/*
 * Invalidate exceptional entry if easily possible. This handles exceptional
 * entries for invalidate_inode_pages() so for DAX it evicts only unlocked and
 * clean entries.
 */
static int invalidate_exceptional_entry(struct address_space *mapping,
					pgoff_t index, void *entry)
{
	/* Handled by shmem itself */
	if (shmem_mapping(mapping))
		return 1;
	if (dax_mapping(mapping))
		return dax_invalidate_mapping_entry(mapping, index);
	clear_shadow_entry(mapping, index, entry);
	return 1;
}

/*
 * Invalidate exceptional entry if clean. This handles exceptional entries for
 * invalidate_inode_pages2() so for DAX it evicts only clean entries.
 */
static int invalidate_exceptional_entry2(struct address_space *mapping,
					 pgoff_t index, void *entry)
{
	/* Handled by shmem itself */
	if (shmem_mapping(mapping))
		return 1;
	if (dax_mapping(mapping))
		return dax_invalidate_mapping_entry_sync(mapping, index);
	clear_shadow_entry(mapping, index, entry);
	return 1;
}

/**
 * do_invalidatepage - invalidate part or all of a page
 * @page: the page which is affected
 * @offset: start of the range to invalidate
 * @length: length of the range to invalidate
 *
 * do_invalidatepage() is called when all or part of the page has become
 * invalidated by a truncate operation.
 *
 * do_invalidatepage() does not have to release all buffers, but it must
 * ensure that no dirty buffer is left outside @offset and that no I/O
 * is underway against any of the blocks which are outside the truncation
 * point.  Because the caller is about to free (and possibly reuse) those
 * blocks on-disk.
 */
void do_invalidatepage(struct page *page, unsigned int offset,
		       unsigned int length)
{
	void (*invalidatepage)(struct page *, unsigned int, unsigned int);

	invalidatepage = page->mapping->a_ops->invalidatepage;
#ifdef CONFIG_BLOCK
	if (!invalidatepage)
		invalidatepage = block_invalidatepage;
#endif
	if (invalidatepage)
		(*invalidatepage)(page, offset, length);
}

/*
 * If truncate cannot remove the fs-private metadata from the page, the page
 * becomes orphaned.  It will be left on the LRU and may even be mapped into
 * user pagetables if we're racing with filemap_fault().
 *
 * We need to bale out if page->mapping is no longer equal to the original
 * mapping.  This happens a) when the VM reclaimed the page while we waited on
 * its lock, b) when a concurrent invalidate_mapping_pages got there first and
 * c) when tmpfs swizzles a page between a tmpfs inode and swapper_space.
 */
static int
truncate_complete_page(struct address_space *mapping, struct page *page)
{
	if (page->mapping != mapping)
		return -EIO;

	if (page_has_private(page))
		do_invalidatepage(page, 0, PAGE_SIZE);

	/*
	 * Some filesystems seem to re-dirty the page even after
	 * the VM has canceled the dirty bit (eg ext3 journaling).
	 * Hence dirty accounting check is placed after invalidation.
	 */
	cancel_dirty_page(page);
	ClearPageMappedToDisk(page);
	delete_from_page_cache(page);
	return 0;
}

/*
 * This is for invalidate_mapping_pages().  That function can be called at
 * any time, and is not supposed to throw away dirty pages.  But pages can
 * be marked dirty at any time too, so use remove_mapping which safely
 * discards clean, unused pages.
 *
 * Returns non-zero if the page was successfully invalidated.
 */
static int
invalidate_complete_page(struct address_space *mapping, struct page *page)
{
	int ret;

	if (page->mapping != mapping)
		return 0;

	if (page_has_private(page) && !try_to_release_page(page, 0))
		return 0;

	ret = remove_mapping(mapping, page);

	return ret;
}

int truncate_inode_page(struct address_space *mapping, struct page *page)
{
	loff_t holelen;
	VM_BUG_ON_PAGE(PageTail(page), page);

	holelen = PageTransHuge(page) ? HPAGE_PMD_SIZE : PAGE_SIZE;
	if (page_mapped(page)) {
		unmap_mapping_range(mapping,
				   (loff_t)page->index << PAGE_SHIFT,
				   holelen, 0);
	}
	return truncate_complete_page(mapping, page);
}

/*
 * Used to get rid of pages on hardware memory corruption.
 */
int generic_error_remove_page(struct address_space *mapping, struct page *page)
{
	if (!mapping)
		return -EINVAL;
	/*
	 * Only punch for normal data pages for now.
	 * Handling other types like directories would need more auditing.
	 */
	if (!S_ISREG(mapping->host->i_mode))
		return -EIO;
	return truncate_inode_page(mapping, page);
}
EXPORT_SYMBOL(generic_error_remove_page);

/*
 * Safely invalidate one page from its pagecache mapping.
 * It only drops clean, unused pages. The page must be locked.
 *
 * Returns 1 if the page is successfully invalidated, otherwise 0.
 */
int invalidate_inode_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);
	if (!mapping)
		return 0;
	if (PageDirty(page) || PageWriteback(page))
		return 0;
	if (page_mapped(page))
		return 0;
	return invalidate_complete_page(mapping, page);
}

/**
 * truncate_inode_pages_range - truncate range of pages specified by start & end byte offsets
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 * @lend: offset to which to truncate (inclusive)
 *
 * Truncate the page cache, removing the pages that are between
 * specified offsets (and zeroing out partial pages
 * if lstart or lend + 1 is not page aligned).
 *
 * Truncate takes two passes - the first pass is nonblocking.  It will not
 * block on page locks and it will not block on writeback.  The second pass
 * will wait.  This is to prevent as much IO as possible in the affected region.
 * The first pass will remove most pages, so the search cost of the second pass
 * is low.
 *
 * We pass down the cache-hot hint to the page freeing code.  Even if the
 * mapping is large, it is probably the case that the final pages are the most
 * recently touched, and freeing happens in ascending file offset order.
 *
 * Note that since ->invalidatepage() accepts range to invalidate
 * truncate_inode_pages_range is able to handle cases where lend + 1 is not
 * page aligned properly.
 */
void truncate_inode_pages_range(struct address_space *mapping,
				loff_t lstart, loff_t lend)
{
	pgoff_t		start;		/* inclusive */
	pgoff_t		end;		/* exclusive */
	unsigned int	partial_start;	/* inclusive */
	unsigned int	partial_end;	/* exclusive */
	struct pagevec	pvec;
	pgoff_t		indices[PAGEVEC_SIZE];
	pgoff_t		index;
	int		i;

	cleancache_invalidate_inode(mapping);
	if (mapping->nrpages == 0 && mapping->nrexceptional == 0)
		return;

	/* Offsets within partial pages */
	partial_start = lstart & (PAGE_SIZE - 1);
	partial_end = (lend + 1) & (PAGE_SIZE - 1);

	/*
	 * 'start' and 'end' always covers the range of pages to be fully
	 * truncated. Partial pages are covered with 'partial_start' at the
	 * start of the range and 'partial_end' at the end of the range.
	 * Note that 'end' is exclusive while 'lend' is inclusive.
	 */
	start = (lstart + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (lend == -1)
		/*
		 * lend == -1 indicates end-of-file so we have to set 'end'
		 * to the highest possible pgoff_t and since the type is
		 * unsigned we're using -1.
		 */
		end = -1;
	else
		end = (lend + 1) >> PAGE_SHIFT;

	pagevec_init(&pvec, 0);
	index = start;
	while (index < end && pagevec_lookup_entries(&pvec, mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE),
			indices)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			/* We rely upon deletion not changing page->index */
			index = indices[i];
			if (index >= end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				truncate_exceptional_entry(mapping, index,
							   page);
				continue;
			}

			if (!trylock_page(page))
				continue;
			WARN_ON(page_to_index(page) != index);
			if (PageWriteback(page)) {
				unlock_page(page);
				continue;
			}
			truncate_inode_page(mapping, page);
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		cond_resched();
		index++;
	}

	if (partial_start) {
		struct page *page = find_lock_page(mapping, start - 1);
		if (page) {
			unsigned int top = PAGE_SIZE;
			if (start > end) {
				/* Truncation within a single page */
				top = partial_end;
				partial_end = 0;
			}
			wait_on_page_writeback(page);
			zero_user_segment(page, partial_start, top);
			cleancache_invalidate_page(mapping, page);
			if (page_has_private(page))
				do_invalidatepage(page, partial_start,
						  top - partial_start);
			unlock_page(page);
			put_page(page);
		}
	}
	if (partial_end) {
		struct page *page = find_lock_page(mapping, end);
		if (page) {
			wait_on_page_writeback(page);
			zero_user_segment(page, 0, partial_end);
			cleancache_invalidate_page(mapping, page);
			if (page_has_private(page))
				do_invalidatepage(page, 0,
						  partial_end);
			unlock_page(page);
			put_page(page);
		}
	}
	/*
	 * If the truncation happened within a single page no pages
	 * will be released, just zeroed, so we can bail out now.
	 */
	if (start >= end)
		return;

	index = start;
	for ( ; ; ) {
		cond_resched();
		if (!pagevec_lookup_entries(&pvec, mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE), indices)) {
			/* If all gone from start onwards, we're done */
			if (index == start)
				break;
			/* Otherwise restart to make sure all gone */
			index = start;
			continue;
		}
		if (index == start && indices[0] >= end) {
			/* All gone out of hole to be punched, we're done */
			pagevec_remove_exceptionals(&pvec);
			pagevec_release(&pvec);
			break;
		}
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			/* We rely upon deletion not changing page->index */
			index = indices[i];
			if (index >= end) {
				/* Restart punch to make sure all gone */
				index = start - 1;
				break;
			}

			if (radix_tree_exceptional_entry(page)) {
				truncate_exceptional_entry(mapping, index,
							   page);
				continue;
			}

			lock_page(page);
			WARN_ON(page_to_index(page) != index);
			wait_on_page_writeback(page);
			truncate_inode_page(mapping, page);
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		index++;
	}
	cleancache_invalidate_inode(mapping);
}
EXPORT_SYMBOL(truncate_inode_pages_range);

/**
 * truncate_inode_pages - truncate *all* the pages from an offset
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 *
 * Called under (and serialised by) inode->i_mutex.
 *
 * Note: When this function returns, there can be a page in the process of
 * deletion (inside __delete_from_page_cache()) in the specified range.  Thus
 * mapping->nrpages can be non-zero when this function returns even after
 * truncation of the whole mapping.
 */
void truncate_inode_pages(struct address_space *mapping, loff_t lstart)
{
	truncate_inode_pages_range(mapping, lstart, (loff_t)-1);
}
EXPORT_SYMBOL(truncate_inode_pages);

/**
 * truncate_inode_pages_final - truncate *all* pages before inode dies
 * @mapping: mapping to truncate
 *
 * Called under (and serialized by) inode->i_mutex.
 *
 * Filesystems have to use this in the .evict_inode path to inform the
 * VM that this is the final truncate and the inode is going away.
 */
void truncate_inode_pages_final(struct address_space *mapping)
{
	unsigned long nrexceptional;
	unsigned long nrpages;

	/*
	 * Page reclaim can not participate in regular inode lifetime
	 * management (can't call iput()) and thus can race with the
	 * inode teardown.  Tell it when the address space is exiting,
	 * so that it does not install eviction information after the
	 * final truncate has begun.
	 */
	mapping_set_exiting(mapping);

	/*
	 * When reclaim installs eviction entries, it increases
	 * nrexceptional first, then decreases nrpages.  Make sure we see
	 * this in the right order or we might miss an entry.
	 */
	nrpages = mapping->nrpages;
	smp_rmb();
	nrexceptional = mapping->nrexceptional;

	if (nrpages || nrexceptional) {
		/*
		 * As truncation uses a lockless tree lookup, cycle
		 * the tree lock to make sure any ongoing tree
		 * modification that does not see AS_EXITING is
		 * completed before starting the final truncate.
		 */
		spin_lock_irq(&mapping->tree_lock);
		spin_unlock_irq(&mapping->tree_lock);

		truncate_inode_pages(mapping, 0);
	}
}
EXPORT_SYMBOL(truncate_inode_pages_final);

/**
 * invalidate_mapping_pages - Invalidate all the unlocked pages of one inode
 * @mapping: the address_space which holds the pages to invalidate
 * @start: the offset 'from' which to invalidate
 * @end: the offset 'to' which to invalidate (inclusive)
 *
 * This function only removes the unlocked pages, if you want to
 * remove all the pages of one inode, you must call truncate_inode_pages.
 *
 * invalidate_mapping_pages() will not block on IO activity. It will not
 * invalidate pages which are dirty, locked, under writeback or mapped into
 * pagetables.
 */
unsigned long invalidate_mapping_pages(struct address_space *mapping,
		pgoff_t start, pgoff_t end)
{
	pgoff_t indices[PAGEVEC_SIZE];
	struct pagevec pvec;
	pgoff_t index = start;
	unsigned long ret;
	unsigned long count = 0;
	int i;

	pagevec_init(&pvec, 0);
	while (index <= end && pagevec_lookup_entries(&pvec, mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE - 1) + 1,
			indices)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			/* We rely upon deletion not changing page->index */
			index = indices[i];
			if (index > end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				invalidate_exceptional_entry(mapping, index,
							     page);
				continue;
			}

			if (!trylock_page(page))
				continue;

			WARN_ON(page_to_index(page) != index);

			/* Middle of THP: skip */
			if (PageTransTail(page)) {
				unlock_page(page);
				continue;
			} else if (PageTransHuge(page)) {
				index += HPAGE_PMD_NR - 1;
				i += HPAGE_PMD_NR - 1;
				/* 'end' is in the middle of THP */
				if (index ==  round_down(end, HPAGE_PMD_NR))
					continue;
			}

			ret = invalidate_inode_page(page);
			unlock_page(page);
			/*
			 * Invalidation is a hint that the page is no longer
			 * of interest and try to speed up its reclaim.
			 */
			if (!ret)
				deactivate_file_page(page);
			count += ret;
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		cond_resched();
		index++;
	}
	return count;
}
EXPORT_SYMBOL(invalidate_mapping_pages);

/*
 * This is like invalidate_complete_page(), except it ignores the page's
 * refcount.  We do this because invalidate_inode_pages2() needs stronger
 * invalidation guarantees, and cannot afford to leave pages behind because
 * shrink_page_list() has a temp ref on them, or because they're transiently
 * sitting in the lru_cache_add() pagevecs.
 */
static int
invalidate_complete_page2(struct address_space *mapping, struct page *page)
{
	unsigned long flags;

	if (page->mapping != mapping)
		return 0;

	if (page_has_private(page) && !try_to_release_page(page, GFP_KERNEL))
		return 0;

	spin_lock_irqsave(&mapping->tree_lock, flags);
	if (PageDirty(page))
		goto failed;

	BUG_ON(page_has_private(page));
	__delete_from_page_cache(page, NULL);
	spin_unlock_irqrestore(&mapping->tree_lock, flags);

	if (mapping->a_ops->freepage)
		mapping->a_ops->freepage(page);

	put_page(page);	/* pagecache ref */
	return 1;
failed:
	spin_unlock_irqrestore(&mapping->tree_lock, flags);
	return 0;
}

static int do_launder_page(struct address_space *mapping, struct page *page)
{
	if (!PageDirty(page))
		return 0;
	if (page->mapping != mapping || mapping->a_ops->launder_page == NULL)
		return 0;
	return mapping->a_ops->launder_page(page);
}

/**
 * invalidate_inode_pages2_range - remove range of pages from an address_space
 * @mapping: the address_space
 * @start: the page offset 'from' which to invalidate
 * @end: the page offset 'to' which to invalidate (inclusive)
 *
 * Any pages which are found to be mapped into pagetables are unmapped prior to
 * invalidation.
 *
 * Returns -EBUSY if any pages could not be invalidated.
 */
int invalidate_inode_pages2_range(struct address_space *mapping,
				  pgoff_t start, pgoff_t end)
{
	pgoff_t indices[PAGEVEC_SIZE];
	struct pagevec pvec;
	pgoff_t index;
	int i;
	int ret = 0;
	int ret2 = 0;
	int did_range_unmap = 0;

	cleancache_invalidate_inode(mapping);
	pagevec_init(&pvec, 0);
	index = start;
	while (index <= end && pagevec_lookup_entries(&pvec, mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE - 1) + 1,
			indices)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			/* We rely upon deletion not changing page->index */
			index = indices[i];
			if (index > end)
				break;

			if (radix_tree_exceptional_entry(page)) {
				if (!invalidate_exceptional_entry2(mapping,
								   index, page))
					ret = -EBUSY;
				continue;
			}

			lock_page(page);
			WARN_ON(page_to_index(page) != index);
			if (page->mapping != mapping) {
				unlock_page(page);
				continue;
			}
			wait_on_page_writeback(page);
			if (page_mapped(page)) {
				if (!did_range_unmap) {
					/*
					 * Zap the rest of the file in one hit.
					 */
					unmap_mapping_range(mapping,
					   (loff_t)index << PAGE_SHIFT,
					   (loff_t)(1 + end - index)
							 << PAGE_SHIFT,
							 0);
					did_range_unmap = 1;
				} else {
					/*
					 * Just zap this page
					 */
					unmap_mapping_range(mapping,
					   (loff_t)index << PAGE_SHIFT,
					   PAGE_SIZE, 0);
				}
			}
			BUG_ON(page_mapped(page));
			ret2 = do_launder_page(mapping, page);
			if (ret2 == 0) {
				if (!invalidate_complete_page2(mapping, page))
					ret2 = -EBUSY;
			}
			if (ret2 < 0)
				ret = ret2;
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		cond_resched();
		index++;
	}
	cleancache_invalidate_inode(mapping);
	return ret;
}
EXPORT_SYMBOL_GPL(invalidate_inode_pages2_range);

/**
 * invalidate_inode_pages2 - remove all pages from an address_space
 * @mapping: the address_space
 *
 * Any pages which are found to be mapped into pagetables are unmapped prior to
 * invalidation.
 *
 * Returns -EBUSY if any pages could not be invalidated.
 */
int invalidate_inode_pages2(struct address_space *mapping)
{
	return invalidate_inode_pages2_range(mapping, 0, -1);
}
EXPORT_SYMBOL_GPL(invalidate_inode_pages2);

/**
 * truncate_pagecache - unmap and remove pagecache that has been truncated
 * @inode: inode
 * @newsize: new file size
 *
 * inode's new i_size must already be written before truncate_pagecache
 * is called.
 *
 * This function should typically be called before the filesystem
 * releases resources associated with the freed range (eg. deallocates
 * blocks). This way, pagecache will always stay logically coherent
 * with on-disk format, and the filesystem would not have to deal with
 * situations such as writepage being called for a page that has already
 * had its underlying blocks deallocated.
 */
void truncate_pagecache(struct inode *inode, loff_t newsize)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t holebegin = round_up(newsize, PAGE_SIZE);

	/*
	 * unmap_mapping_range is called twice, first simply for
	 * efficiency so that truncate_inode_pages does fewer
	 * single-page unmaps.  However after this first call, and
	 * before truncate_inode_pages finishes, it is possible for
	 * private pages to be COWed, which remain after
	 * truncate_inode_pages finishes, hence the second
	 * unmap_mapping_range call must be made for correctness.
	 */
	unmap_mapping_range(mapping, holebegin, 0, 1);
	truncate_inode_pages(mapping, newsize);
	unmap_mapping_range(mapping, holebegin, 0, 1);
}
EXPORT_SYMBOL(truncate_pagecache);

/**
 * truncate_setsize - update inode and pagecache for a new file size
 * @inode: inode
 * @newsize: new file size
 *
 * truncate_setsize updates i_size and performs pagecache truncation (if
 * necessary) to @newsize. It will be typically be called from the filesystem's
 * setattr function when ATTR_SIZE is passed in.
 *
 * Must be called with a lock serializing truncates and writes (generally
 * i_mutex but e.g. xfs uses a different lock) and before all filesystem
 * specific block truncation has been performed.
 */
void truncate_setsize(struct inode *inode, loff_t newsize)
{
	loff_t oldsize = inode->i_size;

	i_size_write(inode, newsize);
	if (newsize > oldsize)
		pagecache_isize_extended(inode, oldsize, newsize);
	truncate_pagecache(inode, newsize);
}
EXPORT_SYMBOL(truncate_setsize);

/**
 * pagecache_isize_extended - update pagecache after extension of i_size
 * @inode:	inode for which i_size was extended
 * @from:	original inode size
 * @to:		new inode size
 *
 * Handle extension of inode size either caused by extending truncate or by
 * write starting after current i_size. We mark the page straddling current
 * i_size RO so that page_mkwrite() is called on the nearest write access to
 * the page.  This way filesystem can be sure that page_mkwrite() is called on
 * the page before user writes to the page via mmap after the i_size has been
 * changed.
 *
 * The function must be called after i_size is updated so that page fault
 * coming after we unlock the page will already see the new i_size.
 * The function must be called while we still hold i_mutex - this not only
 * makes sure i_size is stable but also that userspace cannot observe new
 * i_size value before we are prepared to store mmap writes at new inode size.
 */
void pagecache_isize_extended(struct inode *inode, loff_t from, loff_t to)
{
	int bsize = 1 << inode->i_blkbits;
	loff_t rounded_from;
	struct page *page;
	pgoff_t index;

	WARN_ON(to > inode->i_size);

	if (from >= to || bsize == PAGE_SIZE)
		return;
	/* Page straddling @from will not have any hole block created? */
	rounded_from = round_up(from, bsize);
	if (to <= rounded_from || !(rounded_from & (PAGE_SIZE - 1)))
		return;

	index = from >> PAGE_SHIFT;
	page = find_lock_page(inode->i_mapping, index);
	/* Page not cached? Nothing to do */
	if (!page)
		return;
	/*
	 * See clear_page_dirty_for_io() for details why set_page_dirty()
	 * is needed.
	 */
	if (page_mkclean(page))
		set_page_dirty(page);
	unlock_page(page);
	put_page(page);
}
EXPORT_SYMBOL(pagecache_isize_extended);

/**
 * truncate_pagecache_range - unmap and remove pagecache that is hole-punched
 * @inode: inode
 * @lstart: offset of beginning of hole
 * @lend: offset of last byte of hole
 *
 * This function should typically be called before the filesystem
 * releases resources associated with the freed range (eg. deallocates
 * blocks). This way, pagecache will always stay logically coherent
 * with on-disk format, and the filesystem would not have to deal with
 * situations such as writepage being called for a page that has already
 * had its underlying blocks deallocated.
 */
void truncate_pagecache_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t unmap_start = round_up(lstart, PAGE_SIZE);
	loff_t unmap_end = round_down(1 + lend, PAGE_SIZE) - 1;
	/*
	 * This rounding is currently just for example: unmap_mapping_range
	 * expands its hole outwards, whereas we want it to contract the hole
	 * inwards.  However, existing callers of truncate_pagecache_range are
	 * doing their own page rounding first.  Note that unmap_mapping_range
	 * allows holelen 0 for all, and we allow lend -1 for end of file.
	 */

	/*
	 * Unlike in truncate_pagecache, unmap_mapping_range is called only
	 * once (before truncating pagecache), and without "even_cows" flag:
	 * hole-punching should not remove private COWed pages from the hole.
	 */
	if ((u64)unmap_end > (u64)unmap_start)
		unmap_mapping_range(mapping, unmap_start,
				    1 + unmap_end - unmap_start, 0);
	truncate_inode_pages_range(mapping, lstart, lend);
}
EXPORT_SYMBOL(truncate_pagecache_range);
