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
#include "ctree.h"

/* Name indexes */
enum {
	BTRFS_XATTR_INDEX_USER,
	BTRFS_XATTR_INDEX_POSIX_ACL_ACCESS,
	BTRFS_XATTR_INDEX_POSIX_ACL_DEFAULT,
	BTRFS_XATTR_INDEX_TRUSTED,
	BTRFS_XATTR_INDEX_SECURITY,
	BTRFS_XATTR_INDEX_SYSTEM,
	BTRFS_XATTR_INDEX_END,
};

extern struct xattr_handler btrfs_xattr_user_handler;
extern struct xattr_handler btrfs_xattr_trusted_handler;
extern struct xattr_handler btrfs_xattr_acl_access_handler;
extern struct xattr_handler btrfs_xattr_acl_default_handler;
extern struct xattr_handler btrfs_xattr_security_handler;
extern struct xattr_handler btrfs_xattr_system_handler;

extern struct xattr_handler *btrfs_xattr_handlers[];

ssize_t btrfs_xattr_get(struct inode *inode, int name_index, const char *name,
			void *buffer, size_t size);
int btrfs_xattr_set(struct inode *inode, int name_index, const char *name,
			const void *value, size_t size, int flags);

/*
 * the only reason this is public is for acl.c.  There may be a point where
 * acl.c doesn't need it, and if thats the case we need to remove it and make
 * it static in xattr.c
 */
size_t btrfs_xattr_generic_list(struct inode *inode, char *list,
				size_t list_size, const char *name,
				size_t name_len);
#endif /* __XATTR__ */
