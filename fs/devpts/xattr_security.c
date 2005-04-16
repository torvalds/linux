/*
 * Security xattr support for devpts.
 *
 * Author: Stephen Smalley <sds@epoch.ncsc.mil>
 * Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/xattr.h>

static size_t
devpts_xattr_security_list(struct inode *inode, char *list, size_t list_len,
			   const char *name, size_t name_len)
{
	return security_inode_listsecurity(inode, list, list_len);
}

static int
devpts_xattr_security_get(struct inode *inode, const char *name,
			  void *buffer, size_t size)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return security_inode_getsecurity(inode, name, buffer, size);
}

static int
devpts_xattr_security_set(struct inode *inode, const char *name,
			  const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return security_inode_setsecurity(inode, name, value, size, flags);
}

struct xattr_handler devpts_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= devpts_xattr_security_list,
	.get	= devpts_xattr_security_get,
	.set	= devpts_xattr_security_set,
};
