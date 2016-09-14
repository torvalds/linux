/*
 * Copyright (C) 2007 Red Hat.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __XATTR__
#define __XATTR__

#include <linux/xattr.h>

extern const struct xattr_handler *btrfs_xattr_handlers[];

extern ssize_t __btrfs_getxattr(struct inode *inode, const char *name,
		void *buffer, size_t size);
extern int __btrfs_setxattr(struct btrfs_trans_handle *trans,
			    struct inode *inode, const char *name,
			    const void *value, size_t size, int flags);

extern int btrfs_xattr_security_init(struct btrfs_trans_handle *trans,
				     struct inode *inode, struct inode *dir,
				     const struct qstr *qstr);

#endif /* __XATTR__ */
