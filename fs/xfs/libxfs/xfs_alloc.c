// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_bmap.h"

struct kmem_cache	*xfs_extfree_item_cache;

struct workqueue_struct *xfs_alloc_wq;

#define XFS_ABSDIFF(a,b)	(((a) <= (b)) ? ((b) - (a)) : ((a) - (b)))

#define	XFSA_FIXUP_BNO_OK	1
#define	XFSA_FIXUP_CNT_OK	2

STATIC int xfs_alloc_ag_vextent_exact(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_near(xfs_alloc_arg_t *);
STATIC int xfs_alloc_ag_vextent_size(xfs_alloc_arg_t *);

/*
 * Size of the AGFL.  For CRC-enabled filesystes we steal a couple of slots in
 * the beginning of the block for a proper header with the location information
 * and CRC.
 */
unsigned int
xfs_agfl_size(
	struct xfs_mount	*mp)
{
	unsigned int		size = mp->m_sb.sb_sectsize;

	if (xfs_has_crc(mp))
		size -= sizeof(struct xfs_agfl);

	return size / sizeof(xfs_agblock_t);
}

unsigned int
xfs_refc_block(
	struct xfs_mount	*mp)
{
	if (xfs_has_rmapbt(mp))
		return XFS_RMAP_BLOCK(mp) + 1;
	if (xfs_has_finobt(mp))
		return XFS_FIBT_BLOCK(mp) + 1;
	return XFS_IBT_BLOCK(mp) + 1;
}

xfs_extlen_t
xfs_prealloc_blocks(
	struct xfs_mount	*mp)
{
	if (xfs_has_reflink(mp))
		return xfs_refc_block(mp) + 1;
	if (xfs_has_rmapbt(mp))
		return XFS_RMAP_BLOCK(mp) + 1;
	if (xfs_has_finobt(mp))
		return XFS_FIBT_BLOCK(mp) + 1;
	return XFS_IBT_BLOCK(mp) + 1;
}

/*
 * The number of blocks per AG that we withhold from xfs_mod_fdblocks to
 * guarantee that we can refill the AGFL prior to allocating space in a nearly
 * full AG.  Although the space described by the free space btrees, the
 * blocks used by the freesp btrees themselves, and the blocks owned by the
 * AGFL are counted in the ondisk fdblocks, it's a mistake to let the ondisk
 * free space in the AG drop so low that the free space btrees cannot refill an
 * empty AGFL up to the minimum level.  Rather than grind through empty AGs
 * until the fs goes down, we subtract this many AG blocks from the incore
 * fdblocks to ensure user allocation does not overcommit the space the
 * filesystem needs for the AGFLs.  The rmap btree uses a per-AG reservation to
 * withhold space from xfs_mod_fdblocks, so we do not account for that here.
 */
#define XFS_ALLOCBT_AGFL_RESERVE	4

/*
 * Compute the number of blocks that we set aside to guarantee the ability to
 * refill the AGFL and handle a full bmap btree split.
 *
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
 * For each AG, we need to reserve enough blocks to replenish a totally empty
 * AGFL and 4 more to handle a potential split of the file's bmap btree.
 */
unsigned int
xfs_alloc_set_aside(
	struct xfs_mount	*mp)
{
	return mp->m_sb.sb_agcount * (XFS_ALLOCBT_AGFL_RESERVE + 4);
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
	blocks += XFS_ALLOCBT_AGFL_RESERVE;
	blocks += 3;			/* AGF, AGI btree root blocks */
	if (xfs_has_finobt(mp))
		blocks++;		/* finobt root block */
	if (xfs_has_rmapbt(mp))
		blocks++;		/* rmap root block */
	if (xfs_has_reflink(mp))
		blocks++;		/* refcount root block */

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
	int			error;

	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	error = xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
	cur->bc_ag.abt.active = (*stat == 1);
	return error;
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
	int			error;

	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	error = xfs_btree_lookup(cur, XFS_LOOKUP_GE, stat);
	cur->bc_ag.abt.active = (*stat == 1);
	return error;
}

/*
 * Lookup the first record less than or equal to [bno, len]
 * in the btree given by cur.
 */
int					/* error */
xfs_alloc_lookup_le(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agblock_t		bno,	/* starting block of extent */
	xfs_extlen_t		len,	/* length of extent */
	int			*stat)	/* success/failure */
{
	int			error;
	cur->bc_rec.a.ar_startblock = bno;
	cur->bc_rec.a.ar_blockcount = len;
	error = xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
	cur->bc_ag.abt.active = (*stat == 1);
	return error;
}

static inline bool
xfs_alloc_cur_active(
	struct xfs_btree_cur	*cur)
{
	return cur && cur->bc_ag.abt.active;
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
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_perag	*pag = cur->bc_ag.pag;
	union xfs_btree_rec	*rec;
	int			error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || !(*stat))
		return error;

	*bno = be32_to_cpu(rec->alloc.ar_startblock);
	*len = be32_to_cpu(rec->alloc.ar_blockcount);

	if (*len == 0)
		goto out_bad_rec;

	/* check for valid extent range, including overflow */
	if (!xfs_verify_agbext(pag, *bno, *len))
		goto out_bad_rec;

	return 0;

out_bad_rec:
	xfs_warn(mp,
		"%s Freespace BTree record corruption in AG %d detected!",
		cur->bc_btnum == XFS_BTNUM_BNO ? "Block" : "Size",
		pag->pag_agno);
	xfs_warn(mp,
		"start block 0x%x block count 0x%x", *bno, *len);
	return -EFSCORRUPTED;
}

/*
 * Compute aligned version of the found extent.
 * Takes alignment and min length into account.
 */
STATIC bool
xfs_alloc_compute_aligned(
	xfs_alloc_arg_t	*args,		/* allocation argument structure */
	xfs_agblock_t	foundbno,	/* starting block in found extent */
	xfs_extlen_t	foundlen,	/* length in found extent */
	xfs_agblock_t	*resbno,	/* result block number */
	xfs_extlen_t	*reslen,	/* result length */
	unsigned	*busy_gen)
{
	xfs_agblock_t	bno = foundbno;
	xfs_extlen_t	len = foundlen;
	xfs_extlen_t	diff;
	bool		busy;

	/* Trim busy sections out of found extent */
	busy = xfs_extent_busy_trim(args, &bno, &len, busy_gen);

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

	return busy;
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
	bool		userdata = datatype & XFS_ALLOC_USERDATA;

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
	ASSERT(args->pag->pagf_freeblks + args->pag->pagf_flcount >=
		rlen + args->minleft);
	args->len = rlen;
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
	struct xfs_btree_cur *cnt_cur,	/* cursor for by-size btree */
	struct xfs_btree_cur *bno_cur,	/* cursor for by-block btree */
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
		if (XFS_IS_CORRUPT(mp,
				   i != 1 ||
				   nfbno1 != fbno ||
				   nflen1 != flen))
			return -EFSCORRUPTED;
#endif
	} else {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, fbno, flen, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 1))
			return -EFSCORRUPTED;
	}
	/*
	 * Look up the record in the by-block tree if necessary.
	 */
	if (flags & XFSA_FIXUP_BNO_OK) {
#ifdef DEBUG
		if ((error = xfs_alloc_get_rec(bno_cur, &nfbno1, &nflen1, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp,
				   i != 1 ||
				   nfbno1 != fbno ||
				   nflen1 != flen))
			return -EFSCORRUPTED;
#endif
	} else {
		if ((error = xfs_alloc_lookup_eq(bno_cur, fbno, flen, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 1))
			return -EFSCORRUPTED;
	}

#ifdef DEBUG
	if (bno_cur->bc_nlevels == 1 && cnt_cur->bc_nlevels == 1) {
		struct xfs_btree_block	*bnoblock;
		struct xfs_btree_block	*cntblock;

		bnoblock = XFS_BUF_TO_BLOCK(bno_cur->bc_levels[0].bp);
		cntblock = XFS_BUF_TO_BLOCK(cnt_cur->bc_levels[0].bp);

		if (XFS_IS_CORRUPT(mp,
				   bnoblock->bb_numrecs !=
				   cntblock->bb_numrecs))
			return -EFSCORRUPTED;
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
	if (XFS_IS_CORRUPT(mp, i != 1))
		return -EFSCORRUPTED;
	/*
	 * Add new by-size btree entry(s).
	 */
	if (nfbno1 != NULLAGBLOCK) {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, nfbno1, nflen1, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 0))
			return -EFSCORRUPTED;
		if ((error = xfs_btree_insert(cnt_cur, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 1))
			return -EFSCORRUPTED;
	}
	if (nfbno2 != NULLAGBLOCK) {
		if ((error = xfs_alloc_lookup_eq(cnt_cur, nfbno2, nflen2, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 0))
			return -EFSCORRUPTED;
		if ((error = xfs_btree_insert(cnt_cur, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 1))
			return -EFSCORRUPTED;
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
		if (XFS_IS_CORRUPT(mp, i != 1))
			return -EFSCORRUPTED;
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
		if (XFS_IS_CORRUPT(mp, i != 0))
			return -EFSCORRUPTED;
		if ((error = xfs_btree_insert(bno_cur, &i)))
			return error;
		if (XFS_IS_CORRUPT(mp, i != 1))
			return -EFSCORRUPTED;
	}
	return 0;
}

static xfs_failaddr_t
xfs_agfl_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	struct xfs_agfl	*agfl = XFS_BUF_TO_AGFL(bp);
	__be32		*agfl_bno = xfs_buf_to_agfl_bno(bp);
	int		i;

	/*
	 * There is no verification of non-crc AGFLs because mkfs does not
	 * initialise the AGFL to zero or NULL. Hence the only valid part of the
	 * AGFL is what the AGF says is active. We can't get to the AGF, so we
	 * can't verify just those entries are valid.
	 */
	if (!xfs_has_crc(mp))
		return NULL;

	if (!xfs_verify_magic(bp, agfl->agfl_magicnum))
		return __this_address;
	if (!uuid_equal(&agfl->agfl_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agfl->agfl_seqno) != bp->b_pag->pag_agno)
		return __this_address;

	for (i = 0; i < xfs_agfl_size(mp); i++) {
		if (be32_to_cpu(agfl_bno[i]) != NULLAGBLOCK &&
		    be32_to_cpu(agfl_bno[i]) >= mp->m_sb.sb_agblocks)
			return __this_address;
	}

	if (!xfs_log_check_lsn(mp, be64_to_cpu(XFS_BUF_TO_AGFL(bp)->agfl_lsn)))
		return __this_address;
	return NULL;
}

static void
xfs_agfl_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	xfs_failaddr_t	fa;

	/*
	 * There is no verification of non-crc AGFLs because mkfs does not
	 * initialise the AGFL to zero or NULL. Hence the only valid part of the
	 * AGFL is what the AGF says is active. We can't get to the AGF, so we
	 * can't verify just those entries are valid.
	 */
	if (!xfs_has_crc(mp))
		return;

	if (!xfs_buf_verify_cksum(bp, XFS_AGFL_CRC_OFF))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_agfl_verify(bp);
		if (fa)
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}
}

static void
xfs_agfl_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	xfs_failaddr_t		fa;

	/* no verification of non-crc AGFLs */
	if (!xfs_has_crc(mp))
		return;

	fa = xfs_agfl_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (bip)
		XFS_BUF_TO_AGFL(bp)->agfl_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_AGFL_CRC_OFF);
}

const struct xfs_buf_ops xfs_agfl_buf_ops = {
	.name = "xfs_agfl",
	.magic = { cpu_to_be32(XFS_AGFL_MAGIC), cpu_to_be32(XFS_AGFL_MAGIC) },
	.verify_read = xfs_agfl_read_verify,
	.verify_write = xfs_agfl_write_verify,
	.verify_struct = xfs_agfl_verify,
};

/*
 * Read in the allocation group free block array.
 */
int
xfs_alloc_read_agfl(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		**bpp)
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_buf		*bp;
	int			error;

	error = xfs_trans_read_buf(
			mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, pag->pag_agno, XFS_AGFL_DADDR(mp)),
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
	struct xfs_buf		*agbp,
	long			len)
{
	struct xfs_agf		*agf = agbp->b_addr;

	agbp->b_pag->pagf_freeblks += len;
	be32_add_cpu(&agf->agf_freeblks, len);

	if (unlikely(be32_to_cpu(agf->agf_freeblks) >
		     be32_to_cpu(agf->agf_length))) {
		xfs_buf_mark_corrupt(agbp);
		return -EFSCORRUPTED;
	}

	xfs_alloc_log_agf(tp, agbp, XFS_AGF_FREEBLKS);
	return 0;
}

/*
 * Block allocation algorithm and data structures.
 */
struct xfs_alloc_cur {
	struct xfs_btree_cur		*cnt;	/* btree cursors */
	struct xfs_btree_cur		*bnolt;
	struct xfs_btree_cur		*bnogt;
	xfs_extlen_t			cur_len;/* current search length */
	xfs_agblock_t			rec_bno;/* extent startblock */
	xfs_extlen_t			rec_len;/* extent length */
	xfs_agblock_t			bno;	/* alloc bno */
	xfs_extlen_t			len;	/* alloc len */
	xfs_extlen_t			diff;	/* diff from search bno */
	unsigned int			busy_gen;/* busy state */
	bool				busy;
};

/*
 * Set up cursors, etc. in the extent allocation cursor. This function can be
 * called multiple times to reset an initialized structure without having to
 * reallocate cursors.
 */
static int
xfs_alloc_cur_setup(
	struct xfs_alloc_arg	*args,
	struct xfs_alloc_cur	*acur)
{
	int			error;
	int			i;

	ASSERT(args->alignment == 1 || args->type != XFS_ALLOCTYPE_THIS_BNO);

	acur->cur_len = args->maxlen;
	acur->rec_bno = 0;
	acur->rec_len = 0;
	acur->bno = 0;
	acur->len = 0;
	acur->diff = -1;
	acur->busy = false;
	acur->busy_gen = 0;

	/*
	 * Perform an initial cntbt lookup to check for availability of maxlen
	 * extents. If this fails, we'll return -ENOSPC to signal the caller to
	 * attempt a small allocation.
	 */
	if (!acur->cnt)
		acur->cnt = xfs_allocbt_init_cursor(args->mp, args->tp,
					args->agbp, args->pag, XFS_BTNUM_CNT);
	error = xfs_alloc_lookup_ge(acur->cnt, 0, args->maxlen, &i);
	if (error)
		return error;

	/*
	 * Allocate the bnobt left and right search cursors.
	 */
	if (!acur->bnolt)
		acur->bnolt = xfs_allocbt_init_cursor(args->mp, args->tp,
					args->agbp, args->pag, XFS_BTNUM_BNO);
	if (!acur->bnogt)
		acur->bnogt = xfs_allocbt_init_cursor(args->mp, args->tp,
					args->agbp, args->pag, XFS_BTNUM_BNO);
	return i == 1 ? 0 : -ENOSPC;
}

static void
xfs_alloc_cur_close(
	struct xfs_alloc_cur	*acur,
	bool			error)
{
	int			cur_error = XFS_BTREE_NOERROR;

	if (error)
		cur_error = XFS_BTREE_ERROR;

	if (acur->cnt)
		xfs_btree_del_cursor(acur->cnt, cur_error);
	if (acur->bnolt)
		xfs_btree_del_cursor(acur->bnolt, cur_error);
	if (acur->bnogt)
		xfs_btree_del_cursor(acur->bnogt, cur_error);
	acur->cnt = acur->bnolt = acur->bnogt = NULL;
}

/*
 * Check an extent for allocation and track the best available candidate in the
 * allocation structure. The cursor is deactivated if it has entered an out of
 * range state based on allocation arguments. Optionally return the extent
 * extent geometry and allocation status if requested by the caller.
 */
static int
xfs_alloc_cur_check(
	struct xfs_alloc_arg	*args,
	struct xfs_alloc_cur	*acur,
	struct xfs_btree_cur	*cur,
	int			*new)
{
	int			error, i;
	xfs_agblock_t		bno, bnoa, bnew;
	xfs_extlen_t		len, lena, diff = -1;
	bool			busy;
	unsigned		busy_gen = 0;
	bool			deactivate = false;
	bool			isbnobt = cur->bc_btnum == XFS_BTNUM_BNO;

	*new = 0;

	error = xfs_alloc_get_rec(cur, &bno, &len, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(args->mp, i != 1))
		return -EFSCORRUPTED;

	/*
	 * Check minlen and deactivate a cntbt cursor if out of acceptable size
	 * range (i.e., walking backwards looking for a minlen extent).
	 */
	if (len < args->minlen) {
		deactivate = !isbnobt;
		goto out;
	}

	busy = xfs_alloc_compute_aligned(args, bno, len, &bnoa, &lena,
					 &busy_gen);
	acur->busy |= busy;
	if (busy)
		acur->busy_gen = busy_gen;
	/* deactivate a bnobt cursor outside of locality range */
	if (bnoa < args->min_agbno || bnoa > args->max_agbno) {
		deactivate = isbnobt;
		goto out;
	}
	if (lena < args->minlen)
		goto out;

	args->len = XFS_EXTLEN_MIN(lena, args->maxlen);
	xfs_alloc_fix_len(args);
	ASSERT(args->len >= args->minlen);
	if (args->len < acur->len)
		goto out;

	/*
	 * We have an aligned record that satisfies minlen and beats or matches
	 * the candidate extent size. Compare locality for near allocation mode.
	 */
	ASSERT(args->type == XFS_ALLOCTYPE_NEAR_BNO);
	diff = xfs_alloc_compute_diff(args->agbno, args->len,
				      args->alignment, args->datatype,
				      bnoa, lena, &bnew);
	if (bnew == NULLAGBLOCK)
		goto out;

	/*
	 * Deactivate a bnobt cursor with worse locality than the current best.
	 */
	if (diff > acur->diff) {
		deactivate = isbnobt;
		goto out;
	}

	ASSERT(args->len > acur->len ||
	       (args->len == acur->len && diff <= acur->diff));
	acur->rec_bno = bno;
	acur->rec_len = len;
	acur->bno = bnew;
	acur->len = args->len;
	acur->diff = diff;
	*new = 1;

	/*
	 * We're done if we found a perfect allocation. This only deactivates
	 * the current cursor, but this is just an optimization to terminate a
	 * cntbt search that otherwise runs to the edge of the tree.
	 */
	if (acur->diff == 0 && acur->len == args->maxlen)
		deactivate = true;
out:
	if (deactivate)
		cur->bc_ag.abt.active = false;
	trace_xfs_alloc_cur_check(args->mp, cur->bc_btnum, bno, len, diff,
				  *new);
	return 0;
}

/*
 * Complete an allocation of a candidate extent. Remove the extent from both
 * trees and update the args structure.
 */
STATIC int
xfs_alloc_cur_finish(
	struct xfs_alloc_arg	*args,
	struct xfs_alloc_cur	*acur)
{
	struct xfs_agf __maybe_unused *agf = args->agbp->b_addr;
	int			error;

	ASSERT(acur->cnt && acur->bnolt);
	ASSERT(acur->bno >= acur->rec_bno);
	ASSERT(acur->bno + acur->len <= acur->rec_bno + acur->rec_len);
	ASSERT(acur->rec_bno + acur->rec_len <= be32_to_cpu(agf->agf_length));

	error = xfs_alloc_fixup_trees(acur->cnt, acur->bnolt, acur->rec_bno,
				      acur->rec_len, acur->bno, acur->len, 0);
	if (error)
		return error;

	args->agbno = acur->bno;
	args->len = acur->len;
	args->wasfromfl = 0;

	trace_xfs_alloc_cur(args);
	return 0;
}

/*
 * Locality allocation lookup algorithm. This expects a cntbt cursor and uses
 * bno optimized lookup to search for extents with ideal size and locality.
 */
STATIC int
xfs_alloc_cntbt_iter(
	struct xfs_alloc_arg		*args,
	struct xfs_alloc_cur		*acur)
{
	struct xfs_btree_cur	*cur = acur->cnt;
	xfs_agblock_t		bno;
	xfs_extlen_t		len, cur_len;
	int			error;
	int			i;

	if (!xfs_alloc_cur_active(cur))
		return 0;

	/* locality optimized lookup */
	cur_len = acur->cur_len;
	error = xfs_alloc_lookup_ge(cur, args->agbno, cur_len, &i);
	if (error)
		return error;
	if (i == 0)
		return 0;
	error = xfs_alloc_get_rec(cur, &bno, &len, &i);
	if (error)
		return error;

	/* check the current record and update search length from it */
	error = xfs_alloc_cur_check(args, acur, cur, &i);
	if (error)
		return error;
	ASSERT(len >= acur->cur_len);
	acur->cur_len = len;

	/*
	 * We looked up the first record >= [agbno, len] above. The agbno is a
	 * secondary key and so the current record may lie just before or after
	 * agbno. If it is past agbno, check the previous record too so long as
	 * the length matches as it may be closer. Don't check a smaller record
	 * because that could deactivate our cursor.
	 */
	if (bno > args->agbno) {
		error = xfs_btree_decrement(cur, 0, &i);
		if (!error && i) {
			error = xfs_alloc_get_rec(cur, &bno, &len, &i);
			if (!error && i && len == acur->cur_len)
				error = xfs_alloc_cur_check(args, acur, cur,
							    &i);
		}
		if (error)
			return error;
	}

	/*
	 * Increment the search key until we find at least one allocation
	 * candidate or if the extent we found was larger. Otherwise, double the
	 * search key to optimize the search. Efficiency is more important here
	 * than absolute best locality.
	 */
	cur_len <<= 1;
	if (!acur->len || acur->cur_len >= cur_len)
		acur->cur_len++;
	else
		acur->cur_len = cur_len;

	return error;
}

/*
 * Deal with the case where only small freespaces remain. Either return the
 * contents of the last freespace record, or allocate space from the freelist if
 * there is nothing in the tree.
 */
STATIC int			/* error */
xfs_alloc_ag_vextent_small(
	struct xfs_alloc_arg	*args,	/* allocation argument structure */
	struct xfs_btree_cur	*ccur,	/* optional by-size cursor */
	xfs_agblock_t		*fbnop,	/* result block number */
	xfs_extlen_t		*flenp,	/* result length */
	int			*stat)	/* status: 0-freelist, 1-normal/none */
{
	struct xfs_agf		*agf = args->agbp->b_addr;
	int			error = 0;
	xfs_agblock_t		fbno = NULLAGBLOCK;
	xfs_extlen_t		flen = 0;
	int			i = 0;

	/*
	 * If a cntbt cursor is provided, try to allocate the largest record in
	 * the tree. Try the AGFL if the cntbt is empty, otherwise fail the
	 * allocation. Make sure to respect minleft even when pulling from the
	 * freelist.
	 */
	if (ccur)
		error = xfs_btree_decrement(ccur, 0, &i);
	if (error)
		goto error;
	if (i) {
		error = xfs_alloc_get_rec(ccur, &fbno, &flen, &i);
		if (error)
			goto error;
		if (XFS_IS_CORRUPT(args->mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}
		goto out;
	}

	if (args->minlen != 1 || args->alignment != 1 ||
	    args->resv == XFS_AG_RESV_AGFL ||
	    be32_to_cpu(agf->agf_flcount) <= args->minleft)
		goto out;

	error = xfs_alloc_get_freelist(args->pag, args->tp, args->agbp,
			&fbno, 0);
	if (error)
		goto error;
	if (fbno == NULLAGBLOCK)
		goto out;

	xfs_extent_busy_reuse(args->mp, args->pag, fbno, 1,
			      (args->datatype & XFS_ALLOC_NOBUSY));

	if (args->datatype & XFS_ALLOC_USERDATA) {
		struct xfs_buf	*bp;

		error = xfs_trans_get_buf(args->tp, args->mp->m_ddev_targp,
				XFS_AGB_TO_DADDR(args->mp, args->agno, fbno),
				args->mp->m_bsize, 0, &bp);
		if (error)
			goto error;
		xfs_trans_binval(args->tp, bp);
	}
	*fbnop = args->agbno = fbno;
	*flenp = args->len = 1;
	if (XFS_IS_CORRUPT(args->mp, fbno >= be32_to_cpu(agf->agf_length))) {
		error = -EFSCORRUPTED;
		goto error;
	}
	args->wasfromfl = 1;
	trace_xfs_alloc_small_freelist(args);

	/*
	 * If we're feeding an AGFL block to something that doesn't live in the
	 * free space, we need to clear out the OWN_AG rmap.
	 */
	error = xfs_rmap_free(args->tp, args->agbp, args->pag, fbno, 1,
			      &XFS_RMAP_OINFO_AG);
	if (error)
		goto error;

	*stat = 0;
	return 0;

out:
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

error:
	trace_xfs_alloc_small_error(args);
	return error;
}

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

	if (error || args->agbno == NULLAGBLOCK)
		return error;

	ASSERT(args->len >= args->minlen);
	ASSERT(args->len <= args->maxlen);
	ASSERT(!args->wasfromfl || args->resv != XFS_AG_RESV_AGFL);
	ASSERT(args->agbno % args->alignment == 0);

	/* if not file data, insert new block into the reverse map btree */
	if (!xfs_rmap_should_skip_owner_update(&args->oinfo)) {
		error = xfs_rmap_alloc(args->tp, args->agbp, args->pag,
				       args->agbno, args->len, &args->oinfo);
		if (error)
			return error;
	}

	if (!args->wasfromfl) {
		error = xfs_alloc_update_counters(args->tp, args->agbp,
						  -((long)(args->len)));
		if (error)
			return error;

		ASSERT(!xfs_extent_busy_search(args->mp, args->pag,
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
	struct xfs_agf __maybe_unused *agf = args->agbp->b_addr;
	struct xfs_btree_cur *bno_cur;/* by block-number btree cursor */
	struct xfs_btree_cur *cnt_cur;/* by count btree cursor */
	int		error;
	xfs_agblock_t	fbno;	/* start block of found extent */
	xfs_extlen_t	flen;	/* length of found extent */
	xfs_agblock_t	tbno;	/* start block of busy extent */
	xfs_extlen_t	tlen;	/* length of busy extent */
	xfs_agblock_t	tend;	/* end block of busy extent */
	int		i;	/* success/failure of operation */
	unsigned	busy_gen;

	ASSERT(args->alignment == 1);

	/*
	 * Allocate/initialize a cursor for the by-number freespace btree.
	 */
	bno_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
					  args->pag, XFS_BTNUM_BNO);

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
	if (XFS_IS_CORRUPT(args->mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	ASSERT(fbno <= args->agbno);

	/*
	 * Check for overlapping busy extents.
	 */
	tbno = fbno;
	tlen = flen;
	xfs_extent_busy_trim(args, &tbno, &tlen, &busy_gen);

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
	ASSERT(args->agbno + args->len <= tend);

	/*
	 * We are allocating agbno for args->len
	 * Allocate/initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
					args->pag, XFS_BTNUM_CNT);
	ASSERT(args->agbno + args->len <= be32_to_cpu(agf->agf_length));
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
 * Search a given number of btree records in a given direction. Check each
 * record against the good extent we've already found.
 */
STATIC int
xfs_alloc_walk_iter(
	struct xfs_alloc_arg	*args,
	struct xfs_alloc_cur	*acur,
	struct xfs_btree_cur	*cur,
	bool			increment,
	bool			find_one, /* quit on first candidate */
	int			count,    /* rec count (-1 for infinite) */
	int			*stat)
{
	int			error;
	int			i;

	*stat = 0;

	/*
	 * Search so long as the cursor is active or we find a better extent.
	 * The cursor is deactivated if it extends beyond the range of the
	 * current allocation candidate.
	 */
	while (xfs_alloc_cur_active(cur) && count) {
		error = xfs_alloc_cur_check(args, acur, cur, &i);
		if (error)
			return error;
		if (i == 1) {
			*stat = 1;
			if (find_one)
				break;
		}
		if (!xfs_alloc_cur_active(cur))
			break;

		if (increment)
			error = xfs_btree_increment(cur, 0, &i);
		else
			error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			return error;
		if (i == 0)
			cur->bc_ag.abt.active = false;

		if (count > 0)
			count--;
	}

	return 0;
}

/*
 * Search the by-bno and by-size btrees in parallel in search of an extent with
 * ideal locality based on the NEAR mode ->agbno locality hint.
 */
STATIC int
xfs_alloc_ag_vextent_locality(
	struct xfs_alloc_arg	*args,
	struct xfs_alloc_cur	*acur,
	int			*stat)
{
	struct xfs_btree_cur	*fbcur = NULL;
	int			error;
	int			i;
	bool			fbinc;

	ASSERT(acur->len == 0);
	ASSERT(args->type == XFS_ALLOCTYPE_NEAR_BNO);

	*stat = 0;

	error = xfs_alloc_lookup_ge(acur->cnt, args->agbno, acur->cur_len, &i);
	if (error)
		return error;
	error = xfs_alloc_lookup_le(acur->bnolt, args->agbno, 0, &i);
	if (error)
		return error;
	error = xfs_alloc_lookup_ge(acur->bnogt, args->agbno, 0, &i);
	if (error)
		return error;

	/*
	 * Search the bnobt and cntbt in parallel. Search the bnobt left and
	 * right and lookup the closest extent to the locality hint for each
	 * extent size key in the cntbt. The entire search terminates
	 * immediately on a bnobt hit because that means we've found best case
	 * locality. Otherwise the search continues until the cntbt cursor runs
	 * off the end of the tree. If no allocation candidate is found at this
	 * point, give up on locality, walk backwards from the end of the cntbt
	 * and take the first available extent.
	 *
	 * The parallel tree searches balance each other out to provide fairly
	 * consistent performance for various situations. The bnobt search can
	 * have pathological behavior in the worst case scenario of larger
	 * allocation requests and fragmented free space. On the other hand, the
	 * bnobt is able to satisfy most smaller allocation requests much more
	 * quickly than the cntbt. The cntbt search can sift through fragmented
	 * free space and sets of free extents for larger allocation requests
	 * more quickly than the bnobt. Since the locality hint is just a hint
	 * and we don't want to scan the entire bnobt for perfect locality, the
	 * cntbt search essentially bounds the bnobt search such that we can
	 * find good enough locality at reasonable performance in most cases.
	 */
	while (xfs_alloc_cur_active(acur->bnolt) ||
	       xfs_alloc_cur_active(acur->bnogt) ||
	       xfs_alloc_cur_active(acur->cnt)) {

		trace_xfs_alloc_cur_lookup(args);

		/*
		 * Search the bnobt left and right. In the case of a hit, finish
		 * the search in the opposite direction and we're done.
		 */
		error = xfs_alloc_walk_iter(args, acur, acur->bnolt, false,
					    true, 1, &i);
		if (error)
			return error;
		if (i == 1) {
			trace_xfs_alloc_cur_left(args);
			fbcur = acur->bnogt;
			fbinc = true;
			break;
		}
		error = xfs_alloc_walk_iter(args, acur, acur->bnogt, true, true,
					    1, &i);
		if (error)
			return error;
		if (i == 1) {
			trace_xfs_alloc_cur_right(args);
			fbcur = acur->bnolt;
			fbinc = false;
			break;
		}

		/*
		 * Check the extent with best locality based on the current
		 * extent size search key and keep track of the best candidate.
		 */
		error = xfs_alloc_cntbt_iter(args, acur);
		if (error)
			return error;
		if (!xfs_alloc_cur_active(acur->cnt)) {
			trace_xfs_alloc_cur_lookup_done(args);
			break;
		}
	}

	/*
	 * If we failed to find anything due to busy extents, return empty
	 * handed so the caller can flush and retry. If no busy extents were
	 * found, walk backwards from the end of the cntbt as a last resort.
	 */
	if (!xfs_alloc_cur_active(acur->cnt) && !acur->len && !acur->busy) {
		error = xfs_btree_decrement(acur->cnt, 0, &i);
		if (error)
			return error;
		if (i) {
			acur->cnt->bc_ag.abt.active = true;
			fbcur = acur->cnt;
			fbinc = false;
		}
	}

	/*
	 * Search in the opposite direction for a better entry in the case of
	 * a bnobt hit or walk backwards from the end of the cntbt.
	 */
	if (fbcur) {
		error = xfs_alloc_walk_iter(args, acur, fbcur, fbinc, true, -1,
					    &i);
		if (error)
			return error;
	}

	if (acur->len)
		*stat = 1;

	return 0;
}

/* Check the last block of the cnt btree for allocations. */
static int
xfs_alloc_ag_vextent_lastblock(
	struct xfs_alloc_arg	*args,
	struct xfs_alloc_cur	*acur,
	xfs_agblock_t		*bno,
	xfs_extlen_t		*len,
	bool			*allocated)
{
	int			error;
	int			i;

#ifdef DEBUG
	/* Randomly don't execute the first algorithm. */
	if (prandom_u32_max(2))
		return 0;
#endif

	/*
	 * Start from the entry that lookup found, sequence through all larger
	 * free blocks.  If we're actually pointing at a record smaller than
	 * maxlen, go to the start of this block, and skip all those smaller
	 * than minlen.
	 */
	if (*len || args->alignment > 1) {
		acur->cnt->bc_levels[0].ptr = 1;
		do {
			error = xfs_alloc_get_rec(acur->cnt, bno, len, &i);
			if (error)
				return error;
			if (XFS_IS_CORRUPT(args->mp, i != 1))
				return -EFSCORRUPTED;
			if (*len >= args->minlen)
				break;
			error = xfs_btree_increment(acur->cnt, 0, &i);
			if (error)
				return error;
		} while (i);
		ASSERT(*len >= args->minlen);
		if (!i)
			return 0;
	}

	error = xfs_alloc_walk_iter(args, acur, acur->cnt, true, false, -1, &i);
	if (error)
		return error;

	/*
	 * It didn't work.  We COULD be in a case where there's a good record
	 * somewhere, so try again.
	 */
	if (acur->len == 0)
		return 0;

	trace_xfs_alloc_near_first(args);
	*allocated = true;
	return 0;
}

/*
 * Allocate a variable extent near bno in the allocation group agno.
 * Extent's length (returned in len) will be between minlen and maxlen,
 * and of the form k * prod + mod unless there's nothing that large.
 * Return the starting a.g. block, or NULLAGBLOCK if we can't do it.
 */
STATIC int
xfs_alloc_ag_vextent_near(
	struct xfs_alloc_arg	*args)
{
	struct xfs_alloc_cur	acur = {};
	int			error;		/* error code */
	int			i;		/* result code, temporary */
	xfs_agblock_t		bno;
	xfs_extlen_t		len;

	/* handle uninitialized agbno range so caller doesn't have to */
	if (!args->min_agbno && !args->max_agbno)
		args->max_agbno = args->mp->m_sb.sb_agblocks - 1;
	ASSERT(args->min_agbno <= args->max_agbno);

	/* clamp agbno to the range if it's outside */
	if (args->agbno < args->min_agbno)
		args->agbno = args->min_agbno;
	if (args->agbno > args->max_agbno)
		args->agbno = args->max_agbno;

restart:
	len = 0;

	/*
	 * Set up cursors and see if there are any free extents as big as
	 * maxlen. If not, pick the last entry in the tree unless the tree is
	 * empty.
	 */
	error = xfs_alloc_cur_setup(args, &acur);
	if (error == -ENOSPC) {
		error = xfs_alloc_ag_vextent_small(args, acur.cnt, &bno,
				&len, &i);
		if (error)
			goto out;
		if (i == 0 || len == 0) {
			trace_xfs_alloc_near_noentry(args);
			goto out;
		}
		ASSERT(i == 1);
	} else if (error) {
		goto out;
	}

	/*
	 * First algorithm.
	 * If the requested extent is large wrt the freespaces available
	 * in this a.g., then the cursor will be pointing to a btree entry
	 * near the right edge of the tree.  If it's in the last btree leaf
	 * block, then we just examine all the entries in that block
	 * that are big enough, and pick the best one.
	 */
	if (xfs_btree_islastblock(acur.cnt, 0)) {
		bool		allocated = false;

		error = xfs_alloc_ag_vextent_lastblock(args, &acur, &bno, &len,
				&allocated);
		if (error)
			goto out;
		if (allocated)
			goto alloc_finish;
	}

	/*
	 * Second algorithm. Combined cntbt and bnobt search to find ideal
	 * locality.
	 */
	error = xfs_alloc_ag_vextent_locality(args, &acur, &i);
	if (error)
		goto out;

	/*
	 * If we couldn't get anything, give up.
	 */
	if (!acur.len) {
		if (acur.busy) {
			trace_xfs_alloc_near_busy(args);
			xfs_extent_busy_flush(args->mp, args->pag,
					      acur.busy_gen);
			goto restart;
		}
		trace_xfs_alloc_size_neither(args);
		args->agbno = NULLAGBLOCK;
		goto out;
	}

alloc_finish:
	/* fix up btrees on a successful allocation */
	error = xfs_alloc_cur_finish(args, &acur);

out:
	xfs_alloc_cur_close(&acur, error);
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
	struct xfs_agf	*agf = args->agbp->b_addr;
	struct xfs_btree_cur *bno_cur;	/* cursor for bno btree */
	struct xfs_btree_cur *cnt_cur;	/* cursor for cnt btree */
	int		error;		/* error result */
	xfs_agblock_t	fbno;		/* start of found freespace */
	xfs_extlen_t	flen;		/* length of found freespace */
	int		i;		/* temp status variable */
	xfs_agblock_t	rbno;		/* returned block number */
	xfs_extlen_t	rlen;		/* length of returned extent */
	bool		busy;
	unsigned	busy_gen;

restart:
	/*
	 * Allocate and initialize a cursor for the by-size btree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
					args->pag, XFS_BTNUM_CNT);
	bno_cur = NULL;

	/*
	 * Look for an entry >= maxlen+alignment-1 blocks.
	 */
	if ((error = xfs_alloc_lookup_ge(cnt_cur, 0,
			args->maxlen + args->alignment - 1, &i)))
		goto error0;

	/*
	 * If none then we have to settle for a smaller extent. In the case that
	 * there are no large extents, this will return the last entry in the
	 * tree unless the tree is empty. In the case that there are only busy
	 * large extents, this will return the largest small extent unless there
	 * are no smaller extents available.
	 */
	if (!i) {
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
		busy = xfs_alloc_compute_aligned(args, fbno, flen, &rbno,
				&rlen, &busy_gen);
	} else {
		/*
		 * Search for a non-busy extent that is large enough.
		 */
		for (;;) {
			error = xfs_alloc_get_rec(cnt_cur, &fbno, &flen, &i);
			if (error)
				goto error0;
			if (XFS_IS_CORRUPT(args->mp, i != 1)) {
				error = -EFSCORRUPTED;
				goto error0;
			}

			busy = xfs_alloc_compute_aligned(args, fbno, flen,
					&rbno, &rlen, &busy_gen);

			if (rlen >= args->maxlen)
				break;

			error = xfs_btree_increment(cnt_cur, 0, &i);
			if (error)
				goto error0;
			if (i == 0) {
				/*
				 * Our only valid extents must have been busy.
				 * Make it unbusy by forcing the log out and
				 * retrying.
				 */
				xfs_btree_del_cursor(cnt_cur,
						     XFS_BTREE_NOERROR);
				trace_xfs_alloc_size_busy(args);
				xfs_extent_busy_flush(args->mp,
							args->pag, busy_gen);
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
	if (XFS_IS_CORRUPT(args->mp,
			   rlen != 0 &&
			   (rlen > flen ||
			    rbno + rlen > fbno + flen))) {
		error = -EFSCORRUPTED;
		goto error0;
	}
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
			if (XFS_IS_CORRUPT(args->mp, i != 1)) {
				error = -EFSCORRUPTED;
				goto error0;
			}
			if (flen < bestrlen)
				break;
			busy = xfs_alloc_compute_aligned(args, fbno, flen,
					&rbno, &rlen, &busy_gen);
			rlen = XFS_EXTLEN_MIN(args->maxlen, rlen);
			if (XFS_IS_CORRUPT(args->mp,
					   rlen != 0 &&
					   (rlen > flen ||
					    rbno + rlen > fbno + flen))) {
				error = -EFSCORRUPTED;
				goto error0;
			}
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
		if (XFS_IS_CORRUPT(args->mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
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
		if (busy) {
			xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
			trace_xfs_alloc_size_busy(args);
			xfs_extent_busy_flush(args->mp, args->pag, busy_gen);
			goto restart;
		}
		goto out_nominleft;
	}
	xfs_alloc_fix_len(args);

	rlen = args->len;
	if (XFS_IS_CORRUPT(args->mp, rlen > flen)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	/*
	 * Allocate and initialize a cursor for the by-block tree.
	 */
	bno_cur = xfs_allocbt_init_cursor(args->mp, args->tp, args->agbp,
					args->pag, XFS_BTNUM_BNO);
	if ((error = xfs_alloc_fixup_trees(cnt_cur, bno_cur, fbno, flen,
			rbno, rlen, XFSA_FIXUP_CNT_OK)))
		goto error0;
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	cnt_cur = bno_cur = NULL;
	args->len = rlen;
	args->agbno = rbno;
	if (XFS_IS_CORRUPT(args->mp,
			   args->agbno + args->len >
			   be32_to_cpu(agf->agf_length))) {
		error = -EFSCORRUPTED;
		goto error0;
	}
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
 * Free the extent starting at agno/bno for length.
 */
STATIC int
xfs_free_ag_extent(
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agnumber_t			agno,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	enum xfs_ag_resv_type		type)
{
	struct xfs_mount		*mp;
	struct xfs_btree_cur		*bno_cur;
	struct xfs_btree_cur		*cnt_cur;
	xfs_agblock_t			gtbno; /* start of right neighbor */
	xfs_extlen_t			gtlen; /* length of right neighbor */
	xfs_agblock_t			ltbno; /* start of left neighbor */
	xfs_extlen_t			ltlen; /* length of left neighbor */
	xfs_agblock_t			nbno; /* new starting block of freesp */
	xfs_extlen_t			nlen; /* new length of freespace */
	int				haveleft; /* have a left neighbor */
	int				haveright; /* have a right neighbor */
	int				i;
	int				error;
	struct xfs_perag		*pag = agbp->b_pag;

	bno_cur = cnt_cur = NULL;
	mp = tp->t_mountp;

	if (!xfs_rmap_should_skip_owner_update(oinfo)) {
		error = xfs_rmap_free(tp, agbp, pag, bno, len, oinfo);
		if (error)
			goto error0;
	}

	/*
	 * Allocate and initialize a cursor for the by-block btree.
	 */
	bno_cur = xfs_allocbt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_BNO);
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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
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
			if (XFS_IS_CORRUPT(mp, ltbno + ltlen > bno)) {
				error = -EFSCORRUPTED;
				goto error0;
			}
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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
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
			if (XFS_IS_CORRUPT(mp, bno + len > gtbno)) {
				error = -EFSCORRUPTED;
				goto error0;
			}
		}
	}
	/*
	 * Now allocate and initialize a cursor for the by-size tree.
	 */
	cnt_cur = xfs_allocbt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_CNT);
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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		/*
		 * Delete the old by-size entry on the right.
		 */
		if ((error = xfs_alloc_lookup_eq(cnt_cur, gtbno, gtlen, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		/*
		 * Delete the old by-block entry for the right block.
		 */
		if ((error = xfs_btree_delete(bno_cur, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		/*
		 * Move the by-block cursor back to the left neighbor.
		 */
		if ((error = xfs_btree_decrement(bno_cur, 0, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
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
			if (XFS_IS_CORRUPT(mp,
					   i != 1 ||
					   xxbno != ltbno ||
					   xxlen != ltlen)) {
				error = -EFSCORRUPTED;
				goto error0;
			}
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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		/*
		 * Back up the by-block cursor to the left neighbor, and
		 * update its length.
		 */
		if ((error = xfs_btree_decrement(bno_cur, 0, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		if ((error = xfs_btree_delete(cnt_cur, &i)))
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
	}
	xfs_btree_del_cursor(bno_cur, XFS_BTREE_NOERROR);
	bno_cur = NULL;
	/*
	 * In all cases we need to insert the new freespace in the by-size tree.
	 */
	if ((error = xfs_alloc_lookup_eq(cnt_cur, nbno, nlen, &i)))
		goto error0;
	if (XFS_IS_CORRUPT(mp, i != 0)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	if ((error = xfs_btree_insert(cnt_cur, &i)))
		goto error0;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	xfs_btree_del_cursor(cnt_cur, XFS_BTREE_NOERROR);
	cnt_cur = NULL;

	/*
	 * Update the freespace totals in the ag and superblock.
	 */
	error = xfs_alloc_update_counters(tp, agbp, len);
	xfs_ag_resv_free_extent(agbp->b_pag, type, tp, len);
	if (error)
		goto error0;

	XFS_STATS_INC(mp, xs_freex);
	XFS_STATS_ADD(mp, xs_freeb, len);

	trace_xfs_free_extent(mp, agno, bno, len, type, haveleft, haveright);

	return 0;

 error0:
	trace_xfs_free_extent(mp, agno, bno, len, type, -1, -1);
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
 * Compute and fill in value of m_alloc_maxlevels.
 */
void
xfs_alloc_compute_maxlevels(
	xfs_mount_t	*mp)	/* file system mount structure */
{
	mp->m_alloc_maxlevels = xfs_btree_compute_maxlevels(mp->m_alloc_mnr,
			(mp->m_sb.sb_agblocks + 1) / 2);
	ASSERT(mp->m_alloc_maxlevels <= xfs_allocbt_maxlevels_ondisk());
}

/*
 * Find the length of the longest extent in an AG.  The 'need' parameter
 * specifies how much space we're going to need for the AGFL and the
 * 'reserved' parameter tells us how many blocks in this AG are reserved for
 * other callers.
 */
xfs_extlen_t
xfs_alloc_longest_free_extent(
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
		return min_t(xfs_extlen_t, pag->pag_mount->m_ag_max_usable,
				pag->pagf_longest - delta);

	/* Otherwise, let the caller try for 1 block if there's space. */
	return pag->pagf_flcount > 0 || pag->pagf_longest > 0;
}

/*
 * Compute the minimum length of the AGFL in the given AG.  If @pag is NULL,
 * return the largest possible minimum length.
 */
unsigned int
xfs_alloc_min_freelist(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag)
{
	/* AG btrees have at least 1 level. */
	static const uint8_t	fake_levels[XFS_BTNUM_AGF] = {1, 1, 1};
	const uint8_t		*levels = pag ? pag->pagf_levels : fake_levels;
	unsigned int		min_free;

	ASSERT(mp->m_alloc_maxlevels > 0);

	/* space needed by-bno freespace btree */
	min_free = min_t(unsigned int, levels[XFS_BTNUM_BNOi] + 1,
				       mp->m_alloc_maxlevels);
	/* space needed by-size freespace btree */
	min_free += min_t(unsigned int, levels[XFS_BTNUM_CNTi] + 1,
				       mp->m_alloc_maxlevels);
	/* space needed reverse mapping used space btree */
	if (xfs_has_rmapbt(mp))
		min_free += min_t(unsigned int, levels[XFS_BTNUM_RMAPi] + 1,
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
	xfs_extlen_t		alloc_len, longest;
	xfs_extlen_t		reservation; /* blocks that are still reserved */
	int			available;
	xfs_extlen_t		agflcount;

	if (flags & XFS_ALLOC_FLAG_FREEING)
		return true;

	reservation = xfs_ag_resv_needed(pag, args->resv);

	/* do we have enough contiguous free space for the allocation? */
	alloc_len = args->minlen + (args->alignment - 1) + args->minalignslop;
	longest = xfs_alloc_longest_free_extent(pag, min_free, reservation);
	if (longest < alloc_len)
		return false;

	/*
	 * Do we have enough free space remaining for the allocation? Don't
	 * account extra agfl blocks because we are about to defer free them,
	 * making them unavailable until the current transaction commits.
	 */
	agflcount = min_t(xfs_extlen_t, pag->pagf_flcount, min_free);
	available = (int)(pag->pagf_freeblks + agflcount -
			  reservation - min_free - args->minleft);
	if (available < (int)max(args->total, alloc_len))
		return false;

	/*
	 * Clamp maxlen to the amount of free space available for the actual
	 * extent allocation.
	 */
	if (available < (int)args->maxlen && !(flags & XFS_ALLOC_FLAG_CHECK)) {
		args->maxlen = available;
		ASSERT(args->maxlen > 0);
		ASSERT(args->maxlen >= args->minlen);
	}

	return true;
}

int
xfs_free_agfl_block(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	struct xfs_buf		*agbp,
	struct xfs_owner_info	*oinfo)
{
	int			error;
	struct xfs_buf		*bp;

	error = xfs_free_ag_extent(tp, agbp, agno, agbno, 1, oinfo,
				   XFS_AG_RESV_AGFL);
	if (error)
		return error;

	error = xfs_trans_get_buf(tp, tp->t_mountp->m_ddev_targp,
			XFS_AGB_TO_DADDR(tp->t_mountp, agno, agbno),
			tp->t_mountp->m_bsize, 0, &bp);
	if (error)
		return error;
	xfs_trans_binval(tp, bp);

	return 0;
}

/*
 * Check the agfl fields of the agf for inconsistency or corruption. The purpose
 * is to detect an agfl header padding mismatch between current and early v5
 * kernels. This problem manifests as a 1-slot size difference between the
 * on-disk flcount and the active [first, last] range of a wrapped agfl. This
 * may also catch variants of agfl count corruption unrelated to padding. Either
 * way, we'll reset the agfl and warn the user.
 *
 * Return true if a reset is required before the agfl can be used, false
 * otherwise.
 */
static bool
xfs_agfl_needs_reset(
	struct xfs_mount	*mp,
	struct xfs_agf		*agf)
{
	uint32_t		f = be32_to_cpu(agf->agf_flfirst);
	uint32_t		l = be32_to_cpu(agf->agf_fllast);
	uint32_t		c = be32_to_cpu(agf->agf_flcount);
	int			agfl_size = xfs_agfl_size(mp);
	int			active;

	/* no agfl header on v4 supers */
	if (!xfs_has_crc(mp))
		return false;

	/*
	 * The agf read verifier catches severe corruption of these fields.
	 * Repeat some sanity checks to cover a packed -> unpacked mismatch if
	 * the verifier allows it.
	 */
	if (f >= agfl_size || l >= agfl_size)
		return true;
	if (c > agfl_size)
		return true;

	/*
	 * Check consistency between the on-disk count and the active range. An
	 * agfl padding mismatch manifests as an inconsistent flcount.
	 */
	if (c && l >= f)
		active = l - f + 1;
	else if (c)
		active = agfl_size - f + l + 1;
	else
		active = 0;

	return active != c;
}

/*
 * Reset the agfl to an empty state. Ignore/drop any existing blocks since the
 * agfl content cannot be trusted. Warn the user that a repair is required to
 * recover leaked blocks.
 *
 * The purpose of this mechanism is to handle filesystems affected by the agfl
 * header padding mismatch problem. A reset keeps the filesystem online with a
 * relatively minor free space accounting inconsistency rather than suffer the
 * inevitable crash from use of an invalid agfl block.
 */
static void
xfs_agfl_reset(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agf		*agf = agbp->b_addr;

	ASSERT(pag->pagf_agflreset);
	trace_xfs_agfl_reset(mp, agf, 0, _RET_IP_);

	xfs_warn(mp,
	       "WARNING: Reset corrupted AGFL on AG %u. %d blocks leaked. "
	       "Please unmount and run xfs_repair.",
	         pag->pag_agno, pag->pagf_flcount);

	agf->agf_flfirst = 0;
	agf->agf_fllast = cpu_to_be32(xfs_agfl_size(mp) - 1);
	agf->agf_flcount = 0;
	xfs_alloc_log_agf(tp, agbp, XFS_AGF_FLFIRST | XFS_AGF_FLLAST |
				    XFS_AGF_FLCOUNT);

	pag->pagf_flcount = 0;
	pag->pagf_agflreset = false;
}

/*
 * Defer an AGFL block free. This is effectively equivalent to
 * xfs_free_extent_later() with some special handling particular to AGFL blocks.
 *
 * Deferring AGFL frees helps prevent log reservation overruns due to too many
 * allocation operations in a transaction. AGFL frees are prone to this problem
 * because for one they are always freed one at a time. Further, an immediate
 * AGFL block free can cause a btree join and require another block free before
 * the real allocation can proceed. Deferring the free disconnects freeing up
 * the AGFL slot from freeing the block.
 */
STATIC void
xfs_defer_agfl_block(
	struct xfs_trans		*tp,
	xfs_agnumber_t			agno,
	xfs_fsblock_t			agbno,
	struct xfs_owner_info		*oinfo)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_extent_free_item	*new;		/* new element */

	ASSERT(xfs_extfree_item_cache != NULL);
	ASSERT(oinfo != NULL);

	new = kmem_cache_zalloc(xfs_extfree_item_cache,
			       GFP_KERNEL | __GFP_NOFAIL);
	new->xefi_startblock = XFS_AGB_TO_FSB(mp, agno, agbno);
	new->xefi_blockcount = 1;
	new->xefi_owner = oinfo->oi_owner;

	trace_xfs_agfl_free_defer(mp, agno, 0, agbno, 1);

	xfs_defer_add(tp, XFS_DEFER_OPS_TYPE_AGFL_FREE, &new->xefi_list);
}

/*
 * Add the extent to the list of extents to be free at transaction end.
 * The list is maintained sorted (by block number).
 */
void
__xfs_free_extent_later(
	struct xfs_trans		*tp,
	xfs_fsblock_t			bno,
	xfs_filblks_t			len,
	const struct xfs_owner_info	*oinfo,
	bool				skip_discard)
{
	struct xfs_extent_free_item	*new;		/* new element */
#ifdef DEBUG
	struct xfs_mount		*mp = tp->t_mountp;
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;

	ASSERT(bno != NULLFSBLOCK);
	ASSERT(len > 0);
	ASSERT(len <= XFS_MAX_BMBT_EXTLEN);
	ASSERT(!isnullstartblock(bno));
	agno = XFS_FSB_TO_AGNO(mp, bno);
	agbno = XFS_FSB_TO_AGBNO(mp, bno);
	ASSERT(agno < mp->m_sb.sb_agcount);
	ASSERT(agbno < mp->m_sb.sb_agblocks);
	ASSERT(len < mp->m_sb.sb_agblocks);
	ASSERT(agbno + len <= mp->m_sb.sb_agblocks);
#endif
	ASSERT(xfs_extfree_item_cache != NULL);

	new = kmem_cache_zalloc(xfs_extfree_item_cache,
			       GFP_KERNEL | __GFP_NOFAIL);
	new->xefi_startblock = bno;
	new->xefi_blockcount = (xfs_extlen_t)len;
	if (skip_discard)
		new->xefi_flags |= XFS_EFI_SKIP_DISCARD;
	if (oinfo) {
		ASSERT(oinfo->oi_offset == 0);

		if (oinfo->oi_flags & XFS_OWNER_INFO_ATTR_FORK)
			new->xefi_flags |= XFS_EFI_ATTR_FORK;
		if (oinfo->oi_flags & XFS_OWNER_INFO_BMBT_BLOCK)
			new->xefi_flags |= XFS_EFI_BMBT_BLOCK;
		new->xefi_owner = oinfo->oi_owner;
	} else {
		new->xefi_owner = XFS_RMAP_OWN_NULL;
	}
	trace_xfs_bmap_free_defer(tp->t_mountp,
			XFS_FSB_TO_AGNO(tp->t_mountp, bno), 0,
			XFS_FSB_TO_AGBNO(tp->t_mountp, bno), len);
	xfs_defer_add(tp, XFS_DEFER_OPS_TYPE_FREE, &new->xefi_list);
}

#ifdef DEBUG
/*
 * Check if an AGF has a free extent record whose length is equal to
 * args->minlen.
 */
STATIC int
xfs_exact_minlen_extent_available(
	struct xfs_alloc_arg	*args,
	struct xfs_buf		*agbp,
	int			*stat)
{
	struct xfs_btree_cur	*cnt_cur;
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	int			error = 0;

	cnt_cur = xfs_allocbt_init_cursor(args->mp, args->tp, agbp,
					args->pag, XFS_BTNUM_CNT);
	error = xfs_alloc_lookup_ge(cnt_cur, 0, args->minlen, stat);
	if (error)
		goto out;

	if (*stat == 0) {
		error = -EFSCORRUPTED;
		goto out;
	}

	error = xfs_alloc_get_rec(cnt_cur, &fbno, &flen, stat);
	if (error)
		goto out;

	if (*stat == 1 && flen != args->minlen)
		*stat = 0;

out:
	xfs_btree_del_cursor(cnt_cur, error);

	return error;
}
#endif

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

	/* deferred ops (AGFL block frees) require permanent transactions */
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);

	if (!pag->pagf_init) {
		error = xfs_alloc_read_agf(pag, tp, flags, &agbp);
		if (error) {
			/* Couldn't lock the AGF so skip this AG. */
			if (error == -EAGAIN)
				error = 0;
			goto out_no_agbp;
		}
	}

	/*
	 * If this is a metadata preferred pag and we are user data then try
	 * somewhere else if we are not being asked to try harder at this
	 * point
	 */
	if (pag->pagf_metadata && (args->datatype & XFS_ALLOC_USERDATA) &&
	    (flags & XFS_ALLOC_FLAG_TRYLOCK)) {
		ASSERT(!(flags & XFS_ALLOC_FLAG_FREEING));
		goto out_agbp_relse;
	}

	need = xfs_alloc_min_freelist(mp, pag);
	if (!xfs_alloc_space_available(args, need, flags |
			XFS_ALLOC_FLAG_CHECK))
		goto out_agbp_relse;

	/*
	 * Get the a.g. freespace buffer.
	 * Can fail if we're not blocking on locks, and it's held.
	 */
	if (!agbp) {
		error = xfs_alloc_read_agf(pag, tp, flags, &agbp);
		if (error) {
			/* Couldn't lock the AGF so skip this AG. */
			if (error == -EAGAIN)
				error = 0;
			goto out_no_agbp;
		}
	}

	/* reset a padding mismatched agfl before final free space check */
	if (pag->pagf_agflreset)
		xfs_agfl_reset(tp, agbp, pag);

	/* If there isn't enough total space or single-extent, reject it. */
	need = xfs_alloc_min_freelist(mp, pag);
	if (!xfs_alloc_space_available(args, need, flags))
		goto out_agbp_relse;

#ifdef DEBUG
	if (args->alloc_minlen_only) {
		int stat;

		error = xfs_exact_minlen_extent_available(args, agbp, &stat);
		if (error || !stat)
			goto out_agbp_relse;
	}
#endif
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
	/* struct copy below */
	if (flags & XFS_ALLOC_FLAG_NORMAP)
		targs.oinfo = XFS_RMAP_OINFO_SKIP_UPDATE;
	else
		targs.oinfo = XFS_RMAP_OINFO_AG;
	while (!(flags & XFS_ALLOC_FLAG_NOSHRINK) && pag->pagf_flcount > need) {
		error = xfs_alloc_get_freelist(pag, tp, agbp, &bno, 0);
		if (error)
			goto out_agbp_relse;

		/* defer agfl frees */
		xfs_defer_agfl_block(tp, args->agno, bno, &targs.oinfo);
	}

	targs.tp = tp;
	targs.mp = mp;
	targs.agbp = agbp;
	targs.agno = args->agno;
	targs.alignment = targs.minlen = targs.prod = 1;
	targs.type = XFS_ALLOCTYPE_THIS_AG;
	targs.pag = pag;
	error = xfs_alloc_read_agfl(pag, tp, &agflbp);
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
			error = xfs_alloc_put_freelist(pag, tp, agbp,
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
int
xfs_alloc_get_freelist(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agblock_t		*bnop,
	int			btreeblk)
{
	struct xfs_agf		*agf = agbp->b_addr;
	struct xfs_buf		*agflbp;
	xfs_agblock_t		bno;
	__be32			*agfl_bno;
	int			error;
	uint32_t		logflags;
	struct xfs_mount	*mp = tp->t_mountp;

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
	error = xfs_alloc_read_agfl(pag, tp, &agflbp);
	if (error)
		return error;


	/*
	 * Get the block number and update the data structures.
	 */
	agfl_bno = xfs_buf_to_agfl_bno(agflbp);
	bno = be32_to_cpu(agfl_bno[be32_to_cpu(agf->agf_flfirst)]);
	be32_add_cpu(&agf->agf_flfirst, 1);
	xfs_trans_brelse(tp, agflbp);
	if (be32_to_cpu(agf->agf_flfirst) == xfs_agfl_size(mp))
		agf->agf_flfirst = 0;

	ASSERT(!pag->pagf_agflreset);
	be32_add_cpu(&agf->agf_flcount, -1);
	pag->pagf_flcount--;

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
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	uint32_t		fields)
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
		offsetof(xfs_agf_t, agf_refcount_blocks),
		offsetof(xfs_agf_t, agf_refcount_root),
		offsetof(xfs_agf_t, agf_refcount_level),
		/* needed so that we don't log the whole rest of the structure: */
		offsetof(xfs_agf_t, agf_spare64),
		sizeof(xfs_agf_t)
	};

	trace_xfs_agf(tp->t_mountp, bp->b_addr, fields, _RET_IP_);

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_AGF_BUF);

	xfs_btree_offsets(fields, offsets, XFS_AGF_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, (uint)first, (uint)last);
}

/*
 * Put the block on the freelist for the allocation group.
 */
int
xfs_alloc_put_freelist(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_buf		*agflbp,
	xfs_agblock_t		bno,
	int			btreeblk)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agf		*agf = agbp->b_addr;
	__be32			*blockp;
	int			error;
	uint32_t		logflags;
	__be32			*agfl_bno;
	int			startoff;

	if (!agflbp) {
		error = xfs_alloc_read_agfl(pag, tp, &agflbp);
		if (error)
			return error;
	}

	be32_add_cpu(&agf->agf_fllast, 1);
	if (be32_to_cpu(agf->agf_fllast) == xfs_agfl_size(mp))
		agf->agf_fllast = 0;

	ASSERT(!pag->pagf_agflreset);
	be32_add_cpu(&agf->agf_flcount, 1);
	pag->pagf_flcount++;

	logflags = XFS_AGF_FLLAST | XFS_AGF_FLCOUNT;
	if (btreeblk) {
		be32_add_cpu(&agf->agf_btreeblks, -1);
		pag->pagf_btreeblks--;
		logflags |= XFS_AGF_BTREEBLKS;
	}

	xfs_alloc_log_agf(tp, agbp, logflags);

	ASSERT(be32_to_cpu(agf->agf_flcount) <= xfs_agfl_size(mp));

	agfl_bno = xfs_buf_to_agfl_bno(agflbp);
	blockp = &agfl_bno[be32_to_cpu(agf->agf_fllast)];
	*blockp = cpu_to_be32(bno);
	startoff = (char *)blockp - (char *)agflbp->b_addr;

	xfs_alloc_log_agf(tp, agbp, logflags);

	xfs_trans_buf_set_type(tp, agflbp, XFS_BLFT_AGFL_BUF);
	xfs_trans_log_buf(tp, agflbp, startoff,
			  startoff + sizeof(xfs_agblock_t) - 1);
	return 0;
}

static xfs_failaddr_t
xfs_agf_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_agf		*agf = bp->b_addr;

	if (xfs_has_crc(mp)) {
		if (!uuid_equal(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid))
			return __this_address;
		if (!xfs_log_check_lsn(mp, be64_to_cpu(agf->agf_lsn)))
			return __this_address;
	}

	if (!xfs_verify_magic(bp, agf->agf_magicnum))
		return __this_address;

	if (!(XFS_AGF_GOOD_VERSION(be32_to_cpu(agf->agf_versionnum)) &&
	      be32_to_cpu(agf->agf_freeblks) <= be32_to_cpu(agf->agf_length) &&
	      be32_to_cpu(agf->agf_flfirst) < xfs_agfl_size(mp) &&
	      be32_to_cpu(agf->agf_fllast) < xfs_agfl_size(mp) &&
	      be32_to_cpu(agf->agf_flcount) <= xfs_agfl_size(mp)))
		return __this_address;

	if (be32_to_cpu(agf->agf_length) > mp->m_sb.sb_dblocks)
		return __this_address;

	if (be32_to_cpu(agf->agf_freeblks) < be32_to_cpu(agf->agf_longest) ||
	    be32_to_cpu(agf->agf_freeblks) > be32_to_cpu(agf->agf_length))
		return __this_address;

	if (be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]) < 1 ||
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]) < 1 ||
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNO]) >
						mp->m_alloc_maxlevels ||
	    be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNT]) >
						mp->m_alloc_maxlevels)
		return __this_address;

	if (xfs_has_rmapbt(mp) &&
	    (be32_to_cpu(agf->agf_levels[XFS_BTNUM_RMAP]) < 1 ||
	     be32_to_cpu(agf->agf_levels[XFS_BTNUM_RMAP]) >
						mp->m_rmap_maxlevels))
		return __this_address;

	if (xfs_has_rmapbt(mp) &&
	    be32_to_cpu(agf->agf_rmap_blocks) > be32_to_cpu(agf->agf_length))
		return __this_address;

	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agf->agf_seqno) != bp->b_pag->pag_agno)
		return __this_address;

	if (xfs_has_lazysbcount(mp) &&
	    be32_to_cpu(agf->agf_btreeblks) > be32_to_cpu(agf->agf_length))
		return __this_address;

	if (xfs_has_reflink(mp) &&
	    be32_to_cpu(agf->agf_refcount_blocks) >
	    be32_to_cpu(agf->agf_length))
		return __this_address;

	if (xfs_has_reflink(mp) &&
	    (be32_to_cpu(agf->agf_refcount_level) < 1 ||
	     be32_to_cpu(agf->agf_refcount_level) > mp->m_refc_maxlevels))
		return __this_address;

	return NULL;

}

static void
xfs_agf_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	xfs_failaddr_t	fa;

	if (xfs_has_crc(mp) &&
	    !xfs_buf_verify_cksum(bp, XFS_AGF_CRC_OFF))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_agf_verify(bp);
		if (XFS_TEST_ERROR(fa, mp, XFS_ERRTAG_ALLOC_READ_AGF))
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}
}

static void
xfs_agf_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	struct xfs_agf		*agf = bp->b_addr;
	xfs_failaddr_t		fa;

	fa = xfs_agf_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (!xfs_has_crc(mp))
		return;

	if (bip)
		agf->agf_lsn = cpu_to_be64(bip->bli_item.li_lsn);

	xfs_buf_update_cksum(bp, XFS_AGF_CRC_OFF);
}

const struct xfs_buf_ops xfs_agf_buf_ops = {
	.name = "xfs_agf",
	.magic = { cpu_to_be32(XFS_AGF_MAGIC), cpu_to_be32(XFS_AGF_MAGIC) },
	.verify_read = xfs_agf_read_verify,
	.verify_write = xfs_agf_write_verify,
	.verify_struct = xfs_agf_verify,
};

/*
 * Read in the allocation group header (free/alloc section).
 */
int
xfs_read_agf(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	int			flags,
	struct xfs_buf		**agfbpp)
{
	struct xfs_mount	*mp = pag->pag_mount;
	int			error;

	trace_xfs_read_agf(pag->pag_mount, pag->pag_agno);

	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, pag->pag_agno, XFS_AGF_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), flags, agfbpp, &xfs_agf_buf_ops);
	if (error)
		return error;

	xfs_buf_set_ref(*agfbpp, XFS_AGF_REF);
	return 0;
}

/*
 * Read in the allocation group header (free/alloc section) and initialise the
 * perag structure if necessary. If the caller provides @agfbpp, then return the
 * locked buffer to the caller, otherwise free it.
 */
int
xfs_alloc_read_agf(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	int			flags,
	struct xfs_buf		**agfbpp)
{
	struct xfs_buf		*agfbp;
	struct xfs_agf		*agf;
	int			error;
	int			allocbt_blks;

	trace_xfs_alloc_read_agf(pag->pag_mount, pag->pag_agno);

	/* We don't support trylock when freeing. */
	ASSERT((flags & (XFS_ALLOC_FLAG_FREEING | XFS_ALLOC_FLAG_TRYLOCK)) !=
			(XFS_ALLOC_FLAG_FREEING | XFS_ALLOC_FLAG_TRYLOCK));
	error = xfs_read_agf(pag, tp,
			(flags & XFS_ALLOC_FLAG_TRYLOCK) ? XBF_TRYLOCK : 0,
			&agfbp);
	if (error)
		return error;

	agf = agfbp->b_addr;
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
		pag->pagf_init = 1;
		pag->pagf_agflreset = xfs_agfl_needs_reset(pag->pag_mount, agf);

		/*
		 * Update the in-core allocbt counter. Filter out the rmapbt
		 * subset of the btreeblks counter because the rmapbt is managed
		 * by perag reservation. Subtract one for the rmapbt root block
		 * because the rmap counter includes it while the btreeblks
		 * counter only tracks non-root blocks.
		 */
		allocbt_blks = pag->pagf_btreeblks;
		if (xfs_has_rmapbt(pag->pag_mount))
			allocbt_blks -= be32_to_cpu(agf->agf_rmap_blocks) - 1;
		if (allocbt_blks > 0)
			atomic64_add(allocbt_blks,
					&pag->pag_mount->m_allocbt_blks);
	}
#ifdef DEBUG
	else if (!xfs_is_shutdown(pag->pag_mount)) {
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
	if (agfbpp)
		*agfbpp = agfbp;
	else
		xfs_trans_brelse(tp, agfbp);
	return 0;
}

/*
 * Allocate an extent (variable-size).
 * Depending on the allocation type, we either look in a single allocation
 * group or loop over the allocation groups to find the result.
 */
int				/* error */
xfs_alloc_vextent(
	struct xfs_alloc_arg	*args)	/* allocation argument structure */
{
	xfs_agblock_t		agsize;	/* allocation group size */
	int			error;
	int			flags;	/* XFS_ALLOC_FLAG_... locking flags */
	struct xfs_mount	*mp;	/* mount structure pointer */
	xfs_agnumber_t		sagno;	/* starting allocation group number */
	xfs_alloctype_t		type;	/* input allocation type */
	int			bump_rotor = 0;
	xfs_agnumber_t		rotorstep = xfs_rotorstep; /* inode32 agf stepper */

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

	switch (type) {
	case XFS_ALLOCTYPE_THIS_AG:
	case XFS_ALLOCTYPE_NEAR_BNO:
	case XFS_ALLOCTYPE_THIS_BNO:
		/*
		 * These three force us into a single a.g.
		 */
		args->agno = XFS_FSB_TO_AGNO(mp, args->fsbno);
		args->pag = xfs_perag_get(mp, args->agno);
		error = xfs_alloc_fix_freelist(args, 0);
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
		    xfs_is_inode32(mp)) {
			args->fsbno = XFS_AGB_TO_FSB(mp,
					((mp->m_agfrotor / rotorstep) %
					mp->m_sb.sb_agcount), 0);
			bump_rotor = 1;
		}
		args->agbno = XFS_FSB_TO_AGBNO(mp, args->fsbno);
		args->type = XFS_ALLOCTYPE_NEAR_BNO;
		fallthrough;
	case XFS_ALLOCTYPE_FIRST_AG:
		/*
		 * Rotate through the allocation groups looking for a winner.
		 */
		if (type == XFS_ALLOCTYPE_FIRST_AG) {
			/*
			 * Start with allocation group given by bno.
			 */
			args->agno = XFS_FSB_TO_AGNO(mp, args->fsbno);
			args->type = XFS_ALLOCTYPE_THIS_AG;
			sagno = 0;
			flags = 0;
		} else {
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
			error = xfs_alloc_fix_freelist(args, flags);
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
				if (args->tp->t_firstblock != NULLFSBLOCK)
					args->agno = sagno;
				else
					args->agno = 0;
			}
			/*
			 * Reached the starting a.g., must either be done
			 * or switch to non-trylock mode.
			 */
			if (args->agno == sagno) {
				if (flags == 0) {
					args->agbno = NULLAGBLOCK;
					trace_xfs_alloc_vextent_allfailed(args);
					break;
				}

				flags = 0;
				if (type == XFS_ALLOCTYPE_START_BNO) {
					args->agbno = XFS_FSB_TO_AGBNO(mp,
						args->fsbno);
					args->type = XFS_ALLOCTYPE_NEAR_BNO;
				}
			}
			xfs_perag_put(args->pag);
		}
		if (bump_rotor) {
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

/* Ensure that the freelist is at full capacity. */
int
xfs_free_extent_fix_freelist(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		**agbp)
{
	struct xfs_alloc_arg	args;
	int			error;

	memset(&args, 0, sizeof(struct xfs_alloc_arg));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.agno = pag->pag_agno;
	args.pag = pag;

	/*
	 * validate that the block number is legal - the enables us to detect
	 * and handle a silent filesystem corruption rather than crashing.
	 */
	if (args.agno >= args.mp->m_sb.sb_agcount)
		return -EFSCORRUPTED;

	error = xfs_alloc_fix_freelist(&args, XFS_ALLOC_FLAG_FREEING);
	if (error)
		return error;

	*agbp = args.agbp;
	return 0;
}

/*
 * Free an extent.
 * Just break up the extent address and hand off to xfs_free_ag_extent
 * after fixing up the freelist.
 */
int
__xfs_free_extent(
	struct xfs_trans		*tp,
	xfs_fsblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	enum xfs_ag_resv_type		type,
	bool				skip_discard)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_buf			*agbp;
	xfs_agnumber_t			agno = XFS_FSB_TO_AGNO(mp, bno);
	xfs_agblock_t			agbno = XFS_FSB_TO_AGBNO(mp, bno);
	struct xfs_agf			*agf;
	int				error;
	unsigned int			busy_flags = 0;
	struct xfs_perag		*pag;

	ASSERT(len != 0);
	ASSERT(type != XFS_AG_RESV_AGFL);

	if (XFS_TEST_ERROR(false, mp,
			XFS_ERRTAG_FREE_EXTENT))
		return -EIO;

	pag = xfs_perag_get(mp, agno);
	error = xfs_free_extent_fix_freelist(tp, pag, &agbp);
	if (error)
		goto err;
	agf = agbp->b_addr;

	if (XFS_IS_CORRUPT(mp, agbno >= mp->m_sb.sb_agblocks)) {
		error = -EFSCORRUPTED;
		goto err_release;
	}

	/* validate the extent size is legal now we have the agf locked */
	if (XFS_IS_CORRUPT(mp, agbno + len > be32_to_cpu(agf->agf_length))) {
		error = -EFSCORRUPTED;
		goto err_release;
	}

	error = xfs_free_ag_extent(tp, agbp, agno, agbno, len, oinfo, type);
	if (error)
		goto err_release;

	if (skip_discard)
		busy_flags |= XFS_EXTENT_BUSY_SKIP_DISCARD;
	xfs_extent_busy_insert(tp, pag, agbno, len, busy_flags);
	xfs_perag_put(pag);
	return 0;

err_release:
	xfs_trans_brelse(tp, agbp);
err:
	xfs_perag_put(pag);
	return error;
}

struct xfs_alloc_query_range_info {
	xfs_alloc_query_range_fn	fn;
	void				*priv;
};

/* Format btree record and pass to our callback. */
STATIC int
xfs_alloc_query_range_helper(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_alloc_query_range_info	*query = priv;
	struct xfs_alloc_rec_incore		irec;

	irec.ar_startblock = be32_to_cpu(rec->alloc.ar_startblock);
	irec.ar_blockcount = be32_to_cpu(rec->alloc.ar_blockcount);
	return query->fn(cur, &irec, query->priv);
}

/* Find all free space within a given range of blocks. */
int
xfs_alloc_query_range(
	struct xfs_btree_cur			*cur,
	const struct xfs_alloc_rec_incore	*low_rec,
	const struct xfs_alloc_rec_incore	*high_rec,
	xfs_alloc_query_range_fn		fn,
	void					*priv)
{
	union xfs_btree_irec			low_brec;
	union xfs_btree_irec			high_brec;
	struct xfs_alloc_query_range_info	query;

	ASSERT(cur->bc_btnum == XFS_BTNUM_BNO);
	low_brec.a = *low_rec;
	high_brec.a = *high_rec;
	query.priv = priv;
	query.fn = fn;
	return xfs_btree_query_range(cur, &low_brec, &high_brec,
			xfs_alloc_query_range_helper, &query);
}

/* Find all free space records. */
int
xfs_alloc_query_all(
	struct xfs_btree_cur			*cur,
	xfs_alloc_query_range_fn		fn,
	void					*priv)
{
	struct xfs_alloc_query_range_info	query;

	ASSERT(cur->bc_btnum == XFS_BTNUM_BNO);
	query.priv = priv;
	query.fn = fn;
	return xfs_btree_query_all(cur, xfs_alloc_query_range_helper, &query);
}

/* Is there a record covering a given extent? */
int
xfs_alloc_has_record(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	bool			*exists)
{
	union xfs_btree_irec	low;
	union xfs_btree_irec	high;

	memset(&low, 0, sizeof(low));
	low.a.ar_startblock = bno;
	memset(&high, 0xFF, sizeof(high));
	high.a.ar_startblock = bno + len - 1;

	return xfs_btree_has_record(cur, &low, &high, exists);
}

/*
 * Walk all the blocks in the AGFL.  The @walk_fn can return any negative
 * error code or XFS_ITER_*.
 */
int
xfs_agfl_walk(
	struct xfs_mount	*mp,
	struct xfs_agf		*agf,
	struct xfs_buf		*agflbp,
	xfs_agfl_walk_fn	walk_fn,
	void			*priv)
{
	__be32			*agfl_bno;
	unsigned int		i;
	int			error;

	agfl_bno = xfs_buf_to_agfl_bno(agflbp);
	i = be32_to_cpu(agf->agf_flfirst);

	/* Nothing to walk in an empty AGFL. */
	if (agf->agf_flcount == cpu_to_be32(0))
		return 0;

	/* Otherwise, walk from first to last, wrapping as needed. */
	for (;;) {
		error = walk_fn(mp, be32_to_cpu(agfl_bno[i]), priv);
		if (error)
			return error;
		if (i == be32_to_cpu(agf->agf_fllast))
			break;
		if (++i == xfs_agfl_size(mp))
			i = 0;
	}

	return 0;
}

int __init
xfs_extfree_intent_init_cache(void)
{
	xfs_extfree_item_cache = kmem_cache_create("xfs_extfree_intent",
			sizeof(struct xfs_extent_free_item),
			0, 0, NULL);

	return xfs_extfree_item_cache != NULL ? 0 : -ENOMEM;
}

void
xfs_extfree_intent_destroy_cache(void)
{
	kmem_cache_destroy(xfs_extfree_item_cache);
	xfs_extfree_item_cache = NULL;
}
