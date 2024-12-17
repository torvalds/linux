// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <crypto/hash.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blk-cgroup.h>
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
#include <linux/unaligned.h>
#include <linux/fsverity.h>
#include "misc.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ordered-data.h"
#include "xattr.h"
#include "tree-log.h"
#include "bio.h"
#include "compression.h"
#include "locking.h"
#include "props.h"
#include "qgroup.h"
#include "delalloc-space.h"
#include "block-group.h"
#include "space-info.h"
#include "zoned.h"
#include "subpage.h"
#include "inode-item.h"
#include "fs.h"
#include "accessors.h"
#include "extent-tree.h"
#include "root-tree.h"
#include "defrag.h"
#include "dir-item.h"
#include "file-item.h"
#include "uuid-tree.h"
#include "ioctl.h"
#include "file.h"
#include "acl.h"
#include "relocation.h"
#include "verity.h"
#include "super.h"
#include "orphan.h"
#include "backref.h"
#include "raid-stripe-tree.h"
#include "fiemap.h"

struct btrfs_iget_args {
	u64 ino;
	struct btrfs_root *root;
};

struct btrfs_rename_ctx {
	/* Output field. Stores the index number of the old directory entry. */
	u64 index;
};

/*
 * Used by data_reloc_print_warning_inode() to pass needed info for filename
 * resolution and output of error message.
 */
struct data_reloc_warn {
	struct btrfs_path path;
	struct btrfs_fs_info *fs_info;
	u64 extent_item_size;
	u64 logical;
	int mirror_num;
};

/*
 * For the file_extent_tree, we want to hold the inode lock when we lookup and
 * update the disk_i_size, but lockdep will complain because our io_tree we hold
 * the tree lock and get the inode lock when setting delalloc. These two things
 * are unrelated, so make a class for the file_extent_tree so we don't get the
 * two locking patterns mixed up.
 */
static struct lock_class_key file_extent_tree_class;

static const struct inode_operations btrfs_dir_inode_operations;
static const struct inode_operations btrfs_symlink_inode_operations;
static const struct inode_operations btrfs_special_inode_operations;
static const struct inode_operations btrfs_file_inode_operations;
static const struct address_space_operations btrfs_aops;
static const struct file_operations btrfs_dir_file_operations;

static struct kmem_cache *btrfs_inode_cachep;

static int btrfs_setsize(struct inode *inode, struct iattr *attr);
static int btrfs_truncate(struct btrfs_inode *inode, bool skip_writeback);

static noinline int run_delalloc_cow(struct btrfs_inode *inode,
				     struct folio *locked_folio, u64 start,
				     u64 end, struct writeback_control *wbc,
				     bool pages_dirty);

static int data_reloc_print_warning_inode(u64 inum, u64 offset, u64 num_bytes,
					  u64 root, void *warn_ctx)
{
	struct data_reloc_warn *warn = warn_ctx;
	struct btrfs_fs_info *fs_info = warn->fs_info;
	struct extent_buffer *eb;
	struct btrfs_inode_item *inode_item;
	struct inode_fs_paths *ipath = NULL;
	struct btrfs_root *local_root;
	struct btrfs_key key;
	unsigned int nofs_flag;
	u32 nlink;
	int ret;

	local_root = btrfs_get_fs_root(fs_info, root, true);
	if (IS_ERR(local_root)) {
		ret = PTR_ERR(local_root);
		goto err;
	}

	/* This makes the path point to (inum INODE_ITEM ioff). */
	key.objectid = inum;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, local_root, &key, &warn->path, 0, 0);
	if (ret) {
		btrfs_put_root(local_root);
		btrfs_release_path(&warn->path);
		goto err;
	}

	eb = warn->path.nodes[0];
	inode_item = btrfs_item_ptr(eb, warn->path.slots[0], struct btrfs_inode_item);
	nlink = btrfs_inode_nlink(eb, inode_item);
	btrfs_release_path(&warn->path);

	nofs_flag = memalloc_nofs_save();
	ipath = init_ipath(4096, local_root, &warn->path);
	memalloc_nofs_restore(nofs_flag);
	if (IS_ERR(ipath)) {
		btrfs_put_root(local_root);
		ret = PTR_ERR(ipath);
		ipath = NULL;
		/*
		 * -ENOMEM, not a critical error, just output an generic error
		 * without filename.
		 */
		btrfs_warn(fs_info,
"checksum error at logical %llu mirror %u root %llu, inode %llu offset %llu",
			   warn->logical, warn->mirror_num, root, inum, offset);
		return ret;
	}
	ret = paths_from_inode(inum, ipath);
	if (ret < 0)
		goto err;

	/*
	 * We deliberately ignore the bit ipath might have been too small to
	 * hold all of the paths here
	 */
	for (int i = 0; i < ipath->fspath->elem_cnt; i++) {
		btrfs_warn(fs_info,
"checksum error at logical %llu mirror %u root %llu inode %llu offset %llu length %u links %u (path: %s)",
			   warn->logical, warn->mirror_num, root, inum, offset,
			   fs_info->sectorsize, nlink,
			   (char *)(unsigned long)ipath->fspath->val[i]);
	}

	btrfs_put_root(local_root);
	free_ipath(ipath);
	return 0;

err:
	btrfs_warn(fs_info,
"checksum error at logical %llu mirror %u root %llu inode %llu offset %llu, path resolving failed with ret=%d",
		   warn->logical, warn->mirror_num, root, inum, offset, ret);

	free_ipath(ipath);
	return ret;
}

/*
 * Do extra user-friendly error output (e.g. lookup all the affected files).
 *
 * Return true if we succeeded doing the backref lookup.
 * Return false if such lookup failed, and has to fallback to the old error message.
 */
static void print_data_reloc_error(const struct btrfs_inode *inode, u64 file_off,
				   const u8 *csum, const u8 *csum_expected,
				   int mirror_num)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_path path = { 0 };
	struct btrfs_key found_key = { 0 };
	struct extent_buffer *eb;
	struct btrfs_extent_item *ei;
	const u32 csum_size = fs_info->csum_size;
	u64 logical;
	u64 flags;
	u32 item_size;
	int ret;

	mutex_lock(&fs_info->reloc_mutex);
	logical = btrfs_get_reloc_bg_bytenr(fs_info);
	mutex_unlock(&fs_info->reloc_mutex);

	if (logical == U64_MAX) {
		btrfs_warn_rl(fs_info, "has data reloc tree but no running relocation");
		btrfs_warn_rl(fs_info,
"csum failed root %lld ino %llu off %llu csum " CSUM_FMT " expected csum " CSUM_FMT " mirror %d",
			btrfs_root_id(inode->root), btrfs_ino(inode), file_off,
			CSUM_FMT_VALUE(csum_size, csum),
			CSUM_FMT_VALUE(csum_size, csum_expected),
			mirror_num);
		return;
	}

	logical += file_off;
	btrfs_warn_rl(fs_info,
"csum failed root %lld ino %llu off %llu logical %llu csum " CSUM_FMT " expected csum " CSUM_FMT " mirror %d",
			btrfs_root_id(inode->root),
			btrfs_ino(inode), file_off, logical,
			CSUM_FMT_VALUE(csum_size, csum),
			CSUM_FMT_VALUE(csum_size, csum_expected),
			mirror_num);

	ret = extent_from_logical(fs_info, logical, &path, &found_key, &flags);
	if (ret < 0) {
		btrfs_err_rl(fs_info, "failed to lookup extent item for logical %llu: %d",
			     logical, ret);
		return;
	}
	eb = path.nodes[0];
	ei = btrfs_item_ptr(eb, path.slots[0], struct btrfs_extent_item);
	item_size = btrfs_item_size(eb, path.slots[0]);
	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		unsigned long ptr = 0;
		u64 ref_root;
		u8 ref_level;

		while (true) {
			ret = tree_backref_for_extent(&ptr, eb, &found_key, ei,
						      item_size, &ref_root,
						      &ref_level);
			if (ret < 0) {
				btrfs_warn_rl(fs_info,
				"failed to resolve tree backref for logical %llu: %d",
					      logical, ret);
				break;
			}
			if (ret > 0)
				break;

			btrfs_warn_rl(fs_info,
"csum error at logical %llu mirror %u: metadata %s (level %d) in tree %llu",
				logical, mirror_num,
				(ref_level ? "node" : "leaf"),
				ref_level, ref_root);
		}
		btrfs_release_path(&path);
	} else {
		struct btrfs_backref_walk_ctx ctx = { 0 };
		struct data_reloc_warn reloc_warn = { 0 };

		btrfs_release_path(&path);

		ctx.bytenr = found_key.objectid;
		ctx.extent_item_pos = logical - found_key.objectid;
		ctx.fs_info = fs_info;

		reloc_warn.logical = logical;
		reloc_warn.extent_item_size = found_key.offset;
		reloc_warn.mirror_num = mirror_num;
		reloc_warn.fs_info = fs_info;

		iterate_extent_inodes(&ctx, true,
				      data_reloc_print_warning_inode, &reloc_warn);
	}
}

static void __cold btrfs_print_data_csum_error(struct btrfs_inode *inode,
		u64 logical_start, u8 *csum, u8 *csum_expected, int mirror_num)
{
	struct btrfs_root *root = inode->root;
	const u32 csum_size = root->fs_info->csum_size;

	/* For data reloc tree, it's better to do a backref lookup instead. */
	if (btrfs_root_id(root) == BTRFS_DATA_RELOC_TREE_OBJECTID)
		return print_data_reloc_error(inode, logical_start, csum,
					      csum_expected, mirror_num);

	/* Output without objectid, which is more meaningful */
	if (btrfs_root_id(root) >= BTRFS_LAST_FREE_OBJECTID) {
		btrfs_warn_rl(root->fs_info,
"csum failed root %lld ino %lld off %llu csum " CSUM_FMT " expected csum " CSUM_FMT " mirror %d",
			btrfs_root_id(root), btrfs_ino(inode),
			logical_start,
			CSUM_FMT_VALUE(csum_size, csum),
			CSUM_FMT_VALUE(csum_size, csum_expected),
			mirror_num);
	} else {
		btrfs_warn_rl(root->fs_info,
"csum failed root %llu ino %llu off %llu csum " CSUM_FMT " expected csum " CSUM_FMT " mirror %d",
			btrfs_root_id(root), btrfs_ino(inode),
			logical_start,
			CSUM_FMT_VALUE(csum_size, csum),
			CSUM_FMT_VALUE(csum_size, csum_expected),
			mirror_num);
	}
}

/*
 * Lock inode i_rwsem based on arguments passed.
 *
 * ilock_flags can have the following bit set:
 *
 * BTRFS_ILOCK_SHARED - acquire a shared lock on the inode
 * BTRFS_ILOCK_TRY - try to acquire the lock, if fails on first attempt
 *		     return -EAGAIN
 * BTRFS_ILOCK_MMAP - acquire a write lock on the i_mmap_lock
 */
int btrfs_inode_lock(struct btrfs_inode *inode, unsigned int ilock_flags)
{
	if (ilock_flags & BTRFS_ILOCK_SHARED) {
		if (ilock_flags & BTRFS_ILOCK_TRY) {
			if (!inode_trylock_shared(&inode->vfs_inode))
				return -EAGAIN;
			else
				return 0;
		}
		inode_lock_shared(&inode->vfs_inode);
	} else {
		if (ilock_flags & BTRFS_ILOCK_TRY) {
			if (!inode_trylock(&inode->vfs_inode))
				return -EAGAIN;
			else
				return 0;
		}
		inode_lock(&inode->vfs_inode);
	}
	if (ilock_flags & BTRFS_ILOCK_MMAP)
		down_write(&inode->i_mmap_lock);
	return 0;
}

/*
 * Unock inode i_rwsem.
 *
 * ilock_flags should contain the same bits set as passed to btrfs_inode_lock()
 * to decide whether the lock acquired is shared or exclusive.
 */
void btrfs_inode_unlock(struct btrfs_inode *inode, unsigned int ilock_flags)
{
	if (ilock_flags & BTRFS_ILOCK_MMAP)
		up_write(&inode->i_mmap_lock);
	if (ilock_flags & BTRFS_ILOCK_SHARED)
		inode_unlock_shared(&inode->vfs_inode);
	else
		inode_unlock(&inode->vfs_inode);
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
						 struct folio *locked_folio,
						 u64 offset, u64 bytes)
{
	unsigned long index = offset >> PAGE_SHIFT;
	unsigned long end_index = (offset + bytes - 1) >> PAGE_SHIFT;
	u64 page_start = 0, page_end = 0;
	struct folio *folio;

	if (locked_folio) {
		page_start = folio_pos(locked_folio);
		page_end = page_start + folio_size(locked_folio) - 1;
	}

	while (index <= end_index) {
		/*
		 * For locked page, we will call btrfs_mark_ordered_io_finished
		 * through btrfs_mark_ordered_io_finished() on it
		 * in run_delalloc_range() for the error handling, which will
		 * clear page Ordered and run the ordered extent accounting.
		 *
		 * Here we can't just clear the Ordered bit, or
		 * btrfs_mark_ordered_io_finished() would skip the accounting
		 * for the page range, and the ordered extent will never finish.
		 */
		if (locked_folio && index == (page_start >> PAGE_SHIFT)) {
			index++;
			continue;
		}
		folio = filemap_get_folio(inode->vfs_inode.i_mapping, index);
		index++;
		if (IS_ERR(folio))
			continue;

		/*
		 * Here we just clear all Ordered bits for every page in the
		 * range, then btrfs_mark_ordered_io_finished() will handle
		 * the ordered extent accounting for the range.
		 */
		btrfs_folio_clamp_clear_ordered(inode->root->fs_info, folio,
						offset, bytes);
		folio_put(folio);
	}

	if (locked_folio) {
		/* The locked page covers the full range, nothing needs to be done */
		if (bytes + offset <= page_start + folio_size(locked_folio))
			return;
		/*
		 * In case this page belongs to the delalloc range being
		 * instantiated then skip it, since the first page of a range is
		 * going to be properly cleaned up by the caller of
		 * run_delalloc_range
		 */
		if (page_start >= offset && page_end <= (offset + bytes - 1)) {
			bytes = offset + bytes - folio_pos(locked_folio) -
				folio_size(locked_folio);
			offset = folio_pos(locked_folio) + folio_size(locked_folio);
		}
	}

	return btrfs_mark_ordered_io_finished(inode, NULL, offset, bytes, false);
}

static int btrfs_dirty_inode(struct btrfs_inode *inode);

static int btrfs_init_inode_security(struct btrfs_trans_handle *trans,
				     struct btrfs_new_inode_args *args)
{
	int err;

	if (args->default_acl) {
		err = __btrfs_set_acl(trans, args->inode, args->default_acl,
				      ACL_TYPE_DEFAULT);
		if (err)
			return err;
	}
	if (args->acl) {
		err = __btrfs_set_acl(trans, args->inode, args->acl, ACL_TYPE_ACCESS);
		if (err)
			return err;
	}
	if (!args->default_acl && !args->acl)
		cache_no_acl(args->inode);
	return btrfs_xattr_security_init(trans, args->inode, args->dir,
					 &args->dentry->d_name);
}

/*
 * this does all the hard work for inserting an inline extent into
 * the btree.  The caller should have done a btrfs_drop_extents so that
 * no overlapping inline items exist in the btree
 */
static int insert_inline_extent(struct btrfs_trans_handle *trans,
				struct btrfs_path *path,
				struct btrfs_inode *inode, bool extent_inserted,
				size_t size, size_t compressed_size,
				int compress_type,
				struct folio *compressed_folio,
				bool update_i_size)
{
	struct btrfs_root *root = inode->root;
	struct extent_buffer *leaf;
	const u32 sectorsize = trans->fs_info->sectorsize;
	char *kaddr;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	int ret;
	size_t cur_size = size;
	u64 i_size;

	/*
	 * The decompressed size must still be no larger than a sector.  Under
	 * heavy race, we can have size == 0 passed in, but that shouldn't be a
	 * big deal and we can continue the insertion.
	 */
	ASSERT(size <= sectorsize);

	/*
	 * The compressed size also needs to be no larger than a sector.
	 * That's also why we only need one page as the parameter.
	 */
	if (compressed_folio)
		ASSERT(compressed_size <= sectorsize);
	else
		ASSERT(compressed_size == 0);

	if (compressed_size && compressed_folio)
		cur_size = compressed_size;

	if (!extent_inserted) {
		struct btrfs_key key;
		size_t datasize;

		key.objectid = btrfs_ino(inode);
		key.offset = 0;
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
		kaddr = kmap_local_folio(compressed_folio, 0);
		write_extent_buffer(leaf, kaddr, ptr, compressed_size);
		kunmap_local(kaddr);

		btrfs_set_file_extent_compression(leaf, ei,
						  compress_type);
	} else {
		struct folio *folio;

		folio = filemap_get_folio(inode->vfs_inode.i_mapping, 0);
		ASSERT(!IS_ERR(folio));
		btrfs_set_file_extent_compression(leaf, ei, 0);
		kaddr = kmap_local_folio(folio, 0);
		write_extent_buffer(leaf, kaddr, ptr, size);
		kunmap_local(kaddr);
		folio_put(folio);
	}
	btrfs_mark_buffer_dirty(trans, leaf);
	btrfs_release_path(path);

	/*
	 * We align size to sectorsize for inline extents just for simplicity
	 * sake.
	 */
	ret = btrfs_inode_set_file_extent_range(inode, 0,
					ALIGN(size, root->fs_info->sectorsize));
	if (ret)
		goto fail;

	/*
	 * We're an inline extent, so nobody can extend the file past i_size
	 * without locking a page we already have locked.
	 *
	 * We must do any i_size and inode updates before we unlock the pages.
	 * Otherwise we could end up racing with unlink.
	 */
	i_size = i_size_read(&inode->vfs_inode);
	if (update_i_size && size > i_size) {
		i_size_write(&inode->vfs_inode, size);
		i_size = size;
	}
	inode->disk_i_size = i_size;

fail:
	return ret;
}

static bool can_cow_file_range_inline(struct btrfs_inode *inode,
				      u64 offset, u64 size,
				      size_t compressed_size)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 data_len = (compressed_size ?: size);

	/* Inline extents must start at offset 0. */
	if (offset != 0)
		return false;

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
	if (fs_info->sectorsize != PAGE_SIZE)
		return false;

	/* Inline extents are limited to sectorsize. */
	if (size > fs_info->sectorsize)
		return false;

	/* We cannot exceed the maximum inline data size. */
	if (data_len > BTRFS_MAX_INLINE_DATA_SIZE(fs_info))
		return false;

	/* We cannot exceed the user specified max_inline size. */
	if (data_len > fs_info->max_inline)
		return false;

	/* Inline extents must be the entirety of the file. */
	if (size < i_size_read(&inode->vfs_inode))
		return false;

	return true;
}

/*
 * conditionally insert an inline extent into the file.  This
 * does the checks required to make sure the data is small enough
 * to fit as an inline extent.
 *
 * If being used directly, you must have already checked we're allowed to cow
 * the range by getting true from can_cow_file_range_inline().
 */
static noinline int __cow_file_range_inline(struct btrfs_inode *inode,
					    u64 size, size_t compressed_size,
					    int compress_type,
					    struct folio *compressed_folio,
					    bool update_i_size)
{
	struct btrfs_drop_extents_args drop_args = { 0 };
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	u64 data_len = (compressed_size ?: size);
	int ret;
	struct btrfs_path *path;

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
	drop_args.start = 0;
	drop_args.end = fs_info->sectorsize;
	drop_args.drop_cache = true;
	drop_args.replace_extent = true;
	drop_args.extent_item_size = btrfs_file_extent_calc_inline_size(data_len);
	ret = btrfs_drop_extents(trans, root, inode, &drop_args);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = insert_inline_extent(trans, path, inode, drop_args.extent_inserted,
				   size, compressed_size, compress_type,
				   compressed_folio, update_i_size);
	if (ret && ret != -ENOSPC) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else if (ret == -ENOSPC) {
		ret = 1;
		goto out;
	}

	btrfs_update_inode_bytes(inode, size, drop_args.bytes_found);
	ret = btrfs_update_inode(trans, inode);
	if (ret && ret != -ENOSPC) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else if (ret == -ENOSPC) {
		ret = 1;
		goto out;
	}

	btrfs_set_inode_full_sync(inode);
out:
	/*
	 * Don't forget to free the reserved space, as for inlined extent
	 * it won't count as data extent, free them directly here.
	 * And at reserve time, it's always aligned to page size, so
	 * just free one page here.
	 */
	btrfs_qgroup_free_data(inode, NULL, 0, PAGE_SIZE, NULL);
	btrfs_free_path(path);
	btrfs_end_transaction(trans);
	return ret;
}

static noinline int cow_file_range_inline(struct btrfs_inode *inode,
					  struct folio *locked_folio,
					  u64 offset, u64 end,
					  size_t compressed_size,
					  int compress_type,
					  struct folio *compressed_folio,
					  bool update_i_size)
{
	struct extent_state *cached = NULL;
	unsigned long clear_flags = EXTENT_DELALLOC | EXTENT_DELALLOC_NEW |
		EXTENT_DEFRAG | EXTENT_DO_ACCOUNTING | EXTENT_LOCKED;
	u64 size = min_t(u64, i_size_read(&inode->vfs_inode), end + 1);
	int ret;

	if (!can_cow_file_range_inline(inode, offset, size, compressed_size))
		return 1;

	lock_extent(&inode->io_tree, offset, end, &cached);
	ret = __cow_file_range_inline(inode, size, compressed_size,
				      compress_type, compressed_folio,
				      update_i_size);
	if (ret > 0) {
		unlock_extent(&inode->io_tree, offset, end, &cached);
		return ret;
	}

	/*
	 * In the successful case (ret == 0 here), cow_file_range will return 1.
	 *
	 * Quite a bit further up the callstack in extent_writepage(), ret == 1
	 * is treated as a short circuited success and does not unlock the folio,
	 * so we must do it here.
	 *
	 * In the failure case, the locked_folio does get unlocked by
	 * btrfs_folio_end_all_writers, which asserts that it is still locked
	 * at that point, so we must *not* unlock it here.
	 *
	 * The other two callsites in compress_file_range do not have a
	 * locked_folio, so they are not relevant to this logic.
	 */
	if (ret == 0)
		locked_folio = NULL;

	extent_clear_unlock_delalloc(inode, offset, end, locked_folio, &cached,
				     clear_flags, PAGE_UNLOCK |
				     PAGE_START_WRITEBACK | PAGE_END_WRITEBACK);
	return ret;
}

struct async_extent {
	u64 start;
	u64 ram_size;
	u64 compressed_size;
	struct folio **folios;
	unsigned long nr_folios;
	int compress_type;
	struct list_head list;
};

struct async_chunk {
	struct btrfs_inode *inode;
	struct folio *locked_folio;
	u64 start;
	u64 end;
	blk_opf_t write_flags;
	struct list_head extents;
	struct cgroup_subsys_state *blkcg_css;
	struct btrfs_work work;
	struct async_cow *async_cow;
};

struct async_cow {
	atomic_t num_chunks;
	struct async_chunk chunks[];
};

static noinline int add_async_extent(struct async_chunk *cow,
				     u64 start, u64 ram_size,
				     u64 compressed_size,
				     struct folio **folios,
				     unsigned long nr_folios,
				     int compress_type)
{
	struct async_extent *async_extent;

	async_extent = kmalloc(sizeof(*async_extent), GFP_NOFS);
	if (!async_extent)
		return -ENOMEM;
	async_extent->start = start;
	async_extent->ram_size = ram_size;
	async_extent->compressed_size = compressed_size;
	async_extent->folios = folios;
	async_extent->nr_folios = nr_folios;
	async_extent->compress_type = compress_type;
	list_add_tail(&async_extent->list, &cow->extents);
	return 0;
}

/*
 * Check if the inode needs to be submitted to compression, based on mount
 * options, defragmentation, properties or heuristics.
 */
static inline int inode_need_compress(struct btrfs_inode *inode, u64 start,
				      u64 end)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	if (!btrfs_inode_can_compress(inode)) {
		WARN(IS_ENABLED(CONFIG_BTRFS_DEBUG),
			KERN_ERR "BTRFS: unexpected compression for ino %llu\n",
			btrfs_ino(inode));
		return 0;
	}
	/*
	 * Only enable sector perfect compression for experimental builds.
	 *
	 * This is a big feature change for subpage cases, and can hit
	 * different corner cases, so only limit this feature for
	 * experimental build for now.
	 *
	 * ETA for moving this out of experimental builds is 6.15.
	 */
	if (fs_info->sectorsize < PAGE_SIZE &&
	    !IS_ENABLED(CONFIG_BTRFS_EXPERIMENTAL)) {
		if (!PAGE_ALIGNED(start) ||
		    !PAGE_ALIGNED(end + 1))
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
		return btrfs_compress_heuristic(inode, start, end);
	return 0;
}

static inline void inode_should_defrag(struct btrfs_inode *inode,
		u64 start, u64 end, u64 num_bytes, u32 small_write)
{
	/* If this is a small write inside eof, kick off a defrag */
	if (num_bytes < small_write &&
	    (start > 0 || end + 1 < inode->disk_i_size))
		btrfs_add_inode_defrag(inode, small_write);
}

static int extent_range_clear_dirty_for_io(struct inode *inode, u64 start, u64 end)
{
	unsigned long end_index = end >> PAGE_SHIFT;
	struct folio *folio;
	int ret = 0;

	for (unsigned long index = start >> PAGE_SHIFT;
	     index <= end_index; index++) {
		folio = filemap_get_folio(inode->i_mapping, index);
		if (IS_ERR(folio)) {
			if (!ret)
				ret = PTR_ERR(folio);
			continue;
		}
		btrfs_folio_clamp_clear_dirty(inode_to_fs_info(inode), folio, start,
					      end + 1 - start);
		folio_put(folio);
	}
	return ret;
}

/*
 * Work queue call back to started compression on a file and pages.
 *
 * This is done inside an ordered work queue, and the compression is spread
 * across many cpus.  The actual IO submission is step two, and the ordered work
 * queue takes care of making sure that happens in the same order things were
 * put onto the queue by writepages and friends.
 *
 * If this code finds it can't get good compression, it puts an entry onto the
 * work queue to write the uncompressed bytes.  This makes sure that both
 * compressed inodes and uncompressed inodes are written in the same order that
 * the flusher thread sent them down.
 */
static void compress_file_range(struct btrfs_work *work)
{
	struct async_chunk *async_chunk =
		container_of(work, struct async_chunk, work);
	struct btrfs_inode *inode = async_chunk->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct address_space *mapping = inode->vfs_inode.i_mapping;
	u64 blocksize = fs_info->sectorsize;
	u64 start = async_chunk->start;
	u64 end = async_chunk->end;
	u64 actual_end;
	u64 i_size;
	int ret = 0;
	struct folio **folios;
	unsigned long nr_folios;
	unsigned long total_compressed = 0;
	unsigned long total_in = 0;
	unsigned int poff;
	int i;
	int compress_type = fs_info->compress_type;

	inode_should_defrag(inode, start, end, end - start + 1, SZ_16K);

	/*
	 * We need to call clear_page_dirty_for_io on each page in the range.
	 * Otherwise applications with the file mmap'd can wander in and change
	 * the page contents while we are compressing them.
	 */
	ret = extent_range_clear_dirty_for_io(&inode->vfs_inode, start, end);

	/*
	 * All the folios should have been locked thus no failure.
	 *
	 * And even if some folios are missing, btrfs_compress_folios()
	 * would handle them correctly, so here just do an ASSERT() check for
	 * early logic errors.
	 */
	ASSERT(ret == 0);

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
	i_size = i_size_read(&inode->vfs_inode);
	barrier();
	actual_end = min_t(u64, i_size, end + 1);
again:
	folios = NULL;
	nr_folios = (end >> PAGE_SHIFT) - (start >> PAGE_SHIFT) + 1;
	nr_folios = min_t(unsigned long, nr_folios, BTRFS_MAX_COMPRESSED_PAGES);

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
	 * Skip compression for a small file range(<=blocksize) that
	 * isn't an inline extent, since it doesn't save disk space at all.
	 */
	if (total_compressed <= blocksize &&
	   (start > 0 || end + 1 < inode->disk_i_size))
		goto cleanup_and_bail_uncompressed;

	total_compressed = min_t(unsigned long, total_compressed,
			BTRFS_MAX_UNCOMPRESSED);
	total_in = 0;
	ret = 0;

	/*
	 * We do compression for mount -o compress and when the inode has not
	 * been flagged as NOCOMPRESS.  This flag can change at any time if we
	 * discover bad compression ratios.
	 */
	if (!inode_need_compress(inode, start, end))
		goto cleanup_and_bail_uncompressed;

	folios = kcalloc(nr_folios, sizeof(struct folio *), GFP_NOFS);
	if (!folios) {
		/*
		 * Memory allocation failure is not a fatal error, we can fall
		 * back to uncompressed code.
		 */
		goto cleanup_and_bail_uncompressed;
	}

	if (inode->defrag_compress)
		compress_type = inode->defrag_compress;
	else if (inode->prop_compress)
		compress_type = inode->prop_compress;

	/* Compression level is applied here. */
	ret = btrfs_compress_folios(compress_type | (fs_info->compress_level << 4),
				    mapping, start, folios, &nr_folios, &total_in,
				    &total_compressed);
	if (ret)
		goto mark_incompressible;

	/*
	 * Zero the tail end of the last page, as we might be sending it down
	 * to disk.
	 */
	poff = offset_in_page(total_compressed);
	if (poff)
		folio_zero_range(folios[nr_folios - 1], poff, PAGE_SIZE - poff);

	/*
	 * Try to create an inline extent.
	 *
	 * If we didn't compress the entire range, try to create an uncompressed
	 * inline extent, else a compressed one.
	 *
	 * Check cow_file_range() for why we don't even try to create inline
	 * extent for the subpage case.
	 */
	if (total_in < actual_end)
		ret = cow_file_range_inline(inode, NULL, start, end, 0,
					    BTRFS_COMPRESS_NONE, NULL, false);
	else
		ret = cow_file_range_inline(inode, NULL, start, end, total_compressed,
					    compress_type, folios[0], false);
	if (ret <= 0) {
		if (ret < 0)
			mapping_set_error(mapping, -EIO);
		goto free_pages;
	}

	/*
	 * We aren't doing an inline extent. Round the compressed size up to a
	 * block size boundary so the allocator does sane things.
	 */
	total_compressed = ALIGN(total_compressed, blocksize);

	/*
	 * One last check to make sure the compression is really a win, compare
	 * the page count read with the blocks on disk, compression must free at
	 * least one sector.
	 */
	total_in = round_up(total_in, fs_info->sectorsize);
	if (total_compressed + blocksize > total_in)
		goto mark_incompressible;

	/*
	 * The async work queues will take care of doing actual allocation on
	 * disk for these compressed pages, and will submit the bios.
	 */
	ret = add_async_extent(async_chunk, start, total_in, total_compressed, folios,
			       nr_folios, compress_type);
	BUG_ON(ret);
	if (start + total_in < end) {
		start += total_in;
		cond_resched();
		goto again;
	}
	return;

mark_incompressible:
	if (!btrfs_test_opt(fs_info, FORCE_COMPRESS) && !inode->prop_compress)
		inode->flags |= BTRFS_INODE_NOCOMPRESS;
cleanup_and_bail_uncompressed:
	ret = add_async_extent(async_chunk, start, end - start + 1, 0, NULL, 0,
			       BTRFS_COMPRESS_NONE);
	BUG_ON(ret);
free_pages:
	if (folios) {
		for (i = 0; i < nr_folios; i++) {
			WARN_ON(folios[i]->mapping);
			btrfs_free_compr_folio(folios[i]);
		}
		kfree(folios);
	}
}

static void free_async_extent_pages(struct async_extent *async_extent)
{
	int i;

	if (!async_extent->folios)
		return;

	for (i = 0; i < async_extent->nr_folios; i++) {
		WARN_ON(async_extent->folios[i]->mapping);
		btrfs_free_compr_folio(async_extent->folios[i]);
	}
	kfree(async_extent->folios);
	async_extent->nr_folios = 0;
	async_extent->folios = NULL;
}

static void submit_uncompressed_range(struct btrfs_inode *inode,
				      struct async_extent *async_extent,
				      struct folio *locked_folio)
{
	u64 start = async_extent->start;
	u64 end = async_extent->start + async_extent->ram_size - 1;
	int ret;
	struct writeback_control wbc = {
		.sync_mode		= WB_SYNC_ALL,
		.range_start		= start,
		.range_end		= end,
		.no_cgroup_owner	= 1,
	};

	wbc_attach_fdatawrite_inode(&wbc, &inode->vfs_inode);
	ret = run_delalloc_cow(inode, locked_folio, start, end,
			       &wbc, false);
	wbc_detach_inode(&wbc);
	if (ret < 0) {
		btrfs_cleanup_ordered_extents(inode, locked_folio,
					      start, end - start + 1);
		if (locked_folio) {
			const u64 page_start = folio_pos(locked_folio);

			folio_start_writeback(locked_folio);
			folio_end_writeback(locked_folio);
			btrfs_mark_ordered_io_finished(inode, locked_folio,
						       page_start, PAGE_SIZE,
						       !ret);
			mapping_set_error(locked_folio->mapping, ret);
			folio_unlock(locked_folio);
		}
	}
}

static void submit_one_async_extent(struct async_chunk *async_chunk,
				    struct async_extent *async_extent,
				    u64 *alloc_hint)
{
	struct btrfs_inode *inode = async_chunk->inode;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_file_extent file_extent;
	struct btrfs_key ins;
	struct folio *locked_folio = NULL;
	struct extent_state *cached = NULL;
	struct extent_map *em;
	int ret = 0;
	u64 start = async_extent->start;
	u64 end = async_extent->start + async_extent->ram_size - 1;

	if (async_chunk->blkcg_css)
		kthread_associate_blkcg(async_chunk->blkcg_css);

	/*
	 * If async_chunk->locked_folio is in the async_extent range, we need to
	 * handle it.
	 */
	if (async_chunk->locked_folio) {
		u64 locked_folio_start = folio_pos(async_chunk->locked_folio);
		u64 locked_folio_end = locked_folio_start +
			folio_size(async_chunk->locked_folio) - 1;

		if (!(start >= locked_folio_end || end <= locked_folio_start))
			locked_folio = async_chunk->locked_folio;
	}

	if (async_extent->compress_type == BTRFS_COMPRESS_NONE) {
		submit_uncompressed_range(inode, async_extent, locked_folio);
		goto done;
	}

	ret = btrfs_reserve_extent(root, async_extent->ram_size,
				   async_extent->compressed_size,
				   async_extent->compressed_size,
				   0, *alloc_hint, &ins, 1, 1);
	if (ret) {
		/*
		 * We can't reserve contiguous space for the compressed size.
		 * Unlikely, but it's possible that we could have enough
		 * non-contiguous space for the uncompressed size instead.  So
		 * fall back to uncompressed.
		 */
		submit_uncompressed_range(inode, async_extent, locked_folio);
		goto done;
	}

	lock_extent(io_tree, start, end, &cached);

	/* Here we're doing allocation and writeback of the compressed pages */
	file_extent.disk_bytenr = ins.objectid;
	file_extent.disk_num_bytes = ins.offset;
	file_extent.ram_bytes = async_extent->ram_size;
	file_extent.num_bytes = async_extent->ram_size;
	file_extent.offset = 0;
	file_extent.compression = async_extent->compress_type;

	em = btrfs_create_io_em(inode, start, &file_extent, BTRFS_ORDERED_COMPRESSED);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out_free_reserve;
	}
	free_extent_map(em);

	ordered = btrfs_alloc_ordered_extent(inode, start, &file_extent,
					     1 << BTRFS_ORDERED_COMPRESSED);
	if (IS_ERR(ordered)) {
		btrfs_drop_extent_map_range(inode, start, end, false);
		ret = PTR_ERR(ordered);
		goto out_free_reserve;
	}
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);

	/* Clear dirty, set writeback and unlock the pages. */
	extent_clear_unlock_delalloc(inode, start, end,
			NULL, &cached, EXTENT_LOCKED | EXTENT_DELALLOC,
			PAGE_UNLOCK | PAGE_START_WRITEBACK);
	btrfs_submit_compressed_write(ordered,
			    async_extent->folios,	/* compressed_folios */
			    async_extent->nr_folios,
			    async_chunk->write_flags, true);
	*alloc_hint = ins.objectid + ins.offset;
done:
	if (async_chunk->blkcg_css)
		kthread_associate_blkcg(NULL);
	kfree(async_extent);
	return;

out_free_reserve:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
	mapping_set_error(inode->vfs_inode.i_mapping, -EIO);
	extent_clear_unlock_delalloc(inode, start, end,
				     NULL, &cached,
				     EXTENT_LOCKED | EXTENT_DELALLOC |
				     EXTENT_DELALLOC_NEW |
				     EXTENT_DEFRAG | EXTENT_DO_ACCOUNTING,
				     PAGE_UNLOCK | PAGE_START_WRITEBACK |
				     PAGE_END_WRITEBACK);
	free_async_extent_pages(async_extent);
	if (async_chunk->blkcg_css)
		kthread_associate_blkcg(NULL);
	btrfs_debug(fs_info,
"async extent submission failed root=%lld inode=%llu start=%llu len=%llu ret=%d",
		    btrfs_root_id(root), btrfs_ino(inode), start,
		    async_extent->ram_size, ret);
	kfree(async_extent);
}

u64 btrfs_get_extent_allocation_hint(struct btrfs_inode *inode, u64 start,
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
		if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
			free_extent_map(em);
			em = search_extent_mapping(em_tree, 0, 0);
			if (em && em->disk_bytenr < EXTENT_MAP_LAST_BYTE)
				alloc_hint = extent_map_block_start(em);
			if (em)
				free_extent_map(em);
		} else {
			alloc_hint = extent_map_block_start(em);
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
 * locked_folio is the folio that writepage had locked already.  We use
 * it to make sure we don't do extra locks or unlocks.
 *
 * When this function fails, it unlocks all pages except @locked_folio.
 *
 * When this function successfully creates an inline extent, it returns 1 and
 * unlocks all pages including locked_folio and starts I/O on them.
 * (In reality inline extents are limited to a single page, so locked_folio is
 * the only page handled anyway).
 *
 * When this function succeed and creates a normal extent, the page locking
 * status depends on the passed in flags:
 *
 * - If @keep_locked is set, all pages are kept locked.
 * - Else all pages except for @locked_folio are unlocked.
 *
 * When a failure happens in the second or later iteration of the
 * while-loop, the ordered extents created in previous iterations are kept
 * intact. So, the caller must clean them up by calling
 * btrfs_cleanup_ordered_extents(). See btrfs_run_delalloc_range() for
 * example.
 */
static noinline int cow_file_range(struct btrfs_inode *inode,
				   struct folio *locked_folio, u64 start,
				   u64 end, u64 *done_offset,
				   bool keep_locked, bool no_inline)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_state *cached = NULL;
	u64 alloc_hint = 0;
	u64 orig_start = start;
	u64 num_bytes;
	u64 cur_alloc_size = 0;
	u64 min_alloc_size;
	u64 blocksize = fs_info->sectorsize;
	struct btrfs_key ins;
	struct extent_map *em;
	unsigned clear_bits;
	unsigned long page_ops;
	int ret = 0;

	if (btrfs_is_free_space_inode(inode)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	num_bytes = ALIGN(end - start + 1, blocksize);
	num_bytes = max(blocksize,  num_bytes);
	ASSERT(num_bytes <= btrfs_super_total_bytes(fs_info->super_copy));

	inode_should_defrag(inode, start, end, num_bytes, SZ_64K);

	if (!no_inline) {
		/* lets try to make an inline extent */
		ret = cow_file_range_inline(inode, locked_folio, start, end, 0,
					    BTRFS_COMPRESS_NONE, NULL, false);
		if (ret <= 0) {
			/*
			 * We succeeded, return 1 so the caller knows we're done
			 * with this page and already handled the IO.
			 *
			 * If there was an error then cow_file_range_inline() has
			 * already done the cleanup.
			 */
			if (ret == 0)
				ret = 1;
			goto done;
		}
	}

	alloc_hint = btrfs_get_extent_allocation_hint(inode, start, num_bytes);

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
		struct btrfs_ordered_extent *ordered;
		struct btrfs_file_extent file_extent;

		ret = btrfs_reserve_extent(root, num_bytes, num_bytes,
					   min_alloc_size, 0, alloc_hint,
					   &ins, 1, 1);
		if (ret == -EAGAIN) {
			/*
			 * btrfs_reserve_extent only returns -EAGAIN for zoned
			 * file systems, which is an indication that there are
			 * no active zones to allocate from at the moment.
			 *
			 * If this is the first loop iteration, wait for at
			 * least one zone to finish before retrying the
			 * allocation.  Otherwise ask the caller to write out
			 * the already allocated blocks before coming back to
			 * us, or return -ENOSPC if it can't handle retries.
			 */
			ASSERT(btrfs_is_zoned(fs_info));
			if (start == orig_start) {
				wait_on_bit_io(&inode->root->fs_info->flags,
					       BTRFS_FS_NEED_ZONE_FINISH,
					       TASK_UNINTERRUPTIBLE);
				continue;
			}
			if (done_offset) {
				*done_offset = start - 1;
				return 0;
			}
			ret = -ENOSPC;
		}
		if (ret < 0)
			goto out_unlock;
		cur_alloc_size = ins.offset;

		file_extent.disk_bytenr = ins.objectid;
		file_extent.disk_num_bytes = ins.offset;
		file_extent.num_bytes = ins.offset;
		file_extent.ram_bytes = ins.offset;
		file_extent.offset = 0;
		file_extent.compression = BTRFS_COMPRESS_NONE;

		lock_extent(&inode->io_tree, start, start + cur_alloc_size - 1,
			    &cached);

		em = btrfs_create_io_em(inode, start, &file_extent,
					BTRFS_ORDERED_REGULAR);
		if (IS_ERR(em)) {
			unlock_extent(&inode->io_tree, start,
				      start + cur_alloc_size - 1, &cached);
			ret = PTR_ERR(em);
			goto out_reserve;
		}
		free_extent_map(em);

		ordered = btrfs_alloc_ordered_extent(inode, start, &file_extent,
						     1 << BTRFS_ORDERED_REGULAR);
		if (IS_ERR(ordered)) {
			unlock_extent(&inode->io_tree, start,
				      start + cur_alloc_size - 1, &cached);
			ret = PTR_ERR(ordered);
			goto out_drop_extent_cache;
		}

		if (btrfs_is_data_reloc_root(root)) {
			ret = btrfs_reloc_clone_csums(ordered);

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
				btrfs_drop_extent_map_range(inode, start,
							    start + cur_alloc_size - 1,
							    false);
		}
		btrfs_put_ordered_extent(ordered);

		btrfs_dec_block_group_reservations(fs_info, ins.objectid);

		/*
		 * We're not doing compressed IO, don't unlock the first page
		 * (which the caller expects to stay locked), don't clear any
		 * dirty bits and don't set any writeback bits
		 *
		 * Do set the Ordered flag so we know this page was
		 * properly setup for writepage.
		 */
		page_ops = (keep_locked ? 0 : PAGE_UNLOCK);
		page_ops |= PAGE_SET_ORDERED;

		extent_clear_unlock_delalloc(inode, start, start + cur_alloc_size - 1,
					     locked_folio, &cached,
					     EXTENT_LOCKED | EXTENT_DELALLOC,
					     page_ops);
		if (num_bytes < cur_alloc_size)
			num_bytes = 0;
		else
			num_bytes -= cur_alloc_size;
		alloc_hint = ins.objectid + ins.offset;
		start += cur_alloc_size;
		cur_alloc_size = 0;

		/*
		 * btrfs_reloc_clone_csums() error, since start is increased
		 * extent_clear_unlock_delalloc() at out_unlock label won't
		 * free metadata of current ordered extent, we're OK to exit.
		 */
		if (ret)
			goto out_unlock;
	}
done:
	if (done_offset)
		*done_offset = end;
	return ret;

out_drop_extent_cache:
	btrfs_drop_extent_map_range(inode, start, start + cur_alloc_size - 1, false);
out_reserve:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
out_unlock:
	/*
	 * Now, we have three regions to clean up:
	 *
	 * |-------(1)----|---(2)---|-------------(3)----------|
	 * `- orig_start  `- start  `- start + cur_alloc_size  `- end
	 *
	 * We process each region below.
	 */

	clear_bits = EXTENT_LOCKED | EXTENT_DELALLOC | EXTENT_DELALLOC_NEW |
		EXTENT_DEFRAG | EXTENT_CLEAR_META_RESV;
	page_ops = PAGE_UNLOCK | PAGE_START_WRITEBACK | PAGE_END_WRITEBACK;

	/*
	 * For the range (1). We have already instantiated the ordered extents
	 * for this region. They are cleaned up by
	 * btrfs_cleanup_ordered_extents() in e.g,
	 * btrfs_run_delalloc_range(). EXTENT_LOCKED | EXTENT_DELALLOC are
	 * already cleared in the above loop. And, EXTENT_DELALLOC_NEW |
	 * EXTENT_DEFRAG | EXTENT_CLEAR_META_RESV are handled by the cleanup
	 * function.
	 *
	 * However, in case of @keep_locked, we still need to unlock the pages
	 * (except @locked_folio) to ensure all the pages are unlocked.
	 */
	if (keep_locked && orig_start < start) {
		if (!locked_folio)
			mapping_set_error(inode->vfs_inode.i_mapping, ret);
		extent_clear_unlock_delalloc(inode, orig_start, start - 1,
					     locked_folio, NULL, 0, page_ops);
	}

	/*
	 * At this point we're unlocked, we want to make sure we're only
	 * clearing these flags under the extent lock, so lock the rest of the
	 * range and clear everything up.
	 */
	lock_extent(&inode->io_tree, start, end, NULL);

	/*
	 * For the range (2). If we reserved an extent for our delalloc range
	 * (or a subrange) and failed to create the respective ordered extent,
	 * then it means that when we reserved the extent we decremented the
	 * extent's size from the data space_info's bytes_may_use counter and
	 * incremented the space_info's bytes_reserved counter by the same
	 * amount. We must make sure extent_clear_unlock_delalloc() does not try
	 * to decrement again the data space_info's bytes_may_use counter,
	 * therefore we do not pass it the flag EXTENT_CLEAR_DATA_RESV.
	 */
	if (cur_alloc_size) {
		extent_clear_unlock_delalloc(inode, start,
					     start + cur_alloc_size - 1,
					     locked_folio, &cached, clear_bits,
					     page_ops);
		btrfs_qgroup_free_data(inode, NULL, start, cur_alloc_size, NULL);
	}

	/*
	 * For the range (3). We never touched the region. In addition to the
	 * clear_bits above, we add EXTENT_CLEAR_DATA_RESV to release the data
	 * space_info's bytes_may_use counter, reserved in
	 * btrfs_check_data_free_space().
	 */
	if (start + cur_alloc_size < end) {
		clear_bits |= EXTENT_CLEAR_DATA_RESV;
		extent_clear_unlock_delalloc(inode, start + cur_alloc_size,
					     end, locked_folio,
					     &cached, clear_bits, page_ops);
		btrfs_qgroup_free_data(inode, NULL, start + cur_alloc_size,
				       end - start - cur_alloc_size + 1, NULL);
	}
	return ret;
}

/*
 * Phase two of compressed writeback.  This is the ordered portion of the code,
 * which only gets called in the order the work was queued.  We walk all the
 * async extents created by compress_file_range and send them down to the disk.
 *
 * If called with @do_free == true then it'll try to finish the work and free
 * the work struct eventually.
 */
static noinline void submit_compressed_extents(struct btrfs_work *work, bool do_free)
{
	struct async_chunk *async_chunk = container_of(work, struct async_chunk,
						     work);
	struct btrfs_fs_info *fs_info = btrfs_work_owner(work);
	struct async_extent *async_extent;
	unsigned long nr_pages;
	u64 alloc_hint = 0;

	if (do_free) {
		struct async_cow *async_cow;

		btrfs_add_delayed_iput(async_chunk->inode);
		if (async_chunk->blkcg_css)
			css_put(async_chunk->blkcg_css);

		async_cow = async_chunk->async_cow;
		if (atomic_dec_and_test(&async_cow->num_chunks))
			kvfree(async_cow);
		return;
	}

	nr_pages = (async_chunk->end - async_chunk->start + PAGE_SIZE) >>
		PAGE_SHIFT;

	while (!list_empty(&async_chunk->extents)) {
		async_extent = list_entry(async_chunk->extents.next,
					  struct async_extent, list);
		list_del(&async_extent->list);
		submit_one_async_extent(async_chunk, async_extent, &alloc_hint);
	}

	/* atomic_sub_return implies a barrier */
	if (atomic_sub_return(nr_pages, &fs_info->async_delalloc_pages) <
	    5 * SZ_1M)
		cond_wake_up_nomb(&fs_info->async_submit_wait);
}

static bool run_delalloc_compressed(struct btrfs_inode *inode,
				    struct folio *locked_folio, u64 start,
				    u64 end, struct writeback_control *wbc)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct cgroup_subsys_state *blkcg_css = wbc_blkcg_css(wbc);
	struct async_cow *ctx;
	struct async_chunk *async_chunk;
	unsigned long nr_pages;
	u64 num_chunks = DIV_ROUND_UP(end - start, SZ_512K);
	int i;
	unsigned nofs_flag;
	const blk_opf_t write_flags = wbc_to_write_flags(wbc);

	nofs_flag = memalloc_nofs_save();
	ctx = kvmalloc(struct_size(ctx, chunks, num_chunks), GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);
	if (!ctx)
		return false;

	set_bit(BTRFS_INODE_HAS_ASYNC_EXTENT, &inode->runtime_flags);

	async_chunk = ctx->chunks;
	atomic_set(&ctx->num_chunks, num_chunks);

	for (i = 0; i < num_chunks; i++) {
		u64 cur_end = min(end, start + SZ_512K - 1);

		/*
		 * igrab is called higher up in the call chain, take only the
		 * lightweight reference for the callback lifetime
		 */
		ihold(&inode->vfs_inode);
		async_chunk[i].async_cow = ctx;
		async_chunk[i].inode = inode;
		async_chunk[i].start = start;
		async_chunk[i].end = cur_end;
		async_chunk[i].write_flags = write_flags;
		INIT_LIST_HEAD(&async_chunk[i].extents);

		/*
		 * The locked_folio comes all the way from writepage and its
		 * the original folio we were actually given.  As we spread
		 * this large delalloc region across multiple async_chunk
		 * structs, only the first struct needs a pointer to
		 * locked_folio.
		 *
		 * This way we don't need racey decisions about who is supposed
		 * to unlock it.
		 */
		if (locked_folio) {
			/*
			 * Depending on the compressibility, the pages might or
			 * might not go through async.  We want all of them to
			 * be accounted against wbc once.  Let's do it here
			 * before the paths diverge.  wbc accounting is used
			 * only for foreign writeback detection and doesn't
			 * need full accuracy.  Just account the whole thing
			 * against the first page.
			 */
			wbc_account_cgroup_owner(wbc, locked_folio,
						 cur_end - start);
			async_chunk[i].locked_folio = locked_folio;
			locked_folio = NULL;
		} else {
			async_chunk[i].locked_folio = NULL;
		}

		if (blkcg_css != blkcg_root_css) {
			css_get(blkcg_css);
			async_chunk[i].blkcg_css = blkcg_css;
			async_chunk[i].write_flags |= REQ_BTRFS_CGROUP_PUNT;
		} else {
			async_chunk[i].blkcg_css = NULL;
		}

		btrfs_init_work(&async_chunk[i].work, compress_file_range,
				submit_compressed_extents);

		nr_pages = DIV_ROUND_UP(cur_end - start, PAGE_SIZE);
		atomic_add(nr_pages, &fs_info->async_delalloc_pages);

		btrfs_queue_work(fs_info->delalloc_workers, &async_chunk[i].work);

		start = cur_end + 1;
	}
	return true;
}

/*
 * Run the delalloc range from start to end, and write back any dirty pages
 * covered by the range.
 */
static noinline int run_delalloc_cow(struct btrfs_inode *inode,
				     struct folio *locked_folio, u64 start,
				     u64 end, struct writeback_control *wbc,
				     bool pages_dirty)
{
	u64 done_offset = end;
	int ret;

	while (start <= end) {
		ret = cow_file_range(inode, locked_folio, start, end,
				     &done_offset, true, false);
		if (ret)
			return ret;
		extent_write_locked_range(&inode->vfs_inode, locked_folio,
					  start, done_offset, wbc, pages_dirty);
		start = done_offset + 1;
	}

	return 1;
}

static int fallback_to_cow(struct btrfs_inode *inode,
			   struct folio *locked_folio, const u64 start,
			   const u64 end)
{
	const bool is_space_ino = btrfs_is_free_space_inode(inode);
	const bool is_reloc_ino = btrfs_is_data_reloc_root(inode->root);
	const u64 range_bytes = end + 1 - start;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct extent_state *cached_state = NULL;
	u64 range_start = start;
	u64 count;
	int ret;

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
	lock_extent(io_tree, start, end, &cached_state);
	count = count_range_bits(io_tree, &range_start, end, range_bytes,
				 EXTENT_NORESERVE, 0, NULL);
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
					 NULL);
	}
	unlock_extent(io_tree, start, end, &cached_state);

	/*
	 * Don't try to create inline extents, as a mix of inline extent that
	 * is written out and unlocked directly and a normal NOCOW extent
	 * doesn't work.
	 */
	ret = cow_file_range(inode, locked_folio, start, end, NULL, false,
			     true);
	ASSERT(ret != 1);
	return ret;
}

struct can_nocow_file_extent_args {
	/* Input fields. */

	/* Start file offset of the range we want to NOCOW. */
	u64 start;
	/* End file offset (inclusive) of the range we want to NOCOW. */
	u64 end;
	bool writeback_path;
	bool strict;
	/*
	 * Free the path passed to can_nocow_file_extent() once it's not needed
	 * anymore.
	 */
	bool free_path;

	/*
	 * Output fields. Only set when can_nocow_file_extent() returns 1.
	 * The expected file extent for the NOCOW write.
	 */
	struct btrfs_file_extent file_extent;
};

/*
 * Check if we can NOCOW the file extent that the path points to.
 * This function may return with the path released, so the caller should check
 * if path->nodes[0] is NULL or not if it needs to use the path afterwards.
 *
 * Returns: < 0 on error
 *            0 if we can not NOCOW
 *            1 if we can NOCOW
 */
static int can_nocow_file_extent(struct btrfs_path *path,
				 struct btrfs_key *key,
				 struct btrfs_inode *inode,
				 struct can_nocow_file_extent_args *args)
{
	const bool is_freespace_inode = btrfs_is_free_space_inode(inode);
	struct extent_buffer *leaf = path->nodes[0];
	struct btrfs_root *root = inode->root;
	struct btrfs_file_extent_item *fi;
	struct btrfs_root *csum_root;
	u64 io_start;
	u64 extent_end;
	u8 extent_type;
	int can_nocow = 0;
	int ret = 0;
	bool nowait = path->nowait;

	fi = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);
	extent_type = btrfs_file_extent_type(leaf, fi);

	if (extent_type == BTRFS_FILE_EXTENT_INLINE)
		goto out;

	if (!(inode->flags & BTRFS_INODE_NODATACOW) &&
	    extent_type == BTRFS_FILE_EXTENT_REG)
		goto out;

	/*
	 * If the extent was created before the generation where the last snapshot
	 * for its subvolume was created, then this implies the extent is shared,
	 * hence we must COW.
	 */
	if (!args->strict &&
	    btrfs_file_extent_generation(leaf, fi) <=
	    btrfs_root_last_snapshot(&root->root_item))
		goto out;

	/* An explicit hole, must COW. */
	if (btrfs_file_extent_disk_bytenr(leaf, fi) == 0)
		goto out;

	/* Compressed/encrypted/encoded extents must be COWed. */
	if (btrfs_file_extent_compression(leaf, fi) ||
	    btrfs_file_extent_encryption(leaf, fi) ||
	    btrfs_file_extent_other_encoding(leaf, fi))
		goto out;

	extent_end = btrfs_file_extent_end(path);

	args->file_extent.disk_bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
	args->file_extent.disk_num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
	args->file_extent.ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
	args->file_extent.offset = btrfs_file_extent_offset(leaf, fi);
	args->file_extent.compression = btrfs_file_extent_compression(leaf, fi);

	/*
	 * The following checks can be expensive, as they need to take other
	 * locks and do btree or rbtree searches, so release the path to avoid
	 * blocking other tasks for too long.
	 */
	btrfs_release_path(path);

	ret = btrfs_cross_ref_exist(root, btrfs_ino(inode),
				    key->offset - args->file_extent.offset,
				    args->file_extent.disk_bytenr, args->strict, path);
	WARN_ON_ONCE(ret > 0 && is_freespace_inode);
	if (ret != 0)
		goto out;

	if (args->free_path) {
		/*
		 * We don't need the path anymore, plus through the
		 * btrfs_lookup_csums_list() call below we will end up allocating
		 * another path. So free the path to avoid unnecessary extra
		 * memory usage.
		 */
		btrfs_free_path(path);
		path = NULL;
	}

	/* If there are pending snapshots for this root, we must COW. */
	if (args->writeback_path && !is_freespace_inode &&
	    atomic_read(&root->snapshot_force_cow))
		goto out;

	args->file_extent.num_bytes = min(args->end + 1, extent_end) - args->start;
	args->file_extent.offset += args->start - key->offset;
	io_start = args->file_extent.disk_bytenr + args->file_extent.offset;

	/*
	 * Force COW if csums exist in the range. This ensures that csums for a
	 * given extent are either valid or do not exist.
	 */

	csum_root = btrfs_csum_root(root->fs_info, io_start);
	ret = btrfs_lookup_csums_list(csum_root, io_start,
				      io_start + args->file_extent.num_bytes - 1,
				      NULL, nowait);
	WARN_ON_ONCE(ret > 0 && is_freespace_inode);
	if (ret != 0)
		goto out;

	can_nocow = 1;
 out:
	if (args->free_path && path)
		btrfs_free_path(path);

	return ret < 0 ? ret : can_nocow;
}

/*
 * when nowcow writeback call back.  This checks for snapshots or COW copies
 * of the extents that exist in the file, and COWs the file as required.
 *
 * If no cow copies or snapshots exist, we write directly to the existing
 * blocks on disk
 */
static noinline int run_delalloc_nocow(struct btrfs_inode *inode,
				       struct folio *locked_folio,
				       const u64 start, const u64 end)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_root *root = inode->root;
	struct btrfs_path *path;
	u64 cow_start = (u64)-1;
	u64 cur_offset = start;
	int ret;
	bool check_prev = true;
	u64 ino = btrfs_ino(inode);
	struct can_nocow_file_extent_args nocow_args = { 0 };

	/*
	 * Normally on a zoned device we're only doing COW writes, but in case
	 * of relocation on a zoned filesystem serializes I/O so that we're only
	 * writing sequentially and can end up here as well.
	 */
	ASSERT(!btrfs_is_zoned(fs_info) || btrfs_is_data_reloc_root(root));

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto error;
	}

	nocow_args.end = end;
	nocow_args.writeback_path = true;

	while (cur_offset <= end) {
		struct btrfs_block_group *nocow_bg = NULL;
		struct btrfs_ordered_extent *ordered;
		struct btrfs_key found_key;
		struct btrfs_file_extent_item *fi;
		struct extent_buffer *leaf;
		struct extent_state *cached_state = NULL;
		u64 extent_end;
		u64 nocow_end;
		int extent_type;
		bool is_prealloc;

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
			if (ret < 0)
				goto error;
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
			goto must_cow;
		}

		/*
		 * Found extent which begins before our range and potentially
		 * intersect it
		 */
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(leaf, fi);
		/* If this is triggered then we have a memory corruption. */
		ASSERT(extent_type < BTRFS_NR_FILE_EXTENT_TYPES);
		if (WARN_ON(extent_type >= BTRFS_NR_FILE_EXTENT_TYPES)) {
			ret = -EUCLEAN;
			goto error;
		}
		extent_end = btrfs_file_extent_end(path);

		/*
		 * If the extent we got ends before our current offset, skip to
		 * the next extent.
		 */
		if (extent_end <= cur_offset) {
			path->slots[0]++;
			goto next_slot;
		}

		nocow_args.start = cur_offset;
		ret = can_nocow_file_extent(path, &found_key, inode, &nocow_args);
		if (ret < 0)
			goto error;
		if (ret == 0)
			goto must_cow;

		ret = 0;
		nocow_bg = btrfs_inc_nocow_writers(fs_info,
				nocow_args.file_extent.disk_bytenr +
				nocow_args.file_extent.offset);
		if (!nocow_bg) {
must_cow:
			/*
			 * If we can't perform NOCOW writeback for the range,
			 * then record the beginning of the range that needs to
			 * be COWed.  It will be written out before the next
			 * NOCOW range if we find one, or when exiting this
			 * loop.
			 */
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
			ret = fallback_to_cow(inode, locked_folio, cow_start,
					      found_key.offset - 1);
			cow_start = (u64)-1;
			if (ret) {
				btrfs_dec_nocow_writers(nocow_bg);
				goto error;
			}
		}

		nocow_end = cur_offset + nocow_args.file_extent.num_bytes - 1;
		lock_extent(&inode->io_tree, cur_offset, nocow_end, &cached_state);

		is_prealloc = extent_type == BTRFS_FILE_EXTENT_PREALLOC;
		if (is_prealloc) {
			struct extent_map *em;

			em = btrfs_create_io_em(inode, cur_offset,
						&nocow_args.file_extent,
						BTRFS_ORDERED_PREALLOC);
			if (IS_ERR(em)) {
				unlock_extent(&inode->io_tree, cur_offset,
					      nocow_end, &cached_state);
				btrfs_dec_nocow_writers(nocow_bg);
				ret = PTR_ERR(em);
				goto error;
			}
			free_extent_map(em);
		}

		ordered = btrfs_alloc_ordered_extent(inode, cur_offset,
				&nocow_args.file_extent,
				is_prealloc
				? (1 << BTRFS_ORDERED_PREALLOC)
				: (1 << BTRFS_ORDERED_NOCOW));
		btrfs_dec_nocow_writers(nocow_bg);
		if (IS_ERR(ordered)) {
			if (is_prealloc) {
				btrfs_drop_extent_map_range(inode, cur_offset,
							    nocow_end, false);
			}
			unlock_extent(&inode->io_tree, cur_offset,
				      nocow_end, &cached_state);
			ret = PTR_ERR(ordered);
			goto error;
		}

		if (btrfs_is_data_reloc_root(root))
			/*
			 * Error handled later, as we must prevent
			 * extent_clear_unlock_delalloc() in error handler
			 * from freeing metadata of created ordered extent.
			 */
			ret = btrfs_reloc_clone_csums(ordered);
		btrfs_put_ordered_extent(ordered);

		extent_clear_unlock_delalloc(inode, cur_offset, nocow_end,
					     locked_folio, &cached_state,
					     EXTENT_LOCKED | EXTENT_DELALLOC |
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
	}
	btrfs_release_path(path);

	if (cur_offset <= end && cow_start == (u64)-1)
		cow_start = cur_offset;

	if (cow_start != (u64)-1) {
		cur_offset = end;
		ret = fallback_to_cow(inode, locked_folio, cow_start, end);
		cow_start = (u64)-1;
		if (ret)
			goto error;
	}

	btrfs_free_path(path);
	return 0;

error:
	/*
	 * If an error happened while a COW region is outstanding, cur_offset
	 * needs to be reset to cow_start to ensure the COW region is unlocked
	 * as well.
	 */
	if (cow_start != (u64)-1)
		cur_offset = cow_start;

	/*
	 * We need to lock the extent here because we're clearing DELALLOC and
	 * we're not locked at this point.
	 */
	if (cur_offset < end) {
		struct extent_state *cached = NULL;

		lock_extent(&inode->io_tree, cur_offset, end, &cached);
		extent_clear_unlock_delalloc(inode, cur_offset, end,
					     locked_folio, &cached,
					     EXTENT_LOCKED | EXTENT_DELALLOC |
					     EXTENT_DEFRAG |
					     EXTENT_DO_ACCOUNTING, PAGE_UNLOCK |
					     PAGE_START_WRITEBACK |
					     PAGE_END_WRITEBACK);
		btrfs_qgroup_free_data(inode, NULL, cur_offset, end - cur_offset + 1, NULL);
	}
	btrfs_free_path(path);
	return ret;
}

static bool should_nocow(struct btrfs_inode *inode, u64 start, u64 end)
{
	if (inode->flags & (BTRFS_INODE_NODATACOW | BTRFS_INODE_PREALLOC)) {
		if (inode->defrag_bytes &&
		    test_range_bit_exists(&inode->io_tree, start, end, EXTENT_DEFRAG))
			return false;
		return true;
	}
	return false;
}

/*
 * Function to process delayed allocation (create CoW) for ranges which are
 * being touched for the first time.
 */
int btrfs_run_delalloc_range(struct btrfs_inode *inode, struct folio *locked_folio,
			     u64 start, u64 end, struct writeback_control *wbc)
{
	const bool zoned = btrfs_is_zoned(inode->root->fs_info);
	int ret;

	/*
	 * The range must cover part of the @locked_folio, or a return of 1
	 * can confuse the caller.
	 */
	ASSERT(!(end <= folio_pos(locked_folio) ||
		 start >= folio_pos(locked_folio) + folio_size(locked_folio)));

	if (should_nocow(inode, start, end)) {
		ret = run_delalloc_nocow(inode, locked_folio, start, end);
		goto out;
	}

	if (btrfs_inode_can_compress(inode) &&
	    inode_need_compress(inode, start, end) &&
	    run_delalloc_compressed(inode, locked_folio, start, end, wbc))
		return 1;

	if (zoned)
		ret = run_delalloc_cow(inode, locked_folio, start, end, wbc,
				       true);
	else
		ret = cow_file_range(inode, locked_folio, start, end, NULL,
				     false, false);

out:
	if (ret < 0)
		btrfs_cleanup_ordered_extents(inode, locked_folio, start,
					      end - start + 1);
	return ret;
}

void btrfs_split_delalloc_extent(struct btrfs_inode *inode,
				 struct extent_state *orig, u64 split)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 size;

	lockdep_assert_held(&inode->io_tree.lock);

	/* not delalloc, ignore it */
	if (!(orig->state & EXTENT_DELALLOC))
		return;

	size = orig->end - orig->start + 1;
	if (size > fs_info->max_extent_size) {
		u32 num_extents;
		u64 new_size;

		/*
		 * See the explanation in btrfs_merge_delalloc_extent, the same
		 * applies here, just in reverse.
		 */
		new_size = orig->end - split + 1;
		num_extents = count_max_extents(fs_info, new_size);
		new_size = split - orig->start;
		num_extents += count_max_extents(fs_info, new_size);
		if (count_max_extents(fs_info, size) >= num_extents)
			return;
	}

	spin_lock(&inode->lock);
	btrfs_mod_outstanding_extents(inode, 1);
	spin_unlock(&inode->lock);
}

/*
 * Handle merged delayed allocation extents so we can keep track of new extents
 * that are just merged onto old extents, such as when we are doing sequential
 * writes, so we can properly account for the metadata space we'll need.
 */
void btrfs_merge_delalloc_extent(struct btrfs_inode *inode, struct extent_state *new,
				 struct extent_state *other)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 new_size, old_size;
	u32 num_extents;

	lockdep_assert_held(&inode->io_tree.lock);

	/* not delalloc, ignore it */
	if (!(other->state & EXTENT_DELALLOC))
		return;

	if (new->start > other->start)
		new_size = new->end - other->start + 1;
	else
		new_size = other->end - new->start + 1;

	/* we're not bigger than the max, unreserve the space and go */
	if (new_size <= fs_info->max_extent_size) {
		spin_lock(&inode->lock);
		btrfs_mod_outstanding_extents(inode, -1);
		spin_unlock(&inode->lock);
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
	num_extents = count_max_extents(fs_info, old_size);
	old_size = new->end - new->start + 1;
	num_extents += count_max_extents(fs_info, old_size);
	if (count_max_extents(fs_info, new_size) >= num_extents)
		return;

	spin_lock(&inode->lock);
	btrfs_mod_outstanding_extents(inode, -1);
	spin_unlock(&inode->lock);
}

static void btrfs_add_delalloc_inode(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;

	spin_lock(&root->delalloc_lock);
	ASSERT(list_empty(&inode->delalloc_inodes));
	list_add_tail(&inode->delalloc_inodes, &root->delalloc_inodes);
	root->nr_delalloc_inodes++;
	if (root->nr_delalloc_inodes == 1) {
		spin_lock(&fs_info->delalloc_root_lock);
		ASSERT(list_empty(&root->delalloc_root));
		list_add_tail(&root->delalloc_root, &fs_info->delalloc_roots);
		spin_unlock(&fs_info->delalloc_root_lock);
	}
	spin_unlock(&root->delalloc_lock);
}

void btrfs_del_delalloc_inode(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;

	lockdep_assert_held(&root->delalloc_lock);

	/*
	 * We may be called after the inode was already deleted from the list,
	 * namely in the transaction abort path btrfs_destroy_delalloc_inodes(),
	 * and then later through btrfs_clear_delalloc_extent() while the inode
	 * still has ->delalloc_bytes > 0.
	 */
	if (!list_empty(&inode->delalloc_inodes)) {
		list_del_init(&inode->delalloc_inodes);
		root->nr_delalloc_inodes--;
		if (!root->nr_delalloc_inodes) {
			ASSERT(list_empty(&root->delalloc_inodes));
			spin_lock(&fs_info->delalloc_root_lock);
			ASSERT(!list_empty(&root->delalloc_root));
			list_del_init(&root->delalloc_root);
			spin_unlock(&fs_info->delalloc_root_lock);
		}
	}
}

/*
 * Properly track delayed allocation bytes in the inode and to maintain the
 * list of inodes that have pending delalloc work to be done.
 */
void btrfs_set_delalloc_extent(struct btrfs_inode *inode, struct extent_state *state,
			       u32 bits)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	lockdep_assert_held(&inode->io_tree.lock);

	if ((bits & EXTENT_DEFRAG) && !(bits & EXTENT_DELALLOC))
		WARN_ON(1);
	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if (!(state->state & EXTENT_DELALLOC) && (bits & EXTENT_DELALLOC)) {
		u64 len = state->end + 1 - state->start;
		u64 prev_delalloc_bytes;
		u32 num_extents = count_max_extents(fs_info, len);

		spin_lock(&inode->lock);
		btrfs_mod_outstanding_extents(inode, num_extents);
		spin_unlock(&inode->lock);

		/* For sanity tests */
		if (btrfs_is_testing(fs_info))
			return;

		percpu_counter_add_batch(&fs_info->delalloc_bytes, len,
					 fs_info->delalloc_batch);
		spin_lock(&inode->lock);
		prev_delalloc_bytes = inode->delalloc_bytes;
		inode->delalloc_bytes += len;
		if (bits & EXTENT_DEFRAG)
			inode->defrag_bytes += len;
		spin_unlock(&inode->lock);

		/*
		 * We don't need to be under the protection of the inode's lock,
		 * because we are called while holding the inode's io_tree lock
		 * and are therefore protected against concurrent calls of this
		 * function and btrfs_clear_delalloc_extent().
		 */
		if (!btrfs_is_free_space_inode(inode) && prev_delalloc_bytes == 0)
			btrfs_add_delalloc_inode(inode);
	}

	if (!(state->state & EXTENT_DELALLOC_NEW) &&
	    (bits & EXTENT_DELALLOC_NEW)) {
		spin_lock(&inode->lock);
		inode->new_delalloc_bytes += state->end + 1 - state->start;
		spin_unlock(&inode->lock);
	}
}

/*
 * Once a range is no longer delalloc this function ensures that proper
 * accounting happens.
 */
void btrfs_clear_delalloc_extent(struct btrfs_inode *inode,
				 struct extent_state *state, u32 bits)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 len = state->end + 1 - state->start;
	u32 num_extents = count_max_extents(fs_info, len);

	lockdep_assert_held(&inode->io_tree.lock);

	if ((state->state & EXTENT_DEFRAG) && (bits & EXTENT_DEFRAG)) {
		spin_lock(&inode->lock);
		inode->defrag_bytes -= len;
		spin_unlock(&inode->lock);
	}

	/*
	 * set_bit and clear bit hooks normally require _irqsave/restore
	 * but in this case, we are only testing for the DELALLOC
	 * bit, which is only set or cleared with irqs on
	 */
	if ((state->state & EXTENT_DELALLOC) && (bits & EXTENT_DELALLOC)) {
		struct btrfs_root *root = inode->root;
		u64 new_delalloc_bytes;

		spin_lock(&inode->lock);
		btrfs_mod_outstanding_extents(inode, -num_extents);
		spin_unlock(&inode->lock);

		/*
		 * We don't reserve metadata space for space cache inodes so we
		 * don't need to call delalloc_release_metadata if there is an
		 * error.
		 */
		if (bits & EXTENT_CLEAR_META_RESV &&
		    root != fs_info->tree_root)
			btrfs_delalloc_release_metadata(inode, len, true);

		/* For sanity tests. */
		if (btrfs_is_testing(fs_info))
			return;

		if (!btrfs_is_data_reloc_root(root) &&
		    !btrfs_is_free_space_inode(inode) &&
		    !(state->state & EXTENT_NORESERVE) &&
		    (bits & EXTENT_CLEAR_DATA_RESV))
			btrfs_free_reserved_data_space_noquota(fs_info, len);

		percpu_counter_add_batch(&fs_info->delalloc_bytes, -len,
					 fs_info->delalloc_batch);
		spin_lock(&inode->lock);
		inode->delalloc_bytes -= len;
		new_delalloc_bytes = inode->delalloc_bytes;
		spin_unlock(&inode->lock);

		/*
		 * We don't need to be under the protection of the inode's lock,
		 * because we are called while holding the inode's io_tree lock
		 * and are therefore protected against concurrent calls of this
		 * function and btrfs_set_delalloc_extent().
		 */
		if (!btrfs_is_free_space_inode(inode) && new_delalloc_bytes == 0) {
			spin_lock(&root->delalloc_lock);
			btrfs_del_delalloc_inode(inode);
			spin_unlock(&root->delalloc_lock);
		}
	}

	if ((state->state & EXTENT_DELALLOC_NEW) &&
	    (bits & EXTENT_DELALLOC_NEW)) {
		spin_lock(&inode->lock);
		ASSERT(inode->new_delalloc_bytes >= len);
		inode->new_delalloc_bytes -= len;
		if (bits & EXTENT_ADD_INODE_BYTES)
			inode_add_bytes(&inode->vfs_inode, len);
		spin_unlock(&inode->lock);
	}
}

/*
 * given a list of ordered sums record them in the inode.  This happens
 * at IO completion time based on sums calculated at bio submission time.
 */
static int add_pending_csums(struct btrfs_trans_handle *trans,
			     struct list_head *list)
{
	struct btrfs_ordered_sum *sum;
	struct btrfs_root *csum_root = NULL;
	int ret;

	list_for_each_entry(sum, list, list) {
		trans->adding_csums = true;
		if (!csum_root)
			csum_root = btrfs_csum_root(trans->fs_info,
						    sum->logical);
		ret = btrfs_csum_file_blocks(trans, csum_root, sum);
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

		em = btrfs_get_extent(inode, NULL, search_start, search_len);
		if (IS_ERR(em))
			return PTR_ERR(em);

		if (em->disk_bytenr != EXTENT_MAP_HOLE)
			goto next;

		em_len = em->len;
		if (em->start < search_start)
			em_len -= search_start - em->start;
		if (em_len > search_len)
			em_len = search_len;

		ret = set_extent_bit(&inode->io_tree, search_start,
				     search_start + em_len - 1,
				     EXTENT_DELALLOC_NEW, cached_state);
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

	return set_extent_bit(&inode->io_tree, start, end,
			      EXTENT_DELALLOC | extra_bits, cached_state);
}

/* see btrfs_writepage_start_hook for details on why this is required */
struct btrfs_writepage_fixup {
	struct folio *folio;
	struct btrfs_inode *inode;
	struct btrfs_work work;
};

static void btrfs_writepage_fixup_worker(struct btrfs_work *work)
{
	struct btrfs_writepage_fixup *fixup =
		container_of(work, struct btrfs_writepage_fixup, work);
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_changeset *data_reserved = NULL;
	struct folio *folio = fixup->folio;
	struct btrfs_inode *inode = fixup->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 page_start = folio_pos(folio);
	u64 page_end = folio_pos(folio) + folio_size(folio) - 1;
	int ret = 0;
	bool free_delalloc_space = true;

	/*
	 * This is similar to page_mkwrite, we need to reserve the space before
	 * we take the folio lock.
	 */
	ret = btrfs_delalloc_reserve_space(inode, &data_reserved, page_start,
					   folio_size(folio));
again:
	folio_lock(folio);

	/*
	 * Before we queued this fixup, we took a reference on the folio.
	 * folio->mapping may go NULL, but it shouldn't be moved to a different
	 * address space.
	 */
	if (!folio->mapping || !folio_test_dirty(folio) ||
	    !folio_test_checked(folio)) {
		/*
		 * Unfortunately this is a little tricky, either
		 *
		 * 1) We got here and our folio had already been dealt with and
		 *    we reserved our space, thus ret == 0, so we need to just
		 *    drop our space reservation and bail.  This can happen the
		 *    first time we come into the fixup worker, or could happen
		 *    while waiting for the ordered extent.
		 * 2) Our folio was already dealt with, but we happened to get an
		 *    ENOSPC above from the btrfs_delalloc_reserve_space.  In
		 *    this case we obviously don't have anything to release, but
		 *    because the folio was already dealt with we don't want to
		 *    mark the folio with an error, so make sure we're resetting
		 *    ret to 0.  This is why we have this check _before_ the ret
		 *    check, because we do not want to have a surprise ENOSPC
		 *    when the folio was already properly dealt with.
		 */
		if (!ret) {
			btrfs_delalloc_release_extents(inode, folio_size(folio));
			btrfs_delalloc_release_space(inode, data_reserved,
						     page_start, folio_size(folio),
						     true);
		}
		ret = 0;
		goto out_page;
	}

	/*
	 * We can't mess with the folio state unless it is locked, so now that
	 * it is locked bail if we failed to make our space reservation.
	 */
	if (ret)
		goto out_page;

	lock_extent(&inode->io_tree, page_start, page_end, &cached_state);

	/* already ordered? We're done */
	if (folio_test_ordered(folio))
		goto out_reserved;

	ordered = btrfs_lookup_ordered_range(inode, page_start, PAGE_SIZE);
	if (ordered) {
		unlock_extent(&inode->io_tree, page_start, page_end,
			      &cached_state);
		folio_unlock(folio);
		btrfs_start_ordered_extent(ordered);
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
	BUG_ON(!folio_test_dirty(folio));
	free_delalloc_space = false;
out_reserved:
	btrfs_delalloc_release_extents(inode, PAGE_SIZE);
	if (free_delalloc_space)
		btrfs_delalloc_release_space(inode, data_reserved, page_start,
					     PAGE_SIZE, true);
	unlock_extent(&inode->io_tree, page_start, page_end, &cached_state);
out_page:
	if (ret) {
		/*
		 * We hit ENOSPC or other errors.  Update the mapping and page
		 * to reflect the errors and clean the page.
		 */
		mapping_set_error(folio->mapping, ret);
		btrfs_mark_ordered_io_finished(inode, folio, page_start,
					       folio_size(folio), !ret);
		folio_clear_dirty_for_io(folio);
	}
	btrfs_folio_clear_checked(fs_info, folio, page_start, PAGE_SIZE);
	folio_unlock(folio);
	folio_put(folio);
	kfree(fixup);
	extent_changeset_free(data_reserved);
	/*
	 * As a precaution, do a delayed iput in case it would be the last iput
	 * that could need flushing space. Recursing back to fixup worker would
	 * deadlock.
	 */
	btrfs_add_delayed_iput(inode);
}

/*
 * There are a few paths in the higher layers of the kernel that directly
 * set the folio dirty bit without asking the filesystem if it is a
 * good idea.  This causes problems because we want to make sure COW
 * properly happens and the data=ordered rules are followed.
 *
 * In our case any range that doesn't have the ORDERED bit set
 * hasn't been properly setup for IO.  We kick off an async process
 * to fix it up.  The async helper will wait for ordered extents, set
 * the delalloc bit and make it safe to write the folio.
 */
int btrfs_writepage_cow_fixup(struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	struct btrfs_writepage_fixup *fixup;

	/* This folio has ordered extent covering it already */
	if (folio_test_ordered(folio))
		return 0;

	/*
	 * folio_checked is set below when we create a fixup worker for this
	 * folio, don't try to create another one if we're already
	 * folio_test_checked.
	 *
	 * The extent_io writepage code will redirty the foio if we send back
	 * EAGAIN.
	 */
	if (folio_test_checked(folio))
		return -EAGAIN;

	fixup = kzalloc(sizeof(*fixup), GFP_NOFS);
	if (!fixup)
		return -EAGAIN;

	/*
	 * We are already holding a reference to this inode from
	 * write_cache_pages.  We need to hold it because the space reservation
	 * takes place outside of the folio lock, and we can't trust
	 * page->mapping outside of the folio lock.
	 */
	ihold(inode);
	btrfs_folio_set_checked(fs_info, folio, folio_pos(folio), folio_size(folio));
	folio_get(folio);
	btrfs_init_work(&fixup->work, btrfs_writepage_fixup_worker, NULL);
	fixup->folio = folio;
	fixup->inode = BTRFS_I(inode);
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
	u64 offset = btrfs_stack_file_extent_offset(stack_fi);
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

	btrfs_mark_buffer_dirty(trans, leaf);
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
					       file_pos - offset,
					       qgroup_reserved, &ins);
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
	bool update_inode_bytes;
	u64 num_bytes = oe->num_bytes;
	u64 ram_bytes = oe->ram_bytes;

	memset(&stack_fi, 0, sizeof(stack_fi));
	btrfs_set_stack_file_extent_type(&stack_fi, BTRFS_FILE_EXTENT_REG);
	btrfs_set_stack_file_extent_disk_bytenr(&stack_fi, oe->disk_bytenr);
	btrfs_set_stack_file_extent_disk_num_bytes(&stack_fi,
						   oe->disk_num_bytes);
	btrfs_set_stack_file_extent_offset(&stack_fi, oe->offset);
	if (test_bit(BTRFS_ORDERED_TRUNCATED, &oe->flags))
		num_bytes = oe->truncated_len;
	btrfs_set_stack_file_extent_num_bytes(&stack_fi, num_bytes);
	btrfs_set_stack_file_extent_ram_bytes(&stack_fi, ram_bytes);
	btrfs_set_stack_file_extent_compression(&stack_fi, oe->compress_type);
	/* Encryption and other encoding is reserved and all 0 */

	/*
	 * For delalloc, when completing an ordered extent we update the inode's
	 * bytes when clearing the range in the inode's io tree, so pass false
	 * as the argument 'update_inode_bytes' to insert_reserved_file_extent(),
	 * except if the ordered extent was truncated.
	 */
	update_inode_bytes = test_bit(BTRFS_ORDERED_DIRECT, &oe->flags) ||
			     test_bit(BTRFS_ORDERED_ENCODED, &oe->flags) ||
			     test_bit(BTRFS_ORDERED_TRUNCATED, &oe->flags);

	return insert_reserved_file_extent(trans, oe->inode,
					   oe->file_offset, &stack_fi,
					   update_inode_bytes, oe->qgroup_rsv);
}

/*
 * As ordered data IO finishes, this gets called so we can finish
 * an ordered extent if the range of bytes in the file it covers are
 * fully written.
 */
int btrfs_finish_one_ordered(struct btrfs_ordered_extent *ordered_extent)
{
	struct btrfs_inode *inode = ordered_extent->inode;
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
	    !test_bit(BTRFS_ORDERED_DIRECT, &ordered_extent->flags) &&
	    !test_bit(BTRFS_ORDERED_ENCODED, &ordered_extent->flags))
		clear_bits |= EXTENT_DELALLOC_NEW;

	freespace_inode = btrfs_is_free_space_inode(inode);
	if (!freespace_inode)
		btrfs_lockdep_acquire(fs_info, btrfs_ordered_extent);

	if (test_bit(BTRFS_ORDERED_IOERR, &ordered_extent->flags)) {
		ret = -EIO;
		goto out;
	}

	if (btrfs_is_zoned(fs_info))
		btrfs_zone_finish_endio(fs_info, ordered_extent->disk_bytenr,
					ordered_extent->disk_num_bytes);

	if (test_bit(BTRFS_ORDERED_TRUNCATED, &ordered_extent->flags)) {
		truncated = true;
		logical_len = ordered_extent->truncated_len;
		/* Truncated the entire extent, don't bother adding */
		if (!logical_len)
			goto out;
	}

	/*
	 * If it's a COW write we need to lock the extent range as we will be
	 * inserting/replacing file extent items and unpinning an extent map.
	 * This must be taken before joining a transaction, as it's a higher
	 * level lock (like the inode's VFS lock), otherwise we can run into an
	 * ABBA deadlock with other tasks (transactions work like a lock,
	 * depending on their current state).
	 */
	if (!test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags)) {
		clear_bits |= EXTENT_LOCKED;
		lock_extent(io_tree, start, end, &cached_state);
	}

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

	ret = btrfs_insert_raid_extent(trans, ordered_extent);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	if (test_bit(BTRFS_ORDERED_NOCOW, &ordered_extent->flags)) {
		/* Logic error */
		ASSERT(list_empty(&ordered_extent->list));
		if (!list_empty(&ordered_extent->list)) {
			ret = -EINVAL;
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		btrfs_inode_safe_disk_i_size_write(inode, 0);
		ret = btrfs_update_inode_fallback(trans, inode);
		if (ret) {
			/* -ENOMEM or corruption */
			btrfs_abort_transaction(trans, ret);
		}
		goto out;
	}

	if (test_bit(BTRFS_ORDERED_COMPRESSED, &ordered_extent->flags))
		compress_type = ordered_extent->compress_type;
	if (test_bit(BTRFS_ORDERED_PREALLOC, &ordered_extent->flags)) {
		BUG_ON(compress_type);
		ret = btrfs_mark_extent_written(trans, inode,
						ordered_extent->file_offset,
						ordered_extent->file_offset +
						logical_len);
		btrfs_zoned_release_data_reloc_bg(fs_info, ordered_extent->disk_bytenr,
						  ordered_extent->disk_num_bytes);
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
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	ret = unpin_extent_cache(inode, ordered_extent->file_offset,
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
				 &cached_state);

	btrfs_inode_safe_disk_i_size_write(inode, 0);
	ret = btrfs_update_inode_fallback(trans, inode);
	if (ret) { /* -ENOMEM or corruption */
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
out:
	clear_extent_bit(&inode->io_tree, start, end, clear_bits,
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
		if (ret)
			btrfs_mark_ordered_extent_error(ordered_extent);

		if (truncated)
			unwritten_start += logical_len;
		clear_extent_uptodate(io_tree, unwritten_start, end, NULL);

		/*
		 * Drop extent maps for the part of the extent we didn't write.
		 *
		 * We have an exception here for the free_space_inode, this is
		 * because when we do btrfs_get_extent() on the free space inode
		 * we will search the commit root.  If this is a new block group
		 * we won't find anything, and we will trip over the assert in
		 * writepage where we do ASSERT(em->block_start !=
		 * EXTENT_MAP_HOLE).
		 *
		 * Theoretically we could also skip this for any NOCOW extent as
		 * we don't mess with the extent map tree in the NOCOW case, but
		 * for now simply skip this if we are the free space inode.
		 */
		if (!btrfs_is_free_space_inode(inode))
			btrfs_drop_extent_map_range(inode, unwritten_start,
						    end, false);

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
			/*
			 * Actually free the qgroup rsv which was released when
			 * the ordered extent was created.
			 */
			btrfs_qgroup_free_refroot(fs_info, btrfs_root_id(inode->root),
						  ordered_extent->qgroup_rsv,
						  BTRFS_QGROUP_RSV_DATA);
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

int btrfs_finish_ordered_io(struct btrfs_ordered_extent *ordered)
{
	if (btrfs_is_zoned(ordered->inode->root->fs_info) &&
	    !test_bit(BTRFS_ORDERED_IOERR, &ordered->flags) &&
	    list_empty(&ordered->bioc_list))
		btrfs_finish_ordered_zoned(ordered);
	return btrfs_finish_one_ordered(ordered);
}

/*
 * Verify the checksum for a single sector without any extra action that depend
 * on the type of I/O.
 */
int btrfs_check_sector_csum(struct btrfs_fs_info *fs_info, struct page *page,
			    u32 pgoff, u8 *csum, const u8 * const csum_expected)
{
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	char *kaddr;

	ASSERT(pgoff + fs_info->sectorsize <= PAGE_SIZE);

	shash->tfm = fs_info->csum_shash;

	kaddr = kmap_local_page(page) + pgoff;
	crypto_shash_digest(shash, kaddr, fs_info->sectorsize, csum);
	kunmap_local(kaddr);

	if (memcmp(csum, csum_expected, fs_info->csum_size))
		return -EIO;
	return 0;
}

/*
 * Verify the checksum of a single data sector.
 *
 * @bbio:	btrfs_io_bio which contains the csum
 * @dev:	device the sector is on
 * @bio_offset:	offset to the beginning of the bio (in bytes)
 * @bv:		bio_vec to check
 *
 * Check if the checksum on a data block is valid.  When a checksum mismatch is
 * detected, report the error and fill the corrupted range with zero.
 *
 * Return %true if the sector is ok or had no checksum to start with, else %false.
 */
bool btrfs_data_csum_ok(struct btrfs_bio *bbio, struct btrfs_device *dev,
			u32 bio_offset, struct bio_vec *bv)
{
	struct btrfs_inode *inode = bbio->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u64 file_offset = bbio->file_offset + bio_offset;
	u64 end = file_offset + bv->bv_len - 1;
	u8 *csum_expected;
	u8 csum[BTRFS_CSUM_SIZE];

	ASSERT(bv->bv_len == fs_info->sectorsize);

	if (!bbio->csum)
		return true;

	if (btrfs_is_data_reloc_root(inode->root) &&
	    test_range_bit(&inode->io_tree, file_offset, end, EXTENT_NODATASUM,
			   NULL)) {
		/* Skip the range without csum for data reloc inode */
		clear_extent_bits(&inode->io_tree, file_offset, end,
				  EXTENT_NODATASUM);
		return true;
	}

	csum_expected = bbio->csum + (bio_offset >> fs_info->sectorsize_bits) *
				fs_info->csum_size;
	if (btrfs_check_sector_csum(fs_info, bv->bv_page, bv->bv_offset, csum,
				    csum_expected))
		goto zeroit;
	return true;

zeroit:
	btrfs_print_data_csum_error(inode, file_offset, csum, csum_expected,
				    bbio->mirror_num);
	if (dev)
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_CORRUPTION_ERRS);
	memzero_bvec(bv);
	return false;
}

/*
 * Perform a delayed iput on @inode.
 *
 * @inode: The inode we want to perform iput on
 *
 * This function uses the generic vfs_inode::i_count to track whether we should
 * just decrement it (in case it's > 1) or if this is the last iput then link
 * the inode to the delayed iput machinery. Delayed iputs are processed at
 * transaction commit time/superblock commit/cleaner kthread.
 */
void btrfs_add_delayed_iput(struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	unsigned long flags;

	if (atomic_add_unless(&inode->vfs_inode.i_count, -1, 1))
		return;

	atomic_inc(&fs_info->nr_delayed_iputs);
	/*
	 * Need to be irq safe here because we can be called from either an irq
	 * context (see bio.c and btrfs_put_ordered_extent()) or a non-irq
	 * context.
	 */
	spin_lock_irqsave(&fs_info->delayed_iput_lock, flags);
	ASSERT(list_empty(&inode->delayed_iput));
	list_add_tail(&inode->delayed_iput, &fs_info->delayed_iputs);
	spin_unlock_irqrestore(&fs_info->delayed_iput_lock, flags);
	if (!test_bit(BTRFS_FS_CLEANER_RUNNING, &fs_info->flags))
		wake_up_process(fs_info->cleaner_kthread);
}

static void run_delayed_iput_locked(struct btrfs_fs_info *fs_info,
				    struct btrfs_inode *inode)
{
	list_del_init(&inode->delayed_iput);
	spin_unlock_irq(&fs_info->delayed_iput_lock);
	iput(&inode->vfs_inode);
	if (atomic_dec_and_test(&fs_info->nr_delayed_iputs))
		wake_up(&fs_info->delayed_iputs_wait);
	spin_lock_irq(&fs_info->delayed_iput_lock);
}

static void btrfs_run_delayed_iput(struct btrfs_fs_info *fs_info,
				   struct btrfs_inode *inode)
{
	if (!list_empty(&inode->delayed_iput)) {
		spin_lock_irq(&fs_info->delayed_iput_lock);
		if (!list_empty(&inode->delayed_iput))
			run_delayed_iput_locked(fs_info, inode);
		spin_unlock_irq(&fs_info->delayed_iput_lock);
	}
}

void btrfs_run_delayed_iputs(struct btrfs_fs_info *fs_info)
{
	/*
	 * btrfs_put_ordered_extent() can run in irq context (see bio.c), which
	 * calls btrfs_add_delayed_iput() and that needs to lock
	 * fs_info->delayed_iput_lock. So we need to disable irqs here to
	 * prevent a deadlock.
	 */
	spin_lock_irq(&fs_info->delayed_iput_lock);
	while (!list_empty(&fs_info->delayed_iputs)) {
		struct btrfs_inode *inode;

		inode = list_first_entry(&fs_info->delayed_iputs,
				struct btrfs_inode, delayed_iput);
		run_delayed_iput_locked(fs_info, inode);
		if (need_resched()) {
			spin_unlock_irq(&fs_info->delayed_iput_lock);
			cond_resched();
			spin_lock_irq(&fs_info->delayed_iput_lock);
		}
	}
	spin_unlock_irq(&fs_info->delayed_iput_lock);
}

/*
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

	if (test_and_set_bit(BTRFS_ROOT_ORPHAN_CLEANUP, &root->state))
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
			/*
			 * We found the same inode as before. This means we were
			 * not able to remove its items via eviction triggered
			 * by an iput(). A transaction abort may have happened,
			 * due to -ENOSPC for example, so try to grab the error
			 * that lead to a transaction abort, if any.
			 */
			btrfs_err(fs_info,
				  "Error removing orphan entry, stopping orphan cleanup");
			ret = BTRFS_FS_ERROR(fs_info) ?: -EINVAL;
			goto out;
		}

		last_objectid = found_key.offset;

		found_key.objectid = found_key.offset;
		found_key.type = BTRFS_INODE_ITEM_KEY;
		found_key.offset = 0;
		inode = btrfs_iget(last_objectid, root);
		if (IS_ERR(inode)) {
			ret = PTR_ERR(inode);
			inode = NULL;
			if (ret != -ENOENT)
				goto out;
		}

		if (!inode && root == fs_info->tree_root) {
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
		if (!inode || inode->i_nlink) {
			if (inode) {
				ret = btrfs_drop_verity_items(BTRFS_I(inode));
				iput(inode);
				inode = NULL;
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

static int btrfs_init_file_extent_tree(struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;

	if (WARN_ON_ONCE(inode->file_extent_tree))
		return 0;
	if (btrfs_fs_incompat(fs_info, NO_HOLES))
		return 0;
	if (!S_ISREG(inode->vfs_inode.i_mode))
		return 0;
	if (btrfs_is_free_space_inode(inode))
		return 0;

	inode->file_extent_tree = kmalloc(sizeof(struct extent_io_tree), GFP_KERNEL);
	if (!inode->file_extent_tree)
		return -ENOMEM;

	extent_io_tree_init(fs_info, inode->file_extent_tree, IO_TREE_INODE_FILE_EXTENT);
	/* Lockdep class is set only for the file extent tree. */
	lockdep_set_class(&inode->file_extent_tree->lock, &file_extent_tree_class);

	return 0;
}

static int btrfs_add_inode_to_root(struct btrfs_inode *inode, bool prealloc)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_inode *existing;
	const u64 ino = btrfs_ino(inode);
	int ret;

	if (inode_unhashed(&inode->vfs_inode))
		return 0;

	if (prealloc) {
		ret = xa_reserve(&root->inodes, ino, GFP_NOFS);
		if (ret)
			return ret;
	}

	existing = xa_store(&root->inodes, ino, inode, GFP_ATOMIC);

	if (xa_is_err(existing)) {
		ret = xa_err(existing);
		ASSERT(ret != -EINVAL);
		ASSERT(ret != -ENOMEM);
		return ret;
	} else if (existing) {
		WARN_ON(!(existing->vfs_inode.i_state & (I_WILL_FREE | I_FREEING)));
	}

	return 0;
}

/*
 * Read a locked inode from the btree into the in-memory inode and add it to
 * its root list/tree.
 *
 * On failure clean up the inode.
 */
static int btrfs_read_locked_inode(struct inode *inode, struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
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

	ret = btrfs_init_file_extent_tree(BTRFS_I(inode));
	if (ret)
		goto out;

	ret = btrfs_fill_inode(inode, &rdev);
	if (!ret)
		filled = true;

	ASSERT(path);

	btrfs_get_inode_key(BTRFS_I(inode), &location);

	ret = btrfs_lookup_inode(NULL, root, path, &location, 0);
	if (ret) {
		/*
		 * ret > 0 can come from btrfs_search_slot called by
		 * btrfs_lookup_inode(), this means the inode was not found.
		 */
		if (ret > 0)
			ret = -ENOENT;
		goto out;
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

	inode_set_atime(inode, btrfs_timespec_sec(leaf, &inode_item->atime),
			btrfs_timespec_nsec(leaf, &inode_item->atime));

	inode_set_mtime(inode, btrfs_timespec_sec(leaf, &inode_item->mtime),
			btrfs_timespec_nsec(leaf, &inode_item->mtime));

	inode_set_ctime(inode, btrfs_timespec_sec(leaf, &inode_item->ctime),
			btrfs_timespec_nsec(leaf, &inode_item->ctime));

	BTRFS_I(inode)->i_otime_sec = btrfs_timespec_sec(leaf, &inode_item->otime);
	BTRFS_I(inode)->i_otime_nsec = btrfs_timespec_nsec(leaf, &inode_item->otime);

	inode_set_bytes(inode, btrfs_inode_nbytes(leaf, inode_item));
	BTRFS_I(inode)->generation = btrfs_inode_generation(leaf, inode_item);
	BTRFS_I(inode)->last_trans = btrfs_inode_transid(leaf, inode_item);

	inode_set_iversion_queried(inode,
				   btrfs_inode_sequence(leaf, inode_item));
	inode->i_generation = BTRFS_I(inode)->generation;
	inode->i_rdev = 0;
	rdev = btrfs_inode_rdev(leaf, inode_item);

	if (S_ISDIR(inode->i_mode))
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
	 * in the delayed_nodes xarray.
	 */
	if (BTRFS_I(inode)->last_trans == btrfs_get_fs_generation(fs_info))
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
				  btrfs_root_id(root), ret);
	}

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

	ret = btrfs_add_inode_to_root(BTRFS_I(inode), true);
	if (ret)
		goto out;

	return 0;
out:
	iget_failed(inode);
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
	u64 flags;

	btrfs_init_map_token(&token, leaf);

	btrfs_set_token_inode_uid(&token, item, i_uid_read(inode));
	btrfs_set_token_inode_gid(&token, item, i_gid_read(inode));
	btrfs_set_token_inode_size(&token, item, BTRFS_I(inode)->disk_i_size);
	btrfs_set_token_inode_mode(&token, item, inode->i_mode);
	btrfs_set_token_inode_nlink(&token, item, inode->i_nlink);

	btrfs_set_token_timespec_sec(&token, &item->atime,
				     inode_get_atime_sec(inode));
	btrfs_set_token_timespec_nsec(&token, &item->atime,
				      inode_get_atime_nsec(inode));

	btrfs_set_token_timespec_sec(&token, &item->mtime,
				     inode_get_mtime_sec(inode));
	btrfs_set_token_timespec_nsec(&token, &item->mtime,
				      inode_get_mtime_nsec(inode));

	btrfs_set_token_timespec_sec(&token, &item->ctime,
				     inode_get_ctime_sec(inode));
	btrfs_set_token_timespec_nsec(&token, &item->ctime,
				      inode_get_ctime_nsec(inode));

	btrfs_set_token_timespec_sec(&token, &item->otime, BTRFS_I(inode)->i_otime_sec);
	btrfs_set_token_timespec_nsec(&token, &item->otime, BTRFS_I(inode)->i_otime_nsec);

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
					    struct btrfs_inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	btrfs_get_inode_key(inode, &key);
	ret = btrfs_lookup_inode(trans, inode->root, path, &key, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);

	fill_inode_item(trans, leaf, inode_item, &inode->vfs_inode);
	btrfs_mark_buffer_dirty(trans, leaf);
	btrfs_set_inode_last_trans(trans, inode);
	ret = 0;
failed:
	btrfs_free_path(path);
	return ret;
}

/*
 * copy everything in the in-memory inode into the btree.
 */
int btrfs_update_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
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

		ret = btrfs_delayed_update_inode(trans, inode);
		if (!ret)
			btrfs_set_inode_last_trans(trans, inode);
		return ret;
	}

	return btrfs_update_inode_item(trans, inode);
}

int btrfs_update_inode_fallback(struct btrfs_trans_handle *trans,
				struct btrfs_inode *inode)
{
	int ret;

	ret = btrfs_update_inode(trans, inode);
	if (ret == -ENOSPC)
		return btrfs_update_inode_item(trans, inode);
	return ret;
}

/*
 * unlink helper that gets used here in inode.c and in the tree logging
 * recovery code.  It remove a link in a directory with a given name, and
 * also drops the back refs in the inode to the directory
 */
static int __btrfs_unlink_inode(struct btrfs_trans_handle *trans,
				struct btrfs_inode *dir,
				struct btrfs_inode *inode,
				const struct fscrypt_str *name,
				struct btrfs_rename_ctx *rename_ctx)
{
	struct btrfs_root *root = dir->root;
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

	di = btrfs_lookup_dir_item(trans, root, path, dir_ino, name, -1);
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

	ret = btrfs_del_inode_ref(trans, root, name, ino, dir_ino, &index);
	if (ret) {
		btrfs_info(fs_info,
			"failed to delete reference to %.*s, inode %llu parent %llu",
			name->len, name->name, ino, dir_ino);
		btrfs_abort_transaction(trans, ret);
		goto err;
	}
skip_backref:
	if (rename_ctx)
		rename_ctx->index = index;

	ret = btrfs_delete_delayed_dir_index(trans, dir, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto err;
	}

	/*
	 * If we are in a rename context, we don't need to update anything in the
	 * log. That will be done later during the rename by btrfs_log_new_name().
	 * Besides that, doing it here would only cause extra unnecessary btree
	 * operations on the log tree, increasing latency for applications.
	 */
	if (!rename_ctx) {
		btrfs_del_inode_ref_in_log(trans, root, name, inode, dir_ino);
		btrfs_del_dir_entries_in_log(trans, root, name, dir, index);
	}

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

	btrfs_i_size_write(dir, dir->vfs_inode.i_size - name->len * 2);
	inode_inc_iversion(&inode->vfs_inode);
	inode_set_ctime_current(&inode->vfs_inode);
	inode_inc_iversion(&dir->vfs_inode);
 	inode_set_mtime_to_ts(&dir->vfs_inode, inode_set_ctime_current(&dir->vfs_inode));
	ret = btrfs_update_inode(trans, dir);
out:
	return ret;
}

int btrfs_unlink_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_inode *dir, struct btrfs_inode *inode,
		       const struct fscrypt_str *name)
{
	int ret;

	ret = __btrfs_unlink_inode(trans, dir, inode, name, NULL);
	if (!ret) {
		drop_nlink(&inode->vfs_inode);
		ret = btrfs_update_inode(trans, inode);
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
static struct btrfs_trans_handle *__unlink_start_trans(struct btrfs_inode *dir)
{
	struct btrfs_root *root = dir->root;

	return btrfs_start_transaction_fallback_global_rsv(root,
						   BTRFS_UNLINK_METADATA_UNITS);
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_trans_handle *trans;
	struct inode *inode = d_inode(dentry);
	int ret;
	struct fscrypt_name fname;

	ret = fscrypt_setup_filename(dir, &dentry->d_name, 1, &fname);
	if (ret)
		return ret;

	/* This needs to handle no-key deletions later on */

	trans = __unlink_start_trans(BTRFS_I(dir));
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto fscrypt_free;
	}

	btrfs_record_unlink_dir(trans, BTRFS_I(dir), BTRFS_I(d_inode(dentry)),
				false);

	ret = btrfs_unlink_inode(trans, BTRFS_I(dir), BTRFS_I(d_inode(dentry)),
				 &fname.disk_name);
	if (ret)
		goto end_trans;

	if (inode->i_nlink == 0) {
		ret = btrfs_orphan_add(trans, BTRFS_I(inode));
		if (ret)
			goto end_trans;
	}

end_trans:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(BTRFS_I(dir)->root->fs_info);
fscrypt_free:
	fscrypt_free_filename(&fname);
	return ret;
}

static int btrfs_unlink_subvol(struct btrfs_trans_handle *trans,
			       struct btrfs_inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root = dir->root;
	struct btrfs_inode *inode = BTRFS_I(d_inode(dentry));
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	u64 index;
	int ret;
	u64 objectid;
	u64 dir_ino = btrfs_ino(dir);
	struct fscrypt_name fname;

	ret = fscrypt_setup_filename(&dir->vfs_inode, &dentry->d_name, 1, &fname);
	if (ret)
		return ret;

	/* This needs to handle no-key deletions later on */

	if (btrfs_ino(inode) == BTRFS_FIRST_FREE_OBJECTID) {
		objectid = btrfs_root_id(inode->root);
	} else if (btrfs_ino(inode) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID) {
		objectid = inode->ref_root_id;
	} else {
		WARN_ON(1);
		fscrypt_free_filename(&fname);
		return -EINVAL;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	di = btrfs_lookup_dir_item(trans, root, path, dir_ino,
				   &fname.disk_name, -1);
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
		di = btrfs_search_dir_index_item(root, path, dir_ino, &fname.disk_name);
		if (IS_ERR(di)) {
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
					 btrfs_root_id(root), dir_ino,
					 &index, &fname.disk_name);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	}

	ret = btrfs_delete_delayed_dir_index(trans, dir, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	btrfs_i_size_write(dir, dir->vfs_inode.i_size - fname.disk_name.len * 2);
	inode_inc_iversion(&dir->vfs_inode);
	inode_set_mtime_to_ts(&dir->vfs_inode, inode_set_ctime_current(&dir->vfs_inode));
	ret = btrfs_update_inode_fallback(trans, dir);
	if (ret)
		btrfs_abort_transaction(trans, ret);
out:
	btrfs_free_path(path);
	fscrypt_free_filename(&fname);
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
	struct fscrypt_str name = FSTR_INIT("default", 7);
	u64 dir_id;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/* Make sure this root isn't set as the default subvol */
	dir_id = btrfs_super_root_dir(fs_info->super_copy);
	di = btrfs_lookup_dir_item(NULL, fs_info->tree_root, path,
				   dir_id, &name, 0);
	if (di && !IS_ERR(di)) {
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &key);
		if (key.objectid == btrfs_root_id(root)) {
			ret = -EPERM;
			btrfs_err(fs_info,
				  "deleting default subvolume %llu is not allowed",
				  key.objectid);
			goto out;
		}
		btrfs_release_path(path);
	}

	key.objectid = btrfs_root_id(root);
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, fs_info->tree_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret == 0) {
		/*
		 * Key with offset -1 found, there would have to exist a root
		 * with such id, but this is out of valid range.
		 */
		ret = -EUCLEAN;
		goto out;
	}

	ret = 0;
	if (path->slots[0] > 0) {
		path->slots[0]--;
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == btrfs_root_id(root) && key.type == BTRFS_ROOT_REF_KEY)
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
	struct btrfs_inode *inode;
	u64 min_ino = 0;

	if (!BTRFS_FS_ERROR(fs_info))
		WARN_ON(btrfs_root_refs(&root->root_item) != 0);

	inode = btrfs_find_first_inode(root, min_ino);
	while (inode) {
		if (atomic_read(&inode->vfs_inode.i_count) > 1)
			d_prune_aliases(&inode->vfs_inode);

		min_ino = btrfs_ino(inode) + 1;
		/*
		 * btrfs_drop_inode() will have it removed from the inode
		 * cache when its usage count hits zero.
		 */
		iput(&inode->vfs_inode);
		cond_resched();
		inode = btrfs_find_first_inode(root, min_ino);
	}
}

int btrfs_delete_subvolume(struct btrfs_inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root = dir->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct inode *inode = d_inode(dentry);
	struct btrfs_root *dest = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct btrfs_block_rsv block_rsv;
	u64 root_flags;
	u64 qgroup_reserved = 0;
	int ret;

	down_write(&fs_info->subvol_sem);

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
			   btrfs_root_id(dest));
		ret = -EPERM;
		goto out_up_write;
	}
	if (atomic_read(&dest->nr_swapfiles)) {
		spin_unlock(&dest->root_item_lock);
		btrfs_warn(fs_info,
			   "attempt to delete subvolume %llu with active swapfile",
			   btrfs_root_id(root));
		ret = -EPERM;
		goto out_up_write;
	}
	root_flags = btrfs_root_flags(&dest->root_item);
	btrfs_set_root_flags(&dest->root_item,
			     root_flags | BTRFS_ROOT_SUBVOL_DEAD);
	spin_unlock(&dest->root_item_lock);

	ret = may_destroy_subvol(dest);
	if (ret)
		goto out_undead;

	btrfs_init_block_rsv(&block_rsv, BTRFS_BLOCK_RSV_TEMP);
	/*
	 * One for dir inode,
	 * two for dir entries,
	 * two for root ref/backref.
	 */
	ret = btrfs_subvolume_reserve_metadata(root, &block_rsv, 5, true);
	if (ret)
		goto out_undead;
	qgroup_reserved = block_rsv.qgroup_rsv_reserved;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_release;
	}
	btrfs_qgroup_convert_reserved_meta(root, qgroup_reserved);
	qgroup_reserved = 0;
	trans->block_rsv = &block_rsv;
	trans->bytes_reserved = block_rsv.size;

	btrfs_record_snapshot_destroy(trans, dir);

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
					btrfs_root_id(dest));
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	}

	ret = btrfs_uuid_tree_remove(trans, dest->root_item.uuid,
				     BTRFS_UUID_KEY_SUBVOL, btrfs_root_id(dest));
	if (ret && ret != -ENOENT) {
		btrfs_abort_transaction(trans, ret);
		goto out_end_trans;
	}
	if (!btrfs_is_empty_uuid(dest->root_item.received_uuid)) {
		ret = btrfs_uuid_tree_remove(trans,
					  dest->root_item.received_uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  btrfs_root_id(dest));
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
	btrfs_block_rsv_release(fs_info, &block_rsv, (u64)-1, NULL);
	if (qgroup_reserved)
		btrfs_qgroup_free_meta_prealloc(root, qgroup_reserved);
out_undead:
	if (ret) {
		spin_lock(&dest->root_item_lock);
		root_flags = btrfs_root_flags(&dest->root_item);
		btrfs_set_root_flags(&dest->root_item,
				root_flags & ~BTRFS_ROOT_SUBVOL_DEAD);
		spin_unlock(&dest->root_item_lock);
	}
out_up_write:
	up_write(&fs_info->subvol_sem);
	if (!ret) {
		d_invalidate(dentry);
		btrfs_prune_dentries(dest);
		ASSERT(dest->send_in_progress == 0);
	}

	return ret;
}

static int btrfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	int ret = 0;
	struct btrfs_trans_handle *trans;
	u64 last_unlink_trans;
	struct fscrypt_name fname;

	if (inode->i_size > BTRFS_EMPTY_DIR_SIZE)
		return -ENOTEMPTY;
	if (btrfs_ino(BTRFS_I(inode)) == BTRFS_FIRST_FREE_OBJECTID) {
		if (unlikely(btrfs_fs_incompat(fs_info, EXTENT_TREE_V2))) {
			btrfs_err(fs_info,
			"extent tree v2 doesn't support snapshot deletion yet");
			return -EOPNOTSUPP;
		}
		return btrfs_delete_subvolume(BTRFS_I(dir), dentry);
	}

	ret = fscrypt_setup_filename(dir, &dentry->d_name, 1, &fname);
	if (ret)
		return ret;

	/* This needs to handle no-key deletions later on */

	trans = __unlink_start_trans(BTRFS_I(dir));
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_notrans;
	}

	if (unlikely(btrfs_ino(BTRFS_I(inode)) == BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
		ret = btrfs_unlink_subvol(trans, BTRFS_I(dir), dentry);
		goto out;
	}

	ret = btrfs_orphan_add(trans, BTRFS_I(inode));
	if (ret)
		goto out;

	last_unlink_trans = BTRFS_I(inode)->last_unlink_trans;

	/* now the directory is empty */
	ret = btrfs_unlink_inode(trans, BTRFS_I(dir), BTRFS_I(d_inode(dentry)),
				 &fname.disk_name);
	if (!ret) {
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
out_notrans:
	btrfs_btree_balance_dirty(fs_info);
	fscrypt_free_filename(&fname);

	return ret;
}

/*
 * Read, zero a chunk and write a block.
 *
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
	struct folio *folio;
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
					  blocksize, false);
	if (ret < 0) {
		if (btrfs_check_nocow_lock(inode, block_start, &write_bytes, false) > 0) {
			/* For nocow case, no need to reserve data space */
			only_release_metadata = true;
		} else {
			goto out;
		}
	}
	ret = btrfs_delalloc_reserve_metadata(inode, blocksize, blocksize, false);
	if (ret < 0) {
		if (!only_release_metadata)
			btrfs_free_reserved_data_space(inode, data_reserved,
						       block_start, blocksize);
		goto out;
	}
again:
	folio = __filemap_get_folio(mapping, index,
				    FGP_LOCK | FGP_ACCESSED | FGP_CREAT, mask);
	if (IS_ERR(folio)) {
		btrfs_delalloc_release_space(inode, data_reserved, block_start,
					     blocksize, true);
		btrfs_delalloc_release_extents(inode, blocksize);
		ret = -ENOMEM;
		goto out;
	}

	if (!folio_test_uptodate(folio)) {
		ret = btrfs_read_folio(NULL, folio);
		folio_lock(folio);
		if (folio->mapping != mapping) {
			folio_unlock(folio);
			folio_put(folio);
			goto again;
		}
		if (!folio_test_uptodate(folio)) {
			ret = -EIO;
			goto out_unlock;
		}
	}

	/*
	 * We unlock the page after the io is completed and then re-lock it
	 * above.  release_folio() could have come in between that and cleared
	 * folio private, but left the page in the mapping.  Set the page mapped
	 * here to make sure it's properly set for the subpage stuff.
	 */
	ret = set_folio_extent_mapped(folio);
	if (ret < 0)
		goto out_unlock;

	folio_wait_writeback(folio);

	lock_extent(io_tree, block_start, block_end, &cached_state);

	ordered = btrfs_lookup_ordered_extent(inode, block_start);
	if (ordered) {
		unlock_extent(io_tree, block_start, block_end, &cached_state);
		folio_unlock(folio);
		folio_put(folio);
		btrfs_start_ordered_extent(ordered);
		btrfs_put_ordered_extent(ordered);
		goto again;
	}

	clear_extent_bit(&inode->io_tree, block_start, block_end,
			 EXTENT_DELALLOC | EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG,
			 &cached_state);

	ret = btrfs_set_extent_delalloc(inode, block_start, block_end, 0,
					&cached_state);
	if (ret) {
		unlock_extent(io_tree, block_start, block_end, &cached_state);
		goto out_unlock;
	}

	if (offset != blocksize) {
		if (!len)
			len = blocksize - offset;
		if (front)
			folio_zero_range(folio, block_start - folio_pos(folio),
					 offset);
		else
			folio_zero_range(folio,
					 (block_start - folio_pos(folio)) + offset,
					 len);
	}
	btrfs_folio_clear_checked(fs_info, folio, block_start,
				  block_end + 1 - block_start);
	btrfs_folio_set_dirty(fs_info, folio, block_start,
			      block_end + 1 - block_start);
	unlock_extent(io_tree, block_start, block_end, &cached_state);

	if (only_release_metadata)
		set_extent_bit(&inode->io_tree, block_start, block_end,
			       EXTENT_NORESERVE, NULL);

out_unlock:
	if (ret) {
		if (only_release_metadata)
			btrfs_delalloc_release_metadata(inode, blocksize, true);
		else
			btrfs_delalloc_release_space(inode, data_reserved,
					block_start, blocksize, true);
	}
	btrfs_delalloc_release_extents(inode, blocksize);
	folio_unlock(folio);
	folio_put(folio);
out:
	if (only_release_metadata)
		btrfs_check_nocow_unlock(inode);
	extent_changeset_free(data_reserved);
	return ret;
}

static int maybe_insert_hole(struct btrfs_inode *inode, u64 offset, u64 len)
{
	struct btrfs_root *root = inode->root;
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

	ret = btrfs_insert_hole_extent(trans, root, btrfs_ino(inode), offset, len);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
	} else {
		btrfs_update_inode_bytes(inode, 0, drop_args.bytes_found);
		btrfs_update_inode(trans, inode);
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
	u64 hole_start = ALIGN(oldsize, fs_info->sectorsize);
	u64 block_end = ALIGN(size, fs_info->sectorsize);
	u64 last_byte;
	u64 cur_offset;
	u64 hole_size;
	int ret = 0;

	/*
	 * If our size started in the middle of a block we need to zero out the
	 * rest of the block before we expand the i_size, otherwise we could
	 * expose stale data.
	 */
	ret = btrfs_truncate_block(inode, oldsize, 0, 0);
	if (ret)
		return ret;

	if (size <= hole_start)
		return 0;

	btrfs_lock_and_flush_ordered_range(inode, hole_start, block_end - 1,
					   &cached_state);
	cur_offset = hole_start;
	while (1) {
		em = btrfs_get_extent(inode, NULL, cur_offset, block_end - cur_offset);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			em = NULL;
			break;
		}
		last_byte = min(extent_map_end(em), block_end);
		last_byte = ALIGN(last_byte, fs_info->sectorsize);
		hole_size = last_byte - cur_offset;

		if (!(em->flags & EXTENT_FLAG_PREALLOC)) {
			struct extent_map *hole_em;

			ret = maybe_insert_hole(inode, cur_offset, hole_size);
			if (ret)
				break;

			ret = btrfs_inode_set_file_extent_range(inode,
							cur_offset, hole_size);
			if (ret)
				break;

			hole_em = alloc_extent_map();
			if (!hole_em) {
				btrfs_drop_extent_map_range(inode, cur_offset,
						    cur_offset + hole_size - 1,
						    false);
				btrfs_set_inode_full_sync(inode);
				goto next;
			}
			hole_em->start = cur_offset;
			hole_em->len = hole_size;

			hole_em->disk_bytenr = EXTENT_MAP_HOLE;
			hole_em->disk_num_bytes = 0;
			hole_em->ram_bytes = hole_size;
			hole_em->generation = btrfs_get_fs_generation(fs_info);

			ret = btrfs_replace_extent_map_range(inode, hole_em, true);
			free_extent_map(hole_em);
		} else {
			ret = btrfs_inode_set_file_extent_range(inode,
							cur_offset, hole_size);
			if (ret)
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
	unlock_extent(io_tree, hole_start, block_end - 1, &cached_state);
	return ret;
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
		if (!(mask & (ATTR_CTIME | ATTR_MTIME))) {
			inode_set_mtime_to_ts(inode,
					      inode_set_ctime_current(inode));
		}
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
		ret = btrfs_update_inode(trans, BTRFS_I(inode));
		btrfs_drew_write_unlock(&root->snapshot_lock);
		btrfs_end_transaction(trans);
	} else {
		struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);

		if (btrfs_is_zoned(fs_info)) {
			ret = btrfs_wait_ordered_range(BTRFS_I(inode),
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

		ret = btrfs_truncate(BTRFS_I(inode), newsize == oldsize);
		if (ret && inode->i_nlink) {
			int err;

			/*
			 * Truncate failed, so fix up the in-memory size. We
			 * adjusted disk_i_size down as we removed extents, so
			 * wait for disk_i_size to be stable and then update the
			 * in-memory size to match.
			 */
			err = btrfs_wait_ordered_range(BTRFS_I(inode), 0, (u64)-1);
			if (err)
				return err;
			i_size_write(inode, BTRFS_I(inode)->disk_i_size);
		}
	}

	return ret;
}

static int btrfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			 struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int err;

	if (btrfs_root_readonly(root))
		return -EROFS;

	err = setattr_prepare(idmap, dentry, attr);
	if (err)
		return err;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		err = btrfs_setsize(inode, attr);
		if (err)
			return err;
	}

	if (attr->ia_valid) {
		setattr_copy(idmap, inode, attr);
		inode_inc_iversion(inode);
		err = btrfs_dirty_inode(BTRFS_I(inode));

		if (!err && attr->ia_valid & ATTR_MODE)
			err = posix_acl_chmod(idmap, dentry, inode->i_mode);
	}

	return err;
}

/*
 * While truncating the inode pages during eviction, we get the VFS
 * calling btrfs_invalidate_folio() against each folio of the inode. This
 * is slow because the calls to btrfs_invalidate_folio() result in a
 * huge amount of calls to lock_extent() and clear_extent_bit(),
 * which keep merging and splitting extent_state structures over and over,
 * wasting lots of time.
 *
 * Therefore if the inode is being evicted, let btrfs_invalidate_folio()
 * skip all those expensive operations on a per folio basis and do only
 * the ordered io finishing, while we release here the extent_map and
 * extent_state structures, without the excessive merging and splitting.
 */
static void evict_inode_truncate_pages(struct inode *inode)
{
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct rb_node *node;

	ASSERT(inode->i_state & I_FREEING);
	truncate_inode_pages_final(&inode->i_data);

	btrfs_drop_extent_map_range(BTRFS_I(inode), 0, (u64)-1, false);

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

		lock_extent(io_tree, start, end, &cached_state);

		/*
		 * If still has DELALLOC flag, the extent didn't reach disk,
		 * and its reserved space won't be freed by delayed_ref.
		 * So we need to free its reserved space here.
		 * (Refer to comment in btrfs_invalidate_folio, case 2)
		 *
		 * Note, end is the bytenr of last byte, so we need + 1 here.
		 */
		if (state_flags & EXTENT_DELALLOC)
			btrfs_qgroup_free_data(BTRFS_I(inode), NULL, start,
					       end - start + 1, NULL);

		clear_extent_bit(io_tree, start, end,
				 EXTENT_CLEAR_ALL_BITS | EXTENT_DO_ACCOUNTING,
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
	struct btrfs_trans_handle *trans;
	u64 delayed_refs_extra = btrfs_calc_delayed_ref_bytes(fs_info, 1);
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
	 * BTRFS_RESERVE_FLUSH_EVICT will steal from the global_rsv if it can,
	 * if we fail to make this reservation we can re-try without the
	 * delayed_refs_extra so we can make some forward progress.
	 */
	ret = btrfs_block_rsv_refill(fs_info, rsv, rsv->size + delayed_refs_extra,
				     BTRFS_RESERVE_FLUSH_EVICT);
	if (ret) {
		ret = btrfs_block_rsv_refill(fs_info, rsv, rsv->size,
					     BTRFS_RESERVE_FLUSH_EVICT);
		if (ret) {
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
					delayed_refs_extra, true);
	}
	return trans;
}

void btrfs_evict_inode(struct inode *inode)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *rsv = NULL;
	int ret;

	trace_btrfs_inode_evict(inode);

	if (!root) {
		fsverity_cleanup_inode(inode);
		clear_inode(inode);
		return;
	}

	fs_info = inode_to_fs_info(inode);
	evict_inode_truncate_pages(inode);

	if (inode->i_nlink &&
	    ((btrfs_root_refs(&root->root_item) != 0 &&
	      btrfs_root_id(root) != BTRFS_ROOT_TREE_OBJECTID) ||
	     btrfs_is_free_space_inode(BTRFS_I(inode))))
		goto out;

	if (is_bad_inode(inode))
		goto out;

	if (test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags))
		goto out;

	if (inode->i_nlink > 0) {
		BUG_ON(btrfs_root_refs(&root->root_item) != 0 &&
		       btrfs_root_id(root) != BTRFS_ROOT_TREE_OBJECTID);
		goto out;
	}

	/*
	 * This makes sure the inode item in tree is uptodate and the space for
	 * the inode update is released.
	 */
	ret = btrfs_commit_inode_delayed_inode(BTRFS_I(inode));
	if (ret)
		goto out;

	/*
	 * This drops any pending insert or delete operations we have for this
	 * inode.  We could have a delayed dir index deletion queued up, but
	 * we're removing the inode completely so that'll be taken care of in
	 * the truncate.
	 */
	btrfs_kill_delayed_inode_items(BTRFS_I(inode));

	rsv = btrfs_alloc_block_rsv(fs_info, BTRFS_BLOCK_RSV_TEMP);
	if (!rsv)
		goto out;
	rsv->size = btrfs_calc_metadata_size(fs_info, 1);
	rsv->failfast = true;

	btrfs_i_size_write(BTRFS_I(inode), 0);

	while (1) {
		struct btrfs_truncate_control control = {
			.inode = BTRFS_I(inode),
			.ino = btrfs_ino(BTRFS_I(inode)),
			.new_size = 0,
			.min_type = 0,
		};

		trans = evict_refill_and_join(root, rsv);
		if (IS_ERR(trans))
			goto out;

		trans->block_rsv = rsv;

		ret = btrfs_truncate_inode_items(trans, root, &control);
		trans->block_rsv = &fs_info->trans_block_rsv;
		btrfs_end_transaction(trans);
		/*
		 * We have not added new delayed items for our inode after we
		 * have flushed its delayed items, so no need to throttle on
		 * delayed items. However we have modified extent buffers.
		 */
		btrfs_btree_balance_dirty_nodelay(fs_info);
		if (ret && ret != -ENOSPC && ret != -EAGAIN)
			goto out;
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

out:
	btrfs_free_block_rsv(fs_info, rsv);
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
static int btrfs_inode_by_name(struct btrfs_inode *dir, struct dentry *dentry,
			       struct btrfs_key *location, u8 *type)
{
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct btrfs_root *root = dir->root;
	int ret = 0;
	struct fscrypt_name fname;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = fscrypt_setup_filename(&dir->vfs_inode, &dentry->d_name, 1, &fname);
	if (ret < 0)
		goto out;
	/*
	 * fscrypt_setup_filename() should never return a positive value, but
	 * gcc on sparc/parisc thinks it can, so assert that doesn't happen.
	 */
	ASSERT(ret == 0);

	/* This needs to handle no-key deletions later on */

	di = btrfs_lookup_dir_item(NULL, root, path, btrfs_ino(dir),
				   &fname.disk_name, 0);
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
			   __func__, fname.disk_name.name, btrfs_ino(dir),
			   location->objectid, location->type, location->offset);
	}
	if (!ret)
		*type = btrfs_dir_ftype(path->nodes[0], di);
out:
	fscrypt_free_filename(&fname);
	btrfs_free_path(path);
	return ret;
}

/*
 * when we hit a tree root in a directory, the btrfs part of the inode
 * needs to be changed to reflect the root directory of the tree root.  This
 * is kind of like crossing a mount point.
 */
static int fixup_tree_root_location(struct btrfs_fs_info *fs_info,
				    struct btrfs_inode *dir,
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
	struct fscrypt_name fname;

	ret = fscrypt_setup_filename(&dir->vfs_inode, &dentry->d_name, 0, &fname);
	if (ret)
		return ret;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out;
	}

	err = -ENOENT;
	key.objectid = btrfs_root_id(dir->root);
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
	    btrfs_root_ref_name_len(leaf, ref) != fname.disk_name.len)
		goto out;

	ret = memcmp_extent_buffer(leaf, fname.disk_name.name,
				   (unsigned long)(ref + 1), fname.disk_name.len);
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
	fscrypt_free_filename(&fname);
	return err;
}



static void btrfs_del_inode_from_root(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_inode *entry;
	bool empty = false;

	xa_lock(&root->inodes);
	entry = __xa_erase(&root->inodes, btrfs_ino(inode));
	if (entry == inode)
		empty = xa_empty(&root->inodes);
	xa_unlock(&root->inodes);

	if (empty && btrfs_root_refs(&root->root_item) == 0) {
		xa_lock(&root->inodes);
		empty = xa_empty(&root->inodes);
		xa_unlock(&root->inodes);
		if (empty)
			btrfs_add_dead_root(root);
	}
}


static int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;

	btrfs_set_inode_number(BTRFS_I(inode), args->ino);
	BTRFS_I(inode)->root = btrfs_grab_root(args->root);

	if (args->root && args->root == args->root->fs_info->tree_root &&
	    args->ino != BTRFS_BTREE_INODE_OBJECTID)
		set_bit(BTRFS_INODE_FREE_SPACE_INODE,
			&BTRFS_I(inode)->runtime_flags);
	return 0;
}

static int btrfs_find_actor(struct inode *inode, void *opaque)
{
	struct btrfs_iget_args *args = opaque;

	return args->ino == btrfs_ino(BTRFS_I(inode)) &&
		args->root == BTRFS_I(inode)->root;
}

static struct inode *btrfs_iget_locked(u64 ino, struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	unsigned long hashval = btrfs_inode_hash(ino, root);

	args.ino = ino;
	args.root = root;

	inode = iget5_locked_rcu(root->fs_info->sb, hashval, btrfs_find_actor,
			     btrfs_init_locked_inode,
			     (void *)&args);
	return inode;
}

/*
 * Get an inode object given its inode number and corresponding root.  Path is
 * preallocated to prevent recursing back to iget through allocator.
 */
struct inode *btrfs_iget_path(u64 ino, struct btrfs_root *root,
			      struct btrfs_path *path)
{
	struct inode *inode;
	int ret;

	inode = btrfs_iget_locked(ino, root);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	ret = btrfs_read_locked_inode(inode, path);
	if (ret)
		return ERR_PTR(ret);

	unlock_new_inode(inode);
	return inode;
}

/*
 * Get an inode object given its inode number and corresponding root.
 */
struct inode *btrfs_iget(u64 ino, struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_path *path;
	int ret;

	inode = btrfs_iget_locked(ino, root);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);

	ret = btrfs_read_locked_inode(inode, path);
	btrfs_free_path(path);
	if (ret)
		return ERR_PTR(ret);

	unlock_new_inode(inode);
	return inode;
}

static struct inode *new_simple_dir(struct inode *dir,
				    struct btrfs_key *key,
				    struct btrfs_root *root)
{
	struct timespec64 ts;
	struct inode *inode = new_inode(dir->i_sb);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	BTRFS_I(inode)->root = btrfs_grab_root(root);
	BTRFS_I(inode)->ref_root_id = key->objectid;
	set_bit(BTRFS_INODE_ROOT_STUB, &BTRFS_I(inode)->runtime_flags);
	set_bit(BTRFS_INODE_DUMMY, &BTRFS_I(inode)->runtime_flags);

	btrfs_set_inode_number(BTRFS_I(inode), BTRFS_EMPTY_SUBVOL_DIR_OBJECTID);
	/*
	 * We only need lookup, the rest is read-only and there's no inode
	 * associated with the dentry
	 */
	inode->i_op = &simple_dir_inode_operations;
	inode->i_opflags &= ~IOP_XATTR;
	inode->i_fop = &simple_dir_operations;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO;

	ts = inode_set_ctime_current(inode);
	inode_set_mtime_to_ts(inode, ts);
	inode_set_atime_to_ts(inode, inode_get_atime(dir));
	BTRFS_I(inode)->i_otime_sec = ts.tv_sec;
	BTRFS_I(inode)->i_otime_nsec = ts.tv_nsec;

	inode->i_uid = dir->i_uid;
	inode->i_gid = dir->i_gid;

	return inode;
}

static_assert(BTRFS_FT_UNKNOWN == FT_UNKNOWN);
static_assert(BTRFS_FT_REG_FILE == FT_REG_FILE);
static_assert(BTRFS_FT_DIR == FT_DIR);
static_assert(BTRFS_FT_CHRDEV == FT_CHRDEV);
static_assert(BTRFS_FT_BLKDEV == FT_BLKDEV);
static_assert(BTRFS_FT_FIFO == FT_FIFO);
static_assert(BTRFS_FT_SOCK == FT_SOCK);
static_assert(BTRFS_FT_SYMLINK == FT_SYMLINK);

static inline u8 btrfs_inode_type(struct inode *inode)
{
	return fs_umode_to_ftype(inode->i_mode);
}

struct inode *btrfs_lookup_dentry(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(dir);
	struct inode *inode;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location = { 0 };
	u8 di_type = 0;
	int ret = 0;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(BTRFS_I(dir), dentry, &location, &di_type);
	if (ret < 0)
		return ERR_PTR(ret);

	if (location.type == BTRFS_INODE_ITEM_KEY) {
		inode = btrfs_iget(location.objectid, root);
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

	ret = fixup_tree_root_location(fs_info, BTRFS_I(dir), dentry,
				       &location, &sub_root);
	if (ret < 0) {
		if (ret != -ENOENT)
			inode = ERR_PTR(ret);
		else
			inode = new_simple_dir(dir, &location, root);
	} else {
		inode = btrfs_iget(location.objectid, sub_root);
		btrfs_put_root(sub_root);

		if (IS_ERR(inode))
			return inode;

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
 * Find the highest existing sequence number in a directory and then set the
 * in-memory index_cnt variable to the first free sequence number.
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

	if (path->slots[0] == 0) {
		inode->index_cnt = BTRFS_DIR_START_INDEX;
		goto out;
	}

	path->slots[0]--;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

	if (found_key.objectid != btrfs_ino(inode) ||
	    found_key.type != BTRFS_DIR_INDEX_KEY) {
		inode->index_cnt = BTRFS_DIR_START_INDEX;
		goto out;
	}

	inode->index_cnt = found_key.offset + 1;
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_get_dir_last_index(struct btrfs_inode *dir, u64 *index)
{
	int ret = 0;

	btrfs_inode_lock(dir, 0);
	if (dir->index_cnt == (u64)-1) {
		ret = btrfs_inode_delayed_dir_index_count(dir);
		if (ret) {
			ret = btrfs_set_inode_index_count(dir);
			if (ret)
				goto out;
		}
	}

	/* index_cnt is the index number of next new entry, so decrement it. */
	*index = dir->index_cnt - 1;
out:
	btrfs_inode_unlock(dir, 0);

	return ret;
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
	u64 last_index;
	int ret;

	ret = btrfs_get_dir_last_index(BTRFS_I(inode), &last_index);
	if (ret)
		return ret;

	private = kzalloc(sizeof(struct btrfs_file_private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;
	private->last_index = last_index;
	private->filldir_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!private->filldir_buf) {
		kfree(private);
		return -ENOMEM;
	}
	file->private_data = private;
	return 0;
}

static loff_t btrfs_dir_llseek(struct file *file, loff_t offset, int whence)
{
	struct btrfs_file_private *private = file->private_data;
	int ret;

	ret = btrfs_get_dir_last_index(BTRFS_I(file_inode(file)),
				       &private->last_index);
	if (ret)
		return ret;

	return generic_file_llseek(file, offset, whence);
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
	LIST_HEAD(ins_list);
	LIST_HEAD(del_list);
	int ret;
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

	put = btrfs_readdir_get_delayed_items(BTRFS_I(inode), private->last_index,
					      &ins_list, &del_list);

again:
	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = ctx->pos;
	key.objectid = btrfs_ino(BTRFS_I(inode));

	btrfs_for_each_slot(root, &key, &found_key, path, ret) {
		struct dir_entry *entry;
		struct extent_buffer *leaf = path->nodes[0];
		u8 ftype;

		if (found_key.objectid != key.objectid)
			break;
		if (found_key.type != BTRFS_DIR_INDEX_KEY)
			break;
		if (found_key.offset < ctx->pos)
			continue;
		if (found_key.offset > private->last_index)
			break;
		if (btrfs_should_delete_dir_index(&del_list, found_key.offset))
			continue;
		di = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_dir_item);
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

		ftype = btrfs_dir_flags_to_ftype(btrfs_dir_flags(leaf, di));
		entry = addr;
		name_ptr = (char *)(entry + 1);
		read_extent_buffer(leaf, name_ptr,
				   (unsigned long)(di + 1), name_len);
		put_unaligned(name_len, &entry->name_len);
		put_unaligned(fs_ftype_to_dtype(ftype), &entry->type);
		btrfs_dir_item_key_to_cpu(leaf, di, &location);
		put_unaligned(location.objectid, &entry->ino);
		put_unaligned(found_key.offset, &entry->offset);
		entries++;
		addr += sizeof(struct dir_entry) + name_len;
		total_len += sizeof(struct dir_entry) + name_len;
	}
	/* Catch error encountered during iteration */
	if (ret < 0)
		goto err;

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
	 * they're returned by readdir.  Until we reuse freed offsets
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
		btrfs_readdir_put_delayed_items(BTRFS_I(inode), &ins_list, &del_list);
	btrfs_free_path(path);
	return ret;
}

/*
 * This is somewhat expensive, updating the tree every time the
 * inode changes.  But, it is most likely to find the inode in cache.
 * FIXME, needs more benchmarking...there are no reasons other than performance
 * to keep or drop this code.
 */
static int btrfs_dirty_inode(struct btrfs_inode *inode)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_trans_handle *trans;
	int ret;

	if (test_bit(BTRFS_INODE_DUMMY, &inode->runtime_flags))
		return 0;

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = btrfs_update_inode(trans, inode);
	if (ret == -ENOSPC || ret == -EDQUOT) {
		/* whoops, lets try again with the full transaction */
		btrfs_end_transaction(trans);
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans))
			return PTR_ERR(trans);

		ret = btrfs_update_inode(trans, inode);
	}
	btrfs_end_transaction(trans);
	if (inode->delayed_node)
		btrfs_balance_delayed_items(fs_info);

	return ret;
}

/*
 * This is a copy of file_update_time.  We need this so we can return error on
 * ENOSPC for updating the inode in the case of file write and mmap writes.
 */
static int btrfs_update_time(struct inode *inode, int flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	bool dirty;

	if (btrfs_root_readonly(root))
		return -EROFS;

	dirty = inode_update_timestamps(inode, flags);
	return dirty ? btrfs_dirty_inode(BTRFS_I(inode)) : 0;
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

	args.ino = btrfs_ino(BTRFS_I(inode));
	args.root = BTRFS_I(inode)->root;

	return insert_inode_locked4(inode,
		   btrfs_inode_hash(inode->i_ino, BTRFS_I(inode)->root),
		   btrfs_find_actor, &args);
}

int btrfs_new_inode_prepare(struct btrfs_new_inode_args *args,
			    unsigned int *trans_num_items)
{
	struct inode *dir = args->dir;
	struct inode *inode = args->inode;
	int ret;

	if (!args->orphan) {
		ret = fscrypt_setup_filename(dir, &args->dentry->d_name, 0,
					     &args->fname);
		if (ret)
			return ret;
	}

	ret = posix_acl_create(dir, &inode->i_mode, &args->default_acl, &args->acl);
	if (ret) {
		fscrypt_free_filename(&args->fname);
		return ret;
	}

	/* 1 to add inode item */
	*trans_num_items = 1;
	/* 1 to add compression property */
	if (BTRFS_I(dir)->prop_compress)
		(*trans_num_items)++;
	/* 1 to add default ACL xattr */
	if (args->default_acl)
		(*trans_num_items)++;
	/* 1 to add access ACL xattr */
	if (args->acl)
		(*trans_num_items)++;
#ifdef CONFIG_SECURITY
	/* 1 to add LSM xattr */
	if (dir->i_security)
		(*trans_num_items)++;
#endif
	if (args->orphan) {
		/* 1 to add orphan item */
		(*trans_num_items)++;
	} else {
		/*
		 * 1 to add dir item
		 * 1 to add dir index
		 * 1 to update parent inode item
		 *
		 * No need for 1 unit for the inode ref item because it is
		 * inserted in a batch together with the inode item at
		 * btrfs_create_new_inode().
		 */
		*trans_num_items += 3;
	}
	return 0;
}

void btrfs_new_inode_args_destroy(struct btrfs_new_inode_args *args)
{
	posix_acl_release(args->acl);
	posix_acl_release(args->default_acl);
	fscrypt_free_filename(&args->fname);
}

/*
 * Inherit flags from the parent inode.
 *
 * Currently only the compression flags and the cow flags are inherited.
 */
static void btrfs_inherit_iflags(struct btrfs_inode *inode, struct btrfs_inode *dir)
{
	unsigned int flags;

	flags = dir->flags;

	if (flags & BTRFS_INODE_NOCOMPRESS) {
		inode->flags &= ~BTRFS_INODE_COMPRESS;
		inode->flags |= BTRFS_INODE_NOCOMPRESS;
	} else if (flags & BTRFS_INODE_COMPRESS) {
		inode->flags &= ~BTRFS_INODE_NOCOMPRESS;
		inode->flags |= BTRFS_INODE_COMPRESS;
	}

	if (flags & BTRFS_INODE_NODATACOW) {
		inode->flags |= BTRFS_INODE_NODATACOW;
		if (S_ISREG(inode->vfs_inode.i_mode))
			inode->flags |= BTRFS_INODE_NODATASUM;
	}

	btrfs_sync_inode_flags_to_i_flags(&inode->vfs_inode);
}

int btrfs_create_new_inode(struct btrfs_trans_handle *trans,
			   struct btrfs_new_inode_args *args)
{
	struct timespec64 ts;
	struct inode *dir = args->dir;
	struct inode *inode = args->inode;
	const struct fscrypt_str *name = args->orphan ? NULL : &args->fname.disk_name;
	struct btrfs_fs_info *fs_info = inode_to_fs_info(dir);
	struct btrfs_root *root;
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	u64 objectid;
	struct btrfs_inode_ref *ref;
	struct btrfs_key key[2];
	u32 sizes[2];
	struct btrfs_item_batch batch;
	unsigned long ptr;
	int ret;
	bool xa_reserved = false;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (!args->subvol)
		BTRFS_I(inode)->root = btrfs_grab_root(BTRFS_I(dir)->root);
	root = BTRFS_I(inode)->root;

	ret = btrfs_init_file_extent_tree(BTRFS_I(inode));
	if (ret)
		goto out;

	ret = btrfs_get_free_objectid(root, &objectid);
	if (ret)
		goto out;
	btrfs_set_inode_number(BTRFS_I(inode), objectid);

	ret = xa_reserve(&root->inodes, objectid, GFP_NOFS);
	if (ret)
		goto out;
	xa_reserved = true;

	if (args->orphan) {
		/*
		 * O_TMPFILE, set link count to 0, so that after this point, we
		 * fill in an inode item with the correct link count.
		 */
		set_nlink(inode, 0);
	} else {
		trace_btrfs_inode_request(dir);

		ret = btrfs_set_inode_index(BTRFS_I(dir), &BTRFS_I(inode)->dir_index);
		if (ret)
			goto out;
	}

	if (S_ISDIR(inode->i_mode))
		BTRFS_I(inode)->index_cnt = BTRFS_DIR_START_INDEX;

	BTRFS_I(inode)->generation = trans->transid;
	inode->i_generation = BTRFS_I(inode)->generation;

	/*
	 * We don't have any capability xattrs set here yet, shortcut any
	 * queries for the xattrs here.  If we add them later via the inode
	 * security init path or any other path this flag will be cleared.
	 */
	set_bit(BTRFS_INODE_NO_CAP_XATTR, &BTRFS_I(inode)->runtime_flags);

	/*
	 * Subvolumes don't inherit flags from their parent directory.
	 * Originally this was probably by accident, but we probably can't
	 * change it now without compatibility issues.
	 */
	if (!args->subvol)
		btrfs_inherit_iflags(BTRFS_I(inode), BTRFS_I(dir));

	if (S_ISREG(inode->i_mode)) {
		if (btrfs_test_opt(fs_info, NODATASUM))
			BTRFS_I(inode)->flags |= BTRFS_INODE_NODATASUM;
		if (btrfs_test_opt(fs_info, NODATACOW))
			BTRFS_I(inode)->flags |= BTRFS_INODE_NODATACOW |
				BTRFS_INODE_NODATASUM;
	}

	ret = btrfs_insert_inode_locked(inode);
	if (ret < 0) {
		if (!args->orphan)
			BTRFS_I(dir)->index_cnt--;
		goto out;
	}

	/*
	 * We could have gotten an inode number from somebody who was fsynced
	 * and then removed in this same transaction, so let's just set full
	 * sync since it will be a full sync anyway and this will blow away the
	 * old info in the log.
	 */
	btrfs_set_inode_full_sync(BTRFS_I(inode));

	key[0].objectid = objectid;
	key[0].type = BTRFS_INODE_ITEM_KEY;
	key[0].offset = 0;

	sizes[0] = sizeof(struct btrfs_inode_item);

	if (!args->orphan) {
		/*
		 * Start new inodes with an inode_ref. This is slightly more
		 * efficient for small numbers of hard links since they will
		 * be packed into one item. Extended refs will kick in if we
		 * add more hard links than can fit in the ref item.
		 */
		key[1].objectid = objectid;
		key[1].type = BTRFS_INODE_REF_KEY;
		if (args->subvol) {
			key[1].offset = objectid;
			sizes[1] = 2 + sizeof(*ref);
		} else {
			key[1].offset = btrfs_ino(BTRFS_I(dir));
			sizes[1] = name->len + sizeof(*ref);
		}
	}

	batch.keys = &key[0];
	batch.data_sizes = &sizes[0];
	batch.total_data_size = sizes[0] + (args->orphan ? 0 : sizes[1]);
	batch.nr = args->orphan ? 1 : 2;
	ret = btrfs_insert_empty_items(trans, root, path, &batch);
	if (ret != 0) {
		btrfs_abort_transaction(trans, ret);
		goto discard;
	}

	ts = simple_inode_init_ts(inode);
	BTRFS_I(inode)->i_otime_sec = ts.tv_sec;
	BTRFS_I(inode)->i_otime_nsec = ts.tv_nsec;

	/*
	 * We're going to fill the inode item now, so at this point the inode
	 * must be fully initialized.
	 */

	inode_item = btrfs_item_ptr(path->nodes[0], path->slots[0],
				  struct btrfs_inode_item);
	memzero_extent_buffer(path->nodes[0], (unsigned long)inode_item,
			     sizeof(*inode_item));
	fill_inode_item(trans, path->nodes[0], inode_item, inode);

	if (!args->orphan) {
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0] + 1,
				     struct btrfs_inode_ref);
		ptr = (unsigned long)(ref + 1);
		if (args->subvol) {
			btrfs_set_inode_ref_name_len(path->nodes[0], ref, 2);
			btrfs_set_inode_ref_index(path->nodes[0], ref, 0);
			write_extent_buffer(path->nodes[0], "..", ptr, 2);
		} else {
			btrfs_set_inode_ref_name_len(path->nodes[0], ref,
						     name->len);
			btrfs_set_inode_ref_index(path->nodes[0], ref,
						  BTRFS_I(inode)->dir_index);
			write_extent_buffer(path->nodes[0], name->name, ptr,
					    name->len);
		}
	}

	btrfs_mark_buffer_dirty(trans, path->nodes[0]);
	/*
	 * We don't need the path anymore, plus inheriting properties, adding
	 * ACLs, security xattrs, orphan item or adding the link, will result in
	 * allocating yet another path. So just free our path.
	 */
	btrfs_free_path(path);
	path = NULL;

	if (args->subvol) {
		struct inode *parent;

		/*
		 * Subvolumes inherit properties from their parent subvolume,
		 * not the directory they were created in.
		 */
		parent = btrfs_iget(BTRFS_FIRST_FREE_OBJECTID, BTRFS_I(dir)->root);
		if (IS_ERR(parent)) {
			ret = PTR_ERR(parent);
		} else {
			ret = btrfs_inode_inherit_props(trans, inode, parent);
			iput(parent);
		}
	} else {
		ret = btrfs_inode_inherit_props(trans, inode, dir);
	}
	if (ret) {
		btrfs_err(fs_info,
			  "error inheriting props for ino %llu (root %llu): %d",
			  btrfs_ino(BTRFS_I(inode)), btrfs_root_id(root), ret);
	}

	/*
	 * Subvolumes don't inherit ACLs or get passed to the LSM. This is
	 * probably a bug.
	 */
	if (!args->subvol) {
		ret = btrfs_init_inode_security(trans, args);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto discard;
		}
	}

	ret = btrfs_add_inode_to_root(BTRFS_I(inode), false);
	if (WARN_ON(ret)) {
		/* Shouldn't happen, we used xa_reserve() before. */
		btrfs_abort_transaction(trans, ret);
		goto discard;
	}

	trace_btrfs_inode_new(inode);
	btrfs_set_inode_last_trans(trans, BTRFS_I(inode));

	btrfs_update_root_times(trans, root);

	if (args->orphan) {
		ret = btrfs_orphan_add(trans, BTRFS_I(inode));
	} else {
		ret = btrfs_add_link(trans, BTRFS_I(dir), BTRFS_I(inode), name,
				     0, BTRFS_I(inode)->dir_index);
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto discard;
	}

	return 0;

discard:
	/*
	 * discard_new_inode() calls iput(), but the caller owns the reference
	 * to the inode.
	 */
	ihold(inode);
	discard_new_inode(inode);
out:
	if (xa_reserved)
		xa_release(&root->inodes, objectid);

	btrfs_free_path(path);
	return ret;
}

/*
 * utility function to add 'inode' into 'parent_inode' with
 * a give name and a given sequence number.
 * if 'add_backref' is true, also insert a backref from the
 * inode to the parent directory.
 */
int btrfs_add_link(struct btrfs_trans_handle *trans,
		   struct btrfs_inode *parent_inode, struct btrfs_inode *inode,
		   const struct fscrypt_str *name, int add_backref, u64 index)
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
					 btrfs_root_id(root), parent_ino,
					 index, name);
	} else if (add_backref) {
		ret = btrfs_insert_inode_ref(trans, root, name,
					     ino, parent_ino, index);
	}

	/* Nothing to clean up yet */
	if (ret)
		return ret;

	ret = btrfs_insert_dir_item(trans, name, parent_inode, &key,
				    btrfs_inode_type(&inode->vfs_inode), index);
	if (ret == -EEXIST || ret == -EOVERFLOW)
		goto fail_dir_item;
	else if (ret) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	btrfs_i_size_write(parent_inode, parent_inode->vfs_inode.i_size +
			   name->len * 2);
	inode_inc_iversion(&parent_inode->vfs_inode);
	/*
	 * If we are replaying a log tree, we do not want to update the mtime
	 * and ctime of the parent directory with the current time, since the
	 * log replay procedure is responsible for setting them to their correct
	 * values (the ones it had when the fsync was done).
	 */
	if (!test_bit(BTRFS_FS_LOG_RECOVERING, &root->fs_info->flags))
		inode_set_mtime_to_ts(&parent_inode->vfs_inode,
				      inode_set_ctime_current(&parent_inode->vfs_inode));

	ret = btrfs_update_inode(trans, parent_inode);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;

fail_dir_item:
	if (unlikely(ino == BTRFS_FIRST_FREE_OBJECTID)) {
		u64 local_index;
		int err;
		err = btrfs_del_root_ref(trans, key.objectid,
					 btrfs_root_id(root), parent_ino,
					 &local_index, name);
		if (err)
			btrfs_abort_transaction(trans, err);
	} else if (add_backref) {
		u64 local_index;
		int err;

		err = btrfs_del_inode_ref(trans, root, name, ino, parent_ino,
					  &local_index);
		if (err)
			btrfs_abort_transaction(trans, err);
	}

	/* Return the original error code */
	return ret;
}

static int btrfs_create_common(struct inode *dir, struct dentry *dentry,
			       struct inode *inode)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(dir);
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_new_inode_args new_inode_args = {
		.dir = dir,
		.dentry = dentry,
		.inode = inode,
	};
	unsigned int trans_num_items;
	struct btrfs_trans_handle *trans;
	int err;

	err = btrfs_new_inode_prepare(&new_inode_args, &trans_num_items);
	if (err)
		goto out_inode;

	trans = btrfs_start_transaction(root, trans_num_items);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_new_inode_args;
	}

	err = btrfs_create_new_inode(trans, &new_inode_args);
	if (!err)
		d_instantiate_new(dentry, inode);

	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out_new_inode_args:
	btrfs_new_inode_args_destroy(&new_inode_args);
out_inode:
	if (err)
		iput(inode);
	return err;
}

static int btrfs_mknod(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode *inode;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return -ENOMEM;
	inode_init_owner(idmap, inode, dir, mode);
	inode->i_op = &btrfs_special_inode_operations;
	init_special_inode(inode, inode->i_mode, rdev);
	return btrfs_create_common(dir, dentry, inode);
}

static int btrfs_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return -ENOMEM;
	inode_init_owner(idmap, inode, dir, mode);
	inode->i_fop = &btrfs_file_operations;
	inode->i_op = &btrfs_file_inode_operations;
	inode->i_mapping->a_ops = &btrfs_aops;
	return btrfs_create_common(dir, dentry, inode);
}

static int btrfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = d_inode(old_dentry);
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	struct fscrypt_name fname;
	u64 index;
	int err;
	int drop_inode = 0;

	/* do not allow sys_link's with other subvols of the same device */
	if (btrfs_root_id(root) != btrfs_root_id(BTRFS_I(inode)->root))
		return -EXDEV;

	if (inode->i_nlink >= BTRFS_LINK_MAX)
		return -EMLINK;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &fname);
	if (err)
		goto fail;

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
	inode_set_ctime_current(inode);
	ihold(inode);
	set_bit(BTRFS_INODE_COPY_EVERYTHING, &BTRFS_I(inode)->runtime_flags);

	err = btrfs_add_link(trans, BTRFS_I(dir), BTRFS_I(inode),
			     &fname.disk_name, 1, index);

	if (err) {
		drop_inode = 1;
	} else {
		struct dentry *parent = dentry->d_parent;

		err = btrfs_update_inode(trans, BTRFS_I(inode));
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
		btrfs_log_new_name(trans, old_dentry, NULL, 0, parent);
	}

fail:
	fscrypt_free_filename(&fname);
	if (trans)
		btrfs_end_transaction(trans);
	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(fs_info);
	return err;
}

static int btrfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return -ENOMEM;
	inode_init_owner(idmap, inode, dir, S_IFDIR | mode);
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;
	return btrfs_create_common(dir, dentry, inode);
}

static noinline int uncompress_inline(struct btrfs_path *path,
				      struct folio *folio,
				      struct btrfs_file_extent_item *item)
{
	int ret;
	struct extent_buffer *leaf = path->nodes[0];
	char *tmp;
	size_t max_size;
	unsigned long inline_size;
	unsigned long ptr;
	int compress_type;

	compress_type = btrfs_file_extent_compression(leaf, item);
	max_size = btrfs_file_extent_ram_bytes(leaf, item);
	inline_size = btrfs_file_extent_inline_item_len(leaf, path->slots[0]);
	tmp = kmalloc(inline_size, GFP_NOFS);
	if (!tmp)
		return -ENOMEM;
	ptr = btrfs_file_extent_inline_start(item);

	read_extent_buffer(leaf, tmp, ptr, inline_size);

	max_size = min_t(unsigned long, PAGE_SIZE, max_size);
	ret = btrfs_decompress(compress_type, tmp, folio, 0, inline_size,
			       max_size);

	/*
	 * decompression code contains a memset to fill in any space between the end
	 * of the uncompressed data and the end of max_size in case the decompressed
	 * data ends up shorter than ram_bytes.  That doesn't cover the hole between
	 * the end of an inline extent and the beginning of the next block, so we
	 * cover that region here.
	 */

	if (max_size < PAGE_SIZE)
		folio_zero_range(folio, max_size, PAGE_SIZE - max_size);
	kfree(tmp);
	return ret;
}

static int read_inline_extent(struct btrfs_path *path, struct folio *folio)
{
	struct btrfs_file_extent_item *fi;
	void *kaddr;
	size_t copy_size;

	if (!folio || folio_test_uptodate(folio))
		return 0;

	ASSERT(folio_pos(folio) == 0);

	fi = btrfs_item_ptr(path->nodes[0], path->slots[0],
			    struct btrfs_file_extent_item);
	if (btrfs_file_extent_compression(path->nodes[0], fi) != BTRFS_COMPRESS_NONE)
		return uncompress_inline(path, folio, fi);

	copy_size = min_t(u64, PAGE_SIZE,
			  btrfs_file_extent_ram_bytes(path->nodes[0], fi));
	kaddr = kmap_local_folio(folio, 0);
	read_extent_buffer(path->nodes[0], kaddr,
			   btrfs_file_extent_inline_start(fi), copy_size);
	kunmap_local(kaddr);
	if (copy_size < PAGE_SIZE)
		folio_zero_range(folio, copy_size, PAGE_SIZE - copy_size);
	return 0;
}

/*
 * Lookup the first extent overlapping a range in a file.
 *
 * @inode:	file to search in
 * @page:	page to read extent data into if the extent is inline
 * @start:	file offset
 * @len:	length of range starting at @start
 *
 * Return the first &struct extent_map which overlaps the given range, reading
 * it from the B-tree and caching it if necessary. Note that there may be more
 * extents which overlap the given range after the returned extent_map.
 *
 * If @page is not NULL and the extent is inline, this also reads the extent
 * data directly into the page and marks the extent up to date in the io_tree.
 *
 * Return: ERR_PTR on error, non-NULL extent_map on success.
 */
struct extent_map *btrfs_get_extent(struct btrfs_inode *inode,
				    struct folio *folio, u64 start, u64 len)
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

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	read_unlock(&em_tree->lock);

	if (em) {
		if (em->start > start || em->start + em->len <= start)
			free_extent_map(em);
		else if (em->disk_bytenr == EXTENT_MAP_INLINE && folio)
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
	em->disk_bytenr = EXTENT_MAP_HOLE;
	em->len = (u64)-1;

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
		em->len = found_key.offset - start;
		em->disk_bytenr = EXTENT_MAP_HOLE;
		goto insert;
	}

	btrfs_extent_item_to_extent_map(inode, path, item, em);

	if (extent_type == BTRFS_FILE_EXTENT_REG ||
	    extent_type == BTRFS_FILE_EXTENT_PREALLOC) {
		goto insert;
	} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		/*
		 * Inline extent can only exist at file offset 0. This is
		 * ensured by tree-checker and inline extent creation path.
		 * Thus all members representing file offsets should be zero.
		 */
		ASSERT(extent_start == 0);
		ASSERT(em->start == 0);

		/*
		 * btrfs_extent_item_to_extent_map() should have properly
		 * initialized em members already.
		 *
		 * Other members are not utilized for inline extents.
		 */
		ASSERT(em->disk_bytenr == EXTENT_MAP_INLINE);
		ASSERT(em->len == fs_info->sectorsize);

		ret = read_inline_extent(path, folio);
		if (ret < 0)
			goto out;
		goto insert;
	}
not_found:
	em->start = start;
	em->len = len;
	em->disk_bytenr = EXTENT_MAP_HOLE;
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
	ret = btrfs_add_extent_mapping(inode, &em, start, len);
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
			      struct btrfs_file_extent *file_extent,
			      bool nowait, bool strict)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
	struct can_nocow_file_extent_args nocow_args = { 0 };
	struct btrfs_path *path;
	int ret;
	struct extent_buffer *leaf;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	int found_type;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->nowait = nowait;

	ret = btrfs_lookup_file_extent(NULL, root, path,
			btrfs_ino(BTRFS_I(inode)), offset, 0);
	if (ret < 0)
		goto out;

	if (ret == 1) {
		if (path->slots[0] == 0) {
			/* can't find the item, must cow */
			ret = 0;
			goto out;
		}
		path->slots[0]--;
	}
	ret = 0;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.objectid != btrfs_ino(BTRFS_I(inode)) ||
	    key.type != BTRFS_EXTENT_DATA_KEY) {
		/* not our file or wrong item type, must cow */
		goto out;
	}

	if (key.offset > offset) {
		/* Wrong offset, must cow */
		goto out;
	}

	if (btrfs_file_extent_end(path) <= offset)
		goto out;

	fi = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);
	found_type = btrfs_file_extent_type(leaf, fi);

	nocow_args.start = offset;
	nocow_args.end = offset + *len - 1;
	nocow_args.strict = strict;
	nocow_args.free_path = true;

	ret = can_nocow_file_extent(path, &key, BTRFS_I(inode), &nocow_args);
	/* can_nocow_file_extent() has freed the path. */
	path = NULL;

	if (ret != 1) {
		/* Treat errors as not being able to NOCOW. */
		ret = 0;
		goto out;
	}

	ret = 0;
	if (btrfs_extent_readonly(fs_info,
				  nocow_args.file_extent.disk_bytenr +
				  nocow_args.file_extent.offset))
		goto out;

	if (!(BTRFS_I(inode)->flags & BTRFS_INODE_NODATACOW) &&
	    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
		u64 range_end;

		range_end = round_up(offset + nocow_args.file_extent.num_bytes,
				     root->fs_info->sectorsize) - 1;
		ret = test_range_bit_exists(io_tree, offset, range_end, EXTENT_DELALLOC);
		if (ret) {
			ret = -EAGAIN;
			goto out;
		}
	}

	if (file_extent)
		memcpy(file_extent, &nocow_args.file_extent, sizeof(*file_extent));

	*len = nocow_args.file_extent.num_bytes;
	ret = 1;
out:
	btrfs_free_path(path);
	return ret;
}

/* The callers of this must take lock_extent() */
struct extent_map *btrfs_create_io_em(struct btrfs_inode *inode, u64 start,
				      const struct btrfs_file_extent *file_extent,
				      int type)
{
	struct extent_map *em;
	int ret;

	/*
	 * Note the missing NOCOW type.
	 *
	 * For pure NOCOW writes, we should not create an io extent map, but
	 * just reusing the existing one.
	 * Only PREALLOC writes (NOCOW write into preallocated range) can
	 * create an io extent map.
	 */
	ASSERT(type == BTRFS_ORDERED_PREALLOC ||
	       type == BTRFS_ORDERED_COMPRESSED ||
	       type == BTRFS_ORDERED_REGULAR);

	switch (type) {
	case BTRFS_ORDERED_PREALLOC:
		/* We're only referring part of a larger preallocated extent. */
		ASSERT(file_extent->num_bytes <= file_extent->ram_bytes);
		break;
	case BTRFS_ORDERED_REGULAR:
		/* COW results a new extent matching our file extent size. */
		ASSERT(file_extent->disk_num_bytes == file_extent->num_bytes);
		ASSERT(file_extent->ram_bytes == file_extent->num_bytes);

		/* Since it's a new extent, we should not have any offset. */
		ASSERT(file_extent->offset == 0);
		break;
	case BTRFS_ORDERED_COMPRESSED:
		/* Must be compressed. */
		ASSERT(file_extent->compression != BTRFS_COMPRESS_NONE);

		/*
		 * Encoded write can make us to refer to part of the
		 * uncompressed extent.
		 */
		ASSERT(file_extent->num_bytes <= file_extent->ram_bytes);
		break;
	}

	em = alloc_extent_map();
	if (!em)
		return ERR_PTR(-ENOMEM);

	em->start = start;
	em->len = file_extent->num_bytes;
	em->disk_bytenr = file_extent->disk_bytenr;
	em->disk_num_bytes = file_extent->disk_num_bytes;
	em->ram_bytes = file_extent->ram_bytes;
	em->generation = -1;
	em->offset = file_extent->offset;
	em->flags |= EXTENT_FLAG_PINNED;
	if (type == BTRFS_ORDERED_COMPRESSED)
		extent_map_set_compression(em, file_extent->compression);

	ret = btrfs_replace_extent_map_range(inode, em, true);
	if (ret) {
		free_extent_map(em);
		return ERR_PTR(ret);
	}

	/* em got 2 refs now, callers needs to do free_extent_map once. */
	return em;
}

/*
 * For release_folio() and invalidate_folio() we have a race window where
 * folio_end_writeback() is called but the subpage spinlock is not yet released.
 * If we continue to release/invalidate the page, we could cause use-after-free
 * for subpage spinlock.  So this function is to spin and wait for subpage
 * spinlock.
 */
static void wait_subpage_spinlock(struct folio *folio)
{
	struct btrfs_fs_info *fs_info = folio_to_fs_info(folio);
	struct btrfs_subpage *subpage;

	if (!btrfs_is_subpage(fs_info, folio->mapping))
		return;

	ASSERT(folio_test_private(folio) && folio_get_private(folio));
	subpage = folio_get_private(folio);

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

static int btrfs_launder_folio(struct folio *folio)
{
	return btrfs_qgroup_free_data(folio_to_inode(folio), NULL, folio_pos(folio),
				      PAGE_SIZE, NULL);
}

static bool __btrfs_release_folio(struct folio *folio, gfp_t gfp_flags)
{
	if (try_release_extent_mapping(folio, gfp_flags)) {
		wait_subpage_spinlock(folio);
		clear_folio_extent_mapped(folio);
		return true;
	}
	return false;
}

static bool btrfs_release_folio(struct folio *folio, gfp_t gfp_flags)
{
	if (folio_test_writeback(folio) || folio_test_dirty(folio))
		return false;
	return __btrfs_release_folio(folio, gfp_flags);
}

#ifdef CONFIG_MIGRATION
static int btrfs_migrate_folio(struct address_space *mapping,
			     struct folio *dst, struct folio *src,
			     enum migrate_mode mode)
{
	int ret = filemap_migrate_folio(mapping, dst, src, mode);

	if (ret != MIGRATEPAGE_SUCCESS)
		return ret;

	if (folio_test_ordered(src)) {
		folio_clear_ordered(src);
		folio_set_ordered(dst);
	}

	return MIGRATEPAGE_SUCCESS;
}
#else
#define btrfs_migrate_folio NULL
#endif

static void btrfs_invalidate_folio(struct folio *folio, size_t offset,
				 size_t length)
{
	struct btrfs_inode *inode = folio_to_inode(folio);
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_io_tree *tree = &inode->io_tree;
	struct extent_state *cached_state = NULL;
	u64 page_start = folio_pos(folio);
	u64 page_end = page_start + folio_size(folio) - 1;
	u64 cur;
	int inode_evicting = inode->vfs_inode.i_state & I_FREEING;

	/*
	 * We have folio locked so no new ordered extent can be created on this
	 * page, nor bio can be submitted for this folio.
	 *
	 * But already submitted bio can still be finished on this folio.
	 * Furthermore, endio function won't skip folio which has Ordered
	 * already cleared, so it's possible for endio and
	 * invalidate_folio to do the same ordered extent accounting twice
	 * on one folio.
	 *
	 * So here we wait for any submitted bios to finish, so that we won't
	 * do double ordered extent accounting on the same folio.
	 */
	folio_wait_writeback(folio);
	wait_subpage_spinlock(folio);

	/*
	 * For subpage case, we have call sites like
	 * btrfs_punch_hole_lock_range() which passes range not aligned to
	 * sectorsize.
	 * If the range doesn't cover the full folio, we don't need to and
	 * shouldn't clear page extent mapped, as folio->private can still
	 * record subpage dirty bits for other part of the range.
	 *
	 * For cases that invalidate the full folio even the range doesn't
	 * cover the full folio, like invalidating the last folio, we're
	 * still safe to wait for ordered extent to finish.
	 */
	if (!(offset == 0 && length == folio_size(folio))) {
		btrfs_release_folio(folio, GFP_NOFS);
		return;
	}

	if (!inode_evicting)
		lock_extent(tree, page_start, page_end, &cached_state);

	cur = page_start;
	while (cur < page_end) {
		struct btrfs_ordered_extent *ordered;
		u64 range_end;
		u32 range_len;
		u32 extra_flags = 0;

		ordered = btrfs_lookup_first_ordered_range(inode, cur,
							   page_end + 1 - cur);
		if (!ordered) {
			range_end = page_end;
			/*
			 * No ordered extent covering this range, we are safe
			 * to delete all extent states in the range.
			 */
			extra_flags = EXTENT_CLEAR_ALL_BITS;
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
			extra_flags = EXTENT_CLEAR_ALL_BITS;
			goto next;
		}

		range_end = min(ordered->file_offset + ordered->num_bytes - 1,
				page_end);
		ASSERT(range_end + 1 - cur < U32_MAX);
		range_len = range_end + 1 - cur;
		if (!btrfs_folio_test_ordered(fs_info, folio, cur, range_len)) {
			/*
			 * If Ordered is cleared, it means endio has
			 * already been executed for the range.
			 * We can't delete the extent states as
			 * btrfs_finish_ordered_io() may still use some of them.
			 */
			goto next;
		}
		btrfs_folio_clear_ordered(fs_info, folio, cur, range_len);

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
					 EXTENT_DEFRAG, &cached_state);

		spin_lock_irq(&inode->ordered_tree_lock);
		set_bit(BTRFS_ORDERED_TRUNCATED, &ordered->flags);
		ordered->truncated_len = min(ordered->truncated_len,
					     cur - ordered->file_offset);
		spin_unlock_irq(&inode->ordered_tree_lock);

		/*
		 * If the ordered extent has finished, we're safe to delete all
		 * the extent states of the range, otherwise
		 * btrfs_finish_ordered_io() will get executed by endio for
		 * other pages, so we can't delete extent states.
		 */
		if (btrfs_dec_test_ordered_pending(inode, &ordered,
						   cur, range_end + 1 - cur)) {
			btrfs_finish_ordered_io(ordered);
			/*
			 * The ordered extent has finished, now we're again
			 * safe to delete all extent states of the range.
			 */
			extra_flags = EXTENT_CLEAR_ALL_BITS;
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
		btrfs_qgroup_free_data(inode, NULL, cur, range_end + 1 - cur, NULL);
		if (!inode_evicting) {
			clear_extent_bit(tree, cur, range_end, EXTENT_LOCKED |
				 EXTENT_DELALLOC | EXTENT_UPTODATE |
				 EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG |
				 extra_flags, &cached_state);
		}
		cur = range_end + 1;
	}
	/*
	 * We have iterated through all ordered extents of the page, the page
	 * should not have Ordered anymore, or the above iteration
	 * did something wrong.
	 */
	ASSERT(!folio_test_ordered(folio));
	btrfs_folio_clear_checked(fs_info, folio, folio_pos(folio), folio_size(folio));
	if (!inode_evicting)
		__btrfs_release_folio(folio, GFP_NOFS);
	clear_folio_extent_mapped(folio);
}

static int btrfs_truncate(struct btrfs_inode *inode, bool skip_writeback)
{
	struct btrfs_truncate_control control = {
		.inode = inode,
		.ino = btrfs_ino(inode),
		.min_type = BTRFS_EXTENT_DATA_KEY,
		.clear_extent_range = true,
	};
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *rsv;
	int ret;
	struct btrfs_trans_handle *trans;
	u64 mask = fs_info->sectorsize - 1;
	const u64 min_size = btrfs_calc_metadata_size(fs_info, 1);

	if (!skip_writeback) {
		ret = btrfs_wait_ordered_range(inode,
					       inode->vfs_inode.i_size & (~mask),
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
	rsv->failfast = true;

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
	/*
	 * We have reserved 2 metadata units when we started the transaction and
	 * min_size matches 1 unit, so this should never fail, but if it does,
	 * it's not critical we just fail truncation.
	 */
	if (WARN_ON(ret)) {
		btrfs_end_transaction(trans);
		goto out;
	}

	trans->block_rsv = rsv;

	while (1) {
		struct extent_state *cached_state = NULL;
		const u64 new_size = inode->vfs_inode.i_size;
		const u64 lock_start = ALIGN_DOWN(new_size, fs_info->sectorsize);

		control.new_size = new_size;
		lock_extent(&inode->io_tree, lock_start, (u64)-1, &cached_state);
		/*
		 * We want to drop from the next block forward in case this new
		 * size is not block aligned since we will be keeping the last
		 * block of the extent just the way it is.
		 */
		btrfs_drop_extent_map_range(inode,
					    ALIGN(new_size, fs_info->sectorsize),
					    (u64)-1, false);

		ret = btrfs_truncate_inode_items(trans, root, &control);

		inode_sub_bytes(&inode->vfs_inode, control.sub_bytes);
		btrfs_inode_safe_disk_i_size_write(inode, control.last_size);

		unlock_extent(&inode->io_tree, lock_start, (u64)-1, &cached_state);

		trans->block_rsv = &fs_info->trans_block_rsv;
		if (ret != -ENOSPC && ret != -EAGAIN)
			break;

		ret = btrfs_update_inode(trans, inode);
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
		/*
		 * We have reserved 2 metadata units when we started the
		 * transaction and min_size matches 1 unit, so this should never
		 * fail, but if it does, it's not critical we just fail truncation.
		 */
		if (WARN_ON(ret))
			break;

		trans->block_rsv = rsv;
	}

	/*
	 * We can't call btrfs_truncate_block inside a trans handle as we could
	 * deadlock with freeze, if we got BTRFS_NEED_TRUNCATE_BLOCK then we
	 * know we've truncated everything except the last little bit, and can
	 * do btrfs_truncate_block and then update the disk_i_size.
	 */
	if (ret == BTRFS_NEED_TRUNCATE_BLOCK) {
		btrfs_end_transaction(trans);
		btrfs_btree_balance_dirty(fs_info);

		ret = btrfs_truncate_block(inode, inode->vfs_inode.i_size, 0, 0);
		if (ret)
			goto out;
		trans = btrfs_start_transaction(root, 1);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		btrfs_inode_safe_disk_i_size_write(inode, 0);
	}

	if (trans) {
		int ret2;

		trans->block_rsv = &fs_info->trans_block_rsv;
		ret2 = btrfs_update_inode(trans, inode);
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
	if (control.extents_found > 0)
		btrfs_set_inode_full_sync(inode);

	return ret;
}

struct inode *btrfs_new_subvol_inode(struct mnt_idmap *idmap,
				     struct inode *dir)
{
	struct inode *inode;

	inode = new_inode(dir->i_sb);
	if (inode) {
		/*
		 * Subvolumes don't inherit the sgid bit or the parent's gid if
		 * the parent's sgid bit is set. This is probably a bug.
		 */
		inode_init_owner(idmap, inode, NULL,
				 S_IFDIR | (~current_umask() & S_IRWXUGO));
		inode->i_op = &btrfs_dir_inode_operations;
		inode->i_fop = &btrfs_dir_file_operations;
	}
	return inode;
}

struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_inode *ei;
	struct inode *inode;

	ei = alloc_inode_sb(sb, btrfs_inode_cachep, GFP_KERNEL);
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
	/*
	 * ->index_cnt will be properly initialized later when creating a new
	 * inode (btrfs_create_new_inode()) or when reading an existing inode
	 * from disk (btrfs_read_locked_inode()).
	 */
	ei->csum_bytes = 0;
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

	ei->i_otime_sec = 0;
	ei->i_otime_nsec = 0;

	inode = &ei->vfs_inode;
	extent_map_tree_init(&ei->extent_tree);

	/* This io tree sets the valid inode. */
	extent_io_tree_init(fs_info, &ei->io_tree, IO_TREE_INODE_IO);
	ei->io_tree.inode = ei;

	ei->file_extent_tree = NULL;

	mutex_init(&ei->log_mutex);
	spin_lock_init(&ei->ordered_tree_lock);
	ei->ordered_tree = RB_ROOT;
	ei->ordered_tree_last = NULL;
	INIT_LIST_HEAD(&ei->delalloc_inodes);
	INIT_LIST_HEAD(&ei->delayed_iput);
	init_rwsem(&ei->i_mmap_lock);

	return inode;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
void btrfs_test_destroy_inode(struct inode *inode)
{
	btrfs_drop_extent_map_range(BTRFS_I(inode), 0, (u64)-1, false);
	kfree(BTRFS_I(inode)->file_extent_tree);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}
#endif

void btrfs_free_inode(struct inode *inode)
{
	kfree(BTRFS_I(inode)->file_extent_tree);
	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

void btrfs_destroy_inode(struct inode *vfs_inode)
{
	struct btrfs_ordered_extent *ordered;
	struct btrfs_inode *inode = BTRFS_I(vfs_inode);
	struct btrfs_root *root = inode->root;
	bool freespace_inode;

	WARN_ON(!hlist_empty(&vfs_inode->i_dentry));
	WARN_ON(vfs_inode->i_data.nrpages);
	WARN_ON(inode->block_rsv.reserved);
	WARN_ON(inode->block_rsv.size);
	WARN_ON(inode->outstanding_extents);
	if (!S_ISDIR(vfs_inode->i_mode)) {
		WARN_ON(inode->delalloc_bytes);
		WARN_ON(inode->new_delalloc_bytes);
		WARN_ON(inode->csum_bytes);
	}
	if (!root || !btrfs_is_data_reloc_root(root))
		WARN_ON(inode->defrag_bytes);

	/*
	 * This can happen where we create an inode, but somebody else also
	 * created the same inode and we need to destroy the one we already
	 * created.
	 */
	if (!root)
		return;

	/*
	 * If this is a free space inode do not take the ordered extents lockdep
	 * map.
	 */
	freespace_inode = btrfs_is_free_space_inode(inode);

	while (1) {
		ordered = btrfs_lookup_first_ordered_extent(inode, (u64)-1);
		if (!ordered)
			break;
		else {
			btrfs_err(root->fs_info,
				  "found ordered extent %llu %llu on inode cleanup",
				  ordered->file_offset, ordered->num_bytes);

			if (!freespace_inode)
				btrfs_lockdep_acquire(root->fs_info, btrfs_ordered_extent);

			btrfs_remove_ordered_extent(inode, ordered);
			btrfs_put_ordered_extent(ordered);
			btrfs_put_ordered_extent(ordered);
		}
	}
	btrfs_qgroup_check_reserved_leak(inode);
	btrfs_del_inode_from_root(inode);
	btrfs_drop_extent_map_range(inode, 0, (u64)-1, false);
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
	struct btrfs_inode *ei = foo;

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
}

int __init btrfs_init_cachep(void)
{
	btrfs_inode_cachep = kmem_cache_create("btrfs_inode",
			sizeof(struct btrfs_inode), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
			init_once);
	if (!btrfs_inode_cachep)
		return -ENOMEM;

	return 0;
}

static int btrfs_getattr(struct mnt_idmap *idmap,
			 const struct path *path, struct kstat *stat,
			 u32 request_mask, unsigned int flags)
{
	u64 delalloc_bytes;
	u64 inode_bytes;
	struct inode *inode = d_inode(path->dentry);
	u32 blocksize = btrfs_sb(inode->i_sb)->sectorsize;
	u32 bi_flags = BTRFS_I(inode)->flags;
	u32 bi_ro_flags = BTRFS_I(inode)->ro_flags;

	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = BTRFS_I(inode)->i_otime_sec;
	stat->btime.tv_nsec = BTRFS_I(inode)->i_otime_nsec;
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

	generic_fillattr(idmap, request_mask, inode, stat);
	stat->dev = BTRFS_I(inode)->root->anon_dev;

	stat->subvol = BTRFS_I(inode)->root->root_key.objectid;
	stat->result_mask |= STATX_SUBVOL;

	spin_lock(&BTRFS_I(inode)->lock);
	delalloc_bytes = BTRFS_I(inode)->new_delalloc_bytes;
	inode_bytes = inode_get_bytes(inode);
	spin_unlock(&BTRFS_I(inode)->lock);
	stat->blocks = (ALIGN(inode_bytes, blocksize) +
			ALIGN(delalloc_bytes, blocksize)) >> SECTOR_SHIFT;
	return 0;
}

static int btrfs_rename_exchange(struct inode *old_dir,
			      struct dentry *old_dentry,
			      struct inode *new_dir,
			      struct dentry *new_dentry)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(old_dir);
	struct btrfs_trans_handle *trans;
	unsigned int trans_num_items;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct btrfs_root *dest = BTRFS_I(new_dir)->root;
	struct inode *new_inode = new_dentry->d_inode;
	struct inode *old_inode = old_dentry->d_inode;
	struct btrfs_rename_ctx old_rename_ctx;
	struct btrfs_rename_ctx new_rename_ctx;
	u64 old_ino = btrfs_ino(BTRFS_I(old_inode));
	u64 new_ino = btrfs_ino(BTRFS_I(new_inode));
	u64 old_idx = 0;
	u64 new_idx = 0;
	int ret;
	int ret2;
	bool need_abort = false;
	struct fscrypt_name old_fname, new_fname;
	struct fscrypt_str *old_name, *new_name;

	/*
	 * For non-subvolumes allow exchange only within one subvolume, in the
	 * same inode namespace. Two subvolumes (represented as directory) can
	 * be exchanged as they're a logical link and have a fixed inode number.
	 */
	if (root != dest &&
	    (old_ino != BTRFS_FIRST_FREE_OBJECTID ||
	     new_ino != BTRFS_FIRST_FREE_OBJECTID))
		return -EXDEV;

	ret = fscrypt_setup_filename(old_dir, &old_dentry->d_name, 0, &old_fname);
	if (ret)
		return ret;

	ret = fscrypt_setup_filename(new_dir, &new_dentry->d_name, 0, &new_fname);
	if (ret) {
		fscrypt_free_filename(&old_fname);
		return ret;
	}

	old_name = &old_fname.disk_name;
	new_name = &new_fname.disk_name;

	/* close the race window with snapshot create/destroy ioctl */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID ||
	    new_ino == BTRFS_FIRST_FREE_OBJECTID)
		down_read(&fs_info->subvol_sem);

	/*
	 * For each inode:
	 * 1 to remove old dir item
	 * 1 to remove old dir index
	 * 1 to add new dir item
	 * 1 to add new dir index
	 * 1 to update parent inode
	 *
	 * If the parents are the same, we only need to account for one
	 */
	trans_num_items = (old_dir == new_dir ? 9 : 10);
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		/*
		 * 1 to remove old root ref
		 * 1 to remove old root backref
		 * 1 to add new root ref
		 * 1 to add new root backref
		 */
		trans_num_items += 4;
	} else {
		/*
		 * 1 to update inode item
		 * 1 to remove old inode ref
		 * 1 to add new inode ref
		 */
		trans_num_items += 3;
	}
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID)
		trans_num_items += 4;
	else
		trans_num_items += 3;
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
		ret = btrfs_insert_inode_ref(trans, dest, new_name, old_ino,
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
		ret = btrfs_insert_inode_ref(trans, root, old_name, new_ino,
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
	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);

	if (old_dentry->d_parent != new_dentry->d_parent) {
		btrfs_record_unlink_dir(trans, BTRFS_I(old_dir),
					BTRFS_I(old_inode), true);
		btrfs_record_unlink_dir(trans, BTRFS_I(new_dir),
					BTRFS_I(new_inode), true);
	}

	/* src is a subvolume */
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_unlink_subvol(trans, BTRFS_I(old_dir), old_dentry);
	} else { /* src is an inode */
		ret = __btrfs_unlink_inode(trans, BTRFS_I(old_dir),
					   BTRFS_I(old_dentry->d_inode),
					   old_name, &old_rename_ctx);
		if (!ret)
			ret = btrfs_update_inode(trans, BTRFS_I(old_inode));
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	/* dest is a subvolume */
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_unlink_subvol(trans, BTRFS_I(new_dir), new_dentry);
	} else { /* dest is an inode */
		ret = __btrfs_unlink_inode(trans, BTRFS_I(new_dir),
					   BTRFS_I(new_dentry->d_inode),
					   new_name, &new_rename_ctx);
		if (!ret)
			ret = btrfs_update_inode(trans, BTRFS_I(new_inode));
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, BTRFS_I(new_dir), BTRFS_I(old_inode),
			     new_name, 0, old_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	ret = btrfs_add_link(trans, BTRFS_I(old_dir), BTRFS_I(new_inode),
			     old_name, 0, new_idx);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_inode->i_nlink == 1)
		BTRFS_I(old_inode)->dir_index = old_idx;
	if (new_inode->i_nlink == 1)
		BTRFS_I(new_inode)->dir_index = new_idx;

	/*
	 * Now pin the logs of the roots. We do it to ensure that no other task
	 * can sync the logs while we are in progress with the rename, because
	 * that could result in an inconsistency in case any of the inodes that
	 * are part of this rename operation were logged before.
	 */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_pin_log_trans(root);
	if (new_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_pin_log_trans(dest);

	/* Do the log updates for all inodes. */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_log_new_name(trans, old_dentry, BTRFS_I(old_dir),
				   old_rename_ctx.index, new_dentry->d_parent);
	if (new_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_log_new_name(trans, new_dentry, BTRFS_I(new_dir),
				   new_rename_ctx.index, old_dentry->d_parent);

	/* Now unpin the logs. */
	if (old_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_end_log_trans(root);
	if (new_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_end_log_trans(dest);
out_fail:
	ret2 = btrfs_end_transaction(trans);
	ret = ret ? ret : ret2;
out_notrans:
	if (new_ino == BTRFS_FIRST_FREE_OBJECTID ||
	    old_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);

	fscrypt_free_filename(&new_fname);
	fscrypt_free_filename(&old_fname);
	return ret;
}

static struct inode *new_whiteout_inode(struct mnt_idmap *idmap,
					struct inode *dir)
{
	struct inode *inode;

	inode = new_inode(dir->i_sb);
	if (inode) {
		inode_init_owner(idmap, inode, dir,
				 S_IFCHR | WHITEOUT_MODE);
		inode->i_op = &btrfs_special_inode_operations;
		init_special_inode(inode, inode->i_mode, WHITEOUT_DEV);
	}
	return inode;
}

static int btrfs_rename(struct mnt_idmap *idmap,
			struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(old_dir);
	struct btrfs_new_inode_args whiteout_args = {
		.dir = old_dir,
		.dentry = old_dentry,
	};
	struct btrfs_trans_handle *trans;
	unsigned int trans_num_items;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct btrfs_root *dest = BTRFS_I(new_dir)->root;
	struct inode *new_inode = d_inode(new_dentry);
	struct inode *old_inode = d_inode(old_dentry);
	struct btrfs_rename_ctx rename_ctx;
	u64 index = 0;
	int ret;
	int ret2;
	u64 old_ino = btrfs_ino(BTRFS_I(old_inode));
	struct fscrypt_name old_fname, new_fname;

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

	ret = fscrypt_setup_filename(old_dir, &old_dentry->d_name, 0, &old_fname);
	if (ret)
		return ret;

	ret = fscrypt_setup_filename(new_dir, &new_dentry->d_name, 0, &new_fname);
	if (ret) {
		fscrypt_free_filename(&old_fname);
		return ret;
	}

	/* check for collisions, even if the  name isn't there */
	ret = btrfs_check_dir_item_collision(dest, new_dir->i_ino, &new_fname.disk_name);
	if (ret) {
		if (ret == -EEXIST) {
			/* we shouldn't get
			 * eexist without a new_inode */
			if (WARN_ON(!new_inode)) {
				goto out_fscrypt_names;
			}
		} else {
			/* maybe -EOVERFLOW */
			goto out_fscrypt_names;
		}
	}
	ret = 0;

	/*
	 * we're using rename to replace one file with another.  Start IO on it
	 * now so  we don't add too much work to the end of the transaction
	 */
	if (new_inode && S_ISREG(old_inode->i_mode) && new_inode->i_size)
		filemap_flush(old_inode->i_mapping);

	if (flags & RENAME_WHITEOUT) {
		whiteout_args.inode = new_whiteout_inode(idmap, old_dir);
		if (!whiteout_args.inode) {
			ret = -ENOMEM;
			goto out_fscrypt_names;
		}
		ret = btrfs_new_inode_prepare(&whiteout_args, &trans_num_items);
		if (ret)
			goto out_whiteout_inode;
	} else {
		/* 1 to update the old parent inode. */
		trans_num_items = 1;
	}

	if (old_ino == BTRFS_FIRST_FREE_OBJECTID) {
		/* Close the race window with snapshot create/destroy ioctl */
		down_read(&fs_info->subvol_sem);
		/*
		 * 1 to remove old root ref
		 * 1 to remove old root backref
		 * 1 to add new root ref
		 * 1 to add new root backref
		 */
		trans_num_items += 4;
	} else {
		/*
		 * 1 to update inode
		 * 1 to remove old inode ref
		 * 1 to add new inode ref
		 */
		trans_num_items += 3;
	}
	/*
	 * 1 to remove old dir item
	 * 1 to remove old dir index
	 * 1 to add new dir item
	 * 1 to add new dir index
	 */
	trans_num_items += 4;
	/* 1 to update new parent inode if it's not the same as the old parent */
	if (new_dir != old_dir)
		trans_num_items++;
	if (new_inode) {
		/*
		 * 1 to update inode
		 * 1 to remove inode ref
		 * 1 to remove dir item
		 * 1 to remove dir index
		 * 1 to possibly add orphan item
		 */
		trans_num_items += 5;
	}
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
		ret = btrfs_insert_inode_ref(trans, dest, &new_fname.disk_name,
					     old_ino, btrfs_ino(BTRFS_I(new_dir)),
					     index);
		if (ret)
			goto out_fail;
	}

	inode_inc_iversion(old_dir);
	inode_inc_iversion(new_dir);
	inode_inc_iversion(old_inode);
	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);

	if (old_dentry->d_parent != new_dentry->d_parent)
		btrfs_record_unlink_dir(trans, BTRFS_I(old_dir),
					BTRFS_I(old_inode), true);

	if (unlikely(old_ino == BTRFS_FIRST_FREE_OBJECTID)) {
		ret = btrfs_unlink_subvol(trans, BTRFS_I(old_dir), old_dentry);
	} else {
		ret = __btrfs_unlink_inode(trans, BTRFS_I(old_dir),
					   BTRFS_I(d_inode(old_dentry)),
					   &old_fname.disk_name, &rename_ctx);
		if (!ret)
			ret = btrfs_update_inode(trans, BTRFS_I(old_inode));
	}
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (new_inode) {
		inode_inc_iversion(new_inode);
		if (unlikely(btrfs_ino(BTRFS_I(new_inode)) ==
			     BTRFS_EMPTY_SUBVOL_DIR_OBJECTID)) {
			ret = btrfs_unlink_subvol(trans, BTRFS_I(new_dir), new_dentry);
			BUG_ON(new_inode->i_nlink == 0);
		} else {
			ret = btrfs_unlink_inode(trans, BTRFS_I(new_dir),
						 BTRFS_I(d_inode(new_dentry)),
						 &new_fname.disk_name);
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
			     &new_fname.disk_name, 0, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto out_fail;
	}

	if (old_inode->i_nlink == 1)
		BTRFS_I(old_inode)->dir_index = index;

	if (old_ino != BTRFS_FIRST_FREE_OBJECTID)
		btrfs_log_new_name(trans, old_dentry, BTRFS_I(old_dir),
				   rename_ctx.index, new_dentry->d_parent);

	if (flags & RENAME_WHITEOUT) {
		ret = btrfs_create_new_inode(trans, &whiteout_args);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_fail;
		} else {
			unlock_new_inode(whiteout_args.inode);
			iput(whiteout_args.inode);
			whiteout_args.inode = NULL;
		}
	}
out_fail:
	ret2 = btrfs_end_transaction(trans);
	ret = ret ? ret : ret2;
out_notrans:
	if (old_ino == BTRFS_FIRST_FREE_OBJECTID)
		up_read(&fs_info->subvol_sem);
	if (flags & RENAME_WHITEOUT)
		btrfs_new_inode_args_destroy(&whiteout_args);
out_whiteout_inode:
	if (flags & RENAME_WHITEOUT)
		iput(whiteout_args.inode);
out_fscrypt_names:
	fscrypt_free_filename(&old_fname);
	fscrypt_free_filename(&new_fname);
	return ret;
}

static int btrfs_rename2(struct mnt_idmap *idmap, struct inode *old_dir,
			 struct dentry *old_dentry, struct inode *new_dir,
			 struct dentry *new_dentry, unsigned int flags)
{
	int ret;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		ret = btrfs_rename_exchange(old_dir, old_dentry, new_dir,
					    new_dentry);
	else
		ret = btrfs_rename(idmap, old_dir, old_dentry, new_dir,
				   new_dentry, flags);

	btrfs_btree_balance_dirty(BTRFS_I(new_dir)->root->fs_info);

	return ret;
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
	btrfs_init_work(&work->work, btrfs_run_delalloc_work, NULL);

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
	LIST_HEAD(works);
	LIST_HEAD(splice);
	int ret = 0;
	bool full_flush = wbc->nr_to_write == LONG_MAX;

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
			btrfs_add_delayed_iput(BTRFS_I(inode));
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

	if (BTRFS_FS_ERROR(fs_info))
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
	LIST_HEAD(splice);
	int ret;

	if (BTRFS_FS_ERROR(fs_info))
		return -EROFS;

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

static int btrfs_symlink(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(dir);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct inode *inode;
	struct btrfs_new_inode_args new_inode_args = {
		.dir = dir,
		.dentry = dentry,
	};
	unsigned int trans_num_items;
	int err;
	int name_len;
	int datasize;
	unsigned long ptr;
	struct btrfs_file_extent_item *ei;
	struct extent_buffer *leaf;

	name_len = strlen(symname);
	if (name_len > BTRFS_MAX_INLINE_DATA_SIZE(fs_info))
		return -ENAMETOOLONG;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return -ENOMEM;
	inode_init_owner(idmap, inode, dir, S_IFLNK | S_IRWXUGO);
	inode->i_op = &btrfs_symlink_inode_operations;
	inode_nohighmem(inode);
	inode->i_mapping->a_ops = &btrfs_aops;
	btrfs_i_size_write(BTRFS_I(inode), name_len);
	inode_set_bytes(inode, name_len);

	new_inode_args.inode = inode;
	err = btrfs_new_inode_prepare(&new_inode_args, &trans_num_items);
	if (err)
		goto out_inode;
	/* 1 additional item for the inline extent */
	trans_num_items++;

	trans = btrfs_start_transaction(root, trans_num_items);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_new_inode_args;
	}

	err = btrfs_create_new_inode(trans, &new_inode_args);
	if (err)
		goto out;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		btrfs_abort_transaction(trans, err);
		discard_new_inode(inode);
		inode = NULL;
		goto out;
	}
	key.objectid = btrfs_ino(BTRFS_I(inode));
	key.offset = 0;
	key.type = BTRFS_EXTENT_DATA_KEY;
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	if (err) {
		btrfs_abort_transaction(trans, err);
		btrfs_free_path(path);
		discard_new_inode(inode);
		inode = NULL;
		goto out;
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
	btrfs_mark_buffer_dirty(trans, leaf);
	btrfs_free_path(path);

	d_instantiate_new(dentry, inode);
	err = 0;
out:
	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out_new_inode_args:
	btrfs_new_inode_args_destroy(&new_inode_args);
out_inode:
	if (err)
		iput(inode);
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
	u64 qgroup_released = 0;
	int ret;

	memset(&stack_fi, 0, sizeof(stack_fi));

	btrfs_set_stack_file_extent_type(&stack_fi, BTRFS_FILE_EXTENT_PREALLOC);
	btrfs_set_stack_file_extent_disk_bytenr(&stack_fi, start);
	btrfs_set_stack_file_extent_disk_num_bytes(&stack_fi, len);
	btrfs_set_stack_file_extent_num_bytes(&stack_fi, len);
	btrfs_set_stack_file_extent_ram_bytes(&stack_fi, len);
	btrfs_set_stack_file_extent_compression(&stack_fi, BTRFS_COMPRESS_NONE);
	/* Encryption and other encoding is reserved and all 0 */

	ret = btrfs_qgroup_release_data(inode, file_offset, len, &qgroup_released);
	if (ret < 0)
		return ERR_PTR(ret);

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
	extent_info.update_times = true;
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
			btrfs_root_id(inode->root), qgroup_released,
			BTRFS_QGROUP_RSV_DATA);
	return ERR_PTR(ret);
}

static int __btrfs_prealloc_file_range(struct inode *inode, int mode,
				       u64 start, u64 num_bytes, u64 min_size,
				       loff_t actual_len, u64 *alloc_hint,
				       struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(inode);
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

		em = alloc_extent_map();
		if (!em) {
			btrfs_drop_extent_map_range(BTRFS_I(inode), cur_offset,
					    cur_offset + ins.offset - 1, false);
			btrfs_set_inode_full_sync(BTRFS_I(inode));
			goto next;
		}

		em->start = cur_offset;
		em->len = ins.offset;
		em->disk_bytenr = ins.objectid;
		em->offset = 0;
		em->disk_num_bytes = ins.offset;
		em->ram_bytes = ins.offset;
		em->flags |= EXTENT_FLAG_PREALLOC;
		em->generation = trans->transid;

		ret = btrfs_replace_extent_map_range(BTRFS_I(inode), em, true);
		free_extent_map(em);
next:
		num_bytes -= ins.offset;
		cur_offset += ins.offset;
		*alloc_hint = ins.objectid + ins.offset;

		inode_inc_iversion(inode);
		inode_set_ctime_current(inode);
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

		ret = btrfs_update_inode(trans, BTRFS_I(inode));

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

static int btrfs_permission(struct mnt_idmap *idmap,
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
	return generic_permission(idmap, inode, mask);
}

static int btrfs_tmpfile(struct mnt_idmap *idmap, struct inode *dir,
			 struct file *file, umode_t mode)
{
	struct btrfs_fs_info *fs_info = inode_to_fs_info(dir);
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode;
	struct btrfs_new_inode_args new_inode_args = {
		.dir = dir,
		.dentry = file->f_path.dentry,
		.orphan = true,
	};
	unsigned int trans_num_items;
	int ret;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return -ENOMEM;
	inode_init_owner(idmap, inode, dir, mode);
	inode->i_fop = &btrfs_file_operations;
	inode->i_op = &btrfs_file_inode_operations;
	inode->i_mapping->a_ops = &btrfs_aops;

	new_inode_args.inode = inode;
	ret = btrfs_new_inode_prepare(&new_inode_args, &trans_num_items);
	if (ret)
		goto out_inode;

	trans = btrfs_start_transaction(root, trans_num_items);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_new_inode_args;
	}

	ret = btrfs_create_new_inode(trans, &new_inode_args);

	/*
	 * We set number of links to 0 in btrfs_create_new_inode(), and here we
	 * set it to 1 because d_tmpfile() will issue a warning if the count is
	 * 0, through:
	 *
	 *    d_tmpfile() -> inode_dec_link_count() -> drop_nlink()
	 */
	set_nlink(inode, 1);

	if (!ret) {
		d_tmpfile(file, inode);
		unlock_new_inode(inode);
		mark_inode_dirty(inode);
	}

	btrfs_end_transaction(trans);
	btrfs_btree_balance_dirty(fs_info);
out_new_inode_args:
	btrfs_new_inode_args_destroy(&new_inode_args);
out_inode:
	if (ret)
		iput(inode);
	return finish_open_simple(file, ret);
}

int btrfs_encoded_io_compression_from_extent(struct btrfs_fs_info *fs_info,
					     int compress_type)
{
	switch (compress_type) {
	case BTRFS_COMPRESS_NONE:
		return BTRFS_ENCODED_IO_COMPRESSION_NONE;
	case BTRFS_COMPRESS_ZLIB:
		return BTRFS_ENCODED_IO_COMPRESSION_ZLIB;
	case BTRFS_COMPRESS_LZO:
		/*
		 * The LZO format depends on the sector size. 64K is the maximum
		 * sector size that we support.
		 */
		if (fs_info->sectorsize < SZ_4K || fs_info->sectorsize > SZ_64K)
			return -EINVAL;
		return BTRFS_ENCODED_IO_COMPRESSION_LZO_4K +
		       (fs_info->sectorsize_bits - 12);
	case BTRFS_COMPRESS_ZSTD:
		return BTRFS_ENCODED_IO_COMPRESSION_ZSTD;
	default:
		return -EUCLEAN;
	}
}

static ssize_t btrfs_encoded_read_inline(
				struct kiocb *iocb,
				struct iov_iter *iter, u64 start,
				u64 lockend,
				struct extent_state **cached_state,
				u64 extent_start, size_t count,
				struct btrfs_ioctl_encoded_io_args *encoded,
				bool *unlocked)
{
	struct btrfs_inode *inode = BTRFS_I(file_inode(iocb->ki_filp));
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *item;
	u64 ram_bytes;
	unsigned long ptr;
	void *tmp;
	ssize_t ret;
	const bool nowait = (iocb->ki_flags & IOCB_NOWAIT);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	path->nowait = nowait;

	ret = btrfs_lookup_file_extent(NULL, root, path, btrfs_ino(inode),
				       extent_start, 0);
	if (ret) {
		if (ret > 0) {
			/* The extent item disappeared? */
			ret = -EIO;
		}
		goto out;
	}
	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_file_extent_item);

	ram_bytes = btrfs_file_extent_ram_bytes(leaf, item);
	ptr = btrfs_file_extent_inline_start(item);

	encoded->len = min_t(u64, extent_start + ram_bytes,
			     inode->vfs_inode.i_size) - iocb->ki_pos;
	ret = btrfs_encoded_io_compression_from_extent(fs_info,
				 btrfs_file_extent_compression(leaf, item));
	if (ret < 0)
		goto out;
	encoded->compression = ret;
	if (encoded->compression) {
		size_t inline_size;

		inline_size = btrfs_file_extent_inline_item_len(leaf,
								path->slots[0]);
		if (inline_size > count) {
			ret = -ENOBUFS;
			goto out;
		}
		count = inline_size;
		encoded->unencoded_len = ram_bytes;
		encoded->unencoded_offset = iocb->ki_pos - extent_start;
	} else {
		count = min_t(u64, count, encoded->len);
		encoded->len = count;
		encoded->unencoded_len = count;
		ptr += iocb->ki_pos - extent_start;
	}

	tmp = kmalloc(count, GFP_NOFS);
	if (!tmp) {
		ret = -ENOMEM;
		goto out;
	}
	read_extent_buffer(leaf, tmp, ptr, count);
	btrfs_release_path(path);
	unlock_extent(io_tree, start, lockend, cached_state);
	btrfs_inode_unlock(inode, BTRFS_ILOCK_SHARED);
	*unlocked = true;

	ret = copy_to_iter(tmp, count, iter);
	if (ret != count)
		ret = -EFAULT;
	kfree(tmp);
out:
	btrfs_free_path(path);
	return ret;
}

struct btrfs_encoded_read_private {
	wait_queue_head_t wait;
	void *uring_ctx;
	atomic_t pending;
	blk_status_t status;
};

static void btrfs_encoded_read_endio(struct btrfs_bio *bbio)
{
	struct btrfs_encoded_read_private *priv = bbio->private;

	if (bbio->bio.bi_status) {
		/*
		 * The memory barrier implied by the atomic_dec_return() here
		 * pairs with the memory barrier implied by the
		 * atomic_dec_return() or io_wait_event() in
		 * btrfs_encoded_read_regular_fill_pages() to ensure that this
		 * write is observed before the load of status in
		 * btrfs_encoded_read_regular_fill_pages().
		 */
		WRITE_ONCE(priv->status, bbio->bio.bi_status);
	}
	if (atomic_dec_and_test(&priv->pending)) {
		int err = blk_status_to_errno(READ_ONCE(priv->status));

		if (priv->uring_ctx) {
			btrfs_uring_read_extent_endio(priv->uring_ctx, err);
			kfree(priv);
		} else {
			wake_up(&priv->wait);
		}
	}
	bio_put(&bbio->bio);
}

int btrfs_encoded_read_regular_fill_pages(struct btrfs_inode *inode,
					  u64 disk_bytenr, u64 disk_io_size,
					  struct page **pages, void *uring_ctx)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_encoded_read_private *priv;
	unsigned long i = 0;
	struct btrfs_bio *bbio;
	int ret;

	priv = kmalloc(sizeof(struct btrfs_encoded_read_private), GFP_NOFS);
	if (!priv)
		return -ENOMEM;

	init_waitqueue_head(&priv->wait);
	atomic_set(&priv->pending, 1);
	priv->status = 0;
	priv->uring_ctx = uring_ctx;

	bbio = btrfs_bio_alloc(BIO_MAX_VECS, REQ_OP_READ, fs_info,
			       btrfs_encoded_read_endio, priv);
	bbio->bio.bi_iter.bi_sector = disk_bytenr >> SECTOR_SHIFT;
	bbio->inode = inode;

	do {
		size_t bytes = min_t(u64, disk_io_size, PAGE_SIZE);

		if (bio_add_page(&bbio->bio, pages[i], bytes, 0) < bytes) {
			atomic_inc(&priv->pending);
			btrfs_submit_bbio(bbio, 0);

			bbio = btrfs_bio_alloc(BIO_MAX_VECS, REQ_OP_READ, fs_info,
					       btrfs_encoded_read_endio, priv);
			bbio->bio.bi_iter.bi_sector = disk_bytenr >> SECTOR_SHIFT;
			bbio->inode = inode;
			continue;
		}

		i++;
		disk_bytenr += bytes;
		disk_io_size -= bytes;
	} while (disk_io_size);

	atomic_inc(&priv->pending);
	btrfs_submit_bbio(bbio, 0);

	if (uring_ctx) {
		if (atomic_dec_return(&priv->pending) == 0) {
			ret = blk_status_to_errno(READ_ONCE(priv->status));
			btrfs_uring_read_extent_endio(uring_ctx, ret);
			kfree(priv);
			return ret;
		}

		return -EIOCBQUEUED;
	} else {
		if (atomic_dec_return(&priv->pending) != 0)
			io_wait_event(priv->wait, !atomic_read(&priv->pending));
		/* See btrfs_encoded_read_endio() for ordering. */
		ret = blk_status_to_errno(READ_ONCE(priv->status));
		kfree(priv);
		return ret;
	}
}

ssize_t btrfs_encoded_read_regular(struct kiocb *iocb, struct iov_iter *iter,
				   u64 start, u64 lockend,
				   struct extent_state **cached_state,
				   u64 disk_bytenr, u64 disk_io_size,
				   size_t count, bool compressed, bool *unlocked)
{
	struct btrfs_inode *inode = BTRFS_I(file_inode(iocb->ki_filp));
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct page **pages;
	unsigned long nr_pages, i;
	u64 cur;
	size_t page_offset;
	ssize_t ret;

	nr_pages = DIV_ROUND_UP(disk_io_size, PAGE_SIZE);
	pages = kcalloc(nr_pages, sizeof(struct page *), GFP_NOFS);
	if (!pages)
		return -ENOMEM;
	ret = btrfs_alloc_page_array(nr_pages, pages, false);
	if (ret) {
		ret = -ENOMEM;
		goto out;
		}

	ret = btrfs_encoded_read_regular_fill_pages(inode, disk_bytenr,
						    disk_io_size, pages, NULL);
	if (ret)
		goto out;

	unlock_extent(io_tree, start, lockend, cached_state);
	btrfs_inode_unlock(inode, BTRFS_ILOCK_SHARED);
	*unlocked = true;

	if (compressed) {
		i = 0;
		page_offset = 0;
	} else {
		i = (iocb->ki_pos - start) >> PAGE_SHIFT;
		page_offset = (iocb->ki_pos - start) & (PAGE_SIZE - 1);
	}
	cur = 0;
	while (cur < count) {
		size_t bytes = min_t(size_t, count - cur,
				     PAGE_SIZE - page_offset);

		if (copy_page_to_iter(pages[i], page_offset, bytes,
				      iter) != bytes) {
			ret = -EFAULT;
			goto out;
		}
		i++;
		cur += bytes;
		page_offset = 0;
	}
	ret = count;
out:
	for (i = 0; i < nr_pages; i++) {
		if (pages[i])
			__free_page(pages[i]);
	}
	kfree(pages);
	return ret;
}

ssize_t btrfs_encoded_read(struct kiocb *iocb, struct iov_iter *iter,
			   struct btrfs_ioctl_encoded_io_args *encoded,
			   struct extent_state **cached_state,
			   u64 *disk_bytenr, u64 *disk_io_size)
{
	struct btrfs_inode *inode = BTRFS_I(file_inode(iocb->ki_filp));
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct extent_io_tree *io_tree = &inode->io_tree;
	ssize_t ret;
	size_t count = iov_iter_count(iter);
	u64 start, lockend;
	struct extent_map *em;
	const bool nowait = (iocb->ki_flags & IOCB_NOWAIT);
	bool unlocked = false;

	file_accessed(iocb->ki_filp);

	ret = btrfs_inode_lock(inode,
			       BTRFS_ILOCK_SHARED | (nowait ? BTRFS_ILOCK_TRY : 0));
	if (ret)
		return ret;

	if (iocb->ki_pos >= inode->vfs_inode.i_size) {
		btrfs_inode_unlock(inode, BTRFS_ILOCK_SHARED);
		return 0;
	}
	start = ALIGN_DOWN(iocb->ki_pos, fs_info->sectorsize);
	/*
	 * We don't know how long the extent containing iocb->ki_pos is, but if
	 * it's compressed we know that it won't be longer than this.
	 */
	lockend = start + BTRFS_MAX_UNCOMPRESSED - 1;

	if (nowait) {
		struct btrfs_ordered_extent *ordered;

		if (filemap_range_needs_writeback(inode->vfs_inode.i_mapping,
						  start, lockend)) {
			ret = -EAGAIN;
			goto out_unlock_inode;
		}

		if (!try_lock_extent(io_tree, start, lockend, cached_state)) {
			ret = -EAGAIN;
			goto out_unlock_inode;
		}

		ordered = btrfs_lookup_ordered_range(inode, start,
						     lockend - start + 1);
		if (ordered) {
			btrfs_put_ordered_extent(ordered);
			unlock_extent(io_tree, start, lockend, cached_state);
			ret = -EAGAIN;
			goto out_unlock_inode;
		}
	} else {
		for (;;) {
			struct btrfs_ordered_extent *ordered;

			ret = btrfs_wait_ordered_range(inode, start,
						       lockend - start + 1);
			if (ret)
				goto out_unlock_inode;

			lock_extent(io_tree, start, lockend, cached_state);
			ordered = btrfs_lookup_ordered_range(inode, start,
							     lockend - start + 1);
			if (!ordered)
				break;
			btrfs_put_ordered_extent(ordered);
			unlock_extent(io_tree, start, lockend, cached_state);
			cond_resched();
		}
	}

	em = btrfs_get_extent(inode, NULL, start, lockend - start + 1);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out_unlock_extent;
	}

	if (em->disk_bytenr == EXTENT_MAP_INLINE) {
		u64 extent_start = em->start;

		/*
		 * For inline extents we get everything we need out of the
		 * extent item.
		 */
		free_extent_map(em);
		em = NULL;
		ret = btrfs_encoded_read_inline(iocb, iter, start, lockend,
						cached_state, extent_start,
						count, encoded, &unlocked);
		goto out_unlock_extent;
	}

	/*
	 * We only want to return up to EOF even if the extent extends beyond
	 * that.
	 */
	encoded->len = min_t(u64, extent_map_end(em),
			     inode->vfs_inode.i_size) - iocb->ki_pos;
	if (em->disk_bytenr == EXTENT_MAP_HOLE ||
	    (em->flags & EXTENT_FLAG_PREALLOC)) {
		*disk_bytenr = EXTENT_MAP_HOLE;
		count = min_t(u64, count, encoded->len);
		encoded->len = count;
		encoded->unencoded_len = count;
	} else if (extent_map_is_compressed(em)) {
		*disk_bytenr = em->disk_bytenr;
		/*
		 * Bail if the buffer isn't large enough to return the whole
		 * compressed extent.
		 */
		if (em->disk_num_bytes > count) {
			ret = -ENOBUFS;
			goto out_em;
		}
		*disk_io_size = em->disk_num_bytes;
		count = em->disk_num_bytes;
		encoded->unencoded_len = em->ram_bytes;
		encoded->unencoded_offset = iocb->ki_pos - (em->start - em->offset);
		ret = btrfs_encoded_io_compression_from_extent(fs_info,
							       extent_map_compression(em));
		if (ret < 0)
			goto out_em;
		encoded->compression = ret;
	} else {
		*disk_bytenr = extent_map_block_start(em) + (start - em->start);
		if (encoded->len > count)
			encoded->len = count;
		/*
		 * Don't read beyond what we locked. This also limits the page
		 * allocations that we'll do.
		 */
		*disk_io_size = min(lockend + 1, iocb->ki_pos + encoded->len) - start;
		count = start + *disk_io_size - iocb->ki_pos;
		encoded->len = count;
		encoded->unencoded_len = count;
		*disk_io_size = ALIGN(*disk_io_size, fs_info->sectorsize);
	}
	free_extent_map(em);
	em = NULL;

	if (*disk_bytenr == EXTENT_MAP_HOLE) {
		unlock_extent(io_tree, start, lockend, cached_state);
		btrfs_inode_unlock(inode, BTRFS_ILOCK_SHARED);
		unlocked = true;
		ret = iov_iter_zero(count, iter);
		if (ret != count)
			ret = -EFAULT;
	} else {
		ret = -EIOCBQUEUED;
		goto out_unlock_extent;
	}

out_em:
	free_extent_map(em);
out_unlock_extent:
	/* Leave inode and extent locked if we need to do a read. */
	if (!unlocked && ret != -EIOCBQUEUED)
		unlock_extent(io_tree, start, lockend, cached_state);
out_unlock_inode:
	if (!unlocked && ret != -EIOCBQUEUED)
		btrfs_inode_unlock(inode, BTRFS_ILOCK_SHARED);
	return ret;
}

ssize_t btrfs_do_encoded_write(struct kiocb *iocb, struct iov_iter *from,
			       const struct btrfs_ioctl_encoded_io_args *encoded)
{
	struct btrfs_inode *inode = BTRFS_I(file_inode(iocb->ki_filp));
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *io_tree = &inode->io_tree;
	struct extent_changeset *data_reserved = NULL;
	struct extent_state *cached_state = NULL;
	struct btrfs_ordered_extent *ordered;
	struct btrfs_file_extent file_extent;
	int compression;
	size_t orig_count;
	u64 start, end;
	u64 num_bytes, ram_bytes, disk_num_bytes;
	unsigned long nr_folios, i;
	struct folio **folios;
	struct btrfs_key ins;
	bool extent_reserved = false;
	struct extent_map *em;
	ssize_t ret;

	switch (encoded->compression) {
	case BTRFS_ENCODED_IO_COMPRESSION_ZLIB:
		compression = BTRFS_COMPRESS_ZLIB;
		break;
	case BTRFS_ENCODED_IO_COMPRESSION_ZSTD:
		compression = BTRFS_COMPRESS_ZSTD;
		break;
	case BTRFS_ENCODED_IO_COMPRESSION_LZO_4K:
	case BTRFS_ENCODED_IO_COMPRESSION_LZO_8K:
	case BTRFS_ENCODED_IO_COMPRESSION_LZO_16K:
	case BTRFS_ENCODED_IO_COMPRESSION_LZO_32K:
	case BTRFS_ENCODED_IO_COMPRESSION_LZO_64K:
		/* The sector size must match for LZO. */
		if (encoded->compression -
		    BTRFS_ENCODED_IO_COMPRESSION_LZO_4K + 12 !=
		    fs_info->sectorsize_bits)
			return -EINVAL;
		compression = BTRFS_COMPRESS_LZO;
		break;
	default:
		return -EINVAL;
	}
	if (encoded->encryption != BTRFS_ENCODED_IO_ENCRYPTION_NONE)
		return -EINVAL;

	/*
	 * Compressed extents should always have checksums, so error out if we
	 * have a NOCOW file or inode was created while mounted with NODATASUM.
	 */
	if (inode->flags & BTRFS_INODE_NODATASUM)
		return -EINVAL;

	orig_count = iov_iter_count(from);

	/* The extent size must be sane. */
	if (encoded->unencoded_len > BTRFS_MAX_UNCOMPRESSED ||
	    orig_count > BTRFS_MAX_COMPRESSED || orig_count == 0)
		return -EINVAL;

	/*
	 * The compressed data must be smaller than the decompressed data.
	 *
	 * It's of course possible for data to compress to larger or the same
	 * size, but the buffered I/O path falls back to no compression for such
	 * data, and we don't want to break any assumptions by creating these
	 * extents.
	 *
	 * Note that this is less strict than the current check we have that the
	 * compressed data must be at least one sector smaller than the
	 * decompressed data. We only want to enforce the weaker requirement
	 * from old kernels that it is at least one byte smaller.
	 */
	if (orig_count >= encoded->unencoded_len)
		return -EINVAL;

	/* The extent must start on a sector boundary. */
	start = iocb->ki_pos;
	if (!IS_ALIGNED(start, fs_info->sectorsize))
		return -EINVAL;

	/*
	 * The extent must end on a sector boundary. However, we allow a write
	 * which ends at or extends i_size to have an unaligned length; we round
	 * up the extent size and set i_size to the unaligned end.
	 */
	if (start + encoded->len < inode->vfs_inode.i_size &&
	    !IS_ALIGNED(start + encoded->len, fs_info->sectorsize))
		return -EINVAL;

	/* Finally, the offset in the unencoded data must be sector-aligned. */
	if (!IS_ALIGNED(encoded->unencoded_offset, fs_info->sectorsize))
		return -EINVAL;

	num_bytes = ALIGN(encoded->len, fs_info->sectorsize);
	ram_bytes = ALIGN(encoded->unencoded_len, fs_info->sectorsize);
	end = start + num_bytes - 1;

	/*
	 * If the extent cannot be inline, the compressed data on disk must be
	 * sector-aligned. For convenience, we extend it with zeroes if it
	 * isn't.
	 */
	disk_num_bytes = ALIGN(orig_count, fs_info->sectorsize);
	nr_folios = DIV_ROUND_UP(disk_num_bytes, PAGE_SIZE);
	folios = kvcalloc(nr_folios, sizeof(struct folio *), GFP_KERNEL_ACCOUNT);
	if (!folios)
		return -ENOMEM;
	for (i = 0; i < nr_folios; i++) {
		size_t bytes = min_t(size_t, PAGE_SIZE, iov_iter_count(from));
		char *kaddr;

		folios[i] = folio_alloc(GFP_KERNEL_ACCOUNT, 0);
		if (!folios[i]) {
			ret = -ENOMEM;
			goto out_folios;
		}
		kaddr = kmap_local_folio(folios[i], 0);
		if (copy_from_iter(kaddr, bytes, from) != bytes) {
			kunmap_local(kaddr);
			ret = -EFAULT;
			goto out_folios;
		}
		if (bytes < PAGE_SIZE)
			memset(kaddr + bytes, 0, PAGE_SIZE - bytes);
		kunmap_local(kaddr);
	}

	for (;;) {
		struct btrfs_ordered_extent *ordered;

		ret = btrfs_wait_ordered_range(inode, start, num_bytes);
		if (ret)
			goto out_folios;
		ret = invalidate_inode_pages2_range(inode->vfs_inode.i_mapping,
						    start >> PAGE_SHIFT,
						    end >> PAGE_SHIFT);
		if (ret)
			goto out_folios;
		lock_extent(io_tree, start, end, &cached_state);
		ordered = btrfs_lookup_ordered_range(inode, start, num_bytes);
		if (!ordered &&
		    !filemap_range_has_page(inode->vfs_inode.i_mapping, start, end))
			break;
		if (ordered)
			btrfs_put_ordered_extent(ordered);
		unlock_extent(io_tree, start, end, &cached_state);
		cond_resched();
	}

	/*
	 * We don't use the higher-level delalloc space functions because our
	 * num_bytes and disk_num_bytes are different.
	 */
	ret = btrfs_alloc_data_chunk_ondemand(inode, disk_num_bytes);
	if (ret)
		goto out_unlock;
	ret = btrfs_qgroup_reserve_data(inode, &data_reserved, start, num_bytes);
	if (ret)
		goto out_free_data_space;
	ret = btrfs_delalloc_reserve_metadata(inode, num_bytes, disk_num_bytes,
					      false);
	if (ret)
		goto out_qgroup_free_data;

	/* Try an inline extent first. */
	if (encoded->unencoded_len == encoded->len &&
	    encoded->unencoded_offset == 0 &&
	    can_cow_file_range_inline(inode, start, encoded->len, orig_count)) {
		ret = __cow_file_range_inline(inode, encoded->len,
					      orig_count, compression, folios[0],
					      true);
		if (ret <= 0) {
			if (ret == 0)
				ret = orig_count;
			goto out_delalloc_release;
		}
	}

	ret = btrfs_reserve_extent(root, disk_num_bytes, disk_num_bytes,
				   disk_num_bytes, 0, 0, &ins, 1, 1);
	if (ret)
		goto out_delalloc_release;
	extent_reserved = true;

	file_extent.disk_bytenr = ins.objectid;
	file_extent.disk_num_bytes = ins.offset;
	file_extent.num_bytes = num_bytes;
	file_extent.ram_bytes = ram_bytes;
	file_extent.offset = encoded->unencoded_offset;
	file_extent.compression = compression;
	em = btrfs_create_io_em(inode, start, &file_extent, BTRFS_ORDERED_COMPRESSED);
	if (IS_ERR(em)) {
		ret = PTR_ERR(em);
		goto out_free_reserved;
	}
	free_extent_map(em);

	ordered = btrfs_alloc_ordered_extent(inode, start, &file_extent,
				       (1 << BTRFS_ORDERED_ENCODED) |
				       (1 << BTRFS_ORDERED_COMPRESSED));
	if (IS_ERR(ordered)) {
		btrfs_drop_extent_map_range(inode, start, end, false);
		ret = PTR_ERR(ordered);
		goto out_free_reserved;
	}
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);

	if (start + encoded->len > inode->vfs_inode.i_size)
		i_size_write(&inode->vfs_inode, start + encoded->len);

	unlock_extent(io_tree, start, end, &cached_state);

	btrfs_delalloc_release_extents(inode, num_bytes);

	btrfs_submit_compressed_write(ordered, folios, nr_folios, 0, false);
	ret = orig_count;
	goto out;

out_free_reserved:
	btrfs_dec_block_group_reservations(fs_info, ins.objectid);
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 1);
out_delalloc_release:
	btrfs_delalloc_release_extents(inode, num_bytes);
	btrfs_delalloc_release_metadata(inode, disk_num_bytes, ret < 0);
out_qgroup_free_data:
	if (ret < 0)
		btrfs_qgroup_free_data(inode, data_reserved, start, num_bytes, NULL);
out_free_data_space:
	/*
	 * If btrfs_reserve_extent() succeeded, then we already decremented
	 * bytes_may_use.
	 */
	if (!extent_reserved)
		btrfs_free_reserved_data_space_noquota(fs_info, disk_num_bytes);
out_unlock:
	unlock_extent(io_tree, start, end, &cached_state);
out_folios:
	for (i = 0; i < nr_folios; i++) {
		if (folios[i])
			folio_put(folios[i]);
	}
	kvfree(folios);
out:
	if (ret >= 0)
		iocb->ki_pos += encoded->len;
	return ret;
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
	unsigned long max_pages;
	u64 first_ppage, first_ppage_reported, next_ppage;
	int ret;

	/*
	 * Our swapfile may have had its size extended after the swap header was
	 * written. In that case activating the swapfile should not go beyond
	 * the max size set in the swap header.
	 */
	if (bsi->nr_pages >= sis->max)
		return 0;

	max_pages = sis->max - bsi->nr_pages;
	first_ppage = PAGE_ALIGN(bsi->block_start) >> PAGE_SHIFT;
	next_ppage = PAGE_ALIGN_DOWN(bsi->block_start + bsi->block_len) >> PAGE_SHIFT;

	if (first_ppage >= next_ppage)
		return 0;
	nr_pages = next_ppage - first_ppage;
	nr_pages = min(nr_pages, max_pages);

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
	struct btrfs_chunk_map *map = NULL;
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
	ret = btrfs_wait_ordered_range(BTRFS_I(inode), 0, (u64)-1);
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
	 *
	 * It is possible that subvolume is marked for deletion but still not
	 * removed yet. To prevent this race, we check the root status before
	 * activating the swapfile.
	 */
	spin_lock(&root->root_item_lock);
	if (btrfs_root_dead(root)) {
		spin_unlock(&root->root_item_lock);

		btrfs_drew_write_unlock(&root->snapshot_lock);
		btrfs_exclop_finish(fs_info);
		btrfs_warn(fs_info,
		"cannot activate swapfile because subvolume %llu is being deleted",
			btrfs_root_id(root));
		return -EPERM;
	}
	atomic_inc(&root->nr_swapfiles);
	spin_unlock(&root->root_item_lock);

	isize = ALIGN_DOWN(inode->i_size, fs_info->sectorsize);

	lock_extent(io_tree, 0, isize - 1, &cached_state);
	start = 0;
	while (start < isize) {
		u64 logical_block_start, physical_block_start;
		struct btrfs_block_group *bg;
		u64 len = isize - start;

		em = btrfs_get_extent(BTRFS_I(inode), NULL, start, len);
		if (IS_ERR(em)) {
			ret = PTR_ERR(em);
			goto out;
		}

		if (em->disk_bytenr == EXTENT_MAP_HOLE) {
			btrfs_warn(fs_info, "swapfile must not have holes");
			ret = -EINVAL;
			goto out;
		}
		if (em->disk_bytenr == EXTENT_MAP_INLINE) {
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
		if (extent_map_is_compressed(em)) {
			btrfs_warn(fs_info, "swapfile must not be compressed");
			ret = -EINVAL;
			goto out;
		}

		logical_block_start = extent_map_block_start(em) + (start - em->start);
		len = min(len, em->len - (start - em->start));
		free_extent_map(em);
		em = NULL;

		ret = can_nocow_extent(inode, start, &len, NULL, false, true);
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

		map = btrfs_get_chunk_map(fs_info, logical_block_start, len);
		if (IS_ERR(map)) {
			ret = PTR_ERR(map);
			goto out;
		}

		if (map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK) {
			btrfs_warn(fs_info,
				   "swapfile must have single data profile");
			ret = -EINVAL;
			goto out;
		}

		if (device == NULL) {
			device = map->stripes[0].dev;
			ret = btrfs_add_swapfile_pin(inode, device, false);
			if (ret == 1)
				ret = 0;
			else if (ret)
				goto out;
		} else if (device != map->stripes[0].dev) {
			btrfs_warn(fs_info, "swapfile must be on one device");
			ret = -EINVAL;
			goto out;
		}

		physical_block_start = (map->stripes[0].physical +
					(logical_block_start - map->start));
		len = min(len, map->chunk_len - (logical_block_start - map->start));
		btrfs_free_chunk_map(map);
		map = NULL;

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
	if (!IS_ERR_OR_NULL(map))
		btrfs_free_chunk_map(map);

	unlock_extent(io_tree, 0, isize - 1, &cached_state);

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

/*
 * Verify that there are no ordered extents for a given file range.
 *
 * @inode:   The target inode.
 * @start:   Start offset of the file range, should be sector size aligned.
 * @end:     End offset (inclusive) of the file range, its value +1 should be
 *           sector size aligned.
 *
 * This should typically be used for cases where we locked an inode's VFS lock in
 * exclusive mode, we have also locked the inode's i_mmap_lock in exclusive mode,
 * we have flushed all delalloc in the range, we have waited for all ordered
 * extents in the range to complete and finally we have locked the file range in
 * the inode's io_tree.
 */
void btrfs_assert_inode_range_clean(struct btrfs_inode *inode, u64 start, u64 end)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_ordered_extent *ordered;

	if (!IS_ENABLED(CONFIG_BTRFS_ASSERT))
		return;

	ordered = btrfs_lookup_first_ordered_range(inode, start, end + 1 - start);
	if (ordered) {
		btrfs_err(root->fs_info,
"found unexpected ordered extent in file range [%llu, %llu] for inode %llu root %llu (ordered range [%llu, %llu])",
			  start, end, btrfs_ino(inode), btrfs_root_id(root),
			  ordered->file_offset,
			  ordered->file_offset + ordered->num_bytes - 1);
		btrfs_put_ordered_extent(ordered);
	}

	ASSERT(ordered == NULL);
}

/*
 * Find the first inode with a minimum number.
 *
 * @root:	The root to search for.
 * @min_ino:	The minimum inode number.
 *
 * Find the first inode in the @root with a number >= @min_ino and return it.
 * Returns NULL if no such inode found.
 */
struct btrfs_inode *btrfs_find_first_inode(struct btrfs_root *root, u64 min_ino)
{
	struct btrfs_inode *inode;
	unsigned long from = min_ino;

	xa_lock(&root->inodes);
	while (true) {
		inode = xa_find(&root->inodes, &from, ULONG_MAX, XA_PRESENT);
		if (!inode)
			break;
		if (igrab(&inode->vfs_inode))
			break;

		from = btrfs_ino(inode) + 1;
		cond_resched_lock(&root->inodes.xa_lock);
	}
	xa_unlock(&root->inodes);

	return inode;
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
	.get_inode_acl	= btrfs_get_acl,
	.set_acl	= btrfs_set_acl,
	.update_time	= btrfs_update_time,
	.tmpfile        = btrfs_tmpfile,
	.fileattr_get	= btrfs_fileattr_get,
	.fileattr_set	= btrfs_fileattr_set,
};

static const struct file_operations btrfs_dir_file_operations = {
	.llseek		= btrfs_dir_llseek,
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
	.read_folio	= btrfs_read_folio,
	.writepages	= btrfs_writepages,
	.readahead	= btrfs_readahead,
	.invalidate_folio = btrfs_invalidate_folio,
	.launder_folio	= btrfs_launder_folio,
	.release_folio	= btrfs_release_folio,
	.migrate_folio	= btrfs_migrate_folio,
	.dirty_folio	= filemap_dirty_folio,
	.error_remove_folio = generic_error_remove_folio,
	.swap_activate	= btrfs_swap_activate,
	.swap_deactivate = btrfs_swap_deactivate,
};

static const struct inode_operations btrfs_file_inode_operations = {
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
	.listxattr      = btrfs_listxattr,
	.permission	= btrfs_permission,
	.fiemap		= btrfs_fiemap,
	.get_inode_acl	= btrfs_get_acl,
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
	.get_inode_acl	= btrfs_get_acl,
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
