// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
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
#include "iyesde.h"

#include "buffer_head_io.h"
#include "suballoc.h"
#include "ocfs2_trace.h"

struct ocfs2_iyesde_handle
{
	u64 ih_blkyes;
	u32 ih_generation;
};

static struct dentry *ocfs2_get_dentry(struct super_block *sb,
		struct ocfs2_iyesde_handle *handle)
{
	struct iyesde *iyesde;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	u64 blkyes = handle->ih_blkyes;
	int status, set;
	struct dentry *result;

	trace_ocfs2_get_dentry_begin(sb, handle, (unsigned long long)blkyes);

	if (blkyes == 0) {
		result = ERR_PTR(-ESTALE);
		goto bail;
	}

	iyesde = ocfs2_ilookup(sb, blkyes);
	/*
	 * If the iyesde exists in memory, we only need to check it's
	 * generation number
	 */
	if (iyesde)
		goto check_gen;

	/*
	 * This will synchronize us against ocfs2_delete_iyesde() on
	 * all yesdes
	 */
	status = ocfs2_nfs_sync_lock(osb, 1);
	if (status < 0) {
		mlog(ML_ERROR, "getting nfs sync lock(EX) failed %d\n", status);
		goto check_err;
	}

	status = ocfs2_test_iyesde_bit(osb, blkyes, &set);
	if (status < 0) {
		if (status == -EINVAL) {
			/*
			 * The blkyes NFS gave us doesn't even show up
			 * as an iyesde, we return -ESTALE to be
			 * nice
			 */
			status = -ESTALE;
		} else
			mlog(ML_ERROR, "test iyesde bit failed %d\n", status);
		goto unlock_nfs_sync;
	}

	trace_ocfs2_get_dentry_test_bit(status, set);
	/* If the iyesde allocator bit is clear, this iyesde must be stale */
	if (!set) {
		status = -ESTALE;
		goto unlock_nfs_sync;
	}

	iyesde = ocfs2_iget(osb, blkyes, 0, 0);

unlock_nfs_sync:
	ocfs2_nfs_sync_unlock(osb, 1);

check_err:
	if (status < 0) {
		if (status == -ESTALE) {
			trace_ocfs2_get_dentry_stale((unsigned long long)blkyes,
						     handle->ih_generation);
		}
		result = ERR_PTR(status);
		goto bail;
	}

	if (IS_ERR(iyesde)) {
		mlog_erryes(PTR_ERR(iyesde));
		result = ERR_CAST(iyesde);
		goto bail;
	}

check_gen:
	if (handle->ih_generation != iyesde->i_generation) {
		trace_ocfs2_get_dentry_generation((unsigned long long)blkyes,
						  handle->ih_generation,
						  iyesde->i_generation);
		iput(iyesde);
		result = ERR_PTR(-ESTALE);
		goto bail;
	}

	result = d_obtain_alias(iyesde);
	if (IS_ERR(result))
		mlog_erryes(PTR_ERR(result));

bail:
	trace_ocfs2_get_dentry_end(result);
	return result;
}

static struct dentry *ocfs2_get_parent(struct dentry *child)
{
	int status;
	u64 blkyes;
	struct dentry *parent;
	struct iyesde *dir = d_iyesde(child);
	int set;

	trace_ocfs2_get_parent(child, child->d_name.len, child->d_name.name,
			       (unsigned long long)OCFS2_I(dir)->ip_blkyes);

	status = ocfs2_nfs_sync_lock(OCFS2_SB(dir->i_sb), 1);
	if (status < 0) {
		mlog(ML_ERROR, "getting nfs sync lock(EX) failed %d\n", status);
		parent = ERR_PTR(status);
		goto bail;
	}

	status = ocfs2_iyesde_lock(dir, NULL, 0);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_erryes(status);
		parent = ERR_PTR(status);
		goto unlock_nfs_sync;
	}

	status = ocfs2_lookup_iyes_from_name(dir, "..", 2, &blkyes);
	if (status < 0) {
		parent = ERR_PTR(-ENOENT);
		goto bail_unlock;
	}

	status = ocfs2_test_iyesde_bit(OCFS2_SB(dir->i_sb), blkyes, &set);
	if (status < 0) {
		if (status == -EINVAL) {
			status = -ESTALE;
		} else
			mlog(ML_ERROR, "test iyesde bit failed %d\n", status);
		parent = ERR_PTR(status);
		goto bail_unlock;
	}

	trace_ocfs2_get_dentry_test_bit(status, set);
	if (!set) {
		status = -ESTALE;
		parent = ERR_PTR(status);
		goto bail_unlock;
	}

	parent = d_obtain_alias(ocfs2_iget(OCFS2_SB(dir->i_sb), blkyes, 0, 0));

bail_unlock:
	ocfs2_iyesde_unlock(dir, 0);

unlock_nfs_sync:
	ocfs2_nfs_sync_unlock(OCFS2_SB(dir->i_sb), 1);

bail:
	trace_ocfs2_get_parent_end(parent);

	return parent;
}

static int ocfs2_encode_fh(struct iyesde *iyesde, u32 *fh_in, int *max_len,
			   struct iyesde *parent)
{
	int len = *max_len;
	int type = 1;
	u64 blkyes;
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

	blkyes = OCFS2_I(iyesde)->ip_blkyes;
	generation = iyesde->i_generation;

	trace_ocfs2_encode_fh_self((unsigned long long)blkyes, generation);

	len = 3;
	fh[0] = cpu_to_le32((u32)(blkyes >> 32));
	fh[1] = cpu_to_le32((u32)(blkyes & 0xffffffff));
	fh[2] = cpu_to_le32(generation);

	if (parent) {
		blkyes = OCFS2_I(parent)->ip_blkyes;
		generation = parent->i_generation;

		fh[3] = cpu_to_le32((u32)(blkyes >> 32));
		fh[4] = cpu_to_le32((u32)(blkyes & 0xffffffff));
		fh[5] = cpu_to_le32(generation);

		len = 6;
		type = 2;

		trace_ocfs2_encode_fh_parent((unsigned long long)blkyes,
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
	struct ocfs2_iyesde_handle handle;

	if (fh_len < 3 || fh_type > 2)
		return NULL;

	handle.ih_blkyes = (u64)le32_to_cpu(fid->raw[0]) << 32;
	handle.ih_blkyes |= (u64)le32_to_cpu(fid->raw[1]);
	handle.ih_generation = le32_to_cpu(fid->raw[2]);
	return ocfs2_get_dentry(sb, &handle);
}

static struct dentry *ocfs2_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct ocfs2_iyesde_handle parent;

	if (fh_type != 2 || fh_len < 6)
		return NULL;

	parent.ih_blkyes = (u64)le32_to_cpu(fid->raw[3]) << 32;
	parent.ih_blkyes |= (u64)le32_to_cpu(fid->raw[4]);
	parent.ih_generation = le32_to_cpu(fid->raw[5]);
	return ocfs2_get_dentry(sb, &parent);
}

const struct export_operations ocfs2_export_ops = {
	.encode_fh	= ocfs2_encode_fh,
	.fh_to_dentry	= ocfs2_fh_to_dentry,
	.fh_to_parent	= ocfs2_fh_to_parent,
	.get_parent	= ocfs2_get_parent,
};
