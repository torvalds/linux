// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_RMAP_BTREE_H__
#define __XFS_RMAP_BTREE_H__

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;
struct xbtree_afakeroot;
struct xfbtree;

/* rmaps only exist on crc enabled filesystems */
#define XFS_RMAP_BLOCK_LEN	XFS_BTREE_SBLOCK_CRC_LEN

/*
 * Record, key, and pointer address macros for btree blocks.
 *
 * (note that some of these may appear unused, but they are used in userspace)
 */
#define XFS_RMAP_REC_ADDR(block, index) \
	((struct xfs_rmap_rec *) \
		((char *)(block) + XFS_RMAP_BLOCK_LEN + \
		 (((index) - 1) * sizeof(struct xfs_rmap_rec))))

#define XFS_RMAP_KEY_ADDR(block, index) \
	((struct xfs_rmap_key *) \
		((char *)(block) + XFS_RMAP_BLOCK_LEN + \
		 ((index) - 1) * 2 * sizeof(struct xfs_rmap_key)))

#define XFS_RMAP_HIGH_KEY_ADDR(block, index) \
	((struct xfs_rmap_key *) \
		((char *)(block) + XFS_RMAP_BLOCK_LEN + \
		 sizeof(struct xfs_rmap_key) + \
		 ((index) - 1) * 2 * sizeof(struct xfs_rmap_key)))

#define XFS_RMAP_PTR_ADDR(block, index, maxrecs) \
	((xfs_rmap_ptr_t *) \
		((char *)(block) + XFS_RMAP_BLOCK_LEN + \
		 (maxrecs) * 2 * sizeof(struct xfs_rmap_key) + \
		 ((index) - 1) * sizeof(xfs_rmap_ptr_t)))

struct xfs_btree_cur *xfs_rmapbt_init_cursor(struct xfs_mount *mp,
				struct xfs_trans *tp, struct xfs_buf *bp,
				struct xfs_perag *pag);
void xfs_rmapbt_commit_staged_btree(struct xfs_btree_cur *cur,
		struct xfs_trans *tp, struct xfs_buf *agbp);
int xfs_rmapbt_maxrecs(int blocklen, int leaf);
extern void xfs_rmapbt_compute_maxlevels(struct xfs_mount *mp);

extern xfs_extlen_t xfs_rmapbt_calc_size(struct xfs_mount *mp,
		unsigned long long len);
extern xfs_extlen_t xfs_rmapbt_max_size(struct xfs_mount *mp,
		xfs_agblock_t agblocks);

extern int xfs_rmapbt_calc_reserves(struct xfs_mount *mp, struct xfs_trans *tp,
		struct xfs_perag *pag, xfs_extlen_t *ask, xfs_extlen_t *used);

unsigned int xfs_rmapbt_maxlevels_ondisk(void);

int __init xfs_rmapbt_init_cur_cache(void);
void xfs_rmapbt_destroy_cur_cache(void);

struct xfs_btree_cur *xfs_rmapbt_mem_cursor(struct xfs_perag *pag,
		struct xfs_trans *tp, struct xfbtree *xfbtree);
int xfs_rmapbt_mem_init(struct xfs_mount *mp, struct xfbtree *xfbtree,
		struct xfs_buftarg *btp, xfs_agnumber_t agno);

#endif /* __XFS_RMAP_BTREE_H__ */
