#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/magic.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/uidgid.h>
#include <linux/xattr.h>

struct shiftfs_super_info {
	struct vfsmount *mnt;
	struct user_namespace *userns;
	bool mark;
};

static struct inode *shiftfs_new_inode(struct super_block *sb, umode_t mode,
				       struct dentry *dentry);

enum {
	OPT_MARK,
	OPT_LAST,
};

/* global filesystem options */
static const match_table_t tokens = {
	{ OPT_MARK, "mark" },
	{ OPT_LAST, NULL }
};

static const struct cred *shiftfs_get_up_creds(struct super_block *sb)
{
	struct shiftfs_super_info *ssi = sb->s_fs_info;
	struct cred *cred = prepare_creds();

	if (!cred)
		return NULL;

	cred->fsuid = KUIDT_INIT(from_kuid(sb->s_user_ns, cred->fsuid));
	cred->fsgid = KGIDT_INIT(from_kgid(sb->s_user_ns, cred->fsgid));
	put_user_ns(cred->user_ns);
	cred->user_ns = get_user_ns(ssi->userns);

	return cred;
}

static const struct cred *shiftfs_new_creds(const struct cred **newcred,
					    struct super_block *sb)
{
	const struct cred *cred = shiftfs_get_up_creds(sb);

	*newcred = cred;

	if (cred)
		cred = override_creds(cred);
	else
		printk(KERN_ERR "shiftfs: Credential override failed: no memory\n");

	return cred;
}

static void shiftfs_old_creds(const struct cred *oldcred,
			      const struct cred **newcred)
{
	if (!*newcred)
		return;

	revert_creds(oldcred);
	put_cred(*newcred);
}

static int shiftfs_parse_options(struct shiftfs_super_info *ssi, char *options)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];

	ssi->mark = false;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case OPT_MARK:
			ssi->mark = true;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static void shiftfs_d_release(struct dentry *dentry)
{
	struct dentry *real = dentry->d_fsdata;

	dput(real);
}

static struct dentry *shiftfs_d_real(struct dentry *dentry,
				     const struct inode *inode)
{
	struct dentry *real = dentry->d_fsdata;

	if (unlikely(real->d_flags & DCACHE_OP_REAL))
		return real->d_op->d_real(real, real->d_inode);

	return real;
}

static int shiftfs_d_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct dentry *real = dentry->d_fsdata;

	if (d_unhashed(real))
		return 0;

	if (!(real->d_flags & DCACHE_OP_WEAK_REVALIDATE))
		return 1;

	return real->d_op->d_weak_revalidate(real, flags);
}

static int shiftfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct dentry *real = dentry->d_fsdata;
	int ret;

	if (d_unhashed(real))
		return 0;

	/*
	 * inode state of underlying changed from positive to negative
	 * or vice versa; force a lookup to update our view
	 */
	if (d_is_negative(real) != d_is_negative(dentry))
		return 0;

	if (!(real->d_flags & DCACHE_OP_REVALIDATE))
		return 1;

	ret = real->d_op->d_revalidate(real, flags);

	if (ret == 0 && !(flags & LOOKUP_RCU))
		d_invalidate(real);

	return ret;
}

static const struct dentry_operations shiftfs_dentry_ops = {
	.d_release	= shiftfs_d_release,
	.d_real		= shiftfs_d_real,
	.d_revalidate	= shiftfs_d_revalidate,
	.d_weak_revalidate = shiftfs_d_weak_revalidate,
};

static int shiftfs_readlink(struct dentry *dentry, char __user *data,
			    int flags)
{
	struct dentry *real = dentry->d_fsdata;
	const struct inode_operations *iop = real->d_inode->i_op;

	if (iop->readlink)
		return iop->readlink(real, data, flags);

	return -EINVAL;
}

static const char *shiftfs_get_link(struct dentry *dentry, struct inode *inode,
				    struct delayed_call *done)
{
	if (dentry) {
		struct dentry *real = dentry->d_fsdata;
		struct inode *reali = real->d_inode;
		const struct inode_operations *iop = reali->i_op;
		const char *res = ERR_PTR(-EPERM);

		if (iop->get_link)
			res = iop->get_link(real, reali, done);

		return res;
	} else {
		/* RCU lookup not supported */
		return ERR_PTR(-ECHILD);
	}
}

static int shiftfs_setxattr(struct dentry *dentry, struct inode *inode,
			    const char *name, const void *value,
			    size_t size, int flags)
{
	struct dentry *real = dentry->d_fsdata;
	int err = -EOPNOTSUPP;
	const struct cred *oldcred, *newcred;

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);
	err = vfs_setxattr(real, name, value, size, flags);
	shiftfs_old_creds(oldcred, &newcred);

	return err;
}

static int shiftfs_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, void *value, size_t size)
{
	struct dentry *real = dentry->d_fsdata;
	int err;
	const struct cred *oldcred, *newcred;

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);
	err = vfs_getxattr(real, name, value, size);
	shiftfs_old_creds(oldcred, &newcred);

	return err;
}

static ssize_t shiftfs_listxattr(struct dentry *dentry, char *list,
				 size_t size)
{
	struct dentry *real = dentry->d_fsdata;
	int err;
	const struct cred *oldcred, *newcred;

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);
	err = vfs_listxattr(real, list, size);
	shiftfs_old_creds(oldcred, &newcred);

	return err;
}

static int shiftfs_removexattr(struct dentry *dentry, const char *name)
{
	struct dentry *real = dentry->d_fsdata;
	int err;
	const struct cred *oldcred, *newcred;

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);
	err = vfs_removexattr(real, name);
	shiftfs_old_creds(oldcred, &newcred);

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

static void shiftfs_fill_inode(struct inode *inode, struct dentry *dentry)
{
	struct inode *reali;

	if (!dentry)
		return;

	reali = dentry->d_inode;

	if (!reali->i_op->get_link)
		inode->i_opflags |= IOP_NOFOLLOW;

	inode->i_mapping = reali->i_mapping;
	inode->i_private = dentry;
}

static int shiftfs_make_object(struct inode *dir, struct dentry *dentry,
			       umode_t mode, const char *symlink,
			       struct dentry *hardlink, bool excl)
{
	struct dentry *real = dir->i_private, *new = dentry->d_fsdata;
	struct inode *reali = real->d_inode, *newi;
	const struct inode_operations *iop = reali->i_op;
	int err;
	const struct cred *oldcred, *newcred;
	bool op_ok = false;

	if (hardlink) {
		op_ok = iop->link;
	} else {
		switch (mode & S_IFMT) {
		case S_IFDIR:
			op_ok = iop->mkdir;
			break;
		case S_IFREG:
			op_ok = iop->create;
			break;
		case S_IFLNK:
			op_ok = iop->symlink;
		}
	}
	if (!op_ok)
		return -EINVAL;


	newi = shiftfs_new_inode(dentry->d_sb, mode, NULL);
	if (!newi)
		return -ENOMEM;

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);

	inode_lock_nested(reali, I_MUTEX_PARENT);

	err = -EINVAL;		/* shut gcc up about uninit var */
	if (hardlink) {
		struct dentry *realhardlink = hardlink->d_fsdata;

		err = vfs_link(realhardlink, reali, new, NULL);
	} else {
		switch (mode & S_IFMT) {
		case S_IFDIR:
			err = vfs_mkdir(reali, new, mode);
			break;
		case S_IFREG:
			err = vfs_create(reali, new, mode, excl);
			break;
		case S_IFLNK:
			err = vfs_symlink(reali, new, symlink);
		}
	}

	shiftfs_old_creds(oldcred, &newcred);

	if (err)
		goto out_dput;

	shiftfs_fill_inode(newi, new);

	d_instantiate(dentry, newi);

	new = NULL;
	newi = NULL;

 out_dput:
	dput(new);
	iput(newi);
	inode_unlock(reali);

	return err;
}

static int shiftfs_create(struct inode *dir, struct dentry *dentry,
			  umode_t mode,  bool excl)
{
	mode |= S_IFREG;

	return shiftfs_make_object(dir, dentry, mode, NULL, NULL, excl);
}

static int shiftfs_mkdir(struct inode *dir, struct dentry *dentry,
			 umode_t mode)
{
	mode |= S_IFDIR;

	return shiftfs_make_object(dir, dentry, mode, NULL, NULL, false);
}

static int shiftfs_link(struct dentry *hardlink, struct inode *dir,
			struct dentry *dentry)
{
	return shiftfs_make_object(dir, dentry, 0, NULL, hardlink, false);
}

static int shiftfs_symlink(struct inode *dir, struct dentry *dentry,
			   const char *symlink)
{
	return shiftfs_make_object(dir, dentry, S_IFLNK, symlink, NULL, false);
}

static int shiftfs_rm(struct inode *dir, struct dentry *dentry, bool rmdir)
{
	struct dentry *real = dir->i_private, *new = dentry->d_fsdata;
	struct inode *reali = real->d_inode;
	int err;
	const struct cred *oldcred, *newcred;

	inode_lock_nested(reali, I_MUTEX_PARENT);

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);

	if (rmdir)
		err = vfs_rmdir(reali, new);
	else
		err = vfs_unlink(reali, new, NULL);

	shiftfs_old_creds(oldcred, &newcred);
	inode_unlock(reali);

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
	struct dentry *rodd = olddir->i_private, *rndd = newdir->i_private,
		*realold = old->d_fsdata,
		*realnew = new->d_fsdata, *trap;
	struct inode *realolddir = rodd->d_inode, *realnewdir = rndd->d_inode;
	int err = -EINVAL;
	const struct cred *oldcred, *newcred;

	trap = lock_rename(rndd, rodd);

	if (trap == realold || trap == realnew)
		goto out_unlock;

	oldcred = shiftfs_new_creds(&newcred, old->d_sb);

	err = vfs_rename(realolddir, realold, realnewdir,
			 realnew, NULL, flags);

	shiftfs_old_creds(oldcred, &newcred);

 out_unlock:
	unlock_rename(rndd, rodd);

	return err;
}

static struct dentry *shiftfs_lookup(struct inode *dir, struct dentry *dentry,
				     unsigned int flags)
{
	struct dentry *real = dir->i_private, *new;
	struct inode *reali = real->d_inode, *newi;
	const struct cred *oldcred, *newcred;

	inode_lock(reali);
	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);
	new = lookup_one_len(dentry->d_name.name, real, dentry->d_name.len);
	shiftfs_old_creds(oldcred, &newcred);
	inode_unlock(reali);

	if (IS_ERR(new))
		return new;

	dentry->d_fsdata = new;

	newi = NULL;
	if (!new->d_inode)
		goto out;

	newi = shiftfs_new_inode(dentry->d_sb, new->d_inode->i_mode, new);
	if (!newi) {
		dput(new);
		return ERR_PTR(-ENOMEM);
	}

 out:
	return d_splice_alias(newi, dentry);
}

static int shiftfs_permission(struct inode *inode, int mask)
{
	struct dentry *real = inode->i_private;
	struct inode *reali = real->d_inode;
	const struct inode_operations *iop = reali->i_op;
	int err;
	const struct cred *oldcred, *newcred;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	oldcred = shiftfs_new_creds(&newcred, inode->i_sb);
	if (iop->permission)
		err = iop->permission(reali, mask);
	else
		err = generic_permission(reali, mask);
	shiftfs_old_creds(oldcred, &newcred);

	return err;
}

static int shiftfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct dentry *real = dentry->d_fsdata;
	struct inode *reali = real->d_inode;
	const struct inode_operations *iop = reali->i_op;
	struct iattr newattr = *attr;
	const struct cred *oldcred, *newcred;
	struct super_block *sb = dentry->d_sb;
	int err;

	newattr.ia_uid = KUIDT_INIT(from_kuid(sb->s_user_ns, attr->ia_uid));
	newattr.ia_gid = KGIDT_INIT(from_kgid(sb->s_user_ns, attr->ia_gid));

	oldcred = shiftfs_new_creds(&newcred, dentry->d_sb);
	inode_lock(reali);
	if (iop->setattr)
		err = iop->setattr(real, &newattr);
	else
		err = simple_setattr(real, &newattr);
	inode_unlock(reali);
	shiftfs_old_creds(oldcred, &newcred);

	if (err)
		return err;

	/* all OK, reflect the change on our inode */
	setattr_copy(d_inode(dentry), attr);
	return 0;
}

static int shiftfs_getattr(const struct path *path, struct kstat *stat,
			   u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = path->dentry->d_inode;
	struct dentry *real = path->dentry->d_fsdata;
	struct inode *reali = real->d_inode;
	const struct inode_operations *iop = reali->i_op;
	struct path newpath = { .mnt = path->dentry->d_sb->s_fs_info, .dentry = real };
	int err = 0;

	if (iop->getattr)
		err = iop->getattr(&newpath, stat, request_mask, query_flags);
	else
		generic_fillattr(reali, stat);

	if (err)
		return err;

	/* transform the underlying id */
	stat->uid = make_kuid(inode->i_sb->s_user_ns, __kuid_val(stat->uid));
	stat->gid = make_kgid(inode->i_sb->s_user_ns, __kgid_val(stat->gid));
	return 0;
}

static const struct inode_operations shiftfs_inode_ops = {
	.lookup		= shiftfs_lookup,
	.getattr	= shiftfs_getattr,
	.setattr	= shiftfs_setattr,
	.permission	= shiftfs_permission,
	.mkdir		= shiftfs_mkdir,
	.symlink	= shiftfs_symlink,
	.get_link	= shiftfs_get_link,
	.readlink	= shiftfs_readlink,
	.unlink		= shiftfs_unlink,
	.rmdir		= shiftfs_rmdir,
	.rename		= shiftfs_rename,
	.link		= shiftfs_link,
	.create		= shiftfs_create,
	.mknod		= NULL,	/* no special files currently */
	.listxattr	= shiftfs_listxattr,
};

static struct inode *shiftfs_new_inode(struct super_block *sb, umode_t mode,
				       struct dentry *dentry)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		return NULL;

	/*
	 * our inode is completely vestigial.  All lookups, getattr
	 * and permission checks are done on the underlying inode, so
	 * what the user sees is entirely from the underlying inode.
	 */
	mode &= S_IFMT;

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_flags |= S_NOATIME | S_NOCMTIME;

	inode->i_op = &shiftfs_inode_ops;

	shiftfs_fill_inode(inode, dentry);

	return inode;
}

static int shiftfs_show_options(struct seq_file *m, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct shiftfs_super_info *ssi = sb->s_fs_info;

	if (ssi->mark)
		seq_show_option(m, "mark", NULL);

	return 0;
}

static int shiftfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct shiftfs_super_info *ssi = sb->s_fs_info;
	struct dentry *root = sb->s_root;
	struct dentry *realroot = root->d_fsdata;
	struct path realpath = { .mnt = ssi->mnt, .dentry = realroot };
	int err;

	err = vfs_statfs(&realpath, buf);
	if (err)
		return err;

	buf->f_type = sb->s_magic;

	return 0;
}

static void shiftfs_put_super(struct super_block *sb)
{
	struct shiftfs_super_info *ssi = sb->s_fs_info;

	mntput(ssi->mnt);
	put_user_ns(ssi->userns);
	kfree(ssi);
}

static const struct xattr_handler shiftfs_xattr_handler = {
	.prefix = "",
	.get    = shiftfs_xattr_get,
	.set    = shiftfs_xattr_set,
};

const struct xattr_handler *shiftfs_xattr_handlers[] = {
	&shiftfs_xattr_handler,
	NULL
};

static const struct super_operations shiftfs_super_ops = {
	.put_super	= shiftfs_put_super,
	.show_options	= shiftfs_show_options,
	.statfs		= shiftfs_statfs,
};

struct shiftfs_data {
	void *data;
	const char *path;
};

static int shiftfs_fill_super(struct super_block *sb, void *raw_data,
			      int silent)
{
	struct shiftfs_data *data = raw_data;
	char *name = kstrdup(data->path, GFP_KERNEL);
	int err = -ENOMEM;
	struct shiftfs_super_info *ssi = NULL;
	struct path path;
	struct dentry *dentry;

	if (!name)
		goto out;

	ssi = kzalloc(sizeof(*ssi), GFP_KERNEL);
	if (!ssi)
		goto out;

	err = -EPERM;
	err = shiftfs_parse_options(ssi, data->data);
	if (err)
		goto out;

	/* to mark a mount point, must be real root */
	if (ssi->mark && !capable(CAP_SYS_ADMIN))
		goto out;

	/* else to mount a mark, must be userns admin */
	if (!ssi->mark && !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		goto out;

	err = kern_path(name, LOOKUP_FOLLOW, &path);
	if (err)
		goto out;

	err = -EPERM;

	if (!S_ISDIR(path.dentry->d_inode->i_mode)) {
		err = -ENOTDIR;
		goto out_put;
	}

	sb->s_stack_depth = path.dentry->d_sb->s_stack_depth + 1;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		printk(KERN_ERR "shiftfs: maximum stacking depth exceeded\n");
		err = -EINVAL;
		goto out_put;
	}

	if (ssi->mark) {
		/*
		 * this part is visible unshifted, so make sure no
		 * executables that could be used to give suid
		 * privileges
		 */
		sb->s_iflags = SB_I_NOEXEC;
		ssi->mnt = path.mnt;
		dentry = path.dentry;
	} else {
		struct shiftfs_super_info *mp_ssi;

		/*
		 * this leg executes if we're admin capable in
		 * the namespace, so be very careful
		 */
		if (path.dentry->d_sb->s_magic != SHIFTFS_MAGIC)
			goto out_put;
		mp_ssi = path.dentry->d_sb->s_fs_info;
		if (!mp_ssi->mark)
			goto out_put;
		ssi->mnt = mntget(mp_ssi->mnt);
		dentry = dget(path.dentry->d_fsdata);
		path_put(&path);
	}
	ssi->userns = get_user_ns(dentry->d_sb->s_user_ns);
	sb->s_fs_info = ssi;
	sb->s_magic = SHIFTFS_MAGIC;
	sb->s_op = &shiftfs_super_ops;
	sb->s_xattr = shiftfs_xattr_handlers;
	sb->s_d_op = &shiftfs_dentry_ops;
	sb->s_root = d_make_root(shiftfs_new_inode(sb, S_IFDIR, dentry));
	sb->s_root->d_fsdata = dentry;

	return 0;

 out_put:
	path_put(&path);
 out:
	kfree(name);
	kfree(ssi);
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
	return register_filesystem(&shiftfs_type);
}

static void __exit shiftfs_exit(void)
{
	unregister_filesystem(&shiftfs_type);
}

MODULE_ALIAS_FS("shiftfs");
MODULE_AUTHOR("James Bottomley");
MODULE_DESCRIPTION("uid/gid shifting bind filesystem");
MODULE_LICENSE("GPL v2");
module_init(shiftfs_init)
module_exit(shiftfs_exit)
