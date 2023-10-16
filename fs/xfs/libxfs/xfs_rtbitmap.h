// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_RTBITMAP_H__
#define	__XFS_RTBITMAP_H__

/*
 * XXX: Most of the realtime allocation functions deal in units of realtime
 * extents, not realtime blocks.  This looks funny when paired with the type
 * name and screams for a larger cleanup.
 */
struct xfs_rtalloc_rec {
	xfs_rtblock_t		ar_startext;
	xfs_rtblock_t		ar_extcount;
};

typedef int (*xfs_rtalloc_query_range_fn)(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*rec,
	void				*priv);

#ifdef CONFIG_XFS_RT
int xfs_rtbuf_get(struct xfs_mount *mp, struct xfs_trans *tp,
		  xfs_rtblock_t block, int issum, struct xfs_buf **bpp);
int xfs_rtcheck_range(struct xfs_mount *mp, struct xfs_trans *tp,
		      xfs_rtblock_t start, xfs_extlen_t len, int val,
		      xfs_rtblock_t *new, int *stat);
int xfs_rtfind_back(struct xfs_mount *mp, struct xfs_trans *tp,
		    xfs_rtblock_t start, xfs_rtblock_t limit,
		    xfs_rtblock_t *rtblock);
int xfs_rtfind_forw(struct xfs_mount *mp, struct xfs_trans *tp,
		    xfs_rtblock_t start, xfs_rtblock_t limit,
		    xfs_rtblock_t *rtblock);
int xfs_rtmodify_range(struct xfs_mount *mp, struct xfs_trans *tp,
		       xfs_rtblock_t start, xfs_extlen_t len, int val);
int xfs_rtmodify_summary_int(struct xfs_mount *mp, struct xfs_trans *tp,
			     int log, xfs_rtblock_t bbno, int delta,
			     struct xfs_buf **rbpp, xfs_fsblock_t *rsb,
			     xfs_suminfo_t *sum);
int xfs_rtmodify_summary(struct xfs_mount *mp, struct xfs_trans *tp, int log,
			 xfs_rtblock_t bbno, int delta, struct xfs_buf **rbpp,
			 xfs_fsblock_t *rsb);
int xfs_rtfree_range(struct xfs_mount *mp, struct xfs_trans *tp,
		     xfs_rtblock_t start, xfs_extlen_t len,
		     struct xfs_buf **rbpp, xfs_fsblock_t *rsb);
int xfs_rtalloc_query_range(struct xfs_mount *mp, struct xfs_trans *tp,
		const struct xfs_rtalloc_rec *low_rec,
		const struct xfs_rtalloc_rec *high_rec,
		xfs_rtalloc_query_range_fn fn, void *priv);
int xfs_rtalloc_query_all(struct xfs_mount *mp, struct xfs_trans *tp,
			  xfs_rtalloc_query_range_fn fn,
			  void *priv);
bool xfs_verify_rtbno(struct xfs_mount *mp, xfs_rtblock_t rtbno);
int xfs_rtalloc_extent_is_free(struct xfs_mount *mp, struct xfs_trans *tp,
			       xfs_rtblock_t start, xfs_extlen_t len,
			       bool *is_free);
/*
 * Free an extent in the realtime subvolume.  Length is expressed in
 * realtime extents, as is the block number.
 */
int					/* error */
xfs_rtfree_extent(
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_rtblock_t		bno,	/* starting block number to free */
	xfs_extlen_t		len);	/* length of extent freed */

/* Same as above, but in units of rt blocks. */
int xfs_rtfree_blocks(struct xfs_trans *tp, xfs_fsblock_t rtbno,
		xfs_filblks_t rtlen);
#else /* CONFIG_XFS_RT */
# define xfs_rtfree_extent(t,b,l)			(-ENOSYS)
# define xfs_rtfree_blocks(t,rb,rl)			(-ENOSYS)
# define xfs_rtalloc_query_range(m,t,l,h,f,p)		(-ENOSYS)
# define xfs_rtalloc_query_all(m,t,f,p)			(-ENOSYS)
# define xfs_rtbuf_get(m,t,b,i,p)			(-ENOSYS)
# define xfs_rtalloc_extent_is_free(m,t,s,l,i)		(-ENOSYS)
#endif /* CONFIG_XFS_RT */

#endif /* __XFS_RTBITMAP_H__ */
