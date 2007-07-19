/*
 * mm/readahead.c - address_space-level file readahead.
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 09Apr2002	akpm@zip.com.au
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/pagevec.h>

void default_unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
}
EXPORT_SYMBOL(default_unplug_io_fn);

/*
 * Convienent macros for min/max read-ahead pages.
 * Note that MAX_RA_PAGES is rounded down, while MIN_RA_PAGES is rounded up.
 * The latter is necessary for systems with large page size(i.e. 64k).
 */
#define MAX_RA_PAGES	(VM_MAX_READAHEAD*1024 / PAGE_CACHE_SIZE)
#define MIN_RA_PAGES	DIV_ROUND_UP(VM_MIN_READAHEAD*1024, PAGE_CACHE_SIZE)

struct backing_dev_info default_backing_dev_info = {
	.ra_pages	= MAX_RA_PAGES,
	.state		= 0,
	.capabilities	= BDI_CAP_MAP_COPY,
	.unplug_io_fn	= default_unplug_io_fn,
};
EXPORT_SYMBOL_GPL(default_backing_dev_info);

/*
 * Initialise a struct file's readahead state.  Assumes that the caller has
 * memset *ra to zero.
 */
void
file_ra_state_init(struct file_ra_state *ra, struct address_space *mapping)
{
	ra->ra_pages = mapping->backing_dev_info->ra_pages;
	ra->prev_index = -1;
}
EXPORT_SYMBOL_GPL(file_ra_state_init);

#define list_to_page(head) (list_entry((head)->prev, struct page, lru))

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
	struct pagevec lru_pvec;
	int ret = 0;

	pagevec_init(&lru_pvec, 0);

	while (!list_empty(pages)) {
		page = list_to_page(pages);
		list_del(&page->lru);
		if (add_to_page_cache(page, mapping, page->index, GFP_KERNEL)) {
			page_cache_release(page);
			continue;
		}
		ret = filler(data, page);
		if (!pagevec_add(&lru_pvec, page))
			__pagevec_lru_add(&lru_pvec);
		if (ret) {
			put_pages_list(pages);
			break;
		}
		task_io_account_read(PAGE_CACHE_SIZE);
	}
	pagevec_lru_add(&lru_pvec);
	return ret;
}

EXPORT_SYMBOL(read_cache_pages);

static int read_pages(struct address_space *mapping, struct file *filp,
		struct list_head *pages, unsigned nr_pages)
{
	unsigned page_idx;
	struct pagevec lru_pvec;
	int ret;

	if (mapping->a_ops->readpages) {
		ret = mapping->a_ops->readpages(filp, mapping, pages, nr_pages);
		/* Clean up the remaining pages */
		put_pages_list(pages);
		goto out;
	}

	pagevec_init(&lru_pvec, 0);
	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_to_page(pages);
		list_del(&page->lru);
		if (!add_to_page_cache(page, mapping,
					page->index, GFP_KERNEL)) {
			mapping->a_ops->readpage(filp, page);
			if (!pagevec_add(&lru_pvec, page))
				__pagevec_lru_add(&lru_pvec);
		} else
			page_cache_release(page);
	}
	pagevec_lru_add(&lru_pvec);
	ret = 0;
out:
	return ret;
}

/*
 * do_page_cache_readahead actually reads a chunk of disk.  It allocates all
 * the pages first, then submits them all for I/O. This avoids the very bad
 * behaviour which would occur if page allocations are causing VM writeback.
 * We really don't want to intermingle reads and writes like that.
 *
 * Returns the number of pages requested, or the maximum amount of I/O allowed.
 *
 * do_page_cache_readahead() returns -1 if it encountered request queue
 * congestion.
 */
static int
__do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			pgoff_t offset, unsigned long nr_to_read,
			unsigned long lookahead_size)
{
	struct inode *inode = mapping->host;
	struct page *page;
	unsigned long end_index;	/* The last page we want to read */
	LIST_HEAD(page_pool);
	int page_idx;
	int ret = 0;
	loff_t isize = i_size_read(inode);

	if (isize == 0)
		goto out;

	end_index = ((isize - 1) >> PAGE_CACHE_SHIFT);

	/*
	 * Preallocate as many pages as we will need.
	 */
	read_lock_irq(&mapping->tree_lock);
	for (page_idx = 0; page_idx < nr_to_read; page_idx++) {
		pgoff_t page_offset = offset + page_idx;

		if (page_offset > end_index)
			break;

		page = radix_tree_lookup(&mapping->page_tree, page_offset);
		if (page)
			continue;

		read_unlock_irq(&mapping->tree_lock);
		page = page_cache_alloc_cold(mapping);
		read_lock_irq(&mapping->tree_lock);
		if (!page)
			break;
		page->index = page_offset;
		list_add(&page->lru, &page_pool);
		if (page_idx == nr_to_read - lookahead_size)
			SetPageReadahead(page);
		ret++;
	}
	read_unlock_irq(&mapping->tree_lock);

	/*
	 * Now start the IO.  We ignore I/O errors - if the page is not
	 * uptodate then the caller will launch readpage again, and
	 * will then handle the error.
	 */
	if (ret)
		read_pages(mapping, filp, &page_pool, ret);
	BUG_ON(!list_empty(&page_pool));
out:
	return ret;
}

/*
 * Chunk the readahead into 2 megabyte units, so that we don't pin too much
 * memory at once.
 */
int force_page_cache_readahead(struct address_space *mapping, struct file *filp,
		pgoff_t offset, unsigned long nr_to_read)
{
	int ret = 0;

	if (unlikely(!mapping->a_ops->readpage && !mapping->a_ops->readpages))
		return -EINVAL;

	while (nr_to_read) {
		int err;

		unsigned long this_chunk = (2 * 1024 * 1024) / PAGE_CACHE_SIZE;

		if (this_chunk > nr_to_read)
			this_chunk = nr_to_read;
		err = __do_page_cache_readahead(mapping, filp,
						offset, this_chunk, 0);
		if (err < 0) {
			ret = err;
			break;
		}
		ret += err;
		offset += this_chunk;
		nr_to_read -= this_chunk;
	}
	return ret;
}

/*
 * This version skips the IO if the queue is read-congested, and will tell the
 * block layer to abandon the readahead if request allocation would block.
 *
 * force_page_cache_readahead() will ignore queue congestion and will block on
 * request queues.
 */
int do_page_cache_readahead(struct address_space *mapping, struct file *filp,
			pgoff_t offset, unsigned long nr_to_read)
{
	if (bdi_read_congested(mapping->backing_dev_info))
		return -1;

	return __do_page_cache_readahead(mapping, filp, offset, nr_to_read, 0);
}

/*
 * Given a desired number of PAGE_CACHE_SIZE readahead pages, return a
 * sensible upper limit.
 */
unsigned long max_sane_readahead(unsigned long nr)
{
	return min(nr, (node_page_state(numa_node_id(), NR_INACTIVE)
		+ node_page_state(numa_node_id(), NR_FREE_PAGES)) / 2);
}

/*
 * Submit IO for the read-ahead request in file_ra_state.
 */
unsigned long ra_submit(struct file_ra_state *ra,
		       struct address_space *mapping, struct file *filp)
{
	unsigned long ra_size;
	unsigned long la_size;
	int actual;

	ra_size = ra_readahead_size(ra);
	la_size = ra_lookahead_size(ra);
	actual = __do_page_cache_readahead(mapping, filp,
					ra->ra_index, ra_size, la_size);

	return actual;
}
EXPORT_SYMBOL_GPL(ra_submit);

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
	unsigned long cur = ra->readahead_index - ra->ra_index;
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
 *                    |-------- last readahead window -------->|
 *       |-- application walking here -->|
 * ======#============|==================#=====================|
 *       ^la_index    ^ra_index          ^lookahead_index      ^readahead_index
 *
 * [ra_index, readahead_index) represents the last readahead window.
 *
 * [la_index, lookahead_index] is where the application would be walking(in
 * the common case of cache-cold sequential reads): the last window was
 * established when the application was at la_index, and the next window will
 * be bring in when the application reaches lookahead_index.
 *
 * To overlap application thinking time and disk I/O time, we do
 * `readahead pipelining': Do not wait until the application consumed all
 * readahead pages and stalled on the missing page at readahead_index;
 * Instead, submit an asynchronous readahead I/O as early as the application
 * reads on the page at lookahead_index. Normally lookahead_index will be
 * equal to ra_index, for maximum pipelining.
 *
 * In interleaved sequential reads, concurrent streams on the same fd can
 * be invalidating each other's readahead state. So we flag the new readahead
 * page at lookahead_index with PG_readahead, and use it as readahead
 * indicator. The flag won't be set on already cached pages, to avoid the
 * readahead-for-nothing fuss, saving pointless page cache lookups.
 *
 * prev_index tracks the last visited page in the _previous_ read request.
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
 * A minimal readahead algorithm for trivial sequential/random reads.
 */
static unsigned long
ondemand_readahead(struct address_space *mapping,
		   struct file_ra_state *ra, struct file *filp,
		   struct page *page, pgoff_t offset,
		   unsigned long req_size)
{
	unsigned long max;	/* max readahead pages */
	pgoff_t ra_index;	/* readahead index */
	unsigned long ra_size;	/* readahead size */
	unsigned long la_size;	/* lookahead size */
	int sequential;

	max = ra->ra_pages;
	sequential = (offset - ra->prev_index <= 1UL) || (req_size > max);

	/*
	 * Lookahead/readahead hit, assume sequential access.
	 * Ramp up sizes, and push forward the readahead window.
	 */
	if (offset && (offset == ra->lookahead_index ||
			offset == ra->readahead_index)) {
		ra_index = ra->readahead_index;
		ra_size = get_next_ra_size(ra, max);
		la_size = ra_size;
		goto fill_ra;
	}

	/*
	 * Standalone, small read.
	 * Read as is, and do not pollute the readahead state.
	 */
	if (!page && !sequential) {
		return __do_page_cache_readahead(mapping, filp,
						offset, req_size, 0);
	}

	/*
	 * It may be one of
	 * 	- first read on start of file
	 * 	- sequential cache miss
	 * 	- oversize random read
	 * Start readahead for it.
	 */
	ra_index = offset;
	ra_size = get_init_ra_size(req_size, max);
	la_size = ra_size > req_size ? ra_size - req_size : ra_size;

	/*
	 * Hit on a lookahead page without valid readahead state.
	 * E.g. interleaved reads.
	 * Not knowing its readahead pos/size, bet on the minimal possible one.
	 */
	if (page) {
		ra_index++;
		ra_size = min(4 * ra_size, max);
	}

fill_ra:
	ra_set_index(ra, offset, ra_index);
	ra_set_size(ra, ra_size, la_size);

	return ra_submit(ra, mapping, filp);
}

/**
 * page_cache_readahead_ondemand - generic file readahead
 * @mapping: address_space which holds the pagecache and I/O vectors
 * @ra: file_ra_state which holds the readahead state
 * @filp: passed on to ->readpage() and ->readpages()
 * @page: the page at @offset, or NULL if non-present
 * @offset: start offset into @mapping, in PAGE_CACHE_SIZE units
 * @req_size: hint: total size of the read which the caller is performing in
 *            PAGE_CACHE_SIZE units
 *
 * page_cache_readahead_ondemand() is the entry point of readahead logic.
 * This function should be called when it is time to perform readahead:
 * 1) @page == NULL
 *    A cache miss happened, time for synchronous readahead.
 * 2) @page != NULL && PageReadahead(@page)
 *    A look-ahead hit occured, time for asynchronous readahead.
 */
unsigned long
page_cache_readahead_ondemand(struct address_space *mapping,
				struct file_ra_state *ra, struct file *filp,
				struct page *page, pgoff_t offset,
				unsigned long req_size)
{
	/* no read-ahead */
	if (!ra->ra_pages)
		return 0;

	if (page) {
		/*
		 * It can be PG_reclaim.
		 */
		if (PageWriteback(page))
			return 0;

		ClearPageReadahead(page);

		/*
		 * Defer asynchronous read-ahead on IO congestion.
		 */
		if (bdi_read_congested(mapping->backing_dev_info))
			return 0;
	}

	/* do read-ahead */
	return ondemand_readahead(mapping, ra, filp, page,
					offset, req_size);
}
EXPORT_SYMBOL_GPL(page_cache_readahead_ondemand);
