/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_rmap.h"

/*
 * Determine the extent state.
 */
/* ARGSUSED */
STATIC xfs_exntst_t
xfs_extent_state(
	xfs_filblks_t		blks,
	int			extent_flag)
{
	if (extent_flag) {
		ASSERT(blks != 0);	/* saved for DMIG */
		return XFS_EXT_UNWRITTEN;
	}
	return XFS_EXT_NORM;
}

/*
 * Convert on-disk form of btree root to in-memory form.
 */
void
xfs_bmdr_to_bmbt(
	struct xfs_inode	*ip,
	xfs_bmdr_block_t	*dblock,
	int			dblocklen,
	struct xfs_btree_block	*rblock,
	int			rblocklen)
{
	struct xfs_mount	*mp = ip->i_mount;
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	__be64			*fpp;
	xfs_bmbt_key_t		*tkp;
	__be64			*tpp;

	if (xfs_sb_version_hascrc(&mp->m_sb))
		xfs_btree_init_block_int(mp, rblock, XFS_BUF_DADDR_NULL,
				 XFS_BMAP_CRC_MAGIC, 0, 0, ip->i_ino,
				 XFS_BTREE_LONG_PTRS | XFS_BTREE_CRC_BLOCKS);
	else
		xfs_btree_init_block_int(mp, rblock, XFS_BUF_DADDR_NULL,
				 XFS_BMAP_MAGIC, 0, 0, ip->i_ino,
				 XFS_BTREE_LONG_PTRS);

	rblock->bb_level = dblock->bb_level;
	ASSERT(be16_to_cpu(rblock->bb_level) > 0);
	rblock->bb_numrecs = dblock->bb_numrecs;
	dmxr = xfs_bmdr_maxrecs(dblocklen, 0);
	fkp = XFS_BMDR_KEY_ADDR(dblock, 1);
	tkp = XFS_BMBT_KEY_ADDR(mp, rblock, 1);
	fpp = XFS_BMDR_PTR_ADDR(dblock, 1, dmxr);
	tpp = XFS_BMAP_BROOT_PTR_ADDR(mp, rblock, 1, rblocklen);
	dmxr = be16_to_cpu(dblock->bb_numrecs);
	memcpy(tkp, fkp, sizeof(*fkp) * dmxr);
	memcpy(tpp, fpp, sizeof(*fpp) * dmxr);
}

/*
 * Convert a compressed bmap extent record to an uncompressed form.
 * This code must be in sync with the routines xfs_bmbt_get_startoff,
 * xfs_bmbt_get_startblock, xfs_bmbt_get_blockcount and xfs_bmbt_get_state.
 */
STATIC void
__xfs_bmbt_get_all(
		__uint64_t l0,
		__uint64_t l1,
		xfs_bmbt_irec_t *s)
{
	int	ext_flag;
	xfs_exntst_t st;

	ext_flag = (int)(l0 >> (64 - BMBT_EXNTFLAG_BITLEN));
	s->br_startoff = ((xfs_fileoff_t)l0 &
			   xfs_mask64lo(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
	s->br_startblock = (((xfs_fsblock_t)l0 & xfs_mask64lo(9)) << 43) |
			   (((xfs_fsblock_t)l1) >> 21);
	s->br_blockcount = (xfs_filblks_t)(l1 & xfs_mask64lo(21));
	/* This is xfs_extent_state() in-line */
	if (ext_flag) {
		ASSERT(s->br_blockcount != 0);	/* saved for DMIG */
		st = XFS_EXT_UNWRITTEN;
	} else
		st = XFS_EXT_NORM;
	s->br_state = st;
}

void
xfs_bmbt_get_all(
	xfs_bmbt_rec_host_t *r,
	xfs_bmbt_irec_t *s)
{
	__xfs_bmbt_get_all(r->l0, r->l1, s);
}

/*
 * Extract the blockcount field from an in memory bmap extent record.
 */
xfs_filblks_t
xfs_bmbt_get_blockcount(
	xfs_bmbt_rec_host_t	*r)
{
	return (xfs_filblks_t)(r->l1 & xfs_mask64lo(21));
}

/*
 * Extract the startblock field from an in memory bmap extent record.
 */
xfs_fsblock_t
xfs_bmbt_get_startblock(
	xfs_bmbt_rec_host_t	*r)
{
	return (((xfs_fsblock_t)r->l0 & xfs_mask64lo(9)) << 43) |
	       (((xfs_fsblock_t)r->l1) >> 21);
}

/*
 * Extract the startoff field from an in memory bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_get_startoff(
	xfs_bmbt_rec_host_t	*r)
{
	return ((xfs_fileoff_t)r->l0 &
		 xfs_mask64lo(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
}

xfs_exntst_t
xfs_bmbt_get_state(
	xfs_bmbt_rec_host_t	*r)
{
	int	ext_flag;

	ext_flag = (int)((r->l0) >> (64 - BMBT_EXNTFLAG_BITLEN));
	return xfs_extent_state(xfs_bmbt_get_blockcount(r),
				ext_flag);
}

/*
 * Extract the blockcount field from an on disk bmap extent record.
 */
xfs_filblks_t
xfs_bmbt_disk_get_blockcount(
	xfs_bmbt_rec_t	*r)
{
	return (xfs_filblks_t)(be64_to_cpu(r->l1) & xfs_mask64lo(21));
}

/*
 * Extract the startoff field from a disk format bmap extent record.
 */
xfs_fileoff_t
xfs_bmbt_disk_get_startoff(
	xfs_bmbt_rec_t	*r)
{
	return ((xfs_fileoff_t)be64_to_cpu(r->l0) &
		 xfs_mask64lo(64 - BMBT_EXNTFLAG_BITLEN)) >> 9;
}


/*
 * Set all the fields in a bmap extent record from the arguments.
 */
void
xfs_bmbt_set_allf(
	xfs_bmbt_rec_host_t	*r,
	xfs_fileoff_t		startoff,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount,
	xfs_exntst_t		state)
{
	int		extent_flag = (state == XFS_EXT_NORM) ? 0 : 1;

	ASSERT(state == XFS_EXT_NORM || state == XFS_EXT_UNWRITTEN);
	ASSERT((startoff & xfs_mask64hi(64-BMBT_STARTOFF_BITLEN)) == 0);
	ASSERT((blockcount & xfs_mask64hi(64-BMBT_BLOCKCOUNT_BITLEN)) == 0);

	ASSERT((startblock & xfs_mask64hi(64-BMBT_STARTBLOCK_BITLEN)) == 0);

	r->l0 = ((xfs_bmbt_rec_base_t)extent_flag << 63) |
		((xfs_bmbt_rec_base_t)startoff << 9) |
		((xfs_bmbt_rec_base_t)startblock >> 43);
	r->l1 = ((xfs_bmbt_rec_base_t)startblock << 21) |
		((xfs_bmbt_rec_base_t)blockcount &
		(xfs_bmbt_rec_base_t)xfs_mask64lo(21));
}

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
void
xfs_bmbt_set_all(
	xfs_bmbt_rec_host_t *r,
	xfs_bmbt_irec_t	*s)
{
	xfs_bmbt_set_allf(r, s->br_startoff, s->br_startblock,
			     s->br_blockcount, s->br_state);
}


/*
 * Set all the fields in a disk format bmap extent record from the arguments.
 */
void
xfs_bmbt_disk_set_allf(
	xfs_bmbt_rec_t		*r,
	xfs_fileoff_t		startoff,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount,
	xfs_exntst_t		state)
{
	int			extent_flag = (state == XFS_EXT_NORM) ? 0 : 1;

	ASSERT(state == XFS_EXT_NORM || state == XFS_EXT_UNWRITTEN);
	ASSERT((startoff & xfs_mask64hi(64-BMBT_STARTOFF_BITLEN)) == 0);
	ASSERT((blockcount & xfs_mask64hi(64-BMBT_BLOCKCOUNT_BITLEN)) == 0);
	ASSERT((startblock & xfs_mask64hi(64-BMBT_STARTBLOCK_BITLEN)) == 0);

	r->l0 = cpu_to_be64(
		((xfs_bmbt_rec_base_t)extent_flag << 63) |
		 ((xfs_bmbt_rec_base_t)startoff << 9) |
		 ((xfs_bmbt_rec_base_t)startblock >> 43));
	r->l1 = cpu_to_be64(
		((xfs_bmbt_rec_base_t)startblock << 21) |
		 ((xfs_bmbt_rec_base_t)blockcount &
		  (xfs_bmbt_rec_base_t)xfs_mask64lo(21)));
}

/*
 * Set all the fields in a bmap extent record from the uncompressed form.
 */
STATIC void
xfs_bmbt_disk_set_all(
	xfs_bmbt_rec_t	*r,
	xfs_bmbt_irec_t *s)
{
	xfs_bmbt_disk_set_allf(r, s->br_startoff, s->br_startblock,
				  s->br_blockcount, s->br_state);
}

/*
 * Set the blockcount field in a bmap extent record.
 */
void
xfs_bmbt_set_blockcount(
	xfs_bmbt_rec_host_t *r,
	xfs_filblks_t	v)
{
	ASSERT((v & xfs_mask64hi(43)) == 0);
	r->l1 = (r->l1 & (xfs_bmbt_rec_base_t)xfs_mask64hi(43)) |
		  (xfs_bmbt_rec_base_t)(v & xfs_mask64lo(21));
}

/*
 * Set the startblock field in a bmap extent record.
 */
void
xfs_bmbt_set_startblock(
	xfs_bmbt_rec_host_t *r,
	xfs_fsblock_t	v)
{
	ASSERT((v & xfs_mask64hi(12)) == 0);
	r->l0 = (r->l0 & (xfs_bmbt_rec_base_t)xfs_mask64hi(55)) |
		  (xfs_bmbt_rec_base_t)(v >> 43);
	r->l1 = (r->l1 & (xfs_bmbt_rec_base_t)xfs_mask64lo(21)) |
		  (xfs_bmbt_rec_base_t)(v << 21);
}

/*
 * Set the startoff field in a bmap extent record.
 */
void
xfs_bmbt_set_startoff(
	xfs_bmbt_rec_host_t *r,
	xfs_fileoff_t	v)
{
	ASSERT((v & xfs_mask64hi(9)) == 0);
	r->l0 = (r->l0 & (xfs_bmbt_rec_base_t) xfs_mask64hi(1)) |
		((xfs_bmbt_rec_base_t)v << 9) |
		  (r->l0 & (xfs_bmbt_rec_base_t)xfs_mask64lo(9));
}

/*
 * Set the extent state field in a bmap extent record.
 */
void
xfs_bmbt_set_state(
	xfs_bmbt_rec_host_t *r,
	xfs_exntst_t	v)
{
	ASSERT(v == XFS_EXT_NORM || v == XFS_EXT_UNWRITTEN);
	if (v == XFS_EXT_NORM)
		r->l0 &= xfs_mask64lo(64 - BMBT_EXNTFLAG_BITLEN);
	else
		r->l0 |= xfs_mask64hi(BMBT_EXNTFLAG_BITLEN);
}

/*
 * Convert in-memory form of btree root to on-disk form.
 */
void
xfs_bmbt_to_bmdr(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*rblock,
	int			rblocklen,
	xfs_bmdr_block_t	*dblock,
	int			dblocklen)
{
	int			dmxr;
	xfs_bmbt_key_t		*fkp;
	__be64			*fpp;
	xfs_bmbt_key_t		*tkp;
	__be64			*tpp;

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		ASSERT(rblock->bb_magic == cpu_to_be32(XFS_BMAP_CRC_MAGIC));
		ASSERT(uuid_equal(&rblock->bb_u.l.bb_uuid,
		       &mp->m_sb.sb_meta_uuid));
		ASSERT(rblock->bb_u.l.bb_blkno ==
		       cpu_to_be64(XFS_BUF_DADDR_NULL));
	} else
		ASSERT(rblock->bb_magic == cpu_to_be32(XFS_BMAP_MAGIC));
	ASSERT(rblock->bb_u.l.bb_leftsib == cpu_to_be64(NULLFSBLOCK));
	ASSERT(rblock->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK));
	ASSERT(rblock->bb_level != 0);
	dblock->bb_level = rblock->bb_level;
	dblock->bb_numrecs = rblock->bb_numrecs;
	dmxr = xfs_bmdr_maxrecs(dblocklen, 0);
	fkp = XFS_BMBT_KEY_ADDR(mp, rblock, 1);
	tkp = XFS_BMDR_KEY_ADDR(dblock, 1);
	fpp = XFS_BMAP_BROOT_PTR_ADDR(mp, rblock, 1, rblocklen);
	tpp = XFS_BMDR_PTR_ADDR(dblock, 1, dmxr);
	dmxr = be16_to_cpu(dblock->bb_numrecs);
	memcpy(tkp, fkp, sizeof(*fkp) * dmxr);
	memcpy(tpp, fpp, sizeof(*fpp) * dmxr);
}

/*
 * Check extent records, which have just been read, for
 * any bit in the extent flag field. ASSERT on debug
 * kernels, as this condition should not occur.
 * Return an error condition (1) if any flags found,
 * otherwise return 0.
 */

int
xfs_check_nostate_extents(
	xfs_ifork_t		*ifp,
	xfs_extnum_t		idx,
	xfs_extnum_t		num)
{
	for (; num > 0; num--, idx++) {
		xfs_bmbt_rec_host_t *ep = xfs_iext_get_ext(ifp, idx);
		if ((ep->l0 >>
		     (64 - BMBT_EXNTFLAG_BITLEN)) != 0) {
			ASSERT(0);
			return 1;
		}
	}
	return 0;
}


STATIC struct xfs_btree_cur *
xfs_bmbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	struct xfs_btree_cur	*new;

	new = xfs_bmbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.b.ip, cur->bc_private.b.whichfork);

	/*
	 * Copy the firstblock, dfops, and flags values,
	 * since init cursor doesn't get them.
	 */
	new->bc_private.b.firstblock = cur->bc_private.b.firstblock;
	new->bc_private.b.dfops = cur->bc_private.b.dfops;
	new->bc_private.b.flags = cur->bc_private.b.flags;

	return new;
}

STATIC void
xfs_bmbt_update_cursor(
	struct xfs_btree_cur	*src,
	struct xfs_btree_cur	*dst)
{
	ASSERT((dst->bc_private.b.firstblock != NULLFSBLOCK) ||
	       (dst->bc_private.b.ip->i_d.di_flags & XFS_DIFLAG_REALTIME));
	ASSERT(dst->bc_private.b.dfops == src->bc_private.b.dfops);

	dst->bc_private.b.allocated += src->bc_private.b.allocated;
	dst->bc_private.b.firstblock = src->bc_private.b.firstblock;

	src->bc_private.b.allocated = 0;
}

STATIC int
xfs_bmbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			*stat)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.fsbno = cur->bc_private.b.firstblock;
	args.firstblock = args.fsbno;
	xfs_rmap_ino_bmbt_owner(&args.oinfo, cur->bc_private.b.ip->i_ino,
			cur->bc_private.b.whichfork);

	if (args.fsbno == NULLFSBLOCK) {
		args.fsbno = be64_to_cpu(start->l);
try_another_ag:
		args.type = XFS_ALLOCTYPE_START_BNO;
		/*
		 * Make sure there is sufficient room left in the AG to
		 * complete a full tree split for an extent insert.  If
		 * we are converting the middle part of an extent then
		 * we may need space for two tree splits.
		 *
		 * We are relying on the caller to make the correct block
		 * reservation for this operation to succeed.  If the
		 * reservation amount is insufficient then we may fail a
		 * block allocation here and corrupt the filesystem.
		 */
		args.minleft = args.tp->t_blk_res;
	} else if (cur->bc_private.b.dfops->dop_low) {
		args.type = XFS_ALLOCTYPE_START_BNO;
	} else {
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
	}

	args.minlen = args.maxlen = args.prod = 1;
	args.wasdel = cur->bc_private.b.flags & XFS_BTCUR_BPRV_WASDEL;
	if (!args.wasdel && args.tp->t_blk_res == 0) {
		error = -ENOSPC;
		goto error0;
	}
	error = xfs_alloc_vextent(&args);
	if (error)
		goto error0;

	/*
	 * During a CoW operation, the allocation and bmbt updates occur in
	 * different transactions.  The mapping code tries to put new bmbt
	 * blocks near extents being mapped, but the only way to guarantee this
	 * is if the alloc and the mapping happen in a single transaction that
	 * has a block reservation.  That isn't the case here, so if we run out
	 * of space we'll try again with another AG.
	 */
	if (xfs_sb_version_hasreflink(&cur->bc_mp->m_sb) &&
	    args.fsbno == NULLFSBLOCK &&
	    args.type == XFS_ALLOCTYPE_NEAR_BNO) {
		cur->bc_private.b.dfops->dop_low = true;
		args.fsbno = cur->bc_private.b.firstblock;
		goto try_another_ag;
	}

	if (args.fsbno == NULLFSBLOCK && args.minleft) {
		/*
		 * Could not find an AG with enough free space to satisfy
		 * a full btree split.  Try again and if
		 * successful activate the lowspace algorithm.
		 */
		args.fsbno = 0;
		args.type = XFS_ALLOCTYPE_FIRST_AG;
		error = xfs_alloc_vextent(&args);
		if (error)
			goto error0;
		cur->bc_private.b.dfops->dop_low = true;
	}
	if (args.fsbno == NULLFSBLOCK) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	cur->bc_private.b.firstblock = args.fsbno;
	cur->bc_private.b.allocated++;
	cur->bc_private.b.ip->i_d.di_nblocks++;
	xfs_trans_log_inode(args.tp, cur->bc_private.b.ip, XFS_ILOG_CORE);
	xfs_trans_mod_dquot_byino(args.tp, cur->bc_private.b.ip,
			XFS_TRANS_DQ_BCOUNT, 1L);

	new->l = cpu_to_be64(args.fsbno);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
	*stat = 1;
	return 0;

 error0:
	XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
	return error;
}

STATIC int
xfs_bmbt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_inode	*ip = cur->bc_private.b.ip;
	struct xfs_trans	*tp = cur->bc_tp;
	xfs_fsblock_t		fsbno = XFS_DADDR_TO_FSB(mp, XFS_BUF_ADDR(bp));
	struct xfs_owner_info	oinfo;

	xfs_rmap_ino_bmbt_owner(&oinfo, ip->i_ino, cur->bc_private.b.whichfork);
	xfs_bmap_add_free(mp, cur->bc_private.b.dfops, fsbno, 1, &oinfo);
	ip->i_d.di_nblocks--;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_BCOUNT, -1L);
	return 0;
}

STATIC int
xfs_bmbt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level == cur->bc_nlevels - 1) {
		struct xfs_ifork	*ifp;

		ifp = XFS_IFORK_PTR(cur->bc_private.b.ip,
				    cur->bc_private.b.whichfork);

		return xfs_bmbt_maxrecs(cur->bc_mp,
					ifp->if_broot_bytes, level == 0) / 2;
	}

	return cur->bc_mp->m_bmap_dmnr[level != 0];
}

int
xfs_bmbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level == cur->bc_nlevels - 1) {
		struct xfs_ifork	*ifp;

		ifp = XFS_IFORK_PTR(cur->bc_private.b.ip,
				    cur->bc_private.b.whichfork);

		return xfs_bmbt_maxrecs(cur->bc_mp,
					ifp->if_broot_bytes, level == 0);
	}

	return cur->bc_mp->m_bmap_dmxr[level != 0];

}

/*
 * Get the maximum records we could store in the on-disk format.
 *
 * For non-root nodes this is equivalent to xfs_bmbt_get_maxrecs, but
 * for the root node this checks the available space in the dinode fork
 * so that we can resize the in-memory buffer to match it.  After a
 * resize to the maximum size this function returns the same value
 * as xfs_bmbt_get_maxrecs for the root node, too.
 */
STATIC int
xfs_bmbt_get_dmaxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level != cur->bc_nlevels - 1)
		return cur->bc_mp->m_bmap_dmxr[level != 0];
	return xfs_bmdr_maxrecs(cur->bc_private.b.forksize, level == 0);
}

STATIC void
xfs_bmbt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->bmbt.br_startoff =
		cpu_to_be64(xfs_bmbt_disk_get_startoff(&rec->bmbt));
}

STATIC void
xfs_bmbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	xfs_bmbt_disk_set_all(&rec->bmbt, &cur->bc_rec.b);
}

STATIC void
xfs_bmbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	ptr->l = 0;
}

STATIC __int64_t
xfs_bmbt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	return (__int64_t)be64_to_cpu(key->bmbt.br_startoff) -
				      cur->bc_rec.b.br_startoff;
}

static bool
xfs_bmbt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	unsigned int		level;

	switch (block->bb_magic) {
	case cpu_to_be32(XFS_BMAP_CRC_MAGIC):
		if (!xfs_sb_version_hascrc(&mp->m_sb))
			return false;
		if (!uuid_equal(&block->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid))
			return false;
		if (be64_to_cpu(block->bb_u.l.bb_blkno) != bp->b_bn)
			return false;
		/*
		 * XXX: need a better way of verifying the owner here. Right now
		 * just make sure there has been one set.
		 */
		if (be64_to_cpu(block->bb_u.l.bb_owner) == 0)
			return false;
		/* fall through */
	case cpu_to_be32(XFS_BMAP_MAGIC):
		break;
	default:
		return false;
	}

	/*
	 * numrecs and level verification.
	 *
	 * We don't know what fork we belong to, so just verify that the level
	 * is less than the maximum of the two. Later checks will be more
	 * precise.
	 */
	level = be16_to_cpu(block->bb_level);
	if (level > max(mp->m_bm_maxlevels[0], mp->m_bm_maxlevels[1]))
		return false;
	if (be16_to_cpu(block->bb_numrecs) > mp->m_bmap_dmxr[level != 0])
		return false;

	/* sibling pointer verification */
	if (!block->bb_u.l.bb_leftsib ||
	    (block->bb_u.l.bb_leftsib != cpu_to_be64(NULLFSBLOCK) &&
	     !XFS_FSB_SANITY_CHECK(mp, be64_to_cpu(block->bb_u.l.bb_leftsib))))
		return false;
	if (!block->bb_u.l.bb_rightsib ||
	    (block->bb_u.l.bb_rightsib != cpu_to_be64(NULLFSBLOCK) &&
	     !XFS_FSB_SANITY_CHECK(mp, be64_to_cpu(block->bb_u.l.bb_rightsib))))
		return false;

	return true;
}

static void
xfs_bmbt_read_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_btree_lblock_verify_crc(bp))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (!xfs_bmbt_verify(bp))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp);
	}
}

static void
xfs_bmbt_write_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_bmbt_verify(bp)) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}
	xfs_btree_lblock_calc_crc(bp);
}

const struct xfs_buf_ops xfs_bmbt_buf_ops = {
	.name = "xfs_bmbt",
	.verify_read = xfs_bmbt_read_verify,
	.verify_write = xfs_bmbt_write_verify,
};


#if defined(DEBUG) || defined(XFS_WARN)
STATIC int
xfs_bmbt_keys_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return be64_to_cpu(k1->bmbt.br_startoff) <
		be64_to_cpu(k2->bmbt.br_startoff);
}

STATIC int
xfs_bmbt_recs_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*r1,
	union xfs_btree_rec	*r2)
{
	return xfs_bmbt_disk_get_startoff(&r1->bmbt) +
		xfs_bmbt_disk_get_blockcount(&r1->bmbt) <=
		xfs_bmbt_disk_get_startoff(&r2->bmbt);
}
#endif	/* DEBUG */

static const struct xfs_btree_ops xfs_bmbt_ops = {
	.rec_len		= sizeof(xfs_bmbt_rec_t),
	.key_len		= sizeof(xfs_bmbt_key_t),

	.dup_cursor		= xfs_bmbt_dup_cursor,
	.update_cursor		= xfs_bmbt_update_cursor,
	.alloc_block		= xfs_bmbt_alloc_block,
	.free_block		= xfs_bmbt_free_block,
	.get_maxrecs		= xfs_bmbt_get_maxrecs,
	.get_minrecs		= xfs_bmbt_get_minrecs,
	.get_dmaxrecs		= xfs_bmbt_get_dmaxrecs,
	.init_key_from_rec	= xfs_bmbt_init_key_from_rec,
	.init_rec_from_cur	= xfs_bmbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_bmbt_init_ptr_from_cur,
	.key_diff		= xfs_bmbt_key_diff,
	.buf_ops		= &xfs_bmbt_buf_ops,
#if defined(DEBUG) || defined(XFS_WARN)
	.keys_inorder		= xfs_bmbt_keys_inorder,
	.recs_inorder		= xfs_bmbt_recs_inorder,
#endif
};

/*
 * Allocate a new bmap btree cursor.
 */
struct xfs_btree_cur *				/* new bmap btree cursor */
xfs_bmbt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* inode owning the btree */
	int			whichfork)	/* data or attr fork */
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	struct xfs_btree_cur	*cur;
	ASSERT(whichfork != XFS_COW_FORK);

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_NOFS);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_nlevels = be16_to_cpu(ifp->if_broot->bb_level) + 1;
	cur->bc_btnum = XFS_BTNUM_BMAP;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;
	cur->bc_statoff = XFS_STATS_CALC_INDEX(xs_bmbt_2);

	cur->bc_ops = &xfs_bmbt_ops;
	cur->bc_flags = XFS_BTREE_LONG_PTRS | XFS_BTREE_ROOT_IN_INODE;
	if (xfs_sb_version_hascrc(&mp->m_sb))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	cur->bc_private.b.forksize = XFS_IFORK_SIZE(ip, whichfork);
	cur->bc_private.b.ip = ip;
	cur->bc_private.b.firstblock = NULLFSBLOCK;
	cur->bc_private.b.dfops = NULL;
	cur->bc_private.b.allocated = 0;
	cur->bc_private.b.flags = 0;
	cur->bc_private.b.whichfork = whichfork;

	return cur;
}

/*
 * Calculate number of records in a bmap btree block.
 */
int
xfs_bmbt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_BMBT_BLOCK_LEN(mp);

	if (leaf)
		return blocklen / sizeof(xfs_bmbt_rec_t);
	return blocklen / (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t));
}

/*
 * Calculate number of records in a bmap btree inode root.
 */
int
xfs_bmdr_maxrecs(
	int			blocklen,
	int			leaf)
{
	blocklen -= sizeof(xfs_bmdr_block_t);

	if (leaf)
		return blocklen / sizeof(xfs_bmdr_rec_t);
	return blocklen / (sizeof(xfs_bmdr_key_t) + sizeof(xfs_bmdr_ptr_t));
}

/*
 * Change the owner of a btree format fork fo the inode passed in. Change it to
 * the owner of that is passed in so that we can change owners before or after
 * we switch forks between inodes. The operation that the caller is doing will
 * determine whether is needs to change owner before or after the switch.
 *
 * For demand paged transactional modification, the fork switch should be done
 * after reading in all the blocks, modifying them and pinning them in the
 * transaction. For modification when the buffers are already pinned in memory,
 * the fork switch can be done before changing the owner as we won't need to
 * validate the owner until the btree buffers are unpinned and writes can occur
 * again.
 *
 * For recovery based ownership change, there is no transactional context and
 * so a buffer list must be supplied so that we can record the buffers that we
 * modified for the caller to issue IO on.
 */
int
xfs_bmbt_change_owner(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_ino_t		new_owner,
	struct list_head	*buffer_list)
{
	struct xfs_btree_cur	*cur;
	int			error;

	ASSERT(tp || buffer_list);
	ASSERT(!(tp && buffer_list));
	if (whichfork == XFS_DATA_FORK)
		ASSERT(ip->i_d.di_format == XFS_DINODE_FMT_BTREE);
	else
		ASSERT(ip->i_d.di_aformat == XFS_DINODE_FMT_BTREE);

	cur = xfs_bmbt_init_cursor(ip->i_mount, tp, ip, whichfork);
	if (!cur)
		return -ENOMEM;

	error = xfs_btree_change_owner(cur, new_owner, buffer_list);
	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);
	return error;
}
