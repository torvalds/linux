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
#include <asm/uaccess.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h>
#include <linux/capability.h>
#include <linux/quotaops.h>
#include <linux/types.h>

/* Check validity of generic quotactl commands */
static int generic_quotactl_valid(struct super_block *sb, int type, int cmd, qid_t id)
{
	if (type >= MAXQUOTAS)
		return -EINVAL;
	if (!sb && cmd != Q_SYNC)
		return -ENODEV;
	/* Is operation supported? */
	if (sb && !sb->s_qcop)
		return -ENOSYS;

	switch (cmd) {
		case Q_GETFMT:
			break;
		case Q_QUOTAON:
			if (!sb->s_qcop->quota_on)
				return -ENOSYS;
			break;
		case Q_QUOTAOFF:
			if (!sb->s_qcop->quota_off)
				return -ENOSYS;
			break;
		case Q_SETINFO:
			if (!sb->s_qcop->set_info)
				return -ENOSYS;
			break;
		case Q_GETINFO:
			if (!sb->s_qcop->get_info)
				return -ENOSYS;
			break;
		case Q_SETQUOTA:
			if (!sb->s_qcop->set_dqblk)
				return -ENOSYS;
			break;
		case Q_GETQUOTA:
			if (!sb->s_qcop->get_dqblk)
				return -ENOSYS;
			break;
		case Q_SYNC:
			if (sb && !sb->s_qcop->quota_sync)
				return -ENOSYS;
			break;
		default:
			return -EINVAL;
	}

	/* Is quota turned on for commands which need it? */
	switch (cmd) {
		case Q_GETFMT:
		case Q_GETINFO:
		case Q_SETINFO:
		case Q_SETQUOTA:
		case Q_GETQUOTA:
			/* This is just informative test so we are satisfied without a lock */
			if (!sb_has_quota_enabled(sb, type))
				return -ESRCH;
	}

	/* Check privileges */
	if (cmd == Q_GETQUOTA) {
		if (((type == USRQUOTA && current->euid != id) ||
		     (type == GRPQUOTA && !in_egroup_p(id))) &&
		    !capable(CAP_SYS_ADMIN))
			return -EPERM;
	}
	else if (cmd != Q_GETFMT && cmd != Q_SYNC && cmd != Q_GETINFO)
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

	return 0;
}

/* Check validity of XFS Quota Manager commands */
static int xqm_quotactl_valid(struct super_block *sb, int type, int cmd, qid_t id)
{
	if (type >= XQM_MAXQUOTAS)
		return -EINVAL;
	if (!sb)
		return -ENODEV;
	if (!sb->s_qcop)
		return -ENOSYS;

	switch (cmd) {
		case Q_XQUOTAON:
		case Q_XQUOTAOFF:
		case Q_XQUOTARM:
			if (!sb->s_qcop->set_xstate)
				return -ENOSYS;
			break;
		case Q_XGETQSTAT:
			if (!sb->s_qcop->get_xstate)
				return -ENOSYS;
			break;
		case Q_XSETQLIM:
			if (!sb->s_qcop->set_xquota)
				return -ENOSYS;
			break;
		case Q_XGETQUOTA:
			if (!sb->s_qcop->get_xquota)
				return -ENOSYS;
			break;
		case Q_XQUOTASYNC:
			if (!sb->s_qcop->quota_sync)
				return -ENOSYS;
			break;
		default:
			return -EINVAL;
	}

	/* Check privileges */
	if (cmd == Q_XGETQUOTA) {
		if (((type == XQM_USRQUOTA && current->euid != id) ||
		     (type == XQM_GRPQUOTA && !in_egroup_p(id))) &&
		     !capable(CAP_SYS_ADMIN))
			return -EPERM;
	} else if (cmd != Q_XGETQSTAT && cmd != Q_XQUOTASYNC) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	}

	return 0;
}

static int check_quotactl_valid(struct super_block *sb, int type, int cmd, qid_t id)
{
	int error;

	if (XQM_COMMAND(cmd))
		error = xqm_quotactl_valid(sb, type, cmd, id);
	else
		error = generic_quotactl_valid(sb, type, cmd, id);
	if (!error)
		error = security_quotactl(cmd, type, id, sb);
	return error;
}

static void quota_sync_sb(struct super_block *sb, int type)
{
	int cnt;

	sb->s_qcop->quota_sync(sb, type);
	/* This is not very clever (and fast) but currently I don't know about
	 * any other simple way of getting quota data to disk and we must get
	 * them there for userspace to be visible... */
	if (sb->s_op->sync_fs)
		sb->s_op->sync_fs(sb, 1);
	sync_blockdev(sb->s_bdev);

	/*
	 * Now when everything is written we can discard the pagecache so
	 * that userspace sees the changes.
	 */
	mutex_lock(&sb_dqopt(sb)->dqonoff_mutex);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_enabled(sb, cnt))
			continue;
		mutex_lock_nested(&sb_dqopt(sb)->files[cnt]->i_mutex, I_MUTEX_QUOTA);
		truncate_inode_pages(&sb_dqopt(sb)->files[cnt]->i_data, 0);
		mutex_unlock(&sb_dqopt(sb)->files[cnt]->i_mutex);
	}
	mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
}

void sync_dquots(struct super_block *sb, int type)
{
	int cnt;

	if (sb) {
		if (sb->s_qcop->quota_sync)
			quota_sync_sb(sb, type);
		return;
	}

	spin_lock(&sb_lock);
restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		/* This test just improves performance so it needn't be reliable... */
		for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
			if (type != -1 && type != cnt)
				continue;
			if (!sb_has_quota_enabled(sb, cnt))
				continue;
			if (!info_dirty(&sb_dqopt(sb)->info[cnt]) &&
			    list_empty(&sb_dqopt(sb)->info[cnt].dqi_dirty_list))
				continue;
			break;
		}
		if (cnt == MAXQUOTAS)
			continue;
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (sb->s_root && sb->s_qcop->quota_sync)
			quota_sync_sb(sb, type);
		up_read(&sb->s_umount);
		spin_lock(&sb_lock);
		if (__put_super_and_need_restart(sb))
			goto restart;
	}
	spin_unlock(&sb_lock);
}

/* Copy parameters and call proper function */
static int do_quotactl(struct super_block *sb, int type, int cmd, qid_t id, void __user *addr)
{
	int ret;

	switch (cmd) {
		case Q_QUOTAON: {
			char *pathname;

			if (IS_ERR(pathname = getname(addr)))
				return PTR_ERR(pathname);
			ret = sb->s_qcop->quota_on(sb, type, id, pathname, 0);
			putname(pathname);
			return ret;
		}
		case Q_QUOTAOFF:
			return sb->s_qcop->quota_off(sb, type, 0);

		case Q_GETFMT: {
			__u32 fmt;

			down_read(&sb_dqopt(sb)->dqptr_sem);
			if (!sb_has_quota_enabled(sb, type)) {
				up_read(&sb_dqopt(sb)->dqptr_sem);
				return -ESRCH;
			}
			fmt = sb_dqopt(sb)->info[type].dqi_format->qf_fmt_id;
			up_read(&sb_dqopt(sb)->dqptr_sem);
			if (copy_to_user(addr, &fmt, sizeof(fmt)))
				return -EFAULT;
			return 0;
		}
		case Q_GETINFO: {
			struct if_dqinfo info;

			if ((ret = sb->s_qcop->get_info(sb, type, &info)))
				return ret;
			if (copy_to_user(addr, &info, sizeof(info)))
				return -EFAULT;
			return 0;
		}
		case Q_SETINFO: {
			struct if_dqinfo info;

			if (copy_from_user(&info, addr, sizeof(info)))
				return -EFAULT;
			return sb->s_qcop->set_info(sb, type, &info);
		}
		case Q_GETQUOTA: {
			struct if_dqblk idq;

			if ((ret = sb->s_qcop->get_dqblk(sb, type, id, &idq)))
				return ret;
			if (copy_to_user(addr, &idq, sizeof(idq)))
				return -EFAULT;
			return 0;
		}
		case Q_SETQUOTA: {
			struct if_dqblk idq;

			if (copy_from_user(&idq, addr, sizeof(idq)))
				return -EFAULT;
			return sb->s_qcop->set_dqblk(sb, type, id, &idq);
		}
		case Q_SYNC:
			sync_dquots(sb, type);
			return 0;

		case Q_XQUOTAON:
		case Q_XQUOTAOFF:
		case Q_XQUOTARM: {
			__u32 flags;

			if (copy_from_user(&flags, addr, sizeof(flags)))
				return -EFAULT;
			return sb->s_qcop->set_xstate(sb, flags, cmd);
		}
		case Q_XGETQSTAT: {
			struct fs_quota_stat fqs;
		
			if ((ret = sb->s_qcop->get_xstate(sb, &fqs)))
				return ret;
			if (copy_to_user(addr, &fqs, sizeof(fqs)))
				return -EFAULT;
			return 0;
		}
		case Q_XSETQLIM: {
			struct fs_disk_quota fdq;

			if (copy_from_user(&fdq, addr, sizeof(fdq)))
				return -EFAULT;
		       return sb->s_qcop->set_xquota(sb, type, id, &fdq);
		}
		case Q_XGETQUOTA: {
			struct fs_disk_quota fdq;

			if ((ret = sb->s_qcop->get_xquota(sb, type, id, &fdq)))
				return ret;
			if (copy_to_user(addr, &fdq, sizeof(fdq)))
				return -EFAULT;
			return 0;
		}
		case Q_XQUOTASYNC:
			return sb->s_qcop->quota_sync(sb, type);
		/* We never reach here unless validity check is broken */
		default:
			BUG();
	}
	return 0;
}

/*
 * look up a superblock on which quota ops will be performed
 * - use the name of a block device to find the superblock thereon
 */
static inline struct super_block *quotactl_block(const char __user *special)
{
#ifdef CONFIG_BLOCK
	struct block_device *bdev;
	struct super_block *sb;
	char *tmp = getname(special);

	if (IS_ERR(tmp))
		return ERR_CAST(tmp);
	bdev = lookup_bdev(tmp);
	putname(tmp);
	if (IS_ERR(bdev))
		return ERR_CAST(bdev);
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
asmlinkage long sys_quotactl(unsigned int cmd, const char __user *special, qid_t id, void __user *addr)
{
	uint cmds, type;
	struct super_block *sb = NULL;
	int ret;

	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

	if (cmds != Q_SYNC || special) {
		sb = quotactl_block(special);
		if (IS_ERR(sb))
			return PTR_ERR(sb);
	}

	ret = check_quotactl_valid(sb, type, cmds, id);
	if (ret >= 0)
		ret = do_quotactl(sb, type, cmds, id, addr);
	if (sb)
		drop_super(sb);

	return ret;
}

#if defined(CONFIG_COMPAT_FOR_U64_ALIGNMENT)
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
#endif
