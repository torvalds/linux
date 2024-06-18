/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_BTREE_MEM_H__
#define __XFS_BTREE_MEM_H__

typedef uint64_t xfbno_t;

#define XFBNO_BLOCKSIZE			(XMBUF_BLOCKSIZE)
#define XFBNO_BBSHIFT			(XMBUF_BLOCKSHIFT - BBSHIFT)
#define XFBNO_BBSIZE			(XFBNO_BLOCKSIZE >> BBSHIFT)

static inline xfs_daddr_t xfbno_to_daddr(xfbno_t blkno)
{
	return blkno << XFBNO_BBSHIFT;
}

static inline xfbno_t xfs_daddr_to_xfbno(xfs_daddr_t daddr)
{
	return daddr >> XFBNO_BBSHIFT;
}

struct xfbtree {
	/* buffer cache target for this in-memory btree */
	struct xfs_buftarg		*target;

	/* Highest block number that has been written to. */
	xfbno_t				highest_bno;

	/* Owner of this btree. */
	unsigned long long		owner;

	/* Btree header */
	union xfs_btree_ptr		root;
	unsigned int			nlevels;

	/* Minimum and maximum records per block. */
	unsigned int			maxrecs[2];
	unsigned int			minrecs[2];
};

#ifdef CONFIG_XFS_BTREE_IN_MEM
static inline bool xfbtree_verify_bno(struct xfbtree *xfbt, xfbno_t bno)
{
	return xmbuf_verify_daddr(xfbt->target, xfbno_to_daddr(bno));
}

void xfbtree_set_root(struct xfs_btree_cur *cur,
		const union xfs_btree_ptr *ptr, int inc);
void xfbtree_init_ptr_from_cur(struct xfs_btree_cur *cur,
		union xfs_btree_ptr *ptr);
struct xfs_btree_cur *xfbtree_dup_cursor(struct xfs_btree_cur *cur);

int xfbtree_get_minrecs(struct xfs_btree_cur *cur, int level);
int xfbtree_get_maxrecs(struct xfs_btree_cur *cur, int level);

int xfbtree_alloc_block(struct xfs_btree_cur *cur,
		const union xfs_btree_ptr *start, union xfs_btree_ptr *ptr,
		int *stat);
int xfbtree_free_block(struct xfs_btree_cur *cur, struct xfs_buf *bp);

/* Callers must set xfbt->target and xfbt->owner before calling this */
int xfbtree_init(struct xfs_mount *mp, struct xfbtree *xfbt,
		struct xfs_buftarg *btp, const struct xfs_btree_ops *ops);
void xfbtree_destroy(struct xfbtree *xfbt);

int xfbtree_trans_commit(struct xfbtree *xfbt, struct xfs_trans *tp);
void xfbtree_trans_cancel(struct xfbtree *xfbt, struct xfs_trans *tp);
#else
# define xfbtree_verify_bno(...)	(false)
#endif /* CONFIG_XFS_BTREE_IN_MEM */

#endif /* __XFS_BTREE_MEM_H__ */
