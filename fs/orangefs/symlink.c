/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

static const char *pvfs2_follow_link(struct dentry *dentry, void **cookie)
{
	char *target =  PVFS2_I(dentry->d_inode)->link_target;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s: called on %s (target is %p)\n",
		     __func__, (char *)dentry->d_name.name, target);

	*cookie = target;

	return target;
}

struct inode_operations pvfs2_symlink_inode_operations = {
	.readlink = generic_readlink,
	.follow_link = pvfs2_follow_link,
	.setattr = pvfs2_setattr,
	.getattr = pvfs2_getattr,
	.listxattr = pvfs2_listxattr,
	.setxattr = generic_setxattr,
};
