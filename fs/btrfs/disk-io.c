/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/radix-tree.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "compat.h"
#include "crc32c.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "print-tree.h"
#include "async-thread.h"
#include "locking.h"
#include "ref-cache.h"
#include "tree-log.h"

static struct extent_io_ops btree_extent_io_ops;
static void end_workqueue_fn(struct btrfs_work *work);

/*
 * end_io_wq structs are used to do processing in task context when an IO is
 * complete.  This is used during reads to verify checksums, and it is used
 * by writes to insert metadata for new file extents after IO is complete.
 */
struct end_io_wq {
	struct bio *bio;
	bio_end_io_t *end_io;
	void *private;
	struct btrfs_fs_info *info;
	int error;
	int metadata;
	struct list_head list;
	struct btrfs_work work;
};

/*
 * async submit bios are used to offload expensive checksumming
 * onto the worker threads.  They checksum file and metadata bios
 * just before they are sent down the IO stack.
 */
struct async_submit_bio {
	struct inode *inode;
	struct bio *bio;
	struct list_head list;
	extent_submit_bio_hook_t *submit_bio_start;
	extent_submit_bio_hook_t *submit_bio_done;
	int rw;
	int mirror_num;
	unsigned long bio_flags;
	struct btrfs_work work;
};

/*
 * extents on the btree inode are pretty simple, there's one extent
 * that covers the entire device
 */
static struct extent_map *btree_get_extent(struct inode *inode,
		struct page *page, size_t page_offset, u64 start, u64 len,
		int create)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	int ret;

	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (em) {
		em->bdev =
			BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;
		spin_unlock(&em_tree->lock);
		goto out;
	}
	spin_unlock(&em_tree->lock);

	em = alloc_extent_map(GFP_NOFS);
	if (!em) {
		em = ERR_PTR(-ENOMEM);
		goto out;
	}
	em->start = 0;
	em->len = (u64)-1;
	em->block_len = (u64)-1;
	em->block_start = 0;
	em->bdev = BTRFS_I(inode)->root->fs_info->fs_devices->latest_bdev;

	spin_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	if (ret == -EEXIST) {
		u64 failed_start = em->start;
		u64 failed_len = em->len;

		free_extent_map(em);
		em = lookup_extent_mapping(em_tree, start, len);
		if (em) {
			ret = 0;
		} else {
			em = lookup_extent_mapping(em_tree, failed_start,
						   failed_len);
			ret = -EIO;
		}
	} else if (ret) {
		free_extent_map(em);
		em = NULL;
	}
	spin_unlock(&em_tree->lock);

	if (ret)
		em = ERR_PTR(ret);
out:
	return em;
}

u32 btrfs_csum_data(struct btrfs_root *root, char *data, u32 seed, size_t len)
{
	return btrfs_crc32c(seed, data, len);
}

void btrfs_csum_final(u32 crc, char *result)
{
	*(__le32 *)result = ~cpu_to_le32(crc);
}

/*
 * compute the csum for a btree block, and either verify it or write it
 * into the csum field of the block.
 */
static int csum_tree_block(struct btrfs_root *root, struct extent_buffer *buf,
			   int verify)
{
	u16 csum_size =
		btrfs_super_csum_size(&root->fs_info->super_copy);
	char *result = NULL;
	unsigned long len;
	unsigned long cur_len;
	unsigned long offset = BTRFS_CSUM_SIZE;
	char *map_token = NULL;
	char *kaddr;
	unsigned long map_start;
	unsigned long map_len;
	int err;
	u32 crc = ~(u32)0;
	unsigned long inline_result;

	len = buf->len - offset;
	while (len > 0) {
		err = map_private_extent_buffer(buf, offset, 32,
					&map_token, &kaddr,
					&map_start, &map_len, KM_USER0);
		if (err)
			return 1;
		cur_len = min(len, map_len - (offset - map_start));
		crc = btrfs_csum_data(root, kaddr + offset - map_start,
				      crc, cur_len);
		len -= cur_len;
		offset += cur_len;
		unmap_extent_buffer(buf, map_token, KM_USER0);
	}
	if (csum_size > sizeof(inline_result)) {
		result = kzalloc(csum_size * sizeof(char), GFP_NOFS);
		if (!result)
			return 1;
	} else {
		result = (char *)&inline_result;
	}

	btrfs_csum_final(crc, result);

	if (verify) {
		if (memcmp_extent_buffer(buf, result, 0, csum_size)) {
			u32 val;
			u32 found = 0;
			memcpy(&found, result, csum_size);

			read_extent_buffer(buf, &val, 0, csum_size);
			printk(KERN_INFO "btrfs: %s checksum verify failed "
			       "on %llu wanted %X found %X level %d\n",
			       root->fs_info->sb->s_id,
			       buf->start, val, found, btrfs_header_level(buf));
			if (result != (char *)&inline_result)
				kfree(result);
			return 1;
		}
	} else {
		write_extent_buffer(buf, result, 0, csum_size);
	}
	if (result != (char *)&inline_result)
		kfree(result);
	return 0;
}

/*
 * we can't consider a given block up to date unless the transid of the
 * block matches the transid in the parent node's pointer.  This is how we
 * detect blocks that either didn't get written at all or got written
 * in the wrong place.
 */
static int verify_parent_transid(struct extent_io_tree *io_tree,
				 struct extent_buffer *eb, u64 parent_transid)
{
	int ret;

	if (!parent_transid || btrfs_header_generation(eb) == parent_transid)
		return 0;

	lock_extent(io_tree, eb->start, eb->start + eb->len - 1, GFP_NOFS);
	if (extent_buffer_uptodate(io_tree, eb) &&
	    btrfs_header_generation(eb) == parent_transid) {
		ret = 0;
		goto out;
	}
	printk("parent transid verify failed on %llu wanted %llu found %llu\n",
	       (unsigned long long)eb->start,
	       (unsigned long long)parent_transid,
	       (unsigned long long)btrfs_header_generation(eb));
	ret = 1;
	clear_extent_buffer_uptodate(io_tree, eb);
out:
	unlock_extent(io_tree, eb->start, eb->start + eb->len - 1,
		      GFP_NOFS);
	return ret;
}

/*
 * helper to read a given tree block, doing retries as required when
 * the checksums don't match and we have alternate mirrors to try.
 */
static int btree_read_extent_buffer_pages(struct btrfs_root *root,
					  struct extent_buffer *eb,
					  u64 start, u64 parent_transid)
{
	struct extent_io_tree *io_tree;
	int ret;
	int num_copies = 0;
	int mirror_num = 0;

	io_tree = &BTRFS_I(root->fs_info->btree_inode)->io_tree;
	while (1) {
		ret = read_extent_buffer_pages(io_tree, eb, start, 1,
					       btree_get_extent, mirror_num);
		if (!ret &&
		    !verify_parent_transid(io_tree, eb, parent_transid))
			return ret;

		num_copies = btrfs_num_copies(&root->fs_info->mapping_tree,
					      eb->start, eb->len);
		if (num_copies == 1)
			return ret;

		mirror_num++;
		if (mirror_num > num_copies)
			return ret;
	}
	return -EIO;
}

/*
 * checksum a dirty tree block before IO.  This has extra checks to make sure
 * we only fill in the checksum field in the first page of a multi-page block
 */

static int csum_dirty_buffer(struct btrfs_root *root, struct page *page)
{
	struct extent_io_tree *tree;
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 found_start;
	int found_level;
	unsigned long len;
	struct extent_buffer *eb;
	int ret;

	tree = &BTRFS_I(page->mapping->host)->io_tree;

	if (page->private == EXTENT_PAGE_PRIVATE)
		goto out;
	if (!page->private)
		goto out;
	len = page->private >> 2;
	WARN_ON(len == 0);

	eb = alloc_extent_buffer(tree, start, len, page, GFP_NOFS);
	ret = btree_read_extent_buffer_pages(root, eb, start + PAGE_CACHE_SIZE,
					     btrfs_header_generation(eb));
	BUG_ON(ret);
	found_start = btrfs_header_bytenr(eb);
	if (found_start != start) {
		WARN_ON(1);
		goto err;
	}
	if (eb->first_page != page) {
		WARN_ON(1);
		goto err;
	}
	if (!PageUptodate(page)) {
		WARN_ON(1);
		goto err;
	}
	found_level = btrfs_header_level(eb);

	csum_tree_block(root, eb, 0);
err:
	free_extent_buffer(eb);
out:
	return 0;
}

static int check_tree_block_fsid(struct btrfs_root *root,
				 struct extent_buffer *eb)
{
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
	u8 fsid[BTRFS_UUID_SIZE];
	int ret = 1;

	read_extent_buffer(eb, fsid, (unsigned long)btrfs_header_fsid(eb),
			   BTRFS_FSID_SIZE);
	while (fs_devices) {
		if (!memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE)) {
			ret = 0;
			break;
		}
		fs_devices = fs_devices->seed;
	}
	return ret;
}

static int btree_readpage_end_io_hook(struct page *page, u64 start, u64 end,
			       struct extent_state *state)
{
	struct extent_io_tree *tree;
	u64 found_start;
	int found_level;
	unsigned long len;
	struct extent_buffer *eb;
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	int ret = 0;

	tree = &BTRFS_I(page->mapping->host)->io_tree;
	if (page->private == EXTENT_PAGE_PRIVATE)
		goto out;
	if (!page->private)
		goto out;

	len = page->private >> 2;
	WARN_ON(len == 0);

	eb = alloc_extent_buffer(tree, start, len, page, GFP_NOFS);

	found_start = btrfs_header_bytenr(eb);
	if (found_start != start) {
		printk(KERN_INFO "btrfs bad tree block start %llu %llu\n",
		       (unsigned long long)found_start,
		       (unsigned long long)eb->start);
		ret = -EIO;
		goto err;
	}
	if (eb->first_page != page) {
		printk(KERN_INFO "btrfs bad first page %lu %lu\n",
		       eb->first_page->index, page->index);
		WARN_ON(1);
		ret = -EIO;
		goto err;
	}
	if (check_tree_block_fsid(root, eb)) {
		printk(KERN_INFO "btrfs bad fsid on block %llu\n",
		       (unsigned long long)eb->start);
		ret = -EIO;
		goto err;
	}
	found_level = btrfs_header_level(eb);

	ret = csum_tree_block(root, eb, 1);
	if (ret)
		ret = -EIO;

	end = min_t(u64, eb->len, PAGE_CACHE_SIZE);
	end = eb->start + end - 1;
err:
	free_extent_buffer(eb);
out:
	return ret;
}

static void end_workqueue_bio(struct bio *bio, int err)
{
	struct end_io_wq *end_io_wq = bio->bi_private;
	struct btrfs_fs_info *fs_info;

	fs_info = end_io_wq->info;
	end_io_wq->error = err;
	end_io_wq->work.func = end_workqueue_fn;
	end_io_wq->work.flags = 0;

	if (bio->bi_rw & (1 << BIO_RW)) {
		if (end_io_wq->metadata)
			btrfs_queue_worker(&fs_info->endio_meta_write_workers,
					   &end_io_wq->work);
		else
			btrfs_queue_worker(&fs_info->endio_write_workers,
					   &end_io_wq->work);
	} else {
		if (end_io_wq->metadata)
			btrfs_queue_worker(&fs_info->endio_meta_workers,
					   &end_io_wq->work);
		else
			btrfs_queue_worker(&fs_info->endio_workers,
					   &end_io_wq->work);
	}
}

int btrfs_bio_wq_end_io(struct btrfs_fs_info *info, struct bio *bio,
			int metadata)
{
	struct end_io_wq *end_io_wq;
	end_io_wq = kmalloc(sizeof(*end_io_wq), GFP_NOFS);
	if (!end_io_wq)
		return -ENOMEM;

	end_io_wq->private = bio->bi_private;
	end_io_wq->end_io = bio->bi_end_io;
	end_io_wq->info = info;
	end_io_wq->error = 0;
	end_io_wq->bio = bio;
	end_io_wq->metadata = metadata;

	bio->bi_private = end_io_wq;
	bio->bi_end_io = end_workqueue_bio;
	return 0;
}

unsigned long btrfs_async_submit_limit(struct btrfs_fs_info *info)
{
	unsigned long limit = min_t(unsigned long,
				    info->workers.max_workers,
				    info->fs_devices->open_devices);
	return 256 * limit;
}

int btrfs_congested_async(struct btrfs_fs_info *info, int iodone)
{
	return atomic_read(&info->nr_async_bios) >
		btrfs_async_submit_limit(info);
}

static void run_one_async_start(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info;
	struct async_submit_bio *async;

	async = container_of(work, struct  async_submit_bio, work);
	fs_info = BTRFS_I(async->inode)->root->fs_info;
	async->submit_bio_start(async->inode, async->rw, async->bio,
			       async->mirror_num, async->bio_flags);
}

static void run_one_async_done(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info;
	struct async_submit_bio *async;
	int limit;

	async = container_of(work, struct  async_submit_bio, work);
	fs_info = BTRFS_I(async->inode)->root->fs_info;

	limit = btrfs_async_submit_limit(fs_info);
	limit = limit * 2 / 3;

	atomic_dec(&fs_info->nr_async_submits);

	if (atomic_read(&fs_info->nr_async_submits) < limit &&
	    waitqueue_active(&fs_info->async_submit_wait))
		wake_up(&fs_info->async_submit_wait);

	async->submit_bio_done(async->inode, async->rw, async->bio,
			       async->mirror_num, async->bio_flags);
}

static void run_one_async_free(struct btrfs_work *work)
{
	struct async_submit_bio *async;

	async = container_of(work, struct  async_submit_bio, work);
	kfree(async);
}

int btrfs_wq_submit_bio(struct btrfs_fs_info *fs_info, struct inode *inode,
			int rw, struct bio *bio, int mirror_num,
			unsigned long bio_flags,
			extent_submit_bio_hook_t *submit_bio_start,
			extent_submit_bio_hook_t *submit_bio_done)
{
	struct async_submit_bio *async;

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return -ENOMEM;

	async->inode = inode;
	async->rw = rw;
	async->bio = bio;
	async->mirror_num = mirror_num;
	async->submit_bio_start = submit_bio_start;
	async->submit_bio_done = submit_bio_done;

	async->work.func = run_one_async_start;
	async->work.ordered_func = run_one_async_done;
	async->work.ordered_free = run_one_async_free;

	async->work.flags = 0;
	async->bio_flags = bio_flags;

	atomic_inc(&fs_info->nr_async_submits);
	btrfs_queue_worker(&fs_info->workers, &async->work);
#if 0
	int limit = btrfs_async_submit_limit(fs_info);
	if (atomic_read(&fs_info->nr_async_submits) > limit) {
		wait_event_timeout(fs_info->async_submit_wait,
			   (atomic_read(&fs_info->nr_async_submits) < limit),
			   HZ/10);

		wait_event_timeout(fs_info->async_submit_wait,
			   (atomic_read(&fs_info->nr_async_bios) < limit),
			   HZ/10);
	}
#endif
	while (atomic_read(&fs_info->async_submit_draining) &&
	      atomic_read(&fs_info->nr_async_submits)) {
		wait_event(fs_info->async_submit_wait,
			   (atomic_read(&fs_info->nr_async_submits) == 0));
	}

	return 0;
}

static int btree_csum_one_bio(struct bio *bio)
{
	struct bio_vec *bvec = bio->bi_io_vec;
	int bio_index = 0;
	struct btrfs_root *root;

	WARN_ON(bio->bi_vcnt <= 0);
	while (bio_index < bio->bi_vcnt) {
		root = BTRFS_I(bvec->bv_page->mapping->host)->root;
		csum_dirty_buffer(root, bvec->bv_page);
		bio_index++;
		bvec++;
	}
	return 0;
}

static int __btree_submit_bio_start(struct inode *inode, int rw,
				    struct bio *bio, int mirror_num,
				    unsigned long bio_flags)
{
	/*
	 * when we're called for a write, we're already in the async
	 * submission context.  Just jump into btrfs_map_bio
	 */
	btree_csum_one_bio(bio);
	return 0;
}

static int __btree_submit_bio_done(struct inode *inode, int rw, struct bio *bio,
				 int mirror_num, unsigned long bio_flags)
{
	/*
	 * when we're called for a write, we're already in the async
	 * submission context.  Just jump into btrfs_map_bio
	 */
	return btrfs_map_bio(BTRFS_I(inode)->root, rw, bio, mirror_num, 1);
}

static int btree_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
				 int mirror_num, unsigned long bio_flags)
{
	int ret;

	ret = btrfs_bio_wq_end_io(BTRFS_I(inode)->root->fs_info,
					  bio, 1);
	BUG_ON(ret);

	if (!(rw & (1 << BIO_RW))) {
		/*
		 * called for a read, do the setup so that checksum validation
		 * can happen in the async kernel threads
		 */
		return btrfs_map_bio(BTRFS_I(inode)->root, rw, bio,
				     mirror_num, 0);
	}
	/*
	 * kthread helpers are used to submit writes so that checksumming
	 * can happen in parallel across all CPUs
	 */
	return btrfs_wq_submit_bio(BTRFS_I(inode)->root->fs_info,
				   inode, rw, bio, mirror_num, 0,
				   __btree_submit_bio_start,
				   __btree_submit_bio_done);
}

static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;

	if (current->flags & PF_MEMALLOC) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}
	return extent_write_full_page(tree, page, btree_get_extent, wbc);
}

static int btree_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(mapping->host)->io_tree;
	if (wbc->sync_mode == WB_SYNC_NONE) {
		u64 num_dirty;
		u64 start = 0;
		unsigned long thresh = 32 * 1024 * 1024;

		if (wbc->for_kupdate)
			return 0;

		num_dirty = count_range_bits(tree, &start, (u64)-1,
					     thresh, EXTENT_DIRTY);
		if (num_dirty < thresh)
			return 0;
	}
	return extent_writepages(tree, mapping, btree_get_extent, wbc);
}

static int btree_readpage(struct file *file, struct page *page)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_read_full_page(tree, page, btree_get_extent);
}

static int btree_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct extent_io_tree *tree;
	struct extent_map_tree *map;
	int ret;

	if (PageWriteback(page) || PageDirty(page))
		return 0;

	tree = &BTRFS_I(page->mapping->host)->io_tree;
	map = &BTRFS_I(page->mapping->host)->extent_tree;

	ret = try_release_extent_state(map, tree, page, gfp_flags);
	if (!ret)
		return 0;

	ret = try_release_extent_buffer(tree, page);
	if (ret == 1) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}

	return ret;
}

static void btree_invalidatepage(struct page *page, unsigned long offset)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	extent_invalidatepage(tree, page, offset);
	btree_releasepage(page, GFP_NOFS);
	if (PagePrivate(page)) {
		printk(KERN_WARNING "btrfs warning page private not zero "
		       "on page %llu\n", (unsigned long long)page_offset(page));
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}
}

#if 0
static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct buffer_head *bh;
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	struct buffer_head *head;
	if (!page_has_buffers(page)) {
		create_empty_buffers(page, root->fs_info->sb->s_blocksize,
					(1 << BH_Dirty)|(1 << BH_Uptodate));
	}
	head = page_buffers(page);
	bh = head;
	do {
		if (buffer_dirty(bh))
			csum_tree_block(root, bh, 0);
		bh = bh->b_this_page;
	} while (bh != head);
	return block_write_full_page(page, btree_get_block, wbc);
}
#endif

static struct address_space_operations btree_aops = {
	.readpage	= btree_readpage,
	.writepage	= btree_writepage,
	.writepages	= btree_writepages,
	.releasepage	= btree_releasepage,
	.invalidatepage = btree_invalidatepage,
	.sync_page	= block_sync_page,
};

int readahead_tree_block(struct btrfs_root *root, u64 bytenr, u32 blocksize,
			 u64 parent_transid)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	int ret = 0;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return 0;
	read_extent_buffer_pages(&BTRFS_I(btree_inode)->io_tree,
				 buf, 0, 0, btree_get_extent, 0);
	free_extent_buffer(buf);
	return ret;
}

struct extent_buffer *btrfs_find_tree_block(struct btrfs_root *root,
					    u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;
	eb = find_extent_buffer(&BTRFS_I(btree_inode)->io_tree,
				bytenr, blocksize, GFP_NOFS);
	return eb;
}

struct extent_buffer *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;

	eb = alloc_extent_buffer(&BTRFS_I(btree_inode)->io_tree,
				 bytenr, blocksize, NULL, GFP_NOFS);
	return eb;
}


int btrfs_write_tree_block(struct extent_buffer *buf)
{
	return btrfs_fdatawrite_range(buf->first_page->mapping, buf->start,
				      buf->start + buf->len - 1, WB_SYNC_ALL);
}

int btrfs_wait_tree_block_writeback(struct extent_buffer *buf)
{
	return btrfs_wait_on_page_writeback_range(buf->first_page->mapping,
				  buf->start, buf->start + buf->len - 1);
}

struct extent_buffer *read_tree_block(struct btrfs_root *root, u64 bytenr,
				      u32 blocksize, u64 parent_transid)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_io_tree *io_tree;
	int ret;

	io_tree = &BTRFS_I(btree_inode)->io_tree;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return NULL;

	ret = btree_read_extent_buffer_pages(root, buf, 0, parent_transid);

	if (ret == 0)
		buf->flags |= EXTENT_UPTODATE;
	else
		WARN_ON(1);
	return buf;

}

int clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct extent_buffer *buf)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	if (btrfs_header_generation(buf) ==
	    root->fs_info->running_transaction->transid) {
		WARN_ON(!btrfs_tree_locked(buf));
		clear_extent_buffer_dirty(&BTRFS_I(btree_inode)->io_tree,
					  buf);
	}
	return 0;
}

static int __setup_root(u32 nodesize, u32 leafsize, u32 sectorsize,
			u32 stripesize, struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid)
{
	root->node = NULL;
	root->commit_root = NULL;
	root->ref_tree = NULL;
	root->sectorsize = sectorsize;
	root->nodesize = nodesize;
	root->leafsize = leafsize;
	root->stripesize = stripesize;
	root->ref_cows = 0;
	root->track_dirty = 0;

	root->fs_info = fs_info;
	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_inode = 0;
	root->last_inode_alloc = 0;
	root->name = NULL;
	root->in_sysfs = 0;

	INIT_LIST_HEAD(&root->dirty_list);
	INIT_LIST_HEAD(&root->orphan_list);
	INIT_LIST_HEAD(&root->dead_list);
	spin_lock_init(&root->node_lock);
	spin_lock_init(&root->list_lock);
	mutex_init(&root->objectid_mutex);
	mutex_init(&root->log_mutex);
	extent_io_tree_init(&root->dirty_log_pages,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);

	btrfs_leaf_ref_tree_init(&root->ref_tree_struct);
	root->ref_tree = &root->ref_tree_struct;

	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	memset(&root->root_kobj, 0, sizeof(root->root_kobj));
	root->defrag_trans_start = fs_info->generation;
	init_completion(&root->kobj_unregister);
	root->defrag_running = 0;
	root->defrag_level = 0;
	root->root_key.objectid = objectid;
	root->anon_super.s_root = NULL;
	root->anon_super.s_dev = 0;
	INIT_LIST_HEAD(&root->anon_super.s_list);
	INIT_LIST_HEAD(&root->anon_super.s_instances);
	init_rwsem(&root->anon_super.s_umount);

	return 0;
}

static int find_and_setup_root(struct btrfs_root *tree_root,
			       struct btrfs_fs_info *fs_info,
			       u64 objectid,
			       struct btrfs_root *root)
{
	int ret;
	u32 blocksize;
	u64 generation;

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, objectid);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	BUG_ON(ret);

	generation = btrfs_root_generation(&root->root_item);
	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize, generation);
	BUG_ON(!root->node);
	return 0;
}

int btrfs_free_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info)
{
	struct extent_buffer *eb;
	struct btrfs_root *log_root_tree = fs_info->log_root_tree;
	u64 start = 0;
	u64 end = 0;
	int ret;

	if (!log_root_tree)
		return 0;

	while (1) {
		ret = find_first_extent_bit(&log_root_tree->dirty_log_pages,
				    0, &start, &end, EXTENT_DIRTY);
		if (ret)
			break;

		clear_extent_dirty(&log_root_tree->dirty_log_pages,
				   start, end, GFP_NOFS);
	}
	eb = fs_info->log_root_tree->node;

	WARN_ON(btrfs_header_level(eb) != 0);
	WARN_ON(btrfs_header_nritems(eb) != 0);

	ret = btrfs_free_reserved_extent(fs_info->tree_root,
				eb->start, eb->len);
	BUG_ON(ret);

	free_extent_buffer(eb);
	kfree(fs_info->log_root_tree);
	fs_info->log_root_tree = NULL;
	return 0;
}

int btrfs_init_log_root_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *root;
	struct btrfs_root *tree_root = fs_info->tree_root;

	root = kzalloc(sizeof(*root), GFP_NOFS);
	if (!root)
		return -ENOMEM;

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, BTRFS_TREE_LOG_OBJECTID);

	root->root_key.objectid = BTRFS_TREE_LOG_OBJECTID;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = BTRFS_TREE_LOG_OBJECTID;
	root->ref_cows = 0;

	root->node = btrfs_alloc_free_block(trans, root, root->leafsize,
					    0, BTRFS_TREE_LOG_OBJECTID,
					    trans->transid, 0, 0, 0);

	btrfs_set_header_nritems(root->node, 0);
	btrfs_set_header_level(root->node, 0);
	btrfs_set_header_bytenr(root->node, root->node->start);
	btrfs_set_header_generation(root->node, trans->transid);
	btrfs_set_header_owner(root->node, BTRFS_TREE_LOG_OBJECTID);

	write_extent_buffer(root->node, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(root->node),
			    BTRFS_FSID_SIZE);
	btrfs_mark_buffer_dirty(root->node);
	btrfs_tree_unlock(root->node);
	fs_info->log_root_tree = root;
	return 0;
}

struct btrfs_root *btrfs_read_fs_root_no_radix(struct btrfs_root *tree_root,
					       struct btrfs_key *location)
{
	struct btrfs_root *root;
	struct btrfs_fs_info *fs_info = tree_root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *l;
	u64 highest_inode;
	u64 generation;
	u32 blocksize;
	int ret = 0;

	root = kzalloc(sizeof(*root), GFP_NOFS);
	if (!root)
		return ERR_PTR(-ENOMEM);
	if (location->offset == (u64)-1) {
		ret = find_and_setup_root(tree_root, fs_info,
					  location->objectid, root);
		if (ret) {
			kfree(root);
			return ERR_PTR(ret);
		}
		goto insert;
	}

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, location->objectid);

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(NULL, tree_root, location, path, 0, 0);
	if (ret != 0) {
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}
	l = path->nodes[0];
	read_extent_buffer(l, &root->root_item,
	       btrfs_item_ptr_offset(l, path->slots[0]),
	       sizeof(root->root_item));
	memcpy(&root->root_key, location, sizeof(*location));
	ret = 0;
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	if (ret) {
		kfree(root);
		return ERR_PTR(ret);
	}
	generation = btrfs_root_generation(&root->root_item);
	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize, generation);
	BUG_ON(!root->node);
insert:
	if (location->objectid != BTRFS_TREE_LOG_OBJECTID) {
		root->ref_cows = 1;
		ret = btrfs_find_highest_inode(root, &highest_inode);
		if (ret == 0) {
			root->highest_inode = highest_inode;
			root->last_inode_alloc = highest_inode;
		}
	}
	return root;
}

struct btrfs_root *btrfs_lookup_fs_root(struct btrfs_fs_info *fs_info,
					u64 root_objectid)
{
	struct btrfs_root *root;

	if (root_objectid == BTRFS_ROOT_TREE_OBJECTID)
		return fs_info->tree_root;
	if (root_objectid == BTRFS_EXTENT_TREE_OBJECTID)
		return fs_info->extent_root;

	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)root_objectid);
	return root;
}

struct btrfs_root *btrfs_read_fs_root_no_name(struct btrfs_fs_info *fs_info,
					      struct btrfs_key *location)
{
	struct btrfs_root *root;
	int ret;

	if (location->objectid == BTRFS_ROOT_TREE_OBJECTID)
		return fs_info->tree_root;
	if (location->objectid == BTRFS_EXTENT_TREE_OBJECTID)
		return fs_info->extent_root;
	if (location->objectid == BTRFS_CHUNK_TREE_OBJECTID)
		return fs_info->chunk_root;
	if (location->objectid == BTRFS_DEV_TREE_OBJECTID)
		return fs_info->dev_root;
	if (location->objectid == BTRFS_CSUM_TREE_OBJECTID)
		return fs_info->csum_root;

	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)location->objectid);
	if (root)
		return root;

	root = btrfs_read_fs_root_no_radix(fs_info->tree_root, location);
	if (IS_ERR(root))
		return root;

	set_anon_super(&root->anon_super, NULL);

	ret = radix_tree_insert(&fs_info->fs_roots_radix,
				(unsigned long)root->root_key.objectid,
				root);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root);
		return ERR_PTR(ret);
	}
	if (!(fs_info->sb->s_flags & MS_RDONLY)) {
		ret = btrfs_find_dead_roots(fs_info->tree_root,
					    root->root_key.objectid, root);
		BUG_ON(ret);
		btrfs_orphan_cleanup(root);
	}
	return root;
}

struct btrfs_root *btrfs_read_fs_root(struct btrfs_fs_info *fs_info,
				      struct btrfs_key *location,
				      const char *name, int namelen)
{
	struct btrfs_root *root;
	int ret;

	root = btrfs_read_fs_root_no_name(fs_info, location);
	if (!root)
		return NULL;

	if (root->in_sysfs)
		return root;

	ret = btrfs_set_root_name(root, name, namelen);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root);
		return ERR_PTR(ret);
	}
#if 0
	ret = btrfs_sysfs_add_root(root);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root->name);
		kfree(root);
		return ERR_PTR(ret);
	}
#endif
	root->in_sysfs = 1;
	return root;
}

static int btrfs_congested_fn(void *congested_data, int bdi_bits)
{
	struct btrfs_fs_info *info = (struct btrfs_fs_info *)congested_data;
	int ret = 0;
	struct list_head *cur;
	struct btrfs_device *device;
	struct backing_dev_info *bdi;
#if 0
	if ((bdi_bits & (1 << BDI_write_congested)) &&
	    btrfs_congested_async(info, 0))
		return 1;
#endif
	list_for_each(cur, &info->fs_devices->devices) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		if (!device->bdev)
			continue;
		bdi = blk_get_backing_dev_info(device->bdev);
		if (bdi && bdi_congested(bdi, bdi_bits)) {
			ret = 1;
			break;
		}
	}
	return ret;
}

/*
 * this unplugs every device on the box, and it is only used when page
 * is null
 */
static void __unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
	struct list_head *cur;
	struct btrfs_device *device;
	struct btrfs_fs_info *info;

	info = (struct btrfs_fs_info *)bdi->unplug_io_data;
	list_for_each(cur, &info->fs_devices->devices) {
		device = list_entry(cur, struct btrfs_device, dev_list);
		if (!device->bdev)
			continue;

		bdi = blk_get_backing_dev_info(device->bdev);
		if (bdi->unplug_io_fn)
			bdi->unplug_io_fn(bdi, page);
	}
}

static void btrfs_unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
	struct inode *inode;
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct address_space *mapping;
	u64 offset;

	/* the generic O_DIRECT read code does this */
	if (1 || !page) {
		__unplug_io_fn(bdi, page);
		return;
	}

	/*
	 * page->mapping may change at any time.  Get a consistent copy
	 * and use that for everything below
	 */
	smp_mb();
	mapping = page->mapping;
	if (!mapping)
		return;

	inode = mapping->host;

	/*
	 * don't do the expensive searching for a small number of
	 * devices
	 */
	if (BTRFS_I(inode)->root->fs_info->fs_devices->open_devices <= 2) {
		__unplug_io_fn(bdi, page);
		return;
	}

	offset = page_offset(page);

	em_tree = &BTRFS_I(inode)->extent_tree;
	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, offset, PAGE_CACHE_SIZE);
	spin_unlock(&em_tree->lock);
	if (!em) {
		__unplug_io_fn(bdi, page);
		return;
	}

	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		free_extent_map(em);
		__unplug_io_fn(bdi, page);
		return;
	}
	offset = offset - em->start;
	btrfs_unplug_page(&BTRFS_I(inode)->root->fs_info->mapping_tree,
			  em->block_start + offset, page);
	free_extent_map(em);
}

static int setup_bdi(struct btrfs_fs_info *info, struct backing_dev_info *bdi)
{
	bdi_init(bdi);
	bdi->ra_pages	= default_backing_dev_info.ra_pages;
	bdi->state		= 0;
	bdi->capabilities	= default_backing_dev_info.capabilities;
	bdi->unplug_io_fn	= btrfs_unplug_io_fn;
	bdi->unplug_io_data	= info;
	bdi->congested_fn	= btrfs_congested_fn;
	bdi->congested_data	= info;
	return 0;
}

static int bio_ready_for_csum(struct bio *bio)
{
	u64 length = 0;
	u64 buf_len = 0;
	u64 start = 0;
	struct page *page;
	struct extent_io_tree *io_tree = NULL;
	struct btrfs_fs_info *info = NULL;
	struct bio_vec *bvec;
	int i;
	int ret;

	bio_for_each_segment(bvec, bio, i) {
		page = bvec->bv_page;
		if (page->private == EXTENT_PAGE_PRIVATE) {
			length += bvec->bv_len;
			continue;
		}
		if (!page->private) {
			length += bvec->bv_len;
			continue;
		}
		length = bvec->bv_len;
		buf_len = page->private >> 2;
		start = page_offset(page) + bvec->bv_offset;
		io_tree = &BTRFS_I(page->mapping->host)->io_tree;
		info = BTRFS_I(page->mapping->host)->root->fs_info;
	}
	/* are we fully contained in this bio? */
	if (buf_len <= length)
		return 1;

	ret = extent_range_uptodate(io_tree, start + length,
				    start + buf_len - 1);
	if (ret == 1)
		return ret;
	return ret;
}

/*
 * called by the kthread helper functions to finally call the bio end_io
 * functions.  This is where read checksum verification actually happens
 */
static void end_workqueue_fn(struct btrfs_work *work)
{
	struct bio *bio;
	struct end_io_wq *end_io_wq;
	struct btrfs_fs_info *fs_info;
	int error;

	end_io_wq = container_of(work, struct end_io_wq, work);
	bio = end_io_wq->bio;
	fs_info = end_io_wq->info;

	/* metadata bio reads are special because the whole tree block must
	 * be checksummed at once.  This makes sure the entire block is in
	 * ram and up to date before trying to verify things.  For
	 * blocksize <= pagesize, it is basically a noop
	 */
	if (!(bio->bi_rw & (1 << BIO_RW)) && end_io_wq->metadata &&
	    !bio_ready_for_csum(bio)) {
		btrfs_queue_worker(&fs_info->endio_meta_workers,
				   &end_io_wq->work);
		return;
	}
	error = end_io_wq->error;
	bio->bi_private = end_io_wq->private;
	bio->bi_end_io = end_io_wq->end_io;
	kfree(end_io_wq);
	bio_endio(bio, error);
}

static int cleaner_kthread(void *arg)
{
	struct btrfs_root *root = arg;

	do {
		smp_mb();
		if (root->fs_info->closing)
			break;

		vfs_check_frozen(root->fs_info->sb, SB_FREEZE_WRITE);
		mutex_lock(&root->fs_info->cleaner_mutex);
		btrfs_clean_old_snapshots(root);
		mutex_unlock(&root->fs_info->cleaner_mutex);

		if (freezing(current)) {
			refrigerator();
		} else {
			smp_mb();
			if (root->fs_info->closing)
				break;
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			__set_current_state(TASK_RUNNING);
		}
	} while (!kthread_should_stop());
	return 0;
}

static int transaction_kthread(void *arg)
{
	struct btrfs_root *root = arg;
	struct btrfs_trans_handle *trans;
	struct btrfs_transaction *cur;
	unsigned long now;
	unsigned long delay;
	int ret;

	do {
		smp_mb();
		if (root->fs_info->closing)
			break;

		delay = HZ * 30;
		vfs_check_frozen(root->fs_info->sb, SB_FREEZE_WRITE);
		mutex_lock(&root->fs_info->transaction_kthread_mutex);

		if (root->fs_info->total_ref_cache_size > 20 * 1024 * 1024) {
			printk(KERN_INFO "btrfs: total reference cache "
			       "size %llu\n",
			       root->fs_info->total_ref_cache_size);
		}

		mutex_lock(&root->fs_info->trans_mutex);
		cur = root->fs_info->running_transaction;
		if (!cur) {
			mutex_unlock(&root->fs_info->trans_mutex);
			goto sleep;
		}

		now = get_seconds();
		if (now < cur->start_time || now - cur->start_time < 30) {
			mutex_unlock(&root->fs_info->trans_mutex);
			delay = HZ * 5;
			goto sleep;
		}
		mutex_unlock(&root->fs_info->trans_mutex);
		trans = btrfs_start_transaction(root, 1);
		ret = btrfs_commit_transaction(trans, root);
sleep:
		wake_up_process(root->fs_info->cleaner_kthread);
		mutex_unlock(&root->fs_info->transaction_kthread_mutex);

		if (freezing(current)) {
			refrigerator();
		} else {
			if (root->fs_info->closing)
				break;
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(delay);
			__set_current_state(TASK_RUNNING);
		}
	} while (!kthread_should_stop());
	return 0;
}

struct btrfs_root *open_ctree(struct super_block *sb,
			      struct btrfs_fs_devices *fs_devices,
			      char *options)
{
	u32 sectorsize;
	u32 nodesize;
	u32 leafsize;
	u32 blocksize;
	u32 stripesize;
	u64 generation;
	u64 features;
	struct btrfs_key location;
	struct buffer_head *bh;
	struct btrfs_root *extent_root = kzalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *csum_root = kzalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *tree_root = kzalloc(sizeof(struct btrfs_root),
					       GFP_NOFS);
	struct btrfs_fs_info *fs_info = kzalloc(sizeof(*fs_info),
						GFP_NOFS);
	struct btrfs_root *chunk_root = kzalloc(sizeof(struct btrfs_root),
						GFP_NOFS);
	struct btrfs_root *dev_root = kzalloc(sizeof(struct btrfs_root),
					      GFP_NOFS);
	struct btrfs_root *log_tree_root;

	int ret;
	int err = -EINVAL;

	struct btrfs_super_block *disk_super;

	if (!extent_root || !tree_root || !fs_info ||
	    !chunk_root || !dev_root || !csum_root) {
		err = -ENOMEM;
		goto fail;
	}
	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_NOFS);
	INIT_LIST_HEAD(&fs_info->trans_list);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	INIT_LIST_HEAD(&fs_info->hashers);
	INIT_LIST_HEAD(&fs_info->delalloc_inodes);
	spin_lock_init(&fs_info->hash_lock);
	spin_lock_init(&fs_info->delalloc_lock);
	spin_lock_init(&fs_info->new_trans_lock);
	spin_lock_init(&fs_info->ref_cache_lock);

	init_completion(&fs_info->kobj_unregister);
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->csum_root = csum_root;
	fs_info->chunk_root = chunk_root;
	fs_info->dev_root = dev_root;
	fs_info->fs_devices = fs_devices;
	INIT_LIST_HEAD(&fs_info->dirty_cowonly_roots);
	INIT_LIST_HEAD(&fs_info->space_info);
	btrfs_mapping_init(&fs_info->mapping_tree);
	atomic_set(&fs_info->nr_async_submits, 0);
	atomic_set(&fs_info->async_delalloc_pages, 0);
	atomic_set(&fs_info->async_submit_draining, 0);
	atomic_set(&fs_info->nr_async_bios, 0);
	atomic_set(&fs_info->throttles, 0);
	atomic_set(&fs_info->throttle_gen, 0);
	fs_info->sb = sb;
	fs_info->max_extent = (u64)-1;
	fs_info->max_inline = 8192 * 1024;
	setup_bdi(fs_info, &fs_info->bdi);
	fs_info->btree_inode = new_inode(sb);
	fs_info->btree_inode->i_ino = 1;
	fs_info->btree_inode->i_nlink = 1;

	fs_info->thread_pool_size = min_t(unsigned long,
					  num_online_cpus() + 2, 8);

	INIT_LIST_HEAD(&fs_info->ordered_extents);
	spin_lock_init(&fs_info->ordered_extent_lock);

	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = blksize_bits(4096);

	/*
	 * we set the i_size on the btree inode to the max possible int.
	 * the real end of the address space is determined by all of
	 * the devices in the system
	 */
	fs_info->btree_inode->i_size = OFFSET_MAX;
	fs_info->btree_inode->i_mapping->a_ops = &btree_aops;
	fs_info->btree_inode->i_mapping->backing_dev_info = &fs_info->bdi;

	extent_io_tree_init(&BTRFS_I(fs_info->btree_inode)->io_tree,
			     fs_info->btree_inode->i_mapping,
			     GFP_NOFS);
	extent_map_tree_init(&BTRFS_I(fs_info->btree_inode)->extent_tree,
			     GFP_NOFS);

	BTRFS_I(fs_info->btree_inode)->io_tree.ops = &btree_extent_io_ops;

	spin_lock_init(&fs_info->block_group_cache_lock);
	fs_info->block_group_cache_tree.rb_node = NULL;

	extent_io_tree_init(&fs_info->pinned_extents,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&fs_info->pending_del,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&fs_info->extent_ins,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	fs_info->do_barriers = 1;

	INIT_LIST_HEAD(&fs_info->dead_reloc_roots);
	btrfs_leaf_ref_tree_init(&fs_info->reloc_ref_tree);
	btrfs_leaf_ref_tree_init(&fs_info->shared_ref_tree);

	BTRFS_I(fs_info->btree_inode)->root = tree_root;
	memset(&BTRFS_I(fs_info->btree_inode)->location, 0,
	       sizeof(struct btrfs_key));
	insert_inode_hash(fs_info->btree_inode);

	mutex_init(&fs_info->trans_mutex);
	mutex_init(&fs_info->tree_log_mutex);
	mutex_init(&fs_info->drop_mutex);
	mutex_init(&fs_info->extent_ins_mutex);
	mutex_init(&fs_info->pinned_mutex);
	mutex_init(&fs_info->chunk_mutex);
	mutex_init(&fs_info->transaction_kthread_mutex);
	mutex_init(&fs_info->cleaner_mutex);
	mutex_init(&fs_info->volume_mutex);
	mutex_init(&fs_info->tree_reloc_mutex);
	init_waitqueue_head(&fs_info->transaction_throttle);
	init_waitqueue_head(&fs_info->transaction_wait);
	init_waitqueue_head(&fs_info->async_submit_wait);
	init_waitqueue_head(&fs_info->tree_log_wait);
	atomic_set(&fs_info->tree_log_commit, 0);
	atomic_set(&fs_info->tree_log_writers, 0);
	fs_info->tree_log_transid = 0;

	__setup_root(4096, 4096, 4096, 4096, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);


	bh = btrfs_read_dev_super(fs_devices->latest_bdev);
	if (!bh)
		goto fail_iput;

	memcpy(&fs_info->super_copy, bh->b_data, sizeof(fs_info->super_copy));
	memcpy(&fs_info->super_for_commit, &fs_info->super_copy,
	       sizeof(fs_info->super_for_commit));
	brelse(bh);

	memcpy(fs_info->fsid, fs_info->super_copy.fsid, BTRFS_FSID_SIZE);

	disk_super = &fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		goto fail_iput;

	ret = btrfs_parse_options(tree_root, options);
	if (ret) {
		err = ret;
		goto fail_iput;
	}

	features = btrfs_super_incompat_flags(disk_super) &
		~BTRFS_FEATURE_INCOMPAT_SUPP;
	if (features) {
		printk(KERN_ERR "BTRFS: couldn't mount because of "
		       "unsupported optional features (%Lx).\n",
		       features);
		err = -EINVAL;
		goto fail_iput;
	}

	features = btrfs_super_compat_ro_flags(disk_super) &
		~BTRFS_FEATURE_COMPAT_RO_SUPP;
	if (!(sb->s_flags & MS_RDONLY) && features) {
		printk(KERN_ERR "BTRFS: couldn't mount RDWR because of "
		       "unsupported option features (%Lx).\n",
		       features);
		err = -EINVAL;
		goto fail_iput;
	}

	/*
	 * we need to start all the end_io workers up front because the
	 * queue work function gets called at interrupt time, and so it
	 * cannot dynamically grow.
	 */
	btrfs_init_workers(&fs_info->workers, "worker",
			   fs_info->thread_pool_size);

	btrfs_init_workers(&fs_info->delalloc_workers, "delalloc",
			   fs_info->thread_pool_size);

	btrfs_init_workers(&fs_info->submit_workers, "submit",
			   min_t(u64, fs_devices->num_devices,
			   fs_info->thread_pool_size));

	/* a higher idle thresh on the submit workers makes it much more
	 * likely that bios will be send down in a sane order to the
	 * devices
	 */
	fs_info->submit_workers.idle_thresh = 64;

	fs_info->workers.idle_thresh = 16;
	fs_info->workers.ordered = 1;

	fs_info->delalloc_workers.idle_thresh = 2;
	fs_info->delalloc_workers.ordered = 1;

	btrfs_init_workers(&fs_info->fixup_workers, "fixup", 1);
	btrfs_init_workers(&fs_info->endio_workers, "endio",
			   fs_info->thread_pool_size);
	btrfs_init_workers(&fs_info->endio_meta_workers, "endio-meta",
			   fs_info->thread_pool_size);
	btrfs_init_workers(&fs_info->endio_meta_write_workers,
			   "endio-meta-write", fs_info->thread_pool_size);
	btrfs_init_workers(&fs_info->endio_write_workers, "endio-write",
			   fs_info->thread_pool_size);

	/*
	 * endios are largely parallel and should have a very
	 * low idle thresh
	 */
	fs_info->endio_workers.idle_thresh = 4;
	fs_info->endio_write_workers.idle_thresh = 64;
	fs_info->endio_meta_write_workers.idle_thresh = 64;

	btrfs_start_workers(&fs_info->workers, 1);
	btrfs_start_workers(&fs_info->submit_workers, 1);
	btrfs_start_workers(&fs_info->delalloc_workers, 1);
	btrfs_start_workers(&fs_info->fixup_workers, 1);
	btrfs_start_workers(&fs_info->endio_workers, fs_info->thread_pool_size);
	btrfs_start_workers(&fs_info->endio_meta_workers,
			    fs_info->thread_pool_size);
	btrfs_start_workers(&fs_info->endio_meta_write_workers,
			    fs_info->thread_pool_size);
	btrfs_start_workers(&fs_info->endio_write_workers,
			    fs_info->thread_pool_size);

	fs_info->bdi.ra_pages *= btrfs_super_num_devices(disk_super);
	fs_info->bdi.ra_pages = max(fs_info->bdi.ra_pages,
				    4 * 1024 * 1024 / PAGE_CACHE_SIZE);

	nodesize = btrfs_super_nodesize(disk_super);
	leafsize = btrfs_super_leafsize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	stripesize = btrfs_super_stripesize(disk_super);
	tree_root->nodesize = nodesize;
	tree_root->leafsize = leafsize;
	tree_root->sectorsize = sectorsize;
	tree_root->stripesize = stripesize;

	sb->s_blocksize = sectorsize;
	sb->s_blocksize_bits = blksize_bits(sectorsize);

	if (strncmp((char *)(&disk_super->magic), BTRFS_MAGIC,
		    sizeof(disk_super->magic))) {
		printk(KERN_INFO "btrfs: valid FS not found on %s\n", sb->s_id);
		goto fail_sb_buffer;
	}

	mutex_lock(&fs_info->chunk_mutex);
	ret = btrfs_read_sys_array(tree_root);
	mutex_unlock(&fs_info->chunk_mutex);
	if (ret) {
		printk(KERN_WARNING "btrfs: failed to read the system "
		       "array on %s\n", sb->s_id);
		goto fail_sys_array;
	}

	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_chunk_root_level(disk_super));
	generation = btrfs_super_chunk_root_generation(disk_super);

	__setup_root(nodesize, leafsize, sectorsize, stripesize,
		     chunk_root, fs_info, BTRFS_CHUNK_TREE_OBJECTID);

	chunk_root->node = read_tree_block(chunk_root,
					   btrfs_super_chunk_root(disk_super),
					   blocksize, generation);
	BUG_ON(!chunk_root->node);

	read_extent_buffer(chunk_root->node, fs_info->chunk_tree_uuid,
	   (unsigned long)btrfs_header_chunk_tree_uuid(chunk_root->node),
	   BTRFS_UUID_SIZE);

	mutex_lock(&fs_info->chunk_mutex);
	ret = btrfs_read_chunk_tree(chunk_root);
	mutex_unlock(&fs_info->chunk_mutex);
	if (ret) {
		printk(KERN_WARNING "btrfs: failed to read chunk tree on %s\n",
		       sb->s_id);
		goto fail_chunk_root;
	}

	btrfs_close_extra_devices(fs_devices);

	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_root_level(disk_super));
	generation = btrfs_super_generation(disk_super);

	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super),
					  blocksize, generation);
	if (!tree_root->node)
		goto fail_chunk_root;


	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root);
	if (ret)
		goto fail_tree_root;
	extent_root->track_dirty = 1;

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_DEV_TREE_OBJECTID, dev_root);
	dev_root->track_dirty = 1;

	if (ret)
		goto fail_extent_root;

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_CSUM_TREE_OBJECTID, csum_root);
	if (ret)
		goto fail_extent_root;

	csum_root->track_dirty = 1;

	btrfs_read_block_groups(extent_root);

	fs_info->generation = generation;
	fs_info->last_trans_committed = generation;
	fs_info->data_alloc_profile = (u64)-1;
	fs_info->metadata_alloc_profile = (u64)-1;
	fs_info->system_alloc_profile = fs_info->metadata_alloc_profile;
	fs_info->cleaner_kthread = kthread_run(cleaner_kthread, tree_root,
					       "btrfs-cleaner");
	if (!fs_info->cleaner_kthread)
		goto fail_csum_root;

	fs_info->transaction_kthread = kthread_run(transaction_kthread,
						   tree_root,
						   "btrfs-transaction");
	if (!fs_info->transaction_kthread)
		goto fail_cleaner;

	if (btrfs_super_log_root(disk_super) != 0) {
		u64 bytenr = btrfs_super_log_root(disk_super);

		if (fs_devices->rw_devices == 0) {
			printk(KERN_WARNING "Btrfs log replay required "
			       "on RO media\n");
			err = -EIO;
			goto fail_trans_kthread;
		}
		blocksize =
		     btrfs_level_size(tree_root,
				      btrfs_super_log_root_level(disk_super));

		log_tree_root = kzalloc(sizeof(struct btrfs_root),
						      GFP_NOFS);

		__setup_root(nodesize, leafsize, sectorsize, stripesize,
			     log_tree_root, fs_info, BTRFS_TREE_LOG_OBJECTID);

		log_tree_root->node = read_tree_block(tree_root, bytenr,
						      blocksize,
						      generation + 1);
		ret = btrfs_recover_log_trees(log_tree_root);
		BUG_ON(ret);

		if (sb->s_flags & MS_RDONLY) {
			ret =  btrfs_commit_super(tree_root);
			BUG_ON(ret);
		}
	}

	if (!(sb->s_flags & MS_RDONLY)) {
		ret = btrfs_cleanup_reloc_trees(tree_root);
		BUG_ON(ret);
	}

	location.objectid = BTRFS_FS_TREE_OBJECTID;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = (u64)-1;

	fs_info->fs_root = btrfs_read_fs_root_no_name(fs_info, &location);
	if (!fs_info->fs_root)
		goto fail_trans_kthread;
	return tree_root;

fail_trans_kthread:
	kthread_stop(fs_info->transaction_kthread);
fail_cleaner:
	kthread_stop(fs_info->cleaner_kthread);

	/*
	 * make sure we're done with the btree inode before we stop our
	 * kthreads
	 */
	filemap_write_and_wait(fs_info->btree_inode->i_mapping);
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);

fail_csum_root:
	free_extent_buffer(csum_root->node);
fail_extent_root:
	free_extent_buffer(extent_root->node);
fail_tree_root:
	free_extent_buffer(tree_root->node);
fail_chunk_root:
	free_extent_buffer(chunk_root->node);
fail_sys_array:
	free_extent_buffer(dev_root->node);
fail_sb_buffer:
	btrfs_stop_workers(&fs_info->fixup_workers);
	btrfs_stop_workers(&fs_info->delalloc_workers);
	btrfs_stop_workers(&fs_info->workers);
	btrfs_stop_workers(&fs_info->endio_workers);
	btrfs_stop_workers(&fs_info->endio_meta_workers);
	btrfs_stop_workers(&fs_info->endio_meta_write_workers);
	btrfs_stop_workers(&fs_info->endio_write_workers);
	btrfs_stop_workers(&fs_info->submit_workers);
fail_iput:
	invalidate_inode_pages2(fs_info->btree_inode->i_mapping);
	iput(fs_info->btree_inode);
fail:
	btrfs_close_devices(fs_info->fs_devices);
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	kfree(extent_root);
	kfree(tree_root);
	bdi_destroy(&fs_info->bdi);
	kfree(fs_info);
	kfree(chunk_root);
	kfree(dev_root);
	kfree(csum_root);
	return ERR_PTR(err);
}

static void btrfs_end_buffer_write_sync(struct buffer_head *bh, int uptodate)
{
	char b[BDEVNAME_SIZE];

	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		if (!buffer_eopnotsupp(bh) && printk_ratelimit()) {
			printk(KERN_WARNING "lost page write due to "
					"I/O error on %s\n",
				       bdevname(bh->b_bdev, b));
		}
		/* note, we dont' set_buffer_write_io_error because we have
		 * our own ways of dealing with the IO errors
		 */
		clear_buffer_uptodate(bh);
	}
	unlock_buffer(bh);
	put_bh(bh);
}

struct buffer_head *btrfs_read_dev_super(struct block_device *bdev)
{
	struct buffer_head *bh;
	struct buffer_head *latest = NULL;
	struct btrfs_super_block *super;
	int i;
	u64 transid = 0;
	u64 bytenr;

	/* we would like to check all the supers, but that would make
	 * a btrfs mount succeed after a mkfs from a different FS.
	 * So, we need to add a special mount option to scan for
	 * later supers, using BTRFS_SUPER_MIRROR_MAX instead
	 */
	for (i = 0; i < 1; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + 4096 >= i_size_read(bdev->bd_inode))
			break;
		bh = __bread(bdev, bytenr / 4096, 4096);
		if (!bh)
			continue;

		super = (struct btrfs_super_block *)bh->b_data;
		if (btrfs_super_bytenr(super) != bytenr ||
		    strncmp((char *)(&super->magic), BTRFS_MAGIC,
			    sizeof(super->magic))) {
			brelse(bh);
			continue;
		}

		if (!latest || btrfs_super_generation(super) > transid) {
			brelse(latest);
			latest = bh;
			transid = btrfs_super_generation(super);
		} else {
			brelse(bh);
		}
	}
	return latest;
}

static int write_dev_supers(struct btrfs_device *device,
			    struct btrfs_super_block *sb,
			    int do_barriers, int wait, int max_mirrors)
{
	struct buffer_head *bh;
	int i;
	int ret;
	int errors = 0;
	u32 crc;
	u64 bytenr;
	int last_barrier = 0;

	if (max_mirrors == 0)
		max_mirrors = BTRFS_SUPER_MIRROR_MAX;

	/* make sure only the last submit_bh does a barrier */
	if (do_barriers) {
		for (i = 0; i < max_mirrors; i++) {
			bytenr = btrfs_sb_offset(i);
			if (bytenr + BTRFS_SUPER_INFO_SIZE >=
			    device->total_bytes)
				break;
			last_barrier = i;
		}
	}

	for (i = 0; i < max_mirrors; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + BTRFS_SUPER_INFO_SIZE >= device->total_bytes)
			break;

		if (wait) {
			bh = __find_get_block(device->bdev, bytenr / 4096,
					      BTRFS_SUPER_INFO_SIZE);
			BUG_ON(!bh);
			brelse(bh);
			wait_on_buffer(bh);
			if (buffer_uptodate(bh)) {
				brelse(bh);
				continue;
			}
		} else {
			btrfs_set_super_bytenr(sb, bytenr);

			crc = ~(u32)0;
			crc = btrfs_csum_data(NULL, (char *)sb +
					      BTRFS_CSUM_SIZE, crc,
					      BTRFS_SUPER_INFO_SIZE -
					      BTRFS_CSUM_SIZE);
			btrfs_csum_final(crc, sb->csum);

			bh = __getblk(device->bdev, bytenr / 4096,
				      BTRFS_SUPER_INFO_SIZE);
			memcpy(bh->b_data, sb, BTRFS_SUPER_INFO_SIZE);

			set_buffer_uptodate(bh);
			get_bh(bh);
			lock_buffer(bh);
			bh->b_end_io = btrfs_end_buffer_write_sync;
		}

		if (i == last_barrier && do_barriers && device->barriers) {
			ret = submit_bh(WRITE_BARRIER, bh);
			if (ret == -EOPNOTSUPP) {
				printk("btrfs: disabling barriers on dev %s\n",
				       device->name);
				set_buffer_uptodate(bh);
				device->barriers = 0;
				get_bh(bh);
				lock_buffer(bh);
				ret = submit_bh(WRITE, bh);
			}
		} else {
			ret = submit_bh(WRITE, bh);
		}

		if (!ret && wait) {
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh))
				errors++;
		} else if (ret) {
			errors++;
		}
		if (wait)
			brelse(bh);
	}
	return errors < i ? 0 : -1;
}

int write_all_supers(struct btrfs_root *root, int max_mirrors)
{
	struct list_head *cur;
	struct list_head *head = &root->fs_info->fs_devices->devices;
	struct btrfs_device *dev;
	struct btrfs_super_block *sb;
	struct btrfs_dev_item *dev_item;
	int ret;
	int do_barriers;
	int max_errors;
	int total_errors = 0;
	u64 flags;

	max_errors = btrfs_super_num_devices(&root->fs_info->super_copy) - 1;
	do_barriers = !btrfs_test_opt(root, NOBARRIER);

	sb = &root->fs_info->super_for_commit;
	dev_item = &sb->dev_item;
	list_for_each(cur, head) {
		dev = list_entry(cur, struct btrfs_device, dev_list);
		if (!dev->bdev) {
			total_errors++;
			continue;
		}
		if (!dev->in_fs_metadata || !dev->writeable)
			continue;

		btrfs_set_stack_device_generation(dev_item, 0);
		btrfs_set_stack_device_type(dev_item, dev->type);
		btrfs_set_stack_device_id(dev_item, dev->devid);
		btrfs_set_stack_device_total_bytes(dev_item, dev->total_bytes);
		btrfs_set_stack_device_bytes_used(dev_item, dev->bytes_used);
		btrfs_set_stack_device_io_align(dev_item, dev->io_align);
		btrfs_set_stack_device_io_width(dev_item, dev->io_width);
		btrfs_set_stack_device_sector_size(dev_item, dev->sector_size);
		memcpy(dev_item->uuid, dev->uuid, BTRFS_UUID_SIZE);
		memcpy(dev_item->fsid, dev->fs_devices->fsid, BTRFS_UUID_SIZE);

		flags = btrfs_super_flags(sb);
		btrfs_set_super_flags(sb, flags | BTRFS_HEADER_FLAG_WRITTEN);

		ret = write_dev_supers(dev, sb, do_barriers, 0, max_mirrors);
		if (ret)
			total_errors++;
	}
	if (total_errors > max_errors) {
		printk(KERN_ERR "btrfs: %d errors while writing supers\n",
		       total_errors);
		BUG();
	}

	total_errors = 0;
	list_for_each(cur, head) {
		dev = list_entry(cur, struct btrfs_device, dev_list);
		if (!dev->bdev)
			continue;
		if (!dev->in_fs_metadata || !dev->writeable)
			continue;

		ret = write_dev_supers(dev, sb, do_barriers, 1, max_mirrors);
		if (ret)
			total_errors++;
	}
	if (total_errors > max_errors) {
		printk(KERN_ERR "btrfs: %d errors while writing supers\n",
		       total_errors);
		BUG();
	}
	return 0;
}

int write_ctree_super(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root, int max_mirrors)
{
	int ret;

	ret = write_all_supers(root, max_mirrors);
	return ret;
}

int btrfs_free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)root->root_key.objectid);
	if (root->anon_super.s_dev) {
		down_write(&root->anon_super.s_umount);
		kill_anon_super(&root->anon_super);
	}
	if (root->node)
		free_extent_buffer(root->node);
	if (root->commit_root)
		free_extent_buffer(root->commit_root);
	kfree(root->name);
	kfree(root);
	return 0;
}

static int del_fs_roots(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *gang[8];
	int i;

	while (1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, 0,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++)
			btrfs_free_fs_root(fs_info, gang[i]);
	}
	return 0;
}

int btrfs_cleanup_fs_roots(struct btrfs_fs_info *fs_info)
{
	u64 root_objectid = 0;
	struct btrfs_root *gang[8];
	int i;
	int ret;

	while (1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, root_objectid,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++) {
			root_objectid = gang[i]->root_key.objectid;
			ret = btrfs_find_dead_roots(fs_info->tree_root,
						    root_objectid, gang[i]);
			BUG_ON(ret);
			btrfs_orphan_cleanup(gang[i]);
		}
		root_objectid++;
	}
	return 0;
}

int btrfs_commit_super(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	int ret;

	mutex_lock(&root->fs_info->cleaner_mutex);
	btrfs_clean_old_snapshots(root);
	mutex_unlock(&root->fs_info->cleaner_mutex);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	BUG_ON(ret);
	/* run commit again to drop the original snapshot */
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	ret = btrfs_write_and_wait_transaction(NULL, root);
	BUG_ON(ret);

	ret = write_ctree_super(NULL, root, 0);
	return ret;
}

int close_ctree(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	fs_info->closing = 1;
	smp_mb();

	kthread_stop(root->fs_info->transaction_kthread);
	kthread_stop(root->fs_info->cleaner_kthread);

	if (!(fs_info->sb->s_flags & MS_RDONLY)) {
		ret =  btrfs_commit_super(root);
		if (ret)
			printk(KERN_ERR "btrfs: commit super ret %d\n", ret);
	}

	if (fs_info->delalloc_bytes) {
		printk(KERN_INFO "btrfs: at unmount delalloc count %llu\n",
		       fs_info->delalloc_bytes);
	}
	if (fs_info->total_ref_cache_size) {
		printk(KERN_INFO "btrfs: at umount reference cache size %llu\n",
		       (unsigned long long)fs_info->total_ref_cache_size);
	}

	if (fs_info->extent_root->node)
		free_extent_buffer(fs_info->extent_root->node);

	if (fs_info->tree_root->node)
		free_extent_buffer(fs_info->tree_root->node);

	if (root->fs_info->chunk_root->node)
		free_extent_buffer(root->fs_info->chunk_root->node);

	if (root->fs_info->dev_root->node)
		free_extent_buffer(root->fs_info->dev_root->node);

	if (root->fs_info->csum_root->node)
		free_extent_buffer(root->fs_info->csum_root->node);

	btrfs_free_block_groups(root->fs_info);

	del_fs_roots(fs_info);

	iput(fs_info->btree_inode);

	btrfs_stop_workers(&fs_info->fixup_workers);
	btrfs_stop_workers(&fs_info->delalloc_workers);
	btrfs_stop_workers(&fs_info->workers);
	btrfs_stop_workers(&fs_info->endio_workers);
	btrfs_stop_workers(&fs_info->endio_meta_workers);
	btrfs_stop_workers(&fs_info->endio_meta_write_workers);
	btrfs_stop_workers(&fs_info->endio_write_workers);
	btrfs_stop_workers(&fs_info->submit_workers);

#if 0
	while (!list_empty(&fs_info->hashers)) {
		struct btrfs_hasher *hasher;
		hasher = list_entry(fs_info->hashers.next, struct btrfs_hasher,
				    hashers);
		list_del(&hasher->hashers);
		crypto_free_hash(&fs_info->hash_tfm);
		kfree(hasher);
	}
#endif
	btrfs_close_devices(fs_info->fs_devices);
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	bdi_destroy(&fs_info->bdi);

	kfree(fs_info->extent_root);
	kfree(fs_info->tree_root);
	kfree(fs_info->chunk_root);
	kfree(fs_info->dev_root);
	kfree(fs_info->csum_root);
	return 0;
}

int btrfs_buffer_uptodate(struct extent_buffer *buf, u64 parent_transid)
{
	int ret;
	struct inode *btree_inode = buf->first_page->mapping->host;

	ret = extent_buffer_uptodate(&BTRFS_I(btree_inode)->io_tree, buf);
	if (!ret)
		return ret;

	ret = verify_parent_transid(&BTRFS_I(btree_inode)->io_tree, buf,
				    parent_transid);
	return !ret;
}

int btrfs_set_buffer_uptodate(struct extent_buffer *buf)
{
	struct inode *btree_inode = buf->first_page->mapping->host;
	return set_extent_buffer_uptodate(&BTRFS_I(btree_inode)->io_tree,
					  buf);
}

void btrfs_mark_buffer_dirty(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	u64 transid = btrfs_header_generation(buf);
	struct inode *btree_inode = root->fs_info->btree_inode;

	WARN_ON(!btrfs_tree_locked(buf));
	if (transid != root->fs_info->generation) {
		printk(KERN_CRIT "btrfs transid mismatch buffer %llu, "
		       "found %llu running %llu\n",
			(unsigned long long)buf->start,
			(unsigned long long)transid,
			(unsigned long long)root->fs_info->generation);
		WARN_ON(1);
	}
	set_extent_buffer_dirty(&BTRFS_I(btree_inode)->io_tree, buf);
}

void btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr)
{
	/*
	 * looks as though older kernels can get into trouble with
	 * this code, they end up stuck in balance_dirty_pages forever
	 */
	struct extent_io_tree *tree;
	u64 num_dirty;
	u64 start = 0;
	unsigned long thresh = 32 * 1024 * 1024;
	tree = &BTRFS_I(root->fs_info->btree_inode)->io_tree;

	if (current_is_pdflush() || current->flags & PF_MEMALLOC)
		return;

	num_dirty = count_range_bits(tree, &start, (u64)-1,
				     thresh, EXTENT_DIRTY);
	if (num_dirty > thresh) {
		balance_dirty_pages_ratelimited_nr(
				   root->fs_info->btree_inode->i_mapping, 1);
	}
	return;
}

int btrfs_read_buffer(struct extent_buffer *buf, u64 parent_transid)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	int ret;
	ret = btree_read_extent_buffer_pages(root, buf, 0, parent_transid);
	if (ret == 0)
		buf->flags |= EXTENT_UPTODATE;
	return ret;
}

int btree_lock_page_hook(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_buffer *eb;
	unsigned long len;
	u64 bytenr = page_offset(page);

	if (page->private == EXTENT_PAGE_PRIVATE)
		goto out;

	len = page->private >> 2;
	eb = find_extent_buffer(io_tree, bytenr, len, GFP_NOFS);
	if (!eb)
		goto out;

	btrfs_tree_lock(eb);
	spin_lock(&root->fs_info->hash_lock);
	btrfs_set_header_flag(eb, BTRFS_HEADER_FLAG_WRITTEN);
	spin_unlock(&root->fs_info->hash_lock);
	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);
out:
	lock_page(page);
	return 0;
}

static struct extent_io_ops btree_extent_io_ops = {
	.write_cache_pages_lock_hook = btree_lock_page_hook,
	.readpage_end_io_hook = btree_readpage_end_io_hook,
	.submit_bio_hook = btree_submit_bio_hook,
	/* note we're sharing with inode.c for the merge bio hook */
	.merge_bio_hook = btrfs_merge_bio_hook,
};
