// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IOCTL_H__
#define __XFS_IOCTL_H__

struct xfs_bstat;
struct xfs_ibulk;
struct xfs_inogrp;

int
xfs_ioc_swapext(
	xfs_swapext_t	*sxp);

extern int
xfs_find_handle(
	unsigned int		cmd,
	xfs_fsop_handlereq_t	*hreq);

extern int
xfs_open_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq);

extern int
xfs_readlink_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq);

int xfs_ioc_attrmulti_one(struct file *parfilp, struct inode *inode,
		uint32_t opcode, void __user *uname, void __user *value,
		uint32_t *len, uint32_t flags);
int xfs_ioc_attr_list(struct xfs_inode *dp, void __user *ubuf,
		      size_t bufsize, int flags,
		      struct xfs_attrlist_cursor __user *ucursor);

extern struct dentry *
xfs_handle_to_dentry(
	struct file		*parfilp,
	void __user		*uhandle,
	u32			hlen);

extern int
xfs_fileattr_get(
	struct dentry		*dentry,
	struct fileattr		*fa);

extern int
xfs_fileattr_set(
	struct user_namespace	*mnt_userns,
	struct dentry		*dentry,
	struct fileattr		*fa);

extern long
xfs_file_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p);

extern long
xfs_file_compat_ioctl(
	struct file		*file,
	unsigned int		cmd,
	unsigned long		arg);

int xfs_fsbulkstat_one_fmt(struct xfs_ibulk *breq,
			   const struct xfs_bulkstat *bstat);
int xfs_fsinumbers_fmt(struct xfs_ibulk *breq, const struct xfs_inumbers *igrp);

#endif
