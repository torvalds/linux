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

#endif	/* __XFS_AG_RESV_H__ */
