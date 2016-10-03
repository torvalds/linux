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
#ifndef __XFS_REFCOUNT_H__
#define __XFS_REFCOUNT_H__

extern int xfs_refcount_lookup_le(struct xfs_btree_cur *cur,
		xfs_agblock_t bno, int *stat);
extern int xfs_refcount_lookup_ge(struct xfs_btree_cur *cur,
		xfs_agblock_t bno, int *stat);
extern int xfs_refcount_get_rec(struct xfs_btree_cur *cur,
		struct xfs_refcount_irec *irec, int *stat);

enum xfs_refcount_intent_type {
	XFS_REFCOUNT_INCREASE = 1,
	XFS_REFCOUNT_DECREASE,
	XFS_REFCOUNT_ALLOC_COW,
	XFS_REFCOUNT_FREE_COW,
};

struct xfs_refcount_intent {
	struct list_head			ri_list;
	enum xfs_refcount_intent_type		ri_type;
	xfs_fsblock_t				ri_startblock;
	xfs_extlen_t				ri_blockcount;
};

#endif	/* __XFS_REFCOUNT_H__ */
