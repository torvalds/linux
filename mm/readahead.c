// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/readahead.c - address_space-level file readahead.
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 09Apr2002	Andrew Morton
 *		Initial version.
 */

/**
 * DOC: Readahead Overview
 *
 * Readahead is used to read content into the page cache before it is
 * explicitly requested by the application.  Readahead only ever
 * attempts to read folios that are not yet in the page cache.  If a
 * folio is present but not up-to-date, readahead will not try to read
 * it. In that case a simple ->read_folio() will be requested.
 *
 * Readahead is triggered when an application read request (whether a
 * system call or a page fault) finds that the requested folio is not in
 * the page cache, or that it is in the page cache and has the
 * readahead flag set.  This flag indicates that the folio was read
 * as part of a previous readahead request and now that it has been
 * accessed, it is time for the next readahead.
 *
 * Each readahead request is partly synchronous read, and partly async
 * readahead.  This is reflected in the struct file_ra_state which
 * contains ->size being the total number of pages, and ->async_size
 * which is the number of pages in the async section.  The readahead
 * flag will be set on the first folio in this async section to trigger
 * a subsequent readahead.  Once a series of sequential reads has been
 * established, there should be no need for a synchronous component and
 * all readahead request will be fully asynchronous.
 *
 * When either of the triggers causes a readahead, three numbers need
 * to be determined: the start of the region to read, the size of the
 * region, and the size of the async tail.
 *
 * The start of the region is simply the first page address at or after
 * the accessed address, which is not currently populated in the page
 * cache.  This is found with a simple search in the page cache.
 *
 * The size of the async tail is determined by subtracting the size that
 * was explicitly requested from the determined request size, unless
 * this would be less than zero - then zero is used.  NOTE THIS
 * CALCULATION IS WRONG WHEN THE START OF THE REGION IS NOT THE ACCESSED
 * PAGE.  ALSO THIS CALCULATION IS NOT USED CONSISTENTLY.
 *
 * The size of the region is normally determined from the size of the
 * previous readahead which loaded the preceding pages.  This may be
 * discovered from the struct file_ra_state for simple sequential reads,
 * or from examining the state of the page cache when multiple
 * sequential reads are interleaved.  Specifically: where the readahead
 * was triggered by the readahead flag, the size of the previous
 * readahead is assumed to be the number of pages from the triggering
 * page to the start of the new readahead.  In these cases, the size of
 * the previous readahead is scaled, often doubled, for the new
 * readahead, though see get_next_ra_size() for details.
 *
 * If the size of the previous read cannot be determined, the number of
 * preceding pages in the page cache is used to estimate the size of
 * a previous read.  This estimate could easily be misled by random
 * reads being coincidentally adjacent, so it is ignored unless it is
 * larger than the current request, and it is not scaled up, unless it
 * is at the start of file.
 *
 * In general readahead is accelerated at the start of the file, as
 * reads from there are often sequential.  There are other minor
 * adjustments to the readahead size in various special cases and these
 * are best discovered by reading the code.
 *
 * The above calculation, based on the previous readahead size,
 * determines the size of the readahead, to which any requested read
 * size may be added.
 *
 * Readahead requests are sent to the filesystem using the ->readahead()
 * address space operation, for which mpage_readahead() is a canonical
 * implementation.  ->readahead() should normally initiate reads on all
 * folios, but may fail to read any or all folios without causing an I/O
 * error.  The page cache reading code will issue a ->read_folio() request
 * for any folio which ->readahead() did not read, and only an error
 * from this will be final.
 *
 * ->readahead() will generally call readahead_folio() repeatedly to get
 * each folio from those prepared for readahead.  It may fail to read a
 * folio by:
 *
 * * not calling readahead_folio() sufficiently many times, effectively
 *   ignoring some folios, as might be appropriate if the path to
 *   storage is congested.
 *
 * * failing to actually submit a read request for a given folio,
 *   possibly due to insufficient resources, or
 *
 * * getting an error during subsequent processing of a request.
 *
 * In the last two cases, the folio should be unlocked by the filesystem
 * to indicate that the read attempt has failed.  In the first case the
 * folio will be unlocked by the VFS.
 *
 * Those folios not in the final ``async_size`` of the request should be
 * considered to be important and ->readahead() should not fail them due
 * to congestion or temporary resource unavailability, but should wait
 * for necessary resources (e.g.  memory or indexing information) to
 * become available.  Folios in the final ``async_size`` may be
 * considered less urgent and failure to read them is more acceptable.
 * In this case it is best to use filemap_remove_folio() to remove the
 * folios from the page cache as is automatically done for folios that
 * were not fetched with readahead_folio().  This will allow a
 * subsequent synchronous readahead request to try them again.  If they
 * are left in the page cache, then they will be read individually using
 * ->read_folio() which may be less efficient.
 */

#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/pagemap.h>
#include <linux/psi.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/mm_inline.h>
#include <linux/blk-cgroup.h>
#include <linux/fadvise.h>
#include <linux/sched/mm.h>

#define CREATE_TRACE_POINTS
#include <trace/events/readahead.h>

#include "internal.h"

/*
 * Initialise a struct file's readahead state.  Assumes that the caller has
 * memset *ra to zero.
 */
void
file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping)
{
	ra->ra_pages = inode_to_bdi(mapping->host)->ra_pages;
	ra->prev_pos = -1;
}
EXPORT_SYMBOL_GPL(file_ra_state_init);

static void read_pages(struct readahead_control *rac)
{
	const struct address_space_operations *aops = rac->mapping->a_ops;
	struct folio *folio;
	struct blk_plug plug;

	if (!readahead_count(rac))
		return;

	if (unlikely(rac->_workingset))
		psi_memstall_enter(&rac->_pflags);
	blk_start_plug(&plug);

	if (aops->readahead) {
		aops->readahead(rac);
		/* Clean up the remaining folios. */
		while ((folio = readahead_folio(rac)) != NULL) {
			folio_get(folio);
			filemap_remove_folio(folio);
			folio_unlock(folio);
			folio_put(folio);
		}
	} else {
		while ((folio = readahead_folio(rac)) != NULL)
			aops->read_folio(rac->file, folio);
	}

	blk_finish_plug(&plug);
	if (unlikely(rac->_workingset))
		psi_memstall_leave(&rac->_pflags);
	rac->_workingset = false;

	BUG_ON(readahead_count(rac));
}

static struct folio *ractl_alloc_folio(struct readahead_control *ractl,
				       gfp_t gfp_mask, unsigned int order)
{
	struct folio *folio;

	folio = filemap_alloc_folio(gfp_mask, order);
	if (folio && ractl->dropbehind)
		__folio_set_dropbehind(folio);

	return folio;
}

/**
 * page_cache_ra_unbounded - Start unchecked readahead.
 * @ractl: Readahead control.
 * @nr_to_read: The number of pages to read.
 * @lookahead_size: Where to start the next readahead.
 *
 * This function is for filesystems to call when they want to start
 * readahead beyond a file's stated i_size.  This is almost certainly
 * not the function you want to call.  Use page_cache_async_readahead()
 * or page_cache_sync_readahead() instead.
 *
 * Context: File is referenced by caller.  Mutexes may be held by caller.
 * May sleep, but will not reenter filesystem to reclaim memory.
 */
void page_cache_ra_unbounded(struct readahead_control *ractl,
		unsigned long nr_to_read, unsigned long lookahead_size)
{
	struct address_space *mapping = ractl->mapping;
	unsigned long index = readahead_index(ractl);
	gfp_t gfp_mask = readahead_gfp_mask(mapping);
	unsigned long mark = ULONG_MAX, i = 0;
	unsigned int min_nrpages = mapping_min_folio_nrpages(mapping);

	/*
	 * Partway through the readahead operation, we will have added
	 * locked pages to the page cache, but will not yet have submitted
	 * them for I/O.  Adding another page may need to allocate memory,
	 * which can trigger memory reclaim.  Telling the VM we're in
	 * the middle of a filesystem operation will cause it to not
	 * touch file-backed pages, preventing a deadlock.  Most (all?)
	 * filesystems already specify __GFP_NOFS in their mapping's
	 * gfp_mask, but let's be explicit here.
	 */
	unsigned int nofs = memalloc_nofs_save();

	trace_page_cache_ra_unbounded(mapping->host, index, nr_to_read,
				      lookahead_size);
	filemap_invalidate_lock_shared(mapping);
	index = mapping_align_index(mapping, index);

	/*
	 * As iterator `i` is aligned to min_nrpages, round_up the
	 * difference between nr_to_read and lookahead_size to mark the
	 * index that only has lookahead or "async_region" to set the
	 * readahead flag.
	 */
	if (lookahead_size <= nr_to_read) {
		unsigned long ra_folio_index;

		ra_folio_index = round_up(readahead_index(ractl) +
					  nr_to_read - lookahead_size,
					  min_nrpages);
		mark = ra_folio_index - index;
	}
	nr_to_read += readahead_index(ractl) - index;
	ractl->_index = index;

	/*
	 * Preallocate as many pages as we will need.
	 */
	while (i < nr_to_read) {
		struct folio *folio = xa_load(&mapping->i_pages, index + i);
		int ret;

		if (folio && !xa_is_value(folio)) {
			/*
			 * Page already present?  Kick off the current batch
			 * of contiguous pages before continuing with the
			 * next batch.  This page may be the one we would
			 * have intended to mark as Readahead, but we don't
			 * have a stable reference to this page, and it's
			 * not worth getting one just for that.
			 */
			read_pages(ractl);
			ractl->_index += min_nrpages;
			i = ractl->_index + ractl->_nr_pages - index;
			continue;
		}

		folio = ractl_alloc_folio(ractl, gfp_mask,
					mapping_min_folio_order(mapping));
		if (!folio)
			break;

		ret = filemap_add_folio(mapping, folio, index + i, gfp_mask);
		if (ret < 0) {
			folio_put(folio);
			if (ret == -ENOMEM)
				break;
			read_pages(ractl);
			ractl->_index += min_nrpages;
			i = ractl->_index + ractl->_nr_pages - index;
			continue;
		}
		if (i == mark)
			folio_set_readahead(folio);
		ractl->_workingset |= folio_test_workingset(folio);
		ractl->_nr_pages += min_nrpages;
		i += min_nrpages;
	}

	/*
	 * Now start the IO.  We ignore I/O errors - if the folio is not
	 * uptodate then the caller will launch read_folio again, and
	 * will then handle the error.
	 */
	read_pages(ractl);
	filemap_invalidate_unlock_shared(mapping);
	memalloc_nofs_restore(nofs);
}
EXPORT_SYMBOL_GPL(page_cache_ra_unbounded);

/*
 * do_page_cache_ra() actually reads a chunk of disk.  It allocates
 * the pages first, then submits them for I/O. This avoids the very bad
 * behaviour which would occur if page allocations are causing VM writeback.
 * We really don't want to intermingle reads and writes like that.
 */
static void do_page_cache_ra(struct readahead_control *ractl,
		unsigned long nr_to_read, unsigned long lookahead_size)
{
	struct inode *inode = ractl->mapping->host;
	unsigned long index = readahead_index(ractl);
	loff_t isize = i_size_read(inode);
	pgoff_t end_index;	/* The last page we want to read */

	if (isize == 0)
		return;

	end_index = (isize - 1) >> PAGE_SHIFT;
	if (index > end_index)
		return;
	/* Don't read past the page containing the last byte of the file */
	if (nr_to_read > end_index - index)
		nr_to_read = end_index - index + 1;

	page_cache_ra_unbounded(ractl, nr_to_read, lookahead_size);
}

/*
 * Chunk the readahead into 2 megabyte units, so that we don't pin too much
 * memory at once.
 */
void force_page_cache_ra(struct readahead_control *ractl,
		unsigned long nr_to_read)
{
	struct address_space *mapping = ractl->mapping;
	struct file_ra_state *ra = ractl->ra;
	struct backing_dev_info *bdi = inode_to_bdi(mapping->host);
	unsigned long max_pages;

	if (unlikely(!mapping->a_ops->read_folio && !mapping->a_ops->readahead))
		return;

	/*
	 * If the request exceeds the readahead window, allow the read to
	 * be up to the optimal hardware IO size
	 */
	max_pages = max_t(unsigned long, bdi->io_pages, ra->ra_pages);
	nr_to_read = min_t(unsigned long, nr_to_read, max_pages);
	while (nr_to_read) {
		unsigned long this_chunk = (2 * 1024 * 1024) / PAGE_SIZE;

		if (this_chunk > nr_to_read)
			this_chunk = nr_to_read;
		do_page_cache_ra(ractl, this_chunk, 0);

		nr_to_read -= this_chunk;
	}
}

/*
 * Set the initial window size, round to next power of 2 and square
 * for small size, x 4 for medium, and x 2 for large
 * for 128k (32 page) max ra
 * 1-2 page = 16k, 3-4 page 32k, 5-8 page = 64k, > 8 page = 128k initial
 */
static unsigned long get_init_ra_size(unsigned long size, unsigned long max)
{
	unsigned long newsize = roundup_pow_of_two(size);

	if (newsize <= max / 32)
		newsize = newsize * 4;
	else if (newsize <= max / 4)
		newsize = newsize * 2;
	else
		newsize = max;

	return newsize;
}

/*
 *  Get the previous window size, ramp it up, and
 *  return it as the new window size.
 */
static unsigned long get_next_ra_size(struct file_ra_state *ra,
				      unsigned long max)
{
	unsigned long cur = ra->size;

	if (cur < max / 16)
		return 4 * cur;
	if (cur <= max / 2)
		return 2 * cur;
	return max;
}

/*
 * On-demand readahead design.
 *
 * The fields in struct file_ra_state represent the most-recently-executed
 * readahead attempt:
 *
 *                        |<----- async_size ---------|
 *     |------------------- size -------------------->|
 *     |==================#===========================|
 *     ^start             ^page marked with PG_readahead
 *
 * To overlap application thinking time and disk I/O time, we do
 * `readahead pipelining': Do not wait until the application consumed all
 * readahead pages and stalled on the missing page at readahead_index;
 * Instead, submit an asynchronous readahead I/O as soon as there are
 * only async_size pages left in the readahead window. Normally async_size
 * will be equal to size, for maximum pipelining.
 *
 * In interleaved sequential reads, concurrent streams on the same fd can
 * be invalidating each other's readahead state. So we flag the new readahead
 * page at (start+size-async_size) with PG_readahead, and use it as readahead
 * indicator. The flag won't be set on already cached pages, to avoid the
 * readahead-for-nothing fuss, saving pointless page cache lookups.
 *
 * prev_pos tracks the last visited byte in the _previous_ read request.
 * It should be maintained by the caller, and will be used for detecting
 * small random reads. Note that the readahead algorithm checks loosely
 * for sequential patterns. Hence interleaved reads might be served as
 * sequential ones.
 *
 * There is a special-case: if the first page which the application tries to
 * read happens to be the first page of the file, it is assumed that a linear
 * read is about to happen and the window is immediately set to the initial size
 * based on I/O request size and the max_readahead.
 *
 * The code ramps up the readahead size aggressively at first, but slow down as
 * it approaches max_readhead.
 */

static inline int ra_alloc_folio(struct readahead_control *ractl, pgoff_t index,
		pgoff_t mark, unsigned int order, gfp_t gfp)
{
	int err;
	struct folio *folio = ractl_alloc_folio(ractl, gfp, order);

	if (!folio)
		return -ENOMEM;
	mark = round_down(mark, 1UL << order);
	if (index == mark)
		folio_set_readahead(folio);
	err = filemap_add_folio(ractl->mapping, folio, index, gfp);
	if (err) {
		folio_put(folio);
		return err;
	}

	ractl->_nr_pages += 1UL << order;
	ractl->_workingset |= folio_test_workingset(folio);
	return 0;
}

void page_cache_ra_order(struct readahead_control *ractl,
		struct file_ra_state *ra)
{
	struct address_space *mapping = ractl->mapping;
	pgoff_t start = readahead_index(ractl);
	pgoff_t index = start;
	unsigned int min_order = mapping_min_folio_order(mapping);
	pgoff_t limit = (i_size_read(mapping->host) - 1) >> PAGE_SHIFT;
	pgoff_t mark = index + ra->size - ra->async_size;
	unsigned int nofs;
	int err = 0;
	gfp_t gfp = readahead_gfp_mask(mapping);
	unsigned int new_order = ra->order;

	trace_page_cache_ra_order(mapping->host, start, ra);
	if (!mapping_large_folio_support(mapping)) {
		ra->order = 0;
		goto fallback;
	}

	limit = min(limit, index + ra->size - 1);

	new_order = min(mapping_max_folio_order(mapping), new_order);
	new_order = min_t(unsigned int, new_order, ilog2(ra->size));
	new_order = max(new_order, min_order);

	ra->order = new_order;

	/* See comment in page_cache_ra_unbounded() */
	nofs = memalloc_nofs_save();
	filemap_invalidate_lock_shared(mapping);
	/*
	 * If the new_order is greater than min_order and index is
	 * already aligned to new_order, then this will be noop as index
	 * aligned to new_order should also be aligned to min_order.
	 */
	ractl->_index = mapping_align_index(mapping, index);
	index = readahead_index(ractl);

	while (index <= limit) {
		unsigned int order = new_order;

		/* Align with smaller pages if needed */
		if (index & ((1UL << order) - 1))
			order = __ffs(index);
		/* Don't allocate pages past EOF */
		while (order > min_order && index + (1UL << order) - 1 > limit)
			order--;
		err = ra_alloc_folio(ractl, index, mark, order, gfp);
		if (err)
			break;
		index += 1UL << order;
	}

	read_pages(ractl);
	filemap_invalidate_unlock_shared(mapping);
	memalloc_nofs_restore(nofs);

	/*
	 * If there were already pages in the page cache, then we may have
	 * left some gaps.  Let the regular readahead code take care of this
	 * situation below.
	 */
	if (!err)
		return;
fallback:
	/*
	 * ->readahead() may have updated readahead window size so we have to
	 * check there's still something to read.
	 */
	if (ra->size > index - start)
		do_page_cache_ra(ractl, ra->size - (index - start),
				 ra->async_size);
}

static unsigned long ractl_max_pages(struct readahead_control *ractl,
		unsigned long req_size)
{
	struct backing_dev_info *bdi = inode_to_bdi(ractl->mapping->host);
	unsigned long max_pages = ractl->ra->ra_pages;

	/*
	 * If the request exceeds the readahead window, allow the read to
	 * be up to the optimal hardware IO size
	 */
	if (req_size > max_pages && bdi->io_pages > max_pages)
		max_pages = min(req_size, bdi->io_pages);
	return max_pages;
}

void page_cache_sync_ra(struct readahead_control *ractl,
		unsigned long req_count)
{
	pgoff_t index = readahead_index(ractl);
	bool do_forced_ra = ractl->file && (ractl->file->f_mode & FMODE_RANDOM);
	struct file_ra_state *ra = ractl->ra;
	unsigned long max_pages, contig_count;
	pgoff_t prev_index, miss;

	trace_page_cache_sync_ra(ractl->mapping->host, index, ra, req_count);
	/*
	 * Even if readahead is disabled, issue this request as readahead
	 * as we'll need it to satisfy the requested range. The forced
	 * readahead will do the right thing and limit the read to just the
	 * requested range, which we'll set to 1 page for this case.
	 */
	if (!ra->ra_pages || blk_cgroup_congested()) {
		if (!ractl->file)
			return;
		req_count = 1;
		do_forced_ra = true;
	}

	/* be dumb */
	if (do_forced_ra) {
		force_page_cache_ra(ractl, req_count);
		return;
	}

	max_pages = ractl_max_pages(ractl, req_count);
	prev_index = (unsigned long long)ra->prev_pos >> PAGE_SHIFT;
	/*
	 * A start of file, oversized read, or sequential cache miss:
	 * trivial case: (index - prev_index) == 1
	 * unaligned reads: (index - prev_index) == 0
	 */
	if (!index || req_count > max_pages || index - prev_index <= 1UL) {
		ra->start = index;
		ra->size = get_init_ra_size(req_count, max_pages);
		ra->async_size = ra->size > req_count ? ra->size - req_count :
							ra->size >> 1;
		goto readit;
	}

	/*
	 * Query the page cache and look for the traces(cached history pages)
	 * that a sequential stream would leave behind.
	 */
	rcu_read_lock();
	miss = page_cache_prev_miss(ractl->mapping, index - 1, max_pages);
	rcu_read_unlock();
	contig_count = index - miss - 1;
	/*
	 * Standalone, small random read. Read as is, and do not pollute the
	 * readahead state.
	 */
	if (contig_count <= req_count) {
		do_page_cache_ra(ractl, req_count, 0);
		return;
	}
	/*
	 * File cached from the beginning:
	 * it is a strong indication of long-run stream (or whole-file-read)
	 */
	if (miss == ULONG_MAX)
		contig_count *= 2;
	ra->start = index;
	ra->size = min(contig_count + req_count, max_pages);
	ra->async_size = 1;
readit:
	ra->order = 0;
	ractl->_index = ra->start;
	page_cache_ra_order(ractl, ra);
}
EXPORT_SYMBOL_GPL(page_cache_sync_ra);

void page_cache_async_ra(struct readahead_control *ractl,
		struct folio *folio, unsigned long req_count)
{
	unsigned long max_pages;
	struct file_ra_state *ra = ractl->ra;
	pgoff_t index = readahead_index(ractl);
	pgoff_t expected, start, end, aligned_end, align;

	/* no readahead */
	if (!ra->ra_pages)
		return;

	/*
	 * Same bit is used for PG_readahead and PG_reclaim.
	 */
	if (folio_test_writeback(folio))
		return;

	trace_page_cache_async_ra(ractl->mapping->host, index, ra, req_count);
	folio_clear_readahead(folio);

	if (blk_cgroup_congested())
		return;

	max_pages = ractl_max_pages(ractl, req_count);
	/*
	 * It's the expected callback index, assume sequential access.
	 * Ramp up sizes, and push forward the readahead window.
	 */
	expected = round_down(ra->start + ra->size - ra->async_size,
			folio_nr_pages(folio));
	if (index == expected) {
		ra->start += ra->size;
		/*
		 * In the case of MADV_HUGEPAGE, the actual size might exceed
		 * the readahead window.
		 */
		ra->size = max(ra->size, get_next_ra_size(ra, max_pages));
		goto readit;
	}

	/*
	 * Hit a marked folio without valid readahead state.
	 * E.g. interleaved reads.
	 * Query the pagecache for async_size, which normally equals to
	 * readahead size. Ramp it up and use it as the new readahead size.
	 */
	rcu_read_lock();
	start = page_cache_next_miss(ractl->mapping, index + 1, max_pages);
	rcu_read_unlock();

	if (!start || start - index > max_pages)
		return;

	ra->start = start;
	ra->size = start - index;	/* old async_size */
	ra->size += req_count;
	ra->size = get_next_ra_size(ra, max_pages);
readit:
	ra->order += 2;
	align = 1UL << min(ra->order, ffs(max_pages) - 1);
	end = ra->start + ra->size;
	aligned_end = round_down(end, align);
	if (aligned_end > ra->start)
		ra->size -= end - aligned_end;
	ra->async_size = ra->size;
	ractl->_index = ra->start;
	page_cache_ra_order(ractl, ra);
}
EXPORT_SYMBOL_GPL(page_cache_async_ra);

ssize_t ksys_readahead(int fd, loff_t offset, size_t count)
{
	struct file *file;
	const struct inode *inode;

	CLASS(fd, f)(fd);
	if (fd_empty(f))
		return -EBADF;

	file = fd_file(f);
	if (!(file->f_mode & FMODE_READ))
		return -EBADF;

	/*
	 * The readahead() syscall is intended to run only on files
	 * that can execute readahead. If readahead is not possible
	 * on this file, then we must return -EINVAL.
	 */
	if (!file->f_mapping)
		return -EINVAL;
	if (!file->f_mapping->a_ops)
		return -EINVAL;

	inode = file_inode(file);
	if (!S_ISREG(inode->i_mode) && !S_ISBLK(inode->i_mode))
		return -EINVAL;
	if (IS_ANON_FILE(inode))
		return -EINVAL;

	return vfs_fadvise(fd_file(f), offset, count, POSIX_FADV_WILLNEED);
}

SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return ksys_readahead(fd, offset, count);
}

#if defined(CONFIG_COMPAT) && defined(__ARCH_WANT_COMPAT_READAHEAD)
COMPAT_SYSCALL_DEFINE4(readahead, int, fd, compat_arg_u64_dual(offset), size_t, count)
{
	return ksys_readahead(fd, compat_arg_u64_glue(offset), count);
}
#endif

/**
 * readahead_expand - Expand a readahead request
 * @ractl: The request to be expanded
 * @new_start: The revised start
 * @new_len: The revised size of the request
 *
 * Attempt to expand a readahead request outwards from the current size to the
 * specified size by inserting locked pages before and after the current window
 * to increase the size to the new window.  This may involve the insertion of
 * THPs, in which case the window may get expanded even beyond what was
 * requested.
 *
 * The algorithm will stop if it encounters a conflicting page already in the
 * pagecache and leave a smaller expansion than requested.
 *
 * The caller must check for this by examining the revised @ractl object for a
 * different expansion than was requested.
 */
void readahead_expand(struct readahead_control *ractl,
		      loff_t new_start, size_t new_len)
{
	struct address_space *mapping = ractl->mapping;
	struct file_ra_state *ra = ractl->ra;
	pgoff_t new_index, new_nr_pages;
	gfp_t gfp_mask = readahead_gfp_mask(mapping);
	unsigned long min_nrpages = mapping_min_folio_nrpages(mapping);
	unsigned int min_order = mapping_min_folio_order(mapping);

	new_index = new_start / PAGE_SIZE;
	/*
	 * Readahead code should have aligned the ractl->_index to
	 * min_nrpages before calling readahead aops.
	 */
	VM_BUG_ON(!IS_ALIGNED(ractl->_index, min_nrpages));

	/* Expand the leading edge downwards */
	while (ractl->_index > new_index) {
		unsigned long index = ractl->_index - 1;
		struct folio *folio = xa_load(&mapping->i_pages, index);

		if (folio && !xa_is_value(folio))
			return; /* Folio apparently present */

		folio = ractl_alloc_folio(ractl, gfp_mask, min_order);
		if (!folio)
			return;

		index = mapping_align_index(mapping, index);
		if (filemap_add_folio(mapping, folio, index, gfp_mask) < 0) {
			folio_put(folio);
			return;
		}
		if (unlikely(folio_test_workingset(folio)) &&
				!ractl->_workingset) {
			ractl->_workingset = true;
			psi_memstall_enter(&ractl->_pflags);
		}
		ractl->_nr_pages += min_nrpages;
		ractl->_index = folio->index;
	}

	new_len += new_start - readahead_pos(ractl);
	new_nr_pages = DIV_ROUND_UP(new_len, PAGE_SIZE);

	/* Expand the trailing edge upwards */
	while (ractl->_nr_pages < new_nr_pages) {
		unsigned long index = ractl->_index + ractl->_nr_pages;
		struct folio *folio = xa_load(&mapping->i_pages, index);

		if (folio && !xa_is_value(folio))
			return; /* Folio apparently present */

		folio = ractl_alloc_folio(ractl, gfp_mask, min_order);
		if (!folio)
			return;

		index = mapping_align_index(mapping, index);
		if (filemap_add_folio(mapping, folio, index, gfp_mask) < 0) {
			folio_put(folio);
			return;
		}
		if (unlikely(folio_test_workingset(folio)) &&
				!ractl->_workingset) {
			ractl->_workingset = true;
			psi_memstall_enter(&ractl->_pflags);
		}
		ractl->_nr_pages += min_nrpages;
		if (ra) {
			ra->size += min_nrpages;
			ra->async_size += min_nrpages;
		}
	}
}
EXPORT_SYMBOL(readahead_expand);
