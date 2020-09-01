/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_AG_RESV_H__
#define	__XFS_AG_RESV_H__

int xfs_ag_resv_free(struct xfs_perag *pag);
int xfs_ag_resv_init(struct xfs_perag *pag, struct xfs_trans *tp);

bool xfs_ag_resv_critical(struct xfs_perag *pag, enum xfs_ag_resv_type type);
xfs_extlen_t xfs_ag_resv_needed(struct xfs_perag *pag,
		enum xfs_ag_resv_type type);

void xfs_ag_resv_alloc_extent(struct xfs_perag *pag, enum xfs_ag_resv_type type,
		struct xfs_alloc_arg *args);
void xfs_ag_resv_free_extent(struct xfs_perag *pag, enum xfs_ag_resv_type type,
		struct xfs_trans *tp, xfs_extlen_t len);

/*
 * RMAPBT reservation accounting wrappers. Since rmapbt blocks are sourced from
 * the AGFL, they are allocated one at a time and the reservation updates don't
 * require a transaction.
 */
static inline void
xfs_ag_resv_rmapbt_alloc(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_alloc_arg	args = { NULL };
	struct xfs_perag	*pag;

	args.len = 1;
	pag = xfs_perag_get(mp, agno);
	xfs_ag_resv_alloc_extent(pag, XFS_AG_RESV_RMAPBT, &args);
	xfs_perag_put(pag);
}

#endif	/* __XFS_AG_RESV_H__ */
