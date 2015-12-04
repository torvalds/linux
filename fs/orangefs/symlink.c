/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

static const char *orangefs_follow_link(struct dentry *dentry, void **cookie)
{
	char *target =  ORANGEFS_I(dentry->d_inode)->link_target;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s: called on %s (target is %p)\n",
		     __func__, (char *)dentry->d_name.name, target);

	*cookie = target;

	return target;
}

struct inode_operations orangefs_symlink_inode_operations = {
	.readlink = generic_readlink,
	.follow_link = orangefs_follow_link,
	.setattr = orangefs_setattr,
	.getattr = orangefs_getattr,
	.listxattr = orangefs_listxattr,
	.setxattr = generic_setxattr,
};
