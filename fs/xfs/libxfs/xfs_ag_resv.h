/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_AG_RESV_H__
#define	__XFS_AG_RESV_H__

void xfs_ag_resv_free(struct xfs_perag *pag);
int xfs_ag_resv_init(struct xfs_perag *pag, struct xfs_trans *tp);

bool xfs_ag_resv_critical(struct xfs_perag *pag, enum xfs_ag_resv_type type);
xfs_extlen_t xfs_ag_resv_needed(struct xfs_perag *pag,
		enum xfs_ag_resv_type type);

void xfs_ag_resv_alloc_extent(struct xfs_perag *pag, enum xfs_ag_resv_type type,
		struct xfs_alloc_arg *args);
void xfs_ag_resv_free_extent(struct xfs_perag *pag, enum xfs_ag_resv_type type,
		struct xfs_trans *tp, xfs_extlen_t len);

static inline struct xfs_ag_resv *
xfs_perag_resv(
	struct xfs_perag	*pag,
	enum xfs_ag_resv_type	type)
{
	switch (type) {
	case XFS_AG_RESV_METADATA:
		return &pag->pag_meta_resv;
	case XFS_AG_RESV_RMAPBT:
		return &pag->pag_rmapbt_resv;
	default:
		return NULL;
	}
}

#endif	/* __XFS_AG_RESV_H__ */
