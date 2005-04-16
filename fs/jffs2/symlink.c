/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: symlink.c,v 1.14 2004/11/16 20:36:12 dwmw2 Exp $
 *
 */


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "nodelist.h"

static int jffs2_follow_link(struct dentry *dentry, struct nameidata *nd);
static void jffs2_put_link(struct dentry *dentry, struct nameidata *nd);

struct inode_operations jffs2_symlink_inode_operations =
{	
	.readlink =	generic_readlink,
	.follow_link =	jffs2_follow_link,
	.put_link =	jffs2_put_link,
	.setattr =	jffs2_setattr
};

static int jffs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	unsigned char *buf;
	buf = jffs2_getlink(JFFS2_SB_INFO(dentry->d_inode->i_sb), JFFS2_INODE_INFO(dentry->d_inode));
	nd_set_link(nd, buf);
	return 0;
}

static void jffs2_put_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s = nd_get_link(nd);
	if (!IS_ERR(s))
		kfree(s);
}
