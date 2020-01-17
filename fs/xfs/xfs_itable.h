// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 */
#ifndef __XFS_ITABLE_H__
#define	__XFS_ITABLE_H__

/* In-memory representation of a userspace request for batch iyesde data. */
struct xfs_ibulk {
	struct xfs_mount	*mp;
	void __user		*ubuffer; /* user output buffer */
	xfs_iyes_t		startiyes; /* start with this iyesde */
	unsigned int		icount;   /* number of elements in ubuffer */
	unsigned int		ocount;   /* number of records returned */
	unsigned int		flags;    /* see XFS_IBULK_FLAG_* */
};

/* Only iterate within the same AG as startiyes */
#define XFS_IBULK_SAME_AG	(XFS_IWALK_SAME_AG)

/*
 * Advance the user buffer pointer by one record of the given size.  If the
 * buffer is yesw full, return the appropriate error code.
 */
static inline int
xfs_ibulk_advance(
	struct xfs_ibulk	*breq,
	size_t			bytes)
{
	char __user		*b = breq->ubuffer;

	breq->ubuffer = b + bytes;
	breq->ocount++;
	return breq->ocount == breq->icount ? -ECANCELED : 0;
}

/*
 * Return stat information in bulk (by-iyesde) for the filesystem.
 */

/*
 * Return codes for the formatter function are 0 to continue iterating, and
 * yesn-zero to stop iterating.  Any yesn-zero value will be passed up to the
 * bulkstat/inumbers caller.  The special value -ECANCELED can be used to stop
 * iteration, as neither bulkstat yesr inumbers will ever generate that error
 * code on their own.
 */

typedef int (*bulkstat_one_fmt_pf)(struct xfs_ibulk *breq,
		const struct xfs_bulkstat *bstat);

int xfs_bulkstat_one(struct xfs_ibulk *breq, bulkstat_one_fmt_pf formatter);
int xfs_bulkstat(struct xfs_ibulk *breq, bulkstat_one_fmt_pf formatter);
void xfs_bulkstat_to_bstat(struct xfs_mount *mp, struct xfs_bstat *bs1,
		const struct xfs_bulkstat *bstat);

typedef int (*inumbers_fmt_pf)(struct xfs_ibulk *breq,
		const struct xfs_inumbers *igrp);

int xfs_inumbers(struct xfs_ibulk *breq, inumbers_fmt_pf formatter);
void xfs_inumbers_to_iyesgrp(struct xfs_iyesgrp *ig1,
		const struct xfs_inumbers *ig);

#endif	/* __XFS_ITABLE_H__ */
