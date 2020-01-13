// SPDX-License-Identifier: GPL-2.0
/*
 * fs/mpage.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains functions related to preparing and submitting BIOs which contain
 * multiple pagecache pages.
 *
 * 15May2002	Andrew Morton
 *		Initial version
 * 27Jun2002	axboe@suse.de
 *		use bio_add_page() to build bio's just the right size
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/kdev_t.h>
#include <linux/gfp.h>
#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/prefetch.h>
#include <linux/mpage.h>
#include <linux/mm_inline.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/cleancache.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/android_fs.h>

EXPORT_TRACEPOINT_SYMBOL(android_fs_datawrite_start);
EXPORT_TRACEPOINT_SYMBOL(android_fs_datawrite_end);
EXPORT_TRACEPOINT_SYMBOL(android_fs_dataread_start);
EXPORT_TRACEPOINT_SYMBOL(android_fs_dataread_end);

/*
 * I/O completion handler for multipage BIOs.
 *
 * The mpage code never puts partial pages into a BIO (except for end-of-file).
 * If a page does not map to a contiguous run of blocks then it simply falls
 * back to block_read_full_page().
 *
 * Why is this?  If a page's completion depends on a number of different BIOs
 * which can complete in any order (or at the same time) then determining the
 * status of that page is hard.  See end_buffer_async_read() for the details.
 * There is no point in duplicating all that complexity.
 */
static void mpage_end_io(struct bio *bio)
{
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;

	if (trace_android_fs_dataread_end_enabled() &&
	    (bio_data_dir(bio) == READ)) {
		struct page *first_page = bio->bi_io_vec[0].bv_page;

		if (first_page != NULL)
			trace_android_fs_dataread_end(first_page->mapping->host,
						      page_offset(first_page),
						      bio->bi_iter.bi_size);
	}

	bio_for_each_segment_all(bv, bio, iter_all) {
		struct page *page = bv->bv_page;
		page_endio(page, bio_op(bio),
			   blk_status_to_errno(bio->bi_status));
	}

	bio_put(bio);
}

static struct bio *mpage_bio_submit(int op, int op_flags, struct bio *bio)
{
	if (trace_android_fs_dataread_start_enabled() && (op == REQ_OP_READ)) {
		struct page *first_page = bio->bi_io_vec[0].bv_page;

		if (first_page != NULL) {
			char *path, pathbuf[MAX_TRACE_PATHBUF_LEN];

			path = android_fstrace_get_pathname(pathbuf,
						    MAX_TRACE_PATHBUF_LEN,
						    first_page->mapping->host);
			trace_android_fs_dataread_start(
				first_page->mapping->host,
				page_offset(first_page),
				bio->bi_iter.bi_size,
				current->pid,
				path,
				current->comm);
		}
	}
	bio->bi_end_io = mpage_end_io;
	bio_set_op_attrs(bio, op, op_flags);
	guard_bio_eod(bio);
	submit_bio(bio);
	return NULL;
}

static struct bio *
mpage_alloc(struct block_device *bdev,
		sector_t first_sector, int nr_vecs,
		gfp_t gfp_flags)
{
	struct bio *bio;

	/* Restrict the given (page cache) mask for slab allocations */
	gfp_flags &= GFP_KERNEL;
	bio = bio_alloc(gfp_flags, nr_vecs);

	if (bio == NULL && (current->flags & PF_MEMALLOC)) {
		while (!bio && (nr_vecs /= 2))
			bio = bio_alloc(gfp_flags, nr_vecs);
	}

	if (bio) {
		bio_set_dev(bio, bdev);
		bio->bi_iter.bi_sector = first_sector;
	}
	return bio;
}

/*
 * support function for mpage_readpages.  The fs supplied get_block might
 * return an up to date buffer.  This is used to map that buffer into
 * the page, which allows readpage to avoid triggering a duplicate call
 * to get_block.
 *
 * The idea is to avoid adding buffers to pages that don't already have
 * them.  So when the buffer is up to date and the page size == block size,
 * this marks the page up to date instead of adding new buffers.
 */
static void 
map_buffer_to_page(struct page *page, struct buffer_head *bh, int page_block) 
{
	struct inode *inode = page->mapping->host;
	struct buffer_head *page_bh, *head;
	int block = 0;

	if (!page_has_buffers(page)) {
		/*
		 * don't make any buffers if there is only one buffer on
		 * the page and the page just needs to be set up to date
		 */
		if (inode->i_blkbits == PAGE_SHIFT &&
		    buffer_uptodate(bh)) {
			SetPageUptodate(page);    
			return;
		}
		create_empty_buffers(page, i_blocksize(inode), 0);
	}
	head = page_buffers(page);
	page_bh = head;
	do {
		if (block == page_block) {
			page_bh->b_state = bh->b_state;
			page_bh->b_bdev = bh->b_bdev;
			page_bh->b_blocknr = bh->b_blocknr;
			break;
		}
		page_bh = page_bh->b_this_page;
		block++;
	} while (page_bh != head);
}

struct mpage_readpage_args {
	struct bio *bio;
	struct page *page;
	unsigned int nr_pages;
	bool is_readahead;
	sector_t last_block_in_bio;
	struct buffer_head map_bh;
	unsigned long first_logical_block;
	get_block_t *get_block;
};

/*
 * This is the worker routine which does all the work of mapping the disk
 * blocks and constructs largest possible bios, submits them for IO if the
 * blocks are not contiguous on the disk.
 *
 * We pass a buffer_head back and forth and use its buffer_mapped() flag to
 * represent the validity of its disk mapping and to decide when to do the next
 * get_block() call.
 */
static struct bio *do_mpage_readpage(struct mpage_readpage_args *args)
{
	struct page *page = args->page;
	struct inode *inode = page->mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	const unsigned blocksize = 1 << blkbits;
	struct buffer_head *map_bh = &args->map_bh;
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t blocks[MAX_BUF_PER_PAGE];
	unsigned page_block;
	unsigned first_hole = blocks_per_page;
	struct block_device *bdev = NULL;
	int length;
	int fully_mapped = 1;
	int op_flags;
	unsigned nblocks;
	unsigned relative_block;
	gfp_t gfp;

	if (args->is_readahead) {
		op_flags = REQ_RAHEAD;
		gfp = readahead_gfp_mask(page->mapping);
	} else {
		op_flags = 0;
		gfp = mapping_gfp_constraint(page->mapping, GFP_KERNEL);
	}

	if (page_has_buffers(page))
		goto confused;

	block_in_file = (sector_t)page->index << (PAGE_SHIFT - blkbits);
	last_block = block_in_file + args->nr_pages * blocks_per_page;
	last_block_in_file = (i_size_read(inode) + blocksize - 1) >> blkbits;
	if (last_block > last_block_in_file)
		last_block = last_block_in_file;
	page_block = 0;

	/*
	 * Map blocks using the result from the previous get_blocks call first.
	 */
	nblocks = map_bh->b_size >> blkbits;
	if (buffer_mapped(map_bh) &&
			block_in_file > args->first_logical_block &&
			block_in_file < (args->first_logical_block + nblocks)) {
		unsigned map_offset = block_in_file - args->first_logical_block;
		unsigned last = nblocks - map_offset;

		for (relative_block = 0; ; relative_block++) {
			if (relative_block == last) {
				clear_buffer_mapped(map_bh);
				break;
			}
			if (page_block == blocks_per_page)
				break;
			blocks[page_block] = map_bh->b_blocknr + map_offset +
						relative_block;
			page_block++;
			block_in_file++;
		}
		bdev = map_bh->b_bdev;
	}

	/*
	 * Then do more get_blocks calls until we are done with this page.
	 */
	map_bh->b_page = page;
	while (page_block < blocks_per_page) {
		map_bh->b_state = 0;
		map_bh->b_size = 0;

		if (block_in_file < last_block) {
			map_bh->b_size = (last_block-block_in_file) << blkbits;
			if (args->get_block(inode, block_in_file, map_bh, 0))
				goto confused;
			args->first_logical_block = block_in_file;
		}

		if (!buffer_mapped(map_bh)) {
			fully_mapped = 0;
			if (first_hole == blocks_per_page)
				first_hole = page_block;
			page_block++;
			block_in_file++;
			continue;
		}

		/* some filesystems will copy data into the page during
		 * the get_block call, in which case we don't want to
		 * read it again.  map_buffer_to_page copies the data
		 * we just collected from get_block into the page's buffers
		 * so readpage doesn't have to repeat the get_block call
		 */
		if (buffer_uptodate(map_bh)) {
			map_buffer_to_page(page, map_bh, page_block);
			goto confused;
		}
	
		if (first_hole != blocks_per_page)
			goto confused;		/* hole -> non-hole */

		/* Contiguous blocks? */
		if (page_block && blocks[page_block-1] != map_bh->b_blocknr-1)
			goto confused;
		nblocks = map_bh->b_size >> blkbits;
		for (relative_block = 0; ; relative_block++) {
			if (relative_block == nblocks) {
				clear_buffer_mapped(map_bh);
				break;
			} else if (page_block == blocks_per_page)
				break;
			blocks[page_block] = map_bh->b_blocknr+relative_block;
			page_block++;
			block_in_file++;
		}
		bdev = map_bh->b_bdev;
	}

	if (first_hole != blocks_per_page) {
		zero_user_segment(page, first_hole << blkbits, PAGE_SIZE);
		if (first_hole == 0) {
			SetPageUptodate(page);
			unlock_page(page);
			goto out;
		}
	} else if (fully_mapped) {
		SetPageMappedToDisk(page);
	}

	if (fully_mapped && blocks_per_page == 1 && !PageUptodate(page) &&
	    cleancache_get_page(page) == 0) {
		SetPageUptodate(page);
		goto confused;
	}

	/*
	 * This page will go to BIO.  Do we need to send this BIO off first?
	 */
	if (args->bio && (args->last_block_in_bio != blocks[0] - 1))
		args->bio = mpage_bio_submit(REQ_OP_READ, op_flags, args->bio);

alloc_new:
	if (args->bio == NULL) {
		if (first_hole == blocks_per_page) {
			if (!bdev_read_page(bdev, blocks[0] << (blkbits - 9),
								page))
				goto out;
		}
		args->bio = mpage_alloc(bdev, blocks[0] << (blkbits - 9),
					min_t(int, args->nr_pages,
					      BIO_MAX_PAGES),
					gfp);
		if (args->bio == NULL)
			goto confused;
	}

	length = first_hole << blkbits;
	if (bio_add_page(args->bio, page, length, 0) < length) {
		args->bio = mpage_bio_submit(REQ_OP_READ, op_flags, args->bio);
		goto alloc_new;
	}

	relative_block = block_in_file - args->first_logical_block;
	nblocks = map_bh->b_size >> blkbits;
	if ((buffer_boundary(map_bh) && relative_block == nblocks) ||
	    (first_hole != blocks_per_page))
		args->bio = mpage_bio_submit(REQ_OP_READ, op_flags, args->bio);
	else
		args->last_block_in_bio = blocks[blocks_per_page - 1];
out:
	return args->bio;

confused:
	if (args->bio)
		args->bio = mpage_bio_submit(REQ_OP_READ, op_flags, args->bio);
	if (!PageUptodate(page))
		block_read_full_page(page, args->get_block);
	else
		unlock_page(page);
	goto out;
}

/**
 * mpage_readpages - populate an address space with some pages & start reads against them
 * @mapping: the address_space
 * @pages: The address of a list_head which contains the target pages.  These
 *   pages have their ->index populated and are otherwise uninitialised.
 *   The page at @pages->prev has the lowest file offset, and reads should be
 *   issued in @pages->prev to @pages->next order.
 * @nr_pages: The number of pages at *@pages
 * @get_block: The filesystem's block mapper function.
 *
 * This function walks the pages and the blocks within each page, building and
 * emitting large BIOs.
 *
 * If anything unusual happens, such as:
 *
 * - encountering a page which has buffers
 * - encountering a page which has a non-hole after a hole
 * - encountering a page with non-contiguous blocks
 *
 * then this code just gives up and calls the buffer_head-based read function.
 * It does handle a page which has holes at the end - that is a common case:
 * the end-of-file on blocksize < PAGE_SIZE setups.
 *
 * BH_Boundary explanation:
 *
 * There is a problem.  The mpage read code assembles several pages, gets all
 * their disk mappings, and then submits them all.  That's fine, but obtaining
 * the disk mappings may require I/O.  Reads of indirect blocks, for example.
 *
 * So an mpage read of the first 16 blocks of an ext2 file will cause I/O to be
 * submitted in the following order:
 *
 * 	12 0 1 2 3 4 5 6 7 8 9 10 11 13 14 15 16
 *
 * because the indirect block has to be read to get the mappings of blocks
 * 13,14,15,16.  Obviously, this impacts performance.
 *
 * So what we do it to allow the filesystem's get_block() function to set
 * BH_Boundary when it maps block 11.  BH_Boundary says: mapping of the block
 * after this one will require I/O against a block which is probably close to
 * this one.  So you should push what I/O you have currently accumulated.
 *
 * This all causes the disk requests to be issued in the correct order.
 */
int
mpage_readpages(struct address_space *mapping, struct list_head *pages,
				unsigned nr_pages, get_block_t get_block)
{
	struct mpage_readpage_args args = {
		.get_block = get_block,
		.is_readahead = true,
	};
	unsigned page_idx;

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = lru_to_page(pages);

		prefetchw(&page->flags);
		list_del(&page->lru);
		if (!add_to_page_cache_lru(page, mapping,
					page->index,
					readahead_gfp_mask(mapping))) {
			args.page = page;
			args.nr_pages = nr_pages - page_idx;
			args.bio = do_mpage_readpage(&args);
		}
		put_page(page);
	}
	BUG_ON(!list_empty(pages));
	if (args.bio)
		mpage_bio_submit(REQ_OP_READ, REQ_RAHEAD, args.bio);
	return 0;
}
EXPORT_SYMBOL(mpage_readpages);

/*
 * This isn't called much at all
 */
int mpage_readpage(struct page *page, get_block_t get_block)
{
	struct mpage_readpage_args args = {
		.page = page,
		.nr_pages = 1,
		.get_block = get_block,
	};

	args.bio = do_mpage_readpage(&args);
	if (args.bio)
		mpage_bio_submit(REQ_OP_READ, 0, args.bio);
	return 0;
}
EXPORT_SYMBOL(mpage_readpage);

/*
 * Writing is not so simple.
 *
 * If the page has buffers then they will be used for obtaining the disk
 * mapping.  We only support pages which are fully mapped-and-dirty, with a
 * special case for pages which are unmapped at the end: end-of-file.
 *
 * If the page has no buffers (preferred) then the page is mapped here.
 *
 * If all blocks are found to be contiguous then the page can go into the
 * BIO.  Otherwise fall back to the mapping's writepage().
 * 
 * FIXME: This code wants an estimate of how many pages are still to be
 * written, so it can intelligently allocate a suitably-sized BIO.  For now,
 * just allocate full-size (16-page) BIOs.
 */

struct mpage_data {
	struct bio *bio;
	sector_t last_block_in_bio;
	get_block_t *get_block;
	unsigned use_writepage;
};

/*
 * We have our BIO, so we can now mark the buffers clean.  Make
 * sure to only clean buffers which we know we'll be writing.
 */
static void clean_buffers(struct page *page, unsigned first_unmapped)
{
	unsigned buffer_counter = 0;
	struct buffer_head *bh, *head;
	if (!page_has_buffers(page))
		return;
	head = page_buffers(page);
	bh = head;

	do {
		if (buffer_counter++ == first_unmapped)
			break;
		clear_buffer_dirty(bh);
		bh = bh->b_this_page;
	} while (bh != head);

	/*
	 * we cannot drop the bh if the page is not uptodate or a concurrent
	 * readpage would fail to serialize with the bh and it would read from
	 * disk before we reach the platter.
	 */
	if (buffer_heads_over_limit && PageUptodate(page))
		try_to_free_buffers(page);
}

/*
 * For situations where we want to clean all buffers attached to a page.
 * We don't need to calculate how many buffers are attached to the page,
 * we just need to specify a number larger than the maximum number of buffers.
 */
void clean_page_buffers(struct page *page)
{
	clean_buffers(page, ~0U);
}

static int __mpage_writepage(struct page *page, struct writeback_control *wbc,
		      void *data)
{
	struct mpage_data *mpd = data;
	struct bio *bio = mpd->bio;
	struct address_space *mapping = page->mapping;
	struct inode *inode = page->mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	unsigned long end_index;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	sector_t last_block;
	sector_t block_in_file;
	sector_t blocks[MAX_BUF_PER_PAGE];
	unsigned page_block;
	unsigned first_unmapped = blocks_per_page;
	struct block_device *bdev = NULL;
	int boundary = 0;
	sector_t boundary_block = 0;
	struct block_device *boundary_bdev = NULL;
	int length;
	struct buffer_head map_bh;
	loff_t i_size = i_size_read(inode);
	int ret = 0;
	int op_flags = wbc_to_write_flags(wbc);

	if (page_has_buffers(page)) {
		struct buffer_head *head = page_buffers(page);
		struct buffer_head *bh = head;

		/* If they're all mapped and dirty, do it */
		page_block = 0;
		do {
			BUG_ON(buffer_locked(bh));
			if (!buffer_mapped(bh)) {
				/*
				 * unmapped dirty buffers are created by
				 * __set_page_dirty_buffers -> mmapped data
				 */
				if (buffer_dirty(bh))
					goto confused;
				if (first_unmapped == blocks_per_page)
					first_unmapped = page_block;
				continue;
			}

			if (first_unmapped != blocks_per_page)
				goto confused;	/* hole -> non-hole */

			if (!buffer_dirty(bh) || !buffer_uptodate(bh))
				goto confused;
			if (page_block) {
				if (bh->b_blocknr != blocks[page_block-1] + 1)
					goto confused;
			}
			blocks[page_block++] = bh->b_blocknr;
			boundary = buffer_boundary(bh);
			if (boundary) {
				boundary_block = bh->b_blocknr;
				boundary_bdev = bh->b_bdev;
			}
			bdev = bh->b_bdev;
		} while ((bh = bh->b_this_page) != head);

		if (first_unmapped)
			goto page_is_mapped;

		/*
		 * Page has buffers, but they are all unmapped. The page was
		 * created by pagein or read over a hole which was handled by
		 * block_read_full_page().  If this address_space is also
		 * using mpage_readpages then this can rarely happen.
		 */
		goto confused;
	}

	/*
	 * The page has no buffers: map it to disk
	 */
	BUG_ON(!PageUptodate(page));
	block_in_file = (sector_t)page->index << (PAGE_SHIFT - blkbits);
	last_block = (i_size - 1) >> blkbits;
	map_bh.b_page = page;
	for (page_block = 0; page_block < blocks_per_page; ) {

		map_bh.b_state = 0;
		map_bh.b_size = 1 << blkbits;
		if (mpd->get_block(inode, block_in_file, &map_bh, 1))
			goto confused;
		if (buffer_new(&map_bh))
			clean_bdev_bh_alias(&map_bh);
		if (buffer_boundary(&map_bh)) {
			boundary_block = map_bh.b_blocknr;
			boundary_bdev = map_bh.b_bdev;
		}
		if (page_block) {
			if (map_bh.b_blocknr != blocks[page_block-1] + 1)
				goto confused;
		}
		blocks[page_block++] = map_bh.b_blocknr;
		boundary = buffer_boundary(&map_bh);
		bdev = map_bh.b_bdev;
		if (block_in_file == last_block)
			break;
		block_in_file++;
	}
	BUG_ON(page_block == 0);

	first_unmapped = page_block;

page_is_mapped:
	end_index = i_size >> PAGE_SHIFT;
	if (page->index >= end_index) {
		/*
		 * The page straddles i_size.  It must be zeroed out on each
		 * and every writepage invocation because it may be mmapped.
		 * "A file is mapped in multiples of the page size.  For a file
		 * that is not a multiple of the page size, the remaining memory
		 * is zeroed when mapped, and writes to that region are not
		 * written out to the file."
		 */
		unsigned offset = i_size & (PAGE_SIZE - 1);

		if (page->index > end_index || !offset)
			goto confused;
		zero_user_segment(page, offset, PAGE_SIZE);
	}

	/*
	 * This page will go to BIO.  Do we need to send this BIO off first?
	 */
	if (bio && mpd->last_block_in_bio != blocks[0] - 1)
		bio = mpage_bio_submit(REQ_OP_WRITE, op_flags, bio);

alloc_new:
	if (bio == NULL) {
		if (first_unmapped == blocks_per_page) {
			if (!bdev_write_page(bdev, blocks[0] << (blkbits - 9),
								page, wbc))
				goto out;
		}
		bio = mpage_alloc(bdev, blocks[0] << (blkbits - 9),
				BIO_MAX_PAGES, GFP_NOFS|__GFP_HIGH);
		if (bio == NULL)
			goto confused;

		wbc_init_bio(wbc, bio);
		bio->bi_write_hint = inode->i_write_hint;
	}

	/*
	 * Must try to add the page before marking the buffer clean or
	 * the confused fail path above (OOM) will be very confused when
	 * it finds all bh marked clean (i.e. it will not write anything)
	 */
	wbc_account_cgroup_owner(wbc, page, PAGE_SIZE);
	length = first_unmapped << blkbits;
	if (bio_add_page(bio, page, length, 0) < length) {
		bio = mpage_bio_submit(REQ_OP_WRITE, op_flags, bio);
		goto alloc_new;
	}

	clean_buffers(page, first_unmapped);

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	unlock_page(page);
	if (boundary || (first_unmapped != blocks_per_page)) {
		bio = mpage_bio_submit(REQ_OP_WRITE, op_flags, bio);
		if (boundary_block) {
			write_boundary_block(boundary_bdev,
					boundary_block, 1 << blkbits);
		}
	} else {
		mpd->last_block_in_bio = blocks[blocks_per_page - 1];
	}
	goto out;

confused:
	if (bio)
		bio = mpage_bio_submit(REQ_OP_WRITE, op_flags, bio);

	if (mpd->use_writepage) {
		ret = mapping->a_ops->writepage(page, wbc);
	} else {
		ret = -EAGAIN;
		goto out;
	}
	/*
	 * The caller has a ref on the inode, so *mapping is stable
	 */
	mapping_set_error(mapping, ret);
out:
	mpd->bio = bio;
	return ret;
}

/**
 * mpage_writepages - walk the list of dirty pages of the given address space & writepage() all of them
 * @mapping: address space structure to write
 * @wbc: subtract the number of written pages from *@wbc->nr_to_write
 * @get_block: the filesystem's block mapper function.
 *             If this is NULL then use a_ops->writepage.  Otherwise, go
 *             direct-to-BIO.
 *
 * This is a library function, which implements the writepages()
 * address_space_operation.
 *
 * If a page is already under I/O, generic_writepages() skips it, even
 * if it's dirty.  This is desirable behaviour for memory-cleaning writeback,
 * but it is INCORRECT for data-integrity system calls such as fsync().  fsync()
 * and msync() need to guarantee that all the data which was dirty at the time
 * the call was made get new I/O started against them.  If wbc->sync_mode is
 * WB_SYNC_ALL then we were called for data integrity and we must wait for
 * existing IO to complete.
 */
int
mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc, get_block_t get_block)
{
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);

	if (!get_block)
		ret = generic_writepages(mapping, wbc);
	else {
		struct mpage_data mpd = {
			.bio = NULL,
			.last_block_in_bio = 0,
			.get_block = get_block,
			.use_writepage = 1,
		};

		ret = write_cache_pages(mapping, wbc, __mpage_writepage, &mpd);
		if (mpd.bio) {
			int op_flags = (wbc->sync_mode == WB_SYNC_ALL ?
				  REQ_SYNC : 0);
			mpage_bio_submit(REQ_OP_WRITE, op_flags, mpd.bio);
		}
	}
	blk_finish_plug(&plug);
	return ret;
}
EXPORT_SYMBOL(mpage_writepages);

int mpage_writepage(struct page *page, get_block_t get_block,
	struct writeback_control *wbc)
{
	struct mpage_data mpd = {
		.bio = NULL,
		.last_block_in_bio = 0,
		.get_block = get_block,
		.use_writepage = 0,
	};
	int ret = __mpage_writepage(page, wbc, &mpd);
	if (mpd.bio) {
		int op_flags = (wbc->sync_mode == WB_SYNC_ALL ?
			  REQ_SYNC : 0);
		mpage_bio_submit(REQ_OP_WRITE, op_flags, mpd.bio);
	}
	return ret;
}
EXPORT_SYMBOL(mpage_writepage);
