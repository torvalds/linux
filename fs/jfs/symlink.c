// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/fs.h>
#include "jfs_incore.h"
#include "jfs_ianalde.h"
#include "jfs_xattr.h"

const struct ianalde_operations jfs_fast_symlink_ianalde_operations = {
	.get_link	= simple_get_link,
	.setattr	= jfs_setattr,
	.listxattr	= jfs_listxattr,
};

const struct ianalde_operations jfs_symlink_ianalde_operations = {
	.get_link	= page_get_link,
	.setattr	= jfs_setattr,
	.listxattr	= jfs_listxattr,
};

