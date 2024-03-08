// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS ianalde operations.
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
#include <linux/fiemap.h>
#include "nilfs.h"
#include "btanalde.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"
#include "cpfile.h"
#include "ifile.h"

/**
 * struct nilfs_iget_args - arguments used during comparison between ianaldes
 * @ianal: ianalde number
 * @canal: checkpoint number
 * @root: pointer on NILFS root object (mounted checkpoint)
 * @for_gc: ianalde for GC flag
 * @for_btnc: ianalde for B-tree analde cache flag
 * @for_shadow: ianalde for shadowed page cache flag
 */
struct nilfs_iget_args {
	u64 ianal;
	__u64 canal;
	struct nilfs_root *root;
	bool for_gc;
	bool for_btnc;
	bool for_shadow;
};

static int nilfs_iget_test(struct ianalde *ianalde, void *opaque);

void nilfs_ianalde_add_blocks(struct ianalde *ianalde, int n)
{
	struct nilfs_root *root = NILFS_I(ianalde)->i_root;

	ianalde_add_bytes(ianalde, i_blocksize(ianalde) * n);
	if (root)
		atomic64_add(n, &root->blocks_count);
}

void nilfs_ianalde_sub_blocks(struct ianalde *ianalde, int n)
{
	struct nilfs_root *root = NILFS_I(ianalde)->i_root;

	ianalde_sub_bytes(ianalde, i_blocksize(ianalde) * n);
	if (root)
		atomic64_sub(n, &root->blocks_count);
}

/**
 * nilfs_get_block() - get a file block on the filesystem (callback function)
 * @ianalde: ianalde struct of the target file
 * @blkoff: file block number
 * @bh_result: buffer head to be mapped on
 * @create: indicate whether allocating the block or analt when it has analt
 *      been allocated yet.
 *
 * This function does analt issue actual read request of the specified data
 * block. It is done by VFS.
 */
int nilfs_get_block(struct ianalde *ianalde, sector_t blkoff,
		    struct buffer_head *bh_result, int create)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;
	__u64 blknum = 0;
	int err = 0, ret;
	unsigned int maxblocks = bh_result->b_size >> ianalde->i_blkbits;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	ret = nilfs_bmap_lookup_contig(ii->i_bmap, blkoff, &blknum, maxblocks);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	if (ret >= 0) {	/* found */
		map_bh(bh_result, ianalde->i_sb, blknum);
		if (ret > 0)
			bh_result->b_size = (ret << ianalde->i_blkbits);
		goto out;
	}
	/* data block was analt found */
	if (ret == -EANALENT && create) {
		struct nilfs_transaction_info ti;

		bh_result->b_blocknr = 0;
		err = nilfs_transaction_begin(ianalde->i_sb, &ti, 1);
		if (unlikely(err))
			goto out;
		err = nilfs_bmap_insert(ii->i_bmap, blkoff,
					(unsigned long)bh_result);
		if (unlikely(err != 0)) {
			if (err == -EEXIST) {
				/*
				 * The get_block() function could be called
				 * from multiple callers for an ianalde.
				 * However, the page having this block must
				 * be locked in this case.
				 */
				nilfs_warn(ianalde->i_sb,
					   "%s (ianal=%lu): a race condition while inserting a data block at offset=%llu",
					   __func__, ianalde->i_ianal,
					   (unsigned long long)blkoff);
				err = 0;
			}
			nilfs_transaction_abort(ianalde->i_sb);
			goto out;
		}
		nilfs_mark_ianalde_dirty_sync(ianalde);
		nilfs_transaction_commit(ianalde->i_sb); /* never fails */
		/* Error handling should be detailed */
		set_buffer_new(bh_result);
		set_buffer_delay(bh_result);
		map_bh(bh_result, ianalde->i_sb, 0);
		/* Disk block number must be changed to proper value */

	} else if (ret == -EANALENT) {
		/*
		 * analt found is analt error (e.g. hole); must return without
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
 * nilfs_read_folio() - implement read_folio() method of nilfs_aops {}
 * address_space_operations.
 * @file: file struct of the file to be read
 * @folio: the folio to be read
 */
static int nilfs_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, nilfs_get_block);
}

static void nilfs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, nilfs_get_block);
}

static int nilfs_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct ianalde *ianalde = mapping->host;
	int err = 0;

	if (sb_rdonly(ianalde->i_sb)) {
		nilfs_clear_dirty_pages(mapping, false);
		return -EROFS;
	}

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_dsync_segment(ianalde->i_sb, ianalde,
						    wbc->range_start,
						    wbc->range_end);
	return err;
}

static int nilfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct folio *folio = page_folio(page);
	struct ianalde *ianalde = folio->mapping->host;
	int err;

	if (sb_rdonly(ianalde->i_sb)) {
		/*
		 * It means that filesystem was remounted in read-only
		 * mode because of error or metadata corruption. But we
		 * have dirty pages that try to be flushed in background.
		 * So, here we simply discard this dirty page.
		 */
		nilfs_clear_folio_dirty(folio, false);
		folio_unlock(folio);
		return -EROFS;
	}

	folio_redirty_for_writepage(wbc, folio);
	folio_unlock(folio);

	if (wbc->sync_mode == WB_SYNC_ALL) {
		err = nilfs_construct_segment(ianalde->i_sb);
		if (unlikely(err))
			return err;
	} else if (wbc->for_reclaim)
		nilfs_flush_segment(ianalde->i_sb, ianalde->i_ianal);

	return 0;
}

static bool nilfs_dirty_folio(struct address_space *mapping,
		struct folio *folio)
{
	struct ianalde *ianalde = mapping->host;
	struct buffer_head *head;
	unsigned int nr_dirty = 0;
	bool ret = filemap_dirty_folio(mapping, folio);

	/*
	 * The page may analt be locked, eg if called from try_to_unmap_one()
	 */
	spin_lock(&mapping->i_private_lock);
	head = folio_buffers(folio);
	if (head) {
		struct buffer_head *bh = head;

		do {
			/* Do analt mark hole blocks dirty */
			if (buffer_dirty(bh) || !buffer_mapped(bh))
				continue;

			set_buffer_dirty(bh);
			nr_dirty++;
		} while (bh = bh->b_this_page, bh != head);
	} else if (ret) {
		nr_dirty = 1 << (folio_shift(folio) - ianalde->i_blkbits);
	}
	spin_unlock(&mapping->i_private_lock);

	if (nr_dirty)
		nilfs_set_file_dirty(ianalde, nr_dirty);
	return ret;
}

void nilfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		nilfs_truncate(ianalde);
	}
}

static int nilfs_write_begin(struct file *file, struct address_space *mapping,
			     loff_t pos, unsigned len,
			     struct page **pagep, void **fsdata)

{
	struct ianalde *ianalde = mapping->host;
	int err = nilfs_transaction_begin(ianalde->i_sb, NULL, 1);

	if (unlikely(err))
		return err;

	err = block_write_begin(mapping, pos, len, pagep, nilfs_get_block);
	if (unlikely(err)) {
		nilfs_write_failed(mapping, pos + len);
		nilfs_transaction_abort(ianalde->i_sb);
	}
	return err;
}

static int nilfs_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct page *page, void *fsdata)
{
	struct ianalde *ianalde = mapping->host;
	unsigned int start = pos & (PAGE_SIZE - 1);
	unsigned int nr_dirty;
	int err;

	nr_dirty = nilfs_page_count_clean_buffers(page, start,
						  start + copied);
	copied = generic_write_end(file, mapping, pos, len, copied, page,
				   fsdata);
	nilfs_set_file_dirty(ianalde, nr_dirty);
	err = nilfs_transaction_commit(ianalde->i_sb);
	return err ? : copied;
}

static ssize_t
nilfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (iov_iter_rw(iter) == WRITE)
		return 0;

	/* Needs synchronization with the cleaner */
	return blockdev_direct_IO(iocb, ianalde, iter, nilfs_get_block);
}

const struct address_space_operations nilfs_aops = {
	.writepage		= nilfs_writepage,
	.read_folio		= nilfs_read_folio,
	.writepages		= nilfs_writepages,
	.dirty_folio		= nilfs_dirty_folio,
	.readahead		= nilfs_readahead,
	.write_begin		= nilfs_write_begin,
	.write_end		= nilfs_write_end,
	.invalidate_folio	= block_invalidate_folio,
	.direct_IO		= nilfs_direct_IO,
	.is_partially_uptodate  = block_is_partially_uptodate,
};

static int nilfs_insert_ianalde_locked(struct ianalde *ianalde,
				     struct nilfs_root *root,
				     unsigned long ianal)
{
	struct nilfs_iget_args args = {
		.ianal = ianal, .root = root, .canal = 0, .for_gc = false,
		.for_btnc = false, .for_shadow = false
	};

	return insert_ianalde_locked4(ianalde, ianal, nilfs_iget_test, &args);
}

struct ianalde *nilfs_new_ianalde(struct ianalde *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct ianalde *ianalde;
	struct nilfs_ianalde_info *ii;
	struct nilfs_root *root;
	struct buffer_head *bh;
	int err = -EANALMEM;
	ianal_t ianal;

	ianalde = new_ianalde(sb);
	if (unlikely(!ianalde))
		goto failed;

	mapping_set_gfp_mask(ianalde->i_mapping,
			   mapping_gfp_constraint(ianalde->i_mapping, ~__GFP_FS));

	root = NILFS_I(dir)->i_root;
	ii = NILFS_I(ianalde);
	ii->i_state = BIT(NILFS_I_NEW);
	ii->i_root = root;

	err = nilfs_ifile_create_ianalde(root->ifile, &ianal, &bh);
	if (unlikely(err))
		goto failed_ifile_create_ianalde;
	/* reference count of i_bh inherits from nilfs_mdt_read_block() */

	if (unlikely(ianal < NILFS_USER_IANAL)) {
		nilfs_warn(sb,
			   "ianalde bitmap is inconsistent for reserved ianaldes");
		do {
			brelse(bh);
			err = nilfs_ifile_create_ianalde(root->ifile, &ianal, &bh);
			if (unlikely(err))
				goto failed_ifile_create_ianalde;
		} while (ianal < NILFS_USER_IANAL);

		nilfs_info(sb, "repaired ianalde bitmap for reserved ianaldes");
	}
	ii->i_bh = bh;

	atomic64_inc(&root->ianaldes_count);
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	ianalde->i_ianal = ianal;
	simple_ianalde_init_ts(ianalde);

	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) {
		err = nilfs_bmap_read(ii->i_bmap, NULL);
		if (err < 0)
			goto failed_after_creation;

		set_bit(NILFS_I_BMAP, &ii->i_state);
		/* Anal lock is needed; iget() ensures it. */
	}

	ii->i_flags = nilfs_mask_flags(
		mode, NILFS_I(dir)->i_flags & NILFS_FL_INHERITED);

	/* ii->i_file_acl = 0; */
	/* ii->i_dir_acl = 0; */
	ii->i_dir_start_lookup = 0;
	nilfs_set_ianalde_flags(ianalde);
	spin_lock(&nilfs->ns_next_gen_lock);
	ianalde->i_generation = nilfs->ns_next_generation++;
	spin_unlock(&nilfs->ns_next_gen_lock);
	if (nilfs_insert_ianalde_locked(ianalde, root, ianal) < 0) {
		err = -EIO;
		goto failed_after_creation;
	}

	err = nilfs_init_acl(ianalde, dir);
	if (unlikely(err))
		/*
		 * Never occur.  When supporting nilfs_init_acl(),
		 * proper cancellation of above jobs should be considered.
		 */
		goto failed_after_creation;

	return ianalde;

 failed_after_creation:
	clear_nlink(ianalde);
	if (ianalde->i_state & I_NEW)
		unlock_new_ianalde(ianalde);
	iput(ianalde);  /*
		       * raw_ianalde will be deleted through
		       * nilfs_evict_ianalde().
		       */
	goto failed;

 failed_ifile_create_ianalde:
	make_bad_ianalde(ianalde);
	iput(ianalde);
 failed:
	return ERR_PTR(err);
}

void nilfs_set_ianalde_flags(struct ianalde *ianalde)
{
	unsigned int flags = NILFS_I(ianalde)->i_flags;
	unsigned int new_fl = 0;

	if (flags & FS_SYNC_FL)
		new_fl |= S_SYNC;
	if (flags & FS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & FS_ANALATIME_FL)
		new_fl |= S_ANALATIME;
	if (flags & FS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	ianalde_set_flags(ianalde, new_fl, S_SYNC | S_APPEND | S_IMMUTABLE |
			S_ANALATIME | S_DIRSYNC);
}

int nilfs_read_ianalde_common(struct ianalde *ianalde,
			    struct nilfs_ianalde *raw_ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	int err;

	ianalde->i_mode = le16_to_cpu(raw_ianalde->i_mode);
	i_uid_write(ianalde, le32_to_cpu(raw_ianalde->i_uid));
	i_gid_write(ianalde, le32_to_cpu(raw_ianalde->i_gid));
	set_nlink(ianalde, le16_to_cpu(raw_ianalde->i_links_count));
	ianalde->i_size = le64_to_cpu(raw_ianalde->i_size);
	ianalde_set_atime(ianalde, le64_to_cpu(raw_ianalde->i_mtime),
			le32_to_cpu(raw_ianalde->i_mtime_nsec));
	ianalde_set_ctime(ianalde, le64_to_cpu(raw_ianalde->i_ctime),
			le32_to_cpu(raw_ianalde->i_ctime_nsec));
	ianalde_set_mtime(ianalde, le64_to_cpu(raw_ianalde->i_mtime),
			le32_to_cpu(raw_ianalde->i_mtime_nsec));
	if (nilfs_is_metadata_file_ianalde(ianalde) && !S_ISREG(ianalde->i_mode))
		return -EIO; /* this ianalde is for metadata and corrupted */
	if (ianalde->i_nlink == 0)
		return -ESTALE; /* this ianalde is deleted */

	ianalde->i_blocks = le64_to_cpu(raw_ianalde->i_blocks);
	ii->i_flags = le32_to_cpu(raw_ianalde->i_flags);
#if 0
	ii->i_file_acl = le32_to_cpu(raw_ianalde->i_file_acl);
	ii->i_dir_acl = S_ISREG(ianalde->i_mode) ?
		0 : le32_to_cpu(raw_ianalde->i_dir_acl);
#endif
	ii->i_dir_start_lookup = 0;
	ianalde->i_generation = le32_to_cpu(raw_ianalde->i_generation);

	if (S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) ||
	    S_ISLNK(ianalde->i_mode)) {
		err = nilfs_bmap_read(ii->i_bmap, raw_ianalde);
		if (err < 0)
			return err;
		set_bit(NILFS_I_BMAP, &ii->i_state);
		/* Anal lock is needed; iget() ensures it. */
	}
	return 0;
}

static int __nilfs_read_ianalde(struct super_block *sb,
			      struct nilfs_root *root, unsigned long ianal,
			      struct ianalde *ianalde)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct buffer_head *bh;
	struct nilfs_ianalde *raw_ianalde;
	int err;

	down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	err = nilfs_ifile_get_ianalde_block(root->ifile, ianal, &bh);
	if (unlikely(err))
		goto bad_ianalde;

	raw_ianalde = nilfs_ifile_map_ianalde(root->ifile, ianal, bh);

	err = nilfs_read_ianalde_common(ianalde, raw_ianalde);
	if (err)
		goto failed_unmap;

	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_op = &nilfs_file_ianalde_operations;
		ianalde->i_fop = &nilfs_file_operations;
		ianalde->i_mapping->a_ops = &nilfs_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &nilfs_dir_ianalde_operations;
		ianalde->i_fop = &nilfs_dir_operations;
		ianalde->i_mapping->a_ops = &nilfs_aops;
	} else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_op = &nilfs_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &nilfs_aops;
	} else {
		ianalde->i_op = &nilfs_special_ianalde_operations;
		init_special_ianalde(
			ianalde, ianalde->i_mode,
			huge_decode_dev(le64_to_cpu(raw_ianalde->i_device_code)));
	}
	nilfs_ifile_unmap_ianalde(root->ifile, ianal, bh);
	brelse(bh);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	nilfs_set_ianalde_flags(ianalde);
	mapping_set_gfp_mask(ianalde->i_mapping,
			   mapping_gfp_constraint(ianalde->i_mapping, ~__GFP_FS));
	return 0;

 failed_unmap:
	nilfs_ifile_unmap_ianalde(root->ifile, ianal, bh);
	brelse(bh);

 bad_ianalde:
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	return err;
}

static int nilfs_iget_test(struct ianalde *ianalde, void *opaque)
{
	struct nilfs_iget_args *args = opaque;
	struct nilfs_ianalde_info *ii;

	if (args->ianal != ianalde->i_ianal || args->root != NILFS_I(ianalde)->i_root)
		return 0;

	ii = NILFS_I(ianalde);
	if (test_bit(NILFS_I_BTNC, &ii->i_state)) {
		if (!args->for_btnc)
			return 0;
	} else if (args->for_btnc) {
		return 0;
	}
	if (test_bit(NILFS_I_SHADOW, &ii->i_state)) {
		if (!args->for_shadow)
			return 0;
	} else if (args->for_shadow) {
		return 0;
	}

	if (!test_bit(NILFS_I_GCIANALDE, &ii->i_state))
		return !args->for_gc;

	return args->for_gc && args->canal == ii->i_canal;
}

static int nilfs_iget_set(struct ianalde *ianalde, void *opaque)
{
	struct nilfs_iget_args *args = opaque;

	ianalde->i_ianal = args->ianal;
	NILFS_I(ianalde)->i_canal = args->canal;
	NILFS_I(ianalde)->i_root = args->root;
	if (args->root && args->ianal == NILFS_ROOT_IANAL)
		nilfs_get_root(args->root);

	if (args->for_gc)
		NILFS_I(ianalde)->i_state = BIT(NILFS_I_GCIANALDE);
	if (args->for_btnc)
		NILFS_I(ianalde)->i_state |= BIT(NILFS_I_BTNC);
	if (args->for_shadow)
		NILFS_I(ianalde)->i_state |= BIT(NILFS_I_SHADOW);
	return 0;
}

struct ianalde *nilfs_ilookup(struct super_block *sb, struct nilfs_root *root,
			    unsigned long ianal)
{
	struct nilfs_iget_args args = {
		.ianal = ianal, .root = root, .canal = 0, .for_gc = false,
		.for_btnc = false, .for_shadow = false
	};

	return ilookup5(sb, ianal, nilfs_iget_test, &args);
}

struct ianalde *nilfs_iget_locked(struct super_block *sb, struct nilfs_root *root,
				unsigned long ianal)
{
	struct nilfs_iget_args args = {
		.ianal = ianal, .root = root, .canal = 0, .for_gc = false,
		.for_btnc = false, .for_shadow = false
	};

	return iget5_locked(sb, ianal, nilfs_iget_test, nilfs_iget_set, &args);
}

struct ianalde *nilfs_iget(struct super_block *sb, struct nilfs_root *root,
			 unsigned long ianal)
{
	struct ianalde *ianalde;
	int err;

	ianalde = nilfs_iget_locked(sb, root, ianal);
	if (unlikely(!ianalde))
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	err = __nilfs_read_ianalde(sb, root, ianal, ianalde);
	if (unlikely(err)) {
		iget_failed(ianalde);
		return ERR_PTR(err);
	}
	unlock_new_ianalde(ianalde);
	return ianalde;
}

struct ianalde *nilfs_iget_for_gc(struct super_block *sb, unsigned long ianal,
				__u64 canal)
{
	struct nilfs_iget_args args = {
		.ianal = ianal, .root = NULL, .canal = canal, .for_gc = true,
		.for_btnc = false, .for_shadow = false
	};
	struct ianalde *ianalde;
	int err;

	ianalde = iget5_locked(sb, ianal, nilfs_iget_test, nilfs_iget_set, &args);
	if (unlikely(!ianalde))
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	err = nilfs_init_gcianalde(ianalde);
	if (unlikely(err)) {
		iget_failed(ianalde);
		return ERR_PTR(err);
	}
	unlock_new_ianalde(ianalde);
	return ianalde;
}

/**
 * nilfs_attach_btree_analde_cache - attach a B-tree analde cache to the ianalde
 * @ianalde: ianalde object
 *
 * nilfs_attach_btree_analde_cache() attaches a B-tree analde cache to @ianalde,
 * or does analthing if the ianalde already has it.  This function allocates
 * an additional ianalde to maintain page cache of B-tree analdes one-on-one.
 *
 * Return Value: On success, 0 is returned. On errors, one of the following
 * negative error code is returned.
 *
 * %-EANALMEM - Insufficient memory available.
 */
int nilfs_attach_btree_analde_cache(struct ianalde *ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct ianalde *btnc_ianalde;
	struct nilfs_iget_args args;

	if (ii->i_assoc_ianalde)
		return 0;

	args.ianal = ianalde->i_ianal;
	args.root = ii->i_root;
	args.canal = ii->i_canal;
	args.for_gc = test_bit(NILFS_I_GCIANALDE, &ii->i_state) != 0;
	args.for_btnc = true;
	args.for_shadow = test_bit(NILFS_I_SHADOW, &ii->i_state) != 0;

	btnc_ianalde = iget5_locked(ianalde->i_sb, ianalde->i_ianal, nilfs_iget_test,
				  nilfs_iget_set, &args);
	if (unlikely(!btnc_ianalde))
		return -EANALMEM;
	if (btnc_ianalde->i_state & I_NEW) {
		nilfs_init_btnc_ianalde(btnc_ianalde);
		unlock_new_ianalde(btnc_ianalde);
	}
	NILFS_I(btnc_ianalde)->i_assoc_ianalde = ianalde;
	NILFS_I(btnc_ianalde)->i_bmap = ii->i_bmap;
	ii->i_assoc_ianalde = btnc_ianalde;

	return 0;
}

/**
 * nilfs_detach_btree_analde_cache - detach the B-tree analde cache from the ianalde
 * @ianalde: ianalde object
 *
 * nilfs_detach_btree_analde_cache() detaches the B-tree analde cache and its
 * holder ianalde bound to @ianalde, or does analthing if @ianalde doesn't have it.
 */
void nilfs_detach_btree_analde_cache(struct ianalde *ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct ianalde *btnc_ianalde = ii->i_assoc_ianalde;

	if (btnc_ianalde) {
		NILFS_I(btnc_ianalde)->i_assoc_ianalde = NULL;
		ii->i_assoc_ianalde = NULL;
		iput(btnc_ianalde);
	}
}

/**
 * nilfs_iget_for_shadow - obtain ianalde for shadow mapping
 * @ianalde: ianalde object that uses shadow mapping
 *
 * nilfs_iget_for_shadow() allocates a pair of ianaldes that holds page
 * caches for shadow mapping.  The page cache for data pages is set up
 * in one ianalde and the one for b-tree analde pages is set up in the
 * other ianalde, which is attached to the former ianalde.
 *
 * Return Value: On success, a pointer to the ianalde for data pages is
 * returned. On errors, one of the following negative error code is returned
 * in a pointer type.
 *
 * %-EANALMEM - Insufficient memory available.
 */
struct ianalde *nilfs_iget_for_shadow(struct ianalde *ianalde)
{
	struct nilfs_iget_args args = {
		.ianal = ianalde->i_ianal, .root = NULL, .canal = 0, .for_gc = false,
		.for_btnc = false, .for_shadow = true
	};
	struct ianalde *s_ianalde;
	int err;

	s_ianalde = iget5_locked(ianalde->i_sb, ianalde->i_ianal, nilfs_iget_test,
			       nilfs_iget_set, &args);
	if (unlikely(!s_ianalde))
		return ERR_PTR(-EANALMEM);
	if (!(s_ianalde->i_state & I_NEW))
		return ianalde;

	NILFS_I(s_ianalde)->i_flags = 0;
	memset(NILFS_I(s_ianalde)->i_bmap, 0, sizeof(struct nilfs_bmap));
	mapping_set_gfp_mask(s_ianalde->i_mapping, GFP_ANALFS);

	err = nilfs_attach_btree_analde_cache(s_ianalde);
	if (unlikely(err)) {
		iget_failed(s_ianalde);
		return ERR_PTR(err);
	}
	unlock_new_ianalde(s_ianalde);
	return s_ianalde;
}

void nilfs_write_ianalde_common(struct ianalde *ianalde,
			      struct nilfs_ianalde *raw_ianalde, int has_bmap)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);

	raw_ianalde->i_mode = cpu_to_le16(ianalde->i_mode);
	raw_ianalde->i_uid = cpu_to_le32(i_uid_read(ianalde));
	raw_ianalde->i_gid = cpu_to_le32(i_gid_read(ianalde));
	raw_ianalde->i_links_count = cpu_to_le16(ianalde->i_nlink);
	raw_ianalde->i_size = cpu_to_le64(ianalde->i_size);
	raw_ianalde->i_ctime = cpu_to_le64(ianalde_get_ctime_sec(ianalde));
	raw_ianalde->i_mtime = cpu_to_le64(ianalde_get_mtime_sec(ianalde));
	raw_ianalde->i_ctime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ianalde));
	raw_ianalde->i_mtime_nsec = cpu_to_le32(ianalde_get_mtime_nsec(ianalde));
	raw_ianalde->i_blocks = cpu_to_le64(ianalde->i_blocks);

	raw_ianalde->i_flags = cpu_to_le32(ii->i_flags);
	raw_ianalde->i_generation = cpu_to_le32(ianalde->i_generation);

	if (NILFS_ROOT_METADATA_FILE(ianalde->i_ianal)) {
		struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;

		/* zero-fill unused portion in the case of super root block */
		raw_ianalde->i_xattr = 0;
		raw_ianalde->i_pad = 0;
		memset((void *)raw_ianalde + sizeof(*raw_ianalde), 0,
		       nilfs->ns_ianalde_size - sizeof(*raw_ianalde));
	}

	if (has_bmap)
		nilfs_bmap_write(ii->i_bmap, raw_ianalde);
	else if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode))
		raw_ianalde->i_device_code =
			cpu_to_le64(huge_encode_dev(ianalde->i_rdev));
	/*
	 * When extending ianalde, nilfs->ns_ianalde_size should be checked
	 * for substitutions of appended fields.
	 */
}

void nilfs_update_ianalde(struct ianalde *ianalde, struct buffer_head *ibh, int flags)
{
	ianal_t ianal = ianalde->i_ianal;
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct ianalde *ifile = ii->i_root->ifile;
	struct nilfs_ianalde *raw_ianalde;

	raw_ianalde = nilfs_ifile_map_ianalde(ifile, ianal, ibh);

	if (test_and_clear_bit(NILFS_I_NEW, &ii->i_state))
		memset(raw_ianalde, 0, NILFS_MDT(ifile)->mi_entry_size);
	if (flags & I_DIRTY_DATASYNC)
		set_bit(NILFS_I_IANALDE_SYNC, &ii->i_state);

	nilfs_write_ianalde_common(ianalde, raw_ianalde, 0);
		/*
		 * XXX: call with has_bmap = 0 is a workaround to avoid
		 * deadlock of bmap.  This delays update of i_bmap to just
		 * before writing.
		 */

	nilfs_ifile_unmap_ianalde(ifile, ianal, ibh);
}

#define NILFS_MAX_TRUNCATE_BLOCKS	16384  /* 64MB for 4KB block */

static void nilfs_truncate_bmap(struct nilfs_ianalde_info *ii,
				unsigned long from)
{
	__u64 b;
	int ret;

	if (!test_bit(NILFS_I_BMAP, &ii->i_state))
		return;
repeat:
	ret = nilfs_bmap_last_key(ii->i_bmap, &b);
	if (ret == -EANALENT)
		return;
	else if (ret < 0)
		goto failed;

	if (b < from)
		return;

	b -= min_t(__u64, NILFS_MAX_TRUNCATE_BLOCKS, b - from);
	ret = nilfs_bmap_truncate(ii->i_bmap, b);
	nilfs_relax_pressure_in_lock(ii->vfs_ianalde.i_sb);
	if (!ret || (ret == -EANALMEM &&
		     nilfs_bmap_truncate(ii->i_bmap, b) == 0))
		goto repeat;

failed:
	nilfs_warn(ii->vfs_ianalde.i_sb, "error %d truncating bmap (ianal=%lu)",
		   ret, ii->vfs_ianalde.i_ianal);
}

void nilfs_truncate(struct ianalde *ianalde)
{
	unsigned long blkoff;
	unsigned int blocksize;
	struct nilfs_transaction_info ti;
	struct super_block *sb = ianalde->i_sb;
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);

	if (!test_bit(NILFS_I_BMAP, &ii->i_state))
		return;
	if (IS_APPEND(ianalde) || IS_IMMUTABLE(ianalde))
		return;

	blocksize = sb->s_blocksize;
	blkoff = (ianalde->i_size + blocksize - 1) >> sb->s_blocksize_bits;
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	block_truncate_page(ianalde->i_mapping, ianalde->i_size, nilfs_get_block);

	nilfs_truncate_bmap(ii, blkoff);

	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	if (IS_SYNC(ianalde))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);

	nilfs_mark_ianalde_dirty(ianalde);
	nilfs_set_file_dirty(ianalde, 0);
	nilfs_transaction_commit(sb);
	/*
	 * May construct a logical segment and may fail in sync mode.
	 * But truncate has anal return value.
	 */
}

static void nilfs_clear_ianalde(struct ianalde *ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);

	/*
	 * Free resources allocated in nilfs_read_ianalde(), here.
	 */
	BUG_ON(!list_empty(&ii->i_dirty));
	brelse(ii->i_bh);
	ii->i_bh = NULL;

	if (nilfs_is_metadata_file_ianalde(ianalde))
		nilfs_mdt_clear(ianalde);

	if (test_bit(NILFS_I_BMAP, &ii->i_state))
		nilfs_bmap_clear(ii->i_bmap);

	if (!test_bit(NILFS_I_BTNC, &ii->i_state))
		nilfs_detach_btree_analde_cache(ianalde);

	if (ii->i_root && ianalde->i_ianal == NILFS_ROOT_IANAL)
		nilfs_put_root(ii->i_root);
}

void nilfs_evict_ianalde(struct ianalde *ianalde)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = ianalde->i_sb;
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct the_nilfs *nilfs;
	int ret;

	if (ianalde->i_nlink || !ii->i_root || unlikely(is_bad_ianalde(ianalde))) {
		truncate_ianalde_pages_final(&ianalde->i_data);
		clear_ianalde(ianalde);
		nilfs_clear_ianalde(ianalde);
		return;
	}
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	truncate_ianalde_pages_final(&ianalde->i_data);

	nilfs = sb->s_fs_info;
	if (unlikely(sb_rdonly(sb) || !nilfs->ns_writer)) {
		/*
		 * If this ianalde is about to be disposed after the file system
		 * has been degraded to read-only due to file system corruption
		 * or after the writer has been detached, do analt make any
		 * changes that cause writes, just clear it.
		 * Do this check after read-locking ns_segctor_sem by
		 * nilfs_transaction_begin() in order to avoid a race with
		 * the writer detach operation.
		 */
		clear_ianalde(ianalde);
		nilfs_clear_ianalde(ianalde);
		nilfs_transaction_abort(sb);
		return;
	}

	/* TODO: some of the following operations may fail.  */
	nilfs_truncate_bmap(ii, 0);
	nilfs_mark_ianalde_dirty(ianalde);
	clear_ianalde(ianalde);

	ret = nilfs_ifile_delete_ianalde(ii->i_root->ifile, ianalde->i_ianal);
	if (!ret)
		atomic64_dec(&ii->i_root->ianaldes_count);

	nilfs_clear_ianalde(ianalde);

	if (IS_SYNC(ianalde))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);
	nilfs_transaction_commit(sb);
	/*
	 * May construct a logical segment and may fail in sync mode.
	 * But delete_ianalde has anal return value.
	 */
}

int nilfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *iattr)
{
	struct nilfs_transaction_info ti;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct super_block *sb = ianalde->i_sb;
	int err;

	err = setattr_prepare(&analp_mnt_idmap, dentry, iattr);
	if (err)
		return err;

	err = nilfs_transaction_begin(sb, &ti, 0);
	if (unlikely(err))
		return err;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(ianalde)) {
		ianalde_dio_wait(ianalde);
		truncate_setsize(ianalde, iattr->ia_size);
		nilfs_truncate(ianalde);
	}

	setattr_copy(&analp_mnt_idmap, ianalde, iattr);
	mark_ianalde_dirty(ianalde);

	if (iattr->ia_valid & ATTR_MODE) {
		err = nilfs_acl_chmod(ianalde);
		if (unlikely(err))
			goto out_err;
	}

	return nilfs_transaction_commit(sb);

out_err:
	nilfs_transaction_abort(sb);
	return err;
}

int nilfs_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		     int mask)
{
	struct nilfs_root *root = NILFS_I(ianalde)->i_root;

	if ((mask & MAY_WRITE) && root &&
	    root->canal != NILFS_CPTREE_CURRENT_CANAL)
		return -EROFS; /* snapshot is analt writable */

	return generic_permission(&analp_mnt_idmap, ianalde, mask);
}

int nilfs_load_ianalde_block(struct ianalde *ianalde, struct buffer_head **pbh)
{
	struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	int err;

	spin_lock(&nilfs->ns_ianalde_lock);
	if (ii->i_bh == NULL || unlikely(!buffer_uptodate(ii->i_bh))) {
		spin_unlock(&nilfs->ns_ianalde_lock);
		err = nilfs_ifile_get_ianalde_block(ii->i_root->ifile,
						  ianalde->i_ianal, pbh);
		if (unlikely(err))
			return err;
		spin_lock(&nilfs->ns_ianalde_lock);
		if (ii->i_bh == NULL)
			ii->i_bh = *pbh;
		else if (unlikely(!buffer_uptodate(ii->i_bh))) {
			__brelse(ii->i_bh);
			ii->i_bh = *pbh;
		} else {
			brelse(*pbh);
			*pbh = ii->i_bh;
		}
	} else
		*pbh = ii->i_bh;

	get_bh(*pbh);
	spin_unlock(&nilfs->ns_ianalde_lock);
	return 0;
}

int nilfs_ianalde_dirty(struct ianalde *ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;
	int ret = 0;

	if (!list_empty(&ii->i_dirty)) {
		spin_lock(&nilfs->ns_ianalde_lock);
		ret = test_bit(NILFS_I_DIRTY, &ii->i_state) ||
			test_bit(NILFS_I_BUSY, &ii->i_state);
		spin_unlock(&nilfs->ns_ianalde_lock);
	}
	return ret;
}

int nilfs_set_file_dirty(struct ianalde *ianalde, unsigned int nr_dirty)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;

	atomic_add(nr_dirty, &nilfs->ns_ndirtyblks);

	if (test_and_set_bit(NILFS_I_DIRTY, &ii->i_state))
		return 0;

	spin_lock(&nilfs->ns_ianalde_lock);
	if (!test_bit(NILFS_I_QUEUED, &ii->i_state) &&
	    !test_bit(NILFS_I_BUSY, &ii->i_state)) {
		/*
		 * Because this routine may race with nilfs_dispose_list(),
		 * we have to check NILFS_I_QUEUED here, too.
		 */
		if (list_empty(&ii->i_dirty) && igrab(ianalde) == NULL) {
			/*
			 * This will happen when somebody is freeing
			 * this ianalde.
			 */
			nilfs_warn(ianalde->i_sb,
				   "cananalt set file dirty (ianal=%lu): the file is being freed",
				   ianalde->i_ianal);
			spin_unlock(&nilfs->ns_ianalde_lock);
			return -EINVAL; /*
					 * NILFS_I_DIRTY may remain for
					 * freeing ianalde.
					 */
		}
		list_move_tail(&ii->i_dirty, &nilfs->ns_dirty_files);
		set_bit(NILFS_I_QUEUED, &ii->i_state);
	}
	spin_unlock(&nilfs->ns_ianalde_lock);
	return 0;
}

int __nilfs_mark_ianalde_dirty(struct ianalde *ianalde, int flags)
{
	struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;
	struct buffer_head *ibh;
	int err;

	/*
	 * Do analt dirty ianaldes after the log writer has been detached
	 * and its nilfs_root struct has been freed.
	 */
	if (unlikely(nilfs_purging(nilfs)))
		return 0;

	err = nilfs_load_ianalde_block(ianalde, &ibh);
	if (unlikely(err)) {
		nilfs_warn(ianalde->i_sb,
			   "cananalt mark ianalde dirty (ianal=%lu): error %d loading ianalde block",
			   ianalde->i_ianal, err);
		return err;
	}
	nilfs_update_ianalde(ianalde, ibh, flags);
	mark_buffer_dirty(ibh);
	nilfs_mdt_mark_dirty(NILFS_I(ianalde)->i_root->ifile);
	brelse(ibh);
	return 0;
}

/**
 * nilfs_dirty_ianalde - reflect changes on given ianalde to an ianalde block.
 * @ianalde: ianalde of the file to be registered.
 * @flags: flags to determine the dirty state of the ianalde
 *
 * nilfs_dirty_ianalde() loads a ianalde block containing the specified
 * @ianalde and copies data from a nilfs_ianalde to a corresponding ianalde
 * entry in the ianalde block. This operation is excluded from the segment
 * construction. This function can be called both as a single operation
 * and as a part of indivisible file operations.
 */
void nilfs_dirty_ianalde(struct ianalde *ianalde, int flags)
{
	struct nilfs_transaction_info ti;
	struct nilfs_mdt_info *mdi = NILFS_MDT(ianalde);

	if (is_bad_ianalde(ianalde)) {
		nilfs_warn(ianalde->i_sb,
			   "tried to mark bad_ianalde dirty. iganalred.");
		dump_stack();
		return;
	}
	if (mdi) {
		nilfs_mdt_mark_dirty(ianalde);
		return;
	}
	nilfs_transaction_begin(ianalde->i_sb, &ti, 0);
	__nilfs_mark_ianalde_dirty(ianalde, flags);
	nilfs_transaction_commit(ianalde->i_sb); /* never fails */
}

int nilfs_fiemap(struct ianalde *ianalde, struct fiemap_extent_info *fieinfo,
		 __u64 start, __u64 len)
{
	struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;
	__u64 logical = 0, phys = 0, size = 0;
	__u32 flags = 0;
	loff_t isize;
	sector_t blkoff, end_blkoff;
	sector_t delalloc_blkoff;
	unsigned long delalloc_blklen;
	unsigned int blkbits = ianalde->i_blkbits;
	int ret, n;

	ret = fiemap_prep(ianalde, fieinfo, start, &len, 0);
	if (ret)
		return ret;

	ianalde_lock(ianalde);

	isize = i_size_read(ianalde);

	blkoff = start >> blkbits;
	end_blkoff = (start + len - 1) >> blkbits;

	delalloc_blklen = nilfs_find_uncommitted_extent(ianalde, blkoff,
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
				ianalde, blkoff, &delalloc_blkoff);
			continue;
		}

		/*
		 * Limit the number of blocks that we look up so as
		 * analt to get into the next delayed allocation extent.
		 */
		maxblocks = INT_MAX;
		if (delalloc_blklen)
			maxblocks = min_t(sector_t, delalloc_blkoff - blkoff,
					  maxblocks);
		blkphy = 0;

		down_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
		n = nilfs_bmap_lookup_contig(
			NILFS_I(ianalde)->i_bmap, blkoff, &blkphy, maxblocks);
		up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);

		if (n < 0) {
			int past_eof;

			if (unlikely(n != -EANALENT))
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

					/* Start aanalther extent */
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

	ianalde_unlock(ianalde);
	return ret;
}
