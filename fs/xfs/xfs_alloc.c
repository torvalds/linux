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
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"


#define XFS_ABSDIFF(a,b)	(((a) <= (b)) ? ((b) - (a)) : ((a) - (b)))

#define	XFSA_FIXUP_BNO_OK	1
#define	XFSA_FIXUP_CNT_OK	2

static int
xfs_alloc_busy_search(struct xfs_mount *mp, xfs_agnumber_t agno,
		    xfs_agblock_t bno, xfs_extlen_t len);

/*
 * Prototypes for per-ag allocation routines
 */

STATIC int xfs_alloc_ag_vextent_exact(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_near(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_size(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_small(xfs_alloc_arg_t *,
	xfs_btree_cur_t *, xfs_agblock_t *, xfs_extlen_t *, int *);

/*
 * Internal functions.
 */

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
STATIC int				/* error */
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
STATIC int				/* error */
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
STATIC int				/* error */
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
	xfs_agblock_t	foundbno,	/* starting block in found extent */
	xfs_extlen_t	foundlen,	/* length in found extent */
	xfs_extlen_t	alignment,	/* alignment for allocation */
	xfs_extlen_t	minlen,		/* minimum length for allocation */
	xfs_agblock_t	*resbno,	/* result block number */
	xfs_extlen_t	*reslen)	/* result length */
{
	xfs_agblock_t	bno;
	xfs_extlen_t	diff;
	xfs_extlen_t	len;

	if (alignment > 1 && foundlen >= minlen) {
		bno = roundup(foundbno, alignment);
		diff = bno - foundbno;
		len = diff >= foundlen ? 0 : foundlen - diff;
	} else {
		bno = foundbno;
		len = foundlen;
	}
	*resbno = bno;
	*reslen = len;
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

	ASSERT(freelen >= wantlen);
	freeend = freebno + freelen;
	wantend = wantbno + wantlen;
	if (freebno >= wantbno) {
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
	if (k > args->mod) {
		if ((int)(rlen = rlen - k - args->mod) < (int)args->minlen)
			return;
	} else {
		if ((int)(rlen = rlen - args->prod - (args->mod - k)) <
		    (int)args->minlen)
			return;
	}
	ASSERT(rlen >= args->minlen);
	ASSERT(rlen <= args->maxlen);
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
		+ be32_to_cpu(agf->agf_flcount)
		- args->len - args->minleft;
	if (diff >= 0)
		return 1;
	args->len += diff;		/* shrink the allocated space */
	if (args->len >= args->minlen)
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

	/*
	 * Look up the record in the by-size tree if necessary.
	 */
	if (flags & XFSA_FIXUP_CNT_OK) {
#ifdef DEBUG
		if ((error = xfs_alloc_get_rec(cnt_cur, &nfbno1, &nflen1, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(
			i == 1 && nfbno1 == fbno && nflen1 == flen);
#endif
	} else {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, fbno, flen, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
	}
	/*
	 * Look up the record in the by-block tree if necessary.
	 */
	if (flags & XFSA_FIXUP_BNO_OK) {
#ifdef DEBUG
		if ((error = xfs_alloc_get_rec(bno_cur, &nfbno1, &nflen1, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(
			i == 1 && nfbno1 == fbno && nflen1 == flen);
#endif
	} else {
		if ((error = xfs_alloc_lookup_eq(bno_cur, fbno, flen, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
	}

#ifdef DEBUG
	if (bno_cur->bc_nlevels == 1 && cnt_cur->bc_nlevels == 1) {
		struct xfs_btree_block	*bnoblock;
		struct xfs_btree_block	*cntblock;

		bnoblock = XFS_BUF_TO_BLOCK(bno_cur->bc_bufs[0]);
		cntblock = XFS_BUF_TO_BLOCK(cnt_cur->bc_bufs[0]);

		XFS_WANT_CORRUPTED_RETURN(
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
	XFS_WANT_CORRUPTED_RETURN(i == 1);
	/*
	 * Add new by-size btree entry(s).
	 */
	if (nfbno1 != NULLAGBLOCK) {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, nfbno1, nflen1, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 0);
		if ((error = xfs_btree_insert(cnt_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
	}
	if (nfbno2 != NULLAGBLOCK) {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, nfbno2, nflen2, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 0);
		if ((error = xfs_btree_insert(cnt_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
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
		XFS_WANT_CORRUPTED_RETURN(i == 1);
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
		XFS_WANT_CORRUPTED_RETURN(i == 0);
		if ((error = xfs_btree_insert(bno_cur, &i)))
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
	}
	return 0;
}

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
			XFS_FSS_TO_BB(mp, 1), 0, &bp);
	if (error)
		return error;
	ASSERT(bp);
	ASSERT(!XFS_BUF_GETERROR(bp));
	XFS_BUF_SET_VTYPE_REF(bp, B_FS_AGFL, XFS_AGFL_REF);
	*bpp = bp;
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

	ASSERT(args->minlen > 0);
	ASSERT(args->maxlen > 0);
	ASSERT(args->minlen <= args->maxlen);
	ASSERT(args->mod < args->prod);
	ASSERT(args->alignment > 0);
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
	if (error)
		return error;
	/*
	 * If the allocation worked, need to change the agf structure
	 * (and log it), and the superblock.
	 */
	if (args->agbno != NULLAGBLOCK) {
		xfs_agf_t	*agf;	/* allocation group freelist header */
		long		slen = (long)args->len;

		ASSERT(args->len >= args->minlen && args->len <= args->maxlen);
		ASSERT(!(args->wasfromfl) || !args->isfl);
		ASSERT(args->agbno % args->alignment == 0);
		if (!(args->wasfromfl)) {

			agf = XFS_BUF_TO_AGF(args->agbp);
			be32_add_cpu(&agf->agf_freeblks, -(args->len));
			xfs_trans_agblocks_delta(args->tp,
						 -((long)(args->len)));
			args->pag->pagf_freeblks -= args->len;
			ASSERT(be32_to_cpu(agf->agf_freeblks) <=
				be32_to_cpu(agf->agf_length));
			xfs_alloc_log_agf(args->tp, args->agbp,
						XFS_AGF_FREEBLKS);
			/*
			 * Search the busylist for these blocks and mark the
			 * transaction as synchronous if blocks are found. This
			 * avoids the need to block due to a synchronous log
			 * force to ensure correct ordering as the synchronous
			 * transaction will guarantee that for us.
			 */
			if (xfs_alloc_busy_search(args->mp, args->agno,
						args->agbno, args->len))
				xfs_trans_set_sync(args->tp);
		}
		if (!args->isfl)
			xfs_trans_mod_sb(args->tp,
				args->wasdel ? XFS_TRANS_SB_RES_FDBLOCKS :
					XFS_TRANS_SB_FDBLOCKS, -slen);
		XFS_STATS_INC(xs_allocx);
		XFS_STATS_ADD(xs_allocb, args->len);
	}
	return 0;
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
	xfs_agblock_t	end;	/* end of allocated extent */
	int		error;
	xfs_agblock_t	fbno;	/* start block of found extent */
	xfs_agblock_t	fend;	/* end block of found extent */
	xfs_extlen_t	flen;	/* length of found extent */
	int		i;	/* success/failure of operation */
	xfs_agblock_t	maxend;	/* end of maximal extent */
	xfs_agblock_t	minend;	/* end of minimal extent */
	xfs_extlen_t	rlen;	/* length of returned extent */

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
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	ASSERT(fbno <= args->agbno);
	minend = args->agbno + args->minlen;
	maxend = args->agbno + args->maxlen;
	fend = fbno + flen;

	/*
	 * Give up if the freespace isn't long enough for the minimum request.
	 */
	if (fend < minend)
		goto not_found;

	/*
	 * End of extent will be smaller of the freespace end and the
	 * maximal requested end.
	 *
	 * Fix the length according to mod and prod if given.
	 */
	end = XFS_AGBLOCK_MIN(fend, maxend);
	args->len = end - args->agbno;
	xfs_alloc_fix_len(args);
	if (!xfs_alloc_fix_minleft(args))
		goto not_found;

	rlen = args->len;
	ASSERT(args->agbno + rlen <= fend);
	end = args->agbno + rlen;

	/*
	 * We are allocating agbno for rlen [agbno .. end]
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
	xfs_extlen_t	gtlena = 0;	/* aligned ... */
	xfs_agblock_t	gtnew;		/* useful start bno of right side */
	int		error;		/* error code */
	int		i;		/* result code, temporary */
	int		j;		/* result code, temporary */
	xfs_agblock_t	ltbno;		/* start bno of left side entry */
	xfs_agblock_t	ltbnoa;		/* aligned ... */
	xfs_extlen_t	ltdiff;		/* difference to left side entry */
	xfs_extlen_t	ltlen;		/* length of left side entry */
	xfs_extlen_t	ltlena = 0;	/* aligned ... */
	xfs_agblock_t	ltnew;		/* useful start bno of left side */
	xfs_extlen_t	rlen;		/* length of returned extent */
#if defined(DEBUG) && defined(__KERNEL__)
	/*
	 * Randomly don't execute the first algorithm.
	 */
	int		dofirst;	/* set to do first algorithm */

	dofirst = random32() & 1;
#endif
	/*
	 * Get a cursor for the by-size btree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
		args->agno, XFS_BTNUM_CNT);
	ltlen = 0;
	bno_cur_lt = bno_cur_gt = NULL;
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

#if defined(DEBUG) && defined(__KERNEL__)
		if (!dofirst)
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
				XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			xfs_alloc_compute_aligned(ltbno, ltlen, args->alignment,
					args->minlen, &ltbnoa, &ltlena);
			if (ltlena < args->minlen)
				continue;
			args->len = XFS_EXTLEN_MIN(ltlena, args->maxlen);
			xfs_alloc_fix_len(args);
			ASSERT(args->len >= args->minlen);
			if (args->len < blen)
				continue;
			ltdiff = xfs_alloc_compute_diff(args->agbno, args->len,
				args->alignment, ltbno, ltlen, &ltnew);
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			xfs_alloc_compute_aligned(ltbno, ltlen, args->alignment,
					args->minlen, &ltbnoa, &ltlena);
			if (ltlena >= args->minlen)
				break;
			if ((error = xfs_btree_decrement(bno_cur_lt, 0, &i)))
				goto error0;
			if (!i) {
				xfs_btree_del_cursor(bno_cur_lt,
						     XFS_BTREE_NOERROR);
				bno_cur_lt = NULL;
			}
		}
		if (bno_cur_gt) {
			if ((error = xfs_alloc_get_rec(bno_cur_gt, &gtbno, &gtlen, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			xfs_alloc_compute_aligned(gtbno, gtlen, args->alignment,
					args->minlen, &gtbnoa, &gtlena);
			if (gtlena >= args->minlen)
				break;
			if ((error = xfs_btree_increment(bno_cur_gt, 0, &i)))
				goto error0;
			if (!i) {
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
		/*
		 * Left side is long enough, look for a right side entry.
		 */
		if (ltlena >= args->minlen) {
			/*
			 * Fix up the length.
			 */
			args->len = XFS_EXTLEN_MIN(ltlena, args->maxlen);
			xfs_alloc_fix_len(args);
			rlen = args->len;
			ltdiff = xfs_alloc_compute_diff(args->agbno, rlen,
				args->alignment, ltbno, ltlen, &ltnew);
			/*
			 * Not perfect.
			 */
			if (ltdiff) {
				/*
				 * Look until we find a better one, run out of
				 * space, or run off the end.
				 */
				while (bno_cur_lt && bno_cur_gt) {
					if ((error = xfs_alloc_get_rec(
							bno_cur_gt, &gtbno,
							&gtlen, &i)))
						goto error0;
					XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
					xfs_alloc_compute_aligned(gtbno, gtlen,
						args->alignment, args->minlen,
						&gtbnoa, &gtlena);
					/*
					 * The left one is clearly better.
					 */
					if (gtbnoa >= args->agbno + ltdiff) {
						xfs_btree_del_cursor(
							bno_cur_gt,
							XFS_BTREE_NOERROR);
						bno_cur_gt = NULL;
						break;
					}
					/*
					 * If we reach a big enough entry,
					 * compare the two and pick the best.
					 */
					if (gtlena >= args->minlen) {
						args->len =
							XFS_EXTLEN_MIN(gtlena,
								args->maxlen);
						xfs_alloc_fix_len(args);
						rlen = args->len;
						gtdiff = xfs_alloc_compute_diff(
							args->agbno, rlen,
							args->alignment,
							gtbno, gtlen, &gtnew);
						/*
						 * Right side is better.
						 */
						if (gtdiff < ltdiff) {
							xfs_btree_del_cursor(
								bno_cur_lt,
								XFS_BTREE_NOERROR);
							bno_cur_lt = NULL;
						}
						/*
						 * Left side is better.
						 */
						else {
							xfs_btree_del_cursor(
								bno_cur_gt,
								XFS_BTREE_NOERROR);
							bno_cur_gt = NULL;
						}
						break;
					}
					/*
					 * Fell off the right end.
					 */
					if ((error = xfs_btree_increment(
							bno_cur_gt, 0, &i)))
						goto error0;
					if (!i) {
						xfs_btree_del_cursor(
							bno_cur_gt,
							XFS_BTREE_NOERROR);
						bno_cur_gt = NULL;
						break;
					}
				}
			}
			/*
			 * The left side is perfect, trash the right side.
			 */
			else {
				xfs_btree_del_cursor(bno_cur_gt,
						     XFS_BTREE_NOERROR);
				bno_cur_gt = NULL;
			}
		}
		/*
		 * It's the right side that was found first, look left.
		 */
		else {
			/*
			 * Fix up the length.
			 */
			args->len = XFS_EXTLEN_MIN(gtlena, args->maxlen);
			xfs_alloc_fix_len(args);
			rlen = args->len;
			gtdiff = xfs_alloc_compute_diff(args->agbno, rlen,
				args->alignment, gtbno, gtlen, &gtnew);
			/*
			 * Right side entry isn't perfect.
			 */
			if (gtdiff) {
				/*
				 * Look until we find a better one, run out of
				 * space, or run off the end.
				 */
				while (bno_cur_lt && bno_cur_gt) {
					if ((error = xfs_alloc_get_rec(
							bno_cur_lt, &ltbno,
							&ltlen, &i)))
						goto error0;
					XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
					xfs_alloc_compute_aligned(ltbno, ltlen,
						args->alignment, args->minlen,
						&ltbnoa, &ltlena);
					/*
					 * The right one is clearly better.
					 */
					if (ltbnoa <= args->agbno - gtdiff) {
						xfs_btree_del_cursor(
							bno_cur_lt,
							XFS_BTREE_NOERROR);
						bno_cur_lt = NULL;
						break;
					}
					/*
					 * If we reach a big enough entry,
					 * compare the two and pick the best.
					 */
					if (ltlena >= args->minlen) {
						args->len = XFS_EXTLEN_MIN(
							ltlena, args->maxlen);
						xfs_alloc_fix_len(args);
						rlen = args->len;
						ltdiff = xfs_alloc_compute_diff(
							args->agbno, rlen,
							args->alignment,
							ltbno, ltlen, &ltnew);
						/*
						 * Left side is better.
						 */
						if (ltdiff < gtdiff) {
							xfs_btree_del_cursor(
								bno_cur_gt,
								XFS_BTREE_NOERROR);
							bno_cur_gt = NULL;
						}
						/*
						 * Right side is better.
						 */
						else {
							xfs_btree_del_cursor(
								bno_cur_lt,
								XFS_BTREE_NOERROR);
							bno_cur_lt = NULL;
						}
						break;
					}
					/*
					 * Fell off the left end.
					 */
					if ((error = xfs_btree_decrement(
							bno_cur_lt, 0, &i)))
						goto error0;
					if (!i) {
						xfs_btree_del_cursor(bno_cur_lt,
							XFS_BTREE_NOERROR);
						bno_cur_lt = NULL;
						break;
					}
				}
			}
			/*
			 * The right side is perfect, trash the left side.
			 */
			else {
				xfs_btree_del_cursor(bno_cur_lt,
					XFS_BTREE_NOERROR);
				bno_cur_lt = NULL;
			}
		}
	}
	/*
	 * If we couldn't get anything, give up.
	 */
	if (bno_cur_lt == NULL && bno_cur_gt == NULL) {
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
	(void)xfs_alloc_compute_diff(args->agbno, rlen, args->alignment, ltbno,
		ltlen, &ltnew);
	ASSERT(ltnew >= ltbno);
	ASSERT(ltnew + rlen <= ltbno + ltlen);
	ASSERT(ltnew + rlen <= be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length));
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
	 * If none, then pick up the last entry in the tree unless the
	 * tree is empty.
	 */
	if (!i) {
		if ((error = xfs_alloc_ag_vextent_small(args, cnt_cur, &fbno,
				&flen, &i)))
			goto error0;
		if (i == 0 || flen == 0) {
			xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
			trace_xfs_alloc_size_noentry(args);
			return 0;
		}
		ASSERT(i == 1);
	}
	/*
	 * There's a freespace as big as maxlen+alignment-1, get it.
	 */
	else {
		if ((error = xfs_alloc_get_rec(cnt_cur, &fbno, &flen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	}
	/*
	 * In the first case above, we got the last entry in the
	 * by-size btree.  Now we check to see if the space hits maxlen
	 * once aligned; if not, we search left for something better.
	 * This can't happen in the second case above.
	 */
	xfs_alloc_compute_aligned(fbno, flen, args->alignment, args->minlen,
		&rbno, &rlen);
	rlen = XFS_EXTLEN_MIN(args->maxlen, rlen);
	XFS_WANT_CORRUPTED_GOTO(rlen == 0 ||
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
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			if (flen < bestrlen)
				break;
			xfs_alloc_compute_aligned(fbno, flen, args->alignment,
				args->minlen, &rbno, &rlen);
			rlen = XFS_EXTLEN_MIN(args->maxlen, rlen);
			XFS_WANT_CORRUPTED_GOTO(rlen == 0 ||
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
	xfs_alloc_fix_len(args);
	if (rlen < args->minlen || !xfs_alloc_fix_minleft(args)) {
		xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
		trace_xfs_alloc_size_nominleft(args);
		args->agbno = NULLAGBLOCK;
		return 0;
	}
	rlen = args->len;
	XFS_WANT_CORRUPTED_GOTO(rlen <= flen, error0);
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
	XFS_WANT_CORRUPTED_GOTO(
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
	int		error;
	xfs_agblock_t	fbno;
	xfs_extlen_t	flen;
	int		i;

	if ((error = xfs_btree_decrement(ccur, 0, &i)))
		goto error0;
	if (i) {
		if ((error = xfs_alloc_get_rec(ccur, &fbno, &flen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	}
	/*
	 * Nothing in the btree, try the freelist.  Make sure
	 * to respect minleft even when pulling from the
	 * freelist.
	 */
	else if (args->minlen == 1 && args->alignment == 1 && !args->isfl &&
		 (be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_flcount)
		  > args->minleft)) {
		error = xfs_alloc_get_freelist(args->tp, args->agbp, &fbno, 0);
		if (error)
			goto error0;
		if (fbno != NULLAGBLOCK) {
			if (args->userdata) {
				xfs_buf_t	*bp;

				bp = xfs_btree_get_bufs(args->mp, args->tp,
					args->agno, fbno, 0);
				xfs_trans_binval(args->tp, bp);
			}
			args->len = 1;
			args->agbno = fbno;
			XFS_WANT_CORRUPTED_GOTO(
				args->agbno + args->len <=
				be32_to_cpu(XFS_BUF_TO_AGF(args->agbp)->agf_length),
				error0);
			args->wasfromfl = 1;
			trace_xfs_alloc_small_freelist(args);
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
STATIC int			/* error */
xfs_free_ag_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_buf_t	*agbp,	/* buffer for a.g. freelist header */
	xfs_agnumber_t	agno,	/* allocation group number */
	xfs_agblock_t	bno,	/* starting block number */
	xfs_extlen_t	len,	/* length of extent */
	int		isfl)	/* set if is freelist blocks - no sb acctg */
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

	mp = tp->t_mountp;
	/*
	 * Allocate and initialize a cursor for the by-block btree.
	 */
	bno_cur = xfs_allocbt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_BNO);
	cnt_cur = NULL;
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
			XFS_WANT_CORRUPTED_GOTO(ltbno + ltlen <= bno, error0);
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
			XFS_WANT_CORRUPTED_GOTO(gtbno >= bno + len, error0);
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Delete the old by-size entry on the right.
		 */
		if ((error = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Delete the old by-block entry for the right block.
		 */
		if ((error = xfs_btree_delete(bno_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Move the by-block cursor back to the left neighbor.
		 */
		if ((error = xfs_btree_decrement(bno_cur, 0, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
			XFS_WANT_CORRUPTED_GOTO(
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Back up the by-block cursor to the left neighbor, and
		 * update its length.
		 */
		if ((error = xfs_btree_decrement(bno_cur, 0, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
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
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	}
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	bno_cur = NULL;
	/*
	 * In all cases we need to insert the new freespace in the by-size tree.
	 */
	if ((error = xfs_alloc_lookup_eq(cnt_cur, nbno, nlen, &i)))
		goto error0;
	XFS_WANT_CORRUPTED_GOTO(i == 0, error0);
	if ((error = xfs_btree_insert(cnt_cur, &i)))
		goto error0;
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	cnt_cur = NULL;
	/*
	 * Update the freespace totals in the ag and superblock.
	 */
	{
		xfs_agf_t	*agf;
		xfs_perag_t	*pag;		/* per allocation group data */

		pag = xfs_perag_get(mp, agno);
		pag->pagf_freeblks += len;
		xfs_perag_put(pag);

		agf = XFS_BUF_TO_AGF(agbp);
		be32_add_cpu(&agf->agf_freeblks, len);
		xfs_trans_agblocks_delta(tp, len);
		XFS_WANT_CORRUPTED_GOTO(
			be32_to_cpu(agf->agf_freeblks) <=
			be32_to_cpu(agf->agf_length),
			error0);
		xfs_alloc_log_agf(tp, agbp, XFS_AGF_FREEBLKS);
		if (!isfl)
			xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, (long)len);
		XFS_STATS_INC(xs_freex);
		XFS_STATS_ADD(xs_freeb, len);
	}

	trace_xfs_free_extent(mp, agno, bno, len, isfl, haveleft, haveright);

	/*
	 * Since blocks move to the free list without the coordination
	 * used in xfs_bmap_finish, we can't allow block to be available
	 * for reallocation and non-transaction writing (user data)
	 * until we know that the transaction that moved it to the free
	 * list is permanently on disk.  We track the blocks by declaring
	 * these blocks as "busy"; the busy list is maintained on a per-ag
	 * basis and each transaction records which entries should be removed
	 * when the iclog commits to disk.  If a busy block is allocated,
	 * the iclog is pushed up to the LSN that freed the block.
	 */
	xfs_alloc_busy_insert(tp, agno, bno, len);
	return 0;

 error0:
	trace_xfs_free_extent(mp, agno, bno, len, isfl, -1, -1);
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
	int		level;
	uint		maxblocks;
	uint		maxleafents;
	int		minleafrecs;
	int		minnoderecs;

	maxleafents = (mp->m_sb.sb_agblocks + 1) / 2;
	minleafrecs = mp->m_alloc_mnr[0];
	minnoderecs = mp->m_alloc_mnr[1];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++)
		maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	mp->m_ag_maxlevels = level;
}

/*
 * Find the length of the longest extent in an AG.
 */
xfs_extlen_t
xfs_alloc_longest_free_extent(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag)
{
	xfs_extlen_t		need, delta = 0;

	need = XFS_MIN_FREELIST_PAG(pag, mp);
	if (need > pag->pagf_flcount)
		delta = need - pag->pagf_flcount;

	if (pag->pagf_longest > delta)
		return pag->pagf_longest - delta;
	return pag->pagf_flcount > 0 || pag->pagf_longest > 0;
}

/*
 * Decide whether to use this allocation group for this allocation.
 * If so, fix up the btree freelist's size.
 */
STATIC int			/* error */
xfs_alloc_fix_freelist(
	xfs_alloc_arg_t	*args,	/* allocation argument structure */
	int		flags)	/* XFS_ALLOC_FLAG_... */
{
	xfs_buf_t	*agbp;	/* agf buffer pointer */
	xfs_agf_t	*agf;	/* a.g. freespace structure pointer */
	xfs_buf_t	*agflbp;/* agfl buffer pointer */
	xfs_agblock_t	bno;	/* freelist block */
	xfs_extlen_t	delta;	/* new blocks needed in freelist */
	int		error;	/* error result code */
	xfs_extlen_t	longest;/* longest extent in allocation group */
	xfs_mount_t	*mp;	/* file system mount point structure */
	xfs_extlen_t	need;	/* total blocks needed in freelist */
	xfs_perag_t	*pag;	/* per-ag information structure */
	xfs_alloc_arg_t	targs;	/* local allocation arguments */
	xfs_trans_t	*tp;	/* transaction pointer */

	mp = args->mp;

	pag = args->pag;
	tp = args->tp;
	if (!pag->pagf_init) {
		if ((error = xfs_alloc_read_agf(mp, tp, args->agno, flags,
				&agbp)))
			return error;
		if (!pag->pagf_init) {
			ASSERT(flags & XFS_ALLOC_FLAG_TRYLOCK);
			ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
			args->agbp = NULL;
			return 0;
		}
	} else
		agbp = NULL;

	/*
	 * If this is a metadata preferred pag and we are user data
	 * then try somewhere else if we are not being asked to
	 * try harder at this point
	 */
	if (pag->pagf_metadata && args->userdata &&
	    (flags & XFS_ALLOC_FLAG_TRYLOCK)) {
		ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
		args->agbp = NULL;
		return 0;
	}

	if (!(flags & XFS_ALLOC_FLAG_FREEING)) {
		/*
		 * If it looks like there isn't a long enough extent, or enough
		 * total blocks, reject it.
		 */
		need = XFS_MIN_FREELIST_PAG(pag, mp);
		longest = xfs_alloc_longest_free_extent(mp, pag);
		if ((args->minlen + args->alignment + args->minalignslop - 1) >
				longest ||
		    ((int)(pag->pagf_freeblks + pag->pagf_flcount -
			   need - args->total) < (int)args->minleft)) {
			if (agbp)
				xfs_trans_brelse(tp, agbp);
			args->agbp = NULL;
			return 0;
		}
	}

	/*
	 * Get the a.g. freespace buffer.
	 * Can fail if we're not blocking on locks, and it's held.
	 */
	if (agbp == NULL) {
		if ((error = xfs_alloc_read_agf(mp, tp, args->agno, flags,
				&agbp)))
			return error;
		if (agbp == NULL) {
			ASSERT(flags & XFS_ALLOC_FLAG_TRYLOCK);
			ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
			args->agbp = NULL;
			return 0;
		}
	}
	/*
	 * Figure out how many blocks we should have in the freelist.
	 */
	agf = XFS_BUF_TO_AGF(agbp);
	need = XFS_MIN_FREELIST(agf, mp);
	/*
	 * If there isn't enough total or single-extent, reject it.
	 */
	if (!(flags & XFS_ALLOC_FLAG_FREEING)) {
		delta = need > be32_to_cpu(agf->agf_flcount) ?
			(need - be32_to_cpu(agf->agf_flcount)) : 0;
		longest = be32_to_cpu(agf->agf_longest);
		longest = (longest > delta) ? (longest - delta) :
			(be32_to_cpu(agf->agf_flcount) > 0 || longest > 0);
		if ((args->minlen + args->alignment + args->minalignslop - 1) >
				longest ||
		    ((int)(be32_to_cpu(agf->agf_freeblks) +
		     be32_to_cpu(agf->agf_flcount) - need - args->total) <
				(int)args->minleft)) {
			xfs_trans_brelse(tp, agbp);
			args->agbp = NULL;
			return 0;
		}
	}
	/*
	 * Make the freelist shorter if it's too long.
	 */
	while (be32_to_cpu(agf->agf_flcount) > need) {
		xfs_buf_t	*bp;

		error = xfs_alloc_get_freelist(tp, agbp, &bno, 0);
		if (error)
			return error;
		if ((error = xfs_free_ag_extent(tp, agbp, args->agno, bno, 1, 1)))
			return error;
		bp = xfs_btree_get_bufs(mp, tp, args->agno, bno, 0);
		xfs_trans_binval(tp, bp);
	}
	/*
	 * Initialize the args structure.
	 */
	targs.tp = tp;
	targs.mp = mp;
	targs.agbp = agbp;
	targs.agno = args->agno;
	targs.mod = targs.minleft = targs.wasdel = targs.userdata =
		targs.minalignslop = 0;
	targs.alignment = targs.minlen = targs.prod = targs.isfl = 1;
	targs.type = XFS_ALLOCTYPE_THIS_AG;
	targs.pag = pag;
	if ((error = xfs_alloc_read_agfl(mp, tp, targs.agno, &agflbp)))
		return error;
	/*
	 * Make the freelist longer if it's too short.
	 */
	while (be32_to_cpu(agf->agf_flcount) < need) {
		targs.agbno = 0;
		targs.maxlen = need - be32_to_cpu(agf->agf_flcount);
		/*
		 * Allocate as many blocks as possible at once.
		 */
		if ((error = xfs_alloc_ag_vextent(&targs))) {
			xfs_trans_brelse(tp, agflbp);
			return error;
		}
		/*
		 * Stop if we run out.  Won't happen if callers are obeying
		 * the restrictions correctly.  Can happen for free calls
		 * on a completely full ag.
		 */
		if (targs.agbno == NULLAGBLOCK) {
			if (flags & XFS_ALLOC_FLAG_FREEING)
				break;
			xfs_trans_brelse(tp, agflbp);
			args->agbp = NULL;
			return 0;
		}
		/*
		 * Put each allocated block on the list.
		 */
		for (bno = targs.agbno; bno < targs.agbno + targs.len; bno++) {
			error = xfs_alloc_put_freelist(tp, agbp,
							agflbp, bno, 0);
			if (error)
				return error;
		}
	}
	xfs_trans_brelse(tp, agflbp);
	args->agbp = agbp;
	return 0;
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
	xfs_agfl_t	*agfl;	/* a.g. freelist structure */
	xfs_buf_t	*agflbp;/* buffer for a.g. freelist structure */
	xfs_agblock_t	bno;	/* block number returned */
	int		error;
	int		logflags;
	xfs_mount_t	*mp;	/* mount structure */
	xfs_perag_t	*pag;	/* per allocation group data */

	agf = XFS_BUF_TO_AGF(agbp);
	/*
	 * Freelist is empty, give up.
	 */
	if (!agf->agf_flcount) {
		*bnop = NULLAGBLOCK;
		return 0;
	}
	/*
	 * Read the array of free blocks.
	 */
	mp = tp->t_mountp;
	if ((error = xfs_alloc_read_agfl(mp, tp,
			be32_to_cpu(agf->agf_seqno), &agflbp)))
		return error;
	agfl = XFS_BUF_TO_AGFL(agflbp);
	/*
	 * Get the block number and update the data structures.
	 */
	bno = be32_to_cpu(agfl->agfl_bno[be32_to_cpu(agf->agf_flfirst)]);
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

	/*
	 * As blocks are freed, they are added to the per-ag busy list and
	 * remain there until the freeing transaction is committed to disk.
	 * Now that we have allocated blocks, this list must be searched to see
	 * if a block is being reused.  If one is, then the freeing transaction
	 * must be pushed to disk before this transaction.
	 *
	 * We do this by setting the current transaction to a sync transaction
	 * which guarantees that the freeing transaction is on disk before this
	 * transaction. This is done instead of a synchronous log force here so
	 * that we don't sit and wait with the AGF locked in the transaction
	 * during the log force.
	 */
	if (xfs_alloc_busy_search(mp, be32_to_cpu(agf->agf_seqno), bno, 1))
		xfs_trans_set_sync(tp);
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
		sizeof(xfs_agf_t)
	};

	trace_xfs_agf(tp->t_mountp, XFS_BUF_TO_AGF(bp), fields, _RET_IP_);

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
	xfs_agfl_t		*agfl;	/* a.g. free block array */
	__be32			*blockp;/* pointer to array entry */
	int			error;
	int			logflags;
	xfs_mount_t		*mp;	/* mount structure */
	xfs_perag_t		*pag;	/* per allocation group data */

	agf = XFS_BUF_TO_AGF(agbp);
	mp = tp->t_mountp;

	if (!agflbp && (error = xfs_alloc_read_agfl(mp, tp,
			be32_to_cpu(agf->agf_seqno), &agflbp)))
		return error;
	agfl = XFS_BUF_TO_AGFL(agflbp);
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
	blockp = &agfl->agfl_bno[be32_to_cpu(agf->agf_fllast)];
	*blockp = cpu_to_be32(bno);
	xfs_alloc_log_agf(tp, agbp, logflags);
	xfs_trans_log_buf(tp, agflbp,
		(int)((xfs_caddr_t)blockp - (xfs_caddr_t)agfl),
		(int)((xfs_caddr_t)blockp - (xfs_caddr_t)agfl +
			sizeof(xfs_agblock_t) - 1));
	return 0;
}

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
	struct xfs_agf	*agf;		/* ag freelist header */
	int		agf_ok;		/* set if agf is consistent */
	int		error;

	ASSERT(agno != NULLAGNUMBER);
	error = xfs_trans_read_buf(
			mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), flags, bpp);
	if (error)
		return error;
	if (!*bpp)
		return 0;

	ASSERT(!XFS_BUF_GETERROR(*bpp));
	agf = XFS_BUF_TO_AGF(*bpp);

	/*
	 * Validate the magic number of the agf block.
	 */
	agf_ok =
		be32_to_cpu(agf->agf_magicnum) == XFS_AGF_MAGIC &&
		XFS_AGF_GOOD_VERSION(be32_to_cpu(agf->agf_versionnum)) &&
		be32_to_cpu(agf->agf_freeblks) <= be32_to_cpu(agf->agf_length) &&
		be32_to_cpu(agf->agf_flfirst) < XFS_AGFL_SIZE(mp) &&
		be32_to_cpu(agf->agf_fllast) < XFS_AGFL_SIZE(mp) &&
		be32_to_cpu(agf->agf_flcount) <= XFS_AGFL_SIZE(mp) &&
		be32_to_cpu(agf->agf_seqno) == agno;
	if (xfs_sb_version_haslazysbcount(&mp->m_sb))
		agf_ok = agf_ok && be32_to_cpu(agf->agf_btreeblks) <=
						be32_to_cpu(agf->agf_length);
	if (unlikely(XFS_TEST_ERROR(!agf_ok, mp, XFS_ERRTAG_ALLOC_READ_AGF,
			XFS_RANDOM_ALLOC_READ_AGF))) {
		XFS_CORRUPTION_ERROR("xfs_alloc_read_agf",
				     XFS_ERRLEVEL_LOW, mp, agf);
		xfs_trans_brelse(tp, *bpp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	XFS_BUF_SET_VTYPE_REF(*bpp, B_FS_AGF, XFS_AGF_REF);
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

	ASSERT(agno != NULLAGNUMBER);

	error = xfs_read_agf(mp, tp, agno,
			(flags & XFS_ALLOC_FLAG_TRYLOCK) ? XBF_TRYLOCK : 0,
			bpp);
	if (error)
		return error;
	if (!*bpp)
		return 0;
	ASSERT(!XFS_BUF_GETERROR(*bpp));

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
		if ((args->userdata  == XFS_ALLOC_INITIAL_USER_DATA) &&
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
	}
	xfs_perag_put(args->pag);
	return 0;
error0:
	xfs_perag_put(args->pag);
	return error;
}

/*
 * Free an extent.
 * Just break up the extent address and hand off to xfs_free_ag_extent
 * after fixing up the freelist.
 */
int				/* error */
xfs_free_extent(
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_fsblock_t	bno,	/* starting block number of extent */
	xfs_extlen_t	len)	/* length of extent */
{
	xfs_alloc_arg_t	args;
	int		error;

	ASSERT(len != 0);
	memset(&args, 0, sizeof(xfs_alloc_arg_t));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.agno = XFS_FSB_TO_AGNO(args.mp, bno);
	ASSERT(args.agno < args.mp->m_sb.sb_agcount);
	args.agbno = XFS_FSB_TO_AGBNO(args.mp, bno);
	args.pag = xfs_perag_get(args.mp, args.agno);
	if ((error = xfs_alloc_fix_freelist(&args, XFS_ALLOC_FLAG_FREEING)))
		goto error0;
#ifdef DEBUG
	ASSERT(args.agbp != NULL);
	ASSERT((args.agbno + len) <=
		be32_to_cpu(XFS_BUF_TO_AGF(args.agbp)->agf_length));
#endif
	error = xfs_free_ag_extent(tp, args.agbp, args.agno, args.agbno, len, 0);
error0:
	xfs_perag_put(args.pag);
	return error;
}


/*
 * AG Busy list management
 * The busy list contains block ranges that have been freed but whose
 * transactions have not yet hit disk.  If any block listed in a busy
 * list is reused, the transaction that freed it must be forced to disk
 * before continuing to use the block.
 *
 * xfs_alloc_busy_insert - add to the per-ag busy list
 * xfs_alloc_busy_clear - remove an item from the per-ag busy list
 * xfs_alloc_busy_search - search for a busy extent
 */

/*
 * Insert a new extent into the busy tree.
 *
 * The busy extent tree is indexed by the start block of the busy extent.
 * there can be multiple overlapping ranges in the busy extent tree but only
 * ever one entry at a given start block. The reason for this is that
 * multi-block extents can be freed, then smaller chunks of that extent
 * allocated and freed again before the first transaction commit is on disk.
 * If the exact same start block is freed a second time, we have to wait for
 * that busy extent to pass out of the tree before the new extent is inserted.
 * There are two main cases we have to handle here.
 *
 * The first case is a transaction that triggers a "free - allocate - free"
 * cycle. This can occur during btree manipulations as a btree block is freed
 * to the freelist, then allocated from the free list, then freed again. In
 * this case, the second extxpnet free is what triggers the duplicate and as
 * such the transaction IDs should match. Because the extent was allocated in
 * this transaction, the transaction must be marked as synchronous. This is
 * true for all cases where the free/alloc/free occurs in the one transaction,
 * hence the addition of the ASSERT(tp->t_flags & XFS_TRANS_SYNC) to this case.
 * This serves to catch violations of the second case quite effectively.
 *
 * The second case is where the free/alloc/free occur in different
 * transactions. In this case, the thread freeing the extent the second time
 * can't mark the extent busy immediately because it is already tracked in a
 * transaction that may be committing.  When the log commit for the existing
 * busy extent completes, the busy extent will be removed from the tree. If we
 * allow the second busy insert to continue using that busy extent structure,
 * it can be freed before this transaction is safely in the log.  Hence our
 * only option in this case is to force the log to remove the existing busy
 * extent from the list before we insert the new one with the current
 * transaction ID.
 *
 * The problem we are trying to avoid in the free-alloc-free in separate
 * transactions is most easily described with a timeline:
 *
 *      Thread 1	Thread 2	Thread 3	xfslogd
 *	xact alloc
 *	free X
 *	mark busy
 *	commit xact
 *	free xact
 *			xact alloc
 *			alloc X
 *			busy search
 *			mark xact sync
 *			commit xact
 *			free xact
 *			force log
 *			checkpoint starts
 *			....
 *					xact alloc
 *					free X
 *					mark busy
 *					finds match
 *					*** KABOOM! ***
 *					....
 *							log IO completes
 *							unbusy X
 *			checkpoint completes
 *
 * By issuing a log force in thread 3 @ "KABOOM", the thread will block until
 * the checkpoint completes, and the busy extent it matched will have been
 * removed from the tree when it is woken. Hence it can then continue safely.
 *
 * However, to ensure this matching process is robust, we need to use the
 * transaction ID for identifying transaction, as delayed logging results in
 * the busy extent and transaction lifecycles being different. i.e. the busy
 * extent is active for a lot longer than the transaction.  Hence the
 * transaction structure can be freed and reallocated, then mark the same
 * extent busy again in the new transaction. In this case the new transaction
 * will have a different tid but can have the same address, and hence we need
 * to check against the tid.
 *
 * Future: for delayed logging, we could avoid the log force if the extent was
 * first freed in the current checkpoint sequence. This, however, requires the
 * ability to pin the current checkpoint in memory until this transaction
 * commits to ensure that both the original free and the current one combine
 * logically into the one checkpoint. If the checkpoint sequences are
 * different, however, we still need to wait on a log force.
 */
void
xfs_alloc_busy_insert(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len)
{
	struct xfs_busy_extent	*new;
	struct xfs_busy_extent	*busyp;
	struct xfs_perag	*pag;
	struct rb_node		**rbp;
	struct rb_node		*parent;
	int			match;


	new = kmem_zalloc(sizeof(struct xfs_busy_extent), KM_MAYFAIL);
	if (!new) {
		/*
		 * No Memory!  Since it is now not possible to track the free
		 * block, make this a synchronous transaction to insure that
		 * the block is not reused before this transaction commits.
		 */
		trace_xfs_alloc_busy(tp, agno, bno, len, 1);
		xfs_trans_set_sync(tp);
		return;
	}

	new->agno = agno;
	new->bno = bno;
	new->length = len;
	new->tid = xfs_log_get_trans_ident(tp);

	INIT_LIST_HEAD(&new->list);

	/* trace before insert to be able to see failed inserts */
	trace_xfs_alloc_busy(tp, agno, bno, len, 0);

	pag = xfs_perag_get(tp->t_mountp, new->agno);
restart:
	spin_lock(&pag->pagb_lock);
	rbp = &pag->pagb_tree.rb_node;
	parent = NULL;
	busyp = NULL;
	match = 0;
	while (*rbp && match >= 0) {
		parent = *rbp;
		busyp = rb_entry(parent, struct xfs_busy_extent, rb_node);

		if (new->bno < busyp->bno) {
			/* may overlap, but exact start block is lower */
			rbp = &(*rbp)->rb_left;
			if (new->bno + new->length > busyp->bno)
				match = busyp->tid == new->tid ? 1 : -1;
		} else if (new->bno > busyp->bno) {
			/* may overlap, but exact start block is higher */
			rbp = &(*rbp)->rb_right;
			if (bno < busyp->bno + busyp->length)
				match = busyp->tid == new->tid ? 1 : -1;
		} else {
			match = busyp->tid == new->tid ? 1 : -1;
			break;
		}
	}
	if (match < 0) {
		/* overlap marked busy in different transaction */
		spin_unlock(&pag->pagb_lock);
		xfs_log_force(tp->t_mountp, XFS_LOG_SYNC);
		goto restart;
	}
	if (match > 0) {
		/*
		 * overlap marked busy in same transaction. Update if exact
		 * start block match, otherwise combine the busy extents into
		 * a single range.
		 */
		if (busyp->bno == new->bno) {
			busyp->length = max(busyp->length, new->length);
			spin_unlock(&pag->pagb_lock);
			ASSERT(tp->t_flags & XFS_TRANS_SYNC);
			xfs_perag_put(pag);
			kmem_free(new);
			return;
		}
		rb_erase(&busyp->rb_node, &pag->pagb_tree);
		new->length = max(busyp->bno + busyp->length,
					new->bno + new->length) -
				min(busyp->bno, new->bno);
		new->bno = min(busyp->bno, new->bno);
	} else
		busyp = NULL;

	rb_link_node(&new->rb_node, parent, rbp);
	rb_insert_color(&new->rb_node, &pag->pagb_tree);

	list_add(&new->list, &tp->t_busy);
	spin_unlock(&pag->pagb_lock);
	xfs_perag_put(pag);
	kmem_free(busyp);
}

/*
 * Search for a busy extent within the range of the extent we are about to
 * allocate.  You need to be holding the busy extent tree lock when calling
 * xfs_alloc_busy_search(). This function returns 0 for no overlapping busy
 * extent, -1 for an overlapping but not exact busy extent, and 1 for an exact
 * match. This is done so that a non-zero return indicates an overlap that
 * will require a synchronous transaction, but it can still be
 * used to distinguish between a partial or exact match.
 */
static int
xfs_alloc_busy_search(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len)
{
	struct xfs_perag	*pag;
	struct rb_node		*rbp;
	struct xfs_busy_extent	*busyp;
	int			match = 0;

	pag = xfs_perag_get(mp, agno);
	spin_lock(&pag->pagb_lock);

	rbp = pag->pagb_tree.rb_node;

	/* find closest start bno overlap */
	while (rbp) {
		busyp = rb_entry(rbp, struct xfs_busy_extent, rb_node);
		if (bno < busyp->bno) {
			/* may overlap, but exact start block is lower */
			if (bno + len > busyp->bno)
				match = -1;
			rbp = rbp->rb_left;
		} else if (bno > busyp->bno) {
			/* may overlap, but exact start block is higher */
			if (bno < busyp->bno + busyp->length)
				match = -1;
			rbp = rbp->rb_right;
		} else {
			/* bno matches busyp, length determines exact match */
			match = (busyp->length == len) ? 1 : -1;
			break;
		}
	}
	spin_unlock(&pag->pagb_lock);
	trace_xfs_alloc_busysearch(mp, agno, bno, len, !!match);
	xfs_perag_put(pag);
	return match;
}

void
xfs_alloc_busy_clear(
	struct xfs_mount	*mp,
	struct xfs_busy_extent	*busyp)
{
	struct xfs_perag	*pag;

	trace_xfs_alloc_unbusy(mp, busyp->agno, busyp->bno,
						busyp->length);

	ASSERT(xfs_alloc_busy_search(mp, busyp->agno, busyp->bno,
						busyp->length) == 1);

	list_del_init(&busyp->list);

	pag = xfs_perag_get(mp, busyp->agno);
	spin_lock(&pag->pagb_lock);
	rb_erase(&busyp->rb_node, &pag->pagb_tree);
	spin_unlock(&pag->pagb_lock);
	xfs_perag_put(pag);

	kmem_free(busyp);
}
