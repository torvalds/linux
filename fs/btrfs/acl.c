/*
 * Copyright (C) 2007 Red Hat.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/posix_acl.h>
#include <linux/sched.h>

#include "ctree.h"
#include "btrfs_inode.h"
#include "xattr.h"

#ifdef CONFIG_BTRFS_POSIX_ACL

static struct posix_acl *btrfs_get_acl(struct inode *inode, int type)
{
	int size;
	const char *name;
	char *value = NULL;
	struct posix_acl *acl;

	acl = get_cached_acl(inode, type);
	if (acl != ACL_NOT_CACHED)
		return acl;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}

	size = __btrfs_getxattr(inode, name, "", 0);
	if (size > 0) {
		value = kzalloc(size, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __btrfs_getxattr(inode, name, value, size);
		if (size > 0) {
			acl = posix_acl_from_xattr(value, size);
			set_cached_acl(inode, type, acl);
		}
		kfree(value);
	} else if (size == -ENOENT || size == -ENODATA || size == 0) {
		/* FIXME, who returns -ENOENT?  I think nobody */
		acl = NULL;
		set_cached_acl(inode, type, acl);
	} else {
		acl = ERR_PTR(-EIO);
	}

	return acl;
}

static int btrfs_xattr_get_acl(struct inode *inode, int type,
			       void *value, size_t size)
{
	struct posix_acl *acl;
	int ret = 0;

	acl = btrfs_get_acl(inode, type);

	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	ret = posix_acl_to_xattr(acl, value, size);
	posix_acl_release(acl);

	return ret;
}

/*
 * Needs to be called with fs_mutex held
 */
static int btrfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int ret, size = 0;
	const char *name;
	char *value = NULL;
	mode_t mode;

	if (acl) {
		ret = posix_acl_valid(acl);
		if (ret < 0)
			return ret;
		ret = 0;
	}

	switch (type) {
	case ACL_TYPE_ACCESS:
		mode = inode->i_mode;
		ret = posix_acl_equiv_mode(acl, &mode);
		if (ret < 0)
			return ret;
		ret = 0;
		inode->i_mode = mode;
		name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EINVAL : 0;
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		value = kmalloc(size, GFP_NOFS);
		if (!value) {
			ret = -ENOMEM;
			goto out;
		}

		ret = posix_acl_to_xattr(acl, value, size);
		if (ret < 0)
			goto out;
	}

	ret = __btrfs_setxattr(inode, name, value, size, 0);

out:
	kfree(value);

	if (!ret)
		set_cached_acl(inode, type, acl);

	return ret;
}

static int btrfs_xattr_set_acl(struct inode *inode, int type,
			       const void *value, size_t size)
{
	int ret = 0;
	struct posix_acl *acl = NULL;

	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (acl == NULL) {
			value = NULL;
			size = 0;
		} else if (IS_ERR(acl)) {
			return PTR_ERR(acl);
		}
	}

	ret = btrfs_set_acl(inode, acl, type);

	posix_acl_release(acl);

	return ret;
}


static int btrfs_xattr_acl_access_get(struct inode *inode, const char *name,
				      void *value, size_t size)
{
	return btrfs_xattr_get_acl(inode, ACL_TYPE_ACCESS, value, size);
}

static int btrfs_xattr_acl_access_set(struct inode *inode, const char *name,
				      const void *value, size_t size, int flags)
{
	return btrfs_xattr_set_acl(inode, ACL_TYPE_ACCESS, value, size);
}

static int btrfs_xattr_acl_default_get(struct inode *inode, const char *name,
				       void *value, size_t size)
{
	return btrfs_xattr_get_acl(inode, ACL_TYPE_DEFAULT, value, size);
}

static int btrfs_xattr_acl_default_set(struct inode *inode, const char *name,
			       const void *value, size_t size, int flags)
{
	return btrfs_xattr_set_acl(inode, ACL_TYPE_DEFAULT, value, size);
}

int btrfs_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl;
	int error = -EAGAIN;

	acl = btrfs_get_acl(inode, ACL_TYPE_ACCESS);

	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
	}

	return error;
}

/*
 * btrfs_init_acl is already generally called under fs_mutex, so the locking
 * stuff has been fixed to work with that.  If the locking stuff changes, we
 * need to re-evaluate the acl locking stuff.
 */
int btrfs_init_acl(struct inode *inode, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	int ret = 0;

	/* this happens with subvols */
	if (!dir)
		return 0;

	if (!S_ISLNK(inode->i_mode)) {
		if (IS_POSIXACL(dir)) {
			acl = btrfs_get_acl(dir, ACL_TYPE_DEFAULT);
			if (IS_ERR(acl))
				return PTR_ERR(acl);
		}

		if (!acl)
			inode->i_mode &= ~current_umask();
	}

	if (IS_POSIXACL(dir) && acl) {
		struct posix_acl *clone;
		mode_t mode;

		if (S_ISDIR(inode->i_mode)) {
			ret = btrfs_set_acl(inode, acl, ACL_TYPE_DEFAULT);
			if (ret)
				goto failed;
		}
		clone = posix_acl_clone(acl, GFP_NOFS);
		ret = -ENOMEM;
		if (!clone)
			goto failed;

		mode = inode->i_mode;
		ret = posix_acl_create_masq(clone, &mode);
		if (ret >= 0) {
			inode->i_mode = mode;
			if (ret > 0) {
				/* we need an acl */
				ret = btrfs_set_acl(inode, clone,
						    ACL_TYPE_ACCESS);
			}
		}
	}
failed:
	posix_acl_release(acl);

	return ret;
}

int btrfs_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int ret = 0;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (!IS_POSIXACL(inode))
		return 0;

	acl = btrfs_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);

	clone = posix_acl_clone(acl, GFP_KERNEL);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;

	ret = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!ret)
		ret = btrfs_set_acl(inode, clone, ACL_TYPE_ACCESS);

	posix_acl_release(clone);

	return ret;
}

struct xattr_handler btrfs_xattr_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.get	= btrfs_xattr_acl_default_get,
	.set	= btrfs_xattr_acl_default_set,
};

struct xattr_handler btrfs_xattr_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.get	= btrfs_xattr_acl_access_get,
	.set	= btrfs_xattr_acl_access_set,
};

#else /* CONFIG_BTRFS_POSIX_ACL */

int btrfs_acl_chmod(struct inode *inode)
{
	return 0;
}

int btrfs_init_acl(struct inode *inode, struct inode *dir)
{
	return 0;
}

#endif /* CONFIG_BTRFS_POSIX_ACL */
