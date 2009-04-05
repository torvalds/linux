/*
 *  linux/fs/hfs/sysdep.c
 *
 * Copyright (C) 1996  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains the code to do various system dependent things.
 */

#include "hfs_fs.h"

/* dentry case-handling: just lowercase everything */

static int hfs_revalidate_dentry(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	int diff;

	if(!inode)
		return 1;

	/* fix up inode on a timezone change */
	diff = sys_tz.tz_minuteswest * 60 - HFS_I(inode)->tz_secondswest;
	if (diff) {
		inode->i_ctime.tv_sec += diff;
		inode->i_atime.tv_sec += diff;
		inode->i_mtime.tv_sec += diff;
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

