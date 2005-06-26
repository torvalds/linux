/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_dfrag.h"
#include "xfs_error.h"
#include "xfs_mac.h"
#include "xfs_rw.h"

/*
 * Syssgi interface for swapext
 */
int
xfs_swapext(
	xfs_swapext_t	__user *sxp)
{
	xfs_swapext_t	sx;
	xfs_inode_t     *ip=NULL, *tip=NULL, *ips[2];
	xfs_trans_t     *tp;
	xfs_mount_t     *mp;
	xfs_bstat_t	*sbp;
	struct file	*fp = NULL, *tfp = NULL;
	vnode_t		*vp, *tvp;
	bhv_desc_t      *bdp, *tbdp;
	vn_bhv_head_t   *bhp, *tbhp;
	uint		lock_flags=0;
	int		ilf_fields, tilf_fields;
	int		error = 0;
	xfs_ifork_t	tempif, *ifp, *tifp;
	__uint64_t	tmp;
	int		aforkblks = 0;
	int		taforkblks = 0;
	int		locked = 0;

	if (copy_from_user(&sx, sxp, sizeof(sx)))
		return XFS_ERROR(EFAULT);

	/* Pull information for the target fd */
	if (((fp = fget((int)sx.sx_fdtarget)) == NULL) ||
	    ((vp = LINVFS_GET_VP(fp->f_dentry->d_inode)) == NULL))  {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	bhp = VN_BHV_HEAD(vp);
	bdp = vn_bhv_lookup(bhp, &xfs_vnodeops);
	if (bdp == NULL) {
		error = XFS_ERROR(EBADF);
		goto error0;
	} else {
		ip = XFS_BHVTOI(bdp);
	}

	if (((tfp = fget((int)sx.sx_fdtmp)) == NULL) ||
	    ((tvp = LINVFS_GET_VP(tfp->f_dentry->d_inode)) == NULL)) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	tbhp = VN_BHV_HEAD(tvp);
	tbdp = vn_bhv_lookup(tbhp, &xfs_vnodeops);
	if (tbdp == NULL) {
		error = XFS_ERROR(EBADF);
		goto error0;
	} else {
		tip = XFS_BHVTOI(tbdp);
	}

	if (ip->i_mount != tip->i_mount) {
		error =  XFS_ERROR(EINVAL);
		goto error0;
	}

	if (ip->i_ino == tip->i_ino) {
		error =  XFS_ERROR(EINVAL);
		goto error0;
	}

	mp = ip->i_mount;

	sbp = &sx.sx_stat;

	if (XFS_FORCED_SHUTDOWN(mp)) {
		error =  XFS_ERROR(EIO);
		goto error0;
	}

	locked = 1;

	/* Lock in i_ino order */
	if (ip->i_ino < tip->i_ino) {
		ips[0] = ip;
		ips[1] = tip;
	} else {
		ips[0] = tip;
		ips[1] = ip;
	}
	lock_flags = XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL;
	xfs_lock_inodes(ips, 2, 0, lock_flags);

	/* Check permissions */
	error = xfs_iaccess(ip, S_IWUSR, NULL);
	if (error)
		goto error0;

	error = xfs_iaccess(tip, S_IWUSR, NULL);
	if (error)
		goto error0;

	/* Verify that both files have the same format */
	if ((ip->i_d.di_mode & S_IFMT) != (tip->i_d.di_mode & S_IFMT)) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/* Verify both files are either real-time or non-realtime */
	if ((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) !=
	    (tip->i_d.di_flags & XFS_DIFLAG_REALTIME)) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/* Should never get a local format */
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL ||
	    tip->i_d.di_format == XFS_DINODE_FMT_LOCAL) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	if (VN_CACHED(tvp) != 0) {
		xfs_inval_cached_trace(&tip->i_iocore, 0, -1, 0, -1);
		VOP_FLUSHINVAL_PAGES(tvp, 0, -1, FI_REMAPF_LOCKED);
	}

	/* Verify O_DIRECT for ftmp */
	if (VN_CACHED(tvp) != 0) {
		error = XFS_ERROR(EINVAL);
		goto error0;
	}

	/* Verify all data are being swapped */
	if (sx.sx_offset != 0 ||
	    sx.sx_length != ip->i_d.di_size ||
	    sx.sx_length != tip->i_d.di_size) {
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
	 * VOP_READ (or write in the case of autogrow) they block on the iolock
	 * until we have switched the extents.
	 */
	if (VN_MAPPED(vp)) {
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

	VOP_TOSS_PAGES(vp, 0, -1, FI_REMAPF);

	tp = xfs_trans_alloc(mp, XFS_TRANS_SWAPEXT);
	if ((error = xfs_trans_reserve(tp, 0,
				     XFS_ICHANGE_LOG_RES(mp), 0,
				     0, 0))) {
		xfs_iunlock(ip,  XFS_IOLOCK_EXCL);
		xfs_iunlock(tip, XFS_IOLOCK_EXCL);
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_lock_inodes(ips, 2, 0, XFS_ILOCK_EXCL);

	/*
	 * Count the number of extended attribute blocks
	 */
	if ( ((XFS_IFORK_Q(ip) != 0) && (ip->i_d.di_anextents > 0)) &&
	     (ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, ip, XFS_ATTR_FORK, &aforkblks);
		if (error) {
			xfs_iunlock(ip,  lock_flags);
			xfs_iunlock(tip, lock_flags);
			xfs_trans_cancel(tp, 0);
			return error;
		}
	}
	if ( ((XFS_IFORK_Q(tip) != 0) && (tip->i_d.di_anextents > 0)) &&
	     (tip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, tip, XFS_ATTR_FORK,
			&taforkblks);
		if (error) {
			xfs_iunlock(ip,  lock_flags);
			xfs_iunlock(tip, lock_flags);
			xfs_trans_cancel(tp, 0);
			return error;
		}
	}

	/*
	 * Swap the data forks of the inodes
	 */
	ifp = &ip->i_df;
	tifp = &tip->i_df;
	tempif = *ifp;	/* struct copy */
	*ifp = *tifp;	/* struct copy */
	*tifp = tempif;	/* struct copy */

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

	/*
	 * Increment vnode ref counts since xfs_trans_commit &
	 * xfs_trans_cancel will both unlock the inodes and
	 * decrement the associated ref counts.
	 */
	VN_HOLD(vp);
	VN_HOLD(tvp);

	xfs_trans_ijoin(tp, ip, lock_flags);
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

	error = xfs_trans_commit(tp, XFS_TRANS_SWAPEXT, NULL);

	fput(fp);
	fput(tfp);

	return error;

 error0:
	if (locked) {
		xfs_iunlock(ip,  lock_flags);
		xfs_iunlock(tip, lock_flags);
	}

	if (fp != NULL) fput(fp);
	if (tfp != NULL) fput(tfp);

	return error;
}
