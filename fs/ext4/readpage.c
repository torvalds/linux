// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/readpage.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 * Copyright (C) 2015, Google, Inc.
 *
 * This was originally taken from fs/mpage.c
 *
 * The ext4_mpage_readpages() function here is intended to
 * replace mpage_readahead() in the general case, not just for
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
#include <linux/blk-crypto.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/prefetch.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>

#include "ext4.h"
#include <trace/events/ext4.h>

#define NUM_VERITY_WORKS 128

static struct kmem_cache *ext4_verity_work_cache;
static mempool_t *ext4_verity_work_pool;

struct ext4_verity_work {
	struct bio *bio;
	struct fsverity_info *vi;
	struct work_struct work;
};

static void __read_end_io(struct bio *bio)
{
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio)
		folio_end_read(fi.folio, bio->bi_status == 0);
	if (bio->bi_private)
		mempool_free(bio->bi_private, ext4_verity_work_pool);
	bio_put(bio);
}

static void verity_work(struct work_struct *work)
{
	struct ext4_verity_work *ctx =
		container_of(work, struct ext4_verity_work, work);
	struct bio *bio = ctx->bio;
	struct fsverity_info *vi = ctx->vi;

	/*
	 * Free the ext4_verity_work right away, since it's no longer needed.
	 * This relieves the pressure on the mempool as much as possible.
	 */
	mempool_free(ctx, ext4_verity_work_pool);
	bio->bi_private = NULL;

	fsverity_verify_bio(vi, bio);

	__read_end_io(bio);
}

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
static void mpage_end_io(struct bio *bio)
{
	if (IS_ENABLED(CONFIG_FS_VERITY) && bio->bi_private &&
	    !bio->bi_status) {
		struct ext4_verity_work *ctx = bio->bi_private;

		INIT_WORK(&ctx->work, verity_work);
		fsverity_enqueue_verify_work(&ctx->work);
		return;
	}
	__read_end_io(bio);
}

static void ext4_set_verity_work(struct bio *bio, struct fsverity_info *vi)
{
	if (vi) {
		/* Due to the mempool, this never fails. */
		struct ext4_verity_work *ctx =
			mempool_alloc(ext4_verity_work_pool, GFP_NOFS);

		ctx->bio = bio;
		ctx->vi = vi;
		bio->bi_private = ctx;
	}
}

static inline loff_t ext4_readpage_limit(struct inode *inode)
{
	if (IS_ENABLED(CONFIG_FS_VERITY) && IS_VERITY(inode))
		return inode->i_sb->s_maxbytes;

	return i_size_read(inode);
}

static int ext4_mpage_readpages(struct inode *inode, struct fsverity_info *vi,
		struct readahead_control *rac, struct folio *folio)
{
	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocksize = 1 << blkbits;
	sector_t block_in_file;
	sector_t last_block;
	sector_t last_block_in_file;
	sector_t first_block;
	loff_t pos;
	unsigned page_block;
	struct block_device *bdev = inode->i_sb->s_bdev;
	int length;
	unsigned relative_block = 0;
	struct ext4_map_blocks map;
	unsigned int nr_pages, folio_pages;

	map.m_pblk = 0;
	map.m_lblk = 0;
	map.m_len = 0;
	map.m_flags = 0;

	nr_pages = rac ? readahead_count(rac) : folio_nr_pages(folio);
	for (; nr_pages; nr_pages -= folio_pages) {
		int fully_mapped = 1;
		unsigned int first_hole;
		unsigned int blocks_per_folio;

		if (rac)
			folio = readahead_folio(rac);

		folio_pages = folio_nr_pages(folio);
		prefetchw(&folio->flags);

		if (folio_buffers(folio))
			goto confused;

		blocks_per_folio = folio_size(folio) >> blkbits;
		first_hole = blocks_per_folio;
		pos = folio_pos(folio);
		block_in_file = pos >> blkbits;
		last_block = EXT4_PG_TO_LBLK(inode, folio->index + nr_pages);
		last_block_in_file = (ext4_readpage_limit(inode) +
				      blocksize - 1) >> blkbits;
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

			first_block = map.m_pblk + map_offset;
			for (relative_block = 0; ; relative_block++) {
				if (relative_block == last) {
					/* needed? */
					map.m_flags &= ~EXT4_MAP_MAPPED;
					break;
				}
				if (page_block == blocks_per_folio)
					break;
				page_block++;
				block_in_file++;
			}
		}

		/*
		 * Then do more ext4_map_blocks() calls until we are
		 * done with this folio.
		 */
		while (page_block < blocks_per_folio) {
			if (block_in_file < last_block) {
				map.m_lblk = block_in_file;
				map.m_len = last_block - block_in_file;

				if (ext4_map_blocks(NULL, inode, &map, 0) < 0) {
				set_error_page:
					folio_zero_segment(folio, 0,
							  folio_size(folio));
					folio_unlock(folio);
					goto next_page;
				}
			}
			if ((map.m_flags & EXT4_MAP_MAPPED) == 0) {
				fully_mapped = 0;
				if (first_hole == blocks_per_folio)
					first_hole = page_block;
				page_block++;
				block_in_file++;
				continue;
			}
			if (first_hole != blocks_per_folio)
				goto confused;		/* hole -> non-hole */

			/* Contiguous blocks? */
			if (!page_block)
				first_block = map.m_pblk;
			else if (first_block + page_block != map.m_pblk)
				goto confused;
			for (relative_block = 0; ; relative_block++) {
				if (relative_block == map.m_len) {
					/* needed? */
					map.m_flags &= ~EXT4_MAP_MAPPED;
					break;
				} else if (page_block == blocks_per_folio)
					break;
				page_block++;
				block_in_file++;
			}
		}
		if (first_hole != blocks_per_folio) {
			folio_zero_segment(folio, first_hole << blkbits,
					  folio_size(folio));
			if (first_hole == 0) {
				if (vi && !fsverity_verify_folio(vi, folio))
					goto set_error_page;
				folio_end_read(folio, true);
				continue;
			}
		} else if (fully_mapped) {
			folio_set_mappedtodisk(folio);
		}

		/*
		 * This folio will go to BIO.  Do we need to send this
		 * BIO off first?
		 */
		if (bio && (last_block_in_bio != first_block - 1 ||
			    !fscrypt_mergeable_bio(bio, inode, pos))) {
		submit_and_realloc:
			blk_crypto_submit_bio(bio);
			bio = NULL;
		}
		if (bio == NULL) {
			/*
			 * bio_alloc will _always_ be able to allocate a bio if
			 * __GFP_DIRECT_RECLAIM is set, see bio_alloc_bioset().
			 */
			bio = bio_alloc(bdev, bio_max_segs(nr_pages),
					REQ_OP_READ, GFP_KERNEL);
			fscrypt_set_bio_crypt_ctx(bio, inode, pos, GFP_KERNEL);
			ext4_set_verity_work(bio, vi);
			bio->bi_iter.bi_sector = first_block << (blkbits - 9);
			bio->bi_end_io = mpage_end_io;
			if (rac)
				bio->bi_opf |= REQ_RAHEAD;
		}

		length = first_hole << blkbits;
		if (!bio_add_folio(bio, folio, length, 0))
			goto submit_and_realloc;

		if (((map.m_flags & EXT4_MAP_BOUNDARY) &&
		     (relative_block == map.m_len)) ||
		    (first_hole != blocks_per_folio)) {
			blk_crypto_submit_bio(bio);
			bio = NULL;
		} else
			last_block_in_bio = first_block + blocks_per_folio - 1;
		continue;
	confused:
		if (bio) {
			blk_crypto_submit_bio(bio);
			bio = NULL;
		}
		if (!folio_test_uptodate(folio))
			block_read_full_folio(folio, ext4_get_block);
		else
			folio_unlock(folio);
next_page:
		; /* A label shall be followed by a statement until C23 */
	}
	if (bio)
		blk_crypto_submit_bio(bio);
	return 0;
}

int ext4_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct fsverity_info *vi = NULL;
	int ret;

	trace_ext4_read_folio(inode, folio);

	if (ext4_has_inline_data(inode)) {
		ret = ext4_readpage_inline(inode, folio);
		if (ret != -EAGAIN)
			return ret;
	}

	if (folio->index < DIV_ROUND_UP(inode->i_size, PAGE_SIZE))
		vi = fsverity_get_info(inode);
	if (vi)
		fsverity_readahead(vi, folio->index, folio_nr_pages(folio));
	return ext4_mpage_readpages(inode, vi, NULL, folio);
}

void ext4_readahead(struct readahead_control *rac)
{
	struct inode *inode = rac->mapping->host;
	struct fsverity_info *vi = NULL;

	/* If the file has inline data, no need to do readahead. */
	if (ext4_has_inline_data(inode))
		return;

	if (readahead_index(rac) < DIV_ROUND_UP(inode->i_size, PAGE_SIZE))
		vi = fsverity_get_info(inode);
	if (vi)
		fsverity_readahead(vi, readahead_index(rac),
				   readahead_count(rac));
	ext4_mpage_readpages(inode, vi, rac, NULL);
}

int __init ext4_init_verity_caches(void)
{
	if (!IS_ENABLED(CONFIG_FS_VERITY))
		return 0;
	ext4_verity_work_cache =
		KMEM_CACHE(ext4_verity_work, SLAB_RECLAIM_ACCOUNT);

	if (!ext4_verity_work_cache)
		goto fail;
	ext4_verity_work_pool = mempool_create_slab_pool(
		NUM_VERITY_WORKS, ext4_verity_work_cache);
	if (!ext4_verity_work_pool)
		goto fail_free_cache;
	return 0;

fail_free_cache:
	kmem_cache_destroy(ext4_verity_work_cache);
fail:
	return -ENOMEM;
}

void ext4_exit_verity_caches(void)
{
	if (!IS_ENABLED(CONFIG_FS_VERITY))
		return;
	mempool_destroy(ext4_verity_work_pool);
	kmem_cache_destroy(ext4_verity_work_cache);
}
