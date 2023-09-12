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
#include <linux/file.h>
#include <linux/init_syscalls.h>
#include <linux/security.h>
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
	error = path_permission(&path, MAY_EXEC | MAY_CHDIR);
	if (!error)
		set_fs_pwd(current->fs, &path);
	path_put(&path);
	return error;
}

int __init init_chroot(const char *filename)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &path);
	if (error)
		return error;
	error = path_permission(&path, MAY_EXEC | MAY_CHDIR);
	if (error)
		goto dput_and_out;
	error = -EPERM;
	if (!ns_capable(current_user_ns(), CAP_SYS_CHROOT))
		goto dput_and_out;
	error = security_path_chroot(&path);
	if (error)
		goto dput_and_out;
	set_fs_root(current->fs, &path);
dput_and_out:
	path_put(&path);
	return error;
}

int __init init_chown(const char *filename, uid_t user, gid_t group, int flags)
{
	int lookup_flags = (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	struct path path;
	int error;

	error = kern_path(filename, lookup_flags, &path);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = chown_common(&path, user, group);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	return error;
}

int __init init_chmod(const char *filename, umode_t mode)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (error)
		return error;
	error = chmod_common(&path, mode);
	path_put(&path);
	return error;
}

int __init init_eaccess(const char *filename)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (error)
		return error;
	error = path_permission(&path, MAY_ACCESS);
	path_put(&path);
	return error;
}

int __init init_stat(const char *filename, struct kstat *stat, int flags)
{
	int lookup_flags = (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	struct path path;
	int error;

	error = kern_path(filename, lookup_flags, &path);
	if (error)
		return error;
	error = vfs_getattr(&path, stat, STATX_BASIC_STATS,
			    flags | AT_NO_AUTOMOUNT);
	path_put(&path);
	return error;
}

int __init init_mknod(const char *filename, umode_t mode, unsigned int dev)
{
	struct dentry *dentry;
	struct path path;
	int error;

	if (S_ISFIFO(mode) || S_ISSOCK(mode))
		dev = 0;
	else if (!(S_ISBLK(mode) || S_ISCHR(mode)))
		return -EINVAL;

	dentry = kern_path_create(AT_FDCWD, filename, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	mode = mode_strip_umask(d_inode(path.dentry), mode);
	error = security_path_mknod(&path, dentry, mode, dev);
	if (!error)
		error = vfs_mknod(mnt_idmap(path.mnt), path.dentry->d_inode,
				  dentry, mode, new_decode_dev(dev));
	done_path_create(&path, dentry);
	return error;
}

int __init init_link(const char *oldname, const char *newname)
{
	struct dentry *new_dentry;
	struct path old_path, new_path;
	struct mnt_idmap *idmap;
	int error;

	error = kern_path(oldname, 0, &old_path);
	if (error)
		return error;

	new_dentry = kern_path_create(AT_FDCWD, newname, &new_path, 0);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto out;

	error = -EXDEV;
	if (old_path.mnt != new_path.mnt)
		goto out_dput;
	idmap = mnt_idmap(new_path.mnt);
	error = may_linkat(idmap, &old_path);
	if (unlikely(error))
		goto out_dput;
	error = security_path_link(old_path.dentry, &new_path, new_dentry);
	if (error)
		goto out_dput;
	error = vfs_link(old_path.dentry, idmap, new_path.dentry->d_inode,
			 new_dentry, NULL);
out_dput:
	done_path_create(&new_path, new_dentry);
out:
	path_put(&old_path);
	return error;
}

int __init init_symlink(const char *oldname, const char *newname)
{
	struct dentry *dentry;
	struct path path;
	int error;

	dentry = kern_path_create(AT_FDCWD, newname, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	error = security_path_symlink(&path, dentry, oldname);
	if (!error)
		error = vfs_symlink(mnt_idmap(path.mnt), path.dentry->d_inode,
				    dentry, oldname);
	done_path_create(&path, dentry);
	return error;
}

int __init init_unlink(const char *pathname)
{
	return do_unlinkat(AT_FDCWD, getname_kernel(pathname));
}

int __init init_mkdir(const char *pathname, umode_t mode)
{
	struct dentry *dentry;
	struct path path;
	int error;

	dentry = kern_path_create(AT_FDCWD, pathname, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	mode = mode_strip_umask(d_inode(path.dentry), mode);
	error = security_path_mkdir(&path, dentry, mode);
	if (!error)
		error = vfs_mkdir(mnt_idmap(path.mnt), path.dentry->d_inode,
				  dentry, mode);
	done_path_create(&path, dentry);
	return error;
}

int __init init_rmdir(const char *pathname)
{
	return do_rmdir(AT_FDCWD, getname_kernel(pathname));
}

int __init init_utimes(char *filename, struct timespec64 *ts)
{
	struct path path;
	int error;

	error = kern_path(filename, 0, &path);
	if (error)
		return error;
	error = vfs_utimes(&path, ts);
	path_put(&path);
	return error;
}

int __init init_dup(struct file *file)
{
	int fd;

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;
	fd_install(fd, get_file(file));
	return 0;
}
