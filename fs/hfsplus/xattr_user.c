/*
 * linux/fs/hfsplus/xattr_user.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handler for user extended attributes.
 */

#include "hfsplus_fs.h"
#include "xattr.h"

static int hfsplus_user_getxattr(struct dentry *dentry, const char *name,
					void *buffer, size_t size, int type)
{
	char xattr_name[HFSPLUS_ATTR_MAX_STRLEN + 1] = {0};
	size_t len = strlen(name);

	if (!strcmp(name, ""))
		return -EINVAL;

	if (len + XATTR_USER_PREFIX_LEN > HFSPLUS_ATTR_MAX_STRLEN)
		return -EOPNOTSUPP;

	strcpy(xattr_name, XATTR_USER_PREFIX);
	strcpy(xattr_name + XATTR_USER_PREFIX_LEN, name);

	return hfsplus_getxattr(dentry, xattr_name, buffer, size);
}

static int hfsplus_user_setxattr(struct dentry *dentry, const char *name,
		const void *buffer, size_t size, int flags, int type)
{
	char xattr_name[HFSPLUS_ATTR_MAX_STRLEN + 1] = {0};
	size_t len = strlen(name);

	if (!strcmp(name, ""))
		return -EINVAL;

	if (len + XATTR_USER_PREFIX_LEN > HFSPLUS_ATTR_MAX_STRLEN)
		return -EOPNOTSUPP;

	strcpy(xattr_name, XATTR_USER_PREFIX);
	strcpy(xattr_name + XATTR_USER_PREFIX_LEN, name);

	return hfsplus_setxattr(dentry, xattr_name, buffer, size, flags);
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
