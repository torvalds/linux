/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_IWALK_H__
#define __XFS_IWALK_H__

/* Walk all inodes in the filesystem starting from @startino. */
typedef int (*xfs_iwalk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
			    xfs_ino_t ino, void *data);
/* Return values for xfs_iwalk_fn. */
#define XFS_IWALK_CONTINUE	(XFS_ITER_CONTINUE)
#define XFS_IWALK_ABORT		(XFS_ITER_ABORT)

int xfs_iwalk(struct xfs_mount *mp, struct xfs_trans *tp, xfs_ino_t startino,
		xfs_iwalk_fn iwalk_fn, unsigned int inode_records, void *data);

#endif /* __XFS_IWALK_H__ */
