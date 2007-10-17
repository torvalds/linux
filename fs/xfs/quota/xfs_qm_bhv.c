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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_qm.h"


STATIC void
xfs_fill_statvfs_from_dquot(
	bhv_statvfs_t		*statp,
	xfs_disk_dquot_t	*dp)
{
	__uint64_t		limit;

	limit = dp->d_blk_softlimit ?
		be64_to_cpu(dp->d_blk_softlimit) :
		be64_to_cpu(dp->d_blk_hardlimit);
	if (limit && statp->f_blocks > limit) {
		statp->f_blocks = limit;
		statp->f_bfree =
			(statp->f_blocks > be64_to_cpu(dp->d_bcount)) ?
			 (statp->f_blocks - be64_to_cpu(dp->d_bcount)) : 0;
	}

	limit = dp->d_ino_softlimit ?
		be64_to_cpu(dp->d_ino_softlimit) :
		be64_to_cpu(dp->d_ino_hardlimit);
	if (limit && statp->f_files > limit) {
		statp->f_files = limit;
		statp->f_ffree =
			(statp->f_files > be64_to_cpu(dp->d_icount)) ?
			 (statp->f_ffree - be64_to_cpu(dp->d_icount)) : 0;
	}
}


/*
 * Directory tree accounting is implemented using project quotas, where
 * the project identifier is inherited from parent directories.
 * A statvfs (df, etc.) of a directory that is using project quota should
 * return a statvfs of the project, not the entire filesystem.
 * This makes such trees appear as if they are filesystems in themselves.
 */
STATIC void
xfs_qm_statvfs(
	xfs_inode_t		*ip,
	bhv_statvfs_t		*statp)
{
	xfs_mount_t		*mp = ip->i_mount;
	xfs_dquot_t		*dqp;

	if (!(ip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) ||
	    !((mp->m_qflags & (XFS_PQUOTA_ACCT|XFS_OQUOTA_ENFD))) ==
	    		      (XFS_PQUOTA_ACCT|XFS_OQUOTA_ENFD))
		return;

	if (!xfs_qm_dqget(mp, NULL, ip->i_d.di_projid, XFS_DQ_PROJ, 0, &dqp)) {
		xfs_disk_dquot_t	*dp = &dqp->q_core;

		xfs_fill_statvfs_from_dquot(statp, dp);
		xfs_qm_dqput(dqp);
	}
}

STATIC int
xfs_qm_newmount(
	xfs_mount_t	*mp,
	uint		*needquotamount,
	uint		*quotaflags)
{
	uint		quotaondisk;
	uint		uquotaondisk = 0, gquotaondisk = 0, pquotaondisk = 0;

	*quotaflags = 0;
	*needquotamount = B_FALSE;

	quotaondisk = XFS_SB_VERSION_HASQUOTA(&mp->m_sb) &&
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
	     (pquotaondisk && !XFS_IS_PQUOTA_ON(mp)) ||
	    (!pquotaondisk &&  XFS_IS_PQUOTA_ON(mp)) ||
	     (gquotaondisk && !XFS_IS_GQUOTA_ON(mp)) ||
	    (!gquotaondisk &&  XFS_IS_OQUOTA_ON(mp)))  &&
	    xfs_dev_is_read_only(mp, "changing quota state")) {
		cmn_err(CE_WARN,
			"XFS: please mount with%s%s%s%s.",
			(!quotaondisk ? "out quota" : ""),
			(uquotaondisk ? " usrquota" : ""),
			(pquotaondisk ? " prjquota" : ""),
			(gquotaondisk ? " grpquota" : ""));
		return XFS_ERROR(EPERM);
	}

	if (XFS_IS_QUOTA_ON(mp) || quotaondisk) {
		/*
		 * Call mount_quotas at this point only if we won't have to do
		 * a quotacheck.
		 */
		if (quotaondisk && !XFS_QM_NEED_QUOTACHECK(mp)) {
			/*
			 * If an error occured, qm_mount_quotas code
			 * has already disabled quotas. So, just finish
			 * mounting, and get on with the boring life
			 * without disk quotas.
			 */
			xfs_qm_mount_quotas(mp, 0);
		} else {
			/*
			 * Clear the quota flags, but remember them. This
			 * is so that the quota code doesn't get invoked
			 * before we're ready. This can happen when an
			 * inode goes inactive and wants to free blocks,
			 * or via xfs_log_mount_finish.
			 */
			*needquotamount = B_TRUE;
			*quotaflags = mp->m_qflags;
			mp->m_qflags = 0;
		}
	}

	return 0;
}

STATIC int
xfs_qm_endmount(
	xfs_mount_t	*mp,
	uint		needquotamount,
	uint		quotaflags,
	int		mfsi_flags)
{
	if (needquotamount) {
		ASSERT(mp->m_qflags == 0);
		mp->m_qflags = quotaflags;
		xfs_qm_mount_quotas(mp, mfsi_flags);
	}

#if defined(DEBUG) && defined(XFS_LOUD_RECOVERY)
	if (! (XFS_IS_QUOTA_ON(mp)))
		xfs_fs_cmn_err(CE_NOTE, mp, "Disk quotas not turned on");
	else
		xfs_fs_cmn_err(CE_NOTE, mp, "Disk quotas turned on");
#endif

#ifdef QUOTADEBUG
	if (XFS_IS_QUOTA_ON(mp) && xfs_qm_internalqcheck(mp))
		cmn_err(CE_WARN, "XFS: mount internalqcheck failed");
#endif

	return 0;
}

STATIC void
xfs_qm_dqrele_null(
	xfs_dquot_t	*dq)
{
	/*
	 * Called from XFS, where we always check first for a NULL dquot.
	 */
	if (!dq)
		return;
	xfs_qm_dqrele(dq);
}


struct xfs_qmops xfs_qmcore_xfs = {
	.xfs_qminit		= xfs_qm_newmount,
	.xfs_qmdone		= xfs_qm_unmount_quotadestroy,
	.xfs_qmmount		= xfs_qm_endmount,
	.xfs_qmunmount		= xfs_qm_unmount_quotas,
	.xfs_dqrele		= xfs_qm_dqrele_null,
	.xfs_dqattach		= xfs_qm_dqattach,
	.xfs_dqdetach		= xfs_qm_dqdetach,
	.xfs_dqpurgeall		= xfs_qm_dqpurge_all,
	.xfs_dqvopalloc		= xfs_qm_vop_dqalloc,
	.xfs_dqvopcreate	= xfs_qm_vop_dqattach_and_dqmod_newinode,
	.xfs_dqvoprename	= xfs_qm_vop_rename_dqattach,
	.xfs_dqvopchown		= xfs_qm_vop_chown,
	.xfs_dqvopchownresv	= xfs_qm_vop_chown_reserve,
	.xfs_dqstatvfs		= xfs_qm_statvfs,
	.xfs_dqsync		= xfs_qm_sync,
	.xfs_quotactl		= xfs_qm_quotactl,
	.xfs_dqtrxops		= &xfs_trans_dquot_ops,
};
EXPORT_SYMBOL(xfs_qmcore_xfs);

void __init
xfs_qm_init(void)
{
	printk(KERN_INFO "SGI XFS Quota Management subsystem\n");
	mutex_init(&xfs_Gqm_lock);
	xfs_qm_init_procfs();
}

void __exit
xfs_qm_exit(void)
{
	xfs_qm_cleanup_procfs();
	if (qm_dqzone)
		kmem_zone_destroy(qm_dqzone);
	if (qm_dqtrxzone)
		kmem_zone_destroy(qm_dqtrxzone);
}
