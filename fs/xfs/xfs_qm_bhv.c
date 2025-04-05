// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_quota.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_qm.h"


STATIC void
xfs_fill_statvfs_from_dquot(
	struct kstatfs		*statp,
	struct xfs_inode	*ip,
	struct xfs_dquot	*dqp)
{
	struct xfs_dquot_res	*blkres = &dqp->q_blk;
	uint64_t		limit;

	if (XFS_IS_REALTIME_MOUNT(ip->i_mount) &&
	    (ip->i_diflags & (XFS_DIFLAG_RTINHERIT | XFS_DIFLAG_REALTIME)))
		blkres = &dqp->q_rtb;

	limit = blkres->softlimit ?
		blkres->softlimit :
		blkres->hardlimit;
	if (limit) {
		uint64_t	remaining = 0;

		if (limit > blkres->reserved)
			remaining = limit - blkres->reserved;

		statp->f_blocks = min(statp->f_blocks, limit);
		statp->f_bfree = min(statp->f_bfree, remaining);
	}

	limit = dqp->q_ino.softlimit ?
		dqp->q_ino.softlimit :
		dqp->q_ino.hardlimit;
	if (limit) {
		uint64_t	remaining = 0;

		if (limit > dqp->q_ino.reserved)
			remaining = limit - dqp->q_ino.reserved;

		statp->f_files = min(statp->f_files, limit);
		statp->f_ffree = min(statp->f_ffree, remaining);
	}
}


/*
 * Directory tree accounting is implemented using project quotas, where
 * the project identifier is inherited from parent directories.
 * A statvfs (df, etc.) of a directory that is using project quota should
 * return a statvfs of the project, not the entire filesystem.
 * This makes such trees appear as if they are filesystems in themselves.
 */
void
xfs_qm_statvfs(
	struct xfs_inode	*ip,
	struct kstatfs		*statp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_dquot	*dqp;

	if (!xfs_qm_dqget(mp, ip->i_projid, XFS_DQTYPE_PROJ, false, &dqp)) {
		xfs_fill_statvfs_from_dquot(statp, ip, dqp);
		xfs_qm_dqput(dqp);
	}
}

STATIC int
xfs_qm_validate_state_change(
	struct xfs_mount	*mp,
	uint			uqd,
	uint			gqd,
	uint			pqd)
{
	int state;

	/* Is quota state changing? */
	state = ((uqd && !XFS_IS_UQUOTA_ON(mp)) ||
		(!uqd &&  XFS_IS_UQUOTA_ON(mp)) ||
		 (gqd && !XFS_IS_GQUOTA_ON(mp)) ||
		(!gqd &&  XFS_IS_GQUOTA_ON(mp)) ||
		 (pqd && !XFS_IS_PQUOTA_ON(mp)) ||
		(!pqd &&  XFS_IS_PQUOTA_ON(mp)));

	return  state &&
		(xfs_dev_is_read_only(mp, "changing quota state") ||
		xfs_has_norecovery(mp));
}

int
xfs_qm_newmount(
	xfs_mount_t	*mp,
	uint		*needquotamount,
	uint		*quotaflags)
{
	uint		quotaondisk;
	uint		uquotaondisk = 0, gquotaondisk = 0, pquotaondisk = 0;

	quotaondisk = xfs_has_quota(mp) &&
				(mp->m_sb.sb_qflags & XFS_ALL_QUOTA_ACCT);

	if (quotaondisk) {
		uquotaondisk = mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT;
		pquotaondisk = mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT;
		gquotaondisk = mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT;
	}

	/*
	 * If the device itself is read-only and/or in norecovery
	 * mode, we can't allow the user to change the state of
	 * quota on the mount - this would generate a transaction
	 * on the ro device, which would lead to an I/O error and
	 * shutdown.
	 */

	if (xfs_qm_validate_state_change(mp, uquotaondisk,
			    gquotaondisk, pquotaondisk)) {

		if (xfs_has_metadir(mp))
			xfs_warn(mp,
		"metadir enabled, please mount without any quota mount options");
		else
			xfs_warn(mp, "please mount with%s%s%s%s.",
				(!quotaondisk ? "out quota" : ""),
				(uquotaondisk ? " usrquota" : ""),
				(gquotaondisk ? " grpquota" : ""),
				(pquotaondisk ? " prjquota" : ""));
		return -EPERM;
	}

	if (XFS_IS_QUOTA_ON(mp) || quotaondisk) {
		/*
		 * Call mount_quotas at this point only if we won't have to do
		 * a quotacheck.
		 */
		if (quotaondisk && !XFS_QM_NEED_QUOTACHECK(mp)) {
			/*
			 * If an error occurred, qm_mount_quotas code
			 * has already disabled quotas. So, just finish
			 * mounting, and get on with the boring life
			 * without disk quotas.
			 */
			xfs_qm_mount_quotas(mp);
		} else {
			/*
			 * Clear the quota flags, but remember them. This
			 * is so that the quota code doesn't get invoked
			 * before we're ready. This can happen when an
			 * inode goes inactive and wants to free blocks,
			 * or via xfs_log_mount_finish.
			 */
			*needquotamount = true;
			*quotaflags = mp->m_qflags;
			mp->m_qflags = 0;
		}
	}

	return 0;
}

/*
 * If the sysadmin didn't provide any quota mount options, restore the quota
 * accounting and enforcement state from the ondisk superblock.  Only do this
 * for metadir filesystems because this is a behavior change.
 */
void
xfs_qm_resume_quotaon(
	struct xfs_mount	*mp)
{
	if (!xfs_has_metadir(mp))
		return;
	if (xfs_has_norecovery(mp))
		return;

	mp->m_qflags = mp->m_sb.sb_qflags & (XFS_ALL_QUOTA_ACCT |
					     XFS_ALL_QUOTA_ENFD);
}
