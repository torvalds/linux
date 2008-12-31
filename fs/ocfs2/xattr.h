/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.h
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef OCFS2_XATTR_H
#define OCFS2_XATTR_H

#include <linux/init.h>
#include <linux/xattr.h>

enum ocfs2_xattr_type {
	OCFS2_XATTR_INDEX_USER = 1,
	OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS,
	OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT,
	OCFS2_XATTR_INDEX_TRUSTED,
	OCFS2_XATTR_INDEX_SECURITY,
	OCFS2_XATTR_MAX
};

extern struct xattr_handler ocfs2_xattr_user_handler;
extern struct xattr_handler ocfs2_xattr_trusted_handler;
extern struct xattr_handler *ocfs2_xattr_handlers[];

ssize_t ocfs2_listxattr(struct dentry *, char *, size_t);
int ocfs2_xattr_set(struct inode *, int, const char *, const void *,
		    size_t, int);
int ocfs2_xattr_remove(struct inode *, struct buffer_head *);

#endif /* OCFS2_XATTR_H */
