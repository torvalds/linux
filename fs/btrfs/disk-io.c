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

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/radix-tree.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h> // for block_sync_page
#include <linux/workqueue.h>
#include "crc32c.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "volumes.h"
#include "print-tree.h"

#if 0
static int check_tree_block(struct btrfs_root *root, struct extent_buffer *buf)
{
	if (extent_buffer_blocknr(buf) != btrfs_header_blocknr(buf)) {
		printk(KERN_CRIT "buf blocknr(buf) is %llu, header is %llu\n",
		       (unsigned long long)extent_buffer_blocknr(buf),
		       (unsigned long long)btrfs_header_blocknr(buf));
		return 1;
	}
	return 0;
}
#endif

static struct extent_io_ops btree_extent_io_ops;
static struct workqueue_struct *end_io_workqueue;
static struct workqueue_struct *async_submit_workqueue;

struct end_io_wq {
	struct bio *bio;
	bio_end_io_t *end_io;
	void *private;
	struct btrfs_fs_info *info;
	int error;
	int metadata;
	struct list_head list;
};

struct async_submit_bio {
	struct inode *inode;
	struct bio *bio;
	struct list_head list;
	extent_submit_bio_hook_t *submit_bio_hook;
	int rw;
	int mirror_num;
};

struct extent_map *btree_get_extent(struct inode *inode, struct page *page,
				    size_t page_offset, u64 start, u64 len,
				    int create)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	int ret;

	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	spin_unlock(&em_tree->lock);
	if (em)
		goto out;

	em = alloc_extent_map(GFP_NOFS);
	if (!em) {
		em = ERR_PTR(-ENOMEM);
		goto out;
	}
	em->start = 0;
	em->len = (u64)-1;
	em->block_start = 0;
	em->bdev = inode->i_sb->s_bdev;

	spin_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	if (ret == -EEXIST) {
		u64 failed_start = em->start;
		u64 failed_len = em->len;

		printk("failed to insert %Lu %Lu -> %Lu into tree\n",
		       em->start, em->len, em->block_start);
		free_extent_map(em);
		em = lookup_extent_mapping(em_tree, start, len);
		if (em) {
			printk("after failing, found %Lu %Lu %Lu\n",
			       em->start, em->len, em->block_start);
			ret = 0;
		} else {
			em = lookup_extent_mapping(em_tree, failed_start,
						   failed_len);
			if (em) {
				printk("double failure lookup gives us "
				       "%Lu %Lu -> %Lu\n", em->start,
				       em->len, em->block_start);
				free_extent_map(em);
			}
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

static int csum_tree_block(struct btrfs_root *root, struct extent_buffer *buf,
			   int verify)
{
	char result[BTRFS_CRC32_SIZE];
	unsigned long len;
	unsigned long cur_len;
	unsigned long offset = BTRFS_CSUM_SIZE;
	char *map_token = NULL;
	char *kaddr;
	unsigned long map_start;
	unsigned long map_len;
	int err;
	u32 crc = ~(u32)0;

	len = buf->len - offset;
	while(len > 0) {
		err = map_private_extent_buffer(buf, offset, 32,
					&map_token, &kaddr,
					&map_start, &map_len, KM_USER0);
		if (err) {
			printk("failed to map extent buffer! %lu\n",
			       offset);
			return 1;
		}
		cur_len = min(len, map_len - (offset - map_start));
		crc = btrfs_csum_data(root, kaddr + offset - map_start,
				      crc, cur_len);
		len -= cur_len;
		offset += cur_len;
		unmap_extent_buffer(buf, map_token, KM_USER0);
	}
	btrfs_csum_final(crc, result);

	if (verify) {
		int from_this_trans = 0;

		if (root->fs_info->running_transaction &&
		    btrfs_header_generation(buf) ==
		    root->fs_info->running_transaction->transid)
			from_this_trans = 1;

		/* FIXME, this is not good */
		if (memcmp_extent_buffer(buf, result, 0, BTRFS_CRC32_SIZE)) {
			u32 val;
			u32 found = 0;
			memcpy(&found, result, BTRFS_CRC32_SIZE);

			read_extent_buffer(buf, &val, 0, BTRFS_CRC32_SIZE);
			printk("btrfs: %s checksum verify failed on %llu "
			       "wanted %X found %X from_this_trans %d "
			       "level %d\n",
			       root->fs_info->sb->s_id,
			       buf->start, val, found, from_this_trans,
			       btrfs_header_level(buf));
			return 1;
		}
	} else {
		write_extent_buffer(buf, result, 0, BTRFS_CRC32_SIZE);
	}
	return 0;
}

static int btree_read_extent_buffer_pages(struct btrfs_root *root,
					  struct extent_buffer *eb,
					  u64 start)
{
	struct extent_io_tree *io_tree;
	int ret;
	int num_copies = 0;
	int mirror_num = 0;

	io_tree = &BTRFS_I(root->fs_info->btree_inode)->io_tree;
	while (1) {
		ret = read_extent_buffer_pages(io_tree, eb, start, 1,
					       btree_get_extent, mirror_num);
		if (!ret)
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

int csum_dirty_buffer(struct btrfs_root *root, struct page *page)
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
	if (len == 0) {
		WARN_ON(1);
	}
	eb = alloc_extent_buffer(tree, start, len, page, GFP_NOFS);
	ret = btree_read_extent_buffer_pages(root, eb, start + PAGE_CACHE_SIZE);
	BUG_ON(ret);
	btrfs_clear_buffer_defrag(eb);
	found_start = btrfs_header_bytenr(eb);
	if (found_start != start) {
		printk("warning: eb start incorrect %Lu buffer %Lu len %lu\n",
		       start, found_start, len);
		WARN_ON(1);
		goto err;
	}
	if (eb->first_page != page) {
		printk("bad first page %lu %lu\n", eb->first_page->index,
		       page->index);
		WARN_ON(1);
		goto err;
	}
	if (!PageUptodate(page)) {
		printk("csum not up to date page %lu\n", page->index);
		WARN_ON(1);
		goto err;
	}
	found_level = btrfs_header_level(eb);
	spin_lock(&root->fs_info->hash_lock);
	btrfs_set_header_flag(eb, BTRFS_HEADER_FLAG_WRITTEN);
	spin_unlock(&root->fs_info->hash_lock);
	csum_tree_block(root, eb, 0);
err:
	free_extent_buffer(eb);
out:
	return 0;
}

static int btree_writepage_io_hook(struct page *page, u64 start, u64 end)
{
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;

	csum_dirty_buffer(root, page);
	return 0;
}

int btree_readpage_end_io_hook(struct page *page, u64 start, u64 end,
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
	if (len == 0) {
		WARN_ON(1);
	}
	eb = alloc_extent_buffer(tree, start, len, page, GFP_NOFS);

	btrfs_clear_buffer_defrag(eb);
	found_start = btrfs_header_bytenr(eb);
	if (found_start != start) {
		ret = -EIO;
		goto err;
	}
	if (eb->first_page != page) {
		printk("bad first page %lu %lu\n", eb->first_page->index,
		       page->index);
		WARN_ON(1);
		ret = -EIO;
		goto err;
	}
	found_level = btrfs_header_level(eb);

	ret = csum_tree_block(root, eb, 1);
	if (ret)
		ret = -EIO;

	end = min_t(u64, eb->len, PAGE_CACHE_SIZE);
	end = eb->start + end - 1;
	release_extent_buffer_tail_pages(eb);
err:
	free_extent_buffer(eb);
out:
	return ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
static void end_workqueue_bio(struct bio *bio, int err)
#else
static int end_workqueue_bio(struct bio *bio,
				   unsigned int bytes_done, int err)
#endif
{
	struct end_io_wq *end_io_wq = bio->bi_private;
	struct btrfs_fs_info *fs_info;
	unsigned long flags;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	if (bio->bi_size)
		return 1;
#endif

	fs_info = end_io_wq->info;
	spin_lock_irqsave(&fs_info->end_io_work_lock, flags);
	end_io_wq->error = err;
	list_add_tail(&end_io_wq->list, &fs_info->end_io_work_list);
	spin_unlock_irqrestore(&fs_info->end_io_work_lock, flags);
	queue_work(end_io_workqueue, &fs_info->end_io_work);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
	return 0;
#endif
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

int btrfs_wq_submit_bio(struct btrfs_fs_info *fs_info, struct inode *inode,
			int rw, struct bio *bio, int mirror_num,
			extent_submit_bio_hook_t *submit_bio_hook)
{
	struct async_submit_bio *async;

	/*
	 * inline writerback should stay inline, only hop to the async
	 * queue if we're pdflush
	 */
	if (!current_is_pdflush())
		return submit_bio_hook(inode, rw, bio, mirror_num);

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return -ENOMEM;

	async->inode = inode;
	async->rw = rw;
	async->bio = bio;
	async->mirror_num = mirror_num;
	async->submit_bio_hook = submit_bio_hook;

	spin_lock(&fs_info->async_submit_work_lock);
	list_add_tail(&async->list, &fs_info->async_submit_work_list);
	spin_unlock(&fs_info->async_submit_work_lock);

	queue_work(async_submit_workqueue, &fs_info->async_submit_work);
	return 0;
}

static int __btree_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
				 int mirror_num)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 offset;
	int ret;

	offset = bio->bi_sector << 9;

	if (rw & (1 << BIO_RW)) {
		return btrfs_map_bio(BTRFS_I(inode)->root, rw, bio, mirror_num);
	}

	ret = btrfs_bio_wq_end_io(root->fs_info, bio, 1);
	BUG_ON(ret);

	if (offset == BTRFS_SUPER_INFO_OFFSET) {
		bio->bi_bdev = root->fs_info->fs_devices->latest_bdev;
		submit_bio(rw, bio);
		return 0;
	}
	return btrfs_map_bio(BTRFS_I(inode)->root, rw, bio, mirror_num);
}

static int btree_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
				 int mirror_num)
{
	if (!(rw & (1 << BIO_RW))) {
		return __btree_submit_bio_hook(inode, rw, bio, mirror_num);
	}
	return btrfs_wq_submit_bio(BTRFS_I(inode)->root->fs_info,
				   inode, rw, bio, mirror_num,
				   __btree_submit_bio_hook);
}

static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
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
		unsigned long thresh = 96 * 1024 * 1024;

		if (wbc->for_kupdate)
			return 0;

		if (current_is_pdflush()) {
			thresh = 96 * 1024 * 1024;
		} else {
			thresh = 8 * 1024 * 1024;
		}
		num_dirty = count_range_bits(tree, &start, (u64)-1,
					     thresh, EXTENT_DIRTY);
		if (num_dirty < thresh) {
			return 0;
		}
	}
	return extent_writepages(tree, mapping, btree_get_extent, wbc);
}

int btree_readpage(struct file *file, struct page *page)
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

	if (page_count(page) > 3) {
		/* once for page->private, once for the caller, once
		 * once for the page cache
		 */
		return 0;
	}
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	map = &BTRFS_I(page->mapping->host)->extent_tree;
	ret = try_release_extent_state(map, tree, page, gfp_flags);
	if (ret == 1) {
		invalidate_extent_lru(tree, page_offset(page), PAGE_CACHE_SIZE);
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
		invalidate_extent_lru(tree, page_offset(page), PAGE_CACHE_SIZE);
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

int readahead_tree_block(struct btrfs_root *root, u64 bytenr, u32 blocksize)
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

static int close_all_devices(struct btrfs_fs_info *fs_info)
{
	struct list_head *list;
	struct list_head *next;
	struct btrfs_device *device;

	list = &fs_info->fs_devices->devices;
	list_for_each(next, list) {
		device = list_entry(next, struct btrfs_device, dev_list);
		if (device->bdev && device->bdev != fs_info->sb->s_bdev)
			close_bdev_excl(device->bdev);
		device->bdev = NULL;
	}
	return 0;
}

int btrfs_verify_block_csum(struct btrfs_root *root,
			    struct extent_buffer *buf)
{
	return btrfs_buffer_uptodate(buf);
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


struct extent_buffer *read_tree_block(struct btrfs_root *root, u64 bytenr,
				      u32 blocksize)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_io_tree *io_tree;
	int ret;

	io_tree = &BTRFS_I(btree_inode)->io_tree;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return NULL;

	ret = btree_read_extent_buffer_pages(root, buf, 0);

	if (ret == 0) {
		buf->flags |= EXTENT_UPTODATE;
	}
	return buf;

}

int clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct extent_buffer *buf)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	if (btrfs_header_generation(buf) ==
	    root->fs_info->running_transaction->transid)
		clear_extent_buffer_dirty(&BTRFS_I(btree_inode)->io_tree,
					  buf);
	return 0;
}

int wait_on_tree_block_writeback(struct btrfs_root *root,
				 struct extent_buffer *buf)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	wait_on_extent_buffer_writeback(&BTRFS_I(btree_inode)->io_tree,
					buf);
	return 0;
}

static int __setup_root(u32 nodesize, u32 leafsize, u32 sectorsize,
			u32 stripesize, struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid)
{
	root->node = NULL;
	root->inode = NULL;
	root->commit_root = NULL;
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
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	memset(&root->root_kobj, 0, sizeof(root->root_kobj));
	init_completion(&root->kobj_unregister);
	root->defrag_running = 0;
	root->defrag_level = 0;
	root->root_key.objectid = objectid;
	return 0;
}

static int find_and_setup_root(struct btrfs_root *tree_root,
			       struct btrfs_fs_info *fs_info,
			       u64 objectid,
			       struct btrfs_root *root)
{
	int ret;
	u32 blocksize;

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, objectid);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	BUG_ON(ret);

	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize);
	BUG_ON(!root->node);
	return 0;
}

struct btrfs_root *btrfs_read_fs_root_no_radix(struct btrfs_fs_info *fs_info,
					       struct btrfs_key *location)
{
	struct btrfs_root *root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path;
	struct extent_buffer *l;
	u64 highest_inode;
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
	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize);
	BUG_ON(!root->node);
insert:
	root->ref_cows = 1;
	ret = btrfs_find_highest_inode(root, &highest_inode);
	if (ret == 0) {
		root->highest_inode = highest_inode;
		root->last_inode_alloc = highest_inode;
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

	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)location->objectid);
	if (root)
		return root;

	root = btrfs_read_fs_root_no_radix(fs_info, location);
	if (IS_ERR(root))
		return root;
	ret = radix_tree_insert(&fs_info->fs_roots_radix,
				(unsigned long)root->root_key.objectid,
				root);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root);
		return ERR_PTR(ret);
	}
	ret = btrfs_find_dead_roots(fs_info->tree_root,
				    root->root_key.objectid, root);
	BUG_ON(ret);

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

	ret = btrfs_sysfs_add_root(root);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root->name);
		kfree(root);
		return ERR_PTR(ret);
	}
	root->in_sysfs = 1;
	return root;
}
#if 0
static int add_hasher(struct btrfs_fs_info *info, char *type) {
	struct btrfs_hasher *hasher;

	hasher = kmalloc(sizeof(*hasher), GFP_NOFS);
	if (!hasher)
		return -ENOMEM;
	hasher->hash_tfm = crypto_alloc_hash(type, 0, CRYPTO_ALG_ASYNC);
	if (!hasher->hash_tfm) {
		kfree(hasher);
		return -EINVAL;
	}
	spin_lock(&info->hash_lock);
	list_add(&hasher->list, &info->hashers);
	spin_unlock(&info->hash_lock);
	return 0;
}
#endif

static int btrfs_congested_fn(void *congested_data, int bdi_bits)
{
	struct btrfs_fs_info *info = (struct btrfs_fs_info *)congested_data;
	int ret = 0;
	struct list_head *cur;
	struct btrfs_device *device;
	struct backing_dev_info *bdi;

	list_for_each(cur, &info->fs_devices->devices) {
		device = list_entry(cur, struct btrfs_device, dev_list);
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
		bdi = blk_get_backing_dev_info(device->bdev);
		if (bdi->unplug_io_fn) {
			bdi->unplug_io_fn(bdi, page);
		}
	}
}

void btrfs_unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
	struct inode *inode;
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct address_space *mapping;
	u64 offset;

	/* the generic O_DIRECT read code does this */
	if (!page) {
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
	offset = page_offset(page);

	em_tree = &BTRFS_I(inode)->extent_tree;
	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, offset, PAGE_CACHE_SIZE);
	spin_unlock(&em_tree->lock);
	if (!em)
		return;

	offset = offset - em->start;
	btrfs_unplug_page(&BTRFS_I(inode)->root->fs_info->mapping_tree,
			  em->block_start + offset, page);
	free_extent_map(em);
}

static int setup_bdi(struct btrfs_fs_info *info, struct backing_dev_info *bdi)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
	bdi_init(bdi);
#endif
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

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
static void btrfs_end_io_csum(void *p)
#else
static void btrfs_end_io_csum(struct work_struct *work)
#endif
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
	struct btrfs_fs_info *fs_info = p;
#else
	struct btrfs_fs_info *fs_info = container_of(work,
						     struct btrfs_fs_info,
						     end_io_work);
#endif
	unsigned long flags;
	struct end_io_wq *end_io_wq;
	struct bio *bio;
	struct list_head *next;
	int error;
	int was_empty;

	while(1) {
		spin_lock_irqsave(&fs_info->end_io_work_lock, flags);
		if (list_empty(&fs_info->end_io_work_list)) {
			spin_unlock_irqrestore(&fs_info->end_io_work_lock,
					       flags);
			return;
		}
		next = fs_info->end_io_work_list.next;
		list_del(next);
		spin_unlock_irqrestore(&fs_info->end_io_work_lock, flags);

		end_io_wq = list_entry(next, struct end_io_wq, list);

		bio = end_io_wq->bio;
		if (end_io_wq->metadata && !bio_ready_for_csum(bio)) {
			spin_lock_irqsave(&fs_info->end_io_work_lock, flags);
			was_empty = list_empty(&fs_info->end_io_work_list);
			list_add_tail(&end_io_wq->list,
				      &fs_info->end_io_work_list);
			spin_unlock_irqrestore(&fs_info->end_io_work_lock,
					       flags);
			if (was_empty)
				return;
			continue;
		}
		error = end_io_wq->error;
		bio->bi_private = end_io_wq->private;
		bio->bi_end_io = end_io_wq->end_io;
		kfree(end_io_wq);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)
		bio_endio(bio, bio->bi_size, error);
#else
		bio_endio(bio, error);
#endif
	}
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
static void btrfs_async_submit_work(void *p)
#else
static void btrfs_async_submit_work(struct work_struct *work)
#endif
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
	struct btrfs_fs_info *fs_info = p;
#else
	struct btrfs_fs_info *fs_info = container_of(work,
						     struct btrfs_fs_info,
						     async_submit_work);
#endif
	struct async_submit_bio *async;
	struct list_head *next;

	while(1) {
		spin_lock(&fs_info->async_submit_work_lock);
		if (list_empty(&fs_info->async_submit_work_list)) {
			spin_unlock(&fs_info->async_submit_work_lock);
			return;
		}
		next = fs_info->async_submit_work_list.next;
		list_del(next);
		spin_unlock(&fs_info->async_submit_work_lock);

		async = list_entry(next, struct async_submit_bio, list);
		async->submit_bio_hook(async->inode, async->rw, async->bio,
				       async->mirror_num);
		kfree(async);
	}
}

struct btrfs_root *open_ctree(struct super_block *sb,
			      struct btrfs_fs_devices *fs_devices)
{
	u32 sectorsize;
	u32 nodesize;
	u32 leafsize;
	u32 blocksize;
	u32 stripesize;
	struct btrfs_root *extent_root = kmalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *tree_root = kmalloc(sizeof(struct btrfs_root),
					       GFP_NOFS);
	struct btrfs_fs_info *fs_info = kzalloc(sizeof(*fs_info),
						GFP_NOFS);
	struct btrfs_root *chunk_root = kmalloc(sizeof(struct btrfs_root),
						GFP_NOFS);
	struct btrfs_root *dev_root = kmalloc(sizeof(struct btrfs_root),
					      GFP_NOFS);
	int ret;
	int err = -EINVAL;
	struct btrfs_super_block *disk_super;

	if (!extent_root || !tree_root || !fs_info) {
		err = -ENOMEM;
		goto fail;
	}
	end_io_workqueue = create_workqueue("btrfs-end-io");
	BUG_ON(!end_io_workqueue);
	async_submit_workqueue = create_workqueue("btrfs-async-submit");

	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_NOFS);
	INIT_LIST_HEAD(&fs_info->trans_list);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	INIT_LIST_HEAD(&fs_info->hashers);
	INIT_LIST_HEAD(&fs_info->end_io_work_list);
	INIT_LIST_HEAD(&fs_info->async_submit_work_list);
	spin_lock_init(&fs_info->hash_lock);
	spin_lock_init(&fs_info->end_io_work_lock);
	spin_lock_init(&fs_info->async_submit_work_lock);
	spin_lock_init(&fs_info->delalloc_lock);
	spin_lock_init(&fs_info->new_trans_lock);

	init_completion(&fs_info->kobj_unregister);
	sb_set_blocksize(sb, BTRFS_SUPER_INFO_SIZE);
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->chunk_root = chunk_root;
	fs_info->dev_root = dev_root;
	fs_info->fs_devices = fs_devices;
	INIT_LIST_HEAD(&fs_info->dirty_cowonly_roots);
	INIT_LIST_HEAD(&fs_info->space_info);
	btrfs_mapping_init(&fs_info->mapping_tree);
	fs_info->sb = sb;
	fs_info->max_extent = (u64)-1;
	fs_info->max_inline = 8192 * 1024;
	setup_bdi(fs_info, &fs_info->bdi);
	fs_info->btree_inode = new_inode(sb);
	fs_info->btree_inode->i_ino = 1;
	fs_info->btree_inode->i_nlink = 1;

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

	extent_io_tree_init(&fs_info->free_space_cache,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&fs_info->block_group_cache,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&fs_info->pinned_extents,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&fs_info->pending_del,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&fs_info->extent_ins,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	fs_info->do_barriers = 1;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
	INIT_WORK(&fs_info->end_io_work, btrfs_end_io_csum, fs_info);
	INIT_WORK(&fs_info->async_submit_work, btrfs_async_submit_work,
		  fs_info);
	INIT_WORK(&fs_info->trans_work, btrfs_transaction_cleaner, fs_info);
#else
	INIT_WORK(&fs_info->end_io_work, btrfs_end_io_csum);
	INIT_WORK(&fs_info->async_submit_work, btrfs_async_submit_work);
	INIT_DELAYED_WORK(&fs_info->trans_work, btrfs_transaction_cleaner);
#endif
	BTRFS_I(fs_info->btree_inode)->root = tree_root;
	memset(&BTRFS_I(fs_info->btree_inode)->location, 0,
	       sizeof(struct btrfs_key));
	insert_inode_hash(fs_info->btree_inode);
	mapping_set_gfp_mask(fs_info->btree_inode->i_mapping, GFP_NOFS);

	mutex_init(&fs_info->trans_mutex);
	mutex_init(&fs_info->fs_mutex);

#if 0
	ret = add_hasher(fs_info, "crc32c");
	if (ret) {
		printk("btrfs: failed hash setup, modprobe cryptomgr?\n");
		err = -ENOMEM;
		goto fail_iput;
	}
#endif
	__setup_root(4096, 4096, 4096, 4096, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);

	fs_info->sb_buffer = read_tree_block(tree_root,
					     BTRFS_SUPER_INFO_OFFSET,
					     4096);

	if (!fs_info->sb_buffer)
		goto fail_iput;

	read_extent_buffer(fs_info->sb_buffer, &fs_info->super_copy, 0,
			   sizeof(fs_info->super_copy));

	read_extent_buffer(fs_info->sb_buffer, fs_info->fsid,
			   (unsigned long)btrfs_super_fsid(fs_info->sb_buffer),
			   BTRFS_FSID_SIZE);

	disk_super = &fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		goto fail_sb_buffer;

	if (btrfs_super_num_devices(disk_super) != fs_devices->num_devices) {
		printk("Btrfs: wanted %llu devices, but found %llu\n",
		       (unsigned long long)btrfs_super_num_devices(disk_super),
		       (unsigned long long)fs_devices->num_devices);
		goto fail_sb_buffer;
	}
	fs_info->bdi.ra_pages *= btrfs_super_num_devices(disk_super);

	nodesize = btrfs_super_nodesize(disk_super);
	leafsize = btrfs_super_leafsize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	stripesize = btrfs_super_stripesize(disk_super);
	tree_root->nodesize = nodesize;
	tree_root->leafsize = leafsize;
	tree_root->sectorsize = sectorsize;
	tree_root->stripesize = stripesize;
	sb_set_blocksize(sb, sectorsize);

	if (strncmp((char *)(&disk_super->magic), BTRFS_MAGIC,
		    sizeof(disk_super->magic))) {
		printk("btrfs: valid FS not found on %s\n", sb->s_id);
		goto fail_sb_buffer;
	}

	mutex_lock(&fs_info->fs_mutex);

	ret = btrfs_read_sys_array(tree_root);
	if (ret) {
		printk("btrfs: failed to read the system array on %s\n",
		       sb->s_id);
		goto fail_sys_array;
	}

	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_chunk_root_level(disk_super));

	__setup_root(nodesize, leafsize, sectorsize, stripesize,
		     chunk_root, fs_info, BTRFS_CHUNK_TREE_OBJECTID);

	chunk_root->node = read_tree_block(chunk_root,
					   btrfs_super_chunk_root(disk_super),
					   blocksize);
	BUG_ON(!chunk_root->node);

	read_extent_buffer(chunk_root->node, fs_info->chunk_tree_uuid,
	         (unsigned long)btrfs_header_chunk_tree_uuid(chunk_root->node),
		 BTRFS_UUID_SIZE);

	ret = btrfs_read_chunk_tree(chunk_root);
	BUG_ON(ret);

	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_root_level(disk_super));


	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super),
					  blocksize);
	if (!tree_root->node)
		goto fail_sb_buffer;


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

	btrfs_read_block_groups(extent_root);

	fs_info->generation = btrfs_super_generation(disk_super) + 1;
	fs_info->data_alloc_profile = (u64)-1;
	fs_info->metadata_alloc_profile = (u64)-1;
	fs_info->system_alloc_profile = fs_info->metadata_alloc_profile;

	mutex_unlock(&fs_info->fs_mutex);
	return tree_root;

fail_extent_root:
	free_extent_buffer(extent_root->node);
fail_tree_root:
	free_extent_buffer(tree_root->node);
fail_sys_array:
	mutex_unlock(&fs_info->fs_mutex);
fail_sb_buffer:
	free_extent_buffer(fs_info->sb_buffer);
	extent_io_tree_empty_lru(&BTRFS_I(fs_info->btree_inode)->io_tree);
fail_iput:
	iput(fs_info->btree_inode);
fail:
	close_all_devices(fs_info);
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

	kfree(extent_root);
	kfree(tree_root);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
	bdi_destroy(&fs_info->bdi);
#endif
	kfree(fs_info);
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
		set_buffer_write_io_error(bh);
		clear_buffer_uptodate(bh);
	}
	unlock_buffer(bh);
	put_bh(bh);
}

int write_all_supers(struct btrfs_root *root)
{
	struct list_head *cur;
	struct list_head *head = &root->fs_info->fs_devices->devices;
	struct btrfs_device *dev;
	struct extent_buffer *sb;
	struct btrfs_dev_item *dev_item;
	struct buffer_head *bh;
	int ret;
	int do_barriers;

	do_barriers = !btrfs_test_opt(root, NOBARRIER);

	sb = root->fs_info->sb_buffer;
	dev_item = (struct btrfs_dev_item *)offsetof(struct btrfs_super_block,
						      dev_item);
	list_for_each(cur, head) {
		dev = list_entry(cur, struct btrfs_device, dev_list);
		btrfs_set_device_type(sb, dev_item, dev->type);
		btrfs_set_device_id(sb, dev_item, dev->devid);
		btrfs_set_device_total_bytes(sb, dev_item, dev->total_bytes);
		btrfs_set_device_bytes_used(sb, dev_item, dev->bytes_used);
		btrfs_set_device_io_align(sb, dev_item, dev->io_align);
		btrfs_set_device_io_width(sb, dev_item, dev->io_width);
		btrfs_set_device_sector_size(sb, dev_item, dev->sector_size);
		write_extent_buffer(sb, dev->uuid,
				    (unsigned long)btrfs_device_uuid(dev_item),
				    BTRFS_UUID_SIZE);

		btrfs_set_header_flag(sb, BTRFS_HEADER_FLAG_WRITTEN);
		csum_tree_block(root, sb, 0);

		bh = __getblk(dev->bdev, BTRFS_SUPER_INFO_OFFSET /
			      root->fs_info->sb->s_blocksize,
			      BTRFS_SUPER_INFO_SIZE);

		read_extent_buffer(sb, bh->b_data, 0, BTRFS_SUPER_INFO_SIZE);
		dev->pending_io = bh;

		get_bh(bh);
		set_buffer_uptodate(bh);
		lock_buffer(bh);
		bh->b_end_io = btrfs_end_buffer_write_sync;

		if (do_barriers && dev->barriers) {
			ret = submit_bh(WRITE_BARRIER, bh);
			if (ret == -EOPNOTSUPP) {
				printk("btrfs: disabling barriers on dev %s\n",
				       dev->name);
				set_buffer_uptodate(bh);
				dev->barriers = 0;
				get_bh(bh);
				lock_buffer(bh);
				ret = submit_bh(WRITE, bh);
			}
		} else {
			ret = submit_bh(WRITE, bh);
		}
		BUG_ON(ret);
	}

	list_for_each(cur, head) {
		dev = list_entry(cur, struct btrfs_device, dev_list);
		BUG_ON(!dev->pending_io);
		bh = dev->pending_io;
		wait_on_buffer(bh);
		if (!buffer_uptodate(dev->pending_io)) {
			if (do_barriers && dev->barriers) {
				printk("btrfs: disabling barriers on dev %s\n",
				       dev->name);
				set_buffer_uptodate(bh);
				get_bh(bh);
				lock_buffer(bh);
				dev->barriers = 0;
				ret = submit_bh(WRITE, bh);
				BUG_ON(ret);
				wait_on_buffer(bh);
				BUG_ON(!buffer_uptodate(bh));
			} else {
				BUG();
			}

		}
		dev->pending_io = NULL;
		brelse(bh);
	}
	return 0;
}

int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root)
{
	int ret;

	ret = write_all_supers(root);
#if 0
	if (!btrfs_test_opt(root, NOBARRIER))
		blkdev_issue_flush(sb->s_bdev, NULL);
	set_extent_buffer_dirty(&BTRFS_I(btree_inode)->io_tree, super);
	ret = sync_page_range_nolock(btree_inode, btree_inode->i_mapping,
				     super->start, super->len);
	if (!btrfs_test_opt(root, NOBARRIER))
		blkdev_issue_flush(sb->s_bdev, NULL);
#endif
	return ret;
}

int btrfs_free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)root->root_key.objectid);
	if (root->in_sysfs)
		btrfs_sysfs_del_root(root);
	if (root->inode)
		iput(root->inode);
	if (root->node)
		free_extent_buffer(root->node);
	if (root->commit_root)
		free_extent_buffer(root->commit_root);
	if (root->name)
		kfree(root->name);
	kfree(root);
	return 0;
}

static int del_fs_roots(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *gang[8];
	int i;

	while(1) {
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

int close_ctree(struct btrfs_root *root)
{
	int ret;
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = root->fs_info;

	fs_info->closing = 1;
	btrfs_transaction_flush_work(root);
	mutex_lock(&fs_info->fs_mutex);
	btrfs_defrag_dirty_roots(root->fs_info);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	/* run commit again to  drop the original snapshot */
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	ret = btrfs_write_and_wait_transaction(NULL, root);
	BUG_ON(ret);
	write_ctree_super(NULL, root);
	mutex_unlock(&fs_info->fs_mutex);

	btrfs_transaction_flush_work(root);

	if (fs_info->delalloc_bytes) {
		printk("btrfs: at unmount delalloc count %Lu\n",
		       fs_info->delalloc_bytes);
	}
	if (fs_info->extent_root->node)
		free_extent_buffer(fs_info->extent_root->node);

	if (fs_info->tree_root->node)
		free_extent_buffer(fs_info->tree_root->node);

	if (root->fs_info->chunk_root->node);
		free_extent_buffer(root->fs_info->chunk_root->node);

	if (root->fs_info->dev_root->node);
		free_extent_buffer(root->fs_info->dev_root->node);

	free_extent_buffer(fs_info->sb_buffer);

	btrfs_free_block_groups(root->fs_info);
	del_fs_roots(fs_info);

	filemap_write_and_wait(fs_info->btree_inode->i_mapping);

	extent_io_tree_empty_lru(&fs_info->free_space_cache);
	extent_io_tree_empty_lru(&fs_info->block_group_cache);
	extent_io_tree_empty_lru(&fs_info->pinned_extents);
	extent_io_tree_empty_lru(&fs_info->pending_del);
	extent_io_tree_empty_lru(&fs_info->extent_ins);
	extent_io_tree_empty_lru(&BTRFS_I(fs_info->btree_inode)->io_tree);

	flush_workqueue(end_io_workqueue);
	flush_workqueue(async_submit_workqueue);

	truncate_inode_pages(fs_info->btree_inode->i_mapping, 0);

	flush_workqueue(end_io_workqueue);
	destroy_workqueue(end_io_workqueue);

	flush_workqueue(async_submit_workqueue);
	destroy_workqueue(async_submit_workqueue);

	iput(fs_info->btree_inode);
#if 0
	while(!list_empty(&fs_info->hashers)) {
		struct btrfs_hasher *hasher;
		hasher = list_entry(fs_info->hashers.next, struct btrfs_hasher,
				    hashers);
		list_del(&hasher->hashers);
		crypto_free_hash(&fs_info->hash_tfm);
		kfree(hasher);
	}
#endif
	close_all_devices(fs_info);
	btrfs_mapping_tree_free(&fs_info->mapping_tree);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
	bdi_destroy(&fs_info->bdi);
#endif

	kfree(fs_info->extent_root);
	kfree(fs_info->tree_root);
	kfree(fs_info->chunk_root);
	kfree(fs_info->dev_root);
	return 0;
}

int btrfs_buffer_uptodate(struct extent_buffer *buf)
{
	struct inode *btree_inode = buf->first_page->mapping->host;
	return extent_buffer_uptodate(&BTRFS_I(btree_inode)->io_tree, buf);
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

	if (transid != root->fs_info->generation) {
		printk(KERN_CRIT "transid mismatch buffer %llu, found %Lu running %Lu\n",
			(unsigned long long)buf->start,
			transid, root->fs_info->generation);
		WARN_ON(1);
	}
	set_extent_buffer_dirty(&BTRFS_I(btree_inode)->io_tree, buf);
}

void btrfs_throttle(struct btrfs_root *root)
{
	struct backing_dev_info *bdi;

	bdi = root->fs_info->sb->s_bdev->bd_inode->i_mapping->backing_dev_info;
	if (root->fs_info->throttles && bdi_write_congested(bdi)) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)
		congestion_wait(WRITE, HZ/20);
#else
		blk_congestion_wait(WRITE, HZ/20);
#endif
	}
}

void btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr)
{
	balance_dirty_pages_ratelimited_nr(
				   root->fs_info->btree_inode->i_mapping, 1);
}

void btrfs_set_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_bits(&BTRFS_I(btree_inode)->io_tree, buf->start,
			buf->start + buf->len - 1, EXTENT_DEFRAG, GFP_NOFS);
}

void btrfs_set_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_bits(&BTRFS_I(btree_inode)->io_tree, buf->start,
			buf->start + buf->len - 1, EXTENT_DEFRAG_DONE,
			GFP_NOFS);
}

int btrfs_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return test_range_bit(&BTRFS_I(btree_inode)->io_tree,
		     buf->start, buf->start + buf->len - 1, EXTENT_DEFRAG, 0);
}

int btrfs_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return test_range_bit(&BTRFS_I(btree_inode)->io_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG_DONE, 0);
}

int btrfs_clear_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return clear_extent_bits(&BTRFS_I(btree_inode)->io_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG_DONE, GFP_NOFS);
}

int btrfs_clear_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return clear_extent_bits(&BTRFS_I(btree_inode)->io_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG, GFP_NOFS);
}

int btrfs_read_buffer(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	int ret;
	ret = btree_read_extent_buffer_pages(root, buf, 0);
	if (ret == 0) {
		buf->flags |= EXTENT_UPTODATE;
	}
	return ret;
}

static struct extent_io_ops btree_extent_io_ops = {
	.writepage_io_hook = btree_writepage_io_hook,
	.readpage_end_io_hook = btree_readpage_end_io_hook,
	.submit_bio_hook = btree_submit_bio_hook,
	/* note we're sharing with inode.c for the merge bio hook */
	.merge_bio_hook = btrfs_merge_bio_hook,
};
