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
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap_util.h"
#include "xfs_trans.h"
#include "xfs_trans_space.h"
#include "xfs_icache.h"
#include "xfs_rtalloc.h"
#include "xfs_sb.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_quota.h"
#include "xfs_log_priv.h"
#include "xfs_health.h"
#include "xfs_da_format.h"
#include "xfs_metafile.h"
#include "xfs_rtgroup.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_rtrefcount_btree.h"
#include "xfs_reflink.h"
#include "xfs_zone_alloc.h"

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
	uint8_t			*rsum_cache = args->rtg->rtg_rsum_cache;
	int			error;
	int			log;	/* loop counter, log2 of ext. size */
	xfs_suminfo_t		sum;	/* summary data */

	/* There are no extents at levels >= rsum_cache[bbno]. */
	if (rsum_cache) {
		high = min(high, rsum_cache[bbno] - 1);
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
	if (rsum_cache && log + 1 < rsum_cache[bbno])
		rsum_cache[bbno] = log + 1;
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
	error = xfs_rtfind_back(args, start, &preblock);
	if (error)
		return error;

	/*
	 * Find the next allocated block (end of free extent).
	 */
	error = xfs_rtfind_forw(args, end, args->rtg->rtg_extents - 1,
			&postblock);
	if (error)
		return error;

	/*
	 * Decrement the summary information corresponding to the entire
	 * (old) free extent.
	 */
	error = xfs_rtmodify_summary(args,
			xfs_highbit64(postblock + 1 - preblock),
			xfs_rtx_to_rbmblock(mp, preblock), -1);
	if (error)
		return error;

	/*
	 * If there are blocks not being allocated at the front of the
	 * old extent, add summary data for them to be free.
	 */
	if (preblock < start) {
		error = xfs_rtmodify_summary(args,
				xfs_highbit64(start - preblock),
				xfs_rtx_to_rbmblock(mp, preblock), 1);
		if (error)
			return error;
	}

	/*
	 * If there are blocks not being allocated at the end of the
	 * old extent, add summary data for them to be free.
	 */
	if (postblock > end) {
		error = xfs_rtmodify_summary(args,
				xfs_highbit64(postblock - end),
				xfs_rtx_to_rbmblock(mp, end + 1), 1);
		if (error)
			return error;
	}

	/*
	 * Modify the bitmap to mark this extent allocated.
	 */
	return xfs_rtmodify_range(args, start, len, 0);
}

/* Reduce @rtxlen until it is a multiple of @prod. */
static inline xfs_rtxlen_t
xfs_rtalloc_align_len(
	xfs_rtxlen_t	rtxlen,
	xfs_rtxlen_t	prod)
{
	if (unlikely(prod > 1))
		return rounddown(rtxlen, prod);
	return rtxlen;
}

/*
 * Make sure we don't run off the end of the rt volume.  Be careful that
 * adjusting maxlen downwards doesn't cause us to fail the alignment checks.
 */
static inline xfs_rtxlen_t
xfs_rtallocate_clamp_len(
	struct xfs_rtgroup	*rtg,
	xfs_rtxnum_t		startrtx,
	xfs_rtxlen_t		rtxlen,
	xfs_rtxlen_t		prod)
{
	xfs_rtxlen_t		ret;

	ret = min(rtg->rtg_extents, startrtx + rtxlen) - startrtx;
	return xfs_rtalloc_align_len(ret, prod);
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
	xfs_rtxnum_t		besti = -1; /* best rtext found so far */
	xfs_rtxnum_t		end;	/* last rtext in chunk */
	xfs_rtxnum_t		i;	/* current rtext trying */
	xfs_rtxnum_t		next;	/* next rtext to try */
	xfs_rtxlen_t		scanlen; /* number of free rtx to look for */
	xfs_rtxlen_t		bestlen = 0; /* best length found so far */
	int			stat;	/* status from internal calls */
	int			error;

	/*
	 * Loop over all the extents starting in this bitmap block up to the
	 * end of the rt volume, looking for one that's long enough.
	 */
	end = min(args->rtg->rtg_extents, xfs_rbmblock_to_rtx(mp, bbno + 1)) -
		1;
	for (i = xfs_rbmblock_to_rtx(mp, bbno); i <= end; i++) {
		/* Make sure we don't scan off the end of the rt volume. */
		scanlen = xfs_rtallocate_clamp_len(args->rtg, i, maxlen, prod);
		if (scanlen < minlen)
			break;

		/*
		 * See if there's a free extent of scanlen starting at i.
		 * If it's not so then next will contain the first non-free.
		 */
		error = xfs_rtcheck_range(args, i, scanlen, 1, &next, &stat);
		if (error)
			return error;
		if (stat) {
			/*
			 * i to scanlen is all free, allocate and return that.
			 */
			*len = scanlen;
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
		if (next >= end)
			break;
		error = xfs_rtfind_forw(args, next, end, &i);
		if (error)
			return error;
	}

	/* Searched the whole thing & didn't find a maxlen free extent. */
	if (besti == -1)
		goto nospace;

	/*
	 * Ensure bestlen is a multiple of prod, but don't return a too-short
	 * extent.
	 */
	bestlen = xfs_rtalloc_align_len(bestlen, prod);
	if (bestlen < minlen)
		goto nospace;

	/*
	 * Pick besti for bestlen & return that.
	 */
	*len = bestlen;
	*rtx = besti;
	return 0;
nospace:
	/* Allocation failed.  Set *nextp to the next block to try. */
	*nextp = next;
	return -ENOSPC;
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
	xfs_rtxnum_t		next;	/* next rtext to try (dummy) */
	xfs_rtxlen_t		alloclen; /* candidate length */
	xfs_rtxlen_t		scanlen; /* number of free rtx to look for */
	int			isfree;	/* extent is free */
	int			error;

	ASSERT(minlen % prod == 0);
	ASSERT(maxlen % prod == 0);

	/* Make sure we don't run off the end of the rt volume. */
	scanlen = xfs_rtallocate_clamp_len(args->rtg, start, maxlen, prod);
	if (scanlen < minlen)
		return -ENOSPC;

	/* Check if the range in question (for scanlen) is free. */
	error = xfs_rtcheck_range(args, start, scanlen, 1, &next, &isfree);
	if (error)
		return error;

	if (isfree) {
		/* start to scanlen is all free; allocate it. */
		*len = scanlen;
		*rtx = start;
		return 0;
	}

	/*
	 * If not, allocate what there is, if it's at least minlen.
	 */
	alloclen = next - start;
	if (alloclen < minlen)
		return -ENOSPC;

	/* Ensure alloclen is a multiple of prod. */
	alloclen = xfs_rtalloc_align_len(alloclen, prod);
	if (alloclen < minlen)
		return -ENOSPC;

	*len = alloclen;
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

	ASSERT(minlen % prod == 0);
	ASSERT(maxlen % prod == 0);

	/*
	 * If the block number given is off the end, silently set it to the last
	 * block.
	 */
	start = min(start, args->rtg->rtg_extents - 1);

	/*
	 * Try the exact allocation first.
	 */
	error = xfs_rtallocate_extent_exact(args, start, minlen, maxlen, len,
			prod, rtx);
	if (error != -ENOSPC)
		return error;

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
		if (error)
			return error;

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
						&n, prod, rtx);
				if (error != -ENOSPC)
					return error;
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
							rtx);
					if (error != -ENOSPC)
						return error;
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
	return -ENOSPC;
}

static int
xfs_rtalloc_sumlevel(
	struct xfs_rtalloc_args	*args,
	int			l,	/* level number */
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	xfs_rtxnum_t		*rtx)	/* out: start rtext allocated */
{
	xfs_fileoff_t		i;	/* bitmap block number */
	int			error;

	for (i = 0; i < args->mp->m_sb.sb_rbmblocks; i++) {
		xfs_suminfo_t	sum;	/* summary information for extents */
		xfs_rtxnum_t	n;	/* next rtext to be tried */

		error = xfs_rtget_summary(args, l, i, &sum);
		if (error)
			return error;

		/*
		 * Nothing there, on to the next block.
		 */
		if (!sum)
			continue;

		/*
		 * Try allocating the extent.
		 */
		error = xfs_rtallocate_extent_block(args, i, minlen, maxlen,
				len, &n, prod, rtx);
		if (error != -ENOSPC)
			return error;

		/*
		 * If the "next block to try" returned from the allocator is
		 * beyond the next bitmap block, skip to that bitmap block.
		 */
		if (xfs_rtx_to_rbmblock(args->mp, n) > i + 1)
			i = xfs_rtx_to_rbmblock(args->mp, n) - 1;
	}

	return -ENOSPC;
}

/*
 * Allocate an extent of length minlen<=len<=maxlen, with no position
 * specified.  If we don't get maxlen then use prod to trim
 * the length, if given.  The lengths are all in rtextents.
 */
static int
xfs_rtallocate_extent_size(
	struct xfs_rtalloc_args	*args,
	xfs_rtxlen_t		minlen,	/* minimum length to allocate */
	xfs_rtxlen_t		maxlen,	/* maximum length to allocate */
	xfs_rtxlen_t		*len,	/* out: actual length allocated */
	xfs_rtxlen_t		prod,	/* extent product factor */
	xfs_rtxnum_t		*rtx)	/* out: start rtext allocated */
{
	int			error;
	int			l;	/* level number (loop control) */

	ASSERT(minlen % prod == 0);
	ASSERT(maxlen % prod == 0);
	ASSERT(maxlen != 0);

	/*
	 * Loop over all the levels starting with maxlen.
	 *
	 * At each level, look at all the bitmap blocks, to see if there are
	 * extents starting there that are long enough (>= maxlen).
	 *
	 * Note, only on the initial level can the allocation fail if the
	 * summary says there's an extent.
	 */
	for (l = xfs_highbit32(maxlen); l < args->mp->m_rsumlevels; l++) {
		error = xfs_rtalloc_sumlevel(args, l, minlen, maxlen, prod, len,
				rtx);
		if (error != -ENOSPC)
			return error;
	}

	/*
	 * Didn't find any maxlen blocks.  Try smaller ones, unless we are
	 * looking for a fixed size extent.
	 */
	if (minlen > --maxlen)
		return -ENOSPC;
	ASSERT(minlen != 0);
	ASSERT(maxlen != 0);

	/*
	 * Loop over sizes, from maxlen down to minlen.
	 *
	 * This time, when we do the allocations, allow smaller ones to succeed,
	 * but make sure the specified minlen/maxlen are in the possible range
	 * for this summary level.
	 */
	for (l = xfs_highbit32(maxlen); l >= xfs_highbit32(minlen); l--) {
		error = xfs_rtalloc_sumlevel(args, l,
				max_t(xfs_rtxlen_t, minlen, 1 << l),
				min_t(xfs_rtxlen_t, maxlen, (1 << (l + 1)) - 1),
				prod, len, rtx);
		if (error != -ENOSPC)
			return error;
	}

	return -ENOSPC;
}

static void
xfs_rtunmount_rtg(
	struct xfs_rtgroup	*rtg)
{
	int			i;

	for (i = 0; i < XFS_RTGI_MAX; i++)
		xfs_rtginode_irele(&rtg->rtg_inodes[i]);
	if (!xfs_has_zoned(rtg_mount(rtg)))
		kvfree(rtg->rtg_rsum_cache);
}

static int
xfs_alloc_rsum_cache(
	struct xfs_rtgroup	*rtg,
	xfs_extlen_t		rbmblocks)
{
	/*
	 * The rsum cache is initialized to the maximum value, which is
	 * trivially an upper bound on the maximum level with any free extents.
	 */
	rtg->rtg_rsum_cache = kvmalloc(rbmblocks, GFP_KERNEL);
	if (!rtg->rtg_rsum_cache)
		return -ENOMEM;
	memset(rtg->rtg_rsum_cache, -1, rbmblocks);
	return 0;
}

/*
 * If we changed the rt extent size (meaning there was no rt volume previously)
 * and the root directory had EXTSZINHERIT and RTINHERIT set, it's possible
 * that the extent size hint on the root directory is no longer congruent with
 * the new rt extent size.  Log the rootdir inode to fix this.
 */
static int
xfs_growfs_rt_fixup_extsize(
	struct xfs_mount	*mp)
{
	struct xfs_inode	*ip = mp->m_rootip;
	struct xfs_trans	*tp;
	int			error = 0;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	if (!(ip->i_diflags & XFS_DIFLAG_RTINHERIT) ||
	    !(ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT))
		goto out_iolock;

	error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_ichange, 0, 0, false,
			&tp);
	if (error)
		goto out_iolock;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

out_iolock:
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}

/* Ensure that the rtgroup metadata inode is loaded, creating it if neeeded. */
static int
xfs_rtginode_ensure(
	struct xfs_rtgroup	*rtg,
	enum xfs_rtg_inodes	type)
{
	struct xfs_trans	*tp;
	int			error;

	if (rtg->rtg_inodes[type])
		return 0;

	error = xfs_trans_alloc_empty(rtg_mount(rtg), &tp);
	if (error)
		return error;
	error = xfs_rtginode_load(rtg, type, tp);
	xfs_trans_cancel(tp);

	if (error != -ENOENT)
		return 0;
	return xfs_rtginode_create(rtg, type, true);
}

static struct xfs_mount *
xfs_growfs_rt_alloc_fake_mount(
	const struct xfs_mount	*mp,
	xfs_rfsblock_t		rblocks,
	xfs_agblock_t		rextsize)
{
	struct xfs_mount	*nmp;

	nmp = kmemdup(mp, sizeof(*mp), GFP_KERNEL);
	if (!nmp)
		return NULL;
	xfs_mount_sb_set_rextsize(nmp, &nmp->m_sb, rextsize);
	nmp->m_sb.sb_rblocks = rblocks;
	nmp->m_sb.sb_rextents = xfs_blen_to_rtbxlen(nmp, nmp->m_sb.sb_rblocks);
	nmp->m_sb.sb_rbmblocks = xfs_rtbitmap_blockcount(nmp);
	nmp->m_sb.sb_rextslog = xfs_compute_rextslog(nmp->m_sb.sb_rextents);
	if (xfs_has_rtgroups(nmp))
		nmp->m_sb.sb_rgcount = howmany_64(nmp->m_sb.sb_rextents,
						  nmp->m_sb.sb_rgextents);
	else
		nmp->m_sb.sb_rgcount = 1;
	nmp->m_rsumblocks = xfs_rtsummary_blockcount(nmp, &nmp->m_rsumlevels);

	if (rblocks > 0)
		nmp->m_features |= XFS_FEAT_REALTIME;

	/* recompute growfsrt reservation from new rsumsize */
	xfs_trans_resv_calc(nmp, &nmp->m_resv);
	return nmp;
}

/* Free all the new space and return the number of extents actually freed. */
static int
xfs_growfs_rt_free_new(
	struct xfs_rtgroup	*rtg,
	struct xfs_rtalloc_args	*nargs,
	xfs_rtbxlen_t		*freed_rtx)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_rgnumber_t		rgno = rtg_rgno(rtg);
	xfs_rtxnum_t		start_rtx = 0, end_rtx;

	if (rgno < mp->m_sb.sb_rgcount)
		start_rtx = xfs_rtgroup_extents(mp, rgno);
	end_rtx = xfs_rtgroup_extents(nargs->mp, rgno);

	/*
	 * Compute the first new extent that we want to free, being careful to
	 * skip past a realtime superblock at the start of the realtime volume.
	 */
	if (xfs_has_rtsb(nargs->mp) && rgno == 0 && start_rtx == 0)
		start_rtx++;
	*freed_rtx = end_rtx - start_rtx;
	return xfs_rtfree_range(nargs, start_rtx, *freed_rtx);
}

static xfs_rfsblock_t
xfs_growfs_rt_nrblocks(
	struct xfs_rtgroup	*rtg,
	xfs_rfsblock_t		nrblocks,
	xfs_agblock_t		rextsize,
	xfs_fileoff_t		bmbno)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_rfsblock_t		step;

	step = (bmbno + 1) * mp->m_rtx_per_rbmblock * rextsize;
	if (xfs_has_rtgroups(mp)) {
		xfs_rfsblock_t	rgblocks = mp->m_sb.sb_rgextents * rextsize;

		step = min(rgblocks, step) + rgblocks * rtg_rgno(rtg);
	}

	return min(nrblocks, step);
}

/*
 * If the post-grow filesystem will have an rtsb; we're initializing the first
 * rtgroup; and the filesystem didn't have a realtime section, write the rtsb
 * now, and attach the rtsb buffer to the real mount.
 */
static int
xfs_growfs_rt_init_rtsb(
	const struct xfs_rtalloc_args	*nargs,
	const struct xfs_rtgroup	*rtg,
	const struct xfs_rtalloc_args	*args)
{
	struct xfs_mount		*mp = args->mp;
	struct xfs_buf			*rtsb_bp;
	int				error;

	if (!xfs_has_rtsb(nargs->mp))
		return 0;
	if (rtg_rgno(rtg) > 0)
		return 0;
	if (mp->m_sb.sb_rblocks)
		return 0;

	error = xfs_buf_get_uncached(mp->m_rtdev_targp, XFS_FSB_TO_BB(mp, 1),
			&rtsb_bp);
	if (error)
		return error;

	rtsb_bp->b_maps[0].bm_bn = XFS_RTSB_DADDR;
	rtsb_bp->b_ops = &xfs_rtsb_buf_ops;

	xfs_update_rtsb(rtsb_bp, mp->m_sb_bp);
	mp->m_rtsb_bp = rtsb_bp;
	error = xfs_bwrite(rtsb_bp);
	xfs_buf_unlock(rtsb_bp);
	if (error)
		return error;

	/* Initialize the rtrmap to reflect the rtsb. */
	if (rtg_rmap(args->rtg) != NULL)
		error = xfs_rtrmapbt_init_rtsb(nargs->mp, args->rtg, args->tp);

	return error;
}

static void
xfs_growfs_rt_sb_fields(
	struct xfs_trans	*tp,
	const struct xfs_mount	*nmp)
{
	struct xfs_mount	*mp = tp->t_mountp;

	if (nmp->m_sb.sb_rextsize != mp->m_sb.sb_rextsize)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTSIZE,
			nmp->m_sb.sb_rextsize - mp->m_sb.sb_rextsize);
	if (nmp->m_sb.sb_rbmblocks != mp->m_sb.sb_rbmblocks)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_RBMBLOCKS,
			nmp->m_sb.sb_rbmblocks - mp->m_sb.sb_rbmblocks);
	if (nmp->m_sb.sb_rblocks != mp->m_sb.sb_rblocks)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_RBLOCKS,
			nmp->m_sb.sb_rblocks - mp->m_sb.sb_rblocks);
	if (nmp->m_sb.sb_rextents != mp->m_sb.sb_rextents)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTENTS,
			nmp->m_sb.sb_rextents - mp->m_sb.sb_rextents);
	if (nmp->m_sb.sb_rextslog != mp->m_sb.sb_rextslog)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTSLOG,
			nmp->m_sb.sb_rextslog - mp->m_sb.sb_rextslog);
	if (nmp->m_sb.sb_rgcount != mp->m_sb.sb_rgcount)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_RGCOUNT,
			nmp->m_sb.sb_rgcount - mp->m_sb.sb_rgcount);
}

static int
xfs_growfs_rt_zoned(
	struct xfs_rtgroup	*rtg,
	xfs_rfsblock_t		nrblocks)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_mount	*nmp;
	struct xfs_trans	*tp;
	xfs_rtbxlen_t		freed_rtx;
	int			error;

	/*
	 * Calculate new sb and mount fields for this round.  Also ensure the
	 * rtg_extents value is uptodate as the rtbitmap code relies on it.
	 */
	nmp = xfs_growfs_rt_alloc_fake_mount(mp, nrblocks,
			mp->m_sb.sb_rextsize);
	if (!nmp)
		return -ENOMEM;
	freed_rtx = nmp->m_sb.sb_rextents - mp->m_sb.sb_rextents;

	xfs_rtgroup_calc_geometry(nmp, rtg, rtg_rgno(rtg),
			nmp->m_sb.sb_rgcount, nmp->m_sb.sb_rextents);

	error = xfs_trans_alloc(mp, &M_RES(nmp)->tr_growrtfree, 0, 0, 0, &tp);
	if (error)
		goto out_free;

	xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
	xfs_rtgroup_trans_join(tp, rtg, XFS_RTGLOCK_RMAP);

	xfs_growfs_rt_sb_fields(tp, nmp);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_FREXTENTS, freed_rtx);

	error = xfs_trans_commit(tp);
	if (error)
		goto out_free;

	/*
	 * Ensure the mount RT feature flag is now set, and compute new
	 * maxlevels for rt btrees.
	 */
	mp->m_features |= XFS_FEAT_REALTIME;
	xfs_rtrmapbt_compute_maxlevels(mp);
	xfs_rtrefcountbt_compute_maxlevels(mp);
	xfs_zoned_add_available(mp, freed_rtx);
out_free:
	kfree(nmp);
	return error;
}

static int
xfs_growfs_rt_bmblock(
	struct xfs_rtgroup	*rtg,
	xfs_rfsblock_t		nrblocks,
	xfs_agblock_t		rextsize,
	xfs_fileoff_t		bmbno)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_inode	*rbmip = rtg_bitmap(rtg);
	struct xfs_inode	*rsumip = rtg_summary(rtg);
	struct xfs_rtalloc_args	args = {
		.mp		= mp,
		.rtg		= rtg,
	};
	struct xfs_rtalloc_args	nargs = {
		.rtg		= rtg,
	};
	struct xfs_mount	*nmp;
	xfs_rtbxlen_t		freed_rtx;
	int			error;

	/*
	 * Calculate new sb and mount fields for this round.  Also ensure the
	 * rtg_extents value is uptodate as the rtbitmap code relies on it.
	 */
	nmp = nargs.mp = xfs_growfs_rt_alloc_fake_mount(mp,
			xfs_growfs_rt_nrblocks(rtg, nrblocks, rextsize, bmbno),
			rextsize);
	if (!nmp)
		return -ENOMEM;

	xfs_rtgroup_calc_geometry(nmp, rtg, rtg_rgno(rtg),
			nmp->m_sb.sb_rgcount, nmp->m_sb.sb_rextents);

	/*
	 * Recompute the growfsrt reservation from the new rsumsize, so that the
	 * transaction below use the new, potentially larger value.
	 * */
	xfs_trans_resv_calc(nmp, &nmp->m_resv);
	error = xfs_trans_alloc(mp, &M_RES(nmp)->tr_growrtfree, 0, 0, 0,
			&args.tp);
	if (error)
		goto out_free;
	nargs.tp = args.tp;

	xfs_rtgroup_lock(args.rtg, XFS_RTGLOCK_BITMAP | XFS_RTGLOCK_RMAP);
	xfs_rtgroup_trans_join(args.tp, args.rtg,
			XFS_RTGLOCK_BITMAP | XFS_RTGLOCK_RMAP);

	/*
	 * Update the bitmap inode's size ondisk and incore.  We need to update
	 * the incore size so that inode inactivation won't punch what it thinks
	 * are "posteof" blocks.
	 */
	rbmip->i_disk_size = nmp->m_sb.sb_rbmblocks * nmp->m_sb.sb_blocksize;
	i_size_write(VFS_I(rbmip), rbmip->i_disk_size);
	xfs_trans_log_inode(args.tp, rbmip, XFS_ILOG_CORE);

	/*
	 * Update the summary inode's size.  We need to update the incore size
	 * so that inode inactivation won't punch what it thinks are "posteof"
	 * blocks.
	 */
	rsumip->i_disk_size = nmp->m_rsumblocks * nmp->m_sb.sb_blocksize;
	i_size_write(VFS_I(rsumip), rsumip->i_disk_size);
	xfs_trans_log_inode(args.tp, rsumip, XFS_ILOG_CORE);

	/*
	 * Copy summary data from old to new sizes when the real size (not
	 * block-aligned) changes.
	 */
	if (mp->m_sb.sb_rbmblocks != nmp->m_sb.sb_rbmblocks ||
	    mp->m_rsumlevels != nmp->m_rsumlevels) {
		error = xfs_rtcopy_summary(&args, &nargs);
		if (error)
			goto out_cancel;
	}

	error = xfs_growfs_rt_init_rtsb(&nargs, rtg, &args);
	if (error)
		goto out_cancel;

	/*
	 * Update superblock fields.
	 */
	xfs_growfs_rt_sb_fields(args.tp, nmp);

	/*
	 * Free the new extent.
	 */
	error = xfs_growfs_rt_free_new(rtg, &nargs, &freed_rtx);
	xfs_rtbuf_cache_relse(&nargs);
	if (error)
		goto out_cancel;

	/*
	 * Mark more blocks free in the superblock.
	 */
	xfs_trans_mod_sb(args.tp, XFS_TRANS_SB_FREXTENTS, freed_rtx);

	/*
	 * Update the calculated values in the real mount structure.
	 */
	mp->m_rsumlevels = nmp->m_rsumlevels;
	mp->m_rsumblocks = nmp->m_rsumblocks;

	/*
	 * Recompute the growfsrt reservation from the new rsumsize.
	 */
	xfs_trans_resv_calc(mp, &mp->m_resv);

	error = xfs_trans_commit(args.tp);
	if (error)
		goto out_free;

	/*
	 * Ensure the mount RT feature flag is now set, and compute new
	 * maxlevels for rt btrees.
	 */
	mp->m_features |= XFS_FEAT_REALTIME;
	xfs_rtrmapbt_compute_maxlevels(mp);
	xfs_rtrefcountbt_compute_maxlevels(mp);

	kfree(nmp);
	return 0;

out_cancel:
	xfs_trans_cancel(args.tp);
out_free:
	kfree(nmp);
	return error;
}

static xfs_rtxnum_t
xfs_last_rtgroup_extents(
	struct xfs_mount	*mp)
{
	return mp->m_sb.sb_rextents -
		((xfs_rtxnum_t)(mp->m_sb.sb_rgcount - 1) *
		 mp->m_sb.sb_rgextents);
}

/*
 * Calculate the last rbmblock currently used.
 *
 * This also deals with the case where there were no rtextents before.
 */
static xfs_fileoff_t
xfs_last_rt_bmblock(
	struct xfs_rtgroup	*rtg)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_rgnumber_t		rgno = rtg_rgno(rtg);
	xfs_fileoff_t		bmbno = 0;

	ASSERT(!mp->m_sb.sb_rgcount || rgno >= mp->m_sb.sb_rgcount - 1);

	if (mp->m_sb.sb_rgcount && rgno == mp->m_sb.sb_rgcount - 1) {
		xfs_rtxnum_t	nrext = xfs_last_rtgroup_extents(mp);

		/* Also fill up the previous block if not entirely full. */
		bmbno = xfs_rtbitmap_blockcount_len(mp, nrext);
		if (xfs_rtx_to_rbmword(mp, nrext) != 0)
			bmbno--;
	}

	return bmbno;
}

/*
 * Allocate space to the bitmap and summary files, as necessary.
 */
static int
xfs_growfs_rt_alloc_blocks(
	struct xfs_rtgroup	*rtg,
	xfs_rfsblock_t		nrblocks,
	xfs_agblock_t		rextsize,
	xfs_extlen_t		*nrbmblocks)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_inode	*rbmip = rtg_bitmap(rtg);
	struct xfs_inode	*rsumip = rtg_summary(rtg);
	xfs_extlen_t		orbmblocks = 0;
	xfs_extlen_t		orsumblocks = 0;
	struct xfs_mount	*nmp;
	int			error = 0;

	nmp = xfs_growfs_rt_alloc_fake_mount(mp, nrblocks, rextsize);
	if (!nmp)
		return -ENOMEM;
	*nrbmblocks = nmp->m_sb.sb_rbmblocks;

	if (xfs_has_rtgroups(mp)) {
		/*
		 * For file systems with the rtgroups feature, the RT bitmap and
		 * summary are always fully allocated, which means that we never
		 * need to grow the existing files.
		 *
		 * But we have to be careful to only fill the bitmap until the
		 * end of the actually used range.
		 */
		if (rtg_rgno(rtg) == nmp->m_sb.sb_rgcount - 1)
			*nrbmblocks = xfs_rtbitmap_blockcount_len(nmp,
					xfs_last_rtgroup_extents(nmp));

		if (mp->m_sb.sb_rgcount &&
		    rtg_rgno(rtg) == mp->m_sb.sb_rgcount - 1)
			goto out_free;
	} else {
		/*
		 * Get the old block counts for bitmap and summary inodes.
		 * These can't change since other growfs callers are locked out.
		 */
		orbmblocks = XFS_B_TO_FSB(mp, rbmip->i_disk_size);
		orsumblocks = XFS_B_TO_FSB(mp, rsumip->i_disk_size);
	}

	error = xfs_rtfile_initialize_blocks(rtg, XFS_RTGI_BITMAP, orbmblocks,
			nmp->m_sb.sb_rbmblocks, NULL);
	if (error)
		goto out_free;
	error = xfs_rtfile_initialize_blocks(rtg, XFS_RTGI_SUMMARY, orsumblocks,
			nmp->m_rsumblocks, NULL);
out_free:
	kfree(nmp);
	return error;
}

static int
xfs_growfs_rtg(
	struct xfs_mount	*mp,
	xfs_rgnumber_t		rgno,
	xfs_rfsblock_t		nrblocks,
	xfs_agblock_t		rextsize)
{
	uint8_t			*old_rsum_cache = NULL;
	xfs_extlen_t		bmblocks;
	xfs_fileoff_t		bmbno;
	struct xfs_rtgroup	*rtg;
	unsigned int		i;
	int			error;

	rtg = xfs_rtgroup_grab(mp, rgno);
	if (!rtg)
		return -EINVAL;

	for (i = 0; i < XFS_RTGI_MAX; i++) {
		error = xfs_rtginode_ensure(rtg, i);
		if (error)
			goto out_rele;
	}

	if (xfs_has_zoned(mp)) {
		error = xfs_growfs_rt_zoned(rtg, nrblocks);
		goto out_rele;
	}

	error = xfs_growfs_rt_alloc_blocks(rtg, nrblocks, rextsize, &bmblocks);
	if (error)
		goto out_rele;

	if (bmblocks != rtg_mount(rtg)->m_sb.sb_rbmblocks) {
		old_rsum_cache = rtg->rtg_rsum_cache;
		error = xfs_alloc_rsum_cache(rtg, bmblocks);
		if (error)
			goto out_rele;
	}

	for (bmbno = xfs_last_rt_bmblock(rtg); bmbno < bmblocks; bmbno++) {
		error = xfs_growfs_rt_bmblock(rtg, nrblocks, rextsize, bmbno);
		if (error)
			goto out_error;
	}

	kvfree(old_rsum_cache);
	goto out_rele;

out_error:
	/*
	 * Reset rtg_extents to the old value if adding more blocks failed.
	 */
	xfs_rtgroup_calc_geometry(mp, rtg, rtg_rgno(rtg), mp->m_sb.sb_rgcount,
			mp->m_sb.sb_rextents);
	if (old_rsum_cache) {
		kvfree(rtg->rtg_rsum_cache);
		rtg->rtg_rsum_cache = old_rsum_cache;
	}
out_rele:
	xfs_rtgroup_rele(rtg);
	return error;
}

int
xfs_growfs_check_rtgeom(
	const struct xfs_mount	*mp,
	xfs_rfsblock_t		dblocks,
	xfs_rfsblock_t		rblocks,
	xfs_extlen_t		rextsize)
{
	xfs_extlen_t		min_logfsbs;
	struct xfs_mount	*nmp;

	nmp = xfs_growfs_rt_alloc_fake_mount(mp, rblocks, rextsize);
	if (!nmp)
		return -ENOMEM;
	nmp->m_sb.sb_dblocks = dblocks;

	xfs_rtrmapbt_compute_maxlevels(nmp);
	xfs_rtrefcountbt_compute_maxlevels(nmp);
	xfs_trans_resv_calc(nmp, M_RES(nmp));

	/*
	 * New summary size can't be more than half the size of the log.  This
	 * prevents us from getting a log overflow, since we'll log basically
	 * the whole summary file at once.
	 */
	min_logfsbs = min_t(xfs_extlen_t, xfs_log_calc_minimum_size(nmp),
			nmp->m_rsumblocks * 2);

	kfree(nmp);

	trace_xfs_growfs_check_rtgeom(mp, min_logfsbs);

	if (min_logfsbs > mp->m_sb.sb_logblocks)
		return -EINVAL;

	if (xfs_has_zoned(mp)) {
		uint32_t	gblocks = mp->m_groups[XG_TYPE_RTG].blocks;
		uint32_t	rem;

		if (rextsize != 1)
			return -EINVAL;
		div_u64_rem(mp->m_sb.sb_rblocks, gblocks, &rem);
		if (rem) {
			xfs_warn(mp,
"new RT volume size (%lld) not aligned to RT group size (%d)",
				mp->m_sb.sb_rblocks, gblocks);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Compute the new number of rt groups and ensure that /rtgroups exists.
 *
 * Changing the rtgroup size is not allowed (even if the rt volume hasn't yet
 * been initialized) because the userspace ABI doesn't support it.
 */
static int
xfs_growfs_rt_prep_groups(
	struct xfs_mount	*mp,
	xfs_rfsblock_t		rblocks,
	xfs_extlen_t		rextsize,
	xfs_rgnumber_t		*new_rgcount)
{
	int			error;

	*new_rgcount = howmany_64(rblocks, mp->m_sb.sb_rgextents * rextsize);
	if (*new_rgcount > XFS_MAX_RGNUMBER)
		return -EINVAL;

	/* Make sure the /rtgroups dir has been created */
	if (!mp->m_rtdirip) {
		struct xfs_trans	*tp;

		error = xfs_trans_alloc_empty(mp, &tp);
		if (error)
			return error;
		error = xfs_rtginode_load_parent(tp);
		xfs_trans_cancel(tp);

		if (error == -ENOENT)
			error = xfs_rtginode_mkdir_parent(mp);
		if (error)
			return error;
	}

	return 0;
}

static bool
xfs_grow_last_rtg(
	struct xfs_mount	*mp)
{
	if (!xfs_has_rtgroups(mp))
		return true;
	if (mp->m_sb.sb_rgcount == 0)
		return false;
	return xfs_rtgroup_extents(mp, mp->m_sb.sb_rgcount - 1) <=
			mp->m_sb.sb_rgextents;
}

/*
 * Read in the last block of the RT device to make sure it is accessible.
 */
static int
xfs_rt_check_size(
	struct xfs_mount	*mp,
	xfs_rfsblock_t		last_block)
{
	xfs_daddr_t		daddr = XFS_FSB_TO_BB(mp, last_block);
	struct xfs_buf		*bp;
	int			error;

	if (XFS_BB_TO_FSB(mp, daddr) != last_block) {
		xfs_warn(mp, "RT device size overflow: %llu != %llu",
			XFS_BB_TO_FSB(mp, daddr), last_block);
		return -EFBIG;
	}

	error = xfs_buf_read_uncached(mp->m_rtdev_targp,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_rtstart) + daddr,
			XFS_FSB_TO_BB(mp, 1), &bp, NULL);
	if (error)
		xfs_warn(mp, "cannot read last RT device sector (%lld)",
				last_block);
	else
		xfs_buf_relse(bp);
	return error;
}

/*
 * Grow the realtime area of the filesystem.
 */
int
xfs_growfs_rt(
	struct xfs_mount	*mp,
	struct xfs_growfs_rt	*in)
{
	xfs_rgnumber_t		old_rgcount = mp->m_sb.sb_rgcount;
	xfs_rgnumber_t		new_rgcount = 1;
	xfs_rgnumber_t		rgno;
	xfs_agblock_t		old_rextsize = mp->m_sb.sb_rextsize;
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* Needs to have been mounted with an rt device. */
	if (!XFS_IS_REALTIME_MOUNT(mp))
		return -EINVAL;

	if (!mutex_trylock(&mp->m_growlock))
		return -EWOULDBLOCK;

	/* Shrink not supported. */
	error = -EINVAL;
	if (in->newblocks <= mp->m_sb.sb_rblocks)
		goto out_unlock;
	/* Can only change rt extent size when adding rt volume. */
	if (mp->m_sb.sb_rblocks > 0 && in->extsize != mp->m_sb.sb_rextsize)
		goto out_unlock;

	/* Range check the extent size. */
	if (XFS_FSB_TO_B(mp, in->extsize) > XFS_MAX_RTEXTSIZE ||
	    XFS_FSB_TO_B(mp, in->extsize) < XFS_MIN_RTEXTSIZE)
		goto out_unlock;

	/* Check for features supported only on rtgroups filesystems. */
	error = -EOPNOTSUPP;
	if (!xfs_has_rtgroups(mp)) {
		if (xfs_has_rmapbt(mp))
			goto out_unlock;
		if (xfs_has_quota(mp))
			goto out_unlock;
		if (xfs_has_reflink(mp))
			goto out_unlock;
	} else if (xfs_has_reflink(mp) &&
		   !xfs_reflink_supports_rextsize(mp, in->extsize))
		goto out_unlock;

	error = xfs_sb_validate_fsb_count(&mp->m_sb, in->newblocks);
	if (error)
		goto out_unlock;

	error = xfs_rt_check_size(mp, in->newblocks - 1);
	if (error)
		goto out_unlock;

	/*
	 * Calculate new parameters.  These are the final values to be reached.
	 */
	error = -EINVAL;
	if (in->newblocks < in->extsize)
		goto out_unlock;

	/* Make sure the new fs size won't cause problems with the log. */
	error = xfs_growfs_check_rtgeom(mp, mp->m_sb.sb_dblocks, in->newblocks,
			in->extsize);
	if (error)
		goto out_unlock;

	if (xfs_has_rtgroups(mp)) {
		error = xfs_growfs_rt_prep_groups(mp, in->newblocks,
				in->extsize, &new_rgcount);
		if (error)
			goto out_unlock;
	}

	if (xfs_grow_last_rtg(mp)) {
		error = xfs_growfs_rtg(mp, old_rgcount - 1, in->newblocks,
				in->extsize);
		if (error)
			goto out_unlock;
	}

	for (rgno = old_rgcount; rgno < new_rgcount; rgno++) {
		xfs_rtbxlen_t	rextents = div_u64(in->newblocks, in->extsize);

		error = xfs_rtgroup_alloc(mp, rgno, new_rgcount, rextents);
		if (error)
			goto out_unlock;

		error = xfs_growfs_rtg(mp, rgno, in->newblocks, in->extsize);
		if (error) {
			struct xfs_rtgroup	*rtg;

			rtg = xfs_rtgroup_grab(mp, rgno);
			if (!WARN_ON_ONCE(!rtg)) {
				xfs_rtunmount_rtg(rtg);
				xfs_rtgroup_rele(rtg);
				xfs_rtgroup_free(mp, rgno);
			}
			break;
		}
	}

	if (!error && old_rextsize != in->extsize)
		error = xfs_growfs_rt_fixup_extsize(mp);

	/*
	 * Update secondary superblocks now the physical grow has completed.
	 *
	 * Also do this in case of an error as we might have already
	 * successfully updated one or more RTGs and incremented sb_rgcount.
	 */
	if (!xfs_is_shutdown(mp)) {
		int error2 = xfs_update_secondary_sbs(mp);

		if (!error)
			error = error2;

		/* Reset the rt metadata btree space reservations. */
		error2 = xfs_metafile_resv_init(mp);
		if (error2 && error2 != -ENOSPC)
			error = error2;
	}

out_unlock:
	mutex_unlock(&mp->m_growlock);
	return error;
}

/* Read the realtime superblock and attach it to the mount. */
int
xfs_rtmount_readsb(
	struct xfs_mount	*mp)
{
	struct xfs_buf		*bp;
	int			error;

	if (!xfs_has_rtsb(mp))
		return 0;
	if (mp->m_sb.sb_rblocks == 0)
		return 0;
	if (mp->m_rtdev_targp == NULL) {
		xfs_warn(mp,
	"Filesystem has a realtime volume, use rtdev=device option");
		return -ENODEV;
	}

	/* m_blkbb_log is not set up yet */
	error = xfs_buf_read_uncached(mp->m_rtdev_targp, XFS_RTSB_DADDR,
			mp->m_sb.sb_blocksize >> BBSHIFT, &bp,
			&xfs_rtsb_buf_ops);
	if (error) {
		xfs_warn(mp, "rt sb validate failed with error %d.", error);
		/* bad CRC means corrupted metadata */
		if (error == -EFSBADCRC)
			error = -EFSCORRUPTED;
		return error;
	}

	mp->m_rtsb_bp = bp;
	xfs_buf_unlock(bp);
	return 0;
}

/* Detach the realtime superblock from the mount and free it. */
void
xfs_rtmount_freesb(
	struct xfs_mount	*mp)
{
	struct xfs_buf		*bp = mp->m_rtsb_bp;

	if (!bp)
		return;

	xfs_buf_lock(bp);
	mp->m_rtsb_bp = NULL;
	xfs_buf_relse(bp);
}

/*
 * Initialize realtime fields in the mount structure.
 */
int				/* error */
xfs_rtmount_init(
	struct xfs_mount	*mp)	/* file system mount structure */
{
	if (mp->m_sb.sb_rblocks == 0)
		return 0;
	if (mp->m_rtdev_targp == NULL) {
		xfs_warn(mp,
	"Filesystem has a realtime volume, use rtdev=device option");
		return -ENODEV;
	}

	mp->m_rsumblocks = xfs_rtsummary_blockcount(mp, &mp->m_rsumlevels);

	return xfs_rt_check_size(mp, mp->m_sb.sb_rblocks - 1);
}

static int
xfs_rtalloc_count_frextent(
	struct xfs_rtgroup		*rtg,
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

	struct xfs_rtgroup	*rtg = NULL;

	while ((rtg = xfs_rtgroup_next(mp, rtg))) {
		xfs_rtgroup_lock(rtg, XFS_RTGLOCK_BITMAP_SHARED);
		error = xfs_rtalloc_query_all(rtg, NULL,
				xfs_rtalloc_count_frextent, &val);
		xfs_rtgroup_unlock(rtg, XFS_RTGLOCK_BITMAP_SHARED);
		if (error) {
			xfs_rtgroup_rele(rtg);
			return error;
		}
	}

	spin_lock(&mp->m_sb_lock);
	mp->m_sb.sb_frextents = val;
	spin_unlock(&mp->m_sb_lock);
	xfs_set_freecounter(mp, XC_FREE_RTEXTENTS, mp->m_sb.sb_frextents);
	return 0;
}

/*
 * Read in the bmbt of an rt metadata inode so that we never have to load them
 * at runtime.  This enables the use of shared ILOCKs for rtbitmap scans.  Use
 * an empty transaction to avoid deadlocking on loops in the bmbt.
 */
static inline int
xfs_rtmount_iread_extents(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	int			error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	error = xfs_iread_extents(tp, ip, XFS_DATA_FORK);
	if (error)
		goto out_unlock;

	if (xfs_inode_has_attr_fork(ip)) {
		error = xfs_iread_extents(tp, ip, XFS_ATTR_FORK);
		if (error)
			goto out_unlock;
	}

out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

static int
xfs_rtmount_rtg(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_rtgroup	*rtg)
{
	int			error, i;

	for (i = 0; i < XFS_RTGI_MAX; i++) {
		error = xfs_rtginode_load(rtg, i, tp);
		if (error)
			return error;

		if (rtg->rtg_inodes[i]) {
			error = xfs_rtmount_iread_extents(tp,
					rtg->rtg_inodes[i]);
			if (error)
				return error;
		}
	}

	if (xfs_has_zoned(mp))
		return 0;
	return xfs_alloc_rsum_cache(rtg, mp->m_sb.sb_rbmblocks);
}

/*
 * Get the bitmap and summary inodes and the summary cache into the mount
 * structure at mount time.
 */
int
xfs_rtmount_inodes(
	struct xfs_mount	*mp)
{
	struct xfs_trans	*tp;
	struct xfs_rtgroup	*rtg = NULL;
	int			error;

	error = xfs_trans_alloc_empty(mp, &tp);
	if (error)
		return error;

	if (xfs_has_rtgroups(mp) && mp->m_sb.sb_rgcount > 0) {
		error = xfs_rtginode_load_parent(tp);
		if (error)
			goto out_cancel;
	}

	while ((rtg = xfs_rtgroup_next(mp, rtg))) {
		error = xfs_rtmount_rtg(mp, tp, rtg);
		if (error) {
			xfs_rtgroup_rele(rtg);
			xfs_rtunmount_inodes(mp);
			break;
		}
	}

out_cancel:
	xfs_trans_cancel(tp);
	return error;
}

void
xfs_rtunmount_inodes(
	struct xfs_mount	*mp)
{
	struct xfs_rtgroup	*rtg = NULL;

	while ((rtg = xfs_rtgroup_next(mp, rtg)))
		xfs_rtunmount_rtg(rtg);
	xfs_rtginode_irele(&mp->m_rtdirip);
}

/*
 * Pick an extent for allocation at the start of a new realtime file.
 * Use the sequence number stored in the atime field of the bitmap inode.
 * Translate this to a fraction of the rtextents, and return the product
 * of rtextents and the fraction.
 * The fraction sequence is 0, 1/2, 1/4, 3/4, 1/8, ..., 7/8, 1/16, ...
 */
static xfs_rtxnum_t
xfs_rtpick_extent(
	struct xfs_rtgroup	*rtg,
	struct xfs_trans	*tp,
	xfs_rtxlen_t		len)		/* allocation length (rtextents) */
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_inode	*rbmip = rtg_bitmap(rtg);
	xfs_rtxnum_t		b = 0;		/* result rtext */
	int			log2;		/* log of sequence number */
	uint64_t		resid;		/* residual after log removed */
	uint64_t		seq;		/* sequence number of file creation */
	struct timespec64	ts;		/* timespec in inode */

	xfs_assert_ilocked(rbmip, XFS_ILOCK_EXCL);

	ts = inode_get_atime(VFS_I(rbmip));
	if (!(rbmip->i_diflags & XFS_DIFLAG_NEWRTBM)) {
		rbmip->i_diflags |= XFS_DIFLAG_NEWRTBM;
		seq = 0;
	} else {
		seq = ts.tv_sec;
	}
	log2 = xfs_highbit64(seq);
	if (log2 != -1) {
		resid = seq - (1ULL << log2);
		b = (mp->m_sb.sb_rextents * ((resid << 1) + 1ULL)) >>
		    (log2 + 1);
		if (b >= mp->m_sb.sb_rextents)
			div64_u64_rem(b, mp->m_sb.sb_rextents, &b);
		if (b + len > mp->m_sb.sb_rextents)
			b = mp->m_sb.sb_rextents - len;
	}
	ts.tv_sec = seq + 1;
	inode_set_atime_to_ts(VFS_I(rbmip), ts);
	xfs_trans_log_inode(tp, rbmip, XFS_ILOG_CORE);
	return b;
}

static void
xfs_rtalloc_align_minmax(
	xfs_rtxlen_t		*raminlen,
	xfs_rtxlen_t		*ramaxlen,
	xfs_rtxlen_t		*prod)
{
	xfs_rtxlen_t		newmaxlen = *ramaxlen;
	xfs_rtxlen_t		newminlen = *raminlen;
	xfs_rtxlen_t		slack;

	slack = newmaxlen % *prod;
	if (slack)
		newmaxlen -= slack;
	slack = newminlen % *prod;
	if (slack)
		newminlen += *prod - slack;

	/*
	 * If adjusting for extent size hint alignment produces an invalid
	 * min/max len combination, go ahead without it.
	 */
	if (newmaxlen < newminlen) {
		*prod = 1;
		return;
	}
	*ramaxlen = newmaxlen;
	*raminlen = newminlen;
}

/* Given a free extent, find any part of it that isn't busy, if possible. */
STATIC bool
xfs_rtalloc_check_busy(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,
	xfs_rtxlen_t		minlen_rtx,
	xfs_rtxlen_t		maxlen_rtx,
	xfs_rtxlen_t		len_rtx,
	xfs_rtxlen_t		prod,
	xfs_rtxnum_t		rtx,
	xfs_rtxlen_t		*reslen,
	xfs_rtxnum_t		*resrtx,
	unsigned		*busy_gen)
{
	struct xfs_rtgroup	*rtg = args->rtg;
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_agblock_t		rgbno = xfs_rtx_to_rgbno(rtg, rtx);
	xfs_rgblock_t		min_rgbno = xfs_rtx_to_rgbno(rtg, start);
	xfs_extlen_t		minlen = xfs_rtxlen_to_extlen(mp, minlen_rtx);
	xfs_extlen_t		len = xfs_rtxlen_to_extlen(mp, len_rtx);
	xfs_extlen_t		diff;
	bool			busy;

	busy = xfs_extent_busy_trim(rtg_group(rtg), minlen,
			xfs_rtxlen_to_extlen(mp, maxlen_rtx), &rgbno, &len,
			busy_gen);

	/*
	 * If we have a largish extent that happens to start before min_rgbno,
	 * see if we can shift it into range...
	 */
	if (rgbno < min_rgbno && rgbno + len > min_rgbno) {
		diff = min_rgbno - rgbno;
		if (len > diff) {
			rgbno += diff;
			len -= diff;
		}
	}

	if (prod > 1 && len >= minlen) {
		xfs_rgblock_t	aligned_rgbno = roundup(rgbno, prod);

		diff = aligned_rgbno - rgbno;

		*resrtx = xfs_rgbno_to_rtx(mp, aligned_rgbno);
		*reslen = xfs_extlen_to_rtxlen(mp,
				diff >= len ? 0 : len - diff);
	} else {
		*resrtx = xfs_rgbno_to_rtx(mp, rgbno);
		*reslen = xfs_extlen_to_rtxlen(mp, len);
	}

	return busy;
}

/*
 * Adjust the given free extent so that it isn't busy, or flush the log and
 * wait for the space to become unbusy.  Only needed for rtgroups.
 */
STATIC int
xfs_rtallocate_adjust_for_busy(
	struct xfs_rtalloc_args	*args,
	xfs_rtxnum_t		start,
	xfs_rtxlen_t		minlen,
	xfs_rtxlen_t		maxlen,
	xfs_rtxlen_t		*len,
	xfs_rtxlen_t		prod,
	xfs_rtxnum_t		*rtx)
{
	xfs_rtxnum_t		resrtx;
	xfs_rtxlen_t		reslen;
	unsigned		busy_gen;
	bool			busy;
	int			error;

again:
	busy = xfs_rtalloc_check_busy(args, start, minlen, maxlen, *len, prod,
			*rtx, &reslen, &resrtx, &busy_gen);
	if (!busy)
		return 0;

	if (reslen < minlen || (start != 0 && resrtx != *rtx)) {
		/*
		 * Enough of the extent was busy that we cannot satisfy the
		 * allocation, or this is a near allocation and the start of
		 * the extent is busy.  Flush the log and wait for the busy
		 * situation to resolve.
		 */
		trace_xfs_rtalloc_extent_busy(args->rtg, start, minlen, maxlen,
				*len, prod, *rtx, busy_gen);

		error = xfs_extent_busy_flush(args->tp, rtg_group(args->rtg),
				busy_gen, 0);
		if (error)
			return error;

		goto again;
	}

	/* Some of the free space wasn't busy, hand that back to the caller. */
	trace_xfs_rtalloc_extent_busy_trim(args->rtg, *rtx, *len, resrtx,
			reslen);
	*len = reslen;
	*rtx = resrtx;

	return 0;
}

static int
xfs_rtallocate_rtg(
	struct xfs_trans	*tp,
	xfs_rgnumber_t		rgno,
	xfs_rtblock_t		bno_hint,
	xfs_rtxlen_t		minlen,
	xfs_rtxlen_t		maxlen,
	xfs_rtxlen_t		prod,
	bool			wasdel,
	bool			initial_user_data,
	bool			*rtlocked,
	xfs_rtblock_t		*bno,
	xfs_extlen_t		*blen)
{
	struct xfs_rtalloc_args	args = {
		.mp		= tp->t_mountp,
		.tp		= tp,
	};
	xfs_rtxnum_t		start = 0;
	xfs_rtxnum_t		rtx;
	xfs_rtxlen_t		len = 0;
	int			error = 0;

	args.rtg = xfs_rtgroup_grab(args.mp, rgno);
	if (!args.rtg)
		return -ENOSPC;

	/*
	 * We need to lock out modifications to both the RT bitmap and summary
	 * inodes for finding free space in xfs_rtallocate_extent_{near,size}
	 * and join the bitmap and summary inodes for the actual allocation
	 * down in xfs_rtallocate_range.
	 *
	 * For RTG-enabled file system we don't want to join the inodes to the
	 * transaction until we are committed to allocate to allocate from this
	 * RTG so that only one inode of each type is locked at a time.
	 *
	 * But for pre-RTG file systems we need to already to join the bitmap
	 * inode to the transaction for xfs_rtpick_extent, which bumps the
	 * sequence number in it, so we'll have to join the inode to the
	 * transaction early here.
	 *
	 * This is all a bit messy, but at least the mess is contained in
	 * this function.
	 */
	if (!*rtlocked) {
		xfs_rtgroup_lock(args.rtg, XFS_RTGLOCK_BITMAP);
		if (!xfs_has_rtgroups(args.mp))
			xfs_rtgroup_trans_join(tp, args.rtg,
					XFS_RTGLOCK_BITMAP);
		*rtlocked = true;
	}

	/*
	 * For an allocation to an empty file at offset 0, pick an extent that
	 * will space things out in the rt area.
	 */
	if (bno_hint != NULLFSBLOCK)
		start = xfs_rtb_to_rtx(args.mp, bno_hint);
	else if (!xfs_has_rtgroups(args.mp) && initial_user_data)
		start = xfs_rtpick_extent(args.rtg, tp, maxlen);

	if (start) {
		error = xfs_rtallocate_extent_near(&args, start, minlen, maxlen,
				&len, prod, &rtx);
		/*
		 * If we can't allocate near a specific rt extent, try again
		 * without locality criteria.
		 */
		if (error == -ENOSPC) {
			xfs_rtbuf_cache_relse(&args);
			error = 0;
		}
	}

	if (!error) {
		error = xfs_rtallocate_extent_size(&args, minlen, maxlen, &len,
				prod, &rtx);
	}

	if (error) {
		if (xfs_has_rtgroups(args.mp))
			goto out_unlock;
		goto out_release;
	}

	if (xfs_has_rtgroups(args.mp)) {
		error = xfs_rtallocate_adjust_for_busy(&args, start, minlen,
				maxlen, &len, prod, &rtx);
		if (error)
			goto out_unlock;

		xfs_rtgroup_trans_join(tp, args.rtg, XFS_RTGLOCK_BITMAP);
	}

	error = xfs_rtallocate_range(&args, rtx, len);
	if (error)
		goto out_release;

	xfs_trans_mod_sb(tp, wasdel ?
			XFS_TRANS_SB_RES_FREXTENTS : XFS_TRANS_SB_FREXTENTS,
			-(long)len);
	*bno = xfs_rtx_to_rtb(args.rtg, rtx);
	*blen = xfs_rtxlen_to_extlen(args.mp, len);

out_release:
	xfs_rtgroup_rele(args.rtg);
	xfs_rtbuf_cache_relse(&args);
	return error;
out_unlock:
	xfs_rtgroup_unlock(args.rtg, XFS_RTGLOCK_BITMAP);
	*rtlocked = false;
	goto out_release;
}

int
xfs_rtallocate_rtgs(
	struct xfs_trans	*tp,
	xfs_fsblock_t		bno_hint,
	xfs_rtxlen_t		minlen,
	xfs_rtxlen_t		maxlen,
	xfs_rtxlen_t		prod,
	bool			wasdel,
	bool			initial_user_data,
	xfs_rtblock_t		*bno,
	xfs_extlen_t		*blen)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_rgnumber_t		start_rgno, rgno;
	int			error;

	/*
	 * For now this just blindly iterates over the RTGs for an initial
	 * allocation.  We could try to keep an in-memory rtg_longest member
	 * to avoid the locking when just looking for big enough free space,
	 * but for now this keeps things simple.
	 */
	if (bno_hint != NULLFSBLOCK)
		start_rgno = xfs_rtb_to_rgno(mp, bno_hint);
	else
		start_rgno = (atomic_inc_return(&mp->m_rtgrotor) - 1) %
				mp->m_sb.sb_rgcount;

	rgno = start_rgno;
	do {
		bool		rtlocked = false;

		error = xfs_rtallocate_rtg(tp, rgno, bno_hint, minlen, maxlen,
				prod, wasdel, initial_user_data, &rtlocked,
				bno, blen);
		if (error != -ENOSPC)
			return error;
		ASSERT(!rtlocked);

		if (++rgno == mp->m_sb.sb_rgcount)
			rgno = 0;
		bno_hint = NULLFSBLOCK;
	} while (rgno != start_rgno);

	return -ENOSPC;
}

static int
xfs_rtallocate_align(
	struct xfs_bmalloca	*ap,
	xfs_rtxlen_t		*ralen,
	xfs_rtxlen_t		*raminlen,
	xfs_rtxlen_t		*prod,
	bool			*noalign)
{
	struct xfs_mount	*mp = ap->ip->i_mount;
	xfs_fileoff_t		orig_offset = ap->offset;
	xfs_extlen_t		minlen = mp->m_sb.sb_rextsize;
	xfs_extlen_t            align;	/* minimum allocation alignment */
	xfs_extlen_t		mod;	/* product factor for allocators */
	int			error;

	if (*noalign) {
		align = mp->m_sb.sb_rextsize;
	} else {
		if (ap->flags & XFS_BMAPI_COWFORK)
			align = xfs_get_cowextsz_hint(ap->ip);
		else
			align = xfs_get_extsz_hint(ap->ip);
		if (!align)
			align = 1;
		if (align == mp->m_sb.sb_rextsize)
			*noalign = true;
	}

	error = xfs_bmap_extsize_align(mp, &ap->got, &ap->prev, align, 1,
			ap->eof, 0, ap->conv, &ap->offset, &ap->length);
	if (error)
		return error;
	ASSERT(ap->length);
	ASSERT(xfs_extlen_to_rtxmod(mp, ap->length) == 0);

	/*
	 * If we shifted the file offset downward to satisfy an extent size
	 * hint, increase minlen by that amount so that the allocator won't
	 * give us an allocation that's too short to cover at least one of the
	 * blocks that the caller asked for.
	 */
	if (ap->offset != orig_offset)
		minlen += orig_offset - ap->offset;

	/*
	 * Set ralen to be the actual requested length in rtextents.
	 *
	 * If the old value was close enough to XFS_BMBT_MAX_EXTLEN that
	 * we rounded up to it, cut it back so it's valid again.
	 * Note that if it's a really large request (bigger than
	 * XFS_BMBT_MAX_EXTLEN), we don't hear about that number, and can't
	 * adjust the starting point to match it.
	 */
	*ralen = xfs_extlen_to_rtxlen(mp, min(ap->length, XFS_MAX_BMBT_EXTLEN));
	*raminlen = max_t(xfs_rtxlen_t, 1, xfs_extlen_to_rtxlen(mp, minlen));
	ASSERT(*raminlen > 0);
	ASSERT(*raminlen <= *ralen);

	/*
	 * Only bother calculating a real prod factor if offset & length are
	 * perfectly aligned, otherwise it will just get us in trouble.
	 */
	div_u64_rem(ap->offset, align, &mod);
	if (mod || ap->length % align)
		*prod = 1;
	else
		*prod = xfs_extlen_to_rtxlen(mp, align);

	if (*prod > 1)
		xfs_rtalloc_align_minmax(raminlen, ralen, prod);
	return 0;
}

int
xfs_bmap_rtalloc(
	struct xfs_bmalloca	*ap)
{
	xfs_fileoff_t		orig_offset = ap->offset;
	xfs_rtxlen_t		prod = 0;  /* product factor for allocators */
	xfs_rtxlen_t		ralen = 0; /* realtime allocation length */
	xfs_rtblock_t		bno_hint = NULLRTBLOCK;
	xfs_extlen_t		orig_length = ap->length;
	xfs_rtxlen_t		raminlen;
	bool			rtlocked = false;
	bool			noalign = false;
	bool			initial_user_data =
		ap->datatype & XFS_ALLOC_INITIAL_USER_DATA;
	int			error;

	ASSERT(!xfs_has_zoned(ap->tp->t_mountp));

retry:
	error = xfs_rtallocate_align(ap, &ralen, &raminlen, &prod, &noalign);
	if (error)
		return error;

	if (xfs_bmap_adjacent(ap))
		bno_hint = ap->blkno;

	if (xfs_has_rtgroups(ap->ip->i_mount)) {
		error = xfs_rtallocate_rtgs(ap->tp, bno_hint, raminlen, ralen,
				prod, ap->wasdel, initial_user_data,
				&ap->blkno, &ap->length);
	} else {
		error = xfs_rtallocate_rtg(ap->tp, 0, bno_hint, raminlen, ralen,
				prod, ap->wasdel, initial_user_data,
				&rtlocked, &ap->blkno, &ap->length);
	}

	if (error == -ENOSPC) {
		if (!noalign) {
			/*
			 * We previously enlarged the request length to try to
			 * satisfy an extent size hint.  The allocator didn't
			 * return anything, so reset the parameters to the
			 * original values and try again without alignment
			 * criteria.
			 */
			ap->offset = orig_offset;
			ap->length = orig_length;
			noalign = true;
			goto retry;
		}

		ap->blkno = NULLFSBLOCK;
		ap->length = 0;
		return 0;
	}
	if (error)
		return error;

	xfs_bmap_alloc_account(ap);
	return 0;
}
