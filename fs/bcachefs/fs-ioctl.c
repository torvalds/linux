// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "chardev.h"
#include "fs.h"
#include "fs-ioctl.h"
#include "quota.h"

#include <linux/compat.h>
#include <linux/mount.h>

#define FS_IOC_GOINGDOWN	     _IOR('X', 125, __u32)

/* Inode flags: */

/* bcachefs inode flags -> vfs inode flags: */
static const unsigned bch_flags_to_vfs[] = {
	[__BCH_INODE_SYNC]	= S_SYNC,
	[__BCH_INODE_IMMUTABLE]	= S_IMMUTABLE,
	[__BCH_INODE_APPEND]	= S_APPEND,
	[__BCH_INODE_NOATIME]	= S_NOATIME,
};

/* bcachefs inode flags -> FS_IOC_GETFLAGS: */
static const unsigned bch_flags_to_uflags[] = {
	[__BCH_INODE_SYNC]	= FS_SYNC_FL,
	[__BCH_INODE_IMMUTABLE]	= FS_IMMUTABLE_FL,
	[__BCH_INODE_APPEND]	= FS_APPEND_FL,
	[__BCH_INODE_NODUMP]	= FS_NODUMP_FL,
	[__BCH_INODE_NOATIME]	= FS_NOATIME_FL,
};

/* bcachefs inode flags -> FS_IOC_FSGETXATTR: */
static const unsigned bch_flags_to_xflags[] = {
	[__BCH_INODE_SYNC]	= FS_XFLAG_SYNC,
	[__BCH_INODE_IMMUTABLE]	= FS_XFLAG_IMMUTABLE,
	[__BCH_INODE_APPEND]	= FS_XFLAG_APPEND,
	[__BCH_INODE_NODUMP]	= FS_XFLAG_NODUMP,
	[__BCH_INODE_NOATIME]	= FS_XFLAG_NOATIME,
	//[__BCH_INODE_PROJINHERIT] = FS_XFLAG_PROJINHERIT;
};

#define set_flags(_map, _in, _out)					\
do {									\
	unsigned _i;							\
									\
	for (_i = 0; _i < ARRAY_SIZE(_map); _i++)			\
		if ((_in) & (1 << _i))					\
			(_out) |= _map[_i];				\
		else							\
			(_out) &= ~_map[_i];				\
} while (0)

#define map_flags(_map, _in)						\
({									\
	unsigned _out = 0;						\
									\
	set_flags(_map, _in, _out);					\
	_out;								\
})

#define map_flags_rev(_map, _in)					\
({									\
	unsigned _i, _out = 0;						\
									\
	for (_i = 0; _i < ARRAY_SIZE(_map); _i++)			\
		if ((_in) & _map[_i]) {					\
			(_out) |= 1 << _i;				\
			(_in) &= ~_map[_i];				\
		}							\
	(_out);								\
})

#define map_defined(_map)						\
({									\
	unsigned _in = ~0;						\
									\
	map_flags_rev(_map, _in);					\
})

/* Set VFS inode flags from bcachefs inode: */
void bch2_inode_flags_to_vfs(struct bch_inode_info *inode)
{
	set_flags(bch_flags_to_vfs, inode->ei_inode.bi_flags, inode->v.i_flags);
}

struct flags_set {
	unsigned		mask;
	unsigned		flags;

	unsigned		projid;
};

static int bch2_inode_flags_set(struct bch_inode_info *inode,
				struct bch_inode_unpacked *bi,
				void *p)
{
	/*
	 * We're relying on btree locking here for exclusion with other ioctl
	 * calls - use the flags in the btree (@bi), not inode->i_flags:
	 */
	struct flags_set *s = p;
	unsigned newflags = s->flags;
	unsigned oldflags = bi->bi_flags & s->mask;

	if (((newflags ^ oldflags) & (BCH_INODE_APPEND|BCH_INODE_IMMUTABLE)) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return -EPERM;

	if (!S_ISREG(inode->v.i_mode) &&
	    !S_ISDIR(inode->v.i_mode) &&
	    (newflags & (BCH_INODE_NODUMP|BCH_INODE_NOATIME)) != newflags)
		return -EINVAL;

	bi->bi_flags &= ~s->mask;
	bi->bi_flags |= newflags;
	inode_set_ctime_current(&inode->v);
	return 0;
}

static int bch2_ioc_getflags(struct bch_inode_info *inode, int __user *arg)
{
	unsigned flags = map_flags(bch_flags_to_uflags, inode->ei_inode.bi_flags);

	return put_user(flags, arg);
}

static int bch2_ioc_setflags(struct bch_fs *c,
			     struct file *file,
			     struct bch_inode_info *inode,
			     void __user *arg)
{
	struct flags_set s = { .mask = map_defined(bch_flags_to_uflags) };
	unsigned uflags;
	int ret;

	if (get_user(uflags, (int __user *) arg))
		return -EFAULT;

	s.flags = map_flags_rev(bch_flags_to_uflags, uflags);
	if (uflags)
		return -EOPNOTSUPP;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(&inode->v);
	if (!inode_owner_or_capable(file_mnt_idmap(file), &inode->v)) {
		ret = -EACCES;
		goto setflags_out;
	}

	mutex_lock(&inode->ei_update_lock);
	ret = __bch2_write_inode(c, inode, bch2_inode_flags_set, &s, 0);

	if (!ret)
		bch2_inode_flags_to_vfs(inode);
	mutex_unlock(&inode->ei_update_lock);

setflags_out:
	inode_unlock(&inode->v);
	mnt_drop_write_file(file);
	return ret;
}

static int bch2_ioc_fsgetxattr(struct bch_inode_info *inode,
			       struct fsxattr __user *arg)
{
	struct fsxattr fa = { 0 };

	fa.fsx_xflags = map_flags(bch_flags_to_xflags, inode->ei_inode.bi_flags);
	fa.fsx_projid = inode->ei_qid.q[QTYP_PRJ];

	return copy_to_user(arg, &fa, sizeof(fa));
}

static int bch2_set_projid(struct bch_fs *c,
			   struct bch_inode_info *inode,
			   u32 projid)
{
	struct bch_qid qid = inode->ei_qid;
	int ret;

	if (projid == inode->ei_qid.q[QTYP_PRJ])
		return 0;

	qid.q[QTYP_PRJ] = projid;

	return bch2_quota_transfer(c, 1 << QTYP_PRJ, qid, inode->ei_qid,
				   inode->v.i_blocks +
				   inode->ei_quota_reserved);
	if (ret)
		return ret;

	inode->ei_qid.q[QTYP_PRJ] = projid;
	return 0;
}

static int fssetxattr_inode_update_fn(struct bch_inode_info *inode,
				      struct bch_inode_unpacked *bi,
				      void *p)
{
	struct flags_set *s = p;

	bi->bi_project = s->projid;

	return bch2_inode_flags_set(inode, bi, p);
}

static int bch2_ioc_fssetxattr(struct bch_fs *c,
			       struct file *file,
			       struct bch_inode_info *inode,
			       struct fsxattr __user *arg)
{
	struct flags_set s = { .mask = map_defined(bch_flags_to_xflags) };
	struct fsxattr fa;
	int ret;

	if (copy_from_user(&fa, arg, sizeof(fa)))
		return -EFAULT;

	s.flags = map_flags_rev(bch_flags_to_xflags, fa.fsx_xflags);
	if (fa.fsx_xflags)
		return -EOPNOTSUPP;

	s.projid = fa.fsx_projid;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(&inode->v);
	if (!inode_owner_or_capable(file_mnt_idmap(file), &inode->v)) {
		ret = -EACCES;
		goto err;
	}

	mutex_lock(&inode->ei_update_lock);
	ret = bch2_set_projid(c, inode, fa.fsx_projid);
	if (ret)
		goto err_unlock;

	ret = __bch2_write_inode(c, inode, fssetxattr_inode_update_fn, &s, 0);
	if (!ret)
		bch2_inode_flags_to_vfs(inode);
err_unlock:
	mutex_unlock(&inode->ei_update_lock);
err:
	inode_unlock(&inode->v);
	mnt_drop_write_file(file);
	return ret;
}

long bch2_fs_file_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct super_block *sb = inode->v.i_sb;
	struct bch_fs *c = sb->s_fs_info;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return bch2_ioc_getflags(inode, (int __user *) arg);

	case FS_IOC_SETFLAGS:
		return bch2_ioc_setflags(c, file, inode, (int __user *) arg);

	case FS_IOC_FSGETXATTR:
		return bch2_ioc_fsgetxattr(inode, (void __user *) arg);
	case FS_IOC_FSSETXATTR:
		return bch2_ioc_fssetxattr(c, file, inode, (void __user *) arg);

	case FS_IOC_GETVERSION:
		return -ENOTTY;
	case FS_IOC_SETVERSION:
		return -ENOTTY;

	case FS_IOC_GOINGDOWN:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		down_write(&sb->s_umount);
		sb->s_flags |= SB_RDONLY;
		bch2_fs_emergency_read_only(c);
		up_write(&sb->s_umount);
		return 0;

	default:
		return bch2_fs_ioctl(c, cmd, (void __user *) arg);
	}
}

#ifdef CONFIG_COMPAT
long bch2_compat_fs_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case FS_IOC_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return bch2_fs_file_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

#endif /* NO_BCACHEFS_FS */
