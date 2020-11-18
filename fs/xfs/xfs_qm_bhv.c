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
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_qm.h"


STATIC void
xfs_fill_statvfs_from_dquot(
	struct kstatfs		*statp,
	struct xfs_dquot	*dqp)
{
	uint64_t		limit;

	limit = dqp->q_blk.softlimit ?
		dqp->q_blk.softlimit :
		dqp->q_blk.hardlimit;
	if (limit && statp->f_blocks > limit) {
		statp->f_blocks = limit;
		statp->f_bfree = statp->f_bavail =
			(statp->f_blocks > dqp->q_blk.reserved) ?
			 (statp->f_blocks - dqp->q_blk.reserved) : 0;
	}

	limit = dqp->q_ino.softlimit ?
		dqp->q_ino.softlimit :
		dqp->q_ino.hardlimit;
	if (limit && statp->f_files > limit) {
		statp->f_files = limit;
		statp->f_ffree =
			(statp->f_files > dqp->q_ino.reserved) ?
			 (statp->f_files - dqp->q_ino.reserved) : 0;
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

	if (!xfs_qm_dqget(mp, ip->i_d.di_projid, XFS_DQTYPE_PROJ, false, &dqp)) {
		xfs_fill_statvfs_from_dquot(statp, dqp);
		xfs_qm_dqput(dqp);
	}
}

int
xfs_qm_newmount(
	xfs_mount_t	*mp,
	uint		*needquotamount,
	uint		*quotaflags)
{
	uint		quotaondisk;
	uint		uquotaondisk = 0, gquotaondisk = 0, pquotaondisk = 0;

	quotaondisk = xfs_sb_version_hasquota(&mp->m_sb) &&
				(mp->m_sb.sb_qflags & XFS_ALL_QUOTA_ACCT);

	if (quotaondisk) {
		uquotaondisk = mp->m_sb.sb_qflags & XFS_UQUOTA_ACCT;
		pquotaondisk = mp->m_sb.sb_qflags & XFS_PQUOTA_ACCT;
		gquotaondisk = mp->m_sb.sb_qflags & XFS_GQUOTA_ACCT;
	}

	/*
	 * If the device itself is read-only, we can't allow
	 * the user to change the state of quota on the mount -
	 * this would generate a transaction on the ro device,
	 * which would lead to an I/O error and shutdown
	 */

	if (((uquotaondisk && !XFS_IS_UQUOTA_ON(mp)) ||
	    (!uquotaondisk &&  XFS_IS_UQUOTA_ON(mp)) ||
	     (gquotaondisk && !XFS_IS_GQUOTA_ON(mp)) ||
	    (!gquotaondisk &&  XFS_IS_GQUOTA_ON(mp)) ||
	     (pquotaondisk && !XFS_IS_PQUOTA_ON(mp)) ||
	    (!pquotaondisk &&  XFS_IS_PQUOTA_ON(mp)))  &&
	    xfs_dev_is_read_only(mp, "changing quota state")) {
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
