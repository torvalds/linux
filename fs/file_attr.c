// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/fscrypt.h>
#include <linux/fileattr.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/namei.h>

#include "internal.h"

/**
 * fileattr_fill_xflags - initialize fileattr with xflags
 * @fa:		fileattr pointer
 * @xflags:	FS_XFLAG_* flags
 *
 * Set ->fsx_xflags, ->fsx_valid and ->flags (translated xflags).  All
 * other fields are zeroed.
 */
void fileattr_fill_xflags(struct file_kattr *fa, u32 xflags)
{
	memset(fa, 0, sizeof(*fa));
	fa->fsx_valid = true;
	fa->fsx_xflags = xflags;
	if (fa->fsx_xflags & FS_XFLAG_IMMUTABLE)
		fa->flags |= FS_IMMUTABLE_FL;
	if (fa->fsx_xflags & FS_XFLAG_APPEND)
		fa->flags |= FS_APPEND_FL;
	if (fa->fsx_xflags & FS_XFLAG_SYNC)
		fa->flags |= FS_SYNC_FL;
	if (fa->fsx_xflags & FS_XFLAG_NOATIME)
		fa->flags |= FS_NOATIME_FL;
	if (fa->fsx_xflags & FS_XFLAG_NODUMP)
		fa->flags |= FS_NODUMP_FL;
	if (fa->fsx_xflags & FS_XFLAG_DAX)
		fa->flags |= FS_DAX_FL;
	if (fa->fsx_xflags & FS_XFLAG_PROJINHERIT)
		fa->flags |= FS_PROJINHERIT_FL;
}
EXPORT_SYMBOL(fileattr_fill_xflags);

/**
 * fileattr_fill_flags - initialize fileattr with flags
 * @fa:		fileattr pointer
 * @flags:	FS_*_FL flags
 *
 * Set ->flags, ->flags_valid and ->fsx_xflags (translated flags).
 * All other fields are zeroed.
 */
void fileattr_fill_flags(struct file_kattr *fa, u32 flags)
{
	memset(fa, 0, sizeof(*fa));
	fa->flags_valid = true;
	fa->flags = flags;
	if (fa->flags & FS_SYNC_FL)
		fa->fsx_xflags |= FS_XFLAG_SYNC;
	if (fa->flags & FS_IMMUTABLE_FL)
		fa->fsx_xflags |= FS_XFLAG_IMMUTABLE;
	if (fa->flags & FS_APPEND_FL)
		fa->fsx_xflags |= FS_XFLAG_APPEND;
	if (fa->flags & FS_NODUMP_FL)
		fa->fsx_xflags |= FS_XFLAG_NODUMP;
	if (fa->flags & FS_NOATIME_FL)
		fa->fsx_xflags |= FS_XFLAG_NOATIME;
	if (fa->flags & FS_DAX_FL)
		fa->fsx_xflags |= FS_XFLAG_DAX;
	if (fa->flags & FS_PROJINHERIT_FL)
		fa->fsx_xflags |= FS_XFLAG_PROJINHERIT;
}
EXPORT_SYMBOL(fileattr_fill_flags);

/**
 * vfs_fileattr_get - retrieve miscellaneous file attributes
 * @dentry:	the object to retrieve from
 * @fa:		fileattr pointer
 *
 * Call i_op->fileattr_get() callback, if exists.
 *
 * Return: 0 on success, or a negative error on failure.
 */
int vfs_fileattr_get(struct dentry *dentry, struct file_kattr *fa)
{
	struct inode *inode = d_inode(dentry);
	int error;

	if (!inode->i_op->fileattr_get)
		return -ENOIOCTLCMD;

	error = security_inode_file_getattr(dentry, fa);
	if (error)
		return error;

	return inode->i_op->fileattr_get(dentry, fa);
}
EXPORT_SYMBOL(vfs_fileattr_get);

static void fileattr_to_file_attr(const struct file_kattr *fa,
				  struct file_attr *fattr)
{
	__u32 mask = FS_XFLAGS_MASK;

	memset(fattr, 0, sizeof(struct file_attr));
	fattr->fa_xflags = fa->fsx_xflags & mask;
	fattr->fa_extsize = fa->fsx_extsize;
	fattr->fa_nextents = fa->fsx_nextents;
	fattr->fa_projid = fa->fsx_projid;
	fattr->fa_cowextsize = fa->fsx_cowextsize;
}

/**
 * copy_fsxattr_to_user - copy fsxattr to userspace.
 * @fa:		fileattr pointer
 * @ufa:	fsxattr user pointer
 *
 * Return: 0 on success, or -EFAULT on failure.
 */
int copy_fsxattr_to_user(const struct file_kattr *fa, struct fsxattr __user *ufa)
{
	struct fsxattr xfa;
	__u32 mask = FS_XFLAGS_MASK;

	memset(&xfa, 0, sizeof(xfa));
	xfa.fsx_xflags = fa->fsx_xflags & mask;
	xfa.fsx_extsize = fa->fsx_extsize;
	xfa.fsx_nextents = fa->fsx_nextents;
	xfa.fsx_projid = fa->fsx_projid;
	xfa.fsx_cowextsize = fa->fsx_cowextsize;

	if (copy_to_user(ufa, &xfa, sizeof(xfa)))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL(copy_fsxattr_to_user);

static int file_attr_to_fileattr(const struct file_attr *fattr,
				 struct file_kattr *fa)
{
	__u64 mask = FS_XFLAGS_MASK;

	if (fattr->fa_xflags & ~mask)
		return -EINVAL;

	fileattr_fill_xflags(fa, fattr->fa_xflags);
	fa->fsx_xflags &= ~FS_XFLAG_RDONLY_MASK;
	fa->fsx_extsize = fattr->fa_extsize;
	fa->fsx_projid = fattr->fa_projid;
	fa->fsx_cowextsize = fattr->fa_cowextsize;

	return 0;
}

static int copy_fsxattr_from_user(struct file_kattr *fa,
				  struct fsxattr __user *ufa)
{
	struct fsxattr xfa;
	__u32 mask = FS_XFLAGS_MASK;

	if (copy_from_user(&xfa, ufa, sizeof(xfa)))
		return -EFAULT;

	if (xfa.fsx_xflags & ~mask)
		return -EOPNOTSUPP;

	fileattr_fill_xflags(fa, xfa.fsx_xflags);
	fa->fsx_xflags &= ~FS_XFLAG_RDONLY_MASK;
	fa->fsx_extsize = xfa.fsx_extsize;
	fa->fsx_nextents = xfa.fsx_nextents;
	fa->fsx_projid = xfa.fsx_projid;
	fa->fsx_cowextsize = xfa.fsx_cowextsize;

	return 0;
}

/*
 * Generic function to check FS_IOC_FSSETXATTR/FS_IOC_SETFLAGS values and reject
 * any invalid configurations.
 *
 * Note: must be called with inode lock held.
 */
static int fileattr_set_prepare(struct inode *inode,
			      const struct file_kattr *old_ma,
			      struct file_kattr *fa)
{
	int err;

	/*
	 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
	 * the relevant capability.
	 */
	if ((fa->flags ^ old_ma->flags) & (FS_APPEND_FL | FS_IMMUTABLE_FL) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return -EPERM;

	err = fscrypt_prepare_setflags(inode, old_ma->flags, fa->flags);
	if (err)
		return err;

	/*
	 * Project Quota ID state is only allowed to change from within the init
	 * namespace. Enforce that restriction only if we are trying to change
	 * the quota ID state. Everything else is allowed in user namespaces.
	 */
	if (current_user_ns() != &init_user_ns) {
		if (old_ma->fsx_projid != fa->fsx_projid)
			return -EINVAL;
		if ((old_ma->fsx_xflags ^ fa->fsx_xflags) &
				FS_XFLAG_PROJINHERIT)
			return -EINVAL;
	} else {
		/*
		 * Caller is allowed to change the project ID. If it is being
		 * changed, make sure that the new value is valid.
		 */
		if (old_ma->fsx_projid != fa->fsx_projid &&
		    !projid_valid(make_kprojid(&init_user_ns, fa->fsx_projid)))
			return -EINVAL;
	}

	/* Check extent size hints. */
	if ((fa->fsx_xflags & FS_XFLAG_EXTSIZE) && !S_ISREG(inode->i_mode))
		return -EINVAL;

	if ((fa->fsx_xflags & FS_XFLAG_EXTSZINHERIT) &&
			!S_ISDIR(inode->i_mode))
		return -EINVAL;

	if ((fa->fsx_xflags & FS_XFLAG_COWEXTSIZE) &&
	    !S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
		return -EINVAL;

	/*
	 * It is only valid to set the DAX flag on regular files and
	 * directories on filesystems.
	 */
	if ((fa->fsx_xflags & FS_XFLAG_DAX) &&
	    !(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		return -EINVAL;

	/* Extent size hints of zero turn off the flags. */
	if (fa->fsx_extsize == 0)
		fa->fsx_xflags &= ~(FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT);
	if (fa->fsx_cowextsize == 0)
		fa->fsx_xflags &= ~FS_XFLAG_COWEXTSIZE;

	return 0;
}

/**
 * vfs_fileattr_set - change miscellaneous file attributes
 * @idmap:	idmap of the mount
 * @dentry:	the object to change
 * @fa:		fileattr pointer
 *
 * After verifying permissions, call i_op->fileattr_set() callback, if
 * exists.
 *
 * Verifying attributes involves retrieving current attributes with
 * i_op->fileattr_get(), this also allows initializing attributes that have
 * not been set by the caller to current values.  Inode lock is held
 * thoughout to prevent racing with another instance.
 *
 * Return: 0 on success, or a negative error on failure.
 */
int vfs_fileattr_set(struct mnt_idmap *idmap, struct dentry *dentry,
		     struct file_kattr *fa)
{
	struct inode *inode = d_inode(dentry);
	struct file_kattr old_ma = {};
	int err;

	if (!inode->i_op->fileattr_set)
		return -ENOIOCTLCMD;

	if (!inode_owner_or_capable(idmap, inode))
		return -EPERM;

	inode_lock(inode);
	err = vfs_fileattr_get(dentry, &old_ma);
	if (!err) {
		/* initialize missing bits from old_ma */
		if (fa->flags_valid) {
			fa->fsx_xflags |= old_ma.fsx_xflags & ~FS_XFLAG_COMMON;
			fa->fsx_extsize = old_ma.fsx_extsize;
			fa->fsx_nextents = old_ma.fsx_nextents;
			fa->fsx_projid = old_ma.fsx_projid;
			fa->fsx_cowextsize = old_ma.fsx_cowextsize;
		} else {
			fa->flags |= old_ma.flags & ~FS_COMMON_FL;
		}

		err = fileattr_set_prepare(inode, &old_ma, fa);
		if (err)
			goto out;
		err = security_inode_file_setattr(dentry, fa);
		if (err)
			goto out;
		err = inode->i_op->fileattr_set(idmap, dentry, fa);
		if (err)
			goto out;
	}

out:
	inode_unlock(inode);
	return err;
}
EXPORT_SYMBOL(vfs_fileattr_set);

int ioctl_getflags(struct file *file, unsigned int __user *argp)
{
	struct file_kattr fa = { .flags_valid = true }; /* hint only */
	int err;

	err = vfs_fileattr_get(file->f_path.dentry, &fa);
	if (!err)
		err = put_user(fa.flags, argp);
	return err;
}
EXPORT_SYMBOL(ioctl_getflags);

int ioctl_setflags(struct file *file, unsigned int __user *argp)
{
	struct mnt_idmap *idmap = file_mnt_idmap(file);
	struct dentry *dentry = file->f_path.dentry;
	struct file_kattr fa;
	unsigned int flags;
	int err;

	err = get_user(flags, argp);
	if (!err) {
		err = mnt_want_write_file(file);
		if (!err) {
			fileattr_fill_flags(&fa, flags);
			err = vfs_fileattr_set(idmap, dentry, &fa);
			mnt_drop_write_file(file);
		}
	}
	return err;
}
EXPORT_SYMBOL(ioctl_setflags);

int ioctl_fsgetxattr(struct file *file, void __user *argp)
{
	struct file_kattr fa = { .fsx_valid = true }; /* hint only */
	int err;

	err = vfs_fileattr_get(file->f_path.dentry, &fa);
	if (!err)
		err = copy_fsxattr_to_user(&fa, argp);

	return err;
}
EXPORT_SYMBOL(ioctl_fsgetxattr);

int ioctl_fssetxattr(struct file *file, void __user *argp)
{
	struct mnt_idmap *idmap = file_mnt_idmap(file);
	struct dentry *dentry = file->f_path.dentry;
	struct file_kattr fa;
	int err;

	err = copy_fsxattr_from_user(&fa, argp);
	if (!err) {
		err = mnt_want_write_file(file);
		if (!err) {
			err = vfs_fileattr_set(idmap, dentry, &fa);
			mnt_drop_write_file(file);
		}
	}
	return err;
}
EXPORT_SYMBOL(ioctl_fssetxattr);

SYSCALL_DEFINE5(file_getattr, int, dfd, const char __user *, filename,
		struct file_attr __user *, ufattr, size_t, usize,
		unsigned int, at_flags)
{
	struct path filepath __free(path_put) = {};
	struct filename *name __free(putname) = NULL;
	unsigned int lookup_flags = 0;
	struct file_attr fattr;
	struct file_kattr fa;
	int error;

	BUILD_BUG_ON(sizeof(struct file_attr) < FILE_ATTR_SIZE_VER0);
	BUILD_BUG_ON(sizeof(struct file_attr) != FILE_ATTR_SIZE_LATEST);

	if ((at_flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;

	if (!(at_flags & AT_SYMLINK_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;

	if (usize > PAGE_SIZE)
		return -E2BIG;

	if (usize < FILE_ATTR_SIZE_VER0)
		return -EINVAL;

	name = getname_maybe_null(filename, at_flags);
	if (IS_ERR(name))
		return PTR_ERR(name);

	if (!name && dfd >= 0) {
		CLASS(fd, f)(dfd);
		if (fd_empty(f))
			return -EBADF;

		filepath = fd_file(f)->f_path;
		path_get(&filepath);
	} else {
		error = filename_lookup(dfd, name, lookup_flags, &filepath,
					NULL);
		if (error)
			return error;
	}

	error = vfs_fileattr_get(filepath.dentry, &fa);
	if (error == -ENOIOCTLCMD || error == -ENOTTY)
		error = -EOPNOTSUPP;
	if (error)
		return error;

	fileattr_to_file_attr(&fa, &fattr);
	error = copy_struct_to_user(ufattr, usize, &fattr,
				    sizeof(struct file_attr), NULL);

	return error;
}

SYSCALL_DEFINE5(file_setattr, int, dfd, const char __user *, filename,
		struct file_attr __user *, ufattr, size_t, usize,
		unsigned int, at_flags)
{
	struct path filepath __free(path_put) = {};
	struct filename *name __free(putname) = NULL;
	unsigned int lookup_flags = 0;
	struct file_attr fattr;
	struct file_kattr fa;
	int error;

	BUILD_BUG_ON(sizeof(struct file_attr) < FILE_ATTR_SIZE_VER0);
	BUILD_BUG_ON(sizeof(struct file_attr) != FILE_ATTR_SIZE_LATEST);

	if ((at_flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;

	if (!(at_flags & AT_SYMLINK_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;

	if (usize > PAGE_SIZE)
		return -E2BIG;

	if (usize < FILE_ATTR_SIZE_VER0)
		return -EINVAL;

	error = copy_struct_from_user(&fattr, sizeof(struct file_attr), ufattr,
				      usize);
	if (error)
		return error;

	error = file_attr_to_fileattr(&fattr, &fa);
	if (error)
		return error;

	name = getname_maybe_null(filename, at_flags);
	if (IS_ERR(name))
		return PTR_ERR(name);

	if (!name && dfd >= 0) {
		CLASS(fd, f)(dfd);
		if (fd_empty(f))
			return -EBADF;

		filepath = fd_file(f)->f_path;
		path_get(&filepath);
	} else {
		error = filename_lookup(dfd, name, lookup_flags, &filepath,
					NULL);
		if (error)
			return error;
	}

	error = mnt_want_write(filepath.mnt);
	if (!error) {
		error = vfs_fileattr_set(mnt_idmap(filepath.mnt),
					 filepath.dentry, &fa);
		if (error == -ENOIOCTLCMD || error == -ENOTTY)
			error = -EOPNOTSUPP;
		mnt_drop_write(filepath.mnt);
	}

	return error;
}
