/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * export.c
 *
 * Functions to facilitate NFS exporting
 *
 * Copyright (C) 2002, 2005 Oracle.  All rights reserved.
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

#define MLOG_MASK_PREFIX ML_EXPORT
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "dir.h"
#include "dlmglue.h"
#include "export.h"
#include "inode.h"

#include "buffer_head_io.h"

struct ocfs2_inode_handle
{
	u64 ih_blkno;
	u32 ih_generation;
};

static struct dentry *ocfs2_get_dentry(struct super_block *sb, void *vobjp)
{
	struct ocfs2_inode_handle *handle = vobjp;
	struct inode *inode;
	struct dentry *result;

	mlog_entry("(0x%p, 0x%p)\n", sb, handle);

	if (handle->ih_blkno == 0) {
		mlog_errno(-ESTALE);
		return ERR_PTR(-ESTALE);
	}

	inode = ocfs2_iget(OCFS2_SB(sb), handle->ih_blkno);

	if (IS_ERR(inode)) {
		mlog_errno(PTR_ERR(inode));
		return (void *)inode;
	}

	if (handle->ih_generation != inode->i_generation) {
		iput(inode);
		mlog_errno(-ESTALE);
		return ERR_PTR(-ESTALE);
	}

	result = d_alloc_anon(inode);

	if (!result) {
		iput(inode);
		mlog_errno(-ENOMEM);
		return ERR_PTR(-ENOMEM);
	}

	mlog_exit_ptr(result);
	return result;
}

static struct dentry *ocfs2_get_parent(struct dentry *child)
{
	int status;
	u64 blkno;
	struct dentry *parent;
	struct inode *inode;
	struct inode *dir = child->d_inode;
	struct buffer_head *dirent_bh = NULL;
	struct ocfs2_dir_entry *dirent;

	mlog_entry("(0x%p, '%.*s')\n", child,
		   child->d_name.len, child->d_name.name);

	mlog(0, "find parent of directory %"MLFu64"\n",
	     OCFS2_I(dir)->ip_blkno);

	status = ocfs2_meta_lock(dir, NULL, NULL, 0);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		parent = ERR_PTR(status);
		goto bail;
	}

	status = ocfs2_find_files_on_disk("..", 2, &blkno, dir, &dirent_bh,
					  &dirent);
	if (status < 0) {
		parent = ERR_PTR(-ENOENT);
		goto bail_unlock;
	}

	inode = ocfs2_iget(OCFS2_SB(dir->i_sb), blkno);
	if (IS_ERR(inode)) {
		mlog(ML_ERROR, "Unable to create inode %"MLFu64"\n", blkno);
		parent = ERR_PTR(-EACCES);
		goto bail_unlock;
	}

	parent = d_alloc_anon(inode);
	if (!parent) {
		iput(inode);
		parent = ERR_PTR(-ENOMEM);
	}

bail_unlock:
	ocfs2_meta_unlock(dir, 0);

	if (dirent_bh)
		brelse(dirent_bh);

bail:
	mlog_exit_ptr(parent);

	return parent;
}

static int ocfs2_encode_fh(struct dentry *dentry, __be32 *fh, int *max_len,
			   int connectable)
{
	struct inode *inode = dentry->d_inode;
	int len = *max_len;
	int type = 1;
	u64 blkno;
	u32 generation;

	mlog_entry("(0x%p, '%.*s', 0x%p, %d, %d)\n", dentry,
		   dentry->d_name.len, dentry->d_name.name,
		   fh, len, connectable);

	if (len < 3 || (connectable && len < 6)) {
		mlog(ML_ERROR, "fh buffer is too small for encoding\n");
		type = 255;
		goto bail;
	}

	blkno = OCFS2_I(inode)->ip_blkno;
	generation = inode->i_generation;

	mlog(0, "Encoding fh: blkno: %"MLFu64", generation: %u\n",
	     blkno, generation);

	len = 3;
	fh[0] = cpu_to_le32((u32)(blkno >> 32));
	fh[1] = cpu_to_le32((u32)(blkno & 0xffffffff));
	fh[2] = cpu_to_le32(generation);

	if (connectable && !S_ISDIR(inode->i_mode)) {
		struct inode *parent;

		spin_lock(&dentry->d_lock);

		parent = dentry->d_parent->d_inode;
		blkno = OCFS2_I(parent)->ip_blkno;
		generation = parent->i_generation;

		fh[3] = cpu_to_le32((u32)(blkno >> 32));
		fh[4] = cpu_to_le32((u32)(blkno & 0xffffffff));
		fh[5] = cpu_to_le32(generation);

		spin_unlock(&dentry->d_lock);

		len = 6;
		type = 2;

		mlog(0, "Encoding parent: blkno: %"MLFu64", generation: %u\n",
		     blkno, generation);
	}
	
	*max_len = len;

bail:
	mlog_exit(type);
	return type;
}

static struct dentry *ocfs2_decode_fh(struct super_block *sb, __be32 *fh,
				      int fh_len, int fileid_type,
				      int (*acceptable)(void *context,
						        struct dentry *de),
				      void *context)
{
	struct ocfs2_inode_handle handle, parent;
	struct dentry *ret = NULL;

	mlog_entry("(0x%p, 0x%p, %d, %d, 0x%p, 0x%p)\n",
		   sb, fh, fh_len, fileid_type, acceptable, context);

	if (fh_len < 3 || fileid_type > 2)
		goto bail;

	if (fileid_type == 2) {
		if (fh_len < 6)
			goto bail;

		parent.ih_blkno = (u64)le32_to_cpu(fh[3]) << 32;
		parent.ih_blkno |= (u64)le32_to_cpu(fh[4]);
		parent.ih_generation = le32_to_cpu(fh[5]);

		mlog(0, "Decoding parent: blkno: %"MLFu64", generation: %u\n",
		     parent.ih_blkno, parent.ih_generation);
	}

	handle.ih_blkno = (u64)le32_to_cpu(fh[0]) << 32;
	handle.ih_blkno |= (u64)le32_to_cpu(fh[1]);
	handle.ih_generation = le32_to_cpu(fh[2]);

	mlog(0, "Encoding fh: blkno: %"MLFu64", generation: %u\n",
	     handle.ih_blkno, handle.ih_generation);

	ret = ocfs2_export_ops.find_exported_dentry(sb, &handle, &parent,
						    acceptable, context);

bail:
	mlog_exit_ptr(ret);
	return ret;
}

struct export_operations ocfs2_export_ops = {
	.decode_fh	= ocfs2_decode_fh,
	.encode_fh	= ocfs2_encode_fh,

	.get_parent	= ocfs2_get_parent,
	.get_dentry	= ocfs2_get_dentry,
};
