// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_RTBITMAP_H__
#define	__XFS_RTBITMAP_H__

#include "xfs_rtgroup.h"

struct xfs_rtalloc_args {
	struct xfs_rtgroup	*rtg;
	struct xfs_mount	*mp;
	struct xfs_trans	*tp;

	struct xfs_buf		*rbmbp;	/* bitmap block buffer */
	struct xfs_buf		*sumbp;	/* summary block buffer */

	xfs_fileoff_t		rbmoff;	/* bitmap block number */
	xfs_fileoff_t		sumoff;	/* summary block number */
};

static inline xfs_rtblock_t
xfs_rtx_to_rtb(
	struct xfs_rtgroup	*rtg,
	xfs_rtxnum_t		rtx)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_rtblock_t		start = xfs_group_start_fsb(rtg_group(rtg));

	if (mp->m_rtxblklog >= 0)
		return start + (rtx << mp->m_rtxblklog);
	return start + (rtx * mp->m_sb.sb_rextsize);
}

/* Convert an rgbno into an rt extent number. */
static inline xfs_rtxnum_t
xfs_rgbno_to_rtx(
	struct xfs_mount	*mp,
	xfs_rgblock_t		rgbno)
{
	if (likely(mp->m_rtxblklog >= 0))
		return rgbno >> mp->m_rtxblklog;
	return rgbno / mp->m_sb.sb_rextsize;
}

static inline uint64_t
xfs_rtbxlen_to_blen(
	struct xfs_mount	*mp,
	xfs_rtbxlen_t		rtbxlen)
{
	if (mp->m_rtxblklog >= 0)
		return rtbxlen << mp->m_rtxblklog;

	return rtbxlen * mp->m_sb.sb_rextsize;
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

/* Convert an rt block count into an rt extent count. */
static inline xfs_rtbxlen_t
xfs_blen_to_rtbxlen(
	struct xfs_mount	*mp,
	uint64_t		blen)
{
	if (likely(mp->m_rtxblklog >= 0))
		return blen >> mp->m_rtxblklog;

	return div_u64(blen, mp->m_sb.sb_rextsize);
}

/* Return the offset of a file block length within an rt extent. */
static inline xfs_extlen_t
xfs_blen_to_rtxoff(
	struct xfs_mount	*mp,
	xfs_filblks_t		blen)
{
	if (likely(mp->m_rtxblklog >= 0))
		return blen & mp->m_rtxblkmask;

	return do_div(blen, mp->m_sb.sb_rextsize);
}

/* Round this block count up to the nearest rt extent size. */
static inline xfs_filblks_t
xfs_blen_roundup_rtx(
	struct xfs_mount	*mp,
	xfs_filblks_t		blen)
{
	return roundup_64(blen, mp->m_sb.sb_rextsize);
}

/* Convert an rt block number into an rt extent number. */
static inline xfs_rtxnum_t
xfs_rtb_to_rtx(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	/* open-coded 64-bit masking operation */
	rtbno &= mp->m_groups[XG_TYPE_RTG].blkmask;
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
	/* open-coded 64-bit masking operation */
	rtbno &= mp->m_groups[XG_TYPE_RTG].blkmask;
	if (likely(mp->m_rtxblklog >= 0))
		return rtbno & mp->m_rtxblkmask;
	return do_div(rtbno, mp->m_sb.sb_rextsize);
}

/* Round this file block offset up to the nearest rt extent size. */
static inline xfs_rtblock_t
xfs_fileoff_roundup_rtx(
	struct xfs_mount	*mp,
	xfs_fileoff_t		off)
{
	return roundup_64(off, mp->m_sb.sb_rextsize);
}

/* Round this file block offset down to the nearest rt extent size. */
static inline xfs_rtblock_t
xfs_fileoff_rounddown_rtx(
	struct xfs_mount	*mp,
	xfs_fileoff_t		off)
{
	return rounddown_64(off, mp->m_sb.sb_rextsize);
}

/* Convert an rt extent number to a file block offset in the rt bitmap file. */
static inline xfs_fileoff_t
xfs_rtx_to_rbmblock(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		rtx)
{
	if (xfs_has_rtgroups(mp))
		return div_u64(rtx, mp->m_rtx_per_rbmblock);

	return rtx >> mp->m_blkbit_log;
}

/* Convert an rt extent number to a word offset within an rt bitmap block. */
static inline unsigned int
xfs_rtx_to_rbmword(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		rtx)
{
	if (xfs_has_rtgroups(mp)) {
		unsigned int	mod;

		div_u64_rem(rtx >> XFS_NBWORDLOG, mp->m_blockwsize, &mod);
		return mod;
	}

	return (rtx >> XFS_NBWORDLOG) & (mp->m_blockwsize - 1);
}

/* Convert a file block offset in the rt bitmap file to an rt extent number. */
static inline xfs_rtxnum_t
xfs_rbmblock_to_rtx(
	struct xfs_mount	*mp,
	xfs_fileoff_t		rbmoff)
{
	if (xfs_has_rtgroups(mp))
		return rbmoff * mp->m_rtx_per_rbmblock;

	return rbmoff << mp->m_blkbit_log;
}

/* Return a pointer to a bitmap word within a rt bitmap block. */
static inline union xfs_rtword_raw *
xfs_rbmblock_wordptr(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	struct xfs_mount	*mp = args->mp;
	union xfs_rtword_raw	*words;
	struct xfs_rtbuf_blkinfo *hdr = args->rbmbp->b_addr;

	if (xfs_has_rtgroups(mp))
		words = (union xfs_rtword_raw *)(hdr + 1);
	else
		words = args->rbmbp->b_addr;

	return words + index;
}

/* Convert an ondisk bitmap word to its incore representation. */
static inline xfs_rtword_t
xfs_rtbitmap_getword(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_rtword_raw	*word = xfs_rbmblock_wordptr(args, index);

	if (xfs_has_rtgroups(args->mp))
		return be32_to_cpu(word->rtg);
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

	if (xfs_has_rtgroups(args->mp))
		word->rtg = cpu_to_be32(value);
	else
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
	if (xfs_has_rtgroups(mp))
		return rsumoff / mp->m_blockwsize;

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

	if (xfs_has_rtgroups(mp))
		return rsumoff % mp->m_blockwsize;

	return rsumoff & mask;
}

/* Return a pointer to a summary info word within a rt summary block. */
static inline union xfs_suminfo_raw *
xfs_rsumblock_infoptr(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_suminfo_raw	*info;
	struct xfs_rtbuf_blkinfo *hdr = args->sumbp->b_addr;

	if (xfs_has_rtgroups(args->mp))
		info = (union xfs_suminfo_raw *)(hdr + 1);
	else
		info = args->sumbp->b_addr;

	return info + index;
}

/* Get the current value of a summary counter. */
static inline xfs_suminfo_t
xfs_suminfo_get(
	struct xfs_rtalloc_args	*args,
	unsigned int		index)
{
	union xfs_suminfo_raw	*info = xfs_rsumblock_infoptr(args, index);

	if (xfs_has_rtgroups(args->mp))
		return be32_to_cpu(info->rtg);
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

	if (xfs_has_rtgroups(args->mp)) {
		be32_add_cpu(&info->rtg, delta);
		return be32_to_cpu(info->rtg);
	}

	info->old += delta;
	return info->old;
}

static inline const struct xfs_buf_ops *
xfs_rtblock_ops(
	struct xfs_mount	*mp,
	enum xfs_rtg_inodes	type)
{
	if (xfs_has_rtgroups(mp)) {
		if (type == XFS_RTGI_SUMMARY)
			return &xfs_rtsummary_buf_ops;
		return &xfs_rtbitmap_buf_ops;
	}
	return &xfs_rtbuf_ops;
}

/*
 * Functions for walking free space rtextents in the realtime bitmap.
 */
struct xfs_rtalloc_rec {
	xfs_rtxnum_t		ar_startext;
	xfs_rtbxlen_t		ar_extcount;
};

typedef int (*xfs_rtalloc_query_range_fn)(
	struct xfs_rtgroup		*rtg,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*rec,
	void				*priv);

#ifdef CONFIG_XFS_RT
void xfs_rtbuf_cache_relse(struct xfs_rtalloc_args *args);
int xfs_rtbitmap_read_buf(struct xfs_rtalloc_args *args, xfs_fileoff_t block);
int xfs_rtsummary_read_buf(struct xfs_rtalloc_args *args, xfs_fileoff_t block);
int xfs_rtcheck_range(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxlen_t len, int val, xfs_rtxnum_t *new, int *stat);
int xfs_rtfind_back(struct xfs_rtalloc_args *args, xfs_rtxnum_t start,
		xfs_rtxnum_t *rtblock);
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
int xfs_rtalloc_query_range(struct xfs_rtgroup *rtg, struct xfs_trans *tp,
		xfs_rtxnum_t start, xfs_rtxnum_t end,
		xfs_rtalloc_query_range_fn fn, void *priv);
int xfs_rtalloc_query_all(struct xfs_rtgroup *rtg, struct xfs_trans *tp,
		xfs_rtalloc_query_range_fn fn, void *priv);
int xfs_rtalloc_extent_is_free(struct xfs_rtgroup *rtg, struct xfs_trans *tp,
		xfs_rtxnum_t start, xfs_rtxlen_t len, bool *is_free);
int xfs_rtfree_extent(struct xfs_trans *tp, struct xfs_rtgroup *rtg,
		xfs_rtxnum_t start, xfs_rtxlen_t len);
/* Same as above, but in units of rt blocks. */
int xfs_rtfree_blocks(struct xfs_trans *tp, struct xfs_rtgroup *rtg,
		xfs_fsblock_t rtbno, xfs_filblks_t rtlen);

xfs_rtxnum_t xfs_rtbitmap_rtx_per_rbmblock(struct xfs_mount *mp);
xfs_filblks_t xfs_rtbitmap_blockcount(struct xfs_mount *mp);
xfs_filblks_t xfs_rtbitmap_blockcount_len(struct xfs_mount *mp,
		xfs_rtbxlen_t rtextents);
xfs_filblks_t xfs_rtsummary_blockcount(struct xfs_mount *mp,
		unsigned int *rsumlevels);

int xfs_rtfile_initialize_blocks(struct xfs_rtgroup *rtg,
		enum xfs_rtg_inodes type, xfs_fileoff_t offset_fsb,
		xfs_fileoff_t end_fsb, void *data);
int xfs_rtbitmap_create(struct xfs_rtgroup *rtg, struct xfs_inode *ip,
		struct xfs_trans *tp, bool init);
int xfs_rtsummary_create(struct xfs_rtgroup *rtg, struct xfs_inode *ip,
		struct xfs_trans *tp, bool init);

#else /* CONFIG_XFS_RT */
# define xfs_rtfree_extent(t,b,l)			(-ENOSYS)

static inline int xfs_rtfree_blocks(struct xfs_trans *tp,
		struct xfs_rtgroup *rtg, xfs_fsblock_t rtbno,
		xfs_filblks_t rtlen)
{
	return -ENOSYS;
}
# define xfs_rtalloc_query_range(m,t,l,h,f,p)		(-ENOSYS)
# define xfs_rtalloc_query_all(m,t,f,p)			(-ENOSYS)
# define xfs_rtbitmap_read_buf(a,b)			(-ENOSYS)
# define xfs_rtsummary_read_buf(a,b)			(-ENOSYS)
# define xfs_rtbuf_cache_relse(a)			(0)
# define xfs_rtalloc_extent_is_free(m,t,s,l,i)		(-ENOSYS)
static inline xfs_filblks_t
xfs_rtbitmap_blockcount_len(struct xfs_mount *mp, xfs_rtbxlen_t rtextents)
{
	/* shut up gcc */
	return 0;
}
#endif /* CONFIG_XFS_RT */

#endif /* __XFS_RTBITMAP_H__ */
