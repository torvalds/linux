/*
 * inode.c - NILFS inode operations.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Ryusuke Konishi.
 *
 */

#include <linux/buffer_head.h>
#include <linux/gfp.h>
#include <linux/mpage.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/uio.h>
#include "nilfs.h"
#include "btnode.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"
#include "cpfile.h"
#include "ifile.h"

/**
 * struct nilfs_iget_args - arguments used during comparison between inodes
 * @ino: inode number
 * @cno: checkpoint number
 * @root: pointer on NILFS root object (mounted checkpoint)
 * @for_gc: inode for GC flag
 */
struct nilfs_iget_args {
	u64 ino;
	__u64 cno;
	struct nilfs_root *root;
	int for_gc;
};

static int nilfs_iget_test(struct inode *inode, void *opaque);

void nilfs_inode_add_blocks(struct inode *inode, int n)
{
	struct nilfs_root *root = NILFS_I(inode)->i_root;

	inode_add_bytes(inode, (1 << inode->i_blkbits) * n);
	if (root)
		atomic64_add(n, &root->blocks_count);
}

void nilfs_inode_sub_blocks(struct inode *inode, int n)
{
	struct nilfs_root *root = NILFS_I(inode)->i_root;

	inode_sub_bytes(inode, (1 << inode->i_blkbits) * n);
	if (root)
		atomic64_sub(n, &root->blocks_count);
}

/**
 * nilfs_get_block() - get a file block on the filesystem (callback function)
 * @inode - inode struct of the target file
 * @blkoff - file block number
 * @bh_result - buffer head to be mapped on
 * @create - indicate whether allocating the block or not when it has not
 *      been allocated yet.
 *
 * This function does not issue actual read request of the specified data
 * block. It is done by VFS.
 */
int nilfs_get_block(struct inode *inode, sector_t blkoff,
		    struct buffer_head *bh_result, int create)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	__u64 blknum = 0;
	int err = 0, ret;
	unsigned maxblocks = bh_result->b_size >> inode->i_blkbits;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	ret = nilfs_bmap_lookup_contig(ii->i_bmap, blkoff, &blknum, maxblocks);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	if (ret >= 0) {	/* found */
		map_bh(bh_result, inode->i_sb, blknum);
		if (ret > 0)
			bh_result->b_size = (ret << inode->i_blkbits);
		goto out;
	}
	/* data block was not found */
	if (ret == -ENOENT && create) {
		struct nilfs_transaction_info ti;

		bh_result->b_blocknr = 0;
		err = nilfs_transaction_begin(inode->i_sb, &ti, 1);
		if (unlikely(err))
			goto out;
		err = nilfs_bmap_insert(ii->i_bmap, blkoff,
					(unsigned long)bh_result);
		if (unlikely(err != 0)) {
			if (err == -EEXIST) {
				/*
				 * The get_block() function could be called
				 * from multiple callers for an inode.
				 * However, the page having this block must
				 * be locked in this case.
				 */
				printk(KERN_WARNING
				       "nilfs_get_block: a race condition "
				       "while inserting a data block. "
				       "(inode number=%lu, file block "
				       "offset=%llu)\n",
				       inode->i_ino,
				       (unsigned long long)blkoff);
				err = 0;
			}
			nilfs_transaction_abort(inode->i_sb);
			goto out;
		}
		nilfs_mark_inode_dirty_sync(inode);
		nilfs_transaction_commit(inode->i_sb); /* never fails */
		/* Error handling should be detailed */
		set_buffer_new(bh_result);
		set_buffer_delay(bh_result);
		map_bh(bh_result, inode->i_sb, 0); /* dbn must be changed
						      to proper value */
	} else if (ret == -ENOENT) {
		/* not found is not error (e.g. hole); must return without
		   the mapped state flag. */
		;
	} else {
		err = ret;
	}

 out:
	return err;
}

/**
 * nilfs_readpage() - implement readpage() method of nilfs_aops {}
 * address_space_operations.
 * @file - file struct of the file to be read
 * @page - the page to be read
 */
static int nilfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, nilfs_get_block);
}

/**
 * nilfs_readpages() - implement readpages() method of nilfs_aops {}
 * address_space_operations.
 * @file - file struct of the file to be read
 * @mapping - address_space struct used for reading multiple pages
 * @pages - the pages to be read
 * @nr_pages - number of pages to be read
 */
static int nilfs_readpages(struct file *file, struct address_space *mapping,
			   struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, nilfs_get_block);
}

static int nilfs_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	int err = 0;

	if (inode->i_sb->s_flags & MS_RDONLY) {
		nilfs_clear_dirty_pages(mapping, false);
		return -EROFS;
	}

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_dsync_segment(inode->i_sb, inode,
						    wbc->range_start,
						    wbc->range_end);
	return err;
}

static int nilfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	int err;

	if (inode->i_sb->s_flags & MS_RDONLY) {
		/*
		 * It means that filesystem was remounted in read-only
		 * mode because of error or metadata corruption. But we
		 * have dirty pages that try to be flushed in background.
		 * So, here we simply discard this dirty page.
		 */
		nilfs_clear_dirty_page(page, false);
		unlock_page(page);
		return -EROFS;
	}

	redirty_page_for_writepage(wbc, page);
	unlock_page(page);

	if (wbc->sync_mode == WB_SYNC_ALL) {
		err = nilfs_construct_segment(inode->i_sb);
		if (unlikely(err))
			return err;
	} else if (wbc->for_reclaim)
		nilfs_flush_segment(inode->i_sb, inode->i_ino);

	return 0;
}

static int nilfs_set_page_dirty(struct page *page)
{
	struct inode *inode = page->mapping->host;
	int ret = __set_page_dirty_nobuffers(page);

	if (page_has_buffers(page)) {
		unsigned nr_dirty = 0;
		struct buffer_head *bh, *head;

		/*
		 * This page is locked by callers, and no other thread
		 * concurrently marks its buffers dirty since they are
		 * only dirtied through routines in fs/buffer.c in
		 * which call sites of mark_buffer_dirty are protected
		 * by page lock.
		 */
		bh = head = page_buffers(page);
		do {
			/* Do not mark hole blocks dirty */
			if (buffer_dirty(bh) || !buffer_mapped(bh))
				continue;

			set_buffer_dirty(bh);
			nr_dirty++;
		} while (bh = bh->b_this_page, bh != head);

		if (nr_dirty)
			nilfs_set_file_dirty(inode, nr_dirty);
	} else if (ret) {
		unsigned nr_dirty = 1 << (PAGE_SHIFT - inode->i_blkbits);

		nilfs_set_file_dirty(inode, nr_dirty);
	}
	return ret;
}

void nilfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		nilfs_truncate(inode);
	}
}

static int nilfs_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata)

{
	struct inode *inode = mapping->host;
	int err = nilfs_transaction_begin(inode->i_sb, NULL, 1);

	if (unlikely(err))
		return err;

	err = block_write_begin(mapping, pos, len, flags, pagep,
				nilfs_get_block);
	if (unlikely(err)) {
		nilfs_write_failed(mapping, pos + len);
		nilfs_transaction_abort(inode->i_sb);
	}
	return err;
}

static int nilfs_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	unsigned start = pos & (PAGE_SIZE - 1);
	unsigned nr_dirty;
	int err;

	nr_dirty = nilfs_page_count_clean_buffers(page, start,
						  start + copied);
	copied = generic_write_end(file, mapping, pos, len, copied, page,
				   fsdata);
	nilfs_set_file_dirty(inode, nr_dirty);
	err = nilfs_transaction_commit(inode->i_sb);
	return err ? : copied;
}

static ssize_t
nilfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	if (iov_iter_rw(iter) == WRITE)
		return 0;

	/* Needs synchronization with the cleaner */
	return blockdev_direct_IO(iocb, inode, iter, nilfs_get_block);
}

const struct address_space_operations nilfs_aops = {
	.writepage		= nilfs_writepage,
	.readpage		= nilfs_readpage,
	.writepages		= nilfs_writepages,
	.set_page_dirty		= nilfs_set_page_dirty,
	.readpages		= nilfs_readpages,
	.write_begin		= nilfs_write_begin,
	.write_end		= nilfs_write_end,
	/* .releasepage		= nilfs_releasepage, */
	.invalidatepage		= block_invalidatepage,
	.direct_IO		= nilfs_direct_IO,
	.is_partially_uptodate  = block_is_partially_uptodate,
};

static int nilfs_insert_inode_locked(struct inode *inode,
				     struct nilfs_root *root,
				     unsigned long ino)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = root, .cno = 0, .for_gc = 0
	};

	return insert_inode_locked4(inode, ino, nilfs_iget_test, &args);
}

struct inode *nilfs_new_inode(struct inode *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct inode *inode;
	struct nilfs_inode_info *ii;
	struct nilfs_root *root;
	int err = -ENOMEM;
	ino_t ino;

	inode = new_inode(sb);
	if (unlikely(!inode))
		goto failed;

	mapping_set_gfp_mask(inode->i_mapping,
			   mapping_gfp_constraint(inode->i_mapping, ~__GFP_FS));

	root = NILFS_I(dir)->i_root;
	ii = NILFS_I(inode);
	ii->i_state = 1 << NILFS_I_NEW;
	ii->i_root = root;

	err = nilfs_ifile_create_inode(root->ifile, &ino, &ii->i_bh);
	if (unlikely(err))
		goto failed_ifile_create_inode;
	/* reference count of i_bh inherits from nilfs_mdt_read_block() */

	atomic64_inc(&root->inodes_count);
	inode_init_owner(inode, dir, mode);
	inode->i_ino = ino;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) {
		err = nilfs_bmap_read(ii->i_bmap, NULL);
		if (err < 0)
			goto failed_after_creation;

		set_bit(NILFS_I_BMAP, &ii->i_state);
		/* No lock is needed; iget() ensures it. */
	}

	ii->i_flags = nilfs_mask_flags(
		mode, NILFS_I(dir)->i_flags & NILFS_FL_INHERITED);

	/* ii->i_file_acl = 0; */
	/* ii->i_dir_acl = 0; */
	ii->i_dir_start_lookup = 0;
	nilfs_set_inode_flags(inode);
	spin_lock(&nilfs->ns_next_gen_lock);
	inode->i_generation = nilfs->ns_next_generation++;
	spin_unlock(&nilfs->ns_next_gen_lock);
	if (nilfs_insert_inode_locked(inode, root, ino) < 0) {
		err = -EIO;
		goto failed_after_creation;
	}

	err = nilfs_init_acl(inode, dir);
	if (unlikely(err))
		goto failed_after_creation; /* never occur. When supporting
				    nilfs_init_acl(), proper cancellation of
				    above jobs should be considered */

	return inode;

 failed_after_creation:
	clear_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);  /* raw_inode will be deleted through
			 nilfs_evict_inode() */
	goto failed;

 failed_ifile_create_inode:
	make_bad_inode(inode);
	iput(inode);  /* if i_nlink == 1, generic_forget_inode() will be
			 called */
 failed:
	return ERR_PTR(err);
}

void nilfs_set_inode_flags(struct inode *inode)
{
	unsigned int flags = NILFS_I(inode)->i_flags;
	unsigned int new_fl = 0;

	if (flags & FS_SYNC_FL)
		new_fl |= S_SYNC;
	if (flags & FS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & FS_NOATIME_FL)
		new_fl |= S_NOATIME;
	if (flags & FS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	inode_set_flags(inode, new_fl, S_SYNC | S_APPEND | S_IMMUTABLE |
			S_NOATIME | S_DIRSYNC);
}

int nilfs_read_inode_common(struct inode *inode,
			    struct nilfs_inode *raw_inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	int err;

	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	i_uid_write(inode, le32_to_cpu(raw_inode->i_uid));
	i_gid_write(inode, le32_to_cpu(raw_inode->i_gid));
	set_nlink(inode, le16_to_cpu(raw_inode->i_links_count));
	inode->i_size = le64_to_cpu(raw_inode->i_size);
	inode->i_atime.tv_sec = le64_to_cpu(raw_inode->i_mtime);
	inode->i_ctime.tv_sec = le64_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = le64_to_cpu(raw_inode->i_mtime);
	inode->i_atime.tv_nsec = le32_to_cpu(raw_inode->i_mtime_nsec);
	inode->i_ctime.tv_nsec = le32_to_cpu(raw_inode->i_ctime_nsec);
	inode->i_mtime.tv_nsec = le32_to_cpu(raw_inode->i_mtime_nsec);
	if (inode->i_nlink == 0)
		return -ESTALE; /* this inode is deleted */

	inode->i_blocks = le64_to_cpu(raw_inode->i_blocks);
	ii->i_flags = le32_to_cpu(raw_inode->i_flags);
#if 0
	ii->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
	ii->i_dir_acl = S_ISREG(inode->i_mode) ?
		0 : le32_to_cpu(raw_inode->i_dir_acl);
#endif
	ii->i_dir_start_lookup = 0;
	inode->i_generation = le32_to_cpu(raw_inode->i_generation);

	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	    S_ISLNK(inode->i_mode)) {
		err = nilfs_bmap_read(ii->i_bmap, raw_inode);
		if (err < 0)
			return err;
		set_bit(NILFS_I_BMAP, &ii->i_state);
		/* No lock is needed; iget() ensures it. */
	}
	return 0;
}

static int __nilfs_read_inode(struct super_block *sb,
			      struct nilfs_root *root, unsigned long ino,
			      struct inode *inode)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct buffer_head *bh;
	struct nilfs_inode *raw_inode;
	int err;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	err = nilfs_ifile_get_inode_block(root->ifile, ino, &bh);
	if (unlikely(err))
		goto bad_inode;

	raw_inode = nilfs_ifile_map_inode(root->ifile, ino, bh);

	err = nilfs_read_inode_common(inode, raw_inode);
	if (err)
		goto failed_unmap;

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &nilfs_file_inode_operations;
		inode->i_fop = &nilfs_file_operations;
		inode->i_mapping->a_ops = &nilfs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &nilfs_dir_inode_operations;
		inode->i_fop = &nilfs_dir_operations;
		inode->i_mapping->a_ops = &nilfs_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &nilfs_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &nilfs_aops;
	} else {
		inode->i_op = &nilfs_special_inode_operations;
		init_special_inode(
			inode, inode->i_mode,
			huge_decode_dev(le64_to_cpu(raw_inode->i_device_code)));
	}
	nilfs_ifile_unmap_inode(root->ifile, ino, bh);
	brelse(bh);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	nilfs_set_inode_flags(inode);
	mapping_set_gfp_mask(inode->i_mapping,
			   mapping_gfp_constraint(inode->i_mapping, ~__GFP_FS));
	return 0;

 failed_unmap:
	nilfs_ifile_unmap_inode(root->ifile, ino, bh);
	brelse(bh);

 bad_inode:
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	return err;
}

static int nilfs_iget_test(struct inode *inode, void *opaque)
{
	struct nilfs_iget_args *args = opaque;
	struct nilfs_inode_info *ii;

	if (args->ino != inode->i_ino || args->root != NILFS_I(inode)->i_root)
		return 0;

	ii = NILFS_I(inode);
	if (!test_bit(NILFS_I_GCINODE, &ii->i_state))
		return !args->for_gc;

	return args->for_gc && args->cno == ii->i_cno;
}

static int nilfs_iget_set(struct inode *inode, void *opaque)
{
	struct nilfs_iget_args *args = opaque;

	inode->i_ino = args->ino;
	if (args->for_gc) {
		NILFS_I(inode)->i_state = 1 << NILFS_I_GCINODE;
		NILFS_I(inode)->i_cno = args->cno;
		NILFS_I(inode)->i_root = NULL;
	} else {
		if (args->root && args->ino == NILFS_ROOT_INO)
			nilfs_get_root(args->root);
		NILFS_I(inode)->i_root = args->root;
	}
	return 0;
}

struct inode *nilfs_ilookup(struct super_block *sb, struct nilfs_root *root,
			    unsigned long ino)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = root, .cno = 0, .for_gc = 0
	};

	return ilookup5(sb, ino, nilfs_iget_test, &args);
}

struct inode *nilfs_iget_locked(struct super_block *sb, struct nilfs_root *root,
				unsigned long ino)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = root, .cno = 0, .for_gc = 0
	};

	return iget5_locked(sb, ino, nilfs_iget_test, nilfs_iget_set, &args);
}

struct inode *nilfs_iget(struct super_block *sb, struct nilfs_root *root,
			 unsigned long ino)
{
	struct inode *inode;
	int err;

	inode = nilfs_iget_locked(sb, root, ino);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	err = __nilfs_read_inode(sb, root, ino, inode);
	if (unlikely(err)) {
		iget_failed(inode);
		return ERR_PTR(err);
	}
	unlock_new_inode(inode);
	return inode;
}

struct inode *nilfs_iget_for_gc(struct super_block *sb, unsigned long ino,
				__u64 cno)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = NULL, .cno = cno, .for_gc = 1
	};
	struct inode *inode;
	int err;

	inode = iget5_locked(sb, ino, nilfs_iget_test, nilfs_iget_set, &args);
	if (unlikely(!inode))
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	err = nilfs_init_gcinode(inode);
	if (unlikely(err)) {
		iget_failed(inode);
		return ERR_PTR(err);
	}
	unlock_new_inode(inode);
	return inode;
}

void nilfs_write_inode_common(struct inode *inode,
			      struct nilfs_inode *raw_inode, int has_bmap)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	raw_inode->i_uid = cpu_to_le32(i_uid_read(inode));
	raw_inode->i_gid = cpu_to_le32(i_gid_read(inode));
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
	raw_inode->i_size = cpu_to_le64(inode->i_size);
	raw_inode->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	raw_inode->i_mtime = cpu_to_le64(inode->i_mtime.tv_sec);
	raw_inode->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	raw_inode->i_mtime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);
	raw_inode->i_blocks = cpu_to_le64(inode->i_blocks);

	raw_inode->i_flags = cpu_to_le32(ii->i_flags);
	raw_inode->i_generation = cpu_to_le32(inode->i_generation);

	if (NILFS_ROOT_METADATA_FILE(inode->i_ino)) {
		struct the_nilfs *nilfs = inode->i_sb->s_fs_info;

		/* zero-fill unused portion in the case of super root block */
		raw_inode->i_xattr = 0;
		raw_inode->i_pad = 0;
		memset((void *)raw_inode + sizeof(*raw_inode), 0,
		       nilfs->ns_inode_size - sizeof(*raw_inode));
	}

	if (has_bmap)
		nilfs_bmap_write(ii->i_bmap, raw_inode);
	else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_device_code =
			cpu_to_le64(huge_encode_dev(inode->i_rdev));
	/* When extending inode, nilfs->ns_inode_size should be checked
	   for substitutions of appended fields */
}

void nilfs_update_inode(struct inode *inode, struct buffer_head *ibh, int flags)
{
	ino_t ino = inode->i_ino;
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct inode *ifile = ii->i_root->ifile;
	struct nilfs_inode *raw_inode;

	raw_inode = nilfs_ifile_map_inode(ifile, ino, ibh);

	if (test_and_clear_bit(NILFS_I_NEW, &ii->i_state))
		memset(raw_inode, 0, NILFS_MDT(ifile)->mi_entry_size);
	if (flags & I_DIRTY_DATASYNC)
		set_bit(NILFS_I_INODE_SYNC, &ii->i_state);

	nilfs_write_inode_common(inode, raw_inode, 0);
		/* XXX: call with has_bmap = 0 is a workaround to avoid
		   deadlock of bmap. This delays update of i_bmap to just
		   before writing */
	nilfs_ifile_unmap_inode(ifile, ino, ibh);
}

#define NILFS_MAX_TRUNCATE_BLOCKS	16384  /* 64MB for 4KB block */

static void nilfs_truncate_bmap(struct nilfs_inode_info *ii,
				unsigned long from)
{
	__u64 b;
	int ret;

	if (!test_bit(NILFS_I_BMAP, &ii->i_state))
		return;
repeat:
	ret = nilfs_bmap_last_key(ii->i_bmap, &b);
	if (ret == -ENOENT)
		return;
	else if (ret < 0)
		goto failed;

	if (b < from)
		return;

	b -= min_t(__u64, NILFS_MAX_TRUNCATE_BLOCKS, b - from);
	ret = nilfs_bmap_truncate(ii->i_bmap, b);
	nilfs_relax_pressure_in_lock(ii->vfs_inode.i_sb);
	if (!ret || (ret == -ENOMEM &&
		     nilfs_bmap_truncate(ii->i_bmap, b) == 0))
		goto repeat;

failed:
	nilfs_warning(ii->vfs_inode.i_sb, __func__,
		      "failed to truncate bmap (ino=%lu, err=%d)",
		      ii->vfs_inode.i_ino, ret);
}

void nilfs_truncate(struct inode *inode)
{
	unsigned long blkoff;
	unsigned int blocksize;
	struct nilfs_transaction_info ti;
	struct super_block *sb = inode->i_sb;
	struct nilfs_inode_info *ii = NILFS_I(inode);

	if (!test_bit(NILFS_I_BMAP, &ii->i_state))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	blocksize = sb->s_blocksize;
	blkoff = (inode->i_size + blocksize - 1) >> sb->s_blocksize_bits;
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	block_truncate_page(inode->i_mapping, inode->i_size, nilfs_get_block);

	nilfs_truncate_bmap(ii, blkoff);

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	if (IS_SYNC(inode))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);

	nilfs_mark_inode_dirty(inode);
	nilfs_set_file_dirty(inode, 0);
	nilfs_transaction_commit(sb);
	/* May construct a logical segment and may fail in sync mode.
	   But truncate has no return value. */
}

static void nilfs_clear_inode(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

	/*
	 * Free resources allocated in nilfs_read_inode(), here.
	 */
	BUG_ON(!list_empty(&ii->i_dirty));
	brelse(ii->i_bh);
	ii->i_bh = NULL;

	if (nilfs_is_metadata_file_inode(inode))
		nilfs_mdt_clear(inode);

	if (test_bit(NILFS_I_BMAP, &ii->i_state))
		nilfs_bmap_clear(ii->i_bmap);

	nilfs_btnode_cache_clear(&ii->i_btnode_cache);

	if (ii->i_root && inode->i_ino == NILFS_ROOT_INO)
		nilfs_put_root(ii->i_root);
}

void nilfs_evict_inode(struct inode *inode)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = inode->i_sb;
	struct nilfs_inode_info *ii = NILFS_I(inode);
	int ret;

	if (inode->i_nlink || !ii->i_root || unlikely(is_bad_inode(inode))) {
		truncate_inode_pages_final(&inode->i_data);
		clear_inode(inode);
		nilfs_clear_inode(inode);
		return;
	}
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	truncate_inode_pages_final(&inode->i_data);

	/* TODO: some of the following operations may fail.  */
	nilfs_truncate_bmap(ii, 0);
	nilfs_mark_inode_dirty(inode);
	clear_inode(inode);

	ret = nilfs_ifile_delete_inode(ii->i_root->ifile, inode->i_ino);
	if (!ret)
		atomic64_dec(&ii->i_root->inodes_count);

	nilfs_clear_inode(inode);

	if (IS_SYNC(inode))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);
	nilfs_transaction_commit(sb);
	/* May construct a logical segment and may fail in sync mode.
	   But delete_inode has no return value. */
}

int nilfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct nilfs_transaction_info ti;
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	int err;

	err = inode_change_ok(inode, iattr);
	if (err)
		return err;

	err = nilfs_transaction_begin(sb, &ti, 0);
	if (unlikely(err))
		return err;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(inode)) {
		inode_dio_wait(inode);
		truncate_setsize(inode, iattr->ia_size);
		nilfs_truncate(inode);
	}

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);

	if (iattr->ia_valid & ATTR_MODE) {
		err = nilfs_acl_chmod(inode);
		if (unlikely(err))
			goto out_err;
	}

	return nilfs_transaction_commit(sb);

out_err:
	nilfs_transaction_abort(sb);
	return err;
}

int nilfs_permission(struct inode *inode, int mask)
{
	struct nilfs_root *root = NILFS_I(inode)->i_root;

	if ((mask & MAY_WRITE) && root &&
	    root->cno != NILFS_CPTREE_CURRENT_CNO)
		return -EROFS; /* snapshot is not writable */

	return generic_permission(inode, mask);
}

int nilfs_load_inode_block(struct inode *inode, struct buffer_head **pbh)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_inode_info *ii = NILFS_I(inode);
	int err;

	spin_lock(&nilfs->ns_inode_lock);
	if (ii->i_bh == NULL) {
		spin_unlock(&nilfs->ns_inode_lock);
		err = nilfs_ifile_get_inode_block(ii->i_root->ifile,
						  inode->i_ino, pbh);
		if (unlikely(err))
			return err;
		spin_lock(&nilfs->ns_inode_lock);
		if (ii->i_bh == NULL)
			ii->i_bh = *pbh;
		else {
			brelse(*pbh);
			*pbh = ii->i_bh;
		}
	} else
		*pbh = ii->i_bh;

	get_bh(*pbh);
	spin_unlock(&nilfs->ns_inode_lock);
	return 0;
}

int nilfs_inode_dirty(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	int ret = 0;

	if (!list_empty(&ii->i_dirty)) {
		spin_lock(&nilfs->ns_inode_lock);
		ret = test_bit(NILFS_I_DIRTY, &ii->i_state) ||
			test_bit(NILFS_I_BUSY, &ii->i_state);
		spin_unlock(&nilfs->ns_inode_lock);
	}
	return ret;
}

int nilfs_set_file_dirty(struct inode *inode, unsigned nr_dirty)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;

	atomic_add(nr_dirty, &nilfs->ns_ndirtyblks);

	if (test_and_set_bit(NILFS_I_DIRTY, &ii->i_state))
		return 0;

	spin_lock(&nilfs->ns_inode_lock);
	if (!test_bit(NILFS_I_QUEUED, &ii->i_state) &&
	    !test_bit(NILFS_I_BUSY, &ii->i_state)) {
		/* Because this routine may race with nilfs_dispose_list(),
		   we have to check NILFS_I_QUEUED here, too. */
		if (list_empty(&ii->i_dirty) && igrab(inode) == NULL) {
			/* This will happen when somebody is freeing
			   this inode. */
			nilfs_warning(inode->i_sb, __func__,
				      "cannot get inode (ino=%lu)",
				      inode->i_ino);
			spin_unlock(&nilfs->ns_inode_lock);
			return -EINVAL; /* NILFS_I_DIRTY may remain for
					   freeing inode */
		}
		list_move_tail(&ii->i_dirty, &nilfs->ns_dirty_files);
		set_bit(NILFS_I_QUEUED, &ii->i_state);
	}
	spin_unlock(&nilfs->ns_inode_lock);
	return 0;
}

int __nilfs_mark_inode_dirty(struct inode *inode, int flags)
{
	struct buffer_head *ibh;
	int err;

	err = nilfs_load_inode_block(inode, &ibh);
	if (unlikely(err)) {
		nilfs_warning(inode->i_sb, __func__,
			      "failed to reget inode block.");
		return err;
	}
	nilfs_update_inode(inode, ibh, flags);
	mark_buffer_dirty(ibh);
	nilfs_mdt_mark_dirty(NILFS_I(inode)->i_root->ifile);
	brelse(ibh);
	return 0;
}

/**
 * nilfs_dirty_inode - reflect changes on given inode to an inode block.
 * @inode: inode of the file to be registered.
 *
 * nilfs_dirty_inode() loads a inode block containing the specified
 * @inode and copies data from a nilfs_inode to a corresponding inode
 * entry in the inode block. This operation is excluded from the segment
 * construction. This function can be called both as a single operation
 * and as a part of indivisible file operations.
 */
void nilfs_dirty_inode(struct inode *inode, int flags)
{
	struct nilfs_transaction_info ti;
	struct nilfs_mdt_info *mdi = NILFS_MDT(inode);

	if (is_bad_inode(inode)) {
		nilfs_warning(inode->i_sb, __func__,
			      "tried to mark bad_inode dirty. ignored.");
		dump_stack();
		return;
	}
	if (mdi) {
		nilfs_mdt_mark_dirty(inode);
		return;
	}
	nilfs_transaction_begin(inode->i_sb, &ti, 0);
	__nilfs_mark_inode_dirty(inode, flags);
	nilfs_transaction_commit(inode->i_sb); /* never fails */
}

int nilfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 __u64 start, __u64 len)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	__u64 logical = 0, phys = 0, size = 0;
	__u32 flags = 0;
	loff_t isize;
	sector_t blkoff, end_blkoff;
	sector_t delalloc_blkoff;
	unsigned long delalloc_blklen;
	unsigned int blkbits = inode->i_blkbits;
	int ret, n;

	ret = fiemap_check_flags(fieinfo, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	inode_lock(inode);

	isize = i_size_read(inode);

	blkoff = start >> blkbits;
	end_blkoff = (start + len - 1) >> blkbits;

	delalloc_blklen = nilfs_find_uncommitted_extent(inode, blkoff,
							&delalloc_blkoff);

	do {
		__u64 blkphy;
		unsigned int maxblocks;

		if (delalloc_blklen && blkoff == delalloc_blkoff) {
			if (size) {
				/* End of the current extent */
				ret = fiemap_fill_next_extent(
					fieinfo, logical, phys, size, flags);
				if (ret)
					break;
			}
			if (blkoff > end_blkoff)
				break;

			flags = FIEMAP_EXTENT_MERGED | FIEMAP_EXTENT_DELALLOC;
			logical = blkoff << blkbits;
			phys = 0;
			size = delalloc_blklen << blkbits;

			blkoff = delalloc_blkoff + delalloc_blklen;
			delalloc_blklen = nilfs_find_uncommitted_extent(
				inode, blkoff, &delalloc_blkoff);
			continue;
		}

		/*
		 * Limit the number of blocks that we look up so as
		 * not to get into the next delayed allocation extent.
		 */
		maxblocks = INT_MAX;
		if (delalloc_blklen)
			maxblocks = min_t(sector_t, delalloc_blkoff - blkoff,
					  maxblocks);
		blkphy = 0;

		down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
		n = nilfs_bmap_lookup_contig(
			NILFS_I(inode)->i_bmap, blkoff, &blkphy, maxblocks);
		up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);

		if (n < 0) {
			int past_eof;

			if (unlikely(n != -ENOENT))
				break; /* error */

			/* HOLE */
			blkoff++;
			past_eof = ((blkoff << blkbits) >= isize);

			if (size) {
				/* End of the current extent */

				if (past_eof)
					flags |= FIEMAP_EXTENT_LAST;

				ret = fiemap_fill_next_extent(
					fieinfo, logical, phys, size, flags);
				if (ret)
					break;
				size = 0;
			}
			if (blkoff > end_blkoff || past_eof)
				break;
		} else {
			if (size) {
				if (phys && blkphy << blkbits == phys + size) {
					/* The current extent goes on */
					size += n << blkbits;
				} else {
					/* Terminate the current extent */
					ret = fiemap_fill_next_extent(
						fieinfo, logical, phys, size,
						flags);
					if (ret || blkoff > end_blkoff)
						break;

					/* Start another extent */
					flags = FIEMAP_EXTENT_MERGED;
					logical = blkoff << blkbits;
					phys = blkphy << blkbits;
					size = n << blkbits;
				}
			} else {
				/* Start a new extent */
				flags = FIEMAP_EXTENT_MERGED;
				logical = blkoff << blkbits;
				phys = blkphy << blkbits;
				size = n << blkbits;
			}
			blkoff += n;
		}
		cond_resched();
	} while (true);

	/* If ret is 1 then we just hit the end of the extent array */
	if (ret == 1)
		ret = 0;

	inode_unlock(inode);
	return ret;
}
