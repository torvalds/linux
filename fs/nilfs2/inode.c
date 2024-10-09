// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS inode operations.
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
#include <linux/random.h>
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
 * @type: inode type
 */
struct nilfs_iget_args {
	u64 ino;
	__u64 cno;
	struct nilfs_root *root;
	unsigned int type;
};

static int nilfs_iget_test(struct inode *inode, void *opaque);

void nilfs_inode_add_blocks(struct inode *inode, int n)
{
	struct nilfs_root *root = NILFS_I(inode)->i_root;

	inode_add_bytes(inode, i_blocksize(inode) * n);
	if (root)
		atomic64_add(n, &root->blocks_count);
}

void nilfs_inode_sub_blocks(struct inode *inode, int n)
{
	struct nilfs_root *root = NILFS_I(inode)->i_root;

	inode_sub_bytes(inode, i_blocksize(inode) * n);
	if (root)
		atomic64_sub(n, &root->blocks_count);
}

/**
 * nilfs_get_block() - get a file block on the filesystem (callback function)
 * @inode: inode struct of the target file
 * @blkoff: file block number
 * @bh_result: buffer head to be mapped on
 * @create: indicate whether allocating the block or not when it has not
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
	unsigned int maxblocks = bh_result->b_size >> inode->i_blkbits;

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
				nilfs_warn(inode->i_sb,
					   "%s (ino=%lu): a race condition while inserting a data block at offset=%llu",
					   __func__, inode->i_ino,
					   (unsigned long long)blkoff);
				err = -EAGAIN;
			}
			nilfs_transaction_abort(inode->i_sb);
			goto out;
		}
		nilfs_mark_inode_dirty_sync(inode);
		nilfs_transaction_commit(inode->i_sb); /* never fails */
		/* Error handling should be detailed */
		set_buffer_new(bh_result);
		set_buffer_delay(bh_result);
		map_bh(bh_result, inode->i_sb, 0);
		/* Disk block number must be changed to proper value */

	} else if (ret == -ENOENT) {
		/*
		 * not found is not error (e.g. hole); must return without
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
	struct inode *inode = mapping->host;
	int err = 0;

	if (sb_rdonly(inode->i_sb)) {
		nilfs_clear_dirty_pages(mapping);
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
	struct folio *folio = page_folio(page);
	struct inode *inode = folio->mapping->host;
	int err;

	if (sb_rdonly(inode->i_sb)) {
		/*
		 * It means that filesystem was remounted in read-only
		 * mode because of error or metadata corruption. But we
		 * have dirty pages that try to be flushed in background.
		 * So, here we simply discard this dirty page.
		 */
		nilfs_clear_folio_dirty(folio);
		folio_unlock(folio);
		return -EROFS;
	}

	folio_redirty_for_writepage(wbc, folio);
	folio_unlock(folio);

	if (wbc->sync_mode == WB_SYNC_ALL) {
		err = nilfs_construct_segment(inode->i_sb);
		if (unlikely(err))
			return err;
	} else if (wbc->for_reclaim)
		nilfs_flush_segment(inode->i_sb, inode->i_ino);

	return 0;
}

static bool nilfs_dirty_folio(struct address_space *mapping,
		struct folio *folio)
{
	struct inode *inode = mapping->host;
	struct buffer_head *head;
	unsigned int nr_dirty = 0;
	bool ret = filemap_dirty_folio(mapping, folio);

	/*
	 * The page may not be locked, eg if called from try_to_unmap_one()
	 */
	spin_lock(&mapping->i_private_lock);
	head = folio_buffers(folio);
	if (head) {
		struct buffer_head *bh = head;

		do {
			/* Do not mark hole blocks dirty */
			if (buffer_dirty(bh) || !buffer_mapped(bh))
				continue;

			set_buffer_dirty(bh);
			nr_dirty++;
		} while (bh = bh->b_this_page, bh != head);
	} else if (ret) {
		nr_dirty = 1 << (folio_shift(folio) - inode->i_blkbits);
	}
	spin_unlock(&mapping->i_private_lock);

	if (nr_dirty)
		nilfs_set_file_dirty(inode, nr_dirty);
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
			     loff_t pos, unsigned len,
			     struct folio **foliop, void **fsdata)

{
	struct inode *inode = mapping->host;
	int err = nilfs_transaction_begin(inode->i_sb, NULL, 1);

	if (unlikely(err))
		return err;

	err = block_write_begin(mapping, pos, len, foliop, nilfs_get_block);
	if (unlikely(err)) {
		nilfs_write_failed(mapping, pos + len);
		nilfs_transaction_abort(inode->i_sb);
	}
	return err;
}

static int nilfs_write_end(struct file *file, struct address_space *mapping,
			   loff_t pos, unsigned len, unsigned copied,
			   struct folio *folio, void *fsdata)
{
	struct inode *inode = mapping->host;
	unsigned int start = pos & (PAGE_SIZE - 1);
	unsigned int nr_dirty;
	int err;

	nr_dirty = nilfs_page_count_clean_buffers(&folio->page, start,
						  start + copied);
	copied = generic_write_end(file, mapping, pos, len, copied, folio,
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

static int nilfs_insert_inode_locked(struct inode *inode,
				     struct nilfs_root *root,
				     unsigned long ino)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = root, .cno = 0, .type = NILFS_I_TYPE_NORMAL
	};

	return insert_inode_locked4(inode, ino, nilfs_iget_test, &args);
}

struct inode *nilfs_new_inode(struct inode *dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct nilfs_inode_info *ii;
	struct nilfs_root *root;
	struct buffer_head *bh;
	int err = -ENOMEM;
	ino_t ino;

	inode = new_inode(sb);
	if (unlikely(!inode))
		goto failed;

	mapping_set_gfp_mask(inode->i_mapping,
			   mapping_gfp_constraint(inode->i_mapping, ~__GFP_FS));

	root = NILFS_I(dir)->i_root;
	ii = NILFS_I(inode);
	ii->i_state = BIT(NILFS_I_NEW);
	ii->i_type = NILFS_I_TYPE_NORMAL;
	ii->i_root = root;

	err = nilfs_ifile_create_inode(root->ifile, &ino, &bh);
	if (unlikely(err))
		goto failed_ifile_create_inode;
	/* reference count of i_bh inherits from nilfs_mdt_read_block() */
	ii->i_bh = bh;

	atomic64_inc(&root->inodes_count);
	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	inode->i_ino = ino;
	simple_inode_init_ts(inode);

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
	inode->i_generation = get_random_u32();
	if (nilfs_insert_inode_locked(inode, root, ino) < 0) {
		err = -EIO;
		goto failed_after_creation;
	}

	err = nilfs_init_acl(inode, dir);
	if (unlikely(err))
		/*
		 * Never occur.  When supporting nilfs_init_acl(),
		 * proper cancellation of above jobs should be considered.
		 */
		goto failed_after_creation;

	return inode;

 failed_after_creation:
	clear_nlink(inode);
	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);
	iput(inode);  /*
		       * raw_inode will be deleted through
		       * nilfs_evict_inode().
		       */
	goto failed;

 failed_ifile_create_inode:
	make_bad_inode(inode);
	iput(inode);
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
	inode_set_atime(inode, le64_to_cpu(raw_inode->i_mtime),
			le32_to_cpu(raw_inode->i_mtime_nsec));
	inode_set_ctime(inode, le64_to_cpu(raw_inode->i_ctime),
			le32_to_cpu(raw_inode->i_ctime_nsec));
	inode_set_mtime(inode, le64_to_cpu(raw_inode->i_mtime),
			le32_to_cpu(raw_inode->i_mtime_nsec));
	if (nilfs_is_metadata_file_inode(inode) && !S_ISREG(inode->i_mode))
		return -EIO; /* this inode is for metadata and corrupted */
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
	nilfs_ifile_unmap_inode(raw_inode);
	brelse(bh);
	up_read(&NILFS_MDT(nilfs->ns_dat)->mi_sem);
	nilfs_set_inode_flags(inode);
	mapping_set_gfp_mask(inode->i_mapping,
			   mapping_gfp_constraint(inode->i_mapping, ~__GFP_FS));
	return 0;

 failed_unmap:
	nilfs_ifile_unmap_inode(raw_inode);
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
	if (ii->i_type != args->type)
		return 0;

	return !(args->type & NILFS_I_TYPE_GC) || args->cno == ii->i_cno;
}

static int nilfs_iget_set(struct inode *inode, void *opaque)
{
	struct nilfs_iget_args *args = opaque;

	inode->i_ino = args->ino;
	NILFS_I(inode)->i_cno = args->cno;
	NILFS_I(inode)->i_root = args->root;
	NILFS_I(inode)->i_type = args->type;
	if (args->root && args->ino == NILFS_ROOT_INO)
		nilfs_get_root(args->root);
	return 0;
}

struct inode *nilfs_ilookup(struct super_block *sb, struct nilfs_root *root,
			    unsigned long ino)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = root, .cno = 0, .type = NILFS_I_TYPE_NORMAL
	};

	return ilookup5(sb, ino, nilfs_iget_test, &args);
}

struct inode *nilfs_iget_locked(struct super_block *sb, struct nilfs_root *root,
				unsigned long ino)
{
	struct nilfs_iget_args args = {
		.ino = ino, .root = root, .cno = 0, .type = NILFS_I_TYPE_NORMAL
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
		.ino = ino, .root = NULL, .cno = cno, .type = NILFS_I_TYPE_GC
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

/**
 * nilfs_attach_btree_node_cache - attach a B-tree node cache to the inode
 * @inode: inode object
 *
 * nilfs_attach_btree_node_cache() attaches a B-tree node cache to @inode,
 * or does nothing if the inode already has it.  This function allocates
 * an additional inode to maintain page cache of B-tree nodes one-on-one.
 *
 * Return Value: On success, 0 is returned. On errors, one of the following
 * negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_attach_btree_node_cache(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct inode *btnc_inode;
	struct nilfs_iget_args args;

	if (ii->i_assoc_inode)
		return 0;

	args.ino = inode->i_ino;
	args.root = ii->i_root;
	args.cno = ii->i_cno;
	args.type = ii->i_type | NILFS_I_TYPE_BTNC;

	btnc_inode = iget5_locked(inode->i_sb, inode->i_ino, nilfs_iget_test,
				  nilfs_iget_set, &args);
	if (unlikely(!btnc_inode))
		return -ENOMEM;
	if (btnc_inode->i_state & I_NEW) {
		nilfs_init_btnc_inode(btnc_inode);
		unlock_new_inode(btnc_inode);
	}
	NILFS_I(btnc_inode)->i_assoc_inode = inode;
	NILFS_I(btnc_inode)->i_bmap = ii->i_bmap;
	ii->i_assoc_inode = btnc_inode;

	return 0;
}

/**
 * nilfs_detach_btree_node_cache - detach the B-tree node cache from the inode
 * @inode: inode object
 *
 * nilfs_detach_btree_node_cache() detaches the B-tree node cache and its
 * holder inode bound to @inode, or does nothing if @inode doesn't have it.
 */
void nilfs_detach_btree_node_cache(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct inode *btnc_inode = ii->i_assoc_inode;

	if (btnc_inode) {
		NILFS_I(btnc_inode)->i_assoc_inode = NULL;
		ii->i_assoc_inode = NULL;
		iput(btnc_inode);
	}
}

/**
 * nilfs_iget_for_shadow - obtain inode for shadow mapping
 * @inode: inode object that uses shadow mapping
 *
 * nilfs_iget_for_shadow() allocates a pair of inodes that holds page
 * caches for shadow mapping.  The page cache for data pages is set up
 * in one inode and the one for b-tree node pages is set up in the
 * other inode, which is attached to the former inode.
 *
 * Return Value: On success, a pointer to the inode for data pages is
 * returned. On errors, one of the following negative error code is returned
 * in a pointer type.
 *
 * %-ENOMEM - Insufficient memory available.
 */
struct inode *nilfs_iget_for_shadow(struct inode *inode)
{
	struct nilfs_iget_args args = {
		.ino = inode->i_ino, .root = NULL, .cno = 0,
		.type = NILFS_I_TYPE_SHADOW
	};
	struct inode *s_inode;
	int err;

	s_inode = iget5_locked(inode->i_sb, inode->i_ino, nilfs_iget_test,
			       nilfs_iget_set, &args);
	if (unlikely(!s_inode))
		return ERR_PTR(-ENOMEM);
	if (!(s_inode->i_state & I_NEW))
		return inode;

	NILFS_I(s_inode)->i_flags = 0;
	memset(NILFS_I(s_inode)->i_bmap, 0, sizeof(struct nilfs_bmap));
	mapping_set_gfp_mask(s_inode->i_mapping, GFP_NOFS);

	err = nilfs_attach_btree_node_cache(s_inode);
	if (unlikely(err)) {
		iget_failed(s_inode);
		return ERR_PTR(err);
	}
	unlock_new_inode(s_inode);
	return s_inode;
}

/**
 * nilfs_write_inode_common - export common inode information to on-disk inode
 * @inode:     inode object
 * @raw_inode: on-disk inode
 *
 * This function writes standard information from the on-memory inode @inode
 * to @raw_inode on ifile, cpfile or a super root block.  Since inode bmap
 * data is not exported, nilfs_bmap_write() must be called separately during
 * log writing.
 */
void nilfs_write_inode_common(struct inode *inode,
			      struct nilfs_inode *raw_inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

	raw_inode->i_mode = cpu_to_le16(inode->i_mode);
	raw_inode->i_uid = cpu_to_le32(i_uid_read(inode));
	raw_inode->i_gid = cpu_to_le32(i_gid_read(inode));
	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
	raw_inode->i_size = cpu_to_le64(inode->i_size);
	raw_inode->i_ctime = cpu_to_le64(inode_get_ctime_sec(inode));
	raw_inode->i_mtime = cpu_to_le64(inode_get_mtime_sec(inode));
	raw_inode->i_ctime_nsec = cpu_to_le32(inode_get_ctime_nsec(inode));
	raw_inode->i_mtime_nsec = cpu_to_le32(inode_get_mtime_nsec(inode));
	raw_inode->i_blocks = cpu_to_le64(inode->i_blocks);

	raw_inode->i_flags = cpu_to_le32(ii->i_flags);
	raw_inode->i_generation = cpu_to_le32(inode->i_generation);

	/*
	 * When extending inode, nilfs->ns_inode_size should be checked
	 * for substitutions of appended fields.
	 */
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

	nilfs_write_inode_common(inode, raw_inode);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->i_device_code =
			cpu_to_le64(huge_encode_dev(inode->i_rdev));

	nilfs_ifile_unmap_inode(raw_inode);
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
	nilfs_warn(ii->vfs_inode.i_sb, "error %d truncating bmap (ino=%lu)",
		   ret, ii->vfs_inode.i_ino);
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

	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	if (IS_SYNC(inode))
		nilfs_set_transaction_flag(NILFS_TI_SYNC);

	nilfs_mark_inode_dirty(inode);
	nilfs_set_file_dirty(inode, 0);
	nilfs_transaction_commit(sb);
	/*
	 * May construct a logical segment and may fail in sync mode.
	 * But truncate has no return value.
	 */
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

	if (!(ii->i_type & NILFS_I_TYPE_BTNC))
		nilfs_detach_btree_node_cache(inode);

	if (ii->i_root && inode->i_ino == NILFS_ROOT_INO)
		nilfs_put_root(ii->i_root);
}

void nilfs_evict_inode(struct inode *inode)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = inode->i_sb;
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct the_nilfs *nilfs;
	int ret;

	if (inode->i_nlink || !ii->i_root || unlikely(is_bad_inode(inode))) {
		truncate_inode_pages_final(&inode->i_data);
		clear_inode(inode);
		nilfs_clear_inode(inode);
		return;
	}
	nilfs_transaction_begin(sb, &ti, 0); /* never fails */

	truncate_inode_pages_final(&inode->i_data);

	nilfs = sb->s_fs_info;
	if (unlikely(sb_rdonly(sb) || !nilfs->ns_writer)) {
		/*
		 * If this inode is about to be disposed after the file system
		 * has been degraded to read-only due to file system corruption
		 * or after the writer has been detached, do not make any
		 * changes that cause writes, just clear it.
		 * Do this check after read-locking ns_segctor_sem by
		 * nilfs_transaction_begin() in order to avoid a race with
		 * the writer detach operation.
		 */
		clear_inode(inode);
		nilfs_clear_inode(inode);
		nilfs_transaction_abort(sb);
		return;
	}

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
	/*
	 * May construct a logical segment and may fail in sync mode.
	 * But delete_inode has no return value.
	 */
}

int nilfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *iattr)
{
	struct nilfs_transaction_info ti;
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	int err;

	err = setattr_prepare(&nop_mnt_idmap, dentry, iattr);
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

	setattr_copy(&nop_mnt_idmap, inode, iattr);
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

int nilfs_permission(struct mnt_idmap *idmap, struct inode *inode,
		     int mask)
{
	struct nilfs_root *root = NILFS_I(inode)->i_root;

	if ((mask & MAY_WRITE) && root &&
	    root->cno != NILFS_CPTREE_CURRENT_CNO)
		return -EROFS; /* snapshot is not writable */

	return generic_permission(&nop_mnt_idmap, inode, mask);
}

int nilfs_load_inode_block(struct inode *inode, struct buffer_head **pbh)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct nilfs_inode_info *ii = NILFS_I(inode);
	int err;

	spin_lock(&nilfs->ns_inode_lock);
	if (ii->i_bh == NULL || unlikely(!buffer_uptodate(ii->i_bh))) {
		spin_unlock(&nilfs->ns_inode_lock);
		err = nilfs_ifile_get_inode_block(ii->i_root->ifile,
						  inode->i_ino, pbh);
		if (unlikely(err))
			return err;
		spin_lock(&nilfs->ns_inode_lock);
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

int nilfs_set_file_dirty(struct inode *inode, unsigned int nr_dirty)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;

	atomic_add(nr_dirty, &nilfs->ns_ndirtyblks);

	if (test_and_set_bit(NILFS_I_DIRTY, &ii->i_state))
		return 0;

	spin_lock(&nilfs->ns_inode_lock);
	if (!test_bit(NILFS_I_QUEUED, &ii->i_state) &&
	    !test_bit(NILFS_I_BUSY, &ii->i_state)) {
		/*
		 * Because this routine may race with nilfs_dispose_list(),
		 * we have to check NILFS_I_QUEUED here, too.
		 */
		if (list_empty(&ii->i_dirty) && igrab(inode) == NULL) {
			/*
			 * This will happen when somebody is freeing
			 * this inode.
			 */
			nilfs_warn(inode->i_sb,
				   "cannot set file dirty (ino=%lu): the file is being freed",
				   inode->i_ino);
			spin_unlock(&nilfs->ns_inode_lock);
			return -EINVAL; /*
					 * NILFS_I_DIRTY may remain for
					 * freeing inode.
					 */
		}
		list_move_tail(&ii->i_dirty, &nilfs->ns_dirty_files);
		set_bit(NILFS_I_QUEUED, &ii->i_state);
	}
	spin_unlock(&nilfs->ns_inode_lock);
	return 0;
}

int __nilfs_mark_inode_dirty(struct inode *inode, int flags)
{
	struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	struct buffer_head *ibh;
	int err;

	/*
	 * Do not dirty inodes after the log writer has been detached
	 * and its nilfs_root struct has been freed.
	 */
	if (unlikely(nilfs_purging(nilfs)))
		return 0;

	err = nilfs_load_inode_block(inode, &ibh);
	if (unlikely(err)) {
		nilfs_warn(inode->i_sb,
			   "cannot mark inode dirty (ino=%lu): error %d loading inode block",
			   inode->i_ino, err);
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
 * @flags: flags to determine the dirty state of the inode
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
		nilfs_warn(inode->i_sb,
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

	ret = fiemap_prep(inode, fieinfo, start, &len, 0);
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
