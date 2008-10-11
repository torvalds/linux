/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_dfrag.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_vnodeops.h"

/*
 * Syssgi interface for swapext
 */
int
xfs_swapext(
	xfs_swapext_t	__user *sxu)
{
	xfs_swapext_t	*sxp;
	xfs_inode_t     *ip, *tip;
	struct file	*file, *target_file;
	int		error = 0;

	sxp = kmem_alloc(sizeof(xfs_swapext_t), KM_MAYFAIL);
	if (!sxp) {
		error = XFS_ERROR(ENOMEM);
		goto out;
	}

	if (copy_from_user(sxp, sxu, sizeof(xfs_swapext_t))) {
		error = XFS_ERROR(EFAULT);
		goto out_free_sxp;
	}

	/* Pull information for the target fd */
	file = fget((int)sxp->sx_fdtarget);
	if (!file) {
		error = XFS_ERROR(EINVAL);
		goto out_free_sxp;
	}

	if (!(file->f_mode & FMODE_WRITE) || (file->f_flags & O_APPEND)) {
		error = XFS_ERROR(EBADF);
		goto out_put_file;
	}

	target_file = fget((int)sxp->sx_fdtmp);
	if (!target_file) {
		error = XFS_ERROR(EINVAL);
		goto out_put_file;
	}

	if (!(target_file->f_mode & FMODE_WRITE) ||
	    (target_file->f_flags & O_APPEND)) {
		error = XFS_ERROR(EBADF);
		goto out_put_target_file;
	}

	ip = XFS_I(file->f_path.dentry->d_inode);
	tip = XFS_I(target_file->f_path.dentry->d_inode);

	if (ip->i_mount != tip->i_mount) {
		error = XFS_ERROR(EINVAL);
		goto out_put_target_file;
	}

	if (ip->i_ino == tip->i_ino) {
		error = XFS_ERROR(EINVAL);
		goto out_put_target_file;
	}

	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		error = XFS_ERROR(EIO);
		goto out_put_target_file;
	}

	error = xfs_swap_extents(ip, tip, sxp);

 out_put_target_file:
	fput(target_file);
 out_put_file:
	fput(file);
 out_free_sxp:
	kmem_free(sxp);
 out:
	return error;
}

int
xfs_swap_extents(
	xfs_inode_t	*ip,
	xfs_inode_t	*tip,
	xfs_swapext_t	*sxp)
{
	xfs_mount_t	*mp;
	xfs_trans_t	*tp;
	xfs_bstat_t	*sbp = &sxp->sx_stat;
	xfs_ifork_t	*tempifp, *ifp, *tifp;
	int		ilf_fields, tilf_fields;
	static uint	lock_flags = XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL;
	int		error = 0;
	int		aforkblks = 0;
	int		taforkblks = 0;
	__uint64_t	tmp;
	char		locked = 0;

	mp = ip->i_mount;

	tempifp = kmem_alloc(sizeof(xfs_ifork_t), KM_MAYFAIL);
	if (!tempifp) {
		error = XFS_ERROR(ENOMEM);
		goto error0;
	}

	sbp = &sxp->sx_stat;

	/*
	 * we have to do two separate lock calls here to keep lockdep
	 * happy. If we try to get all the locks in one call, lock will
	 * report false positives when we drop the ILOCK and regain them
	 * below.
	 */
	xfs_lock_two_inodes(ip, tip, XFS_IOLOCK_EXCL);
	xfs_lock_two_inodes(ip, tip, XFS_ILOCK_EXCL);
	locked = 1;

	/* Verify that both files have the same format */
	if ((ip->i_d.di_mode & S_IFMT) != (tip->i_d.di_mode & S_IFMT)) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/* Verify both files are either real-time or non-realtime */
	if (XFS_IS_REALTIME_INODE(ip) != XFS_IS_REALTIME_INODE(tip)) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/* Should never get a local format */
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL ||
	    tip->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	if (VN_CACHED(VFS_I(tip)) != 0) {
		xfs_inval_cached_trace(tip, 0, -1, 0, -1);
		error = xfs_flushinval_pages(tip, 0, -1,
				FI_REMAPF_LOCKED);
		if (error)
			goto error0;
	}

	/* Verify O_DIRECT for ftmp */
	if (VN_CACHED(VFS_I(tip)) != 0) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/* Verify all data are being swapped */
	if (sxp->sx_offset != 0 ||
	    sxp->sx_length != ip->i_d.di_size ||
	    sxp->sx_length != tip->i_d.di_size) {
		error = XFS_ERROR(EFAULT);
		goto error0;
	}

	/*
	 * If the target has extended attributes, the tmp file
	 * must also in order to ensure the correct data fork
	 * format.
	 */
	if ( XFS_IFORK_Q(ip) != XFS_IFORK_Q(tip) ) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/*
	 * Compare the current change & modify times with that
	 * passed in.  If they differ, we abort this swap.
	 * This is the mechanism used to ensure the calling
	 * process that the file was not changed out from
	 * under it.
	 */
	if ((sbp->bs_ctime.tv_sec != ip->i_d.di_ctime.t_sec) ||
	    (sbp->bs_ctime.tv_nsec != ip->i_d.di_ctime.t_nsec) ||
	    (sbp->bs_mtime.tv_sec != ip->i_d.di_mtime.t_sec) ||
	    (sbp->bs_mtime.tv_nsec != ip->i_d.di_mtime.t_nsec)) {
		error = XFS_ERROR(EBUSY);
		goto error0;
	}

	/* We need to fail if the file is memory mapped.  Once we have tossed
	 * all existing pages, the page fault will have no option
	 * but to go to the filesystem for pages. By making the page fault call
	 * vop_read (or write in the case of autogrow) they block on the iolock
	 * until we have switched the extents.
	 */
	if (VN_MAPPED(VFS_I(ip))) {
		error = XFS_ERROR(EBUSY);
		goto error0;
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_iunlock(tip, XFS_ILOCK_EXCL);

	/*
	 * There is a race condition here since we gave up the
	 * ilock.  However, the data fork will not change since
	 * we have the iolock (locked for truncation too) so we
	 * are safe.  We don't really care if non-io related
	 * fields change.
	 */

	xfs_tosspages(ip, 0, -1, FI_REMAPF);

	tp = xfs_trans_alloc(mp, XFS_TRANS_SWAPEXT);
	if ((error = xfs_trans_reserve(tp, 0,
				     XFS_ICHANGE_LOG_RES(mp), 0,
				     0, 0))) {
		xfs_iunlock(ip,  XFS_IOLOCK_EXCL);
		xfs_iunlock(tip, XFS_IOLOCK_EXCL);
		xfs_trans_cancel(tp, 0);
		locked = 0;
		goto error0;
	}
	xfs_lock_two_inodes(ip, tip, XFS_ILOCK_EXCL);

	/*
	 * Count the number of extended attribute blocks
	 */
	if ( ((XFS_IFORK_Q(ip) != 0) && (ip->i_d.di_anextents > 0)) &&
	     (ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, ip, XFS_ATTR_FORK, &aforkblks);
		if (error) {
			xfs_trans_cancel(tp, 0);
			goto error0;
		}
	}
	if ( ((XFS_IFORK_Q(tip) != 0) && (tip->i_d.di_anextents > 0)) &&
	     (tip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, tip, XFS_ATTR_FORK,
			&taforkblks);
		if (error) {
			xfs_trans_cancel(tp, 0);
			goto error0;
		}
	}

	/*
	 * Swap the data forks of the inodes
	 */
	ifp = &ip->i_df;
	tifp = &tip->i_df;
	*tempifp = *ifp;	/* struct copy */
	*ifp = *tifp;		/* struct copy */
	*tifp = *tempifp;	/* struct copy */

	/*
	 * Fix the on-disk inode values
	 */
	tmp = (__uint64_t)ip->i_d.di_nblocks;
	ip->i_d.di_nblocks = tip->i_d.di_nblocks - taforkblks + aforkblks;
	tip->i_d.di_nblocks = tmp + taforkblks - aforkblks;

	tmp = (__uint64_t) ip->i_d.di_nextents;
	ip->i_d.di_nextents = tip->i_d.di_nextents;
	tip->i_d.di_nextents = tmp;

	tmp = (__uint64_t) ip->i_d.di_format;
	ip->i_d.di_format = tip->i_d.di_format;
	tip->i_d.di_format = tmp;

	ilf_fields = XFS_ILOG_CORE;

	switch(ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/* If the extents fit in the inode, fix the
		 * pointer.  Otherwise it's already NULL or
		 * pointing to the extent.
		 */
		if (ip->i_d.di_nextents <= XFS_INLINE_EXTS) {
			ifp->if_u1.if_extents =
				ifp->if_u2.if_inline_ext;
		}
		ilf_fields |= XFS_ILOG_DEXT;
		break;
	case XFS_DINODE_FMT_BTREE:
		ilf_fields |= XFS_ILOG_DBROOT;
		break;
	}

	tilf_fields = XFS_ILOG_CORE;

	switch(tip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/* If the extents fit in the inode, fix the
		 * pointer.  Otherwise it's already NULL or
		 * pointing to the extent.
		 */
		if (tip->i_d.di_nextents <= XFS_INLINE_EXTS) {
			tifp->if_u1.if_extents =
				tifp->if_u2.if_inline_ext;
		}
		tilf_fields |= XFS_ILOG_DEXT;
		break;
	case XFS_DINODE_FMT_BTREE:
		tilf_fields |= XFS_ILOG_DBROOT;
		break;
	}


	IHOLD(ip);
	xfs_trans_ijoin(tp, ip, lock_flags);

	IHOLD(tip);
	xfs_trans_ijoin(tp, tip, lock_flags);

	xfs_trans_log_inode(tp, ip,  ilf_fields);
	xfs_trans_log_inode(tp, tip, tilf_fields);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_trans_commit(tp, XFS_TRANS_SWAPEXT);
	locked = 0;

 error0:
	if (locked) {
		xfs_iunlock(ip,  lock_flags);
		xfs_iunlock(tip, lock_flags);
	}
	if (tempifp != NULL)
		kmem_free(tempifp);
	return error;
}
