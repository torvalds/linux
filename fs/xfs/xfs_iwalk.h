/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_IWALK_H__
#define __XFS_IWALK_H__

/*
 * Return codes for the iyesde/iyesbt walk function are 0 to continue iterating,
 * and yesn-zero to stop iterating.  Any yesn-zero value will be passed up to the
 * iwalk or iyesbt_walk caller.  The special value -ECANCELED can be used to
 * stop iteration, as neither iwalk yesr iyesbt_walk will ever generate that
 * error code on their own.
 */

/* Walk all iyesdes in the filesystem starting from @startiyes. */
typedef int (*xfs_iwalk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
			    xfs_iyes_t iyes, void *data);

int xfs_iwalk(struct xfs_mount *mp, struct xfs_trans *tp, xfs_iyes_t startiyes,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int iyesde_records, void *data);
int xfs_iwalk_threaded(struct xfs_mount *mp, xfs_iyes_t startiyes,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int iyesde_records, bool poll, void *data);

/* Only iterate iyesdes within the same AG as @startiyes. */
#define XFS_IWALK_SAME_AG	(0x1)

#define XFS_IWALK_FLAGS_ALL	(XFS_IWALK_SAME_AG)

/* Walk all iyesde btree records in the filesystem starting from @startiyes. */
typedef int (*xfs_iyesbt_walk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
				 xfs_agnumber_t agyes,
				 const struct xfs_iyesbt_rec_incore *irec,
				 void *data);

int xfs_iyesbt_walk(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_iyes_t startiyes, unsigned int flags,
		xfs_iyesbt_walk_fn iyesbt_walk_fn, unsigned int iyesbt_records,
		void *data);

/* Only iterate iyesbt records within the same AG as @startiyes. */
#define XFS_INOBT_WALK_SAME_AG	(XFS_IWALK_SAME_AG)

#define XFS_INOBT_WALK_FLAGS_ALL (XFS_INOBT_WALK_SAME_AG)

#endif /* __XFS_IWALK_H__ */
