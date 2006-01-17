/*
 * linux/fs/ext3/xattr_trusted.c
 * Handler for trusted extended attributes.
 *
 * Copyright (C) 2003 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include "xattr.h"

#define XATTR_TRUSTED_PREFIX "trusted."

static size_t
ext3_xattr_trusted_list(struct inode *inode, char *list, size_t list_size,
			const char *name, size_t name_len)
{
	const size_t prefix_len = sizeof(XATTR_TRUSTED_PREFIX)-1;
	const size_t total_len = prefix_len + name_len + 1;

	if (!capable(CAP_SYS_ADMIN))
		return 0;

	if (list && total_len <= list_size) {
		memcpy(list, XATTR_TRUSTED_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return total_len;
}

static int
ext3_xattr_trusted_get(struct inode *inode, const char *name,
		       void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext3_xattr_get(inode, EXT3_XATTR_INDEX_TRUSTED, name,
			      buffer, size);
}

static int
ext3_xattr_trusted_set(struct inode *inode, const char *name,
		       const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext3_xattr_set(inode, EXT3_XATTR_INDEX_TRUSTED, name,
			      value, size, flags);
}

struct xattr_handler ext3_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.list	= ext3_xattr_trusted_list,
	.get	= ext3_xattr_trusted_get,
	.set	= ext3_xattr_trusted_set,
};
