/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2006  NEC Corporation
 *
 * Created by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/crc32.h>
#include <linux/jffs2.h>
#include <linux/xattr.h>
#include <linux/mtd/mtd.h>
#include <linux/security.h>
#include "nodelist.h"

/* ---- Initial Security Label(s) Attachment callback --- */
int jffs2_initxattrs(struct inode *inode, const struct xattr *xattr_array,
		     void *fs_info)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = do_jffs2_setxattr(inode, JFFS2_XPREFIX_SECURITY,
					xattr->name, xattr->value,
					xattr->value_len, 0);
		if (err < 0)
			break;
	}
	return err;
}

/* ---- Initial Security Label(s) Attachment ----------- */
int jffs2_init_security(struct inode *inode, struct inode *dir,
			const struct qstr *qstr)
{
	return security_inode_init_security(inode, dir, qstr,
					    &jffs2_initxattrs, NULL);
}

/* ---- XATTR Handler for "security.*" ----------------- */
static int jffs2_security_getxattr(struct dentry *dentry, const char *name,
				   void *buffer, size_t size, int type)
{
	if (!strcmp(name, ""))
		return -EINVAL;

	return do_jffs2_getxattr(dentry->d_inode, JFFS2_XPREFIX_SECURITY,
				 name, buffer, size);
}

static int jffs2_security_setxattr(struct dentry *dentry, const char *name,
		const void *buffer, size_t size, int flags, int type)
{
	if (!strcmp(name, ""))
		return -EINVAL;

	return do_jffs2_setxattr(dentry->d_inode, JFFS2_XPREFIX_SECURITY,
				 name, buffer, size, flags);
}

static size_t jffs2_security_listxattr(struct dentry *dentry, char *list,
		size_t list_size, const char *name, size_t name_len, int type)
{
	size_t retlen = XATTR_SECURITY_PREFIX_LEN + name_len + 1;

	if (list && retlen <= list_size) {
		strcpy(list, XATTR_SECURITY_PREFIX);
		strcpy(list + XATTR_SECURITY_PREFIX_LEN, name);
	}

	return retlen;
}

const struct xattr_handler jffs2_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.list = jffs2_security_listxattr,
	.set = jffs2_security_setxattr,
	.get = jffs2_security_getxattr
};
