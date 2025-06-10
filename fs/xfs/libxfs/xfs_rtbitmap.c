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
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_trans.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_rtbitmap.h"
#include "xfs_health.h"
#include "xfs_sb.h"
#include "xfs_errortag.h"
#include "xfs_log.h"
#include "xfs_buf_item.h"
#include "xfs_extent_busy.h"

/*
 * Realtime allocator bitmap functions shared with userspace.
 */

static xfs_failaddr_t
xfs_rtbuf_verify(
	struct xfs_buf			*bp)
{
	struct xfs_mount		*mp = bp->b_mount;
	struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;

	if (!xfs_verify_magic(bp, hdr->rt_magic))
		return __this_address;
	if (!xfs_has_rtgroups(mp))
		return __this_address;
	if (!xfs_has_crc(mp))
		return __this_address;
	if (!uuid_equal(&hdr->rt_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	if (hdr->rt_blkno != cpu_to_be64(xfs_buf_daddr(bp)))
		return __this_address;
	return NULL;
}

static void
xfs_rtbuf_verify_read(
	struct xfs_buf			*bp)
{
	struct xfs_mount		*mp = bp->b_mount;
	struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;
	xfs_failaddr_t			fa;

	if (!xfs_has_rtgroups(mp))
		return;

	if (!xfs_log_check_lsn(mp, be64_to_cpu(hdr->rt_lsn))) {
		fa = __this_address;
		goto fail;
	}

	if (!xfs_buf_verify_cksum(bp, XFS_RTBUF_CRC_OFF)) {
		fa = __this_address;
		goto fail;
	}

	fa = xfs_rtbuf_verify(bp);
	if (fa)
		goto fail;

	return;
fail:
	xfs_verifier_error(bp, -EFSCORRUPTED, fa);
}

static void
xfs_rtbuf_verify_write(
	struct xfs_buf	*bp)
{
	struct xfs_mount		*mp = bp->b_mount;
	struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;
	struct xfs_buf_log_item		*bip = bp->b_log_item;
	xfs_failaddr_t			fa;

	if (!xfs_has_rtgroups(mp))
		return;

	fa = xfs_rtbuf_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (bip)
		hdr->rt_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_RTBUF_CRC_OFF);
}

const struct xfs_buf_ops xfs_rtbuf_ops = {
	.name = "rtbuf",
	.verify_read = xfs_rtbuf_verify_read,
	.verify_write = xfs_rtbuf_verify_write,
};

const struct xfs_buf_ops xfs_rtbitmap_buf_ops = {
	.name		= "xfs_rtbitmap",
	.magic		= { 0, cpu_to_be32(XFS_RTBITMAP_MAGIC) },
	.verify_read	= xfs_rtbuf_verify_read,
	.verify_write	= xfs_rtbuf_verify_write,
	.verify_struct	= xfs_rtbuf_verify,
};

const struct xfs_buf_ops xfs_rtsummary_buf_ops = {
	.name		= "xfs_rtsummary",
	.magic		= { 0, cpu_to_be32(XFS_RTSUMMARY_MAGIC) },
	.verify_read	= xfs_rtbuf_verify_read,
	.verify_write	= xfs_rtbuf_verify_write,
	.verify_struct	= xfs_rtbuf_verify,
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
static int
xfs_rtbuf_get(
	struct xfs_rtalloc_args	*args,
	xfs_fileoff_t		block,	/* block number in bitmap or summary */
	enum xfs_rtg_inodes	type)
{
	struct xfs_inode	*ip = args->rtg->rtg_inodes[type];
	struct xfs_mount	*mp = args->mp;
	struct xfs_buf		**cbpp;	/* cached block buffer */
	xfs_fileoff_t		*coffp;	/* cached block number */
	struct xfs_buf		*bp;	/* block buffer, result */
	struct xfs_bmbt_irec	map;
	enum xfs_blft		buf_type;
	int			nmap = 1;
	int			error;

	switch (type) {
	case XFS_RTGI_SUMMARY:
		cbpp = &args->sumbp;
		coffp = &args->sumoff;
		buf_type = XFS_BLFT_RTSUMMARY_BUF;
		break;
	case XFS_RTGI_BITMAP:
		cbpp = &args->rbmbp;
		coffp = &args->rbmoff;
		buf_type = XFS_BLFT_RTBITMAP_BUF;
		break;
	default:
		return -EINVAL;
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

	if (XFS_IS_CORRUPT(mp, nmap == 0 || !xfs_bmap_is_written_extent(&map))) {
		xfs_rtginode_mark_sick(args->rtg, type);
		return -EFSCORRUPTED;
	}

	ASSERT(map.br_startblock != NULLFSBLOCK);
	error = xfs_trans_read_buf(mp, args->tp, mp->m_ddev_targp,
				   XFS_FSB_TO_DADDR(mp, map.br_startblock),
				   mp->m_bsize, 0, &bp,
				   xfs_rtblock_ops(mp, type));
	if (xfs_metadata_is_sick(error))
		xfs_rtginode_mark_sick(args->rtg, type);
	if (error)
		return error;

	if (xfs_has_rtgroups(mp)) {
		struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;

		if (hdr->rt_owner != cpu_to_be64(ip->i_ino)) {
			xfs_buf_mark_corrupt(bp);
			xfs_trans_brelse(args->tp, bp);
			xfs_rtginode_mark_sick(args->rtg, type);
			return -EFSCORRUPTED;
		}
	}

	xfs_trans_buf_set_type(args->tp, bp, buf_type);
	*cbpp = bp;
	*coffp = block;
	return 0;
}

int
xfs_rtbitmap_read_buf(
	struct xfs_rtalloc_args		*args,
	xfs_fileoff_t			block)
{
	struct xfs_mount		*mp = args->mp;

	if (XFS_IS_CORRUPT(mp, block >= mp->m_sb.sb_rbmblocks)) {
		xfs_rtginode_mark_sick(args->rtg, XFS_RTGI_BITMAP);
		return -EFSCORRUPTED;
	}

	return xfs_rtbuf_get(args, block, XFS_RTGI_BITMAP);
}

int
xfs_rtsummary_read_buf(
	struct xfs_rtalloc_args		*args,
	xfs_fileoff_t			block)
{
	struct xfs_mount		*mp = args->mp;

	if (XFS_IS_CORRUPT(mp, block >= mp->m_rsumblocks)) {
		xfs_rtginode_mark_sick(args->rtg, XFS_RTGI_SUMMARY);
		return -EFSCORRUPTED;
	}
	return xfs_rtbuf_get(args, block, XFS_RTGI_SUMMARY);
}

/*
 * Searching backward from start find the first block whose allocated/free state
 * is different from start's.
 */
int
xfs_rtfind_back(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext to look at */
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
	len = start + 1;
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

	ASSERT(start <= limit);

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
	uint8_t			*rsum_cache = args->rtg->rtg_rsum_cache;
	unsigned int		infoword;
	xfs_suminfo_t		val;
	int			error;

	error = xfs_rtsummary_read_buf(args, xfs_rtsumoffs_to_block(mp, so));
	if (error)
		return error;

	infoword = xfs_rtsumoffs_to_infoword(mp, so);
	val = xfs_suminfo_add(args, infoword, delta);

	if (rsum_cache) {
		if (val == 0 && log + 1 == rsum_cache[bbno])
			rsum_cache[bbno] = log;
		if (val != 0 && log >= rsum_cache[bbno])
			rsum_cache[bbno] = log + 1;
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
	error = xfs_rtfind_back(args, start, &preblock);
	if (error) {
		return error;
	}
	/*
	 * Find the next allocated block (end of allocated extent).
	 */
	error = xfs_rtfind_forw(args, end, args->rtg->rtg_extents - 1,
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
	struct xfs_rtgroup	*rtg,
	xfs_rtxnum_t		start,	/* starting rtext number to free */
	xfs_rtxlen_t		len)	/* length of extent freed */
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_inode	*rbmip = rtg_bitmap(rtg);
	struct xfs_rtalloc_args	args = {
		.mp		= mp,
		.tp		= tp,
		.rtg		= rtg,
	};
	int			error;
	struct timespec64	atime;

	ASSERT(rbmip->i_itemp != NULL);
	xfs_assert_ilocked(rbmip, XFS_ILOCK_EXCL);

	if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_FREE_EXTENT))
		return -EIO;

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
	 * number to 0 for pre-RTG file systems.
	 */
	if (!xfs_has_rtgroups(mp) &&
	    tp->t_frextents_delta + mp->m_sb.sb_frextents ==
	    mp->m_sb.sb_rextents) {
		if (!(rbmip->i_diflags & XFS_DIFLAG_NEWRTBM))
			rbmip->i_diflags |= XFS_DIFLAG_NEWRTBM;

		atime = inode_get_atime(VFS_I(rbmip));
		atime.tv_sec = 0;
		inode_set_atime_to_ts(VFS_I(rbmip), atime);
		xfs_trans_log_inode(tp, rbmip, XFS_ILOG_CORE);
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
	struct xfs_rtgroup	*rtg,
	xfs_fsblock_t		rtbno,
	xfs_filblks_t		rtlen)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_extlen_t		mod;
	int			error;

	ASSERT(!xfs_has_zoned(mp));
	ASSERT(rtlen <= XFS_MAX_BMBT_EXTLEN);

	mod = xfs_blen_to_rtxoff(mp, rtlen);
	if (mod) {
		ASSERT(mod == 0);
		return -EIO;
	}

	mod = xfs_rtb_to_rtxoff(mp, rtbno);
	if (mod) {
		ASSERT(mod == 0);
		return -EIO;
	}

	error = xfs_rtfree_extent(tp, rtg, xfs_rtb_to_rtx(mp, rtbno),
			xfs_extlen_to_rtxlen(mp, rtlen));
	if (error)
		return error;

	if (xfs_has_rtgroups(mp))
		xfs_extent_busy_insert(tp, rtg_group(rtg),
				xfs_rtb_to_rgbno(mp, rtbno), rtlen, 0);

	return 0;
}

/* Find all the free records within a given range. */
int
xfs_rtalloc_query_range(
	struct xfs_rtgroup		*rtg,
	struct xfs_trans		*tp,
	xfs_rtxnum_t			start,
	xfs_rtxnum_t			end,
	xfs_rtalloc_query_range_fn	fn,
	void				*priv)
{
	struct xfs_mount		*mp = rtg_mount(rtg);
	struct xfs_rtalloc_args		args = {
		.rtg			= rtg,
		.mp			= mp,
		.tp			= tp,
	};
	int				error = 0;

	if (start > end)
		return -EINVAL;
	if (start == end || start >= rtg->rtg_extents)
		return 0;

	end = min(end, rtg->rtg_extents - 1);

	if (xfs_has_zoned(mp))
		return -EINVAL;

	/* Iterate the bitmap, looking for discrepancies. */
	while (start <= end) {
		struct xfs_rtalloc_rec	rec;
		int			is_free;
		xfs_rtxnum_t		rtend;

		/* Is the first block free? */
		error = xfs_rtcheck_range(&args, start, 1, 1, &rtend,
				&is_free);
		if (error)
			break;

		/* How long does the extent go for? */
		error = xfs_rtfind_forw(&args, start, end, &rtend);
		if (error)
			break;

		if (is_free) {
			rec.ar_startext = start;
			rec.ar_extcount = rtend - start + 1;

			error = fn(rtg, tp, &rec, priv);
			if (error)
				break;
		}

		start = rtend + 1;
	}

	xfs_rtbuf_cache_relse(&args);
	return error;
}

/* Find all the free records. */
int
xfs_rtalloc_query_all(
	struct xfs_rtgroup		*rtg,
	struct xfs_trans		*tp,
	xfs_rtalloc_query_range_fn	fn,
	void				*priv)
{
	return xfs_rtalloc_query_range(rtg, tp, 0, rtg->rtg_extents - 1, fn,
			priv);
}

/* Is the given extent all free? */
int
xfs_rtalloc_extent_is_free(
	struct xfs_rtgroup		*rtg,
	struct xfs_trans		*tp,
	xfs_rtxnum_t			start,
	xfs_rtxlen_t			len,
	bool				*is_free)
{
	struct xfs_rtalloc_args		args = {
		.mp			= rtg_mount(rtg),
		.rtg			= rtg,
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

/* Compute the number of rt extents tracked by a single bitmap block. */
xfs_rtxnum_t
xfs_rtbitmap_rtx_per_rbmblock(
	struct xfs_mount	*mp)
{
	unsigned int		rbmblock_bytes = mp->m_sb.sb_blocksize;

	if (xfs_has_rtgroups(mp))
		rbmblock_bytes -= sizeof(struct xfs_rtbuf_blkinfo);

	return rbmblock_bytes * NBBY;
}

/*
 * Compute the number of rtbitmap blocks needed to track the given number of rt
 * extents.
 */
xfs_filblks_t
xfs_rtbitmap_blockcount_len(
	struct xfs_mount	*mp,
	xfs_rtbxlen_t		rtextents)
{
	if (xfs_has_zoned(mp))
		return 0;
	return howmany_64(rtextents, xfs_rtbitmap_rtx_per_rbmblock(mp));
}

/* How many rt extents does each rtbitmap file track? */
static inline xfs_rtbxlen_t
xfs_rtbitmap_bitcount(
	struct xfs_mount	*mp)
{
	if (!mp->m_sb.sb_rextents)
		return 0;

	/* rtgroup size can be nonzero even if rextents is zero */
	if (xfs_has_rtgroups(mp))
		return mp->m_sb.sb_rgextents;

	return mp->m_sb.sb_rextents;
}

/*
 * Compute the number of rtbitmap blocks used for a given file system.
 */
xfs_filblks_t
xfs_rtbitmap_blockcount(
	struct xfs_mount	*mp)
{
	return xfs_rtbitmap_blockcount_len(mp, xfs_rtbitmap_bitcount(mp));
}

/*
 * Compute the geometry of the rtsummary file needed to track the given rt
 * space.
 */
xfs_filblks_t
xfs_rtsummary_blockcount(
	struct xfs_mount	*mp,
	unsigned int		*rsumlevels)
{
	xfs_rtbxlen_t		rextents = xfs_rtbitmap_bitcount(mp);
	unsigned long long	rsumwords;

	if (xfs_has_zoned(mp)) {
		*rsumlevels = 0;
		return 0;
	}

	*rsumlevels = xfs_compute_rextslog(rextents) + 1;
	rsumwords = xfs_rtbitmap_blockcount_len(mp, rextents) * (*rsumlevels);
	return howmany_64(rsumwords, mp->m_blockwsize);
}

static int
xfs_rtfile_alloc_blocks(
	struct xfs_inode	*ip,
	xfs_fileoff_t		offset_fsb,
	xfs_filblks_t		count_fsb,
	struct xfs_bmbt_irec	*map)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			nmap = 1;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growrtalloc,
			XFS_GROWFSRT_SPACE_RES(mp, count_fsb), 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	error = xfs_iext_count_extend(tp, ip, XFS_DATA_FORK,
				XFS_IEXT_ADD_NOSPLIT_CNT);
	if (error)
		goto out_trans_cancel;

	error = xfs_bmapi_write(tp, ip, offset_fsb, count_fsb,
			XFS_BMAPI_METADATA, 0, map, &nmap);
	if (error)
		goto out_trans_cancel;

	return xfs_trans_commit(tp);

out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
}

/* Get a buffer for the block. */
static int
xfs_rtfile_initialize_block(
	struct xfs_rtgroup	*rtg,
	enum xfs_rtg_inodes	type,
	xfs_fsblock_t		fsbno,
	void			*data)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_inode	*ip = rtg->rtg_inodes[type];
	struct xfs_trans	*tp;
	struct xfs_buf		*bp;
	void			*bufdata;
	const size_t		copylen = mp->m_blockwsize << XFS_WORDLOG;
	enum xfs_blft		buf_type;
	int			error;

	if (type == XFS_RTGI_BITMAP)
		buf_type = XFS_BLFT_RTBITMAP_BUF;
	else if (type == XFS_RTGI_SUMMARY)
		buf_type = XFS_BLFT_RTSUMMARY_BUF;
	else
		return -EINVAL;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growrtzero, 0, 0, 0, &tp);
	if (error)
		return error;
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	error = xfs_trans_get_buf(tp, mp->m_ddev_targp,
			XFS_FSB_TO_DADDR(mp, fsbno), mp->m_bsize, 0, &bp);
	if (error) {
		xfs_trans_cancel(tp);
		return error;
	}
	bufdata = bp->b_addr;

	xfs_trans_buf_set_type(tp, bp, buf_type);
	bp->b_ops = xfs_rtblock_ops(mp, type);

	if (xfs_has_rtgroups(mp)) {
		struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;

		if (type == XFS_RTGI_BITMAP)
			hdr->rt_magic = cpu_to_be32(XFS_RTBITMAP_MAGIC);
		else
			hdr->rt_magic = cpu_to_be32(XFS_RTSUMMARY_MAGIC);
		hdr->rt_owner = cpu_to_be64(ip->i_ino);
		hdr->rt_blkno = cpu_to_be64(XFS_FSB_TO_DADDR(mp, fsbno));
		hdr->rt_lsn = 0;
		uuid_copy(&hdr->rt_uuid, &mp->m_sb.sb_meta_uuid);

		bufdata += sizeof(*hdr);
	}

	if (data)
		memcpy(bufdata, data, copylen);
	else
		memset(bufdata, 0, copylen);
	xfs_trans_log_buf(tp, bp, 0, mp->m_sb.sb_blocksize - 1);
	return xfs_trans_commit(tp);
}

/*
 * Allocate space to the bitmap or summary file, and zero it, for growfs.
 * @data must be a contiguous buffer large enough to fill all blocks in the
 * file; or NULL to initialize the contents to zeroes.
 */
int
xfs_rtfile_initialize_blocks(
	struct xfs_rtgroup	*rtg,
	enum xfs_rtg_inodes	type,
	xfs_fileoff_t		offset_fsb,	/* offset to start from */
	xfs_fileoff_t		end_fsb,	/* offset to allocate to */
	void			*data)		/* data to fill the blocks */
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	const size_t		copylen = mp->m_blockwsize << XFS_WORDLOG;

	while (offset_fsb < end_fsb) {
		struct xfs_bmbt_irec	map;
		xfs_filblks_t		i;
		int			error;

		error = xfs_rtfile_alloc_blocks(rtg->rtg_inodes[type],
				offset_fsb, end_fsb - offset_fsb, &map);
		if (error)
			return error;

		/*
		 * Now we need to clear the allocated blocks.
		 *
		 * Do this one block per transaction, to keep it simple.
		 */
		for (i = 0; i < map.br_blockcount; i++) {
			error = xfs_rtfile_initialize_block(rtg, type,
					map.br_startblock + i, data);
			if (error)
				return error;
			if (data)
				data += copylen;
		}

		offset_fsb = map.br_startoff + map.br_blockcount;
	}

	return 0;
}

int
xfs_rtbitmap_create(
	struct xfs_rtgroup	*rtg,
	struct xfs_inode	*ip,
	struct xfs_trans	*tp,
	bool			init)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	ip->i_disk_size = mp->m_sb.sb_rbmblocks * mp->m_sb.sb_blocksize;
	if (init && !xfs_has_rtgroups(mp)) {
		ip->i_diflags |= XFS_DIFLAG_NEWRTBM;
		inode_set_atime(VFS_I(ip), 0, 0);
	}
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}

int
xfs_rtsummary_create(
	struct xfs_rtgroup	*rtg,
	struct xfs_inode	*ip,
	struct xfs_trans	*tp,
	bool			init)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	ip->i_disk_size = mp->m_rsumblocks * mp->m_sb.sb_blocksize;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}
