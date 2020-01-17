// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IALLOC_BTREE_H__
#define	__XFS_IALLOC_BTREE_H__

/*
 * Iyesde map on-disk structures
 */

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;

/*
 * Btree block header size depends on a superblock flag.
 */
#define XFS_INOBT_BLOCK_LEN(mp) \
	(xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		XFS_BTREE_SBLOCK_CRC_LEN : XFS_BTREE_SBLOCK_LEN)

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (yeste that some of these may appear unused, but they are used in userspace)
 */
#define XFS_INOBT_REC_ADDR(mp, block, index) \
	((xfs_iyesbt_rec_t *) \
		((char *)(block) + \
		 XFS_INOBT_BLOCK_LEN(mp) + \
		 (((index) - 1) * sizeof(xfs_iyesbt_rec_t))))

#define XFS_INOBT_KEY_ADDR(mp, block, index) \
	((xfs_iyesbt_key_t *) \
		((char *)(block) + \
		 XFS_INOBT_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_iyesbt_key_t)))

#define XFS_INOBT_PTR_ADDR(mp, block, index, maxrecs) \
	((xfs_iyesbt_ptr_t *) \
		((char *)(block) + \
		 XFS_INOBT_BLOCK_LEN(mp) + \
		 (maxrecs) * sizeof(xfs_iyesbt_key_t) + \
		 ((index) - 1) * sizeof(xfs_iyesbt_ptr_t)))

extern struct xfs_btree_cur *xfs_iyesbt_init_cursor(struct xfs_mount *,
		struct xfs_trans *, struct xfs_buf *, xfs_agnumber_t,
		xfs_btnum_t);
extern int xfs_iyesbt_maxrecs(struct xfs_mount *, int, int);

/* ir_holemask to iyesde allocation bitmap conversion */
uint64_t xfs_iyesbt_irec_to_allocmask(struct xfs_iyesbt_rec_incore *);

#if defined(DEBUG) || defined(XFS_WARN)
int xfs_iyesbt_rec_check_count(struct xfs_mount *,
			      struct xfs_iyesbt_rec_incore *);
#else
#define xfs_iyesbt_rec_check_count(mp, rec)	0
#endif	/* DEBUG */

int xfs_fiyesbt_calc_reserves(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agyes, xfs_extlen_t *ask, xfs_extlen_t *used);
extern xfs_extlen_t xfs_iallocbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);
int xfs_iyesbt_cur(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agyes, xfs_btnum_t btnum,
		struct xfs_btree_cur **curpp, struct xfs_buf **agi_bpp);

#endif	/* __XFS_IALLOC_BTREE_H__ */
