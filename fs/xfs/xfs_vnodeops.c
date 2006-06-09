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
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_dir_leaf.h"
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_rw.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_utils.h"
#include "xfs_rtalloc.h"
#include "xfs_refcache.h"
#include "xfs_trans_space.h"
#include "xfs_log_priv.h"
#include "xfs_mac.h"

STATIC int
xfs_open(
	bhv_desc_t	*bdp,
	cred_t		*credp)
{
	int		mode;
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	xfs_inode_t	*ip = XFS_BHVTOI(bdp);

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return XFS_ERROR(EIO);

	/*
	 * If it's a directory with any blocks, read-ahead block 0
	 * as we're almost certain to have the next operation be a read there.
	 */
	if (VN_ISDIR(vp) && ip->i_d.di_nextents > 0) {
		mode = xfs_ilock_map_shared(ip);
		if (ip->i_d.di_nextents > 0)
			(void)xfs_da_reada_buf(NULL, ip, 0, XFS_DATA_FORK);
		xfs_iunlock(ip, mode);
	}
	return 0;
}

STATIC int
xfs_close(
	bhv_desc_t	*bdp,
	int		flags,
	lastclose_t	lastclose,
	cred_t		*credp)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	xfs_inode_t	*ip = XFS_BHVTOI(bdp);
	int		error = 0;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return XFS_ERROR(EIO);

	if (lastclose != L_TRUE || !VN_ISREG(vp))
		return 0;

	/*
	 * If we previously truncated this file and removed old data in
	 * the process, we want to initiate "early" writeout on the last
	 * close.  This is an attempt to combat the notorious NULL files
	 * problem which is particularly noticable from a truncate down,
	 * buffered (re-)write (delalloc), followed by a crash.  What we
	 * are effectively doing here is significantly reducing the time
	 * window where we'd otherwise be exposed to that problem.
	 */
	if (VUNTRUNCATE(vp) && VN_DIRTY(vp) && ip->i_delayed_blks > 0)
		VOP_FLUSH_PAGES(vp, 0, -1, XFS_B_ASYNC, FI_NONE, error);
	return error;
}

/*
 * xfs_getattr
 */
STATIC int
xfs_getattr(
	bhv_desc_t	*bdp,
	vattr_t		*vap,
	int		flags,
	cred_t		*credp)
{
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	vnode_t		*vp;

	vp  = BHV_TO_VNODE(bdp);
	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	if (!(flags & ATTR_LAZY))
		xfs_ilock(ip, XFS_ILOCK_SHARED);

	vap->va_size = ip->i_d.di_size;
	if (vap->va_mask == XFS_AT_SIZE)
		goto all_done;

	vap->va_nblocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);
	vap->va_nodeid = ip->i_ino;
#if XFS_BIG_INUMS
	vap->va_nodeid += mp->m_inoadd;
#endif
	vap->va_nlink = ip->i_d.di_nlink;

	/*
	 * Quick exit for non-stat callers
	 */
	if ((vap->va_mask &
	    ~(XFS_AT_SIZE|XFS_AT_FSID|XFS_AT_NODEID|
	      XFS_AT_NLINK|XFS_AT_BLKSIZE)) == 0)
		goto all_done;

	/*
	 * Copy from in-core inode.
	 */
	vap->va_mode = ip->i_d.di_mode;
	vap->va_uid = ip->i_d.di_uid;
	vap->va_gid = ip->i_d.di_gid;
	vap->va_projid = ip->i_d.di_projid;

	/*
	 * Check vnode type block/char vs. everything else.
	 */
	switch (ip->i_d.di_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		vap->va_rdev = ip->i_df.if_u2.if_rdev;
		vap->va_blocksize = BLKDEV_IOSIZE;
		break;
	default:
		vap->va_rdev = 0;

		if (!(ip->i_d.di_flags & XFS_DIFLAG_REALTIME)) {
			vap->va_blocksize = xfs_preferred_iosize(mp);
		} else {

			/*
			 * If the file blocks are being allocated from a
			 * realtime partition, then return the inode's
			 * realtime extent size or the realtime volume's
			 * extent size.
			 */
			vap->va_blocksize = ip->i_d.di_extsize ?
				(ip->i_d.di_extsize << mp->m_sb.sb_blocklog) :
				(mp->m_sb.sb_rextsize << mp->m_sb.sb_blocklog);
		}
		break;
	}

	vn_atime_to_timespec(vp, &vap->va_atime);
	vap->va_mtime.tv_sec = ip->i_d.di_mtime.t_sec;
	vap->va_mtime.tv_nsec = ip->i_d.di_mtime.t_nsec;
	vap->va_ctime.tv_sec = ip->i_d.di_ctime.t_sec;
	vap->va_ctime.tv_nsec = ip->i_d.di_ctime.t_nsec;

	/*
	 * Exit for stat callers.  See if any of the rest of the fields
	 * to be filled in are needed.
	 */
	if ((vap->va_mask &
	     (XFS_AT_XFLAGS|XFS_AT_EXTSIZE|XFS_AT_NEXTENTS|XFS_AT_ANEXTENTS|
	      XFS_AT_GENCOUNT|XFS_AT_VCODE)) == 0)
		goto all_done;

	/*
	 * Convert di_flags to xflags.
	 */
	vap->va_xflags = xfs_ip2xflags(ip);

	/*
	 * Exit for inode revalidate.  See if any of the rest of
	 * the fields to be filled in are needed.
	 */
	if ((vap->va_mask &
	     (XFS_AT_EXTSIZE|XFS_AT_NEXTENTS|XFS_AT_ANEXTENTS|
	      XFS_AT_GENCOUNT|XFS_AT_VCODE)) == 0)
		goto all_done;

	vap->va_extsize = ip->i_d.di_extsize << mp->m_sb.sb_blocklog;
	vap->va_nextents =
		(ip->i_df.if_flags & XFS_IFEXTENTS) ?
			ip->i_df.if_bytes / sizeof(xfs_bmbt_rec_t) :
			ip->i_d.di_nextents;
	if (ip->i_afp)
		vap->va_anextents =
			(ip->i_afp->if_flags & XFS_IFEXTENTS) ?
				ip->i_afp->if_bytes / sizeof(xfs_bmbt_rec_t) :
				 ip->i_d.di_anextents;
	else
		vap->va_anextents = 0;
	vap->va_gen = ip->i_d.di_gen;

 all_done:
	if (!(flags & ATTR_LAZY))
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return 0;
}


/*
 * xfs_setattr
 */
int
xfs_setattr(
	bhv_desc_t		*bdp,
	vattr_t			*vap,
	int			flags,
	cred_t			*credp)
{
	xfs_inode_t		*ip;
	xfs_trans_t		*tp;
	xfs_mount_t		*mp;
	int			mask;
	int			code;
	uint			lock_flags;
	uint			commit_flags=0;
	uid_t			uid=0, iuid=0;
	gid_t			gid=0, igid=0;
	int			timeflags = 0;
	vnode_t			*vp;
	xfs_prid_t		projid=0, iprojid=0;
	int			mandlock_before, mandlock_after;
	struct xfs_dquot	*udqp, *gdqp, *olddquot1, *olddquot2;
	int			file_owner;
	int			need_iolock = 1;

	vp = BHV_TO_VNODE(bdp);
	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		return XFS_ERROR(EROFS);

	/*
	 * Cannot set certain attributes.
	 */
	mask = vap->va_mask;
	if (mask & XFS_AT_NOSET) {
		return XFS_ERROR(EINVAL);
	}

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	/*
	 * Timestamps do not need to be logged and hence do not
	 * need to be done within a transaction.
	 */
	if (mask & XFS_AT_UPDTIMES) {
		ASSERT((mask & ~XFS_AT_UPDTIMES) == 0);
		timeflags = ((mask & XFS_AT_UPDATIME) ? XFS_ICHGTIME_ACC : 0) |
			    ((mask & XFS_AT_UPDCTIME) ? XFS_ICHGTIME_CHG : 0) |
			    ((mask & XFS_AT_UPDMTIME) ? XFS_ICHGTIME_MOD : 0);
		xfs_ichgtime(ip, timeflags);
		return 0;
	}

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
	if (XFS_IS_QUOTA_ON(mp) &&
	    (mask & (XFS_AT_UID|XFS_AT_GID|XFS_AT_PROJID))) {
		uint	qflags = 0;

		if ((mask & XFS_AT_UID) && XFS_IS_UQUOTA_ON(mp)) {
			uid = vap->va_uid;
			qflags |= XFS_QMOPT_UQUOTA;
		} else {
			uid = ip->i_d.di_uid;
		}
		if ((mask & XFS_AT_GID) && XFS_IS_GQUOTA_ON(mp)) {
			gid = vap->va_gid;
			qflags |= XFS_QMOPT_GQUOTA;
		}  else {
			gid = ip->i_d.di_gid;
		}
		if ((mask & XFS_AT_PROJID) && XFS_IS_PQUOTA_ON(mp)) {
			projid = vap->va_projid;
			qflags |= XFS_QMOPT_PQUOTA;
		}  else {
			projid = ip->i_d.di_projid;
		}
		/*
		 * We take a reference when we initialize udqp and gdqp,
		 * so it is important that we never blindly double trip on
		 * the same variable. See xfs_create() for an example.
		 */
		ASSERT(udqp == NULL);
		ASSERT(gdqp == NULL);
		code = XFS_QM_DQVOPALLOC(mp, ip, uid, gid, projid, qflags,
					 &udqp, &gdqp);
		if (code)
			return code;
	}

	/*
	 * For the other attributes, we acquire the inode lock and
	 * first do an error checking pass.
	 */
	tp = NULL;
	lock_flags = XFS_ILOCK_EXCL;
	if (flags & ATTR_NOLOCK)
		need_iolock = 0;
	if (!(mask & XFS_AT_SIZE)) {
		if ((mask != (XFS_AT_CTIME|XFS_AT_ATIME|XFS_AT_MTIME)) ||
		    (mp->m_flags & XFS_MOUNT_WSYNC)) {
			tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_NOT_SIZE);
			commit_flags = 0;
			if ((code = xfs_trans_reserve(tp, 0,
						     XFS_ICHANGE_LOG_RES(mp), 0,
						     0, 0))) {
				lock_flags = 0;
				goto error_return;
			}
		}
	} else {
		if (DM_EVENT_ENABLED (vp->v_vfsp, ip, DM_EVENT_TRUNCATE) &&
		    !(flags & ATTR_DMI)) {
			int dmflags = AT_DELAY_FLAG(flags) | DM_SEM_FLAG_WR;
			code = XFS_SEND_DATA(mp, DM_EVENT_TRUNCATE, vp,
				vap->va_size, 0, dmflags, NULL);
			if (code) {
				lock_flags = 0;
				goto error_return;
			}
		}
		if (need_iolock)
			lock_flags |= XFS_IOLOCK_EXCL;
	}

	xfs_ilock(ip, lock_flags);

	/* boolean: are we the file owner? */
	file_owner = (current_fsuid(credp) == ip->i_d.di_uid);

	/*
	 * Change various properties of a file.
	 * Only the owner or users with CAP_FOWNER
	 * capability may do these things.
	 */
	if (mask &
	    (XFS_AT_MODE|XFS_AT_XFLAGS|XFS_AT_EXTSIZE|XFS_AT_UID|
	     XFS_AT_GID|XFS_AT_PROJID)) {
		/*
		 * CAP_FOWNER overrides the following restrictions:
		 *
		 * The user ID of the calling process must be equal
		 * to the file owner ID, except in cases where the
		 * CAP_FSETID capability is applicable.
		 */
		if (!file_owner && !capable(CAP_FOWNER)) {
			code = XFS_ERROR(EPERM);
			goto error_return;
		}

		/*
		 * CAP_FSETID overrides the following restrictions:
		 *
		 * The effective user ID of the calling process shall match
		 * the file owner when setting the set-user-ID and
		 * set-group-ID bits on that file.
		 *
		 * The effective group ID or one of the supplementary group
		 * IDs of the calling process shall match the group owner of
		 * the file when setting the set-group-ID bit on that file
		 */
		if (mask & XFS_AT_MODE) {
			mode_t m = 0;

			if ((vap->va_mode & S_ISUID) && !file_owner)
				m |= S_ISUID;
			if ((vap->va_mode & S_ISGID) &&
			    !in_group_p((gid_t)ip->i_d.di_gid))
				m |= S_ISGID;
#if 0
			/* Linux allows this, Irix doesn't. */
			if ((vap->va_mode & S_ISVTX) && !VN_ISDIR(vp))
				m |= S_ISVTX;
#endif
			if (m && !capable(CAP_FSETID))
				vap->va_mode &= ~m;
		}
	}

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 * If the system was configured with the "restricted_chown"
	 * option, the owner is not permitted to give away the file,
	 * and can change the group id only to a group of which he
	 * or she is a member.
	 */
	if (mask & (XFS_AT_UID|XFS_AT_GID|XFS_AT_PROJID)) {
		/*
		 * These IDs could have changed since we last looked at them.
		 * But, we're assured that if the ownership did change
		 * while we didn't have the inode locked, inode's dquot(s)
		 * would have changed also.
		 */
		iuid = ip->i_d.di_uid;
		iprojid = ip->i_d.di_projid;
		igid = ip->i_d.di_gid;
		gid = (mask & XFS_AT_GID) ? vap->va_gid : igid;
		uid = (mask & XFS_AT_UID) ? vap->va_uid : iuid;
		projid = (mask & XFS_AT_PROJID) ? (xfs_prid_t)vap->va_projid :
			 iprojid;

		/*
		 * CAP_CHOWN overrides the following restrictions:
		 *
		 * If _POSIX_CHOWN_RESTRICTED is defined, this capability
		 * shall override the restriction that a process cannot
		 * change the user ID of a file it owns and the restriction
		 * that the group ID supplied to the chown() function
		 * shall be equal to either the group ID or one of the
		 * supplementary group IDs of the calling process.
		 */
		if (restricted_chown &&
		    (iuid != uid || (igid != gid &&
				     !in_group_p((gid_t)gid))) &&
		    !capable(CAP_CHOWN)) {
			code = XFS_ERROR(EPERM);
			goto error_return;
		}
		/*
		 * Do a quota reservation only if uid/projid/gid is actually
		 * going to change.
		 */
		if ((XFS_IS_UQUOTA_ON(mp) && iuid != uid) ||
		    (XFS_IS_PQUOTA_ON(mp) && iprojid != projid) ||
		    (XFS_IS_GQUOTA_ON(mp) && igid != gid)) {
			ASSERT(tp);
			code = XFS_QM_DQVOPCHOWNRESV(mp, tp, ip, udqp, gdqp,
						capable(CAP_FOWNER) ?
						XFS_QMOPT_FORCE_RES : 0);
			if (code)	/* out of quota */
				goto error_return;
		}
	}

	/*
	 * Truncate file.  Must have write permission and not be a directory.
	 */
	if (mask & XFS_AT_SIZE) {
		/* Short circuit the truncate case for zero length files */
		if ((vap->va_size == 0) &&
		   (ip->i_d.di_size == 0) && (ip->i_d.di_nextents == 0)) {
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			lock_flags &= ~XFS_ILOCK_EXCL;
			if (mask & XFS_AT_CTIME)
				xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
			code = 0;
			goto error_return;
		}

		if (VN_ISDIR(vp)) {
			code = XFS_ERROR(EISDIR);
			goto error_return;
		} else if (!VN_ISREG(vp)) {
			code = XFS_ERROR(EINVAL);
			goto error_return;
		}
		/*
		 * Make sure that the dquots are attached to the inode.
		 */
		if ((code = XFS_QM_DQATTACH(mp, ip, XFS_QMOPT_ILOCKED)))
			goto error_return;
	}

	/*
	 * Change file access or modified times.
	 */
	if (mask & (XFS_AT_ATIME|XFS_AT_MTIME)) {
		if (!file_owner) {
			if ((flags & ATTR_UTIME) &&
			    !capable(CAP_FOWNER)) {
				code = XFS_ERROR(EPERM);
				goto error_return;
			}
		}
	}

	/*
	 * Change extent size or realtime flag.
	 */
	if (mask & (XFS_AT_EXTSIZE|XFS_AT_XFLAGS)) {
		/*
		 * Can't change extent size if any extents are allocated.
		 */
		if (ip->i_d.di_nextents && (mask & XFS_AT_EXTSIZE) &&
		    ((ip->i_d.di_extsize << mp->m_sb.sb_blocklog) !=
		     vap->va_extsize) ) {
			code = XFS_ERROR(EINVAL);	/* EFBIG? */
			goto error_return;
		}

		/*
		 * Can't change realtime flag if any extents are allocated.
		 */
		if ((ip->i_d.di_nextents || ip->i_delayed_blks) &&
		    (mask & XFS_AT_XFLAGS) &&
		    (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) !=
		    (vap->va_xflags & XFS_XFLAG_REALTIME)) {
			code = XFS_ERROR(EINVAL);	/* EFBIG? */
			goto error_return;
		}
		/*
		 * Extent size must be a multiple of the appropriate block
		 * size, if set at all.
		 */
		if ((mask & XFS_AT_EXTSIZE) && vap->va_extsize != 0) {
			xfs_extlen_t	size;

			if ((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) ||
			    ((mask & XFS_AT_XFLAGS) &&
			    (vap->va_xflags & XFS_XFLAG_REALTIME))) {
				size = mp->m_sb.sb_rextsize <<
				       mp->m_sb.sb_blocklog;
			} else {
				size = mp->m_sb.sb_blocksize;
			}
			if (vap->va_extsize % size) {
				code = XFS_ERROR(EINVAL);
				goto error_return;
			}
		}
		/*
		 * If realtime flag is set then must have realtime data.
		 */
		if ((mask & XFS_AT_XFLAGS) &&
		    (vap->va_xflags & XFS_XFLAG_REALTIME)) {
			if ((mp->m_sb.sb_rblocks == 0) ||
			    (mp->m_sb.sb_rextsize == 0) ||
			    (ip->i_d.di_extsize % mp->m_sb.sb_rextsize)) {
				code = XFS_ERROR(EINVAL);
				goto error_return;
			}
		}

		/*
		 * Can't modify an immutable/append-only file unless
		 * we have appropriate permission.
		 */
		if ((mask & XFS_AT_XFLAGS) &&
		    (ip->i_d.di_flags &
				(XFS_DIFLAG_IMMUTABLE|XFS_DIFLAG_APPEND) ||
		     (vap->va_xflags &
				(XFS_XFLAG_IMMUTABLE | XFS_XFLAG_APPEND))) &&
		    !capable(CAP_LINUX_IMMUTABLE)) {
			code = XFS_ERROR(EPERM);
			goto error_return;
		}
	}

	/*
	 * Now we can make the changes.  Before we join the inode
	 * to the transaction, if XFS_AT_SIZE is set then take care of
	 * the part of the truncation that must be done without the
	 * inode lock.  This needs to be done before joining the inode
	 * to the transaction, because the inode cannot be unlocked
	 * once it is a part of the transaction.
	 */
	if (mask & XFS_AT_SIZE) {
		code = 0;
		if ((vap->va_size > ip->i_d.di_size) && 
		    (flags & ATTR_NOSIZETOK) == 0) {
			code = xfs_igrow_start(ip, vap->va_size, credp);
		}
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		vn_iowait(vp); /* wait for the completion of any pending DIOs */
		if (!code)
			code = xfs_itruncate_data(ip, vap->va_size);
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
	}

	if (tp) {
		xfs_trans_ijoin(tp, ip, lock_flags);
		xfs_trans_ihold(tp, ip);
	}

	/* determine whether mandatory locking mode changes */
	mandlock_before = MANDLOCK(vp, ip->i_d.di_mode);

	/*
	 * Truncate file.  Must have write permission and not be a directory.
	 */
	if (mask & XFS_AT_SIZE) {
		if (vap->va_size > ip->i_d.di_size) {
			xfs_igrow_finish(tp, ip, vap->va_size,
			    !(flags & ATTR_DMI));
		} else if ((vap->va_size <= ip->i_d.di_size) ||
			   ((vap->va_size == 0) && ip->i_d.di_nextents)) {
			/*
			 * signal a sync transaction unless
			 * we're truncating an already unlinked
			 * file on a wsync filesystem
			 */
			code = xfs_itruncate_finish(&tp, ip,
					    (xfs_fsize_t)vap->va_size,
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
			VTRUNCATE(vp);
		}
		/*
		 * Have to do this even if the file's size doesn't change.
		 */
		timeflags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
	}

	/*
	 * Change file access modes.
	 */
	if (mask & XFS_AT_MODE) {
		ip->i_d.di_mode &= S_IFMT;
		ip->i_d.di_mode |= vap->va_mode & ~S_IFMT;

		xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);
		timeflags |= XFS_ICHGTIME_CHG;
	}

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 * If the system was configured with the "restricted_chown"
	 * option, the owner is not permitted to give away the file,
	 * and can change the group id only to a group of which he
	 * or she is a member.
	 */
	if (mask & (XFS_AT_UID|XFS_AT_GID|XFS_AT_PROJID)) {
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
			if (XFS_IS_UQUOTA_ON(mp)) {
				ASSERT(mask & XFS_AT_UID);
				ASSERT(udqp);
				olddquot1 = XFS_QM_DQVOPCHOWN(mp, tp, ip,
							&ip->i_udquot, udqp);
			}
			ip->i_d.di_uid = uid;
		}
		if (igid != gid) {
			if (XFS_IS_GQUOTA_ON(mp)) {
				ASSERT(!XFS_IS_PQUOTA_ON(mp));
				ASSERT(mask & XFS_AT_GID);
				ASSERT(gdqp);
				olddquot2 = XFS_QM_DQVOPCHOWN(mp, tp, ip,
							&ip->i_gdquot, gdqp);
			}
			ip->i_d.di_gid = gid;
		}
		if (iprojid != projid) {
			if (XFS_IS_PQUOTA_ON(mp)) {
				ASSERT(!XFS_IS_GQUOTA_ON(mp));
				ASSERT(mask & XFS_AT_PROJID);
				ASSERT(gdqp);
				olddquot2 = XFS_QM_DQVOPCHOWN(mp, tp, ip,
							&ip->i_gdquot, gdqp);
			}
			ip->i_d.di_projid = projid;
			/*
			 * We may have to rev the inode as well as
			 * the superblock version number since projids didn't
			 * exist before DINODE_VERSION_2 and SB_VERSION_NLINK.
			 */
			if (ip->i_d.di_version == XFS_DINODE_VERSION_1)
				xfs_bump_ino_vers2(tp, ip);
		}

		xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);
		timeflags |= XFS_ICHGTIME_CHG;
	}


	/*
	 * Change file access or modified times.
	 */
	if (mask & (XFS_AT_ATIME|XFS_AT_MTIME)) {
		if (mask & XFS_AT_ATIME) {
			ip->i_d.di_atime.t_sec = vap->va_atime.tv_sec;
			ip->i_d.di_atime.t_nsec = vap->va_atime.tv_nsec;
			ip->i_update_core = 1;
			timeflags &= ~XFS_ICHGTIME_ACC;
		}
		if (mask & XFS_AT_MTIME) {
			ip->i_d.di_mtime.t_sec = vap->va_mtime.tv_sec;
			ip->i_d.di_mtime.t_nsec = vap->va_mtime.tv_nsec;
			timeflags &= ~XFS_ICHGTIME_MOD;
			timeflags |= XFS_ICHGTIME_CHG;
		}
		if (tp && (flags & ATTR_UTIME))
			xfs_trans_log_inode (tp, ip, XFS_ILOG_CORE);
	}

	/*
	 * Change XFS-added attributes.
	 */
	if (mask & (XFS_AT_EXTSIZE|XFS_AT_XFLAGS)) {
		if (mask & XFS_AT_EXTSIZE) {
			/*
			 * Converting bytes to fs blocks.
			 */
			ip->i_d.di_extsize = vap->va_extsize >>
				mp->m_sb.sb_blocklog;
		}
		if (mask & XFS_AT_XFLAGS) {
			uint	di_flags;

			/* can't set PREALLOC this way, just preserve it */
			di_flags = (ip->i_d.di_flags & XFS_DIFLAG_PREALLOC);
			if (vap->va_xflags & XFS_XFLAG_IMMUTABLE)
				di_flags |= XFS_DIFLAG_IMMUTABLE;
			if (vap->va_xflags & XFS_XFLAG_APPEND)
				di_flags |= XFS_DIFLAG_APPEND;
			if (vap->va_xflags & XFS_XFLAG_SYNC)
				di_flags |= XFS_DIFLAG_SYNC;
			if (vap->va_xflags & XFS_XFLAG_NOATIME)
				di_flags |= XFS_DIFLAG_NOATIME;
			if (vap->va_xflags & XFS_XFLAG_NODUMP)
				di_flags |= XFS_DIFLAG_NODUMP;
			if (vap->va_xflags & XFS_XFLAG_PROJINHERIT)
				di_flags |= XFS_DIFLAG_PROJINHERIT;
			if (vap->va_xflags & XFS_XFLAG_NODEFRAG)
				di_flags |= XFS_DIFLAG_NODEFRAG;
			if ((ip->i_d.di_mode & S_IFMT) == S_IFDIR) {
				if (vap->va_xflags & XFS_XFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_RTINHERIT;
				if (vap->va_xflags & XFS_XFLAG_NOSYMLINKS)
					di_flags |= XFS_DIFLAG_NOSYMLINKS;
				if (vap->va_xflags & XFS_XFLAG_EXTSZINHERIT)
					di_flags |= XFS_DIFLAG_EXTSZINHERIT;
			} else if ((ip->i_d.di_mode & S_IFMT) == S_IFREG) {
				if (vap->va_xflags & XFS_XFLAG_REALTIME) {
					di_flags |= XFS_DIFLAG_REALTIME;
					ip->i_iocore.io_flags |= XFS_IOCORE_RT;
				} else {
					ip->i_iocore.io_flags &= ~XFS_IOCORE_RT;
				}
				if (vap->va_xflags & XFS_XFLAG_EXTSIZE)
					di_flags |= XFS_DIFLAG_EXTSIZE;
			}
			ip->i_d.di_flags = di_flags;
		}
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		timeflags |= XFS_ICHGTIME_CHG;
	}

	/*
	 * Change file inode change time only if XFS_AT_CTIME set
	 * AND we have been called by a DMI function.
	 */

	if ( (flags & ATTR_DMI) && (mask & XFS_AT_CTIME) ) {
		ip->i_d.di_ctime.t_sec = vap->va_ctime.tv_sec;
		ip->i_d.di_ctime.t_nsec = vap->va_ctime.tv_nsec;
		ip->i_update_core = 1;
		timeflags &= ~XFS_ICHGTIME_CHG;
	}

	/*
	 * Send out timestamp changes that need to be set to the
	 * current time.  Not done when called by a DMI function.
	 */
	if (timeflags && !(flags & ATTR_DMI))
		xfs_ichgtime(ip, timeflags);

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
	if (tp) {
		if (mp->m_flags & XFS_MOUNT_WSYNC)
			xfs_trans_set_sync(tp);

		code = xfs_trans_commit(tp, commit_flags, NULL);
	}

	/*
	 * If the (regular) file's mandatory locking mode changed, then
	 * notify the vnode.  We do this under the inode lock to prevent
	 * racing calls to vop_vnode_change.
	 */
	mandlock_after = MANDLOCK(vp, ip->i_d.di_mode);
	if (mandlock_before != mandlock_after) {
		VOP_VNODE_CHANGE(vp, VCHANGE_FLAGS_ENF_LOCKING,
				 mandlock_after);
	}

	xfs_iunlock(ip, lock_flags);

	/*
	 * Release any dquot(s) the inode had kept before chown.
	 */
	XFS_QM_DQRELE(mp, olddquot1);
	XFS_QM_DQRELE(mp, olddquot2);
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	if (code) {
		return code;
	}

	if (DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_EVENT_ATTRIBUTE) &&
	    !(flags & ATTR_DMI)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_ATTRIBUTE, vp, DM_RIGHT_NULL,
					NULL, DM_RIGHT_NULL, NULL, NULL,
					0, 0, AT_DELAY_FLAG(flags));
	}
	return 0;

 abort_return:
	commit_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */
 error_return:
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);
	if (tp) {
		xfs_trans_cancel(tp, commit_flags);
	}
	if (lock_flags != 0) {
		xfs_iunlock(ip, lock_flags);
	}
	return code;
}


/*
 * xfs_access
 * Null conversion from vnode mode bits to inode mode bits, as in efs.
 */
STATIC int
xfs_access(
	bhv_desc_t	*bdp,
	int		mode,
	cred_t		*credp)
{
	xfs_inode_t	*ip;
	int		error;

	vn_trace_entry(BHV_TO_VNODE(bdp), __FUNCTION__,
					       (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = xfs_iaccess(ip, mode, credp);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}


/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 2 extents back from
 * bmapi.
 */
#define SYMLINK_MAPS 2

/*
 * xfs_readlink
 *
 */
STATIC int
xfs_readlink(
	bhv_desc_t	*bdp,
	uio_t		*uiop,
	int		ioflags,
	cred_t		*credp)
{
	xfs_inode_t     *ip;
	int		count;
	xfs_off_t	offset;
	int		pathlen;
	vnode_t		*vp;
	int		error = 0;
	xfs_mount_t	*mp;
	int             nmaps;
	xfs_bmbt_irec_t mval[SYMLINK_MAPS];
	xfs_daddr_t	d;
	int		byte_cnt;
	int		n;
	xfs_buf_t	*bp;

	vp = BHV_TO_VNODE(bdp);
	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	xfs_ilock(ip, XFS_ILOCK_SHARED);

	ASSERT((ip->i_d.di_mode & S_IFMT) == S_IFLNK);

	offset = uiop->uio_offset;
	count = uiop->uio_resid;

	if (offset < 0) {
		error = XFS_ERROR(EINVAL);
		goto error_return;
	}
	if (count <= 0) {
		error = 0;
		goto error_return;
	}

	/*
	 * See if the symlink is stored inline.
	 */
	pathlen = (int)ip->i_d.di_size;

	if (ip->i_df.if_flags & XFS_IFINLINE) {
		error = uio_read(ip->i_df.if_u1.if_data, pathlen, uiop);
	}
	else {
		/*
		 * Symlink not inline.  Call bmap to get it in.
		 */
		nmaps = SYMLINK_MAPS;

		error = xfs_bmapi(NULL, ip, 0, XFS_B_TO_FSB(mp, pathlen),
				  0, NULL, 0, mval, &nmaps, NULL, NULL);

		if (error) {
			goto error_return;
		}

		for (n = 0; n < nmaps; n++) {
			d = XFS_FSB_TO_DADDR(mp, mval[n].br_startblock);
			byte_cnt = XFS_FSB_TO_B(mp, mval[n].br_blockcount);
			bp = xfs_buf_read(mp->m_ddev_targp, d,
				      BTOBB(byte_cnt), 0);
			error = XFS_BUF_GETERROR(bp);
			if (error) {
				xfs_ioerror_alert("xfs_readlink",
					  ip->i_mount, bp, XFS_BUF_ADDR(bp));
				xfs_buf_relse(bp);
				goto error_return;
			}
			if (pathlen < byte_cnt)
				byte_cnt = pathlen;
			pathlen -= byte_cnt;

			error = uio_read(XFS_BUF_PTR(bp), byte_cnt, uiop);
			xfs_buf_relse (bp);
		}

	}

error_return:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}


/*
 * xfs_fsync
 *
 * This is called to sync the inode and its data out to disk.
 * We need to hold the I/O lock while flushing the data, and
 * the inode lock while flushing the inode.  The inode lock CANNOT
 * be held while flushing the data, so acquire after we're done
 * with that.
 */
STATIC int
xfs_fsync(
	bhv_desc_t	*bdp,
	int		flag,
	cred_t		*credp,
	xfs_off_t	start,
	xfs_off_t	stop)
{
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	int		error;
	int		log_flushed = 0, changed = 1;

	vn_trace_entry(BHV_TO_VNODE(bdp),
			__FUNCTION__, (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);

	ASSERT(start >= 0 && stop >= -1);

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		return XFS_ERROR(EIO);

	/*
	 * We always need to make sure that the required inode state
	 * is safe on disk.  The vnode might be clean but because
	 * of committed transactions that haven't hit the disk yet.
	 * Likewise, there could be unflushed non-transactional
	 * changes to the inode core that have to go to disk.
	 *
	 * The following code depends on one assumption:  that
	 * any transaction that changes an inode logs the core
	 * because it has to change some field in the inode core
	 * (typically nextents or nblocks).  That assumption
	 * implies that any transactions against an inode will
	 * catch any non-transactional updates.  If inode-altering
	 * transactions exist that violate this assumption, the
	 * code breaks.  Right now, it figures that if the involved
	 * update_* field is clear and the inode is unpinned, the
	 * inode is clean.  Either it's been flushed or it's been
	 * committed and the commit has hit the disk unpinning the inode.
	 * (Note that xfs_inode_item_format() called at commit clears
	 * the update_* fields.)
	 */
	xfs_ilock(ip, XFS_ILOCK_SHARED);

	/* If we are flushing data then we care about update_size
	 * being set, otherwise we care about update_core
	 */
	if ((flag & FSYNC_DATA) ?
			(ip->i_update_size == 0) :
			(ip->i_update_core == 0)) {
		/*
		 * Timestamps/size haven't changed since last inode
		 * flush or inode transaction commit.  That means
		 * either nothing got written or a transaction
		 * committed which caught the updates.	If the
		 * latter happened and the transaction hasn't
		 * hit the disk yet, the inode will be still
		 * be pinned.  If it is, force the log.
		 */

		xfs_iunlock(ip, XFS_ILOCK_SHARED);

		if (xfs_ipincount(ip)) {
			_xfs_log_force(ip->i_mount, (xfs_lsn_t)0,
				      XFS_LOG_FORCE |
				      ((flag & FSYNC_WAIT)
				       ? XFS_LOG_SYNC : 0),
				      &log_flushed);
		} else {
			/*
			 * If the inode is not pinned and nothing
			 * has changed we don't need to flush the
			 * cache.
			 */
			changed = 0;
		}
		error = 0;
	} else	{
		/*
		 * Kick off a transaction to log the inode
		 * core to get the updates.  Make it
		 * sync if FSYNC_WAIT is passed in (which
		 * is done by everybody but specfs).  The
		 * sync transaction will also force the log.
		 */
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		tp = xfs_trans_alloc(ip->i_mount, XFS_TRANS_FSYNC_TS);
		if ((error = xfs_trans_reserve(tp, 0,
				XFS_FSYNC_TS_LOG_RES(ip->i_mount),
				0, 0, 0)))  {
			xfs_trans_cancel(tp, 0);
			return error;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		/*
		 * Note - it's possible that we might have pushed
		 * ourselves out of the way during trans_reserve
		 * which would flush the inode.	 But there's no
		 * guarantee that the inode buffer has actually
		 * gone out yet (it's delwri).	Plus the buffer
		 * could be pinned anyway if it's part of an
		 * inode in another recent transaction.	 So we
		 * play it safe and fire off the transaction anyway.
		 */
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		if (flag & FSYNC_WAIT)
			xfs_trans_set_sync(tp);
		error = _xfs_trans_commit(tp, 0, NULL, &log_flushed);

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
		if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)
			xfs_blkdev_issue_flush(ip->i_mount->m_rtdev_targp);
	}

	return error;
}

/*
 * This is called by xfs_inactive to free any blocks beyond eof,
 * when the link count isn't zero.
 */
STATIC int
xfs_inactive_free_eofblocks(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip)
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
	end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)ip->i_d.di_size));
	last_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_MAXIOFFSET(mp));
	map_len = last_fsb - end_fsb;
	if (map_len <= 0)
		return 0;

	nimaps = 1;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = XFS_BMAPI(mp, NULL, &ip->i_iocore, end_fsb, map_len, 0,
			  NULL, 0, &imap, &nimaps, NULL, NULL);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!error && (nimaps != 0) &&
	    (imap.br_startblock != HOLESTARTBLOCK ||
	     ip->i_delayed_blks)) {
		/*
		 * Attach the dquots to the inode up front.
		 */
		if ((error = XFS_QM_DQATTACH(mp, ip, 0)))
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
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE,
				    ip->i_d.di_size);

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
					     ip->i_d.di_size,
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
						XFS_TRANS_RELEASE_LOG_RES,
						NULL);
		}
		xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
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
	XFS_BMAP_INIT(&free_list, &first_block);
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
	if ((error = xfs_bmap_finish(&tp, &free_list, first_block, &committed)))
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
	error = xfs_trans_commit(tp, 0, NULL);
	tp = ntp;
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		goto error0;
	}
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

	ASSERT(ismrlocked(&ip->i_iolock, MR_UPDATE));
	tp = *tpp;
	mp = ip->i_mount;
	ASSERT(ip->i_d.di_forkoff != 0);
	xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	error = xfs_attr_inactive(ip);
	if (error) {
		*tpp = NULL;
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		return error; /* goto out */
	}

	tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);
	error = xfs_trans_reserve(tp, 0,
				  XFS_IFREE_LOG_RES(mp),
				  0, XFS_TRANS_PERM_LOG_RES,
				  XFS_INACTIVE_LOG_COUNT);
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		xfs_trans_cancel(tp, 0);
		*tpp = NULL;
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	xfs_idestroy_fork(ip, XFS_ATTR_FORK);

	ASSERT(ip->i_d.di_anextents == 0);

	*tpp = tp;
	return 0;
}

STATIC int
xfs_release(
	bhv_desc_t	*bdp)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;
	xfs_mount_t	*mp;
	int		error;

	vp = BHV_TO_VNODE(bdp);
	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	if (!VN_ISREG(vp) || (ip->i_d.di_mode == 0))
		return 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		return 0;

#ifdef HAVE_REFCACHE
	/* If we are in the NFS reference cache then don't do this now */
	if (ip->i_refcache)
		return 0;
#endif

	if (ip->i_d.di_nlink != 0) {
		if ((((ip->i_d.di_mode & S_IFMT) == S_IFREG) &&
		     ((ip->i_d.di_size > 0) || (VN_CACHED(vp) > 0 ||
		       ip->i_delayed_blks > 0)) &&
		     (ip->i_df.if_flags & XFS_IFEXTENTS))  &&
		    (!(ip->i_d.di_flags &
				(XFS_DIFLAG_PREALLOC | XFS_DIFLAG_APPEND)))) {
			if ((error = xfs_inactive_free_eofblocks(mp, ip)))
				return error;
			/* Update linux inode block count after free above */
			vn_to_inode(vp)->i_blocks = XFS_FSB_TO_BB(mp,
				ip->i_d.di_nblocks + ip->i_delayed_blks);
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
STATIC int
xfs_inactive(
	bhv_desc_t	*bdp,
	cred_t		*credp)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;
	xfs_bmap_free_t	free_list; 
	xfs_fsblock_t	first_block;
	int		committed;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;
	int		truncate;

	vp = BHV_TO_VNODE(bdp);
	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (ip->i_d.di_mode == 0 || VN_BAD(vp)) {
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
            ((ip->i_d.di_size != 0) || (ip->i_d.di_nextents > 0) ||
             (ip->i_delayed_blks > 0)) &&
	    ((ip->i_d.di_mode & S_IFMT) == S_IFREG));

	mp = ip->i_mount;

	if (ip->i_d.di_nlink == 0 &&
	    DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_EVENT_DESTROY)) {
		(void) XFS_SEND_DESTROY(mp, vp, DM_RIGHT_NULL);
	}

	error = 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		goto out;

	if (ip->i_d.di_nlink != 0) {
		if ((((ip->i_d.di_mode & S_IFMT) == S_IFREG) &&
                     ((ip->i_d.di_size > 0) || (VN_CACHED(vp) > 0 ||
                       ip->i_delayed_blks > 0)) &&
		      (ip->i_df.if_flags & XFS_IFEXTENTS) &&
		     (!(ip->i_d.di_flags &
				(XFS_DIFLAG_PREALLOC | XFS_DIFLAG_APPEND)) ||
		      (ip->i_delayed_blks != 0)))) {
			if ((error = xfs_inactive_free_eofblocks(mp, ip)))
				return VN_INACTIVE_CACHE;
			/* Update linux inode block count after free above */
			vn_to_inode(vp)->i_blocks = XFS_FSB_TO_BB(mp,
				ip->i_d.di_nblocks + ip->i_delayed_blks);
		}
		goto out;
	}

	ASSERT(ip->i_d.di_nlink == 0);

	if ((error = XFS_QM_DQATTACH(mp, ip, 0)))
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

		xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, 0);

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
	XFS_BMAP_INIT(&free_list, &first_block);
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
		XFS_TRANS_MOD_DQUOT_BYINO(mp, tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

		/*
		 * Just ignore errors at this point.  There is
		 * nothing we can do except to try to keep going.
		 */
		(void) xfs_bmap_finish(&tp,  &free_list, first_block,
				       &committed);
		(void) xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	}
	/*
	 * Release the dquots held by inode, if any.
	 */
	XFS_QM_DQDETACH(mp, ip);

	xfs_iunlock(ip, XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL);

 out:
	return VN_INACTIVE_CACHE;
}


/*
 * xfs_lookup
 */
STATIC int
xfs_lookup(
	bhv_desc_t		*dir_bdp,
	vname_t			*dentry,
	vnode_t			**vpp,
	int			flags,
	vnode_t			*rdir,
	cred_t			*credp)
{
	xfs_inode_t		*dp, *ip;
	xfs_ino_t		e_inum;
	int			error;
	uint			lock_mode;
	vnode_t			*dir_vp;

	dir_vp = BHV_TO_VNODE(dir_bdp);
	vn_trace_entry(dir_vp, __FUNCTION__, (inst_t *)__return_address);

	dp = XFS_BHVTOI(dir_bdp);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return XFS_ERROR(EIO);

	lock_mode = xfs_ilock_map_shared(dp);
	error = xfs_dir_lookup_int(dir_bdp, lock_mode, dentry, &e_inum, &ip);
	if (!error) {
		*vpp = XFS_ITOV(ip);
		ITRACE(ip);
	}
	xfs_iunlock_map_shared(dp, lock_mode);
	return error;
}


/*
 * xfs_create (create a new file).
 */
STATIC int
xfs_create(
	bhv_desc_t		*dir_bdp,
	vname_t			*dentry,
	vattr_t			*vap,
	vnode_t			**vpp,
	cred_t			*credp)
{
	char			*name = VNAME(dentry);
	vnode_t			*dir_vp;
	xfs_inode_t		*dp, *ip;
	vnode_t		        *vp=NULL;
	xfs_trans_t		*tp;
	xfs_mount_t	        *mp;
	xfs_dev_t		rdev;
	int                     error;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		dp_joined_to_trans;
	int			dm_event_sent = 0;
	uint			cancel_flags;
	int			committed;
	xfs_prid_t		prid;
	struct xfs_dquot	*udqp, *gdqp;
	uint			resblks;
	int			dm_di_mode;
	int			namelen;

	ASSERT(!*vpp);
	dir_vp = BHV_TO_VNODE(dir_bdp);
	vn_trace_entry(dir_vp, __FUNCTION__, (inst_t *)__return_address);

	dp = XFS_BHVTOI(dir_bdp);
	mp = dp->i_mount;

	dm_di_mode = vap->va_mode;
	namelen = VNAMELEN(dentry);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_EVENT_CREATE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_CREATE,
				dir_vp, DM_RIGHT_NULL, NULL,
				DM_RIGHT_NULL, name, NULL,
				dm_di_mode, 0, 0);

		if (error)
			return error;
		dm_event_sent = 1;
	}

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	/* Return through std_return after this point. */

	udqp = gdqp = NULL;
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = dp->i_d.di_projid;
	else if (vap->va_mask & XFS_AT_PROJID)
		prid = (xfs_prid_t)vap->va_projid;
	else
		prid = (xfs_prid_t)dfltprid;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = XFS_QM_DQVOPALLOC(mp, dp,
			current_fsuid(credp), current_fsgid(credp), prid,
			XFS_QMOPT_QUOTALL|XFS_QMOPT_INHERIT, &udqp, &gdqp);
	if (error)
		goto std_return;

	ip = NULL;
	dp_joined_to_trans = B_FALSE;

	tp = xfs_trans_alloc(mp, XFS_TRANS_CREATE);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	resblks = XFS_CREATE_SPACE_RES(mp, namelen);
	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_reserve(tp, resblks, XFS_CREATE_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_CREATE_LOG_COUNT);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_CREATE_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_CREATE_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL);

	XFS_BMAP_INIT(&free_list, &first_block);

	ASSERT(ip == NULL);

	/*
	 * Reserve disk quota and the inode.
	 */
	error = XFS_TRANS_RESERVE_QUOTA(mp, tp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto error_return;

	if (resblks == 0 &&
	    (error = XFS_DIR_CANENTER(mp, tp, dp, name, namelen)))
		goto error_return;
	rdev = (vap->va_mask & XFS_AT_RDEV) ? vap->va_rdev : 0;
	error = xfs_dir_ialloc(&tp, dp, vap->va_mode, 1,
			rdev, credp, prid, resblks > 0,
			&ip, &committed);
	if (error) {
		if (error == ENOSPC)
			goto error_return;
		goto abort_return;
	}
	ITRACE(ip);

	/*
	 * At this point, we've gotten a newly allocated inode.
	 * It is locked (and joined to the transaction).
	 */

	ASSERT(ismrlocked (&ip->i_lock, MR_UPDATE));

	/*
	 * Now we join the directory inode to the transaction.
	 * We do not do it earlier because xfs_dir_ialloc
	 * might commit the previous transaction (and release
	 * all the locks).
	 */

	VN_HOLD(dir_vp);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	dp_joined_to_trans = B_TRUE;

	error = XFS_DIR_CREATENAME(mp, tp, dp, name, namelen, ip->i_ino,
		&first_block, &free_list,
		resblks ? resblks - XFS_IALLOC_SPACE_RES(mp) : 0);
	if (error) {
		ASSERT(error != ENOSPC);
		goto abort_return;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	/*
	 * If this is a synchronous mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	dp->i_gen++;

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	XFS_QM_DQVOPCREATE(mp, tp, ip, udqp, gdqp);

	/*
	 * xfs_trans_commit normally decrements the vnode ref count
	 * when it unlocks the inode. Since we want to return the
	 * vnode to the caller, we bump the vnode ref count now.
	 */
	IHOLD(ip);
	vp = XFS_ITOV(ip);

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		goto abort_rele;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error) {
		IRELE(ip);
		tp = NULL;
		goto error_return;
	}

	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	/*
	 * Propagate the fact that the vnode changed after the
	 * xfs_inode locks have been released.
	 */
	VOP_VNODE_CHANGE(vp, VCHANGE_FLAGS_TRUNCATED, 3);

	*vpp = vp;

	/* Fallthrough to std_return with error = 0  */

std_return:
	if ( (*vpp || (error != 0 && dm_event_sent != 0)) &&
			DM_EVENT_ENABLED(dir_vp->v_vfsp, XFS_BHVTOI(dir_bdp),
							DM_EVENT_POSTCREATE)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTCREATE,
			dir_vp, DM_RIGHT_NULL,
			*vpp ? vp:NULL,
			DM_RIGHT_NULL, name, NULL,
			dm_di_mode, error, 0);
	}
	return error;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */

 error_return:
	if (tp != NULL)
		xfs_trans_cancel(tp, cancel_flags);

	if (!dp_joined_to_trans && (dp != NULL))
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	goto std_return;

 abort_rele:
	/*
	 * Wait until after the current transaction is aborted to
	 * release the inode.  This prevents recursive transactions
	 * and deadlocks from xfs_inactive.
	 */
	cancel_flags |= XFS_TRANS_ABORT;
	xfs_trans_cancel(tp, cancel_flags);
	IRELE(ip);

	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	goto std_return;
}

#ifdef DEBUG
/*
 * Some counters to see if (and how often) we are hitting some deadlock
 * prevention code paths.
 */

int xfs_rm_locks;
int xfs_rm_lock_delays;
int xfs_rm_attempts;
#endif

/*
 * The following routine will lock the inodes associated with the
 * directory and the named entry in the directory. The locks are
 * acquired in increasing inode number.
 *
 * If the entry is "..", then only the directory is locked. The
 * vnode ref count will still include that from the .. entry in
 * this case.
 *
 * There is a deadlock we need to worry about. If the locked directory is
 * in the AIL, it might be blocking up the log. The next inode we lock
 * could be already locked by another thread waiting for log space (e.g
 * a permanent log reservation with a long running transaction (see
 * xfs_itruncate_finish)). To solve this, we must check if the directory
 * is in the ail and use lock_nowait. If we can't lock, we need to
 * drop the inode lock on the directory and try again. xfs_iunlock will
 * potentially push the tail if we were holding up the log.
 */
STATIC int
xfs_lock_dir_and_entry(
	xfs_inode_t	*dp,
	vname_t		*dentry,
	xfs_inode_t	*ip)	/* inode of entry 'name' */
{
	int		attempts;
	xfs_ino_t	e_inum;
	xfs_inode_t	*ips[2];
	xfs_log_item_t	*lp;

#ifdef DEBUG
	xfs_rm_locks++;
#endif
	attempts = 0;

again:
	xfs_ilock(dp, XFS_ILOCK_EXCL);

	e_inum = ip->i_ino;

	ITRACE(ip);

	/*
	 * We want to lock in increasing inum. Since we've already
	 * acquired the lock on the directory, we may need to release
	 * if if the inum of the entry turns out to be less.
	 */
	if (e_inum > dp->i_ino) {
		/*
		 * We are already in the right order, so just
		 * lock on the inode of the entry.
		 * We need to use nowait if dp is in the AIL.
		 */

		lp = (xfs_log_item_t *)dp->i_itemp;
		if (lp && (lp->li_flags & XFS_LI_IN_AIL)) {
			if (!xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
				attempts++;
#ifdef DEBUG
				xfs_rm_attempts++;
#endif

				/*
				 * Unlock dp and try again.
				 * xfs_iunlock will try to push the tail
				 * if the inode is in the AIL.
				 */

				xfs_iunlock(dp, XFS_ILOCK_EXCL);

				if ((attempts % 5) == 0) {
					delay(1); /* Don't just spin the CPU */
#ifdef DEBUG
					xfs_rm_lock_delays++;
#endif
				}
				goto again;
			}
		} else {
			xfs_ilock(ip, XFS_ILOCK_EXCL);
		}
	} else if (e_inum < dp->i_ino) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);

		ips[0] = ip;
		ips[1] = dp;
		xfs_lock_inodes(ips, 2, 0, XFS_ILOCK_EXCL);
	}
	/* else	 e_inum == dp->i_ino */
	/*     This can happen if we're asked to lock /x/..
	 *     the entry is "..", which is also the parent directory.
	 */

	return 0;
}

#ifdef DEBUG
int xfs_locked_n;
int xfs_small_retries;
int xfs_middle_retries;
int xfs_lots_retries;
int xfs_lock_delays;
#endif

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
	int		first_locked,
	uint		lock_mode)
{
	int		attempts = 0, i, j, try_lock;
	xfs_log_item_t	*lp;

	ASSERT(ips && (inodes >= 2)); /* we need at least two */

	if (first_locked) {
		try_lock = 1;
		i = 1;
	} else {
		try_lock = 0;
		i = 0;
	}

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
			if (!xfs_ilock_nowait(ips[i], lock_mode)) {
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
			xfs_ilock(ips[i], lock_mode);
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

#ifdef	DEBUG
#define	REMOVE_DEBUG_TRACE(x)	{remove_which_error_return = (x);}
int remove_which_error_return = 0;
#else /* ! DEBUG */
#define	REMOVE_DEBUG_TRACE(x)
#endif	/* ! DEBUG */


/*
 * xfs_remove
 *
 */
STATIC int
xfs_remove(
	bhv_desc_t		*dir_bdp,
	vname_t			*dentry,
	cred_t			*credp)
{
	vnode_t			*dir_vp;
	char			*name = VNAME(dentry);
	xfs_inode_t             *dp, *ip;
	xfs_trans_t             *tp = NULL;
	xfs_mount_t		*mp;
	int                     error = 0;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	int			dm_di_mode = 0;
	int			link_zero;
	uint			resblks;
	int			namelen;

	dir_vp = BHV_TO_VNODE(dir_bdp);
	vn_trace_entry(dir_vp, __FUNCTION__, (inst_t *)__return_address);

	dp = XFS_BHVTOI(dir_bdp);
	mp = dp->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	namelen = VNAMELEN(dentry);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_EVENT_REMOVE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_REMOVE, dir_vp,
					DM_RIGHT_NULL, NULL, DM_RIGHT_NULL,
					name, NULL, 0, 0, 0);
		if (error)
			return error;
	}

	/* From this point on, return through std_return */
	ip = NULL;

	/*
	 * We need to get a reference to ip before we get our log
	 * reservation. The reason for this is that we cannot call
	 * xfs_iget for an inode for which we do not have a reference
	 * once we've acquired a log reservation. This is because the
	 * inode we are trying to get might be in xfs_inactive going
	 * for a log reservation. Since we'll have to wait for the
	 * inactive code to complete before returning from xfs_iget,
	 * we need to make sure that we don't have log space reserved
	 * when we call xfs_iget.  Instead we get an unlocked reference
	 * to the inode before getting our log reservation.
	 */
	error = xfs_get_dir_entry(dentry, &ip);
	if (error) {
		REMOVE_DEBUG_TRACE(__LINE__);
		goto std_return;
	}

	dm_di_mode = ip->i_d.di_mode;

	vn_trace_entry(XFS_ITOV(ip), __FUNCTION__, (inst_t *)__return_address);

	ITRACE(ip);

	error = XFS_QM_DQATTACH(mp, dp, 0);
	if (!error && dp != ip)
		error = XFS_QM_DQATTACH(mp, ip, 0);
	if (error) {
		REMOVE_DEBUG_TRACE(__LINE__);
		IRELE(ip);
		goto std_return;
	}

	tp = xfs_trans_alloc(mp, XFS_TRANS_REMOVE);
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
			XFS_TRANS_PERM_LOG_RES, XFS_REMOVE_LOG_COUNT);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_REMOVE_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_REMOVE_LOG_COUNT);
	}
	if (error) {
		ASSERT(error != ENOSPC);
		REMOVE_DEBUG_TRACE(__LINE__);
		xfs_trans_cancel(tp, 0);
		IRELE(ip);
		return error;
	}

	error = xfs_lock_dir_and_entry(dp, dentry, ip);
	if (error) {
		REMOVE_DEBUG_TRACE(__LINE__);
		xfs_trans_cancel(tp, cancel_flags);
		IRELE(ip);
		goto std_return;
	}

	/*
	 * At this point, we've gotten both the directory and the entry
	 * inodes locked.
	 */
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	if (dp != ip) {
		/*
		 * Increment vnode ref count only in this case since
		 * there's an extra vnode reference in the case where
		 * dp == ip.
		 */
		IHOLD(dp);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	}

	/*
	 * Entry must exist since we did a lookup in xfs_lock_dir_and_entry.
	 */
	XFS_BMAP_INIT(&free_list, &first_block);
	error = XFS_DIR_REMOVENAME(mp, tp, dp, name, namelen, ip->i_ino,
		&first_block, &free_list, 0);
	if (error) {
		ASSERT(error != ENOENT);
		REMOVE_DEBUG_TRACE(__LINE__);
		goto error1;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	dp->i_gen++;
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	error = xfs_droplink(tp, ip);
	if (error) {
		REMOVE_DEBUG_TRACE(__LINE__);
		goto error1;
	}

	/* Determine if this is the last link while
	 * we are in the transaction.
	 */
	link_zero = (ip)->i_d.di_nlink==0;

	/*
	 * Take an extra ref on the inode so that it doesn't
	 * go to xfs_inactive() from within the commit.
	 */
	IHOLD(ip);

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		REMOVE_DEBUG_TRACE(__LINE__);
		goto error_rele;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error) {
		IRELE(ip);
		goto std_return;
	}

	/*
	 * Before we drop our extra reference to the inode, purge it
	 * from the refcache if it is there.  By waiting until afterwards
	 * to do the IRELE, we ensure that we won't go inactive in the
	 * xfs_refcache_purge_ip routine (although that would be OK).
	 */
	xfs_refcache_purge_ip(ip);

	vn_trace_exit(XFS_ITOV(ip), __FUNCTION__, (inst_t *)__return_address);

	/*
	 * Let interposed file systems know about removed links.
	 */
	VOP_LINK_REMOVED(XFS_ITOV(ip), dir_vp, link_zero);

	IRELE(ip);

/*	Fall through to std_return with error = 0 */
 std_return:
	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp,
						DM_EVENT_POSTREMOVE)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTREMOVE,
				dir_vp, DM_RIGHT_NULL,
				NULL, DM_RIGHT_NULL,
				name, NULL, dm_di_mode, error, 0);
	}
	return error;

 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
	xfs_trans_cancel(tp, cancel_flags);
	goto std_return;

 error_rele:
	/*
	 * In this case make sure to not release the inode until after
	 * the current transaction is aborted.  Releasing it beforehand
	 * can cause us to go to xfs_inactive and start a recursive
	 * transaction which can easily deadlock with the current one.
	 */
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
	xfs_trans_cancel(tp, cancel_flags);

	/*
	 * Before we drop our extra reference to the inode, purge it
	 * from the refcache if it is there.  By waiting until afterwards
	 * to do the IRELE, we ensure that we won't go inactive in the
	 * xfs_refcache_purge_ip routine (although that would be OK).
	 */
	xfs_refcache_purge_ip(ip);

	IRELE(ip);

	goto std_return;
}


/*
 * xfs_link
 *
 */
STATIC int
xfs_link(
	bhv_desc_t		*target_dir_bdp,
	vnode_t			*src_vp,
	vname_t			*dentry,
	cred_t			*credp)
{
	xfs_inode_t		*tdp, *sip;
	xfs_trans_t		*tp;
	xfs_mount_t		*mp;
	xfs_inode_t		*ips[2];
	int			error;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	vnode_t			*target_dir_vp;
	int			resblks;
	char			*target_name = VNAME(dentry);
	int			target_namelen;

	target_dir_vp = BHV_TO_VNODE(target_dir_bdp);
	vn_trace_entry(target_dir_vp, __FUNCTION__, (inst_t *)__return_address);
	vn_trace_entry(src_vp, __FUNCTION__, (inst_t *)__return_address);

	target_namelen = VNAMELEN(dentry);
	if (VN_ISDIR(src_vp))
		return XFS_ERROR(EPERM);

	sip = xfs_vtoi(src_vp);
	tdp = XFS_BHVTOI(target_dir_bdp);
	mp = tdp->i_mount;
	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	if (DM_EVENT_ENABLED(src_vp->v_vfsp, tdp, DM_EVENT_LINK)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_LINK,
					target_dir_vp, DM_RIGHT_NULL,
					src_vp, DM_RIGHT_NULL,
					target_name, NULL, 0, 0, 0);
		if (error)
			return error;
	}

	/* Return through std_return after this point. */

	error = XFS_QM_DQATTACH(mp, sip, 0);
	if (!error && sip != tdp)
		error = XFS_QM_DQATTACH(mp, tdp, 0);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_LINK);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	resblks = XFS_LINK_SPACE_RES(mp, target_namelen);
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

	if (sip->i_ino < tdp->i_ino) {
		ips[0] = sip;
		ips[1] = tdp;
	} else {
		ips[0] = tdp;
		ips[1] = sip;
	}

	xfs_lock_inodes(ips, 2, 0, XFS_ILOCK_EXCL);

	/*
	 * Increment vnode ref counts since xfs_trans_commit &
	 * xfs_trans_cancel will both unlock the inodes and
	 * decrement the associated ref counts.
	 */
	VN_HOLD(src_vp);
	VN_HOLD(target_dir_vp);
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

	if (resblks == 0 &&
	    (error = XFS_DIR_CANENTER(mp, tp, tdp, target_name,
			target_namelen)))
		goto error_return;

	XFS_BMAP_INIT(&free_list, &first_block);

	error = XFS_DIR_CREATENAME(mp, tp, tdp, target_name, target_namelen,
				   sip->i_ino, &first_block, &free_list,
				   resblks);
	if (error)
		goto abort_return;
	xfs_ichgtime(tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	tdp->i_gen++;
	xfs_trans_log_inode(tp, tdp, XFS_ILOG_CORE);

	error = xfs_bumplink(tp, sip);
	if (error) {
		goto abort_return;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		goto abort_return;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error) {
		goto std_return;
	}

	/* Fall through to std_return with error = 0. */
std_return:
	if (DM_EVENT_ENABLED(src_vp->v_vfsp, sip,
						DM_EVENT_POSTLINK)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTLINK,
				target_dir_vp, DM_RIGHT_NULL,
				src_vp, DM_RIGHT_NULL,
				target_name, NULL, 0, error, 0);
	}
	return error;

 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */

 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	goto std_return;
}
/*
 * xfs_mkdir
 *
 */
STATIC int
xfs_mkdir(
	bhv_desc_t		*dir_bdp,
	vname_t			*dentry,
	vattr_t			*vap,
	vnode_t			**vpp,
	cred_t			*credp)
{
	char			*dir_name = VNAME(dentry);
	xfs_inode_t             *dp;
	xfs_inode_t		*cdp;	/* inode of created dir */
	vnode_t			*cvp;	/* vnode of created dir */
	xfs_trans_t		*tp;
	xfs_mount_t		*mp;
	int			cancel_flags;
	int			error;
	int			committed;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	vnode_t			*dir_vp;
	boolean_t		dp_joined_to_trans;
	boolean_t		created = B_FALSE;
	int			dm_event_sent = 0;
	xfs_prid_t		prid;
	struct xfs_dquot	*udqp, *gdqp;
	uint			resblks;
	int			dm_di_mode;
	int			dir_namelen;

	dir_vp = BHV_TO_VNODE(dir_bdp);
	dp = XFS_BHVTOI(dir_bdp);
	mp = dp->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	dir_namelen = VNAMELEN(dentry);

	tp = NULL;
	dp_joined_to_trans = B_FALSE;
	dm_di_mode = vap->va_mode;

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_EVENT_CREATE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_CREATE,
					dir_vp, DM_RIGHT_NULL, NULL,
					DM_RIGHT_NULL, dir_name, NULL,
					dm_di_mode, 0, 0);
		if (error)
			return error;
		dm_event_sent = 1;
	}

	/* Return through std_return after this point. */

	vn_trace_entry(dir_vp, __FUNCTION__, (inst_t *)__return_address);

	mp = dp->i_mount;
	udqp = gdqp = NULL;
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = dp->i_d.di_projid;
	else if (vap->va_mask & XFS_AT_PROJID)
		prid = (xfs_prid_t)vap->va_projid;
	else
		prid = (xfs_prid_t)dfltprid;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = XFS_QM_DQVOPALLOC(mp, dp,
			current_fsuid(credp), current_fsgid(credp), prid,
			XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT, &udqp, &gdqp);
	if (error)
		goto std_return;

	tp = xfs_trans_alloc(mp, XFS_TRANS_MKDIR);
	cancel_flags = XFS_TRANS_RELEASE_LOG_RES;
	resblks = XFS_MKDIR_SPACE_RES(mp, dir_namelen);
	error = xfs_trans_reserve(tp, resblks, XFS_MKDIR_LOG_RES(mp), 0,
				  XFS_TRANS_PERM_LOG_RES, XFS_MKDIR_LOG_COUNT);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_MKDIR_LOG_RES(mp), 0,
					  XFS_TRANS_PERM_LOG_RES,
					  XFS_MKDIR_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL);

	/*
	 * Check for directory link count overflow.
	 */
	if (dp->i_d.di_nlink >= XFS_MAXLINK) {
		error = XFS_ERROR(EMLINK);
		goto error_return;
	}

	/*
	 * Reserve disk quota and the inode.
	 */
	error = XFS_TRANS_RESERVE_QUOTA(mp, tp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto error_return;

	if (resblks == 0 &&
	    (error = XFS_DIR_CANENTER(mp, tp, dp, dir_name, dir_namelen)))
		goto error_return;
	/*
	 * create the directory inode.
	 */
	error = xfs_dir_ialloc(&tp, dp, vap->va_mode, 2,
			0, credp, prid, resblks > 0,
		&cdp, NULL);
	if (error) {
		if (error == ENOSPC)
			goto error_return;
		goto abort_return;
	}
	ITRACE(cdp);

	/*
	 * Now we add the directory inode to the transaction.
	 * We waited until now since xfs_dir_ialloc might start
	 * a new transaction.  Had we joined the transaction
	 * earlier, the locks might have gotten released.
	 */
	VN_HOLD(dir_vp);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	dp_joined_to_trans = B_TRUE;

	XFS_BMAP_INIT(&free_list, &first_block);

	error = XFS_DIR_CREATENAME(mp, tp, dp, dir_name, dir_namelen,
			cdp->i_ino, &first_block, &free_list,
			resblks ? resblks - XFS_IALLOC_SPACE_RES(mp) : 0);
	if (error) {
		ASSERT(error != ENOSPC);
		goto error1;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/*
	 * Bump the in memory version number of the parent directory
	 * so that other processes accessing it will recognize that
	 * the directory has changed.
	 */
	dp->i_gen++;

	error = XFS_DIR_INIT(mp, tp, cdp, dp);
	if (error) {
		goto error2;
	}

	cdp->i_gen = 1;
	error = xfs_bumplink(tp, dp);
	if (error) {
		goto error2;
	}

	cvp = XFS_ITOV(cdp);

	created = B_TRUE;

	*vpp = cvp;
	IHOLD(cdp);

	/*
	 * Attach the dquots to the new inode and modify the icount incore.
	 */
	XFS_QM_DQVOPCREATE(mp, tp, cdp, udqp, gdqp);

	/*
	 * If this is a synchronous mount, make sure that the
	 * mkdir transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		IRELE(cdp);
		goto error2;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);
	if (error) {
		IRELE(cdp);
	}

	/* Fall through to std_return with error = 0 or errno from
	 * xfs_trans_commit. */

std_return:
	if ( (created || (error != 0 && dm_event_sent != 0)) &&
			DM_EVENT_ENABLED(dir_vp->v_vfsp, XFS_BHVTOI(dir_bdp),
						DM_EVENT_POSTCREATE)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTCREATE,
					dir_vp, DM_RIGHT_NULL,
					created ? XFS_ITOV(cdp):NULL,
					DM_RIGHT_NULL,
					dir_name, NULL,
					dm_di_mode, error, 0);
	}
	return error;

 error2:
 error1:
	xfs_bmap_cancel(&free_list);
 abort_return:
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	if (!dp_joined_to_trans && (dp != NULL)) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	}

	goto std_return;
}


/*
 * xfs_rmdir
 *
 */
STATIC int
xfs_rmdir(
	bhv_desc_t		*dir_bdp,
	vname_t			*dentry,
	cred_t			*credp)
{
	char			*name = VNAME(dentry);
	xfs_inode_t             *dp;
	xfs_inode_t             *cdp;   /* child directory */
	xfs_trans_t             *tp;
	xfs_mount_t		*mp;
	int                     error;
	xfs_bmap_free_t         free_list;
	xfs_fsblock_t           first_block;
	int			cancel_flags;
	int			committed;
	vnode_t			*dir_vp;
	int			dm_di_mode = 0;
	int			last_cdp_link;
	int			namelen;
	uint			resblks;

	dir_vp = BHV_TO_VNODE(dir_bdp);
	dp = XFS_BHVTOI(dir_bdp);
	mp = dp->i_mount;

	vn_trace_entry(dir_vp, __FUNCTION__, (inst_t *)__return_address);

	if (XFS_FORCED_SHUTDOWN(XFS_BHVTOI(dir_bdp)->i_mount))
		return XFS_ERROR(EIO);
	namelen = VNAMELEN(dentry);

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_EVENT_REMOVE)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_REMOVE,
					dir_vp, DM_RIGHT_NULL,
					NULL, DM_RIGHT_NULL,
					name, NULL, 0, 0, 0);
		if (error)
			return XFS_ERROR(error);
	}

	/* Return through std_return after this point. */

	cdp = NULL;

	/*
	 * We need to get a reference to cdp before we get our log
	 * reservation.  The reason for this is that we cannot call
	 * xfs_iget for an inode for which we do not have a reference
	 * once we've acquired a log reservation.  This is because the
	 * inode we are trying to get might be in xfs_inactive going
	 * for a log reservation.  Since we'll have to wait for the
	 * inactive code to complete before returning from xfs_iget,
	 * we need to make sure that we don't have log space reserved
	 * when we call xfs_iget.  Instead we get an unlocked reference
	 * to the inode before getting our log reservation.
	 */
	error = xfs_get_dir_entry(dentry, &cdp);
	if (error) {
		REMOVE_DEBUG_TRACE(__LINE__);
		goto std_return;
	}
	mp = dp->i_mount;
	dm_di_mode = cdp->i_d.di_mode;

	/*
	 * Get the dquots for the inodes.
	 */
	error = XFS_QM_DQATTACH(mp, dp, 0);
	if (!error && dp != cdp)
		error = XFS_QM_DQATTACH(mp, cdp, 0);
	if (error) {
		IRELE(cdp);
		REMOVE_DEBUG_TRACE(__LINE__);
		goto std_return;
	}

	tp = xfs_trans_alloc(mp, XFS_TRANS_RMDIR);
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
			XFS_TRANS_PERM_LOG_RES, XFS_DEFAULT_LOG_COUNT);
	if (error == ENOSPC) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_REMOVE_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_DEFAULT_LOG_COUNT);
	}
	if (error) {
		ASSERT(error != ENOSPC);
		cancel_flags = 0;
		IRELE(cdp);
		goto error_return;
	}
	XFS_BMAP_INIT(&free_list, &first_block);

	/*
	 * Now lock the child directory inode and the parent directory
	 * inode in the proper order.  This will take care of validating
	 * that the directory entry for the child directory inode has
	 * not changed while we were obtaining a log reservation.
	 */
	error = xfs_lock_dir_and_entry(dp, dentry, cdp);
	if (error) {
		xfs_trans_cancel(tp, cancel_flags);
		IRELE(cdp);
		goto std_return;
	}

	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	if (dp != cdp) {
		/*
		 * Only increment the parent directory vnode count if
		 * we didn't bump it in looking up cdp.  The only time
		 * we don't bump it is when we're looking up ".".
		 */
		VN_HOLD(dir_vp);
	}

	ITRACE(cdp);
	xfs_trans_ijoin(tp, cdp, XFS_ILOCK_EXCL);

	ASSERT(cdp->i_d.di_nlink >= 2);
	if (cdp->i_d.di_nlink != 2) {
		error = XFS_ERROR(ENOTEMPTY);
		goto error_return;
	}
	if (!XFS_DIR_ISEMPTY(mp, cdp)) {
		error = XFS_ERROR(ENOTEMPTY);
		goto error_return;
	}

	error = XFS_DIR_REMOVENAME(mp, tp, dp, name, namelen, cdp->i_ino,
		&first_block, &free_list, resblks);
	if (error) {
		goto error1;
	}

	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/*
	 * Bump the in memory generation count on the parent
	 * directory so that other can know that it has changed.
	 */
	dp->i_gen++;

	/*
	 * Drop the link from cdp's "..".
	 */
	error = xfs_droplink(tp, dp);
	if (error) {
		goto error1;
	}

	/*
	 * Drop the link from dp to cdp.
	 */
	error = xfs_droplink(tp, cdp);
	if (error) {
		goto error1;
	}

	/*
	 * Drop the "." link from cdp to self.
	 */
	error = xfs_droplink(tp, cdp);
	if (error) {
		goto error1;
	}

	/* Determine these before committing transaction */
	last_cdp_link = (cdp)->i_d.di_nlink==0;

	/*
	 * Take an extra ref on the child vnode so that it
	 * does not go to xfs_inactive() from within the commit.
	 */
	IHOLD(cdp);

	/*
	 * If this is a synchronous mount, make sure that the
	 * rmdir transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC)) {
		xfs_trans_set_sync(tp);
	}

	error = xfs_bmap_finish (&tp, &free_list, first_block, &committed);
	if (error) {
		xfs_bmap_cancel(&free_list);
		xfs_trans_cancel(tp, (XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT));
		IRELE(cdp);
		goto std_return;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error) {
		IRELE(cdp);
		goto std_return;
	}


	/*
	 * Let interposed file systems know about removed links.
	 */
	VOP_LINK_REMOVED(XFS_ITOV(cdp), dir_vp, last_cdp_link);

	IRELE(cdp);

	/* Fall through to std_return with error = 0 or the errno
	 * from xfs_trans_commit. */
 std_return:
	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_EVENT_POSTREMOVE)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTREMOVE,
					dir_vp, DM_RIGHT_NULL,
					NULL, DM_RIGHT_NULL,
					name, NULL, dm_di_mode,
					error, 0);
	}
	return error;

 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
	/* FALLTHROUGH */

 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	goto std_return;
}


/*
 * xfs_readdir
 *
 * Read dp's entries starting at uiop->uio_offset and translate them into
 * bufsize bytes worth of struct dirents starting at bufbase.
 */
STATIC int
xfs_readdir(
	bhv_desc_t	*dir_bdp,
	uio_t		*uiop,
	cred_t		*credp,
	int		*eofp)
{
	xfs_inode_t	*dp;
	xfs_trans_t	*tp = NULL;
	int		error = 0;
	uint		lock_mode;

	vn_trace_entry(BHV_TO_VNODE(dir_bdp), __FUNCTION__,
					       (inst_t *)__return_address);
	dp = XFS_BHVTOI(dir_bdp);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount)) {
		return XFS_ERROR(EIO);
	}

	lock_mode = xfs_ilock_map_shared(dp);
	error = XFS_DIR_GETDENTS(dp->i_mount, tp, dp, uiop, eofp);
	xfs_iunlock_map_shared(dp, lock_mode);
	return error;
}


/*
 * xfs_symlink
 *
 */
STATIC int
xfs_symlink(
	bhv_desc_t		*dir_bdp,
	vname_t			*dentry,
	vattr_t			*vap,
	char			*target_path,
	vnode_t			**vpp,
	cred_t			*credp)
{
	xfs_trans_t		*tp;
	xfs_mount_t		*mp;
	xfs_inode_t		*dp;
	xfs_inode_t		*ip;
	int			error;
	int			pathlen;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	boolean_t		dp_joined_to_trans;
	vnode_t			*dir_vp;
	uint			cancel_flags;
	int			committed;
	xfs_fileoff_t		first_fsb;
	xfs_filblks_t		fs_blocks;
	int			nmaps;
	xfs_bmbt_irec_t		mval[SYMLINK_MAPS];
	xfs_daddr_t		d;
	char			*cur_chunk;
	int			byte_cnt;
	int			n;
	xfs_buf_t		*bp;
	xfs_prid_t		prid;
	struct xfs_dquot	*udqp, *gdqp;
	uint			resblks;
	char			*link_name = VNAME(dentry);
	int			link_namelen;

	*vpp = NULL;
	dir_vp = BHV_TO_VNODE(dir_bdp);
	dp = XFS_BHVTOI(dir_bdp);
	dp_joined_to_trans = B_FALSE;
	error = 0;
	ip = NULL;
	tp = NULL;

	vn_trace_entry(dir_vp, __FUNCTION__, (inst_t *)__return_address);

	mp = dp->i_mount;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	link_namelen = VNAMELEN(dentry);

	/*
	 * Check component lengths of the target path name.
	 */
	pathlen = strlen(target_path);
	if (pathlen >= MAXPATHLEN)      /* total string too long */
		return XFS_ERROR(ENAMETOOLONG);
	if (pathlen >= MAXNAMELEN) {    /* is any component too long? */
		int len, total;
		char *path;

		for(total = 0, path = target_path; total < pathlen;) {
			/*
			 * Skip any slashes.
			 */
			while(*path == '/') {
				total++;
				path++;
			}

			/*
			 * Count up to the next slash or end of path.
			 * Error out if the component is bigger than MAXNAMELEN.
			 */
			for(len = 0; *path != '/' && total < pathlen;total++, path++) {
				if (++len >= MAXNAMELEN) {
					error = ENAMETOOLONG;
					return error;
				}
			}
		}
	}

	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, dp, DM_EVENT_SYMLINK)) {
		error = XFS_SEND_NAMESP(mp, DM_EVENT_SYMLINK, dir_vp,
					DM_RIGHT_NULL, NULL, DM_RIGHT_NULL,
					link_name, target_path, 0, 0, 0);
		if (error)
			return error;
	}

	/* Return through std_return after this point. */

	udqp = gdqp = NULL;
	if (dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
		prid = dp->i_d.di_projid;
	else if (vap->va_mask & XFS_AT_PROJID)
		prid = (xfs_prid_t)vap->va_projid;
	else
		prid = (xfs_prid_t)dfltprid;

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = XFS_QM_DQVOPALLOC(mp, dp,
			current_fsuid(credp), current_fsgid(credp), prid,
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
	resblks = XFS_SYMLINK_SPACE_RES(mp, link_namelen, fs_blocks);
	error = xfs_trans_reserve(tp, resblks, XFS_SYMLINK_LOG_RES(mp), 0,
			XFS_TRANS_PERM_LOG_RES, XFS_SYMLINK_LOG_COUNT);
	if (error == ENOSPC && fs_blocks == 0) {
		resblks = 0;
		error = xfs_trans_reserve(tp, 0, XFS_SYMLINK_LOG_RES(mp), 0,
				XFS_TRANS_PERM_LOG_RES, XFS_SYMLINK_LOG_COUNT);
	}
	if (error) {
		cancel_flags = 0;
		dp = NULL;
		goto error_return;
	}

	xfs_ilock(dp, XFS_ILOCK_EXCL);

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
	error = XFS_TRANS_RESERVE_QUOTA(mp, tp, udqp, gdqp, resblks, 1, 0);
	if (error)
		goto error_return;

	/*
	 * Check for ability to enter directory entry, if no space reserved.
	 */
	if (resblks == 0 &&
	    (error = XFS_DIR_CANENTER(mp, tp, dp, link_name, link_namelen)))
		goto error_return;
	/*
	 * Initialize the bmap freelist prior to calling either
	 * bmapi or the directory create code.
	 */
	XFS_BMAP_INIT(&free_list, &first_block);

	/*
	 * Allocate an inode for the symlink.
	 */
	error = xfs_dir_ialloc(&tp, dp, S_IFLNK | (vap->va_mode&~S_IFMT),
			       1, 0, credp, prid, resblks > 0, &ip, NULL);
	if (error) {
		if (error == ENOSPC)
			goto error_return;
		goto error1;
	}
	ITRACE(ip);

	VN_HOLD(dir_vp);
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	dp_joined_to_trans = B_TRUE;

	/*
	 * Also attach the dquot(s) to it, if applicable.
	 */
	XFS_QM_DQVOPCREATE(mp, tp, ip, udqp, gdqp);

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
	error = XFS_DIR_CREATENAME(mp, tp, dp, link_name, link_namelen,
			ip->i_ino, &first_block, &free_list, resblks);
	if (error) {
		goto error1;
	}
	xfs_ichgtime(dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	/*
	 * Bump the in memory version number of the parent directory
	 * so that other processes accessing it will recognize that
	 * the directory has changed.
	 */
	dp->i_gen++;

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

	error = xfs_bmap_finish(&tp, &free_list, first_block, &committed);
	if (error) {
		goto error2;
	}
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	/* Fall through to std_return with error = 0 or errno from
	 * xfs_trans_commit	*/
std_return:
	if (DM_EVENT_ENABLED(dir_vp->v_vfsp, XFS_BHVTOI(dir_bdp),
			     DM_EVENT_POSTSYMLINK)) {
		(void) XFS_SEND_NAMESP(mp, DM_EVENT_POSTSYMLINK,
					dir_vp, DM_RIGHT_NULL,
					error ? NULL : XFS_ITOV(ip),
					DM_RIGHT_NULL, link_name, target_path,
					0, error, 0);
	}

	if (!error) {
		vnode_t *vp;

		ASSERT(ip);
		vp = XFS_ITOV(ip);
		*vpp = vp;
	}
	return error;

 error2:
	IRELE(ip);
 error1:
	xfs_bmap_cancel(&free_list);
	cancel_flags |= XFS_TRANS_ABORT;
 error_return:
	xfs_trans_cancel(tp, cancel_flags);
	XFS_QM_DQRELE(mp, udqp);
	XFS_QM_DQRELE(mp, gdqp);

	if (!dp_joined_to_trans && (dp != NULL)) {
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	}

	goto std_return;
}


/*
 * xfs_fid2
 *
 * A fid routine that takes a pointer to a previously allocated
 * fid structure (like xfs_fast_fid) but uses a 64 bit inode number.
 */
STATIC int
xfs_fid2(
	bhv_desc_t	*bdp,
	fid_t		*fidp)
{
	xfs_inode_t	*ip;
	xfs_fid2_t	*xfid;

	vn_trace_entry(BHV_TO_VNODE(bdp), __FUNCTION__,
				       (inst_t *)__return_address);
	ASSERT(sizeof(fid_t) >= sizeof(xfs_fid2_t));

	xfid = (xfs_fid2_t *)fidp;
	ip = XFS_BHVTOI(bdp);
	xfid->fid_len = sizeof(xfs_fid2_t) - sizeof(xfid->fid_len);
	xfid->fid_pad = 0;
	/*
	 * use memcpy because the inode is a long long and there's no
	 * assurance that xfid->fid_ino is properly aligned.
	 */
	memcpy(&xfid->fid_ino, &ip->i_ino, sizeof(xfid->fid_ino));
	xfid->fid_gen = ip->i_d.di_gen;

	return 0;
}


/*
 * xfs_rwlock
 */
int
xfs_rwlock(
	bhv_desc_t	*bdp,
	vrwlock_t	locktype)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;

	vp = BHV_TO_VNODE(bdp);
	if (VN_ISDIR(vp))
		return 1;
	ip = XFS_BHVTOI(bdp);
	if (locktype == VRWLOCK_WRITE) {
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
	} else if (locktype == VRWLOCK_TRY_READ) {
		return xfs_ilock_nowait(ip, XFS_IOLOCK_SHARED);
	} else if (locktype == VRWLOCK_TRY_WRITE) {
		return xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL);
	} else {
		ASSERT((locktype == VRWLOCK_READ) ||
		       (locktype == VRWLOCK_WRITE_DIRECT));
		xfs_ilock(ip, XFS_IOLOCK_SHARED);
	}

	return 1;
}


/*
 * xfs_rwunlock
 */
void
xfs_rwunlock(
	bhv_desc_t	*bdp,
	vrwlock_t	locktype)
{
	xfs_inode_t     *ip;
	vnode_t		*vp;

	vp = BHV_TO_VNODE(bdp);
	if (VN_ISDIR(vp))
		return;
	ip = XFS_BHVTOI(bdp);
	if (locktype == VRWLOCK_WRITE) {
		/*
		 * In the write case, we may have added a new entry to
		 * the reference cache.  This might store a pointer to
		 * an inode to be released in this inode.  If it is there,
		 * clear the pointer and release the inode after unlocking
		 * this one.
		 */
		xfs_refcache_iunlock(ip, XFS_IOLOCK_EXCL);
	} else {
		ASSERT((locktype == VRWLOCK_READ) ||
		       (locktype == VRWLOCK_WRITE_DIRECT));
		xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	}
	return;
}

STATIC int
xfs_inode_flush(
	bhv_desc_t	*bdp,
	int		flags)
{
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	xfs_inode_log_item_t *iip;
	int		error = 0;

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;
	iip = ip->i_itemp;

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	/*
	 * Bypass inodes which have already been cleaned by
	 * the inode flush clustering code inside xfs_iflush
	 */
	if ((ip->i_update_core == 0) &&
	    ((iip == NULL) || !(iip->ili_format.ilf_fields & XFS_ILOG_ALL)))
		return 0;

	if (flags & FLUSH_LOG) {
		if (iip && iip->ili_last_lsn) {
			xlog_t		*log = mp->m_log;
			xfs_lsn_t	sync_lsn;
			int		s, log_flags = XFS_LOG_FORCE;

			s = GRANT_LOCK(log);
			sync_lsn = log->l_last_sync_lsn;
			GRANT_UNLOCK(log, s);

			if ((XFS_LSN_CMP(iip->ili_last_lsn, sync_lsn) <= 0))
				return 0;

			if (flags & FLUSH_SYNC)
				log_flags |= XFS_LOG_SYNC;
			return xfs_log_force(mp, iip->ili_last_lsn, log_flags);
		}
	}

	/*
	 * We make this non-blocking if the inode is contended,
	 * return EAGAIN to indicate to the caller that they
	 * did not succeed. This prevents the flush path from
	 * blocking on inodes inside another operation right
	 * now, they get caught later by xfs_sync.
	 */
	if (flags & FLUSH_INODE) {
		int	flush_flags;

		if (xfs_ipincount(ip))
			return EAGAIN;

		if (flags & FLUSH_SYNC) {
			xfs_ilock(ip, XFS_ILOCK_SHARED);
			xfs_iflock(ip);
		} else if (xfs_ilock_nowait(ip, XFS_ILOCK_SHARED)) {
			if (xfs_ipincount(ip) || !xfs_iflock_nowait(ip)) {
				xfs_iunlock(ip, XFS_ILOCK_SHARED);
				return EAGAIN;
			}
		} else {
			return EAGAIN;
		}

		if (flags & FLUSH_SYNC)
			flush_flags = XFS_IFLUSH_SYNC;
		else
			flush_flags = XFS_IFLUSH_ASYNC;

		error = xfs_iflush(ip, flush_flags);
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
	}

	return error;
}

int
xfs_set_dmattrs (
	bhv_desc_t	*bdp,
	u_int		evmask,
	u_int16_t	state,
	cred_t		*credp)
{
	xfs_inode_t     *ip;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;

	if (!capable(CAP_SYS_ADMIN))
		return XFS_ERROR(EPERM);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

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

	ip->i_iocore.io_dmevmask = ip->i_d.di_dmevmask = evmask;
	ip->i_iocore.io_dmstate  = ip->i_d.di_dmstate  = state;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	IHOLD(ip);
	error = xfs_trans_commit(tp, 0, NULL);

	return error;
}

STATIC int
xfs_reclaim(
	bhv_desc_t	*bdp)
{
	xfs_inode_t	*ip;
	vnode_t		*vp;

	vp = BHV_TO_VNODE(bdp);
	ip = XFS_BHVTOI(bdp);

	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	ASSERT(!VN_MAPPED(vp));

	/* bad inode, get out here ASAP */
	if (VN_BAD(vp)) {
		xfs_ireclaim(ip);
		return 0;
	}

	vn_iowait(vp);

	ASSERT(XFS_FORCED_SHUTDOWN(ip->i_mount) || ip->i_delayed_blks == 0);

	/*
	 * Make sure the atime in the XFS inode is correct before freeing the
	 * Linux inode.
	 */
	xfs_synchronize_atime(ip);

	/* If we have nothing to flush with this inode then complete the
	 * teardown now, otherwise break the link between the xfs inode
	 * and the linux inode and clean up the xfs inode later. This
	 * avoids flushing the inode to disk during the delete operation
	 * itself.
	 */
	if (!ip->i_update_core && (ip->i_itemp == NULL)) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_iflock(ip);
		return xfs_finish_reclaim(ip, 1, XFS_IFLUSH_DELWRI_ELSE_SYNC);
	} else {
		xfs_mount_t	*mp = ip->i_mount;

		/* Protect sync from us */
		XFS_MOUNT_ILOCK(mp);
		vn_bhv_remove(VN_BHV_HEAD(vp), XFS_ITOBHV(ip));
		list_add_tail(&ip->i_reclaim, &mp->m_del_inodes);
		ip->i_flags |= XFS_IRECLAIMABLE;
		XFS_MOUNT_IUNLOCK(mp);
	}
	return 0;
}

int
xfs_finish_reclaim(
	xfs_inode_t	*ip,
	int		locked,
	int		sync_mode)
{
	xfs_ihash_t	*ih = ip->i_hash;
	vnode_t		*vp = XFS_ITOV_NULL(ip);
	int		error;

	if (vp && VN_BAD(vp))
		goto reclaim;

	/* The hash lock here protects a thread in xfs_iget_core from
	 * racing with us on linking the inode back with a vnode.
	 * Once we have the XFS_IRECLAIM flag set it will not touch
	 * us.
	 */
	write_lock(&ih->ih_lock);
	if ((ip->i_flags & XFS_IRECLAIM) ||
	    (!(ip->i_flags & XFS_IRECLAIMABLE) && vp == NULL)) {
		write_unlock(&ih->ih_lock);
		if (locked) {
			xfs_ifunlock(ip);
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}
		return 1;
	}
	ip->i_flags |= XFS_IRECLAIM;
	write_unlock(&ih->ih_lock);

	/*
	 * If the inode is still dirty, then flush it out.  If the inode
	 * is not in the AIL, then it will be OK to flush it delwri as
	 * long as xfs_iflush() does not keep any references to the inode.
	 * We leave that decision up to xfs_iflush() since it has the
	 * knowledge of whether it's OK to simply do a delwri flush of
	 * the inode or whether we need to wait until the inode is
	 * pulled from the AIL.
	 * We get the flush lock regardless, though, just to make sure
	 * we don't free it while it is being flushed.
	 */
	if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		if (!locked) {
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_iflock(ip);
		}

		if (ip->i_update_core ||
		    ((ip->i_itemp != NULL) &&
		     (ip->i_itemp->ili_format.ilf_fields != 0))) {
			error = xfs_iflush(ip, sync_mode);
			/*
			 * If we hit an error, typically because of filesystem
			 * shutdown, we don't need to let vn_reclaim to know
			 * because we're gonna reclaim the inode anyway.
			 */
			if (error) {
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				goto reclaim;
			}
			xfs_iflock(ip); /* synchronize with xfs_iflush_done */
		}

		ASSERT(ip->i_update_core == 0);
		ASSERT(ip->i_itemp == NULL ||
		       ip->i_itemp->ili_format.ilf_fields == 0);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	} else if (locked) {
		/*
		 * We are not interested in doing an iflush if we're
		 * in the process of shutting down the filesystem forcibly.
		 * So, just reclaim the inode.
		 */
		xfs_ifunlock(ip);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

 reclaim:
	xfs_ireclaim(ip);
	return 0;
}

int
xfs_finish_reclaim_all(xfs_mount_t *mp, int noblock)
{
	int		purged;
	xfs_inode_t	*ip, *n;
	int		done = 0;

	while (!done) {
		purged = 0;
		XFS_MOUNT_ILOCK(mp);
		list_for_each_entry_safe(ip, n, &mp->m_del_inodes, i_reclaim) {
			if (noblock) {
				if (xfs_ilock_nowait(ip, XFS_ILOCK_EXCL) == 0)
					continue;
				if (xfs_ipincount(ip) ||
				    !xfs_iflock_nowait(ip)) {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					continue;
				}
			}
			XFS_MOUNT_IUNLOCK(mp);
			if (xfs_finish_reclaim(ip, noblock,
					XFS_IFLUSH_DELWRI_ELSE_ASYNC))
				delay(1);
			purged = 1;
			break;
		}

		done = !purged;
	}

	XFS_MOUNT_IUNLOCK(mp);
	return 0;
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

	vn_trace_entry(XFS_ITOV(ip), __FUNCTION__, (inst_t *)__return_address);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	rt = XFS_IS_REALTIME_INODE(ip);
	if (unlikely(rt)) {
		if (!(extsz = ip->i_d.di_extsize))
			extsz = mp->m_sb.sb_rextsize;
	} else {
		extsz = ip->i_d.di_extsize;
	}

	if ((error = XFS_QM_DQATTACH(mp, ip, 0)))
		return error;

	if (len <= 0)
		return XFS_ERROR(EINVAL);

	count = len;
	error = 0;
	imapp = &imaps[0];
	nimaps = 1;
	bmapi_flag = XFS_BMAPI_WRITE | (alloc_type ? XFS_BMAPI_PREALLOC : 0);
	startoffset_fsb	= XFS_B_TO_FSBT(mp, offset);
	allocatesize_fsb = XFS_B_TO_FSB(mp, count);

	/*	Generate a DMAPI event if needed.	*/
	if (alloc_type != 0 && offset < ip->i_d.di_size &&
			(attr_flags&ATTR_DMI) == 0  &&
			DM_EVENT_ENABLED(XFS_MTOVFS(mp), ip, DM_EVENT_WRITE)) {
		xfs_off_t           end_dmi_offset;

		end_dmi_offset = offset+len;
		if (end_dmi_offset > ip->i_d.di_size)
			end_dmi_offset = ip->i_d.di_size;
		error = XFS_SEND_DATA(mp, DM_EVENT_WRITE, XFS_ITOV(ip),
			offset, end_dmi_offset - offset,
			0, NULL);
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
		error = XFS_TRANS_RESERVE_QUOTA_NBLKS(mp, tp, ip,
						      qblocks, 0, quota_flag);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * Issue the xfs_bmapi() call to allocate the blocks
		 */
		XFS_BMAP_INIT(&free_list, &firstfsb);
		error = XFS_BMAPI(mp, tp, &ip->i_iocore, startoffset_fsb,
				  allocatesize_fsb, bmapi_flag,
				  &firstfsb, 0, imapp, &nimaps,
				  &free_list, NULL);
		if (error) {
			goto error0;
		}

		/*
		 * Complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, firstfsb, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
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
	if (error == ENOSPC && (attr_flags&ATTR_DMI) == 0 &&
	    DM_EVENT_ENABLED(XFS_MTOVFS(mp), ip, DM_EVENT_NOSPACE)) {

		error = XFS_SEND_NAMESP(mp, DM_EVENT_NOSPACE,
				XFS_ITOV(ip), DM_RIGHT_NULL,
				XFS_ITOV(ip), DM_RIGHT_NULL,
				NULL, NULL, 0, 0, 0); /* Delay flag intentionally unused */
		if (error == 0)
			goto retry;	/* Maybe DMAPI app. has made space */
		/* else fall through with error from XFS_SEND_DATA */
	}

	return error;

error0:	/* Cancel bmap, unlock inode, unreserve quota blocks, cancel trans */
	xfs_bmap_cancel(&free_list);
	XFS_TRANS_UNRESERVE_QUOTA_NBLKS(mp, tp, ip, qblocks, 0, quota_flag);

error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	goto dmapi_enospc_check;
}

/*
 * Zero file bytes between startoff and endoff inclusive.
 * The iolock is held exclusive and no blocks are buffered.
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

	bp = xfs_buf_get_noaddr(mp->m_sb.sb_blocksize,
				ip->i_d.di_flags & XFS_DIFLAG_REALTIME ?
				mp->m_rtdev_targp : mp->m_ddev_targp);

	for (offset = startoff; offset <= endoff; offset = lastoffset + 1) {
		offset_fsb = XFS_B_TO_FSBT(mp, offset);
		nimap = 1;
		error = XFS_BMAPI(mp, NULL, &ip->i_iocore, offset_fsb, 1, 0,
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
		XFS_BUF_SET_ADDR(bp, XFS_FSB_TO_DB(ip, imap.br_startblock));
		xfsbdstrat(mp, bp);
		if ((error = xfs_iowait(bp))) {
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
		if ((error = xfs_iowait(bp))) {
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
	vnode_t			*vp;
	int			committed;
	int			done;
	xfs_off_t		end_dmi_offset;
	xfs_fileoff_t		endoffset_fsb;
	int			error;
	xfs_fsblock_t		firstfsb;
	xfs_bmap_free_t		free_list;
	xfs_off_t		ilen;
	xfs_bmbt_irec_t		imap;
	xfs_off_t		ioffset;
	xfs_extlen_t		mod=0;
	xfs_mount_t		*mp;
	int			nimap;
	uint			resblks;
	int			rounding;
	int			rt;
	xfs_fileoff_t		startoffset_fsb;
	xfs_trans_t		*tp;
	int			need_iolock = 1;

	vp = XFS_ITOV(ip);
	mp = ip->i_mount;

	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	if ((error = XFS_QM_DQATTACH(mp, ip, 0)))
		return error;

	error = 0;
	if (len <= 0)	/* if nothing being freed */
		return error;
	rt = (ip->i_d.di_flags & XFS_DIFLAG_REALTIME);
	startoffset_fsb	= XFS_B_TO_FSB(mp, offset);
	end_dmi_offset = offset + len;
	endoffset_fsb = XFS_B_TO_FSBT(mp, end_dmi_offset);

	if (offset < ip->i_d.di_size &&
	    (attr_flags & ATTR_DMI) == 0 &&
	    DM_EVENT_ENABLED(XFS_MTOVFS(mp), ip, DM_EVENT_WRITE)) {
		if (end_dmi_offset > ip->i_d.di_size)
			end_dmi_offset = ip->i_d.di_size;
		error = XFS_SEND_DATA(mp, DM_EVENT_WRITE, vp,
				offset, end_dmi_offset - offset,
				AT_DELAY_FLAG(attr_flags), NULL);
		if (error)
			return error;
	}

	if (attr_flags & ATTR_NOLOCK)
		need_iolock = 0;
	if (need_iolock) {
		xfs_ilock(ip, XFS_IOLOCK_EXCL);
		vn_iowait(vp);	/* wait for the completion of any pending DIOs */
	}

	rounding = MAX((__uint8_t)(1 << mp->m_sb.sb_blocklog),
			(__uint8_t)NBPP);
	ilen = len + (offset & (rounding - 1));
	ioffset = offset & ~(rounding - 1);
	if (ilen & (rounding - 1))
		ilen = (ilen + rounding) & ~(rounding - 1);

	if (VN_CACHED(vp) != 0) {
		xfs_inval_cached_trace(&ip->i_iocore, ioffset, -1,
				ctooff(offtoct(ioffset)), -1);
		VOP_FLUSHINVAL_PAGES(vp, ctooff(offtoct(ioffset)),
				-1, FI_REMAPF_LOCKED);
	}

	/*
	 * Need to zero the stuff we're not freeing, on disk.
	 * If its a realtime file & can't use unwritten extents then we
	 * actually need to zero the extent edges.  Otherwise xfs_bunmapi
	 * will take care of it for us.
	 */
	if (rt && !XFS_SB_VERSION_HASEXTFLGBIT(&mp->m_sb)) {
		nimap = 1;
		error = XFS_BMAPI(mp, NULL, &ip->i_iocore, startoffset_fsb,
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
		error = XFS_BMAPI(mp, NULL, &ip->i_iocore, endoffset_fsb - 1,
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
		 * allocate and setup the transaction
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
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
		error = XFS_TRANS_RESERVE_QUOTA(mp, tp,
				ip->i_udquot, ip->i_gdquot, resblks, 0,
				XFS_QMOPT_RES_REGBLKS);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
		xfs_trans_ihold(tp, ip);

		/*
		 * issue the bunmapi() call to free the blocks
		 */
		XFS_BMAP_INIT(&free_list, &firstfsb);
		error = XFS_BUNMAPI(mp, tp, &ip->i_iocore, startoffset_fsb,
				  endoffset_fsb - startoffset_fsb,
				  0, 2, &firstfsb, &free_list, NULL, &done);
		if (error) {
			goto error0;
		}

		/*
		 * complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, firstfsb, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
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
	bhv_desc_t	*bdp,
	int		cmd,
	xfs_flock64_t	*bf,
	xfs_off_t	offset,
	cred_t		*credp,
	int		attr_flags)
{
	int		clrprealloc;
	int		error;
	xfs_fsize_t	fsize;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	int		setprealloc;
	xfs_off_t	startoffset;
	xfs_off_t	llen;
	xfs_trans_t	*tp;
	vattr_t		va;
	vnode_t		*vp;

	vp = BHV_TO_VNODE(bdp);
	vn_trace_entry(vp, __FUNCTION__, (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	/*
	 * must be a regular file and have write permission
	 */
	if (!VN_ISREG(vp))
		return XFS_ERROR(EINVAL);

	xfs_ilock(ip, XFS_ILOCK_SHARED);

	if ((error = xfs_iaccess(ip, S_IWUSR, credp))) {
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		return error;
	}

	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	switch (bf->l_whence) {
	case 0: /*SEEK_SET*/
		break;
	case 1: /*SEEK_CUR*/
		bf->l_start += offset;
		break;
	case 2: /*SEEK_END*/
		bf->l_start += ip->i_d.di_size;
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
	fsize = ip->i_d.di_size;

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

		va.va_mask = XFS_AT_SIZE;
		va.va_size = startoffset;

		error = xfs_setattr(bdp, &va, attr_flags, credp);

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

	if ((attr_flags & ATTR_DMI) == 0) {
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

	error = xfs_trans_commit(tp, 0, NULL);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	return error;
}

vnodeops_t xfs_vnodeops = {
	BHV_IDENTITY_INIT(VN_BHV_XFS,VNODE_POSITION_XFS),
	.vop_open		= xfs_open,
	.vop_close		= xfs_close,
	.vop_read		= xfs_read,
#ifdef HAVE_SENDFILE
	.vop_sendfile		= xfs_sendfile,
#endif
#ifdef HAVE_SPLICE
	.vop_splice_read	= xfs_splice_read,
	.vop_splice_write	= xfs_splice_write,
#endif
	.vop_write		= xfs_write,
	.vop_ioctl		= xfs_ioctl,
	.vop_getattr		= xfs_getattr,
	.vop_setattr		= xfs_setattr,
	.vop_access		= xfs_access,
	.vop_lookup		= xfs_lookup,
	.vop_create		= xfs_create,
	.vop_remove		= xfs_remove,
	.vop_link		= xfs_link,
	.vop_rename		= xfs_rename,
	.vop_mkdir		= xfs_mkdir,
	.vop_rmdir		= xfs_rmdir,
	.vop_readdir		= xfs_readdir,
	.vop_symlink		= xfs_symlink,
	.vop_readlink		= xfs_readlink,
	.vop_fsync		= xfs_fsync,
	.vop_inactive		= xfs_inactive,
	.vop_fid2		= xfs_fid2,
	.vop_rwlock		= xfs_rwlock,
	.vop_rwunlock		= xfs_rwunlock,
	.vop_bmap		= xfs_bmap,
	.vop_reclaim		= xfs_reclaim,
	.vop_attr_get		= xfs_attr_get,
	.vop_attr_set		= xfs_attr_set,
	.vop_attr_remove	= xfs_attr_remove,
	.vop_attr_list		= xfs_attr_list,
	.vop_link_removed	= (vop_link_removed_t)fs_noval,
	.vop_vnode_change	= (vop_vnode_change_t)fs_noval,
	.vop_tosspages		= fs_tosspages,
	.vop_flushinval_pages	= fs_flushinval_pages,
	.vop_flush_pages	= fs_flush_pages,
	.vop_release		= xfs_release,
	.vop_iflush		= xfs_inode_flush,
};
