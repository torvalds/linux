/*
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_export.h"
#include "xfs_vnodeops.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_vfsops.h"

static struct dentry dotdot = { .d_name.name = "..", .d_name.len = 2, };

/*
 * Note that we only accept fileids which are long enough rather than allow
 * the parent generation number to default to zero.  XFS considers zero a
 * valid generation number not an invalid/wildcard value.
 */
static int xfs_fileid_length(int fileid_type)
{
	switch (fileid_type) {
	case FILEID_INO32_GEN:
		return 2;
	case FILEID_INO32_GEN_PARENT:
		return 4;
	case FILEID_INO32_GEN | XFS_FILEID_TYPE_64FLAG:
		return 3;
	case FILEID_INO32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
		return 6;
	}
	return 255; /* invalid */
}

STATIC int
xfs_fs_encode_fh(
	struct dentry		*dentry,
	__u32			*fh,
	int			*max_len,
	int			connectable)
{
	struct fid		*fid = (struct fid *)fh;
	struct xfs_fid64	*fid64 = (struct xfs_fid64 *)fh;
	struct inode		*inode = dentry->d_inode;
	int			fileid_type;
	int			len;

	/* Directories don't need their parent encoded, they have ".." */
	if (S_ISDIR(inode->i_mode))
		fileid_type = FILEID_INO32_GEN;
	else
		fileid_type = FILEID_INO32_GEN_PARENT;

	/* filesystem may contain 64bit inode numbers */
	if (!(XFS_M(inode->i_sb)->m_flags & XFS_MOUNT_SMALL_INUMS))
		fileid_type |= XFS_FILEID_TYPE_64FLAG;

	/*
	 * Only encode if there is enough space given.  In practice
	 * this means we can't export a filesystem with 64bit inodes
	 * over NFSv2 with the subtree_check export option; the other
	 * seven combinations work.  The real answer is "don't use v2".
	 */
	len = xfs_fileid_length(fileid_type);
	if (*max_len < len)
		return 255;
	*max_len = len;

	switch (fileid_type) {
	case FILEID_INO32_GEN_PARENT:
		spin_lock(&dentry->d_lock);
		fid->i32.parent_ino = dentry->d_parent->d_inode->i_ino;
		fid->i32.parent_gen = dentry->d_parent->d_inode->i_generation;
		spin_unlock(&dentry->d_lock);
		/*FALLTHRU*/
	case FILEID_INO32_GEN:
		fid->i32.ino = inode->i_ino;
		fid->i32.gen = inode->i_generation;
		break;
	case FILEID_INO32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
		spin_lock(&dentry->d_lock);
		fid64->parent_ino = dentry->d_parent->d_inode->i_ino;
		fid64->parent_gen = dentry->d_parent->d_inode->i_generation;
		spin_unlock(&dentry->d_lock);
		/*FALLTHRU*/
	case FILEID_INO32_GEN | XFS_FILEID_TYPE_64FLAG:
		fid64->ino = inode->i_ino;
		fid64->gen = inode->i_generation;
		break;
	}

	return fileid_type;
}

STATIC struct inode *
xfs_nfs_get_inode(
	struct super_block	*sb,
	u64			ino,
	u32			generation)
 {
	xfs_fid_t		xfid;
	bhv_vnode_t		*vp;
	int			error;

	xfid.fid_len = sizeof(xfs_fid_t) - sizeof(xfid.fid_len);
	xfid.fid_pad = 0;
	xfid.fid_ino = ino;
	xfid.fid_gen = generation;

	error = xfs_vget(XFS_M(sb), &vp, &xfid);
	if (error)
		return ERR_PTR(-error);

	return vp ? vn_to_inode(vp) : NULL;
}

STATIC struct dentry *
xfs_fs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		 int fh_len, int fileid_type)
{
	struct xfs_fid64	*fid64 = (struct xfs_fid64 *)fid;
	struct inode		*inode = NULL;
	struct dentry		*result;

	if (fh_len < xfs_fileid_length(fileid_type))
		return NULL;

	switch (fileid_type) {
	case FILEID_INO32_GEN_PARENT:
	case FILEID_INO32_GEN:
		inode = xfs_nfs_get_inode(sb, fid->i32.ino, fid->i32.gen);
		break;
	case FILEID_INO32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
	case FILEID_INO32_GEN | XFS_FILEID_TYPE_64FLAG:
		inode = xfs_nfs_get_inode(sb, fid64->ino, fid64->gen);
		break;
	}

	if (!inode)
		return NULL;
	if (IS_ERR(inode))
		return ERR_PTR(PTR_ERR(inode));
	result = d_alloc_anon(inode);
	if (!result) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	return result;
}

STATIC struct dentry *
xfs_fs_fh_to_parent(struct super_block *sb, struct fid *fid,
		 int fh_len, int fileid_type)
{
	struct xfs_fid64	*fid64 = (struct xfs_fid64 *)fid;
	struct inode		*inode = NULL;
	struct dentry		*result;

	switch (fileid_type) {
	case FILEID_INO32_GEN_PARENT:
		inode = xfs_nfs_get_inode(sb, fid->i32.parent_ino,
					      fid->i32.parent_gen);
		break;
	case FILEID_INO32_GEN_PARENT | XFS_FILEID_TYPE_64FLAG:
		inode = xfs_nfs_get_inode(sb, fid64->parent_ino,
					      fid64->parent_gen);
		break;
	}

	if (!inode)
		return NULL;
	if (IS_ERR(inode))
		return ERR_PTR(PTR_ERR(inode));
	result = d_alloc_anon(inode);
	if (!result) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	return result;
}

STATIC struct dentry *
xfs_fs_get_parent(
	struct dentry		*child)
{
	int			error;
	bhv_vnode_t		*cvp;
	struct dentry		*parent;

	cvp = NULL;
	error = xfs_lookup(XFS_I(child->d_inode), &dotdot, &cvp);
	if (unlikely(error))
		return ERR_PTR(-error);

	parent = d_alloc_anon(vn_to_inode(cvp));
	if (unlikely(!parent)) {
		VN_RELE(cvp);
		return ERR_PTR(-ENOMEM);
	}
	return parent;
}

const struct export_operations xfs_export_operations = {
	.encode_fh		= xfs_fs_encode_fh,
	.fh_to_dentry		= xfs_fs_fh_to_dentry,
	.fh_to_parent		= xfs_fs_fh_to_parent,
	.get_parent		= xfs_fs_get_parent,
};
