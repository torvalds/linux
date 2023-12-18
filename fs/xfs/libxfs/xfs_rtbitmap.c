// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_trans.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_rtbitmap.h"

/*
 * Realtime allocator bitmap functions shared with userspace.
 */

/*
 * Real time buffers need verifiers to avoid runtime warnings during IO.
 * We don't have anything to verify, however, so these are just dummy
 * operations.
 */
static void
xfs_rtbuf_verify_read(
	struct xfs_buf	*bp)
{
	return;
}

static void
xfs_rtbuf_verify_write(
	struct xfs_buf	*bp)
{
	return;
}

const struct xfs_buf_ops xfs_rtbuf_ops = {
	.name = "rtbuf",
	.verify_read = xfs_rtbuf_verify_read,
	.verify_write = xfs_rtbuf_verify_write,
};

/* Release cached rt bitmap and summary buffers. */
void
xfs_rtbuf_cache_relse(
	struct xfs_rtalloc_args	*args)
{
	if (args->rbmbp) {
		xfs_trans_brelse(args->tp, args->rbmbp);
		args->rbmbp = NULL;
		args->rbmoff = NULLFILEOFF;
	}
	if (args->sumbp) {
		xfs_trans_brelse(args->tp, args->sumbp);
		args->sumbp = NULL;
		args->sumoff = NULLFILEOFF;
	}
}

/*
 * Get a buffer for the bitmap or summary file block specified.
 * The buffer is returned read and locked.
 */
int
xfs_rtbuf_get(
	struct xfs_rtalloc_args	*args,
	xfs_fileoff_t		block,	/* block number in bitmap or summary */
	int			issum)	/* is summary not bitmap */
{
	struct xfs_mount	*mp = args->mp;
	struct xfs_buf		**cbpp;	/* cached block buffer */
	xfs_fileoff_t		*coffp;	/* cached block number */
	struct xfs_buf		*bp;	/* block buffer, result */
	struct xfs_inode	*ip;	/* bitmap or summary inode */
	struct xfs_bmbt_irec	map;
	enum xfs_blft		type;
	int			nmap = 1;
	int			error;

	if (issum) {
		cbpp = &args->sumbp;
		coffp = &args->sumoff;
		ip = mp->m_rsumip;
		type = XFS_BLFT_RTSUMMARY_BUF;
	} else {
		cbpp = &args->rbmbp;
		coffp = &args->rbmoff;
		ip = mp->m_rbmip;
		type = XFS_BLFT_RTBITMAP_BUF;
	}

	/*
	 * If we have a cached buffer, and the block number matches, use that.
	 */
	if (*cbpp && *coffp == block)
		return 0;

	/*
	 * Otherwise we have to have to get the buffer.  If there was an old
	 * one, get rid of it first.
	 */
	if (*cbpp) {
		xfs_trans_brelse(args->tp, *cbpp);
		*cbpp = NULL;
	}

	error = xfs_bmapi_read(ip, block, 1, &map, &nmap, 0);
	if (error)
		return error;

	if (XFS_IS_CORRUPT(mp, nmap == 0 || !xfs_bmap_is_written_extent(&map)))
		return -EFSCORRUPTED;

	ASSERT(map.br_startblock != NULLFSBLOCK);
	error = xfs_trans_read_buf(mp, args->tp, mp->m_ddev_targp,
				   XFS_FSB_TO_DADDR(mp, map.br_startblock),
				   mp->m_bsize, 0, &bp, &xfs_rtbuf_ops);
	if (error)
		return error;

	xfs_trans_buf_set_type(args->tp, bp, type);
	*cbpp = bp;
	*coffp = block;
	return 0;
}

/*
 * Searching backward from start to limit, find the first block whose
 * allocated/free state is different from start's.
 */
int
xfs_rtfind_back(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext to look at */
	xfs_rtxnum_t		limit,	/* last rtext to look at */
	xfs_rtxnum_t		*rtx)	/* out: start rtext found */
{
	struct xfs_mount	*mp = args->mp;
	int			bit;	/* bit number in the word */
	xfs_fileoff_t		block;	/* bitmap block number */
	int			error;	/* error value */
	xfs_rtxnum_t		firstbit; /* first useful bit in the word */
	xfs_rtxnum_t		i;	/* current bit number rel. to start */
	xfs_rtxnum_t		len;	/* length of inspected area */
	xfs_rtword_t		mask;	/* mask of relevant bits for value */
	xfs_rtword_t		want;	/* mask for "good" values */
	xfs_rtword_t		wdiff;	/* difference from wanted value */
	xfs_rtword_t		incore;
	unsigned int		word;	/* word number in the buffer */

	/*
	 * Compute and read in starting bitmap block for starting block.
	 */
	block = xfs_rtx_to_rbmblock(mp, start);
	error = xfs_rtbitmap_read_buf(args, block);
	if (error)
		return error;

	/*
	 * Get the first word's index & point to it.
	 */
	word = xfs_rtx_to_rbmword(mp, start);
	bit = (int)(start & (XFS_NBWORD - 1));
	len = start - limit + 1;
	/*
	 * Compute match value, based on the bit at start: if 1 (free)
	 * then all-ones, else all-zeroes.
	 */
	incore = xfs_rtbitmap_getword(args, word);
	want = (incore & ((xfs_rtword_t)1 << bit)) ? -1 : 0;
	/*
	 * If the starting position is not word-aligned, deal with the
	 * partial word.
	 */
	if (bit < XFS_NBWORD - 1) {
		/*
		 * Calculate first (leftmost) bit number to look at,
		 * and mask for all the relevant bits in this word.
		 */
		firstbit = max_t(xfs_srtblock_t, bit - len + 1, 0);
		mask = (((xfs_rtword_t)1 << (bit - firstbit + 1)) - 1) <<
			firstbit;
		/*
		 * Calculate the difference between the value there
		 * and what we're looking for.
		 */
		if ((wdiff = (incore ^ want) & mask)) {
			/*
			 * Different.  Mark where we are and return.
			 */
			i = bit - xfs_highbit32(wdiff);
			*rtx = start - i + 1;
			return 0;
		}
		i = bit - firstbit + 1;
		/*
		 * Go on to previous block if that's where the previous word is
		 * and we need the previous word.
		 */
		if (--word == -1 && i < len) {
			/*
			 * If done with this block, get the previous one.
			 */
			error = xfs_rtbitmap_read_buf(args, --block);
			if (error)
				return error;

			word = mp->m_blockwsize - 1;
		}
	} else {
		/*
		 * Starting on a word boundary, no partial word.
		 */
		i = 0;
	}
	/*
	 * Loop over whole words in buffers.  When we use up one buffer
	 * we move on to the previous one.
	 */
	while (len - i >= XFS_NBWORD) {
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = incore ^ want)) {
			/*
			 * Different, mark where we are and return.
			 */
			i += XFS_NBWORD - 1 - xfs_highbit32(wdiff);
			*rtx = start - i + 1;
			return 0;
		}
		i += XFS_NBWORD;
		/*
		 * Go on to previous block if that's where the previous word is
		 * and we need the previous word.
		 */
		if (--word == -1 && i < len) {
			/*
			 * If done with this block, get the previous one.
			 */
			error = xfs_rtbitmap_read_buf(args, --block);
			if (error)
				return error;

			word = mp->m_blockwsize - 1;
		}
	}
	/*
	 * If not ending on a word boundary, deal with the last
	 * (partial) word.
	 */
	if (len - i) {
		/*
		 * Calculate first (leftmost) bit number to look at,
		 * and mask for all the relevant bits in this word.
		 */
		firstbit = XFS_NBWORD - (len - i);
		mask = (((xfs_rtword_t)1 << (len - i)) - 1) << firstbit;
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = (incore ^ want) & mask)) {
			/*
			 * Different, mark where we are and return.
			 */
			i += XFS_NBWORD - 1 - xfs_highbit32(wdiff);
			*rtx = start - i + 1;
			return 0;
		} else
			i = len;
	}
	/*
	 * No match, return that we scanned the whole area.
	 */
	*rtx = start - i + 1;
	return 0;
}

/*
 * Searching forward from start to limit, find the first block whose
 * allocated/free state is different from start's.
 */
int
xfs_rtfind_forw(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext to look at */
	xfs_rtxnum_t		limit,	/* last rtext to look at */
	xfs_rtxnum_t		*rtx)	/* out: start rtext found */
{
	struct xfs_mount	*mp = args->mp;
	int			bit;	/* bit number in the word */
	xfs_fileoff_t		block;	/* bitmap block number */
	int			error;
	xfs_rtxnum_t		i;	/* current bit number rel. to start */
	xfs_rtxnum_t		lastbit;/* last useful bit in the word */
	xfs_rtxnum_t		len;	/* length of inspected area */
	xfs_rtword_t		mask;	/* mask of relevant bits for value */
	xfs_rtword_t		want;	/* mask for "good" values */
	xfs_rtword_t		wdiff;	/* difference from wanted value */
	xfs_rtword_t		incore;
	unsigned int		word;	/* word number in the buffer */

	/*
	 * Compute and read in starting bitmap block for starting block.
	 */
	block = xfs_rtx_to_rbmblock(mp, start);
	error = xfs_rtbitmap_read_buf(args, block);
	if (error)
		return error;

	/*
	 * Get the first word's index & point to it.
	 */
	word = xfs_rtx_to_rbmword(mp, start);
	bit = (int)(start & (XFS_NBWORD - 1));
	len = limit - start + 1;
	/*
	 * Compute match value, based on the bit at start: if 1 (free)
	 * then all-ones, else all-zeroes.
	 */
	incore = xfs_rtbitmap_getword(args, word);
	want = (incore & ((xfs_rtword_t)1 << bit)) ? -1 : 0;
	/*
	 * If the starting position is not word-aligned, deal with the
	 * partial word.
	 */
	if (bit) {
		/*
		 * Calculate last (rightmost) bit number to look at,
		 * and mask for all the relevant bits in this word.
		 */
		lastbit = min(bit + len, XFS_NBWORD);
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;
		/*
		 * Calculate the difference between the value there
		 * and what we're looking for.
		 */
		if ((wdiff = (incore ^ want) & mask)) {
			/*
			 * Different.  Mark where we are and return.
			 */
			i = xfs_lowbit32(wdiff) - bit;
			*rtx = start + i - 1;
			return 0;
		}
		i = lastbit - bit;
		/*
		 * Go on to next block if that's where the next word is
		 * and we need the next word.
		 */
		if (++word == mp->m_blockwsize && i < len) {
			/*
			 * If done with this block, get the previous one.
			 */
			error = xfs_rtbitmap_read_buf(args, ++block);
			if (error)
				return error;

			word = 0;
		}
	} else {
		/*
		 * Starting on a word boundary, no partial word.
		 */
		i = 0;
	}
	/*
	 * Loop over whole words in buffers.  When we use up one buffer
	 * we move on to the next one.
	 */
	while (len - i >= XFS_NBWORD) {
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = incore ^ want)) {
			/*
			 * Different, mark where we are and return.
			 */
			i += xfs_lowbit32(wdiff);
			*rtx = start + i - 1;
			return 0;
		}
		i += XFS_NBWORD;
		/*
		 * Go on to next block if that's where the next word is
		 * and we need the next word.
		 */
		if (++word == mp->m_blockwsize && i < len) {
			/*
			 * If done with this block, get the next one.
			 */
			error = xfs_rtbitmap_read_buf(args, ++block);
			if (error)
				return error;

			word = 0;
		}
	}
	/*
	 * If not ending on a word boundary, deal with the last
	 * (partial) word.
	 */
	if ((lastbit = len - i)) {
		/*
		 * Calculate mask for all the relevant bits in this word.
		 */
		mask = ((xfs_rtword_t)1 << lastbit) - 1;
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = (incore ^ want) & mask)) {
			/*
			 * Different, mark where we are and return.
			 */
			i += xfs_lowbit32(wdiff);
			*rtx = start + i - 1;
			return 0;
		} else
			i = len;
	}
	/*
	 * No match, return that we scanned the whole area.
	 */
	*rtx = start + i - 1;
	return 0;
}

/* Log rtsummary counter at @infoword. */
static inline void
xfs_trans_log_rtsummary(
	struct xfs_rtalloc_args	*args,
	unsigned int		infoword)
{
	struct xfs_buf		*bp = args->sumbp;
	size_t			first, last;

	first = (void *)xfs_rsumblock_infoptr(args, infoword) - bp->b_addr;
	last = first + sizeof(xfs_suminfo_t) - 1;

	xfs_trans_log_buf(args->tp, bp, first, last);
}

/*
 * Modify the summary information for a given extent size, bitmap block
 * combination.
 */
int
xfs_rtmodify_summary(
	struct xfs_rtalloc_args	*args,
	int			log,	/* log2 of extent size */
	xfs_fileoff_t		bbno,	/* bitmap block number */
	int			delta)	/* in/out: summary block number */
{
	struct xfs_mount	*mp = args->mp;
	xfs_rtsumoff_t		so = xfs_rtsumoffs(mp, log, bbno);
	unsigned int		infoword;
	xfs_suminfo_t		val;
	int			error;

	error = xfs_rtsummary_read_buf(args, xfs_rtsumoffs_to_block(mp, so));
	if (error)
		return error;

	infoword = xfs_rtsumoffs_to_infoword(mp, so);
	val = xfs_suminfo_add(args, infoword, delta);

	if (mp->m_rsum_cache) {
		if (val == 0 && log + 1 == mp->m_rsum_cache[bbno])
			mp->m_rsum_cache[bbno] = log;
		if (val != 0 && log >= mp->m_rsum_cache[bbno])
			mp->m_rsum_cache[bbno] = log + 1;
	}

	xfs_trans_log_rtsummary(args, infoword);
	return 0;
}

/*
 * Read and return the summary information for a given extent size, bitmap block
 * combination.
 */
int
xfs_rtget_summary(
	struct xfs_rtalloc_args	*args,
	int			log,	/* log2 of extent size */
	xfs_fileoff_t		bbno,	/* bitmap block number */
	xfs_suminfo_t		*sum)	/* out: summary info for this block */
{
	struct xfs_mount	*mp = args->mp;
	xfs_rtsumoff_t		so = xfs_rtsumoffs(mp, log, bbno);
	int			error;

	error = xfs_rtsummary_read_buf(args, xfs_rtsumoffs_to_block(mp, so));
	if (!error)
		*sum = xfs_suminfo_get(args, xfs_rtsumoffs_to_infoword(mp, so));
	return error;
}

/* Log rtbitmap block from the word @from to the byte before @next. */
static inline void
xfs_trans_log_rtbitmap(
	struct xfs_rtalloc_args	*args,
	unsigned int		from,
	unsigned int		next)
{
	struct xfs_buf		*bp = args->rbmbp;
	size_t			first, last;

	first = (void *)xfs_rbmblock_wordptr(args, from) - bp->b_addr;
	last = ((void *)xfs_rbmblock_wordptr(args, next) - 1) - bp->b_addr;

	xfs_trans_log_buf(args->tp, bp, first, last);
}

/*
 * Set the given range of bitmap bits to the given value.
 * Do whatever I/O and logging is required.
 */
int
xfs_rtmodify_range(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext to modify */
	xfs_rtxlen_t		len,	/* length of extent to modify */
	int			val)	/* 1 for free, 0 for allocated */
{
	struct xfs_mount	*mp = args->mp;
	int			bit;	/* bit number in the word */
	xfs_fileoff_t		block;	/* bitmap block number */
	int			error;
	int			i;	/* current bit number rel. to start */
	int			lastbit; /* last useful bit in word */
	xfs_rtword_t		mask;	 /* mask of relevant bits for value */
	xfs_rtword_t		incore;
	unsigned int		firstword; /* first word used in the buffer */
	unsigned int		word;	/* word number in the buffer */

	/*
	 * Compute starting bitmap block number.
	 */
	block = xfs_rtx_to_rbmblock(mp, start);
	/*
	 * Read the bitmap block, and point to its data.
	 */
	error = xfs_rtbitmap_read_buf(args, block);
	if (error)
		return error;

	/*
	 * Compute the starting word's address, and starting bit.
	 */
	firstword = word = xfs_rtx_to_rbmword(mp, start);
	bit = (int)(start & (XFS_NBWORD - 1));
	/*
	 * 0 (allocated) => all zeroes; 1 (free) => all ones.
	 */
	val = -val;
	/*
	 * If not starting on a word boundary, deal with the first
	 * (partial) word.
	 */
	if (bit) {
		/*
		 * Compute first bit not changed and mask of relevant bits.
		 */
		lastbit = min(bit + len, XFS_NBWORD);
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;
		/*
		 * Set/clear the active bits.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if (val)
			incore |= mask;
		else
			incore &= ~mask;
		xfs_rtbitmap_setword(args, word, incore);
		i = lastbit - bit;
		/*
		 * Go on to the next block if that's where the next word is
		 * and we need the next word.
		 */
		if (++word == mp->m_blockwsize && i < len) {
			/*
			 * Log the changed part of this block.
			 * Get the next one.
			 */
			xfs_trans_log_rtbitmap(args, firstword, word);
			error = xfs_rtbitmap_read_buf(args, ++block);
			if (error)
				return error;

			firstword = word = 0;
		}
	} else {
		/*
		 * Starting on a word boundary, no partial word.
		 */
		i = 0;
	}
	/*
	 * Loop over whole words in buffers.  When we use up one buffer
	 * we move on to the next one.
	 */
	while (len - i >= XFS_NBWORD) {
		/*
		 * Set the word value correctly.
		 */
		xfs_rtbitmap_setword(args, word, val);
		i += XFS_NBWORD;
		/*
		 * Go on to the next block if that's where the next word is
		 * and we need the next word.
		 */
		if (++word == mp->m_blockwsize && i < len) {
			/*
			 * Log the changed part of this block.
			 * Get the next one.
			 */
			xfs_trans_log_rtbitmap(args, firstword, word);
			error = xfs_rtbitmap_read_buf(args, ++block);
			if (error)
				return error;

			firstword = word = 0;
		}
	}
	/*
	 * If not ending on a word boundary, deal with the last
	 * (partial) word.
	 */
	if ((lastbit = len - i)) {
		/*
		 * Compute a mask of relevant bits.
		 */
		mask = ((xfs_rtword_t)1 << lastbit) - 1;
		/*
		 * Set/clear the active bits.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if (val)
			incore |= mask;
		else
			incore &= ~mask;
		xfs_rtbitmap_setword(args, word, incore);
		word++;
	}
	/*
	 * Log any remaining changed bytes.
	 */
	if (word > firstword)
		xfs_trans_log_rtbitmap(args, firstword, word);
	return 0;
}

/*
 * Mark an extent specified by start and len freed.
 * Updates all the summary information as well as the bitmap.
 */
int
xfs_rtfree_range(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext to free */
	xfs_rtxlen_t		len)	/* in/out: summary block number */
{
	struct xfs_mount	*mp = args->mp;
	xfs_rtxnum_t		end;	/* end of the freed extent */
	int			error;	/* error value */
	xfs_rtxnum_t		postblock; /* first rtext freed > end */
	xfs_rtxnum_t		preblock;  /* first rtext freed < start */

	end = start + len - 1;
	/*
	 * Modify the bitmap to mark this extent freed.
	 */
	error = xfs_rtmodify_range(args, start, len, 1);
	if (error) {
		return error;
	}
	/*
	 * Assume we're freeing out of the middle of an allocated extent.
	 * We need to find the beginning and end of the extent so we can
	 * properly update the summary.
	 */
	error = xfs_rtfind_back(args, start, 0, &preblock);
	if (error) {
		return error;
	}
	/*
	 * Find the next allocated block (end of allocated extent).
	 */
	error = xfs_rtfind_forw(args, end, mp->m_sb.sb_rextents - 1,
			&postblock);
	if (error)
		return error;
	/*
	 * If there are blocks not being freed at the front of the
	 * old extent, add summary data for them to be allocated.
	 */
	if (preblock < start) {
		error = xfs_rtmodify_summary(args,
				xfs_highbit64(start - preblock),
				xfs_rtx_to_rbmblock(mp, preblock), -1);
		if (error) {
			return error;
		}
	}
	/*
	 * If there are blocks not being freed at the end of the
	 * old extent, add summary data for them to be allocated.
	 */
	if (postblock > end) {
		error = xfs_rtmodify_summary(args,
				xfs_highbit64(postblock - end),
				xfs_rtx_to_rbmblock(mp, end + 1), -1);
		if (error) {
			return error;
		}
	}
	/*
	 * Increment the summary information corresponding to the entire
	 * (new) free extent.
	 */
	return xfs_rtmodify_summary(args,
			xfs_highbit64(postblock + 1 - preblock),
			xfs_rtx_to_rbmblock(mp, preblock), 1);
}

/*
 * Check that the given range is either all allocated (val = 0) or
 * all free (val = 1).
 */
int
xfs_rtcheck_range(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext number of extent */
	xfs_rtxlen_t		len,	/* length of extent */
	int			val,	/* 1 for free, 0 for allocated */
	xfs_rtxnum_t		*new,	/* out: first rtext not matching */
	int			*stat)	/* out: 1 for matches, 0 for not */
{
	struct xfs_mount	*mp = args->mp;
	int			bit;	/* bit number in the word */
	xfs_fileoff_t		block;	/* bitmap block number */
	int			error;
	xfs_rtxnum_t		i;	/* current bit number rel. to start */
	xfs_rtxnum_t		lastbit; /* last useful bit in word */
	xfs_rtword_t		mask;	/* mask of relevant bits for value */
	xfs_rtword_t		wdiff;	/* difference from wanted value */
	xfs_rtword_t		incore;
	unsigned int		word;	/* word number in the buffer */

	/*
	 * Compute starting bitmap block number
	 */
	block = xfs_rtx_to_rbmblock(mp, start);
	/*
	 * Read the bitmap block.
	 */
	error = xfs_rtbitmap_read_buf(args, block);
	if (error)
		return error;

	/*
	 * Compute the starting word's address, and starting bit.
	 */
	word = xfs_rtx_to_rbmword(mp, start);
	bit = (int)(start & (XFS_NBWORD - 1));
	/*
	 * 0 (allocated) => all zero's; 1 (free) => all one's.
	 */
	val = -val;
	/*
	 * If not starting on a word boundary, deal with the first
	 * (partial) word.
	 */
	if (bit) {
		/*
		 * Compute first bit not examined.
		 */
		lastbit = min(bit + len, XFS_NBWORD);
		/*
		 * Mask of relevant bits.
		 */
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = (incore ^ val) & mask)) {
			/*
			 * Different, compute first wrong bit and return.
			 */
			i = xfs_lowbit32(wdiff) - bit;
			*new = start + i;
			*stat = 0;
			return 0;
		}
		i = lastbit - bit;
		/*
		 * Go on to next block if that's where the next word is
		 * and we need the next word.
		 */
		if (++word == mp->m_blockwsize && i < len) {
			/*
			 * If done with this block, get the next one.
			 */
			error = xfs_rtbitmap_read_buf(args, ++block);
			if (error)
				return error;

			word = 0;
		}
	} else {
		/*
		 * Starting on a word boundary, no partial word.
		 */
		i = 0;
	}
	/*
	 * Loop over whole words in buffers.  When we use up one buffer
	 * we move on to the next one.
	 */
	while (len - i >= XFS_NBWORD) {
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = incore ^ val)) {
			/*
			 * Different, compute first wrong bit and return.
			 */
			i += xfs_lowbit32(wdiff);
			*new = start + i;
			*stat = 0;
			return 0;
		}
		i += XFS_NBWORD;
		/*
		 * Go on to next block if that's where the next word is
		 * and we need the next word.
		 */
		if (++word == mp->m_blockwsize && i < len) {
			/*
			 * If done with this block, get the next one.
			 */
			error = xfs_rtbitmap_read_buf(args, ++block);
			if (error)
				return error;

			word = 0;
		}
	}
	/*
	 * If not ending on a word boundary, deal with the last
	 * (partial) word.
	 */
	if ((lastbit = len - i)) {
		/*
		 * Mask of relevant bits.
		 */
		mask = ((xfs_rtword_t)1 << lastbit) - 1;
		/*
		 * Compute difference between actual and desired value.
		 */
		incore = xfs_rtbitmap_getword(args, word);
		if ((wdiff = (incore ^ val) & mask)) {
			/*
			 * Different, compute first wrong bit and return.
			 */
			i += xfs_lowbit32(wdiff);
			*new = start + i;
			*stat = 0;
			return 0;
		} else
			i = len;
	}
	/*
	 * Successful, return.
	 */
	*new = start + i;
	*stat = 1;
	return 0;
}

#ifdef DEBUG
/*
 * Check that the given extent (block range) is allocated already.
 */
STATIC int
xfs_rtcheck_alloc_range(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext number of extent */
	xfs_rtxlen_t		len)	/* length of extent */
{
	xfs_rtxnum_t		new;	/* dummy for xfs_rtcheck_range */
	int			stat;
	int			error;

	error = xfs_rtcheck_range(args, start, len, 0, &new, &stat);
	if (error)
		return error;
	ASSERT(stat);
	return 0;
}
#else
#define xfs_rtcheck_alloc_range(a,b,l)	(0)
#endif
/*
 * Free an extent in the realtime subvolume.  Length is expressed in
 * realtime extents, as is the block number.
 */
int
xfs_rtfree_extent(
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_rtxnum_t		start,	/* starting rtext number to free */
	xfs_rtxlen_t		len)	/* length of extent freed */
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_rtalloc_args	args = {
		.mp		= mp,
		.tp		= tp,
	};
	int			error;
	struct timespec64	atime;

	ASSERT(mp->m_rbmip->i_itemp != NULL);
	ASSERT(xfs_isilocked(mp->m_rbmip, XFS_ILOCK_EXCL));

	error = xfs_rtcheck_alloc_range(&args, start, len);
	if (error)
		return error;

	/*
	 * Free the range of realtime blocks.
	 */
	error = xfs_rtfree_range(&args, start, len);
	if (error)
		goto out;

	/*
	 * Mark more blocks free in the superblock.
	 */
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, (long)len);
	/*
	 * If we've now freed all the blocks, reset the file sequence
	 * number to 0.
	 */
	if (tp->t_frextents_delta + mp->m_sb.sb_frextents ==
	    mp->m_sb.sb_rextents) {
		if (!(mp->m_rbmip->i_diflags & XFS_DIFLAG_NEWRTBM))
			mp->m_rbmip->i_diflags |= XFS_DIFLAG_NEWRTBM;

		atime = inode_get_atime(VFS_I(mp->m_rbmip));
		atime.tv_sec = 0;
		inode_set_atime_to_ts(VFS_I(mp->m_rbmip), atime);
		xfs_trans_log_inode(tp, mp->m_rbmip, XFS_ILOG_CORE);
	}
	error = 0;
out:
	xfs_rtbuf_cache_relse(&args);
	return error;
}

/*
 * Free some blocks in the realtime subvolume.  rtbno and rtlen are in units of
 * rt blocks, not rt extents; must be aligned to the rt extent size; and rtlen
 * cannot exceed XFS_MAX_BMBT_EXTLEN.
 */
int
xfs_rtfree_blocks(
	struct xfs_trans	*tp,
	xfs_fsblock_t		rtbno,
	xfs_filblks_t		rtlen)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_rtxnum_t		start;
	xfs_filblks_t		len;
	xfs_extlen_t		mod;

	ASSERT(rtlen <= XFS_MAX_BMBT_EXTLEN);

	len = xfs_rtb_to_rtxrem(mp, rtlen, &mod);
	if (mod) {
		ASSERT(mod == 0);
		return -EIO;
	}

	start = xfs_rtb_to_rtxrem(mp, rtbno, &mod);
	if (mod) {
		ASSERT(mod == 0);
		return -EIO;
	}

	return xfs_rtfree_extent(tp, start, len);
}

/* Find all the free records within a given range. */
int
xfs_rtalloc_query_range(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*low_rec,
	const struct xfs_rtalloc_rec	*high_rec,
	xfs_rtalloc_query_range_fn	fn,
	void				*priv)
{
	struct xfs_rtalloc_args		args = {
		.mp			= mp,
		.tp			= tp,
	};
	struct xfs_rtalloc_rec		rec;
	xfs_rtxnum_t			rtstart;
	xfs_rtxnum_t			rtend;
	xfs_rtxnum_t			high_key;
	int				is_free;
	int				error = 0;

	if (low_rec->ar_startext > high_rec->ar_startext)
		return -EINVAL;
	if (low_rec->ar_startext >= mp->m_sb.sb_rextents ||
	    low_rec->ar_startext == high_rec->ar_startext)
		return 0;

	high_key = min(high_rec->ar_startext, mp->m_sb.sb_rextents - 1);

	/* Iterate the bitmap, looking for discrepancies. */
	rtstart = low_rec->ar_startext;
	while (rtstart <= high_key) {
		/* Is the first block free? */
		error = xfs_rtcheck_range(&args, rtstart, 1, 1, &rtend,
				&is_free);
		if (error)
			break;

		/* How long does the extent go for? */
		error = xfs_rtfind_forw(&args, rtstart, high_key, &rtend);
		if (error)
			break;

		if (is_free) {
			rec.ar_startext = rtstart;
			rec.ar_extcount = rtend - rtstart + 1;

			error = fn(mp, tp, &rec, priv);
			if (error)
				break;
		}

		rtstart = rtend + 1;
	}

	xfs_rtbuf_cache_relse(&args);
	return error;
}

/* Find all the free records. */
int
xfs_rtalloc_query_all(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	xfs_rtalloc_query_range_fn	fn,
	void				*priv)
{
	struct xfs_rtalloc_rec		keys[2];

	keys[0].ar_startext = 0;
	keys[1].ar_startext = mp->m_sb.sb_rextents - 1;
	keys[0].ar_extcount = keys[1].ar_extcount = 0;

	return xfs_rtalloc_query_range(mp, tp, &keys[0], &keys[1], fn, priv);
}

/* Is the given extent all free? */
int
xfs_rtalloc_extent_is_free(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	xfs_rtxnum_t			start,
	xfs_rtxlen_t			len,
	bool				*is_free)
{
	struct xfs_rtalloc_args		args = {
		.mp			= mp,
		.tp			= tp,
	};
	xfs_rtxnum_t			end;
	int				matches;
	int				error;

	error = xfs_rtcheck_range(&args, start, len, 1, &end, &matches);
	xfs_rtbuf_cache_relse(&args);
	if (error)
		return error;

	*is_free = matches;
	return 0;
}

/*
 * Compute the number of rtbitmap blocks needed to track the given number of rt
 * extents.
 */
xfs_filblks_t
xfs_rtbitmap_blockcount(
	struct xfs_mount	*mp,
	xfs_rtbxlen_t		rtextents)
{
	return howmany_64(rtextents, NBBY * mp->m_sb.sb_blocksize);
}

/*
 * Compute the maximum level number of the realtime summary file, as defined by
 * mkfs.  The historic use of highbit32 on a 64-bit quantity prohibited correct
 * use of rt volumes with more than 2^32 extents.
 */
uint8_t
xfs_compute_rextslog(
	xfs_rtbxlen_t		rtextents)
{
	if (!rtextents)
		return 0;
	return xfs_highbit64(rtextents);
}

/*
 * Compute the number of rtbitmap words needed to populate every block of a
 * bitmap that is large enough to track the given number of rt extents.
 */
unsigned long long
xfs_rtbitmap_wordcount(
	struct xfs_mount	*mp,
	xfs_rtbxlen_t		rtextents)
{
	xfs_filblks_t		blocks;

	blocks = xfs_rtbitmap_blockcount(mp, rtextents);
	return XFS_FSB_TO_B(mp, blocks) >> XFS_WORDLOG;
}

/* Compute the number of rtsummary blocks needed to track the given rt space. */
xfs_filblks_t
xfs_rtsummary_blockcount(
	struct xfs_mount	*mp,
	unsigned int		rsumlevels,
	xfs_extlen_t		rbmblocks)
{
	unsigned long long	rsumwords;

	rsumwords = (unsigned long long)rsumlevels * rbmblocks;
	return XFS_B_TO_FSB(mp, rsumwords << XFS_WORDLOG);
}

/*
 * Compute the number of rtsummary info words needed to populate every block of
 * a summary file that is large enough to track the given rt space.
 */
unsigned long long
xfs_rtsummary_wordcount(
	struct xfs_mount	*mp,
	unsigned int		rsumlevels,
	xfs_extlen_t		rbmblocks)
{
	xfs_filblks_t		blocks;

	blocks = xfs_rtsummary_blockcount(mp, rsumlevels, rbmblocks);
	return XFS_FSB_TO_B(mp, blocks) >> XFS_WORDLOG;
}
