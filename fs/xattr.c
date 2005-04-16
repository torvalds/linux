/*
  File: fs/xattr.c

  Extended attribute handling.

  Copyright (C) 2001 by Andreas Gruenbacher <a.gruenbacher@computer.org>
  Copyright (C) 2001 SGI - Silicon Graphics, Inc <linux-xfs@oss.sgi.com>
  Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <asm/uaccess.h>

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

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->setxattr) {
		down(&d->d_inode->i_sem);
		error = security_inode_setxattr(d, kname, kvalue, size, flags);
		if (error)
			goto out;
		error = d->d_inode->i_op->setxattr(d, kname, kvalue, size, flags);
		if (!error)
			security_inode_post_setxattr(d, kname, kvalue, size, flags);
out:
		up(&d->d_inode->i_sem);
	}
	if (kvalue)
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
	error = setxattr(nd.dentry, name, value, size, flags);
	path_release(&nd);
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
	error = setxattr(nd.dentry, name, value, size, flags);
	path_release(&nd);
	return error;
}

asmlinkage long
sys_fsetxattr(int fd, char __user *name, void __user *value,
	      size_t size, int flags)
{
	struct file *f;
	int error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	error = setxattr(f->f_dentry, name, value, size, flags);
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
		kvalue = kmalloc(size, GFP_KERNEL);
		if (!kvalue)
			return -ENOMEM;
	}

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->getxattr) {
		error = security_inode_getxattr(d, kname);
		if (error)
			goto out;
		error = d->d_inode->i_op->getxattr(d, kname, kvalue, size);
		if (error > 0) {
			if (size && copy_to_user(value, kvalue, error))
				error = -EFAULT;
		} else if (error == -ERANGE && size >= XATTR_SIZE_MAX) {
			/* The file system tried to returned a value bigger
			   than XATTR_SIZE_MAX bytes. Not possible. */
			error = -E2BIG;
		}
	}
out:
	if (kvalue)
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
	error = getxattr(nd.dentry, name, value, size);
	path_release(&nd);
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
	error = getxattr(nd.dentry, name, value, size);
	path_release(&nd);
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
	error = getxattr(f->f_dentry, name, value, size);
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

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->listxattr) {
		error = security_inode_listxattr(d);
		if (error)
			goto out;
		error = d->d_inode->i_op->listxattr(d, klist, size);
		if (error > 0) {
			if (size && copy_to_user(list, klist, error))
				error = -EFAULT;
		} else if (error == -ERANGE && size >= XATTR_LIST_MAX) {
			/* The file system tried to returned a list bigger
			   than XATTR_LIST_MAX bytes. Not possible. */
			error = -E2BIG;
		}
	}
out:
	if (klist)
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
	error = listxattr(nd.dentry, list, size);
	path_release(&nd);
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
	error = listxattr(nd.dentry, list, size);
	path_release(&nd);
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
	error = listxattr(f->f_dentry, list, size);
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

	error = -EOPNOTSUPP;
	if (d->d_inode->i_op && d->d_inode->i_op->removexattr) {
		error = security_inode_removexattr(d, kname);
		if (error)
			goto out;
		down(&d->d_inode->i_sem);
		error = d->d_inode->i_op->removexattr(d, kname);
		up(&d->d_inode->i_sem);
	}
out:
	return error;
}

asmlinkage long
sys_removexattr(char __user *path, char __user *name)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (error)
		return error;
	error = removexattr(nd.dentry, name);
	path_release(&nd);
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
	error = removexattr(nd.dentry, name);
	path_release(&nd);
	return error;
}

asmlinkage long
sys_fremovexattr(int fd, char __user *name)
{
	struct file *f;
	int error = -EBADF;

	f = fget(fd);
	if (!f)
		return error;
	error = removexattr(f->f_dentry, name);
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
