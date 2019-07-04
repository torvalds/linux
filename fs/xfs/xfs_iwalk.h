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
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int inode_records, void *data);
int xfs_iwalk_threaded(struct xfs_mount *mp, xfs_ino_t startino,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int inode_records, bool poll, void *data);

/* Only iterate inodes within the same AG as @startino. */
#define XFS_IWALK_SAME_AG	(0x1)

#define XFS_IWALK_FLAGS_ALL	(XFS_IWALK_SAME_AG)

/* Walk all inode btree records in the filesystem starting from @startino. */
typedef int (*xfs_inobt_walk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
				 xfs_agnumber_t agno,
				 const struct xfs_inobt_rec_incore *irec,
				 void *data);
/* Return value (for xfs_inobt_walk_fn) that aborts the walk immediately. */
#define XFS_INOBT_WALK_ABORT	(XFS_IWALK_ABORT)

int xfs_inobt_walk(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_ino_t startino, unsigned int flags,
		xfs_inobt_walk_fn inobt_walk_fn, unsigned int inobt_records,
		void *data);

/* Only iterate inobt records within the same AG as @startino. */
#define XFS_INOBT_WALK_SAME_AG	(XFS_IWALK_SAME_AG)

#define XFS_INOBT_WALK_FLAGS_ALL (XFS_INOBT_WALK_SAME_AG)

#endif /* __XFS_IWALK_H__ */
