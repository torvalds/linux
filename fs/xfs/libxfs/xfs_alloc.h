/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_ALLOC_H__
#define	__XFS_ALLOC_H__

struct xfs_buf;
struct xfs_btree_cur;
struct xfs_mount;
struct xfs_perag;
struct xfs_trans;

extern struct workqueue_struct *xfs_alloc_wq;

unsigned int xfs_agfl_size(struct xfs_mount *mp);

/*
 * Freespace allocation types.  Argument to xfs_alloc_[v]extent.
 */
#define XFS_ALLOCTYPE_FIRST_AG	0x02	/* ... start at ag 0 */
#define XFS_ALLOCTYPE_THIS_AG	0x08	/* anywhere in this a.g. */
#define XFS_ALLOCTYPE_START_BNO	0x10	/* near this block else anywhere */
#define XFS_ALLOCTYPE_NEAR_BNO	0x20	/* in this a.g. and near this block */
#define XFS_ALLOCTYPE_THIS_BNO	0x40	/* at exactly this block */

/* this should become an enum again when the tracing code is fixed */
typedef unsigned int xfs_alloctype_t;

#define XFS_ALLOC_TYPES \
	{ XFS_ALLOCTYPE_FIRST_AG,	"FIRST_AG" }, \
	{ XFS_ALLOCTYPE_THIS_AG,	"THIS_AG" }, \
	{ XFS_ALLOCTYPE_START_BNO,	"START_BNO" }, \
	{ XFS_ALLOCTYPE_NEAR_BNO,	"NEAR_BNO" }, \
	{ XFS_ALLOCTYPE_THIS_BNO,	"THIS_BNO" }

/*
 * Flags for xfs_alloc_fix_freelist.
 */
#define	XFS_ALLOC_FLAG_TRYLOCK	0x00000001  /* use trylock for buffer locking */
#define	XFS_ALLOC_FLAG_FREEING	0x00000002  /* indicate caller is freeing extents*/
#define	XFS_ALLOC_FLAG_NORMAP	0x00000004  /* don't modify the rmapbt */
#define	XFS_ALLOC_FLAG_NOSHRINK	0x00000008  /* don't shrink the freelist */
#define	XFS_ALLOC_FLAG_CHECK	0x00000010  /* test only, don't modify args */

/*
 * Argument structure for xfs_alloc routines.
 * This is turned into a structure to avoid having 20 arguments passed
 * down several levels of the stack.
 */
typedef struct xfs_alloc_arg {
	struct xfs_trans *tp;		/* transaction pointer */
	struct xfs_mount *mp;		/* file system mount point */
	struct xfs_buf	*agbp;		/* buffer for a.g. freelist header */
	struct xfs_perag *pag;		/* per-ag struct for this agno */
	xfs_fsblock_t	fsbno;		/* file system block number */
	xfs_agnumber_t	agno;		/* allocation group number */
	xfs_agblock_t	agbno;		/* allocation group-relative block # */
	xfs_extlen_t	minlen;		/* minimum size of extent */
	xfs_extlen_t	maxlen;		/* maximum size of extent */
	xfs_extlen_t	mod;		/* mod value for extent size */
	xfs_extlen_t	prod;		/* prod value for extent size */
	xfs_extlen_t	minleft;	/* min blocks must be left after us */
	xfs_extlen_t	total;		/* total blocks needed in xaction */
	xfs_extlen_t	alignment;	/* align answer to multiple of this */
	xfs_extlen_t	minalignslop;	/* slop for minlen+alignment calcs */
	xfs_agblock_t	min_agbno;	/* set an agbno range for NEAR allocs */
	xfs_agblock_t	max_agbno;	/* ... */
	xfs_extlen_t	len;		/* output: actual size of extent */
	xfs_alloctype_t	type;		/* allocation type XFS_ALLOCTYPE_... */
	xfs_alloctype_t	otype;		/* original allocation type */
	int		datatype;	/* mask defining data type treatment */
	char		wasdel;		/* set if allocation was prev delayed */
	char		wasfromfl;	/* set if allocation is from freelist */
	struct xfs_owner_info	oinfo;	/* owner of blocks being allocated */
	enum xfs_ag_resv_type	resv;	/* block reservation to use */
} xfs_alloc_arg_t;

/*
 * Defines for datatype
 */
#define XFS_ALLOC_USERDATA		(1 << 0)/* allocation is for user data*/
#define XFS_ALLOC_INITIAL_USER_DATA	(1 << 1)/* special case start of file */
#define XFS_ALLOC_NOBUSY		(1 << 2)/* Busy extents not allowed */

/* freespace limit calculations */
#define XFS_ALLOC_AGFL_RESERVE	4
unsigned int xfs_alloc_set_aside(struct xfs_mount *mp);
unsigned int xfs_alloc_ag_max_usable(struct xfs_mount *mp);

xfs_extlen_t xfs_alloc_longest_free_extent(struct xfs_perag *pag,
		xfs_extlen_t need, xfs_extlen_t reserved);
unsigned int xfs_alloc_min_freelist(struct xfs_mount *mp,
		struct xfs_perag *pag);

/*
 * Compute and fill in value of m_ag_maxlevels.
 */
void
xfs_alloc_compute_maxlevels(
	struct xfs_mount	*mp);	/* file system mount structure */

/*
 * Get a block from the freelist.
 * Returns with the buffer for the block gotten.
 */
int				/* error */
xfs_alloc_get_freelist(
	struct xfs_trans *tp,	/* transaction pointer */
	struct xfs_buf	*agbp,	/* buffer containing the agf structure */
	xfs_agblock_t	*bnop,	/* block address retrieved from freelist */
	int		btreeblk); /* destination is a AGF btree */

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	struct xfs_trans *tp,	/* transaction pointer */
	struct xfs_buf	*bp,	/* buffer for a.g. freelist header */
	int		fields);/* mask of fields to be logged (XFS_AGF_...) */

/*
 * Interface for inode allocation to force the pag data to be initialized.
 */
int				/* error */
xfs_alloc_pagf_init(
	struct xfs_mount *mp,	/* file system mount structure */
	struct xfs_trans *tp,	/* transaction pointer */
	xfs_agnumber_t	agno,	/* allocation group number */
	int		flags);	/* XFS_ALLOC_FLAGS_... */

/*
 * Put the block on the freelist for the allocation group.
 */
int				/* error */
xfs_alloc_put_freelist(
	struct xfs_trans *tp,	/* transaction pointer */
	struct xfs_buf	*agbp,	/* buffer for a.g. freelist header */
	struct xfs_buf	*agflbp,/* buffer for a.g. free block array */
	xfs_agblock_t	bno,	/* block being freed */
	int		btreeblk); /* owner was a AGF btree */

/*
 * Read in the allocation group header (free/alloc section).
 */
int					/* error  */
xfs_alloc_read_agf(
	struct xfs_mount *mp,		/* mount point structure */
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	int		flags,		/* XFS_ALLOC_FLAG_... */
	struct xfs_buf	**bpp);		/* buffer for the ag freelist header */

/*
 * Allocate an extent (variable-size).
 */
int				/* error */
xfs_alloc_vextent(
	xfs_alloc_arg_t	*args);	/* allocation argument structure */

/*
 * Free an extent.
 */
int				/* error */
__xfs_free_extent(
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_fsblock_t		bno,	/* starting block number of extent */
	xfs_extlen_t		len,	/* length of extent */
	const struct xfs_owner_info	*oinfo,	/* extent owner */
	enum xfs_ag_resv_type	type,	/* block reservation type */
	bool			skip_discard);

static inline int
xfs_free_extent(
	struct xfs_trans	*tp,
	xfs_fsblock_t		bno,
	xfs_extlen_t		len,
	const struct xfs_owner_info	*oinfo,
	enum xfs_ag_resv_type	type)
{
	return __xfs_free_extent(tp, bno, len, oinfo, type, false);
}

int				/* error */
xfs_alloc_lookup_le(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len,	/* length of extent */
	int			*stat);	/* success/failure */

int				/* error */
xfs_alloc_lookup_ge(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len,	/* length of extent */
	int			*stat);	/* success/failure */

int					/* error */
xfs_alloc_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		*bno,	/* output: starting block of extent */
	xfs_extlen_t		*len,	/* output: length of extent */
	int			*stat);	/* output: success/failure */

int xfs_read_agf(struct xfs_mount *mp, struct xfs_trans *tp,
			xfs_agnumber_t agno, int flags, struct xfs_buf **bpp);
int xfs_alloc_read_agfl(struct xfs_mount *mp, struct xfs_trans *tp,
			xfs_agnumber_t agno, struct xfs_buf **bpp);
int xfs_free_agfl_block(struct xfs_trans *, xfs_agnumber_t, xfs_agblock_t,
			struct xfs_buf *, struct xfs_owner_info *);
int xfs_alloc_fix_freelist(struct xfs_alloc_arg *args, int flags);
int xfs_free_extent_fix_freelist(struct xfs_trans *tp, xfs_agnumber_t agno,
		struct xfs_buf **agbp);

xfs_extlen_t xfs_prealloc_blocks(struct xfs_mount *mp);

typedef int (*xfs_alloc_query_range_fn)(
	struct xfs_btree_cur		*cur,
	struct xfs_alloc_rec_incore	*rec,
	void				*priv);

int xfs_alloc_query_range(struct xfs_btree_cur *cur,
		struct xfs_alloc_rec_incore *low_rec,
		struct xfs_alloc_rec_incore *high_rec,
		xfs_alloc_query_range_fn fn, void *priv);
int xfs_alloc_query_all(struct xfs_btree_cur *cur, xfs_alloc_query_range_fn fn,
		void *priv);

int xfs_alloc_has_record(struct xfs_btree_cur *cur, xfs_agblock_t bno,
		xfs_extlen_t len, bool *exist);

typedef int (*xfs_agfl_walk_fn)(struct xfs_mount *mp, xfs_agblock_t bno,
		void *priv);
int xfs_agfl_walk(struct xfs_mount *mp, struct xfs_agf *agf,
		struct xfs_buf *agflbp, xfs_agfl_walk_fn walk_fn, void *priv);

static inline __be32 *
xfs_buf_to_agfl_bno(
	struct xfs_buf		*bp)
{
	if (xfs_sb_version_hascrc(&bp->b_mount->m_sb))
		return bp->b_addr + sizeof(struct xfs_agfl);
	return bp->b_addr;
}

#endif	/* __XFS_ALLOC_H__ */
