/*
 * linux/fs/ext3/xattr_security.c
 * Handler for storing security labels as extended attributes.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include <linux/security.h>
#include "xattr.h"

static size_t
ext3_xattr_security_list(struct dentry *dentry, char *list, size_t list_size,
			 const char *name, size_t name_len, int type)
{
	const size_t prefix_len = XATTR_SECURITY_PREFIX_LEN;
	const size_t total_len = prefix_len + name_len + 1;


	if (list && total_len <= list_size) {
		memcpy(list, XATTR_SECURITY_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return total_len;
}

static int
ext3_xattr_security_get(struct dentry *dentry, const char *name,
		void *buffer, size_t size, int type)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext3_xattr_get(dentry->d_inode, EXT3_XATTR_INDEX_SECURITY,
			      name, buffer, size);
}

static int
ext3_xattr_security_set(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags, int type)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext3_xattr_set(dentry->d_inode, EXT3_XATTR_INDEX_SECURITY,
			      name, value, size, flags);
}

int ext3_initxattrs(struct inode *inode, const struct xattr *xattr_array,
		    void *fs_info)
{
	const struct xattr *xattr;
	handle_t *handle = fs_info;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = ext3_xattr_set_handle(handle, inode,
					    EXT3_XATTR_INDEX_SECURITY,
					    xattr->name, xattr->value,
					    xattr->value_len, 0);
		if (err < 0)
			break;
	}
	return err;
}

int
ext3_init_security(handle_t *handle, struct inode *inode, struct inode *dir,
		   const struct qstr *qstr)
{
	return security_inode_init_security(inode, dir, qstr,
					    &ext3_initxattrs, handle);
}

const struct xattr_handler ext3_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= ext3_xattr_security_list,
	.get	= ext3_xattr_security_get,
	.set	= ext3_xattr_security_set,
};
