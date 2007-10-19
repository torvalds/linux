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
 * XFS encodes and decodes the fileid portion of NFS filehandles
 * itself instead of letting the generic NFS code do it.  This
 * allows filesystems with 64 bit inode numbers to be exported.
 *
 * Note that a side effect is that xfs_vget() won't be passed a
 * zero inode/generation pair under normal circumstances.  As
 * however a malicious client could send us such data, the check
 * remains in that code.
 */

STATIC struct dentry *
xfs_fs_decode_fh(
	struct super_block	*sb,
	__u32			*fh,
	int			fh_len,
	int			fileid_type,
	int (*acceptable)(
		void		*context,
		struct dentry	*de),
	void			*context)
{
	xfs_fid_t		ifid;
	xfs_fid_t		pfid;
	void			*parent = NULL;
	int			is64 = 0;
	__u32			*p = fh;

#if XFS_BIG_INUMS
	is64 = (fileid_type & XFS_FILEID_TYPE_64FLAG);
	fileid_type &= ~XFS_FILEID_TYPE_64FLAG;
#endif

	/*
	 * Note that we only accept fileids which are long enough
	 * rather than allow the parent generation number to default
	 * to zero.  XFS considers zero a valid generation number not
	 * an invalid/wildcard value.  There's little point printk'ing
	 * a warning here as we don't have the client information
	 * which would make such a warning useful.
	 */
	if (fileid_type > 2 ||
	    fh_len < xfs_fileid_length((fileid_type == 2), is64))
		return NULL;

	p = xfs_fileid_decode_fid2(p, &ifid, is64);

	if (fileid_type == 2) {
		p = xfs_fileid_decode_fid2(p, &pfid, is64);
		parent = &pfid;
	}

	fh = (__u32 *)&ifid;
	return sb->s_export_op->find_exported_dentry(sb, fh, parent, acceptable, context);
}


STATIC int
xfs_fs_encode_fh(
	struct dentry		*dentry,
	__u32			*fh,
	int			*max_len,
	int			connectable)
{
	struct inode		*inode = dentry->d_inode;
	int			type = 1;
	__u32			*p = fh;
	int			len;
	int			is64 = 0;
#if XFS_BIG_INUMS
	if (!(XFS_M(inode->i_sb)->m_flags & XFS_MOUNT_SMALL_INUMS)) {
		/* filesystem may contain 64bit inode numbers */
		is64 = XFS_FILEID_TYPE_64FLAG;
	}
#endif

	/* Directories don't need their parent encoded, they have ".." */
	if (S_ISDIR(inode->i_mode))
	    connectable = 0;

	/*
	 * Only encode if there is enough space given.  In practice
	 * this means we can't export a filesystem with 64bit inodes
	 * over NFSv2 with the subtree_check export option; the other
	 * seven combinations work.  The real answer is "don't use v2".
	 */
	len = xfs_fileid_length(connectable, is64);
	if (*max_len < len)
		return 255;
	*max_len = len;

	p = xfs_fileid_encode_inode(p, inode, is64);
	if (connectable) {
		spin_lock(&dentry->d_lock);
		p = xfs_fileid_encode_inode(p, dentry->d_parent->d_inode, is64);
		spin_unlock(&dentry->d_lock);
		type = 2;
	}
	BUG_ON((p - fh) != len);
	return type | is64;
}

STATIC struct dentry *
xfs_fs_get_dentry(
	struct super_block	*sb,
	void			*data)
{
	bhv_vnode_t		*vp;
	struct inode		*inode;
	struct dentry		*result;
	int			error;

	error = xfs_vget(XFS_M(sb), &vp, data);
	if (error || vp == NULL)
		return ERR_PTR(-ESTALE) ;

	inode = vn_to_inode(vp);
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

struct export_operations xfs_export_operations = {
	.decode_fh		= xfs_fs_decode_fh,
	.encode_fh		= xfs_fs_encode_fh,
	.get_parent		= xfs_fs_get_parent,
	.get_dentry		= xfs_fs_get_dentry,
};
