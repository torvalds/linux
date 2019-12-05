// SPDX-License-Identifier: GPL-2.0
/*
 * Quota code necessary even when VFS quota support is not compiled
 * into the kernel.  The interesting stuff is over in dquot.c, here
 * we have symbols for initial quotactl(2) handling, the sysctl(2)
 * variables, etc - things needed even when quota support disabled.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/quotaops.h>
#include <linux/types.h>
#include <linux/writeback.h>
#include <linux/nospec.h>

static int check_quotactl_permission(struct super_block *sb, int type, int cmd,
				     qid_t id)
{
	switch (cmd) {
	/* these commands do not require any special privilegues */
	case Q_GETFMT:
	case Q_SYNC:
	case Q_GETINFO:
	case Q_XGETQSTAT:
	case Q_XGETQSTATV:
	case Q_XQUOTASYNC:
		break;
	/* allow to query information for dquots we "own" */
	case Q_GETQUOTA:
	case Q_XGETQUOTA:
		if ((type == USRQUOTA && uid_eq(current_euid(), make_kuid(current_user_ns(), id))) ||
		    (type == GRPQUOTA && in_egroup_p(make_kgid(current_user_ns(), id))))
			break;
		/*FALLTHROUGH*/
	default:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	}

	return security_quotactl(cmd, type, id, sb);
}

static void quota_sync_one(struct super_block *sb, void *arg)
{
	int type = *(int *)arg;

	if (sb->s_qcop && sb->s_qcop->quota_sync &&
	    (sb->s_quota_types & (1 << type)))
		sb->s_qcop->quota_sync(sb, type);
}

static int quota_sync_all(int type)
{
	int ret;

	ret = security_quotactl(Q_SYNC, type, 0, NULL);
	if (!ret)
		iterate_supers(quota_sync_one, &type);
	return ret;
}

unsigned int qtype_enforce_flag(int type)
{
	switch (type) {
	case USRQUOTA:
		return FS_QUOTA_UDQ_ENFD;
	case GRPQUOTA:
		return FS_QUOTA_GDQ_ENFD;
	case PRJQUOTA:
		return FS_QUOTA_PDQ_ENFD;
	}
	return 0;
}

static int quota_quotaon(struct super_block *sb, int type, qid_t id,
		         const struct path *path)
{
	if (!sb->s_qcop->quota_on && !sb->s_qcop->quota_enable)
		return -ENOSYS;
	if (sb->s_qcop->quota_enable)
		return sb->s_qcop->quota_enable(sb, qtype_enforce_flag(type));
	if (IS_ERR(path))
		return PTR_ERR(path);
	return sb->s_qcop->quota_on(sb, type, id, path);
}

static int quota_quotaoff(struct super_block *sb, int type)
{
	if (!sb->s_qcop->quota_off && !sb->s_qcop->quota_disable)
		return -ENOSYS;
	if (sb->s_qcop->quota_disable)
		return sb->s_qcop->quota_disable(sb, qtype_enforce_flag(type));
	return sb->s_qcop->quota_off(sb, type);
}

static int quota_getfmt(struct super_block *sb, int type, void __user *addr)
{
	__u32 fmt;

	if (!sb_has_quota_active(sb, type))
		return -ESRCH;
	fmt = sb_dqopt(sb)->info[type].dqi_format->qf_fmt_id;
	if (copy_to_user(addr, &fmt, sizeof(fmt)))
		return -EFAULT;
	return 0;
}

static int quota_getinfo(struct super_block *sb, int type, void __user *addr)
{
	struct qc_state state;
	struct qc_type_state *tstate;
	struct if_dqinfo uinfo;
	int ret;

	if (!sb->s_qcop->get_state)
		return -ENOSYS;
	ret = sb->s_qcop->get_state(sb, &state);
	if (ret)
		return ret;
	tstate = state.s_state + type;
	if (!(tstate->flags & QCI_ACCT_ENABLED))
		return -ESRCH;
	memset(&uinfo, 0, sizeof(uinfo));
	uinfo.dqi_bgrace = tstate->spc_timelimit;
	uinfo.dqi_igrace = tstate->ino_timelimit;
	if (tstate->flags & QCI_SYSFILE)
		uinfo.dqi_flags |= DQF_SYS_FILE;
	if (tstate->flags & QCI_ROOT_SQUASH)
		uinfo.dqi_flags |= DQF_ROOT_SQUASH;
	uinfo.dqi_valid = IIF_ALL;
	if (copy_to_user(addr, &uinfo, sizeof(uinfo)))
		return -EFAULT;
	return 0;
}

static int quota_setinfo(struct super_block *sb, int type, void __user *addr)
{
	struct if_dqinfo info;
	struct qc_info qinfo;

	if (copy_from_user(&info, addr, sizeof(info)))
		return -EFAULT;
	if (!sb->s_qcop->set_info)
		return -ENOSYS;
	if (info.dqi_valid & ~(IIF_FLAGS | IIF_BGRACE | IIF_IGRACE))
		return -EINVAL;
	memset(&qinfo, 0, sizeof(qinfo));
	if (info.dqi_valid & IIF_FLAGS) {
		if (info.dqi_flags & ~DQF_SETINFO_MASK)
			return -EINVAL;
		if (info.dqi_flags & DQF_ROOT_SQUASH)
			qinfo.i_flags |= QCI_ROOT_SQUASH;
		qinfo.i_fieldmask |= QC_FLAGS;
	}
	if (info.dqi_valid & IIF_BGRACE) {
		qinfo.i_spc_timelimit = info.dqi_bgrace;
		qinfo.i_fieldmask |= QC_SPC_TIMER;
	}
	if (info.dqi_valid & IIF_IGRACE) {
		qinfo.i_ino_timelimit = info.dqi_igrace;
		qinfo.i_fieldmask |= QC_INO_TIMER;
	}
	return sb->s_qcop->set_info(sb, type, &qinfo);
}

static inline qsize_t qbtos(qsize_t blocks)
{
	return blocks << QIF_DQBLKSIZE_BITS;
}

static inline qsize_t stoqb(qsize_t space)
{
	return (space + QIF_DQBLKSIZE - 1) >> QIF_DQBLKSIZE_BITS;
}

static void copy_to_if_dqblk(struct if_dqblk *dst, struct qc_dqblk *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->dqb_bhardlimit = stoqb(src->d_spc_hardlimit);
	dst->dqb_bsoftlimit = stoqb(src->d_spc_softlimit);
	dst->dqb_curspace = src->d_space;
	dst->dqb_ihardlimit = src->d_ino_hardlimit;
	dst->dqb_isoftlimit = src->d_ino_softlimit;
	dst->dqb_curinodes = src->d_ino_count;
	dst->dqb_btime = src->d_spc_timer;
	dst->dqb_itime = src->d_ino_timer;
	dst->dqb_valid = QIF_ALL;
}

static int quota_getquota(struct super_block *sb, int type, qid_t id,
			  void __user *addr)
{
	struct kqid qid;
	struct qc_dqblk fdq;
	struct if_dqblk idq;
	int ret;

	if (!sb->s_qcop->get_dqblk)
		return -ENOSYS;
	qid = make_kqid(current_user_ns(), type, id);
	if (!qid_has_mapping(sb->s_user_ns, qid))
		return -EINVAL;
	ret = sb->s_qcop->get_dqblk(sb, qid, &fdq);
	if (ret)
		return ret;
	copy_to_if_dqblk(&idq, &fdq);
	if (copy_to_user(addr, &idq, sizeof(idq)))
		return -EFAULT;
	return 0;
}

/*
 * Return quota for next active quota >= this id, if any exists,
 * otherwise return -ENOENT via ->get_nextdqblk
 */
static int quota_getnextquota(struct super_block *sb, int type, qid_t id,
			  void __user *addr)
{
	struct kqid qid;
	struct qc_dqblk fdq;
	struct if_nextdqblk idq;
	int ret;

	if (!sb->s_qcop->get_nextdqblk)
		return -ENOSYS;
	qid = make_kqid(current_user_ns(), type, id);
	if (!qid_has_mapping(sb->s_user_ns, qid))
		return -EINVAL;
	ret = sb->s_qcop->get_nextdqblk(sb, &qid, &fdq);
	if (ret)
		return ret;
	/* struct if_nextdqblk is a superset of struct if_dqblk */
	copy_to_if_dqblk((struct if_dqblk *)&idq, &fdq);
	idq.dqb_id = from_kqid(current_user_ns(), qid);
	if (copy_to_user(addr, &idq, sizeof(idq)))
		return -EFAULT;
	return 0;
}

static void copy_from_if_dqblk(struct qc_dqblk *dst, struct if_dqblk *src)
{
	dst->d_spc_hardlimit = qbtos(src->dqb_bhardlimit);
	dst->d_spc_softlimit = qbtos(src->dqb_bsoftlimit);
	dst->d_space = src->dqb_curspace;
	dst->d_ino_hardlimit = src->dqb_ihardlimit;
	dst->d_ino_softlimit = src->dqb_isoftlimit;
	dst->d_ino_count = src->dqb_curinodes;
	dst->d_spc_timer = src->dqb_btime;
	dst->d_ino_timer = src->dqb_itime;

	dst->d_fieldmask = 0;
	if (src->dqb_valid & QIF_BLIMITS)
		dst->d_fieldmask |= QC_SPC_SOFT | QC_SPC_HARD;
	if (src->dqb_valid & QIF_SPACE)
		dst->d_fieldmask |= QC_SPACE;
	if (src->dqb_valid & QIF_ILIMITS)
		dst->d_fieldmask |= QC_INO_SOFT | QC_INO_HARD;
	if (src->dqb_valid & QIF_INODES)
		dst->d_fieldmask |= QC_INO_COUNT;
	if (src->dqb_valid & QIF_BTIME)
		dst->d_fieldmask |= QC_SPC_TIMER;
	if (src->dqb_valid & QIF_ITIME)
		dst->d_fieldmask |= QC_INO_TIMER;
}

static int quota_setquota(struct super_block *sb, int type, qid_t id,
			  void __user *addr)
{
	struct qc_dqblk fdq;
	struct if_dqblk idq;
	struct kqid qid;

	if (copy_from_user(&idq, addr, sizeof(idq)))
		return -EFAULT;
	if (!sb->s_qcop->set_dqblk)
		return -ENOSYS;
	qid = make_kqid(current_user_ns(), type, id);
	if (!qid_has_mapping(sb->s_user_ns, qid))
		return -EINVAL;
	copy_from_if_dqblk(&fdq, &idq);
	return sb->s_qcop->set_dqblk(sb, qid, &fdq);
}

static int quota_enable(struct super_block *sb, void __user *addr)
{
	__u32 flags;

	if (copy_from_user(&flags, addr, sizeof(flags)))
		return -EFAULT;
	if (!sb->s_qcop->quota_enable)
		return -ENOSYS;
	return sb->s_qcop->quota_enable(sb, flags);
}

static int quota_disable(struct super_block *sb, void __user *addr)
{
	__u32 flags;

	if (copy_from_user(&flags, addr, sizeof(flags)))
		return -EFAULT;
	if (!sb->s_qcop->quota_disable)
		return -ENOSYS;
	return sb->s_qcop->quota_disable(sb, flags);
}

static int quota_state_to_flags(struct qc_state *state)
{
	int flags = 0;

	if (state->s_state[USRQUOTA].flags & QCI_ACCT_ENABLED)
		flags |= FS_QUOTA_UDQ_ACCT;
	if (state->s_state[USRQUOTA].flags & QCI_LIMITS_ENFORCED)
		flags |= FS_QUOTA_UDQ_ENFD;
	if (state->s_state[GRPQUOTA].flags & QCI_ACCT_ENABLED)
		flags |= FS_QUOTA_GDQ_ACCT;
	if (state->s_state[GRPQUOTA].flags & QCI_LIMITS_ENFORCED)
		flags |= FS_QUOTA_GDQ_ENFD;
	if (state->s_state[PRJQUOTA].flags & QCI_ACCT_ENABLED)
		flags |= FS_QUOTA_PDQ_ACCT;
	if (state->s_state[PRJQUOTA].flags & QCI_LIMITS_ENFORCED)
		flags |= FS_QUOTA_PDQ_ENFD;
	return flags;
}

static int quota_getstate(struct super_block *sb, int type,
			  struct fs_quota_stat *fqs)
{
	struct qc_state state;
	int ret;

	memset(&state, 0, sizeof (struct qc_state));
	ret = sb->s_qcop->get_state(sb, &state);
	if (ret < 0)
		return ret;

	memset(fqs, 0, sizeof(*fqs));
	fqs->qs_version = FS_QSTAT_VERSION;
	fqs->qs_flags = quota_state_to_flags(&state);
	/* No quota enabled? */
	if (!fqs->qs_flags)
		return -ENOSYS;
	fqs->qs_incoredqs = state.s_incoredqs;

	fqs->qs_btimelimit = state.s_state[type].spc_timelimit;
	fqs->qs_itimelimit = state.s_state[type].ino_timelimit;
	fqs->qs_rtbtimelimit = state.s_state[type].rt_spc_timelimit;
	fqs->qs_bwarnlimit = state.s_state[type].spc_warnlimit;
	fqs->qs_iwarnlimit = state.s_state[type].ino_warnlimit;

	/* Inodes may be allocated even if inactive; copy out if present */
	if (state.s_state[USRQUOTA].ino) {
		fqs->qs_uquota.qfs_ino = state.s_state[USRQUOTA].ino;
		fqs->qs_uquota.qfs_nblks = state.s_state[USRQUOTA].blocks;
		fqs->qs_uquota.qfs_nextents = state.s_state[USRQUOTA].nextents;
	}
	if (state.s_state[GRPQUOTA].ino) {
		fqs->qs_gquota.qfs_ino = state.s_state[GRPQUOTA].ino;
		fqs->qs_gquota.qfs_nblks = state.s_state[GRPQUOTA].blocks;
		fqs->qs_gquota.qfs_nextents = state.s_state[GRPQUOTA].nextents;
	}
	if (state.s_state[PRJQUOTA].ino) {
		/*
		 * Q_XGETQSTAT doesn't have room for both group and project
		 * quotas.  So, allow the project quota values to be copied out
		 * only if there is no group quota information available.
		 */
		if (!(state.s_state[GRPQUOTA].flags & QCI_ACCT_ENABLED)) {
			fqs->qs_gquota.qfs_ino = state.s_state[PRJQUOTA].ino;
			fqs->qs_gquota.qfs_nblks =
					state.s_state[PRJQUOTA].blocks;
			fqs->qs_gquota.qfs_nextents =
					state.s_state[PRJQUOTA].nextents;
		}
	}
	return 0;
}

static int quota_getxstate(struct super_block *sb, int type, void __user *addr)
{
	struct fs_quota_stat fqs;
	int ret;

	if (!sb->s_qcop->get_state)
		return -ENOSYS;
	ret = quota_getstate(sb, type, &fqs);
	if (!ret && copy_to_user(addr, &fqs, sizeof(fqs)))
		return -EFAULT;
	return ret;
}

static int quota_getstatev(struct super_block *sb, int type,
			   struct fs_quota_statv *fqs)
{
	struct qc_state state;
	int ret;

	memset(&state, 0, sizeof (struct qc_state));
	ret = sb->s_qcop->get_state(sb, &state);
	if (ret < 0)
		return ret;

	memset(fqs, 0, sizeof(*fqs));
	fqs->qs_version = FS_QSTAT_VERSION;
	fqs->qs_flags = quota_state_to_flags(&state);
	/* No quota enabled? */
	if (!fqs->qs_flags)
		return -ENOSYS;
	fqs->qs_incoredqs = state.s_incoredqs;

	fqs->qs_btimelimit = state.s_state[type].spc_timelimit;
	fqs->qs_itimelimit = state.s_state[type].ino_timelimit;
	fqs->qs_rtbtimelimit = state.s_state[type].rt_spc_timelimit;
	fqs->qs_bwarnlimit = state.s_state[type].spc_warnlimit;
	fqs->qs_iwarnlimit = state.s_state[type].ino_warnlimit;

	/* Inodes may be allocated even if inactive; copy out if present */
	if (state.s_state[USRQUOTA].ino) {
		fqs->qs_uquota.qfs_ino = state.s_state[USRQUOTA].ino;
		fqs->qs_uquota.qfs_nblks = state.s_state[USRQUOTA].blocks;
		fqs->qs_uquota.qfs_nextents = state.s_state[USRQUOTA].nextents;
	}
	if (state.s_state[GRPQUOTA].ino) {
		fqs->qs_gquota.qfs_ino = state.s_state[GRPQUOTA].ino;
		fqs->qs_gquota.qfs_nblks = state.s_state[GRPQUOTA].blocks;
		fqs->qs_gquota.qfs_nextents = state.s_state[GRPQUOTA].nextents;
	}
	if (state.s_state[PRJQUOTA].ino) {
		fqs->qs_pquota.qfs_ino = state.s_state[PRJQUOTA].ino;
		fqs->qs_pquota.qfs_nblks = state.s_state[PRJQUOTA].blocks;
		fqs->qs_pquota.qfs_nextents = state.s_state[PRJQUOTA].nextents;
	}
	return 0;
}

static int quota_getxstatev(struct super_block *sb, int type, void __user *addr)
{
	struct fs_quota_statv fqs;
	int ret;

	if (!sb->s_qcop->get_state)
		return -ENOSYS;

	memset(&fqs, 0, sizeof(fqs));
	if (copy_from_user(&fqs, addr, 1)) /* Just read qs_version */
		return -EFAULT;

	/* If this kernel doesn't support user specified version, fail */
	switch (fqs.qs_version) {
	case FS_QSTATV_VERSION1:
		break;
	default:
		return -EINVAL;
	}
	ret = quota_getstatev(sb, type, &fqs);
	if (!ret && copy_to_user(addr, &fqs, sizeof(fqs)))
		return -EFAULT;
	return ret;
}

/*
 * XFS defines BBTOB and BTOBB macros inside fs/xfs/ and we cannot move them
 * out of there as xfsprogs rely on definitions being in that header file. So
 * just define same functions here for quota purposes.
 */
#define XFS_BB_SHIFT 9

static inline u64 quota_bbtob(u64 blocks)
{
	return blocks << XFS_BB_SHIFT;
}

static inline u64 quota_btobb(u64 bytes)
{
	return (bytes + (1 << XFS_BB_SHIFT) - 1) >> XFS_BB_SHIFT;
}

static void copy_from_xfs_dqblk(struct qc_dqblk *dst, struct fs_disk_quota *src)
{
	dst->d_spc_hardlimit = quota_bbtob(src->d_blk_hardlimit);
	dst->d_spc_softlimit = quota_bbtob(src->d_blk_softlimit);
	dst->d_ino_hardlimit = src->d_ino_hardlimit;
	dst->d_ino_softlimit = src->d_ino_softlimit;
	dst->d_space = quota_bbtob(src->d_bcount);
	dst->d_ino_count = src->d_icount;
	dst->d_ino_timer = src->d_itimer;
	dst->d_spc_timer = src->d_btimer;
	dst->d_ino_warns = src->d_iwarns;
	dst->d_spc_warns = src->d_bwarns;
	dst->d_rt_spc_hardlimit = quota_bbtob(src->d_rtb_hardlimit);
	dst->d_rt_spc_softlimit = quota_bbtob(src->d_rtb_softlimit);
	dst->d_rt_space = quota_bbtob(src->d_rtbcount);
	dst->d_rt_spc_timer = src->d_rtbtimer;
	dst->d_rt_spc_warns = src->d_rtbwarns;
	dst->d_fieldmask = 0;
	if (src->d_fieldmask & FS_DQ_ISOFT)
		dst->d_fieldmask |= QC_INO_SOFT;
	if (src->d_fieldmask & FS_DQ_IHARD)
		dst->d_fieldmask |= QC_INO_HARD;
	if (src->d_fieldmask & FS_DQ_BSOFT)
		dst->d_fieldmask |= QC_SPC_SOFT;
	if (src->d_fieldmask & FS_DQ_BHARD)
		dst->d_fieldmask |= QC_SPC_HARD;
	if (src->d_fieldmask & FS_DQ_RTBSOFT)
		dst->d_fieldmask |= QC_RT_SPC_SOFT;
	if (src->d_fieldmask & FS_DQ_RTBHARD)
		dst->d_fieldmask |= QC_RT_SPC_HARD;
	if (src->d_fieldmask & FS_DQ_BTIMER)
		dst->d_fieldmask |= QC_SPC_TIMER;
	if (src->d_fieldmask & FS_DQ_ITIMER)
		dst->d_fieldmask |= QC_INO_TIMER;
	if (src->d_fieldmask & FS_DQ_RTBTIMER)
		dst->d_fieldmask |= QC_RT_SPC_TIMER;
	if (src->d_fieldmask & FS_DQ_BWARNS)
		dst->d_fieldmask |= QC_SPC_WARNS;
	if (src->d_fieldmask & FS_DQ_IWARNS)
		dst->d_fieldmask |= QC_INO_WARNS;
	if (src->d_fieldmask & FS_DQ_RTBWARNS)
		dst->d_fieldmask |= QC_RT_SPC_WARNS;
	if (src->d_fieldmask & FS_DQ_BCOUNT)
		dst->d_fieldmask |= QC_SPACE;
	if (src->d_fieldmask & FS_DQ_ICOUNT)
		dst->d_fieldmask |= QC_INO_COUNT;
	if (src->d_fieldmask & FS_DQ_RTBCOUNT)
		dst->d_fieldmask |= QC_RT_SPACE;
}

static void copy_qcinfo_from_xfs_dqblk(struct qc_info *dst,
				       struct fs_disk_quota *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->i_spc_timelimit = src->d_btimer;
	dst->i_ino_timelimit = src->d_itimer;
	dst->i_rt_spc_timelimit = src->d_rtbtimer;
	dst->i_ino_warnlimit = src->d_iwarns;
	dst->i_spc_warnlimit = src->d_bwarns;
	dst->i_rt_spc_warnlimit = src->d_rtbwarns;
	if (src->d_fieldmask & FS_DQ_BWARNS)
		dst->i_fieldmask |= QC_SPC_WARNS;
	if (src->d_fieldmask & FS_DQ_IWARNS)
		dst->i_fieldmask |= QC_INO_WARNS;
	if (src->d_fieldmask & FS_DQ_RTBWARNS)
		dst->i_fieldmask |= QC_RT_SPC_WARNS;
	if (src->d_fieldmask & FS_DQ_BTIMER)
		dst->i_fieldmask |= QC_SPC_TIMER;
	if (src->d_fieldmask & FS_DQ_ITIMER)
		dst->i_fieldmask |= QC_INO_TIMER;
	if (src->d_fieldmask & FS_DQ_RTBTIMER)
		dst->i_fieldmask |= QC_RT_SPC_TIMER;
}

static int quota_setxquota(struct super_block *sb, int type, qid_t id,
			   void __user *addr)
{
	struct fs_disk_quota fdq;
	struct qc_dqblk qdq;
	struct kqid qid;

	if (copy_from_user(&fdq, addr, sizeof(fdq)))
		return -EFAULT;
	if (!sb->s_qcop->set_dqblk)
		return -ENOSYS;
	qid = make_kqid(current_user_ns(), type, id);
	if (!qid_has_mapping(sb->s_user_ns, qid))
		return -EINVAL;
	/* Are we actually setting timer / warning limits for all users? */
	if (from_kqid(sb->s_user_ns, qid) == 0 &&
	    fdq.d_fieldmask & (FS_DQ_WARNS_MASK | FS_DQ_TIMER_MASK)) {
		struct qc_info qinfo;
		int ret;

		if (!sb->s_qcop->set_info)
			return -EINVAL;
		copy_qcinfo_from_xfs_dqblk(&qinfo, &fdq);
		ret = sb->s_qcop->set_info(sb, type, &qinfo);
		if (ret)
			return ret;
		/* These are already done */
		fdq.d_fieldmask &= ~(FS_DQ_WARNS_MASK | FS_DQ_TIMER_MASK);
	}
	copy_from_xfs_dqblk(&qdq, &fdq);
	return sb->s_qcop->set_dqblk(sb, qid, &qdq);
}

static void copy_to_xfs_dqblk(struct fs_disk_quota *dst, struct qc_dqblk *src,
			      int type, qid_t id)
{
	memset(dst, 0, sizeof(*dst));
	dst->d_version = FS_DQUOT_VERSION;
	dst->d_id = id;
	if (type == USRQUOTA)
		dst->d_flags = FS_USER_QUOTA;
	else if (type == PRJQUOTA)
		dst->d_flags = FS_PROJ_QUOTA;
	else
		dst->d_flags = FS_GROUP_QUOTA;
	dst->d_blk_hardlimit = quota_btobb(src->d_spc_hardlimit);
	dst->d_blk_softlimit = quota_btobb(src->d_spc_softlimit);
	dst->d_ino_hardlimit = src->d_ino_hardlimit;
	dst->d_ino_softlimit = src->d_ino_softlimit;
	dst->d_bcount = quota_btobb(src->d_space);
	dst->d_icount = src->d_ino_count;
	dst->d_itimer = src->d_ino_timer;
	dst->d_btimer = src->d_spc_timer;
	dst->d_iwarns = src->d_ino_warns;
	dst->d_bwarns = src->d_spc_warns;
	dst->d_rtb_hardlimit = quota_btobb(src->d_rt_spc_hardlimit);
	dst->d_rtb_softlimit = quota_btobb(src->d_rt_spc_softlimit);
	dst->d_rtbcount = quota_btobb(src->d_rt_space);
	dst->d_rtbtimer = src->d_rt_spc_timer;
	dst->d_rtbwarns = src->d_rt_spc_warns;
}

static int quota_getxquota(struct super_block *sb, int type, qid_t id,
			   void __user *addr)
{
	struct fs_disk_quota fdq;
	struct qc_dqblk qdq;
	struct kqid qid;
	int ret;

	if (!sb->s_qcop->get_dqblk)
		return -ENOSYS;
	qid = make_kqid(current_user_ns(), type, id);
	if (!qid_has_mapping(sb->s_user_ns, qid))
		return -EINVAL;
	ret = sb->s_qcop->get_dqblk(sb, qid, &qdq);
	if (ret)
		return ret;
	copy_to_xfs_dqblk(&fdq, &qdq, type, id);
	if (copy_to_user(addr, &fdq, sizeof(fdq)))
		return -EFAULT;
	return ret;
}

/*
 * Return quota for next active quota >= this id, if any exists,
 * otherwise return -ENOENT via ->get_nextdqblk.
 */
static int quota_getnextxquota(struct super_block *sb, int type, qid_t id,
			    void __user *addr)
{
	struct fs_disk_quota fdq;
	struct qc_dqblk qdq;
	struct kqid qid;
	qid_t id_out;
	int ret;

	if (!sb->s_qcop->get_nextdqblk)
		return -ENOSYS;
	qid = make_kqid(current_user_ns(), type, id);
	if (!qid_has_mapping(sb->s_user_ns, qid))
		return -EINVAL;
	ret = sb->s_qcop->get_nextdqblk(sb, &qid, &qdq);
	if (ret)
		return ret;
	id_out = from_kqid(current_user_ns(), qid);
	copy_to_xfs_dqblk(&fdq, &qdq, type, id_out);
	if (copy_to_user(addr, &fdq, sizeof(fdq)))
		return -EFAULT;
	return ret;
}

static int quota_rmxquota(struct super_block *sb, void __user *addr)
{
	__u32 flags;

	if (copy_from_user(&flags, addr, sizeof(flags)))
		return -EFAULT;
	if (!sb->s_qcop->rm_xquota)
		return -ENOSYS;
	return sb->s_qcop->rm_xquota(sb, flags);
}

/* Copy parameters and call proper function */
static int do_quotactl(struct super_block *sb, int type, int cmd, qid_t id,
		       void __user *addr, const struct path *path)
{
	int ret;

	type = array_index_nospec(type, MAXQUOTAS);
	/*
	 * Quota not supported on this fs? Check this before s_quota_types
	 * since they needn't be set if quota is not supported at all.
	 */
	if (!sb->s_qcop)
		return -ENOSYS;
	if (!(sb->s_quota_types & (1 << type)))
		return -EINVAL;

	ret = check_quotactl_permission(sb, type, cmd, id);
	if (ret < 0)
		return ret;

	switch (cmd) {
	case Q_QUOTAON:
		return quota_quotaon(sb, type, id, path);
	case Q_QUOTAOFF:
		return quota_quotaoff(sb, type);
	case Q_GETFMT:
		return quota_getfmt(sb, type, addr);
	case Q_GETINFO:
		return quota_getinfo(sb, type, addr);
	case Q_SETINFO:
		return quota_setinfo(sb, type, addr);
	case Q_GETQUOTA:
		return quota_getquota(sb, type, id, addr);
	case Q_GETNEXTQUOTA:
		return quota_getnextquota(sb, type, id, addr);
	case Q_SETQUOTA:
		return quota_setquota(sb, type, id, addr);
	case Q_SYNC:
		if (!sb->s_qcop->quota_sync)
			return -ENOSYS;
		return sb->s_qcop->quota_sync(sb, type);
	case Q_XQUOTAON:
		return quota_enable(sb, addr);
	case Q_XQUOTAOFF:
		return quota_disable(sb, addr);
	case Q_XQUOTARM:
		return quota_rmxquota(sb, addr);
	case Q_XGETQSTAT:
		return quota_getxstate(sb, type, addr);
	case Q_XGETQSTATV:
		return quota_getxstatev(sb, type, addr);
	case Q_XSETQLIM:
		return quota_setxquota(sb, type, id, addr);
	case Q_XGETQUOTA:
		return quota_getxquota(sb, type, id, addr);
	case Q_XGETNEXTQUOTA:
		return quota_getnextxquota(sb, type, id, addr);
	case Q_XQUOTASYNC:
		if (sb_rdonly(sb))
			return -EROFS;
		/* XFS quotas are fully coherent now, making this call a noop */
		return 0;
	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_BLOCK

/* Return 1 if 'cmd' will block on frozen filesystem */
static int quotactl_cmd_write(int cmd)
{
	/*
	 * We cannot allow Q_GETQUOTA and Q_GETNEXTQUOTA without write access
	 * as dquot_acquire() may allocate space for new structure and OCFS2
	 * needs to increment on-disk use count.
	 */
	switch (cmd) {
	case Q_GETFMT:
	case Q_GETINFO:
	case Q_SYNC:
	case Q_XGETQSTAT:
	case Q_XGETQSTATV:
	case Q_XGETQUOTA:
	case Q_XGETNEXTQUOTA:
	case Q_XQUOTASYNC:
		return 0;
	}
	return 1;
}
#endif /* CONFIG_BLOCK */

/* Return true if quotactl command is manipulating quota on/off state */
static bool quotactl_cmd_onoff(int cmd)
{
	return (cmd == Q_QUOTAON) || (cmd == Q_QUOTAOFF) ||
		 (cmd == Q_XQUOTAON) || (cmd == Q_XQUOTAOFF);
}

/*
 * look up a superblock on which quota ops will be performed
 * - use the name of a block device to find the superblock thereon
 */
static struct super_block *quotactl_block(const char __user *special, int cmd)
{
#ifdef CONFIG_BLOCK
	struct block_device *bdev;
	struct super_block *sb;
	struct filename *tmp = getname(special);

	if (IS_ERR(tmp))
		return ERR_CAST(tmp);
	bdev = lookup_bdev(tmp->name);
	putname(tmp);
	if (IS_ERR(bdev))
		return ERR_CAST(bdev);
	if (quotactl_cmd_onoff(cmd))
		sb = get_super_exclusive_thawed(bdev);
	else if (quotactl_cmd_write(cmd))
		sb = get_super_thawed(bdev);
	else
		sb = get_super(bdev);
	bdput(bdev);
	if (!sb)
		return ERR_PTR(-ENODEV);

	return sb;
#else
	return ERR_PTR(-ENODEV);
#endif
}

/*
 * This is the system call interface. This communicates with
 * the user-level programs. Currently this only supports diskquota
 * calls. Maybe we need to add the process quotas etc. in the future,
 * but we probably should use rlimits for that.
 */
int kernel_quotactl(unsigned int cmd, const char __user *special,
		    qid_t id, void __user *addr)
{
	uint cmds, type;
	struct super_block *sb = NULL;
	struct path path, *pathp = NULL;
	int ret;

	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

	if (type >= MAXQUOTAS)
		return -EINVAL;

	/*
	 * As a special case Q_SYNC can be called without a specific device.
	 * It will iterate all superblocks that have quota enabled and call
	 * the sync action on each of them.
	 */
	if (!special) {
		if (cmds == Q_SYNC)
			return quota_sync_all(type);
		return -ENODEV;
	}

	/*
	 * Path for quotaon has to be resolved before grabbing superblock
	 * because that gets s_umount sem which is also possibly needed by path
	 * resolution (think about autofs) and thus deadlocks could arise.
	 */
	if (cmds == Q_QUOTAON) {
		ret = user_path_at(AT_FDCWD, addr, LOOKUP_FOLLOW|LOOKUP_AUTOMOUNT, &path);
		if (ret)
			pathp = ERR_PTR(ret);
		else
			pathp = &path;
	}

	sb = quotactl_block(special, cmds);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		goto out;
	}

	ret = do_quotactl(sb, type, cmds, id, addr, pathp);

	if (!quotactl_cmd_onoff(cmds))
		drop_super(sb);
	else
		drop_super_exclusive(sb);
out:
	if (pathp && !IS_ERR(pathp))
		path_put(pathp);
	return ret;
}

SYSCALL_DEFINE4(quotactl, unsigned int, cmd, const char __user *, special,
		qid_t, id, void __user *, addr)
{
	return kernel_quotactl(cmd, special, id, addr);
}
