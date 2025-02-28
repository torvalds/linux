/*
 *  linux/fs/hfs/sysdep.c
 *
 * Copyright (C) 1996  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains the code to do various system dependent things.
 */

#include <linux/namei.h>
#include "hfs_fs.h"

/* dentry case-handling: just lowercase everything */

static int hfs_revalidate_dentry(struct inode *dir, const struct qstr *name,
				 struct dentry *dentry, unsigned int flags)
{
	struct inode *inode;
	int diff;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	inode = d_inode(dentry);
	if(!inode)
		return 1;

	/* fix up inode on a timezone change */
	diff = sys_tz.tz_minuteswest * 60 - HFS_I(inode)->tz_secondswest;
	if (diff) {
		struct timespec64 ts = inode_get_ctime(inode);

		inode_set_ctime(inode, ts.tv_sec + diff, ts.tv_nsec);
		ts = inode_get_atime(inode);
		inode_set_atime(inode, ts.tv_sec + diff, ts.tv_nsec);
		ts = inode_get_mtime(inode);
		inode_set_mtime(inode, ts.tv_sec + diff, ts.tv_nsec);
		HFS_I(inode)->tz_secondswest += diff;
	}
	return 1;
}

const struct dentry_operations hfs_dentry_operations =
{
	.d_revalidate	= hfs_revalidate_dentry,
	.d_hash		= hfs_hash_dentry,
	.d_compare	= hfs_compare_dentry,
};

