/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_IWALK_H__
#define __XFS_IWALK_H__

/*
 * Return codes for the inode/inobt walk function are 0 to continue iterating,
 * and non-zero to stop iterating.  Any non-zero value will be passed up to the
 * iwalk or inobt_walk caller.  The special value -ECANCELED can be used to
 * stop iteration, as neither iwalk nor inobt_walk will ever generate that
 * error code on their own.
 */

/* Walk all inodes in the filesystem starting from @startino. */
typedef int (*xfs_iwalk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
			    xfs_ino_t ino, void *data);

int xfs_iwalk(struct xfs_mount *mp, struct xfs_trans *tp, xfs_ino_t startino,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int inode_records, void *data);
int xfs_iwalk_threaded(struct xfs_mount *mp, xfs_ino_t startino,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int inode_records, bool poll, void *data);

/* Only iterate within the same AG as @startino. */
#define XFS_IWALK_SAME_AG	(1U << 0)

#define XFS_IWALK_FLAGS_ALL	(XFS_IWALK_SAME_AG)

/* Walk all inode btree records in the filesystem starting from @startino. */
typedef int (*xfs_inobt_walk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
				 xfs_agnumber_t agno,
				 const struct xfs_inobt_rec_incore *irec,
				 void *data);

int xfs_inobt_walk(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_ino_t startino, unsigned int flags,
		xfs_inobt_walk_fn inobt_walk_fn, unsigned int inobt_records,
		void *data);

#endif /* __XFS_IWALK_H__ */
