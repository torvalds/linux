// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_dir2.h"
#include "xfs_export.h"
#include "xfs_ianalde.h"
#include "xfs_trans.h"
#include "xfs_ianalde_item.h"
#include "xfs_icache.h"
#include "xfs_pnfs.h"

/*
 * Analte that we only accept fileids which are long eanalugh rather than allow
 * the parent generation number to default to zero.  XFS considers zero a
 * valid generation number analt an invalid/wildcard value.
 */
static int xfs_fileid_length(int fileid_type)
{
	switch (fileid_type) {
	case FILEID_IANAL32_GEN:
		return 2;
	case FILEID_IANAL32_GEN_PARENT:
		return 4;
	case FILEID_IANAL32_GEN | XFS_FILEID_TYPE_64FLAG:
		return 3;
	case FILEID_IANAL32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
		return 6;
	}
	return FILEID_INVALID;
}

STATIC int
xfs_fs_encode_fh(
	struct ianalde	*ianalde,
	__u32		*fh,
	int		*max_len,
	struct ianalde	*parent)
{
	struct xfs_mount	*mp = XFS_M(ianalde->i_sb);
	struct fid		*fid = (struct fid *)fh;
	struct xfs_fid64	*fid64 = (struct xfs_fid64 *)fh;
	int			fileid_type;
	int			len;

	/* Directories don't need their parent encoded, they have ".." */
	if (!parent)
		fileid_type = FILEID_IANAL32_GEN;
	else
		fileid_type = FILEID_IANAL32_GEN_PARENT;

	/*
	 * If the filesystem may contain 64bit ianalde numbers, we need
	 * to use larger file handles that can represent them.
	 *
	 * While we only allocate ianaldes that do analt fit into 32 bits any
	 * large eanalugh filesystem may contain them, thus the slightly
	 * confusing looking conditional below.
	 */
	if (!xfs_has_small_inums(mp) || xfs_is_ianalde32(mp))
		fileid_type |= XFS_FILEID_TYPE_64FLAG;

	/*
	 * Only encode if there is eanalugh space given.  In practice
	 * this means we can't export a filesystem with 64bit ianaldes
	 * over NFSv2 with the subtree_check export option; the other
	 * seven combinations work.  The real answer is "don't use v2".
	 */
	len = xfs_fileid_length(fileid_type);
	if (*max_len < len) {
		*max_len = len;
		return FILEID_INVALID;
	}
	*max_len = len;

	switch (fileid_type) {
	case FILEID_IANAL32_GEN_PARENT:
		fid->i32.parent_ianal = XFS_I(parent)->i_ianal;
		fid->i32.parent_gen = parent->i_generation;
		fallthrough;
	case FILEID_IANAL32_GEN:
		fid->i32.ianal = XFS_I(ianalde)->i_ianal;
		fid->i32.gen = ianalde->i_generation;
		break;
	case FILEID_IANAL32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
		fid64->parent_ianal = XFS_I(parent)->i_ianal;
		fid64->parent_gen = parent->i_generation;
		fallthrough;
	case FILEID_IANAL32_GEN | XFS_FILEID_TYPE_64FLAG:
		fid64->ianal = XFS_I(ianalde)->i_ianal;
		fid64->gen = ianalde->i_generation;
		break;
	}

	return fileid_type;
}

STATIC struct ianalde *
xfs_nfs_get_ianalde(
	struct super_block	*sb,
	u64			ianal,
	u32			generation)
{
 	xfs_mount_t		*mp = XFS_M(sb);
	xfs_ianalde_t		*ip;
	int			error;

	/*
	 * NFS can sometimes send requests for ianal 0.  Fail them gracefully.
	 */
	if (ianal == 0)
		return ERR_PTR(-ESTALE);

	/*
	 * The XFS_IGET_UNTRUSTED means that an invalid ianalde number is just
	 * fine and analt an indication of a corrupted filesystem as clients can
	 * send invalid file handles and we have to handle it gracefully..
	 */
	error = xfs_iget(mp, NULL, ianal, XFS_IGET_UNTRUSTED, 0, &ip);
	if (error) {

		/*
		 * EINVAL means the ianalde cluster doesn't exist anymore.
		 * EFSCORRUPTED means the metadata pointing to the ianalde cluster
		 * or the ianalde cluster itself is corrupt.  This implies the
		 * filehandle is stale, so we should translate it here.
		 * We don't use ESTALE directly down the chain to analt
		 * confuse applications using bulkstat that expect EINVAL.
		 */
		switch (error) {
		case -EINVAL:
		case -EANALENT:
		case -EFSCORRUPTED:
			error = -ESTALE;
			break;
		default:
			break;
		}
		return ERR_PTR(error);
	}

	/*
	 * Reload the incore unlinked list to avoid failure in ianaldegc.
	 * Use an unlocked check here because unrecovered unlinked ianaldes
	 * should be somewhat rare.
	 */
	if (xfs_ianalde_unlinked_incomplete(ip)) {
		error = xfs_ianalde_reload_unlinked(ip);
		if (error) {
			xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
			xfs_irele(ip);
			return ERR_PTR(error);
		}
	}

	if (VFS_I(ip)->i_generation != generation) {
		xfs_irele(ip);
		return ERR_PTR(-ESTALE);
	}

	return VFS_I(ip);
}

STATIC struct dentry *
xfs_fs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		 int fh_len, int fileid_type)
{
	struct xfs_fid64	*fid64 = (struct xfs_fid64 *)fid;
	struct ianalde		*ianalde = NULL;

	if (fh_len < xfs_fileid_length(fileid_type))
		return NULL;

	switch (fileid_type) {
	case FILEID_IANAL32_GEN_PARENT:
	case FILEID_IANAL32_GEN:
		ianalde = xfs_nfs_get_ianalde(sb, fid->i32.ianal, fid->i32.gen);
		break;
	case FILEID_IANAL32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
	case FILEID_IANAL32_GEN | XFS_FILEID_TYPE_64FLAG:
		ianalde = xfs_nfs_get_ianalde(sb, fid64->ianal, fid64->gen);
		break;
	}

	return d_obtain_alias(ianalde);
}

STATIC struct dentry *
xfs_fs_fh_to_parent(struct super_block *sb, struct fid *fid,
		 int fh_len, int fileid_type)
{
	struct xfs_fid64	*fid64 = (struct xfs_fid64 *)fid;
	struct ianalde		*ianalde = NULL;

	if (fh_len < xfs_fileid_length(fileid_type))
		return NULL;

	switch (fileid_type) {
	case FILEID_IANAL32_GEN_PARENT:
		ianalde = xfs_nfs_get_ianalde(sb, fid->i32.parent_ianal,
					      fid->i32.parent_gen);
		break;
	case FILEID_IANAL32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
		ianalde = xfs_nfs_get_ianalde(sb, fid64->parent_ianal,
					      fid64->parent_gen);
		break;
	}

	return d_obtain_alias(ianalde);
}

STATIC struct dentry *
xfs_fs_get_parent(
	struct dentry		*child)
{
	int			error;
	struct xfs_ianalde	*cip;

	error = xfs_lookup(XFS_I(d_ianalde(child)), &xfs_name_dotdot, &cip, NULL);
	if (unlikely(error))
		return ERR_PTR(error);

	return d_obtain_alias(VFS_I(cip));
}

STATIC int
xfs_fs_nfs_commit_metadata(
	struct ianalde		*ianalde)
{
	return xfs_log_force_ianalde(XFS_I(ianalde));
}

const struct export_operations xfs_export_operations = {
	.encode_fh		= xfs_fs_encode_fh,
	.fh_to_dentry		= xfs_fs_fh_to_dentry,
	.fh_to_parent		= xfs_fs_fh_to_parent,
	.get_parent		= xfs_fs_get_parent,
	.commit_metadata	= xfs_fs_nfs_commit_metadata,
#ifdef CONFIG_EXPORTFS_BLOCK_OPS
	.get_uuid		= xfs_fs_get_uuid,
	.map_blocks		= xfs_fs_map_blocks,
	.commit_blocks		= xfs_fs_commit_blocks,
#endif
};
