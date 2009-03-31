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
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/bit_spinlock.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/falloc.h>
#include "compat.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "volumes.h"
#include "ordered-data.h"
#include "xattr.h"
#include "tree-log.h"
#include "ref-cache.h"
#include "compression.h"
#include "locking.h"

struct btrfs_iget_args {
	u64 ino;
	struct btrfs_root *root;
};

static struct inode_operations btrfs_dir_inode_operations;
static struct inode_operations btrfs_symlink_inode_operations;
static struct inode_operations btrfs_dir_ro_inode_operations;
static struct inode_operations btrfs_special_inode_operations;
static struct inode_operations btrfs_file_inode_operations;
static struct address_space_operations btrfs_aops;
static struct address_space_operations btrfs_symlink_aops;
static struct file_operations btrfs_dir_file_operations;
static struct extent_io_ops btrfs_extent_io_ops;

static struct kmem_cache *btrfs_inode_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_transaction_cachep;
struct kmem_cache *btrfs_bit_radix_cachep;
struct kmem_cache *btrfs_path_cachep;

#define S_SHIFT 12
static unsigned char btrfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= BTRFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= BTRFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= BTRFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= BTRFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= BTRFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= BTRFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= BTRFS_FT_SYMLINK,
};

static void btrfs_truncate(struct inode *inode);
static int btrfs_finish_ordered_io(struct inode *inode, u64 start, u64 end);
static noinline int cow_file_range(struct inode *inode,
				   struct page *locked_page,
				   u64 start, u64 end, int *page_started,
				   unsigned long *nr_written, int unlock);

static int btrfs_init_inode_security(struct inode *inode,  struct inode *dir)
{
	int err;

	err = btrfs_init_acl(inode, dir);
	if (!err)
		err = btrfs_xattr_security_init(inode, dir);
	return err;
}

/*
 * this does all the hard work for inserting an inline extent into
 * the btree.  The caller should have done a btrfs_drop_extents so that
 * no overlapping inline items exist in the btree
 */
static noinline int insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct inode *inode,
				u64 start, size_t size, size_t compressed_size,
				struct page **compressed_pages)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct page *page = NULL;
	char *kaddr;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	int err = 0;
	int ret;
	size_t cur_size = size;
	size_t datasize;
	unsigned long offset;
	int use_compress = 0;

	if (compressed_size && compressed_pages) {
		use_compress = 1;
		cur_size = compressed_size;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	btrfs_set_trans_block_group(trans, inode);

	key.objectid = inode->i_ino;
	key.offset = start;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
	datasize = btrfs_file_extent_calc_inline_size(cur_size);

	inode_add_bytes(inode, size);
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	BUG_ON(ret);
	if (ret) {
		err = ret;
		goto fail;
	}
	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(leaf, ei, trans->transid);
	btrfs_set_file_extent_type(leaf, ei, BTRFS_FILE_EXTENT_INLINE);
	btrfs_set_file_extent_encryption(leaf, ei, 0);
	btrfs_set_file_extent_other_encoding(leaf, ei, 0);
	btrfs_set_file_extent_ram_bytes(leaf, ei, size);
	ptr = btrfs_file_extent_inline_start(ei);

	if (use_compress) {
		struct page *cpage;
		int i = 0;
		while (compressed_size > 0) {
			cpage = compressed_pages[i];
			cur_size = min_t(unsigned long, compressed_size,
				       PAGE_CACHE_SIZE);

			kaddr = kmap(cpage);
			write_extent_buffer(leaf, kaddr, ptr, cur_size);
			kunmap(cpage);

			i++;
			ptr += cur_size;
			compressed_size -= cur_size;
		}
		btrfs_set_file_extent_compression(leaf, ei,
						  BTRFS_COMPRESS_ZLIB);
	} else {
		page = find_get_page(inode->i_mapping,
				     start >> PAGE_CACHE_SHIFT);
		btrfs_set_file_extent_compression(leaf, ei, 0);
		kaddr = kmap_atomic(page, KM_USER0);
		offset = start & (PAGE_CACHE_SIZE - 1);
		write_extent_buffer(leaf, kaddr + offset, ptr, size);
		kunmap_atomic(kaddr, KM_USER0);
		page_cache_release(page);
	}
	btrfs_mark_buffer_dirty(leaf);
	btrfs_free_path(path);

	BTRFS_I(inode)->disk_i_size = inode->i_size;
	btrfs_update_inode(trans, root, inode);
	return 0;
fail:
	btrfs_free_path(path);
	return err;
}


/*
 * conditionally insert an inline extent into the file.  This
 * does the checks required to make sure the data is small enough
 * to fit as an inline extent.
 */
static int cow_file_range_inline(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct inode *inode, u64 start, u64 end,
				 size_t compressed_size,
				 struct page **compressed_pages)
{
	u64 isize = i_size_read(inode);
	u64 actual_end = min(end + 1, isize);
	u64 inline_len = actual_end - start;
	u64 aligned_end = (end + root->sectorsize - 1) &
			~((u64)root->sectorsize - 1);
	u64 hint_byte;
	u64 data_len = inline_len;
	int ret;

	if (compressed_size)
		data_len = compressed_size;

	if (start > 0 ||
	    actual_end >= PAGE_CACHE_SIZE ||
	    data_len >= BTRFS_MAX_INLINE_DATA_SIZE(root) ||
	    (!compressed_size &&
	    (actual_end & (root->sectorsize - 1)) == 0) ||
	    end + 1 < isize ||
	    data_len > root->fs_info->max_inline) {
		return 1;
	}

	ret = btrfs_drop_extents(trans, root, inode, start,
				 aligned_end, start, &hint_byte);
	BUG_ON(ret);

	if (isize > actual_end)
		inline_len = min_t(u64, isize, actual_end);
	ret = insert_inline_extent(trans, root, inode, start,
				   inline_len, compressed_size,
				   compressed_pages);
	BUG_ON(ret);
	btrfs_drop_extent_cache(inode, start, aligned_end, 0);
	return 0;
}

struct async_extent {
	u64 start;
	u64 ram_size;
	u64 compressed_size;
	struct page **pages;
	unsigned long nr_pages;
	struct list_head list;
};

struct async_cow {
	struct inode *inode;
	struct btrfs_root *root;
	struct page *locked_page;
	u64 start;
	u64 end;
	struct list_head extents;
	struct btrfs_work work;
};

static noinline int add_async_extent(struct async_cow *cow,
				     u64 start, u64 ram_size,
				     u64 compressed_size,
				     struct page **pages,
				     unsigned long nr_pages)
{
	struct async_extent *async_extent;

	async_extent = kmalloc(sizeof(*async_extent), GFP_NOFS);
	async_extent->start = start;
	async_extent->ram_size = ram_size;
	async_extent->compressed_size = compressed_size;
	async_extent->pages = pages;
	async_extent->nr_pages = nr_pages;
	list_add_tail(&async_extent->list, &cow->extents);
	return 0;
}

/*
 * we create compressed extents in two phases.  The first
 * phase compresses a range of pages that have already been
 * locked (both pages and state bits are locked).
 *
 * This is done inside an ordered work queue, and the compression
 * is spread across many cpus.  The actual IO submission is step
 * two, and the ordered work queue takes care of making sure that
 * happens in the same order things were put onto the queue by
 * writepages and friends.
 *
 * If this code finds it can't get good compression, it puts an
 * entry onto the work queue to write the uncompressed bytes.  This
 * makes sure that both compressed inodes and uncompressed inodes
 * are written in the same order that pdflush sent them down.
 */
static noinline int compress_file_range(struct inode *inode,
					struct page *locked_page,
					u64 start, u64 end,
					struct async_cow *async_cow,
					int *num_added)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	u64 num_bytes;
	u64 orig_start;
	u64 disk_num_bytes;
	u64 blocksize = root->sectorsize;
	u64 actual_end;
	u64 isize = i_size_read(inode);
	int ret = 0;
	struct page **pages = NULL;
	unsigned long nr_pages;
	unsigned long nr_pages_ret = 0;
	unsigned long total_compressed = 0;
	unsigned long total_in = 0;
	unsigned long max_compressed = 128 * 1024;
	unsigned long max_uncompressed = 128 * 1024;
	int i;
	int will_compress;

	orig_start = start;

	actual_end = min_t(u64, isize, end + 1);
again:
	will_compress = 0;
	nr_pages = (end >> PAGE_CACHE_SHIFT) - (start >> PAGE_CACHE_SHIFT) + 1;
	nr_pages = min(nr_pages, (128 * 1024UL) / PAGE_CACHE_SIZE);

	/*
	 * we don't want to send crud past the end of i_size through
	 * compression, that's just a waste of CPU time.  So, if the
	 * end of the file is before the start of our current
	 * requested range of bytes, we bail out to the uncompressed
	 * cleanup code that can deal with all of this.
	 *
	 * It isn't really the fastest way to fix things, but this is a
	 * very uncommon corner.
	 */
	if (actual_end <= start)
		goto cleanup_and_bail_uncompressed;

	total_compressed = actual_end - start;

	/* we want to make sure that amount of ram required to uncompress
	 * an extent is reasonable, so we limit the total size in ram
	 * of a compressed extent to 128k.  This is a crucial number
	 * because it also controls how easily we can spread reads across
	 * cpus for decompression.
	 *
	 * We also want to make sure the amount of IO required to do
	 * a random read is reasonably small, so we limit the size of
	 * a compressed extent to 128k.
	 */
	total_compressed = min(total_compressed, max_uncompressed);
	num_bytes = (end - start + blocksize) & ~(blocksize - 1);
	num_bytes = max(blocksize,  num_bytes);
	disk_num_bytes = num_bytes;
	total_in = 0;
	ret = 0;

	/*
	 * we do compression for mount -o compress and when the
	 * inode has not been flagged as nocompress.  This flag can
	 * change at any time if we discover bad compression ratios.
	 */
	if (!btrfs_test_flag(inode, NOCOMPRESS) &&
	    btrfs_test_opt(root, COMPRESS)) {
		WARN_ON(pages);
		pages = kzalloc(sizeof(struct page *) * nr_pages, GFP_NOFS);

		ret = btrfs_zlib_compress_pages(inode->i_mapping, start,
						total_compressed, pages,
						nr_pages, &nr_pages_ret,
						&total_in,
						&total_compressed,
						max_compressed);

		if (!ret) {
			unsigned long offset = total_compressed &
				(PAGE_CACHE_SIZE - 1);
			struct page *page = pages[nr_pages_ret - 1];
			char *kaddr;

			/* zero the tail end of the last page, we might be
			 * sending it down to disk
			 */
			if (offset) {
				kaddr = kmap_atomic(page, KM_USER0);
				memset(kaddr + offset, 0,
				       PAGE_CACHE_SIZE - offset);
				kunmap_atomic(kaddr, KM_USER0);
			}
			will_compress = 1;
		}
	}
	if (start == 0) {
		trans = btrfs_join_transaction(root, 1);
		BUG_ON(!trans);
		btrfs_set_trans_block_group(trans, inode);

		/* lets try to make an inline extent */
		if (ret || total_in < (actual_end - start)) {
			/* we didn't compress the entire range, try
			 * to make an uncompressed inline extent.
			 */
			ret = cow_file_range_inline(trans, root, inode,
						    start, end, 0, NULL);
		} else {
			/* try making a compressed inline extent */
			ret = cow_file_range_inline(trans, root, inode,
						    start, end,
						    total_compressed, pages);
		}
		btrfs_end_transaction(trans, root);
		if (ret == 0) {
			/*
			 * inline extent creation worked, we don't need
			 * to create any more async work items.  Unlock
			 * and free up our temp pages.
			 */
			extent_clear_unlock_delalloc(inode,
						     &BTRFS_I(inode)->io_tree,
						     start, end, NULL, 1, 0,
						     0, 1, 1, 1);
			ret = 0;
			goto free_pages_out;
		}
	}

	if (will_compress) {
		/*
		 * we aren't doing an inline extent round the compressed size
		 * up to a block size boundary so the allocator does sane
		 * things
		 */
		total_compressed = (total_compressed + blocksize - 1) &
			~(blocksize - 1);

		/*
		 * one last check to make sure the compression is really a
		 * win, compare the page count read with the blocks on disk
		 */
		total_in = (total_in + PAGE_CACHE_SIZE - 1) &
			~(PAGE_CACHE_SIZE - 1);
		if (total_compressed >= total_in) {
			will_compress = 0;
		} else {
			disk_num_bytes = total_compressed;
			num_bytes = total_in;
		}
	}
	if (!will_compress && pages) {
		/*
		 * the compression code ran but failed to make things smaller,
		 * free any pages it allocated and our page pointer array
		 */
		for (i = 0; i < nr_pages_ret; i++) {
			WARN_ON(pages[i]->mapping);
			page_cache_release(pages[i]);
		}
		kfree(pages);
		pages = NULL;
		total_compressed = 0;
		nr_pages_ret = 0;

		/* flag the file so we don't compress in the future */
		btrfs_set_flag(inode, NOCOMPRESS);
	}
	if (will_compress) {
		*num_added += 1;

		/* the async work queues will take care of doing actual
		 * allocation on disk for these compressed pages,
		 * and will submit them to the elevator.
		 */
		add_async_extent(async_cow, start, num_bytes,
				 total_compressed, pages, nr_pages_ret);

		if (start + num_bytes < end && start + num_bytes < actual_end) {
			start += num_bytes;
			pages = NULL;
			cond_resched();
			goto again;
		}
	} else {
cleanup_and_bail_uncompressed:
		/*
		 * No compression, but we still need to write the pages in
		 * the file we've been given so far.  redirty the locked
		 * page if it corresponds to our extent and set things up
		 * for the async work queue to run cow_file_range to do
		 * the normal delalloc dance
		 */
		if (page_offset(locked_page) >= start &&
		    page_offset(locked_page) <= end) {
			__set_page_dirty_nobuffers(locked_page);
			/* unlocked later on in the async handlers */
		}
		add_async_extent(async_cow, start, end - start + 1, 0, NULL, 0);
		*num_added += 1;
	}

out:
	return 0;

free_pages_out:
	for (i = 0; i < nr_pages_ret; i++) {
		WARN_ON(pages[i]->mapping);
		page_cache_release(pages[i]);
	}
	kfree(pages);

	goto out;
}

/*
 * phase two of compressed writeback.  This is the ordered portion
 * of the code, which only gets called in the order the work was
 * queued.  We walk all the async extents created by compress_file_range
 * and send them down to the disk.
 */
static noinline int submit_compressed_extents(struct inode *inode,
					      struct async_cow *async_cow)
{
	struct async_extent *async_extent;
	u64 alloc_hint = 0;
	struct btrfs_trans_handle *trans;
	struct btrfs_key ins;
	struct extent_map *em;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_io_tree *io_tree;
	int ret;

	if (list_empty(&async_cow->extents))
		return 0;

	trans = btrfs_join_transaction(root, 1);

	while (!list_empty(&async_cow->extents)) {
		async_extent = list_entry(async_cow->extents.next,
					  struct async_extent, list);
		list_del(&async_extent->list);

		io_tree = &BTRFS_I(inode)->io_tree;

		/* did the compression code fall back to uncompressed IO? */
		if (!async_extent->pages) {
			int page_started = 0;
			unsigned long nr_written = 0;

			lock_extent(io_tree, async_extent->start,
				    async_extent->start +
				    async_extent->ram_size - 1, GFP_NOFS);

			/* allocate blocks */
			cow_file_range(inode, async_cow->locked_page,
				       async_extent->start,
				       async_extent->start +
				       async_extent->ram_size - 1,
				       &page_started, &nr_written, 0);

			/*
			 * if page_started, cow_file_range inserted an
			 * inline extent and took care of all the unlocking
			 * and IO for us.  Otherwise, we need to submit
			 * all those pages down to the drive.
			 */
			if (!page_started)
				extent_write_locked_range(io_tree,
						  inode, async_extent->start,
						  async_extent->start +
						  async_extent->ram_size - 1,
						  btrfs_get_extent,
						  WB_SYNC_ALL);
			kfree(async_extent);
			cond_resched();
			continue;
		}

		lock_extent(io_tree, async_extent->start,
			    async_extent->start + async_extent->ram_size - 1,
			    GFP_NOFS);
		/*
		 * here we're doing allocation and writeback of the
		 * compressed pages
		 */
		btrfs_drop_extent_cache(inode, async_extent->start,
					async_extent->start +
					async_extent->ram_size - 1, 0);

		ret = btrfs_reserve_extent(trans, root,
					   async_extent->compressed_size,
					   async_extent->compressed_size,
					   0, alloc_hint,
					   (u64)-1, &ins, 1);
		BUG_ON(ret);
		em = alloc_extent_map(GFP_NOFS);
		em->start = async_extent->start;
		em->len = async_extent->ram_size;
		em->orig_start = em->start;

		em->block_start = ins.objectid;
		em->block_len = ins.offset;
		em->bdev = root->fs_info->fs_devices->latest_bdev;
		set_bit(EXTENT_FLAG_PINNED, &em->flags);
		set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);

		while (1) {
			spin_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em);
			spin_unlock(&em_tree->lock);
			if (ret != -EEXIST) {
				free_extent_map(em);
				break;
			}
			btrfs_drop_extent_cache(inode, async_extent->start,
						async_extent->start +
						async_extent->ram_size - 1, 0);
		}

		ret = btrfs_add_ordered_extent(inode, async_extent->start,
					       ins.objectid,
					       async_extent->ram_size,
					       ins.offset,
					       BTRFS_ORDERED_COMPRESSED);
		BUG_ON(ret);

		btrfs_end_transaction(trans, root);

		/*
		 * clear dirty, set writeback and unlock the pages.
		 */
		extent_clear_unlock_delalloc(inode,
					     &BTRFS_I(inode)->io_tree,
					     async_extent->start,
					     async_extent->start +
					     async_extent->ram_size - 1,
					     NULL, 1, 1, 0, 1, 1, 0);

		ret = btrfs_submit_compressed_write(inode,
				    async_extent->start,
				    async_extent->ram_size,
				    ins.objectid,
				    ins.offset, async_extent->pages,
				    async_extent->nr_pages);

		BUG_ON(ret);
		trans = btrfs_join_transaction(root, 1);
		alloc_hint = ins.objectid + ins.offset;
		kfree(async_extent);
		cond_resched();
	}

	btrfs_end_transaction(trans, root);
	return 0;
}

/*
 * when extent_io.c finds a delayed allocation range in the file,
 * the call backs end up in this code.  The basic idea is to
 * allocate extents on disk for the range, and create ordered data structs
 * in ram to track those extents.
 *
 * locked_page is the page that writepage had locked already.  We use
 * it to make sure we don't do extra locks or unlocks.
 *
 * *page_started is set to one if we unlock locked_page and do everything
 * required to start IO on it.  It may be clean and already done with
 * IO when we return.
 */
static noinline int cow_file_range(struct inode *inode,
				   struct page *locked_page,
				   u64 start, u64 end, int *page_started,
				   unsigned long *nr_written,
				   int unlock)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	u64 alloc_hint = 0;
	u64 num_bytes;
	unsigned long ram_size;
	u64 disk_num_bytes;
	u64 cur_alloc_size;
	u64 blocksize = root->sectorsize;
	u64 actual_end;
	u64 isize = i_size_read(inode);
	struct btrfs_key ins;
	struct extent_map *em;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	int ret = 0;

	trans = btrfs_join_transaction(root, 1);
	BUG_ON(!trans);
	btrfs_set_trans_block_group(trans, inode);

	actual_end = min_t(u64, isize, end + 1);

	num_bytes = (end - start + blocksize) & ~(blocksize - 1);
	num_bytes = max(blocksize,  num_bytes);
	disk_num_bytes = num_bytes;
	ret = 0;

	if (start == 0) {
		/* lets try to make an inline extent */
		ret = cow_file_range_inline(trans, root, inode,
					    start, end, 0, NULL);
		if (ret == 0) {
			extent_clear_unlock_delalloc(inode,
						     &BTRFS_I(inode)->io_tree,
						     start, end, NULL, 1, 1,
						     1, 1, 1, 1);
			*nr_written = *nr_written +
			     (end - start + PAGE_CACHE_SIZE) / PAGE_CACHE_SIZE;
			*page_started = 1;
			ret = 0;
			goto out;
		}
	}

	BUG_ON(disk_num_bytes >
	       btrfs_super_total_bytes(&root->fs_info->super_copy));

	btrfs_drop_extent_cache(inode, start, start + num_bytes - 1, 0);

	while (disk_num_bytes > 0) {
		cur_alloc_size = min(disk_num_bytes, root->fs_info->max_extent);
		ret = btrfs_reserve_extent(trans, root, cur_alloc_size,
					   root->sectorsize, 0, alloc_hint,
					   (u64)-1, &ins, 1);
		BUG_ON(ret);

		em = alloc_extent_map(GFP_NOFS);
		em->start = start;
		em->orig_start = em->start;

		ram_size = ins.offset;
		em->len = ins.offset;

		em->block_start = ins.objectid;
		em->block_len = ins.offset;
		em->bdev = root->fs_info->fs_devices->latest_bdev;
		set_bit(EXTENT_FLAG_PINNED, &em->flags);

		while (1) {
			spin_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em);
			spin_unlock(&em_tree->lock);
			if (ret != -EEXIST) {
				free_extent_map(em);
				break;
			}
			btrfs_drop_extent_cache(inode, start,
						start + ram_size - 1, 0);
		}

		cur_alloc_size = ins.offset;
		ret = btrfs_add_ordered_extent(inode, start, ins.objectid,
					       ram_size, cur_alloc_size, 0);
		BUG_ON(ret);

		if (root->root_key.objectid ==
		    BTRFS_DATA_RELOC_TREE_OBJECTID) {
			ret = btrfs_reloc_clone_csums(inode, start,
						      cur_alloc_size);
			BUG_ON(ret);
		}

		if (disk_num_bytes < cur_alloc_size)
			break;

		/* we're not doing compressed IO, don't unlock the first
		 * page (which the caller expects to stay locked), don't
		 * clear any dirty bits and don't set any writeback bits
		 */
		extent_clear_unlock_delalloc(inode, &BTRFS_I(inode)->io_tree,
					     start, start + ram_size - 1,
					     locked_page, unlock, 1,
					     1, 0, 0, 0);
		disk_num_bytes -= cur_alloc_size;
		num_bytes -= cur_alloc_size;
		alloc_hint = ins.objectid + ins.offset;
		start += cur_alloc_size;
	}
out:
	ret = 0;
	btrfs_end_transaction(trans, root);

	return ret;
}

/*
 * work queue call back to started compression on a file and pages
 */
static noinline void async_cow_start(struct btrfs_work *work)
{
	struct async_cow *async_cow;
	int num_added = 0;
	async_cow = container_of(work, struct async_cow, work);

	compress_file_range(async_cow->inode, async_cow->locked_page,
			    async_cow->start, async_cow->end, async_cow,
			    &num_added);
	if (num_added == 0)
		async_cow->inode = NULL;
}

/*
 * work queue call back to submit previously compressed pages
 */
static noinline void async_cow_submit(struct btrfs_work *work)
{
	struct async_cow *async_cow;
	struct btrfs_root *root;
	unsigned long nr_pages;

	async_cow = container_of(work, struct async_cow, work);

	root = async_cow->root;
	nr_pages = (async_cow->end - async_cow->start + PAGE_CACHE_SIZE) >>
		PAGE_CACHE_SHIFT;

	atomic_sub(nr_pages, &root->fs_info->async_delalloc_pages);

	if (atomic_read(&root->fs_info->async_delalloc_pages) <
	    5 * 1042 * 1024 &&
	    waitqueue_active(&root->fs_info->async_submit_wait))
		wake_up(&root->fs_info->async_submit_wait);

	if (async_cow->inode)
		submit_compressed_extents(async_cow->inode, async_cow);
}

static noinline void async_cow_free(struct btrfs_work *work)
{
	struct async_cow *async_cow;
	async_cow = container_of(work, struct async_cow, work);
	kfree(async_cow);
}

static int cow_file_range_async(struct inode *inode, struct page *locked_page,
				u64 start, u64 end, int *page_started,
				unsigned long *nr_written)
{
	struct async_cow *async_cow;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	unsigned long nr_pages;
	u64 cur_end;
	int limit = 10 * 1024 * 1042;

	if (!btrfs_test_opt(root, COMPRESS)) {
		return cow_file_range(inode, locked_page, start, end,
				      page_started, nr_written, 1);
	}

	clear_extent_bit(&BTRFS_I(inode)->io_tree, start, end, EXTENT_LOCKED |
			 EXTENT_DELALLOC, 1, 0, GFP_NOFS);
	while (start < end) {
		async_cow = kmalloc(sizeof(*async_cow), GFP_NOFS);
		async_cow->inode = inode;
		async_cow->root = root;
		async_cow->locked_page = locked_page;
		async_cow->start = start;

		if (btrfs_test_flag(inode, NOCOMPRESS))
			cur_end = end;
		else
			cur_end = min(end, start + 512 * 1024 - 1);

		async_cow->end = cur_end;
		INIT_LIST_HEAD(&async_cow->extents);

		async_cow->work.func = async_cow_start;
		async_cow->work.ordered_func = async_cow_submit;
		async_cow->work.ordered_free = async_cow_free;
		async_cow->work.flags = 0;

		nr_pages = (cur_end - start + PAGE_CACHE_SIZE) >>
			PAGE_CACHE_SHIFT;
		atomic_add(nr_pages, &root->fs_info->async_delalloc_pages);

		btrfs_queue_worker(&root->fs_info->delalloc_workers,
				   &async_cow->work);

		if (atomic_read(&root->fs_info->async_delalloc_pages) > limit) {
			wait_event(root->fs_info->async_submit_wait,
			   (atomic_read(&root->fs_info->async_delalloc_pages) <
			    limit));
		}

		while (atomic_read(&root->fs_info->async_submit_draining) &&
		      atomic_read(&root->fs_info->async_delalloc_pages)) {
			wait_event(root->fs_info->async_submit_wait,
			  (atomic_read(&root->fs_info->async_delalloc_pages) ==
			   0));
		}

		*nr_written += nr_pages;
		start = cur_end + 1;
	}
	*page_started = 1;
	return 0;
}

static noinline int csum_exist_in_range(struct btrfs_root *root,
					u64 bytenr, u64 num_bytes)
{
	int ret;
	struct btrfs_ordered_sum *sums;
	LIST_HEAD(list);

	ret = btrfs_lookup_csums_range(root->fs_info->csum_root, bytenr,
				       bytenr + num_bytes - 1, &list);
	if (ret == 0 && list_empty(&list))
		return 0;

	while (!list_empty(&list)) {
		sums = list_entry(list.next, struct btrfs_ordered_sum, list);
		list_del(&sums->list);
		kfree(sums);
	}
	return 1;
}

/*
 * when nowcow writeback call back.  This checks for snapshots or COW copies
 * of the extents that exist in the file, and COWs the file as required.
 *
 * If no cow copies or snapshots exist, we write directly to the existing
 * blocks on disk
 */
static int run_delalloc_nocow(struct inode *inode, struct page *locked_page,
			      u64 start, u64 end, int *page_started, int force,
			      unsigned long *nr_written)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key found_key;
	u64 cow_start;
	u64 cur_offset;
	u64 extent_end;
	u64 disk_bytenr;
	u64 num_bytes;
	int extent_type;
	int ret;
	int type;
	int nocow;
	int check_prev = 1;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	trans = btrfs_join_transaction(root, 1);
	BUG_ON(!trans);

	cow_start = (u64)-1;
	cur_offset = start;
	while (1) {
		ret = btrfs_lookup_file_extent(trans, root, path, inode->i_ino,
					       cur_offset, 0);
		BUG_ON(ret < 0);
		if (ret > 0 && path->slots[0] > 0 && check_prev) {
			leaf = path->nodes[0];
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0] - 1);
			if (found_key.objectid == inode->i_ino &&
			    found_key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}
		check_prev = 0;
next_slot:
		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				BUG_ON(1);
			if (ret > 0)
				break;
			leaf = path->nodes[0];
		}

		nocow = 0;
		disk_bytenr = 0;
		num_bytes = 0;
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (found_key.objectid > inode->i_ino ||
		    found_key.type > BTRFS_EXTENT_DATA_KEY ||
		    found_key.offset > end)
			break;

		if (found_key.offset > cur_offset) {
			extent_end = found_key.offset;
			goto out_check;
		}

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(leaf, fi);

		if (extent_type == BTRFS_FILE_EXTENT_REG ||
		    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
			extent_end = found_key.offset +
				btrfs_file_extent_num_bytes(leaf, fi);
			if (extent_end <= start) {
				path->slots[0]++;
				goto next_slot;
			}
			if (disk_bytenr == 0)
				goto out_check;
			if (btrfs_file_extent_compression(leaf, fi) ||
			    btrfs_file_extent_encryption(leaf, fi) ||
			    btrfs_file_extent_other_encoding(leaf, fi))
				goto out_check;
			if (extent_type == BTRFS_FILE_EXTENT_REG && !force)
				goto out_check;
			if (btrfs_extent_readonly(root, disk_bytenr))
				goto out_check;
			if (btrfs_cross_ref_exist(trans, root, inode->i_ino,
						  disk_bytenr))
				goto out_check;
			disk_bytenr += btrfs_file_extent_offset(leaf, fi);
			disk_bytenr += cur_offset - found_key.offset;
			num_bytes = min(end + 1, extent_end) - cur_offset;
			/*
			 * force cow if csum exists in the range.
			 * this ensure that csum for a given extent are
			 * either valid or do not exist.
			 */
			if (csum_exist_in_range(root, disk_bytenr, num_bytes))
				goto out_check;
			nocow = 1;
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			extent_end = found_key.offset +
				btrfs_file_extent_inline_len(leaf, fi);
			extent_end = ALIGN(extent_end, root->sectorsize);
		} else {
			BUG_ON(1);
		}
out_check:
		if (extent_end <= start) {
			path->slots[0]++;
			goto next_slot;
		}
		if (!nocow) {
			if (cow_start == (u64)-1)
				cow_start = cur_offset;
			cur_offset = extent_end;
			if (cur_offset > end)
				break;
			path->slots[0]++;
			goto next_slot;
		}

		btrfs_release_path(root, path);
		if (cow_start != (u64)-1) {
			ret = cow_file_range(inode, locked_page, cow_start,
					found_key.offset - 1, page_started,
					nr_written, 1);
			BUG_ON(ret);
			cow_start = (u64)-1;
		}

		if (extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			struct extent_map *em;
			struct extent_map_tree *em_tree;
			em_tree = &BTRFS_I(inode)->extent_tree;
			em = alloc_extent_map(GFP_NOFS);
			em->start = cur_offset;
			em->orig_start = em->start;
			em->len = num_bytes;
			em->block_len = num_bytes;
			em->block_start = disk_bytenr;
			em->bdev = root->fs_info->fs_devices->latest_bdev;
			set_bit(EXTENT_FLAG_PINNED, &em->flags);
			while (1) {
				spin_lock(&em_tree->lock);
				ret = add_extent_mapping(em_tree, em);
				spin_unlock(&em_tree->lock);
				if (ret != -EEXIST) {
					free_extent_map(em);
					break;
				}
				btrfs_drop_extent_cache(inode, em->start,
						em->start + em->len - 1, 0);
			}
			type = BTRFS_ORDERED_PREALLOC;
		} else {
			type = BTRFS_ORDERED_NOCOW;
		}

		ret = btrfs_add_ordered_extent(inode, cur_offset, disk_bytenr,
					       num_bytes, num_bytes, type);
		BUG_ON(ret);

		extent_clear_unlock_delalloc(inode, &BTRFS_I(inode)->io_tree,
					cur_offset, cur_offset + num_bytes - 1,
					locked_page, 1, 1, 1, 0, 0, 0);
		cur_offset = extent_end;
		if (cur_offset > end)
			break;
	}
	btrfs_release_path(root, path);

	if (cur_offset <= end && cow_start == (u64)-1)
		cow_start = cur_offset;
	if (cow_start != (u64)-1) {
		ret = cow_file_range(inode, locked_page, cow_start, end,
				     page_started, nr_written, 1);
		BUG_ON(ret);
	}

	ret = btrfs_end_transaction(trans, root);
	BUG_ON(ret);
	btrfs_free_path(path);
	return 0;
}

/*
 * extent_io.c call back to do delayed allocation processing
 */
static int run_delalloc_range(struct inode *inode, struct page *locked_page,
			      u64 start, u64 end, int *page_started,
			      unsigned long *nr_written)
{
	int ret;

	if (btrfs_test_flag(inode, NODATACOW))
		ret = run_delalloc_nocow(inode, locked_page, start, end,
					 page_started, 1, nr_written);
	else if (btrfs_test_flag(inode, PREALLOC))
		ret = run_delalloc_nocow(inode, locked_page, start, end,
					 page_started, 0, nr_written);
	else
		ret = cow_file_range_async(inode, locked_page, start, end,
					   page_started, nr_written);

	return ret;
}

/*
 * extent_io.c set_bit_hook, used to track delayed allocation
 * bytes in this file, and to maintain the list of inodes that
 * have pending delalloc work to be done.
 */
static int btrfs_set_bit_hook(struct inode *inode, u64 start, u64 end,
		       unsigned long old, unsigned long bits)
{
	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testeing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if (!(old & EXTENT_DELALLOC) && (bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(inode)->root;
		btrfs_delalloc_reserve_space(root, inode, end - start + 1);
		spin_lock(&root->fs_info->delalloc_lock);
		BTRFS_I(inode)->delalloc_bytes += end - start + 1;
		root->fs_info->delalloc_bytes += end - start + 1;
		if (list_empty(&BTRFS_I(inode)->delalloc_inodes)) {
			list_add_tail(&BTRFS_I(inode)->delalloc_inodes,
				      &root->fs_info->delalloc_inodes);
		}
		spin_unlock(&root->fs_info->delalloc_lock);
	}
	return 0;
}

/*
 * extent_io.c clear_bit_hook, see set_bit_hook for why
 */
static int btrfs_clear_bit_hook(struct inode *inode, u64 start, u64 end,
			 unsigned long old, unsigned long bits)
{
	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testeing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if ((old & EXTENT_DELALLOC) && (bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(inode)->root;

		spin_lock(&root->fs_info->delalloc_lock);
		if (end - start + 1 > root->fs_info->delalloc_bytes) {
			printk(KERN_INFO "btrfs warning: delalloc account "
			       "%llu %llu\n",
			       (unsigned long long)end - start + 1,
			       (unsigned long long)
			       root->fs_info->delalloc_bytes);
			btrfs_delalloc_free_space(root, inode, (u64)-1);
			root->fs_info->delalloc_bytes = 0;
			BTRFS_I(inode)->delalloc_bytes = 0;
		} else {
			btrfs_delalloc_free_space(root, inode,
						  end - start + 1);
			root->fs_info->delalloc_bytes -= end - start + 1;
			BTRFS_I(inode)->delalloc_bytes -= end - start + 1;
		}
		if (BTRFS_I(inode)->delalloc_bytes == 0 &&
		    !list_empty(&BTRFS_I(inode)->delalloc_inodes)) {
			list_del_init(&BTRFS_I(inode)->delalloc_inodes);
		}
		spin_unlock(&root->fs_info->delalloc_lock);
	}
	return 0;
}

/*
 * extent_io.c merge_bio_hook, this must check the chunk tree to make sure
 * we don't create bios that span stripes or chunks
 */
int btrfs_merge_bio_hook(struct page *page, unsigned long offset,
			 size_t size, struct bio *bio,
			 unsigned long bio_flags)
{
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	struct btrfs_mapping_tree *map_tree;
	u64 logical = (u64)bio->bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	int ret;

	if (bio_flags & EXTENT_BIO_COMPRESSED)
		return 0;

	length = bio->bi_size;
	map_tree = &root->fs_info->mapping_tree;
	map_length = length;
	ret = btrfs_map_block(map_tree, READ, logical,
			      &map_length, NULL, 0);

	if (map_length < length + size)
		return 1;
	return 0;
}

/*
 * in order to insert checksums into the metadata in large chunks,
 * we wait until bio submission time.   All the pages in the bio are
 * checksummed and sums are attached onto the ordered extent record.
 *
 * At IO completion time the cums attached on the ordered extent record
 * are inserted into the btree
 */
static int __btrfs_submit_bio_start(struct inode *inode, int rw,
				    struct bio *bio, int mirror_num,
				    unsigned long bio_flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	ret = btrfs_csum_one_bio(root, inode, bio, 0, 0);
	BUG_ON(ret);
	return 0;
}

/*
 * in order to insert checksums into the metadata in large chunks,
 * we wait until bio submission time.   All the pages in the bio are
 * checksummed and sums are attached onto the ordered extent record.
 *
 * At IO completion time the cums attached on the ordered extent record
 * are inserted into the btree
 */
static int __btrfs_submit_bio_done(struct inode *inode, int rw, struct bio *bio,
			  int mirror_num, unsigned long bio_flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	return btrfs_map_bio(root, rw, bio, mirror_num, 1);
}

/*
 * extent_io.c submission hook. This does the right thing for csum calculation
 * on write, or reading the csums from the tree before a read
 */
static int btrfs_submit_bio_hook(struct inode *inode, int rw, struct bio *bio,
			  int mirror_num, unsigned long bio_flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	int skip_sum;

	skip_sum = btrfs_test_flag(inode, NODATASUM);

	ret = btrfs_bio_wq_end_io(root->fs_info, bio, 0);
	BUG_ON(ret);

	if (!(rw & (1 << BIO_RW))) {
		if (bio_flags & EXTENT_BIO_COMPRESSED) {
			return btrfs_submit_compressed_read(inode, bio,
						    mirror_num, bio_flags);
		} else if (!skip_sum)
			btrfs_lookup_bio_sums(root, inode, bio, NULL);
		goto mapit;
	} else if (!skip_sum) {
		/* csum items have already been cloned */
		if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID)
			goto mapit;
		/* we're doing a write, do the async checksumming */
		return btrfs_wq_submit_bio(BTRFS_I(inode)->root->fs_info,
				   inode, rw, bio, mirror_num,
				   bio_flags, __btrfs_submit_bio_start,
				   __btrfs_submit_bio_done);
	}

mapit:
	return btrfs_map_bio(root, rw, bio, mirror_num, 0);
}

/*
 * given a list of ordered sums record them in the inode.  This happens
 * at IO completion time based on sums calculated at bio submission time.
 */
static noinline int add_pending_csums(struct btrfs_trans_handle *trans,
			     struct inode *inode, u64 file_offset,
			     struct list_head *list)
{
	struct btrfs_ordered_sum *sum;

	btrfs_set_trans_block_group(trans, inode);

	list_for_each_entry(sum, list, list) {
		btrfs_csum_file_blocks(trans,
		       BTRFS_I(inode)->root->fs_info->csum_root, sum);
	}
	return 0;
}

int btrfs_set_extent_delalloc(struct inode *inode, u64 start, u64 end)
{
	if ((end & (PAGE_CACHE_SIZE - 1)) == 0)
		WARN_ON(1);
	return set_extent_delalloc(&BTRFS_I(inode)->io_tree, start, end,
				   GFP_NOFS);
}

/* see btrfs_writepage_start_hook for details on why this is required */
struct btrfs_writepage_fixup {
	struct page *page;
	struct btrfs_work work;
};

static void btrfs_writepage_fixup_worker(struct btrfs_work *work)
{
	struct btrfs_writepage_fixup *fixup;
	struct btrfs_ordered_extent *ordered;
	struct page *page;
	struct inode *inode;
	u64 page_start;
	u64 page_end;

	fixup = container_of(work, struct btrfs_writepage_fixup, work);
	page = fixup->page;
again:
	lock_page(page);
	if (!page->mapping || !PageDirty(page) || !PageChecked(page)) {
		ClearPageChecked(page);
		goto out_page;
	}

	inode = page->mapping->host;
	page_start = page_offset(page);
	page_end = page_offset(page) + PAGE_CACHE_SIZE - 1;

	lock_extent(&BTRFS_I(inode)->io_tree, page_start, page_end, GFP_NOFS);

	/* already ordered? We're done */
	if (test_range_bit(&BTRFS_I(inode)->io_tree, page_start, page_end,
			     EXTENT_ORDERED, 0)) {
		goto out;
	}

	ordered = btrfs_lookup_ordered_extent(inode, page_start);
	if (ordered) {
		unlock_extent(&BTRFS_I(inode)->io_tree, page_start,
			      page_end, GFP_NOFS);
		unlock_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		goto again;
	}

	btrfs_set_extent_delalloc(inode, page_start, page_end);
	ClearPageChecked(page);
out:
	unlock_extent(&BTRFS_I(inode)->io_tree, page_start, page_end, GFP_NOFS);
out_page:
	unlock_page(page);
	page_cache_release(page);
}

/*
 * There are a few paths in the higher layers of the kernel that directly
 * set the page dirty bit without asking the filesystem if it is a
 * good idea.  This causes problems because we want to make sure COW
 * properly happens and the data=ordered rules are followed.
 *
 * In our case any range that doesn't have the ORDERED bit set
 * hasn't been properly setup for IO.  We kick off an async process
 * to fix it up.  The async helper will wait for ordered extents, set
 * the delalloc bit and make it safe to write the page.
 */
static int btrfs_writepage_start_hook(struct page *page, u64 start, u64 end)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_writepage_fixup *fixup;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	ret = test_range_bit(&BTRFS_I(inode)->io_tree, start, end,
			     EXTENT_ORDERED, 0);
	if (ret)
		return 0;

	if (PageChecked(page))
		return -EAGAIN;

	fixup = kzalloc(sizeof(*fixup), GFP_NOFS);
	if (!fixup)
		return -EAGAIN;

	SetPageChecked(page);
	page_cache_get(page);
	fixup->work.func = btrfs_writepage_fixup_worker;
	fixup->page = page;
	btrfs_queue_worker(&root->fs_info->fixup_workers, &fixup->work);
	return -EAGAIN;
}

static int insert_reserved_file_extent(struct btrfs_trans_handle *trans,
				       struct inode *inode, u64 file_pos,
				       u64 disk_bytenr, u64 disk_num_bytes,
				       u64 num_bytes, u64 ram_bytes,
				       u8 compression, u8 encryption,
				       u16 other_encoding, int extent_type)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_file_extent_item *fi;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key ins;
	u64 hint;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	ret = btrfs_drop_extents(trans, root, inode, file_pos,
				 file_pos + num_bytes, file_pos, &hint);
	BUG_ON(ret);

	ins.objectid = inode->i_ino;
	ins.offset = file_pos;
	ins.type = BTRFS_EXTENT_DATA_KEY;
	ret = btrfs_insert_empty_item(trans, root, path, &ins, sizeof(*fi));
	BUG_ON(ret);
	leaf = path->nodes[0];
	fi = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(leaf, fi, trans->transid);
	btrfs_set_file_extent_type(leaf, fi, extent_type);
	btrfs_set_file_extent_disk_bytenr(leaf, fi, disk_bytenr);
	btrfs_set_file_extent_disk_num_bytes(leaf, fi, disk_num_bytes);
	btrfs_set_file_extent_offset(leaf, fi, 0);
	btrfs_set_file_extent_num_bytes(leaf, fi, num_bytes);
	btrfs_set_file_extent_ram_bytes(leaf, fi, ram_bytes);
	btrfs_set_file_extent_compression(leaf, fi, compression);
	btrfs_set_file_extent_encryption(leaf, fi, encryption);
	btrfs_set_file_extent_other_encoding(leaf, fi, other_encoding);
	btrfs_mark_buffer_dirty(leaf);

	inode_add_bytes(inode, num_bytes);
	btrfs_drop_extent_cache(inode, file_pos, file_pos + num_bytes - 1, 0);

	ins.objectid = disk_bytenr;
	ins.offset = disk_num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;
	ret = btrfs_alloc_reserved_extent(trans, root, leaf->start,
					  root->root_key.objectid,
					  trans->transid, inode->i_ino, &ins);
	BUG_ON(ret);

	btrfs_free_path(path);
	return 0;
}

/* as ordered data IO finishes, this gets called so we can finish
 * an ordered extent if the range of bytes in the file it covers are
 * fully written.
 */
static int btrfs_finish_ordered_io(struct inode *inode, u64 start, u64 end)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct btrfs_ordered_extent *ordered_extent;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	int compressed = 0;
	int ret;

	ret = btrfs_dec_test_ordered_pending(inode, start, end - start + 1);
	if (!ret)
		return 0;

	trans = btrfs_join_transaction(root, 1);

	ordered_extent = btrfs_lookup_ordered_extent(inode, start);
	BUG_ON(!ordered_extent);
	if (test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags))
		goto nocow;

	lock_extent(io_tree, ordered_extent->file_offset,
		    ordered_extent->file_offset + ordered_extent->len - 1,
		    GFP_NOFS);

	if (test_bit(BTRFS_ORDERED_COMPRESSED, &ordered_extent->flags))
		compressed = 1;
	if (test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags)) {
		BUG_ON(compressed);
		ret = btrfs_mark_extent_written(trans, root, inode,
						ordered_extent->file_offset,
						ordered_extent->file_offset +
						ordered_extent->len);
		BUG_ON(ret);
	} else {
		ret = insert_reserved_file_extent(trans, inode,
						ordered_extent->file_offset,
						ordered_extent->start,
						ordered_extent->disk_len,
						ordered_extent->len,
						ordered_extent->len,
						compressed, 0, 0,
						BTRFS_FILE_EXTENT_REG);
		BUG_ON(ret);
	}
	unlock_extent(io_tree, ordered_extent->file_offset,
		    ordered_extent->file_offset + ordered_extent->len - 1,
		    GFP_NOFS);
nocow:
	add_pending_csums(trans, inode, ordered_extent->file_offset,
			  &ordered_extent->list);

	mutex_lock(&BTRFS_I(inode)->extent_mutex);
	btrfs_ordered_update_i_size(inode, ordered_extent);
	btrfs_update_inode(trans, root, inode);
	btrfs_remove_ordered_extent(inode, ordered_extent);
	mutex_unlock(&BTRFS_I(inode)->extent_mutex);

	/* once for us */
	btrfs_put_ordered_extent(ordered_extent);
	/* once for the tree */
	btrfs_put_ordered_extent(ordered_extent);

	btrfs_end_transaction(trans, root);
	return 0;
}

static int btrfs_writepage_end_io_hook(struct page *page, u64 start, u64 end,
				struct extent_state *state, int uptodate)
{
	return btrfs_finish_ordered_io(page->mapping->host, start, end);
}

/*
 * When IO fails, either with EIO or csum verification fails, we
 * try other mirrors that might have a good copy of the data.  This
 * io_failure_record is used to record state as we go through all the
 * mirrors.  If another mirror has good data, the page is set up to date
 * and things continue.  If a good mirror can't be found, the original
 * bio end_io callback is called to indicate things have failed.
 */
struct io_failure_record {
	struct page *page;
	u64 start;
	u64 len;
	u64 logical;
	unsigned long bio_flags;
	int last_mirror;
};

static int btrfs_io_failed_hook(struct bio *failed_bio,
			 struct page *page, u64 start, u64 end,
			 struct extent_state *state)
{
	struct io_failure_record *failrec = NULL;
	u64 private;
	struct extent_map *em;
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct bio *bio;
	int num_copies;
	int ret;
	int rw;
	u64 logical;

	ret = get_state_private(failure_tree, start, &private);
	if (ret) {
		failrec = kmalloc(sizeof(*failrec), GFP_NOFS);
		if (!failrec)
			return -ENOMEM;
		failrec->start = start;
		failrec->len = end - start + 1;
		failrec->last_mirror = 0;
		failrec->bio_flags = 0;

		spin_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, start, failrec->len);
		if (em->start > start || em->start + em->len < start) {
			free_extent_map(em);
			em = NULL;
		}
		spin_unlock(&em_tree->lock);

		if (!em || IS_ERR(em)) {
			kfree(failrec);
			return -EIO;
		}
		logical = start - em->start;
		logical = em->block_start + logical;
		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
			logical = em->block_start;
			failrec->bio_flags = EXTENT_BIO_COMPRESSED;
		}
		failrec->logical = logical;
		free_extent_map(em);
		set_extent_bits(failure_tree, start, end, EXTENT_LOCKED |
				EXTENT_DIRTY, GFP_NOFS);
		set_state_private(failure_tree, start,
				 (u64)(unsigned long)failrec);
	} else {
		failrec = (struct io_failure_record *)(unsigned long)private;
	}
	num_copies = btrfs_num_copies(
			      &BTRFS_I(inode)->root->fs_info->mapping_tree,
			      failrec->logical, failrec->len);
	failrec->last_mirror++;
	if (!state) {
		spin_lock(&BTRFS_I(inode)->io_tree.lock);
		state = find_first_extent_bit_state(&BTRFS_I(inode)->io_tree,
						    failrec->start,
						    EXTENT_LOCKED);
		if (state && state->start != failrec->start)
			state = NULL;
		spin_unlock(&BTRFS_I(inode)->io_tree.lock);
	}
	if (!state || failrec->last_mirror > num_copies) {
		set_state_private(failure_tree, failrec->start, 0);
		clear_extent_bits(failure_tree, failrec->start,
				  failrec->start + failrec->len - 1,
				  EXTENT_LOCKED | EXTENT_DIRTY, GFP_NOFS);
		kfree(failrec);
		return -EIO;
	}
	bio = bio_alloc(GFP_NOFS, 1);
	bio->bi_private = state;
	bio->bi_end_io = failed_bio->bi_end_io;
	bio->bi_sector = failrec->logical >> 9;
	bio->bi_bdev = failed_bio->bi_bdev;
	bio->bi_size = 0;

	bio_add_page(bio, page, failrec->len, start - page_offset(page));
	if (failed_bio->bi_rw & (1 << BIO_RW))
		rw = WRITE;
	else
		rw = READ;

	BTRFS_I(inode)->io_tree.ops->submit_bio_hook(inode, rw, bio,
						      failrec->last_mirror,
						      failrec->bio_flags);
	return 0;
}

/*
 * each time an IO finishes, we do a fast check in the IO failure tree
 * to see if we need to process or clean up an io_failure_record
 */
static int btrfs_clean_io_failures(struct inode *inode, u64 start)
{
	u64 private;
	u64 private_failure;
	struct io_failure_record *failure;
	int ret;

	private = 0;
	if (count_range_bits(&BTRFS_I(inode)->io_failure_tree, &private,
			     (u64)-1, 1, EXTENT_DIRTY)) {
		ret = get_state_private(&BTRFS_I(inode)->io_failure_tree,
					start, &private_failure);
		if (ret == 0) {
			failure = (struct io_failure_record *)(unsigned long)
				   private_failure;
			set_state_private(&BTRFS_I(inode)->io_failure_tree,
					  failure->start, 0);
			clear_extent_bits(&BTRFS_I(inode)->io_failure_tree,
					  failure->start,
					  failure->start + failure->len - 1,
					  EXTENT_DIRTY | EXTENT_LOCKED,
					  GFP_NOFS);
			kfree(failure);
		}
	}
	return 0;
}

/*
 * when reads are done, we need to check csums to verify the data is correct
 * if there's a match, we allow the bio to finish.  If not, we go through
 * the io_failure_record routines to find good copies
 */
static int btrfs_readpage_end_io_hook(struct page *page, u64 start, u64 end,
			       struct extent_state *state)
{
	size_t offset = start - ((u64)page->index << PAGE_CACHE_SHIFT);
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	char *kaddr;
	u64 private = ~(u32)0;
	int ret;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u32 csum = ~(u32)0;

	if (PageChecked(page)) {
		ClearPageChecked(page);
		goto good;
	}
	if (btrfs_test_flag(inode, NODATASUM))
		return 0;

	if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID &&
	    test_range_bit(io_tree, start, end, EXTENT_NODATASUM, 1)) {
		clear_extent_bits(io_tree, start, end, EXTENT_NODATASUM,
				  GFP_NOFS);
		return 0;
	}

	if (state && state->start == start) {
		private = state->private;
		ret = 0;
	} else {
		ret = get_state_private(io_tree, start, &private);
	}
	kaddr = kmap_atomic(page, KM_USER0);
	if (ret)
		goto zeroit;

	csum = btrfs_csum_data(root, kaddr + offset, csum,  end - start + 1);
	btrfs_csum_final(csum, (char *)&csum);
	if (csum != private)
		goto zeroit;

	kunmap_atomic(kaddr, KM_USER0);
good:
	/* if the io failure tree for this inode is non-empty,
	 * check to see if we've recovered from a failed IO
	 */
	btrfs_clean_io_failures(inode, start);
	return 0;

zeroit:
	printk(KERN_INFO "btrfs csum failed ino %lu off %llu csum %u "
	       "private %llu\n", page->mapping->host->i_ino,
	       (unsigned long long)start, csum,
	       (unsigned long long)private);
	memset(kaddr + offset, 1, end - start + 1);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	if (private == 0)
		return 0;
	return -EIO;
}

/*
 * This creates an orphan entry for the given inode in case something goes
 * wrong in the middle of an unlink/truncate.
 */
int btrfs_orphan_add(struct btrfs_trans_handle *trans, struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	spin_lock(&root->list_lock);

	/* already on the orphan list, we're good */
	if (!list_empty(&BTRFS_I(inode)->i_orphan)) {
		spin_unlock(&root->list_lock);
		return 0;
	}

	list_add(&BTRFS_I(inode)->i_orphan, &root->orphan_list);

	spin_unlock(&root->list_lock);

	/*
	 * insert an orphan item to track this unlinked/truncated file
	 */
	ret = btrfs_insert_orphan_item(trans, root, inode->i_ino);

	return ret;
}

/*
 * We have done the truncate/delete so we can go ahead and remove the orphan
 * item for this particular inode.
 */
int btrfs_orphan_del(struct btrfs_trans_handle *trans, struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;

	spin_lock(&root->list_lock);

	if (list_empty(&BTRFS_I(inode)->i_orphan)) {
		spin_unlock(&root->list_lock);
		return 0;
	}

	list_del_init(&BTRFS_I(inode)->i_orphan);
	if (!trans) {
		spin_unlock(&root->list_lock);
		return 0;
	}

	spin_unlock(&root->list_lock);

	ret = btrfs_del_orphan_item(trans, root, inode->i_ino);

	return ret;
}

/*
 * this cleans up any orphans that may be left on the list from the last use
 * of this root.
 */
void btrfs_orphan_cleanup(struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_item *item;
	struct btrfs_key key, found_key;
	struct btrfs_trans_handle *trans;
	struct inode *inode;
	int ret = 0, nr_unlink = 0, nr_truncate = 0;

	path = btrfs_alloc_path();
	if (!path)
		return;
	path->reada = -1;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	btrfs_set_key_type(&key, BTRFS_ORPHAN_ITEM_KEY);
	key.offset = (u64)-1;


	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			printk(KERN_ERR "Error searching slot for orphan: %d"
			       "\n", ret);
			break;
		}

		/*
		 * if ret == 0 means we found what we were searching for, which
		 * is weird, but possible, so only screw with path if we didnt
		 * find the key and see if we have stuff that matches
		 */
		if (ret > 0) {
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}

		/* pull out the item */
		leaf = path->nodes[0];
		item = btrfs_item_nr(leaf, path->slots[0]);
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		/* make sure the item matches what we want */
		if (found_key.objectid != BTRFS_ORPHAN_OBJECTID)
			break;
		if (btrfs_key_type(&found_key) != BTRFS_ORPHAN_ITEM_KEY)
			break;

		/* release the path since we're done with it */
		btrfs_release_path(root, path);

		/*
		 * this is where we are basically btrfs_lookup, without the
		 * crossing root thing.  we store the inode number in the
		 * offset of the orphan item.
		 */
		inode = btrfs_iget_locked(root->fs_info->sb,
					  found_key.offset, root);
		if (!inode)
			break;

		if (inode->i_state & I_NEW) {
			BTRFS_I(inode)->root = root;

			/* have to set the location manually */
			BTRFS_I(inode)->location.objectid = inode->i_ino;
			BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
			BTRFS_I(inode)->location.offset = 0;

			btrfs_read_locked_inode(inode);
			unlock_new_inode(inode);
		}

		/*
		 * add this inode to the orphan list so btrfs_orphan_del does
		 * the proper thing when we hit it
		 */
		spin_lock(&root->list_lock);
		list_add(&BTRFS_I(inode)->i_orphan, &root->orphan_list);
		spin_unlock(&root->list_lock);

		/*
		 * if this is a bad inode, means we actually succeeded in
		 * removing the inode, but not the orphan record, which means
		 * we need to manually delete the orphan since iput will just
		 * do a destroy_inode
		 */
		if (is_bad_inode(inode)) {
			trans = btrfs_start_transaction(root, 1);
			btrfs_orphan_del(trans, inode);
			btrfs_end_transaction(trans, root);
			iput(inode);
			continue;
		}

		/* if we have links, this was a truncate, lets do that */
		if (inode->i_nlink) {
			nr_truncate++;
			btrfs_truncate(inode);
		} else {
			nr_unlink++;
		}

		/* this will do delete_inode and everything for us */
		iput(inode);
	}

	if (nr_unlink)
		printk(KERN_INFO "btrfs: unlinked %d orphans\n", nr_unlink);
	if (nr_truncate)
		printk(KERN_INFO "btrfs: truncated %d orphans\n", nr_truncate);

	btrfs_free_path(path);
}

/*
 * read an inode from the btree into the in-memory inode
 */
void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_inode_item *inode_item;
	struct btrfs_timespec *tspec;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key location;
	u64 alloc_group_block;
	u32 rdev;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	memcpy(&location, &BTRFS_I(inode)->location, sizeof(location));

	ret = btrfs_lookup_inode(NULL, root, path, &location, 0);
	if (ret)
		goto make_bad;

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);

	inode->i_mode = btrfs_inode_mode(leaf, inode_item);
	inode->i_nlink = btrfs_inode_nlink(leaf, inode_item);
	inode->i_uid = btrfs_inode_uid(leaf, inode_item);
	inode->i_gid = btrfs_inode_gid(leaf, inode_item);
	btrfs_i_size_write(inode, btrfs_inode_size(leaf, inode_item));

	tspec = btrfs_inode_atime(inode_item);
	inode->i_atime.tv_sec = btrfs_timespec_sec(leaf, tspec);
	inode->i_atime.tv_nsec = btrfs_timespec_nsec(leaf, tspec);

	tspec = btrfs_inode_mtime(inode_item);
	inode->i_mtime.tv_sec = btrfs_timespec_sec(leaf, tspec);
	inode->i_mtime.tv_nsec = btrfs_timespec_nsec(leaf, tspec);

	tspec = btrfs_inode_ctime(inode_item);
	inode->i_ctime.tv_sec = btrfs_timespec_sec(leaf, tspec);
	inode->i_ctime.tv_nsec = btrfs_timespec_nsec(leaf, tspec);

	inode_set_bytes(inode, btrfs_inode_nbytes(leaf, inode_item));
	BTRFS_I(inode)->generation = btrfs_inode_generation(leaf, inode_item);
	BTRFS_I(inode)->sequence = btrfs_inode_sequence(leaf, inode_item);
	inode->i_generation = BTRFS_I(inode)->generation;
	inode->i_rdev = 0;
	rdev = btrfs_inode_rdev(leaf, inode_item);

	BTRFS_I(inode)->index_cnt = (u64)-1;
	BTRFS_I(inode)->flags = btrfs_inode_flags(leaf, inode_item);

	alloc_group_block = btrfs_inode_block_group(leaf, inode_item);

	BTRFS_I(inode)->block_group = btrfs_find_block_group(root, 0,
						alloc_group_block, 0);
	btrfs_free_path(path);
	inode_item = NULL;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		break;
	case S_IFDIR:
		inode->i_fop = &btrfs_dir_file_operations;
		if (root == root->fs_info->tree_root)
			inode->i_op = &btrfs_dir_ro_inode_operations;
		else
			inode->i_op = &btrfs_dir_inode_operations;
		break;
	case S_IFLNK:
		inode->i_op = &btrfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &btrfs_symlink_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		break;
	default:
		inode->i_op = &btrfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		break;
	}
	return;

make_bad:
	btrfs_free_path(path);
	make_bad_inode(inode);
}

/*
 * given a leaf and an inode, copy the inode fields into the leaf
 */
static void fill_inode_item(struct btrfs_trans_handle *trans,
			    struct extent_buffer *leaf,
			    struct btrfs_inode_item *item,
			    struct inode *inode)
{
	btrfs_set_inode_uid(leaf, item, inode->i_uid);
	btrfs_set_inode_gid(leaf, item, inode->i_gid);
	btrfs_set_inode_size(leaf, item, BTRFS_I(inode)->disk_i_size);
	btrfs_set_inode_mode(leaf, item, inode->i_mode);
	btrfs_set_inode_nlink(leaf, item, inode->i_nlink);

	btrfs_set_timespec_sec(leaf, btrfs_inode_atime(item),
			       inode->i_atime.tv_sec);
	btrfs_set_timespec_nsec(leaf, btrfs_inode_atime(item),
				inode->i_atime.tv_nsec);

	btrfs_set_timespec_sec(leaf, btrfs_inode_mtime(item),
			       inode->i_mtime.tv_sec);
	btrfs_set_timespec_nsec(leaf, btrfs_inode_mtime(item),
				inode->i_mtime.tv_nsec);

	btrfs_set_timespec_sec(leaf, btrfs_inode_ctime(item),
			       inode->i_ctime.tv_sec);
	btrfs_set_timespec_nsec(leaf, btrfs_inode_ctime(item),
				inode->i_ctime.tv_nsec);

	btrfs_set_inode_nbytes(leaf, item, inode_get_bytes(inode));
	btrfs_set_inode_generation(leaf, item, BTRFS_I(inode)->generation);
	btrfs_set_inode_sequence(leaf, item, BTRFS_I(inode)->sequence);
	btrfs_set_inode_transid(leaf, item, trans->transid);
	btrfs_set_inode_rdev(leaf, item, inode->i_rdev);
	btrfs_set_inode_flags(leaf, item, BTRFS_I(inode)->flags);
	btrfs_set_inode_block_group(leaf, item, BTRFS_I(inode)->block_group);
}

/*
 * copy everything in the in-memory inode into the btree.
 */
noinline int btrfs_update_inode(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_lookup_inode(trans, root, path,
				 &BTRFS_I(inode)->location, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	btrfs_unlock_up_safe(path, 1);
	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				  struct btrfs_inode_item);

	fill_inode_item(trans, leaf, inode_item, inode);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_set_inode_last_trans(trans, inode);
	ret = 0;
failed:
	btrfs_free_path(path);
	return ret;
}


/*
 * unlink helper that gets used here in inode.c and in the tree logging
 * recovery code.  It remove a link in a directory with a given name, and
 * also drops the back refs in the inode to the directory
 */
int btrfs_unlink_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       struct inode *dir, struct inode *inode,
		       const char *name, int name_len)
{
	struct btrfs_path *path;
	int ret = 0;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 index;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto err;
	}

	di = btrfs_lookup_dir_item(trans, root, path, dir->i_ino,
				    name, name_len, -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto err;
	}
	if (!di) {
		ret = -ENOENT;
		goto err;
	}
	leaf = path->nodes[0];
	btrfs_dir_item_key_to_cpu(leaf, di, &key);
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	if (ret)
		goto err;
	btrfs_release_path(root, path);

	ret = btrfs_del_inode_ref(trans, root, name, name_len,
				  inode->i_ino,
				  dir->i_ino, &index);
	if (ret) {
		printk(KERN_INFO "btrfs failed to delete reference to %.*s, "
		       "inode %lu parent %lu\n", name_len, name,
		       inode->i_ino, dir->i_ino);
		goto err;
	}

	di = btrfs_lookup_dir_index_item(trans, root, path, dir->i_ino,
					 index, name, name_len, -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto err;
	}
	if (!di) {
		ret = -ENOENT;
		goto err;
	}
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	btrfs_release_path(root, path);

	ret = btrfs_del_inode_ref_in_log(trans, root, name, name_len,
					 inode, dir->i_ino);
	BUG_ON(ret != 0 && ret != -ENOENT);
	if (ret != -ENOENT)
		BTRFS_I(dir)->log_dirty_trans = trans->transid;

	ret = btrfs_del_dir_entries_in_log(trans, root, name, name_len,
					   dir, index);
	BUG_ON(ret);
err:
	btrfs_free_path(path);
	if (ret)
		goto out;

	btrfs_i_size_write(dir, dir->i_size - name_len * 2);
	inode->i_ctime = dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	btrfs_update_inode(trans, root, dir);
	btrfs_drop_nlink(inode);
	ret = btrfs_update_inode(trans, root, inode);
	dir->i_sb->s_dirt = 1;
out:
	return ret;
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans;
	struct inode *inode = dentry->d_inode;
	int ret;
	unsigned long nr = 0;

	root = BTRFS_I(dir)->root;

	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, dir);
	ret = btrfs_unlink_inode(trans, root, dir, dentry->d_inode,
				 dentry->d_name.name, dentry->d_name.len);

	if (inode->i_nlink == 0)
		ret = btrfs_orphan_add(trans, inode);

	nr = trans->blocks_used;

	btrfs_end_transaction_throttle(trans, root);
	btrfs_btree_balance_dirty(root, nr);
	return ret;
}

static int btrfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int err = 0;
	int ret;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_trans_handle *trans;
	unsigned long nr = 0;

	/*
	 * the FIRST_FREE_OBJECTID check makes sure we don't try to rmdir
	 * the root of a subvolume or snapshot
	 */
	if (inode->i_size > BTRFS_EMPTY_DIR_SIZE ||
	    inode->i_ino == BTRFS_FIRST_FREE_OBJECTID) {
		return -ENOTEMPTY;
	}

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_orphan_add(trans, inode);
	if (err)
		goto fail_trans;

	/* now the directory is empty */
	err = btrfs_unlink_inode(trans, root, dir, dentry->d_inode,
				 dentry->d_name.name, dentry->d_name.len);
	if (!err)
		btrfs_i_size_write(inode, 0);

fail_trans:
	nr = trans->blocks_used;
	ret = btrfs_end_transaction_throttle(trans, root);
	btrfs_btree_balance_dirty(root, nr);

	if (ret && !err)
		err = ret;
	return err;
}

#if 0
/*
 * when truncating bytes in a file, it is possible to avoid reading
 * the leaves that contain only checksum items.  This can be the
 * majority of the IO required to delete a large file, but it must
 * be done carefully.
 *
 * The keys in the level just above the leaves are checked to make sure
 * the lowest key in a given leaf is a csum key, and starts at an offset
 * after the new  size.
 *
 * Then the key for the next leaf is checked to make sure it also has
 * a checksum item for the same file.  If it does, we know our target leaf
 * contains only checksum items, and it can be safely freed without reading
 * it.
 *
 * This is just an optimization targeted at large files.  It may do
 * nothing.  It will return 0 unless things went badly.
 */
static noinline int drop_csum_leaves(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct btrfs_path *path,
				     struct inode *inode, u64 new_size)
{
	struct btrfs_key key;
	int ret;
	int nritems;
	struct btrfs_key found_key;
	struct btrfs_key other_key;
	struct btrfs_leaf_ref *ref;
	u64 leaf_gen;
	u64 leaf_start;

	path->lowest_level = 1;
	key.objectid = inode->i_ino;
	key.type = BTRFS_CSUM_ITEM_KEY;
	key.offset = new_size;
again:
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (path->nodes[1] == NULL) {
		ret = 0;
		goto out;
	}
	ret = 0;
	btrfs_node_key_to_cpu(path->nodes[1], &found_key, path->slots[1]);
	nritems = btrfs_header_nritems(path->nodes[1]);

	if (!nritems)
		goto out;

	if (path->slots[1] >= nritems)
		goto next_node;

	/* did we find a key greater than anything we want to delete? */
	if (found_key.objectid > inode->i_ino ||
	   (found_key.objectid == inode->i_ino && found_key.type > key.type))
		goto out;

	/* we check the next key in the node to make sure the leave contains
	 * only checksum items.  This comparison doesn't work if our
	 * leaf is the last one in the node
	 */
	if (path->slots[1] + 1 >= nritems) {
next_node:
		/* search forward from the last key in the node, this
		 * will bring us into the next node in the tree
		 */
		btrfs_node_key_to_cpu(path->nodes[1], &found_key, nritems - 1);

		/* unlikely, but we inc below, so check to be safe */
		if (found_key.offset == (u64)-1)
			goto out;

		/* search_forward needs a path with locks held, do the
		 * search again for the original key.  It is possible
		 * this will race with a balance and return a path that
		 * we could modify, but this drop is just an optimization
		 * and is allowed to miss some leaves.
		 */
		btrfs_release_path(root, path);
		found_key.offset++;

		/* setup a max key for search_forward */
		other_key.offset = (u64)-1;
		other_key.type = key.type;
		other_key.objectid = key.objectid;

		path->keep_locks = 1;
		ret = btrfs_search_forward(root, &found_key, &other_key,
					   path, 0, 0);
		path->keep_locks = 0;
		if (ret || found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			ret = 0;
			goto out;
		}

		key.offset = found_key.offset;
		btrfs_release_path(root, path);
		cond_resched();
		goto again;
	}

	/* we know there's one more slot after us in the tree,
	 * read that key so we can verify it is also a checksum item
	 */
	btrfs_node_key_to_cpu(path->nodes[1], &other_key, path->slots[1] + 1);

	if (found_key.objectid < inode->i_ino)
		goto next_key;

	if (found_key.type != key.type || found_key.offset < new_size)
		goto next_key;

	/*
	 * if the key for the next leaf isn't a csum key from this objectid,
	 * we can't be sure there aren't good items inside this leaf.
	 * Bail out
	 */
	if (other_key.objectid != inode->i_ino || other_key.type != key.type)
		goto out;

	leaf_start = btrfs_node_blockptr(path->nodes[1], path->slots[1]);
	leaf_gen = btrfs_node_ptr_generation(path->nodes[1], path->slots[1]);
	/*
	 * it is safe to delete this leaf, it contains only
	 * csum items from this inode at an offset >= new_size
	 */
	ret = btrfs_del_leaf(trans, root, path, leaf_start);
	BUG_ON(ret);

	if (root->ref_cows && leaf_gen < trans->transid) {
		ref = btrfs_alloc_leaf_ref(root, 0);
		if (ref) {
			ref->root_gen = root->root_key.offset;
			ref->bytenr = leaf_start;
			ref->owner = 0;
			ref->generation = leaf_gen;
			ref->nritems = 0;

			btrfs_sort_leaf_ref(ref);

			ret = btrfs_add_leaf_ref(root, ref, 0);
			WARN_ON(ret);
			btrfs_free_leaf_ref(root, ref);
		} else {
			WARN_ON(1);
		}
	}
next_key:
	btrfs_release_path(root, path);

	if (other_key.objectid == inode->i_ino &&
	    other_key.type == key.type && other_key.offset > key.offset) {
		key.offset = other_key.offset;
		cond_resched();
		goto again;
	}
	ret = 0;
out:
	/* fixup any changes we've made to the path */
	path->lowest_level = 0;
	path->keep_locks = 0;
	btrfs_release_path(root, path);
	return ret;
}

#endif

/*
 * this can truncate away extent items, csum items and directory items.
 * It starts at a high offset and removes keys until it can't find
 * any higher than new_size
 *
 * csum items that cross the new i_size are truncated to the new size
 * as well.
 *
 * min_type is the minimum key type to truncate down to.  If set to 0, this
 * will kill all the items on this inode, including the INODE_ITEM_KEY.
 */
noinline int btrfs_truncate_inode_items(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct inode *inode,
					u64 new_size, u32 min_type)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u32 found_type = (u8)-1;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	u64 extent_start = 0;
	u64 extent_num_bytes = 0;
	u64 item_end = 0;
	u64 root_gen = 0;
	u64 root_owner = 0;
	int found_extent;
	int del_item;
	int pending_del_nr = 0;
	int pending_del_slot = 0;
	int extent_type = -1;
	int encoding;
	u64 mask = root->sectorsize - 1;

	if (root->ref_cows)
		btrfs_drop_extent_cache(inode, new_size & (~mask), (u64)-1, 0);
	path = btrfs_alloc_path();
	path->reada = -1;
	BUG_ON(!path);

	/* FIXME, add redo link to tree so we don't leak on crash */
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.type = (u8)-1;

search_again:
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto error;

	if (ret > 0) {
		/* there are no items in the tree for us to truncate, we're
		 * done
		 */
		if (path->slots[0] == 0) {
			ret = 0;
			goto error;
		}
		path->slots[0]--;
	}

	while (1) {
		fi = NULL;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		found_type = btrfs_key_type(&found_key);
		encoding = 0;

		if (found_key.objectid != inode->i_ino)
			break;

		if (found_type < min_type)
			break;

		item_end = found_key.offset;
		if (found_type == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			extent_type = btrfs_file_extent_type(leaf, fi);
			encoding = btrfs_file_extent_compression(leaf, fi);
			encoding |= btrfs_file_extent_encryption(leaf, fi);
			encoding |= btrfs_file_extent_other_encoding(leaf, fi);

			if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
				item_end +=
				    btrfs_file_extent_num_bytes(leaf, fi);
			} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
				item_end += btrfs_file_extent_inline_len(leaf,
									 fi);
			}
			item_end--;
		}
		if (item_end < new_size) {
			if (found_type == BTRFS_DIR_ITEM_KEY)
				found_type = BTRFS_INODE_ITEM_KEY;
			else if (found_type == BTRFS_EXTENT_ITEM_KEY)
				found_type = BTRFS_EXTENT_DATA_KEY;
			else if (found_type == BTRFS_EXTENT_DATA_KEY)
				found_type = BTRFS_XATTR_ITEM_KEY;
			else if (found_type == BTRFS_XATTR_ITEM_KEY)
				found_type = BTRFS_INODE_REF_KEY;
			else if (found_type)
				found_type--;
			else
				break;
			btrfs_set_key_type(&key, found_type);
			goto next;
		}
		if (found_key.offset >= new_size)
			del_item = 1;
		else
			del_item = 0;
		found_extent = 0;

		/* FIXME, shrink the extent if the ref count is only 1 */
		if (found_type != BTRFS_EXTENT_DATA_KEY)
			goto delete;

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			u64 num_dec;
			extent_start = btrfs_file_extent_disk_bytenr(leaf, fi);
			if (!del_item && !encoding) {
				u64 orig_num_bytes =
					btrfs_file_extent_num_bytes(leaf, fi);
				extent_num_bytes = new_size -
					found_key.offset + root->sectorsize - 1;
				extent_num_bytes = extent_num_bytes &
					~((u64)root->sectorsize - 1);
				btrfs_set_file_extent_num_bytes(leaf, fi,
							 extent_num_bytes);
				num_dec = (orig_num_bytes -
					   extent_num_bytes);
				if (root->ref_cows && extent_start != 0)
					inode_sub_bytes(inode, num_dec);
				btrfs_mark_buffer_dirty(leaf);
			} else {
				extent_num_bytes =
					btrfs_file_extent_disk_num_bytes(leaf,
									 fi);
				/* FIXME blocksize != 4096 */
				num_dec = btrfs_file_extent_num_bytes(leaf, fi);
				if (extent_start != 0) {
					found_extent = 1;
					if (root->ref_cows)
						inode_sub_bytes(inode, num_dec);
				}
				root_gen = btrfs_header_generation(leaf);
				root_owner = btrfs_header_owner(leaf);
			}
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			/*
			 * we can't truncate inline items that have had
			 * special encodings
			 */
			if (!del_item &&
			    btrfs_file_extent_compression(leaf, fi) == 0 &&
			    btrfs_file_extent_encryption(leaf, fi) == 0 &&
			    btrfs_file_extent_other_encoding(leaf, fi) == 0) {
				u32 size = new_size - found_key.offset;

				if (root->ref_cows) {
					inode_sub_bytes(inode, item_end + 1 -
							new_size);
				}
				size =
				    btrfs_file_extent_calc_inline_size(size);
				ret = btrfs_truncate_item(trans, root, path,
							  size, 1);
				BUG_ON(ret);
			} else if (root->ref_cows) {
				inode_sub_bytes(inode, item_end + 1 -
						found_key.offset);
			}
		}
delete:
		if (del_item) {
			if (!pending_del_nr) {
				/* no pending yet, add ourselves */
				pending_del_slot = path->slots[0];
				pending_del_nr = 1;
			} else if (pending_del_nr &&
				   path->slots[0] + 1 == pending_del_slot) {
				/* hop on the pending chunk */
				pending_del_nr++;
				pending_del_slot = path->slots[0];
			} else {
				BUG();
			}
		} else {
			break;
		}
		if (found_extent) {
			ret = btrfs_free_extent(trans, root, extent_start,
						extent_num_bytes,
						leaf->start, root_owner,
						root_gen, inode->i_ino, 0);
			BUG_ON(ret);
		}
next:
		if (path->slots[0] == 0) {
			if (pending_del_nr)
				goto del_pending;
			btrfs_release_path(root, path);
			if (found_type == BTRFS_INODE_ITEM_KEY)
				break;
			goto search_again;
		}

		path->slots[0]--;
		if (pending_del_nr &&
		    path->slots[0] + 1 != pending_del_slot) {
			struct btrfs_key debug;
del_pending:
			btrfs_item_key_to_cpu(path->nodes[0], &debug,
					      pending_del_slot);
			ret = btrfs_del_items(trans, root, path,
					      pending_del_slot,
					      pending_del_nr);
			BUG_ON(ret);
			pending_del_nr = 0;
			btrfs_release_path(root, path);
			if (found_type == BTRFS_INODE_ITEM_KEY)
				break;
			goto search_again;
		}
	}
	ret = 0;
error:
	if (pending_del_nr) {
		ret = btrfs_del_items(trans, root, path, pending_del_slot,
				      pending_del_nr);
	}
	btrfs_free_path(path);
	inode->i_sb->s_dirt = 1;
	return ret;
}

/*
 * taken from block_truncate_page, but does cow as it zeros out
 * any bytes left in the last page in the file.
 */
static int btrfs_truncate_page(struct address_space *mapping, loff_t from)
{
	struct inode *inode = mapping->host;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	char *kaddr;
	u32 blocksize = root->sectorsize;
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	struct page *page;
	int ret = 0;
	u64 page_start;
	u64 page_end;

	if ((offset & (blocksize - 1)) == 0)
		goto out;

	ret = -ENOMEM;
again:
	page = grab_cache_page(mapping, index);
	if (!page)
		goto out;

	page_start = page_offset(page);
	page_end = page_start + PAGE_CACHE_SIZE - 1;

	if (!PageUptodate(page)) {
		ret = btrfs_readpage(NULL, page);
		lock_page(page);
		if (page->mapping != mapping) {
			unlock_page(page);
			page_cache_release(page);
			goto again;
		}
		if (!PageUptodate(page)) {
			ret = -EIO;
			goto out_unlock;
		}
	}
	wait_on_page_writeback(page);

	lock_extent(io_tree, page_start, page_end, GFP_NOFS);
	set_page_extent_mapped(page);

	ordered = btrfs_lookup_ordered_extent(inode, page_start);
	if (ordered) {
		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		unlock_page(page);
		page_cache_release(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	btrfs_set_extent_delalloc(inode, page_start, page_end);
	ret = 0;
	if (offset != PAGE_CACHE_SIZE) {
		kaddr = kmap(page);
		memset(kaddr + offset, 0, PAGE_CACHE_SIZE - offset);
		flush_dcache_page(page);
		kunmap(page);
	}
	ClearPageChecked(page);
	set_page_dirty(page);
	unlock_extent(io_tree, page_start, page_end, GFP_NOFS);

out_unlock:
	unlock_page(page);
	page_cache_release(page);
out:
	return ret;
}

int btrfs_cont_expand(struct inode *inode, loff_t size)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em;
	u64 mask = root->sectorsize - 1;
	u64 hole_start = (inode->i_size + mask) & ~mask;
	u64 block_end = (size + mask) & ~mask;
	u64 last_byte;
	u64 cur_offset;
	u64 hole_size;
	int err;

	if (size <= hole_start)
		return 0;

	err = btrfs_check_metadata_free_space(root);
	if (err)
		return err;

	btrfs_truncate_page(inode->i_mapping, inode->i_size);

	while (1) {
		struct btrfs_ordered_extent *ordered;
		btrfs_wait_ordered_range(inode, hole_start,
					 block_end - hole_start);
		lock_extent(io_tree, hole_start, block_end - 1, GFP_NOFS);
		ordered = btrfs_lookup_ordered_extent(inode, hole_start);
		if (!ordered)
			break;
		unlock_extent(io_tree, hole_start, block_end - 1, GFP_NOFS);
		btrfs_put_ordered_extent(ordered);
	}

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);

	cur_offset = hole_start;
	while (1) {
		em = btrfs_get_extent(inode, NULL, 0, cur_offset,
				block_end - cur_offset, 0);
		BUG_ON(IS_ERR(em) || !em);
		last_byte = min(extent_map_end(em), block_end);
		last_byte = (last_byte + mask) & ~mask;
		if (test_bit(EXTENT_FLAG_VACANCY, &em->flags)) {
			u64 hint_byte = 0;
			hole_size = last_byte - cur_offset;
			err = btrfs_drop_extents(trans, root, inode,
						 cur_offset,
						 cur_offset + hole_size,
						 cur_offset, &hint_byte);
			if (err)
				break;
			err = btrfs_insert_file_extent(trans, root,
					inode->i_ino, cur_offset, 0,
					0, hole_size, 0, hole_size,
					0, 0, 0);
			btrfs_drop_extent_cache(inode, hole_start,
					last_byte - 1, 0);
		}
		free_extent_map(em);
		cur_offset = last_byte;
		if (err || cur_offset >= block_end)
			break;
	}

	btrfs_end_transaction(trans, root);
	unlock_extent(io_tree, hole_start, block_end - 1, GFP_NOFS);
	return err;
}

static int btrfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int err;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE && attr->ia_size > inode->i_size) {
		err = btrfs_cont_expand(inode, attr->ia_size);
		if (err)
			return err;
	}

	err = inode_setattr(inode, attr);

	if (!err && ((attr->ia_valid & ATTR_MODE)))
		err = btrfs_acl_chmod(inode);
	return err;
}

void btrfs_delete_inode(struct inode *inode)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	unsigned long nr;
	int ret;

	truncate_inode_pages(&inode->i_data, 0);
	if (is_bad_inode(inode)) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete;
	}
	btrfs_wait_ordered_range(inode, 0, (u64)-1);

	btrfs_i_size_write(inode, 0);
	trans = btrfs_join_transaction(root, 1);

	btrfs_set_trans_block_group(trans, inode);
	ret = btrfs_truncate_inode_items(trans, root, inode, inode->i_size, 0);
	if (ret) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete_lock;
	}

	btrfs_orphan_del(trans, inode);

	nr = trans->blocks_used;
	clear_inode(inode);

	btrfs_end_transaction(trans, root);
	btrfs_btree_balance_dirty(root, nr);
	return;

no_delete_lock:
	nr = trans->blocks_used;
	btrfs_end_transaction(trans, root);
	btrfs_btree_balance_dirty(root, nr);
no_delete:
	clear_inode(inode);
}

/*
 * this returns the key found in the dir entry in the location pointer.
 * If no dir entries were found, location->objectid is 0.
 */
static int btrfs_inode_by_name(struct inode *dir, struct dentry *dentry,
			       struct btrfs_key *location)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int ret = 0;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	di = btrfs_lookup_dir_item(NULL, root, path, dir->i_ino, name,
				    namelen, 0);
	if (IS_ERR(di))
		ret = PTR_ERR(di);

	if (!di || IS_ERR(di))
		goto out_err;

	btrfs_dir_item_key_to_cpu(path->nodes[0], di, location);
out:
	btrfs_free_path(path);
	return ret;
out_err:
	location->objectid = 0;
	goto out;
}

/*
 * when we hit a tree root in a directory, the btrfs part of the inode
 * needs to be changed to reflect the root directory of the tree root.  This
 * is kind of like crossing a mount point.
 */
static int fixup_tree_root_location(struct btrfs_root *root,
			     struct btrfs_key *location,
			     struct btrfs_root **sub_root,
			     struct dentry *dentry)
{
	struct btrfs_root_item *ri;

	if (btrfs_key_type(location) != BTRFS_ROOT_ITEM_KEY)
		return 0;
	if (location->objectid == BTRFS_ROOT_TREE_OBJECTID)
		return 0;

	*sub_root = btrfs_read_fs_root(root->fs_info, location,
					dentry->d_name.name,
					dentry->d_name.len);
	if (IS_ERR(*sub_root))
		return PTR_ERR(*sub_root);

	ri = &(*sub_root)->root_item;
	location->objectid = btrfs_root_dirid(ri);
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);
	location->offset = 0;

	return 0;
}

static noinline void init_btrfs_i(struct inode *inode)
{
	struct btrfs_inode *bi = BTRFS_I(inode);

	bi->i_acl = NULL;
	bi->i_default_acl = NULL;

	bi->generation = 0;
	bi->sequence = 0;
	bi->last_trans = 0;
	bi->logged_trans = 0;
	bi->delalloc_bytes = 0;
	bi->reserved_bytes = 0;
	bi->disk_i_size = 0;
	bi->flags = 0;
	bi->index_cnt = (u64)-1;
	bi->log_dirty_trans = 0;
	extent_map_tree_init(&BTRFS_I(inode)->extent_tree, GFP_NOFS);
	extent_io_tree_init(&BTRFS_I(inode)->io_tree,
			     inode->i_mapping, GFP_NOFS);
	extent_io_tree_init(&BTRFS_I(inode)->io_failure_tree,
			     inode->i_mapping, GFP_NOFS);
	INIT_LIST_HEAD(&BTRFS_I(inode)->delalloc_inodes);
	btrfs_ordered_inode_tree_init(&BTRFS_I(inode)->ordered_tree);
	mutex_init(&BTRFS_I(inode)->extent_mutex);
	mutex_init(&BTRFS_I(inode)->log_mutex);
}

static int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;
	inode->i_ino = args->ino;
	init_btrfs_i(inode);
	BTRFS_I(inode)->root = args->root;
	btrfs_set_inode_space_info(args->root, inode);
	return 0;
}

static int btrfs_find_actor(struct inode *inode, void *opaque)
{
	struct btrfs_iget_args *args = opaque;
	return args->ino == inode->i_ino &&
		args->root == BTRFS_I(inode)->root;
}

struct inode *btrfs_ilookup(struct super_block *s, u64 objectid,
			    struct btrfs_root *root, int wait)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	args.ino = objectid;
	args.root = root;

	if (wait) {
		inode = ilookup5(s, objectid, btrfs_find_actor,
				 (void *)&args);
	} else {
		inode = ilookup5_nowait(s, objectid, btrfs_find_actor,
					(void *)&args);
	}
	return inode;
}

struct inode *btrfs_iget_locked(struct super_block *s, u64 objectid,
				struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	args.ino = objectid;
	args.root = root;

	inode = iget5_locked(s, objectid, btrfs_find_actor,
			     btrfs_init_locked_inode,
			     (void *)&args);
	return inode;
}

/* Get an inode object given its location and corresponding root.
 * Returns in *is_new if the inode was read from disk
 */
struct inode *btrfs_iget(struct super_block *s, struct btrfs_key *location,
			 struct btrfs_root *root, int *is_new)
{
	struct inode *inode;

	inode = btrfs_iget_locked(s, location->objectid, root);
	if (!inode)
		return ERR_PTR(-EACCES);

	if (inode->i_state & I_NEW) {
		BTRFS_I(inode)->root = root;
		memcpy(&BTRFS_I(inode)->location, location, sizeof(*location));
		btrfs_read_locked_inode(inode);
		unlock_new_inode(inode);
		if (is_new)
			*is_new = 1;
	} else {
		if (is_new)
			*is_new = 0;
	}

	return inode;
}

struct inode *btrfs_lookup_dentry(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode;
	struct btrfs_inode *bi = BTRFS_I(dir);
	struct btrfs_root *root = bi->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location;
	int ret, new;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(dir, dentry, &location);

	if (ret < 0)
		return ERR_PTR(ret);

	inode = NULL;
	if (location.objectid) {
		ret = fixup_tree_root_location(root, &location, &sub_root,
						dentry);
		if (ret < 0)
			return ERR_PTR(ret);
		if (ret > 0)
			return ERR_PTR(-ENOENT);
		inode = btrfs_iget(dir->i_sb, &location, sub_root, &new);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	return inode;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode *inode;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	inode = btrfs_lookup_dentry(dir, dentry);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	return d_splice_alias(inode, dentry);
}

static unsigned char btrfs_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int btrfs_real_readdir(struct file *filp, void *dirent,
			      filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	int ret;
	u32 nritems;
	struct extent_buffer *leaf;
	int slot;
	int advance;
	unsigned char d_type;
	int over = 0;
	u32 di_cur;
	u32 di_total;
	u32 di_len;
	int key_type = BTRFS_DIR_INDEX_KEY;
	char tmp_name[32];
	char *name_ptr;
	int name_len;

	/* FIXME, use a real flag for deciding about the key type */
	if (root->fs_info->tree_root == root)
		key_type = BTRFS_DIR_ITEM_KEY;

	/* special case for "." */
	if (filp->f_pos == 0) {
		over = filldir(dirent, ".", 1,
			       1, inode->i_ino,
			       DT_DIR);
		if (over)
			return 0;
		filp->f_pos = 1;
	}
	/* special case for .., just use the back ref */
	if (filp->f_pos == 1) {
		u64 pino = parent_ino(filp->f_path.dentry);
		over = filldir(dirent, "..", 2,
			       2, pino, DT_DIR);
		if (over)
			return 0;
		filp->f_pos = 2;
	}
	path = btrfs_alloc_path();
	path->reada = 2;

	btrfs_set_key_type(&key, key_type);
	key.offset = filp->f_pos;
	key.objectid = inode->i_ino;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	advance = 0;

	while (1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		slot = path->slots[0];
		if (advance || slot >= nritems) {
			if (slot >= nritems - 1) {
				ret = btrfs_next_leaf(root, path);
				if (ret)
					break;
				leaf = path->nodes[0];
				nritems = btrfs_header_nritems(leaf);
				slot = path->slots[0];
			} else {
				slot++;
				path->slots[0]++;
			}
		}

		advance = 1;
		item = btrfs_item_nr(leaf, slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid)
			break;
		if (btrfs_key_type(&found_key) != key_type)
			break;
		if (found_key.offset < filp->f_pos)
			continue;

		filp->f_pos = found_key.offset;

		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		di_cur = 0;
		di_total = btrfs_item_size(leaf, item);

		while (di_cur < di_total) {
			struct btrfs_key location;

			name_len = btrfs_dir_name_len(leaf, di);
			if (name_len <= sizeof(tmp_name)) {
				name_ptr = tmp_name;
			} else {
				name_ptr = kmalloc(name_len, GFP_NOFS);
				if (!name_ptr) {
					ret = -ENOMEM;
					goto err;
				}
			}
			read_extent_buffer(leaf, name_ptr,
					   (unsigned long)(di + 1), name_len);

			d_type = btrfs_filetype_table[btrfs_dir_type(leaf, di)];
			btrfs_dir_item_key_to_cpu(leaf, di, &location);

			/* is this a reference to our own snapshot? If so
			 * skip it
			 */
			if (location.type == BTRFS_ROOT_ITEM_KEY &&
			    location.objectid == root->root_key.objectid) {
				over = 0;
				goto skip;
			}
			over = filldir(dirent, name_ptr, name_len,
				       found_key.offset, location.objectid,
				       d_type);

skip:
			if (name_ptr != tmp_name)
				kfree(name_ptr);

			if (over)
				goto nopos;
			di_len = btrfs_dir_name_len(leaf, di) +
				 btrfs_dir_data_len(leaf, di) + sizeof(*di);
			di_cur += di_len;
			di = (struct btrfs_dir_item *)((char *)di + di_len);
		}
	}

	/* Reached end of directory/root. Bump pos past the last item. */
	if (key_type == BTRFS_DIR_INDEX_KEY)
		filp->f_pos = INT_LIMIT(off_t);
	else
		filp->f_pos++;
nopos:
	ret = 0;
err:
	btrfs_free_path(path);
	return ret;
}

int btrfs_write_inode(struct inode *inode, int wait)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	int ret = 0;

	if (root->fs_info->btree_inode == inode)
		return 0;

	if (wait) {
		trans = btrfs_join_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);
		ret = btrfs_commit_transaction(trans, root);
	}
	return ret;
}

/*
 * This is somewhat expensive, updating the tree every time the
 * inode changes.  But, it is most likely to find the inode in cache.
 * FIXME, needs more benchmarking...there are no reasons other than performance
 * to keep or drop this code.
 */
void btrfs_dirty_inode(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;

	trans = btrfs_join_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	btrfs_update_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
}

/*
 * find the highest existing sequence number in a directory
 * and then set the in-memory index_cnt variable to reflect
 * free sequence numbers
 */
static int btrfs_set_inode_index_count(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key key, found_key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = inode->i_ino;
	btrfs_set_key_type(&key, BTRFS_DIR_INDEX_KEY);
	key.offset = (u64)-1;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	/* FIXME: we should be able to handle this */
	if (ret == 0)
		goto out;
	ret = 0;

	/*
	 * MAGIC NUMBER EXPLANATION:
	 * since we search a directory based on f_pos we have to start at 2
	 * since '.' and '..' have f_pos of 0 and 1 respectively, so everybody
	 * else has to start at 2
	 */
	if (path->slots[0] == 0) {
		BTRFS_I(inode)->index_cnt = 2;
		goto out;
	}

	path->slots[0]--;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	if (found_key.objectid != inode->i_ino ||
	    btrfs_key_type(&found_key) != BTRFS_DIR_INDEX_KEY) {
		BTRFS_I(inode)->index_cnt = 2;
		goto out;
	}

	BTRFS_I(inode)->index_cnt = found_key.offset + 1;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper to find a free sequence number in a given directory.  This current
 * code is very simple, later versions will do smarter things in the btree
 */
int btrfs_set_inode_index(struct inode *dir, u64 *index)
{
	int ret = 0;

	if (BTRFS_I(dir)->index_cnt == (u64)-1) {
		ret = btrfs_set_inode_index_count(dir);
		if (ret)
			return ret;
	}

	*index = BTRFS_I(dir)->index_cnt;
	BTRFS_I(dir)->index_cnt++;

	return ret;
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct inode *dir,
				     const char *name, int name_len,
				     u64 ref_objectid, u64 objectid,
				     u64 alloc_hint, int mode, u64 *index)
{
	struct inode *inode;
	struct btrfs_inode_item *inode_item;
	struct btrfs_key *location;
	struct btrfs_path *path;
	struct btrfs_inode_ref *ref;
	struct btrfs_key key[2];
	u32 sizes[2];
	unsigned long ptr;
	int ret;
	int owner;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	inode = new_inode(root->fs_info->sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (dir) {
		ret = btrfs_set_inode_index(dir, index);
		if (ret)
			return ERR_PTR(ret);
	}
	/*
	 * index_cnt is ignored for everything but a dir,
	 * btrfs_get_inode_index_count has an explanation for the magic
	 * number
	 */
	init_btrfs_i(inode);
	BTRFS_I(inode)->index_cnt = 2;
	BTRFS_I(inode)->root = root;
	BTRFS_I(inode)->generation = trans->transid;
	btrfs_set_inode_space_info(root, inode);

	if (mode & S_IFDIR)
		owner = 0;
	else
		owner = 1;
	BTRFS_I(inode)->block_group =
			btrfs_find_block_group(root, 0, alloc_hint, owner);
	if ((mode & S_IFREG)) {
		if (btrfs_test_opt(root, NODATASUM))
			btrfs_set_flag(inode, NODATASUM);
		if (btrfs_test_opt(root, NODATACOW))
			btrfs_set_flag(inode, NODATACOW);
	}

	key[0].objectid = objectid;
	btrfs_set_key_type(&key[0], BTRFS_INODE_ITEM_KEY);
	key[0].offset = 0;

	key[1].objectid = objectid;
	btrfs_set_key_type(&key[1], BTRFS_INODE_REF_KEY);
	key[1].offset = ref_objectid;

	sizes[0] = sizeof(struct btrfs_inode_item);
	sizes[1] = name_len + sizeof(*ref);

	ret = btrfs_insert_empty_items(trans, root, path, key, sizes, 2);
	if (ret != 0)
		goto fail;

	if (objectid > root->highest_inode)
		root->highest_inode = objectid;

	inode->i_uid = current_fsuid();

	if (dir && (dir->i_mode & S_ISGID)) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current_fsgid();

	inode->i_mode = mode;
	inode->i_ino = objectid;
	inode_set_bytes(inode, 0);
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode_item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				  struct btrfs_inode_item);
	fill_inode_item(trans, path->nodes[0], inode_item, inode);

	ref = btrfs_item_ptr(path->nodes[0], path->slots[0] + 1,
			     struct btrfs_inode_ref);
	btrfs_set_inode_ref_name_len(path->nodes[0], ref, name_len);
	btrfs_set_inode_ref_index(path->nodes[0], ref, *index);
	ptr = (unsigned long)(ref + 1);
	write_extent_buffer(path->nodes[0], name, ptr, name_len);

	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	location = &BTRFS_I(inode)->location;
	location->objectid = objectid;
	location->offset = 0;
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);

	insert_inode_hash(inode);
	return inode;
fail:
	if (dir)
		BTRFS_I(dir)->index_cnt--;
	btrfs_free_path(path);
	return ERR_PTR(ret);
}

static inline u8 btrfs_inode_type(struct inode *inode)
{
	return btrfs_type_by_mode[(inode->i_mode & S_IFMT) >> S_SHIFT];
}

/*
 * utility function to add 'inode' into 'parent_inode' with
 * a give name and a given sequence number.
 * if 'add_backref' is true, also insert a backref from the
 * inode to the parent directory.
 */
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct inode *parent_inode, struct inode *inode,
		   const char *name, int name_len, int add_backref, u64 index)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_root *root = BTRFS_I(parent_inode)->root;

	key.objectid = inode->i_ino;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	ret = btrfs_insert_dir_item(trans, root, name, name_len,
				    parent_inode->i_ino,
				    &key, btrfs_inode_type(inode),
				    index);
	if (ret == 0) {
		if (add_backref) {
			ret = btrfs_insert_inode_ref(trans, root,
						     name, name_len,
						     inode->i_ino,
						     parent_inode->i_ino,
						     index);
		}
		btrfs_i_size_write(parent_inode, parent_inode->i_size +
				   name_len * 2);
		parent_inode->i_mtime = parent_inode->i_ctime = CURRENT_TIME;
		ret = btrfs_update_inode(trans, root, parent_inode);
	}
	return ret;
}

static int btrfs_add_nondir(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode,
			    int backref, u64 index)
{
	int err = btrfs_add_link(trans, dentry->d_parent->d_inode,
				 inode, dentry->d_name.name,
				 dentry->d_name.len, backref, index);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	if (err > 0)
		err = -EEXIST;
	return err;
}

static int btrfs_mknod(struct inode *dir, struct dentry *dentry,
			int mode, dev_t rdev)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	u64 objectid;
	unsigned long nr = 0;
	u64 index = 0;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	err = btrfs_check_metadata_free_space(root);
	if (err)
		goto fail;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino, objectid,
				BTRFS_I(dir)->block_group, mode, &index);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	err = btrfs_init_inode_security(inode, dir);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode, 0, index);
	if (err)
		drop_inode = 1;
	else {
		inode->i_op = &btrfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		btrfs_update_inode(trans, root, inode);
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
out_unlock:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_create(struct inode *dir, struct dentry *dentry,
			int mode, struct nameidata *nd)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	unsigned long nr = 0;
	u64 objectid;
	u64 index = 0;

	err = btrfs_check_metadata_free_space(root);
	if (err)
		goto fail;
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino,
				objectid, BTRFS_I(dir)->block_group, mode,
				&index);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	err = btrfs_init_inode_security(inode, dir);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode, 0, index);
	if (err)
		drop_inode = 1;
	else {
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
out_unlock:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = old_dentry->d_inode;
	u64 index;
	unsigned long nr = 0;
	int err;
	int drop_inode = 0;

	if (inode->i_nlink == 0)
		return -ENOENT;

	btrfs_inc_nlink(inode);
	err = btrfs_check_metadata_free_space(root);
	if (err)
		goto fail;
	err = btrfs_set_inode_index(dir, &index);
	if (err)
		goto fail;

	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, dir);
	atomic_inc(&inode->i_count);

	err = btrfs_add_nondir(trans, dentry, inode, 1, index);

	if (err)
		drop_inode = 1;

	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, dir);
	err = btrfs_update_inode(trans, root, inode);

	if (err)
		drop_inode = 1;

	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int btrfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int err = 0;
	int drop_on_err = 0;
	u64 objectid = 0;
	u64 index = 0;
	unsigned long nr = 1;

	err = btrfs_check_metadata_free_space(root);
	if (err)
		goto out_unlock;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_unlock;
	}

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino, objectid,
				BTRFS_I(dir)->block_group, S_IFDIR | mode,
				&index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_fail;
	}

	drop_on_err = 1;

	err = btrfs_init_inode_security(inode, dir);
	if (err)
		goto out_fail;

	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;
	btrfs_set_trans_block_group(trans, inode);

	btrfs_i_size_write(inode, 0);
	err = btrfs_update_inode(trans, root, inode);
	if (err)
		goto out_fail;

	err = btrfs_add_link(trans, dentry->d_parent->d_inode,
				 inode, dentry->d_name.name,
				 dentry->d_name.len, 0, index);
	if (err)
		goto out_fail;

	d_instantiate(dentry, inode);
	drop_on_err = 0;
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);

out_fail:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);

out_unlock:
	if (drop_on_err)
		iput(inode);
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

/* helper for btfs_get_extent.  Given an existing extent in the tree,
 * and an extent that you want to insert, deal with overlap and insert
 * the new extent into the tree.
 */
static int merge_extent_mapping(struct extent_map_tree *em_tree,
				struct extent_map *existing,
				struct extent_map *em,
				u64 map_start, u64 map_len)
{
	u64 start_diff;

	BUG_ON(map_start < em->start || map_start >= extent_map_end(em));
	start_diff = map_start - em->start;
	em->start = map_start;
	em->len = map_len;
	if (em->block_start < EXTENT_MAP_LAST_BYTE &&
	    !test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
		em->block_start += start_diff;
		em->block_len -= start_diff;
	}
	return add_extent_mapping(em_tree, em);
}

static noinline int uncompress_inline(struct btrfs_path *path,
				      struct inode *inode, struct page *page,
				      size_t pg_offset, u64 extent_offset,
				      struct btrfs_file_extent_item *item)
{
	int ret;
	struct extent_buffer *leaf = path->nodes[0];
	char *tmp;
	size_t max_size;
	unsigned long inline_size;
	unsigned long ptr;

	WARN_ON(pg_offset != 0);
	max_size = btrfs_file_extent_ram_bytes(leaf, item);
	inline_size = btrfs_file_extent_inline_item_len(leaf,
					btrfs_item_nr(leaf, path->slots[0]));
	tmp = kmalloc(inline_size, GFP_NOFS);
	ptr = btrfs_file_extent_inline_start(item);

	read_extent_buffer(leaf, tmp, ptr, inline_size);

	max_size = min_t(unsigned long, PAGE_CACHE_SIZE, max_size);
	ret = btrfs_zlib_decompress(tmp, page, extent_offset,
				    inline_size, max_size);
	if (ret) {
		char *kaddr = kmap_atomic(page, KM_USER0);
		unsigned long copy_size = min_t(u64,
				  PAGE_CACHE_SIZE - pg_offset,
				  max_size - extent_offset);
		memset(kaddr + pg_offset, 0, copy_size);
		kunmap_atomic(kaddr, KM_USER0);
	}
	kfree(tmp);
	return 0;
}

/*
 * a bit scary, this does extent mapping from logical file offset to the disk.
 * the ugly parts come from merging extents from the disk with the in-ram
 * representation.  This gets more complex because of the data=ordered code,
 * where the in-ram extents might be locked pending data=ordered completion.
 *
 * This also copies inline extents directly into the page.
 */

struct extent_map *btrfs_get_extent(struct inode *inode, struct page *page,
				    size_t pg_offset, u64 start, u64 len,
				    int create)
{
	int ret;
	int err = 0;
	u64 bytenr;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = inode->i_ino;
	u32 found_type;
	struct btrfs_path *path = NULL;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_file_extent_item *item;
	struct extent_buffer *leaf;
	struct btrfs_key found_key;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_trans_handle *trans = NULL;
	int compressed;

again:
	spin_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (em)
		em->bdev = root->fs_info->fs_devices->latest_bdev;
	spin_unlock(&em_tree->lock);

	if (em) {
		if (em->start > start || em->start + em->len <= start)
			free_extent_map(em);
		else if (em->block_start == EXTENT_MAP_INLINE && page)
			free_extent_map(em);
		else
			goto out;
	}
	em = alloc_extent_map(GFP_NOFS);
	if (!em) {
		err = -ENOMEM;
		goto out;
	}
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	em->start = EXTENT_MAP_HOLE;
	em->orig_start = EXTENT_MAP_HOLE;
	em->len = (u64)-1;
	em->block_len = (u64)-1;

	if (!path) {
		path = btrfs_alloc_path();
		BUG_ON(!path);
	}

	ret = btrfs_lookup_file_extent(trans, root, path,
				       objectid, start, trans != NULL);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (ret != 0) {
		if (path->slots[0] == 0)
			goto not_found;
		path->slots[0]--;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);
	/* are we inside the extent that was found? */
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	found_type = btrfs_key_type(&found_key);
	if (found_key.objectid != objectid ||
	    found_type != BTRFS_EXTENT_DATA_KEY) {
		goto not_found;
	}

	found_type = btrfs_file_extent_type(leaf, item);
	extent_start = found_key.offset;
	compressed = btrfs_file_extent_compression(leaf, item);
	if (found_type == BTRFS_FILE_EXTENT_REG ||
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		extent_end = extent_start +
		       btrfs_file_extent_num_bytes(leaf, item);
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		size_t size;
		size = btrfs_file_extent_inline_len(leaf, item);
		extent_end = (extent_start + size + root->sectorsize - 1) &
			~((u64)root->sectorsize - 1);
	}

	if (start >= extent_end) {
		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				err = ret;
				goto out;
			}
			if (ret > 0)
				goto not_found;
			leaf = path->nodes[0];
		}
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != objectid ||
		    found_key.type != BTRFS_EXTENT_DATA_KEY)
			goto not_found;
		if (start + len <= found_key.offset)
			goto not_found;
		em->start = start;
		em->len = found_key.offset - start;
		goto not_found_em;
	}

	if (found_type == BTRFS_FILE_EXTENT_REG ||
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		em->start = extent_start;
		em->len = extent_end - extent_start;
		em->orig_start = extent_start -
				 btrfs_file_extent_offset(leaf, item);
		bytenr = btrfs_file_extent_disk_bytenr(leaf, item);
		if (bytenr == 0) {
			em->block_start = EXTENT_MAP_HOLE;
			goto insert;
		}
		if (compressed) {
			set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
			em->block_start = bytenr;
			em->block_len = btrfs_file_extent_disk_num_bytes(leaf,
									 item);
		} else {
			bytenr += btrfs_file_extent_offset(leaf, item);
			em->block_start = bytenr;
			em->block_len = em->len;
			if (found_type == BTRFS_FILE_EXTENT_PREALLOC)
				set_bit(EXTENT_FLAG_PREALLOC, &em->flags);
		}
		goto insert;
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		unsigned long ptr;
		char *map;
		size_t size;
		size_t extent_offset;
		size_t copy_size;

		em->block_start = EXTENT_MAP_INLINE;
		if (!page || create) {
			em->start = extent_start;
			em->len = extent_end - extent_start;
			goto out;
		}

		size = btrfs_file_extent_inline_len(leaf, item);
		extent_offset = page_offset(page) + pg_offset - extent_start;
		copy_size = min_t(u64, PAGE_CACHE_SIZE - pg_offset,
				size - extent_offset);
		em->start = extent_start + extent_offset;
		em->len = (copy_size + root->sectorsize - 1) &
			~((u64)root->sectorsize - 1);
		em->orig_start = EXTENT_MAP_INLINE;
		if (compressed)
			set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
		ptr = btrfs_file_extent_inline_start(item) + extent_offset;
		if (create == 0 && !PageUptodate(page)) {
			if (btrfs_file_extent_compression(leaf, item) ==
			    BTRFS_COMPRESS_ZLIB) {
				ret = uncompress_inline(path, inode, page,
							pg_offset,
							extent_offset, item);
				BUG_ON(ret);
			} else {
				map = kmap(page);
				read_extent_buffer(leaf, map + pg_offset, ptr,
						   copy_size);
				kunmap(page);
			}
			flush_dcache_page(page);
		} else if (create && PageUptodate(page)) {
			if (!trans) {
				kunmap(page);
				free_extent_map(em);
				em = NULL;
				btrfs_release_path(root, path);
				trans = btrfs_join_transaction(root, 1);
				goto again;
			}
			map = kmap(page);
			write_extent_buffer(leaf, map + pg_offset, ptr,
					    copy_size);
			kunmap(page);
			btrfs_mark_buffer_dirty(leaf);
		}
		set_extent_uptodate(io_tree, em->start,
				    extent_map_end(em) - 1, GFP_NOFS);
		goto insert;
	} else {
		printk(KERN_ERR "btrfs unknown found_type %d\n", found_type);
		WARN_ON(1);
	}
not_found:
	em->start = start;
	em->len = len;
not_found_em:
	em->block_start = EXTENT_MAP_HOLE;
	set_bit(EXTENT_FLAG_VACANCY, &em->flags);
insert:
	btrfs_release_path(root, path);
	if (em->start > start || extent_map_end(em) <= start) {
		printk(KERN_ERR "Btrfs: bad extent! em: [%llu %llu] passed "
		       "[%llu %llu]\n", (unsigned long long)em->start,
		       (unsigned long long)em->len,
		       (unsigned long long)start,
		       (unsigned long long)len);
		err = -EIO;
		goto out;
	}

	err = 0;
	spin_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em);
	/* it is possible that someone inserted the extent into the tree
	 * while we had the lock dropped.  It is also possible that
	 * an overlapping map exists in the tree
	 */
	if (ret == -EEXIST) {
		struct extent_map *existing;

		ret = 0;

		existing = lookup_extent_mapping(em_tree, start, len);
		if (existing && (existing->start > start ||
		    existing->start + existing->len <= start)) {
			free_extent_map(existing);
			existing = NULL;
		}
		if (!existing) {
			existing = lookup_extent_mapping(em_tree, em->start,
							 em->len);
			if (existing) {
				err = merge_extent_mapping(em_tree, existing,
							   em, start,
							   root->sectorsize);
				free_extent_map(existing);
				if (err) {
					free_extent_map(em);
					em = NULL;
				}
			} else {
				err = -EIO;
				free_extent_map(em);
				em = NULL;
			}
		} else {
			free_extent_map(em);
			em = existing;
			err = 0;
		}
	}
	spin_unlock(&em_tree->lock);
out:
	if (path)
		btrfs_free_path(path);
	if (trans) {
		ret = btrfs_end_transaction(trans, root);
		if (!err)
			err = ret;
	}
	if (err) {
		free_extent_map(em);
		WARN_ON(1);
		return ERR_PTR(err);
	}
	return em;
}

static ssize_t btrfs_direct_IO(int rw, struct kiocb *iocb,
			const struct iovec *iov, loff_t offset,
			unsigned long nr_segs)
{
	return -EINVAL;
}

static int btrfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len)
{
	return extent_fiemap(inode, fieinfo, start, len, btrfs_get_extent);
}

int btrfs_readpage(struct file *file, struct page *page)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_read_full_page(tree, page, btrfs_get_extent);
}

static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_io_tree *tree;


	if (current->flags & PF_MEMALLOC) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_write_full_page(tree, page, btrfs_get_extent, wbc);
}

int btrfs_writepages(struct address_space *mapping,
		     struct writeback_control *wbc)
{
	struct extent_io_tree *tree;

	tree = &BTRFS_I(mapping->host)->io_tree;
	return extent_writepages(tree, mapping, btrfs_get_extent, wbc);
}

static int
btrfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(mapping->host)->io_tree;
	return extent_readpages(tree, mapping, pages, nr_pages,
				btrfs_get_extent);
}
static int __btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	struct extent_io_tree *tree;
	struct extent_map_tree *map;
	int ret;

	tree = &BTRFS_I(page->mapping->host)->io_tree;
	map = &BTRFS_I(page->mapping->host)->extent_tree;
	ret = try_release_extent_mapping(map, tree, page, gfp_flags);
	if (ret == 1) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}
	return ret;
}

static int btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	if (PageWriteback(page) || PageDirty(page))
		return 0;
	return __btrfs_releasepage(page, gfp_flags & GFP_NOFS);
}

static void btrfs_invalidatepage(struct page *page, unsigned long offset)
{
	struct extent_io_tree *tree;
	struct btrfs_ordered_extent *ordered;
	u64 page_start = page_offset(page);
	u64 page_end = page_start + PAGE_CACHE_SIZE - 1;

	wait_on_page_writeback(page);
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	if (offset) {
		btrfs_releasepage(page, GFP_NOFS);
		return;
	}

	lock_extent(tree, page_start, page_end, GFP_NOFS);
	ordered = btrfs_lookup_ordered_extent(page->mapping->host,
					   page_offset(page));
	if (ordered) {
		/*
		 * IO on this page will never be started, so we need
		 * to account for any ordered extents now
		 */
		clear_extent_bit(tree, page_start, page_end,
				 EXTENT_DIRTY | EXTENT_DELALLOC |
				 EXTENT_LOCKED, 1, 0, GFP_NOFS);
		btrfs_finish_ordered_io(page->mapping->host,
					page_start, page_end);
		btrfs_put_ordered_extent(ordered);
		lock_extent(tree, page_start, page_end, GFP_NOFS);
	}
	clear_extent_bit(tree, page_start, page_end,
		 EXTENT_LOCKED | EXTENT_DIRTY | EXTENT_DELALLOC |
		 EXTENT_ORDERED,
		 1, 1, GFP_NOFS);
	__btrfs_releasepage(page, GFP_NOFS);

	ClearPageChecked(page);
	if (PagePrivate(page)) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}
}

/*
 * btrfs_page_mkwrite() is not allowed to change the file size as it gets
 * called from a page fault handler when a page is first dirtied. Hence we must
 * be careful to check for EOF conditions here. We set the page up correctly
 * for a written page which means we get ENOSPC checking when writing into
 * holes and correct delalloc and unwritten extent mapping on filesystems that
 * support these features.
 *
 * We are not allowed to take the i_mutex here so we have to play games to
 * protect against truncate races as the page could now be beyond EOF.  Because
 * vmtruncate() writes the inode size before removing pages, once we have the
 * page lock we can determine safely if the page is beyond EOF. If it is not
 * beyond EOF, then the page is guaranteed safe against truncation until we
 * unlock the page.
 */
int btrfs_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = fdentry(vma->vm_file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	char *kaddr;
	unsigned long zero_start;
	loff_t size;
	int ret;
	u64 page_start;
	u64 page_end;

	ret = btrfs_check_data_free_space(root, inode, PAGE_CACHE_SIZE);
	if (ret) {
		if (ret == -ENOMEM)
			ret = VM_FAULT_OOM;
		else /* -ENOSPC, -EIO, etc */
			ret = VM_FAULT_SIGBUS;
		goto out;
	}

	ret = VM_FAULT_NOPAGE; /* make the VM retry the fault */
again:
	lock_page(page);
	size = i_size_read(inode);
	page_start = page_offset(page);
	page_end = page_start + PAGE_CACHE_SIZE - 1;

	if ((page->mapping != inode->i_mapping) ||
	    (page_start >= size)) {
		btrfs_free_reserved_data_space(root, inode, PAGE_CACHE_SIZE);
		/* page got truncated out from underneath us */
		goto out_unlock;
	}
	wait_on_page_writeback(page);

	lock_extent(io_tree, page_start, page_end, GFP_NOFS);
	set_page_extent_mapped(page);

	/*
	 * we can't set the delalloc bits if there are pending ordered
	 * extents.  Drop our locks and wait for them to finish
	 */
	ordered = btrfs_lookup_ordered_extent(inode, page_start);
	if (ordered) {
		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		unlock_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	btrfs_set_extent_delalloc(inode, page_start, page_end);
	ret = 0;

	/* page is wholly or partially inside EOF */
	if (page_start + PAGE_CACHE_SIZE > size)
		zero_start = size & ~PAGE_CACHE_MASK;
	else
		zero_start = PAGE_CACHE_SIZE;

	if (zero_start != PAGE_CACHE_SIZE) {
		kaddr = kmap(page);
		memset(kaddr + zero_start, 0, PAGE_CACHE_SIZE - zero_start);
		flush_dcache_page(page);
		kunmap(page);
	}
	ClearPageChecked(page);
	set_page_dirty(page);
	unlock_extent(io_tree, page_start, page_end, GFP_NOFS);

out_unlock:
	unlock_page(page);
out:
	return ret;
}

static void btrfs_truncate(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;
	struct btrfs_trans_handle *trans;
	unsigned long nr;
	u64 mask = root->sectorsize - 1;

	if (!S_ISREG(inode->i_mode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	btrfs_truncate_page(inode->i_mapping, inode->i_size);
	btrfs_wait_ordered_range(inode, inode->i_size & (~mask), (u64)-1);

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	btrfs_i_size_write(inode, inode->i_size);

	ret = btrfs_orphan_add(trans, inode);
	if (ret)
		goto out;
	/* FIXME, add redo link to tree so we don't leak on crash */
	ret = btrfs_truncate_inode_items(trans, root, inode, inode->i_size,
				      BTRFS_EXTENT_DATA_KEY);
	btrfs_update_inode(trans, root, inode);

	ret = btrfs_orphan_del(trans, inode);
	BUG_ON(ret);

out:
	nr = trans->blocks_used;
	ret = btrfs_end_transaction_throttle(trans, root);
	BUG_ON(ret);
	btrfs_btree_balance_dirty(root, nr);
}

/*
 * create a new subvolume directory/inode (helper for the ioctl).
 */
int btrfs_create_subvol_root(struct btrfs_trans_handle *trans,
			     struct btrfs_root *new_root, struct dentry *dentry,
			     u64 new_dirid, u64 alloc_hint)
{
	struct inode *inode;
	int error;
	u64 index = 0;

	inode = btrfs_new_inode(trans, new_root, NULL, "..", 2, new_dirid,
				new_dirid, alloc_hint, S_IFDIR | 0700, &index);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	inode->i_nlink = 1;
	btrfs_i_size_write(inode, 0);

	error = btrfs_update_inode(trans, new_root, inode);
	if (error)
		return error;

	d_instantiate(dentry, inode);
	return 0;
}

/* helper function for file defrag and space balancing.  This
 * forces readahead on a given range of bytes in an inode
 */
unsigned long btrfs_force_ra(struct address_space *mapping,
			      struct file_ra_state *ra, struct file *file,
			      pgoff_t offset, pgoff_t last_index)
{
	pgoff_t req_size = last_index - offset + 1;

	page_cache_sync_readahead(mapping, ra, file, offset, req_size);
	return offset + req_size;
}

struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_inode *ei;

	ei = kmem_cache_alloc(btrfs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;
	ei->last_trans = 0;
	ei->logged_trans = 0;
	btrfs_ordered_inode_tree_init(&ei->ordered_tree);
	ei->i_acl = BTRFS_ACL_NOT_CACHED;
	ei->i_default_acl = BTRFS_ACL_NOT_CACHED;
	INIT_LIST_HEAD(&ei->i_orphan);
	return &ei->vfs_inode;
}

void btrfs_destroy_inode(struct inode *inode)
{
	struct btrfs_ordered_extent *ordered;
	WARN_ON(!list_empty(&inode->i_dentry));
	WARN_ON(inode->i_data.nrpages);

	if (BTRFS_I(inode)->i_acl &&
	    BTRFS_I(inode)->i_acl != BTRFS_ACL_NOT_CACHED)
		posix_acl_release(BTRFS_I(inode)->i_acl);
	if (BTRFS_I(inode)->i_default_acl &&
	    BTRFS_I(inode)->i_default_acl != BTRFS_ACL_NOT_CACHED)
		posix_acl_release(BTRFS_I(inode)->i_default_acl);

	spin_lock(&BTRFS_I(inode)->root->list_lock);
	if (!list_empty(&BTRFS_I(inode)->i_orphan)) {
		printk(KERN_ERR "BTRFS: inode %lu: inode still on the orphan"
		       " list\n", inode->i_ino);
		dump_stack();
	}
	spin_unlock(&BTRFS_I(inode)->root->list_lock);

	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, (u64)-1);
		if (!ordered)
			break;
		else {
			printk(KERN_ERR "btrfs found ordered "
			       "extent %llu %llu on inode cleanup\n",
			       (unsigned long long)ordered->file_offset,
			       (unsigned long long)ordered->len);
			btrfs_remove_ordered_extent(inode, ordered);
			btrfs_put_ordered_extent(ordered);
			btrfs_put_ordered_extent(ordered);
		}
	}
	btrfs_drop_extent_cache(inode, 0, (u64)-1, 0);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

static void init_once(void *foo)
{
	struct btrfs_inode *ei = (struct btrfs_inode *) foo;

	inode_init_once(&ei->vfs_inode);
}

void btrfs_destroy_cachep(void)
{
	if (btrfs_inode_cachep)
		kmem_cache_destroy(btrfs_inode_cachep);
	if (btrfs_trans_handle_cachep)
		kmem_cache_destroy(btrfs_trans_handle_cachep);
	if (btrfs_transaction_cachep)
		kmem_cache_destroy(btrfs_transaction_cachep);
	if (btrfs_bit_radix_cachep)
		kmem_cache_destroy(btrfs_bit_radix_cachep);
	if (btrfs_path_cachep)
		kmem_cache_destroy(btrfs_path_cachep);
}

struct kmem_cache *btrfs_cache_create(const char *name, size_t size,
				       unsigned long extra_flags,
				       void (*ctor)(void *))
{
	return kmem_cache_create(name, size, 0, (SLAB_RECLAIM_ACCOUNT |
				 SLAB_MEM_SPREAD | extra_flags), ctor);
}

int btrfs_init_cachep(void)
{
	btrfs_inode_cachep = btrfs_cache_create("btrfs_inode_cache",
					  sizeof(struct btrfs_inode),
					  0, init_once);
	if (!btrfs_inode_cachep)
		goto fail;
	btrfs_trans_handle_cachep =
			btrfs_cache_create("btrfs_trans_handle_cache",
					   sizeof(struct btrfs_trans_handle),
					   0, NULL);
	if (!btrfs_trans_handle_cachep)
		goto fail;
	btrfs_transaction_cachep = btrfs_cache_create("btrfs_transaction_cache",
					     sizeof(struct btrfs_transaction),
					     0, NULL);
	if (!btrfs_transaction_cachep)
		goto fail;
	btrfs_path_cachep = btrfs_cache_create("btrfs_path_cache",
					 sizeof(struct btrfs_path),
					 0, NULL);
	if (!btrfs_path_cachep)
		goto fail;
	btrfs_bit_radix_cachep = btrfs_cache_create("btrfs_radix", 256,
					      SLAB_DESTROY_BY_RCU, NULL);
	if (!btrfs_bit_radix_cachep)
		goto fail;
	return 0;
fail:
	btrfs_destroy_cachep();
	return -ENOMEM;
}

static int btrfs_getattr(struct vfsmount *mnt,
			 struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->dev = BTRFS_I(inode)->root->anon_super.s_dev;
	stat->blksize = PAGE_CACHE_SIZE;
	stat->blocks = (inode_get_bytes(inode) +
			BTRFS_I(inode)->delalloc_bytes) >> 9;
	return 0;
}

static int btrfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct inode *new_inode = new_dentry->d_inode;
	struct inode *old_inode = old_dentry->d_inode;
	struct timespec ctime = CURRENT_TIME;
	u64 index = 0;
	int ret;

	/* we're not allowed to rename between subvolumes */
	if (BTRFS_I(old_inode)->root->root_key.objectid !=
	    BTRFS_I(new_dir)->root->root_key.objectid)
		return -EXDEV;

	if (S_ISDIR(old_inode->i_mode) && new_inode &&
	    new_inode->i_size > BTRFS_EMPTY_DIR_SIZE) {
		return -ENOTEMPTY;
	}

	/* to rename a snapshot or subvolume, we need to juggle the
	 * backrefs.  This isn't coded yet
	 */
	if (old_inode->i_ino == BTRFS_FIRST_FREE_OBJECTID)
		return -EXDEV;

	ret = btrfs_check_metadata_free_space(root);
	if (ret)
		goto out_unlock;

	trans = btrfs_start_transaction(root, 1);

	btrfs_set_trans_block_group(trans, new_dir);

	btrfs_inc_nlink(old_dentry->d_inode);
	old_dir->i_ctime = old_dir->i_mtime = ctime;
	new_dir->i_ctime = new_dir->i_mtime = ctime;
	old_inode->i_ctime = ctime;

	ret = btrfs_unlink_inode(trans, root, old_dir, old_dentry->d_inode,
				 old_dentry->d_name.name,
				 old_dentry->d_name.len);
	if (ret)
		goto out_fail;

	if (new_inode) {
		new_inode->i_ctime = CURRENT_TIME;
		ret = btrfs_unlink_inode(trans, root, new_dir,
					 new_dentry->d_inode,
					 new_dentry->d_name.name,
					 new_dentry->d_name.len);
		if (ret)
			goto out_fail;
		if (new_inode->i_nlink == 0) {
			ret = btrfs_orphan_add(trans, new_dentry->d_inode);
			if (ret)
				goto out_fail;
		}

	}
	ret = btrfs_set_inode_index(new_dir, &index);
	if (ret)
		goto out_fail;

	ret = btrfs_add_link(trans, new_dentry->d_parent->d_inode,
			     old_inode, new_dentry->d_name.name,
			     new_dentry->d_name.len, 1, index);
	if (ret)
		goto out_fail;

out_fail:
	btrfs_end_transaction_throttle(trans, root);
out_unlock:
	return ret;
}

/*
 * some fairly slow code that needs optimization. This walks the list
 * of all the inodes with pending delalloc and forces them to disk.
 */
int btrfs_start_delalloc_inodes(struct btrfs_root *root)
{
	struct list_head *head = &root->fs_info->delalloc_inodes;
	struct btrfs_inode *binode;
	struct inode *inode;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	spin_lock(&root->fs_info->delalloc_lock);
	while (!list_empty(head)) {
		binode = list_entry(head->next, struct btrfs_inode,
				    delalloc_inodes);
		inode = igrab(&binode->vfs_inode);
		if (!inode)
			list_del_init(&binode->delalloc_inodes);
		spin_unlock(&root->fs_info->delalloc_lock);
		if (inode) {
			filemap_flush(inode->i_mapping);
			iput(inode);
		}
		cond_resched();
		spin_lock(&root->fs_info->delalloc_lock);
	}
	spin_unlock(&root->fs_info->delalloc_lock);

	/* the filemap_flush will queue IO into the worker threads, but
	 * we have to make sure the IO is actually started and that
	 * ordered extents get created before we return
	 */
	atomic_inc(&root->fs_info->async_submit_draining);
	while (atomic_read(&root->fs_info->nr_async_submits) ||
	      atomic_read(&root->fs_info->async_delalloc_pages)) {
		wait_event(root->fs_info->async_submit_wait,
		   (atomic_read(&root->fs_info->nr_async_submits) == 0 &&
		    atomic_read(&root->fs_info->async_delalloc_pages) == 0));
	}
	atomic_dec(&root->fs_info->async_submit_draining);
	return 0;
}

static int btrfs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	u64 objectid;
	u64 index = 0 ;
	int name_len;
	int datasize;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	struct extent_buffer *leaf;
	unsigned long nr = 0;

	name_len = strlen(symname) + 1;
	if (name_len > BTRFS_MAX_INLINE_DATA_SIZE(root))
		return -ENAMETOOLONG;

	err = btrfs_check_metadata_free_space(root);
	if (err)
		goto out_fail;

	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len,
				dentry->d_parent->d_inode->i_ino, objectid,
				BTRFS_I(dir)->block_group, S_IFLNK|S_IRWXUGO,
				&index);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	err = btrfs_init_inode_security(inode, dir);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode, 0, index);
	if (err)
		drop_inode = 1;
	else {
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
	if (drop_inode)
		goto out_unlock;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	key.objectid = inode->i_ino;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (err) {
		drop_inode = 1;
		goto out_unlock;
	}
	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(leaf, ei, trans->transid);
	btrfs_set_file_extent_type(leaf, ei,
				   BTRFS_FILE_EXTENT_INLINE);
	btrfs_set_file_extent_encryption(leaf, ei, 0);
	btrfs_set_file_extent_compression(leaf, ei, 0);
	btrfs_set_file_extent_other_encoding(leaf, ei, 0);
	btrfs_set_file_extent_ram_bytes(leaf, ei, name_len);

	ptr = btrfs_file_extent_inline_start(ei);
	write_extent_buffer(leaf, symname, ptr, name_len);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_free_path(path);

	inode->i_op = &btrfs_symlink_inode_operations;
	inode->i_mapping->a_ops = &btrfs_symlink_aops;
	inode->i_mapping->backing_dev_info = &root->fs_info->bdi;
	inode_set_bytes(inode, name_len);
	btrfs_i_size_write(inode, name_len - 1);
	err = btrfs_update_inode(trans, root, inode);
	if (err)
		drop_inode = 1;

out_unlock:
	nr = trans->blocks_used;
	btrfs_end_transaction_throttle(trans, root);
out_fail:
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root, nr);
	return err;
}

static int prealloc_file_range(struct inode *inode, u64 start, u64 end,
			       u64 alloc_hint, int mode)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key ins;
	u64 alloc_size;
	u64 cur_offset = start;
	u64 num_bytes = end - start;
	int ret = 0;

	trans = btrfs_join_transaction(root, 1);
	BUG_ON(!trans);
	btrfs_set_trans_block_group(trans, inode);

	while (num_bytes > 0) {
		alloc_size = min(num_bytes, root->fs_info->max_extent);
		ret = btrfs_reserve_extent(trans, root, alloc_size,
					   root->sectorsize, 0, alloc_hint,
					   (u64)-1, &ins, 1);
		if (ret) {
			WARN_ON(1);
			goto out;
		}
		ret = insert_reserved_file_extent(trans, inode,
						  cur_offset, ins.objectid,
						  ins.offset, ins.offset,
						  ins.offset, 0, 0, 0,
						  BTRFS_FILE_EXTENT_PREALLOC);
		BUG_ON(ret);
		num_bytes -= ins.offset;
		cur_offset += ins.offset;
		alloc_hint = ins.objectid + ins.offset;
	}
out:
	if (cur_offset > start) {
		inode->i_ctime = CURRENT_TIME;
		btrfs_set_flag(inode, PREALLOC);
		if (!(mode & FALLOC_FL_KEEP_SIZE) &&
		    cur_offset > i_size_read(inode))
			btrfs_i_size_write(inode, cur_offset);
		ret = btrfs_update_inode(trans, root, inode);
		BUG_ON(ret);
	}

	btrfs_end_transaction(trans, root);
	return ret;
}

static long btrfs_fallocate(struct inode *inode, int mode,
			    loff_t offset, loff_t len)
{
	u64 cur_offset;
	u64 last_byte;
	u64 alloc_start;
	u64 alloc_end;
	u64 alloc_hint = 0;
	u64 mask = BTRFS_I(inode)->root->sectorsize - 1;
	struct extent_map *em;
	int ret;

	alloc_start = offset & ~mask;
	alloc_end =  (offset + len + mask) & ~mask;

	mutex_lock(&inode->i_mutex);
	if (alloc_start > inode->i_size) {
		ret = btrfs_cont_expand(inode, alloc_start);
		if (ret)
			goto out;
	}

	while (1) {
		struct btrfs_ordered_extent *ordered;
		lock_extent(&BTRFS_I(inode)->io_tree, alloc_start,
			    alloc_end - 1, GFP_NOFS);
		ordered = btrfs_lookup_first_ordered_extent(inode,
							    alloc_end - 1);
		if (ordered &&
		    ordered->file_offset + ordered->len > alloc_start &&
		    ordered->file_offset < alloc_end) {
			btrfs_put_ordered_extent(ordered);
			unlock_extent(&BTRFS_I(inode)->io_tree,
				      alloc_start, alloc_end - 1, GFP_NOFS);
			btrfs_wait_ordered_range(inode, alloc_start,
						 alloc_end - alloc_start);
		} else {
			if (ordered)
				btrfs_put_ordered_extent(ordered);
			break;
		}
	}

	cur_offset = alloc_start;
	while (1) {
		em = btrfs_get_extent(inode, NULL, 0, cur_offset,
				      alloc_end - cur_offset, 0);
		BUG_ON(IS_ERR(em) || !em);
		last_byte = min(extent_map_end(em), alloc_end);
		last_byte = (last_byte + mask) & ~mask;
		if (em->block_start == EXTENT_MAP_HOLE) {
			ret = prealloc_file_range(inode, cur_offset,
					last_byte, alloc_hint, mode);
			if (ret < 0) {
				free_extent_map(em);
				break;
			}
		}
		if (em->block_start <= EXTENT_MAP_LAST_BYTE)
			alloc_hint = em->block_start;
		free_extent_map(em);

		cur_offset = last_byte;
		if (cur_offset >= alloc_end) {
			ret = 0;
			break;
		}
	}
	unlock_extent(&BTRFS_I(inode)->io_tree, alloc_start, alloc_end - 1,
		      GFP_NOFS);
out:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

static int btrfs_set_page_dirty(struct page *page)
{
	return __set_page_dirty_nobuffers(page);
}

static int btrfs_permission(struct inode *inode, int mask)
{
	if (btrfs_test_flag(inode, READONLY) && (mask & MAY_WRITE))
		return -EACCES;
	return generic_permission(inode, mask, btrfs_check_acl);
}

static struct inode_operations btrfs_dir_inode_operations = {
	.getattr	= btrfs_getattr,
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.link		= btrfs_link,
	.mkdir		= btrfs_mkdir,
	.rmdir		= btrfs_rmdir,
	.rename		= btrfs_rename,
	.symlink	= btrfs_symlink,
	.setattr	= btrfs_setattr,
	.mknod		= btrfs_mknod,
	.setxattr	= btrfs_setxattr,
	.getxattr	= btrfs_getxattr,
	.listxattr	= btrfs_listxattr,
	.removexattr	= btrfs_removexattr,
	.permission	= btrfs_permission,
};
static struct inode_operations btrfs_dir_ro_inode_operations = {
	.lookup		= btrfs_lookup,
	.permission	= btrfs_permission,
};
static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_real_readdir,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_ioctl,
#endif
	.release        = btrfs_release_file,
	.fsync		= btrfs_sync_file,
};

static struct extent_io_ops btrfs_extent_io_ops = {
	.fill_delalloc = run_delalloc_range,
	.submit_bio_hook = btrfs_submit_bio_hook,
	.merge_bio_hook = btrfs_merge_bio_hook,
	.readpage_end_io_hook = btrfs_readpage_end_io_hook,
	.writepage_end_io_hook = btrfs_writepage_end_io_hook,
	.writepage_start_hook = btrfs_writepage_start_hook,
	.readpage_io_failed_hook = btrfs_io_failed_hook,
	.set_bit_hook = btrfs_set_bit_hook,
	.clear_bit_hook = btrfs_clear_bit_hook,
};

/*
 * btrfs doesn't support the bmap operation because swapfiles
 * use bmap to make a mapping of extents in the file.  They assume
 * these extents won't change over the life of the file and they
 * use the bmap result to do IO directly to the drive.
 *
 * the btrfs bmap call would return logical addresses that aren't
 * suitable for IO and they also will change frequently as COW
 * operations happen.  So, swapfile + btrfs == corruption.
 *
 * For now we're avoiding this by dropping bmap.
 */
static struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.writepages	= btrfs_writepages,
	.readpages	= btrfs_readpages,
	.sync_page	= block_sync_page,
	.direct_IO	= btrfs_direct_IO,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
	.set_page_dirty	= btrfs_set_page_dirty,
};

static struct address_space_operations btrfs_symlink_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
};

static struct inode_operations btrfs_file_inode_operations = {
	.truncate	= btrfs_truncate,
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.setxattr	= btrfs_setxattr,
	.getxattr	= btrfs_getxattr,
	.listxattr      = btrfs_listxattr,
	.removexattr	= btrfs_removexattr,
	.permission	= btrfs_permission,
	.fallocate	= btrfs_fallocate,
	.fiemap		= btrfs_fiemap,
};
static struct inode_operations btrfs_special_inode_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.permission	= btrfs_permission,
	.setxattr	= btrfs_setxattr,
	.getxattr	= btrfs_getxattr,
	.listxattr	= btrfs_listxattr,
	.removexattr	= btrfs_removexattr,
};
static struct inode_operations btrfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.permission	= btrfs_permission,
	.setxattr	= btrfs_setxattr,
	.getxattr	= btrfs_getxattr,
	.listxattr	= btrfs_listxattr,
	.removexattr	= btrfs_removexattr,
};
