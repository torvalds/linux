// SPDX-License-Identifier: GPL-2.0-only
/*
  File: fs/xattr.c

  Extended attribute handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (C) 2001 SGI - Silicon Graphics, Inc <linux-xfs@oss.sgi.com>
  Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/fsnotify.h>
#include <linux/audit.h>
#include <linux/vmalloc.h>
#include <linux/posix_acl_xattr.h>
#include <linux/rhashtable.h>

#include <linux/uaccess.h>

#include "internal.h"

static const char *
strcmp_prefix(const char *a, const char *a_prefix)
{
	while (*a_prefix && *a == *a_prefix) {
		a++;
		a_prefix++;
	}
	return *a_prefix ? NULL : a;
}

/*
 * In order to implement different sets of xattr operations for each xattr
 * prefix, a filesystem should create a null-terminated array of struct
 * xattr_handler (one for each prefix) and hang a pointer to it off of the
 * s_xattr field of the superblock.
 */
#define for_each_xattr_handler(handlers, handler)		\
	if (handlers)						\
		for ((handler) = *(handlers)++;			\
			(handler) != NULL;			\
			(handler) = *(handlers)++)

/*
 * Find the xattr_handler with the matching prefix.
 */
static const struct xattr_handler *
xattr_resolve_name(struct inode *inode, const char **name)
{
	const struct xattr_handler * const *handlers = inode->i_sb->s_xattr;
	const struct xattr_handler *handler;

	if (!(inode->i_opflags & IOP_XATTR)) {
		if (unlikely(is_bad_inode(inode)))
			return ERR_PTR(-EIO);
		return ERR_PTR(-EOPNOTSUPP);
	}
	for_each_xattr_handler(handlers, handler) {
		const char *n;

		n = strcmp_prefix(*name, xattr_prefix(handler));
		if (n) {
			if (!handler->prefix ^ !*n) {
				if (*n)
					continue;
				return ERR_PTR(-EINVAL);
			}
			*name = n;
			return handler;
		}
	}
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * may_write_xattr - check whether inode allows writing xattr
 * @idmap: idmap of the mount the inode was found from
 * @inode: the inode on which to set an xattr
 *
 * Check whether the inode allows writing xattrs. Specifically, we can never
 * set or remove an extended attribute on a read-only filesystem  or on an
 * immutable / append-only inode.
 *
 * We also need to ensure that the inode has a mapping in the mount to
 * not risk writing back invalid i_{g,u}id values.
 *
 * Return: On success zero is returned. On error a negative errno is returned.
 */
int may_write_xattr(struct mnt_idmap *idmap, struct inode *inode)
{
	if (IS_IMMUTABLE(inode))
		return -EPERM;
	if (IS_APPEND(inode))
		return -EPERM;
	if (HAS_UNMAPPED_ID(idmap, inode))
		return -EPERM;
	return 0;
}

static inline int xattr_permission_error(int mask)
{
	if (mask & MAY_WRITE)
		return -EPERM;
	return -ENODATA;
}

/*
 * Check permissions for extended attribute access.  This is a bit complicated
 * because different namespaces have very different rules.
 */
static int
xattr_permission(struct mnt_idmap *idmap, struct inode *inode,
		 const char *name, int mask)
{
	if (mask & MAY_WRITE) {
		int ret;

		ret = may_write_xattr(idmap, inode);
		if (ret)
			return ret;
	}

	/*
	 * No restriction for security.* and system.* from the VFS.  Decision
	 * on these is left to the underlying filesystem / security module.
	 */
	if (!strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN) ||
	    !strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return 0;

	/*
	 * The trusted.* namespace can only be accessed by privileged users.
	 */
	if (!strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN)) {
		if (!capable(CAP_SYS_ADMIN))
			return xattr_permission_error(mask);
		return 0;
	}

	/*
	 * In the user.* namespace, only regular files and directories can have
	 * extended attributes. For sticky directories, only the owner and
	 * privileged users can write attributes.
	 */
	if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN)) {
		switch (inode->i_mode & S_IFMT) {
		case S_IFREG:
			break;
		case S_IFDIR:
			if (!(inode->i_mode & S_ISVTX))
				break;
			if (!(mask & MAY_WRITE))
				break;
			if (inode_owner_or_capable(idmap, inode))
				break;
			return -EPERM;
		case S_IFSOCK:
			break;
		default:
			return xattr_permission_error(mask);
		}
	}

	return inode_permission(idmap, inode, mask);
}

/*
 * Look for any handler that deals with the specified namespace.
 */
int
xattr_supports_user_prefix(struct inode *inode)
{
	const struct xattr_handler * const *handlers = inode->i_sb->s_xattr;
	const struct xattr_handler *handler;

	if (!(inode->i_opflags & IOP_XATTR)) {
		if (unlikely(is_bad_inode(inode)))
			return -EIO;
		return -EOPNOTSUPP;
	}

	for_each_xattr_handler(handlers, handler) {
		if (!strncmp(xattr_prefix(handler), XATTR_USER_PREFIX,
			     XATTR_USER_PREFIX_LEN))
			return 0;
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(xattr_supports_user_prefix);

int
__vfs_setxattr(struct mnt_idmap *idmap, struct dentry *dentry,
	       struct inode *inode, const char *name, const void *value,
	       size_t size, int flags)
{
	const struct xattr_handler *handler;

	if (is_posix_acl_xattr(name))
		return -EOPNOTSUPP;

	handler = xattr_resolve_name(inode, &name);
	if (IS_ERR(handler))
		return PTR_ERR(handler);
	if (!handler->set)
		return -EOPNOTSUPP;
	if (size == 0)
		value = "";  /* empty EA, do not remove */
	return handler->set(handler, idmap, dentry, inode, name, value,
			    size, flags);
}
EXPORT_SYMBOL(__vfs_setxattr);

/**
 *  __vfs_setxattr_noperm - perform setxattr operation without performing
 *  permission checks.
 *
 *  @idmap: idmap of the mount the inode was found from
 *  @dentry: object to perform setxattr on
 *  @name: xattr name to set
 *  @value: value to set @name to
 *  @size: size of @value
 *  @flags: flags to pass into filesystem operations
 *
 *  returns the result of the internal setxattr or setsecurity operations.
 *
 *  This function requires the caller to lock the inode's i_rwsem before it
 *  is executed. It also assumes that the caller will make the appropriate
 *  permission checks.
 */
int __vfs_setxattr_noperm(struct mnt_idmap *idmap,
			  struct dentry *dentry, const char *name,
			  const void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	int error = -EAGAIN;
	int issec = !strncmp(name, XATTR_SECURITY_PREFIX,
				   XATTR_SECURITY_PREFIX_LEN);

	if (issec)
		inode->i_flags &= ~S_NOSEC;
	if (inode->i_opflags & IOP_XATTR) {
		error = __vfs_setxattr(idmap, dentry, inode, name, value,
				       size, flags);
		if (!error) {
			fsnotify_xattr(dentry);
			security_inode_post_setxattr(dentry, name, value,
						     size, flags);
		}
	} else {
		if (unlikely(is_bad_inode(inode)))
			return -EIO;
	}
	if (error == -EAGAIN) {
		error = -EOPNOTSUPP;

		if (issec) {
			const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;

			error = security_inode_setsecurity(inode, suffix, value,
							   size, flags);
			if (!error)
				fsnotify_xattr(dentry);
		}
	}

	return error;
}

/**
 * __vfs_setxattr_locked - set an extended attribute while holding the inode
 * lock
 *
 *  @idmap: idmap of the mount of the target inode
 *  @dentry: object to perform setxattr on
 *  @name: xattr name to set
 *  @value: value to set @name to
 *  @size: size of @value
 *  @flags: flags to pass into filesystem operations
 *  @delegated_inode: on return, will contain an inode pointer that
 *  a delegation was broken on, NULL if none.
 */
int
__vfs_setxattr_locked(struct mnt_idmap *idmap, struct dentry *dentry,
		      const char *name, const void *value, size_t size,
		      int flags, struct delegated_inode *delegated_inode)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = xattr_permission(idmap, inode, name, MAY_WRITE);
	if (error)
		return error;

	error = security_inode_setxattr(idmap, dentry, name, value, size,
					flags);
	if (error)
		goto out;

	error = try_break_deleg(inode, delegated_inode);
	if (error)
		goto out;

	error = __vfs_setxattr_noperm(idmap, dentry, name, value,
				      size, flags);

out:
	return error;
}
EXPORT_SYMBOL_GPL(__vfs_setxattr_locked);

int
vfs_setxattr(struct mnt_idmap *idmap, struct dentry *dentry,
	     const char *name, const void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct delegated_inode delegated_inode = { };
	const void  *orig_value = value;
	int error;

	if (size && strcmp(name, XATTR_NAME_CAPS) == 0) {
		error = cap_convert_nscap(idmap, dentry, &value, size);
		if (error < 0)
			return error;
		size = error;
	}

retry_deleg:
	inode_lock(inode);
	error = __vfs_setxattr_locked(idmap, dentry, name, value, size,
				      flags, &delegated_inode);
	inode_unlock(inode);

	if (is_delegated(&delegated_inode)) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
	if (value != orig_value)
		kfree(value);

	return error;
}
EXPORT_SYMBOL_GPL(vfs_setxattr);

static ssize_t
xattr_getsecurity(struct mnt_idmap *idmap, struct inode *inode,
		  const char *name, void *value, size_t size)
{
	void *buffer = NULL;
	ssize_t len;

	if (!value || !size) {
		len = security_inode_getsecurity(idmap, inode, name,
						 &buffer, false);
		goto out_noalloc;
	}

	len = security_inode_getsecurity(idmap, inode, name, &buffer,
					 true);
	if (len < 0)
		return len;
	if (size < len) {
		len = -ERANGE;
		goto out;
	}
	memcpy(value, buffer, len);
out:
	kfree(buffer);
out_noalloc:
	return len;
}

/*
 * vfs_getxattr_alloc - allocate memory, if necessary, before calling getxattr
 *
 * Allocate memory, if not already allocated, or re-allocate correct size,
 * before retrieving the extended attribute.  The xattr value buffer should
 * always be freed by the caller, even on error.
 *
 * Returns the result of alloc, if failed, or the getxattr operation.
 */
int
vfs_getxattr_alloc(struct mnt_idmap *idmap, struct dentry *dentry,
		   const char *name, char **xattr_value, size_t xattr_size,
		   gfp_t flags)
{
	const struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;
	char *value = *xattr_value;
	int error;

	error = xattr_permission(idmap, inode, name, MAY_READ);
	if (error)
		return error;

	handler = xattr_resolve_name(inode, &name);
	if (IS_ERR(handler))
		return PTR_ERR(handler);
	if (!handler->get)
		return -EOPNOTSUPP;
	error = handler->get(handler, dentry, inode, name, NULL, 0);
	if (error < 0)
		return error;

	if (!value || (error > xattr_size)) {
		value = krealloc(*xattr_value, error + 1, flags);
		if (!value)
			return -ENOMEM;
		memset(value, 0, error + 1);
	}

	error = handler->get(handler, dentry, inode, name, value, error);
	*xattr_value = value;
	return error;
}

ssize_t
__vfs_getxattr(struct dentry *dentry, struct inode *inode, const char *name,
	       void *value, size_t size)
{
	const struct xattr_handler *handler;

	if (is_posix_acl_xattr(name))
		return -EOPNOTSUPP;

	handler = xattr_resolve_name(inode, &name);
	if (IS_ERR(handler))
		return PTR_ERR(handler);
	if (!handler->get)
		return -EOPNOTSUPP;
	return handler->get(handler, dentry, inode, name, value, size);
}
EXPORT_SYMBOL(__vfs_getxattr);

ssize_t
vfs_getxattr(struct mnt_idmap *idmap, struct dentry *dentry,
	     const char *name, void *value, size_t size)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = xattr_permission(idmap, inode, name, MAY_READ);
	if (error)
		return error;

	error = security_inode_getxattr(dentry, name);
	if (error)
		return error;

	if (!strncmp(name, XATTR_SECURITY_PREFIX,
				XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		int ret = xattr_getsecurity(idmap, inode, suffix, value,
					    size);
		/*
		 * Only overwrite the return value if a security module
		 * is actually active.
		 */
		if (ret == -EOPNOTSUPP)
			goto nolsm;
		return ret;
	}
nolsm:
	return __vfs_getxattr(dentry, inode, name, value, size);
}
EXPORT_SYMBOL_GPL(vfs_getxattr);

/**
 * vfs_listxattr - retrieve \0 separated list of xattr names
 * @dentry: the dentry from whose inode the xattr names are retrieved
 * @list: buffer to store xattr names into
 * @size: size of the buffer
 *
 * This function returns the names of all xattrs associated with the
 * inode of @dentry.
 *
 * Note, for legacy reasons the vfs_listxattr() function lists POSIX
 * ACLs as well. Since POSIX ACLs are decoupled from IOP_XATTR the
 * vfs_listxattr() function doesn't check for this flag since a
 * filesystem could implement POSIX ACLs without implementing any other
 * xattrs.
 *
 * However, since all codepaths that remove IOP_XATTR also assign of
 * inode operations that either don't implement or implement a stub
 * ->listxattr() operation.
 *
 * Return: On success, the size of the buffer that was used. On error a
 *         negative error code.
 */
ssize_t
vfs_listxattr(struct dentry *dentry, char *list, size_t size)
{
	struct inode *inode = d_inode(dentry);
	ssize_t error;

	error = security_inode_listxattr(dentry);
	if (error)
		return error;

	if (inode->i_op->listxattr) {
		error = inode->i_op->listxattr(dentry, list, size);
	} else {
		error = security_inode_listsecurity(inode, list, size);
		if (size && error > size)
			error = -ERANGE;
	}
	return error;
}
EXPORT_SYMBOL_GPL(vfs_listxattr);

int
__vfs_removexattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  const char *name)
{
	struct inode *inode = d_inode(dentry);
	const struct xattr_handler *handler;

	if (is_posix_acl_xattr(name))
		return -EOPNOTSUPP;

	handler = xattr_resolve_name(inode, &name);
	if (IS_ERR(handler))
		return PTR_ERR(handler);
	if (!handler->set)
		return -EOPNOTSUPP;
	return handler->set(handler, idmap, dentry, inode, name, NULL, 0,
			    XATTR_REPLACE);
}
EXPORT_SYMBOL(__vfs_removexattr);

/**
 * __vfs_removexattr_locked - set an extended attribute while holding the inode
 * lock
 *
 *  @idmap: idmap of the mount of the target inode
 *  @dentry: object to perform setxattr on
 *  @name: name of xattr to remove
 *  @delegated_inode: on return, will contain an inode pointer that
 *  a delegation was broken on, NULL if none.
 */
int
__vfs_removexattr_locked(struct mnt_idmap *idmap,
			 struct dentry *dentry, const char *name,
			 struct delegated_inode *delegated_inode)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = xattr_permission(idmap, inode, name, MAY_WRITE);
	if (error)
		return error;

	error = security_inode_removexattr(idmap, dentry, name);
	if (error)
		goto out;

	error = try_break_deleg(inode, delegated_inode);
	if (error)
		goto out;

	error = __vfs_removexattr(idmap, dentry, name);
	if (error)
		return error;

	fsnotify_xattr(dentry);
	security_inode_post_removexattr(dentry, name);

out:
	return error;
}
EXPORT_SYMBOL_GPL(__vfs_removexattr_locked);

int
vfs_removexattr(struct mnt_idmap *idmap, struct dentry *dentry,
		const char *name)
{
	struct inode *inode = dentry->d_inode;
	struct delegated_inode delegated_inode = { };
	int error;

retry_deleg:
	inode_lock(inode);
	error = __vfs_removexattr_locked(idmap, dentry,
					 name, &delegated_inode);
	inode_unlock(inode);

	if (is_delegated(&delegated_inode)) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}

	return error;
}
EXPORT_SYMBOL_GPL(vfs_removexattr);

int import_xattr_name(struct xattr_name *kname, const char __user *name)
{
	int error = strncpy_from_user(kname->name, name,
					sizeof(kname->name));
	if (error == 0 || error == sizeof(kname->name))
		return -ERANGE;
	if (error < 0)
		return error;
	return 0;
}

/*
 * Extended attribute SET operations
 */

int setxattr_copy(const char __user *name, struct kernel_xattr_ctx *ctx)
{
	int error;

	if (ctx->flags & ~(XATTR_CREATE|XATTR_REPLACE))
		return -EINVAL;

	error = import_xattr_name(ctx->kname, name);
	if (error)
		return error;

	if (ctx->size) {
		if (ctx->size > XATTR_SIZE_MAX)
			return -E2BIG;

		ctx->kvalue = vmemdup_user(ctx->cvalue, ctx->size);
		if (IS_ERR(ctx->kvalue)) {
			error = PTR_ERR(ctx->kvalue);
			ctx->kvalue = NULL;
		}
	}

	return error;
}

static int do_setxattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct kernel_xattr_ctx *ctx)
{
	if (is_posix_acl_xattr(ctx->kname->name))
		return do_set_acl(idmap, dentry, ctx->kname->name,
				  ctx->kvalue, ctx->size);

	return vfs_setxattr(idmap, dentry, ctx->kname->name,
			ctx->kvalue, ctx->size, ctx->flags);
}

int file_setxattr(struct file *f, struct kernel_xattr_ctx *ctx)
{
	int error = mnt_want_write_file(f);

	if (!error) {
		audit_file(f);
		error = do_setxattr(file_mnt_idmap(f), f->f_path.dentry, ctx);
		mnt_drop_write_file(f);
	}
	return error;
}

int filename_setxattr(int dfd, struct filename *filename,
		      unsigned int lookup_flags, struct kernel_xattr_ctx *ctx)
{
	struct path path;
	int error;

retry:
	error = filename_lookup(dfd, filename, lookup_flags, &path, NULL);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = do_setxattr(mnt_idmap(path.mnt), path.dentry, ctx);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

static int path_setxattrat(int dfd, const char __user *pathname,
			   unsigned int at_flags, const char __user *name,
			   const void __user *value, size_t size, int flags)
{
	struct xattr_name kname;
	struct kernel_xattr_ctx ctx = {
		.cvalue	= value,
		.kvalue	= NULL,
		.size	= size,
		.kname	= &kname,
		.flags	= flags,
	};
	unsigned int lookup_flags = 0;
	int error;

	if ((at_flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;

	if (!(at_flags & AT_SYMLINK_NOFOLLOW))
		lookup_flags = LOOKUP_FOLLOW;

	error = setxattr_copy(name, &ctx);
	if (error)
		return error;

	CLASS(filename_maybe_null, filename)(pathname, at_flags);
	if (!filename && dfd >= 0) {
		CLASS(fd, f)(dfd);
		if (fd_empty(f))
			error = -EBADF;
		else
			error = file_setxattr(fd_file(f), &ctx);
	} else {
		error = filename_setxattr(dfd, filename, lookup_flags, &ctx);
	}
	kvfree(ctx.kvalue);
	return error;
}

SYSCALL_DEFINE6(setxattrat, int, dfd, const char __user *, pathname, unsigned int, at_flags,
		const char __user *, name, const struct xattr_args __user *, uargs,
		size_t, usize)
{
	struct xattr_args args = {};
	int error;

	BUILD_BUG_ON(sizeof(struct xattr_args) < XATTR_ARGS_SIZE_VER0);
	BUILD_BUG_ON(sizeof(struct xattr_args) != XATTR_ARGS_SIZE_LATEST);

	if (unlikely(usize < XATTR_ARGS_SIZE_VER0))
		return -EINVAL;
	if (usize > PAGE_SIZE)
		return -E2BIG;

	error = copy_struct_from_user(&args, sizeof(args), uargs, usize);
	if (error)
		return error;

	return path_setxattrat(dfd, pathname, at_flags, name,
			       u64_to_user_ptr(args.value), args.size,
			       args.flags);
}

SYSCALL_DEFINE5(setxattr, const char __user *, pathname,
		const char __user *, name, const void __user *, value,
		size_t, size, int, flags)
{
	return path_setxattrat(AT_FDCWD, pathname, 0, name, value, size, flags);
}

SYSCALL_DEFINE5(lsetxattr, const char __user *, pathname,
		const char __user *, name, const void __user *, value,
		size_t, size, int, flags)
{
	return path_setxattrat(AT_FDCWD, pathname, AT_SYMLINK_NOFOLLOW, name,
			       value, size, flags);
}

SYSCALL_DEFINE5(fsetxattr, int, fd, const char __user *, name,
		const void __user *,value, size_t, size, int, flags)
{
	return path_setxattrat(fd, NULL, AT_EMPTY_PATH, name,
			       value, size, flags);
}

/*
 * Extended attribute GET operations
 */
static ssize_t
do_getxattr(struct mnt_idmap *idmap, struct dentry *d,
	struct kernel_xattr_ctx *ctx)
{
	ssize_t error;
	char *kname = ctx->kname->name;
	void *kvalue = NULL;

	if (ctx->size) {
		if (ctx->size > XATTR_SIZE_MAX)
			ctx->size = XATTR_SIZE_MAX;
		kvalue = kvzalloc(ctx->size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
	}

	if (is_posix_acl_xattr(kname))
		error = do_get_acl(idmap, d, kname, kvalue, ctx->size);
	else
		error = vfs_getxattr(idmap, d, kname, kvalue, ctx->size);
	if (error > 0) {
		if (ctx->size && copy_to_user(ctx->value, kvalue, error))
			error = -EFAULT;
	} else if (error == -ERANGE && ctx->size >= XATTR_SIZE_MAX) {
		/* The file system tried to returned a value bigger
		   than XATTR_SIZE_MAX bytes. Not possible. */
		error = -E2BIG;
	}

	kvfree(kvalue);
	return error;
}

ssize_t file_getxattr(struct file *f, struct kernel_xattr_ctx *ctx)
{
	audit_file(f);
	return do_getxattr(file_mnt_idmap(f), f->f_path.dentry, ctx);
}

ssize_t filename_getxattr(int dfd, struct filename *filename,
			  unsigned int lookup_flags, struct kernel_xattr_ctx *ctx)
{
	struct path path;
	ssize_t error;
retry:
	error = filename_lookup(dfd, filename, lookup_flags, &path, NULL);
	if (error)
		return error;
	error = do_getxattr(mnt_idmap(path.mnt), path.dentry, ctx);
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

static ssize_t path_getxattrat(int dfd, const char __user *pathname,
			       unsigned int at_flags, const char __user *name,
			       void __user *value, size_t size)
{
	struct xattr_name kname;
	struct kernel_xattr_ctx ctx = {
		.value    = value,
		.size     = size,
		.kname    = &kname,
		.flags    = 0,
	};
	ssize_t error;

	if ((at_flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;

	error = import_xattr_name(&kname, name);
	if (error)
		return error;

	CLASS(filename_maybe_null, filename)(pathname, at_flags);
	if (!filename && dfd >= 0) {
		CLASS(fd, f)(dfd);
		if (fd_empty(f))
			return -EBADF;
		return file_getxattr(fd_file(f), &ctx);
	} else {
		int lookup_flags = 0;
		if (!(at_flags & AT_SYMLINK_NOFOLLOW))
			lookup_flags = LOOKUP_FOLLOW;
		return filename_getxattr(dfd, filename, lookup_flags, &ctx);
	}
}

SYSCALL_DEFINE6(getxattrat, int, dfd, const char __user *, pathname, unsigned int, at_flags,
		const char __user *, name, struct xattr_args __user *, uargs, size_t, usize)
{
	struct xattr_args args = {};
	int error;

	BUILD_BUG_ON(sizeof(struct xattr_args) < XATTR_ARGS_SIZE_VER0);
	BUILD_BUG_ON(sizeof(struct xattr_args) != XATTR_ARGS_SIZE_LATEST);

	if (unlikely(usize < XATTR_ARGS_SIZE_VER0))
		return -EINVAL;
	if (usize > PAGE_SIZE)
		return -E2BIG;

	error = copy_struct_from_user(&args, sizeof(args), uargs, usize);
	if (error)
		return error;

	if (args.flags != 0)
		return -EINVAL;

	return path_getxattrat(dfd, pathname, at_flags, name,
			       u64_to_user_ptr(args.value), args.size);
}

SYSCALL_DEFINE4(getxattr, const char __user *, pathname,
		const char __user *, name, void __user *, value, size_t, size)
{
	return path_getxattrat(AT_FDCWD, pathname, 0, name, value, size);
}

SYSCALL_DEFINE4(lgetxattr, const char __user *, pathname,
		const char __user *, name, void __user *, value, size_t, size)
{
	return path_getxattrat(AT_FDCWD, pathname, AT_SYMLINK_NOFOLLOW, name,
			       value, size);
}

SYSCALL_DEFINE4(fgetxattr, int, fd, const char __user *, name,
		void __user *, value, size_t, size)
{
	return path_getxattrat(fd, NULL, AT_EMPTY_PATH, name, value, size);
}

/*
 * Extended attribute LIST operations
 */
static ssize_t
listxattr(struct dentry *d, char __user *list, size_t size)
{
	ssize_t error;
	char *klist = NULL;

	if (size) {
		if (size > XATTR_LIST_MAX)
			size = XATTR_LIST_MAX;
		klist = kvmalloc(size, GFP_KERNEL);
		if (!klist)
			return -ENOMEM;
	}

	error = vfs_listxattr(d, klist, size);
	if (error > 0) {
		if (size && copy_to_user(list, klist, error))
			error = -EFAULT;
	} else if (error == -ERANGE && size >= XATTR_LIST_MAX) {
		/* The file system tried to returned a list bigger
		   than XATTR_LIST_MAX bytes. Not possible. */
		error = -E2BIG;
	}

	kvfree(klist);

	return error;
}

static
ssize_t file_listxattr(struct file *f, char __user *list, size_t size)
{
	audit_file(f);
	return listxattr(f->f_path.dentry, list, size);
}

static
ssize_t filename_listxattr(int dfd, struct filename *filename,
			   unsigned int lookup_flags,
			   char __user *list, size_t size)
{
	struct path path;
	ssize_t error;
retry:
	error = filename_lookup(dfd, filename, lookup_flags, &path, NULL);
	if (error)
		return error;
	error = listxattr(path.dentry, list, size);
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

static ssize_t path_listxattrat(int dfd, const char __user *pathname,
				unsigned int at_flags, char __user *list,
				size_t size)
{
	int lookup_flags;

	if ((at_flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;

	CLASS(filename_maybe_null, filename)(pathname, at_flags);
	if (!filename) {
		CLASS(fd, f)(dfd);
		if (fd_empty(f))
			return -EBADF;
		return file_listxattr(fd_file(f), list, size);
	}

	lookup_flags = (at_flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	return filename_listxattr(dfd, filename, lookup_flags, list, size);
}

SYSCALL_DEFINE5(listxattrat, int, dfd, const char __user *, pathname,
		unsigned int, at_flags,
		char __user *, list, size_t, size)
{
	return path_listxattrat(dfd, pathname, at_flags, list, size);
}

SYSCALL_DEFINE3(listxattr, const char __user *, pathname, char __user *, list,
		size_t, size)
{
	return path_listxattrat(AT_FDCWD, pathname, 0, list, size);
}

SYSCALL_DEFINE3(llistxattr, const char __user *, pathname, char __user *, list,
		size_t, size)
{
	return path_listxattrat(AT_FDCWD, pathname, AT_SYMLINK_NOFOLLOW, list, size);
}

SYSCALL_DEFINE3(flistxattr, int, fd, char __user *, list, size_t, size)
{
	return path_listxattrat(fd, NULL, AT_EMPTY_PATH, list, size);
}

/*
 * Extended attribute REMOVE operations
 */
static long
removexattr(struct mnt_idmap *idmap, struct dentry *d, const char *name)
{
	if (is_posix_acl_xattr(name))
		return vfs_remove_acl(idmap, d, name);
	return vfs_removexattr(idmap, d, name);
}

static int file_removexattr(struct file *f, struct xattr_name *kname)
{
	int error = mnt_want_write_file(f);

	if (!error) {
		audit_file(f);
		error = removexattr(file_mnt_idmap(f),
				    f->f_path.dentry, kname->name);
		mnt_drop_write_file(f);
	}
	return error;
}

static int filename_removexattr(int dfd, struct filename *filename,
				unsigned int lookup_flags, struct xattr_name *kname)
{
	struct path path;
	int error;

retry:
	error = filename_lookup(dfd, filename, lookup_flags, &path, NULL);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = removexattr(mnt_idmap(path.mnt), path.dentry, kname->name);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

static int path_removexattrat(int dfd, const char __user *pathname,
			      unsigned int at_flags, const char __user *name)
{
	struct xattr_name kname;
	unsigned int lookup_flags;
	int error;

	if ((at_flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;

	error = import_xattr_name(&kname, name);
	if (error)
		return error;

	CLASS(filename_maybe_null, filename)(pathname, at_flags);
	if (!filename) {
		CLASS(fd, f)(dfd);
		if (fd_empty(f))
			return -EBADF;
		return file_removexattr(fd_file(f), &kname);
	}
	lookup_flags = (at_flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	return filename_removexattr(dfd, filename, lookup_flags, &kname);
}

SYSCALL_DEFINE4(removexattrat, int, dfd, const char __user *, pathname,
		unsigned int, at_flags, const char __user *, name)
{
	return path_removexattrat(dfd, pathname, at_flags, name);
}

SYSCALL_DEFINE2(removexattr, const char __user *, pathname,
		const char __user *, name)
{
	return path_removexattrat(AT_FDCWD, pathname, 0, name);
}

SYSCALL_DEFINE2(lremovexattr, const char __user *, pathname,
		const char __user *, name)
{
	return path_removexattrat(AT_FDCWD, pathname, AT_SYMLINK_NOFOLLOW, name);
}

SYSCALL_DEFINE2(fremovexattr, int, fd, const char __user *, name)
{
	return path_removexattrat(fd, NULL, AT_EMPTY_PATH, name);
}

int xattr_list_one(char **buffer, ssize_t *remaining_size, const char *name)
{
	size_t len;

	len = strlen(name) + 1;
	if (*buffer) {
		if (*remaining_size < len)
			return -ERANGE;
		memcpy(*buffer, name, len);
		*buffer += len;
	}
	*remaining_size -= len;
	return 0;
}

/**
 * generic_listxattr - run through a dentry's xattr list() operations
 * @dentry: dentry to list the xattrs
 * @buffer: result buffer
 * @buffer_size: size of @buffer
 *
 * Combine the results of the list() operation from every xattr_handler in the
 * xattr_handler stack.
 *
 * Note that this will not include the entries for POSIX ACLs.
 */
ssize_t
generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	const struct xattr_handler *handler, * const *handlers = dentry->d_sb->s_xattr;
	ssize_t remaining_size = buffer_size;

	for_each_xattr_handler(handlers, handler) {
		int err;

		if (!handler->name || (handler->list && !handler->list(dentry)))
			continue;
		err = xattr_list_one(&buffer, &remaining_size, handler->name);
		if (err)
			return err;
	}

	return buffer_size - remaining_size;
}
EXPORT_SYMBOL(generic_listxattr);

/**
 * xattr_full_name  -  Compute full attribute name from suffix
 *
 * @handler:	handler of the xattr_handler operation
 * @name:	name passed to the xattr_handler operation
 *
 * The get and set xattr handler operations are called with the remainder of
 * the attribute name after skipping the handler's prefix: for example, "foo"
 * is passed to the get operation of a handler with prefix "user." to get
 * attribute "user.foo".  The full name is still "there" in the name though.
 *
 * Note: the list xattr handler operation when called from the vfs is passed a
 * NULL name; some file systems use this operation internally, with varying
 * semantics.
 */
const char *xattr_full_name(const struct xattr_handler *handler,
			    const char *name)
{
	size_t prefix_len = strlen(xattr_prefix(handler));

	return name - prefix_len;
}
EXPORT_SYMBOL(xattr_full_name);

/**
 * simple_xattr_space - estimate the memory used by a simple xattr
 * @name: the full name of the xattr
 * @size: the size of its value
 *
 * This takes no account of how much larger the two slab objects actually are:
 * that would depend on the slab implementation, when what is required is a
 * deterministic number, which grows with name length and size and quantity.
 *
 * Return: The approximate number of bytes of memory used by such an xattr.
 */
size_t simple_xattr_space(const char *name, size_t size)
{
	/*
	 * Use "40" instead of sizeof(struct simple_xattr), to return the
	 * same result on 32-bit and 64-bit, and even if simple_xattr grows.
	 */
	return 40 + size + strlen(name);
}

/**
 * simple_xattr_free - free an xattr object
 * @xattr: the xattr object
 *
 * Free the xattr object. Can handle @xattr being NULL.
 */
void simple_xattr_free(struct simple_xattr *xattr)
{
	if (xattr)
		kfree(xattr->name);
	kvfree(xattr);
}

static void simple_xattr_rcu_free(struct rcu_head *head)
{
	struct simple_xattr *xattr = container_of(head, struct simple_xattr, rcu);

	simple_xattr_free(xattr);
}

/**
 * simple_xattr_free_rcu - free an xattr object with RCU delay
 * @xattr: the xattr object
 *
 * Free the xattr object after an RCU grace period. This must be used when
 * the xattr was removed from a data structure that concurrent RCU readers
 * may still be traversing. Can handle @xattr being NULL.
 */
void simple_xattr_free_rcu(struct simple_xattr *xattr)
{
	if (xattr)
		call_rcu(&xattr->rcu, simple_xattr_rcu_free);
}

/**
 * simple_xattr_alloc - allocate new xattr object
 * @value: value of the xattr object
 * @size: size of @value
 *
 * Allocate a new xattr object and initialize respective members. The caller is
 * responsible for handling the name of the xattr.
 *
 * Return: New xattr object on success, NULL if @value is NULL, ERR_PTR on
 * failure.
 */
struct simple_xattr *simple_xattr_alloc(const void *value, size_t size)
{
	struct simple_xattr *new_xattr;
	size_t len;

	if (!value)
		return NULL;

	/* wrap around? */
	len = sizeof(*new_xattr) + size;
	if (len < sizeof(*new_xattr))
		return ERR_PTR(-ENOMEM);

	new_xattr = kvmalloc(len, GFP_KERNEL_ACCOUNT);
	if (!new_xattr)
		return ERR_PTR(-ENOMEM);

	new_xattr->size = size;
	memcpy(new_xattr->value, value, size);
	return new_xattr;
}

static u32 simple_xattr_hashfn(const void *data, u32 len, u32 seed)
{
	const char *name = data;
	return jhash(name, strlen(name), seed);
}

static u32 simple_xattr_obj_hashfn(const void *obj, u32 len, u32 seed)
{
	const struct simple_xattr *xattr = obj;
	return jhash(xattr->name, strlen(xattr->name), seed);
}

static int simple_xattr_obj_cmpfn(struct rhashtable_compare_arg *arg,
				   const void *obj)
{
	const struct simple_xattr *xattr = obj;
	return strcmp(xattr->name, arg->key);
}

static const struct rhashtable_params simple_xattr_params = {
	.head_offset    = offsetof(struct simple_xattr, hash_node),
	.hashfn         = simple_xattr_hashfn,
	.obj_hashfn     = simple_xattr_obj_hashfn,
	.obj_cmpfn      = simple_xattr_obj_cmpfn,
	.automatic_shrinking = true,
};

/**
 * simple_xattr_get - get an xattr object
 * @xattrs: the header of the xattr object
 * @name: the name of the xattr to retrieve
 * @buffer: the buffer to store the value into
 * @size: the size of @buffer
 *
 * Try to find and retrieve the xattr object associated with @name.
 * If @buffer is provided store the value of @xattr in @buffer
 * otherwise just return the length. The size of @buffer is limited
 * to XATTR_SIZE_MAX which currently is 65536.
 *
 * Return: On success the length of the xattr value is returned. On error a
 * negative error code is returned.
 */
int simple_xattr_get(struct simple_xattrs *xattrs, const char *name,
		     void *buffer, size_t size)
{
	struct simple_xattr *xattr;
	int ret = -ENODATA;

	guard(rcu)();
	xattr = rhashtable_lookup(&xattrs->ht, name, simple_xattr_params);
	if (xattr) {
		ret = xattr->size;
		if (buffer) {
			if (size < xattr->size)
				ret = -ERANGE;
			else
				memcpy(buffer, xattr->value, xattr->size);
		}
	}
	return ret;
}

/**
 * simple_xattr_set - set an xattr object
 * @xattrs: the header of the xattr object
 * @name: the name of the xattr to retrieve
 * @value: the value to store along the xattr
 * @size: the size of @value
 * @flags: the flags determining how to set the xattr
 *
 * Set a new xattr object.
 * If @value is passed a new xattr object will be allocated. If XATTR_REPLACE
 * is specified in @flags a matching xattr object for @name must already exist.
 * If it does it will be replaced with the new xattr object. If it doesn't we
 * fail. If XATTR_CREATE is specified and a matching xattr does already exist
 * we fail. If it doesn't we create a new xattr. If @flags is zero we simply
 * insert the new xattr replacing any existing one.
 *
 * If @value is empty and a matching xattr object is found we delete it if
 * XATTR_REPLACE is specified in @flags or @flags is zero.
 *
 * If @value is empty and no matching xattr object for @name is found we do
 * nothing if XATTR_CREATE is specified in @flags or @flags is zero. For
 * XATTR_REPLACE we fail as mentioned above.
 *
 * Note: Callers must externally serialize writes. All current callers hold
 * the inode lock for write operations. The lookup->replace/remove sequence
 * is not atomic with respect to the rhashtable's per-bucket locking, but
 * is safe because writes are serialized by the caller.
 *
 * Return: On success, the removed or replaced xattr is returned, to be freed
 * by the caller; or NULL if none. On failure a negative error code is returned.
 */
struct simple_xattr *simple_xattr_set(struct simple_xattrs *xattrs,
				      const char *name, const void *value,
				      size_t size, int flags)
{
	struct simple_xattr *old_xattr = NULL;
	int err;

	CLASS(simple_xattr, new_xattr)(value, size);
	if (IS_ERR(new_xattr))
		return new_xattr;

	if (new_xattr) {
		new_xattr->name = kstrdup(name, GFP_KERNEL_ACCOUNT);
		if (!new_xattr->name)
			return ERR_PTR(-ENOMEM);
	}

	/* Lookup is safe without RCU here since writes are serialized. */
	old_xattr = rhashtable_lookup_fast(&xattrs->ht, name,
					   simple_xattr_params);

	if (old_xattr) {
		/* Fail if XATTR_CREATE is requested and the xattr exists. */
		if (flags & XATTR_CREATE)
			return ERR_PTR(-EEXIST);

		if (new_xattr) {
			err = rhashtable_replace_fast(&xattrs->ht,
						      &old_xattr->hash_node,
						      &new_xattr->hash_node,
						      simple_xattr_params);
			if (err)
				return ERR_PTR(err);
		} else {
			err = rhashtable_remove_fast(&xattrs->ht,
						     &old_xattr->hash_node,
						     simple_xattr_params);
			if (err)
				return ERR_PTR(err);
		}
	} else {
		/* Fail if XATTR_REPLACE is requested but no xattr is found. */
		if (flags & XATTR_REPLACE)
			return ERR_PTR(-ENODATA);

		/*
		 * If XATTR_CREATE or no flags are specified together with a
		 * new value simply insert it.
		 */
		if (new_xattr) {
			err = rhashtable_insert_fast(&xattrs->ht,
						     &new_xattr->hash_node,
						     simple_xattr_params);
			if (err)
				return ERR_PTR(err);
		}

		/*
		 * If XATTR_CREATE or no flags are specified and neither an
		 * old or new xattr exist then we don't need to do anything.
		 */
	}

	retain_and_null_ptr(new_xattr);
	return old_xattr;
}

static inline void simple_xattr_limits_dec(struct simple_xattr_limits *limits,
					   size_t size)
{
	atomic_sub(size, &limits->xattr_size);
	atomic_dec(&limits->nr_xattrs);
}

static inline int simple_xattr_limits_inc(struct simple_xattr_limits *limits,
					  size_t size)
{
	if (atomic_inc_return(&limits->nr_xattrs) > SIMPLE_XATTR_MAX_NR) {
		atomic_dec(&limits->nr_xattrs);
		return -ENOSPC;
	}

	if (atomic_add_return(size, &limits->xattr_size) <= SIMPLE_XATTR_MAX_SIZE)
		return 0;

	simple_xattr_limits_dec(limits, size);
	return -ENOSPC;
}

/**
 * simple_xattr_set_limited - set an xattr with per-inode user.* limits
 * @xattrs: the header of the xattr object
 * @limits: per-inode limit counters for user.* xattrs
 * @name: the name of the xattr to set or remove
 * @value: the value to store (NULL to remove)
 * @size: the size of @value
 * @flags: XATTR_CREATE, XATTR_REPLACE, or 0
 *
 * Like simple_xattr_set(), but enforces per-inode count and total value size
 * limits for user.* xattrs. Uses speculative pre-increment of the atomic
 * counters to avoid races without requiring external locks.
 *
 * Return: On success zero is returned. On failure a negative error code is
 * returned.
 */
int simple_xattr_set_limited(struct simple_xattrs *xattrs,
			     struct simple_xattr_limits *limits,
			     const char *name, const void *value,
			     size_t size, int flags)
{
	struct simple_xattr *old_xattr;
	int ret;

	if (value) {
		ret = simple_xattr_limits_inc(limits, size);
		if (ret)
			return ret;
	}

	old_xattr = simple_xattr_set(xattrs, name, value, size, flags);
	if (IS_ERR(old_xattr)) {
		if (value)
			simple_xattr_limits_dec(limits, size);
		return PTR_ERR(old_xattr);
	}
	if (old_xattr) {
		simple_xattr_limits_dec(limits, old_xattr->size);
		simple_xattr_free_rcu(old_xattr);
	}
	return 0;
}

static bool xattr_is_trusted(const char *name)
{
	return !strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN);
}

static bool xattr_is_maclabel(const char *name)
{
	const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;

	return !strncmp(name, XATTR_SECURITY_PREFIX,
			XATTR_SECURITY_PREFIX_LEN) &&
		security_ismaclabel(suffix);
}

/**
 * simple_xattr_list - list all xattr objects
 * @inode: inode from which to get the xattrs
 * @xattrs: the header of the xattr object
 * @buffer: the buffer to store all xattrs into
 * @size: the size of @buffer
 *
 * List all xattrs associated with @inode. If @buffer is NULL we returned
 * the required size of the buffer. If @buffer is provided we store the
 * xattrs value into it provided it is big enough.
 *
 * Note, the number of xattr names that can be listed with listxattr(2) is
 * limited to XATTR_LIST_MAX aka 65536 bytes. If a larger buffer is passed
 * then vfs_listxattr() caps it to XATTR_LIST_MAX and if more xattr names
 * are found it will return -E2BIG.
 *
 * Return: On success the required size or the size of the copied xattrs is
 * returned. On error a negative error code is returned.
 */
ssize_t simple_xattr_list(struct inode *inode, struct simple_xattrs *xattrs,
			  char *buffer, size_t size)
{
	bool trusted = ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN);
	struct rhashtable_iter iter;
	struct simple_xattr *xattr;
	ssize_t remaining_size = size;
	int err = 0;

	err = posix_acl_listxattr(inode, &buffer, &remaining_size);
	if (err)
		return err;

	err = security_inode_listsecurity(inode, buffer, remaining_size);
	if (err < 0)
		return err;

	if (buffer) {
		if (remaining_size < err)
			return -ERANGE;
		buffer += err;
	}
	remaining_size -= err;
	err = 0;

	if (!xattrs)
		return size - remaining_size;

	rhashtable_walk_enter(&xattrs->ht, &iter);
	rhashtable_walk_start(&iter);

	while ((xattr = rhashtable_walk_next(&iter)) != NULL) {
		if (IS_ERR(xattr)) {
			if (PTR_ERR(xattr) == -EAGAIN)
				continue;
			err = PTR_ERR(xattr);
			break;
		}

		/* skip "trusted." attributes for unprivileged callers */
		if (!trusted && xattr_is_trusted(xattr->name))
			continue;

		/* skip MAC labels; these are provided by LSM above */
		if (xattr_is_maclabel(xattr->name))
			continue;

		err = xattr_list_one(&buffer, &remaining_size, xattr->name);
		if (err)
			break;
	}

	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);

	return err ? err : size - remaining_size;
}

/**
 * simple_xattr_add - add xattr objects
 * @xattrs: the header of the xattr object
 * @new_xattr: the xattr object to add
 *
 * Add an xattr object to @xattrs. This assumes no replacement or removal
 * of matching xattrs is wanted. Should only be called during inode
 * initialization when a few distinct initial xattrs are supposed to be set.
 *
 * Return: On success zero is returned. On failure a negative error code is
 * returned.
 */
int simple_xattr_add(struct simple_xattrs *xattrs,
		     struct simple_xattr *new_xattr)
{
	return rhashtable_insert_fast(&xattrs->ht, &new_xattr->hash_node,
				      simple_xattr_params);
}

/**
 * simple_xattrs_init - initialize new xattr header
 * @xattrs: header to initialize
 *
 * Initialize the rhashtable used to store xattr objects.
 *
 * Return: On success zero is returned. On failure a negative error code is
 * returned.
 */
int simple_xattrs_init(struct simple_xattrs *xattrs)
{
	return rhashtable_init(&xattrs->ht, &simple_xattr_params);
}

/**
 * simple_xattrs_alloc - allocate and initialize a new xattr header
 *
 * Dynamically allocate a simple_xattrs header and initialize the
 * underlying rhashtable. This is intended for consumers that want
 * to lazily allocate xattr storage only when the first xattr is set,
 * avoiding the per-inode rhashtable overhead when no xattrs are used.
 *
 * Return: On success a new simple_xattrs is returned. On failure an
 * ERR_PTR is returned.
 */
struct simple_xattrs *simple_xattrs_alloc(void)
{
	struct simple_xattrs *xattrs __free(kfree) = NULL;
	int ret;

	xattrs = kzalloc(sizeof(*xattrs), GFP_KERNEL);
	if (!xattrs)
		return ERR_PTR(-ENOMEM);

	ret = simple_xattrs_init(xattrs);
	if (ret)
		return ERR_PTR(ret);

	return no_free_ptr(xattrs);
}

/**
 * simple_xattrs_lazy_alloc - get or allocate xattrs for a set operation
 * @xattrsp: pointer to the xattrs pointer (may point to NULL)
 * @value: value being set (NULL means remove)
 * @flags: xattr set flags
 *
 * For lazily-allocated xattrs on the write path. If no xattrs exist yet
 * and this is a remove operation, returns the appropriate result without
 * allocating. Otherwise ensures xattrs is allocated and published with
 * store-release semantics.
 *
 * Return: On success a valid pointer to the xattrs is returned. On
 * failure or early-exit an ERR_PTR or NULL is returned. Callers should
 * check with IS_ERR_OR_NULL() and propagate with PTR_ERR() which
 * correctly returns 0 for the NULL no-op case.
 */
struct simple_xattrs *simple_xattrs_lazy_alloc(struct simple_xattrs **xattrsp,
					       const void *value, int flags)
{
	struct simple_xattrs *xattrs;

	xattrs = READ_ONCE(*xattrsp);
	if (xattrs)
		return xattrs;

	if (!value)
		return (flags & XATTR_REPLACE) ? ERR_PTR(-ENODATA) : NULL;

	xattrs = simple_xattrs_alloc();
	if (!IS_ERR(xattrs))
		smp_store_release(xattrsp, xattrs);
	return xattrs;
}

static void simple_xattr_ht_free(void *ptr, void *arg)
{
	struct simple_xattr *xattr = ptr;
	size_t *freed_space = arg;

	if (freed_space)
		*freed_space += simple_xattr_space(xattr->name, xattr->size);
	simple_xattr_free(xattr);
}

/**
 * simple_xattrs_free - free xattrs
 * @xattrs: xattr header whose xattrs to destroy
 * @freed_space: approximate number of bytes of memory freed from @xattrs
 *
 * Destroy all xattrs in @xattr. When this is called no one can hold a
 * reference to any of the xattrs anymore.
 */
void simple_xattrs_free(struct simple_xattrs *xattrs, size_t *freed_space)
{
	might_sleep();

	if (freed_space)
		*freed_space = 0;
	rhashtable_free_and_destroy(&xattrs->ht, simple_xattr_ht_free,
				    freed_space);
}
