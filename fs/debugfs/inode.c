/*
 *  file.c - part of debugfs, a tiny little debug file system
 *
 *  Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See Documentation/DocBook/kernel-api for more details.
 *
 */

/* uncomment to get debug messages from the debug filesystem, ah the irony. */
/* #define DEBUG */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/debugfs.h>
#include <linux/fsnotify.h>
#include <linux/string.h>
#include <linux/magic.h>

static struct vfsmount *debugfs_mount;
static int debugfs_mount_count;
static bool debugfs_registered;

static struct inode *debugfs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = &debugfs_file_operations;
			break;
		case S_IFLNK:
			inode->i_op = &debugfs_link_operations;
			break;
		case S_IFDIR:
			inode->i_op = &simple_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2
			 * (for "." entry) */
			inc_nlink(inode);
			break;
		}
	}
	return inode; 
}

/* SMP-safe */
static int debugfs_mknod(struct inode *dir, struct dentry *dentry,
			 int mode, dev_t dev)
{
	struct inode *inode;
	int error = -EPERM;

	if (dentry->d_inode)
		return -EEXIST;

	inode = debugfs_get_inode(dir->i_sb, mode, dev);
	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}

static int debugfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;

	mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
	res = debugfs_mknod(dir, dentry, mode, 0);
	if (!res) {
		inc_nlink(dir);
		fsnotify_mkdir(dir, dentry);
	}
	return res;
}

static int debugfs_link(struct inode *dir, struct dentry *dentry, int mode)
{
	mode = (mode & S_IALLUGO) | S_IFLNK;
	return debugfs_mknod(dir, dentry, mode, 0);
}

static int debugfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;

	mode = (mode & S_IALLUGO) | S_IFREG;
	res = debugfs_mknod(dir, dentry, mode, 0);
	if (!res)
		fsnotify_create(dir, dentry);
	return res;
}

static inline int debugfs_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static int debug_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr debug_files[] = {{""}};

	return simple_fill_super(sb, DEBUGFS_MAGIC, debug_files);
}

static int debug_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, debug_fill_super, mnt);
}

static struct file_system_type debug_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"debugfs",
	.get_sb =	debug_get_sb,
	.kill_sb =	kill_litter_super,
};

static int debugfs_create_by_name(const char *name, mode_t mode,
				  struct dentry *parent,
				  struct dentry **dentry)
{
	int error = 0;

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super 
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent) {
		if (debugfs_mount && debugfs_mount->mnt_sb) {
			parent = debugfs_mount->mnt_sb->s_root;
		}
	}
	if (!parent) {
		pr_debug("debugfs: Ah! can not find a parent!\n");
		return -EFAULT;
	}

	*dentry = NULL;
	mutex_lock(&parent->d_inode->i_mutex);
	*dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(*dentry)) {
		switch (mode & S_IFMT) {
		case S_IFDIR:
			error = debugfs_mkdir(parent->d_inode, *dentry, mode);
			break;
		case S_IFLNK:
			error = debugfs_link(parent->d_inode, *dentry, mode);
			break;
		default:
			error = debugfs_create(parent->d_inode, *dentry, mode);
			break;
		}
		dput(*dentry);
	} else
		error = PTR_ERR(*dentry);
	mutex_unlock(&parent->d_inode->i_mutex);

	return error;
}

/**
 * debugfs_create_file - create a file in the debugfs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this paramater is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * This is the basic "create a file" function for debugfs.  It allows for a
 * wide range of flexibility in createing a file, or a directory (if you
 * want to create a directory, the debugfs_create_dir() function is
 * recommended to be used instead.)
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_create_file(const char *name, mode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{
	struct dentry *dentry = NULL;
	int error;

	pr_debug("debugfs: creating file '%s'\n",name);

	error = simple_pin_fs(&debug_fs_type, &debugfs_mount,
			      &debugfs_mount_count);
	if (error)
		goto exit;

	error = debugfs_create_by_name(name, mode, parent, &dentry);
	if (error) {
		dentry = NULL;
		simple_release_fs(&debugfs_mount, &debugfs_mount_count);
		goto exit;
	}

	if (dentry->d_inode) {
		if (data)
			dentry->d_inode->i_private = data;
		if (fops)
			dentry->d_inode->i_fop = fops;
	}
exit:
	return dentry;
}
EXPORT_SYMBOL_GPL(debugfs_create_file);

/**
 * debugfs_create_dir - create a directory in the debugfs filesystem
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this paramater is NULL, then the
 *          directory will be created in the root of the debugfs filesystem.
 *
 * This function creates a directory in debugfs with the given name.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_create_dir(const char *name, struct dentry *parent)
{
	return debugfs_create_file(name, 
				   S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
				   parent, NULL, NULL);
}
EXPORT_SYMBOL_GPL(debugfs_create_dir);

/**
 * debugfs_create_symlink- create a symbolic link in the debugfs filesystem
 * @name: a pointer to a string containing the name of the symbolic link to
 *        create.
 * @parent: a pointer to the parent dentry for this symbolic link.  This
 *          should be a directory dentry if set.  If this paramater is NULL,
 *          then the symbolic link will be created in the root of the debugfs
 *          filesystem.
 * @target: a pointer to a string containing the path to the target of the
 *          symbolic link.
 *
 * This function creates a symbolic link with the given name in debugfs that
 * links to the given target path.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the symbolic
 * link is to be removed (no automatic cleanup happens if your module is
 * unloaded, you are responsible here.)  If an error occurs, %NULL will be
 * returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_create_symlink(const char *name, struct dentry *parent,
				      const char *target)
{
	struct dentry *result;
	char *link;

	link = kstrdup(target, GFP_KERNEL);
	if (!link)
		return NULL;

	result = debugfs_create_file(name, S_IFLNK | S_IRWXUGO, parent, link,
				     NULL);
	if (!result)
		kfree(link);
	return result;
}
EXPORT_SYMBOL_GPL(debugfs_create_symlink);

static void __debugfs_remove(struct dentry *dentry, struct dentry *parent)
{
	int ret = 0;

	if (debugfs_positive(dentry)) {
		if (dentry->d_inode) {
			dget(dentry);
			switch (dentry->d_inode->i_mode & S_IFMT) {
			case S_IFDIR:
				ret = simple_rmdir(parent->d_inode, dentry);
				break;
			case S_IFLNK:
				kfree(dentry->d_inode->i_private);
				/* fall through */
			default:
				simple_unlink(parent->d_inode, dentry);
				break;
			}
			if (!ret)
				d_delete(dentry);
			dput(dentry);
		}
	}
}

/**
 * debugfs_remove - removes a file or directory from the debugfs filesystem
 * @dentry: a pointer to a the dentry of the file or directory to be
 *          removed.
 *
 * This function removes a file or directory in debugfs that was previously
 * created with a call to another debugfs function (like
 * debugfs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed, no automatic cleanup of files will happen when a module is
 * removed, you are responsible here.
 */
void debugfs_remove(struct dentry *dentry)
{
	struct dentry *parent;
	
	if (!dentry)
		return;

	parent = dentry->d_parent;
	if (!parent || !parent->d_inode)
		return;

	mutex_lock(&parent->d_inode->i_mutex);
	__debugfs_remove(dentry, parent);
	mutex_unlock(&parent->d_inode->i_mutex);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
}
EXPORT_SYMBOL_GPL(debugfs_remove);

/**
 * debugfs_remove_recursive - recursively removes a directory
 * @dentry: a pointer to a the dentry of the directory to be removed.
 *
 * This function recursively removes a directory tree in debugfs that
 * was previously created with a call to another debugfs function
 * (like debugfs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed, no automatic cleanup of files will happen when a module is
 * removed, you are responsible here.
 */
void debugfs_remove_recursive(struct dentry *dentry)
{
	struct dentry *child;
	struct dentry *parent;

	if (!dentry)
		return;

	parent = dentry->d_parent;
	if (!parent || !parent->d_inode)
		return;

	parent = dentry;
	mutex_lock(&parent->d_inode->i_mutex);

	while (1) {
		/*
		 * When all dentries under "parent" has been removed,
		 * walk up the tree until we reach our starting point.
		 */
		if (list_empty(&parent->d_subdirs)) {
			mutex_unlock(&parent->d_inode->i_mutex);
			if (parent == dentry)
				break;
			parent = parent->d_parent;
			mutex_lock(&parent->d_inode->i_mutex);
		}
		child = list_entry(parent->d_subdirs.next, struct dentry,
				d_u.d_child);

		/*
		 * If "child" isn't empty, walk down the tree and
		 * remove all its descendants first.
		 */
		if (!list_empty(&child->d_subdirs)) {
			mutex_unlock(&parent->d_inode->i_mutex);
			parent = child;
			mutex_lock(&parent->d_inode->i_mutex);
			continue;
		}
		__debugfs_remove(child, parent);
		if (parent->d_subdirs.next == &child->d_u.d_child) {
			/*
			 * Avoid infinite loop if we fail to remove
			 * one dentry.
			 */
			mutex_unlock(&parent->d_inode->i_mutex);
			break;
		}
		simple_release_fs(&debugfs_mount, &debugfs_mount_count);
	}

	parent = dentry->d_parent;
	mutex_lock(&parent->d_inode->i_mutex);
	__debugfs_remove(dentry, parent);
	mutex_unlock(&parent->d_inode->i_mutex);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
}
EXPORT_SYMBOL_GPL(debugfs_remove_recursive);

/**
 * debugfs_rename - rename a file/directory in the debugfs filesystem
 * @old_dir: a pointer to the parent dentry for the renamed object. This
 *          should be a directory dentry.
 * @old_dentry: dentry of an object to be renamed.
 * @new_dir: a pointer to the parent dentry where the object should be
 *          moved. This should be a directory dentry.
 * @new_name: a pointer to a string containing the target name.
 *
 * This function renames a file/directory in debugfs.  The target must not
 * exist for rename to succeed.
 *
 * This function will return a pointer to old_dentry (which is updated to
 * reflect renaming) if it succeeds. If an error occurs, %NULL will be
 * returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_rename(struct dentry *old_dir, struct dentry *old_dentry,
		struct dentry *new_dir, const char *new_name)
{
	int error;
	struct dentry *dentry = NULL, *trap;
	const char *old_name;

	trap = lock_rename(new_dir, old_dir);
	/* Source or destination directories don't exist? */
	if (!old_dir->d_inode || !new_dir->d_inode)
		goto exit;
	/* Source does not exist, cyclic rename, or mountpoint? */
	if (!old_dentry->d_inode || old_dentry == trap ||
	    d_mountpoint(old_dentry))
		goto exit;
	dentry = lookup_one_len(new_name, new_dir, strlen(new_name));
	/* Lookup failed, cyclic rename or target exists? */
	if (IS_ERR(dentry) || dentry == trap || dentry->d_inode)
		goto exit;

	old_name = fsnotify_oldname_init(old_dentry->d_name.name);

	error = simple_rename(old_dir->d_inode, old_dentry, new_dir->d_inode,
		dentry);
	if (error) {
		fsnotify_oldname_free(old_name);
		goto exit;
	}
	d_move(old_dentry, dentry);
	fsnotify_move(old_dir->d_inode, new_dir->d_inode, old_name,
		old_dentry->d_name.name, S_ISDIR(old_dentry->d_inode->i_mode),
		NULL, old_dentry);
	fsnotify_oldname_free(old_name);
	unlock_rename(new_dir, old_dir);
	dput(dentry);
	return old_dentry;
exit:
	if (dentry && !IS_ERR(dentry))
		dput(dentry);
	unlock_rename(new_dir, old_dir);
	return NULL;
}
EXPORT_SYMBOL_GPL(debugfs_rename);

/**
 * debugfs_initialized - Tells whether debugfs has been registered
 */
bool debugfs_initialized(void)
{
	return debugfs_registered;
}
EXPORT_SYMBOL_GPL(debugfs_initialized);


static struct kobject *debug_kobj;

static int __init debugfs_init(void)
{
	int retval;

	debug_kobj = kobject_create_and_add("debug", kernel_kobj);
	if (!debug_kobj)
		return -EINVAL;

	retval = register_filesystem(&debug_fs_type);
	if (retval)
		kobject_put(debug_kobj);
	else
		debugfs_registered = true;

	return retval;
}

static void __exit debugfs_exit(void)
{
	debugfs_registered = false;

	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
	unregister_filesystem(&debug_fs_type);
	kobject_put(debug_kobj);
}

core_initcall(debugfs_init);
module_exit(debugfs_exit);
MODULE_LICENSE("GPL");

