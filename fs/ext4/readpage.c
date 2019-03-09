// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/readpage.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 * Copyright (C) 2015, Google, Inc.
 *
 * This was originally taken from fs/mpage.c
 *
 * The intent is the ext4_mpage_readpages() function here is intended
 * to replace mpage_readpages() in the general case, not just for
 * encrypted files.  It has some limitations (see below), where it
 * will fall back to read_block_full_page(), but these limitations
 * should only be hit when page_size != block_size.
 *
 * This will allow us to attach a callback function to support ext4
 * encryption.
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
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/cleancache.h>

#include "ext4.h"

static inline bool ext4_bio_encrypted(struct bio *bio)
{
#ifdef CONFIG_FS_ENCRYPTION
	return unlikely(bio->bi_private != NULL);
#else
	return false;
#endif
}

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
	int i;
	struct bvec_iter_all iter_all;

	if (ext4_bio_encrypted(bio)) {
		if (bio->bi_status) {
			fscrypt_release_ctx(bio->bi_private);
		} else {
			fscrypt_enqueue_decrypt_bio(bio->bi_private, bio);
			return;
		}
	}
	bio_for_each_segment_all(bv, bio, i, iter_all) {
		struct page *page = bv->bv_page;

		if (!bio->bi_status) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}

	bio_put(bio);
}

int ext4_mpage_readpages(struct address_space *mapping,
			 struct list_head *pages, struct page *page,
			 unsigned nr_pages, bool is_readahead)
{
	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;

	struct inode *inode = mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocks_per_page = PAGE_SIZE >> blkbits;
	const unsigned blocksize = 1 << blkbits;
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t blocks[MAX_BUF_PER_PAGE];
	unsigned page_block;
	struct block_device *bdev = inode->i_sb->s_bdev;
	int length;
	unsigned relative_block = 0;
	struct ext4_map_blocks map;

	map.m_pblk = 0;
	map.m_lblk = 0;
	map.m_len = 0;
	map.m_flags = 0;

	for (; nr_pages; nr_pages--) {
		int fully_mapped = 1;
		unsigned first_hole = blocks_per_page;

		prefetchw(&page->flags);
		if (pages) {
			page = lru_to_page(pages);
			list_del(&page->lru);
			if (add_to_page_cache_lru(page, mapping, page->index,
				  readahead_gfp_mask(mapping)))
				goto next_page;
		}

		if (page_has_buffers(page))
			goto confused;

		block_in_file = (sector_t)page->index << (PAGE_SHIFT - blkbits);
		last_block = block_in_file + nr_pages * blocks_per_page;
		last_block_in_file = (i_size_read(inode) + blocksize - 1) >> blkbits;
		if (last_block > last_block_in_file)
			last_block = last_block_in_file;
		page_block = 0;

		/*
		 * Map blocks using the previous result first.
		 */
		if ((map.m_flags & EXT4_MAP_MAPPED) &&
		    block_in_file > map.m_lblk &&
		    block_in_file < (map.m_lblk + map.m_len)) {
			unsigned map_offset = block_in_file - map.m_lblk;
			unsigned last = map.m_len - map_offset;

			for (relative_block = 0; ; relative_block++) {
				if (relative_block == last) {
					/* needed? */
					map.m_flags &= ~EXT4_MAP_MAPPED;
					break;
				}
				if (page_block == blocks_per_page)
					break;
				blocks[page_block] = map.m_pblk + map_offset +
					relative_block;
				page_block++;
				block_in_file++;
			}
		}

		/*
		 * Then do more ext4_map_blocks() calls until we are
		 * done with this page.
		 */
		while (page_block < blocks_per_page) {
			if (block_in_file < last_block) {
				map.m_lblk = block_in_file;
				map.m_len = last_block - block_in_file;

				if (ext4_map_blocks(NULL, inode, &map, 0) < 0) {
				set_error_page:
					SetPageError(page);
					zero_user_segment(page, 0,
							  PAGE_SIZE);
					unlock_page(page);
					goto next_page;
				}
			}
			if ((map.m_flags & EXT4_MAP_MAPPED) == 0) {
				fully_mapped = 0;
				if (first_hole == blocks_per_page)
					first_hole = page_block;
				page_block++;
				block_in_file++;
				continue;
			}
			if (first_hole != blocks_per_page)
				goto confused;		/* hole -> non-hole */

			/* Contiguous blocks? */
			if (page_block && blocks[page_block-1] != map.m_pblk-1)
				goto confused;
			for (relative_block = 0; ; relative_block++) {
				if (relative_block == map.m_len) {
					/* needed? */
					map.m_flags &= ~EXT4_MAP_MAPPED;
					break;
				} else if (page_block == blocks_per_page)
					break;
				blocks[page_block] = map.m_pblk+relative_block;
				page_block++;
				block_in_file++;
			}
		}
		if (first_hole != blocks_per_page) {
			zero_user_segment(page, first_hole << blkbits,
					  PAGE_SIZE);
			if (first_hole == 0) {
				SetPageUptodate(page);
				unlock_page(page);
				goto next_page;
			}
		} else if (fully_mapped) {
			SetPageMappedToDisk(page);
		}
		if (fully_mapped && blocks_per_page == 1 &&
		    !PageUptodate(page) && cleancache_get_page(page) == 0) {
			SetPageUptodate(page);
			goto confused;
		}

		/*
		 * This page will go to BIO.  Do we need to send this
		 * BIO off first?
		 */
		if (bio && (last_block_in_bio != blocks[0] - 1)) {
		submit_and_realloc:
			submit_bio(bio);
			bio = NULL;
		}
		if (bio == NULL) {
			struct fscrypt_ctx *ctx = NULL;

			if (IS_ENCRYPTED(inode) && S_ISREG(inode->i_mode)) {
				ctx = fscrypt_get_ctx(inode, GFP_NOFS);
				if (IS_ERR(ctx))
					goto set_error_page;
			}
			bio = bio_alloc(GFP_KERNEL,
				min_t(int, nr_pages, BIO_MAX_PAGES));
			if (!bio) {
				if (ctx)
					fscrypt_release_ctx(ctx);
				goto set_error_page;
			}
			bio_set_dev(bio, bdev);
			bio->bi_iter.bi_sector = blocks[0] << (blkbits - 9);
			bio->bi_end_io = mpage_end_io;
			bio->bi_private = ctx;
			bio_set_op_attrs(bio, REQ_OP_READ,
						is_readahead ? REQ_RAHEAD : 0);
		}

		length = first_hole << blkbits;
		if (bio_add_page(bio, page, length, 0) < length)
			goto submit_and_realloc;

		if (((map.m_flags & EXT4_MAP_BOUNDARY) &&
		     (relative_block == map.m_len)) ||
		    (first_hole != blocks_per_page)) {
			submit_bio(bio);
			bio = NULL;
		} else
			last_block_in_bio = blocks[blocks_per_page - 1];
		goto next_page;
	confused:
		if (bio) {
			submit_bio(bio);
			bio = NULL;
		}
		if (!PageUptodate(page))
			block_read_full_page(page, ext4_get_block);
		else
			unlock_page(page);
	next_page:
		if (pages)
			put_page(page);
	}
	BUG_ON(pages && !list_empty(pages));
	if (bio)
		submit_bio(bio);
	return 0;
}
