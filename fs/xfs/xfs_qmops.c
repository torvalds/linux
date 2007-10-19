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
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_quota.h"
#include "xfs_error.h"
#include "xfs_clnt.h"


STATIC struct xfs_dquot *
xfs_dqvopchown_default(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_dquot	**dqp,
	struct xfs_dquot	*dq)
{
	return NULL;
}

/*
 * Clear the quotaflags in memory and in the superblock.
 */
int
xfs_mount_reset_sbqflags(xfs_mount_t *mp)
{
	int			error;
	xfs_trans_t		*tp;
	unsigned long		s;

	mp->m_qflags = 0;
	/*
	 * It is OK to look at sb_qflags here in mount path,
	 * without SB_LOCK.
	 */
	if (mp->m_sb.sb_qflags == 0)
		return 0;
	s = XFS_SB_LOCK(mp);
	mp->m_sb.sb_qflags = 0;
	XFS_SB_UNLOCK(mp, s);

	/*
	 * if the fs is readonly, let the incore superblock run
	 * with quotas off but don't flush the update out to disk
	 */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;
#ifdef QUOTADEBUG
	xfs_fs_cmn_err(CE_NOTE, mp, "Writing superblock quota changes");
#endif
	tp = xfs_trans_alloc(mp, XFS_TRANS_QM_SBCHANGE);
	if ((error = xfs_trans_reserve(tp, 0, mp->m_sb.sb_sectsize + 128, 0, 0,
				      XFS_DEFAULT_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		xfs_fs_cmn_err(CE_ALERT, mp,
			"xfs_mount_reset_sbqflags: Superblock update failed!");
		return error;
	}
	xfs_mod_sb(tp, XFS_SB_QFLAGS);
	error = xfs_trans_commit(tp, 0);
	return error;
}

STATIC int
xfs_noquota_init(
	xfs_mount_t	*mp,
	uint		*needquotamount,
	uint		*quotaflags)
{
	int		error = 0;

	*quotaflags = 0;
	*needquotamount = B_FALSE;

	ASSERT(!XFS_IS_QUOTA_ON(mp));

	/*
	 * If a file system had quotas running earlier, but decided to
	 * mount without -o uquota/pquota/gquota options, revoke the
	 * quotachecked license.
	 */
	if (mp->m_sb.sb_qflags & XFS_ALL_QUOTA_ACCT) {
		cmn_err(CE_NOTE,
                        "XFS resetting qflags for filesystem %s",
                        mp->m_fsname);

		error = xfs_mount_reset_sbqflags(mp);
	}
	return error;
}

static struct xfs_qmops xfs_qmcore_stub = {
	.xfs_qminit		= (xfs_qminit_t) xfs_noquota_init,
	.xfs_qmdone		= (xfs_qmdone_t) fs_noerr,
	.xfs_qmmount		= (xfs_qmmount_t) fs_noerr,
	.xfs_qmunmount		= (xfs_qmunmount_t) fs_noerr,
	.xfs_dqrele		= (xfs_dqrele_t) fs_noerr,
	.xfs_dqattach		= (xfs_dqattach_t) fs_noerr,
	.xfs_dqdetach		= (xfs_dqdetach_t) fs_noerr,
	.xfs_dqpurgeall		= (xfs_dqpurgeall_t) fs_noerr,
	.xfs_dqvopalloc		= (xfs_dqvopalloc_t) fs_noerr,
	.xfs_dqvopcreate	= (xfs_dqvopcreate_t) fs_noerr,
	.xfs_dqvoprename	= (xfs_dqvoprename_t) fs_noerr,
	.xfs_dqvopchown		= xfs_dqvopchown_default,
	.xfs_dqvopchownresv	= (xfs_dqvopchownresv_t) fs_noerr,
	.xfs_dqstatvfs		= (xfs_dqstatvfs_t) fs_noval,
	.xfs_dqsync		= (xfs_dqsync_t) fs_noerr,
	.xfs_quotactl		= (xfs_quotactl_t) fs_nosys,
};

int
xfs_qmops_get(struct xfs_mount *mp, struct xfs_mount_args *args)
{
	if (args->flags & (XFSMNT_UQUOTA | XFSMNT_PQUOTA | XFSMNT_GQUOTA)) {
#ifdef CONFIG_XFS_QUOTA
		mp->m_qm_ops = &xfs_qmcore_xfs;
#else
		cmn_err(CE_WARN,
			"XFS: qouta support not available in this kernel.");
		return EINVAL;
#endif
	} else {
		mp->m_qm_ops = &xfs_qmcore_stub;
	}

	return 0;
}

void
xfs_qmops_put(struct xfs_mount *mp)
{
}
