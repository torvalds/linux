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
xfs_fileattr_get(
	struct dentry		*dentry,
	struct file_kattr	*fa);

extern int
xfs_fileattr_set(
	struct mnt_idmap	*idmap,
	struct dentry		*dentry,
	struct file_kattr	*fa);

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
