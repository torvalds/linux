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
#include <trace/events/android_fs.h>

#define NUM_PREALLOC_POST_READ_CTXS	128

static struct kmem_cache *bio_post_read_ctx_cache;
static mempool_t *bio_post_read_ctx_pool;

/* postprocessing steps for read bios */
enum bio_post_read_step {
	STEP_INITIAL = 0,
	STEP_DECRYPT,
	STEP_VERITY,
};

struct bio_post_read_ctx {
	struct bio *bio;
	struct work_struct work;
	unsigned int cur_step;
	unsigned int enabled_steps;
};

static void __read_end_io(struct bio *bio)
{
	struct page *page;
	struct bio_vec *bv;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bv, bio, iter_all) {
		page = bv->bv_page;

		/* PG_error was set if any post_read step failed */
		if (bio->bi_status || PageError(page)) {
			ClearPageUptodate(page);
			/* will re-read again later */
			ClearPageError(page);
		} else {
			SetPageUptodate(page);
		}
		unlock_page(page);
	}
	if (bio->bi_private)
		mempool_free(bio->bi_private, bio_post_read_ctx_pool);
	bio_put(bio);
}

static void bio_post_read_processing(struct bio_post_read_ctx *ctx);

static void decrypt_work(struct work_struct *work)
{
	struct bio_post_read_ctx *ctx =
		container_of(work, struct bio_post_read_ctx, work);

	fscrypt_decrypt_bio(ctx->bio);

	bio_post_read_processing(ctx);
}

static void verity_work(struct work_struct *work)
{
	struct bio_post_read_ctx *ctx =
		container_of(work, struct bio_post_read_ctx, work);

	fsverity_verify_bio(ctx->bio);

	bio_post_read_processing(ctx);
}

static void bio_post_read_processing(struct bio_post_read_ctx *ctx)
{
	/*
	 * We use different work queues for decryption and for verity because
	 * verity may require reading metadata pages that need decryption, and
	 * we shouldn't recurse to the same workqueue.
	 */
	switch (++ctx->cur_step) {
	case STEP_DECRYPT:
		if (ctx->enabled_steps & (1 << STEP_DECRYPT)) {
			INIT_WORK(&ctx->work, decrypt_work);
			fscrypt_enqueue_decrypt_work(&ctx->work);
			return;
		}
		ctx->cur_step++;
		/* fall-through */
	case STEP_VERITY:
		if (ctx->enabled_steps & (1 << STEP_VERITY)) {
			INIT_WORK(&ctx->work, verity_work);
			fsverity_enqueue_verify_work(&ctx->work);
			return;
		}
		ctx->cur_step++;
		/* fall-through */
	default:
		__read_end_io(ctx->bio);
	}
}

static bool bio_post_read_required(struct bio *bio)
{
	return bio->bi_private && !bio->bi_status;
}

static void
ext4_trace_read_completion(struct bio *bio)
{
	struct page *first_page = bio->bi_io_vec[0].bv_page;

	if (first_page != NULL)
		trace_android_fs_dataread_end(first_page->mapping->host,
					      page_offset(first_page),
					      bio->bi_iter.bi_size);
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
	if (trace_android_fs_dataread_start_enabled())
		ext4_trace_read_completion(bio);

	if (bio_post_read_required(bio)) {
		struct bio_post_read_ctx *ctx = bio->bi_private;

		ctx->cur_step = STEP_INITIAL;
		bio_post_read_processing(ctx);
		return;
	}
	__read_end_io(bio);
}

static inline bool ext4_need_verity(const struct inode *inode, pgoff_t idx)
{
	return fsverity_active(inode) &&
	       idx < DIV_ROUND_UP(inode->i_size, PAGE_SIZE);
}

static struct bio_post_read_ctx *get_bio_post_read_ctx(struct inode *inode,
						       struct bio *bio,
						       pgoff_t first_idx)
{
	unsigned int post_read_steps = 0;
	struct bio_post_read_ctx *ctx = NULL;

	if (fscrypt_inode_uses_fs_layer_crypto(inode))
		post_read_steps |= 1 << STEP_DECRYPT;

	if (ext4_need_verity(inode, first_idx))
		post_read_steps |= 1 << STEP_VERITY;

	if (post_read_steps) {
		ctx = mempool_alloc(bio_post_read_ctx_pool, GFP_NOFS);
		if (!ctx)
			return ERR_PTR(-ENOMEM);
		ctx->bio = bio;
		ctx->enabled_steps = post_read_steps;
		bio->bi_private = ctx;
	}
	return ctx;
}

static inline loff_t ext4_readpage_limit(struct inode *inode)
{
	if (IS_ENABLED(CONFIG_FS_VERITY) &&
	    (IS_VERITY(inode) || ext4_verity_in_progress(inode)))
		return inode->i_sb->s_maxbytes;

	return i_size_read(inode);
}

static void
ext4_submit_bio_read(struct bio *bio)
{
	if (trace_android_fs_dataread_start_enabled()) {
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
	submit_bio(bio);
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
	sector_t next_block;
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

		if (pages) {
			page = lru_to_page(pages);

			prefetchw(&page->flags);
			list_del(&page->lru);
			if (add_to_page_cache_lru(page, mapping, page->index,
				  readahead_gfp_mask(mapping)))
				goto next_page;
		}

		if (page_has_buffers(page))
			goto confused;

		block_in_file = next_block =
			(sector_t)page->index << (PAGE_SHIFT - blkbits);
		last_block = block_in_file + nr_pages * blocks_per_page;
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
				if (ext4_need_verity(inode, page->index) &&
				    !fsverity_verify_page(page))
					goto set_error_page;
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
		if (bio && (last_block_in_bio != blocks[0] - 1 ||
			    !fscrypt_mergeable_bio(bio, inode, next_block))) {
		submit_and_realloc:
			ext4_submit_bio_read(bio);
			bio = NULL;
		}
		if (bio == NULL) {
			struct bio_post_read_ctx *ctx;

			bio = bio_alloc(GFP_KERNEL,
				min_t(int, nr_pages, BIO_MAX_PAGES));
			if (!bio)
				goto set_error_page;
			if (fscrypt_set_bio_crypt_ctx(bio, inode, next_block,
						      GFP_KERNEL) != 0) {
				bio_put(bio);
				bio = NULL;
				goto set_error_page;
			}
			ctx = get_bio_post_read_ctx(inode, bio, page->index);
			if (IS_ERR(ctx)) {
				bio_put(bio);
				bio = NULL;
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
			ext4_submit_bio_read(bio);
			bio = NULL;
		} else
			last_block_in_bio = blocks[blocks_per_page - 1];
		goto next_page;
	confused:
		if (bio) {
			ext4_submit_bio_read(bio);
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
		ext4_submit_bio_read(bio);
	return 0;
}

int __init ext4_init_post_read_processing(void)
{
	bio_post_read_ctx_cache =
		kmem_cache_create("ext4_bio_post_read_ctx",
				  sizeof(struct bio_post_read_ctx), 0, 0, NULL);
	if (!bio_post_read_ctx_cache)
		goto fail;
	bio_post_read_ctx_pool =
		mempool_create_slab_pool(NUM_PREALLOC_POST_READ_CTXS,
					 bio_post_read_ctx_cache);
	if (!bio_post_read_ctx_pool)
		goto fail_free_cache;
	return 0;

fail_free_cache:
	kmem_cache_destroy(bio_post_read_ctx_cache);
fail:
	return -ENOMEM;
}

void ext4_exit_post_read_processing(void)
{
	mempool_destroy(bio_post_read_ctx_pool);
	kmem_cache_destroy(bio_post_read_ctx_cache);
}
