// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IOPS_H__
#define __XFS_IOPS_H__

struct xfs_inode;

extern ssize_t xfs_vn_listxattr(struct dentry *, char *data, size_t size);

int xfs_vn_setattr_size(struct mnt_idmap *idmap,
		struct dentry *dentry, struct iattr *vap);

int xfs_inode_init_security(struct inode *inode, struct inode *dir,
		const struct qstr *qstr);

extern void xfs_setup_inode(struct xfs_inode *ip);
extern void xfs_setup_iops(struct xfs_inode *ip);
extern void xfs_diflags_to_iflags(struct xfs_inode *ip, bool init);
unsigned int xfs_get_atomic_write_min(struct xfs_inode *ip);
unsigned int xfs_get_atomic_write_max(struct xfs_inode *ip);
unsigned int xfs_get_atomic_write_max_opt(struct xfs_inode *ip);

#endif /* __XFS_IOPS_H__ */
