// SPDX-License-Identifier: GPL-2.0
/*
 * Routines that mimic syscalls, but don't use the user address space or file
 * descriptors.  Only for init/ and related early init code.
 */
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/init_syscalls.h>
#include "internal.h"

int __init init_mount(const char *dev_name, const char *dir_name,
		const char *type_page, unsigned long flags, void *data_page)
{
	struct path path;
	int ret;

	ret = kern_path(dir_name, LOOKUP_FOLLOW, &path);
	if (ret)
		return ret;
	ret = path_mount(dev_name, &path, type_page, flags, data_page);
	path_put(&path);
	return ret;
}

int __init init_umount(const char *name, int flags)
{
	int lookup_flags = LOOKUP_MOUNTPOINT;
	struct path path;
	int ret;

	if (!(flags & UMOUNT_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	ret = kern_path(name, lookup_flags, &path);
	if (ret)
		return ret;
	return path_umount(&path, flags);
}

int __init init_chdir(const char *filename)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &path);
	if (error)
		return error;
	error = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
	if (!error)
		set_fs_pwd(current->fs, &path);
	path_put(&path);
	return error;
}

int __init init_unlink(const char *pathname)
{
	return do_unlinkat(AT_FDCWD, getname_kernel(pathname));
}

int __init init_rmdir(const char *pathname)
{
	return do_rmdir(AT_FDCWD, getname_kernel(pathname));
}
