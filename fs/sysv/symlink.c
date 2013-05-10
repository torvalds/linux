/*
 *  linux/fs/sysv/symlink.c
 *
 *  Handling of System V filesystem fast symlinks extensions.
 *  Aug 2001, Christoph Hellwig (hch@infradead.org)
 */

#include "sysv.h"
#include <linux/namei.h>

static void *sysv_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	nd_set_link(nd, (char *)SYSV_I(dentry->d_inode)->i_data);
	return NULL;
}

const struct inode_operations sysv_fast_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= sysv_follow_link,
};
