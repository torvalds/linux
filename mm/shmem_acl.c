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

/**
 * shmem_list_acl_access, shmem_get_acl_access, shmem_set_acl_access,
 * shmem_xattr_acl_access_handler  -  plumbing code to implement the
 * system.posix_acl_access xattr using the generic acl functions.
 */

static size_t
shmem_list_acl_access(struct inode *inode, char *list, size_t list_size,
		      const char *name, size_t name_len)
{
	return generic_acl_list(inode, &shmem_acl_ops, ACL_TYPE_ACCESS,
				list, list_size);
}

static int
shmem_get_acl_access(struct inode *inode, const char *name, void *buffer,
		     size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_get(inode, &shmem_acl_ops, ACL_TYPE_ACCESS, buffer,
			       size);
}

static int
shmem_set_acl_access(struct inode *inode, const char *name, const void *value,
		     size_t size, int flags)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_set(inode, &shmem_acl_ops, ACL_TYPE_ACCESS, value,
			       size);
}

struct xattr_handler shmem_xattr_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.list	= shmem_list_acl_access,
	.get	= shmem_get_acl_access,
	.set	= shmem_set_acl_access,
};

/**
 * shmem_list_acl_default, shmem_get_acl_default, shmem_set_acl_default,
 * shmem_xattr_acl_default_handler  -  plumbing code to implement the
 * system.posix_acl_default xattr using the generic acl functions.
 */

static size_t
shmem_list_acl_default(struct inode *inode, char *list, size_t list_size,
		       const char *name, size_t name_len)
{
	return generic_acl_list(inode, &shmem_acl_ops, ACL_TYPE_DEFAULT,
				list, list_size);
}

static int
shmem_get_acl_default(struct inode *inode, const char *name, void *buffer,
		      size_t size)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_get(inode, &shmem_acl_ops, ACL_TYPE_DEFAULT, buffer,
			       size);
}

static int
shmem_set_acl_default(struct inode *inode, const char *name, const void *value,
		      size_t size, int flags)
{
	if (strcmp(name, "") != 0)
		return -EINVAL;
	return generic_acl_set(inode, &shmem_acl_ops, ACL_TYPE_DEFAULT, value,
			       size);
}

struct xattr_handler shmem_xattr_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.list	= shmem_list_acl_default,
	.get	= shmem_get_acl_default,
	.set	= shmem_set_acl_default,
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
static int
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

/**
 * shmem_permission  -  permission() inode operation
 */
int
shmem_permission(struct inode *inode, int mask)
{
	return generic_permission(inode, mask, shmem_check_acl);
}
