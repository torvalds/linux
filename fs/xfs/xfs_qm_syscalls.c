// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */


#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_icache.h"

STATIC int
xfs_qm_log_quotaoff(
	struct xfs_mount	*mp,
	struct xfs_qoff_logitem	**qoffstartp,
	uint			flags)
{
	struct xfs_trans	*tp;
	int			error;
	struct xfs_qoff_logitem	*qoffi;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_qm_quotaoff, 0, 0, 0, &tp);
	if (error)
		goto out;

	qoffi = xfs_trans_get_qoff_item(tp, NULL, flags & XFS_ALL_QUOTA_ACCT);
	xfs_trans_log_quotaoff_item(tp, qoffi);

	spin_lock(&mp->m_sb_lock);
	mp->m_sb.sb_qflags = (mp->m_qflags & ~(flags)) & XFS_MOUNT_QUOTA_ALL;
	spin_unlock(&mp->m_sb_lock);

	xfs_log_sb(tp);

	/*
	 * We have to make sure that the transaction is secure on disk before we
	 * return and actually stop quota accounting. So, make it synchronous.
	 * We don't care about quotoff's performance.
	 */
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp);
	if (error)
		goto out;

	*qoffstartp = qoffi;
out:
	return error;
}

STATIC int
xfs_qm_log_quotaoff_end(
	struct xfs_mount	*mp,
	struct xfs_qoff_logitem	**startqoff,
	uint			flags)
{
	struct xfs_trans	*tp;
	int			error;
	struct xfs_qoff_logitem	*qoffi;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_qm_equotaoff, 0, 0, 0, &tp);
	if (error)
		return error;

	qoffi = xfs_trans_get_qoff_item(tp, *startqoff,
					flags & XFS_ALL_QUOTA_ACCT);
	xfs_trans_log_quotaoff_item(tp, qoffi);
	*startqoff = NULL;

	/*
	 * We have to make sure that the transaction is secure on disk before we
	 * return and actually stop quota accounting. So, make it synchronous.
	 * We don't care about quotoff's performance.
	 */
	xfs_trans_set_sync(tp);
	return xfs_trans_commit(tp);
}

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
	struct xfs_qoff_logitem	*qoffstart = NULL;

	/*
	 * No file system can have quotas enabled on disk but not in core.
	 * Note that quota utilities (like quotaoff) _expect_
	 * errno == -EEXIST here.
	 */
	if ((mp->m_qflags & flags) == 0)
		return -EEXIST;
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
		return xfs_sync_sb(mp, false);
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
		flags |= (XFS_GQUOTA_CHKD | XFS_GQUOTA_ENFD);
		inactivate_flags |= XFS_GQUOTA_ACTIVE;
	}
	if (flags & XFS_PQUOTA_ACCT) {
		dqtype |= XFS_QMOPT_PQUOTA;
		flags |= (XFS_PQUOTA_CHKD | XFS_PQUOTA_ENFD);
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
	error = xfs_dqrele_all_inodes(mp, flags);
	ASSERT(!error);

	/*
	 * Next we make the changes in the quota flag in the mount struct.
	 * This isn't protected by a particular lock directly, because we
	 * don't want to take a mrlock every time we depend on quotas being on.
	 */
	mp->m_qflags &= ~flags;

	/*
	 * Go through all the dquots of this file system and purge them,
	 * according to what was turned off.
	 */
	xfs_qm_dqpurge_all(mp, dqtype);

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
	error = xfs_qm_log_quotaoff_end(mp, &qoffstart, flags);
	if (error) {
		/* We're screwed now. Shutdown is the only option. */
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		goto out_unlock;
	}

	/*
	 * If all quotas are completely turned off, close shop.
	 */
	if (mp->m_qflags == 0) {
		mutex_unlock(&q->qi_quotaofflock);
		xfs_qm_destroy_quotainfo(mp);
		return 0;
	}

	/*
	 * Release our quotainode references if we don't need them anymore.
	 */
	if ((dqtype & XFS_QMOPT_UQUOTA) && q->qi_uquotaip) {
		xfs_irele(q->qi_uquotaip);
		q->qi_uquotaip = NULL;
	}
	if ((dqtype & XFS_QMOPT_GQUOTA) && q->qi_gquotaip) {
		xfs_irele(q->qi_gquotaip);
		q->qi_gquotaip = NULL;
	}
	if ((dqtype & XFS_QMOPT_PQUOTA) && q->qi_pquotaip) {
		xfs_irele(q->qi_pquotaip);
		q->qi_pquotaip = NULL;
	}

out_unlock:
	if (error && qoffstart)
		xfs_qm_qoff_logitem_relse(qoffstart);
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

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate, 0, 0, 0, &tp);
	if (error) {
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		goto out_put;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	ip->i_disk_size = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, 0);
	if (error) {
		xfs_trans_cancel(tp);
		goto out_unlock;
	}

	ASSERT(ip->i_df.if_nextents == 0);

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	error = xfs_trans_commit(tp);

out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
out_put:
	xfs_irele(ip);
	return error;
}

int
xfs_qm_scall_trunc_qfiles(
	xfs_mount_t	*mp,
	uint		flags)
{
	int		error = -EINVAL;

	if (!xfs_sb_version_hasquota(&mp->m_sb) || flags == 0 ||
	    (flags & ~XFS_QMOPT_QUOTALL)) {
		xfs_debug(mp, "%s: flags=%x m_qflags=%x",
			__func__, flags, mp->m_qflags);
		return -EINVAL;
	}

	if (flags & XFS_QMOPT_UQUOTA) {
		error = xfs_qm_scall_trunc_qfile(mp, mp->m_sb.sb_uquotino);
		if (error)
			return error;
	}
	if (flags & XFS_QMOPT_GQUOTA) {
		error = xfs_qm_scall_trunc_qfile(mp, mp->m_sb.sb_gquotino);
		if (error)
			return error;
	}
	if (flags & XFS_QMOPT_PQUOTA)
		error = xfs_qm_scall_trunc_qfile(mp, mp->m_sb.sb_pquotino);

	return error;
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

	/*
	 * Switching on quota accounting must be done at mount time,
	 * only consider quota enforcement stuff here.
	 */
	flags &= XFS_ALL_QUOTA_ENFD;

	if (flags == 0) {
		xfs_debug(mp, "%s: zero flags, m_qflags=%x",
			__func__, mp->m_qflags);
		return -EINVAL;
	}

	/*
	 * Can't enforce without accounting. We check the superblock
	 * qflags here instead of m_qflags because rootfs can have
	 * quota acct on ondisk without m_qflags' knowing.
	 */
	if (((mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT) == 0 &&
	     (flags & XFS_UQUOTA_ENFD)) ||
	    ((mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT) == 0 &&
	     (flags & XFS_GQUOTA_ENFD)) ||
	    ((mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT) == 0 &&
	     (flags & XFS_PQUOTA_ENFD))) {
		xfs_debug(mp,
			"%s: Can't enforce without acct, flags=%x sbflags=%x",
			__func__, flags, mp->m_sb.sb_qflags);
		return -EINVAL;
	}
	/*
	 * If everything's up to-date incore, then don't waste time.
	 */
	if ((mp->m_qflags & flags) == flags)
		return -EEXIST;

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
	if ((qf & flags) == flags)
		return -EEXIST;

	error = xfs_sync_sb(mp, false);
	if (error)
		return error;
	/*
	 * If we aren't trying to switch on quota enforcement, we are done.
	 */
	if  (((mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT) !=
	     (mp->m_qflags & XFS_UQUOTA_ACCT)) ||
	     ((mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT) !=
	     (mp->m_qflags & XFS_PQUOTA_ACCT)) ||
	     ((mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT) !=
	     (mp->m_qflags & XFS_GQUOTA_ACCT)))
		return 0;

	if (! XFS_IS_QUOTA_RUNNING(mp))
		return -ESRCH;

	/*
	 * Switch on quota enforcement in core.
	 */
	mutex_lock(&mp->m_quotainfo->qi_quotaofflock);
	mp->m_qflags |= (flags & XFS_ALL_QUOTA_ENFD);
	mutex_unlock(&mp->m_quotainfo->qi_quotaofflock);

	return 0;
}

#define XFS_QC_MASK \
	(QC_LIMIT_MASK | QC_TIMER_MASK | QC_WARNS_MASK)

/*
 * Adjust limits of this quota, and the defaults if passed in.  Returns true
 * if the new limits made sense and were applied, false otherwise.
 */
static inline bool
xfs_setqlim_limits(
	struct xfs_mount	*mp,
	struct xfs_dquot_res	*res,
	struct xfs_quota_limits	*qlim,
	xfs_qcnt_t		hard,
	xfs_qcnt_t		soft,
	const char		*tag)
{
	/* The hard limit can't be less than the soft limit. */
	if (hard != 0 && hard < soft) {
		xfs_debug(mp, "%shard %lld < %ssoft %lld", tag, hard, tag,
				soft);
		return false;
	}

	res->hardlimit = hard;
	res->softlimit = soft;
	if (qlim) {
		qlim->hard = hard;
		qlim->soft = soft;
	}

	return true;
}

static inline void
xfs_setqlim_warns(
	struct xfs_dquot_res	*res,
	struct xfs_quota_limits	*qlim,
	int			warns)
{
	res->warnings = warns;
	if (qlim)
		qlim->warn = warns;
}

static inline void
xfs_setqlim_timer(
	struct xfs_mount	*mp,
	struct xfs_dquot_res	*res,
	struct xfs_quota_limits	*qlim,
	s64			timer)
{
	if (qlim) {
		/* Set the length of the default grace period. */
		res->timer = xfs_dquot_set_grace_period(timer);
		qlim->time = res->timer;
	} else {
		/* Set the grace period expiration on a quota. */
		res->timer = xfs_dquot_set_timeout(mp, timer);
	}
}

/*
 * Adjust quota limits, and start/stop timers accordingly.
 */
int
xfs_qm_scall_setqlim(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	struct qc_dqblk		*newlim)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_dquot	*dqp;
	struct xfs_trans	*tp;
	struct xfs_def_quota	*defq;
	struct xfs_dquot_res	*res;
	struct xfs_quota_limits	*qlim;
	int			error;
	xfs_qcnt_t		hard, soft;

	if (newlim->d_fieldmask & ~XFS_QC_MASK)
		return -EINVAL;
	if ((newlim->d_fieldmask & XFS_QC_MASK) == 0)
		return 0;

	/*
	 * We don't want to race with a quotaoff so take the quotaoff lock.
	 * We don't hold an inode lock, so there's nothing else to stop
	 * a quotaoff from happening.
	 */
	mutex_lock(&q->qi_quotaofflock);

	/*
	 * Get the dquot (locked) before we start, as we need to do a
	 * transaction to allocate it if it doesn't exist. Once we have the
	 * dquot, unlock it so we can start the next transaction safely. We hold
	 * a reference to the dquot, so it's safe to do this unlock/lock without
	 * it being reclaimed in the mean time.
	 */
	error = xfs_qm_dqget(mp, id, type, true, &dqp);
	if (error) {
		ASSERT(error != -ENOENT);
		goto out_unlock;
	}

	defq = xfs_get_defquota(q, xfs_dquot_type(dqp));
	xfs_dqunlock(dqp);

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_qm_setqlim, 0, 0, 0, &tp);
	if (error)
		goto out_rele;

	xfs_dqlock(dqp);
	xfs_trans_dqjoin(tp, dqp);

	/*
	 * Update quota limits, warnings, and timers, and the defaults
	 * if we're touching id == 0.
	 *
	 * Make sure that hardlimits are >= soft limits before changing.
	 *
	 * Update warnings counter(s) if requested.
	 *
	 * Timelimits for the super user set the relative time the other users
	 * can be over quota for this file system. If it is zero a default is
	 * used.  Ditto for the default soft and hard limit values (already
	 * done, above), and for warnings.
	 *
	 * For other IDs, userspace can bump out the grace period if over
	 * the soft limit.
	 */

	/* Blocks on the data device. */
	hard = (newlim->d_fieldmask & QC_SPC_HARD) ?
		(xfs_qcnt_t) XFS_B_TO_FSB(mp, newlim->d_spc_hardlimit) :
			dqp->q_blk.hardlimit;
	soft = (newlim->d_fieldmask & QC_SPC_SOFT) ?
		(xfs_qcnt_t) XFS_B_TO_FSB(mp, newlim->d_spc_softlimit) :
			dqp->q_blk.softlimit;
	res = &dqp->q_blk;
	qlim = id == 0 ? &defq->blk : NULL;

	if (xfs_setqlim_limits(mp, res, qlim, hard, soft, "blk"))
		xfs_dquot_set_prealloc_limits(dqp);
	if (newlim->d_fieldmask & QC_SPC_WARNS)
		xfs_setqlim_warns(res, qlim, newlim->d_spc_warns);
	if (newlim->d_fieldmask & QC_SPC_TIMER)
		xfs_setqlim_timer(mp, res, qlim, newlim->d_spc_timer);

	/* Blocks on the realtime device. */
	hard = (newlim->d_fieldmask & QC_RT_SPC_HARD) ?
		(xfs_qcnt_t) XFS_B_TO_FSB(mp, newlim->d_rt_spc_hardlimit) :
			dqp->q_rtb.hardlimit;
	soft = (newlim->d_fieldmask & QC_RT_SPC_SOFT) ?
		(xfs_qcnt_t) XFS_B_TO_FSB(mp, newlim->d_rt_spc_softlimit) :
			dqp->q_rtb.softlimit;
	res = &dqp->q_rtb;
	qlim = id == 0 ? &defq->rtb : NULL;

	xfs_setqlim_limits(mp, res, qlim, hard, soft, "rtb");
	if (newlim->d_fieldmask & QC_RT_SPC_WARNS)
		xfs_setqlim_warns(res, qlim, newlim->d_rt_spc_warns);
	if (newlim->d_fieldmask & QC_RT_SPC_TIMER)
		xfs_setqlim_timer(mp, res, qlim, newlim->d_rt_spc_timer);

	/* Inodes */
	hard = (newlim->d_fieldmask & QC_INO_HARD) ?
		(xfs_qcnt_t) newlim->d_ino_hardlimit :
			dqp->q_ino.hardlimit;
	soft = (newlim->d_fieldmask & QC_INO_SOFT) ?
		(xfs_qcnt_t) newlim->d_ino_softlimit :
			dqp->q_ino.softlimit;
	res = &dqp->q_ino;
	qlim = id == 0 ? &defq->ino : NULL;

	xfs_setqlim_limits(mp, res, qlim, hard, soft, "ino");
	if (newlim->d_fieldmask & QC_INO_WARNS)
		xfs_setqlim_warns(res, qlim, newlim->d_ino_warns);
	if (newlim->d_fieldmask & QC_INO_TIMER)
		xfs_setqlim_timer(mp, res, qlim, newlim->d_ino_timer);

	if (id != 0) {
		/*
		 * If the user is now over quota, start the timelimit.
		 * The user will not be 'warned'.
		 * Note that we keep the timers ticking, whether enforcement
		 * is on or off. We don't really want to bother with iterating
		 * over all ondisk dquots and turning the timers on/off.
		 */
		xfs_qm_adjust_dqtimers(dqp);
	}
	dqp->q_flags |= XFS_DQFLAG_DIRTY;
	xfs_trans_log_dquot(tp, dqp);

	error = xfs_trans_commit(tp);

out_rele:
	xfs_qm_dqrele(dqp);
out_unlock:
	mutex_unlock(&q->qi_quotaofflock);
	return error;
}

/* Fill out the quota context. */
static void
xfs_qm_scall_getquota_fill_qc(
	struct xfs_mount	*mp,
	xfs_dqtype_t		type,
	const struct xfs_dquot	*dqp,
	struct qc_dqblk		*dst)
{
	memset(dst, 0, sizeof(*dst));
	dst->d_spc_hardlimit = XFS_FSB_TO_B(mp, dqp->q_blk.hardlimit);
	dst->d_spc_softlimit = XFS_FSB_TO_B(mp, dqp->q_blk.softlimit);
	dst->d_ino_hardlimit = dqp->q_ino.hardlimit;
	dst->d_ino_softlimit = dqp->q_ino.softlimit;
	dst->d_space = XFS_FSB_TO_B(mp, dqp->q_blk.reserved);
	dst->d_ino_count = dqp->q_ino.reserved;
	dst->d_spc_timer = dqp->q_blk.timer;
	dst->d_ino_timer = dqp->q_ino.timer;
	dst->d_ino_warns = dqp->q_ino.warnings;
	dst->d_spc_warns = dqp->q_blk.warnings;
	dst->d_rt_spc_hardlimit = XFS_FSB_TO_B(mp, dqp->q_rtb.hardlimit);
	dst->d_rt_spc_softlimit = XFS_FSB_TO_B(mp, dqp->q_rtb.softlimit);
	dst->d_rt_space = XFS_FSB_TO_B(mp, dqp->q_rtb.reserved);
	dst->d_rt_spc_timer = dqp->q_rtb.timer;
	dst->d_rt_spc_warns = dqp->q_rtb.warnings;

	/*
	 * Internally, we don't reset all the timers when quota enforcement
	 * gets turned off. No need to confuse the user level code,
	 * so return zeroes in that case.
	 */
	if (!xfs_dquot_is_enforced(dqp)) {
		dst->d_spc_timer = 0;
		dst->d_ino_timer = 0;
		dst->d_rt_spc_timer = 0;
	}

#ifdef DEBUG
	if (xfs_dquot_is_enforced(dqp) && dqp->q_id != 0) {
		if ((dst->d_space > dst->d_spc_softlimit) &&
		    (dst->d_spc_softlimit > 0)) {
			ASSERT(dst->d_spc_timer != 0);
		}
		if ((dst->d_ino_count > dqp->q_ino.softlimit) &&
		    (dqp->q_ino.softlimit > 0)) {
			ASSERT(dst->d_ino_timer != 0);
		}
	}
#endif
}

/* Return the quota information for the dquot matching id. */
int
xfs_qm_scall_getquota(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	struct qc_dqblk		*dst)
{
	struct xfs_dquot	*dqp;
	int			error;

	/*
	 * Try to get the dquot. We don't want it allocated on disk, so don't
	 * set doalloc. If it doesn't exist, we'll get ENOENT back.
	 */
	error = xfs_qm_dqget(mp, id, type, false, &dqp);
	if (error)
		return error;

	/*
	 * If everything's NULL, this dquot doesn't quite exist as far as
	 * our utility programs are concerned.
	 */
	if (XFS_IS_DQUOT_UNINITIALIZED(dqp)) {
		error = -ENOENT;
		goto out_put;
	}

	xfs_qm_scall_getquota_fill_qc(mp, type, dqp, dst);

out_put:
	xfs_qm_dqput(dqp);
	return error;
}

/*
 * Return the quota information for the first initialized dquot whose id
 * is at least as high as id.
 */
int
xfs_qm_scall_getquota_next(
	struct xfs_mount	*mp,
	xfs_dqid_t		*id,
	xfs_dqtype_t		type,
	struct qc_dqblk		*dst)
{
	struct xfs_dquot	*dqp;
	int			error;

	error = xfs_qm_dqget_next(mp, *id, type, &dqp);
	if (error)
		return error;

	/* Fill in the ID we actually read from disk */
	*id = dqp->q_id;

	xfs_qm_scall_getquota_fill_qc(mp, type, dqp, dst);

	xfs_qm_dqput(dqp);
	return error;
}
