// SPDX-License-Identifier: GPL-2.0
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/exportfs.h>
#include <linux/fs_struct.h>
#include <linux/fsnotify.h>
#include <linux/personality.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include "internal.h"
#include "mount.h"

static long do_sys_name_to_handle(const struct path *path,
				  struct file_handle __user *ufh,
				  int __user *mnt_id, int fh_flags)
{
	long retval;
	struct file_handle f_handle;
	int handle_dwords, handle_bytes;
	struct file_handle *handle = NULL;

	/*
	 * We need to make sure whether the file system support decoding of
	 * the file handle if decodeable file handle was requested.
	 * Otherwise, even empty export_operations are sufficient to opt-in
	 * to encoding FIDs.
	 */
	if (!path->dentry->d_sb->s_export_op ||
	    (!(fh_flags & EXPORT_FH_FID) &&
	     !path->dentry->d_sb->s_export_op->fh_to_dentry))
		return -EOPNOTSUPP;

	if (copy_from_user(&f_handle, ufh, sizeof(struct file_handle)))
		return -EFAULT;

	if (f_handle.handle_bytes > MAX_HANDLE_SZ)
		return -EINVAL;

	handle = kmalloc(sizeof(struct file_handle) + f_handle.handle_bytes,
			 GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	/* convert handle size to multiple of sizeof(u32) */
	handle_dwords = f_handle.handle_bytes >> 2;

	/* we ask for a non connectable maybe decodeable file handle */
	retval = exportfs_encode_fh(path->dentry,
				    (struct fid *)handle->f_handle,
				    &handle_dwords, fh_flags);
	handle->handle_type = retval;
	/* convert handle size to bytes */
	handle_bytes = handle_dwords * sizeof(u32);
	handle->handle_bytes = handle_bytes;
	if ((handle->handle_bytes > f_handle.handle_bytes) ||
	    (retval == FILEID_INVALID) || (retval < 0)) {
		/* As per old exportfs_encode_fh documentation
		 * we could return ENOSPC to indicate overflow
		 * But file system returned 255 always. So handle
		 * both the values
		 */
		if (retval == FILEID_INVALID || retval == -ENOSPC)
			retval = -EOVERFLOW;
		/*
		 * set the handle size to zero so we copy only
		 * non variable part of the file_handle
		 */
		handle_bytes = 0;
	} else
		retval = 0;
	/* copy the mount id */
	if (put_user(real_mount(path->mnt)->mnt_id, mnt_id) ||
	    copy_to_user(ufh, handle,
			 sizeof(struct file_handle) + handle_bytes))
		retval = -EFAULT;
	kfree(handle);
	return retval;
}

/**
 * sys_name_to_handle_at: convert name to handle
 * @dfd: directory relative to which name is interpreted if not absolute
 * @name: name that should be converted to handle.
 * @handle: resulting file handle
 * @mnt_id: mount id of the file system containing the file
 * @flag: flag value to indicate whether to follow symlink or not
 *        and whether a decodable file handle is required.
 *
 * @handle->handle_size indicate the space available to store the
 * variable part of the file handle in bytes. If there is not
 * enough space, the field is updated to return the minimum
 * value required.
 */
SYSCALL_DEFINE5(name_to_handle_at, int, dfd, const char __user *, name,
		struct file_handle __user *, handle, int __user *, mnt_id,
		int, flag)
{
	struct path path;
	int lookup_flags;
	int fh_flags;
	int err;

	if (flag & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH | AT_HANDLE_FID))
		return -EINVAL;

	lookup_flags = (flag & AT_SYMLINK_FOLLOW) ? LOOKUP_FOLLOW : 0;
	fh_flags = (flag & AT_HANDLE_FID) ? EXPORT_FH_FID : 0;
	if (flag & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;
	err = user_path_at(dfd, name, lookup_flags, &path);
	if (!err) {
		err = do_sys_name_to_handle(&path, handle, mnt_id, fh_flags);
		path_put(&path);
	}
	return err;
}

static struct vfsmount *get_vfsmount_from_fd(int fd)
{
	struct vfsmount *mnt;

	if (fd == AT_FDCWD) {
		struct fs_struct *fs = current->fs;
		spin_lock(&fs->lock);
		mnt = mntget(fs->pwd.mnt);
		spin_unlock(&fs->lock);
	} else {
		struct fd f = fdget(fd);
		if (!f.file)
			return ERR_PTR(-EBADF);
		mnt = mntget(f.file->f_path.mnt);
		fdput(f);
	}
	return mnt;
}

static int vfs_dentry_acceptable(void *context, struct dentry *dentry)
{
	return 1;
}

static int do_handle_to_path(int mountdirfd, struct file_handle *handle,
			     struct path *path)
{
	int retval = 0;
	int handle_dwords;

	path->mnt = get_vfsmount_from_fd(mountdirfd);
	if (IS_ERR(path->mnt)) {
		retval = PTR_ERR(path->mnt);
		goto out_err;
	}
	/* change the handle size to multiple of sizeof(u32) */
	handle_dwords = handle->handle_bytes >> 2;
	path->dentry = exportfs_decode_fh(path->mnt,
					  (struct fid *)handle->f_handle,
					  handle_dwords, handle->handle_type,
					  vfs_dentry_acceptable, NULL);
	if (IS_ERR(path->dentry)) {
		retval = PTR_ERR(path->dentry);
		goto out_mnt;
	}
	return 0;
out_mnt:
	mntput(path->mnt);
out_err:
	return retval;
}

static int handle_to_path(int mountdirfd, struct file_handle __user *ufh,
		   struct path *path)
{
	int retval = 0;
	struct file_handle f_handle;
	struct file_handle *handle = NULL;

	/*
	 * With handle we don't look at the execute bit on the
	 * directory. Ideally we would like CAP_DAC_SEARCH.
	 * But we don't have that
	 */
	if (!capable(CAP_DAC_READ_SEARCH)) {
		retval = -EPERM;
		goto out_err;
	}
	if (copy_from_user(&f_handle, ufh, sizeof(struct file_handle))) {
		retval = -EFAULT;
		goto out_err;
	}
	if ((f_handle.handle_bytes > MAX_HANDLE_SZ) ||
	    (f_handle.handle_bytes == 0)) {
		retval = -EINVAL;
		goto out_err;
	}
	handle = kmalloc(sizeof(struct file_handle) + f_handle.handle_bytes,
			 GFP_KERNEL);
	if (!handle) {
		retval = -ENOMEM;
		goto out_err;
	}
	/* copy the full handle */
	*handle = f_handle;
	if (copy_from_user(&handle->f_handle,
			   &ufh->f_handle,
			   f_handle.handle_bytes)) {
		retval = -EFAULT;
		goto out_handle;
	}

	retval = do_handle_to_path(mountdirfd, handle, path);

out_handle:
	kfree(handle);
out_err:
	return retval;
}

static long do_handle_open(int mountdirfd, struct file_handle __user *ufh,
			   int open_flag)
{
	long retval = 0;
	struct path path;
	struct file *file;
	int fd;

	retval = handle_to_path(mountdirfd, ufh, &path);
	if (retval)
		return retval;

	fd = get_unused_fd_flags(open_flag);
	if (fd < 0) {
		path_put(&path);
		return fd;
	}
	file = file_open_root(&path, "", open_flag, 0);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		retval =  PTR_ERR(file);
	} else {
		retval = fd;
		fd_install(fd, file);
	}
	path_put(&path);
	return retval;
}

/**
 * sys_open_by_handle_at: Open the file handle
 * @mountdirfd: directory file descriptor
 * @handle: file handle to be opened
 * @flags: open flags.
 *
 * @mountdirfd indicate the directory file descriptor
 * of the mount point. file handle is decoded relative
 * to the vfsmount pointed by the @mountdirfd. @flags
 * value is same as the open(2) flags.
 */
SYSCALL_DEFINE3(open_by_handle_at, int, mountdirfd,
		struct file_handle __user *, handle,
		int, flags)
{
	long ret;

	if (force_o_largefile())
		flags |= O_LARGEFILE;

	ret = do_handle_open(mountdirfd, handle, flags);
	return ret;
}

#ifdef CONFIG_COMPAT
/*
 * Exactly like fs/open.c:sys_open_by_handle_at(), except that it
 * doesn't set the O_LARGEFILE flag.
 */
COMPAT_SYSCALL_DEFINE3(open_by_handle_at, int, mountdirfd,
			     struct file_handle __user *, handle, int, flags)
{
	return do_handle_open(mountdirfd, handle, flags);
}
#endif
