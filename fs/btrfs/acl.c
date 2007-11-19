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

/*
 * FIXME: At this point this is all place holder stuff, we just return
 * -EOPNOTSUPP so cp won't complain when it tries to copy over a file with an
 *  acl on it.
 */

static int btrfs_xattr_acl_access_get(struct inode *inode, const char *name,
				      void *value, size_t size)
{
	/*
	return btrfs_xattr_get(inode, BTRFS_XATTR_INDEX_POSIX_ACL_ACCESS, name,
			       value, size);
	*/
	return -EOPNOTSUPP;
}

static int btrfs_xattr_acl_access_set(struct inode *inode, const char *name,
				      const void *value, size_t size, int flags)
{
	/*
	return btrfs_xattr_set(inode, BTRFS_XATTR_INDEX_POSIX_ACL_ACCESS, name,
			       value, size, flags);
	*/
	return -EOPNOTSUPP;
}

static int btrfs_xattr_acl_default_get(struct inode *inode, const char *name,
				       void *value, size_t size)
{
	/*
	return btrfs_xattr_get(inode, BTRFS_XATTR_INDEX_POSIX_ACL_DEFAULT,
			       name, value, size);
	*/
	return -EOPNOTSUPP;
}

static int btrfs_xattr_acl_default_set(struct inode *inode, const char *name,
				       const void *value, size_t size, int flags)
{
	/*
	return btrfs_xattr_set(inode, BTRFS_XATTR_INDEX_POSIX_ACL_DEFAULT,
			       name, value, size, flags);
	*/
	return -EOPNOTSUPP;
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
