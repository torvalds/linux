// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008, Christoph Hellwig
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_quota.h"
#include "xfs_trans.h"
#include "xfs_icache.h"
#include "xfs_qm.h"


static void
xfs_qm_fill_state(
	struct qc_type_state	*tstate,
	struct xfs_mount	*mp,
	struct xfs_inode	*ip,
	xfs_ino_t		ino)
{
	struct xfs_quotainfo *q = mp->m_quotainfo;
	bool tempqip = false;

	tstate->ino = ino;
	if (!ip && ino == NULLFSINO)
		return;
	if (!ip) {
		if (xfs_iget(mp, NULL, ino, 0, 0, &ip))
			return;
		tempqip = true;
	}
	tstate->flags |= QCI_SYSFILE;
	tstate->blocks = ip->i_d.di_nblocks;
	tstate->nextents = ip->i_d.di_nextents;
	tstate->spc_timelimit = (u32)q->qi_btimelimit;
	tstate->ino_timelimit = (u32)q->qi_itimelimit;
	tstate->rt_spc_timelimit = (u32)q->qi_rtbtimelimit;
	tstate->spc_warnlimit = q->qi_bwarnlimit;
	tstate->ino_warnlimit = q->qi_iwarnlimit;
	tstate->rt_spc_warnlimit = q->qi_rtbwarnlimit;
	if (tempqip)
		xfs_irele(ip);
}

/*
 * Return quota status information, such as enforcements, quota file inode
 * numbers etc.
 */
static int
xfs_fs_get_quota_state(
	struct super_block	*sb,
	struct qc_state		*state)
{
	struct xfs_mount *mp = XFS_M(sb);
	struct xfs_quotainfo *q = mp->m_quotainfo;

	memset(state, 0, sizeof(*state));
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return 0;
	state->s_incoredqs = q->qi_dquots;
	if (XFS_IS_UQUOTA_RUNNING(mp))
		state->s_state[USRQUOTA].flags |= QCI_ACCT_ENABLED;
	if (XFS_IS_UQUOTA_ENFORCED(mp))
		state->s_state[USRQUOTA].flags |= QCI_LIMITS_ENFORCED;
	if (XFS_IS_GQUOTA_RUNNING(mp))
		state->s_state[GRPQUOTA].flags |= QCI_ACCT_ENABLED;
	if (XFS_IS_GQUOTA_ENFORCED(mp))
		state->s_state[GRPQUOTA].flags |= QCI_LIMITS_ENFORCED;
	if (XFS_IS_PQUOTA_RUNNING(mp))
		state->s_state[PRJQUOTA].flags |= QCI_ACCT_ENABLED;
	if (XFS_IS_PQUOTA_ENFORCED(mp))
		state->s_state[PRJQUOTA].flags |= QCI_LIMITS_ENFORCED;

	xfs_qm_fill_state(&state->s_state[USRQUOTA], mp, q->qi_uquotaip,
			  mp->m_sb.sb_uquotino);
	xfs_qm_fill_state(&state->s_state[GRPQUOTA], mp, q->qi_gquotaip,
			  mp->m_sb.sb_gquotino);
	xfs_qm_fill_state(&state->s_state[PRJQUOTA], mp, q->qi_pquotaip,
			  mp->m_sb.sb_pquotino);
	return 0;
}

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

#define XFS_QC_SETINFO_MASK (QC_TIMER_MASK | QC_WARNS_MASK)

/*
 * Adjust quota timers & warnings
 */
static int
xfs_fs_set_info(
	struct super_block	*sb,
	int			type,
	struct qc_info		*info)
{
	struct xfs_mount *mp = XFS_M(sb);
	struct qc_dqblk newlim;

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;
	if (info->i_fieldmask & ~XFS_QC_SETINFO_MASK)
		return -EINVAL;
	if ((info->i_fieldmask & XFS_QC_SETINFO_MASK) == 0)
		return 0;

	newlim.d_fieldmask = info->i_fieldmask;
	newlim.d_spc_timer = info->i_spc_timelimit;
	newlim.d_ino_timer = info->i_ino_timelimit;
	newlim.d_rt_spc_timer = info->i_rt_spc_timelimit;
	newlim.d_ino_warns = info->i_ino_warnlimit;
	newlim.d_spc_warns = info->i_spc_warnlimit;
	newlim.d_rt_spc_warns = info->i_rt_spc_warnlimit;

	return xfs_qm_scall_setqlim(mp, 0, xfs_quota_type(type), &newlim);
}

static unsigned int
xfs_quota_flags(unsigned int uflags)
{
	unsigned int flags = 0;

	if (uflags & FS_QUOTA_UDQ_ACCT)
		flags |= XFS_UQUOTA_ACCT;
	if (uflags & FS_QUOTA_PDQ_ACCT)
		flags |= XFS_PQUOTA_ACCT;
	if (uflags & FS_QUOTA_GDQ_ACCT)
		flags |= XFS_GQUOTA_ACCT;
	if (uflags & FS_QUOTA_UDQ_ENFD)
		flags |= XFS_UQUOTA_ENFD;
	if (uflags & FS_QUOTA_GDQ_ENFD)
		flags |= XFS_GQUOTA_ENFD;
	if (uflags & FS_QUOTA_PDQ_ENFD)
		flags |= XFS_PQUOTA_ENFD;

	return flags;
}

STATIC int
xfs_quota_enable(
	struct super_block	*sb,
	unsigned int		uflags)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;

	return xfs_qm_scall_quotaon(mp, xfs_quota_flags(uflags));
}

STATIC int
xfs_quota_disable(
	struct super_block	*sb,
	unsigned int		uflags)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -EINVAL;

	return xfs_qm_scall_quotaoff(mp, xfs_quota_flags(uflags));
}

STATIC int
xfs_fs_rm_xquota(
	struct super_block	*sb,
	unsigned int		uflags)
{
	struct xfs_mount	*mp = XFS_M(sb);
	unsigned int		flags = 0;

	if (sb_rdonly(sb))
		return -EROFS;

	if (XFS_IS_QUOTA_ON(mp))
		return -EINVAL;

	if (uflags & ~(FS_USER_QUOTA | FS_GROUP_QUOTA | FS_PROJ_QUOTA))
		return -EINVAL;

	if (uflags & FS_USER_QUOTA)
		flags |= XFS_DQ_USER;
	if (uflags & FS_GROUP_QUOTA)
		flags |= XFS_DQ_GROUP;
	if (uflags & FS_PROJ_QUOTA)
		flags |= XFS_DQ_PROJ;

	return xfs_qm_scall_trunc_qfiles(mp, flags);
}

STATIC int
xfs_fs_get_dqblk(
	struct super_block	*sb,
	struct kqid		qid,
	struct qc_dqblk		*qdq)
{
	struct xfs_mount	*mp = XFS_M(sb);
	xfs_dqid_t		id;

	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;

	id = from_kqid(&init_user_ns, qid);
	return xfs_qm_scall_getquota(mp, id, xfs_quota_type(qid.type), qdq);
}

/* Return quota info for active quota >= this qid */
STATIC int
xfs_fs_get_nextdqblk(
	struct super_block	*sb,
	struct kqid		*qid,
	struct qc_dqblk		*qdq)
{
	int			ret;
	struct xfs_mount	*mp = XFS_M(sb);
	xfs_dqid_t		id;

	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;

	id = from_kqid(&init_user_ns, *qid);
	ret = xfs_qm_scall_getquota_next(mp, &id, xfs_quota_type(qid->type),
			qdq);
	if (ret)
		return ret;

	/* ID may be different, so convert back what we got */
	*qid = make_kqid(current_user_ns(), qid->type, id);
	return 0;
}

STATIC int
xfs_fs_set_dqblk(
	struct super_block	*sb,
	struct kqid		qid,
	struct qc_dqblk		*qdq)
{
	struct xfs_mount	*mp = XFS_M(sb);

	if (sb_rdonly(sb))
		return -EROFS;
	if (!XFS_IS_QUOTA_RUNNING(mp))
		return -ENOSYS;
	if (!XFS_IS_QUOTA_ON(mp))
		return -ESRCH;

	return xfs_qm_scall_setqlim(mp, from_kqid(&init_user_ns, qid),
				     xfs_quota_type(qid.type), qdq);
}

const struct quotactl_ops xfs_quotactl_operations = {
	.get_state		= xfs_fs_get_quota_state,
	.set_info		= xfs_fs_set_info,
	.quota_enable		= xfs_quota_enable,
	.quota_disable		= xfs_quota_disable,
	.rm_xquota		= xfs_fs_rm_xquota,
	.get_dqblk		= xfs_fs_get_dqblk,
	.get_nextdqblk		= xfs_fs_get_nextdqblk,
	.set_dqblk		= xfs_fs_set_dqblk,
};
