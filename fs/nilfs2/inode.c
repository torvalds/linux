// SPDX-License-Identifier: GPL-2.0+
/*
 * iyesde.c - NILFS iyesde operations.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
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
#include "btyesde.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"
#include "cpfile.h"
#include "ifile.h"

/**
 * struct nilfs_iget_args - arguments used during comparison between iyesdes
 * @iyes: iyesde number
 * @cyes: checkpoint number
 * @root: pointer on NILFS root object (mounted checkpoint)
 * @for_gc: iyesde for GC flag
 */
struct nilfs_iget_args {
	u64 iyes;
	__u64 cyes;
	struct nilfs_root *root;
	int for_gc;
};

static int nilfs_iget_test(struct iyesde *iyesde, void *opaque);

void nilfs_iyesde_add_blocks(struct iyesde *iyesde, int n)
{
	struct nilfs_root *root = NILFS_I(iyesde)->i_root;

	iyesde_add_bytes(iyesde, i_blocksize(iyesde) * n);
	if (root)
		atomic64_add(n, &root->blocks_count);
}

void nilfs_iyesde_sub_blocks(struct iyesde *iyesde, int n)
{
	struct nilfs_root *root = NILFS_I(iyesde)->i_root;

	iyesde_sub_bytes(iyesde, i_blocksize(iyesde) * n);
	if (root)
		atomic64_sub(n, &root->blocks_count);
}

/**
 * nilfs_get_block() - get a file block on the filesystem (callback function)
 * @iyesde - iyesde struct of the target file
 * @blkoff - file block number
 * @bh_result - buffer head to be mapped on
 * @create - indicate whether allocating the block or yest when it has yest
 *      been allocated yet.
 *
 * This function does yest issue actual read request of the specified data
 * block. It is done by VFS.
 */
int nilfs_get_block(struct iyesde *iyesde, sector_t blkoff,
		    struct buffer_head *bh_result, int create)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;
	__u64 blknum = 0;
	int err = 0, ret;
	unsigned int maxblocks = bh_result->b_size >> iyesde->i_blkbits;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	ret = nilfs_bmap_lookup_contig(ii->i_bmap, blkoff, &blknum, maxblocks);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	if (ret >= 0) {	/* found */
		map_bh(bh_result, iyesde->i_sb, blknum);
		if (ret > 0)
			bh_result->b_size = (ret << iyesde->i_blkbits);
		goto out;
	}
	/* data block was yest found */
	if (ret == -ENOENT && create) {
		struct nilfs_transaction_info ti;

		bh_result->b_blocknr = 0;
		err = nilfs_transaction_begin(iyesde->i_sb, &ti, 1);
		if (unlikely(err))
			goto out;
		err = nilfs_bmap_insert(ii->i_bmap, blkoff,
					(unsigned long)bh_result);
		if (unlikely(err != 0)) {
			if (err == -EEXIST) {
				/*
				 * The get_block() function could be called
				 * from multiple callers for an iyesde.
				 * However, the page having this block must
				 * be locked in this case.
				 */
				nilfs_msg(iyesde->i_sb, KERN_WARNING,
					  "%s (iyes=%lu): a race condition while inserting a data block at offset=%llu",
					  __func__, iyesde->i_iyes,
					  (unsigned long long)blkoff);
				err = 0;
			}
			nilfs_transaction_abort(iyesde->i_sb);
			goto out;
		}
		nilfs_mark_iyesde_dirty_sync(iyesde);
		nilfs_transaction_commit(iyesde->i_sb); /* never fails */
		/* Error handling should be detailed */
		set_buffer_new(bh_result);
		set_buffer_delay(bh_result);
		map_bh(bh_result, iyesde->i_sb, 0);
		/* Disk block number must be changed to proper value */

	} else if (ret == -ENOENT) {
		/*
		 * yest found is yest error (e.g. hole); must return without
		 * the mapped state flag.
		 */
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
			   struct list_head *pages, unsigned int nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, nilfs_get_block);
}

static int nilfs_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct iyesde *iyesde = mapping->host;
	int err = 0;

	if (sb_rdonly(iyesde->i_sb)) {
		nilfs_clear_dirty_pages(mapping, false);
		return -EROFS;
	}

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_dsync_segment(iyesde->i_sb, iyesde,
						    wbc->range_start,
						    wbc->range_end);
	return err;
}

static int nilfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct iyesde *iyesde = page->mapping->host;
	int err;

	if (sb_rdonly(iyesde->i_sb)) {
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
		err = nilfs_construct_segment(iyesde->i_sb);
		if (unlikely(err))
			return err;
	} else if (wbc->for_reclaim)
		nilfs_flush_segment(iyesde->i_sb, iyesde->i_iyes);

	return 0;
}

static int nilfs_set_page_dirty(struct page *page)
{
	struct iyesde *iyesde = page->mapping->host;
	int ret = __set_page_dirty_yesbuffers(page);

	if (page_has_buffers(page)) {
		unsigned int nr_dirty = 0;
		struct buffer_head *bh, *head;

		/*
		 * This page is locked by callers, and yes other thread
		 * concurrently marks its buffers dirty since they are
		 * only dirtied through routines in fs/buffer.c in
		 * which call sites of mark_buffer_dirty are protected
		 * by page lock.
		 */
		bh = head = page_buffers(page);
		do {
			/* Do yest mark hole blocks dirty */
			if (buffer_dirty(bh) || !buffer_mapped(bh))
				continue;

			set_buffer_dirty(bh);
			nr_dirty++;
		} while (bh = bh->b_this_page, bh != head);

		if (nr_dirty)
			nilfs_set_file_dirty(iyesde, nr_dirty);
	} else if (ret) {
		unsigned int nr_dirty = 1 << (PAGE_SHIFT - iyesde->i_blkbits);

		nilfs_set_file_dirty(iyesde, nr_dirty);
	}
	return ret;
}

void nilfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		nilfs_truncate(iyesde);
	}
}

static int nilfs_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len, unsigned flags,
			     struct page **pagep, void **fsdata)

{
	struct iyesde *iyesde = mapping->host;
	int err = nilfs_transaction_begin(iyesde->i_sb, NULL, 1);

	if (unlikely(err))
		return err;

	err = block_write_begin(mapping, pos, len, flags, pagep,
				nilfs_get_block);
	if (unlikely(err)) {
		nilfs_write_failed(mapping, pos + len);
		nilfs_transaction_abort(iyesde->i_sb);
	}
	return err;
}

static int nilfs_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata)
{
	struct iyesde *iyesde = mapping->host;
	unsigned int start = pos & (PAGE_SIZE - 1);
	unsigned int nr_dirty;
	int err;

	nr_dirty = nilfs_page_count_clean_buffers(page, start,
						  start + copied);
	copied = generic_write_end(file, mapping, pos, len, copied, page,
				   fsdata);
	nilfs_set_file_dirty(iyesde, nr_dirty);
	err = nilfs_transaction_commit(iyesde->i_sb);
	return err ? : copied;
}

static ssize_t
nilfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (iov_iter_rw(iter) == WRITE)
		return 0;

	/* Needs synchronization with the cleaner */
	return blockdev_direct_IO(iocb, iyesde, iter, nilfs_get_block);
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

static int nilfs_insert_iyesde_locked(struct iyesde *iyesde,
				     struct nilfs_root *root,
				     unsigned long iyes)
{
	struct nilfs_iget_args args = {
		.iyes = iyes, .root = root, .cyes = 0, .for_gc = 0
	};

	return insert_iyesde_locked4(iyesde, iyes, nilfs_iget_test, &args);
}

struct iyesde *nilfs_new_iyesde(struct iyesde *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct iyesde *iyesde;
	struct nilfs_iyesde_info *ii;
	struct nilfs_root *root;
	int err = -ENOMEM;
	iyes_t iyes;

	iyesde = new_iyesde(sb);
	if (unlikely(!iyesde))
		goto failed;

	mapping_set_gfp_mask(iyesde->i_mapping,
			   mapping_gfp_constraint(iyesde->i_mapping, ~__GFP_FS));

	root = NILFS_I(dir)->i_root;
	ii = NILFS_I(iyesde);
	ii->i_state = BIT(NILFS_I_NEW);
	ii->i_root = root;

	err = nilfs_ifile_create_iyesde(root->ifile, &iyes, &ii->i_bh);
	if (unlikely(err))
		goto failed_ifile_create_iyesde;
	/* reference count of i_bh inherits from nilfs_mdt_read_block() */

	atomic64_inc(&root->iyesdes_count);
	iyesde_init_owner(iyesde, dir, mode);
	iyesde->i_iyes = iyes;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);

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
	nilfs_set_iyesde_flags(iyesde);
	spin_lock(&nilfs->ns_next_gen_lock);
	iyesde->i_generation = nilfs->ns_next_generation++;
	spin_unlock(&nilfs->ns_next_gen_lock);
	if (nilfs_insert_iyesde_locked(iyesde, root, iyes) < 0) {
		err = -EIO;
		goto failed_after_creation;
	}

	err = nilfs_init_acl(iyesde, dir);
	if (unlikely(err))
		/*
		 * Never occur.  When supporting nilfs_init_acl(),
		 * proper cancellation of above jobs should be considered.
		 */
		goto failed_after_creation;

	return iyesde;

 failed_after_creation:
	clear_nlink(iyesde);
	unlock_new_iyesde(iyesde);
	iput(iyesde);  /*
		       * raw_iyesde will be deleted through
		       * nilfs_evict_iyesde().
		       */
	goto failed;

 failed_ifile_create_iyesde:
	make_bad_iyesde(iyesde);
	iput(iyesde);
 failed:
	return ERR_PTR(err);
}

void nilfs_set_iyesde_flags(struct iyesde *iyesde)
{
	unsigned int flags = NILFS_I(iyesde)->i_flags;
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
	iyesde_set_flags(iyesde, new_fl, S_SYNC | S_APPEND | S_IMMUTABLE |
			S_NOATIME | S_DIRSYNC);
}

int nilfs_read_iyesde_common(struct iyesde *iyesde,
			    struct nilfs_iyesde *raw_iyesde)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	int err;

	iyesde->i_mode = le16_to_cpu(raw_iyesde->i_mode);
	i_uid_write(iyesde, le32_to_cpu(raw_iyesde->i_uid));
	i_gid_write(iyesde, le32_to_cpu(raw_iyesde->i_gid));
	set_nlink(iyesde, le16_to_cpu(raw_iyesde->i_links_count));
	iyesde->i_size = le64_to_cpu(raw_iyesde->i_size);
	iyesde->i_atime.tv_sec = le64_to_cpu(raw_iyesde->i_mtime);
	iyesde->i_ctime.tv_sec = le64_to_cpu(raw_iyesde->i_ctime);
	iyesde->i_mtime.tv_sec = le64_to_cpu(raw_iyesde->i_mtime);
	iyesde->i_atime.tv_nsec = le32_to_cpu(raw_iyesde->i_mtime_nsec);
	iyesde->i_ctime.tv_nsec = le32_to_cpu(raw_iyesde->i_ctime_nsec);
	iyesde->i_mtime.tv_nsec = le32_to_cpu(raw_iyesde->i_mtime_nsec);
	if (iyesde->i_nlink == 0)
		return -ESTALE; /* this iyesde is deleted */

	iyesde->i_blocks = le64_to_cpu(raw_iyesde->i_blocks);
	ii->i_flags = le32_to_cpu(raw_iyesde->i_flags);
#if 0
	ii->i_file_acl = le32_to_cpu(raw_iyesde->i_file_acl);
	ii->i_dir_acl = S_ISREG(iyesde->i_mode) ?
		0 : le32_to_cpu(raw_iyesde->i_dir_acl);
#endif
	ii->i_dir_start_lookup = 0;
	iyesde->i_generation = le32_to_cpu(raw_iyesde->i_generation);

	if (S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode) ||
	    S_ISLNK(iyesde->i_mode)) {
		err = nilfs_bmap_read(ii->i_bmap, raw_iyesde);
		if (err < 0)
			return err;
		set_bit(NILFS_I_BMAP, &ii->i_state);
		/* No lock is needed; iget() ensures it. */
	}
	return 0;
}

static int __nilfs_read_iyesde(struct super_block *sb,
			      struct nilfs_root *root, unsigned long iyes,
			      struct iyesde *iyesde)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct buffer_head *bh;
	struct nilfs_iyesde *raw_iyesde;
	int err;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	err = nilfs_ifile_get_iyesde_block(root->ifile, iyes, &bh);
	if (unlikely(err))
		goto bad_iyesde;

	raw_iyesde = nilfs_ifile_map_iyesde(root->ifile, iyes, bh);

	err = nilfs_read_iyesde_common(iyesde, raw_iyesde);
	if (err)
		goto failed_unmap;

	if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_op = &nilfs_file_iyesde_operations;
		iyesde->i_fop = &nilfs_file_operations;
		iyesde->i_mapping->a_ops = &nilfs_aops;
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &nilfs_dir_iyesde_operations;
		iyesde->i_fop = &nilfs_dir_operations;
		iyesde->i_mapping->a_ops = &nilfs_aops;
	} else if (S_ISLNK(iyesde->i_mode)) {
		iyesde->i_op = &nilfs_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &nilfs_aops;
	} else {
		iyesde->i_op = &nilfs_special_iyesde_operations;
		init_special_iyesde(
			iyesde, iyesde->i_mode,
			huge_decode_dev(le64_to_cpu(raw_iyesde->i_device_code)));
	}
	nilfs_ifile_unmap_iyesde(root->ifile, iyes, bh);
	brelse(bh);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	nilfs_set_iyesde_flags(iyesde);
	mapping_set_gfp_mask(iyesde->i_mapping,
			   mapping_gfp_constraint(iyesde->i_mapping, ~__GFP_FS));
	return 0;

 failed_unmap:
	nilfs_ifile_unmap_iyesde(root->ifile, iyes, bh);
	brelse(bh);

 bad_iyesde:
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	return err;
}

static int nilfs_iget_test(struct iyesde *iyesde, void *opaque)
{
	struct nilfs_iget_args *args = opaque;
	struct nilfs_iyesde_info *ii;

	if (args->iyes != iyesde->i_iyes || args->root != NILFS_I(iyesde)->i_root)
		return 0;

	ii = NILFS_I(iyesde);
	if (!test_bit(NILFS_I_GCINODE, &ii->i_state))
		return !args->for_gc;

	return args->for_gc && args->cyes == ii->i_cyes;
}

static int nilfs_iget_set(struct iyesde *iyesde, void *opaque)
{
	struct nilfs_iget_args *args = opaque;

	iyesde->i_iyes = args->iyes;
	if (args->for_gc) {
		NILFS_I(iyesde)->i_state = BIT(NILFS_I_GCINODE);
		NILFS_I(iyesde)->i_cyes = args->cyes;
		NILFS_I(iyesde)->i_root = NULL;
	} else {
		if (args->root && args->iyes == NILFS_ROOT_INO)
			nilfs_get_root(args->root);
		NILFS_I(iyesde)->i_root = args->root;
	}
	return 0;
}

struct iyesde *nilfs_ilookup(struct super_block *sb, struct nilfs_root *root,
			    unsigned long iyes)
{
	struct nilfs_iget_args args = {
		.iyes = iyes, .root = root, .cyes = 0, .for_gc = 0
	};

	return ilookup5(sb, iyes, nilfs_iget_test, &args);
}

struct iyesde *nilfs_iget_locked(struct super_block *sb, struct nilfs_root *root,
				unsigned long iyes)
{
	struct nilfs_iget_args args = {
		.iyes = iyes, .root = root, .cyes = 0, .for_gc = 0
	};

	return iget5_locked(sb, iyes, nilfs_iget_test, nilfs_iget_set, &args);
}

struct iyesde *nilfs_iget(struct super_block *sb, struct nilfs_root *root,
			 unsigned long iyes)
{
	struct iyesde *iyesde;
	int err;

	iyesde = nilfs_iget_locked(sb, root, iyes);
	if (unlikely(!iyesde))
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	err = __nilfs_read_iyesde(sb, root, iyes, iyesde);
	if (unlikely(err)) {
		iget_failed(iyesde);
		return ERR_PTR(err);
	}
	unlock_new_iyesde(iyesde);
	return iyesde;
}

struct iyesde *nilfs_iget_for_gc(struct super_block *sb, unsigned long iyes,
				__u64 cyes)
{
	struct nilfs_iget_args args = {
		.iyes = iyes, .root = NULL, .cyes = cyes, .for_gc = 1
	};
	struct iyesde *iyesde;
	int err;

	iyesde = iget5_locked(sb, iyes, nilfs_iget_test, nilfs_iget_set, &args);
	if (unlikely(!iyesde))
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	err = nilfs_init_gciyesde(iyesde);
	if (unlikely(err)) {
		iget_failed(iyesde);
		return ERR_PTR(err);
	}
	unlock_new_iyesde(iyesde);
	return iyesde;
}

void nilfs_write_iyesde_common(struct iyesde *iyesde,
			      struct nilfs_iyesde *raw_iyesde, int has_bmap)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);

	raw_iyesde->i_mode = cpu_to_le16(iyesde->i_mode);
	raw_iyesde->i_uid = cpu_to_le32(i_uid_read(iyesde));
	raw_iyesde->i_gid = cpu_to_le32(i_gid_read(iyesde));
	raw_iyesde->i_links_count = cpu_to_le16(iyesde->i_nlink);
	raw_iyesde->i_size = cpu_to_le64(iyesde->i_size);
	raw_iyesde->i_ctime = cpu_to_le64(iyesde->i_ctime.tv_sec);
	raw_iyesde->i_mtime = cpu_to_le64(iyesde->i_mtime.tv_sec);
	raw_iyesde->i_ctime_nsec = cpu_to_le32(iyesde->i_ctime.tv_nsec);
	raw_iyesde->i_mtime_nsec = cpu_to_le32(iyesde->i_mtime.tv_nsec);
	raw_iyesde->i_blocks = cpu_to_le64(iyesde->i_blocks);

	raw_iyesde->i_flags = cpu_to_le32(ii->i_flags);
	raw_iyesde->i_generation = cpu_to_le32(iyesde->i_generation);

	if (NILFS_ROOT_METADATA_FILE(iyesde->i_iyes)) {
		struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;

		/* zero-fill unused portion in the case of super root block */
		raw_iyesde->i_xattr = 0;
		raw_iyesde->i_pad = 0;
		memset((void *)raw_iyesde + sizeof(*raw_iyesde), 0,
		       nilfs->ns_iyesde_size - sizeof(*raw_iyesde));
	}

	if (has_bmap)
		nilfs_bmap_write(ii->i_bmap, raw_iyesde);
	else if (S_ISCHR(iyesde->i_mode) || S_ISBLK(iyesde->i_mode))
		raw_iyesde->i_device_code =
			cpu_to_le64(huge_encode_dev(iyesde->i_rdev));
	/*
	 * When extending iyesde, nilfs->ns_iyesde_size should be checked
	 * for substitutions of appended fields.
	 */
}

void nilfs_update_iyesde(struct iyesde *iyesde, struct buffer_head *ibh, int flags)
{
	iyes_t iyes = iyesde->i_iyes;
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	struct iyesde *ifile = ii->i_root->ifile;
	struct nilfs_iyesde *raw_iyesde;

	raw_iyesde = nilfs_ifile_map_iyesde(ifile, iyes, ibh);

	if (test_and_clear_bit(NILFS_I_NEW, &ii->i_state))
		memset(raw_iyesde, 0, NILFS_MDT(ifile)->mi_entry_size);
	if (flags & I_DIRTY_DATASYNC)
		set_bit(NILFS_I_INODE_SYNC, &ii->i_state);

	nilfs_write_iyesde_common(iyesde, raw_iyesde, 0);
		/*
		 * XXX: call with has_bmap = 0 is a workaround to avoid
		 * deadlock of bmap.  This delays update of i_bmap to just
		 * before writing.
		 */

	nilfs_ifile_unmap_iyesde(ifile, iyes, ibh);
}

#define NILFS_MAX_TRUNCATE_BLOCKS	16384  /* 64MB for 4KB block */

static void nilfs_truncate_bmap(struct nilfs_iyesde_info *ii,
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
	nilfs_relax_pressure_in_lock(ii->vfs_iyesde.i_sb);
	if (!ret || (ret == -ENOMEM &&
		     nilfs_bmap_truncate(ii->i_bmap, b) == 0))
		goto repeat;

failed:
	nilfs_msg(ii->vfs_iyesde.i_sb, KERN_WARNING,
		  "error %d truncating bmap (iyes=%lu)", ret,
		  ii->vfs_iyesde.i_iyes);
}

void nilfs_truncate(struct iyesde *iyesde)
{
	unsigned long blkoff;
	unsigned int blocksize;
	struct nilfs_transaction_info ti;
	struct super_block *sb = iyesde->i_sb;
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);

	if (!test_bit(NILFS_I_BMAP, &ii->i_state))
		return;
	if (IS_APPEND(iyesde) || IS_IMMUTABLE(iyesde))
		return;

	blocksize = sb->s_blocksize;
	blkoff = (iyesde->i_size + blocksize - 1) >> sb->s_blocksize_bits;
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	block_truncate_page(iyesde->i_mapping, iyesde->i_size, nilfs_get_block);

	nilfs_truncate_bmap(ii, blkoff);

	iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	if (IS_SYNC(iyesde))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);

	nilfs_mark_iyesde_dirty(iyesde);
	nilfs_set_file_dirty(iyesde, 0);
	nilfs_transaction_commit(sb);
	/*
	 * May construct a logical segment and may fail in sync mode.
	 * But truncate has yes return value.
	 */
}

static void nilfs_clear_iyesde(struct iyesde *iyesde)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);

	/*
	 * Free resources allocated in nilfs_read_iyesde(), here.
	 */
	BUG_ON(!list_empty(&ii->i_dirty));
	brelse(ii->i_bh);
	ii->i_bh = NULL;

	if (nilfs_is_metadata_file_iyesde(iyesde))
		nilfs_mdt_clear(iyesde);

	if (test_bit(NILFS_I_BMAP, &ii->i_state))
		nilfs_bmap_clear(ii->i_bmap);

	nilfs_btyesde_cache_clear(&ii->i_btyesde_cache);

	if (ii->i_root && iyesde->i_iyes == NILFS_ROOT_INO)
		nilfs_put_root(ii->i_root);
}

void nilfs_evict_iyesde(struct iyesde *iyesde)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = iyesde->i_sb;
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	int ret;

	if (iyesde->i_nlink || !ii->i_root || unlikely(is_bad_iyesde(iyesde))) {
		truncate_iyesde_pages_final(&iyesde->i_data);
		clear_iyesde(iyesde);
		nilfs_clear_iyesde(iyesde);
		return;
	}
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	truncate_iyesde_pages_final(&iyesde->i_data);

	/* TODO: some of the following operations may fail.  */
	nilfs_truncate_bmap(ii, 0);
	nilfs_mark_iyesde_dirty(iyesde);
	clear_iyesde(iyesde);

	ret = nilfs_ifile_delete_iyesde(ii->i_root->ifile, iyesde->i_iyes);
	if (!ret)
		atomic64_dec(&ii->i_root->iyesdes_count);

	nilfs_clear_iyesde(iyesde);

	if (IS_SYNC(iyesde))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);
	nilfs_transaction_commit(sb);
	/*
	 * May construct a logical segment and may fail in sync mode.
	 * But delete_iyesde has yes return value.
	 */
}

int nilfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct nilfs_transaction_info ti;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct super_block *sb = iyesde->i_sb;
	int err;

	err = setattr_prepare(dentry, iattr);
	if (err)
		return err;

	err = nilfs_transaction_begin(sb, &ti, 0);
	if (unlikely(err))
		return err;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(iyesde)) {
		iyesde_dio_wait(iyesde);
		truncate_setsize(iyesde, iattr->ia_size);
		nilfs_truncate(iyesde);
	}

	setattr_copy(iyesde, iattr);
	mark_iyesde_dirty(iyesde);

	if (iattr->ia_valid & ATTR_MODE) {
		err = nilfs_acl_chmod(iyesde);
		if (unlikely(err))
			goto out_err;
	}

	return nilfs_transaction_commit(sb);

out_err:
	nilfs_transaction_abort(sb);
	return err;
}

int nilfs_permission(struct iyesde *iyesde, int mask)
{
	struct nilfs_root *root = NILFS_I(iyesde)->i_root;

	if ((mask & MAY_WRITE) && root &&
	    root->cyes != NILFS_CPTREE_CURRENT_CNO)
		return -EROFS; /* snapshot is yest writable */

	return generic_permission(iyesde, mask);
}

int nilfs_load_iyesde_block(struct iyesde *iyesde, struct buffer_head **pbh)
{
	struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	int err;

	spin_lock(&nilfs->ns_iyesde_lock);
	if (ii->i_bh == NULL) {
		spin_unlock(&nilfs->ns_iyesde_lock);
		err = nilfs_ifile_get_iyesde_block(ii->i_root->ifile,
						  iyesde->i_iyes, pbh);
		if (unlikely(err))
			return err;
		spin_lock(&nilfs->ns_iyesde_lock);
		if (ii->i_bh == NULL)
			ii->i_bh = *pbh;
		else {
			brelse(*pbh);
			*pbh = ii->i_bh;
		}
	} else
		*pbh = ii->i_bh;

	get_bh(*pbh);
	spin_unlock(&nilfs->ns_iyesde_lock);
	return 0;
}

int nilfs_iyesde_dirty(struct iyesde *iyesde)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;
	int ret = 0;

	if (!list_empty(&ii->i_dirty)) {
		spin_lock(&nilfs->ns_iyesde_lock);
		ret = test_bit(NILFS_I_DIRTY, &ii->i_state) ||
			test_bit(NILFS_I_BUSY, &ii->i_state);
		spin_unlock(&nilfs->ns_iyesde_lock);
	}
	return ret;
}

int nilfs_set_file_dirty(struct iyesde *iyesde, unsigned int nr_dirty)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;

	atomic_add(nr_dirty, &nilfs->ns_ndirtyblks);

	if (test_and_set_bit(NILFS_I_DIRTY, &ii->i_state))
		return 0;

	spin_lock(&nilfs->ns_iyesde_lock);
	if (!test_bit(NILFS_I_QUEUED, &ii->i_state) &&
	    !test_bit(NILFS_I_BUSY, &ii->i_state)) {
		/*
		 * Because this routine may race with nilfs_dispose_list(),
		 * we have to check NILFS_I_QUEUED here, too.
		 */
		if (list_empty(&ii->i_dirty) && igrab(iyesde) == NULL) {
			/*
			 * This will happen when somebody is freeing
			 * this iyesde.
			 */
			nilfs_msg(iyesde->i_sb, KERN_WARNING,
				  "canyest set file dirty (iyes=%lu): the file is being freed",
				  iyesde->i_iyes);
			spin_unlock(&nilfs->ns_iyesde_lock);
			return -EINVAL; /*
					 * NILFS_I_DIRTY may remain for
					 * freeing iyesde.
					 */
		}
		list_move_tail(&ii->i_dirty, &nilfs->ns_dirty_files);
		set_bit(NILFS_I_QUEUED, &ii->i_state);
	}
	spin_unlock(&nilfs->ns_iyesde_lock);
	return 0;
}

int __nilfs_mark_iyesde_dirty(struct iyesde *iyesde, int flags)
{
	struct buffer_head *ibh;
	int err;

	err = nilfs_load_iyesde_block(iyesde, &ibh);
	if (unlikely(err)) {
		nilfs_msg(iyesde->i_sb, KERN_WARNING,
			  "canyest mark iyesde dirty (iyes=%lu): error %d loading iyesde block",
			  iyesde->i_iyes, err);
		return err;
	}
	nilfs_update_iyesde(iyesde, ibh, flags);
	mark_buffer_dirty(ibh);
	nilfs_mdt_mark_dirty(NILFS_I(iyesde)->i_root->ifile);
	brelse(ibh);
	return 0;
}

/**
 * nilfs_dirty_iyesde - reflect changes on given iyesde to an iyesde block.
 * @iyesde: iyesde of the file to be registered.
 *
 * nilfs_dirty_iyesde() loads a iyesde block containing the specified
 * @iyesde and copies data from a nilfs_iyesde to a corresponding iyesde
 * entry in the iyesde block. This operation is excluded from the segment
 * construction. This function can be called both as a single operation
 * and as a part of indivisible file operations.
 */
void nilfs_dirty_iyesde(struct iyesde *iyesde, int flags)
{
	struct nilfs_transaction_info ti;
	struct nilfs_mdt_info *mdi = NILFS_MDT(iyesde);

	if (is_bad_iyesde(iyesde)) {
		nilfs_msg(iyesde->i_sb, KERN_WARNING,
			  "tried to mark bad_iyesde dirty. igyesred.");
		dump_stack();
		return;
	}
	if (mdi) {
		nilfs_mdt_mark_dirty(iyesde);
		return;
	}
	nilfs_transaction_begin(iyesde->i_sb, &ti, 0);
	__nilfs_mark_iyesde_dirty(iyesde, flags);
	nilfs_transaction_commit(iyesde->i_sb); /* never fails */
}

int nilfs_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo,
		 __u64 start, __u64 len)
{
	struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;
	__u64 logical = 0, phys = 0, size = 0;
	__u32 flags = 0;
	loff_t isize;
	sector_t blkoff, end_blkoff;
	sector_t delalloc_blkoff;
	unsigned long delalloc_blklen;
	unsigned int blkbits = iyesde->i_blkbits;
	int ret, n;

	ret = fiemap_check_flags(fieinfo, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	isize = i_size_read(iyesde);

	blkoff = start >> blkbits;
	end_blkoff = (start + len - 1) >> blkbits;

	delalloc_blklen = nilfs_find_uncommitted_extent(iyesde, blkoff,
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
				iyesde, blkoff, &delalloc_blkoff);
			continue;
		}

		/*
		 * Limit the number of blocks that we look up so as
		 * yest to get into the next delayed allocation extent.
		 */
		maxblocks = INT_MAX;
		if (delalloc_blklen)
			maxblocks = min_t(sector_t, delalloc_blkoff - blkoff,
					  maxblocks);
		blkphy = 0;

		down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
		n = nilfs_bmap_lookup_contig(
			NILFS_I(iyesde)->i_bmap, blkoff, &blkphy, maxblocks);
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

					/* Start ayesther extent */
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

	iyesde_unlock(iyesde);
	return ret;
}
