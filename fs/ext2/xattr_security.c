/*
 * linux/fs/ext2/xattr_security.c
 * Handler for storing security labels as extended attributes.
 */

#include "ext2.h"
#include <linux/security.h>
#include "xattr.h"

static int
ext2_xattr_security_get(const struct xattr_handler *handler,
			struct dentry *dentry, const char *name,
			void *buffer, size_t size)
{
	return ext2_xattr_get(d_inode(dentry), EXT2_XATTR_INDEX_SECURITY, name,
			      buffer, size);
}

static int
ext2_xattr_security_set(const struct xattr_handler *handler,
			struct dentry *dentry, const char *name,
			const void *value, size_t size, int flags)
{
	return ext2_xattr_set(d_inode(dentry), EXT2_XATTR_INDEX_SECURITY, name,
			      value, size, flags);
}

static int ext2_initxattrs(struct inode *inode, const struct xattr *xattr_array,
			   void *fs_info)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = ext2_xattr_set(inode, EXT2_XATTR_INDEX_SECURITY,
				     xattr->name, xattr->value,
				     xattr->value_len, 0);
		if (err < 0)
			break;
	}
	return err;
}

int
ext2_init_security(struct inode *inode, struct inode *dir,
		   const struct qstr *qstr)
{
	return security_inode_init_security(inode, dir, qstr,
					    &ext2_initxattrs, NULL);
}

const struct xattr_handler ext2_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= ext2_xattr_security_get,
	.set	= ext2_xattr_security_set,
};
