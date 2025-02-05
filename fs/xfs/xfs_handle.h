// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2022-2024 Oracle.
 * All rights reserved.
 */
#ifndef	__XFS_HANDLE_H__
#define	__XFS_HANDLE_H__

int xfs_attrlist_by_handle(struct file *parfilp,
		struct xfs_fsop_attrlist_handlereq __user *p);
int xfs_attrmulti_by_handle(struct file *parfilp, void __user *arg);

int xfs_find_handle(unsigned int cmd, struct xfs_fsop_handlereq *hreq);
int xfs_open_by_handle(struct file *parfilp, struct xfs_fsop_handlereq *hreq);
int xfs_readlink_by_handle(struct file *parfilp,
		struct xfs_fsop_handlereq *hreq);

int xfs_ioc_attrmulti_one(struct file *parfilp, struct inode *inode,
		uint32_t opcode, void __user *uname, void __user *value,
		uint32_t *len, uint32_t flags);
int xfs_ioc_attr_list(struct xfs_inode *dp, void __user *ubuf,
		      size_t bufsize, int flags,
		      struct xfs_attrlist_cursor __user *ucursor);

struct dentry *xfs_handle_to_dentry(struct file *parfilp, void __user *uhandle,
		u32 hlen);

int xfs_ioc_getparents(struct file *file, struct xfs_getparents __user *arg);
int xfs_ioc_getparents_by_handle(struct file *file,
		struct xfs_getparents_by_handle __user *arg);

#endif	/* __XFS_HANDLE_H__ */
