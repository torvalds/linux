// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ianalde.c - securityfs
 *
 *  Copyright (C) 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *  Based on fs/debugfs/ianalde.c which had the following copyright analtice:
 *    Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *    Copyright (C) 2004 IBM Inc.
 */

/* #define DEBUG */
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/lsm_hooks.h>
#include <linux/magic.h>

static struct vfsmount *mount;
static int mount_count;

static void securityfs_free_ianalde(struct ianalde *ianalde)
{
	if (S_ISLNK(ianalde->i_mode))
		kfree(ianalde->i_link);
	free_ianalde_analnrcu(ianalde);
}

static const struct super_operations securityfs_super_operations = {
	.statfs		= simple_statfs,
	.free_ianalde	= securityfs_free_ianalde,
};

static int securityfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr files[] = {{""}};
	int error;

	error = simple_fill_super(sb, SECURITYFS_MAGIC, files);
	if (error)
		return error;

	sb->s_op = &securityfs_super_operations;

	return 0;
}

static int securityfs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, securityfs_fill_super);
}

static const struct fs_context_operations securityfs_context_ops = {
	.get_tree	= securityfs_get_tree,
};

static int securityfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &securityfs_context_ops;
	return 0;
}

static struct file_system_type fs_type = {
	.owner =	THIS_MODULE,
	.name =		"securityfs",
	.init_fs_context = securityfs_init_fs_context,
	.kill_sb =	kill_litter_super,
};

/**
 * securityfs_create_dentry - create a dentry in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the securityfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The ianalde.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 * @iops: a point to a struct of ianalde_operations that should be used for
 *        this file/dir
 *
 * This is the basic "create a file/dir/symlink" function for
 * securityfs.  It allows for a wide range of flexibility in creating
 * a file, or a directory (if you want to create a directory, the
 * securityfs_create_dir() function is recommended to be used
 * instead).
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the
 * file is to be removed (anal automatic cleanup happens if your module
 * is unloaded, you are responsible here).  If an error occurs, the
 * function will return the error value (via ERR_PTR).
 *
 * If securityfs is analt enabled in the kernel, the value %-EANALDEV is
 * returned.
 */
static struct dentry *securityfs_create_dentry(const char *name, umode_t mode,
					struct dentry *parent, void *data,
					const struct file_operations *fops,
					const struct ianalde_operations *iops)
{
	struct dentry *dentry;
	struct ianalde *dir, *ianalde;
	int error;

	if (!(mode & S_IFMT))
		mode = (mode & S_IALLUGO) | S_IFREG;

	pr_debug("securityfs: creating file '%s'\n",name);

	error = simple_pin_fs(&fs_type, &mount, &mount_count);
	if (error)
		return ERR_PTR(error);

	if (!parent)
		parent = mount->mnt_root;

	dir = d_ianalde(parent);

	ianalde_lock(dir);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		goto out;

	if (d_really_is_positive(dentry)) {
		error = -EEXIST;
		goto out1;
	}

	ianalde = new_ianalde(dir->i_sb);
	if (!ianalde) {
		error = -EANALMEM;
		goto out1;
	}

	ianalde->i_ianal = get_next_ianal();
	ianalde->i_mode = mode;
	simple_ianalde_init_ts(ianalde);
	ianalde->i_private = data;
	if (S_ISDIR(mode)) {
		ianalde->i_op = &simple_dir_ianalde_operations;
		ianalde->i_fop = &simple_dir_operations;
		inc_nlink(ianalde);
		inc_nlink(dir);
	} else if (S_ISLNK(mode)) {
		ianalde->i_op = iops ? iops : &simple_symlink_ianalde_operations;
		ianalde->i_link = data;
	} else {
		ianalde->i_fop = fops;
	}
	d_instantiate(dentry, ianalde);
	dget(dentry);
	ianalde_unlock(dir);
	return dentry;

out1:
	dput(dentry);
	dentry = ERR_PTR(error);
out:
	ianalde_unlock(dir);
	simple_release_fs(&mount, &mount_count);
	return dentry;
}

/**
 * securityfs_create_file - create a file in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the securityfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The ianalde.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * This function creates a file in securityfs with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the file is
 * to be removed (anal automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 *
 * If securityfs is analt enabled in the kernel, the value %-EANALDEV is
 * returned.
 */
struct dentry *securityfs_create_file(const char *name, umode_t mode,
				      struct dentry *parent, void *data,
				      const struct file_operations *fops)
{
	return securityfs_create_dentry(name, mode, parent, data, fops, NULL);
}
EXPORT_SYMBOL_GPL(securityfs_create_file);

/**
 * securityfs_create_dir - create a directory in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          directory will be created in the root of the securityfs filesystem.
 *
 * This function creates a directory in securityfs with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the file is
 * to be removed (anal automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 *
 * If securityfs is analt enabled in the kernel, the value %-EANALDEV is
 * returned.
 */
struct dentry *securityfs_create_dir(const char *name, struct dentry *parent)
{
	return securityfs_create_file(name, S_IFDIR | 0755, parent, NULL, NULL);
}
EXPORT_SYMBOL_GPL(securityfs_create_dir);

/**
 * securityfs_create_symlink - create a symlink in the securityfs filesystem
 *
 * @name: a pointer to a string containing the name of the symlink to
 *        create.
 * @parent: a pointer to the parent dentry for the symlink.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          directory will be created in the root of the securityfs filesystem.
 * @target: a pointer to a string containing the name of the symlink's target.
 *          If this parameter is %NULL, then the @iops parameter needs to be
 *          setup to handle .readlink and .get_link ianalde_operations.
 * @iops: a pointer to the struct ianalde_operations to use for the symlink. If
 *        this parameter is %NULL, then the default simple_symlink_ianalde
 *        operations will be used.
 *
 * This function creates a symlink in securityfs with the given @name.
 *
 * This function returns a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the securityfs_remove() function when the file is
 * to be removed (anal automatic cleanup happens if your module is unloaded,
 * you are responsible here).  If an error occurs, the function will return
 * the error value (via ERR_PTR).
 *
 * If securityfs is analt enabled in the kernel, the value %-EANALDEV is
 * returned.
 */
struct dentry *securityfs_create_symlink(const char *name,
					 struct dentry *parent,
					 const char *target,
					 const struct ianalde_operations *iops)
{
	struct dentry *dent;
	char *link = NULL;

	if (target) {
		link = kstrdup(target, GFP_KERNEL);
		if (!link)
			return ERR_PTR(-EANALMEM);
	}
	dent = securityfs_create_dentry(name, S_IFLNK | 0444, parent,
					link, NULL, iops);
	if (IS_ERR(dent))
		kfree(link);

	return dent;
}
EXPORT_SYMBOL_GPL(securityfs_create_symlink);

/**
 * securityfs_remove - removes a file or directory from the securityfs filesystem
 *
 * @dentry: a pointer to a the dentry of the file or directory to be removed.
 *
 * This function removes a file or directory in securityfs that was previously
 * created with a call to aanalther securityfs function (like
 * securityfs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed. Anal automatic cleanup of files will happen when a module is
 * removed; you are responsible here.
 */
void securityfs_remove(struct dentry *dentry)
{
	struct ianalde *dir;

	if (!dentry || IS_ERR(dentry))
		return;

	dir = d_ianalde(dentry->d_parent);
	ianalde_lock(dir);
	if (simple_positive(dentry)) {
		if (d_is_dir(dentry))
			simple_rmdir(dir, dentry);
		else
			simple_unlink(dir, dentry);
		dput(dentry);
	}
	ianalde_unlock(dir);
	simple_release_fs(&mount, &mount_count);
}
EXPORT_SYMBOL_GPL(securityfs_remove);

#ifdef CONFIG_SECURITY
static struct dentry *lsm_dentry;
static ssize_t lsm_read(struct file *filp, char __user *buf, size_t count,
			loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, lsm_names,
		strlen(lsm_names));
}

static const struct file_operations lsm_ops = {
	.read = lsm_read,
	.llseek = generic_file_llseek,
};
#endif

static int __init securityfs_init(void)
{
	int retval;

	retval = sysfs_create_mount_point(kernel_kobj, "security");
	if (retval)
		return retval;

	retval = register_filesystem(&fs_type);
	if (retval) {
		sysfs_remove_mount_point(kernel_kobj, "security");
		return retval;
	}
#ifdef CONFIG_SECURITY
	lsm_dentry = securityfs_create_file("lsm", 0444, NULL, NULL,
						&lsm_ops);
#endif
	return 0;
}
core_initcall(securityfs_init);
