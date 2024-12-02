// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/fs.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_xattr.h"

const struct inode_operations jfs_fast_symlink_inode_operations = {
	.get_link	= simple_get_link,
	.setattr	= jfs_setattr,
	.listxattr	= jfs_listxattr,
};

const struct inode_operations jfs_symlink_inode_operations = {
	.get_link	= page_get_link,
	.setattr	= jfs_setattr,
	.listxattr	= jfs_listxattr,
};

