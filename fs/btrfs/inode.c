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
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/compat.h>
#include <linux/bit_spinlock.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/falloc.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/mount.h>
#include <linux/btrfs.h>
#include <linux/blkdev.h>
#include <linux/posix_acl_xattr.h>
#include <linux/uio.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "print-tree.h"
#include "ordered-data.h"
#include "xattr.h"
#include "tree-log.h"
#include "volumes.h"
#include "compression.h"
#include "locking.h"
#include "free-space-cache.h"
#include "inode-map.h"
#include "backref.h"
#include "hash.h"
#include "props.h"
#include "qgroup.h"
#include "dedupe.h"

struct btrfs_iget_args {
	struct btrfs_key *location;
	struct btrfs_root *root;
};

struct btrfs_dio_data {
	u64 outstanding_extents;
	u64 reserve;
	u64 unsubmitted_oe_range_start;
	u64 unsubmitted_oe_range_end;
};

static const struct inode_operations btrfs_dir_inode_operations;
static const struct inode_operations btrfs_symlink_inode_operations;
static const struct inode_operations btrfs_dir_ro_inode_operations;
static const struct inode_operations btrfs_special_inode_operations;
static const struct inode_operations btrfs_file_inode_operations;
static const struct address_space_operations btrfs_aops;
static const struct address_space_operations btrfs_symlink_aops;
static const struct file_operations btrfs_dir_file_operations;
static const struct extent_io_ops btrfs_extent_io_ops;

static struct kmem_cache *btrfs_inode_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_transaction_cachep;
struct kmem_cache *btrfs_path_cachep;
struct kmem_cache *btrfs_free_space_cachep;

#define S_SHIFT 12
static const unsigned char btrfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= BTRFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= BTRFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= BTRFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= BTRFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= BTRFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= BTRFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= BTRFS_FT_SYMLINK,
};

static int btrfs_setsize(struct inode *inode, struct iattr *attr);
static int btrfs_truncate(struct inode *inode);
static int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered_extent);
static noinline int cow_file_range(struct inode *inode,
				   struct page *locked_page,
				   u64 start, u64 end, u64 delalloc_end,
				   int *page_started, unsigned long *nr_written,
				   int unlock, struct btrfs_dedupe_hash *hash);
static struct extent_map *create_pinned_em(struct inode *inode, u64 start,
					   u64 len, u64 orig_start,
					   u64 block_start, u64 block_len,
					   u64 orig_block_len, u64 ram_bytes,
					   int type);

static int btrfs_dirty_inode(struct inode *inode);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_inode_set_ops(struct inode *inode)
{
	BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
}
#endif

static int btrfs_init_inode_security(struct btrfs_trans_handle *trans,
				     struct inode *inode,  struct inode *dir,
				     const struct qstr *qstr)
{
	int err;

	err = btrfs_init_acl(trans, inode, dir);
	if (!err)
		err = btrfs_xattr_security_init(trans, inode, dir, qstr);
	return err;
}

/*
 * this does all the hard work for inserting an inline extent into
 * the btree.  The caller should have done a btrfs_drop_extents so that
 * no overlapping inline items exist in the btree
 */
static int insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_path *path, int extent_inserted,
				struct btrfs_root *root, struct inode *inode,
				u64 start, size_t size, size_t compressed_size,
				int compress_type,
				struct page **compressed_pages)
{
	struct extent_buffer *leaf;
	struct page *page = NULL;
	char *kaddr;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	int err = 0;
	int ret;
	size_t cur_size = size;
	unsigned long offset;

	if (compressed_size && compressed_pages)
		cur_size = compressed_size;

	inode_add_bytes(inode, size);

	if (!extent_inserted) {
		struct btrfs_key key;
		size_t datasize;

		key.objectid = btrfs_ino(inode);
		key.offset = start;
		key.type = BTRFS_EXTENT_DATA_KEY;

		datasize = btrfs_file_extent_calc_inline_size(cur_size);
		path->leave_spinning = 1;
		ret = btrfs_insert_empty_item(trans, root, path, &key,
					      datasize);
		if (ret) {
			err = ret;
			goto fail;
		}
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

	if (compress_type != BTRFS_COMPRESS_NONE) {
		struct page *cpage;
		int i = 0;
		while (compressed_size > 0) {
			cpage = compressed_pages[i];
			cur_size = min_t(unsigned long, compressed_size,
				       PAGE_SIZE);

			kaddr = kmap_atomic(cpage);
			write_extent_buffer(leaf, kaddr, ptr, cur_size);
			kunmap_atomic(kaddr);

			i++;
			ptr += cur_size;
			compressed_size -= cur_size;
		}
		btrfs_set_file_extent_compression(leaf, ei,
						  compress_type);
	} else {
		page = find_get_page(inode->i_mapping,
				     start >> PAGE_SHIFT);
		btrfs_set_file_extent_compression(leaf, ei, 0);
		kaddr = kmap_atomic(page);
		offset = start & (PAGE_SIZE - 1);
		write_extent_buffer(leaf, kaddr + offset, ptr, size);
		kunmap_atomic(kaddr);
		put_page(page);
	}
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	/*
	 * we're an inline extent, so nobody can
	 * extend the file past i_size without locking
	 * a page we already have locked.
	 *
	 * We must do any isize and inode updates
	 * before we unlock the pages.  Otherwise we
	 * could end up racing with unlink.
	 */
	BTRFS_I(inode)->disk_i_size = inode->i_size;
	ret = btrfs_update_inode(trans, root, inode);

	return ret;
fail:
	return err;
}


/*
 * conditionally insert an inline extent into the file.  This
 * does the checks required to make sure the data is small enough
 * to fit as an inline extent.
 */
static noinline int cow_file_range_inline(struct btrfs_root *root,
					  struct inode *inode, u64 start,
					  u64 end, size_t compressed_size,
					  int compress_type,
					  struct page **compressed_pages)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	u64 isize = i_size_read(inode);
	u64 actual_end = min(end + 1, isize);
	u64 inline_len = actual_end - start;
	u64 aligned_end = ALIGN(end, fs_info->sectorsize);
	u64 data_len = inline_len;
	int ret;
	struct btrfs_path *path;
	int extent_inserted = 0;
	u32 extent_item_size;

	if (compressed_size)
		data_len = compressed_size;

	if (start > 0 ||
	    actual_end > fs_info->sectorsize ||
	    data_len > BTRFS_MAX_INLINE_DATA_SIZE(fs_info) ||
	    (!compressed_size &&
	    (actual_end & (fs_info->sectorsize - 1)) == 0) ||
	    end + 1 < isize ||
	    data_len > fs_info->max_inline) {
		return 1;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}
	trans->block_rsv = &fs_info->delalloc_block_rsv;

	if (compressed_size && compressed_pages)
		extent_item_size = btrfs_file_extent_calc_inline_size(
		   compressed_size);
	else
		extent_item_size = btrfs_file_extent_calc_inline_size(
		    inline_len);

	ret = __btrfs_drop_extents(trans, root, inode, path,
				   start, aligned_end, NULL,
				   1, 1, extent_item_size, &extent_inserted);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	if (isize > actual_end)
		inline_len = min_t(u64, isize, actual_end);
	ret = insert_inline_extent(trans, path, extent_inserted,
				   root, inode, start,
				   inline_len, compressed_size,
				   compress_type, compressed_pages);
	if (ret && ret != -ENOSPC) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else if (ret == -ENOSPC) {
		ret = 1;
		goto out;
	}

	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(inode)->runtime_flags);
	btrfs_delalloc_release_metadata(inode, end + 1 - start);
	btrfs_drop_extent_cache(inode, start, aligned_end - 1, 0);
out:
	/*
	 * Don't forget to free the reserved space, as for inlined extent
	 * it won't count as data extent, free them directly here.
	 * And at reserve time, it's always aligned to page size, so
	 * just free one page here.
	 */
	btrfs_qgroup_free_data(inode, 0, PAGE_SIZE);
	btrfs_free_path(path);
	btrfs_end_transaction(trans);
	return ret;
}

struct async_extent {
	u64 start;
	u64 ram_size;
	u64 compressed_size;
	struct page **pages;
	unsigned long nr_pages;
	int compress_type;
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
				     unsigned long nr_pages,
				     int compress_type)
{
	struct async_extent *async_extent;

	async_extent = kmalloc(sizeof(*async_extent), GFP_NOFS);
	BUG_ON(!async_extent); /* -ENOMEM */
	async_extent->start = start;
	async_extent->ram_size = ram_size;
	async_extent->compressed_size = compressed_size;
	async_extent->pages = pages;
	async_extent->nr_pages = nr_pages;
	async_extent->compress_type = compress_type;
	list_add_tail(&async_extent->list, &cow->extents);
	return 0;
}

static inline int inode_need_compress(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);

	/* force compress */
	if (btrfs_test_opt(fs_info, FORCE_COMPRESS))
		return 1;
	/* bad compression ratios */
	if (BTRFS_I(inode)->flags & BTRFS_INODE_NOCOMPRESS)
		return 0;
	if (btrfs_test_opt(fs_info, COMPRESS) ||
	    BTRFS_I(inode)->flags & BTRFS_INODE_COMPRESS ||
	    BTRFS_I(inode)->force_compress)
		return 1;
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
 * are written in the same order that the flusher thread sent them
 * down.
 */
static noinline void compress_file_range(struct inode *inode,
					struct page *locked_page,
					u64 start, u64 end,
					struct async_cow *async_cow,
					int *num_added)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 num_bytes;
	u64 blocksize = fs_info->sectorsize;
	u64 actual_end;
	u64 isize = i_size_read(inode);
	int ret = 0;
	struct page **pages = NULL;
	unsigned long nr_pages;
	unsigned long nr_pages_ret = 0;
	unsigned long total_compressed = 0;
	unsigned long total_in = 0;
	unsigned long max_compressed = SZ_128K;
	unsigned long max_uncompressed = SZ_128K;
	int i;
	int will_compress;
	int compress_type = fs_info->compress_type;
	int redirty = 0;

	/* if this is a small write inside eof, kick off a defrag */
	if ((end - start + 1) < SZ_16K &&
	    (start > 0 || end + 1 < BTRFS_I(inode)->disk_i_size))
		btrfs_add_inode_defrag(NULL, inode);

	actual_end = min_t(u64, isize, end + 1);
again:
	will_compress = 0;
	nr_pages = (end >> PAGE_SHIFT) - (start >> PAGE_SHIFT) + 1;
	nr_pages = min_t(unsigned long, nr_pages, SZ_128K / PAGE_SIZE);

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

	/*
	 * skip compression for a small file range(<=blocksize) that
	 * isn't an inline extent, since it doesn't save disk space at all.
	 */
	if (total_compressed <= blocksize &&
	   (start > 0 || end + 1 < BTRFS_I(inode)->disk_i_size))
		goto cleanup_and_bail_uncompressed;

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
	num_bytes = ALIGN(end - start + 1, blocksize);
	num_bytes = max(blocksize,  num_bytes);
	total_in = 0;
	ret = 0;

	/*
	 * we do compression for mount -o compress and when the
	 * inode has not been flagged as nocompress.  This flag can
	 * change at any time if we discover bad compression ratios.
	 */
	if (inode_need_compress(inode)) {
		WARN_ON(pages);
		pages = kcalloc(nr_pages, sizeof(struct page *), GFP_NOFS);
		if (!pages) {
			/* just bail out to the uncompressed code */
			goto cont;
		}

		if (BTRFS_I(inode)->force_compress)
			compress_type = BTRFS_I(inode)->force_compress;

		/*
		 * we need to call clear_page_dirty_for_io on each
		 * page in the range.  Otherwise applications with the file
		 * mmap'd can wander in and change the page contents while
		 * we are compressing them.
		 *
		 * If the compression fails for any reason, we set the pages
		 * dirty again later on.
		 */
		extent_range_clear_dirty_for_io(inode, start, end);
		redirty = 1;
		ret = btrfs_compress_pages(compress_type,
					   inode->i_mapping, start,
					   total_compressed, pages,
					   nr_pages, &nr_pages_ret,
					   &total_in,
					   &total_compressed,
					   max_compressed);

		if (!ret) {
			unsigned long offset = total_compressed &
				(PAGE_SIZE - 1);
			struct page *page = pages[nr_pages_ret - 1];
			char *kaddr;

			/* zero the tail end of the last page, we might be
			 * sending it down to disk
			 */
			if (offset) {
				kaddr = kmap_atomic(page);
				memset(kaddr + offset, 0,
				       PAGE_SIZE - offset);
				kunmap_atomic(kaddr);
			}
			will_compress = 1;
		}
	}
cont:
	if (start == 0) {
		/* lets try to make an inline extent */
		if (ret || total_in < (actual_end - start)) {
			/* we didn't compress the entire range, try
			 * to make an uncompressed inline extent.
			 */
			ret = cow_file_range_inline(root, inode, start, end,
						    0, 0, NULL);
		} else {
			/* try making a compressed inline extent */
			ret = cow_file_range_inline(root, inode, start, end,
						    total_compressed,
						    compress_type, pages);
		}
		if (ret <= 0) {
			unsigned long clear_flags = EXTENT_DELALLOC |
				EXTENT_DEFRAG;
			unsigned long page_error_op;

			clear_flags |= (ret < 0) ? EXTENT_DO_ACCOUNTING : 0;
			page_error_op = ret < 0 ? PAGE_SET_ERROR : 0;

			/*
			 * inline extent creation worked or returned error,
			 * we don't need to create any more async work items.
			 * Unlock and free up our temp pages.
			 */
			extent_clear_unlock_delalloc(inode, start, end, end,
						     NULL, clear_flags,
						     PAGE_UNLOCK |
						     PAGE_CLEAR_DIRTY |
						     PAGE_SET_WRITEBACK |
						     page_error_op |
						     PAGE_END_WRITEBACK);
			btrfs_free_reserved_data_space_noquota(inode, start,
						end - start + 1);
			goto free_pages_out;
		}
	}

	if (will_compress) {
		/*
		 * we aren't doing an inline extent round the compressed size
		 * up to a block size boundary so the allocator does sane
		 * things
		 */
		total_compressed = ALIGN(total_compressed, blocksize);

		/*
		 * one last check to make sure the compression is really a
		 * win, compare the page count read with the blocks on disk
		 */
		total_in = ALIGN(total_in, PAGE_SIZE);
		if (total_compressed >= total_in) {
			will_compress = 0;
		} else {
			num_bytes = total_in;
			*num_added += 1;

			/*
			 * The async work queues will take care of doing actual
			 * allocation on disk for these compressed pages, and
			 * will submit them to the elevator.
			 */
			add_async_extent(async_cow, start, num_bytes,
					total_compressed, pages, nr_pages_ret,
					compress_type);

			if (start + num_bytes < end) {
				start += num_bytes;
				pages = NULL;
				cond_resched();
				goto again;
			}
			return;
		}
	}
	if (pages) {
		/*
		 * the compression code ran but failed to make things smaller,
		 * free any pages it allocated and our page pointer array
		 */
		for (i = 0; i < nr_pages_ret; i++) {
			WARN_ON(pages[i]->mapping);
			put_page(pages[i]);
		}
		kfree(pages);
		pages = NULL;
		total_compressed = 0;
		nr_pages_ret = 0;

		/* flag the file so we don't compress in the future */
		if (!btrfs_test_opt(fs_info, FORCE_COMPRESS) &&
		    !(BTRFS_I(inode)->force_compress)) {
			BTRFS_I(inode)->flags |= BTRFS_INODE_NOCOMPRESS;
		}
	}
cleanup_and_bail_uncompressed:
	/*
	 * No compression, but we still need to write the pages in the file
	 * we've been given so far.  redirty the locked page if it corresponds
	 * to our extent and set things up for the async work queue to run
	 * cow_file_range to do the normal delalloc dance.
	 */
	if (page_offset(locked_page) >= start &&
	    page_offset(locked_page) <= end)
		__set_page_dirty_nobuffers(locked_page);
		/* unlocked later on in the async handlers */

	if (redirty)
		extent_range_redirty_for_io(inode, start, end);
	add_async_extent(async_cow, start, end - start + 1, 0, NULL, 0,
			 BTRFS_COMPRESS_NONE);
	*num_added += 1;

	return;

free_pages_out:
	for (i = 0; i < nr_pages_ret; i++) {
		WARN_ON(pages[i]->mapping);
		put_page(pages[i]);
	}
	kfree(pages);
}

static void free_async_extent_pages(struct async_extent *async_extent)
{
	int i;

	if (!async_extent->pages)
		return;

	for (i = 0; i < async_extent->nr_pages; i++) {
		WARN_ON(async_extent->pages[i]->mapping);
		put_page(async_extent->pages[i]);
	}
	kfree(async_extent->pages);
	async_extent->nr_pages = 0;
	async_extent->pages = NULL;
}

/*
 * phase two of compressed writeback.  This is the ordered portion
 * of the code, which only gets called in the order the work was
 * queued.  We walk all the async extents created by compress_file_range
 * and send them down to the disk.
 */
static noinline void submit_compressed_extents(struct inode *inode,
					      struct async_cow *async_cow)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct async_extent *async_extent;
	u64 alloc_hint = 0;
	struct btrfs_key ins;
	struct extent_map *em;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_io_tree *io_tree;
	int ret = 0;

again:
	while (!list_empty(&async_cow->extents)) {
		async_extent = list_entry(async_cow->extents.next,
					  struct async_extent, list);
		list_del(&async_extent->list);

		io_tree = &BTRFS_I(inode)->io_tree;

retry:
		/* did the compression code fall back to uncompressed IO? */
		if (!async_extent->pages) {
			int page_started = 0;
			unsigned long nr_written = 0;

			lock_extent(io_tree, async_extent->start,
					 async_extent->start +
					 async_extent->ram_size - 1);

			/* allocate blocks */
			ret = cow_file_range(inode, async_cow->locked_page,
					     async_extent->start,
					     async_extent->start +
					     async_extent->ram_size - 1,
					     async_extent->start +
					     async_extent->ram_size - 1,
					     &page_started, &nr_written, 0,
					     NULL);

			/* JDM XXX */

			/*
			 * if page_started, cow_file_range inserted an
			 * inline extent and took care of all the unlocking
			 * and IO for us.  Otherwise, we need to submit
			 * all those pages down to the drive.
			 */
			if (!page_started && !ret)
				extent_write_locked_range(io_tree,
						  inode, async_extent->start,
						  async_extent->start +
						  async_extent->ram_size - 1,
						  btrfs_get_extent,
						  WB_SYNC_ALL);
			else if (ret)
				unlock_page(async_cow->locked_page);
			kfree(async_extent);
			cond_resched();
			continue;
		}

		lock_extent(io_tree, async_extent->start,
			    async_extent->start + async_extent->ram_size - 1);

		ret = btrfs_reserve_extent(root, async_extent->ram_size,
					   async_extent->compressed_size,
					   async_extent->compressed_size,
					   0, alloc_hint, &ins, 1, 1);
		if (ret) {
			free_async_extent_pages(async_extent);

			if (ret == -ENOSPC) {
				unlock_extent(io_tree, async_extent->start,
					      async_extent->start +
					      async_extent->ram_size - 1);

				/*
				 * we need to redirty the pages if we decide to
				 * fallback to uncompressed IO, otherwise we
				 * will not submit these pages down to lower
				 * layers.
				 */
				extent_range_redirty_for_io(inode,
						async_extent->start,
						async_extent->start +
						async_extent->ram_size - 1);

				goto retry;
			}
			goto out_free;
		}
		/*
		 * here we're doing allocation and writeback of the
		 * compressed pages
		 */
		btrfs_drop_extent_cache(inode, async_extent->start,
					async_extent->start +
					async_extent->ram_size - 1, 0);

		em = alloc_extent_map();
		if (!em) {
			ret = -ENOMEM;
			goto out_free_reserve;
		}
		em->start = async_extent->start;
		em->len = async_extent->ram_size;
		em->orig_start = em->start;
		em->mod_start = em->start;
		em->mod_len = em->len;

		em->block_start = ins.objectid;
		em->block_len = ins.offset;
		em->orig_block_len = ins.offset;
		em->ram_bytes = async_extent->ram_size;
		em->bdev = fs_info->fs_devices->latest_bdev;
		em->compress_type = async_extent->compress_type;
		set_bit(EXTENT_FLAG_PINNED, &em->flags);
		set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
		em->generation = -1;

		while (1) {
			write_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em, 1);
			write_unlock(&em_tree->lock);
			if (ret != -EEXIST) {
				free_extent_map(em);
				break;
			}
			btrfs_drop_extent_cache(inode, async_extent->start,
						async_extent->start +
						async_extent->ram_size - 1, 0);
		}

		if (ret)
			goto out_free_reserve;

		ret = btrfs_add_ordered_extent_compress(inode,
						async_extent->start,
						ins.objectid,
						async_extent->ram_size,
						ins.offset,
						BTRFS_ORDERED_COMPRESSED,
						async_extent->compress_type);
		if (ret) {
			btrfs_drop_extent_cache(inode, async_extent->start,
						async_extent->start +
						async_extent->ram_size - 1, 0);
			goto out_free_reserve;
		}
		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		/*
		 * clear dirty, set writeback and unlock the pages.
		 */
		extent_clear_unlock_delalloc(inode, async_extent->start,
				async_extent->start +
				async_extent->ram_size - 1,
				async_extent->start +
				async_extent->ram_size - 1,
				NULL, EXTENT_LOCKED | EXTENT_DELALLOC,
				PAGE_UNLOCK | PAGE_CLEAR_DIRTY |
				PAGE_SET_WRITEBACK);
		ret = btrfs_submit_compressed_write(inode,
				    async_extent->start,
				    async_extent->ram_size,
				    ins.objectid,
				    ins.offset, async_extent->pages,
				    async_extent->nr_pages);
		if (ret) {
			struct extent_io_tree *tree = &BTRFS_I(inode)->io_tree;
			struct page *p = async_extent->pages[0];
			const u64 start = async_extent->start;
			const u64 end = start + async_extent->ram_size - 1;

			p->mapping = inode->i_mapping;
			tree->ops->writepage_end_io_hook(p, start, end,
							 NULL, 0);
			p->mapping = NULL;
			extent_clear_unlock_delalloc(inode, start, end, end,
						     NULL, 0,
						     PAGE_END_WRITEBACK |
						     PAGE_SET_ERROR);
			free_async_extent_pages(async_extent);
		}
		alloc_hint = ins.objectid + ins.offset;
		kfree(async_extent);
		cond_resched();
	}
	return;
out_free_reserve:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
out_free:
	extent_clear_unlock_delalloc(inode, async_extent->start,
				     async_extent->start +
				     async_extent->ram_size - 1,
				     async_extent->start +
				     async_extent->ram_size - 1,
				     NULL, EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DEFRAG | EXTENT_DO_ACCOUNTING,
				     PAGE_UNLOCK | PAGE_CLEAR_DIRTY |
				     PAGE_SET_WRITEBACK | PAGE_END_WRITEBACK |
				     PAGE_SET_ERROR);
	free_async_extent_pages(async_extent);
	kfree(async_extent);
	goto again;
}

static u64 get_extent_allocation_hint(struct inode *inode, u64 start,
				      u64 num_bytes)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	u64 alloc_hint = 0;

	read_lock(&em_tree->lock);
	em = search_extent_mapping(em_tree, start, num_bytes);
	if (em) {
		/*
		 * if block start isn't an actual block number then find the
		 * first block in this inode and use that as a hint.  If that
		 * block is also bogus then just don't worry about it.
		 */
		if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
			free_extent_map(em);
			em = search_extent_mapping(em_tree, 0, 0);
			if (em && em->block_start < EXTENT_MAP_LAST_BYTE)
				alloc_hint = em->block_start;
			if (em)
				free_extent_map(em);
		} else {
			alloc_hint = em->block_start;
			free_extent_map(em);
		}
	}
	read_unlock(&em_tree->lock);

	return alloc_hint;
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
				   u64 start, u64 end, u64 delalloc_end,
				   int *page_started, unsigned long *nr_written,
				   int unlock, struct btrfs_dedupe_hash *hash)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 alloc_hint = 0;
	u64 num_bytes;
	unsigned long ram_size;
	u64 disk_num_bytes;
	u64 cur_alloc_size;
	u64 blocksize = fs_info->sectorsize;
	struct btrfs_key ins;
	struct extent_map *em;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	int ret = 0;

	if (btrfs_is_free_space_inode(inode)) {
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		goto out_unlock;
	}

	num_bytes = ALIGN(end - start + 1, blocksize);
	num_bytes = max(blocksize,  num_bytes);
	disk_num_bytes = num_bytes;

	/* if this is a small write inside eof, kick off defrag */
	if (num_bytes < SZ_64K &&
	    (start > 0 || end + 1 < BTRFS_I(inode)->disk_i_size))
		btrfs_add_inode_defrag(NULL, inode);

	if (start == 0) {
		/* lets try to make an inline extent */
		ret = cow_file_range_inline(root, inode, start, end, 0, 0,
					    NULL);
		if (ret == 0) {
			extent_clear_unlock_delalloc(inode, start, end,
				     delalloc_end, NULL,
				     EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DEFRAG, PAGE_UNLOCK |
				     PAGE_CLEAR_DIRTY | PAGE_SET_WRITEBACK |
				     PAGE_END_WRITEBACK);
			btrfs_free_reserved_data_space_noquota(inode, start,
						end - start + 1);
			*nr_written = *nr_written +
			     (end - start + PAGE_SIZE) / PAGE_SIZE;
			*page_started = 1;
			goto out;
		} else if (ret < 0) {
			goto out_unlock;
		}
	}

	BUG_ON(disk_num_bytes >
	       btrfs_super_total_bytes(fs_info->super_copy));

	alloc_hint = get_extent_allocation_hint(inode, start, num_bytes);
	btrfs_drop_extent_cache(inode, start, start + num_bytes - 1, 0);

	while (disk_num_bytes > 0) {
		unsigned long op;

		cur_alloc_size = disk_num_bytes;
		ret = btrfs_reserve_extent(root, cur_alloc_size, cur_alloc_size,
					   fs_info->sectorsize, 0, alloc_hint,
					   &ins, 1, 1);
		if (ret < 0)
			goto out_unlock;

		em = alloc_extent_map();
		if (!em) {
			ret = -ENOMEM;
			goto out_reserve;
		}
		em->start = start;
		em->orig_start = em->start;
		ram_size = ins.offset;
		em->len = ins.offset;
		em->mod_start = em->start;
		em->mod_len = em->len;

		em->block_start = ins.objectid;
		em->block_len = ins.offset;
		em->orig_block_len = ins.offset;
		em->ram_bytes = ram_size;
		em->bdev = fs_info->fs_devices->latest_bdev;
		set_bit(EXTENT_FLAG_PINNED, &em->flags);
		em->generation = -1;

		while (1) {
			write_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em, 1);
			write_unlock(&em_tree->lock);
			if (ret != -EEXIST) {
				free_extent_map(em);
				break;
			}
			btrfs_drop_extent_cache(inode, start,
						start + ram_size - 1, 0);
		}
		if (ret)
			goto out_reserve;

		cur_alloc_size = ins.offset;
		ret = btrfs_add_ordered_extent(inode, start, ins.objectid,
					       ram_size, cur_alloc_size, 0);
		if (ret)
			goto out_drop_extent_cache;

		if (root->root_key.objectid ==
		    BTRFS_DATA_RELOC_TREE_OBJECTID) {
			ret = btrfs_reloc_clone_csums(inode, start,
						      cur_alloc_size);
			if (ret)
				goto out_drop_extent_cache;
		}

		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		if (disk_num_bytes < cur_alloc_size)
			break;

		/* we're not doing compressed IO, don't unlock the first
		 * page (which the caller expects to stay locked), don't
		 * clear any dirty bits and don't set any writeback bits
		 *
		 * Do set the Private2 bit so we know this page was properly
		 * setup for writepage
		 */
		op = unlock ? PAGE_UNLOCK : 0;
		op |= PAGE_SET_PRIVATE2;

		extent_clear_unlock_delalloc(inode, start,
					     start + ram_size - 1,
					     delalloc_end, locked_page,
					     EXTENT_LOCKED | EXTENT_DELALLOC,
					     op);
		disk_num_bytes -= cur_alloc_size;
		num_bytes -= cur_alloc_size;
		alloc_hint = ins.objectid + ins.offset;
		start += cur_alloc_size;
	}
out:
	return ret;

out_drop_extent_cache:
	btrfs_drop_extent_cache(inode, start, start + ram_size - 1, 0);
out_reserve:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
out_unlock:
	extent_clear_unlock_delalloc(inode, start, end, delalloc_end,
				     locked_page,
				     EXTENT_LOCKED | EXTENT_DO_ACCOUNTING |
				     EXTENT_DELALLOC | EXTENT_DEFRAG,
				     PAGE_UNLOCK | PAGE_CLEAR_DIRTY |
				     PAGE_SET_WRITEBACK | PAGE_END_WRITEBACK);
	goto out;
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
	if (num_added == 0) {
		btrfs_add_delayed_iput(async_cow->inode);
		async_cow->inode = NULL;
	}
}

/*
 * work queue call back to submit previously compressed pages
 */
static noinline void async_cow_submit(struct btrfs_work *work)
{
	struct btrfs_fs_info *fs_info;
	struct async_cow *async_cow;
	struct btrfs_root *root;
	unsigned long nr_pages;

	async_cow = container_of(work, struct async_cow, work);

	root = async_cow->root;
	fs_info = root->fs_info;
	nr_pages = (async_cow->end - async_cow->start + PAGE_SIZE) >>
		PAGE_SHIFT;

	/*
	 * atomic_sub_return implies a barrier for waitqueue_active
	 */
	if (atomic_sub_return(nr_pages, &fs_info->async_delalloc_pages) <
	    5 * SZ_1M &&
	    waitqueue_active(&fs_info->async_submit_wait))
		wake_up(&fs_info->async_submit_wait);

	if (async_cow->inode)
		submit_compressed_extents(async_cow->inode, async_cow);
}

static noinline void async_cow_free(struct btrfs_work *work)
{
	struct async_cow *async_cow;
	async_cow = container_of(work, struct async_cow, work);
	if (async_cow->inode)
		btrfs_add_delayed_iput(async_cow->inode);
	kfree(async_cow);
}

static int cow_file_range_async(struct inode *inode, struct page *locked_page,
				u64 start, u64 end, int *page_started,
				unsigned long *nr_written)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct async_cow *async_cow;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	unsigned long nr_pages;
	u64 cur_end;
	int limit = 10 * SZ_1M;

	clear_extent_bit(&BTRFS_I(inode)->io_tree, start, end, EXTENT_LOCKED,
			 1, 0, NULL, GFP_NOFS);
	while (start < end) {
		async_cow = kmalloc(sizeof(*async_cow), GFP_NOFS);
		BUG_ON(!async_cow); /* -ENOMEM */
		async_cow->inode = igrab(inode);
		async_cow->root = root;
		async_cow->locked_page = locked_page;
		async_cow->start = start;

		if (BTRFS_I(inode)->flags & BTRFS_INODE_NOCOMPRESS &&
		    !btrfs_test_opt(fs_info, FORCE_COMPRESS))
			cur_end = end;
		else
			cur_end = min(end, start + SZ_512K - 1);

		async_cow->end = cur_end;
		INIT_LIST_HEAD(&async_cow->extents);

		btrfs_init_work(&async_cow->work,
				btrfs_delalloc_helper,
				async_cow_start, async_cow_submit,
				async_cow_free);

		nr_pages = (cur_end - start + PAGE_SIZE) >>
			PAGE_SHIFT;
		atomic_add(nr_pages, &fs_info->async_delalloc_pages);

		btrfs_queue_work(fs_info->delalloc_workers, &async_cow->work);

		if (atomic_read(&fs_info->async_delalloc_pages) > limit) {
			wait_event(fs_info->async_submit_wait,
				   (atomic_read(&fs_info->async_delalloc_pages) <
				    limit));
		}

		while (atomic_read(&fs_info->async_submit_draining) &&
		       atomic_read(&fs_info->async_delalloc_pages)) {
			wait_event(fs_info->async_submit_wait,
				   (atomic_read(&fs_info->async_delalloc_pages) ==
				    0));
		}

		*nr_written += nr_pages;
		start = cur_end + 1;
	}
	*page_started = 1;
	return 0;
}

static noinline int csum_exist_in_range(struct btrfs_fs_info *fs_info,
					u64 bytenr, u64 num_bytes)
{
	int ret;
	struct btrfs_ordered_sum *sums;
	LIST_HEAD(list);

	ret = btrfs_lookup_csums_range(fs_info->csum_root, bytenr,
				       bytenr + num_bytes - 1, &list, 0);
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
static noinline int run_delalloc_nocow(struct inode *inode,
				       struct page *locked_page,
			      u64 start, u64 end, int *page_started, int force,
			      unsigned long *nr_written)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key found_key;
	u64 cow_start;
	u64 cur_offset;
	u64 extent_end;
	u64 extent_offset;
	u64 disk_bytenr;
	u64 num_bytes;
	u64 disk_num_bytes;
	u64 ram_bytes;
	int extent_type;
	int ret, err;
	int type;
	int nocow;
	int check_prev = 1;
	bool nolock;
	u64 ino = btrfs_ino(inode);

	path = btrfs_alloc_path();
	if (!path) {
		extent_clear_unlock_delalloc(inode, start, end, end,
					     locked_page,
					     EXTENT_LOCKED | EXTENT_DELALLOC |
					     EXTENT_DO_ACCOUNTING |
					     EXTENT_DEFRAG, PAGE_UNLOCK |
					     PAGE_CLEAR_DIRTY |
					     PAGE_SET_WRITEBACK |
					     PAGE_END_WRITEBACK);
		return -ENOMEM;
	}

	nolock = btrfs_is_free_space_inode(inode);

	if (nolock)
		trans = btrfs_join_transaction_nolock(root);
	else
		trans = btrfs_join_transaction(root);

	if (IS_ERR(trans)) {
		extent_clear_unlock_delalloc(inode, start, end, end,
					     locked_page,
					     EXTENT_LOCKED | EXTENT_DELALLOC |
					     EXTENT_DO_ACCOUNTING |
					     EXTENT_DEFRAG, PAGE_UNLOCK |
					     PAGE_CLEAR_DIRTY |
					     PAGE_SET_WRITEBACK |
					     PAGE_END_WRITEBACK);
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	trans->block_rsv = &fs_info->delalloc_block_rsv;

	cow_start = (u64)-1;
	cur_offset = start;
	while (1) {
		ret = btrfs_lookup_file_extent(trans, root, path, ino,
					       cur_offset, 0);
		if (ret < 0)
			goto error;
		if (ret > 0 && path->slots[0] > 0 && check_prev) {
			leaf = path->nodes[0];
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0] - 1);
			if (found_key.objectid == ino &&
			    found_key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}
		check_prev = 0;
next_slot:
		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto error;
			if (ret > 0)
				break;
			leaf = path->nodes[0];
		}

		nocow = 0;
		disk_bytenr = 0;
		num_bytes = 0;
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (found_key.objectid > ino)
			break;
		if (WARN_ON_ONCE(found_key.objectid < ino) ||
		    found_key.type < BTRFS_EXTENT_DATA_KEY) {
			path->slots[0]++;
			goto next_slot;
		}
		if (found_key.type > BTRFS_EXTENT_DATA_KEY ||
		    found_key.offset > end)
			break;

		if (found_key.offset > cur_offset) {
			extent_end = found_key.offset;
			extent_type = 0;
			goto out_check;
		}

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(leaf, fi);

		ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
		if (extent_type == BTRFS_FILE_EXTENT_REG ||
		    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
			extent_offset = btrfs_file_extent_offset(leaf, fi);
			extent_end = found_key.offset +
				btrfs_file_extent_num_bytes(leaf, fi);
			disk_num_bytes =
				btrfs_file_extent_disk_num_bytes(leaf, fi);
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
			if (btrfs_extent_readonly(fs_info, disk_bytenr))
				goto out_check;
			if (btrfs_cross_ref_exist(trans, root, ino,
						  found_key.offset -
						  extent_offset, disk_bytenr))
				goto out_check;
			disk_bytenr += extent_offset;
			disk_bytenr += cur_offset - found_key.offset;
			num_bytes = min(end + 1, extent_end) - cur_offset;
			/*
			 * if there are pending snapshots for this root,
			 * we fall into common COW way.
			 */
			if (!nolock) {
				err = btrfs_start_write_no_snapshoting(root);
				if (!err)
					goto out_check;
			}
			/*
			 * force cow if csum exists in the range.
			 * this ensure that csum for a given extent are
			 * either valid or do not exist.
			 */
			if (csum_exist_in_range(fs_info, disk_bytenr,
						num_bytes))
				goto out_check;
			if (!btrfs_inc_nocow_writers(fs_info, disk_bytenr))
				goto out_check;
			nocow = 1;
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			extent_end = found_key.offset +
				btrfs_file_extent_inline_len(leaf,
						     path->slots[0], fi);
			extent_end = ALIGN(extent_end,
					   fs_info->sectorsize);
		} else {
			BUG_ON(1);
		}
out_check:
		if (extent_end <= start) {
			path->slots[0]++;
			if (!nolock && nocow)
				btrfs_end_write_no_snapshoting(root);
			if (nocow)
				btrfs_dec_nocow_writers(fs_info, disk_bytenr);
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

		btrfs_release_path(path);
		if (cow_start != (u64)-1) {
			ret = cow_file_range(inode, locked_page,
					     cow_start, found_key.offset - 1,
					     end, page_started, nr_written, 1,
					     NULL);
			if (ret) {
				if (!nolock && nocow)
					btrfs_end_write_no_snapshoting(root);
				if (nocow)
					btrfs_dec_nocow_writers(fs_info,
								disk_bytenr);
				goto error;
			}
			cow_start = (u64)-1;
		}

		if (extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			struct extent_map *em;
			struct extent_map_tree *em_tree;
			em_tree = &BTRFS_I(inode)->extent_tree;
			em = alloc_extent_map();
			BUG_ON(!em); /* -ENOMEM */
			em->start = cur_offset;
			em->orig_start = found_key.offset - extent_offset;
			em->len = num_bytes;
			em->block_len = num_bytes;
			em->block_start = disk_bytenr;
			em->orig_block_len = disk_num_bytes;
			em->ram_bytes = ram_bytes;
			em->bdev = fs_info->fs_devices->latest_bdev;
			em->mod_start = em->start;
			em->mod_len = em->len;
			set_bit(EXTENT_FLAG_PINNED, &em->flags);
			set_bit(EXTENT_FLAG_FILLING, &em->flags);
			em->generation = -1;
			while (1) {
				write_lock(&em_tree->lock);
				ret = add_extent_mapping(em_tree, em, 1);
				write_unlock(&em_tree->lock);
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
		if (nocow)
			btrfs_dec_nocow_writers(fs_info, disk_bytenr);
		BUG_ON(ret); /* -ENOMEM */

		if (root->root_key.objectid ==
		    BTRFS_DATA_RELOC_TREE_OBJECTID) {
			ret = btrfs_reloc_clone_csums(inode, cur_offset,
						      num_bytes);
			if (ret) {
				if (!nolock && nocow)
					btrfs_end_write_no_snapshoting(root);
				goto error;
			}
		}

		extent_clear_unlock_delalloc(inode, cur_offset,
					     cur_offset + num_bytes - 1, end,
					     locked_page, EXTENT_LOCKED |
					     EXTENT_DELALLOC |
					     EXTENT_CLEAR_DATA_RESV,
					     PAGE_UNLOCK | PAGE_SET_PRIVATE2);

		if (!nolock && nocow)
			btrfs_end_write_no_snapshoting(root);
		cur_offset = extent_end;
		if (cur_offset > end)
			break;
	}
	btrfs_release_path(path);

	if (cur_offset <= end && cow_start == (u64)-1) {
		cow_start = cur_offset;
		cur_offset = end;
	}

	if (cow_start != (u64)-1) {
		ret = cow_file_range(inode, locked_page, cow_start, end, end,
				     page_started, nr_written, 1, NULL);
		if (ret)
			goto error;
	}

error:
	err = btrfs_end_transaction(trans);
	if (!ret)
		ret = err;

	if (ret && cur_offset < end)
		extent_clear_unlock_delalloc(inode, cur_offset, end, end,
					     locked_page, EXTENT_LOCKED |
					     EXTENT_DELALLOC | EXTENT_DEFRAG |
					     EXTENT_DO_ACCOUNTING, PAGE_UNLOCK |
					     PAGE_CLEAR_DIRTY |
					     PAGE_SET_WRITEBACK |
					     PAGE_END_WRITEBACK);
	btrfs_free_path(path);
	return ret;
}

static inline int need_force_cow(struct inode *inode, u64 start, u64 end)
{

	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW) &&
	    !(BTRFS_I(inode)->flags & BTRFS_INODE_PREALLOC))
		return 0;

	/*
	 * @defrag_bytes is a hint value, no spinlock held here,
	 * if is not zero, it means the file is defragging.
	 * Force cow if given extent needs to be defragged.
	 */
	if (BTRFS_I(inode)->defrag_bytes &&
	    test_range_bit(&BTRFS_I(inode)->io_tree, start, end,
			   EXTENT_DEFRAG, 0, NULL))
		return 1;

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
	int force_cow = need_force_cow(inode, start, end);

	if (BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW && !force_cow) {
		ret = run_delalloc_nocow(inode, locked_page, start, end,
					 page_started, 1, nr_written);
	} else if (BTRFS_I(inode)->flags & BTRFS_INODE_PREALLOC && !force_cow) {
		ret = run_delalloc_nocow(inode, locked_page, start, end,
					 page_started, 0, nr_written);
	} else if (!inode_need_compress(inode)) {
		ret = cow_file_range(inode, locked_page, start, end, end,
				      page_started, nr_written, 1, NULL);
	} else {
		set_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
			&BTRFS_I(inode)->runtime_flags);
		ret = cow_file_range_async(inode, locked_page, start, end,
					   page_started, nr_written);
	}
	return ret;
}

static void btrfs_split_extent_hook(struct inode *inode,
				    struct extent_state *orig, u64 split)
{
	u64 size;

	/* not delalloc, ignore it */
	if (!(orig->state & EXTENT_DELALLOC))
		return;

	size = orig->end - orig->start + 1;
	if (size > BTRFS_MAX_EXTENT_SIZE) {
		u64 num_extents;
		u64 new_size;

		/*
		 * See the explanation in btrfs_merge_extent_hook, the same
		 * applies here, just in reverse.
		 */
		new_size = orig->end - split + 1;
		num_extents = div64_u64(new_size + BTRFS_MAX_EXTENT_SIZE - 1,
					BTRFS_MAX_EXTENT_SIZE);
		new_size = split - orig->start;
		num_extents += div64_u64(new_size + BTRFS_MAX_EXTENT_SIZE - 1,
					BTRFS_MAX_EXTENT_SIZE);
		if (div64_u64(size + BTRFS_MAX_EXTENT_SIZE - 1,
			      BTRFS_MAX_EXTENT_SIZE) >= num_extents)
			return;
	}

	spin_lock(&BTRFS_I(inode)->lock);
	BTRFS_I(inode)->outstanding_extents++;
	spin_unlock(&BTRFS_I(inode)->lock);
}

/*
 * extent_io.c merge_extent_hook, used to track merged delayed allocation
 * extents so we can keep track of new extents that are just merged onto old
 * extents, such as when we are doing sequential writes, so we can properly
 * account for the metadata space we'll need.
 */
static void btrfs_merge_extent_hook(struct inode *inode,
				    struct extent_state *new,
				    struct extent_state *other)
{
	u64 new_size, old_size;
	u64 num_extents;

	/* not delalloc, ignore it */
	if (!(other->state & EXTENT_DELALLOC))
		return;

	if (new->start > other->start)
		new_size = new->end - other->start + 1;
	else
		new_size = other->end - new->start + 1;

	/* we're not bigger than the max, unreserve the space and go */
	if (new_size <= BTRFS_MAX_EXTENT_SIZE) {
		spin_lock(&BTRFS_I(inode)->lock);
		BTRFS_I(inode)->outstanding_extents--;
		spin_unlock(&BTRFS_I(inode)->lock);
		return;
	}

	/*
	 * We have to add up either side to figure out how many extents were
	 * accounted for before we merged into one big extent.  If the number of
	 * extents we accounted for is <= the amount we need for the new range
	 * then we can return, otherwise drop.  Think of it like this
	 *
	 * [ 4k][MAX_SIZE]
	 *
	 * So we've grown the extent by a MAX_SIZE extent, this would mean we
	 * need 2 outstanding extents, on one side we have 1 and the other side
	 * we have 1 so they are == and we can return.  But in this case
	 *
	 * [MAX_SIZE+4k][MAX_SIZE+4k]
	 *
	 * Each range on their own accounts for 2 extents, but merged together
	 * they are only 3 extents worth of accounting, so we need to drop in
	 * this case.
	 */
	old_size = other->end - other->start + 1;
	num_extents = div64_u64(old_size + BTRFS_MAX_EXTENT_SIZE - 1,
				BTRFS_MAX_EXTENT_SIZE);
	old_size = new->end - new->start + 1;
	num_extents += div64_u64(old_size + BTRFS_MAX_EXTENT_SIZE - 1,
				 BTRFS_MAX_EXTENT_SIZE);

	if (div64_u64(new_size + BTRFS_MAX_EXTENT_SIZE - 1,
		      BTRFS_MAX_EXTENT_SIZE) >= num_extents)
		return;

	spin_lock(&BTRFS_I(inode)->lock);
	BTRFS_I(inode)->outstanding_extents--;
	spin_unlock(&BTRFS_I(inode)->lock);
}

static void btrfs_add_delalloc_inodes(struct btrfs_root *root,
				      struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);

	spin_lock(&root->delalloc_lock);
	if (list_empty(&BTRFS_I(inode)->delalloc_inodes)) {
		list_add_tail(&BTRFS_I(inode)->delalloc_inodes,
			      &root->delalloc_inodes);
		set_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			&BTRFS_I(inode)->runtime_flags);
		root->nr_delalloc_inodes++;
		if (root->nr_delalloc_inodes == 1) {
			spin_lock(&fs_info->delalloc_root_lock);
			BUG_ON(!list_empty(&root->delalloc_root));
			list_add_tail(&root->delalloc_root,
				      &fs_info->delalloc_roots);
			spin_unlock(&fs_info->delalloc_root_lock);
		}
	}
	spin_unlock(&root->delalloc_lock);
}

static void btrfs_del_delalloc_inode(struct btrfs_root *root,
				     struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);

	spin_lock(&root->delalloc_lock);
	if (!list_empty(&BTRFS_I(inode)->delalloc_inodes)) {
		list_del_init(&BTRFS_I(inode)->delalloc_inodes);
		clear_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			  &BTRFS_I(inode)->runtime_flags);
		root->nr_delalloc_inodes--;
		if (!root->nr_delalloc_inodes) {
			spin_lock(&fs_info->delalloc_root_lock);
			BUG_ON(list_empty(&root->delalloc_root));
			list_del_init(&root->delalloc_root);
			spin_unlock(&fs_info->delalloc_root_lock);
		}
	}
	spin_unlock(&root->delalloc_lock);
}

/*
 * extent_io.c set_bit_hook, used to track delayed allocation
 * bytes in this file, and to maintain the list of inodes that
 * have pending delalloc work to be done.
 */
static void btrfs_set_bit_hook(struct inode *inode,
			       struct extent_state *state, unsigned *bits)
{

	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);

	if ((*bits & EXTENT_DEFRAG) && !(*bits & EXTENT_DELALLOC))
		WARN_ON(1);
	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if (!(state->state & EXTENT_DELALLOC) && (*bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(inode)->root;
		u64 len = state->end + 1 - state->start;
		bool do_list = !btrfs_is_free_space_inode(inode);

		if (*bits & EXTENT_FIRST_DELALLOC) {
			*bits &= ~EXTENT_FIRST_DELALLOC;
		} else {
			spin_lock(&BTRFS_I(inode)->lock);
			BTRFS_I(inode)->outstanding_extents++;
			spin_unlock(&BTRFS_I(inode)->lock);
		}

		/* For sanity tests */
		if (btrfs_is_testing(fs_info))
			return;

		__percpu_counter_add(&fs_info->delalloc_bytes, len,
				     fs_info->delalloc_batch);
		spin_lock(&BTRFS_I(inode)->lock);
		BTRFS_I(inode)->delalloc_bytes += len;
		if (*bits & EXTENT_DEFRAG)
			BTRFS_I(inode)->defrag_bytes += len;
		if (do_list && !test_bit(BTRFS_INODE_IN_DELALLOC_LIST,
					 &BTRFS_I(inode)->runtime_flags))
			btrfs_add_delalloc_inodes(root, inode);
		spin_unlock(&BTRFS_I(inode)->lock);
	}
}

/*
 * extent_io.c clear_bit_hook, see set_bit_hook for why
 */
static void btrfs_clear_bit_hook(struct inode *inode,
				 struct extent_state *state,
				 unsigned *bits)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 len = state->end + 1 - state->start;
	u64 num_extents = div64_u64(len + BTRFS_MAX_EXTENT_SIZE -1,
				    BTRFS_MAX_EXTENT_SIZE);

	spin_lock(&BTRFS_I(inode)->lock);
	if ((state->state & EXTENT_DEFRAG) && (*bits & EXTENT_DEFRAG))
		BTRFS_I(inode)->defrag_bytes -= len;
	spin_unlock(&BTRFS_I(inode)->lock);

	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if ((state->state & EXTENT_DELALLOC) && (*bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(inode)->root;
		bool do_list = !btrfs_is_free_space_inode(inode);

		if (*bits & EXTENT_FIRST_DELALLOC) {
			*bits &= ~EXTENT_FIRST_DELALLOC;
		} else if (!(*bits & EXTENT_DO_ACCOUNTING)) {
			spin_lock(&BTRFS_I(inode)->lock);
			BTRFS_I(inode)->outstanding_extents -= num_extents;
			spin_unlock(&BTRFS_I(inode)->lock);
		}

		/*
		 * We don't reserve metadata space for space cache inodes so we
		 * don't need to call dellalloc_release_metadata if there is an
		 * error.
		 */
		if (*bits & EXTENT_DO_ACCOUNTING &&
		    root != fs_info->tree_root)
			btrfs_delalloc_release_metadata(inode, len);

		/* For sanity tests. */
		if (btrfs_is_testing(fs_info))
			return;

		if (root->root_key.objectid != BTRFS_DATA_RELOC_TREE_OBJECTID
		    && do_list && !(state->state & EXTENT_NORESERVE)
		    && (*bits & (EXTENT_DO_ACCOUNTING |
		    EXTENT_CLEAR_DATA_RESV)))
			btrfs_free_reserved_data_space_noquota(inode,
					state->start, len);

		__percpu_counter_add(&fs_info->delalloc_bytes, -len,
				     fs_info->delalloc_batch);
		spin_lock(&BTRFS_I(inode)->lock);
		BTRFS_I(inode)->delalloc_bytes -= len;
		if (do_list && BTRFS_I(inode)->delalloc_bytes == 0 &&
		    test_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			     &BTRFS_I(inode)->runtime_flags))
			btrfs_del_delalloc_inode(root, inode);
		spin_unlock(&BTRFS_I(inode)->lock);
	}
}

/*
 * extent_io.c merge_bio_hook, this must check the chunk tree to make sure
 * we don't create bios that span stripes or chunks
 *
 * return 1 if page cannot be merged to bio
 * return 0 if page can be merged to bio
 * return error otherwise
 */
int btrfs_merge_bio_hook(struct page *page, unsigned long offset,
			 size_t size, struct bio *bio,
			 unsigned long bio_flags)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 logical = (u64)bio->bi_iter.bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	int ret;

	if (bio_flags & EXTENT_BIO_COMPRESSED)
		return 0;

	length = bio->bi_iter.bi_size;
	map_length = length;
	ret = btrfs_map_block(fs_info, btrfs_op(bio), logical, &map_length,
			      NULL, 0);
	if (ret < 0)
		return ret;
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
static int __btrfs_submit_bio_start(struct inode *inode, struct bio *bio,
				    int mirror_num, unsigned long bio_flags,
				    u64 bio_offset)
{
	int ret = 0;

	ret = btrfs_csum_one_bio(inode, bio, 0, 0);
	BUG_ON(ret); /* -ENOMEM */
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
static int __btrfs_submit_bio_done(struct inode *inode, struct bio *bio,
			  int mirror_num, unsigned long bio_flags,
			  u64 bio_offset)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;

	ret = btrfs_map_bio(fs_info, bio, mirror_num, 1);
	if (ret) {
		bio->bi_error = ret;
		bio_endio(bio);
	}
	return ret;
}

/*
 * extent_io.c submission hook. This does the right thing for csum calculation
 * on write, or reading the csums from the tree before a read
 */
static int btrfs_submit_bio_hook(struct inode *inode, struct bio *bio,
			  int mirror_num, unsigned long bio_flags,
			  u64 bio_offset)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	enum btrfs_wq_endio_type metadata = BTRFS_WQ_ENDIO_DATA;
	int ret = 0;
	int skip_sum;
	int async = !atomic_read(&BTRFS_I(inode)->sync_writers);

	skip_sum = BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM;

	if (btrfs_is_free_space_inode(inode))
		metadata = BTRFS_WQ_ENDIO_FREE_SPACE;

	if (bio_op(bio) != REQ_OP_WRITE) {
		ret = btrfs_bio_wq_end_io(fs_info, bio, metadata);
		if (ret)
			goto out;

		if (bio_flags & EXTENT_BIO_COMPRESSED) {
			ret = btrfs_submit_compressed_read(inode, bio,
							   mirror_num,
							   bio_flags);
			goto out;
		} else if (!skip_sum) {
			ret = btrfs_lookup_bio_sums(inode, bio, NULL);
			if (ret)
				goto out;
		}
		goto mapit;
	} else if (async && !skip_sum) {
		/* csum items have already been cloned */
		if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID)
			goto mapit;
		/* we're doing a write, do the async checksumming */
		ret = btrfs_wq_submit_bio(fs_info, inode, bio, mirror_num,
					  bio_flags, bio_offset,
					  __btrfs_submit_bio_start,
					  __btrfs_submit_bio_done);
		goto out;
	} else if (!skip_sum) {
		ret = btrfs_csum_one_bio(inode, bio, 0, 0);
		if (ret)
			goto out;
	}

mapit:
	ret = btrfs_map_bio(fs_info, bio, mirror_num, 0);

out:
	if (ret < 0) {
		bio->bi_error = ret;
		bio_endio(bio);
	}
	return ret;
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

	list_for_each_entry(sum, list, list) {
		trans->adding_csums = 1;
		btrfs_csum_file_blocks(trans,
		       BTRFS_I(inode)->root->fs_info->csum_root, sum);
		trans->adding_csums = 0;
	}
	return 0;
}

int btrfs_set_extent_delalloc(struct inode *inode, u64 start, u64 end,
			      struct extent_state **cached_state, int dedupe)
{
	WARN_ON((end & (PAGE_SIZE - 1)) == 0);
	return set_extent_delalloc(&BTRFS_I(inode)->io_tree, start, end,
				   cached_state);
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
	struct extent_state *cached_state = NULL;
	struct page *page;
	struct inode *inode;
	u64 page_start;
	u64 page_end;
	int ret;

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
	page_end = page_offset(page) + PAGE_SIZE - 1;

	lock_extent_bits(&BTRFS_I(inode)->io_tree, page_start, page_end,
			 &cached_state);

	/* already ordered? We're done */
	if (PagePrivate2(page))
		goto out;

	ordered = btrfs_lookup_ordered_range(inode, page_start,
					PAGE_SIZE);
	if (ordered) {
		unlock_extent_cached(&BTRFS_I(inode)->io_tree, page_start,
				     page_end, &cached_state, GFP_NOFS);
		unlock_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	ret = btrfs_delalloc_reserve_space(inode, page_start,
					   PAGE_SIZE);
	if (ret) {
		mapping_set_error(page->mapping, ret);
		end_extent_writepage(page, ret, page_start, page_end);
		ClearPageChecked(page);
		goto out;
	 }

	btrfs_set_extent_delalloc(inode, page_start, page_end, &cached_state,
				  0);
	ClearPageChecked(page);
	set_page_dirty(page);
out:
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, page_start, page_end,
			     &cached_state, GFP_NOFS);
out_page:
	unlock_page(page);
	put_page(page);
	kfree(fixup);
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
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_writepage_fixup *fixup;

	/* this page is properly in the ordered list */
	if (TestClearPagePrivate2(page))
		return 0;

	if (PageChecked(page))
		return -EAGAIN;

	fixup = kzalloc(sizeof(*fixup), GFP_NOFS);
	if (!fixup)
		return -EAGAIN;

	SetPageChecked(page);
	get_page(page);
	btrfs_init_work(&fixup->work, btrfs_fixup_helper,
			btrfs_writepage_fixup_worker, NULL, NULL);
	fixup->page = page;
	btrfs_queue_work(fs_info->fixup_workers, &fixup->work);
	return -EBUSY;
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
	int extent_inserted = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * we may be replacing one extent in the tree with another.
	 * The new extent is pinned in the extent map, and we don't want
	 * to drop it from the cache until it is completely in the btree.
	 *
	 * So, tell btrfs_drop_extents to leave this extent in the cache.
	 * the caller is expected to unpin it and allow it to be merged
	 * with the others.
	 */
	ret = __btrfs_drop_extents(trans, root, inode, path, file_pos,
				   file_pos + num_bytes, NULL, 0,
				   1, sizeof(*fi), &extent_inserted);
	if (ret)
		goto out;

	if (!extent_inserted) {
		ins.objectid = btrfs_ino(inode);
		ins.offset = file_pos;
		ins.type = BTRFS_EXTENT_DATA_KEY;

		path->leave_spinning = 1;
		ret = btrfs_insert_empty_item(trans, root, path, &ins,
					      sizeof(*fi));
		if (ret)
			goto out;
	}
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
	btrfs_release_path(path);

	inode_add_bytes(inode, num_bytes);

	ins.objectid = disk_bytenr;
	ins.offset = disk_num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;
	ret = btrfs_alloc_reserved_file_extent(trans, root->root_key.objectid,
					       btrfs_ino(inode), file_pos,
					       ram_bytes, &ins);
	/*
	 * Release the reserved range from inode dirty range map, as it is
	 * already moved into delayed_ref_head
	 */
	btrfs_qgroup_release_data(inode, file_pos, ram_bytes);
out:
	btrfs_free_path(path);

	return ret;
}

/* snapshot-aware defrag */
struct sa_defrag_extent_backref {
	struct rb_node node;
	struct old_sa_defrag_extent *old;
	u64 root_id;
	u64 inum;
	u64 file_pos;
	u64 extent_offset;
	u64 num_bytes;
	u64 generation;
};

struct old_sa_defrag_extent {
	struct list_head list;
	struct new_sa_defrag_extent *new;

	u64 extent_offset;
	u64 bytenr;
	u64 offset;
	u64 len;
	int count;
};

struct new_sa_defrag_extent {
	struct rb_root root;
	struct list_head head;
	struct btrfs_path *path;
	struct inode *inode;
	u64 file_pos;
	u64 len;
	u64 bytenr;
	u64 disk_len;
	u8 compress_type;
};

static int backref_comp(struct sa_defrag_extent_backref *b1,
			struct sa_defrag_extent_backref *b2)
{
	if (b1->root_id < b2->root_id)
		return -1;
	else if (b1->root_id > b2->root_id)
		return 1;

	if (b1->inum < b2->inum)
		return -1;
	else if (b1->inum > b2->inum)
		return 1;

	if (b1->file_pos < b2->file_pos)
		return -1;
	else if (b1->file_pos > b2->file_pos)
		return 1;

	/*
	 * [------------------------------] ===> (a range of space)
	 *     |<--->|   |<---->| =============> (fs/file tree A)
	 * |<---------------------------->| ===> (fs/file tree B)
	 *
	 * A range of space can refer to two file extents in one tree while
	 * refer to only one file extent in another tree.
	 *
	 * So we may process a disk offset more than one time(two extents in A)
	 * and locate at the same extent(one extent in B), then insert two same
	 * backrefs(both refer to the extent in B).
	 */
	return 0;
}

static void backref_insert(struct rb_root *root,
			   struct sa_defrag_extent_backref *backref)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct sa_defrag_extent_backref *entry;
	int ret;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct sa_defrag_extent_backref, node);

		ret = backref_comp(backref, entry);
		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&backref->node, parent, p);
	rb_insert_color(&backref->node, root);
}

/*
 * Note the backref might has changed, and in this case we just return 0.
 */
static noinline int record_one_backref(u64 inum, u64 offset, u64 root_id,
				       void *ctx)
{
	struct btrfs_file_extent_item *extent;
	struct old_sa_defrag_extent *old = ctx;
	struct new_sa_defrag_extent *new = old->new;
	struct btrfs_path *path = new->path;
	struct btrfs_key key;
	struct btrfs_root *root;
	struct sa_defrag_extent_backref *backref;
	struct extent_buffer *leaf;
	struct inode *inode = new->inode;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int slot;
	int ret;
	u64 extent_offset;
	u64 num_bytes;

	if (BTRFS_I(inode)->root->root_key.objectid == root_id &&
	    inum == btrfs_ino(inode))
		return 0;

	key.objectid = root_id;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	root = btrfs_read_fs_root_no_name(fs_info, &key);
	if (IS_ERR(root)) {
		if (PTR_ERR(root) == -ENOENT)
			return 0;
		WARN_ON(1);
		btrfs_debug(fs_info, "inum=%llu, offset=%llu, root_id=%llu",
			 inum, offset, root_id);
		return PTR_ERR(root);
	}

	key.objectid = inum;
	key.type = BTRFS_EXTENT_DATA_KEY;
	if (offset > (u64)-1 << 32)
		key.offset = 0;
	else
		key.offset = offset;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (WARN_ON(ret < 0))
		return ret;
	ret = 0;

	while (1) {
		cond_resched();

		leaf = path->nodes[0];
		slot = path->slots[0];

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = 0;
				goto out;
			}
			continue;
		}

		path->slots[0]++;

		btrfs_item_key_to_cpu(leaf, &key, slot);

		if (key.objectid > inum)
			goto out;

		if (key.objectid < inum || key.type != BTRFS_EXTENT_DATA_KEY)
			continue;

		extent = btrfs_item_ptr(leaf, slot,
					struct btrfs_file_extent_item);

		if (btrfs_file_extent_disk_bytenr(leaf, extent) != old->bytenr)
			continue;

		/*
		 * 'offset' refers to the exact key.offset,
		 * NOT the 'offset' field in btrfs_extent_data_ref, ie.
		 * (key.offset - extent_offset).
		 */
		if (key.offset != offset)
			continue;

		extent_offset = btrfs_file_extent_offset(leaf, extent);
		num_bytes = btrfs_file_extent_num_bytes(leaf, extent);

		if (extent_offset >= old->extent_offset + old->offset +
		    old->len || extent_offset + num_bytes <=
		    old->extent_offset + old->offset)
			continue;
		break;
	}

	backref = kmalloc(sizeof(*backref), GFP_NOFS);
	if (!backref) {
		ret = -ENOENT;
		goto out;
	}

	backref->root_id = root_id;
	backref->inum = inum;
	backref->file_pos = offset;
	backref->num_bytes = num_bytes;
	backref->extent_offset = extent_offset;
	backref->generation = btrfs_file_extent_generation(leaf, extent);
	backref->old = old;
	backref_insert(&new->root, backref);
	old->count++;
out:
	btrfs_release_path(path);
	WARN_ON(ret);
	return ret;
}

static noinline bool record_extent_backrefs(struct btrfs_path *path,
				   struct new_sa_defrag_extent *new)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(new->inode->i_sb);
	struct old_sa_defrag_extent *old, *tmp;
	int ret;

	new->path = path;

	list_for_each_entry_safe(old, tmp, &new->head, list) {
		ret = iterate_inodes_from_logical(old->bytenr +
						  old->extent_offset, fs_info,
						  path, record_one_backref,
						  old);
		if (ret < 0 && ret != -ENOENT)
			return false;

		/* no backref to be processed for this extent */
		if (!old->count) {
			list_del(&old->list);
			kfree(old);
		}
	}

	if (list_empty(&new->head))
		return false;

	return true;
}

static int relink_is_mergable(struct extent_buffer *leaf,
			      struct btrfs_file_extent_item *fi,
			      struct new_sa_defrag_extent *new)
{
	if (btrfs_file_extent_disk_bytenr(leaf, fi) != new->bytenr)
		return 0;

	if (btrfs_file_extent_type(leaf, fi) != BTRFS_FILE_EXTENT_REG)
		return 0;

	if (btrfs_file_extent_compression(leaf, fi) != new->compress_type)
		return 0;

	if (btrfs_file_extent_encryption(leaf, fi) ||
	    btrfs_file_extent_other_encoding(leaf, fi))
		return 0;

	return 1;
}

/*
 * Note the backref might has changed, and in this case we just return 0.
 */
static noinline int relink_extent_backref(struct btrfs_path *path,
				 struct sa_defrag_extent_backref *prev,
				 struct sa_defrag_extent_backref *backref)
{
	struct btrfs_file_extent_item *extent;
	struct btrfs_file_extent_item *item;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct old_sa_defrag_extent *old = backref->old;
	struct new_sa_defrag_extent *new = old->new;
	struct btrfs_fs_info *fs_info = btrfs_sb(new->inode->i_sb);
	struct inode *inode;
	struct extent_state *cached = NULL;
	int ret = 0;
	u64 start;
	u64 len;
	u64 lock_start;
	u64 lock_end;
	bool merge = false;
	int index;

	if (prev && prev->root_id == backref->root_id &&
	    prev->inum == backref->inum &&
	    prev->file_pos + prev->num_bytes == backref->file_pos)
		merge = true;

	/* step 1: get root */
	key.objectid = backref->root_id;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	index = srcu_read_lock(&fs_info->subvol_srcu);

	root = btrfs_read_fs_root_no_name(fs_info, &key);
	if (IS_ERR(root)) {
		srcu_read_unlock(&fs_info->subvol_srcu, index);
		if (PTR_ERR(root) == -ENOENT)
			return 0;
		return PTR_ERR(root);
	}

	if (btrfs_root_readonly(root)) {
		srcu_read_unlock(&fs_info->subvol_srcu, index);
		return 0;
	}

	/* step 2: get inode */
	key.objectid = backref->inum;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	inode = btrfs_iget(fs_info->sb, &key, root, NULL);
	if (IS_ERR(inode)) {
		srcu_read_unlock(&fs_info->subvol_srcu, index);
		return 0;
	}

	srcu_read_unlock(&fs_info->subvol_srcu, index);

	/* step 3: relink backref */
	lock_start = backref->file_pos;
	lock_end = backref->file_pos + backref->num_bytes - 1;
	lock_extent_bits(&BTRFS_I(inode)->io_tree, lock_start, lock_end,
			 &cached);

	ordered = btrfs_lookup_first_ordered_extent(inode, lock_end);
	if (ordered) {
		btrfs_put_ordered_extent(ordered);
		goto out_unlock;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	key.objectid = backref->inum;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = backref->file_pos;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out_free_path;
	} else if (ret > 0) {
		ret = 0;
		goto out_free_path;
	}

	extent = btrfs_item_ptr(path->nodes[0], path->slots[0],
				struct btrfs_file_extent_item);

	if (btrfs_file_extent_generation(path->nodes[0], extent) !=
	    backref->generation)
		goto out_free_path;

	btrfs_release_path(path);

	start = backref->file_pos;
	if (backref->extent_offset < old->extent_offset + old->offset)
		start += old->extent_offset + old->offset -
			 backref->extent_offset;

	len = min(backref->extent_offset + backref->num_bytes,
		  old->extent_offset + old->offset + old->len);
	len -= max(backref->extent_offset, old->extent_offset + old->offset);

	ret = btrfs_drop_extents(trans, root, inode, start,
				 start + len, 1);
	if (ret)
		goto out_free_path;
again:
	key.objectid = btrfs_ino(inode);
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = start;

	path->leave_spinning = 1;
	if (merge) {
		struct btrfs_file_extent_item *fi;
		u64 extent_len;
		struct btrfs_key found_key;

		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		if (ret < 0)
			goto out_free_path;

		path->slots[0]--;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_len = btrfs_file_extent_num_bytes(leaf, fi);

		if (extent_len + found_key.offset == start &&
		    relink_is_mergable(leaf, fi, new)) {
			btrfs_set_file_extent_num_bytes(leaf, fi,
							extent_len + len);
			btrfs_mark_buffer_dirty(leaf);
			inode_add_bytes(inode, len);

			ret = 1;
			goto out_free_path;
		} else {
			merge = false;
			btrfs_release_path(path);
			goto again;
		}
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key,
					sizeof(*extent));
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_file_extent_item);
	btrfs_set_file_extent_disk_bytenr(leaf, item, new->bytenr);
	btrfs_set_file_extent_disk_num_bytes(leaf, item, new->disk_len);
	btrfs_set_file_extent_offset(leaf, item, start - new->file_pos);
	btrfs_set_file_extent_num_bytes(leaf, item, len);
	btrfs_set_file_extent_ram_bytes(leaf, item, new->len);
	btrfs_set_file_extent_generation(leaf, item, trans->transid);
	btrfs_set_file_extent_type(leaf, item, BTRFS_FILE_EXTENT_REG);
	btrfs_set_file_extent_compression(leaf, item, new->compress_type);
	btrfs_set_file_extent_encryption(leaf, item, 0);
	btrfs_set_file_extent_other_encoding(leaf, item, 0);

	btrfs_mark_buffer_dirty(leaf);
	inode_add_bytes(inode, len);
	btrfs_release_path(path);

	ret = btrfs_inc_extent_ref(trans, fs_info, new->bytenr,
			new->disk_len, 0,
			backref->root_id, backref->inum,
			new->file_pos);	/* start - extent_offset */
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_free_path;
	}

	ret = 1;
out_free_path:
	btrfs_release_path(path);
	path->leave_spinning = 0;
	btrfs_end_transaction(trans);
out_unlock:
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, lock_start, lock_end,
			     &cached, GFP_NOFS);
	iput(inode);
	return ret;
}

static void free_sa_defrag_extent(struct new_sa_defrag_extent *new)
{
	struct old_sa_defrag_extent *old, *tmp;

	if (!new)
		return;

	list_for_each_entry_safe(old, tmp, &new->head, list) {
		kfree(old);
	}
	kfree(new);
}

static void relink_file_extents(struct new_sa_defrag_extent *new)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(new->inode->i_sb);
	struct btrfs_path *path;
	struct sa_defrag_extent_backref *backref;
	struct sa_defrag_extent_backref *prev = NULL;
	struct inode *inode;
	struct btrfs_root *root;
	struct rb_node *node;
	int ret;

	inode = new->inode;
	root = BTRFS_I(inode)->root;

	path = btrfs_alloc_path();
	if (!path)
		return;

	if (!record_extent_backrefs(path, new)) {
		btrfs_free_path(path);
		goto out;
	}
	btrfs_release_path(path);

	while (1) {
		node = rb_first(&new->root);
		if (!node)
			break;
		rb_erase(node, &new->root);

		backref = rb_entry(node, struct sa_defrag_extent_backref, node);

		ret = relink_extent_backref(path, prev, backref);
		WARN_ON(ret < 0);

		kfree(prev);

		if (ret == 1)
			prev = backref;
		else
			prev = NULL;
		cond_resched();
	}
	kfree(prev);

	btrfs_free_path(path);
out:
	free_sa_defrag_extent(new);

	atomic_dec(&fs_info->defrag_running);
	wake_up(&fs_info->transaction_wait);
}

static struct new_sa_defrag_extent *
record_old_file_extents(struct inode *inode,
			struct btrfs_ordered_extent *ordered)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct old_sa_defrag_extent *old;
	struct new_sa_defrag_extent *new;
	int ret;

	new = kmalloc(sizeof(*new), GFP_NOFS);
	if (!new)
		return NULL;

	new->inode = inode;
	new->file_pos = ordered->file_offset;
	new->len = ordered->len;
	new->bytenr = ordered->start;
	new->disk_len = ordered->disk_len;
	new->compress_type = ordered->compress_type;
	new->root = RB_ROOT;
	INIT_LIST_HEAD(&new->head);

	path = btrfs_alloc_path();
	if (!path)
		goto out_kfree;

	key.objectid = btrfs_ino(inode);
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = new->file_pos;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out_free_path;
	if (ret > 0 && path->slots[0] > 0)
		path->slots[0]--;

	/* find out all the old extents for the file range */
	while (1) {
		struct btrfs_file_extent_item *extent;
		struct extent_buffer *l;
		int slot;
		u64 num_bytes;
		u64 offset;
		u64 end;
		u64 disk_bytenr;
		u64 extent_offset;

		l = path->nodes[0];
		slot = path->slots[0];

		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out_free_path;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(l, &key, slot);

		if (key.objectid != btrfs_ino(inode))
			break;
		if (key.type != BTRFS_EXTENT_DATA_KEY)
			break;
		if (key.offset >= new->file_pos + new->len)
			break;

		extent = btrfs_item_ptr(l, slot, struct btrfs_file_extent_item);

		num_bytes = btrfs_file_extent_num_bytes(l, extent);
		if (key.offset + num_bytes < new->file_pos)
			goto next;

		disk_bytenr = btrfs_file_extent_disk_bytenr(l, extent);
		if (!disk_bytenr)
			goto next;

		extent_offset = btrfs_file_extent_offset(l, extent);

		old = kmalloc(sizeof(*old), GFP_NOFS);
		if (!old)
			goto out_free_path;

		offset = max(new->file_pos, key.offset);
		end = min(new->file_pos + new->len, key.offset + num_bytes);

		old->bytenr = disk_bytenr;
		old->extent_offset = extent_offset;
		old->offset = offset - key.offset;
		old->len = end - offset;
		old->new = new;
		old->count = 0;
		list_add_tail(&old->list, &new->head);
next:
		path->slots[0]++;
		cond_resched();
	}

	btrfs_free_path(path);
	atomic_inc(&fs_info->defrag_running);

	return new;

out_free_path:
	btrfs_free_path(path);
out_kfree:
	free_sa_defrag_extent(new);
	return NULL;
}

static void btrfs_release_delalloc_bytes(struct btrfs_fs_info *fs_info,
					 u64 start, u64 len)
{
	struct btrfs_block_group_cache *cache;

	cache = btrfs_lookup_block_group(fs_info, start);
	ASSERT(cache);

	spin_lock(&cache->lock);
	cache->delalloc_bytes -= len;
	spin_unlock(&cache->lock);

	btrfs_put_block_group(cache);
}

/* as ordered data IO finishes, this gets called so we can finish
 * an ordered extent if the range of bytes in the file it covers are
 * fully written.
 */
static int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered_extent)
{
	struct inode *inode = ordered_extent->inode;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans = NULL;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_state *cached_state = NULL;
	struct new_sa_defrag_extent *new = NULL;
	int compress_type = 0;
	int ret = 0;
	u64 logical_len = ordered_extent->len;
	bool nolock;
	bool truncated = false;

	nolock = btrfs_is_free_space_inode(inode);

	if (test_bit(BTRFS_ORDERED_IOERR, &ordered_extent->flags)) {
		ret = -EIO;
		goto out;
	}

	btrfs_free_io_failure_record(inode, ordered_extent->file_offset,
				     ordered_extent->file_offset +
				     ordered_extent->len - 1);

	if (test_bit(BTRFS_ORDERED_TRUNCATED, &ordered_extent->flags)) {
		truncated = true;
		logical_len = ordered_extent->truncated_len;
		/* Truncated the entire extent, don't bother adding */
		if (!logical_len)
			goto out;
	}

	if (test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags)) {
		BUG_ON(!list_empty(&ordered_extent->list)); /* Logic error */

		/*
		 * For mwrite(mmap + memset to write) case, we still reserve
		 * space for NOCOW range.
		 * As NOCOW won't cause a new delayed ref, just free the space
		 */
		btrfs_qgroup_free_data(inode, ordered_extent->file_offset,
				       ordered_extent->len);
		btrfs_ordered_update_i_size(inode, 0, ordered_extent);
		if (nolock)
			trans = btrfs_join_transaction_nolock(root);
		else
			trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}
		trans->block_rsv = &fs_info->delalloc_block_rsv;
		ret = btrfs_update_inode_fallback(trans, root, inode);
		if (ret) /* -ENOMEM or corruption */
			btrfs_abort_transaction(trans, ret);
		goto out;
	}

	lock_extent_bits(io_tree, ordered_extent->file_offset,
			 ordered_extent->file_offset + ordered_extent->len - 1,
			 &cached_state);

	ret = test_range_bit(io_tree, ordered_extent->file_offset,
			ordered_extent->file_offset + ordered_extent->len - 1,
			EXTENT_DEFRAG, 1, cached_state);
	if (ret) {
		u64 last_snapshot = btrfs_root_last_snapshot(&root->root_item);
		if (0 && last_snapshot >= BTRFS_I(inode)->generation)
			/* the inode is shared */
			new = record_old_file_extents(inode, ordered_extent);

		clear_extent_bit(io_tree, ordered_extent->file_offset,
			ordered_extent->file_offset + ordered_extent->len - 1,
			EXTENT_DEFRAG, 0, 0, &cached_state, GFP_NOFS);
	}

	if (nolock)
		trans = btrfs_join_transaction_nolock(root);
	else
		trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out_unlock;
	}

	trans->block_rsv = &fs_info->delalloc_block_rsv;

	if (test_bit(BTRFS_ORDERED_COMPRESSED, &ordered_extent->flags))
		compress_type = ordered_extent->compress_type;
	if (test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags)) {
		BUG_ON(compress_type);
		ret = btrfs_mark_extent_written(trans, inode,
						ordered_extent->file_offset,
						ordered_extent->file_offset +
						logical_len);
	} else {
		BUG_ON(root == fs_info->tree_root);
		ret = insert_reserved_file_extent(trans, inode,
						ordered_extent->file_offset,
						ordered_extent->start,
						ordered_extent->disk_len,
						logical_len, logical_len,
						compress_type, 0, 0,
						BTRFS_FILE_EXTENT_REG);
		if (!ret)
			btrfs_release_delalloc_bytes(fs_info,
						     ordered_extent->start,
						     ordered_extent->disk_len);
	}
	unpin_extent_cache(&BTRFS_I(inode)->extent_tree,
			   ordered_extent->file_offset, ordered_extent->len,
			   trans->transid);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out_unlock;
	}

	add_pending_csums(trans, inode, ordered_extent->file_offset,
			  &ordered_extent->list);

	btrfs_ordered_update_i_size(inode, 0, ordered_extent);
	ret = btrfs_update_inode_fallback(trans, root, inode);
	if (ret) { /* -ENOMEM or corruption */
		btrfs_abort_transaction(trans, ret);
		goto out_unlock;
	}
	ret = 0;
out_unlock:
	unlock_extent_cached(io_tree, ordered_extent->file_offset,
			     ordered_extent->file_offset +
			     ordered_extent->len - 1, &cached_state, GFP_NOFS);
out:
	if (root != fs_info->tree_root)
		btrfs_delalloc_release_metadata(inode, ordered_extent->len);
	if (trans)
		btrfs_end_transaction(trans);

	if (ret || truncated) {
		u64 start, end;

		if (truncated)
			start = ordered_extent->file_offset + logical_len;
		else
			start = ordered_extent->file_offset;
		end = ordered_extent->file_offset + ordered_extent->len - 1;
		clear_extent_uptodate(io_tree, start, end, NULL, GFP_NOFS);

		/* Drop the cache for the part of the extent we didn't write. */
		btrfs_drop_extent_cache(inode, start, end, 0);

		/*
		 * If the ordered extent had an IOERR or something else went
		 * wrong we need to return the space for this ordered extent
		 * back to the allocator.  We only free the extent in the
		 * truncated case if we didn't write out the extent at all.
		 */
		if ((ret || !logical_len) &&
		    !test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags) &&
		    !test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags))
			btrfs_free_reserved_extent(fs_info,
						   ordered_extent->start,
						   ordered_extent->disk_len, 1);
	}


	/*
	 * This needs to be done to make sure anybody waiting knows we are done
	 * updating everything for this ordered extent.
	 */
	btrfs_remove_ordered_extent(inode, ordered_extent);

	/* for snapshot-aware defrag */
	if (new) {
		if (ret) {
			free_sa_defrag_extent(new);
			atomic_dec(&fs_info->defrag_running);
		} else {
			relink_file_extents(new);
		}
	}

	/* once for us */
	btrfs_put_ordered_extent(ordered_extent);
	/* once for the tree */
	btrfs_put_ordered_extent(ordered_extent);

	return ret;
}

static void finish_ordered_fn(struct btrfs_work *work)
{
	struct btrfs_ordered_extent *ordered_extent;
	ordered_extent = container_of(work, struct btrfs_ordered_extent, work);
	btrfs_finish_ordered_io(ordered_extent);
}

static int btrfs_writepage_end_io_hook(struct page *page, u64 start, u64 end,
				struct extent_state *state, int uptodate)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ordered_extent *ordered_extent = NULL;
	struct btrfs_workqueue *wq;
	btrfs_work_func_t func;

	trace_btrfs_writepage_end_io_hook(page, start, end, uptodate);

	ClearPagePrivate2(page);
	if (!btrfs_dec_test_ordered_pending(inode, &ordered_extent, start,
					    end - start + 1, uptodate))
		return 0;

	if (btrfs_is_free_space_inode(inode)) {
		wq = fs_info->endio_freespace_worker;
		func = btrfs_freespace_write_helper;
	} else {
		wq = fs_info->endio_write_workers;
		func = btrfs_endio_write_helper;
	}

	btrfs_init_work(&ordered_extent->work, func, finish_ordered_fn, NULL,
			NULL);
	btrfs_queue_work(wq, &ordered_extent->work);

	return 0;
}

static int __readpage_endio_check(struct inode *inode,
				  struct btrfs_io_bio *io_bio,
				  int icsum, struct page *page,
				  int pgoff, u64 start, size_t len)
{
	char *kaddr;
	u32 csum_expected;
	u32 csum = ~(u32)0;

	csum_expected = *(((u32 *)io_bio->csum) + icsum);

	kaddr = kmap_atomic(page);
	csum = btrfs_csum_data(kaddr + pgoff, csum,  len);
	btrfs_csum_final(csum, (u8 *)&csum);
	if (csum != csum_expected)
		goto zeroit;

	kunmap_atomic(kaddr);
	return 0;
zeroit:
	btrfs_warn_rl(BTRFS_I(inode)->root->fs_info,
		"csum failed ino %llu off %llu csum %u expected csum %u",
			   btrfs_ino(inode), start, csum, csum_expected);
	memset(kaddr + pgoff, 1, len);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);
	if (csum_expected == 0)
		return 0;
	return -EIO;
}

/*
 * when reads are done, we need to check csums to verify the data is correct
 * if there's a match, we allow the bio to finish.  If not, the code in
 * extent_io.c will try to find good copies for us.
 */
static int btrfs_readpage_end_io_hook(struct btrfs_io_bio *io_bio,
				      u64 phy_offset, struct page *page,
				      u64 start, u64 end, int mirror)
{
	size_t offset = start - page_offset(page);
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_root *root = BTRFS_I(inode)->root;

	if (PageChecked(page)) {
		ClearPageChecked(page);
		return 0;
	}

	if (BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)
		return 0;

	if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID &&
	    test_range_bit(io_tree, start, end, EXTENT_NODATASUM, 1, NULL)) {
		clear_extent_bits(io_tree, start, end, EXTENT_NODATASUM);
		return 0;
	}

	phy_offset >>= inode->i_sb->s_blocksize_bits;
	return __readpage_endio_check(inode, io_bio, phy_offset, page, offset,
				      start, (size_t)(end - start + 1));
}

void btrfs_add_delayed_iput(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_inode *binode = BTRFS_I(inode);

	if (atomic_add_unless(&inode->i_count, -1, 1))
		return;

	spin_lock(&fs_info->delayed_iput_lock);
	if (binode->delayed_iput_count == 0) {
		ASSERT(list_empty(&binode->delayed_iput));
		list_add_tail(&binode->delayed_iput, &fs_info->delayed_iputs);
	} else {
		binode->delayed_iput_count++;
	}
	spin_unlock(&fs_info->delayed_iput_lock);
}

void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info)
{

	spin_lock(&fs_info->delayed_iput_lock);
	while (!list_empty(&fs_info->delayed_iputs)) {
		struct btrfs_inode *inode;

		inode = list_first_entry(&fs_info->delayed_iputs,
				struct btrfs_inode, delayed_iput);
		if (inode->delayed_iput_count) {
			inode->delayed_iput_count--;
			list_move_tail(&inode->delayed_iput,
					&fs_info->delayed_iputs);
		} else {
			list_del_init(&inode->delayed_iput);
		}
		spin_unlock(&fs_info->delayed_iput_lock);
		iput(&inode->vfs_inode);
		spin_lock(&fs_info->delayed_iput_lock);
	}
	spin_unlock(&fs_info->delayed_iput_lock);
}

/*
 * This is called in transaction commit time. If there are no orphan
 * files in the subvolume, it removes orphan item and frees block_rsv
 * structure.
 */
void btrfs_orphan_commit_root(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *block_rsv;
	int ret;

	if (atomic_read(&root->orphan_inodes) ||
	    root->orphan_cleanup_state != ORPHAN_CLEANUP_DONE)
		return;

	spin_lock(&root->orphan_lock);
	if (atomic_read(&root->orphan_inodes)) {
		spin_unlock(&root->orphan_lock);
		return;
	}

	if (root->orphan_cleanup_state != ORPHAN_CLEANUP_DONE) {
		spin_unlock(&root->orphan_lock);
		return;
	}

	block_rsv = root->orphan_block_rsv;
	root->orphan_block_rsv = NULL;
	spin_unlock(&root->orphan_lock);

	if (test_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &root->state) &&
	    btrfs_root_refs(&root->root_item) > 0) {
		ret = btrfs_del_orphan_item(trans, fs_info->tree_root,
					    root->root_key.objectid);
		if (ret)
			btrfs_abort_transaction(trans, ret);
		else
			clear_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED,
				  &root->state);
	}

	if (block_rsv) {
		WARN_ON(block_rsv->size > 0);
		btrfs_free_block_rsv(fs_info, block_rsv);
	}
}

/*
 * This creates an orphan entry for the given inode in case something goes
 * wrong in the middle of an unlink/truncate.
 *
 * NOTE: caller of this function should reserve 5 units of metadata for
 *	 this function.
 */
int btrfs_orphan_add(struct btrfs_trans_handle *trans, struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *block_rsv = NULL;
	int reserve = 0;
	int insert = 0;
	int ret;

	if (!root->orphan_block_rsv) {
		block_rsv = btrfs_alloc_block_rsv(fs_info,
						  BTRFS_BLOCK_RSV_TEMP);
		if (!block_rsv)
			return -ENOMEM;
	}

	spin_lock(&root->orphan_lock);
	if (!root->orphan_block_rsv) {
		root->orphan_block_rsv = block_rsv;
	} else if (block_rsv) {
		btrfs_free_block_rsv(fs_info, block_rsv);
		block_rsv = NULL;
	}

	if (!test_and_set_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
			      &BTRFS_I(inode)->runtime_flags)) {
#if 0
		/*
		 * For proper ENOSPC handling, we should do orphan
		 * cleanup when mounting. But this introduces backward
		 * compatibility issue.
		 */
		if (!xchg(&root->orphan_item_inserted, 1))
			insert = 2;
		else
			insert = 1;
#endif
		insert = 1;
		atomic_inc(&root->orphan_inodes);
	}

	if (!test_and_set_bit(BTRFS_INODE_ORPHAN_META_RESERVED,
			      &BTRFS_I(inode)->runtime_flags))
		reserve = 1;
	spin_unlock(&root->orphan_lock);

	/* grab metadata reservation from transaction handle */
	if (reserve) {
		ret = btrfs_orphan_reserve_metadata(trans, inode);
		ASSERT(!ret);
		if (ret) {
			atomic_dec(&root->orphan_inodes);
			clear_bit(BTRFS_INODE_ORPHAN_META_RESERVED,
				  &BTRFS_I(inode)->runtime_flags);
			if (insert)
				clear_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
					  &BTRFS_I(inode)->runtime_flags);
			return ret;
		}
	}

	/* insert an orphan item to track this unlinked/truncated file */
	if (insert >= 1) {
		ret = btrfs_insert_orphan_item(trans, root, btrfs_ino(inode));
		if (ret) {
			atomic_dec(&root->orphan_inodes);
			if (reserve) {
				clear_bit(BTRFS_INODE_ORPHAN_META_RESERVED,
					  &BTRFS_I(inode)->runtime_flags);
				btrfs_orphan_release_metadata(inode);
			}
			if (ret != -EEXIST) {
				clear_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
					  &BTRFS_I(inode)->runtime_flags);
				btrfs_abort_transaction(trans, ret);
				return ret;
			}
		}
		ret = 0;
	}

	/* insert an orphan item to track subvolume contains orphan files */
	if (insert >= 2) {
		ret = btrfs_insert_orphan_item(trans, fs_info->tree_root,
					       root->root_key.objectid);
		if (ret && ret != -EEXIST) {
			btrfs_abort_transaction(trans, ret);
			return ret;
		}
	}
	return 0;
}

/*
 * We have done the truncate/delete so we can go ahead and remove the orphan
 * item for this particular inode.
 */
static int btrfs_orphan_del(struct btrfs_trans_handle *trans,
			    struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int delete_item = 0;
	int release_rsv = 0;
	int ret = 0;

	spin_lock(&root->orphan_lock);
	if (test_and_clear_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
			       &BTRFS_I(inode)->runtime_flags))
		delete_item = 1;

	if (test_and_clear_bit(BTRFS_INODE_ORPHAN_META_RESERVED,
			       &BTRFS_I(inode)->runtime_flags))
		release_rsv = 1;
	spin_unlock(&root->orphan_lock);

	if (delete_item) {
		atomic_dec(&root->orphan_inodes);
		if (trans)
			ret = btrfs_del_orphan_item(trans, root,
						    btrfs_ino(inode));
	}

	if (release_rsv)
		btrfs_orphan_release_metadata(inode);

	return ret;
}

/*
 * this cleans up any orphans that may be left on the list from the last use
 * of this root.
 */
int btrfs_orphan_cleanup(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key, found_key;
	struct btrfs_trans_handle *trans;
	struct inode *inode;
	u64 last_objectid = 0;
	int ret = 0, nr_unlink = 0, nr_truncate = 0;

	if (cmpxchg(&root->orphan_cleanup_state, 0, ORPHAN_CLEANUP_STARTED))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	path->reada = READA_BACK;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		/*
		 * if ret == 0 means we found what we were searching for, which
		 * is weird, but possible, so only screw with path if we didn't
		 * find the key and see if we have stuff that matches
		 */
		if (ret > 0) {
			ret = 0;
			if (path->slots[0] == 0)
				break;
			path->slots[0]--;
		}

		/* pull out the item */
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		/* make sure the item matches what we want */
		if (found_key.objectid != BTRFS_ORPHAN_OBJECTID)
			break;
		if (found_key.type != BTRFS_ORPHAN_ITEM_KEY)
			break;

		/* release the path since we're done with it */
		btrfs_release_path(path);

		/*
		 * this is where we are basically btrfs_lookup, without the
		 * crossing root thing.  we store the inode number in the
		 * offset of the orphan item.
		 */

		if (found_key.offset == last_objectid) {
			btrfs_err(fs_info,
				  "Error removing orphan entry, stopping orphan cleanup");
			ret = -EINVAL;
			goto out;
		}

		last_objectid = found_key.offset;

		found_key.objectid = found_key.offset;
		found_key.type = BTRFS_INODE_ITEM_KEY;
		found_key.offset = 0;
		inode = btrfs_iget(fs_info->sb, &found_key, root, NULL);
		ret = PTR_ERR_OR_ZERO(inode);
		if (ret && ret != -ENOENT)
			goto out;

		if (ret == -ENOENT && root == fs_info->tree_root) {
			struct btrfs_root *dead_root;
			struct btrfs_fs_info *fs_info = root->fs_info;
			int is_dead_root = 0;

			/*
			 * this is an orphan in the tree root. Currently these
			 * could come from 2 sources:
			 *  a) a snapshot deletion in progress
			 *  b) a free space cache inode
			 * We need to distinguish those two, as the snapshot
			 * orphan must not get deleted.
			 * find_dead_roots already ran before us, so if this
			 * is a snapshot deletion, we should find the root
			 * in the dead_roots list
			 */
			spin_lock(&fs_info->trans_lock);
			list_for_each_entry(dead_root, &fs_info->dead_roots,
					    root_list) {
				if (dead_root->root_key.objectid ==
				    found_key.objectid) {
					is_dead_root = 1;
					break;
				}
			}
			spin_unlock(&fs_info->trans_lock);
			if (is_dead_root) {
				/* prevent this orphan from being found again */
				key.offset = found_key.objectid - 1;
				continue;
			}
		}
		/*
		 * Inode is already gone but the orphan item is still there,
		 * kill the orphan item.
		 */
		if (ret == -ENOENT) {
			trans = btrfs_start_transaction(root, 1);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				goto out;
			}
			btrfs_debug(fs_info, "auto deleting %Lu",
				    found_key.objectid);
			ret = btrfs_del_orphan_item(trans, root,
						    found_key.objectid);
			btrfs_end_transaction(trans);
			if (ret)
				goto out;
			continue;
		}

		/*
		 * add this inode to the orphan list so btrfs_orphan_del does
		 * the proper thing when we hit it
		 */
		set_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
			&BTRFS_I(inode)->runtime_flags);
		atomic_inc(&root->orphan_inodes);

		/* if we have links, this was a truncate, lets do that */
		if (inode->i_nlink) {
			if (WARN_ON(!S_ISREG(inode->i_mode))) {
				iput(inode);
				continue;
			}
			nr_truncate++;

			/* 1 for the orphan item deletion. */
			trans = btrfs_start_transaction(root, 1);
			if (IS_ERR(trans)) {
				iput(inode);
				ret = PTR_ERR(trans);
				goto out;
			}
			ret = btrfs_orphan_add(trans, inode);
			btrfs_end_transaction(trans);
			if (ret) {
				iput(inode);
				goto out;
			}

			ret = btrfs_truncate(inode);
			if (ret)
				btrfs_orphan_del(NULL, inode);
		} else {
			nr_unlink++;
		}

		/* this will do delete_inode and everything for us */
		iput(inode);
		if (ret)
			goto out;
	}
	/* release the path since we're done with it */
	btrfs_release_path(path);

	root->orphan_cleanup_state = ORPHAN_CLEANUP_DONE;

	if (root->orphan_block_rsv)
		btrfs_block_rsv_release(fs_info, root->orphan_block_rsv,
					(u64)-1);

	if (root->orphan_block_rsv ||
	    test_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &root->state)) {
		trans = btrfs_join_transaction(root);
		if (!IS_ERR(trans))
			btrfs_end_transaction(trans);
	}

	if (nr_unlink)
		btrfs_debug(fs_info, "unlinked %d orphans", nr_unlink);
	if (nr_truncate)
		btrfs_debug(fs_info, "truncated %d orphans", nr_truncate);

out:
	if (ret)
		btrfs_err(fs_info, "could not do orphan cleanup %d", ret);
	btrfs_free_path(path);
	return ret;
}

/*
 * very simple check to peek ahead in the leaf looking for xattrs.  If we
 * don't find any xattrs, we know there can't be any acls.
 *
 * slot is the slot the inode is in, objectid is the objectid of the inode
 */
static noinline int acls_after_inode_item(struct extent_buffer *leaf,
					  int slot, u64 objectid,
					  int *first_xattr_slot)
{
	u32 nritems = btrfs_header_nritems(leaf);
	struct btrfs_key found_key;
	static u64 xattr_access = 0;
	static u64 xattr_default = 0;
	int scanned = 0;

	if (!xattr_access) {
		xattr_access = btrfs_name_hash(XATTR_NAME_POSIX_ACL_ACCESS,
					strlen(XATTR_NAME_POSIX_ACL_ACCESS));
		xattr_default = btrfs_name_hash(XATTR_NAME_POSIX_ACL_DEFAULT,
					strlen(XATTR_NAME_POSIX_ACL_DEFAULT));
	}

	slot++;
	*first_xattr_slot = -1;
	while (slot < nritems) {
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		/* we found a different objectid, there must not be acls */
		if (found_key.objectid != objectid)
			return 0;

		/* we found an xattr, assume we've got an acl */
		if (found_key.type == BTRFS_XATTR_ITEM_KEY) {
			if (*first_xattr_slot == -1)
				*first_xattr_slot = slot;
			if (found_key.offset == xattr_access ||
			    found_key.offset == xattr_default)
				return 1;
		}

		/*
		 * we found a key greater than an xattr key, there can't
		 * be any acls later on
		 */
		if (found_key.type > BTRFS_XATTR_ITEM_KEY)
			return 0;

		slot++;
		scanned++;

		/*
		 * it goes inode, inode backrefs, xattrs, extents,
		 * so if there are a ton of hard links to an inode there can
		 * be a lot of backrefs.  Don't waste time searching too hard,
		 * this is just an optimization
		 */
		if (scanned >= 8)
			break;
	}
	/* we hit the end of the leaf before we found an xattr or
	 * something larger than an xattr.  We have to assume the inode
	 * has acls
	 */
	if (*first_xattr_slot == -1)
		*first_xattr_slot = slot;
	return 1;
}

/*
 * read an inode from the btree into the in-memory inode
 */
static int btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_inode_item *inode_item;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key location;
	unsigned long ptr;
	int maybe_acls;
	u32 rdev;
	int ret;
	bool filled = false;
	int first_xattr_slot;

	ret = btrfs_fill_inode(inode, &rdev);
	if (!ret)
		filled = true;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto make_bad;
	}

	memcpy(&location, &BTRFS_I(inode)->location, sizeof(location));

	ret = btrfs_lookup_inode(NULL, root, path, &location, 0);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto make_bad;
	}

	leaf = path->nodes[0];

	if (filled)
		goto cache_index;

	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);
	inode->i_mode = btrfs_inode_mode(leaf, inode_item);
	set_nlink(inode, btrfs_inode_nlink(leaf, inode_item));
	i_uid_write(inode, btrfs_inode_uid(leaf, inode_item));
	i_gid_write(inode, btrfs_inode_gid(leaf, inode_item));
	btrfs_i_size_write(inode, btrfs_inode_size(leaf, inode_item));

	inode->i_atime.tv_sec = btrfs_timespec_sec(leaf, &inode_item->atime);
	inode->i_atime.tv_nsec = btrfs_timespec_nsec(leaf, &inode_item->atime);

	inode->i_mtime.tv_sec = btrfs_timespec_sec(leaf, &inode_item->mtime);
	inode->i_mtime.tv_nsec = btrfs_timespec_nsec(leaf, &inode_item->mtime);

	inode->i_ctime.tv_sec = btrfs_timespec_sec(leaf, &inode_item->ctime);
	inode->i_ctime.tv_nsec = btrfs_timespec_nsec(leaf, &inode_item->ctime);

	BTRFS_I(inode)->i_otime.tv_sec =
		btrfs_timespec_sec(leaf, &inode_item->otime);
	BTRFS_I(inode)->i_otime.tv_nsec =
		btrfs_timespec_nsec(leaf, &inode_item->otime);

	inode_set_bytes(inode, btrfs_inode_nbytes(leaf, inode_item));
	BTRFS_I(inode)->generation = btrfs_inode_generation(leaf, inode_item);
	BTRFS_I(inode)->last_trans = btrfs_inode_transid(leaf, inode_item);

	inode->i_version = btrfs_inode_sequence(leaf, inode_item);
	inode->i_generation = BTRFS_I(inode)->generation;
	inode->i_rdev = 0;
	rdev = btrfs_inode_rdev(leaf, inode_item);

	BTRFS_I(inode)->index_cnt = (u64)-1;
	BTRFS_I(inode)->flags = btrfs_inode_flags(leaf, inode_item);

cache_index:
	/*
	 * If we were modified in the current generation and evicted from memory
	 * and then re-read we need to do a full sync since we don't have any
	 * idea about which extents were modified before we were evicted from
	 * cache.
	 *
	 * This is required for both inode re-read from disk and delayed inode
	 * in delayed_nodes_tree.
	 */
	if (BTRFS_I(inode)->last_trans == fs_info->generation)
		set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
			&BTRFS_I(inode)->runtime_flags);

	/*
	 * We don't persist the id of the transaction where an unlink operation
	 * against the inode was last made. So here we assume the inode might
	 * have been evicted, and therefore the exact value of last_unlink_trans
	 * lost, and set it to last_trans to avoid metadata inconsistencies
	 * between the inode and its parent if the inode is fsync'ed and the log
	 * replayed. For example, in the scenario:
	 *
	 * touch mydir/foo
	 * ln mydir/foo mydir/bar
	 * sync
	 * unlink mydir/bar
	 * echo 2 > /proc/sys/vm/drop_caches   # evicts inode
	 * xfs_io -c fsync mydir/foo
	 * <power failure>
	 * mount fs, triggers fsync log replay
	 *
	 * We must make sure that when we fsync our inode foo we also log its
	 * parent inode, otherwise after log replay the parent still has the
	 * dentry with the "bar" name but our inode foo has a link count of 1
	 * and doesn't have an inode ref with the name "bar" anymore.
	 *
	 * Setting last_unlink_trans to last_trans is a pessimistic approach,
	 * but it guarantees correctness at the expense of occasional full
	 * transaction commits on fsync if our inode is a directory, or if our
	 * inode is not a directory, logging its parent unnecessarily.
	 */
	BTRFS_I(inode)->last_unlink_trans = BTRFS_I(inode)->last_trans;

	path->slots[0]++;
	if (inode->i_nlink != 1 ||
	    path->slots[0] >= btrfs_header_nritems(leaf))
		goto cache_acl;

	btrfs_item_key_to_cpu(leaf, &location, path->slots[0]);
	if (location.objectid != btrfs_ino(inode))
		goto cache_acl;

	ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
	if (location.type == BTRFS_INODE_REF_KEY) {
		struct btrfs_inode_ref *ref;

		ref = (struct btrfs_inode_ref *)ptr;
		BTRFS_I(inode)->dir_index = btrfs_inode_ref_index(leaf, ref);
	} else if (location.type == BTRFS_INODE_EXTREF_KEY) {
		struct btrfs_inode_extref *extref;

		extref = (struct btrfs_inode_extref *)ptr;
		BTRFS_I(inode)->dir_index = btrfs_inode_extref_index(leaf,
								     extref);
	}
cache_acl:
	/*
	 * try to precache a NULL acl entry for files that don't have
	 * any xattrs or acls
	 */
	maybe_acls = acls_after_inode_item(leaf, path->slots[0],
					   btrfs_ino(inode), &first_xattr_slot);
	if (first_xattr_slot != -1) {
		path->slots[0] = first_xattr_slot;
		ret = btrfs_load_inode_props(inode, path);
		if (ret)
			btrfs_err(fs_info,
				  "error loading props for ino %llu (root %llu): %d",
				  btrfs_ino(inode),
				  root->root_key.objectid, ret);
	}
	btrfs_free_path(path);

	if (!maybe_acls)
		cache_no_acl(inode);

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_mapping->a_ops = &btrfs_aops;
		BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		break;
	case S_IFDIR:
		inode->i_fop = &btrfs_dir_file_operations;
		if (root == fs_info->tree_root)
			inode->i_op = &btrfs_dir_ro_inode_operations;
		else
			inode->i_op = &btrfs_dir_inode_operations;
		break;
	case S_IFLNK:
		inode->i_op = &btrfs_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &btrfs_symlink_aops;
		break;
	default:
		inode->i_op = &btrfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		break;
	}

	btrfs_update_iflags(inode);
	return 0;

make_bad:
	btrfs_free_path(path);
	make_bad_inode(inode);
	return ret;
}

/*
 * given a leaf and an inode, copy the inode fields into the leaf
 */
static void fill_inode_item(struct btrfs_trans_handle *trans,
			    struct extent_buffer *leaf,
			    struct btrfs_inode_item *item,
			    struct inode *inode)
{
	struct btrfs_map_token token;

	btrfs_init_map_token(&token);

	btrfs_set_token_inode_uid(leaf, item, i_uid_read(inode), &token);
	btrfs_set_token_inode_gid(leaf, item, i_gid_read(inode), &token);
	btrfs_set_token_inode_size(leaf, item, BTRFS_I(inode)->disk_i_size,
				   &token);
	btrfs_set_token_inode_mode(leaf, item, inode->i_mode, &token);
	btrfs_set_token_inode_nlink(leaf, item, inode->i_nlink, &token);

	btrfs_set_token_timespec_sec(leaf, &item->atime,
				     inode->i_atime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->atime,
				      inode->i_atime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->mtime,
				     inode->i_mtime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->mtime,
				      inode->i_mtime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->ctime,
				     inode->i_ctime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->ctime,
				      inode->i_ctime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->otime,
				     BTRFS_I(inode)->i_otime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->otime,
				      BTRFS_I(inode)->i_otime.tv_nsec, &token);

	btrfs_set_token_inode_nbytes(leaf, item, inode_get_bytes(inode),
				     &token);
	btrfs_set_token_inode_generation(leaf, item, BTRFS_I(inode)->generation,
					 &token);
	btrfs_set_token_inode_sequence(leaf, item, inode->i_version, &token);
	btrfs_set_token_inode_transid(leaf, item, trans->transid, &token);
	btrfs_set_token_inode_rdev(leaf, item, inode->i_rdev, &token);
	btrfs_set_token_inode_flags(leaf, item, BTRFS_I(inode)->flags, &token);
	btrfs_set_token_inode_block_group(leaf, item, 0, &token);
}

/*
 * copy everything in the in-memory inode into the btree.
 */
static noinline int btrfs_update_inode_item(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	ret = btrfs_lookup_inode(trans, root, path, &BTRFS_I(inode)->location,
				 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

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
 * copy everything in the in-memory inode into the btree.
 */
noinline int btrfs_update_inode(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct inode *inode)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	/*
	 * If the inode is a free space inode, we can deadlock during commit
	 * if we put it into the delayed code.
	 *
	 * The data relocation inode should also be directly updated
	 * without delay
	 */
	if (!btrfs_is_free_space_inode(inode)
	    && root->root_key.objectid != BTRFS_DATA_RELOC_TREE_OBJECTID
	    && !test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags)) {
		btrfs_update_root_times(trans, root);

		ret = btrfs_delayed_update_inode(trans, root, inode);
		if (!ret)
			btrfs_set_inode_last_trans(trans, inode);
		return ret;
	}

	return btrfs_update_inode_item(trans, root, inode);
}

noinline int btrfs_update_inode_fallback(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct inode *inode)
{
	int ret;

	ret = btrfs_update_inode(trans, root, inode);
	if (ret == -ENOSPC)
		return btrfs_update_inode_item(trans, root, inode);
	return ret;
}

/*
 * unlink helper that gets used here in inode.c and in the tree logging
 * recovery code.  It remove a link in a directory with a given name, and
 * also drops the back refs in the inode to the directory
 */
static int __btrfs_unlink_inode(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct inode *dir, struct inode *inode,
				const char *name, int name_len)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	int ret = 0;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 index;
	u64 ino = btrfs_ino(inode);
	u64 dir_ino = btrfs_ino(dir);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	path->leave_spinning = 1;
	di = btrfs_lookup_dir_item(trans, root, path, dir_ino,
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
	btrfs_release_path(path);

	/*
	 * If we don't have dir index, we have to get it by looking up
	 * the inode ref, since we get the inode ref, remove it directly,
	 * it is unnecessary to do delayed deletion.
	 *
	 * But if we have dir index, needn't search inode ref to get it.
	 * Since the inode ref is close to the inode item, it is better
	 * that we delay to delete it, and just do this deletion when
	 * we update the inode item.
	 */
	if (BTRFS_I(inode)->dir_index) {
		ret = btrfs_delayed_delete_inode_ref(inode);
		if (!ret) {
			index = BTRFS_I(inode)->dir_index;
			goto skip_backref;
		}
	}

	ret = btrfs_del_inode_ref(trans, root, name, name_len, ino,
				  dir_ino, &index);
	if (ret) {
		btrfs_info(fs_info,
			"failed to delete reference to %.*s, inode %llu parent %llu",
			name_len, name, ino, dir_ino);
		btrfs_abort_transaction(trans, ret);
		goto err;
	}
skip_backref:
	ret = btrfs_delete_delayed_dir_index(trans, fs_info, dir, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto err;
	}

	ret = btrfs_del_inode_ref_in_log(trans, root, name, name_len,
					 inode, dir_ino);
	if (ret != 0 && ret != -ENOENT) {
		btrfs_abort_transaction(trans, ret);
		goto err;
	}

	ret = btrfs_del_dir_entries_in_log(trans, root, name, name_len,
					   dir, index);
	if (ret == -ENOENT)
		ret = 0;
	else if (ret)
		btrfs_abort_transaction(trans, ret);
err:
	btrfs_free_path(path);
	if (ret)
		goto out;

	btrfs_i_size_write(dir, dir->i_size - name_len * 2);
	inode_inc_iversion(inode);
	inode_inc_iversion(dir);
	inode->i_ctime = dir->i_mtime =
		dir->i_ctime = current_time(inode);
	ret = btrfs_update_inode(trans, root, dir);
out:
	return ret;
}

int btrfs_unlink_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       struct inode *dir, struct inode *inode,
		       const char *name, int name_len)
{
	int ret;
	ret = __btrfs_unlink_inode(trans, root, dir, inode, name, name_len);
	if (!ret) {
		drop_nlink(inode);
		ret = btrfs_update_inode(trans, root, inode);
	}
	return ret;
}

/*
 * helper to start transaction for unlink and rmdir.
 *
 * unlink and rmdir are special in btrfs, they do not always free space, so
 * if we cannot make our reservations the normal way try and see if there is
 * plenty of slack room in the global reserve to migrate, otherwise we cannot
 * allow the unlink to occur.
 */
static struct btrfs_trans_handle *__unlink_start_trans(struct inode *dir)
{
	struct btrfs_root *root = BTRFS_I(dir)->root;

	/*
	 * 1 for the possible orphan item
	 * 1 for the dir item
	 * 1 for the dir index
	 * 1 for the inode ref
	 * 1 for the inode
	 */
	return btrfs_start_transaction_fallback_global_rsv(root, 5, 5);
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_trans_handle *trans;
	struct inode *inode = d_inode(dentry);
	int ret;

	trans = __unlink_start_trans(dir);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	btrfs_record_unlink_dir(trans, dir, d_inode(dentry), 0);

	ret = btrfs_unlink_inode(trans, root, dir, d_inode(dentry),
				 dentry->d_name.name, dentry->d_name.len);
	if (ret)
		goto out;

	if (inode->i_nlink == 0) {
		ret = btrfs_orphan_add(trans, inode);
		if (ret)
			goto out;
	}

out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(root->fs_info);
	return ret;
}

int btrfs_unlink_subvol(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct inode *dir, u64 objectid,
			const char *name, int name_len)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 index;
	int ret;
	u64 dir_ino = btrfs_ino(dir);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	di = btrfs_lookup_dir_item(trans, root, path, dir_ino,
				   name, name_len, -1);
	if (IS_ERR_OR_NULL(di)) {
		if (!di)
			ret = -ENOENT;
		else
			ret = PTR_ERR(di);
		goto out;
	}

	leaf = path->nodes[0];
	btrfs_dir_item_key_to_cpu(leaf, di, &key);
	WARN_ON(key.type != BTRFS_ROOT_ITEM_KEY || key.objectid != objectid);
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
	btrfs_release_path(path);

	ret = btrfs_del_root_ref(trans, fs_info, objectid,
				 root->root_key.objectid, dir_ino,
				 &index, name, name_len);
	if (ret < 0) {
		if (ret != -ENOENT) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
		di = btrfs_search_dir_index_item(root, path, dir_ino,
						 name, name_len);
		if (IS_ERR_OR_NULL(di)) {
			if (!di)
				ret = -ENOENT;
			else
				ret = PTR_ERR(di);
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		btrfs_release_path(path);
		index = key.offset;
	}
	btrfs_release_path(path);

	ret = btrfs_delete_delayed_dir_index(trans, fs_info, dir, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	btrfs_i_size_write(dir, dir->i_size - name_len * 2);
	inode_inc_iversion(dir);
	dir->i_mtime = dir->i_ctime = current_time(dir);
	ret = btrfs_update_inode_fallback(trans, root, dir);
	if (ret)
		btrfs_abort_transaction(trans, ret);
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	int err = 0;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_trans_handle *trans;
	u64 last_unlink_trans;

	if (inode->i_size > BTRFS_EMPTY_DIR_SIZE)
		return -ENOTEMPTY;
	if (btrfs_ino(inode) == BTRFS_FIRST_FREE_OBJECTID)
		return -EPERM;

	trans = __unlink_start_trans(dir);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	if (unlikely(btrfs_ino(inode) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
		err = btrfs_unlink_subvol(trans, root, dir,
					  BTRFS_I(inode)->location.objectid,
					  dentry->d_name.name,
					  dentry->d_name.len);
		goto out;
	}

	err = btrfs_orphan_add(trans, inode);
	if (err)
		goto out;

	last_unlink_trans = BTRFS_I(inode)->last_unlink_trans;

	/* now the directory is empty */
	err = btrfs_unlink_inode(trans, root, dir, d_inode(dentry),
				 dentry->d_name.name, dentry->d_name.len);
	if (!err) {
		btrfs_i_size_write(inode, 0);
		/*
		 * Propagate the last_unlink_trans value of the deleted dir to
		 * its parent directory. This is to prevent an unrecoverable
		 * log tree in the case we do something like this:
		 * 1) create dir foo
		 * 2) create snapshot under dir foo
		 * 3) delete the snapshot
		 * 4) rmdir foo
		 * 5) mkdir foo
		 * 6) fsync foo or some file inside foo
		 */
		if (last_unlink_trans >= trans->transid)
			BTRFS_I(dir)->last_unlink_trans = last_unlink_trans;
	}
out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(root->fs_info);

	return err;
}

static int truncate_space_check(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytes_deleted)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	/*
	 * This is only used to apply pressure to the enospc system, we don't
	 * intend to use this reservation at all.
	 */
	bytes_deleted = btrfs_csum_bytes_to_leaves(fs_info, bytes_deleted);
	bytes_deleted *= fs_info->nodesize;
	ret = btrfs_block_rsv_add(root, &fs_info->trans_block_rsv,
				  bytes_deleted, BTRFS_RESERVE_NO_FLUSH);
	if (!ret) {
		trace_btrfs_space_reservation(fs_info, "transaction",
					      trans->transid,
					      bytes_deleted, 1);
		trans->bytes_reserved += bytes_deleted;
	}
	return ret;

}

static int truncate_inline_extent(struct inode *inode,
				  struct btrfs_path *path,
				  struct btrfs_key *found_key,
				  const u64 item_end,
				  const u64 new_size)
{
	struct extent_buffer *leaf = path->nodes[0];
	int slot = path->slots[0];
	struct btrfs_file_extent_item *fi;
	u32 size = (u32)(new_size - found_key->offset);
	struct btrfs_root *root = BTRFS_I(inode)->root;

	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);

	if (btrfs_file_extent_compression(leaf, fi) != BTRFS_COMPRESS_NONE) {
		loff_t offset = new_size;
		loff_t page_end = ALIGN(offset, PAGE_SIZE);

		/*
		 * Zero out the remaining of the last page of our inline extent,
		 * instead of directly truncating our inline extent here - that
		 * would be much more complex (decompressing all the data, then
		 * compressing the truncated data, which might be bigger than
		 * the size of the inline extent, resize the extent, etc).
		 * We release the path because to get the page we might need to
		 * read the extent item from disk (data not in the page cache).
		 */
		btrfs_release_path(path);
		return btrfs_truncate_block(inode, offset, page_end - offset,
					0);
	}

	btrfs_set_file_extent_ram_bytes(leaf, fi, size);
	size = btrfs_file_extent_calc_inline_size(size);
	btrfs_truncate_item(root->fs_info, path, size, 1);

	if (test_bit(BTRFS_ROOT_REF_COWS, &root->state))
		inode_sub_bytes(inode, item_end + 1 - new_size);

	return 0;
}

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
int btrfs_truncate_inode_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct inode *inode,
			       u64 new_size, u32 min_type)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 extent_start = 0;
	u64 extent_num_bytes = 0;
	u64 extent_offset = 0;
	u64 item_end = 0;
	u64 last_size = new_size;
	u32 found_type = (u8)-1;
	int found_extent;
	int del_item;
	int pending_del_nr = 0;
	int pending_del_slot = 0;
	int extent_type = -1;
	int ret;
	int err = 0;
	u64 ino = btrfs_ino(inode);
	u64 bytes_deleted = 0;
	bool be_nice = 0;
	bool should_throttle = 0;
	bool should_end = 0;

	BUG_ON(new_size > 0 && min_type != BTRFS_EXTENT_DATA_KEY);

	/*
	 * for non-free space inodes and ref cows, we want to back off from
	 * time to time
	 */
	if (!btrfs_is_free_space_inode(inode) &&
	    test_bit(BTRFS_ROOT_REF_COWS, &root->state))
		be_nice = 1;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_BACK;

	/*
	 * We want to drop from the next block forward in case this new size is
	 * not block aligned since we will be keeping the last block of the
	 * extent just the way it is.
	 */
	if (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
	    root == fs_info->tree_root)
		btrfs_drop_extent_cache(inode, ALIGN(new_size,
					fs_info->sectorsize),
					(u64)-1, 0);

	/*
	 * This function is also used to drop the items in the log tree before
	 * we relog the inode, so if root != BTRFS_I(inode)->root, it means
	 * it is used to drop the loged items. So we shouldn't kill the delayed
	 * items.
	 */
	if (min_type == 0 && root == BTRFS_I(inode)->root)
		btrfs_kill_delayed_inode_items(inode);

	key.objectid = ino;
	key.offset = (u64)-1;
	key.type = (u8)-1;

search_again:
	/*
	 * with a 16K leaf size and 128MB extents, you can actually queue
	 * up a huge file in a single leaf.  Most of the time that
	 * bytes_deleted is > 0, it will be huge by the time we get here
	 */
	if (be_nice && bytes_deleted > SZ_32M) {
		if (btrfs_should_end_transaction(trans)) {
			err = -EAGAIN;
			goto error;
		}
	}


	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (ret > 0) {
		/* there are no items in the tree for us to truncate, we're
		 * done
		 */
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}

	while (1) {
		fi = NULL;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		found_type = found_key.type;

		if (found_key.objectid != ino)
			break;

		if (found_type < min_type)
			break;

		item_end = found_key.offset;
		if (found_type == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			extent_type = btrfs_file_extent_type(leaf, fi);
			if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
				item_end +=
				    btrfs_file_extent_num_bytes(leaf, fi);
			} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
				item_end += btrfs_file_extent_inline_len(leaf,
							 path->slots[0], fi);
			}
			item_end--;
		}
		if (found_type > min_type) {
			del_item = 1;
		} else {
			if (item_end < new_size)
				break;
			if (found_key.offset >= new_size)
				del_item = 1;
			else
				del_item = 0;
		}
		found_extent = 0;
		/* FIXME, shrink the extent if the ref count is only 1 */
		if (found_type != BTRFS_EXTENT_DATA_KEY)
			goto delete;

		if (del_item)
			last_size = found_key.offset;
		else
			last_size = new_size;

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			u64 num_dec;
			extent_start = btrfs_file_extent_disk_bytenr(leaf, fi);
			if (!del_item) {
				u64 orig_num_bytes =
					btrfs_file_extent_num_bytes(leaf, fi);
				extent_num_bytes = ALIGN(new_size -
						found_key.offset,
						fs_info->sectorsize);
				btrfs_set_file_extent_num_bytes(leaf, fi,
							 extent_num_bytes);
				num_dec = (orig_num_bytes -
					   extent_num_bytes);
				if (test_bit(BTRFS_ROOT_REF_COWS,
					     &root->state) &&
				    extent_start != 0)
					inode_sub_bytes(inode, num_dec);
				btrfs_mark_buffer_dirty(leaf);
			} else {
				extent_num_bytes =
					btrfs_file_extent_disk_num_bytes(leaf,
									 fi);
				extent_offset = found_key.offset -
					btrfs_file_extent_offset(leaf, fi);

				/* FIXME blocksize != 4096 */
				num_dec = btrfs_file_extent_num_bytes(leaf, fi);
				if (extent_start != 0) {
					found_extent = 1;
					if (test_bit(BTRFS_ROOT_REF_COWS,
						     &root->state))
						inode_sub_bytes(inode, num_dec);
				}
			}
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			/*
			 * we can't truncate inline items that have had
			 * special encodings
			 */
			if (!del_item &&
			    btrfs_file_extent_encryption(leaf, fi) == 0 &&
			    btrfs_file_extent_other_encoding(leaf, fi) == 0) {

				/*
				 * Need to release path in order to truncate a
				 * compressed extent. So delete any accumulated
				 * extent items so far.
				 */
				if (btrfs_file_extent_compression(leaf, fi) !=
				    BTRFS_COMPRESS_NONE && pending_del_nr) {
					err = btrfs_del_items(trans, root, path,
							      pending_del_slot,
							      pending_del_nr);
					if (err) {
						btrfs_abort_transaction(trans,
									err);
						goto error;
					}
					pending_del_nr = 0;
				}

				err = truncate_inline_extent(inode, path,
							     &found_key,
							     item_end,
							     new_size);
				if (err) {
					btrfs_abort_transaction(trans, err);
					goto error;
				}
			} else if (test_bit(BTRFS_ROOT_REF_COWS,
					    &root->state)) {
				inode_sub_bytes(inode, item_end + 1 - new_size);
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
		should_throttle = 0;

		if (found_extent &&
		    (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
		     root == fs_info->tree_root)) {
			btrfs_set_path_blocking(path);
			bytes_deleted += extent_num_bytes;
			ret = btrfs_free_extent(trans, fs_info, extent_start,
						extent_num_bytes, 0,
						btrfs_header_owner(leaf),
						ino, extent_offset);
			BUG_ON(ret);
			if (btrfs_should_throttle_delayed_refs(trans, fs_info))
				btrfs_async_run_delayed_refs(fs_info,
					trans->delayed_ref_updates * 2,
					trans->transid, 0);
			if (be_nice) {
				if (truncate_space_check(trans, root,
							 extent_num_bytes)) {
					should_end = 1;
				}
				if (btrfs_should_throttle_delayed_refs(trans,
								       fs_info))
					should_throttle = 1;
			}
		}

		if (found_type == BTRFS_INODE_ITEM_KEY)
			break;

		if (path->slots[0] == 0 ||
		    path->slots[0] != pending_del_slot ||
		    should_throttle || should_end) {
			if (pending_del_nr) {
				ret = btrfs_del_items(trans, root, path,
						pending_del_slot,
						pending_del_nr);
				if (ret) {
					btrfs_abort_transaction(trans, ret);
					goto error;
				}
				pending_del_nr = 0;
			}
			btrfs_release_path(path);
			if (should_throttle) {
				unsigned long updates = trans->delayed_ref_updates;
				if (updates) {
					trans->delayed_ref_updates = 0;
					ret = btrfs_run_delayed_refs(trans,
								   fs_info,
								   updates * 2);
					if (ret && !err)
						err = ret;
				}
			}
			/*
			 * if we failed to refill our space rsv, bail out
			 * and let the transaction restart
			 */
			if (should_end) {
				err = -EAGAIN;
				goto error;
			}
			goto search_again;
		} else {
			path->slots[0]--;
		}
	}
out:
	if (pending_del_nr) {
		ret = btrfs_del_items(trans, root, path, pending_del_slot,
				      pending_del_nr);
		if (ret)
			btrfs_abort_transaction(trans, ret);
	}
error:
	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID)
		btrfs_ordered_update_i_size(inode, last_size, NULL);

	btrfs_free_path(path);

	if (be_nice && bytes_deleted > SZ_32M) {
		unsigned long updates = trans->delayed_ref_updates;
		if (updates) {
			trans->delayed_ref_updates = 0;
			ret = btrfs_run_delayed_refs(trans, fs_info,
						     updates * 2);
			if (ret && !err)
				err = ret;
		}
	}
	return err;
}

/*
 * btrfs_truncate_block - read, zero a chunk and write a block
 * @inode - inode that we're zeroing
 * @from - the offset to start zeroing
 * @len - the length to zero, 0 to zero the entire range respective to the
 *	offset
 * @front - zero up to the offset instead of from the offset on
 *
 * This will find the block for the "from" offset and cow the block and zero the
 * part we want to zero.  This is used with truncate and hole punching.
 */
int btrfs_truncate_block(struct inode *inode, loff_t from, loff_t len,
			int front)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct address_space *mapping = inode->i_mapping;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	char *kaddr;
	u32 blocksize = fs_info->sectorsize;
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (blocksize - 1);
	struct page *page;
	gfp_t mask = btrfs_alloc_write_mask(mapping);
	int ret = 0;
	u64 block_start;
	u64 block_end;

	if ((offset & (blocksize - 1)) == 0 &&
	    (!len || ((len & (blocksize - 1)) == 0)))
		goto out;

	ret = btrfs_delalloc_reserve_space(inode,
			round_down(from, blocksize), blocksize);
	if (ret)
		goto out;

again:
	page = find_or_create_page(mapping, index, mask);
	if (!page) {
		btrfs_delalloc_release_space(inode,
				round_down(from, blocksize),
				blocksize);
		ret = -ENOMEM;
		goto out;
	}

	block_start = round_down(from, blocksize);
	block_end = block_start + blocksize - 1;

	if (!PageUptodate(page)) {
		ret = btrfs_readpage(NULL, page);
		lock_page(page);
		if (page->mapping != mapping) {
			unlock_page(page);
			put_page(page);
			goto again;
		}
		if (!PageUptodate(page)) {
			ret = -EIO;
			goto out_unlock;
		}
	}
	wait_on_page_writeback(page);

	lock_extent_bits(io_tree, block_start, block_end, &cached_state);
	set_page_extent_mapped(page);

	ordered = btrfs_lookup_ordered_extent(inode, block_start);
	if (ordered) {
		unlock_extent_cached(io_tree, block_start, block_end,
				     &cached_state, GFP_NOFS);
		unlock_page(page);
		put_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	clear_extent_bit(&BTRFS_I(inode)->io_tree, block_start, block_end,
			  EXTENT_DIRTY | EXTENT_DELALLOC |
			  EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			  0, 0, &cached_state, GFP_NOFS);

	ret = btrfs_set_extent_delalloc(inode, block_start, block_end,
					&cached_state, 0);
	if (ret) {
		unlock_extent_cached(io_tree, block_start, block_end,
				     &cached_state, GFP_NOFS);
		goto out_unlock;
	}

	if (offset != blocksize) {
		if (!len)
			len = blocksize - offset;
		kaddr = kmap(page);
		if (front)
			memset(kaddr + (block_start - page_offset(page)),
				0, offset);
		else
			memset(kaddr + (block_start - page_offset(page)) +  offset,
				0, len);
		flush_dcache_page(page);
		kunmap(page);
	}
	ClearPageChecked(page);
	set_page_dirty(page);
	unlock_extent_cached(io_tree, block_start, block_end, &cached_state,
			     GFP_NOFS);

out_unlock:
	if (ret)
		btrfs_delalloc_release_space(inode, block_start,
					     blocksize);
	unlock_page(page);
	put_page(page);
out:
	return ret;
}

static int maybe_insert_hole(struct btrfs_root *root, struct inode *inode,
			     u64 offset, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans;
	int ret;

	/*
	 * Still need to make sure the inode looks like it's been updated so
	 * that any holes get logged if we fsync.
	 */
	if (btrfs_fs_incompat(fs_info, NO_HOLES)) {
		BTRFS_I(inode)->last_trans = fs_info->generation;
		BTRFS_I(inode)->last_sub_trans = root->log_transid;
		BTRFS_I(inode)->last_log_commit = root->last_log_commit;
		return 0;
	}

	/*
	 * 1 - for the one we're dropping
	 * 1 - for the one we're adding
	 * 1 - for updating the inode.
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_drop_extents(trans, root, inode, offset, offset + len, 1);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		return ret;
	}

	ret = btrfs_insert_file_extent(trans, root, btrfs_ino(inode), offset,
				       0, 0, len, 0, len, 0, 0, 0);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	else
		btrfs_update_inode(trans, root, inode);
	btrfs_end_transaction(trans);
	return ret;
}

/*
 * This function puts in dummy file extents for the area we're creating a hole
 * for.  So if we are truncating this file to a larger size we need to insert
 * these file extents so that btrfs_get_extent will return a EXTENT_MAP_HOLE for
 * the range between oldsize and size
 */
int btrfs_cont_expand(struct inode *inode, loff_t oldsize, loff_t size)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em = NULL;
	struct extent_state *cached_state = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 hole_start = ALIGN(oldsize, fs_info->sectorsize);
	u64 block_end = ALIGN(size, fs_info->sectorsize);
	u64 last_byte;
	u64 cur_offset;
	u64 hole_size;
	int err = 0;

	/*
	 * If our size started in the middle of a block we need to zero out the
	 * rest of the block before we expand the i_size, otherwise we could
	 * expose stale data.
	 */
	err = btrfs_truncate_block(inode, oldsize, 0, 0);
	if (err)
		return err;

	if (size <= hole_start)
		return 0;

	while (1) {
		struct btrfs_ordered_extent *ordered;

		lock_extent_bits(io_tree, hole_start, block_end - 1,
				 &cached_state);
		ordered = btrfs_lookup_ordered_range(inode, hole_start,
						     block_end - hole_start);
		if (!ordered)
			break;
		unlock_extent_cached(io_tree, hole_start, block_end - 1,
				     &cached_state, GFP_NOFS);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
	}

	cur_offset = hole_start;
	while (1) {
		em = btrfs_get_extent(inode, NULL, 0, cur_offset,
				block_end - cur_offset, 0);
		if (IS_ERR(em)) {
			err = PTR_ERR(em);
			em = NULL;
			break;
		}
		last_byte = min(extent_map_end(em), block_end);
		last_byte = ALIGN(last_byte, fs_info->sectorsize);
		if (!test_bit(EXTENT_FLAG_PREALLOC, &em->flags)) {
			struct extent_map *hole_em;
			hole_size = last_byte - cur_offset;

			err = maybe_insert_hole(root, inode, cur_offset,
						hole_size);
			if (err)
				break;
			btrfs_drop_extent_cache(inode, cur_offset,
						cur_offset + hole_size - 1, 0);
			hole_em = alloc_extent_map();
			if (!hole_em) {
				set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
					&BTRFS_I(inode)->runtime_flags);
				goto next;
			}
			hole_em->start = cur_offset;
			hole_em->len = hole_size;
			hole_em->orig_start = cur_offset;

			hole_em->block_start = EXTENT_MAP_HOLE;
			hole_em->block_len = 0;
			hole_em->orig_block_len = 0;
			hole_em->ram_bytes = hole_size;
			hole_em->bdev = fs_info->fs_devices->latest_bdev;
			hole_em->compress_type = BTRFS_COMPRESS_NONE;
			hole_em->generation = fs_info->generation;

			while (1) {
				write_lock(&em_tree->lock);
				err = add_extent_mapping(em_tree, hole_em, 1);
				write_unlock(&em_tree->lock);
				if (err != -EEXIST)
					break;
				btrfs_drop_extent_cache(inode, cur_offset,
							cur_offset +
							hole_size - 1, 0);
			}
			free_extent_map(hole_em);
		}
next:
		free_extent_map(em);
		em = NULL;
		cur_offset = last_byte;
		if (cur_offset >= block_end)
			break;
	}
	free_extent_map(em);
	unlock_extent_cached(io_tree, hole_start, block_end - 1, &cached_state,
			     GFP_NOFS);
	return err;
}

static int btrfs_setsize(struct inode *inode, struct iattr *attr)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	loff_t oldsize = i_size_read(inode);
	loff_t newsize = attr->ia_size;
	int mask = attr->ia_valid;
	int ret;

	/*
	 * The regular truncate() case without ATTR_CTIME and ATTR_MTIME is a
	 * special case where we need to update the times despite not having
	 * these flags set.  For all other operations the VFS set these flags
	 * explicitly if it wants a timestamp update.
	 */
	if (newsize != oldsize) {
		inode_inc_iversion(inode);
		if (!(mask & (ATTR_CTIME | ATTR_MTIME)))
			inode->i_ctime = inode->i_mtime =
				current_time(inode);
	}

	if (newsize > oldsize) {
		/*
		 * Don't do an expanding truncate while snapshoting is ongoing.
		 * This is to ensure the snapshot captures a fully consistent
		 * state of this file - if the snapshot captures this expanding
		 * truncation, it must capture all writes that happened before
		 * this truncation.
		 */
		btrfs_wait_for_snapshot_creation(root);
		ret = btrfs_cont_expand(inode, oldsize, newsize);
		if (ret) {
			btrfs_end_write_no_snapshoting(root);
			return ret;
		}

		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			btrfs_end_write_no_snapshoting(root);
			return PTR_ERR(trans);
		}

		i_size_write(inode, newsize);
		btrfs_ordered_update_i_size(inode, i_size_read(inode), NULL);
		pagecache_isize_extended(inode, oldsize, newsize);
		ret = btrfs_update_inode(trans, root, inode);
		btrfs_end_write_no_snapshoting(root);
		btrfs_end_transaction(trans);
	} else {

		/*
		 * We're truncating a file that used to have good data down to
		 * zero. Make sure it gets into the ordered flush list so that
		 * any new writes get down to disk quickly.
		 */
		if (newsize == 0)
			set_bit(BTRFS_INODE_ORDERED_DATA_CLOSE,
				&BTRFS_I(inode)->runtime_flags);

		/*
		 * 1 for the orphan item we're going to add
		 * 1 for the orphan item deletion.
		 */
		trans = btrfs_start_transaction(root, 2);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		/*
		 * We need to do this in case we fail at _any_ point during the
		 * actual truncate.  Once we do the truncate_setsize we could
		 * invalidate pages which forces any outstanding ordered io to
		 * be instantly completed which will give us extents that need
		 * to be truncated.  If we fail to get an orphan inode down we
		 * could have left over extents that were never meant to live,
		 * so we need to guarantee from this point on that everything
		 * will be consistent.
		 */
		ret = btrfs_orphan_add(trans, inode);
		btrfs_end_transaction(trans);
		if (ret)
			return ret;

		/* we don't support swapfiles, so vmtruncate shouldn't fail */
		truncate_setsize(inode, newsize);

		/* Disable nonlocked read DIO to avoid the end less truncate */
		btrfs_inode_block_unlocked_dio(inode);
		inode_dio_wait(inode);
		btrfs_inode_resume_unlocked_dio(inode);

		ret = btrfs_truncate(inode);
		if (ret && inode->i_nlink) {
			int err;

			/*
			 * failed to truncate, disk_i_size is only adjusted down
			 * as we remove extents, so it should represent the true
			 * size of the inode, so reset the in memory size and
			 * delete our orphan entry.
			 */
			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans)) {
				btrfs_orphan_del(NULL, inode);
				return ret;
			}
			i_size_write(inode, BTRFS_I(inode)->disk_i_size);
			err = btrfs_orphan_del(trans, inode);
			if (err)
				btrfs_abort_transaction(trans, err);
			btrfs_end_transaction(trans);
		}
	}

	return ret;
}

static int btrfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int err;

	if (btrfs_root_readonly(root))
		return -EROFS;

	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		err = btrfs_setsize(inode, attr);
		if (err)
			return err;
	}

	if (attr->ia_valid) {
		setattr_copy(inode, attr);
		inode_inc_iversion(inode);
		err = btrfs_dirty_inode(inode);

		if (!err && attr->ia_valid & ATTR_MODE)
			err = posix_acl_chmod(inode, inode->i_mode);
	}

	return err;
}

/*
 * While truncating the inode pages during eviction, we get the VFS calling
 * btrfs_invalidatepage() against each page of the inode. This is slow because
 * the calls to btrfs_invalidatepage() result in a huge amount of calls to
 * lock_extent_bits() and clear_extent_bit(), which keep merging and splitting
 * extent_state structures over and over, wasting lots of time.
 *
 * Therefore if the inode is being evicted, let btrfs_invalidatepage() skip all
 * those expensive operations on a per page basis and do only the ordered io
 * finishing, while we release here the extent_map and extent_state structures,
 * without the excessive merging and splitting.
 */
static void evict_inode_truncate_pages(struct inode *inode)
{
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map_tree *map_tree = &BTRFS_I(inode)->extent_tree;
	struct rb_node *node;

	ASSERT(inode->i_state & I_FREEING);
	truncate_inode_pages_final(&inode->i_data);

	write_lock(&map_tree->lock);
	while (!RB_EMPTY_ROOT(&map_tree->map)) {
		struct extent_map *em;

		node = rb_first(&map_tree->map);
		em = rb_entry(node, struct extent_map, rb_node);
		clear_bit(EXTENT_FLAG_PINNED, &em->flags);
		clear_bit(EXTENT_FLAG_LOGGING, &em->flags);
		remove_extent_mapping(map_tree, em);
		free_extent_map(em);
		if (need_resched()) {
			write_unlock(&map_tree->lock);
			cond_resched();
			write_lock(&map_tree->lock);
		}
	}
	write_unlock(&map_tree->lock);

	/*
	 * Keep looping until we have no more ranges in the io tree.
	 * We can have ongoing bios started by readpages (called from readahead)
	 * that have their endio callback (extent_io.c:end_bio_extent_readpage)
	 * still in progress (unlocked the pages in the bio but did not yet
	 * unlocked the ranges in the io tree). Therefore this means some
	 * ranges can still be locked and eviction started because before
	 * submitting those bios, which are executed by a separate task (work
	 * queue kthread), inode references (inode->i_count) were not taken
	 * (which would be dropped in the end io callback of each bio).
	 * Therefore here we effectively end up waiting for those bios and
	 * anyone else holding locked ranges without having bumped the inode's
	 * reference count - if we don't do it, when they access the inode's
	 * io_tree to unlock a range it may be too late, leading to an
	 * use-after-free issue.
	 */
	spin_lock(&io_tree->lock);
	while (!RB_EMPTY_ROOT(&io_tree->state)) {
		struct extent_state *state;
		struct extent_state *cached_state = NULL;
		u64 start;
		u64 end;

		node = rb_first(&io_tree->state);
		state = rb_entry(node, struct extent_state, rb_node);
		start = state->start;
		end = state->end;
		spin_unlock(&io_tree->lock);

		lock_extent_bits(io_tree, start, end, &cached_state);

		/*
		 * If still has DELALLOC flag, the extent didn't reach disk,
		 * and its reserved space won't be freed by delayed_ref.
		 * So we need to free its reserved space here.
		 * (Refer to comment in btrfs_invalidatepage, case 2)
		 *
		 * Note, end is the bytenr of last byte, so we need + 1 here.
		 */
		if (state->state & EXTENT_DELALLOC)
			btrfs_qgroup_free_data(inode, start, end - start + 1);

		clear_extent_bit(io_tree, start, end,
				 EXTENT_LOCKED | EXTENT_DIRTY |
				 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING |
				 EXTENT_DEFRAG, 1, 1,
				 &cached_state, GFP_NOFS);

		cond_resched();
		spin_lock(&io_tree->lock);
	}
	spin_unlock(&io_tree->lock);
}

void btrfs_evict_inode(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *rsv, *global_rsv;
	int steal_from_global = 0;
	u64 min_size;
	int ret;

	trace_btrfs_inode_evict(inode);

	if (!root) {
		kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
		return;
	}

	min_size = btrfs_calc_trunc_metadata_size(fs_info, 1);

	evict_inode_truncate_pages(inode);

	if (inode->i_nlink &&
	    ((btrfs_root_refs(&root->root_item) != 0 &&
	      root->root_key.objectid != BTRFS_ROOT_TREE_OBJECTID) ||
	     btrfs_is_free_space_inode(inode)))
		goto no_delete;

	if (is_bad_inode(inode)) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete;
	}
	/* do we really want it for ->i_nlink > 0 and zero btrfs_root_refs? */
	if (!special_file(inode->i_mode))
		btrfs_wait_ordered_range(inode, 0, (u64)-1);

	btrfs_free_io_failure_record(inode, 0, (u64)-1);

	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags)) {
		BUG_ON(test_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
				 &BTRFS_I(inode)->runtime_flags));
		goto no_delete;
	}

	if (inode->i_nlink > 0) {
		BUG_ON(btrfs_root_refs(&root->root_item) != 0 &&
		       root->root_key.objectid != BTRFS_ROOT_TREE_OBJECTID);
		goto no_delete;
	}

	ret = btrfs_commit_inode_delayed_inode(inode);
	if (ret) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete;
	}

	rsv = btrfs_alloc_block_rsv(fs_info, BTRFS_BLOCK_RSV_TEMP);
	if (!rsv) {
		btrfs_orphan_del(NULL, inode);
		goto no_delete;
	}
	rsv->size = min_size;
	rsv->failfast = 1;
	global_rsv = &fs_info->global_block_rsv;

	btrfs_i_size_write(inode, 0);

	/*
	 * This is a bit simpler than btrfs_truncate since we've already
	 * reserved our space for our orphan item in the unlink, so we just
	 * need to reserve some slack space in case we add bytes and update
	 * inode item when doing the truncate.
	 */
	while (1) {
		ret = btrfs_block_rsv_refill(root, rsv, min_size,
					     BTRFS_RESERVE_FLUSH_LIMIT);

		/*
		 * Try and steal from the global reserve since we will
		 * likely not use this space anyway, we want to try as
		 * hard as possible to get this to work.
		 */
		if (ret)
			steal_from_global++;
		else
			steal_from_global = 0;
		ret = 0;

		/*
		 * steal_from_global == 0: we reserved stuff, hooray!
		 * steal_from_global == 1: we didn't reserve stuff, boo!
		 * steal_from_global == 2: we've committed, still not a lot of
		 * room but maybe we'll have room in the global reserve this
		 * time.
		 * steal_from_global == 3: abandon all hope!
		 */
		if (steal_from_global > 2) {
			btrfs_warn(fs_info,
				   "Could not get space for a delete, will truncate on mount %d",
				   ret);
			btrfs_orphan_del(NULL, inode);
			btrfs_free_block_rsv(fs_info, rsv);
			goto no_delete;
		}

		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			btrfs_orphan_del(NULL, inode);
			btrfs_free_block_rsv(fs_info, rsv);
			goto no_delete;
		}

		/*
		 * We can't just steal from the global reserve, we need to make
		 * sure there is room to do it, if not we need to commit and try
		 * again.
		 */
		if (steal_from_global) {
			if (!btrfs_check_space_for_delayed_refs(trans, fs_info))
				ret = btrfs_block_rsv_migrate(global_rsv, rsv,
							      min_size, 0);
			else
				ret = -ENOSPC;
		}

		/*
		 * Couldn't steal from the global reserve, we have too much
		 * pending stuff built up, commit the transaction and try it
		 * again.
		 */
		if (ret) {
			ret = btrfs_commit_transaction(trans);
			if (ret) {
				btrfs_orphan_del(NULL, inode);
				btrfs_free_block_rsv(fs_info, rsv);
				goto no_delete;
			}
			continue;
		} else {
			steal_from_global = 0;
		}

		trans->block_rsv = rsv;

		ret = btrfs_truncate_inode_items(trans, root, inode, 0, 0);
		if (ret != -ENOSPC && ret != -EAGAIN)
			break;

		trans->block_rsv = &fs_info->trans_block_rsv;
		btrfs_end_transaction(trans);
		trans = NULL;
		btrfs_btree_balance_dirty(fs_info);
	}

	btrfs_free_block_rsv(fs_info, rsv);

	/*
	 * Errors here aren't a big deal, it just means we leave orphan items
	 * in the tree.  They will be cleaned up on the next mount.
	 */
	if (ret == 0) {
		trans->block_rsv = root->orphan_block_rsv;
		btrfs_orphan_del(trans, inode);
	} else {
		btrfs_orphan_del(NULL, inode);
	}

	trans->block_rsv = &fs_info->trans_block_rsv;
	if (!(root == fs_info->tree_root ||
	      root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID))
		btrfs_return_ino(root, btrfs_ino(inode));

	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
no_delete:
	btrfs_remove_delayed_node(inode);
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
	if (!path)
		return -ENOMEM;

	di = btrfs_lookup_dir_item(NULL, root, path, btrfs_ino(dir), name,
				    namelen, 0);
	if (IS_ERR(di))
		ret = PTR_ERR(di);

	if (IS_ERR_OR_NULL(di))
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
static int fixup_tree_root_location(struct btrfs_fs_info *fs_info,
				    struct inode *dir,
				    struct dentry *dentry,
				    struct btrfs_key *location,
				    struct btrfs_root **sub_root)
{
	struct btrfs_path *path;
	struct btrfs_root *new_root;
	struct btrfs_root_ref *ref;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out;
	}

	err = -ENOENT;
	key.objectid = BTRFS_I(dir)->root->root_key.objectid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = location->objectid;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	if (ret) {
		if (ret < 0)
			err = ret;
		goto out;
	}

	leaf = path->nodes[0];
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_ref);
	if (btrfs_root_ref_dirid(leaf, ref) != btrfs_ino(dir) ||
	    btrfs_root_ref_name_len(leaf, ref) != dentry->d_name.len)
		goto out;

	ret = memcmp_extent_buffer(leaf, dentry->d_name.name,
				   (unsigned long)(ref + 1),
				   dentry->d_name.len);
	if (ret)
		goto out;

	btrfs_release_path(path);

	new_root = btrfs_read_fs_root_no_name(fs_info, location);
	if (IS_ERR(new_root)) {
		err = PTR_ERR(new_root);
		goto out;
	}

	*sub_root = new_root;
	location->objectid = btrfs_root_dirid(&new_root->root_item);
	location->type = BTRFS_INODE_ITEM_KEY;
	location->offset = 0;
	err = 0;
out:
	btrfs_free_path(path);
	return err;
}

static void inode_tree_add(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_inode *entry;
	struct rb_node **p;
	struct rb_node *parent;
	struct rb_node *new = &BTRFS_I(inode)->rb_node;
	u64 ino = btrfs_ino(inode);

	if (inode_unhashed(inode))
		return;
	parent = NULL;
	spin_lock(&root->inode_lock);
	p = &root->inode_tree.rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_inode, rb_node);

		if (ino < btrfs_ino(&entry->vfs_inode))
			p = &parent->rb_left;
		else if (ino > btrfs_ino(&entry->vfs_inode))
			p = &parent->rb_right;
		else {
			WARN_ON(!(entry->vfs_inode.i_state &
				  (I_WILL_FREE | I_FREEING)));
			rb_replace_node(parent, new, &root->inode_tree);
			RB_CLEAR_NODE(parent);
			spin_unlock(&root->inode_lock);
			return;
		}
	}
	rb_link_node(new, parent, p);
	rb_insert_color(new, &root->inode_tree);
	spin_unlock(&root->inode_lock);
}

static void inode_tree_del(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int empty = 0;

	spin_lock(&root->inode_lock);
	if (!RB_EMPTY_NODE(&BTRFS_I(inode)->rb_node)) {
		rb_erase(&BTRFS_I(inode)->rb_node, &root->inode_tree);
		RB_CLEAR_NODE(&BTRFS_I(inode)->rb_node);
		empty = RB_EMPTY_ROOT(&root->inode_tree);
	}
	spin_unlock(&root->inode_lock);

	if (empty && btrfs_root_refs(&root->root_item) == 0) {
		synchronize_srcu(&fs_info->subvol_srcu);
		spin_lock(&root->inode_lock);
		empty = RB_EMPTY_ROOT(&root->inode_tree);
		spin_unlock(&root->inode_lock);
		if (empty)
			btrfs_add_dead_root(root);
	}
}

void btrfs_invalidate_inodes(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_node *node;
	struct rb_node *prev;
	struct btrfs_inode *entry;
	struct inode *inode;
	u64 objectid = 0;

	if (!test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		WARN_ON(btrfs_root_refs(&root->root_item) != 0);

	spin_lock(&root->inode_lock);
again:
	node = root->inode_tree.rb_node;
	prev = NULL;
	while (node) {
		prev = node;
		entry = rb_entry(node, struct btrfs_inode, rb_node);

		if (objectid < btrfs_ino(&entry->vfs_inode))
			node = node->rb_left;
		else if (objectid > btrfs_ino(&entry->vfs_inode))
			node = node->rb_right;
		else
			break;
	}
	if (!node) {
		while (prev) {
			entry = rb_entry(prev, struct btrfs_inode, rb_node);
			if (objectid <= btrfs_ino(&entry->vfs_inode)) {
				node = prev;
				break;
			}
			prev = rb_next(prev);
		}
	}
	while (node) {
		entry = rb_entry(node, struct btrfs_inode, rb_node);
		objectid = btrfs_ino(&entry->vfs_inode) + 1;
		inode = igrab(&entry->vfs_inode);
		if (inode) {
			spin_unlock(&root->inode_lock);
			if (atomic_read(&inode->i_count) > 1)
				d_prune_aliases(inode);
			/*
			 * btrfs_drop_inode will have it removed from
			 * the inode cache when its usage count
			 * hits zero.
			 */
			iput(inode);
			cond_resched();
			spin_lock(&root->inode_lock);
			goto again;
		}

		if (cond_resched_lock(&root->inode_lock))
			goto again;

		node = rb_next(node);
	}
	spin_unlock(&root->inode_lock);
}

static int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;
	inode->i_ino = args->location->objectid;
	memcpy(&BTRFS_I(inode)->location, args->location,
	       sizeof(*args->location));
	BTRFS_I(inode)->root = args->root;
	return 0;
}

static int btrfs_find_actor(struct inode *inode, void *opaque)
{
	struct btrfs_iget_args *args = opaque;
	return args->location->objectid == BTRFS_I(inode)->location.objectid &&
		args->root == BTRFS_I(inode)->root;
}

static struct inode *btrfs_iget_locked(struct super_block *s,
				       struct btrfs_key *location,
				       struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	unsigned long hashval = btrfs_inode_hash(location->objectid, root);

	args.location = location;
	args.root = root;

	inode = iget5_locked(s, hashval, btrfs_find_actor,
			     btrfs_init_locked_inode,
			     (void *)&args);
	return inode;
}

/* Get an inode object given its location and corresponding root.
 * Returns in *is_new if the inode was read from disk
 */
struct inode *btrfs_iget(struct super_block *s, struct btrfs_key *location,
			 struct btrfs_root *root, int *new)
{
	struct inode *inode;

	inode = btrfs_iget_locked(s, location, root);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		int ret;

		ret = btrfs_read_locked_inode(inode);
		if (!is_bad_inode(inode)) {
			inode_tree_add(inode);
			unlock_new_inode(inode);
			if (new)
				*new = 1;
		} else {
			unlock_new_inode(inode);
			iput(inode);
			ASSERT(ret < 0);
			inode = ERR_PTR(ret < 0 ? ret : -ESTALE);
		}
	}

	return inode;
}

static struct inode *new_simple_dir(struct super_block *s,
				    struct btrfs_key *key,
				    struct btrfs_root *root)
{
	struct inode *inode = new_inode(s);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	BTRFS_I(inode)->root = root;
	memcpy(&BTRFS_I(inode)->location, key, sizeof(*key));
	set_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags);

	inode->i_ino = BTRFS_EMPTY_SUBVOL_DIR_OBJECTID;
	inode->i_op = &btrfs_dir_ro_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO;
	inode->i_mtime = current_time(inode);
	inode->i_atime = inode->i_mtime;
	inode->i_ctime = inode->i_mtime;
	BTRFS_I(inode)->i_otime = inode->i_mtime;

	return inode;
}

struct inode *btrfs_lookup_dentry(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct inode *inode;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location;
	int index;
	int ret = 0;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(dir, dentry, &location);
	if (ret < 0)
		return ERR_PTR(ret);

	if (location.objectid == 0)
		return ERR_PTR(-ENOENT);

	if (location.type == BTRFS_INODE_ITEM_KEY) {
		inode = btrfs_iget(dir->i_sb, &location, root, NULL);
		return inode;
	}

	BUG_ON(location.type != BTRFS_ROOT_ITEM_KEY);

	index = srcu_read_lock(&fs_info->subvol_srcu);
	ret = fixup_tree_root_location(fs_info, dir, dentry,
				       &location, &sub_root);
	if (ret < 0) {
		if (ret != -ENOENT)
			inode = ERR_PTR(ret);
		else
			inode = new_simple_dir(dir->i_sb, &location, sub_root);
	} else {
		inode = btrfs_iget(dir->i_sb, &location, sub_root, NULL);
	}
	srcu_read_unlock(&fs_info->subvol_srcu, index);

	if (!IS_ERR(inode) && root != sub_root) {
		down_read(&fs_info->cleanup_work_sem);
		if (!(inode->i_sb->s_flags & MS_RDONLY))
			ret = btrfs_orphan_cleanup(sub_root);
		up_read(&fs_info->cleanup_work_sem);
		if (ret) {
			iput(inode);
			inode = ERR_PTR(ret);
		}
	}

	return inode;
}

static int btrfs_dentry_delete(const struct dentry *dentry)
{
	struct btrfs_root *root;
	struct inode *inode = d_inode(dentry);

	if (!inode && !IS_ROOT(dentry))
		inode = d_inode(dentry->d_parent);

	if (inode) {
		root = BTRFS_I(inode)->root;
		if (btrfs_root_refs(&root->root_item) == 0)
			return 1;

		if (btrfs_ino(inode) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)
			return 1;
	}
	return 0;
}

static void btrfs_dentry_release(struct dentry *dentry)
{
	kfree(dentry->d_fsdata);
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct inode *inode;

	inode = btrfs_lookup_dentry(dir, dentry);
	if (IS_ERR(inode)) {
		if (PTR_ERR(inode) == -ENOENT)
			inode = NULL;
		else
			return ERR_CAST(inode);
	}

	return d_splice_alias(inode, dentry);
}

unsigned char btrfs_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

static int btrfs_real_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct list_head ins_list;
	struct list_head del_list;
	int ret;
	struct extent_buffer *leaf;
	int slot;
	unsigned char d_type;
	int over = 0;
	char tmp_name[32];
	char *name_ptr;
	int name_len;
	bool put = false;
	struct btrfs_key location;

	if (!dir_emit_dots(file, ctx))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_FORWARD;

	INIT_LIST_HEAD(&ins_list);
	INIT_LIST_HEAD(&del_list);
	put = btrfs_readdir_get_delayed_items(inode, &ins_list, &del_list);

	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = ctx->pos;
	key.objectid = btrfs_ino(inode);

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto err;
			else if (ret > 0)
				break;
			continue;
		}

		item = btrfs_item_nr(slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid)
			break;
		if (found_key.type != BTRFS_DIR_INDEX_KEY)
			break;
		if (found_key.offset < ctx->pos)
			goto next;
		if (btrfs_should_delete_dir_index(&del_list, found_key.offset))
			goto next;

		ctx->pos = found_key.offset;

		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		if (verify_dir_item(fs_info, leaf, di))
			goto next;

		name_len = btrfs_dir_name_len(leaf, di);
		if (name_len <= sizeof(tmp_name)) {
			name_ptr = tmp_name;
		} else {
			name_ptr = kmalloc(name_len, GFP_KERNEL);
			if (!name_ptr) {
				ret = -ENOMEM;
				goto err;
			}
		}
		read_extent_buffer(leaf, name_ptr, (unsigned long)(di + 1),
				   name_len);

		d_type = btrfs_filetype_table[btrfs_dir_type(leaf, di)];
		btrfs_dir_item_key_to_cpu(leaf, di, &location);

		over = !dir_emit(ctx, name_ptr, name_len, location.objectid,
				 d_type);

		if (name_ptr != tmp_name)
			kfree(name_ptr);

		if (over)
			goto nopos;
		ctx->pos++;
next:
		path->slots[0]++;
	}

	ret = btrfs_readdir_delayed_dir_index(ctx, &ins_list);
	if (ret)
		goto nopos;

	/*
	 * Stop new entries from being returned after we return the last
	 * entry.
	 *
	 * New directory entries are assigned a strictly increasing
	 * offset.  This means that new entries created during readdir
	 * are *guaranteed* to be seen in the future by that readdir.
	 * This has broken buggy programs which operate on names as
	 * they're returned by readdir.  Until we re-use freed offsets
	 * we have this hack to stop new entries from being returned
	 * under the assumption that they'll never reach this huge
	 * offset.
	 *
	 * This is being careful not to overflow 32bit loff_t unless the
	 * last entry requires it because doing so has broken 32bit apps
	 * in the past.
	 */
	if (ctx->pos >= INT_MAX)
		ctx->pos = LLONG_MAX;
	else
		ctx->pos = INT_MAX;
nopos:
	ret = 0;
err:
	if (put)
		btrfs_readdir_put_delayed_items(inode, &ins_list, &del_list);
	btrfs_free_path(path);
	return ret;
}

int btrfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	int ret = 0;
	bool nolock = false;

	if (test_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags))
		return 0;

	if (btrfs_fs_closing(root->fs_info) && btrfs_is_free_space_inode(inode))
		nolock = true;

	if (wbc->sync_mode == WB_SYNC_ALL) {
		if (nolock)
			trans = btrfs_join_transaction_nolock(root);
		else
			trans = btrfs_join_transaction(root);
		if (IS_ERR(trans))
			return PTR_ERR(trans);
		ret = btrfs_commit_transaction(trans);
	}
	return ret;
}

/*
 * This is somewhat expensive, updating the tree every time the
 * inode changes.  But, it is most likely to find the inode in cache.
 * FIXME, needs more benchmarking...there are no reasons other than performance
 * to keep or drop this code.
 */
static int btrfs_dirty_inode(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	int ret;

	if (test_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags))
		return 0;

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_update_inode(trans, root, inode);
	if (ret && ret == -ENOSPC) {
		/* whoops, lets try again with the full transaction */
		btrfs_end_transaction(trans);
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_update_inode(trans, root, inode);
	}
	btrfs_end_transaction(trans);
	if (BTRFS_I(inode)->delayed_node)
		btrfs_balance_delayed_items(fs_info);

	return ret;
}

/*
 * This is a copy of file_update_time.  We need this so we can return error on
 * ENOSPC for updating the inode in the case of file write and mmap writes.
 */
static int btrfs_update_time(struct inode *inode, struct timespec *now,
			     int flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (flags & S_VERSION)
		inode_inc_iversion(inode);
	if (flags & S_CTIME)
		inode->i_ctime = *now;
	if (flags & S_MTIME)
		inode->i_mtime = *now;
	if (flags & S_ATIME)
		inode->i_atime = *now;
	return btrfs_dirty_inode(inode);
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

	key.objectid = btrfs_ino(inode);
	key.type = BTRFS_DIR_INDEX_KEY;
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

	if (found_key.objectid != btrfs_ino(inode) ||
	    found_key.type != BTRFS_DIR_INDEX_KEY) {
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
		ret = btrfs_inode_delayed_dir_index_count(dir);
		if (ret) {
			ret = btrfs_set_inode_index_count(dir);
			if (ret)
				return ret;
		}
	}

	*index = BTRFS_I(dir)->index_cnt;
	BTRFS_I(dir)->index_cnt++;

	return ret;
}

static int btrfs_insert_inode_locked(struct inode *inode)
{
	struct btrfs_iget_args args;
	args.location = &BTRFS_I(inode)->location;
	args.root = BTRFS_I(inode)->root;

	return insert_inode_locked4(inode,
		   btrfs_inode_hash(inode->i_ino, BTRFS_I(inode)->root),
		   btrfs_find_actor, &args);
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct inode *dir,
				     const char *name, int name_len,
				     u64 ref_objectid, u64 objectid,
				     umode_t mode, u64 *index)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct inode *inode;
	struct btrfs_inode_item *inode_item;
	struct btrfs_key *location;
	struct btrfs_path *path;
	struct btrfs_inode_ref *ref;
	struct btrfs_key key[2];
	u32 sizes[2];
	int nitems = name ? 2 : 1;
	unsigned long ptr;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);

	inode = new_inode(fs_info->sb);
	if (!inode) {
		btrfs_free_path(path);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * O_TMPFILE, set link count to 0, so that after this point,
	 * we fill in an inode item with the correct link count.
	 */
	if (!name)
		set_nlink(inode, 0);

	/*
	 * we have to initialize this early, so we can reclaim the inode
	 * number if we fail afterwards in this function.
	 */
	inode->i_ino = objectid;

	if (dir && name) {
		trace_btrfs_inode_request(dir);

		ret = btrfs_set_inode_index(dir, index);
		if (ret) {
			btrfs_free_path(path);
			iput(inode);
			return ERR_PTR(ret);
		}
	} else if (dir) {
		*index = 0;
	}
	/*
	 * index_cnt is ignored for everything but a dir,
	 * btrfs_get_inode_index_count has an explanation for the magic
	 * number
	 */
	BTRFS_I(inode)->index_cnt = 2;
	BTRFS_I(inode)->dir_index = *index;
	BTRFS_I(inode)->root = root;
	BTRFS_I(inode)->generation = trans->transid;
	inode->i_generation = BTRFS_I(inode)->generation;

	/*
	 * We could have gotten an inode number from somebody who was fsynced
	 * and then removed in this same transaction, so let's just set full
	 * sync since it will be a full sync anyway and this will blow away the
	 * old info in the log.
	 */
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(inode)->runtime_flags);

	key[0].objectid = objectid;
	key[0].type = BTRFS_INODE_ITEM_KEY;
	key[0].offset = 0;

	sizes[0] = sizeof(struct btrfs_inode_item);

	if (name) {
		/*
		 * Start new inodes with an inode_ref. This is slightly more
		 * efficient for small numbers of hard links since they will
		 * be packed into one item. Extended refs will kick in if we
		 * add more hard links than can fit in the ref item.
		 */
		key[1].objectid = objectid;
		key[1].type = BTRFS_INODE_REF_KEY;
		key[1].offset = ref_objectid;

		sizes[1] = name_len + sizeof(*ref);
	}

	location = &BTRFS_I(inode)->location;
	location->objectid = objectid;
	location->offset = 0;
	location->type = BTRFS_INODE_ITEM_KEY;

	ret = btrfs_insert_inode_locked(inode);
	if (ret < 0)
		goto fail;

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_items(trans, root, path, key, sizes, nitems);
	if (ret != 0)
		goto fail_unlock;

	inode_init_owner(inode, dir, mode);
	inode_set_bytes(inode, 0);

	inode->i_mtime = current_time(inode);
	inode->i_atime = inode->i_mtime;
	inode->i_ctime = inode->i_mtime;
	BTRFS_I(inode)->i_otime = inode->i_mtime;

	inode_item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				  struct btrfs_inode_item);
	memzero_extent_buffer(path->nodes[0], (unsigned long)inode_item,
			     sizeof(*inode_item));
	fill_inode_item(trans, path->nodes[0], inode_item, inode);

	if (name) {
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0] + 1,
				     struct btrfs_inode_ref);
		btrfs_set_inode_ref_name_len(path->nodes[0], ref, name_len);
		btrfs_set_inode_ref_index(path->nodes[0], ref, *index);
		ptr = (unsigned long)(ref + 1);
		write_extent_buffer(path->nodes[0], name, ptr, name_len);
	}

	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	btrfs_inherit_iflags(inode, dir);

	if (S_ISREG(mode)) {
		if (btrfs_test_opt(fs_info, NODATASUM))
			BTRFS_I(inode)->flags |= BTRFS_INODE_NODATASUM;
		if (btrfs_test_opt(fs_info, NODATACOW))
			BTRFS_I(inode)->flags |= BTRFS_INODE_NODATACOW |
				BTRFS_INODE_NODATASUM;
	}

	inode_tree_add(inode);

	trace_btrfs_inode_new(inode);
	btrfs_set_inode_last_trans(trans, inode);

	btrfs_update_root_times(trans, root);

	ret = btrfs_inode_inherit_props(trans, inode, dir);
	if (ret)
		btrfs_err(fs_info,
			  "error inheriting props for ino %llu (root %llu): %d",
			  btrfs_ino(inode), root->root_key.objectid, ret);

	return inode;

fail_unlock:
	unlock_new_inode(inode);
fail:
	if (dir && name)
		BTRFS_I(dir)->index_cnt--;
	btrfs_free_path(path);
	iput(inode);
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
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret = 0;
	struct btrfs_key key;
	struct btrfs_root *root = BTRFS_I(parent_inode)->root;
	u64 ino = btrfs_ino(inode);
	u64 parent_ino = btrfs_ino(parent_inode);

	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		memcpy(&key, &BTRFS_I(inode)->root->root_key, sizeof(key));
	} else {
		key.objectid = ino;
		key.type = BTRFS_INODE_ITEM_KEY;
		key.offset = 0;
	}

	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		ret = btrfs_add_root_ref(trans, fs_info, key.objectid,
					 root->root_key.objectid, parent_ino,
					 index, name, name_len);
	} else if (add_backref) {
		ret = btrfs_insert_inode_ref(trans, root, name, name_len, ino,
					     parent_ino, index);
	}

	/* Nothing to clean up yet */
	if (ret)
		return ret;

	ret = btrfs_insert_dir_item(trans, root, name, name_len,
				    parent_inode, &key,
				    btrfs_inode_type(inode), index);
	if (ret == -EEXIST || ret == -EOVERFLOW)
		goto fail_dir_item;
	else if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	btrfs_i_size_write(parent_inode, parent_inode->i_size +
			   name_len * 2);
	inode_inc_iversion(parent_inode);
	parent_inode->i_mtime = parent_inode->i_ctime =
		current_time(parent_inode);
	ret = btrfs_update_inode(trans, root, parent_inode);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;

fail_dir_item:
	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		u64 local_index;
		int err;
		err = btrfs_del_root_ref(trans, fs_info, key.objectid,
					 root->root_key.objectid, parent_ino,
					 &local_index, name, name_len);

	} else if (add_backref) {
		u64 local_index;
		int err;

		err = btrfs_del_inode_ref(trans, root, name, name_len,
					  ino, parent_ino, &local_index);
	}
	return ret;
}

static int btrfs_add_nondir(struct btrfs_trans_handle *trans,
			    struct inode *dir, struct dentry *dentry,
			    struct inode *inode, int backref, u64 index)
{
	int err = btrfs_add_link(trans, dir, inode,
				 dentry->d_name.name, dentry->d_name.len,
				 backref, index);
	if (err > 0)
		err = -EEXIST;
	return err;
}

static int btrfs_mknod(struct inode *dir, struct dentry *dentry,
			umode_t mode, dev_t rdev)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	u64 objectid;
	u64 index = 0;

	/*
	 * 2 for inode item and ref
	 * 2 for dir items
	 * 1 for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_ino(root, &objectid);
	if (err)
		goto out_unlock;

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len, btrfs_ino(dir), objectid,
				mode, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_unlock;
	}

	/*
	* If the active LSM wants to access the inode during
	* d_instantiate it needs these. Smack checks to see
	* if the filesystem supports xattrs by looking at the
	* ops vector.
	*/
	inode->i_op = &btrfs_special_inode_operations;
	init_special_inode(inode, inode->i_mode, rdev);

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_unlock_inode;

	err = btrfs_add_nondir(trans, dir, dentry, inode, 0, index);
	if (err) {
		goto out_unlock_inode;
	} else {
		btrfs_update_inode(trans, root, inode);
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);
	}

out_unlock:
	btrfs_end_transaction(trans);
	btrfs_balance_delayed_items(fs_info);
	btrfs_btree_balance_dirty(fs_info);
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	return err;

out_unlock_inode:
	drop_inode = 1;
	unlock_new_inode(inode);
	goto out_unlock;

}

static int btrfs_create(struct inode *dir, struct dentry *dentry,
			umode_t mode, bool excl)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	int drop_inode_on_err = 0;
	int err;
	u64 objectid;
	u64 index = 0;

	/*
	 * 2 for inode item and ref
	 * 2 for dir items
	 * 1 for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_ino(root, &objectid);
	if (err)
		goto out_unlock;

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len, btrfs_ino(dir), objectid,
				mode, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_unlock;
	}
	drop_inode_on_err = 1;
	/*
	* If the active LSM wants to access the inode during
	* d_instantiate it needs these. Smack checks to see
	* if the filesystem supports xattrs by looking at the
	* ops vector.
	*/
	inode->i_fop = &btrfs_file_operations;
	inode->i_op = &btrfs_file_inode_operations;
	inode->i_mapping->a_ops = &btrfs_aops;

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_unlock_inode;

	err = btrfs_update_inode(trans, root, inode);
	if (err)
		goto out_unlock_inode;

	err = btrfs_add_nondir(trans, dir, dentry, inode, 0, index);
	if (err)
		goto out_unlock_inode;

	BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;
	unlock_new_inode(inode);
	d_instantiate(dentry, inode);

out_unlock:
	btrfs_end_transaction(trans);
	if (err && drop_inode_on_err) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_balance_delayed_items(fs_info);
	btrfs_btree_balance_dirty(fs_info);
	return err;

out_unlock_inode:
	unlock_new_inode(inode);
	goto out_unlock;

}

static int btrfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = d_inode(old_dentry);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 index;
	int err;
	int drop_inode = 0;

	/* do not allow sys_link's with other subvols of the same device */
	if (root->objectid != BTRFS_I(inode)->root->objectid)
		return -EXDEV;

	if (inode->i_nlink >= BTRFS_LINK_MAX)
		return -EMLINK;

	err = btrfs_set_inode_index(dir, &index);
	if (err)
		goto fail;

	/*
	 * 2 items for inode and inode ref
	 * 2 items for dir items
	 * 1 item for parent inode
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		trans = NULL;
		goto fail;
	}

	/* There are several dir indexes for this inode, clear the cache. */
	BTRFS_I(inode)->dir_index = 0ULL;
	inc_nlink(inode);
	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);
	ihold(inode);
	set_bit(BTRFS_INODE_COPY_EVERYTHING, &BTRFS_I(inode)->runtime_flags);

	err = btrfs_add_nondir(trans, dir, dentry, inode, 1, index);

	if (err) {
		drop_inode = 1;
	} else {
		struct dentry *parent = dentry->d_parent;
		err = btrfs_update_inode(trans, root, inode);
		if (err)
			goto fail;
		if (inode->i_nlink == 1) {
			/*
			 * If new hard link count is 1, it's a file created
			 * with open(2) O_TMPFILE flag.
			 */
			err = btrfs_orphan_del(trans, inode);
			if (err)
				goto fail;
		}
		d_instantiate(dentry, inode);
		btrfs_log_new_name(trans, inode, NULL, parent);
	}

	btrfs_balance_delayed_items(fs_info);
fail:
	if (trans)
		btrfs_end_transaction(trans);
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static int btrfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int err = 0;
	int drop_on_err = 0;
	u64 objectid = 0;
	u64 index = 0;

	/*
	 * 2 items for inode and ref
	 * 2 items for dir items
	 * 1 for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_ino(root, &objectid);
	if (err)
		goto out_fail;

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len, btrfs_ino(dir), objectid,
				S_IFDIR | mode, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_fail;
	}

	drop_on_err = 1;
	/* these must be set before we unlock the inode */
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_fail_inode;

	btrfs_i_size_write(inode, 0);
	err = btrfs_update_inode(trans, root, inode);
	if (err)
		goto out_fail_inode;

	err = btrfs_add_link(trans, dir, inode, dentry->d_name.name,
			     dentry->d_name.len, 0, index);
	if (err)
		goto out_fail_inode;

	d_instantiate(dentry, inode);
	/*
	 * mkdir is special.  We're unlocking after we call d_instantiate
	 * to avoid a race with nfsd calling d_instantiate.
	 */
	unlock_new_inode(inode);
	drop_on_err = 0;

out_fail:
	btrfs_end_transaction(trans);
	if (drop_on_err) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_balance_delayed_items(fs_info);
	btrfs_btree_balance_dirty(fs_info);
	return err;

out_fail_inode:
	unlock_new_inode(inode);
	goto out_fail;
}

/* Find next extent map of a given extent map, caller needs to ensure locks */
static struct extent_map *next_extent_map(struct extent_map *em)
{
	struct rb_node *next;

	next = rb_next(&em->rb_node);
	if (!next)
		return NULL;
	return container_of(next, struct extent_map, rb_node);
}

static struct extent_map *prev_extent_map(struct extent_map *em)
{
	struct rb_node *prev;

	prev = rb_prev(&em->rb_node);
	if (!prev)
		return NULL;
	return container_of(prev, struct extent_map, rb_node);
}

/* helper for btfs_get_extent.  Given an existing extent in the tree,
 * the existing extent is the nearest extent to map_start,
 * and an extent that you want to insert, deal with overlap and insert
 * the best fitted new extent into the tree.
 */
static int merge_extent_mapping(struct extent_map_tree *em_tree,
				struct extent_map *existing,
				struct extent_map *em,
				u64 map_start)
{
	struct extent_map *prev;
	struct extent_map *next;
	u64 start;
	u64 end;
	u64 start_diff;

	BUG_ON(map_start < em->start || map_start >= extent_map_end(em));

	if (existing->start > map_start) {
		next = existing;
		prev = prev_extent_map(next);
	} else {
		prev = existing;
		next = next_extent_map(prev);
	}

	start = prev ? extent_map_end(prev) : em->start;
	start = max_t(u64, start, em->start);
	end = next ? next->start : extent_map_end(em);
	end = min_t(u64, end, extent_map_end(em));
	start_diff = start - em->start;
	em->start = start;
	em->len = end - start;
	if (em->block_start < EXTENT_MAP_LAST_BYTE &&
	    !test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
		em->block_start += start_diff;
		em->block_len -= start_diff;
	}
	return add_extent_mapping(em_tree, em, 0);
}

static noinline int uncompress_inline(struct btrfs_path *path,
				      struct page *page,
				      size_t pg_offset, u64 extent_offset,
				      struct btrfs_file_extent_item *item)
{
	int ret;
	struct extent_buffer *leaf = path->nodes[0];
	char *tmp;
	size_t max_size;
	unsigned long inline_size;
	unsigned long ptr;
	int compress_type;

	WARN_ON(pg_offset != 0);
	compress_type = btrfs_file_extent_compression(leaf, item);
	max_size = btrfs_file_extent_ram_bytes(leaf, item);
	inline_size = btrfs_file_extent_inline_item_len(leaf,
					btrfs_item_nr(path->slots[0]));
	tmp = kmalloc(inline_size, GFP_NOFS);
	if (!tmp)
		return -ENOMEM;
	ptr = btrfs_file_extent_inline_start(item);

	read_extent_buffer(leaf, tmp, ptr, inline_size);

	max_size = min_t(unsigned long, PAGE_SIZE, max_size);
	ret = btrfs_decompress(compress_type, tmp, page,
			       extent_offset, inline_size, max_size);
	kfree(tmp);
	return ret;
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
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;
	int err = 0;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = btrfs_ino(inode);
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
	const bool new_inline = !page || create;

again:
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (em)
		em->bdev = fs_info->fs_devices->latest_bdev;
	read_unlock(&em_tree->lock);

	if (em) {
		if (em->start > start || em->start + em->len <= start)
			free_extent_map(em);
		else if (em->block_start == EXTENT_MAP_INLINE && page)
			free_extent_map(em);
		else
			goto out;
	}
	em = alloc_extent_map();
	if (!em) {
		err = -ENOMEM;
		goto out;
	}
	em->bdev = fs_info->fs_devices->latest_bdev;
	em->start = EXTENT_MAP_HOLE;
	em->orig_start = EXTENT_MAP_HOLE;
	em->len = (u64)-1;
	em->block_len = (u64)-1;

	if (!path) {
		path = btrfs_alloc_path();
		if (!path) {
			err = -ENOMEM;
			goto out;
		}
		/*
		 * Chances are we'll be called again, so go ahead and do
		 * readahead
		 */
		path->reada = READA_FORWARD;
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
	found_type = found_key.type;
	if (found_key.objectid != objectid ||
	    found_type != BTRFS_EXTENT_DATA_KEY) {
		/*
		 * If we backup past the first extent we want to move forward
		 * and see if there is an extent in front of us, otherwise we'll
		 * say there is a hole for our whole search range which can
		 * cause problems.
		 */
		extent_end = start;
		goto next;
	}

	found_type = btrfs_file_extent_type(leaf, item);
	extent_start = found_key.offset;
	if (found_type == BTRFS_FILE_EXTENT_REG ||
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		extent_end = extent_start +
		       btrfs_file_extent_num_bytes(leaf, item);
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		size_t size;
		size = btrfs_file_extent_inline_len(leaf, path->slots[0], item);
		extent_end = ALIGN(extent_start + size,
				   fs_info->sectorsize);
	}
next:
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
		if (start > found_key.offset)
			goto next;
		em->start = start;
		em->orig_start = start;
		em->len = found_key.offset - start;
		goto not_found_em;
	}

	btrfs_extent_item_to_extent_map(inode, path, item, new_inline, em);

	if (found_type == BTRFS_FILE_EXTENT_REG ||
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		goto insert;
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		unsigned long ptr;
		char *map;
		size_t size;
		size_t extent_offset;
		size_t copy_size;

		if (new_inline)
			goto out;

		size = btrfs_file_extent_inline_len(leaf, path->slots[0], item);
		extent_offset = page_offset(page) + pg_offset - extent_start;
		copy_size = min_t(u64, PAGE_SIZE - pg_offset,
				  size - extent_offset);
		em->start = extent_start + extent_offset;
		em->len = ALIGN(copy_size, fs_info->sectorsize);
		em->orig_block_len = em->len;
		em->orig_start = em->start;
		ptr = btrfs_file_extent_inline_start(item) + extent_offset;
		if (create == 0 && !PageUptodate(page)) {
			if (btrfs_file_extent_compression(leaf, item) !=
			    BTRFS_COMPRESS_NONE) {
				ret = uncompress_inline(path, page, pg_offset,
							extent_offset, item);
				if (ret) {
					err = ret;
					goto out;
				}
			} else {
				map = kmap(page);
				read_extent_buffer(leaf, map + pg_offset, ptr,
						   copy_size);
				if (pg_offset + copy_size < PAGE_SIZE) {
					memset(map + pg_offset + copy_size, 0,
					       PAGE_SIZE - pg_offset -
					       copy_size);
				}
				kunmap(page);
			}
			flush_dcache_page(page);
		} else if (create && PageUptodate(page)) {
			BUG();
			if (!trans) {
				kunmap(page);
				free_extent_map(em);
				em = NULL;

				btrfs_release_path(path);
				trans = btrfs_join_transaction(root);

				if (IS_ERR(trans))
					return ERR_CAST(trans);
				goto again;
			}
			map = kmap(page);
			write_extent_buffer(leaf, map + pg_offset, ptr,
					    copy_size);
			kunmap(page);
			btrfs_mark_buffer_dirty(leaf);
		}
		set_extent_uptodate(io_tree, em->start,
				    extent_map_end(em) - 1, NULL, GFP_NOFS);
		goto insert;
	}
not_found:
	em->start = start;
	em->orig_start = start;
	em->len = len;
not_found_em:
	em->block_start = EXTENT_MAP_HOLE;
	set_bit(EXTENT_FLAG_VACANCY, &em->flags);
insert:
	btrfs_release_path(path);
	if (em->start > start || extent_map_end(em) <= start) {
		btrfs_err(fs_info,
			  "bad extent! em: [%llu %llu] passed [%llu %llu]",
			  em->start, em->len, start, len);
		err = -EIO;
		goto out;
	}

	err = 0;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	/* it is possible that someone inserted the extent into the tree
	 * while we had the lock dropped.  It is also possible that
	 * an overlapping map exists in the tree
	 */
	if (ret == -EEXIST) {
		struct extent_map *existing;

		ret = 0;

		existing = search_extent_mapping(em_tree, start, len);
		/*
		 * existing will always be non-NULL, since there must be
		 * extent causing the -EEXIST.
		 */
		if (existing->start == em->start &&
		    extent_map_end(existing) >= extent_map_end(em) &&
		    em->block_start == existing->block_start) {
			/*
			 * The existing extent map already encompasses the
			 * entire extent map we tried to add.
			 */
			free_extent_map(em);
			em = existing;
			err = 0;

		} else if (start >= extent_map_end(existing) ||
		    start <= existing->start) {
			/*
			 * The existing extent map is the one nearest to
			 * the [start, start + len) range which overlaps
			 */
			err = merge_extent_mapping(em_tree, existing,
						   em, start);
			free_extent_map(existing);
			if (err) {
				free_extent_map(em);
				em = NULL;
			}
		} else {
			free_extent_map(em);
			em = existing;
			err = 0;
		}
	}
	write_unlock(&em_tree->lock);
out:

	trace_btrfs_get_extent(root, inode, em);

	btrfs_free_path(path);
	if (trans) {
		ret = btrfs_end_transaction(trans);
		if (!err)
			err = ret;
	}
	if (err) {
		free_extent_map(em);
		return ERR_PTR(err);
	}
	BUG_ON(!em); /* Error is always set */
	return em;
}

struct extent_map *btrfs_get_extent_fiemap(struct inode *inode, struct page *page,
					   size_t pg_offset, u64 start, u64 len,
					   int create)
{
	struct extent_map *em;
	struct extent_map *hole_em = NULL;
	u64 range_start = start;
	u64 end;
	u64 found;
	u64 found_end;
	int err = 0;

	em = btrfs_get_extent(inode, page, pg_offset, start, len, create);
	if (IS_ERR(em))
		return em;
	if (em) {
		/*
		 * if our em maps to
		 * -  a hole or
		 * -  a pre-alloc extent,
		 * there might actually be delalloc bytes behind it.
		 */
		if (em->block_start != EXTENT_MAP_HOLE &&
		    !test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			return em;
		else
			hole_em = em;
	}

	/* check to see if we've wrapped (len == -1 or similar) */
	end = start + len;
	if (end < start)
		end = (u64)-1;
	else
		end -= 1;

	em = NULL;

	/* ok, we didn't find anything, lets look for delalloc */
	found = count_range_bits(&BTRFS_I(inode)->io_tree, &range_start,
				 end, len, EXTENT_DELALLOC, 1);
	found_end = range_start + found;
	if (found_end < range_start)
		found_end = (u64)-1;

	/*
	 * we didn't find anything useful, return
	 * the original results from get_extent()
	 */
	if (range_start > end || found_end <= start) {
		em = hole_em;
		hole_em = NULL;
		goto out;
	}

	/* adjust the range_start to make sure it doesn't
	 * go backwards from the start they passed in
	 */
	range_start = max(start, range_start);
	found = found_end - range_start;

	if (found > 0) {
		u64 hole_start = start;
		u64 hole_len = len;

		em = alloc_extent_map();
		if (!em) {
			err = -ENOMEM;
			goto out;
		}
		/*
		 * when btrfs_get_extent can't find anything it
		 * returns one huge hole
		 *
		 * make sure what it found really fits our range, and
		 * adjust to make sure it is based on the start from
		 * the caller
		 */
		if (hole_em) {
			u64 calc_end = extent_map_end(hole_em);

			if (calc_end <= start || (hole_em->start > end)) {
				free_extent_map(hole_em);
				hole_em = NULL;
			} else {
				hole_start = max(hole_em->start, start);
				hole_len = calc_end - hole_start;
			}
		}
		em->bdev = NULL;
		if (hole_em && range_start > hole_start) {
			/* our hole starts before our delalloc, so we
			 * have to return just the parts of the hole
			 * that go until  the delalloc starts
			 */
			em->len = min(hole_len,
				      range_start - hole_start);
			em->start = hole_start;
			em->orig_start = hole_start;
			/*
			 * don't adjust block start at all,
			 * it is fixed at EXTENT_MAP_HOLE
			 */
			em->block_start = hole_em->block_start;
			em->block_len = hole_len;
			if (test_bit(EXTENT_FLAG_PREALLOC, &hole_em->flags))
				set_bit(EXTENT_FLAG_PREALLOC, &em->flags);
		} else {
			em->start = range_start;
			em->len = found;
			em->orig_start = range_start;
			em->block_start = EXTENT_MAP_DELALLOC;
			em->block_len = found;
		}
	} else if (hole_em) {
		return hole_em;
	}
out:

	free_extent_map(hole_em);
	if (err) {
		free_extent_map(em);
		return ERR_PTR(err);
	}
	return em;
}

static struct extent_map *btrfs_create_dio_extent(struct inode *inode,
						  const u64 start,
						  const u64 len,
						  const u64 orig_start,
						  const u64 block_start,
						  const u64 block_len,
						  const u64 orig_block_len,
						  const u64 ram_bytes,
						  const int type)
{
	struct extent_map *em = NULL;
	int ret;

	down_read(&BTRFS_I(inode)->dio_sem);
	if (type != BTRFS_ORDERED_NOCOW) {
		em = create_pinned_em(inode, start, len, orig_start,
				      block_start, block_len, orig_block_len,
				      ram_bytes, type);
		if (IS_ERR(em))
			goto out;
	}
	ret = btrfs_add_ordered_extent_dio(inode, start, block_start,
					   len, block_len, type);
	if (ret) {
		if (em) {
			free_extent_map(em);
			btrfs_drop_extent_cache(inode, start,
						start + len - 1, 0);
		}
		em = ERR_PTR(ret);
	}
 out:
	up_read(&BTRFS_I(inode)->dio_sem);

	return em;
}

static struct extent_map *btrfs_new_extent_direct(struct inode *inode,
						  u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_map *em;
	struct btrfs_key ins;
	u64 alloc_hint;
	int ret;

	alloc_hint = get_extent_allocation_hint(inode, start, len);
	ret = btrfs_reserve_extent(root, len, len, fs_info->sectorsize,
				   0, alloc_hint, &ins, 1, 1);
	if (ret)
		return ERR_PTR(ret);

	em = btrfs_create_dio_extent(inode, start, ins.offset, start,
				     ins.objectid, ins.offset, ins.offset,
				     ins.offset, 0);
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	if (IS_ERR(em))
		btrfs_free_reserved_extent(fs_info, ins.objectid,
					   ins.offset, 1);

	return em;
}

/*
 * returns 1 when the nocow is safe, < 1 on error, 0 if the
 * block must be cow'd
 */
noinline int can_nocow_extent(struct inode *inode, u64 offset, u64 *len,
			      u64 *orig_start, u64 *orig_block_len,
			      u64 *ram_bytes)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	int ret;
	struct extent_buffer *leaf;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 disk_bytenr;
	u64 backref_offset;
	u64 extent_end;
	u64 num_bytes;
	int slot;
	int found_type;
	bool nocow = (BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_lookup_file_extent(NULL, root, path, btrfs_ino(inode),
				       offset, 0);
	if (ret < 0)
		goto out;

	slot = path->slots[0];
	if (ret == 1) {
		if (slot == 0) {
			/* can't find the item, must cow */
			ret = 0;
			goto out;
		}
		slot--;
	}
	ret = 0;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, slot);
	if (key.objectid != btrfs_ino(inode) ||
	    key.type != BTRFS_EXTENT_DATA_KEY) {
		/* not our file or wrong item type, must cow */
		goto out;
	}

	if (key.offset > offset) {
		/* Wrong offset, must cow */
		goto out;
	}

	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
	found_type = btrfs_file_extent_type(leaf, fi);
	if (found_type != BTRFS_FILE_EXTENT_REG &&
	    found_type != BTRFS_FILE_EXTENT_PREALLOC) {
		/* not a regular extent, must cow */
		goto out;
	}

	if (!nocow && found_type == BTRFS_FILE_EXTENT_REG)
		goto out;

	extent_end = key.offset + btrfs_file_extent_num_bytes(leaf, fi);
	if (extent_end <= offset)
		goto out;

	disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
	if (disk_bytenr == 0)
		goto out;

	if (btrfs_file_extent_compression(leaf, fi) ||
	    btrfs_file_extent_encryption(leaf, fi) ||
	    btrfs_file_extent_other_encoding(leaf, fi))
		goto out;

	backref_offset = btrfs_file_extent_offset(leaf, fi);

	if (orig_start) {
		*orig_start = key.offset - backref_offset;
		*orig_block_len = btrfs_file_extent_disk_num_bytes(leaf, fi);
		*ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
	}

	if (btrfs_extent_readonly(fs_info, disk_bytenr))
		goto out;

	num_bytes = min(offset + *len, extent_end) - offset;
	if (!nocow && found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		u64 range_end;

		range_end = round_up(offset + num_bytes,
				     root->fs_info->sectorsize) - 1;
		ret = test_range_bit(io_tree, offset, range_end,
				     EXTENT_DELALLOC, 0, NULL);
		if (ret) {
			ret = -EAGAIN;
			goto out;
		}
	}

	btrfs_release_path(path);

	/*
	 * look for other files referencing this extent, if we
	 * find any we must cow
	 */
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = 0;
		goto out;
	}

	ret = btrfs_cross_ref_exist(trans, root, btrfs_ino(inode),
				    key.offset - backref_offset, disk_bytenr);
	btrfs_end_transaction(trans);
	if (ret) {
		ret = 0;
		goto out;
	}

	/*
	 * adjust disk_bytenr and num_bytes to cover just the bytes
	 * in this extent we are about to write.  If there
	 * are any csums in that range we have to cow in order
	 * to keep the csums correct
	 */
	disk_bytenr += backref_offset;
	disk_bytenr += offset - key.offset;
	if (csum_exist_in_range(fs_info, disk_bytenr, num_bytes))
		goto out;
	/*
	 * all of the above have passed, it is safe to overwrite this extent
	 * without cow
	 */
	*len = num_bytes;
	ret = 1;
out:
	btrfs_free_path(path);
	return ret;
}

bool btrfs_page_exists_in_range(struct inode *inode, loff_t start, loff_t end)
{
	struct radix_tree_root *root = &inode->i_mapping->page_tree;
	int found = false;
	void **pagep = NULL;
	struct page *page = NULL;
	int start_idx;
	int end_idx;

	start_idx = start >> PAGE_SHIFT;

	/*
	 * end is the last byte in the last page.  end == start is legal
	 */
	end_idx = end >> PAGE_SHIFT;

	rcu_read_lock();

	/* Most of the code in this while loop is lifted from
	 * find_get_page.  It's been modified to begin searching from a
	 * page and return just the first page found in that range.  If the
	 * found idx is less than or equal to the end idx then we know that
	 * a page exists.  If no pages are found or if those pages are
	 * outside of the range then we're fine (yay!) */
	while (page == NULL &&
	       radix_tree_gang_lookup_slot(root, &pagep, NULL, start_idx, 1)) {
		page = radix_tree_deref_slot(pagep);
		if (unlikely(!page))
			break;

		if (radix_tree_exception(page)) {
			if (radix_tree_deref_retry(page)) {
				page = NULL;
				continue;
			}
			/*
			 * Otherwise, shmem/tmpfs must be storing a swap entry
			 * here as an exceptional entry: so return it without
			 * attempting to raise page count.
			 */
			page = NULL;
			break; /* TODO: Is this relevant for this use case? */
		}

		if (!page_cache_get_speculative(page)) {
			page = NULL;
			continue;
		}

		/*
		 * Has the page moved?
		 * This is part of the lockless pagecache protocol. See
		 * include/linux/pagemap.h for details.
		 */
		if (unlikely(page != *pagep)) {
			put_page(page);
			page = NULL;
		}
	}

	if (page) {
		if (page->index <= end_idx)
			found = true;
		put_page(page);
	}

	rcu_read_unlock();
	return found;
}

static int lock_extent_direct(struct inode *inode, u64 lockstart, u64 lockend,
			      struct extent_state **cached_state, int writing)
{
	struct btrfs_ordered_extent *ordered;
	int ret = 0;

	while (1) {
		lock_extent_bits(&BTRFS_I(inode)->io_tree, lockstart, lockend,
				 cached_state);
		/*
		 * We're concerned with the entire range that we're going to be
		 * doing DIO to, so we need to make sure there's no ordered
		 * extents in this range.
		 */
		ordered = btrfs_lookup_ordered_range(inode, lockstart,
						     lockend - lockstart + 1);

		/*
		 * We need to make sure there are no buffered pages in this
		 * range either, we could have raced between the invalidate in
		 * generic_file_direct_write and locking the extent.  The
		 * invalidate needs to happen so that reads after a write do not
		 * get stale data.
		 */
		if (!ordered &&
		    (!writing ||
		     !btrfs_page_exists_in_range(inode, lockstart, lockend)))
			break;

		unlock_extent_cached(&BTRFS_I(inode)->io_tree, lockstart, lockend,
				     cached_state, GFP_NOFS);

		if (ordered) {
			/*
			 * If we are doing a DIO read and the ordered extent we
			 * found is for a buffered write, we can not wait for it
			 * to complete and retry, because if we do so we can
			 * deadlock with concurrent buffered writes on page
			 * locks. This happens only if our DIO read covers more
			 * than one extent map, if at this point has already
			 * created an ordered extent for a previous extent map
			 * and locked its range in the inode's io tree, and a
			 * concurrent write against that previous extent map's
			 * range and this range started (we unlock the ranges
			 * in the io tree only when the bios complete and
			 * buffered writes always lock pages before attempting
			 * to lock range in the io tree).
			 */
			if (writing ||
			    test_bit(BTRFS_ORDERED_DIRECT, &ordered->flags))
				btrfs_start_ordered_extent(inode, ordered, 1);
			else
				ret = -ENOTBLK;
			btrfs_put_ordered_extent(ordered);
		} else {
			/*
			 * We could trigger writeback for this range (and wait
			 * for it to complete) and then invalidate the pages for
			 * this range (through invalidate_inode_pages2_range()),
			 * but that can lead us to a deadlock with a concurrent
			 * call to readpages() (a buffered read or a defrag call
			 * triggered a readahead) on a page lock due to an
			 * ordered dio extent we created before but did not have
			 * yet a corresponding bio submitted (whence it can not
			 * complete), which makes readpages() wait for that
			 * ordered extent to complete while holding a lock on
			 * that page.
			 */
			ret = -ENOTBLK;
		}

		if (ret)
			break;

		cond_resched();
	}

	return ret;
}

static struct extent_map *create_pinned_em(struct inode *inode, u64 start,
					   u64 len, u64 orig_start,
					   u64 block_start, u64 block_len,
					   u64 orig_block_len, u64 ram_bytes,
					   int type)
{
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	em_tree = &BTRFS_I(inode)->extent_tree;
	em = alloc_extent_map();
	if (!em)
		return ERR_PTR(-ENOMEM);

	em->start = start;
	em->orig_start = orig_start;
	em->mod_start = start;
	em->mod_len = len;
	em->len = len;
	em->block_len = block_len;
	em->block_start = block_start;
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	em->orig_block_len = orig_block_len;
	em->ram_bytes = ram_bytes;
	em->generation = -1;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);
	if (type == BTRFS_ORDERED_PREALLOC)
		set_bit(EXTENT_FLAG_FILLING, &em->flags);

	do {
		btrfs_drop_extent_cache(inode, em->start,
				em->start + em->len - 1, 0);
		write_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em, 1);
		write_unlock(&em_tree->lock);
	} while (ret == -EEXIST);

	if (ret) {
		free_extent_map(em);
		return ERR_PTR(ret);
	}

	return em;
}

static void adjust_dio_outstanding_extents(struct inode *inode,
					   struct btrfs_dio_data *dio_data,
					   const u64 len)
{
	unsigned num_extents;

	num_extents = (unsigned) div64_u64(len + BTRFS_MAX_EXTENT_SIZE - 1,
					   BTRFS_MAX_EXTENT_SIZE);
	/*
	 * If we have an outstanding_extents count still set then we're
	 * within our reservation, otherwise we need to adjust our inode
	 * counter appropriately.
	 */
	if (dio_data->outstanding_extents >= num_extents) {
		dio_data->outstanding_extents -= num_extents;
	} else {
		/*
		 * If dio write length has been split due to no large enough
		 * contiguous space, we need to compensate our inode counter
		 * appropriately.
		 */
		u64 num_needed = num_extents - dio_data->outstanding_extents;

		spin_lock(&BTRFS_I(inode)->lock);
		BTRFS_I(inode)->outstanding_extents += num_needed;
		spin_unlock(&BTRFS_I(inode)->lock);
	}
}

static int btrfs_get_blocks_direct(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_map *em;
	struct extent_state *cached_state = NULL;
	struct btrfs_dio_data *dio_data = NULL;
	u64 start = iblock << inode->i_blkbits;
	u64 lockstart, lockend;
	u64 len = bh_result->b_size;
	int unlock_bits = EXTENT_LOCKED;
	int ret = 0;

	if (create)
		unlock_bits |= EXTENT_DIRTY;
	else
		len = min_t(u64, len, fs_info->sectorsize);

	lockstart = start;
	lockend = start + len - 1;

	if (current->journal_info) {
		/*
		 * Need to pull our outstanding extents and set journal_info to NULL so
		 * that anything that needs to check if there's a transaction doesn't get
		 * confused.
		 */
		dio_data = current->journal_info;
		current->journal_info = NULL;
	}

	/*
	 * If this errors out it's because we couldn't invalidate pagecache for
	 * this range and we need to fallback to buffered.
	 */
	if (lock_extent_direct(inode, lockstart, lockend, &cached_state,
			       create)) {
		ret = -ENOTBLK;
		goto err;
	}

	em = btrfs_get_extent(inode, NULL, 0, start, len, 0);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto unlock_err;
	}

	/*
	 * Ok for INLINE and COMPRESSED extents we need to fallback on buffered
	 * io.  INLINE is special, and we could probably kludge it in here, but
	 * it's still buffered so for safety lets just fall back to the generic
	 * buffered path.
	 *
	 * For COMPRESSED we _have_ to read the entire extent in so we can
	 * decompress it, so there will be buffering required no matter what we
	 * do, so go ahead and fallback to buffered.
	 *
	 * We return -ENOTBLK because that's what makes DIO go ahead and go back
	 * to buffered IO.  Don't blame me, this is the price we pay for using
	 * the generic code.
	 */
	if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags) ||
	    em->block_start == EXTENT_MAP_INLINE) {
		free_extent_map(em);
		ret = -ENOTBLK;
		goto unlock_err;
	}

	/* Just a good old fashioned hole, return */
	if (!create && (em->block_start == EXTENT_MAP_HOLE ||
			test_bit(EXTENT_FLAG_PREALLOC, &em->flags))) {
		free_extent_map(em);
		goto unlock_err;
	}

	/*
	 * We don't allocate a new extent in the following cases
	 *
	 * 1) The inode is marked as NODATACOW.  In this case we'll just use the
	 * existing extent.
	 * 2) The extent is marked as PREALLOC.  We're good to go here and can
	 * just use the extent.
	 *
	 */
	if (!create) {
		len = min(len, em->len - (start - em->start));
		lockstart = start + len;
		goto unlock;
	}

	if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags) ||
	    ((BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW) &&
	     em->block_start != EXTENT_MAP_HOLE)) {
		int type;
		u64 block_start, orig_start, orig_block_len, ram_bytes;

		if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			type = BTRFS_ORDERED_PREALLOC;
		else
			type = BTRFS_ORDERED_NOCOW;
		len = min(len, em->len - (start - em->start));
		block_start = em->block_start + (start - em->start);

		if (can_nocow_extent(inode, start, &len, &orig_start,
				     &orig_block_len, &ram_bytes) == 1 &&
		    btrfs_inc_nocow_writers(fs_info, block_start)) {
			struct extent_map *em2;

			em2 = btrfs_create_dio_extent(inode, start, len,
						      orig_start, block_start,
						      len, orig_block_len,
						      ram_bytes, type);
			btrfs_dec_nocow_writers(fs_info, block_start);
			if (type == BTRFS_ORDERED_PREALLOC) {
				free_extent_map(em);
				em = em2;
			}
			if (em2 && IS_ERR(em2)) {
				ret = PTR_ERR(em2);
				goto unlock_err;
			}
			/*
			 * For inode marked NODATACOW or extent marked PREALLOC,
			 * use the existing or preallocated extent, so does not
			 * need to adjust btrfs_space_info's bytes_may_use.
			 */
			btrfs_free_reserved_data_space_noquota(inode,
					start, len);
			goto unlock;
		}
	}

	/*
	 * this will cow the extent, reset the len in case we changed
	 * it above
	 */
	len = bh_result->b_size;
	free_extent_map(em);
	em = btrfs_new_extent_direct(inode, start, len);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto unlock_err;
	}
	len = min(len, em->len - (start - em->start));
unlock:
	bh_result->b_blocknr = (em->block_start + (start - em->start)) >>
		inode->i_blkbits;
	bh_result->b_size = len;
	bh_result->b_bdev = em->bdev;
	set_buffer_mapped(bh_result);
	if (create) {
		if (!test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			set_buffer_new(bh_result);

		/*
		 * Need to update the i_size under the extent lock so buffered
		 * readers will get the updated i_size when we unlock.
		 */
		if (start + len > i_size_read(inode))
			i_size_write(inode, start + len);

		adjust_dio_outstanding_extents(inode, dio_data, len);
		WARN_ON(dio_data->reserve < len);
		dio_data->reserve -= len;
		dio_data->unsubmitted_oe_range_end = start + len;
		current->journal_info = dio_data;
	}

	/*
	 * In the case of write we need to clear and unlock the entire range,
	 * in the case of read we need to unlock only the end area that we
	 * aren't using if there is any left over space.
	 */
	if (lockstart < lockend) {
		clear_extent_bit(&BTRFS_I(inode)->io_tree, lockstart,
				 lockend, unlock_bits, 1, 0,
				 &cached_state, GFP_NOFS);
	} else {
		free_extent_state(cached_state);
	}

	free_extent_map(em);

	return 0;

unlock_err:
	clear_extent_bit(&BTRFS_I(inode)->io_tree, lockstart, lockend,
			 unlock_bits, 1, 0, &cached_state, GFP_NOFS);
err:
	if (dio_data)
		current->journal_info = dio_data;
	/*
	 * Compensate the delalloc release we do in btrfs_direct_IO() when we
	 * write less data then expected, so that we don't underflow our inode's
	 * outstanding extents counter.
	 */
	if (create && dio_data)
		adjust_dio_outstanding_extents(inode, dio_data, len);

	return ret;
}

static inline int submit_dio_repair_bio(struct inode *inode, struct bio *bio,
					int mirror_num)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;

	BUG_ON(bio_op(bio) == REQ_OP_WRITE);

	bio_get(bio);

	ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DIO_REPAIR);
	if (ret)
		goto err;

	ret = btrfs_map_bio(fs_info, bio, mirror_num, 0);
err:
	bio_put(bio);
	return ret;
}

static int btrfs_check_dio_repairable(struct inode *inode,
				      struct bio *failed_bio,
				      struct io_failure_record *failrec,
				      int failed_mirror)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int num_copies;

	num_copies = btrfs_num_copies(fs_info, failrec->logical, failrec->len);
	if (num_copies == 1) {
		/*
		 * we only have a single copy of the data, so don't bother with
		 * all the retry and error correction code that follows. no
		 * matter what the error is, it is very likely to persist.
		 */
		btrfs_debug(fs_info,
			"Check DIO Repairable: cannot repair, num_copies=%d, next_mirror %d, failed_mirror %d",
			num_copies, failrec->this_mirror, failed_mirror);
		return 0;
	}

	failrec->failed_mirror = failed_mirror;
	failrec->this_mirror++;
	if (failrec->this_mirror == failed_mirror)
		failrec->this_mirror++;

	if (failrec->this_mirror > num_copies) {
		btrfs_debug(fs_info,
			"Check DIO Repairable: (fail) num_copies=%d, next_mirror %d, failed_mirror %d",
			num_copies, failrec->this_mirror, failed_mirror);
		return 0;
	}

	return 1;
}

static int dio_read_error(struct inode *inode, struct bio *failed_bio,
			struct page *page, unsigned int pgoff,
			u64 start, u64 end, int failed_mirror,
			bio_end_io_t *repair_endio, void *repair_arg)
{
	struct io_failure_record *failrec;
	struct bio *bio;
	int isector;
	int read_mode = 0;
	int ret;

	BUG_ON(bio_op(failed_bio) == REQ_OP_WRITE);

	ret = btrfs_get_io_failure_record(inode, start, end, &failrec);
	if (ret)
		return ret;

	ret = btrfs_check_dio_repairable(inode, failed_bio, failrec,
					 failed_mirror);
	if (!ret) {
		free_io_failure(inode, failrec);
		return -EIO;
	}

	if ((failed_bio->bi_vcnt > 1)
		|| (failed_bio->bi_io_vec->bv_len
			> btrfs_inode_sectorsize(inode)))
		read_mode |= REQ_FAILFAST_DEV;

	isector = start - btrfs_io_bio(failed_bio)->logical;
	isector >>= inode->i_sb->s_blocksize_bits;
	bio = btrfs_create_repair_bio(inode, failed_bio, failrec, page,
				pgoff, isector, repair_endio, repair_arg);
	if (!bio) {
		free_io_failure(inode, failrec);
		return -EIO;
	}
	bio_set_op_attrs(bio, REQ_OP_READ, read_mode);

	btrfs_debug(BTRFS_I(inode)->root->fs_info,
		    "Repair DIO Read Error: submitting new dio read[%#x] to this_mirror=%d, in_validation=%d\n",
		    read_mode, failrec->this_mirror, failrec->in_validation);

	ret = submit_dio_repair_bio(inode, bio, failrec->this_mirror);
	if (ret) {
		free_io_failure(inode, failrec);
		bio_put(bio);
	}

	return ret;
}

struct btrfs_retry_complete {
	struct completion done;
	struct inode *inode;
	u64 start;
	int uptodate;
};

static void btrfs_retry_endio_nocsum(struct bio *bio)
{
	struct btrfs_retry_complete *done = bio->bi_private;
	struct inode *inode;
	struct bio_vec *bvec;
	int i;

	if (bio->bi_error)
		goto end;

	ASSERT(bio->bi_vcnt == 1);
	inode = bio->bi_io_vec->bv_page->mapping->host;
	ASSERT(bio->bi_io_vec->bv_len == btrfs_inode_sectorsize(inode));

	done->uptodate = 1;
	bio_for_each_segment_all(bvec, bio, i)
		clean_io_failure(done->inode, done->start, bvec->bv_page, 0);
end:
	complete(&done->done);
	bio_put(bio);
}

static int __btrfs_correct_data_nocsum(struct inode *inode,
				       struct btrfs_io_bio *io_bio)
{
	struct btrfs_fs_info *fs_info;
	struct bio_vec *bvec;
	struct btrfs_retry_complete done;
	u64 start;
	unsigned int pgoff;
	u32 sectorsize;
	int nr_sectors;
	int i;
	int ret;

	fs_info = BTRFS_I(inode)->root->fs_info;
	sectorsize = fs_info->sectorsize;

	start = io_bio->logical;
	done.inode = inode;

	bio_for_each_segment_all(bvec, &io_bio->bio, i) {
		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info, bvec->bv_len);
		pgoff = bvec->bv_offset;

next_block_or_try_again:
		done.uptodate = 0;
		done.start = start;
		init_completion(&done.done);

		ret = dio_read_error(inode, &io_bio->bio, bvec->bv_page,
				pgoff, start, start + sectorsize - 1,
				io_bio->mirror_num,
				btrfs_retry_endio_nocsum, &done);
		if (ret)
			return ret;

		wait_for_completion(&done.done);

		if (!done.uptodate) {
			/* We might have another mirror, so try again */
			goto next_block_or_try_again;
		}

		start += sectorsize;

		if (nr_sectors--) {
			pgoff += sectorsize;
			goto next_block_or_try_again;
		}
	}

	return 0;
}

static void btrfs_retry_endio(struct bio *bio)
{
	struct btrfs_retry_complete *done = bio->bi_private;
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	struct inode *inode;
	struct bio_vec *bvec;
	u64 start;
	int uptodate;
	int ret;
	int i;

	if (bio->bi_error)
		goto end;

	uptodate = 1;

	start = done->start;

	ASSERT(bio->bi_vcnt == 1);
	inode = bio->bi_io_vec->bv_page->mapping->host;
	ASSERT(bio->bi_io_vec->bv_len == btrfs_inode_sectorsize(inode));

	bio_for_each_segment_all(bvec, bio, i) {
		ret = __readpage_endio_check(done->inode, io_bio, i,
					bvec->bv_page, bvec->bv_offset,
					done->start, bvec->bv_len);
		if (!ret)
			clean_io_failure(done->inode, done->start,
					bvec->bv_page, bvec->bv_offset);
		else
			uptodate = 0;
	}

	done->uptodate = uptodate;
end:
	complete(&done->done);
	bio_put(bio);
}

static int __btrfs_subio_endio_read(struct inode *inode,
				    struct btrfs_io_bio *io_bio, int err)
{
	struct btrfs_fs_info *fs_info;
	struct bio_vec *bvec;
	struct btrfs_retry_complete done;
	u64 start;
	u64 offset = 0;
	u32 sectorsize;
	int nr_sectors;
	unsigned int pgoff;
	int csum_pos;
	int i;
	int ret;

	fs_info = BTRFS_I(inode)->root->fs_info;
	sectorsize = fs_info->sectorsize;

	err = 0;
	start = io_bio->logical;
	done.inode = inode;

	bio_for_each_segment_all(bvec, &io_bio->bio, i) {
		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info, bvec->bv_len);

		pgoff = bvec->bv_offset;
next_block:
		csum_pos = BTRFS_BYTES_TO_BLKS(fs_info, offset);
		ret = __readpage_endio_check(inode, io_bio, csum_pos,
					bvec->bv_page, pgoff, start,
					sectorsize);
		if (likely(!ret))
			goto next;
try_again:
		done.uptodate = 0;
		done.start = start;
		init_completion(&done.done);

		ret = dio_read_error(inode, &io_bio->bio, bvec->bv_page,
				pgoff, start, start + sectorsize - 1,
				io_bio->mirror_num,
				btrfs_retry_endio, &done);
		if (ret) {
			err = ret;
			goto next;
		}

		wait_for_completion(&done.done);

		if (!done.uptodate) {
			/* We might have another mirror, so try again */
			goto try_again;
		}
next:
		offset += sectorsize;
		start += sectorsize;

		ASSERT(nr_sectors);

		if (--nr_sectors) {
			pgoff += sectorsize;
			goto next_block;
		}
	}

	return err;
}

static int btrfs_subio_endio_read(struct inode *inode,
				  struct btrfs_io_bio *io_bio, int err)
{
	bool skip_csum = BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM;

	if (skip_csum) {
		if (unlikely(err))
			return __btrfs_correct_data_nocsum(inode, io_bio);
		else
			return 0;
	} else {
		return __btrfs_subio_endio_read(inode, io_bio, err);
	}
}

static void btrfs_endio_direct_read(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	struct inode *inode = dip->inode;
	struct bio *dio_bio;
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	int err = bio->bi_error;

	if (dip->flags & BTRFS_DIO_ORIG_BIO_SUBMITTED)
		err = btrfs_subio_endio_read(inode, io_bio, err);

	unlock_extent(&BTRFS_I(inode)->io_tree, dip->logical_offset,
		      dip->logical_offset + dip->bytes - 1);
	dio_bio = dip->dio_bio;

	kfree(dip);

	dio_bio->bi_error = bio->bi_error;
	dio_end_io(dio_bio, bio->bi_error);

	if (io_bio->end_io)
		io_bio->end_io(io_bio, err);
	bio_put(bio);
}

static void btrfs_endio_direct_write_update_ordered(struct inode *inode,
						    const u64 offset,
						    const u64 bytes,
						    const int uptodate)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ordered_extent *ordered = NULL;
	u64 ordered_offset = offset;
	u64 ordered_bytes = bytes;
	int ret;

again:
	ret = btrfs_dec_test_first_ordered_pending(inode, &ordered,
						   &ordered_offset,
						   ordered_bytes,
						   uptodate);
	if (!ret)
		goto out_test;

	btrfs_init_work(&ordered->work, btrfs_endio_write_helper,
			finish_ordered_fn, NULL, NULL);
	btrfs_queue_work(fs_info->endio_write_workers, &ordered->work);
out_test:
	/*
	 * our bio might span multiple ordered extents.  If we haven't
	 * completed the accounting for the whole dio, go back and try again
	 */
	if (ordered_offset < offset + bytes) {
		ordered_bytes = offset + bytes - ordered_offset;
		ordered = NULL;
		goto again;
	}
}

static void btrfs_endio_direct_write(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	struct bio *dio_bio = dip->dio_bio;

	btrfs_endio_direct_write_update_ordered(dip->inode,
						dip->logical_offset,
						dip->bytes,
						!bio->bi_error);

	kfree(dip);

	dio_bio->bi_error = bio->bi_error;
	dio_end_io(dio_bio, bio->bi_error);
	bio_put(bio);
}

static int __btrfs_submit_bio_start_direct_io(struct inode *inode,
				    struct bio *bio, int mirror_num,
				    unsigned long bio_flags, u64 offset)
{
	int ret;
	ret = btrfs_csum_one_bio(inode, bio, offset, 1);
	BUG_ON(ret); /* -ENOMEM */
	return 0;
}

static void btrfs_end_dio_bio(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	int err = bio->bi_error;

	if (err)
		btrfs_warn(BTRFS_I(dip->inode)->root->fs_info,
			   "direct IO failed ino %llu rw %d,%u sector %#Lx len %u err no %d",
			   btrfs_ino(dip->inode), bio_op(bio), bio->bi_opf,
			   (unsigned long long)bio->bi_iter.bi_sector,
			   bio->bi_iter.bi_size, err);

	if (dip->subio_endio)
		err = dip->subio_endio(dip->inode, btrfs_io_bio(bio), err);

	if (err) {
		dip->errors = 1;

		/*
		 * before atomic variable goto zero, we must make sure
		 * dip->errors is perceived to be set.
		 */
		smp_mb__before_atomic();
	}

	/* if there are more bios still pending for this dio, just exit */
	if (!atomic_dec_and_test(&dip->pending_bios))
		goto out;

	if (dip->errors) {
		bio_io_error(dip->orig_bio);
	} else {
		dip->dio_bio->bi_error = 0;
		bio_endio(dip->orig_bio);
	}
out:
	bio_put(bio);
}

static struct bio *btrfs_dio_bio_alloc(struct block_device *bdev,
				       u64 first_sector, gfp_t gfp_flags)
{
	struct bio *bio;
	bio = btrfs_bio_alloc(bdev, first_sector, BIO_MAX_PAGES, gfp_flags);
	if (bio)
		bio_associate_current(bio);
	return bio;
}

static inline int btrfs_lookup_and_bind_dio_csum(struct inode *inode,
						 struct btrfs_dio_private *dip,
						 struct bio *bio,
						 u64 file_offset)
{
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	struct btrfs_io_bio *orig_io_bio = btrfs_io_bio(dip->orig_bio);
	int ret;

	/*
	 * We load all the csum data we need when we submit
	 * the first bio to reduce the csum tree search and
	 * contention.
	 */
	if (dip->logical_offset == file_offset) {
		ret = btrfs_lookup_bio_sums_dio(inode, dip->orig_bio,
						file_offset);
		if (ret)
			return ret;
	}

	if (bio == dip->orig_bio)
		return 0;

	file_offset -= dip->logical_offset;
	file_offset >>= inode->i_sb->s_blocksize_bits;
	io_bio->csum = (u8 *)(((u32 *)orig_io_bio->csum) + file_offset);

	return 0;
}

static inline int __btrfs_submit_dio_bio(struct bio *bio, struct inode *inode,
					 u64 file_offset, int skip_sum,
					 int async_submit)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_dio_private *dip = bio->bi_private;
	bool write = bio_op(bio) == REQ_OP_WRITE;
	int ret;

	if (async_submit)
		async_submit = !atomic_read(&BTRFS_I(inode)->sync_writers);

	bio_get(bio);

	if (!write) {
		ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DATA);
		if (ret)
			goto err;
	}

	if (skip_sum)
		goto map;

	if (write && async_submit) {
		ret = btrfs_wq_submit_bio(fs_info, inode, bio, 0, 0,
					  file_offset,
					  __btrfs_submit_bio_start_direct_io,
					  __btrfs_submit_bio_done);
		goto err;
	} else if (write) {
		/*
		 * If we aren't doing async submit, calculate the csum of the
		 * bio now.
		 */
		ret = btrfs_csum_one_bio(inode, bio, file_offset, 1);
		if (ret)
			goto err;
	} else {
		ret = btrfs_lookup_and_bind_dio_csum(inode, dip, bio,
						     file_offset);
		if (ret)
			goto err;
	}
map:
	ret = btrfs_map_bio(fs_info, bio, 0, async_submit);
err:
	bio_put(bio);
	return ret;
}

static int btrfs_submit_direct_hook(struct btrfs_dio_private *dip,
				    int skip_sum)
{
	struct inode *inode = dip->inode;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct bio *bio;
	struct bio *orig_bio = dip->orig_bio;
	struct bio_vec *bvec;
	u64 start_sector = orig_bio->bi_iter.bi_sector;
	u64 file_offset = dip->logical_offset;
	u64 submit_len = 0;
	u64 map_length;
	u32 blocksize = fs_info->sectorsize;
	int async_submit = 0;
	int nr_sectors;
	int ret;
	int i, j;

	map_length = orig_bio->bi_iter.bi_size;
	ret = btrfs_map_block(fs_info, btrfs_op(orig_bio), start_sector << 9,
			      &map_length, NULL, 0);
	if (ret)
		return -EIO;

	if (map_length >= orig_bio->bi_iter.bi_size) {
		bio = orig_bio;
		dip->flags |= BTRFS_DIO_ORIG_BIO_SUBMITTED;
		goto submit;
	}

	/* async crcs make it difficult to collect full stripe writes. */
	if (btrfs_get_alloc_profile(root, 1) & BTRFS_BLOCK_GROUP_RAID56_MASK)
		async_submit = 0;
	else
		async_submit = 1;

	bio = btrfs_dio_bio_alloc(orig_bio->bi_bdev, start_sector, GFP_NOFS);
	if (!bio)
		return -ENOMEM;

	bio->bi_opf = orig_bio->bi_opf;
	bio->bi_private = dip;
	bio->bi_end_io = btrfs_end_dio_bio;
	btrfs_io_bio(bio)->logical = file_offset;
	atomic_inc(&dip->pending_bios);

	bio_for_each_segment_all(bvec, orig_bio, j) {
		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info, bvec->bv_len);
		i = 0;
next_block:
		if (unlikely(map_length < submit_len + blocksize ||
		    bio_add_page(bio, bvec->bv_page, blocksize,
			    bvec->bv_offset + (i * blocksize)) < blocksize)) {
			/*
			 * inc the count before we submit the bio so
			 * we know the end IO handler won't happen before
			 * we inc the count. Otherwise, the dip might get freed
			 * before we're done setting it up
			 */
			atomic_inc(&dip->pending_bios);
			ret = __btrfs_submit_dio_bio(bio, inode,
						     file_offset, skip_sum,
						     async_submit);
			if (ret) {
				bio_put(bio);
				atomic_dec(&dip->pending_bios);
				goto out_err;
			}

			start_sector += submit_len >> 9;
			file_offset += submit_len;

			submit_len = 0;

			bio = btrfs_dio_bio_alloc(orig_bio->bi_bdev,
						  start_sector, GFP_NOFS);
			if (!bio)
				goto out_err;
			bio->bi_opf = orig_bio->bi_opf;
			bio->bi_private = dip;
			bio->bi_end_io = btrfs_end_dio_bio;
			btrfs_io_bio(bio)->logical = file_offset;

			map_length = orig_bio->bi_iter.bi_size;
			ret = btrfs_map_block(fs_info, btrfs_op(orig_bio),
					      start_sector << 9,
					      &map_length, NULL, 0);
			if (ret) {
				bio_put(bio);
				goto out_err;
			}

			goto next_block;
		} else {
			submit_len += blocksize;
			if (--nr_sectors) {
				i++;
				goto next_block;
			}
		}
	}

submit:
	ret = __btrfs_submit_dio_bio(bio, inode, file_offset, skip_sum,
				     async_submit);
	if (!ret)
		return 0;

	bio_put(bio);
out_err:
	dip->errors = 1;
	/*
	 * before atomic variable goto zero, we must
	 * make sure dip->errors is perceived to be set.
	 */
	smp_mb__before_atomic();
	if (atomic_dec_and_test(&dip->pending_bios))
		bio_io_error(dip->orig_bio);

	/* bio_end_io() will handle error, so we needn't return it */
	return 0;
}

static void btrfs_submit_direct(struct bio *dio_bio, struct inode *inode,
				loff_t file_offset)
{
	struct btrfs_dio_private *dip = NULL;
	struct bio *io_bio = NULL;
	struct btrfs_io_bio *btrfs_bio;
	int skip_sum;
	bool write = (bio_op(dio_bio) == REQ_OP_WRITE);
	int ret = 0;

	skip_sum = BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM;

	io_bio = btrfs_bio_clone(dio_bio, GFP_NOFS);
	if (!io_bio) {
		ret = -ENOMEM;
		goto free_ordered;
	}

	dip = kzalloc(sizeof(*dip), GFP_NOFS);
	if (!dip) {
		ret = -ENOMEM;
		goto free_ordered;
	}

	dip->private = dio_bio->bi_private;
	dip->inode = inode;
	dip->logical_offset = file_offset;
	dip->bytes = dio_bio->bi_iter.bi_size;
	dip->disk_bytenr = (u64)dio_bio->bi_iter.bi_sector << 9;
	io_bio->bi_private = dip;
	dip->orig_bio = io_bio;
	dip->dio_bio = dio_bio;
	atomic_set(&dip->pending_bios, 0);
	btrfs_bio = btrfs_io_bio(io_bio);
	btrfs_bio->logical = file_offset;

	if (write) {
		io_bio->bi_end_io = btrfs_endio_direct_write;
	} else {
		io_bio->bi_end_io = btrfs_endio_direct_read;
		dip->subio_endio = btrfs_subio_endio_read;
	}

	/*
	 * Reset the range for unsubmitted ordered extents (to a 0 length range)
	 * even if we fail to submit a bio, because in such case we do the
	 * corresponding error handling below and it must not be done a second
	 * time by btrfs_direct_IO().
	 */
	if (write) {
		struct btrfs_dio_data *dio_data = current->journal_info;

		dio_data->unsubmitted_oe_range_end = dip->logical_offset +
			dip->bytes;
		dio_data->unsubmitted_oe_range_start =
			dio_data->unsubmitted_oe_range_end;
	}

	ret = btrfs_submit_direct_hook(dip, skip_sum);
	if (!ret)
		return;

	if (btrfs_bio->end_io)
		btrfs_bio->end_io(btrfs_bio, ret);

free_ordered:
	/*
	 * If we arrived here it means either we failed to submit the dip
	 * or we either failed to clone the dio_bio or failed to allocate the
	 * dip. If we cloned the dio_bio and allocated the dip, we can just
	 * call bio_endio against our io_bio so that we get proper resource
	 * cleanup if we fail to submit the dip, otherwise, we must do the
	 * same as btrfs_endio_direct_[write|read] because we can't call these
	 * callbacks - they require an allocated dip and a clone of dio_bio.
	 */
	if (io_bio && dip) {
		io_bio->bi_error = -EIO;
		bio_endio(io_bio);
		/*
		 * The end io callbacks free our dip, do the final put on io_bio
		 * and all the cleanup and final put for dio_bio (through
		 * dio_end_io()).
		 */
		dip = NULL;
		io_bio = NULL;
	} else {
		if (write)
			btrfs_endio_direct_write_update_ordered(inode,
						file_offset,
						dio_bio->bi_iter.bi_size,
						0);
		else
			unlock_extent(&BTRFS_I(inode)->io_tree, file_offset,
			      file_offset + dio_bio->bi_iter.bi_size - 1);

		dio_bio->bi_error = -EIO;
		/*
		 * Releases and cleans up our dio_bio, no need to bio_put()
		 * nor bio_endio()/bio_io_error() against dio_bio.
		 */
		dio_end_io(dio_bio, ret);
	}
	if (io_bio)
		bio_put(io_bio);
	kfree(dip);
}

static ssize_t check_direct_IO(struct btrfs_fs_info *fs_info,
			       struct kiocb *iocb,
			       const struct iov_iter *iter, loff_t offset)
{
	int seg;
	int i;
	unsigned int blocksize_mask = fs_info->sectorsize - 1;
	ssize_t retval = -EINVAL;

	if (offset & blocksize_mask)
		goto out;

	if (iov_iter_alignment(iter) & blocksize_mask)
		goto out;

	/* If this is a write we don't need to check anymore */
	if (iov_iter_rw(iter) != READ || !iter_is_iovec(iter))
		return 0;
	/*
	 * Check to make sure we don't have duplicate iov_base's in this
	 * iovec, if so return EINVAL, otherwise we'll get csum errors
	 * when reading back.
	 */
	for (seg = 0; seg < iter->nr_segs; seg++) {
		for (i = seg + 1; i < iter->nr_segs; i++) {
			if (iter->iov[seg].iov_base == iter->iov[i].iov_base)
				goto out;
		}
	}
	retval = 0;
out:
	return retval;
}

static ssize_t btrfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_dio_data dio_data = { 0 };
	loff_t offset = iocb->ki_pos;
	size_t count = 0;
	int flags = 0;
	bool wakeup = true;
	bool relock = false;
	ssize_t ret;

	if (check_direct_IO(fs_info, iocb, iter, offset))
		return 0;

	inode_dio_begin(inode);
	smp_mb__after_atomic();

	/*
	 * The generic stuff only does filemap_write_and_wait_range, which
	 * isn't enough if we've written compressed pages to this area, so
	 * we need to flush the dirty pages again to make absolutely sure
	 * that any outstanding dirty pages are on disk.
	 */
	count = iov_iter_count(iter);
	if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
		     &BTRFS_I(inode)->runtime_flags))
		filemap_fdatawrite_range(inode->i_mapping, offset,
					 offset + count - 1);

	if (iov_iter_rw(iter) == WRITE) {
		/*
		 * If the write DIO is beyond the EOF, we need update
		 * the isize, but it is protected by i_mutex. So we can
		 * not unlock the i_mutex at this case.
		 */
		if (offset + count <= inode->i_size) {
			inode_unlock(inode);
			relock = true;
		}
		ret = btrfs_delalloc_reserve_space(inode, offset, count);
		if (ret)
			goto out;
		dio_data.outstanding_extents = div64_u64(count +
						BTRFS_MAX_EXTENT_SIZE - 1,
						BTRFS_MAX_EXTENT_SIZE);

		/*
		 * We need to know how many extents we reserved so that we can
		 * do the accounting properly if we go over the number we
		 * originally calculated.  Abuse current->journal_info for this.
		 */
		dio_data.reserve = round_up(count,
					    fs_info->sectorsize);
		dio_data.unsubmitted_oe_range_start = (u64)offset;
		dio_data.unsubmitted_oe_range_end = (u64)offset;
		current->journal_info = &dio_data;
	} else if (test_bit(BTRFS_INODE_READDIO_NEED_LOCK,
				     &BTRFS_I(inode)->runtime_flags)) {
		inode_dio_end(inode);
		flags = DIO_LOCKING | DIO_SKIP_HOLES;
		wakeup = false;
	}

	ret = __blockdev_direct_IO(iocb, inode,
				   fs_info->fs_devices->latest_bdev,
				   iter, btrfs_get_blocks_direct, NULL,
				   btrfs_submit_direct, flags);
	if (iov_iter_rw(iter) == WRITE) {
		current->journal_info = NULL;
		if (ret < 0 && ret != -EIOCBQUEUED) {
			if (dio_data.reserve)
				btrfs_delalloc_release_space(inode, offset,
							     dio_data.reserve);
			/*
			 * On error we might have left some ordered extents
			 * without submitting corresponding bios for them, so
			 * cleanup them up to avoid other tasks getting them
			 * and waiting for them to complete forever.
			 */
			if (dio_data.unsubmitted_oe_range_start <
			    dio_data.unsubmitted_oe_range_end)
				btrfs_endio_direct_write_update_ordered(inode,
					dio_data.unsubmitted_oe_range_start,
					dio_data.unsubmitted_oe_range_end -
					dio_data.unsubmitted_oe_range_start,
					0);
		} else if (ret >= 0 && (size_t)ret < count)
			btrfs_delalloc_release_space(inode, offset,
						     count - (size_t)ret);
	}
out:
	if (wakeup)
		inode_dio_end(inode);
	if (relock)
		inode_lock(inode);

	return ret;
}

#define BTRFS_FIEMAP_FLAGS	(FIEMAP_FLAG_SYNC)

static int btrfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len)
{
	int	ret;

	ret = fiemap_check_flags(fieinfo, BTRFS_FIEMAP_FLAGS);
	if (ret)
		return ret;

	return extent_fiemap(inode, fieinfo, start, len, btrfs_get_extent_fiemap);
}

int btrfs_readpage(struct file *file, struct page *page)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_read_full_page(tree, page, btrfs_get_extent, 0);
}

static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_io_tree *tree;
	struct inode *inode = page->mapping->host;
	int ret;

	if (current->flags & PF_MEMALLOC) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}

	/*
	 * If we are under memory pressure we will call this directly from the
	 * VM, we need to make sure we have the inode referenced for the ordered
	 * extent.  If not just return like we didn't do anything.
	 */
	if (!igrab(inode)) {
		redirty_page_for_writepage(wbc, page);
		return AOP_WRITEPAGE_ACTIVATE;
	}
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	ret = extent_write_full_page(tree, page, btrfs_get_extent, wbc);
	btrfs_add_delayed_iput(inode);
	return ret;
}

static int btrfs_writepages(struct address_space *mapping,
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
		put_page(page);
	}
	return ret;
}

static int btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	if (PageWriteback(page) || PageDirty(page))
		return 0;
	return __btrfs_releasepage(page, gfp_flags & GFP_NOFS);
}

static void btrfs_invalidatepage(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	u64 page_start = page_offset(page);
	u64 page_end = page_start + PAGE_SIZE - 1;
	u64 start;
	u64 end;
	int inode_evicting = inode->i_state & I_FREEING;

	/*
	 * we have the page locked, so new writeback can't start,
	 * and the dirty bit won't be cleared while we are here.
	 *
	 * Wait for IO on this page so that we can safely clear
	 * the PagePrivate2 bit and do ordered accounting
	 */
	wait_on_page_writeback(page);

	tree = &BTRFS_I(inode)->io_tree;
	if (offset) {
		btrfs_releasepage(page, GFP_NOFS);
		return;
	}

	if (!inode_evicting)
		lock_extent_bits(tree, page_start, page_end, &cached_state);
again:
	start = page_start;
	ordered = btrfs_lookup_ordered_range(inode, start,
					page_end - start + 1);
	if (ordered) {
		end = min(page_end, ordered->file_offset + ordered->len - 1);
		/*
		 * IO on this page will never be started, so we need
		 * to account for any ordered extents now
		 */
		if (!inode_evicting)
			clear_extent_bit(tree, start, end,
					 EXTENT_DIRTY | EXTENT_DELALLOC |
					 EXTENT_LOCKED | EXTENT_DO_ACCOUNTING |
					 EXTENT_DEFRAG, 1, 0, &cached_state,
					 GFP_NOFS);
		/*
		 * whoever cleared the private bit is responsible
		 * for the finish_ordered_io
		 */
		if (TestClearPagePrivate2(page)) {
			struct btrfs_ordered_inode_tree *tree;
			u64 new_len;

			tree = &BTRFS_I(inode)->ordered_tree;

			spin_lock_irq(&tree->lock);
			set_bit(BTRFS_ORDERED_TRUNCATED, &ordered->flags);
			new_len = start - ordered->file_offset;
			if (new_len < ordered->truncated_len)
				ordered->truncated_len = new_len;
			spin_unlock_irq(&tree->lock);

			if (btrfs_dec_test_ordered_pending(inode, &ordered,
							   start,
							   end - start + 1, 1))
				btrfs_finish_ordered_io(ordered);
		}
		btrfs_put_ordered_extent(ordered);
		if (!inode_evicting) {
			cached_state = NULL;
			lock_extent_bits(tree, start, end,
					 &cached_state);
		}

		start = end + 1;
		if (start < page_end)
			goto again;
	}

	/*
	 * Qgroup reserved space handler
	 * Page here will be either
	 * 1) Already written to disk
	 *    In this case, its reserved space is released from data rsv map
	 *    and will be freed by delayed_ref handler finally.
	 *    So even we call qgroup_free_data(), it won't decrease reserved
	 *    space.
	 * 2) Not written to disk
	 *    This means the reserved space should be freed here. However,
	 *    if a truncate invalidates the page (by clearing PageDirty)
	 *    and the page is accounted for while allocating extent
	 *    in btrfs_check_data_free_space() we let delayed_ref to
	 *    free the entire extent.
	 */
	if (PageDirty(page))
		btrfs_qgroup_free_data(inode, page_start, PAGE_SIZE);
	if (!inode_evicting) {
		clear_extent_bit(tree, page_start, page_end,
				 EXTENT_LOCKED | EXTENT_DIRTY |
				 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING |
				 EXTENT_DEFRAG, 1, 1,
				 &cached_state, GFP_NOFS);

		__btrfs_releasepage(page, GFP_NOFS);
	}

	ClearPageChecked(page);
	if (PagePrivate(page)) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		put_page(page);
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
	struct inode *inode = file_inode(vma->vm_file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	char *kaddr;
	unsigned long zero_start;
	loff_t size;
	int ret;
	int reserved = 0;
	u64 reserved_space;
	u64 page_start;
	u64 page_end;
	u64 end;

	reserved_space = PAGE_SIZE;

	sb_start_pagefault(inode->i_sb);
	page_start = page_offset(page);
	page_end = page_start + PAGE_SIZE - 1;
	end = page_end;

	/*
	 * Reserving delalloc space after obtaining the page lock can lead to
	 * deadlock. For example, if a dirty page is locked by this function
	 * and the call to btrfs_delalloc_reserve_space() ends up triggering
	 * dirty page write out, then the btrfs_writepage() function could
	 * end up waiting indefinitely to get a lock on the page currently
	 * being processed by btrfs_page_mkwrite() function.
	 */
	ret = btrfs_delalloc_reserve_space(inode, page_start,
					   reserved_space);
	if (!ret) {
		ret = file_update_time(vma->vm_file);
		reserved = 1;
	}
	if (ret) {
		if (ret == -ENOMEM)
			ret = VM_FAULT_OOM;
		else /* -ENOSPC, -EIO, etc */
			ret = VM_FAULT_SIGBUS;
		if (reserved)
			goto out;
		goto out_noreserve;
	}

	ret = VM_FAULT_NOPAGE; /* make the VM retry the fault */
again:
	lock_page(page);
	size = i_size_read(inode);

	if ((page->mapping != inode->i_mapping) ||
	    (page_start >= size)) {
		/* page got truncated out from underneath us */
		goto out_unlock;
	}
	wait_on_page_writeback(page);

	lock_extent_bits(io_tree, page_start, page_end, &cached_state);
	set_page_extent_mapped(page);

	/*
	 * we can't set the delalloc bits if there are pending ordered
	 * extents.  Drop our locks and wait for them to finish
	 */
	ordered = btrfs_lookup_ordered_range(inode, page_start, page_end);
	if (ordered) {
		unlock_extent_cached(io_tree, page_start, page_end,
				     &cached_state, GFP_NOFS);
		unlock_page(page);
		btrfs_start_ordered_extent(inode, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	if (page->index == ((size - 1) >> PAGE_SHIFT)) {
		reserved_space = round_up(size - page_start,
					  fs_info->sectorsize);
		if (reserved_space < PAGE_SIZE) {
			end = page_start + reserved_space - 1;
			spin_lock(&BTRFS_I(inode)->lock);
			BTRFS_I(inode)->outstanding_extents++;
			spin_unlock(&BTRFS_I(inode)->lock);
			btrfs_delalloc_release_space(inode, page_start,
						PAGE_SIZE - reserved_space);
		}
	}

	/*
	 * XXX - page_mkwrite gets called every time the page is dirtied, even
	 * if it was already dirty, so for space accounting reasons we need to
	 * clear any delalloc bits for the range we are fixing to save.  There
	 * is probably a better way to do this, but for now keep consistent with
	 * prepare_pages in the normal write path.
	 */
	clear_extent_bit(&BTRFS_I(inode)->io_tree, page_start, end,
			  EXTENT_DIRTY | EXTENT_DELALLOC |
			  EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			  0, 0, &cached_state, GFP_NOFS);

	ret = btrfs_set_extent_delalloc(inode, page_start, end,
					&cached_state, 0);
	if (ret) {
		unlock_extent_cached(io_tree, page_start, page_end,
				     &cached_state, GFP_NOFS);
		ret = VM_FAULT_SIGBUS;
		goto out_unlock;
	}
	ret = 0;

	/* page is wholly or partially inside EOF */
	if (page_start + PAGE_SIZE > size)
		zero_start = size & ~PAGE_MASK;
	else
		zero_start = PAGE_SIZE;

	if (zero_start != PAGE_SIZE) {
		kaddr = kmap(page);
		memset(kaddr + zero_start, 0, PAGE_SIZE - zero_start);
		flush_dcache_page(page);
		kunmap(page);
	}
	ClearPageChecked(page);
	set_page_dirty(page);
	SetPageUptodate(page);

	BTRFS_I(inode)->last_trans = fs_info->generation;
	BTRFS_I(inode)->last_sub_trans = BTRFS_I(inode)->root->log_transid;
	BTRFS_I(inode)->last_log_commit = BTRFS_I(inode)->root->last_log_commit;

	unlock_extent_cached(io_tree, page_start, page_end, &cached_state, GFP_NOFS);

out_unlock:
	if (!ret) {
		sb_end_pagefault(inode->i_sb);
		return VM_FAULT_LOCKED;
	}
	unlock_page(page);
out:
	btrfs_delalloc_release_space(inode, page_start, reserved_space);
out_noreserve:
	sb_end_pagefault(inode->i_sb);
	return ret;
}

static int btrfs_truncate(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *rsv;
	int ret = 0;
	int err = 0;
	struct btrfs_trans_handle *trans;
	u64 mask = fs_info->sectorsize - 1;
	u64 min_size = btrfs_calc_trunc_metadata_size(fs_info, 1);

	ret = btrfs_wait_ordered_range(inode, inode->i_size & (~mask),
				       (u64)-1);
	if (ret)
		return ret;

	/*
	 * Yes ladies and gentlemen, this is indeed ugly.  The fact is we have
	 * 3 things going on here
	 *
	 * 1) We need to reserve space for our orphan item and the space to
	 * delete our orphan item.  Lord knows we don't want to have a dangling
	 * orphan item because we didn't reserve space to remove it.
	 *
	 * 2) We need to reserve space to update our inode.
	 *
	 * 3) We need to have something to cache all the space that is going to
	 * be free'd up by the truncate operation, but also have some slack
	 * space reserved in case it uses space during the truncate (thank you
	 * very much snapshotting).
	 *
	 * And we need these to all be separate.  The fact is we can use a lot of
	 * space doing the truncate, and we have no earthly idea how much space
	 * we will use, so we need the truncate reservation to be separate so it
	 * doesn't end up using space reserved for updating the inode or
	 * removing the orphan item.  We also need to be able to stop the
	 * transaction and start a new one, which means we need to be able to
	 * update the inode several times, and we have no idea of knowing how
	 * many times that will be, so we can't just reserve 1 item for the
	 * entirety of the operation, so that has to be done separately as well.
	 * Then there is the orphan item, which does indeed need to be held on
	 * to for the whole operation, and we need nobody to touch this reserved
	 * space except the orphan code.
	 *
	 * So that leaves us with
	 *
	 * 1) root->orphan_block_rsv - for the orphan deletion.
	 * 2) rsv - for the truncate reservation, which we will steal from the
	 * transaction reservation.
	 * 3) fs_info->trans_block_rsv - this will have 1 items worth left for
	 * updating the inode.
	 */
	rsv = btrfs_alloc_block_rsv(fs_info, BTRFS_BLOCK_RSV_TEMP);
	if (!rsv)
		return -ENOMEM;
	rsv->size = min_size;
	rsv->failfast = 1;

	/*
	 * 1 for the truncate slack space
	 * 1 for updating the inode.
	 */
	trans = btrfs_start_transaction(root, 2);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out;
	}

	/* Migrate the slack space for the truncate to our reserve */
	ret = btrfs_block_rsv_migrate(&fs_info->trans_block_rsv, rsv,
				      min_size, 0);
	BUG_ON(ret);

	/*
	 * So if we truncate and then write and fsync we normally would just
	 * write the extents that changed, which is a problem if we need to
	 * first truncate that entire inode.  So set this flag so we write out
	 * all of the extents in the inode to the sync log so we're completely
	 * safe.
	 */
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(inode)->runtime_flags);
	trans->block_rsv = rsv;

	while (1) {
		ret = btrfs_truncate_inode_items(trans, root, inode,
						 inode->i_size,
						 BTRFS_EXTENT_DATA_KEY);
		if (ret != -ENOSPC && ret != -EAGAIN) {
			err = ret;
			break;
		}

		trans->block_rsv = &fs_info->trans_block_rsv;
		ret = btrfs_update_inode(trans, root, inode);
		if (ret) {
			err = ret;
			break;
		}

		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);

		trans = btrfs_start_transaction(root, 2);
		if (IS_ERR(trans)) {
			ret = err = PTR_ERR(trans);
			trans = NULL;
			break;
		}

		ret = btrfs_block_rsv_migrate(&fs_info->trans_block_rsv,
					      rsv, min_size, 0);
		BUG_ON(ret);	/* shouldn't happen */
		trans->block_rsv = rsv;
	}

	if (ret == 0 && inode->i_nlink > 0) {
		trans->block_rsv = root->orphan_block_rsv;
		ret = btrfs_orphan_del(trans, inode);
		if (ret)
			err = ret;
	}

	if (trans) {
		trans->block_rsv = &fs_info->trans_block_rsv;
		ret = btrfs_update_inode(trans, root, inode);
		if (ret && !err)
			err = ret;

		ret = btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);
	}
out:
	btrfs_free_block_rsv(fs_info, rsv);

	if (ret && !err)
		err = ret;

	return err;
}

/*
 * create a new subvolume directory/inode (helper for the ioctl).
 */
int btrfs_create_subvol_root(struct btrfs_trans_handle *trans,
			     struct btrfs_root *new_root,
			     struct btrfs_root *parent_root,
			     u64 new_dirid)
{
	struct inode *inode;
	int err;
	u64 index = 0;

	inode = btrfs_new_inode(trans, new_root, NULL, "..", 2,
				new_dirid, new_dirid,
				S_IFDIR | (~current_umask() & S_IRWXUGO),
				&index);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	set_nlink(inode, 1);
	btrfs_i_size_write(inode, 0);
	unlock_new_inode(inode);

	err = btrfs_subvol_inherit_props(trans, new_root, parent_root);
	if (err)
		btrfs_err(new_root->fs_info,
			  "error inheriting subvolume %llu properties: %d",
			  new_root->root_key.objectid, err);

	err = btrfs_update_inode(trans, new_root, inode);

	iput(inode);
	return err;
}

struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_inode *ei;
	struct inode *inode;

	ei = kmem_cache_alloc(btrfs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;

	ei->root = NULL;
	ei->generation = 0;
	ei->last_trans = 0;
	ei->last_sub_trans = 0;
	ei->logged_trans = 0;
	ei->delalloc_bytes = 0;
	ei->defrag_bytes = 0;
	ei->disk_i_size = 0;
	ei->flags = 0;
	ei->csum_bytes = 0;
	ei->index_cnt = (u64)-1;
	ei->dir_index = 0;
	ei->last_unlink_trans = 0;
	ei->last_log_commit = 0;
	ei->delayed_iput_count = 0;

	spin_lock_init(&ei->lock);
	ei->outstanding_extents = 0;
	ei->reserved_extents = 0;

	ei->runtime_flags = 0;
	ei->force_compress = BTRFS_COMPRESS_NONE;

	ei->delayed_node = NULL;

	ei->i_otime.tv_sec = 0;
	ei->i_otime.tv_nsec = 0;

	inode = &ei->vfs_inode;
	extent_map_tree_init(&ei->extent_tree);
	extent_io_tree_init(&ei->io_tree, &inode->i_data);
	extent_io_tree_init(&ei->io_failure_tree, &inode->i_data);
	ei->io_tree.track_uptodate = 1;
	ei->io_failure_tree.track_uptodate = 1;
	atomic_set(&ei->sync_writers, 0);
	mutex_init(&ei->log_mutex);
	mutex_init(&ei->delalloc_mutex);
	btrfs_ordered_inode_tree_init(&ei->ordered_tree);
	INIT_LIST_HEAD(&ei->delalloc_inodes);
	INIT_LIST_HEAD(&ei->delayed_iput);
	RB_CLEAR_NODE(&ei->rb_node);
	init_rwsem(&ei->dio_sem);

	return inode;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_destroy_inode(struct inode *inode)
{
	btrfs_drop_extent_cache(inode, 0, (u64)-1, 0);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}
#endif

static void btrfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

void btrfs_destroy_inode(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ordered_extent *ordered;
	struct btrfs_root *root = BTRFS_I(inode)->root;

	WARN_ON(!hlist_empty(&inode->i_dentry));
	WARN_ON(inode->i_data.nrpages);
	WARN_ON(BTRFS_I(inode)->outstanding_extents);
	WARN_ON(BTRFS_I(inode)->reserved_extents);
	WARN_ON(BTRFS_I(inode)->delalloc_bytes);
	WARN_ON(BTRFS_I(inode)->csum_bytes);
	WARN_ON(BTRFS_I(inode)->defrag_bytes);

	/*
	 * This can happen where we create an inode, but somebody else also
	 * created the same inode and we need to destroy the one we already
	 * created.
	 */
	if (!root)
		goto free;

	if (test_bit(BTRFS_INODE_HAS_ORPHAN_ITEM,
		     &BTRFS_I(inode)->runtime_flags)) {
		btrfs_info(fs_info, "inode %llu still on the orphan list",
			   btrfs_ino(inode));
		atomic_dec(&root->orphan_inodes);
	}

	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, (u64)-1);
		if (!ordered)
			break;
		else {
			btrfs_err(fs_info,
				  "found ordered extent %llu %llu on inode cleanup",
				  ordered->file_offset, ordered->len);
			btrfs_remove_ordered_extent(inode, ordered);
			btrfs_put_ordered_extent(ordered);
			btrfs_put_ordered_extent(ordered);
		}
	}
	btrfs_qgroup_check_reserved_leak(inode);
	inode_tree_del(inode);
	btrfs_drop_extent_cache(inode, 0, (u64)-1, 0);
free:
	call_rcu(&inode->i_rcu, btrfs_i_callback);
}

int btrfs_drop_inode(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;

	if (root == NULL)
		return 1;

	/* the snap/subvol tree is on deleting */
	if (btrfs_root_refs(&root->root_item) == 0)
		return 1;
	else
		return generic_drop_inode(inode);
}

static void init_once(void *foo)
{
	struct btrfs_inode *ei = (struct btrfs_inode *) foo;

	inode_init_once(&ei->vfs_inode);
}

void btrfs_destroy_cachep(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(btrfs_inode_cachep);
	kmem_cache_destroy(btrfs_trans_handle_cachep);
	kmem_cache_destroy(btrfs_transaction_cachep);
	kmem_cache_destroy(btrfs_path_cachep);
	kmem_cache_destroy(btrfs_free_space_cachep);
}

int btrfs_init_cachep(void)
{
	btrfs_inode_cachep = kmem_cache_create("btrfs_inode",
			sizeof(struct btrfs_inode), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT,
			init_once);
	if (!btrfs_inode_cachep)
		goto fail;

	btrfs_trans_handle_cachep = kmem_cache_create("btrfs_trans_handle",
			sizeof(struct btrfs_trans_handle), 0,
			SLAB_TEMPORARY | SLAB_MEM_SPREAD, NULL);
	if (!btrfs_trans_handle_cachep)
		goto fail;

	btrfs_transaction_cachep = kmem_cache_create("btrfs_transaction",
			sizeof(struct btrfs_transaction), 0,
			SLAB_TEMPORARY | SLAB_MEM_SPREAD, NULL);
	if (!btrfs_transaction_cachep)
		goto fail;

	btrfs_path_cachep = kmem_cache_create("btrfs_path",
			sizeof(struct btrfs_path), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!btrfs_path_cachep)
		goto fail;

	btrfs_free_space_cachep = kmem_cache_create("btrfs_free_space",
			sizeof(struct btrfs_free_space), 0,
			SLAB_MEM_SPREAD, NULL);
	if (!btrfs_free_space_cachep)
		goto fail;

	return 0;
fail:
	btrfs_destroy_cachep();
	return -ENOMEM;
}

static int btrfs_getattr(struct vfsmount *mnt,
			 struct dentry *dentry, struct kstat *stat)
{
	u64 delalloc_bytes;
	struct inode *inode = d_inode(dentry);
	u32 blocksize = inode->i_sb->s_blocksize;

	generic_fillattr(inode, stat);
	stat->dev = BTRFS_I(inode)->root->anon_dev;

	spin_lock(&BTRFS_I(inode)->lock);
	delalloc_bytes = BTRFS_I(inode)->delalloc_bytes;
	spin_unlock(&BTRFS_I(inode)->lock);
	stat->blocks = (ALIGN(inode_get_bytes(inode), blocksize) +
			ALIGN(delalloc_bytes, blocksize)) >> 9;
	return 0;
}

static int btrfs_rename_exchange(struct inode *old_dir,
			      struct dentry *old_dentry,
			      struct inode *new_dir,
			      struct dentry *new_dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(old_dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct btrfs_root *dest = BTRFS_I(new_dir)->root;
	struct inode *new_inode = new_dentry->d_inode;
	struct inode *old_inode = old_dentry->d_inode;
	struct timespec ctime = current_time(old_inode);
	struct dentry *parent;
	u64 old_ino = btrfs_ino(old_inode);
	u64 new_ino = btrfs_ino(new_inode);
	u64 old_idx = 0;
	u64 new_idx = 0;
	u64 root_objectid;
	int ret;
	bool root_log_pinned = false;
	bool dest_log_pinned = false;

	/* we only allow rename subvolume link between subvolumes */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID && root != dest)
		return -EXDEV;

	/* close the race window with snapshot create/destroy ioctl */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID)
		down_read(&fs_info->subvol_sem);
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID)
		down_read(&fs_info->subvol_sem);

	/*
	 * We want to reserve the absolute worst case amount of items.  So if
	 * both inodes are subvols and we need to unlink them then that would
	 * require 4 item modifications, but if they are both normal inodes it
	 * would require 5 item modifications, so we'll assume their normal
	 * inodes.  So 5 * 2 is 10, plus 2 for the new links, so 12 total items
	 * should cover the worst case number of items we'll modify.
	 */
	trans = btrfs_start_transaction(root, 12);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_notrans;
	}

	/*
	 * We need to find a free sequence number both in the source and
	 * in the destination directory for the exchange.
	 */
	ret = btrfs_set_inode_index(new_dir, &old_idx);
	if (ret)
		goto out_fail;
	ret = btrfs_set_inode_index(old_dir, &new_idx);
	if (ret)
		goto out_fail;

	BTRFS_I(old_inode)->dir_index = 0ULL;
	BTRFS_I(new_inode)->dir_index = 0ULL;

	/* Reference for the source. */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(fs_info, trans);
	} else {
		btrfs_pin_log_trans(root);
		root_log_pinned = true;
		ret = btrfs_insert_inode_ref(trans, dest,
					     new_dentry->d_name.name,
					     new_dentry->d_name.len,
					     old_ino,
					     btrfs_ino(new_dir), old_idx);
		if (ret)
			goto out_fail;
	}

	/* And now for the dest. */
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(fs_info, trans);
	} else {
		btrfs_pin_log_trans(dest);
		dest_log_pinned = true;
		ret = btrfs_insert_inode_ref(trans, root,
					     old_dentry->d_name.name,
					     old_dentry->d_name.len,
					     new_ino,
					     btrfs_ino(old_dir), new_idx);
		if (ret)
			goto out_fail;
	}

	/* Update inode version and ctime/mtime. */
	inode_inc_iversion(old_dir);
	inode_inc_iversion(new_dir);
	inode_inc_iversion(old_inode);
	inode_inc_iversion(new_inode);
	old_dir->i_ctime = old_dir->i_mtime = ctime;
	new_dir->i_ctime = new_dir->i_mtime = ctime;
	old_inode->i_ctime = ctime;
	new_inode->i_ctime = ctime;

	if (old_dentry->d_parent != new_dentry->d_parent) {
		btrfs_record_unlink_dir(trans, old_dir, old_inode, 1);
		btrfs_record_unlink_dir(trans, new_dir, new_inode, 1);
	}

	/* src is a subvolume */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		root_objectid = BTRFS_I(old_inode)->root->root_key.objectid;
		ret = btrfs_unlink_subvol(trans, root, old_dir,
					  root_objectid,
					  old_dentry->d_name.name,
					  old_dentry->d_name.len);
	} else { /* src is an inode */
		ret = __btrfs_unlink_inode(trans, root, old_dir,
					   old_dentry->d_inode,
					   old_dentry->d_name.name,
					   old_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_inode(trans, root, old_inode);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	/* dest is a subvolume */
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID) {
		root_objectid = BTRFS_I(new_inode)->root->root_key.objectid;
		ret = btrfs_unlink_subvol(trans, dest, new_dir,
					  root_objectid,
					  new_dentry->d_name.name,
					  new_dentry->d_name.len);
	} else { /* dest is an inode */
		ret = __btrfs_unlink_inode(trans, dest, new_dir,
					   new_dentry->d_inode,
					   new_dentry->d_name.name,
					   new_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_inode(trans, dest, new_inode);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, new_dir, old_inode,
			     new_dentry->d_name.name,
			     new_dentry->d_name.len, 0, old_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, old_dir, new_inode,
			     old_dentry->d_name.name,
			     old_dentry->d_name.len, 0, new_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_inode->i_nlink == 1)
		BTRFS_I(old_inode)->dir_index = old_idx;
	if (new_inode->i_nlink == 1)
		BTRFS_I(new_inode)->dir_index = new_idx;

	if (root_log_pinned) {
		parent = new_dentry->d_parent;
		btrfs_log_new_name(trans, old_inode, old_dir, parent);
		btrfs_end_log_trans(root);
		root_log_pinned = false;
	}
	if (dest_log_pinned) {
		parent = old_dentry->d_parent;
		btrfs_log_new_name(trans, new_inode, new_dir, parent);
		btrfs_end_log_trans(dest);
		dest_log_pinned = false;
	}
out_fail:
	/*
	 * If we have pinned a log and an error happened, we unpin tasks
	 * trying to sync the log and force them to fallback to a transaction
	 * commit if the log currently contains any of the inodes involved in
	 * this rename operation (to ensure we do not persist a log with an
	 * inconsistent state for any of these inodes or leading to any
	 * inconsistencies when replayed). If the transaction was aborted, the
	 * abortion reason is propagated to userspace when attempting to commit
	 * the transaction. If the log does not contain any of these inodes, we
	 * allow the tasks to sync it.
	 */
	if (ret && (root_log_pinned || dest_log_pinned)) {
		if (btrfs_inode_in_log(old_dir, fs_info->generation) ||
		    btrfs_inode_in_log(new_dir, fs_info->generation) ||
		    btrfs_inode_in_log(old_inode, fs_info->generation) ||
		    (new_inode &&
		     btrfs_inode_in_log(new_inode, fs_info->generation)))
			btrfs_set_log_full_commit(fs_info, trans);

		if (root_log_pinned) {
			btrfs_end_log_trans(root);
			root_log_pinned = false;
		}
		if (dest_log_pinned) {
			btrfs_end_log_trans(dest);
			dest_log_pinned = false;
		}
	}
	ret = btrfs_end_transaction(trans);
out_notrans:
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	return ret;
}

static int btrfs_whiteout_for_rename(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct inode *dir,
				     struct dentry *dentry)
{
	int ret;
	struct inode *inode;
	u64 objectid;
	u64 index;

	ret = btrfs_find_free_ino(root, &objectid);
	if (ret)
		return ret;

	inode = btrfs_new_inode(trans, root, dir,
				dentry->d_name.name,
				dentry->d_name.len,
				btrfs_ino(dir),
				objectid,
				S_IFCHR | WHITEOUT_MODE,
				&index);

	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		return ret;
	}

	inode->i_op = &btrfs_special_inode_operations;
	init_special_inode(inode, inode->i_mode,
		WHITEOUT_DEV);

	ret = btrfs_init_inode_security(trans, inode, dir,
				&dentry->d_name);
	if (ret)
		goto out;

	ret = btrfs_add_nondir(trans, dir, dentry,
				inode, 0, index);
	if (ret)
		goto out;

	ret = btrfs_update_inode(trans, root, inode);
out:
	unlock_new_inode(inode);
	if (ret)
		inode_dec_link_count(inode);
	iput(inode);

	return ret;
}

static int btrfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(old_dir->i_sb);
	struct btrfs_trans_handle *trans;
	unsigned int trans_num_items;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct btrfs_root *dest = BTRFS_I(new_dir)->root;
	struct inode *new_inode = d_inode(new_dentry);
	struct inode *old_inode = d_inode(old_dentry);
	u64 index = 0;
	u64 root_objectid;
	int ret;
	u64 old_ino = btrfs_ino(old_inode);
	bool log_pinned = false;

	if (btrfs_ino(new_dir) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)
		return -EPERM;

	/* we only allow rename subvolume link between subvolumes */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID && root != dest)
		return -EXDEV;

	if (old_ino == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID ||
	    (new_inode && btrfs_ino(new_inode) == BTRFS_FIRST_FREE_OBJECTID))
		return -ENOTEMPTY;

	if (S_ISDIR(old_inode->i_mode) && new_inode &&
	    new_inode->i_size > BTRFS_EMPTY_DIR_SIZE)
		return -ENOTEMPTY;


	/* check for collisions, even if the  name isn't there */
	ret = btrfs_check_dir_item_collision(dest, new_dir->i_ino,
			     new_dentry->d_name.name,
			     new_dentry->d_name.len);

	if (ret) {
		if (ret == -EEXIST) {
			/* we shouldn't get
			 * eexist without a new_inode */
			if (WARN_ON(!new_inode)) {
				return ret;
			}
		} else {
			/* maybe -EOVERFLOW */
			return ret;
		}
	}
	ret = 0;

	/*
	 * we're using rename to replace one file with another.  Start IO on it
	 * now so  we don't add too much work to the end of the transaction
	 */
	if (new_inode && S_ISREG(old_inode->i_mode) && new_inode->i_size)
		filemap_flush(old_inode->i_mapping);

	/* close the racy window with snapshot create/destroy ioctl */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID)
		down_read(&fs_info->subvol_sem);
	/*
	 * We want to reserve the absolute worst case amount of items.  So if
	 * both inodes are subvols and we need to unlink them then that would
	 * require 4 item modifications, but if they are both normal inodes it
	 * would require 5 item modifications, so we'll assume they are normal
	 * inodes.  So 5 * 2 is 10, plus 1 for the new link, so 11 total items
	 * should cover the worst case number of items we'll modify.
	 * If our rename has the whiteout flag, we need more 5 units for the
	 * new inode (1 inode item, 1 inode ref, 2 dir items and 1 xattr item
	 * when selinux is enabled).
	 */
	trans_num_items = 11;
	if (flags & RENAME_WHITEOUT)
		trans_num_items += 5;
	trans = btrfs_start_transaction(root, trans_num_items);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_notrans;
	}

	if (dest != root)
		btrfs_record_root_in_trans(trans, dest);

	ret = btrfs_set_inode_index(new_dir, &index);
	if (ret)
		goto out_fail;

	BTRFS_I(old_inode)->dir_index = 0ULL;
	if (unlikely(old_ino == BTRFS_FIRST_FREE_OBJECTID)) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(fs_info, trans);
	} else {
		btrfs_pin_log_trans(root);
		log_pinned = true;
		ret = btrfs_insert_inode_ref(trans, dest,
					     new_dentry->d_name.name,
					     new_dentry->d_name.len,
					     old_ino,
					     btrfs_ino(new_dir), index);
		if (ret)
			goto out_fail;
	}

	inode_inc_iversion(old_dir);
	inode_inc_iversion(new_dir);
	inode_inc_iversion(old_inode);
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	old_inode->i_ctime = current_time(old_dir);

	if (old_dentry->d_parent != new_dentry->d_parent)
		btrfs_record_unlink_dir(trans, old_dir, old_inode, 1);

	if (unlikely(old_ino == BTRFS_FIRST_FREE_OBJECTID)) {
		root_objectid = BTRFS_I(old_inode)->root->root_key.objectid;
		ret = btrfs_unlink_subvol(trans, root, old_dir, root_objectid,
					old_dentry->d_name.name,
					old_dentry->d_name.len);
	} else {
		ret = __btrfs_unlink_inode(trans, root, old_dir,
					d_inode(old_dentry),
					old_dentry->d_name.name,
					old_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_inode(trans, root, old_inode);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (new_inode) {
		inode_inc_iversion(new_inode);
		new_inode->i_ctime = current_time(new_inode);
		if (unlikely(btrfs_ino(new_inode) ==
			     BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
			root_objectid = BTRFS_I(new_inode)->location.objectid;
			ret = btrfs_unlink_subvol(trans, dest, new_dir,
						root_objectid,
						new_dentry->d_name.name,
						new_dentry->d_name.len);
			BUG_ON(new_inode->i_nlink == 0);
		} else {
			ret = btrfs_unlink_inode(trans, dest, new_dir,
						 d_inode(new_dentry),
						 new_dentry->d_name.name,
						 new_dentry->d_name.len);
		}
		if (!ret && new_inode->i_nlink == 0)
			ret = btrfs_orphan_add(trans, d_inode(new_dentry));
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_fail;
		}
	}

	ret = btrfs_add_link(trans, new_dir, old_inode,
			     new_dentry->d_name.name,
			     new_dentry->d_name.len, 0, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_inode->i_nlink == 1)
		BTRFS_I(old_inode)->dir_index = index;

	if (log_pinned) {
		struct dentry *parent = new_dentry->d_parent;

		btrfs_log_new_name(trans, old_inode, old_dir, parent);
		btrfs_end_log_trans(root);
		log_pinned = false;
	}

	if (flags & RENAME_WHITEOUT) {
		ret = btrfs_whiteout_for_rename(trans, root, old_dir,
						old_dentry);

		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_fail;
		}
	}
out_fail:
	/*
	 * If we have pinned the log and an error happened, we unpin tasks
	 * trying to sync the log and force them to fallback to a transaction
	 * commit if the log currently contains any of the inodes involved in
	 * this rename operation (to ensure we do not persist a log with an
	 * inconsistent state for any of these inodes or leading to any
	 * inconsistencies when replayed). If the transaction was aborted, the
	 * abortion reason is propagated to userspace when attempting to commit
	 * the transaction. If the log does not contain any of these inodes, we
	 * allow the tasks to sync it.
	 */
	if (ret && log_pinned) {
		if (btrfs_inode_in_log(old_dir, fs_info->generation) ||
		    btrfs_inode_in_log(new_dir, fs_info->generation) ||
		    btrfs_inode_in_log(old_inode, fs_info->generation) ||
		    (new_inode &&
		     btrfs_inode_in_log(new_inode, fs_info->generation)))
			btrfs_set_log_full_commit(fs_info, trans);

		btrfs_end_log_trans(root);
		log_pinned = false;
	}
	btrfs_end_transaction(trans);
out_notrans:
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	return ret;
}

static int btrfs_rename2(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry,
			 unsigned int flags)
{
	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return btrfs_rename_exchange(old_dir, old_dentry, new_dir,
					  new_dentry);

	return btrfs_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

static void btrfs_run_delalloc_work(struct btrfs_work *work)
{
	struct btrfs_delalloc_work *delalloc_work;
	struct inode *inode;

	delalloc_work = container_of(work, struct btrfs_delalloc_work,
				     work);
	inode = delalloc_work->inode;
	filemap_flush(inode->i_mapping);
	if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
				&BTRFS_I(inode)->runtime_flags))
		filemap_flush(inode->i_mapping);

	if (delalloc_work->delay_iput)
		btrfs_add_delayed_iput(inode);
	else
		iput(inode);
	complete(&delalloc_work->completion);
}

struct btrfs_delalloc_work *btrfs_alloc_delalloc_work(struct inode *inode,
						    int delay_iput)
{
	struct btrfs_delalloc_work *work;

	work = kmalloc(sizeof(*work), GFP_NOFS);
	if (!work)
		return NULL;

	init_completion(&work->completion);
	INIT_LIST_HEAD(&work->list);
	work->inode = inode;
	work->delay_iput = delay_iput;
	WARN_ON_ONCE(!inode);
	btrfs_init_work(&work->work, btrfs_flush_delalloc_helper,
			btrfs_run_delalloc_work, NULL, NULL);

	return work;
}

void btrfs_wait_and_free_delalloc_work(struct btrfs_delalloc_work *work)
{
	wait_for_completion(&work->completion);
	kfree(work);
}

/*
 * some fairly slow code that needs optimization. This walks the list
 * of all the inodes with pending delalloc and forces them to disk.
 */
static int __start_delalloc_inodes(struct btrfs_root *root, int delay_iput,
				   int nr)
{
	struct btrfs_inode *binode;
	struct inode *inode;
	struct btrfs_delalloc_work *work, *next;
	struct list_head works;
	struct list_head splice;
	int ret = 0;

	INIT_LIST_HEAD(&works);
	INIT_LIST_HEAD(&splice);

	mutex_lock(&root->delalloc_mutex);
	spin_lock(&root->delalloc_lock);
	list_splice_init(&root->delalloc_inodes, &splice);
	while (!list_empty(&splice)) {
		binode = list_entry(splice.next, struct btrfs_inode,
				    delalloc_inodes);

		list_move_tail(&binode->delalloc_inodes,
			       &root->delalloc_inodes);
		inode = igrab(&binode->vfs_inode);
		if (!inode) {
			cond_resched_lock(&root->delalloc_lock);
			continue;
		}
		spin_unlock(&root->delalloc_lock);

		work = btrfs_alloc_delalloc_work(inode, delay_iput);
		if (!work) {
			if (delay_iput)
				btrfs_add_delayed_iput(inode);
			else
				iput(inode);
			ret = -ENOMEM;
			goto out;
		}
		list_add_tail(&work->list, &works);
		btrfs_queue_work(root->fs_info->flush_workers,
				 &work->work);
		ret++;
		if (nr != -1 && ret >= nr)
			goto out;
		cond_resched();
		spin_lock(&root->delalloc_lock);
	}
	spin_unlock(&root->delalloc_lock);

out:
	list_for_each_entry_safe(work, next, &works, list) {
		list_del_init(&work->list);
		btrfs_wait_and_free_delalloc_work(work);
	}

	if (!list_empty_careful(&splice)) {
		spin_lock(&root->delalloc_lock);
		list_splice_tail(&splice, &root->delalloc_inodes);
		spin_unlock(&root->delalloc_lock);
	}
	mutex_unlock(&root->delalloc_mutex);
	return ret;
}

int btrfs_start_delalloc_inodes(struct btrfs_root *root, int delay_iput)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		return -EROFS;

	ret = __start_delalloc_inodes(root, delay_iput, -1);
	if (ret > 0)
		ret = 0;
	/*
	 * the filemap_flush will queue IO into the worker threads, but
	 * we have to make sure the IO is actually started and that
	 * ordered extents get created before we return
	 */
	atomic_inc(&fs_info->async_submit_draining);
	while (atomic_read(&fs_info->nr_async_submits) ||
	       atomic_read(&fs_info->async_delalloc_pages)) {
		wait_event(fs_info->async_submit_wait,
			   (atomic_read(&fs_info->nr_async_submits) == 0 &&
			    atomic_read(&fs_info->async_delalloc_pages) == 0));
	}
	atomic_dec(&fs_info->async_submit_draining);
	return ret;
}

int btrfs_start_delalloc_roots(struct btrfs_fs_info *fs_info, int delay_iput,
			       int nr)
{
	struct btrfs_root *root;
	struct list_head splice;
	int ret;

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		return -EROFS;

	INIT_LIST_HEAD(&splice);

	mutex_lock(&fs_info->delalloc_root_mutex);
	spin_lock(&fs_info->delalloc_root_lock);
	list_splice_init(&fs_info->delalloc_roots, &splice);
	while (!list_empty(&splice) && nr) {
		root = list_first_entry(&splice, struct btrfs_root,
					delalloc_root);
		root = btrfs_grab_fs_root(root);
		BUG_ON(!root);
		list_move_tail(&root->delalloc_root,
			       &fs_info->delalloc_roots);
		spin_unlock(&fs_info->delalloc_root_lock);

		ret = __start_delalloc_inodes(root, delay_iput, nr);
		btrfs_put_fs_root(root);
		if (ret < 0)
			goto out;

		if (nr != -1) {
			nr -= ret;
			WARN_ON(nr < 0);
		}
		spin_lock(&fs_info->delalloc_root_lock);
	}
	spin_unlock(&fs_info->delalloc_root_lock);

	ret = 0;
	atomic_inc(&fs_info->async_submit_draining);
	while (atomic_read(&fs_info->nr_async_submits) ||
	      atomic_read(&fs_info->async_delalloc_pages)) {
		wait_event(fs_info->async_submit_wait,
		   (atomic_read(&fs_info->nr_async_submits) == 0 &&
		    atomic_read(&fs_info->async_delalloc_pages) == 0));
	}
	atomic_dec(&fs_info->async_submit_draining);
out:
	if (!list_empty_careful(&splice)) {
		spin_lock(&fs_info->delalloc_root_lock);
		list_splice_tail(&splice, &fs_info->delalloc_roots);
		spin_unlock(&fs_info->delalloc_root_lock);
	}
	mutex_unlock(&fs_info->delalloc_root_mutex);
	return ret;
}

static int btrfs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct inode *inode = NULL;
	int err;
	int drop_inode = 0;
	u64 objectid;
	u64 index = 0;
	int name_len;
	int datasize;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	struct extent_buffer *leaf;

	name_len = strlen(symname);
	if (name_len > BTRFS_MAX_INLINE_DATA_SIZE(fs_info))
		return -ENAMETOOLONG;

	/*
	 * 2 items for inode item and ref
	 * 2 items for dir items
	 * 1 item for updating parent inode item
	 * 1 item for the inline extent item
	 * 1 item for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 7);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_ino(root, &objectid);
	if (err)
		goto out_unlock;

	inode = btrfs_new_inode(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len, btrfs_ino(dir), objectid,
				S_IFLNK|S_IRWXUGO, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_unlock;
	}

	/*
	* If the active LSM wants to access the inode during
	* d_instantiate it needs these. Smack checks to see
	* if the filesystem supports xattrs by looking at the
	* ops vector.
	*/
	inode->i_fop = &btrfs_file_operations;
	inode->i_op = &btrfs_file_inode_operations;
	inode->i_mapping->a_ops = &btrfs_aops;
	BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_unlock_inode;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out_unlock_inode;
	}
	key.objectid = btrfs_ino(inode);
	key.offset = 0;
	key.type = BTRFS_EXTENT_DATA_KEY;
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (err) {
		btrfs_free_path(path);
		goto out_unlock_inode;
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
	inode_nohighmem(inode);
	inode->i_mapping->a_ops = &btrfs_symlink_aops;
	inode_set_bytes(inode, name_len);
	btrfs_i_size_write(inode, name_len);
	err = btrfs_update_inode(trans, root, inode);
	/*
	 * Last step, add directory indexes for our symlink inode. This is the
	 * last step to avoid extra cleanup of these indexes if an error happens
	 * elsewhere above.
	 */
	if (!err)
		err = btrfs_add_nondir(trans, dir, dentry, inode, 0, index);
	if (err) {
		drop_inode = 1;
		goto out_unlock_inode;
	}

	unlock_new_inode(inode);
	d_instantiate(dentry, inode);

out_unlock:
	btrfs_end_transaction(trans);
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;

out_unlock_inode:
	drop_inode = 1;
	unlock_new_inode(inode);
	goto out_unlock;
}

static int __btrfs_prealloc_file_range(struct inode *inode, int mode,
				       u64 start, u64 num_bytes, u64 min_size,
				       loff_t actual_len, u64 *alloc_hint,
				       struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key ins;
	u64 cur_offset = start;
	u64 i_size;
	u64 cur_bytes;
	u64 last_alloc = (u64)-1;
	int ret = 0;
	bool own_trans = true;
	u64 end = start + num_bytes - 1;

	if (trans)
		own_trans = false;
	while (num_bytes > 0) {
		if (own_trans) {
			trans = btrfs_start_transaction(root, 3);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				break;
			}
		}

		cur_bytes = min_t(u64, num_bytes, SZ_256M);
		cur_bytes = max(cur_bytes, min_size);
		/*
		 * If we are severely fragmented we could end up with really
		 * small allocations, so if the allocator is returning small
		 * chunks lets make its job easier by only searching for those
		 * sized chunks.
		 */
		cur_bytes = min(cur_bytes, last_alloc);
		ret = btrfs_reserve_extent(root, cur_bytes, cur_bytes,
				min_size, 0, *alloc_hint, &ins, 1, 0);
		if (ret) {
			if (own_trans)
				btrfs_end_transaction(trans);
			break;
		}
		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		last_alloc = ins.offset;
		ret = insert_reserved_file_extent(trans, inode,
						  cur_offset, ins.objectid,
						  ins.offset, ins.offset,
						  ins.offset, 0, 0, 0,
						  BTRFS_FILE_EXTENT_PREALLOC);
		if (ret) {
			btrfs_free_reserved_extent(fs_info, ins.objectid,
						   ins.offset, 0);
			btrfs_abort_transaction(trans, ret);
			if (own_trans)
				btrfs_end_transaction(trans);
			break;
		}

		btrfs_drop_extent_cache(inode, cur_offset,
					cur_offset + ins.offset -1, 0);

		em = alloc_extent_map();
		if (!em) {
			set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
				&BTRFS_I(inode)->runtime_flags);
			goto next;
		}

		em->start = cur_offset;
		em->orig_start = cur_offset;
		em->len = ins.offset;
		em->block_start = ins.objectid;
		em->block_len = ins.offset;
		em->orig_block_len = ins.offset;
		em->ram_bytes = ins.offset;
		em->bdev = fs_info->fs_devices->latest_bdev;
		set_bit(EXTENT_FLAG_PREALLOC, &em->flags);
		em->generation = trans->transid;

		while (1) {
			write_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em, 1);
			write_unlock(&em_tree->lock);
			if (ret != -EEXIST)
				break;
			btrfs_drop_extent_cache(inode, cur_offset,
						cur_offset + ins.offset - 1,
						0);
		}
		free_extent_map(em);
next:
		num_bytes -= ins.offset;
		cur_offset += ins.offset;
		*alloc_hint = ins.objectid + ins.offset;

		inode_inc_iversion(inode);
		inode->i_ctime = current_time(inode);
		BTRFS_I(inode)->flags |= BTRFS_INODE_PREALLOC;
		if (!(mode & FALLOC_FL_KEEP_SIZE) &&
		    (actual_len > inode->i_size) &&
		    (cur_offset > inode->i_size)) {
			if (cur_offset > actual_len)
				i_size = actual_len;
			else
				i_size = cur_offset;
			i_size_write(inode, i_size);
			btrfs_ordered_update_i_size(inode, i_size, NULL);
		}

		ret = btrfs_update_inode(trans, root, inode);

		if (ret) {
			btrfs_abort_transaction(trans, ret);
			if (own_trans)
				btrfs_end_transaction(trans);
			break;
		}

		if (own_trans)
			btrfs_end_transaction(trans);
	}
	if (cur_offset < end)
		btrfs_free_reserved_data_space(inode, cur_offset,
			end - cur_offset + 1);
	return ret;
}

int btrfs_prealloc_file_range(struct inode *inode, int mode,
			      u64 start, u64 num_bytes, u64 min_size,
			      loff_t actual_len, u64 *alloc_hint)
{
	return __btrfs_prealloc_file_range(inode, mode, start, num_bytes,
					   min_size, actual_len, alloc_hint,
					   NULL);
}

int btrfs_prealloc_file_range_trans(struct inode *inode,
				    struct btrfs_trans_handle *trans, int mode,
				    u64 start, u64 num_bytes, u64 min_size,
				    loff_t actual_len, u64 *alloc_hint)
{
	return __btrfs_prealloc_file_range(inode, mode, start, num_bytes,
					   min_size, actual_len, alloc_hint, trans);
}

static int btrfs_set_page_dirty(struct page *page)
{
	return __set_page_dirty_nobuffers(page);
}

static int btrfs_permission(struct inode *inode, int mask)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	umode_t mode = inode->i_mode;

	if (mask & MAY_WRITE &&
	    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode))) {
		if (btrfs_root_readonly(root))
			return -EROFS;
		if (BTRFS_I(inode)->flags & BTRFS_INODE_READONLY)
			return -EACCES;
	}
	return generic_permission(inode, mask);
}

static int btrfs_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
	u64 objectid;
	u64 index;
	int ret = 0;

	/*
	 * 5 units required for adding orphan entry
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_find_free_ino(root, &objectid);
	if (ret)
		goto out;

	inode = btrfs_new_inode(trans, root, dir, NULL, 0,
				btrfs_ino(dir), objectid, mode, &index);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		inode = NULL;
		goto out;
	}

	inode->i_fop = &btrfs_file_operations;
	inode->i_op = &btrfs_file_inode_operations;

	inode->i_mapping->a_ops = &btrfs_aops;
	BTRFS_I(inode)->io_tree.ops = &btrfs_extent_io_ops;

	ret = btrfs_init_inode_security(trans, inode, dir, NULL);
	if (ret)
		goto out_inode;

	ret = btrfs_update_inode(trans, root, inode);
	if (ret)
		goto out_inode;
	ret = btrfs_orphan_add(trans, inode);
	if (ret)
		goto out_inode;

	/*
	 * We set number of links to 0 in btrfs_new_inode(), and here we set
	 * it to 1 because d_tmpfile() will issue a warning if the count is 0,
	 * through:
	 *
	 *    d_tmpfile() -> inode_dec_link_count() -> drop_nlink()
	 */
	set_nlink(inode, 1);
	unlock_new_inode(inode);
	d_tmpfile(dentry, inode);
	mark_inode_dirty(inode);

out:
	btrfs_end_transaction(trans);
	if (ret)
		iput(inode);
	btrfs_balance_delayed_items(fs_info);
	btrfs_btree_balance_dirty(fs_info);
	return ret;

out_inode:
	unlock_new_inode(inode);
	goto out;

}

static const struct inode_operations btrfs_dir_inode_operations = {
	.getattr	= btrfs_getattr,
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.link		= btrfs_link,
	.mkdir		= btrfs_mkdir,
	.rmdir		= btrfs_rmdir,
	.rename		= btrfs_rename2,
	.symlink	= btrfs_symlink,
	.setattr	= btrfs_setattr,
	.mknod		= btrfs_mknod,
	.listxattr	= btrfs_listxattr,
	.permission	= btrfs_permission,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
	.tmpfile        = btrfs_tmpfile,
};
static const struct inode_operations btrfs_dir_ro_inode_operations = {
	.lookup		= btrfs_lookup,
	.permission	= btrfs_permission,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
};

static const struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= btrfs_real_readdir,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_compat_ioctl,
#endif
	.release        = btrfs_release_file,
	.fsync		= btrfs_sync_file,
};

static const struct extent_io_ops btrfs_extent_io_ops = {
	.fill_delalloc = run_delalloc_range,
	.submit_bio_hook = btrfs_submit_bio_hook,
	.merge_bio_hook = btrfs_merge_bio_hook,
	.readpage_end_io_hook = btrfs_readpage_end_io_hook,
	.writepage_end_io_hook = btrfs_writepage_end_io_hook,
	.writepage_start_hook = btrfs_writepage_start_hook,
	.set_bit_hook = btrfs_set_bit_hook,
	.clear_bit_hook = btrfs_clear_bit_hook,
	.merge_extent_hook = btrfs_merge_extent_hook,
	.split_extent_hook = btrfs_split_extent_hook,
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
static const struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.writepages	= btrfs_writepages,
	.readpages	= btrfs_readpages,
	.direct_IO	= btrfs_direct_IO,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
	.set_page_dirty	= btrfs_set_page_dirty,
	.error_remove_page = generic_error_remove_page,
};

static const struct address_space_operations btrfs_symlink_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
};

static const struct inode_operations btrfs_file_inode_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.listxattr      = btrfs_listxattr,
	.permission	= btrfs_permission,
	.fiemap		= btrfs_fiemap,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
};
static const struct inode_operations btrfs_special_inode_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.permission	= btrfs_permission,
	.listxattr	= btrfs_listxattr,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
};
static const struct inode_operations btrfs_symlink_inode_operations = {
	.get_link	= page_get_link,
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.permission	= btrfs_permission,
	.listxattr	= btrfs_listxattr,
	.update_time	= btrfs_update_time,
};

const struct dentry_operations btrfs_dentry_operations = {
	.d_delete	= btrfs_dentry_delete,
	.d_release	= btrfs_dentry_release,
};
