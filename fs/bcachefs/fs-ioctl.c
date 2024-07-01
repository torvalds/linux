// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "chardev.h"
#include "dirent.h"
#include "fs.h"
#include "fs-common.h"
#include "fs-ioctl.h"
#include "quota.h"

#include <linux/compat.h>
#include <linux/fsnotify.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/writeback.h>

#define FS_IOC_GOINGDOWN	     _IOR('X', 125, __u32)
#define FSOP_GOING_FLAGS_DEFAULT	0x0	/* going down */
#define FSOP_GOING_FLAGS_LOGFLUSH	0x1	/* flush log but not data */
#define FSOP_GOING_FLAGS_NOLOGFLUSH	0x2	/* don't flush log nor data */

struct flags_set {
	unsigned		mask;
	unsigned		flags;

	unsigned		projid;

	bool			set_projinherit;
	bool			projinherit;
};

static int bch2_inode_flags_set(struct btree_trans *trans,
				struct bch_inode_info *inode,
				struct bch_inode_unpacked *bi,
				void *p)
{
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	/*
	 * We're relying on btree locking here for exclusion with other ioctl
	 * calls - use the flags in the btree (@bi), not inode->i_flags:
	 */
	struct flags_set *s = p;
	unsigned newflags = s->flags;
	unsigned oldflags = bi->bi_flags & s->mask;

	if (((newflags ^ oldflags) & (BCH_INODE_append|BCH_INODE_immutable)) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return -EPERM;

	if (!S_ISREG(bi->bi_mode) &&
	    !S_ISDIR(bi->bi_mode) &&
	    (newflags & (BCH_INODE_nodump|BCH_INODE_noatime)) != newflags)
		return -EINVAL;

	if (s->set_projinherit) {
		bi->bi_fields_set &= ~(1 << Inode_opt_project);
		bi->bi_fields_set |= ((int) s->projinherit << Inode_opt_project);
	}

	bi->bi_flags &= ~s->mask;
	bi->bi_flags |= newflags;

	bi->bi_ctime = timespec_to_bch2_time(c, current_time(&inode->v));
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
	ret   = bch2_subvol_is_ro(c, inode->ei_subvol) ?:
		bch2_write_inode(c, inode, bch2_inode_flags_set, &s,
			       ATTR_CTIME);
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

	if (inode->ei_inode.bi_fields_set & (1 << Inode_opt_project))
		fa.fsx_xflags |= FS_XFLAG_PROJINHERIT;

	fa.fsx_projid = inode->ei_qid.q[QTYP_PRJ];

	if (copy_to_user(arg, &fa, sizeof(fa)))
		return -EFAULT;

	return 0;
}

static int fssetxattr_inode_update_fn(struct btree_trans *trans,
				      struct bch_inode_info *inode,
				      struct bch_inode_unpacked *bi,
				      void *p)
{
	struct flags_set *s = p;

	if (s->projid != bi->bi_project) {
		bi->bi_fields_set |= 1U << Inode_opt_project;
		bi->bi_project = s->projid;
	}

	return bch2_inode_flags_set(trans, inode, bi, p);
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

	s.set_projinherit = true;
	s.projinherit = (fa.fsx_xflags & FS_XFLAG_PROJINHERIT) != 0;
	fa.fsx_xflags &= ~FS_XFLAG_PROJINHERIT;

	s.flags = map_flags_rev(bch_flags_to_xflags, fa.fsx_xflags);
	if (fa.fsx_xflags)
		return -EOPNOTSUPP;

	if (fa.fsx_projid >= U32_MAX)
		return -EINVAL;

	/*
	 * inode fields accessible via the xattr interface are stored with a +1
	 * bias, so that 0 means unset:
	 */
	s.projid = fa.fsx_projid + 1;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(&inode->v);
	if (!inode_owner_or_capable(file_mnt_idmap(file), &inode->v)) {
		ret = -EACCES;
		goto err;
	}

	mutex_lock(&inode->ei_update_lock);
	ret   = bch2_subvol_is_ro(c, inode->ei_subvol) ?:
		bch2_set_projid(c, inode, fa.fsx_projid) ?:
		bch2_write_inode(c, inode, fssetxattr_inode_update_fn, &s,
			       ATTR_CTIME);
	mutex_unlock(&inode->ei_update_lock);
err:
	inode_unlock(&inode->v);
	mnt_drop_write_file(file);
	return ret;
}

static int bch2_reinherit_attrs_fn(struct btree_trans *trans,
				   struct bch_inode_info *inode,
				   struct bch_inode_unpacked *bi,
				   void *p)
{
	struct bch_inode_info *dir = p;

	return !bch2_reinherit_attrs(bi, &dir->ei_inode);
}

static int bch2_ioc_reinherit_attrs(struct bch_fs *c,
				    struct file *file,
				    struct bch_inode_info *src,
				    const char __user *name)
{
	struct bch_hash_info hash = bch2_hash_info_init(c, &src->ei_inode);
	struct bch_inode_info *dst;
	struct inode *vinode = NULL;
	char *kname = NULL;
	struct qstr qstr;
	int ret = 0;
	subvol_inum inum;

	kname = kmalloc(BCH_NAME_MAX + 1, GFP_KERNEL);
	if (!kname)
		return -ENOMEM;

	ret = strncpy_from_user(kname, name, BCH_NAME_MAX);
	if (unlikely(ret < 0))
		goto err1;

	qstr.len	= ret;
	qstr.name	= kname;

	ret = bch2_dirent_lookup(c, inode_inum(src), &hash, &qstr, &inum);
	if (ret)
		goto err1;

	vinode = bch2_vfs_inode_get(c, inum);
	ret = PTR_ERR_OR_ZERO(vinode);
	if (ret)
		goto err1;

	dst = to_bch_ei(vinode);

	ret = mnt_want_write_file(file);
	if (ret)
		goto err2;

	bch2_lock_inodes(INODE_UPDATE_LOCK, src, dst);

	if (inode_attr_changing(src, dst, Inode_opt_project)) {
		ret = bch2_fs_quota_transfer(c, dst,
					     src->ei_qid,
					     1 << QTYP_PRJ,
					     KEY_TYPE_QUOTA_PREALLOC);
		if (ret)
			goto err3;
	}

	ret = bch2_write_inode(c, dst, bch2_reinherit_attrs_fn, src, 0);
err3:
	bch2_unlock_inodes(INODE_UPDATE_LOCK, src, dst);

	/* return true if we did work */
	if (ret >= 0)
		ret = !ret;

	mnt_drop_write_file(file);
err2:
	iput(vinode);
err1:
	kfree(kname);

	return ret;
}

static int bch2_ioc_goingdown(struct bch_fs *c, u32 __user *arg)
{
	u32 flags;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, arg))
		return -EFAULT;

	bch_notice(c, "shutdown by ioctl type %u", flags);

	switch (flags) {
	case FSOP_GOING_FLAGS_DEFAULT:
		ret = bdev_freeze(c->vfs_sb->s_bdev);
		if (ret)
			break;
		bch2_journal_flush(&c->journal);
		bch2_fs_emergency_read_only(c);
		bdev_thaw(c->vfs_sb->s_bdev);
		break;
	case FSOP_GOING_FLAGS_LOGFLUSH:
		bch2_journal_flush(&c->journal);
		fallthrough;
	case FSOP_GOING_FLAGS_NOLOGFLUSH:
		bch2_fs_emergency_read_only(c);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static long bch2_ioctl_subvolume_create(struct bch_fs *c, struct file *filp,
					struct bch_ioctl_subvolume arg)
{
	struct inode *dir;
	struct bch_inode_info *inode;
	struct user_namespace *s_user_ns;
	struct dentry *dst_dentry;
	struct path src_path, dst_path;
	int how = LOOKUP_FOLLOW;
	int error;
	subvol_inum snapshot_src = { 0 };
	unsigned lookup_flags = 0;
	unsigned create_flags = BCH_CREATE_SUBVOL;

	if (arg.flags & ~(BCH_SUBVOL_SNAPSHOT_CREATE|
			  BCH_SUBVOL_SNAPSHOT_RO))
		return -EINVAL;

	if (!(arg.flags & BCH_SUBVOL_SNAPSHOT_CREATE) &&
	    (arg.src_ptr ||
	     (arg.flags & BCH_SUBVOL_SNAPSHOT_RO)))
		return -EINVAL;

	if (arg.flags & BCH_SUBVOL_SNAPSHOT_CREATE)
		create_flags |= BCH_CREATE_SNAPSHOT;

	if (arg.flags & BCH_SUBVOL_SNAPSHOT_RO)
		create_flags |= BCH_CREATE_SNAPSHOT_RO;

	if (arg.flags & BCH_SUBVOL_SNAPSHOT_CREATE) {
		/* sync_inodes_sb enforce s_umount is locked */
		down_read(&c->vfs_sb->s_umount);
		sync_inodes_sb(c->vfs_sb);
		up_read(&c->vfs_sb->s_umount);
	}
retry:
	if (arg.src_ptr) {
		error = user_path_at(arg.dirfd,
				(const char __user *)(unsigned long)arg.src_ptr,
				how, &src_path);
		if (error)
			goto err1;

		if (src_path.dentry->d_sb->s_fs_info != c) {
			path_put(&src_path);
			error = -EXDEV;
			goto err1;
		}

		snapshot_src = inode_inum(to_bch_ei(src_path.dentry->d_inode));
	}

	dst_dentry = user_path_create(arg.dirfd,
			(const char __user *)(unsigned long)arg.dst_ptr,
			&dst_path, lookup_flags);
	error = PTR_ERR_OR_ZERO(dst_dentry);
	if (error)
		goto err2;

	if (dst_dentry->d_sb->s_fs_info != c) {
		error = -EXDEV;
		goto err3;
	}

	if (dst_dentry->d_inode) {
		error = -BCH_ERR_EEXIST_subvolume_create;
		goto err3;
	}

	dir = dst_path.dentry->d_inode;
	if (IS_DEADDIR(dir)) {
		error = -BCH_ERR_ENOENT_directory_dead;
		goto err3;
	}

	s_user_ns = dir->i_sb->s_user_ns;
	if (!kuid_has_mapping(s_user_ns, current_fsuid()) ||
	    !kgid_has_mapping(s_user_ns, current_fsgid())) {
		error = -EOVERFLOW;
		goto err3;
	}

	error = inode_permission(file_mnt_idmap(filp),
				 dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto err3;

	if (!IS_POSIXACL(dir))
		arg.mode &= ~current_umask();

	error = security_path_mkdir(&dst_path, dst_dentry, arg.mode);
	if (error)
		goto err3;

	if ((arg.flags & BCH_SUBVOL_SNAPSHOT_CREATE) &&
	    !arg.src_ptr)
		snapshot_src.subvol = inode_inum(to_bch_ei(dir)).subvol;

	down_write(&c->snapshot_create_lock);
	inode = __bch2_create(file_mnt_idmap(filp), to_bch_ei(dir),
			      dst_dentry, arg.mode|S_IFDIR,
			      0, snapshot_src, create_flags);
	up_write(&c->snapshot_create_lock);

	error = PTR_ERR_OR_ZERO(inode);
	if (error)
		goto err3;

	d_instantiate(dst_dentry, &inode->v);
	fsnotify_mkdir(dir, dst_dentry);
err3:
	done_path_create(&dst_path, dst_dentry);
err2:
	if (arg.src_ptr)
		path_put(&src_path);

	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
err1:
	return error;
}

static long bch2_ioctl_subvolume_destroy(struct bch_fs *c, struct file *filp,
				struct bch_ioctl_subvolume arg)
{
	const char __user *name = (void __user *)(unsigned long)arg.dst_ptr;
	struct path path;
	struct inode *dir;
	struct dentry *victim;
	int ret = 0;

	if (arg.flags)
		return -EINVAL;

	victim = user_path_locked_at(arg.dirfd, name, &path);
	if (IS_ERR(victim))
		return PTR_ERR(victim);

	dir = d_inode(path.dentry);
	if (victim->d_sb->s_fs_info != c) {
		ret = -EXDEV;
		goto err;
	}
	if (!d_is_positive(victim)) {
		ret = -ENOENT;
		goto err;
	}
	ret = __bch2_unlink(dir, victim, true);
	if (!ret) {
		fsnotify_rmdir(dir, victim);
		d_delete(victim);
	}
err:
	inode_unlock(dir);
	dput(victim);
	path_put(&path);
	return ret;
}

long bch2_fs_file_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	long ret;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		ret = bch2_ioc_getflags(inode, (int __user *) arg);
		break;

	case FS_IOC_SETFLAGS:
		ret = bch2_ioc_setflags(c, file, inode, (int __user *) arg);
		break;

	case FS_IOC_FSGETXATTR:
		ret = bch2_ioc_fsgetxattr(inode, (void __user *) arg);
		break;

	case FS_IOC_FSSETXATTR:
		ret = bch2_ioc_fssetxattr(c, file, inode,
					  (void __user *) arg);
		break;

	case BCHFS_IOC_REINHERIT_ATTRS:
		ret = bch2_ioc_reinherit_attrs(c, file, inode,
					       (void __user *) arg);
		break;

	case FS_IOC_GETVERSION:
		ret = -ENOTTY;
		break;

	case FS_IOC_SETVERSION:
		ret = -ENOTTY;
		break;

	case FS_IOC_GOINGDOWN:
		ret = bch2_ioc_goingdown(c, (u32 __user *) arg);
		break;

	case BCH_IOCTL_SUBVOLUME_CREATE: {
		struct bch_ioctl_subvolume i;

		ret = copy_from_user(&i, (void __user *) arg, sizeof(i))
			? -EFAULT
			: bch2_ioctl_subvolume_create(c, file, i);
		break;
	}

	case BCH_IOCTL_SUBVOLUME_DESTROY: {
		struct bch_ioctl_subvolume i;

		ret = copy_from_user(&i, (void __user *) arg, sizeof(i))
			? -EFAULT
			: bch2_ioctl_subvolume_destroy(c, file, i);
		break;
	}

	default:
		ret = bch2_fs_ioctl(c, cmd, (void __user *) arg);
		break;
	}

	return bch2_err_class(ret);
}

#ifdef CONFIG_COMPAT
long bch2_compat_fs_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
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
