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
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h>

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
		case Q_QUOTAOFF:
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
		default:
			return -EINVAL;
	}

	/* Check privileges */
	if (cmd == Q_XGETQUOTA) {
		if (((type == XQM_USRQUOTA && current->euid != id) ||
		     (type == XQM_GRPQUOTA && !in_egroup_p(id))) &&
		     !capable(CAP_SYS_ADMIN))
			return -EPERM;
	} else if (cmd != Q_XGETQSTAT) {
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
	struct inode *discard[MAXQUOTAS];

	sb->s_qcop->quota_sync(sb, type);
	/* This is not very clever (and fast) but currently I don't know about
	 * any other simple way of getting quota data to disk and we must get
	 * them there for userspace to be visible... */
	if (sb->s_op->sync_fs)
		sb->s_op->sync_fs(sb, 1);
	sync_blockdev(sb->s_bdev);

	/* Now when everything is written we can discard the pagecache so
	 * that userspace sees the changes. We need i_sem and so we could
	 * not do it inside dqonoff_sem. Moreover we need to be carefull
	 * about races with quotaoff() (that is the reason why we have own
	 * reference to inode). */
	down(&sb_dqopt(sb)->dqonoff_sem);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		discard[cnt] = NULL;
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_enabled(sb, cnt))
			continue;
		discard[cnt] = igrab(sb_dqopt(sb)->files[cnt]);
	}
	up(&sb_dqopt(sb)->dqonoff_sem);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (discard[cnt]) {
			down(&discard[cnt]->i_sem);
			truncate_inode_pages(&discard[cnt]->i_data, 0);
			up(&discard[cnt]->i_sem);
			iput(discard[cnt]);
		}
	}
}

void sync_dquots(struct super_block *sb, int type)
{
	int cnt, dirty;

	if (sb) {
		if (sb->s_qcop->quota_sync)
			quota_sync_sb(sb, type);
		return;
	}

	spin_lock(&sb_lock);
restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		/* This test just improves performance so it needn't be reliable... */
		for (cnt = 0, dirty = 0; cnt < MAXQUOTAS; cnt++)
			if ((type == cnt || type == -1) && sb_has_quota_enabled(sb, cnt)
			    && info_any_dirty(&sb_dqopt(sb)->info[cnt]))
				dirty = 1;
		if (!dirty)
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
			ret = sb->s_qcop->quota_on(sb, type, id, pathname);
			putname(pathname);
			return ret;
		}
		case Q_QUOTAOFF:
			return sb->s_qcop->quota_off(sb, type);

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
		/* We never reach here unless validity check is broken */
		default:
			BUG();
	}
	return 0;
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
	struct block_device *bdev;
	char *tmp;
	int ret;

	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

	if (cmds != Q_SYNC || special) {
		tmp = getname(special);
		if (IS_ERR(tmp))
			return PTR_ERR(tmp);
		bdev = lookup_bdev(tmp);
		putname(tmp);
		if (IS_ERR(bdev))
			return PTR_ERR(bdev);
		sb = get_super(bdev);
		bdput(bdev);
		if (!sb)
			return -ENODEV;
	}

	ret = check_quotactl_valid(sb, type, cmds, id);
	if (ret >= 0)
		ret = do_quotactl(sb, type, cmds, id, addr);
	if (sb)
		drop_super(sb);

	return ret;
}
