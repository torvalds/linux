// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <crypto/hash.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>
#include <linux/compat.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/falloc.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/btrfs.h>
#include <linux/blkdev.h>
#include <linux/posix_acl_xattr.h>
#include <linux/uio.h>
#include <linux/magic.h>
#include <linux/iversion.h>
#include <linux/swap.h>
#include <linux/migrate.h>
#include <linux/sched/mm.h>
#include <linux/iomap.h>
#include <asm/unaligned.h>
#include <linux/fsverity.h>
#include "misc.h"
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
#include "props.h"
#include "qgroup.h"
#include "delalloc-space.h"
#include "block-group.h"
#include "space-info.h"
#include "zoned.h"
#include "subpage.h"

struct btrfs_iget_args {
	u64 ino;
	struct btrfs_root *root;
};

struct btrfs_dio_data {
	u64 reserve;
	loff_t length;
	ssize_t submitted;
	struct extent_changeset *data_reserved;
};

static const struct inode_operations btrfs_dir_inode_operations;
static const struct inode_operations btrfs_symlink_inode_operations;
static const struct inode_operations btrfs_special_inode_operations;
static const struct inode_operations btrfs_file_inode_operations;
static const struct address_space_operations btrfs_aops;
static const struct file_operations btrfs_dir_file_operations;

static struct kmem_cache *btrfs_inode_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_path_cachep;
struct kmem_cache *btrfs_free_space_cachep;
struct kmem_cache *btrfs_free_space_bitmap_cachep;

static int btrfs_setsize(struct inode *inode, struct iattr *attr);
static int btrfs_truncate(struct inode *inode, bool skip_writeback);
static int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered_extent);
static noinline int cow_file_range(struct btrfs_inode *inode,
				   struct page *locked_page,
				   u64 start, u64 end, int *page_started,
				   unsigned long *nr_written, int unlock);
static struct extent_map *create_io_em(struct btrfs_inode *inode, u64 start,
				       u64 len, u64 orig_start, u64 block_start,
				       u64 block_len, u64 orig_block_len,
				       u64 ram_bytes, int compress_type,
				       int type);

static void __endio_write_update_ordered(struct btrfs_inode *inode,
					 const u64 offset, const u64 bytes,
					 const bool uptodate);

/*
 * btrfs_inode_lock - lock inode i_rwsem based on arguments passed
 *
 * ilock_flags can have the following bit set:
 *
 * BTRFS_ILOCK_SHARED - acquire a shared lock on the inode
 * BTRFS_ILOCK_TRY - try to acquire the lock, if fails on first attempt
 *		     return -EAGAIN
 * BTRFS_ILOCK_MMAP - acquire a write lock on the i_mmap_lock
 */
int btrfs_inode_lock(struct inode *inode, unsigned int ilock_flags)
{
	if (ilock_flags & BTRFS_ILOCK_SHARED) {
		if (ilock_flags & BTRFS_ILOCK_TRY) {
			if (!inode_trylock_shared(inode))
				return -EAGAIN;
			else
				return 0;
		}
		inode_lock_shared(inode);
	} else {
		if (ilock_flags & BTRFS_ILOCK_TRY) {
			if (!inode_trylock(inode))
				return -EAGAIN;
			else
				return 0;
		}
		inode_lock(inode);
	}
	if (ilock_flags & BTRFS_ILOCK_MMAP)
		down_write(&BTRFS_I(inode)->i_mmap_lock);
	return 0;
}

/*
 * btrfs_inode_unlock - unock inode i_rwsem
 *
 * ilock_flags should contain the same bits set as passed to btrfs_inode_lock()
 * to decide whether the lock acquired is shared or exclusive.
 */
void btrfs_inode_unlock(struct inode *inode, unsigned int ilock_flags)
{
	if (ilock_flags & BTRFS_ILOCK_MMAP)
		up_write(&BTRFS_I(inode)->i_mmap_lock);
	if (ilock_flags & BTRFS_ILOCK_SHARED)
		inode_unlock_shared(inode);
	else
		inode_unlock(inode);
}

/*
 * Cleanup all submitted ordered extents in specified range to handle errors
 * from the btrfs_run_delalloc_range() callback.
 *
 * NOTE: caller must ensure that when an error happens, it can not call
 * extent_clear_unlock_delalloc() to clear both the bits EXTENT_DO_ACCOUNTING
 * and EXTENT_DELALLOC simultaneously, because that causes the reserved metadata
 * to be released, which we want to happen only when finishing the ordered
 * extent (btrfs_finish_ordered_io()).
 */
static inline void btrfs_cleanup_ordered_extents(struct btrfs_inode *inode,
						 struct page *locked_page,
						 u64 offset, u64 bytes)
{
	unsigned long index = offset >> PAGE_SHIFT;
	unsigned long end_index = (offset + bytes - 1) >> PAGE_SHIFT;
	u64 page_start = page_offset(locked_page);
	u64 page_end = page_start + PAGE_SIZE - 1;

	struct page *page;

	while (index <= end_index) {
		/*
		 * For locked page, we will call end_extent_writepage() on it
		 * in run_delalloc_range() for the error handling.  That
		 * end_extent_writepage() function will call
		 * btrfs_mark_ordered_io_finished() to clear page Ordered and
		 * run the ordered extent accounting.
		 *
		 * Here we can't just clear the Ordered bit, or
		 * btrfs_mark_ordered_io_finished() would skip the accounting
		 * for the page range, and the ordered extent will never finish.
		 */
		if (index == (page_offset(locked_page) >> PAGE_SHIFT)) {
			index++;
			continue;
		}
		page = find_get_page(inode->vfs_inode.i_mapping, index);
		index++;
		if (!page)
			continue;

		/*
		 * Here we just clear all Ordered bits for every page in the
		 * range, then __endio_write_update_ordered() will handle
		 * the ordered extent accounting for the range.
		 */
		btrfs_page_clamp_clear_ordered(inode->root->fs_info, page,
					       offset, bytes);
		put_page(page);
	}

	/* The locked page covers the full range, nothing needs to be done */
	if (bytes + offset <= page_offset(locked_page) + PAGE_SIZE)
		return;
	/*
	 * In case this page belongs to the delalloc range being instantiated
	 * then skip it, since the first page of a range is going to be
	 * properly cleaned up by the caller of run_delalloc_range
	 */
	if (page_start >= offset && page_end <= (offset + bytes - 1)) {
		bytes = offset + bytes - page_offset(locked_page) - PAGE_SIZE;
		offset = page_offset(locked_page) + PAGE_SIZE;
	}

	return __endio_write_update_ordered(inode, offset, bytes, false);
}

static int btrfs_dirty_inode(struct inode *inode);

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
				struct btrfs_path *path, bool extent_inserted,
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
	int ret;
	size_t cur_size = size;
	unsigned long offset;

	ASSERT((compressed_size > 0 && compressed_pages) ||
	       (compressed_size == 0 && !compressed_pages));

	if (compressed_size && compressed_pages)
		cur_size = compressed_size;

	if (!extent_inserted) {
		struct btrfs_key key;
		size_t datasize;

		key.objectid = btrfs_ino(BTRFS_I(inode));
		key.offset = start;
		key.type = BTRFS_EXTENT_DATA_KEY;

		datasize = btrfs_file_extent_calc_inline_size(cur_size);
		ret = btrfs_insert_empty_item(trans, root, path, &key,
					      datasize);
		if (ret)
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
		offset = offset_in_page(start);
		write_extent_buffer(leaf, kaddr + offset, ptr, size);
		kunmap_atomic(kaddr);
		put_page(page);
	}
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	/*
	 * We align size to sectorsize for inline extents just for simplicity
	 * sake.
	 */
	size = ALIGN(size, root->fs_info->sectorsize);
	ret = btrfs_inode_set_file_extent_range(BTRFS_I(inode), start, size);
	if (ret)
		goto fail;

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
fail:
	return ret;
}


/*
 * conditionally insert an inline extent into the file.  This
 * does the checks required to make sure the data is small enough
 * to fit as an inline extent.
 */
static noinline int cow_file_range_inline(struct btrfs_inode *inode, u64 start,
					  u64 end, size_t compressed_size,
					  int compress_type,
					  struct page **compressed_pages)
{
	struct btrfs_drop_extents_args drop_args = { 0 };
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	u64 isize = i_size_read(&inode->vfs_inode);
	u64 actual_end = min(end + 1, isize);
	u64 inline_len = actual_end - start;
	u64 aligned_end = ALIGN(end, fs_info->sectorsize);
	u64 data_len = inline_len;
	int ret;
	struct btrfs_path *path;

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
	trans->block_rsv = &inode->block_rsv;

	drop_args.path = path;
	drop_args.start = start;
	drop_args.end = aligned_end;
	drop_args.drop_cache = true;
	drop_args.replace_extent = true;

	if (compressed_size && compressed_pages)
		drop_args.extent_item_size = btrfs_file_extent_calc_inline_size(
		   compressed_size);
	else
		drop_args.extent_item_size = btrfs_file_extent_calc_inline_size(
		    inline_len);

	ret = btrfs_drop_extents(trans, root, inode, &drop_args);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	if (isize > actual_end)
		inline_len = min_t(u64, isize, actual_end);
	ret = insert_inline_extent(trans, path, drop_args.extent_inserted,
				   root, &inode->vfs_inode, start,
				   inline_len, compressed_size,
				   compress_type, compressed_pages);
	if (ret && ret != -ENOSPC) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else if (ret == -ENOSPC) {
		ret = 1;
		goto out;
	}

	btrfs_update_inode_bytes(inode, inline_len, drop_args.bytes_found);
	ret = btrfs_update_inode(trans, root, inode);
	if (ret && ret != -ENOSPC) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else if (ret == -ENOSPC) {
		ret = 1;
		goto out;
	}

	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &inode->runtime_flags);
out:
	/*
	 * Don't forget to free the reserved space, as for inlined extent
	 * it won't count as data extent, free them directly here.
	 * And at reserve time, it's always aligned to page size, so
	 * just free one page here.
	 */
	btrfs_qgroup_free_data(inode, NULL, 0, PAGE_SIZE);
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

struct async_chunk {
	struct inode *inode;
	struct page *locked_page;
	u64 start;
	u64 end;
	unsigned int write_flags;
	struct list_head extents;
	struct cgroup_subsys_state *blkcg_css;
	struct btrfs_work work;
	atomic_t *pending;
};

struct async_cow {
	/* Number of chunks in flight; must be first in the structure */
	atomic_t num_chunks;
	struct async_chunk chunks[];
};

static noinline int add_async_extent(struct async_chunk *cow,
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

/*
 * Check if the inode has flags compatible with compression
 */
static inline bool inode_can_compress(struct btrfs_inode *inode)
{
	/* Subpage doesn't support compression yet */
	if (inode->root->fs_info->sectorsize < PAGE_SIZE)
		return false;
	if (inode->flags & BTRFS_INODE_NODATACOW ||
	    inode->flags & BTRFS_INODE_NODATASUM)
		return false;
	return true;
}

/*
 * Check if the inode needs to be submitted to compression, based on mount
 * options, defragmentation, properties or heuristics.
 */
static inline int inode_need_compress(struct btrfs_inode *inode, u64 start,
				      u64 end)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	if (!inode_can_compress(inode)) {
		WARN(IS_ENABLED(CONFIG_BTRFS_DEBUG),
			KERN_ERR "BTRFS: unexpected compression for ino %llu\n",
			btrfs_ino(inode));
		return 0;
	}
	/* force compress */
	if (btrfs_test_opt(fs_info, FORCE_COMPRESS))
		return 1;
	/* defrag ioctl */
	if (inode->defrag_compress)
		return 1;
	/* bad compression ratios */
	if (inode->flags & BTRFS_INODE_NOCOMPRESS)
		return 0;
	if (btrfs_test_opt(fs_info, COMPRESS) ||
	    inode->flags & BTRFS_INODE_COMPRESS ||
	    inode->prop_compress)
		return btrfs_compress_heuristic(&inode->vfs_inode, start, end);
	return 0;
}

static inline void inode_should_defrag(struct btrfs_inode *inode,
		u64 start, u64 end, u64 num_bytes, u64 small_write)
{
	/* If this is a small write inside eof, kick off a defrag */
	if (num_bytes < small_write &&
	    (start > 0 || end + 1 < inode->disk_i_size))
		btrfs_add_inode_defrag(NULL, inode);
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
static noinline int compress_file_range(struct async_chunk *async_chunk)
{
	struct inode *inode = async_chunk->inode;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 blocksize = fs_info->sectorsize;
	u64 start = async_chunk->start;
	u64 end = async_chunk->end;
	u64 actual_end;
	u64 i_size;
	int ret = 0;
	struct page **pages = NULL;
	unsigned long nr_pages;
	unsigned long total_compressed = 0;
	unsigned long total_in = 0;
	int i;
	int will_compress;
	int compress_type = fs_info->compress_type;
	int compressed_extents = 0;
	int redirty = 0;

	inode_should_defrag(BTRFS_I(inode), start, end, end - start + 1,
			SZ_16K);

	/*
	 * We need to save i_size before now because it could change in between
	 * us evaluating the size and assigning it.  This is because we lock and
	 * unlock the page in truncate and fallocate, and then modify the i_size
	 * later on.
	 *
	 * The barriers are to emulate READ_ONCE, remove that once i_size_read
	 * does that for us.
	 */
	barrier();
	i_size = i_size_read(inode);
	barrier();
	actual_end = min_t(u64, i_size, end + 1);
again:
	will_compress = 0;
	nr_pages = (end >> PAGE_SHIFT) - (start >> PAGE_SHIFT) + 1;
	BUILD_BUG_ON((BTRFS_MAX_COMPRESSED % PAGE_SIZE) != 0);
	nr_pages = min_t(unsigned long, nr_pages,
			BTRFS_MAX_COMPRESSED / PAGE_SIZE);

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

	total_compressed = min_t(unsigned long, total_compressed,
			BTRFS_MAX_UNCOMPRESSED);
	total_in = 0;
	ret = 0;

	/*
	 * we do compression for mount -o compress and when the
	 * inode has not been flagged as nocompress.  This flag can
	 * change at any time if we discover bad compression ratios.
	 */
	if (inode_need_compress(BTRFS_I(inode), start, end)) {
		WARN_ON(pages);
		pages = kcalloc(nr_pages, sizeof(struct page *), GFP_NOFS);
		if (!pages) {
			/* just bail out to the uncompressed code */
			nr_pages = 0;
			goto cont;
		}

		if (BTRFS_I(inode)->defrag_compress)
			compress_type = BTRFS_I(inode)->defrag_compress;
		else if (BTRFS_I(inode)->prop_compress)
			compress_type = BTRFS_I(inode)->prop_compress;

		/*
		 * we need to call clear_page_dirty_for_io on each
		 * page in the range.  Otherwise applications with the file
		 * mmap'd can wander in and change the page contents while
		 * we are compressing them.
		 *
		 * If the compression fails for any reason, we set the pages
		 * dirty again later on.
		 *
		 * Note that the remaining part is redirtied, the start pointer
		 * has moved, the end is the original one.
		 */
		if (!redirty) {
			extent_range_clear_dirty_for_io(inode, start, end);
			redirty = 1;
		}

		/* Compression level is applied here and only here */
		ret = btrfs_compress_pages(
			compress_type | (fs_info->compress_level << 4),
					   inode->i_mapping, start,
					   pages,
					   &nr_pages,
					   &total_in,
					   &total_compressed);

		if (!ret) {
			unsigned long offset = offset_in_page(total_compressed);
			struct page *page = pages[nr_pages - 1];

			/* zero the tail end of the last page, we might be
			 * sending it down to disk
			 */
			if (offset)
				memzero_page(page, offset, PAGE_SIZE - offset);
			will_compress = 1;
		}
	}
cont:
	/*
	 * Check cow_file_range() for why we don't even try to create inline
	 * extent for subpage case.
	 */
	if (start == 0 && fs_info->sectorsize == PAGE_SIZE) {
		/* lets try to make an inline extent */
		if (ret || total_in < actual_end) {
			/* we didn't compress the entire range, try
			 * to make an uncompressed inline extent.
			 */
			ret = cow_file_range_inline(BTRFS_I(inode), start, end,
						    0, BTRFS_COMPRESS_NONE,
						    NULL);
		} else {
			/* try making a compressed inline extent */
			ret = cow_file_range_inline(BTRFS_I(inode), start, end,
						    total_compressed,
						    compress_type, pages);
		}
		if (ret <= 0) {
			unsigned long clear_flags = EXTENT_DELALLOC |
				EXTENT_DELALLOC_NEW | EXTENT_DEFRAG |
				EXTENT_DO_ACCOUNTING;
			unsigned long page_error_op;

			page_error_op = ret < 0 ? PAGE_SET_ERROR : 0;

			/*
			 * inline extent creation worked or returned error,
			 * we don't need to create any more async work items.
			 * Unlock and free up our temp pages.
			 *
			 * We use DO_ACCOUNTING here because we need the
			 * delalloc_release_metadata to be done _after_ we drop
			 * our outstanding extent for clearing delalloc for this
			 * range.
			 */
			extent_clear_unlock_delalloc(BTRFS_I(inode), start, end,
						     NULL,
						     clear_flags,
						     PAGE_UNLOCK |
						     PAGE_START_WRITEBACK |
						     page_error_op |
						     PAGE_END_WRITEBACK);

			/*
			 * Ensure we only free the compressed pages if we have
			 * them allocated, as we can still reach here with
			 * inode_need_compress() == false.
			 */
			if (pages) {
				for (i = 0; i < nr_pages; i++) {
					WARN_ON(pages[i]->mapping);
					put_page(pages[i]);
				}
				kfree(pages);
			}
			return 0;
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
		 * win, compare the page count read with the blocks on disk,
		 * compression must free at least one sector size
		 */
		total_in = ALIGN(total_in, PAGE_SIZE);
		if (total_compressed + blocksize <= total_in) {
			compressed_extents++;

			/*
			 * The async work queues will take care of doing actual
			 * allocation on disk for these compressed pages, and
			 * will submit them to the elevator.
			 */
			add_async_extent(async_chunk, start, total_in,
					total_compressed, pages, nr_pages,
					compress_type);

			if (start + total_in < end) {
				start += total_in;
				pages = NULL;
				cond_resched();
				goto again;
			}
			return compressed_extents;
		}
	}
	if (pages) {
		/*
		 * the compression code ran but failed to make things smaller,
		 * free any pages it allocated and our page pointer array
		 */
		for (i = 0; i < nr_pages; i++) {
			WARN_ON(pages[i]->mapping);
			put_page(pages[i]);
		}
		kfree(pages);
		pages = NULL;
		total_compressed = 0;
		nr_pages = 0;

		/* flag the file so we don't compress in the future */
		if (!btrfs_test_opt(fs_info, FORCE_COMPRESS) &&
		    !(BTRFS_I(inode)->prop_compress)) {
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
	if (async_chunk->locked_page &&
	    (page_offset(async_chunk->locked_page) >= start &&
	     page_offset(async_chunk->locked_page)) <= end) {
		__set_page_dirty_nobuffers(async_chunk->locked_page);
		/* unlocked later on in the async handlers */
	}

	if (redirty)
		extent_range_redirty_for_io(inode, start, end);
	add_async_extent(async_chunk, start, end - start + 1, 0, NULL, 0,
			 BTRFS_COMPRESS_NONE);
	compressed_extents++;

	return compressed_extents;
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
static noinline void submit_compressed_extents(struct async_chunk *async_chunk)
{
	struct btrfs_inode *inode = BTRFS_I(async_chunk->inode);
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct async_extent *async_extent;
	u64 alloc_hint = 0;
	struct btrfs_key ins;
	struct extent_map *em;
	struct btrfs_root *root = inode->root;
	struct extent_io_tree *io_tree = &inode->io_tree;
	int ret = 0;

again:
	while (!list_empty(&async_chunk->extents)) {
		async_extent = list_entry(async_chunk->extents.next,
					  struct async_extent, list);
		list_del(&async_extent->list);

retry:
		lock_extent(io_tree, async_extent->start,
			    async_extent->start + async_extent->ram_size - 1);
		/* did the compression code fall back to uncompressed IO? */
		if (!async_extent->pages) {
			int page_started = 0;
			unsigned long nr_written = 0;

			/* allocate blocks */
			ret = cow_file_range(inode, async_chunk->locked_page,
					     async_extent->start,
					     async_extent->start +
					     async_extent->ram_size - 1,
					     &page_started, &nr_written, 0);

			/* JDM XXX */

			/*
			 * if page_started, cow_file_range inserted an
			 * inline extent and took care of all the unlocking
			 * and IO for us.  Otherwise, we need to submit
			 * all those pages down to the drive.
			 */
			if (!page_started && !ret)
				extent_write_locked_range(&inode->vfs_inode,
						  async_extent->start,
						  async_extent->start +
						  async_extent->ram_size - 1,
						  WB_SYNC_ALL);
			else if (ret && async_chunk->locked_page)
				unlock_page(async_chunk->locked_page);
			kfree(async_extent);
			cond_resched();
			continue;
		}

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
				extent_range_redirty_for_io(&inode->vfs_inode,
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
		em = create_io_em(inode, async_extent->start,
				  async_extent->ram_size, /* len */
				  async_extent->start, /* orig_start */
				  ins.objectid, /* block_start */
				  ins.offset, /* block_len */
				  ins.offset, /* orig_block_len */
				  async_extent->ram_size, /* ram_bytes */
				  async_extent->compress_type,
				  BTRFS_ORDERED_COMPRESSED);
		if (IS_ERR(em))
			/* ret value is not necessary due to void function */
			goto out_free_reserve;
		free_extent_map(em);

		ret = btrfs_add_ordered_extent_compress(inode,
						async_extent->start,
						ins.objectid,
						async_extent->ram_size,
						ins.offset,
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
				NULL, EXTENT_LOCKED | EXTENT_DELALLOC,
				PAGE_UNLOCK | PAGE_START_WRITEBACK);
		if (btrfs_submit_compressed_write(inode, async_extent->start,
				    async_extent->ram_size,
				    ins.objectid,
				    ins.offset, async_extent->pages,
				    async_extent->nr_pages,
				    async_chunk->write_flags,
				    async_chunk->blkcg_css)) {
			struct page *p = async_extent->pages[0];
			const u64 start = async_extent->start;
			const u64 end = start + async_extent->ram_size - 1;

			p->mapping = inode->vfs_inode.i_mapping;
			btrfs_writepage_endio_finish_ordered(inode, p, start,
							     end, false);

			p->mapping = NULL;
			extent_clear_unlock_delalloc(inode, start, end, NULL, 0,
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
				     NULL, EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DELALLOC_NEW |
				     EXTENT_DEFRAG | EXTENT_DO_ACCOUNTING,
				     PAGE_UNLOCK | PAGE_START_WRITEBACK |
				     PAGE_END_WRITEBACK | PAGE_SET_ERROR);
	free_async_extent_pages(async_extent);
	kfree(async_extent);
	goto again;
}

static u64 get_extent_allocation_hint(struct btrfs_inode *inode, u64 start,
				      u64 num_bytes)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
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
static noinline int cow_file_range(struct btrfs_inode *inode,
				   struct page *locked_page,
				   u64 start, u64 end, int *page_started,
				   unsigned long *nr_written, int unlock)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 alloc_hint = 0;
	u64 num_bytes;
	unsigned long ram_size;
	u64 cur_alloc_size = 0;
	u64 min_alloc_size;
	u64 blocksize = fs_info->sectorsize;
	struct btrfs_key ins;
	struct extent_map *em;
	unsigned clear_bits;
	unsigned long page_ops;
	bool extent_reserved = false;
	int ret = 0;

	if (btrfs_is_free_space_inode(inode)) {
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		goto out_unlock;
	}

	num_bytes = ALIGN(end - start + 1, blocksize);
	num_bytes = max(blocksize,  num_bytes);
	ASSERT(num_bytes <= btrfs_super_total_bytes(fs_info->super_copy));

	inode_should_defrag(inode, start, end, num_bytes, SZ_64K);

	/*
	 * Due to the page size limit, for subpage we can only trigger the
	 * writeback for the dirty sectors of page, that means data writeback
	 * is doing more writeback than what we want.
	 *
	 * This is especially unexpected for some call sites like fallocate,
	 * where we only increase i_size after everything is done.
	 * This means we can trigger inline extent even if we didn't want to.
	 * So here we skip inline extent creation completely.
	 */
	if (start == 0 && fs_info->sectorsize == PAGE_SIZE) {
		/* lets try to make an inline extent */
		ret = cow_file_range_inline(inode, start, end, 0,
					    BTRFS_COMPRESS_NONE, NULL);
		if (ret == 0) {
			/*
			 * We use DO_ACCOUNTING here because we need the
			 * delalloc_release_metadata to be run _after_ we drop
			 * our outstanding extent for clearing delalloc for this
			 * range.
			 */
			extent_clear_unlock_delalloc(inode, start, end,
				     locked_page,
				     EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DELALLOC_NEW | EXTENT_DEFRAG |
				     EXTENT_DO_ACCOUNTING, PAGE_UNLOCK |
				     PAGE_START_WRITEBACK | PAGE_END_WRITEBACK);
			*nr_written = *nr_written +
			     (end - start + PAGE_SIZE) / PAGE_SIZE;
			*page_started = 1;
			/*
			 * locked_page is locked by the caller of
			 * writepage_delalloc(), not locked by
			 * __process_pages_contig().
			 *
			 * We can't let __process_pages_contig() to unlock it,
			 * as it doesn't have any subpage::writers recorded.
			 *
			 * Here we manually unlock the page, since the caller
			 * can't use page_started to determine if it's an
			 * inline extent or a compressed extent.
			 */
			unlock_page(locked_page);
			goto out;
		} else if (ret < 0) {
			goto out_unlock;
		}
	}

	alloc_hint = get_extent_allocation_hint(inode, start, num_bytes);
	btrfs_drop_extent_cache(inode, start, start + num_bytes - 1, 0);

	/*
	 * Relocation relies on the relocated extents to have exactly the same
	 * size as the original extents. Normally writeback for relocation data
	 * extents follows a NOCOW path because relocation preallocates the
	 * extents. However, due to an operation such as scrub turning a block
	 * group to RO mode, it may fallback to COW mode, so we must make sure
	 * an extent allocated during COW has exactly the requested size and can
	 * not be split into smaller extents, otherwise relocation breaks and
	 * fails during the stage where it updates the bytenr of file extent
	 * items.
	 */
	if (btrfs_is_data_reloc_root(root))
		min_alloc_size = num_bytes;
	else
		min_alloc_size = fs_info->sectorsize;

	while (num_bytes > 0) {
		cur_alloc_size = num_bytes;
		ret = btrfs_reserve_extent(root, cur_alloc_size, cur_alloc_size,
					   min_alloc_size, 0, alloc_hint,
					   &ins, 1, 1);
		if (ret < 0)
			goto out_unlock;
		cur_alloc_size = ins.offset;
		extent_reserved = true;

		ram_size = ins.offset;
		em = create_io_em(inode, start, ins.offset, /* len */
				  start, /* orig_start */
				  ins.objectid, /* block_start */
				  ins.offset, /* block_len */
				  ins.offset, /* orig_block_len */
				  ram_size, /* ram_bytes */
				  BTRFS_COMPRESS_NONE, /* compress_type */
				  BTRFS_ORDERED_REGULAR /* type */);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out_reserve;
		}
		free_extent_map(em);

		ret = btrfs_add_ordered_extent(inode, start, ins.objectid,
					       ram_size, cur_alloc_size,
					       BTRFS_ORDERED_REGULAR);
		if (ret)
			goto out_drop_extent_cache;

		if (btrfs_is_data_reloc_root(root)) {
			ret = btrfs_reloc_clone_csums(inode, start,
						      cur_alloc_size);
			/*
			 * Only drop cache here, and process as normal.
			 *
			 * We must not allow extent_clear_unlock_delalloc()
			 * at out_unlock label to free meta of this ordered
			 * extent, as its meta should be freed by
			 * btrfs_finish_ordered_io().
			 *
			 * So we must continue until @start is increased to
			 * skip current ordered extent.
			 */
			if (ret)
				btrfs_drop_extent_cache(inode, start,
						start + ram_size - 1, 0);
		}

		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		/*
		 * We're not doing compressed IO, don't unlock the first page
		 * (which the caller expects to stay locked), don't clear any
		 * dirty bits and don't set any writeback bits
		 *
		 * Do set the Ordered (Private2) bit so we know this page was
		 * properly setup for writepage.
		 */
		page_ops = unlock ? PAGE_UNLOCK : 0;
		page_ops |= PAGE_SET_ORDERED;

		extent_clear_unlock_delalloc(inode, start, start + ram_size - 1,
					     locked_page,
					     EXTENT_LOCKED | EXTENT_DELALLOC,
					     page_ops);
		if (num_bytes < cur_alloc_size)
			num_bytes = 0;
		else
			num_bytes -= cur_alloc_size;
		alloc_hint = ins.objectid + ins.offset;
		start += cur_alloc_size;
		extent_reserved = false;

		/*
		 * btrfs_reloc_clone_csums() error, since start is increased
		 * extent_clear_unlock_delalloc() at out_unlock label won't
		 * free metadata of current ordered extent, we're OK to exit.
		 */
		if (ret)
			goto out_unlock;
	}
out:
	return ret;

out_drop_extent_cache:
	btrfs_drop_extent_cache(inode, start, start + ram_size - 1, 0);
out_reserve:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
out_unlock:
	clear_bits = EXTENT_LOCKED | EXTENT_DELALLOC | EXTENT_DELALLOC_NEW |
		EXTENT_DEFRAG | EXTENT_CLEAR_META_RESV;
	page_ops = PAGE_UNLOCK | PAGE_START_WRITEBACK | PAGE_END_WRITEBACK;
	/*
	 * If we reserved an extent for our delalloc range (or a subrange) and
	 * failed to create the respective ordered extent, then it means that
	 * when we reserved the extent we decremented the extent's size from
	 * the data space_info's bytes_may_use counter and incremented the
	 * space_info's bytes_reserved counter by the same amount. We must make
	 * sure extent_clear_unlock_delalloc() does not try to decrement again
	 * the data space_info's bytes_may_use counter, therefore we do not pass
	 * it the flag EXTENT_CLEAR_DATA_RESV.
	 */
	if (extent_reserved) {
		extent_clear_unlock_delalloc(inode, start,
					     start + cur_alloc_size - 1,
					     locked_page,
					     clear_bits,
					     page_ops);
		start += cur_alloc_size;
		if (start >= end)
			goto out;
	}
	extent_clear_unlock_delalloc(inode, start, end, locked_page,
				     clear_bits | EXTENT_CLEAR_DATA_RESV,
				     page_ops);
	goto out;
}

/*
 * work queue call back to started compression on a file and pages
 */
static noinline void async_cow_start(struct btrfs_work *work)
{
	struct async_chunk *async_chunk;
	int compressed_extents;

	async_chunk = container_of(work, struct async_chunk, work);

	compressed_extents = compress_file_range(async_chunk);
	if (compressed_extents == 0) {
		btrfs_add_delayed_iput(async_chunk->inode);
		async_chunk->inode = NULL;
	}
}

/*
 * work queue call back to submit previously compressed pages
 */
static noinline void async_cow_submit(struct btrfs_work *work)
{
	struct async_chunk *async_chunk = container_of(work, struct async_chunk,
						     work);
	struct btrfs_fs_info *fs_info = btrfs_work_owner(work);
	unsigned long nr_pages;

	nr_pages = (async_chunk->end - async_chunk->start + PAGE_SIZE) >>
		PAGE_SHIFT;

	/*
	 * ->inode could be NULL if async_chunk_start has failed to compress,
	 * in which case we don't have anything to submit, yet we need to
	 * always adjust ->async_delalloc_pages as its paired with the init
	 * happening in cow_file_range_async
	 */
	if (async_chunk->inode)
		submit_compressed_extents(async_chunk);

	/* atomic_sub_return implies a barrier */
	if (atomic_sub_return(nr_pages, &fs_info->async_delalloc_pages) <
	    5 * SZ_1M)
		cond_wake_up_nomb(&fs_info->async_submit_wait);
}

static noinline void async_cow_free(struct btrfs_work *work)
{
	struct async_chunk *async_chunk;

	async_chunk = container_of(work, struct async_chunk, work);
	if (async_chunk->inode)
		btrfs_add_delayed_iput(async_chunk->inode);
	if (async_chunk->blkcg_css)
		css_put(async_chunk->blkcg_css);
	/*
	 * Since the pointer to 'pending' is at the beginning of the array of
	 * async_chunk's, freeing it ensures the whole array has been freed.
	 */
	if (atomic_dec_and_test(async_chunk->pending))
		kvfree(async_chunk->pending);
}

static int cow_file_range_async(struct btrfs_inode *inode,
				struct writeback_control *wbc,
				struct page *locked_page,
				u64 start, u64 end, int *page_started,
				unsigned long *nr_written)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct cgroup_subsys_state *blkcg_css = wbc_blkcg_css(wbc);
	struct async_cow *ctx;
	struct async_chunk *async_chunk;
	unsigned long nr_pages;
	u64 cur_end;
	u64 num_chunks = DIV_ROUND_UP(end - start, SZ_512K);
	int i;
	bool should_compress;
	unsigned nofs_flag;
	const unsigned int write_flags = wbc_to_write_flags(wbc);

	unlock_extent(&inode->io_tree, start, end);

	if (inode->flags & BTRFS_INODE_NOCOMPRESS &&
	    !btrfs_test_opt(fs_info, FORCE_COMPRESS)) {
		num_chunks = 1;
		should_compress = false;
	} else {
		should_compress = true;
	}

	nofs_flag = memalloc_nofs_save();
	ctx = kvmalloc(struct_size(ctx, chunks, num_chunks), GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);

	if (!ctx) {
		unsigned clear_bits = EXTENT_LOCKED | EXTENT_DELALLOC |
			EXTENT_DELALLOC_NEW | EXTENT_DEFRAG |
			EXTENT_DO_ACCOUNTING;
		unsigned long page_ops = PAGE_UNLOCK | PAGE_START_WRITEBACK |
					 PAGE_END_WRITEBACK | PAGE_SET_ERROR;

		extent_clear_unlock_delalloc(inode, start, end, locked_page,
					     clear_bits, page_ops);
		return -ENOMEM;
	}

	async_chunk = ctx->chunks;
	atomic_set(&ctx->num_chunks, num_chunks);

	for (i = 0; i < num_chunks; i++) {
		if (should_compress)
			cur_end = min(end, start + SZ_512K - 1);
		else
			cur_end = end;

		/*
		 * igrab is called higher up in the call chain, take only the
		 * lightweight reference for the callback lifetime
		 */
		ihold(&inode->vfs_inode);
		async_chunk[i].pending = &ctx->num_chunks;
		async_chunk[i].inode = &inode->vfs_inode;
		async_chunk[i].start = start;
		async_chunk[i].end = cur_end;
		async_chunk[i].write_flags = write_flags;
		INIT_LIST_HEAD(&async_chunk[i].extents);

		/*
		 * The locked_page comes all the way from writepage and its
		 * the original page we were actually given.  As we spread
		 * this large delalloc region across multiple async_chunk
		 * structs, only the first struct needs a pointer to locked_page
		 *
		 * This way we don't need racey decisions about who is supposed
		 * to unlock it.
		 */
		if (locked_page) {
			/*
			 * Depending on the compressibility, the pages might or
			 * might not go through async.  We want all of them to
			 * be accounted against wbc once.  Let's do it here
			 * before the paths diverge.  wbc accounting is used
			 * only for foreign writeback detection and doesn't
			 * need full accuracy.  Just account the whole thing
			 * against the first page.
			 */
			wbc_account_cgroup_owner(wbc, locked_page,
						 cur_end - start);
			async_chunk[i].locked_page = locked_page;
			locked_page = NULL;
		} else {
			async_chunk[i].locked_page = NULL;
		}

		if (blkcg_css != blkcg_root_css) {
			css_get(blkcg_css);
			async_chunk[i].blkcg_css = blkcg_css;
		} else {
			async_chunk[i].blkcg_css = NULL;
		}

		btrfs_init_work(&async_chunk[i].work, async_cow_start,
				async_cow_submit, async_cow_free);

		nr_pages = DIV_ROUND_UP(cur_end - start, PAGE_SIZE);
		atomic_add(nr_pages, &fs_info->async_delalloc_pages);

		btrfs_queue_work(fs_info->delalloc_workers, &async_chunk[i].work);

		*nr_written += nr_pages;
		start = cur_end + 1;
	}
	*page_started = 1;
	return 0;
}

static noinline int run_delalloc_zoned(struct btrfs_inode *inode,
				       struct page *locked_page, u64 start,
				       u64 end, int *page_started,
				       unsigned long *nr_written)
{
	int ret;

	ret = cow_file_range(inode, locked_page, start, end, page_started,
			     nr_written, 0);
	if (ret)
		return ret;

	if (*page_started)
		return 0;

	__set_page_dirty_nobuffers(locked_page);
	account_page_redirty(locked_page);
	extent_write_locked_range(&inode->vfs_inode, start, end, WB_SYNC_ALL);
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
	if (ret < 0)
		return ret;
	return 1;
}

static int fallback_to_cow(struct btrfs_inode *inode, struct page *locked_page,
			   const u64 start, const u64 end,
			   int *page_started, unsigned long *nr_written)
{
	const bool is_space_ino = btrfs_is_free_space_inode(inode);
	const bool is_reloc_ino = btrfs_is_data_reloc_root(inode->root);
	const u64 range_bytes = end + 1 - start;
	struct extent_io_tree *io_tree = &inode->io_tree;
	u64 range_start = start;
	u64 count;

	/*
	 * If EXTENT_NORESERVE is set it means that when the buffered write was
	 * made we had not enough available data space and therefore we did not
	 * reserve data space for it, since we though we could do NOCOW for the
	 * respective file range (either there is prealloc extent or the inode
	 * has the NOCOW bit set).
	 *
	 * However when we need to fallback to COW mode (because for example the
	 * block group for the corresponding extent was turned to RO mode by a
	 * scrub or relocation) we need to do the following:
	 *
	 * 1) We increment the bytes_may_use counter of the data space info.
	 *    If COW succeeds, it allocates a new data extent and after doing
	 *    that it decrements the space info's bytes_may_use counter and
	 *    increments its bytes_reserved counter by the same amount (we do
	 *    this at btrfs_add_reserved_bytes()). So we need to increment the
	 *    bytes_may_use counter to compensate (when space is reserved at
	 *    buffered write time, the bytes_may_use counter is incremented);
	 *
	 * 2) We clear the EXTENT_NORESERVE bit from the range. We do this so
	 *    that if the COW path fails for any reason, it decrements (through
	 *    extent_clear_unlock_delalloc()) the bytes_may_use counter of the
	 *    data space info, which we incremented in the step above.
	 *
	 * If we need to fallback to cow and the inode corresponds to a free
	 * space cache inode or an inode of the data relocation tree, we must
	 * also increment bytes_may_use of the data space_info for the same
	 * reason. Space caches and relocated data extents always get a prealloc
	 * extent for them, however scrub or balance may have set the block
	 * group that contains that extent to RO mode and therefore force COW
	 * when starting writeback.
	 */
	count = count_range_bits(io_tree, &range_start, end, range_bytes,
				 EXTENT_NORESERVE, 0);
	if (count > 0 || is_space_ino || is_reloc_ino) {
		u64 bytes = count;
		struct btrfs_fs_info *fs_info = inode->root->fs_info;
		struct btrfs_space_info *sinfo = fs_info->data_sinfo;

		if (is_space_ino || is_reloc_ino)
			bytes = range_bytes;

		spin_lock(&sinfo->lock);
		btrfs_space_info_update_bytes_may_use(fs_info, sinfo, bytes);
		spin_unlock(&sinfo->lock);

		if (count > 0)
			clear_extent_bit(io_tree, start, end, EXTENT_NORESERVE,
					 0, 0, NULL);
	}

	return cow_file_range(inode, locked_page, start, end, page_started,
			      nr_written, 1);
}

/*
 * when nowcow writeback call back.  This checks for snapshots or COW copies
 * of the extents that exist in the file, and COWs the file as required.
 *
 * If no cow copies or snapshots exist, we write directly to the existing
 * blocks on disk
 */
static noinline int run_delalloc_nocow(struct btrfs_inode *inode,
				       struct page *locked_page,
				       const u64 start, const u64 end,
				       int *page_started,
				       unsigned long *nr_written)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_root *root = inode->root;
	struct btrfs_path *path;
	u64 cow_start = (u64)-1;
	u64 cur_offset = start;
	int ret;
	bool check_prev = true;
	const bool freespace_inode = btrfs_is_free_space_inode(inode);
	u64 ino = btrfs_ino(inode);
	bool nocow = false;
	u64 disk_bytenr = 0;
	const bool force = inode->flags & BTRFS_INODE_NODATACOW;

	path = btrfs_alloc_path();
	if (!path) {
		extent_clear_unlock_delalloc(inode, start, end, locked_page,
					     EXTENT_LOCKED | EXTENT_DELALLOC |
					     EXTENT_DO_ACCOUNTING |
					     EXTENT_DEFRAG, PAGE_UNLOCK |
					     PAGE_START_WRITEBACK |
					     PAGE_END_WRITEBACK);
		return -ENOMEM;
	}

	while (1) {
		struct btrfs_key found_key;
		struct btrfs_file_extent_item *fi;
		struct extent_buffer *leaf;
		u64 extent_end;
		u64 extent_offset;
		u64 num_bytes = 0;
		u64 disk_num_bytes;
		u64 ram_bytes;
		int extent_type;

		nocow = false;

		ret = btrfs_lookup_file_extent(NULL, root, path, ino,
					       cur_offset, 0);
		if (ret < 0)
			goto error;

		/*
		 * If there is no extent for our range when doing the initial
		 * search, then go back to the previous slot as it will be the
		 * one containing the search offset
		 */
		if (ret > 0 && path->slots[0] > 0 && check_prev) {
			leaf = path->nodes[0];
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0] - 1);
			if (found_key.objectid == ino &&
			    found_key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}
		check_prev = false;
next_slot:
		/* Go to next leaf if we have exhausted the current one */
		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				if (cow_start != (u64)-1)
					cur_offset = cow_start;
				goto error;
			}
			if (ret > 0)
				break;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		/* Didn't find anything for our INO */
		if (found_key.objectid > ino)
			break;
		/*
		 * Keep searching until we find an EXTENT_ITEM or there are no
		 * more extents for this inode
		 */
		if (WARN_ON_ONCE(found_key.objectid < ino) ||
		    found_key.type < BTRFS_EXTENT_DATA_KEY) {
			path->slots[0]++;
			goto next_slot;
		}

		/* Found key is not EXTENT_DATA_KEY or starts after req range */
		if (found_key.type > BTRFS_EXTENT_DATA_KEY ||
		    found_key.offset > end)
			break;

		/*
		 * If the found extent starts after requested offset, then
		 * adjust extent_end to be right before this extent begins
		 */
		if (found_key.offset > cur_offset) {
			extent_end = found_key.offset;
			extent_type = 0;
			goto out_check;
		}

		/*
		 * Found extent which begins before our range and potentially
		 * intersect it
		 */
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
			/*
			 * If the extent we got ends before our current offset,
			 * skip to the next extent.
			 */
			if (extent_end <= cur_offset) {
				path->slots[0]++;
				goto next_slot;
			}
			/* Skip holes */
			if (disk_bytenr == 0)
				goto out_check;
			/* Skip compressed/encrypted/encoded extents */
			if (btrfs_file_extent_compression(leaf, fi) ||
			    btrfs_file_extent_encryption(leaf, fi) ||
			    btrfs_file_extent_other_encoding(leaf, fi))
				goto out_check;
			/*
			 * If extent is created before the last volume's snapshot
			 * this implies the extent is shared, hence we can't do
			 * nocow. This is the same check as in
			 * btrfs_cross_ref_exist but without calling
			 * btrfs_search_slot.
			 */
			if (!freespace_inode &&
			    btrfs_file_extent_generation(leaf, fi) <=
			    btrfs_root_last_snapshot(&root->root_item))
				goto out_check;
			if (extent_type == BTRFS_FILE_EXTENT_REG && !force)
				goto out_check;

			/*
			 * The following checks can be expensive, as they need to
			 * take other locks and do btree or rbtree searches, so
			 * release the path to avoid blocking other tasks for too
			 * long.
			 */
			btrfs_release_path(path);

			ret = btrfs_cross_ref_exist(root, ino,
						    found_key.offset -
						    extent_offset, disk_bytenr, false);
			if (ret) {
				/*
				 * ret could be -EIO if the above fails to read
				 * metadata.
				 */
				if (ret < 0) {
					if (cow_start != (u64)-1)
						cur_offset = cow_start;
					goto error;
				}

				WARN_ON_ONCE(freespace_inode);
				goto out_check;
			}
			disk_bytenr += extent_offset;
			disk_bytenr += cur_offset - found_key.offset;
			num_bytes = min(end + 1, extent_end) - cur_offset;
			/*
			 * If there are pending snapshots for this root, we
			 * fall into common COW way
			 */
			if (!freespace_inode && atomic_read(&root->snapshot_force_cow))
				goto out_check;
			/*
			 * force cow if csum exists in the range.
			 * this ensure that csum for a given extent are
			 * either valid or do not exist.
			 */
			ret = csum_exist_in_range(fs_info, disk_bytenr,
						  num_bytes);
			if (ret) {
				/*
				 * ret could be -EIO if the above fails to read
				 * metadata.
				 */
				if (ret < 0) {
					if (cow_start != (u64)-1)
						cur_offset = cow_start;
					goto error;
				}
				WARN_ON_ONCE(freespace_inode);
				goto out_check;
			}
			/* If the extent's block group is RO, we must COW */
			if (!btrfs_inc_nocow_writers(fs_info, disk_bytenr))
				goto out_check;
			nocow = true;
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			extent_end = found_key.offset + ram_bytes;
			extent_end = ALIGN(extent_end, fs_info->sectorsize);
			/* Skip extents outside of our requested range */
			if (extent_end <= start) {
				path->slots[0]++;
				goto next_slot;
			}
		} else {
			/* If this triggers then we have a memory corruption */
			BUG();
		}
out_check:
		/*
		 * If nocow is false then record the beginning of the range
		 * that needs to be COWed
		 */
		if (!nocow) {
			if (cow_start == (u64)-1)
				cow_start = cur_offset;
			cur_offset = extent_end;
			if (cur_offset > end)
				break;
			if (!path->nodes[0])
				continue;
			path->slots[0]++;
			goto next_slot;
		}

		/*
		 * COW range from cow_start to found_key.offset - 1. As the key
		 * will contain the beginning of the first extent that can be
		 * NOCOW, following one which needs to be COW'ed
		 */
		if (cow_start != (u64)-1) {
			ret = fallback_to_cow(inode, locked_page,
					      cow_start, found_key.offset - 1,
					      page_started, nr_written);
			if (ret)
				goto error;
			cow_start = (u64)-1;
		}

		if (extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			u64 orig_start = found_key.offset - extent_offset;
			struct extent_map *em;

			em = create_io_em(inode, cur_offset, num_bytes,
					  orig_start,
					  disk_bytenr, /* block_start */
					  num_bytes, /* block_len */
					  disk_num_bytes, /* orig_block_len */
					  ram_bytes, BTRFS_COMPRESS_NONE,
					  BTRFS_ORDERED_PREALLOC);
			if (IS_ERR(em)) {
				ret = PTR_ERR(em);
				goto error;
			}
			free_extent_map(em);
			ret = btrfs_add_ordered_extent(inode, cur_offset,
						       disk_bytenr, num_bytes,
						       num_bytes,
						       BTRFS_ORDERED_PREALLOC);
			if (ret) {
				btrfs_drop_extent_cache(inode, cur_offset,
							cur_offset + num_bytes - 1,
							0);
				goto error;
			}
		} else {
			ret = btrfs_add_ordered_extent(inode, cur_offset,
						       disk_bytenr, num_bytes,
						       num_bytes,
						       BTRFS_ORDERED_NOCOW);
			if (ret)
				goto error;
		}

		if (nocow)
			btrfs_dec_nocow_writers(fs_info, disk_bytenr);
		nocow = false;

		if (btrfs_is_data_reloc_root(root))
			/*
			 * Error handled later, as we must prevent
			 * extent_clear_unlock_delalloc() in error handler
			 * from freeing metadata of created ordered extent.
			 */
			ret = btrfs_reloc_clone_csums(inode, cur_offset,
						      num_bytes);

		extent_clear_unlock_delalloc(inode, cur_offset,
					     cur_offset + num_bytes - 1,
					     locked_page, EXTENT_LOCKED |
					     EXTENT_DELALLOC |
					     EXTENT_CLEAR_DATA_RESV,
					     PAGE_UNLOCK | PAGE_SET_ORDERED);

		cur_offset = extent_end;

		/*
		 * btrfs_reloc_clone_csums() error, now we're OK to call error
		 * handler, as metadata for created ordered extent will only
		 * be freed by btrfs_finish_ordered_io().
		 */
		if (ret)
			goto error;
		if (cur_offset > end)
			break;
	}
	btrfs_release_path(path);

	if (cur_offset <= end && cow_start == (u64)-1)
		cow_start = cur_offset;

	if (cow_start != (u64)-1) {
		cur_offset = end;
		ret = fallback_to_cow(inode, locked_page, cow_start, end,
				      page_started, nr_written);
		if (ret)
			goto error;
	}

error:
	if (nocow)
		btrfs_dec_nocow_writers(fs_info, disk_bytenr);

	if (ret && cur_offset < end)
		extent_clear_unlock_delalloc(inode, cur_offset, end,
					     locked_page, EXTENT_LOCKED |
					     EXTENT_DELALLOC | EXTENT_DEFRAG |
					     EXTENT_DO_ACCOUNTING, PAGE_UNLOCK |
					     PAGE_START_WRITEBACK |
					     PAGE_END_WRITEBACK);
	btrfs_free_path(path);
	return ret;
}

static bool should_nocow(struct btrfs_inode *inode, u64 start, u64 end)
{
	if (inode->flags & (BTRFS_INODE_NODATACOW | BTRFS_INODE_PREALLOC)) {
		if (inode->defrag_bytes &&
		    test_range_bit(&inode->io_tree, start, end, EXTENT_DEFRAG,
				   0, NULL))
			return false;
		return true;
	}
	return false;
}

/*
 * Function to process delayed allocation (create CoW) for ranges which are
 * being touched for the first time.
 */
int btrfs_run_delalloc_range(struct btrfs_inode *inode, struct page *locked_page,
		u64 start, u64 end, int *page_started, unsigned long *nr_written,
		struct writeback_control *wbc)
{
	int ret;
	const bool zoned = btrfs_is_zoned(inode->root->fs_info);

	if (should_nocow(inode, start, end)) {
		/*
		 * Normally on a zoned device we're only doing COW writes, but
		 * in case of relocation on a zoned filesystem we have taken
		 * precaution, that we're only writing sequentially. It's safe
		 * to use run_delalloc_nocow() here, like for  regular
		 * preallocated inodes.
		 */
		ASSERT(!zoned ||
		       (zoned && btrfs_is_data_reloc_root(inode->root)));
		ret = run_delalloc_nocow(inode, locked_page, start, end,
					 page_started, nr_written);
	} else if (!inode_can_compress(inode) ||
		   !inode_need_compress(inode, start, end)) {
		if (zoned)
			ret = run_delalloc_zoned(inode, locked_page, start, end,
						 page_started, nr_written);
		else
			ret = cow_file_range(inode, locked_page, start, end,
					     page_started, nr_written, 1);
	} else {
		set_bit(BTRFS_INODE_HAS_ASYNC_EXTENT, &inode->runtime_flags);
		ret = cow_file_range_async(inode, wbc, locked_page, start, end,
					   page_started, nr_written);
	}
	ASSERT(ret <= 0);
	if (ret)
		btrfs_cleanup_ordered_extents(inode, locked_page, start,
					      end - start + 1);
	return ret;
}

void btrfs_split_delalloc_extent(struct inode *inode,
				 struct extent_state *orig, u64 split)
{
	u64 size;

	/* not delalloc, ignore it */
	if (!(orig->state & EXTENT_DELALLOC))
		return;

	size = orig->end - orig->start + 1;
	if (size > BTRFS_MAX_EXTENT_SIZE) {
		u32 num_extents;
		u64 new_size;

		/*
		 * See the explanation in btrfs_merge_delalloc_extent, the same
		 * applies here, just in reverse.
		 */
		new_size = orig->end - split + 1;
		num_extents = count_max_extents(new_size);
		new_size = split - orig->start;
		num_extents += count_max_extents(new_size);
		if (count_max_extents(size) >= num_extents)
			return;
	}

	spin_lock(&BTRFS_I(inode)->lock);
	btrfs_mod_outstanding_extents(BTRFS_I(inode), 1);
	spin_unlock(&BTRFS_I(inode)->lock);
}

/*
 * Handle merged delayed allocation extents so we can keep track of new extents
 * that are just merged onto old extents, such as when we are doing sequential
 * writes, so we can properly account for the metadata space we'll need.
 */
void btrfs_merge_delalloc_extent(struct inode *inode, struct extent_state *new,
				 struct extent_state *other)
{
	u64 new_size, old_size;
	u32 num_extents;

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
		btrfs_mod_outstanding_extents(BTRFS_I(inode), -1);
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
	num_extents = count_max_extents(old_size);
	old_size = new->end - new->start + 1;
	num_extents += count_max_extents(old_size);
	if (count_max_extents(new_size) >= num_extents)
		return;

	spin_lock(&BTRFS_I(inode)->lock);
	btrfs_mod_outstanding_extents(BTRFS_I(inode), -1);
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


void __btrfs_del_delalloc_inode(struct btrfs_root *root,
				struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (!list_empty(&inode->delalloc_inodes)) {
		list_del_init(&inode->delalloc_inodes);
		clear_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			  &inode->runtime_flags);
		root->nr_delalloc_inodes--;
		if (!root->nr_delalloc_inodes) {
			ASSERT(list_empty(&root->delalloc_inodes));
			spin_lock(&fs_info->delalloc_root_lock);
			BUG_ON(list_empty(&root->delalloc_root));
			list_del_init(&root->delalloc_root);
			spin_unlock(&fs_info->delalloc_root_lock);
		}
	}
}

static void btrfs_del_delalloc_inode(struct btrfs_root *root,
				     struct btrfs_inode *inode)
{
	spin_lock(&root->delalloc_lock);
	__btrfs_del_delalloc_inode(root, inode);
	spin_unlock(&root->delalloc_lock);
}

/*
 * Properly track delayed allocation bytes in the inode and to maintain the
 * list of inodes that have pending delalloc work to be done.
 */
void btrfs_set_delalloc_extent(struct inode *inode, struct extent_state *state,
			       unsigned *bits)
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
		u32 num_extents = count_max_extents(len);
		bool do_list = !btrfs_is_free_space_inode(BTRFS_I(inode));

		spin_lock(&BTRFS_I(inode)->lock);
		btrfs_mod_outstanding_extents(BTRFS_I(inode), num_extents);
		spin_unlock(&BTRFS_I(inode)->lock);

		/* For sanity tests */
		if (btrfs_is_testing(fs_info))
			return;

		percpu_counter_add_batch(&fs_info->delalloc_bytes, len,
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

	if (!(state->state & EXTENT_DELALLOC_NEW) &&
	    (*bits & EXTENT_DELALLOC_NEW)) {
		spin_lock(&BTRFS_I(inode)->lock);
		BTRFS_I(inode)->new_delalloc_bytes += state->end + 1 -
			state->start;
		spin_unlock(&BTRFS_I(inode)->lock);
	}
}

/*
 * Once a range is no longer delalloc this function ensures that proper
 * accounting happens.
 */
void btrfs_clear_delalloc_extent(struct inode *vfs_inode,
				 struct extent_state *state, unsigned *bits)
{
	struct btrfs_inode *inode = BTRFS_I(vfs_inode);
	struct btrfs_fs_info *fs_info = btrfs_sb(vfs_inode->i_sb);
	u64 len = state->end + 1 - state->start;
	u32 num_extents = count_max_extents(len);

	if ((state->state & EXTENT_DEFRAG) && (*bits & EXTENT_DEFRAG)) {
		spin_lock(&inode->lock);
		inode->defrag_bytes -= len;
		spin_unlock(&inode->lock);
	}

	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if ((state->state & EXTENT_DELALLOC) && (*bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = inode->root;
		bool do_list = !btrfs_is_free_space_inode(inode);

		spin_lock(&inode->lock);
		btrfs_mod_outstanding_extents(inode, -num_extents);
		spin_unlock(&inode->lock);

		/*
		 * We don't reserve metadata space for space cache inodes so we
		 * don't need to call delalloc_release_metadata if there is an
		 * error.
		 */
		if (*bits & EXTENT_CLEAR_META_RESV &&
		    root != fs_info->tree_root)
			btrfs_delalloc_release_metadata(inode, len, false);

		/* For sanity tests. */
		if (btrfs_is_testing(fs_info))
			return;

		if (!btrfs_is_data_reloc_root(root) &&
		    do_list && !(state->state & EXTENT_NORESERVE) &&
		    (*bits & EXTENT_CLEAR_DATA_RESV))
			btrfs_free_reserved_data_space_noquota(fs_info, len);

		percpu_counter_add_batch(&fs_info->delalloc_bytes, -len,
					 fs_info->delalloc_batch);
		spin_lock(&inode->lock);
		inode->delalloc_bytes -= len;
		if (do_list && inode->delalloc_bytes == 0 &&
		    test_bit(BTRFS_INODE_IN_DELALLOC_LIST,
					&inode->runtime_flags))
			btrfs_del_delalloc_inode(root, inode);
		spin_unlock(&inode->lock);
	}

	if ((state->state & EXTENT_DELALLOC_NEW) &&
	    (*bits & EXTENT_DELALLOC_NEW)) {
		spin_lock(&inode->lock);
		ASSERT(inode->new_delalloc_bytes >= len);
		inode->new_delalloc_bytes -= len;
		if (*bits & EXTENT_ADD_INODE_BYTES)
			inode_add_bytes(&inode->vfs_inode, len);
		spin_unlock(&inode->lock);
	}
}

/*
 * btrfs_bio_fits_in_stripe - Checks whether the size of the given bio will fit
 * in a chunk's stripe. This function ensures that bios do not span a
 * stripe/chunk
 *
 * @page - The page we are about to add to the bio
 * @size - size we want to add to the bio
 * @bio - bio we want to ensure is smaller than a stripe
 * @bio_flags - flags of the bio
 *
 * return 1 if page cannot be added to the bio
 * return 0 if page can be added to the bio
 * return error otherwise
 */
int btrfs_bio_fits_in_stripe(struct page *page, size_t size, struct bio *bio,
			     unsigned long bio_flags)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 logical = bio->bi_iter.bi_sector << 9;
	u32 bio_len = bio->bi_iter.bi_size;
	struct extent_map *em;
	int ret = 0;
	struct btrfs_io_geometry geom;

	if (bio_flags & EXTENT_BIO_COMPRESSED)
		return 0;

	em = btrfs_get_chunk_map(fs_info, logical, fs_info->sectorsize);
	if (IS_ERR(em))
		return PTR_ERR(em);
	ret = btrfs_get_io_geometry(fs_info, em, btrfs_op(bio), logical, &geom);
	if (ret < 0)
		goto out;

	if (geom.len < bio_len + size)
		ret = 1;
out:
	free_extent_map(em);
	return ret;
}

/*
 * in order to insert checksums into the metadata in large chunks,
 * we wait until bio submission time.   All the pages in the bio are
 * checksummed and sums are attached onto the ordered extent record.
 *
 * At IO completion time the cums attached on the ordered extent record
 * are inserted into the btree
 */
static blk_status_t btrfs_submit_bio_start(struct inode *inode, struct bio *bio,
					   u64 dio_file_offset)
{
	return btrfs_csum_one_bio(BTRFS_I(inode), bio, 0, 0);
}

/*
 * Split an extent_map at [start, start + len]
 *
 * This function is intended to be used only for extract_ordered_extent().
 */
static int split_zoned_em(struct btrfs_inode *inode, u64 start, u64 len,
			  u64 pre, u64 post)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	struct extent_map *split_pre = NULL;
	struct extent_map *split_mid = NULL;
	struct extent_map *split_post = NULL;
	int ret = 0;
	unsigned long flags;

	/* Sanity check */
	if (pre == 0 && post == 0)
		return 0;

	split_pre = alloc_extent_map();
	if (pre)
		split_mid = alloc_extent_map();
	if (post)
		split_post = alloc_extent_map();
	if (!split_pre || (pre && !split_mid) || (post && !split_post)) {
		ret = -ENOMEM;
		goto out;
	}

	ASSERT(pre + post < len);

	lock_extent(&inode->io_tree, start, start + len - 1);
	write_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	if (!em) {
		ret = -EIO;
		goto out_unlock;
	}

	ASSERT(em->len == len);
	ASSERT(!test_bit(EXTENT_FLAG_COMPRESSED, &em->flags));
	ASSERT(em->block_start < EXTENT_MAP_LAST_BYTE);
	ASSERT(test_bit(EXTENT_FLAG_PINNED, &em->flags));
	ASSERT(!test_bit(EXTENT_FLAG_LOGGING, &em->flags));
	ASSERT(!list_empty(&em->list));

	flags = em->flags;
	clear_bit(EXTENT_FLAG_PINNED, &em->flags);

	/* First, replace the em with a new extent_map starting from * em->start */
	split_pre->start = em->start;
	split_pre->len = (pre ? pre : em->len - post);
	split_pre->orig_start = split_pre->start;
	split_pre->block_start = em->block_start;
	split_pre->block_len = split_pre->len;
	split_pre->orig_block_len = split_pre->block_len;
	split_pre->ram_bytes = split_pre->len;
	split_pre->flags = flags;
	split_pre->compress_type = em->compress_type;
	split_pre->generation = em->generation;

	replace_extent_mapping(em_tree, em, split_pre, 1);

	/*
	 * Now we only have an extent_map at:
	 *     [em->start, em->start + pre] if pre != 0
	 *     [em->start, em->start + em->len - post] if pre == 0
	 */

	if (pre) {
		/* Insert the middle extent_map */
		split_mid->start = em->start + pre;
		split_mid->len = em->len - pre - post;
		split_mid->orig_start = split_mid->start;
		split_mid->block_start = em->block_start + pre;
		split_mid->block_len = split_mid->len;
		split_mid->orig_block_len = split_mid->block_len;
		split_mid->ram_bytes = split_mid->len;
		split_mid->flags = flags;
		split_mid->compress_type = em->compress_type;
		split_mid->generation = em->generation;
		add_extent_mapping(em_tree, split_mid, 1);
	}

	if (post) {
		split_post->start = em->start + em->len - post;
		split_post->len = post;
		split_post->orig_start = split_post->start;
		split_post->block_start = em->block_start + em->len - post;
		split_post->block_len = split_post->len;
		split_post->orig_block_len = split_post->block_len;
		split_post->ram_bytes = split_post->len;
		split_post->flags = flags;
		split_post->compress_type = em->compress_type;
		split_post->generation = em->generation;
		add_extent_mapping(em_tree, split_post, 1);
	}

	/* Once for us */
	free_extent_map(em);
	/* Once for the tree */
	free_extent_map(em);

out_unlock:
	write_unlock(&em_tree->lock);
	unlock_extent(&inode->io_tree, start, start + len - 1);
out:
	free_extent_map(split_pre);
	free_extent_map(split_mid);
	free_extent_map(split_post);

	return ret;
}

static blk_status_t extract_ordered_extent(struct btrfs_inode *inode,
					   struct bio *bio, loff_t file_offset)
{
	struct btrfs_ordered_extent *ordered;
	u64 start = (u64)bio->bi_iter.bi_sector << SECTOR_SHIFT;
	u64 file_len;
	u64 len = bio->bi_iter.bi_size;
	u64 end = start + len;
	u64 ordered_end;
	u64 pre, post;
	int ret = 0;

	ordered = btrfs_lookup_ordered_extent(inode, file_offset);
	if (WARN_ON_ONCE(!ordered))
		return BLK_STS_IOERR;

	/* No need to split */
	if (ordered->disk_num_bytes == len)
		goto out;

	/* We cannot split once end_bio'd ordered extent */
	if (WARN_ON_ONCE(ordered->bytes_left != ordered->disk_num_bytes)) {
		ret = -EINVAL;
		goto out;
	}

	/* We cannot split a compressed ordered extent */
	if (WARN_ON_ONCE(ordered->disk_num_bytes != ordered->num_bytes)) {
		ret = -EINVAL;
		goto out;
	}

	ordered_end = ordered->disk_bytenr + ordered->disk_num_bytes;
	/* bio must be in one ordered extent */
	if (WARN_ON_ONCE(start < ordered->disk_bytenr || end > ordered_end)) {
		ret = -EINVAL;
		goto out;
	}

	/* Checksum list should be empty */
	if (WARN_ON_ONCE(!list_empty(&ordered->list))) {
		ret = -EINVAL;
		goto out;
	}

	file_len = ordered->num_bytes;
	pre = start - ordered->disk_bytenr;
	post = ordered_end - end;

	ret = btrfs_split_ordered_extent(ordered, pre, post);
	if (ret)
		goto out;
	ret = split_zoned_em(inode, file_offset, file_len, pre, post);

out:
	btrfs_put_ordered_extent(ordered);

	return errno_to_blk_status(ret);
}

/*
 * extent_io.c submission hook. This does the right thing for csum calculation
 * on write, or reading the csums from the tree before a read.
 *
 * Rules about async/sync submit,
 * a) read:				sync submit
 *
 * b) write without checksum:		sync submit
 *
 * c) write with checksum:
 *    c-1) if bio is issued by fsync:	sync submit
 *         (sync_writers != 0)
 *
 *    c-2) if root is reloc root:	sync submit
 *         (only in case of buffered IO)
 *
 *    c-3) otherwise:			async submit
 */
blk_status_t btrfs_submit_data_bio(struct inode *inode, struct bio *bio,
				   int mirror_num, unsigned long bio_flags)

{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	enum btrfs_wq_endio_type metadata = BTRFS_WQ_ENDIO_DATA;
	blk_status_t ret = 0;
	int skip_sum;
	int async = !atomic_read(&BTRFS_I(inode)->sync_writers);

	skip_sum = (BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM) ||
		   !fs_info->csum_root;

	if (btrfs_is_free_space_inode(BTRFS_I(inode)))
		metadata = BTRFS_WQ_ENDIO_FREE_SPACE;

	if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
		struct page *page = bio_first_bvec_all(bio)->bv_page;
		loff_t file_offset = page_offset(page);

		ret = extract_ordered_extent(BTRFS_I(inode), bio, file_offset);
		if (ret)
			goto out;
	}

	if (btrfs_op(bio) != BTRFS_MAP_WRITE) {
		ret = btrfs_bio_wq_end_io(fs_info, bio, metadata);
		if (ret)
			goto out;

		if (bio_flags & EXTENT_BIO_COMPRESSED) {
			ret = btrfs_submit_compressed_read(inode, bio,
							   mirror_num,
							   bio_flags);
			goto out;
		} else {
			/*
			 * Lookup bio sums does extra checks around whether we
			 * need to csum or not, which is why we ignore skip_sum
			 * here.
			 */
			ret = btrfs_lookup_bio_sums(inode, bio, NULL);
			if (ret)
				goto out;
		}
		goto mapit;
	} else if (async && !skip_sum) {
		/* csum items have already been cloned */
		if (btrfs_is_data_reloc_root(root))
			goto mapit;
		/* we're doing a write, do the async checksumming */
		ret = btrfs_wq_submit_bio(inode, bio, mirror_num, bio_flags,
					  0, btrfs_submit_bio_start);
		goto out;
	} else if (!skip_sum) {
		ret = btrfs_csum_one_bio(BTRFS_I(inode), bio, 0, 0);
		if (ret)
			goto out;
	}

mapit:
	ret = btrfs_map_bio(fs_info, bio, mirror_num);

out:
	if (ret) {
		bio->bi_status = ret;
		bio_endio(bio);
	}
	return ret;
}

/*
 * given a list of ordered sums record them in the inode.  This happens
 * at IO completion time based on sums calculated at bio submission time.
 */
static int add_pending_csums(struct btrfs_trans_handle *trans,
			     struct list_head *list)
{
	struct btrfs_ordered_sum *sum;
	int ret;

	list_for_each_entry(sum, list, list) {
		trans->adding_csums = true;
		ret = btrfs_csum_file_blocks(trans, trans->fs_info->csum_root, sum);
		trans->adding_csums = false;
		if (ret)
			return ret;
	}
	return 0;
}

static int btrfs_find_new_delalloc_bytes(struct btrfs_inode *inode,
					 const u64 start,
					 const u64 len,
					 struct extent_state **cached_state)
{
	u64 search_start = start;
	const u64 end = start + len - 1;

	while (search_start < end) {
		const u64 search_len = end - search_start + 1;
		struct extent_map *em;
		u64 em_len;
		int ret = 0;

		em = btrfs_get_extent(inode, NULL, 0, search_start, search_len);
		if (IS_ERR(em))
			return PTR_ERR(em);

		if (em->block_start != EXTENT_MAP_HOLE)
			goto next;

		em_len = em->len;
		if (em->start < search_start)
			em_len -= search_start - em->start;
		if (em_len > search_len)
			em_len = search_len;

		ret = set_extent_bit(&inode->io_tree, search_start,
				     search_start + em_len - 1,
				     EXTENT_DELALLOC_NEW, 0, NULL, cached_state,
				     GFP_NOFS, NULL);
next:
		search_start = extent_map_end(em);
		free_extent_map(em);
		if (ret)
			return ret;
	}
	return 0;
}

int btrfs_set_extent_delalloc(struct btrfs_inode *inode, u64 start, u64 end,
			      unsigned int extra_bits,
			      struct extent_state **cached_state)
{
	WARN_ON(PAGE_ALIGNED(end));

	if (start >= i_size_read(&inode->vfs_inode) &&
	    !(inode->flags & BTRFS_INODE_PREALLOC)) {
		/*
		 * There can't be any extents following eof in this case so just
		 * set the delalloc new bit for the range directly.
		 */
		extra_bits |= EXTENT_DELALLOC_NEW;
	} else {
		int ret;

		ret = btrfs_find_new_delalloc_bytes(inode, start,
						    end + 1 - start,
						    cached_state);
		if (ret)
			return ret;
	}

	return set_extent_delalloc(&inode->io_tree, start, end, extra_bits,
				   cached_state);
}

/* see btrfs_writepage_start_hook for details on why this is required */
struct btrfs_writepage_fixup {
	struct page *page;
	struct inode *inode;
	struct btrfs_work work;
};

static void btrfs_writepage_fixup_worker(struct btrfs_work *work)
{
	struct btrfs_writepage_fixup *fixup;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_changeset *data_reserved = NULL;
	struct page *page;
	struct btrfs_inode *inode;
	u64 page_start;
	u64 page_end;
	int ret = 0;
	bool free_delalloc_space = true;

	fixup = container_of(work, struct btrfs_writepage_fixup, work);
	page = fixup->page;
	inode = BTRFS_I(fixup->inode);
	page_start = page_offset(page);
	page_end = page_offset(page) + PAGE_SIZE - 1;

	/*
	 * This is similar to page_mkwrite, we need to reserve the space before
	 * we take the page lock.
	 */
	ret = btrfs_delalloc_reserve_space(inode, &data_reserved, page_start,
					   PAGE_SIZE);
again:
	lock_page(page);

	/*
	 * Before we queued this fixup, we took a reference on the page.
	 * page->mapping may go NULL, but it shouldn't be moved to a different
	 * address space.
	 */
	if (!page->mapping || !PageDirty(page) || !PageChecked(page)) {
		/*
		 * Unfortunately this is a little tricky, either
		 *
		 * 1) We got here and our page had already been dealt with and
		 *    we reserved our space, thus ret == 0, so we need to just
		 *    drop our space reservation and bail.  This can happen the
		 *    first time we come into the fixup worker, or could happen
		 *    while waiting for the ordered extent.
		 * 2) Our page was already dealt with, but we happened to get an
		 *    ENOSPC above from the btrfs_delalloc_reserve_space.  In
		 *    this case we obviously don't have anything to release, but
		 *    because the page was already dealt with we don't want to
		 *    mark the page with an error, so make sure we're resetting
		 *    ret to 0.  This is why we have this check _before_ the ret
		 *    check, because we do not want to have a surprise ENOSPC
		 *    when the page was already properly dealt with.
		 */
		if (!ret) {
			btrfs_delalloc_release_extents(inode, PAGE_SIZE);
			btrfs_delalloc_release_space(inode, data_reserved,
						     page_start, PAGE_SIZE,
						     true);
		}
		ret = 0;
		goto out_page;
	}

	/*
	 * We can't mess with the page state unless it is locked, so now that
	 * it is locked bail if we failed to make our space reservation.
	 */
	if (ret)
		goto out_page;

	lock_extent_bits(&inode->io_tree, page_start, page_end, &cached_state);

	/* already ordered? We're done */
	if (PageOrdered(page))
		goto out_reserved;

	ordered = btrfs_lookup_ordered_range(inode, page_start, PAGE_SIZE);
	if (ordered) {
		unlock_extent_cached(&inode->io_tree, page_start, page_end,
				     &cached_state);
		unlock_page(page);
		btrfs_start_ordered_extent(ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	ret = btrfs_set_extent_delalloc(inode, page_start, page_end, 0,
					&cached_state);
	if (ret)
		goto out_reserved;

	/*
	 * Everything went as planned, we're now the owner of a dirty page with
	 * delayed allocation bits set and space reserved for our COW
	 * destination.
	 *
	 * The page was dirty when we started, nothing should have cleaned it.
	 */
	BUG_ON(!PageDirty(page));
	free_delalloc_space = false;
out_reserved:
	btrfs_delalloc_release_extents(inode, PAGE_SIZE);
	if (free_delalloc_space)
		btrfs_delalloc_release_space(inode, data_reserved, page_start,
					     PAGE_SIZE, true);
	unlock_extent_cached(&inode->io_tree, page_start, page_end,
			     &cached_state);
out_page:
	if (ret) {
		/*
		 * We hit ENOSPC or other errors.  Update the mapping and page
		 * to reflect the errors and clean the page.
		 */
		mapping_set_error(page->mapping, ret);
		end_extent_writepage(page, ret, page_start, page_end);
		clear_page_dirty_for_io(page);
		SetPageError(page);
	}
	ClearPageChecked(page);
	unlock_page(page);
	put_page(page);
	kfree(fixup);
	extent_changeset_free(data_reserved);
	/*
	 * As a precaution, do a delayed iput in case it would be the last iput
	 * that could need flushing space. Recursing back to fixup worker would
	 * deadlock.
	 */
	btrfs_add_delayed_iput(&inode->vfs_inode);
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
int btrfs_writepage_cow_fixup(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_writepage_fixup *fixup;

	/* This page has ordered extent covering it already */
	if (PageOrdered(page))
		return 0;

	/*
	 * PageChecked is set below when we create a fixup worker for this page,
	 * don't try to create another one if we're already PageChecked()
	 *
	 * The extent_io writepage code will redirty the page if we send back
	 * EAGAIN.
	 */
	if (PageChecked(page))
		return -EAGAIN;

	fixup = kzalloc(sizeof(*fixup), GFP_NOFS);
	if (!fixup)
		return -EAGAIN;

	/*
	 * We are already holding a reference to this inode from
	 * write_cache_pages.  We need to hold it because the space reservation
	 * takes place outside of the page lock, and we can't trust
	 * page->mapping outside of the page lock.
	 */
	ihold(inode);
	SetPageChecked(page);
	get_page(page);
	btrfs_init_work(&fixup->work, btrfs_writepage_fixup_worker, NULL, NULL);
	fixup->page = page;
	fixup->inode = inode;
	btrfs_queue_work(fs_info->fixup_workers, &fixup->work);

	return -EAGAIN;
}

static int insert_reserved_file_extent(struct btrfs_trans_handle *trans,
				       struct btrfs_inode *inode, u64 file_pos,
				       struct btrfs_file_extent_item *stack_fi,
				       const bool update_inode_bytes,
				       u64 qgroup_reserved)
{
	struct btrfs_root *root = inode->root;
	const u64 sectorsize = root->fs_info->sectorsize;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key ins;
	u64 disk_num_bytes = btrfs_stack_file_extent_disk_num_bytes(stack_fi);
	u64 disk_bytenr = btrfs_stack_file_extent_disk_bytenr(stack_fi);
	u64 num_bytes = btrfs_stack_file_extent_num_bytes(stack_fi);
	u64 ram_bytes = btrfs_stack_file_extent_ram_bytes(stack_fi);
	struct btrfs_drop_extents_args drop_args = { 0 };
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
	drop_args.path = path;
	drop_args.start = file_pos;
	drop_args.end = file_pos + num_bytes;
	drop_args.replace_extent = true;
	drop_args.extent_item_size = sizeof(*stack_fi);
	ret = btrfs_drop_extents(trans, root, inode, &drop_args);
	if (ret)
		goto out;

	if (!drop_args.extent_inserted) {
		ins.objectid = btrfs_ino(inode);
		ins.offset = file_pos;
		ins.type = BTRFS_EXTENT_DATA_KEY;

		ret = btrfs_insert_empty_item(trans, root, path, &ins,
					      sizeof(*stack_fi));
		if (ret)
			goto out;
	}
	leaf = path->nodes[0];
	btrfs_set_stack_file_extent_generation(stack_fi, trans->transid);
	write_extent_buffer(leaf, stack_fi,
			btrfs_item_ptr_offset(leaf, path->slots[0]),
			sizeof(struct btrfs_file_extent_item));

	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	/*
	 * If we dropped an inline extent here, we know the range where it is
	 * was not marked with the EXTENT_DELALLOC_NEW bit, so we update the
	 * number of bytes only for that range containing the inline extent.
	 * The remaining of the range will be processed when clearning the
	 * EXTENT_DELALLOC_BIT bit through the ordered extent completion.
	 */
	if (file_pos == 0 && !IS_ALIGNED(drop_args.bytes_found, sectorsize)) {
		u64 inline_size = round_down(drop_args.bytes_found, sectorsize);

		inline_size = drop_args.bytes_found - inline_size;
		btrfs_update_inode_bytes(inode, sectorsize, inline_size);
		drop_args.bytes_found -= inline_size;
		num_bytes -= sectorsize;
	}

	if (update_inode_bytes)
		btrfs_update_inode_bytes(inode, num_bytes, drop_args.bytes_found);

	ins.objectid = disk_bytenr;
	ins.offset = disk_num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	ret = btrfs_inode_set_file_extent_range(inode, file_pos, ram_bytes);
	if (ret)
		goto out;

	ret = btrfs_alloc_reserved_file_extent(trans, root, btrfs_ino(inode),
					       file_pos, qgroup_reserved, &ins);
out:
	btrfs_free_path(path);

	return ret;
}

static void btrfs_release_delalloc_bytes(struct btrfs_fs_info *fs_info,
					 u64 start, u64 len)
{
	struct btrfs_block_group *cache;

	cache = btrfs_lookup_block_group(fs_info, start);
	ASSERT(cache);

	spin_lock(&cache->lock);
	cache->delalloc_bytes -= len;
	spin_unlock(&cache->lock);

	btrfs_put_block_group(cache);
}

static int insert_ordered_extent_file_extent(struct btrfs_trans_handle *trans,
					     struct btrfs_ordered_extent *oe)
{
	struct btrfs_file_extent_item stack_fi;
	u64 logical_len;
	bool update_inode_bytes;

	memset(&stack_fi, 0, sizeof(stack_fi));
	btrfs_set_stack_file_extent_type(&stack_fi, BTRFS_FILE_EXTENT_REG);
	btrfs_set_stack_file_extent_disk_bytenr(&stack_fi, oe->disk_bytenr);
	btrfs_set_stack_file_extent_disk_num_bytes(&stack_fi,
						   oe->disk_num_bytes);
	if (test_bit(BTRFS_ORDERED_TRUNCATED, &oe->flags))
		logical_len = oe->truncated_len;
	else
		logical_len = oe->num_bytes;
	btrfs_set_stack_file_extent_num_bytes(&stack_fi, logical_len);
	btrfs_set_stack_file_extent_ram_bytes(&stack_fi, logical_len);
	btrfs_set_stack_file_extent_compression(&stack_fi, oe->compress_type);
	/* Encryption and other encoding is reserved and all 0 */

	/*
	 * For delalloc, when completing an ordered extent we update the inode's
	 * bytes when clearing the range in the inode's io tree, so pass false
	 * as the argument 'update_inode_bytes' to insert_reserved_file_extent(),
	 * except if the ordered extent was truncated.
	 */
	update_inode_bytes = test_bit(BTRFS_ORDERED_DIRECT, &oe->flags) ||
			     test_bit(BTRFS_ORDERED_TRUNCATED, &oe->flags);

	return insert_reserved_file_extent(trans, BTRFS_I(oe->inode),
					   oe->file_offset, &stack_fi,
					   update_inode_bytes, oe->qgroup_rsv);
}

/*
 * As ordered data IO finishes, this gets called so we can finish
 * an ordered extent if the range of bytes in the file it covers are
 * fully written.
 */
static int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered_extent)
{
	struct btrfs_inode *inode = BTRFS_I(ordered_extent->inode);
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans = NULL;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct extent_state *cached_state = NULL;
	u64 start, end;
	int compress_type = 0;
	int ret = 0;
	u64 logical_len = ordered_extent->num_bytes;
	bool freespace_inode;
	bool truncated = false;
	bool clear_reserved_extent = true;
	unsigned int clear_bits = EXTENT_DEFRAG;

	start = ordered_extent->file_offset;
	end = start + ordered_extent->num_bytes - 1;

	if (!test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags) &&
	    !test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags) &&
	    !test_bit(BTRFS_ORDERED_DIRECT, &ordered_extent->flags))
		clear_bits |= EXTENT_DELALLOC_NEW;

	freespace_inode = btrfs_is_free_space_inode(inode);

	if (test_bit(BTRFS_ORDERED_IOERR, &ordered_extent->flags)) {
		ret = -EIO;
		goto out;
	}

	if (ordered_extent->bdev)
		btrfs_rewrite_logical_zoned(ordered_extent);

	btrfs_free_io_failure_record(inode, start, end);

	if (test_bit(BTRFS_ORDERED_TRUNCATED, &ordered_extent->flags)) {
		truncated = true;
		logical_len = ordered_extent->truncated_len;
		/* Truncated the entire extent, don't bother adding */
		if (!logical_len)
			goto out;
	}

	if (test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags)) {
		BUG_ON(!list_empty(&ordered_extent->list)); /* Logic error */

		btrfs_inode_safe_disk_i_size_write(inode, 0);
		if (freespace_inode)
			trans = btrfs_join_transaction_spacecache(root);
		else
			trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}
		trans->block_rsv = &inode->block_rsv;
		ret = btrfs_update_inode_fallback(trans, root, inode);
		if (ret) /* -ENOMEM or corruption */
			btrfs_abort_transaction(trans, ret);
		goto out;
	}

	clear_bits |= EXTENT_LOCKED;
	lock_extent_bits(io_tree, start, end, &cached_state);

	if (freespace_inode)
		trans = btrfs_join_transaction_spacecache(root);
	else
		trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	trans->block_rsv = &inode->block_rsv;

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
		ret = insert_ordered_extent_file_extent(trans, ordered_extent);
		if (!ret) {
			clear_reserved_extent = false;
			btrfs_release_delalloc_bytes(fs_info,
						ordered_extent->disk_bytenr,
						ordered_extent->disk_num_bytes);
		}
	}
	unpin_extent_cache(&inode->extent_tree, ordered_extent->file_offset,
			   ordered_extent->num_bytes, trans->transid);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = add_pending_csums(trans, &ordered_extent->list);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	/*
	 * If this is a new delalloc range, clear its new delalloc flag to
	 * update the inode's number of bytes. This needs to be done first
	 * before updating the inode item.
	 */
	if ((clear_bits & EXTENT_DELALLOC_NEW) &&
	    !test_bit(BTRFS_ORDERED_TRUNCATED, &ordered_extent->flags))
		clear_extent_bit(&inode->io_tree, start, end,
				 EXTENT_DELALLOC_NEW | EXTENT_ADD_INODE_BYTES,
				 0, 0, &cached_state);

	btrfs_inode_safe_disk_i_size_write(inode, 0);
	ret = btrfs_update_inode_fallback(trans, root, inode);
	if (ret) { /* -ENOMEM or corruption */
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
	ret = 0;
out:
	clear_extent_bit(&inode->io_tree, start, end, clear_bits,
			 (clear_bits & EXTENT_LOCKED) ? 1 : 0, 0,
			 &cached_state);

	if (trans)
		btrfs_end_transaction(trans);

	if (ret || truncated) {
		u64 unwritten_start = start;

		/*
		 * If we failed to finish this ordered extent for any reason we
		 * need to make sure BTRFS_ORDERED_IOERR is set on the ordered
		 * extent, and mark the inode with the error if it wasn't
		 * already set.  Any error during writeback would have already
		 * set the mapping error, so we need to set it if we're the ones
		 * marking this ordered extent as failed.
		 */
		if (ret && !test_and_set_bit(BTRFS_ORDERED_IOERR,
					     &ordered_extent->flags))
			mapping_set_error(ordered_extent->inode->i_mapping, -EIO);

		if (truncated)
			unwritten_start += logical_len;
		clear_extent_uptodate(io_tree, unwritten_start, end, NULL);

		/* Drop the cache for the part of the extent we didn't write. */
		btrfs_drop_extent_cache(inode, unwritten_start, end, 0);

		/*
		 * If the ordered extent had an IOERR or something else went
		 * wrong we need to return the space for this ordered extent
		 * back to the allocator.  We only free the extent in the
		 * truncated case if we didn't write out the extent at all.
		 *
		 * If we made it past insert_reserved_file_extent before we
		 * errored out then we don't need to do this as the accounting
		 * has already been done.
		 */
		if ((ret || !logical_len) &&
		    clear_reserved_extent &&
		    !test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags) &&
		    !test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags)) {
			/*
			 * Discard the range before returning it back to the
			 * free space pool
			 */
			if (ret && btrfs_test_opt(fs_info, DISCARD_SYNC))
				btrfs_discard_extent(fs_info,
						ordered_extent->disk_bytenr,
						ordered_extent->disk_num_bytes,
						NULL);
			btrfs_free_reserved_extent(fs_info,
					ordered_extent->disk_bytenr,
					ordered_extent->disk_num_bytes, 1);
		}
	}

	/*
	 * This needs to be done to make sure anybody waiting knows we are done
	 * updating everything for this ordered extent.
	 */
	btrfs_remove_ordered_extent(inode, ordered_extent);

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

void btrfs_writepage_endio_finish_ordered(struct btrfs_inode *inode,
					  struct page *page, u64 start,
					  u64 end, bool uptodate)
{
	trace_btrfs_writepage_end_io_hook(inode, start, end, uptodate);

	btrfs_mark_ordered_io_finished(inode, page, start, end + 1 - start,
				       finish_ordered_fn, uptodate);
}

/*
 * check_data_csum - verify checksum of one sector of uncompressed data
 * @inode:	inode
 * @io_bio:	btrfs_io_bio which contains the csum
 * @bio_offset:	offset to the beginning of the bio (in bytes)
 * @page:	page where is the data to be verified
 * @pgoff:	offset inside the page
 * @start:	logical offset in the file
 *
 * The length of such check is always one sector size.
 */
static int check_data_csum(struct inode *inode, struct btrfs_io_bio *io_bio,
			   u32 bio_offset, struct page *page, u32 pgoff,
			   u64 start)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	char *kaddr;
	u32 len = fs_info->sectorsize;
	const u32 csum_size = fs_info->csum_size;
	unsigned int offset_sectors;
	u8 *csum_expected;
	u8 csum[BTRFS_CSUM_SIZE];

	ASSERT(pgoff + len <= PAGE_SIZE);

	offset_sectors = bio_offset >> fs_info->sectorsize_bits;
	csum_expected = ((u8 *)io_bio->csum) + offset_sectors * csum_size;

	kaddr = kmap_atomic(page);
	shash->tfm = fs_info->csum_shash;

	crypto_shash_digest(shash, kaddr + pgoff, len, csum);

	if (memcmp(csum, csum_expected, csum_size))
		goto zeroit;

	kunmap_atomic(kaddr);
	return 0;
zeroit:
	btrfs_print_data_csum_error(BTRFS_I(inode), start, csum, csum_expected,
				    io_bio->mirror_num);
	if (io_bio->device)
		btrfs_dev_stat_inc_and_print(io_bio->device,
					     BTRFS_DEV_STAT_CORRUPTION_ERRS);
	memset(kaddr + pgoff, 1, len);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);
	return -EIO;
}

/*
 * When reads are done, we need to check csums to verify the data is correct.
 * if there's a match, we allow the bio to finish.  If not, the code in
 * extent_io.c will try to find good copies for us.
 *
 * @bio_offset:	offset to the beginning of the bio (in bytes)
 * @start:	file offset of the range start
 * @end:	file offset of the range end (inclusive)
 *
 * Return a bitmap where bit set means a csum mismatch, and bit not set means
 * csum match.
 */
unsigned int btrfs_verify_data_csum(struct btrfs_io_bio *io_bio, u32 bio_offset,
				    struct page *page, u64 start, u64 end)
{
	struct inode *inode = page->mapping->host;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	const u32 sectorsize = root->fs_info->sectorsize;
	u32 pg_off;
	unsigned int result = 0;

	if (PageChecked(page)) {
		ClearPageChecked(page);
		return 0;
	}

	/*
	 * For subpage case, above PageChecked is not safe as it's not subpage
	 * compatible.
	 * But for now only cow fixup and compressed read utilize PageChecked
	 * flag, while in this context we can easily use io_bio->csum to
	 * determine if we really need to do csum verification.
	 *
	 * So for now, just exit if io_bio->csum is NULL, as it means it's
	 * compressed read, and its compressed data csum has already been
	 * verified.
	 */
	if (io_bio->csum == NULL)
		return 0;

	if (BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)
		return 0;

	if (!root->fs_info->csum_root)
		return 0;

	ASSERT(page_offset(page) <= start &&
	       end <= page_offset(page) + PAGE_SIZE - 1);
	for (pg_off = offset_in_page(start);
	     pg_off < offset_in_page(end);
	     pg_off += sectorsize, bio_offset += sectorsize) {
		u64 file_offset = pg_off + page_offset(page);
		int ret;

		if (btrfs_is_data_reloc_root(root) &&
		    test_range_bit(io_tree, file_offset,
				   file_offset + sectorsize - 1,
				   EXTENT_NODATASUM, 1, NULL)) {
			/* Skip the range without csum for data reloc inode */
			clear_extent_bits(io_tree, file_offset,
					  file_offset + sectorsize - 1,
					  EXTENT_NODATASUM);
			continue;
		}
		ret = check_data_csum(inode, io_bio, bio_offset, page, pg_off,
				      page_offset(page) + pg_off);
		if (ret < 0) {
			const int nr_bit = (pg_off - offset_in_page(start)) >>
				     root->fs_info->sectorsize_bits;

			result |= (1U << nr_bit);
		}
	}
	return result;
}

/*
 * btrfs_add_delayed_iput - perform a delayed iput on @inode
 *
 * @inode: The inode we want to perform iput on
 *
 * This function uses the generic vfs_inode::i_count to track whether we should
 * just decrement it (in case it's > 1) or if this is the last iput then link
 * the inode to the delayed iput machinery. Delayed iputs are processed at
 * transaction commit time/superblock commit/cleaner kthread.
 */
void btrfs_add_delayed_iput(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_inode *binode = BTRFS_I(inode);

	if (atomic_add_unless(&inode->i_count, -1, 1))
		return;

	atomic_inc(&fs_info->nr_delayed_iputs);
	spin_lock(&fs_info->delayed_iput_lock);
	ASSERT(list_empty(&binode->delayed_iput));
	list_add_tail(&binode->delayed_iput, &fs_info->delayed_iputs);
	spin_unlock(&fs_info->delayed_iput_lock);
	if (!test_bit(BTRFS_FS_CLEANER_RUNNING, &fs_info->flags))
		wake_up_process(fs_info->cleaner_kthread);
}

static void run_delayed_iput_locked(struct btrfs_fs_info *fs_info,
				    struct btrfs_inode *inode)
{
	list_del_init(&inode->delayed_iput);
	spin_unlock(&fs_info->delayed_iput_lock);
	iput(&inode->vfs_inode);
	if (atomic_dec_and_test(&fs_info->nr_delayed_iputs))
		wake_up(&fs_info->delayed_iputs_wait);
	spin_lock(&fs_info->delayed_iput_lock);
}

static void btrfs_run_delayed_iput(struct btrfs_fs_info *fs_info,
				   struct btrfs_inode *inode)
{
	if (!list_empty(&inode->delayed_iput)) {
		spin_lock(&fs_info->delayed_iput_lock);
		if (!list_empty(&inode->delayed_iput))
			run_delayed_iput_locked(fs_info, inode);
		spin_unlock(&fs_info->delayed_iput_lock);
	}
}

void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info)
{

	spin_lock(&fs_info->delayed_iput_lock);
	while (!list_empty(&fs_info->delayed_iputs)) {
		struct btrfs_inode *inode;

		inode = list_first_entry(&fs_info->delayed_iputs,
				struct btrfs_inode, delayed_iput);
		run_delayed_iput_locked(fs_info, inode);
		cond_resched_lock(&fs_info->delayed_iput_lock);
	}
	spin_unlock(&fs_info->delayed_iput_lock);
}

/**
 * Wait for flushing all delayed iputs
 *
 * @fs_info:  the filesystem
 *
 * This will wait on any delayed iputs that are currently running with KILLABLE
 * set.  Once they are all done running we will return, unless we are killed in
 * which case we return EINTR. This helps in user operations like fallocate etc
 * that might get blocked on the iputs.
 *
 * Return EINTR if we were killed, 0 if nothing's pending
 */
int btrfs_wait_on_delayed_iputs(struct btrfs_fs_info *fs_info)
{
	int ret = wait_event_killable(fs_info->delayed_iputs_wait,
			atomic_read(&fs_info->nr_delayed_iputs) == 0);
	if (ret)
		return -EINTR;
	return 0;
}

/*
 * This creates an orphan entry for the given inode in case something goes wrong
 * in the middle of an unlink.
 */
int btrfs_orphan_add(struct btrfs_trans_handle *trans,
		     struct btrfs_inode *inode)
{
	int ret;

	ret = btrfs_insert_orphan_item(trans, inode->root, btrfs_ino(inode));
	if (ret && ret != -EEXIST) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	return 0;
}

/*
 * We have done the delete so we can go ahead and remove the orphan item for
 * this particular inode.
 */
static int btrfs_orphan_del(struct btrfs_trans_handle *trans,
			    struct btrfs_inode *inode)
{
	return btrfs_del_orphan_item(trans, inode->root, btrfs_ino(inode));
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
	int ret = 0, nr_unlink = 0;

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
		inode = btrfs_iget(fs_info->sb, last_objectid, root);
		ret = PTR_ERR_OR_ZERO(inode);
		if (ret && ret != -ENOENT)
			goto out;

		if (ret == -ENOENT && root == fs_info->tree_root) {
			struct btrfs_root *dead_root;
			int is_dead_root = 0;

			/*
			 * This is an orphan in the tree root. Currently these
			 * could come from 2 sources:
			 *  a) a root (snapshot/subvolume) deletion in progress
			 *  b) a free space cache inode
			 * We need to distinguish those two, as the orphan item
			 * for a root must not get deleted before the deletion
			 * of the snapshot/subvolume's tree completes.
			 *
			 * btrfs_find_orphan_roots() ran before us, which has
			 * found all deleted roots and loaded them into
			 * fs_info->fs_roots_radix. So here we can find if an
			 * orphan item corresponds to a deleted root by looking
			 * up the root from that radix tree.
			 */

			spin_lock(&fs_info->fs_roots_radix_lock);
			dead_root = radix_tree_lookup(&fs_info->fs_roots_radix,
							 (unsigned long)found_key.objectid);
			if (dead_root && btrfs_root_refs(&dead_root->root_item) == 0)
				is_dead_root = 1;
			spin_unlock(&fs_info->fs_roots_radix_lock);

			if (is_dead_root) {
				/* prevent this orphan from being found again */
				key.offset = found_key.objectid - 1;
				continue;
			}

		}

		/*
		 * If we have an inode with links, there are a couple of
		 * possibilities:
		 *
		 * 1. We were halfway through creating fsverity metadata for the
		 * file. In that case, the orphan item represents incomplete
		 * fsverity metadata which must be cleaned up with
		 * btrfs_drop_verity_items and deleting the orphan item.

		 * 2. Old kernels (before v3.12) used to create an
		 * orphan item for truncate indicating that there were possibly
		 * extent items past i_size that needed to be deleted. In v3.12,
		 * truncate was changed to update i_size in sync with the extent
		 * items, but the (useless) orphan item was still created. Since
		 * v4.18, we don't create the orphan item for truncate at all.
		 *
		 * So, this item could mean that we need to do a truncate, but
		 * only if this filesystem was last used on a pre-v3.12 kernel
		 * and was not cleanly unmounted. The odds of that are quite
		 * slim, and it's a pain to do the truncate now, so just delete
		 * the orphan item.
		 *
		 * It's also possible that this orphan item was supposed to be
		 * deleted but wasn't. The inode number may have been reused,
		 * but either way, we can delete the orphan item.
		 */
		if (ret == -ENOENT || inode->i_nlink) {
			if (!ret) {
				ret = btrfs_drop_verity_items(BTRFS_I(inode));
				iput(inode);
				if (ret)
					goto out;
			}
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

		nr_unlink++;

		/* this will do delete_inode and everything for us */
		iput(inode);
	}
	/* release the path since we're done with it */
	btrfs_release_path(path);

	root->orphan_cleanup_state = ORPHAN_CLEANUP_DONE;

	if (test_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &root->state)) {
		trans = btrfs_join_transaction(root);
		if (!IS_ERR(trans))
			btrfs_end_transaction(trans);
	}

	if (nr_unlink)
		btrfs_debug(fs_info, "unlinked %d orphans", nr_unlink);

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
static int btrfs_read_locked_inode(struct inode *inode,
				   struct btrfs_path *in_path)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_path *path = in_path;
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

	if (!path) {
		path = btrfs_alloc_path();
		if (!path)
			return -ENOMEM;
	}

	memcpy(&location, &BTRFS_I(inode)->location, sizeof(location));

	ret = btrfs_lookup_inode(NULL, root, path, &location, 0);
	if (ret) {
		if (path != in_path)
			btrfs_free_path(path);
		return ret;
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
	btrfs_i_size_write(BTRFS_I(inode), btrfs_inode_size(leaf, inode_item));
	btrfs_inode_set_file_extent_range(BTRFS_I(inode), 0,
			round_up(i_size_read(inode), fs_info->sectorsize));

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

	inode_set_iversion_queried(inode,
				   btrfs_inode_sequence(leaf, inode_item));
	inode->i_generation = BTRFS_I(inode)->generation;
	inode->i_rdev = 0;
	rdev = btrfs_inode_rdev(leaf, inode_item);

	BTRFS_I(inode)->index_cnt = (u64)-1;
	btrfs_inode_split_flags(btrfs_inode_flags(leaf, inode_item),
				&BTRFS_I(inode)->flags, &BTRFS_I(inode)->ro_flags);

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

	/*
	 * Same logic as for last_unlink_trans. We don't persist the generation
	 * of the last transaction where this inode was used for a reflink
	 * operation, so after eviction and reloading the inode we must be
	 * pessimistic and assume the last transaction that modified the inode.
	 */
	BTRFS_I(inode)->last_reflink_trans = BTRFS_I(inode)->last_trans;

	path->slots[0]++;
	if (inode->i_nlink != 1 ||
	    path->slots[0] >= btrfs_header_nritems(leaf))
		goto cache_acl;

	btrfs_item_key_to_cpu(leaf, &location, path->slots[0]);
	if (location.objectid != btrfs_ino(BTRFS_I(inode)))
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
			btrfs_ino(BTRFS_I(inode)), &first_xattr_slot);
	if (first_xattr_slot != -1) {
		path->slots[0] = first_xattr_slot;
		ret = btrfs_load_inode_props(inode, path);
		if (ret)
			btrfs_err(fs_info,
				  "error loading props for ino %llu (root %llu): %d",
				  btrfs_ino(BTRFS_I(inode)),
				  root->root_key.objectid, ret);
	}
	if (path != in_path)
		btrfs_free_path(path);

	if (!maybe_acls)
		cache_no_acl(inode);

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		break;
	case S_IFDIR:
		inode->i_fop = &btrfs_dir_file_operations;
		inode->i_op = &btrfs_dir_inode_operations;
		break;
	case S_IFLNK:
		inode->i_op = &btrfs_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &btrfs_aops;
		break;
	default:
		inode->i_op = &btrfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		break;
	}

	btrfs_sync_inode_flags_to_i_flags(inode);
	return 0;
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
	u64 flags;

	btrfs_init_map_token(&token, leaf);

	btrfs_set_token_inode_uid(&token, item, i_uid_read(inode));
	btrfs_set_token_inode_gid(&token, item, i_gid_read(inode));
	btrfs_set_token_inode_size(&token, item, BTRFS_I(inode)->disk_i_size);
	btrfs_set_token_inode_mode(&token, item, inode->i_mode);
	btrfs_set_token_inode_nlink(&token, item, inode->i_nlink);

	btrfs_set_token_timespec_sec(&token, &item->atime,
				     inode->i_atime.tv_sec);
	btrfs_set_token_timespec_nsec(&token, &item->atime,
				      inode->i_atime.tv_nsec);

	btrfs_set_token_timespec_sec(&token, &item->mtime,
				     inode->i_mtime.tv_sec);
	btrfs_set_token_timespec_nsec(&token, &item->mtime,
				      inode->i_mtime.tv_nsec);

	btrfs_set_token_timespec_sec(&token, &item->ctime,
				     inode->i_ctime.tv_sec);
	btrfs_set_token_timespec_nsec(&token, &item->ctime,
				      inode->i_ctime.tv_nsec);

	btrfs_set_token_timespec_sec(&token, &item->otime,
				     BTRFS_I(inode)->i_otime.tv_sec);
	btrfs_set_token_timespec_nsec(&token, &item->otime,
				      BTRFS_I(inode)->i_otime.tv_nsec);

	btrfs_set_token_inode_nbytes(&token, item, inode_get_bytes(inode));
	btrfs_set_token_inode_generation(&token, item,
					 BTRFS_I(inode)->generation);
	btrfs_set_token_inode_sequence(&token, item, inode_peek_iversion(inode));
	btrfs_set_token_inode_transid(&token, item, trans->transid);
	btrfs_set_token_inode_rdev(&token, item, inode->i_rdev);
	flags = btrfs_inode_combine_flags(BTRFS_I(inode)->flags,
					  BTRFS_I(inode)->ro_flags);
	btrfs_set_token_inode_flags(&token, item, flags);
	btrfs_set_token_inode_block_group(&token, item, 0);
}

/*
 * copy everything in the in-memory inode into the btree.
 */
static noinline int btrfs_update_inode_item(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_lookup_inode(trans, root, path, &inode->location, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);

	fill_inode_item(trans, leaf, inode_item, &inode->vfs_inode);
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
				struct btrfs_root *root,
				struct btrfs_inode *inode)
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
	    && !btrfs_is_data_reloc_root(root)
	    && !test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags)) {
		btrfs_update_root_times(trans, root);

		ret = btrfs_delayed_update_inode(trans, root, inode);
		if (!ret)
			btrfs_set_inode_last_trans(trans, inode);
		return ret;
	}

	return btrfs_update_inode_item(trans, root, inode);
}

int btrfs_update_inode_fallback(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct btrfs_inode *inode)
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
				struct btrfs_inode *dir,
				struct btrfs_inode *inode,
				const char *name, int name_len)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	int ret = 0;
	struct btrfs_dir_item *di;
	u64 index;
	u64 ino = btrfs_ino(inode);
	u64 dir_ino = btrfs_ino(dir);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	di = btrfs_lookup_dir_item(trans, root, path, dir_ino,
				    name, name_len, -1);
	if (IS_ERR_OR_NULL(di)) {
		ret = di ? PTR_ERR(di) : -ENOENT;
		goto err;
	}
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
	if (inode->dir_index) {
		ret = btrfs_delayed_delete_inode_ref(inode);
		if (!ret) {
			index = inode->dir_index;
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
	ret = btrfs_delete_delayed_dir_index(trans, dir, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto err;
	}

	ret = btrfs_del_inode_ref_in_log(trans, root, name, name_len, inode,
			dir_ino);
	if (ret != 0 && ret != -ENOENT) {
		btrfs_abort_transaction(trans, ret);
		goto err;
	}

	ret = btrfs_del_dir_entries_in_log(trans, root, name, name_len, dir,
			index);
	if (ret == -ENOENT)
		ret = 0;
	else if (ret)
		btrfs_abort_transaction(trans, ret);

	/*
	 * If we have a pending delayed iput we could end up with the final iput
	 * being run in btrfs-cleaner context.  If we have enough of these built
	 * up we can end up burning a lot of time in btrfs-cleaner without any
	 * way to throttle the unlinks.  Since we're currently holding a ref on
	 * the inode we can run the delayed iput here without any issues as the
	 * final iput won't be done until after we drop the ref we're currently
	 * holding.
	 */
	btrfs_run_delayed_iput(fs_info, inode);
err:
	btrfs_free_path(path);
	if (ret)
		goto out;

	btrfs_i_size_write(dir, dir->vfs_inode.i_size - name_len * 2);
	inode_inc_iversion(&inode->vfs_inode);
	inode_inc_iversion(&dir->vfs_inode);
	inode->vfs_inode.i_ctime = dir->vfs_inode.i_mtime =
		dir->vfs_inode.i_ctime = current_time(&inode->vfs_inode);
	ret = btrfs_update_inode(trans, root, dir);
out:
	return ret;
}

int btrfs_unlink_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       struct btrfs_inode *dir, struct btrfs_inode *inode,
		       const char *name, int name_len)
{
	int ret;
	ret = __btrfs_unlink_inode(trans, root, dir, inode, name, name_len);
	if (!ret) {
		drop_nlink(&inode->vfs_inode);
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
	return btrfs_start_transaction_fallback_global_rsv(root, 5);
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

	btrfs_record_unlink_dir(trans, BTRFS_I(dir), BTRFS_I(d_inode(dentry)),
			0);

	ret = btrfs_unlink_inode(trans, root, BTRFS_I(dir),
			BTRFS_I(d_inode(dentry)), dentry->d_name.name,
			dentry->d_name.len);
	if (ret)
		goto out;

	if (inode->i_nlink == 0) {
		ret = btrfs_orphan_add(trans, BTRFS_I(inode));
		if (ret)
			goto out;
	}

out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(root->fs_info);
	return ret;
}

static int btrfs_unlink_subvol(struct btrfs_trans_handle *trans,
			       struct inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_inode *inode = BTRFS_I(d_inode(dentry));
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	const char *name = dentry->d_name.name;
	int name_len = dentry->d_name.len;
	u64 index;
	int ret;
	u64 objectid;
	u64 dir_ino = btrfs_ino(BTRFS_I(dir));

	if (btrfs_ino(inode) == BTRFS_FIRST_FREE_OBJECTID) {
		objectid = inode->root->root_key.objectid;
	} else if (btrfs_ino(inode) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID) {
		objectid = inode->location.objectid;
	} else {
		WARN_ON(1);
		return -EINVAL;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	di = btrfs_lookup_dir_item(trans, root, path, dir_ino,
				   name, name_len, -1);
	if (IS_ERR_OR_NULL(di)) {
		ret = di ? PTR_ERR(di) : -ENOENT;
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

	/*
	 * This is a placeholder inode for a subvolume we didn't have a
	 * reference to at the time of the snapshot creation.  In the meantime
	 * we could have renamed the real subvol link into our snapshot, so
	 * depending on btrfs_del_root_ref to return -ENOENT here is incorrect.
	 * Instead simply lookup the dir_index_item for this entry so we can
	 * remove it.  Otherwise we know we have a ref to the root and we can
	 * call btrfs_del_root_ref, and it _shouldn't_ fail.
	 */
	if (btrfs_ino(inode) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID) {
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
		index = key.offset;
		btrfs_release_path(path);
	} else {
		ret = btrfs_del_root_ref(trans, objectid,
					 root->root_key.objectid, dir_ino,
					 &index, name, name_len);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	}

	ret = btrfs_delete_delayed_dir_index(trans, BTRFS_I(dir), index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	btrfs_i_size_write(BTRFS_I(dir), dir->i_size - name_len * 2);
	inode_inc_iversion(dir);
	dir->i_mtime = dir->i_ctime = current_time(dir);
	ret = btrfs_update_inode_fallback(trans, root, BTRFS_I(dir));
	if (ret)
		btrfs_abort_transaction(trans, ret);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Helper to check if the subvolume references other subvolumes or if it's
 * default.
 */
static noinline int may_destroy_subvol(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 dir_id;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/* Make sure this root isn't set as the default subvol */
	dir_id = btrfs_super_root_dir(fs_info->super_copy);
	di = btrfs_lookup_dir_item(NULL, fs_info->tree_root, path,
				   dir_id, "default", 7, 0);
	if (di && !IS_ERR(di)) {
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &key);
		if (key.objectid == root->root_key.objectid) {
			ret = -EPERM;
			btrfs_err(fs_info,
				  "deleting default subvolume %llu is not allowed",
				  key.objectid);
			goto out;
		}
		btrfs_release_path(path);
	}

	key.objectid = root->root_key.objectid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);

	ret = 0;
	if (path->slots[0] > 0) {
		path->slots[0]--;
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == root->root_key.objectid &&
		    key.type == BTRFS_ROOT_REF_KEY)
			ret = -ENOTEMPTY;
	}
out:
	btrfs_free_path(path);
	return ret;
}

/* Delete all dentries for inodes belonging to the root */
static void btrfs_prune_dentries(struct btrfs_root *root)
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

		if (objectid < btrfs_ino(entry))
			node = node->rb_left;
		else if (objectid > btrfs_ino(entry))
			node = node->rb_right;
		else
			break;
	}
	if (!node) {
		while (prev) {
			entry = rb_entry(prev, struct btrfs_inode, rb_node);
			if (objectid <= btrfs_ino(entry)) {
				node = prev;
				break;
			}
			prev = rb_next(prev);
		}
	}
	while (node) {
		entry = rb_entry(node, struct btrfs_inode, rb_node);
		objectid = btrfs_ino(entry) + 1;
		inode = igrab(&entry->vfs_inode);
		if (inode) {
			spin_unlock(&root->inode_lock);
			if (atomic_read(&inode->i_count) > 1)
				d_prune_aliases(inode);
			/*
			 * btrfs_drop_inode will have it removed from the inode
			 * cache when its usage count hits zero.
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

int btrfs_delete_subvolume(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dentry->d_sb);
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = d_inode(dentry);
	struct btrfs_root *dest = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct btrfs_block_rsv block_rsv;
	u64 root_flags;
	int ret;

	/*
	 * Don't allow to delete a subvolume with send in progress. This is
	 * inside the inode lock so the error handling that has to drop the bit
	 * again is not run concurrently.
	 */
	spin_lock(&dest->root_item_lock);
	if (dest->send_in_progress) {
		spin_unlock(&dest->root_item_lock);
		btrfs_warn(fs_info,
			   "attempt to delete subvolume %llu during send",
			   dest->root_key.objectid);
		return -EPERM;
	}
	root_flags = btrfs_root_flags(&dest->root_item);
	btrfs_set_root_flags(&dest->root_item,
			     root_flags | BTRFS_ROOT_SUBVOL_DEAD);
	spin_unlock(&dest->root_item_lock);

	down_write(&fs_info->subvol_sem);

	ret = may_destroy_subvol(dest);
	if (ret)
		goto out_up_write;

	btrfs_init_block_rsv(&block_rsv, BTRFS_BLOCK_RSV_TEMP);
	/*
	 * One for dir inode,
	 * two for dir entries,
	 * two for root ref/backref.
	 */
	ret = btrfs_subvolume_reserve_metadata(root, &block_rsv, 5, true);
	if (ret)
		goto out_up_write;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_release;
	}
	trans->block_rsv = &block_rsv;
	trans->bytes_reserved = block_rsv.size;

	btrfs_record_snapshot_destroy(trans, BTRFS_I(dir));

	ret = btrfs_unlink_subvol(trans, dir, dentry);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_end_trans;
	}

	ret = btrfs_record_root_in_trans(trans, dest);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_end_trans;
	}

	memset(&dest->root_item.drop_progress, 0,
		sizeof(dest->root_item.drop_progress));
	btrfs_set_root_drop_level(&dest->root_item, 0);
	btrfs_set_root_refs(&dest->root_item, 0);

	if (!test_and_set_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &dest->state)) {
		ret = btrfs_insert_orphan_item(trans,
					fs_info->tree_root,
					dest->root_key.objectid);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	}

	ret = btrfs_uuid_tree_remove(trans, dest->root_item.uuid,
				  BTRFS_UUID_KEY_SUBVOL,
				  dest->root_key.objectid);
	if (ret && ret != -ENOENT) {
		btrfs_abort_transaction(trans, ret);
		goto out_end_trans;
	}
	if (!btrfs_is_empty_uuid(dest->root_item.received_uuid)) {
		ret = btrfs_uuid_tree_remove(trans,
					  dest->root_item.received_uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  dest->root_key.objectid);
		if (ret && ret != -ENOENT) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	}

	free_anon_bdev(dest->anon_dev);
	dest->anon_dev = 0;
out_end_trans:
	trans->block_rsv = NULL;
	trans->bytes_reserved = 0;
	ret = btrfs_end_transaction(trans);
	inode->i_flags |= S_DEAD;
out_release:
	btrfs_subvolume_release_metadata(root, &block_rsv);
out_up_write:
	up_write(&fs_info->subvol_sem);
	if (ret) {
		spin_lock(&dest->root_item_lock);
		root_flags = btrfs_root_flags(&dest->root_item);
		btrfs_set_root_flags(&dest->root_item,
				root_flags & ~BTRFS_ROOT_SUBVOL_DEAD);
		spin_unlock(&dest->root_item_lock);
	} else {
		d_invalidate(dentry);
		btrfs_prune_dentries(dest);
		ASSERT(dest->send_in_progress == 0);
	}

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
	if (btrfs_ino(BTRFS_I(inode)) == BTRFS_FIRST_FREE_OBJECTID)
		return btrfs_delete_subvolume(dir, dentry);

	trans = __unlink_start_trans(dir);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	if (unlikely(btrfs_ino(BTRFS_I(inode)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
		err = btrfs_unlink_subvol(trans, dir, dentry);
		goto out;
	}

	err = btrfs_orphan_add(trans, BTRFS_I(inode));
	if (err)
		goto out;

	last_unlink_trans = BTRFS_I(inode)->last_unlink_trans;

	/* now the directory is empty */
	err = btrfs_unlink_inode(trans, root, BTRFS_I(dir),
			BTRFS_I(d_inode(dentry)), dentry->d_name.name,
			dentry->d_name.len);
	if (!err) {
		btrfs_i_size_write(BTRFS_I(inode), 0);
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

/*
 * Return this if we need to call truncate_block for the last bit of the
 * truncate.
 */
#define NEED_TRUNCATE_BLOCK 1

/*
 * Remove inode items from a given root.
 *
 * @trans:		A transaction handle.
 * @root:		The root from which to remove items.
 * @inode:		The inode whose items we want to remove.
 * @new_size:		The new i_size for the inode. This is only applicable when
 *			@min_type is BTRFS_EXTENT_DATA_KEY, must be 0 otherwise.
 * @min_type:		The minimum key type to remove. All keys with a type
 *			greater than this value are removed and all keys with
 *			this type are removed only if their offset is >= @new_size.
 * @extents_found:	Output parameter that will contain the number of file
 *			extent items that were removed or adjusted to the new
 *			inode i_size. The caller is responsible for initializing
 *			the counter. Also, it can be NULL if the caller does not
 *			need this counter.
 *
 * Remove all keys associated with the inode from the given root that have a key
 * with a type greater than or equals to @min_type. When @min_type has a value of
 * BTRFS_EXTENT_DATA_KEY, only remove file extent items that have an offset value
 * greater than or equals to @new_size. If a file extent item that starts before
 * @new_size and ends after it is found, its length is adjusted.
 *
 * Returns: 0 on success, < 0 on error and NEED_TRUNCATE_BLOCK when @min_type is
 * BTRFS_EXTENT_DATA_KEY and the caller must truncate the last block.
 */
int btrfs_truncate_inode_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_inode *inode,
			       u64 new_size, u32 min_type,
			       u64 *extents_found)
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
	u64 ino = btrfs_ino(inode);
	u64 bytes_deleted = 0;
	bool be_nice = false;
	bool should_throttle = false;
	const u64 lock_start = ALIGN_DOWN(new_size, fs_info->sectorsize);
	struct extent_state *cached_state = NULL;

	BUG_ON(new_size > 0 && min_type != BTRFS_EXTENT_DATA_KEY);

	/*
	 * For non-free space inodes and non-shareable roots, we want to back
	 * off from time to time.  This means all inodes in subvolume roots,
	 * reloc roots, and data reloc roots.
	 */
	if (!btrfs_is_free_space_inode(inode) &&
	    test_bit(BTRFS_ROOT_SHAREABLE, &root->state))
		be_nice = true;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_BACK;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		lock_extent_bits(&inode->io_tree, lock_start, (u64)-1,
				 &cached_state);

		/*
		 * We want to drop from the next block forward in case this
		 * new size is not block aligned since we will be keeping the
		 * last block of the extent just the way it is.
		 */
		btrfs_drop_extent_cache(inode, ALIGN(new_size,
					fs_info->sectorsize),
					(u64)-1, 0);
	}

	/*
	 * This function is also used to drop the items in the log tree before
	 * we relog the inode, so if root != BTRFS_I(inode)->root, it means
	 * it is used to drop the logged items. So we shouldn't kill the delayed
	 * items.
	 */
	if (min_type == 0 && root == inode->root)
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
	if (be_nice && bytes_deleted > SZ_32M &&
	    btrfs_should_end_transaction(trans)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = 0;
		/* there are no items in the tree for us to truncate, we're
		 * done
		 */
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}

	while (1) {
		u64 clear_start = 0, clear_len = 0;

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

				trace_btrfs_truncate_show_fi_regular(
					inode, leaf, fi, found_key.offset);
			} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
				item_end += btrfs_file_extent_ram_bytes(leaf,
									fi);

				trace_btrfs_truncate_show_fi_inline(
					inode, leaf, fi, path->slots[0],
					found_key.offset);
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

		if (extents_found != NULL)
			(*extents_found)++;

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			u64 num_dec;

			clear_start = found_key.offset;
			extent_start = btrfs_file_extent_disk_bytenr(leaf, fi);
			if (!del_item) {
				u64 orig_num_bytes =
					btrfs_file_extent_num_bytes(leaf, fi);
				extent_num_bytes = ALIGN(new_size -
						found_key.offset,
						fs_info->sectorsize);
				clear_start = ALIGN(new_size, fs_info->sectorsize);
				btrfs_set_file_extent_num_bytes(leaf, fi,
							 extent_num_bytes);
				num_dec = (orig_num_bytes -
					   extent_num_bytes);
				if (test_bit(BTRFS_ROOT_SHAREABLE,
					     &root->state) &&
				    extent_start != 0)
					inode_sub_bytes(&inode->vfs_inode,
							num_dec);
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
					if (test_bit(BTRFS_ROOT_SHAREABLE,
						     &root->state))
						inode_sub_bytes(&inode->vfs_inode,
								num_dec);
				}
			}
			clear_len = num_dec;
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			/*
			 * we can't truncate inline items that have had
			 * special encodings
			 */
			if (!del_item &&
			    btrfs_file_extent_encryption(leaf, fi) == 0 &&
			    btrfs_file_extent_other_encoding(leaf, fi) == 0 &&
			    btrfs_file_extent_compression(leaf, fi) == 0) {
				u32 size = (u32)(new_size - found_key.offset);

				btrfs_set_file_extent_ram_bytes(leaf, fi, size);
				size = btrfs_file_extent_calc_inline_size(size);
				btrfs_truncate_item(path, size, 1);
			} else if (!del_item) {
				/*
				 * We have to bail so the last_size is set to
				 * just before this extent.
				 */
				ret = NEED_TRUNCATE_BLOCK;
				break;
			} else {
				/*
				 * Inline extents are special, we just treat
				 * them as a full sector worth in the file
				 * extent tree just for simplicity sake.
				 */
				clear_len = fs_info->sectorsize;
			}

			if (test_bit(BTRFS_ROOT_SHAREABLE, &root->state))
				inode_sub_bytes(&inode->vfs_inode,
						item_end + 1 - new_size);
		}
delete:
		/*
		 * We use btrfs_truncate_inode_items() to clean up log trees for
		 * multiple fsyncs, and in this case we don't want to clear the
		 * file extent range because it's just the log.
		 */
		if (root == inode->root) {
			ret = btrfs_inode_clear_file_extent_range(inode,
						  clear_start, clear_len);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				break;
			}
		}

		if (del_item)
			last_size = found_key.offset;
		else
			last_size = new_size;
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
		should_throttle = false;

		if (found_extent &&
		    root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
			struct btrfs_ref ref = { 0 };

			bytes_deleted += extent_num_bytes;

			btrfs_init_generic_ref(&ref, BTRFS_DROP_DELAYED_REF,
					extent_start, extent_num_bytes, 0);
			ref.real_root = root->root_key.objectid;
			btrfs_init_data_ref(&ref, btrfs_header_owner(leaf),
					ino, extent_offset);
			ret = btrfs_free_extent(trans, &ref);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				break;
			}
			if (be_nice) {
				if (btrfs_should_throttle_delayed_refs(trans))
					should_throttle = true;
			}
		}

		if (found_type == BTRFS_INODE_ITEM_KEY)
			break;

		if (path->slots[0] == 0 ||
		    path->slots[0] != pending_del_slot ||
		    should_throttle) {
			if (pending_del_nr) {
				ret = btrfs_del_items(trans, root, path,
						pending_del_slot,
						pending_del_nr);
				if (ret) {
					btrfs_abort_transaction(trans, ret);
					break;
				}
				pending_del_nr = 0;
			}
			btrfs_release_path(path);

			/*
			 * We can generate a lot of delayed refs, so we need to
			 * throttle every once and a while and make sure we're
			 * adding enough space to keep up with the work we are
			 * generating.  Since we hold a transaction here we
			 * can't flush, and we don't want to FLUSH_LIMIT because
			 * we could have generated too many delayed refs to
			 * actually allocate, so just bail if we're short and
			 * let the normal reservation dance happen higher up.
			 */
			if (should_throttle) {
				ret = btrfs_delayed_refs_rsv_refill(fs_info,
							BTRFS_RESERVE_NO_FLUSH);
				if (ret) {
					ret = -EAGAIN;
					break;
				}
			}
			goto search_again;
		} else {
			path->slots[0]--;
		}
	}
out:
	if (ret >= 0 && pending_del_nr) {
		int err;

		err = btrfs_del_items(trans, root, path, pending_del_slot,
				      pending_del_nr);
		if (err) {
			btrfs_abort_transaction(trans, err);
			ret = err;
		}
	}
	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		ASSERT(last_size >= new_size);
		if (!ret && last_size > new_size)
			last_size = new_size;
		btrfs_inode_safe_disk_i_size_write(inode, last_size);
		unlock_extent_cached(&inode->io_tree, lock_start, (u64)-1,
				     &cached_state);
	}

	btrfs_free_path(path);
	return ret;
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
int btrfs_truncate_block(struct btrfs_inode *inode, loff_t from, loff_t len,
			 int front)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct address_space *mapping = inode->vfs_inode.i_mapping;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_changeset *data_reserved = NULL;
	bool only_release_metadata = false;
	u32 blocksize = fs_info->sectorsize;
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (blocksize - 1);
	struct page *page;
	gfp_t mask = btrfs_alloc_write_mask(mapping);
	size_t write_bytes = blocksize;
	int ret = 0;
	u64 block_start;
	u64 block_end;

	if (IS_ALIGNED(offset, blocksize) &&
	    (!len || IS_ALIGNED(len, blocksize)))
		goto out;

	block_start = round_down(from, blocksize);
	block_end = block_start + blocksize - 1;

	ret = btrfs_check_data_free_space(inode, &data_reserved, block_start,
					  blocksize);
	if (ret < 0) {
		if (btrfs_check_nocow_lock(inode, block_start, &write_bytes) > 0) {
			/* For nocow case, no need to reserve data space */
			only_release_metadata = true;
		} else {
			goto out;
		}
	}
	ret = btrfs_delalloc_reserve_metadata(inode, blocksize);
	if (ret < 0) {
		if (!only_release_metadata)
			btrfs_free_reserved_data_space(inode, data_reserved,
						       block_start, blocksize);
		goto out;
	}
again:
	page = find_or_create_page(mapping, index, mask);
	if (!page) {
		btrfs_delalloc_release_space(inode, data_reserved, block_start,
					     blocksize, true);
		btrfs_delalloc_release_extents(inode, blocksize);
		ret = -ENOMEM;
		goto out;
	}
	ret = set_page_extent_mapped(page);
	if (ret < 0)
		goto out_unlock;

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

	ordered = btrfs_lookup_ordered_extent(inode, block_start);
	if (ordered) {
		unlock_extent_cached(io_tree, block_start, block_end,
				     &cached_state);
		unlock_page(page);
		put_page(page);
		btrfs_start_ordered_extent(ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	clear_extent_bit(&inode->io_tree, block_start, block_end,
			 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			 0, 0, &cached_state);

	ret = btrfs_set_extent_delalloc(inode, block_start, block_end, 0,
					&cached_state);
	if (ret) {
		unlock_extent_cached(io_tree, block_start, block_end,
				     &cached_state);
		goto out_unlock;
	}

	if (offset != blocksize) {
		if (!len)
			len = blocksize - offset;
		if (front)
			memzero_page(page, (block_start - page_offset(page)),
				     offset);
		else
			memzero_page(page, (block_start - page_offset(page)) + offset,
				     len);
		flush_dcache_page(page);
	}
	ClearPageChecked(page);
	btrfs_page_set_dirty(fs_info, page, block_start, block_end + 1 - block_start);
	unlock_extent_cached(io_tree, block_start, block_end, &cached_state);

	if (only_release_metadata)
		set_extent_bit(&inode->io_tree, block_start, block_end,
			       EXTENT_NORESERVE, 0, NULL, NULL, GFP_NOFS, NULL);

out_unlock:
	if (ret) {
		if (only_release_metadata)
			btrfs_delalloc_release_metadata(inode, blocksize, true);
		else
			btrfs_delalloc_release_space(inode, data_reserved,
					block_start, blocksize, true);
	}
	btrfs_delalloc_release_extents(inode, blocksize);
	unlock_page(page);
	put_page(page);
out:
	if (only_release_metadata)
		btrfs_check_nocow_unlock(inode);
	extent_changeset_free(data_reserved);
	return ret;
}

static int maybe_insert_hole(struct btrfs_root *root, struct btrfs_inode *inode,
			     u64 offset, u64 len)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_drop_extents_args drop_args = { 0 };
	int ret;

	/*
	 * If NO_HOLES is enabled, we don't need to do anything.
	 * Later, up in the call chain, either btrfs_set_inode_last_sub_trans()
	 * or btrfs_update_inode() will be called, which guarantee that the next
	 * fsync will know this inode was changed and needs to be logged.
	 */
	if (btrfs_fs_incompat(fs_info, NO_HOLES))
		return 0;

	/*
	 * 1 - for the one we're dropping
	 * 1 - for the one we're adding
	 * 1 - for updating the inode.
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	drop_args.start = offset;
	drop_args.end = offset + len;
	drop_args.drop_cache = true;

	ret = btrfs_drop_extents(trans, root, inode, &drop_args);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		return ret;
	}

	ret = btrfs_insert_file_extent(trans, root, btrfs_ino(inode),
			offset, 0, 0, len, 0, len, 0, 0, 0);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
	} else {
		btrfs_update_inode_bytes(inode, 0, drop_args.bytes_found);
		btrfs_update_inode(trans, root, inode);
	}
	btrfs_end_transaction(trans);
	return ret;
}

/*
 * This function puts in dummy file extents for the area we're creating a hole
 * for.  So if we are truncating this file to a larger size we need to insert
 * these file extents so that btrfs_get_extent will return a EXTENT_MAP_HOLE for
 * the range between oldsize and size
 */
int btrfs_cont_expand(struct btrfs_inode *inode, loff_t oldsize, loff_t size)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct extent_map *em = NULL;
	struct extent_state *cached_state = NULL;
	struct extent_map_tree *em_tree = &inode->extent_tree;
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

	btrfs_lock_and_flush_ordered_range(inode, hole_start, block_end - 1,
					   &cached_state);
	cur_offset = hole_start;
	while (1) {
		em = btrfs_get_extent(inode, NULL, 0, cur_offset,
				      block_end - cur_offset);
		if (IS_ERR(em)) {
			err = PTR_ERR(em);
			em = NULL;
			break;
		}
		last_byte = min(extent_map_end(em), block_end);
		last_byte = ALIGN(last_byte, fs_info->sectorsize);
		hole_size = last_byte - cur_offset;

		if (!test_bit(EXTENT_FLAG_PREALLOC, &em->flags)) {
			struct extent_map *hole_em;

			err = maybe_insert_hole(root, inode, cur_offset,
						hole_size);
			if (err)
				break;

			err = btrfs_inode_set_file_extent_range(inode,
							cur_offset, hole_size);
			if (err)
				break;

			btrfs_drop_extent_cache(inode, cur_offset,
						cur_offset + hole_size - 1, 0);
			hole_em = alloc_extent_map();
			if (!hole_em) {
				set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
					&inode->runtime_flags);
				goto next;
			}
			hole_em->start = cur_offset;
			hole_em->len = hole_size;
			hole_em->orig_start = cur_offset;

			hole_em->block_start = EXTENT_MAP_HOLE;
			hole_em->block_len = 0;
			hole_em->orig_block_len = 0;
			hole_em->ram_bytes = hole_size;
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
		} else {
			err = btrfs_inode_set_file_extent_range(inode,
							cur_offset, hole_size);
			if (err)
				break;
		}
next:
		free_extent_map(em);
		em = NULL;
		cur_offset = last_byte;
		if (cur_offset >= block_end)
			break;
	}
	free_extent_map(em);
	unlock_extent_cached(io_tree, hole_start, block_end - 1, &cached_state);
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
		 * Don't do an expanding truncate while snapshotting is ongoing.
		 * This is to ensure the snapshot captures a fully consistent
		 * state of this file - if the snapshot captures this expanding
		 * truncation, it must capture all writes that happened before
		 * this truncation.
		 */
		btrfs_drew_write_lock(&root->snapshot_lock);
		ret = btrfs_cont_expand(BTRFS_I(inode), oldsize, newsize);
		if (ret) {
			btrfs_drew_write_unlock(&root->snapshot_lock);
			return ret;
		}

		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			btrfs_drew_write_unlock(&root->snapshot_lock);
			return PTR_ERR(trans);
		}

		i_size_write(inode, newsize);
		btrfs_inode_safe_disk_i_size_write(BTRFS_I(inode), 0);
		pagecache_isize_extended(inode, oldsize, newsize);
		ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
		btrfs_drew_write_unlock(&root->snapshot_lock);
		btrfs_end_transaction(trans);
	} else {
		struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);

		if (btrfs_is_zoned(fs_info)) {
			ret = btrfs_wait_ordered_range(inode,
					ALIGN(newsize, fs_info->sectorsize),
					(u64)-1);
			if (ret)
				return ret;
		}

		/*
		 * We're truncating a file that used to have good data down to
		 * zero. Make sure any new writes to the file get on disk
		 * on close.
		 */
		if (newsize == 0)
			set_bit(BTRFS_INODE_FLUSH_ON_CLOSE,
				&BTRFS_I(inode)->runtime_flags);

		truncate_setsize(inode, newsize);

		inode_dio_wait(inode);

		ret = btrfs_truncate(inode, newsize == oldsize);
		if (ret && inode->i_nlink) {
			int err;

			/*
			 * Truncate failed, so fix up the in-memory size. We
			 * adjusted disk_i_size down as we removed extents, so
			 * wait for disk_i_size to be stable and then update the
			 * in-memory size to match.
			 */
			err = btrfs_wait_ordered_range(inode, 0, (u64)-1);
			if (err)
				return err;
			i_size_write(inode, BTRFS_I(inode)->disk_i_size);
		}
	}

	return ret;
}

static int btrfs_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
			 struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int err;

	if (btrfs_root_readonly(root))
		return -EROFS;

	err = setattr_prepare(mnt_userns, dentry, attr);
	if (err)
		return err;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		err = btrfs_setsize(inode, attr);
		if (err)
			return err;
	}

	if (attr->ia_valid) {
		setattr_copy(mnt_userns, inode, attr);
		inode_inc_iversion(inode);
		err = btrfs_dirty_inode(inode);

		if (!err && attr->ia_valid & ATTR_MODE)
			err = posix_acl_chmod(mnt_userns, inode, inode->i_mode);
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
	while (!RB_EMPTY_ROOT(&map_tree->map.rb_root)) {
		struct extent_map *em;

		node = rb_first_cached(&map_tree->map);
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
	 * We can have ongoing bios started by readahead that have
	 * their endio callback (extent_io.c:end_bio_extent_readpage)
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
		unsigned state_flags;

		node = rb_first(&io_tree->state);
		state = rb_entry(node, struct extent_state, rb_node);
		start = state->start;
		end = state->end;
		state_flags = state->state;
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
		if (state_flags & EXTENT_DELALLOC)
			btrfs_qgroup_free_data(BTRFS_I(inode), NULL, start,
					       end - start + 1);

		clear_extent_bit(io_tree, start, end,
				 EXTENT_LOCKED | EXTENT_DELALLOC |
				 EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG, 1, 1,
				 &cached_state);

		cond_resched();
		spin_lock(&io_tree->lock);
	}
	spin_unlock(&io_tree->lock);
}

static struct btrfs_trans_handle *evict_refill_and_join(struct btrfs_root *root,
							struct btrfs_block_rsv *rsv)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	struct btrfs_trans_handle *trans;
	u64 delayed_refs_extra = btrfs_calc_insert_metadata_size(fs_info, 1);
	int ret;

	/*
	 * Eviction should be taking place at some place safe because of our
	 * delayed iputs.  However the normal flushing code will run delayed
	 * iputs, so we cannot use FLUSH_ALL otherwise we'll deadlock.
	 *
	 * We reserve the delayed_refs_extra here again because we can't use
	 * btrfs_start_transaction(root, 0) for the same deadlocky reason as
	 * above.  We reserve our extra bit here because we generate a ton of
	 * delayed refs activity by truncating.
	 *
	 * If we cannot make our reservation we'll attempt to steal from the
	 * global reserve, because we really want to be able to free up space.
	 */
	ret = btrfs_block_rsv_refill(root, rsv, rsv->size + delayed_refs_extra,
				     BTRFS_RESERVE_FLUSH_EVICT);
	if (ret) {
		/*
		 * Try to steal from the global reserve if there is space for
		 * it.
		 */
		if (btrfs_check_space_for_delayed_refs(fs_info) ||
		    btrfs_block_rsv_migrate(global_rsv, rsv, rsv->size, 0)) {
			btrfs_warn(fs_info,
				   "could not allocate space for delete; will truncate on mount");
			return ERR_PTR(-ENOSPC);
		}
		delayed_refs_extra = 0;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return trans;

	if (delayed_refs_extra) {
		trans->block_rsv = &fs_info->trans_block_rsv;
		trans->bytes_reserved = delayed_refs_extra;
		btrfs_block_rsv_migrate(rsv, trans->block_rsv,
					delayed_refs_extra, 1);
	}
	return trans;
}

void btrfs_evict_inode(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *rsv;
	int ret;

	trace_btrfs_inode_evict(inode);

	if (!root) {
		fsverity_cleanup_inode(inode);
		clear_inode(inode);
		return;
	}

	evict_inode_truncate_pages(inode);

	if (inode->i_nlink &&
	    ((btrfs_root_refs(&root->root_item) != 0 &&
	      root->root_key.objectid != BTRFS_ROOT_TREE_OBJECTID) ||
	     btrfs_is_free_space_inode(BTRFS_I(inode))))
		goto no_delete;

	if (is_bad_inode(inode))
		goto no_delete;

	btrfs_free_io_failure_record(BTRFS_I(inode), 0, (u64)-1);

	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		goto no_delete;

	if (inode->i_nlink > 0) {
		BUG_ON(btrfs_root_refs(&root->root_item) != 0 &&
		       root->root_key.objectid != BTRFS_ROOT_TREE_OBJECTID);
		goto no_delete;
	}

	ret = btrfs_commit_inode_delayed_inode(BTRFS_I(inode));
	if (ret)
		goto no_delete;

	rsv = btrfs_alloc_block_rsv(fs_info, BTRFS_BLOCK_RSV_TEMP);
	if (!rsv)
		goto no_delete;
	rsv->size = btrfs_calc_metadata_size(fs_info, 1);
	rsv->failfast = 1;

	btrfs_i_size_write(BTRFS_I(inode), 0);

	while (1) {
		trans = evict_refill_and_join(root, rsv);
		if (IS_ERR(trans))
			goto free_rsv;

		trans->block_rsv = rsv;

		ret = btrfs_truncate_inode_items(trans, root, BTRFS_I(inode),
						 0, 0, NULL);
		trans->block_rsv = &fs_info->trans_block_rsv;
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);
		if (ret && ret != -ENOSPC && ret != -EAGAIN)
			goto free_rsv;
		else if (!ret)
			break;
	}

	/*
	 * Errors here aren't a big deal, it just means we leave orphan items in
	 * the tree. They will be cleaned up on the next mount. If the inode
	 * number gets reused, cleanup deletes the orphan item without doing
	 * anything, and unlink reuses the existing orphan item.
	 *
	 * If it turns out that we are dropping too many of these, we might want
	 * to add a mechanism for retrying these after a commit.
	 */
	trans = evict_refill_and_join(root, rsv);
	if (!IS_ERR(trans)) {
		trans->block_rsv = rsv;
		btrfs_orphan_del(trans, BTRFS_I(inode));
		trans->block_rsv = &fs_info->trans_block_rsv;
		btrfs_end_transaction(trans);
	}

free_rsv:
	btrfs_free_block_rsv(fs_info, rsv);
no_delete:
	/*
	 * If we didn't successfully delete, the orphan item will still be in
	 * the tree and we'll retry on the next mount. Again, we might also want
	 * to retry these periodically in the future.
	 */
	btrfs_remove_delayed_node(BTRFS_I(inode));
	fsverity_cleanup_inode(inode);
	clear_inode(inode);
}

/*
 * Return the key found in the dir entry in the location pointer, fill @type
 * with BTRFS_FT_*, and return 0.
 *
 * If no dir entries were found, returns -ENOENT.
 * If found a corrupted location in dir entry, returns -EUCLEAN.
 */
static int btrfs_inode_by_name(struct inode *dir, struct dentry *dentry,
			       struct btrfs_key *location, u8 *type)
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

	di = btrfs_lookup_dir_item(NULL, root, path, btrfs_ino(BTRFS_I(dir)),
			name, namelen, 0);
	if (IS_ERR_OR_NULL(di)) {
		ret = di ? PTR_ERR(di) : -ENOENT;
		goto out;
	}

	btrfs_dir_item_key_to_cpu(path->nodes[0], di, location);
	if (location->type != BTRFS_INODE_ITEM_KEY &&
	    location->type != BTRFS_ROOT_ITEM_KEY) {
		ret = -EUCLEAN;
		btrfs_warn(root->fs_info,
"%s gets something invalid in DIR_ITEM (name %s, directory ino %llu, location(%llu %u %llu))",
			   __func__, name, btrfs_ino(BTRFS_I(dir)),
			   location->objectid, location->type, location->offset);
	}
	if (!ret)
		*type = btrfs_dir_type(path->nodes[0], di);
out:
	btrfs_free_path(path);
	return ret;
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
	if (btrfs_root_ref_dirid(leaf, ref) != btrfs_ino(BTRFS_I(dir)) ||
	    btrfs_root_ref_name_len(leaf, ref) != dentry->d_name.len)
		goto out;

	ret = memcmp_extent_buffer(leaf, dentry->d_name.name,
				   (unsigned long)(ref + 1),
				   dentry->d_name.len);
	if (ret)
		goto out;

	btrfs_release_path(path);

	new_root = btrfs_get_fs_root(fs_info, location->objectid, true);
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
	u64 ino = btrfs_ino(BTRFS_I(inode));

	if (inode_unhashed(inode))
		return;
	parent = NULL;
	spin_lock(&root->inode_lock);
	p = &root->inode_tree.rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_inode, rb_node);

		if (ino < btrfs_ino(entry))
			p = &parent->rb_left;
		else if (ino > btrfs_ino(entry))
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

static void inode_tree_del(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
	int empty = 0;

	spin_lock(&root->inode_lock);
	if (!RB_EMPTY_NODE(&inode->rb_node)) {
		rb_erase(&inode->rb_node, &root->inode_tree);
		RB_CLEAR_NODE(&inode->rb_node);
		empty = RB_EMPTY_ROOT(&root->inode_tree);
	}
	spin_unlock(&root->inode_lock);

	if (empty && btrfs_root_refs(&root->root_item) == 0) {
		spin_lock(&root->inode_lock);
		empty = RB_EMPTY_ROOT(&root->inode_tree);
		spin_unlock(&root->inode_lock);
		if (empty)
			btrfs_add_dead_root(root);
	}
}


static int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;

	inode->i_ino = args->ino;
	BTRFS_I(inode)->location.objectid = args->ino;
	BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
	BTRFS_I(inode)->location.offset = 0;
	BTRFS_I(inode)->root = btrfs_grab_root(args->root);
	BUG_ON(args->root && !BTRFS_I(inode)->root);
	return 0;
}

static int btrfs_find_actor(struct inode *inode, void *opaque)
{
	struct btrfs_iget_args *args = opaque;

	return args->ino == BTRFS_I(inode)->location.objectid &&
		args->root == BTRFS_I(inode)->root;
}

static struct inode *btrfs_iget_locked(struct super_block *s, u64 ino,
				       struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	unsigned long hashval = btrfs_inode_hash(ino, root);

	args.ino = ino;
	args.root = root;

	inode = iget5_locked(s, hashval, btrfs_find_actor,
			     btrfs_init_locked_inode,
			     (void *)&args);
	return inode;
}

/*
 * Get an inode object given its inode number and corresponding root.
 * Path can be preallocated to prevent recursing back to iget through
 * allocator. NULL is also valid but may require an additional allocation
 * later.
 */
struct inode *btrfs_iget_path(struct super_block *s, u64 ino,
			      struct btrfs_root *root, struct btrfs_path *path)
{
	struct inode *inode;

	inode = btrfs_iget_locked(s, ino, root);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		int ret;

		ret = btrfs_read_locked_inode(inode, path);
		if (!ret) {
			inode_tree_add(inode);
			unlock_new_inode(inode);
		} else {
			iget_failed(inode);
			/*
			 * ret > 0 can come from btrfs_search_slot called by
			 * btrfs_read_locked_inode, this means the inode item
			 * was not found.
			 */
			if (ret > 0)
				ret = -ENOENT;
			inode = ERR_PTR(ret);
		}
	}

	return inode;
}

struct inode *btrfs_iget(struct super_block *s, u64 ino, struct btrfs_root *root)
{
	return btrfs_iget_path(s, ino, root, NULL);
}

static struct inode *new_simple_dir(struct super_block *s,
				    struct btrfs_key *key,
				    struct btrfs_root *root)
{
	struct inode *inode = new_inode(s);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	BTRFS_I(inode)->root = btrfs_grab_root(root);
	memcpy(&BTRFS_I(inode)->location, key, sizeof(*key));
	set_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags);

	inode->i_ino = BTRFS_EMPTY_SUBVOL_DIR_OBJECTID;
	/*
	 * We only need lookup, the rest is read-only and there's no inode
	 * associated with the dentry
	 */
	inode->i_op = &simple_dir_inode_operations;
	inode->i_opflags &= ~IOP_XATTR;
	inode->i_fop = &simple_dir_operations;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO;
	inode->i_mtime = current_time(inode);
	inode->i_atime = inode->i_mtime;
	inode->i_ctime = inode->i_mtime;
	BTRFS_I(inode)->i_otime = inode->i_mtime;

	return inode;
}

static inline u8 btrfs_inode_type(struct inode *inode)
{
	/*
	 * Compile-time asserts that generic FT_* types still match
	 * BTRFS_FT_* types
	 */
	BUILD_BUG_ON(BTRFS_FT_UNKNOWN != FT_UNKNOWN);
	BUILD_BUG_ON(BTRFS_FT_REG_FILE != FT_REG_FILE);
	BUILD_BUG_ON(BTRFS_FT_DIR != FT_DIR);
	BUILD_BUG_ON(BTRFS_FT_CHRDEV != FT_CHRDEV);
	BUILD_BUG_ON(BTRFS_FT_BLKDEV != FT_BLKDEV);
	BUILD_BUG_ON(BTRFS_FT_FIFO != FT_FIFO);
	BUILD_BUG_ON(BTRFS_FT_SOCK != FT_SOCK);
	BUILD_BUG_ON(BTRFS_FT_SYMLINK != FT_SYMLINK);

	return fs_umode_to_ftype(inode->i_mode);
}

struct inode *btrfs_lookup_dentry(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct inode *inode;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location;
	u8 di_type = 0;
	int ret = 0;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(dir, dentry, &location, &di_type);
	if (ret < 0)
		return ERR_PTR(ret);

	if (location.type == BTRFS_INODE_ITEM_KEY) {
		inode = btrfs_iget(dir->i_sb, location.objectid, root);
		if (IS_ERR(inode))
			return inode;

		/* Do extra check against inode mode with di_type */
		if (btrfs_inode_type(inode) != di_type) {
			btrfs_crit(fs_info,
"inode mode mismatch with dir: inode mode=0%o btrfs type=%u dir type=%u",
				  inode->i_mode, btrfs_inode_type(inode),
				  di_type);
			iput(inode);
			return ERR_PTR(-EUCLEAN);
		}
		return inode;
	}

	ret = fixup_tree_root_location(fs_info, dir, dentry,
				       &location, &sub_root);
	if (ret < 0) {
		if (ret != -ENOENT)
			inode = ERR_PTR(ret);
		else
			inode = new_simple_dir(dir->i_sb, &location, sub_root);
	} else {
		inode = btrfs_iget(dir->i_sb, location.objectid, sub_root);
	}
	if (root != sub_root)
		btrfs_put_root(sub_root);

	if (!IS_ERR(inode) && root != sub_root) {
		down_read(&fs_info->cleanup_work_sem);
		if (!sb_rdonly(inode->i_sb))
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

		if (btrfs_ino(BTRFS_I(inode)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)
			return 1;
	}
	return 0;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct inode *inode = btrfs_lookup_dentry(dir, dentry);

	if (inode == ERR_PTR(-ENOENT))
		inode = NULL;
	return d_splice_alias(inode, dentry);
}

/*
 * All this infrastructure exists because dir_emit can fault, and we are holding
 * the tree lock when doing readdir.  For now just allocate a buffer and copy
 * our information into that, and then dir_emit from the buffer.  This is
 * similar to what NFS does, only we don't keep the buffer around in pagecache
 * because I'm afraid I'll mess that up.  Long term we need to make filldir do
 * copy_to_user_inatomic so we don't have to worry about page faulting under the
 * tree lock.
 */
static int btrfs_opendir(struct inode *inode, struct file *file)
{
	struct btrfs_file_private *private;

	private = kzalloc(sizeof(struct btrfs_file_private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;
	private->filldir_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!private->filldir_buf) {
		kfree(private);
		return -ENOMEM;
	}
	file->private_data = private;
	return 0;
}

struct dir_entry {
	u64 ino;
	u64 offset;
	unsigned type;
	int name_len;
};

static int btrfs_filldir(void *addr, int entries, struct dir_context *ctx)
{
	while (entries--) {
		struct dir_entry *entry = addr;
		char *name = (char *)(entry + 1);

		ctx->pos = get_unaligned(&entry->offset);
		if (!dir_emit(ctx, name, get_unaligned(&entry->name_len),
					 get_unaligned(&entry->ino),
					 get_unaligned(&entry->type)))
			return 1;
		addr += sizeof(struct dir_entry) +
			get_unaligned(&entry->name_len);
		ctx->pos++;
	}
	return 0;
}

static int btrfs_real_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_file_private *private = file->private_data;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	void *addr;
	struct list_head ins_list;
	struct list_head del_list;
	int ret;
	struct extent_buffer *leaf;
	int slot;
	char *name_ptr;
	int name_len;
	int entries = 0;
	int total_len = 0;
	bool put = false;
	struct btrfs_key location;

	if (!dir_emit_dots(file, ctx))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	addr = private->filldir_buf;
	path->reada = READA_FORWARD;

	INIT_LIST_HEAD(&ins_list);
	INIT_LIST_HEAD(&del_list);
	put = btrfs_readdir_get_delayed_items(inode, &ins_list, &del_list);

again:
	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = ctx->pos;
	key.objectid = btrfs_ino(BTRFS_I(inode));

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	while (1) {
		struct dir_entry *entry;

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

		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid != key.objectid)
			break;
		if (found_key.type != BTRFS_DIR_INDEX_KEY)
			break;
		if (found_key.offset < ctx->pos)
			goto next;
		if (btrfs_should_delete_dir_index(&del_list, found_key.offset))
			goto next;
		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		name_len = btrfs_dir_name_len(leaf, di);
		if ((total_len + sizeof(struct dir_entry) + name_len) >=
		    PAGE_SIZE) {
			btrfs_release_path(path);
			ret = btrfs_filldir(private->filldir_buf, entries, ctx);
			if (ret)
				goto nopos;
			addr = private->filldir_buf;
			entries = 0;
			total_len = 0;
			goto again;
		}

		entry = addr;
		put_unaligned(name_len, &entry->name_len);
		name_ptr = (char *)(entry + 1);
		read_extent_buffer(leaf, name_ptr, (unsigned long)(di + 1),
				   name_len);
		put_unaligned(fs_ftype_to_dtype(btrfs_dir_type(leaf, di)),
				&entry->type);
		btrfs_dir_item_key_to_cpu(leaf, di, &location);
		put_unaligned(location.objectid, &entry->ino);
		put_unaligned(found_key.offset, &entry->offset);
		entries++;
		addr += sizeof(struct dir_entry) + name_len;
		total_len += sizeof(struct dir_entry) + name_len;
next:
		path->slots[0]++;
	}
	btrfs_release_path(path);

	ret = btrfs_filldir(private->filldir_buf, entries, ctx);
	if (ret)
		goto nopos;

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

	ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
	if (ret && (ret == -ENOSPC || ret == -EDQUOT)) {
		/* whoops, lets try again with the full transaction */
		btrfs_end_transaction(trans);
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
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
static int btrfs_update_time(struct inode *inode, struct timespec64 *now,
			     int flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	bool dirty = flags & ~S_VERSION;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (flags & S_VERSION)
		dirty |= inode_maybe_inc_iversion(inode, dirty);
	if (flags & S_CTIME)
		inode->i_ctime = *now;
	if (flags & S_MTIME)
		inode->i_mtime = *now;
	if (flags & S_ATIME)
		inode->i_atime = *now;
	return dirty ? btrfs_dirty_inode(inode) : 0;
}

/*
 * find the highest existing sequence number in a directory
 * and then set the in-memory index_cnt variable to reflect
 * free sequence numbers
 */
static int btrfs_set_inode_index_count(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
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
		inode->index_cnt = 2;
		goto out;
	}

	path->slots[0]--;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	if (found_key.objectid != btrfs_ino(inode) ||
	    found_key.type != BTRFS_DIR_INDEX_KEY) {
		inode->index_cnt = 2;
		goto out;
	}

	inode->index_cnt = found_key.offset + 1;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper to find a free sequence number in a given directory.  This current
 * code is very simple, later versions will do smarter things in the btree
 */
int btrfs_set_inode_index(struct btrfs_inode *dir, u64 *index)
{
	int ret = 0;

	if (dir->index_cnt == (u64)-1) {
		ret = btrfs_inode_delayed_dir_index_count(dir);
		if (ret) {
			ret = btrfs_set_inode_index_count(dir);
			if (ret)
				return ret;
		}
	}

	*index = dir->index_cnt;
	dir->index_cnt++;

	return ret;
}

static int btrfs_insert_inode_locked(struct inode *inode)
{
	struct btrfs_iget_args args;

	args.ino = BTRFS_I(inode)->location.objectid;
	args.root = BTRFS_I(inode)->root;

	return insert_inode_locked4(inode,
		   btrfs_inode_hash(inode->i_ino, BTRFS_I(inode)->root),
		   btrfs_find_actor, &args);
}

/*
 * Inherit flags from the parent inode.
 *
 * Currently only the compression flags and the cow flags are inherited.
 */
static void btrfs_inherit_iflags(struct inode *inode, struct inode *dir)
{
	unsigned int flags;

	if (!dir)
		return;

	flags = BTRFS_I(dir)->flags;

	if (flags & BTRFS_INODE_NOCOMPRESS) {
		BTRFS_I(inode)->flags &= ~BTRFS_INODE_COMPRESS;
		BTRFS_I(inode)->flags |= BTRFS_INODE_NOCOMPRESS;
	} else if (flags & BTRFS_INODE_COMPRESS) {
		BTRFS_I(inode)->flags &= ~BTRFS_INODE_NOCOMPRESS;
		BTRFS_I(inode)->flags |= BTRFS_INODE_COMPRESS;
	}

	if (flags & BTRFS_INODE_NODATACOW) {
		BTRFS_I(inode)->flags |= BTRFS_INODE_NODATACOW;
		if (S_ISREG(inode->i_mode))
			BTRFS_I(inode)->flags |= BTRFS_INODE_NODATASUM;
	}

	btrfs_sync_inode_flags_to_i_flags(inode);
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct user_namespace *mnt_userns,
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
	unsigned int nofs_flag;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);

	nofs_flag = memalloc_nofs_save();
	inode = new_inode(fs_info->sb);
	memalloc_nofs_restore(nofs_flag);
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

		ret = btrfs_set_inode_index(BTRFS_I(dir), index);
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
	 * btrfs_set_inode_index_count has an explanation for the magic
	 * number
	 */
	BTRFS_I(inode)->index_cnt = 2;
	BTRFS_I(inode)->dir_index = *index;
	BTRFS_I(inode)->root = btrfs_grab_root(root);
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
	if (ret < 0) {
		iput(inode);
		goto fail;
	}

	ret = btrfs_insert_empty_items(trans, root, path, key, sizes, nitems);
	if (ret != 0)
		goto fail_unlock;

	inode_init_owner(mnt_userns, inode, dir, mode);
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
	btrfs_set_inode_last_trans(trans, BTRFS_I(inode));

	btrfs_update_root_times(trans, root);

	ret = btrfs_inode_inherit_props(trans, inode, dir);
	if (ret)
		btrfs_err(fs_info,
			  "error inheriting props for ino %llu (root %llu): %d",
			btrfs_ino(BTRFS_I(inode)), root->root_key.objectid, ret);

	return inode;

fail_unlock:
	discard_new_inode(inode);
fail:
	if (dir && name)
		BTRFS_I(dir)->index_cnt--;
	btrfs_free_path(path);
	return ERR_PTR(ret);
}

/*
 * utility function to add 'inode' into 'parent_inode' with
 * a give name and a given sequence number.
 * if 'add_backref' is true, also insert a backref from the
 * inode to the parent directory.
 */
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct btrfs_inode *parent_inode, struct btrfs_inode *inode,
		   const char *name, int name_len, int add_backref, u64 index)
{
	int ret = 0;
	struct btrfs_key key;
	struct btrfs_root *root = parent_inode->root;
	u64 ino = btrfs_ino(inode);
	u64 parent_ino = btrfs_ino(parent_inode);

	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		memcpy(&key, &inode->root->root_key, sizeof(key));
	} else {
		key.objectid = ino;
		key.type = BTRFS_INODE_ITEM_KEY;
		key.offset = 0;
	}

	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		ret = btrfs_add_root_ref(trans, key.objectid,
					 root->root_key.objectid, parent_ino,
					 index, name, name_len);
	} else if (add_backref) {
		ret = btrfs_insert_inode_ref(trans, root, name, name_len, ino,
					     parent_ino, index);
	}

	/* Nothing to clean up yet */
	if (ret)
		return ret;

	ret = btrfs_insert_dir_item(trans, name, name_len, parent_inode, &key,
				    btrfs_inode_type(&inode->vfs_inode), index);
	if (ret == -EEXIST || ret == -EOVERFLOW)
		goto fail_dir_item;
	else if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	btrfs_i_size_write(parent_inode, parent_inode->vfs_inode.i_size +
			   name_len * 2);
	inode_inc_iversion(&parent_inode->vfs_inode);
	/*
	 * If we are replaying a log tree, we do not want to update the mtime
	 * and ctime of the parent directory with the current time, since the
	 * log replay procedure is responsible for setting them to their correct
	 * values (the ones it had when the fsync was done).
	 */
	if (!test_bit(BTRFS_FS_LOG_RECOVERING, &root->fs_info->flags)) {
		struct timespec64 now = current_time(&parent_inode->vfs_inode);

		parent_inode->vfs_inode.i_mtime = now;
		parent_inode->vfs_inode.i_ctime = now;
	}
	ret = btrfs_update_inode(trans, root, parent_inode);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;

fail_dir_item:
	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		u64 local_index;
		int err;
		err = btrfs_del_root_ref(trans, key.objectid,
					 root->root_key.objectid, parent_ino,
					 &local_index, name, name_len);
		if (err)
			btrfs_abort_transaction(trans, err);
	} else if (add_backref) {
		u64 local_index;
		int err;

		err = btrfs_del_inode_ref(trans, root, name, name_len,
					  ino, parent_ino, &local_index);
		if (err)
			btrfs_abort_transaction(trans, err);
	}

	/* Return the original error code */
	return ret;
}

static int btrfs_add_nondir(struct btrfs_trans_handle *trans,
			    struct btrfs_inode *dir, struct dentry *dentry,
			    struct btrfs_inode *inode, int backref, u64 index)
{
	int err = btrfs_add_link(trans, dir, inode,
				 dentry->d_name.name, dentry->d_name.len,
				 backref, index);
	if (err > 0)
		err = -EEXIST;
	return err;
}

static int btrfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
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

	err = btrfs_get_free_objectid(root, &objectid);
	if (err)
		goto out_unlock;

	inode = btrfs_new_inode(trans, root, mnt_userns, dir,
			dentry->d_name.name, dentry->d_name.len,
			btrfs_ino(BTRFS_I(dir)), objectid, mode, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
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
		goto out_unlock;

	err = btrfs_add_nondir(trans, BTRFS_I(dir), dentry, BTRFS_I(inode),
			0, index);
	if (err)
		goto out_unlock;

	btrfs_update_inode(trans, root, BTRFS_I(inode));
	d_instantiate_new(dentry, inode);

out_unlock:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
	if (err && inode) {
		inode_dec_link_count(inode);
		discard_new_inode(inode);
	}
	return err;
}

static int btrfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = NULL;
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

	err = btrfs_get_free_objectid(root, &objectid);
	if (err)
		goto out_unlock;

	inode = btrfs_new_inode(trans, root, mnt_userns, dir,
			dentry->d_name.name, dentry->d_name.len,
			btrfs_ino(BTRFS_I(dir)), objectid, mode, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
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

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_unlock;

	err = btrfs_update_inode(trans, root, BTRFS_I(inode));
	if (err)
		goto out_unlock;

	err = btrfs_add_nondir(trans, BTRFS_I(dir), dentry, BTRFS_I(inode),
			0, index);
	if (err)
		goto out_unlock;

	d_instantiate_new(dentry, inode);

out_unlock:
	btrfs_end_transaction(trans);
	if (err && inode) {
		inode_dec_link_count(inode);
		discard_new_inode(inode);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
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
	if (root->root_key.objectid != BTRFS_I(inode)->root->root_key.objectid)
		return -EXDEV;

	if (inode->i_nlink >= BTRFS_LINK_MAX)
		return -EMLINK;

	err = btrfs_set_inode_index(BTRFS_I(dir), &index);
	if (err)
		goto fail;

	/*
	 * 2 items for inode and inode ref
	 * 2 items for dir items
	 * 1 item for parent inode
	 * 1 item for orphan item deletion if O_TMPFILE
	 */
	trans = btrfs_start_transaction(root, inode->i_nlink ? 5 : 6);
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

	err = btrfs_add_nondir(trans, BTRFS_I(dir), dentry, BTRFS_I(inode),
			1, index);

	if (err) {
		drop_inode = 1;
	} else {
		struct dentry *parent = dentry->d_parent;

		err = btrfs_update_inode(trans, root, BTRFS_I(inode));
		if (err)
			goto fail;
		if (inode->i_nlink == 1) {
			/*
			 * If new hard link count is 1, it's a file created
			 * with open(2) O_TMPFILE flag.
			 */
			err = btrfs_orphan_del(trans, BTRFS_I(inode));
			if (err)
				goto fail;
		}
		d_instantiate(dentry, inode);
		btrfs_log_new_name(trans, BTRFS_I(inode), NULL, parent);
	}

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

static int btrfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct inode *inode = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int err = 0;
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

	err = btrfs_get_free_objectid(root, &objectid);
	if (err)
		goto out_fail;

	inode = btrfs_new_inode(trans, root, mnt_userns, dir,
			dentry->d_name.name, dentry->d_name.len,
			btrfs_ino(BTRFS_I(dir)), objectid,
			S_IFDIR | mode, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
		goto out_fail;
	}

	/* these must be set before we unlock the inode */
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_fail;

	btrfs_i_size_write(BTRFS_I(inode), 0);
	err = btrfs_update_inode(trans, root, BTRFS_I(inode));
	if (err)
		goto out_fail;

	err = btrfs_add_link(trans, BTRFS_I(dir), BTRFS_I(inode),
			dentry->d_name.name,
			dentry->d_name.len, 0, index);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, inode);

out_fail:
	btrfs_end_transaction(trans);
	if (err && inode) {
		inode_dec_link_count(inode);
		discard_new_inode(inode);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
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

	/*
	 * decompression code contains a memset to fill in any space between the end
	 * of the uncompressed data and the end of max_size in case the decompressed
	 * data ends up shorter than ram_bytes.  That doesn't cover the hole between
	 * the end of an inline extent and the beginning of the next block, so we
	 * cover that region here.
	 */

	if (max_size + pg_offset < PAGE_SIZE)
		memzero_page(page,  pg_offset + max_size,
			     PAGE_SIZE - max_size - pg_offset);
	kfree(tmp);
	return ret;
}

/**
 * btrfs_get_extent - Lookup the first extent overlapping a range in a file.
 * @inode:	file to search in
 * @page:	page to read extent data into if the extent is inline
 * @pg_offset:	offset into @page to copy to
 * @start:	file offset
 * @len:	length of range starting at @start
 *
 * This returns the first &struct extent_map which overlaps with the given
 * range, reading it from the B-tree and caching it if necessary. Note that
 * there may be more extents which overlap the given range after the returned
 * extent_map.
 *
 * If @page is not NULL and the extent is inline, this also reads the extent
 * data directly into the page and marks the extent up to date in the io_tree.
 *
 * Return: ERR_PTR on error, non-NULL extent_map on success.
 */
struct extent_map *btrfs_get_extent(struct btrfs_inode *inode,
				    struct page *page, size_t pg_offset,
				    u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	int ret = 0;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = btrfs_ino(inode);
	int extent_type = -1;
	struct btrfs_path *path = NULL;
	struct btrfs_root *root = inode->root;
	struct btrfs_file_extent_item *item;
	struct extent_buffer *leaf;
	struct btrfs_key found_key;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_io_tree *io_tree = &inode->io_tree;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
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
		ret = -ENOMEM;
		goto out;
	}
	em->start = EXTENT_MAP_HOLE;
	em->orig_start = EXTENT_MAP_HOLE;
	em->len = (u64)-1;
	em->block_len = (u64)-1;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/* Chances are we'll be called again, so go ahead and do readahead */
	path->reada = READA_FORWARD;

	/*
	 * The same explanation in load_free_space_cache applies here as well,
	 * we only read when we're loading the free space cache, and at that
	 * point the commit_root has everything we need.
	 */
	if (btrfs_is_free_space_inode(inode)) {
		path->search_commit_root = 1;
		path->skip_locking = 1;
	}

	ret = btrfs_lookup_file_extent(NULL, root, path, objectid, start, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		if (path->slots[0] == 0)
			goto not_found;
		path->slots[0]--;
		ret = 0;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_file_extent_item);
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	if (found_key.objectid != objectid ||
	    found_key.type != BTRFS_EXTENT_DATA_KEY) {
		/*
		 * If we backup past the first extent we want to move forward
		 * and see if there is an extent in front of us, otherwise we'll
		 * say there is a hole for our whole search range which can
		 * cause problems.
		 */
		extent_end = start;
		goto next;
	}

	extent_type = btrfs_file_extent_type(leaf, item);
	extent_start = found_key.offset;
	extent_end = btrfs_file_extent_end(path);
	if (extent_type == BTRFS_FILE_EXTENT_REG ||
	    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
		/* Only regular file could have regular/prealloc extent */
		if (!S_ISREG(inode->vfs_inode.i_mode)) {
			ret = -EUCLEAN;
			btrfs_crit(fs_info,
		"regular/prealloc extent found for non-regular inode %llu",
				   btrfs_ino(inode));
			goto out;
		}
		trace_btrfs_get_extent_show_fi_regular(inode, leaf, item,
						       extent_start);
	} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		trace_btrfs_get_extent_show_fi_inline(inode, leaf, item,
						      path->slots[0],
						      extent_start);
	}
next:
	if (start >= extent_end) {
		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
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

		/* New extent overlaps with existing one */
		em->start = start;
		em->orig_start = start;
		em->len = found_key.offset - start;
		em->block_start = EXTENT_MAP_HOLE;
		goto insert;
	}

	btrfs_extent_item_to_extent_map(inode, path, item, !page, em);

	if (extent_type == BTRFS_FILE_EXTENT_REG ||
	    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
		goto insert;
	} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		unsigned long ptr;
		char *map;
		size_t size;
		size_t extent_offset;
		size_t copy_size;

		if (!page)
			goto out;

		size = btrfs_file_extent_ram_bytes(leaf, item);
		extent_offset = page_offset(page) + pg_offset - extent_start;
		copy_size = min_t(u64, PAGE_SIZE - pg_offset,
				  size - extent_offset);
		em->start = extent_start + extent_offset;
		em->len = ALIGN(copy_size, fs_info->sectorsize);
		em->orig_block_len = em->len;
		em->orig_start = em->start;
		ptr = btrfs_file_extent_inline_start(item) + extent_offset;

		if (!PageUptodate(page)) {
			if (btrfs_file_extent_compression(leaf, item) !=
			    BTRFS_COMPRESS_NONE) {
				ret = uncompress_inline(path, page, pg_offset,
							extent_offset, item);
				if (ret)
					goto out;
			} else {
				map = kmap_local_page(page);
				read_extent_buffer(leaf, map + pg_offset, ptr,
						   copy_size);
				if (pg_offset + copy_size < PAGE_SIZE) {
					memset(map + pg_offset + copy_size, 0,
					       PAGE_SIZE - pg_offset -
					       copy_size);
				}
				kunmap_local(map);
			}
			flush_dcache_page(page);
		}
		set_extent_uptodate(io_tree, em->start,
				    extent_map_end(em) - 1, NULL, GFP_NOFS);
		goto insert;
	}
not_found:
	em->start = start;
	em->orig_start = start;
	em->len = len;
	em->block_start = EXTENT_MAP_HOLE;
insert:
	ret = 0;
	btrfs_release_path(path);
	if (em->start > start || extent_map_end(em) <= start) {
		btrfs_err(fs_info,
			  "bad extent! em: [%llu %llu] passed [%llu %llu]",
			  em->start, em->len, start, len);
		ret = -EIO;
		goto out;
	}

	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, start, len);
	write_unlock(&em_tree->lock);
out:
	btrfs_free_path(path);

	trace_btrfs_get_extent(root, inode, em);

	if (ret) {
		free_extent_map(em);
		return ERR_PTR(ret);
	}
	return em;
}

struct extent_map *btrfs_get_extent_fiemap(struct btrfs_inode *inode,
					   u64 start, u64 len)
{
	struct extent_map *em;
	struct extent_map *hole_em = NULL;
	u64 delalloc_start = start;
	u64 end;
	u64 delalloc_len;
	u64 delalloc_end;
	int err = 0;

	em = btrfs_get_extent(inode, NULL, 0, start, len);
	if (IS_ERR(em))
		return em;
	/*
	 * If our em maps to:
	 * - a hole or
	 * - a pre-alloc extent,
	 * there might actually be delalloc bytes behind it.
	 */
	if (em->block_start != EXTENT_MAP_HOLE &&
	    !test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
		return em;
	else
		hole_em = em;

	/* check to see if we've wrapped (len == -1 or similar) */
	end = start + len;
	if (end < start)
		end = (u64)-1;
	else
		end -= 1;

	em = NULL;

	/* ok, we didn't find anything, lets look for delalloc */
	delalloc_len = count_range_bits(&inode->io_tree, &delalloc_start,
				 end, len, EXTENT_DELALLOC, 1);
	delalloc_end = delalloc_start + delalloc_len;
	if (delalloc_end < delalloc_start)
		delalloc_end = (u64)-1;

	/*
	 * We didn't find anything useful, return the original results from
	 * get_extent()
	 */
	if (delalloc_start > end || delalloc_end <= start) {
		em = hole_em;
		hole_em = NULL;
		goto out;
	}

	/*
	 * Adjust the delalloc_start to make sure it doesn't go backwards from
	 * the start they passed in
	 */
	delalloc_start = max(start, delalloc_start);
	delalloc_len = delalloc_end - delalloc_start;

	if (delalloc_len > 0) {
		u64 hole_start;
		u64 hole_len;
		const u64 hole_end = extent_map_end(hole_em);

		em = alloc_extent_map();
		if (!em) {
			err = -ENOMEM;
			goto out;
		}

		ASSERT(hole_em);
		/*
		 * When btrfs_get_extent can't find anything it returns one
		 * huge hole
		 *
		 * Make sure what it found really fits our range, and adjust to
		 * make sure it is based on the start from the caller
		 */
		if (hole_end <= start || hole_em->start > end) {
		       free_extent_map(hole_em);
		       hole_em = NULL;
		} else {
		       hole_start = max(hole_em->start, start);
		       hole_len = hole_end - hole_start;
		}

		if (hole_em && delalloc_start > hole_start) {
			/*
			 * Our hole starts before our delalloc, so we have to
			 * return just the parts of the hole that go until the
			 * delalloc starts
			 */
			em->len = min(hole_len, delalloc_start - hole_start);
			em->start = hole_start;
			em->orig_start = hole_start;
			/*
			 * Don't adjust block start at all, it is fixed at
			 * EXTENT_MAP_HOLE
			 */
			em->block_start = hole_em->block_start;
			em->block_len = hole_len;
			if (test_bit(EXTENT_FLAG_PREALLOC, &hole_em->flags))
				set_bit(EXTENT_FLAG_PREALLOC, &em->flags);
		} else {
			/*
			 * Hole is out of passed range or it starts after
			 * delalloc range
			 */
			em->start = delalloc_start;
			em->len = delalloc_len;
			em->orig_start = delalloc_start;
			em->block_start = EXTENT_MAP_DELALLOC;
			em->block_len = delalloc_len;
		}
	} else {
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

static struct extent_map *btrfs_create_dio_extent(struct btrfs_inode *inode,
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

	if (type != BTRFS_ORDERED_NOCOW) {
		em = create_io_em(inode, start, len, orig_start, block_start,
				  block_len, orig_block_len, ram_bytes,
				  BTRFS_COMPRESS_NONE, /* compress_type */
				  type);
		if (IS_ERR(em))
			goto out;
	}
	ret = btrfs_add_ordered_extent_dio(inode, start, block_start, len,
					   block_len, type);
	if (ret) {
		if (em) {
			free_extent_map(em);
			btrfs_drop_extent_cache(inode, start, start + len - 1, 0);
		}
		em = ERR_PTR(ret);
	}
 out:

	return em;
}

static struct extent_map *btrfs_new_extent_direct(struct btrfs_inode *inode,
						  u64 start, u64 len)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
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
				     ins.offset, BTRFS_ORDERED_REGULAR);
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	if (IS_ERR(em))
		btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset,
					   1);

	return em;
}

static bool btrfs_extent_readonly(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_block_group *block_group;
	bool readonly = false;

	block_group = btrfs_lookup_block_group(fs_info, bytenr);
	if (!block_group || block_group->ro)
		readonly = true;
	if (block_group)
		btrfs_put_block_group(block_group);
	return readonly;
}

/*
 * Check if we can do nocow write into the range [@offset, @offset + @len)
 *
 * @offset:	File offset
 * @len:	The length to write, will be updated to the nocow writeable
 *		range
 * @orig_start:	(optional) Return the original file offset of the file extent
 * @orig_len:	(optional) Return the original on-disk length of the file extent
 * @ram_bytes:	(optional) Return the ram_bytes of the file extent
 * @strict:	if true, omit optimizations that might force us into unnecessary
 *		cow. e.g., don't trust generation number.
 *
 * Return:
 * >0	and update @len if we can do nocow write
 *  0	if we can't do nocow write
 * <0	if error happened
 *
 * NOTE: This only checks the file extents, caller is responsible to wait for
 *	 any ordered extents.
 */
noinline int can_nocow_extent(struct inode *inode, u64 offset, u64 *len,
			      u64 *orig_start, u64 *orig_block_len,
			      u64 *ram_bytes, bool strict)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
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

	ret = btrfs_lookup_file_extent(NULL, root, path,
			btrfs_ino(BTRFS_I(inode)), offset, 0);
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
	if (key.objectid != btrfs_ino(BTRFS_I(inode)) ||
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

	/*
	 * Do the same check as in btrfs_cross_ref_exist but without the
	 * unnecessary search.
	 */
	if (!strict &&
	    (btrfs_file_extent_generation(leaf, fi) <=
	     btrfs_root_last_snapshot(&root->root_item)))
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

	ret = btrfs_cross_ref_exist(root, btrfs_ino(BTRFS_I(inode)),
				    key.offset - backref_offset, disk_bytenr,
				    strict);
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

static int lock_extent_direct(struct inode *inode, u64 lockstart, u64 lockend,
			      struct extent_state **cached_state, bool writing)
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
		ordered = btrfs_lookup_ordered_range(BTRFS_I(inode), lockstart,
						     lockend - lockstart + 1);

		/*
		 * We need to make sure there are no buffered pages in this
		 * range either, we could have raced between the invalidate in
		 * generic_file_direct_write and locking the extent.  The
		 * invalidate needs to happen so that reads after a write do not
		 * get stale data.
		 */
		if (!ordered &&
		    (!writing || !filemap_range_has_page(inode->i_mapping,
							 lockstart, lockend)))
			break;

		unlock_extent_cached(&BTRFS_I(inode)->io_tree, lockstart, lockend,
				     cached_state);

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
				btrfs_start_ordered_extent(ordered, 1);
			else
				ret = -ENOTBLK;
			btrfs_put_ordered_extent(ordered);
		} else {
			/*
			 * We could trigger writeback for this range (and wait
			 * for it to complete) and then invalidate the pages for
			 * this range (through invalidate_inode_pages2_range()),
			 * but that can lead us to a deadlock with a concurrent
			 * call to readahead (a buffered read or a defrag call
			 * triggered a readahead) on a page lock due to an
			 * ordered dio extent we created before but did not have
			 * yet a corresponding bio submitted (whence it can not
			 * complete), which makes readahead wait for that
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

/* The callers of this must take lock_extent() */
static struct extent_map *create_io_em(struct btrfs_inode *inode, u64 start,
				       u64 len, u64 orig_start, u64 block_start,
				       u64 block_len, u64 orig_block_len,
				       u64 ram_bytes, int compress_type,
				       int type)
{
	struct extent_map_tree *em_tree;
	struct extent_map *em;
	int ret;

	ASSERT(type == BTRFS_ORDERED_PREALLOC ||
	       type == BTRFS_ORDERED_COMPRESSED ||
	       type == BTRFS_ORDERED_NOCOW ||
	       type == BTRFS_ORDERED_REGULAR);

	em_tree = &inode->extent_tree;
	em = alloc_extent_map();
	if (!em)
		return ERR_PTR(-ENOMEM);

	em->start = start;
	em->orig_start = orig_start;
	em->len = len;
	em->block_len = block_len;
	em->block_start = block_start;
	em->orig_block_len = orig_block_len;
	em->ram_bytes = ram_bytes;
	em->generation = -1;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);
	if (type == BTRFS_ORDERED_PREALLOC) {
		set_bit(EXTENT_FLAG_FILLING, &em->flags);
	} else if (type == BTRFS_ORDERED_COMPRESSED) {
		set_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
		em->compress_type = compress_type;
	}

	do {
		btrfs_drop_extent_cache(inode, em->start,
					em->start + em->len - 1, 0);
		write_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em, 1);
		write_unlock(&em_tree->lock);
		/*
		 * The caller has taken lock_extent(), who could race with us
		 * to add em?
		 */
	} while (ret == -EEXIST);

	if (ret) {
		free_extent_map(em);
		return ERR_PTR(ret);
	}

	/* em got 2 refs now, callers needs to do free_extent_map once. */
	return em;
}


static int btrfs_get_blocks_direct_write(struct extent_map **map,
					 struct inode *inode,
					 struct btrfs_dio_data *dio_data,
					 u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_map *em = *map;
	int ret = 0;

	/*
	 * We don't allocate a new extent in the following cases
	 *
	 * 1) The inode is marked as NODATACOW. In this case we'll just use the
	 * existing extent.
	 * 2) The extent is marked as PREALLOC. We're good to go here and can
	 * just use the extent.
	 *
	 */
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
				     &orig_block_len, &ram_bytes, false) == 1 &&
		    btrfs_inc_nocow_writers(fs_info, block_start)) {
			struct extent_map *em2;

			em2 = btrfs_create_dio_extent(BTRFS_I(inode), start, len,
						      orig_start, block_start,
						      len, orig_block_len,
						      ram_bytes, type);
			btrfs_dec_nocow_writers(fs_info, block_start);
			if (type == BTRFS_ORDERED_PREALLOC) {
				free_extent_map(em);
				*map = em = em2;
			}

			if (em2 && IS_ERR(em2)) {
				ret = PTR_ERR(em2);
				goto out;
			}
			/*
			 * For inode marked NODATACOW or extent marked PREALLOC,
			 * use the existing or preallocated extent, so does not
			 * need to adjust btrfs_space_info's bytes_may_use.
			 */
			btrfs_free_reserved_data_space_noquota(fs_info, len);
			goto skip_cow;
		}
	}

	/* this will cow the extent */
	free_extent_map(em);
	*map = em = btrfs_new_extent_direct(BTRFS_I(inode), start, len);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out;
	}

	len = min(len, em->len - (start - em->start));

skip_cow:
	/*
	 * Need to update the i_size under the extent lock so buffered
	 * readers will get the updated i_size when we unlock.
	 */
	if (start + len > i_size_read(inode))
		i_size_write(inode, start + len);

	dio_data->reserve -= len;
out:
	return ret;
}

static int btrfs_dio_iomap_begin(struct inode *inode, loff_t start,
		loff_t length, unsigned int flags, struct iomap *iomap,
		struct iomap *srcmap)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_map *em;
	struct extent_state *cached_state = NULL;
	struct btrfs_dio_data *dio_data = NULL;
	u64 lockstart, lockend;
	const bool write = !!(flags & IOMAP_WRITE);
	int ret = 0;
	u64 len = length;
	bool unlock_extents = false;

	if (!write)
		len = min_t(u64, len, fs_info->sectorsize);

	lockstart = start;
	lockend = start + len - 1;

	/*
	 * The generic stuff only does filemap_write_and_wait_range, which
	 * isn't enough if we've written compressed pages to this area, so we
	 * need to flush the dirty pages again to make absolutely sure that any
	 * outstanding dirty pages are on disk.
	 */
	if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
		     &BTRFS_I(inode)->runtime_flags)) {
		ret = filemap_fdatawrite_range(inode->i_mapping, start,
					       start + length - 1);
		if (ret)
			return ret;
	}

	dio_data = kzalloc(sizeof(*dio_data), GFP_NOFS);
	if (!dio_data)
		return -ENOMEM;

	dio_data->length = length;
	if (write) {
		dio_data->reserve = round_up(length, fs_info->sectorsize);
		ret = btrfs_delalloc_reserve_space(BTRFS_I(inode),
				&dio_data->data_reserved,
				start, dio_data->reserve);
		if (ret) {
			extent_changeset_free(dio_data->data_reserved);
			kfree(dio_data);
			return ret;
		}
	}
	iomap->private = dio_data;


	/*
	 * If this errors out it's because we couldn't invalidate pagecache for
	 * this range and we need to fallback to buffered.
	 */
	if (lock_extent_direct(inode, lockstart, lockend, &cached_state, write)) {
		ret = -ENOTBLK;
		goto err;
	}

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, start, len);
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

	len = min(len, em->len - (start - em->start));
	if (write) {
		ret = btrfs_get_blocks_direct_write(&em, inode, dio_data,
						    start, len);
		if (ret < 0)
			goto unlock_err;
		unlock_extents = true;
		/* Recalc len in case the new em is smaller than requested */
		len = min(len, em->len - (start - em->start));
	} else {
		/*
		 * We need to unlock only the end area that we aren't using.
		 * The rest is going to be unlocked by the endio routine.
		 */
		lockstart = start + len;
		if (lockstart < lockend)
			unlock_extents = true;
	}

	if (unlock_extents)
		unlock_extent_cached(&BTRFS_I(inode)->io_tree,
				     lockstart, lockend, &cached_state);
	else
		free_extent_state(cached_state);

	/*
	 * Translate extent map information to iomap.
	 * We trim the extents (and move the addr) even though iomap code does
	 * that, since we have locked only the parts we are performing I/O in.
	 */
	if ((em->block_start == EXTENT_MAP_HOLE) ||
	    (test_bit(EXTENT_FLAG_PREALLOC, &em->flags) && !write)) {
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->type = IOMAP_HOLE;
	} else {
		iomap->addr = em->block_start + (start - em->start);
		iomap->type = IOMAP_MAPPED;
	}
	iomap->offset = start;
	iomap->bdev = fs_info->fs_devices->latest_dev->bdev;
	iomap->length = len;

	if (write && btrfs_use_zone_append(BTRFS_I(inode), em->block_start))
		iomap->flags |= IOMAP_F_ZONE_APPEND;

	free_extent_map(em);

	return 0;

unlock_err:
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, lockstart, lockend,
			     &cached_state);
err:
	if (dio_data) {
		btrfs_delalloc_release_space(BTRFS_I(inode),
				dio_data->data_reserved, start,
				dio_data->reserve, true);
		btrfs_delalloc_release_extents(BTRFS_I(inode), dio_data->reserve);
		extent_changeset_free(dio_data->data_reserved);
		kfree(dio_data);
	}
	return ret;
}

static int btrfs_dio_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	int ret = 0;
	struct btrfs_dio_data *dio_data = iomap->private;
	size_t submitted = dio_data->submitted;
	const bool write = !!(flags & IOMAP_WRITE);

	if (!write && (iomap->type == IOMAP_HOLE)) {
		/* If reading from a hole, unlock and return */
		unlock_extent(&BTRFS_I(inode)->io_tree, pos, pos + length - 1);
		goto out;
	}

	if (submitted < length) {
		pos += submitted;
		length -= submitted;
		if (write)
			__endio_write_update_ordered(BTRFS_I(inode), pos,
					length, false);
		else
			unlock_extent(&BTRFS_I(inode)->io_tree, pos,
				      pos + length - 1);
		ret = -ENOTBLK;
	}

	if (write) {
		if (dio_data->reserve)
			btrfs_delalloc_release_space(BTRFS_I(inode),
					dio_data->data_reserved, pos,
					dio_data->reserve, true);
		btrfs_delalloc_release_extents(BTRFS_I(inode), dio_data->length);
		extent_changeset_free(dio_data->data_reserved);
	}
out:
	kfree(dio_data);
	iomap->private = NULL;

	return ret;
}

static void btrfs_dio_private_put(struct btrfs_dio_private *dip)
{
	/*
	 * This implies a barrier so that stores to dio_bio->bi_status before
	 * this and loads of dio_bio->bi_status after this are fully ordered.
	 */
	if (!refcount_dec_and_test(&dip->refs))
		return;

	if (btrfs_op(dip->dio_bio) == BTRFS_MAP_WRITE) {
		__endio_write_update_ordered(BTRFS_I(dip->inode),
					     dip->logical_offset,
					     dip->bytes,
					     !dip->dio_bio->bi_status);
	} else {
		unlock_extent(&BTRFS_I(dip->inode)->io_tree,
			      dip->logical_offset,
			      dip->logical_offset + dip->bytes - 1);
	}

	bio_endio(dip->dio_bio);
	kfree(dip);
}

static blk_status_t submit_dio_repair_bio(struct inode *inode, struct bio *bio,
					  int mirror_num,
					  unsigned long bio_flags)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	blk_status_t ret;

	BUG_ON(bio_op(bio) == REQ_OP_WRITE);

	ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DATA);
	if (ret)
		return ret;

	refcount_inc(&dip->refs);
	ret = btrfs_map_bio(fs_info, bio, mirror_num);
	if (ret)
		refcount_dec(&dip->refs);
	return ret;
}

static blk_status_t btrfs_check_read_dio_bio(struct inode *inode,
					     struct btrfs_io_bio *io_bio,
					     const bool uptodate)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	const u32 sectorsize = fs_info->sectorsize;
	struct extent_io_tree *failure_tree = &BTRFS_I(inode)->io_failure_tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	const bool csum = !(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM);
	struct bio_vec bvec;
	struct bvec_iter iter;
	u64 start = io_bio->logical;
	u32 bio_offset = 0;
	blk_status_t err = BLK_STS_OK;

	__bio_for_each_segment(bvec, &io_bio->bio, iter, io_bio->iter) {
		unsigned int i, nr_sectors, pgoff;

		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info, bvec.bv_len);
		pgoff = bvec.bv_offset;
		for (i = 0; i < nr_sectors; i++) {
			ASSERT(pgoff < PAGE_SIZE);
			if (uptodate &&
			    (!csum || !check_data_csum(inode, io_bio,
						       bio_offset, bvec.bv_page,
						       pgoff, start))) {
				clean_io_failure(fs_info, failure_tree, io_tree,
						 start, bvec.bv_page,
						 btrfs_ino(BTRFS_I(inode)),
						 pgoff);
			} else {
				int ret;

				ASSERT((start - io_bio->logical) < UINT_MAX);
				ret = btrfs_repair_one_sector(inode,
						&io_bio->bio,
						start - io_bio->logical,
						bvec.bv_page, pgoff,
						start, io_bio->mirror_num,
						submit_dio_repair_bio);
				if (ret)
					err = errno_to_blk_status(ret);
			}
			start += sectorsize;
			ASSERT(bio_offset + sectorsize > bio_offset);
			bio_offset += sectorsize;
			pgoff += sectorsize;
		}
	}
	return err;
}

static void __endio_write_update_ordered(struct btrfs_inode *inode,
					 const u64 offset, const u64 bytes,
					 const bool uptodate)
{
	btrfs_mark_ordered_io_finished(inode, NULL, offset, bytes,
				       finish_ordered_fn, uptodate);
}

static blk_status_t btrfs_submit_bio_start_direct_io(struct inode *inode,
						     struct bio *bio,
						     u64 dio_file_offset)
{
	return btrfs_csum_one_bio(BTRFS_I(inode), bio, dio_file_offset, 1);
}

static void btrfs_end_dio_bio(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	blk_status_t err = bio->bi_status;

	if (err)
		btrfs_warn(BTRFS_I(dip->inode)->root->fs_info,
			   "direct IO failed ino %llu rw %d,%u sector %#Lx len %u err no %d",
			   btrfs_ino(BTRFS_I(dip->inode)), bio_op(bio),
			   bio->bi_opf, bio->bi_iter.bi_sector,
			   bio->bi_iter.bi_size, err);

	if (bio_op(bio) == REQ_OP_READ) {
		err = btrfs_check_read_dio_bio(dip->inode, btrfs_io_bio(bio),
					       !err);
	}

	if (err)
		dip->dio_bio->bi_status = err;

	btrfs_record_physical_zoned(dip->inode, dip->logical_offset, bio);

	bio_put(bio);
	btrfs_dio_private_put(dip);
}

static inline blk_status_t btrfs_submit_dio_bio(struct bio *bio,
		struct inode *inode, u64 file_offset, int async_submit)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_dio_private *dip = bio->bi_private;
	bool write = btrfs_op(bio) == BTRFS_MAP_WRITE;
	blk_status_t ret;

	/* Check btrfs_submit_bio_hook() for rules about async submit. */
	if (async_submit)
		async_submit = !atomic_read(&BTRFS_I(inode)->sync_writers);

	if (!write) {
		ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DATA);
		if (ret)
			goto err;
	}

	if (BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)
		goto map;

	if (write && async_submit) {
		ret = btrfs_wq_submit_bio(inode, bio, 0, 0, file_offset,
					  btrfs_submit_bio_start_direct_io);
		goto err;
	} else if (write) {
		/*
		 * If we aren't doing async submit, calculate the csum of the
		 * bio now.
		 */
		ret = btrfs_csum_one_bio(BTRFS_I(inode), bio, file_offset, 1);
		if (ret)
			goto err;
	} else {
		u64 csum_offset;

		csum_offset = file_offset - dip->logical_offset;
		csum_offset >>= fs_info->sectorsize_bits;
		csum_offset *= fs_info->csum_size;
		btrfs_io_bio(bio)->csum = dip->csums + csum_offset;
	}
map:
	ret = btrfs_map_bio(fs_info, bio, 0);
err:
	return ret;
}

/*
 * If this succeeds, the btrfs_dio_private is responsible for cleaning up locked
 * or ordered extents whether or not we submit any bios.
 */
static struct btrfs_dio_private *btrfs_create_dio_private(struct bio *dio_bio,
							  struct inode *inode,
							  loff_t file_offset)
{
	const bool write = (btrfs_op(dio_bio) == BTRFS_MAP_WRITE);
	const bool csum = !(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM);
	size_t dip_size;
	struct btrfs_dio_private *dip;

	dip_size = sizeof(*dip);
	if (!write && csum) {
		struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
		size_t nblocks;

		nblocks = dio_bio->bi_iter.bi_size >> fs_info->sectorsize_bits;
		dip_size += fs_info->csum_size * nblocks;
	}

	dip = kzalloc(dip_size, GFP_NOFS);
	if (!dip)
		return NULL;

	dip->inode = inode;
	dip->logical_offset = file_offset;
	dip->bytes = dio_bio->bi_iter.bi_size;
	dip->disk_bytenr = dio_bio->bi_iter.bi_sector << 9;
	dip->dio_bio = dio_bio;
	refcount_set(&dip->refs, 1);
	return dip;
}

static blk_qc_t btrfs_submit_direct(const struct iomap_iter *iter,
		struct bio *dio_bio, loff_t file_offset)
{
	struct inode *inode = iter->inode;
	const bool write = (btrfs_op(dio_bio) == BTRFS_MAP_WRITE);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	const bool raid56 = (btrfs_data_alloc_profile(fs_info) &
			     BTRFS_BLOCK_GROUP_RAID56_MASK);
	struct btrfs_dio_private *dip;
	struct bio *bio;
	u64 start_sector;
	int async_submit = 0;
	u64 submit_len;
	u64 clone_offset = 0;
	u64 clone_len;
	u64 logical;
	int ret;
	blk_status_t status;
	struct btrfs_io_geometry geom;
	struct btrfs_dio_data *dio_data = iter->iomap.private;
	struct extent_map *em = NULL;

	dip = btrfs_create_dio_private(dio_bio, inode, file_offset);
	if (!dip) {
		if (!write) {
			unlock_extent(&BTRFS_I(inode)->io_tree, file_offset,
				file_offset + dio_bio->bi_iter.bi_size - 1);
		}
		dio_bio->bi_status = BLK_STS_RESOURCE;
		bio_endio(dio_bio);
		return BLK_QC_T_NONE;
	}

	if (!write) {
		/*
		 * Load the csums up front to reduce csum tree searches and
		 * contention when submitting bios.
		 *
		 * If we have csums disabled this will do nothing.
		 */
		status = btrfs_lookup_bio_sums(inode, dio_bio, dip->csums);
		if (status != BLK_STS_OK)
			goto out_err;
	}

	start_sector = dio_bio->bi_iter.bi_sector;
	submit_len = dio_bio->bi_iter.bi_size;

	do {
		logical = start_sector << 9;
		em = btrfs_get_chunk_map(fs_info, logical, submit_len);
		if (IS_ERR(em)) {
			status = errno_to_blk_status(PTR_ERR(em));
			em = NULL;
			goto out_err_em;
		}
		ret = btrfs_get_io_geometry(fs_info, em, btrfs_op(dio_bio),
					    logical, &geom);
		if (ret) {
			status = errno_to_blk_status(ret);
			goto out_err_em;
		}

		clone_len = min(submit_len, geom.len);
		ASSERT(clone_len <= UINT_MAX);

		/*
		 * This will never fail as it's passing GPF_NOFS and
		 * the allocation is backed by btrfs_bioset.
		 */
		bio = btrfs_bio_clone_partial(dio_bio, clone_offset, clone_len);
		bio->bi_private = dip;
		bio->bi_end_io = btrfs_end_dio_bio;
		btrfs_io_bio(bio)->logical = file_offset;

		if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
			status = extract_ordered_extent(BTRFS_I(inode), bio,
							file_offset);
			if (status) {
				bio_put(bio);
				goto out_err;
			}
		}

		ASSERT(submit_len >= clone_len);
		submit_len -= clone_len;

		/*
		 * Increase the count before we submit the bio so we know
		 * the end IO handler won't happen before we increase the
		 * count. Otherwise, the dip might get freed before we're
		 * done setting it up.
		 *
		 * We transfer the initial reference to the last bio, so we
		 * don't need to increment the reference count for the last one.
		 */
		if (submit_len > 0) {
			refcount_inc(&dip->refs);
			/*
			 * If we are submitting more than one bio, submit them
			 * all asynchronously. The exception is RAID 5 or 6, as
			 * asynchronous checksums make it difficult to collect
			 * full stripe writes.
			 */
			if (!raid56)
				async_submit = 1;
		}

		status = btrfs_submit_dio_bio(bio, inode, file_offset,
						async_submit);
		if (status) {
			bio_put(bio);
			if (submit_len > 0)
				refcount_dec(&dip->refs);
			goto out_err_em;
		}

		dio_data->submitted += clone_len;
		clone_offset += clone_len;
		start_sector += clone_len >> 9;
		file_offset += clone_len;

		free_extent_map(em);
	} while (submit_len > 0);
	return BLK_QC_T_NONE;

out_err_em:
	free_extent_map(em);
out_err:
	dip->dio_bio->bi_status = status;
	btrfs_dio_private_put(dip);

	return BLK_QC_T_NONE;
}

const struct iomap_ops btrfs_dio_iomap_ops = {
	.iomap_begin            = btrfs_dio_iomap_begin,
	.iomap_end              = btrfs_dio_iomap_end,
};

const struct iomap_dio_ops btrfs_dio_ops = {
	.submit_io		= btrfs_submit_direct,
};

static int btrfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
			u64 start, u64 len)
{
	int	ret;

	ret = fiemap_prep(inode, fieinfo, start, &len, 0);
	if (ret)
		return ret;

	return extent_fiemap(BTRFS_I(inode), fieinfo, start, len);
}

int btrfs_readpage(struct file *file, struct page *page)
{
	struct btrfs_inode *inode = BTRFS_I(page->mapping->host);
	u64 start = page_offset(page);
	u64 end = start + PAGE_SIZE - 1;
	struct btrfs_bio_ctrl bio_ctrl = { 0 };
	int ret;

	btrfs_lock_and_flush_ordered_range(inode, start, end, NULL);

	ret = btrfs_do_readpage(page, NULL, &bio_ctrl, 0, NULL);
	if (bio_ctrl.bio)
		ret = submit_one_bio(bio_ctrl.bio, 0, bio_ctrl.bio_flags);
	return ret;
}

static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
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
	ret = extent_write_full_page(page, wbc);
	btrfs_add_delayed_iput(inode);
	return ret;
}

static int btrfs_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	return extent_writepages(mapping, wbc);
}

static void btrfs_readahead(struct readahead_control *rac)
{
	extent_readahead(rac);
}

/*
 * For releasepage() and invalidatepage() we have a race window where
 * end_page_writeback() is called but the subpage spinlock is not yet released.
 * If we continue to release/invalidate the page, we could cause use-after-free
 * for subpage spinlock.  So this function is to spin and wait for subpage
 * spinlock.
 */
static void wait_subpage_spinlock(struct page *page)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(page->mapping->host->i_sb);
	struct btrfs_subpage *subpage;

	if (fs_info->sectorsize == PAGE_SIZE)
		return;

	ASSERT(PagePrivate(page) && page->private);
	subpage = (struct btrfs_subpage *)page->private;

	/*
	 * This may look insane as we just acquire the spinlock and release it,
	 * without doing anything.  But we just want to make sure no one is
	 * still holding the subpage spinlock.
	 * And since the page is not dirty nor writeback, and we have page
	 * locked, the only possible way to hold a spinlock is from the endio
	 * function to clear page writeback.
	 *
	 * Here we just acquire the spinlock so that all existing callers
	 * should exit and we're safe to release/invalidate the page.
	 */
	spin_lock_irq(&subpage->lock);
	spin_unlock_irq(&subpage->lock);
}

static int __btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	int ret = try_release_extent_mapping(page, gfp_flags);

	if (ret == 1) {
		wait_subpage_spinlock(page);
		clear_page_extent_mapped(page);
	}
	return ret;
}

static int btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	if (PageWriteback(page) || PageDirty(page))
		return 0;
	return __btrfs_releasepage(page, gfp_flags);
}

#ifdef CONFIG_MIGRATION
static int btrfs_migratepage(struct address_space *mapping,
			     struct page *newpage, struct page *page,
			     enum migrate_mode mode)
{
	int ret;

	ret = migrate_page_move_mapping(mapping, newpage, page, 0);
	if (ret != MIGRATEPAGE_SUCCESS)
		return ret;

	if (page_has_private(page))
		attach_page_private(newpage, detach_page_private(page));

	if (PageOrdered(page)) {
		ClearPageOrdered(page);
		SetPageOrdered(newpage);
	}

	if (mode != MIGRATE_SYNC_NO_COPY)
		migrate_page_copy(newpage, page);
	else
		migrate_page_states(newpage, page);
	return MIGRATEPAGE_SUCCESS;
}
#endif

static void btrfs_invalidatepage(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct btrfs_inode *inode = BTRFS_I(page->mapping->host);
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_io_tree *tree = &inode->io_tree;
	struct extent_state *cached_state = NULL;
	u64 page_start = page_offset(page);
	u64 page_end = page_start + PAGE_SIZE - 1;
	u64 cur;
	int inode_evicting = inode->vfs_inode.i_state & I_FREEING;

	/*
	 * We have page locked so no new ordered extent can be created on this
	 * page, nor bio can be submitted for this page.
	 *
	 * But already submitted bio can still be finished on this page.
	 * Furthermore, endio function won't skip page which has Ordered
	 * (Private2) already cleared, so it's possible for endio and
	 * invalidatepage to do the same ordered extent accounting twice
	 * on one page.
	 *
	 * So here we wait for any submitted bios to finish, so that we won't
	 * do double ordered extent accounting on the same page.
	 */
	wait_on_page_writeback(page);
	wait_subpage_spinlock(page);

	/*
	 * For subpage case, we have call sites like
	 * btrfs_punch_hole_lock_range() which passes range not aligned to
	 * sectorsize.
	 * If the range doesn't cover the full page, we don't need to and
	 * shouldn't clear page extent mapped, as page->private can still
	 * record subpage dirty bits for other part of the range.
	 *
	 * For cases that can invalidate the full even the range doesn't
	 * cover the full page, like invalidating the last page, we're
	 * still safe to wait for ordered extent to finish.
	 */
	if (!(offset == 0 && length == PAGE_SIZE)) {
		btrfs_releasepage(page, GFP_NOFS);
		return;
	}

	if (!inode_evicting)
		lock_extent_bits(tree, page_start, page_end, &cached_state);

	cur = page_start;
	while (cur < page_end) {
		struct btrfs_ordered_extent *ordered;
		bool delete_states;
		u64 range_end;
		u32 range_len;

		ordered = btrfs_lookup_first_ordered_range(inode, cur,
							   page_end + 1 - cur);
		if (!ordered) {
			range_end = page_end;
			/*
			 * No ordered extent covering this range, we are safe
			 * to delete all extent states in the range.
			 */
			delete_states = true;
			goto next;
		}
		if (ordered->file_offset > cur) {
			/*
			 * There is a range between [cur, oe->file_offset) not
			 * covered by any ordered extent.
			 * We are safe to delete all extent states, and handle
			 * the ordered extent in the next iteration.
			 */
			range_end = ordered->file_offset - 1;
			delete_states = true;
			goto next;
		}

		range_end = min(ordered->file_offset + ordered->num_bytes - 1,
				page_end);
		ASSERT(range_end + 1 - cur < U32_MAX);
		range_len = range_end + 1 - cur;
		if (!btrfs_page_test_ordered(fs_info, page, cur, range_len)) {
			/*
			 * If Ordered (Private2) is cleared, it means endio has
			 * already been executed for the range.
			 * We can't delete the extent states as
			 * btrfs_finish_ordered_io() may still use some of them.
			 */
			delete_states = false;
			goto next;
		}
		btrfs_page_clear_ordered(fs_info, page, cur, range_len);

		/*
		 * IO on this page will never be started, so we need to account
		 * for any ordered extents now. Don't clear EXTENT_DELALLOC_NEW
		 * here, must leave that up for the ordered extent completion.
		 *
		 * This will also unlock the range for incoming
		 * btrfs_finish_ordered_io().
		 */
		if (!inode_evicting)
			clear_extent_bit(tree, cur, range_end,
					 EXTENT_DELALLOC |
					 EXTENT_LOCKED | EXTENT_DO_ACCOUNTING |
					 EXTENT_DEFRAG, 1, 0, &cached_state);

		spin_lock_irq(&inode->ordered_tree.lock);
		set_bit(BTRFS_ORDERED_TRUNCATED, &ordered->flags);
		ordered->truncated_len = min(ordered->truncated_len,
					     cur - ordered->file_offset);
		spin_unlock_irq(&inode->ordered_tree.lock);

		if (btrfs_dec_test_ordered_pending(inode, &ordered,
						   cur, range_end + 1 - cur)) {
			btrfs_finish_ordered_io(ordered);
			/*
			 * The ordered extent has finished, now we're again
			 * safe to delete all extent states of the range.
			 */
			delete_states = true;
		} else {
			/*
			 * btrfs_finish_ordered_io() will get executed by endio
			 * of other pages, thus we can't delete extent states
			 * anymore
			 */
			delete_states = false;
		}
next:
		if (ordered)
			btrfs_put_ordered_extent(ordered);
		/*
		 * Qgroup reserved space handler
		 * Sector(s) here will be either:
		 *
		 * 1) Already written to disk or bio already finished
		 *    Then its QGROUP_RESERVED bit in io_tree is already cleared.
		 *    Qgroup will be handled by its qgroup_record then.
		 *    btrfs_qgroup_free_data() call will do nothing here.
		 *
		 * 2) Not written to disk yet
		 *    Then btrfs_qgroup_free_data() call will clear the
		 *    QGROUP_RESERVED bit of its io_tree, and free the qgroup
		 *    reserved data space.
		 *    Since the IO will never happen for this page.
		 */
		btrfs_qgroup_free_data(inode, NULL, cur, range_end + 1 - cur);
		if (!inode_evicting) {
			clear_extent_bit(tree, cur, range_end, EXTENT_LOCKED |
				 EXTENT_DELALLOC | EXTENT_UPTODATE |
				 EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG, 1,
				 delete_states, &cached_state);
		}
		cur = range_end + 1;
	}
	/*
	 * We have iterated through all ordered extents of the page, the page
	 * should not have Ordered (Private2) anymore, or the above iteration
	 * did something wrong.
	 */
	ASSERT(!PageOrdered(page));
	if (!inode_evicting)
		__btrfs_releasepage(page, GFP_NOFS);
	ClearPageChecked(page);
	clear_page_extent_mapped(page);
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
 * truncate_setsize() writes the inode size before removing pages, once we have
 * the page lock we can determine safely if the page is beyond EOF. If it is not
 * beyond EOF, then the page is guaranteed safe against truncation until we
 * unlock the page.
 */
vm_fault_t btrfs_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_changeset *data_reserved = NULL;
	unsigned long zero_start;
	loff_t size;
	vm_fault_t ret;
	int ret2;
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
	ret2 = btrfs_delalloc_reserve_space(BTRFS_I(inode), &data_reserved,
					    page_start, reserved_space);
	if (!ret2) {
		ret2 = file_update_time(vmf->vma->vm_file);
		reserved = 1;
	}
	if (ret2) {
		ret = vmf_error(ret2);
		if (reserved)
			goto out;
		goto out_noreserve;
	}

	ret = VM_FAULT_NOPAGE; /* make the VM retry the fault */
again:
	down_read(&BTRFS_I(inode)->i_mmap_lock);
	lock_page(page);
	size = i_size_read(inode);

	if ((page->mapping != inode->i_mapping) ||
	    (page_start >= size)) {
		/* page got truncated out from underneath us */
		goto out_unlock;
	}
	wait_on_page_writeback(page);

	lock_extent_bits(io_tree, page_start, page_end, &cached_state);
	ret2 = set_page_extent_mapped(page);
	if (ret2 < 0) {
		ret = vmf_error(ret2);
		unlock_extent_cached(io_tree, page_start, page_end, &cached_state);
		goto out_unlock;
	}

	/*
	 * we can't set the delalloc bits if there are pending ordered
	 * extents.  Drop our locks and wait for them to finish
	 */
	ordered = btrfs_lookup_ordered_range(BTRFS_I(inode), page_start,
			PAGE_SIZE);
	if (ordered) {
		unlock_extent_cached(io_tree, page_start, page_end,
				     &cached_state);
		unlock_page(page);
		up_read(&BTRFS_I(inode)->i_mmap_lock);
		btrfs_start_ordered_extent(ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	if (page->index == ((size - 1) >> PAGE_SHIFT)) {
		reserved_space = round_up(size - page_start,
					  fs_info->sectorsize);
		if (reserved_space < PAGE_SIZE) {
			end = page_start + reserved_space - 1;
			btrfs_delalloc_release_space(BTRFS_I(inode),
					data_reserved, page_start,
					PAGE_SIZE - reserved_space, true);
		}
	}

	/*
	 * page_mkwrite gets called when the page is firstly dirtied after it's
	 * faulted in, but write(2) could also dirty a page and set delalloc
	 * bits, thus in this case for space account reason, we still need to
	 * clear any delalloc bits within this page range since we have to
	 * reserve data&meta space before lock_page() (see above comments).
	 */
	clear_extent_bit(&BTRFS_I(inode)->io_tree, page_start, end,
			  EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING |
			  EXTENT_DEFRAG, 0, 0, &cached_state);

	ret2 = btrfs_set_extent_delalloc(BTRFS_I(inode), page_start, end, 0,
					&cached_state);
	if (ret2) {
		unlock_extent_cached(io_tree, page_start, page_end,
				     &cached_state);
		ret = VM_FAULT_SIGBUS;
		goto out_unlock;
	}

	/* page is wholly or partially inside EOF */
	if (page_start + PAGE_SIZE > size)
		zero_start = offset_in_page(size);
	else
		zero_start = PAGE_SIZE;

	if (zero_start != PAGE_SIZE) {
		memzero_page(page, zero_start, PAGE_SIZE - zero_start);
		flush_dcache_page(page);
	}
	ClearPageChecked(page);
	btrfs_page_set_dirty(fs_info, page, page_start, end + 1 - page_start);
	btrfs_page_set_uptodate(fs_info, page, page_start, end + 1 - page_start);

	btrfs_set_inode_last_sub_trans(BTRFS_I(inode));

	unlock_extent_cached(io_tree, page_start, page_end, &cached_state);
	up_read(&BTRFS_I(inode)->i_mmap_lock);

	btrfs_delalloc_release_extents(BTRFS_I(inode), PAGE_SIZE);
	sb_end_pagefault(inode->i_sb);
	extent_changeset_free(data_reserved);
	return VM_FAULT_LOCKED;

out_unlock:
	unlock_page(page);
	up_read(&BTRFS_I(inode)->i_mmap_lock);
out:
	btrfs_delalloc_release_extents(BTRFS_I(inode), PAGE_SIZE);
	btrfs_delalloc_release_space(BTRFS_I(inode), data_reserved, page_start,
				     reserved_space, (ret != 0));
out_noreserve:
	sb_end_pagefault(inode->i_sb);
	extent_changeset_free(data_reserved);
	return ret;
}

static int btrfs_truncate(struct inode *inode, bool skip_writeback)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *rsv;
	int ret;
	struct btrfs_trans_handle *trans;
	u64 mask = fs_info->sectorsize - 1;
	u64 min_size = btrfs_calc_metadata_size(fs_info, 1);
	u64 extents_found = 0;

	if (!skip_writeback) {
		ret = btrfs_wait_ordered_range(inode, inode->i_size & (~mask),
					       (u64)-1);
		if (ret)
			return ret;
	}

	/*
	 * Yes ladies and gentlemen, this is indeed ugly.  We have a couple of
	 * things going on here:
	 *
	 * 1) We need to reserve space to update our inode.
	 *
	 * 2) We need to have something to cache all the space that is going to
	 * be free'd up by the truncate operation, but also have some slack
	 * space reserved in case it uses space during the truncate (thank you
	 * very much snapshotting).
	 *
	 * And we need these to be separate.  The fact is we can use a lot of
	 * space doing the truncate, and we have no earthly idea how much space
	 * we will use, so we need the truncate reservation to be separate so it
	 * doesn't end up using space reserved for updating the inode.  We also
	 * need to be able to stop the transaction and start a new one, which
	 * means we need to be able to update the inode several times, and we
	 * have no idea of knowing how many times that will be, so we can't just
	 * reserve 1 item for the entirety of the operation, so that has to be
	 * done separately as well.
	 *
	 * So that leaves us with
	 *
	 * 1) rsv - for the truncate reservation, which we will steal from the
	 * transaction reservation.
	 * 2) fs_info->trans_block_rsv - this will have 1 items worth left for
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
		ret = PTR_ERR(trans);
		goto out;
	}

	/* Migrate the slack space for the truncate to our reserve */
	ret = btrfs_block_rsv_migrate(&fs_info->trans_block_rsv, rsv,
				      min_size, false);
	BUG_ON(ret);

	trans->block_rsv = rsv;

	while (1) {
		ret = btrfs_truncate_inode_items(trans, root, BTRFS_I(inode),
						 inode->i_size,
						 BTRFS_EXTENT_DATA_KEY,
						 &extents_found);
		trans->block_rsv = &fs_info->trans_block_rsv;
		if (ret != -ENOSPC && ret != -EAGAIN)
			break;

		ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
		if (ret)
			break;

		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);

		trans = btrfs_start_transaction(root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			break;
		}

		btrfs_block_rsv_release(fs_info, rsv, -1, NULL);
		ret = btrfs_block_rsv_migrate(&fs_info->trans_block_rsv,
					      rsv, min_size, false);
		BUG_ON(ret);	/* shouldn't happen */
		trans->block_rsv = rsv;
	}

	/*
	 * We can't call btrfs_truncate_block inside a trans handle as we could
	 * deadlock with freeze, if we got NEED_TRUNCATE_BLOCK then we know
	 * we've truncated everything except the last little bit, and can do
	 * btrfs_truncate_block and then update the disk_i_size.
	 */
	if (ret == NEED_TRUNCATE_BLOCK) {
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);

		ret = btrfs_truncate_block(BTRFS_I(inode), inode->i_size, 0, 0);
		if (ret)
			goto out;
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		btrfs_inode_safe_disk_i_size_write(BTRFS_I(inode), 0);
	}

	if (trans) {
		int ret2;

		trans->block_rsv = &fs_info->trans_block_rsv;
		ret2 = btrfs_update_inode(trans, root, BTRFS_I(inode));
		if (ret2 && !ret)
			ret = ret2;

		ret2 = btrfs_end_transaction(trans);
		if (ret2 && !ret)
			ret = ret2;
		btrfs_btree_balance_dirty(fs_info);
	}
out:
	btrfs_free_block_rsv(fs_info, rsv);
	/*
	 * So if we truncate and then write and fsync we normally would just
	 * write the extents that changed, which is a problem if we need to
	 * first truncate that entire inode.  So set this flag so we write out
	 * all of the extents in the inode to the sync log so we're completely
	 * safe.
	 *
	 * If no extents were dropped or trimmed we don't need to force the next
	 * fsync to truncate all the inode's items from the log and re-log them
	 * all. This means the truncate operation did not change the file size,
	 * or changed it to a smaller size but there was only an implicit hole
	 * between the old i_size and the new i_size, and there were no prealloc
	 * extents beyond i_size to drop.
	 */
	if (extents_found > 0)
		set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(inode)->runtime_flags);

	return ret;
}

/*
 * create a new subvolume directory/inode (helper for the ioctl).
 */
int btrfs_create_subvol_root(struct btrfs_trans_handle *trans,
			     struct btrfs_root *new_root,
			     struct btrfs_root *parent_root,
			     struct user_namespace *mnt_userns)
{
	struct inode *inode;
	int err;
	u64 index = 0;
	u64 ino;

	err = btrfs_get_free_objectid(new_root, &ino);
	if (err < 0)
		return err;

	inode = btrfs_new_inode(trans, new_root, mnt_userns, NULL, "..", 2,
				ino, ino,
				S_IFDIR | (~current_umask() & S_IRWXUGO),
				&index);
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	set_nlink(inode, 1);
	btrfs_i_size_write(BTRFS_I(inode), 0);
	unlock_new_inode(inode);

	err = btrfs_subvol_inherit_props(trans, new_root, parent_root);
	if (err)
		btrfs_err(new_root->fs_info,
			  "error inheriting subvolume %llu properties: %d",
			  new_root->root_key.objectid, err);

	err = btrfs_update_inode(trans, new_root, BTRFS_I(inode));

	iput(inode);
	return err;
}

struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_inode *ei;
	struct inode *inode;

	ei = kmem_cache_alloc(btrfs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;

	ei->root = NULL;
	ei->generation = 0;
	ei->last_trans = 0;
	ei->last_sub_trans = 0;
	ei->logged_trans = 0;
	ei->delalloc_bytes = 0;
	ei->new_delalloc_bytes = 0;
	ei->defrag_bytes = 0;
	ei->disk_i_size = 0;
	ei->flags = 0;
	ei->ro_flags = 0;
	ei->csum_bytes = 0;
	ei->index_cnt = (u64)-1;
	ei->dir_index = 0;
	ei->last_unlink_trans = 0;
	ei->last_reflink_trans = 0;
	ei->last_log_commit = 0;

	spin_lock_init(&ei->lock);
	ei->outstanding_extents = 0;
	if (sb->s_magic != BTRFS_TEST_MAGIC)
		btrfs_init_metadata_block_rsv(fs_info, &ei->block_rsv,
					      BTRFS_BLOCK_RSV_DELALLOC);
	ei->runtime_flags = 0;
	ei->prop_compress = BTRFS_COMPRESS_NONE;
	ei->defrag_compress = BTRFS_COMPRESS_NONE;

	ei->delayed_node = NULL;

	ei->i_otime.tv_sec = 0;
	ei->i_otime.tv_nsec = 0;

	inode = &ei->vfs_inode;
	extent_map_tree_init(&ei->extent_tree);
	extent_io_tree_init(fs_info, &ei->io_tree, IO_TREE_INODE_IO, inode);
	extent_io_tree_init(fs_info, &ei->io_failure_tree,
			    IO_TREE_INODE_IO_FAILURE, inode);
	extent_io_tree_init(fs_info, &ei->file_extent_tree,
			    IO_TREE_INODE_FILE_EXTENT, inode);
	ei->io_tree.track_uptodate = true;
	ei->io_failure_tree.track_uptodate = true;
	atomic_set(&ei->sync_writers, 0);
	mutex_init(&ei->log_mutex);
	btrfs_ordered_inode_tree_init(&ei->ordered_tree);
	INIT_LIST_HEAD(&ei->delalloc_inodes);
	INIT_LIST_HEAD(&ei->delayed_iput);
	RB_CLEAR_NODE(&ei->rb_node);
	init_rwsem(&ei->i_mmap_lock);

	return inode;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_destroy_inode(struct inode *inode)
{
	btrfs_drop_extent_cache(BTRFS_I(inode), 0, (u64)-1, 0);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}
#endif

void btrfs_free_inode(struct inode *inode)
{
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

void btrfs_destroy_inode(struct inode *vfs_inode)
{
	struct btrfs_ordered_extent *ordered;
	struct btrfs_inode *inode = BTRFS_I(vfs_inode);
	struct btrfs_root *root = inode->root;

	WARN_ON(!hlist_empty(&vfs_inode->i_dentry));
	WARN_ON(vfs_inode->i_data.nrpages);
	WARN_ON(inode->block_rsv.reserved);
	WARN_ON(inode->block_rsv.size);
	WARN_ON(inode->outstanding_extents);
	WARN_ON(inode->delalloc_bytes);
	WARN_ON(inode->new_delalloc_bytes);
	WARN_ON(inode->csum_bytes);
	WARN_ON(inode->defrag_bytes);

	/*
	 * This can happen where we create an inode, but somebody else also
	 * created the same inode and we need to destroy the one we already
	 * created.
	 */
	if (!root)
		return;

	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, (u64)-1);
		if (!ordered)
			break;
		else {
			btrfs_err(root->fs_info,
				  "found ordered extent %llu %llu on inode cleanup",
				  ordered->file_offset, ordered->num_bytes);
			btrfs_remove_ordered_extent(inode, ordered);
			btrfs_put_ordered_extent(ordered);
			btrfs_put_ordered_extent(ordered);
		}
	}
	btrfs_qgroup_check_reserved_leak(inode);
	inode_tree_del(inode);
	btrfs_drop_extent_cache(inode, 0, (u64)-1, 0);
	btrfs_inode_clear_file_extent_range(inode, 0, (u64)-1);
	btrfs_put_root(inode->root);
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

void __cold btrfs_destroy_cachep(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(btrfs_inode_cachep);
	kmem_cache_destroy(btrfs_trans_handle_cachep);
	kmem_cache_destroy(btrfs_path_cachep);
	kmem_cache_destroy(btrfs_free_space_cachep);
	kmem_cache_destroy(btrfs_free_space_bitmap_cachep);
}

int __init btrfs_init_cachep(void)
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

	btrfs_free_space_bitmap_cachep = kmem_cache_create("btrfs_free_space_bitmap",
							PAGE_SIZE, PAGE_SIZE,
							SLAB_MEM_SPREAD, NULL);
	if (!btrfs_free_space_bitmap_cachep)
		goto fail;

	return 0;
fail:
	btrfs_destroy_cachep();
	return -ENOMEM;
}

static int btrfs_getattr(struct user_namespace *mnt_userns,
			 const struct path *path, struct kstat *stat,
			 u32 request_mask, unsigned int flags)
{
	u64 delalloc_bytes;
	u64 inode_bytes;
	struct inode *inode = d_inode(path->dentry);
	u32 blocksize = inode->i_sb->s_blocksize;
	u32 bi_flags = BTRFS_I(inode)->flags;
	u32 bi_ro_flags = BTRFS_I(inode)->ro_flags;

	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = BTRFS_I(inode)->i_otime.tv_sec;
	stat->btime.tv_nsec = BTRFS_I(inode)->i_otime.tv_nsec;
	if (bi_flags & BTRFS_INODE_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	if (bi_flags & BTRFS_INODE_COMPRESS)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (bi_flags & BTRFS_INODE_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (bi_flags & BTRFS_INODE_NODUMP)
		stat->attributes |= STATX_ATTR_NODUMP;
	if (bi_ro_flags & BTRFS_INODE_RO_VERITY)
		stat->attributes |= STATX_ATTR_VERITY;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
				  STATX_ATTR_COMPRESSED |
				  STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_NODUMP);

	generic_fillattr(mnt_userns, inode, stat);
	stat->dev = BTRFS_I(inode)->root->anon_dev;

	spin_lock(&BTRFS_I(inode)->lock);
	delalloc_bytes = BTRFS_I(inode)->new_delalloc_bytes;
	inode_bytes = inode_get_bytes(inode);
	spin_unlock(&BTRFS_I(inode)->lock);
	stat->blocks = (ALIGN(inode_bytes, blocksize) +
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
	struct timespec64 ctime = current_time(old_inode);
	u64 old_ino = btrfs_ino(BTRFS_I(old_inode));
	u64 new_ino = btrfs_ino(BTRFS_I(new_inode));
	u64 old_idx = 0;
	u64 new_idx = 0;
	int ret;
	int ret2;
	bool root_log_pinned = false;
	bool dest_log_pinned = false;
	bool need_abort = false;

	/*
	 * For non-subvolumes allow exchange only within one subvolume, in the
	 * same inode namespace. Two subvolumes (represented as directory) can
	 * be exchanged as they're a logical link and have a fixed inode number.
	 */
	if (root != dest &&
	    (old_ino != BTRFS_FIRST_FREE_OBJECTID ||
	     new_ino != BTRFS_FIRST_FREE_OBJECTID))
		return -EXDEV;

	/* close the race window with snapshot create/destroy ioctl */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID ||
	    new_ino == BTRFS_FIRST_FREE_OBJECTID)
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

	if (dest != root) {
		ret = btrfs_record_root_in_trans(trans, dest);
		if (ret)
			goto out_fail;
	}

	/*
	 * We need to find a free sequence number both in the source and
	 * in the destination directory for the exchange.
	 */
	ret = btrfs_set_inode_index(BTRFS_I(new_dir), &old_idx);
	if (ret)
		goto out_fail;
	ret = btrfs_set_inode_index(BTRFS_I(old_dir), &new_idx);
	if (ret)
		goto out_fail;

	BTRFS_I(old_inode)->dir_index = 0ULL;
	BTRFS_I(new_inode)->dir_index = 0ULL;

	/* Reference for the source. */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(trans);
	} else {
		ret = btrfs_insert_inode_ref(trans, dest,
					     new_dentry->d_name.name,
					     new_dentry->d_name.len,
					     old_ino,
					     btrfs_ino(BTRFS_I(new_dir)),
					     old_idx);
		if (ret)
			goto out_fail;
		need_abort = true;
	}

	/* And now for the dest. */
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(trans);
	} else {
		ret = btrfs_insert_inode_ref(trans, root,
					     old_dentry->d_name.name,
					     old_dentry->d_name.len,
					     new_ino,
					     btrfs_ino(BTRFS_I(old_dir)),
					     new_idx);
		if (ret) {
			if (need_abort)
				btrfs_abort_transaction(trans, ret);
			goto out_fail;
		}
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
		btrfs_record_unlink_dir(trans, BTRFS_I(old_dir),
				BTRFS_I(old_inode), 1);
		btrfs_record_unlink_dir(trans, BTRFS_I(new_dir),
				BTRFS_I(new_inode), 1);
	}

	/*
	 * Now pin the logs of the roots. We do it to ensure that no other task
	 * can sync the logs while we are in progress with the rename, because
	 * that could result in an inconsistency in case any of the inodes that
	 * are part of this rename operation were logged before.
	 *
	 * We pin the logs even if at this precise moment none of the inodes was
	 * logged before. This is because right after we checked for that, some
	 * other task fsyncing some other inode not involved with this rename
	 * operation could log that one of our inodes exists.
	 *
	 * We don't need to pin the logs before the above calls to
	 * btrfs_insert_inode_ref(), since those don't ever need to change a log.
	 */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID) {
		btrfs_pin_log_trans(root);
		root_log_pinned = true;
	}
	if (new_ino != BTRFS_FIRST_FREE_OBJECTID) {
		btrfs_pin_log_trans(dest);
		dest_log_pinned = true;
	}

	/* src is a subvolume */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_unlink_subvol(trans, old_dir, old_dentry);
	} else { /* src is an inode */
		ret = __btrfs_unlink_inode(trans, root, BTRFS_I(old_dir),
					   BTRFS_I(old_dentry->d_inode),
					   old_dentry->d_name.name,
					   old_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_inode(trans, root, BTRFS_I(old_inode));
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	/* dest is a subvolume */
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_unlink_subvol(trans, new_dir, new_dentry);
	} else { /* dest is an inode */
		ret = __btrfs_unlink_inode(trans, dest, BTRFS_I(new_dir),
					   BTRFS_I(new_dentry->d_inode),
					   new_dentry->d_name.name,
					   new_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_inode(trans, dest, BTRFS_I(new_inode));
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, BTRFS_I(new_dir), BTRFS_I(old_inode),
			     new_dentry->d_name.name,
			     new_dentry->d_name.len, 0, old_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, BTRFS_I(old_dir), BTRFS_I(new_inode),
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
		btrfs_log_new_name(trans, BTRFS_I(old_inode), BTRFS_I(old_dir),
				   new_dentry->d_parent);
		btrfs_end_log_trans(root);
		root_log_pinned = false;
	}
	if (dest_log_pinned) {
		btrfs_log_new_name(trans, BTRFS_I(new_inode), BTRFS_I(new_dir),
				   old_dentry->d_parent);
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
		if (btrfs_inode_in_log(BTRFS_I(old_dir), fs_info->generation) ||
		    btrfs_inode_in_log(BTRFS_I(new_dir), fs_info->generation) ||
		    btrfs_inode_in_log(BTRFS_I(old_inode), fs_info->generation) ||
		    btrfs_inode_in_log(BTRFS_I(new_inode), fs_info->generation))
			btrfs_set_log_full_commit(trans);

		if (root_log_pinned) {
			btrfs_end_log_trans(root);
			root_log_pinned = false;
		}
		if (dest_log_pinned) {
			btrfs_end_log_trans(dest);
			dest_log_pinned = false;
		}
	}
	ret2 = btrfs_end_transaction(trans);
	ret = ret ? ret : ret2;
out_notrans:
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID ||
	    old_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	return ret;
}

static int btrfs_whiteout_for_rename(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct user_namespace *mnt_userns,
				     struct inode *dir,
				     struct dentry *dentry)
{
	int ret;
	struct inode *inode;
	u64 objectid;
	u64 index;

	ret = btrfs_get_free_objectid(root, &objectid);
	if (ret)
		return ret;

	inode = btrfs_new_inode(trans, root, mnt_userns, dir,
				dentry->d_name.name,
				dentry->d_name.len,
				btrfs_ino(BTRFS_I(dir)),
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

	ret = btrfs_add_nondir(trans, BTRFS_I(dir), dentry,
				BTRFS_I(inode), 0, index);
	if (ret)
		goto out;

	ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
out:
	unlock_new_inode(inode);
	if (ret)
		inode_dec_link_count(inode);
	iput(inode);

	return ret;
}

static int btrfs_rename(struct user_namespace *mnt_userns,
			struct inode *old_dir, struct dentry *old_dentry,
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
	int ret;
	int ret2;
	u64 old_ino = btrfs_ino(BTRFS_I(old_inode));
	bool log_pinned = false;

	if (btrfs_ino(BTRFS_I(new_dir)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)
		return -EPERM;

	/* we only allow rename subvolume link between subvolumes */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID && root != dest)
		return -EXDEV;

	if (old_ino == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID ||
	    (new_inode && btrfs_ino(BTRFS_I(new_inode)) == BTRFS_FIRST_FREE_OBJECTID))
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

	if (dest != root) {
		ret = btrfs_record_root_in_trans(trans, dest);
		if (ret)
			goto out_fail;
	}

	ret = btrfs_set_inode_index(BTRFS_I(new_dir), &index);
	if (ret)
		goto out_fail;

	BTRFS_I(old_inode)->dir_index = 0ULL;
	if (unlikely(old_ino == BTRFS_FIRST_FREE_OBJECTID)) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(trans);
	} else {
		ret = btrfs_insert_inode_ref(trans, dest,
					     new_dentry->d_name.name,
					     new_dentry->d_name.len,
					     old_ino,
					     btrfs_ino(BTRFS_I(new_dir)), index);
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
		btrfs_record_unlink_dir(trans, BTRFS_I(old_dir),
				BTRFS_I(old_inode), 1);

	if (unlikely(old_ino == BTRFS_FIRST_FREE_OBJECTID)) {
		ret = btrfs_unlink_subvol(trans, old_dir, old_dentry);
	} else {
		/*
		 * Now pin the log. We do it to ensure that no other task can
		 * sync the log while we are in progress with the rename, as
		 * that could result in an inconsistency in case any of the
		 * inodes that are part of this rename operation were logged
		 * before.
		 *
		 * We pin the log even if at this precise moment none of the
		 * inodes was logged before. This is because right after we
		 * checked for that, some other task fsyncing some other inode
		 * not involved with this rename operation could log that one of
		 * our inodes exists.
		 *
		 * We don't need to pin the logs before the above call to
		 * btrfs_insert_inode_ref(), since that does not need to change
		 * a log.
		 */
		btrfs_pin_log_trans(root);
		log_pinned = true;
		ret = __btrfs_unlink_inode(trans, root, BTRFS_I(old_dir),
					BTRFS_I(d_inode(old_dentry)),
					old_dentry->d_name.name,
					old_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_inode(trans, root, BTRFS_I(old_inode));
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (new_inode) {
		inode_inc_iversion(new_inode);
		new_inode->i_ctime = current_time(new_inode);
		if (unlikely(btrfs_ino(BTRFS_I(new_inode)) ==
			     BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
			ret = btrfs_unlink_subvol(trans, new_dir, new_dentry);
			BUG_ON(new_inode->i_nlink == 0);
		} else {
			ret = btrfs_unlink_inode(trans, dest, BTRFS_I(new_dir),
						 BTRFS_I(d_inode(new_dentry)),
						 new_dentry->d_name.name,
						 new_dentry->d_name.len);
		}
		if (!ret && new_inode->i_nlink == 0)
			ret = btrfs_orphan_add(trans,
					BTRFS_I(d_inode(new_dentry)));
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_fail;
		}
	}

	ret = btrfs_add_link(trans, BTRFS_I(new_dir), BTRFS_I(old_inode),
			     new_dentry->d_name.name,
			     new_dentry->d_name.len, 0, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_inode->i_nlink == 1)
		BTRFS_I(old_inode)->dir_index = index;

	if (log_pinned) {
		btrfs_log_new_name(trans, BTRFS_I(old_inode), BTRFS_I(old_dir),
				   new_dentry->d_parent);
		btrfs_end_log_trans(root);
		log_pinned = false;
	}

	if (flags & RENAME_WHITEOUT) {
		ret = btrfs_whiteout_for_rename(trans, root, mnt_userns,
						old_dir, old_dentry);

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
		if (btrfs_inode_in_log(BTRFS_I(old_dir), fs_info->generation) ||
		    btrfs_inode_in_log(BTRFS_I(new_dir), fs_info->generation) ||
		    btrfs_inode_in_log(BTRFS_I(old_inode), fs_info->generation) ||
		    (new_inode &&
		     btrfs_inode_in_log(BTRFS_I(new_inode), fs_info->generation)))
			btrfs_set_log_full_commit(trans);

		btrfs_end_log_trans(root);
		log_pinned = false;
	}
	ret2 = btrfs_end_transaction(trans);
	ret = ret ? ret : ret2;
out_notrans:
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	return ret;
}

static int btrfs_rename2(struct user_namespace *mnt_userns, struct inode *old_dir,
			 struct dentry *old_dentry, struct inode *new_dir,
			 struct dentry *new_dentry, unsigned int flags)
{
	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return btrfs_rename_exchange(old_dir, old_dentry, new_dir,
					  new_dentry);

	return btrfs_rename(mnt_userns, old_dir, old_dentry, new_dir,
			    new_dentry, flags);
}

struct btrfs_delalloc_work {
	struct inode *inode;
	struct completion completion;
	struct list_head list;
	struct btrfs_work work;
};

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

	iput(inode);
	complete(&delalloc_work->completion);
}

static struct btrfs_delalloc_work *btrfs_alloc_delalloc_work(struct inode *inode)
{
	struct btrfs_delalloc_work *work;

	work = kmalloc(sizeof(*work), GFP_NOFS);
	if (!work)
		return NULL;

	init_completion(&work->completion);
	INIT_LIST_HEAD(&work->list);
	work->inode = inode;
	btrfs_init_work(&work->work, btrfs_run_delalloc_work, NULL, NULL);

	return work;
}

/*
 * some fairly slow code that needs optimization. This walks the list
 * of all the inodes with pending delalloc and forces them to disk.
 */
static int start_delalloc_inodes(struct btrfs_root *root,
				 struct writeback_control *wbc, bool snapshot,
				 bool in_reclaim_context)
{
	struct btrfs_inode *binode;
	struct inode *inode;
	struct btrfs_delalloc_work *work, *next;
	struct list_head works;
	struct list_head splice;
	int ret = 0;
	bool full_flush = wbc->nr_to_write == LONG_MAX;

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

		if (in_reclaim_context &&
		    test_bit(BTRFS_INODE_NO_DELALLOC_FLUSH, &binode->runtime_flags))
			continue;

		inode = igrab(&binode->vfs_inode);
		if (!inode) {
			cond_resched_lock(&root->delalloc_lock);
			continue;
		}
		spin_unlock(&root->delalloc_lock);

		if (snapshot)
			set_bit(BTRFS_INODE_SNAPSHOT_FLUSH,
				&binode->runtime_flags);
		if (full_flush) {
			work = btrfs_alloc_delalloc_work(inode);
			if (!work) {
				iput(inode);
				ret = -ENOMEM;
				goto out;
			}
			list_add_tail(&work->list, &works);
			btrfs_queue_work(root->fs_info->flush_workers,
					 &work->work);
		} else {
			ret = filemap_fdatawrite_wbc(inode->i_mapping, wbc);
			btrfs_add_delayed_iput(inode);
			if (ret || wbc->nr_to_write <= 0)
				goto out;
		}
		cond_resched();
		spin_lock(&root->delalloc_lock);
	}
	spin_unlock(&root->delalloc_lock);

out:
	list_for_each_entry_safe(work, next, &works, list) {
		list_del_init(&work->list);
		wait_for_completion(&work->completion);
		kfree(work);
	}

	if (!list_empty(&splice)) {
		spin_lock(&root->delalloc_lock);
		list_splice_tail(&splice, &root->delalloc_inodes);
		spin_unlock(&root->delalloc_lock);
	}
	mutex_unlock(&root->delalloc_mutex);
	return ret;
}

int btrfs_start_delalloc_snapshot(struct btrfs_root *root, bool in_reclaim_context)
{
	struct writeback_control wbc = {
		.nr_to_write = LONG_MAX,
		.sync_mode = WB_SYNC_NONE,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		return -EROFS;

	return start_delalloc_inodes(root, &wbc, true, in_reclaim_context);
}

int btrfs_start_delalloc_roots(struct btrfs_fs_info *fs_info, long nr,
			       bool in_reclaim_context)
{
	struct writeback_control wbc = {
		.nr_to_write = nr,
		.sync_mode = WB_SYNC_NONE,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};
	struct btrfs_root *root;
	struct list_head splice;
	int ret;

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		return -EROFS;

	INIT_LIST_HEAD(&splice);

	mutex_lock(&fs_info->delalloc_root_mutex);
	spin_lock(&fs_info->delalloc_root_lock);
	list_splice_init(&fs_info->delalloc_roots, &splice);
	while (!list_empty(&splice)) {
		/*
		 * Reset nr_to_write here so we know that we're doing a full
		 * flush.
		 */
		if (nr == LONG_MAX)
			wbc.nr_to_write = LONG_MAX;

		root = list_first_entry(&splice, struct btrfs_root,
					delalloc_root);
		root = btrfs_grab_root(root);
		BUG_ON(!root);
		list_move_tail(&root->delalloc_root,
			       &fs_info->delalloc_roots);
		spin_unlock(&fs_info->delalloc_root_lock);

		ret = start_delalloc_inodes(root, &wbc, false, in_reclaim_context);
		btrfs_put_root(root);
		if (ret < 0 || wbc.nr_to_write <= 0)
			goto out;
		spin_lock(&fs_info->delalloc_root_lock);
	}
	spin_unlock(&fs_info->delalloc_root_lock);

	ret = 0;
out:
	if (!list_empty(&splice)) {
		spin_lock(&fs_info->delalloc_root_lock);
		list_splice_tail(&splice, &fs_info->delalloc_roots);
		spin_unlock(&fs_info->delalloc_root_lock);
	}
	mutex_unlock(&fs_info->delalloc_root_mutex);
	return ret;
}

static int btrfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct inode *inode = NULL;
	int err;
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

	err = btrfs_get_free_objectid(root, &objectid);
	if (err)
		goto out_unlock;

	inode = btrfs_new_inode(trans, root, mnt_userns, dir,
				dentry->d_name.name, dentry->d_name.len,
				btrfs_ino(BTRFS_I(dir)), objectid,
				S_IFLNK | S_IRWXUGO, &index);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		inode = NULL;
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

	err = btrfs_init_inode_security(trans, inode, dir, &dentry->d_name);
	if (err)
		goto out_unlock;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out_unlock;
	}
	key.objectid = btrfs_ino(BTRFS_I(inode));
	key.offset = 0;
	key.type = BTRFS_EXTENT_DATA_KEY;
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (err) {
		btrfs_free_path(path);
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
	inode_nohighmem(inode);
	inode_set_bytes(inode, name_len);
	btrfs_i_size_write(BTRFS_I(inode), name_len);
	err = btrfs_update_inode(trans, root, BTRFS_I(inode));
	/*
	 * Last step, add directory indexes for our symlink inode. This is the
	 * last step to avoid extra cleanup of these indexes if an error happens
	 * elsewhere above.
	 */
	if (!err)
		err = btrfs_add_nondir(trans, BTRFS_I(dir), dentry,
				BTRFS_I(inode), 0, index);
	if (err)
		goto out_unlock;

	d_instantiate_new(dentry, inode);

out_unlock:
	btrfs_end_transaction(trans);
	if (err && inode) {
		inode_dec_link_count(inode);
		discard_new_inode(inode);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static struct btrfs_trans_handle *insert_prealloc_file_extent(
				       struct btrfs_trans_handle *trans_in,
				       struct btrfs_inode *inode,
				       struct btrfs_key *ins,
				       u64 file_offset)
{
	struct btrfs_file_extent_item stack_fi;
	struct btrfs_replace_extent_info extent_info;
	struct btrfs_trans_handle *trans = trans_in;
	struct btrfs_path *path;
	u64 start = ins->objectid;
	u64 len = ins->offset;
	int qgroup_released;
	int ret;

	memset(&stack_fi, 0, sizeof(stack_fi));

	btrfs_set_stack_file_extent_type(&stack_fi, BTRFS_FILE_EXTENT_PREALLOC);
	btrfs_set_stack_file_extent_disk_bytenr(&stack_fi, start);
	btrfs_set_stack_file_extent_disk_num_bytes(&stack_fi, len);
	btrfs_set_stack_file_extent_num_bytes(&stack_fi, len);
	btrfs_set_stack_file_extent_ram_bytes(&stack_fi, len);
	btrfs_set_stack_file_extent_compression(&stack_fi, BTRFS_COMPRESS_NONE);
	/* Encryption and other encoding is reserved and all 0 */

	qgroup_released = btrfs_qgroup_release_data(inode, file_offset, len);
	if (qgroup_released < 0)
		return ERR_PTR(qgroup_released);

	if (trans) {
		ret = insert_reserved_file_extent(trans, inode,
						  file_offset, &stack_fi,
						  true, qgroup_released);
		if (ret)
			goto free_qgroup;
		return trans;
	}

	extent_info.disk_offset = start;
	extent_info.disk_len = len;
	extent_info.data_offset = 0;
	extent_info.data_len = len;
	extent_info.file_offset = file_offset;
	extent_info.extent_buf = (char *)&stack_fi;
	extent_info.is_new_extent = true;
	extent_info.qgroup_reserved = qgroup_released;
	extent_info.insertions = 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto free_qgroup;
	}

	ret = btrfs_replace_file_extents(inode, path, file_offset,
				     file_offset + len - 1, &extent_info,
				     &trans);
	btrfs_free_path(path);
	if (ret)
		goto free_qgroup;
	return trans;

free_qgroup:
	/*
	 * We have released qgroup data range at the beginning of the function,
	 * and normally qgroup_released bytes will be freed when committing
	 * transaction.
	 * But if we error out early, we have to free what we have released
	 * or we leak qgroup data reservation.
	 */
	btrfs_qgroup_free_refroot(inode->root->fs_info,
			inode->root->root_key.objectid, qgroup_released,
			BTRFS_QGROUP_RSV_DATA);
	return ERR_PTR(ret);
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
	u64 clear_offset = start;
	u64 i_size;
	u64 cur_bytes;
	u64 last_alloc = (u64)-1;
	int ret = 0;
	bool own_trans = true;
	u64 end = start + num_bytes - 1;

	if (trans)
		own_trans = false;
	while (num_bytes > 0) {
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
		if (ret)
			break;

		/*
		 * We've reserved this space, and thus converted it from
		 * ->bytes_may_use to ->bytes_reserved.  Any error that happens
		 * from here on out we will only need to clear our reservation
		 * for the remaining unreserved area, so advance our
		 * clear_offset by our extent size.
		 */
		clear_offset += ins.offset;

		last_alloc = ins.offset;
		trans = insert_prealloc_file_extent(trans, BTRFS_I(inode),
						    &ins, cur_offset);
		/*
		 * Now that we inserted the prealloc extent we can finally
		 * decrement the number of reservations in the block group.
		 * If we did it before, we could race with relocation and have
		 * relocation miss the reserved extent, making it fail later.
		 */
		btrfs_dec_block_group_reservations(fs_info, ins.objectid);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			btrfs_free_reserved_extent(fs_info, ins.objectid,
						   ins.offset, 0);
			break;
		}

		btrfs_drop_extent_cache(BTRFS_I(inode), cur_offset,
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
		set_bit(EXTENT_FLAG_PREALLOC, &em->flags);
		em->generation = trans->transid;

		while (1) {
			write_lock(&em_tree->lock);
			ret = add_extent_mapping(em_tree, em, 1);
			write_unlock(&em_tree->lock);
			if (ret != -EEXIST)
				break;
			btrfs_drop_extent_cache(BTRFS_I(inode), cur_offset,
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
			btrfs_inode_safe_disk_i_size_write(BTRFS_I(inode), 0);
		}

		ret = btrfs_update_inode(trans, root, BTRFS_I(inode));

		if (ret) {
			btrfs_abort_transaction(trans, ret);
			if (own_trans)
				btrfs_end_transaction(trans);
			break;
		}

		if (own_trans) {
			btrfs_end_transaction(trans);
			trans = NULL;
		}
	}
	if (clear_offset < end)
		btrfs_free_reserved_data_space(BTRFS_I(inode), NULL, clear_offset,
			end - clear_offset + 1);
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

static int btrfs_permission(struct user_namespace *mnt_userns,
			    struct inode *inode, int mask)
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
	return generic_permission(mnt_userns, inode, mask);
}

static int btrfs_tmpfile(struct user_namespace *mnt_userns, struct inode *dir,
			 struct dentry *dentry, umode_t mode)
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

	ret = btrfs_get_free_objectid(root, &objectid);
	if (ret)
		goto out;

	inode = btrfs_new_inode(trans, root, mnt_userns, dir, NULL, 0,
			btrfs_ino(BTRFS_I(dir)), objectid, mode, &index);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		inode = NULL;
		goto out;
	}

	inode->i_fop = &btrfs_file_operations;
	inode->i_op = &btrfs_file_inode_operations;

	inode->i_mapping->a_ops = &btrfs_aops;

	ret = btrfs_init_inode_security(trans, inode, dir, NULL);
	if (ret)
		goto out;

	ret = btrfs_update_inode(trans, root, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = btrfs_orphan_add(trans, BTRFS_I(inode));
	if (ret)
		goto out;

	/*
	 * We set number of links to 0 in btrfs_new_inode(), and here we set
	 * it to 1 because d_tmpfile() will issue a warning if the count is 0,
	 * through:
	 *
	 *    d_tmpfile() -> inode_dec_link_count() -> drop_nlink()
	 */
	set_nlink(inode, 1);
	d_tmpfile(dentry, inode);
	unlock_new_inode(inode);
	mark_inode_dirty(inode);
out:
	btrfs_end_transaction(trans);
	if (ret && inode)
		discard_new_inode(inode);
	btrfs_btree_balance_dirty(fs_info);
	return ret;
}

void btrfs_set_range_writeback(struct btrfs_inode *inode, u64 start, u64 end)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	struct page *page;
	u32 len;

	ASSERT(end + 1 - start <= U32_MAX);
	len = end + 1 - start;
	while (index <= end_index) {
		page = find_get_page(inode->vfs_inode.i_mapping, index);
		ASSERT(page); /* Pages should be in the extent_io_tree */

		btrfs_page_set_writeback(fs_info, page, start, len);
		put_page(page);
		index++;
	}
}

#ifdef CONFIG_SWAP
/*
 * Add an entry indicating a block group or device which is pinned by a
 * swapfile. Returns 0 on success, 1 if there is already an entry for it, or a
 * negative errno on failure.
 */
static int btrfs_add_swapfile_pin(struct inode *inode, void *ptr,
				  bool is_block_group)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct btrfs_swapfile_pin *sp, *entry;
	struct rb_node **p;
	struct rb_node *parent = NULL;

	sp = kmalloc(sizeof(*sp), GFP_NOFS);
	if (!sp)
		return -ENOMEM;
	sp->ptr = ptr;
	sp->inode = inode;
	sp->is_block_group = is_block_group;
	sp->bg_extent_count = 1;

	spin_lock(&fs_info->swapfile_pins_lock);
	p = &fs_info->swapfile_pins.rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_swapfile_pin, node);
		if (sp->ptr < entry->ptr ||
		    (sp->ptr == entry->ptr && sp->inode < entry->inode)) {
			p = &(*p)->rb_left;
		} else if (sp->ptr > entry->ptr ||
			   (sp->ptr == entry->ptr && sp->inode > entry->inode)) {
			p = &(*p)->rb_right;
		} else {
			if (is_block_group)
				entry->bg_extent_count++;
			spin_unlock(&fs_info->swapfile_pins_lock);
			kfree(sp);
			return 1;
		}
	}
	rb_link_node(&sp->node, parent, p);
	rb_insert_color(&sp->node, &fs_info->swapfile_pins);
	spin_unlock(&fs_info->swapfile_pins_lock);
	return 0;
}

/* Free all of the entries pinned by this swapfile. */
static void btrfs_free_swapfile_pins(struct inode *inode)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct btrfs_swapfile_pin *sp;
	struct rb_node *node, *next;

	spin_lock(&fs_info->swapfile_pins_lock);
	node = rb_first(&fs_info->swapfile_pins);
	while (node) {
		next = rb_next(node);
		sp = rb_entry(node, struct btrfs_swapfile_pin, node);
		if (sp->inode == inode) {
			rb_erase(&sp->node, &fs_info->swapfile_pins);
			if (sp->is_block_group) {
				btrfs_dec_block_group_swap_extents(sp->ptr,
							   sp->bg_extent_count);
				btrfs_put_block_group(sp->ptr);
			}
			kfree(sp);
		}
		node = next;
	}
	spin_unlock(&fs_info->swapfile_pins_lock);
}

struct btrfs_swap_info {
	u64 start;
	u64 block_start;
	u64 block_len;
	u64 lowest_ppage;
	u64 highest_ppage;
	unsigned long nr_pages;
	int nr_extents;
};

static int btrfs_add_swap_extent(struct swap_info_struct *sis,
				 struct btrfs_swap_info *bsi)
{
	unsigned long nr_pages;
	u64 first_ppage, first_ppage_reported, next_ppage;
	int ret;

	first_ppage = ALIGN(bsi->block_start, PAGE_SIZE) >> PAGE_SHIFT;
	next_ppage = ALIGN_DOWN(bsi->block_start + bsi->block_len,
				PAGE_SIZE) >> PAGE_SHIFT;

	if (first_ppage >= next_ppage)
		return 0;
	nr_pages = next_ppage - first_ppage;

	first_ppage_reported = first_ppage;
	if (bsi->start == 0)
		first_ppage_reported++;
	if (bsi->lowest_ppage > first_ppage_reported)
		bsi->lowest_ppage = first_ppage_reported;
	if (bsi->highest_ppage < (next_ppage - 1))
		bsi->highest_ppage = next_ppage - 1;

	ret = add_swap_extent(sis, bsi->nr_pages, nr_pages, first_ppage);
	if (ret < 0)
		return ret;
	bsi->nr_extents += ret;
	bsi->nr_pages += nr_pages;
	return 0;
}

static void btrfs_swap_deactivate(struct file *file)
{
	struct inode *inode = file_inode(file);

	btrfs_free_swapfile_pins(inode);
	atomic_dec(&BTRFS_I(inode)->root->nr_swapfiles);
}

static int btrfs_swap_activate(struct swap_info_struct *sis, struct file *file,
			       sector_t *span)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_state *cached_state = NULL;
	struct extent_map *em = NULL;
	struct btrfs_device *device = NULL;
	struct btrfs_swap_info bsi = {
		.lowest_ppage = (sector_t)-1ULL,
	};
	int ret = 0;
	u64 isize;
	u64 start;

	/*
	 * If the swap file was just created, make sure delalloc is done. If the
	 * file changes again after this, the user is doing something stupid and
	 * we don't really care.
	 */
	ret = btrfs_wait_ordered_range(inode, 0, (u64)-1);
	if (ret)
		return ret;

	/*
	 * The inode is locked, so these flags won't change after we check them.
	 */
	if (BTRFS_I(inode)->flags & BTRFS_INODE_COMPRESS) {
		btrfs_warn(fs_info, "swapfile must not be compressed");
		return -EINVAL;
	}
	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW)) {
		btrfs_warn(fs_info, "swapfile must not be copy-on-write");
		return -EINVAL;
	}
	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM)) {
		btrfs_warn(fs_info, "swapfile must not be checksummed");
		return -EINVAL;
	}

	/*
	 * Balance or device remove/replace/resize can move stuff around from
	 * under us. The exclop protection makes sure they aren't running/won't
	 * run concurrently while we are mapping the swap extents, and
	 * fs_info->swapfile_pins prevents them from running while the swap
	 * file is active and moving the extents. Note that this also prevents
	 * a concurrent device add which isn't actually necessary, but it's not
	 * really worth the trouble to allow it.
	 */
	if (!btrfs_exclop_start(fs_info, BTRFS_EXCLOP_SWAP_ACTIVATE)) {
		btrfs_warn(fs_info,
	   "cannot activate swapfile while exclusive operation is running");
		return -EBUSY;
	}

	/*
	 * Prevent snapshot creation while we are activating the swap file.
	 * We do not want to race with snapshot creation. If snapshot creation
	 * already started before we bumped nr_swapfiles from 0 to 1 and
	 * completes before the first write into the swap file after it is
	 * activated, than that write would fallback to COW.
	 */
	if (!btrfs_drew_try_write_lock(&root->snapshot_lock)) {
		btrfs_exclop_finish(fs_info);
		btrfs_warn(fs_info,
	   "cannot activate swapfile because snapshot creation is in progress");
		return -EINVAL;
	}
	/*
	 * Snapshots can create extents which require COW even if NODATACOW is
	 * set. We use this counter to prevent snapshots. We must increment it
	 * before walking the extents because we don't want a concurrent
	 * snapshot to run after we've already checked the extents.
	 */
	atomic_inc(&root->nr_swapfiles);

	isize = ALIGN_DOWN(inode->i_size, fs_info->sectorsize);

	lock_extent_bits(io_tree, 0, isize - 1, &cached_state);
	start = 0;
	while (start < isize) {
		u64 logical_block_start, physical_block_start;
		struct btrfs_block_group *bg;
		u64 len = isize - start;

		em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, start, len);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out;
		}

		if (em->block_start == EXTENT_MAP_HOLE) {
			btrfs_warn(fs_info, "swapfile must not have holes");
			ret = -EINVAL;
			goto out;
		}
		if (em->block_start == EXTENT_MAP_INLINE) {
			/*
			 * It's unlikely we'll ever actually find ourselves
			 * here, as a file small enough to fit inline won't be
			 * big enough to store more than the swap header, but in
			 * case something changes in the future, let's catch it
			 * here rather than later.
			 */
			btrfs_warn(fs_info, "swapfile must not be inline");
			ret = -EINVAL;
			goto out;
		}
		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
			btrfs_warn(fs_info, "swapfile must not be compressed");
			ret = -EINVAL;
			goto out;
		}

		logical_block_start = em->block_start + (start - em->start);
		len = min(len, em->len - (start - em->start));
		free_extent_map(em);
		em = NULL;

		ret = can_nocow_extent(inode, start, &len, NULL, NULL, NULL, true);
		if (ret < 0) {
			goto out;
		} else if (ret) {
			ret = 0;
		} else {
			btrfs_warn(fs_info,
				   "swapfile must not be copy-on-write");
			ret = -EINVAL;
			goto out;
		}

		em = btrfs_get_chunk_map(fs_info, logical_block_start, len);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out;
		}

		if (em->map_lookup->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
			btrfs_warn(fs_info,
				   "swapfile must have single data profile");
			ret = -EINVAL;
			goto out;
		}

		if (device == NULL) {
			device = em->map_lookup->stripes[0].dev;
			ret = btrfs_add_swapfile_pin(inode, device, false);
			if (ret == 1)
				ret = 0;
			else if (ret)
				goto out;
		} else if (device != em->map_lookup->stripes[0].dev) {
			btrfs_warn(fs_info, "swapfile must be on one device");
			ret = -EINVAL;
			goto out;
		}

		physical_block_start = (em->map_lookup->stripes[0].physical +
					(logical_block_start - em->start));
		len = min(len, em->len - (logical_block_start - em->start));
		free_extent_map(em);
		em = NULL;

		bg = btrfs_lookup_block_group(fs_info, logical_block_start);
		if (!bg) {
			btrfs_warn(fs_info,
			   "could not find block group containing swapfile");
			ret = -EINVAL;
			goto out;
		}

		if (!btrfs_inc_block_group_swap_extents(bg)) {
			btrfs_warn(fs_info,
			   "block group for swapfile at %llu is read-only%s",
			   bg->start,
			   atomic_read(&fs_info->scrubs_running) ?
				       " (scrub running)" : "");
			btrfs_put_block_group(bg);
			ret = -EINVAL;
			goto out;
		}

		ret = btrfs_add_swapfile_pin(inode, bg, true);
		if (ret) {
			btrfs_put_block_group(bg);
			if (ret == 1)
				ret = 0;
			else
				goto out;
		}

		if (bsi.block_len &&
		    bsi.block_start + bsi.block_len == physical_block_start) {
			bsi.block_len += len;
		} else {
			if (bsi.block_len) {
				ret = btrfs_add_swap_extent(sis, &bsi);
				if (ret)
					goto out;
			}
			bsi.start = start;
			bsi.block_start = physical_block_start;
			bsi.block_len = len;
		}

		start += len;
	}

	if (bsi.block_len)
		ret = btrfs_add_swap_extent(sis, &bsi);

out:
	if (!IS_ERR_OR_NULL(em))
		free_extent_map(em);

	unlock_extent_cached(io_tree, 0, isize - 1, &cached_state);

	if (ret)
		btrfs_swap_deactivate(file);

	btrfs_drew_write_unlock(&root->snapshot_lock);

	btrfs_exclop_finish(fs_info);

	if (ret)
		return ret;

	if (device)
		sis->bdev = device->bdev;
	*span = bsi.highest_ppage - bsi.lowest_ppage + 1;
	sis->max = bsi.nr_pages;
	sis->pages = bsi.nr_pages - 1;
	sis->highest_bit = bsi.nr_pages - 1;
	return bsi.nr_extents;
}
#else
static void btrfs_swap_deactivate(struct file *file)
{
}

static int btrfs_swap_activate(struct swap_info_struct *sis, struct file *file,
			       sector_t *span)
{
	return -EOPNOTSUPP;
}
#endif

/*
 * Update the number of bytes used in the VFS' inode. When we replace extents in
 * a range (clone, dedupe, fallocate's zero range), we must update the number of
 * bytes used by the inode in an atomic manner, so that concurrent stat(2) calls
 * always get a correct value.
 */
void btrfs_update_inode_bytes(struct btrfs_inode *inode,
			      const u64 add_bytes,
			      const u64 del_bytes)
{
	if (add_bytes == del_bytes)
		return;

	spin_lock(&inode->lock);
	if (del_bytes > 0)
		inode_sub_bytes(&inode->vfs_inode, del_bytes);
	if (add_bytes > 0)
		inode_add_bytes(&inode->vfs_inode, add_bytes);
	spin_unlock(&inode->lock);
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
	.fileattr_get	= btrfs_fileattr_get,
	.fileattr_set	= btrfs_fileattr_set,
};

static const struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= btrfs_real_readdir,
	.open		= btrfs_opendir,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_compat_ioctl,
#endif
	.release        = btrfs_release_file,
	.fsync		= btrfs_sync_file,
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
	.readahead	= btrfs_readahead,
	.direct_IO	= noop_direct_IO,
	.invalidatepage = btrfs_invalidatepage,
	.releasepage	= btrfs_releasepage,
#ifdef CONFIG_MIGRATION
	.migratepage	= btrfs_migratepage,
#endif
	.set_page_dirty	= btrfs_set_page_dirty,
	.error_remove_page = generic_error_remove_page,
	.swap_activate	= btrfs_swap_activate,
	.swap_deactivate = btrfs_swap_deactivate,
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
	.fileattr_get	= btrfs_fileattr_get,
	.fileattr_set	= btrfs_fileattr_set,
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
};
