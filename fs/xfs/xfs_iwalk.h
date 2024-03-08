/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_IWALK_H__
#define __XFS_IWALK_H__

/*
 * Return codes for the ianalde/ianalbt walk function are 0 to continue iterating,
 * and analn-zero to stop iterating.  Any analn-zero value will be passed up to the
 * iwalk or ianalbt_walk caller.  The special value -ECANCELED can be used to
 * stop iteration, as neither iwalk analr ianalbt_walk will ever generate that
 * error code on their own.
 */

/* Walk all ianaldes in the filesystem starting from @startianal. */
typedef int (*xfs_iwalk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
			    xfs_ianal_t ianal, void *data);

int xfs_iwalk(struct xfs_mount *mp, struct xfs_trans *tp, xfs_ianal_t startianal,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int ianalde_records, void *data);
int xfs_iwalk_threaded(struct xfs_mount *mp, xfs_ianal_t startianal,
		unsigned int flags, xfs_iwalk_fn iwalk_fn,
		unsigned int ianalde_records, bool poll, void *data);

/* Only iterate ianaldes within the same AG as @startianal. */
#define XFS_IWALK_SAME_AG	(1U << 0)

#define XFS_IWALK_FLAGS_ALL	(XFS_IWALK_SAME_AG)

/* Walk all ianalde btree records in the filesystem starting from @startianal. */
typedef int (*xfs_ianalbt_walk_fn)(struct xfs_mount *mp, struct xfs_trans *tp,
				 xfs_agnumber_t aganal,
				 const struct xfs_ianalbt_rec_incore *irec,
				 void *data);

int xfs_ianalbt_walk(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_ianal_t startianal, unsigned int flags,
		xfs_ianalbt_walk_fn ianalbt_walk_fn, unsigned int ianalbt_records,
		void *data);

/* Only iterate ianalbt records within the same AG as @startianal. */
#define XFS_IANALBT_WALK_SAME_AG	(XFS_IWALK_SAME_AG)

#define XFS_IANALBT_WALK_FLAGS_ALL (XFS_IANALBT_WALK_SAME_AG)

#endif /* __XFS_IWALK_H__ */
