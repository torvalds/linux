/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/bit_spinlock.h>
#include <linux/version.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "ordered-data.h"
#include "compat.h"
#include "compression.h"
#include "extent_io.h"
#include "extent_map.h"

struct compressed_bio {
	/* number of bios pending for this compressed extent */
	atomic_t pending_bios;

	/* the pages with the compressed data on them */
	struct page **compressed_pages;

	/* inode that owns this data */
	struct inode *inode;

	/* starting offset in the inode for our pages */
	u64 start;

	/* number of bytes in the inode we're working on */
	unsigned long len;

	/* number of bytes on disk */
	unsigned long compressed_len;

	/* number of compressed pages in the array */
	unsigned long nr_pages;

	/* IO errors */
	int errors;

	/* for reads, this is the bio we are copying the data into */
	struct bio *orig_bio;
};

static struct bio *compressed_bio_alloc(struct block_device *bdev,
					u64 first_byte, gfp_t gfp_flags)
{
	struct bio *bio;
	int nr_vecs;

	nr_vecs = bio_get_nr_vecs(bdev);
	bio = bio_alloc(gfp_flags, nr_vecs);

	if (bio == NULL && (current->flags & PF_MEMALLOC)) {
		while (!bio && (nr_vecs /= 2))
			bio = bio_alloc(gfp_flags, nr_vecs);
	}

	if (bio) {
		bio->bi_size = 0;
		bio->bi_bdev = bdev;
		bio->bi_sector = first_byte >> 9;
	}
	return bio;
}

/* when we finish reading compressed pages from the disk, we
 * decompress them and then run the bio end_io routines on the
 * decompressed pages (in the inode address space).
 *
 * This allows the checksumming and other IO error handling routines
 * to work normally
 *
 * The compressed pages are freed here, and it must be run
 * in process context
 */
static void end_compressed_bio_read(struct bio *bio, int err)
{
	struct extent_io_tree *tree;
	struct compressed_bio *cb = bio->bi_private;
	struct inode *inode;
	struct page *page;
	unsigned long index;
	int ret;

	if (err)
		cb->errors = 1;

	/* if there are more bios still pending for this compressed
	 * extent, just exit
	 */
	if (!atomic_dec_and_test(&cb->pending_bios))
		goto out;

	/* ok, we're the last bio for this extent, lets start
	 * the decompression.
	 */
	inode = cb->inode;
	tree = &BTRFS_I(inode)->io_tree;
	ret = btrfs_zlib_decompress_biovec(cb->compressed_pages,
					cb->start,
					cb->orig_bio->bi_io_vec,
					cb->orig_bio->bi_vcnt,
					cb->compressed_len);
	if (ret)
		cb->errors = 1;

	/* release the compressed pages */
	index = 0;
	for (index = 0; index < cb->nr_pages; index++) {
		page = cb->compressed_pages[index];
		page->mapping = NULL;
		page_cache_release(page);
	}

	/* do io completion on the original bio */
	if (cb->errors)
		bio_io_error(cb->orig_bio);
	else
		bio_endio(cb->orig_bio, 0);

	/* finally free the cb struct */
	kfree(cb->compressed_pages);
	kfree(cb);
out:
	bio_put(bio);
}

/*
 * Clear the writeback bits on all of the file
 * pages for a compressed write
 */
static noinline int end_compressed_writeback(struct inode *inode, u64 start,
					     unsigned long ram_size)
{
	unsigned long index = start >> PAGE_CACHE_SHIFT;
	unsigned long end_index = (start + ram_size - 1) >> PAGE_CACHE_SHIFT;
	struct page *pages[16];
	unsigned long nr_pages = end_index - index + 1;
	int i;
	int ret;

	while(nr_pages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min(nr_pages, ARRAY_SIZE(pages)), pages);
		if (ret == 0) {
			nr_pages -= 1;
			index += 1;
			continue;
		}
		for (i = 0; i < ret; i++) {
			end_page_writeback(pages[i]);
			page_cache_release(pages[i]);
		}
		nr_pages -= ret;
		index += ret;
	}
	/* the inode may be gone now */
	return 0;
}

/*
 * do the cleanup once all the compressed pages hit the disk.
 * This will clear writeback on the file pages and free the compressed
 * pages.
 *
 * This also calls the writeback end hooks for the file pages so that
 * metadata and checksums can be updated in the file.
 */
static void end_compressed_bio_write(struct bio *bio, int err)
{
	struct extent_io_tree *tree;
	struct compressed_bio *cb = bio->bi_private;
	struct inode *inode;
	struct page *page;
	unsigned long index;

	if (err)
		cb->errors = 1;

	/* if there are more bios still pending for this compressed
	 * extent, just exit
	 */
	if (!atomic_dec_and_test(&cb->pending_bios))
		goto out;

	/* ok, we're the last bio for this extent, step one is to
	 * call back into the FS and do all the end_io operations
	 */
	inode = cb->inode;
	tree = &BTRFS_I(inode)->io_tree;
	cb->compressed_pages[0]->mapping = cb->inode->i_mapping;
	tree->ops->writepage_end_io_hook(cb->compressed_pages[0],
					 cb->start,
					 cb->start + cb->len - 1,
					 NULL, 1);
	cb->compressed_pages[0]->mapping = NULL;

	end_compressed_writeback(inode, cb->start, cb->len);
	/* note, our inode could be gone now */

	/*
	 * release the compressed pages, these came from alloc_page and
	 * are not attached to the inode at all
	 */
	index = 0;
	for (index = 0; index < cb->nr_pages; index++) {
		page = cb->compressed_pages[index];
		page->mapping = NULL;
		page_cache_release(page);
	}

	/* finally free the cb struct */
	kfree(cb->compressed_pages);
	kfree(cb);
out:
	bio_put(bio);
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
int btrfs_submit_compressed_write(struct inode *inode, u64 start,
				 unsigned long len, u64 disk_start,
				 unsigned long compressed_len,
				 struct page **compressed_pages,
				 unsigned long nr_pages)
{
	struct bio *bio = NULL;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct compressed_bio *cb;
	unsigned long bytes_left;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	int page_index = 0;
	struct page *page;
	u64 first_byte = disk_start;
	struct block_device *bdev;
	int ret;

	WARN_ON(start & ((u64)PAGE_CACHE_SIZE - 1));
	cb = kmalloc(sizeof(*cb), GFP_NOFS);
	atomic_set(&cb->pending_bios, 0);
	cb->errors = 0;
	cb->inode = inode;
	cb->start = start;
	cb->len = len;
	cb->compressed_pages = compressed_pages;
	cb->compressed_len = compressed_len;
	cb->orig_bio = NULL;
	cb->nr_pages = nr_pages;

	bdev = BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;

	ret = btrfs_csum_file_bytes(root, inode, start, len);
	BUG_ON(ret);

	bio = compressed_bio_alloc(bdev, first_byte, GFP_NOFS);
	bio->bi_private = cb;
	bio->bi_end_io = end_compressed_bio_write;
	atomic_inc(&cb->pending_bios);

	/* create and submit bios for the compressed pages */
	bytes_left = compressed_len;
	for (page_index = 0; page_index < cb->nr_pages; page_index++) {
		page = compressed_pages[page_index];
		page->mapping = inode->i_mapping;
		if (bio->bi_size)
			ret = io_tree->ops->merge_bio_hook(page, 0,
							   PAGE_CACHE_SIZE,
							   bio, 0);
		else
			ret = 0;

		page->mapping = NULL;
		if (ret || bio_add_page(bio, page, PAGE_CACHE_SIZE, 0) <
		    PAGE_CACHE_SIZE) {
			bio_get(bio);

			ret = btrfs_bio_wq_end_io(root->fs_info, bio, 0);
			BUG_ON(ret);

			ret = btrfs_map_bio(root, WRITE, bio, 0, 1);
			BUG_ON(ret);

			bio_put(bio);

			bio = compressed_bio_alloc(bdev, first_byte, GFP_NOFS);
			atomic_inc(&cb->pending_bios);
			bio->bi_private = cb;
			bio->bi_end_io = end_compressed_bio_write;
			bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
		}
		if (bytes_left < PAGE_CACHE_SIZE) {
			printk("bytes left %lu compress len %lu nr %lu\n",
			       bytes_left, cb->compressed_len, cb->nr_pages);
		}
		bytes_left -= PAGE_CACHE_SIZE;
		first_byte += PAGE_CACHE_SIZE;
	}
	bio_get(bio);

	ret = btrfs_bio_wq_end_io(root->fs_info, bio, 0);
	BUG_ON(ret);

	ret = btrfs_map_bio(root, WRITE, bio, 0, 1);
	BUG_ON(ret);

	bio_put(bio);
	return 0;
}

/*
 * for a compressed read, the bio we get passed has all the inode pages
 * in it.  We don't actually do IO on those pages but allocate new ones
 * to hold the compressed pages on disk.
 *
 * bio->bi_sector points to the compressed extent on disk
 * bio->bi_io_vec points to all of the inode pages
 * bio->bi_vcnt is a count of pages
 *
 * After the compressed pages are read, we copy the bytes into the
 * bio we were passed and then call the bio end_io calls
 */
int btrfs_submit_compressed_read(struct inode *inode, struct bio *bio,
				 int mirror_num, unsigned long bio_flags)
{
	struct extent_io_tree *tree;
	struct extent_map_tree *em_tree;
	struct compressed_bio *cb;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	unsigned long uncompressed_len = bio->bi_vcnt * PAGE_CACHE_SIZE;
	unsigned long compressed_len;
	unsigned long nr_pages;
	unsigned long page_index;
	struct page *page;
	struct block_device *bdev;
	struct bio *comp_bio;
	u64 cur_disk_byte = (u64)bio->bi_sector << 9;
	struct extent_map *em;
	int ret;

	tree = &BTRFS_I(inode)->io_tree;
	em_tree = &BTRFS_I(inode)->extent_tree;

	/* we need the actual starting offset of this extent in the file */
	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree,
				   page_offset(bio->bi_io_vec->bv_page),
				   PAGE_CACHE_SIZE);
	spin_unlock(&em_tree->lock);

	cb = kmalloc(sizeof(*cb), GFP_NOFS);
	atomic_set(&cb->pending_bios, 0);
	cb->errors = 0;
	cb->inode = inode;

	cb->start = em->start;
	compressed_len = em->block_len;
	free_extent_map(em);

	cb->len = uncompressed_len;
	cb->compressed_len = compressed_len;
	cb->orig_bio = bio;

	nr_pages = (compressed_len + PAGE_CACHE_SIZE - 1) /
				 PAGE_CACHE_SIZE;
	cb->compressed_pages = kmalloc(sizeof(struct page *) * nr_pages,
				       GFP_NOFS);
	bdev = BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;

	for (page_index = 0; page_index < nr_pages; page_index++) {
		cb->compressed_pages[page_index] = alloc_page(GFP_NOFS |
							      __GFP_HIGHMEM);
	}
	cb->nr_pages = nr_pages;

	comp_bio = compressed_bio_alloc(bdev, cur_disk_byte, GFP_NOFS);
	comp_bio->bi_private = cb;
	comp_bio->bi_end_io = end_compressed_bio_read;
	atomic_inc(&cb->pending_bios);

	for (page_index = 0; page_index < nr_pages; page_index++) {
		page = cb->compressed_pages[page_index];
		page->mapping = inode->i_mapping;
		if (comp_bio->bi_size)
			ret = tree->ops->merge_bio_hook(page, 0,
							PAGE_CACHE_SIZE,
							comp_bio, 0);
		else
			ret = 0;

		page->mapping = NULL;
		if (ret || bio_add_page(comp_bio, page, PAGE_CACHE_SIZE, 0) <
		    PAGE_CACHE_SIZE) {
			bio_get(comp_bio);

			ret = btrfs_bio_wq_end_io(root->fs_info, comp_bio, 0);
			BUG_ON(ret);

			ret = btrfs_map_bio(root, READ, comp_bio, 0, 0);
			BUG_ON(ret);

			bio_put(comp_bio);

			comp_bio = compressed_bio_alloc(bdev, cur_disk_byte,
							GFP_NOFS);
			atomic_inc(&cb->pending_bios);
			bio->bi_private = cb;
			bio->bi_end_io = end_compressed_bio_write;
			bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
		}
		cur_disk_byte += PAGE_CACHE_SIZE;
	}
	bio_get(comp_bio);

	ret = btrfs_bio_wq_end_io(root->fs_info, comp_bio, 0);
	BUG_ON(ret);

	ret = btrfs_map_bio(root, READ, comp_bio, 0, 0);
	BUG_ON(ret);

	bio_put(comp_bio);
	return 0;
}
