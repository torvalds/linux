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
#include <linux/fs.h>
#include <linux/jffs2.h>
#include <linux/xattr.h>
#include <linux/mtd/mtd.h>
#include "nodelist.h"

static int jffs2_trusted_getxattr(const struct xattr_handler *handler,
				  struct dentry *dentry, const char *name,
				  void *buffer, size_t size)
{
	if (!strcmp(name, ""))
		return -EINVAL;
	return do_jffs2_getxattr(d_inode(dentry), JFFS2_XPREFIX_TRUSTED,
				 name, buffer, size);
}

static int jffs2_trusted_setxattr(const struct xattr_handler *handler,
				  struct dentry *dentry, const char *name,
				  const void *buffer, size_t size, int flags)
{
	if (!strcmp(name, ""))
		return -EINVAL;
	return do_jffs2_setxattr(d_inode(dentry), JFFS2_XPREFIX_TRUSTED,
				 name, buffer, size, flags);
}

static size_t jffs2_trusted_listxattr(const struct xattr_handler *handler,
				      struct dentry *dentry, char *list,
				      size_t list_size, const char *name,
				      size_t name_len)
{
	size_t retlen = XATTR_TRUSTED_PREFIX_LEN + name_len + 1;

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	if (list && retlen<=list_size) {
		strcpy(list, XATTR_TRUSTED_PREFIX);
		strcpy(list + XATTR_TRUSTED_PREFIX_LEN, name);
	}

	return retlen;
}

const struct xattr_handler jffs2_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.list = jffs2_trusted_listxattr,
	.set = jffs2_trusted_setxattr,
	.get = jffs2_trusted_getxattr
};
