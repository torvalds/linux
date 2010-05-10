/*
 * (C) 2005 Andreas Gruenbacher <agruen@suse.de>
 *
 * This file is released under the GPL.
 *
 * Generic ACL support for in-memory filesystems.
 */

#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/generic_acl.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>


static size_t
generic_acl_list(struct dentry *dentry, char *list, size_t list_size,
		const char *name, size_t name_len, int type)
{
	struct posix_acl *acl;
	const char *xname;
	size_t size;

	acl = get_cached_acl(dentry->d_inode, type);
	if (!acl)
		return 0;
	posix_acl_release(acl);

	switch (type) {
	case ACL_TYPE_ACCESS:
		xname = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		xname = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		return 0;
	}
	size = strlen(xname) + 1;
	if (list && size <= list_size)
		memcpy(list, xname, size);
	return size;
}

static int
generic_acl_get(struct dentry *dentry, const char *name, void *buffer,
		     size_t size, int type)
{
	struct posix_acl *acl;
	int error;

	if (strcmp(name, "") != 0)
		return -EINVAL;

	acl = get_cached_acl(dentry->d_inode, type);
	if (!acl)
		return -ENODATA;
	error = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return error;
}

static int
generic_acl_set(struct dentry *dentry, const char *name, const void *value,
		     size_t size, int flags, int type)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl = NULL;
	int error;

	if (strcmp(name, "") != 0)
		return -EINVAL;
	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	if (!is_owner_or_cap(inode))
		return -EPERM;
	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	if (acl) {
		mode_t mode;

		error = posix_acl_valid(acl);
		if (error)
			goto failed;
		switch (type) {
		case ACL_TYPE_ACCESS:
			mode = inode->i_mode;
			error = posix_acl_equiv_mode(acl, &mode);
			if (error < 0)
				goto failed;
			inode->i_mode = mode;
			if (error == 0) {
				posix_acl_release(acl);
				acl = NULL;
			}
			break;
		case ACL_TYPE_DEFAULT:
			if (!S_ISDIR(inode->i_mode)) {
				error = -EINVAL;
				goto failed;
			}
			break;
		}
	}
	set_cached_acl(inode, type, acl);
	error = 0;
failed:
	posix_acl_release(acl);
	return error;
}

/**
 * generic_acl_init  -  Take care of acl inheritance at @inode create time
 *
 * Files created inside a directory with a default ACL inherit the
 * directory's default ACL.
 */
int
generic_acl_init(struct inode *inode, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	mode_t mode = inode->i_mode;
	int error;

	inode->i_mode = mode & ~current_umask();
	if (!S_ISLNK(inode->i_mode))
		acl = get_cached_acl(dir, ACL_TYPE_DEFAULT);
	if (acl) {
		struct posix_acl *clone;

		if (S_ISDIR(inode->i_mode)) {
			clone = posix_acl_clone(acl, GFP_KERNEL);
			error = -ENOMEM;
			if (!clone)
				goto cleanup;
			set_cached_acl(inode, ACL_TYPE_DEFAULT, clone);
			posix_acl_release(clone);
		}
		clone = posix_acl_clone(acl, GFP_KERNEL);
		error = -ENOMEM;
		if (!clone)
			goto cleanup;
		error = posix_acl_create_masq(clone, &mode);
		if (error >= 0) {
			inode->i_mode = mode;
			if (error > 0)
				set_cached_acl(inode, ACL_TYPE_ACCESS, clone);
		}
		posix_acl_release(clone);
	}
	error = 0;

cleanup:
	posix_acl_release(acl);
	return error;
}

/**
 * generic_acl_chmod  -  change the access acl of @inode upon chmod()
 *
 * A chmod also changes the permissions of the owner, group/mask, and
 * other ACL entries.
 */
int
generic_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int error = 0;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	acl = get_cached_acl(inode, ACL_TYPE_ACCESS);
	if (acl) {
		clone = posix_acl_clone(acl, GFP_KERNEL);
		posix_acl_release(acl);
		if (!clone)
			return -ENOMEM;
		error = posix_acl_chmod_masq(clone, inode->i_mode);
		if (!error)
			set_cached_acl(inode, ACL_TYPE_ACCESS, clone);
		posix_acl_release(clone);
	}
	return error;
}

int
generic_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl = get_cached_acl(inode, ACL_TYPE_ACCESS);

	if (acl) {
		int error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return error;
	}
	return -EAGAIN;
}

struct xattr_handler generic_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.flags	= ACL_TYPE_ACCESS,
	.list	= generic_acl_list,
	.get	= generic_acl_get,
	.set	= generic_acl_set,
};

struct xattr_handler generic_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.flags	= ACL_TYPE_DEFAULT,
	.list	= generic_acl_list,
	.get	= generic_acl_get,
	.set	= generic_acl_set,
};
