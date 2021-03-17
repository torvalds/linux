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

static int jffs2_user_getxattr(const struct xattr_handler *handler,
			       struct dentry *unused, struct inode *inode,
			       const char *name, void *buffer, size_t size,
			       int flags)
{
	return do_jffs2_getxattr(inode, JFFS2_XPREFIX_USER,
				 name, buffer, size);
}

static int jffs2_user_setxattr(const struct xattr_handler *handler,
			       struct dentry *unused, struct inode *inode,
			       const char *name, const void *buffer,
			       size_t size, int flags)
{
	return do_jffs2_setxattr(inode, JFFS2_XPREFIX_USER,
				 name, buffer, size, flags);
}

const struct xattr_handler jffs2_user_xattr_handler = {
	.prefix = XATTR_USER_PREFIX,
	.set = jffs2_user_setxattr,
	.get = jffs2_user_getxattr
};
