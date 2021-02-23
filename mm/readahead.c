// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/readahead.c - address_space-level file readahead.
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 09Apr2002	Andrew Morton
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/pagevec.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/mm_inline.h>
#include <linux/blk-cgroup.h>
#include <linux/fadvise.h>
#include <linux/sched/mm.h>

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

/*
 * see if a page needs releasing upon read_cache_pages() failure
 * - the caller of read_cache_pages() may have set PG_private or PG_fscache
 *   before calling, such as the NFS fs marking pages that are cached locally
 *   on disk, thus we need to give the fs a chance to clean up in the event of
 *   an error
 */
static void read_cache_pages_invalidate_page(struct address_space *mapping,
					     struct page *page)
{
	if (page_has_private(page)) {
		if (!trylock_page(page))
			BUG();
		page->mapping = mapping;
		do_invalidatepage(page, 0, PAGE_SIZE);
		page->mapping = NULL;
		unlock_page(page);
	}
	put_page(page);
}

/*
 * release a list of pages, invalidating them first if need be
 */
static void read_cache_pages_invalidate_pages(struct address_space *mapping,
					      struct list_head *pages)
{
	struct page *victim;

	while (!list_empty(pages)) {
		victim = lru_to_page(pages);
		list_del(&victim->lru);
		read_cache_pages_invalidate_page(mapping, victim);
	}
}

/**
 * read_cache_pages - populate an address space with some pages & start reads against them
 * @mapping: the address_space
 * @pages: The address of a list_head which contains the target pages.  These
 *   pages have their ->index populated and are otherwise uninitialised.
 * @filler: callback routine for filling a single page.
 * @data: private data for the callback routine.
 *
 * Hides the details of the LRU cache etc from the filesystems.
 *
 * Returns: %0 on success, error return by @filler otherwise
 */
int read_cache_pages(struct address_space *mapping, struct list_head *pages,
			int (*filler)(void *, struct page *), void *data)
{
	struct page *page;
	int ret = 0;

	while (!list_empty(pages)) {
		page = lru_to_page(pages);
		list_del(&page->lru);
		if (add_to_page_cache_lru(page, mapping, page->index,
				readahead_gfp_mask(mapping))) {
			read_cache_pages_invalidate_page(mapping, page);
			continue;
		}
		put_page(page);

		ret = filler(data, page);
		if (unlikely(ret)) {
			read_cache_pages_invalidate_pages(mapping, pages);
			break;
		}
		task_io_account_read(PAGE_SIZE);
	}
	return ret;
}

EXPORT_SYMBOL(read_cache_pages);

static void read_pages(struct readahead_control *rac, struct list_head *pages,
		bool skip_page)
{
	const struct address_space_operations *aops = rac->mapping->a_ops;
	struct page *page;
	struct blk_plug plug;

	if (!readahead_count(rac))
		goto out;

	blk_start_plug(&plug);

	if (aops->readahead) {
		aops->readahead(rac);
		/* Clean up the remaining pages */
		while ((page = readahead_page(rac))) {
			unlock_page(page);
			put_page(page);
		}
	} else if (aops->readpages) {
		aops->readpages(rac->file, rac->mapping, pages,
				readahead_count(rac));
		/* Clean up the remaining pages */
		put_pages_list(pages);
		rac->_index += rac->_nr_pages;
		rac->_nr_pages = 0;
	} else {
		while ((page = readahead_page(rac))) {
			aops->readpage(rac->file, page);
			put_page(page);
		}
	}

	blk_finish_plug(&plug);

	BUG_ON(!list_empty(pages));
	BUG_ON(readahead_count(rac));

out:
	if (skip_page)
		rac->_index++;
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
	LIST_HEAD(page_pool);
	gfp_t gfp_mask = readahead_gfp_mask(mapping);
	unsigned long i;

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

	/*
	 * Preallocate as many pages as we will need.
	 */
	for (i = 0; i < nr_to_read; i++) {
		struct page *page = xa_load(&mapping->i_pages, index + i);

		BUG_ON(index + i != ractl->_index + ractl->_nr_pages);

		if (page && !xa_is_value(page)) {
			/*
			 * Page already present?  Kick off the current batch
			 * of contiguous pages before continuing with the
			 * next batch.  This page may be the one we would
			 * have intended to mark as Readahead, but we don't
			 * have a stable reference to this page, and it's
			 * not worth getting one just for that.
			 */
			read_pages(ractl, &page_pool, true);
			continue;
		}

		page = __page_cache_alloc(gfp_mask);
		if (!page)
			break;
		if (mapping->a_ops->readpages) {
			page->index = index + i;
			list_add(&page->lru, &page_pool);
		} else if (add_to_page_cache_lru(page, mapping, index + i,
					gfp_mask) < 0) {
			put_page(page);
			read_pages(ractl, &page_pool, true);
			continue;
		}
		if (i == nr_to_read - lookahead_size)
			SetPageReadahead(page);
		ractl->_nr_pages++;
	}

	/*
	 * Now start the IO.  We ignore I/O errors - if the page is not
	 * uptodate then the caller will launch readpage again, and
	 * will then handle the error.
	 */
	read_pages(ractl, &page_pool, false);
	memalloc_nofs_restore(nofs);
}
EXPORT_SYMBOL_GPL(page_cache_ra_unbounded);

/*
 * do_page_cache_ra() actually reads a chunk of disk.  It allocates
 * the pages first, then submits them for I/O. This avoids the very bad
 * behaviour which would occur if page allocations are causing VM writeback.
 * We really don't want to intermingle reads and writes like that.
 */
void do_page_cache_ra(struct readahead_control *ractl,
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
		struct file_ra_state *ra, unsigned long nr_to_read)
{
	struct address_space *mapping = ractl->mapping;
	struct backing_dev_info *bdi = inode_to_bdi(mapping->host);
	unsigned long max_pages, index;

	if (unlikely(!mapping->a_ops->readpage && !mapping->a_ops->readpages &&
			!mapping->a_ops->readahead))
		return;

	/*
	 * If the request exceeds the readahead window, allow the read to
	 * be up to the optimal hardware IO size
	 */
	index = readahead_index(ractl);
	max_pages = max_t(unsigned long, bdi->io_pages, ra->ra_pages);
	nr_to_read = min_t(unsigned long, nr_to_read, max_pages);
	while (nr_to_read) {
		unsigned long this_chunk = (2 * 1024 * 1024) / PAGE_SIZE;

		if (this_chunk > nr_to_read)
			this_chunk = nr_to_read;
		ractl->_index = index;
		do_page_cache_ra(ractl, this_chunk, 0);

		index += this_chunk;
		nr_to_read -= this_chunk;
	}
}

/*
 * Set the initial window size, round to next power of 2 and square
 * for small size, x 4 for medium, and x 2 for large
 * for 128k (32 page) max ra
 * 1-8 page = 32k initial, > 8 page = 128k initial
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

/*
 * Count contiguously cached pages from @index-1 to @index-@max,
 * this count is a conservative estimation of
 * 	- length of the sequential read sequence, or
 * 	- thrashing threshold in memory tight systems
 */
static pgoff_t count_history_pages(struct address_space *mapping,
				   pgoff_t index, unsigned long max)
{
	pgoff_t head;

	rcu_read_lock();
	head = page_cache_prev_miss(mapping, index - 1, max);
	rcu_read_unlock();

	return index - 1 - head;
}

/*
 * page cache context based read-ahead
 */
static int try_context_readahead(struct address_space *mapping,
				 struct file_ra_state *ra,
				 pgoff_t index,
				 unsigned long req_size,
				 unsigned long max)
{
	pgoff_t size;

	size = count_history_pages(mapping, index, max);

	/*
	 * not enough history pages:
	 * it could be a random read
	 */
	if (size <= req_size)
		return 0;

	/*
	 * starts from beginning of file:
	 * it is a strong indication of long-run stream (or whole-file-read)
	 */
	if (size >= index)
		size *= 2;

	ra->start = index;
	ra->size = min(size + req_size, max);
	ra->async_size = 1;

	return 1;
}

/*
 * A minimal readahead algorithm for trivial sequential/random reads.
 */
static void ondemand_readahead(struct readahead_control *ractl,
		struct file_ra_state *ra, bool hit_readahead_marker,
		unsigned long req_size)
{
	struct backing_dev_info *bdi = inode_to_bdi(ractl->mapping->host);
	unsigned long max_pages = ra->ra_pages;
	unsigned long add_pages;
	unsigned long index = readahead_index(ractl);
	pgoff_t prev_index;

	/*
	 * If the request exceeds the readahead window, allow the read to
	 * be up to the optimal hardware IO size
	 */
	if (req_size > max_pages && bdi->io_pages > max_pages)
		max_pages = min(req_size, bdi->io_pages);

	/*
	 * start of file
	 */
	if (!index)
		goto initial_readahead;

	/*
	 * It's the expected callback index, assume sequential access.
	 * Ramp up sizes, and push forward the readahead window.
	 */
	if ((index == (ra->start + ra->size - ra->async_size) ||
	     index == (ra->start + ra->size))) {
		ra->start += ra->size;
		ra->size = get_next_ra_size(ra, max_pages);
		ra->async_size = ra->size;
		goto readit;
	}

	/*
	 * Hit a marked page without valid readahead state.
	 * E.g. interleaved reads.
	 * Query the pagecache for async_size, which normally equals to
	 * readahead size. Ramp it up and use it as the new readahead size.
	 */
	if (hit_readahead_marker) {
		pgoff_t start;

		rcu_read_lock();
		start = page_cache_next_miss(ractl->mapping, index + 1,
				max_pages);
		rcu_read_unlock();

		if (!start || start - index > max_pages)
			return;

		ra->start = start;
		ra->size = start - index;	/* old async_size */
		ra->size += req_size;
		ra->size = get_next_ra_size(ra, max_pages);
		ra->async_size = ra->size;
		goto readit;
	}

	/*
	 * oversize read
	 */
	if (req_size > max_pages)
		goto initial_readahead;

	/*
	 * sequential cache miss
	 * trivial case: (index - prev_index) == 1
	 * unaligned reads: (index - prev_index) == 0
	 */
	prev_index = (unsigned long long)ra->prev_pos >> PAGE_SHIFT;
	if (index - prev_index <= 1UL)
		goto initial_readahead;

	/*
	 * Query the page cache and look for the traces(cached history pages)
	 * that a sequential stream would leave behind.
	 */
	if (try_context_readahead(ractl->mapping, ra, index, req_size,
			max_pages))
		goto readit;

	/*
	 * standalone, small random read
	 * Read as is, and do not pollute the readahead state.
	 */
	do_page_cache_ra(ractl, req_size, 0);
	return;

initial_readahead:
	ra->start = index;
	ra->size = get_init_ra_size(req_size, max_pages);
	ra->async_size = ra->size > req_size ? ra->size - req_size : ra->size;

readit:
	/*
	 * Will this read hit the readahead marker made by itself?
	 * If so, trigger the readahead marker hit now, and merge
	 * the resulted next readahead window into the current one.
	 * Take care of maximum IO pages as above.
	 */
	if (index == ra->start && ra->size == ra->async_size) {
		add_pages = get_next_ra_size(ra, max_pages);
		if (ra->size + add_pages <= max_pages) {
			ra->async_size = add_pages;
			ra->size += add_pages;
		} else {
			ra->size = max_pages;
			ra->async_size = max_pages >> 1;
		}
	}

	ractl->_index = ra->start;
	do_page_cache_ra(ractl, ra->size, ra->async_size);
}

void page_cache_sync_ra(struct readahead_control *ractl,
		struct file_ra_state *ra, unsigned long req_count)
{
	bool do_forced_ra = ractl->file && (ractl->file->f_mode & FMODE_RANDOM);

	/*
	 * Even if read-ahead is disabled, issue this request as read-ahead
	 * as we'll need it to satisfy the requested range. The forced
	 * read-ahead will do the right thing and limit the read to just the
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
		force_page_cache_ra(ractl, ra, req_count);
		return;
	}

	/* do read-ahead */
	ondemand_readahead(ractl, ra, false, req_count);
}
EXPORT_SYMBOL_GPL(page_cache_sync_ra);

void page_cache_async_ra(struct readahead_control *ractl,
		struct file_ra_state *ra, struct page *page,
		unsigned long req_count)
{
	/* no read-ahead */
	if (!ra->ra_pages)
		return;

	/*
	 * Same bit is used for PG_readahead and PG_reclaim.
	 */
	if (PageWriteback(page))
		return;

	ClearPageReadahead(page);

	/*
	 * Defer asynchronous read-ahead on IO congestion.
	 */
	if (inode_read_congested(ractl->mapping->host))
		return;

	if (blk_cgroup_congested())
		return;

	/* do read-ahead */
	ondemand_readahead(ractl, ra, true, req_count);
}
EXPORT_SYMBOL_GPL(page_cache_async_ra);

ssize_t ksys_readahead(int fd, loff_t offset, size_t count)
{
	ssize_t ret;
	struct fd f;

	ret = -EBADF;
	f = fdget(fd);
	if (!f.file || !(f.file->f_mode & FMODE_READ))
		goto out;

	/*
	 * The readahead() syscall is intended to run only on files
	 * that can execute readahead. If readahead is not possible
	 * on this file, then we must return -EINVAL.
	 */
	ret = -EINVAL;
	if (!f.file->f_mapping || !f.file->f_mapping->a_ops ||
	    !S_ISREG(file_inode(f.file)->i_mode))
		goto out;

	ret = vfs_fadvise(f.file, offset, count, POSIX_FADV_WILLNEED);
out:
	fdput(f);
	return ret;
}

SYSCALL_DEFINE3(readahead, int, fd, loff_t, offset, size_t, count)
{
	return ksys_readahead(fd, offset, count);
}
