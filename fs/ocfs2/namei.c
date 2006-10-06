/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * namei.c
 *
 * Create and rename file, directory, symlinks
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 *  Portions of this code from linux/fs/ext3/dir.c
 *
 *  Copyright (C) 1992, 1993, 1994, 1995
 *  Remy Card (card@masi.ibp.fr)
 *  Laboratoire MASI - Institut Blaise pascal
 *  Universite Pierre et Marie Curie (Paris VI)
 *
 *   from
 *
 *   linux/fs/minix/dir.c
 *
 *   Copyright (C) 1991, 1992 Linux Torvalds
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>

#define MLOG_MASK_PREFIX ML_NAMEI
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dcache.h"
#include "dir.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "namei.h"
#include "suballoc.h"
#include "super.h"
#include "symlink.h"
#include "sysfile.h"
#include "uptodate.h"
#include "vote.h"

#include "buffer_head_io.h"

#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

static int inline ocfs2_search_dirblock(struct buffer_head *bh,
					struct inode *dir,
					const char *name, int namelen,
					unsigned long offset,
					struct ocfs2_dir_entry **res_dir);

static int ocfs2_delete_entry(struct ocfs2_journal_handle *handle,
			      struct inode *dir,
			      struct ocfs2_dir_entry *de_del,
			      struct buffer_head *bh);

static int __ocfs2_add_entry(struct ocfs2_journal_handle *handle,
			     struct inode *dir,
			     const char *name, int namelen,
			     struct inode *inode, u64 blkno,
			     struct buffer_head *parent_fe_bh,
			     struct buffer_head *insert_bh);

static int ocfs2_mknod_locked(struct ocfs2_super *osb,
			      struct inode *dir,
			      struct dentry *dentry, int mode,
			      dev_t dev,
			      struct buffer_head **new_fe_bh,
			      struct buffer_head *parent_fe_bh,
			      struct ocfs2_journal_handle *handle,
			      struct inode **ret_inode,
			      struct ocfs2_alloc_context *inode_ac);

static int ocfs2_fill_new_dir(struct ocfs2_super *osb,
			      struct ocfs2_journal_handle *handle,
			      struct inode *parent,
			      struct inode *inode,
			      struct buffer_head *fe_bh,
			      struct ocfs2_alloc_context *data_ac);

static int ocfs2_double_lock(struct ocfs2_super *osb,
			     struct ocfs2_journal_handle *handle,
			     struct buffer_head **bh1,
			     struct inode *inode1,
			     struct buffer_head **bh2,
			     struct inode *inode2);

static int ocfs2_prepare_orphan_dir(struct ocfs2_super *osb,
				    struct inode **ret_orphan_dir,
				    struct inode *inode,
				    char *name,
				    struct buffer_head **de_bh);

static int ocfs2_orphan_add(struct ocfs2_super *osb,
			    struct ocfs2_journal_handle *handle,
			    struct inode *inode,
			    struct ocfs2_dinode *fe,
			    char *name,
			    struct buffer_head *de_bh,
			    struct inode *orphan_dir_inode);

static int ocfs2_create_symlink_data(struct ocfs2_super *osb,
				     struct ocfs2_journal_handle *handle,
				     struct inode *inode,
				     const char *symname);

static inline int ocfs2_add_entry(struct ocfs2_journal_handle *handle,
				  struct dentry *dentry,
				  struct inode *inode, u64 blkno,
				  struct buffer_head *parent_fe_bh,
				  struct buffer_head *insert_bh)
{
	return __ocfs2_add_entry(handle, dentry->d_parent->d_inode,
				 dentry->d_name.name, dentry->d_name.len,
				 inode, blkno, parent_fe_bh, insert_bh);
}

/* An orphan dir name is an 8 byte value, printed as a hex string */
#define OCFS2_ORPHAN_NAMELEN ((int)(2 * sizeof(u64)))

static struct dentry *ocfs2_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	int status;
	u64 blkno;
	struct buffer_head *dirent_bh = NULL;
	struct inode *inode = NULL;
	struct dentry *ret;
	struct ocfs2_dir_entry *dirent;
	struct ocfs2_inode_info *oi;

	mlog_entry("(0x%p, 0x%p, '%.*s')\n", dir, dentry,
		   dentry->d_name.len, dentry->d_name.name);

	if (dentry->d_name.len > OCFS2_MAX_FILENAME_LEN) {
		ret = ERR_PTR(-ENAMETOOLONG);
		goto bail;
	}

	mlog(0, "find name %.*s in directory %llu\n", dentry->d_name.len,
	     dentry->d_name.name, (unsigned long long)OCFS2_I(dir)->ip_blkno);

	status = ocfs2_meta_lock(dir, NULL, NULL, 0);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		ret = ERR_PTR(status);
		goto bail;
	}

	status = ocfs2_find_files_on_disk(dentry->d_name.name,
					  dentry->d_name.len, &blkno,
					  dir, &dirent_bh, &dirent);
	if (status < 0)
		goto bail_add;

	inode = ocfs2_iget(OCFS2_SB(dir->i_sb), blkno, 0);
	if (IS_ERR(inode)) {
		mlog(ML_ERROR, "Unable to create inode %llu\n",
		     (unsigned long long)blkno);
		ret = ERR_PTR(-EACCES);
		goto bail_unlock;
	}

	oi = OCFS2_I(inode);
	/* Clear any orphaned state... If we were able to look up the
	 * inode from a directory, it certainly can't be orphaned. We
	 * might have the bad state from a node which intended to
	 * orphan this inode but crashed before it could commit the
	 * unlink. */
	spin_lock(&oi->ip_lock);
	oi->ip_flags &= ~OCFS2_INODE_MAYBE_ORPHANED;
	oi->ip_orphaned_slot = OCFS2_INVALID_SLOT;
	spin_unlock(&oi->ip_lock);

bail_add:
	dentry->d_op = &ocfs2_dentry_ops;
	ret = d_splice_alias(inode, dentry);

	if (inode) {
		/*
		 * If d_splice_alias() finds a DCACHE_DISCONNECTED
		 * dentry, it will d_move() it on top of ourse. The
		 * return value will indicate this however, so in
		 * those cases, we switch them around for the locking
		 * code.
		 *
		 * NOTE: This dentry already has ->d_op set from
		 * ocfs2_get_parent() and ocfs2_get_dentry()
		 */
		if (ret)
			dentry = ret;

		status = ocfs2_dentry_attach_lock(dentry, inode,
						  OCFS2_I(dir)->ip_blkno);
		if (status) {
			mlog_errno(status);
			ret = ERR_PTR(status);
			goto bail_unlock;
		}
	}

bail_unlock:
	/* Don't drop the cluster lock until *after* the d_add --
	 * unlink on another node will message us to remove that
	 * dentry under this lock so otherwise we can race this with
	 * the vote thread and have a stale dentry. */
	ocfs2_meta_unlock(dir, 0);

bail:
	if (dirent_bh)
		brelse(dirent_bh);

	mlog_exit_ptr(ret);

	return ret;
}

static int ocfs2_fill_new_dir(struct ocfs2_super *osb,
			      struct ocfs2_journal_handle *handle,
			      struct inode *parent,
			      struct inode *inode,
			      struct buffer_head *fe_bh,
			      struct ocfs2_alloc_context *data_ac)
{
	int status;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_dir_entry *de = NULL;

	mlog_entry_void();

	status = ocfs2_do_extend_dir(osb->sb, handle, inode, fe_bh,
				     data_ac, NULL, &new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_set_new_buffer_uptodate(inode, new_bh);

	status = ocfs2_journal_access(handle, inode, new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	memset(new_bh->b_data, 0, osb->sb->s_blocksize);

	de = (struct ocfs2_dir_entry *) new_bh->b_data;
	de->inode = cpu_to_le64(OCFS2_I(inode)->ip_blkno);
	de->name_len = 1;
	de->rec_len =
		cpu_to_le16(OCFS2_DIR_REC_LEN(de->name_len));
	strcpy(de->name, ".");
	ocfs2_set_de_type(de, S_IFDIR);
	de = (struct ocfs2_dir_entry *) ((char *)de + le16_to_cpu(de->rec_len));
	de->inode = cpu_to_le64(OCFS2_I(parent)->ip_blkno);
	de->rec_len = cpu_to_le16(inode->i_sb->s_blocksize -
				  OCFS2_DIR_REC_LEN(1));
	de->name_len = 2;
	strcpy(de->name, "..");
	ocfs2_set_de_type(de, S_IFDIR);

	status = ocfs2_journal_dirty(handle, new_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	i_size_write(inode, inode->i_sb->s_blocksize);
	inode->i_nlink = 2;
	inode->i_blocks = ocfs2_align_bytes_to_sectors(inode->i_sb->s_blocksize);
	status = ocfs2_mark_inode_dirty(handle, inode, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	if (new_bh)
		brelse(new_bh);

	mlog_exit(status);
	return status;
}

static int ocfs2_mknod(struct inode *dir,
		       struct dentry *dentry,
		       int mode,
		       dev_t dev)
{
	int status = 0;
	struct buffer_head *parent_fe_bh = NULL;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_super *osb;
	struct ocfs2_dinode *dirfe;
	struct buffer_head *new_fe_bh = NULL;
	struct buffer_head *de_bh = NULL;
	struct inode *inode = NULL;
	struct ocfs2_alloc_context *inode_ac = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;

	mlog_entry("(0x%p, 0x%p, %d, %lu, '%.*s')\n", dir, dentry, mode,
		   (unsigned long)dev, dentry->d_name.len,
		   dentry->d_name.name);

	/* get our super block */
	osb = OCFS2_SB(dir->i_sb);

	status = ocfs2_meta_lock(dir, NULL, &parent_fe_bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		return status;
	}

	handle = ocfs2_alloc_handle(osb);
	if (handle == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto leave;
	}

	if (S_ISDIR(mode) && (dir->i_nlink >= OCFS2_LINK_MAX)) {
		status = -EMLINK;
		goto leave;
	}

	dirfe = (struct ocfs2_dinode *) parent_fe_bh->b_data;
	if (!dirfe->i_links_count) {
		/* can't make a file in a deleted directory. */
		status = -ENOENT;
		goto leave;
	}

	status = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					   dentry->d_name.len);
	if (status)
		goto leave;

	/* get a spot inside the dir. */
	status = ocfs2_prepare_dir_for_insert(osb, dir, parent_fe_bh,
					      dentry->d_name.name,
					      dentry->d_name.len, &de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	/* reserve an inode spot */
	status = ocfs2_reserve_new_inode(osb, handle, &inode_ac);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	/* are we making a directory? If so, reserve a cluster for his
	 * 1st extent. */
	if (S_ISDIR(mode)) {
		status = ocfs2_reserve_clusters(osb, handle, 1, &data_ac);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto leave;
		}
	}

	handle = ocfs2_start_trans(osb, handle, OCFS2_MKNOD_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

	/* do the real work now. */
	status = ocfs2_mknod_locked(osb, dir, dentry, mode, dev,
				    &new_fe_bh, parent_fe_bh, handle,
				    &inode, inode_ac);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	if (S_ISDIR(mode)) {
		status = ocfs2_fill_new_dir(osb, handle, dir, inode,
					    new_fe_bh, data_ac);
		if (status < 0) {
			mlog_errno(status);
			goto leave;
		}

		status = ocfs2_journal_access(handle, dir, parent_fe_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto leave;
		}
		le16_add_cpu(&dirfe->i_links_count, 1);
		status = ocfs2_journal_dirty(handle, parent_fe_bh);
		if (status < 0) {
			mlog_errno(status);
			goto leave;
		}
		inc_nlink(dir);
	}

	status = ocfs2_add_entry(handle, dentry, inode,
				 OCFS2_I(inode)->ip_blkno, parent_fe_bh,
				 de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_dentry_attach_lock(dentry, inode,
					  OCFS2_I(dir)->ip_blkno);
	if (status) {
		mlog_errno(status);
		goto leave;
	}

	insert_inode_hash(inode);
	dentry->d_op = &ocfs2_dentry_ops;
	d_instantiate(dentry, inode);
	status = 0;
leave:
	if (handle)
		ocfs2_commit_trans(handle);

	ocfs2_meta_unlock(dir, 1);

	if (status == -ENOSPC)
		mlog(0, "Disk is full\n");

	if (new_fe_bh)
		brelse(new_fe_bh);

	if (de_bh)
		brelse(de_bh);

	if (parent_fe_bh)
		brelse(parent_fe_bh);

	if ((status < 0) && inode)
		iput(inode);

	if (inode_ac)
		ocfs2_free_alloc_context(inode_ac);

	if (data_ac)
		ocfs2_free_alloc_context(data_ac);

	mlog_exit(status);

	return status;
}

static int ocfs2_mknod_locked(struct ocfs2_super *osb,
			      struct inode *dir,
			      struct dentry *dentry, int mode,
			      dev_t dev,
			      struct buffer_head **new_fe_bh,
			      struct buffer_head *parent_fe_bh,
			      struct ocfs2_journal_handle *handle,
			      struct inode **ret_inode,
			      struct ocfs2_alloc_context *inode_ac)
{
	int status = 0;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_extent_list *fel;
	u64 fe_blkno = 0;
	u16 suballoc_bit;
	struct inode *inode = NULL;

	mlog_entry("(0x%p, 0x%p, %d, %lu, '%.*s')\n", dir, dentry, mode,
		   (unsigned long)dev, dentry->d_name.len,
		   dentry->d_name.name);

	*new_fe_bh = NULL;
	*ret_inode = NULL;

	status = ocfs2_claim_new_inode(osb, handle, inode_ac, &suballoc_bit,
				       &fe_blkno);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	inode = new_inode(dir->i_sb);
	if (IS_ERR(inode)) {
		status = PTR_ERR(inode);
		mlog(ML_ERROR, "new_inode failed!\n");
		goto leave;
	}

	/* populate as many fields early on as possible - many of
	 * these are used by the support functions here and in
	 * callers. */
	inode->i_ino = ino_from_blkno(osb->sb, fe_blkno);
	OCFS2_I(inode)->ip_blkno = fe_blkno;
	if (S_ISDIR(mode))
		inode->i_nlink = 2;
	else
		inode->i_nlink = 1;
	inode->i_mode = mode;
	spin_lock(&osb->osb_lock);
	inode->i_generation = osb->s_next_generation++;
	spin_unlock(&osb->osb_lock);

	*new_fe_bh = sb_getblk(osb->sb, fe_blkno);
	if (!*new_fe_bh) {
		status = -EIO;
		mlog_errno(status);
		goto leave;
	}
	ocfs2_set_new_buffer_uptodate(inode, *new_fe_bh);

	status = ocfs2_journal_access(handle, inode, *new_fe_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	fe = (struct ocfs2_dinode *) (*new_fe_bh)->b_data;
	memset(fe, 0, osb->sb->s_blocksize);

	fe->i_generation = cpu_to_le32(inode->i_generation);
	fe->i_fs_generation = cpu_to_le32(osb->fs_generation);
	fe->i_blkno = cpu_to_le64(fe_blkno);
	fe->i_suballoc_bit = cpu_to_le16(suballoc_bit);
	fe->i_suballoc_slot = cpu_to_le16(osb->slot_num);
	fe->i_uid = cpu_to_le32(current->fsuid);
	if (dir->i_mode & S_ISGID) {
		fe->i_gid = cpu_to_le32(dir->i_gid);
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		fe->i_gid = cpu_to_le32(current->fsgid);
	fe->i_mode = cpu_to_le16(mode);
	if (S_ISCHR(mode) || S_ISBLK(mode))
		fe->id1.dev1.i_rdev = cpu_to_le64(huge_encode_dev(dev));

	fe->i_links_count = cpu_to_le16(inode->i_nlink);

	fe->i_last_eb_blk = 0;
	strcpy(fe->i_signature, OCFS2_INODE_SIGNATURE);
	le32_add_cpu(&fe->i_flags, OCFS2_VALID_FL);
	fe->i_atime = fe->i_ctime = fe->i_mtime =
		cpu_to_le64(CURRENT_TIME.tv_sec);
	fe->i_mtime_nsec = fe->i_ctime_nsec = fe->i_atime_nsec =
		cpu_to_le32(CURRENT_TIME.tv_nsec);
	fe->i_dtime = 0;

	fel = &fe->id2.i_list;
	fel->l_tree_depth = 0;
	fel->l_next_free_rec = 0;
	fel->l_count = cpu_to_le16(ocfs2_extent_recs_per_inode(osb->sb));

	status = ocfs2_journal_dirty(handle, *new_fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	if (ocfs2_populate_inode(inode, fe, 1) < 0) {
		mlog(ML_ERROR, "populate inode failed! bh->b_blocknr=%llu, "
		     "i_blkno=%llu, i_ino=%lu\n",
		     (unsigned long long) (*new_fe_bh)->b_blocknr,
		     (unsigned long long)fe->i_blkno, inode->i_ino);
		BUG();
	}

	ocfs2_inode_set_new(osb, inode);
	status = ocfs2_create_new_inode_locks(inode);
	if (status < 0)
		mlog_errno(status);

	status = 0; /* error in ocfs2_create_new_inode_locks is not
		     * critical */

	*ret_inode = inode;
leave:
	if (status < 0) {
		if (*new_fe_bh) {
			brelse(*new_fe_bh);
			*new_fe_bh = NULL;
		}
		if (inode)
			iput(inode);
	}

	mlog_exit(status);
	return status;
}

static int ocfs2_mkdir(struct inode *dir,
		       struct dentry *dentry,
		       int mode)
{
	int ret;

	mlog_entry("(0x%p, 0x%p, %d, '%.*s')\n", dir, dentry, mode,
		   dentry->d_name.len, dentry->d_name.name);
	ret = ocfs2_mknod(dir, dentry, mode | S_IFDIR, 0);
	mlog_exit(ret);

	return ret;
}

static int ocfs2_create(struct inode *dir,
			struct dentry *dentry,
			int mode,
			struct nameidata *nd)
{
	int ret;

	mlog_entry("(0x%p, 0x%p, %d, '%.*s')\n", dir, dentry, mode,
		   dentry->d_name.len, dentry->d_name.name);
	ret = ocfs2_mknod(dir, dentry, mode | S_IFREG, 0);
	mlog_exit(ret);

	return ret;
}

static int ocfs2_link(struct dentry *old_dentry,
		      struct inode *dir,
		      struct dentry *dentry)
{
	struct ocfs2_journal_handle *handle;
	struct inode *inode = old_dentry->d_inode;
	int err;
	struct buffer_head *fe_bh = NULL;
	struct buffer_head *parent_fe_bh = NULL;
	struct buffer_head *de_bh = NULL;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);

	mlog_entry("(inode=%lu, old='%.*s' new='%.*s')\n", inode->i_ino,
		   old_dentry->d_name.len, old_dentry->d_name.name,
		   dentry->d_name.len, dentry->d_name.name);

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	err = ocfs2_meta_lock(dir, NULL, &parent_fe_bh, 1);
	if (err < 0) {
		if (err != -ENOENT)
			mlog_errno(err);
		return err;
	}

	if (!dir->i_nlink) {
		err = -ENOENT;
		goto out;
	}

	err = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					dentry->d_name.len);
	if (err)
		goto out;

	err = ocfs2_prepare_dir_for_insert(osb, dir, parent_fe_bh,
					   dentry->d_name.name,
					   dentry->d_name.len, &de_bh);
	if (err < 0) {
		mlog_errno(err);
		goto out;
	}

	err = ocfs2_meta_lock(inode, NULL, &fe_bh, 1);
	if (err < 0) {
		if (err != -ENOENT)
			mlog_errno(err);
		goto out;
	}

	fe = (struct ocfs2_dinode *) fe_bh->b_data;
	if (le16_to_cpu(fe->i_links_count) >= OCFS2_LINK_MAX) {
		err = -EMLINK;
		goto out_unlock_inode;
	}

	handle = ocfs2_start_trans(osb, NULL, OCFS2_LINK_CREDITS);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(err);
		goto out_unlock_inode;
	}

	err = ocfs2_journal_access(handle, inode, fe_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (err < 0) {
		mlog_errno(err);
		goto out_commit;
	}

	inc_nlink(inode);
	inode->i_ctime = CURRENT_TIME;
	fe->i_links_count = cpu_to_le16(inode->i_nlink);
	fe->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	fe->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);

	err = ocfs2_journal_dirty(handle, fe_bh);
	if (err < 0) {
		le16_add_cpu(&fe->i_links_count, -1);
		drop_nlink(inode);
		mlog_errno(err);
		goto out_commit;
	}

	err = ocfs2_add_entry(handle, dentry, inode,
			      OCFS2_I(inode)->ip_blkno,
			      parent_fe_bh, de_bh);
	if (err) {
		le16_add_cpu(&fe->i_links_count, -1);
		drop_nlink(inode);
		mlog_errno(err);
		goto out_commit;
	}

	err = ocfs2_dentry_attach_lock(dentry, inode, OCFS2_I(dir)->ip_blkno);
	if (err) {
		mlog_errno(err);
		goto out_commit;
	}

	atomic_inc(&inode->i_count);
	dentry->d_op = &ocfs2_dentry_ops;
	d_instantiate(dentry, inode);

out_commit:
	ocfs2_commit_trans(handle);
out_unlock_inode:
	ocfs2_meta_unlock(inode, 1);

out:
	ocfs2_meta_unlock(dir, 1);

	if (de_bh)
		brelse(de_bh);
	if (fe_bh)
		brelse(fe_bh);
	if (parent_fe_bh)
		brelse(parent_fe_bh);

	mlog_exit(err);

	return err;
}

/*
 * Takes and drops an exclusive lock on the given dentry. This will
 * force other nodes to drop it.
 */
static int ocfs2_remote_dentry_delete(struct dentry *dentry)
{
	int ret;

	ret = ocfs2_dentry_lock(dentry, 1);
	if (ret)
		mlog_errno(ret);
	else
		ocfs2_dentry_unlock(dentry, 1);

	return ret;
}

static inline int inode_is_unlinkable(struct inode *inode)
{
	if (S_ISDIR(inode->i_mode)) {
		if (inode->i_nlink == 2)
			return 1;
		return 0;
	}

	if (inode->i_nlink == 1)
		return 1;
	return 0;
}

static int ocfs2_unlink(struct inode *dir,
			struct dentry *dentry)
{
	int status;
	int child_locked = 0;
	struct inode *inode = dentry->d_inode;
	struct inode *orphan_dir = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	u64 blkno;
	struct ocfs2_dinode *fe = NULL;
	struct buffer_head *fe_bh = NULL;
	struct buffer_head *parent_node_bh = NULL;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_dir_entry *dirent = NULL;
	struct buffer_head *dirent_bh = NULL;
	char orphan_name[OCFS2_ORPHAN_NAMELEN + 1];
	struct buffer_head *orphan_entry_bh = NULL;

	mlog_entry("(0x%p, 0x%p, '%.*s')\n", dir, dentry,
		   dentry->d_name.len, dentry->d_name.name);

	BUG_ON(dentry->d_parent->d_inode != dir);

	mlog(0, "ino = %llu\n", (unsigned long long)OCFS2_I(inode)->ip_blkno);

	if (inode == osb->root_inode) {
		mlog(0, "Cannot delete the root directory\n");
		return -EPERM;
	}

	status = ocfs2_meta_lock(dir, NULL, &parent_node_bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		return status;
	}

	status = ocfs2_find_files_on_disk(dentry->d_name.name,
					  dentry->d_name.len, &blkno,
					  dir, &dirent_bh, &dirent);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto leave;
	}

	if (OCFS2_I(inode)->ip_blkno != blkno) {
		status = -ENOENT;

		mlog(0, "ip_blkno %llu != dirent blkno %llu ip_flags = %x\n",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno,
		     (unsigned long long)blkno, OCFS2_I(inode)->ip_flags);
		goto leave;
	}

	status = ocfs2_meta_lock(inode, NULL, &fe_bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto leave;
	}
	child_locked = 1;

	if (S_ISDIR(inode->i_mode)) {
	       	if (!ocfs2_empty_dir(inode)) {
			status = -ENOTEMPTY;
			goto leave;
		} else if (inode->i_nlink != 2) {
			status = -ENOTEMPTY;
			goto leave;
		}
	}

	status = ocfs2_remote_dentry_delete(dentry);
	if (status < 0) {
		/* This vote should succeed under all normal
		 * circumstances. */
		mlog_errno(status);
		goto leave;
	}

	if (inode_is_unlinkable(inode)) {
		status = ocfs2_prepare_orphan_dir(osb, &orphan_dir, inode,
						  orphan_name,
						  &orphan_entry_bh);
		if (status < 0) {
			mlog_errno(status);
			goto leave;
		}
	}

	handle = ocfs2_start_trans(osb, NULL, OCFS2_UNLINK_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	if (inode_is_unlinkable(inode)) {
		status = ocfs2_orphan_add(osb, handle, inode, fe, orphan_name,
					  orphan_entry_bh, orphan_dir);
		if (status < 0) {
			mlog_errno(status);
			goto leave;
		}
	}

	/* delete the name from the parent dir */
	status = ocfs2_delete_entry(handle, dir, dirent, dirent_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	if (S_ISDIR(inode->i_mode))
		drop_nlink(inode);
	drop_nlink(inode);
	fe->i_links_count = cpu_to_le16(inode->i_nlink);

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	if (S_ISDIR(inode->i_mode)) {
		drop_nlink(dir);
		status = ocfs2_mark_inode_dirty(handle, dir,
						parent_node_bh);
		if (status < 0) {
			mlog_errno(status);
			inc_nlink(dir);
		}
	}

leave:
	if (handle)
		ocfs2_commit_trans(handle);

	if (child_locked)
		ocfs2_meta_unlock(inode, 1);

	ocfs2_meta_unlock(dir, 1);

	if (orphan_dir) {
		/* This was locked for us in ocfs2_prepare_orphan_dir() */
		ocfs2_meta_unlock(orphan_dir, 1);
		mutex_unlock(&orphan_dir->i_mutex);
		iput(orphan_dir);
	}

	if (fe_bh)
		brelse(fe_bh);

	if (dirent_bh)
		brelse(dirent_bh);

	if (parent_node_bh)
		brelse(parent_node_bh);

	if (orphan_entry_bh)
		brelse(orphan_entry_bh);

	mlog_exit(status);

	return status;
}

/*
 * The only place this should be used is rename!
 * if they have the same id, then the 1st one is the only one locked.
 */
static int ocfs2_double_lock(struct ocfs2_super *osb,
			     struct ocfs2_journal_handle *handle,
			     struct buffer_head **bh1,
			     struct inode *inode1,
			     struct buffer_head **bh2,
			     struct inode *inode2)
{
	int status;
	struct ocfs2_inode_info *oi1 = OCFS2_I(inode1);
	struct ocfs2_inode_info *oi2 = OCFS2_I(inode2);
	struct buffer_head **tmpbh;
	struct inode *tmpinode;

	mlog_entry("(inode1 = %llu, inode2 = %llu)\n",
		   (unsigned long long)oi1->ip_blkno,
		   (unsigned long long)oi2->ip_blkno);

	BUG_ON(!handle);

	if (*bh1)
		*bh1 = NULL;
	if (*bh2)
		*bh2 = NULL;

	/* we always want to lock the one with the lower lockid first. */
	if (oi1->ip_blkno != oi2->ip_blkno) {
		if (oi1->ip_blkno < oi2->ip_blkno) {
			/* switch id1 and id2 around */
			mlog(0, "switching them around...\n");
			tmpbh = bh2;
			bh2 = bh1;
			bh1 = tmpbh;

			tmpinode = inode2;
			inode2 = inode1;
			inode1 = tmpinode;
		}
		/* lock id2 */
		status = ocfs2_meta_lock(inode2, handle, bh2, 1);
		if (status < 0) {
			if (status != -ENOENT)
				mlog_errno(status);
			goto bail;
		}
	}
	/* lock id1 */
	status = ocfs2_meta_lock(inode1, handle, bh1, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}
bail:
	mlog_exit(status);
	return status;
}

#define PARENT_INO(buffer) \
	((struct ocfs2_dir_entry *) \
	 ((char *)buffer + \
	  le16_to_cpu(((struct ocfs2_dir_entry *)buffer)->rec_len)))->inode

static int ocfs2_rename(struct inode *old_dir,
			struct dentry *old_dentry,
			struct inode *new_dir,
			struct dentry *new_dentry)
{
	int status = 0, rename_lock = 0;
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct inode *orphan_dir = NULL;
	struct ocfs2_dinode *newfe = NULL;
	char orphan_name[OCFS2_ORPHAN_NAMELEN + 1];
	struct buffer_head *orphan_entry_bh = NULL;
	struct buffer_head *newfe_bh = NULL;
	struct buffer_head *insert_entry_bh = NULL;
	struct ocfs2_super *osb = NULL;
	u64 newfe_blkno;
	struct ocfs2_journal_handle *handle = NULL;
	struct buffer_head *old_dir_bh = NULL;
	struct buffer_head *new_dir_bh = NULL;
	struct ocfs2_dir_entry *old_de = NULL, *new_de = NULL; // dirent for old_dentry
							       // and new_dentry
	struct buffer_head *new_de_bh = NULL, *old_de_bh = NULL; // bhs for above
	struct buffer_head *old_inode_de_bh = NULL; // if old_dentry is a dir,
						    // this is the 1st dirent bh
	nlink_t old_dir_nlink = old_dir->i_nlink, new_dir_nlink = new_dir->i_nlink;

	/* At some point it might be nice to break this function up a
	 * bit. */

	mlog_entry("(0x%p, 0x%p, 0x%p, 0x%p, from='%.*s' to='%.*s')\n",
		   old_dir, old_dentry, new_dir, new_dentry,
		   old_dentry->d_name.len, old_dentry->d_name.name,
		   new_dentry->d_name.len, new_dentry->d_name.name);

	osb = OCFS2_SB(old_dir->i_sb);

	if (new_inode) {
		if (!igrab(new_inode))
			BUG();
	}

	/* Assume a directory heirarchy thusly:
	 * a/b/c
	 * a/d
	 * a,b,c, and d are all directories.
	 *
	 * from cwd of 'a' on both nodes:
	 * node1: mv b/c d
	 * node2: mv d   b/c
	 *
	 * And that's why, just like the VFS, we need a file system
	 * rename lock. */
	if (old_dentry != new_dentry) {
		status = ocfs2_rename_lock(osb);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		rename_lock = 1;
	}

	handle = ocfs2_alloc_handle(osb);
	if (handle == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	/* if old and new are the same, this'll just do one lock. */
	status = ocfs2_double_lock(osb, handle,
				  &old_dir_bh, old_dir,
				  &new_dir_bh, new_dir);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* make sure both dirs have bhs
	 * get an extra ref on old_dir_bh if old==new */
	if (!new_dir_bh) {
		if (old_dir_bh) {
			new_dir_bh = old_dir_bh;
			get_bh(new_dir_bh);
		} else {
			mlog(ML_ERROR, "no old_dir_bh!\n");
			status = -EIO;
			goto bail;
		}
	}

	/*
	 * Though we don't require an inode meta data update if
	 * old_inode is not a directory, we lock anyway here to ensure
	 * the vote thread on other nodes won't have to concurrently
	 * downconvert the inode and the dentry locks.
	 */
	status = ocfs2_meta_lock(old_inode, handle, NULL, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}

	status = ocfs2_remote_dentry_delete(old_dentry);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (S_ISDIR(old_inode->i_mode)) {
		status = -EIO;
		old_inode_de_bh = ocfs2_bread(old_inode, 0, &status, 0);
		if (!old_inode_de_bh)
			goto bail;

		status = -EIO;
		if (le64_to_cpu(PARENT_INO(old_inode_de_bh->b_data)) !=
		    OCFS2_I(old_dir)->ip_blkno)
			goto bail;
		status = -EMLINK;
		if (!new_inode && new_dir!=old_dir &&
		    new_dir->i_nlink >= OCFS2_LINK_MAX)
			goto bail;
	}

	status = -ENOENT;
	old_de_bh = ocfs2_find_entry(old_dentry->d_name.name,
				     old_dentry->d_name.len,
				     old_dir, &old_de);
	if (!old_de_bh)
		goto bail;

	/*
	 *  Check for inode number is _not_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	if (le64_to_cpu(old_de->inode) != OCFS2_I(old_inode)->ip_blkno)
		goto bail;

	/* check if the target already exists (in which case we need
	 * to delete it */
	status = ocfs2_find_files_on_disk(new_dentry->d_name.name,
					  new_dentry->d_name.len,
					  &newfe_blkno, new_dir, &new_de_bh,
					  &new_de);
	/* The only error we allow here is -ENOENT because the new
	 * file not existing is perfectly valid. */
	if ((status < 0) && (status != -ENOENT)) {
		/* If we cannot find the file specified we should just */
		/* return the error... */
		mlog_errno(status);
		goto bail;
	}

	if (!new_de && new_inode)
		mlog(ML_ERROR, "inode %lu does not exist in it's parent "
		     "directory!", new_inode->i_ino);

	/* In case we need to overwrite an existing file, we blow it
	 * away first */
	if (new_de) {
		/* VFS didn't think there existed an inode here, but
		 * someone else in the cluster must have raced our
		 * rename to create one. Today we error cleanly, in
		 * the future we should consider calling iget to build
		 * a new struct inode for this entry. */
		if (!new_inode) {
			status = -EACCES;

			mlog(0, "We found an inode for name %.*s but VFS "
			     "didn't give us one.\n", new_dentry->d_name.len,
			     new_dentry->d_name.name);
			goto bail;
		}

		if (OCFS2_I(new_inode)->ip_blkno != newfe_blkno) {
			status = -EACCES;

			mlog(0, "Inode %llu and dir %llu disagree. flags = %x\n",
			     (unsigned long long)OCFS2_I(new_inode)->ip_blkno,
			     (unsigned long long)newfe_blkno,
			     OCFS2_I(new_inode)->ip_flags);
			goto bail;
		}

		status = ocfs2_meta_lock(new_inode, handle, &newfe_bh, 1);
		if (status < 0) {
			if (status != -ENOENT)
				mlog_errno(status);
			goto bail;
		}

		status = ocfs2_remote_dentry_delete(new_dentry);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		newfe = (struct ocfs2_dinode *) newfe_bh->b_data;

		mlog(0, "aha rename over existing... new_de=%p new_blkno=%llu "
		     "newfebh=%p bhblocknr=%llu\n", new_de,
		     (unsigned long long)newfe_blkno, newfe_bh, newfe_bh ?
		     (unsigned long long)newfe_bh->b_blocknr : 0ULL);

		if (S_ISDIR(new_inode->i_mode) || (new_inode->i_nlink == 1)) {
			status = ocfs2_prepare_orphan_dir(osb, &orphan_dir,
							  new_inode,
							  orphan_name,
							  &orphan_entry_bh);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
		}
	} else {
		BUG_ON(new_dentry->d_parent->d_inode != new_dir);

		status = ocfs2_check_dir_for_entry(new_dir,
						   new_dentry->d_name.name,
						   new_dentry->d_name.len);
		if (status)
			goto bail;

		status = ocfs2_prepare_dir_for_insert(osb, new_dir, new_dir_bh,
						      new_dentry->d_name.name,
						      new_dentry->d_name.len,
						      &insert_entry_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	handle = ocfs2_start_trans(osb, handle, OCFS2_RENAME_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	if (new_de) {
		if (S_ISDIR(new_inode->i_mode)) {
			if (!ocfs2_empty_dir(new_inode) ||
			    new_inode->i_nlink != 2) {
				status = -ENOTEMPTY;
				goto bail;
			}
		}
		status = ocfs2_journal_access(handle, new_inode, newfe_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		if (S_ISDIR(new_inode->i_mode) ||
		    (newfe->i_links_count == cpu_to_le16(1))){
			status = ocfs2_orphan_add(osb, handle, new_inode,
						  newfe, orphan_name,
						  orphan_entry_bh, orphan_dir);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
		}

		/* change the dirent to point to the correct inode */
		status = ocfs2_journal_access(handle, new_dir, new_de_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		new_de->inode = cpu_to_le64(OCFS2_I(old_inode)->ip_blkno);
		new_de->file_type = old_de->file_type;
		new_dir->i_version++;
		status = ocfs2_journal_dirty(handle, new_de_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		if (S_ISDIR(new_inode->i_mode))
			newfe->i_links_count = 0;
		else
			le16_add_cpu(&newfe->i_links_count, -1);

		status = ocfs2_journal_dirty(handle, newfe_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	} else {
		/* if the name was not found in new_dir, add it now */
		status = ocfs2_add_entry(handle, new_dentry, old_inode,
					 OCFS2_I(old_inode)->ip_blkno,
					 new_dir_bh, insert_entry_bh);
	}

	old_inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(old_inode);

	/* now that the name has been added to new_dir, remove the old name */
	status = ocfs2_delete_entry(handle, old_dir, old_de, old_de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
	}
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	if (old_inode_de_bh) {
		status = ocfs2_journal_access(handle, old_inode,
					     old_inode_de_bh,
					     OCFS2_JOURNAL_ACCESS_WRITE);
		PARENT_INO(old_inode_de_bh->b_data) =
			cpu_to_le64(OCFS2_I(new_dir)->ip_blkno);
		status = ocfs2_journal_dirty(handle, old_inode_de_bh);
		old_dir->i_nlink--;
		if (new_inode) {
			new_inode->i_nlink--;
		} else {
			inc_nlink(new_dir);
			mark_inode_dirty(new_dir);
		}
	}
	mark_inode_dirty(old_dir);
	if (new_inode)
		mark_inode_dirty(new_inode);

	if (old_dir != new_dir)
		if (new_dir_nlink != new_dir->i_nlink) {
			if (!new_dir_bh) {
				mlog(ML_ERROR, "need to change nlink for new "
				     "dir %llu from %d to %d but bh is NULL\n",
				     (unsigned long long)OCFS2_I(new_dir)->ip_blkno,
				     (int)new_dir_nlink, new_dir->i_nlink);
			} else {
				struct ocfs2_dinode *fe;
				status = ocfs2_journal_access(handle,
							      new_dir,
							      new_dir_bh,
							      OCFS2_JOURNAL_ACCESS_WRITE);
				fe = (struct ocfs2_dinode *) new_dir_bh->b_data;
				fe->i_links_count = cpu_to_le16(new_dir->i_nlink);
				status = ocfs2_journal_dirty(handle, new_dir_bh);
			}
		}

	if (old_dir_nlink != old_dir->i_nlink) {
		if (!old_dir_bh) {
			mlog(ML_ERROR, "need to change nlink for old dir "
			     "%llu from %d to %d but bh is NULL!\n",
			     (unsigned long long)OCFS2_I(old_dir)->ip_blkno,
			     (int)old_dir_nlink, old_dir->i_nlink);
		} else {
			struct ocfs2_dinode *fe;
			status = ocfs2_journal_access(handle, old_dir,
						      old_dir_bh,
						      OCFS2_JOURNAL_ACCESS_WRITE);
			fe = (struct ocfs2_dinode *) old_dir_bh->b_data;
			fe->i_links_count = cpu_to_le16(old_dir->i_nlink);
			status = ocfs2_journal_dirty(handle, old_dir_bh);
		}
	}

	ocfs2_dentry_move(old_dentry, new_dentry, old_dir, new_dir);
	status = 0;
bail:
	if (rename_lock)
		ocfs2_rename_unlock(osb);

	if (handle)
		ocfs2_commit_trans(handle);

	if (orphan_dir) {
		/* This was locked for us in ocfs2_prepare_orphan_dir() */
		ocfs2_meta_unlock(orphan_dir, 1);
		mutex_unlock(&orphan_dir->i_mutex);
		iput(orphan_dir);
	}

	if (new_inode)
		sync_mapping_buffers(old_inode->i_mapping);

	if (new_inode)
		iput(new_inode);
	if (newfe_bh)
		brelse(newfe_bh);
	if (old_dir_bh)
		brelse(old_dir_bh);
	if (new_dir_bh)
		brelse(new_dir_bh);
	if (new_de_bh)
		brelse(new_de_bh);
	if (old_de_bh)
		brelse(old_de_bh);
	if (old_inode_de_bh)
		brelse(old_inode_de_bh);
	if (orphan_entry_bh)
		brelse(orphan_entry_bh);
	if (insert_entry_bh)
		brelse(insert_entry_bh);

	mlog_exit(status);

	return status;
}

/*
 * we expect i_size = strlen(symname). Copy symname into the file
 * data, including the null terminator.
 */
static int ocfs2_create_symlink_data(struct ocfs2_super *osb,
				     struct ocfs2_journal_handle *handle,
				     struct inode *inode,
				     const char *symname)
{
	struct buffer_head **bhs = NULL;
	const char *c;
	struct super_block *sb = osb->sb;
	u64 p_blkno;
	int p_blocks;
	int virtual, blocks, status, i, bytes_left;

	bytes_left = i_size_read(inode) + 1;
	/* we can't trust i_blocks because we're actually going to
	 * write i_size + 1 bytes. */
	blocks = (bytes_left + sb->s_blocksize - 1) >> sb->s_blocksize_bits;

	mlog_entry("i_blocks = %llu, i_size = %llu, blocks = %d\n",
			(unsigned long long)inode->i_blocks,
			i_size_read(inode), blocks);

	/* Sanity check -- make sure we're going to fit. */
	if (bytes_left >
	    ocfs2_clusters_to_bytes(sb, OCFS2_I(inode)->ip_clusters)) {
		status = -EIO;
		mlog_errno(status);
		goto bail;
	}

	bhs = kcalloc(blocks, sizeof(struct buffer_head *), GFP_KERNEL);
	if (!bhs) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_extent_map_get_blocks(inode, 0, 1, &p_blkno,
					     &p_blocks);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* links can never be larger than one cluster so we know this
	 * is all going to be contiguous, but do a sanity check
	 * anyway. */
	if ((p_blocks << sb->s_blocksize_bits) < bytes_left) {
		status = -EIO;
		mlog_errno(status);
		goto bail;
	}

	virtual = 0;
	while(bytes_left > 0) {
		c = &symname[virtual * sb->s_blocksize];

		bhs[virtual] = sb_getblk(sb, p_blkno);
		if (!bhs[virtual]) {
			status = -ENOMEM;
			mlog_errno(status);
			goto bail;
		}
		ocfs2_set_new_buffer_uptodate(inode, bhs[virtual]);

		status = ocfs2_journal_access(handle, inode, bhs[virtual],
					      OCFS2_JOURNAL_ACCESS_CREATE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		memset(bhs[virtual]->b_data, 0, sb->s_blocksize);

		memcpy(bhs[virtual]->b_data, c,
		       (bytes_left > sb->s_blocksize) ? sb->s_blocksize :
		       bytes_left);

		status = ocfs2_journal_dirty(handle, bhs[virtual]);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		virtual++;
		p_blkno++;
		bytes_left -= sb->s_blocksize;
	}

	status = 0;
bail:

	if (bhs) {
		for(i = 0; i < blocks; i++)
			if (bhs[i])
				brelse(bhs[i]);
		kfree(bhs);
	}

	mlog_exit(status);
	return status;
}

static int ocfs2_symlink(struct inode *dir,
			 struct dentry *dentry,
			 const char *symname)
{
	int status, l, credits;
	u64 newsize;
	struct ocfs2_super *osb = NULL;
	struct inode *inode = NULL;
	struct super_block *sb;
	struct buffer_head *new_fe_bh = NULL;
	struct buffer_head *de_bh = NULL;
	struct buffer_head *parent_fe_bh = NULL;
	struct ocfs2_dinode *fe = NULL;
	struct ocfs2_dinode *dirfe;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_alloc_context *inode_ac = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;

	mlog_entry("(0x%p, 0x%p, symname='%s' actual='%.*s')\n", dir,
		   dentry, symname, dentry->d_name.len, dentry->d_name.name);

	sb = dir->i_sb;
	osb = OCFS2_SB(sb);

	l = strlen(symname) + 1;

	credits = ocfs2_calc_symlink_credits(sb);

	/* lock the parent directory */
	status = ocfs2_meta_lock(dir, NULL, &parent_fe_bh, 1);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		return status;
	}

	dirfe = (struct ocfs2_dinode *) parent_fe_bh->b_data;
	if (!dirfe->i_links_count) {
		/* can't make a file in a deleted directory. */
		status = -ENOENT;
		goto bail;
	}

	status = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					   dentry->d_name.len);
	if (status)
		goto bail;

	status = ocfs2_prepare_dir_for_insert(osb, dir, parent_fe_bh,
					      dentry->d_name.name,
					      dentry->d_name.len, &de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	handle = ocfs2_alloc_handle(osb);
	if (handle == NULL) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_reserve_new_inode(osb, handle, &inode_ac);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto bail;
	}

	/* don't reserve bitmap space for fast symlinks. */
	if (l > ocfs2_fast_symlink_chars(sb)) {
		status = ocfs2_reserve_clusters(osb, handle, 1, &data_ac);
		if (status < 0) {
			if (status != -ENOSPC)
				mlog_errno(status);
			goto bail;
		}
	}

	handle = ocfs2_start_trans(osb, handle, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_mknod_locked(osb, dir, dentry,
				    S_IFLNK | S_IRWXUGO, 0,
				    &new_fe_bh, parent_fe_bh, handle,
				    &inode, inode_ac);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	fe = (struct ocfs2_dinode *) new_fe_bh->b_data;
	inode->i_rdev = 0;
	newsize = l - 1;
	if (l > ocfs2_fast_symlink_chars(sb)) {
		inode->i_op = &ocfs2_symlink_inode_operations;
		status = ocfs2_do_extend_allocation(osb, inode, 1, new_fe_bh,
						    handle, data_ac, NULL,
						    NULL);
		if (status < 0) {
			if (status != -ENOSPC && status != -EINTR) {
				mlog(ML_ERROR,
				     "Failed to extend file to %llu\n",
				     (unsigned long long)newsize);
				mlog_errno(status);
				status = -ENOSPC;
			}
			goto bail;
		}
		i_size_write(inode, newsize);
		inode->i_blocks = ocfs2_align_bytes_to_sectors(newsize);
	} else {
		inode->i_op = &ocfs2_fast_symlink_inode_operations;
		memcpy((char *) fe->id2.i_symlink, symname, l);
		i_size_write(inode, newsize);
		inode->i_blocks = 0;
	}

	status = ocfs2_mark_inode_dirty(handle, inode, new_fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (!ocfs2_inode_is_fast_symlink(inode)) {
		status = ocfs2_create_symlink_data(osb, handle, inode,
						   symname);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	status = ocfs2_add_entry(handle, dentry, inode,
				 le64_to_cpu(fe->i_blkno), parent_fe_bh,
				 de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_dentry_attach_lock(dentry, inode, OCFS2_I(dir)->ip_blkno);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	insert_inode_hash(inode);
	dentry->d_op = &ocfs2_dentry_ops;
	d_instantiate(dentry, inode);
bail:
	if (handle)
		ocfs2_commit_trans(handle);

	ocfs2_meta_unlock(dir, 1);

	if (new_fe_bh)
		brelse(new_fe_bh);
	if (parent_fe_bh)
		brelse(parent_fe_bh);
	if (de_bh)
		brelse(de_bh);
	if (inode_ac)
		ocfs2_free_alloc_context(inode_ac);
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if ((status < 0) && inode)
		iput(inode);

	mlog_exit(status);

	return status;
}

int ocfs2_check_dir_entry(struct inode * dir,
			  struct ocfs2_dir_entry * de,
			  struct buffer_head * bh,
			  unsigned long offset)
{
	const char *error_msg = NULL;
	const int rlen = le16_to_cpu(de->rec_len);

	if (rlen < OCFS2_DIR_REC_LEN(1))
		error_msg = "rec_len is smaller than minimal";
	else if (rlen % 4 != 0)
		error_msg = "rec_len % 4 != 0";
	else if (rlen < OCFS2_DIR_REC_LEN(de->name_len))
		error_msg = "rec_len is too small for name_len";
	else if (((char *) de - bh->b_data) + rlen > dir->i_sb->s_blocksize)
		error_msg = "directory entry across blocks";

	if (error_msg != NULL)
		mlog(ML_ERROR, "bad entry in directory #%llu: %s - "
		     "offset=%lu, inode=%llu, rec_len=%d, name_len=%d\n",
		     (unsigned long long)OCFS2_I(dir)->ip_blkno, error_msg,
		     offset, (unsigned long long)le64_to_cpu(de->inode), rlen,
		     de->name_len);
	return error_msg == NULL ? 1 : 0;
}

/* we don't always have a dentry for what we want to add, so people
 * like orphan dir can call this instead.
 *
 * If you pass me insert_bh, I'll skip the search of the other dir
 * blocks and put the record in there.
 */
static int __ocfs2_add_entry(struct ocfs2_journal_handle *handle,
			     struct inode *dir,
			     const char *name, int namelen,
			     struct inode *inode, u64 blkno,
			     struct buffer_head *parent_fe_bh,
			     struct buffer_head *insert_bh)
{
	unsigned long offset;
	unsigned short rec_len;
	struct ocfs2_dir_entry *de, *de1;
	struct super_block *sb;
	int retval, status;

	mlog_entry_void();

	sb = dir->i_sb;

	if (!namelen)
		return -EINVAL;

	rec_len = OCFS2_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ocfs2_dir_entry *) insert_bh->b_data;
	while (1) {
		BUG_ON((char *)de >= sb->s_blocksize + insert_bh->b_data);
		/* These checks should've already been passed by the
		 * prepare function, but I guess we can leave them
		 * here anyway. */
		if (!ocfs2_check_dir_entry(dir, de, insert_bh, offset)) {
			retval = -ENOENT;
			goto bail;
		}
		if (ocfs2_match(namelen, name, de)) {
			retval = -EEXIST;
			goto bail;
		}
		if (((le64_to_cpu(de->inode) == 0) &&
		     (le16_to_cpu(de->rec_len) >= rec_len)) ||
		    (le16_to_cpu(de->rec_len) >=
		     (OCFS2_DIR_REC_LEN(de->name_len) + rec_len))) {
			status = ocfs2_journal_access(handle, dir, insert_bh,
						      OCFS2_JOURNAL_ACCESS_WRITE);
			/* By now the buffer is marked for journaling */
			offset += le16_to_cpu(de->rec_len);
			if (le64_to_cpu(de->inode)) {
				de1 = (struct ocfs2_dir_entry *)((char *) de +
					OCFS2_DIR_REC_LEN(de->name_len));
				de1->rec_len =
					cpu_to_le16(le16_to_cpu(de->rec_len) -
					OCFS2_DIR_REC_LEN(de->name_len));
				de->rec_len = cpu_to_le16(OCFS2_DIR_REC_LEN(de->name_len));
				de = de1;
			}
			de->file_type = OCFS2_FT_UNKNOWN;
			if (blkno) {
				de->inode = cpu_to_le64(blkno);
				ocfs2_set_de_type(de, inode->i_mode);
			} else
				de->inode = 0;
			de->name_len = namelen;
			memcpy(de->name, name, namelen);

			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			dir->i_version++;
			status = ocfs2_journal_dirty(handle, insert_bh);
			retval = 0;
			goto bail;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ocfs2_dir_entry *) ((char *) de + le16_to_cpu(de->rec_len));
	}

	/* when you think about it, the assert above should prevent us
	 * from ever getting here. */
	retval = -ENOSPC;
bail:

	mlog_exit(retval);
	return retval;
}


/*
 * ocfs2_delete_entry deletes a directory entry by merging it with the
 * previous entry
 */
static int ocfs2_delete_entry(struct ocfs2_journal_handle *handle,
			      struct inode *dir,
			      struct ocfs2_dir_entry *de_del,
			      struct buffer_head *bh)
{
	struct ocfs2_dir_entry *de, *pde;
	int i, status = -ENOENT;

	mlog_entry("(0x%p, 0x%p, 0x%p, 0x%p)\n", handle, dir, de_del, bh);

	i = 0;
	pde = NULL;
	de = (struct ocfs2_dir_entry *) bh->b_data;
	while (i < bh->b_size) {
		if (!ocfs2_check_dir_entry(dir, de, bh, i)) {
			status = -EIO;
			mlog_errno(status);
			goto bail;
		}
		if (de == de_del)  {
			status = ocfs2_journal_access(handle, dir, bh,
						      OCFS2_JOURNAL_ACCESS_WRITE);
			if (status < 0) {
				status = -EIO;
				mlog_errno(status);
				goto bail;
			}
			if (pde)
				pde->rec_len =
					cpu_to_le16(le16_to_cpu(pde->rec_len) +
						    le16_to_cpu(de->rec_len));
			else
				de->inode = 0;
			dir->i_version++;
			status = ocfs2_journal_dirty(handle, bh);
			goto bail;
		}
		i += le16_to_cpu(de->rec_len);
		pde = de;
		de = (struct ocfs2_dir_entry *)((char *)de + le16_to_cpu(de->rec_len));
	}
bail:
	mlog_exit(status);
	return status;
}

/*
 * Returns 0 if not found, -1 on failure, and 1 on success
 */
static int inline ocfs2_search_dirblock(struct buffer_head *bh,
					struct inode *dir,
					const char *name, int namelen,
					unsigned long offset,
					struct ocfs2_dir_entry **res_dir)
{
	struct ocfs2_dir_entry *de;
	char *dlimit, *de_buf;
	int de_len;
	int ret = 0;

	mlog_entry_void();

	de_buf = bh->b_data;
	dlimit = de_buf + dir->i_sb->s_blocksize;

	while (de_buf < dlimit) {
		/* this code is executed quadratically often */
		/* do minimal checking `by hand' */

		de = (struct ocfs2_dir_entry *) de_buf;

		if (de_buf + namelen <= dlimit &&
		    ocfs2_match(namelen, name, de)) {
			/* found a match - just to be sure, do a full check */
			if (!ocfs2_check_dir_entry(dir, de, bh, offset)) {
				ret = -1;
				goto bail;
			}
			*res_dir = de;
			ret = 1;
			goto bail;
		}

		/* prevent looping on a bad block */
		de_len = le16_to_cpu(de->rec_len);
		if (de_len <= 0) {
			ret = -1;
			goto bail;
		}

		de_buf += de_len;
		offset += de_len;
	}

bail:
	mlog_exit(ret);
	return ret;
}

struct buffer_head *ocfs2_find_entry(const char *name, int namelen,
				     struct inode *dir,
				     struct ocfs2_dir_entry **res_dir)
{
	struct super_block *sb;
	struct buffer_head *bh_use[NAMEI_RA_SIZE];
	struct buffer_head *bh, *ret = NULL;
	unsigned long start, block, b;
	int ra_max = 0;		/* Number of bh's in the readahead
				   buffer, bh_use[] */
	int ra_ptr = 0;		/* Current index into readahead
				   buffer */
	int num = 0;
	int nblocks, i, err;

	mlog_entry_void();

	*res_dir = NULL;
	sb = dir->i_sb;

	nblocks = i_size_read(dir) >> sb->s_blocksize_bits;
	start = OCFS2_I(dir)->ip_dir_start_lookup;
	if (start >= nblocks)
		start = 0;
	block = start;

restart:
	do {
		/*
		 * We deal with the read-ahead logic here.
		 */
		if (ra_ptr >= ra_max) {
			/* Refill the readahead buffer */
			ra_ptr = 0;
			b = block;
			for (ra_max = 0; ra_max < NAMEI_RA_SIZE; ra_max++) {
				/*
				 * Terminate if we reach the end of the
				 * directory and must wrap, or if our
				 * search has finished at this block.
				 */
				if (b >= nblocks || (num && block == start)) {
					bh_use[ra_max] = NULL;
					break;
				}
				num++;

				bh = ocfs2_bread(dir, b++, &err, 1);
				bh_use[ra_max] = bh;
			}
		}
		if ((bh = bh_use[ra_ptr++]) == NULL)
			goto next;
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			/* read error, skip block & hope for the best */
			ocfs2_error(dir->i_sb, "reading directory %llu, "
				    "offset %lu\n",
				    (unsigned long long)OCFS2_I(dir)->ip_blkno,
				    block);
			brelse(bh);
			goto next;
		}
		i = ocfs2_search_dirblock(bh, dir, name, namelen,
					  block << sb->s_blocksize_bits,
					  res_dir);
		if (i == 1) {
			OCFS2_I(dir)->ip_dir_start_lookup = block;
			ret = bh;
			goto cleanup_and_exit;
		} else {
			brelse(bh);
			if (i < 0)
				goto cleanup_and_exit;
		}
	next:
		if (++block >= nblocks)
			block = 0;
	} while (block != start);

	/*
	 * If the directory has grown while we were searching, then
	 * search the last part of the directory before giving up.
	 */
	block = nblocks;
	nblocks = i_size_read(dir) >> sb->s_blocksize_bits;
	if (block < nblocks) {
		start = 0;
		goto restart;
	}

cleanup_and_exit:
	/* Clean up the read-ahead blocks */
	for (; ra_ptr < ra_max; ra_ptr++)
		brelse(bh_use[ra_ptr]);

	mlog_exit_ptr(ret);
	return ret;
}

static int ocfs2_blkno_stringify(u64 blkno, char *name)
{
	int status, namelen;

	mlog_entry_void();

	namelen = snprintf(name, OCFS2_ORPHAN_NAMELEN + 1, "%016llx",
			   (long long)blkno);
	if (namelen <= 0) {
		if (namelen)
			status = namelen;
		else
			status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}
	if (namelen != OCFS2_ORPHAN_NAMELEN) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "built filename '%s' for orphan dir (len=%d)\n", name,
	     namelen);

	status = 0;
bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_prepare_orphan_dir(struct ocfs2_super *osb,
				    struct inode **ret_orphan_dir,
				    struct inode *inode,
				    char *name,
				    struct buffer_head **de_bh)
{
	struct inode *orphan_dir_inode;
	struct buffer_head *orphan_dir_bh = NULL;
	int status = 0;

	status = ocfs2_blkno_stringify(OCFS2_I(inode)->ip_blkno, name);
	if (status < 0) {
		mlog_errno(status);
		return status;
	}

	orphan_dir_inode = ocfs2_get_system_file_inode(osb,
						       ORPHAN_DIR_SYSTEM_INODE,
						       osb->slot_num);
	if (!orphan_dir_inode) {
		status = -ENOENT;
		mlog_errno(status);
		return status;
	}

	mutex_lock(&orphan_dir_inode->i_mutex);

	status = ocfs2_meta_lock(orphan_dir_inode, NULL, &orphan_dir_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_prepare_dir_for_insert(osb, orphan_dir_inode,
					      orphan_dir_bh, name,
					      OCFS2_ORPHAN_NAMELEN, de_bh);
	if (status < 0) {
		ocfs2_meta_unlock(orphan_dir_inode, 1);

		mlog_errno(status);
		goto leave;
	}

	*ret_orphan_dir = orphan_dir_inode;

leave:
	if (status) {
		mutex_unlock(&orphan_dir_inode->i_mutex);
		iput(orphan_dir_inode);
	}

	if (orphan_dir_bh)
		brelse(orphan_dir_bh);

	mlog_exit(status);
	return status;
}

static int ocfs2_orphan_add(struct ocfs2_super *osb,
			    struct ocfs2_journal_handle *handle,
			    struct inode *inode,
			    struct ocfs2_dinode *fe,
			    char *name,
			    struct buffer_head *de_bh,
			    struct inode *orphan_dir_inode)
{
	struct buffer_head *orphan_dir_bh = NULL;
	int status = 0;
	struct ocfs2_dinode *orphan_fe;

	mlog_entry("(inode->i_ino = %lu)\n", inode->i_ino);

	status = ocfs2_read_block(osb,
				  OCFS2_I(orphan_dir_inode)->ip_blkno,
				  &orphan_dir_bh, OCFS2_BH_CACHED,
				  orphan_dir_inode);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_journal_access(handle, orphan_dir_inode, orphan_dir_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	/* we're a cluster, and nlink can change on disk from
	 * underneath us... */
	orphan_fe = (struct ocfs2_dinode *) orphan_dir_bh->b_data;
	if (S_ISDIR(inode->i_mode))
		le16_add_cpu(&orphan_fe->i_links_count, 1);
	orphan_dir_inode->i_nlink = le16_to_cpu(orphan_fe->i_links_count);

	status = ocfs2_journal_dirty(handle, orphan_dir_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = __ocfs2_add_entry(handle, orphan_dir_inode, name,
				   OCFS2_ORPHAN_NAMELEN, inode,
				   OCFS2_I(inode)->ip_blkno,
				   orphan_dir_bh, de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	le32_add_cpu(&fe->i_flags, OCFS2_ORPHANED_FL);

	/* Record which orphan dir our inode now resides
	 * in. delete_inode will use this to determine which orphan
	 * dir to lock. */
	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_orphaned_slot = osb->slot_num;
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	mlog(0, "Inode %llu orphaned in slot %d\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno, osb->slot_num);

leave:
	if (orphan_dir_bh)
		brelse(orphan_dir_bh);

	mlog_exit(status);
	return status;
}

/* unlike orphan_add, we expect the orphan dir to already be locked here. */
int ocfs2_orphan_del(struct ocfs2_super *osb,
		     struct ocfs2_journal_handle *handle,
		     struct inode *orphan_dir_inode,
		     struct inode *inode,
		     struct buffer_head *orphan_dir_bh)
{
	char name[OCFS2_ORPHAN_NAMELEN + 1];
	struct ocfs2_dinode *orphan_fe;
	int status = 0;
	struct buffer_head *target_de_bh = NULL;
	struct ocfs2_dir_entry *target_de = NULL;

	mlog_entry_void();

	status = ocfs2_blkno_stringify(OCFS2_I(inode)->ip_blkno, name);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	mlog(0, "removing '%s' from orphan dir %llu (namelen=%d)\n",
	     name, (unsigned long long)OCFS2_I(orphan_dir_inode)->ip_blkno,
	     OCFS2_ORPHAN_NAMELEN);

	/* find it's spot in the orphan directory */
	target_de_bh = ocfs2_find_entry(name, OCFS2_ORPHAN_NAMELEN,
					orphan_dir_inode, &target_de);
	if (!target_de_bh) {
		status = -ENOENT;
		mlog_errno(status);
		goto leave;
	}

	/* remove it from the orphan directory */
	status = ocfs2_delete_entry(handle, orphan_dir_inode, target_de,
				    target_de_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_journal_access(handle,orphan_dir_inode,  orphan_dir_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	/* do the i_nlink dance! :) */
	orphan_fe = (struct ocfs2_dinode *) orphan_dir_bh->b_data;
	if (S_ISDIR(inode->i_mode))
		le16_add_cpu(&orphan_fe->i_links_count, -1);
	orphan_dir_inode->i_nlink = le16_to_cpu(orphan_fe->i_links_count);

	status = ocfs2_journal_dirty(handle, orphan_dir_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

leave:
	if (target_de_bh)
		brelse(target_de_bh);

	mlog_exit(status);
	return status;
}

struct inode_operations ocfs2_dir_iops = {
	.create		= ocfs2_create,
	.lookup		= ocfs2_lookup,
	.link		= ocfs2_link,
	.unlink		= ocfs2_unlink,
	.rmdir		= ocfs2_unlink,
	.symlink	= ocfs2_symlink,
	.mkdir		= ocfs2_mkdir,
	.mknod		= ocfs2_mknod,
	.rename		= ocfs2_rename,
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
};
