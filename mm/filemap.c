// SPDX-License-Identifier: GPL-2.0-only
/*
 *	linux/mm/filemap.c
 *
 * Copyright (C) 1994-1999  Linus Torvalds
 */

/*
 * This file handles the generic file mmap semantics used by
 * most "normal" filesystems (but you don't /have/ to use this:
 * the NFS filesystem used to do this differently, for example)
 */
#include <linux/export.h>
#include <linux/compiler.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <linux/capability.h>
#include <linux/kernel_stat.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/error-injection.h>
#include <linux/hash.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/security.h>
#include <linux/cpuset.h>
#include <linux/hugetlb.h>
#include <linux/memcontrol.h>
#include <linux/cleancache.h>
#include <linux/shmem_fs.h>
#include <linux/rmap.h>
#include <linux/delayacct.h>
#include <linux/psi.h>
#include <linux/ramfs.h>
#include <linux/page_idle.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/filemap.h>

/*
 * FIXME: remove all knowledge of the buffer layer from the core VM
 */
#include <linux/buffer_head.h> /* for try_to_free_buffers */

#include <asm/mman.h>

/*
 * Shared mappings implemented 30.11.1994. It's not fully working yet,
 * though.
 *
 * Shared mappings now work. 15.8.1995  Bruno.
 *
 * finished 'unifying' the page and buffer cache and SMP-threaded the
 * page-cache, 21.05.1999, Ingo Molnar <mingo@redhat.com>
 *
 * SMP-threaded pagemap-LRU 1999, Andrea Arcangeli <andrea@suse.de>
 */

/*
 * Lock ordering:
 *
 *  ->i_mmap_rwsem		(truncate_pagecache)
 *    ->private_lock		(__free_pte->__set_page_dirty_buffers)
 *      ->swap_lock		(exclusive_swap_page, others)
 *        ->i_pages lock
 *
 *  ->i_rwsem
 *    ->invalidate_lock		(acquired by fs in truncate path)
 *      ->i_mmap_rwsem		(truncate->unmap_mapping_range)
 *
 *  ->mmap_lock
 *    ->i_mmap_rwsem
 *      ->page_table_lock or pte_lock	(various, mainly in memory.c)
 *        ->i_pages lock	(arch-dependent flush_dcache_mmap_lock)
 *
 *  ->mmap_lock
 *    ->invalidate_lock		(filemap_fault)
 *      ->lock_page		(filemap_fault, access_process_vm)
 *
 *  ->i_rwsem			(generic_perform_write)
 *    ->mmap_lock		(fault_in_readable->do_page_fault)
 *
 *  bdi->wb.list_lock
 *    sb_lock			(fs/fs-writeback.c)
 *    ->i_pages lock		(__sync_single_inode)
 *
 *  ->i_mmap_rwsem
 *    ->anon_vma.lock		(vma_adjust)
 *
 *  ->anon_vma.lock
 *    ->page_table_lock or pte_lock	(anon_vma_prepare and various)
 *
 *  ->page_table_lock or pte_lock
 *    ->swap_lock		(try_to_unmap_one)
 *    ->private_lock		(try_to_unmap_one)
 *    ->i_pages lock		(try_to_unmap_one)
 *    ->lruvec->lru_lock	(follow_page->mark_page_accessed)
 *    ->lruvec->lru_lock	(check_pte_range->isolate_lru_page)
 *    ->private_lock		(page_remove_rmap->set_page_dirty)
 *    ->i_pages lock		(page_remove_rmap->set_page_dirty)
 *    bdi.wb->list_lock		(page_remove_rmap->set_page_dirty)
 *    ->inode->i_lock		(page_remove_rmap->set_page_dirty)
 *    ->memcg->move_lock	(page_remove_rmap->lock_page_memcg)
 *    bdi.wb->list_lock		(zap_pte_range->set_page_dirty)
 *    ->inode->i_lock		(zap_pte_range->set_page_dirty)
 *    ->private_lock		(zap_pte_range->__set_page_dirty_buffers)
 *
 * ->i_mmap_rwsem
 *   ->tasklist_lock            (memory_failure, collect_procs_ao)
 */

static void page_cache_delete(struct address_space *mapping,
				   struct page *page, void *shadow)
{
	XA_STATE(xas, &mapping->i_pages, page->index);
	unsigned int nr = 1;

	mapping_set_update(&xas, mapping);

	/* hugetlb pages are represented by a single entry in the xarray */
	if (!PageHuge(page)) {
		xas_set_order(&xas, page->index, compound_order(page));
		nr = compound_nr(page);
	}

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(PageTail(page), page);
	VM_BUG_ON_PAGE(nr != 1 && shadow, page);

	xas_store(&xas, shadow);
	xas_init_marks(&xas);

	page->mapping = NULL;
	/* Leave page->index set: truncation lookup relies upon it */
	mapping->nrpages -= nr;
}

static void unaccount_page_cache_page(struct address_space *mapping,
				      struct page *page)
{
	int nr;

	/*
	 * if we're uptodate, flush out into the cleancache, otherwise
	 * invalidate any existing cleancache entries.  We can't leave
	 * stale data around in the cleancache once our page is gone
	 */
	if (PageUptodate(page) && PageMappedToDisk(page))
		cleancache_put_page(page);
	else
		cleancache_invalidate_page(mapping, page);

	VM_BUG_ON_PAGE(PageTail(page), page);
	VM_BUG_ON_PAGE(page_mapped(page), page);
	if (!IS_ENABLED(CONFIG_DEBUG_VM) && unlikely(page_mapped(page))) {
		int mapcount;

		pr_alert("BUG: Bad page cache in process %s  pfn:%05lx\n",
			 current->comm, page_to_pfn(page));
		dump_page(page, "still mapped when deleted");
		dump_stack();
		add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);

		mapcount = page_mapcount(page);
		if (mapping_exiting(mapping) &&
		    page_count(page) >= mapcount + 2) {
			/*
			 * All vmas have already been torn down, so it's
			 * a good bet that actually the page is unmapped,
			 * and we'd prefer not to leak it: if we're wrong,
			 * some other bad page check should catch it later.
			 */
			page_mapcount_reset(page);
			page_ref_sub(page, mapcount);
		}
	}

	/* hugetlb pages do not participate in page cache accounting. */
	if (PageHuge(page))
		return;

	nr = thp_nr_pages(page);

	__mod_lruvec_page_state(page, NR_FILE_PAGES, -nr);
	if (PageSwapBacked(page)) {
		__mod_lruvec_page_state(page, NR_SHMEM, -nr);
		if (PageTransHuge(page))
			__mod_lruvec_page_state(page, NR_SHMEM_THPS, -nr);
	} else if (PageTransHuge(page)) {
		__mod_lruvec_page_state(page, NR_FILE_THPS, -nr);
		filemap_nr_thps_dec(mapping);
	}

	/*
	 * At this point page must be either written or cleaned by
	 * truncate.  Dirty page here signals a bug and loss of
	 * unwritten data.
	 *
	 * This fixes dirty accounting after removing the page entirely
	 * but leaves PageDirty set: it has no effect for truncated
	 * page and anyway will be cleared before returning page into
	 * buddy allocator.
	 */
	if (WARN_ON_ONCE(PageDirty(page)))
		account_page_cleaned(page, mapping, inode_to_wb(mapping->host));
}

/*
 * Delete a page from the page cache and free it. Caller has to make
 * sure the page is locked and that nobody else uses it - or that usage
 * is safe.  The caller must hold the i_pages lock.
 */
void __delete_from_page_cache(struct page *page, void *shadow)
{
	struct address_space *mapping = page->mapping;

	trace_mm_filemap_delete_from_page_cache(page);

	unaccount_page_cache_page(mapping, page);
	page_cache_delete(mapping, page, shadow);
}

static void page_cache_free_page(struct address_space *mapping,
				struct page *page)
{
	void (*freepage)(struct page *);

	freepage = mapping->a_ops->freepage;
	if (freepage)
		freepage(page);

	if (PageTransHuge(page) && !PageHuge(page)) {
		page_ref_sub(page, thp_nr_pages(page));
		VM_BUG_ON_PAGE(page_count(page) <= 0, page);
	} else {
		put_page(page);
	}
}

/**
 * delete_from_page_cache - delete page from page cache
 * @page: the page which the kernel is trying to remove from page cache
 *
 * This must be called only on pages that have been verified to be in the page
 * cache and locked.  It will never put the page into the free list, the caller
 * has a reference on the page.
 */
void delete_from_page_cache(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	BUG_ON(!PageLocked(page));
	xa_lock_irq(&mapping->i_pages);
	__delete_from_page_cache(page, NULL);
	xa_unlock_irq(&mapping->i_pages);

	page_cache_free_page(mapping, page);
}
EXPORT_SYMBOL(delete_from_page_cache);

/*
 * page_cache_delete_batch - delete several pages from page cache
 * @mapping: the mapping to which pages belong
 * @pvec: pagevec with pages to delete
 *
 * The function walks over mapping->i_pages and removes pages passed in @pvec
 * from the mapping. The function expects @pvec to be sorted by page index
 * and is optimised for it to be dense.
 * It tolerates holes in @pvec (mapping entries at those indices are not
 * modified). The function expects only THP head pages to be present in the
 * @pvec.
 *
 * The function expects the i_pages lock to be held.
 */
static void page_cache_delete_batch(struct address_space *mapping,
			     struct pagevec *pvec)
{
	XA_STATE(xas, &mapping->i_pages, pvec->pages[0]->index);
	int total_pages = 0;
	int i = 0;
	struct page *page;

	mapping_set_update(&xas, mapping);
	xas_for_each(&xas, page, ULONG_MAX) {
		if (i >= pagevec_count(pvec))
			break;

		/* A swap/dax/shadow entry got inserted? Skip it. */
		if (xa_is_value(page))
			continue;
		/*
		 * A page got inserted in our range? Skip it. We have our
		 * pages locked so they are protected from being removed.
		 * If we see a page whose index is higher than ours, it
		 * means our page has been removed, which shouldn't be
		 * possible because we're holding the PageLock.
		 */
		if (page != pvec->pages[i]) {
			VM_BUG_ON_PAGE(page->index > pvec->pages[i]->index,
					page);
			continue;
		}

		WARN_ON_ONCE(!PageLocked(page));

		if (page->index == xas.xa_index)
			page->mapping = NULL;
		/* Leave page->index set: truncation lookup relies on it */

		/*
		 * Move to the next page in the vector if this is a regular
		 * page or the index is of the last sub-page of this compound
		 * page.
		 */
		if (page->index + compound_nr(page) - 1 == xas.xa_index)
			i++;
		xas_store(&xas, NULL);
		total_pages++;
	}
	mapping->nrpages -= total_pages;
}

void delete_from_page_cache_batch(struct address_space *mapping,
				  struct pagevec *pvec)
{
	int i;

	if (!pagevec_count(pvec))
		return;

	xa_lock_irq(&mapping->i_pages);
	for (i = 0; i < pagevec_count(pvec); i++) {
		trace_mm_filemap_delete_from_page_cache(pvec->pages[i]);

		unaccount_page_cache_page(mapping, pvec->pages[i]);
	}
	page_cache_delete_batch(mapping, pvec);
	xa_unlock_irq(&mapping->i_pages);

	for (i = 0; i < pagevec_count(pvec); i++)
		page_cache_free_page(mapping, pvec->pages[i]);
}

int filemap_check_errors(struct address_space *mapping)
{
	int ret = 0;
	/* Check for outstanding write errors */
	if (test_bit(AS_ENOSPC, &mapping->flags) &&
	    test_and_clear_bit(AS_ENOSPC, &mapping->flags))
		ret = -ENOSPC;
	if (test_bit(AS_EIO, &mapping->flags) &&
	    test_and_clear_bit(AS_EIO, &mapping->flags))
		ret = -EIO;
	return ret;
}
EXPORT_SYMBOL(filemap_check_errors);

static int filemap_check_and_keep_errors(struct address_space *mapping)
{
	/* Check for outstanding write errors */
	if (test_bit(AS_EIO, &mapping->flags))
		return -EIO;
	if (test_bit(AS_ENOSPC, &mapping->flags))
		return -ENOSPC;
	return 0;
}

/**
 * filemap_fdatawrite_wbc - start writeback on mapping dirty pages in range
 * @mapping:	address space structure to write
 * @wbc:	the writeback_control controlling the writeout
 *
 * Call writepages on the mapping using the provided wbc to control the
 * writeout.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int filemap_fdatawrite_wbc(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	int ret;

	if (!mapping_can_writeback(mapping) ||
	    !mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		return 0;

	wbc_attach_fdatawrite_inode(wbc, mapping->host);
	ret = do_writepages(mapping, wbc);
	wbc_detach_inode(wbc);
	return ret;
}
EXPORT_SYMBOL(filemap_fdatawrite_wbc);

/**
 * __filemap_fdatawrite_range - start writeback on mapping dirty pages in range
 * @mapping:	address space structure to write
 * @start:	offset in bytes where the range starts
 * @end:	offset in bytes where the range ends (inclusive)
 * @sync_mode:	enable synchronous operation
 *
 * Start writeback against all of a mapping's dirty pages that lie
 * within the byte offsets <start, end> inclusive.
 *
 * If sync_mode is WB_SYNC_ALL then this is a "data integrity" operation, as
 * opposed to a regular memory cleansing writeback.  The difference between
 * these two operations is that if a dirty page/buffer is encountered, it must
 * be waited upon, and not just skipped over.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int __filemap_fdatawrite_range(struct address_space *mapping, loff_t start,
				loff_t end, int sync_mode)
{
	struct writeback_control wbc = {
		.sync_mode = sync_mode,
		.nr_to_write = LONG_MAX,
		.range_start = start,
		.range_end = end,
	};

	return filemap_fdatawrite_wbc(mapping, &wbc);
}

static inline int __filemap_fdatawrite(struct address_space *mapping,
	int sync_mode)
{
	return __filemap_fdatawrite_range(mapping, 0, LLONG_MAX, sync_mode);
}

int filemap_fdatawrite(struct address_space *mapping)
{
	return __filemap_fdatawrite(mapping, WB_SYNC_ALL);
}
EXPORT_SYMBOL(filemap_fdatawrite);

int filemap_fdatawrite_range(struct address_space *mapping, loff_t start,
				loff_t end)
{
	return __filemap_fdatawrite_range(mapping, start, end, WB_SYNC_ALL);
}
EXPORT_SYMBOL(filemap_fdatawrite_range);

/**
 * filemap_flush - mostly a non-blocking flush
 * @mapping:	target address_space
 *
 * This is a mostly non-blocking flush.  Not suitable for data-integrity
 * purposes - I/O may not be started against all dirty pages.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int filemap_flush(struct address_space *mapping)
{
	return __filemap_fdatawrite(mapping, WB_SYNC_NONE);
}
EXPORT_SYMBOL(filemap_flush);

/**
 * filemap_range_has_page - check if a page exists in range.
 * @mapping:           address space within which to check
 * @start_byte:        offset in bytes where the range starts
 * @end_byte:          offset in bytes where the range ends (inclusive)
 *
 * Find at least one page in the range supplied, usually used to check if
 * direct writing in this range will trigger a writeback.
 *
 * Return: %true if at least one page exists in the specified range,
 * %false otherwise.
 */
bool filemap_range_has_page(struct address_space *mapping,
			   loff_t start_byte, loff_t end_byte)
{
	struct page *page;
	XA_STATE(xas, &mapping->i_pages, start_byte >> PAGE_SHIFT);
	pgoff_t max = end_byte >> PAGE_SHIFT;

	if (end_byte < start_byte)
		return false;

	rcu_read_lock();
	for (;;) {
		page = xas_find(&xas, max);
		if (xas_retry(&xas, page))
			continue;
		/* Shadow entries don't count */
		if (xa_is_value(page))
			continue;
		/*
		 * We don't need to try to pin this page; we're about to
		 * release the RCU lock anyway.  It is enough to know that
		 * there was a page here recently.
		 */
		break;
	}
	rcu_read_unlock();

	return page != NULL;
}
EXPORT_SYMBOL(filemap_range_has_page);

static void __filemap_fdatawait_range(struct address_space *mapping,
				     loff_t start_byte, loff_t end_byte)
{
	pgoff_t index = start_byte >> PAGE_SHIFT;
	pgoff_t end = end_byte >> PAGE_SHIFT;
	struct pagevec pvec;
	int nr_pages;

	if (end_byte < start_byte)
		return;

	pagevec_init(&pvec);
	while (index <= end) {
		unsigned i;

		nr_pages = pagevec_lookup_range_tag(&pvec, mapping, &index,
				end, PAGECACHE_TAG_WRITEBACK);
		if (!nr_pages)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			wait_on_page_writeback(page);
			ClearPageError(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	}
}

/**
 * filemap_fdatawait_range - wait for writeback to complete
 * @mapping:		address space structure to wait for
 * @start_byte:		offset in bytes where the range starts
 * @end_byte:		offset in bytes where the range ends (inclusive)
 *
 * Walk the list of under-writeback pages of the given address space
 * in the given range and wait for all of them.  Check error status of
 * the address space and return it.
 *
 * Since the error status of the address space is cleared by this function,
 * callers are responsible for checking the return value and handling and/or
 * reporting the error.
 *
 * Return: error status of the address space.
 */
int filemap_fdatawait_range(struct address_space *mapping, loff_t start_byte,
			    loff_t end_byte)
{
	__filemap_fdatawait_range(mapping, start_byte, end_byte);
	return filemap_check_errors(mapping);
}
EXPORT_SYMBOL(filemap_fdatawait_range);

/**
 * filemap_fdatawait_range_keep_errors - wait for writeback to complete
 * @mapping:		address space structure to wait for
 * @start_byte:		offset in bytes where the range starts
 * @end_byte:		offset in bytes where the range ends (inclusive)
 *
 * Walk the list of under-writeback pages of the given address space in the
 * given range and wait for all of them.  Unlike filemap_fdatawait_range(),
 * this function does not clear error status of the address space.
 *
 * Use this function if callers don't handle errors themselves.  Expected
 * call sites are system-wide / filesystem-wide data flushers: e.g. sync(2),
 * fsfreeze(8)
 */
int filemap_fdatawait_range_keep_errors(struct address_space *mapping,
		loff_t start_byte, loff_t end_byte)
{
	__filemap_fdatawait_range(mapping, start_byte, end_byte);
	return filemap_check_and_keep_errors(mapping);
}
EXPORT_SYMBOL(filemap_fdatawait_range_keep_errors);

/**
 * file_fdatawait_range - wait for writeback to complete
 * @file:		file pointing to address space structure to wait for
 * @start_byte:		offset in bytes where the range starts
 * @end_byte:		offset in bytes where the range ends (inclusive)
 *
 * Walk the list of under-writeback pages of the address space that file
 * refers to, in the given range and wait for all of them.  Check error
 * status of the address space vs. the file->f_wb_err cursor and return it.
 *
 * Since the error status of the file is advanced by this function,
 * callers are responsible for checking the return value and handling and/or
 * reporting the error.
 *
 * Return: error status of the address space vs. the file->f_wb_err cursor.
 */
int file_fdatawait_range(struct file *file, loff_t start_byte, loff_t end_byte)
{
	struct address_space *mapping = file->f_mapping;

	__filemap_fdatawait_range(mapping, start_byte, end_byte);
	return file_check_and_advance_wb_err(file);
}
EXPORT_SYMBOL(file_fdatawait_range);

/**
 * filemap_fdatawait_keep_errors - wait for writeback without clearing errors
 * @mapping: address space structure to wait for
 *
 * Walk the list of under-writeback pages of the given address space
 * and wait for all of them.  Unlike filemap_fdatawait(), this function
 * does not clear error status of the address space.
 *
 * Use this function if callers don't handle errors themselves.  Expected
 * call sites are system-wide / filesystem-wide data flushers: e.g. sync(2),
 * fsfreeze(8)
 *
 * Return: error status of the address space.
 */
int filemap_fdatawait_keep_errors(struct address_space *mapping)
{
	__filemap_fdatawait_range(mapping, 0, LLONG_MAX);
	return filemap_check_and_keep_errors(mapping);
}
EXPORT_SYMBOL(filemap_fdatawait_keep_errors);

/* Returns true if writeback might be needed or already in progress. */
static bool mapping_needs_writeback(struct address_space *mapping)
{
	return mapping->nrpages;
}

/**
 * filemap_range_needs_writeback - check if range potentially needs writeback
 * @mapping:           address space within which to check
 * @start_byte:        offset in bytes where the range starts
 * @end_byte:          offset in bytes where the range ends (inclusive)
 *
 * Find at least one page in the range supplied, usually used to check if
 * direct writing in this range will trigger a writeback. Used by O_DIRECT
 * read/write with IOCB_NOWAIT, to see if the caller needs to do
 * filemap_write_and_wait_range() before proceeding.
 *
 * Return: %true if the caller should do filemap_write_and_wait_range() before
 * doing O_DIRECT to a page in this range, %false otherwise.
 */
bool filemap_range_needs_writeback(struct address_space *mapping,
				   loff_t start_byte, loff_t end_byte)
{
	XA_STATE(xas, &mapping->i_pages, start_byte >> PAGE_SHIFT);
	pgoff_t max = end_byte >> PAGE_SHIFT;
	struct page *page;

	if (!mapping_needs_writeback(mapping))
		return false;
	if (!mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) &&
	    !mapping_tagged(mapping, PAGECACHE_TAG_WRITEBACK))
		return false;
	if (end_byte < start_byte)
		return false;

	rcu_read_lock();
	xas_for_each(&xas, page, max) {
		if (xas_retry(&xas, page))
			continue;
		if (xa_is_value(page))
			continue;
		if (PageDirty(page) || PageLocked(page) || PageWriteback(page))
			break;
	}
	rcu_read_unlock();
	return page != NULL;
}
EXPORT_SYMBOL_GPL(filemap_range_needs_writeback);

/**
 * filemap_write_and_wait_range - write out & wait on a file range
 * @mapping:	the address_space for the pages
 * @lstart:	offset in bytes where the range starts
 * @lend:	offset in bytes where the range ends (inclusive)
 *
 * Write out and wait upon file offsets lstart->lend, inclusive.
 *
 * Note that @lend is inclusive (describes the last byte to be written) so
 * that this function can be used to write to the very end-of-file (end = -1).
 *
 * Return: error status of the address space.
 */
int filemap_write_and_wait_range(struct address_space *mapping,
				 loff_t lstart, loff_t lend)
{
	int err = 0;

	if (mapping_needs_writeback(mapping)) {
		err = __filemap_fdatawrite_range(mapping, lstart, lend,
						 WB_SYNC_ALL);
		/*
		 * Even if the above returned error, the pages may be
		 * written partially (e.g. -ENOSPC), so we wait for it.
		 * But the -EIO is special case, it may indicate the worst
		 * thing (e.g. bug) happened, so we avoid waiting for it.
		 */
		if (err != -EIO) {
			int err2 = filemap_fdatawait_range(mapping,
						lstart, lend);
			if (!err)
				err = err2;
		} else {
			/* Clear any previously stored errors */
			filemap_check_errors(mapping);
		}
	} else {
		err = filemap_check_errors(mapping);
	}
	return err;
}
EXPORT_SYMBOL(filemap_write_and_wait_range);

void __filemap_set_wb_err(struct address_space *mapping, int err)
{
	errseq_t eseq = errseq_set(&mapping->wb_err, err);

	trace_filemap_set_wb_err(mapping, eseq);
}
EXPORT_SYMBOL(__filemap_set_wb_err);

/**
 * file_check_and_advance_wb_err - report wb error (if any) that was previously
 * 				   and advance wb_err to current one
 * @file: struct file on which the error is being reported
 *
 * When userland calls fsync (or something like nfsd does the equivalent), we
 * want to report any writeback errors that occurred since the last fsync (or
 * since the file was opened if there haven't been any).
 *
 * Grab the wb_err from the mapping. If it matches what we have in the file,
 * then just quickly return 0. The file is all caught up.
 *
 * If it doesn't match, then take the mapping value, set the "seen" flag in
 * it and try to swap it into place. If it works, or another task beat us
 * to it with the new value, then update the f_wb_err and return the error
 * portion. The error at this point must be reported via proper channels
 * (a'la fsync, or NFS COMMIT operation, etc.).
 *
 * While we handle mapping->wb_err with atomic operations, the f_wb_err
 * value is protected by the f_lock since we must ensure that it reflects
 * the latest value swapped in for this file descriptor.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int file_check_and_advance_wb_err(struct file *file)
{
	int err = 0;
	errseq_t old = READ_ONCE(file->f_wb_err);
	struct address_space *mapping = file->f_mapping;

	/* Locklessly handle the common case where nothing has changed */
	if (errseq_check(&mapping->wb_err, old)) {
		/* Something changed, must use slow path */
		spin_lock(&file->f_lock);
		old = file->f_wb_err;
		err = errseq_check_and_advance(&mapping->wb_err,
						&file->f_wb_err);
		trace_file_check_and_advance_wb_err(file, old);
		spin_unlock(&file->f_lock);
	}

	/*
	 * We're mostly using this function as a drop in replacement for
	 * filemap_check_errors. Clear AS_EIO/AS_ENOSPC to emulate the effect
	 * that the legacy code would have had on these flags.
	 */
	clear_bit(AS_EIO, &mapping->flags);
	clear_bit(AS_ENOSPC, &mapping->flags);
	return err;
}
EXPORT_SYMBOL(file_check_and_advance_wb_err);

/**
 * file_write_and_wait_range - write out & wait on a file range
 * @file:	file pointing to address_space with pages
 * @lstart:	offset in bytes where the range starts
 * @lend:	offset in bytes where the range ends (inclusive)
 *
 * Write out and wait upon file offsets lstart->lend, inclusive.
 *
 * Note that @lend is inclusive (describes the last byte to be written) so
 * that this function can be used to write to the very end-of-file (end = -1).
 *
 * After writing out and waiting on the data, we check and advance the
 * f_wb_err cursor to the latest value, and return any errors detected there.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int file_write_and_wait_range(struct file *file, loff_t lstart, loff_t lend)
{
	int err = 0, err2;
	struct address_space *mapping = file->f_mapping;

	if (mapping_needs_writeback(mapping)) {
		err = __filemap_fdatawrite_range(mapping, lstart, lend,
						 WB_SYNC_ALL);
		/* See comment of filemap_write_and_wait() */
		if (err != -EIO)
			__filemap_fdatawait_range(mapping, lstart, lend);
	}
	err2 = file_check_and_advance_wb_err(file);
	if (!err)
		err = err2;
	return err;
}
EXPORT_SYMBOL(file_write_and_wait_range);

/**
 * replace_page_cache_page - replace a pagecache page with a new one
 * @old:	page to be replaced
 * @new:	page to replace with
 *
 * This function replaces a page in the pagecache with a new one.  On
 * success it acquires the pagecache reference for the new page and
 * drops it for the old page.  Both the old and new pages must be
 * locked.  This function does not add the new page to the LRU, the
 * caller must do that.
 *
 * The remove + add is atomic.  This function cannot fail.
 */
void replace_page_cache_page(struct page *old, struct page *new)
{
	struct address_space *mapping = old->mapping;
	void (*freepage)(struct page *) = mapping->a_ops->freepage;
	pgoff_t offset = old->index;
	XA_STATE(xas, &mapping->i_pages, offset);

	VM_BUG_ON_PAGE(!PageLocked(old), old);
	VM_BUG_ON_PAGE(!PageLocked(new), new);
	VM_BUG_ON_PAGE(new->mapping, new);

	get_page(new);
	new->mapping = mapping;
	new->index = offset;

	mem_cgroup_migrate(old, new);

	xas_lock_irq(&xas);
	xas_store(&xas, new);

	old->mapping = NULL;
	/* hugetlb pages do not participate in page cache accounting. */
	if (!PageHuge(old))
		__dec_lruvec_page_state(old, NR_FILE_PAGES);
	if (!PageHuge(new))
		__inc_lruvec_page_state(new, NR_FILE_PAGES);
	if (PageSwapBacked(old))
		__dec_lruvec_page_state(old, NR_SHMEM);
	if (PageSwapBacked(new))
		__inc_lruvec_page_state(new, NR_SHMEM);
	xas_unlock_irq(&xas);
	if (freepage)
		freepage(old);
	put_page(old);
}
EXPORT_SYMBOL_GPL(replace_page_cache_page);

noinline int __add_to_page_cache_locked(struct page *page,
					struct address_space *mapping,
					pgoff_t offset, gfp_t gfp,
					void **shadowp)
{
	XA_STATE(xas, &mapping->i_pages, offset);
	int huge = PageHuge(page);
	int error;
	bool charged = false;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(PageSwapBacked(page), page);
	mapping_set_update(&xas, mapping);

	get_page(page);
	page->mapping = mapping;
	page->index = offset;

	if (!huge) {
		error = mem_cgroup_charge(page, NULL, gfp);
		if (error)
			goto error;
		charged = true;
	}

	gfp &= GFP_RECLAIM_MASK;

	do {
		unsigned int order = xa_get_order(xas.xa, xas.xa_index);
		void *entry, *old = NULL;

		if (order > thp_order(page))
			xas_split_alloc(&xas, xa_load(xas.xa, xas.xa_index),
					order, gfp);
		xas_lock_irq(&xas);
		xas_for_each_conflict(&xas, entry) {
			old = entry;
			if (!xa_is_value(entry)) {
				xas_set_err(&xas, -EEXIST);
				goto unlock;
			}
		}

		if (old) {
			if (shadowp)
				*shadowp = old;
			/* entry may have been split before we acquired lock */
			order = xa_get_order(xas.xa, xas.xa_index);
			if (order > thp_order(page)) {
				xas_split(&xas, old, order);
				xas_reset(&xas);
			}
		}

		xas_store(&xas, page);
		if (xas_error(&xas))
			goto unlock;

		mapping->nrpages++;

		/* hugetlb pages do not participate in page cache accounting */
		if (!huge)
			__inc_lruvec_page_state(page, NR_FILE_PAGES);
unlock:
		xas_unlock_irq(&xas);
	} while (xas_nomem(&xas, gfp));

	if (xas_error(&xas)) {
		error = xas_error(&xas);
		if (charged)
			mem_cgroup_uncharge(page);
		goto error;
	}

	trace_mm_filemap_add_to_page_cache(page);
	return 0;
error:
	page->mapping = NULL;
	/* Leave page->index set: truncation relies upon it */
	put_page(page);
	return error;
}
ALLOW_ERROR_INJECTION(__add_to_page_cache_locked, ERRNO);

/**
 * add_to_page_cache_locked - add a locked page to the pagecache
 * @page:	page to add
 * @mapping:	the page's address_space
 * @offset:	page index
 * @gfp_mask:	page allocation mode
 *
 * This function is used to add a page to the pagecache. It must be locked.
 * This function does not add the page to the LRU.  The caller must do that.
 *
 * Return: %0 on success, negative error code otherwise.
 */
int add_to_page_cache_locked(struct page *page, struct address_space *mapping,
		pgoff_t offset, gfp_t gfp_mask)
{
	return __add_to_page_cache_locked(page, mapping, offset,
					  gfp_mask, NULL);
}
EXPORT_SYMBOL(add_to_page_cache_locked);

int add_to_page_cache_lru(struct page *page, struct address_space *mapping,
				pgoff_t offset, gfp_t gfp_mask)
{
	void *shadow = NULL;
	int ret;

	__SetPageLocked(page);
	ret = __add_to_page_cache_locked(page, mapping, offset,
					 gfp_mask, &shadow);
	if (unlikely(ret))
		__ClearPageLocked(page);
	else {
		/*
		 * The page might have been evicted from cache only
		 * recently, in which case it should be activated like
		 * any other repeatedly accessed page.
		 * The exception is pages getting rewritten; evicting other
		 * data from the working set, only to cache data that will
		 * get overwritten with something else, is a waste of memory.
		 */
		WARN_ON_ONCE(PageActive(page));
		if (!(gfp_mask & __GFP_WRITE) && shadow)
			workingset_refault(page, shadow);
		lru_cache_add(page);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(add_to_page_cache_lru);

#ifdef CONFIG_NUMA
struct page *__page_cache_alloc(gfp_t gfp)
{
	int n;
	struct page *page;

	if (cpuset_do_page_mem_spread()) {
		unsigned int cpuset_mems_cookie;
		do {
			cpuset_mems_cookie = read_mems_allowed_begin();
			n = cpuset_mem_spread_node();
			page = __alloc_pages_node(n, gfp, 0);
		} while (!page && read_mems_allowed_retry(cpuset_mems_cookie));

		return page;
	}
	return alloc_pages(gfp, 0);
}
EXPORT_SYMBOL(__page_cache_alloc);
#endif

/*
 * filemap_invalidate_lock_two - lock invalidate_lock for two mappings
 *
 * Lock exclusively invalidate_lock of any passed mapping that is not NULL.
 *
 * @mapping1: the first mapping to lock
 * @mapping2: the second mapping to lock
 */
void filemap_invalidate_lock_two(struct address_space *mapping1,
				 struct address_space *mapping2)
{
	if (mapping1 > mapping2)
		swap(mapping1, mapping2);
	if (mapping1)
		down_write(&mapping1->invalidate_lock);
	if (mapping2 && mapping1 != mapping2)
		down_write_nested(&mapping2->invalidate_lock, 1);
}
EXPORT_SYMBOL(filemap_invalidate_lock_two);

/*
 * filemap_invalidate_unlock_two - unlock invalidate_lock for two mappings
 *
 * Unlock exclusive invalidate_lock of any passed mapping that is not NULL.
 *
 * @mapping1: the first mapping to unlock
 * @mapping2: the second mapping to unlock
 */
void filemap_invalidate_unlock_two(struct address_space *mapping1,
				   struct address_space *mapping2)
{
	if (mapping1)
		up_write(&mapping1->invalidate_lock);
	if (mapping2 && mapping1 != mapping2)
		up_write(&mapping2->invalidate_lock);
}
EXPORT_SYMBOL(filemap_invalidate_unlock_two);

/*
 * In order to wait for pages to become available there must be
 * waitqueues associated with pages. By using a hash table of
 * waitqueues where the bucket discipline is to maintain all
 * waiters on the same queue and wake all when any of the pages
 * become available, and for the woken contexts to check to be
 * sure the appropriate page became available, this saves space
 * at a cost of "thundering herd" phenomena during rare hash
 * collisions.
 */
#define PAGE_WAIT_TABLE_BITS 8
#define PAGE_WAIT_TABLE_SIZE (1 << PAGE_WAIT_TABLE_BITS)
static wait_queue_head_t page_wait_table[PAGE_WAIT_TABLE_SIZE] __cacheline_aligned;

static wait_queue_head_t *page_waitqueue(struct page *page)
{
	return &page_wait_table[hash_ptr(page, PAGE_WAIT_TABLE_BITS)];
}

void __init pagecache_init(void)
{
	int i;

	for (i = 0; i < PAGE_WAIT_TABLE_SIZE; i++)
		init_waitqueue_head(&page_wait_table[i]);

	page_writeback_init();
}

/*
 * The page wait code treats the "wait->flags" somewhat unusually, because
 * we have multiple different kinds of waits, not just the usual "exclusive"
 * one.
 *
 * We have:
 *
 *  (a) no special bits set:
 *
 *	We're just waiting for the bit to be released, and when a waker
 *	calls the wakeup function, we set WQ_FLAG_WOKEN and wake it up,
 *	and remove it from the wait queue.
 *
 *	Simple and straightforward.
 *
 *  (b) WQ_FLAG_EXCLUSIVE:
 *
 *	The waiter is waiting to get the lock, and only one waiter should
 *	be woken up to avoid any thundering herd behavior. We'll set the
 *	WQ_FLAG_WOKEN bit, wake it up, and remove it from the wait queue.
 *
 *	This is the traditional exclusive wait.
 *
 *  (c) WQ_FLAG_EXCLUSIVE | WQ_FLAG_CUSTOM:
 *
 *	The waiter is waiting to get the bit, and additionally wants the
 *	lock to be transferred to it for fair lock behavior. If the lock
 *	cannot be taken, we stop walking the wait queue without waking
 *	the waiter.
 *
 *	This is the "fair lock handoff" case, and in addition to setting
 *	WQ_FLAG_WOKEN, we set WQ_FLAG_DONE to let the waiter easily see
 *	that it now has the lock.
 */
static int wake_page_function(wait_queue_entry_t *wait, unsigned mode, int sync, void *arg)
{
	unsigned int flags;
	struct wait_page_key *key = arg;
	struct wait_page_queue *wait_page
		= container_of(wait, struct wait_page_queue, wait);

	if (!wake_page_match(wait_page, key))
		return 0;

	/*
	 * If it's a lock handoff wait, we get the bit for it, and
	 * stop walking (and do not wake it up) if we can't.
	 */
	flags = wait->flags;
	if (flags & WQ_FLAG_EXCLUSIVE) {
		if (test_bit(key->bit_nr, &key->page->flags))
			return -1;
		if (flags & WQ_FLAG_CUSTOM) {
			if (test_and_set_bit(key->bit_nr, &key->page->flags))
				return -1;
			flags |= WQ_FLAG_DONE;
		}
	}

	/*
	 * We are holding the wait-queue lock, but the waiter that
	 * is waiting for this will be checking the flags without
	 * any locking.
	 *
	 * So update the flags atomically, and wake up the waiter
	 * afterwards to avoid any races. This store-release pairs
	 * with the load-acquire in wait_on_page_bit_common().
	 */
	smp_store_release(&wait->flags, flags | WQ_FLAG_WOKEN);
	wake_up_state(wait->private, mode);

	/*
	 * Ok, we have successfully done what we're waiting for,
	 * and we can unconditionally remove the wait entry.
	 *
	 * Note that this pairs with the "finish_wait()" in the
	 * waiter, and has to be the absolute last thing we do.
	 * After this list_del_init(&wait->entry) the wait entry
	 * might be de-allocated and the process might even have
	 * exited.
	 */
	list_del_init_careful(&wait->entry);
	return (flags & WQ_FLAG_EXCLUSIVE) != 0;
}

static void wake_up_page_bit(struct page *page, int bit_nr)
{
	wait_queue_head_t *q = page_waitqueue(page);
	struct wait_page_key key;
	unsigned long flags;
	wait_queue_entry_t bookmark;

	key.page = page;
	key.bit_nr = bit_nr;
	key.page_match = 0;

	bookmark.flags = 0;
	bookmark.private = NULL;
	bookmark.func = NULL;
	INIT_LIST_HEAD(&bookmark.entry);

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_locked_key_bookmark(q, TASK_NORMAL, &key, &bookmark);

	while (bookmark.flags & WQ_FLAG_BOOKMARK) {
		/*
		 * Take a breather from holding the lock,
		 * allow pages that finish wake up asynchronously
		 * to acquire the lock and remove themselves
		 * from wait queue
		 */
		spin_unlock_irqrestore(&q->lock, flags);
		cpu_relax();
		spin_lock_irqsave(&q->lock, flags);
		__wake_up_locked_key_bookmark(q, TASK_NORMAL, &key, &bookmark);
	}

	/*
	 * It is possible for other pages to have collided on the waitqueue
	 * hash, so in that case check for a page match. That prevents a long-
	 * term waiter
	 *
	 * It is still possible to miss a case here, when we woke page waiters
	 * and removed them from the waitqueue, but there are still other
	 * page waiters.
	 */
	if (!waitqueue_active(q) || !key.page_match) {
		ClearPageWaiters(page);
		/*
		 * It's possible to miss clearing Waiters here, when we woke
		 * our page waiters, but the hashed waitqueue has waiters for
		 * other pages on it.
		 *
		 * That's okay, it's a rare case. The next waker will clear it.
		 */
	}
	spin_unlock_irqrestore(&q->lock, flags);
}

static void wake_up_page(struct page *page, int bit)
{
	if (!PageWaiters(page))
		return;
	wake_up_page_bit(page, bit);
}

/*
 * A choice of three behaviors for wait_on_page_bit_common():
 */
enum behavior {
	EXCLUSIVE,	/* Hold ref to page and take the bit when woken, like
			 * __lock_page() waiting on then setting PG_locked.
			 */
	SHARED,		/* Hold ref to page and check the bit when woken, like
			 * wait_on_page_writeback() waiting on PG_writeback.
			 */
	DROP,		/* Drop ref to page before wait, no check when woken,
			 * like put_and_wait_on_page_locked() on PG_locked.
			 */
};

/*
 * Attempt to check (or get) the page bit, and mark us done
 * if successful.
 */
static inline bool trylock_page_bit_common(struct page *page, int bit_nr,
					struct wait_queue_entry *wait)
{
	if (wait->flags & WQ_FLAG_EXCLUSIVE) {
		if (test_and_set_bit(bit_nr, &page->flags))
			return false;
	} else if (test_bit(bit_nr, &page->flags))
		return false;

	wait->flags |= WQ_FLAG_WOKEN | WQ_FLAG_DONE;
	return true;
}

/* How many times do we accept lock stealing from under a waiter? */
int sysctl_page_lock_unfairness = 5;

static inline int wait_on_page_bit_common(wait_queue_head_t *q,
	struct page *page, int bit_nr, int state, enum behavior behavior)
{
	int unfairness = sysctl_page_lock_unfairness;
	struct wait_page_queue wait_page;
	wait_queue_entry_t *wait = &wait_page.wait;
	bool thrashing = false;
	bool delayacct = false;
	unsigned long pflags;

	if (bit_nr == PG_locked &&
	    !PageUptodate(page) && PageWorkingset(page)) {
		if (!PageSwapBacked(page)) {
			delayacct_thrashing_start();
			delayacct = true;
		}
		psi_memstall_enter(&pflags);
		thrashing = true;
	}

	init_wait(wait);
	wait->func = wake_page_function;
	wait_page.page = page;
	wait_page.bit_nr = bit_nr;

repeat:
	wait->flags = 0;
	if (behavior == EXCLUSIVE) {
		wait->flags = WQ_FLAG_EXCLUSIVE;
		if (--unfairness < 0)
			wait->flags |= WQ_FLAG_CUSTOM;
	}

	/*
	 * Do one last check whether we can get the
	 * page bit synchronously.
	 *
	 * Do the SetPageWaiters() marking before that
	 * to let any waker we _just_ missed know they
	 * need to wake us up (otherwise they'll never
	 * even go to the slow case that looks at the
	 * page queue), and add ourselves to the wait
	 * queue if we need to sleep.
	 *
	 * This part needs to be done under the queue
	 * lock to avoid races.
	 */
	spin_lock_irq(&q->lock);
	SetPageWaiters(page);
	if (!trylock_page_bit_common(page, bit_nr, wait))
		__add_wait_queue_entry_tail(q, wait);
	spin_unlock_irq(&q->lock);

	/*
	 * From now on, all the logic will be based on
	 * the WQ_FLAG_WOKEN and WQ_FLAG_DONE flag, to
	 * see whether the page bit testing has already
	 * been done by the wake function.
	 *
	 * We can drop our reference to the page.
	 */
	if (behavior == DROP)
		put_page(page);

	/*
	 * Note that until the "finish_wait()", or until
	 * we see the WQ_FLAG_WOKEN flag, we need to
	 * be very careful with the 'wait->flags', because
	 * we may race with a waker that sets them.
	 */
	for (;;) {
		unsigned int flags;

		set_current_state(state);

		/* Loop until we've been woken or interrupted */
		flags = smp_load_acquire(&wait->flags);
		if (!(flags & WQ_FLAG_WOKEN)) {
			if (signal_pending_state(state, current))
				break;

			io_schedule();
			continue;
		}

		/* If we were non-exclusive, we're done */
		if (behavior != EXCLUSIVE)
			break;

		/* If the waker got the lock for us, we're done */
		if (flags & WQ_FLAG_DONE)
			break;

		/*
		 * Otherwise, if we're getting the lock, we need to
		 * try to get it ourselves.
		 *
		 * And if that fails, we'll have to retry this all.
		 */
		if (unlikely(test_and_set_bit(bit_nr, &page->flags)))
			goto repeat;

		wait->flags |= WQ_FLAG_DONE;
		break;
	}

	/*
	 * If a signal happened, this 'finish_wait()' may remove the last
	 * waiter from the wait-queues, but the PageWaiters bit will remain
	 * set. That's ok. The next wakeup will take care of it, and trying
	 * to do it here would be difficult and prone to races.
	 */
	finish_wait(q, wait);

	if (thrashing) {
		if (delayacct)
			delayacct_thrashing_end();
		psi_memstall_leave(&pflags);
	}

	/*
	 * NOTE! The wait->flags weren't stable until we've done the
	 * 'finish_wait()', and we could have exited the loop above due
	 * to a signal, and had a wakeup event happen after the signal
	 * test but before the 'finish_wait()'.
	 *
	 * So only after the finish_wait() can we reliably determine
	 * if we got woken up or not, so we can now figure out the final
	 * return value based on that state without races.
	 *
	 * Also note that WQ_FLAG_WOKEN is sufficient for a non-exclusive
	 * waiter, but an exclusive one requires WQ_FLAG_DONE.
	 */
	if (behavior == EXCLUSIVE)
		return wait->flags & WQ_FLAG_DONE ? 0 : -EINTR;

	return wait->flags & WQ_FLAG_WOKEN ? 0 : -EINTR;
}

void wait_on_page_bit(struct page *page, int bit_nr)
{
	wait_queue_head_t *q = page_waitqueue(page);
	wait_on_page_bit_common(q, page, bit_nr, TASK_UNINTERRUPTIBLE, SHARED);
}
EXPORT_SYMBOL(wait_on_page_bit);

int wait_on_page_bit_killable(struct page *page, int bit_nr)
{
	wait_queue_head_t *q = page_waitqueue(page);
	return wait_on_page_bit_common(q, page, bit_nr, TASK_KILLABLE, SHARED);
}
EXPORT_SYMBOL(wait_on_page_bit_killable);

/**
 * put_and_wait_on_page_locked - Drop a reference and wait for it to be unlocked
 * @page: The page to wait for.
 * @state: The sleep state (TASK_KILLABLE, TASK_UNINTERRUPTIBLE, etc).
 *
 * The caller should hold a reference on @page.  They expect the page to
 * become unlocked relatively soon, but do not wish to hold up migration
 * (for example) by holding the reference while waiting for the page to
 * come unlocked.  After this function returns, the caller should not
 * dereference @page.
 *
 * Return: 0 if the page was unlocked or -EINTR if interrupted by a signal.
 */
int put_and_wait_on_page_locked(struct page *page, int state)
{
	wait_queue_head_t *q;

	page = compound_head(page);
	q = page_waitqueue(page);
	return wait_on_page_bit_common(q, page, PG_locked, state, DROP);
}

/**
 * add_page_wait_queue - Add an arbitrary waiter to a page's wait queue
 * @page: Page defining the wait queue of interest
 * @waiter: Waiter to add to the queue
 *
 * Add an arbitrary @waiter to the wait queue for the nominated @page.
 */
void add_page_wait_queue(struct page *page, wait_queue_entry_t *waiter)
{
	wait_queue_head_t *q = page_waitqueue(page);
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue_entry_tail(q, waiter);
	SetPageWaiters(page);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(add_page_wait_queue);

#ifndef clear_bit_unlock_is_negative_byte

/*
 * PG_waiters is the high bit in the same byte as PG_lock.
 *
 * On x86 (and on many other architectures), we can clear PG_lock and
 * test the sign bit at the same time. But if the architecture does
 * not support that special operation, we just do this all by hand
 * instead.
 *
 * The read of PG_waiters has to be after (or concurrently with) PG_locked
 * being cleared, but a memory barrier should be unnecessary since it is
 * in the same byte as PG_locked.
 */
static inline bool clear_bit_unlock_is_negative_byte(long nr, volatile void *mem)
{
	clear_bit_unlock(nr, mem);
	/* smp_mb__after_atomic(); */
	return test_bit(PG_waiters, mem);
}

#endif

/**
 * unlock_page - unlock a locked page
 * @page: the page
 *
 * Unlocks the page and wakes up sleepers in wait_on_page_locked().
 * Also wakes sleepers in wait_on_page_writeback() because the wakeup
 * mechanism between PageLocked pages and PageWriteback pages is shared.
 * But that's OK - sleepers in wait_on_page_writeback() just go back to sleep.
 *
 * Note that this depends on PG_waiters being the sign bit in the byte
 * that contains PG_locked - thus the BUILD_BUG_ON(). That allows us to
 * clear the PG_locked bit and test PG_waiters at the same time fairly
 * portably (architectures that do LL/SC can test any bit, while x86 can
 * test the sign bit).
 */
void unlock_page(struct page *page)
{
	BUILD_BUG_ON(PG_waiters != 7);
	page = compound_head(page);
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	if (clear_bit_unlock_is_negative_byte(PG_locked, &page->flags))
		wake_up_page_bit(page, PG_locked);
}
EXPORT_SYMBOL(unlock_page);

/**
 * end_page_private_2 - Clear PG_private_2 and release any waiters
 * @page: The page
 *
 * Clear the PG_private_2 bit on a page and wake up any sleepers waiting for
 * this.  The page ref held for PG_private_2 being set is released.
 *
 * This is, for example, used when a netfs page is being written to a local
 * disk cache, thereby allowing writes to the cache for the same page to be
 * serialised.
 */
void end_page_private_2(struct page *page)
{
	page = compound_head(page);
	VM_BUG_ON_PAGE(!PagePrivate2(page), page);
	clear_bit_unlock(PG_private_2, &page->flags);
	wake_up_page_bit(page, PG_private_2);
	put_page(page);
}
EXPORT_SYMBOL(end_page_private_2);

/**
 * wait_on_page_private_2 - Wait for PG_private_2 to be cleared on a page
 * @page: The page to wait on
 *
 * Wait for PG_private_2 (aka PG_fscache) to be cleared on a page.
 */
void wait_on_page_private_2(struct page *page)
{
	page = compound_head(page);
	while (PagePrivate2(page))
		wait_on_page_bit(page, PG_private_2);
}
EXPORT_SYMBOL(wait_on_page_private_2);

/**
 * wait_on_page_private_2_killable - Wait for PG_private_2 to be cleared on a page
 * @page: The page to wait on
 *
 * Wait for PG_private_2 (aka PG_fscache) to be cleared on a page or until a
 * fatal signal is received by the calling task.
 *
 * Return:
 * - 0 if successful.
 * - -EINTR if a fatal signal was encountered.
 */
int wait_on_page_private_2_killable(struct page *page)
{
	int ret = 0;

	page = compound_head(page);
	while (PagePrivate2(page)) {
		ret = wait_on_page_bit_killable(page, PG_private_2);
		if (ret < 0)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(wait_on_page_private_2_killable);

/**
 * end_page_writeback - end writeback against a page
 * @page: the page
 */
void end_page_writeback(struct page *page)
{
	/*
	 * TestClearPageReclaim could be used here but it is an atomic
	 * operation and overkill in this particular case. Failing to
	 * shuffle a page marked for immediate reclaim is too mild to
	 * justify taking an atomic operation penalty at the end of
	 * ever page writeback.
	 */
	if (PageReclaim(page)) {
		ClearPageReclaim(page);
		rotate_reclaimable_page(page);
	}

	/*
	 * Writeback does not hold a page reference of its own, relying
	 * on truncation to wait for the clearing of PG_writeback.
	 * But here we must make sure that the page is not freed and
	 * reused before the wake_up_page().
	 */
	get_page(page);
	if (!test_clear_page_writeback(page))
		BUG();

	smp_mb__after_atomic();
	wake_up_page(page, PG_writeback);
	put_page(page);
}
EXPORT_SYMBOL(end_page_writeback);

/*
 * After completing I/O on a page, call this routine to update the page
 * flags appropriately
 */
void page_endio(struct page *page, bool is_write, int err)
{
	if (!is_write) {
		if (!err) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	} else {
		if (err) {
			struct address_space *mapping;

			SetPageError(page);
			mapping = page_mapping(page);
			if (mapping)
				mapping_set_error(mapping, err);
		}
		end_page_writeback(page);
	}
}
EXPORT_SYMBOL_GPL(page_endio);

/**
 * __lock_page - get a lock on the page, assuming we need to sleep to get it
 * @__page: the page to lock
 */
void __lock_page(struct page *__page)
{
	struct page *page = compound_head(__page);
	wait_queue_head_t *q = page_waitqueue(page);
	wait_on_page_bit_common(q, page, PG_locked, TASK_UNINTERRUPTIBLE,
				EXCLUSIVE);
}
EXPORT_SYMBOL(__lock_page);

int __lock_page_killable(struct page *__page)
{
	struct page *page = compound_head(__page);
	wait_queue_head_t *q = page_waitqueue(page);
	return wait_on_page_bit_common(q, page, PG_locked, TASK_KILLABLE,
					EXCLUSIVE);
}
EXPORT_SYMBOL_GPL(__lock_page_killable);

int __lock_page_async(struct page *page, struct wait_page_queue *wait)
{
	struct wait_queue_head *q = page_waitqueue(page);
	int ret = 0;

	wait->page = page;
	wait->bit_nr = PG_locked;

	spin_lock_irq(&q->lock);
	__add_wait_queue_entry_tail(q, &wait->wait);
	SetPageWaiters(page);
	ret = !trylock_page(page);
	/*
	 * If we were successful now, we know we're still on the
	 * waitqueue as we're still under the lock. This means it's
	 * safe to remove and return success, we know the callback
	 * isn't going to trigger.
	 */
	if (!ret)
		__remove_wait_queue(q, &wait->wait);
	else
		ret = -EIOCBQUEUED;
	spin_unlock_irq(&q->lock);
	return ret;
}

/*
 * Return values:
 * 1 - page is locked; mmap_lock is still held.
 * 0 - page is not locked.
 *     mmap_lock has been released (mmap_read_unlock(), unless flags had both
 *     FAULT_FLAG_ALLOW_RETRY and FAULT_FLAG_RETRY_NOWAIT set, in
 *     which case mmap_lock is still held.
 *
 * If neither ALLOW_RETRY nor KILLABLE are set, will always return 1
 * with the page locked and the mmap_lock unperturbed.
 */
int __lock_page_or_retry(struct page *page, struct mm_struct *mm,
			 unsigned int flags)
{
	if (fault_flag_allow_retry_first(flags)) {
		/*
		 * CAUTION! In this case, mmap_lock is not released
		 * even though return 0.
		 */
		if (flags & FAULT_FLAG_RETRY_NOWAIT)
			return 0;

		mmap_read_unlock(mm);
		if (flags & FAULT_FLAG_KILLABLE)
			wait_on_page_locked_killable(page);
		else
			wait_on_page_locked(page);
		return 0;
	}
	if (flags & FAULT_FLAG_KILLABLE) {
		int ret;

		ret = __lock_page_killable(page);
		if (ret) {
			mmap_read_unlock(mm);
			return 0;
		}
	} else {
		__lock_page(page);
	}
	return 1;

}

/**
 * page_cache_next_miss() - Find the next gap in the page cache.
 * @mapping: Mapping.
 * @index: Index.
 * @max_scan: Maximum range to search.
 *
 * Search the range [index, min(index + max_scan - 1, ULONG_MAX)] for the
 * gap with the lowest index.
 *
 * This function may be called under the rcu_read_lock.  However, this will
 * not atomically search a snapshot of the cache at a single point in time.
 * For example, if a gap is created at index 5, then subsequently a gap is
 * created at index 10, page_cache_next_miss covering both indices may
 * return 10 if called under the rcu_read_lock.
 *
 * Return: The index of the gap if found, otherwise an index outside the
 * range specified (in which case 'return - index >= max_scan' will be true).
 * In the rare case of index wrap-around, 0 will be returned.
 */
pgoff_t page_cache_next_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan)
{
	XA_STATE(xas, &mapping->i_pages, index);

	while (max_scan--) {
		void *entry = xas_next(&xas);
		if (!entry || xa_is_value(entry))
			break;
		if (xas.xa_index == 0)
			break;
	}

	return xas.xa_index;
}
EXPORT_SYMBOL(page_cache_next_miss);

/**
 * page_cache_prev_miss() - Find the previous gap in the page cache.
 * @mapping: Mapping.
 * @index: Index.
 * @max_scan: Maximum range to search.
 *
 * Search the range [max(index - max_scan + 1, 0), index] for the
 * gap with the highest index.
 *
 * This function may be called under the rcu_read_lock.  However, this will
 * not atomically search a snapshot of the cache at a single point in time.
 * For example, if a gap is created at index 10, then subsequently a gap is
 * created at index 5, page_cache_prev_miss() covering both indices may
 * return 5 if called under the rcu_read_lock.
 *
 * Return: The index of the gap if found, otherwise an index outside the
 * range specified (in which case 'index - return >= max_scan' will be true).
 * In the rare case of wrap-around, ULONG_MAX will be returned.
 */
pgoff_t page_cache_prev_miss(struct address_space *mapping,
			     pgoff_t index, unsigned long max_scan)
{
	XA_STATE(xas, &mapping->i_pages, index);

	while (max_scan--) {
		void *entry = xas_prev(&xas);
		if (!entry || xa_is_value(entry))
			break;
		if (xas.xa_index == ULONG_MAX)
			break;
	}

	return xas.xa_index;
}
EXPORT_SYMBOL(page_cache_prev_miss);

/*
 * mapping_get_entry - Get a page cache entry.
 * @mapping: the address_space to search
 * @index: The page cache index.
 *
 * Looks up the page cache slot at @mapping & @index.  If there is a
 * page cache page, the head page is returned with an increased refcount.
 *
 * If the slot holds a shadow entry of a previously evicted page, or a
 * swap entry from shmem/tmpfs, it is returned.
 *
 * Return: The head page or shadow entry, %NULL if nothing is found.
 */
static struct page *mapping_get_entry(struct address_space *mapping,
		pgoff_t index)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct page *page;

	rcu_read_lock();
repeat:
	xas_reset(&xas);
	page = xas_load(&xas);
	if (xas_retry(&xas, page))
		goto repeat;
	/*
	 * A shadow entry of a recently evicted page, or a swap entry from
	 * shmem/tmpfs.  Return it without attempting to raise page count.
	 */
	if (!page || xa_is_value(page))
		goto out;

	if (!page_cache_get_speculative(page))
		goto repeat;

	/*
	 * Has the page moved or been split?
	 * This is part of the lockless pagecache protocol. See
	 * include/linux/pagemap.h for details.
	 */
	if (unlikely(page != xas_reload(&xas))) {
		put_page(page);
		goto repeat;
	}
out:
	rcu_read_unlock();

	return page;
}

/**
 * pagecache_get_page - Find and get a reference to a page.
 * @mapping: The address_space to search.
 * @index: The page index.
 * @fgp_flags: %FGP flags modify how the page is returned.
 * @gfp_mask: Memory allocation flags to use if %FGP_CREAT is specified.
 *
 * Looks up the page cache entry at @mapping & @index.
 *
 * @fgp_flags can be zero or more of these flags:
 *
 * * %FGP_ACCESSED - The page will be marked accessed.
 * * %FGP_LOCK - The page is returned locked.
 * * %FGP_HEAD - If the page is present and a THP, return the head page
 *   rather than the exact page specified by the index.
 * * %FGP_ENTRY - If there is a shadow / swap / DAX entry, return it
 *   instead of allocating a new page to replace it.
 * * %FGP_CREAT - If no page is present then a new page is allocated using
 *   @gfp_mask and added to the page cache and the VM's LRU list.
 *   The page is returned locked and with an increased refcount.
 * * %FGP_FOR_MMAP - The caller wants to do its own locking dance if the
 *   page is already in cache.  If the page was allocated, unlock it before
 *   returning so the caller can do the same dance.
 * * %FGP_WRITE - The page will be written
 * * %FGP_NOFS - __GFP_FS will get cleared in gfp mask
 * * %FGP_NOWAIT - Don't get blocked by page lock
 *
 * If %FGP_LOCK or %FGP_CREAT are specified then the function may sleep even
 * if the %GFP flags specified for %FGP_CREAT are atomic.
 *
 * If there is a page cache page, it is returned with an increased refcount.
 *
 * Return: The found page or %NULL otherwise.
 */
struct page *pagecache_get_page(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp_mask)
{
	struct page *page;

repeat:
	page = mapping_get_entry(mapping, index);
	if (xa_is_value(page)) {
		if (fgp_flags & FGP_ENTRY)
			return page;
		page = NULL;
	}
	if (!page)
		goto no_page;

	if (fgp_flags & FGP_LOCK) {
		if (fgp_flags & FGP_NOWAIT) {
			if (!trylock_page(page)) {
				put_page(page);
				return NULL;
			}
		} else {
			lock_page(page);
		}

		/* Has the page been truncated? */
		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			put_page(page);
			goto repeat;
		}
		VM_BUG_ON_PAGE(!thp_contains(page, index), page);
	}

	if (fgp_flags & FGP_ACCESSED)
		mark_page_accessed(page);
	else if (fgp_flags & FGP_WRITE) {
		/* Clear idle flag for buffer write */
		if (page_is_idle(page))
			clear_page_idle(page);
	}
	if (!(fgp_flags & FGP_HEAD))
		page = find_subpage(page, index);

no_page:
	if (!page && (fgp_flags & FGP_CREAT)) {
		int err;
		if ((fgp_flags & FGP_WRITE) && mapping_can_writeback(mapping))
			gfp_mask |= __GFP_WRITE;
		if (fgp_flags & FGP_NOFS)
			gfp_mask &= ~__GFP_FS;

		page = __page_cache_alloc(gfp_mask);
		if (!page)
			return NULL;

		if (WARN_ON_ONCE(!(fgp_flags & (FGP_LOCK | FGP_FOR_MMAP))))
			fgp_flags |= FGP_LOCK;

		/* Init accessed so avoid atomic mark_page_accessed later */
		if (fgp_flags & FGP_ACCESSED)
			__SetPageReferenced(page);

		err = add_to_page_cache_lru(page, mapping, index, gfp_mask);
		if (unlikely(err)) {
			put_page(page);
			page = NULL;
			if (err == -EEXIST)
				goto repeat;
		}

		/*
		 * add_to_page_cache_lru locks the page, and for mmap we expect
		 * an unlocked page.
		 */
		if (page && (fgp_flags & FGP_FOR_MMAP))
			unlock_page(page);
	}

	return page;
}
EXPORT_SYMBOL(pagecache_get_page);

static inline struct page *find_get_entry(struct xa_state *xas, pgoff_t max,
		xa_mark_t mark)
{
	struct page *page;

retry:
	if (mark == XA_PRESENT)
		page = xas_find(xas, max);
	else
		page = xas_find_marked(xas, max, mark);

	if (xas_retry(xas, page))
		goto retry;
	/*
	 * A shadow entry of a recently evicted page, a swap
	 * entry from shmem/tmpfs or a DAX entry.  Return it
	 * without attempting to raise page count.
	 */
	if (!page || xa_is_value(page))
		return page;

	if (!page_cache_get_speculative(page))
		goto reset;

	/* Has the page moved or been split? */
	if (unlikely(page != xas_reload(xas))) {
		put_page(page);
		goto reset;
	}

	return page;
reset:
	xas_reset(xas);
	goto retry;
}

/**
 * find_get_entries - gang pagecache lookup
 * @mapping:	The address_space to search
 * @start:	The starting page cache index
 * @end:	The final page index (inclusive).
 * @pvec:	Where the resulting entries are placed.
 * @indices:	The cache indices corresponding to the entries in @entries
 *
 * find_get_entries() will search for and return a batch of entries in
 * the mapping.  The entries are placed in @pvec.  find_get_entries()
 * takes a reference on any actual pages it returns.
 *
 * The search returns a group of mapping-contiguous page cache entries
 * with ascending indexes.  There may be holes in the indices due to
 * not-present pages.
 *
 * Any shadow entries of evicted pages, or swap entries from
 * shmem/tmpfs, are included in the returned array.
 *
 * If it finds a Transparent Huge Page, head or tail, find_get_entries()
 * stops at that page: the caller is likely to have a better way to handle
 * the compound page as a whole, and then skip its extent, than repeatedly
 * calling find_get_entries() to return all its tails.
 *
 * Return: the number of pages and shadow entries which were found.
 */
unsigned find_get_entries(struct address_space *mapping, pgoff_t start,
		pgoff_t end, struct pagevec *pvec, pgoff_t *indices)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct page *page;
	unsigned int ret = 0;
	unsigned nr_entries = PAGEVEC_SIZE;

	rcu_read_lock();
	while ((page = find_get_entry(&xas, end, XA_PRESENT))) {
		/*
		 * Terminate early on finding a THP, to allow the caller to
		 * handle it all at once; but continue if this is hugetlbfs.
		 */
		if (!xa_is_value(page) && PageTransHuge(page) &&
				!PageHuge(page)) {
			page = find_subpage(page, xas.xa_index);
			nr_entries = ret + 1;
		}

		indices[ret] = xas.xa_index;
		pvec->pages[ret] = page;
		if (++ret == nr_entries)
			break;
	}
	rcu_read_unlock();

	pvec->nr = ret;
	return ret;
}

/**
 * find_lock_entries - Find a batch of pagecache entries.
 * @mapping:	The address_space to search.
 * @start:	The starting page cache index.
 * @end:	The final page index (inclusive).
 * @pvec:	Where the resulting entries are placed.
 * @indices:	The cache indices of the entries in @pvec.
 *
 * find_lock_entries() will return a batch of entries from @mapping.
 * Swap, shadow and DAX entries are included.  Pages are returned
 * locked and with an incremented refcount.  Pages which are locked by
 * somebody else or under writeback are skipped.  Only the head page of
 * a THP is returned.  Pages which are partially outside the range are
 * not returned.
 *
 * The entries have ascending indexes.  The indices may not be consecutive
 * due to not-present entries, THP pages, pages which could not be locked
 * or pages under writeback.
 *
 * Return: The number of entries which were found.
 */
unsigned find_lock_entries(struct address_space *mapping, pgoff_t start,
		pgoff_t end, struct pagevec *pvec, pgoff_t *indices)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct page *page;

	rcu_read_lock();
	while ((page = find_get_entry(&xas, end, XA_PRESENT))) {
		if (!xa_is_value(page)) {
			if (page->index < start)
				goto put;
			VM_BUG_ON_PAGE(page->index != xas.xa_index, page);
			if (page->index + thp_nr_pages(page) - 1 > end)
				goto put;
			if (!trylock_page(page))
				goto put;
			if (page->mapping != mapping || PageWriteback(page))
				goto unlock;
			VM_BUG_ON_PAGE(!thp_contains(page, xas.xa_index),
					page);
		}
		indices[pvec->nr] = xas.xa_index;
		if (!pagevec_add(pvec, page))
			break;
		goto next;
unlock:
		unlock_page(page);
put:
		put_page(page);
next:
		if (!xa_is_value(page) && PageTransHuge(page)) {
			unsigned int nr_pages = thp_nr_pages(page);

			/* Final THP may cross MAX_LFS_FILESIZE on 32-bit */
			xas_set(&xas, page->index + nr_pages);
			if (xas.xa_index < nr_pages)
				break;
		}
	}
	rcu_read_unlock();

	return pagevec_count(pvec);
}

/**
 * find_get_pages_range - gang pagecache lookup
 * @mapping:	The address_space to search
 * @start:	The starting page index
 * @end:	The final page index (inclusive)
 * @nr_pages:	The maximum number of pages
 * @pages:	Where the resulting pages are placed
 *
 * find_get_pages_range() will search for and return a group of up to @nr_pages
 * pages in the mapping starting at index @start and up to index @end
 * (inclusive).  The pages are placed at @pages.  find_get_pages_range() takes
 * a reference against the returned pages.
 *
 * The search returns a group of mapping-contiguous pages with ascending
 * indexes.  There may be holes in the indices due to not-present pages.
 * We also update @start to index the next page for the traversal.
 *
 * Return: the number of pages which were found. If this number is
 * smaller than @nr_pages, the end of specified range has been
 * reached.
 */
unsigned find_get_pages_range(struct address_space *mapping, pgoff_t *start,
			      pgoff_t end, unsigned int nr_pages,
			      struct page **pages)
{
	XA_STATE(xas, &mapping->i_pages, *start);
	struct page *page;
	unsigned ret = 0;

	if (unlikely(!nr_pages))
		return 0;

	rcu_read_lock();
	while ((page = find_get_entry(&xas, end, XA_PRESENT))) {
		/* Skip over shadow, swap and DAX entries */
		if (xa_is_value(page))
			continue;

		pages[ret] = find_subpage(page, xas.xa_index);
		if (++ret == nr_pages) {
			*start = xas.xa_index + 1;
			goto out;
		}
	}

	/*
	 * We come here when there is no page beyond @end. We take care to not
	 * overflow the index @start as it confuses some of the callers. This
	 * breaks the iteration when there is a page at index -1 but that is
	 * already broken anyway.
	 */
	if (end == (pgoff_t)-1)
		*start = (pgoff_t)-1;
	else
		*start = end + 1;
out:
	rcu_read_unlock();

	return ret;
}

/**
 * find_get_pages_contig - gang contiguous pagecache lookup
 * @mapping:	The address_space to search
 * @index:	The starting page index
 * @nr_pages:	The maximum number of pages
 * @pages:	Where the resulting pages are placed
 *
 * find_get_pages_contig() works exactly like find_get_pages(), except
 * that the returned number of pages are guaranteed to be contiguous.
 *
 * Return: the number of pages which were found.
 */
unsigned find_get_pages_contig(struct address_space *mapping, pgoff_t index,
			       unsigned int nr_pages, struct page **pages)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct page *page;
	unsigned int ret = 0;

	if (unlikely(!nr_pages))
		return 0;

	rcu_read_lock();
	for (page = xas_load(&xas); page; page = xas_next(&xas)) {
		if (xas_retry(&xas, page))
			continue;
		/*
		 * If the entry has been swapped out, we can stop looking.
		 * No current caller is looking for DAX entries.
		 */
		if (xa_is_value(page))
			break;

		if (!page_cache_get_speculative(page))
			goto retry;

		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(&xas)))
			goto put_page;

		pages[ret] = find_subpage(page, xas.xa_index);
		if (++ret == nr_pages)
			break;
		continue;
put_page:
		put_page(page);
retry:
		xas_reset(&xas);
	}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(find_get_pages_contig);

/**
 * find_get_pages_range_tag - Find and return head pages matching @tag.
 * @mapping:	the address_space to search
 * @index:	the starting page index
 * @end:	The final page index (inclusive)
 * @tag:	the tag index
 * @nr_pages:	the maximum number of pages
 * @pages:	where the resulting pages are placed
 *
 * Like find_get_pages(), except we only return head pages which are tagged
 * with @tag.  @index is updated to the index immediately after the last
 * page we return, ready for the next iteration.
 *
 * Return: the number of pages which were found.
 */
unsigned find_get_pages_range_tag(struct address_space *mapping, pgoff_t *index,
			pgoff_t end, xa_mark_t tag, unsigned int nr_pages,
			struct page **pages)
{
	XA_STATE(xas, &mapping->i_pages, *index);
	struct page *page;
	unsigned ret = 0;

	if (unlikely(!nr_pages))
		return 0;

	rcu_read_lock();
	while ((page = find_get_entry(&xas, end, tag))) {
		/*
		 * Shadow entries should never be tagged, but this iteration
		 * is lockless so there is a window for page reclaim to evict
		 * a page we saw tagged.  Skip over it.
		 */
		if (xa_is_value(page))
			continue;

		pages[ret] = page;
		if (++ret == nr_pages) {
			*index = page->index + thp_nr_pages(page);
			goto out;
		}
	}

	/*
	 * We come here when we got to @end. We take care to not overflow the
	 * index @index as it confuses some of the callers. This breaks the
	 * iteration when there is a page at index -1 but that is already
	 * broken anyway.
	 */
	if (end == (pgoff_t)-1)
		*index = (pgoff_t)-1;
	else
		*index = end + 1;
out:
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(find_get_pages_range_tag);

/*
 * CD/DVDs are error prone. When a medium error occurs, the driver may fail
 * a _large_ part of the i/o request. Imagine the worst scenario:
 *
 *      ---R__________________________________________B__________
 *         ^ reading here                             ^ bad block(assume 4k)
 *
 * read(R) => miss => readahead(R...B) => media error => frustrating retries
 * => failing the whole request => read(R) => read(R+1) =>
 * readahead(R+1...B+1) => bang => read(R+2) => read(R+3) =>
 * readahead(R+3...B+2) => bang => read(R+3) => read(R+4) =>
 * readahead(R+4...B+3) => bang => read(R+4) => read(R+5) => ......
 *
 * It is going insane. Fix it by quickly scaling down the readahead size.
 */
static void shrink_readahead_size_eio(struct file_ra_state *ra)
{
	ra->ra_pages /= 4;
}

/*
 * filemap_get_read_batch - Get a batch of pages for read
 *
 * Get a batch of pages which represent a contiguous range of bytes
 * in the file.  No tail pages will be returned.  If @index is in the
 * middle of a THP, the entire THP will be returned.  The last page in
 * the batch may have Readahead set or be not Uptodate so that the
 * caller can take the appropriate action.
 */
static void filemap_get_read_batch(struct address_space *mapping,
		pgoff_t index, pgoff_t max, struct pagevec *pvec)
{
	XA_STATE(xas, &mapping->i_pages, index);
	struct page *head;

	rcu_read_lock();
	for (head = xas_load(&xas); head; head = xas_next(&xas)) {
		if (xas_retry(&xas, head))
			continue;
		if (xas.xa_index > max || xa_is_value(head))
			break;
		if (!page_cache_get_speculative(head))
			goto retry;

		/* Has the page moved or been split? */
		if (unlikely(head != xas_reload(&xas)))
			goto put_page;

		if (!pagevec_add(pvec, head))
			break;
		if (!PageUptodate(head))
			break;
		if (PageReadahead(head))
			break;
		xas.xa_index = head->index + thp_nr_pages(head) - 1;
		xas.xa_offset = (xas.xa_index >> xas.xa_shift) & XA_CHUNK_MASK;
		continue;
put_page:
		put_page(head);
retry:
		xas_reset(&xas);
	}
	rcu_read_unlock();
}

static int filemap_read_page(struct file *file, struct address_space *mapping,
		struct page *page)
{
	int error;

	/*
	 * A previous I/O error may have been due to temporary failures,
	 * eg. multipath errors.  PG_error will be set again if readpage
	 * fails.
	 */
	ClearPageError(page);
	/* Start the actual read. The read will unlock the page. */
	error = mapping->a_ops->readpage(file, page);
	if (error)
		return error;

	error = wait_on_page_locked_killable(page);
	if (error)
		return error;
	if (PageUptodate(page))
		return 0;
	shrink_readahead_size_eio(&file->f_ra);
	return -EIO;
}

static bool filemap_range_uptodate(struct address_space *mapping,
		loff_t pos, struct iov_iter *iter, struct page *page)
{
	int count;

	if (PageUptodate(page))
		return true;
	/* pipes can't handle partially uptodate pages */
	if (iov_iter_is_pipe(iter))
		return false;
	if (!mapping->a_ops->is_partially_uptodate)
		return false;
	if (mapping->host->i_blkbits >= (PAGE_SHIFT + thp_order(page)))
		return false;

	count = iter->count;
	if (page_offset(page) > pos) {
		count -= page_offset(page) - pos;
		pos = 0;
	} else {
		pos -= page_offset(page);
	}

	return mapping->a_ops->is_partially_uptodate(page, pos, count);
}

static int filemap_update_page(struct kiocb *iocb,
		struct address_space *mapping, struct iov_iter *iter,
		struct page *page)
{
	int error;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!filemap_invalidate_trylock_shared(mapping))
			return -EAGAIN;
	} else {
		filemap_invalidate_lock_shared(mapping);
	}

	if (!trylock_page(page)) {
		error = -EAGAIN;
		if (iocb->ki_flags & (IOCB_NOWAIT | IOCB_NOIO))
			goto unlock_mapping;
		if (!(iocb->ki_flags & IOCB_WAITQ)) {
			filemap_invalidate_unlock_shared(mapping);
			put_and_wait_on_page_locked(page, TASK_KILLABLE);
			return AOP_TRUNCATED_PAGE;
		}
		error = __lock_page_async(page, iocb->ki_waitq);
		if (error)
			goto unlock_mapping;
	}

	error = AOP_TRUNCATED_PAGE;
	if (!page->mapping)
		goto unlock;

	error = 0;
	if (filemap_range_uptodate(mapping, iocb->ki_pos, iter, page))
		goto unlock;

	error = -EAGAIN;
	if (iocb->ki_flags & (IOCB_NOIO | IOCB_NOWAIT | IOCB_WAITQ))
		goto unlock;

	error = filemap_read_page(iocb->ki_filp, mapping, page);
	goto unlock_mapping;
unlock:
	unlock_page(page);
unlock_mapping:
	filemap_invalidate_unlock_shared(mapping);
	if (error == AOP_TRUNCATED_PAGE)
		put_page(page);
	return error;
}

static int filemap_create_page(struct file *file,
		struct address_space *mapping, pgoff_t index,
		struct pagevec *pvec)
{
	struct page *page;
	int error;

	page = page_cache_alloc(mapping);
	if (!page)
		return -ENOMEM;

	/*
	 * Protect against truncate / hole punch. Grabbing invalidate_lock here
	 * assures we cannot instantiate and bring uptodate new pagecache pages
	 * after evicting page cache during truncate and before actually
	 * freeing blocks.  Note that we could release invalidate_lock after
	 * inserting the page into page cache as the locked page would then be
	 * enough to synchronize with hole punching. But there are code paths
	 * such as filemap_update_page() filling in partially uptodate pages or
	 * ->readpages() that need to hold invalidate_lock while mapping blocks
	 * for IO so let's hold the lock here as well to keep locking rules
	 * simple.
	 */
	filemap_invalidate_lock_shared(mapping);
	error = add_to_page_cache_lru(page, mapping, index,
			mapping_gfp_constraint(mapping, GFP_KERNEL));
	if (error == -EEXIST)
		error = AOP_TRUNCATED_PAGE;
	if (error)
		goto error;

	error = filemap_read_page(file, mapping, page);
	if (error)
		goto error;

	filemap_invalidate_unlock_shared(mapping);
	pagevec_add(pvec, page);
	return 0;
error:
	filemap_invalidate_unlock_shared(mapping);
	put_page(page);
	return error;
}

static int filemap_readahead(struct kiocb *iocb, struct file *file,
		struct address_space *mapping, struct page *page,
		pgoff_t last_index)
{
	if (iocb->ki_flags & IOCB_NOIO)
		return -EAGAIN;
	page_cache_async_readahead(mapping, &file->f_ra, file, page,
			page->index, last_index - page->index);
	return 0;
}

static int filemap_get_pages(struct kiocb *iocb, struct iov_iter *iter,
		struct pagevec *pvec)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	struct file_ra_state *ra = &filp->f_ra;
	pgoff_t index = iocb->ki_pos >> PAGE_SHIFT;
	pgoff_t last_index;
	struct page *page;
	int err = 0;

	last_index = DIV_ROUND_UP(iocb->ki_pos + iter->count, PAGE_SIZE);
retry:
	if (fatal_signal_pending(current))
		return -EINTR;

	filemap_get_read_batch(mapping, index, last_index, pvec);
	if (!pagevec_count(pvec)) {
		if (iocb->ki_flags & IOCB_NOIO)
			return -EAGAIN;
		page_cache_sync_readahead(mapping, ra, filp, index,
				last_index - index);
		filemap_get_read_batch(mapping, index, last_index, pvec);
	}
	if (!pagevec_count(pvec)) {
		if (iocb->ki_flags & (IOCB_NOWAIT | IOCB_WAITQ))
			return -EAGAIN;
		err = filemap_create_page(filp, mapping,
				iocb->ki_pos >> PAGE_SHIFT, pvec);
		if (err == AOP_TRUNCATED_PAGE)
			goto retry;
		return err;
	}

	page = pvec->pages[pagevec_count(pvec) - 1];
	if (PageReadahead(page)) {
		err = filemap_readahead(iocb, filp, mapping, page, last_index);
		if (err)
			goto err;
	}
	if (!PageUptodate(page)) {
		if ((iocb->ki_flags & IOCB_WAITQ) && pagevec_count(pvec) > 1)
			iocb->ki_flags |= IOCB_NOWAIT;
		err = filemap_update_page(iocb, mapping, iter, page);
		if (err)
			goto err;
	}

	return 0;
err:
	if (err < 0)
		put_page(page);
	if (likely(--pvec->nr))
		return 0;
	if (err == AOP_TRUNCATED_PAGE)
		goto retry;
	return err;
}

/**
 * filemap_read - Read data from the page cache.
 * @iocb: The iocb to read.
 * @iter: Destination for the data.
 * @already_read: Number of bytes already read by the caller.
 *
 * Copies data from the page cache.  If the data is not currently present,
 * uses the readahead and readpage address_space operations to fetch it.
 *
 * Return: Total number of bytes copied, including those already read by
 * the caller.  If an error happens before any bytes are copied, returns
 * a negative error number.
 */
ssize_t filemap_read(struct kiocb *iocb, struct iov_iter *iter,
		ssize_t already_read)
{
	struct file *filp = iocb->ki_filp;
	struct file_ra_state *ra = &filp->f_ra;
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	struct pagevec pvec;
	int i, error = 0;
	bool writably_mapped;
	loff_t isize, end_offset;

	if (unlikely(iocb->ki_pos >= inode->i_sb->s_maxbytes))
		return 0;
	if (unlikely(!iov_iter_count(iter)))
		return 0;

	iov_iter_truncate(iter, inode->i_sb->s_maxbytes);
	pagevec_init(&pvec);

	do {
		cond_resched();

		/*
		 * If we've already successfully copied some data, then we
		 * can no longer safely return -EIOCBQUEUED. Hence mark
		 * an async read NOWAIT at that point.
		 */
		if ((iocb->ki_flags & IOCB_WAITQ) && already_read)
			iocb->ki_flags |= IOCB_NOWAIT;

		error = filemap_get_pages(iocb, iter, &pvec);
		if (error < 0)
			break;

		/*
		 * i_size must be checked after we know the pages are Uptodate.
		 *
		 * Checking i_size after the check allows us to calculate
		 * the correct value for "nr", which means the zero-filled
		 * part of the page is not copied back to userspace (unless
		 * another truncate extends the file - this is desired though).
		 */
		isize = i_size_read(inode);
		if (unlikely(iocb->ki_pos >= isize))
			goto put_pages;
		end_offset = min_t(loff_t, isize, iocb->ki_pos + iter->count);

		/*
		 * Once we start copying data, we don't want to be touching any
		 * cachelines that might be contended:
		 */
		writably_mapped = mapping_writably_mapped(mapping);

		/*
		 * When a sequential read accesses a page several times, only
		 * mark it as accessed the first time.
		 */
		if (iocb->ki_pos >> PAGE_SHIFT !=
		    ra->prev_pos >> PAGE_SHIFT)
			mark_page_accessed(pvec.pages[0]);

		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			size_t page_size = thp_size(page);
			size_t offset = iocb->ki_pos & (page_size - 1);
			size_t bytes = min_t(loff_t, end_offset - iocb->ki_pos,
					     page_size - offset);
			size_t copied;

			if (end_offset < page_offset(page))
				break;
			if (i > 0)
				mark_page_accessed(page);
			/*
			 * If users can be writing to this page using arbitrary
			 * virtual addresses, take care about potential aliasing
			 * before reading the page on the kernel side.
			 */
			if (writably_mapped) {
				int j;

				for (j = 0; j < thp_nr_pages(page); j++)
					flush_dcache_page(page + j);
			}

			copied = copy_page_to_iter(page, offset, bytes, iter);

			already_read += copied;
			iocb->ki_pos += copied;
			ra->prev_pos = iocb->ki_pos;

			if (copied < bytes) {
				error = -EFAULT;
				break;
			}
		}
put_pages:
		for (i = 0; i < pagevec_count(&pvec); i++)
			put_page(pvec.pages[i]);
		pagevec_reinit(&pvec);
	} while (iov_iter_count(iter) && iocb->ki_pos < isize && !error);

	file_accessed(filp);

	return already_read ? already_read : error;
}
EXPORT_SYMBOL_GPL(filemap_read);

/**
 * generic_file_read_iter - generic filesystem read routine
 * @iocb:	kernel I/O control block
 * @iter:	destination for the data read
 *
 * This is the "read_iter()" routine for all filesystems
 * that can use the page cache directly.
 *
 * The IOCB_NOWAIT flag in iocb->ki_flags indicates that -EAGAIN shall
 * be returned when no data can be read without waiting for I/O requests
 * to complete; it doesn't prevent readahead.
 *
 * The IOCB_NOIO flag in iocb->ki_flags indicates that no new I/O
 * requests shall be made for the read or for readahead.  When no data
 * can be read, -EAGAIN shall be returned.  When readahead would be
 * triggered, a partial, possibly empty read shall be returned.
 *
 * Return:
 * * number of bytes copied, even for partial reads
 * * negative error code (or 0 if IOCB_NOIO) if nothing was read
 */
ssize_t
generic_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);
	ssize_t retval = 0;

	if (!count)
		return 0; /* skip atime */

	if (iocb->ki_flags & IOCB_DIRECT) {
		struct file *file = iocb->ki_filp;
		struct address_space *mapping = file->f_mapping;
		struct inode *inode = mapping->host;
		loff_t size;

		size = i_size_read(inode);
		if (iocb->ki_flags & IOCB_NOWAIT) {
			if (filemap_range_needs_writeback(mapping, iocb->ki_pos,
						iocb->ki_pos + count - 1))
				return -EAGAIN;
		} else {
			retval = filemap_write_and_wait_range(mapping,
						iocb->ki_pos,
					        iocb->ki_pos + count - 1);
			if (retval < 0)
				return retval;
		}

		file_accessed(file);

		retval = mapping->a_ops->direct_IO(iocb, iter);
		if (retval >= 0) {
			iocb->ki_pos += retval;
			count -= retval;
		}
		if (retval != -EIOCBQUEUED)
			iov_iter_revert(iter, count - iov_iter_count(iter));

		/*
		 * Btrfs can have a short DIO read if we encounter
		 * compressed extents, so if there was an error, or if
		 * we've already read everything we wanted to, or if
		 * there was a short read because we hit EOF, go ahead
		 * and return.  Otherwise fallthrough to buffered io for
		 * the rest of the read.  Buffered reads will not work for
		 * DAX files, so don't bother trying.
		 */
		if (retval < 0 || !count || iocb->ki_pos >= size ||
		    IS_DAX(inode))
			return retval;
	}

	return filemap_read(iocb, iter, retval);
}
EXPORT_SYMBOL(generic_file_read_iter);

static inline loff_t page_seek_hole_data(struct xa_state *xas,
		struct address_space *mapping, struct page *page,
		loff_t start, loff_t end, bool seek_data)
{
	const struct address_space_operations *ops = mapping->a_ops;
	size_t offset, bsz = i_blocksize(mapping->host);

	if (xa_is_value(page) || PageUptodate(page))
		return seek_data ? start : end;
	if (!ops->is_partially_uptodate)
		return seek_data ? end : start;

	xas_pause(xas);
	rcu_read_unlock();
	lock_page(page);
	if (unlikely(page->mapping != mapping))
		goto unlock;

	offset = offset_in_thp(page, start) & ~(bsz - 1);

	do {
		if (ops->is_partially_uptodate(page, offset, bsz) == seek_data)
			break;
		start = (start + bsz) & ~(bsz - 1);
		offset += bsz;
	} while (offset < thp_size(page));
unlock:
	unlock_page(page);
	rcu_read_lock();
	return start;
}

static inline
unsigned int seek_page_size(struct xa_state *xas, struct page *page)
{
	if (xa_is_value(page))
		return PAGE_SIZE << xa_get_order(xas->xa, xas->xa_index);
	return thp_size(page);
}

/**
 * mapping_seek_hole_data - Seek for SEEK_DATA / SEEK_HOLE in the page cache.
 * @mapping: Address space to search.
 * @start: First byte to consider.
 * @end: Limit of search (exclusive).
 * @whence: Either SEEK_HOLE or SEEK_DATA.
 *
 * If the page cache knows which blocks contain holes and which blocks
 * contain data, your filesystem can use this function to implement
 * SEEK_HOLE and SEEK_DATA.  This is useful for filesystems which are
 * entirely memory-based such as tmpfs, and filesystems which support
 * unwritten extents.
 *
 * Return: The requested offset on success, or -ENXIO if @whence specifies
 * SEEK_DATA and there is no data after @start.  There is an implicit hole
 * after @end - 1, so SEEK_HOLE returns @end if all the bytes between @start
 * and @end contain data.
 */
loff_t mapping_seek_hole_data(struct address_space *mapping, loff_t start,
		loff_t end, int whence)
{
	XA_STATE(xas, &mapping->i_pages, start >> PAGE_SHIFT);
	pgoff_t max = (end - 1) >> PAGE_SHIFT;
	bool seek_data = (whence == SEEK_DATA);
	struct page *page;

	if (end <= start)
		return -ENXIO;

	rcu_read_lock();
	while ((page = find_get_entry(&xas, max, XA_PRESENT))) {
		loff_t pos = (u64)xas.xa_index << PAGE_SHIFT;
		unsigned int seek_size;

		if (start < pos) {
			if (!seek_data)
				goto unlock;
			start = pos;
		}

		seek_size = seek_page_size(&xas, page);
		pos = round_up(pos + 1, seek_size);
		start = page_seek_hole_data(&xas, mapping, page, start, pos,
				seek_data);
		if (start < pos)
			goto unlock;
		if (start >= end)
			break;
		if (seek_size > PAGE_SIZE)
			xas_set(&xas, pos >> PAGE_SHIFT);
		if (!xa_is_value(page))
			put_page(page);
	}
	if (seek_data)
		start = -ENXIO;
unlock:
	rcu_read_unlock();
	if (page && !xa_is_value(page))
		put_page(page);
	if (start > end)
		return end;
	return start;
}

#ifdef CONFIG_MMU
#define MMAP_LOTSAMISS  (100)
/*
 * lock_page_maybe_drop_mmap - lock the page, possibly dropping the mmap_lock
 * @vmf - the vm_fault for this fault.
 * @page - the page to lock.
 * @fpin - the pointer to the file we may pin (or is already pinned).
 *
 * This works similar to lock_page_or_retry in that it can drop the mmap_lock.
 * It differs in that it actually returns the page locked if it returns 1 and 0
 * if it couldn't lock the page.  If we did have to drop the mmap_lock then fpin
 * will point to the pinned file and needs to be fput()'ed at a later point.
 */
static int lock_page_maybe_drop_mmap(struct vm_fault *vmf, struct page *page,
				     struct file **fpin)
{
	if (trylock_page(page))
		return 1;

	/*
	 * NOTE! This will make us return with VM_FAULT_RETRY, but with
	 * the mmap_lock still held. That's how FAULT_FLAG_RETRY_NOWAIT
	 * is supposed to work. We have way too many special cases..
	 */
	if (vmf->flags & FAULT_FLAG_RETRY_NOWAIT)
		return 0;

	*fpin = maybe_unlock_mmap_for_io(vmf, *fpin);
	if (vmf->flags & FAULT_FLAG_KILLABLE) {
		if (__lock_page_killable(page)) {
			/*
			 * We didn't have the right flags to drop the mmap_lock,
			 * but all fault_handlers only check for fatal signals
			 * if we return VM_FAULT_RETRY, so we need to drop the
			 * mmap_lock here and return 0 if we don't have a fpin.
			 */
			if (*fpin == NULL)
				mmap_read_unlock(vmf->vma->vm_mm);
			return 0;
		}
	} else
		__lock_page(page);
	return 1;
}


/*
 * Synchronous readahead happens when we don't even find a page in the page
 * cache at all.  We don't want to perform IO under the mmap sem, so if we have
 * to drop the mmap sem we return the file that was pinned in order for us to do
 * that.  If we didn't pin a file then we return NULL.  The file that is
 * returned needs to be fput()'ed when we're done with it.
 */
static struct file *do_sync_mmap_readahead(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct file_ra_state *ra = &file->f_ra;
	struct address_space *mapping = file->f_mapping;
	DEFINE_READAHEAD(ractl, file, ra, mapping, vmf->pgoff);
	struct file *fpin = NULL;
	unsigned int mmap_miss;

	/* If we don't want any read-ahead, don't bother */
	if (vmf->vma->vm_flags & VM_RAND_READ)
		return fpin;
	if (!ra->ra_pages)
		return fpin;

	if (vmf->vma->vm_flags & VM_SEQ_READ) {
		fpin = maybe_unlock_mmap_for_io(vmf, fpin);
		page_cache_sync_ra(&ractl, ra->ra_pages);
		return fpin;
	}

	/* Avoid banging the cache line if not needed */
	mmap_miss = READ_ONCE(ra->mmap_miss);
	if (mmap_miss < MMAP_LOTSAMISS * 10)
		WRITE_ONCE(ra->mmap_miss, ++mmap_miss);

	/*
	 * Do we miss much more than hit in this file? If so,
	 * stop bothering with read-ahead. It will only hurt.
	 */
	if (mmap_miss > MMAP_LOTSAMISS)
		return fpin;

	/*
	 * mmap read-around
	 */
	fpin = maybe_unlock_mmap_for_io(vmf, fpin);
	ra->start = max_t(long, 0, vmf->pgoff - ra->ra_pages / 2);
	ra->size = ra->ra_pages;
	ra->async_size = ra->ra_pages / 4;
	ractl._index = ra->start;
	do_page_cache_ra(&ractl, ra->size, ra->async_size);
	return fpin;
}

/*
 * Asynchronous readahead happens when we find the page and PG_readahead,
 * so we want to possibly extend the readahead further.  We return the file that
 * was pinned if we have to drop the mmap_lock in order to do IO.
 */
static struct file *do_async_mmap_readahead(struct vm_fault *vmf,
					    struct page *page)
{
	struct file *file = vmf->vma->vm_file;
	struct file_ra_state *ra = &file->f_ra;
	struct address_space *mapping = file->f_mapping;
	struct file *fpin = NULL;
	unsigned int mmap_miss;
	pgoff_t offset = vmf->pgoff;

	/* If we don't want any read-ahead, don't bother */
	if (vmf->vma->vm_flags & VM_RAND_READ || !ra->ra_pages)
		return fpin;
	mmap_miss = READ_ONCE(ra->mmap_miss);
	if (mmap_miss)
		WRITE_ONCE(ra->mmap_miss, --mmap_miss);
	if (PageReadahead(page)) {
		fpin = maybe_unlock_mmap_for_io(vmf, fpin);
		page_cache_async_readahead(mapping, ra, file,
					   page, offset, ra->ra_pages);
	}
	return fpin;
}

/**
 * filemap_fault - read in file data for page fault handling
 * @vmf:	struct vm_fault containing details of the fault
 *
 * filemap_fault() is invoked via the vma operations vector for a
 * mapped memory region to read in file data during a page fault.
 *
 * The goto's are kind of ugly, but this streamlines the normal case of having
 * it in the page cache, and handles the special cases reasonably without
 * having a lot of duplicated code.
 *
 * vma->vm_mm->mmap_lock must be held on entry.
 *
 * If our return value has VM_FAULT_RETRY set, it's because the mmap_lock
 * may be dropped before doing I/O or by lock_page_maybe_drop_mmap().
 *
 * If our return value does not have VM_FAULT_RETRY set, the mmap_lock
 * has not been released.
 *
 * We never return with VM_FAULT_RETRY and a bit from VM_FAULT_ERROR set.
 *
 * Return: bitwise-OR of %VM_FAULT_ codes.
 */
vm_fault_t filemap_fault(struct vm_fault *vmf)
{
	int error;
	struct file *file = vmf->vma->vm_file;
	struct file *fpin = NULL;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	pgoff_t offset = vmf->pgoff;
	pgoff_t max_off;
	struct page *page;
	vm_fault_t ret = 0;
	bool mapping_locked = false;

	max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(offset >= max_off))
		return VM_FAULT_SIGBUS;

	/*
	 * Do we have something in the page cache already?
	 */
	page = find_get_page(mapping, offset);
	if (likely(page)) {
		/*
		 * We found the page, so try async readahead before waiting for
		 * the lock.
		 */
		if (!(vmf->flags & FAULT_FLAG_TRIED))
			fpin = do_async_mmap_readahead(vmf, page);
		if (unlikely(!PageUptodate(page))) {
			filemap_invalidate_lock_shared(mapping);
			mapping_locked = true;
		}
	} else {
		/* No page in the page cache at all */
		count_vm_event(PGMAJFAULT);
		count_memcg_event_mm(vmf->vma->vm_mm, PGMAJFAULT);
		ret = VM_FAULT_MAJOR;
		fpin = do_sync_mmap_readahead(vmf);
retry_find:
		/*
		 * See comment in filemap_create_page() why we need
		 * invalidate_lock
		 */
		if (!mapping_locked) {
			filemap_invalidate_lock_shared(mapping);
			mapping_locked = true;
		}
		page = pagecache_get_page(mapping, offset,
					  FGP_CREAT|FGP_FOR_MMAP,
					  vmf->gfp_mask);
		if (!page) {
			if (fpin)
				goto out_retry;
			filemap_invalidate_unlock_shared(mapping);
			return VM_FAULT_OOM;
		}
	}

	if (!lock_page_maybe_drop_mmap(vmf, page, &fpin))
		goto out_retry;

	/* Did it get truncated? */
	if (unlikely(compound_head(page)->mapping != mapping)) {
		unlock_page(page);
		put_page(page);
		goto retry_find;
	}
	VM_BUG_ON_PAGE(page_to_pgoff(page) != offset, page);

	/*
	 * We have a locked page in the page cache, now we need to check
	 * that it's up-to-date. If not, it is going to be due to an error.
	 */
	if (unlikely(!PageUptodate(page))) {
		/*
		 * The page was in cache and uptodate and now it is not.
		 * Strange but possible since we didn't hold the page lock all
		 * the time. Let's drop everything get the invalidate lock and
		 * try again.
		 */
		if (!mapping_locked) {
			unlock_page(page);
			put_page(page);
			goto retry_find;
		}
		goto page_not_uptodate;
	}

	/*
	 * We've made it this far and we had to drop our mmap_lock, now is the
	 * time to return to the upper layer and have it re-find the vma and
	 * redo the fault.
	 */
	if (fpin) {
		unlock_page(page);
		goto out_retry;
	}
	if (mapping_locked)
		filemap_invalidate_unlock_shared(mapping);

	/*
	 * Found the page and have a reference on it.
	 * We must recheck i_size under page lock.
	 */
	max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(offset >= max_off)) {
		unlock_page(page);
		put_page(page);
		return VM_FAULT_SIGBUS;
	}

	vmf->page = page;
	return ret | VM_FAULT_LOCKED;

page_not_uptodate:
	/*
	 * Umm, take care of errors if the page isn't up-to-date.
	 * Try to re-read it _once_. We do this synchronously,
	 * because there really aren't any performance issues here
	 * and we need to check for errors.
	 */
	fpin = maybe_unlock_mmap_for_io(vmf, fpin);
	error = filemap_read_page(file, mapping, page);
	if (fpin)
		goto out_retry;
	put_page(page);

	if (!error || error == AOP_TRUNCATED_PAGE)
		goto retry_find;
	filemap_invalidate_unlock_shared(mapping);

	return VM_FAULT_SIGBUS;

out_retry:
	/*
	 * We dropped the mmap_lock, we need to return to the fault handler to
	 * re-find the vma and come back and find our hopefully still populated
	 * page.
	 */
	if (page)
		put_page(page);
	if (mapping_locked)
		filemap_invalidate_unlock_shared(mapping);
	if (fpin)
		fput(fpin);
	return ret | VM_FAULT_RETRY;
}
EXPORT_SYMBOL(filemap_fault);

static bool filemap_map_pmd(struct vm_fault *vmf, struct page *page)
{
	struct mm_struct *mm = vmf->vma->vm_mm;

	/* Huge page is mapped? No need to proceed. */
	if (pmd_trans_huge(*vmf->pmd)) {
		unlock_page(page);
		put_page(page);
		return true;
	}

	if (pmd_none(*vmf->pmd) && PageTransHuge(page)) {
	    vm_fault_t ret = do_set_pmd(vmf, page);
	    if (!ret) {
		    /* The page is mapped successfully, reference consumed. */
		    unlock_page(page);
		    return true;
	    }
	}

	if (pmd_none(*vmf->pmd)) {
		vmf->ptl = pmd_lock(mm, vmf->pmd);
		if (likely(pmd_none(*vmf->pmd))) {
			mm_inc_nr_ptes(mm);
			pmd_populate(mm, vmf->pmd, vmf->prealloc_pte);
			vmf->prealloc_pte = NULL;
		}
		spin_unlock(vmf->ptl);
	}

	/* See comment in handle_pte_fault() */
	if (pmd_devmap_trans_unstable(vmf->pmd)) {
		unlock_page(page);
		put_page(page);
		return true;
	}

	return false;
}

static struct page *next_uptodate_page(struct page *page,
				       struct address_space *mapping,
				       struct xa_state *xas, pgoff_t end_pgoff)
{
	unsigned long max_idx;

	do {
		if (!page)
			return NULL;
		if (xas_retry(xas, page))
			continue;
		if (xa_is_value(page))
			continue;
		if (PageLocked(page))
			continue;
		if (!page_cache_get_speculative(page))
			continue;
		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(xas)))
			goto skip;
		if (!PageUptodate(page) || PageReadahead(page))
			goto skip;
		if (PageHWPoison(page))
			goto skip;
		if (!trylock_page(page))
			goto skip;
		if (page->mapping != mapping)
			goto unlock;
		if (!PageUptodate(page))
			goto unlock;
		max_idx = DIV_ROUND_UP(i_size_read(mapping->host), PAGE_SIZE);
		if (xas->xa_index >= max_idx)
			goto unlock;
		return page;
unlock:
		unlock_page(page);
skip:
		put_page(page);
	} while ((page = xas_next_entry(xas, end_pgoff)) != NULL);

	return NULL;
}

static inline struct page *first_map_page(struct address_space *mapping,
					  struct xa_state *xas,
					  pgoff_t end_pgoff)
{
	return next_uptodate_page(xas_find(xas, end_pgoff),
				  mapping, xas, end_pgoff);
}

static inline struct page *next_map_page(struct address_space *mapping,
					 struct xa_state *xas,
					 pgoff_t end_pgoff)
{
	return next_uptodate_page(xas_next_entry(xas, end_pgoff),
				  mapping, xas, end_pgoff);
}

vm_fault_t filemap_map_pages(struct vm_fault *vmf,
			     pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	struct vm_area_struct *vma = vmf->vma;
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	pgoff_t last_pgoff = start_pgoff;
	unsigned long addr;
	XA_STATE(xas, &mapping->i_pages, start_pgoff);
	struct page *head, *page;
	unsigned int mmap_miss = READ_ONCE(file->f_ra.mmap_miss);
	vm_fault_t ret = 0;

	rcu_read_lock();
	head = first_map_page(mapping, &xas, end_pgoff);
	if (!head)
		goto out;

	if (filemap_map_pmd(vmf, head)) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	addr = vma->vm_start + ((start_pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	vmf->pte = pte_offset_map_lock(vma->vm_mm, vmf->pmd, addr, &vmf->ptl);
	do {
		page = find_subpage(head, xas.xa_index);
		if (PageHWPoison(page))
			goto unlock;

		if (mmap_miss > 0)
			mmap_miss--;

		addr += (xas.xa_index - last_pgoff) << PAGE_SHIFT;
		vmf->pte += xas.xa_index - last_pgoff;
		last_pgoff = xas.xa_index;

		if (!pte_none(*vmf->pte))
			goto unlock;

		/* We're about to handle the fault */
		if (vmf->address == addr)
			ret = VM_FAULT_NOPAGE;

		do_set_pte(vmf, page, addr);
		/* no need to invalidate: a not-present page won't be cached */
		update_mmu_cache(vma, addr, vmf->pte);
		unlock_page(head);
		continue;
unlock:
		unlock_page(head);
		put_page(head);
	} while ((head = next_map_page(mapping, &xas, end_pgoff)) != NULL);
	pte_unmap_unlock(vmf->pte, vmf->ptl);
out:
	rcu_read_unlock();
	WRITE_ONCE(file->f_ra.mmap_miss, mmap_miss);
	return ret;
}
EXPORT_SYMBOL(filemap_map_pages);

vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct page *page = vmf->page;
	vm_fault_t ret = VM_FAULT_LOCKED;

	sb_start_pagefault(mapping->host->i_sb);
	file_update_time(vmf->vma->vm_file);
	lock_page(page);
	if (page->mapping != mapping) {
		unlock_page(page);
		ret = VM_FAULT_NOPAGE;
		goto out;
	}
	/*
	 * We mark the page dirty already here so that when freeze is in
	 * progress, we are guaranteed that writeback during freezing will
	 * see the dirty page and writeprotect it again.
	 */
	set_page_dirty(page);
	wait_for_stable_page(page);
out:
	sb_end_pagefault(mapping->host->i_sb);
	return ret;
}

const struct vm_operations_struct generic_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= filemap_page_mkwrite,
};

/* This is used for a general mmap of a disk file */

int generic_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	file_accessed(file);
	vma->vm_ops = &generic_file_vm_ops;
	return 0;
}

/*
 * This is for filesystems which do not implement ->writepage.
 */
int generic_file_readonly_mmap(struct file *file, struct vm_area_struct *vma)
{
	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE))
		return -EINVAL;
	return generic_file_mmap(file, vma);
}
#else
vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}
int generic_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -ENOSYS;
}
int generic_file_readonly_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -ENOSYS;
}
#endif /* CONFIG_MMU */

EXPORT_SYMBOL(filemap_page_mkwrite);
EXPORT_SYMBOL(generic_file_mmap);
EXPORT_SYMBOL(generic_file_readonly_mmap);

static struct page *wait_on_page_read(struct page *page)
{
	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		if (!PageUptodate(page)) {
			put_page(page);
			page = ERR_PTR(-EIO);
		}
	}
	return page;
}

static struct page *do_read_cache_page(struct address_space *mapping,
				pgoff_t index,
				int (*filler)(void *, struct page *),
				void *data,
				gfp_t gfp)
{
	struct page *page;
	int err;
repeat:
	page = find_get_page(mapping, index);
	if (!page) {
		page = __page_cache_alloc(gfp);
		if (!page)
			return ERR_PTR(-ENOMEM);
		err = add_to_page_cache_lru(page, mapping, index, gfp);
		if (unlikely(err)) {
			put_page(page);
			if (err == -EEXIST)
				goto repeat;
			/* Presumably ENOMEM for xarray node */
			return ERR_PTR(err);
		}

filler:
		if (filler)
			err = filler(data, page);
		else
			err = mapping->a_ops->readpage(data, page);

		if (err < 0) {
			put_page(page);
			return ERR_PTR(err);
		}

		page = wait_on_page_read(page);
		if (IS_ERR(page))
			return page;
		goto out;
	}
	if (PageUptodate(page))
		goto out;

	/*
	 * Page is not up to date and may be locked due to one of the following
	 * case a: Page is being filled and the page lock is held
	 * case b: Read/write error clearing the page uptodate status
	 * case c: Truncation in progress (page locked)
	 * case d: Reclaim in progress
	 *
	 * Case a, the page will be up to date when the page is unlocked.
	 *    There is no need to serialise on the page lock here as the page
	 *    is pinned so the lock gives no additional protection. Even if the
	 *    page is truncated, the data is still valid if PageUptodate as
	 *    it's a race vs truncate race.
	 * Case b, the page will not be up to date
	 * Case c, the page may be truncated but in itself, the data may still
	 *    be valid after IO completes as it's a read vs truncate race. The
	 *    operation must restart if the page is not uptodate on unlock but
	 *    otherwise serialising on page lock to stabilise the mapping gives
	 *    no additional guarantees to the caller as the page lock is
	 *    released before return.
	 * Case d, similar to truncation. If reclaim holds the page lock, it
	 *    will be a race with remove_mapping that determines if the mapping
	 *    is valid on unlock but otherwise the data is valid and there is
	 *    no need to serialise with page lock.
	 *
	 * As the page lock gives no additional guarantee, we optimistically
	 * wait on the page to be unlocked and check if it's up to date and
	 * use the page if it is. Otherwise, the page lock is required to
	 * distinguish between the different cases. The motivation is that we
	 * avoid spurious serialisations and wakeups when multiple processes
	 * wait on the same page for IO to complete.
	 */
	wait_on_page_locked(page);
	if (PageUptodate(page))
		goto out;

	/* Distinguish between all the cases under the safety of the lock */
	lock_page(page);

	/* Case c or d, restart the operation */
	if (!page->mapping) {
		unlock_page(page);
		put_page(page);
		goto repeat;
	}

	/* Someone else locked and filled the page in a very small window */
	if (PageUptodate(page)) {
		unlock_page(page);
		goto out;
	}

	/*
	 * A previous I/O error may have been due to temporary
	 * failures.
	 * Clear page error before actual read, PG_error will be
	 * set again if read page fails.
	 */
	ClearPageError(page);
	goto filler;

out:
	mark_page_accessed(page);
	return page;
}

/**
 * read_cache_page - read into page cache, fill it if needed
 * @mapping:	the page's address_space
 * @index:	the page index
 * @filler:	function to perform the read
 * @data:	first arg to filler(data, page) function, often left as NULL
 *
 * Read into the page cache. If a page already exists, and PageUptodate() is
 * not set, try to fill the page and wait for it to become unlocked.
 *
 * If the page does not get brought uptodate, return -EIO.
 *
 * The function expects mapping->invalidate_lock to be already held.
 *
 * Return: up to date page on success, ERR_PTR() on failure.
 */
struct page *read_cache_page(struct address_space *mapping,
				pgoff_t index,
				int (*filler)(void *, struct page *),
				void *data)
{
	return do_read_cache_page(mapping, index, filler, data,
			mapping_gfp_mask(mapping));
}
EXPORT_SYMBOL(read_cache_page);

/**
 * read_cache_page_gfp - read into page cache, using specified page allocation flags.
 * @mapping:	the page's address_space
 * @index:	the page index
 * @gfp:	the page allocator flags to use if allocating
 *
 * This is the same as "read_mapping_page(mapping, index, NULL)", but with
 * any new page allocations done using the specified allocation flags.
 *
 * If the page does not get brought uptodate, return -EIO.
 *
 * The function expects mapping->invalidate_lock to be already held.
 *
 * Return: up to date page on success, ERR_PTR() on failure.
 */
struct page *read_cache_page_gfp(struct address_space *mapping,
				pgoff_t index,
				gfp_t gfp)
{
	return do_read_cache_page(mapping, index, NULL, NULL, gfp);
}
EXPORT_SYMBOL(read_cache_page_gfp);

int pagecache_write_begin(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	const struct address_space_operations *aops = mapping->a_ops;

	return aops->write_begin(file, mapping, pos, len, flags,
							pagep, fsdata);
}
EXPORT_SYMBOL(pagecache_write_begin);

int pagecache_write_end(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	const struct address_space_operations *aops = mapping->a_ops;

	return aops->write_end(file, mapping, pos, len, copied, page, fsdata);
}
EXPORT_SYMBOL(pagecache_write_end);

/*
 * Warn about a page cache invalidation failure during a direct I/O write.
 */
void dio_warn_stale_pagecache(struct file *filp)
{
	static DEFINE_RATELIMIT_STATE(_rs, 86400 * HZ, DEFAULT_RATELIMIT_BURST);
	char pathname[128];
	char *path;

	errseq_set(&filp->f_mapping->wb_err, -EIO);
	if (__ratelimit(&_rs)) {
		path = file_path(filp, pathname, sizeof(pathname));
		if (IS_ERR(path))
			path = "(unknown)";
		pr_crit("Page cache invalidation failure on direct I/O.  Possible data corruption due to collision with buffered I/O!\n");
		pr_crit("File: %s PID: %d Comm: %.20s\n", path, current->pid,
			current->comm);
	}
}

ssize_t
generic_file_direct_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file	*file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode	*inode = mapping->host;
	loff_t		pos = iocb->ki_pos;
	ssize_t		written;
	size_t		write_len;
	pgoff_t		end;

	write_len = iov_iter_count(from);
	end = (pos + write_len - 1) >> PAGE_SHIFT;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		/* If there are pages to writeback, return */
		if (filemap_range_has_page(file->f_mapping, pos,
					   pos + write_len - 1))
			return -EAGAIN;
	} else {
		written = filemap_write_and_wait_range(mapping, pos,
							pos + write_len - 1);
		if (written)
			goto out;
	}

	/*
	 * After a write we want buffered reads to be sure to go to disk to get
	 * the new data.  We invalidate clean cached page from the region we're
	 * about to write.  We do this *before* the write so that we can return
	 * without clobbering -EIOCBQUEUED from ->direct_IO().
	 */
	written = invalidate_inode_pages2_range(mapping,
					pos >> PAGE_SHIFT, end);
	/*
	 * If a page can not be invalidated, return 0 to fall back
	 * to buffered write.
	 */
	if (written) {
		if (written == -EBUSY)
			return 0;
		goto out;
	}

	written = mapping->a_ops->direct_IO(iocb, from);

	/*
	 * Finally, try again to invalidate clean pages which might have been
	 * cached by non-direct readahead, or faulted in by get_user_pages()
	 * if the source of the write was an mmap'ed region of the file
	 * we're writing.  Either one is a pretty crazy thing to do,
	 * so we don't support it 100%.  If this invalidation
	 * fails, tough, the write still worked...
	 *
	 * Most of the time we do not need this since dio_complete() will do
	 * the invalidation for us. However there are some file systems that
	 * do not end up with dio_complete() being called, so let's not break
	 * them by removing it completely.
	 *
	 * Noticeable example is a blkdev_direct_IO().
	 *
	 * Skip invalidation for async writes or if mapping has no pages.
	 */
	if (written > 0 && mapping->nrpages &&
	    invalidate_inode_pages2_range(mapping, pos >> PAGE_SHIFT, end))
		dio_warn_stale_pagecache(file);

	if (written > 0) {
		pos += written;
		write_len -= written;
		if (pos > i_size_read(inode) && !S_ISBLK(inode->i_mode)) {
			i_size_write(inode, pos);
			mark_inode_dirty(inode);
		}
		iocb->ki_pos = pos;
	}
	if (written != -EIOCBQUEUED)
		iov_iter_revert(from, write_len - iov_iter_count(from));
out:
	return written;
}
EXPORT_SYMBOL(generic_file_direct_write);

/*
 * Find or create a page at the given pagecache position. Return the locked
 * page. This function is specifically for buffered writes.
 */
struct page *grab_cache_page_write_begin(struct address_space *mapping,
					pgoff_t index, unsigned flags)
{
	struct page *page;
	int fgp_flags = FGP_LOCK|FGP_WRITE|FGP_CREAT;

	if (flags & AOP_FLAG_NOFS)
		fgp_flags |= FGP_NOFS;

	page = pagecache_get_page(mapping, index, fgp_flags,
			mapping_gfp_mask(mapping));
	if (page)
		wait_for_stable_page(page);

	return page;
}
EXPORT_SYMBOL(grab_cache_page_write_begin);

ssize_t generic_perform_write(struct file *file,
				struct iov_iter *i, loff_t pos)
{
	struct address_space *mapping = file->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	long status = 0;
	ssize_t written = 0;
	unsigned int flags = 0;

	do {
		struct page *page;
		unsigned long offset;	/* Offset into pagecache page */
		unsigned long bytes;	/* Bytes to write to page */
		size_t copied;		/* Bytes copied from user */
		void *fsdata;

		offset = (pos & (PAGE_SIZE - 1));
		bytes = min_t(unsigned long, PAGE_SIZE - offset,
						iov_iter_count(i));

again:
		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 */
		if (unlikely(fault_in_iov_iter_readable(i, bytes))) {
			status = -EFAULT;
			break;
		}

		if (fatal_signal_pending(current)) {
			status = -EINTR;
			break;
		}

		status = a_ops->write_begin(file, mapping, pos, bytes, flags,
						&page, &fsdata);
		if (unlikely(status < 0))
			break;

		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		copied = copy_page_from_iter_atomic(page, offset, bytes, i);
		flush_dcache_page(page);

		status = a_ops->write_end(file, mapping, pos, bytes, copied,
						page, fsdata);
		if (unlikely(status != copied)) {
			iov_iter_revert(i, copied - max(status, 0L));
			if (unlikely(status < 0))
				break;
		}
		cond_resched();

		if (unlikely(status == 0)) {
			/*
			 * A short copy made ->write_end() reject the
			 * thing entirely.  Might be memory poisoning
			 * halfway through, might be a race with munmap,
			 * might be severe memory pressure.
			 */
			if (copied)
				bytes = copied;
			goto again;
		}
		pos += status;
		written += status;

		balance_dirty_pages_ratelimited(mapping);
	} while (iov_iter_count(i));

	return written ? written : status;
}
EXPORT_SYMBOL(generic_perform_write);

/**
 * __generic_file_write_iter - write data to a file
 * @iocb:	IO state structure (file, offset, etc.)
 * @from:	iov_iter with data to write
 *
 * This function does all the work needed for actually writing data to a
 * file. It does all basic checks, removes SUID from the file, updates
 * modification times and calls proper subroutines depending on whether we
 * do direct IO or a standard buffered write.
 *
 * It expects i_rwsem to be grabbed unless we work on a block device or similar
 * object which does not need locking at all.
 *
 * This function does *not* take care of syncing data in case of O_SYNC write.
 * A caller has to handle it. This is mainly due to the fact that we want to
 * avoid syncing under i_rwsem.
 *
 * Return:
 * * number of bytes written, even for truncated writes
 * * negative error code if no data has been written at all
 */
ssize_t __generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode 	*inode = mapping->host;
	ssize_t		written = 0;
	ssize_t		err;
	ssize_t		status;

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = inode_to_bdi(inode);
	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	if (iocb->ki_flags & IOCB_DIRECT) {
		loff_t pos, endbyte;

		written = generic_file_direct_write(iocb, from);
		/*
		 * If the write stopped short of completing, fall back to
		 * buffered writes.  Some filesystems do this for writes to
		 * holes, for example.  For DAX files, a buffered write will
		 * not succeed (even if it did, DAX does not handle dirty
		 * page-cache pages correctly).
		 */
		if (written < 0 || !iov_iter_count(from) || IS_DAX(inode))
			goto out;

		status = generic_perform_write(file, from, pos = iocb->ki_pos);
		/*
		 * If generic_perform_write() returned a synchronous error
		 * then we want to return the number of bytes which were
		 * direct-written, or the error code if that was zero.  Note
		 * that this differs from normal direct-io semantics, which
		 * will return -EFOO even if some bytes were written.
		 */
		if (unlikely(status < 0)) {
			err = status;
			goto out;
		}
		/*
		 * We need to ensure that the page cache pages are written to
		 * disk and invalidated to preserve the expected O_DIRECT
		 * semantics.
		 */
		endbyte = pos + status - 1;
		err = filemap_write_and_wait_range(mapping, pos, endbyte);
		if (err == 0) {
			iocb->ki_pos = endbyte + 1;
			written += status;
			invalidate_mapping_pages(mapping,
						 pos >> PAGE_SHIFT,
						 endbyte >> PAGE_SHIFT);
		} else {
			/*
			 * We don't know how much we wrote, so just return
			 * the number of bytes which were direct-written
			 */
		}
	} else {
		written = generic_perform_write(file, from, iocb->ki_pos);
		if (likely(written > 0))
			iocb->ki_pos += written;
	}
out:
	current->backing_dev_info = NULL;
	return written ? written : err;
}
EXPORT_SYMBOL(__generic_file_write_iter);

/**
 * generic_file_write_iter - write data to a file
 * @iocb:	IO state structure
 * @from:	iov_iter with data to write
 *
 * This is a wrapper around __generic_file_write_iter() to be used by most
 * filesystems. It takes care of syncing the file in case of O_SYNC file
 * and acquires i_rwsem as needed.
 * Return:
 * * negative error code if no data has been written at all of
 *   vfs_fsync_range() failed for a synchronous write
 * * number of bytes written, even for truncated writes
 */
ssize_t generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret > 0)
		ret = __generic_file_write_iter(iocb, from);
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}
EXPORT_SYMBOL(generic_file_write_iter);

/**
 * try_to_release_page() - release old fs-specific metadata on a page
 *
 * @page: the page which the kernel is trying to free
 * @gfp_mask: memory allocation flags (and I/O mode)
 *
 * The address_space is to try to release any data against the page
 * (presumably at page->private).
 *
 * This may also be called if PG_fscache is set on a page, indicating that the
 * page is known to the local caching routines.
 *
 * The @gfp_mask argument specifies whether I/O may be performed to release
 * this page (__GFP_IO), and whether the call may block (__GFP_RECLAIM & __GFP_FS).
 *
 * Return: %1 if the release was successful, otherwise return zero.
 */
int try_to_release_page(struct page *page, gfp_t gfp_mask)
{
	struct address_space * const mapping = page->mapping;

	BUG_ON(!PageLocked(page));
	if (PageWriteback(page))
		return 0;

	if (mapping && mapping->a_ops->releasepage)
		return mapping->a_ops->releasepage(page, gfp_mask);
	return try_to_free_buffers(page);
}

EXPORT_SYMBOL(try_to_release_page);
