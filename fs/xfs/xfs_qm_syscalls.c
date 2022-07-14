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

int
xfs_qm_scall_quotaoff(
	xfs_mount_t		*mp,
	uint			flags)
{
	/*
	 * No file system can have quotas enabled on disk but not in core.
	 * Note that quota utilities (like quotaoff) _expect_
	 * errno == -EEXIST here.
	 */
	if ((mp->m_qflags & flags) == 0)
		return -EEXIST;

	/*
	 * We do not support actually turning off quota accounting any more.
	 * Just log a warning and ignore the accounting related flags.
	 */
	if (flags & XFS_ALL_QUOTA_ACCT)
		xfs_info(mp, "disabling of quota accounting not supported.");

	mutex_lock(&mp->m_quotainfo->qi_quotaofflock);
	mp->m_qflags &= ~(flags & XFS_ALL_QUOTA_ENFD);
	spin_lock(&mp->m_sb_lock);
	mp->m_sb.sb_qflags = mp->m_qflags;
	spin_unlock(&mp->m_sb_lock);
	mutex_unlock(&mp->m_quotainfo->qi_quotaofflock);

	/* XXX what to do if error ? Revert back to old vals incore ? */
	return xfs_sync_sb(mp, false);
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

	if (!xfs_has_quota(mp) || flags == 0 ||
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

	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;

	/*
	 * Switch on quota enforcement in core.
	 */
	mutex_lock(&mp->m_quotainfo->qi_quotaofflock);
	mp->m_qflags |= (flags & XFS_ALL_QUOTA_ENFD);
	mutex_unlock(&mp->m_quotainfo->qi_quotaofflock);

	return 0;
}

#define XFS_QC_MASK (QC_LIMIT_MASK | QC_TIMER_MASK)

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
	 * Get the dquot (locked) before we start, as we need to do a
	 * transaction to allocate it if it doesn't exist. Once we have the
	 * dquot, unlock it so we can start the next transaction safely. We hold
	 * a reference to the dquot, so it's safe to do this unlock/lock without
	 * it being reclaimed in the mean time.
	 */
	error = xfs_qm_dqget(mp, id, type, true, &dqp);
	if (error) {
		ASSERT(error != -ENOENT);
		return error;
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
	dst->d_ino_warns = 0;
	dst->d_spc_warns = 0;
	dst->d_rt_spc_hardlimit = XFS_FSB_TO_B(mp, dqp->q_rtb.hardlimit);
	dst->d_rt_spc_softlimit = XFS_FSB_TO_B(mp, dqp->q_rtb.softlimit);
	dst->d_rt_space = XFS_FSB_TO_B(mp, dqp->q_rtb.reserved);
	dst->d_rt_spc_timer = dqp->q_rtb.timer;
	dst->d_rt_spc_warns = 0;

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
	 * Expedite pending inodegc work at the start of a quota reporting
	 * scan but don't block waiting for it to complete.
	 */
	if (id == 0)
		xfs_inodegc_push(mp);

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

	/* Flush inodegc work at the start of a quota reporting scan. */
	if (*id == 0)
		xfs_inodegc_push(mp);

	error = xfs_qm_dqget_next(mp, *id, type, &dqp);
	if (error)
		return error;

	/* Fill in the ID we actually read from disk */
	*id = dqp->q_id;

	xfs_qm_scall_getquota_fill_qc(mp, type, dqp, dst);

	xfs_qm_dqput(dqp);
	return error;
}
