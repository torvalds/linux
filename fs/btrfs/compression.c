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
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/bit_spinlock.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "ordered-data.h"
#include "compression.h"
#include "extent_io.h"
#include "extent_map.h"

static int btrfs_decompress_bio(struct compressed_bio *cb);

static inline int compressed_bio_size(struct btrfs_fs_info *fs_info,
				      unsigned long disk_size)
{
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);

	return sizeof(struct compressed_bio) +
		(DIV_ROUND_UP(disk_size, fs_info->sectorsize)) * csum_size;
}

static int check_compressed_csum(struct btrfs_inode *inode,
				 struct compressed_bio *cb,
				 u64 disk_start)
{
	int ret;
	struct page *page;
	unsigned long i;
	char *kaddr;
	u32 csum;
	u32 *cb_sum = &cb->sums;

	if (inode->flags & BTRFS_INODE_NODATASUM)
		return 0;

	for (i = 0; i < cb->nr_pages; i++) {
		page = cb->compressed_pages[i];
		csum = ~(u32)0;

		kaddr = kmap_atomic(page);
		csum = btrfs_csum_data(kaddr, csum, PAGE_SIZE);
		btrfs_csum_final(csum, (u8 *)&csum);
		kunmap_atomic(kaddr);

		if (csum != *cb_sum) {
			btrfs_print_data_csum_error(inode, disk_start, csum,
					*cb_sum, cb->mirror_num);
			ret = -EIO;
			goto fail;
		}
		cb_sum++;

	}
	ret = 0;
fail:
	return ret;
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
static void end_compressed_bio_read(struct bio *bio)
{
	struct compressed_bio *cb = bio->bi_private;
	struct inode *inode;
	struct page *page;
	unsigned long index;
	int ret;

	if (bio->bi_status)
		cb->errors = 1;

	/* if there are more bios still pending for this compressed
	 * extent, just exit
	 */
	if (!refcount_dec_and_test(&cb->pending_bios))
		goto out;

	inode = cb->inode;
	ret = check_compressed_csum(BTRFS_I(inode), cb,
				    (u64)bio->bi_iter.bi_sector << 9);
	if (ret)
		goto csum_failed;

	/* ok, we're the last bio for this extent, lets start
	 * the decompression.
	 */
	ret = btrfs_decompress_bio(cb);

csum_failed:
	if (ret)
		cb->errors = 1;

	/* release the compressed pages */
	index = 0;
	for (index = 0; index < cb->nr_pages; index++) {
		page = cb->compressed_pages[index];
		page->mapping = NULL;
		put_page(page);
	}

	/* do io completion on the original bio */
	if (cb->errors) {
		bio_io_error(cb->orig_bio);
	} else {
		int i;
		struct bio_vec *bvec;

		/*
		 * we have verified the checksum already, set page
		 * checked so the end_io handlers know about it
		 */
		ASSERT(!bio_flagged(bio, BIO_CLONED));
		bio_for_each_segment_all(bvec, cb->orig_bio, i)
			SetPageChecked(bvec->bv_page);

		bio_endio(cb->orig_bio);
	}

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
static noinline void end_compressed_writeback(struct inode *inode,
					      const struct compressed_bio *cb)
{
	unsigned long index = cb->start >> PAGE_SHIFT;
	unsigned long end_index = (cb->start + cb->len - 1) >> PAGE_SHIFT;
	struct page *pages[16];
	unsigned long nr_pages = end_index - index + 1;
	int i;
	int ret;

	if (cb->errors)
		mapping_set_error(inode->i_mapping, -EIO);

	while (nr_pages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min_t(unsigned long,
				     nr_pages, ARRAY_SIZE(pages)), pages);
		if (ret == 0) {
			nr_pages -= 1;
			index += 1;
			continue;
		}
		for (i = 0; i < ret; i++) {
			if (cb->errors)
				SetPageError(pages[i]);
			end_page_writeback(pages[i]);
			put_page(pages[i]);
		}
		nr_pages -= ret;
		index += ret;
	}
	/* the inode may be gone now */
}

/*
 * do the cleanup once all the compressed pages hit the disk.
 * This will clear writeback on the file pages and free the compressed
 * pages.
 *
 * This also calls the writeback end hooks for the file pages so that
 * metadata and checksums can be updated in the file.
 */
static void end_compressed_bio_write(struct bio *bio)
{
	struct extent_io_tree *tree;
	struct compressed_bio *cb = bio->bi_private;
	struct inode *inode;
	struct page *page;
	unsigned long index;

	if (bio->bi_status)
		cb->errors = 1;

	/* if there are more bios still pending for this compressed
	 * extent, just exit
	 */
	if (!refcount_dec_and_test(&cb->pending_bios))
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
					 NULL,
					 bio->bi_status ? 0 : 1);
	cb->compressed_pages[0]->mapping = NULL;

	end_compressed_writeback(inode, cb);
	/* note, our inode could be gone now */

	/*
	 * release the compressed pages, these came from alloc_page and
	 * are not attached to the inode at all
	 */
	index = 0;
	for (index = 0; index < cb->nr_pages; index++) {
		page = cb->compressed_pages[index];
		page->mapping = NULL;
		put_page(page);
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
blk_status_t btrfs_submit_compressed_write(struct inode *inode, u64 start,
				 unsigned long len, u64 disk_start,
				 unsigned long compressed_len,
				 struct page **compressed_pages,
				 unsigned long nr_pages)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct bio *bio = NULL;
	struct compressed_bio *cb;
	unsigned long bytes_left;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	int pg_index = 0;
	struct page *page;
	u64 first_byte = disk_start;
	struct block_device *bdev;
	blk_status_t ret;
	int skip_sum = BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM;

	WARN_ON(start & ((u64)PAGE_SIZE - 1));
	cb = kmalloc(compressed_bio_size(fs_info, compressed_len), GFP_NOFS);
	if (!cb)
		return BLK_STS_RESOURCE;
	refcount_set(&cb->pending_bios, 0);
	cb->errors = 0;
	cb->inode = inode;
	cb->start = start;
	cb->len = len;
	cb->mirror_num = 0;
	cb->compressed_pages = compressed_pages;
	cb->compressed_len = compressed_len;
	cb->orig_bio = NULL;
	cb->nr_pages = nr_pages;

	bdev = fs_info->fs_devices->latest_bdev;

	bio = btrfs_bio_alloc(bdev, first_byte);
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
	bio->bi_private = cb;
	bio->bi_end_io = end_compressed_bio_write;
	refcount_set(&cb->pending_bios, 1);

	/* create and submit bios for the compressed pages */
	bytes_left = compressed_len;
	for (pg_index = 0; pg_index < cb->nr_pages; pg_index++) {
		int submit = 0;

		page = compressed_pages[pg_index];
		page->mapping = inode->i_mapping;
		if (bio->bi_iter.bi_size)
			submit = io_tree->ops->merge_bio_hook(page, 0,
							   PAGE_SIZE,
							   bio, 0);

		page->mapping = NULL;
		if (submit || bio_add_page(bio, page, PAGE_SIZE, 0) <
		    PAGE_SIZE) {
			bio_get(bio);

			/*
			 * inc the count before we submit the bio so
			 * we know the end IO handler won't happen before
			 * we inc the count.  Otherwise, the cb might get
			 * freed before we're done setting it up
			 */
			refcount_inc(&cb->pending_bios);
			ret = btrfs_bio_wq_end_io(fs_info, bio,
						  BTRFS_WQ_ENDIO_DATA);
			BUG_ON(ret); /* -ENOMEM */

			if (!skip_sum) {
				ret = btrfs_csum_one_bio(inode, bio, start, 1);
				BUG_ON(ret); /* -ENOMEM */
			}

			ret = btrfs_map_bio(fs_info, bio, 0, 1);
			if (ret) {
				bio->bi_status = ret;
				bio_endio(bio);
			}

			bio_put(bio);

			bio = btrfs_bio_alloc(bdev, first_byte);
			bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
			bio->bi_private = cb;
			bio->bi_end_io = end_compressed_bio_write;
			bio_add_page(bio, page, PAGE_SIZE, 0);
		}
		if (bytes_left < PAGE_SIZE) {
			btrfs_info(fs_info,
					"bytes left %lu compress len %lu nr %lu",
			       bytes_left, cb->compressed_len, cb->nr_pages);
		}
		bytes_left -= PAGE_SIZE;
		first_byte += PAGE_SIZE;
		cond_resched();
	}
	bio_get(bio);

	ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DATA);
	BUG_ON(ret); /* -ENOMEM */

	if (!skip_sum) {
		ret = btrfs_csum_one_bio(inode, bio, start, 1);
		BUG_ON(ret); /* -ENOMEM */
	}

	ret = btrfs_map_bio(fs_info, bio, 0, 1);
	if (ret) {
		bio->bi_status = ret;
		bio_endio(bio);
	}

	bio_put(bio);
	return 0;
}

static u64 bio_end_offset(struct bio *bio)
{
	struct bio_vec *last = &bio->bi_io_vec[bio->bi_vcnt - 1];

	return page_offset(last->bv_page) + last->bv_len + last->bv_offset;
}

static noinline int add_ra_bio_pages(struct inode *inode,
				     u64 compressed_end,
				     struct compressed_bio *cb)
{
	unsigned long end_index;
	unsigned long pg_index;
	u64 last_offset;
	u64 isize = i_size_read(inode);
	int ret;
	struct page *page;
	unsigned long nr_pages = 0;
	struct extent_map *em;
	struct address_space *mapping = inode->i_mapping;
	struct extent_map_tree *em_tree;
	struct extent_io_tree *tree;
	u64 end;
	int misses = 0;

	last_offset = bio_end_offset(cb->orig_bio);
	em_tree = &BTRFS_I(inode)->extent_tree;
	tree = &BTRFS_I(inode)->io_tree;

	if (isize == 0)
		return 0;

	end_index = (i_size_read(inode) - 1) >> PAGE_SHIFT;

	while (last_offset < compressed_end) {
		pg_index = last_offset >> PAGE_SHIFT;

		if (pg_index > end_index)
			break;

		rcu_read_lock();
		page = radix_tree_lookup(&mapping->page_tree, pg_index);
		rcu_read_unlock();
		if (page && !radix_tree_exceptional_entry(page)) {
			misses++;
			if (misses > 4)
				break;
			goto next;
		}

		page = __page_cache_alloc(mapping_gfp_constraint(mapping,
								 ~__GFP_FS));
		if (!page)
			break;

		if (add_to_page_cache_lru(page, mapping, pg_index, GFP_NOFS)) {
			put_page(page);
			goto next;
		}

		end = last_offset + PAGE_SIZE - 1;
		/*
		 * at this point, we have a locked page in the page cache
		 * for these bytes in the file.  But, we have to make
		 * sure they map to this compressed extent on disk.
		 */
		set_page_extent_mapped(page);
		lock_extent(tree, last_offset, end);
		read_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, last_offset,
					   PAGE_SIZE);
		read_unlock(&em_tree->lock);

		if (!em || last_offset < em->start ||
		    (last_offset + PAGE_SIZE > extent_map_end(em)) ||
		    (em->block_start >> 9) != cb->orig_bio->bi_iter.bi_sector) {
			free_extent_map(em);
			unlock_extent(tree, last_offset, end);
			unlock_page(page);
			put_page(page);
			break;
		}
		free_extent_map(em);

		if (page->index == end_index) {
			char *userpage;
			size_t zero_offset = isize & (PAGE_SIZE - 1);

			if (zero_offset) {
				int zeros;
				zeros = PAGE_SIZE - zero_offset;
				userpage = kmap_atomic(page);
				memset(userpage + zero_offset, 0, zeros);
				flush_dcache_page(page);
				kunmap_atomic(userpage);
			}
		}

		ret = bio_add_page(cb->orig_bio, page,
				   PAGE_SIZE, 0);

		if (ret == PAGE_SIZE) {
			nr_pages++;
			put_page(page);
		} else {
			unlock_extent(tree, last_offset, end);
			unlock_page(page);
			put_page(page);
			break;
		}
next:
		last_offset += PAGE_SIZE;
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
blk_status_t btrfs_submit_compressed_read(struct inode *inode, struct bio *bio,
				 int mirror_num, unsigned long bio_flags)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_io_tree *tree;
	struct extent_map_tree *em_tree;
	struct compressed_bio *cb;
	unsigned long compressed_len;
	unsigned long nr_pages;
	unsigned long pg_index;
	struct page *page;
	struct block_device *bdev;
	struct bio *comp_bio;
	u64 cur_disk_byte = (u64)bio->bi_iter.bi_sector << 9;
	u64 em_len;
	u64 em_start;
	struct extent_map *em;
	blk_status_t ret = BLK_STS_RESOURCE;
	int faili = 0;
	u32 *sums;

	tree = &BTRFS_I(inode)->io_tree;
	em_tree = &BTRFS_I(inode)->extent_tree;

	/* we need the actual starting offset of this extent in the file */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree,
				   page_offset(bio->bi_io_vec->bv_page),
				   PAGE_SIZE);
	read_unlock(&em_tree->lock);
	if (!em)
		return BLK_STS_IOERR;

	compressed_len = em->block_len;
	cb = kmalloc(compressed_bio_size(fs_info, compressed_len), GFP_NOFS);
	if (!cb)
		goto out;

	refcount_set(&cb->pending_bios, 0);
	cb->errors = 0;
	cb->inode = inode;
	cb->mirror_num = mirror_num;
	sums = &cb->sums;

	cb->start = em->orig_start;
	em_len = em->len;
	em_start = em->start;

	free_extent_map(em);
	em = NULL;

	cb->len = bio->bi_iter.bi_size;
	cb->compressed_len = compressed_len;
	cb->compress_type = extent_compress_type(bio_flags);
	cb->orig_bio = bio;

	nr_pages = DIV_ROUND_UP(compressed_len, PAGE_SIZE);
	cb->compressed_pages = kcalloc(nr_pages, sizeof(struct page *),
				       GFP_NOFS);
	if (!cb->compressed_pages)
		goto fail1;

	bdev = fs_info->fs_devices->latest_bdev;

	for (pg_index = 0; pg_index < nr_pages; pg_index++) {
		cb->compressed_pages[pg_index] = alloc_page(GFP_NOFS |
							      __GFP_HIGHMEM);
		if (!cb->compressed_pages[pg_index]) {
			faili = pg_index - 1;
			ret = BLK_STS_RESOURCE;
			goto fail2;
		}
	}
	faili = nr_pages - 1;
	cb->nr_pages = nr_pages;

	add_ra_bio_pages(inode, em_start + em_len, cb);

	/* include any pages we added in add_ra-bio_pages */
	cb->len = bio->bi_iter.bi_size;

	comp_bio = btrfs_bio_alloc(bdev, cur_disk_byte);
	bio_set_op_attrs (comp_bio, REQ_OP_READ, 0);
	comp_bio->bi_private = cb;
	comp_bio->bi_end_io = end_compressed_bio_read;
	refcount_set(&cb->pending_bios, 1);

	for (pg_index = 0; pg_index < nr_pages; pg_index++) {
		int submit = 0;

		page = cb->compressed_pages[pg_index];
		page->mapping = inode->i_mapping;
		page->index = em_start >> PAGE_SHIFT;

		if (comp_bio->bi_iter.bi_size)
			submit = tree->ops->merge_bio_hook(page, 0,
							PAGE_SIZE,
							comp_bio, 0);

		page->mapping = NULL;
		if (submit || bio_add_page(comp_bio, page, PAGE_SIZE, 0) <
		    PAGE_SIZE) {
			bio_get(comp_bio);

			ret = btrfs_bio_wq_end_io(fs_info, comp_bio,
						  BTRFS_WQ_ENDIO_DATA);
			BUG_ON(ret); /* -ENOMEM */

			/*
			 * inc the count before we submit the bio so
			 * we know the end IO handler won't happen before
			 * we inc the count.  Otherwise, the cb might get
			 * freed before we're done setting it up
			 */
			refcount_inc(&cb->pending_bios);

			if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)) {
				ret = btrfs_lookup_bio_sums(inode, comp_bio,
							    sums);
				BUG_ON(ret); /* -ENOMEM */
			}
			sums += DIV_ROUND_UP(comp_bio->bi_iter.bi_size,
					     fs_info->sectorsize);

			ret = btrfs_map_bio(fs_info, comp_bio, mirror_num, 0);
			if (ret) {
				comp_bio->bi_status = ret;
				bio_endio(comp_bio);
			}

			bio_put(comp_bio);

			comp_bio = btrfs_bio_alloc(bdev, cur_disk_byte);
			bio_set_op_attrs(comp_bio, REQ_OP_READ, 0);
			comp_bio->bi_private = cb;
			comp_bio->bi_end_io = end_compressed_bio_read;

			bio_add_page(comp_bio, page, PAGE_SIZE, 0);
		}
		cur_disk_byte += PAGE_SIZE;
	}
	bio_get(comp_bio);

	ret = btrfs_bio_wq_end_io(fs_info, comp_bio, BTRFS_WQ_ENDIO_DATA);
	BUG_ON(ret); /* -ENOMEM */

	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)) {
		ret = btrfs_lookup_bio_sums(inode, comp_bio, sums);
		BUG_ON(ret); /* -ENOMEM */
	}

	ret = btrfs_map_bio(fs_info, comp_bio, mirror_num, 0);
	if (ret) {
		comp_bio->bi_status = ret;
		bio_endio(comp_bio);
	}

	bio_put(comp_bio);
	return 0;

fail2:
	while (faili >= 0) {
		__free_page(cb->compressed_pages[faili]);
		faili--;
	}

	kfree(cb->compressed_pages);
fail1:
	kfree(cb);
out:
	free_extent_map(em);
	return ret;
}

static struct {
	struct list_head idle_ws;
	spinlock_t ws_lock;
	/* Number of free workspaces */
	int free_ws;
	/* Total number of allocated workspaces */
	atomic_t total_ws;
	/* Waiters for a free workspace */
	wait_queue_head_t ws_wait;
} btrfs_comp_ws[BTRFS_COMPRESS_TYPES];

static const struct btrfs_compress_op * const btrfs_compress_op[] = {
	&btrfs_zlib_compress,
	&btrfs_lzo_compress,
};

void __init btrfs_init_compress(void)
{
	int i;

	for (i = 0; i < BTRFS_COMPRESS_TYPES; i++) {
		struct list_head *workspace;

		INIT_LIST_HEAD(&btrfs_comp_ws[i].idle_ws);
		spin_lock_init(&btrfs_comp_ws[i].ws_lock);
		atomic_set(&btrfs_comp_ws[i].total_ws, 0);
		init_waitqueue_head(&btrfs_comp_ws[i].ws_wait);

		/*
		 * Preallocate one workspace for each compression type so
		 * we can guarantee forward progress in the worst case
		 */
		workspace = btrfs_compress_op[i]->alloc_workspace();
		if (IS_ERR(workspace)) {
			pr_warn("BTRFS: cannot preallocate compression workspace, will try later\n");
		} else {
			atomic_set(&btrfs_comp_ws[i].total_ws, 1);
			btrfs_comp_ws[i].free_ws = 1;
			list_add(workspace, &btrfs_comp_ws[i].idle_ws);
		}
	}
}

/*
 * This finds an available workspace or allocates a new one.
 * If it's not possible to allocate a new one, waits until there's one.
 * Preallocation makes a forward progress guarantees and we do not return
 * errors.
 */
static struct list_head *find_workspace(int type)
{
	struct list_head *workspace;
	int cpus = num_online_cpus();
	int idx = type - 1;
	unsigned nofs_flag;

	struct list_head *idle_ws	= &btrfs_comp_ws[idx].idle_ws;
	spinlock_t *ws_lock		= &btrfs_comp_ws[idx].ws_lock;
	atomic_t *total_ws		= &btrfs_comp_ws[idx].total_ws;
	wait_queue_head_t *ws_wait	= &btrfs_comp_ws[idx].ws_wait;
	int *free_ws			= &btrfs_comp_ws[idx].free_ws;
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
	workspace = btrfs_compress_op[idx]->alloc_workspace();
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

/*
 * put a workspace struct back on the list or free it if we have enough
 * idle ones sitting around
 */
static void free_workspace(int type, struct list_head *workspace)
{
	int idx = type - 1;
	struct list_head *idle_ws	= &btrfs_comp_ws[idx].idle_ws;
	spinlock_t *ws_lock		= &btrfs_comp_ws[idx].ws_lock;
	atomic_t *total_ws		= &btrfs_comp_ws[idx].total_ws;
	wait_queue_head_t *ws_wait	= &btrfs_comp_ws[idx].ws_wait;
	int *free_ws			= &btrfs_comp_ws[idx].free_ws;

	spin_lock(ws_lock);
	if (*free_ws <= num_online_cpus()) {
		list_add(workspace, idle_ws);
		(*free_ws)++;
		spin_unlock(ws_lock);
		goto wake;
	}
	spin_unlock(ws_lock);

	btrfs_compress_op[idx]->free_workspace(workspace);
	atomic_dec(total_ws);
wake:
	/*
	 * Make sure counter is updated before we wake up waiters.
	 */
	smp_mb();
	if (waitqueue_active(ws_wait))
		wake_up(ws_wait);
}

/*
 * cleanup function for module exit
 */
static void free_workspaces(void)
{
	struct list_head *workspace;
	int i;

	for (i = 0; i < BTRFS_COMPRESS_TYPES; i++) {
		while (!list_empty(&btrfs_comp_ws[i].idle_ws)) {
			workspace = btrfs_comp_ws[i].idle_ws.next;
			list_del(workspace);
			btrfs_compress_op[i]->free_workspace(workspace);
			atomic_dec(&btrfs_comp_ws[i].total_ws);
		}
	}
}

/*
 * Given an address space and start and length, compress the bytes into @pages
 * that are allocated on demand.
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
 *
 * @max_out tells us the max number of bytes that we're allowed to
 * stuff into pages
 */
int btrfs_compress_pages(int type, struct address_space *mapping,
			 u64 start, struct page **pages,
			 unsigned long *out_pages,
			 unsigned long *total_in,
			 unsigned long *total_out)
{
	struct list_head *workspace;
	int ret;

	workspace = find_workspace(type);

	ret = btrfs_compress_op[type-1]->compress_pages(workspace, mapping,
						      start, pages,
						      out_pages,
						      total_in, total_out);
	free_workspace(type, workspace);
	return ret;
}

/*
 * pages_in is an array of pages with compressed data.
 *
 * disk_start is the starting logical offset of this array in the file
 *
 * orig_bio contains the pages from the file that we want to decompress into
 *
 * srclen is the number of bytes in pages_in
 *
 * The basic idea is that we have a bio that was created by readpages.
 * The pages in the bio are for the uncompressed data, and they may not
 * be contiguous.  They all correspond to the range of bytes covered by
 * the compressed extent.
 */
static int btrfs_decompress_bio(struct compressed_bio *cb)
{
	struct list_head *workspace;
	int ret;
	int type = cb->compress_type;

	workspace = find_workspace(type);
	ret = btrfs_compress_op[type - 1]->decompress_bio(workspace, cb);
	free_workspace(type, workspace);

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

	workspace = find_workspace(type);

	ret = btrfs_compress_op[type-1]->decompress(workspace, data_in,
						  dest_page, start_byte,
						  srclen, destlen);

	free_workspace(type, workspace);
	return ret;
}

void btrfs_exit_compress(void)
{
	free_workspaces();
}

/*
 * Copy uncompressed data from working buffer to pages.
 *
 * buf_start is the byte offset we're of the start of our workspace buffer.
 *
 * total_out is the last byte of the buffer
 */
int btrfs_decompress_buf2page(const char *buf, unsigned long buf_start,
			      unsigned long total_out, u64 disk_start,
			      struct bio *bio)
{
	unsigned long buf_offset;
	unsigned long current_buf_start;
	unsigned long start_byte;
	unsigned long prev_start_byte;
	unsigned long working_bytes = total_out - buf_start;
	unsigned long bytes;
	char *kaddr;
	struct bio_vec bvec = bio_iter_iovec(bio, bio->bi_iter);

	/*
	 * start byte is the first byte of the page we're currently
	 * copying into relative to the start of the compressed data.
	 */
	start_byte = page_offset(bvec.bv_page) - disk_start;

	/* we haven't yet hit data corresponding to this page */
	if (total_out <= start_byte)
		return 1;

	/*
	 * the start of the data we care about is offset into
	 * the middle of our working buffer
	 */
	if (total_out > start_byte && buf_start < start_byte) {
		buf_offset = start_byte - buf_start;
		working_bytes -= buf_offset;
	} else {
		buf_offset = 0;
	}
	current_buf_start = buf_start;

	/* copy bytes from the working buffer into the pages */
	while (working_bytes > 0) {
		bytes = min_t(unsigned long, bvec.bv_len,
				PAGE_SIZE - buf_offset);
		bytes = min(bytes, working_bytes);

		kaddr = kmap_atomic(bvec.bv_page);
		memcpy(kaddr + bvec.bv_offset, buf + buf_offset, bytes);
		kunmap_atomic(kaddr);
		flush_dcache_page(bvec.bv_page);

		buf_offset += bytes;
		working_bytes -= bytes;
		current_buf_start += bytes;

		/* check if we need to pick another page */
		bio_advance(bio, bytes);
		if (!bio->bi_iter.bi_size)
			return 0;
		bvec = bio_iter_iovec(bio, bio->bi_iter);
		prev_start_byte = start_byte;
		start_byte = page_offset(bvec.bv_page) - disk_start;

		/*
		 * We need to make sure we're only adjusting
		 * our offset into compression working buffer when
		 * we're switching pages.  Otherwise we can incorrectly
		 * keep copying when we were actually done.
		 */
		if (start_byte != prev_start_byte) {
			/*
			 * make sure our new page is covered by this
			 * working buffer
			 */
			if (total_out <= start_byte)
				return 1;

			/*
			 * the next page in the biovec might not be adjacent
			 * to the last page, but it might still be found
			 * inside this working buffer. bump our offset pointer
			 */
			if (total_out > start_byte &&
			    current_buf_start < start_byte) {
				buf_offset = start_byte - buf_start;
				working_bytes = total_out - start_byte;
				current_buf_start = buf_start + buf_offset;
			}
		}
	}

	return 1;
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
	u64 index = start >> PAGE_SHIFT;
	u64 end_index = end >> PAGE_SHIFT;
	struct page *page;
	int ret = 1;

	while (index <= end_index) {
		page = find_get_page(inode->i_mapping, index);
		kmap(page);
		kunmap(page);
		put_page(page);
		index++;
	}

	return ret;
}
