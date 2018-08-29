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

static int read_pages(struct address_space *mapping, struct file *filp,
		struct list_head *pages, unsigned int nr_pages, gfp_t gfp)
{
	struct blk_plug plug;
	unsigned page_idx;
	int ret;

	blk_start_plug(&plug);

	if (mapping->a_ops->readpages) {
		ret = mapping->a_ops->readpages(filp, mapping, pages, nr_pages);
		/* Clean up the remaining pages */
		put_pages_list(pages);
		goto out;
	}

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = lru_to_page(pages);
		list_del(&page->lru);
		if (!add_to_page_cache_lru(page, mapping, page->index, gfp))
			mapping->a_ops->readpage(filp, page);
		put_page(page);
	}
	ret = 0;

out:
	blk_finish_plug(&plug);

	return ret;
}

/*
 * __do_page_cache_readahead() actually reads a chunk of disk.  It allocates
 * the pages first, then submits them for I/O. This avoids the very bad
 * behaviour which would occur if page allocations are causing VM writeback.
 * We really don't want to intermingle reads and writes like that.
 *
 * Returns the number of pages requested, or the maximum amount of I/O allowed.
 */
unsigned int __do_page_cache_readahead(struct address_space *mapping,
		struct file *filp, pgoff_t offset, unsigned long nr_to_read,
		unsigned long lookahead_size)
{
	struct inode *inode = mapping->host;
	struct page *page;
	unsigned long end_index;	/* The last page we want to read */
	LIST_HEAD(page_pool);
	int page_idx;
	unsigned int nr_pages = 0;
	loff_t isize = i_size_read(inode);
	gfp_t gfp_mask = readahead_gfp_mask(mapping);

	if (isize == 0)
		goto out;

	end_index = ((isize - 1) >> PAGE_SHIFT);

	/*
	 * Preallocate as many pages as we will need.
	 */
	for (page_idx = 0; page_idx < nr_to_read; page_idx++) {
		pgoff_t page_offset = offset + page_idx;

		if (page_offset > end_index)
			break;

		rcu_read_lock();
		page = radix_tree_lookup(&mapping->i_pages, page_offset);
		rcu_read_unlock();
		if (page && !radix_tree_exceptional_entry(page)) {
			/*
			 * Page already present?  Kick off the current batch of
			 * contiguous pages before continuing with the next
			 * batch.
			 */
			if (nr_pages)
				read_pages(mapping, filp, &page_pool, nr_pages,
						gfp_mask);
			nr_pages = 0;
			continue;
		}

		page = __page_cache_alloc(gfp_mask);
		if (!page)
			break;
		page->index = page_offset;
		list_add(&page->lru, &page_pool);
		if (page_idx == nr_to_read - lookahead_size)
			SetPageReadahead(page);
		nr_pages++;
	}

	/*
	 * Now start the IO.  We ignore I/O errors - if the page is not
	 * uptodate then the caller will launch readpage again, and
	 * will then handle the error.
	 */
	if (nr_pages)
		read_pages(mapping, filp, &page_pool, nr_pages, gfp_mask);
	BUG_ON(!list_empty(&page_pool));
out:
	return nr_pages;
}

/*
 * Chunk the readahead into 2 megabyte units, so that we don't pin too much
 * memory at once.
 */
int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
			       pgoff_t offset, unsigned long nr_to_read)
{
	struct backing_dev_info *bdi = inode_to_bdi(mapping->host);
	struct file_ra_state *ra = &filp->f_ra;
	unsigned long max_pages;

	if (unlikely(!mapping->a_ops->readpage && !mapping->a_ops->readpages))
		return -EINVAL;

	/*
	 * If the request exceeds the readahead window, allow the read to
	 * be up to the optimal hardware IO size
	 */
	max_pages = max_t(unsigned long, bdi->io_pages, ra->ra_pages);
	nr_to_read = min(nr_to_read, max_pages);
	while (nr_to_read) {
		unsigned long this_chunk = (2 * 1024 * 1024) / PAGE_SIZE;

		if (this_chunk > nr_to_read)
			this_chunk = nr_to_read;
		__do_page_cache_readahead(mapping, filp, offset, this_chunk, 0);

		offset += this_chunk;
		nr_to_read -= this_chunk;
	}
	return 0;
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
	unsigned long newsize;

	if (cur < max / 16)
		newsize = 4 * cur;
	else
		newsize = 2 * cur;

	return min(newsize, max);
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
 * Count contiguously cached pages from @offset-1 to @offset-@max,
 * this count is a conservative estimation of
 * 	- length of the sequential read sequence, or
 * 	- thrashing threshold in memory tight systems
 */
static pgoff_t count_history_pages(struct address_space *mapping,
				   pgoff_t offset, unsigned long max)
{
	pgoff_t head;

	rcu_read_lock();
	head = page_cache_prev_hole(mapping, offset - 1, max);
	rcu_read_unlock();

	return offset - 1 - head;
}

/*
 * page cache context based read-ahead
 */
static int try_context_readahead(struct address_space *mapping,
				 struct file_ra_state *ra,
				 pgoff_t offset,
				 unsigned long req_size,
				 unsigned long max)
{
	pgoff_t size;

	size = count_history_pages(mapping, offset, max);

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
	if (size >= offset)
		size *= 2;

	ra->start = offset;
	ra->size = min(size + req_size, max);
	ra->async_size = 1;

	return 1;
}

/*
 * A minimal readahead algorithm for trivial sequential/random reads.
 */
static unsigned long
ondemand_readahead(struct address_space *mapping,
		   struct file_ra_state *ra, struct file *filp,
		   bool hit_readahead_marker, pgoff_t offset,
		   unsigned long req_size)
{
	struct backing_dev_info *bdi = inode_to_bdi(mapping->host);
	unsigned long max_pages = ra->ra_pages;
	unsigned long add_pages;
	pgoff_t prev_offset;

	/*
	 * If the request exceeds the readahead window, allow the read to
	 * be up to the optimal hardware IO size
	 */
	if (req_size > max_pages && bdi->io_pages > max_pages)
		max_pages = min(req_size, bdi->io_pages);

	/*
	 * start of file
	 */
	if (!offset)
		goto initial_readahead;

	/*
	 * It's the expected callback offset, assume sequential access.
	 * Ramp up sizes, and push forward the readahead window.
	 */
	if ((offset == (ra->start + ra->size - ra->async_size) ||
	     offset == (ra->start + ra->size))) {
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
		start = page_cache_next_hole(mapping, offset + 1, max_pages);
		rcu_read_unlock();

		if (!start || start - offset > max_pages)
			return 0;

		ra->start = start;
		ra->size = start - offset;	/* old async_size */
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
	 * trivial case: (offset - prev_offset) == 1
	 * unaligned reads: (offset - prev_offset) == 0
	 */
	prev_offset = (unsigned long long)ra->prev_pos >> PAGE_SHIFT;
	if (offset - prev_offset <= 1UL)
		goto initial_readahead;

	/*
	 * Query the page cache and look for the traces(cached history pages)
	 * that a sequential stream would leave behind.
	 */
	if (try_context_readahead(mapping, ra, offset, req_size, max_pages))
		goto readit;

	/*
	 * standalone, small random read
	 * Read as is, and do not pollute the readahead state.
	 */
	return __do_page_cache_readahead(mapping, filp, offset, req_size, 0);

initial_readahead:
	ra->start = offset;
	ra->size = get_init_ra_size(req_size, max_pages);
	ra->async_size = ra->size > req_size ? ra->size - req_size : ra->size;

readit:
	/*
	 * Will this read hit the readahead marker made by itself?
	 * If so, trigger the readahead marker hit now, and merge
	 * the resulted next readahead window into the current one.
	 * Take care of maximum IO pages as above.
	 */
	if (offset == ra->start && ra->size == ra->async_size) {
		add_pages = get_next_ra_size(ra, max_pages);
		if (ra->size + add_pages <= max_pages) {
			ra->async_size = add_pages;
			ra->size += add_pages;
		} else {
			ra->size = max_pages;
			ra->async_size = max_pages >> 1;
		}
	}

	return ra_submit(ra, mapping, filp);
}

/**
 * page_cache_sync_readahead - generic file readahead
 * @mapping: address_space which holds the pagecache and I/O vectors
 * @ra: file_ra_state which holds the readahead state
 * @filp: passed on to ->readpage() and ->readpages()
 * @offset: start offset into @mapping, in pagecache page-sized units
 * @req_size: hint: total size of the read which the caller is performing in
 *            pagecache pages
 *
 * page_cache_sync_readahead() should be called when a cache miss happened:
 * it will submit the read.  The readahead logic may decide to piggyback more
 * pages onto the read request if access patterns suggest it will improve
 * performance.
 */
void page_cache_sync_readahead(struct address_space *mapping,
			       struct file_ra_state *ra, struct file *filp,
			       pgoff_t offset, unsigned long req_size)
{
	/* no read-ahead */
	if (!ra->ra_pages)
		return;

	if (blk_cgroup_congested())
		return;

	/* be dumb */
	if (filp && (filp->f_mode & FMODE_RANDOM)) {
		force_page_cache_readahead(mapping, filp, offset, req_size);
		return;
	}

	/* do read-ahead */
	ondemand_readahead(mapping, ra, filp, false, offset, req_size);
}
EXPORT_SYMBOL_GPL(page_cache_sync_readahead);

/**
 * page_cache_async_readahead - file readahead for marked pages
 * @mapping: address_space which holds the pagecache and I/O vectors
 * @ra: file_ra_state which holds the readahead state
 * @filp: passed on to ->readpage() and ->readpages()
 * @page: the page at @offset which has the PG_readahead flag set
 * @offset: start offset into @mapping, in pagecache page-sized units
 * @req_size: hint: total size of the read which the caller is performing in
 *            pagecache pages
 *
 * page_cache_async_readahead() should be called when a page is used which
 * has the PG_readahead flag; this is a marker to suggest that the application
 * has used up enough of the readahead window that we should start pulling in
 * more pages.
 */
void
page_cache_async_readahead(struct address_space *mapping,
			   struct file_ra_state *ra, struct file *filp,
			   struct page *page, pgoff_t offset,
			   unsigned long req_size)
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
	if (inode_read_congested(mapping->host))
		return;

	if (blk_cgroup_congested())
		return;

	/* do read-ahead */
	ondemand_readahead(mapping, ra, filp, true, offset, req_size);
}
EXPORT_SYMBOL_GPL(page_cache_async_readahead);

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
