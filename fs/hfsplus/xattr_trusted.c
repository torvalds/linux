/*
 * linux/fs/hfsplus/xattr_trusted.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handler for trusted extended attributes.
 */

#include <linux/nls.h>

#include "hfsplus_fs.h"
#include "xattr.h"

static int hfsplus_trusted_getxattr(const struct xattr_handler *handler,
				    struct dentry *dentry, const char *name,
				    void *buffer, size_t size)
{
	return hfsplus_getxattr(dentry, name, buffer, size,
				XATTR_TRUSTED_PREFIX,
				XATTR_TRUSTED_PREFIX_LEN);
}

static int hfsplus_trusted_setxattr(const struct xattr_handler *handler,
				    struct dentry *dentry, const char *name,
				    const void *buffer, size_t size, int flags)
{
	return hfsplus_setxattr(dentry, name, buffer, size, flags,
				XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN);
}

const struct xattr_handler hfsplus_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.get	= hfsplus_trusted_getxattr,
	.set	= hfsplus_trusted_setxattr,
};
