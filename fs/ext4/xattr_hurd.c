// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/xattr_hurd.c
 * Handler for extended gnu attributes for the Hurd.
 *
 * Copyright (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 * Copyright (C) 2020 by Jan (janneke) Nieuwenhuizen, <janneke@gnu.org>
 */

#include <linux/init.h>
#include <linux/string.h>
#include "ext4.h"
#include "xattr.h"

static bool
ext4_xattr_hurd_list(struct dentry *dentry)
{
	return test_opt(dentry->d_sb, XATTR_USER);
}

static int
ext4_xattr_hurd_get(const struct xattr_handler *handler,
		    struct dentry *unused, struct inode *inode,
		    const char *name, void *buffer, size_t size)
{
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;

	return ext4_xattr_get(inode, EXT4_XATTR_INDEX_HURD,
			      name, buffer, size);
}

static int
ext4_xattr_hurd_set(const struct xattr_handler *handler,
		    struct mnt_idmap *idmap,
		    struct dentry *unused, struct inode *inode,
		    const char *name, const void *value,
		    size_t size, int flags)
{
	if (!test_opt(inode->i_sb, XATTR_USER))
		return -EOPNOTSUPP;

	return ext4_xattr_set(inode, EXT4_XATTR_INDEX_HURD,
			      name, value, size, flags);
}

const struct xattr_handler ext4_xattr_hurd_handler = {
	.prefix	= XATTR_HURD_PREFIX,
	.list	= ext4_xattr_hurd_list,
	.get	= ext4_xattr_hurd_get,
	.set	= ext4_xattr_hurd_set,
};
