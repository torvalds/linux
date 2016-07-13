/*
 * linux/fs/hfsplus/xattr_trusted.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handler for storing security labels as extended attributes.
 */

#include <linux/security.h>
#include <linux/nls.h>

#include "hfsplus_fs.h"
#include "xattr.h"
#include "acl.h"

static int hfsplus_security_getxattr(const struct xattr_handler *handler,
				     struct dentry *unused, struct inode *inode,
				     const char *name, void *buffer, size_t size)
{
	return hfsplus_getxattr(inode, name, buffer, size,
				XATTR_SECURITY_PREFIX,
				XATTR_SECURITY_PREFIX_LEN);
}

static int hfsplus_security_setxattr(const struct xattr_handler *handler,
				     struct dentry *unused, struct inode *inode,
				     const char *name, const void *buffer,
				     size_t size, int flags)
{
	return hfsplus_setxattr(inode, name, buffer, size, flags,
				XATTR_SECURITY_PREFIX,
				XATTR_SECURITY_PREFIX_LEN);
}

static int hfsplus_initxattrs(struct inode *inode,
				const struct xattr *xattr_array,
				void *fs_info)
{
	const struct xattr *xattr;
	char *xattr_name;
	int err = 0;

	xattr_name = kmalloc(NLS_MAX_CHARSET_SIZE * HFSPLUS_ATTR_MAX_STRLEN + 1,
		GFP_KERNEL);
	if (!xattr_name)
		return -ENOMEM;
	for (xattr = xattr_array; xattr->name != NULL; xattr++) {

		if (!strcmp(xattr->name, ""))
			continue;

		strcpy(xattr_name, XATTR_SECURITY_PREFIX);
		strcpy(xattr_name +
			XATTR_SECURITY_PREFIX_LEN, xattr->name);
		memset(xattr_name +
			XATTR_SECURITY_PREFIX_LEN + strlen(xattr->name), 0, 1);

		err = __hfsplus_setxattr(inode, xattr_name,
					xattr->value, xattr->value_len, 0);
		if (err)
			break;
	}
	kfree(xattr_name);
	return err;
}

int hfsplus_init_security(struct inode *inode, struct inode *dir,
				const struct qstr *qstr)
{
	return security_inode_init_security(inode, dir, qstr,
					&hfsplus_initxattrs, NULL);
}

int hfsplus_init_inode_security(struct inode *inode,
						struct inode *dir,
						const struct qstr *qstr)
{
	int err;

	err = hfsplus_init_posix_acl(inode, dir);
	if (!err)
		err = hfsplus_init_security(inode, dir, qstr);
	return err;
}

const struct xattr_handler hfsplus_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= hfsplus_security_getxattr,
	.set	= hfsplus_security_setxattr,
};
