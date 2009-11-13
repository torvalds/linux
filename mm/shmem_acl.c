/*
 * mm/shmem_acl.c
 *
 * (C) 2005 Andreas Gruenbacher <agruen@suse.de>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/shmem_fs.h>
#include <linux/xattr.h>
#include <linux/generic_acl.h>

/**
 * shmem_get_acl  -   generic_acl_operations->getacl() operation
 */
static struct posix_acl *
shmem_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl = NULL;

	spin_lock(&inode->i_lock);
	switch(type) {
		case ACL_TYPE_ACCESS:
			acl = posix_acl_dup(inode->i_acl);
			break;

		case ACL_TYPE_DEFAULT:
			acl = posix_acl_dup(inode->i_default_acl);
			break;
	}
	spin_unlock(&inode->i_lock);

	return acl;
}

/**
 * shmem_set_acl  -   generic_acl_operations->setacl() operation
 */
static void
shmem_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	struct posix_acl *free = NULL;

	spin_lock(&inode->i_lock);
	switch(type) {
		case ACL_TYPE_ACCESS:
			free = inode->i_acl;
			inode->i_acl = posix_acl_dup(acl);
			break;

		case ACL_TYPE_DEFAULT:
			free = inode->i_default_acl;
			inode->i_default_acl = posix_acl_dup(acl);
			break;
	}
	spin_unlock(&inode->i_lock);
	posix_acl_release(free);
}

struct generic_acl_operations shmem_acl_ops = {
	.getacl = shmem_get_acl,
	.setacl = shmem_set_acl,
};

static size_t
shmem_xattr_list_acl(struct dentry *dentry, char *list, size_t list_size,
		const char *name, size_t name_len, int type)
{
	return generic_acl_list(dentry->d_inode, &shmem_acl_ops,
				type, list, list_size);
}

static int
shmem_xattr_get_acl(struct dentry *dentry, const char *name, void *buffer,
		     size_t size, int type)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_get(dentry->d_inode, &shmem_acl_ops, type,
			       buffer, size);
}

static int
shmem_xattr_set_acl(struct dentry *dentry, const char *name, const void *value,
		     size_t size, int flags, int type)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_set(dentry->d_inode, &shmem_acl_ops, type,
			       value, size);
}

struct xattr_handler shmem_xattr_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.flags	= ACL_TYPE_ACCESS,
	.list	= shmem_xattr_list_acl,
	.get	= shmem_xattr_get_acl,
	.set	= shmem_xattr_set_acl,
};

struct xattr_handler shmem_xattr_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.flags	= ACL_TYPE_DEFAULT,
	.list	= shmem_xattr_list_acl,
	.get	= shmem_xattr_get_acl,
	.set	= shmem_xattr_set_acl,
};

/**
 * shmem_acl_init  -  Inizialize the acl(s) of a new inode
 */
int
shmem_acl_init(struct inode *inode, struct inode *dir)
{
	return generic_acl_init(inode, dir, &shmem_acl_ops);
}

/**
 * shmem_check_acl  -  check_acl() callback for generic_permission()
 */
int
shmem_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl = shmem_get_acl(inode, ACL_TYPE_ACCESS);

	if (acl) {
		int error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return error;
	}
	return -EAGAIN;
}
