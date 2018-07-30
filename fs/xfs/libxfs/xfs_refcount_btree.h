// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
		struct xfs_trans *tp, struct xfs_buf *agbp,
		xfs_agnumber_t agno);
extern int xfs_refcountbt_maxrecs(int blocklen, bool leaf);
extern void xfs_refcountbt_compute_maxlevels(struct xfs_mount *mp);

extern xfs_extlen_t xfs_refcountbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);
extern xfs_extlen_t xfs_refcountbt_max_size(struct xfs_mount *mp,
		xfs_agblock_t agblocks);

extern int xfs_refcountbt_calc_reserves(struct xfs_mount *mp,
		struct xfs_trans *tp, xfs_agnumber_t agno, xfs_extlen_t *ask,
		xfs_extlen_t *used);

#endif	/* __XFS_REFCOUNT_BTREE_H__ */
