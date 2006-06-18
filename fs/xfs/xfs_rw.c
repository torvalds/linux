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
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
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
#include "xfs_acl.h"
#include "xfs_mac.h"
#include "xfs_error.h"
#include "xfs_buf_item.h"
#include "xfs_rw.h"

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
	error = xfs_trans_commit(tp, 0, NULL);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * Handle logging requirements of various synchronous types of write.
 */
int
xfs_write_sync_logforce(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip)
{
	int		error = 0;

	/*
	 * If we're treating this as O_DSYNC and we have not updated the
	 * size, force the log.
	 */
	if (!(mp->m_flags & XFS_MOUNT_OSYNCISOSYNC) &&
	    !(ip->i_update_size)) {
		xfs_inode_log_item_t	*iip = ip->i_itemp;

		/*
		 * If an allocation transaction occurred
		 * without extending the size, then we have to force
		 * the log up the proper point to ensure that the
		 * allocation is permanent.  We can't count on
		 * the fact that buffered writes lock out direct I/O
		 * writes - the direct I/O write could have extended
		 * the size nontransactionally, then finished before
		 * we started.  xfs_write_file will think that the file
		 * didn't grow but the update isn't safe unless the
		 * size change is logged.
		 *
		 * Force the log if we've committed a transaction
		 * against the inode or if someone else has and
		 * the commit record hasn't gone to disk (e.g.
		 * the inode is pinned).  This guarantees that
		 * all changes affecting the inode are permanent
		 * when we return.
		 */
		if (iip && iip->ili_last_lsn) {
			xfs_log_force(mp, iip->ili_last_lsn,
					XFS_LOG_FORCE | XFS_LOG_SYNC);
		} else if (xfs_ipincount(ip) > 0) {
			xfs_log_force(mp, (xfs_lsn_t)0,
					XFS_LOG_FORCE | XFS_LOG_SYNC);
		}

	} else {
		xfs_trans_t	*tp;

		/*
		 * O_SYNC or O_DSYNC _with_ a size update are handled
		 * the same way.
		 *
		 * If the write was synchronous then we need to make
		 * sure that the inode modification time is permanent.
		 * We'll have updated the timestamp above, so here
		 * we use a synchronous transaction to log the inode.
		 * It's not fast, but it's necessary.
		 *
		 * If this a dsync write and the size got changed
		 * non-transactionally, then we need to ensure that
		 * the size change gets logged in a synchronous
		 * transaction.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_WRITE_SYNC);
		if ((error = xfs_trans_reserve(tp, 0,
						XFS_SWRITE_LOG_RES(mp),
						0, 0, 0))) {
			/* Transaction reserve failed */
			xfs_trans_cancel(tp, 0);
		} else {
			/* Transaction reserve successful */
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
			xfs_trans_ihold(tp, ip);
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
			xfs_trans_set_sync(tp);
			error = xfs_trans_commit(tp, 0, NULL);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}
	}

	return error;
}

/*
 * Force a shutdown of the filesystem instantly while keeping
 * the filesystem consistent. We don't do an unmount here; just shutdown
 * the shop, make sure that absolutely nothing persistent happens to
 * this filesystem after this point.
 */

void
xfs_do_force_shutdown(
	bhv_desc_t	*bdp,
	int		flags,
	char		*fname,
	int		lnnum)
{
	int		logerror;
	xfs_mount_t	*mp;

	mp = XFS_BHVTOM(bdp);
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
 * Called when we want to stop a buffer from getting written or read.
 * We attach the EIO error, muck with its flags, and call biodone
 * so that the proper iodone callbacks get called.
 */
int
xfs_bioerror(
	xfs_buf_t *bp)
{

#ifdef XFSERRORDEBUG
	ASSERT(XFS_BUF_ISREAD(bp) || bp->b_iodone);
#endif

	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 */
	xfs_buftrace("XFS IOERROR", bp);
	XFS_BUF_ERROR(bp, EIO);
	/*
	 * We're calling biodone, so delete B_DONE flag. Either way
	 * we have to call the iodone callback, and calling biodone
	 * probably is the best way since it takes care of
	 * GRIO as well.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_UNDONE(bp);
	XFS_BUF_STALE(bp);

	XFS_BUF_CLR_BDSTRAT_FUNC(bp);
	xfs_biodone(bp);

	return (EIO);
}

/*
 * Same as xfs_bioerror, except that we are releasing the buffer
 * here ourselves, and avoiding the biodone call.
 * This is meant for userdata errors; metadata bufs come with
 * iodone functions attached, so that we can track down errors.
 */
int
xfs_bioerror_relse(
	xfs_buf_t *bp)
{
	int64_t fl;

	ASSERT(XFS_BUF_IODONE_FUNC(bp) != xfs_buf_iodone_callbacks);
	ASSERT(XFS_BUF_IODONE_FUNC(bp) != xlog_iodone);

	xfs_buftrace("XFS IOERRELSE", bp);
	fl = XFS_BUF_BFLAGS(bp);
	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 *
	 * chunkhold expects B_DONE to be set, whether
	 * we actually finish the I/O or not. We don't want to
	 * change that interface.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_DONE(bp);
	XFS_BUF_STALE(bp);
	XFS_BUF_CLR_IODONE_FUNC(bp);
	XFS_BUF_CLR_BDSTRAT_FUNC(bp);
	if (!(fl & XFS_B_ASYNC)) {
		/*
		 * Mark b_error and B_ERROR _both_.
		 * Lot's of chunkcache code assumes that.
		 * There's no reason to mark error for
		 * ASYNC buffers.
		 */
		XFS_BUF_ERROR(bp, EIO);
		XFS_BUF_V_IODONESEMA(bp);
	} else {
		xfs_buf_relse(bp);
	}
	return (EIO);
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

	if (flags)
		bp = xfs_buf_read_flags(target, blkno, len, flags);
	else
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
 * Wrapper around bwrite() so that we can trap
 * write errors, and act accordingly.
 */
int
xfs_bwrite(
	struct xfs_mount *mp,
	struct xfs_buf	 *bp)
{
	int	error;

	/*
	 * XXXsup how does this work for quotas.
	 */
	XFS_BUF_SET_BDSTRAT_FUNC(bp, xfs_bdstrat_cb);
	XFS_BUF_SET_FSPRIVATE3(bp, mp);
	XFS_BUF_WRITE(bp);

	if ((error = XFS_bwrite(bp))) {
		ASSERT(mp);
		/*
		 * Cannot put a buftrace here since if the buffer is not
		 * B_HOLD then we will brelse() the buffer before returning
		 * from bwrite and we could be tracing a buffer that has
		 * been reused.
		 */
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	}
	return (error);
}
