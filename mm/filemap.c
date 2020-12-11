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
 *  ->i_mutex
 *    ->i_mmap_rwsem		(truncate->unmap_mapping_range)
 *
 *  ->mmap_lock
 *    ->i_mmap_rwsem
 *      ->page_table_lock or pte_lock	(various, mainly in memory.c)
 *        ->i_pages lock	(arch-dependent flush_dcache_mmap_lock)
 *
 *  ->mmap_lock
 *    ->lock_page		(access_process_vm)
 *
 *  ->i_mutex			(generic_perform_write)
 *    ->mmap_lock		(fault_in_pages_readable->do_page_fault)
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
 *    ->pgdat->lru_lock		(follow_page->mark_page_accessed)
 *    ->pgdat->lru_lock		(check_pte_range->isolate_lru_page)
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

	if (shadow) {
		mapping->nrexceptional += nr;
		/*
		 * Make sure the nrexceptional update is committed before
		 * the nrpages update so that final truncate racing
		 * with reclaim does not see both counters 0 at the
		 * same time and miss a shadow entry.
		 */
		smp_wmb();
	}
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
			__dec_node_page_state(page, NR_SHMEM_THPS);
	} else if (PageTransHuge(page)) {
		__dec_node_page_state(page, NR_FILE_THPS);
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
	unsigned long flags;

	BUG_ON(!PageLocked(page));
	xa_lock_irqsave(&mapping->i_pages, flags);
	__delete_from_page_cache(page, NULL);
	xa_unlock_irqrestore(&mapping->i_pages, flags);

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
	unsigned long flags;

	if (!pagevec_count(pvec))
		return;

	xa_lock_irqsave(&mapping->i_pages, flags);
	for (i = 0; i < pagevec_count(pvec); i++) {
		trace_mm_filemap_delete_from_page_cache(pvec->pages[i]);

		unaccount_page_cache_page(mapping, pvec->pages[i]);
	}
	page_cache_delete_batch(mapping, pvec);
	xa_unlock_irqrestore(&mapping->i_pages, flags);

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
	int ret;
	struct writeback_control wbc = {
		.sync_mode = sync_mode,
		.nr_to_write = LONG_MAX,
		.range_start = start,
		.range_end = end,
	};

	if (!mapping_can_writeback(mapping) ||
	    !mapping_tagged(mapping, PAGECACHE_TAG_DIRTY))
		return 0;

	wbc_attach_fdatawrite_inode(&wbc, mapping->host);
	ret = do_writepages(mapping, &wbc);
	wbc_detach_inode(&wbc);
	return ret;
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
	if (dax_mapping(mapping))
		return mapping->nrexceptional;

	return mapping->nrpages;
}

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
 * @gfp_mask:	allocation mode
 *
 * This function replaces a page in the pagecache with a new one.  On
 * success it acquires the pagecache reference for the new page and
 * drops it for the old page.  Both the old and new pages must be
 * locked.  This function does not add the new page to the LRU, the
 * caller must do that.
 *
 * The remove + add is atomic.  This function cannot fail.
 *
 * Return: %0
 */
int replace_page_cache_page(struct page *old, struct page *new, gfp_t gfp_mask)
{
	struct address_space *mapping = old->mapping;
	void (*freepage)(struct page *) = mapping->a_ops->freepage;
	pgoff_t offset = old->index;
	XA_STATE(xas, &mapping->i_pages, offset);
	unsigned long flags;

	VM_BUG_ON_PAGE(!PageLocked(old), old);
	VM_BUG_ON_PAGE(!PageLocked(new), new);
	VM_BUG_ON_PAGE(new->mapping, new);

	get_page(new);
	new->mapping = mapping;
	new->index = offset;

	mem_cgroup_migrate(old, new);

	xas_lock_irqsave(&xas, flags);
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
	xas_unlock_irqrestore(&xas, flags);
	if (freepage)
		freepage(old);
	put_page(old);

	return 0;
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

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(PageSwapBacked(page), page);
	mapping_set_update(&xas, mapping);

	get_page(page);
	page->mapping = mapping;
	page->index = offset;

	if (!huge) {
		error = mem_cgroup_charge(page, current->mm, gfp);
		if (error)
			goto error;
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

		if (old)
			mapping->nrexceptional--;
		mapping->nrpages++;

		/* hugetlb pages do not participate in page cache accounting */
		if (!huge)
			__inc_lruvec_page_state(page, NR_FILE_PAGES);
unlock:
		xas_unlock_irq(&xas);
	} while (xas_nomem(&xas, gfp));

	if (xas_error(&xas)) {
		error = xas_error(&xas);
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

static int __wait_on_page_locked_async(struct page *page,
				       struct wait_page_queue *wait, bool set)
{
	struct wait_queue_head *q = page_waitqueue(page);
	int ret = 0;

	wait->page = page;
	wait->bit_nr = PG_locked;

	spin_lock_irq(&q->lock);
	__add_wait_queue_entry_tail(q, &wait->wait);
	SetPageWaiters(page);
	if (set)
		ret = !trylock_page(page);
	else
		ret = PageLocked(page);
	/*
	 * If we were succesful now, we know we're still on the
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

static int wait_on_page_locked_async(struct page *page,
				     struct wait_page_queue *wait)
{
	if (!PageLocked(page))
		return 0;
	return __wait_on_page_locked_async(compound_head(page), wait, false);
}

/**
 * put_and_wait_on_page_locked - Drop a reference and wait for it to be unlocked
 * @page: The page to wait for.
 *
 * The caller should hold a reference on @page.  They expect the page to
 * become unlocked relatively soon, but do not wish to hold up migration
 * (for example) by holding the reference while waiting for the page to
 * come unlocked.  After this function returns, the caller should not
 * dereference @page.
 */
void put_and_wait_on_page_locked(struct page *page)
{
	wait_queue_head_t *q;

	page = compound_head(page);
	q = page_waitqueue(page);
	wait_on_page_bit_common(q, page, PG_locked, TASK_UNINTERRUPTIBLE, DROP);
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

	if (!test_clear_page_writeback(page))
		BUG();

	smp_mb__after_atomic();
	wake_up_page(page, PG_writeback);
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
	return __wait_on_page_locked_async(page, wait, true);
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
	} else {
		if (flags & FAULT_FLAG_KILLABLE) {
			int ret;

			ret = __lock_page_killable(page);
			if (ret) {
				mmap_read_unlock(mm);
				return 0;
			}
		} else
			__lock_page(page);
		return 1;
	}
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

/**
 * find_get_entry - find and get a page cache entry
 * @mapping: the address_space to search
 * @index: The page cache index.
 *
 * Looks up the page cache slot at @mapping & @offset.  If there is a
 * page cache page, the head page is returned with an increased refcount.
 *
 * If the slot holds a shadow entry of a previously evicted page, or a
 * swap entry from shmem/tmpfs, it is returned.
 *
 * Return: The head page or shadow entry, %NULL if nothing is found.
 */
struct page *find_get_entry(struct address_space *mapping, pgoff_t index)
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
 * find_lock_entry - Locate and lock a page cache entry.
 * @mapping: The address_space to search.
 * @index: The page cache index.
 *
 * Looks up the page at @mapping & @index.  If there is a page in the
 * cache, the head page is returned locked and with an increased refcount.
 *
 * If the slot holds a shadow entry of a previously evicted page, or a
 * swap entry from shmem/tmpfs, it is returned.
 *
 * Context: May sleep.
 * Return: The head page or shadow entry, %NULL if nothing is found.
 */
struct page *find_lock_entry(struct address_space *mapping, pgoff_t index)
{
	struct page *page;

repeat:
	page = find_get_entry(mapping, index);
	if (page && !xa_is_value(page)) {
		lock_page(page);
		/* Has the page been truncated? */
		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			put_page(page);
			goto repeat;
		}
		VM_BUG_ON_PAGE(!thp_contains(page, index), page);
	}
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
	page = find_get_entry(mapping, index);
	if (xa_is_value(page))
		page = NULL;
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

/**
 * find_get_entries - gang pagecache lookup
 * @mapping:	The address_space to search
 * @start:	The starting page cache index
 * @nr_entries:	The maximum number of entries
 * @entries:	Where the resulting entries are placed
 * @indices:	The cache indices corresponding to the entries in @entries
 *
 * find_get_entries() will search for and return a group of up to
 * @nr_entries entries in the mapping.  The entries are placed at
 * @entries.  find_get_entries() takes a reference against any actual
 * pages it returns.
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
unsigned find_get_entries(struct address_space *mapping,
			  pgoff_t start, unsigned int nr_entries,
			  struct page **entries, pgoff_t *indices)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct page *page;
	unsigned int ret = 0;

	if (!nr_entries)
		return 0;

	rcu_read_lock();
	xas_for_each(&xas, page, ULONG_MAX) {
		if (xas_retry(&xas, page))
			continue;
		/*
		 * A shadow entry of a recently evicted page, a swap
		 * entry from shmem/tmpfs or a DAX entry.  Return it
		 * without attempting to raise page count.
		 */
		if (xa_is_value(page))
			goto export;

		if (!page_cache_get_speculative(page))
			goto retry;

		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(&xas)))
			goto put_page;

		/*
		 * Terminate early on finding a THP, to allow the caller to
		 * handle it all at once; but continue if this is hugetlbfs.
		 */
		if (PageTransHuge(page) && !PageHuge(page)) {
			page = find_subpage(page, xas.xa_index);
			nr_entries = ret + 1;
		}
export:
		indices[ret] = xas.xa_index;
		entries[ret] = page;
		if (++ret == nr_entries)
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
	xas_for_each(&xas, page, end) {
		if (xas_retry(&xas, page))
			continue;
		/* Skip over shadow, swap and DAX entries */
		if (xa_is_value(page))
			continue;

		if (!page_cache_get_speculative(page))
			goto retry;

		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(&xas)))
			goto put_page;

		pages[ret] = find_subpage(page, xas.xa_index);
		if (++ret == nr_pages) {
			*start = xas.xa_index + 1;
			goto out;
		}
		continue;
put_page:
		put_page(page);
retry:
		xas_reset(&xas);
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
 * find_get_pages_range_tag - find and return pages in given range matching @tag
 * @mapping:	the address_space to search
 * @index:	the starting page index
 * @end:	The final page index (inclusive)
 * @tag:	the tag index
 * @nr_pages:	the maximum number of pages
 * @pages:	where the resulting pages are placed
 *
 * Like find_get_pages, except we only return pages which are tagged with
 * @tag.   We update @index to index the next page for the traversal.
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
	xas_for_each_marked(&xas, page, end, tag) {
		if (xas_retry(&xas, page))
			continue;
		/*
		 * Shadow entries should never be tagged, but this iteration
		 * is lockless so there is a window for page reclaim to evict
		 * a page we saw tagged.  Skip over it.
		 */
		if (xa_is_value(page))
			continue;

		if (!page_cache_get_speculative(page))
			goto retry;

		/* Has the page moved or been split? */
		if (unlikely(page != xas_reload(&xas)))
			goto put_page;

		pages[ret] = find_subpage(page, xas.xa_index);
		if (++ret == nr_pages) {
			*index = xas.xa_index + 1;
			goto out;
		}
		continue;
put_page:
		put_page(page);
retry:
		xas_reset(&xas);
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

/**
 * generic_file_buffered_read - generic file read routine
 * @iocb:	the iocb to read
 * @iter:	data destination
 * @written:	already copied
 *
 * This is a generic file read routine, and uses the
 * mapping->a_ops->readpage() function for the actual low-level stuff.
 *
 * This is really ugly. But the goto's actually try to clarify some
 * of the logic when it comes to error handling etc.
 *
 * Return:
 * * total number of bytes copied, including those the were already @written
 * * negative error code if nothing was copied
 */
ssize_t generic_file_buffered_read(struct kiocb *iocb,
		struct iov_iter *iter, ssize_t written)
{
	struct file *filp = iocb->ki_filp;
	struct address_space *mapping = filp->f_mapping;
	struct inode *inode = mapping->host;
	struct file_ra_state *ra = &filp->f_ra;
	loff_t *ppos = &iocb->ki_pos;
	pgoff_t index;
	pgoff_t last_index;
	pgoff_t prev_index;
	unsigned long offset;      /* offset into pagecache page */
	unsigned int prev_offset;
	int error = 0;

	if (unlikely(*ppos >= inode->i_sb->s_maxbytes))
		return 0;
	iov_iter_truncate(iter, inode->i_sb->s_maxbytes);

	index = *ppos >> PAGE_SHIFT;
	prev_index = ra->prev_pos >> PAGE_SHIFT;
	prev_offset = ra->prev_pos & (PAGE_SIZE-1);
	last_index = (*ppos + iter->count + PAGE_SIZE-1) >> PAGE_SHIFT;
	offset = *ppos & ~PAGE_MASK;

	/*
	 * If we've already successfully copied some data, then we
	 * can no longer safely return -EIOCBQUEUED. Hence mark
	 * an async read NOWAIT at that point.
	 */
	if (written && (iocb->ki_flags & IOCB_WAITQ))
		iocb->ki_flags |= IOCB_NOWAIT;

	for (;;) {
		struct page *page;
		pgoff_t end_index;
		loff_t isize;
		unsigned long nr, ret;

		cond_resched();
find_page:
		if (fatal_signal_pending(current)) {
			error = -EINTR;
			goto out;
		}

		page = find_get_page(mapping, index);
		if (!page) {
			if (iocb->ki_flags & IOCB_NOIO)
				goto would_block;
			page_cache_sync_readahead(mapping,
					ra, filp,
					index, last_index - index);
			page = find_get_page(mapping, index);
			if (unlikely(page == NULL))
				goto no_cached_page;
		}
		if (PageReadahead(page)) {
			if (iocb->ki_flags & IOCB_NOIO) {
				put_page(page);
				goto out;
			}
			page_cache_async_readahead(mapping,
					ra, filp, page,
					index, last_index - index);
		}
		if (!PageUptodate(page)) {
			/*
			 * See comment in do_read_cache_page on why
			 * wait_on_page_locked is used to avoid unnecessarily
			 * serialisations and why it's safe.
			 */
			if (iocb->ki_flags & IOCB_WAITQ) {
				if (written) {
					put_page(page);
					goto out;
				}
				error = wait_on_page_locked_async(page,
								iocb->ki_waitq);
			} else {
				if (iocb->ki_flags & IOCB_NOWAIT) {
					put_page(page);
					goto would_block;
				}
				error = wait_on_page_locked_killable(page);
			}
			if (unlikely(error))
				goto readpage_error;
			if (PageUptodate(page))
				goto page_ok;

			if (inode->i_blkbits == PAGE_SHIFT ||
					!mapping->a_ops->is_partially_uptodate)
				goto page_not_up_to_date;
			/* pipes can't handle partially uptodate pages */
			if (unlikely(iov_iter_is_pipe(iter)))
				goto page_not_up_to_date;
			if (!trylock_page(page))
				goto page_not_up_to_date;
			/* Did it get truncated before we got the lock? */
			if (!page->mapping)
				goto page_not_up_to_date_locked;
			if (!mapping->a_ops->is_partially_uptodate(page,
							offset, iter->count))
				goto page_not_up_to_date_locked;
			unlock_page(page);
		}
page_ok:
		/*
		 * i_size must be checked after we know the page is Uptodate.
		 *
		 * Checking i_size after the check allows us to calculate
		 * the correct value for "nr", which means the zero-filled
		 * part of the page is not copied back to userspace (unless
		 * another truncate extends the file - this is desired though).
		 */

		isize = i_size_read(inode);
		end_index = (isize - 1) >> PAGE_SHIFT;
		if (unlikely(!isize || index > end_index)) {
			put_page(page);
			goto out;
		}

		/* nr is the maximum number of bytes to copy from this page */
		nr = PAGE_SIZE;
		if (index == end_index) {
			nr = ((isize - 1) & ~PAGE_MASK) + 1;
			if (nr <= offset) {
				put_page(page);
				goto out;
			}
		}
		nr = nr - offset;

		/* If users can be writing to this page using arbitrary
		 * virtual addresses, take care about potential aliasing
		 * before reading the page on the kernel side.
		 */
		if (mapping_writably_mapped(mapping))
			flush_dcache_page(page);

		/*
		 * When a sequential read accesses a page several times,
		 * only mark it as accessed the first time.
		 */
		if (prev_index != index || offset != prev_offset)
			mark_page_accessed(page);
		prev_index = index;

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 */

		ret = copy_page_to_iter(page, offset, nr, iter);
		offset += ret;
		index += offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;
		prev_offset = offset;

		put_page(page);
		written += ret;
		if (!iov_iter_count(iter))
			goto out;
		if (ret < nr) {
			error = -EFAULT;
			goto out;
		}
		continue;

page_not_up_to_date:
		/* Get exclusive access to the page ... */
		if (iocb->ki_flags & IOCB_WAITQ)
			error = lock_page_async(page, iocb->ki_waitq);
		else
			error = lock_page_killable(page);
		if (unlikely(error))
			goto readpage_error;

page_not_up_to_date_locked:
		/* Did it get truncated before we got the lock? */
		if (!page->mapping) {
			unlock_page(page);
			put_page(page);
			continue;
		}

		/* Did somebody else fill it already? */
		if (PageUptodate(page)) {
			unlock_page(page);
			goto page_ok;
		}

readpage:
		if (iocb->ki_flags & (IOCB_NOIO | IOCB_NOWAIT)) {
			unlock_page(page);
			put_page(page);
			goto would_block;
		}
		/*
		 * A previous I/O error may have been due to temporary
		 * failures, eg. multipath errors.
		 * PG_error will be set again if readpage fails.
		 */
		ClearPageError(page);
		/* Start the actual read. The read will unlock the page. */
		error = mapping->a_ops->readpage(filp, page);

		if (unlikely(error)) {
			if (error == AOP_TRUNCATED_PAGE) {
				put_page(page);
				error = 0;
				goto find_page;
			}
			goto readpage_error;
		}

		if (!PageUptodate(page)) {
			if (iocb->ki_flags & IOCB_WAITQ)
				error = lock_page_async(page, iocb->ki_waitq);
			else
				error = lock_page_killable(page);

			if (unlikely(error))
				goto readpage_error;
			if (!PageUptodate(page)) {
				if (page->mapping == NULL) {
					/*
					 * invalidate_mapping_pages got it
					 */
					unlock_page(page);
					put_page(page);
					goto find_page;
				}
				unlock_page(page);
				shrink_readahead_size_eio(ra);
				error = -EIO;
				goto readpage_error;
			}
			unlock_page(page);
		}

		goto page_ok;

readpage_error:
		/* UHHUH! A synchronous read error occurred. Report it */
		put_page(page);
		goto out;

no_cached_page:
		/*
		 * Ok, it wasn't cached, so we need to create a new
		 * page..
		 */
		page = page_cache_alloc(mapping);
		if (!page) {
			error = -ENOMEM;
			goto out;
		}
		error = add_to_page_cache_lru(page, mapping, index,
				mapping_gfp_constraint(mapping, GFP_KERNEL));
		if (error) {
			put_page(page);
			if (error == -EEXIST) {
				error = 0;
				goto find_page;
			}
			goto out;
		}
		goto readpage;
	}

would_block:
	error = -EAGAIN;
out:
	ra->prev_pos = prev_index;
	ra->prev_pos <<= PAGE_SHIFT;
	ra->prev_pos |= prev_offset;

	*ppos = ((loff_t)index << PAGE_SHIFT) + offset;
	file_accessed(filp);
	return written ? written : error;
}
EXPORT_SYMBOL_GPL(generic_file_buffered_read);

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
		goto out; /* skip atime */

	if (iocb->ki_flags & IOCB_DIRECT) {
		struct file *file = iocb->ki_filp;
		struct address_space *mapping = file->f_mapping;
		struct inode *inode = mapping->host;
		loff_t size;

		size = i_size_read(inode);
		if (iocb->ki_flags & IOCB_NOWAIT) {
			if (filemap_range_has_page(mapping, iocb->ki_pos,
						   iocb->ki_pos + count - 1))
				return -EAGAIN;
		} else {
			retval = filemap_write_and_wait_range(mapping,
						iocb->ki_pos,
					        iocb->ki_pos + count - 1);
			if (retval < 0)
				goto out;
		}

		file_accessed(file);

		retval = mapping->a_ops->direct_IO(iocb, iter);
		if (retval >= 0) {
			iocb->ki_pos += retval;
			count -= retval;
		}
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
			goto out;
	}

	retval = generic_file_buffered_read(iocb, iter, retval);
out:
	return retval;
}
EXPORT_SYMBOL(generic_file_read_iter);

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
	DEFINE_READAHEAD(ractl, file, mapping, vmf->pgoff);
	struct file *fpin = NULL;
	unsigned int mmap_miss;

	/* If we don't want any read-ahead, don't bother */
	if (vmf->vma->vm_flags & VM_RAND_READ)
		return fpin;
	if (!ra->ra_pages)
		return fpin;

	if (vmf->vma->vm_flags & VM_SEQ_READ) {
		fpin = maybe_unlock_mmap_for_io(vmf, fpin);
		page_cache_sync_ra(&ractl, ra, ra->ra_pages);
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
	struct file_ra_state *ra = &file->f_ra;
	struct inode *inode = mapping->host;
	pgoff_t offset = vmf->pgoff;
	pgoff_t max_off;
	struct page *page;
	vm_fault_t ret = 0;

	max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(offset >= max_off))
		return VM_FAULT_SIGBUS;

	/*
	 * Do we have something in the page cache already?
	 */
	page = find_get_page(mapping, offset);
	if (likely(page) && !(vmf->flags & FAULT_FLAG_TRIED)) {
		/*
		 * We found the page, so try async readahead before
		 * waiting for the lock.
		 */
		fpin = do_async_mmap_readahead(vmf, page);
	} else if (!page) {
		/* No page in the page cache at all */
		count_vm_event(PGMAJFAULT);
		count_memcg_event_mm(vmf->vma->vm_mm, PGMAJFAULT);
		ret = VM_FAULT_MAJOR;
		fpin = do_sync_mmap_readahead(vmf);
retry_find:
		page = pagecache_get_page(mapping, offset,
					  FGP_CREAT|FGP_FOR_MMAP,
					  vmf->gfp_mask);
		if (!page) {
			if (fpin)
				goto out_retry;
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
	if (unlikely(!PageUptodate(page)))
		goto page_not_uptodate;

	/*
	 * We've made it this far and we had to drop our mmap_lock, now is the
	 * time to return to the upper layer and have it re-find the vma and
	 * redo the fault.
	 */
	if (fpin) {
		unlock_page(page);
		goto out_retry;
	}

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
	ClearPageError(page);
	fpin = maybe_unlock_mmap_for_io(vmf, fpin);
	error = mapping->a_ops->readpage(file, page);
	if (!error) {
		wait_on_page_locked(page);
		if (!PageUptodate(page))
			error = -EIO;
	}
	if (fpin)
		goto out_retry;
	put_page(page);

	if (!error || error == AOP_TRUNCATED_PAGE)
		goto retry_find;

	shrink_readahead_size_eio(ra);
	return VM_FAULT_SIGBUS;

out_retry:
	/*
	 * We dropped the mmap_lock, we need to return to the fault handler to
	 * re-find the vma and come back and find our hopefully still populated
	 * page.
	 */
	if (page)
		put_page(page);
	if (fpin)
		fput(fpin);
	return ret | VM_FAULT_RETRY;
}
EXPORT_SYMBOL(filemap_fault);

void filemap_map_pages(struct vm_fault *vmf,
		pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	pgoff_t last_pgoff = start_pgoff;
	unsigned long max_idx;
	XA_STATE(xas, &mapping->i_pages, start_pgoff);
	struct page *head, *page;
	unsigned int mmap_miss = READ_ONCE(file->f_ra.mmap_miss);

	rcu_read_lock();
	xas_for_each(&xas, head, end_pgoff) {
		if (xas_retry(&xas, head))
			continue;
		if (xa_is_value(head))
			goto next;

		/*
		 * Check for a locked page first, as a speculative
		 * reference may adversely influence page migration.
		 */
		if (PageLocked(head))
			goto next;
		if (!page_cache_get_speculative(head))
			goto next;

		/* Has the page moved or been split? */
		if (unlikely(head != xas_reload(&xas)))
			goto skip;
		page = find_subpage(head, xas.xa_index);

		if (!PageUptodate(head) ||
				PageReadahead(page) ||
				PageHWPoison(page))
			goto skip;
		if (!trylock_page(head))
			goto skip;

		if (head->mapping != mapping || !PageUptodate(head))
			goto unlock;

		max_idx = DIV_ROUND_UP(i_size_read(mapping->host), PAGE_SIZE);
		if (xas.xa_index >= max_idx)
			goto unlock;

		if (mmap_miss > 0)
			mmap_miss--;

		vmf->address += (xas.xa_index - last_pgoff) << PAGE_SHIFT;
		if (vmf->pte)
			vmf->pte += xas.xa_index - last_pgoff;
		last_pgoff = xas.xa_index;
		if (alloc_set_pte(vmf, page))
			goto unlock;
		unlock_page(head);
		goto next;
unlock:
		unlock_page(head);
skip:
		put_page(head);
next:
		/* Huge page is mapped? No need to proceed. */
		if (pmd_trans_huge(*vmf->pmd))
			break;
	}
	rcu_read_unlock();
	WRITE_ONCE(file->f_ra.mmap_miss, mmap_miss);
}
EXPORT_SYMBOL(filemap_map_pages);

vm_fault_t filemap_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	vm_fault_t ret = VM_FAULT_LOCKED;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vmf->vma->vm_file);
	lock_page(page);
	if (page->mapping != inode->i_mapping) {
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
	sb_end_pagefault(inode->i_sb);
	return ret;
}

const struct vm_operations_struct generic_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= filemap_page_mkwrite,
};

/* This is used for a general mmap of a disk file */

int generic_file_mmap(struct file * file, struct vm_area_struct * vma)
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
int generic_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	return -ENOSYS;
}
int generic_file_readonly_mmap(struct file * file, struct vm_area_struct * vma)
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
	struct inode *inode = file_inode(filp);
	char *path;

	errseq_set(&inode->i_mapping->wb_err, -EIO);
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
		if (filemap_range_has_page(inode->i_mapping, pos,
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
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(iov_iter_fault_in_readable(i, bytes))) {
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

		copied = iov_iter_copy_from_user_atomic(page, i, offset, bytes);
		flush_dcache_page(page);

		status = a_ops->write_end(file, mapping, pos, bytes, copied,
						page, fsdata);
		if (unlikely(status < 0))
			break;
		copied = status;

		cond_resched();

		iov_iter_advance(i, copied);
		if (unlikely(copied == 0)) {
			/*
			 * If we were unable to copy any data at all, we must
			 * fall back to a single segment length write.
			 *
			 * If we didn't fallback here, we could livelock
			 * because not all segments in the iov can be copied at
			 * once without a pagefault.
			 */
			bytes = min_t(unsigned long, PAGE_SIZE - offset,
						iov_iter_single_seg_count(i));
			goto again;
		}
		pos += copied;
		written += copied;

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
 * It expects i_mutex to be grabbed unless we work on a block device or similar
 * object which does not need locking at all.
 *
 * This function does *not* take care of syncing data in case of O_SYNC write.
 * A caller has to handle it. This is mainly due to the fact that we want to
 * avoid syncing under i_mutex.
 *
 * Return:
 * * number of bytes written, even for truncated writes
 * * negative error code if no data has been written at all
 */
ssize_t __generic_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space * mapping = file->f_mapping;
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
 * and acquires i_mutex as needed.
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
