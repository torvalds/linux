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
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_attr.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_buf_item.h"
#include "xfs_rw.h"
#include "xfs_trace.h"

/*
 * This is a subroutine for xfs_write() and other writers (xfs_ioctl)
 * which clears the setuid and setgid bits when a file is written.
 */
int
xfs_write_clear_setuid(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_trans_t	*tp;
	int		error;

	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_WRITEID);
	if ((error = xfs_trans_reserve(tp, 0,
				      XFS_WRITEID_LOG_RES(mp),
				      0, 0, 0))) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	ip->i_d.di_mode &= ~S_ISUID;

	/*
	 * Note that we don't have to worry about mandatory
	 * file locking being disabled here because we only
	 * clear the S_ISGID bit if the Group execute bit is
	 * on, but if it was on then mandatory locking wouldn't
	 * have been enabled.
	 */
	if (ip->i_d.di_mode & S_IXGRP) {
		ip->i_d.di_mode &= ~S_ISGID;
	}
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * Force a shutdown of the filesystem instantly while keeping
 * the filesystem consistent. We don't do an unmount here; just shutdown
 * the shop, make sure that absolutely nothing persistent happens to
 * this filesystem after this point.
 */
void
xfs_do_force_shutdown(
	xfs_mount_t	*mp,
	int		flags,
	char		*fname,
	int		lnnum)
{
	int		logerror;

	logerror = flags & SHUTDOWN_LOG_IO_ERROR;

	if (!(flags & SHUTDOWN_FORCE_UMOUNT)) {
		cmn_err(CE_NOTE, "xfs_force_shutdown(%s,0x%x) called from "
				 "line %d of file %s.  Return address = 0x%p",
			mp->m_fsname, flags, lnnum, fname, __return_address);
	}
	/*
	 * No need to duplicate efforts.
	 */
	if (XFS_FORCED_SHUTDOWN(mp) && !logerror)
		return;

	/*
	 * This flags XFS_MOUNT_FS_SHUTDOWN, makes sure that we don't
	 * queue up anybody new on the log reservations, and wakes up
	 * everybody who's sleeping on log reservations to tell them
	 * the bad news.
	 */
	if (xfs_log_force_umount(mp, logerror))
		return;

	if (flags & SHUTDOWN_CORRUPT_INCORE) {
		xfs_cmn_err(XFS_PTAG_SHUTDOWN_CORRUPT, CE_ALERT, mp,
    "Corruption of in-memory data detected.  Shutting down filesystem: %s",
			mp->m_fsname);
		if (XFS_ERRLEVEL_HIGH <= xfs_error_level) {
			xfs_stack_trace();
		}
	} else if (!(flags & SHUTDOWN_FORCE_UMOUNT)) {
		if (logerror) {
			xfs_cmn_err(XFS_PTAG_SHUTDOWN_LOGERROR, CE_ALERT, mp,
		"Log I/O Error Detected.  Shutting down filesystem: %s",
				mp->m_fsname);
		} else if (flags & SHUTDOWN_DEVICE_REQ) {
			xfs_cmn_err(XFS_PTAG_SHUTDOWN_IOERROR, CE_ALERT, mp,
		"All device paths lost.  Shutting down filesystem: %s",
				mp->m_fsname);
		} else if (!(flags & SHUTDOWN_REMOTE_REQ)) {
			xfs_cmn_err(XFS_PTAG_SHUTDOWN_IOERROR, CE_ALERT, mp,
		"I/O Error Detected.  Shutting down filesystem: %s",
				mp->m_fsname);
		}
	}
	if (!(flags & SHUTDOWN_FORCE_UMOUNT)) {
		cmn_err(CE_ALERT, "Please umount the filesystem, "
				  "and rectify the problem(s)");
	}
}

/*
 * Prints out an ALERT message about I/O error.
 */
void
xfs_ioerror_alert(
	char			*func,
	struct xfs_mount	*mp,
	xfs_buf_t		*bp,
	xfs_daddr_t		blkno)
{
	cmn_err(CE_ALERT,
 "I/O error in filesystem (\"%s\") meta-data dev %s block 0x%llx"
 "       (\"%s\") error %d buf count %zd",
		(!mp || !mp->m_fsname) ? "(fs name not set)" : mp->m_fsname,
		XFS_BUFTARG_NAME(XFS_BUF_TARGET(bp)),
		(__uint64_t)blkno, func,
		XFS_BUF_GETERROR(bp), XFS_BUF_COUNT(bp));
}

/*
 * This isn't an absolute requirement, but it is
 * just a good idea to call xfs_read_buf instead of
 * directly doing a read_buf call. For one, we shouldn't
 * be doing this disk read if we are in SHUTDOWN state anyway,
 * so this stops that from happening. Secondly, this does all
 * the error checking stuff and the brelse if appropriate for
 * the caller, so the code can be a little leaner.
 */

int
xfs_read_buf(
	struct xfs_mount *mp,
	xfs_buftarg_t	 *target,
	xfs_daddr_t	 blkno,
	int              len,
	uint             flags,
	xfs_buf_t	 **bpp)
{
	xfs_buf_t	 *bp;
	int		 error;

	if (!flags)
		flags = XBF_LOCK | XBF_MAPPED;

	bp = xfs_buf_read(target, blkno, len, flags);
	if (!bp)
		return XFS_ERROR(EIO);
	error = XFS_BUF_GETERROR(bp);
	if (bp && !error && !XFS_FORCED_SHUTDOWN(mp)) {
		*bpp = bp;
	} else {
		*bpp = NULL;
		if (error) {
			xfs_ioerror_alert("xfs_read_buf", mp, bp, XFS_BUF_ADDR(bp));
		} else {
			error = XFS_ERROR(EIO);
		}
		if (bp) {
			XFS_BUF_UNDONE(bp);
			XFS_BUF_UNDELAYWRITE(bp);
			XFS_BUF_STALE(bp);
			/*
			 * brelse clears B_ERROR and b_error
			 */
			xfs_buf_relse(bp);
		}
	}
	return (error);
}

/*
 * helper function to extract extent size hint from inode
 */
xfs_extlen_t
xfs_get_extsz_hint(
	struct xfs_inode	*ip)
{
	xfs_extlen_t		extsz;

	if (unlikely(XFS_IS_REALTIME_INODE(ip))) {
		extsz = (ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE)
				? ip->i_d.di_extsize
				: ip->i_mount->m_sb.sb_rextsize;
		ASSERT(extsz);
	} else {
		extsz = (ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE)
				? ip->i_d.di_extsize : 0;
	}

	return extsz;
}
