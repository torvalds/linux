/*
 * Copyright (c) 2008, Christoph Hellwig
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
#include "xfs_dmapi.h"
#include "xfs_sb.h"
#include "xfs_inum.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_quota.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "quota/xfs_qm.h"
#include <linux/quota.h>


STATIC int
xfs_quota_type(int type)
{
	switch (type) {
	case USRQUOTA:
		return XFS_DQ_USER;
	case GRPQUOTA:
		return XFS_DQ_GROUP;
	default:
		return XFS_DQ_PROJ;
	}
}

STATIC int
xfs_fs_quota_sync(
	struct super_block	*sb,
	int			type)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb->s_flags & MS_RDONLY)
		return -EROFS;
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	return -xfs_sync_data(mp, 0);
}

STATIC int
xfs_fs_get_xstate(
	struct super_block	*sb,
	struct fs_quota_stat	*fqs)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	return -xfs_qm_scall_getqstat(mp, fqs);
}

STATIC int
xfs_fs_set_xstate(
	struct super_block	*sb,
	unsigned int		uflags,
	int			op)
{
	struct xfs_mount	*mp = XFS_M(sb);
	unsigned int		flags = 0;

	if (sb->s_flags & MS_RDONLY)
		return -EROFS;
	if (op != Q_XQUOTARM && !XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (uflags & XFS_QUOTA_UDQ_ACCT)
		flags |= XFS_UQUOTA_ACCT;
	if (uflags & XFS_QUOTA_PDQ_ACCT)
		flags |= XFS_PQUOTA_ACCT;
	if (uflags & XFS_QUOTA_GDQ_ACCT)
		flags |= XFS_GQUOTA_ACCT;
	if (uflags & XFS_QUOTA_UDQ_ENFD)
		flags |= XFS_UQUOTA_ENFD;
	if (uflags & (XFS_QUOTA_PDQ_ENFD|XFS_QUOTA_GDQ_ENFD))
		flags |= XFS_OQUOTA_ENFD;

	switch (op) {
	case Q_XQUOTAON:
		return -xfs_qm_scall_quotaon(mp, flags);
	case Q_XQUOTAOFF:
		if (!XFS_IS_QUOTA_ON(mp))
			return -EINVAL;
		return -xfs_qm_scall_quotaoff(mp, flags);
	case Q_XQUOTARM:
		if (XFS_IS_QUOTA_ON(mp))
			return -EINVAL;
		return -xfs_qm_scall_trunc_qfiles(mp, flags);
	}

	return -EINVAL;
}

STATIC int
xfs_fs_get_xquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;

	return -xfs_qm_scall_getquota(mp, id, xfs_quota_type(type), fdq);
}

STATIC int
xfs_fs_set_xquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb->s_flags & MS_RDONLY)
		return -EROFS;
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return -xfs_qm_scall_setqlim(mp, id, xfs_quota_type(type), fdq);
}

const struct quotactl_ops xfs_quotactl_operations = {
	.quota_sync		= xfs_fs_quota_sync,
	.get_xstate		= xfs_fs_get_xstate,
	.set_xstate		= xfs_fs_set_xstate,
	.get_xquota		= xfs_fs_get_xquota,
	.set_xquota		= xfs_fs_set_xquota,
};
