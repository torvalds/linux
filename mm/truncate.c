/*
 * mm/truncate.c - code for taking down pages from address_spaces
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 10Sep2002	akpm@zip.com.au
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/buffer_head.h>	/* grr. try_to_release_page,
				   do_invalidatepage */


/**
 * do_invalidatepage - invalidate part of all of a page
 * @page: the page which is affected
 * @offset: the index of the truncation point
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
void do_invalidatepage(struct page *page, unsigned long offset)
{
	void (*invalidatepage)(struct page *, unsigned long);
	invalidatepage = page->mapping->a_ops->invalidatepage;
#ifdef CONFIG_BLOCK
	if (!invalidatepage)
		invalidatepage = block_invalidatepage;
#endif
	if (invalidatepage)
		(*invalidatepage)(page, offset);
}

static inline void truncate_partial_page(struct page *page, unsigned partial)
{
	memclear_highpage_flush(page, partial, PAGE_CACHE_SIZE-partial);
	if (PagePrivate(page))
		do_invalidatepage(page, partial);
}

/*
 * If truncate cannot remove the fs-private metadata from the page, the page
 * becomes anonymous.  It will be left on the LRU and may even be mapped into
 * user pagetables if we're racing with filemap_nopage().
 *
 * We need to bale out if page->mapping is no longer equal to the original
 * mapping.  This happens a) when the VM reclaimed the page while we waited on
 * its lock, b) when a concurrent invalidate_inode_pages got there first and
 * c) when tmpfs swizzles a page between a tmpfs inode and swapper_space.
 */
static void
truncate_complete_page(struct address_space *mapping, struct page *page)
{
	if (page->mapping != mapping)
		return;

	if (PagePrivate(page))
		do_invalidatepage(page, 0);

	if (test_clear_page_dirty(page))
		task_io_account_cancelled_write(PAGE_CACHE_SIZE);
	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);
	remove_from_page_cache(page);
	page_cache_release(page);	/* pagecache ref */
}

/*
 * This is for invalidate_inode_pages().  That function can be called at
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

	if (PagePrivate(page) && !try_to_release_page(page, 0))
		return 0;

	ret = remove_mapping(mapping, page);

	return ret;
}

/**
 * truncate_inode_pages - truncate range of pages specified by start and
 * end byte offsets
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 * @lend: offset to which to truncate
 *
 * Truncate the page cache, removing the pages that are between
 * specified offsets (and zeroing out partial page
 * (if lstart is not page aligned)).
 *
 * Truncate takes two passes - the first pass is nonblocking.  It will not
 * block on page locks and it will not block on writeback.  The second pass
 * will wait.  This is to prevent as much IO as possible in the affected region.
 * The first pass will remove most pages, so the search cost of the second pass
 * is low.
 *
 * When looking at page->index outside the page lock we need to be careful to
 * copy it into a local to avoid races (it could change at any time).
 *
 * We pass down the cache-hot hint to the page freeing code.  Even if the
 * mapping is large, it is probably the case that the final pages are the most
 * recently touched, and freeing happens in ascending file offset order.
 */
void truncate_inode_pages_range(struct address_space *mapping,
				loff_t lstart, loff_t lend)
{
	const pgoff_t start = (lstart + PAGE_CACHE_SIZE-1) >> PAGE_CACHE_SHIFT;
	pgoff_t end;
	const unsigned partial = lstart & (PAGE_CACHE_SIZE - 1);
	struct pagevec pvec;
	pgoff_t next;
	int i;

	if (mapping->nrpages == 0)
		return;

	BUG_ON((lend & (PAGE_CACHE_SIZE - 1)) != (PAGE_CACHE_SIZE - 1));
	end = (lend >> PAGE_CACHE_SHIFT);

	pagevec_init(&pvec, 0);
	next = start;
	while (next <= end &&
	       pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			pgoff_t page_index = page->index;

			if (page_index > end) {
				next = page_index;
				break;
			}

			if (page_index > next)
				next = page_index;
			next++;
			if (TestSetPageLocked(page))
				continue;
			if (PageWriteback(page)) {
				unlock_page(page);
				continue;
			}
			truncate_complete_page(mapping, page);
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (partial) {
		struct page *page = find_lock_page(mapping, start - 1);
		if (page) {
			wait_on_page_writeback(page);
			truncate_partial_page(page, partial);
			unlock_page(page);
			page_cache_release(page);
		}
	}

	next = start;
	for ( ; ; ) {
		cond_resched();
		if (!pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
			if (next == start)
				break;
			next = start;
			continue;
		}
		if (pvec.pages[0]->index > end) {
			pagevec_release(&pvec);
			break;
		}
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			if (page->index > end)
				break;
			lock_page(page);
			wait_on_page_writeback(page);
			if (page->index > next)
				next = page->index;
			next++;
			truncate_complete_page(mapping, page);
			unlock_page(page);
		}
		pagevec_release(&pvec);
	}
}
EXPORT_SYMBOL(truncate_inode_pages_range);

/**
 * truncate_inode_pages - truncate *all* the pages from an offset
 * @mapping: mapping to truncate
 * @lstart: offset from which to truncate
 *
 * Called under (and serialised by) inode->i_mutex.
 */
void truncate_inode_pages(struct address_space *mapping, loff_t lstart)
{
	truncate_inode_pages_range(mapping, lstart, (loff_t)-1);
}
EXPORT_SYMBOL(truncate_inode_pages);

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
	struct pagevec pvec;
	pgoff_t next = start;
	unsigned long ret = 0;
	int i;

	pagevec_init(&pvec, 0);
	while (next <= end &&
			pagevec_lookup(&pvec, mapping, next, PAGEVEC_SIZE)) {
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			pgoff_t index;
			int lock_failed;

			lock_failed = TestSetPageLocked(page);

			/*
			 * We really shouldn't be looking at the ->index of an
			 * unlocked page.  But we're not allowed to lock these
			 * pages.  So we rely upon nobody altering the ->index
			 * of this (pinned-by-us) page.
			 */
			index = page->index;
			if (index > next)
				next = index;
			next++;
			if (lock_failed)
				continue;

			if (PageDirty(page) || PageWriteback(page))
				goto unlock;
			if (page_mapped(page))
				goto unlock;
			ret += invalidate_complete_page(mapping, page);
unlock:
			unlock_page(page);
			if (next > end)
				break;
		}
		pagevec_release(&pvec);
	}
	return ret;
}

unsigned long invalidate_inode_pages(struct address_space *mapping)
{
	return invalidate_mapping_pages(mapping, 0, ~0UL);
}
EXPORT_SYMBOL(invalidate_inode_pages);

/*
 * This is like invalidate_complete_page(), except it ignores the page's
 * refcount.  We do this because invalidate_inode_pages2() needs stronger
 * invalidation guarantees, and cannot afford to leave pages behind because
 * shrink_list() has a temp ref on them, or because they're transiently sitting
 * in the lru_cache_add() pagevecs.
 */
static int
invalidate_complete_page2(struct address_space *mapping, struct page *page)
{
	if (page->mapping != mapping)
		return 0;

	if (PagePrivate(page) && !try_to_release_page(page, GFP_KERNEL))
		return 0;

	write_lock_irq(&mapping->tree_lock);
	if (PageDirty(page))
		goto failed;

	BUG_ON(PagePrivate(page));
	__remove_from_page_cache(page);
	write_unlock_irq(&mapping->tree_lock);
	ClearPageUptodate(page);
	page_cache_release(page);	/* pagecache ref */
	return 1;
failed:
	write_unlock_irq(&mapping->tree_lock);
	return 0;
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
 * Returns -EIO if any pages could not be invalidated.
 */
int invalidate_inode_pages2_range(struct address_space *mapping,
				  pgoff_t start, pgoff_t end)
{
	struct pagevec pvec;
	pgoff_t next;
	int i;
	int ret = 0;
	int did_range_unmap = 0;
	int wrapped = 0;

	pagevec_init(&pvec, 0);
	next = start;
	while (next <= end && !ret && !wrapped &&
		pagevec_lookup(&pvec, mapping, next,
			min(end - next, (pgoff_t)PAGEVEC_SIZE - 1) + 1)) {
		for (i = 0; !ret && i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			pgoff_t page_index;
			int was_dirty;

			lock_page(page);
			if (page->mapping != mapping) {
				unlock_page(page);
				continue;
			}
			page_index = page->index;
			next = page_index + 1;
			if (next == 0)
				wrapped = 1;
			if (page_index > end) {
				unlock_page(page);
				break;
			}
			wait_on_page_writeback(page);
			while (page_mapped(page)) {
				if (!did_range_unmap) {
					/*
					 * Zap the rest of the file in one hit.
					 */
					unmap_mapping_range(mapping,
					   (loff_t)page_index<<PAGE_CACHE_SHIFT,
					   (loff_t)(end - page_index + 1)
							<< PAGE_CACHE_SHIFT,
					    0);
					did_range_unmap = 1;
				} else {
					/*
					 * Just zap this page
					 */
					unmap_mapping_range(mapping,
					  (loff_t)page_index<<PAGE_CACHE_SHIFT,
					  PAGE_CACHE_SIZE, 0);
				}
			}
			was_dirty = test_clear_page_dirty(page);
			if (!invalidate_complete_page2(mapping, page)) {
				if (was_dirty)
					set_page_dirty(page);
				ret = -EIO;
			}
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
	WARN_ON_ONCE(ret);
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
 * Returns -EIO if any pages could not be invalidated.
 */
int invalidate_inode_pages2(struct address_space *mapping)
{
	return invalidate_inode_pages2_range(mapping, 0, -1);
}
EXPORT_SYMBOL_GPL(invalidate_inode_pages2);
