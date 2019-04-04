#include <linux/btrfs.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/uidgid.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/uio.h>

struct shiftfs_super_info {
	struct vfsmount *mnt;
	struct user_namespace *userns;
	/* creds of process who created the super block */
	const struct cred *creator_cred;
	bool mark;
	unsigned int passthrough;
	struct shiftfs_super_info *info_mark;
};

struct shiftfs_file_info {
	struct path realpath;
	struct file *realfile;
};

struct kmem_cache *shiftfs_file_info_cache;

static void shiftfs_fill_inode(struct inode *inode, unsigned long ino,
			       umode_t mode, dev_t dev, struct dentry *dentry);

#define SHIFTFS_PASSTHROUGH_NONE 0
#define SHIFTFS_PASSTHROUGH_STAT 1
#define SHIFTFS_PASSTHROUGH_IOCTL 2
#define SHIFTFS_PASSTHROUGH_ALL                                                \
	(SHIFTFS_PASSTHROUGH_STAT | SHIFTFS_PASSTHROUGH_IOCTL)

static inline bool shiftfs_passthrough_ioctls(struct shiftfs_super_info *info)
{
	if (!(info->passthrough & SHIFTFS_PASSTHROUGH_IOCTL))
		return false;

	if (info->info_mark &&
	    !(info->info_mark->passthrough & SHIFTFS_PASSTHROUGH_IOCTL))
		return false;

	return true;
}

static inline bool shiftfs_passthrough_statfs(struct shiftfs_super_info *info)
{
	if (!(info->passthrough & SHIFTFS_PASSTHROUGH_STAT))
		return false;

	if (info->info_mark &&
	    !(info->info_mark->passthrough & SHIFTFS_PASSTHROUGH_STAT))
		return false;

	return true;
}

enum {
	OPT_MARK,
	OPT_PASSTHROUGH,
	OPT_LAST,
};

/* global filesystem options */
static const match_table_t tokens = {
	{ OPT_MARK, "mark" },
	{ OPT_PASSTHROUGH, "passthrough=%u" },
	{ OPT_LAST, NULL }
};

static const struct cred *shiftfs_override_creds(const struct super_block *sb)
{
	struct shiftfs_super_info *sbinfo = sb->s_fs_info;

	return override_creds(sbinfo->creator_cred);
}

static inline void shiftfs_revert_object_creds(const struct cred *oldcred,
					       struct cred *newcred)
{
	revert_creds(oldcred);
	put_cred(newcred);
}

static int shiftfs_override_object_creds(const struct super_block *sb,
					 const struct cred **oldcred,
					 struct cred **newcred,
					 struct dentry *dentry, umode_t mode,
					 bool hardlink)
{
	kuid_t fsuid = current_fsuid();
	kgid_t fsgid = current_fsgid();

	*oldcred = shiftfs_override_creds(sb);

	*newcred = prepare_creds();
	if (!*newcred) {
		revert_creds(*oldcred);
		return -ENOMEM;
	}

	(*newcred)->fsuid = KUIDT_INIT(from_kuid(sb->s_user_ns, fsuid));
	(*newcred)->fsgid = KGIDT_INIT(from_kgid(sb->s_user_ns, fsgid));

	if (!hardlink) {
		int err = security_dentry_create_files_as(dentry, mode,
							  &dentry->d_name,
							  *oldcred, *newcred);
		if (err) {
			shiftfs_revert_object_creds(*oldcred, *newcred);
			return err;
		}
	}

	put_cred(override_creds(*newcred));
	return 0;
}

static kuid_t shift_kuid(struct user_namespace *from, struct user_namespace *to,
			 kuid_t kuid)
{
	uid_t uid = from_kuid(from, kuid);
	return make_kuid(to, uid);
}

static kgid_t shift_kgid(struct user_namespace *from, struct user_namespace *to,
			 kgid_t kgid)
{
	gid_t gid = from_kgid(from, kgid);
	return make_kgid(to, gid);
}

static void shiftfs_copyattr(struct inode *from, struct inode *to)
{
	struct user_namespace *from_ns = from->i_sb->s_user_ns;
	struct user_namespace *to_ns = to->i_sb->s_user_ns;

	to->i_uid = shift_kuid(from_ns, to_ns, from->i_uid);
	to->i_gid = shift_kgid(from_ns, to_ns, from->i_gid);
	to->i_mode = from->i_mode;
	to->i_atime = from->i_atime;
	to->i_mtime = from->i_mtime;
	to->i_ctime = from->i_ctime;
	i_size_write(to, i_size_read(from));
}

static void shiftfs_copyflags(struct inode *from, struct inode *to)
{
	unsigned int mask = S_SYNC | S_IMMUTABLE | S_APPEND | S_NOATIME;

	inode_set_flags(to, from->i_flags & mask, mask);
}

static void shiftfs_file_accessed(struct file *file)
{
	struct inode *upperi, *loweri;

	if (file->f_flags & O_NOATIME)
		return;

	upperi = file_inode(file);
	loweri = upperi->i_private;

	if (!loweri)
		return;

	upperi->i_mtime = loweri->i_mtime;
	upperi->i_ctime = loweri->i_ctime;

	touch_atime(&file->f_path);
}

static int shiftfs_parse_mount_options(struct shiftfs_super_info *sbinfo,
				       char *options)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];

	sbinfo->mark = false;
	sbinfo->passthrough = 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int err, intarg, token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case OPT_MARK:
			sbinfo->mark = true;
			break;
		case OPT_PASSTHROUGH:
			err = match_int(&args[0], &intarg);
			if (err)
				return err;

			if (intarg & ~SHIFTFS_PASSTHROUGH_ALL)
				return -EINVAL;

			sbinfo->passthrough = intarg;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static void shiftfs_d_release(struct dentry *dentry)
{
	struct dentry *lowerd = dentry->d_fsdata;

	if (lowerd)
		dput(lowerd);
}

static struct dentry *shiftfs_d_real(struct dentry *dentry,
				     const struct inode *inode)
{
	struct dentry *lowerd = dentry->d_fsdata;

	if (inode && d_inode(dentry) == inode)
		return dentry;

	lowerd = d_real(lowerd, inode);
	if (lowerd && (!inode || inode == d_inode(lowerd)))
		return lowerd;

	WARN(1, "shiftfs_d_real(%pd4, %s:%lu): real dentry not found\n", dentry,
	     inode ? inode->i_sb->s_id : "NULL", inode ? inode->i_ino : 0);
	return dentry;
}

static int shiftfs_d_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	int err = 1;
	struct dentry *lowerd = dentry->d_fsdata;

	if (d_is_negative(lowerd) != d_is_negative(dentry))
		return 0;

	if ((lowerd->d_flags & DCACHE_OP_WEAK_REVALIDATE))
		err = lowerd->d_op->d_weak_revalidate(lowerd, flags);

	if (d_really_is_positive(dentry)) {
		struct inode *inode = d_inode(dentry);
		struct inode *loweri = d_inode(lowerd);

		shiftfs_copyattr(loweri, inode);
		if (!inode->i_nlink)
			err = 0;
	}

	return err;
}

static int shiftfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int err = 1;
	struct dentry *lowerd = dentry->d_fsdata;

	if (d_unhashed(lowerd) ||
	    ((d_is_negative(lowerd) != d_is_negative(dentry))))
		return 0;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if ((lowerd->d_flags & DCACHE_OP_REVALIDATE))
		err = lowerd->d_op->d_revalidate(lowerd, flags);

	if (d_really_is_positive(dentry)) {
		struct inode *inode = d_inode(dentry);
		struct inode *loweri = d_inode(lowerd);

		shiftfs_copyattr(loweri, inode);
		if (!inode->i_nlink)
			err = 0;
	}

	return err;
}

static const struct dentry_operations shiftfs_dentry_ops = {
	.d_release	   = shiftfs_d_release,
	.d_real		   = shiftfs_d_real,
	.d_revalidate	   = shiftfs_d_revalidate,
	.d_weak_revalidate = shiftfs_d_weak_revalidate,
};

static const char *shiftfs_get_link(struct dentry *dentry, struct inode *inode,
				    struct delayed_call *done)
{
	const char *p;
	const struct cred *oldcred;
	struct dentry *lowerd;

	/* RCU lookup not supported */
	if (!dentry)
		return ERR_PTR(-ECHILD);

	lowerd = dentry->d_fsdata;
	oldcred = shiftfs_override_creds(dentry->d_sb);
	p = vfs_get_link(lowerd, done);
	revert_creds(oldcred);

	return p;
}

static int shiftfs_setxattr(struct dentry *dentry, struct inode *inode,
			    const char *name, const void *value,
			    size_t size, int flags)
{
	struct dentry *lowerd = dentry->d_fsdata;
	int err;
	const struct cred *oldcred;

	oldcred = shiftfs_override_creds(dentry->d_sb);
	err = vfs_setxattr(lowerd, name, value, size, flags);
	revert_creds(oldcred);

	shiftfs_copyattr(lowerd->d_inode, inode);

	return err;
}

static int shiftfs_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, void *value, size_t size)
{
	struct dentry *lowerd = dentry->d_fsdata;
	int err;
	const struct cred *oldcred;

	oldcred = shiftfs_override_creds(dentry->d_sb);
	err = vfs_getxattr(lowerd, name, value, size);
	revert_creds(oldcred);

	return err;
}

static ssize_t shiftfs_listxattr(struct dentry *dentry, char *list,
				 size_t size)
{
	struct dentry *lowerd = dentry->d_fsdata;
	int err;
	const struct cred *oldcred;

	oldcred = shiftfs_override_creds(dentry->d_sb);
	err = vfs_listxattr(lowerd, list, size);
	revert_creds(oldcred);

	return err;
}

static int shiftfs_removexattr(struct dentry *dentry, const char *name)
{
	struct dentry *lowerd = dentry->d_fsdata;
	int err;
	const struct cred *oldcred;

	oldcred = shiftfs_override_creds(dentry->d_sb);
	err = vfs_removexattr(lowerd, name);
	revert_creds(oldcred);

	/* update c/mtime */
	shiftfs_copyattr(lowerd->d_inode, d_inode(dentry));

	return err;
}

static int shiftfs_xattr_set(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, const void *value, size_t size,
			     int flags)
{
	if (!value)
		return shiftfs_removexattr(dentry, name);
	return shiftfs_setxattr(dentry, inode, name, value, size, flags);
}

static int shiftfs_inode_test(struct inode *inode, void *data)
{
	return inode->i_private == data;
}

static int shiftfs_inode_set(struct inode *inode, void *data)
{
	inode->i_private = data;
	return 0;
}

static int shiftfs_create_object(struct inode *diri, struct dentry *dentry,
				 umode_t mode, const char *symlink,
				 struct dentry *hardlink, bool excl)
{
	int err;
	const struct cred *oldcred;
	struct cred *newcred;
	void *loweri_iop_ptr = NULL;
	umode_t modei = mode;
	struct super_block *dir_sb = diri->i_sb;
	struct dentry *lowerd_new = dentry->d_fsdata;
	struct inode *inode = NULL, *loweri_dir = diri->i_private;
	const struct inode_operations *loweri_dir_iop = loweri_dir->i_op;
	struct dentry *lowerd_link = NULL;

	if (hardlink) {
		loweri_iop_ptr = loweri_dir_iop->link;
	} else {
		switch (mode & S_IFMT) {
		case S_IFDIR:
			loweri_iop_ptr = loweri_dir_iop->mkdir;
			break;
		case S_IFREG:
			loweri_iop_ptr = loweri_dir_iop->create;
			break;
		case S_IFLNK:
			loweri_iop_ptr = loweri_dir_iop->symlink;
			break;
		case S_IFSOCK:
			/* fall through */
		case S_IFIFO:
			loweri_iop_ptr = loweri_dir_iop->mknod;
			break;
		}
	}
	if (!loweri_iop_ptr) {
		err = -EINVAL;
		goto out_iput;
	}

	inode_lock_nested(loweri_dir, I_MUTEX_PARENT);

	if (!hardlink) {
		inode = new_inode(dir_sb);
		if (!inode) {
			err = -ENOMEM;
			goto out_iput;
		}

		/*
		 * new_inode() will have added the new inode to the super
		 * block's list of inodes. Further below we will call
		 * inode_insert5() Which would perform the same operation again
		 * thereby corrupting the list. To avoid this raise I_CREATING
		 * in i_state which will cause inode_insert5() to skip this
		 * step. I_CREATING will be cleared by d_instantiate_new()
		 * below.
		 */
		spin_lock(&inode->i_lock);
		inode->i_state |= I_CREATING;
		spin_unlock(&inode->i_lock);

		inode_init_owner(inode, diri, mode);
		modei = inode->i_mode;
	}

	err = shiftfs_override_object_creds(dentry->d_sb, &oldcred, &newcred,
					    dentry, modei, hardlink != NULL);
	if (err)
		goto out_iput;

	if (hardlink) {
		lowerd_link = hardlink->d_fsdata;
		err = vfs_link(lowerd_link, loweri_dir, lowerd_new, NULL);
	} else {
		switch (modei & S_IFMT) {
		case S_IFDIR:
			err = vfs_mkdir(loweri_dir, lowerd_new, modei);
			break;
		case S_IFREG:
			err = vfs_create(loweri_dir, lowerd_new, modei, excl);
			break;
		case S_IFLNK:
			err = vfs_symlink(loweri_dir, lowerd_new, symlink);
			break;
		case S_IFSOCK:
			/* fall through */
		case S_IFIFO:
			err = vfs_mknod(loweri_dir, lowerd_new, modei, 0);
			break;
		default:
			err = -EINVAL;
			break;
		}
	}

	shiftfs_revert_object_creds(oldcred, newcred);

	if (!err && WARN_ON(!lowerd_new->d_inode))
		err = -EIO;
	if (err)
		goto out_iput;

	if (hardlink) {
		inode = d_inode(hardlink);
		ihold(inode);

		/* copy up times from lower inode */
		shiftfs_copyattr(d_inode(lowerd_link), inode);
		set_nlink(d_inode(hardlink), d_inode(lowerd_link)->i_nlink);
		d_instantiate(dentry, inode);
	} else {
		struct inode *inode_tmp;
		struct inode *loweri_new = d_inode(lowerd_new);

		inode_tmp = inode_insert5(inode, (unsigned long)loweri_new,
					  shiftfs_inode_test, shiftfs_inode_set,
					  loweri_new);
		if (unlikely(inode_tmp != inode)) {
			pr_err_ratelimited("shiftfs: newly created inode found in cache\n");
			iput(inode_tmp);
			err = -EINVAL;
			goto out_iput;
		}

		ihold(loweri_new);
		shiftfs_fill_inode(inode, loweri_new->i_ino, loweri_new->i_mode,
				   0, lowerd_new);
		d_instantiate_new(dentry, inode);
	}

	shiftfs_copyattr(loweri_dir, diri);
	if (loweri_iop_ptr == loweri_dir_iop->mkdir)
		set_nlink(diri, loweri_dir->i_nlink);

	inode = NULL;

out_iput:
	iput(inode);
	inode_unlock(loweri_dir);

	return err;
}

static int shiftfs_create(struct inode *dir, struct dentry *dentry,
			  umode_t mode,  bool excl)
{
	mode |= S_IFREG;

	return shiftfs_create_object(dir, dentry, mode, NULL, NULL, excl);
}

static int shiftfs_mkdir(struct inode *dir, struct dentry *dentry,
			 umode_t mode)
{
	mode |= S_IFDIR;

	return shiftfs_create_object(dir, dentry, mode, NULL, NULL, false);
}

static int shiftfs_link(struct dentry *hardlink, struct inode *dir,
			struct dentry *dentry)
{
	return shiftfs_create_object(dir, dentry, 0, NULL, hardlink, false);
}

static int shiftfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
			 dev_t rdev)
{
	if (!S_ISFIFO(mode) && !S_ISSOCK(mode))
		return -EPERM;

	return shiftfs_create_object(dir, dentry, mode, NULL, NULL, false);
}

static int shiftfs_symlink(struct inode *dir, struct dentry *dentry,
			   const char *symlink)
{
	return shiftfs_create_object(dir, dentry, S_IFLNK, symlink, NULL, false);
}

static int shiftfs_rm(struct inode *dir, struct dentry *dentry, bool rmdir)
{
	struct dentry *lowerd = dentry->d_fsdata;
	struct inode *loweri = dir->i_private;
	int err;
	const struct cred *oldcred;

	oldcred = shiftfs_override_creds(dentry->d_sb);
	inode_lock_nested(loweri, I_MUTEX_PARENT);
	if (rmdir)
		err = vfs_rmdir(loweri, lowerd);
	else
		err = vfs_unlink(loweri, lowerd, NULL);
	inode_unlock(loweri);
	revert_creds(oldcred);

	shiftfs_copyattr(loweri, dir);
	set_nlink(d_inode(dentry), loweri->i_nlink);
	if (!err)
		d_drop(dentry);

	set_nlink(dir, loweri->i_nlink);

	return err;
}

static int shiftfs_unlink(struct inode *dir, struct dentry *dentry)
{
	return shiftfs_rm(dir, dentry, false);
}

static int shiftfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	return shiftfs_rm(dir, dentry, true);
}

static int shiftfs_rename(struct inode *olddir, struct dentry *old,
			  struct inode *newdir, struct dentry *new,
			  unsigned int flags)
{
	struct dentry *lowerd_dir_old = old->d_parent->d_fsdata,
		      *lowerd_dir_new = new->d_parent->d_fsdata,
		      *lowerd_old = old->d_fsdata, *lowerd_new = new->d_fsdata,
		      *trapd;
	struct inode *loweri_dir_old = lowerd_dir_old->d_inode,
		     *loweri_dir_new = lowerd_dir_new->d_inode;
	int err = -EINVAL;
	const struct cred *oldcred;

	trapd = lock_rename(lowerd_dir_new, lowerd_dir_old);

	if (trapd == lowerd_old || trapd == lowerd_new)
		goto out_unlock;

	oldcred = shiftfs_override_creds(old->d_sb);
	err = vfs_rename(loweri_dir_old, lowerd_old, loweri_dir_new, lowerd_new,
			 NULL, flags);
	revert_creds(oldcred);

	shiftfs_copyattr(loweri_dir_old, olddir);
	shiftfs_copyattr(loweri_dir_new, newdir);

out_unlock:
	unlock_rename(lowerd_dir_new, lowerd_dir_old);

	return err;
}

static struct dentry *shiftfs_lookup(struct inode *dir, struct dentry *dentry,
				     unsigned int flags)
{
	struct dentry *new;
	struct inode *newi;
	const struct cred *oldcred;
	struct dentry *lowerd = dentry->d_parent->d_fsdata;
	struct inode *inode = NULL, *loweri = lowerd->d_inode;

	inode_lock(loweri);
	oldcred = shiftfs_override_creds(dentry->d_sb);
	new = lookup_one_len(dentry->d_name.name, lowerd, dentry->d_name.len);
	revert_creds(oldcred);
	inode_unlock(loweri);

	if (IS_ERR(new))
		return new;

	dentry->d_fsdata = new;

	newi = new->d_inode;
	if (!newi)
		goto out;

	inode = iget5_locked(dentry->d_sb, (unsigned long)newi,
			     shiftfs_inode_test, shiftfs_inode_set, newi);
	if (!inode) {
		dput(new);
		return ERR_PTR(-ENOMEM);
	}
	if (inode->i_state & I_NEW) {
		/*
		 * inode->i_private set by shiftfs_inode_set(), but we still
		 * need to take a reference
		*/
		ihold(newi);
		shiftfs_fill_inode(inode, newi->i_ino, newi->i_mode, 0, new);
		unlock_new_inode(inode);
	}

out:
	return d_splice_alias(inode, dentry);
}

static int shiftfs_permission(struct inode *inode, int mask)
{
	int err;
	const struct cred *oldcred;
	struct inode *loweri = inode->i_private;

	if (!loweri) {
		WARN_ON(!(mask & MAY_NOT_BLOCK));
		return -ECHILD;
	}

	err = generic_permission(inode, mask);
	if (err)
		return err;

	oldcred = shiftfs_override_creds(inode->i_sb);
	err = inode_permission(loweri, mask);
	revert_creds(oldcred);

	return err;
}

static int shiftfs_fiemap(struct inode *inode,
			  struct fiemap_extent_info *fieinfo, u64 start,
			  u64 len)
{
	int err;
	const struct cred *oldcred;
	struct inode *loweri = inode->i_private;

	if (!loweri->i_op->fiemap)
		return -EOPNOTSUPP;

	oldcred = shiftfs_override_creds(inode->i_sb);
	if (fieinfo->fi_flags & FIEMAP_FLAG_SYNC)
		filemap_write_and_wait(loweri->i_mapping);
	err = loweri->i_op->fiemap(loweri, fieinfo, start, len);
	revert_creds(oldcred);

	return err;
}

static int shiftfs_tmpfile(struct inode *dir, struct dentry *dentry,
			   umode_t mode)
{
	int err;
	const struct cred *oldcred;
	struct dentry *lowerd = dentry->d_fsdata;
	struct inode *loweri = dir->i_private;

	if (!loweri->i_op->tmpfile)
		return -EOPNOTSUPP;

	oldcred = shiftfs_override_creds(dir->i_sb);
	err = loweri->i_op->tmpfile(loweri, lowerd, mode);
	revert_creds(oldcred);

	return err;
}

static int shiftfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct dentry *lowerd = dentry->d_fsdata;
	struct inode *loweri = lowerd->d_inode;
	struct iattr newattr = *attr;
	const struct cred *oldcred;
	struct super_block *sb = dentry->d_sb;
	int err;

	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	newattr.ia_uid = KUIDT_INIT(from_kuid(sb->s_user_ns, attr->ia_uid));
	newattr.ia_gid = KGIDT_INIT(from_kgid(sb->s_user_ns, attr->ia_gid));

	inode_lock(loweri);
	oldcred = shiftfs_override_creds(dentry->d_sb);
	err = notify_change(lowerd, attr, NULL);
	revert_creds(oldcred);
	inode_unlock(loweri);

	shiftfs_copyattr(loweri, d_inode(dentry));

	return err;
}

static int shiftfs_getattr(const struct path *path, struct kstat *stat,
			   u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = path->dentry->d_inode;
	struct dentry *lowerd = path->dentry->d_fsdata;
	struct inode *loweri = lowerd->d_inode;
	struct shiftfs_super_info *info = path->dentry->d_sb->s_fs_info;
	struct path newpath = { .mnt = info->mnt, .dentry = lowerd };
	struct user_namespace *from_ns = loweri->i_sb->s_user_ns;
	struct user_namespace *to_ns = inode->i_sb->s_user_ns;
	const struct cred *oldcred;
	int err;

	oldcred = shiftfs_override_creds(inode->i_sb);
	err = vfs_getattr(&newpath, stat, request_mask, query_flags);
	revert_creds(oldcred);

	if (err)
		return err;

	/* transform the underlying id */
	stat->uid = shift_kuid(from_ns, to_ns, stat->uid);
	stat->gid = shift_kgid(from_ns, to_ns, stat->gid);
	return 0;
}

#ifdef CONFIG_SHIFT_FS_POSIX_ACL

static int
shift_acl_ids(struct user_namespace *from, struct user_namespace *to,
	      struct posix_acl *acl)
{
	int i;

	for (i = 0; i < acl->a_count; i++) {
		struct posix_acl_entry *e = &acl->a_entries[i];
		switch(e->e_tag) {
		case ACL_USER:
			e->e_uid = shift_kuid(from, to, e->e_uid);
			if (!uid_valid(e->e_uid))
				return -EOVERFLOW;
			break;
		case ACL_GROUP:
			e->e_gid = shift_kgid(from, to, e->e_gid);
			if (!gid_valid(e->e_gid))
				return -EOVERFLOW;
			break;
		}
	}
	return 0;
}

static void
shift_acl_xattr_ids(struct user_namespace *from, struct user_namespace *to,
		    void *value, size_t size)
{
	struct posix_acl_xattr_header *header = value;
	struct posix_acl_xattr_entry *entry = (void *)(header + 1), *end;
	int count;
	kuid_t kuid;
	kgid_t kgid;

	if (!value)
		return;
	if (size < sizeof(struct posix_acl_xattr_header))
		return;
	if (header->a_version != cpu_to_le32(POSIX_ACL_XATTR_VERSION))
		return;

	count = posix_acl_xattr_count(size);
	if (count < 0)
		return;
	if (count == 0)
		return;

	for (end = entry + count; entry != end; entry++) {
		switch(le16_to_cpu(entry->e_tag)) {
		case ACL_USER:
			kuid = make_kuid(&init_user_ns, le32_to_cpu(entry->e_id));
			kuid = shift_kuid(from, to, kuid);
			entry->e_id = cpu_to_le32(from_kuid(&init_user_ns, kuid));
			break;
		case ACL_GROUP:
			kgid = make_kgid(&init_user_ns, le32_to_cpu(entry->e_id));
			kgid = shift_kgid(from, to, kgid);
			entry->e_id = cpu_to_le32(from_kgid(&init_user_ns, kgid));
			break;
		default:
			break;
		}
	}
}

static struct posix_acl *shiftfs_get_acl(struct inode *inode, int type)
{
	struct inode *loweri = inode->i_private;
	const struct cred *oldcred;
	struct posix_acl *lower_acl, *acl = NULL;
	struct user_namespace *from_ns = loweri->i_sb->s_user_ns;
	struct user_namespace *to_ns = inode->i_sb->s_user_ns;
	int size;
	int err;

	if (!IS_POSIXACL(loweri))
		return NULL;

	oldcred = shiftfs_override_creds(inode->i_sb);
	lower_acl = get_acl(loweri, type);
	revert_creds(oldcred);

	if (lower_acl && !IS_ERR(lower_acl)) {
		/* XXX: export posix_acl_clone? */
		size = sizeof(struct posix_acl) +
		       lower_acl->a_count * sizeof(struct posix_acl_entry);
		acl = kmemdup(lower_acl, size, GFP_KERNEL);
		posix_acl_release(lower_acl);

		if (!acl)
			return ERR_PTR(-ENOMEM);

		refcount_set(&acl->a_refcount, 1);

		err = shift_acl_ids(from_ns, to_ns, acl);
		if (err) {
			kfree(acl);
			return ERR_PTR(err);
		}
	}

	return acl;
}

static int
shiftfs_posix_acl_xattr_get(const struct xattr_handler *handler,
			   struct dentry *dentry, struct inode *inode,
			   const char *name, void *buffer, size_t size)
{
	struct inode *loweri = inode->i_private;
	int ret;

	ret = shiftfs_xattr_get(NULL, dentry, inode, handler->name,
				buffer, size);
	if (ret < 0)
		return ret;

	inode_lock(loweri);
	shift_acl_xattr_ids(loweri->i_sb->s_user_ns, inode->i_sb->s_user_ns,
			    buffer, size);
	inode_unlock(loweri);
	return ret;
}

static int
shiftfs_posix_acl_xattr_set(const struct xattr_handler *handler,
			    struct dentry *dentry, struct inode *inode,
			    const char *name, const void *value,
			    size_t size, int flags)
{
	struct inode *loweri = inode->i_private;
	int err;

	if (!IS_POSIXACL(loweri) || !loweri->i_op->set_acl)
		return -EOPNOTSUPP;
	if (handler->flags == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode))
		return value ? -EACCES : 0;
	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (value) {
		shift_acl_xattr_ids(inode->i_sb->s_user_ns,
				    loweri->i_sb->s_user_ns,
				    (void *)value, size);
		err = shiftfs_setxattr(dentry, inode, handler->name, value,
				       size, flags);
	} else {
		err = shiftfs_removexattr(dentry, handler->name);
	}

	if (!err)
		shiftfs_copyattr(loweri, inode);

	return err;
}

static const struct xattr_handler
shiftfs_posix_acl_access_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_ACCESS,
	.flags = ACL_TYPE_ACCESS,
	.get = shiftfs_posix_acl_xattr_get,
	.set = shiftfs_posix_acl_xattr_set,
};

static const struct xattr_handler
shiftfs_posix_acl_default_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_DEFAULT,
	.flags = ACL_TYPE_DEFAULT,
	.get = shiftfs_posix_acl_xattr_get,
	.set = shiftfs_posix_acl_xattr_set,
};

#else /* !CONFIG_SHIFT_FS_POSIX_ACL */

#define shiftfs_get_acl NULL

#endif /* CONFIG_SHIFT_FS_POSIX_ACL */

static const struct inode_operations shiftfs_dir_inode_operations = {
	.lookup		= shiftfs_lookup,
	.mkdir		= shiftfs_mkdir,
	.symlink	= shiftfs_symlink,
	.unlink		= shiftfs_unlink,
	.rmdir		= shiftfs_rmdir,
	.rename		= shiftfs_rename,
	.link		= shiftfs_link,
	.setattr	= shiftfs_setattr,
	.create		= shiftfs_create,
	.mknod		= shiftfs_mknod,
	.permission	= shiftfs_permission,
	.getattr	= shiftfs_getattr,
	.listxattr	= shiftfs_listxattr,
	.get_acl	= shiftfs_get_acl,
};

static const struct inode_operations shiftfs_file_inode_operations = {
	.fiemap		= shiftfs_fiemap,
	.getattr	= shiftfs_getattr,
	.get_acl	= shiftfs_get_acl,
	.listxattr	= shiftfs_listxattr,
	.permission	= shiftfs_permission,
	.setattr	= shiftfs_setattr,
	.tmpfile	= shiftfs_tmpfile,
};

static const struct inode_operations shiftfs_special_inode_operations = {
	.getattr	= shiftfs_getattr,
	.get_acl	= shiftfs_get_acl,
	.listxattr	= shiftfs_listxattr,
	.permission	= shiftfs_permission,
	.setattr	= shiftfs_setattr,
};

static const struct inode_operations shiftfs_symlink_inode_operations = {
	.getattr	= shiftfs_getattr,
	.get_link	= shiftfs_get_link,
	.listxattr	= shiftfs_listxattr,
	.setattr	= shiftfs_setattr,
};

static struct file *shiftfs_open_realfile(const struct file *file,
					  struct path *realpath)
{
	struct file *lowerf;
	const struct cred *oldcred;
	struct inode *inode = file_inode(file);
	struct inode *loweri = realpath->dentry->d_inode;
	struct shiftfs_super_info *info = inode->i_sb->s_fs_info;

	oldcred = shiftfs_override_creds(inode->i_sb);
	/* XXX: open_with_fake_path() not gauranteed to stay around, if
	 * removed use dentry_open() */
	lowerf = open_with_fake_path(realpath, file->f_flags, loweri, info->creator_cred);
	revert_creds(oldcred);

	return lowerf;
}

#define SHIFTFS_SETFL_MASK (O_APPEND | O_NONBLOCK | O_NDELAY | O_DIRECT)

static int shiftfs_change_flags(struct file *file, unsigned int flags)
{
	struct inode *inode = file_inode(file);
	int err;

	/* if some flag changed that cannot be changed then something's amiss */
	if (WARN_ON((file->f_flags ^ flags) & ~SHIFTFS_SETFL_MASK))
		return -EIO;

	flags &= SHIFTFS_SETFL_MASK;

	if (((flags ^ file->f_flags) & O_APPEND) && IS_APPEND(inode))
		return -EPERM;

	if (flags & O_DIRECT) {
		if (!file->f_mapping->a_ops ||
		    !file->f_mapping->a_ops->direct_IO)
			return -EINVAL;
	}

	if (file->f_op->check_flags) {
		err = file->f_op->check_flags(flags);
		if (err)
			return err;
	}

	spin_lock(&file->f_lock);
	file->f_flags = (file->f_flags & ~SHIFTFS_SETFL_MASK) | flags;
	spin_unlock(&file->f_lock);

	return 0;
}

static int shiftfs_real_fdget(const struct file *file, struct fd *lowerfd)
{
	struct shiftfs_file_info *file_info = file->private_data;
	struct file *realfile = file_info->realfile;

	lowerfd->flags = 0;
	lowerfd->file = realfile;

	/* Did the flags change since open? */
	if (unlikely(file->f_flags & ~lowerfd->file->f_flags))
		return shiftfs_change_flags(lowerfd->file, file->f_flags);

	return 0;
}

static int shiftfs_open(struct inode *inode, struct file *file)
{
	struct shiftfs_super_info *ssi = inode->i_sb->s_fs_info;
	struct shiftfs_file_info *file_info;
	struct file *realfile;
	struct path *realpath;

	file_info = kmem_cache_zalloc(shiftfs_file_info_cache, GFP_KERNEL);
	if (!file_info)
		return -ENOMEM;

	realpath = &file_info->realpath;
	realpath->mnt = ssi->mnt;
	realpath->dentry = file->f_path.dentry->d_fsdata;

	realfile = shiftfs_open_realfile(file, realpath);
	if (IS_ERR(realfile)) {
		kmem_cache_free(shiftfs_file_info_cache, file_info);
		return PTR_ERR(realfile);
	}

	file->private_data = file_info;
	file_info->realfile = realfile;
	return 0;
}

static int shiftfs_release(struct inode *inode, struct file *file)
{
	struct shiftfs_file_info *file_info = file->private_data;

	if (file_info) {
		if (file_info->realfile)
			fput(file_info->realfile);

		kmem_cache_free(shiftfs_file_info_cache, file_info);
	}

	return 0;
}

static loff_t shiftfs_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *realinode = file_inode(file)->i_private;

	return generic_file_llseek_size(file, offset, whence,
					realinode->i_sb->s_maxbytes,
					i_size_read(realinode));
}

/* XXX: Need to figure out what to to about atime updates, maybe other
 * timestamps too ... ref. ovl_file_accessed() */

static rwf_t shiftfs_iocb_to_rwf(struct kiocb *iocb)
{
	int ifl = iocb->ki_flags;
	rwf_t flags = 0;

	if (ifl & IOCB_NOWAIT)
		flags |= RWF_NOWAIT;
	if (ifl & IOCB_HIPRI)
		flags |= RWF_HIPRI;
	if (ifl & IOCB_DSYNC)
		flags |= RWF_DSYNC;
	if (ifl & IOCB_SYNC)
		flags |= RWF_SYNC;

	return flags;
}

static ssize_t shiftfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct fd lowerfd;
	const struct cred *oldcred;
	ssize_t ret;

	if (!iov_iter_count(iter))
		return 0;

	ret = shiftfs_real_fdget(file, &lowerfd);
	if (ret)
		return ret;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	ret = vfs_iter_read(lowerfd.file, iter, &iocb->ki_pos,
			    shiftfs_iocb_to_rwf(iocb));
	revert_creds(oldcred);

	shiftfs_file_accessed(file);

	fdput(lowerfd);
	return ret;
}

static ssize_t shiftfs_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct fd lowerfd;
	const struct cred *oldcred;
	ssize_t ret;

	if (!iov_iter_count(iter))
		return 0;

	inode_lock(inode);
	/* Update mode */
	shiftfs_copyattr(inode->i_private, inode);
	ret = file_remove_privs(file);
	if (ret)
		goto out_unlock;

	ret = shiftfs_real_fdget(file, &lowerfd);
	if (ret)
		goto out_unlock;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	file_start_write(lowerfd.file);
	ret = vfs_iter_write(lowerfd.file, iter, &iocb->ki_pos,
			     shiftfs_iocb_to_rwf(iocb));
	file_end_write(lowerfd.file);
	revert_creds(oldcred);

	/* Update size */
	shiftfs_copyattr(inode->i_private, inode);

	fdput(lowerfd);

out_unlock:
	inode_unlock(inode);
	return ret;
}

static int shiftfs_fsync(struct file *file, loff_t start, loff_t end,
			 int datasync)
{
	struct fd lowerfd;
	const struct cred *oldcred;
	int ret;

	ret = shiftfs_real_fdget(file, &lowerfd);
	if (ret)
		return ret;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	ret = vfs_fsync_range(lowerfd.file, start, end, datasync);
	revert_creds(oldcred);

	fdput(lowerfd);
	return ret;
}

static int shiftfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct shiftfs_file_info *file_info = file->private_data;
	struct file *realfile = file_info->realfile;
	const struct cred *oldcred;
	int ret;

	if (!realfile->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	vma->vm_file = get_file(realfile);
	ret = call_mmap(vma->vm_file, vma);
	revert_creds(oldcred);

	shiftfs_file_accessed(file);

	if (ret)
		fput(realfile); /* Drop refcount from new vm_file value */
	else
		fput(file); /* Drop refcount from previous vm_file value */

	return ret;
}

static long shiftfs_fallocate(struct file *file, int mode, loff_t offset,
			      loff_t len)
{
	struct inode *inode = file_inode(file);
	struct inode *loweri = inode->i_private;
	struct fd lowerfd;
	const struct cred *oldcred;
	int ret;

	ret = shiftfs_real_fdget(file, &lowerfd);
	if (ret)
		return ret;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	ret = vfs_fallocate(lowerfd.file, mode, offset, len);
	revert_creds(oldcred);

	/* Update size */
	shiftfs_copyattr(loweri, inode);

	fdput(lowerfd);
	return ret;
}

static int shiftfs_fadvise(struct file *file, loff_t offset, loff_t len,
			   int advice)
{
	struct fd lowerfd;
	const struct cred *oldcred;
	int ret;

	ret = shiftfs_real_fdget(file, &lowerfd);
	if (ret)
		return ret;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	ret = vfs_fadvise(lowerfd.file, offset, len, advice);
	revert_creds(oldcred);

	fdput(lowerfd);
	return ret;
}

static int shiftfs_override_ioctl_creds(const struct super_block *sb,
					const struct cred **oldcred,
					struct cred **newcred)
{
	kuid_t fsuid = current_fsuid();
	kgid_t fsgid = current_fsgid();

	*oldcred = shiftfs_override_creds(sb);

	*newcred = prepare_creds();
	if (!*newcred) {
		revert_creds(*oldcred);
		return -ENOMEM;
	}

	(*newcred)->fsuid = KUIDT_INIT(from_kuid(sb->s_user_ns, fsuid));
	(*newcred)->fsgid = KGIDT_INIT(from_kgid(sb->s_user_ns, fsgid));

	/* clear all caps to prevent bypassing capable() checks */
	cap_clear((*newcred)->cap_bset);
	cap_clear((*newcred)->cap_effective);
	cap_clear((*newcred)->cap_inheritable);
	cap_clear((*newcred)->cap_permitted);

	put_cred(override_creds(*newcred));
	return 0;
}

static inline void shiftfs_revert_ioctl_creds(const struct cred *oldcred,
					      struct cred *newcred)
{
	return shiftfs_revert_object_creds(oldcred, newcred);
}

static inline bool is_btrfs_snap_ioctl(int cmd)
{
	if ((cmd == BTRFS_IOC_SNAP_CREATE) || (cmd == BTRFS_IOC_SNAP_CREATE_V2))
		return true;

	return false;
}

static int shiftfs_btrfs_ioctl_fd_restore(int cmd, struct fd lfd, int fd,
					  void __user *arg,
					  struct btrfs_ioctl_vol_args *v1,
					  struct btrfs_ioctl_vol_args_v2 *v2)
{
	int ret;

	if (!is_btrfs_snap_ioctl(cmd))
		return 0;

	if (cmd == BTRFS_IOC_SNAP_CREATE)
		ret = copy_to_user(arg, v1, sizeof(*v1));
	else
		ret = copy_to_user(arg, v2, sizeof(*v2));

	fdput(lfd);
	__close_fd(current->files, fd);
	kfree(v1);
	kfree(v2);

	return ret;
}

static int shiftfs_btrfs_ioctl_fd_replace(int cmd, void __user *arg,
					  struct btrfs_ioctl_vol_args **b1,
					  struct btrfs_ioctl_vol_args_v2 **b2,
					  struct fd *lfd,
					  int *newfd)
{
	int oldfd, ret;
	struct fd src;
	struct btrfs_ioctl_vol_args *v1 = NULL;
	struct btrfs_ioctl_vol_args_v2 *v2 = NULL;

	if (!is_btrfs_snap_ioctl(cmd))
		return 0;

	if (cmd == BTRFS_IOC_SNAP_CREATE) {
		v1 = memdup_user(arg, sizeof(*v1));
		if (IS_ERR(v1))
			return PTR_ERR(v1);
		oldfd = v1->fd;
		*b1 = v1;
	} else {
		v2 = memdup_user(arg, sizeof(*v2));
		if (IS_ERR(v2))
			return PTR_ERR(v2);
		oldfd = v2->fd;
		*b2 = v2;
	}

	src = fdget(oldfd);
	if (!src.file)
		return -EINVAL;

	ret = shiftfs_real_fdget(src.file, lfd);
	fdput(src);
	if (ret)
		return ret;

	*newfd = get_unused_fd_flags(lfd->file->f_flags);
	if (*newfd < 0) {
		fdput(*lfd);
		return *newfd;
	}

	fd_install(*newfd, lfd->file);

	if (cmd == BTRFS_IOC_SNAP_CREATE) {
		v1->fd = *newfd;
		ret = copy_to_user(arg, v1, sizeof(*v1));
		v1->fd = oldfd;
	} else {
		v2->fd = *newfd;
		ret = copy_to_user(arg, v2, sizeof(*v2));
		v2->fd = oldfd;
	}

	if (ret)
		shiftfs_btrfs_ioctl_fd_restore(cmd, *lfd, *newfd, arg, v1, v2);

	return ret;
}

static long shiftfs_real_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct fd lowerfd;
	struct cred *newcred;
	const struct cred *oldcred;
	int newfd = -EBADF;
	long err = 0, ret = 0;
	void __user *argp = (void __user *)arg;
	struct fd btrfs_lfd = {};
	struct super_block *sb = file->f_path.dentry->d_sb;
	struct btrfs_ioctl_vol_args *btrfs_v1 = NULL;
	struct btrfs_ioctl_vol_args_v2 *btrfs_v2 = NULL;

	ret = shiftfs_btrfs_ioctl_fd_replace(cmd, argp, &btrfs_v1, &btrfs_v2,
					     &btrfs_lfd, &newfd);
	if (ret < 0)
		return ret;

	ret = shiftfs_real_fdget(file, &lowerfd);
	if (ret)
		goto out_restore;

	ret = shiftfs_override_ioctl_creds(sb, &oldcred, &newcred);
	if (ret)
		goto out_fdput;

	ret = vfs_ioctl(lowerfd.file, cmd, arg);

	shiftfs_revert_ioctl_creds(oldcred, newcred);

	shiftfs_copyattr(file_inode(lowerfd.file), file_inode(file));
	shiftfs_copyflags(file_inode(lowerfd.file), file_inode(file));

out_fdput:
	fdput(lowerfd);

out_restore:
	err = shiftfs_btrfs_ioctl_fd_restore(cmd, btrfs_lfd, newfd, argp,
					     btrfs_v1, btrfs_v2);
	if (!ret)
		ret = err;

	return ret;
}

static bool in_ioctl_whitelist(int flag)
{
	switch (flag) {
	case BTRFS_IOC_SNAP_CREATE:
		return true;
	case BTRFS_IOC_SNAP_CREATE_V2:
		return true;
	case BTRFS_IOC_SUBVOL_CREATE:
		return true;
	case BTRFS_IOC_SUBVOL_CREATE_V2:
		return true;
	case BTRFS_IOC_SNAP_DESTROY:
		return true;
	}

	return false;
}

static long shiftfs_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	switch (cmd) {
	case FS_IOC_GETVERSION:
		/* fall through */
	case FS_IOC_GETFLAGS:
		/* fall through */
	case FS_IOC_SETFLAGS:
		break;
	default:
		if (!in_ioctl_whitelist(cmd) ||
		    !shiftfs_passthrough_ioctls(file->f_path.dentry->d_sb->s_fs_info))
			return -ENOTTY;
	}

	return shiftfs_real_ioctl(file, cmd, arg);
}

static long shiftfs_compat_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETVERSION:
		/* fall through */
	case FS_IOC32_GETFLAGS:
		/* fall through */
	case FS_IOC32_SETFLAGS:
		break;
	default:
		if (!in_ioctl_whitelist(cmd) ||
		    !shiftfs_passthrough_ioctls(file->f_path.dentry->d_sb->s_fs_info))
			return -ENOIOCTLCMD;
	}

	return shiftfs_real_ioctl(file, cmd, arg);
}

enum shiftfs_copyop {
	SHIFTFS_COPY,
	SHIFTFS_CLONE,
	SHIFTFS_DEDUPE,
};

static ssize_t shiftfs_copyfile(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out, u64 len,
				unsigned int flags, enum shiftfs_copyop op)
{
	ssize_t ret;
	struct fd real_in, real_out;
	const struct cred *oldcred;
	struct inode *inode_out = file_inode(file_out);
	struct inode *loweri = inode_out->i_private;

	ret = shiftfs_real_fdget(file_out, &real_out);
	if (ret)
		return ret;

	ret = shiftfs_real_fdget(file_in, &real_in);
	if (ret) {
		fdput(real_out);
		return ret;
	}

	oldcred = shiftfs_override_creds(inode_out->i_sb);
	switch (op) {
	case SHIFTFS_COPY:
		ret = vfs_copy_file_range(real_in.file, pos_in, real_out.file,
					  pos_out, len, flags);
		break;

	case SHIFTFS_CLONE:
		ret = vfs_clone_file_range(real_in.file, pos_in, real_out.file,
					   pos_out, len, flags);
		break;

	case SHIFTFS_DEDUPE:
		ret = vfs_dedupe_file_range_one(real_in.file, pos_in,
						real_out.file, pos_out, len,
						flags);
		break;
	}
	revert_creds(oldcred);

	/* Update size */
	shiftfs_copyattr(loweri, inode_out);

	fdput(real_in);
	fdput(real_out);

	return ret;
}

static ssize_t shiftfs_copy_file_range(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       size_t len, unsigned int flags)
{
	return shiftfs_copyfile(file_in, pos_in, file_out, pos_out, len, flags,
				SHIFTFS_COPY);
}

static loff_t shiftfs_remap_file_range(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       loff_t len, unsigned int remap_flags)
{
	enum shiftfs_copyop op;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (remap_flags & REMAP_FILE_DEDUP)
		op = SHIFTFS_DEDUPE;
	else
		op = SHIFTFS_CLONE;

	return shiftfs_copyfile(file_in, pos_in, file_out, pos_out, len,
				remap_flags, op);
}

static int shiftfs_iterate_shared(struct file *file, struct dir_context *ctx)
{
	const struct cred *oldcred;
	int err = -ENOTDIR;
	struct shiftfs_file_info *file_info = file->private_data;
	struct file *realfile = file_info->realfile;

	oldcred = shiftfs_override_creds(file->f_path.dentry->d_sb);
	err = iterate_dir(realfile, ctx);
	revert_creds(oldcred);

	return err;
}

const struct file_operations shiftfs_file_operations = {
	.open			= shiftfs_open,
	.release		= shiftfs_release,
	.llseek			= shiftfs_llseek,
	.read_iter		= shiftfs_read_iter,
	.write_iter		= shiftfs_write_iter,
	.fsync			= shiftfs_fsync,
	.mmap			= shiftfs_mmap,
	.fallocate		= shiftfs_fallocate,
	.fadvise		= shiftfs_fadvise,
	.unlocked_ioctl		= shiftfs_ioctl,
	.compat_ioctl		= shiftfs_compat_ioctl,
	.copy_file_range	= shiftfs_copy_file_range,
	.remap_file_range	= shiftfs_remap_file_range,
};

const struct file_operations shiftfs_dir_operations = {
	.compat_ioctl		= shiftfs_compat_ioctl,
	.fsync			= shiftfs_fsync,
	.iterate_shared		= shiftfs_iterate_shared,
	.llseek			= shiftfs_llseek,
	.open			= shiftfs_open,
	.read			= generic_read_dir,
	.release		= shiftfs_release,
	.unlocked_ioctl		= shiftfs_ioctl,
};

static const struct address_space_operations shiftfs_aops = {
	/* For O_DIRECT dentry_open() checks f_mapping->a_ops->direct_IO */
	.direct_IO	= noop_direct_IO,
};

static void shiftfs_fill_inode(struct inode *inode, unsigned long ino,
			       umode_t mode, dev_t dev, struct dentry *dentry)
{
	struct inode *loweri;

	inode->i_ino = ino;
	inode->i_flags |= S_NOCMTIME;

	mode &= S_IFMT;
	inode->i_mode = mode;
	switch (mode & S_IFMT) {
	case S_IFDIR:
		inode->i_op = &shiftfs_dir_inode_operations;
		inode->i_fop = &shiftfs_dir_operations;
		break;
	case S_IFLNK:
		inode->i_op = &shiftfs_symlink_inode_operations;
		break;
	case S_IFREG:
		inode->i_op = &shiftfs_file_inode_operations;
		inode->i_fop = &shiftfs_file_operations;
		inode->i_mapping->a_ops = &shiftfs_aops;
		break;
	default:
		inode->i_op = &shiftfs_special_inode_operations;
		init_special_inode(inode, mode, dev);
		break;
	}

	if (!dentry)
		return;

	loweri = dentry->d_inode;
	if (!loweri->i_op->get_link)
		inode->i_opflags |= IOP_NOFOLLOW;

	shiftfs_copyattr(loweri, inode);
	shiftfs_copyflags(loweri, inode);
	set_nlink(inode, loweri->i_nlink);
}

static int shiftfs_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct shiftfs_super_info *sbinfo = sb->s_fs_info;

	if (sbinfo->mark)
		seq_show_option(m, "mark", NULL);

	if (sbinfo->passthrough)
		seq_printf(m, ",passthrough=%u", sbinfo->passthrough);

	return 0;
}

static int shiftfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct shiftfs_super_info *sbinfo = sb->s_fs_info;
	struct dentry *root = sb->s_root;
	struct dentry *realroot = root->d_fsdata;
	struct path realpath = { .mnt = sbinfo->mnt, .dentry = realroot };
	int err;

	err = vfs_statfs(&realpath, buf);
	if (err)
		return err;

	if (!shiftfs_passthrough_statfs(sbinfo))
		buf->f_type = sb->s_magic;

	return 0;
}

static void shiftfs_evict_inode(struct inode *inode)
{
	struct inode *loweri = inode->i_private;

	clear_inode(inode);

	if (loweri)
		iput(loweri);
}

static void shiftfs_put_super(struct super_block *sb)
{
	struct shiftfs_super_info *sbinfo = sb->s_fs_info;

	if (sbinfo) {
		mntput(sbinfo->mnt);
		put_cred(sbinfo->creator_cred);
		kfree(sbinfo);
	}
}

static const struct xattr_handler shiftfs_xattr_handler = {
	.prefix = "",
	.get    = shiftfs_xattr_get,
	.set    = shiftfs_xattr_set,
};

const struct xattr_handler *shiftfs_xattr_handlers[] = {
#ifdef CONFIG_SHIFT_FS_POSIX_ACL
	&shiftfs_posix_acl_access_xattr_handler,
	&shiftfs_posix_acl_default_xattr_handler,
#endif
	&shiftfs_xattr_handler,
	NULL
};

static inline bool passthrough_is_subset(int old_flags, int new_flags)
{
	if ((new_flags & old_flags) != new_flags)
		return false;

	return true;
}

static int shiftfs_remount(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct shiftfs_super_info new = {};
	struct shiftfs_super_info *info = sb->s_fs_info;

	err = shiftfs_parse_mount_options(&new, data);
	if (err)
		return err;

	/* Mark mount option cannot be changed. */
	if (info->mark || (info->mark != new.mark))
		return -EPERM;

	if (info->passthrough != new.passthrough) {
		/* Don't allow exceeding passthrough options of mark mount. */
		if (!passthrough_is_subset(info->info_mark->passthrough,
					   info->passthrough))
			return -EPERM;

		info->passthrough = new.passthrough;
	}

	return 0;
}

static const struct super_operations shiftfs_super_ops = {
	.put_super	= shiftfs_put_super,
	.show_options	= shiftfs_show_options,
	.statfs		= shiftfs_statfs,
	.remount_fs	= shiftfs_remount,
	.evict_inode	= shiftfs_evict_inode,
};

struct shiftfs_data {
	void *data;
	const char *path;
};

static int shiftfs_fill_super(struct super_block *sb, void *raw_data,
			      int silent)
{
	int err;
	struct path path = {};
	struct shiftfs_super_info *sbinfo_mp;
	char *name = NULL;
	struct inode *inode = NULL;
	struct dentry *dentry = NULL;
	struct shiftfs_data *data = raw_data;
	struct shiftfs_super_info *sbinfo = NULL;

	if (!data->path)
		return -EINVAL;

	sb->s_fs_info = kzalloc(sizeof(*sbinfo), GFP_KERNEL);
	if (!sb->s_fs_info)
		return -ENOMEM;
	sbinfo = sb->s_fs_info;

	err = shiftfs_parse_mount_options(sbinfo, data->data);
	if (err)
		return err;

	/* to mount a mark, must be userns admin */
	if (!sbinfo->mark && !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	name = kstrdup(data->path, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	err = kern_path(name, LOOKUP_FOLLOW, &path);
	if (err)
		goto out_free_name;

	if (!S_ISDIR(path.dentry->d_inode->i_mode)) {
		err = -ENOTDIR;
		goto out_put_path;
	}

	if (sbinfo->mark) {
		struct super_block *lower_sb = path.mnt->mnt_sb;

		/* to mark a mount point, must root wrt lower s_user_ns */
		if (!ns_capable(lower_sb->s_user_ns, CAP_SYS_ADMIN)) {
			err = -EPERM;
			goto out_put_path;
		}

		/*
		 * this part is visible unshifted, so make sure no
		 * executables that could be used to give suid
		 * privileges
		 */
		sb->s_iflags = SB_I_NOEXEC;

		/*
		 * Handle nesting of shiftfs mounts by referring this mark
		 * mount back to the original mark mount. This is more
		 * efficient and alleviates concerns about stack depth.
		 */
		if (lower_sb->s_magic == SHIFTFS_MAGIC) {
			sbinfo_mp = lower_sb->s_fs_info;

			/* Doesn't make sense to mark a mark mount */
			if (sbinfo_mp->mark) {
				err = -EINVAL;
				goto out_put_path;
			}

			if (!passthrough_is_subset(sbinfo_mp->passthrough,
						   sbinfo->passthrough)) {
				err = -EPERM;
				goto out_put_path;
			}

			sbinfo->mnt = mntget(sbinfo_mp->mnt);
			dentry = dget(path.dentry->d_fsdata);
		} else {
			sbinfo->mnt = mntget(path.mnt);
			dentry = dget(path.dentry);
		}

		sbinfo->creator_cred = prepare_creds();
		if (!sbinfo->creator_cred) {
			err = -ENOMEM;
			goto out_put_path;
		}
	} else {
		/*
		 * This leg executes if we're admin capable in the namespace,
		 * so be very careful.
		 */
		err = -EPERM;
		if (path.dentry->d_sb->s_magic != SHIFTFS_MAGIC)
			goto out_put_path;

		sbinfo_mp = path.dentry->d_sb->s_fs_info;
		if (!sbinfo_mp->mark)
			goto out_put_path;

		if (!passthrough_is_subset(sbinfo_mp->passthrough,
					   sbinfo->passthrough))
			goto out_put_path;

		sbinfo->mnt = mntget(sbinfo_mp->mnt);
		sbinfo->creator_cred = get_cred(sbinfo_mp->creator_cred);
		dentry = dget(path.dentry->d_fsdata);
		sbinfo->info_mark = sbinfo_mp;
	}

	sb->s_stack_depth = dentry->d_sb->s_stack_depth + 1;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		printk(KERN_ERR "shiftfs: maximum stacking depth exceeded\n");
		err = -EINVAL;
		goto out_put_path;
	}

	inode = new_inode(sb);
	if (!inode) {
		err = -ENOMEM;
		goto out_put_path;
	}
	shiftfs_fill_inode(inode, dentry->d_inode->i_ino, S_IFDIR, 0, dentry);

	ihold(dentry->d_inode);
	inode->i_private = dentry->d_inode;

	sb->s_magic = SHIFTFS_MAGIC;
	sb->s_op = &shiftfs_super_ops;
	sb->s_xattr = shiftfs_xattr_handlers;
	sb->s_d_op = &shiftfs_dentry_ops;
	sb->s_flags |= SB_POSIXACL;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_put_path;
	}

	sb->s_root->d_fsdata = dentry;
	sbinfo->userns = get_user_ns(dentry->d_sb->s_user_ns);
	shiftfs_copyattr(dentry->d_inode, sb->s_root->d_inode);

	dentry = NULL;
	err = 0;

out_put_path:
	path_put(&path);

out_free_name:
	kfree(name);

	dput(dentry);

	return err;
}

static struct dentry *shiftfs_mount(struct file_system_type *fs_type,
				    int flags, const char *dev_name, void *data)
{
	struct shiftfs_data d = { data, dev_name };

	return mount_nodev(fs_type, flags, &d, shiftfs_fill_super);
}

static struct file_system_type shiftfs_type = {
	.owner		= THIS_MODULE,
	.name		= "shiftfs",
	.mount		= shiftfs_mount,
	.kill_sb	= kill_anon_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

static int __init shiftfs_init(void)
{
	shiftfs_file_info_cache = kmem_cache_create(
		"shiftfs_file_info_cache", sizeof(struct shiftfs_file_info), 0,
		SLAB_HWCACHE_ALIGN | SLAB_ACCOUNT | SLAB_MEM_SPREAD, NULL);
	if (!shiftfs_file_info_cache)
		return -ENOMEM;

	return register_filesystem(&shiftfs_type);
}

static void __exit shiftfs_exit(void)
{
	unregister_filesystem(&shiftfs_type);
	kmem_cache_destroy(shiftfs_file_info_cache);
}

MODULE_ALIAS_FS("shiftfs");
MODULE_AUTHOR("James Bottomley");
MODULE_AUTHOR("Seth Forshee <seth.forshee@canonical.com>");
MODULE_AUTHOR("Christian Brauner <christian.brauner@ubuntu.com>");
MODULE_DESCRIPTION("id shifting filesystem");
MODULE_LICENSE("GPL v2");
module_init(shiftfs_init)
module_exit(shiftfs_exit)
