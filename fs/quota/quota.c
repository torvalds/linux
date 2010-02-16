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
#include <net/netlink.h>
#include <net/genetlink.h>

static int check_quotactl_permission(struct super_block *sb, int type, int cmd,
				     qid_t id)
{
	switch (cmd) {
	/* these commands do not require any special privilegues */
	case Q_GETFMT:
	case Q_SYNC:
	case Q_GETINFO:
	case Q_XGETQSTAT:
	case Q_XQUOTASYNC:
		break;
	/* allow to query information for dquots we "own" */
	case Q_GETQUOTA:
	case Q_XGETQUOTA:
		if ((type == USRQUOTA && current_euid() == id) ||
		    (type == GRPQUOTA && in_egroup_p(id)))
			break;
		/*FALLTHROUGH*/
	default:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	}

	return security_quotactl(cmd, type, id, sb);
}

#ifdef CONFIG_QUOTA
void sync_quota_sb(struct super_block *sb, int type)
{
	int cnt;

	if (!sb->s_qcop->quota_sync)
		return;

	sb->s_qcop->quota_sync(sb, type);

	if (sb_dqopt(sb)->flags & DQUOT_QUOTA_SYS_FILE)
		return;
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
		if (!sb_has_quota_active(sb, cnt))
			continue;
		mutex_lock_nested(&sb_dqopt(sb)->files[cnt]->i_mutex,
				  I_MUTEX_QUOTA);
		truncate_inode_pages(&sb_dqopt(sb)->files[cnt]->i_data, 0);
		mutex_unlock(&sb_dqopt(sb)->files[cnt]->i_mutex);
	}
	mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
}
#endif

static int quota_sync_all(int type)
{
	struct super_block *sb;
	int cnt;
	int ret;

	if (type >= MAXQUOTAS)
		return -EINVAL;
	ret = security_quotactl(Q_SYNC, type, 0, NULL);
	if (ret)
		return ret;

	spin_lock(&sb_lock);
restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		/* This test just improves performance so it needn't be
		 * reliable... */
		for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
			if (type != -1 && type != cnt)
				continue;
			if (!sb_has_quota_active(sb, cnt))
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
		if (sb->s_root)
			sync_quota_sb(sb, type);
		up_read(&sb->s_umount);
		spin_lock(&sb_lock);
		if (__put_super_and_need_restart(sb))
			goto restart;
	}
	spin_unlock(&sb_lock);

	return 0;
}

static int quota_quotaon(struct super_block *sb, int type, int cmd, qid_t id,
		         void __user *addr)
{
	char *pathname;
	int ret = -ENOSYS;

	pathname = getname(addr);
	if (IS_ERR(pathname))
		return PTR_ERR(pathname);
	if (sb->s_qcop->quota_on)
		ret = sb->s_qcop->quota_on(sb, type, id, pathname, 0);
	putname(pathname);
	return ret;
}

static int quota_getfmt(struct super_block *sb, int type, void __user *addr)
{
	__u32 fmt;

	down_read(&sb_dqopt(sb)->dqptr_sem);
	if (!sb_has_quota_active(sb, type)) {
		up_read(&sb_dqopt(sb)->dqptr_sem);
		return -ESRCH;
	}
	fmt = sb_dqopt(sb)->info[type].dqi_format->qf_fmt_id;
	up_read(&sb_dqopt(sb)->dqptr_sem);
	if (copy_to_user(addr, &fmt, sizeof(fmt)))
		return -EFAULT;
	return 0;
}

static int quota_getinfo(struct super_block *sb, int type, void __user *addr)
{
	struct if_dqinfo info;
	int ret;

	if (!sb_has_quota_active(sb, type))
		return -ESRCH;
	if (!sb->s_qcop->get_info)
		return -ENOSYS;
	ret = sb->s_qcop->get_info(sb, type, &info);
	if (!ret && copy_to_user(addr, &info, sizeof(info)))
		return -EFAULT;
	return ret;
}

static int quota_setinfo(struct super_block *sb, int type, void __user *addr)
{
	struct if_dqinfo info;

	if (copy_from_user(&info, addr, sizeof(info)))
		return -EFAULT;
	if (!sb_has_quota_active(sb, type))
		return -ESRCH;
	if (!sb->s_qcop->set_info)
		return -ENOSYS;
	return sb->s_qcop->set_info(sb, type, &info);
}

static int quota_getquota(struct super_block *sb, int type, qid_t id,
			  void __user *addr)
{
	struct if_dqblk idq;
	int ret;

	if (!sb_has_quota_active(sb, type))
		return -ESRCH;
	if (!sb->s_qcop->get_dqblk)
		return -ENOSYS;
	ret = sb->s_qcop->get_dqblk(sb, type, id, &idq);
	if (ret)
		return ret;
	if (copy_to_user(addr, &idq, sizeof(idq)))
		return -EFAULT;
	return 0;
}

static int quota_setquota(struct super_block *sb, int type, qid_t id,
			  void __user *addr)
{
	struct if_dqblk idq;

	if (copy_from_user(&idq, addr, sizeof(idq)))
		return -EFAULT;
	if (!sb_has_quota_active(sb, type))
		return -ESRCH;
	if (!sb->s_qcop->set_dqblk)
		return -ENOSYS;
	return sb->s_qcop->set_dqblk(sb, type, id, &idq);
}

static int quota_setxstate(struct super_block *sb, int cmd, void __user *addr)
{
	__u32 flags;

	if (copy_from_user(&flags, addr, sizeof(flags)))
		return -EFAULT;
	if (!sb->s_qcop->set_xstate)
		return -ENOSYS;
	return sb->s_qcop->set_xstate(sb, flags, cmd);
}

static int quota_getxstate(struct super_block *sb, void __user *addr)
{
	struct fs_quota_stat fqs;
	int ret;

	if (!sb->s_qcop->get_xstate)
		return -ENOSYS;
	ret = sb->s_qcop->get_xstate(sb, &fqs);
	if (!ret && copy_to_user(addr, &fqs, sizeof(fqs)))
		return -EFAULT;
	return ret;
}

static int quota_setxquota(struct super_block *sb, int type, qid_t id,
			   void __user *addr)
{
	struct fs_disk_quota fdq;

	if (copy_from_user(&fdq, addr, sizeof(fdq)))
		return -EFAULT;
	if (!sb->s_qcop->set_xquota)
		return -ENOSYS;
	return sb->s_qcop->set_xquota(sb, type, id, &fdq);
}

static int quota_getxquota(struct super_block *sb, int type, qid_t id,
			   void __user *addr)
{
	struct fs_disk_quota fdq;
	int ret;

	if (!sb->s_qcop->get_xquota)
		return -ENOSYS;
	ret = sb->s_qcop->get_xquota(sb, type, id, &fdq);
	if (!ret && copy_to_user(addr, &fdq, sizeof(fdq)))
		return -EFAULT;
	return ret;
}

/* Copy parameters and call proper function */
static int do_quotactl(struct super_block *sb, int type, int cmd, qid_t id,
		       void __user *addr)
{
	int ret;

	if (type >= (XQM_COMMAND(cmd) ? XQM_MAXQUOTAS : MAXQUOTAS))
		return -EINVAL;
	if (!sb->s_qcop)
		return -ENOSYS;

	ret = check_quotactl_permission(sb, type, cmd, id);
	if (ret < 0)
		return ret;

	switch (cmd) {
	case Q_QUOTAON:
		return quota_quotaon(sb, type, cmd, id, addr);
	case Q_QUOTAOFF:
		if (!sb->s_qcop->quota_off)
			return -ENOSYS;
		return sb->s_qcop->quota_off(sb, type, 0);
	case Q_GETFMT:
		return quota_getfmt(sb, type, addr);
	case Q_GETINFO:
		return quota_getinfo(sb, type, addr);
	case Q_SETINFO:
		return quota_setinfo(sb, type, addr);
	case Q_GETQUOTA:
		return quota_getquota(sb, type, id, addr);
	case Q_SETQUOTA:
		return quota_setquota(sb, type, id, addr);
	case Q_SYNC:
		if (!sb->s_qcop->quota_sync)
			return -ENOSYS;
		sync_quota_sb(sb, type);
		return 0;
	case Q_XQUOTAON:
	case Q_XQUOTAOFF:
	case Q_XQUOTARM:
		return quota_setxstate(sb, cmd, addr);
	case Q_XGETQSTAT:
		return quota_getxstate(sb, addr);
	case Q_XSETQLIM:
		return quota_setxquota(sb, type, id, addr);
	case Q_XGETQUOTA:
		return quota_getxquota(sb, type, id, addr);
	case Q_XQUOTASYNC:
		if (!sb->s_qcop->quota_sync)
			return -ENOSYS;
		return sb->s_qcop->quota_sync(sb, type);
	default:
		return -EINVAL;
	}
}

/*
 * look up a superblock on which quota ops will be performed
 * - use the name of a block device to find the superblock thereon
 */
static struct super_block *quotactl_block(const char __user *special)
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
SYSCALL_DEFINE4(quotactl, unsigned int, cmd, const char __user *, special,
		qid_t, id, void __user *, addr)
{
	uint cmds, type;
	struct super_block *sb = NULL;
	int ret;

	cmds = cmd >> SUBCMDSHIFT;
	type = cmd & SUBCMDMASK;

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

	sb = quotactl_block(special);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	ret = do_quotactl(sb, type, cmds, id, addr);

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


#ifdef CONFIG_QUOTA_NETLINK_INTERFACE

/* Netlink family structure for quota */
static struct genl_family quota_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = "VFS_DQUOT",
	.version = 1,
	.maxattr = QUOTA_NL_A_MAX,
};

/**
 * quota_send_warning - Send warning to userspace about exceeded quota
 * @type: The quota type: USRQQUOTA, GRPQUOTA,...
 * @id: The user or group id of the quota that was exceeded
 * @dev: The device on which the fs is mounted (sb->s_dev)
 * @warntype: The type of the warning: QUOTA_NL_...
 *
 * This can be used by filesystems (including those which don't use
 * dquot) to send a message to userspace relating to quota limits.
 *
 */

void quota_send_warning(short type, unsigned int id, dev_t dev,
			const char warntype)
{
	static atomic_t seq;
	struct sk_buff *skb;
	void *msg_head;
	int ret;
	int msg_size = 4 * nla_total_size(sizeof(u32)) +
		       2 * nla_total_size(sizeof(u64));

	/* We have to allocate using GFP_NOFS as we are called from a
	 * filesystem performing write and thus further recursion into
	 * the fs to free some data could cause deadlocks. */
	skb = genlmsg_new(msg_size, GFP_NOFS);
	if (!skb) {
		printk(KERN_ERR
		  "VFS: Not enough memory to send quota warning.\n");
		return;
	}
	msg_head = genlmsg_put(skb, 0, atomic_add_return(1, &seq),
			&quota_genl_family, 0, QUOTA_NL_C_WARNING);
	if (!msg_head) {
		printk(KERN_ERR
		  "VFS: Cannot store netlink header in quota warning.\n");
		goto err_out;
	}
	ret = nla_put_u32(skb, QUOTA_NL_A_QTYPE, type);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64(skb, QUOTA_NL_A_EXCESS_ID, id);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u32(skb, QUOTA_NL_A_WARNING, warntype);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u32(skb, QUOTA_NL_A_DEV_MAJOR, MAJOR(dev));
	if (ret)
		goto attr_err_out;
	ret = nla_put_u32(skb, QUOTA_NL_A_DEV_MINOR, MINOR(dev));
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64(skb, QUOTA_NL_A_CAUSED_ID, current_uid());
	if (ret)
		goto attr_err_out;
	genlmsg_end(skb, msg_head);

	genlmsg_multicast(skb, 0, quota_genl_family.id, GFP_NOFS);
	return;
attr_err_out:
	printk(KERN_ERR "VFS: Not enough space to compose quota message!\n");
err_out:
	kfree_skb(skb);
}
EXPORT_SYMBOL(quota_send_warning);

static int __init quota_init(void)
{
	if (genl_register_family(&quota_genl_family) != 0)
		printk(KERN_ERR
		       "VFS: Failed to create quota netlink interface.\n");
	return 0;
};

module_init(quota_init);
#endif

