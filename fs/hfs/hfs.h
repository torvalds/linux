/*
 *  linux/fs/hfs/hfs.h
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 */

#ifndef _HFS_H
#define _HFS_H

#include <linux/hfs_common.h>

/*======== Data structures kept in memory ========*/

struct hfs_readdir_data {
	struct list_head list;
	struct file *file;
	struct hfs_cat_key key;
};

#endif
