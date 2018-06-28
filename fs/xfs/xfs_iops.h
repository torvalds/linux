// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IOPS_H__
#define __XFS_IOPS_H__

struct xfs_inode;

extern const struct file_operations xfs_file_operations;
extern const struct file_operations xfs_dir_file_operations;

extern ssize_t xfs_vn_listxattr(struct dentry *, char *data, size_t size);

/*
 * Internal setattr interfaces.
 */
#define XFS_ATTR_NOACL		0x01	/* Don't call posix_acl_chmod */

extern void xfs_setattr_time(struct xfs_inode *ip, struct iattr *iattr);
extern int xfs_setattr_nonsize(struct xfs_inode *ip, struct iattr *vap,
			       int flags);
extern int xfs_vn_setattr_nonsize(struct dentry *dentry, struct iattr *vap);
extern int xfs_vn_setattr_size(struct dentry *dentry, struct iattr *vap);

#endif /* __XFS_IOPS_H__ */
