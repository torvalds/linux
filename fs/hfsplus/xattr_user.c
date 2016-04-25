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
				 struct dentry *dentry, const char *name,
				 void *buffer, size_t size)
{

	return hfsplus_getxattr(dentry, name, buffer, size,
				XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

static int hfsplus_user_setxattr(const struct xattr_handler *handler,
				 struct dentry *dentry, const char *name,
				 const void *buffer, size_t size, int flags)
{
	return hfsplus_setxattr(dentry, name, buffer, size, flags,
				XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

const struct xattr_handler hfsplus_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= hfsplus_user_getxattr,
	.set	= hfsplus_user_setxattr,
};
