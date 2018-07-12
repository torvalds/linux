/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef __XFS_AG_RESV_H__
#define	__XFS_AG_RESV_H__

int xfs_ag_resv_free(struct xfs_perag *pag);
int xfs_ag_resv_init(struct xfs_perag *pag);

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
	struct xfs_alloc_arg	args = {0};
	struct xfs_perag	*pag;

	args.len = 1;
	pag = xfs_perag_get(mp, agno);
	xfs_ag_resv_alloc_extent(pag, XFS_AG_RESV_RMAPBT, &args);
	xfs_perag_put(pag);
}

static inline void
xfs_ag_resv_rmapbt_free(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_perag	*pag;

	pag = xfs_perag_get(mp, agno);
	xfs_ag_resv_free_extent(pag, XFS_AG_RESV_RMAPBT, NULL, 1);
	xfs_perag_put(pag);
}

#endif	/* __XFS_AG_RESV_H__ */
