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
				  void __user *mnt_id, bool unique_mntid,
				  int fh_flags)
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

	/*
	 * A request to encode a connectable handle for a disconnected dentry
	 * is unexpected since AT_EMPTY_PATH is not allowed.
	 */
	if (fh_flags & EXPORT_FH_CONNECTABLE &&
	    WARN_ON(path->dentry->d_flags & DCACHE_DISCONNECTED))
		return -EINVAL;

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

	/* Encode a possibly decodeable/connectable file handle */
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
	} else {
		/*
		 * When asked to encode a connectable file handle, encode this
		 * property in the file handle itself, so that we later know
		 * how to decode it.
		 * For sanity, also encode in the file handle if the encoded
		 * object is a directory and verify this during decode, because
		 * decoding directory file handles is quite different than
		 * decoding connectable non-directory file handles.
		 */
		if (fh_flags & EXPORT_FH_CONNECTABLE) {
			handle->handle_type |= FILEID_IS_CONNECTABLE;
			if (d_is_dir(path->dentry))
				handle->handle_type |= FILEID_IS_DIR;
		}
		retval = 0;
	}
	/* copy the mount id */
	if (unique_mntid) {
		if (put_user(real_mount(path->mnt)->mnt_id_unique,
			     (u64 __user *) mnt_id))
			retval = -EFAULT;
	} else {
		if (put_user(real_mount(path->mnt)->mnt_id,
			     (int __user *) mnt_id))
			retval = -EFAULT;
	}
	/* copy the handle */
	if (retval != -EFAULT &&
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
 *          (u64 if AT_HANDLE_MNT_ID_UNIQUE, otherwise int)
 * @flag: flag value to indicate whether to follow symlink or not
 *        and whether a decodable file handle is required.
 *
 * @handle->handle_size indicate the space available to store the
 * variable part of the file handle in bytes. If there is not
 * enough space, the field is updated to return the minimum
 * value required.
 */
SYSCALL_DEFINE5(name_to_handle_at, int, dfd, const char __user *, name,
		struct file_handle __user *, handle, void __user *, mnt_id,
		int, flag)
{
	struct path path;
	int lookup_flags;
	int fh_flags = 0;
	int err;

	if (flag & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH | AT_HANDLE_FID |
		     AT_HANDLE_MNT_ID_UNIQUE | AT_HANDLE_CONNECTABLE))
		return -EINVAL;

	/*
	 * AT_HANDLE_FID means there is no intention to decode file handle
	 * AT_HANDLE_CONNECTABLE means there is an intention to decode a
	 * connected fd (with known path), so these flags are conflicting.
	 * AT_EMPTY_PATH could be used along with a dfd that refers to a
	 * disconnected non-directory, which cannot be used to encode a
	 * connectable file handle, because its parent is unknown.
	 */
	if (flag & AT_HANDLE_CONNECTABLE &&
	    flag & (AT_HANDLE_FID | AT_EMPTY_PATH))
		return -EINVAL;
	else if (flag & AT_HANDLE_FID)
		fh_flags |= EXPORT_FH_FID;
	else if (flag & AT_HANDLE_CONNECTABLE)
		fh_flags |= EXPORT_FH_CONNECTABLE;

	lookup_flags = (flag & AT_SYMLINK_FOLLOW) ? LOOKUP_FOLLOW : 0;
	if (flag & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;
	err = user_path_at(dfd, name, lookup_flags, &path);
	if (!err) {
		err = do_sys_name_to_handle(&path, handle, mnt_id,
					    flag & AT_HANDLE_MNT_ID_UNIQUE,
					    fh_flags);
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
		CLASS(fd, f)(fd);
		if (fd_empty(f))
			return -EBADF;
		*root = fd_file(f)->f_path;
		path_get(root);
	}

	return 0;
}

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
	/*
	 * exportfs_decode_fh_raw() does not call acceptable() callback with
	 * a disconnected directory dentry, so we should have reached either
	 * mount fd directory or sb root.
	 */
	if (ctx->fh_flags & EXPORT_FH_DIR_ONLY)
		WARN_ON_ONCE(d != root && d != root->d_sb->s_root);
	dput(d);
	return retval;
}

static int do_handle_to_path(struct file_handle *handle, struct path *path,
			     struct handle_to_path_ctx *ctx)
{
	int handle_dwords;
	struct vfsmount *mnt = ctx->root.mnt;
	struct dentry *dentry;

	/* change the handle size to multiple of sizeof(u32) */
	handle_dwords = handle->handle_bytes >> 2;
	dentry = exportfs_decode_fh_raw(mnt, (struct fid *)handle->f_handle,
					handle_dwords, handle->handle_type,
					ctx->fh_flags, vfs_dentry_acceptable,
					ctx);
	if (IS_ERR_OR_NULL(dentry)) {
		if (dentry == ERR_PTR(-ENOMEM))
			return -ENOMEM;
		return -ESTALE;
	}
	path->dentry = dentry;
	path->mnt = mntget(mnt);
	return 0;
}

static inline int may_decode_fh(struct handle_to_path_ctx *ctx,
				unsigned int o_flags)
{
	struct path *root = &ctx->root;

	if (capable(CAP_DAC_READ_SEARCH))
		return 0;

	/*
	 * Allow relaxed permissions of file handles if the caller has
	 * the ability to mount the filesystem or create a bind-mount of
	 * the provided @mountdirfd.
	 *
	 * In both cases the caller may be able to get an unobstructed
	 * way to the encoded file handle. If the caller is only able to
	 * create a bind-mount we need to verify that there are no
	 * locked mounts on top of it that could prevent us from getting
	 * to the encoded file.
	 *
	 * In principle, locked mounts can prevent the caller from
	 * mounting the filesystem but that only applies to procfs and
	 * sysfs neither of which support decoding file handles.
	 *
	 * Restrict to O_DIRECTORY to provide a deterministic API that
	 * avoids a confusing api in the face of disconnected non-dir
	 * dentries.
	 *
	 * There's only one dentry for each directory inode (VFS rule)...
	 */
	if (!(o_flags & O_DIRECTORY))
		return -EPERM;

	if (ns_capable(root->mnt->mnt_sb->s_user_ns, CAP_SYS_ADMIN))
		ctx->flags = HANDLE_CHECK_PERMS;
	else if (is_mounted(root->mnt) &&
		 ns_capable(real_mount(root->mnt)->mnt_ns->user_ns,
			    CAP_SYS_ADMIN) &&
		 !has_locked_children(real_mount(root->mnt), root->dentry))
		ctx->flags = HANDLE_CHECK_PERMS | HANDLE_CHECK_SUBTREE;
	else
		return -EPERM;

	/* Are we able to override DAC permissions? */
	if (!ns_capable(current_user_ns(), CAP_DAC_READ_SEARCH))
		return -EPERM;

	ctx->fh_flags = EXPORT_FH_DIR_ONLY;
	return 0;
}

static int handle_to_path(int mountdirfd, struct file_handle __user *ufh,
		   struct path *path, unsigned int o_flags)
{
	int retval = 0;
	struct file_handle f_handle;
	struct file_handle *handle = NULL;
	struct handle_to_path_ctx ctx = {};
	const struct export_operations *eops;

	retval = get_path_from_fd(mountdirfd, &ctx.root);
	if (retval)
		goto out_err;

	eops = ctx.root.mnt->mnt_sb->s_export_op;
	if (eops && eops->permission)
		retval = eops->permission(&ctx, o_flags);
	else
		retval = may_decode_fh(&ctx, o_flags);
	if (retval)
		goto out_path;

	if (copy_from_user(&f_handle, ufh, sizeof(struct file_handle))) {
		retval = -EFAULT;
		goto out_path;
	}
	if ((f_handle.handle_bytes > MAX_HANDLE_SZ) ||
	    (f_handle.handle_bytes == 0)) {
		retval = -EINVAL;
		goto out_path;
	}
	if (f_handle.handle_type < 0 ||
	    FILEID_USER_FLAGS(f_handle.handle_type) & ~FILEID_VALID_USER_FLAGS) {
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

	/*
	 * If handle was encoded with AT_HANDLE_CONNECTABLE, verify that we
	 * are decoding an fd with connected path, which is accessible from
	 * the mount fd path.
	 */
	if (f_handle.handle_type & FILEID_IS_CONNECTABLE) {
		ctx.fh_flags |= EXPORT_FH_CONNECTABLE;
		ctx.flags |= HANDLE_CHECK_SUBTREE;
	}
	if (f_handle.handle_type & FILEID_IS_DIR)
		ctx.fh_flags |= EXPORT_FH_DIR_ONLY;
	/* Filesystem code should not be exposed to user flags */
	handle->handle_type &= ~FILEID_USER_FLAGS_MASK;
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
	struct path path __free(path_put) = {};
	struct file *file;
	const struct export_operations *eops;

	retval = handle_to_path(mountdirfd, ufh, &path, open_flag);
	if (retval)
		return retval;

	CLASS(get_unused_fd, fd)(O_CLOEXEC);
	if (fd < 0)
		return fd;

	eops = path.mnt->mnt_sb->s_export_op;
	if (eops->open)
		file = eops->open(&path, open_flag);
	else
		file = file_open_root(&path, "", open_flag, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	fd_install(fd, file);
	return take_fd(fd);
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
