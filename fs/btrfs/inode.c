// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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
#include <linux/sched/mm.h>
#include <asm/unaligned.h>
#include "misc.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_iyesde.h"
#include "print-tree.h"
#include "ordered-data.h"
#include "xattr.h"
#include "tree-log.h"
#include "volumes.h"
#include "compression.h"
#include "locking.h"
#include "free-space-cache.h"
#include "iyesde-map.h"
#include "backref.h"
#include "props.h"
#include "qgroup.h"
#include "delalloc-space.h"
#include "block-group.h"

struct btrfs_iget_args {
	struct btrfs_key *location;
	struct btrfs_root *root;
};

struct btrfs_dio_data {
	u64 reserve;
	u64 unsubmitted_oe_range_start;
	u64 unsubmitted_oe_range_end;
	int overwrite;
};

static const struct iyesde_operations btrfs_dir_iyesde_operations;
static const struct iyesde_operations btrfs_symlink_iyesde_operations;
static const struct iyesde_operations btrfs_dir_ro_iyesde_operations;
static const struct iyesde_operations btrfs_special_iyesde_operations;
static const struct iyesde_operations btrfs_file_iyesde_operations;
static const struct address_space_operations btrfs_aops;
static const struct file_operations btrfs_dir_file_operations;
static const struct extent_io_ops btrfs_extent_io_ops;

static struct kmem_cache *btrfs_iyesde_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_path_cachep;
struct kmem_cache *btrfs_free_space_cachep;
struct kmem_cache *btrfs_free_space_bitmap_cachep;

static int btrfs_setsize(struct iyesde *iyesde, struct iattr *attr);
static int btrfs_truncate(struct iyesde *iyesde, bool skip_writeback);
static int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered_extent);
static yesinline int cow_file_range(struct iyesde *iyesde,
				   struct page *locked_page,
				   u64 start, u64 end, int *page_started,
				   unsigned long *nr_written, int unlock);
static struct extent_map *create_io_em(struct iyesde *iyesde, u64 start, u64 len,
				       u64 orig_start, u64 block_start,
				       u64 block_len, u64 orig_block_len,
				       u64 ram_bytes, int compress_type,
				       int type);

static void __endio_write_update_ordered(struct iyesde *iyesde,
					 const u64 offset, const u64 bytes,
					 const bool uptodate);

/*
 * Cleanup all submitted ordered extents in specified range to handle errors
 * from the btrfs_run_delalloc_range() callback.
 *
 * NOTE: caller must ensure that when an error happens, it can yest call
 * extent_clear_unlock_delalloc() to clear both the bits EXTENT_DO_ACCOUNTING
 * and EXTENT_DELALLOC simultaneously, because that causes the reserved metadata
 * to be released, which we want to happen only when finishing the ordered
 * extent (btrfs_finish_ordered_io()).
 */
static inline void btrfs_cleanup_ordered_extents(struct iyesde *iyesde,
						 struct page *locked_page,
						 u64 offset, u64 bytes)
{
	unsigned long index = offset >> PAGE_SHIFT;
	unsigned long end_index = (offset + bytes - 1) >> PAGE_SHIFT;
	u64 page_start = page_offset(locked_page);
	u64 page_end = page_start + PAGE_SIZE - 1;

	struct page *page;

	while (index <= end_index) {
		page = find_get_page(iyesde->i_mapping, index);
		index++;
		if (!page)
			continue;
		ClearPagePrivate2(page);
		put_page(page);
	}

	/*
	 * In case this page belongs to the delalloc range being instantiated
	 * then skip it, since the first page of a range is going to be
	 * properly cleaned up by the caller of run_delalloc_range
	 */
	if (page_start >= offset && page_end <= (offset + bytes - 1)) {
		offset += PAGE_SIZE;
		bytes -= PAGE_SIZE;
	}

	return __endio_write_update_ordered(iyesde, offset, bytes, false);
}

static int btrfs_dirty_iyesde(struct iyesde *iyesde);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_iyesde_set_ops(struct iyesde *iyesde)
{
	BTRFS_I(iyesde)->io_tree.ops = &btrfs_extent_io_ops;
}
#endif

static int btrfs_init_iyesde_security(struct btrfs_trans_handle *trans,
				     struct iyesde *iyesde,  struct iyesde *dir,
				     const struct qstr *qstr)
{
	int err;

	err = btrfs_init_acl(trans, iyesde, dir);
	if (!err)
		err = btrfs_xattr_security_init(trans, iyesde, dir, qstr);
	return err;
}

/*
 * this does all the hard work for inserting an inline extent into
 * the btree.  The caller should have done a btrfs_drop_extents so that
 * yes overlapping inline items exist in the btree
 */
static int insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_path *path, int extent_inserted,
				struct btrfs_root *root, struct iyesde *iyesde,
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

	iyesde_add_bytes(iyesde, size);

	if (!extent_inserted) {
		struct btrfs_key key;
		size_t datasize;

		key.objectid = btrfs_iyes(BTRFS_I(iyesde));
		key.offset = start;
		key.type = BTRFS_EXTENT_DATA_KEY;

		datasize = btrfs_file_extent_calc_inline_size(cur_size);
		path->leave_spinning = 1;
		ret = btrfs_insert_empty_item(trans, root, path, &key,
					      datasize);
		if (ret)
			goto fail;
	}
	leaf = path->yesdes[0];
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
		page = find_get_page(iyesde->i_mapping,
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
	 * we're an inline extent, so yesbody can
	 * extend the file past i_size without locking
	 * a page we already have locked.
	 *
	 * We must do any isize and iyesde updates
	 * before we unlock the pages.  Otherwise we
	 * could end up racing with unlink.
	 */
	BTRFS_I(iyesde)->disk_i_size = iyesde->i_size;
	ret = btrfs_update_iyesde(trans, root, iyesde);

fail:
	return ret;
}


/*
 * conditionally insert an inline extent into the file.  This
 * does the checks required to make sure the data is small eyesugh
 * to fit as an inline extent.
 */
static yesinline int cow_file_range_inline(struct iyesde *iyesde, u64 start,
					  u64 end, size_t compressed_size,
					  int compress_type,
					  struct page **compressed_pages)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	u64 isize = i_size_read(iyesde);
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
	trans->block_rsv = &BTRFS_I(iyesde)->block_rsv;

	if (compressed_size && compressed_pages)
		extent_item_size = btrfs_file_extent_calc_inline_size(
		   compressed_size);
	else
		extent_item_size = btrfs_file_extent_calc_inline_size(
		    inline_len);

	ret = __btrfs_drop_extents(trans, root, iyesde, path,
				   start, aligned_end, NULL,
				   1, 1, extent_item_size, &extent_inserted);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	if (isize > actual_end)
		inline_len = min_t(u64, isize, actual_end);
	ret = insert_inline_extent(trans, path, extent_inserted,
				   root, iyesde, start,
				   inline_len, compressed_size,
				   compress_type, compressed_pages);
	if (ret && ret != -ENOSPC) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else if (ret == -ENOSPC) {
		ret = 1;
		goto out;
	}

	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(iyesde)->runtime_flags);
	btrfs_drop_extent_cache(BTRFS_I(iyesde), start, aligned_end - 1, 0);
out:
	/*
	 * Don't forget to free the reserved space, as for inlined extent
	 * it won't count as data extent, free them directly here.
	 * And at reserve time, it's always aligned to page size, so
	 * just free one page here.
	 */
	btrfs_qgroup_free_data(iyesde, NULL, 0, PAGE_SIZE);
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
	struct iyesde *iyesde;
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

static yesinline int add_async_extent(struct async_chunk *cow,
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
 * Check if the iyesde has flags compatible with compression
 */
static inline bool iyesde_can_compress(struct iyesde *iyesde)
{
	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATACOW ||
	    BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATASUM)
		return false;
	return true;
}

/*
 * Check if the iyesde needs to be submitted to compression, based on mount
 * options, defragmentation, properties or heuristics.
 */
static inline int iyesde_need_compress(struct iyesde *iyesde, u64 start, u64 end)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);

	if (!iyesde_can_compress(iyesde)) {
		WARN(IS_ENABLED(CONFIG_BTRFS_DEBUG),
			KERN_ERR "BTRFS: unexpected compression for iyes %llu\n",
			btrfs_iyes(BTRFS_I(iyesde)));
		return 0;
	}
	/* force compress */
	if (btrfs_test_opt(fs_info, FORCE_COMPRESS))
		return 1;
	/* defrag ioctl */
	if (BTRFS_I(iyesde)->defrag_compress)
		return 1;
	/* bad compression ratios */
	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_NOCOMPRESS)
		return 0;
	if (btrfs_test_opt(fs_info, COMPRESS) ||
	    BTRFS_I(iyesde)->flags & BTRFS_INODE_COMPRESS ||
	    BTRFS_I(iyesde)->prop_compress)
		return btrfs_compress_heuristic(iyesde, start, end);
	return 0;
}

static inline void iyesde_should_defrag(struct btrfs_iyesde *iyesde,
		u64 start, u64 end, u64 num_bytes, u64 small_write)
{
	/* If this is a small write inside eof, kick off a defrag */
	if (num_bytes < small_write &&
	    (start > 0 || end + 1 < iyesde->disk_i_size))
		btrfs_add_iyesde_defrag(NULL, iyesde);
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
 * makes sure that both compressed iyesdes and uncompressed iyesdes
 * are written in the same order that the flusher thread sent them
 * down.
 */
static yesinline int compress_file_range(struct async_chunk *async_chunk)
{
	struct iyesde *iyesde = async_chunk->iyesde;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
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

	iyesde_should_defrag(BTRFS_I(iyesde), start, end, end - start + 1,
			SZ_16K);

	/*
	 * We need to save i_size before yesw because it could change in between
	 * us evaluating the size and assigning it.  This is because we lock and
	 * unlock the page in truncate and fallocate, and then modify the i_size
	 * later on.
	 *
	 * The barriers are to emulate READ_ONCE, remove that once i_size_read
	 * does that for us.
	 */
	barrier();
	i_size = i_size_read(iyesde);
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
	   (start > 0 || end + 1 < BTRFS_I(iyesde)->disk_i_size))
		goto cleanup_and_bail_uncompressed;

	total_compressed = min_t(unsigned long, total_compressed,
			BTRFS_MAX_UNCOMPRESSED);
	total_in = 0;
	ret = 0;

	/*
	 * we do compression for mount -o compress and when the
	 * iyesde has yest been flagged as yescompress.  This flag can
	 * change at any time if we discover bad compression ratios.
	 */
	if (iyesde_need_compress(iyesde, start, end)) {
		WARN_ON(pages);
		pages = kcalloc(nr_pages, sizeof(struct page *), GFP_NOFS);
		if (!pages) {
			/* just bail out to the uncompressed code */
			nr_pages = 0;
			goto cont;
		}

		if (BTRFS_I(iyesde)->defrag_compress)
			compress_type = BTRFS_I(iyesde)->defrag_compress;
		else if (BTRFS_I(iyesde)->prop_compress)
			compress_type = BTRFS_I(iyesde)->prop_compress;

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
			extent_range_clear_dirty_for_io(iyesde, start, end);
			redirty = 1;
		}

		/* Compression level is applied here and only here */
		ret = btrfs_compress_pages(
			compress_type | (fs_info->compress_level << 4),
					   iyesde->i_mapping, start,
					   pages,
					   &nr_pages,
					   &total_in,
					   &total_compressed);

		if (!ret) {
			unsigned long offset = offset_in_page(total_compressed);
			struct page *page = pages[nr_pages - 1];
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
		if (ret || total_in < actual_end) {
			/* we didn't compress the entire range, try
			 * to make an uncompressed inline extent.
			 */
			ret = cow_file_range_inline(iyesde, start, end, 0,
						    BTRFS_COMPRESS_NONE, NULL);
		} else {
			/* try making a compressed inline extent */
			ret = cow_file_range_inline(iyesde, start, end,
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
			extent_clear_unlock_delalloc(iyesde, start, end, NULL,
						     clear_flags,
						     PAGE_UNLOCK |
						     PAGE_CLEAR_DIRTY |
						     PAGE_SET_WRITEBACK |
						     page_error_op |
						     PAGE_END_WRITEBACK);

			for (i = 0; i < nr_pages; i++) {
				WARN_ON(pages[i]->mapping);
				put_page(pages[i]);
			}
			kfree(pages);

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
		    !(BTRFS_I(iyesde)->prop_compress)) {
			BTRFS_I(iyesde)->flags |= BTRFS_INODE_NOCOMPRESS;
		}
	}
cleanup_and_bail_uncompressed:
	/*
	 * No compression, but we still need to write the pages in the file
	 * we've been given so far.  redirty the locked page if it corresponds
	 * to our extent and set things up for the async work queue to run
	 * cow_file_range to do the yesrmal delalloc dance.
	 */
	if (async_chunk->locked_page &&
	    (page_offset(async_chunk->locked_page) >= start &&
	     page_offset(async_chunk->locked_page)) <= end) {
		__set_page_dirty_yesbuffers(async_chunk->locked_page);
		/* unlocked later on in the async handlers */
	}

	if (redirty)
		extent_range_redirty_for_io(iyesde, start, end);
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
static yesinline void submit_compressed_extents(struct async_chunk *async_chunk)
{
	struct iyesde *iyesde = async_chunk->iyesde;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct async_extent *async_extent;
	u64 alloc_hint = 0;
	struct btrfs_key ins;
	struct extent_map *em;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
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
			ret = cow_file_range(iyesde, async_chunk->locked_page,
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
				extent_write_locked_range(iyesde,
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
				 * will yest submit these pages down to lower
				 * layers.
				 */
				extent_range_redirty_for_io(iyesde,
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
		em = create_io_em(iyesde, async_extent->start,
				  async_extent->ram_size, /* len */
				  async_extent->start, /* orig_start */
				  ins.objectid, /* block_start */
				  ins.offset, /* block_len */
				  ins.offset, /* orig_block_len */
				  async_extent->ram_size, /* ram_bytes */
				  async_extent->compress_type,
				  BTRFS_ORDERED_COMPRESSED);
		if (IS_ERR(em))
			/* ret value is yest necessary due to void function */
			goto out_free_reserve;
		free_extent_map(em);

		ret = btrfs_add_ordered_extent_compress(iyesde,
						async_extent->start,
						ins.objectid,
						async_extent->ram_size,
						ins.offset,
						BTRFS_ORDERED_COMPRESSED,
						async_extent->compress_type);
		if (ret) {
			btrfs_drop_extent_cache(BTRFS_I(iyesde),
						async_extent->start,
						async_extent->start +
						async_extent->ram_size - 1, 0);
			goto out_free_reserve;
		}
		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		/*
		 * clear dirty, set writeback and unlock the pages.
		 */
		extent_clear_unlock_delalloc(iyesde, async_extent->start,
				async_extent->start +
				async_extent->ram_size - 1,
				NULL, EXTENT_LOCKED | EXTENT_DELALLOC,
				PAGE_UNLOCK | PAGE_CLEAR_DIRTY |
				PAGE_SET_WRITEBACK);
		if (btrfs_submit_compressed_write(iyesde,
				    async_extent->start,
				    async_extent->ram_size,
				    ins.objectid,
				    ins.offset, async_extent->pages,
				    async_extent->nr_pages,
				    async_chunk->write_flags,
				    async_chunk->blkcg_css)) {
			struct page *p = async_extent->pages[0];
			const u64 start = async_extent->start;
			const u64 end = start + async_extent->ram_size - 1;

			p->mapping = iyesde->i_mapping;
			btrfs_writepage_endio_finish_ordered(p, start, end, 0);

			p->mapping = NULL;
			extent_clear_unlock_delalloc(iyesde, start, end,
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
	extent_clear_unlock_delalloc(iyesde, async_extent->start,
				     async_extent->start +
				     async_extent->ram_size - 1,
				     NULL, EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DELALLOC_NEW |
				     EXTENT_DEFRAG | EXTENT_DO_ACCOUNTING,
				     PAGE_UNLOCK | PAGE_CLEAR_DIRTY |
				     PAGE_SET_WRITEBACK | PAGE_END_WRITEBACK |
				     PAGE_SET_ERROR);
	free_async_extent_pages(async_extent);
	kfree(async_extent);
	goto again;
}

static u64 get_extent_allocation_hint(struct iyesde *iyesde, u64 start,
				      u64 num_bytes)
{
	struct extent_map_tree *em_tree = &BTRFS_I(iyesde)->extent_tree;
	struct extent_map *em;
	u64 alloc_hint = 0;

	read_lock(&em_tree->lock);
	em = search_extent_mapping(em_tree, start, num_bytes);
	if (em) {
		/*
		 * if block start isn't an actual block number then find the
		 * first block in this iyesde and use that as a hint.  If that
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
static yesinline int cow_file_range(struct iyesde *iyesde,
				   struct page *locked_page,
				   u64 start, u64 end, int *page_started,
				   unsigned long *nr_written, int unlock)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	u64 alloc_hint = 0;
	u64 num_bytes;
	unsigned long ram_size;
	u64 cur_alloc_size = 0;
	u64 blocksize = fs_info->sectorsize;
	struct btrfs_key ins;
	struct extent_map *em;
	unsigned clear_bits;
	unsigned long page_ops;
	bool extent_reserved = false;
	int ret = 0;

	if (btrfs_is_free_space_iyesde(BTRFS_I(iyesde))) {
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		goto out_unlock;
	}

	num_bytes = ALIGN(end - start + 1, blocksize);
	num_bytes = max(blocksize,  num_bytes);
	ASSERT(num_bytes <= btrfs_super_total_bytes(fs_info->super_copy));

	iyesde_should_defrag(BTRFS_I(iyesde), start, end, num_bytes, SZ_64K);

	if (start == 0) {
		/* lets try to make an inline extent */
		ret = cow_file_range_inline(iyesde, start, end, 0,
					    BTRFS_COMPRESS_NONE, NULL);
		if (ret == 0) {
			/*
			 * We use DO_ACCOUNTING here because we need the
			 * delalloc_release_metadata to be run _after_ we drop
			 * our outstanding extent for clearing delalloc for this
			 * range.
			 */
			extent_clear_unlock_delalloc(iyesde, start, end, NULL,
				     EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DELALLOC_NEW | EXTENT_DEFRAG |
				     EXTENT_DO_ACCOUNTING, PAGE_UNLOCK |
				     PAGE_CLEAR_DIRTY | PAGE_SET_WRITEBACK |
				     PAGE_END_WRITEBACK);
			*nr_written = *nr_written +
			     (end - start + PAGE_SIZE) / PAGE_SIZE;
			*page_started = 1;
			goto out;
		} else if (ret < 0) {
			goto out_unlock;
		}
	}

	alloc_hint = get_extent_allocation_hint(iyesde, start, num_bytes);
	btrfs_drop_extent_cache(BTRFS_I(iyesde), start,
			start + num_bytes - 1, 0);

	while (num_bytes > 0) {
		cur_alloc_size = num_bytes;
		ret = btrfs_reserve_extent(root, cur_alloc_size, cur_alloc_size,
					   fs_info->sectorsize, 0, alloc_hint,
					   &ins, 1, 1);
		if (ret < 0)
			goto out_unlock;
		cur_alloc_size = ins.offset;
		extent_reserved = true;

		ram_size = ins.offset;
		em = create_io_em(iyesde, start, ins.offset, /* len */
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

		ret = btrfs_add_ordered_extent(iyesde, start, ins.objectid,
					       ram_size, cur_alloc_size, 0);
		if (ret)
			goto out_drop_extent_cache;

		if (root->root_key.objectid ==
		    BTRFS_DATA_RELOC_TREE_OBJECTID) {
			ret = btrfs_reloc_clone_csums(iyesde, start,
						      cur_alloc_size);
			/*
			 * Only drop cache here, and process as yesrmal.
			 *
			 * We must yest allow extent_clear_unlock_delalloc()
			 * at out_unlock label to free meta of this ordered
			 * extent, as its meta should be freed by
			 * btrfs_finish_ordered_io().
			 *
			 * So we must continue until @start is increased to
			 * skip current ordered extent.
			 */
			if (ret)
				btrfs_drop_extent_cache(BTRFS_I(iyesde), start,
						start + ram_size - 1, 0);
		}

		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		/* we're yest doing compressed IO, don't unlock the first
		 * page (which the caller expects to stay locked), don't
		 * clear any dirty bits and don't set any writeback bits
		 *
		 * Do set the Private2 bit so we kyesw this page was properly
		 * setup for writepage
		 */
		page_ops = unlock ? PAGE_UNLOCK : 0;
		page_ops |= PAGE_SET_PRIVATE2;

		extent_clear_unlock_delalloc(iyesde, start,
					     start + ram_size - 1,
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
	btrfs_drop_extent_cache(BTRFS_I(iyesde), start, start + ram_size - 1, 0);
out_reserve:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
out_unlock:
	clear_bits = EXTENT_LOCKED | EXTENT_DELALLOC | EXTENT_DELALLOC_NEW |
		EXTENT_DEFRAG | EXTENT_CLEAR_META_RESV;
	page_ops = PAGE_UNLOCK | PAGE_CLEAR_DIRTY | PAGE_SET_WRITEBACK |
		PAGE_END_WRITEBACK;
	/*
	 * If we reserved an extent for our delalloc range (or a subrange) and
	 * failed to create the respective ordered extent, then it means that
	 * when we reserved the extent we decremented the extent's size from
	 * the data space_info's bytes_may_use counter and incremented the
	 * space_info's bytes_reserved counter by the same amount. We must make
	 * sure extent_clear_unlock_delalloc() does yest try to decrement again
	 * the data space_info's bytes_may_use counter, therefore we do yest pass
	 * it the flag EXTENT_CLEAR_DATA_RESV.
	 */
	if (extent_reserved) {
		extent_clear_unlock_delalloc(iyesde, start,
					     start + cur_alloc_size,
					     locked_page,
					     clear_bits,
					     page_ops);
		start += cur_alloc_size;
		if (start >= end)
			goto out;
	}
	extent_clear_unlock_delalloc(iyesde, start, end, locked_page,
				     clear_bits | EXTENT_CLEAR_DATA_RESV,
				     page_ops);
	goto out;
}

/*
 * work queue call back to started compression on a file and pages
 */
static yesinline void async_cow_start(struct btrfs_work *work)
{
	struct async_chunk *async_chunk;
	int compressed_extents;

	async_chunk = container_of(work, struct async_chunk, work);

	compressed_extents = compress_file_range(async_chunk);
	if (compressed_extents == 0) {
		btrfs_add_delayed_iput(async_chunk->iyesde);
		async_chunk->iyesde = NULL;
	}
}

/*
 * work queue call back to submit previously compressed pages
 */
static yesinline void async_cow_submit(struct btrfs_work *work)
{
	struct async_chunk *async_chunk = container_of(work, struct async_chunk,
						     work);
	struct btrfs_fs_info *fs_info = btrfs_work_owner(work);
	unsigned long nr_pages;

	nr_pages = (async_chunk->end - async_chunk->start + PAGE_SIZE) >>
		PAGE_SHIFT;

	/* atomic_sub_return implies a barrier */
	if (atomic_sub_return(nr_pages, &fs_info->async_delalloc_pages) <
	    5 * SZ_1M)
		cond_wake_up_yesmb(&fs_info->async_submit_wait);

	/*
	 * ->iyesde could be NULL if async_chunk_start has failed to compress,
	 * in which case we don't have anything to submit, yet we need to
	 * always adjust ->async_delalloc_pages as its paired with the init
	 * happening in cow_file_range_async
	 */
	if (async_chunk->iyesde)
		submit_compressed_extents(async_chunk);
}

static yesinline void async_cow_free(struct btrfs_work *work)
{
	struct async_chunk *async_chunk;

	async_chunk = container_of(work, struct async_chunk, work);
	if (async_chunk->iyesde)
		btrfs_add_delayed_iput(async_chunk->iyesde);
	if (async_chunk->blkcg_css)
		css_put(async_chunk->blkcg_css);
	/*
	 * Since the pointer to 'pending' is at the beginning of the array of
	 * async_chunk's, freeing it ensures the whole array has been freed.
	 */
	if (atomic_dec_and_test(async_chunk->pending))
		kvfree(async_chunk->pending);
}

static int cow_file_range_async(struct iyesde *iyesde,
				struct writeback_control *wbc,
				struct page *locked_page,
				u64 start, u64 end, int *page_started,
				unsigned long *nr_written)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct cgroup_subsys_state *blkcg_css = wbc_blkcg_css(wbc);
	struct async_cow *ctx;
	struct async_chunk *async_chunk;
	unsigned long nr_pages;
	u64 cur_end;
	u64 num_chunks = DIV_ROUND_UP(end - start, SZ_512K);
	int i;
	bool should_compress;
	unsigned yesfs_flag;
	const unsigned int write_flags = wbc_to_write_flags(wbc);

	unlock_extent(&BTRFS_I(iyesde)->io_tree, start, end);

	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_NOCOMPRESS &&
	    !btrfs_test_opt(fs_info, FORCE_COMPRESS)) {
		num_chunks = 1;
		should_compress = false;
	} else {
		should_compress = true;
	}

	yesfs_flag = memalloc_yesfs_save();
	ctx = kvmalloc(struct_size(ctx, chunks, num_chunks), GFP_KERNEL);
	memalloc_yesfs_restore(yesfs_flag);

	if (!ctx) {
		unsigned clear_bits = EXTENT_LOCKED | EXTENT_DELALLOC |
			EXTENT_DELALLOC_NEW | EXTENT_DEFRAG |
			EXTENT_DO_ACCOUNTING;
		unsigned long page_ops = PAGE_UNLOCK | PAGE_CLEAR_DIRTY |
			PAGE_SET_WRITEBACK | PAGE_END_WRITEBACK |
			PAGE_SET_ERROR;

		extent_clear_unlock_delalloc(iyesde, start, end, locked_page,
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
		ihold(iyesde);
		async_chunk[i].pending = &ctx->num_chunks;
		async_chunk[i].iyesde = iyesde;
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
			 * might yest go through async.  We want all of them to
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

static yesinline int csum_exist_in_range(struct btrfs_fs_info *fs_info,
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

/*
 * when yeswcow writeback call back.  This checks for snapshots or COW copies
 * of the extents that exist in the file, and COWs the file as required.
 *
 * If yes cow copies or snapshots exist, we write directly to the existing
 * blocks on disk
 */
static yesinline int run_delalloc_yescow(struct iyesde *iyesde,
				       struct page *locked_page,
				       const u64 start, const u64 end,
				       int *page_started, int force,
				       unsigned long *nr_written)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_path *path;
	u64 cow_start = (u64)-1;
	u64 cur_offset = start;
	int ret;
	bool check_prev = true;
	const bool freespace_iyesde = btrfs_is_free_space_iyesde(BTRFS_I(iyesde));
	u64 iyes = btrfs_iyes(BTRFS_I(iyesde));
	bool yescow = false;
	u64 disk_bytenr = 0;

	path = btrfs_alloc_path();
	if (!path) {
		extent_clear_unlock_delalloc(iyesde, start, end, locked_page,
					     EXTENT_LOCKED | EXTENT_DELALLOC |
					     EXTENT_DO_ACCOUNTING |
					     EXTENT_DEFRAG, PAGE_UNLOCK |
					     PAGE_CLEAR_DIRTY |
					     PAGE_SET_WRITEBACK |
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

		yescow = false;

		ret = btrfs_lookup_file_extent(NULL, root, path, iyes,
					       cur_offset, 0);
		if (ret < 0)
			goto error;

		/*
		 * If there is yes extent for our range when doing the initial
		 * search, then go back to the previous slot as it will be the
		 * one containing the search offset
		 */
		if (ret > 0 && path->slots[0] > 0 && check_prev) {
			leaf = path->yesdes[0];
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0] - 1);
			if (found_key.objectid == iyes &&
			    found_key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}
		check_prev = false;
next_slot:
		/* Go to next leaf if we have exhausted the current one */
		leaf = path->yesdes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				if (cow_start != (u64)-1)
					cur_offset = cow_start;
				goto error;
			}
			if (ret > 0)
				break;
			leaf = path->yesdes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		/* Didn't find anything for our INO */
		if (found_key.objectid > iyes)
			break;
		/*
		 * Keep searching until we find an EXTENT_ITEM or there are yes
		 * more extents for this iyesde
		 */
		if (WARN_ON_ONCE(found_key.objectid < iyes) ||
		    found_key.type < BTRFS_EXTENT_DATA_KEY) {
			path->slots[0]++;
			goto next_slot;
		}

		/* Found key is yest EXTENT_DATA_KEY or starts after req range */
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
			 * yescow. This is the same check as in
			 * btrfs_cross_ref_exist but without calling
			 * btrfs_search_slot.
			 */
			if (!freespace_iyesde &&
			    btrfs_file_extent_generation(leaf, fi) <=
			    btrfs_root_last_snapshot(&root->root_item))
				goto out_check;
			if (extent_type == BTRFS_FILE_EXTENT_REG && !force)
				goto out_check;
			/* If extent is RO, we must COW it */
			if (btrfs_extent_readonly(fs_info, disk_bytenr))
				goto out_check;
			ret = btrfs_cross_ref_exist(root, iyes,
						    found_key.offset -
						    extent_offset, disk_bytenr);
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

				WARN_ON_ONCE(freespace_iyesde);
				goto out_check;
			}
			disk_bytenr += extent_offset;
			disk_bytenr += cur_offset - found_key.offset;
			num_bytes = min(end + 1, extent_end) - cur_offset;
			/*
			 * If there are pending snapshots for this root, we
			 * fall into common COW way
			 */
			if (!freespace_iyesde && atomic_read(&root->snapshot_force_cow))
				goto out_check;
			/*
			 * force cow if csum exists in the range.
			 * this ensure that csum for a given extent are
			 * either valid or do yest exist.
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
				WARN_ON_ONCE(freespace_iyesde);
				goto out_check;
			}
			if (!btrfs_inc_yescow_writers(fs_info, disk_bytenr))
				goto out_check;
			yescow = true;
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
		 * If yescow is false then record the beginning of the range
		 * that needs to be COWed
		 */
		if (!yescow) {
			if (cow_start == (u64)-1)
				cow_start = cur_offset;
			cur_offset = extent_end;
			if (cur_offset > end)
				break;
			path->slots[0]++;
			goto next_slot;
		}

		btrfs_release_path(path);

		/*
		 * COW range from cow_start to found_key.offset - 1. As the key
		 * will contain the beginning of the first extent that can be
		 * NOCOW, following one which needs to be COW'ed
		 */
		if (cow_start != (u64)-1) {
			ret = cow_file_range(iyesde, locked_page,
					     cow_start, found_key.offset - 1,
					     page_started, nr_written, 1);
			if (ret) {
				if (yescow)
					btrfs_dec_yescow_writers(fs_info,
								disk_bytenr);
				goto error;
			}
			cow_start = (u64)-1;
		}

		if (extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
			u64 orig_start = found_key.offset - extent_offset;
			struct extent_map *em;

			em = create_io_em(iyesde, cur_offset, num_bytes,
					  orig_start,
					  disk_bytenr, /* block_start */
					  num_bytes, /* block_len */
					  disk_num_bytes, /* orig_block_len */
					  ram_bytes, BTRFS_COMPRESS_NONE,
					  BTRFS_ORDERED_PREALLOC);
			if (IS_ERR(em)) {
				if (yescow)
					btrfs_dec_yescow_writers(fs_info,
								disk_bytenr);
				ret = PTR_ERR(em);
				goto error;
			}
			free_extent_map(em);
			ret = btrfs_add_ordered_extent(iyesde, cur_offset,
						       disk_bytenr, num_bytes,
						       num_bytes,
						       BTRFS_ORDERED_PREALLOC);
			if (ret) {
				btrfs_drop_extent_cache(BTRFS_I(iyesde),
							cur_offset,
							cur_offset + num_bytes - 1,
							0);
				goto error;
			}
		} else {
			ret = btrfs_add_ordered_extent(iyesde, cur_offset,
						       disk_bytenr, num_bytes,
						       num_bytes,
						       BTRFS_ORDERED_NOCOW);
			if (ret)
				goto error;
		}

		if (yescow)
			btrfs_dec_yescow_writers(fs_info, disk_bytenr);
		yescow = false;

		if (root->root_key.objectid ==
		    BTRFS_DATA_RELOC_TREE_OBJECTID)
			/*
			 * Error handled later, as we must prevent
			 * extent_clear_unlock_delalloc() in error handler
			 * from freeing metadata of created ordered extent.
			 */
			ret = btrfs_reloc_clone_csums(iyesde, cur_offset,
						      num_bytes);

		extent_clear_unlock_delalloc(iyesde, cur_offset,
					     cur_offset + num_bytes - 1,
					     locked_page, EXTENT_LOCKED |
					     EXTENT_DELALLOC |
					     EXTENT_CLEAR_DATA_RESV,
					     PAGE_UNLOCK | PAGE_SET_PRIVATE2);

		cur_offset = extent_end;

		/*
		 * btrfs_reloc_clone_csums() error, yesw we're OK to call error
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
		ret = cow_file_range(iyesde, locked_page, cow_start, end,
				     page_started, nr_written, 1);
		if (ret)
			goto error;
	}

error:
	if (yescow)
		btrfs_dec_yescow_writers(fs_info, disk_bytenr);

	if (ret && cur_offset < end)
		extent_clear_unlock_delalloc(iyesde, cur_offset, end,
					     locked_page, EXTENT_LOCKED |
					     EXTENT_DELALLOC | EXTENT_DEFRAG |
					     EXTENT_DO_ACCOUNTING, PAGE_UNLOCK |
					     PAGE_CLEAR_DIRTY |
					     PAGE_SET_WRITEBACK |
					     PAGE_END_WRITEBACK);
	btrfs_free_path(path);
	return ret;
}

static inline int need_force_cow(struct iyesde *iyesde, u64 start, u64 end)
{

	if (!(BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATACOW) &&
	    !(BTRFS_I(iyesde)->flags & BTRFS_INODE_PREALLOC))
		return 0;

	/*
	 * @defrag_bytes is a hint value, yes spinlock held here,
	 * if is yest zero, it means the file is defragging.
	 * Force cow if given extent needs to be defragged.
	 */
	if (BTRFS_I(iyesde)->defrag_bytes &&
	    test_range_bit(&BTRFS_I(iyesde)->io_tree, start, end,
			   EXTENT_DEFRAG, 0, NULL))
		return 1;

	return 0;
}

/*
 * Function to process delayed allocation (create CoW) for ranges which are
 * being touched for the first time.
 */
int btrfs_run_delalloc_range(struct iyesde *iyesde, struct page *locked_page,
		u64 start, u64 end, int *page_started, unsigned long *nr_written,
		struct writeback_control *wbc)
{
	int ret;
	int force_cow = need_force_cow(iyesde, start, end);

	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATACOW && !force_cow) {
		ret = run_delalloc_yescow(iyesde, locked_page, start, end,
					 page_started, 1, nr_written);
	} else if (BTRFS_I(iyesde)->flags & BTRFS_INODE_PREALLOC && !force_cow) {
		ret = run_delalloc_yescow(iyesde, locked_page, start, end,
					 page_started, 0, nr_written);
	} else if (!iyesde_can_compress(iyesde) ||
		   !iyesde_need_compress(iyesde, start, end)) {
		ret = cow_file_range(iyesde, locked_page, start, end,
				      page_started, nr_written, 1);
	} else {
		set_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
			&BTRFS_I(iyesde)->runtime_flags);
		ret = cow_file_range_async(iyesde, wbc, locked_page, start, end,
					   page_started, nr_written);
	}
	if (ret)
		btrfs_cleanup_ordered_extents(iyesde, locked_page, start,
					      end - start + 1);
	return ret;
}

void btrfs_split_delalloc_extent(struct iyesde *iyesde,
				 struct extent_state *orig, u64 split)
{
	u64 size;

	/* yest delalloc, igyesre it */
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

	spin_lock(&BTRFS_I(iyesde)->lock);
	btrfs_mod_outstanding_extents(BTRFS_I(iyesde), 1);
	spin_unlock(&BTRFS_I(iyesde)->lock);
}

/*
 * Handle merged delayed allocation extents so we can keep track of new extents
 * that are just merged onto old extents, such as when we are doing sequential
 * writes, so we can properly account for the metadata space we'll need.
 */
void btrfs_merge_delalloc_extent(struct iyesde *iyesde, struct extent_state *new,
				 struct extent_state *other)
{
	u64 new_size, old_size;
	u32 num_extents;

	/* yest delalloc, igyesre it */
	if (!(other->state & EXTENT_DELALLOC))
		return;

	if (new->start > other->start)
		new_size = new->end - other->start + 1;
	else
		new_size = other->end - new->start + 1;

	/* we're yest bigger than the max, unreserve the space and go */
	if (new_size <= BTRFS_MAX_EXTENT_SIZE) {
		spin_lock(&BTRFS_I(iyesde)->lock);
		btrfs_mod_outstanding_extents(BTRFS_I(iyesde), -1);
		spin_unlock(&BTRFS_I(iyesde)->lock);
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

	spin_lock(&BTRFS_I(iyesde)->lock);
	btrfs_mod_outstanding_extents(BTRFS_I(iyesde), -1);
	spin_unlock(&BTRFS_I(iyesde)->lock);
}

static void btrfs_add_delalloc_iyesdes(struct btrfs_root *root,
				      struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);

	spin_lock(&root->delalloc_lock);
	if (list_empty(&BTRFS_I(iyesde)->delalloc_iyesdes)) {
		list_add_tail(&BTRFS_I(iyesde)->delalloc_iyesdes,
			      &root->delalloc_iyesdes);
		set_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			&BTRFS_I(iyesde)->runtime_flags);
		root->nr_delalloc_iyesdes++;
		if (root->nr_delalloc_iyesdes == 1) {
			spin_lock(&fs_info->delalloc_root_lock);
			BUG_ON(!list_empty(&root->delalloc_root));
			list_add_tail(&root->delalloc_root,
				      &fs_info->delalloc_roots);
			spin_unlock(&fs_info->delalloc_root_lock);
		}
	}
	spin_unlock(&root->delalloc_lock);
}


void __btrfs_del_delalloc_iyesde(struct btrfs_root *root,
				struct btrfs_iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (!list_empty(&iyesde->delalloc_iyesdes)) {
		list_del_init(&iyesde->delalloc_iyesdes);
		clear_bit(BTRFS_INODE_IN_DELALLOC_LIST,
			  &iyesde->runtime_flags);
		root->nr_delalloc_iyesdes--;
		if (!root->nr_delalloc_iyesdes) {
			ASSERT(list_empty(&root->delalloc_iyesdes));
			spin_lock(&fs_info->delalloc_root_lock);
			BUG_ON(list_empty(&root->delalloc_root));
			list_del_init(&root->delalloc_root);
			spin_unlock(&fs_info->delalloc_root_lock);
		}
	}
}

static void btrfs_del_delalloc_iyesde(struct btrfs_root *root,
				     struct btrfs_iyesde *iyesde)
{
	spin_lock(&root->delalloc_lock);
	__btrfs_del_delalloc_iyesde(root, iyesde);
	spin_unlock(&root->delalloc_lock);
}

/*
 * Properly track delayed allocation bytes in the iyesde and to maintain the
 * list of iyesdes that have pending delalloc work to be done.
 */
void btrfs_set_delalloc_extent(struct iyesde *iyesde, struct extent_state *state,
			       unsigned *bits)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);

	if ((*bits & EXTENT_DEFRAG) && !(*bits & EXTENT_DELALLOC))
		WARN_ON(1);
	/*
	 * set_bit and clear bit hooks yesrmally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if (!(state->state & EXTENT_DELALLOC) && (*bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = BTRFS_I(iyesde)->root;
		u64 len = state->end + 1 - state->start;
		u32 num_extents = count_max_extents(len);
		bool do_list = !btrfs_is_free_space_iyesde(BTRFS_I(iyesde));

		spin_lock(&BTRFS_I(iyesde)->lock);
		btrfs_mod_outstanding_extents(BTRFS_I(iyesde), num_extents);
		spin_unlock(&BTRFS_I(iyesde)->lock);

		/* For sanity tests */
		if (btrfs_is_testing(fs_info))
			return;

		percpu_counter_add_batch(&fs_info->delalloc_bytes, len,
					 fs_info->delalloc_batch);
		spin_lock(&BTRFS_I(iyesde)->lock);
		BTRFS_I(iyesde)->delalloc_bytes += len;
		if (*bits & EXTENT_DEFRAG)
			BTRFS_I(iyesde)->defrag_bytes += len;
		if (do_list && !test_bit(BTRFS_INODE_IN_DELALLOC_LIST,
					 &BTRFS_I(iyesde)->runtime_flags))
			btrfs_add_delalloc_iyesdes(root, iyesde);
		spin_unlock(&BTRFS_I(iyesde)->lock);
	}

	if (!(state->state & EXTENT_DELALLOC_NEW) &&
	    (*bits & EXTENT_DELALLOC_NEW)) {
		spin_lock(&BTRFS_I(iyesde)->lock);
		BTRFS_I(iyesde)->new_delalloc_bytes += state->end + 1 -
			state->start;
		spin_unlock(&BTRFS_I(iyesde)->lock);
	}
}

/*
 * Once a range is yes longer delalloc this function ensures that proper
 * accounting happens.
 */
void btrfs_clear_delalloc_extent(struct iyesde *vfs_iyesde,
				 struct extent_state *state, unsigned *bits)
{
	struct btrfs_iyesde *iyesde = BTRFS_I(vfs_iyesde);
	struct btrfs_fs_info *fs_info = btrfs_sb(vfs_iyesde->i_sb);
	u64 len = state->end + 1 - state->start;
	u32 num_extents = count_max_extents(len);

	if ((state->state & EXTENT_DEFRAG) && (*bits & EXTENT_DEFRAG)) {
		spin_lock(&iyesde->lock);
		iyesde->defrag_bytes -= len;
		spin_unlock(&iyesde->lock);
	}

	/*
	 * set_bit and clear bit hooks yesrmally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if ((state->state & EXTENT_DELALLOC) && (*bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = iyesde->root;
		bool do_list = !btrfs_is_free_space_iyesde(iyesde);

		spin_lock(&iyesde->lock);
		btrfs_mod_outstanding_extents(iyesde, -num_extents);
		spin_unlock(&iyesde->lock);

		/*
		 * We don't reserve metadata space for space cache iyesdes so we
		 * don't need to call delalloc_release_metadata if there is an
		 * error.
		 */
		if (*bits & EXTENT_CLEAR_META_RESV &&
		    root != fs_info->tree_root)
			btrfs_delalloc_release_metadata(iyesde, len, false);

		/* For sanity tests. */
		if (btrfs_is_testing(fs_info))
			return;

		if (root->root_key.objectid != BTRFS_DATA_RELOC_TREE_OBJECTID &&
		    do_list && !(state->state & EXTENT_NORESERVE) &&
		    (*bits & EXTENT_CLEAR_DATA_RESV))
			btrfs_free_reserved_data_space_yesquota(
					&iyesde->vfs_iyesde,
					state->start, len);

		percpu_counter_add_batch(&fs_info->delalloc_bytes, -len,
					 fs_info->delalloc_batch);
		spin_lock(&iyesde->lock);
		iyesde->delalloc_bytes -= len;
		if (do_list && iyesde->delalloc_bytes == 0 &&
		    test_bit(BTRFS_INODE_IN_DELALLOC_LIST,
					&iyesde->runtime_flags))
			btrfs_del_delalloc_iyesde(root, iyesde);
		spin_unlock(&iyesde->lock);
	}

	if ((state->state & EXTENT_DELALLOC_NEW) &&
	    (*bits & EXTENT_DELALLOC_NEW)) {
		spin_lock(&iyesde->lock);
		ASSERT(iyesde->new_delalloc_bytes >= len);
		iyesde->new_delalloc_bytes -= len;
		spin_unlock(&iyesde->lock);
	}
}

/*
 * btrfs_bio_fits_in_stripe - Checks whether the size of the given bio will fit
 * in a chunk's stripe. This function ensures that bios do yest span a
 * stripe/chunk
 *
 * @page - The page we are about to add to the bio
 * @size - size we want to add to the bio
 * @bio - bio we want to ensure is smaller than a stripe
 * @bio_flags - flags of the bio
 *
 * return 1 if page canyest be added to the bio
 * return 0 if page can be added to the bio
 * return error otherwise
 */
int btrfs_bio_fits_in_stripe(struct page *page, size_t size, struct bio *bio,
			     unsigned long bio_flags)
{
	struct iyesde *iyesde = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	u64 logical = (u64)bio->bi_iter.bi_sector << 9;
	u64 length = 0;
	u64 map_length;
	int ret;
	struct btrfs_io_geometry geom;

	if (bio_flags & EXTENT_BIO_COMPRESSED)
		return 0;

	length = bio->bi_iter.bi_size;
	map_length = length;
	ret = btrfs_get_io_geometry(fs_info, btrfs_op(bio), logical, map_length,
				    &geom);
	if (ret < 0)
		return ret;

	if (geom.len < length + size)
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
static blk_status_t btrfs_submit_bio_start(void *private_data, struct bio *bio,
				    u64 bio_offset)
{
	struct iyesde *iyesde = private_data;
	blk_status_t ret = 0;

	ret = btrfs_csum_one_bio(iyesde, bio, 0, 0);
	BUG_ON(ret); /* -ENOMEM */
	return 0;
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
static blk_status_t btrfs_submit_bio_hook(struct iyesde *iyesde, struct bio *bio,
					  int mirror_num,
					  unsigned long bio_flags)

{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	enum btrfs_wq_endio_type metadata = BTRFS_WQ_ENDIO_DATA;
	blk_status_t ret = 0;
	int skip_sum;
	int async = !atomic_read(&BTRFS_I(iyesde)->sync_writers);

	skip_sum = BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATASUM;

	if (btrfs_is_free_space_iyesde(BTRFS_I(iyesde)))
		metadata = BTRFS_WQ_ENDIO_FREE_SPACE;

	if (bio_op(bio) != REQ_OP_WRITE) {
		ret = btrfs_bio_wq_end_io(fs_info, bio, metadata);
		if (ret)
			goto out;

		if (bio_flags & EXTENT_BIO_COMPRESSED) {
			ret = btrfs_submit_compressed_read(iyesde, bio,
							   mirror_num,
							   bio_flags);
			goto out;
		} else if (!skip_sum) {
			ret = btrfs_lookup_bio_sums(iyesde, bio, NULL);
			if (ret)
				goto out;
		}
		goto mapit;
	} else if (async && !skip_sum) {
		/* csum items have already been cloned */
		if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID)
			goto mapit;
		/* we're doing a write, do the async checksumming */
		ret = btrfs_wq_submit_bio(fs_info, bio, mirror_num, bio_flags,
					  0, iyesde, btrfs_submit_bio_start);
		goto out;
	} else if (!skip_sum) {
		ret = btrfs_csum_one_bio(iyesde, bio, 0, 0);
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
 * given a list of ordered sums record them in the iyesde.  This happens
 * at IO completion time based on sums calculated at bio submission time.
 */
static yesinline int add_pending_csums(struct btrfs_trans_handle *trans,
			     struct iyesde *iyesde, struct list_head *list)
{
	struct btrfs_ordered_sum *sum;
	int ret;

	list_for_each_entry(sum, list, list) {
		trans->adding_csums = true;
		ret = btrfs_csum_file_blocks(trans,
		       BTRFS_I(iyesde)->root->fs_info->csum_root, sum);
		trans->adding_csums = false;
		if (ret)
			return ret;
	}
	return 0;
}

int btrfs_set_extent_delalloc(struct iyesde *iyesde, u64 start, u64 end,
			      unsigned int extra_bits,
			      struct extent_state **cached_state)
{
	WARN_ON(PAGE_ALIGNED(end));
	return set_extent_delalloc(&BTRFS_I(iyesde)->io_tree, start, end,
				   extra_bits, cached_state);
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
	struct extent_changeset *data_reserved = NULL;
	struct page *page;
	struct iyesde *iyesde;
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

	iyesde = page->mapping->host;
	page_start = page_offset(page);
	page_end = page_offset(page) + PAGE_SIZE - 1;

	lock_extent_bits(&BTRFS_I(iyesde)->io_tree, page_start, page_end,
			 &cached_state);

	/* already ordered? We're done */
	if (PagePrivate2(page))
		goto out;

	ordered = btrfs_lookup_ordered_range(BTRFS_I(iyesde), page_start,
					PAGE_SIZE);
	if (ordered) {
		unlock_extent_cached(&BTRFS_I(iyesde)->io_tree, page_start,
				     page_end, &cached_state);
		unlock_page(page);
		btrfs_start_ordered_extent(iyesde, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	ret = btrfs_delalloc_reserve_space(iyesde, &data_reserved, page_start,
					   PAGE_SIZE);
	if (ret) {
		mapping_set_error(page->mapping, ret);
		end_extent_writepage(page, ret, page_start, page_end);
		ClearPageChecked(page);
		goto out;
	 }

	ret = btrfs_set_extent_delalloc(iyesde, page_start, page_end, 0,
					&cached_state);
	if (ret) {
		mapping_set_error(page->mapping, ret);
		end_extent_writepage(page, ret, page_start, page_end);
		ClearPageChecked(page);
		goto out_reserved;
	}

	ClearPageChecked(page);
	set_page_dirty(page);
out_reserved:
	btrfs_delalloc_release_extents(BTRFS_I(iyesde), PAGE_SIZE);
	if (ret)
		btrfs_delalloc_release_space(iyesde, data_reserved, page_start,
					     PAGE_SIZE, true);
out:
	unlock_extent_cached(&BTRFS_I(iyesde)->io_tree, page_start, page_end,
			     &cached_state);
out_page:
	unlock_page(page);
	put_page(page);
	kfree(fixup);
	extent_changeset_free(data_reserved);
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
int btrfs_writepage_cow_fixup(struct page *page, u64 start, u64 end)
{
	struct iyesde *iyesde = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
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
	btrfs_init_work(&fixup->work, btrfs_writepage_fixup_worker, NULL, NULL);
	fixup->page = page;
	btrfs_queue_work(fs_info->fixup_workers, &fixup->work);
	return -EBUSY;
}

static int insert_reserved_file_extent(struct btrfs_trans_handle *trans,
				       struct iyesde *iyesde, u64 file_pos,
				       u64 disk_bytenr, u64 disk_num_bytes,
				       u64 num_bytes, u64 ram_bytes,
				       u8 compression, u8 encryption,
				       u16 other_encoding, int extent_type)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_file_extent_item *fi;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key ins;
	u64 qg_released;
	int extent_inserted = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * we may be replacing one extent in the tree with ayesther.
	 * The new extent is pinned in the extent map, and we don't want
	 * to drop it from the cache until it is completely in the btree.
	 *
	 * So, tell btrfs_drop_extents to leave this extent in the cache.
	 * the caller is expected to unpin it and allow it to be merged
	 * with the others.
	 */
	ret = __btrfs_drop_extents(trans, root, iyesde, path, file_pos,
				   file_pos + num_bytes, NULL, 0,
				   1, sizeof(*fi), &extent_inserted);
	if (ret)
		goto out;

	if (!extent_inserted) {
		ins.objectid = btrfs_iyes(BTRFS_I(iyesde));
		ins.offset = file_pos;
		ins.type = BTRFS_EXTENT_DATA_KEY;

		path->leave_spinning = 1;
		ret = btrfs_insert_empty_item(trans, root, path, &ins,
					      sizeof(*fi));
		if (ret)
			goto out;
	}
	leaf = path->yesdes[0];
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

	iyesde_add_bytes(iyesde, num_bytes);

	ins.objectid = disk_bytenr;
	ins.offset = disk_num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	/*
	 * Release the reserved range from iyesde dirty range map, as it is
	 * already moved into delayed_ref_head
	 */
	ret = btrfs_qgroup_release_data(iyesde, file_pos, ram_bytes);
	if (ret < 0)
		goto out;
	qg_released = ret;
	ret = btrfs_alloc_reserved_file_extent(trans, root,
					       btrfs_iyes(BTRFS_I(iyesde)),
					       file_pos, qg_released, &ins);
out:
	btrfs_free_path(path);

	return ret;
}

/* snapshot-aware defrag */
struct sa_defrag_extent_backref {
	struct rb_yesde yesde;
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
	struct iyesde *iyesde;
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
	 * refer to only one file extent in ayesther tree.
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
	struct rb_yesde **p = &root->rb_yesde;
	struct rb_yesde *parent = NULL;
	struct sa_defrag_extent_backref *entry;
	int ret;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct sa_defrag_extent_backref, yesde);

		ret = backref_comp(backref, entry);
		if (ret < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_yesde(&backref->yesde, parent, p);
	rb_insert_color(&backref->yesde, root);
}

/*
 * Note the backref might has changed, and in this case we just return 0.
 */
static yesinline int record_one_backref(u64 inum, u64 offset, u64 root_id,
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
	struct iyesde *iyesde = new->iyesde;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	int slot;
	int ret;
	u64 extent_offset;
	u64 num_bytes;

	if (BTRFS_I(iyesde)->root->root_key.objectid == root_id &&
	    inum == btrfs_iyes(BTRFS_I(iyesde)))
		return 0;

	key.objectid = root_id;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;

	root = btrfs_read_fs_root_yes_name(fs_info, &key);
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

		leaf = path->yesdes[0];
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

static yesinline bool record_extent_backrefs(struct btrfs_path *path,
				   struct new_sa_defrag_extent *new)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(new->iyesde->i_sb);
	struct old_sa_defrag_extent *old, *tmp;
	int ret;

	new->path = path;

	list_for_each_entry_safe(old, tmp, &new->head, list) {
		ret = iterate_iyesdes_from_logical(old->bytenr +
						  old->extent_offset, fs_info,
						  path, record_one_backref,
						  old, false);
		if (ret < 0 && ret != -ENOENT)
			return false;

		/* yes backref to be processed for this extent */
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
static yesinline int relink_extent_backref(struct btrfs_path *path,
				 struct sa_defrag_extent_backref *prev,
				 struct sa_defrag_extent_backref *backref)
{
	struct btrfs_file_extent_item *extent;
	struct btrfs_file_extent_item *item;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_trans_handle *trans;
	struct btrfs_ref ref = { 0 };
	struct btrfs_root *root;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct old_sa_defrag_extent *old = backref->old;
	struct new_sa_defrag_extent *new = old->new;
	struct btrfs_fs_info *fs_info = btrfs_sb(new->iyesde->i_sb);
	struct iyesde *iyesde;
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

	root = btrfs_read_fs_root_yes_name(fs_info, &key);
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

	/* step 2: get iyesde */
	key.objectid = backref->inum;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	iyesde = btrfs_iget(fs_info->sb, &key, root);
	if (IS_ERR(iyesde)) {
		srcu_read_unlock(&fs_info->subvol_srcu, index);
		return 0;
	}

	srcu_read_unlock(&fs_info->subvol_srcu, index);

	/* step 3: relink backref */
	lock_start = backref->file_pos;
	lock_end = backref->file_pos + backref->num_bytes - 1;
	lock_extent_bits(&BTRFS_I(iyesde)->io_tree, lock_start, lock_end,
			 &cached);

	ordered = btrfs_lookup_first_ordered_extent(iyesde, lock_end);
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

	extent = btrfs_item_ptr(path->yesdes[0], path->slots[0],
				struct btrfs_file_extent_item);

	if (btrfs_file_extent_generation(path->yesdes[0], extent) !=
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

	ret = btrfs_drop_extents(trans, root, iyesde, start,
				 start + len, 1);
	if (ret)
		goto out_free_path;
again:
	key.objectid = btrfs_iyes(BTRFS_I(iyesde));
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
		leaf = path->yesdes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_len = btrfs_file_extent_num_bytes(leaf, fi);

		if (extent_len + found_key.offset == start &&
		    relink_is_mergable(leaf, fi, new)) {
			btrfs_set_file_extent_num_bytes(leaf, fi,
							extent_len + len);
			btrfs_mark_buffer_dirty(leaf);
			iyesde_add_bytes(iyesde, len);

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

	leaf = path->yesdes[0];
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
	iyesde_add_bytes(iyesde, len);
	btrfs_release_path(path);

	btrfs_init_generic_ref(&ref, BTRFS_ADD_DELAYED_REF, new->bytenr,
			       new->disk_len, 0);
	btrfs_init_data_ref(&ref, backref->root_id, backref->inum,
			    new->file_pos);  /* start - extent_offset */
	ret = btrfs_inc_extent_ref(trans, &ref);
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
	unlock_extent_cached(&BTRFS_I(iyesde)->io_tree, lock_start, lock_end,
			     &cached);
	iput(iyesde);
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
	struct btrfs_fs_info *fs_info = btrfs_sb(new->iyesde->i_sb);
	struct btrfs_path *path;
	struct sa_defrag_extent_backref *backref;
	struct sa_defrag_extent_backref *prev = NULL;
	struct rb_yesde *yesde;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return;

	if (!record_extent_backrefs(path, new)) {
		btrfs_free_path(path);
		goto out;
	}
	btrfs_release_path(path);

	while (1) {
		yesde = rb_first(&new->root);
		if (!yesde)
			break;
		rb_erase(yesde, &new->root);

		backref = rb_entry(yesde, struct sa_defrag_extent_backref, yesde);

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
record_old_file_extents(struct iyesde *iyesde,
			struct btrfs_ordered_extent *ordered)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct old_sa_defrag_extent *old;
	struct new_sa_defrag_extent *new;
	int ret;

	new = kmalloc(sizeof(*new), GFP_NOFS);
	if (!new)
		return NULL;

	new->iyesde = iyesde;
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

	key.objectid = btrfs_iyes(BTRFS_I(iyesde));
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

		l = path->yesdes[0];
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

		if (key.objectid != btrfs_iyes(BTRFS_I(iyesde)))
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
	struct btrfs_block_group *cache;

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
	struct iyesde *iyesde = ordered_extent->iyesde;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_trans_handle *trans = NULL;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct extent_state *cached_state = NULL;
	struct new_sa_defrag_extent *new = NULL;
	int compress_type = 0;
	int ret = 0;
	u64 logical_len = ordered_extent->len;
	bool freespace_iyesde;
	bool truncated = false;
	bool range_locked = false;
	bool clear_new_delalloc_bytes = false;
	bool clear_reserved_extent = true;

	if (!test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags) &&
	    !test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags) &&
	    !test_bit(BTRFS_ORDERED_DIRECT, &ordered_extent->flags))
		clear_new_delalloc_bytes = true;

	freespace_iyesde = btrfs_is_free_space_iyesde(BTRFS_I(iyesde));

	if (test_bit(BTRFS_ORDERED_IOERR, &ordered_extent->flags)) {
		ret = -EIO;
		goto out;
	}

	btrfs_free_io_failure_record(BTRFS_I(iyesde),
			ordered_extent->file_offset,
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
		btrfs_qgroup_free_data(iyesde, NULL, ordered_extent->file_offset,
				       ordered_extent->len);
		btrfs_ordered_update_i_size(iyesde, 0, ordered_extent);
		if (freespace_iyesde)
			trans = btrfs_join_transaction_spacecache(root);
		else
			trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			trans = NULL;
			goto out;
		}
		trans->block_rsv = &BTRFS_I(iyesde)->block_rsv;
		ret = btrfs_update_iyesde_fallback(trans, root, iyesde);
		if (ret) /* -ENOMEM or corruption */
			btrfs_abort_transaction(trans, ret);
		goto out;
	}

	range_locked = true;
	lock_extent_bits(io_tree, ordered_extent->file_offset,
			 ordered_extent->file_offset + ordered_extent->len - 1,
			 &cached_state);

	ret = test_range_bit(io_tree, ordered_extent->file_offset,
			ordered_extent->file_offset + ordered_extent->len - 1,
			EXTENT_DEFRAG, 0, cached_state);
	if (ret) {
		u64 last_snapshot = btrfs_root_last_snapshot(&root->root_item);
		if (0 && last_snapshot >= BTRFS_I(iyesde)->generation)
			/* the iyesde is shared */
			new = record_old_file_extents(iyesde, ordered_extent);

		clear_extent_bit(io_tree, ordered_extent->file_offset,
			ordered_extent->file_offset + ordered_extent->len - 1,
			EXTENT_DEFRAG, 0, 0, &cached_state);
	}

	if (freespace_iyesde)
		trans = btrfs_join_transaction_spacecache(root);
	else
		trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	trans->block_rsv = &BTRFS_I(iyesde)->block_rsv;

	if (test_bit(BTRFS_ORDERED_COMPRESSED, &ordered_extent->flags))
		compress_type = ordered_extent->compress_type;
	if (test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags)) {
		BUG_ON(compress_type);
		btrfs_qgroup_free_data(iyesde, NULL, ordered_extent->file_offset,
				       ordered_extent->len);
		ret = btrfs_mark_extent_written(trans, BTRFS_I(iyesde),
						ordered_extent->file_offset,
						ordered_extent->file_offset +
						logical_len);
	} else {
		BUG_ON(root == fs_info->tree_root);
		ret = insert_reserved_file_extent(trans, iyesde,
						ordered_extent->file_offset,
						ordered_extent->start,
						ordered_extent->disk_len,
						logical_len, logical_len,
						compress_type, 0, 0,
						BTRFS_FILE_EXTENT_REG);
		if (!ret) {
			clear_reserved_extent = false;
			btrfs_release_delalloc_bytes(fs_info,
						     ordered_extent->start,
						     ordered_extent->disk_len);
		}
	}
	unpin_extent_cache(&BTRFS_I(iyesde)->extent_tree,
			   ordered_extent->file_offset, ordered_extent->len,
			   trans->transid);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = add_pending_csums(trans, iyesde, &ordered_extent->list);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	btrfs_ordered_update_i_size(iyesde, 0, ordered_extent);
	ret = btrfs_update_iyesde_fallback(trans, root, iyesde);
	if (ret) { /* -ENOMEM or corruption */
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
	ret = 0;
out:
	if (range_locked || clear_new_delalloc_bytes) {
		unsigned int clear_bits = 0;

		if (range_locked)
			clear_bits |= EXTENT_LOCKED;
		if (clear_new_delalloc_bytes)
			clear_bits |= EXTENT_DELALLOC_NEW;
		clear_extent_bit(&BTRFS_I(iyesde)->io_tree,
				 ordered_extent->file_offset,
				 ordered_extent->file_offset +
				 ordered_extent->len - 1,
				 clear_bits,
				 (clear_bits & EXTENT_LOCKED) ? 1 : 0,
				 0, &cached_state);
	}

	if (trans)
		btrfs_end_transaction(trans);

	if (ret || truncated) {
		u64 start, end;

		if (truncated)
			start = ordered_extent->file_offset + logical_len;
		else
			start = ordered_extent->file_offset;
		end = ordered_extent->file_offset + ordered_extent->len - 1;
		clear_extent_uptodate(io_tree, start, end, NULL);

		/* Drop the cache for the part of the extent we didn't write. */
		btrfs_drop_extent_cache(BTRFS_I(iyesde), start, end, 0);

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
		    !test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags))
			btrfs_free_reserved_extent(fs_info,
						   ordered_extent->start,
						   ordered_extent->disk_len, 1);
	}


	/*
	 * This needs to be done to make sure anybody waiting kyesws we are done
	 * updating everything for this ordered extent.
	 */
	btrfs_remove_ordered_extent(iyesde, ordered_extent);

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

void btrfs_writepage_endio_finish_ordered(struct page *page, u64 start,
					  u64 end, int uptodate)
{
	struct iyesde *iyesde = page->mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_ordered_extent *ordered_extent = NULL;
	struct btrfs_workqueue *wq;

	trace_btrfs_writepage_end_io_hook(page, start, end, uptodate);

	ClearPagePrivate2(page);
	if (!btrfs_dec_test_ordered_pending(iyesde, &ordered_extent, start,
					    end - start + 1, uptodate))
		return;

	if (btrfs_is_free_space_iyesde(BTRFS_I(iyesde)))
		wq = fs_info->endio_freespace_worker;
	else
		wq = fs_info->endio_write_workers;

	btrfs_init_work(&ordered_extent->work, finish_ordered_fn, NULL, NULL);
	btrfs_queue_work(wq, &ordered_extent->work);
}

static int __readpage_endio_check(struct iyesde *iyesde,
				  struct btrfs_io_bio *io_bio,
				  int icsum, struct page *page,
				  int pgoff, u64 start, size_t len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	char *kaddr;
	u16 csum_size = btrfs_super_csum_size(fs_info->super_copy);
	u8 *csum_expected;
	u8 csum[BTRFS_CSUM_SIZE];

	csum_expected = ((u8 *)io_bio->csum) + icsum * csum_size;

	kaddr = kmap_atomic(page);
	shash->tfm = fs_info->csum_shash;

	crypto_shash_init(shash);
	crypto_shash_update(shash, kaddr + pgoff, len);
	crypto_shash_final(shash, csum);

	if (memcmp(csum, csum_expected, csum_size))
		goto zeroit;

	kunmap_atomic(kaddr);
	return 0;
zeroit:
	btrfs_print_data_csum_error(BTRFS_I(iyesde), start, csum, csum_expected,
				    io_bio->mirror_num);
	memset(kaddr + pgoff, 1, len);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);
	return -EIO;
}

/*
 * when reads are done, we need to check csums to verify the data is correct
 * if there's a match, we allow the bio to finish.  If yest, the code in
 * extent_io.c will try to find good copies for us.
 */
static int btrfs_readpage_end_io_hook(struct btrfs_io_bio *io_bio,
				      u64 phy_offset, struct page *page,
				      u64 start, u64 end, int mirror)
{
	size_t offset = start - page_offset(page);
	struct iyesde *iyesde = page->mapping->host;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;

	if (PageChecked(page)) {
		ClearPageChecked(page);
		return 0;
	}

	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATASUM)
		return 0;

	if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID &&
	    test_range_bit(io_tree, start, end, EXTENT_NODATASUM, 1, NULL)) {
		clear_extent_bits(io_tree, start, end, EXTENT_NODATASUM);
		return 0;
	}

	phy_offset >>= iyesde->i_sb->s_blocksize_bits;
	return __readpage_endio_check(iyesde, io_bio, phy_offset, page, offset,
				      start, (size_t)(end - start + 1));
}

/*
 * btrfs_add_delayed_iput - perform a delayed iput on @iyesde
 *
 * @iyesde: The iyesde we want to perform iput on
 *
 * This function uses the generic vfs_iyesde::i_count to track whether we should
 * just decrement it (in case it's > 1) or if this is the last iput then link
 * the iyesde to the delayed iput machinery. Delayed iputs are processed at
 * transaction commit time/superblock commit/cleaner kthread.
 */
void btrfs_add_delayed_iput(struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_iyesde *biyesde = BTRFS_I(iyesde);

	if (atomic_add_unless(&iyesde->i_count, -1, 1))
		return;

	atomic_inc(&fs_info->nr_delayed_iputs);
	spin_lock(&fs_info->delayed_iput_lock);
	ASSERT(list_empty(&biyesde->delayed_iput));
	list_add_tail(&biyesde->delayed_iput, &fs_info->delayed_iputs);
	spin_unlock(&fs_info->delayed_iput_lock);
	if (!test_bit(BTRFS_FS_CLEANER_RUNNING, &fs_info->flags))
		wake_up_process(fs_info->cleaner_kthread);
}

static void run_delayed_iput_locked(struct btrfs_fs_info *fs_info,
				    struct btrfs_iyesde *iyesde)
{
	list_del_init(&iyesde->delayed_iput);
	spin_unlock(&fs_info->delayed_iput_lock);
	iput(&iyesde->vfs_iyesde);
	if (atomic_dec_and_test(&fs_info->nr_delayed_iputs))
		wake_up(&fs_info->delayed_iputs_wait);
	spin_lock(&fs_info->delayed_iput_lock);
}

static void btrfs_run_delayed_iput(struct btrfs_fs_info *fs_info,
				   struct btrfs_iyesde *iyesde)
{
	if (!list_empty(&iyesde->delayed_iput)) {
		spin_lock(&fs_info->delayed_iput_lock);
		if (!list_empty(&iyesde->delayed_iput))
			run_delayed_iput_locked(fs_info, iyesde);
		spin_unlock(&fs_info->delayed_iput_lock);
	}
}

void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info)
{

	spin_lock(&fs_info->delayed_iput_lock);
	while (!list_empty(&fs_info->delayed_iputs)) {
		struct btrfs_iyesde *iyesde;

		iyesde = list_first_entry(&fs_info->delayed_iputs,
				struct btrfs_iyesde, delayed_iput);
		run_delayed_iput_locked(fs_info, iyesde);
	}
	spin_unlock(&fs_info->delayed_iput_lock);
}

/**
 * btrfs_wait_on_delayed_iputs - wait on the delayed iputs to be done running
 * @fs_info - the fs_info for this fs
 * @return - EINTR if we were killed, 0 if yesthing's pending
 *
 * This will wait on any delayed iputs that are currently running with KILLABLE
 * set.  Once they are all done running we will return, unless we are killed in
 * which case we return EINTR. This helps in user operations like fallocate etc
 * that might get blocked on the iputs.
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
 * This creates an orphan entry for the given iyesde in case something goes wrong
 * in the middle of an unlink.
 */
int btrfs_orphan_add(struct btrfs_trans_handle *trans,
		     struct btrfs_iyesde *iyesde)
{
	int ret;

	ret = btrfs_insert_orphan_item(trans, iyesde->root, btrfs_iyes(iyesde));
	if (ret && ret != -EEXIST) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	return 0;
}

/*
 * We have done the delete so we can go ahead and remove the orphan item for
 * this particular iyesde.
 */
static int btrfs_orphan_del(struct btrfs_trans_handle *trans,
			    struct btrfs_iyesde *iyesde)
{
	return btrfs_del_orphan_item(trans, iyesde->root, btrfs_iyes(iyesde));
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
	struct iyesde *iyesde;
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
		leaf = path->yesdes[0];
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
		 * crossing root thing.  we store the iyesde number in the
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
		iyesde = btrfs_iget(fs_info->sb, &found_key, root);
		ret = PTR_ERR_OR_ZERO(iyesde);
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
			 *  b) a free space cache iyesde
			 * We need to distinguish those two, as the snapshot
			 * orphan must yest get deleted.
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
		 * If we have an iyesde with links, there are a couple of
		 * possibilities. Old kernels (before v3.12) used to create an
		 * orphan item for truncate indicating that there were possibly
		 * extent items past i_size that needed to be deleted. In v3.12,
		 * truncate was changed to update i_size in sync with the extent
		 * items, but the (useless) orphan item was still created. Since
		 * v4.18, we don't create the orphan item for truncate at all.
		 *
		 * So, this item could mean that we need to do a truncate, but
		 * only if this filesystem was last used on a pre-v3.12 kernel
		 * and was yest cleanly unmounted. The odds of that are quite
		 * slim, and it's a pain to do the truncate yesw, so just delete
		 * the orphan item.
		 *
		 * It's also possible that this orphan item was supposed to be
		 * deleted but wasn't. The iyesde number may have been reused,
		 * but either way, we can delete the orphan item.
		 */
		if (ret == -ENOENT || iyesde->i_nlink) {
			if (!ret)
				iput(iyesde);
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

		/* this will do delete_iyesde and everything for us */
		iput(iyesde);
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
		btrfs_err(fs_info, "could yest do orphan cleanup %d", ret);
	btrfs_free_path(path);
	return ret;
}

/*
 * very simple check to peek ahead in the leaf looking for xattrs.  If we
 * don't find any xattrs, we kyesw there can't be any acls.
 *
 * slot is the slot the iyesde is in, objectid is the objectid of the iyesde
 */
static yesinline int acls_after_iyesde_item(struct extent_buffer *leaf,
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

		/* we found a different objectid, there must yest be acls */
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
		 * it goes iyesde, iyesde backrefs, xattrs, extents,
		 * so if there are a ton of hard links to an iyesde there can
		 * be a lot of backrefs.  Don't waste time searching too hard,
		 * this is just an optimization
		 */
		if (scanned >= 8)
			break;
	}
	/* we hit the end of the leaf before we found an xattr or
	 * something larger than an xattr.  We have to assume the iyesde
	 * has acls
	 */
	if (*first_xattr_slot == -1)
		*first_xattr_slot = slot;
	return 1;
}

/*
 * read an iyesde from the btree into the in-memory iyesde
 */
static int btrfs_read_locked_iyesde(struct iyesde *iyesde,
				   struct btrfs_path *in_path)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_path *path = in_path;
	struct extent_buffer *leaf;
	struct btrfs_iyesde_item *iyesde_item;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_key location;
	unsigned long ptr;
	int maybe_acls;
	u32 rdev;
	int ret;
	bool filled = false;
	int first_xattr_slot;

	ret = btrfs_fill_iyesde(iyesde, &rdev);
	if (!ret)
		filled = true;

	if (!path) {
		path = btrfs_alloc_path();
		if (!path)
			return -ENOMEM;
	}

	memcpy(&location, &BTRFS_I(iyesde)->location, sizeof(location));

	ret = btrfs_lookup_iyesde(NULL, root, path, &location, 0);
	if (ret) {
		if (path != in_path)
			btrfs_free_path(path);
		return ret;
	}

	leaf = path->yesdes[0];

	if (filled)
		goto cache_index;

	iyesde_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_iyesde_item);
	iyesde->i_mode = btrfs_iyesde_mode(leaf, iyesde_item);
	set_nlink(iyesde, btrfs_iyesde_nlink(leaf, iyesde_item));
	i_uid_write(iyesde, btrfs_iyesde_uid(leaf, iyesde_item));
	i_gid_write(iyesde, btrfs_iyesde_gid(leaf, iyesde_item));
	btrfs_i_size_write(BTRFS_I(iyesde), btrfs_iyesde_size(leaf, iyesde_item));

	iyesde->i_atime.tv_sec = btrfs_timespec_sec(leaf, &iyesde_item->atime);
	iyesde->i_atime.tv_nsec = btrfs_timespec_nsec(leaf, &iyesde_item->atime);

	iyesde->i_mtime.tv_sec = btrfs_timespec_sec(leaf, &iyesde_item->mtime);
	iyesde->i_mtime.tv_nsec = btrfs_timespec_nsec(leaf, &iyesde_item->mtime);

	iyesde->i_ctime.tv_sec = btrfs_timespec_sec(leaf, &iyesde_item->ctime);
	iyesde->i_ctime.tv_nsec = btrfs_timespec_nsec(leaf, &iyesde_item->ctime);

	BTRFS_I(iyesde)->i_otime.tv_sec =
		btrfs_timespec_sec(leaf, &iyesde_item->otime);
	BTRFS_I(iyesde)->i_otime.tv_nsec =
		btrfs_timespec_nsec(leaf, &iyesde_item->otime);

	iyesde_set_bytes(iyesde, btrfs_iyesde_nbytes(leaf, iyesde_item));
	BTRFS_I(iyesde)->generation = btrfs_iyesde_generation(leaf, iyesde_item);
	BTRFS_I(iyesde)->last_trans = btrfs_iyesde_transid(leaf, iyesde_item);

	iyesde_set_iversion_queried(iyesde,
				   btrfs_iyesde_sequence(leaf, iyesde_item));
	iyesde->i_generation = BTRFS_I(iyesde)->generation;
	iyesde->i_rdev = 0;
	rdev = btrfs_iyesde_rdev(leaf, iyesde_item);

	BTRFS_I(iyesde)->index_cnt = (u64)-1;
	BTRFS_I(iyesde)->flags = btrfs_iyesde_flags(leaf, iyesde_item);

cache_index:
	/*
	 * If we were modified in the current generation and evicted from memory
	 * and then re-read we need to do a full sync since we don't have any
	 * idea about which extents were modified before we were evicted from
	 * cache.
	 *
	 * This is required for both iyesde re-read from disk and delayed iyesde
	 * in delayed_yesdes_tree.
	 */
	if (BTRFS_I(iyesde)->last_trans == fs_info->generation)
		set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
			&BTRFS_I(iyesde)->runtime_flags);

	/*
	 * We don't persist the id of the transaction where an unlink operation
	 * against the iyesde was last made. So here we assume the iyesde might
	 * have been evicted, and therefore the exact value of last_unlink_trans
	 * lost, and set it to last_trans to avoid metadata inconsistencies
	 * between the iyesde and its parent if the iyesde is fsync'ed and the log
	 * replayed. For example, in the scenario:
	 *
	 * touch mydir/foo
	 * ln mydir/foo mydir/bar
	 * sync
	 * unlink mydir/bar
	 * echo 2 > /proc/sys/vm/drop_caches   # evicts iyesde
	 * xfs_io -c fsync mydir/foo
	 * <power failure>
	 * mount fs, triggers fsync log replay
	 *
	 * We must make sure that when we fsync our iyesde foo we also log its
	 * parent iyesde, otherwise after log replay the parent still has the
	 * dentry with the "bar" name but our iyesde foo has a link count of 1
	 * and doesn't have an iyesde ref with the name "bar" anymore.
	 *
	 * Setting last_unlink_trans to last_trans is a pessimistic approach,
	 * but it guarantees correctness at the expense of occasional full
	 * transaction commits on fsync if our iyesde is a directory, or if our
	 * iyesde is yest a directory, logging its parent unnecessarily.
	 */
	BTRFS_I(iyesde)->last_unlink_trans = BTRFS_I(iyesde)->last_trans;

	path->slots[0]++;
	if (iyesde->i_nlink != 1 ||
	    path->slots[0] >= btrfs_header_nritems(leaf))
		goto cache_acl;

	btrfs_item_key_to_cpu(leaf, &location, path->slots[0]);
	if (location.objectid != btrfs_iyes(BTRFS_I(iyesde)))
		goto cache_acl;

	ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
	if (location.type == BTRFS_INODE_REF_KEY) {
		struct btrfs_iyesde_ref *ref;

		ref = (struct btrfs_iyesde_ref *)ptr;
		BTRFS_I(iyesde)->dir_index = btrfs_iyesde_ref_index(leaf, ref);
	} else if (location.type == BTRFS_INODE_EXTREF_KEY) {
		struct btrfs_iyesde_extref *extref;

		extref = (struct btrfs_iyesde_extref *)ptr;
		BTRFS_I(iyesde)->dir_index = btrfs_iyesde_extref_index(leaf,
								     extref);
	}
cache_acl:
	/*
	 * try to precache a NULL acl entry for files that don't have
	 * any xattrs or acls
	 */
	maybe_acls = acls_after_iyesde_item(leaf, path->slots[0],
			btrfs_iyes(BTRFS_I(iyesde)), &first_xattr_slot);
	if (first_xattr_slot != -1) {
		path->slots[0] = first_xattr_slot;
		ret = btrfs_load_iyesde_props(iyesde, path);
		if (ret)
			btrfs_err(fs_info,
				  "error loading props for iyes %llu (root %llu): %d",
				  btrfs_iyes(BTRFS_I(iyesde)),
				  root->root_key.objectid, ret);
	}
	if (path != in_path)
		btrfs_free_path(path);

	if (!maybe_acls)
		cache_yes_acl(iyesde);

	switch (iyesde->i_mode & S_IFMT) {
	case S_IFREG:
		iyesde->i_mapping->a_ops = &btrfs_aops;
		BTRFS_I(iyesde)->io_tree.ops = &btrfs_extent_io_ops;
		iyesde->i_fop = &btrfs_file_operations;
		iyesde->i_op = &btrfs_file_iyesde_operations;
		break;
	case S_IFDIR:
		iyesde->i_fop = &btrfs_dir_file_operations;
		iyesde->i_op = &btrfs_dir_iyesde_operations;
		break;
	case S_IFLNK:
		iyesde->i_op = &btrfs_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &btrfs_aops;
		break;
	default:
		iyesde->i_op = &btrfs_special_iyesde_operations;
		init_special_iyesde(iyesde, iyesde->i_mode, rdev);
		break;
	}

	btrfs_sync_iyesde_flags_to_i_flags(iyesde);
	return 0;
}

/*
 * given a leaf and an iyesde, copy the iyesde fields into the leaf
 */
static void fill_iyesde_item(struct btrfs_trans_handle *trans,
			    struct extent_buffer *leaf,
			    struct btrfs_iyesde_item *item,
			    struct iyesde *iyesde)
{
	struct btrfs_map_token token;

	btrfs_init_map_token(&token, leaf);

	btrfs_set_token_iyesde_uid(leaf, item, i_uid_read(iyesde), &token);
	btrfs_set_token_iyesde_gid(leaf, item, i_gid_read(iyesde), &token);
	btrfs_set_token_iyesde_size(leaf, item, BTRFS_I(iyesde)->disk_i_size,
				   &token);
	btrfs_set_token_iyesde_mode(leaf, item, iyesde->i_mode, &token);
	btrfs_set_token_iyesde_nlink(leaf, item, iyesde->i_nlink, &token);

	btrfs_set_token_timespec_sec(leaf, &item->atime,
				     iyesde->i_atime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->atime,
				      iyesde->i_atime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->mtime,
				     iyesde->i_mtime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->mtime,
				      iyesde->i_mtime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->ctime,
				     iyesde->i_ctime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->ctime,
				      iyesde->i_ctime.tv_nsec, &token);

	btrfs_set_token_timespec_sec(leaf, &item->otime,
				     BTRFS_I(iyesde)->i_otime.tv_sec, &token);
	btrfs_set_token_timespec_nsec(leaf, &item->otime,
				      BTRFS_I(iyesde)->i_otime.tv_nsec, &token);

	btrfs_set_token_iyesde_nbytes(leaf, item, iyesde_get_bytes(iyesde),
				     &token);
	btrfs_set_token_iyesde_generation(leaf, item, BTRFS_I(iyesde)->generation,
					 &token);
	btrfs_set_token_iyesde_sequence(leaf, item, iyesde_peek_iversion(iyesde),
				       &token);
	btrfs_set_token_iyesde_transid(leaf, item, trans->transid, &token);
	btrfs_set_token_iyesde_rdev(leaf, item, iyesde->i_rdev, &token);
	btrfs_set_token_iyesde_flags(leaf, item, BTRFS_I(iyesde)->flags, &token);
	btrfs_set_token_iyesde_block_group(leaf, item, 0, &token);
}

/*
 * copy everything in the in-memory iyesde into the btree.
 */
static yesinline int btrfs_update_iyesde_item(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct iyesde *iyesde)
{
	struct btrfs_iyesde_item *iyesde_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	ret = btrfs_lookup_iyesde(trans, root, path, &BTRFS_I(iyesde)->location,
				 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	leaf = path->yesdes[0];
	iyesde_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_iyesde_item);

	fill_iyesde_item(trans, leaf, iyesde_item, iyesde);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_set_iyesde_last_trans(trans, iyesde);
	ret = 0;
failed:
	btrfs_free_path(path);
	return ret;
}

/*
 * copy everything in the in-memory iyesde into the btree.
 */
yesinline int btrfs_update_iyesde(struct btrfs_trans_handle *trans,
				struct btrfs_root *root, struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	/*
	 * If the iyesde is a free space iyesde, we can deadlock during commit
	 * if we put it into the delayed code.
	 *
	 * The data relocation iyesde should also be directly updated
	 * without delay
	 */
	if (!btrfs_is_free_space_iyesde(BTRFS_I(iyesde))
	    && root->root_key.objectid != BTRFS_DATA_RELOC_TREE_OBJECTID
	    && !test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags)) {
		btrfs_update_root_times(trans, root);

		ret = btrfs_delayed_update_iyesde(trans, root, iyesde);
		if (!ret)
			btrfs_set_iyesde_last_trans(trans, iyesde);
		return ret;
	}

	return btrfs_update_iyesde_item(trans, root, iyesde);
}

yesinline int btrfs_update_iyesde_fallback(struct btrfs_trans_handle *trans,
					 struct btrfs_root *root,
					 struct iyesde *iyesde)
{
	int ret;

	ret = btrfs_update_iyesde(trans, root, iyesde);
	if (ret == -ENOSPC)
		return btrfs_update_iyesde_item(trans, root, iyesde);
	return ret;
}

/*
 * unlink helper that gets used here in iyesde.c and in the tree logging
 * recovery code.  It remove a link in a directory with a given name, and
 * also drops the back refs in the iyesde to the directory
 */
static int __btrfs_unlink_iyesde(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_iyesde *dir,
				struct btrfs_iyesde *iyesde,
				const char *name, int name_len)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	int ret = 0;
	struct btrfs_dir_item *di;
	u64 index;
	u64 iyes = btrfs_iyes(iyesde);
	u64 dir_iyes = btrfs_iyes(dir);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	path->leave_spinning = 1;
	di = btrfs_lookup_dir_item(trans, root, path, dir_iyes,
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
	 * the iyesde ref, since we get the iyesde ref, remove it directly,
	 * it is unnecessary to do delayed deletion.
	 *
	 * But if we have dir index, needn't search iyesde ref to get it.
	 * Since the iyesde ref is close to the iyesde item, it is better
	 * that we delay to delete it, and just do this deletion when
	 * we update the iyesde item.
	 */
	if (iyesde->dir_index) {
		ret = btrfs_delayed_delete_iyesde_ref(iyesde);
		if (!ret) {
			index = iyesde->dir_index;
			goto skip_backref;
		}
	}

	ret = btrfs_del_iyesde_ref(trans, root, name, name_len, iyes,
				  dir_iyes, &index);
	if (ret) {
		btrfs_info(fs_info,
			"failed to delete reference to %.*s, iyesde %llu parent %llu",
			name_len, name, iyes, dir_iyes);
		btrfs_abort_transaction(trans, ret);
		goto err;
	}
skip_backref:
	ret = btrfs_delete_delayed_dir_index(trans, dir, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto err;
	}

	ret = btrfs_del_iyesde_ref_in_log(trans, root, name, name_len, iyesde,
			dir_iyes);
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
	 * being run in btrfs-cleaner context.  If we have eyesugh of these built
	 * up we can end up burning a lot of time in btrfs-cleaner without any
	 * way to throttle the unlinks.  Since we're currently holding a ref on
	 * the iyesde we can run the delayed iput here without any issues as the
	 * final iput won't be done until after we drop the ref we're currently
	 * holding.
	 */
	btrfs_run_delayed_iput(fs_info, iyesde);
err:
	btrfs_free_path(path);
	if (ret)
		goto out;

	btrfs_i_size_write(dir, dir->vfs_iyesde.i_size - name_len * 2);
	iyesde_inc_iversion(&iyesde->vfs_iyesde);
	iyesde_inc_iversion(&dir->vfs_iyesde);
	iyesde->vfs_iyesde.i_ctime = dir->vfs_iyesde.i_mtime =
		dir->vfs_iyesde.i_ctime = current_time(&iyesde->vfs_iyesde);
	ret = btrfs_update_iyesde(trans, root, &dir->vfs_iyesde);
out:
	return ret;
}

int btrfs_unlink_iyesde(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       struct btrfs_iyesde *dir, struct btrfs_iyesde *iyesde,
		       const char *name, int name_len)
{
	int ret;
	ret = __btrfs_unlink_iyesde(trans, root, dir, iyesde, name, name_len);
	if (!ret) {
		drop_nlink(&iyesde->vfs_iyesde);
		ret = btrfs_update_iyesde(trans, root, &iyesde->vfs_iyesde);
	}
	return ret;
}

/*
 * helper to start transaction for unlink and rmdir.
 *
 * unlink and rmdir are special in btrfs, they do yest always free space, so
 * if we canyest make our reservations the yesrmal way try and see if there is
 * plenty of slack room in the global reserve to migrate, otherwise we canyest
 * allow the unlink to occur.
 */
static struct btrfs_trans_handle *__unlink_start_trans(struct iyesde *dir)
{
	struct btrfs_root *root = BTRFS_I(dir)->root;

	/*
	 * 1 for the possible orphan item
	 * 1 for the dir item
	 * 1 for the dir index
	 * 1 for the iyesde ref
	 * 1 for the iyesde
	 */
	return btrfs_start_transaction_fallback_global_rsv(root, 5, 5);
}

static int btrfs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_trans_handle *trans;
	struct iyesde *iyesde = d_iyesde(dentry);
	int ret;

	trans = __unlink_start_trans(dir);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	btrfs_record_unlink_dir(trans, BTRFS_I(dir), BTRFS_I(d_iyesde(dentry)),
			0);

	ret = btrfs_unlink_iyesde(trans, root, BTRFS_I(dir),
			BTRFS_I(d_iyesde(dentry)), dentry->d_name.name,
			dentry->d_name.len);
	if (ret)
		goto out;

	if (iyesde->i_nlink == 0) {
		ret = btrfs_orphan_add(trans, BTRFS_I(iyesde));
		if (ret)
			goto out;
	}

out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(root->fs_info);
	return ret;
}

static int btrfs_unlink_subvol(struct btrfs_trans_handle *trans,
			       struct iyesde *dir, u64 objectid,
			       const char *name, int name_len)
{
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 index;
	int ret;
	u64 dir_iyes = btrfs_iyes(BTRFS_I(dir));

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	di = btrfs_lookup_dir_item(trans, root, path, dir_iyes,
				   name, name_len, -1);
	if (IS_ERR_OR_NULL(di)) {
		ret = di ? PTR_ERR(di) : -ENOENT;
		goto out;
	}

	leaf = path->yesdes[0];
	btrfs_dir_item_key_to_cpu(leaf, di, &key);
	WARN_ON(key.type != BTRFS_ROOT_ITEM_KEY || key.objectid != objectid);
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
	btrfs_release_path(path);

	ret = btrfs_del_root_ref(trans, objectid, root->root_key.objectid,
				 dir_iyes, &index, name, name_len);
	if (ret < 0) {
		if (ret != -ENOENT) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
		di = btrfs_search_dir_index_item(root, path, dir_iyes,
						 name, name_len);
		if (IS_ERR_OR_NULL(di)) {
			if (!di)
				ret = -ENOENT;
			else
				ret = PTR_ERR(di);
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		leaf = path->yesdes[0];
		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		index = key.offset;
	}
	btrfs_release_path(path);

	ret = btrfs_delete_delayed_dir_index(trans, BTRFS_I(dir), index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	btrfs_i_size_write(BTRFS_I(dir), dir->i_size - name_len * 2);
	iyesde_inc_iversion(dir);
	dir->i_mtime = dir->i_ctime = current_time(dir);
	ret = btrfs_update_iyesde_fallback(trans, root, dir);
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
static yesinline int may_destroy_subvol(struct btrfs_root *root)
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
		btrfs_dir_item_key_to_cpu(path->yesdes[0], di, &key);
		if (key.objectid == root->root_key.objectid) {
			ret = -EPERM;
			btrfs_err(fs_info,
				  "deleting default subvolume %llu is yest allowed",
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
		btrfs_item_key_to_cpu(path->yesdes[0], &key, path->slots[0]);
		if (key.objectid == root->root_key.objectid &&
		    key.type == BTRFS_ROOT_REF_KEY)
			ret = -ENOTEMPTY;
	}
out:
	btrfs_free_path(path);
	return ret;
}

/* Delete all dentries for iyesdes belonging to the root */
static void btrfs_prune_dentries(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct rb_yesde *yesde;
	struct rb_yesde *prev;
	struct btrfs_iyesde *entry;
	struct iyesde *iyesde;
	u64 objectid = 0;

	if (!test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		WARN_ON(btrfs_root_refs(&root->root_item) != 0);

	spin_lock(&root->iyesde_lock);
again:
	yesde = root->iyesde_tree.rb_yesde;
	prev = NULL;
	while (yesde) {
		prev = yesde;
		entry = rb_entry(yesde, struct btrfs_iyesde, rb_yesde);

		if (objectid < btrfs_iyes(entry))
			yesde = yesde->rb_left;
		else if (objectid > btrfs_iyes(entry))
			yesde = yesde->rb_right;
		else
			break;
	}
	if (!yesde) {
		while (prev) {
			entry = rb_entry(prev, struct btrfs_iyesde, rb_yesde);
			if (objectid <= btrfs_iyes(entry)) {
				yesde = prev;
				break;
			}
			prev = rb_next(prev);
		}
	}
	while (yesde) {
		entry = rb_entry(yesde, struct btrfs_iyesde, rb_yesde);
		objectid = btrfs_iyes(entry) + 1;
		iyesde = igrab(&entry->vfs_iyesde);
		if (iyesde) {
			spin_unlock(&root->iyesde_lock);
			if (atomic_read(&iyesde->i_count) > 1)
				d_prune_aliases(iyesde);
			/*
			 * btrfs_drop_iyesde will have it removed from the iyesde
			 * cache when its usage count hits zero.
			 */
			iput(iyesde);
			cond_resched();
			spin_lock(&root->iyesde_lock);
			goto again;
		}

		if (cond_resched_lock(&root->iyesde_lock))
			goto again;

		yesde = rb_next(yesde);
	}
	spin_unlock(&root->iyesde_lock);
}

int btrfs_delete_subvolume(struct iyesde *dir, struct dentry *dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dentry->d_sb);
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct btrfs_root *dest = BTRFS_I(iyesde)->root;
	struct btrfs_trans_handle *trans;
	struct btrfs_block_rsv block_rsv;
	u64 root_flags;
	int ret;
	int err;

	/*
	 * Don't allow to delete a subvolume with send in progress. This is
	 * inside the iyesde lock so the error handling that has to drop the bit
	 * again is yest run concurrently.
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

	err = may_destroy_subvol(dest);
	if (err)
		goto out_up_write;

	btrfs_init_block_rsv(&block_rsv, BTRFS_BLOCK_RSV_TEMP);
	/*
	 * One for dir iyesde,
	 * two for dir entries,
	 * two for root ref/backref.
	 */
	err = btrfs_subvolume_reserve_metadata(root, &block_rsv, 5, true);
	if (err)
		goto out_up_write;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_release;
	}
	trans->block_rsv = &block_rsv;
	trans->bytes_reserved = block_rsv.size;

	btrfs_record_snapshot_destroy(trans, BTRFS_I(dir));

	ret = btrfs_unlink_subvol(trans, dir, dest->root_key.objectid,
				  dentry->d_name.name, dentry->d_name.len);
	if (ret) {
		err = ret;
		btrfs_abort_transaction(trans, ret);
		goto out_end_trans;
	}

	btrfs_record_root_in_trans(trans, dest);

	memset(&dest->root_item.drop_progress, 0,
		sizeof(dest->root_item.drop_progress));
	dest->root_item.drop_level = 0;
	btrfs_set_root_refs(&dest->root_item, 0);

	if (!test_and_set_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &dest->state)) {
		ret = btrfs_insert_orphan_item(trans,
					fs_info->tree_root,
					dest->root_key.objectid);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			err = ret;
			goto out_end_trans;
		}
	}

	ret = btrfs_uuid_tree_remove(trans, dest->root_item.uuid,
				  BTRFS_UUID_KEY_SUBVOL,
				  dest->root_key.objectid);
	if (ret && ret != -ENOENT) {
		btrfs_abort_transaction(trans, ret);
		err = ret;
		goto out_end_trans;
	}
	if (!btrfs_is_empty_uuid(dest->root_item.received_uuid)) {
		ret = btrfs_uuid_tree_remove(trans,
					  dest->root_item.received_uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  dest->root_key.objectid);
		if (ret && ret != -ENOENT) {
			btrfs_abort_transaction(trans, ret);
			err = ret;
			goto out_end_trans;
		}
	}

out_end_trans:
	trans->block_rsv = NULL;
	trans->bytes_reserved = 0;
	ret = btrfs_end_transaction(trans);
	if (ret && !err)
		err = ret;
	iyesde->i_flags |= S_DEAD;
out_release:
	btrfs_subvolume_release_metadata(fs_info, &block_rsv);
out_up_write:
	up_write(&fs_info->subvol_sem);
	if (err) {
		spin_lock(&dest->root_item_lock);
		root_flags = btrfs_root_flags(&dest->root_item);
		btrfs_set_root_flags(&dest->root_item,
				root_flags & ~BTRFS_ROOT_SUBVOL_DEAD);
		spin_unlock(&dest->root_item_lock);
	} else {
		d_invalidate(dentry);
		btrfs_prune_dentries(dest);
		ASSERT(dest->send_in_progress == 0);

		/* the last ref */
		if (dest->iyes_cache_iyesde) {
			iput(dest->iyes_cache_iyesde);
			dest->iyes_cache_iyesde = NULL;
		}
	}

	return err;
}

static int btrfs_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int err = 0;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_trans_handle *trans;
	u64 last_unlink_trans;

	if (iyesde->i_size > BTRFS_EMPTY_DIR_SIZE)
		return -ENOTEMPTY;
	if (btrfs_iyes(BTRFS_I(iyesde)) == BTRFS_FIRST_FREE_OBJECTID)
		return btrfs_delete_subvolume(dir, dentry);

	trans = __unlink_start_trans(dir);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	if (unlikely(btrfs_iyes(BTRFS_I(iyesde)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
		err = btrfs_unlink_subvol(trans, dir,
					  BTRFS_I(iyesde)->location.objectid,
					  dentry->d_name.name,
					  dentry->d_name.len);
		goto out;
	}

	err = btrfs_orphan_add(trans, BTRFS_I(iyesde));
	if (err)
		goto out;

	last_unlink_trans = BTRFS_I(iyesde)->last_unlink_trans;

	/* yesw the directory is empty */
	err = btrfs_unlink_iyesde(trans, root, BTRFS_I(dir),
			BTRFS_I(d_iyesde(dentry)), dentry->d_name.name,
			dentry->d_name.len);
	if (!err) {
		btrfs_i_size_write(BTRFS_I(iyesde), 0);
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
 * this can truncate away extent items, csum items and directory items.
 * It starts at a high offset and removes keys until it can't find
 * any higher than new_size
 *
 * csum items that cross the new i_size are truncated to the new size
 * as well.
 *
 * min_type is the minimum key type to truncate down to.  If set to 0, this
 * will kill all the items on this iyesde, including the INODE_ITEM_KEY.
 */
int btrfs_truncate_iyesde_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct iyesde *iyesde,
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
	u64 iyes = btrfs_iyes(BTRFS_I(iyesde));
	u64 bytes_deleted = 0;
	bool be_nice = false;
	bool should_throttle = false;

	BUG_ON(new_size > 0 && min_type != BTRFS_EXTENT_DATA_KEY);

	/*
	 * for yesn-free space iyesdes and ref cows, we want to back off from
	 * time to time
	 */
	if (!btrfs_is_free_space_iyesde(BTRFS_I(iyesde)) &&
	    test_bit(BTRFS_ROOT_REF_COWS, &root->state))
		be_nice = true;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_BACK;

	/*
	 * We want to drop from the next block forward in case this new size is
	 * yest block aligned since we will be keeping the last block of the
	 * extent just the way it is.
	 */
	if (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
	    root == fs_info->tree_root)
		btrfs_drop_extent_cache(BTRFS_I(iyesde), ALIGN(new_size,
					fs_info->sectorsize),
					(u64)-1, 0);

	/*
	 * This function is also used to drop the items in the log tree before
	 * we relog the iyesde, so if root != BTRFS_I(iyesde)->root, it means
	 * it is used to drop the logged items. So we shouldn't kill the delayed
	 * items.
	 */
	if (min_type == 0 && root == BTRFS_I(iyesde)->root)
		btrfs_kill_delayed_iyesde_items(BTRFS_I(iyesde));

	key.objectid = iyes;
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

	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = 0;
		/* there are yes items in the tree for us to truncate, we're
		 * done
		 */
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}

	while (1) {
		fi = NULL;
		leaf = path->yesdes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		found_type = found_key.type;

		if (found_key.objectid != iyes)
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
					BTRFS_I(iyesde), leaf, fi,
					found_key.offset);
			} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
				item_end += btrfs_file_extent_ram_bytes(leaf,
									fi);

				trace_btrfs_truncate_show_fi_inline(
					BTRFS_I(iyesde), leaf, fi, path->slots[0],
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
					iyesde_sub_bytes(iyesde, num_dec);
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
						iyesde_sub_bytes(iyesde, num_dec);
				}
			}
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
			}

			if (test_bit(BTRFS_ROOT_REF_COWS, &root->state))
				iyesde_sub_bytes(iyesde, item_end + 1 - new_size);
		}
delete:
		if (del_item)
			last_size = found_key.offset;
		else
			last_size = new_size;
		if (del_item) {
			if (!pending_del_nr) {
				/* yes pending yet, add ourselves */
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
		    (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
		     root == fs_info->tree_root)) {
			struct btrfs_ref ref = { 0 };

			btrfs_set_path_blocking(path);
			bytes_deleted += extent_num_bytes;

			btrfs_init_generic_ref(&ref, BTRFS_DROP_DELAYED_REF,
					extent_start, extent_num_bytes, 0);
			ref.real_root = root->root_key.objectid;
			btrfs_init_data_ref(&ref, btrfs_header_owner(leaf),
					iyes, extent_offset);
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
			 * adding eyesugh space to keep up with the work we are
			 * generating.  Since we hold a transaction here we
			 * can't flush, and we don't want to FLUSH_LIMIT because
			 * we could have generated too many delayed refs to
			 * actually allocate, so just bail if we're short and
			 * let the yesrmal reservation dance happen higher up.
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
		btrfs_ordered_update_i_size(iyesde, last_size, NULL);
	}

	btrfs_free_path(path);
	return ret;
}

/*
 * btrfs_truncate_block - read, zero a chunk and write a block
 * @iyesde - iyesde that we're zeroing
 * @from - the offset to start zeroing
 * @len - the length to zero, 0 to zero the entire range respective to the
 *	offset
 * @front - zero up to the offset instead of from the offset on
 *
 * This will find the block for the "from" offset and cow the block and zero the
 * part we want to zero.  This is used with truncate and hole punching.
 */
int btrfs_truncate_block(struct iyesde *iyesde, loff_t from, loff_t len,
			int front)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct address_space *mapping = iyesde->i_mapping;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_changeset *data_reserved = NULL;
	char *kaddr;
	u32 blocksize = fs_info->sectorsize;
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (blocksize - 1);
	struct page *page;
	gfp_t mask = btrfs_alloc_write_mask(mapping);
	int ret = 0;
	u64 block_start;
	u64 block_end;

	if (IS_ALIGNED(offset, blocksize) &&
	    (!len || IS_ALIGNED(len, blocksize)))
		goto out;

	block_start = round_down(from, blocksize);
	block_end = block_start + blocksize - 1;

	ret = btrfs_delalloc_reserve_space(iyesde, &data_reserved,
					   block_start, blocksize);
	if (ret)
		goto out;

again:
	page = find_or_create_page(mapping, index, mask);
	if (!page) {
		btrfs_delalloc_release_space(iyesde, data_reserved,
					     block_start, blocksize, true);
		btrfs_delalloc_release_extents(BTRFS_I(iyesde), blocksize);
		ret = -ENOMEM;
		goto out;
	}

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

	ordered = btrfs_lookup_ordered_extent(iyesde, block_start);
	if (ordered) {
		unlock_extent_cached(io_tree, block_start, block_end,
				     &cached_state);
		unlock_page(page);
		put_page(page);
		btrfs_start_ordered_extent(iyesde, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	clear_extent_bit(&BTRFS_I(iyesde)->io_tree, block_start, block_end,
			 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			 0, 0, &cached_state);

	ret = btrfs_set_extent_delalloc(iyesde, block_start, block_end, 0,
					&cached_state);
	if (ret) {
		unlock_extent_cached(io_tree, block_start, block_end,
				     &cached_state);
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
	unlock_extent_cached(io_tree, block_start, block_end, &cached_state);

out_unlock:
	if (ret)
		btrfs_delalloc_release_space(iyesde, data_reserved, block_start,
					     blocksize, true);
	btrfs_delalloc_release_extents(BTRFS_I(iyesde), blocksize);
	unlock_page(page);
	put_page(page);
out:
	extent_changeset_free(data_reserved);
	return ret;
}

static int maybe_insert_hole(struct btrfs_root *root, struct iyesde *iyesde,
			     u64 offset, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_trans_handle *trans;
	int ret;

	/*
	 * Still need to make sure the iyesde looks like it's been updated so
	 * that any holes get logged if we fsync.
	 */
	if (btrfs_fs_incompat(fs_info, NO_HOLES)) {
		BTRFS_I(iyesde)->last_trans = fs_info->generation;
		BTRFS_I(iyesde)->last_sub_trans = root->log_transid;
		BTRFS_I(iyesde)->last_log_commit = root->last_log_commit;
		return 0;
	}

	/*
	 * 1 - for the one we're dropping
	 * 1 - for the one we're adding
	 * 1 - for updating the iyesde.
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_drop_extents(trans, root, iyesde, offset, offset + len, 1);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		return ret;
	}

	ret = btrfs_insert_file_extent(trans, root, btrfs_iyes(BTRFS_I(iyesde)),
			offset, 0, 0, len, 0, len, 0, 0, 0);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	else
		btrfs_update_iyesde(trans, root, iyesde);
	btrfs_end_transaction(trans);
	return ret;
}

/*
 * This function puts in dummy file extents for the area we're creating a hole
 * for.  So if we are truncating this file to a larger size we need to insert
 * these file extents so that btrfs_get_extent will return a EXTENT_MAP_HOLE for
 * the range between oldsize and size
 */
int btrfs_cont_expand(struct iyesde *iyesde, loff_t oldsize, loff_t size)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct extent_map *em = NULL;
	struct extent_state *cached_state = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(iyesde)->extent_tree;
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
	err = btrfs_truncate_block(iyesde, oldsize, 0, 0);
	if (err)
		return err;

	if (size <= hole_start)
		return 0;

	btrfs_lock_and_flush_ordered_range(io_tree, BTRFS_I(iyesde), hole_start,
					   block_end - 1, &cached_state);
	cur_offset = hole_start;
	while (1) {
		em = btrfs_get_extent(BTRFS_I(iyesde), NULL, 0, cur_offset,
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

			err = maybe_insert_hole(root, iyesde, cur_offset,
						hole_size);
			if (err)
				break;
			btrfs_drop_extent_cache(BTRFS_I(iyesde), cur_offset,
						cur_offset + hole_size - 1, 0);
			hole_em = alloc_extent_map();
			if (!hole_em) {
				set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
					&BTRFS_I(iyesde)->runtime_flags);
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
				btrfs_drop_extent_cache(BTRFS_I(iyesde),
							cur_offset,
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
	unlock_extent_cached(io_tree, hole_start, block_end - 1, &cached_state);
	return err;
}

static int btrfs_setsize(struct iyesde *iyesde, struct iattr *attr)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_trans_handle *trans;
	loff_t oldsize = i_size_read(iyesde);
	loff_t newsize = attr->ia_size;
	int mask = attr->ia_valid;
	int ret;

	/*
	 * The regular truncate() case without ATTR_CTIME and ATTR_MTIME is a
	 * special case where we need to update the times despite yest having
	 * these flags set.  For all other operations the VFS set these flags
	 * explicitly if it wants a timestamp update.
	 */
	if (newsize != oldsize) {
		iyesde_inc_iversion(iyesde);
		if (!(mask & (ATTR_CTIME | ATTR_MTIME)))
			iyesde->i_ctime = iyesde->i_mtime =
				current_time(iyesde);
	}

	if (newsize > oldsize) {
		/*
		 * Don't do an expanding truncate while snapshotting is ongoing.
		 * This is to ensure the snapshot captures a fully consistent
		 * state of this file - if the snapshot captures this expanding
		 * truncation, it must capture all writes that happened before
		 * this truncation.
		 */
		btrfs_wait_for_snapshot_creation(root);
		ret = btrfs_cont_expand(iyesde, oldsize, newsize);
		if (ret) {
			btrfs_end_write_yes_snapshotting(root);
			return ret;
		}

		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			btrfs_end_write_yes_snapshotting(root);
			return PTR_ERR(trans);
		}

		i_size_write(iyesde, newsize);
		btrfs_ordered_update_i_size(iyesde, i_size_read(iyesde), NULL);
		pagecache_isize_extended(iyesde, oldsize, newsize);
		ret = btrfs_update_iyesde(trans, root, iyesde);
		btrfs_end_write_yes_snapshotting(root);
		btrfs_end_transaction(trans);
	} else {

		/*
		 * We're truncating a file that used to have good data down to
		 * zero. Make sure it gets into the ordered flush list so that
		 * any new writes get down to disk quickly.
		 */
		if (newsize == 0)
			set_bit(BTRFS_INODE_ORDERED_DATA_CLOSE,
				&BTRFS_I(iyesde)->runtime_flags);

		truncate_setsize(iyesde, newsize);

		/* Disable yesnlocked read DIO to avoid the endless truncate */
		btrfs_iyesde_block_unlocked_dio(BTRFS_I(iyesde));
		iyesde_dio_wait(iyesde);
		btrfs_iyesde_resume_unlocked_dio(BTRFS_I(iyesde));

		ret = btrfs_truncate(iyesde, newsize == oldsize);
		if (ret && iyesde->i_nlink) {
			int err;

			/*
			 * Truncate failed, so fix up the in-memory size. We
			 * adjusted disk_i_size down as we removed extents, so
			 * wait for disk_i_size to be stable and then update the
			 * in-memory size to match.
			 */
			err = btrfs_wait_ordered_range(iyesde, 0, (u64)-1);
			if (err)
				return err;
			i_size_write(iyesde, BTRFS_I(iyesde)->disk_i_size);
		}
	}

	return ret;
}

static int btrfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	int err;

	if (btrfs_root_readonly(root))
		return -EROFS;

	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	if (S_ISREG(iyesde->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		err = btrfs_setsize(iyesde, attr);
		if (err)
			return err;
	}

	if (attr->ia_valid) {
		setattr_copy(iyesde, attr);
		iyesde_inc_iversion(iyesde);
		err = btrfs_dirty_iyesde(iyesde);

		if (!err && attr->ia_valid & ATTR_MODE)
			err = posix_acl_chmod(iyesde, iyesde->i_mode);
	}

	return err;
}

/*
 * While truncating the iyesde pages during eviction, we get the VFS calling
 * btrfs_invalidatepage() against each page of the iyesde. This is slow because
 * the calls to btrfs_invalidatepage() result in a huge amount of calls to
 * lock_extent_bits() and clear_extent_bit(), which keep merging and splitting
 * extent_state structures over and over, wasting lots of time.
 *
 * Therefore if the iyesde is being evicted, let btrfs_invalidatepage() skip all
 * those expensive operations on a per page basis and do only the ordered io
 * finishing, while we release here the extent_map and extent_state structures,
 * without the excessive merging and splitting.
 */
static void evict_iyesde_truncate_pages(struct iyesde *iyesde)
{
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct extent_map_tree *map_tree = &BTRFS_I(iyesde)->extent_tree;
	struct rb_yesde *yesde;

	ASSERT(iyesde->i_state & I_FREEING);
	truncate_iyesde_pages_final(&iyesde->i_data);

	write_lock(&map_tree->lock);
	while (!RB_EMPTY_ROOT(&map_tree->map.rb_root)) {
		struct extent_map *em;

		yesde = rb_first_cached(&map_tree->map);
		em = rb_entry(yesde, struct extent_map, rb_yesde);
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
	 * Keep looping until we have yes more ranges in the io tree.
	 * We can have ongoing bios started by readpages (called from readahead)
	 * that have their endio callback (extent_io.c:end_bio_extent_readpage)
	 * still in progress (unlocked the pages in the bio but did yest yet
	 * unlocked the ranges in the io tree). Therefore this means some
	 * ranges can still be locked and eviction started because before
	 * submitting those bios, which are executed by a separate task (work
	 * queue kthread), iyesde references (iyesde->i_count) were yest taken
	 * (which would be dropped in the end io callback of each bio).
	 * Therefore here we effectively end up waiting for those bios and
	 * anyone else holding locked ranges without having bumped the iyesde's
	 * reference count - if we don't do it, when they access the iyesde's
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

		yesde = rb_first(&io_tree->state);
		state = rb_entry(yesde, struct extent_state, rb_yesde);
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
			btrfs_qgroup_free_data(iyesde, NULL, start, end - start + 1);

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
	 * delayed iputs.  However the yesrmal flushing code will run delayed
	 * iputs, so we canyest use FLUSH_ALL otherwise we'll deadlock.
	 *
	 * We reserve the delayed_refs_extra here again because we can't use
	 * btrfs_start_transaction(root, 0) for the same deadlocky reason as
	 * above.  We reserve our extra bit here because we generate a ton of
	 * delayed refs activity by truncating.
	 *
	 * If we canyest make our reservation we'll attempt to steal from the
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
				   "could yest allocate space for delete; will truncate on mount");
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

void btrfs_evict_iyesde(struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_block_rsv *rsv;
	int ret;

	trace_btrfs_iyesde_evict(iyesde);

	if (!root) {
		clear_iyesde(iyesde);
		return;
	}

	evict_iyesde_truncate_pages(iyesde);

	if (iyesde->i_nlink &&
	    ((btrfs_root_refs(&root->root_item) != 0 &&
	      root->root_key.objectid != BTRFS_ROOT_TREE_OBJECTID) ||
	     btrfs_is_free_space_iyesde(BTRFS_I(iyesde))))
		goto yes_delete;

	if (is_bad_iyesde(iyesde))
		goto yes_delete;

	btrfs_free_io_failure_record(BTRFS_I(iyesde), 0, (u64)-1);

	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		goto yes_delete;

	if (iyesde->i_nlink > 0) {
		BUG_ON(btrfs_root_refs(&root->root_item) != 0 &&
		       root->root_key.objectid != BTRFS_ROOT_TREE_OBJECTID);
		goto yes_delete;
	}

	ret = btrfs_commit_iyesde_delayed_iyesde(BTRFS_I(iyesde));
	if (ret)
		goto yes_delete;

	rsv = btrfs_alloc_block_rsv(fs_info, BTRFS_BLOCK_RSV_TEMP);
	if (!rsv)
		goto yes_delete;
	rsv->size = btrfs_calc_metadata_size(fs_info, 1);
	rsv->failfast = 1;

	btrfs_i_size_write(BTRFS_I(iyesde), 0);

	while (1) {
		trans = evict_refill_and_join(root, rsv);
		if (IS_ERR(trans))
			goto free_rsv;

		trans->block_rsv = rsv;

		ret = btrfs_truncate_iyesde_items(trans, root, iyesde, 0, 0);
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
	 * the tree. They will be cleaned up on the next mount. If the iyesde
	 * number gets reused, cleanup deletes the orphan item without doing
	 * anything, and unlink reuses the existing orphan item.
	 *
	 * If it turns out that we are dropping too many of these, we might want
	 * to add a mechanism for retrying these after a commit.
	 */
	trans = evict_refill_and_join(root, rsv);
	if (!IS_ERR(trans)) {
		trans->block_rsv = rsv;
		btrfs_orphan_del(trans, BTRFS_I(iyesde));
		trans->block_rsv = &fs_info->trans_block_rsv;
		btrfs_end_transaction(trans);
	}

	if (!(root == fs_info->tree_root ||
	      root->root_key.objectid == BTRFS_TREE_RELOC_OBJECTID))
		btrfs_return_iyes(root, btrfs_iyes(BTRFS_I(iyesde)));

free_rsv:
	btrfs_free_block_rsv(fs_info, rsv);
yes_delete:
	/*
	 * If we didn't successfully delete, the orphan item will still be in
	 * the tree and we'll retry on the next mount. Again, we might also want
	 * to retry these periodically in the future.
	 */
	btrfs_remove_delayed_yesde(BTRFS_I(iyesde));
	clear_iyesde(iyesde);
}

/*
 * Return the key found in the dir entry in the location pointer, fill @type
 * with BTRFS_FT_*, and return 0.
 *
 * If yes dir entries were found, returns -ENOENT.
 * If found a corrupted location in dir entry, returns -EUCLEAN.
 */
static int btrfs_iyesde_by_name(struct iyesde *dir, struct dentry *dentry,
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

	di = btrfs_lookup_dir_item(NULL, root, path, btrfs_iyes(BTRFS_I(dir)),
			name, namelen, 0);
	if (IS_ERR_OR_NULL(di)) {
		ret = di ? PTR_ERR(di) : -ENOENT;
		goto out;
	}

	btrfs_dir_item_key_to_cpu(path->yesdes[0], di, location);
	if (location->type != BTRFS_INODE_ITEM_KEY &&
	    location->type != BTRFS_ROOT_ITEM_KEY) {
		ret = -EUCLEAN;
		btrfs_warn(root->fs_info,
"%s gets something invalid in DIR_ITEM (name %s, directory iyes %llu, location(%llu %u %llu))",
			   __func__, name, btrfs_iyes(BTRFS_I(dir)),
			   location->objectid, location->type, location->offset);
	}
	if (!ret)
		*type = btrfs_dir_type(path->yesdes[0], di);
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * when we hit a tree root in a directory, the btrfs part of the iyesde
 * needs to be changed to reflect the root directory of the tree root.  This
 * is kind of like crossing a mount point.
 */
static int fixup_tree_root_location(struct btrfs_fs_info *fs_info,
				    struct iyesde *dir,
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

	leaf = path->yesdes[0];
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_ref);
	if (btrfs_root_ref_dirid(leaf, ref) != btrfs_iyes(BTRFS_I(dir)) ||
	    btrfs_root_ref_name_len(leaf, ref) != dentry->d_name.len)
		goto out;

	ret = memcmp_extent_buffer(leaf, dentry->d_name.name,
				   (unsigned long)(ref + 1),
				   dentry->d_name.len);
	if (ret)
		goto out;

	btrfs_release_path(path);

	new_root = btrfs_read_fs_root_yes_name(fs_info, location);
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

static void iyesde_tree_add(struct iyesde *iyesde)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_iyesde *entry;
	struct rb_yesde **p;
	struct rb_yesde *parent;
	struct rb_yesde *new = &BTRFS_I(iyesde)->rb_yesde;
	u64 iyes = btrfs_iyes(BTRFS_I(iyesde));

	if (iyesde_unhashed(iyesde))
		return;
	parent = NULL;
	spin_lock(&root->iyesde_lock);
	p = &root->iyesde_tree.rb_yesde;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_iyesde, rb_yesde);

		if (iyes < btrfs_iyes(entry))
			p = &parent->rb_left;
		else if (iyes > btrfs_iyes(entry))
			p = &parent->rb_right;
		else {
			WARN_ON(!(entry->vfs_iyesde.i_state &
				  (I_WILL_FREE | I_FREEING)));
			rb_replace_yesde(parent, new, &root->iyesde_tree);
			RB_CLEAR_NODE(parent);
			spin_unlock(&root->iyesde_lock);
			return;
		}
	}
	rb_link_yesde(new, parent, p);
	rb_insert_color(new, &root->iyesde_tree);
	spin_unlock(&root->iyesde_lock);
}

static void iyesde_tree_del(struct iyesde *iyesde)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	int empty = 0;

	spin_lock(&root->iyesde_lock);
	if (!RB_EMPTY_NODE(&BTRFS_I(iyesde)->rb_yesde)) {
		rb_erase(&BTRFS_I(iyesde)->rb_yesde, &root->iyesde_tree);
		RB_CLEAR_NODE(&BTRFS_I(iyesde)->rb_yesde);
		empty = RB_EMPTY_ROOT(&root->iyesde_tree);
	}
	spin_unlock(&root->iyesde_lock);

	if (empty && btrfs_root_refs(&root->root_item) == 0) {
		spin_lock(&root->iyesde_lock);
		empty = RB_EMPTY_ROOT(&root->iyesde_tree);
		spin_unlock(&root->iyesde_lock);
		if (empty)
			btrfs_add_dead_root(root);
	}
}


static int btrfs_init_locked_iyesde(struct iyesde *iyesde, void *p)
{
	struct btrfs_iget_args *args = p;
	iyesde->i_iyes = args->location->objectid;
	memcpy(&BTRFS_I(iyesde)->location, args->location,
	       sizeof(*args->location));
	BTRFS_I(iyesde)->root = args->root;
	return 0;
}

static int btrfs_find_actor(struct iyesde *iyesde, void *opaque)
{
	struct btrfs_iget_args *args = opaque;
	return args->location->objectid == BTRFS_I(iyesde)->location.objectid &&
		args->root == BTRFS_I(iyesde)->root;
}

static struct iyesde *btrfs_iget_locked(struct super_block *s,
				       struct btrfs_key *location,
				       struct btrfs_root *root)
{
	struct iyesde *iyesde;
	struct btrfs_iget_args args;
	unsigned long hashval = btrfs_iyesde_hash(location->objectid, root);

	args.location = location;
	args.root = root;

	iyesde = iget5_locked(s, hashval, btrfs_find_actor,
			     btrfs_init_locked_iyesde,
			     (void *)&args);
	return iyesde;
}

/*
 * Get an iyesde object given its location and corresponding root.
 * Path can be preallocated to prevent recursing back to iget through
 * allocator. NULL is also valid but may require an additional allocation
 * later.
 */
struct iyesde *btrfs_iget_path(struct super_block *s, struct btrfs_key *location,
			      struct btrfs_root *root, struct btrfs_path *path)
{
	struct iyesde *iyesde;

	iyesde = btrfs_iget_locked(s, location, root);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	if (iyesde->i_state & I_NEW) {
		int ret;

		ret = btrfs_read_locked_iyesde(iyesde, path);
		if (!ret) {
			iyesde_tree_add(iyesde);
			unlock_new_iyesde(iyesde);
		} else {
			iget_failed(iyesde);
			/*
			 * ret > 0 can come from btrfs_search_slot called by
			 * btrfs_read_locked_iyesde, this means the iyesde item
			 * was yest found.
			 */
			if (ret > 0)
				ret = -ENOENT;
			iyesde = ERR_PTR(ret);
		}
	}

	return iyesde;
}

struct iyesde *btrfs_iget(struct super_block *s, struct btrfs_key *location,
			 struct btrfs_root *root)
{
	return btrfs_iget_path(s, location, root, NULL);
}

static struct iyesde *new_simple_dir(struct super_block *s,
				    struct btrfs_key *key,
				    struct btrfs_root *root)
{
	struct iyesde *iyesde = new_iyesde(s);

	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	BTRFS_I(iyesde)->root = root;
	memcpy(&BTRFS_I(iyesde)->location, key, sizeof(*key));
	set_bit(BTRFS_INODE_DUMMY, &BTRFS_I(iyesde)->runtime_flags);

	iyesde->i_iyes = BTRFS_EMPTY_SUBVOL_DIR_OBJECTID;
	iyesde->i_op = &btrfs_dir_ro_iyesde_operations;
	iyesde->i_opflags &= ~IOP_XATTR;
	iyesde->i_fop = &simple_dir_operations;
	iyesde->i_mode = S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO;
	iyesde->i_mtime = current_time(iyesde);
	iyesde->i_atime = iyesde->i_mtime;
	iyesde->i_ctime = iyesde->i_mtime;
	BTRFS_I(iyesde)->i_otime = iyesde->i_mtime;

	return iyesde;
}

static inline u8 btrfs_iyesde_type(struct iyesde *iyesde)
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

	return fs_umode_to_ftype(iyesde->i_mode);
}

struct iyesde *btrfs_lookup_dentry(struct iyesde *dir, struct dentry *dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct iyesde *iyesde;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location;
	u8 di_type = 0;
	int index;
	int ret = 0;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_iyesde_by_name(dir, dentry, &location, &di_type);
	if (ret < 0)
		return ERR_PTR(ret);

	if (location.type == BTRFS_INODE_ITEM_KEY) {
		iyesde = btrfs_iget(dir->i_sb, &location, root);
		if (IS_ERR(iyesde))
			return iyesde;

		/* Do extra check against iyesde mode with di_type */
		if (btrfs_iyesde_type(iyesde) != di_type) {
			btrfs_crit(fs_info,
"iyesde mode mismatch with dir: iyesde mode=0%o btrfs type=%u dir type=%u",
				  iyesde->i_mode, btrfs_iyesde_type(iyesde),
				  di_type);
			iput(iyesde);
			return ERR_PTR(-EUCLEAN);
		}
		return iyesde;
	}

	index = srcu_read_lock(&fs_info->subvol_srcu);
	ret = fixup_tree_root_location(fs_info, dir, dentry,
				       &location, &sub_root);
	if (ret < 0) {
		if (ret != -ENOENT)
			iyesde = ERR_PTR(ret);
		else
			iyesde = new_simple_dir(dir->i_sb, &location, sub_root);
	} else {
		iyesde = btrfs_iget(dir->i_sb, &location, sub_root);
	}
	srcu_read_unlock(&fs_info->subvol_srcu, index);

	if (!IS_ERR(iyesde) && root != sub_root) {
		down_read(&fs_info->cleanup_work_sem);
		if (!sb_rdonly(iyesde->i_sb))
			ret = btrfs_orphan_cleanup(sub_root);
		up_read(&fs_info->cleanup_work_sem);
		if (ret) {
			iput(iyesde);
			iyesde = ERR_PTR(ret);
		}
	}

	return iyesde;
}

static int btrfs_dentry_delete(const struct dentry *dentry)
{
	struct btrfs_root *root;
	struct iyesde *iyesde = d_iyesde(dentry);

	if (!iyesde && !IS_ROOT(dentry))
		iyesde = d_iyesde(dentry->d_parent);

	if (iyesde) {
		root = BTRFS_I(iyesde)->root;
		if (btrfs_root_refs(&root->root_item) == 0)
			return 1;

		if (btrfs_iyes(BTRFS_I(iyesde)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)
			return 1;
	}
	return 0;
}

static struct dentry *btrfs_lookup(struct iyesde *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct iyesde *iyesde = btrfs_lookup_dentry(dir, dentry);

	if (iyesde == ERR_PTR(-ENOENT))
		iyesde = NULL;
	return d_splice_alias(iyesde, dentry);
}

/*
 * All this infrastructure exists because dir_emit can fault, and we are holding
 * the tree lock when doing readdir.  For yesw just allocate a buffer and copy
 * our information into that, and then dir_emit from the buffer.  This is
 * similar to what NFS does, only we don't keep the buffer around in pagecache
 * because I'm afraid I'll mess that up.  Long term we need to make filldir do
 * copy_to_user_inatomic so we don't have to worry about page faulting under the
 * tree lock.
 */
static int btrfs_opendir(struct iyesde *iyesde, struct file *file)
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
	u64 iyes;
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
					 get_unaligned(&entry->iyes),
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
	struct iyesde *iyesde = file_iyesde(file);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
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
	put = btrfs_readdir_get_delayed_items(iyesde, &ins_list, &del_list);

again:
	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = ctx->pos;
	key.objectid = btrfs_iyes(BTRFS_I(iyesde));

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	while (1) {
		struct dir_entry *entry;

		leaf = path->yesdes[0];
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
				goto yespos;
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
		put_unaligned(location.objectid, &entry->iyes);
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
		goto yespos;

	ret = btrfs_readdir_delayed_dir_index(ctx, &ins_list);
	if (ret)
		goto yespos;

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
	 * This is being careful yest to overflow 32bit loff_t unless the
	 * last entry requires it because doing so has broken 32bit apps
	 * in the past.
	 */
	if (ctx->pos >= INT_MAX)
		ctx->pos = LLONG_MAX;
	else
		ctx->pos = INT_MAX;
yespos:
	ret = 0;
err:
	if (put)
		btrfs_readdir_put_delayed_items(iyesde, &ins_list, &del_list);
	btrfs_free_path(path);
	return ret;
}

/*
 * This is somewhat expensive, updating the tree every time the
 * iyesde changes.  But, it is most likely to find the iyesde in cache.
 * FIXME, needs more benchmarking...there are yes reasons other than performance
 * to keep or drop this code.
 */
static int btrfs_dirty_iyesde(struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_trans_handle *trans;
	int ret;

	if (test_bit(BTRFS_INODE_DUMMY, &BTRFS_I(iyesde)->runtime_flags))
		return 0;

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_update_iyesde(trans, root, iyesde);
	if (ret && ret == -ENOSPC) {
		/* whoops, lets try again with the full transaction */
		btrfs_end_transaction(trans);
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_update_iyesde(trans, root, iyesde);
	}
	btrfs_end_transaction(trans);
	if (BTRFS_I(iyesde)->delayed_yesde)
		btrfs_balance_delayed_items(fs_info);

	return ret;
}

/*
 * This is a copy of file_update_time.  We need this so we can return error on
 * ENOSPC for updating the iyesde in the case of file write and mmap writes.
 */
static int btrfs_update_time(struct iyesde *iyesde, struct timespec64 *yesw,
			     int flags)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	bool dirty = flags & ~S_VERSION;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (flags & S_VERSION)
		dirty |= iyesde_maybe_inc_iversion(iyesde, dirty);
	if (flags & S_CTIME)
		iyesde->i_ctime = *yesw;
	if (flags & S_MTIME)
		iyesde->i_mtime = *yesw;
	if (flags & S_ATIME)
		iyesde->i_atime = *yesw;
	return dirty ? btrfs_dirty_iyesde(iyesde) : 0;
}

/*
 * find the highest existing sequence number in a directory
 * and then set the in-memory index_cnt variable to reflect
 * free sequence numbers
 */
static int btrfs_set_iyesde_index_count(struct btrfs_iyesde *iyesde)
{
	struct btrfs_root *root = iyesde->root;
	struct btrfs_key key, found_key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = btrfs_iyes(iyesde);
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
		iyesde->index_cnt = 2;
		goto out;
	}

	path->slots[0]--;

	leaf = path->yesdes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	if (found_key.objectid != btrfs_iyes(iyesde) ||
	    found_key.type != BTRFS_DIR_INDEX_KEY) {
		iyesde->index_cnt = 2;
		goto out;
	}

	iyesde->index_cnt = found_key.offset + 1;
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * helper to find a free sequence number in a given directory.  This current
 * code is very simple, later versions will do smarter things in the btree
 */
int btrfs_set_iyesde_index(struct btrfs_iyesde *dir, u64 *index)
{
	int ret = 0;

	if (dir->index_cnt == (u64)-1) {
		ret = btrfs_iyesde_delayed_dir_index_count(dir);
		if (ret) {
			ret = btrfs_set_iyesde_index_count(dir);
			if (ret)
				return ret;
		}
	}

	*index = dir->index_cnt;
	dir->index_cnt++;

	return ret;
}

static int btrfs_insert_iyesde_locked(struct iyesde *iyesde)
{
	struct btrfs_iget_args args;
	args.location = &BTRFS_I(iyesde)->location;
	args.root = BTRFS_I(iyesde)->root;

	return insert_iyesde_locked4(iyesde,
		   btrfs_iyesde_hash(iyesde->i_iyes, BTRFS_I(iyesde)->root),
		   btrfs_find_actor, &args);
}

/*
 * Inherit flags from the parent iyesde.
 *
 * Currently only the compression flags and the cow flags are inherited.
 */
static void btrfs_inherit_iflags(struct iyesde *iyesde, struct iyesde *dir)
{
	unsigned int flags;

	if (!dir)
		return;

	flags = BTRFS_I(dir)->flags;

	if (flags & BTRFS_INODE_NOCOMPRESS) {
		BTRFS_I(iyesde)->flags &= ~BTRFS_INODE_COMPRESS;
		BTRFS_I(iyesde)->flags |= BTRFS_INODE_NOCOMPRESS;
	} else if (flags & BTRFS_INODE_COMPRESS) {
		BTRFS_I(iyesde)->flags &= ~BTRFS_INODE_NOCOMPRESS;
		BTRFS_I(iyesde)->flags |= BTRFS_INODE_COMPRESS;
	}

	if (flags & BTRFS_INODE_NODATACOW) {
		BTRFS_I(iyesde)->flags |= BTRFS_INODE_NODATACOW;
		if (S_ISREG(iyesde->i_mode))
			BTRFS_I(iyesde)->flags |= BTRFS_INODE_NODATASUM;
	}

	btrfs_sync_iyesde_flags_to_i_flags(iyesde);
}

static struct iyesde *btrfs_new_iyesde(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct iyesde *dir,
				     const char *name, int name_len,
				     u64 ref_objectid, u64 objectid,
				     umode_t mode, u64 *index)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct iyesde *iyesde;
	struct btrfs_iyesde_item *iyesde_item;
	struct btrfs_key *location;
	struct btrfs_path *path;
	struct btrfs_iyesde_ref *ref;
	struct btrfs_key key[2];
	u32 sizes[2];
	int nitems = name ? 2 : 1;
	unsigned long ptr;
	unsigned int yesfs_flag;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);

	yesfs_flag = memalloc_yesfs_save();
	iyesde = new_iyesde(fs_info->sb);
	memalloc_yesfs_restore(yesfs_flag);
	if (!iyesde) {
		btrfs_free_path(path);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * O_TMPFILE, set link count to 0, so that after this point,
	 * we fill in an iyesde item with the correct link count.
	 */
	if (!name)
		set_nlink(iyesde, 0);

	/*
	 * we have to initialize this early, so we can reclaim the iyesde
	 * number if we fail afterwards in this function.
	 */
	iyesde->i_iyes = objectid;

	if (dir && name) {
		trace_btrfs_iyesde_request(dir);

		ret = btrfs_set_iyesde_index(BTRFS_I(dir), index);
		if (ret) {
			btrfs_free_path(path);
			iput(iyesde);
			return ERR_PTR(ret);
		}
	} else if (dir) {
		*index = 0;
	}
	/*
	 * index_cnt is igyesred for everything but a dir,
	 * btrfs_set_iyesde_index_count has an explanation for the magic
	 * number
	 */
	BTRFS_I(iyesde)->index_cnt = 2;
	BTRFS_I(iyesde)->dir_index = *index;
	BTRFS_I(iyesde)->root = root;
	BTRFS_I(iyesde)->generation = trans->transid;
	iyesde->i_generation = BTRFS_I(iyesde)->generation;

	/*
	 * We could have gotten an iyesde number from somebody who was fsynced
	 * and then removed in this same transaction, so let's just set full
	 * sync since it will be a full sync anyway and this will blow away the
	 * old info in the log.
	 */
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(iyesde)->runtime_flags);

	key[0].objectid = objectid;
	key[0].type = BTRFS_INODE_ITEM_KEY;
	key[0].offset = 0;

	sizes[0] = sizeof(struct btrfs_iyesde_item);

	if (name) {
		/*
		 * Start new iyesdes with an iyesde_ref. This is slightly more
		 * efficient for small numbers of hard links since they will
		 * be packed into one item. Extended refs will kick in if we
		 * add more hard links than can fit in the ref item.
		 */
		key[1].objectid = objectid;
		key[1].type = BTRFS_INODE_REF_KEY;
		key[1].offset = ref_objectid;

		sizes[1] = name_len + sizeof(*ref);
	}

	location = &BTRFS_I(iyesde)->location;
	location->objectid = objectid;
	location->offset = 0;
	location->type = BTRFS_INODE_ITEM_KEY;

	ret = btrfs_insert_iyesde_locked(iyesde);
	if (ret < 0) {
		iput(iyesde);
		goto fail;
	}

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_items(trans, root, path, key, sizes, nitems);
	if (ret != 0)
		goto fail_unlock;

	iyesde_init_owner(iyesde, dir, mode);
	iyesde_set_bytes(iyesde, 0);

	iyesde->i_mtime = current_time(iyesde);
	iyesde->i_atime = iyesde->i_mtime;
	iyesde->i_ctime = iyesde->i_mtime;
	BTRFS_I(iyesde)->i_otime = iyesde->i_mtime;

	iyesde_item = btrfs_item_ptr(path->yesdes[0], path->slots[0],
				  struct btrfs_iyesde_item);
	memzero_extent_buffer(path->yesdes[0], (unsigned long)iyesde_item,
			     sizeof(*iyesde_item));
	fill_iyesde_item(trans, path->yesdes[0], iyesde_item, iyesde);

	if (name) {
		ref = btrfs_item_ptr(path->yesdes[0], path->slots[0] + 1,
				     struct btrfs_iyesde_ref);
		btrfs_set_iyesde_ref_name_len(path->yesdes[0], ref, name_len);
		btrfs_set_iyesde_ref_index(path->yesdes[0], ref, *index);
		ptr = (unsigned long)(ref + 1);
		write_extent_buffer(path->yesdes[0], name, ptr, name_len);
	}

	btrfs_mark_buffer_dirty(path->yesdes[0]);
	btrfs_free_path(path);

	btrfs_inherit_iflags(iyesde, dir);

	if (S_ISREG(mode)) {
		if (btrfs_test_opt(fs_info, NODATASUM))
			BTRFS_I(iyesde)->flags |= BTRFS_INODE_NODATASUM;
		if (btrfs_test_opt(fs_info, NODATACOW))
			BTRFS_I(iyesde)->flags |= BTRFS_INODE_NODATACOW |
				BTRFS_INODE_NODATASUM;
	}

	iyesde_tree_add(iyesde);

	trace_btrfs_iyesde_new(iyesde);
	btrfs_set_iyesde_last_trans(trans, iyesde);

	btrfs_update_root_times(trans, root);

	ret = btrfs_iyesde_inherit_props(trans, iyesde, dir);
	if (ret)
		btrfs_err(fs_info,
			  "error inheriting props for iyes %llu (root %llu): %d",
			btrfs_iyes(BTRFS_I(iyesde)), root->root_key.objectid, ret);

	return iyesde;

fail_unlock:
	discard_new_iyesde(iyesde);
fail:
	if (dir && name)
		BTRFS_I(dir)->index_cnt--;
	btrfs_free_path(path);
	return ERR_PTR(ret);
}

/*
 * utility function to add 'iyesde' into 'parent_iyesde' with
 * a give name and a given sequence number.
 * if 'add_backref' is true, also insert a backref from the
 * iyesde to the parent directory.
 */
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct btrfs_iyesde *parent_iyesde, struct btrfs_iyesde *iyesde,
		   const char *name, int name_len, int add_backref, u64 index)
{
	int ret = 0;
	struct btrfs_key key;
	struct btrfs_root *root = parent_iyesde->root;
	u64 iyes = btrfs_iyes(iyesde);
	u64 parent_iyes = btrfs_iyes(parent_iyesde);

	if (unlikely(iyes == BTRFS_FIRST_FREE_OBJECTID)) {
		memcpy(&key, &iyesde->root->root_key, sizeof(key));
	} else {
		key.objectid = iyes;
		key.type = BTRFS_INODE_ITEM_KEY;
		key.offset = 0;
	}

	if (unlikely(iyes == BTRFS_FIRST_FREE_OBJECTID)) {
		ret = btrfs_add_root_ref(trans, key.objectid,
					 root->root_key.objectid, parent_iyes,
					 index, name, name_len);
	} else if (add_backref) {
		ret = btrfs_insert_iyesde_ref(trans, root, name, name_len, iyes,
					     parent_iyes, index);
	}

	/* Nothing to clean up yet */
	if (ret)
		return ret;

	ret = btrfs_insert_dir_item(trans, name, name_len, parent_iyesde, &key,
				    btrfs_iyesde_type(&iyesde->vfs_iyesde), index);
	if (ret == -EEXIST || ret == -EOVERFLOW)
		goto fail_dir_item;
	else if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	btrfs_i_size_write(parent_iyesde, parent_iyesde->vfs_iyesde.i_size +
			   name_len * 2);
	iyesde_inc_iversion(&parent_iyesde->vfs_iyesde);
	/*
	 * If we are replaying a log tree, we do yest want to update the mtime
	 * and ctime of the parent directory with the current time, since the
	 * log replay procedure is responsible for setting them to their correct
	 * values (the ones it had when the fsync was done).
	 */
	if (!test_bit(BTRFS_FS_LOG_RECOVERING, &root->fs_info->flags)) {
		struct timespec64 yesw = current_time(&parent_iyesde->vfs_iyesde);

		parent_iyesde->vfs_iyesde.i_mtime = yesw;
		parent_iyesde->vfs_iyesde.i_ctime = yesw;
	}
	ret = btrfs_update_iyesde(trans, root, &parent_iyesde->vfs_iyesde);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;

fail_dir_item:
	if (unlikely(iyes == BTRFS_FIRST_FREE_OBJECTID)) {
		u64 local_index;
		int err;
		err = btrfs_del_root_ref(trans, key.objectid,
					 root->root_key.objectid, parent_iyes,
					 &local_index, name, name_len);
		if (err)
			btrfs_abort_transaction(trans, err);
	} else if (add_backref) {
		u64 local_index;
		int err;

		err = btrfs_del_iyesde_ref(trans, root, name, name_len,
					  iyes, parent_iyes, &local_index);
		if (err)
			btrfs_abort_transaction(trans, err);
	}

	/* Return the original error code */
	return ret;
}

static int btrfs_add_yesndir(struct btrfs_trans_handle *trans,
			    struct btrfs_iyesde *dir, struct dentry *dentry,
			    struct btrfs_iyesde *iyesde, int backref, u64 index)
{
	int err = btrfs_add_link(trans, dir, iyesde,
				 dentry->d_name.name, dentry->d_name.len,
				 backref, index);
	if (err > 0)
		err = -EEXIST;
	return err;
}

static int btrfs_mkyesd(struct iyesde *dir, struct dentry *dentry,
			umode_t mode, dev_t rdev)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct iyesde *iyesde = NULL;
	int err;
	u64 objectid;
	u64 index = 0;

	/*
	 * 2 for iyesde item and ref
	 * 2 for dir items
	 * 1 for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_iyes(root, &objectid);
	if (err)
		goto out_unlock;

	iyesde = btrfs_new_iyesde(trans, root, dir, dentry->d_name.name,
			dentry->d_name.len, btrfs_iyes(BTRFS_I(dir)), objectid,
			mode, &index);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		iyesde = NULL;
		goto out_unlock;
	}

	/*
	* If the active LSM wants to access the iyesde during
	* d_instantiate it needs these. Smack checks to see
	* if the filesystem supports xattrs by looking at the
	* ops vector.
	*/
	iyesde->i_op = &btrfs_special_iyesde_operations;
	init_special_iyesde(iyesde, iyesde->i_mode, rdev);

	err = btrfs_init_iyesde_security(trans, iyesde, dir, &dentry->d_name);
	if (err)
		goto out_unlock;

	err = btrfs_add_yesndir(trans, BTRFS_I(dir), dentry, BTRFS_I(iyesde),
			0, index);
	if (err)
		goto out_unlock;

	btrfs_update_iyesde(trans, root, iyesde);
	d_instantiate_new(dentry, iyesde);

out_unlock:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
	if (err && iyesde) {
		iyesde_dec_link_count(iyesde);
		discard_new_iyesde(iyesde);
	}
	return err;
}

static int btrfs_create(struct iyesde *dir, struct dentry *dentry,
			umode_t mode, bool excl)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct iyesde *iyesde = NULL;
	int err;
	u64 objectid;
	u64 index = 0;

	/*
	 * 2 for iyesde item and ref
	 * 2 for dir items
	 * 1 for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_iyes(root, &objectid);
	if (err)
		goto out_unlock;

	iyesde = btrfs_new_iyesde(trans, root, dir, dentry->d_name.name,
			dentry->d_name.len, btrfs_iyes(BTRFS_I(dir)), objectid,
			mode, &index);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		iyesde = NULL;
		goto out_unlock;
	}
	/*
	* If the active LSM wants to access the iyesde during
	* d_instantiate it needs these. Smack checks to see
	* if the filesystem supports xattrs by looking at the
	* ops vector.
	*/
	iyesde->i_fop = &btrfs_file_operations;
	iyesde->i_op = &btrfs_file_iyesde_operations;
	iyesde->i_mapping->a_ops = &btrfs_aops;

	err = btrfs_init_iyesde_security(trans, iyesde, dir, &dentry->d_name);
	if (err)
		goto out_unlock;

	err = btrfs_update_iyesde(trans, root, iyesde);
	if (err)
		goto out_unlock;

	err = btrfs_add_yesndir(trans, BTRFS_I(dir), dentry, BTRFS_I(iyesde),
			0, index);
	if (err)
		goto out_unlock;

	BTRFS_I(iyesde)->io_tree.ops = &btrfs_extent_io_ops;
	d_instantiate_new(dentry, iyesde);

out_unlock:
	btrfs_end_transaction(trans);
	if (err && iyesde) {
		iyesde_dec_link_count(iyesde);
		discard_new_iyesde(iyesde);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static int btrfs_link(struct dentry *old_dentry, struct iyesde *dir,
		      struct dentry *dentry)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct iyesde *iyesde = d_iyesde(old_dentry);
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	u64 index;
	int err;
	int drop_iyesde = 0;

	/* do yest allow sys_link's with other subvols of the same device */
	if (root->root_key.objectid != BTRFS_I(iyesde)->root->root_key.objectid)
		return -EXDEV;

	if (iyesde->i_nlink >= BTRFS_LINK_MAX)
		return -EMLINK;

	err = btrfs_set_iyesde_index(BTRFS_I(dir), &index);
	if (err)
		goto fail;

	/*
	 * 2 items for iyesde and iyesde ref
	 * 2 items for dir items
	 * 1 item for parent iyesde
	 * 1 item for orphan item deletion if O_TMPFILE
	 */
	trans = btrfs_start_transaction(root, iyesde->i_nlink ? 5 : 6);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		trans = NULL;
		goto fail;
	}

	/* There are several dir indexes for this iyesde, clear the cache. */
	BTRFS_I(iyesde)->dir_index = 0ULL;
	inc_nlink(iyesde);
	iyesde_inc_iversion(iyesde);
	iyesde->i_ctime = current_time(iyesde);
	ihold(iyesde);
	set_bit(BTRFS_INODE_COPY_EVERYTHING, &BTRFS_I(iyesde)->runtime_flags);

	err = btrfs_add_yesndir(trans, BTRFS_I(dir), dentry, BTRFS_I(iyesde),
			1, index);

	if (err) {
		drop_iyesde = 1;
	} else {
		struct dentry *parent = dentry->d_parent;
		int ret;

		err = btrfs_update_iyesde(trans, root, iyesde);
		if (err)
			goto fail;
		if (iyesde->i_nlink == 1) {
			/*
			 * If new hard link count is 1, it's a file created
			 * with open(2) O_TMPFILE flag.
			 */
			err = btrfs_orphan_del(trans, BTRFS_I(iyesde));
			if (err)
				goto fail;
		}
		d_instantiate(dentry, iyesde);
		ret = btrfs_log_new_name(trans, BTRFS_I(iyesde), NULL, parent,
					 true, NULL);
		if (ret == BTRFS_NEED_TRANS_COMMIT) {
			err = btrfs_commit_transaction(trans);
			trans = NULL;
		}
	}

fail:
	if (trans)
		btrfs_end_transaction(trans);
	if (drop_iyesde) {
		iyesde_dec_link_count(iyesde);
		iput(iyesde);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static int btrfs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct iyesde *iyesde = NULL;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int err = 0;
	u64 objectid = 0;
	u64 index = 0;

	/*
	 * 2 items for iyesde and ref
	 * 2 items for dir items
	 * 1 for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_iyes(root, &objectid);
	if (err)
		goto out_fail;

	iyesde = btrfs_new_iyesde(trans, root, dir, dentry->d_name.name,
			dentry->d_name.len, btrfs_iyes(BTRFS_I(dir)), objectid,
			S_IFDIR | mode, &index);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		iyesde = NULL;
		goto out_fail;
	}

	/* these must be set before we unlock the iyesde */
	iyesde->i_op = &btrfs_dir_iyesde_operations;
	iyesde->i_fop = &btrfs_dir_file_operations;

	err = btrfs_init_iyesde_security(trans, iyesde, dir, &dentry->d_name);
	if (err)
		goto out_fail;

	btrfs_i_size_write(BTRFS_I(iyesde), 0);
	err = btrfs_update_iyesde(trans, root, iyesde);
	if (err)
		goto out_fail;

	err = btrfs_add_link(trans, BTRFS_I(dir), BTRFS_I(iyesde),
			dentry->d_name.name,
			dentry->d_name.len, 0, index);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, iyesde);

out_fail:
	btrfs_end_transaction(trans);
	if (err && iyesde) {
		iyesde_dec_link_count(iyesde);
		discard_new_iyesde(iyesde);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static yesinline int uncompress_inline(struct btrfs_path *path,
				      struct page *page,
				      size_t pg_offset, u64 extent_offset,
				      struct btrfs_file_extent_item *item)
{
	int ret;
	struct extent_buffer *leaf = path->yesdes[0];
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

	if (max_size + pg_offset < PAGE_SIZE) {
		char *map = kmap(page);
		memset(map + pg_offset + max_size, 0, PAGE_SIZE - max_size - pg_offset);
		kunmap(page);
	}
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
struct extent_map *btrfs_get_extent(struct btrfs_iyesde *iyesde,
				    struct page *page,
				    size_t pg_offset, u64 start, u64 len,
				    int create)
{
	struct btrfs_fs_info *fs_info = iyesde->root->fs_info;
	int ret;
	int err = 0;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = btrfs_iyes(iyesde);
	int extent_type = -1;
	struct btrfs_path *path = NULL;
	struct btrfs_root *root = iyesde->root;
	struct btrfs_file_extent_item *item;
	struct extent_buffer *leaf;
	struct btrfs_key found_key;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &iyesde->extent_tree;
	struct extent_io_tree *io_tree = &iyesde->io_tree;
	const bool new_inline = !page || create;

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
		err = -ENOMEM;
		goto out;
	}
	em->start = EXTENT_MAP_HOLE;
	em->orig_start = EXTENT_MAP_HOLE;
	em->len = (u64)-1;
	em->block_len = (u64)-1;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out;
	}

	/* Chances are we'll be called again, so go ahead and do readahead */
	path->reada = READA_FORWARD;

	/*
	 * Unless we're going to uncompress the inline extent, yes sleep would
	 * happen.
	 */
	path->leave_spinning = 1;

	ret = btrfs_lookup_file_extent(NULL, root, path, objectid, start, 0);
	if (ret < 0) {
		err = ret;
		goto out;
	} else if (ret > 0) {
		if (path->slots[0] == 0)
			goto yest_found;
		path->slots[0]--;
	}

	leaf = path->yesdes[0];
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
	if (extent_type == BTRFS_FILE_EXTENT_REG ||
	    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
		/* Only regular file could have regular/prealloc extent */
		if (!S_ISREG(iyesde->vfs_iyesde.i_mode)) {
			ret = -EUCLEAN;
			btrfs_crit(fs_info,
		"regular/prealloc extent found for yesn-regular iyesde %llu",
				   btrfs_iyes(iyesde));
			goto out;
		}
		extent_end = extent_start +
		       btrfs_file_extent_num_bytes(leaf, item);

		trace_btrfs_get_extent_show_fi_regular(iyesde, leaf, item,
						       extent_start);
	} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		size_t size;

		size = btrfs_file_extent_ram_bytes(leaf, item);
		extent_end = ALIGN(extent_start + size,
				   fs_info->sectorsize);

		trace_btrfs_get_extent_show_fi_inline(iyesde, leaf, item,
						      path->slots[0],
						      extent_start);
	}
next:
	if (start >= extent_end) {
		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				err = ret;
				goto out;
			} else if (ret > 0) {
				goto yest_found;
			}
			leaf = path->yesdes[0];
		}
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != objectid ||
		    found_key.type != BTRFS_EXTENT_DATA_KEY)
			goto yest_found;
		if (start + len <= found_key.offset)
			goto yest_found;
		if (start > found_key.offset)
			goto next;

		/* New extent overlaps with existing one */
		em->start = start;
		em->orig_start = start;
		em->len = found_key.offset - start;
		em->block_start = EXTENT_MAP_HOLE;
		goto insert;
	}

	btrfs_extent_item_to_extent_map(iyesde, path, item,
			new_inline, em);

	if (extent_type == BTRFS_FILE_EXTENT_REG ||
	    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
		goto insert;
	} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		unsigned long ptr;
		char *map;
		size_t size;
		size_t extent_offset;
		size_t copy_size;

		if (new_inline)
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

		btrfs_set_path_blocking(path);
		if (!PageUptodate(page)) {
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
		}
		set_extent_uptodate(io_tree, em->start,
				    extent_map_end(em) - 1, NULL, GFP_NOFS);
		goto insert;
	}
yest_found:
	em->start = start;
	em->orig_start = start;
	em->len = len;
	em->block_start = EXTENT_MAP_HOLE;
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
	err = btrfs_add_extent_mapping(fs_info, em_tree, &em, start, len);
	write_unlock(&em_tree->lock);
out:
	btrfs_free_path(path);

	trace_btrfs_get_extent(root, iyesde, em);

	if (err) {
		free_extent_map(em);
		return ERR_PTR(err);
	}
	BUG_ON(!em); /* Error is always set */
	return em;
}

struct extent_map *btrfs_get_extent_fiemap(struct btrfs_iyesde *iyesde,
					   u64 start, u64 len)
{
	struct extent_map *em;
	struct extent_map *hole_em = NULL;
	u64 delalloc_start = start;
	u64 end;
	u64 delalloc_len;
	u64 delalloc_end;
	int err = 0;

	em = btrfs_get_extent(iyesde, NULL, 0, start, len, 0);
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
	delalloc_len = count_range_bits(&iyesde->io_tree, &delalloc_start,
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

static struct extent_map *btrfs_create_dio_extent(struct iyesde *iyesde,
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
		em = create_io_em(iyesde, start, len, orig_start,
				  block_start, block_len, orig_block_len,
				  ram_bytes,
				  BTRFS_COMPRESS_NONE, /* compress_type */
				  type);
		if (IS_ERR(em))
			goto out;
	}
	ret = btrfs_add_ordered_extent_dio(iyesde, start, block_start,
					   len, block_len, type);
	if (ret) {
		if (em) {
			free_extent_map(em);
			btrfs_drop_extent_cache(BTRFS_I(iyesde), start,
						start + len - 1, 0);
		}
		em = ERR_PTR(ret);
	}
 out:

	return em;
}

static struct extent_map *btrfs_new_extent_direct(struct iyesde *iyesde,
						  u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct extent_map *em;
	struct btrfs_key ins;
	u64 alloc_hint;
	int ret;

	alloc_hint = get_extent_allocation_hint(iyesde, start, len);
	ret = btrfs_reserve_extent(root, len, len, fs_info->sectorsize,
				   0, alloc_hint, &ins, 1, 1);
	if (ret)
		return ERR_PTR(ret);

	em = btrfs_create_dio_extent(iyesde, start, ins.offset, start,
				     ins.objectid, ins.offset, ins.offset,
				     ins.offset, BTRFS_ORDERED_REGULAR);
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	if (IS_ERR(em))
		btrfs_free_reserved_extent(fs_info, ins.objectid,
					   ins.offset, 1);

	return em;
}

/*
 * returns 1 when the yescow is safe, < 1 on error, 0 if the
 * block must be cow'd
 */
yesinline int can_yescow_extent(struct iyesde *iyesde, u64 offset, u64 *len,
			      u64 *orig_start, u64 *orig_block_len,
			      u64 *ram_bytes)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_path *path;
	int ret;
	struct extent_buffer *leaf;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 disk_bytenr;
	u64 backref_offset;
	u64 extent_end;
	u64 num_bytes;
	int slot;
	int found_type;
	bool yescow = (BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATACOW);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_lookup_file_extent(NULL, root, path,
			btrfs_iyes(BTRFS_I(iyesde)), offset, 0);
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
	leaf = path->yesdes[0];
	btrfs_item_key_to_cpu(leaf, &key, slot);
	if (key.objectid != btrfs_iyes(BTRFS_I(iyesde)) ||
	    key.type != BTRFS_EXTENT_DATA_KEY) {
		/* yest our file or wrong item type, must cow */
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
		/* yest a regular extent, must cow */
		goto out;
	}

	if (!yescow && found_type == BTRFS_FILE_EXTENT_REG)
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
	if (btrfs_file_extent_generation(leaf, fi) <=
	    btrfs_root_last_snapshot(&root->root_item))
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
	if (!yescow && found_type == BTRFS_FILE_EXTENT_PREALLOC) {
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

	ret = btrfs_cross_ref_exist(root, btrfs_iyes(BTRFS_I(iyesde)),
				    key.offset - backref_offset, disk_bytenr);
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

static int lock_extent_direct(struct iyesde *iyesde, u64 lockstart, u64 lockend,
			      struct extent_state **cached_state, int writing)
{
	struct btrfs_ordered_extent *ordered;
	int ret = 0;

	while (1) {
		lock_extent_bits(&BTRFS_I(iyesde)->io_tree, lockstart, lockend,
				 cached_state);
		/*
		 * We're concerned with the entire range that we're going to be
		 * doing DIO to, so we need to make sure there's yes ordered
		 * extents in this range.
		 */
		ordered = btrfs_lookup_ordered_range(BTRFS_I(iyesde), lockstart,
						     lockend - lockstart + 1);

		/*
		 * We need to make sure there are yes buffered pages in this
		 * range either, we could have raced between the invalidate in
		 * generic_file_direct_write and locking the extent.  The
		 * invalidate needs to happen so that reads after a write do yest
		 * get stale data.
		 */
		if (!ordered &&
		    (!writing || !filemap_range_has_page(iyesde->i_mapping,
							 lockstart, lockend)))
			break;

		unlock_extent_cached(&BTRFS_I(iyesde)->io_tree, lockstart, lockend,
				     cached_state);

		if (ordered) {
			/*
			 * If we are doing a DIO read and the ordered extent we
			 * found is for a buffered write, we can yest wait for it
			 * to complete and retry, because if we do so we can
			 * deadlock with concurrent buffered writes on page
			 * locks. This happens only if our DIO read covers more
			 * than one extent map, if at this point has already
			 * created an ordered extent for a previous extent map
			 * and locked its range in the iyesde's io tree, and a
			 * concurrent write against that previous extent map's
			 * range and this range started (we unlock the ranges
			 * in the io tree only when the bios complete and
			 * buffered writes always lock pages before attempting
			 * to lock range in the io tree).
			 */
			if (writing ||
			    test_bit(BTRFS_ORDERED_DIRECT, &ordered->flags))
				btrfs_start_ordered_extent(iyesde, ordered, 1);
			else
				ret = -ENOTBLK;
			btrfs_put_ordered_extent(ordered);
		} else {
			/*
			 * We could trigger writeback for this range (and wait
			 * for it to complete) and then invalidate the pages for
			 * this range (through invalidate_iyesde_pages2_range()),
			 * but that can lead us to a deadlock with a concurrent
			 * call to readpages() (a buffered read or a defrag call
			 * triggered a readahead) on a page lock due to an
			 * ordered dio extent we created before but did yest have
			 * yet a corresponding bio submitted (whence it can yest
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

/* The callers of this must take lock_extent() */
static struct extent_map *create_io_em(struct iyesde *iyesde, u64 start, u64 len,
				       u64 orig_start, u64 block_start,
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

	em_tree = &BTRFS_I(iyesde)->extent_tree;
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
		btrfs_drop_extent_cache(BTRFS_I(iyesde), em->start,
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

	/* em got 2 refs yesw, callers needs to do free_extent_map once. */
	return em;
}


static int btrfs_get_blocks_direct_read(struct extent_map *em,
					struct buffer_head *bh_result,
					struct iyesde *iyesde,
					u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);

	if (em->block_start == EXTENT_MAP_HOLE ||
			test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
		return -ENOENT;

	len = min(len, em->len - (start - em->start));

	bh_result->b_blocknr = (em->block_start + (start - em->start)) >>
		iyesde->i_blkbits;
	bh_result->b_size = len;
	bh_result->b_bdev = fs_info->fs_devices->latest_bdev;
	set_buffer_mapped(bh_result);

	return 0;
}

static int btrfs_get_blocks_direct_write(struct extent_map **map,
					 struct buffer_head *bh_result,
					 struct iyesde *iyesde,
					 struct btrfs_dio_data *dio_data,
					 u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct extent_map *em = *map;
	int ret = 0;

	/*
	 * We don't allocate a new extent in the following cases
	 *
	 * 1) The iyesde is marked as NODATACOW. In this case we'll just use the
	 * existing extent.
	 * 2) The extent is marked as PREALLOC. We're good to go here and can
	 * just use the extent.
	 *
	 */
	if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags) ||
	    ((BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATACOW) &&
	     em->block_start != EXTENT_MAP_HOLE)) {
		int type;
		u64 block_start, orig_start, orig_block_len, ram_bytes;

		if (test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
			type = BTRFS_ORDERED_PREALLOC;
		else
			type = BTRFS_ORDERED_NOCOW;
		len = min(len, em->len - (start - em->start));
		block_start = em->block_start + (start - em->start);

		if (can_yescow_extent(iyesde, start, &len, &orig_start,
				     &orig_block_len, &ram_bytes) == 1 &&
		    btrfs_inc_yescow_writers(fs_info, block_start)) {
			struct extent_map *em2;

			em2 = btrfs_create_dio_extent(iyesde, start, len,
						      orig_start, block_start,
						      len, orig_block_len,
						      ram_bytes, type);
			btrfs_dec_yescow_writers(fs_info, block_start);
			if (type == BTRFS_ORDERED_PREALLOC) {
				free_extent_map(em);
				*map = em = em2;
			}

			if (em2 && IS_ERR(em2)) {
				ret = PTR_ERR(em2);
				goto out;
			}
			/*
			 * For iyesde marked NODATACOW or extent marked PREALLOC,
			 * use the existing or preallocated extent, so does yest
			 * need to adjust btrfs_space_info's bytes_may_use.
			 */
			btrfs_free_reserved_data_space_yesquota(iyesde, start,
							       len);
			goto skip_cow;
		}
	}

	/* this will cow the extent */
	len = bh_result->b_size;
	free_extent_map(em);
	*map = em = btrfs_new_extent_direct(iyesde, start, len);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out;
	}

	len = min(len, em->len - (start - em->start));

skip_cow:
	bh_result->b_blocknr = (em->block_start + (start - em->start)) >>
		iyesde->i_blkbits;
	bh_result->b_size = len;
	bh_result->b_bdev = fs_info->fs_devices->latest_bdev;
	set_buffer_mapped(bh_result);

	if (!test_bit(EXTENT_FLAG_PREALLOC, &em->flags))
		set_buffer_new(bh_result);

	/*
	 * Need to update the i_size under the extent lock so buffered
	 * readers will get the updated i_size when we unlock.
	 */
	if (!dio_data->overwrite && start + len > i_size_read(iyesde))
		i_size_write(iyesde, start + len);

	WARN_ON(dio_data->reserve < len);
	dio_data->reserve -= len;
	dio_data->unsubmitted_oe_range_end = start + len;
	current->journal_info = dio_data;
out:
	return ret;
}

static int btrfs_get_blocks_direct(struct iyesde *iyesde, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct extent_map *em;
	struct extent_state *cached_state = NULL;
	struct btrfs_dio_data *dio_data = NULL;
	u64 start = iblock << iyesde->i_blkbits;
	u64 lockstart, lockend;
	u64 len = bh_result->b_size;
	int ret = 0;

	if (!create)
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
	if (lock_extent_direct(iyesde, lockstart, lockend, &cached_state,
			       create)) {
		ret = -ENOTBLK;
		goto err;
	}

	em = btrfs_get_extent(BTRFS_I(iyesde), NULL, 0, start, len, 0);
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
	 * decompress it, so there will be buffering required yes matter what we
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

	if (create) {
		ret = btrfs_get_blocks_direct_write(&em, bh_result, iyesde,
						    dio_data, start, len);
		if (ret < 0)
			goto unlock_err;

		unlock_extent_cached(&BTRFS_I(iyesde)->io_tree, lockstart,
				     lockend, &cached_state);
	} else {
		ret = btrfs_get_blocks_direct_read(em, bh_result, iyesde,
						   start, len);
		/* Can be negative only if we read from a hole */
		if (ret < 0) {
			ret = 0;
			free_extent_map(em);
			goto unlock_err;
		}
		/*
		 * We need to unlock only the end area that we aren't using.
		 * The rest is going to be unlocked by the endio routine.
		 */
		lockstart = start + bh_result->b_size;
		if (lockstart < lockend) {
			unlock_extent_cached(&BTRFS_I(iyesde)->io_tree,
					     lockstart, lockend, &cached_state);
		} else {
			free_extent_state(cached_state);
		}
	}

	free_extent_map(em);

	return 0;

unlock_err:
	unlock_extent_cached(&BTRFS_I(iyesde)->io_tree, lockstart, lockend,
			     &cached_state);
err:
	if (dio_data)
		current->journal_info = dio_data;
	return ret;
}

static inline blk_status_t submit_dio_repair_bio(struct iyesde *iyesde,
						 struct bio *bio,
						 int mirror_num)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	blk_status_t ret;

	BUG_ON(bio_op(bio) == REQ_OP_WRITE);

	ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DIO_REPAIR);
	if (ret)
		return ret;

	ret = btrfs_map_bio(fs_info, bio, mirror_num);

	return ret;
}

static int btrfs_check_dio_repairable(struct iyesde *iyesde,
				      struct bio *failed_bio,
				      struct io_failure_record *failrec,
				      int failed_mirror)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	int num_copies;

	num_copies = btrfs_num_copies(fs_info, failrec->logical, failrec->len);
	if (num_copies == 1) {
		/*
		 * we only have a single copy of the data, so don't bother with
		 * all the retry and error correction code that follows. yes
		 * matter what the error is, it is very likely to persist.
		 */
		btrfs_debug(fs_info,
			"Check DIO Repairable: canyest repair, num_copies=%d, next_mirror %d, failed_mirror %d",
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

static blk_status_t dio_read_error(struct iyesde *iyesde, struct bio *failed_bio,
				   struct page *page, unsigned int pgoff,
				   u64 start, u64 end, int failed_mirror,
				   bio_end_io_t *repair_endio, void *repair_arg)
{
	struct io_failure_record *failrec;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct extent_io_tree *failure_tree = &BTRFS_I(iyesde)->io_failure_tree;
	struct bio *bio;
	int isector;
	unsigned int read_mode = 0;
	int segs;
	int ret;
	blk_status_t status;
	struct bio_vec bvec;

	BUG_ON(bio_op(failed_bio) == REQ_OP_WRITE);

	ret = btrfs_get_io_failure_record(iyesde, start, end, &failrec);
	if (ret)
		return erryes_to_blk_status(ret);

	ret = btrfs_check_dio_repairable(iyesde, failed_bio, failrec,
					 failed_mirror);
	if (!ret) {
		free_io_failure(failure_tree, io_tree, failrec);
		return BLK_STS_IOERR;
	}

	segs = bio_segments(failed_bio);
	bio_get_first_bvec(failed_bio, &bvec);
	if (segs > 1 ||
	    (bvec.bv_len > btrfs_iyesde_sectorsize(iyesde)))
		read_mode |= REQ_FAILFAST_DEV;

	isector = start - btrfs_io_bio(failed_bio)->logical;
	isector >>= iyesde->i_sb->s_blocksize_bits;
	bio = btrfs_create_repair_bio(iyesde, failed_bio, failrec, page,
				pgoff, isector, repair_endio, repair_arg);
	bio->bi_opf = REQ_OP_READ | read_mode;

	btrfs_debug(BTRFS_I(iyesde)->root->fs_info,
		    "repair DIO read error: submitting new dio read[%#x] to this_mirror=%d, in_validation=%d",
		    read_mode, failrec->this_mirror, failrec->in_validation);

	status = submit_dio_repair_bio(iyesde, bio, failrec->this_mirror);
	if (status) {
		free_io_failure(failure_tree, io_tree, failrec);
		bio_put(bio);
	}

	return status;
}

struct btrfs_retry_complete {
	struct completion done;
	struct iyesde *iyesde;
	u64 start;
	int uptodate;
};

static void btrfs_retry_endio_yescsum(struct bio *bio)
{
	struct btrfs_retry_complete *done = bio->bi_private;
	struct iyesde *iyesde = done->iyesde;
	struct bio_vec *bvec;
	struct extent_io_tree *io_tree, *failure_tree;
	struct bvec_iter_all iter_all;

	if (bio->bi_status)
		goto end;

	ASSERT(bio->bi_vcnt == 1);
	io_tree = &BTRFS_I(iyesde)->io_tree;
	failure_tree = &BTRFS_I(iyesde)->io_failure_tree;
	ASSERT(bio_first_bvec_all(bio)->bv_len == btrfs_iyesde_sectorsize(iyesde));

	done->uptodate = 1;
	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, iter_all)
		clean_io_failure(BTRFS_I(iyesde)->root->fs_info, failure_tree,
				 io_tree, done->start, bvec->bv_page,
				 btrfs_iyes(BTRFS_I(iyesde)), 0);
end:
	complete(&done->done);
	bio_put(bio);
}

static blk_status_t __btrfs_correct_data_yescsum(struct iyesde *iyesde,
						struct btrfs_io_bio *io_bio)
{
	struct btrfs_fs_info *fs_info;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct btrfs_retry_complete done;
	u64 start;
	unsigned int pgoff;
	u32 sectorsize;
	int nr_sectors;
	blk_status_t ret;
	blk_status_t err = BLK_STS_OK;

	fs_info = BTRFS_I(iyesde)->root->fs_info;
	sectorsize = fs_info->sectorsize;

	start = io_bio->logical;
	done.iyesde = iyesde;
	io_bio->bio.bi_iter = io_bio->iter;

	bio_for_each_segment(bvec, &io_bio->bio, iter) {
		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info, bvec.bv_len);
		pgoff = bvec.bv_offset;

next_block_or_try_again:
		done.uptodate = 0;
		done.start = start;
		init_completion(&done.done);

		ret = dio_read_error(iyesde, &io_bio->bio, bvec.bv_page,
				pgoff, start, start + sectorsize - 1,
				io_bio->mirror_num,
				btrfs_retry_endio_yescsum, &done);
		if (ret) {
			err = ret;
			goto next;
		}

		wait_for_completion_io(&done.done);

		if (!done.uptodate) {
			/* We might have ayesther mirror, so try again */
			goto next_block_or_try_again;
		}

next:
		start += sectorsize;

		nr_sectors--;
		if (nr_sectors) {
			pgoff += sectorsize;
			ASSERT(pgoff < PAGE_SIZE);
			goto next_block_or_try_again;
		}
	}

	return err;
}

static void btrfs_retry_endio(struct bio *bio)
{
	struct btrfs_retry_complete *done = bio->bi_private;
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	struct extent_io_tree *io_tree, *failure_tree;
	struct iyesde *iyesde = done->iyesde;
	struct bio_vec *bvec;
	int uptodate;
	int ret;
	int i = 0;
	struct bvec_iter_all iter_all;

	if (bio->bi_status)
		goto end;

	uptodate = 1;

	ASSERT(bio->bi_vcnt == 1);
	ASSERT(bio_first_bvec_all(bio)->bv_len == btrfs_iyesde_sectorsize(done->iyesde));

	io_tree = &BTRFS_I(iyesde)->io_tree;
	failure_tree = &BTRFS_I(iyesde)->io_failure_tree;

	ASSERT(!bio_flagged(bio, BIO_CLONED));
	bio_for_each_segment_all(bvec, bio, iter_all) {
		ret = __readpage_endio_check(iyesde, io_bio, i, bvec->bv_page,
					     bvec->bv_offset, done->start,
					     bvec->bv_len);
		if (!ret)
			clean_io_failure(BTRFS_I(iyesde)->root->fs_info,
					 failure_tree, io_tree, done->start,
					 bvec->bv_page,
					 btrfs_iyes(BTRFS_I(iyesde)),
					 bvec->bv_offset);
		else
			uptodate = 0;
		i++;
	}

	done->uptodate = uptodate;
end:
	complete(&done->done);
	bio_put(bio);
}

static blk_status_t __btrfs_subio_endio_read(struct iyesde *iyesde,
		struct btrfs_io_bio *io_bio, blk_status_t err)
{
	struct btrfs_fs_info *fs_info;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct btrfs_retry_complete done;
	u64 start;
	u64 offset = 0;
	u32 sectorsize;
	int nr_sectors;
	unsigned int pgoff;
	int csum_pos;
	bool uptodate = (err == 0);
	int ret;
	blk_status_t status;

	fs_info = BTRFS_I(iyesde)->root->fs_info;
	sectorsize = fs_info->sectorsize;

	err = BLK_STS_OK;
	start = io_bio->logical;
	done.iyesde = iyesde;
	io_bio->bio.bi_iter = io_bio->iter;

	bio_for_each_segment(bvec, &io_bio->bio, iter) {
		nr_sectors = BTRFS_BYTES_TO_BLKS(fs_info, bvec.bv_len);

		pgoff = bvec.bv_offset;
next_block:
		if (uptodate) {
			csum_pos = BTRFS_BYTES_TO_BLKS(fs_info, offset);
			ret = __readpage_endio_check(iyesde, io_bio, csum_pos,
					bvec.bv_page, pgoff, start, sectorsize);
			if (likely(!ret))
				goto next;
		}
try_again:
		done.uptodate = 0;
		done.start = start;
		init_completion(&done.done);

		status = dio_read_error(iyesde, &io_bio->bio, bvec.bv_page,
					pgoff, start, start + sectorsize - 1,
					io_bio->mirror_num, btrfs_retry_endio,
					&done);
		if (status) {
			err = status;
			goto next;
		}

		wait_for_completion_io(&done.done);

		if (!done.uptodate) {
			/* We might have ayesther mirror, so try again */
			goto try_again;
		}
next:
		offset += sectorsize;
		start += sectorsize;

		ASSERT(nr_sectors);

		nr_sectors--;
		if (nr_sectors) {
			pgoff += sectorsize;
			ASSERT(pgoff < PAGE_SIZE);
			goto next_block;
		}
	}

	return err;
}

static blk_status_t btrfs_subio_endio_read(struct iyesde *iyesde,
		struct btrfs_io_bio *io_bio, blk_status_t err)
{
	bool skip_csum = BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATASUM;

	if (skip_csum) {
		if (unlikely(err))
			return __btrfs_correct_data_yescsum(iyesde, io_bio);
		else
			return BLK_STS_OK;
	} else {
		return __btrfs_subio_endio_read(iyesde, io_bio, err);
	}
}

static void btrfs_endio_direct_read(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	struct iyesde *iyesde = dip->iyesde;
	struct bio *dio_bio;
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	blk_status_t err = bio->bi_status;

	if (dip->flags & BTRFS_DIO_ORIG_BIO_SUBMITTED)
		err = btrfs_subio_endio_read(iyesde, io_bio, err);

	unlock_extent(&BTRFS_I(iyesde)->io_tree, dip->logical_offset,
		      dip->logical_offset + dip->bytes - 1);
	dio_bio = dip->dio_bio;

	kfree(dip);

	dio_bio->bi_status = err;
	dio_end_io(dio_bio);
	btrfs_io_bio_free_csum(io_bio);
	bio_put(bio);
}

static void __endio_write_update_ordered(struct iyesde *iyesde,
					 const u64 offset, const u64 bytes,
					 const bool uptodate)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_ordered_extent *ordered = NULL;
	struct btrfs_workqueue *wq;
	u64 ordered_offset = offset;
	u64 ordered_bytes = bytes;
	u64 last_offset;

	if (btrfs_is_free_space_iyesde(BTRFS_I(iyesde)))
		wq = fs_info->endio_freespace_worker;
	else
		wq = fs_info->endio_write_workers;

	while (ordered_offset < offset + bytes) {
		last_offset = ordered_offset;
		if (btrfs_dec_test_first_ordered_pending(iyesde, &ordered,
							   &ordered_offset,
							   ordered_bytes,
							   uptodate)) {
			btrfs_init_work(&ordered->work, finish_ordered_fn, NULL,
					NULL);
			btrfs_queue_work(wq, &ordered->work);
		}
		/*
		 * If btrfs_dec_test_ordered_pending does yest find any ordered
		 * extent in the range, we can exit.
		 */
		if (ordered_offset == last_offset)
			return;
		/*
		 * Our bio might span multiple ordered extents. In this case
		 * we keep going until we have accounted the whole dio.
		 */
		if (ordered_offset < offset + bytes) {
			ordered_bytes = offset + bytes - ordered_offset;
			ordered = NULL;
		}
	}
}

static void btrfs_endio_direct_write(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	struct bio *dio_bio = dip->dio_bio;

	__endio_write_update_ordered(dip->iyesde, dip->logical_offset,
				     dip->bytes, !bio->bi_status);

	kfree(dip);

	dio_bio->bi_status = bio->bi_status;
	dio_end_io(dio_bio);
	bio_put(bio);
}

static blk_status_t btrfs_submit_bio_start_direct_io(void *private_data,
				    struct bio *bio, u64 offset)
{
	struct iyesde *iyesde = private_data;
	blk_status_t ret;
	ret = btrfs_csum_one_bio(iyesde, bio, offset, 1);
	BUG_ON(ret); /* -ENOMEM */
	return 0;
}

static void btrfs_end_dio_bio(struct bio *bio)
{
	struct btrfs_dio_private *dip = bio->bi_private;
	blk_status_t err = bio->bi_status;

	if (err)
		btrfs_warn(BTRFS_I(dip->iyesde)->root->fs_info,
			   "direct IO failed iyes %llu rw %d,%u sector %#Lx len %u err yes %d",
			   btrfs_iyes(BTRFS_I(dip->iyesde)), bio_op(bio),
			   bio->bi_opf,
			   (unsigned long long)bio->bi_iter.bi_sector,
			   bio->bi_iter.bi_size, err);

	if (dip->subio_endio)
		err = dip->subio_endio(dip->iyesde, btrfs_io_bio(bio), err);

	if (err) {
		/*
		 * We want to perceive the errors flag being set before
		 * decrementing the reference count. We don't need a barrier
		 * since atomic operations with a return value are fully
		 * ordered as per atomic_t.txt
		 */
		dip->errors = 1;
	}

	/* if there are more bios still pending for this dio, just exit */
	if (!atomic_dec_and_test(&dip->pending_bios))
		goto out;

	if (dip->errors) {
		bio_io_error(dip->orig_bio);
	} else {
		dip->dio_bio->bi_status = BLK_STS_OK;
		bio_endio(dip->orig_bio);
	}
out:
	bio_put(bio);
}

static inline blk_status_t btrfs_lookup_and_bind_dio_csum(struct iyesde *iyesde,
						 struct btrfs_dio_private *dip,
						 struct bio *bio,
						 u64 file_offset)
{
	struct btrfs_io_bio *io_bio = btrfs_io_bio(bio);
	struct btrfs_io_bio *orig_io_bio = btrfs_io_bio(dip->orig_bio);
	blk_status_t ret;

	/*
	 * We load all the csum data we need when we submit
	 * the first bio to reduce the csum tree search and
	 * contention.
	 */
	if (dip->logical_offset == file_offset) {
		ret = btrfs_lookup_bio_sums_dio(iyesde, dip->orig_bio,
						file_offset);
		if (ret)
			return ret;
	}

	if (bio == dip->orig_bio)
		return 0;

	file_offset -= dip->logical_offset;
	file_offset >>= iyesde->i_sb->s_blocksize_bits;
	io_bio->csum = (u8 *)(((u32 *)orig_io_bio->csum) + file_offset);

	return 0;
}

static inline blk_status_t btrfs_submit_dio_bio(struct bio *bio,
		struct iyesde *iyesde, u64 file_offset, int async_submit)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_dio_private *dip = bio->bi_private;
	bool write = bio_op(bio) == REQ_OP_WRITE;
	blk_status_t ret;

	/* Check btrfs_submit_bio_hook() for rules about async submit. */
	if (async_submit)
		async_submit = !atomic_read(&BTRFS_I(iyesde)->sync_writers);

	if (!write) {
		ret = btrfs_bio_wq_end_io(fs_info, bio, BTRFS_WQ_ENDIO_DATA);
		if (ret)
			goto err;
	}

	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATASUM)
		goto map;

	if (write && async_submit) {
		ret = btrfs_wq_submit_bio(fs_info, bio, 0, 0,
					  file_offset, iyesde,
					  btrfs_submit_bio_start_direct_io);
		goto err;
	} else if (write) {
		/*
		 * If we aren't doing async submit, calculate the csum of the
		 * bio yesw.
		 */
		ret = btrfs_csum_one_bio(iyesde, bio, file_offset, 1);
		if (ret)
			goto err;
	} else {
		ret = btrfs_lookup_and_bind_dio_csum(iyesde, dip, bio,
						     file_offset);
		if (ret)
			goto err;
	}
map:
	ret = btrfs_map_bio(fs_info, bio, 0);
err:
	return ret;
}

static int btrfs_submit_direct_hook(struct btrfs_dio_private *dip)
{
	struct iyesde *iyesde = dip->iyesde;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct bio *bio;
	struct bio *orig_bio = dip->orig_bio;
	u64 start_sector = orig_bio->bi_iter.bi_sector;
	u64 file_offset = dip->logical_offset;
	int async_submit = 0;
	u64 submit_len;
	int clone_offset = 0;
	int clone_len;
	int ret;
	blk_status_t status;
	struct btrfs_io_geometry geom;

	submit_len = orig_bio->bi_iter.bi_size;
	ret = btrfs_get_io_geometry(fs_info, btrfs_op(orig_bio),
				    start_sector << 9, submit_len, &geom);
	if (ret)
		return -EIO;

	if (geom.len >= submit_len) {
		bio = orig_bio;
		dip->flags |= BTRFS_DIO_ORIG_BIO_SUBMITTED;
		goto submit;
	}

	/* async crcs make it difficult to collect full stripe writes. */
	if (btrfs_data_alloc_profile(fs_info) & BTRFS_BLOCK_GROUP_RAID56_MASK)
		async_submit = 0;
	else
		async_submit = 1;

	/* bio split */
	ASSERT(geom.len <= INT_MAX);
	atomic_inc(&dip->pending_bios);
	do {
		clone_len = min_t(int, submit_len, geom.len);

		/*
		 * This will never fail as it's passing GPF_NOFS and
		 * the allocation is backed by btrfs_bioset.
		 */
		bio = btrfs_bio_clone_partial(orig_bio, clone_offset,
					      clone_len);
		bio->bi_private = dip;
		bio->bi_end_io = btrfs_end_dio_bio;
		btrfs_io_bio(bio)->logical = file_offset;

		ASSERT(submit_len >= clone_len);
		submit_len -= clone_len;
		if (submit_len == 0)
			break;

		/*
		 * Increase the count before we submit the bio so we kyesw
		 * the end IO handler won't happen before we increase the
		 * count. Otherwise, the dip might get freed before we're
		 * done setting it up.
		 */
		atomic_inc(&dip->pending_bios);

		status = btrfs_submit_dio_bio(bio, iyesde, file_offset,
						async_submit);
		if (status) {
			bio_put(bio);
			atomic_dec(&dip->pending_bios);
			goto out_err;
		}

		clone_offset += clone_len;
		start_sector += clone_len >> 9;
		file_offset += clone_len;

		ret = btrfs_get_io_geometry(fs_info, btrfs_op(orig_bio),
				      start_sector << 9, submit_len, &geom);
		if (ret)
			goto out_err;
	} while (submit_len > 0);

submit:
	status = btrfs_submit_dio_bio(bio, iyesde, file_offset, async_submit);
	if (!status)
		return 0;

	bio_put(bio);
out_err:
	dip->errors = 1;
	/*
	 * Before atomic variable goto zero, we must  make sure dip->errors is
	 * perceived to be set. This ordering is ensured by the fact that an
	 * atomic operations with a return value are fully ordered as per
	 * atomic_t.txt
	 */
	if (atomic_dec_and_test(&dip->pending_bios))
		bio_io_error(dip->orig_bio);

	/* bio_end_io() will handle error, so we needn't return it */
	return 0;
}

static void btrfs_submit_direct(struct bio *dio_bio, struct iyesde *iyesde,
				loff_t file_offset)
{
	struct btrfs_dio_private *dip = NULL;
	struct bio *bio = NULL;
	struct btrfs_io_bio *io_bio;
	bool write = (bio_op(dio_bio) == REQ_OP_WRITE);
	int ret = 0;

	bio = btrfs_bio_clone(dio_bio);

	dip = kzalloc(sizeof(*dip), GFP_NOFS);
	if (!dip) {
		ret = -ENOMEM;
		goto free_ordered;
	}

	dip->private = dio_bio->bi_private;
	dip->iyesde = iyesde;
	dip->logical_offset = file_offset;
	dip->bytes = dio_bio->bi_iter.bi_size;
	dip->disk_bytenr = (u64)dio_bio->bi_iter.bi_sector << 9;
	bio->bi_private = dip;
	dip->orig_bio = bio;
	dip->dio_bio = dio_bio;
	atomic_set(&dip->pending_bios, 0);
	io_bio = btrfs_io_bio(bio);
	io_bio->logical = file_offset;

	if (write) {
		bio->bi_end_io = btrfs_endio_direct_write;
	} else {
		bio->bi_end_io = btrfs_endio_direct_read;
		dip->subio_endio = btrfs_subio_endio_read;
	}

	/*
	 * Reset the range for unsubmitted ordered extents (to a 0 length range)
	 * even if we fail to submit a bio, because in such case we do the
	 * corresponding error handling below and it must yest be done a second
	 * time by btrfs_direct_IO().
	 */
	if (write) {
		struct btrfs_dio_data *dio_data = current->journal_info;

		dio_data->unsubmitted_oe_range_end = dip->logical_offset +
			dip->bytes;
		dio_data->unsubmitted_oe_range_start =
			dio_data->unsubmitted_oe_range_end;
	}

	ret = btrfs_submit_direct_hook(dip);
	if (!ret)
		return;

	btrfs_io_bio_free_csum(io_bio);

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
	if (bio && dip) {
		bio_io_error(bio);
		/*
		 * The end io callbacks free our dip, do the final put on bio
		 * and all the cleanup and final put for dio_bio (through
		 * dio_end_io()).
		 */
		dip = NULL;
		bio = NULL;
	} else {
		if (write)
			__endio_write_update_ordered(iyesde,
						file_offset,
						dio_bio->bi_iter.bi_size,
						false);
		else
			unlock_extent(&BTRFS_I(iyesde)->io_tree, file_offset,
			      file_offset + dio_bio->bi_iter.bi_size - 1);

		dio_bio->bi_status = BLK_STS_IOERR;
		/*
		 * Releases and cleans up our dio_bio, yes need to bio_put()
		 * yesr bio_endio()/bio_io_error() against dio_bio.
		 */
		dio_end_io(dio_bio);
	}
	if (bio)
		bio_put(bio);
	kfree(dip);
}

static ssize_t check_direct_IO(struct btrfs_fs_info *fs_info,
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
	struct iyesde *iyesde = file->f_mapping->host;
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_dio_data dio_data = { 0 };
	struct extent_changeset *data_reserved = NULL;
	loff_t offset = iocb->ki_pos;
	size_t count = 0;
	int flags = 0;
	bool wakeup = true;
	bool relock = false;
	ssize_t ret;

	if (check_direct_IO(fs_info, iter, offset))
		return 0;

	iyesde_dio_begin(iyesde);

	/*
	 * The generic stuff only does filemap_write_and_wait_range, which
	 * isn't eyesugh if we've written compressed pages to this area, so
	 * we need to flush the dirty pages again to make absolutely sure
	 * that any outstanding dirty pages are on disk.
	 */
	count = iov_iter_count(iter);
	if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
		     &BTRFS_I(iyesde)->runtime_flags))
		filemap_fdatawrite_range(iyesde->i_mapping, offset,
					 offset + count - 1);

	if (iov_iter_rw(iter) == WRITE) {
		/*
		 * If the write DIO is beyond the EOF, we need update
		 * the isize, but it is protected by i_mutex. So we can
		 * yest unlock the i_mutex at this case.
		 */
		if (offset + count <= iyesde->i_size) {
			dio_data.overwrite = 1;
			iyesde_unlock(iyesde);
			relock = true;
		} else if (iocb->ki_flags & IOCB_NOWAIT) {
			ret = -EAGAIN;
			goto out;
		}
		ret = btrfs_delalloc_reserve_space(iyesde, &data_reserved,
						   offset, count);
		if (ret)
			goto out;

		/*
		 * We need to kyesw how many extents we reserved so that we can
		 * do the accounting properly if we go over the number we
		 * originally calculated.  Abuse current->journal_info for this.
		 */
		dio_data.reserve = round_up(count,
					    fs_info->sectorsize);
		dio_data.unsubmitted_oe_range_start = (u64)offset;
		dio_data.unsubmitted_oe_range_end = (u64)offset;
		current->journal_info = &dio_data;
		down_read(&BTRFS_I(iyesde)->dio_sem);
	} else if (test_bit(BTRFS_INODE_READDIO_NEED_LOCK,
				     &BTRFS_I(iyesde)->runtime_flags)) {
		iyesde_dio_end(iyesde);
		flags = DIO_LOCKING | DIO_SKIP_HOLES;
		wakeup = false;
	}

	ret = __blockdev_direct_IO(iocb, iyesde,
				   fs_info->fs_devices->latest_bdev,
				   iter, btrfs_get_blocks_direct, NULL,
				   btrfs_submit_direct, flags);
	if (iov_iter_rw(iter) == WRITE) {
		up_read(&BTRFS_I(iyesde)->dio_sem);
		current->journal_info = NULL;
		if (ret < 0 && ret != -EIOCBQUEUED) {
			if (dio_data.reserve)
				btrfs_delalloc_release_space(iyesde, data_reserved,
					offset, dio_data.reserve, true);
			/*
			 * On error we might have left some ordered extents
			 * without submitting corresponding bios for them, so
			 * cleanup them up to avoid other tasks getting them
			 * and waiting for them to complete forever.
			 */
			if (dio_data.unsubmitted_oe_range_start <
			    dio_data.unsubmitted_oe_range_end)
				__endio_write_update_ordered(iyesde,
					dio_data.unsubmitted_oe_range_start,
					dio_data.unsubmitted_oe_range_end -
					dio_data.unsubmitted_oe_range_start,
					false);
		} else if (ret >= 0 && (size_t)ret < count)
			btrfs_delalloc_release_space(iyesde, data_reserved,
					offset, count - (size_t)ret, true);
		btrfs_delalloc_release_extents(BTRFS_I(iyesde), count);
	}
out:
	if (wakeup)
		iyesde_dio_end(iyesde);
	if (relock)
		iyesde_lock(iyesde);

	extent_changeset_free(data_reserved);
	return ret;
}

#define BTRFS_FIEMAP_FLAGS	(FIEMAP_FLAG_SYNC)

static int btrfs_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len)
{
	int	ret;

	ret = fiemap_check_flags(fieinfo, BTRFS_FIEMAP_FLAGS);
	if (ret)
		return ret;

	return extent_fiemap(iyesde, fieinfo, start, len);
}

int btrfs_readpage(struct file *file, struct page *page)
{
	struct extent_io_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->io_tree;
	return extent_read_full_page(tree, page, btrfs_get_extent, 0);
}

static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct iyesde *iyesde = page->mapping->host;
	int ret;

	if (current->flags & PF_MEMALLOC) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}

	/*
	 * If we are under memory pressure we will call this directly from the
	 * VM, we need to make sure we have the iyesde referenced for the ordered
	 * extent.  If yest just return like we didn't do anything.
	 */
	if (!igrab(iyesde)) {
		redirty_page_for_writepage(wbc, page);
		return AOP_WRITEPAGE_ACTIVATE;
	}
	ret = extent_write_full_page(page, wbc);
	btrfs_add_delayed_iput(iyesde);
	return ret;
}

static int btrfs_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	return extent_writepages(mapping, wbc);
}

static int
btrfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return extent_readpages(mapping, pages, nr_pages);
}

static int __btrfs_releasepage(struct page *page, gfp_t gfp_flags)
{
	int ret = try_release_extent_mapping(page, gfp_flags);
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
	return __btrfs_releasepage(page, gfp_flags);
}

static void btrfs_invalidatepage(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct iyesde *iyesde = page->mapping->host;
	struct extent_io_tree *tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	u64 page_start = page_offset(page);
	u64 page_end = page_start + PAGE_SIZE - 1;
	u64 start;
	u64 end;
	int iyesde_evicting = iyesde->i_state & I_FREEING;

	/*
	 * we have the page locked, so new writeback can't start,
	 * and the dirty bit won't be cleared while we are here.
	 *
	 * Wait for IO on this page so that we can safely clear
	 * the PagePrivate2 bit and do ordered accounting
	 */
	wait_on_page_writeback(page);

	tree = &BTRFS_I(iyesde)->io_tree;
	if (offset) {
		btrfs_releasepage(page, GFP_NOFS);
		return;
	}

	if (!iyesde_evicting)
		lock_extent_bits(tree, page_start, page_end, &cached_state);
again:
	start = page_start;
	ordered = btrfs_lookup_ordered_range(BTRFS_I(iyesde), start,
					page_end - start + 1);
	if (ordered) {
		end = min(page_end, ordered->file_offset + ordered->len - 1);
		/*
		 * IO on this page will never be started, so we need
		 * to account for any ordered extents yesw
		 */
		if (!iyesde_evicting)
			clear_extent_bit(tree, start, end,
					 EXTENT_DELALLOC | EXTENT_DELALLOC_NEW |
					 EXTENT_LOCKED | EXTENT_DO_ACCOUNTING |
					 EXTENT_DEFRAG, 1, 0, &cached_state);
		/*
		 * whoever cleared the private bit is responsible
		 * for the finish_ordered_io
		 */
		if (TestClearPagePrivate2(page)) {
			struct btrfs_ordered_iyesde_tree *tree;
			u64 new_len;

			tree = &BTRFS_I(iyesde)->ordered_tree;

			spin_lock_irq(&tree->lock);
			set_bit(BTRFS_ORDERED_TRUNCATED, &ordered->flags);
			new_len = start - ordered->file_offset;
			if (new_len < ordered->truncated_len)
				ordered->truncated_len = new_len;
			spin_unlock_irq(&tree->lock);

			if (btrfs_dec_test_ordered_pending(iyesde, &ordered,
							   start,
							   end - start + 1, 1))
				btrfs_finish_ordered_io(ordered);
		}
		btrfs_put_ordered_extent(ordered);
		if (!iyesde_evicting) {
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
		btrfs_qgroup_free_data(iyesde, NULL, page_start, PAGE_SIZE);
	if (!iyesde_evicting) {
		clear_extent_bit(tree, page_start, page_end, EXTENT_LOCKED |
				 EXTENT_DELALLOC | EXTENT_DELALLOC_NEW |
				 EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG, 1, 1,
				 &cached_state);

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
 * btrfs_page_mkwrite() is yest allowed to change the file size as it gets
 * called from a page fault handler when a page is first dirtied. Hence we must
 * be careful to check for EOF conditions here. We set the page up correctly
 * for a written page which means we get ENOSPC checking when writing into
 * holes and correct delalloc and unwritten extent mapping on filesystems that
 * support these features.
 *
 * We are yest allowed to take the i_mutex here so we have to play games to
 * protect against truncate races as the page could yesw be beyond EOF.  Because
 * truncate_setsize() writes the iyesde size before removing pages, once we have
 * the page lock we can determine safely if the page is beyond EOF. If it is yest
 * beyond EOF, then the page is guaranteed safe against truncation until we
 * unlock the page.
 */
vm_fault_t btrfs_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_changeset *data_reserved = NULL;
	char *kaddr;
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

	sb_start_pagefault(iyesde->i_sb);
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
	ret2 = btrfs_delalloc_reserve_space(iyesde, &data_reserved, page_start,
					   reserved_space);
	if (!ret2) {
		ret2 = file_update_time(vmf->vma->vm_file);
		reserved = 1;
	}
	if (ret2) {
		ret = vmf_error(ret2);
		if (reserved)
			goto out;
		goto out_yesreserve;
	}

	ret = VM_FAULT_NOPAGE; /* make the VM retry the fault */
again:
	lock_page(page);
	size = i_size_read(iyesde);

	if ((page->mapping != iyesde->i_mapping) ||
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
	ordered = btrfs_lookup_ordered_range(BTRFS_I(iyesde), page_start,
			PAGE_SIZE);
	if (ordered) {
		unlock_extent_cached(io_tree, page_start, page_end,
				     &cached_state);
		unlock_page(page);
		btrfs_start_ordered_extent(iyesde, ordered, 1);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	if (page->index == ((size - 1) >> PAGE_SHIFT)) {
		reserved_space = round_up(size - page_start,
					  fs_info->sectorsize);
		if (reserved_space < PAGE_SIZE) {
			end = page_start + reserved_space - 1;
			btrfs_delalloc_release_space(iyesde, data_reserved,
					page_start, PAGE_SIZE - reserved_space,
					true);
		}
	}

	/*
	 * page_mkwrite gets called when the page is firstly dirtied after it's
	 * faulted in, but write(2) could also dirty a page and set delalloc
	 * bits, thus in this case for space account reason, we still need to
	 * clear any delalloc bits within this page range since we have to
	 * reserve data&meta space before lock_page() (see above comments).
	 */
	clear_extent_bit(&BTRFS_I(iyesde)->io_tree, page_start, end,
			  EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING |
			  EXTENT_DEFRAG, 0, 0, &cached_state);

	ret2 = btrfs_set_extent_delalloc(iyesde, page_start, end, 0,
					&cached_state);
	if (ret2) {
		unlock_extent_cached(io_tree, page_start, page_end,
				     &cached_state);
		ret = VM_FAULT_SIGBUS;
		goto out_unlock;
	}
	ret2 = 0;

	/* page is wholly or partially inside EOF */
	if (page_start + PAGE_SIZE > size)
		zero_start = offset_in_page(size);
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

	BTRFS_I(iyesde)->last_trans = fs_info->generation;
	BTRFS_I(iyesde)->last_sub_trans = BTRFS_I(iyesde)->root->log_transid;
	BTRFS_I(iyesde)->last_log_commit = BTRFS_I(iyesde)->root->last_log_commit;

	unlock_extent_cached(io_tree, page_start, page_end, &cached_state);

	if (!ret2) {
		btrfs_delalloc_release_extents(BTRFS_I(iyesde), PAGE_SIZE);
		sb_end_pagefault(iyesde->i_sb);
		extent_changeset_free(data_reserved);
		return VM_FAULT_LOCKED;
	}

out_unlock:
	unlock_page(page);
out:
	btrfs_delalloc_release_extents(BTRFS_I(iyesde), PAGE_SIZE);
	btrfs_delalloc_release_space(iyesde, data_reserved, page_start,
				     reserved_space, (ret != 0));
out_yesreserve:
	sb_end_pagefault(iyesde->i_sb);
	extent_changeset_free(data_reserved);
	return ret;
}

static int btrfs_truncate(struct iyesde *iyesde, bool skip_writeback)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	struct btrfs_block_rsv *rsv;
	int ret;
	struct btrfs_trans_handle *trans;
	u64 mask = fs_info->sectorsize - 1;
	u64 min_size = btrfs_calc_metadata_size(fs_info, 1);

	if (!skip_writeback) {
		ret = btrfs_wait_ordered_range(iyesde, iyesde->i_size & (~mask),
					       (u64)-1);
		if (ret)
			return ret;
	}

	/*
	 * Yes ladies and gentlemen, this is indeed ugly.  We have a couple of
	 * things going on here:
	 *
	 * 1) We need to reserve space to update our iyesde.
	 *
	 * 2) We need to have something to cache all the space that is going to
	 * be free'd up by the truncate operation, but also have some slack
	 * space reserved in case it uses space during the truncate (thank you
	 * very much snapshotting).
	 *
	 * And we need these to be separate.  The fact is we can use a lot of
	 * space doing the truncate, and we have yes earthly idea how much space
	 * we will use, so we need the truncate reservation to be separate so it
	 * doesn't end up using space reserved for updating the iyesde.  We also
	 * need to be able to stop the transaction and start a new one, which
	 * means we need to be able to update the iyesde several times, and we
	 * have yes idea of kyeswing how many times that will be, so we can't just
	 * reserve 1 item for the entirety of the operation, so that has to be
	 * done separately as well.
	 *
	 * So that leaves us with
	 *
	 * 1) rsv - for the truncate reservation, which we will steal from the
	 * transaction reservation.
	 * 2) fs_info->trans_block_rsv - this will have 1 items worth left for
	 * updating the iyesde.
	 */
	rsv = btrfs_alloc_block_rsv(fs_info, BTRFS_BLOCK_RSV_TEMP);
	if (!rsv)
		return -ENOMEM;
	rsv->size = min_size;
	rsv->failfast = 1;

	/*
	 * 1 for the truncate slack space
	 * 1 for updating the iyesde.
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

	/*
	 * So if we truncate and then write and fsync we yesrmally would just
	 * write the extents that changed, which is a problem if we need to
	 * first truncate that entire iyesde.  So set this flag so we write out
	 * all of the extents in the iyesde to the sync log so we're completely
	 * safe.
	 */
	set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &BTRFS_I(iyesde)->runtime_flags);
	trans->block_rsv = rsv;

	while (1) {
		ret = btrfs_truncate_iyesde_items(trans, root, iyesde,
						 iyesde->i_size,
						 BTRFS_EXTENT_DATA_KEY);
		trans->block_rsv = &fs_info->trans_block_rsv;
		if (ret != -ENOSPC && ret != -EAGAIN)
			break;

		ret = btrfs_update_iyesde(trans, root, iyesde);
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

		btrfs_block_rsv_release(fs_info, rsv, -1);
		ret = btrfs_block_rsv_migrate(&fs_info->trans_block_rsv,
					      rsv, min_size, false);
		BUG_ON(ret);	/* shouldn't happen */
		trans->block_rsv = rsv;
	}

	/*
	 * We can't call btrfs_truncate_block inside a trans handle as we could
	 * deadlock with freeze, if we got NEED_TRUNCATE_BLOCK then we kyesw
	 * we've truncated everything except the last little bit, and can do
	 * btrfs_truncate_block and then update the disk_i_size.
	 */
	if (ret == NEED_TRUNCATE_BLOCK) {
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);

		ret = btrfs_truncate_block(iyesde, iyesde->i_size, 0, 0);
		if (ret)
			goto out;
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		btrfs_ordered_update_i_size(iyesde, iyesde->i_size, NULL);
	}

	if (trans) {
		int ret2;

		trans->block_rsv = &fs_info->trans_block_rsv;
		ret2 = btrfs_update_iyesde(trans, root, iyesde);
		if (ret2 && !ret)
			ret = ret2;

		ret2 = btrfs_end_transaction(trans);
		if (ret2 && !ret)
			ret = ret2;
		btrfs_btree_balance_dirty(fs_info);
	}
out:
	btrfs_free_block_rsv(fs_info, rsv);

	return ret;
}

/*
 * create a new subvolume directory/iyesde (helper for the ioctl).
 */
int btrfs_create_subvol_root(struct btrfs_trans_handle *trans,
			     struct btrfs_root *new_root,
			     struct btrfs_root *parent_root,
			     u64 new_dirid)
{
	struct iyesde *iyesde;
	int err;
	u64 index = 0;

	iyesde = btrfs_new_iyesde(trans, new_root, NULL, "..", 2,
				new_dirid, new_dirid,
				S_IFDIR | (~current_umask() & S_IRWXUGO),
				&index);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);
	iyesde->i_op = &btrfs_dir_iyesde_operations;
	iyesde->i_fop = &btrfs_dir_file_operations;

	set_nlink(iyesde, 1);
	btrfs_i_size_write(BTRFS_I(iyesde), 0);
	unlock_new_iyesde(iyesde);

	err = btrfs_subvol_inherit_props(trans, new_root, parent_root);
	if (err)
		btrfs_err(new_root->fs_info,
			  "error inheriting subvolume %llu properties: %d",
			  new_root->root_key.objectid, err);

	err = btrfs_update_iyesde(trans, new_root, iyesde);

	iput(iyesde);
	return err;
}

struct iyesde *btrfs_alloc_iyesde(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_iyesde *ei;
	struct iyesde *iyesde;

	ei = kmem_cache_alloc(btrfs_iyesde_cachep, GFP_KERNEL);
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
	ei->csum_bytes = 0;
	ei->index_cnt = (u64)-1;
	ei->dir_index = 0;
	ei->last_unlink_trans = 0;
	ei->last_log_commit = 0;

	spin_lock_init(&ei->lock);
	ei->outstanding_extents = 0;
	if (sb->s_magic != BTRFS_TEST_MAGIC)
		btrfs_init_metadata_block_rsv(fs_info, &ei->block_rsv,
					      BTRFS_BLOCK_RSV_DELALLOC);
	ei->runtime_flags = 0;
	ei->prop_compress = BTRFS_COMPRESS_NONE;
	ei->defrag_compress = BTRFS_COMPRESS_NONE;

	ei->delayed_yesde = NULL;

	ei->i_otime.tv_sec = 0;
	ei->i_otime.tv_nsec = 0;

	iyesde = &ei->vfs_iyesde;
	extent_map_tree_init(&ei->extent_tree);
	extent_io_tree_init(fs_info, &ei->io_tree, IO_TREE_INODE_IO, iyesde);
	extent_io_tree_init(fs_info, &ei->io_failure_tree,
			    IO_TREE_INODE_IO_FAILURE, iyesde);
	ei->io_tree.track_uptodate = true;
	ei->io_failure_tree.track_uptodate = true;
	atomic_set(&ei->sync_writers, 0);
	mutex_init(&ei->log_mutex);
	btrfs_ordered_iyesde_tree_init(&ei->ordered_tree);
	INIT_LIST_HEAD(&ei->delalloc_iyesdes);
	INIT_LIST_HEAD(&ei->delayed_iput);
	RB_CLEAR_NODE(&ei->rb_yesde);
	init_rwsem(&ei->dio_sem);

	return iyesde;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_destroy_iyesde(struct iyesde *iyesde)
{
	btrfs_drop_extent_cache(BTRFS_I(iyesde), 0, (u64)-1, 0);
	kmem_cache_free(btrfs_iyesde_cachep, BTRFS_I(iyesde));
}
#endif

void btrfs_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(btrfs_iyesde_cachep, BTRFS_I(iyesde));
}

void btrfs_destroy_iyesde(struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct btrfs_ordered_extent *ordered;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;

	WARN_ON(!hlist_empty(&iyesde->i_dentry));
	WARN_ON(iyesde->i_data.nrpages);
	WARN_ON(BTRFS_I(iyesde)->block_rsv.reserved);
	WARN_ON(BTRFS_I(iyesde)->block_rsv.size);
	WARN_ON(BTRFS_I(iyesde)->outstanding_extents);
	WARN_ON(BTRFS_I(iyesde)->delalloc_bytes);
	WARN_ON(BTRFS_I(iyesde)->new_delalloc_bytes);
	WARN_ON(BTRFS_I(iyesde)->csum_bytes);
	WARN_ON(BTRFS_I(iyesde)->defrag_bytes);

	/*
	 * This can happen where we create an iyesde, but somebody else also
	 * created the same iyesde and we need to destroy the one we already
	 * created.
	 */
	if (!root)
		return;

	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(iyesde, (u64)-1);
		if (!ordered)
			break;
		else {
			btrfs_err(fs_info,
				  "found ordered extent %llu %llu on iyesde cleanup",
				  ordered->file_offset, ordered->len);
			btrfs_remove_ordered_extent(iyesde, ordered);
			btrfs_put_ordered_extent(ordered);
			btrfs_put_ordered_extent(ordered);
		}
	}
	btrfs_qgroup_check_reserved_leak(iyesde);
	iyesde_tree_del(iyesde);
	btrfs_drop_extent_cache(BTRFS_I(iyesde), 0, (u64)-1, 0);
}

int btrfs_drop_iyesde(struct iyesde *iyesde)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;

	if (root == NULL)
		return 1;

	/* the snap/subvol tree is on deleting */
	if (btrfs_root_refs(&root->root_item) == 0)
		return 1;
	else
		return generic_drop_iyesde(iyesde);
}

static void init_once(void *foo)
{
	struct btrfs_iyesde *ei = (struct btrfs_iyesde *) foo;

	iyesde_init_once(&ei->vfs_iyesde);
}

void __cold btrfs_destroy_cachep(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(btrfs_iyesde_cachep);
	kmem_cache_destroy(btrfs_trans_handle_cachep);
	kmem_cache_destroy(btrfs_path_cachep);
	kmem_cache_destroy(btrfs_free_space_cachep);
	kmem_cache_destroy(btrfs_free_space_bitmap_cachep);
}

int __init btrfs_init_cachep(void)
{
	btrfs_iyesde_cachep = kmem_cache_create("btrfs_iyesde",
			sizeof(struct btrfs_iyesde), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT,
			init_once);
	if (!btrfs_iyesde_cachep)
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
							SLAB_RED_ZONE, NULL);
	if (!btrfs_free_space_bitmap_cachep)
		goto fail;

	return 0;
fail:
	btrfs_destroy_cachep();
	return -ENOMEM;
}

static int btrfs_getattr(const struct path *path, struct kstat *stat,
			 u32 request_mask, unsigned int flags)
{
	u64 delalloc_bytes;
	struct iyesde *iyesde = d_iyesde(path->dentry);
	u32 blocksize = iyesde->i_sb->s_blocksize;
	u32 bi_flags = BTRFS_I(iyesde)->flags;

	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = BTRFS_I(iyesde)->i_otime.tv_sec;
	stat->btime.tv_nsec = BTRFS_I(iyesde)->i_otime.tv_nsec;
	if (bi_flags & BTRFS_INODE_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	if (bi_flags & BTRFS_INODE_COMPRESS)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (bi_flags & BTRFS_INODE_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (bi_flags & BTRFS_INODE_NODUMP)
		stat->attributes |= STATX_ATTR_NODUMP;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
				  STATX_ATTR_COMPRESSED |
				  STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_NODUMP);

	generic_fillattr(iyesde, stat);
	stat->dev = BTRFS_I(iyesde)->root->ayesn_dev;

	spin_lock(&BTRFS_I(iyesde)->lock);
	delalloc_bytes = BTRFS_I(iyesde)->new_delalloc_bytes;
	spin_unlock(&BTRFS_I(iyesde)->lock);
	stat->blocks = (ALIGN(iyesde_get_bytes(iyesde), blocksize) +
			ALIGN(delalloc_bytes, blocksize)) >> 9;
	return 0;
}

static int btrfs_rename_exchange(struct iyesde *old_dir,
			      struct dentry *old_dentry,
			      struct iyesde *new_dir,
			      struct dentry *new_dentry)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(old_dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct btrfs_root *dest = BTRFS_I(new_dir)->root;
	struct iyesde *new_iyesde = new_dentry->d_iyesde;
	struct iyesde *old_iyesde = old_dentry->d_iyesde;
	struct timespec64 ctime = current_time(old_iyesde);
	struct dentry *parent;
	u64 old_iyes = btrfs_iyes(BTRFS_I(old_iyesde));
	u64 new_iyes = btrfs_iyes(BTRFS_I(new_iyesde));
	u64 old_idx = 0;
	u64 new_idx = 0;
	u64 root_objectid;
	int ret;
	bool root_log_pinned = false;
	bool dest_log_pinned = false;
	struct btrfs_log_ctx ctx_root;
	struct btrfs_log_ctx ctx_dest;
	bool sync_log_root = false;
	bool sync_log_dest = false;
	bool commit_transaction = false;

	/* we only allow rename subvolume link between subvolumes */
	if (old_iyes != BTRFS_FIRST_FREE_OBJECTID && root != dest)
		return -EXDEV;

	btrfs_init_log_ctx(&ctx_root, old_iyesde);
	btrfs_init_log_ctx(&ctx_dest, new_iyesde);

	/* close the race window with snapshot create/destroy ioctl */
	if (old_iyes == BTRFS_FIRST_FREE_OBJECTID ||
	    new_iyes == BTRFS_FIRST_FREE_OBJECTID)
		down_read(&fs_info->subvol_sem);

	/*
	 * We want to reserve the absolute worst case amount of items.  So if
	 * both iyesdes are subvols and we need to unlink them then that would
	 * require 4 item modifications, but if they are both yesrmal iyesdes it
	 * would require 5 item modifications, so we'll assume their yesrmal
	 * iyesdes.  So 5 * 2 is 10, plus 2 for the new links, so 12 total items
	 * should cover the worst case number of items we'll modify.
	 */
	trans = btrfs_start_transaction(root, 12);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_yestrans;
	}

	if (dest != root)
		btrfs_record_root_in_trans(trans, dest);

	/*
	 * We need to find a free sequence number both in the source and
	 * in the destination directory for the exchange.
	 */
	ret = btrfs_set_iyesde_index(BTRFS_I(new_dir), &old_idx);
	if (ret)
		goto out_fail;
	ret = btrfs_set_iyesde_index(BTRFS_I(old_dir), &new_idx);
	if (ret)
		goto out_fail;

	BTRFS_I(old_iyesde)->dir_index = 0ULL;
	BTRFS_I(new_iyesde)->dir_index = 0ULL;

	/* Reference for the source. */
	if (old_iyes == BTRFS_FIRST_FREE_OBJECTID) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(trans);
	} else {
		btrfs_pin_log_trans(root);
		root_log_pinned = true;
		ret = btrfs_insert_iyesde_ref(trans, dest,
					     new_dentry->d_name.name,
					     new_dentry->d_name.len,
					     old_iyes,
					     btrfs_iyes(BTRFS_I(new_dir)),
					     old_idx);
		if (ret)
			goto out_fail;
	}

	/* And yesw for the dest. */
	if (new_iyes == BTRFS_FIRST_FREE_OBJECTID) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(trans);
	} else {
		btrfs_pin_log_trans(dest);
		dest_log_pinned = true;
		ret = btrfs_insert_iyesde_ref(trans, root,
					     old_dentry->d_name.name,
					     old_dentry->d_name.len,
					     new_iyes,
					     btrfs_iyes(BTRFS_I(old_dir)),
					     new_idx);
		if (ret)
			goto out_fail;
	}

	/* Update iyesde version and ctime/mtime. */
	iyesde_inc_iversion(old_dir);
	iyesde_inc_iversion(new_dir);
	iyesde_inc_iversion(old_iyesde);
	iyesde_inc_iversion(new_iyesde);
	old_dir->i_ctime = old_dir->i_mtime = ctime;
	new_dir->i_ctime = new_dir->i_mtime = ctime;
	old_iyesde->i_ctime = ctime;
	new_iyesde->i_ctime = ctime;

	if (old_dentry->d_parent != new_dentry->d_parent) {
		btrfs_record_unlink_dir(trans, BTRFS_I(old_dir),
				BTRFS_I(old_iyesde), 1);
		btrfs_record_unlink_dir(trans, BTRFS_I(new_dir),
				BTRFS_I(new_iyesde), 1);
	}

	/* src is a subvolume */
	if (old_iyes == BTRFS_FIRST_FREE_OBJECTID) {
		root_objectid = BTRFS_I(old_iyesde)->root->root_key.objectid;
		ret = btrfs_unlink_subvol(trans, old_dir, root_objectid,
					  old_dentry->d_name.name,
					  old_dentry->d_name.len);
	} else { /* src is an iyesde */
		ret = __btrfs_unlink_iyesde(trans, root, BTRFS_I(old_dir),
					   BTRFS_I(old_dentry->d_iyesde),
					   old_dentry->d_name.name,
					   old_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_iyesde(trans, root, old_iyesde);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	/* dest is a subvolume */
	if (new_iyes == BTRFS_FIRST_FREE_OBJECTID) {
		root_objectid = BTRFS_I(new_iyesde)->root->root_key.objectid;
		ret = btrfs_unlink_subvol(trans, new_dir, root_objectid,
					  new_dentry->d_name.name,
					  new_dentry->d_name.len);
	} else { /* dest is an iyesde */
		ret = __btrfs_unlink_iyesde(trans, dest, BTRFS_I(new_dir),
					   BTRFS_I(new_dentry->d_iyesde),
					   new_dentry->d_name.name,
					   new_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_iyesde(trans, dest, new_iyesde);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, BTRFS_I(new_dir), BTRFS_I(old_iyesde),
			     new_dentry->d_name.name,
			     new_dentry->d_name.len, 0, old_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, BTRFS_I(old_dir), BTRFS_I(new_iyesde),
			     old_dentry->d_name.name,
			     old_dentry->d_name.len, 0, new_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_iyesde->i_nlink == 1)
		BTRFS_I(old_iyesde)->dir_index = old_idx;
	if (new_iyesde->i_nlink == 1)
		BTRFS_I(new_iyesde)->dir_index = new_idx;

	if (root_log_pinned) {
		parent = new_dentry->d_parent;
		ret = btrfs_log_new_name(trans, BTRFS_I(old_iyesde),
					 BTRFS_I(old_dir), parent,
					 false, &ctx_root);
		if (ret == BTRFS_NEED_LOG_SYNC)
			sync_log_root = true;
		else if (ret == BTRFS_NEED_TRANS_COMMIT)
			commit_transaction = true;
		ret = 0;
		btrfs_end_log_trans(root);
		root_log_pinned = false;
	}
	if (dest_log_pinned) {
		if (!commit_transaction) {
			parent = old_dentry->d_parent;
			ret = btrfs_log_new_name(trans, BTRFS_I(new_iyesde),
						 BTRFS_I(new_dir), parent,
						 false, &ctx_dest);
			if (ret == BTRFS_NEED_LOG_SYNC)
				sync_log_dest = true;
			else if (ret == BTRFS_NEED_TRANS_COMMIT)
				commit_transaction = true;
			ret = 0;
		}
		btrfs_end_log_trans(dest);
		dest_log_pinned = false;
	}
out_fail:
	/*
	 * If we have pinned a log and an error happened, we unpin tasks
	 * trying to sync the log and force them to fallback to a transaction
	 * commit if the log currently contains any of the iyesdes involved in
	 * this rename operation (to ensure we do yest persist a log with an
	 * inconsistent state for any of these iyesdes or leading to any
	 * inconsistencies when replayed). If the transaction was aborted, the
	 * abortion reason is propagated to userspace when attempting to commit
	 * the transaction. If the log does yest contain any of these iyesdes, we
	 * allow the tasks to sync it.
	 */
	if (ret && (root_log_pinned || dest_log_pinned)) {
		if (btrfs_iyesde_in_log(BTRFS_I(old_dir), fs_info->generation) ||
		    btrfs_iyesde_in_log(BTRFS_I(new_dir), fs_info->generation) ||
		    btrfs_iyesde_in_log(BTRFS_I(old_iyesde), fs_info->generation) ||
		    (new_iyesde &&
		     btrfs_iyesde_in_log(BTRFS_I(new_iyesde), fs_info->generation)))
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
	if (!ret && sync_log_root && !commit_transaction) {
		ret = btrfs_sync_log(trans, BTRFS_I(old_iyesde)->root,
				     &ctx_root);
		if (ret)
			commit_transaction = true;
	}
	if (!ret && sync_log_dest && !commit_transaction) {
		ret = btrfs_sync_log(trans, BTRFS_I(new_iyesde)->root,
				     &ctx_dest);
		if (ret)
			commit_transaction = true;
	}
	if (commit_transaction) {
		/*
		 * We may have set commit_transaction when logging the new name
		 * in the destination root, in which case we left the source
		 * root context in the list of log contextes. So make sure we
		 * remove it to avoid invalid memory accesses, since the context
		 * was allocated in our stack frame.
		 */
		if (sync_log_root) {
			mutex_lock(&root->log_mutex);
			list_del_init(&ctx_root.list);
			mutex_unlock(&root->log_mutex);
		}
		ret = btrfs_commit_transaction(trans);
	} else {
		int ret2;

		ret2 = btrfs_end_transaction(trans);
		ret = ret ? ret : ret2;
	}
out_yestrans:
	if (new_iyes == BTRFS_FIRST_FREE_OBJECTID ||
	    old_iyes == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	ASSERT(list_empty(&ctx_root.list));
	ASSERT(list_empty(&ctx_dest.list));

	return ret;
}

static int btrfs_whiteout_for_rename(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct iyesde *dir,
				     struct dentry *dentry)
{
	int ret;
	struct iyesde *iyesde;
	u64 objectid;
	u64 index;

	ret = btrfs_find_free_iyes(root, &objectid);
	if (ret)
		return ret;

	iyesde = btrfs_new_iyesde(trans, root, dir,
				dentry->d_name.name,
				dentry->d_name.len,
				btrfs_iyes(BTRFS_I(dir)),
				objectid,
				S_IFCHR | WHITEOUT_MODE,
				&index);

	if (IS_ERR(iyesde)) {
		ret = PTR_ERR(iyesde);
		return ret;
	}

	iyesde->i_op = &btrfs_special_iyesde_operations;
	init_special_iyesde(iyesde, iyesde->i_mode,
		WHITEOUT_DEV);

	ret = btrfs_init_iyesde_security(trans, iyesde, dir,
				&dentry->d_name);
	if (ret)
		goto out;

	ret = btrfs_add_yesndir(trans, BTRFS_I(dir), dentry,
				BTRFS_I(iyesde), 0, index);
	if (ret)
		goto out;

	ret = btrfs_update_iyesde(trans, root, iyesde);
out:
	unlock_new_iyesde(iyesde);
	if (ret)
		iyesde_dec_link_count(iyesde);
	iput(iyesde);

	return ret;
}

static int btrfs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			   struct iyesde *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(old_dir->i_sb);
	struct btrfs_trans_handle *trans;
	unsigned int trans_num_items;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct btrfs_root *dest = BTRFS_I(new_dir)->root;
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	u64 index = 0;
	u64 root_objectid;
	int ret;
	u64 old_iyes = btrfs_iyes(BTRFS_I(old_iyesde));
	bool log_pinned = false;
	struct btrfs_log_ctx ctx;
	bool sync_log = false;
	bool commit_transaction = false;

	if (btrfs_iyes(BTRFS_I(new_dir)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)
		return -EPERM;

	/* we only allow rename subvolume link between subvolumes */
	if (old_iyes != BTRFS_FIRST_FREE_OBJECTID && root != dest)
		return -EXDEV;

	if (old_iyes == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID ||
	    (new_iyesde && btrfs_iyes(BTRFS_I(new_iyesde)) == BTRFS_FIRST_FREE_OBJECTID))
		return -ENOTEMPTY;

	if (S_ISDIR(old_iyesde->i_mode) && new_iyesde &&
	    new_iyesde->i_size > BTRFS_EMPTY_DIR_SIZE)
		return -ENOTEMPTY;


	/* check for collisions, even if the  name isn't there */
	ret = btrfs_check_dir_item_collision(dest, new_dir->i_iyes,
			     new_dentry->d_name.name,
			     new_dentry->d_name.len);

	if (ret) {
		if (ret == -EEXIST) {
			/* we shouldn't get
			 * eexist without a new_iyesde */
			if (WARN_ON(!new_iyesde)) {
				return ret;
			}
		} else {
			/* maybe -EOVERFLOW */
			return ret;
		}
	}
	ret = 0;

	/*
	 * we're using rename to replace one file with ayesther.  Start IO on it
	 * yesw so  we don't add too much work to the end of the transaction
	 */
	if (new_iyesde && S_ISREG(old_iyesde->i_mode) && new_iyesde->i_size)
		filemap_flush(old_iyesde->i_mapping);

	/* close the racy window with snapshot create/destroy ioctl */
	if (old_iyes == BTRFS_FIRST_FREE_OBJECTID)
		down_read(&fs_info->subvol_sem);
	/*
	 * We want to reserve the absolute worst case amount of items.  So if
	 * both iyesdes are subvols and we need to unlink them then that would
	 * require 4 item modifications, but if they are both yesrmal iyesdes it
	 * would require 5 item modifications, so we'll assume they are yesrmal
	 * iyesdes.  So 5 * 2 is 10, plus 1 for the new link, so 11 total items
	 * should cover the worst case number of items we'll modify.
	 * If our rename has the whiteout flag, we need more 5 units for the
	 * new iyesde (1 iyesde item, 1 iyesde ref, 2 dir items and 1 xattr item
	 * when selinux is enabled).
	 */
	trans_num_items = 11;
	if (flags & RENAME_WHITEOUT)
		trans_num_items += 5;
	trans = btrfs_start_transaction(root, trans_num_items);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_yestrans;
	}

	if (dest != root)
		btrfs_record_root_in_trans(trans, dest);

	ret = btrfs_set_iyesde_index(BTRFS_I(new_dir), &index);
	if (ret)
		goto out_fail;

	BTRFS_I(old_iyesde)->dir_index = 0ULL;
	if (unlikely(old_iyes == BTRFS_FIRST_FREE_OBJECTID)) {
		/* force full log commit if subvolume involved. */
		btrfs_set_log_full_commit(trans);
	} else {
		btrfs_pin_log_trans(root);
		log_pinned = true;
		ret = btrfs_insert_iyesde_ref(trans, dest,
					     new_dentry->d_name.name,
					     new_dentry->d_name.len,
					     old_iyes,
					     btrfs_iyes(BTRFS_I(new_dir)), index);
		if (ret)
			goto out_fail;
	}

	iyesde_inc_iversion(old_dir);
	iyesde_inc_iversion(new_dir);
	iyesde_inc_iversion(old_iyesde);
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	old_iyesde->i_ctime = current_time(old_dir);

	if (old_dentry->d_parent != new_dentry->d_parent)
		btrfs_record_unlink_dir(trans, BTRFS_I(old_dir),
				BTRFS_I(old_iyesde), 1);

	if (unlikely(old_iyes == BTRFS_FIRST_FREE_OBJECTID)) {
		root_objectid = BTRFS_I(old_iyesde)->root->root_key.objectid;
		ret = btrfs_unlink_subvol(trans, old_dir, root_objectid,
					old_dentry->d_name.name,
					old_dentry->d_name.len);
	} else {
		ret = __btrfs_unlink_iyesde(trans, root, BTRFS_I(old_dir),
					BTRFS_I(d_iyesde(old_dentry)),
					old_dentry->d_name.name,
					old_dentry->d_name.len);
		if (!ret)
			ret = btrfs_update_iyesde(trans, root, old_iyesde);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (new_iyesde) {
		iyesde_inc_iversion(new_iyesde);
		new_iyesde->i_ctime = current_time(new_iyesde);
		if (unlikely(btrfs_iyes(BTRFS_I(new_iyesde)) ==
			     BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
			root_objectid = BTRFS_I(new_iyesde)->location.objectid;
			ret = btrfs_unlink_subvol(trans, new_dir, root_objectid,
						new_dentry->d_name.name,
						new_dentry->d_name.len);
			BUG_ON(new_iyesde->i_nlink == 0);
		} else {
			ret = btrfs_unlink_iyesde(trans, dest, BTRFS_I(new_dir),
						 BTRFS_I(d_iyesde(new_dentry)),
						 new_dentry->d_name.name,
						 new_dentry->d_name.len);
		}
		if (!ret && new_iyesde->i_nlink == 0)
			ret = btrfs_orphan_add(trans,
					BTRFS_I(d_iyesde(new_dentry)));
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_fail;
		}
	}

	ret = btrfs_add_link(trans, BTRFS_I(new_dir), BTRFS_I(old_iyesde),
			     new_dentry->d_name.name,
			     new_dentry->d_name.len, 0, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_iyesde->i_nlink == 1)
		BTRFS_I(old_iyesde)->dir_index = index;

	if (log_pinned) {
		struct dentry *parent = new_dentry->d_parent;

		btrfs_init_log_ctx(&ctx, old_iyesde);
		ret = btrfs_log_new_name(trans, BTRFS_I(old_iyesde),
					 BTRFS_I(old_dir), parent,
					 false, &ctx);
		if (ret == BTRFS_NEED_LOG_SYNC)
			sync_log = true;
		else if (ret == BTRFS_NEED_TRANS_COMMIT)
			commit_transaction = true;
		ret = 0;
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
	 * commit if the log currently contains any of the iyesdes involved in
	 * this rename operation (to ensure we do yest persist a log with an
	 * inconsistent state for any of these iyesdes or leading to any
	 * inconsistencies when replayed). If the transaction was aborted, the
	 * abortion reason is propagated to userspace when attempting to commit
	 * the transaction. If the log does yest contain any of these iyesdes, we
	 * allow the tasks to sync it.
	 */
	if (ret && log_pinned) {
		if (btrfs_iyesde_in_log(BTRFS_I(old_dir), fs_info->generation) ||
		    btrfs_iyesde_in_log(BTRFS_I(new_dir), fs_info->generation) ||
		    btrfs_iyesde_in_log(BTRFS_I(old_iyesde), fs_info->generation) ||
		    (new_iyesde &&
		     btrfs_iyesde_in_log(BTRFS_I(new_iyesde), fs_info->generation)))
			btrfs_set_log_full_commit(trans);

		btrfs_end_log_trans(root);
		log_pinned = false;
	}
	if (!ret && sync_log) {
		ret = btrfs_sync_log(trans, BTRFS_I(old_iyesde)->root, &ctx);
		if (ret)
			commit_transaction = true;
	}
	if (commit_transaction) {
		ret = btrfs_commit_transaction(trans);
	} else {
		int ret2;

		ret2 = btrfs_end_transaction(trans);
		ret = ret ? ret : ret2;
	}
out_yestrans:
	if (old_iyes == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	return ret;
}

static int btrfs_rename2(struct iyesde *old_dir, struct dentry *old_dentry,
			 struct iyesde *new_dir, struct dentry *new_dentry,
			 unsigned int flags)
{
	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return btrfs_rename_exchange(old_dir, old_dentry, new_dir,
					  new_dentry);

	return btrfs_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

struct btrfs_delalloc_work {
	struct iyesde *iyesde;
	struct completion completion;
	struct list_head list;
	struct btrfs_work work;
};

static void btrfs_run_delalloc_work(struct btrfs_work *work)
{
	struct btrfs_delalloc_work *delalloc_work;
	struct iyesde *iyesde;

	delalloc_work = container_of(work, struct btrfs_delalloc_work,
				     work);
	iyesde = delalloc_work->iyesde;
	filemap_flush(iyesde->i_mapping);
	if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
				&BTRFS_I(iyesde)->runtime_flags))
		filemap_flush(iyesde->i_mapping);

	iput(iyesde);
	complete(&delalloc_work->completion);
}

static struct btrfs_delalloc_work *btrfs_alloc_delalloc_work(struct iyesde *iyesde)
{
	struct btrfs_delalloc_work *work;

	work = kmalloc(sizeof(*work), GFP_NOFS);
	if (!work)
		return NULL;

	init_completion(&work->completion);
	INIT_LIST_HEAD(&work->list);
	work->iyesde = iyesde;
	btrfs_init_work(&work->work, btrfs_run_delalloc_work, NULL, NULL);

	return work;
}

/*
 * some fairly slow code that needs optimization. This walks the list
 * of all the iyesdes with pending delalloc and forces them to disk.
 */
static int start_delalloc_iyesdes(struct btrfs_root *root, int nr, bool snapshot)
{
	struct btrfs_iyesde *biyesde;
	struct iyesde *iyesde;
	struct btrfs_delalloc_work *work, *next;
	struct list_head works;
	struct list_head splice;
	int ret = 0;

	INIT_LIST_HEAD(&works);
	INIT_LIST_HEAD(&splice);

	mutex_lock(&root->delalloc_mutex);
	spin_lock(&root->delalloc_lock);
	list_splice_init(&root->delalloc_iyesdes, &splice);
	while (!list_empty(&splice)) {
		biyesde = list_entry(splice.next, struct btrfs_iyesde,
				    delalloc_iyesdes);

		list_move_tail(&biyesde->delalloc_iyesdes,
			       &root->delalloc_iyesdes);
		iyesde = igrab(&biyesde->vfs_iyesde);
		if (!iyesde) {
			cond_resched_lock(&root->delalloc_lock);
			continue;
		}
		spin_unlock(&root->delalloc_lock);

		if (snapshot)
			set_bit(BTRFS_INODE_SNAPSHOT_FLUSH,
				&biyesde->runtime_flags);
		work = btrfs_alloc_delalloc_work(iyesde);
		if (!work) {
			iput(iyesde);
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
		wait_for_completion(&work->completion);
		kfree(work);
	}

	if (!list_empty(&splice)) {
		spin_lock(&root->delalloc_lock);
		list_splice_tail(&splice, &root->delalloc_iyesdes);
		spin_unlock(&root->delalloc_lock);
	}
	mutex_unlock(&root->delalloc_mutex);
	return ret;
}

int btrfs_start_delalloc_snapshot(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state))
		return -EROFS;

	ret = start_delalloc_iyesdes(root, -1, true);
	if (ret > 0)
		ret = 0;
	return ret;
}

int btrfs_start_delalloc_roots(struct btrfs_fs_info *fs_info, int nr)
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

		ret = start_delalloc_iyesdes(root, nr, false);
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
out:
	if (!list_empty(&splice)) {
		spin_lock(&fs_info->delalloc_root_lock);
		list_splice_tail(&splice, &fs_info->delalloc_roots);
		spin_unlock(&fs_info->delalloc_root_lock);
	}
	mutex_unlock(&fs_info->delalloc_root_mutex);
	return ret;
}

static int btrfs_symlink(struct iyesde *dir, struct dentry *dentry,
			 const char *symname)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct iyesde *iyesde = NULL;
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
	 * 2 items for iyesde item and ref
	 * 2 items for dir items
	 * 1 item for updating parent iyesde item
	 * 1 item for the inline extent item
	 * 1 item for xattr if selinux is on
	 */
	trans = btrfs_start_transaction(root, 7);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	err = btrfs_find_free_iyes(root, &objectid);
	if (err)
		goto out_unlock;

	iyesde = btrfs_new_iyesde(trans, root, dir, dentry->d_name.name,
				dentry->d_name.len, btrfs_iyes(BTRFS_I(dir)),
				objectid, S_IFLNK|S_IRWXUGO, &index);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		iyesde = NULL;
		goto out_unlock;
	}

	/*
	* If the active LSM wants to access the iyesde during
	* d_instantiate it needs these. Smack checks to see
	* if the filesystem supports xattrs by looking at the
	* ops vector.
	*/
	iyesde->i_fop = &btrfs_file_operations;
	iyesde->i_op = &btrfs_file_iyesde_operations;
	iyesde->i_mapping->a_ops = &btrfs_aops;
	BTRFS_I(iyesde)->io_tree.ops = &btrfs_extent_io_ops;

	err = btrfs_init_iyesde_security(trans, iyesde, dir, &dentry->d_name);
	if (err)
		goto out_unlock;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out_unlock;
	}
	key.objectid = btrfs_iyes(BTRFS_I(iyesde));
	key.offset = 0;
	key.type = BTRFS_EXTENT_DATA_KEY;
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (err) {
		btrfs_free_path(path);
		goto out_unlock;
	}
	leaf = path->yesdes[0];
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

	iyesde->i_op = &btrfs_symlink_iyesde_operations;
	iyesde_yeshighmem(iyesde);
	iyesde_set_bytes(iyesde, name_len);
	btrfs_i_size_write(BTRFS_I(iyesde), name_len);
	err = btrfs_update_iyesde(trans, root, iyesde);
	/*
	 * Last step, add directory indexes for our symlink iyesde. This is the
	 * last step to avoid extra cleanup of these indexes if an error happens
	 * elsewhere above.
	 */
	if (!err)
		err = btrfs_add_yesndir(trans, BTRFS_I(dir), dentry,
				BTRFS_I(iyesde), 0, index);
	if (err)
		goto out_unlock;

	d_instantiate_new(dentry, iyesde);

out_unlock:
	btrfs_end_transaction(trans);
	if (err && iyesde) {
		iyesde_dec_link_count(iyesde);
		discard_new_iyesde(iyesde);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static int __btrfs_prealloc_file_range(struct iyesde *iyesde, int mode,
				       u64 start, u64 num_bytes, u64 min_size,
				       loff_t actual_len, u64 *alloc_hint,
				       struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(iyesde->i_sb);
	struct extent_map_tree *em_tree = &BTRFS_I(iyesde)->extent_tree;
	struct extent_map *em;
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
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
		ret = insert_reserved_file_extent(trans, iyesde,
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

		btrfs_drop_extent_cache(BTRFS_I(iyesde), cur_offset,
					cur_offset + ins.offset -1, 0);

		em = alloc_extent_map();
		if (!em) {
			set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
				&BTRFS_I(iyesde)->runtime_flags);
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
			btrfs_drop_extent_cache(BTRFS_I(iyesde), cur_offset,
						cur_offset + ins.offset - 1,
						0);
		}
		free_extent_map(em);
next:
		num_bytes -= ins.offset;
		cur_offset += ins.offset;
		*alloc_hint = ins.objectid + ins.offset;

		iyesde_inc_iversion(iyesde);
		iyesde->i_ctime = current_time(iyesde);
		BTRFS_I(iyesde)->flags |= BTRFS_INODE_PREALLOC;
		if (!(mode & FALLOC_FL_KEEP_SIZE) &&
		    (actual_len > iyesde->i_size) &&
		    (cur_offset > iyesde->i_size)) {
			if (cur_offset > actual_len)
				i_size = actual_len;
			else
				i_size = cur_offset;
			i_size_write(iyesde, i_size);
			btrfs_ordered_update_i_size(iyesde, i_size, NULL);
		}

		ret = btrfs_update_iyesde(trans, root, iyesde);

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
		btrfs_free_reserved_data_space(iyesde, NULL, cur_offset,
			end - cur_offset + 1);
	return ret;
}

int btrfs_prealloc_file_range(struct iyesde *iyesde, int mode,
			      u64 start, u64 num_bytes, u64 min_size,
			      loff_t actual_len, u64 *alloc_hint)
{
	return __btrfs_prealloc_file_range(iyesde, mode, start, num_bytes,
					   min_size, actual_len, alloc_hint,
					   NULL);
}

int btrfs_prealloc_file_range_trans(struct iyesde *iyesde,
				    struct btrfs_trans_handle *trans, int mode,
				    u64 start, u64 num_bytes, u64 min_size,
				    loff_t actual_len, u64 *alloc_hint)
{
	return __btrfs_prealloc_file_range(iyesde, mode, start, num_bytes,
					   min_size, actual_len, alloc_hint, trans);
}

static int btrfs_set_page_dirty(struct page *page)
{
	return __set_page_dirty_yesbuffers(page);
}

static int btrfs_permission(struct iyesde *iyesde, int mask)
{
	struct btrfs_root *root = BTRFS_I(iyesde)->root;
	umode_t mode = iyesde->i_mode;

	if (mask & MAY_WRITE &&
	    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode))) {
		if (btrfs_root_readonly(root))
			return -EROFS;
		if (BTRFS_I(iyesde)->flags & BTRFS_INODE_READONLY)
			return -EACCES;
	}
	return generic_permission(iyesde, mask);
}

static int btrfs_tmpfile(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct iyesde *iyesde = NULL;
	u64 objectid;
	u64 index;
	int ret = 0;

	/*
	 * 5 units required for adding orphan entry
	 */
	trans = btrfs_start_transaction(root, 5);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_find_free_iyes(root, &objectid);
	if (ret)
		goto out;

	iyesde = btrfs_new_iyesde(trans, root, dir, NULL, 0,
			btrfs_iyes(BTRFS_I(dir)), objectid, mode, &index);
	if (IS_ERR(iyesde)) {
		ret = PTR_ERR(iyesde);
		iyesde = NULL;
		goto out;
	}

	iyesde->i_fop = &btrfs_file_operations;
	iyesde->i_op = &btrfs_file_iyesde_operations;

	iyesde->i_mapping->a_ops = &btrfs_aops;
	BTRFS_I(iyesde)->io_tree.ops = &btrfs_extent_io_ops;

	ret = btrfs_init_iyesde_security(trans, iyesde, dir, NULL);
	if (ret)
		goto out;

	ret = btrfs_update_iyesde(trans, root, iyesde);
	if (ret)
		goto out;
	ret = btrfs_orphan_add(trans, BTRFS_I(iyesde));
	if (ret)
		goto out;

	/*
	 * We set number of links to 0 in btrfs_new_iyesde(), and here we set
	 * it to 1 because d_tmpfile() will issue a warning if the count is 0,
	 * through:
	 *
	 *    d_tmpfile() -> iyesde_dec_link_count() -> drop_nlink()
	 */
	set_nlink(iyesde, 1);
	d_tmpfile(dentry, iyesde);
	unlock_new_iyesde(iyesde);
	mark_iyesde_dirty(iyesde);
out:
	btrfs_end_transaction(trans);
	if (ret && iyesde)
		discard_new_iyesde(iyesde);
	btrfs_btree_balance_dirty(fs_info);
	return ret;
}

void btrfs_set_range_writeback(struct extent_io_tree *tree, u64 start, u64 end)
{
	struct iyesde *iyesde = tree->private_data;
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	struct page *page;

	while (index <= end_index) {
		page = find_get_page(iyesde->i_mapping, index);
		ASSERT(page); /* Pages should be in the extent_io_tree */
		set_page_writeback(page);
		put_page(page);
		index++;
	}
}

#ifdef CONFIG_SWAP
/*
 * Add an entry indicating a block group or device which is pinned by a
 * swapfile. Returns 0 on success, 1 if there is already an entry for it, or a
 * negative erryes on failure.
 */
static int btrfs_add_swapfile_pin(struct iyesde *iyesde, void *ptr,
				  bool is_block_group)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(iyesde)->root->fs_info;
	struct btrfs_swapfile_pin *sp, *entry;
	struct rb_yesde **p;
	struct rb_yesde *parent = NULL;

	sp = kmalloc(sizeof(*sp), GFP_NOFS);
	if (!sp)
		return -ENOMEM;
	sp->ptr = ptr;
	sp->iyesde = iyesde;
	sp->is_block_group = is_block_group;

	spin_lock(&fs_info->swapfile_pins_lock);
	p = &fs_info->swapfile_pins.rb_yesde;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct btrfs_swapfile_pin, yesde);
		if (sp->ptr < entry->ptr ||
		    (sp->ptr == entry->ptr && sp->iyesde < entry->iyesde)) {
			p = &(*p)->rb_left;
		} else if (sp->ptr > entry->ptr ||
			   (sp->ptr == entry->ptr && sp->iyesde > entry->iyesde)) {
			p = &(*p)->rb_right;
		} else {
			spin_unlock(&fs_info->swapfile_pins_lock);
			kfree(sp);
			return 1;
		}
	}
	rb_link_yesde(&sp->yesde, parent, p);
	rb_insert_color(&sp->yesde, &fs_info->swapfile_pins);
	spin_unlock(&fs_info->swapfile_pins_lock);
	return 0;
}

/* Free all of the entries pinned by this swapfile. */
static void btrfs_free_swapfile_pins(struct iyesde *iyesde)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(iyesde)->root->fs_info;
	struct btrfs_swapfile_pin *sp;
	struct rb_yesde *yesde, *next;

	spin_lock(&fs_info->swapfile_pins_lock);
	yesde = rb_first(&fs_info->swapfile_pins);
	while (yesde) {
		next = rb_next(yesde);
		sp = rb_entry(yesde, struct btrfs_swapfile_pin, yesde);
		if (sp->iyesde == iyesde) {
			rb_erase(&sp->yesde, &fs_info->swapfile_pins);
			if (sp->is_block_group)
				btrfs_put_block_group(sp->ptr);
			kfree(sp);
		}
		yesde = next;
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
	struct iyesde *iyesde = file_iyesde(file);

	btrfs_free_swapfile_pins(iyesde);
	atomic_dec(&BTRFS_I(iyesde)->root->nr_swapfiles);
}

static int btrfs_swap_activate(struct swap_info_struct *sis, struct file *file,
			       sector_t *span)
{
	struct iyesde *iyesde = file_iyesde(file);
	struct btrfs_fs_info *fs_info = BTRFS_I(iyesde)->root->fs_info;
	struct extent_io_tree *io_tree = &BTRFS_I(iyesde)->io_tree;
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
	ret = btrfs_wait_ordered_range(iyesde, 0, (u64)-1);
	if (ret)
		return ret;

	/*
	 * The iyesde is locked, so these flags won't change after we check them.
	 */
	if (BTRFS_I(iyesde)->flags & BTRFS_INODE_COMPRESS) {
		btrfs_warn(fs_info, "swapfile must yest be compressed");
		return -EINVAL;
	}
	if (!(BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATACOW)) {
		btrfs_warn(fs_info, "swapfile must yest be copy-on-write");
		return -EINVAL;
	}
	if (!(BTRFS_I(iyesde)->flags & BTRFS_INODE_NODATASUM)) {
		btrfs_warn(fs_info, "swapfile must yest be checksummed");
		return -EINVAL;
	}

	/*
	 * Balance or device remove/replace/resize can move stuff around from
	 * under us. The EXCL_OP flag makes sure they aren't running/won't run
	 * concurrently while we are mapping the swap extents, and
	 * fs_info->swapfile_pins prevents them from running while the swap file
	 * is active and moving the extents. Note that this also prevents a
	 * concurrent device add which isn't actually necessary, but it's yest
	 * really worth the trouble to allow it.
	 */
	if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags)) {
		btrfs_warn(fs_info,
	   "canyest activate swapfile while exclusive operation is running");
		return -EBUSY;
	}
	/*
	 * Snapshots can create extents which require COW even if NODATACOW is
	 * set. We use this counter to prevent snapshots. We must increment it
	 * before walking the extents because we don't want a concurrent
	 * snapshot to run after we've already checked the extents.
	 */
	atomic_inc(&BTRFS_I(iyesde)->root->nr_swapfiles);

	isize = ALIGN_DOWN(iyesde->i_size, fs_info->sectorsize);

	lock_extent_bits(io_tree, 0, isize - 1, &cached_state);
	start = 0;
	while (start < isize) {
		u64 logical_block_start, physical_block_start;
		struct btrfs_block_group *bg;
		u64 len = isize - start;

		em = btrfs_get_extent(BTRFS_I(iyesde), NULL, 0, start, len, 0);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out;
		}

		if (em->block_start == EXTENT_MAP_HOLE) {
			btrfs_warn(fs_info, "swapfile must yest have holes");
			ret = -EINVAL;
			goto out;
		}
		if (em->block_start == EXTENT_MAP_INLINE) {
			/*
			 * It's unlikely we'll ever actually find ourselves
			 * here, as a file small eyesugh to fit inline won't be
			 * big eyesugh to store more than the swap header, but in
			 * case something changes in the future, let's catch it
			 * here rather than later.
			 */
			btrfs_warn(fs_info, "swapfile must yest be inline");
			ret = -EINVAL;
			goto out;
		}
		if (test_bit(EXTENT_FLAG_COMPRESSED, &em->flags)) {
			btrfs_warn(fs_info, "swapfile must yest be compressed");
			ret = -EINVAL;
			goto out;
		}

		logical_block_start = em->block_start + (start - em->start);
		len = min(len, em->len - (start - em->start));
		free_extent_map(em);
		em = NULL;

		ret = can_yescow_extent(iyesde, start, &len, NULL, NULL, NULL);
		if (ret < 0) {
			goto out;
		} else if (ret) {
			ret = 0;
		} else {
			btrfs_warn(fs_info,
				   "swapfile must yest be copy-on-write");
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
			ret = btrfs_add_swapfile_pin(iyesde, device, false);
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
			   "could yest find block group containing swapfile");
			ret = -EINVAL;
			goto out;
		}

		ret = btrfs_add_swapfile_pin(iyesde, bg, true);
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

	clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);

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

static const struct iyesde_operations btrfs_dir_iyesde_operations = {
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
	.mkyesd		= btrfs_mkyesd,
	.listxattr	= btrfs_listxattr,
	.permission	= btrfs_permission,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
	.tmpfile        = btrfs_tmpfile,
};
static const struct iyesde_operations btrfs_dir_ro_iyesde_operations = {
	.lookup		= btrfs_lookup,
	.permission	= btrfs_permission,
	.update_time	= btrfs_update_time,
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

static const struct extent_io_ops btrfs_extent_io_ops = {
	/* mandatory callbacks */
	.submit_bio_hook = btrfs_submit_bio_hook,
	.readpage_end_io_hook = btrfs_readpage_end_io_hook,
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
 * For yesw we're avoiding this by dropping bmap.
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
	.swap_activate	= btrfs_swap_activate,
	.swap_deactivate = btrfs_swap_deactivate,
};

static const struct iyesde_operations btrfs_file_iyesde_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.listxattr      = btrfs_listxattr,
	.permission	= btrfs_permission,
	.fiemap		= btrfs_fiemap,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
};
static const struct iyesde_operations btrfs_special_iyesde_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.permission	= btrfs_permission,
	.listxattr	= btrfs_listxattr,
	.get_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
};
static const struct iyesde_operations btrfs_symlink_iyesde_operations = {
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
