// SPDX-License-Identifier: GPL-2.0

#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/quotaops.h>

/*
 * This code works only for 32 bit quota tools over 64 bit OS (x86_64, ia64)
 * and is necessary due to alignment problems.
 */
struct compat_if_dqblk {
	compat_u64 dqb_bhardlimit;
	compat_u64 dqb_bsoftlimit;
	compat_u64 dqb_curspace;
	compat_u64 dqb_ihardlimit;
	compat_u64 dqb_isoftlimit;
	compat_u64 dqb_curinodes;
	compat_u64 dqb_btime;
	compat_u64 dqb_itime;
	compat_uint_t dqb_valid;
};

/* XFS structures */
struct compat_fs_qfilestat {
	compat_u64 dqb_bhardlimit;
	compat_u64 qfs_nblks;
	compat_uint_t qfs_nextents;
};

struct compat_fs_quota_stat {
	__s8		qs_version;
	__u16		qs_flags;
	__s8		qs_pad;
	struct compat_fs_qfilestat	qs_uquota;
	struct compat_fs_qfilestat	qs_gquota;
	compat_uint_t	qs_incoredqs;
	compat_int_t	qs_btimelimit;
	compat_int_t	qs_itimelimit;
	compat_int_t	qs_rtbtimelimit;
	__u16		qs_bwarnlimit;
	__u16		qs_iwarnlimit;
};

asmlinkage long sys32_quotactl(unsigned int cmd, const char __user *special,
						qid_t id, void __user *addr)
{
	unsigned int cmds;
	struct if_dqblk __user *dqblk;
	struct compat_if_dqblk __user *compat_dqblk;
	struct fs_quota_stat __user *fsqstat;
	struct compat_fs_quota_stat __user *compat_fsqstat;
	compat_uint_t data;
	u16 xdata;
	long ret;

	cmds = cmd >> SUBCMDSHIFT;

	switch (cmds) {
	case Q_GETQUOTA:
		dqblk = compat_alloc_user_space(sizeof(struct if_dqblk));
		compat_dqblk = addr;
		ret = sys_quotactl(cmd, special, id, dqblk);
		if (ret)
			break;
		if (copy_in_user(compat_dqblk, dqblk, sizeof(*compat_dqblk)) ||
			get_user(data, &dqblk->dqb_valid) ||
			put_user(data, &compat_dqblk->dqb_valid))
			ret = -EFAULT;
		break;
	case Q_SETQUOTA:
		dqblk = compat_alloc_user_space(sizeof(struct if_dqblk));
		compat_dqblk = addr;
		ret = -EFAULT;
		if (copy_in_user(dqblk, compat_dqblk, sizeof(*compat_dqblk)) ||
			get_user(data, &compat_dqblk->dqb_valid) ||
			put_user(data, &dqblk->dqb_valid))
			break;
		ret = sys_quotactl(cmd, special, id, dqblk);
		break;
	case Q_XGETQSTAT:
		fsqstat = compat_alloc_user_space(sizeof(struct fs_quota_stat));
		compat_fsqstat = addr;
		ret = sys_quotactl(cmd, special, id, fsqstat);
		if (ret)
			break;
		ret = -EFAULT;
		/* Copying qs_version, qs_flags, qs_pad */
		if (copy_in_user(compat_fsqstat, fsqstat,
			offsetof(struct compat_fs_quota_stat, qs_uquota)))
			break;
		/* Copying qs_uquota */
		if (copy_in_user(&compat_fsqstat->qs_uquota,
			&fsqstat->qs_uquota,
			sizeof(compat_fsqstat->qs_uquota)) ||
			get_user(data, &fsqstat->qs_uquota.qfs_nextents) ||
			put_user(data, &compat_fsqstat->qs_uquota.qfs_nextents))
			break;
		/* Copying qs_gquota */
		if (copy_in_user(&compat_fsqstat->qs_gquota,
			&fsqstat->qs_gquota,
			sizeof(compat_fsqstat->qs_gquota)) ||
			get_user(data, &fsqstat->qs_gquota.qfs_nextents) ||
			put_user(data, &compat_fsqstat->qs_gquota.qfs_nextents))
			break;
		/* Copying the rest */
		if (copy_in_user(&compat_fsqstat->qs_incoredqs,
			&fsqstat->qs_incoredqs,
			sizeof(struct compat_fs_quota_stat) -
			offsetof(struct compat_fs_quota_stat, qs_incoredqs)) ||
			get_user(xdata, &fsqstat->qs_iwarnlimit) ||
			put_user(xdata, &compat_fsqstat->qs_iwarnlimit))
			break;
		ret = 0;
		break;
	default:
		ret = sys_quotactl(cmd, special, id, addr);
	}
	return ret;
}
