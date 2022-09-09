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

extern void xfs_setattr_time(struct xfs_inode *ip, struct iattr *iattr);
int xfs_vn_setattr_size(struct user_namespace *mnt_userns,
		struct dentry *dentry, struct iattr *vap);

int xfs_inode_init_security(struct inode *inode, struct inode *dir,
		const struct qstr *qstr);

#endif /* __XFS_IOPS_H__ */
