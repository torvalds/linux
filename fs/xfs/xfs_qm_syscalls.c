/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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

#include <linux/capability.h>

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_itable.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_qm.h"
#include "xfs_trace.h"

STATIC int	xfs_qm_log_quotaoff(xfs_mount_t *, xfs_qoff_logitem_t **, uint);
STATIC int	xfs_qm_log_quotaoff_end(xfs_mount_t *, xfs_qoff_logitem_t *,
					uint);
STATIC uint	xfs_qm_export_flags(uint);
STATIC uint	xfs_qm_export_qtype_flags(uint);
STATIC void	xfs_qm_export_dquot(xfs_mount_t *, xfs_disk_dquot_t *,
					fs_disk_quota_t *);


/*
 * Turn off quota accounting and/or enforcement for all udquots and/or
 * gdquots. Called only at unmount time.
 *
 * This assumes that there are no dquots of this file system cached
 * incore, and modifies the ondisk dquot directly. Therefore, for example,
 * it is an error to call this twice, without purging the cache.
 */
int
xfs_qm_scall_quotaoff(
	xfs_mount_t		*mp,
	uint			flags)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	uint			dqtype;
	int			error;
	uint			inactivate_flags;
	xfs_qoff_logitem_t	*qoffstart;
	int			nculprits;

	/*
	 * No file system can have quotas enabled on disk but not in core.
	 * Note that quota utilities (like quotaoff) _expect_
	 * errno == EEXIST here.
	 */
	if ((mp->m_qflags & flags) == 0)
		return XFS_ERROR(EEXIST);
	error = 0;

	flags &= (XFS_ALL_QUOTA_ACCT | XFS_ALL_QUOTA_ENFD);

	/*
	 * We don't want to deal with two quotaoffs messing up each other,
	 * so we're going to serialize it. quotaoff isn't exactly a performance
	 * critical thing.
	 * If quotaoff, then we must be dealing with the root filesystem.
	 */
	ASSERT(q);
	mutex_lock(&q->qi_quotaofflock);

	/*
	 * If we're just turning off quota enforcement, change mp and go.
	 */
	if ((flags & XFS_ALL_QUOTA_ACCT) == 0) {
		mp->m_qflags &= ~(flags);

		spin_lock(&mp->m_sb_lock);
		mp->m_sb.sb_qflags = mp->m_qflags;
		spin_unlock(&mp->m_sb_lock);
		mutex_unlock(&q->qi_quotaofflock);

		/* XXX what to do if error ? Revert back to old vals incore ? */
		error = xfs_qm_write_sb_changes(mp, XFS_SB_QFLAGS);
		return (error);
	}

	dqtype = 0;
	inactivate_flags = 0;
	/*
	 * If accounting is off, we must turn enforcement off, clear the
	 * quota 'CHKD' certificate to make it known that we have to
	 * do a quotacheck the next time this quota is turned on.
	 */
	if (flags & XFS_UQUOTA_ACCT) {
		dqtype |= XFS_QMOPT_UQUOTA;
		flags |= (XFS_UQUOTA_CHKD | XFS_UQUOTA_ENFD);
		inactivate_flags |= XFS_UQUOTA_ACTIVE;
	}
	if (flags & XFS_GQUOTA_ACCT) {
		dqtype |= XFS_QMOPT_GQUOTA;
		flags |= (XFS_OQUOTA_CHKD | XFS_OQUOTA_ENFD);
		inactivate_flags |= XFS_GQUOTA_ACTIVE;
	} else if (flags & XFS_PQUOTA_ACCT) {
		dqtype |= XFS_QMOPT_PQUOTA;
		flags |= (XFS_OQUOTA_CHKD | XFS_OQUOTA_ENFD);
		inactivate_flags |= XFS_PQUOTA_ACTIVE;
	}

	/*
	 * Nothing to do?  Don't complain. This happens when we're just
	 * turning off quota enforcement.
	 */
	if ((mp->m_qflags & flags) == 0)
		goto out_unlock;

	/*
	 * Write the LI_QUOTAOFF log record, and do SB changes atomically,
	 * and synchronously. If we fail to write, we should abort the
	 * operation as it cannot be recovered safely if we crash.
	 */
	error = xfs_qm_log_quotaoff(mp, &qoffstart, flags);
	if (error)
		goto out_unlock;

	/*
	 * Next we clear the XFS_MOUNT_*DQ_ACTIVE bit(s) in the mount struct
	 * to take care of the race between dqget and quotaoff. We don't take
	 * any special locks to reset these bits. All processes need to check
	 * these bits *after* taking inode lock(s) to see if the particular
	 * quota type is in the process of being turned off. If *ACTIVE, it is
	 * guaranteed that all dquot structures and all quotainode ptrs will all
	 * stay valid as long as that inode is kept locked.
	 *
	 * There is no turning back after this.
	 */
	mp->m_qflags &= ~inactivate_flags;

	/*
	 * Give back all the dquot reference(s) held by inodes.
	 * Here we go thru every single incore inode in this file system, and
	 * do a dqrele on the i_udquot/i_gdquot that it may have.
	 * Essentially, as long as somebody has an inode locked, this guarantees
	 * that quotas will not be turned off. This is handy because in a
	 * transaction once we lock the inode(s) and check for quotaon, we can
	 * depend on the quota inodes (and other things) being valid as long as
	 * we keep the lock(s).
	 */
	xfs_qm_dqrele_all_inodes(mp, flags);

	/*
	 * Next we make the changes in the quota flag in the mount struct.
	 * This isn't protected by a particular lock directly, because we
	 * don't want to take a mrlock every time we depend on quotas being on.
	 */
	mp->m_qflags &= ~(flags);

	/*
	 * Go through all the dquots of this file system and purge them,
	 * according to what was turned off. We may not be able to get rid
	 * of all dquots, because dquots can have temporary references that
	 * are not attached to inodes. eg. xfs_setattr, xfs_create.
	 * So, if we couldn't purge all the dquots from the filesystem,
	 * we can't get rid of the incore data structures.
	 */
	while ((nculprits = xfs_qm_dqpurge_all(mp, dqtype)))
		delay(10 * nculprits);

	/*
	 * Transactions that had started before ACTIVE state bit was cleared
	 * could have logged many dquots, so they'd have higher LSNs than
	 * the first QUOTAOFF log record does. If we happen to crash when
	 * the tail of the log has gone past the QUOTAOFF record, but
	 * before the last dquot modification, those dquots __will__
	 * recover, and that's not good.
	 *
	 * So, we have QUOTAOFF start and end logitems; the start
	 * logitem won't get overwritten until the end logitem appears...
	 */
	error = xfs_qm_log_quotaoff_end(mp, qoffstart, flags);
	if (error) {
		/* We're screwed now. Shutdown is the only option. */
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		goto out_unlock;
	}

	/*
	 * If quotas is completely disabled, close shop.
	 */
	if (((flags & XFS_MOUNT_QUOTA_ALL) == XFS_MOUNT_QUOTA_SET1) ||
	    ((flags & XFS_MOUNT_QUOTA_ALL) == XFS_MOUNT_QUOTA_SET2)) {
		mutex_unlock(&q->qi_quotaofflock);
		xfs_qm_destroy_quotainfo(mp);
		return (0);
	}

	/*
	 * Release our quotainode references if we don't need them anymore.
	 */
	if ((dqtype & XFS_QMOPT_UQUOTA) && q->qi_uquotaip) {
		IRELE(q->qi_uquotaip);
		q->qi_uquotaip = NULL;
	}
	if ((dqtype & (XFS_QMOPT_GQUOTA|XFS_QMOPT_PQUOTA)) && q->qi_gquotaip) {
		IRELE(q->qi_gquotaip);
		q->qi_gquotaip = NULL;
	}

out_unlock:
	mutex_unlock(&q->qi_quotaofflock);
	return error;
}

STATIC int
xfs_qm_scall_trunc_qfile(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	struct xfs_inode	*ip;
	struct xfs_trans	*tp;
	int			error;

	if (ino == NULLFSINO)
		return 0;

	error = xfs_iget(mp, NULL, ino, 0, 0, &ip);
	if (error)
		return error;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	tp = xfs_trans_alloc(mp, XFS_TRANS_TRUNCATE_FILE);
	error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
				  XFS_TRANS_PERM_LOG_RES,
				  XFS_ITRUNCATE_LOG_COUNT);
	if (error) {
		xfs_trans_cancel(tp, 0);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		goto out_put;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	ip->i_d.di_size = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, 0);
	if (error) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES |
				     XFS_TRANS_ABORT);
		goto out_unlock;
	}

	ASSERT(ip->i_d.di_nextents == 0);

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);

out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
out_put:
	IRELE(ip);
	return error;
}

int
xfs_qm_scall_trunc_qfiles(
	xfs_mount_t	*mp,
	uint		flags)
{
	int		error = 0, error2 = 0;

	if (!xfs_sb_version_hasquota(&mp->m_sb) || flags == 0) {
		xfs_debug(mp, "%s: flags=%x m_qflags=%x\n",
			__func__, flags, mp->m_qflags);
		return XFS_ERROR(EINVAL);
	}

	if (flags & XFS_DQ_USER)
		error = xfs_qm_scall_trunc_qfile(mp, mp->m_sb.sb_uquotino);
	if (flags & (XFS_DQ_GROUP|XFS_DQ_PROJ))
		error2 = xfs_qm_scall_trunc_qfile(mp, mp->m_sb.sb_gquotino);

	return error ? error : error2;
}

/*
 * Switch on (a given) quota enforcement for a filesystem.  This takes
 * effect immediately.
 * (Switching on quota accounting must be done at mount time.)
 */
int
xfs_qm_scall_quotaon(
	xfs_mount_t	*mp,
	uint		flags)
{
	int		error;
	uint		qf;
	__int64_t	sbflags;

	flags &= (XFS_ALL_QUOTA_ACCT | XFS_ALL_QUOTA_ENFD);
	/*
	 * Switching on quota accounting must be done at mount time.
	 */
	flags &= ~(XFS_ALL_QUOTA_ACCT);

	sbflags = 0;

	if (flags == 0) {
		xfs_debug(mp, "%s: zero flags, m_qflags=%x\n",
			__func__, mp->m_qflags);
		return XFS_ERROR(EINVAL);
	}

	/* No fs can turn on quotas with a delayed effect */
	ASSERT((flags & XFS_ALL_QUOTA_ACCT) == 0);

	/*
	 * Can't enforce without accounting. We check the superblock
	 * qflags here instead of m_qflags because rootfs can have
	 * quota acct on ondisk without m_qflags' knowing.
	 */
	if (((flags & XFS_UQUOTA_ACCT) == 0 &&
	    (mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT) == 0 &&
	    (flags & XFS_UQUOTA_ENFD))
	    ||
	    ((flags & XFS_PQUOTA_ACCT) == 0 &&
	    (mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT) == 0 &&
	    (flags & XFS_GQUOTA_ACCT) == 0 &&
	    (mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT) == 0 &&
	    (flags & XFS_OQUOTA_ENFD))) {
		xfs_debug(mp,
			"%s: Can't enforce without acct, flags=%x sbflags=%x\n",
			__func__, flags, mp->m_sb.sb_qflags);
		return XFS_ERROR(EINVAL);
	}
	/*
	 * If everything's up to-date incore, then don't waste time.
	 */
	if ((mp->m_qflags & flags) == flags)
		return XFS_ERROR(EEXIST);

	/*
	 * Change sb_qflags on disk but not incore mp->qflags
	 * if this is the root filesystem.
	 */
	spin_lock(&mp->m_sb_lock);
	qf = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = qf | flags;
	spin_unlock(&mp->m_sb_lock);

	/*
	 * There's nothing to change if it's the same.
	 */
	if ((qf & flags) == flags && sbflags == 0)
		return XFS_ERROR(EEXIST);
	sbflags |= XFS_SB_QFLAGS;

	if ((error = xfs_qm_write_sb_changes(mp, sbflags)))
		return (error);
	/*
	 * If we aren't trying to switch on quota enforcement, we are done.
	 */
	if  (((mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT) !=
	     (mp->m_qflags & XFS_UQUOTA_ACCT)) ||
	     ((mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT) !=
	     (mp->m_qflags & XFS_PQUOTA_ACCT)) ||
	     ((mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT) !=
	     (mp->m_qflags & XFS_GQUOTA_ACCT)) ||
	    (flags & XFS_ALL_QUOTA_ENFD) == 0)
		return (0);

	if (! XFS_IS_QUOTA_RUNNING(mp))
		return XFS_ERROR(ESRCH);

	/*
	 * Switch on quota enforcement in core.
	 */
	mutex_lock(&mp->m_quotainfo->qi_quotaofflock);
	mp->m_qflags |= (flags & XFS_ALL_QUOTA_ENFD);
	mutex_unlock(&mp->m_quotainfo->qi_quotaofflock);

	return (0);
}


/*
 * Return quota status information, such as uquota-off, enforcements, etc.
 */
int
xfs_qm_scall_getqstat(
	struct xfs_mount	*mp,
	struct fs_quota_stat	*out)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_inode	*uip, *gip;
	boolean_t		tempuqip, tempgqip;

	uip = gip = NULL;
	tempuqip = tempgqip = B_FALSE;
	memset(out, 0, sizeof(fs_quota_stat_t));

	out->qs_version = FS_QSTAT_VERSION;
	if (!xfs_sb_version_hasquota(&mp->m_sb)) {
		out->qs_uquota.qfs_ino = NULLFSINO;
		out->qs_gquota.qfs_ino = NULLFSINO;
		return (0);
	}
	out->qs_flags = (__uint16_t) xfs_qm_export_flags(mp->m_qflags &
							(XFS_ALL_QUOTA_ACCT|
							 XFS_ALL_QUOTA_ENFD));
	out->qs_pad = 0;
	out->qs_uquota.qfs_ino = mp->m_sb.sb_uquotino;
	out->qs_gquota.qfs_ino = mp->m_sb.sb_gquotino;

	if (q) {
		uip = q->qi_uquotaip;
		gip = q->qi_gquotaip;
	}
	if (!uip && mp->m_sb.sb_uquotino != NULLFSINO) {
		if (xfs_iget(mp, NULL, mp->m_sb.sb_uquotino,
					0, 0, &uip) == 0)
			tempuqip = B_TRUE;
	}
	if (!gip && mp->m_sb.sb_gquotino != NULLFSINO) {
		if (xfs_iget(mp, NULL, mp->m_sb.sb_gquotino,
					0, 0, &gip) == 0)
			tempgqip = B_TRUE;
	}
	if (uip) {
		out->qs_uquota.qfs_nblks = uip->i_d.di_nblocks;
		out->qs_uquota.qfs_nextents = uip->i_d.di_nextents;
		if (tempuqip)
			IRELE(uip);
	}
	if (gip) {
		out->qs_gquota.qfs_nblks = gip->i_d.di_nblocks;
		out->qs_gquota.qfs_nextents = gip->i_d.di_nextents;
		if (tempgqip)
			IRELE(gip);
	}
	if (q) {
		out->qs_incoredqs = q->qi_dquots;
		out->qs_btimelimit = q->qi_btimelimit;
		out->qs_itimelimit = q->qi_itimelimit;
		out->qs_rtbtimelimit = q->qi_rtbtimelimit;
		out->qs_bwarnlimit = q->qi_bwarnlimit;
		out->qs_iwarnlimit = q->qi_iwarnlimit;
	}
	return 0;
}

#define XFS_DQ_MASK \
	(FS_DQ_LIMIT_MASK | FS_DQ_TIMER_MASK | FS_DQ_WARNS_MASK)

/*
 * Adjust quota limits, and start/stop timers accordingly.
 */
int
xfs_qm_scall_setqlim(
	xfs_mount_t		*mp,
	xfs_dqid_t		id,
	uint			type,
	fs_disk_quota_t		*newlim)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	xfs_disk_dquot_t	*ddq;
	xfs_dquot_t		*dqp;
	xfs_trans_t		*tp;
	int			error;
	xfs_qcnt_t		hard, soft;

	if (newlim->d_fieldmask & ~XFS_DQ_MASK)
		return EINVAL;
	if ((newlim->d_fieldmask & XFS_DQ_MASK) == 0)
		return 0;

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_SETQLIM);
	if ((error = xfs_trans_reserve(tp, 0, sizeof(xfs_disk_dquot_t) + 128,
				      0, 0, XFS_DEFAULT_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		return (error);
	}

	/*
	 * We don't want to race with a quotaoff so take the quotaoff lock.
	 * (We don't hold an inode lock, so there's nothing else to stop
	 * a quotaoff from happening). (XXXThis doesn't currently happen
	 * because we take the vfslock before calling xfs_qm_sysent).
	 */
	mutex_lock(&q->qi_quotaofflock);

	/*
	 * Get the dquot (locked), and join it to the transaction.
	 * Allocate the dquot if this doesn't exist.
	 */
	if ((error = xfs_qm_dqget(mp, NULL, id, type, XFS_QMOPT_DQALLOC, &dqp))) {
		xfs_trans_cancel(tp, XFS_TRANS_ABORT);
		ASSERT(error != ENOENT);
		goto out_unlock;
	}
	xfs_trans_dqjoin(tp, dqp);
	ddq = &dqp->q_core;

	/*
	 * Make sure that hardlimits are >= soft limits before changing.
	 */
	hard = (newlim->d_fieldmask & FS_DQ_BHARD) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim->d_blk_hardlimit) :
			be64_to_cpu(ddq->d_blk_hardlimit);
	soft = (newlim->d_fieldmask & FS_DQ_BSOFT) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim->d_blk_softlimit) :
			be64_to_cpu(ddq->d_blk_softlimit);
	if (hard == 0 || hard >= soft) {
		ddq->d_blk_hardlimit = cpu_to_be64(hard);
		ddq->d_blk_softlimit = cpu_to_be64(soft);
		if (id == 0) {
			q->qi_bhardlimit = hard;
			q->qi_bsoftlimit = soft;
		}
	} else {
		xfs_debug(mp, "blkhard %Ld < blksoft %Ld\n", hard, soft);
	}
	hard = (newlim->d_fieldmask & FS_DQ_RTBHARD) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim->d_rtb_hardlimit) :
			be64_to_cpu(ddq->d_rtb_hardlimit);
	soft = (newlim->d_fieldmask & FS_DQ_RTBSOFT) ?
		(xfs_qcnt_t) XFS_BB_TO_FSB(mp, newlim->d_rtb_softlimit) :
			be64_to_cpu(ddq->d_rtb_softlimit);
	if (hard == 0 || hard >= soft) {
		ddq->d_rtb_hardlimit = cpu_to_be64(hard);
		ddq->d_rtb_softlimit = cpu_to_be64(soft);
		if (id == 0) {
			q->qi_rtbhardlimit = hard;
			q->qi_rtbsoftlimit = soft;
		}
	} else {
		xfs_debug(mp, "rtbhard %Ld < rtbsoft %Ld\n", hard, soft);
	}

	hard = (newlim->d_fieldmask & FS_DQ_IHARD) ?
		(xfs_qcnt_t) newlim->d_ino_hardlimit :
			be64_to_cpu(ddq->d_ino_hardlimit);
	soft = (newlim->d_fieldmask & FS_DQ_ISOFT) ?
		(xfs_qcnt_t) newlim->d_ino_softlimit :
			be64_to_cpu(ddq->d_ino_softlimit);
	if (hard == 0 || hard >= soft) {
		ddq->d_ino_hardlimit = cpu_to_be64(hard);
		ddq->d_ino_softlimit = cpu_to_be64(soft);
		if (id == 0) {
			q->qi_ihardlimit = hard;
			q->qi_isoftlimit = soft;
		}
	} else {
		xfs_debug(mp, "ihard %Ld < isoft %Ld\n", hard, soft);
	}

	/*
	 * Update warnings counter(s) if requested
	 */
	if (newlim->d_fieldmask & FS_DQ_BWARNS)
		ddq->d_bwarns = cpu_to_be16(newlim->d_bwarns);
	if (newlim->d_fieldmask & FS_DQ_IWARNS)
		ddq->d_iwarns = cpu_to_be16(newlim->d_iwarns);
	if (newlim->d_fieldmask & FS_DQ_RTBWARNS)
		ddq->d_rtbwarns = cpu_to_be16(newlim->d_rtbwarns);

	if (id == 0) {
		/*
		 * Timelimits for the super user set the relative time
		 * the other users can be over quota for this file system.
		 * If it is zero a default is used.  Ditto for the default
		 * soft and hard limit values (already done, above), and
		 * for warnings.
		 */
		if (newlim->d_fieldmask & FS_DQ_BTIMER) {
			q->qi_btimelimit = newlim->d_btimer;
			ddq->d_btimer = cpu_to_be32(newlim->d_btimer);
		}
		if (newlim->d_fieldmask & FS_DQ_ITIMER) {
			q->qi_itimelimit = newlim->d_itimer;
			ddq->d_itimer = cpu_to_be32(newlim->d_itimer);
		}
		if (newlim->d_fieldmask & FS_DQ_RTBTIMER) {
			q->qi_rtbtimelimit = newlim->d_rtbtimer;
			ddq->d_rtbtimer = cpu_to_be32(newlim->d_rtbtimer);
		}
		if (newlim->d_fieldmask & FS_DQ_BWARNS)
			q->qi_bwarnlimit = newlim->d_bwarns;
		if (newlim->d_fieldmask & FS_DQ_IWARNS)
			q->qi_iwarnlimit = newlim->d_iwarns;
		if (newlim->d_fieldmask & FS_DQ_RTBWARNS)
			q->qi_rtbwarnlimit = newlim->d_rtbwarns;
	} else {
		/*
		 * If the user is now over quota, start the timelimit.
		 * The user will not be 'warned'.
		 * Note that we keep the timers ticking, whether enforcement
		 * is on or off. We don't really want to bother with iterating
		 * over all ondisk dquots and turning the timers on/off.
		 */
		xfs_qm_adjust_dqtimers(mp, ddq);
	}
	dqp->dq_flags |= XFS_DQ_DIRTY;
	xfs_trans_log_dquot(tp, dqp);

	error = xfs_trans_commit(tp, 0);
	xfs_qm_dqrele(dqp);

 out_unlock:
	mutex_unlock(&q->qi_quotaofflock);
	return error;
}

int
xfs_qm_scall_getquota(
	xfs_mount_t	*mp,
	xfs_dqid_t	id,
	uint		type,
	fs_disk_quota_t *out)
{
	xfs_dquot_t	*dqp;
	int		error;

	/*
	 * Try to get the dquot. We don't want it allocated on disk, so
	 * we aren't passing the XFS_QMOPT_DOALLOC flag. If it doesn't
	 * exist, we'll get ENOENT back.
	 */
	if ((error = xfs_qm_dqget(mp, NULL, id, type, 0, &dqp))) {
		return (error);
	}

	/*
	 * If everything's NULL, this dquot doesn't quite exist as far as
	 * our utility programs are concerned.
	 */
	if (XFS_IS_DQUOT_UNINITIALIZED(dqp)) {
		xfs_qm_dqput(dqp);
		return XFS_ERROR(ENOENT);
	}
	/*
	 * Convert the disk dquot to the exportable format
	 */
	xfs_qm_export_dquot(mp, &dqp->q_core, out);
	xfs_qm_dqput(dqp);
	return (error ? XFS_ERROR(EFAULT) : 0);
}


STATIC int
xfs_qm_log_quotaoff_end(
	xfs_mount_t		*mp,
	xfs_qoff_logitem_t	*startqoff,
	uint			flags)
{
	xfs_trans_t		*tp;
	int			error;
	xfs_qoff_logitem_t	*qoffi;

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_QUOTAOFF_END);

	if ((error = xfs_trans_reserve(tp, 0, sizeof(xfs_qoff_logitem_t) * 2,
				      0, 0, XFS_DEFAULT_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		return (error);
	}

	qoffi = xfs_trans_get_qoff_item(tp, startqoff,
					flags & XFS_ALL_QUOTA_ACCT);
	xfs_trans_log_quotaoff_item(tp, qoffi);

	/*
	 * We have to make sure that the transaction is secure on disk before we
	 * return and actually stop quota accounting. So, make it synchronous.
	 * We don't care about quotoff's performance.
	 */
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);
	return (error);
}


STATIC int
xfs_qm_log_quotaoff(
	xfs_mount_t	       *mp,
	xfs_qoff_logitem_t     **qoffstartp,
	uint		       flags)
{
	xfs_trans_t	       *tp;
	int			error;
	xfs_qoff_logitem_t     *qoffi=NULL;
	uint			oldsbqflag=0;

	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_QUOTAOFF);
	if ((error = xfs_trans_reserve(tp, 0,
				      sizeof(xfs_qoff_logitem_t) * 2 +
				      mp->m_sb.sb_sectsize + 128,
				      0,
				      0,
				      XFS_DEFAULT_LOG_COUNT))) {
		goto error0;
	}

	qoffi = xfs_trans_get_qoff_item(tp, NULL, flags & XFS_ALL_QUOTA_ACCT);
	xfs_trans_log_quotaoff_item(tp, qoffi);

	spin_lock(&mp->m_sb_lock);
	oldsbqflag = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = (mp->m_qflags & ~(flags)) & XFS_MOUNT_QUOTA_ALL;
	spin_unlock(&mp->m_sb_lock);

	xfs_mod_sb(tp, XFS_SB_QFLAGS);

	/*
	 * We have to make sure that the transaction is secure on disk before we
	 * return and actually stop quota accounting. So, make it synchronous.
	 * We don't care about quotoff's performance.
	 */
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);

error0:
	if (error) {
		xfs_trans_cancel(tp, 0);
		/*
		 * No one else is modifying sb_qflags, so this is OK.
		 * We still hold the quotaofflock.
		 */
		spin_lock(&mp->m_sb_lock);
		mp->m_sb.sb_qflags = oldsbqflag;
		spin_unlock(&mp->m_sb_lock);
	}
	*qoffstartp = qoffi;
	return (error);
}


/*
 * Translate an internal style on-disk-dquot to the exportable format.
 * The main differences are that the counters/limits are all in Basic
 * Blocks (BBs) instead of the internal FSBs, and all on-disk data has
 * to be converted to the native endianness.
 */
STATIC void
xfs_qm_export_dquot(
	xfs_mount_t		*mp,
	xfs_disk_dquot_t	*src,
	struct fs_disk_quota	*dst)
{
	memset(dst, 0, sizeof(*dst));
	dst->d_version = FS_DQUOT_VERSION;  /* different from src->d_version */
	dst->d_flags = xfs_qm_export_qtype_flags(src->d_flags);
	dst->d_id = be32_to_cpu(src->d_id);
	dst->d_blk_hardlimit =
		XFS_FSB_TO_BB(mp, be64_to_cpu(src->d_blk_hardlimit));
	dst->d_blk_softlimit =
		XFS_FSB_TO_BB(mp, be64_to_cpu(src->d_blk_softlimit));
	dst->d_ino_hardlimit = be64_to_cpu(src->d_ino_hardlimit);
	dst->d_ino_softlimit = be64_to_cpu(src->d_ino_softlimit);
	dst->d_bcount = XFS_FSB_TO_BB(mp, be64_to_cpu(src->d_bcount));
	dst->d_icount = be64_to_cpu(src->d_icount);
	dst->d_btimer = be32_to_cpu(src->d_btimer);
	dst->d_itimer = be32_to_cpu(src->d_itimer);
	dst->d_iwarns = be16_to_cpu(src->d_iwarns);
	dst->d_bwarns = be16_to_cpu(src->d_bwarns);
	dst->d_rtb_hardlimit =
		XFS_FSB_TO_BB(mp, be64_to_cpu(src->d_rtb_hardlimit));
	dst->d_rtb_softlimit =
		XFS_FSB_TO_BB(mp, be64_to_cpu(src->d_rtb_softlimit));
	dst->d_rtbcount = XFS_FSB_TO_BB(mp, be64_to_cpu(src->d_rtbcount));
	dst->d_rtbtimer = be32_to_cpu(src->d_rtbtimer);
	dst->d_rtbwarns = be16_to_cpu(src->d_rtbwarns);

	/*
	 * Internally, we don't reset all the timers when quota enforcement
	 * gets turned off. No need to confuse the user level code,
	 * so return zeroes in that case.
	 */
	if ((!XFS_IS_UQUOTA_ENFORCED(mp) && src->d_flags == XFS_DQ_USER) ||
	    (!XFS_IS_OQUOTA_ENFORCED(mp) &&
			(src->d_flags & (XFS_DQ_PROJ | XFS_DQ_GROUP)))) {
		dst->d_btimer = 0;
		dst->d_itimer = 0;
		dst->d_rtbtimer = 0;
	}

#ifdef DEBUG
	if (((XFS_IS_UQUOTA_ENFORCED(mp) && dst->d_flags == FS_USER_QUOTA) ||
	     (XFS_IS_OQUOTA_ENFORCED(mp) &&
			(dst->d_flags & (FS_PROJ_QUOTA | FS_GROUP_QUOTA)))) &&
	    dst->d_id != 0) {
		if (((int) dst->d_bcount > (int) dst->d_blk_softlimit) &&
		    (dst->d_blk_softlimit > 0)) {
			ASSERT(dst->d_btimer != 0);
		}
		if (((int) dst->d_icount > (int) dst->d_ino_softlimit) &&
		    (dst->d_ino_softlimit > 0)) {
			ASSERT(dst->d_itimer != 0);
		}
	}
#endif
}

STATIC uint
xfs_qm_export_qtype_flags(
	uint flags)
{
	/*
	 * Can't be more than one, or none.
	 */
	ASSERT((flags & (FS_PROJ_QUOTA | FS_USER_QUOTA)) !=
		(FS_PROJ_QUOTA | FS_USER_QUOTA));
	ASSERT((flags & (FS_PROJ_QUOTA | FS_GROUP_QUOTA)) !=
		(FS_PROJ_QUOTA | FS_GROUP_QUOTA));
	ASSERT((flags & (FS_USER_QUOTA | FS_GROUP_QUOTA)) !=
		(FS_USER_QUOTA | FS_GROUP_QUOTA));
	ASSERT((flags & (FS_PROJ_QUOTA|FS_USER_QUOTA|FS_GROUP_QUOTA)) != 0);

	return (flags & XFS_DQ_USER) ?
		FS_USER_QUOTA : (flags & XFS_DQ_PROJ) ?
			FS_PROJ_QUOTA : FS_GROUP_QUOTA;
}

STATIC uint
xfs_qm_export_flags(
	uint flags)
{
	uint uflags;

	uflags = 0;
	if (flags & XFS_UQUOTA_ACCT)
		uflags |= FS_QUOTA_UDQ_ACCT;
	if (flags & XFS_PQUOTA_ACCT)
		uflags |= FS_QUOTA_PDQ_ACCT;
	if (flags & XFS_GQUOTA_ACCT)
		uflags |= FS_QUOTA_GDQ_ACCT;
	if (flags & XFS_UQUOTA_ENFD)
		uflags |= FS_QUOTA_UDQ_ENFD;
	if (flags & (XFS_OQUOTA_ENFD)) {
		uflags |= (flags & XFS_GQUOTA_ACCT) ?
			FS_QUOTA_GDQ_ENFD : FS_QUOTA_PDQ_ENFD;
	}
	return (uflags);
}


STATIC int
xfs_dqrele_inode(
	struct xfs_inode	*ip,
	struct xfs_perag	*pag,
	int			flags)
{
	/* skip quota inodes */
	if (ip == ip->i_mount->m_quotainfo->qi_uquotaip ||
	    ip == ip->i_mount->m_quotainfo->qi_gquotaip) {
		ASSERT(ip->i_udquot == NULL);
		ASSERT(ip->i_gdquot == NULL);
		return 0;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if ((flags & XFS_UQUOTA_ACCT) && ip->i_udquot) {
		xfs_qm_dqrele(ip->i_udquot);
		ip->i_udquot = NULL;
	}
	if (flags & (XFS_PQUOTA_ACCT|XFS_GQUOTA_ACCT) && ip->i_gdquot) {
		xfs_qm_dqrele(ip->i_gdquot);
		ip->i_gdquot = NULL;
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}


/*
 * Go thru all the inodes in the file system, releasing their dquots.
 *
 * Note that the mount structure gets modified to indicate that quotas are off
 * AFTER this, in the case of quotaoff.
 */
void
xfs_qm_dqrele_all_inodes(
	struct xfs_mount *mp,
	uint		 flags)
{
	ASSERT(mp->m_quotainfo);
	xfs_inode_ag_iterator(mp, xfs_dqrele_inode, flags);
}
