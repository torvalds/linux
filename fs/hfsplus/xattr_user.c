// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/hfsplus/xattr_user.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handler for user extended attributes.
 */

#include <linux/nls.h>

#include "hfsplus_fs.h"
#include "xattr.h"

static int hfsplus_user_getxattr(const struct xattr_handler *handler,
				 struct dentry *unused, struct inode *inode,
				 const char *name, void *buffer, size_t size)
{

	return hfsplus_getxattr(inode, name, buffer, size,
				XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

static int hfsplus_user_setxattr(const struct xattr_handler *handler,
				 struct mnt_idmap *idmap,
				 struct dentry *unused, struct inode *inode,
				 const char *name, const void *buffer,
				 size_t size, int flags)
{
	return hfsplus_setxattr(inode, name, buffer, size, flags,
				XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

const struct xattr_handler hfsplus_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= hfsplus_user_getxattr,
	.set	= hfsplus_user_setxattr,
};
