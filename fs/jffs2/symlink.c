/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: symlink.c,v 1.19 2005/11/07 11:14:42 gleixner Exp $
 *
 */


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "nodelist.h"

static void *jffs2_follow_link(struct dentry *dentry, struct nameidata *nd);

struct inode_operations jffs2_symlink_inode_operations =
{
	.readlink =	generic_readlink,
	.follow_link =	jffs2_follow_link,
	.permission =	jffs2_permission,
	.setattr =	jffs2_setattr,
	.setxattr =	jffs2_setxattr,
	.getxattr =	jffs2_getxattr,
	.listxattr =	jffs2_listxattr,
	.removexattr =	jffs2_removexattr
};

static void *jffs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(dentry->d_inode);
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
		printk(KERN_ERR "jffs2_follow_link(): can't find symlink taerget\n");
		p = ERR_PTR(-EIO);
	}
	D1(printk(KERN_DEBUG "jffs2_follow_link(): target path is '%s'\n", (char *) f->target));

	nd_set_link(nd, p);

	/*
	 * We will unlock the f->sem mutex but VFS will use the f->target string. This is safe
	 * since the only way that may cause f->target to be changed is iput() operation.
	 * But VFS will not use f->target after iput() has been called.
	 */
	return NULL;
}

