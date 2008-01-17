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
#include "ctree.h"
#include "xattr.h"
#ifndef is_owner_or_cap
#define is_owner_or_cap(inode)	\
	((current->fsuid == (inode)->i_uid) || capable(CAP_FOWNER))
#endif

static int btrfs_xattr_set_acl(struct inode *inode, int type,
			       const void *value, size_t size)
{
	int ret = 0;
	struct posix_acl *acl;

	if (!is_owner_or_cap(inode))
		return -EPERM;
	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (acl == NULL) {
			value = NULL;
			size = 0;
		} else if (IS_ERR(acl)) {
			ret = PTR_ERR(acl);
		} else {
			ret = posix_acl_valid(acl);
			posix_acl_release(acl);
		}
		if (ret)
			return ret;
	}
	return btrfs_xattr_set(inode, type, "", value, size, 0);
}

static int btrfs_xattr_get_acl(struct inode *inode, int type,
			       void *value, size_t size)
{
	return btrfs_xattr_get(inode, type, "", value, size);
}
static int btrfs_xattr_acl_access_get(struct inode *inode, const char *name,
				      void *value, size_t size)
{
	if (*name != '\0')
	       return -EINVAL;
	return btrfs_xattr_get_acl(inode, BTRFS_XATTR_INDEX_POSIX_ACL_ACCESS,
				   value, size);
}
static int btrfs_xattr_acl_access_set(struct inode *inode, const char *name,
				      const void *value, size_t size, int flags)
{
	if (*name != '\0')
	       return -EINVAL;
	return btrfs_xattr_set_acl(inode, BTRFS_XATTR_INDEX_POSIX_ACL_ACCESS,
				   value, size);
}
static int btrfs_xattr_acl_default_get(struct inode *inode, const char *name,
				       void *value, size_t size)
{
	if (*name != '\0')
	       return -EINVAL;
	return btrfs_xattr_get_acl(inode, BTRFS_XATTR_INDEX_POSIX_ACL_DEFAULT,
				   value, size);
}
static int btrfs_xattr_acl_default_set(struct inode *inode, const char *name,
				       const void *value, size_t size, int flags)
{
	if (*name != '\0')
	       return -EINVAL;
	return btrfs_xattr_set_acl(inode, BTRFS_XATTR_INDEX_POSIX_ACL_DEFAULT,
				   value, size);
}
struct xattr_handler btrfs_xattr_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.list	= btrfs_xattr_generic_list,
	.get	= btrfs_xattr_acl_default_get,
	.set	= btrfs_xattr_acl_default_set,
};

struct xattr_handler btrfs_xattr_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.list	= btrfs_xattr_generic_list,
	.get	= btrfs_xattr_acl_access_get,
	.set	= btrfs_xattr_acl_access_set,
};
