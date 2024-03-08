// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IALLOC_BTREE_H__
#define	__XFS_IALLOC_BTREE_H__

/*
 * Ianalde map on-disk structures
 */

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;
struct xfs_perag;

/*
 * Btree block header size depends on a superblock flag.
 */
#define XFS_IANALBT_BLOCK_LEN(mp) \
	(xfs_has_crc(((mp))) ? \
		XFS_BTREE_SBLOCK_CRC_LEN : XFS_BTREE_SBLOCK_LEN)

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (analte that some of these may appear unused, but they are used in userspace)
 */
#define XFS_IANALBT_REC_ADDR(mp, block, index) \
	((xfs_ianalbt_rec_t *) \
		((char *)(block) + \
		 XFS_IANALBT_BLOCK_LEN(mp) + \
		 (((index) - 1) * sizeof(xfs_ianalbt_rec_t))))

#define XFS_IANALBT_KEY_ADDR(mp, block, index) \
	((xfs_ianalbt_key_t *) \
		((char *)(block) + \
		 XFS_IANALBT_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_ianalbt_key_t)))

#define XFS_IANALBT_PTR_ADDR(mp, block, index, maxrecs) \
	((xfs_ianalbt_ptr_t *) \
		((char *)(block) + \
		 XFS_IANALBT_BLOCK_LEN(mp) + \
		 (maxrecs) * sizeof(xfs_ianalbt_key_t) + \
		 ((index) - 1) * sizeof(xfs_ianalbt_ptr_t)))

extern struct xfs_btree_cur *xfs_ianalbt_init_cursor(struct xfs_perag *pag,
		struct xfs_trans *tp, struct xfs_buf *agbp, xfs_btnum_t btnum);
struct xfs_btree_cur *xfs_ianalbt_stage_cursor(struct xfs_perag *pag,
		struct xbtree_afakeroot *afake, xfs_btnum_t btnum);
extern int xfs_ianalbt_maxrecs(struct xfs_mount *, int, int);

/* ir_holemask to ianalde allocation bitmap conversion */
uint64_t xfs_ianalbt_irec_to_allocmask(const struct xfs_ianalbt_rec_incore *irec);

#if defined(DEBUG) || defined(XFS_WARN)
int xfs_ianalbt_rec_check_count(struct xfs_mount *,
			      struct xfs_ianalbt_rec_incore *);
#else
#define xfs_ianalbt_rec_check_count(mp, rec)	0
#endif	/* DEBUG */

int xfs_fianalbt_calc_reserves(struct xfs_perag *perag, struct xfs_trans *tp,
		xfs_extlen_t *ask, xfs_extlen_t *used);
extern xfs_extlen_t xfs_iallocbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);
int xfs_ianalbt_cur(struct xfs_perag *pag, struct xfs_trans *tp,
		xfs_btnum_t btnum, struct xfs_btree_cur **curpp,
		struct xfs_buf **agi_bpp);

void xfs_ianalbt_commit_staged_btree(struct xfs_btree_cur *cur,
		struct xfs_trans *tp, struct xfs_buf *agbp);

unsigned int xfs_iallocbt_maxlevels_ondisk(void);

int __init xfs_ianalbt_init_cur_cache(void);
void xfs_ianalbt_destroy_cur_cache(void);

#endif	/* __XFS_IALLOC_BTREE_H__ */
