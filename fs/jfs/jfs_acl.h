/*
 *   Copyright (c) International Business Machines  Corp., 2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _H_JFS_ACL
#define _H_JFS_ACL

#ifdef CONFIG_JFS_POSIX_ACL

#include <linux/xattr_acl.h>

int jfs_permission(struct inode *, int, struct nameidata *);
int jfs_init_acl(struct inode *, struct inode *);
int jfs_setattr(struct dentry *, struct iattr *);

#endif		/* CONFIG_JFS_POSIX_ACL */
#endif		/* _H_JFS_ACL */
