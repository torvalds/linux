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
	 */
	if (!exportfs_can_encode_fh(path->dentry->d_sb->s_export_op, fh_flags))
		return -EOPNOTSUPP;

	if (copy_from_user(&f_handle, ufh, sizeof(struct file_handle)))
		return -EFAULT;

	if (f_handle.handle_bytes > MAX_HANDLE_SZ)
		return -EINVAL;

	handle = kzalloc(struct_size(handle, f_handle, f_handle.handle_bytes),
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
			 struct_size(handle, f_handle, handle_bytes)))
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

static int get_path_from_fd(int fd, struct path *root)
{
	if (fd == AT_FDCWD) {
		struct fs_struct *fs = current->fs;
		spin_lock(&fs->lock);
		*root = fs->pwd;
		path_get(root);
		spin_unlock(&fs->lock);
	} else {
		struct fd f = fdget(fd);
		if (!f.file)
			return -EBADF;
		*root = f.file->f_path;
		path_get(root);
		fdput(f);
	}

	return 0;
}

enum handle_to_path_flags {
	HANDLE_CHECK_PERMS   = (1 << 0),
	HANDLE_CHECK_SUBTREE = (1 << 1),
};

struct handle_to_path_ctx {
	struct path root;
	enum handle_to_path_flags flags;
	unsigned int fh_flags;
};

static int vfs_dentry_acceptable(void *context, struct dentry *dentry)
{
	struct handle_to_path_ctx *ctx = context;
	struct user_namespace *user_ns = current_user_ns();
	struct dentry *d, *root = ctx->root.dentry;
	struct mnt_idmap *idmap = mnt_idmap(ctx->root.mnt);
	int retval = 0;

	if (!root)
		return 1;

	/* Old permission model with global CAP_DAC_READ_SEARCH. */
	if (!ctx->flags)
		return 1;

	/*
	 * It's racy as we're not taking rename_lock but we're able to ignore
	 * permissions and we just need an approximation whether we were able
	 * to follow a path to the file.
	 *
	 * It's also potentially expensive on some filesystems especially if
	 * there is a deep path.
	 */
	d = dget(dentry);
	while (d != root && !IS_ROOT(d)) {
		struct dentry *parent = dget_parent(d);

		/*
		 * We know that we have the ability to override DAC permissions
		 * as we've verified this earlier via CAP_DAC_READ_SEARCH. But
		 * we also need to make sure that there aren't any unmapped
		 * inodes in the path that would prevent us from reaching the
		 * file.
		 */
		if (!privileged_wrt_inode_uidgid(user_ns, idmap,
						 d_inode(parent))) {
			dput(d);
			dput(parent);
			return retval;
		}

		dput(d);
		d = parent;
	}

	if (!(ctx->flags & HANDLE_CHECK_SUBTREE) || d == root)
		retval = 1;
	WARN_ON_ONCE(d != root && d != root->d_sb->s_root);
	dput(d);
	return retval;
}

static int do_handle_to_path(struct file_handle *handle, struct path *path,
			     struct handle_to_path_ctx *ctx)
{
	int handle_dwords;
	struct vfsmount *mnt = ctx->root.mnt;

	/* change the handle size to multiple of sizeof(u32) */
	handle_dwords = handle->handle_bytes >> 2;
	path->dentry = exportfs_decode_fh_raw(mnt,
					  (struct fid *)handle->f_handle,
					  handle_dwords, handle->handle_type,
					  ctx->fh_flags,
					  vfs_dentry_acceptable, ctx);
	if (IS_ERR_OR_NULL(path->dentry)) {
		if (path->dentry == ERR_PTR(-ENOMEM))
			return -ENOMEM;
		return -ESTALE;
	}
	path->mnt = mntget(mnt);
	return 0;
}

/*
 * Allow relaxed permissions of file handles if the caller has the
 * ability to mount the filesystem or create a bind-mount of the
 * provided @mountdirfd.
 *
 * In both cases the caller may be able to get an unobstructed way to
 * the encoded file handle. If the caller is only able to create a
 * bind-mount we need to verify that there are no locked mounts on top
 * of it that could prevent us from getting to the encoded file.
 *
 * In principle, locked mounts can prevent the caller from mounting the
 * filesystem but that only applies to procfs and sysfs neither of which
 * support decoding file handles.
 */
static inline bool may_decode_fh(struct handle_to_path_ctx *ctx,
				 unsigned int o_flags)
{
	struct path *root = &ctx->root;

	/*
	 * Restrict to O_DIRECTORY to provide a deterministic API that avoids a
	 * confusing api in the face of disconnected non-dir dentries.
	 *
	 * There's only one dentry for each directory inode (VFS rule)...
	 */
	if (!(o_flags & O_DIRECTORY))
		return false;

	if (ns_capable(root->mnt->mnt_sb->s_user_ns, CAP_SYS_ADMIN))
		ctx->flags = HANDLE_CHECK_PERMS;
	else if (is_mounted(root->mnt) &&
		 ns_capable(real_mount(root->mnt)->mnt_ns->user_ns,
			    CAP_SYS_ADMIN) &&
		 !has_locked_children(real_mount(root->mnt), root->dentry))
		ctx->flags = HANDLE_CHECK_PERMS | HANDLE_CHECK_SUBTREE;
	else
		return false;

	/* Are we able to override DAC permissions? */
	if (!ns_capable(current_user_ns(), CAP_DAC_READ_SEARCH))
		return false;

	ctx->fh_flags = EXPORT_FH_DIR_ONLY;
	return true;
}

static int handle_to_path(int mountdirfd, struct file_handle __user *ufh,
		   struct path *path, unsigned int o_flags)
{
	int retval = 0;
	struct file_handle f_handle;
	struct file_handle *handle = NULL;
	struct handle_to_path_ctx ctx = {};

	retval = get_path_from_fd(mountdirfd, &ctx.root);
	if (retval)
		goto out_err;

	if (!capable(CAP_DAC_READ_SEARCH) && !may_decode_fh(&ctx, o_flags)) {
		retval = -EPERM;
		goto out_path;
	}

	if (copy_from_user(&f_handle, ufh, sizeof(struct file_handle))) {
		retval = -EFAULT;
		goto out_path;
	}
	if ((f_handle.handle_bytes > MAX_HANDLE_SZ) ||
	    (f_handle.handle_bytes == 0)) {
		retval = -EINVAL;
		goto out_path;
	}
	handle = kmalloc(struct_size(handle, f_handle, f_handle.handle_bytes),
			 GFP_KERNEL);
	if (!handle) {
		retval = -ENOMEM;
		goto out_path;
	}
	/* copy the full handle */
	*handle = f_handle;
	if (copy_from_user(&handle->f_handle,
			   &ufh->f_handle,
			   f_handle.handle_bytes)) {
		retval = -EFAULT;
		goto out_handle;
	}

	retval = do_handle_to_path(handle, path, &ctx);

out_handle:
	kfree(handle);
out_path:
	path_put(&ctx.root);
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

	retval = handle_to_path(mountdirfd, ufh, &path, open_flag);
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
