// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>
#include <linux/psi.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/log2.h>
#include <crypto/hash.h>
#include "misc.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "ordered-data.h"
#include "compression.h"
#include "extent_io.h"
#include "extent_map.h"
#include "subpage.h"
#include "zoned.h"

static const char* const btrfs_compress_types[] = { "", "zlib", "lzo", "zstd" };

const char* btrfs_compress_type2str(enum btrfs_compression_type type)
{
	switch (type) {
	case BTRFS_COMPRESS_ZLIB:
	case BTRFS_COMPRESS_LZO:
	case BTRFS_COMPRESS_ZSTD:
	case BTRFS_COMPRESS_NONE:
		return btrfs_compress_types[type];
	default:
		break;
	}

	return NULL;
}

bool btrfs_compress_is_valid_type(const char *str, size_t len)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(btrfs_compress_types); i++) {
		size_t comp_len = strlen(btrfs_compress_types[i]);

		if (len < comp_len)
			continue;

		if (!strncmp(btrfs_compress_types[i], str, comp_len))
			return true;
	}
	return false;
}

static int compression_compress_pages(int type, struct list_head *ws,
               struct address_space *mapping, u64 start, struct page **pages,
               unsigned long *out_pages, unsigned long *total_in,
               unsigned long *total_out)
{
	switch (type) {
	case BTRFS_COMPRESS_ZLIB:
		return zlib_compress_pages(ws, mapping, start, pages,
				out_pages, total_in, total_out);
	case BTRFS_COMPRESS_LZO:
		return lzo_compress_pages(ws, mapping, start, pages,
				out_pages, total_in, total_out);
	case BTRFS_COMPRESS_ZSTD:
		return zstd_compress_pages(ws, mapping, start, pages,
				out_pages, total_in, total_out);
	case BTRFS_COMPRESS_NONE:
	default:
		/*
		 * This can happen when compression races with remount setting
		 * it to 'no compress', while caller doesn't call
		 * inode_need_compress() to check if we really need to
		 * compress.
		 *
		 * Not a big deal, just need to inform caller that we
		 * haven't allocated any pages yet.
		 */
		*out_pages = 0;
		return -E2BIG;
	}
}

static int compression_decompress_bio(struct list_head *ws,
				      struct compressed_bio *cb)
{
	switch (cb->compress_type) {
	case BTRFS_COMPRESS_ZLIB: return zlib_decompress_bio(ws, cb);
	case BTRFS_COMPRESS_LZO:  return lzo_decompress_bio(ws, cb);
	case BTRFS_COMPRESS_ZSTD: return zstd_decompress_bio(ws, cb);
	case BTRFS_COMPRESS_NONE:
	default:
		/*
		 * This can't happen, the type is validated several times
		 * before we get here.
		 */
		BUG();
	}
}

static int compression_decompress(int type, struct list_head *ws,
               unsigned char *data_in, struct page *dest_page,
               unsigned long start_byte, size_t srclen, size_t destlen)
{
	switch (type) {
	case BTRFS_COMPRESS_ZLIB: return zlib_decompress(ws, data_in, dest_page,
						start_byte, srclen, destlen);
	case BTRFS_COMPRESS_LZO:  return lzo_decompress(ws, data_in, dest_page,
						start_byte, srclen, destlen);
	case BTRFS_COMPRESS_ZSTD: return zstd_decompress(ws, data_in, dest_page,
						start_byte, srclen, destlen);
	case BTRFS_COMPRESS_NONE:
	default:
		/*
		 * This can't happen, the type is validated several times
		 * before we get here.
		 */
		BUG();
	}
}

static int btrfs_decompress_bio(struct compressed_bio *cb);

static void finish_compressed_bio_read(struct compressed_bio *cb)
{
	unsigned int index;
	struct page *page;

	if (cb->status == BLK_STS_OK)
		cb->status = errno_to_blk_status(btrfs_decompress_bio(cb));

	/* Release the compressed pages */
	for (index = 0; index < cb->nr_pages; index++) {
		page = cb->compressed_pages[index];
		page->mapping = NULL;
		put_page(page);
	}

	/* Do io completion on the original bio */
	btrfs_bio_end_io(btrfs_bio(cb->orig_bio), cb->status);

	/* Finally free the cb struct */
	kfree(cb->compressed_pages);
	kfree(cb);
}

/*
 * Verify the checksums and kick off repair if needed on the uncompressed data
 * before decompressing it into the original bio and freeing the uncompressed
 * pages.
 */
static void end_compressed_bio_read(struct btrfs_bio *bbio)
{
	struct compressed_bio *cb = bbio->private;
	struct inode *inode = cb->inode;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_inode *bi = BTRFS_I(inode);
	bool csum = !(bi->flags & BTRFS_INODE_NODATASUM) &&
		    !test_bit(BTRFS_FS_STATE_NO_CSUMS, &fs_info->fs_state);
	blk_status_t status = bbio->bio.bi_status;
	struct bvec_iter iter;
	struct bio_vec bv;
	u32 offset;

	btrfs_bio_for_each_sector(fs_info, bv, bbio, iter, offset) {
		u64 start = bbio->file_offset + offset;

		if (!status &&
		    (!csum || !btrfs_check_data_csum(inode, bbio, offset,
						     bv.bv_page, bv.bv_offset))) {
			btrfs_clean_io_failure(bi, start, bv.bv_page,
					       bv.bv_offset);
		} else {
			int ret;

			refcount_inc(&cb->pending_ios);
			ret = btrfs_repair_one_sector(inode, bbio, offset,
						      bv.bv_page, bv.bv_offset,
						      btrfs_submit_data_read_bio);
			if (ret) {
				refcount_dec(&cb->pending_ios);
				status = errno_to_blk_status(ret);
			}
		}
	}

	if (status)
		cb->status = status;

	if (refcount_dec_and_test(&cb->pending_ios))
		finish_compressed_bio_read(cb);
	btrfs_bio_free_csum(bbio);
	bio_put(&bbio->bio);
}

/*
 * Clear the writeback bits on all of the file
 * pages for a compressed write
 */
static noinline void end_compressed_writeback(struct inode *inode,
					      const struct compressed_bio *cb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	unsigned long index = cb->start >> PAGE_SHIFT;
	unsigned long end_index = (cb->start + cb->len - 1) >> PAGE_SHIFT;
	struct folio_batch fbatch;
	const int errno = blk_status_to_errno(cb->status);
	int i;
	int ret;

	if (errno)
		mapping_set_error(inode->i_mapping, errno);

	folio_batch_init(&fbatch);
	while (index <= end_index) {
		ret = filemap_get_folios(inode->i_mapping, &index, end_index,
				&fbatch);

		if (ret == 0)
			return;

		for (i = 0; i < ret; i++) {
			struct folio *folio = fbatch.folios[i];

			if (errno)
				folio_set_error(folio);
			btrfs_page_clamp_clear_writeback(fs_info, &folio->page,
							 cb->start, cb->len);
		}
		folio_batch_release(&fbatch);
	}
	/* the inode may be gone now */
}

static void finish_compressed_bio_write(struct compressed_bio *cb)
{
	struct inode *inode = cb->inode;
	unsigned int index;

	/*
	 * Ok, we're the last bio for this extent, step one is to call back
	 * into the FS and do all the end_io operations.
	 */
	btrfs_writepage_endio_finish_ordered(BTRFS_I(inode), NULL,
			cb->start, cb->start + cb->len - 1,
			cb->status == BLK_STS_OK);

	if (cb->writeback)
		end_compressed_writeback(inode, cb);
	/* Note, our inode could be gone now */

	/*
	 * Release the compressed pages, these came from alloc_page and
	 * are not attached to the inode at all
	 */
	for (index = 0; index < cb->nr_pages; index++) {
		struct page *page = cb->compressed_pages[index];

		page->mapping = NULL;
		put_page(page);
	}

	/* Finally free the cb struct */
	kfree(cb->compressed_pages);
	kfree(cb);
}

static void btrfs_finish_compressed_write_work(struct work_struct *work)
{
	struct compressed_bio *cb =
		container_of(work, struct compressed_bio, write_end_work);

	finish_compressed_bio_write(cb);
}

/*
 * Do the cleanup once all the compressed pages hit the disk.  This will clear
 * writeback on the file pages and free the compressed pages.
 *
 * This also calls the writeback end hooks for the file pages so that metadata
 * and checksums can be updated in the file.
 */
static void end_compressed_bio_write(struct btrfs_bio *bbio)
{
	struct compressed_bio *cb = bbio->private;

	if (bbio->bio.bi_status)
		cb->status = bbio->bio.bi_status;

	if (refcount_dec_and_test(&cb->pending_ios)) {
		struct btrfs_fs_info *fs_info = btrfs_sb(cb->inode->i_sb);

		btrfs_record_physical_zoned(cb->inode, cb->start, &bbio->bio);
		queue_work(fs_info->compressed_write_workers, &cb->write_end_work);
	}
	bio_put(&bbio->bio);
}

/*
 * Allocate a compressed_bio, which will be used to read/write on-disk
 * (aka, compressed) * data.
 *
 * @cb:                 The compressed_bio structure, which records all the needed
 *                      information to bind the compressed data to the uncompressed
 *                      page cache.
 * @disk_byten:         The logical bytenr where the compressed data will be read
 *                      from or written to.
 * @endio_func:         The endio function to call after the IO for compressed data
 *                      is finished.
 * @next_stripe_start:  Return value of logical bytenr of where next stripe starts.
 *                      Let the caller know to only fill the bio up to the stripe
 *                      boundary.
 */


static struct bio *alloc_compressed_bio(struct compressed_bio *cb, u64 disk_bytenr,
					blk_opf_t opf,
					btrfs_bio_end_io_t endio_func,
					u64 *next_stripe_start)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(cb->inode->i_sb);
	struct btrfs_io_geometry geom;
	struct extent_map *em;
	struct bio *bio;
	int ret;

	bio = btrfs_bio_alloc(BIO_MAX_VECS, opf, endio_func, cb);
	bio->bi_iter.bi_sector = disk_bytenr >> SECTOR_SHIFT;

	em = btrfs_get_chunk_map(fs_info, disk_bytenr, fs_info->sectorsize);
	if (IS_ERR(em)) {
		bio_put(bio);
		return ERR_CAST(em);
	}

	if (bio_op(bio) == REQ_OP_ZONE_APPEND)
		bio_set_dev(bio, em->map_lookup->stripes[0].dev->bdev);

	ret = btrfs_get_io_geometry(fs_info, em, btrfs_op(bio), disk_bytenr, &geom);
	free_extent_map(em);
	if (ret < 0) {
		bio_put(bio);
		return ERR_PTR(ret);
	}
	*next_stripe_start = disk_bytenr + geom.len;
	refcount_inc(&cb->pending_ios);
	return bio;
}

/*
 * worker function to build and submit bios for previously compressed pages.
 * The corresponding pages in the inode should be marked for writeback
 * and the compressed pages should have a reference on them for dropping
 * when the IO is complete.
 *
 * This also checksums the file bytes and gets things ready for
 * the end io hooks.
 */
blk_status_t btrfs_submit_compressed_write(struct btrfs_inode *inode, u64 start,
				 unsigned int len, u64 disk_start,
				 unsigned int compressed_len,
				 struct page **compressed_pages,
				 unsigned int nr_pages,
				 blk_opf_t write_flags,
				 struct cgroup_subsys_state *blkcg_css,
				 bool writeback)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct bio *bio = NULL;
	struct compressed_bio *cb;
	u64 cur_disk_bytenr = disk_start;
	u64 next_stripe_start;
	blk_status_t ret = BLK_STS_OK;
	int skip_sum = inode->flags & BTRFS_INODE_NODATASUM;
	const bool use_append = btrfs_use_zone_append(inode, disk_start);
	const enum req_op bio_op = use_append ? REQ_OP_ZONE_APPEND : REQ_OP_WRITE;

	ASSERT(IS_ALIGNED(start, fs_info->sectorsize) &&
	       IS_ALIGNED(len, fs_info->sectorsize));
	cb = kmalloc(sizeof(struct compressed_bio), GFP_NOFS);
	if (!cb)
		return BLK_STS_RESOURCE;
	refcount_set(&cb->pending_ios, 1);
	cb->status = BLK_STS_OK;
	cb->inode = &inode->vfs_inode;
	cb->start = start;
	cb->len = len;
	cb->compressed_pages = compressed_pages;
	cb->compressed_len = compressed_len;
	cb->writeback = writeback;
	INIT_WORK(&cb->write_end_work, btrfs_finish_compressed_write_work);
	cb->nr_pages = nr_pages;

	if (blkcg_css)
		kthread_associate_blkcg(blkcg_css);

	while (cur_disk_bytenr < disk_start + compressed_len) {
		u64 offset = cur_disk_bytenr - disk_start;
		unsigned int index = offset >> PAGE_SHIFT;
		unsigned int real_size;
		unsigned int added;
		struct page *page = compressed_pages[index];
		bool submit = false;

		/* Allocate new bio if submitted or not yet allocated */
		if (!bio) {
			bio = alloc_compressed_bio(cb, cur_disk_bytenr,
				bio_op | write_flags, end_compressed_bio_write,
				&next_stripe_start);
			if (IS_ERR(bio)) {
				ret = errno_to_blk_status(PTR_ERR(bio));
				break;
			}
			if (blkcg_css)
				bio->bi_opf |= REQ_CGROUP_PUNT;
		}
		/*
		 * We should never reach next_stripe_start start as we will
		 * submit comp_bio when reach the boundary immediately.
		 */
		ASSERT(cur_disk_bytenr != next_stripe_start);

		/*
		 * We have various limits on the real read size:
		 * - stripe boundary
		 * - page boundary
		 * - compressed length boundary
		 */
		real_size = min_t(u64, U32_MAX, next_stripe_start - cur_disk_bytenr);
		real_size = min_t(u64, real_size, PAGE_SIZE - offset_in_page(offset));
		real_size = min_t(u64, real_size, compressed_len - offset);
		ASSERT(IS_ALIGNED(real_size, fs_info->sectorsize));

		if (use_append)
			added = bio_add_zone_append_page(bio, page, real_size,
					offset_in_page(offset));
		else
			added = bio_add_page(bio, page, real_size,
					offset_in_page(offset));
		/* Reached zoned boundary */
		if (added == 0)
			submit = true;

		cur_disk_bytenr += added;
		/* Reached stripe boundary */
		if (cur_disk_bytenr == next_stripe_start)
			submit = true;

		/* Finished the range */
		if (cur_disk_bytenr == disk_start + compressed_len)
			submit = true;

		if (submit) {
			if (!skip_sum) {
				ret = btrfs_csum_one_bio(inode, bio, start, true);
				if (ret) {
					btrfs_bio_end_io(btrfs_bio(bio), ret);
					break;
				}
			}

			ASSERT(bio->bi_iter.bi_size);
			btrfs_submit_bio(fs_info, bio, 0);
			bio = NULL;
		}
		cond_resched();
	}

	if (blkcg_css)
		kthread_associate_blkcg(NULL);

	if (refcount_dec_and_test(&cb->pending_ios))
		finish_compressed_bio_write(cb);
	return ret;
}

static u64 bio_end_offset(struct bio *bio)
{
	struct bio_vec *last = bio_last_bvec_all(bio);

	return page_offset(last->bv_page) + last->bv_len + last->bv_offset;
}

/*
 * Add extra pages in the same compressed file extent so that we don't need to
 * re-read the same extent again and again.
 *
 * NOTE: this won't work well for subpage, as for subpage read, we lock the
 * full page then submit bio for each compressed/regular extents.
 *
 * This means, if we have several sectors in the same page points to the same
 * on-disk compressed data, we will re-read the same extent many times and
 * this function can only help for the next page.
 */
static noinline int add_ra_bio_pages(struct inode *inode,
				     u64 compressed_end,
				     struct compressed_bio *cb,
				     int *memstall, unsigned long *pflags)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	unsigned long end_index;
	u64 cur = bio_end_offset(cb->orig_bio);
	u64 isize = i_size_read(inode);
	int ret;
	struct page *page;
	struct extent_map *em;
	struct address_space *mapping = inode->i_mapping;
	struct extent_map_tree *em_tree;
	struct extent_io_tree *tree;
	int sectors_missed = 0;

	em_tree = &BTRFS_I(inode)->extent_tree;
	tree = &BTRFS_I(inode)->io_tree;

	if (isize == 0)
		return 0;

	/*
	 * For current subpage support, we only support 64K page size,
	 * which means maximum compressed extent size (128K) is just 2x page
	 * size.
	 * This makes readahead less effective, so here disable readahead for
	 * subpage for now, until full compressed write is supported.
	 */
	if (btrfs_sb(inode->i_sb)->sectorsize < PAGE_SIZE)
		return 0;

	end_index = (i_size_read(inode) - 1) >> PAGE_SHIFT;

	while (cur < compressed_end) {
		u64 page_end;
		u64 pg_index = cur >> PAGE_SHIFT;
		u32 add_size;

		if (pg_index > end_index)
			break;

		page = xa_load(&mapping->i_pages, pg_index);
		if (page && !xa_is_value(page)) {
			sectors_missed += (PAGE_SIZE - offset_in_page(cur)) >>
					  fs_info->sectorsize_bits;

			/* Beyond threshold, no need to continue */
			if (sectors_missed > 4)
				break;

			/*
			 * Jump to next page start as we already have page for
			 * current offset.
			 */
			cur = (pg_index << PAGE_SHIFT) + PAGE_SIZE;
			continue;
		}

		page = __page_cache_alloc(mapping_gfp_constraint(mapping,
								 ~__GFP_FS));
		if (!page)
			break;

		if (add_to_page_cache_lru(page, mapping, pg_index, GFP_NOFS)) {
			put_page(page);
			/* There is already a page, skip to page end */
			cur = (pg_index << PAGE_SHIFT) + PAGE_SIZE;
			continue;
		}

		if (!*memstall && PageWorkingset(page)) {
			psi_memstall_enter(pflags);
			*memstall = 1;
		}

		ret = set_page_extent_mapped(page);
		if (ret < 0) {
			unlock_page(page);
			put_page(page);
			break;
		}

		page_end = (pg_index << PAGE_SHIFT) + PAGE_SIZE - 1;
		lock_extent(tree, cur, page_end, NULL);
		read_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, cur, page_end + 1 - cur);
		read_unlock(&em_tree->lock);

		/*
		 * At this point, we have a locked page in the page cache for
		 * these bytes in the file.  But, we have to make sure they map
		 * to this compressed extent on disk.
		 */
		if (!em || cur < em->start ||
		    (cur + fs_info->sectorsize > extent_map_end(em)) ||
		    (em->block_start >> 9) != cb->orig_bio->bi_iter.bi_sector) {
			free_extent_map(em);
			unlock_extent(tree, cur, page_end, NULL);
			unlock_page(page);
			put_page(page);
			break;
		}
		free_extent_map(em);

		if (page->index == end_index) {
			size_t zero_offset = offset_in_page(isize);

			if (zero_offset) {
				int zeros;
				zeros = PAGE_SIZE - zero_offset;
				memzero_page(page, zero_offset, zeros);
			}
		}

		add_size = min(em->start + em->len, page_end + 1) - cur;
		ret = bio_add_page(cb->orig_bio, page, add_size, offset_in_page(cur));
		if (ret != add_size) {
			unlock_extent(tree, cur, page_end, NULL);
			unlock_page(page);
			put_page(page);
			break;
		}
		/*
		 * If it's subpage, we also need to increase its
		 * subpage::readers number, as at endio we will decrease
		 * subpage::readers and to unlock the page.
		 */
		if (fs_info->sectorsize < PAGE_SIZE)
			btrfs_subpage_start_reader(fs_info, page, cur, add_size);
		put_page(page);
		cur += add_size;
	}
	return 0;
}

/*
 * for a compressed read, the bio we get passed has all the inode pages
 * in it.  We don't actually do IO on those pages but allocate new ones
 * to hold the compressed pages on disk.
 *
 * bio->bi_iter.bi_sector points to the compressed extent on disk
 * bio->bi_io_vec points to all of the inode pages
 *
 * After the compressed pages are read, we copy the bytes into the
 * bio we were passed and then call the bio end_io calls
 */
void btrfs_submit_compressed_read(struct inode *inode, struct bio *bio,
				  int mirror_num)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_map_tree *em_tree;
	struct compressed_bio *cb;
	unsigned int compressed_len;
	struct bio *comp_bio = NULL;
	const u64 disk_bytenr = bio->bi_iter.bi_sector << SECTOR_SHIFT;
	u64 cur_disk_byte = disk_bytenr;
	u64 next_stripe_start;
	u64 file_offset;
	u64 em_len;
	u64 em_start;
	struct extent_map *em;
	unsigned long pflags;
	int memstall = 0;
	blk_status_t ret;
	int ret2;
	int i;

	em_tree = &BTRFS_I(inode)->extent_tree;

	file_offset = bio_first_bvec_all(bio)->bv_offset +
		      page_offset(bio_first_page_all(bio));

	/* we need the actual starting offset of this extent in the file */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, file_offset, fs_info->sectorsize);
	read_unlock(&em_tree->lock);
	if (!em) {
		ret = BLK_STS_IOERR;
		goto out;
	}

	ASSERT(em->compress_type != BTRFS_COMPRESS_NONE);
	compressed_len = em->block_len;
	cb = kmalloc(sizeof(struct compressed_bio), GFP_NOFS);
	if (!cb) {
		ret = BLK_STS_RESOURCE;
		goto out;
	}

	refcount_set(&cb->pending_ios, 1);
	cb->status = BLK_STS_OK;
	cb->inode = inode;

	cb->start = em->orig_start;
	em_len = em->len;
	em_start = em->start;

	cb->len = bio->bi_iter.bi_size;
	cb->compressed_len = compressed_len;
	cb->compress_type = em->compress_type;
	cb->orig_bio = bio;

	free_extent_map(em);
	em = NULL;

	cb->nr_pages = DIV_ROUND_UP(compressed_len, PAGE_SIZE);
	cb->compressed_pages = kcalloc(cb->nr_pages, sizeof(struct page *), GFP_NOFS);
	if (!cb->compressed_pages) {
		ret = BLK_STS_RESOURCE;
		goto fail;
	}

	ret2 = btrfs_alloc_page_array(cb->nr_pages, cb->compressed_pages);
	if (ret2) {
		ret = BLK_STS_RESOURCE;
		goto fail;
	}

	add_ra_bio_pages(inode, em_start + em_len, cb, &memstall, &pflags);

	/* include any pages we added in add_ra-bio_pages */
	cb->len = bio->bi_iter.bi_size;

	while (cur_disk_byte < disk_bytenr + compressed_len) {
		u64 offset = cur_disk_byte - disk_bytenr;
		unsigned int index = offset >> PAGE_SHIFT;
		unsigned int real_size;
		unsigned int added;
		struct page *page = cb->compressed_pages[index];
		bool submit = false;

		/* Allocate new bio if submitted or not yet allocated */
		if (!comp_bio) {
			comp_bio = alloc_compressed_bio(cb, cur_disk_byte,
					REQ_OP_READ, end_compressed_bio_read,
					&next_stripe_start);
			if (IS_ERR(comp_bio)) {
				cb->status = errno_to_blk_status(PTR_ERR(comp_bio));
				break;
			}
		}
		/*
		 * We should never reach next_stripe_start start as we will
		 * submit comp_bio when reach the boundary immediately.
		 */
		ASSERT(cur_disk_byte != next_stripe_start);
		/*
		 * We have various limit on the real read size:
		 * - stripe boundary
		 * - page boundary
		 * - compressed length boundary
		 */
		real_size = min_t(u64, U32_MAX, next_stripe_start - cur_disk_byte);
		real_size = min_t(u64, real_size, PAGE_SIZE - offset_in_page(offset));
		real_size = min_t(u64, real_size, compressed_len - offset);
		ASSERT(IS_ALIGNED(real_size, fs_info->sectorsize));

		added = bio_add_page(comp_bio, page, real_size, offset_in_page(offset));
		/*
		 * Maximum compressed extent is smaller than bio size limit,
		 * thus bio_add_page() should always success.
		 */
		ASSERT(added == real_size);
		cur_disk_byte += added;

		/* Reached stripe boundary, need to submit */
		if (cur_disk_byte == next_stripe_start)
			submit = true;

		/* Has finished the range, need to submit */
		if (cur_disk_byte == disk_bytenr + compressed_len)
			submit = true;

		if (submit) {
			/* Save the original iter for read repair */
			if (bio_op(comp_bio) == REQ_OP_READ)
				btrfs_bio(comp_bio)->iter = comp_bio->bi_iter;

			/*
			 * Save the initial offset of this chunk, as there
			 * is no direct correlation between compressed pages and
			 * the original file offset.  The field is only used for
			 * priting error messages.
			 */
			btrfs_bio(comp_bio)->file_offset = file_offset;

			ret = btrfs_lookup_bio_sums(inode, comp_bio, NULL);
			if (ret) {
				btrfs_bio_end_io(btrfs_bio(comp_bio), ret);
				break;
			}

			ASSERT(comp_bio->bi_iter.bi_size);
			btrfs_submit_bio(fs_info, comp_bio, mirror_num);
			comp_bio = NULL;
		}
	}

	if (memstall)
		psi_memstall_leave(&pflags);

	if (refcount_dec_and_test(&cb->pending_ios))
		finish_compressed_bio_read(cb);
	return;

fail:
	if (cb->compressed_pages) {
		for (i = 0; i < cb->nr_pages; i++) {
			if (cb->compressed_pages[i])
				__free_page(cb->compressed_pages[i]);
		}
	}

	kfree(cb->compressed_pages);
	kfree(cb);
out:
	free_extent_map(em);
	btrfs_bio_end_io(btrfs_bio(bio), ret);
	return;
}

/*
 * Heuristic uses systematic sampling to collect data from the input data
 * range, the logic can be tuned by the following constants:
 *
 * @SAMPLING_READ_SIZE - how many bytes will be copied from for each sample
 * @SAMPLING_INTERVAL  - range from which the sampled data can be collected
 */
#define SAMPLING_READ_SIZE	(16)
#define SAMPLING_INTERVAL	(256)

/*
 * For statistical analysis of the input data we consider bytes that form a
 * Galois Field of 256 objects. Each object has an attribute count, ie. how
 * many times the object appeared in the sample.
 */
#define BUCKET_SIZE		(256)

/*
 * The size of the sample is based on a statistical sampling rule of thumb.
 * The common way is to perform sampling tests as long as the number of
 * elements in each cell is at least 5.
 *
 * Instead of 5, we choose 32 to obtain more accurate results.
 * If the data contain the maximum number of symbols, which is 256, we obtain a
 * sample size bound by 8192.
 *
 * For a sample of at most 8KB of data per data range: 16 consecutive bytes
 * from up to 512 locations.
 */
#define MAX_SAMPLE_SIZE		(BTRFS_MAX_UNCOMPRESSED *		\
				 SAMPLING_READ_SIZE / SAMPLING_INTERVAL)

struct bucket_item {
	u32 count;
};

struct heuristic_ws {
	/* Partial copy of input data */
	u8 *sample;
	u32 sample_size;
	/* Buckets store counters for each byte value */
	struct bucket_item *bucket;
	/* Sorting buffer */
	struct bucket_item *bucket_b;
	struct list_head list;
};

static struct workspace_manager heuristic_wsm;

static void free_heuristic_ws(struct list_head *ws)
{
	struct heuristic_ws *workspace;

	workspace = list_entry(ws, struct heuristic_ws, list);

	kvfree(workspace->sample);
	kfree(workspace->bucket);
	kfree(workspace->bucket_b);
	kfree(workspace);
}

static struct list_head *alloc_heuristic_ws(unsigned int level)
{
	struct heuristic_ws *ws;

	ws = kzalloc(sizeof(*ws), GFP_KERNEL);
	if (!ws)
		return ERR_PTR(-ENOMEM);

	ws->sample = kvmalloc(MAX_SAMPLE_SIZE, GFP_KERNEL);
	if (!ws->sample)
		goto fail;

	ws->bucket = kcalloc(BUCKET_SIZE, sizeof(*ws->bucket), GFP_KERNEL);
	if (!ws->bucket)
		goto fail;

	ws->bucket_b = kcalloc(BUCKET_SIZE, sizeof(*ws->bucket_b), GFP_KERNEL);
	if (!ws->bucket_b)
		goto fail;

	INIT_LIST_HEAD(&ws->list);
	return &ws->list;
fail:
	free_heuristic_ws(&ws->list);
	return ERR_PTR(-ENOMEM);
}

const struct btrfs_compress_op btrfs_heuristic_compress = {
	.workspace_manager = &heuristic_wsm,
};

static const struct btrfs_compress_op * const btrfs_compress_op[] = {
	/* The heuristic is represented as compression type 0 */
	&btrfs_heuristic_compress,
	&btrfs_zlib_compress,
	&btrfs_lzo_compress,
	&btrfs_zstd_compress,
};

static struct list_head *alloc_workspace(int type, unsigned int level)
{
	switch (type) {
	case BTRFS_COMPRESS_NONE: return alloc_heuristic_ws(level);
	case BTRFS_COMPRESS_ZLIB: return zlib_alloc_workspace(level);
	case BTRFS_COMPRESS_LZO:  return lzo_alloc_workspace(level);
	case BTRFS_COMPRESS_ZSTD: return zstd_alloc_workspace(level);
	default:
		/*
		 * This can't happen, the type is validated several times
		 * before we get here.
		 */
		BUG();
	}
}

static void free_workspace(int type, struct list_head *ws)
{
	switch (type) {
	case BTRFS_COMPRESS_NONE: return free_heuristic_ws(ws);
	case BTRFS_COMPRESS_ZLIB: return zlib_free_workspace(ws);
	case BTRFS_COMPRESS_LZO:  return lzo_free_workspace(ws);
	case BTRFS_COMPRESS_ZSTD: return zstd_free_workspace(ws);
	default:
		/*
		 * This can't happen, the type is validated several times
		 * before we get here.
		 */
		BUG();
	}
}

static void btrfs_init_workspace_manager(int type)
{
	struct workspace_manager *wsm;
	struct list_head *workspace;

	wsm = btrfs_compress_op[type]->workspace_manager;
	INIT_LIST_HEAD(&wsm->idle_ws);
	spin_lock_init(&wsm->ws_lock);
	atomic_set(&wsm->total_ws, 0);
	init_waitqueue_head(&wsm->ws_wait);

	/*
	 * Preallocate one workspace for each compression type so we can
	 * guarantee forward progress in the worst case
	 */
	workspace = alloc_workspace(type, 0);
	if (IS_ERR(workspace)) {
		pr_warn(
	"BTRFS: cannot preallocate compression workspace, will try later\n");
	} else {
		atomic_set(&wsm->total_ws, 1);
		wsm->free_ws = 1;
		list_add(workspace, &wsm->idle_ws);
	}
}

static void btrfs_cleanup_workspace_manager(int type)
{
	struct workspace_manager *wsman;
	struct list_head *ws;

	wsman = btrfs_compress_op[type]->workspace_manager;
	while (!list_empty(&wsman->idle_ws)) {
		ws = wsman->idle_ws.next;
		list_del(ws);
		free_workspace(type, ws);
		atomic_dec(&wsman->total_ws);
	}
}

/*
 * This finds an available workspace or allocates a new one.
 * If it's not possible to allocate a new one, waits until there's one.
 * Preallocation makes a forward progress guarantees and we do not return
 * errors.
 */
struct list_head *btrfs_get_workspace(int type, unsigned int level)
{
	struct workspace_manager *wsm;
	struct list_head *workspace;
	int cpus = num_online_cpus();
	unsigned nofs_flag;
	struct list_head *idle_ws;
	spinlock_t *ws_lock;
	atomic_t *total_ws;
	wait_queue_head_t *ws_wait;
	int *free_ws;

	wsm = btrfs_compress_op[type]->workspace_manager;
	idle_ws	 = &wsm->idle_ws;
	ws_lock	 = &wsm->ws_lock;
	total_ws = &wsm->total_ws;
	ws_wait	 = &wsm->ws_wait;
	free_ws	 = &wsm->free_ws;

again:
	spin_lock(ws_lock);
	if (!list_empty(idle_ws)) {
		workspace = idle_ws->next;
		list_del(workspace);
		(*free_ws)--;
		spin_unlock(ws_lock);
		return workspace;

	}
	if (atomic_read(total_ws) > cpus) {
		DEFINE_WAIT(wait);

		spin_unlock(ws_lock);
		prepare_to_wait(ws_wait, &wait, TASK_UNINTERRUPTIBLE);
		if (atomic_read(total_ws) > cpus && !*free_ws)
			schedule();
		finish_wait(ws_wait, &wait);
		goto again;
	}
	atomic_inc(total_ws);
	spin_unlock(ws_lock);

	/*
	 * Allocation helpers call vmalloc that can't use GFP_NOFS, so we have
	 * to turn it off here because we might get called from the restricted
	 * context of btrfs_compress_bio/btrfs_compress_pages
	 */
	nofs_flag = memalloc_nofs_save();
	workspace = alloc_workspace(type, level);
	memalloc_nofs_restore(nofs_flag);

	if (IS_ERR(workspace)) {
		atomic_dec(total_ws);
		wake_up(ws_wait);

		/*
		 * Do not return the error but go back to waiting. There's a
		 * workspace preallocated for each type and the compression
		 * time is bounded so we get to a workspace eventually. This
		 * makes our caller's life easier.
		 *
		 * To prevent silent and low-probability deadlocks (when the
		 * initial preallocation fails), check if there are any
		 * workspaces at all.
		 */
		if (atomic_read(total_ws) == 0) {
			static DEFINE_RATELIMIT_STATE(_rs,
					/* once per minute */ 60 * HZ,
					/* no burst */ 1);

			if (__ratelimit(&_rs)) {
				pr_warn("BTRFS: no compression workspaces, low memory, retrying\n");
			}
		}
		goto again;
	}
	return workspace;
}

static struct list_head *get_workspace(int type, int level)
{
	switch (type) {
	case BTRFS_COMPRESS_NONE: return btrfs_get_workspace(type, level);
	case BTRFS_COMPRESS_ZLIB: return zlib_get_workspace(level);
	case BTRFS_COMPRESS_LZO:  return btrfs_get_workspace(type, level);
	case BTRFS_COMPRESS_ZSTD: return zstd_get_workspace(level);
	default:
		/*
		 * This can't happen, the type is validated several times
		 * before we get here.
		 */
		BUG();
	}
}

/*
 * put a workspace struct back on the list or free it if we have enough
 * idle ones sitting around
 */
void btrfs_put_workspace(int type, struct list_head *ws)
{
	struct workspace_manager *wsm;
	struct list_head *idle_ws;
	spinlock_t *ws_lock;
	atomic_t *total_ws;
	wait_queue_head_t *ws_wait;
	int *free_ws;

	wsm = btrfs_compress_op[type]->workspace_manager;
	idle_ws	 = &wsm->idle_ws;
	ws_lock	 = &wsm->ws_lock;
	total_ws = &wsm->total_ws;
	ws_wait	 = &wsm->ws_wait;
	free_ws	 = &wsm->free_ws;

	spin_lock(ws_lock);
	if (*free_ws <= num_online_cpus()) {
		list_add(ws, idle_ws);
		(*free_ws)++;
		spin_unlock(ws_lock);
		goto wake;
	}
	spin_unlock(ws_lock);

	free_workspace(type, ws);
	atomic_dec(total_ws);
wake:
	cond_wake_up(ws_wait);
}

static void put_workspace(int type, struct list_head *ws)
{
	switch (type) {
	case BTRFS_COMPRESS_NONE: return btrfs_put_workspace(type, ws);
	case BTRFS_COMPRESS_ZLIB: return btrfs_put_workspace(type, ws);
	case BTRFS_COMPRESS_LZO:  return btrfs_put_workspace(type, ws);
	case BTRFS_COMPRESS_ZSTD: return zstd_put_workspace(ws);
	default:
		/*
		 * This can't happen, the type is validated several times
		 * before we get here.
		 */
		BUG();
	}
}

/*
 * Adjust @level according to the limits of the compression algorithm or
 * fallback to default
 */
static unsigned int btrfs_compress_set_level(int type, unsigned level)
{
	const struct btrfs_compress_op *ops = btrfs_compress_op[type];

	if (level == 0)
		level = ops->default_level;
	else
		level = min(level, ops->max_level);

	return level;
}

/*
 * Given an address space and start and length, compress the bytes into @pages
 * that are allocated on demand.
 *
 * @type_level is encoded algorithm and level, where level 0 means whatever
 * default the algorithm chooses and is opaque here;
 * - compression algo are 0-3
 * - the level are bits 4-7
 *
 * @out_pages is an in/out parameter, holds maximum number of pages to allocate
 * and returns number of actually allocated pages
 *
 * @total_in is used to return the number of bytes actually read.  It
 * may be smaller than the input length if we had to exit early because we
 * ran out of room in the pages array or because we cross the
 * max_out threshold.
 *
 * @total_out is an in/out parameter, must be set to the input length and will
 * be also used to return the total number of compressed bytes
 */
int btrfs_compress_pages(unsigned int type_level, struct address_space *mapping,
			 u64 start, struct page **pages,
			 unsigned long *out_pages,
			 unsigned long *total_in,
			 unsigned long *total_out)
{
	int type = btrfs_compress_type(type_level);
	int level = btrfs_compress_level(type_level);
	struct list_head *workspace;
	int ret;

	level = btrfs_compress_set_level(type, level);
	workspace = get_workspace(type, level);
	ret = compression_compress_pages(type, workspace, mapping, start, pages,
					 out_pages, total_in, total_out);
	put_workspace(type, workspace);
	return ret;
}

static int btrfs_decompress_bio(struct compressed_bio *cb)
{
	struct list_head *workspace;
	int ret;
	int type = cb->compress_type;

	workspace = get_workspace(type, 0);
	ret = compression_decompress_bio(workspace, cb);
	put_workspace(type, workspace);

	return ret;
}

/*
 * a less complex decompression routine.  Our compressed data fits in a
 * single page, and we want to read a single page out of it.
 * start_byte tells us the offset into the compressed data we're interested in
 */
int btrfs_decompress(int type, unsigned char *data_in, struct page *dest_page,
		     unsigned long start_byte, size_t srclen, size_t destlen)
{
	struct list_head *workspace;
	int ret;

	workspace = get_workspace(type, 0);
	ret = compression_decompress(type, workspace, data_in, dest_page,
				     start_byte, srclen, destlen);
	put_workspace(type, workspace);

	return ret;
}

void __init btrfs_init_compress(void)
{
	btrfs_init_workspace_manager(BTRFS_COMPRESS_NONE);
	btrfs_init_workspace_manager(BTRFS_COMPRESS_ZLIB);
	btrfs_init_workspace_manager(BTRFS_COMPRESS_LZO);
	zstd_init_workspace_manager();
}

void __cold btrfs_exit_compress(void)
{
	btrfs_cleanup_workspace_manager(BTRFS_COMPRESS_NONE);
	btrfs_cleanup_workspace_manager(BTRFS_COMPRESS_ZLIB);
	btrfs_cleanup_workspace_manager(BTRFS_COMPRESS_LZO);
	zstd_cleanup_workspace_manager();
}

/*
 * Copy decompressed data from working buffer to pages.
 *
 * @buf:		The decompressed data buffer
 * @buf_len:		The decompressed data length
 * @decompressed:	Number of bytes that are already decompressed inside the
 * 			compressed extent
 * @cb:			The compressed extent descriptor
 * @orig_bio:		The original bio that the caller wants to read for
 *
 * An easier to understand graph is like below:
 *
 * 		|<- orig_bio ->|     |<- orig_bio->|
 * 	|<-------      full decompressed extent      ----->|
 * 	|<-----------    @cb range   ---->|
 * 	|			|<-- @buf_len -->|
 * 	|<--- @decompressed --->|
 *
 * Note that, @cb can be a subpage of the full decompressed extent, but
 * @cb->start always has the same as the orig_file_offset value of the full
 * decompressed extent.
 *
 * When reading compressed extent, we have to read the full compressed extent,
 * while @orig_bio may only want part of the range.
 * Thus this function will ensure only data covered by @orig_bio will be copied
 * to.
 *
 * Return 0 if we have copied all needed contents for @orig_bio.
 * Return >0 if we need continue decompress.
 */
int btrfs_decompress_buf2page(const char *buf, u32 buf_len,
			      struct compressed_bio *cb, u32 decompressed)
{
	struct bio *orig_bio = cb->orig_bio;
	/* Offset inside the full decompressed extent */
	u32 cur_offset;

	cur_offset = decompressed;
	/* The main loop to do the copy */
	while (cur_offset < decompressed + buf_len) {
		struct bio_vec bvec;
		size_t copy_len;
		u32 copy_start;
		/* Offset inside the full decompressed extent */
		u32 bvec_offset;

		bvec = bio_iter_iovec(orig_bio, orig_bio->bi_iter);
		/*
		 * cb->start may underflow, but subtracting that value can still
		 * give us correct offset inside the full decompressed extent.
		 */
		bvec_offset = page_offset(bvec.bv_page) + bvec.bv_offset - cb->start;

		/* Haven't reached the bvec range, exit */
		if (decompressed + buf_len <= bvec_offset)
			return 1;

		copy_start = max(cur_offset, bvec_offset);
		copy_len = min(bvec_offset + bvec.bv_len,
			       decompressed + buf_len) - copy_start;
		ASSERT(copy_len);

		/*
		 * Extra range check to ensure we didn't go beyond
		 * @buf + @buf_len.
		 */
		ASSERT(copy_start - decompressed < buf_len);
		memcpy_to_page(bvec.bv_page, bvec.bv_offset,
			       buf + copy_start - decompressed, copy_len);
		cur_offset += copy_len;

		bio_advance(orig_bio, copy_len);
		/* Finished the bio */
		if (!orig_bio->bi_iter.bi_size)
			return 0;
	}
	return 1;
}

/*
 * Shannon Entropy calculation
 *
 * Pure byte distribution analysis fails to determine compressibility of data.
 * Try calculating entropy to estimate the average minimum number of bits
 * needed to encode the sampled data.
 *
 * For convenience, return the percentage of needed bits, instead of amount of
 * bits directly.
 *
 * @ENTROPY_LVL_ACEPTABLE - below that threshold, sample has low byte entropy
 *			    and can be compressible with high probability
 *
 * @ENTROPY_LVL_HIGH - data are not compressible with high probability
 *
 * Use of ilog2() decreases precision, we lower the LVL to 5 to compensate.
 */
#define ENTROPY_LVL_ACEPTABLE		(65)
#define ENTROPY_LVL_HIGH		(80)

/*
 * For increasead precision in shannon_entropy calculation,
 * let's do pow(n, M) to save more digits after comma:
 *
 * - maximum int bit length is 64
 * - ilog2(MAX_SAMPLE_SIZE)	-> 13
 * - 13 * 4 = 52 < 64		-> M = 4
 *
 * So use pow(n, 4).
 */
static inline u32 ilog2_w(u64 n)
{
	return ilog2(n * n * n * n);
}

static u32 shannon_entropy(struct heuristic_ws *ws)
{
	const u32 entropy_max = 8 * ilog2_w(2);
	u32 entropy_sum = 0;
	u32 p, p_base, sz_base;
	u32 i;

	sz_base = ilog2_w(ws->sample_size);
	for (i = 0; i < BUCKET_SIZE && ws->bucket[i].count > 0; i++) {
		p = ws->bucket[i].count;
		p_base = ilog2_w(p);
		entropy_sum += p * (sz_base - p_base);
	}

	entropy_sum /= ws->sample_size;
	return entropy_sum * 100 / entropy_max;
}

#define RADIX_BASE		4U
#define COUNTERS_SIZE		(1U << RADIX_BASE)

static u8 get4bits(u64 num, int shift) {
	u8 low4bits;

	num >>= shift;
	/* Reverse order */
	low4bits = (COUNTERS_SIZE - 1) - (num % COUNTERS_SIZE);
	return low4bits;
}

/*
 * Use 4 bits as radix base
 * Use 16 u32 counters for calculating new position in buf array
 *
 * @array     - array that will be sorted
 * @array_buf - buffer array to store sorting results
 *              must be equal in size to @array
 * @num       - array size
 */
static void radix_sort(struct bucket_item *array, struct bucket_item *array_buf,
		       int num)
{
	u64 max_num;
	u64 buf_num;
	u32 counters[COUNTERS_SIZE];
	u32 new_addr;
	u32 addr;
	int bitlen;
	int shift;
	int i;

	/*
	 * Try avoid useless loop iterations for small numbers stored in big
	 * counters.  Example: 48 33 4 ... in 64bit array
	 */
	max_num = array[0].count;
	for (i = 1; i < num; i++) {
		buf_num = array[i].count;
		if (buf_num > max_num)
			max_num = buf_num;
	}

	buf_num = ilog2(max_num);
	bitlen = ALIGN(buf_num, RADIX_BASE * 2);

	shift = 0;
	while (shift < bitlen) {
		memset(counters, 0, sizeof(counters));

		for (i = 0; i < num; i++) {
			buf_num = array[i].count;
			addr = get4bits(buf_num, shift);
			counters[addr]++;
		}

		for (i = 1; i < COUNTERS_SIZE; i++)
			counters[i] += counters[i - 1];

		for (i = num - 1; i >= 0; i--) {
			buf_num = array[i].count;
			addr = get4bits(buf_num, shift);
			counters[addr]--;
			new_addr = counters[addr];
			array_buf[new_addr] = array[i];
		}

		shift += RADIX_BASE;

		/*
		 * Normal radix expects to move data from a temporary array, to
		 * the main one.  But that requires some CPU time. Avoid that
		 * by doing another sort iteration to original array instead of
		 * memcpy()
		 */
		memset(counters, 0, sizeof(counters));

		for (i = 0; i < num; i ++) {
			buf_num = array_buf[i].count;
			addr = get4bits(buf_num, shift);
			counters[addr]++;
		}

		for (i = 1; i < COUNTERS_SIZE; i++)
			counters[i] += counters[i - 1];

		for (i = num - 1; i >= 0; i--) {
			buf_num = array_buf[i].count;
			addr = get4bits(buf_num, shift);
			counters[addr]--;
			new_addr = counters[addr];
			array[new_addr] = array_buf[i];
		}

		shift += RADIX_BASE;
	}
}

/*
 * Size of the core byte set - how many bytes cover 90% of the sample
 *
 * There are several types of structured binary data that use nearly all byte
 * values. The distribution can be uniform and counts in all buckets will be
 * nearly the same (eg. encrypted data). Unlikely to be compressible.
 *
 * Other possibility is normal (Gaussian) distribution, where the data could
 * be potentially compressible, but we have to take a few more steps to decide
 * how much.
 *
 * @BYTE_CORE_SET_LOW  - main part of byte values repeated frequently,
 *                       compression algo can easy fix that
 * @BYTE_CORE_SET_HIGH - data have uniform distribution and with high
 *                       probability is not compressible
 */
#define BYTE_CORE_SET_LOW		(64)
#define BYTE_CORE_SET_HIGH		(200)

static int byte_core_set_size(struct heuristic_ws *ws)
{
	u32 i;
	u32 coreset_sum = 0;
	const u32 core_set_threshold = ws->sample_size * 90 / 100;
	struct bucket_item *bucket = ws->bucket;

	/* Sort in reverse order */
	radix_sort(ws->bucket, ws->bucket_b, BUCKET_SIZE);

	for (i = 0; i < BYTE_CORE_SET_LOW; i++)
		coreset_sum += bucket[i].count;

	if (coreset_sum > core_set_threshold)
		return i;

	for (; i < BYTE_CORE_SET_HIGH && bucket[i].count > 0; i++) {
		coreset_sum += bucket[i].count;
		if (coreset_sum > core_set_threshold)
			break;
	}

	return i;
}

/*
 * Count byte values in buckets.
 * This heuristic can detect textual data (configs, xml, json, html, etc).
 * Because in most text-like data byte set is restricted to limited number of
 * possible characters, and that restriction in most cases makes data easy to
 * compress.
 *
 * @BYTE_SET_THRESHOLD - consider all data within this byte set size:
 *	less - compressible
 *	more - need additional analysis
 */
#define BYTE_SET_THRESHOLD		(64)

static u32 byte_set_size(const struct heuristic_ws *ws)
{
	u32 i;
	u32 byte_set_size = 0;

	for (i = 0; i < BYTE_SET_THRESHOLD; i++) {
		if (ws->bucket[i].count > 0)
			byte_set_size++;
	}

	/*
	 * Continue collecting count of byte values in buckets.  If the byte
	 * set size is bigger then the threshold, it's pointless to continue,
	 * the detection technique would fail for this type of data.
	 */
	for (; i < BUCKET_SIZE; i++) {
		if (ws->bucket[i].count > 0) {
			byte_set_size++;
			if (byte_set_size > BYTE_SET_THRESHOLD)
				return byte_set_size;
		}
	}

	return byte_set_size;
}

static bool sample_repeated_patterns(struct heuristic_ws *ws)
{
	const u32 half_of_sample = ws->sample_size / 2;
	const u8 *data = ws->sample;

	return memcmp(&data[0], &data[half_of_sample], half_of_sample) == 0;
}

static void heuristic_collect_sample(struct inode *inode, u64 start, u64 end,
				     struct heuristic_ws *ws)
{
	struct page *page;
	u64 index, index_end;
	u32 i, curr_sample_pos;
	u8 *in_data;

	/*
	 * Compression handles the input data by chunks of 128KiB
	 * (defined by BTRFS_MAX_UNCOMPRESSED)
	 *
	 * We do the same for the heuristic and loop over the whole range.
	 *
	 * MAX_SAMPLE_SIZE - calculated under assumption that heuristic will
	 * process no more than BTRFS_MAX_UNCOMPRESSED at a time.
	 */
	if (end - start > BTRFS_MAX_UNCOMPRESSED)
		end = start + BTRFS_MAX_UNCOMPRESSED;

	index = start >> PAGE_SHIFT;
	index_end = end >> PAGE_SHIFT;

	/* Don't miss unaligned end */
	if (!IS_ALIGNED(end, PAGE_SIZE))
		index_end++;

	curr_sample_pos = 0;
	while (index < index_end) {
		page = find_get_page(inode->i_mapping, index);
		in_data = kmap_local_page(page);
		/* Handle case where the start is not aligned to PAGE_SIZE */
		i = start % PAGE_SIZE;
		while (i < PAGE_SIZE - SAMPLING_READ_SIZE) {
			/* Don't sample any garbage from the last page */
			if (start > end - SAMPLING_READ_SIZE)
				break;
			memcpy(&ws->sample[curr_sample_pos], &in_data[i],
					SAMPLING_READ_SIZE);
			i += SAMPLING_INTERVAL;
			start += SAMPLING_INTERVAL;
			curr_sample_pos += SAMPLING_READ_SIZE;
		}
		kunmap_local(in_data);
		put_page(page);

		index++;
	}

	ws->sample_size = curr_sample_pos;
}

/*
 * Compression heuristic.
 *
 * For now is's a naive and optimistic 'return true', we'll extend the logic to
 * quickly (compared to direct compression) detect data characteristics
 * (compressible/uncompressible) to avoid wasting CPU time on uncompressible
 * data.
 *
 * The following types of analysis can be performed:
 * - detect mostly zero data
 * - detect data with low "byte set" size (text, etc)
 * - detect data with low/high "core byte" set
 *
 * Return non-zero if the compression should be done, 0 otherwise.
 */
int btrfs_compress_heuristic(struct inode *inode, u64 start, u64 end)
{
	struct list_head *ws_list = get_workspace(0, 0);
	struct heuristic_ws *ws;
	u32 i;
	u8 byte;
	int ret = 0;

	ws = list_entry(ws_list, struct heuristic_ws, list);

	heuristic_collect_sample(inode, start, end, ws);

	if (sample_repeated_patterns(ws)) {
		ret = 1;
		goto out;
	}

	memset(ws->bucket, 0, sizeof(*ws->bucket)*BUCKET_SIZE);

	for (i = 0; i < ws->sample_size; i++) {
		byte = ws->sample[i];
		ws->bucket[byte].count++;
	}

	i = byte_set_size(ws);
	if (i < BYTE_SET_THRESHOLD) {
		ret = 2;
		goto out;
	}

	i = byte_core_set_size(ws);
	if (i <= BYTE_CORE_SET_LOW) {
		ret = 3;
		goto out;
	}

	if (i >= BYTE_CORE_SET_HIGH) {
		ret = 0;
		goto out;
	}

	i = shannon_entropy(ws);
	if (i <= ENTROPY_LVL_ACEPTABLE) {
		ret = 4;
		goto out;
	}

	/*
	 * For the levels below ENTROPY_LVL_HIGH, additional analysis would be
	 * needed to give green light to compression.
	 *
	 * For now just assume that compression at that level is not worth the
	 * resources because:
	 *
	 * 1. it is possible to defrag the data later
	 *
	 * 2. the data would turn out to be hardly compressible, eg. 150 byte
	 * values, every bucket has counter at level ~54. The heuristic would
	 * be confused. This can happen when data have some internal repeated
	 * patterns like "abbacbbc...". This can be detected by analyzing
	 * pairs of bytes, which is too costly.
	 */
	if (i < ENTROPY_LVL_HIGH) {
		ret = 5;
		goto out;
	} else {
		ret = 0;
		goto out;
	}

out:
	put_workspace(0, ws_list);
	return ret;
}

/*
 * Convert the compression suffix (eg. after "zlib" starting with ":") to
 * level, unrecognized string will set the default level
 */
unsigned int btrfs_compress_str2level(unsigned int type, const char *str)
{
	unsigned int level = 0;
	int ret;

	if (!type)
		return 0;

	if (str[0] == ':') {
		ret = kstrtouint(str + 1, 10, &level);
		if (ret)
			level = 0;
	}

	level = btrfs_compress_set_level(type, level);

	return level;
}
