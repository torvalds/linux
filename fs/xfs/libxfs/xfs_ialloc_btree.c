/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"


STATIC int
xfs_inobt_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_inobt_mnr[level != 0];
}

STATIC struct xfs_btree_cur *
xfs_inobt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_inobt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agbp, cur->bc_private.a.agno,
			cur->bc_btnum);
}

STATIC void
xfs_inobt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*nptr,
	int			inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);

	agi->agi_root = nptr->s;
	be32_add_cpu(&agi->agi_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp, XFS_AGI_ROOT | XFS_AGI_LEVEL);
}

STATIC void
xfs_finobt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*nptr,
	int			inc)	/* level change */
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);

	agi->agi_free_root = nptr->s;
	be32_add_cpu(&agi->agi_free_level, inc);
	xfs_ialloc_log_agi(cur->bc_tp, agbp,
			   XFS_AGI_FREE_ROOT | XFS_AGI_FREE_LEVEL);
}

STATIC int
xfs_inobt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			*stat)
{
	xfs_alloc_arg_t		args;		/* block allocation args */
	int			error;		/* error return value */
	xfs_agblock_t		sbno = be32_to_cpu(start->s);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);

	memset(&args, 0, sizeof(args));
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_private.a.agno, sbno);
	args.minlen = 1;
	args.maxlen = 1;
	args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;

	error = xfs_alloc_vextent(&args);
	if (error) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
		return error;
	}
	if (args.fsbno == NULLFSBLOCK) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);

	new->s = cpu_to_be32(XFS_FSB_TO_AGBNO(args.mp, args.fsbno));
	*stat = 1;
	return 0;
}

STATIC int
xfs_inobt_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	xfs_fsblock_t		fsbno;
	int			error;

	fsbno = XFS_DADDR_TO_FSB(cur->bc_mp, XFS_BUF_ADDR(bp));
	error = xfs_free_extent(cur->bc_tp, fsbno, 1);
	if (error)
		return error;

	xfs_trans_binval(cur->bc_tp, bp);
	return error;
}

STATIC int
xfs_inobt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_inobt_mxr[level != 0];
}

STATIC void
xfs_inobt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	key->inobt.ir_startino = rec->inobt.ir_startino;
}

STATIC void
xfs_inobt_init_rec_from_key(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	rec->inobt.ir_startino = key->inobt.ir_startino;
}

STATIC void
xfs_inobt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	rec->inobt.ir_startino = cpu_to_be32(cur->bc_rec.i.ir_startino);
	if (xfs_sb_version_hassparseinodes(&cur->bc_mp->m_sb)) {
		rec->inobt.ir_u.sp.ir_holemask =
					cpu_to_be16(cur->bc_rec.i.ir_holemask);
		rec->inobt.ir_u.sp.ir_count = cur->bc_rec.i.ir_count;
		rec->inobt.ir_u.sp.ir_freecount = cur->bc_rec.i.ir_freecount;
	} else {
		/* ir_holemask/ir_count not supported on-disk */
		rec->inobt.ir_u.f.ir_freecount =
					cpu_to_be32(cur->bc_rec.i.ir_freecount);
	}
	rec->inobt.ir_free = cpu_to_be64(cur->bc_rec.i.ir_free);
}

/*
 * initial value of ptr for lookup
 */
STATIC void
xfs_inobt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agno == be32_to_cpu(agi->agi_seqno));

	ptr->s = agi->agi_root;
}

STATIC void
xfs_finobt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agno == be32_to_cpu(agi->agi_seqno));
	ptr->s = agi->agi_free_root;
}

STATIC __int64_t
xfs_inobt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	return (__int64_t)be32_to_cpu(key->inobt.ir_startino) -
			  cur->bc_rec.i.ir_startino;
}

static int
xfs_inobt_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;
	unsigned int		level;

	/*
	 * During growfs operations, we can't verify the exact owner as the
	 * perag is not fully initialised and hence not attached to the buffer.
	 *
	 * Similarly, during log recovery we will have a perag structure
	 * attached, but the agi information will not yet have been initialised
	 * from the on disk AGI. We don't currently use any of this information,
	 * but beware of the landmine (i.e. need to check pag->pagi_init) if we
	 * ever do.
	 */
	switch (block->bb_magic) {
	case cpu_to_be32(XFS_IBT_CRC_MAGIC):
	case cpu_to_be32(XFS_FIBT_CRC_MAGIC):
		if (!xfs_sb_version_hascrc(&mp->m_sb))
			return false;
		if (!uuid_equal(&block->bb_u.s.bb_uuid, &mp->m_sb.sb_uuid))
			return false;
		if (block->bb_u.s.bb_blkno != cpu_to_be64(bp->b_bn))
			return false;
		if (pag &&
		    be32_to_cpu(block->bb_u.s.bb_owner) != pag->pag_agno)
			return false;
		/* fall through */
	case cpu_to_be32(XFS_IBT_MAGIC):
	case cpu_to_be32(XFS_FIBT_MAGIC):
		break;
	default:
		return 0;
	}

	/* numrecs and level verification */
	level = be16_to_cpu(block->bb_level);
	if (level >= mp->m_in_maxlevels)
		return false;
	if (be16_to_cpu(block->bb_numrecs) > mp->m_inobt_mxr[level != 0])
		return false;

	/* sibling pointer verification */
	if (!block->bb_u.s.bb_leftsib ||
	    (be32_to_cpu(block->bb_u.s.bb_leftsib) >= mp->m_sb.sb_agblocks &&
	     block->bb_u.s.bb_leftsib != cpu_to_be32(NULLAGBLOCK)))
		return false;
	if (!block->bb_u.s.bb_rightsib ||
	    (be32_to_cpu(block->bb_u.s.bb_rightsib) >= mp->m_sb.sb_agblocks &&
	     block->bb_u.s.bb_rightsib != cpu_to_be32(NULLAGBLOCK)))
		return false;

	return true;
}

static void
xfs_inobt_read_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_btree_sblock_verify_crc(bp))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (!xfs_inobt_verify(bp))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_verifier_error(bp);
	}
}

static void
xfs_inobt_write_verify(
	struct xfs_buf	*bp)
{
	if (!xfs_inobt_verify(bp)) {
		trace_xfs_btree_corrupt(bp, _RET_IP_);
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}
	xfs_btree_sblock_calc_crc(bp);

}

const struct xfs_buf_ops xfs_inobt_buf_ops = {
	.verify_read = xfs_inobt_read_verify,
	.verify_write = xfs_inobt_write_verify,
};

#if defined(DEBUG) || defined(XFS_WARN)
STATIC int
xfs_inobt_keys_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*k1,
	union xfs_btree_key	*k2)
{
	return be32_to_cpu(k1->inobt.ir_startino) <
		be32_to_cpu(k2->inobt.ir_startino);
}

STATIC int
xfs_inobt_recs_inorder(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*r1,
	union xfs_btree_rec	*r2)
{
	return be32_to_cpu(r1->inobt.ir_startino) + XFS_INODES_PER_CHUNK <=
		be32_to_cpu(r2->inobt.ir_startino);
}
#endif	/* DEBUG */

static const struct xfs_btree_ops xfs_inobt_ops = {
	.rec_len		= sizeof(xfs_inobt_rec_t),
	.key_len		= sizeof(xfs_inobt_key_t),

	.dup_cursor		= xfs_inobt_dup_cursor,
	.set_root		= xfs_inobt_set_root,
	.alloc_block		= xfs_inobt_alloc_block,
	.free_block		= xfs_inobt_free_block,
	.get_minrecs		= xfs_inobt_get_minrecs,
	.get_maxrecs		= xfs_inobt_get_maxrecs,
	.init_key_from_rec	= xfs_inobt_init_key_from_rec,
	.init_rec_from_key	= xfs_inobt_init_rec_from_key,
	.init_rec_from_cur	= xfs_inobt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_inobt_init_ptr_from_cur,
	.key_diff		= xfs_inobt_key_diff,
	.buf_ops		= &xfs_inobt_buf_ops,
#if defined(DEBUG) || defined(XFS_WARN)
	.keys_inorder		= xfs_inobt_keys_inorder,
	.recs_inorder		= xfs_inobt_recs_inorder,
#endif
};

static const struct xfs_btree_ops xfs_finobt_ops = {
	.rec_len		= sizeof(xfs_inobt_rec_t),
	.key_len		= sizeof(xfs_inobt_key_t),

	.dup_cursor		= xfs_inobt_dup_cursor,
	.set_root		= xfs_finobt_set_root,
	.alloc_block		= xfs_inobt_alloc_block,
	.free_block		= xfs_inobt_free_block,
	.get_minrecs		= xfs_inobt_get_minrecs,
	.get_maxrecs		= xfs_inobt_get_maxrecs,
	.init_key_from_rec	= xfs_inobt_init_key_from_rec,
	.init_rec_from_key	= xfs_inobt_init_rec_from_key,
	.init_rec_from_cur	= xfs_inobt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_finobt_init_ptr_from_cur,
	.key_diff		= xfs_inobt_key_diff,
	.buf_ops		= &xfs_inobt_buf_ops,
#if defined(DEBUG) || defined(XFS_WARN)
	.keys_inorder		= xfs_inobt_keys_inorder,
	.recs_inorder		= xfs_inobt_recs_inorder,
#endif
};

/*
 * Allocate a new inode btree cursor.
 */
struct xfs_btree_cur *				/* new inode btree cursor */
xfs_inobt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_buf		*agbp,		/* buffer for agi structure */
	xfs_agnumber_t		agno,		/* allocation group number */
	xfs_btnum_t		btnum)		/* ialloc or free ino btree */
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	struct xfs_btree_cur	*cur;

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_SLEEP);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_btnum = btnum;
	if (btnum == XFS_BTNUM_INO) {
		cur->bc_nlevels = be32_to_cpu(agi->agi_level);
		cur->bc_ops = &xfs_inobt_ops;
	} else {
		cur->bc_nlevels = be32_to_cpu(agi->agi_free_level);
		cur->bc_ops = &xfs_finobt_ops;
	}

	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	if (xfs_sb_version_hascrc(&mp->m_sb))
		cur->bc_flags |= XFS_BTREE_CRC_BLOCKS;

	cur->bc_private.a.agbp = agbp;
	cur->bc_private.a.agno = agno;

	return cur;
}

/*
 * Calculate number of records in an inobt btree block.
 */
int
xfs_inobt_maxrecs(
	struct xfs_mount	*mp,
	int			blocklen,
	int			leaf)
{
	blocklen -= XFS_INOBT_BLOCK_LEN(mp);

	if (leaf)
		return blocklen / sizeof(xfs_inobt_rec_t);
	return blocklen / (sizeof(xfs_inobt_key_t) + sizeof(xfs_inobt_ptr_t));
}

/*
 * Convert the inode record holemask to an inode allocation bitmap. The inode
 * allocation bitmap is inode granularity and specifies whether an inode is
 * physically allocated on disk (not whether the inode is considered allocated
 * or free by the fs).
 *
 * A bit value of 1 means the inode is allocated, a value of 0 means it is free.
 */
uint64_t
xfs_inobt_irec_to_allocmask(
	struct xfs_inobt_rec_incore	*rec)
{
	uint64_t			bitmap = 0;
	uint64_t			inodespbit;
	int				nextbit;
	uint				allocbitmap;

	/*
	 * The holemask has 16-bits for a 64 inode record. Therefore each
	 * holemask bit represents multiple inodes. Create a mask of bits to set
	 * in the allocmask for each holemask bit.
	 */
	inodespbit = (1 << XFS_INODES_PER_HOLEMASK_BIT) - 1;

	/*
	 * Allocated inodes are represented by 0 bits in holemask. Invert the 0
	 * bits to 1 and convert to a uint so we can use xfs_next_bit(). Mask
	 * anything beyond the 16 holemask bits since this casts to a larger
	 * type.
	 */
	allocbitmap = ~rec->ir_holemask & ((1 << XFS_INOBT_HOLEMASK_BITS) - 1);

	/*
	 * allocbitmap is the inverted holemask so every set bit represents
	 * allocated inodes. To expand from 16-bit holemask granularity to
	 * 64-bit (e.g., bit-per-inode), set inodespbit bits in the target
	 * bitmap for every holemask bit.
	 */
	nextbit = xfs_next_bit(&allocbitmap, 1, 0);
	while (nextbit != -1) {
		ASSERT(nextbit < (sizeof(rec->ir_holemask) * NBBY));

		bitmap |= (inodespbit <<
			   (nextbit * XFS_INODES_PER_HOLEMASK_BIT));

		nextbit = xfs_next_bit(&allocbitmap, 1, nextbit + 1);
	}

	return bitmap;
}

#if defined(DEBUG) || defined(XFS_WARN)
/*
 * Verify that an in-core inode record has a valid inode count.
 */
int
xfs_inobt_rec_check_count(
	struct xfs_mount		*mp,
	struct xfs_inobt_rec_incore	*rec)
{
	int				inocount = 0;
	int				nextbit = 0;
	uint64_t			allocbmap;
	int				wordsz;

	wordsz = sizeof(allocbmap) / sizeof(unsigned int);
	allocbmap = xfs_inobt_irec_to_allocmask(rec);

	nextbit = xfs_next_bit((uint *) &allocbmap, wordsz, nextbit);
	while (nextbit != -1) {
		inocount++;
		nextbit = xfs_next_bit((uint *) &allocbmap, wordsz,
				       nextbit + 1);
	}

	if (inocount != rec->ir_count)
		return -EFSCORRUPTED;

	return 0;
}
#endif	/* DEBUG */
