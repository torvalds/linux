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
#ifndef __XFS_REFCOUNT_BTREE_H__
#define	__XFS_REFCOUNT_BTREE_H__

/*
 * Reference Count Btree on-disk structures
 */

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;

/*
 * Btree block header size
 */
#define XFS_REFCOUNT_BLOCK_LEN	XFS_BTREE_SBLOCK_CRC_LEN

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
#define XFS_REFCOUNT_REC_ADDR(block, index) \
	((struct xfs_refcount_rec *) \
		((char *)(block) + \
		 XFS_REFCOUNT_BLOCK_LEN + \
		 (((index) - 1) * sizeof(struct xfs_refcount_rec))))

#define XFS_REFCOUNT_KEY_ADDR(block, index) \
	((struct xfs_refcount_key *) \
		((char *)(block) + \
		 XFS_REFCOUNT_BLOCK_LEN + \
		 ((index) - 1) * sizeof(struct xfs_refcount_key)))

#define XFS_REFCOUNT_PTR_ADDR(block, index, maxrecs) \
	((xfs_refcount_ptr_t *) \
		((char *)(block) + \
		 XFS_REFCOUNT_BLOCK_LEN + \
		 (maxrecs) * sizeof(struct xfs_refcount_key) + \
		 ((index) - 1) * sizeof(xfs_refcount_ptr_t)))

extern struct xfs_btree_cur *xfs_refcountbt_init_cursor(struct xfs_mount *mp,
		struct xfs_trans *tp, struct xfs_buf *agbp, xfs_agnumber_t agno,
		struct xfs_defer_ops *dfops);
extern int xfs_refcountbt_maxrecs(struct xfs_mount *mp, int blocklen,
		bool leaf);
extern void xfs_refcountbt_compute_maxlevels(struct xfs_mount *mp);

#endif	/* __XFS_REFCOUNT_BTREE_H__ */
