// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_RTBITMAP_H__
#define	__XFS_RTBITMAP_H__

struct xfs_rtalloc_args {
	struct xfs_mount	*mp;
	struct xfs_trans	*tp;

	struct xfs_buf		*rbmbp;	/* bitmap block buffer */
	struct xfs_buf		*sumbp;	/* summary block buffer */

	xfs_fileoff_t		rbmoff;	/* bitmap block number */
	xfs_fileoff_t		sumoff;	/* summary block number */
};

static inline xfs_rtblock_t
xfs_rtx_to_rtb(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		rtx)
{
	if (mp->m_rtxblklog >= 0)
		return rtx << mp->m_rtxblklog;

	return rtx * mp->m_sb.sb_rextsize;
}

static inline xfs_extlen_t
xfs_rtxlen_to_extlen(
	struct xfs_mount	*mp,
	xfs_rtxlen_t		rtxlen)
{
	if (mp->m_rtxblklog >= 0)
		return rtxlen << mp->m_rtxblklog;

	return rtxlen * mp->m_sb.sb_rextsize;
}

/* Compute the misalignment between an extent length and a realtime extent .*/
static inline unsigned int
xfs_extlen_to_rtxmod(
	struct xfs_mount	*mp,
	xfs_extlen_t		len)
{
	if (mp->m_rtxblklog >= 0)
		return len & mp->m_rtxblkmask;

	return len % mp->m_sb.sb_rextsize;
}

static inline xfs_rtxlen_t
xfs_extlen_to_rtxlen(
	struct xfs_mount	*mp,
	xfs_extlen_t		len)
{
	if (mp->m_rtxblklog >= 0)
		return len >> mp->m_rtxblklog;

	return len / mp->m_sb.sb_rextsize;
}

/* Convert an rt block number into an rt extent number. */
static inline xfs_rtxnum_t
xfs_rtb_to_rtx(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	if (likely(mp->m_rtxblklog >= 0))
		return rtbno >> mp->m_rtxblklog;

	return div_u64(rtbno, mp->m_sb.sb_rextsize);
}

/* Return the offset of an rt block number within an rt extent. */
static inline xfs_extlen_t
xfs_rtb_to_rtxoff(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	if (likely(mp->m_rtxblklog >= 0))
		return rtbno & mp->m_rtxblkmask;

	return do_div(rtbno, mp->m_sb.sb_rextsize);
}

/*
 * Crack an rt block number into an rt extent number and an offset within that
 * rt extent.  Returns the rt extent number directly and the offset in @off.
 */
static inline xfs_rtxnum_t
xfs_rtb_to_rtxrem(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno,
	xfs_extlen_t		*off)
{
	if (likely(mp->m_rtxblklog >= 0)) {
		*off = rtbno & mp->m_rtxblkmask;
		return rtbno >> mp->m_rtxblklog;
	}

	return div_u64_rem(rtbno, mp->m_sb.sb_rextsize, off);
}

/*
 * Convert an rt block number into an rt extent number, rounding up to the next
 * rt extent if the rt block is not aligned to an rt extent boundary.
 */
static inline xfs_rtxnum_t
xfs_rtb_to_rtxup(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	if (likely(mp->m_rtxblklog >= 0)) {
		if (rtbno & mp->m_rtxblkmask)
			return (rtbno >> mp->m_rtxblklog) + 1;
		return rtbno >> mp->m_rtxblklog;
	}

	if (do_div(rtbno, mp->m_sb.sb_rextsize))
		rtbno++;
	return rtbno;
}

/* Round this rtblock up to the nearest rt extent size. */
static inline xfs_rtblock_t
xfs_rtb_roundup_rtx(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return roundup_64(rtbno, mp->m_sb.sb_rextsize);
}

/* Round this rtblock down to the nearest rt extent size. */
static inline xfs_rtblock_t
xfs_rtb_rounddown_rtx(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return rounddown_64(rtbno, mp->m_sb.sb_rextsize);
}

/* Convert an rt extent number to a file block offset in the rt bitmap file. */
static inline xfs_fileoff_t
xfs_rtx_to_rbmblock(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		rtx)
{
	return rtx >> mp->m_blkbit_log;
}

/* Convert an rt extent number to a word offset within an rt bitmap block. */
static inline unsigned int
xfs_rtx_to_rbmword(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		rtx)
{
	return (rtx >> XFS_NBWORDLOG) & (mp->m_blockwsize - 1);
}

/* Convert a file block offset in the rt bitmap file to an rt extent number. */
static inline xfs_rtxnum_t
xfs_rbmblock_to_rtx(
	struct xfs_mount	*mp,
	xfs_fileoff_t		rbmoff)
{
	return rbmoff << mp->m_blkbit_log;
}

/* Return a pointer to a bitmap word within a rt bitmap block. */
static inline union xfs_rtword_raw *
xfs_rbmblock_wordptr(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_rtword_raw	*words = args->rbmbp->b_addr;

	return words + index;
}

/* Convert an ondisk bitmap word to its incore representation. */
static inline xfs_rtword_t
xfs_rtbitmap_getword(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_rtword_raw	*word = xfs_rbmblock_wordptr(args, index);

	return word->old;
}

/* Set an ondisk bitmap word from an incore representation. */
static inline void
xfs_rtbitmap_setword(
	struct xfs_rtalloc_args	*args,
	unsigned int		index,
	xfs_rtword_t		value)
{
	union xfs_rtword_raw	*word = xfs_rbmblock_wordptr(args, index);

	word->old = value;
}

/*
 * Convert a rt extent length and rt bitmap block number to a xfs_suminfo_t
 * offset within the rt summary file.
 */
static inline xfs_rtsumoff_t
xfs_rtsumoffs(
	struct xfs_mount	*mp,
	int			log2_len,
	xfs_fileoff_t		rbmoff)
{
	return log2_len * mp->m_sb.sb_rbmblocks + rbmoff;
}

/*
 * Convert an xfs_suminfo_t offset to a file block offset within the rt summary
 * file.
 */
static inline xfs_fileoff_t
xfs_rtsumoffs_to_block(
	struct xfs_mount	*mp,
	xfs_rtsumoff_t		rsumoff)
{
	return XFS_B_TO_FSBT(mp, rsumoff * sizeof(xfs_suminfo_t));
}

/*
 * Convert an xfs_suminfo_t offset to an info word offset within an rt summary
 * block.
 */
static inline unsigned int
xfs_rtsumoffs_to_infoword(
	struct xfs_mount	*mp,
	xfs_rtsumoff_t		rsumoff)
{
	unsigned int		mask = mp->m_blockmask >> XFS_SUMINFOLOG;

	return rsumoff & mask;
}

/* Return a pointer to a summary info word within a rt summary block. */
static inline union xfs_suminfo_raw *
xfs_rsumblock_infoptr(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_suminfo_raw	*info = args->sumbp->b_addr;

	return info + index;
}

/* Get the current value of a summary counter. */
static inline xfs_suminfo_t
xfs_suminfo_get(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_suminfo_raw	*info = xfs_rsumblock_infoptr(args, index);

	return info->old;
}

/* Add to the current value of a summary counter and return the new value. */
static inline xfs_suminfo_t
xfs_suminfo_add(
	struct xfs_rtalloc_args	*args,
	unsigned int		index,
	int			delta)
{
	union xfs_suminfo_raw	*info = xfs_rsumblock_infoptr(args, index);

	info->old += delta;
	return info->old;
}

/*
 * Functions for walking free space rtextents in the realtime bitmap.
 */
struct xfs_rtalloc_rec {
	xfs_rtxnum_t		ar_startext;
	xfs_rtbxlen_t		ar_extcount;
};

typedef int (*xfs_rtalloc_query_range_fn)(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*rec,
	void				*priv);

#ifdef CONFIG_XFS_RT
void xfs_rtbuf_cache_relse(struct xfs_rtalloc_args *args);

int xfs_rtbuf_get(struct xfs_rtalloc_args *args, xfs_fileoff_t block,
		int issum);

static inline int
xfs_rtbitmap_read_buf(
	struct xfs_rtalloc_args		*args,
	xfs_fileoff_t			block)
{
	return xfs_rtbuf_get(args, block, 0);
}

static inline int
xfs_rtsummary_read_buf(
	struct xfs_rtalloc_args		*args,
	xfs_fileoff_t			block)
{
	return xfs_rtbuf_get(args, block, 1);
}

int xfs_rtcheck_range(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxlen_t len, int val, xfs_rtxnum_t *new, int *stat);
int xfs_rtfind_back(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxnum_t limit, xfs_rtxnum_t *rtblock);
int xfs_rtfind_forw(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxnum_t limit, xfs_rtxnum_t *rtblock);
int xfs_rtmodify_range(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxlen_t len, int val);
int xfs_rtget_summary(struct xfs_rtalloc_args *args, int log,
		xfs_fileoff_t bbno, xfs_suminfo_t *sum);
int xfs_rtmodify_summary(struct xfs_rtalloc_args *args, int log,
		xfs_fileoff_t bbno, int delta);
int xfs_rtfree_range(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxlen_t len);
int xfs_rtalloc_query_range(struct xfs_mount *mp, struct xfs_trans *tp,
		const struct xfs_rtalloc_rec *low_rec,
		const struct xfs_rtalloc_rec *high_rec,
		xfs_rtalloc_query_range_fn fn, void *priv);
int xfs_rtalloc_query_all(struct xfs_mount *mp, struct xfs_trans *tp,
			  xfs_rtalloc_query_range_fn fn,
			  void *priv);
int xfs_rtalloc_extent_is_free(struct xfs_mount *mp, struct xfs_trans *tp,
			       xfs_rtxnum_t start, xfs_rtxlen_t len,
			       bool *is_free);
/*
 * Free an extent in the realtime subvolume.  Length is expressed in
 * realtime extents, as is the block number.
 */
int					/* error */
xfs_rtfree_extent(
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_rtxnum_t		start,	/* starting rtext number to free */
	xfs_rtxlen_t		len);	/* length of extent freed */

/* Same as above, but in units of rt blocks. */
int xfs_rtfree_blocks(struct xfs_trans *tp, xfs_fsblock_t rtbno,
		xfs_filblks_t rtlen);

xfs_filblks_t xfs_rtbitmap_blockcount(struct xfs_mount *mp, xfs_rtbxlen_t
		rtextents);
unsigned long long xfs_rtbitmap_wordcount(struct xfs_mount *mp,
		xfs_rtbxlen_t rtextents);

xfs_filblks_t xfs_rtsummary_blockcount(struct xfs_mount *mp,
		unsigned int rsumlevels, xfs_extlen_t rbmblocks);
unsigned long long xfs_rtsummary_wordcount(struct xfs_mount *mp,
		unsigned int rsumlevels, xfs_extlen_t rbmblocks);

void xfs_rtbitmap_lock(struct xfs_trans *tp, struct xfs_mount *mp);
void xfs_rtbitmap_unlock(struct xfs_mount *mp);

/* Lock the rt bitmap inode in shared mode */
#define XFS_RBMLOCK_BITMAP	(1U << 0)
/* Lock the rt summary inode in shared mode */
#define XFS_RBMLOCK_SUMMARY	(1U << 1)

void xfs_rtbitmap_lock_shared(struct xfs_mount *mp,
		unsigned int rbmlock_flags);
void xfs_rtbitmap_unlock_shared(struct xfs_mount *mp,
		unsigned int rbmlock_flags);
#else /* CONFIG_XFS_RT */
# define xfs_rtfree_extent(t,b,l)			(-ENOSYS)
# define xfs_rtfree_blocks(t,rb,rl)			(-ENOSYS)
# define xfs_rtalloc_query_range(m,t,l,h,f,p)		(-ENOSYS)
# define xfs_rtalloc_query_all(m,t,f,p)			(-ENOSYS)
# define xfs_rtbitmap_read_buf(a,b)			(-ENOSYS)
# define xfs_rtsummary_read_buf(a,b)			(-ENOSYS)
# define xfs_rtbuf_cache_relse(a)			(0)
# define xfs_rtalloc_extent_is_free(m,t,s,l,i)		(-ENOSYS)
static inline xfs_filblks_t
xfs_rtbitmap_blockcount(struct xfs_mount *mp, xfs_rtbxlen_t rtextents)
{
	/* shut up gcc */
	return 0;
}
# define xfs_rtbitmap_wordcount(mp, r)			(0)
# define xfs_rtsummary_blockcount(mp, l, b)		(0)
# define xfs_rtsummary_wordcount(mp, l, b)		(0)
# define xfs_rtbitmap_lock(tp, mp)		do { } while (0)
# define xfs_rtbitmap_unlock(mp)		do { } while (0)
# define xfs_rtbitmap_lock_shared(mp, lf)	do { } while (0)
# define xfs_rtbitmap_unlock_shared(mp, lf)	do { } while (0)
#endif /* CONFIG_XFS_RT */

#endif /* __XFS_RTBITMAP_H__ */
