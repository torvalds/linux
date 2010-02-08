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
#include "xfs_da_btree.h"
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
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_rw.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_utils.h"
#include "xfs_rtalloc.h"
#include "xfs_trans_space.h"
#include "xfs_log_priv.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"

int
xfs_setattr(
	struct xfs_inode	*ip,
	struct iattr		*iattr,
	int			flags)
{
	xfs_mount_t		*mp = ip->i_mount;
	struct inode		*inode = VFS_I(ip);
	int			mask = iattr->ia_valid;
	xfs_trans_t		*tp;
	int			code;
	uint			lock_flags;
	uint			commit_flags=0;
	uid_t			uid=0, iuid=0;
	gid_t			gid=0, igid=0;
	struct xfs_dquot	*udqp, *gdqp, *olddquot1, *olddquot2;
	int			need_iolock = 1;

	xfs_itrace_entry(ip);

	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return XFS_ERROR(EROFS);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	code = -inode_change_ok(inode, iattr);
	if (code)
		return code;

	olddquot1 = olddquot2 = NULL;
	udqp = gdqp = NULL;

	/*
	 * If disk quotas is on, we make sure that the dquots do exist on disk,
	 * before we start any other transactions. Trying to do this later
	 * is messy. We don't care to take a readlock to look at the ids
	 * in inode here, because we can't hold it across the trans_reserve.
	 * If the IDs do change before we take the ilock, we're covered
	 * because the i_*dquot fields will get updated anyway.
	 */
	if (XFS_IS_QUOTA_ON(mp) && (mask & (ATTR_UID|ATTR_GID))) {
		uint	qflags = 0;

		if ((mask & ATTR_UID) && XFS_IS_UQUOTA_ON(mp)) {
			uid = iattr->ia_uid;
			qflags |= XFS_QMOPT_UQUOTA;
		} else {
			uid = ip->i_d.di_uid;
		}
		if ((mask & ATTR_GID) && XFS_IS_GQUOTA_ON(mp)) {
			gid = iattr->ia_gid;
			qflags |= XFS_QMOPT_GQUOTA;
		}  else {
			gid = ip->i_d.di_gid;
		}

		/*
		 * We take a reference when we initialize udqp and gdqp,
		 * so it is important that we never blindly double trip on
		 * the same variable. See xfs_create() for an example.
		 */
		ASSERT(udqp == NULL);
		ASSERT(gdqp == NULL);
		code = xfs_qm_vop_dqalloc(ip, uid, gid, ip->i_d.di_projid,
					 qflags, &udqp, &gdqp);
		if (code)
			return code;
	}

	/*
	 * For the other attributes, we acquire the inode lock and
	 * first do an error checking pass.
	 */
	tp = NULL;
	lock_flags = XFS_ILOCK_EXCL;
	if (flags & XFS_ATTR_NOLOCK)
		need_iolock = 0;
	if (!(mask & ATTR_SIZE)) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_NOT_SIZE);
		commit_flags = 0;
		code = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES(mp),
					 0, 0, 0);
		if (code) {
			lock_flags = 0;
			goto error_return;
		}
	} else {
		if (DM_EVENT_ENABLED(ip, DM_EVENT_TRUNCATE) &&
		    !(flags & XFS_ATTR_DMI)) {
			int dmflags = AT_DELAY_FLAG(flags) | DM_SEM_FLAG_WR;
			code = XFS_SEND_DATA(mp, DM_EVENT_TRUNCATE, ip,
				iattr->ia_size, 0, dmflags, NULL);
			if (code) {
				lock_flags = 0;
				goto error_return;
			}
		}
		if (need_iolock)
			lock_flags |= XFS_IOLOCK_EXCL;
	}

	xfs_ilock(ip, lock_flags);

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 */
	if (mask & (ATTR_UID|ATTR_GID)) {
		/*
		 * These IDs could have changed since we last looked at them.
		 * But, we're assured that if the ownership did change
		 * while we didn't have the inode locked, inode's dquot(s)
		 * would have changed also.
		 */
		iuid = ip->i_d.di_uid;
		igid = ip->i_d.di_gid;
		gid = (mask & ATTR_GID) ? iattr->ia_gid : igid;
		uid = (mask & ATTR_UID) ? iattr->ia_uid : iuid;

		/*
		 * Do a quota reservation only if uid/gid is actually
		 * going to change.
		 */
		if (XFS_IS_QUOTA_RUNNING(mp) &&
		    ((XFS_IS_UQUOTA_ON(mp) && iuid != uid) ||
		     (XFS_IS_GQUOTA_ON(mp) && igid != gid))) {
			ASSERT(tp);
			code = xfs_qm_vop_chown_reserve(tp, ip, udqp, gdqp,
						capable(CAP_FOWNER) ?
						XFS_QMOPT_FORCE_RES : 0);
			if (code)	/* out of quota */
				goto error_return;
		}
	}

	/*
	 * Truncate file.  Must have write permission and not be a directory.
	 */
	if (mask & ATTR_SIZE) {
		/* Short circuit the truncate case for zero length files */
		if (iattr->ia_size == 0 &&
		    ip->i_size == 0 && ip->i_d.di_nextents == 0) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			lock_flags &= ~XFS_ILOCK_EXCL;
			if (mask & ATTR_CTIME)
				xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
			code = 0;
			goto error_return;
		}

		if (S_ISDIR(ip->i_d.di_mode)) {
			code = XFS_ERROR(EISDIR);
			goto error_return;
		} else if (!S_ISREG(ip->i_d.di_mode)) {
			code = XFS_ERROR(EINVAL);
			goto error_return;
		}

		/*
		 * Make sure that the dquots are attached to the inode.
		 */
		code = xfs_qm_dqattach_locked(ip, 0);
		if (code)
			goto error_return;

		/*
		 * Now we can make the changes.  Before we join the inode
		 * to the transaction, if ATTR_SIZE is set then take care of
		 * the part of the truncation that must be done without the
		 * inode lock.  This needs to be done before joining the inode
		 * to the transaction, because the inode cannot be unlocked
		 * once it is a part of the transaction.
		 */
		if (iattr->ia_size > ip->i_size) {
			/*
			 * Do the first part of growing a file: zero any data
			 * in the last block that is beyond the old EOF.  We
			 * need to do this before the inode is joined to the
			 * transaction to modify the i_size.
			 */
			code = xfs_zero_eof(ip, iattr->ia_size, ip->i_size);
		}
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

		/*
		 * We are going to log the inode size change in this
		 * transaction so any previous writes that are beyond the on
		 * disk EOF and the new EOF that have not been written out need
		 * to be written here. If we do not write the data out, we
		 * expose ourselves to the null files problem.
		 *
		 * Only flush from the on disk size to the smaller of the in
		 * memory file size or the new size as that's the range we
		 * really care about here and prevents waiting for other data
		 * not within the range we care about here.
		 */
		if (!code &&
		    ip->i_size != ip->i_d.di_size &&
		    iattr->ia_size > ip->i_d.di_size) {
			code = xfs_flush_pages(ip,
					ip->i_d.di_size, iattr->ia_size,
					XFS_B_ASYNC, FI_NONE);
		}

		/* wait for all I/O to complete */
		xfs_ioend_wait(ip);

		if (!code)
			code = xfs_itruncate_data(ip, iattr->ia_size);
		if (code) {
			ASSERT(tp == NULL);
			lock_flags &= ~XFS_ILOCK_EXCL;
			ASSERT(lock_flags == XFS_IOLOCK_EXCL);
			goto error_return;
		}
		tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_SIZE);
		if ((code = xfs_trans_reserve(tp, 0,
					     XFS_ITRUNCATE_LOG_RES(mp), 0,
					     XFS_TRANS_PERM_LOG_RES,
					     XFS_ITRUNCATE_LOG_COUNT))) {
			xfs_trans_cancel(tp, 0);
			if (need_iolock)
				xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return code;
		}
		commit_flags = XFS_TRANS_RELEASE_LOG_RES;
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		xfs_trans_ijoin(tp, ip, lock_flags);
		xfs_trans_ihold(tp, ip);

		/*
		 * Only change the c/mtime if we are changing the size
		 * or we are explicitly asked to change it. This handles
		 * the semantic difference between truncate() and ftruncate()
		 * as implemented in the VFS.
		 *
		 * The regular truncate() case without ATTR_CTIME and ATTR_MTIME
		 * is a special case where we need to update the times despite
		 * not having these flags set.  For all other operations the
		 * VFS set these flags explicitly if it wants a timestamp
		 * update.
		 */
		if (iattr->ia_size != ip->i_size &&
		    (!(mask & (ATTR_CTIME | ATTR_MTIME)))) {
			iattr->ia_ctime = iattr->ia_mtime =
				current_fs_time(inode->i_sb);
			mask |= ATTR_CTIME | ATTR_MTIME;
		}

		if (iattr->ia_size > ip->i_size) {
			ip->i_d.di_size = iattr->ia_size;
			ip->i_size = iattr->ia_size;
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		} else if (iattr->ia_size <= ip->i_size ||
			   (iattr->ia_size == 0 && ip->i_d.di_nextents)) {
			/*
			 * signal a sync transaction unless
			 * we're truncating an already unlinked
			 * file on a wsync filesystem
			 */
			code = xfs_itruncate_finish(&tp, ip, iattr->ia_size,
					    XFS_DATA_FORK,
					    ((ip->i_d.di_nlink != 0 ||
					      !(mp->m_flags & XFS_MOUNT_WSYNC))
					     ? 1 : 0));
			if (code)
				goto abort_return;
			/*
			 * Truncated "down", so we're removing references
			 * to old data here - if we now delay flushing for
			 * a long time, we expose ourselves unduly to the
			 * notorious NULL files problem.  So, we mark this
			 * vnode and flush it when the file is closed, and
			 * do not wait the usual (long) time for writeout.
			 */
			xfs_iflags_set(ip, XFS_ITRUNCATED);
		}
	} else if (tp) {
		xfs_trans_ijoin(tp, ip, lock_flags);
		xfs_trans_ihold(tp, ip);
	}

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 */
	if (mask & (ATTR_UID|ATTR_GID)) {
		/*
		 * CAP_FSETID overrides the following restrictions:
		 *
		 * The set-user-ID and set-group-ID bits of a file will be
		 * cleared upon successful return from chown()
		 */
		if ((ip->i_d.di_mode & (S_ISUID|S_ISGID)) &&
		    !capable(CAP_FSETID)) {
			ip->i_d.di_mode &= ~(S_ISUID|S_ISGID);
		}

		/*
		 * Change the ownerships and register quota modifications
		 * in the transaction.
		 */
		if (iuid != uid) {
			if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_UQUOTA_ON(mp)) {
				ASSERT(mask & ATTR_UID);
				ASSERT(udqp);
				olddquot1 = xfs_qm_vop_chown(tp, ip,
							&ip->i_udquot, udqp);
			}
			ip->i_d.di_uid = uid;
			inode->i_uid = uid;
		}
		if (igid != gid) {
			if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_GQUOTA_ON(mp)) {
				ASSERT(!XFS_IS_PQUOTA_ON(mp));
				ASSERT(mask & ATTR_GID);
				ASSERT(gdqp);
				olddquot2 = xfs_qm_vop_chown(tp, ip,
							&ip->i_gdquot, gdqp);
			}
			ip->i_d.di_gid = gid;
			inode->i_gid = gid;
		}
	}

	/*
	 * Change file access modes.
	 */
	if (mask & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;

		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;

		ip->i_d.di_mode &= S_IFMT;
		ip->i_d.di_mode |= mode & ~S_IFMT;

		inode->i_mode &= S_IFMT;
		inode->i_mode |= mode & ~S_IFMT;
	}

	/*
	 * Change file access or modified times.
	 */
	if (mask & ATTR_ATIME) {
		inode->i_atime = iattr->ia_atime;
		ip->i_d.di_atime.t_sec = iattr->ia_atime.tv_sec;
		ip->i_d.di_atime.t_nsec = iattr->ia_atime.tv_nsec;
		ip->i_update_core = 1;
	}
	if (mask & ATTR_CTIME) {
		inode->i_ctime = iattr->ia_ctime;
		ip->i_d.di_ctime.t_sec = iattr->ia_ctime.tv_sec;
		ip->i_d.di_ctime.t_nsec = iattr->ia_ctime.tv_nsec;
		ip->i_update_core = 1;
	}
	if (mask & ATTR_MTIME) {
		inode->i_mtime = iattr->ia_mtime;
		ip->i_d.di_mtime.t_sec = iattr->ia_mtime.tv_sec;
		ip->i_d.di_mtime.t_nsec = iattr->ia_mtime.tv_nsec;
		ip->i_update_core = 1;
	}

	/*
	 * And finally, log the inode core if any attribute in it
	 * has been changed.
	 */
	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE|
		    ATTR_ATIME|ATTR_CTIME|ATTR_MTIME))
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	XFS_STATS_INC(xs_ig_attrchg);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 * This is slightly sub-optimal in that truncates require
	 * two sync transactions instead of one for wsync filesystems.
	 * One for the truncate and one for the timestamps since we
	 * don't want to change the timestamps unless we're sure the
	 * truncate worked.  Truncates are less than 1% of the laddis
	 * mix so this probably isn't worth the trouble to optimize.
	 */
	code = 0;
	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	code = xfs_trans_commit(tp, commit_flags);

	xfs_iunlock(ip, lock_flags);

	/*
	 * Release any dquot(s) the inode had kept before chown.
	 */
	xfs_qm_dqrele(olddquot1);
	xfs_qm_dqrele(olddquot2);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	if (code)
		return code;

	/*
	 * XXX(hch): Updating the ACL entries is not atomic vs the i_mode
	 * 	     update.  We could avoid this with linked transactions
	 * 	     and passing down the transaction pointer all the way
	 *	     to attr_set.  No previous user of the generic
	 * 	     Posix ACL code seems to care about this issue either.
	 */
	if ((mask & ATTR_MODE) && !(flags & XFS_ATTR_NOACL)) {
		code = -xfs_acl_chmod(inode);
		if (code)
			return XFS_ERROR(code);
	}

	if (DM_EVENT_ENABLED(ip, DM_EVENT_ATTRIBUTE) &&
	    !(flags & XFS_ATTR_DMI)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_ATTRIBUTE, ip, DM_RIGHT_NULL,
					NULL, DM_RIGHT_NULL, NULL, NULL,
					0, 0, AT_DELAY_FLAG(flags));
	}
	return 0;

 abort_return:
	commit_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	if (tp) {
		xfs_trans_cancel(tp, commit_flags);
	}
	if (lock_flags != 0) {
		xfs_iunlock(ip, lock_flags);
	}
	return code;
}

/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 2 extents back from
 * bmapi.
 */
#define SYMLINK_MAPS 2

STATIC int
xfs_readlink_bmap(
	xfs_inode_t	*ip,
	char		*link)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		pathlen = ip->i_d.di_size;
	int             nmaps = SYMLINK_MAPS;
	xfs_bmbt_irec_t mval[SYMLINK_MAPS];
	xfs_daddr_t	d;
	int		byte_cnt;
	int		n;
	xfs_buf_t	*bp;
	int		error = 0;

	error = xfs_bmapi(NULL, ip, 0, XFS_B_TO_FSB(mp, pathlen), 0, NULL, 0,
			mval, &nmaps, NULL, NULL);
	if (error)
		goto out;

	for (n = 0; n < nmaps; n++) {
		d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
		byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);

		bp = xfs_buf_read(mp->m_ddev_targp, d, BTOBB(byte_cnt),
				  XBF_LOCK | XBF_MAPPED | XBF_DONT_BLOCK);
		error = XFS_BUF_GETERROR(bp);
		if (error) {
			xfs_ioerror_alert("xfs_readlink",
				  ip->i_mount, bp, XFS_BUF_ADDR(bp));
			xfs_buf_relse(bp);
			goto out;
		}
		if (pathlen < byte_cnt)
			byte_cnt = pathlen;
		pathlen -= byte_cnt;

		memcpy(link, XFS_BUF_PTR(bp), byte_cnt);
		xfs_buf_relse(bp);
	}

	link[ip->i_d.di_size] = '\0';
	error = 0;

 out:
	return error;
}

int
xfs_readlink(
	xfs_inode_t     *ip,
	char		*link)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		pathlen;
	int		error = 0;

	xfs_itrace_entry(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	xfs_ilock(ip, XFS_ILOCK_SHARED);

	ASSERT((ip->i_d.di_mode & S_IFMT) == S_IFLNK);
	ASSERT(ip->i_d.di_size <= MAXPATHLEN);

	pathlen = ip->i_d.di_size;
	if (!pathlen)
		goto out;

	if (ip->i_df.if_flags & XFS_IFINLINE) {
		memcpy(link, ip->i_df.if_u1.if_data, pathlen);
		link[pathlen] = '\0';
	} else {
		error = xfs_readlink_bmap(ip, link);
	}

 out:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}

/*
 * xfs_fsync
 *
 * This is called to sync the inode and its data out to disk.  We need to hold
 * the I/O lock while flushing the data, and the inode lock while flushing the
 * inode.  The inode lock CANNOT be held while flushing the data, so acquire
 * after we're done with that.
 */
int
xfs_fsync(
	xfs_inode_t	*ip)
{
	xfs_trans_t	*tp;
	int		error = 0;
	int		log_flushed = 0, changed = 1;

	xfs_itrace_entry(ip);

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return XFS_ERROR(EIO);

	/*
	 * We always need to make sure that the required inode state is safe on
	 * disk.  The inode might be clean but we still might need to force the
	 * log because of committed transactions that haven't hit the disk yet.
	 * Likewise, there could be unflushed non-transactional changes to the
	 * inode core that have to go to disk and this requires us to issue
	 * a synchronous transaction to capture these changes correctly.
	 *
	 * This code relies on the assumption that if the update_* fields
	 * of the inode are clear and the inode is unpinned then it is clean
	 * and no action is required.
	 */
	xfs_ilock(ip, XFS_ILOCK_SHARED);

	if (!ip->i_update_core) {
		/*
		 * Timestamps/size haven't changed since last inode flush or
		 * inode transaction commit.  That means either nothing got
		 * written or a transaction committed which caught the updates.
		 * If the latter happened and the transaction hasn't hit the
		 * disk yet, the inode will be still be pinned.  If it is,
		 * force the log.
		 */

		xfs_iunlock(ip, XFS_ILOCK_SHARED);

		if (xfs_ipincount(ip)) {
			error = _xfs_log_force(ip->i_mount, (xfs_lsn_t)0,
				      XFS_LOG_FORCE | XFS_LOG_SYNC,
				      &log_flushed);
		} else {
			/*
			 * If the inode is not pinned and nothing has changed
			 * we don't need to flush the cache.
			 */
			changed = 0;
		}
	} else	{
		/*
		 * Kick off a transaction to log the inode core to get the
		 * updates.  The sync transaction will also force the log.
		 */
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		tp = xfs_trans_alloc(ip->i_mount, XFS_TRANS_FSYNC_TS);
		error = xfs_trans_reserve(tp, 0,
				XFS_FSYNC_TS_LOG_RES(ip->i_mount), 0, 0, 0);
		if (error) {
			xfs_trans_cancel(tp, 0);
			return error;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		/*
		 * Note - it's possible that we might have pushed ourselves out
		 * of the way during trans_reserve which would flush the inode.
		 * But there's no guarantee that the inode buffer has actually
		 * gone out yet (it's delwri).	Plus the buffer could be pinned
		 * anyway if it's part of an inode in another recent
		 * transaction.	 So we play it safe and fire off the
		 * transaction anyway.
		 */
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		xfs_trans_set_sync(tp);
		error = _xfs_trans_commit(tp, 0, &log_flushed);

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	if ((ip->i_mount->m_flags & XFS_MOUNT_BARRIER) && changed) {
		/*
		 * If the log write didn't issue an ordered tag we need
		 * to flush the disk cache for the data device now.
		 */
		if (!log_flushed)
			xfs_blkdev_issue_flush(ip->i_mount->m_ddev_targp);

		/*
		 * If this inode is on the RT dev we need to flush that
		 * cache as well.
		 */
		if (XFS_IS_REALTIME_INODE(ip))
			xfs_blkdev_issue_flush(ip->i_mount->m_rtdev_targp);
	}

	return error;
}

/*
 * Flags for xfs_free_eofblocks
 */
#define XFS_FREE_EOF_TRYLOCK	(1<<0)

/*
 * This is called by xfs_inactive to free any blocks beyond eof
 * when the link count isn't zero and by xfs_dm_punch_hole() when
 * punching a hole to EOF.
 */
STATIC int
xfs_free_eofblocks(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	int		flags)
{
	xfs_trans_t	*tp;
	int		error;
	xfs_fileoff_t	end_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	map_len;
	int		nimaps;
	xfs_bmbt_irec_t	imap;

	/*
	 * Figure out if there are any blocks beyond the end
	 * of the file.  If not, then there is nothing to do.
	 */
	end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)ip->i_size));
	last_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAXIOFFSET(mp));
	map_len = last_fsb - end_fsb;
	if (map_len <= 0)
		return 0;

	nimaps = 1;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = xfs_bmapi(NULL, ip, end_fsb, map_len, 0,
			  NULL, 0, &imap, &nimaps, NULL, NULL);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!error && (nimaps != 0) &&
	    (imap.br_startblock != HOLESTARTBLOCK ||
	     ip->i_delayed_blks)) {
		/*
		 * Attach the dquots to the inode up front.
		 */
		error = xfs_qm_dqattach(ip, 0);
		if (error)
			return error;

		/*
		 * There are blocks after the end of file.
		 * Free them up now by truncating the file to
		 * its current size.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);

		/*
		 * Do the xfs_itruncate_start() call before
		 * reserving any log space because
		 * itruncate_start will call into the buffer
		 * cache and we can't
		 * do that within a transaction.
		 */
		if (flags & XFS_FREE_EOF_TRYLOCK) {
			if (!xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL)) {
				xfs_trans_cancel(tp, 0);
				return 0;
			}
		} else {
			xfs_ilock(ip, XFS_IOLOCK_EXCL);
		}
		error = xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE,
				    ip->i_size);
		if (error) {
			xfs_trans_cancel(tp, 0);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return error;
		}

		error = xfs_trans_reserve(tp, 0,
					  XFS_ITRUNCATE_LOG_RES(mp),
					  0, XFS_TRANS_PERM_LOG_RES,
					  XFS_ITRUNCATE_LOG_COUNT);
		if (error) {
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return error;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip,
				XFS_IOLOCK_EXCL |
				XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		error = xfs_itruncate_finish(&tp, ip,
					     ip->i_size,
					     XFS_DATA_FORK,
					     0);
		/*
		 * If we get an error at this point we
		 * simply don't bother truncating the file.
		 */
		if (error) {
			xfs_trans_cancel(tp,
					 (XFS_TRANS_RELEASE_LOG_RES |
					  XFS_TRANS_ABORT));
		} else {
			error = xfs_trans_commit(tp,
						XFS_TRANS_RELEASE_LOG_RES);
		}
		xfs_iunlock(ip, XFS_IOLOCK_EXCL|XFS_ILOCK_EXCL);
	}
	return error;
}

/*
 * Free a symlink that has blocks associated with it.
 */
STATIC int
xfs_inactive_symlink_rmt(
	xfs_inode_t	*ip,
	xfs_trans_t	**tpp)
{
	xfs_buf_t	*bp;
	int		committed;
	int		done;
	int		error;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t	free_list;
	int		i;
	xfs_mount_t	*mp;
	xfs_bmbt_irec_t	mval[SYMLINK_MAPS];
	int		nmaps;
	xfs_trans_t	*ntp;
	int		size;
	xfs_trans_t	*tp;

	tp = *tpp;
	mp = ip->i_mount;
	ASSERT(ip->i_d.di_size > XFS_IFORK_DSIZE(ip));
	/*
	 * We're freeing a symlink that has some
	 * blocks allocated to it.  Free the
	 * blocks here.  We know that we've got
	 * either 1 or 2 extents and that we can
	 * free them all in one bunmapi call.
	 */
	ASSERT(ip->i_d.di_nextents > 0 && ip->i_d.di_nextents <= 2);
	if ((error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_ITRUNCATE_LOG_COUNT))) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		xfs_trans_cancel(tp, 0);
		*tpp = NULL;
		return error;
	}
	/*
	 * Lock the inode, fix the size, and join it to the transaction.
	 * Hold it so in the normal path, we still have it locked for
	 * the second transaction.  In the error paths we need it
	 * held so the cancel won't rele it, see below.
	 */
	xfs_ilock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	size = (int)ip->i_d.di_size;
	ip->i_d.di_size = 0;
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	/*
	 * Find the block(s) so we can inval and unmap them.
	 */
	done = 0;
	xfs_bmap_init(&free_list, &first_block);
	nmaps = ARRAY_SIZE(mval);
	if ((error = xfs_bmapi(tp, ip, 0, XFS_B_TO_FSB(mp, size),
			XFS_BMAPI_METADATA, &first_block, 0, mval, &nmaps,
			&free_list, NULL)))
		goto error0;
	/*
	 * Invalidate the block(s).
	 */
	for (i = 0; i < nmaps; i++) {
		bp = xfs_trans_get_buf(tp, mp->m_ddev_targp,
			XFS_FSB_TO_DADDR(mp, mval[i].br_startblock),
			XFS_FSB_TO_BB(mp, mval[i].br_blockcount), 0);
		xfs_trans_binval(tp, bp);
	}
	/*
	 * Unmap the dead block(s) to the free_list.
	 */
	if ((error = xfs_bunmapi(tp, ip, 0, size, XFS_BMAPI_METADATA, nmaps,
			&first_block, &free_list, NULL, &done)))
		goto error1;
	ASSERT(done);
	/*
	 * Commit the first transaction.  This logs the EFI and the inode.
	 */
	if ((error = xfs_bmap_finish(&tp, &free_list, &committed)))
		goto error1;
	/*
	 * The transaction must have been committed, since there were
	 * actually extents freed by xfs_bunmapi.  See xfs_bmap_finish.
	 * The new tp has the extent freeing and EFDs.
	 */
	ASSERT(committed);
	/*
	 * The first xact was committed, so add the inode to the new one.
	 * Mark it dirty so it will be logged and moved forward in the log as
	 * part of every commit.
	 */
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	/*
	 * Get a new, empty transaction to return to our caller.
	 */
	ntp = xfs_trans_dup(tp);
	/*
	 * Commit the transaction containing extent freeing and EFDs.
	 * If we get an error on the commit here or on the reserve below,
	 * we need to unlock the inode since the new transaction doesn't
	 * have the inode attached.
	 */
	error = xfs_trans_commit(tp, 0);
	tp = ntp;
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		goto error0;
	}
	/*
	 * transaction commit worked ok so we can drop the extra ticket
	 * reference that we gained in xfs_trans_dup()
	 */
	xfs_log_ticket_put(tp->t_ticket);

	/*
	 * Remove the memory for extent descriptions (just bookkeeping).
	 */
	if (ip->i_df.if_bytes)
		xfs_idata_realloc(ip, -ip->i_df.if_bytes, XFS_DATA_FORK);
	ASSERT(ip->i_df.if_bytes == 0);
	/*
	 * Put an itruncate log reservation in the new transaction
	 * for our caller.
	 */
	if ((error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_ITRUNCATE_LOG_COUNT))) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		goto error0;
	}
	/*
	 * Return with the inode locked but not joined to the transaction.
	 */
	*tpp = tp;
	return 0;

 error1:
	xfs_bmap_cancel(&free_list);
 error0:
	/*
	 * Have to come here with the inode locked and either
	 * (held and in the transaction) or (not in the transaction).
	 * If the inode isn't held then cancel would iput it, but
	 * that's wrong since this is inactive and the vnode ref
	 * count is 0 already.
	 * Cancel won't do anything to the inode if held, but it still
	 * needs to be locked until the cancel is done, if it was
	 * joined to the transaction.
	 */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	*tpp = NULL;
	return error;

}

STATIC int
xfs_inactive_symlink_local(
	xfs_inode_t	*ip,
	xfs_trans_t	**tpp)
{
	int		error;

	ASSERT(ip->i_d.di_size <= XFS_IFORK_DSIZE(ip));
	/*
	 * We're freeing a symlink which fit into
	 * the inode.  Just free the memory used
	 * to hold the old symlink.
	 */
	error = xfs_trans_reserve(*tpp, 0,
				  XFS_ITRUNCATE_LOG_RES(ip->i_mount),
				  0, XFS_TRANS_PERM_LOG_RES,
				  XFS_ITRUNCATE_LOG_COUNT);

	if (error) {
		xfs_trans_cancel(*tpp, 0);
		*tpp = NULL;
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	/*
	 * Zero length symlinks _can_ exist.
	 */
	if (ip->i_df.if_bytes > 0) {
		xfs_idata_realloc(ip,
				  -(ip->i_df.if_bytes),
				  XFS_DATA_FORK);
		ASSERT(ip->i_df.if_bytes == 0);
	}
	return 0;
}

STATIC int
xfs_inactive_attrs(
	xfs_inode_t	*ip,
	xfs_trans_t	**tpp)
{
	xfs_trans_t	*tp;
	int		error;
	xfs_mount_t	*mp;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	tp = *tpp;
	mp = ip->i_mount;
	ASSERT(ip->i_d.di_forkoff != 0);
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		goto error_unlock;

	error = xfs_attr_inactive(ip);
	if (error)
		goto error_unlock;

	tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
	error = xfs_trans_reserve(tp, 0,
				  XFS_IFREE_LOG_RES(mp),
				  0, XFS_TRANS_PERM_LOG_RES,
				  XFS_INACTIVE_LOG_COUNT);
	if (error)
		goto error_cancel;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	xfs_idestroy_fork(ip, XFS_ATTR_FORK);

	ASSERT(ip->i_d.di_anextents == 0);

	*tpp = tp;
	return 0;

error_cancel:
	ASSERT(XFS_FORCED_SHUTDOWN(mp));
	xfs_trans_cancel(tp, 0);
error_unlock:
	*tpp = NULL;
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}

int
xfs_release(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error;

	if (!S_ISREG(ip->i_d.di_mode) || (ip->i_d.di_mode == 0))
		return 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;

	if (!XFS_FORCED_SHUTDOWN(mp)) {
		int truncated;

		/*
		 * If we are using filestreams, and we have an unlinked
		 * file that we are processing the last close on, then nothing
		 * will be able to reopen and write to this file. Purge this
		 * inode from the filestreams cache so that it doesn't delay
		 * teardown of the inode.
		 */
		if ((ip->i_d.di_nlink == 0) && xfs_inode_is_filestream(ip))
			xfs_filestream_deassociate(ip);

		/*
		 * If we previously truncated this file and removed old data
		 * in the process, we want to initiate "early" writeout on
		 * the last close.  This is an attempt to combat the notorious
		 * NULL files problem which is particularly noticable from a
		 * truncate down, buffered (re-)write (delalloc), followed by
		 * a crash.  What we are effectively doing here is
		 * significantly reducing the time window where we'd otherwise
		 * be exposed to that problem.
		 */
		truncated = xfs_iflags_test_and_clear(ip, XFS_ITRUNCATED);
		if (truncated && VN_DIRTY(VFS_I(ip)) && ip->i_delayed_blks > 0)
			xfs_flush_pages(ip, 0, -1, XFS_B_ASYNC, FI_NONE);
	}

	if (ip->i_d.di_nlink != 0) {
		if ((((ip->i_d.di_mode & S_IFMT) == S_IFREG) &&
		     ((ip->i_size > 0) || (VN_CACHED(VFS_I(ip)) > 0 ||
		       ip->i_delayed_blks > 0)) &&
		     (ip->i_df.if_flags & XFS_IFEXTENTS))  &&
		    (!(ip->i_d.di_flags &
				(XFS_DIFLAG_PREALLOC | XFS_DIFLAG_APPEND)))) {

			/*
			 * If we can't get the iolock just skip truncating
			 * the blocks past EOF because we could deadlock
			 * with the mmap_sem otherwise.  We'll get another
			 * chance to drop them once the last reference to
			 * the inode is dropped, so we'll never leak blocks
			 * permanently.
			 */
			error = xfs_free_eofblocks(mp, ip,
						   XFS_FREE_EOF_TRYLOCK);
			if (error)
				return error;
		}
	}

	return 0;
}

/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
int
xfs_inactive(
	xfs_inode_t	*ip)
{
	xfs_bmap_free_t	free_list;
	xfs_fsblock_t	first_block;
	int		committed;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;
	int		truncate;

	xfs_itrace_entry(ip);

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (ip->i_d.di_mode == 0 || is_bad_inode(VFS_I(ip))) {
		ASSERT(ip->i_df.if_real_bytes == 0);
		ASSERT(ip->i_df.if_broot_bytes == 0);
		return VN_INACTIVE_CACHE;
	}

	/*
	 * Only do a truncate if it's a regular file with
	 * some actual space in it.  It's OK to look at the
	 * inode's fields without the lock because we're the
	 * only one with a reference to the inode.
	 */
	truncate = ((ip->i_d.di_nlink == 0) &&
	    ((ip->i_d.di_size != 0) || (ip->i_size != 0) ||
	     (ip->i_d.di_nextents > 0) || (ip->i_delayed_blks > 0)) &&
	    ((ip->i_d.di_mode & S_IFMT) == S_IFREG));

	mp = ip->i_mount;

	if (ip->i_d.di_nlink == 0 && DM_EVENT_ENABLED(ip, DM_EVENT_DESTROY))
		XFS_SEND_DESTROY(mp, ip, DM_RIGHT_NULL);

	error = 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		goto out;

	if (ip->i_d.di_nlink != 0) {
		if ((((ip->i_d.di_mode & S_IFMT) == S_IFREG) &&
                     ((ip->i_size > 0) || (VN_CACHED(VFS_I(ip)) > 0 ||
                       ip->i_delayed_blks > 0)) &&
		      (ip->i_df.if_flags & XFS_IFEXTENTS) &&
		     (!(ip->i_d.di_flags &
				(XFS_DIFLAG_PREALLOC | XFS_DIFLAG_APPEND)) ||
		      (ip->i_delayed_blks != 0)))) {
			error = xfs_free_eofblocks(mp, ip, 0);
			if (error)
				return VN_INACTIVE_CACHE;
		}
		goto out;
	}

	ASSERT(ip->i_d.di_nlink == 0);

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return VN_INACTIVE_CACHE;

	tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
	if (truncate) {
		/*
		 * Do the xfs_itruncate_start() call before
		 * reserving any log space because itruncate_start
		 * will call into the buffer cache and we can't
		 * do that within a transaction.
		 */
		xfs_ilock(ip, XFS_IOLOCK_EXCL);

		error = xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, 0);
		if (error) {
			xfs_trans_cancel(tp, 0);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return VN_INACTIVE_CACHE;
		}

		error = xfs_trans_reserve(tp, 0,
					  XFS_ITRUNCATE_LOG_RES(mp),
					  0, XFS_TRANS_PERM_LOG_RES,
					  XFS_ITRUNCATE_LOG_COUNT);
		if (error) {
			/* Don't call itruncate_cleanup */
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return VN_INACTIVE_CACHE;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * normally, we have to run xfs_itruncate_finish sync.
		 * But if filesystem is wsync and we're in the inactive
		 * path, then we know that nlink == 0, and that the
		 * xaction that made nlink == 0 is permanently committed
		 * since xfs_remove runs as a synchronous transaction.
		 */
		error = xfs_itruncate_finish(&tp, ip, 0, XFS_DATA_FORK,
				(!(mp->m_flags & XFS_MOUNT_WSYNC) ? 1 : 0));

		if (error) {
			xfs_trans_cancel(tp,
				XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
			return VN_INACTIVE_CACHE;
		}
	} else if ((ip->i_d.di_mode & S_IFMT) == S_IFLNK) {

		/*
		 * If we get an error while cleaning up a
		 * symlink we bail out.
		 */
		error = (ip->i_d.di_size > XFS_IFORK_DSIZE(ip)) ?
			xfs_inactive_symlink_rmt(ip, &tp) :
			xfs_inactive_symlink_local(ip, &tp);

		if (error) {
			ASSERT(tp == NULL);
			return VN_INACTIVE_CACHE;
		}

		xfs_trans_ijoin(tp, ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);
	} else {
		error = xfs_trans_reserve(tp, 0,
					  XFS_IFREE_LOG_RES(mp),
					  0, XFS_TRANS_PERM_LOG_RES,
					  XFS_INACTIVE_LOG_COUNT);
		if (error) {
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			return VN_INACTIVE_CACHE;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		xfs_trans_ijoin(tp, ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);
	}

	/*
	 * If there are attributes associated with the file
	 * then blow them away now.  The code calls a routine
	 * that recursively deconstructs the attribute fork.
	 * We need to just commit the current transaction
	 * because we can't use it for xfs_attr_inactive().
	 */
	if (ip->i_d.di_anextents > 0) {
		error = xfs_inactive_attrs(ip, &tp);
		/*
		 * If we got an error, the transaction is already
		 * cancelled, and the inode is unlocked. Just get out.
		 */
		 if (error)
			 return VN_INACTIVE_CACHE;
	} else if (ip->i_afp) {
		xfs_idestroy_fork(ip, XFS_ATTR_FORK);
	}

	/*
	 * Free the inode.
	 */
	xfs_bmap_init(&free_list, &first_block);
	error = xfs_ifree(tp, ip, &free_list);
	if (error) {
		/*
		 * If we fail to free the inode, shut down.  The cancel
		 * might do that, we need to make sure.  Otherwise the
		 * inode might be lost for a long time or forever.
		 */
		if (!XFS_FORCED_SHUTDOWN(mp)) {
			cmn_err(CE_NOTE,
		"xfs_inactive:	xfs_ifree() returned an error = %d on %s",
				error, mp->m_fsname);
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		}
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES|XFS_TRANS_ABORT);
	} else {
		/*
		 * Credit the quota account(s). The inode is gone.
		 */
		xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

		/*
		 * Just ignore errors at this point.  There is nothing we can
		 * do except to try to keep going. Make sure it's not a silent
		 * error.
		 */
		error = xfs_bmap_finish(&tp,  &free_list, &committed);
		if (error)
			xfs_fs_cmn_err(CE_NOTE, mp, "xfs_inactive: "
				"xfs_bmap_finish() returned error %d", error);
		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		if (error)
			xfs_fs_cmn_err(CE_NOTE, mp, "xfs_inactive: "
				"xfs_trans_commit() returned error %d", error);
	}

	/*
	 * Release the dquots held by inode, if any.
	 */
	xfs_qm_dqdetach(ip);
	xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);

 out:
	return VN_INACTIVE_CACHE;
}

/*
 * Lookups up an inode from "name". If ci_name is not NULL, then a CI match
 * is allowed, otherwise it has to be an exact match. If a CI match is found,
 * ci_name->name will point to a the actual name (caller must free) or
 * will be set to NULL if an exact match is found.
 */
int
xfs_lookup(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	xfs_inode_t		**ipp,
	struct xfs_name		*ci_name)
{
	xfs_ino_t		inum;
	int			error;
	uint			lock_mode;

	xfs_itrace_entry(dp);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return XFS_ERROR(EIO);

	lock_mode = xfs_ilock_map_shared(dp);
	error = xfs_dir_lookup(NULL, dp, name, &inum, ci_name);
	xfs_iunlock_map_shared(dp, lock_mode);

	if (error)
		goto out;

	error = xfs_iget(dp->i_mount, NULL, inum, 0, 0, ipp, 0);
	if (error)
		goto out_free_name;

	return 0;

out_free_name:
	if (ci_name)
		kmem_free(ci_name->name);
out:
	*ipp = NULL;
	return error;
}

int
xfs_create(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	mode_t			mode,
	xfs_dev_t		rdev,
	xfs_inode_t		**ipp,
	cred_t			*credp)
{
	int			is_dir = S_ISDIR(mode);
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		unlock_dp_on_error = B_FALSE;
	uint			cancel_flags;
	int			committed;
	xfs_prid_t		prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	uint			resblks;
	uint			log_res;
	uint			log_count;

	xfs_itrace_entry(dp);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	if (DM_EVENT_ENABLED(dp, DM_EVENT_CREATE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_CREATE,
				dp, DM_RIGHT_NULL, NULL,
				DM_RIGHT_NULL, name->name, NULL,
				mode, 0, 0);

		if (error)
			return error;
	}

	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = dp->i_d.di_projid;
	else
		prid = dfltprid;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, current_fsuid(), current_fsgid(), prid,
			XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT, &udqp, &gdqp);
	if (error)
		goto std_return;

	if (is_dir) {
		rdev = 0;
		resblks = XFS_MKDIR_SPACE_RES(mp, name->len);
		log_res = XFS_MKDIR_LOG_RES(mp);
		log_count = XFS_MKDIR_LOG_COUNT;
		tp = xfs_trans_alloc(mp, XFS_TRANS_MKDIR);
	} else {
		resblks = XFS_CREATE_SPACE_RES(mp, name->len);
		log_res = XFS_CREATE_LOG_RES(mp);
		log_count = XFS_CREATE_LOG_COUNT;
		tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE);
	}

	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;

	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_reserve(tp, resblks, log_res, 0,
			XFS_TRANS_PERM_LOG_RES, log_count);
	if (error == ENOSPC) {
		/* flush outstanding delalloc blocks and retry */
		xfs_flush_inodes(dp);
		error = xfs_trans_reserve(tp, resblks, log_res, 0,
				XFS_TRANS_PERM_LOG_RES, log_count);
	}
	if (error == ENOSPC) {
		/* No space at all so try a "no-allocation" reservation */
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, log_res, 0,
				XFS_TRANS_PERM_LOG_RES, log_count);
	}
	if (error) {
		cancel_flags = 0;
		goto out_trans_cancel;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = B_TRUE;

	/*
	 * Check for directory link count overflow.
	 */
	if (is_dir && dp->i_d.di_nlink >= XFS_MAXLINK) {
		error = XFS_ERROR(EMLINK);
		goto out_trans_cancel;
	}

	xfs_bmap_init(&free_list, &first_block);

	/*
	 * Reserve disk quota and the inode.
	 */
	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto out_trans_cancel;

	error = xfs_dir_canenter(tp, dp, name, resblks);
	if (error)
		goto out_trans_cancel;

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dir_ialloc(&tp, dp, mode, is_dir ? 2 : 1, rdev, credp,
			       prid, resblks > 0, &ip, &committed);
	if (error) {
		if (error == ENOSPC)
			goto out_trans_cancel;
		goto out_trans_abort;
	}

	/*
	 * At this point, we've gotten a newly allocated inode.
	 * It is locked (and joined to the transaction).
	 */
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	/*
	 * Now we join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dir_ialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	IHOLD(dp);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	unlock_dp_on_error = B_FALSE;

	error = xfs_dir_createname(tp, dp, name, ip->i_ino,
					&first_block, &free_list, resblks ?
					resblks - XFS_IALLOC_SPACE_RES(mp) : 0);
	if (error) {
		ASSERT(error != ENOSPC);
		goto out_trans_abort;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	if (is_dir) {
		error = xfs_dir_init(tp, ip, dp);
		if (error)
			goto out_bmap_cancel;

		error = xfs_bumplink(tp, dp);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp);

	/*
	 * xfs_trans_commit normally decrements the vnode ref count
	 * when it unlocks the inode. Since we want to return the
	 * vnode to the caller, we bump the vnode ref count now.
	 */
	IHOLD(ip);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto out_abort_rele;

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error) {
		IRELE(ip);
		goto out_dqrele;
	}

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	*ipp = ip;

	/* Fallthrough to std_return with error = 0  */
 std_return:
	if (DM_EVENT_ENABLED(dp, DM_EVENT_POSTCREATE)) {
		XFS_SEND_NAMESP(mp, DM_EVENT_POSTCREATE, dp, DM_RIGHT_NULL,
				ip, DM_RIGHT_NULL, name->name, NULL, mode,
				error, 0);
	}

	return error;

 out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
 out_trans_abort:
	cancel_flags |= XFS_TRANS_ABORT;
 out_trans_cancel:
	xfs_trans_cancel(tp, cancel_flags);
 out_dqrele:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);

	goto std_return;

 out_abort_rele:
	/*
	 * Wait until after the current transaction is aborted to
	 * release the inode.  This prevents recursive transactions
	 * and deadlocks from xfs_inactive.
	 */
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
	xfs_trans_cancel(tp, cancel_flags);
	IRELE(ip);
	unlock_dp_on_error = B_FALSE;
	goto out_dqrele;
}

#ifdef DEBUG
int xfs_locked_n;
int xfs_small_retries;
int xfs_middle_retries;
int xfs_lots_retries;
int xfs_lock_delays;
#endif

/*
 * Bump the subclass so xfs_lock_inodes() acquires each lock with
 * a different value
 */
static inline int
xfs_lock_inumorder(int lock_mode, int subclass)
{
	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL))
		lock_mode |= (subclass + XFS_LOCK_INUMORDER) << XFS_IOLOCK_SHIFT;
	if (lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL))
		lock_mode |= (subclass + XFS_LOCK_INUMORDER) << XFS_ILOCK_SHIFT;

	return lock_mode;
}

/*
 * The following routine will lock n inodes in exclusive mode.
 * We assume the caller calls us with the inodes in i_ino order.
 *
 * We need to detect deadlock where an inode that we lock
 * is in the AIL and we start waiting for another inode that is locked
 * by a thread in a long running transaction (such as truncate). This can
 * result in deadlock since the long running trans might need to wait
 * for the inode we just locked in order to push the tail and free space
 * in the log.
 */
void
xfs_lock_inodes(
	xfs_inode_t	**ips,
	int		inodes,
	uint		lock_mode)
{
	int		attempts = 0, i, j, try_lock;
	xfs_log_item_t	*lp;

	ASSERT(ips && (inodes >= 2)); /* we need at least two */

	try_lock = 0;
	i = 0;

again:
	for (; i < inodes; i++) {
		ASSERT(ips[i]);

		if (i && (ips[i] == ips[i-1]))	/* Already locked */
			continue;

		/*
		 * If try_lock is not set yet, make sure all locked inodes
		 * are not in the AIL.
		 * If any are, set try_lock to be used later.
		 */

		if (!try_lock) {
			for (j = (i - 1); j >= 0 && !try_lock; j--) {
				lp = (xfs_log_item_t *)ips[j]->i_itemp;
				if (lp && (lp->li_flags & XFS_LI_IN_AIL)) {
					try_lock++;
				}
			}
		}

		/*
		 * If any of the previous locks we have locked is in the AIL,
		 * we must TRY to get the second and subsequent locks. If
		 * we can't get any, we must release all we have
		 * and try again.
		 */

		if (try_lock) {
			/* try_lock must be 0 if i is 0. */
			/*
			 * try_lock means we have an inode locked
			 * that is in the AIL.
			 */
			ASSERT(i != 0);
			if (!xfs_ilock_nowait(ips[i], xfs_lock_inumorder(lock_mode, i))) {
				attempts++;

				/*
				 * Unlock all previous guys and try again.
				 * xfs_iunlock will try to push the tail
				 * if the inode is in the AIL.
				 */

				for(j = i - 1; j >= 0; j--) {

					/*
					 * Check to see if we've already
					 * unlocked this one.
					 * Not the first one going back,
					 * and the inode ptr is the same.
					 */
					if ((j != (i - 1)) && ips[j] ==
								ips[j+1])
						continue;

					xfs_iunlock(ips[j], lock_mode);
				}

				if ((attempts % 5) == 0) {
					delay(1); /* Don't just spin the CPU */
#ifdef DEBUG
					xfs_lock_delays++;
#endif
				}
				i = 0;
				try_lock = 0;
				goto again;
			}
		} else {
			xfs_ilock(ips[i], xfs_lock_inumorder(lock_mode, i));
		}
	}

#ifdef DEBUG
	if (attempts) {
		if (attempts < 5) xfs_small_retries++;
		else if (attempts < 100) xfs_middle_retries++;
		else xfs_lots_retries++;
	} else {
		xfs_locked_n++;
	}
#endif
}

/*
 * xfs_lock_two_inodes() can only be used to lock one type of lock
 * at a time - the iolock or the ilock, but not both at once. If
 * we lock both at once, lockdep will report false positives saying
 * we have violated locking orders.
 */
void
xfs_lock_two_inodes(
	xfs_inode_t		*ip0,
	xfs_inode_t		*ip1,
	uint			lock_mode)
{
	xfs_inode_t		*temp;
	int			attempts = 0;
	xfs_log_item_t		*lp;

	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL))
		ASSERT((lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)) == 0);
	ASSERT(ip0->i_ino != ip1->i_ino);

	if (ip0->i_ino > ip1->i_ino) {
		temp = ip0;
		ip0 = ip1;
		ip1 = temp;
	}

 again:
	xfs_ilock(ip0, xfs_lock_inumorder(lock_mode, 0));

	/*
	 * If the first lock we have locked is in the AIL, we must TRY to get
	 * the second lock. If we can't get it, we must release the first one
	 * and try again.
	 */
	lp = (xfs_log_item_t *)ip0->i_itemp;
	if (lp && (lp->li_flags & XFS_LI_IN_AIL)) {
		if (!xfs_ilock_nowait(ip1, xfs_lock_inumorder(lock_mode, 1))) {
			xfs_iunlock(ip0, lock_mode);
			if ((++attempts % 5) == 0)
				delay(1); /* Don't just spin the CPU */
			goto again;
		}
	} else {
		xfs_ilock(ip1, xfs_lock_inumorder(lock_mode, 1));
	}
}

int
xfs_remove(
	xfs_inode_t             *dp,
	struct xfs_name		*name,
	xfs_inode_t		*ip)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t             *tp = NULL;
	int			is_dir = S_ISDIR(ip->i_d.di_mode);
	int                     error = 0;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	int			link_zero;
	uint			resblks;
	uint			log_count;

	xfs_itrace_entry(dp);
	xfs_itrace_entry(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	if (DM_EVENT_ENABLED(dp, DM_EVENT_REMOVE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_REMOVE, dp, DM_RIGHT_NULL,
					NULL, DM_RIGHT_NULL, name->name, NULL,
					ip->i_d.di_mode, 0, 0);
		if (error)
			return error;
	}

	error = xfs_qm_dqattach(dp, 0);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		goto std_return;

	if (is_dir) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_RMDIR);
		log_count = XFS_DEFAULT_LOG_COUNT;
	} else {
		tp = xfs_trans_alloc(mp, XFS_TRANS_REMOVE);
		log_count = XFS_REMOVE_LOG_COUNT;
	}
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;

	/*
	 * We try to get the real space reservation first,
	 * allowing for directory btree deletion(s) implying
	 * possible bmap insert(s).  If we can't get the space
	 * reservation then we use 0 instead, and avoid the bmap
	 * btree insert(s) in the directory code by, if the bmap
	 * insert tries to happen, instead trimming the LAST
	 * block from the directory.
	 */
	resblks = XFS_REMOVE_SPACE_RES(mp);
	error = xfs_trans_reserve(tp, resblks, XFS_REMOVE_LOG_RES(mp), 0,
				  XFS_TRANS_PERM_LOG_RES, log_count);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_REMOVE_LOG_RES(mp), 0,
					  XFS_TRANS_PERM_LOG_RES, log_count);
	}
	if (error) {
		ASSERT(error != ENOSPC);
		cancel_flags = 0;
		goto out_trans_cancel;
	}

	xfs_lock_two_inodes(dp, ip, XFS_ILOCK_EXCL);

	/*
	 * At this point, we've gotten both the directory and the entry
	 * inodes locked.
	 */
	IHOLD(ip);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);

	IHOLD(dp);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	/*
	 * If we're removing a directory perform some additional validation.
	 */
	if (is_dir) {
		ASSERT(ip->i_d.di_nlink >= 2);
		if (ip->i_d.di_nlink != 2) {
			error = XFS_ERROR(ENOTEMPTY);
			goto out_trans_cancel;
		}
		if (!xfs_dir_isempty(ip)) {
			error = XFS_ERROR(ENOTEMPTY);
			goto out_trans_cancel;
		}
	}

	xfs_bmap_init(&free_list, &first_block);
	error = xfs_dir_removename(tp, dp, name, ip->i_ino,
					&first_block, &free_list, resblks);
	if (error) {
		ASSERT(error != ENOENT);
		goto out_bmap_cancel;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	if (is_dir) {
		/*
		 * Drop the link from ip's "..".
		 */
		error = xfs_droplink(tp, dp);
		if (error)
			goto out_bmap_cancel;

		/*
		 * Drop the "." link from ip to self.
		 */
		error = xfs_droplink(tp, ip);
		if (error)
			goto out_bmap_cancel;
	} else {
		/*
		 * When removing a non-directory we need to log the parent
		 * inode here.  For a directory this is done implicitly
		 * by the xfs_droplink call for the ".." entry.
		 */
		xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	}

	/*
	 * Drop the link from dp to ip.
	 */
	error = xfs_droplink(tp, ip);
	if (error)
		goto out_bmap_cancel;

	/*
	 * Determine if this is the last link while
	 * we are in the transaction.
	 */
	link_zero = (ip->i_d.di_nlink == 0);

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error)
		goto std_return;

	/*
	 * If we are using filestreams, kill the stream association.
	 * If the file is still open it may get a new one but that
	 * will get killed on last close in xfs_close() so we don't
	 * have to worry about that.
	 */
	if (!is_dir && link_zero && xfs_inode_is_filestream(ip))
		xfs_filestream_deassociate(ip);

 std_return:
	if (DM_EVENT_ENABLED(dp, DM_EVENT_POSTREMOVE)) {
		XFS_SEND_NAMESP(mp, DM_EVENT_POSTREMOVE, dp, DM_RIGHT_NULL,
				NULL, DM_RIGHT_NULL, name->name, NULL,
				ip->i_d.di_mode, error, 0);
	}

	return error;

 out_bmap_cancel:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 out_trans_cancel:
	xfs_trans_cancel(tp, cancel_flags);
	goto std_return;
}

int
xfs_link(
	xfs_inode_t		*tdp,
	xfs_inode_t		*sip,
	struct xfs_name		*target_name)
{
	xfs_mount_t		*mp = tdp->i_mount;
	xfs_trans_t		*tp;
	int			error;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	int			resblks;

	xfs_itrace_entry(tdp);
	xfs_itrace_entry(sip);

	ASSERT(!S_ISDIR(sip->i_d.di_mode));

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	if (DM_EVENT_ENABLED(tdp, DM_EVENT_LINK)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_LINK,
					tdp, DM_RIGHT_NULL,
					sip, DM_RIGHT_NULL,
					target_name->name, NULL, 0, 0, 0);
		if (error)
			return error;
	}

	/* Return through std_return after this point. */

	error = xfs_qm_dqattach(sip, 0);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(tdp, 0);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_LINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	resblks = XFS_LINK_SPACE_RES(mp, target_name->len);
	error = xfs_trans_reserve(tp, resblks, XFS_LINK_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_LINK_LOG_COUNT);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_LINK_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_LINK_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		goto error_return;
	}

	xfs_lock_two_inodes(sip, tdp, XFS_ILOCK_EXCL);

	/*
	 * Increment vnode ref counts since xfs_trans_commit &
	 * xfs_trans_cancel will both unlock the inodes and
	 * decrement the associated ref counts.
	 */
	IHOLD(sip);
	IHOLD(tdp);
	xfs_trans_ijoin(tp, sip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, tdp, XFS_ILOCK_EXCL);

	/*
	 * If the source has too many links, we can't make any more to it.
	 */
	if (sip->i_d.di_nlink >= XFS_MAXLINK) {
		error = XFS_ERROR(EMLINK);
		goto error_return;
	}

	/*
	 * If we are using project inheritance, we only allow hard link
	 * creation in our tree when the project IDs are the same; else
	 * the tree quota mechanism could be circumvented.
	 */
	if (unlikely((tdp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (tdp->i_d.di_projid != sip->i_d.di_projid))) {
		error = XFS_ERROR(EXDEV);
		goto error_return;
	}

	error = xfs_dir_canenter(tp, tdp, target_name, resblks);
	if (error)
		goto error_return;

	xfs_bmap_init(&free_list, &first_block);

	error = xfs_dir_createname(tp, tdp, target_name, sip->i_ino,
					&first_block, &free_list, resblks);
	if (error)
		goto abort_return;
	xfs_ichgtime(tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, tdp, XFS_ILOG_CORE);

	error = xfs_bumplink(tp, sip);
	if (error)
		goto abort_return;

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		goto abort_return;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	if (error)
		goto std_return;

	/* Fall through to std_return with error = 0. */
std_return:
	if (DM_EVENT_ENABLED(sip, DM_EVENT_POSTLINK)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTLINK,
				tdp, DM_RIGHT_NULL,
				sip, DM_RIGHT_NULL,
				target_name->name, NULL, 0, error, 0);
	}
	return error;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */

 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	goto std_return;
}

int
xfs_symlink(
	xfs_inode_t		*dp,
	struct xfs_name		*link_name,
	const char		*target_path,
	mode_t			mode,
	xfs_inode_t		**ipp,
	cred_t			*credp)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t		*tp;
	xfs_inode_t		*ip;
	int			error;
	int			pathlen;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		unlock_dp_on_error = B_FALSE;
	uint			cancel_flags;
	int			committed;
	xfs_fileoff_t		first_fsb;
	xfs_filblks_t		fs_blocks;
	int			nmaps;
	xfs_bmbt_irec_t		mval[SYMLINK_MAPS];
	xfs_daddr_t		d;
	const char		*cur_chunk;
	int			byte_cnt;
	int			n;
	xfs_buf_t		*bp;
	xfs_prid_t		prid;
	struct xfs_dquot	*udqp, *gdqp;
	uint			resblks;

	*ipp = NULL;
	error = 0;
	ip = NULL;
	tp = NULL;

	xfs_itrace_entry(dp);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	/*
	 * Check component lengths of the target path name.
	 */
	pathlen = strlen(target_path);
	if (pathlen >= MAXPATHLEN)      /* total string too long */
		return XFS_ERROR(ENAMETOOLONG);

	if (DM_EVENT_ENABLED(dp, DM_EVENT_SYMLINK)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_SYMLINK, dp,
					DM_RIGHT_NULL, NULL, DM_RIGHT_NULL,
					link_name->name, target_path, 0, 0, 0);
		if (error)
			return error;
	}

	/* Return through std_return after this point. */

	udqp = gdqp = NULL;
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = dp->i_d.di_projid;
	else
		prid = (xfs_prid_t)dfltprid;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, current_fsuid(), current_fsgid(), prid,
			XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT, &udqp, &gdqp);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_SYMLINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	/*
	 * The symlink will fit into the inode data fork?
	 * There can't be any attributes so we get the whole variable part.
	 */
	if (pathlen <= XFS_LITINO(mp))
		fs_blocks = 0;
	else
		fs_blocks = XFS_B_TO_FSB(mp, pathlen);
	resblks = XFS_SYMLINK_SPACE_RES(mp, link_name->len, fs_blocks);
	error = xfs_trans_reserve(tp, resblks, XFS_SYMLINK_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_SYMLINK_LOG_COUNT);
	if (error == ENOSPC && fs_blocks == 0) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_SYMLINK_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_SYMLINK_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = B_TRUE;

	/*
	 * Check whether the directory allows new symlinks or not.
	 */
	if (dp->i_d.di_flags & XFS_DIFLAG_NOSYMLINKS) {
		error = XFS_ERROR(EPERM);
		goto error_return;
	}

	/*
	 * Reserve disk quota : blocks and inode.
	 */
	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto error_return;

	/*
	 * Check for ability to enter directory entry, if no space reserved.
	 */
	error = xfs_dir_canenter(tp, dp, link_name, resblks);
	if (error)
		goto error_return;
	/*
	 * Initialize the bmap freelist prior to calling either
	 * bmapi or the directory create code.
	 */
	xfs_bmap_init(&free_list, &first_block);

	/*
	 * Allocate an inode for the symlink.
	 */
	error = xfs_dir_ialloc(&tp, dp, S_IFLNK | (mode & ~S_IFMT),
			       1, 0, credp, prid, resblks > 0, &ip, NULL);
	if (error) {
		if (error == ENOSPC)
			goto error_return;
		goto error1;
	}

	/*
	 * An error after we've joined dp to the transaction will result in the
	 * transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	IHOLD(dp);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	unlock_dp_on_error = B_FALSE;

	/*
	 * Also attach the dquot(s) to it, if applicable.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp);

	if (resblks)
		resblks -= XFS_IALLOC_SPACE_RES(mp);
	/*
	 * If the symlink will fit into the inode, write it inline.
	 */
	if (pathlen <= XFS_IFORK_DSIZE(ip)) {
		xfs_idata_realloc(ip, pathlen, XFS_DATA_FORK);
		memcpy(ip->i_df.if_u1.if_data, target_path, pathlen);
		ip->i_d.di_size = pathlen;

		/*
		 * The inode was initially created in extent format.
		 */
		ip->i_df.if_flags &= ~(XFS_IFEXTENTS | XFS_IFBROOT);
		ip->i_df.if_flags |= XFS_IFINLINE;

		ip->i_d.di_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_DDATA | XFS_ILOG_CORE);

	} else {
		first_fsb = 0;
		nmaps = SYMLINK_MAPS;

		error = xfs_bmapi(tp, ip, first_fsb, fs_blocks,
				  XFS_BMAPI_WRITE | XFS_BMAPI_METADATA,
				  &first_block, resblks, mval, &nmaps,
				  &free_list, NULL);
		if (error) {
			goto error1;
		}

		if (resblks)
			resblks -= fs_blocks;
		ip->i_d.di_size = pathlen;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

		cur_chunk = target_path;
		for (n = 0; n < nmaps; n++) {
			d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
			byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
			bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
					       BTOBB(byte_cnt), 0);
			ASSERT(bp && !XFS_BUF_GETERROR(bp));
			if (pathlen < byte_cnt) {
				byte_cnt = pathlen;
			}
			pathlen -= byte_cnt;

			memcpy(XFS_BUF_PTR(bp), cur_chunk, byte_cnt);
			cur_chunk += byte_cnt;

			xfs_trans_log_buf(tp, bp, 0, byte_cnt - 1);
		}
	}

	/*
	 * Create the directory entry for the symlink.
	 */
	error = xfs_dir_createname(tp, dp, link_name, ip->i_ino,
					&first_block, &free_list, resblks);
	if (error)
		goto error1;
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	/*
	 * If this is a synchronous mount, make sure that the
	 * symlink transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	/*
	 * xfs_trans_commit normally decrements the vnode ref count
	 * when it unlocks the inode. Since we want to return the
	 * vnode to the caller, we bump the vnode ref count now.
	 */
	IHOLD(ip);

	error = xfs_bmap_finish(&tp, &free_list, &committed);
	if (error) {
		goto error2;
	}
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	/* Fall through to std_return with error = 0 or errno from
	 * xfs_trans_commit	*/
std_return:
	if (DM_EVENT_ENABLED(dp, DM_EVENT_POSTSYMLINK)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTSYMLINK,
					dp, DM_RIGHT_NULL,
					error ? NULL : ip,
					DM_RIGHT_NULL, link_name->name,
					target_path, 0, error, 0);
	}

	if (!error)
		*ipp = ip;
	return error;

 error2:
	IRELE(ip);
 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);

	goto std_return;
}

int
xfs_set_dmattrs(
	xfs_inode_t     *ip,
	u_int		evmask,
	u_int16_t	state)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_trans_t	*tp;
	int		error;

	if (!capable(CAP_SYS_ADMIN))
		return XFS_ERROR(EPERM);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES (mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = evmask;
	ip->i_d.di_dmstate  = state;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	IHOLD(ip);
	error = xfs_trans_commit(tp, 0);

	return error;
}

/*
 * xfs_alloc_file_space()
 *      This routine allocates disk space for the given file.
 *
 *	If alloc_type == 0, this request is for an ALLOCSP type
 *	request which will change the file size.  In this case, no
 *	DMAPI event will be generated by the call.  A TRUNCATE event
 *	will be generated later by xfs_setattr.
 *
 *	If alloc_type != 0, this request is for a RESVSP type
 *	request, and a DMAPI DM_EVENT_WRITE will be generated if the
 *	lower block boundary byte address is less than the file's
 *	length.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_alloc_file_space(
	xfs_inode_t		*ip,
	xfs_off_t		offset,
	xfs_off_t		len,
	int			alloc_type,
	int			attr_flags)
{
	xfs_mount_t		*mp = ip->i_mount;
	xfs_off_t		count;
	xfs_filblks_t		allocated_fsb;
	xfs_filblks_t		allocatesize_fsb;
	xfs_extlen_t		extsz, temp;
	xfs_fileoff_t		startoffset_fsb;
	xfs_fsblock_t		firstfsb;
	int			nimaps;
	int			bmapi_flag;
	int			quota_flag;
	int			rt;
	xfs_trans_t		*tp;
	xfs_bmbt_irec_t		imaps[1], *imapp;
	xfs_bmap_free_t		free_list;
	uint			qblocks, resblks, resrtextents;
	int			committed;
	int			error;

	xfs_itrace_entry(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	if (len <= 0)
		return XFS_ERROR(EINVAL);

	rt = XFS_IS_REALTIME_INODE(ip);
	extsz = xfs_get_extsz_hint(ip);

	count = len;
	imapp = &imaps[0];
	nimaps = 1;
	bmapi_flag = XFS_BMAPI_WRITE | (alloc_type ? XFS_BMAPI_PREALLOC : 0);
	startoffset_fsb	= XFS_B_TO_FSBT(mp, offset);
	allocatesize_fsb = XFS_B_TO_FSB(mp, count);

	/*	Generate a DMAPI event if needed.	*/
	if (alloc_type != 0 && offset < ip->i_size &&
			(attr_flags & XFS_ATTR_DMI) == 0  &&
			DM_EVENT_ENABLED(ip, DM_EVENT_WRITE)) {
		xfs_off_t           end_dmi_offset;

		end_dmi_offset = offset+len;
		if (end_dmi_offset > ip->i_size)
			end_dmi_offset = ip->i_size;
		error = XFS_SEND_DATA(mp, DM_EVENT_WRITE, ip, offset,
				      end_dmi_offset - offset, 0, NULL);
		if (error)
			return error;
	}

	/*
	 * Allocate file space until done or until there is an error
	 */
retry:
	while (allocatesize_fsb && !error) {
		xfs_fileoff_t	s, e;

		/*
		 * Determine space reservations for data/realtime.
		 */
		if (unlikely(extsz)) {
			s = startoffset_fsb;
			do_div(s, extsz);
			s *= extsz;
			e = startoffset_fsb + allocatesize_fsb;
			if ((temp = do_mod(startoffset_fsb, extsz)))
				e += temp;
			if ((temp = do_mod(e, extsz)))
				e += extsz - temp;
		} else {
			s = 0;
			e = allocatesize_fsb;
		}

		if (unlikely(rt)) {
			resrtextents = qblocks = (uint)(e - s);
			resrtextents /= mp->m_sb.sb_rextsize;
			resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
			quota_flag = XFS_QMOPT_RES_RTBLKS;
		} else {
			resrtextents = 0;
			resblks = qblocks = \
				XFS_DIOSTRAT_SPACE_RES(mp, (uint)(e - s));
			quota_flag = XFS_QMOPT_RES_REGBLKS;
		}

		/*
		 * Allocate and setup the transaction.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		error = xfs_trans_reserve(tp, resblks,
					  XFS_WRITE_LOG_RES(mp), resrtextents,
					  XFS_TRANS_PERM_LOG_RES,
					  XFS_WRITE_LOG_COUNT);
		/*
		 * Check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == ENOSPC || XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			break;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota_nblks(tp, ip, qblocks,
						      0, quota_flag);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * Issue the xfs_bmapi() call to allocate the blocks
		 */
		xfs_bmap_init(&free_list, &firstfsb);
		error = xfs_bmapi(tp, ip, startoffset_fsb,
				  allocatesize_fsb, bmapi_flag,
				  &firstfsb, 0, imapp, &nimaps,
				  &free_list, NULL);
		if (error) {
			goto error0;
		}

		/*
		 * Complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error) {
			break;
		}

		allocated_fsb = imapp->br_blockcount;

		if (nimaps == 0) {
			error = XFS_ERROR(ENOSPC);
			break;
		}

		startoffset_fsb += allocated_fsb;
		allocatesize_fsb -= allocated_fsb;
	}
dmapi_enospc_check:
	if (error == ENOSPC && (attr_flags & XFS_ATTR_DMI) == 0 &&
	    DM_EVENT_ENABLED(ip, DM_EVENT_NOSPACE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_NOSPACE,
				ip, DM_RIGHT_NULL,
				ip, DM_RIGHT_NULL,
				NULL, NULL, 0, 0, 0); /* Delay flag intentionally unused */
		if (error == 0)
			goto retry;	/* Maybe DMAPI app. has made space */
		/* else fall through with error from XFS_SEND_DATA */
	}

	return error;

error0:	/* Cancel bmap, unlock inode, unreserve quota blocks, cancel trans */
	xfs_bmap_cancel(&free_list);
	xfs_trans_unreserve_quota_nblks(tp, ip, qblocks, 0, quota_flag);

error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	goto dmapi_enospc_check;
}

/*
 * Zero file bytes between startoff and endoff inclusive.
 * The iolock is held exclusive and no blocks are buffered.
 *
 * This function is used by xfs_free_file_space() to zero
 * partial blocks when the range to free is not block aligned.
 * When unreserving space with boundaries that are not block
 * aligned we round up the start and round down the end
 * boundaries and then use this function to zero the parts of
 * the blocks that got dropped during the rounding.
 */
STATIC int
xfs_zero_remaining_bytes(
	xfs_inode_t		*ip,
	xfs_off_t		startoff,
	xfs_off_t		endoff)
{
	xfs_bmbt_irec_t		imap;
	xfs_fileoff_t		offset_fsb;
	xfs_off_t		lastoffset;
	xfs_off_t		offset;
	xfs_buf_t		*bp;
	xfs_mount_t		*mp = ip->i_mount;
	int			nimap;
	int			error = 0;

	/*
	 * Avoid doing I/O beyond eof - it's not necessary
	 * since nothing can read beyond eof.  The space will
	 * be zeroed when the file is extended anyway.
	 */
	if (startoff >= ip->i_size)
		return 0;

	if (endoff > ip->i_size)
		endoff = ip->i_size;

	bp = xfs_buf_get_noaddr(mp->m_sb.sb_blocksize,
				XFS_IS_REALTIME_INODE(ip) ?
				mp->m_rtdev_targp : mp->m_ddev_targp);
	if (!bp)
		return XFS_ERROR(ENOMEM);

	for (offset = startoff; offset <= endoff; offset = lastoffset + 1) {
		offset_fsb = XFS_B_TO_FSBT(mp, offset);
		nimap = 1;
		error = xfs_bmapi(NULL, ip, offset_fsb, 1, 0,
			NULL, 0, &imap, &nimap, NULL, NULL);
		if (error || nimap < 1)
			break;
		ASSERT(imap.br_blockcount >= 1);
		ASSERT(imap.br_startoff == offset_fsb);
		lastoffset = XFS_FSB_TO_B(mp, imap.br_startoff + 1) - 1;
		if (lastoffset > endoff)
			lastoffset = endoff;
		if (imap.br_startblock == HOLESTARTBLOCK)
			continue;
		ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
		if (imap.br_state == XFS_EXT_UNWRITTEN)
			continue;
		XFS_BUF_UNDONE(bp);
		XFS_BUF_UNWRITE(bp);
		XFS_BUF_READ(bp);
		XFS_BUF_SET_ADDR(bp, xfs_fsb_to_db(ip, imap.br_startblock));
		xfsbdstrat(mp, bp);
		error = xfs_iowait(bp);
		if (error) {
			xfs_ioerror_alert("xfs_zero_remaining_bytes(read)",
					  mp, bp, XFS_BUF_ADDR(bp));
			break;
		}
		memset(XFS_BUF_PTR(bp) +
			(offset - XFS_FSB_TO_B(mp, imap.br_startoff)),
		      0, lastoffset - offset + 1);
		XFS_BUF_UNDONE(bp);
		XFS_BUF_UNREAD(bp);
		XFS_BUF_WRITE(bp);
		xfsbdstrat(mp, bp);
		error = xfs_iowait(bp);
		if (error) {
			xfs_ioerror_alert("xfs_zero_remaining_bytes(write)",
					  mp, bp, XFS_BUF_ADDR(bp));
			break;
		}
	}
	xfs_buf_free(bp);
	return error;
}

/*
 * xfs_free_file_space()
 *      This routine frees disk space for the given file.
 *
 *	This routine is only called by xfs_change_file_space
 *	for an UNRESVSP type call.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
STATIC int
xfs_free_file_space(
	xfs_inode_t		*ip,
	xfs_off_t		offset,
	xfs_off_t		len,
	int			attr_flags)
{
	int			committed;
	int			done;
	xfs_off_t		end_dmi_offset;
	xfs_fileoff_t		endoffset_fsb;
	int			error;
	xfs_fsblock_t		firstfsb;
	xfs_bmap_free_t		free_list;
	xfs_bmbt_irec_t		imap;
	xfs_off_t		ioffset;
	xfs_extlen_t		mod=0;
	xfs_mount_t		*mp;
	int			nimap;
	uint			resblks;
	uint			rounding;
	int			rt;
	xfs_fileoff_t		startoffset_fsb;
	xfs_trans_t		*tp;
	int			need_iolock = 1;

	mp = ip->i_mount;

	xfs_itrace_entry(ip);

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	error = 0;
	if (len <= 0)	/* if nothing being freed */
		return error;
	rt = XFS_IS_REALTIME_INODE(ip);
	startoffset_fsb	= XFS_B_TO_FSB(mp, offset);
	end_dmi_offset = offset + len;
	endoffset_fsb = XFS_B_TO_FSBT(mp, end_dmi_offset);

	if (offset < ip->i_size && (attr_flags & XFS_ATTR_DMI) == 0 &&
	    DM_EVENT_ENABLED(ip, DM_EVENT_WRITE)) {
		if (end_dmi_offset > ip->i_size)
			end_dmi_offset = ip->i_size;
		error = XFS_SEND_DATA(mp, DM_EVENT_WRITE, ip,
				offset, end_dmi_offset - offset,
				AT_DELAY_FLAG(attr_flags), NULL);
		if (error)
			return error;
	}

	if (attr_flags & XFS_ATTR_NOLOCK)
		need_iolock = 0;
	if (need_iolock) {
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		/* wait for the completion of any pending DIOs */
		xfs_ioend_wait(ip);
	}

	rounding = max_t(uint, 1 << mp->m_sb.sb_blocklog, PAGE_CACHE_SIZE);
	ioffset = offset & ~(rounding - 1);

	if (VN_CACHED(VFS_I(ip)) != 0) {
		error = xfs_flushinval_pages(ip, ioffset, -1, FI_REMAPF_LOCKED);
		if (error)
			goto out_unlock_iolock;
	}

	/*
	 * Need to zero the stuff we're not freeing, on disk.
	 * If it's a realtime file & can't use unwritten extents then we
	 * actually need to zero the extent edges.  Otherwise xfs_bunmapi
	 * will take care of it for us.
	 */
	if (rt && !xfs_sb_version_hasextflgbit(&mp->m_sb)) {
		nimap = 1;
		error = xfs_bmapi(NULL, ip, startoffset_fsb,
			1, 0, NULL, 0, &imap, &nimap, NULL, NULL);
		if (error)
			goto out_unlock_iolock;
		ASSERT(nimap == 0 || nimap == 1);
		if (nimap && imap.br_startblock != HOLESTARTBLOCK) {
			xfs_daddr_t	block;

			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			block = imap.br_startblock;
			mod = do_div(block, mp->m_sb.sb_rextsize);
			if (mod)
				startoffset_fsb += mp->m_sb.sb_rextsize - mod;
		}
		nimap = 1;
		error = xfs_bmapi(NULL, ip, endoffset_fsb - 1,
			1, 0, NULL, 0, &imap, &nimap, NULL, NULL);
		if (error)
			goto out_unlock_iolock;
		ASSERT(nimap == 0 || nimap == 1);
		if (nimap && imap.br_startblock != HOLESTARTBLOCK) {
			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			mod++;
			if (mod && (mod != mp->m_sb.sb_rextsize))
				endoffset_fsb -= mod;
		}
	}
	if ((done = (endoffset_fsb <= startoffset_fsb)))
		/*
		 * One contiguous piece to clear
		 */
		error = xfs_zero_remaining_bytes(ip, offset, offset + len - 1);
	else {
		/*
		 * Some full blocks, possibly two pieces to clear
		 */
		if (offset < XFS_FSB_TO_B(mp, startoffset_fsb))
			error = xfs_zero_remaining_bytes(ip, offset,
				XFS_FSB_TO_B(mp, startoffset_fsb) - 1);
		if (!error &&
		    XFS_FSB_TO_B(mp, endoffset_fsb) < offset + len)
			error = xfs_zero_remaining_bytes(ip,
				XFS_FSB_TO_B(mp, endoffset_fsb),
				offset + len - 1);
	}

	/*
	 * free file space until done or until there is an error
	 */
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
	while (!error && !done) {

		/*
		 * allocate and setup the transaction. Allow this
		 * transaction to dip into the reserve blocks to ensure
		 * the freeing of the space succeeds at ENOSPC.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		tp->t_flags |= XFS_TRANS_RESERVE;
		error = xfs_trans_reserve(tp,
					  resblks,
					  XFS_WRITE_LOG_RES(mp),
					  0,
					  XFS_TRANS_PERM_LOG_RES,
					  XFS_WRITE_LOG_COUNT);

		/*
		 * check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == ENOSPC || XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			break;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota(tp, mp,
				ip->i_udquot, ip->i_gdquot,
				resblks, 0, XFS_QMOPT_RES_REGBLKS);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * issue the bunmapi() call to free the blocks
		 */
		xfs_bmap_init(&free_list, &firstfsb);
		error = xfs_bunmapi(tp, ip, startoffset_fsb,
				  endoffset_fsb - startoffset_fsb,
				  0, 2, &firstfsb, &free_list, NULL, &done);
		if (error) {
			goto error0;
		}

		/*
		 * complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

 out_unlock_iolock:
	if (need_iolock)
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;

 error0:
	xfs_bmap_cancel(&free_list);
 error1:
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, need_iolock ? (XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL) :
		    XFS_ILOCK_EXCL);
	return error;
}

/*
 * xfs_change_file_space()
 *      This routine allocates or frees disk space for the given file.
 *      The user specified parameters are checked for alignment and size
 *      limitations.
 *
 * RETURNS:
 *       0 on success
 *      errno on error
 *
 */
int
xfs_change_file_space(
	xfs_inode_t	*ip,
	int		cmd,
	xfs_flock64_t	*bf,
	xfs_off_t	offset,
	int		attr_flags)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		clrprealloc;
	int		error;
	xfs_fsize_t	fsize;
	int		setprealloc;
	xfs_off_t	startoffset;
	xfs_off_t	llen;
	xfs_trans_t	*tp;
	struct iattr	iattr;

	xfs_itrace_entry(ip);

	if (!S_ISREG(ip->i_d.di_mode))
		return XFS_ERROR(EINVAL);

	switch (bf->l_whence) {
	case 0: /*SEEK_SET*/
		break;
	case 1: /*SEEK_CUR*/
		bf->l_start += offset;
		break;
	case 2: /*SEEK_END*/
		bf->l_start += ip->i_size;
		break;
	default:
		return XFS_ERROR(EINVAL);
	}

	llen = bf->l_len > 0 ? bf->l_len - 1 : bf->l_len;

	if (   (bf->l_start < 0)
	    || (bf->l_start > XFS_MAXIOFFSET(mp))
	    || (bf->l_start + llen < 0)
	    || (bf->l_start + llen > XFS_MAXIOFFSET(mp)))
		return XFS_ERROR(EINVAL);

	bf->l_whence = 0;

	startoffset = bf->l_start;
	fsize = ip->i_size;

	/*
	 * XFS_IOC_RESVSP and XFS_IOC_UNRESVSP will reserve or unreserve
	 * file space.
	 * These calls do NOT zero the data space allocated to the file,
	 * nor do they change the file size.
	 *
	 * XFS_IOC_ALLOCSP and XFS_IOC_FREESP will allocate and free file
	 * space.
	 * These calls cause the new file data to be zeroed and the file
	 * size to be changed.
	 */
	setprealloc = clrprealloc = 0;

	switch (cmd) {
	case XFS_IOC_RESVSP:
	case XFS_IOC_RESVSP64:
		error = xfs_alloc_file_space(ip, startoffset, bf->l_len,
								1, attr_flags);
		if (error)
			return error;
		setprealloc = 1;
		break;

	case XFS_IOC_UNRESVSP:
	case XFS_IOC_UNRESVSP64:
		if ((error = xfs_free_file_space(ip, startoffset, bf->l_len,
								attr_flags)))
			return error;
		break;

	case XFS_IOC_ALLOCSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP:
	case XFS_IOC_FREESP64:
		if (startoffset > fsize) {
			error = xfs_alloc_file_space(ip, fsize,
					startoffset - fsize, 0, attr_flags);
			if (error)
				break;
		}

		iattr.ia_valid = ATTR_SIZE;
		iattr.ia_size = startoffset;

		error = xfs_setattr(ip, &iattr, attr_flags);

		if (error)
			return error;

		clrprealloc = 1;
		break;

	default:
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}

	/*
	 * update the inode timestamp, mode, and prealloc flag bits
	 */
	tp = xfs_trans_alloc(mp, XFS_TRANS_WRITEID);

	if ((error = xfs_trans_reserve(tp, 0, XFS_WRITEID_LOG_RES(mp),
				      0, 0, 0))) {
		/* ASSERT(0); */
		xfs_trans_cancel(tp, 0);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);

	if ((attr_flags & XFS_ATTR_DMI) == 0) {
		ip->i_d.di_mode &= ~S_ISUID;

		/*
		 * Note that we don't have to worry about mandatory
		 * file locking being disabled here because we only
		 * clear the S_ISGID bit if the Group execute bit is
		 * on, but if it was on then mandatory locking wouldn't
		 * have been enabled.
		 */
		if (ip->i_d.di_mode & S_IXGRP)
			ip->i_d.di_mode &= ~S_ISGID;

		xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	}
	if (setprealloc)
		ip->i_d.di_flags |= XFS_DIFLAG_PREALLOC;
	else if (clrprealloc)
		ip->i_d.di_flags &= ~XFS_DIFLAG_PREALLOC;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp, 0);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	return error;
}
