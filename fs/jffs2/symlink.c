/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "nodelist.h"

static void *jffs2_follow_link(struct dentry *dentry, struct nameidata *nd);

const struct inode_operations jffs2_symlink_inode_operations =
{
	.readlink =	generic_readlink,
	.follow_link =	jffs2_follow_link,
	.setattr =	jffs2_setattr,
	.setxattr =	jffs2_setxattr,
	.getxattr =	jffs2_getxattr,
	.listxattr =	jffs2_listxattr,
	.removexattr =	jffs2_removexattr
};

static void *jffs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(d_inode(dentry));
	char *p = (char *)f->target;

	/*
	 * We don't acquire the f->sem mutex here since the only data we
	 * use is f->target.
	 *
	 * 1. If we are here the inode has already built and f->target has
	 * to point to the target path.
	 * 2. Nobody uses f->target (if the inode is symlink's inode). The
	 * exception is inode freeing function which frees f->target. But
	 * it can't be called while we are here and before VFS has
	 * stopped using our f->target string which we provide by means of
	 * nd_set_link() call.
	 */

	if (!p) {
		pr_err("%s(): can't find symlink target\n", __func__);
		p = ERR_PTR(-EIO);
	}
	jffs2_dbg(1, "%s(): target path is '%s'\n",
		  __func__, (char *)f->target);

	nd_set_link(nd, p);

	/*
	 * We will unlock the f->sem mutex but VFS will use the f->target string. This is safe
	 * since the only way that may cause f->target to be changed is iput() operation.
	 * But VFS will not use f->target after iput() has been called.
	 */
	return NULL;
}

