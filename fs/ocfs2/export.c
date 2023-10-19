// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * export.c
 *
 * Functions to facilitate NFS exporting
 *
 * Copyright (C) 2002, 2005 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dir.h"
#include "dlmglue.h"
#include "dcache.h"
#include "export.h"
#include "inode.h"

#include "buffer_head_io.h"
#include "suballoc.h"
#include "ocfs2_trace.h"

struct ocfs2_inode_handle
{
	u64 ih_blkno;
	u32 ih_generation;
};

static struct dentry *ocfs2_get_dentry(struct super_block *sb,
		struct ocfs2_inode_handle *handle)
{
	struct inode *inode;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	u64 blkno = handle->ih_blkno;
	int status, set;
	struct dentry *result;

	trace_ocfs2_get_dentry_begin(sb, handle, (unsigned long long)blkno);

	if (blkno == 0) {
		result = ERR_PTR(-ESTALE);
		goto bail;
	}

	inode = ocfs2_ilookup(sb, blkno);
	/*
	 * If the inode exists in memory, we only need to check it's
	 * generation number
	 */
	if (inode)
		goto check_gen;

	/*
	 * This will synchronize us against ocfs2_delete_inode() on
	 * all nodes
	 */
	status = ocfs2_nfs_sync_lock(osb, 1);
	if (status < 0) {
		mlog(ML_ERROR, "getting nfs sync lock(EX) failed %d\n", status);
		goto check_err;
	}

	status = ocfs2_test_inode_bit(osb, blkno, &set);
	if (status < 0) {
		if (status == -EINVAL) {
			/*
			 * The blkno NFS gave us doesn't even show up
			 * as an inode, we return -ESTALE to be
			 * nice
			 */
			status = -ESTALE;
		} else
			mlog(ML_ERROR, "test inode bit failed %d\n", status);
		goto unlock_nfs_sync;
	}

	trace_ocfs2_get_dentry_test_bit(status, set);
	/* If the inode allocator bit is clear, this inode must be stale */
	if (!set) {
		status = -ESTALE;
		goto unlock_nfs_sync;
	}

	inode = ocfs2_iget(osb, blkno, 0, 0);

unlock_nfs_sync:
	ocfs2_nfs_sync_unlock(osb, 1);

check_err:
	if (status < 0) {
		if (status == -ESTALE) {
			trace_ocfs2_get_dentry_stale((unsigned long long)blkno,
						     handle->ih_generation);
		}
		result = ERR_PTR(status);
		goto bail;
	}

	if (IS_ERR(inode)) {
		mlog_errno(PTR_ERR(inode));
		result = ERR_CAST(inode);
		goto bail;
	}

check_gen:
	if (handle->ih_generation != inode->i_generation) {
		trace_ocfs2_get_dentry_generation((unsigned long long)blkno,
						  handle->ih_generation,
						  inode->i_generation);
		iput(inode);
		result = ERR_PTR(-ESTALE);
		goto bail;
	}

	result = d_obtain_alias(inode);
	if (IS_ERR(result))
		mlog_errno(PTR_ERR(result));

bail:
	trace_ocfs2_get_dentry_end(result);
	return result;
}

static struct dentry *ocfs2_get_parent(struct dentry *child)
{
	int status;
	u64 blkno;
	struct dentry *parent;
	struct inode *dir = d_inode(child);
	int set;

	trace_ocfs2_get_parent(child, child->d_name.len, child->d_name.name,
			       (unsigned long long)OCFS2_I(dir)->ip_blkno);

	status = ocfs2_nfs_sync_lock(OCFS2_SB(dir->i_sb), 1);
	if (status < 0) {
		mlog(ML_ERROR, "getting nfs sync lock(EX) failed %d\n", status);
		parent = ERR_PTR(status);
		goto bail;
	}

	status = ocfs2_inode_lock(dir, NULL, 0);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		parent = ERR_PTR(status);
		goto unlock_nfs_sync;
	}

	status = ocfs2_lookup_ino_from_name(dir, "..", 2, &blkno);
	if (status < 0) {
		parent = ERR_PTR(-ENOENT);
		goto bail_unlock;
	}

	status = ocfs2_test_inode_bit(OCFS2_SB(dir->i_sb), blkno, &set);
	if (status < 0) {
		if (status == -EINVAL) {
			status = -ESTALE;
		} else
			mlog(ML_ERROR, "test inode bit failed %d\n", status);
		parent = ERR_PTR(status);
		goto bail_unlock;
	}

	trace_ocfs2_get_dentry_test_bit(status, set);
	if (!set) {
		status = -ESTALE;
		parent = ERR_PTR(status);
		goto bail_unlock;
	}

	parent = d_obtain_alias(ocfs2_iget(OCFS2_SB(dir->i_sb), blkno, 0, 0));

bail_unlock:
	ocfs2_inode_unlock(dir, 0);

unlock_nfs_sync:
	ocfs2_nfs_sync_unlock(OCFS2_SB(dir->i_sb), 1);

bail:
	trace_ocfs2_get_parent_end(parent);

	return parent;
}

static int ocfs2_encode_fh(struct inode *inode, u32 *fh_in, int *max_len,
			   struct inode *parent)
{
	int len = *max_len;
	int type = 1;
	u64 blkno;
	u32 generation;
	__le32 *fh = (__force __le32 *) fh_in;

#ifdef TRACE_HOOKS_ARE_NOT_BRAINDEAD_IN_YOUR_OPINION
#error "You go ahead and fix that mess, then.  Somehow"
	trace_ocfs2_encode_fh_begin(dentry, dentry->d_name.len,
				    dentry->d_name.name,
				    fh, len, connectable);
#endif

	if (parent && (len < 6)) {
		*max_len = 6;
		type = FILEID_INVALID;
		goto bail;
	} else if (len < 3) {
		*max_len = 3;
		type = FILEID_INVALID;
		goto bail;
	}

	blkno = OCFS2_I(inode)->ip_blkno;
	generation = inode->i_generation;

	trace_ocfs2_encode_fh_self((unsigned long long)blkno, generation);

	len = 3;
	fh[0] = cpu_to_le32((u32)(blkno >> 32));
	fh[1] = cpu_to_le32((u32)(blkno & 0xffffffff));
	fh[2] = cpu_to_le32(generation);

	if (parent) {
		blkno = OCFS2_I(parent)->ip_blkno;
		generation = parent->i_generation;

		fh[3] = cpu_to_le32((u32)(blkno >> 32));
		fh[4] = cpu_to_le32((u32)(blkno & 0xffffffff));
		fh[5] = cpu_to_le32(generation);

		len = 6;
		type = 2;

		trace_ocfs2_encode_fh_parent((unsigned long long)blkno,
					     generation);
	}

	*max_len = len;

bail:
	trace_ocfs2_encode_fh_type(type);
	return type;
}

static struct dentry *ocfs2_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct ocfs2_inode_handle handle;

	if (fh_len < 3 || fh_type > 2)
		return NULL;

	handle.ih_blkno = (u64)le32_to_cpu(fid->raw[0]) << 32;
	handle.ih_blkno |= (u64)le32_to_cpu(fid->raw[1]);
	handle.ih_generation = le32_to_cpu(fid->raw[2]);
	return ocfs2_get_dentry(sb, &handle);
}

static struct dentry *ocfs2_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct ocfs2_inode_handle parent;

	if (fh_type != 2 || fh_len < 6)
		return NULL;

	parent.ih_blkno = (u64)le32_to_cpu(fid->raw[3]) << 32;
	parent.ih_blkno |= (u64)le32_to_cpu(fid->raw[4]);
	parent.ih_generation = le32_to_cpu(fid->raw[5]);
	return ocfs2_get_dentry(sb, &parent);
}

const struct export_operations ocfs2_export_ops = {
	.encode_fh	= ocfs2_encode_fh,
	.fh_to_dentry	= ocfs2_fh_to_dentry,
	.fh_to_parent	= ocfs2_fh_to_parent,
	.get_parent	= ocfs2_get_parent,
};
