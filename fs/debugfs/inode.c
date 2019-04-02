// SPDX-License-Identifier: GPL-2.0
/*
 *  inode.c - part of defs, a tiny little de file system
 *
 *  Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *
 *  defs is for people to use instead of /proc or /sys.
 *  See ./Documentation/core-api/kernel-api.rst for more details.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/defs.h>
#include <linux/fsnotify.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>

#include "internal.h"

#define DEFS_DEFAULT_MODE	0700

static struct vfsmount *defs_mount;
static int defs_mount_count;
static bool defs_registered;

static struct inode *defs_get_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_atime = inode->i_mtime =
			inode->i_ctime = current_time(inode);
	}
	return inode;
}

struct defs_mount_opts {
	kuid_t uid;
	kgid_t gid;
	umode_t mode;
};

enum {
	Opt_uid,
	Opt_gid,
	Opt_mode,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};

struct defs_fs_info {
	struct defs_mount_opts mount_opts;
};

static int defs_parse_options(char *data, struct defs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	kuid_t uid;
	kgid_t gid;
	char *p;

	opts->mode = DEFS_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid))
				return -EINVAL;
			opts->uid = uid;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid))
				return -EINVAL;
			opts->gid = gid;
			break;
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally defs has ignored all mount options
		 */
		}
	}

	return 0;
}

static int defs_apply_options(struct super_block *sb)
{
	struct defs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode = d_inode(sb->s_root);
	struct defs_mount_opts *opts = &fsi->mount_opts;

	inode->i_mode &= ~S_IALLUGO;
	inode->i_mode |= opts->mode;

	inode->i_uid = opts->uid;
	inode->i_gid = opts->gid;

	return 0;
}

static int defs_remount(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct defs_fs_info *fsi = sb->s_fs_info;

	sync_filesystem(sb);
	err = defs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	defs_apply_options(sb);

fail:
	return err;
}

static int defs_show_options(struct seq_file *m, struct dentry *root)
{
	struct defs_fs_info *fsi = root->d_sb->s_fs_info;
	struct defs_mount_opts *opts = &fsi->mount_opts;

	if (!uid_eq(opts->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, opts->uid));
	if (!gid_eq(opts->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, opts->gid));
	if (opts->mode != DEFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", opts->mode);

	return 0;
}

static void defs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	free_inode_nonrcu(inode);
}

static void defs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, defs_i_callback);
}

static const struct super_operations defs_super_operations = {
	.statfs		= simple_statfs,
	.remount_fs	= defs_remount,
	.show_options	= defs_show_options,
	.destroy_inode	= defs_destroy_inode,
};

static void defs_release_dentry(struct dentry *dentry)
{
	void *fsd = dentry->d_fsdata;

	if (!((unsigned long)fsd & DEFS_FSDATA_IS_REAL_FOPS_BIT))
		kfree(dentry->d_fsdata);
}

static struct vfsmount *defs_automount(struct path *path)
{
	defs_automount_t f;
	f = (defs_automount_t)path->dentry->d_fsdata;
	return f(path->dentry, d_inode(path->dentry)->i_private);
}

static const struct dentry_operations defs_dops = {
	.d_delete = always_delete_dentry,
	.d_release = defs_release_dentry,
	.d_automount = defs_automount,
};

static int de_fill_super(struct super_block *sb, void *data, int silent)
{
	static const struct tree_descr de_files[] = {{""}};
	struct defs_fs_info *fsi;
	int err;

	fsi = kzalloc(sizeof(struct defs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi) {
		err = -ENOMEM;
		goto fail;
	}

	err = defs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	err  =  simple_fill_super(sb, DEFS_MAGIC, de_files);
	if (err)
		goto fail;

	sb->s_op = &defs_super_operations;
	sb->s_d_op = &defs_dops;

	defs_apply_options(sb);

	return 0;

fail:
	kfree(fsi);
	sb->s_fs_info = NULL;
	return err;
}

static struct dentry *de_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data)
{
	return mount_single(fs_type, flags, data, de_fill_super);
}

static struct file_system_type de_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"defs",
	.mount =	de_mount,
	.kill_sb =	kill_litter_super,
};
MODULE_ALIAS_FS("defs");

/**
 * defs_lookup() - look up an existing defs file
 * @name: a pointer to a string containing the name of the file to look up.
 * @parent: a pointer to the parent dentry of the file.
 *
 * This function will return a pointer to a dentry if it succeeds.  If the file
 * doesn't exist or an error occurs, %NULL will be returned.  The returned
 * dentry must be passed to dput() when it is no longer needed.
 *
 * If defs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *defs_lookup(const char *name, struct dentry *parent)
{
	struct dentry *dentry;

	if (IS_ERR(parent))
		return NULL;

	if (!parent)
		parent = defs_mount->mnt_root;

	dentry = lookup_one_len_unlocked(name, parent, strlen(name));
	if (IS_ERR(dentry))
		return NULL;
	if (!d_really_is_positive(dentry)) {
		dput(dentry);
		return NULL;
	}
	return dentry;
}
EXPORT_SYMBOL_GPL(defs_lookup);

static struct dentry *start_creating(const char *name, struct dentry *parent)
{
	struct dentry *dentry;
	int error;

	pr_de("defs: creating file '%s'\n",name);

	if (IS_ERR(parent))
		return parent;

	error = simple_pin_fs(&de_fs_type, &defs_mount,
			      &defs_mount_count);
	if (error)
		return ERR_PTR(error);

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = defs_mount->mnt_root;

	inode_lock(d_inode(parent));
	dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(dentry) && d_really_is_positive(dentry)) {
		dput(dentry);
		dentry = ERR_PTR(-EEXIST);
	}

	if (IS_ERR(dentry)) {
		inode_unlock(d_inode(parent));
		simple_release_fs(&defs_mount, &defs_mount_count);
	}

	return dentry;
}

static struct dentry *failed_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	dput(dentry);
	simple_release_fs(&defs_mount, &defs_mount_count);
	return ERR_PTR(-ENOMEM);
}

static struct dentry *end_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	return dentry;
}

static struct dentry *__defs_create_file(const char *name, umode_t mode,
				struct dentry *parent, void *data,
				const struct file_operations *proxy_fops,
				const struct file_operations *real_fops)
{
	struct dentry *dentry;
	struct inode *inode;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;
	_ON(!S_ISREG(mode));
	dentry = start_creating(name, parent);

	if (IS_ERR(dentry))
		return dentry;

	inode = defs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return failed_creating(dentry);

	inode->i_mode = mode;
	inode->i_private = data;

	inode->i_fop = proxy_fops;
	dentry->d_fsdata = (void *)((unsigned long)real_fops |
				DEFS_FSDATA_IS_REAL_FOPS_BIT);

	d_instantiate(dentry, inode);
	fsnotify_create(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}

/**
 * defs_create_file - create a file in the defs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the defs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * This is the basic "create a file" function for defs.  It allows for a
 * wide range of flexibility in creating a file, or a directory (if you want
 * to create a directory, the defs_create_dir() function is
 * recommended to be used instead.)
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the defs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %ERR_PTR(-ERROR) will be
 * returned.
 *
 * If defs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *defs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{

	return __defs_create_file(name, mode, parent, data,
				fops ? &defs_full_proxy_file_operations :
					&defs_noop_file_operations,
				fops);
}
EXPORT_SYMBOL_GPL(defs_create_file);

/**
 * defs_create_file_unsafe - create a file in the defs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the defs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * defs_create_file_unsafe() is completely analogous to
 * defs_create_file(), the only difference being that the fops
 * handed it will not get protected against file removals by the
 * defs core.
 *
 * It is your responsibility to protect your struct file_operation
 * methods against file removals by means of defs_file_get()
 * and defs_file_put(). ->open() is still protected by
 * defs though.
 *
 * Any struct file_operations defined by means of
 * DEFINE_DEFS_ATTRIBUTE() is protected against file removals and
 * thus, may be used here.
 */
struct dentry *defs_create_file_unsafe(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{

	return __defs_create_file(name, mode, parent, data,
				fops ? &defs_open_proxy_file_operations :
					&defs_noop_file_operations,
				fops);
}
EXPORT_SYMBOL_GPL(defs_create_file_unsafe);

/**
 * defs_create_file_size - create a file in the defs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the defs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 * @file_size: initial file size
 *
 * This is the basic "create a file" function for defs.  It allows for a
 * wide range of flexibility in creating a file, or a directory (if you want
 * to create a directory, the defs_create_dir() function is
 * recommended to be used instead.)
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the defs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %ERR_PTR(-ERROR) will be
 * returned.
 *
 * If defs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *defs_create_file_size(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops,
					loff_t file_size)
{
	struct dentry *de = defs_create_file(name, mode, parent, data, fops);

	if (de)
		d_inode(de)->i_size = file_size;
	return de;
}
EXPORT_SYMBOL_GPL(defs_create_file_size);

/**
 * defs_create_dir - create a directory in the defs filesystem
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          directory will be created in the root of the defs filesystem.
 *
 * This function creates a directory in defs with the given name.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the defs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %ERR_PTR(-ERROR) will be
 * returned.
 *
 * If defs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *defs_create_dir(const char *name, struct dentry *parent)
{
	struct dentry *dentry = start_creating(name, parent);
	struct inode *inode;

	if (IS_ERR(dentry))
		return dentry;

	inode = defs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return failed_creating(dentry);

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(d_inode(dentry->d_parent));
	fsnotify_mkdir(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}
EXPORT_SYMBOL_GPL(defs_create_dir);

/**
 * defs_create_automount - create automount point in the defs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the defs filesystem.
 * @f: function to be called when pathname resolution steps on that one.
 * @data: opaque argument to pass to f().
 *
 * @f should return what ->d_automount() would.
 */
struct dentry *defs_create_automount(const char *name,
					struct dentry *parent,
					defs_automount_t f,
					void *data)
{
	struct dentry *dentry = start_creating(name, parent);
	struct inode *inode;

	if (IS_ERR(dentry))
		return dentry;

	inode = defs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return failed_creating(dentry);

	make_empty_dir_inode(inode);
	inode->i_flags |= S_AUTOMOUNT;
	inode->i_private = data;
	dentry->d_fsdata = (void *)f;
	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(d_inode(dentry->d_parent));
	fsnotify_mkdir(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}
EXPORT_SYMBOL(defs_create_automount);

/**
 * defs_create_symlink- create a symbolic link in the defs filesystem
 * @name: a pointer to a string containing the name of the symbolic link to
 *        create.
 * @parent: a pointer to the parent dentry for this symbolic link.  This
 *          should be a directory dentry if set.  If this parameter is NULL,
 *          then the symbolic link will be created in the root of the defs
 *          filesystem.
 * @target: a pointer to a string containing the path to the target of the
 *          symbolic link.
 *
 * This function creates a symbolic link with the given name in defs that
 * links to the given target path.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the defs_remove() function when the symbolic
 * link is to be removed (no automatic cleanup happens if your module is
 * unloaded, you are responsible here.)  If an error occurs, %ERR_PTR(-ERROR)
 * will be returned.
 *
 * If defs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *defs_create_symlink(const char *name, struct dentry *parent,
				      const char *target)
{
	struct dentry *dentry;
	struct inode *inode;
	char *link = kstrdup(target, GFP_KERNEL);
	if (!link)
		return ERR_PTR(-ENOMEM);

	dentry = start_creating(name, parent);
	if (IS_ERR(dentry)) {
		kfree(link);
		return dentry;
	}

	inode = defs_get_inode(dentry->d_sb);
	if (unlikely(!inode)) {
		kfree(link);
		return failed_creating(dentry);
	}
	inode->i_mode = S_IFLNK | S_IRWXUGO;
	inode->i_op = &simple_symlink_inode_operations;
	inode->i_link = link;
	d_instantiate(dentry, inode);
	return end_creating(dentry);
}
EXPORT_SYMBOL_GPL(defs_create_symlink);

static void __defs_remove_file(struct dentry *dentry, struct dentry *parent)
{
	struct defs_fsdata *fsd;

	simple_unlink(d_inode(parent), dentry);
	d_delete(dentry);

	/*
	 * Paired with the closing smp_mb() implied by a successful
	 * cmpxchg() in defs_file_get(): either
	 * defs_file_get() must see a dead dentry or we must see a
	 * defs_fsdata instance at ->d_fsdata here (or both).
	 */
	smp_mb();
	fsd = READ_ONCE(dentry->d_fsdata);
	if ((unsigned long)fsd & DEFS_FSDATA_IS_REAL_FOPS_BIT)
		return;
	if (!refcount_dec_and_test(&fsd->active_users))
		wait_for_completion(&fsd->active_users_drained);
}

static int __defs_remove(struct dentry *dentry, struct dentry *parent)
{
	int ret = 0;

	if (simple_positive(dentry)) {
		dget(dentry);
		if (!d_is_reg(dentry)) {
			if (d_is_dir(dentry))
				ret = simple_rmdir(d_inode(parent), dentry);
			else
				simple_unlink(d_inode(parent), dentry);
			if (!ret)
				d_delete(dentry);
		} else {
			__defs_remove_file(dentry, parent);
		}
		dput(dentry);
	}
	return ret;
}

/**
 * defs_remove - removes a file or directory from the defs filesystem
 * @dentry: a pointer to a the dentry of the file or directory to be
 *          removed.  If this parameter is NULL or an error value, nothing
 *          will be done.
 *
 * This function removes a file or directory in defs that was previously
 * created with a call to another defs function (like
 * defs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed, no automatic cleanup of files will happen when a module is
 * removed, you are responsible here.
 */
void defs_remove(struct dentry *dentry)
{
	struct dentry *parent;
	int ret;

	if (IS_ERR_OR_NULL(dentry))
		return;

	parent = dentry->d_parent;
	inode_lock(d_inode(parent));
	ret = __defs_remove(dentry, parent);
	inode_unlock(d_inode(parent));
	if (!ret)
		simple_release_fs(&defs_mount, &defs_mount_count);
}
EXPORT_SYMBOL_GPL(defs_remove);

/**
 * defs_remove_recursive - recursively removes a directory
 * @dentry: a pointer to a the dentry of the directory to be removed.  If this
 *          parameter is NULL or an error value, nothing will be done.
 *
 * This function recursively removes a directory tree in defs that
 * was previously created with a call to another defs function
 * (like defs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed, no automatic cleanup of files will happen when a module is
 * removed, you are responsible here.
 */
void defs_remove_recursive(struct dentry *dentry)
{
	struct dentry *child, *parent;

	if (IS_ERR_OR_NULL(dentry))
		return;

	parent = dentry;
 down:
	inode_lock(d_inode(parent));
 loop:
	/*
	 * The parent->d_subdirs is protected by the d_lock. Outside that
	 * lock, the child can be unlinked and set to be freed which can
	 * use the d_u.d_child as the rcu head and corrupt this list.
	 */
	spin_lock(&parent->d_lock);
	list_for_each_entry(child, &parent->d_subdirs, d_child) {
		if (!simple_positive(child))
			continue;

		/* perhaps simple_empty(child) makes more sense */
		if (!list_empty(&child->d_subdirs)) {
			spin_unlock(&parent->d_lock);
			inode_unlock(d_inode(parent));
			parent = child;
			goto down;
		}

		spin_unlock(&parent->d_lock);

		if (!__defs_remove(child, parent))
			simple_release_fs(&defs_mount, &defs_mount_count);

		/*
		 * The parent->d_lock protects agaist child from unlinking
		 * from d_subdirs. When releasing the parent->d_lock we can
		 * no longer trust that the next pointer is valid.
		 * Restart the loop. We'll skip this one with the
		 * simple_positive() check.
		 */
		goto loop;
	}
	spin_unlock(&parent->d_lock);

	inode_unlock(d_inode(parent));
	child = parent;
	parent = parent->d_parent;
	inode_lock(d_inode(parent));

	if (child != dentry)
		/* go up */
		goto loop;

	if (!__defs_remove(child, parent))
		simple_release_fs(&defs_mount, &defs_mount_count);
	inode_unlock(d_inode(parent));
}
EXPORT_SYMBOL_GPL(defs_remove_recursive);

/**
 * defs_rename - rename a file/directory in the defs filesystem
 * @old_dir: a pointer to the parent dentry for the renamed object. This
 *          should be a directory dentry.
 * @old_dentry: dentry of an object to be renamed.
 * @new_dir: a pointer to the parent dentry where the object should be
 *          moved. This should be a directory dentry.
 * @new_name: a pointer to a string containing the target name.
 *
 * This function renames a file/directory in defs.  The target must not
 * exist for rename to succeed.
 *
 * This function will return a pointer to old_dentry (which is updated to
 * reflect renaming) if it succeeds. If an error occurs, %NULL will be
 * returned.
 *
 * If defs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *defs_rename(struct dentry *old_dir, struct dentry *old_dentry,
		struct dentry *new_dir, const char *new_name)
{
	int error;
	struct dentry *dentry = NULL, *trap;
	struct name_snapshot old_name;

	if (IS_ERR(old_dir))
		return old_dir;
	if (IS_ERR(new_dir))
		return new_dir;
	if (IS_ERR_OR_NULL(old_dentry))
		return old_dentry;

	trap = lock_rename(new_dir, old_dir);
	/* Source or destination directories don't exist? */
	if (d_really_is_negative(old_dir) || d_really_is_negative(new_dir))
		goto exit;
	/* Source does not exist, cyclic rename, or mountpoint? */
	if (d_really_is_negative(old_dentry) || old_dentry == trap ||
	    d_mountpoint(old_dentry))
		goto exit;
	dentry = lookup_one_len(new_name, new_dir, strlen(new_name));
	/* Lookup failed, cyclic rename or target exists? */
	if (IS_ERR(dentry) || dentry == trap || d_really_is_positive(dentry))
		goto exit;

	take_dentry_name_snapshot(&old_name, old_dentry);

	error = simple_rename(d_inode(old_dir), old_dentry, d_inode(new_dir),
			      dentry, 0);
	if (error) {
		release_dentry_name_snapshot(&old_name);
		goto exit;
	}
	d_move(old_dentry, dentry);
	fsnotify_move(d_inode(old_dir), d_inode(new_dir), old_name.name,
		d_is_dir(old_dentry),
		NULL, old_dentry);
	release_dentry_name_snapshot(&old_name);
	unlock_rename(new_dir, old_dir);
	dput(dentry);
	return old_dentry;
exit:
	if (dentry && !IS_ERR(dentry))
		dput(dentry);
	unlock_rename(new_dir, old_dir);
	if (IS_ERR(dentry))
		return dentry;
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(defs_rename);

/**
 * defs_initialized - Tells whether defs has been registered
 */
bool defs_initialized(void)
{
	return defs_registered;
}
EXPORT_SYMBOL_GPL(defs_initialized);

static int __init defs_init(void)
{
	int retval;

	retval = sysfs_create_mount_point(kernel_kobj, "de");
	if (retval)
		return retval;

	retval = register_filesystem(&de_fs_type);
	if (retval)
		sysfs_remove_mount_point(kernel_kobj, "de");
	else
		defs_registered = true;

	return retval;
}
core_initcall(defs_init);

