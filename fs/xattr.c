/*
  File: fs/xattr.c

  Extended attribute handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (C) 2001 SGI - Silicon Graphics, Inc <linux-xfs@oss.sgi.com>
  Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/fsnotify.h>
#include <linux/audit.h>
#include <asm/uaccess.h>


/*
 * Check permissions for extended attribute access.  This is a bit complicated
 * because different namespaces have very different rules.
 */
static int
xattr_permission(struct inode *inode, const char *name, int mask)
{
	/*
	 * We can never set or remove an extended attribute on a read-only
	 * filesystem  or on an immutable / append-only inode.
	 */
	if (mask & MAY_WRITE) {
		if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
			return -EPERM;
	}

	/*
	 * No restriction for security.* and system.* from the VFS.  Decision
	 * on these is left to the underlying filesystem / security module.
	 */
	if (!strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN) ||
	    !strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return 0;

	/*
	 * The trusted.* namespace can only be accessed by a privileged user.
	 */
	if (!strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN))
		return (capable(CAP_SYS_ADMIN) ? 0 : -EPERM);

	/* In user.* namespace, only regular files and directories can have
	 * extended attributes. For sticky directories, only the owner and
	 * privileged user can write attributes.
	 */
	if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN)) {
		if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
			return -EPERM;
		if (S_ISDIR(inode->i_mode) && (inode->i_mode & S_ISVTX) &&
		    (mask & MAY_WRITE) && !is_owner_or_cap(inode))
			return -EPERM;
	}

	return permission(inode, mask, NULL);
}

int
vfs_setxattr(struct dentry *dentry, char *name, void *value,
		size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = xattr_permission(inode, name, MAY_WRITE);
	if (error)
		return error;

	mutex_lock(&inode->i_mutex);
	error = security_inode_setxattr(dentry, name, value, size, flags);
	if (error)
		goto out;
	error = -EOPNOTSUPP;
	if (inode->i_op->setxattr) {
		error = inode->i_op->setxattr(dentry, name, value, size, flags);
		if (!error) {
			fsnotify_xattr(dentry);
			security_inode_post_setxattr(dentry, name, value,
						     size, flags);
		}
	} else if (!strncmp(name, XATTR_SECURITY_PREFIX,
				XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		error = security_inode_setsecurity(inode, suffix, value,
						   size, flags);
		if (!error)
			fsnotify_xattr(dentry);
	}
out:
	mutex_unlock(&inode->i_mutex);
	return error;
}
EXPORT_SYMBOL_GPL(vfs_setxattr);

ssize_t
xattr_getsecurity(struct inode *inode, const char *name, void *value,
			size_t size)
{
	void *buffer = NULL;
	ssize_t len;

	if (!value || !size) {
		len = security_inode_getsecurity(inode, name, &buffer, false);
		goto out_noalloc;
	}

	len = security_inode_getsecurity(inode, name, &buffer, true);
	if (len < 0)
		return len;
	if (size < len) {
		len = -ERANGE;
		goto out;
	}
	memcpy(value, buffer, len);
out:
	security_release_secctx(buffer, len);
out_noalloc:
	return len;
}
EXPORT_SYMBOL_GPL(xattr_getsecurity);

ssize_t
vfs_getxattr(struct dentry *dentry, char *name, void *value, size_t size)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = xattr_permission(inode, name, MAY_READ);
	if (error)
		return error;

	error = security_inode_getxattr(dentry, name);
	if (error)
		return error;

	if (!strncmp(name, XATTR_SECURITY_PREFIX,
				XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		int ret = xattr_getsecurity(inode, suffix, value, size);
		/*
		 * Only overwrite the return value if a security module
		 * is actually active.
		 */
		if (ret == -EOPNOTSUPP)
			goto nolsm;
		return ret;
	}
nolsm:
	if (inode->i_op->getxattr)
		error = inode->i_op->getxattr(dentry, name, value, size);
	else
		error = -EOPNOTSUPP;

	return error;
}
EXPORT_SYMBOL_GPL(vfs_getxattr);

ssize_t
vfs_listxattr(struct dentry *d, char *list, size_t size)
{
	ssize_t error;

	error = security_inode_listxattr(d);
	if (error)
		return error;
	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->listxattr) {
		error = d->d_inode->i_op->listxattr(d, list, size);
	} else {
		error = security_inode_listsecurity(d->d_inode, list, size);
		if (size && error > size)
			error = -ERANGE;
	}
	return error;
}
EXPORT_SYMBOL_GPL(vfs_listxattr);

int
vfs_removexattr(struct dentry *dentry, char *name)
{
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode->i_op->removexattr)
		return -EOPNOTSUPP;

	error = xattr_permission(inode, name, MAY_WRITE);
	if (error)
		return error;

	error = security_inode_removexattr(dentry, name);
	if (error)
		return error;

	mutex_lock(&inode->i_mutex);
	error = inode->i_op->removexattr(dentry, name);
	mutex_unlock(&inode->i_mutex);

	if (!error)
		fsnotify_xattr(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(vfs_removexattr);


/*
 * Extended attribute SET operations
 */
static long
setxattr(struct dentry *d, char __user *name, void __user *value,
	 size_t size, int flags)
{
	int error;
	void *kvalue = NULL;
	char kname[XATTR_NAME_MAX + 1];

	if (flags & ~(XATTR_CREATE|XATTR_REPLACE))
		return -EINVAL;

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	if (size) {
		if (size > XATTR_SIZE_MAX)
			return -E2BIG;
		kvalue = kmalloc(size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
		if (copy_from_user(kvalue, value, size)) {
			kfree(kvalue);
			return -EFAULT;
		}
	}

	error = vfs_setxattr(d, kname, kvalue, size, flags);
	kfree(kvalue);
	return error;
}

asmlinkage long
sys_setxattr(char __user *path, char __user *name, void __user *value,
	     size_t size, int flags)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (error)
		return error;
	error = mnt_want_write(nd.path.mnt);
	if (!error) {
		error = setxattr(nd.path.dentry, name, value, size, flags);
		mnt_drop_write(nd.path.mnt);
	}
	path_put(&nd.path);
	return error;
}

asmlinkage long
sys_lsetxattr(char __user *path, char __user *name, void __user *value,
	      size_t size, int flags)
{
	struct nameidata nd;
	int error;

	error = user_path_walk_link(path, &nd);
	if (error)
		return error;
	error = mnt_want_write(nd.path.mnt);
	if (!error) {
		error = setxattr(nd.path.dentry, name, value, size, flags);
		mnt_drop_write(nd.path.mnt);
	}
	path_put(&nd.path);
	return error;
}

asmlinkage long
sys_fsetxattr(int fd, char __user *name, void __user *value,
	      size_t size, int flags)
{
	struct file *f;
	struct dentry *dentry;
	int error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	dentry = f->f_path.dentry;
	audit_inode(NULL, dentry);
	error = mnt_want_write(f->f_path.mnt);
	if (!error) {
		error = setxattr(dentry, name, value, size, flags);
		mnt_drop_write(f->f_path.mnt);
	}
out_fput:
	fput(f);
	return error;
}

/*
 * Extended attribute GET operations
 */
static ssize_t
getxattr(struct dentry *d, char __user *name, void __user *value, size_t size)
{
	ssize_t error;
	void *kvalue = NULL;
	char kname[XATTR_NAME_MAX + 1];

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	if (size) {
		if (size > XATTR_SIZE_MAX)
			size = XATTR_SIZE_MAX;
		kvalue = kzalloc(size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
	}

	error = vfs_getxattr(d, kname, kvalue, size);
	if (error > 0) {
		if (size && copy_to_user(value, kvalue, error))
			error = -EFAULT;
	} else if (error == -ERANGE && size >= XATTR_SIZE_MAX) {
		/* The file system tried to returned a value bigger
		   than XATTR_SIZE_MAX bytes. Not possible. */
		error = -E2BIG;
	}
	kfree(kvalue);
	return error;
}

asmlinkage ssize_t
sys_getxattr(char __user *path, char __user *name, void __user *value,
	     size_t size)
{
	struct nameidata nd;
	ssize_t error;

	error = user_path_walk(path, &nd);
	if (error)
		return error;
	error = getxattr(nd.path.dentry, name, value, size);
	path_put(&nd.path);
	return error;
}

asmlinkage ssize_t
sys_lgetxattr(char __user *path, char __user *name, void __user *value,
	      size_t size)
{
	struct nameidata nd;
	ssize_t error;

	error = user_path_walk_link(path, &nd);
	if (error)
		return error;
	error = getxattr(nd.path.dentry, name, value, size);
	path_put(&nd.path);
	return error;
}

asmlinkage ssize_t
sys_fgetxattr(int fd, char __user *name, void __user *value, size_t size)
{
	struct file *f;
	ssize_t error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	audit_inode(NULL, f->f_path.dentry);
	error = getxattr(f->f_path.dentry, name, value, size);
	fput(f);
	return error;
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
		klist = kmalloc(size, GFP_KERNEL);
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
	kfree(klist);
	return error;
}

asmlinkage ssize_t
sys_listxattr(char __user *path, char __user *list, size_t size)
{
	struct nameidata nd;
	ssize_t error;

	error = user_path_walk(path, &nd);
	if (error)
		return error;
	error = listxattr(nd.path.dentry, list, size);
	path_put(&nd.path);
	return error;
}

asmlinkage ssize_t
sys_llistxattr(char __user *path, char __user *list, size_t size)
{
	struct nameidata nd;
	ssize_t error;

	error = user_path_walk_link(path, &nd);
	if (error)
		return error;
	error = listxattr(nd.path.dentry, list, size);
	path_put(&nd.path);
	return error;
}

asmlinkage ssize_t
sys_flistxattr(int fd, char __user *list, size_t size)
{
	struct file *f;
	ssize_t error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	audit_inode(NULL, f->f_path.dentry);
	error = listxattr(f->f_path.dentry, list, size);
	fput(f);
	return error;
}

/*
 * Extended attribute REMOVE operations
 */
static long
removexattr(struct dentry *d, char __user *name)
{
	int error;
	char kname[XATTR_NAME_MAX + 1];

	error = strncpy_from_user(kname, name, sizeof(kname));
	if (error == 0 || error == sizeof(kname))
		error = -ERANGE;
	if (error < 0)
		return error;

	return vfs_removexattr(d, kname);
}

asmlinkage long
sys_removexattr(char __user *path, char __user *name)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (error)
		return error;
	error = mnt_want_write(nd.path.mnt);
	if (!error) {
		error = removexattr(nd.path.dentry, name);
		mnt_drop_write(nd.path.mnt);
	}
	path_put(&nd.path);
	return error;
}

asmlinkage long
sys_lremovexattr(char __user *path, char __user *name)
{
	struct nameidata nd;
	int error;

	error = user_path_walk_link(path, &nd);
	if (error)
		return error;
	error = mnt_want_write(nd.path.mnt);
	if (!error) {
		error = removexattr(nd.path.dentry, name);
		mnt_drop_write(nd.path.mnt);
	}
	path_put(&nd.path);
	return error;
}

asmlinkage long
sys_fremovexattr(int fd, char __user *name)
{
	struct file *f;
	struct dentry *dentry;
	int error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	dentry = f->f_path.dentry;
	audit_inode(NULL, dentry);
	error = mnt_want_write(f->f_path.mnt);
	if (!error) {
		error = removexattr(dentry, name);
		mnt_drop_write(f->f_path.mnt);
	}
	fput(f);
	return error;
}


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
 * prefix with the generic xattr API, a filesystem should create a
 * null-terminated array of struct xattr_handler (one for each prefix) and
 * hang a pointer to it off of the s_xattr field of the superblock.
 *
 * The generic_fooxattr() functions will use this list to dispatch xattr
 * operations to the correct xattr_handler.
 */
#define for_each_xattr_handler(handlers, handler)		\
		for ((handler) = *(handlers)++;			\
			(handler) != NULL;			\
			(handler) = *(handlers)++)

/*
 * Find the xattr_handler with the matching prefix.
 */
static struct xattr_handler *
xattr_resolve_name(struct xattr_handler **handlers, const char **name)
{
	struct xattr_handler *handler;

	if (!*name)
		return NULL;

	for_each_xattr_handler(handlers, handler) {
		const char *n = strcmp_prefix(*name, handler->prefix);
		if (n) {
			*name = n;
			break;
		}
	}
	return handler;
}

/*
 * Find the handler for the prefix and dispatch its get() operation.
 */
ssize_t
generic_getxattr(struct dentry *dentry, const char *name, void *buffer, size_t size)
{
	struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	handler = xattr_resolve_name(inode->i_sb->s_xattr, &name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->get(inode, name, buffer, size);
}

/*
 * Combine the results of the list() operation from every xattr_handler in the
 * list.
 */
ssize_t
generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *inode = dentry->d_inode;
	struct xattr_handler *handler, **handlers = inode->i_sb->s_xattr;
	unsigned int size = 0;

	if (!buffer) {
		for_each_xattr_handler(handlers, handler)
			size += handler->list(inode, NULL, 0, NULL, 0);
	} else {
		char *buf = buffer;

		for_each_xattr_handler(handlers, handler) {
			size = handler->list(inode, buf, buffer_size, NULL, 0);
			if (size > buffer_size)
				return -ERANGE;
			buf += size;
			buffer_size -= size;
		}
		size = buf - buffer;
	}
	return size;
}

/*
 * Find the handler for the prefix and dispatch its set() operation.
 */
int
generic_setxattr(struct dentry *dentry, const char *name, const void *value, size_t size, int flags)
{
	struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	if (size == 0)
		value = "";  /* empty EA, do not remove */
	handler = xattr_resolve_name(inode->i_sb->s_xattr, &name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->set(inode, name, value, size, flags);
}

/*
 * Find the handler for the prefix and dispatch its set() operation to remove
 * any associated extended attribute.
 */
int
generic_removexattr(struct dentry *dentry, const char *name)
{
	struct xattr_handler *handler;
	struct inode *inode = dentry->d_inode;

	handler = xattr_resolve_name(inode->i_sb->s_xattr, &name);
	if (!handler)
		return -EOPNOTSUPP;
	return handler->set(inode, name, NULL, 0, XATTR_REPLACE);
}

EXPORT_SYMBOL(generic_getxattr);
EXPORT_SYMBOL(generic_listxattr);
EXPORT_SYMBOL(generic_setxattr);
EXPORT_SYMBOL(generic_removexattr);
