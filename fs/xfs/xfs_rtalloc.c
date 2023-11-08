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
#include "xfs_trans.h"
#include "xfs_trans_space.h"
#include "xfs_icache.h"
#include "xfs_rtalloc.h"
#include "xfs_sb.h"
#include "xfs_rtbitmap.h"

/*
 * Read and return the summary information for a given extent size,
 * bitmap block combination.
 * Keeps track of a current summary block, so we don't keep reading
 * it from the buffer cache.
 */
static int
xfs_rtget_summary(
	struct xfs_rtalloc_args	*args,
	int			log,	/* log2 of extent size */
	xfs_fileoff_t		bbno,	/* bitmap block number */
	xfs_suminfo_t		*sum)	/* out: summary info for this block */
{
	return xfs_rtmodify_summary_int(args, log, bbno, 0, sum);
}

/*
 * Return whether there are any free extents in the size range given
 * by low and high, for the bitmap block bbno.
 */
STATIC int
xfs_rtany_summary(
	struct xfs_rtalloc_args	*args,
	int			low,	/* low log2 extent size */
	int			high,	/* high log2 extent size */
	xfs_fileoff_t		bbno,	/* bitmap block number */
	int			*maxlog) /* out: max log2 extent size free */
{
	struct xfs_mount	*mp = args->mp;
	int			error;
	int			log;	/* loop counter, log2 of ext. size */
	xfs_suminfo_t		sum;	/* summary data */

	/* There are no extents at levels >= m_rsum_cache[bbno]. */
	if (mp->m_rsum_cache) {
		high = min(high, mp->m_rsum_cache[bbno] - 1);
		if (low > high) {
			*maxlog = -1;
			return 0;
		}
	}

	/*
	 * Loop over logs of extent sizes.
	 */
	for (log = high; log >= low; log--) {
		/*
		 * Get one summary datum.
		 */
		error = xfs_rtget_summary(args, log, bbno, &sum);
		if (error) {
			return error;
		}
		/*
		 * If there are any, return success.
		 */
		if (sum) {
			*maxlog = log;
			goto out;
		}
	}
	/*
	 * Found nothing, return failure.
	 */
	*maxlog = -1;
out:
	/* There were no extents at levels > log. */
	if (mp->m_rsum_cache && log + 1 < mp->m_rsum_cache[bbno])
		mp->m_rsum_cache[bbno] = log + 1;
	return 0;
}


/*
 * Copy and transform the summary file, given the old and new
 * parameters in the mount structures.
 */
STATIC int
xfs_rtcopy_summary(
	struct xfs_rtalloc_args	*oargs,
	struct xfs_rtalloc_args	*nargs)
{
	xfs_fileoff_t		bbno;	/* bitmap block number */
	int			error;
	int			log;	/* summary level number (log length) */
	xfs_suminfo_t		sum;	/* summary data */

	for (log = oargs->mp->m_rsumlevels - 1; log >= 0; log--) {
		for (bbno = oargs->mp->m_sb.sb_rbmblocks - 1;
		     (xfs_srtblock_t)bbno >= 0;
		     bbno--) {
			error = xfs_rtget_summary(oargs, log, bbno, &sum);
			if (error)
				goto out;
			if (sum == 0)
				continue;
			error = xfs_rtmodify_summary(oargs, log, bbno, -sum);
			if (error)
				goto out;
			error = xfs_rtmodify_summary(nargs, log, bbno, sum);
			if (error)
				goto out;
			ASSERT(sum > 0);
		}
	}
	error = 0;
out:
	xfs_rtbuf_cache_relse(oargs);
	return 0;
}
/*
 * Mark an extent specified by start and len allocated.
 * Updates all the summary information as well as the bitmap.
 */
STATIC int
xfs_rtallocate_range(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* start rtext to allocate */
	xfs_rtxlen_t		len)	/* in/out: summary block number */
{
	struct xfs_mount	*mp = args->mp;
	xfs_rtxnum_t		end;	/* end of the allocated rtext */
	int			error;
	xfs_rtxnum_t		postblock = 0; /* first rtext allocated > end */
	xfs_rtxnum_t		preblock = 0; /* first rtext allocated < start */

	end = start + len - 1;
	/*
	 * Assume we're allocating out of the middle of a free extent.
	 * We need to find the beginning and end of the extent so we can
	 * properly update the summary.
	 */
	error = xfs_rtfind_back(args, start, 0, &preblock);
	if (error) {
		return error;
	}
	/*
	 * Find the next allocated block (end of free extent).
	 */
	error = xfs_rtfind_forw(args, end, mp->m_sb.sb_rextents - 1,
			&postblock);
	if (error) {
		return error;
	}
	/*
	 * Decrement the summary information corresponding to the entire
	 * (old) free extent.
	 */
	error = xfs_rtmodify_summary(args,
			XFS_RTBLOCKLOG(postblock + 1 - preblock),
			xfs_rtx_to_rbmblock(mp, preblock), -1);
	if (error) {
		return error;
	}
	/*
	 * If there are blocks not being allocated at the front of the
	 * old extent, add summary data for them to be free.
	 */
	if (preblock < start) {
		error = xfs_rtmodify_summary(args,
				XFS_RTBLOCKLOG(start - preblock),
				xfs_rtx_to_rbmblock(mp, preblock), 1);
		if (error) {
			return error;
		}
	}
	/*
	 * If there are blocks not being allocated at the end of the
	 * old extent, add summary data for them to be free.
	 */
	if (postblock > end) {
		error = xfs_rtmodify_summary(args,
				XFS_RTBLOCKLOG(postblock - end),
				xfs_rtx_to_rbmblock(mp, end + 1), 1);
		if (error) {
			return error;
		}
	}
	/*
	 * Modify the bitmap to mark this extent allocated.
	 */
	error = xfs_rtmodify_range(args, start, len, 0);
	return error;
}

/*
 * Make sure we don't run off the end of the rt volume.  Be careful that
 * adjusting maxlen downwards doesn't cause us to fail the alignment checks.
 */
static inline xfs_rtxlen_t
xfs_rtallocate_clamp_len(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		startrtx,
	xfs_rtxlen_t		rtxlen,
	xfs_rtxlen_t		prod)
{
	xfs_rtxlen_t		ret;

	ret = min(mp->m_sb.sb_rextents, startrtx + rtxlen) - startrtx;
	return rounddown(ret, prod);
}

/*
 * Attempt to allocate an extent minlen<=len<=maxlen starting from
 * bitmap block bbno.  If we don't get maxlen then use prod to trim
 * the length, if given.  Returns error; returns starting block in *rtx.
 * The lengths are all in rtextents.
 */
STATIC int
xfs_rtallocate_extent_block(
	struct xfs_rtalloc_args	*args,
	xfs_fileoff_t		bbno,	/* bitmap block number */
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	xfs_rtxnum_t		*nextp,	/* out: next rtext to try */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxnum_t		*rtx)	/* out: start rtext allocated */
{
	struct xfs_mount	*mp = args->mp;
	xfs_rtxnum_t		besti;	/* best rtext found so far */
	xfs_rtxnum_t		bestlen;/* best length found so far */
	xfs_rtxnum_t		end;	/* last rtext in chunk */
	int			error;
	xfs_rtxnum_t		i;	/* current rtext trying */
	xfs_rtxnum_t		next;	/* next rtext to try */
	int			stat;	/* status from internal calls */

	/*
	 * Loop over all the extents starting in this bitmap block,
	 * looking for one that's long enough.
	 */
	for (i = xfs_rbmblock_to_rtx(mp, bbno), besti = -1, bestlen = 0,
		end = xfs_rbmblock_to_rtx(mp, bbno + 1) - 1;
	     i <= end;
	     i++) {
		/* Make sure we don't scan off the end of the rt volume. */
		maxlen = xfs_rtallocate_clamp_len(mp, i, maxlen, prod);

		/*
		 * See if there's a free extent of maxlen starting at i.
		 * If it's not so then next will contain the first non-free.
		 */
		error = xfs_rtcheck_range(args, i, maxlen, 1, &next, &stat);
		if (error) {
			return error;
		}
		if (stat) {
			/*
			 * i for maxlen is all free, allocate and return that.
			 */
			error = xfs_rtallocate_range(args, i, maxlen);
			if (error) {
				return error;
			}
			*len = maxlen;
			*rtx = i;
			return 0;
		}
		/*
		 * In the case where we have a variable-sized allocation
		 * request, figure out how big this free piece is,
		 * and if it's big enough for the minimum, and the best
		 * so far, remember it.
		 */
		if (minlen < maxlen) {
			xfs_rtxnum_t	thislen;	/* this extent size */

			thislen = next - i;
			if (thislen >= minlen && thislen > bestlen) {
				besti = i;
				bestlen = thislen;
			}
		}
		/*
		 * If not done yet, find the start of the next free space.
		 */
		if (next < end) {
			error = xfs_rtfind_forw(args, next, end, &i);
			if (error) {
				return error;
			}
		} else
			break;
	}
	/*
	 * Searched the whole thing & didn't find a maxlen free extent.
	 */
	if (minlen < maxlen && besti != -1) {
		xfs_rtxlen_t	p;	/* amount to trim length by */

		/*
		 * If size should be a multiple of prod, make that so.
		 */
		if (prod > 1) {
			div_u64_rem(bestlen, prod, &p);
			if (p)
				bestlen -= p;
		}

		/*
		 * Allocate besti for bestlen & return that.
		 */
		error = xfs_rtallocate_range(args, besti, bestlen);
		if (error) {
			return error;
		}
		*len = bestlen;
		*rtx = besti;
		return 0;
	}
	/*
	 * Allocation failed.  Set *nextp to the next block to try.
	 */
	*nextp = next;
	*rtx = NULLRTEXTNO;
	return 0;
}

/*
 * Allocate an extent of length minlen<=len<=maxlen, starting at block
 * bno.  If we don't get maxlen then use prod to trim the length, if given.
 * Returns error; returns starting block in *rtx.
 * The lengths are all in rtextents.
 */
STATIC int
xfs_rtallocate_extent_exact(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext number to allocate */
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxnum_t		*rtx)	/* out: start rtext allocated */
{
	int			error;
	xfs_rtxlen_t		i;	/* extent length trimmed due to prod */
	int			isfree;	/* extent is free */
	xfs_rtxnum_t		next;	/* next rtext to try (dummy) */

	ASSERT(minlen % prod == 0);
	ASSERT(maxlen % prod == 0);
	/*
	 * Check if the range in question (for maxlen) is free.
	 */
	error = xfs_rtcheck_range(args, start, maxlen, 1, &next, &isfree);
	if (error) {
		return error;
	}
	if (isfree) {
		/*
		 * If it is, allocate it and return success.
		 */
		error = xfs_rtallocate_range(args, start, maxlen);
		if (error) {
			return error;
		}
		*len = maxlen;
		*rtx = start;
		return 0;
	}
	/*
	 * If not, allocate what there is, if it's at least minlen.
	 */
	maxlen = next - start;
	if (maxlen < minlen) {
		/*
		 * Failed, return failure status.
		 */
		*rtx = NULLRTEXTNO;
		return 0;
	}
	/*
	 * Trim off tail of extent, if prod is specified.
	 */
	if (prod > 1 && (i = maxlen % prod)) {
		maxlen -= i;
		if (maxlen < minlen) {
			/*
			 * Now we can't do it, return failure status.
			 */
			*rtx = NULLRTEXTNO;
			return 0;
		}
	}
	/*
	 * Allocate what we can and return it.
	 */
	error = xfs_rtallocate_range(args, start, maxlen);
	if (error) {
		return error;
	}
	*len = maxlen;
	*rtx = start;
	return 0;
}

/*
 * Allocate an extent of length minlen<=len<=maxlen, starting as near
 * to start as possible.  If we don't get maxlen then use prod to trim
 * the length, if given.  The lengths are all in rtextents.
 */
STATIC int
xfs_rtallocate_extent_near(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,	/* starting rtext number to allocate */
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxnum_t		*rtx)	/* out: start rtext allocated */
{
	struct xfs_mount	*mp = args->mp;
	int			maxlog;	/* max useful extent from summary */
	xfs_fileoff_t		bbno;	/* bitmap block number */
	int			error;
	int			i;	/* bitmap block offset (loop control) */
	int			j;	/* secondary loop control */
	int			log2len; /* log2 of minlen */
	xfs_rtxnum_t		n;	/* next rtext to try */
	xfs_rtxnum_t		r;	/* result rtext */

	ASSERT(minlen % prod == 0);
	ASSERT(maxlen % prod == 0);

	/*
	 * If the block number given is off the end, silently set it to
	 * the last block.
	 */
	if (start >= mp->m_sb.sb_rextents)
		start = mp->m_sb.sb_rextents - 1;

	/* Make sure we don't run off the end of the rt volume. */
	maxlen = xfs_rtallocate_clamp_len(mp, start, maxlen, prod);
	if (maxlen < minlen) {
		*rtx = NULLRTEXTNO;
		return 0;
	}

	/*
	 * Try the exact allocation first.
	 */
	error = xfs_rtallocate_extent_exact(args, start, minlen, maxlen, len,
			prod, &r);
	if (error) {
		return error;
	}
	/*
	 * If the exact allocation worked, return that.
	 */
	if (r != NULLRTEXTNO) {
		*rtx = r;
		return 0;
	}
	bbno = xfs_rtx_to_rbmblock(mp, start);
	i = 0;
	j = -1;
	ASSERT(minlen != 0);
	log2len = xfs_highbit32(minlen);
	/*
	 * Loop over all bitmap blocks (bbno + i is current block).
	 */
	for (;;) {
		/*
		 * Get summary information of extents of all useful levels
		 * starting in this bitmap block.
		 */
		error = xfs_rtany_summary(args, log2len, mp->m_rsumlevels - 1,
				bbno + i, &maxlog);
		if (error) {
			return error;
		}
		/*
		 * If there are any useful extents starting here, try
		 * allocating one.
		 */
		if (maxlog >= 0) {
			xfs_extlen_t maxavail =
				min_t(xfs_rtblock_t, maxlen,
				      (1ULL << (maxlog + 1)) - 1);
			/*
			 * On the positive side of the starting location.
			 */
			if (i >= 0) {
				/*
				 * Try to allocate an extent starting in
				 * this block.
				 */
				error = xfs_rtallocate_extent_block(args,
						bbno + i, minlen, maxavail, len,
						&n, prod, &r);
				if (error) {
					return error;
				}
				/*
				 * If it worked, return it.
				 */
				if (r != NULLRTEXTNO) {
					*rtx = r;
					return 0;
				}
			}
			/*
			 * On the negative side of the starting location.
			 */
			else {		/* i < 0 */
				int	maxblocks;

				/*
				 * Loop backwards to find the end of the extent
				 * we found in the realtime summary.
				 *
				 * maxblocks is the maximum possible number of
				 * bitmap blocks from the start of the extent
				 * to the end of the extent.
				 */
				if (maxlog == 0)
					maxblocks = 0;
				else if (maxlog < mp->m_blkbit_log)
					maxblocks = 1;
				else
					maxblocks = 2 << (maxlog - mp->m_blkbit_log);

				/*
				 * We need to check bbno + i + maxblocks down to
				 * bbno + i. We already checked bbno down to
				 * bbno + j + 1, so we don't need to check those
				 * again.
				 */
				j = min(i + maxblocks, j);
				for (; j >= i; j--) {
					error = xfs_rtallocate_extent_block(args,
							bbno + j, minlen,
							maxavail, len, &n, prod,
							&r);
					if (error) {
						return error;
					}
					/*
					 * If it works, return the extent.
					 */
					if (r != NULLRTEXTNO) {
						*rtx = r;
						return 0;
					}
				}
			}
		}
		/*
		 * Loop control.  If we were on the positive side, and there's
		 * still more blocks on the negative side, go there.
		 */
		if (i > 0 && (int)bbno - i >= 0)
			i = -i;
		/*
		 * If positive, and no more negative, but there are more
		 * positive, go there.
		 */
		else if (i > 0 && (int)bbno + i < mp->m_sb.sb_rbmblocks - 1)
			i++;
		/*
		 * If negative or 0 (just started), and there are positive
		 * blocks to go, go there.  The 0 case moves to block 1.
		 */
		else if (i <= 0 && (int)bbno - i < mp->m_sb.sb_rbmblocks - 1)
			i = 1 - i;
		/*
		 * If negative or 0 and there are more negative blocks,
		 * go there.
		 */
		else if (i <= 0 && (int)bbno + i > 0)
			i--;
		/*
		 * Must be done.  Return failure.
		 */
		else
			break;
	}
	*rtx = NULLRTEXTNO;
	return 0;
}

/*
 * Allocate an extent of length minlen<=len<=maxlen, with no position
 * specified.  If we don't get maxlen then use prod to trim
 * the length, if given.  The lengths are all in rtextents.
 */
STATIC int
xfs_rtallocate_extent_size(
	struct xfs_rtalloc_args	*args,
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxnum_t		*rtx)	/* out: start rtext allocated */
{
	struct xfs_mount	*mp = args->mp;
	int			error;
	xfs_fileoff_t		i;	/* bitmap block number */
	int			l;	/* level number (loop control) */
	xfs_rtxnum_t		n;	/* next rtext to be tried */
	xfs_rtxnum_t		r;	/* result rtext number */
	xfs_suminfo_t		sum;	/* summary information for extents */

	ASSERT(minlen % prod == 0);
	ASSERT(maxlen % prod == 0);
	ASSERT(maxlen != 0);

	/*
	 * Loop over all the levels starting with maxlen.
	 * At each level, look at all the bitmap blocks, to see if there
	 * are extents starting there that are long enough (>= maxlen).
	 * Note, only on the initial level can the allocation fail if
	 * the summary says there's an extent.
	 */
	for (l = xfs_highbit32(maxlen); l < mp->m_rsumlevels; l++) {
		/*
		 * Loop over all the bitmap blocks.
		 */
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			/*
			 * Get the summary for this level/block.
			 */
			error = xfs_rtget_summary(args, l, i, &sum);
			if (error) {
				return error;
			}
			/*
			 * Nothing there, on to the next block.
			 */
			if (!sum)
				continue;
			/*
			 * Try allocating the extent.
			 */
			error = xfs_rtallocate_extent_block(args, i, maxlen,
					maxlen, len, &n, prod, &r);
			if (error) {
				return error;
			}
			/*
			 * If it worked, return that.
			 */
			if (r != NULLRTEXTNO) {
				*rtx = r;
				return 0;
			}
			/*
			 * If the "next block to try" returned from the
			 * allocator is beyond the next bitmap block,
			 * skip to that bitmap block.
			 */
			if (xfs_rtx_to_rbmblock(mp, n) > i + 1)
				i = xfs_rtx_to_rbmblock(mp, n) - 1;
		}
	}
	/*
	 * Didn't find any maxlen blocks.  Try smaller ones, unless
	 * we're asking for a fixed size extent.
	 */
	if (minlen > --maxlen) {
		*rtx = NULLRTEXTNO;
		return 0;
	}
	ASSERT(minlen != 0);
	ASSERT(maxlen != 0);

	/*
	 * Loop over sizes, from maxlen down to minlen.
	 * This time, when we do the allocations, allow smaller ones
	 * to succeed.
	 */
	for (l = xfs_highbit32(maxlen); l >= xfs_highbit32(minlen); l--) {
		/*
		 * Loop over all the bitmap blocks, try an allocation
		 * starting in that block.
		 */
		for (i = 0; i < mp->m_sb.sb_rbmblocks; i++) {
			/*
			 * Get the summary information for this level/block.
			 */
			error =	xfs_rtget_summary(args, l, i, &sum);
			if (error) {
				return error;
			}
			/*
			 * If nothing there, go on to next.
			 */
			if (!sum)
				continue;
			/*
			 * Try the allocation.  Make sure the specified
			 * minlen/maxlen are in the possible range for
			 * this summary level.
			 */
			error = xfs_rtallocate_extent_block(args, i,
					XFS_RTMAX(minlen, 1 << l),
					XFS_RTMIN(maxlen, (1 << (l + 1)) - 1),
					len, &n, prod, &r);
			if (error) {
				return error;
			}
			/*
			 * If it worked, return that extent.
			 */
			if (r != NULLRTEXTNO) {
				*rtx = r;
				return 0;
			}
			/*
			 * If the "next block to try" returned from the
			 * allocator is beyond the next bitmap block,
			 * skip to that bitmap block.
			 */
			if (xfs_rtx_to_rbmblock(mp, n) > i + 1)
				i = xfs_rtx_to_rbmblock(mp, n) - 1;
		}
	}
	/*
	 * Got nothing, return failure.
	 */
	*rtx = NULLRTEXTNO;
	return 0;
}

/*
 * Allocate space to the bitmap or summary file, and zero it, for growfs.
 */
STATIC int
xfs_growfs_rt_alloc(
	struct xfs_mount	*mp,		/* file system mount point */
	xfs_extlen_t		oblocks,	/* old count of blocks */
	xfs_extlen_t		nblocks,	/* new count of blocks */
	struct xfs_inode	*ip)		/* inode (bitmap/summary) */
{
	xfs_fileoff_t		bno;		/* block number in file */
	struct xfs_buf		*bp;	/* temporary buffer for zeroing */
	xfs_daddr_t		d;		/* disk block address */
	int			error;		/* error return value */
	xfs_fsblock_t		fsbno;		/* filesystem block for bno */
	struct xfs_bmbt_irec	map;		/* block map output */
	int			nmap;		/* number of block maps */
	int			resblks;	/* space reservation */
	enum xfs_blft		buf_type;
	struct xfs_trans	*tp;

	if (ip == mp->m_rsumip)
		buf_type = XFS_BLFT_RTSUMMARY_BUF;
	else
		buf_type = XFS_BLFT_RTBITMAP_BUF;

	/*
	 * Allocate space to the file, as necessary.
	 */
	while (oblocks < nblocks) {
		resblks = XFS_GROWFSRT_SPACE_RES(mp, nblocks - oblocks);
		/*
		 * Reserve space & log for one extent added to the file.
		 */
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growrtalloc, resblks,
				0, 0, &tp);
		if (error)
			return error;
		/*
		 * Lock the inode.
		 */
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

		error = xfs_iext_count_may_overflow(ip, XFS_DATA_FORK,
				XFS_IEXT_ADD_NOSPLIT_CNT);
		if (error == -EFBIG)
			error = xfs_iext_count_upgrade(tp, ip,
					XFS_IEXT_ADD_NOSPLIT_CNT);
		if (error)
			goto out_trans_cancel;

		/*
		 * Allocate blocks to the bitmap file.
		 */
		nmap = 1;
		error = xfs_bmapi_write(tp, ip, oblocks, nblocks - oblocks,
					XFS_BMAPI_METADATA, 0, &map, &nmap);
		if (!error && nmap < 1)
			error = -ENOSPC;
		if (error)
			goto out_trans_cancel;
		/*
		 * Free any blocks freed up in the transaction, then commit.
		 */
		error = xfs_trans_commit(tp);
		if (error)
			return error;
		/*
		 * Now we need to clear the allocated blocks.
		 * Do this one block per transaction, to keep it simple.
		 */
		for (bno = map.br_startoff, fsbno = map.br_startblock;
		     bno < map.br_startoff + map.br_blockcount;
		     bno++, fsbno++) {
			/*
			 * Reserve log for one block zeroing.
			 */
			error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growrtzero,
					0, 0, 0, &tp);
			if (error)
				return error;
			/*
			 * Lock the bitmap inode.
			 */
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
			/*
			 * Get a buffer for the block.
			 */
			d = XFS_FSB_TO_DADDR(mp, fsbno);
			error = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
					mp->m_bsize, 0, &bp);
			if (error)
				goto out_trans_cancel;

			xfs_trans_buf_set_type(tp, bp, buf_type);
			bp->b_ops = &xfs_rtbuf_ops;
			memset(bp->b_addr, 0, mp->m_sb.sb_blocksize);
			xfs_trans_log_buf(tp, bp, 0, mp->m_sb.sb_blocksize - 1);
			/*
			 * Commit the transaction.
			 */
			error = xfs_trans_commit(tp);
			if (error)
				return error;
		}
		/*
		 * Go on to the next extent, if any.
		 */
		oblocks = map.br_startoff + map.br_blockcount;
	}

	return 0;

out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
}

static void
xfs_alloc_rsum_cache(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_extlen_t	rbmblocks)	/* number of rt bitmap blocks */
{
	/*
	 * The rsum cache is initialized to the maximum value, which is
	 * trivially an upper bound on the maximum level with any free extents.
	 * We can continue without the cache if it couldn't be allocated.
	 */
	mp->m_rsum_cache = kvmalloc(rbmblocks, GFP_KERNEL);
	if (mp->m_rsum_cache)
		memset(mp->m_rsum_cache, -1, rbmblocks);
	else
		xfs_warn(mp, "could not allocate realtime summary cache");
}

/*
 * Visible (exported) functions.
 */

/*
 * Grow the realtime area of the filesystem.
 */
int
xfs_growfs_rt(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_growfs_rt_t	*in)		/* growfs rt input struct */
{
	xfs_fileoff_t	bmbno;		/* bitmap block number */
	struct xfs_buf	*bp;		/* temporary buffer */
	int		error;		/* error return value */
	xfs_mount_t	*nmp;		/* new (fake) mount structure */
	xfs_rfsblock_t	nrblocks;	/* new number of realtime blocks */
	xfs_extlen_t	nrbmblocks;	/* new number of rt bitmap blocks */
	xfs_rtxnum_t	nrextents;	/* new number of realtime extents */
	uint8_t		nrextslog;	/* new log2 of sb_rextents */
	xfs_extlen_t	nrsumblocks;	/* new number of summary blocks */
	uint		nrsumlevels;	/* new rt summary levels */
	uint		nrsumsize;	/* new size of rt summary, bytes */
	xfs_sb_t	*nsbp;		/* new superblock */
	xfs_extlen_t	rbmblocks;	/* current number of rt bitmap blocks */
	xfs_extlen_t	rsumblocks;	/* current number of rt summary blks */
	xfs_sb_t	*sbp;		/* old superblock */
	uint8_t		*rsum_cache;	/* old summary cache */

	sbp = &mp->m_sb;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* Needs to have been mounted with an rt device. */
	if (!XFS_IS_REALTIME_MOUNT(mp))
		return -EINVAL;
	/*
	 * Mount should fail if the rt bitmap/summary files don't load, but
	 * we'll check anyway.
	 */
	if (!mp->m_rbmip || !mp->m_rsumip)
		return -EINVAL;

	/* Shrink not supported. */
	if (in->newblocks <= sbp->sb_rblocks)
		return -EINVAL;

	/* Can only change rt extent size when adding rt volume. */
	if (sbp->sb_rblocks > 0 && in->extsize != sbp->sb_rextsize)
		return -EINVAL;

	/* Range check the extent size. */
	if (XFS_FSB_TO_B(mp, in->extsize) > XFS_MAX_RTEXTSIZE ||
	    XFS_FSB_TO_B(mp, in->extsize) < XFS_MIN_RTEXTSIZE)
		return -EINVAL;

	/* Unsupported realtime features. */
	if (xfs_has_rmapbt(mp) || xfs_has_reflink(mp) || xfs_has_quota(mp))
		return -EOPNOTSUPP;

	nrblocks = in->newblocks;
	error = xfs_sb_validate_fsb_count(sbp, nrblocks);
	if (error)
		return error;
	/*
	 * Read in the last block of the device, make sure it exists.
	 */
	error = xfs_buf_read_uncached(mp->m_rtdev_targp,
				XFS_FSB_TO_BB(mp, nrblocks - 1),
				XFS_FSB_TO_BB(mp, 1), 0, &bp, NULL);
	if (error)
		return error;
	xfs_buf_relse(bp);

	/*
	 * Calculate new parameters.  These are the final values to be reached.
	 */
	nrextents = nrblocks;
	do_div(nrextents, in->extsize);
	nrbmblocks = xfs_rtbitmap_blockcount(mp, nrextents);
	nrextslog = xfs_highbit32(nrextents);
	nrsumlevels = nrextslog + 1;
	nrsumblocks = xfs_rtsummary_blockcount(mp, nrsumlevels, nrbmblocks);
	nrsumsize = XFS_FSB_TO_B(mp, nrsumblocks);
	/*
	 * New summary size can't be more than half the size of
	 * the log.  This prevents us from getting a log overflow,
	 * since we'll log basically the whole summary file at once.
	 */
	if (nrsumblocks > (mp->m_sb.sb_logblocks >> 1))
		return -EINVAL;
	/*
	 * Get the old block counts for bitmap and summary inodes.
	 * These can't change since other growfs callers are locked out.
	 */
	rbmblocks = XFS_B_TO_FSB(mp, mp->m_rbmip->i_disk_size);
	rsumblocks = XFS_B_TO_FSB(mp, mp->m_rsumip->i_disk_size);
	/*
	 * Allocate space to the bitmap and summary files, as necessary.
	 */
	error = xfs_growfs_rt_alloc(mp, rbmblocks, nrbmblocks, mp->m_rbmip);
	if (error)
		return error;
	error = xfs_growfs_rt_alloc(mp, rsumblocks, nrsumblocks, mp->m_rsumip);
	if (error)
		return error;

	rsum_cache = mp->m_rsum_cache;
	if (nrbmblocks != sbp->sb_rbmblocks)
		xfs_alloc_rsum_cache(mp, nrbmblocks);

	/*
	 * Allocate a new (fake) mount/sb.
	 */
	nmp = kmem_alloc(sizeof(*nmp), 0);
	/*
	 * Loop over the bitmap blocks.
	 * We will do everything one bitmap block at a time.
	 * Skip the current block if it is exactly full.
	 * This also deals with the case where there were no rtextents before.
	 */
	for (bmbno = sbp->sb_rbmblocks -
		     ((sbp->sb_rextents & ((1 << mp->m_blkbit_log) - 1)) != 0);
	     bmbno < nrbmblocks;
	     bmbno++) {
		struct xfs_rtalloc_args	args = {
			.mp		= mp,
		};
		struct xfs_rtalloc_args	nargs = {
			.mp		= nmp,
		};
		struct xfs_trans	*tp;
		xfs_rfsblock_t		nrblocks_step;

		*nmp = *mp;
		nsbp = &nmp->m_sb;
		/*
		 * Calculate new sb and mount fields for this round.
		 */
		nsbp->sb_rextsize = in->extsize;
		nmp->m_rtxblklog = -1; /* don't use shift or masking */
		nsbp->sb_rbmblocks = bmbno + 1;
		nrblocks_step = (bmbno + 1) * NBBY * nsbp->sb_blocksize *
				nsbp->sb_rextsize;
		nsbp->sb_rblocks = min(nrblocks, nrblocks_step);
		nsbp->sb_rextents = xfs_rtb_to_rtx(nmp, nsbp->sb_rblocks);
		ASSERT(nsbp->sb_rextents != 0);
		nsbp->sb_rextslog = xfs_highbit32(nsbp->sb_rextents);
		nrsumlevels = nmp->m_rsumlevels = nsbp->sb_rextslog + 1;
		nrsumblocks = xfs_rtsummary_blockcount(mp, nrsumlevels,
				nsbp->sb_rbmblocks);
		nmp->m_rsumsize = nrsumsize = XFS_FSB_TO_B(mp, nrsumblocks);
		/*
		 * Start a transaction, get the log reservation.
		 */
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growrtfree, 0, 0, 0,
				&tp);
		if (error)
			break;
		args.tp = tp;
		nargs.tp = tp;

		/*
		 * Lock out other callers by grabbing the bitmap inode lock.
		 */
		xfs_ilock(mp->m_rbmip, XFS_ILOCK_EXCL | XFS_ILOCK_RTBITMAP);
		xfs_trans_ijoin(tp, mp->m_rbmip, XFS_ILOCK_EXCL);
		/*
		 * Update the bitmap inode's size ondisk and incore.  We need
		 * to update the incore size so that inode inactivation won't
		 * punch what it thinks are "posteof" blocks.
		 */
		mp->m_rbmip->i_disk_size =
			nsbp->sb_rbmblocks * nsbp->sb_blocksize;
		i_size_write(VFS_I(mp->m_rbmip), mp->m_rbmip->i_disk_size);
		xfs_trans_log_inode(tp, mp->m_rbmip, XFS_ILOG_CORE);
		/*
		 * Get the summary inode into the transaction.
		 */
		xfs_ilock(mp->m_rsumip, XFS_ILOCK_EXCL | XFS_ILOCK_RTSUM);
		xfs_trans_ijoin(tp, mp->m_rsumip, XFS_ILOCK_EXCL);
		/*
		 * Update the summary inode's size.  We need to update the
		 * incore size so that inode inactivation won't punch what it
		 * thinks are "posteof" blocks.
		 */
		mp->m_rsumip->i_disk_size = nmp->m_rsumsize;
		i_size_write(VFS_I(mp->m_rsumip), mp->m_rsumip->i_disk_size);
		xfs_trans_log_inode(tp, mp->m_rsumip, XFS_ILOG_CORE);
		/*
		 * Copy summary data from old to new sizes.
		 * Do this when the real size (not block-aligned) changes.
		 */
		if (sbp->sb_rbmblocks != nsbp->sb_rbmblocks ||
		    mp->m_rsumlevels != nmp->m_rsumlevels) {
			error = xfs_rtcopy_summary(&args, &nargs);
			if (error)
				goto error_cancel;
		}
		/*
		 * Update superblock fields.
		 */
		if (nsbp->sb_rextsize != sbp->sb_rextsize)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTSIZE,
				nsbp->sb_rextsize - sbp->sb_rextsize);
		if (nsbp->sb_rbmblocks != sbp->sb_rbmblocks)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RBMBLOCKS,
				nsbp->sb_rbmblocks - sbp->sb_rbmblocks);
		if (nsbp->sb_rblocks != sbp->sb_rblocks)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RBLOCKS,
				nsbp->sb_rblocks - sbp->sb_rblocks);
		if (nsbp->sb_rextents != sbp->sb_rextents)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTENTS,
				nsbp->sb_rextents - sbp->sb_rextents);
		if (nsbp->sb_rextslog != sbp->sb_rextslog)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTSLOG,
				nsbp->sb_rextslog - sbp->sb_rextslog);
		/*
		 * Free new extent.
		 */
		error = xfs_rtfree_range(&nargs, sbp->sb_rextents,
				nsbp->sb_rextents - sbp->sb_rextents);
		xfs_rtbuf_cache_relse(&nargs);
		if (error) {
error_cancel:
			xfs_trans_cancel(tp);
			break;
		}
		/*
		 * Mark more blocks free in the superblock.
		 */
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS,
			nsbp->sb_rextents - sbp->sb_rextents);
		/*
		 * Update mp values into the real mp structure.
		 */
		mp->m_rsumlevels = nrsumlevels;
		mp->m_rsumsize = nrsumsize;

		error = xfs_trans_commit(tp);
		if (error)
			break;

		/* Ensure the mount RT feature flag is now set. */
		mp->m_features |= XFS_FEAT_REALTIME;
	}
	if (error)
		goto out_free;

	/* Update secondary superblocks now the physical grow has completed */
	error = xfs_update_secondary_sbs(mp);

out_free:
	/*
	 * Free the fake mp structure.
	 */
	kmem_free(nmp);

	/*
	 * If we had to allocate a new rsum_cache, we either need to free the
	 * old one (if we succeeded) or free the new one and restore the old one
	 * (if there was an error).
	 */
	if (rsum_cache != mp->m_rsum_cache) {
		if (error) {
			kmem_free(mp->m_rsum_cache);
			mp->m_rsum_cache = rsum_cache;
		} else {
			kmem_free(rsum_cache);
		}
	}

	return error;
}

/*
 * Allocate an extent in the realtime subvolume, with the usual allocation
 * parameters.  The length units are all in realtime extents, as is the
 * result block number.
 */
int
xfs_rtallocate_extent(
	struct xfs_trans	*tp,
	xfs_rtxnum_t		start,	/* starting rtext number to allocate */
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	int			wasdel,	/* was a delayed allocation extent */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxnum_t		*rtblock) /* out: start rtext allocated */
{
	struct xfs_rtalloc_args	args = {
		.mp		= tp->t_mountp,
		.tp		= tp,
	};
	int			error;	/* error value */
	xfs_rtxnum_t		r;	/* result allocated rtext */

	ASSERT(xfs_isilocked(args.mp->m_rbmip, XFS_ILOCK_EXCL));
	ASSERT(minlen > 0 && minlen <= maxlen);

	/*
	 * If prod is set then figure out what to do to minlen and maxlen.
	 */
	if (prod > 1) {
		xfs_rtxlen_t	i;

		if ((i = maxlen % prod))
			maxlen -= i;
		if ((i = minlen % prod))
			minlen += prod - i;
		if (maxlen < minlen) {
			*rtblock = NULLRTEXTNO;
			return 0;
		}
	}

retry:
	if (start == 0) {
		error = xfs_rtallocate_extent_size(&args, minlen,
				maxlen, len, prod, &r);
	} else {
		error = xfs_rtallocate_extent_near(&args, start, minlen,
				maxlen, len, prod, &r);
	}

	xfs_rtbuf_cache_relse(&args);
	if (error)
		return error;

	/*
	 * If it worked, update the superblock.
	 */
	if (r != NULLRTEXTNO) {
		long	slen = (long)*len;

		ASSERT(*len >= minlen && *len <= maxlen);
		if (wasdel)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FREXTENTS, -slen);
		else
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, -slen);
	} else if (prod > 1) {
		prod = 1;
		goto retry;
	}

	*rtblock = r;
	return 0;
}

/*
 * Initialize realtime fields in the mount structure.
 */
int				/* error */
xfs_rtmount_init(
	struct xfs_mount	*mp)	/* file system mount structure */
{
	struct xfs_buf		*bp;	/* buffer for last block of subvolume */
	struct xfs_sb		*sbp;	/* filesystem superblock copy in mount */
	xfs_daddr_t		d;	/* address of last block of subvolume */
	unsigned int		rsumblocks;
	int			error;

	sbp = &mp->m_sb;
	if (sbp->sb_rblocks == 0)
		return 0;
	if (mp->m_rtdev_targp == NULL) {
		xfs_warn(mp,
	"Filesystem has a realtime volume, use rtdev=device option");
		return -ENODEV;
	}
	mp->m_rsumlevels = sbp->sb_rextslog + 1;
	rsumblocks = xfs_rtsummary_blockcount(mp, mp->m_rsumlevels,
			mp->m_sb.sb_rbmblocks);
	mp->m_rsumsize = XFS_FSB_TO_B(mp, rsumblocks);
	mp->m_rbmip = mp->m_rsumip = NULL;
	/*
	 * Check that the realtime section is an ok size.
	 */
	d = (xfs_daddr_t)XFS_FSB_TO_BB(mp, mp->m_sb.sb_rblocks);
	if (XFS_BB_TO_FSB(mp, d) != mp->m_sb.sb_rblocks) {
		xfs_warn(mp, "realtime mount -- %llu != %llu",
			(unsigned long long) XFS_BB_TO_FSB(mp, d),
			(unsigned long long) mp->m_sb.sb_rblocks);
		return -EFBIG;
	}
	error = xfs_buf_read_uncached(mp->m_rtdev_targp,
					d - XFS_FSB_TO_BB(mp, 1),
					XFS_FSB_TO_BB(mp, 1), 0, &bp, NULL);
	if (error) {
		xfs_warn(mp, "realtime device size check failed");
		return error;
	}
	xfs_buf_relse(bp);
	return 0;
}

static int
xfs_rtalloc_count_frextent(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*rec,
	void				*priv)
{
	uint64_t			*valp = priv;

	*valp += rec->ar_extcount;
	return 0;
}

/*
 * Reinitialize the number of free realtime extents from the realtime bitmap.
 * Callers must ensure that there is no other activity in the filesystem.
 */
int
xfs_rtalloc_reinit_frextents(
	struct xfs_mount	*mp)
{
	uint64_t		val = 0;
	int			error;

	xfs_ilock(mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	error = xfs_rtalloc_query_all(mp, NULL, xfs_rtalloc_count_frextent,
			&val);
	xfs_iunlock(mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	if (error)
		return error;

	spin_lock(&mp->m_sb_lock);
	mp->m_sb.sb_frextents = val;
	spin_unlock(&mp->m_sb_lock);
	percpu_counter_set(&mp->m_frextents, mp->m_sb.sb_frextents);
	return 0;
}

/*
 * Read in the bmbt of an rt metadata inode so that we never have to load them
 * at runtime.  This enables the use of shared ILOCKs for rtbitmap scans.  Use
 * an empty transaction to avoid deadlocking on loops in the bmbt.
 */
static inline int
xfs_rtmount_iread_extents(
	struct xfs_inode	*ip,
	unsigned int		lock_class)
{
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc_empty(ip->i_mount, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL | lock_class);

	error = xfs_iread_extents(tp, ip, XFS_DATA_FORK);
	if (error)
		goto out_unlock;

	if (xfs_inode_has_attr_fork(ip)) {
		error = xfs_iread_extents(tp, ip, XFS_ATTR_FORK);
		if (error)
			goto out_unlock;
	}

out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL | lock_class);
	xfs_trans_cancel(tp);
	return error;
}

/*
 * Get the bitmap and summary inodes and the summary cache into the mount
 * structure at mount time.
 */
int					/* error */
xfs_rtmount_inodes(
	xfs_mount_t	*mp)		/* file system mount structure */
{
	int		error;		/* error return value */
	xfs_sb_t	*sbp;

	sbp = &mp->m_sb;
	error = xfs_iget(mp, NULL, sbp->sb_rbmino, 0, 0, &mp->m_rbmip);
	if (error)
		return error;
	ASSERT(mp->m_rbmip != NULL);

	error = xfs_rtmount_iread_extents(mp->m_rbmip, XFS_ILOCK_RTBITMAP);
	if (error)
		goto out_rele_bitmap;

	error = xfs_iget(mp, NULL, sbp->sb_rsumino, 0, 0, &mp->m_rsumip);
	if (error)
		goto out_rele_bitmap;
	ASSERT(mp->m_rsumip != NULL);

	error = xfs_rtmount_iread_extents(mp->m_rsumip, XFS_ILOCK_RTSUM);
	if (error)
		goto out_rele_summary;

	xfs_alloc_rsum_cache(mp, sbp->sb_rbmblocks);
	return 0;

out_rele_summary:
	xfs_irele(mp->m_rsumip);
out_rele_bitmap:
	xfs_irele(mp->m_rbmip);
	return error;
}

void
xfs_rtunmount_inodes(
	struct xfs_mount	*mp)
{
	kmem_free(mp->m_rsum_cache);
	if (mp->m_rbmip)
		xfs_irele(mp->m_rbmip);
	if (mp->m_rsumip)
		xfs_irele(mp->m_rsumip);
}

/*
 * Pick an extent for allocation at the start of a new realtime file.
 * Use the sequence number stored in the atime field of the bitmap inode.
 * Translate this to a fraction of the rtextents, and return the product
 * of rtextents and the fraction.
 * The fraction sequence is 0, 1/2, 1/4, 3/4, 1/8, ..., 7/8, 1/16, ...
 */
int						/* error */
xfs_rtpick_extent(
	xfs_mount_t		*mp,		/* file system mount point */
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_rtxlen_t		len,		/* allocation length (rtextents) */
	xfs_rtxnum_t		*pick)		/* result rt extent */
{
	xfs_rtxnum_t		b;		/* result rtext */
	int			log2;		/* log of sequence number */
	uint64_t		resid;		/* residual after log removed */
	uint64_t		seq;		/* sequence number of file creation */
	struct timespec64	ts;		/* timespec in inode */

	ASSERT(xfs_isilocked(mp->m_rbmip, XFS_ILOCK_EXCL));

	ts = inode_get_atime(VFS_I(mp->m_rbmip));
	if (!(mp->m_rbmip->i_diflags & XFS_DIFLAG_NEWRTBM)) {
		mp->m_rbmip->i_diflags |= XFS_DIFLAG_NEWRTBM;
		seq = 0;
	} else {
		seq = ts.tv_sec;
	}
	if ((log2 = xfs_highbit64(seq)) == -1)
		b = 0;
	else {
		resid = seq - (1ULL << log2);
		b = (mp->m_sb.sb_rextents * ((resid << 1) + 1ULL)) >>
		    (log2 + 1);
		if (b >= mp->m_sb.sb_rextents)
			div64_u64_rem(b, mp->m_sb.sb_rextents, &b);
		if (b + len > mp->m_sb.sb_rextents)
			b = mp->m_sb.sb_rextents - len;
	}
	ts.tv_sec = seq + 1;
	inode_set_atime_to_ts(VFS_I(mp->m_rbmip), ts);
	xfs_trans_log_inode(tp, mp->m_rbmip, XFS_ILOG_CORE);
	*pick = b;
	return 0;
}
