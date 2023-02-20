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
				  struct dentry *unused, struct inode *inode,
				  const char *name, void *buffer, size_t size)
{
	return do_jffs2_getxattr(inode, JFFS2_XPREFIX_TRUSTED,
				 name, buffer, size);
}

static int jffs2_trusted_setxattr(const struct xattr_handler *handler,
				  struct mnt_idmap *idmap,
				  struct dentry *unused, struct inode *inode,
				  const char *name, const void *buffer,
				  size_t size, int flags)
{
	return do_jffs2_setxattr(inode, JFFS2_XPREFIX_TRUSTED,
				 name, buffer, size, flags);
}

static bool jffs2_trusted_listxattr(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
}

const struct xattr_handler jffs2_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.list = jffs2_trusted_listxattr,
	.set = jffs2_trusted_setxattr,
	.get = jffs2_trusted_getxattr
};
