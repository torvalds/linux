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
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_itable.h"
#include "xfs_dfrag.h"
#include "xfs_error.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"


static int xfs_swap_extents(
	xfs_inode_t	*ip,	/* target inode */
	xfs_inode_t	*tip,	/* tmp inode */
	xfs_swapext_t	*sxp);

/*
 * ioctl interface for swapext
 */
int
xfs_swapext(
	xfs_swapext_t	*sxp)
{
	xfs_inode_t     *ip, *tip;
	struct fd	f, tmp;
	int		error = 0;

	/* Pull information for the target fd */
	f = fdget((int)sxp->sx_fdtarget);
	if (!f.file) {
		error = XFS_ERROR(EINVAL);
		goto out;
	}

	if (!(f.file->f_mode & FMODE_WRITE) ||
	    !(f.file->f_mode & FMODE_READ) ||
	    (f.file->f_flags & O_APPEND)) {
		error = XFS_ERROR(EBADF);
		goto out_put_file;
	}

	tmp = fdget((int)sxp->sx_fdtmp);
	if (!tmp.file) {
		error = XFS_ERROR(EINVAL);
		goto out_put_file;
	}

	if (!(tmp.file->f_mode & FMODE_WRITE) ||
	    !(tmp.file->f_mode & FMODE_READ) ||
	    (tmp.file->f_flags & O_APPEND)) {
		error = XFS_ERROR(EBADF);
		goto out_put_tmp_file;
	}

	if (IS_SWAPFILE(f.file->f_path.dentry->d_inode) ||
	    IS_SWAPFILE(tmp.file->f_path.dentry->d_inode)) {
		error = XFS_ERROR(EINVAL);
		goto out_put_tmp_file;
	}

	ip = XFS_I(f.file->f_path.dentry->d_inode);
	tip = XFS_I(tmp.file->f_path.dentry->d_inode);

	if (ip->i_mount != tip->i_mount) {
		error = XFS_ERROR(EINVAL);
		goto out_put_tmp_file;
	}

	if (ip->i_ino == tip->i_ino) {
		error = XFS_ERROR(EINVAL);
		goto out_put_tmp_file;
	}

	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		error = XFS_ERROR(EIO);
		goto out_put_tmp_file;
	}

	error = xfs_swap_extents(ip, tip, sxp);

 out_put_tmp_file:
	fdput(tmp);
 out_put_file:
	fdput(f);
 out:
	return error;
}

/*
 * We need to check that the format of the data fork in the temporary inode is
 * valid for the target inode before doing the swap. This is not a problem with
 * attr1 because of the fixed fork offset, but attr2 has a dynamically sized
 * data fork depending on the space the attribute fork is taking so we can get
 * invalid formats on the target inode.
 *
 * E.g. target has space for 7 extents in extent format, temp inode only has
 * space for 6.  If we defragment down to 7 extents, then the tmp format is a
 * btree, but when swapped it needs to be in extent format. Hence we can't just
 * blindly swap data forks on attr2 filesystems.
 *
 * Note that we check the swap in both directions so that we don't end up with
 * a corrupt temporary inode, either.
 *
 * Note that fixing the way xfs_fsr sets up the attribute fork in the source
 * inode will prevent this situation from occurring, so all we do here is
 * reject and log the attempt. basically we are putting the responsibility on
 * userspace to get this right.
 */
static int
xfs_swap_extents_check_format(
	xfs_inode_t	*ip,	/* target inode */
	xfs_inode_t	*tip)	/* tmp inode */
{

	/* Should never get a local format */
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL ||
	    tip->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		return EINVAL;

	/*
	 * if the target inode has less extents that then temporary inode then
	 * why did userspace call us?
	 */
	if (ip->i_d.di_nextents < tip->i_d.di_nextents)
		return EINVAL;

	/*
	 * if the target inode is in extent form and the temp inode is in btree
	 * form then we will end up with the target inode in the wrong format
	 * as we already know there are less extents in the temp inode.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    tip->i_d.di_format == XFS_DINODE_FMT_BTREE)
		return EINVAL;

	/* Check temp in extent form to max in target */
	if (tip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    XFS_IFORK_NEXTENTS(tip, XFS_DATA_FORK) >
			XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK))
		return EINVAL;

	/* Check target in extent form to max in temp */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    XFS_IFORK_NEXTENTS(ip, XFS_DATA_FORK) >
			XFS_IFORK_MAXEXT(tip, XFS_DATA_FORK))
		return EINVAL;

	/*
	 * If we are in a btree format, check that the temp root block will fit
	 * in the target and that it has enough extents to be in btree format
	 * in the target.
	 *
	 * Note that we have to be careful to allow btree->extent conversions
	 * (a common defrag case) which will occur when the temp inode is in
	 * extent format...
	 */
	if (tip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		if (XFS_IFORK_BOFF(ip) &&
		    tip->i_df.if_broot_bytes > XFS_IFORK_BOFF(ip))
			return EINVAL;
		if (XFS_IFORK_NEXTENTS(tip, XFS_DATA_FORK) <=
		    XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK))
			return EINVAL;
	}

	/* Reciprocal target->temp btree format checks */
	if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		if (XFS_IFORK_BOFF(tip) &&
		    ip->i_df.if_broot_bytes > XFS_IFORK_BOFF(tip))
			return EINVAL;

		if (XFS_IFORK_NEXTENTS(ip, XFS_DATA_FORK) <=
		    XFS_IFORK_MAXEXT(tip, XFS_DATA_FORK))
			return EINVAL;
	}

	return 0;
}

static int
xfs_swap_extents(
	xfs_inode_t	*ip,	/* target inode */
	xfs_inode_t	*tip,	/* tmp inode */
	xfs_swapext_t	*sxp)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_trans_t	*tp;
	xfs_bstat_t	*sbp = &sxp->sx_stat;
	xfs_ifork_t	*tempifp, *ifp, *tifp;
	int		src_log_flags, target_log_flags;
	int		error = 0;
	int		aforkblks = 0;
	int		taforkblks = 0;
	__uint64_t	tmp;

	tempifp = kmem_alloc(sizeof(xfs_ifork_t), KM_MAYFAIL);
	if (!tempifp) {
		error = XFS_ERROR(ENOMEM);
		goto out;
	}

	/*
	 * we have to do two separate lock calls here to keep lockdep
	 * happy. If we try to get all the locks in one call, lock will
	 * report false positives when we drop the ILOCK and regain them
	 * below.
	 */
	xfs_lock_two_inodes(ip, tip, XFS_IOLOCK_EXCL);
	xfs_lock_two_inodes(ip, tip, XFS_ILOCK_EXCL);

	/* Verify that both files have the same format */
	if ((ip->i_d.di_mode & S_IFMT) != (tip->i_d.di_mode & S_IFMT)) {
		error = XFS_ERROR(EINVAL);
		goto out_unlock;
	}

	/* Verify both files are either real-time or non-realtime */
	if (XFS_IS_REALTIME_INODE(ip) != XFS_IS_REALTIME_INODE(tip)) {
		error = XFS_ERROR(EINVAL);
		goto out_unlock;
	}

	error = -filemap_write_and_wait(VFS_I(ip)->i_mapping);
	if (error)
		goto out_unlock;
	truncate_pagecache_range(VFS_I(ip), 0, -1);

	/* Verify O_DIRECT for ftmp */
	if (VN_CACHED(VFS_I(tip)) != 0) {
		error = XFS_ERROR(EINVAL);
		goto out_unlock;
	}

	/* Verify all data are being swapped */
	if (sxp->sx_offset != 0 ||
	    sxp->sx_length != ip->i_d.di_size ||
	    sxp->sx_length != tip->i_d.di_size) {
		error = XFS_ERROR(EFAULT);
		goto out_unlock;
	}

	trace_xfs_swap_extent_before(ip, 0);
	trace_xfs_swap_extent_before(tip, 1);

	/* check inode formats now that data is flushed */
	error = xfs_swap_extents_check_format(ip, tip);
	if (error) {
		xfs_notice(mp,
		    "%s: inode 0x%llx format is incompatible for exchanging.",
				__func__, ip->i_ino);
		goto out_unlock;
	}

	/*
	 * Compare the current change & modify times with that
	 * passed in.  If they differ, we abort this swap.
	 * This is the mechanism used to ensure the calling
	 * process that the file was not changed out from
	 * under it.
	 */
	if ((sbp->bs_ctime.tv_sec != VFS_I(ip)->i_ctime.tv_sec) ||
	    (sbp->bs_ctime.tv_nsec != VFS_I(ip)->i_ctime.tv_nsec) ||
	    (sbp->bs_mtime.tv_sec != VFS_I(ip)->i_mtime.tv_sec) ||
	    (sbp->bs_mtime.tv_nsec != VFS_I(ip)->i_mtime.tv_nsec)) {
		error = XFS_ERROR(EBUSY);
		goto out_unlock;
	}

	/* We need to fail if the file is memory mapped.  Once we have tossed
	 * all existing pages, the page fault will have no option
	 * but to go to the filesystem for pages. By making the page fault call
	 * vop_read (or write in the case of autogrow) they block on the iolock
	 * until we have switched the extents.
	 */
	if (VN_MAPPED(VFS_I(ip))) {
		error = XFS_ERROR(EBUSY);
		goto out_unlock;
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
	truncate_pagecache_range(VFS_I(ip), 0, -1);

	tp = xfs_trans_alloc(mp, XFS_TRANS_SWAPEXT);
	if ((error = xfs_trans_reserve(tp, 0,
				     XFS_ICHANGE_LOG_RES(mp), 0,
				     0, 0))) {
		xfs_iunlock(ip,  XFS_IOLOCK_EXCL);
		xfs_iunlock(tip, XFS_IOLOCK_EXCL);
		xfs_trans_cancel(tp, 0);
		goto out;
	}
	xfs_lock_two_inodes(ip, tip, XFS_ILOCK_EXCL);

	/*
	 * Count the number of extended attribute blocks
	 */
	if ( ((XFS_IFORK_Q(ip) != 0) && (ip->i_d.di_anextents > 0)) &&
	     (ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, ip, XFS_ATTR_FORK, &aforkblks);
		if (error)
			goto out_trans_cancel;
	}
	if ( ((XFS_IFORK_Q(tip) != 0) && (tip->i_d.di_anextents > 0)) &&
	     (tip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, tip, XFS_ATTR_FORK,
			&taforkblks);
		if (error)
			goto out_trans_cancel;
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

	/*
	 * The extents in the source inode could still contain speculative
	 * preallocation beyond EOF (e.g. the file is open but not modified
	 * while defrag is in progress). In that case, we need to copy over the
	 * number of delalloc blocks the data fork in the source inode is
	 * tracking beyond EOF so that when the fork is truncated away when the
	 * temporary inode is unlinked we don't underrun the i_delayed_blks
	 * counter on that inode.
	 */
	ASSERT(tip->i_delayed_blks == 0);
	tip->i_delayed_blks = ip->i_delayed_blks;
	ip->i_delayed_blks = 0;

	src_log_flags = XFS_ILOG_CORE;
	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/* If the extents fit in the inode, fix the
		 * pointer.  Otherwise it's already NULL or
		 * pointing to the extent.
		 */
		if (ip->i_d.di_nextents <= XFS_INLINE_EXTS) {
			ifp->if_u1.if_extents =
				ifp->if_u2.if_inline_ext;
		}
		src_log_flags |= XFS_ILOG_DEXT;
		break;
	case XFS_DINODE_FMT_BTREE:
		src_log_flags |= XFS_ILOG_DBROOT;
		break;
	}

	target_log_flags = XFS_ILOG_CORE;
	switch (tip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/* If the extents fit in the inode, fix the
		 * pointer.  Otherwise it's already NULL or
		 * pointing to the extent.
		 */
		if (tip->i_d.di_nextents <= XFS_INLINE_EXTS) {
			tifp->if_u1.if_extents =
				tifp->if_u2.if_inline_ext;
		}
		target_log_flags |= XFS_ILOG_DEXT;
		break;
	case XFS_DINODE_FMT_BTREE:
		target_log_flags |= XFS_ILOG_DBROOT;
		break;
	}


	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_trans_ijoin(tp, tip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	xfs_trans_log_inode(tp, ip,  src_log_flags);
	xfs_trans_log_inode(tp, tip, target_log_flags);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp, 0);

	trace_xfs_swap_extent_after(ip, 0);
	trace_xfs_swap_extent_after(tip, 1);
out:
	kmem_free(tempifp);
	return error;

out_unlock:
	xfs_iunlock(ip,  XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_iunlock(tip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	goto out;

out_trans_cancel:
	xfs_trans_cancel(tp, 0);
	goto out_unlock;
}
