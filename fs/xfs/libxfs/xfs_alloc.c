/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_error.h"
#include "xfs_cksum.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_ag_resv.h"

struct workqueue_struct *xfs_alloc_wq;

#define XFS_ABSDIFF(a,b)	(((a) <= (b)) ? ((b) - (a)) : ((a) - (b)))

#define	XFSA_FIXUP_BNO_OK	1
#define	XFSA_FIXUP_CNT_OK	2

STATIC int xfs_alloc_ag_vextent_exact(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_near(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_size(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_small(xfs_alloc_arg_t *,
		xfs_btree_cur_t *, xfs_agblock_t *, xfs_extlen_t *, int *);

unsigned int
xfs_refc_block(
	struct xfs_mount	*mp)
{
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		return XFS_RMAP_BLOCK(mp) + 1;
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		return XFS_FIBT_BLOCK(mp) + 1;
	return XFS_IBT_BLOCK(mp) + 1;
}

xfs_extlen_t
xfs_prealloc_blocks(
	struct xfs_mount	*mp)
{
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		return xfs_refc_block(mp) + 1;
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		return XFS_RMAP_BLOCK(mp) + 1;
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		return XFS_FIBT_BLOCK(mp) + 1;
	return XFS_IBT_BLOCK(mp) + 1;
}

/*
 * In order to avoid ENOSPC-related deadlock caused by out-of-order locking of
 * AGF buffer (PV 947395), we place constraints on the relationship among
 * actual allocations for data blocks, freelist blocks, and potential file data
 * bmap btree blocks. However, these restrictions may result in no actual space
 * allocated for a delayed extent, for example, a data block in a certain AG is
 * allocated but there is no additional block for the additional bmap btree
 * block due to a split of the bmap btree of the file. The result of this may
 * lead to an infinite loop when the file gets flushed to disk and all delayed
 * extents need to be actually allocated. To get around this, we explicitly set
 * aside a few blocks which will not be reserved in delayed allocation.
 *
 * We need to reserve 4 fsbs _per AG_ for the freelist and 4 more to handle a
 * potential split of the file's bmap btree.
 */
unsigned int
xfs_alloc_set_aside(
	struct xfs_mount	*mp)
{
	unsigned int		blocks;

	blocks = 4 + (mp->m_sb.sb_agcount * XFS_ALLOC_AGFL_RESERVE);
	return blocks;
}

/*
 * When deciding how much space to allocate out of an AG, we limit the
 * allocation maximum size to the size the AG. However, we cannot use all the
 * blocks in the AG - some are permanently used by metadata. These
 * blocks are generally:
 *	- the AG superblock, AGF, AGI and AGFL
 *	- the AGF (bno and cnt) and AGI btree root blocks, and optionally
 *	  the AGI free inode and rmap btree root blocks.
 *	- blocks on the AGFL according to xfs_alloc_set_aside() limits
 *	- the rmapbt root block
 *
 * The AG headers are sector sized, so the amount of space they take up is
 * dependent on filesystem geometry. The others are all single blocks.
 */
unsigned int
xfs_alloc_ag_max_usable(
	struct xfs_mount	*mp)
{
	unsigned int		blocks;

	blocks = XFS_BB_TO_FSB(mp, XFS_FSS_TO_BB(mp, 4)); /* ag headers */
	blocks += XFS_ALLOC_AGFL_RESERVE;
	blocks += 3;			/* AGF, AGI btree root blocks */
	if (xfs_sb_version_hasfinobt(&mp->m_sb))
		blocks++;		/* finobt root block */
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		blocks++; 		/* rmap root block */

	return mp->m_sb.sb_agblocks - blocks;
}

/*
 * Lookup the record equal to [bno, len] in the btree given by cur.
 */
STATIC int				/* error */
xfs_alloc_lookup_eq(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len,	/* length of extent */
	int			*stat)	/* success/failure */
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
}

/*
 * Lookup the first record greater than or equal to [bno, len]
 * in the btree given by cur.
 */
int				/* error */
xfs_alloc_lookup_ge(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len,	/* length of extent */
	int			*stat)	/* success/failure */
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_btree_lookup(cur, XFS_LOOKUP_GE, stat);
}

/*
 * Lookup the first record less than or equal to [bno, len]
 * in the btree given by cur.
 */
static int				/* error */
xfs_alloc_lookup_le(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len,	/* length of extent */
	int			*stat)	/* success/failure */
{
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	return xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Update the record referred to by cur to the value given
 * by [bno, len].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int				/* error */
xfs_alloc_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len)	/* length of extent */
{
	union xfs_btree_rec	rec;

	rec.alloc.ar_startblock = cpu_to_be32(bno);
	rec.alloc.ar_blockcount = cpu_to_be32(len);
	return xfs_btree_update(cur, &rec);
}

/*
 * Get the data from the pointed-to record.
 */
int					/* error */
xfs_alloc_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		*bno,	/* output: starting block of extent */
	xfs_extlen_t		*len,	/* output: length of extent */
	int			*stat)	/* output: success/failure */
{
	union xfs_btree_rec	*rec;
	int			error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (!error && *stat == 1) {
		*bno = be32_to_cpu(rec->alloc.ar_startblock);
		*len = be32_to_cpu(rec->alloc.ar_blockcount);
	}
	return error;
}

/*
 * Compute aligned version of the found extent.
 * Takes alignment and min length into account.
 */
STATIC void
xfs_alloc_compute_aligned(
	xfs_alloc_arg_t	*args,		/* allocation argument structure */
	xfs_agblock_t	foundbno,	/* starting block in found extent */
	xfs_extlen_t	foundlen,	/* length in found extent */
	xfs_agblock_t	*resbno,	/* result block number */
	xfs_extlen_t	*reslen)	/* result length */
{
	xfs_agblock_t	bno;
	xfs_extlen_t	len;
	xfs_extlen_t	diff;

	/* Trim busy sections out of found extent */
	xfs_extent_busy_trim(args, foundbno, foundlen, &bno, &len);

	/*
	 * If we have a largish extent that happens to start before min_agbno,
	 * see if we can shift it into range...
	 */
	if (bno < args->min_agbno && bno + len > args->min_agbno) {
		diff = args->min_agbno - bno;
		if (len > diff) {
			bno += diff;
			len -= diff;
		}
	}

	if (args->alignment > 1 && len >= args->minlen) {
		xfs_agblock_t	aligned_bno = roundup(bno, args->alignment);

		diff = aligned_bno - bno;

		*resbno = aligned_bno;
		*reslen = diff >= len ? 0 : len - diff;
	} else {
		*resbno = bno;
		*reslen = len;
	}
}

/*
 * Compute best start block and diff for "near" allocations.
 * freelen >= wantlen already checked by caller.
 */
STATIC xfs_extlen_t			/* difference value (absolute) */
xfs_alloc_compute_diff(
	xfs_agblock_t	wantbno,	/* target starting block */
	xfs_extlen_t	wantlen,	/* target length */
	xfs_extlen_t	alignment,	/* target alignment */
	int		datatype,	/* are we allocating data? */
	xfs_agblock_t	freebno,	/* freespace's starting block */
	xfs_extlen_t	freelen,	/* freespace's length */
	xfs_agblock_t	*newbnop)	/* result: best start block from free */
{
	xfs_agblock_t	freeend;	/* end of freespace extent */
	xfs_agblock_t	newbno1;	/* return block number */
	xfs_agblock_t	newbno2;	/* other new block number */
	xfs_extlen_t	newlen1=0;	/* length with newbno1 */
	xfs_extlen_t	newlen2=0;	/* length with newbno2 */
	xfs_agblock_t	wantend;	/* end of target extent */
	bool		userdata = xfs_alloc_is_userdata(datatype);

	ASSERT(freelen >= wantlen);
	freeend = freebno + freelen;
	wantend = wantbno + wantlen;
	/*
	 * We want to allocate from the start of a free extent if it is past
	 * the desired block or if we are allocating user data and the free
	 * extent is before desired block. The second case is there to allow
	 * for contiguous allocation from the remaining free space if the file
	 * grows in the short term.
	 */
	if (freebno >= wantbno || (userdata && freeend < wantend)) {
		if ((newbno1 = roundup(freebno, alignment)) >= freeend)
			newbno1 = NULLAGBLOCK;
	} else if (freeend >= wantend && alignment > 1) {
		newbno1 = roundup(wantbno, alignment);
		newbno2 = newbno1 - alignment;
		if (newbno1 >= freeend)
			newbno1 = NULLAGBLOCK;
		else
			newlen1 = XFS_EXTLEN_MIN(wantlen, freeend - newbno1);
		if (newbno2 < freebno)
			newbno2 = NULLAGBLOCK;
		else
			newlen2 = XFS_EXTLEN_MIN(wantlen, freeend - newbno2);
		if (newbno1 != NULLAGBLOCK && newbno2 != NULLAGBLOCK) {
			if (newlen1 < newlen2 ||
			    (newlen1 == newlen2 &&
			     XFS_ABSDIFF(newbno1, wantbno) >
			     XFS_ABSDIFF(newbno2, wantbno)))
				newbno1 = newbno2;
		} else if (newbno2 != NULLAGBLOCK)
			newbno1 = newbno2;
	} else if (freeend >= wantend) {
		newbno1 = wantbno;
	} else if (alignment > 1) {
		newbno1 = roundup(freeend - wantlen, alignment);
		if (newbno1 > freeend - wantlen &&
		    newbno1 - alignment >= freebno)
			newbno1 -= alignment;
		else if (newbno1 >= freeend)
			newbno1 = NULLAGBLOCK;
	} else
		newbno1 = freeend - wantlen;
	*newbnop = newbno1;
	return newbno1 == NULLAGBLOCK ? 0 : XFS_ABSDIFF(newbno1, wantbno);
}

/*
 * Fix up the length, based on mod and prod.
 * len should be k * prod + mod for some k.
 * If len is too small it is returned unchanged.
 * If len hits maxlen it is left alone.
 */
STATIC void
xfs_alloc_fix_len(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_extlen_t	k;
	xfs_extlen_t	rlen;

	ASSERT(args->mod < args->prod);
	rlen = args->len;
	ASSERT(rlen >= args->minlen);
	ASSERT(rlen <= args->maxlen);
	if (args->prod <= 1 || rlen < args->mod || rlen == args->maxlen ||
	    (args->mod == 0 && rlen < args->prod))
		return;
	k = rlen % args->prod;
	if (k == args->mod)
		return;
	if (k > args->mod)
		rlen = rlen - (k - args->mod);
	else
		rlen = rlen - args->prod + (args->mod - k);
	/* casts to (int) catch length underflows */
	if ((int)rlen < (int)args->minlen)
		return;
	ASSERT(rlen >= args->minlen && rlen <= args->maxlen);
	ASSERT(rlen % args->prod == args->mod);
	args->len = rlen;
}

/*
 * Fix up length if there is too little space left in the a.g.
 * Return 1 if ok, 0 if too little, should give up.
 */
STATIC int
xfs_alloc_fix_minleft(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_agf_t	*agf;		/* a.g. freelist header */
	int		diff;		/* free space difference */

	if (args->minleft == 0)
		return 1;
	agf = XFS_BUF_TO_AGF(args->agbp);
	diff = be32_to_cpu(agf->agf_freeblks)
		- args->len - args->minleft;
	if (diff >= 0)
		return 1;
	args->len += diff;		/* shrink the allocated space */
	/* casts to (int) catch length underflows */
	if ((int)args->len >= (int)args->minlen)
		return 1;
	args->agbno = NULLAGBLOCK;
	return 0;
}

/*
 * Update the two btrees, logically removing from freespace the extent
 * starting at rbno, rlen blocks.  The extent is contained within the
 * actual (current) free extent fbno for flen blocks.
 * Flags are passed in indicating whether the cursors are set to the
 * relevant records.
 */
STATIC int				/* error code */
xfs_alloc_fixup_trees(
	xfs_btree_cur_t	*cnt_cur,	/* cursor for by-size btree */
	xfs_btree_cur_t	*bno_cur,	/* cursor for by-block btree */
	xfs_agblock_t	fbno,		/* starting block of free extent */
	xfs_extlen_t	flen,		/* length of free extent */
	xfs_agblock_t	rbno,		/* starting block of returned extent */
	xfs_extlen_t	rlen,		/* length of returned extent */
	int		flags)		/* flags, XFSA_FIXUP_... */
{
	int		error;		/* error code */
	int		i;		/* operation results */
	xfs_agblock_t	nfbno1;		/* first new free startblock */
	xfs_agblock_t	nfbno2;		/* second new free startblock */
	xfs_extlen_t	nflen1=0;	/* first new free length */
	xfs_extlen_t	nflen2=0;	/* second new free length */
	struct xfs_mount *mp;

	mp = cnt_cur->bc_mp;

	/*
	 * Look up the record in the by-size tree if necessary.
	 */
	if (flags & XFSA_FIXUP_CNT_OK) {
#ifdef DEBUG
		if ((error = xfs_alloc_get_rec(cnt_cur, &nfbno1, &nflen1, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp,
			i == 1 && nfbno1 == fbno && nflen1 == flen);
#endif
	} else {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, fbno, flen, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	}
	/*
	 * Look up the record in the by-block tree if necessary.
	 */
	if (flags & XFSA_FIXUP_BNO_OK) {
#ifdef DEBUG
		if ((error = xfs_alloc_get_rec(bno_cur, &nfbno1, &nflen1, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp,
			i == 1 && nfbno1 == fbno && nflen1 == flen);
#endif
	} else {
		if ((error = xfs_alloc_lookup_eq(bno_cur, fbno, flen, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	}

#ifdef DEBUG
	if (bno_cur->bc_nlevels == 1 && cnt_cur->bc_nlevels == 1) {
		struct xfs_btree_block	*bnoblock;
		struct xfs_btree_block	*cntblock;

		bnoblock = XFS_BUF_TO_BLOCK(bno_cur->bc_bufs[0]);
		cntblock = XFS_BUF_TO_BLOCK(cnt_cur->bc_bufs[0]);

		XFS_WANT_CORRUPTED_RETURN(mp,
			bnoblock->bb_numrecs == cntblock->bb_numrecs);
	}
#endif

	/*
	 * Deal with all four cases: the allocated record is contained
	 * within the freespace record, so we can have new freespace
	 * at either (or both) end, or no freespace remaining.
	 */
	if (rbno == fbno && rlen == flen)
		nfbno1 = nfbno2 = NULLAGBLOCK;
	else if (rbno == fbno) {
		nfbno1 = rbno + rlen;
		nflen1 = flen - rlen;
		nfbno2 = NULLAGBLOCK;
	} else if (rbno + rlen == fbno + flen) {
		nfbno1 = fbno;
		nflen1 = flen - rlen;
		nfbno2 = NULLAGBLOCK;
	} else {
		nfbno1 = fbno;
		nflen1 = rbno - fbno;
		nfbno2 = rbno + rlen;
		nflen2 = (fbno + flen) - nfbno2;
	}
	/*
	 * Delete the entry from the by-size btree.
	 */
	if ((error = xfs_btree_delete(cnt_cur, &i)))
		return error;
	XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	/*
	 * Add new by-size btree entry(s).
	 */
	if (nfbno1 != NULLAGBLOCK) {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, nfbno1, nflen1, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 0);
		if ((error = xfs_btree_insert(cnt_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	}
	if (nfbno2 != NULLAGBLOCK) {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, nfbno2, nflen2, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 0);
		if ((error = xfs_btree_insert(cnt_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	}
	/*
	 * Fix up the by-block btree entry(s).
	 */
	if (nfbno1 == NULLAGBLOCK) {
		/*
		 * No remaining freespace, just delete the by-block tree entry.
		 */
		if ((error = xfs_btree_delete(bno_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	} else {
		/*
		 * Update the by-block entry to start later|be shorter.
		 */
		if ((error = xfs_alloc_update(bno_cur, nfbno1, nflen1)))
			return error;
	}
	if (nfbno2 != NULLAGBLOCK) {
		/*
		 * 2 resulting free entries, need to add one.
		 */
		if ((error = xfs_alloc_lookup_eq(bno_cur, nfbno2, nflen2, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 0);
		if ((error = xfs_btree_insert(bno_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(mp, i == 1);
	}
	return 0;
}

static bool
xfs_agfl_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_agfl	*agfl = XFS_BUF_TO_AGFL(bp);
	int		i;

	if (!uuid_equal(&agfl->agfl_uuid, &mp->m_sb.sb_meta_uuid))
		return false;
	if (be32_to_cpu(agfl->agfl_magicnum) != XFS_AGFL_MAGIC)
		return false;
	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agfl->agfl_seqno) != bp->b_pag->pag_agno)
		return false;

	for (i = 0; i < XFS_AGFL_SIZE(mp); i++) {
		if (be32_to_cpu(agfl->agfl_bno[i]) != NULLAGBLOCK &&
		    be32_to_cpu(agfl->agfl_bno[i]) >= mp->m_sb.sb_agblocks)
			return false;
	}

	return xfs_log_check_lsn(mp,
				 be64_to_cpu(XFS_BUF_TO_AGFL(bp)->agfl_lsn));
}

static void
xfs_agfl_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	/*
	 * There is no verification of non-crc AGFLs because mkfs does not
	 * initialise the AGFL to zero or NULL. Hence the only valid part of the
	 * AGFL is what the AGF says is active. We can't get to the AGF, so we
	 * can't verify just those entries are valid.
	 */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (!xfs_buf_verify_cksum(bp, XFS_AGFL_CRC_OFF))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (!xfs_agfl_verify(bp))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error)
		xfs_verifier_error(bp);
}

static void
xfs_agfl_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	/* no verification of non-crc AGFLs */
	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (!xfs_agfl_verify(bp)) {
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}

	if (bip)
		XFS_BUF_TO_AGFL(bp)->agfl_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_AGFL_CRC_OFF);
}

const struct xfs_buf_ops xfs_agfl_buf_ops = {
	.name = "xfs_agfl",
	.verify_read = xfs_agfl_read_verify,
	.verify_write = xfs_agfl_write_verify,
};

/*
 * Read in the allocation group free block array.
 */
STATIC int				/* error */
xfs_alloc_read_agfl(
	xfs_mount_t	*mp,		/* mount point structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_buf_t	**bpp)		/* buffer for the ag free block array */
{
	xfs_buf_t	*bp;		/* return value */
	int		error;

	ASSERT(agno != NULLAGNUMBER);
	error = xfs_trans_read_buf(
			mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agno, XFS_AGFL_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &bp, &xfs_agfl_buf_ops);
	if (error)
		return error;
	xfs_buf_set_ref(bp, XFS_AGFL_REF);
	*bpp = bp;
	return 0;
}

STATIC int
xfs_alloc_update_counters(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agbp,
	long			len)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);

	pag->pagf_freeblks += len;
	be32_add_cpu(&agf->agf_freeblks, len);

	xfs_trans_agblocks_delta(tp, len);
	if (unlikely(be32_to_cpu(agf->agf_freeblks) >
		     be32_to_cpu(agf->agf_length)))
		return -EFSCORRUPTED;

	xfs_alloc_log_agf(tp, agbp, XFS_AGF_FREEBLKS);
	return 0;
}

/*
 * Allocation group level functions.
 */

/*
 * Allocate a variable extent in the allocation group agno.
 * Type and bno are used to determine where in the allocation group the
 * extent will start.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int			/* error */
xfs_alloc_ag_vextent(
	xfs_alloc_arg_t	*args)	/* argument structure for allocation */
{
	int		error=0;
	xfs_extlen_t	reservation;
	xfs_extlen_t	oldmax;

	ASSERT(args->minlen > 0);
	ASSERT(args->maxlen > 0);
	ASSERT(args->minlen <= args->maxlen);
	ASSERT(args->mod < args->prod);
	ASSERT(args->alignment > 0);

	/*
	 * Clamp maxlen to the amount of free space minus any reservations
	 * that have been made.
	 */
	oldmax = args->maxlen;
	reservation = xfs_ag_resv_needed(args->pag, args->resv);
	if (args->maxlen > args->pag->pagf_freeblks - reservation)
		args->maxlen = args->pag->pagf_freeblks - reservation;
	if (args->maxlen == 0) {
		args->agbno = NULLAGBLOCK;
		args->maxlen = oldmax;
		return 0;
	}

	/*
	 * Branch to correct routine based on the type.
	 */
	args->wasfromfl = 0;
	switch (args->type) {
	case XFS_ALLOCTYPE_THIS_AG:
		error = xfs_alloc_ag_vextent_size(args);
		break;
	case XFS_ALLOCTYPE_NEAR_BNO:
		error = xfs_alloc_ag_vextent_near(args);
		break;
	case XFS_ALLOCTYPE_THIS_BNO:
		error = xfs_alloc_ag_vextent_exact(args);
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}

	args->maxlen = oldmax;

	if (error || args->agbno == NULLAGBLOCK)
		return error;

	ASSERT(args->len >= args->minlen);
	ASSERT(args->len <= args->maxlen);
	ASSERT(!args->wasfromfl || args->resv != XFS_AG_RESV_AGFL);
	ASSERT(args->agbno % args->alignment == 0);

	/* if not file data, insert new block into the reverse map btree */
	if (args->oinfo.oi_owner != XFS_RMAP_OWN_UNKNOWN) {
		error = xfs_rmap_alloc(args->tp, args->agbp, args->agno,
				       args->agbno, args->len, &args->oinfo);
		if (error)
			return error;
	}

	if (!args->wasfromfl) {
		error = xfs_alloc_update_counters(args->tp, args->pag,
						  args->agbp,
						  -((long)(args->len)));
		if (error)
			return error;

		ASSERT(!xfs_extent_busy_search(args->mp, args->agno,
					      args->agbno, args->len));
	}

	xfs_ag_resv_alloc_extent(args->pag, args->resv, args);

	XFS_STATS_INC(args->mp, xs_allocx);
	XFS_STATS_ADD(args->mp, xs_allocb, args->len);
	return error;
}

/*
 * Allocate a variable extent at exactly agno/bno.
 * Extent's length (returned in *len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block (bno), or NULLAGBLOCK if we can't do it.
 */
STATIC int			/* error */
xfs_alloc_ag_vextent_exact(
	xfs_alloc_arg_t	*args)	/* allocation argument structure */
{
	xfs_btree_cur_t	*bno_cur;/* by block-number btree cursor */
	xfs_btree_cur_t	*cnt_cur;/* by count btree cursor */
	int		error;
	xfs_agblock_t	fbno;	/* start block of found extent */
	xfs_extlen_t	flen;	/* length of found extent */
	xfs_agblock_t	tbno;	/* start block of trimmed extent */
	xfs_extlen_t	tlen;	/* length of trimmed extent */
	xfs_agblock_t	tend;	/* end block of trimmed extent */
	int		i;	/* success/failure of operation */

	ASSERT(args->alignment == 1);

	/*
	 * Allocate/initialize a cursor for the by-number freespace btree.
	 */
	bno_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
					  args->agno, XFS_BTNUM_BNO);

	/*
	 * Lookup bno and minlen in the btree (minlen is irrelevant, really).
	 * Look for the closest free block <= bno, it must contain bno
	 * if any free block does.
	 */
	error = xfs_alloc_lookup_le(bno_cur, args->agbno, args->minlen, &i);
	if (error)
		goto error0;
	if (!i)
		goto not_found;

	/*
	 * Grab the freespace record.
	 */
	error = xfs_alloc_get_rec(bno_cur, &fbno, &flen, &i);
	if (error)
		goto error0;
	XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
	ASSERT(fbno <= args->agbno);

	/*
	 * Check for overlapping busy extents.
	 */
	xfs_extent_busy_trim(args, fbno, flen, &tbno, &tlen);

	/*
	 * Give up if the start of the extent is busy, or the freespace isn't
	 * long enough for the minimum request.
	 */
	if (tbno > args->agbno)
		goto not_found;
	if (tlen < args->minlen)
		goto not_found;
	tend = tbno + tlen;
	if (tend < args->agbno + args->minlen)
		goto not_found;

	/*
	 * End of extent will be smaller of the freespace end and the
	 * maximal requested end.
	 *
	 * Fix the length according to mod and prod if given.
	 */
	args->len = XFS_AGBLOCK_MIN(tend, args->agbno + args->maxlen)
						- args->agbno;
	xfs_alloc_fix_len(args);
	if (!xfs_alloc_fix_minleft(args))
		goto not_found;

	ASSERT(args->agbno + args->len <= tend);

	/*
	 * We are allocating agbno for args->len
	 * Allocate/initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT);
	ASSERT(args->agbno + args->len <=
		be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length));
	error = xfs_alloc_fixup_trees(cnt_cur, bno_cur, fbno, flen, args->agbno,
				      args->len, XFSA_FIXUP_BNO_OK);
	if (error) {
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_ERROR);
		goto error0;
	}

	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);

	args->wasfromfl = 0;
	trace_xfs_alloc_exact_done(args);
	return 0;

not_found:
	/* Didn't find it, return null. */
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	args->agbno = NULLAGBLOCK;
	trace_xfs_alloc_exact_notfound(args);
	return 0;

error0:
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_ERROR);
	trace_xfs_alloc_exact_error(args);
	return error;
}

/*
 * Search the btree in a given direction via the search cursor and compare
 * the records found against the good extent we've already found.
 */
STATIC int
xfs_alloc_find_best_extent(
	struct xfs_alloc_arg	*args,	/* allocation argument structure */
	struct xfs_btree_cur	**gcur,	/* good cursor */
	struct xfs_btree_cur	**scur,	/* searching cursor */
	xfs_agblock_t		gdiff,	/* difference for search comparison */
	xfs_agblock_t		*sbno,	/* extent found by search */
	xfs_extlen_t		*slen,	/* extent length */
	xfs_agblock_t		*sbnoa,	/* aligned extent found by search */
	xfs_extlen_t		*slena,	/* aligned extent length */
	int			dir)	/* 0 = search right, 1 = search left */
{
	xfs_agblock_t		new;
	xfs_agblock_t		sdiff;
	int			error;
	int			i;

	/* The good extent is perfect, no need to  search. */
	if (!gdiff)
		goto out_use_good;

	/*
	 * Look until we find a better one, run out of space or run off the end.
	 */
	do {
		error = xfs_alloc_get_rec(*scur, sbno, slen, &i);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
		xfs_alloc_compute_aligned(args, *sbno, *slen, sbnoa, slena);

		/*
		 * The good extent is closer than this one.
		 */
		if (!dir) {
			if (*sbnoa > args->max_agbno)
				goto out_use_good;
			if (*sbnoa >= args->agbno + gdiff)
				goto out_use_good;
		} else {
			if (*sbnoa < args->min_agbno)
				goto out_use_good;
			if (*sbnoa <= args->agbno - gdiff)
				goto out_use_good;
		}

		/*
		 * Same distance, compare length and pick the best.
		 */
		if (*slena >= args->minlen) {
			args->len = XFS_EXTLEN_MIN(*slena, args->maxlen);
			xfs_alloc_fix_len(args);

			sdiff = xfs_alloc_compute_diff(args->agbno, args->len,
						       args->alignment,
						       args->datatype, *sbnoa,
						       *slena, &new);

			/*
			 * Choose closer size and invalidate other cursor.
			 */
			if (sdiff < gdiff)
				goto out_use_search;
			goto out_use_good;
		}

		if (!dir)
			error = xfs_btree_increment(*scur, 0, &i);
		else
			error = xfs_btree_decrement(*scur, 0, &i);
		if (error)
			goto error0;
	} while (i);

out_use_good:
	xfs_btree_del_cursor(*scur, XFS_BTREE_NOERROR);
	*scur = NULL;
	return 0;

out_use_search:
	xfs_btree_del_cursor(*gcur, XFS_BTREE_NOERROR);
	*gcur = NULL;
	return 0;

error0:
	/* caller invalidates cursors */
	return error;
}

/*
 * Allocate a variable extent near bno in the allocation group agno.
 * Extent's length (returned in len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int				/* error */
xfs_alloc_ag_vextent_near(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_btree_cur_t	*bno_cur_gt;	/* cursor for bno btree, right side */
	xfs_btree_cur_t	*bno_cur_lt;	/* cursor for bno btree, left side */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for count btree */
	xfs_agblock_t	gtbno;		/* start bno of right side entry */
	xfs_agblock_t	gtbnoa;		/* aligned ... */
	xfs_extlen_t	gtdiff;		/* difference to right side entry */
	xfs_extlen_t	gtlen;		/* length of right side entry */
	xfs_extlen_t	gtlena;		/* aligned ... */
	xfs_agblock_t	gtnew;		/* useful start bno of right side */
	int		error;		/* error code */
	int		i;		/* result code, temporary */
	int		j;		/* result code, temporary */
	xfs_agblock_t	ltbno;		/* start bno of left side entry */
	xfs_agblock_t	ltbnoa;		/* aligned ... */
	xfs_extlen_t	ltdiff;		/* difference to left side entry */
	xfs_extlen_t	ltlen;		/* length of left side entry */
	xfs_extlen_t	ltlena;		/* aligned ... */
	xfs_agblock_t	ltnew;		/* useful start bno of left side */
	xfs_extlen_t	rlen;		/* length of returned extent */
	int		forced = 0;
#ifdef DEBUG
	/*
	 * Randomly don't execute the first algorithm.
	 */
	int		dofirst;	/* set to do first algorithm */

	dofirst = prandom_u32() & 1;
#endif

	/* handle unitialized agbno range so caller doesn't have to */
	if (!args->min_agbno && !args->max_agbno)
		args->max_agbno = args->mp->m_sb.sb_agblocks - 1;
	ASSERT(args->min_agbno <= args->max_agbno);

	/* clamp agbno to the range if it's outside */
	if (args->agbno < args->min_agbno)
		args->agbno = args->min_agbno;
	if (args->agbno > args->max_agbno)
		args->agbno = args->max_agbno;

restart:
	bno_cur_lt = NULL;
	bno_cur_gt = NULL;
	ltlen = 0;
	gtlena = 0;
	ltlena = 0;

	/*
	 * Get a cursor for the by-size btree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT);

	/*
	 * See if there are any free extents as big as maxlen.
	 */
	if ((error = xfs_alloc_lookup_ge(cnt_cur, 0, args->maxlen, &i)))
		goto error0;
	/*
	 * If none, then pick up the last entry in the tree unless the
	 * tree is empty.
	 */
	if (!i) {
		if ((error = xfs_alloc_ag_vextent_small(args, cnt_cur, &ltbno,
				&ltlen, &i)))
			goto error0;
		if (i == 0 || ltlen == 0) {
			xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
			trace_xfs_alloc_near_noentry(args);
			return 0;
		}
		ASSERT(i == 1);
	}
	args->wasfromfl = 0;

	/*
	 * First algorithm.
	 * If the requested extent is large wrt the freespaces available
	 * in this a.g., then the cursor will be pointing to a btree entry
	 * near the right edge of the tree.  If it's in the last btree leaf
	 * block, then we just examine all the entries in that block
	 * that are big enough, and pick the best one.
	 * This is written as a while loop so we can break out of it,
	 * but we never loop back to the top.
	 */
	while (xfs_btree_islastblock(cnt_cur, 0)) {
		xfs_extlen_t	bdiff;
		int		besti=0;
		xfs_extlen_t	blen=0;
		xfs_agblock_t	bnew=0;

#ifdef DEBUG
		if (dofirst)
			break;
#endif
		/*
		 * Start from the entry that lookup found, sequence through
		 * all larger free blocks.  If we're actually pointing at a
		 * record smaller than maxlen, go to the start of this block,
		 * and skip all those smaller than minlen.
		 */
		if (ltlen || args->alignment > 1) {
			cnt_cur->bc_ptrs[0] = 1;
			do {
				if ((error = xfs_alloc_get_rec(cnt_cur, &ltbno,
						&ltlen, &i)))
					goto error0;
				XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
				if (ltlen >= args->minlen)
					break;
				if ((error = xfs_btree_increment(cnt_cur, 0, &i)))
					goto error0;
			} while (i);
			ASSERT(ltlen >= args->minlen);
			if (!i)
				break;
		}
		i = cnt_cur->bc_ptrs[0];
		for (j = 1, blen = 0, bdiff = 0;
		     !error && j && (blen < args->maxlen || bdiff > 0);
		     error = xfs_btree_increment(cnt_cur, 0, &j)) {
			/*
			 * For each entry, decide if it's better than
			 * the previous best entry.
			 */
			if ((error = xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
			xfs_alloc_compute_aligned(args, ltbno, ltlen,
						  &ltbnoa, &ltlena);
			if (ltlena < args->minlen)
				continue;
			if (ltbnoa < args->min_agbno || ltbnoa > args->max_agbno)
				continue;
			args->len = XFS_EXTLEN_MIN(ltlena, args->maxlen);
			xfs_alloc_fix_len(args);
			ASSERT(args->len >= args->minlen);
			if (args->len < blen)
				continue;
			ltdiff = xfs_alloc_compute_diff(args->agbno, args->len,
				args->alignment, args->datatype, ltbnoa,
				ltlena, &ltnew);
			if (ltnew != NULLAGBLOCK &&
			    (args->len > blen || ltdiff < bdiff)) {
				bdiff = ltdiff;
				bnew = ltnew;
				blen = args->len;
				besti = cnt_cur->bc_ptrs[0];
			}
		}
		/*
		 * It didn't work.  We COULD be in a case where
		 * there's a good record somewhere, so try again.
		 */
		if (blen == 0)
			break;
		/*
		 * Point at the best entry, and retrieve it again.
		 */
		cnt_cur->bc_ptrs[0] = besti;
		if ((error = xfs_alloc_get_rec(cnt_cur, &ltbno, &ltlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
		ASSERT(ltbno + ltlen <= be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length));
		args->len = blen;
		if (!xfs_alloc_fix_minleft(args)) {
			xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
			trace_xfs_alloc_near_nominleft(args);
			return 0;
		}
		blen = args->len;
		/*
		 * We are allocating starting at bnew for blen blocks.
		 */
		args->agbno = bnew;
		ASSERT(bnew >= ltbno);
		ASSERT(bnew + blen <= ltbno + ltlen);
		/*
		 * Set up a cursor for the by-bno tree.
		 */
		bno_cur_lt = xfs_allocbt_init_cursor(args->mp, args->tp,
			args->agbp, args->agno, XFS_BTNUM_BNO);
		/*
		 * Fix up the btree entries.
		 */
		if ((error = xfs_alloc_fixup_trees(cnt_cur, bno_cur_lt, ltbno,
				ltlen, bnew, blen, XFSA_FIXUP_CNT_OK)))
			goto error0;
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
		xfs_btree_del_cursor(bno_cur_lt, XFS_BTREE_NOERROR);

		trace_xfs_alloc_near_first(args);
		return 0;
	}
	/*
	 * Second algorithm.
	 * Search in the by-bno tree to the left and to the right
	 * simultaneously, until in each case we find a space big enough,
	 * or run into the edge of the tree.  When we run into the edge,
	 * we deallocate that cursor.
	 * If both searches succeed, we compare the two spaces and pick
	 * the better one.
	 * With alignment, it's possible for both to fail; the upper
	 * level algorithm that picks allocation groups for allocations
	 * is not supposed to do this.
	 */
	/*
	 * Allocate and initialize the cursor for the leftward search.
	 */
	bno_cur_lt = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_BNO);
	/*
	 * Lookup <= bno to find the leftward search's starting point.
	 */
	if ((error = xfs_alloc_lookup_le(bno_cur_lt, args->agbno, args->maxlen, &i)))
		goto error0;
	if (!i) {
		/*
		 * Didn't find anything; use this cursor for the rightward
		 * search.
		 */
		bno_cur_gt = bno_cur_lt;
		bno_cur_lt = NULL;
	}
	/*
	 * Found something.  Duplicate the cursor for the rightward search.
	 */
	else if ((error = xfs_btree_dup_cursor(bno_cur_lt, &bno_cur_gt)))
		goto error0;
	/*
	 * Increment the cursor, so we will point at the entry just right
	 * of the leftward entry if any, or to the leftmost entry.
	 */
	if ((error = xfs_btree_increment(bno_cur_gt, 0, &i)))
		goto error0;
	if (!i) {
		/*
		 * It failed, there are no rightward entries.
		 */
		xfs_btree_del_cursor(bno_cur_gt, XFS_BTREE_NOERROR);
		bno_cur_gt = NULL;
	}
	/*
	 * Loop going left with the leftward cursor, right with the
	 * rightward cursor, until either both directions give up or
	 * we find an entry at least as big as minlen.
	 */
	do {
		if (bno_cur_lt) {
			if ((error = xfs_alloc_get_rec(bno_cur_lt, &ltbno, &ltlen, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
			xfs_alloc_compute_aligned(args, ltbno, ltlen,
						  &ltbnoa, &ltlena);
			if (ltlena >= args->minlen && ltbnoa >= args->min_agbno)
				break;
			if ((error = xfs_btree_decrement(bno_cur_lt, 0, &i)))
				goto error0;
			if (!i || ltbnoa < args->min_agbno) {
				xfs_btree_del_cursor(bno_cur_lt,
						     XFS_BTREE_NOERROR);
				bno_cur_lt = NULL;
			}
		}
		if (bno_cur_gt) {
			if ((error = xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
			xfs_alloc_compute_aligned(args, gtbno, gtlen,
						  &gtbnoa, &gtlena);
			if (gtlena >= args->minlen && gtbnoa <= args->max_agbno)
				break;
			if ((error = xfs_btree_increment(bno_cur_gt, 0, &i)))
				goto error0;
			if (!i || gtbnoa > args->max_agbno) {
				xfs_btree_del_cursor(bno_cur_gt,
						     XFS_BTREE_NOERROR);
				bno_cur_gt = NULL;
			}
		}
	} while (bno_cur_lt || bno_cur_gt);

	/*
	 * Got both cursors still active, need to find better entry.
	 */
	if (bno_cur_lt && bno_cur_gt) {
		if (ltlena >= args->minlen) {
			/*
			 * Left side is good, look for a right side entry.
			 */
			args->len = XFS_EXTLEN_MIN(ltlena, args->maxlen);
			xfs_alloc_fix_len(args);
			ltdiff = xfs_alloc_compute_diff(args->agbno, args->len,
				args->alignment, args->datatype, ltbnoa,
				ltlena, &ltnew);

			error = xfs_alloc_find_best_extent(args,
						&bno_cur_lt, &bno_cur_gt,
						ltdiff, &gtbno, &gtlen,
						&gtbnoa, &gtlena,
						0 /* search right */);
		} else {
			ASSERT(gtlena >= args->minlen);

			/*
			 * Right side is good, look for a left side entry.
			 */
			args->len = XFS_EXTLEN_MIN(gtlena, args->maxlen);
			xfs_alloc_fix_len(args);
			gtdiff = xfs_alloc_compute_diff(args->agbno, args->len,
				args->alignment, args->datatype, gtbnoa,
				gtlena, &gtnew);

			error = xfs_alloc_find_best_extent(args,
						&bno_cur_gt, &bno_cur_lt,
						gtdiff, &ltbno, &ltlen,
						&ltbnoa, &ltlena,
						1 /* search left */);
		}

		if (error)
			goto error0;
	}

	/*
	 * If we couldn't get anything, give up.
	 */
	if (bno_cur_lt == NULL && bno_cur_gt == NULL) {
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);

		if (!forced++) {
			trace_xfs_alloc_near_busy(args);
			xfs_log_force(args->mp, XFS_LOG_SYNC);
			goto restart;
		}
		trace_xfs_alloc_size_neither(args);
		args->agbno = NULLAGBLOCK;
		return 0;
	}

	/*
	 * At this point we have selected a freespace entry, either to the
	 * left or to the right.  If it's on the right, copy all the
	 * useful variables to the "left" set so we only have one
	 * copy of this code.
	 */
	if (bno_cur_gt) {
		bno_cur_lt = bno_cur_gt;
		bno_cur_gt = NULL;
		ltbno = gtbno;
		ltbnoa = gtbnoa;
		ltlen = gtlen;
		ltlena = gtlena;
		j = 1;
	} else
		j = 0;

	/*
	 * Fix up the length and compute the useful address.
	 */
	args->len = XFS_EXTLEN_MIN(ltlena, args->maxlen);
	xfs_alloc_fix_len(args);
	if (!xfs_alloc_fix_minleft(args)) {
		trace_xfs_alloc_near_nominleft(args);
		xfs_btree_del_cursor(bno_cur_lt, XFS_BTREE_NOERROR);
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
		return 0;
	}
	rlen = args->len;
	(void)xfs_alloc_compute_diff(args->agbno, rlen, args->alignment,
				     args->datatype, ltbnoa, ltlena, &ltnew);
	ASSERT(ltnew >= ltbno);
	ASSERT(ltnew + rlen <= ltbnoa + ltlena);
	ASSERT(ltnew + rlen <= be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length));
	ASSERT(ltnew >= args->min_agbno && ltnew <= args->max_agbno);
	args->agbno = ltnew;

	if ((error = xfs_alloc_fixup_trees(cnt_cur, bno_cur_lt, ltbno, ltlen,
			ltnew, rlen, XFSA_FIXUP_BNO_OK)))
		goto error0;

	if (j)
		trace_xfs_alloc_near_greater(args);
	else
		trace_xfs_alloc_near_lesser(args);

	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	xfs_btree_del_cursor(bno_cur_lt, XFS_BTREE_NOERROR);
	return 0;

 error0:
	trace_xfs_alloc_near_error(args);
	if (cnt_cur != NULL)
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_ERROR);
	if (bno_cur_lt != NULL)
		xfs_btree_del_cursor(bno_cur_lt, XFS_BTREE_ERROR);
	if (bno_cur_gt != NULL)
		xfs_btree_del_cursor(bno_cur_gt, XFS_BTREE_ERROR);
	return error;
}

/*
 * Allocate a variable extent anywhere in the allocation group agno.
 * Extent's length (returned in len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int				/* error */
xfs_alloc_ag_vextent_size(
	xfs_alloc_arg_t	*args)		/* allocation argument structure */
{
	xfs_btree_cur_t	*bno_cur;	/* cursor for bno btree */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for cnt btree */
	int		error;		/* error result */
	xfs_agblock_t	fbno;		/* start of found freespace */
	xfs_extlen_t	flen;		/* length of found freespace */
	int		i;		/* temp status variable */
	xfs_agblock_t	rbno;		/* returned block number */
	xfs_extlen_t	rlen;		/* length of returned extent */
	int		forced = 0;

restart:
	/*
	 * Allocate and initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT);
	bno_cur = NULL;

	/*
	 * Look for an entry >= maxlen+alignment-1 blocks.
	 */
	if ((error = xfs_alloc_lookup_ge(cnt_cur, 0,
			args->maxlen + args->alignment - 1, &i)))
		goto error0;

	/*
	 * If none or we have busy extents that we cannot allocate from, then
	 * we have to settle for a smaller extent. In the case that there are
	 * no large extents, this will return the last entry in the tree unless
	 * the tree is empty. In the case that there are only busy large
	 * extents, this will return the largest small extent unless there
	 * are no smaller extents available.
	 */
	if (!i || forced > 1) {
		error = xfs_alloc_ag_vextent_small(args, cnt_cur,
						   &fbno, &flen, &i);
		if (error)
			goto error0;
		if (i == 0 || flen == 0) {
			xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
			trace_xfs_alloc_size_noentry(args);
			return 0;
		}
		ASSERT(i == 1);
		xfs_alloc_compute_aligned(args, fbno, flen, &rbno, &rlen);
	} else {
		/*
		 * Search for a non-busy extent that is large enough.
		 * If we are at low space, don't check, or if we fall of
		 * the end of the btree, turn off the busy check and
		 * restart.
		 */
		for (;;) {
			error = xfs_alloc_get_rec(cnt_cur, &fbno, &flen, &i);
			if (error)
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);

			xfs_alloc_compute_aligned(args, fbno, flen,
						  &rbno, &rlen);

			if (rlen >= args->maxlen)
				break;

			error = xfs_btree_increment(cnt_cur, 0, &i);
			if (error)
				goto error0;
			if (i == 0) {
				/*
				 * Our only valid extents must have been busy.
				 * Make it unbusy by forcing the log out and
				 * retrying. If we've been here before, forcing
				 * the log isn't making the extents available,
				 * which means they have probably been freed in
				 * this transaction.  In that case, we have to
				 * give up on them and we'll attempt a minlen
				 * allocation the next time around.
				 */
				xfs_btree_del_cursor(cnt_cur,
						     XFS_BTREE_NOERROR);
				trace_xfs_alloc_size_busy(args);
				if (!forced++)
					xfs_log_force(args->mp, XFS_LOG_SYNC);
				goto restart;
			}
		}
	}

	/*
	 * In the first case above, we got the last entry in the
	 * by-size btree.  Now we check to see if the space hits maxlen
	 * once aligned; if not, we search left for something better.
	 * This can't happen in the second case above.
	 */
	rlen = XFS_EXTLEN_MIN(args->maxlen, rlen);
	XFS_WANT_CORRUPTED_GOTO(args->mp, rlen == 0 ||
			(rlen <= flen && rbno + rlen <= fbno + flen), error0);
	if (rlen < args->maxlen) {
		xfs_agblock_t	bestfbno;
		xfs_extlen_t	bestflen;
		xfs_agblock_t	bestrbno;
		xfs_extlen_t	bestrlen;

		bestrlen = rlen;
		bestrbno = rbno;
		bestflen = flen;
		bestfbno = fbno;
		for (;;) {
			if ((error = xfs_btree_decrement(cnt_cur, 0, &i)))
				goto error0;
			if (i == 0)
				break;
			if ((error = xfs_alloc_get_rec(cnt_cur, &fbno, &flen,
					&i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
			if (flen < bestrlen)
				break;
			xfs_alloc_compute_aligned(args, fbno, flen,
						  &rbno, &rlen);
			rlen = XFS_EXTLEN_MIN(args->maxlen, rlen);
			XFS_WANT_CORRUPTED_GOTO(args->mp, rlen == 0 ||
				(rlen <= flen && rbno + rlen <= fbno + flen),
				error0);
			if (rlen > bestrlen) {
				bestrlen = rlen;
				bestrbno = rbno;
				bestflen = flen;
				bestfbno = fbno;
				if (rlen == args->maxlen)
					break;
			}
		}
		if ((error = xfs_alloc_lookup_eq(cnt_cur, bestfbno, bestflen,
				&i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
		rlen = bestrlen;
		rbno = bestrbno;
		flen = bestflen;
		fbno = bestfbno;
	}
	args->wasfromfl = 0;
	/*
	 * Fix up the length.
	 */
	args->len = rlen;
	if (rlen < args->minlen) {
		if (!forced++) {
			xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
			trace_xfs_alloc_size_busy(args);
			xfs_log_force(args->mp, XFS_LOG_SYNC);
			goto restart;
		}
		goto out_nominleft;
	}
	xfs_alloc_fix_len(args);

	if (!xfs_alloc_fix_minleft(args))
		goto out_nominleft;
	rlen = args->len;
	XFS_WANT_CORRUPTED_GOTO(args->mp, rlen <= flen, error0);
	/*
	 * Allocate and initialize a cursor for the by-block tree.
	 */
	bno_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_BNO);
	if ((error = xfs_alloc_fixup_trees(cnt_cur, bno_cur, fbno, flen,
			rbno, rlen, XFSA_FIXUP_CNT_OK)))
		goto error0;
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	cnt_cur = bno_cur = NULL;
	args->len = rlen;
	args->agbno = rbno;
	XFS_WANT_CORRUPTED_GOTO(args->mp,
		args->agbno + args->len <=
			be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length),
		error0);
	trace_xfs_alloc_size_done(args);
	return 0;

error0:
	trace_xfs_alloc_size_error(args);
	if (cnt_cur)
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_ERROR);
	if (bno_cur)
		xfs_btree_del_cursor(bno_cur, XFS_BTREE_ERROR);
	return error;

out_nominleft:
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	trace_xfs_alloc_size_nominleft(args);
	args->agbno = NULLAGBLOCK;
	return 0;
}

/*
 * Deal with the case where only small freespaces remain.
 * Either return the contents of the last freespace record,
 * or allocate space from the freelist if there is nothing in the tree.
 */
STATIC int			/* error */
xfs_alloc_ag_vextent_small(
	xfs_alloc_arg_t	*args,	/* allocation argument structure */
	xfs_btree_cur_t	*ccur,	/* by-size cursor */
	xfs_agblock_t	*fbnop,	/* result block number */
	xfs_extlen_t	*flenp,	/* result length */
	int		*stat)	/* status: 0-freelist, 1-normal/none */
{
	struct xfs_owner_info	oinfo;
	struct xfs_perag	*pag;
	int		error;
	xfs_agblock_t	fbno;
	xfs_extlen_t	flen;
	int		i;

	if ((error = xfs_btree_decrement(ccur, 0, &i)))
		goto error0;
	if (i) {
		if ((error = xfs_alloc_get_rec(ccur, &fbno, &flen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(args->mp, i == 1, error0);
	}
	/*
	 * Nothing in the btree, try the freelist.  Make sure
	 * to respect minleft even when pulling from the
	 * freelist.
	 */
	else if (args->minlen == 1 && args->alignment == 1 &&
		 args->resv != XFS_AG_RESV_AGFL &&
		 (be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_flcount)
		  > args->minleft)) {
		error = xfs_alloc_get_freelist(args->tp, args->agbp, &fbno, 0);
		if (error)
			goto error0;
		if (fbno != NULLAGBLOCK) {
			xfs_extent_busy_reuse(args->mp, args->agno, fbno, 1,
			      xfs_alloc_allow_busy_reuse(args->datatype));

			if (xfs_alloc_is_userdata(args->datatype)) {
				xfs_buf_t	*bp;

				bp = xfs_btree_get_bufs(args->mp, args->tp,
					args->agno, fbno, 0);
				xfs_trans_binval(args->tp, bp);
			}
			args->len = 1;
			args->agbno = fbno;
			XFS_WANT_CORRUPTED_GOTO(args->mp,
				args->agbno + args->len <=
				be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length),
				error0);
			args->wasfromfl = 1;
			trace_xfs_alloc_small_freelist(args);

			/*
			 * If we're feeding an AGFL block to something that
			 * doesn't live in the free space, we need to clear
			 * out the OWN_AG rmap and add the block back to
			 * the AGFL per-AG reservation.
			 */
			xfs_rmap_ag_owner(&oinfo, XFS_RMAP_OWN_AG);
			error = xfs_rmap_free(args->tp, args->agbp, args->agno,
					fbno, 1, &oinfo);
			if (error)
				goto error0;
			pag = xfs_perag_get(args->mp, args->agno);
			xfs_ag_resv_free_extent(pag, XFS_AG_RESV_AGFL,
					args->tp, 1);
			xfs_perag_put(pag);

			*stat = 0;
			return 0;
		}
		/*
		 * Nothing in the freelist.
		 */
		else
			flen = 0;
	}
	/*
	 * Can't allocate from the freelist for some reason.
	 */
	else {
		fbno = NULLAGBLOCK;
		flen = 0;
	}
	/*
	 * Can't do the allocation, give up.
	 */
	if (flen < args->minlen) {
		args->agbno = NULLAGBLOCK;
		trace_xfs_alloc_small_notenough(args);
		flen = 0;
	}
	*fbnop = fbno;
	*flenp = flen;
	*stat = 1;
	trace_xfs_alloc_small_done(args);
	return 0;

error0:
	trace_xfs_alloc_small_error(args);
	return error;
}

/*
 * Free the extent starting at agno/bno for length.
 */
STATIC int
xfs_free_ag_extent(
	xfs_trans_t		*tp,
	xfs_buf_t		*agbp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	struct xfs_owner_info	*oinfo,
	enum xfs_ag_resv_type	type)
{
	xfs_btree_cur_t	*bno_cur;	/* cursor for by-block btree */
	xfs_btree_cur_t	*cnt_cur;	/* cursor for by-size btree */
	int		error;		/* error return value */
	xfs_agblock_t	gtbno;		/* start of right neighbor block */
	xfs_extlen_t	gtlen;		/* length of right neighbor block */
	int		haveleft;	/* have a left neighbor block */
	int		haveright;	/* have a right neighbor block */
	int		i;		/* temp, result code */
	xfs_agblock_t	ltbno;		/* start of left neighbor block */
	xfs_extlen_t	ltlen;		/* length of left neighbor block */
	xfs_mount_t	*mp;		/* mount point struct for filesystem */
	xfs_agblock_t	nbno;		/* new starting block of freespace */
	xfs_extlen_t	nlen;		/* new length of freespace */
	xfs_perag_t	*pag;		/* per allocation group data */

	bno_cur = cnt_cur = NULL;
	mp = tp->t_mountp;

	if (oinfo->oi_owner != XFS_RMAP_OWN_UNKNOWN) {
		error = xfs_rmap_free(tp, agbp, agno, bno, len, oinfo);
		if (error)
			goto error0;
	}

	/*
	 * Allocate and initialize a cursor for the by-block btree.
	 */
	bno_cur = xfs_allocbt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_BNO);
	/*
	 * Look for a neighboring block on the left (lower block numbers)
	 * that is contiguous with this space.
	 */
	if ((error = xfs_alloc_lookup_le(bno_cur, bno, len, &haveleft)))
		goto error0;
	if (haveleft) {
		/*
		 * There is a block to our left.
		 */
		if ((error = xfs_alloc_get_rec(bno_cur, &ltbno, &ltlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * It's not contiguous, though.
		 */
		if (ltbno + ltlen < bno)
			haveleft = 0;
		else {
			/*
			 * If this failure happens the request to free this
			 * space was invalid, it's (partly) already free.
			 * Very bad.
			 */
			XFS_WANT_CORRUPTED_GOTO(mp,
						ltbno + ltlen <= bno, error0);
		}
	}
	/*
	 * Look for a neighboring block on the right (higher block numbers)
	 * that is contiguous with this space.
	 */
	if ((error = xfs_btree_increment(bno_cur, 0, &haveright)))
		goto error0;
	if (haveright) {
		/*
		 * There is a block to our right.
		 */
		if ((error = xfs_alloc_get_rec(bno_cur, &gtbno, &gtlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * It's not contiguous, though.
		 */
		if (bno + len < gtbno)
			haveright = 0;
		else {
			/*
			 * If this failure happens the request to free this
			 * space was invalid, it's (partly) already free.
			 * Very bad.
			 */
			XFS_WANT_CORRUPTED_GOTO(mp, gtbno >= bno + len, error0);
		}
	}
	/*
	 * Now allocate and initialize a cursor for the by-size tree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_CNT);
	/*
	 * Have both left and right contiguous neighbors.
	 * Merge all three into a single free block.
	 */
	if (haveleft && haveright) {
		/*
		 * Delete the old by-size entry on the left.
		 */
		if ((error = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * Delete the old by-size entry on the right.
		 */
		if ((error = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * Delete the old by-block entry for the right block.
		 */
		if ((error = xfs_btree_delete(bno_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * Move the by-block cursor back to the left neighbor.
		 */
		if ((error = xfs_btree_decrement(bno_cur, 0, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
#ifdef DEBUG
		/*
		 * Check that this is the right record: delete didn't
		 * mangle the cursor.
		 */
		{
			xfs_agblock_t	xxbno;
			xfs_extlen_t	xxlen;

			if ((error = xfs_alloc_get_rec(bno_cur, &xxbno, &xxlen,
					&i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(mp,
				i == 1 && xxbno == ltbno && xxlen == ltlen,
				error0);
		}
#endif
		/*
		 * Update remaining by-block entry to the new, joined block.
		 */
		nbno = ltbno;
		nlen = len + ltlen + gtlen;
		if ((error = xfs_alloc_update(bno_cur, nbno, nlen)))
			goto error0;
	}
	/*
	 * Have only a left contiguous neighbor.
	 * Merge it together with the new freespace.
	 */
	else if (haveleft) {
		/*
		 * Delete the old by-size entry on the left.
		 */
		if ((error = xfs_alloc_lookup_eq(cnt_cur, ltbno, ltlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * Back up the by-block cursor to the left neighbor, and
		 * update its length.
		 */
		if ((error = xfs_btree_decrement(bno_cur, 0, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		nbno = ltbno;
		nlen = len + ltlen;
		if ((error = xfs_alloc_update(bno_cur, nbno, nlen)))
			goto error0;
	}
	/*
	 * Have only a right contiguous neighbor.
	 * Merge it together with the new freespace.
	 */
	else if (haveright) {
		/*
		 * Delete the old by-size entry on the right.
		 */
		if ((error = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		/*
		 * Update the starting block and length of the right
		 * neighbor in the by-block tree.
		 */
		nbno = bno;
		nlen = len + gtlen;
		if ((error = xfs_alloc_update(bno_cur, nbno, nlen)))
			goto error0;
	}
	/*
	 * No contiguous neighbors.
	 * Insert the new freespace into the by-block tree.
	 */
	else {
		nbno = bno;
		nlen = len;
		if ((error = xfs_btree_insert(bno_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
	}
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	bno_cur = NULL;
	/*
	 * In all cases we need to insert the new freespace in the by-size tree.
	 */
	if ((error = xfs_alloc_lookup_eq(cnt_cur, nbno, nlen, &i)))
		goto error0;
	XFS_WANT_CORRUPTED_GOTO(mp, i == 0, error0);
	if ((error = xfs_btree_insert(cnt_cur, &i)))
		goto error0;
	XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	cnt_cur = NULL;

	/*
	 * Update the freespace totals in the ag and superblock.
	 */
	pag = xfs_perag_get(mp, agno);
	error = xfs_alloc_update_counters(tp, pag, agbp, len);
	xfs_ag_resv_free_extent(pag, type, tp, len);
	xfs_perag_put(pag);
	if (error)
		goto error0;

	XFS_STATS_INC(mp, xs_freex);
	XFS_STATS_ADD(mp, xs_freeb, len);

	trace_xfs_free_extent(mp, agno, bno, len, type == XFS_AG_RESV_AGFL,
			haveleft, haveright);

	return 0;

 error0:
	trace_xfs_free_extent(mp, agno, bno, len, type == XFS_AG_RESV_AGFL,
			-1, -1);
	if (bno_cur)
		xfs_btree_del_cursor(bno_cur, XFS_BTREE_ERROR);
	if (cnt_cur)
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Visible (exported) allocation/free functions.
 * Some of these are used just by xfs_alloc_btree.c and this file.
 */

/*
 * Compute and fill in value of m_ag_maxlevels.
 */
void
xfs_alloc_compute_maxlevels(
	xfs_mount_t	*mp)	/* file system mount structure */
{
	mp->m_ag_maxlevels = xfs_btree_compute_maxlevels(mp, mp->m_alloc_mnr,
			(mp->m_sb.sb_agblocks + 1) / 2);
}

/*
 * Find the length of the longest extent in an AG.  The 'need' parameter
 * specifies how much space we're going to need for the AGFL and the
 * 'reserved' parameter tells us how many blocks in this AG are reserved for
 * other callers.
 */
xfs_extlen_t
xfs_alloc_longest_free_extent(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	xfs_extlen_t		need,
	xfs_extlen_t		reserved)
{
	xfs_extlen_t		delta = 0;

	/*
	 * If the AGFL needs a recharge, we'll have to subtract that from the
	 * longest extent.
	 */
	if (need > pag->pagf_flcount)
		delta = need - pag->pagf_flcount;

	/*
	 * If we cannot maintain others' reservations with space from the
	 * not-longest freesp extents, we'll have to subtract /that/ from
	 * the longest extent too.
	 */
	if (pag->pagf_freeblks - pag->pagf_longest < reserved)
		delta += reserved - (pag->pagf_freeblks - pag->pagf_longest);

	/*
	 * If the longest extent is long enough to satisfy all the
	 * reservations and AGFL rules in place, we can return this extent.
	 */
	if (pag->pagf_longest > delta)
		return pag->pagf_longest - delta;

	/* Otherwise, let the caller try for 1 block if there's space. */
	return pag->pagf_flcount > 0 || pag->pagf_longest > 0;
}

unsigned int
xfs_alloc_min_freelist(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag)
{
	unsigned int		min_free;

	/* space needed by-bno freespace btree */
	min_free = min_t(unsigned int, pag->pagf_levels[XFS_BTNUM_BNOi] + 1,
				       mp->m_ag_maxlevels);
	/* space needed by-size freespace btree */
	min_free += min_t(unsigned int, pag->pagf_levels[XFS_BTNUM_CNTi] + 1,
				       mp->m_ag_maxlevels);
	/* space needed reverse mapping used space btree */
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		min_free += min_t(unsigned int,
				  pag->pagf_levels[XFS_BTNUM_RMAPi] + 1,
				  mp->m_rmap_maxlevels);

	return min_free;
}

/*
 * Check if the operation we are fixing up the freelist for should go ahead or
 * not. If we are freeing blocks, we always allow it, otherwise the allocation
 * is dependent on whether the size and shape of free space available will
 * permit the requested allocation to take place.
 */
static bool
xfs_alloc_space_available(
	struct xfs_alloc_arg	*args,
	xfs_extlen_t		min_free,
	int			flags)
{
	struct xfs_perag	*pag = args->pag;
	xfs_extlen_t		longest;
	xfs_extlen_t		reservation; /* blocks that are still reserved */
	int			available;

	if (flags & XFS_ALLOC_FLAG_FREEING)
		return true;

	reservation = xfs_ag_resv_needed(pag, args->resv);

	/* do we have enough contiguous free space for the allocation? */
	longest = xfs_alloc_longest_free_extent(args->mp, pag, min_free,
			reservation);
	if ((args->minlen + args->alignment + args->minalignslop - 1) > longest)
		return false;

	/* do we have enough free space remaining for the allocation? */
	available = (int)(pag->pagf_freeblks + pag->pagf_flcount -
			  reservation - min_free - args->total);
	if (available < (int)args->minleft || available <= 0)
		return false;

	return true;
}

/*
 * Decide whether to use this allocation group for this allocation.
 * If so, fix up the btree freelist's size.
 */
int			/* error */
xfs_alloc_fix_freelist(
	struct xfs_alloc_arg	*args,	/* allocation argument structure */
	int			flags)	/* XFS_ALLOC_FLAG_... */
{
	struct xfs_mount	*mp = args->mp;
	struct xfs_perag	*pag = args->pag;
	struct xfs_trans	*tp = args->tp;
	struct xfs_buf		*agbp = NULL;
	struct xfs_buf		*agflbp = NULL;
	struct xfs_alloc_arg	targs;	/* local allocation arguments */
	xfs_agblock_t		bno;	/* freelist block */
	xfs_extlen_t		need;	/* total blocks needed in freelist */
	int			error = 0;

	if (!pag->pagf_init) {
		error = xfs_alloc_read_agf(mp, tp, args->agno, flags, &agbp);
		if (error)
			goto out_no_agbp;
		if (!pag->pagf_init) {
			ASSERT(flags & XFS_ALLOC_FLAG_TRYLOCK);
			ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
			goto out_agbp_relse;
		}
	}

	/*
	 * If this is a metadata preferred pag and we are user data then try
	 * somewhere else if we are not being asked to try harder at this
	 * point
	 */
	if (pag->pagf_metadata && xfs_alloc_is_userdata(args->datatype) &&
	    (flags & XFS_ALLOC_FLAG_TRYLOCK)) {
		ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
		goto out_agbp_relse;
	}

	need = xfs_alloc_min_freelist(mp, pag);
	if (!xfs_alloc_space_available(args, need, flags))
		goto out_agbp_relse;

	/*
	 * Get the a.g. freespace buffer.
	 * Can fail if we're not blocking on locks, and it's held.
	 */
	if (!agbp) {
		error = xfs_alloc_read_agf(mp, tp, args->agno, flags, &agbp);
		if (error)
			goto out_no_agbp;
		if (!agbp) {
			ASSERT(flags & XFS_ALLOC_FLAG_TRYLOCK);
			ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
			goto out_no_agbp;
		}
	}

	/* If there isn't enough total space or single-extent, reject it. */
	need = xfs_alloc_min_freelist(mp, pag);
	if (!xfs_alloc_space_available(args, need, flags))
		goto out_agbp_relse;

	/*
	 * Make the freelist shorter if it's too long.
	 *
	 * Note that from this point onwards, we will always release the agf and
	 * agfl buffers on error. This handles the case where we error out and
	 * the buffers are clean or may not have been joined to the transaction
	 * and hence need to be released manually. If they have been joined to
	 * the transaction, then xfs_trans_brelse() will handle them
	 * appropriately based on the recursion count and dirty state of the
	 * buffer.
	 *
	 * XXX (dgc): When we have lots of free space, does this buy us
	 * anything other than extra overhead when we need to put more blocks
	 * back on the free list? Maybe we should only do this when space is
	 * getting low or the AGFL is more than half full?
	 *
	 * The NOSHRINK flag prevents the AGFL from being shrunk if it's too
	 * big; the NORMAP flag prevents AGFL expand/shrink operations from
	 * updating the rmapbt.  Both flags are used in xfs_repair while we're
	 * rebuilding the rmapbt, and neither are used by the kernel.  They're
	 * both required to ensure that rmaps are correctly recorded for the
	 * regenerated AGFL, bnobt, and cntbt.  See repair/phase5.c and
	 * repair/rmap.c in xfsprogs for details.
	 */
	memset(&targs, 0, sizeof(targs));
	if (flags & XFS_ALLOC_FLAG_NORMAP)
		xfs_rmap_skip_owner_update(&targs.oinfo);
	else
		xfs_rmap_ag_owner(&targs.oinfo, XFS_RMAP_OWN_AG);
	while (!(flags & XFS_ALLOC_FLAG_NOSHRINK) && pag->pagf_flcount > need) {
		struct xfs_buf	*bp;

		error = xfs_alloc_get_freelist(tp, agbp, &bno, 0);
		if (error)
			goto out_agbp_relse;
		error = xfs_free_ag_extent(tp, agbp, args->agno, bno, 1,
					   &targs.oinfo, XFS_AG_RESV_AGFL);
		if (error)
			goto out_agbp_relse;
		bp = xfs_btree_get_bufs(mp, tp, args->agno, bno, 0);
		xfs_trans_binval(tp, bp);
	}

	targs.tp = tp;
	targs.mp = mp;
	targs.agbp = agbp;
	targs.agno = args->agno;
	targs.alignment = targs.minlen = targs.prod = 1;
	targs.type = XFS_ALLOCTYPE_THIS_AG;
	targs.pag = pag;
	error = xfs_alloc_read_agfl(mp, tp, targs.agno, &agflbp);
	if (error)
		goto out_agbp_relse;

	/* Make the freelist longer if it's too short. */
	while (pag->pagf_flcount < need) {
		targs.agbno = 0;
		targs.maxlen = need - pag->pagf_flcount;
		targs.resv = XFS_AG_RESV_AGFL;

		/* Allocate as many blocks as possible at once. */
		error = xfs_alloc_ag_vextent(&targs);
		if (error)
			goto out_agflbp_relse;

		/*
		 * Stop if we run out.  Won't happen if callers are obeying
		 * the restrictions correctly.  Can happen for free calls
		 * on a completely full ag.
		 */
		if (targs.agbno == NULLAGBLOCK) {
			if (flags & XFS_ALLOC_FLAG_FREEING)
				break;
			goto out_agflbp_relse;
		}
		/*
		 * Put each allocated block on the list.
		 */
		for (bno = targs.agbno; bno < targs.agbno + targs.len; bno++) {
			error = xfs_alloc_put_freelist(tp, agbp,
							agflbp, bno, 0);
			if (error)
				goto out_agflbp_relse;
		}
	}
	xfs_trans_brelse(tp, agflbp);
	args->agbp = agbp;
	return 0;

out_agflbp_relse:
	xfs_trans_brelse(tp, agflbp);
out_agbp_relse:
	if (agbp)
		xfs_trans_brelse(tp, agbp);
out_no_agbp:
	args->agbp = NULL;
	return error;
}

/*
 * Get a block from the freelist.
 * Returns with the buffer for the block gotten.
 */
int				/* error */
xfs_alloc_get_freelist(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_buf_t	*agbp,	/* buffer containing the agf structure */
	xfs_agblock_t	*bnop,	/* block address retrieved from freelist */
	int		btreeblk) /* destination is a AGF btree */
{
	xfs_agf_t	*agf;	/* a.g. freespace structure */
	xfs_buf_t	*agflbp;/* buffer for a.g. freelist structure */
	xfs_agblock_t	bno;	/* block number returned */
	__be32		*agfl_bno;
	int		error;
	int		logflags;
	xfs_mount_t	*mp = tp->t_mountp;
	xfs_perag_t	*pag;	/* per allocation group data */

	/*
	 * Freelist is empty, give up.
	 */
	agf = XFS_BUF_TO_AGF(agbp);
	if (!agf->agf_flcount) {
		*bnop = NULLAGBLOCK;
		return 0;
	}
	/*
	 * Read the array of free blocks.
	 */
	error = xfs_alloc_read_agfl(mp, tp, be32_to_cpu(agf->agf_seqno),
				    &agflbp);
	if (error)
		return error;


	/*
	 * Get the block number and update the data structures.
	 */
	agfl_bno = XFS_BUF_TO_AGFL_BNO(mp, agflbp);
	bno = be32_to_cpu(agfl_bno[be32_to_cpu(agf->agf_flfirst)]);
	be32_add_cpu(&agf->agf_flfirst, 1);
	xfs_trans_brelse(tp, agflbp);
	if (be32_to_cpu(agf->agf_flfirst) == XFS_AGFL_SIZE(mp))
		agf->agf_flfirst = 0;

	pag = xfs_perag_get(mp, be32_to_cpu(agf->agf_seqno));
	be32_add_cpu(&agf->agf_flcount, -1);
	xfs_trans_agflist_delta(tp, -1);
	pag->pagf_flcount--;
	xfs_perag_put(pag);

	logflags = XFS_AGF_FLFIRST | XFS_AGF_FLCOUNT;
	if (btreeblk) {
		be32_add_cpu(&agf->agf_btreeblks, 1);
		pag->pagf_btreeblks++;
		logflags |= XFS_AGF_BTREEBLKS;
	}

	xfs_alloc_log_agf(tp, agbp, logflags);
	*bnop = bno;

	return 0;
}

/*
 * Log the given fields from the agf structure.
 */
void
xfs_alloc_log_agf(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_buf_t	*bp,	/* buffer for a.g. freelist header */
	int		fields)	/* mask of fields to be logged (XFS_AGF_...) */
{
	int	first;		/* first byte offset */
	int	last;		/* last byte offset */
	static const short	offsets[] = {
		offsetof(xfs_agf_t, agf_magicnum),
		offsetof(xfs_agf_t, agf_versionnum),
		offsetof(xfs_agf_t, agf_seqno),
		offsetof(xfs_agf_t, agf_length),
		offsetof(xfs_agf_t, agf_roots[0]),
		offsetof(xfs_agf_t, agf_levels[0]),
		offsetof(xfs_agf_t, agf_flfirst),
		offsetof(xfs_agf_t, agf_fllast),
		offsetof(xfs_agf_t, agf_flcount),
		offsetof(xfs_agf_t, agf_freeblks),
		offsetof(xfs_agf_t, agf_longest),
		offsetof(xfs_agf_t, agf_btreeblks),
		offsetof(xfs_agf_t, agf_uuid),
		offsetof(xfs_agf_t, agf_rmap_blocks),
		/* needed so that we don't log the whole rest of the structure: */
		offsetof(xfs_agf_t, agf_spare64),
		sizeof(xfs_agf_t)
	};

	trace_xfs_agf(tp->t_mountp, XFS_BUF_TO_AGF(bp), fields, _RET_IP_);

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_AGF_BUF);

	xfs_btree_offsets(fields, offsets, XFS_AGF_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, (uint)first, (uint)last);
}

/*
 * Interface for inode allocation to force the pag data to be initialized.
 */
int					/* error */
xfs_alloc_pagf_init(
	xfs_mount_t		*mp,	/* file system mount structure */
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	int			flags)	/* XFS_ALLOC_FLAGS_... */
{
	xfs_buf_t		*bp;
	int			error;

	if ((error = xfs_alloc_read_agf(mp, tp, agno, flags, &bp)))
		return error;
	if (bp)
		xfs_trans_brelse(tp, bp);
	return 0;
}

/*
 * Put the block on the freelist for the allocation group.
 */
int					/* error */
xfs_alloc_put_freelist(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_buf_t		*agbp,	/* buffer for a.g. freelist header */
	xfs_buf_t		*agflbp,/* buffer for a.g. free block array */
	xfs_agblock_t		bno,	/* block being freed */
	int			btreeblk) /* block came from a AGF btree */
{
	xfs_agf_t		*agf;	/* a.g. freespace structure */
	__be32			*blockp;/* pointer to array entry */
	int			error;
	int			logflags;
	xfs_mount_t		*mp;	/* mount structure */
	xfs_perag_t		*pag;	/* per allocation group data */
	__be32			*agfl_bno;
	int			startoff;

	agf = XFS_BUF_TO_AGF(agbp);
	mp = tp->t_mountp;

	if (!agflbp && (error = xfs_alloc_read_agfl(mp, tp,
			be32_to_cpu(agf->agf_seqno), &agflbp)))
		return error;
	be32_add_cpu(&agf->agf_fllast, 1);
	if (be32_to_cpu(agf->agf_fllast) == XFS_AGFL_SIZE(mp))
		agf->agf_fllast = 0;

	pag = xfs_perag_get(mp, be32_to_cpu(agf->agf_seqno));
	be32_add_cpu(&agf->agf_flcount, 1);
	xfs_trans_agflist_delta(tp, 1);
	pag->pagf_flcount++;

	logflags = XFS_AGF_FLLAST | XFS_AGF_FLCOUNT;
	if (btreeblk) {
		be32_add_cpu(&agf->agf_btreeblks, -1);
		pag->pagf_btreeblks--;
		logflags |= XFS_AGF_BTREEBLKS;
	}
	xfs_perag_put(pag);

	xfs_alloc_log_agf(tp, agbp, logflags);

	ASSERT(be32_to_cpu(agf->agf_flcount) <= XFS_AGFL_SIZE(mp));

	agfl_bno = XFS_BUF_TO_AGFL_BNO(mp, agflbp);
	blockp = &agfl_bno[be32_to_cpu(agf->agf_fllast)];
	*blockp = cpu_to_be32(bno);
	startoff = (char *)blockp - (char *)agflbp->b_addr;

	xfs_alloc_log_agf(tp, agbp, logflags);

	xfs_trans_buf_set_type(tp, agflbp, XFS_BLFT_AGFL_BUF);
	xfs_trans_log_buf(tp, agflbp, startoff,
			  startoff + sizeof(xfs_agblock_t) - 1);
	return 0;
}

static bool
xfs_agf_verify(
	struct xfs_mount *mp,
	struct xfs_buf	*bp)
 {
	struct xfs_agf	*agf = XFS_BUF_TO_AGF(bp);

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		if (!uuid_equal(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid))
			return false;
		if (!xfs_log_check_lsn(mp,
				be64_to_cpu(XFS_BUF_TO_AGF(bp)->agf_lsn)))
			return false;
	}

	if (!(agf->agf_magicnum == cpu_to_be32(XFS_AGF_MAGIC) &&
	      XFS_AGF_GOOD_VERSION(be32_to_cpu(agf->agf_versionnum)) &&
	      be32_to_cpu(agf->agf_freeblks) <= be32_to_cpu(agf->agf_length) &&
	      be32_to_cpu(agf->agf_flfirst) < XFS_AGFL_SIZE(mp) &&
	      be32_to_cpu(agf->agf_fllast) < XFS_AGFL_SIZE(mp) &&
	      be32_to_cpu(agf->agf_flcount) <= XFS_AGFL_SIZE(mp)))
		return false;

	if (be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]) > XFS_BTREE_MAXLEVELS ||
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]) > XFS_BTREE_MAXLEVELS)
		return false;

	if (xfs_sb_version_hasrmapbt(&mp->m_sb) &&
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_RMAP]) > XFS_BTREE_MAXLEVELS)
		return false;

	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agf->agf_seqno) != bp->b_pag->pag_agno)
		return false;

	if (xfs_sb_version_haslazysbcount(&mp->m_sb) &&
	    be32_to_cpu(agf->agf_btreeblks) > be32_to_cpu(agf->agf_length))
		return false;

	if (xfs_sb_version_hasreflink(&mp->m_sb) &&
	    be32_to_cpu(agf->agf_refcount_level) > XFS_BTREE_MAXLEVELS)
		return false;

	return true;;

}

static void
xfs_agf_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	if (xfs_sb_version_hascrc(&mp->m_sb) &&
	    !xfs_buf_verify_cksum(bp, XFS_AGF_CRC_OFF))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (XFS_TEST_ERROR(!xfs_agf_verify(mp, bp), mp,
				XFS_ERRTAG_ALLOC_READ_AGF,
				XFS_RANDOM_ALLOC_READ_AGF))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error)
		xfs_verifier_error(bp);
}

static void
xfs_agf_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	if (!xfs_agf_verify(mp, bp)) {
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (bip)
		XFS_BUF_TO_AGF(bp)->agf_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_AGF_CRC_OFF);
}

const struct xfs_buf_ops xfs_agf_buf_ops = {
	.name = "xfs_agf",
	.verify_read = xfs_agf_read_verify,
	.verify_write = xfs_agf_write_verify,
};

/*
 * Read in the allocation group header (free/alloc section).
 */
int					/* error */
xfs_read_agf(
	struct xfs_mount	*mp,	/* mount point structure */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	int			flags,	/* XFS_BUF_ */
	struct xfs_buf		**bpp)	/* buffer for the ag freelist header */
{
	int		error;

	trace_xfs_read_agf(mp, agno);

	ASSERT(agno != NULLAGNUMBER);
	error = xfs_trans_read_buf(
			mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), flags, bpp, &xfs_agf_buf_ops);
	if (error)
		return error;
	if (!*bpp)
		return 0;

	ASSERT(!(*bpp)->b_error);
	xfs_buf_set_ref(*bpp, XFS_AGF_REF);
	return 0;
}

/*
 * Read in the allocation group header (free/alloc section).
 */
int					/* error */
xfs_alloc_read_agf(
	struct xfs_mount	*mp,	/* mount point structure */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	int			flags,	/* XFS_ALLOC_FLAG_... */
	struct xfs_buf		**bpp)	/* buffer for the ag freelist header */
{
	struct xfs_agf		*agf;		/* ag freelist header */
	struct xfs_perag	*pag;		/* per allocation group data */
	int			error;

	trace_xfs_alloc_read_agf(mp, agno);

	ASSERT(agno != NULLAGNUMBER);
	error = xfs_read_agf(mp, tp, agno,
			(flags & XFS_ALLOC_FLAG_TRYLOCK) ? XBF_TRYLOCK : 0,
			bpp);
	if (error)
		return error;
	if (!*bpp)
		return 0;
	ASSERT(!(*bpp)->b_error);

	agf = XFS_BUF_TO_AGF(*bpp);
	pag = xfs_perag_get(mp, agno);
	if (!pag->pagf_init) {
		pag->pagf_freeblks = be32_to_cpu(agf->agf_freeblks);
		pag->pagf_btreeblks = be32_to_cpu(agf->agf_btreeblks);
		pag->pagf_flcount = be32_to_cpu(agf->agf_flcount);
		pag->pagf_longest = be32_to_cpu(agf->agf_longest);
		pag->pagf_levels[XFS_BTNUM_BNOi] =
			be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNOi]);
		pag->pagf_levels[XFS_BTNUM_CNTi] =
			be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNTi]);
		pag->pagf_levels[XFS_BTNUM_RMAPi] =
			be32_to_cpu(agf->agf_levels[XFS_BTNUM_RMAPi]);
		pag->pagf_refcount_level = be32_to_cpu(agf->agf_refcount_level);
		spin_lock_init(&pag->pagb_lock);
		pag->pagb_count = 0;
		pag->pagb_tree = RB_ROOT;
		pag->pagf_init = 1;
	}
#ifdef DEBUG
	else if (!XFS_FORCED_SHUTDOWN(mp)) {
		ASSERT(pag->pagf_freeblks == be32_to_cpu(agf->agf_freeblks));
		ASSERT(pag->pagf_btreeblks == be32_to_cpu(agf->agf_btreeblks));
		ASSERT(pag->pagf_flcount == be32_to_cpu(agf->agf_flcount));
		ASSERT(pag->pagf_longest == be32_to_cpu(agf->agf_longest));
		ASSERT(pag->pagf_levels[XFS_BTNUM_BNOi] ==
		       be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNOi]));
		ASSERT(pag->pagf_levels[XFS_BTNUM_CNTi] ==
		       be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNTi]));
	}
#endif
	xfs_perag_put(pag);
	return 0;
}

/*
 * Allocate an extent (variable-size).
 * Depending on the allocation type, we either look in a single allocation
 * group or loop over the allocation groups to find the result.
 */
int				/* error */
xfs_alloc_vextent(
	xfs_alloc_arg_t	*args)	/* allocation argument structure */
{
	xfs_agblock_t	agsize;	/* allocation group size */
	int		error;
	int		flags;	/* XFS_ALLOC_FLAG_... locking flags */
	xfs_extlen_t	minleft;/* minimum left value, temp copy */
	xfs_mount_t	*mp;	/* mount structure pointer */
	xfs_agnumber_t	sagno;	/* starting allocation group number */
	xfs_alloctype_t	type;	/* input allocation type */
	int		bump_rotor = 0;
	int		no_min = 0;
	xfs_agnumber_t	rotorstep = xfs_rotorstep; /* inode32 agf stepper */

	mp = args->mp;
	type = args->otype = args->type;
	args->agbno = NULLAGBLOCK;
	/*
	 * Just fix this up, for the case where the last a.g. is shorter
	 * (or there's only one a.g.) and the caller couldn't easily figure
	 * that out (xfs_bmap_alloc).
	 */
	agsize = mp->m_sb.sb_agblocks;
	if (args->maxlen > agsize)
		args->maxlen = agsize;
	if (args->alignment == 0)
		args->alignment = 1;
	ASSERT(XFS_FSB_TO_AGNO(mp, args->fsbno) < mp->m_sb.sb_agcount);
	ASSERT(XFS_FSB_TO_AGBNO(mp, args->fsbno) < agsize);
	ASSERT(args->minlen <= args->maxlen);
	ASSERT(args->minlen <= agsize);
	ASSERT(args->mod < args->prod);
	if (XFS_FSB_TO_AGNO(mp, args->fsbno) >= mp->m_sb.sb_agcount ||
	    XFS_FSB_TO_AGBNO(mp, args->fsbno) >= agsize ||
	    args->minlen > args->maxlen || args->minlen > agsize ||
	    args->mod >= args->prod) {
		args->fsbno = NULLFSBLOCK;
		trace_xfs_alloc_vextent_badargs(args);
		return 0;
	}
	minleft = args->minleft;

	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		/*
		 * These three force us into a single a.g.
		 */
		args->agno = XFS_FSB_TO_AGNO(mp, args->fsbno);
		args->pag = xfs_perag_get(mp, args->agno);
		args->minleft = 0;
		error = xfs_alloc_fix_freelist(args, 0);
		args->minleft = minleft;
		if (error) {
			trace_xfs_alloc_vextent_nofix(args);
			goto error0;
		}
		if (!args->agbp) {
			trace_xfs_alloc_vextent_noagbp(args);
			break;
		}
		args->agbno = XFS_FSB_TO_AGBNO(mp, args->fsbno);
		if ((error = xfs_alloc_ag_vextent(args)))
			goto error0;
		break;
	case XFS_ALLOCTYPE_START_BNO:
		/*
		 * Try near allocation first, then anywhere-in-ag after
		 * the first a.g. fails.
		 */
		if ((args->datatype & XFS_ALLOC_INITIAL_USER_DATA) &&
		    (mp->m_flags & XFS_MOUNT_32BITINODES)) {
			args->fsbno = XFS_AGB_TO_FSB(mp,
					((mp->m_agfrotor / rotorstep) %
					mp->m_sb.sb_agcount), 0);
			bump_rotor = 1;
		}
		args->agbno = XFS_FSB_TO_AGBNO(mp, args->fsbno);
		args->type = XFS_ALLOCTYPE_NEAR_BNO;
		/* FALLTHROUGH */
	case XFS_ALLOCTYPE_ANY_AG:
	case XFS_ALLOCTYPE_START_AG:
	case XFS_ALLOCTYPE_FIRST_AG:
		/*
		 * Rotate through the allocation groups looking for a winner.
		 */
		if (type == XFS_ALLOCTYPE_ANY_AG) {
			/*
			 * Start with the last place we left off.
			 */
			args->agno = sagno = (mp->m_agfrotor / rotorstep) %
					mp->m_sb.sb_agcount;
			args->type = XFS_ALLOCTYPE_THIS_AG;
			flags = XFS_ALLOC_FLAG_TRYLOCK;
		} else if (type == XFS_ALLOCTYPE_FIRST_AG) {
			/*
			 * Start with allocation group given by bno.
			 */
			args->agno = XFS_FSB_TO_AGNO(mp, args->fsbno);
			args->type = XFS_ALLOCTYPE_THIS_AG;
			sagno = 0;
			flags = 0;
		} else {
			if (type == XFS_ALLOCTYPE_START_AG)
				args->type = XFS_ALLOCTYPE_THIS_AG;
			/*
			 * Start with the given allocation group.
			 */
			args->agno = sagno = XFS_FSB_TO_AGNO(mp, args->fsbno);
			flags = XFS_ALLOC_FLAG_TRYLOCK;
		}
		/*
		 * Loop over allocation groups twice; first time with
		 * trylock set, second time without.
		 */
		for (;;) {
			args->pag = xfs_perag_get(mp, args->agno);
			if (no_min) args->minleft = 0;
			error = xfs_alloc_fix_freelist(args, flags);
			args->minleft = minleft;
			if (error) {
				trace_xfs_alloc_vextent_nofix(args);
				goto error0;
			}
			/*
			 * If we get a buffer back then the allocation will fly.
			 */
			if (args->agbp) {
				if ((error = xfs_alloc_ag_vextent(args)))
					goto error0;
				break;
			}

			trace_xfs_alloc_vextent_loopfailed(args);

			/*
			 * Didn't work, figure out the next iteration.
			 */
			if (args->agno == sagno &&
			    type == XFS_ALLOCTYPE_START_BNO)
				args->type = XFS_ALLOCTYPE_THIS_AG;
			/*
			* For the first allocation, we can try any AG to get
			* space.  However, if we already have allocated a
			* block, we don't want to try AGs whose number is below
			* sagno. Otherwise, we may end up with out-of-order
			* locking of AGF, which might cause deadlock.
			*/
			if (++(args->agno) == mp->m_sb.sb_agcount) {
				if (args->firstblock != NULLFSBLOCK)
					args->agno = sagno;
				else
					args->agno = 0;
			}
			/*
			 * Reached the starting a.g., must either be done
			 * or switch to non-trylock mode.
			 */
			if (args->agno == sagno) {
				if (no_min == 1) {
					args->agbno = NULLAGBLOCK;
					trace_xfs_alloc_vextent_allfailed(args);
					break;
				}
				if (flags == 0) {
					no_min = 1;
				} else {
					flags = 0;
					if (type == XFS_ALLOCTYPE_START_BNO) {
						args->agbno = XFS_FSB_TO_AGBNO(mp,
							args->fsbno);
						args->type = XFS_ALLOCTYPE_NEAR_BNO;
					}
				}
			}
			xfs_perag_put(args->pag);
		}
		if (bump_rotor || (type == XFS_ALLOCTYPE_ANY_AG)) {
			if (args->agno == sagno)
				mp->m_agfrotor = (mp->m_agfrotor + 1) %
					(mp->m_sb.sb_agcount * rotorstep);
			else
				mp->m_agfrotor = (args->agno * rotorstep + 1) %
					(mp->m_sb.sb_agcount * rotorstep);
		}
		break;
	default:
		ASSERT(0);
		/* NOTREACHED */
	}
	if (args->agbno == NULLAGBLOCK)
		args->fsbno = NULLFSBLOCK;
	else {
		args->fsbno = XFS_AGB_TO_FSB(mp, args->agno, args->agbno);
#ifdef DEBUG
		ASSERT(args->len >= args->minlen);
		ASSERT(args->len <= args->maxlen);
		ASSERT(args->agbno % args->alignment == 0);
		XFS_AG_CHECK_DADDR(mp, XFS_FSB_TO_DADDR(mp, args->fsbno),
			args->len);
#endif

		/* Zero the extent if we were asked to do so */
		if (args->datatype & XFS_ALLOC_USERDATA_ZERO) {
			error = xfs_zero_extent(args->ip, args->fsbno, args->len);
			if (error)
				goto error0;
		}

	}
	xfs_perag_put(args->pag);
	return 0;
error0:
	xfs_perag_put(args->pag);
	return error;
}

/* Ensure that the freelist is at full capacity. */
int
xfs_free_extent_fix_freelist(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	struct xfs_buf		**agbp)
{
	struct xfs_alloc_arg	args;
	int			error;

	memset(&args, 0, sizeof(struct xfs_alloc_arg));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.agno = agno;

	/*
	 * validate that the block number is legal - the enables us to detect
	 * and handle a silent filesystem corruption rather than crashing.
	 */
	if (args.agno >= args.mp->m_sb.sb_agcount)
		return -EFSCORRUPTED;

	args.pag = xfs_perag_get(args.mp, args.agno);
	ASSERT(args.pag);

	error = xfs_alloc_fix_freelist(&args, XFS_ALLOC_FLAG_FREEING);
	if (error)
		goto out;

	*agbp = args.agbp;
out:
	xfs_perag_put(args.pag);
	return error;
}

/*
 * Free an extent.
 * Just break up the extent address and hand off to xfs_free_ag_extent
 * after fixing up the freelist.
 */
int				/* error */
xfs_free_extent(
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_fsblock_t		bno,	/* starting block number of extent */
	xfs_extlen_t		len,	/* length of extent */
	struct xfs_owner_info	*oinfo,	/* extent owner */
	enum xfs_ag_resv_type	type)	/* block reservation type */
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_buf		*agbp;
	xfs_agnumber_t		agno = XFS_FSB_TO_AGNO(mp, bno);
	xfs_agblock_t		agbno = XFS_FSB_TO_AGBNO(mp, bno);
	int			error;

	ASSERT(len != 0);
	ASSERT(type != XFS_AG_RESV_AGFL);

	if (XFS_TEST_ERROR(false, mp,
			XFS_ERRTAG_FREE_EXTENT,
			XFS_RANDOM_FREE_EXTENT))
		return -EIO;

	error = xfs_free_extent_fix_freelist(tp, agno, &agbp);
	if (error)
		return error;

	XFS_WANT_CORRUPTED_GOTO(mp, agbno < mp->m_sb.sb_agblocks, err);

	/* validate the extent size is legal now we have the agf locked */
	XFS_WANT_CORRUPTED_GOTO(mp,
		agbno + len <= be32_to_cpu(XFS_BUF_TO_AGF(agbp)->agf_length),
				err);

	error = xfs_free_ag_extent(tp, agbp, agno, agbno, len, oinfo, type);
	if (error)
		goto err;

	xfs_extent_busy_insert(tp, agno, agbno, len, 0);
	return 0;

err:
	xfs_trans_brelse(tp, agbp);
	return error;
}
