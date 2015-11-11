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

static int hfsplus_user_getxattr(struct dentry *dentry, const char *name,
					void *buffer, size_t size, int type)
{

	return hfsplus_getxattr(dentry, name, buffer, size,
				XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

static int hfsplus_user_setxattr(struct dentry *dentry, const char *name,
		const void *buffer, size_t size, int flags, int type)
{
	return hfsplus_setxattr(dentry, name, buffer, size, flags,
				XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
}

static size_t hfsplus_user_listxattr(struct dentry *dentry, char *list,
		size_t list_size, const char *name, size_t name_len, int type)
{
	/*
	 * This method is not used.
	 * It is used hfsplus_listxattr() instead of generic_listxattr().
	 */
	return -EOPNOTSUPP;
}

const struct xattr_handler hfsplus_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= hfsplus_user_listxattr,
	.get	= hfsplus_user_getxattr,
	.set	= hfsplus_user_setxattr,
};
