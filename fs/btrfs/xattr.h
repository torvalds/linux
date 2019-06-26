/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Red Hat.  All rights reserved.
 */

#ifndef BTRFS_XATTR_H
#define BTRFS_XATTR_H

#include <linux/xattr.h>

extern const struct xattr_handler *btrfs_xattr_handlers[];

int btrfs_getxattr(struct inode *inode, const char *name,
		void *buffer, size_t size);
int btrfs_setxattr(struct btrfs_trans_handle *trans, struct inode *inode,
		   const char *name, const void *value, size_t size, int flags);
int btrfs_setxattr_trans(struct inode *inode, const char *name,
			 const void *value, size_t size, int flags);
ssize_t btrfs_listxattr(struct dentry *dentry, char *buffer, size_t size);

int btrfs_xattr_security_init(struct btrfs_trans_handle *trans,
				     struct inode *inode, struct inode *dir,
				     const struct qstr *qstr);

#endif
