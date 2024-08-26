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
#include "internal.h"

/*
 * I/O completion handler for multipage BIOs.
 *
 * The mpage code never puts partial pages into a BIO (except for end-of-file).
 * If a page does not map to a contiguous run of blocks then it simply falls
 * back to block_read_full_folio().
 *
 * Why is this?  If a page's completion depends on a number of different BIOs
 * which can complete in any order (or at the same time) then determining the
 * status of that page is hard.  See end_buffer_async_read() for the details.
 * There is no point in duplicating all that complexity.
 */
static void mpage_read_end_io(struct bio *bio)
{
	struct folio_iter fi;
	int err = blk_status_to_errno(bio->bi_status);

	bio_for_each_folio_all(fi, bio)
		folio_end_read(fi.folio, err == 0);

	bio_put(bio);
}

static void mpage_write_end_io(struct bio *bio)
{
	struct folio_iter fi;
	int err = blk_status_to_errno(bio->bi_status);

	bio_for_each_folio_all(fi, bio) {
		if (err)
			mapping_set_error(fi.folio->mapping, err);
		folio_end_writeback(fi.folio);
	}

	bio_put(bio);
}

static struct bio *mpage_bio_submit_read(struct bio *bio)
{
	bio->bi_end_io = mpage_read_end_io;
	guard_bio_eod(bio);
	submit_bio(bio);
	return NULL;
}

static struct bio *mpage_bio_submit_write(struct bio *bio)
{
	bio->bi_end_io = mpage_write_end_io;
	guard_bio_eod(bio);
	submit_bio(bio);
	return NULL;
}

/*
 * support function for mpage_readahead.  The fs supplied get_block might
 * return an up to date buffer.  This is used to map that buffer into
 * the page, which allows read_folio to avoid triggering a duplicate call
 * to get_block.
 *
 * The idea is to avoid adding buffers to pages that don't already have
 * them.  So when the buffer is up to date and the page size == block size,
 * this marks the page up to date instead of adding new buffers.
 */
static void map_buffer_to_folio(struct folio *folio, struct buffer_head *bh,
		int page_block)
{
	struct inode *inode = folio->mapping->host;
	struct buffer_head *page_bh, *head;
	int block = 0;

	head = folio_buffers(folio);
	if (!head) {
		/*
		 * don't make any buffers if there is only one buffer on
		 * the folio and the folio just needs to be set up to date
		 */
		if (inode->i_blkbits == PAGE_SHIFT &&
		    buffer_uptodate(bh)) {
			folio_mark_uptodate(folio);
			return;
		}
		head = create_empty_buffers(folio, i_blocksize(inode), 0);
	}

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
	struct folio *folio;
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
	struct folio *folio = args->folio;
	struct inode *inode = folio->mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	const unsigned blocksize = 1 << blkbits;
	struct buffer_head *map_bh = &args->map_bh;
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t first_block;
	unsigned page_block;
	unsigned first_hole = blocks_per_page;
	struct block_device *bdev = NULL;
	int length;
	int fully_mapped = 1;
	blk_opf_t opf = REQ_OP_READ;
	unsigned nblocks;
	unsigned relative_block;
	gfp_t gfp = mapping_gfp_constraint(folio->mapping, GFP_KERNEL);

	/* MAX_BUF_PER_PAGE, for example */
	VM_BUG_ON_FOLIO(folio_test_large(folio), folio);

	if (args->is_readahead) {
		opf |= REQ_RAHEAD;
		gfp |= __GFP_NORETRY | __GFP_NOWARN;
	}

	if (folio_buffers(folio))
		goto confused;

	block_in_file = (sector_t)folio->index << (PAGE_SHIFT - blkbits);
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

		first_block = map_bh->b_blocknr + map_offset;
		for (relative_block = 0; ; relative_block++) {
			if (relative_block == last) {
				clear_buffer_mapped(map_bh);
				break;
			}
			if (page_block == blocks_per_page)
				break;
			page_block++;
			block_in_file++;
		}
		bdev = map_bh->b_bdev;
	}

	/*
	 * Then do more get_blocks calls until we are done with this folio.
	 */
	map_bh->b_folio = folio;
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
		 * read it again.  map_buffer_to_folio copies the data
		 * we just collected from get_block into the folio's buffers
		 * so read_folio doesn't have to repeat the get_block call
		 */
		if (buffer_uptodate(map_bh)) {
			map_buffer_to_folio(folio, map_bh, page_block);
			goto confused;
		}
	
		if (first_hole != blocks_per_page)
			goto confused;		/* hole -> non-hole */

		/* Contiguous blocks? */
		if (!page_block)
			first_block = map_bh->b_blocknr;
		else if (first_block + page_block != map_bh->b_blocknr)
			goto confused;
		nblocks = map_bh->b_size >> blkbits;
		for (relative_block = 0; ; relative_block++) {
			if (relative_block == nblocks) {
				clear_buffer_mapped(map_bh);
				break;
			} else if (page_block == blocks_per_page)
				break;
			page_block++;
			block_in_file++;
		}
		bdev = map_bh->b_bdev;
	}

	if (first_hole != blocks_per_page) {
		folio_zero_segment(folio, first_hole << blkbits, PAGE_SIZE);
		if (first_hole == 0) {
			folio_mark_uptodate(folio);
			folio_unlock(folio);
			goto out;
		}
	} else if (fully_mapped) {
		folio_set_mappedtodisk(folio);
	}

	/*
	 * This folio will go to BIO.  Do we need to send this BIO off first?
	 */
	if (args->bio && (args->last_block_in_bio != first_block - 1))
		args->bio = mpage_bio_submit_read(args->bio);

alloc_new:
	if (args->bio == NULL) {
		args->bio = bio_alloc(bdev, bio_max_segs(args->nr_pages), opf,
				      gfp);
		if (args->bio == NULL)
			goto confused;
		args->bio->bi_iter.bi_sector = first_block << (blkbits - 9);
	}

	length = first_hole << blkbits;
	if (!bio_add_folio(args->bio, folio, length, 0)) {
		args->bio = mpage_bio_submit_read(args->bio);
		goto alloc_new;
	}

	relative_block = block_in_file - args->first_logical_block;
	nblocks = map_bh->b_size >> blkbits;
	if ((buffer_boundary(map_bh) && relative_block == nblocks) ||
	    (first_hole != blocks_per_page))
		args->bio = mpage_bio_submit_read(args->bio);
	else
		args->last_block_in_bio = first_block + blocks_per_page - 1;
out:
	return args->bio;

confused:
	if (args->bio)
		args->bio = mpage_bio_submit_read(args->bio);
	if (!folio_test_uptodate(folio))
		block_read_full_folio(folio, args->get_block);
	else
		folio_unlock(folio);
	goto out;
}

/**
 * mpage_readahead - start reads against pages
 * @rac: Describes which pages to read.
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
void mpage_readahead(struct readahead_control *rac, get_block_t get_block)
{
	struct folio *folio;
	struct mpage_readpage_args args = {
		.get_block = get_block,
		.is_readahead = true,
	};

	while ((folio = readahead_folio(rac))) {
		prefetchw(&folio->flags);
		args.folio = folio;
		args.nr_pages = readahead_count(rac);
		args.bio = do_mpage_readpage(&args);
	}
	if (args.bio)
		mpage_bio_submit_read(args.bio);
}
EXPORT_SYMBOL(mpage_readahead);

/*
 * This isn't called much at all
 */
int mpage_read_folio(struct folio *folio, get_block_t get_block)
{
	struct mpage_readpage_args args = {
		.folio = folio,
		.nr_pages = 1,
		.get_block = get_block,
	};

	args.bio = do_mpage_readpage(&args);
	if (args.bio)
		mpage_bio_submit_read(args.bio);
	return 0;
}
EXPORT_SYMBOL(mpage_read_folio);

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
};

/*
 * We have our BIO, so we can now mark the buffers clean.  Make
 * sure to only clean buffers which we know we'll be writing.
 */
static void clean_buffers(struct folio *folio, unsigned first_unmapped)
{
	unsigned buffer_counter = 0;
	struct buffer_head *bh, *head = folio_buffers(folio);

	if (!head)
		return;
	bh = head;

	do {
		if (buffer_counter++ == first_unmapped)
			break;
		clear_buffer_dirty(bh);
		bh = bh->b_this_page;
	} while (bh != head);

	/*
	 * we cannot drop the bh if the page is not uptodate or a concurrent
	 * read_folio would fail to serialize with the bh and it would read from
	 * disk before we reach the platter.
	 */
	if (buffer_heads_over_limit && folio_test_uptodate(folio))
		try_to_free_buffers(folio);
}

static int __mpage_writepage(struct folio *folio, struct writeback_control *wbc,
		      void *data)
{
	struct mpage_data *mpd = data;
	struct bio *bio = mpd->bio;
	struct address_space *mapping = folio->mapping;
	struct inode *inode = mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	sector_t last_block;
	sector_t block_in_file;
	sector_t first_block;
	unsigned page_block;
	unsigned first_unmapped = blocks_per_page;
	struct block_device *bdev = NULL;
	int boundary = 0;
	sector_t boundary_block = 0;
	struct block_device *boundary_bdev = NULL;
	size_t length;
	struct buffer_head map_bh;
	loff_t i_size = i_size_read(inode);
	int ret = 0;
	struct buffer_head *head = folio_buffers(folio);

	if (head) {
		struct buffer_head *bh = head;

		/* If they're all mapped and dirty, do it */
		page_block = 0;
		do {
			BUG_ON(buffer_locked(bh));
			if (!buffer_mapped(bh)) {
				/*
				 * unmapped dirty buffers are created by
				 * block_dirty_folio -> mmapped data
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
				if (bh->b_blocknr != first_block + page_block)
					goto confused;
			} else {
				first_block = bh->b_blocknr;
			}
			page_block++;
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
		 * block_read_full_folio().  If this address_space is also
		 * using mpage_readahead then this can rarely happen.
		 */
		goto confused;
	}

	/*
	 * The page has no buffers: map it to disk
	 */
	BUG_ON(!folio_test_uptodate(folio));
	block_in_file = (sector_t)folio->index << (PAGE_SHIFT - blkbits);
	/*
	 * Whole page beyond EOF? Skip allocating blocks to avoid leaking
	 * space.
	 */
	if (block_in_file >= (i_size + (1 << blkbits) - 1) >> blkbits)
		goto page_is_mapped;
	last_block = (i_size - 1) >> blkbits;
	map_bh.b_folio = folio;
	for (page_block = 0; page_block < blocks_per_page; ) {

		map_bh.b_state = 0;
		map_bh.b_size = 1 << blkbits;
		if (mpd->get_block(inode, block_in_file, &map_bh, 1))
			goto confused;
		if (!buffer_mapped(&map_bh))
			goto confused;
		if (buffer_new(&map_bh))
			clean_bdev_bh_alias(&map_bh);
		if (buffer_boundary(&map_bh)) {
			boundary_block = map_bh.b_blocknr;
			boundary_bdev = map_bh.b_bdev;
		}
		if (page_block) {
			if (map_bh.b_blocknr != first_block + page_block)
				goto confused;
		} else {
			first_block = map_bh.b_blocknr;
		}
		page_block++;
		boundary = buffer_boundary(&map_bh);
		bdev = map_bh.b_bdev;
		if (block_in_file == last_block)
			break;
		block_in_file++;
	}
	BUG_ON(page_block == 0);

	first_unmapped = page_block;

page_is_mapped:
	/* Don't bother writing beyond EOF, truncate will discard the folio */
	if (folio_pos(folio) >= i_size)
		goto confused;
	length = folio_size(folio);
	if (folio_pos(folio) + length > i_size) {
		/*
		 * The page straddles i_size.  It must be zeroed out on each
		 * and every writepage invocation because it may be mmapped.
		 * "A file is mapped in multiples of the page size.  For a file
		 * that is not a multiple of the page size, the remaining memory
		 * is zeroed when mapped, and writes to that region are not
		 * written out to the file."
		 */
		length = i_size - folio_pos(folio);
		folio_zero_segment(folio, length, folio_size(folio));
	}

	/*
	 * This page will go to BIO.  Do we need to send this BIO off first?
	 */
	if (bio && mpd->last_block_in_bio != first_block - 1)
		bio = mpage_bio_submit_write(bio);

alloc_new:
	if (bio == NULL) {
		bio = bio_alloc(bdev, BIO_MAX_VECS,
				REQ_OP_WRITE | wbc_to_write_flags(wbc),
				GFP_NOFS);
		bio->bi_iter.bi_sector = first_block << (blkbits - 9);
		wbc_init_bio(wbc, bio);
		bio->bi_write_hint = inode->i_write_hint;
	}

	/*
	 * Must try to add the page before marking the buffer clean or
	 * the confused fail path above (OOM) will be very confused when
	 * it finds all bh marked clean (i.e. it will not write anything)
	 */
	wbc_account_cgroup_owner(wbc, &folio->page, folio_size(folio));
	length = first_unmapped << blkbits;
	if (!bio_add_folio(bio, folio, length, 0)) {
		bio = mpage_bio_submit_write(bio);
		goto alloc_new;
	}

	clean_buffers(folio, first_unmapped);

	BUG_ON(folio_test_writeback(folio));
	folio_start_writeback(folio);
	folio_unlock(folio);
	if (boundary || (first_unmapped != blocks_per_page)) {
		bio = mpage_bio_submit_write(bio);
		if (boundary_block) {
			write_boundary_block(boundary_bdev,
					boundary_block, 1 << blkbits);
		}
	} else {
		mpd->last_block_in_bio = first_block + blocks_per_page - 1;
	}
	goto out;

confused:
	if (bio)
		bio = mpage_bio_submit_write(bio);

	/*
	 * The caller has a ref on the inode, so *mapping is stable
	 */
	ret = block_write_full_folio(folio, wbc, mpd->get_block);
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
 *
 * This is a library function, which implements the writepages()
 * address_space_operation.
 */
int
mpage_writepages(struct address_space *mapping,
		struct writeback_control *wbc, get_block_t get_block)
{
	struct mpage_data mpd = {
		.get_block	= get_block,
	};
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = write_cache_pages(mapping, wbc, __mpage_writepage, &mpd);
	if (mpd.bio)
		mpage_bio_submit_write(mpd.bio);
	blk_finish_plug(&plug);
	return ret;
}
EXPORT_SYMBOL(mpage_writepages);
