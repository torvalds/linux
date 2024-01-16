// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_btree.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_alloc.h"
#include "xfs_log.h"
#include "xfs_btree_staging.h"
#include "xfs_ag.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount_btree.h"

/*
 * Btree magic numbers.
 */
static const uint32_t xfs_magics[2][XFS_BTNUM_MAX] = {
	{ XFS_ABTB_MAGIC, XFS_ABTC_MAGIC, 0, XFS_BMAP_MAGIC, XFS_IBT_MAGIC,
	  XFS_FIBT_MAGIC, 0 },
	{ XFS_ABTB_CRC_MAGIC, XFS_ABTC_CRC_MAGIC, XFS_RMAP_CRC_MAGIC,
	  XFS_BMAP_CRC_MAGIC, XFS_IBT_CRC_MAGIC, XFS_FIBT_CRC_MAGIC,
	  XFS_REFC_CRC_MAGIC }
};

uint32_t
xfs_btree_magic(
	int			crc,
	xfs_btnum_t		btnum)
{
	uint32_t		magic = xfs_magics[crc][btnum];

	/* Ensure we asked for crc for crc-only magics. */
	ASSERT(magic != 0);
	return magic;
}

/*
 * These sibling pointer checks are optimised for null sibling pointers. This
 * happens a lot, and we don't need to byte swap at runtime if the sibling
 * pointer is NULL.
 *
 * These are explicitly marked at inline because the cost of calling them as
 * functions instead of inlining them is about 36 bytes extra code per call site
 * on x86-64. Yes, gcc-11 fails to inline them, and explicit inlining of these
 * two sibling check functions reduces the compiled code size by over 300
 * bytes.
 */
static inline xfs_failaddr_t
xfs_btree_check_lblock_siblings(
	struct xfs_mount	*mp,
	struct xfs_btree_cur	*cur,
	int			level,
	xfs_fsblock_t		fsb,
	__be64			dsibling)
{
	xfs_fsblock_t		sibling;

	if (dsibling == cpu_to_be64(NULLFSBLOCK))
		return NULL;

	sibling = be64_to_cpu(dsibling);
	if (sibling == fsb)
		return __this_address;
	if (level >= 0) {
		if (!xfs_btree_check_lptr(cur, sibling, level + 1))
			return __this_address;
	} else {
		if (!xfs_verify_fsbno(mp, sibling))
			return __this_address;
	}

	return NULL;
}

static inline xfs_failaddr_t
xfs_btree_check_sblock_siblings(
	struct xfs_perag	*pag,
	struct xfs_btree_cur	*cur,
	int			level,
	xfs_agblock_t		agbno,
	__be32			dsibling)
{
	xfs_agblock_t		sibling;

	if (dsibling == cpu_to_be32(NULLAGBLOCK))
		return NULL;

	sibling = be32_to_cpu(dsibling);
	if (sibling == agbno)
		return __this_address;
	if (level >= 0) {
		if (!xfs_btree_check_sptr(cur, sibling, level + 1))
			return __this_address;
	} else {
		if (!xfs_verify_agbno(pag, sibling))
			return __this_address;
	}
	return NULL;
}

/*
 * Check a long btree block header.  Return the address of the failing check,
 * or NULL if everything is ok.
 */
xfs_failaddr_t
__xfs_btree_check_lblock(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	int			level,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	xfs_btnum_t		btnum = cur->bc_btnum;
	int			crc = xfs_has_crc(mp);
	xfs_failaddr_t		fa;
	xfs_fsblock_t		fsb = NULLFSBLOCK;

	if (crc) {
		if (!uuid_equal(&block->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid))
			return __this_address;
		if (block->bb_u.l.bb_blkno !=
		    cpu_to_be64(bp ? xfs_buf_daddr(bp) : XFS_BUF_DADDR_NULL))
			return __this_address;
		if (block->bb_u.l.bb_pad != cpu_to_be32(0))
			return __this_address;
	}

	if (be32_to_cpu(block->bb_magic) != xfs_btree_magic(crc, btnum))
		return __this_address;
	if (be16_to_cpu(block->bb_level) != level)
		return __this_address;
	if (be16_to_cpu(block->bb_numrecs) >
	    cur->bc_ops->get_maxrecs(cur, level))
		return __this_address;

	if (bp)
		fsb = XFS_DADDR_TO_FSB(mp, xfs_buf_daddr(bp));

	fa = xfs_btree_check_lblock_siblings(mp, cur, level, fsb,
			block->bb_u.l.bb_leftsib);
	if (!fa)
		fa = xfs_btree_check_lblock_siblings(mp, cur, level, fsb,
				block->bb_u.l.bb_rightsib);
	return fa;
}

/* Check a long btree block header. */
static int
xfs_btree_check_lblock(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	int			level,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	xfs_failaddr_t		fa;

	fa = __xfs_btree_check_lblock(cur, block, level, bp);
	if (XFS_IS_CORRUPT(mp, fa != NULL) ||
	    XFS_TEST_ERROR(false, mp, XFS_ERRTAG_BTREE_CHECK_LBLOCK)) {
		if (bp)
			trace_xfs_btree_corrupt(bp, _RET_IP_);
		return -EFSCORRUPTED;
	}
	return 0;
}

/*
 * Check a short btree block header.  Return the address of the failing check,
 * or NULL if everything is ok.
 */
xfs_failaddr_t
__xfs_btree_check_sblock(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	int			level,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	struct xfs_perag	*pag = cur->bc_ag.pag;
	xfs_btnum_t		btnum = cur->bc_btnum;
	int			crc = xfs_has_crc(mp);
	xfs_failaddr_t		fa;
	xfs_agblock_t		agbno = NULLAGBLOCK;

	if (crc) {
		if (!uuid_equal(&block->bb_u.s.bb_uuid, &mp->m_sb.sb_meta_uuid))
			return __this_address;
		if (block->bb_u.s.bb_blkno !=
		    cpu_to_be64(bp ? xfs_buf_daddr(bp) : XFS_BUF_DADDR_NULL))
			return __this_address;
	}

	if (be32_to_cpu(block->bb_magic) != xfs_btree_magic(crc, btnum))
		return __this_address;
	if (be16_to_cpu(block->bb_level) != level)
		return __this_address;
	if (be16_to_cpu(block->bb_numrecs) >
	    cur->bc_ops->get_maxrecs(cur, level))
		return __this_address;

	if (bp)
		agbno = xfs_daddr_to_agbno(mp, xfs_buf_daddr(bp));

	fa = xfs_btree_check_sblock_siblings(pag, cur, level, agbno,
			block->bb_u.s.bb_leftsib);
	if (!fa)
		fa = xfs_btree_check_sblock_siblings(pag, cur, level, agbno,
				block->bb_u.s.bb_rightsib);
	return fa;
}

/* Check a short btree block header. */
STATIC int
xfs_btree_check_sblock(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	int			level,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	xfs_failaddr_t		fa;

	fa = __xfs_btree_check_sblock(cur, block, level, bp);
	if (XFS_IS_CORRUPT(mp, fa != NULL) ||
	    XFS_TEST_ERROR(false, mp, XFS_ERRTAG_BTREE_CHECK_SBLOCK)) {
		if (bp)
			trace_xfs_btree_corrupt(bp, _RET_IP_);
		return -EFSCORRUPTED;
	}
	return 0;
}

/*
 * Debug routine: check that block header is ok.
 */
int
xfs_btree_check_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_btree_block	*block,	/* generic btree block pointer */
	int			level,	/* level of the btree block */
	struct xfs_buf		*bp)	/* buffer containing block, if any */
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return xfs_btree_check_lblock(cur, block, level, bp);
	else
		return xfs_btree_check_sblock(cur, block, level, bp);
}

/* Check that this long pointer is valid and points within the fs. */
bool
xfs_btree_check_lptr(
	struct xfs_btree_cur	*cur,
	xfs_fsblock_t		fsbno,
	int			level)
{
	if (level <= 0)
		return false;
	return xfs_verify_fsbno(cur->bc_mp, fsbno);
}

/* Check that this short pointer is valid and points within the AG. */
bool
xfs_btree_check_sptr(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		agbno,
	int			level)
{
	if (level <= 0)
		return false;
	return xfs_verify_agbno(cur->bc_ag.pag, agbno);
}

/*
 * Check that a given (indexed) btree pointer at a certain level of a
 * btree is valid and doesn't point past where it should.
 */
static int
xfs_btree_check_ptr(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	int				index,
	int				level)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (xfs_btree_check_lptr(cur, be64_to_cpu((&ptr->l)[index]),
				level))
			return 0;
		xfs_err(cur->bc_mp,
"Inode %llu fork %d: Corrupt btree %d pointer at level %d index %d.",
				cur->bc_ino.ip->i_ino,
				cur->bc_ino.whichfork, cur->bc_btnum,
				level, index);
	} else {
		if (xfs_btree_check_sptr(cur, be32_to_cpu((&ptr->s)[index]),
				level))
			return 0;
		xfs_err(cur->bc_mp,
"AG %u: Corrupt btree %d pointer at level %d index %d.",
				cur->bc_ag.pag->pag_agno, cur->bc_btnum,
				level, index);
	}

	return -EFSCORRUPTED;
}

#ifdef DEBUG
# define xfs_btree_debug_check_ptr	xfs_btree_check_ptr
#else
# define xfs_btree_debug_check_ptr(...)	(0)
#endif

/*
 * Calculate CRC on the whole btree block and stuff it into the
 * long-form btree header.
 *
 * Prior to calculting the CRC, pull the LSN out of the buffer log item and put
 * it into the buffer so recovery knows what the last modification was that made
 * it to disk.
 */
void
xfs_btree_lblock_calc_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	if (!xfs_has_crc(bp->b_mount))
		return;
	if (bip)
		block->bb_u.l.bb_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_BTREE_LBLOCK_CRC_OFF);
}

bool
xfs_btree_lblock_verify_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_mount	*mp = bp->b_mount;

	if (xfs_has_crc(mp)) {
		if (!xfs_log_check_lsn(mp, be64_to_cpu(block->bb_u.l.bb_lsn)))
			return false;
		return xfs_buf_verify_cksum(bp, XFS_BTREE_LBLOCK_CRC_OFF);
	}

	return true;
}

/*
 * Calculate CRC on the whole btree block and stuff it into the
 * short-form btree header.
 *
 * Prior to calculting the CRC, pull the LSN out of the buffer log item and put
 * it into the buffer so recovery knows what the last modification was that made
 * it to disk.
 */
void
xfs_btree_sblock_calc_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	if (!xfs_has_crc(bp->b_mount))
		return;
	if (bip)
		block->bb_u.s.bb_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_BTREE_SBLOCK_CRC_OFF);
}

bool
xfs_btree_sblock_verify_crc(
	struct xfs_buf		*bp)
{
	struct xfs_btree_block  *block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_mount	*mp = bp->b_mount;

	if (xfs_has_crc(mp)) {
		if (!xfs_log_check_lsn(mp, be64_to_cpu(block->bb_u.s.bb_lsn)))
			return false;
		return xfs_buf_verify_cksum(bp, XFS_BTREE_SBLOCK_CRC_OFF);
	}

	return true;
}

static int
xfs_btree_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	int			error;

	error = cur->bc_ops->free_block(cur, bp);
	if (!error) {
		xfs_trans_binval(cur->bc_tp, bp);
		XFS_BTREE_STATS_INC(cur, free);
	}
	return error;
}

/*
 * Delete the btree cursor.
 */
void
xfs_btree_del_cursor(
	struct xfs_btree_cur	*cur,		/* btree cursor */
	int			error)		/* del because of error */
{
	int			i;		/* btree level */

	/*
	 * Clear the buffer pointers and release the buffers. If we're doing
	 * this because of an error, inspect all of the entries in the bc_bufs
	 * array for buffers to be unlocked. This is because some of the btree
	 * code works from level n down to 0, and if we get an error along the
	 * way we won't have initialized all the entries down to 0.
	 */
	for (i = 0; i < cur->bc_nlevels; i++) {
		if (cur->bc_levels[i].bp)
			xfs_trans_brelse(cur->bc_tp, cur->bc_levels[i].bp);
		else if (!error)
			break;
	}

	/*
	 * If we are doing a BMBT update, the number of unaccounted blocks
	 * allocated during this cursor life time should be zero. If it's not
	 * zero, then we should be shut down or on our way to shutdown due to
	 * cancelling a dirty transaction on error.
	 */
	ASSERT(cur->bc_btnum != XFS_BTNUM_BMAP || cur->bc_ino.allocated == 0 ||
	       xfs_is_shutdown(cur->bc_mp) || error != 0);
	if (unlikely(cur->bc_flags & XFS_BTREE_STAGING))
		kmem_free(cur->bc_ops);
	if (!(cur->bc_flags & XFS_BTREE_LONG_PTRS) && cur->bc_ag.pag)
		xfs_perag_put(cur->bc_ag.pag);
	kmem_cache_free(cur->bc_cache, cur);
}

/*
 * Duplicate the btree cursor.
 * Allocate a new one, copy the record, re-get the buffers.
 */
int					/* error */
xfs_btree_dup_cursor(
	struct xfs_btree_cur *cur,		/* input cursor */
	struct xfs_btree_cur **ncur)		/* output cursor */
{
	struct xfs_buf	*bp;		/* btree block's buffer pointer */
	int		error;		/* error return value */
	int		i;		/* level number of btree block */
	xfs_mount_t	*mp;		/* mount structure for filesystem */
	struct xfs_btree_cur *new;		/* new cursor value */
	xfs_trans_t	*tp;		/* transaction pointer, can be NULL */

	tp = cur->bc_tp;
	mp = cur->bc_mp;

	/*
	 * Allocate a new cursor like the old one.
	 */
	new = cur->bc_ops->dup_cursor(cur);

	/*
	 * Copy the record currently in the cursor.
	 */
	new->bc_rec = cur->bc_rec;

	/*
	 * For each level current, re-get the buffer and copy the ptr value.
	 */
	for (i = 0; i < new->bc_nlevels; i++) {
		new->bc_levels[i].ptr = cur->bc_levels[i].ptr;
		new->bc_levels[i].ra = cur->bc_levels[i].ra;
		bp = cur->bc_levels[i].bp;
		if (bp) {
			error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
						   xfs_buf_daddr(bp), mp->m_bsize,
						   0, &bp,
						   cur->bc_ops->buf_ops);
			if (error) {
				xfs_btree_del_cursor(new, error);
				*ncur = NULL;
				return error;
			}
		}
		new->bc_levels[i].bp = bp;
	}
	*ncur = new;
	return 0;
}

/*
 * XFS btree block layout and addressing:
 *
 * There are two types of blocks in the btree: leaf and non-leaf blocks.
 *
 * The leaf record start with a header then followed by records containing
 * the values.  A non-leaf block also starts with the same header, and
 * then first contains lookup keys followed by an equal number of pointers
 * to the btree blocks at the previous level.
 *
 *		+--------+-------+-------+-------+-------+-------+-------+
 * Leaf:	| header | rec 1 | rec 2 | rec 3 | rec 4 | rec 5 | rec N |
 *		+--------+-------+-------+-------+-------+-------+-------+
 *
 *		+--------+-------+-------+-------+-------+-------+-------+
 * Non-Leaf:	| header | key 1 | key 2 | key N | ptr 1 | ptr 2 | ptr N |
 *		+--------+-------+-------+-------+-------+-------+-------+
 *
 * The header is called struct xfs_btree_block for reasons better left unknown
 * and comes in different versions for short (32bit) and long (64bit) block
 * pointers.  The record and key structures are defined by the btree instances
 * and opaque to the btree core.  The block pointers are simple disk endian
 * integers, available in a short (32bit) and long (64bit) variant.
 *
 * The helpers below calculate the offset of a given record, key or pointer
 * into a btree block (xfs_btree_*_offset) or return a pointer to the given
 * record, key or pointer (xfs_btree_*_addr).  Note that all addressing
 * inside the btree block is done using indices starting at one, not zero!
 *
 * If XFS_BTREE_OVERLAPPING is set, then this btree supports keys containing
 * overlapping intervals.  In such a tree, records are still sorted lowest to
 * highest and indexed by the smallest key value that refers to the record.
 * However, nodes are different: each pointer has two associated keys -- one
 * indexing the lowest key available in the block(s) below (the same behavior
 * as the key in a regular btree) and another indexing the highest key
 * available in the block(s) below.  Because records are /not/ sorted by the
 * highest key, all leaf block updates require us to compute the highest key
 * that matches any record in the leaf and to recursively update the high keys
 * in the nodes going further up in the tree, if necessary.  Nodes look like
 * this:
 *
 *		+--------+-----+-----+-----+-----+-----+-------+-------+-----+
 * Non-Leaf:	| header | lo1 | hi1 | lo2 | hi2 | ... | ptr 1 | ptr 2 | ... |
 *		+--------+-----+-----+-----+-----+-----+-------+-------+-----+
 *
 * To perform an interval query on an overlapped tree, perform the usual
 * depth-first search and use the low and high keys to decide if we can skip
 * that particular node.  If a leaf node is reached, return the records that
 * intersect the interval.  Note that an interval query may return numerous
 * entries.  For a non-overlapped tree, simply search for the record associated
 * with the lowest key and iterate forward until a non-matching record is
 * found.  Section 14.3 ("Interval Trees") of _Introduction to Algorithms_ by
 * Cormen, Leiserson, Rivest, and Stein (2nd or 3rd ed. only) discuss this in
 * more detail.
 *
 * Why do we care about overlapping intervals?  Let's say you have a bunch of
 * reverse mapping records on a reflink filesystem:
 *
 * 1: +- file A startblock B offset C length D -----------+
 * 2:      +- file E startblock F offset G length H --------------+
 * 3:      +- file I startblock F offset J length K --+
 * 4:                                                        +- file L... --+
 *
 * Now say we want to map block (B+D) into file A at offset (C+D).  Ideally,
 * we'd simply increment the length of record 1.  But how do we find the record
 * that ends at (B+D-1) (i.e. record 1)?  A LE lookup of (B+D-1) would return
 * record 3 because the keys are ordered first by startblock.  An interval
 * query would return records 1 and 2 because they both overlap (B+D-1), and
 * from that we can pick out record 1 as the appropriate left neighbor.
 *
 * In the non-overlapped case you can do a LE lookup and decrement the cursor
 * because a record's interval must end before the next record.
 */

/*
 * Return size of the btree block header for this btree instance.
 */
static inline size_t xfs_btree_block_len(struct xfs_btree_cur *cur)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS)
			return XFS_BTREE_LBLOCK_CRC_LEN;
		return XFS_BTREE_LBLOCK_LEN;
	}
	if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS)
		return XFS_BTREE_SBLOCK_CRC_LEN;
	return XFS_BTREE_SBLOCK_LEN;
}

/*
 * Return size of btree block pointers for this btree instance.
 */
static inline size_t xfs_btree_ptr_len(struct xfs_btree_cur *cur)
{
	return (cur->bc_flags & XFS_BTREE_LONG_PTRS) ?
		sizeof(__be64) : sizeof(__be32);
}

/*
 * Calculate offset of the n-th record in a btree block.
 */
STATIC size_t
xfs_btree_rec_offset(
	struct xfs_btree_cur	*cur,
	int			n)
{
	return xfs_btree_block_len(cur) +
		(n - 1) * cur->bc_ops->rec_len;
}

/*
 * Calculate offset of the n-th key in a btree block.
 */
STATIC size_t
xfs_btree_key_offset(
	struct xfs_btree_cur	*cur,
	int			n)
{
	return xfs_btree_block_len(cur) +
		(n - 1) * cur->bc_ops->key_len;
}

/*
 * Calculate offset of the n-th high key in a btree block.
 */
STATIC size_t
xfs_btree_high_key_offset(
	struct xfs_btree_cur	*cur,
	int			n)
{
	return xfs_btree_block_len(cur) +
		(n - 1) * cur->bc_ops->key_len + (cur->bc_ops->key_len / 2);
}

/*
 * Calculate offset of the n-th block pointer in a btree block.
 */
STATIC size_t
xfs_btree_ptr_offset(
	struct xfs_btree_cur	*cur,
	int			n,
	int			level)
{
	return xfs_btree_block_len(cur) +
		cur->bc_ops->get_maxrecs(cur, level) * cur->bc_ops->key_len +
		(n - 1) * xfs_btree_ptr_len(cur);
}

/*
 * Return a pointer to the n-th record in the btree block.
 */
union xfs_btree_rec *
xfs_btree_rec_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	return (union xfs_btree_rec *)
		((char *)block + xfs_btree_rec_offset(cur, n));
}

/*
 * Return a pointer to the n-th key in the btree block.
 */
union xfs_btree_key *
xfs_btree_key_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	return (union xfs_btree_key *)
		((char *)block + xfs_btree_key_offset(cur, n));
}

/*
 * Return a pointer to the n-th high key in the btree block.
 */
union xfs_btree_key *
xfs_btree_high_key_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	return (union xfs_btree_key *)
		((char *)block + xfs_btree_high_key_offset(cur, n));
}

/*
 * Return a pointer to the n-th block pointer in the btree block.
 */
union xfs_btree_ptr *
xfs_btree_ptr_addr(
	struct xfs_btree_cur	*cur,
	int			n,
	struct xfs_btree_block	*block)
{
	int			level = xfs_btree_get_level(block);

	ASSERT(block->bb_level != 0);

	return (union xfs_btree_ptr *)
		((char *)block + xfs_btree_ptr_offset(cur, n, level));
}

struct xfs_ifork *
xfs_btree_ifork_ptr(
	struct xfs_btree_cur	*cur)
{
	ASSERT(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE);

	if (cur->bc_flags & XFS_BTREE_STAGING)
		return cur->bc_ino.ifake->if_fork;
	return xfs_ifork_ptr(cur->bc_ino.ip, cur->bc_ino.whichfork);
}

/*
 * Get the root block which is stored in the inode.
 *
 * For now this btree implementation assumes the btree root is always
 * stored in the if_broot field of an inode fork.
 */
STATIC struct xfs_btree_block *
xfs_btree_get_iroot(
	struct xfs_btree_cur	*cur)
{
	struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);

	return (struct xfs_btree_block *)ifp->if_broot;
}

/*
 * Retrieve the block pointer from the cursor at the given level.
 * This may be an inode btree root or from a buffer.
 */
struct xfs_btree_block *		/* generic btree block pointer */
xfs_btree_get_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level,	/* level in btree */
	struct xfs_buf		**bpp)	/* buffer containing the block */
{
	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (level == cur->bc_nlevels - 1)) {
		*bpp = NULL;
		return xfs_btree_get_iroot(cur);
	}

	*bpp = cur->bc_levels[level].bp;
	return XFS_BUF_TO_BLOCK(*bpp);
}

/*
 * Change the cursor to point to the first record at the given level.
 * Other levels are unaffected.
 */
STATIC int				/* success=1, failure=0 */
xfs_btree_firstrec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level)	/* level to change */
{
	struct xfs_btree_block	*block;	/* generic btree block pointer */
	struct xfs_buf		*bp;	/* buffer containing block */

	/*
	 * Get the block pointer for this level.
	 */
	block = xfs_btree_get_block(cur, level, &bp);
	if (xfs_btree_check_block(cur, block, level, bp))
		return 0;
	/*
	 * It's empty, there is no such record.
	 */
	if (!block->bb_numrecs)
		return 0;
	/*
	 * Set the ptr value to 1, that's the first record/key.
	 */
	cur->bc_levels[level].ptr = 1;
	return 1;
}

/*
 * Change the cursor to point to the last record in the current block
 * at the given level.  Other levels are unaffected.
 */
STATIC int				/* success=1, failure=0 */
xfs_btree_lastrec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level)	/* level to change */
{
	struct xfs_btree_block	*block;	/* generic btree block pointer */
	struct xfs_buf		*bp;	/* buffer containing block */

	/*
	 * Get the block pointer for this level.
	 */
	block = xfs_btree_get_block(cur, level, &bp);
	if (xfs_btree_check_block(cur, block, level, bp))
		return 0;
	/*
	 * It's empty, there is no such record.
	 */
	if (!block->bb_numrecs)
		return 0;
	/*
	 * Set the ptr value to numrecs, that's the last record/key.
	 */
	cur->bc_levels[level].ptr = be16_to_cpu(block->bb_numrecs);
	return 1;
}

/*
 * Compute first and last byte offsets for the fields given.
 * Interprets the offsets table, which contains struct field offsets.
 */
void
xfs_btree_offsets(
	uint32_t	fields,		/* bitmask of fields */
	const short	*offsets,	/* table of field offsets */
	int		nbits,		/* number of bits to inspect */
	int		*first,		/* output: first byte offset */
	int		*last)		/* output: last byte offset */
{
	int		i;		/* current bit number */
	uint32_t	imask;		/* mask for current bit number */

	ASSERT(fields != 0);
	/*
	 * Find the lowest bit, so the first byte offset.
	 */
	for (i = 0, imask = 1u; ; i++, imask <<= 1) {
		if (imask & fields) {
			*first = offsets[i];
			break;
		}
	}
	/*
	 * Find the highest bit, so the last byte offset.
	 */
	for (i = nbits - 1, imask = 1u << i; ; i--, imask >>= 1) {
		if (imask & fields) {
			*last = offsets[i + 1] - 1;
			break;
		}
	}
}

/*
 * Get a buffer for the block, return it read in.
 * Long-form addressing.
 */
int
xfs_btree_read_bufl(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	xfs_fsblock_t		fsbno,		/* file system block number */
	struct xfs_buf		**bpp,		/* buffer for fsbno */
	int			refval,		/* ref count value for buffer */
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;		/* return value */
	xfs_daddr_t		d;		/* real disk block address */
	int			error;

	if (!xfs_verify_fsbno(mp, fsbno))
		return -EFSCORRUPTED;
	d = XFS_FSB_TO_DADDR(mp, fsbno);
	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp, d,
				   mp->m_bsize, 0, &bp, ops);
	if (error)
		return error;
	if (bp)
		xfs_buf_set_ref(bp, refval);
	*bpp = bp;
	return 0;
}

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Long-form addressing.
 */
/* ARGSUSED */
void
xfs_btree_reada_bufl(
	struct xfs_mount	*mp,		/* file system mount point */
	xfs_fsblock_t		fsbno,		/* file system block number */
	xfs_extlen_t		count,		/* count of filesystem blocks */
	const struct xfs_buf_ops *ops)
{
	xfs_daddr_t		d;

	ASSERT(fsbno != NULLFSBLOCK);
	d = XFS_FSB_TO_DADDR(mp, fsbno);
	xfs_buf_readahead(mp->m_ddev_targp, d, mp->m_bsize * count, ops);
}

/*
 * Read-ahead the block, don't wait for it, don't return a buffer.
 * Short-form addressing.
 */
/* ARGSUSED */
void
xfs_btree_reada_bufs(
	struct xfs_mount	*mp,		/* file system mount point */
	xfs_agnumber_t		agno,		/* allocation group number */
	xfs_agblock_t		agbno,		/* allocation group block number */
	xfs_extlen_t		count,		/* count of filesystem blocks */
	const struct xfs_buf_ops *ops)
{
	xfs_daddr_t		d;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agbno != NULLAGBLOCK);
	d = XFS_AGB_TO_DADDR(mp, agno, agbno);
	xfs_buf_readahead(mp->m_ddev_targp, d, mp->m_bsize * count, ops);
}

STATIC int
xfs_btree_readahead_lblock(
	struct xfs_btree_cur	*cur,
	int			lr,
	struct xfs_btree_block	*block)
{
	int			rval = 0;
	xfs_fsblock_t		left = be64_to_cpu(block->bb_u.l.bb_leftsib);
	xfs_fsblock_t		right = be64_to_cpu(block->bb_u.l.bb_rightsib);

	if ((lr & XFS_BTCUR_LEFTRA) && left != NULLFSBLOCK) {
		xfs_btree_reada_bufl(cur->bc_mp, left, 1,
				     cur->bc_ops->buf_ops);
		rval++;
	}

	if ((lr & XFS_BTCUR_RIGHTRA) && right != NULLFSBLOCK) {
		xfs_btree_reada_bufl(cur->bc_mp, right, 1,
				     cur->bc_ops->buf_ops);
		rval++;
	}

	return rval;
}

STATIC int
xfs_btree_readahead_sblock(
	struct xfs_btree_cur	*cur,
	int			lr,
	struct xfs_btree_block *block)
{
	int			rval = 0;
	xfs_agblock_t		left = be32_to_cpu(block->bb_u.s.bb_leftsib);
	xfs_agblock_t		right = be32_to_cpu(block->bb_u.s.bb_rightsib);


	if ((lr & XFS_BTCUR_LEFTRA) && left != NULLAGBLOCK) {
		xfs_btree_reada_bufs(cur->bc_mp, cur->bc_ag.pag->pag_agno,
				     left, 1, cur->bc_ops->buf_ops);
		rval++;
	}

	if ((lr & XFS_BTCUR_RIGHTRA) && right != NULLAGBLOCK) {
		xfs_btree_reada_bufs(cur->bc_mp, cur->bc_ag.pag->pag_agno,
				     right, 1, cur->bc_ops->buf_ops);
		rval++;
	}

	return rval;
}

/*
 * Read-ahead btree blocks, at the given level.
 * Bits in lr are set from XFS_BTCUR_{LEFT,RIGHT}RA.
 */
STATIC int
xfs_btree_readahead(
	struct xfs_btree_cur	*cur,		/* btree cursor */
	int			lev,		/* level in btree */
	int			lr)		/* left/right bits */
{
	struct xfs_btree_block	*block;

	/*
	 * No readahead needed if we are at the root level and the
	 * btree root is stored in the inode.
	 */
	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (lev == cur->bc_nlevels - 1))
		return 0;

	if ((cur->bc_levels[lev].ra | lr) == cur->bc_levels[lev].ra)
		return 0;

	cur->bc_levels[lev].ra |= lr;
	block = XFS_BUF_TO_BLOCK(cur->bc_levels[lev].bp);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return xfs_btree_readahead_lblock(cur, lr, block);
	return xfs_btree_readahead_sblock(cur, lr, block);
}

STATIC int
xfs_btree_ptr_to_daddr(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	xfs_daddr_t			*daddr)
{
	xfs_fsblock_t		fsbno;
	xfs_agblock_t		agbno;
	int			error;

	error = xfs_btree_check_ptr(cur, ptr, 0, 1);
	if (error)
		return error;

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		fsbno = be64_to_cpu(ptr->l);
		*daddr = XFS_FSB_TO_DADDR(cur->bc_mp, fsbno);
	} else {
		agbno = be32_to_cpu(ptr->s);
		*daddr = XFS_AGB_TO_DADDR(cur->bc_mp, cur->bc_ag.pag->pag_agno,
				agbno);
	}

	return 0;
}

/*
 * Readahead @count btree blocks at the given @ptr location.
 *
 * We don't need to care about long or short form btrees here as we have a
 * method of converting the ptr directly to a daddr available to us.
 */
STATIC void
xfs_btree_readahead_ptr(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	xfs_extlen_t		count)
{
	xfs_daddr_t		daddr;

	if (xfs_btree_ptr_to_daddr(cur, ptr, &daddr))
		return;
	xfs_buf_readahead(cur->bc_mp->m_ddev_targp, daddr,
			  cur->bc_mp->m_bsize * count, cur->bc_ops->buf_ops);
}

/*
 * Set the buffer for level "lev" in the cursor to bp, releasing
 * any previous buffer.
 */
STATIC void
xfs_btree_setbuf(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			lev,	/* level in btree */
	struct xfs_buf		*bp)	/* new buffer to set */
{
	struct xfs_btree_block	*b;	/* btree block */

	if (cur->bc_levels[lev].bp)
		xfs_trans_brelse(cur->bc_tp, cur->bc_levels[lev].bp);
	cur->bc_levels[lev].bp = bp;
	cur->bc_levels[lev].ra = 0;

	b = XFS_BUF_TO_BLOCK(bp);
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (b->bb_u.l.bb_leftsib == cpu_to_be64(NULLFSBLOCK))
			cur->bc_levels[lev].ra |= XFS_BTCUR_LEFTRA;
		if (b->bb_u.l.bb_rightsib == cpu_to_be64(NULLFSBLOCK))
			cur->bc_levels[lev].ra |= XFS_BTCUR_RIGHTRA;
	} else {
		if (b->bb_u.s.bb_leftsib == cpu_to_be32(NULLAGBLOCK))
			cur->bc_levels[lev].ra |= XFS_BTCUR_LEFTRA;
		if (b->bb_u.s.bb_rightsib == cpu_to_be32(NULLAGBLOCK))
			cur->bc_levels[lev].ra |= XFS_BTCUR_RIGHTRA;
	}
}

bool
xfs_btree_ptr_is_null(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return ptr->l == cpu_to_be64(NULLFSBLOCK);
	else
		return ptr->s == cpu_to_be32(NULLAGBLOCK);
}

void
xfs_btree_set_ptr_null(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		ptr->l = cpu_to_be64(NULLFSBLOCK);
	else
		ptr->s = cpu_to_be32(NULLAGBLOCK);
}

/*
 * Get/set/init sibling pointers
 */
void
xfs_btree_get_sibling(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_ptr	*ptr,
	int			lr)
{
	ASSERT(lr == XFS_BB_LEFTSIB || lr == XFS_BB_RIGHTSIB);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (lr == XFS_BB_RIGHTSIB)
			ptr->l = block->bb_u.l.bb_rightsib;
		else
			ptr->l = block->bb_u.l.bb_leftsib;
	} else {
		if (lr == XFS_BB_RIGHTSIB)
			ptr->s = block->bb_u.s.bb_rightsib;
		else
			ptr->s = block->bb_u.s.bb_leftsib;
	}
}

void
xfs_btree_set_sibling(
	struct xfs_btree_cur		*cur,
	struct xfs_btree_block		*block,
	const union xfs_btree_ptr	*ptr,
	int				lr)
{
	ASSERT(lr == XFS_BB_LEFTSIB || lr == XFS_BB_RIGHTSIB);

	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (lr == XFS_BB_RIGHTSIB)
			block->bb_u.l.bb_rightsib = ptr->l;
		else
			block->bb_u.l.bb_leftsib = ptr->l;
	} else {
		if (lr == XFS_BB_RIGHTSIB)
			block->bb_u.s.bb_rightsib = ptr->s;
		else
			block->bb_u.s.bb_leftsib = ptr->s;
	}
}

void
xfs_btree_init_block_int(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*buf,
	xfs_daddr_t		blkno,
	xfs_btnum_t		btnum,
	__u16			level,
	__u16			numrecs,
	__u64			owner,
	unsigned int		flags)
{
	int			crc = xfs_has_crc(mp);
	__u32			magic = xfs_btree_magic(crc, btnum);

	buf->bb_magic = cpu_to_be32(magic);
	buf->bb_level = cpu_to_be16(level);
	buf->bb_numrecs = cpu_to_be16(numrecs);

	if (flags & XFS_BTREE_LONG_PTRS) {
		buf->bb_u.l.bb_leftsib = cpu_to_be64(NULLFSBLOCK);
		buf->bb_u.l.bb_rightsib = cpu_to_be64(NULLFSBLOCK);
		if (crc) {
			buf->bb_u.l.bb_blkno = cpu_to_be64(blkno);
			buf->bb_u.l.bb_owner = cpu_to_be64(owner);
			uuid_copy(&buf->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid);
			buf->bb_u.l.bb_pad = 0;
			buf->bb_u.l.bb_lsn = 0;
		}
	} else {
		/* owner is a 32 bit value on short blocks */
		__u32 __owner = (__u32)owner;

		buf->bb_u.s.bb_leftsib = cpu_to_be32(NULLAGBLOCK);
		buf->bb_u.s.bb_rightsib = cpu_to_be32(NULLAGBLOCK);
		if (crc) {
			buf->bb_u.s.bb_blkno = cpu_to_be64(blkno);
			buf->bb_u.s.bb_owner = cpu_to_be32(__owner);
			uuid_copy(&buf->bb_u.s.bb_uuid, &mp->m_sb.sb_meta_uuid);
			buf->bb_u.s.bb_lsn = 0;
		}
	}
}

void
xfs_btree_init_block(
	struct xfs_mount *mp,
	struct xfs_buf	*bp,
	xfs_btnum_t	btnum,
	__u16		level,
	__u16		numrecs,
	__u64		owner)
{
	xfs_btree_init_block_int(mp, XFS_BUF_TO_BLOCK(bp), xfs_buf_daddr(bp),
				 btnum, level, numrecs, owner, 0);
}

void
xfs_btree_init_block_cur(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			level,
	int			numrecs)
{
	__u64			owner;

	/*
	 * we can pull the owner from the cursor right now as the different
	 * owners align directly with the pointer size of the btree. This may
	 * change in future, but is safe for current users of the generic btree
	 * code.
	 */
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		owner = cur->bc_ino.ip->i_ino;
	else
		owner = cur->bc_ag.pag->pag_agno;

	xfs_btree_init_block_int(cur->bc_mp, XFS_BUF_TO_BLOCK(bp),
				xfs_buf_daddr(bp), cur->bc_btnum, level,
				numrecs, owner, cur->bc_flags);
}

/*
 * Return true if ptr is the last record in the btree and
 * we need to track updates to this record.  The decision
 * will be further refined in the update_lastrec method.
 */
STATIC int
xfs_btree_is_lastrec(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	int			level)
{
	union xfs_btree_ptr	ptr;

	if (level > 0)
		return 0;
	if (!(cur->bc_flags & XFS_BTREE_LASTREC_UPDATE))
		return 0;

	xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_RIGHTSIB);
	if (!xfs_btree_ptr_is_null(cur, &ptr))
		return 0;
	return 1;
}

STATIC void
xfs_btree_buf_to_ptr(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	union xfs_btree_ptr	*ptr)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		ptr->l = cpu_to_be64(XFS_DADDR_TO_FSB(cur->bc_mp,
					xfs_buf_daddr(bp)));
	else {
		ptr->s = cpu_to_be32(xfs_daddr_to_agbno(cur->bc_mp,
					xfs_buf_daddr(bp)));
	}
}

STATIC void
xfs_btree_set_refs(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	switch (cur->bc_btnum) {
	case XFS_BTNUM_BNO:
	case XFS_BTNUM_CNT:
		xfs_buf_set_ref(bp, XFS_ALLOC_BTREE_REF);
		break;
	case XFS_BTNUM_INO:
	case XFS_BTNUM_FINO:
		xfs_buf_set_ref(bp, XFS_INO_BTREE_REF);
		break;
	case XFS_BTNUM_BMAP:
		xfs_buf_set_ref(bp, XFS_BMAP_BTREE_REF);
		break;
	case XFS_BTNUM_RMAP:
		xfs_buf_set_ref(bp, XFS_RMAP_BTREE_REF);
		break;
	case XFS_BTNUM_REFC:
		xfs_buf_set_ref(bp, XFS_REFC_BTREE_REF);
		break;
	default:
		ASSERT(0);
	}
}

int
xfs_btree_get_buf_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	struct xfs_btree_block		**block,
	struct xfs_buf			**bpp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	xfs_daddr_t		d;
	int			error;

	error = xfs_btree_ptr_to_daddr(cur, ptr, &d);
	if (error)
		return error;
	error = xfs_trans_get_buf(cur->bc_tp, mp->m_ddev_targp, d, mp->m_bsize,
			0, bpp);
	if (error)
		return error;

	(*bpp)->b_ops = cur->bc_ops->buf_ops;
	*block = XFS_BUF_TO_BLOCK(*bpp);
	return 0;
}

/*
 * Read in the buffer at the given ptr and return the buffer and
 * the block pointer within the buffer.
 */
STATIC int
xfs_btree_read_buf_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	int				flags,
	struct xfs_btree_block		**block,
	struct xfs_buf			**bpp)
{
	struct xfs_mount	*mp = cur->bc_mp;
	xfs_daddr_t		d;
	int			error;

	/* need to sort out how callers deal with failures first */
	ASSERT(!(flags & XBF_TRYLOCK));

	error = xfs_btree_ptr_to_daddr(cur, ptr, &d);
	if (error)
		return error;
	error = xfs_trans_read_buf(mp, cur->bc_tp, mp->m_ddev_targp, d,
				   mp->m_bsize, flags, bpp,
				   cur->bc_ops->buf_ops);
	if (error)
		return error;

	xfs_btree_set_refs(cur, *bpp);
	*block = XFS_BUF_TO_BLOCK(*bpp);
	return 0;
}

/*
 * Copy keys from one btree block to another.
 */
void
xfs_btree_copy_keys(
	struct xfs_btree_cur		*cur,
	union xfs_btree_key		*dst_key,
	const union xfs_btree_key	*src_key,
	int				numkeys)
{
	ASSERT(numkeys >= 0);
	memcpy(dst_key, src_key, numkeys * cur->bc_ops->key_len);
}

/*
 * Copy records from one btree block to another.
 */
STATIC void
xfs_btree_copy_recs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*dst_rec,
	union xfs_btree_rec	*src_rec,
	int			numrecs)
{
	ASSERT(numrecs >= 0);
	memcpy(dst_rec, src_rec, numrecs * cur->bc_ops->rec_len);
}

/*
 * Copy block pointers from one btree block to another.
 */
void
xfs_btree_copy_ptrs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*dst_ptr,
	const union xfs_btree_ptr *src_ptr,
	int			numptrs)
{
	ASSERT(numptrs >= 0);
	memcpy(dst_ptr, src_ptr, numptrs * xfs_btree_ptr_len(cur));
}

/*
 * Shift keys one index left/right inside a single btree block.
 */
STATIC void
xfs_btree_shift_keys(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key,
	int			dir,
	int			numkeys)
{
	char			*dst_key;

	ASSERT(numkeys >= 0);
	ASSERT(dir == 1 || dir == -1);

	dst_key = (char *)key + (dir * cur->bc_ops->key_len);
	memmove(dst_key, key, numkeys * cur->bc_ops->key_len);
}

/*
 * Shift records one index left/right inside a single btree block.
 */
STATIC void
xfs_btree_shift_recs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec,
	int			dir,
	int			numrecs)
{
	char			*dst_rec;

	ASSERT(numrecs >= 0);
	ASSERT(dir == 1 || dir == -1);

	dst_rec = (char *)rec + (dir * cur->bc_ops->rec_len);
	memmove(dst_rec, rec, numrecs * cur->bc_ops->rec_len);
}

/*
 * Shift block pointers one index left/right inside a single btree block.
 */
STATIC void
xfs_btree_shift_ptrs(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			dir,
	int			numptrs)
{
	char			*dst_ptr;

	ASSERT(numptrs >= 0);
	ASSERT(dir == 1 || dir == -1);

	dst_ptr = (char *)ptr + (dir * xfs_btree_ptr_len(cur));
	memmove(dst_ptr, ptr, numptrs * xfs_btree_ptr_len(cur));
}

/*
 * Log key values from the btree block.
 */
STATIC void
xfs_btree_log_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			first,
	int			last)
{

	if (bp) {
		xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
		xfs_trans_log_buf(cur->bc_tp, bp,
				  xfs_btree_key_offset(cur, first),
				  xfs_btree_key_offset(cur, last + 1) - 1);
	} else {
		xfs_trans_log_inode(cur->bc_tp, cur->bc_ino.ip,
				xfs_ilog_fbroot(cur->bc_ino.whichfork));
	}
}

/*
 * Log record values from the btree block.
 */
void
xfs_btree_log_recs(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			first,
	int			last)
{

	xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
	xfs_trans_log_buf(cur->bc_tp, bp,
			  xfs_btree_rec_offset(cur, first),
			  xfs_btree_rec_offset(cur, last + 1) - 1);

}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_btree_log_ptrs(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_buf		*bp,	/* buffer containing btree block */
	int			first,	/* index of first pointer to log */
	int			last)	/* index of last pointer to log */
{

	if (bp) {
		struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
		int			level = xfs_btree_get_level(block);

		xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
		xfs_trans_log_buf(cur->bc_tp, bp,
				xfs_btree_ptr_offset(cur, first, level),
				xfs_btree_ptr_offset(cur, last + 1, level) - 1);
	} else {
		xfs_trans_log_inode(cur->bc_tp, cur->bc_ino.ip,
			xfs_ilog_fbroot(cur->bc_ino.whichfork));
	}

}

/*
 * Log fields from a btree block header.
 */
void
xfs_btree_log_block(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	struct xfs_buf		*bp,	/* buffer containing btree block */
	uint32_t		fields)	/* mask of fields: XFS_BB_... */
{
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	static const short	soffsets[] = {	/* table of offsets (short) */
		offsetof(struct xfs_btree_block, bb_magic),
		offsetof(struct xfs_btree_block, bb_level),
		offsetof(struct xfs_btree_block, bb_numrecs),
		offsetof(struct xfs_btree_block, bb_u.s.bb_leftsib),
		offsetof(struct xfs_btree_block, bb_u.s.bb_rightsib),
		offsetof(struct xfs_btree_block, bb_u.s.bb_blkno),
		offsetof(struct xfs_btree_block, bb_u.s.bb_lsn),
		offsetof(struct xfs_btree_block, bb_u.s.bb_uuid),
		offsetof(struct xfs_btree_block, bb_u.s.bb_owner),
		offsetof(struct xfs_btree_block, bb_u.s.bb_crc),
		XFS_BTREE_SBLOCK_CRC_LEN
	};
	static const short	loffsets[] = {	/* table of offsets (long) */
		offsetof(struct xfs_btree_block, bb_magic),
		offsetof(struct xfs_btree_block, bb_level),
		offsetof(struct xfs_btree_block, bb_numrecs),
		offsetof(struct xfs_btree_block, bb_u.l.bb_leftsib),
		offsetof(struct xfs_btree_block, bb_u.l.bb_rightsib),
		offsetof(struct xfs_btree_block, bb_u.l.bb_blkno),
		offsetof(struct xfs_btree_block, bb_u.l.bb_lsn),
		offsetof(struct xfs_btree_block, bb_u.l.bb_uuid),
		offsetof(struct xfs_btree_block, bb_u.l.bb_owner),
		offsetof(struct xfs_btree_block, bb_u.l.bb_crc),
		offsetof(struct xfs_btree_block, bb_u.l.bb_pad),
		XFS_BTREE_LBLOCK_CRC_LEN
	};

	if (bp) {
		int nbits;

		if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS) {
			/*
			 * We don't log the CRC when updating a btree
			 * block but instead recreate it during log
			 * recovery.  As the log buffers have checksums
			 * of their own this is safe and avoids logging a crc
			 * update in a lot of places.
			 */
			if (fields == XFS_BB_ALL_BITS)
				fields = XFS_BB_ALL_BITS_CRC;
			nbits = XFS_BB_NUM_BITS_CRC;
		} else {
			nbits = XFS_BB_NUM_BITS;
		}
		xfs_btree_offsets(fields,
				  (cur->bc_flags & XFS_BTREE_LONG_PTRS) ?
					loffsets : soffsets,
				  nbits, &first, &last);
		xfs_trans_buf_set_type(cur->bc_tp, bp, XFS_BLFT_BTREE_BUF);
		xfs_trans_log_buf(cur->bc_tp, bp, first, last);
	} else {
		xfs_trans_log_inode(cur->bc_tp, cur->bc_ino.ip,
			xfs_ilog_fbroot(cur->bc_ino.whichfork));
	}
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int						/* error */
xfs_btree_increment(
	struct xfs_btree_cur	*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	struct xfs_btree_block	*block;
	union xfs_btree_ptr	ptr;
	struct xfs_buf		*bp;
	int			error;		/* error return value */
	int			lev;

	ASSERT(level < cur->bc_nlevels);

	/* Read-ahead to the right at this level. */
	xfs_btree_readahead(cur, level, XFS_BTCUR_RIGHTRA);

	/* Get a pointer to the btree block. */
	block = xfs_btree_get_block(cur, level, &bp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, level, bp);
	if (error)
		goto error0;
#endif

	/* We're done if we remain in the block after the increment. */
	if (++cur->bc_levels[level].ptr <= xfs_btree_get_numrecs(block))
		goto out1;

	/* Fail if we just went off the right edge of the tree. */
	xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_RIGHTSIB);
	if (xfs_btree_ptr_is_null(cur, &ptr))
		goto out0;

	XFS_BTREE_STATS_INC(cur, increment);

	/*
	 * March up the tree incrementing pointers.
	 * Stop when we don't go off the right edge of a block.
	 */
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		block = xfs_btree_get_block(cur, lev, &bp);

#ifdef DEBUG
		error = xfs_btree_check_block(cur, block, lev, bp);
		if (error)
			goto error0;
#endif

		if (++cur->bc_levels[lev].ptr <= xfs_btree_get_numrecs(block))
			break;

		/* Read-ahead the right block for the next loop. */
		xfs_btree_readahead(cur, lev, XFS_BTCUR_RIGHTRA);
	}

	/*
	 * If we went off the root then we are either seriously
	 * confused or have the tree root in an inode.
	 */
	if (lev == cur->bc_nlevels) {
		if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE)
			goto out0;
		ASSERT(0);
		error = -EFSCORRUPTED;
		goto error0;
	}
	ASSERT(lev < cur->bc_nlevels);

	/*
	 * Now walk back down the tree, fixing up the cursor's buffer
	 * pointers and key numbers.
	 */
	for (block = xfs_btree_get_block(cur, lev, &bp); lev > level; ) {
		union xfs_btree_ptr	*ptrp;

		ptrp = xfs_btree_ptr_addr(cur, cur->bc_levels[lev].ptr, block);
		--lev;
		error = xfs_btree_read_buf_block(cur, ptrp, 0, &block, &bp);
		if (error)
			goto error0;

		xfs_btree_setbuf(cur, lev, bp);
		cur->bc_levels[lev].ptr = 1;
	}
out1:
	*stat = 1;
	return 0;

out0:
	*stat = 0;
	return 0;

error0:
	return error;
}

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int						/* error */
xfs_btree_decrement(
	struct xfs_btree_cur	*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;
	int			error;		/* error return value */
	int			lev;
	union xfs_btree_ptr	ptr;

	ASSERT(level < cur->bc_nlevels);

	/* Read-ahead to the left at this level. */
	xfs_btree_readahead(cur, level, XFS_BTCUR_LEFTRA);

	/* We're done if we remain in the block after the decrement. */
	if (--cur->bc_levels[level].ptr > 0)
		goto out1;

	/* Get a pointer to the btree block. */
	block = xfs_btree_get_block(cur, level, &bp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, level, bp);
	if (error)
		goto error0;
#endif

	/* Fail if we just went off the left edge of the tree. */
	xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_LEFTSIB);
	if (xfs_btree_ptr_is_null(cur, &ptr))
		goto out0;

	XFS_BTREE_STATS_INC(cur, decrement);

	/*
	 * March up the tree decrementing pointers.
	 * Stop when we don't go off the left edge of a block.
	 */
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_levels[lev].ptr > 0)
			break;
		/* Read-ahead the left block for the next loop. */
		xfs_btree_readahead(cur, lev, XFS_BTCUR_LEFTRA);
	}

	/*
	 * If we went off the root then we are seriously confused.
	 * or the root of the tree is in an inode.
	 */
	if (lev == cur->bc_nlevels) {
		if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE)
			goto out0;
		ASSERT(0);
		error = -EFSCORRUPTED;
		goto error0;
	}
	ASSERT(lev < cur->bc_nlevels);

	/*
	 * Now walk back down the tree, fixing up the cursor's buffer
	 * pointers and key numbers.
	 */
	for (block = xfs_btree_get_block(cur, lev, &bp); lev > level; ) {
		union xfs_btree_ptr	*ptrp;

		ptrp = xfs_btree_ptr_addr(cur, cur->bc_levels[lev].ptr, block);
		--lev;
		error = xfs_btree_read_buf_block(cur, ptrp, 0, &block, &bp);
		if (error)
			goto error0;
		xfs_btree_setbuf(cur, lev, bp);
		cur->bc_levels[lev].ptr = xfs_btree_get_numrecs(block);
	}
out1:
	*stat = 1;
	return 0;

out0:
	*stat = 0;
	return 0;

error0:
	return error;
}

int
xfs_btree_lookup_get_block(
	struct xfs_btree_cur		*cur,	/* btree cursor */
	int				level,	/* level in the btree */
	const union xfs_btree_ptr	*pp,	/* ptr to btree block */
	struct xfs_btree_block		**blkp) /* return btree block */
{
	struct xfs_buf		*bp;	/* buffer pointer for btree block */
	xfs_daddr_t		daddr;
	int			error = 0;

	/* special case the root block if in an inode */
	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (level == cur->bc_nlevels - 1)) {
		*blkp = xfs_btree_get_iroot(cur);
		return 0;
	}

	/*
	 * If the old buffer at this level for the disk address we are
	 * looking for re-use it.
	 *
	 * Otherwise throw it away and get a new one.
	 */
	bp = cur->bc_levels[level].bp;
	error = xfs_btree_ptr_to_daddr(cur, pp, &daddr);
	if (error)
		return error;
	if (bp && xfs_buf_daddr(bp) == daddr) {
		*blkp = XFS_BUF_TO_BLOCK(bp);
		return 0;
	}

	error = xfs_btree_read_buf_block(cur, pp, 0, blkp, &bp);
	if (error)
		return error;

	/* Check the inode owner since the verifiers don't. */
	if (xfs_has_crc(cur->bc_mp) &&
	    !(cur->bc_ino.flags & XFS_BTCUR_BMBT_INVALID_OWNER) &&
	    (cur->bc_flags & XFS_BTREE_LONG_PTRS) &&
	    be64_to_cpu((*blkp)->bb_u.l.bb_owner) !=
			cur->bc_ino.ip->i_ino)
		goto out_bad;

	/* Did we get the level we were looking for? */
	if (be16_to_cpu((*blkp)->bb_level) != level)
		goto out_bad;

	/* Check that internal nodes have at least one record. */
	if (level != 0 && be16_to_cpu((*blkp)->bb_numrecs) == 0)
		goto out_bad;

	xfs_btree_setbuf(cur, level, bp);
	return 0;

out_bad:
	*blkp = NULL;
	xfs_buf_mark_corrupt(bp);
	xfs_trans_brelse(cur->bc_tp, bp);
	return -EFSCORRUPTED;
}

/*
 * Get current search key.  For level 0 we don't actually have a key
 * structure so we make one up from the record.  For all other levels
 * we just return the right key.
 */
STATIC union xfs_btree_key *
xfs_lookup_get_search_key(
	struct xfs_btree_cur	*cur,
	int			level,
	int			keyno,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*kp)
{
	if (level == 0) {
		cur->bc_ops->init_key_from_rec(kp,
				xfs_btree_rec_addr(cur, keyno, block));
		return kp;
	}

	return xfs_btree_key_addr(cur, keyno, block);
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 * stat is set to 0 if can't find any such record, 1 for success.
 */
int					/* error */
xfs_btree_lookup(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_lookup_t		dir,	/* <=, ==, or >= */
	int			*stat)	/* success/failure */
{
	struct xfs_btree_block	*block;	/* current btree block */
	int64_t			diff;	/* difference for the current key */
	int			error;	/* error return value */
	int			keyno;	/* current key number */
	int			level;	/* level in the btree */
	union xfs_btree_ptr	*pp;	/* ptr to btree block */
	union xfs_btree_ptr	ptr;	/* ptr to btree block */

	XFS_BTREE_STATS_INC(cur, lookup);

	/* No such thing as a zero-level tree. */
	if (XFS_IS_CORRUPT(cur->bc_mp, cur->bc_nlevels == 0))
		return -EFSCORRUPTED;

	block = NULL;
	keyno = 0;

	/* initialise start pointer from cursor */
	cur->bc_ops->init_ptr_from_cur(cur, &ptr);
	pp = &ptr;

	/*
	 * Iterate over each level in the btree, starting at the root.
	 * For each level above the leaves, find the key we need, based
	 * on the lookup record, then follow the corresponding block
	 * pointer down to the next level.
	 */
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		/* Get the block we need to do the lookup on. */
		error = xfs_btree_lookup_get_block(cur, level, pp, &block);
		if (error)
			goto error0;

		if (diff == 0) {
			/*
			 * If we already had a key match at a higher level, we
			 * know we need to use the first entry in this block.
			 */
			keyno = 1;
		} else {
			/* Otherwise search this block. Do a binary search. */

			int	high;	/* high entry number */
			int	low;	/* low entry number */

			/* Set low and high entry numbers, 1-based. */
			low = 1;
			high = xfs_btree_get_numrecs(block);
			if (!high) {
				/* Block is empty, must be an empty leaf. */
				if (level != 0 || cur->bc_nlevels != 1) {
					XFS_CORRUPTION_ERROR(__func__,
							XFS_ERRLEVEL_LOW,
							cur->bc_mp, block,
							sizeof(*block));
					return -EFSCORRUPTED;
				}

				cur->bc_levels[0].ptr = dir != XFS_LOOKUP_LE;
				*stat = 0;
				return 0;
			}

			/* Binary search the block. */
			while (low <= high) {
				union xfs_btree_key	key;
				union xfs_btree_key	*kp;

				XFS_BTREE_STATS_INC(cur, compare);

				/* keyno is average of low and high. */
				keyno = (low + high) >> 1;

				/* Get current search key */
				kp = xfs_lookup_get_search_key(cur, level,
						keyno, block, &key);

				/*
				 * Compute difference to get next direction:
				 *  - less than, move right
				 *  - greater than, move left
				 *  - equal, we're done
				 */
				diff = cur->bc_ops->key_diff(cur, kp);
				if (diff < 0)
					low = keyno + 1;
				else if (diff > 0)
					high = keyno - 1;
				else
					break;
			}
		}

		/*
		 * If there are more levels, set up for the next level
		 * by getting the block number and filling in the cursor.
		 */
		if (level > 0) {
			/*
			 * If we moved left, need the previous key number,
			 * unless there isn't one.
			 */
			if (diff > 0 && --keyno < 1)
				keyno = 1;
			pp = xfs_btree_ptr_addr(cur, keyno, block);

			error = xfs_btree_debug_check_ptr(cur, pp, 0, level);
			if (error)
				goto error0;

			cur->bc_levels[level].ptr = keyno;
		}
	}

	/* Done with the search. See if we need to adjust the results. */
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_RIGHTSIB);
		if (dir == XFS_LOOKUP_GE &&
		    keyno > xfs_btree_get_numrecs(block) &&
		    !xfs_btree_ptr_is_null(cur, &ptr)) {
			int	i;

			cur->bc_levels[0].ptr = keyno;
			error = xfs_btree_increment(cur, 0, &i);
			if (error)
				goto error0;
			if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
				return -EFSCORRUPTED;
			*stat = 1;
			return 0;
		}
	} else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_levels[0].ptr = keyno;

	/* Return if we succeeded or not. */
	if (keyno == 0 || keyno > xfs_btree_get_numrecs(block))
		*stat = 0;
	else if (dir != XFS_LOOKUP_EQ || diff == 0)
		*stat = 1;
	else
		*stat = 0;
	return 0;

error0:
	return error;
}

/* Find the high key storage area from a regular key. */
union xfs_btree_key *
xfs_btree_high_key_from_key(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	ASSERT(cur->bc_flags & XFS_BTREE_OVERLAPPING);
	return (union xfs_btree_key *)((char *)key +
			(cur->bc_ops->key_len / 2));
}

/* Determine the low (and high if overlapped) keys of a leaf block */
STATIC void
xfs_btree_get_leaf_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*key)
{
	union xfs_btree_key	max_hkey;
	union xfs_btree_key	hkey;
	union xfs_btree_rec	*rec;
	union xfs_btree_key	*high;
	int			n;

	rec = xfs_btree_rec_addr(cur, 1, block);
	cur->bc_ops->init_key_from_rec(key, rec);

	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {

		cur->bc_ops->init_high_key_from_rec(&max_hkey, rec);
		for (n = 2; n <= xfs_btree_get_numrecs(block); n++) {
			rec = xfs_btree_rec_addr(cur, n, block);
			cur->bc_ops->init_high_key_from_rec(&hkey, rec);
			if (cur->bc_ops->diff_two_keys(cur, &hkey, &max_hkey)
					> 0)
				max_hkey = hkey;
		}

		high = xfs_btree_high_key_from_key(cur, key);
		memcpy(high, &max_hkey, cur->bc_ops->key_len / 2);
	}
}

/* Determine the low (and high if overlapped) keys of a node block */
STATIC void
xfs_btree_get_node_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*key)
{
	union xfs_btree_key	*hkey;
	union xfs_btree_key	*max_hkey;
	union xfs_btree_key	*high;
	int			n;

	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {
		memcpy(key, xfs_btree_key_addr(cur, 1, block),
				cur->bc_ops->key_len / 2);

		max_hkey = xfs_btree_high_key_addr(cur, 1, block);
		for (n = 2; n <= xfs_btree_get_numrecs(block); n++) {
			hkey = xfs_btree_high_key_addr(cur, n, block);
			if (cur->bc_ops->diff_two_keys(cur, hkey, max_hkey) > 0)
				max_hkey = hkey;
		}

		high = xfs_btree_high_key_from_key(cur, key);
		memcpy(high, max_hkey, cur->bc_ops->key_len / 2);
	} else {
		memcpy(key, xfs_btree_key_addr(cur, 1, block),
				cur->bc_ops->key_len);
	}
}

/* Derive the keys for any btree block. */
void
xfs_btree_get_keys(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_key	*key)
{
	if (be16_to_cpu(block->bb_level) == 0)
		xfs_btree_get_leaf_keys(cur, block, key);
	else
		xfs_btree_get_node_keys(cur, block, key);
}

/*
 * Decide if we need to update the parent keys of a btree block.  For
 * a standard btree this is only necessary if we're updating the first
 * record/key.  For an overlapping btree, we must always update the
 * keys because the highest key can be in any of the records or keys
 * in the block.
 */
static inline bool
xfs_btree_needs_key_update(
	struct xfs_btree_cur	*cur,
	int			ptr)
{
	return (cur->bc_flags & XFS_BTREE_OVERLAPPING) || ptr == 1;
}

/*
 * Update the low and high parent keys of the given level, progressing
 * towards the root.  If force_all is false, stop if the keys for a given
 * level do not need updating.
 */
STATIC int
__xfs_btree_updkeys(
	struct xfs_btree_cur	*cur,
	int			level,
	struct xfs_btree_block	*block,
	struct xfs_buf		*bp0,
	bool			force_all)
{
	union xfs_btree_key	key;	/* keys from current level */
	union xfs_btree_key	*lkey;	/* keys from the next level up */
	union xfs_btree_key	*hkey;
	union xfs_btree_key	*nlkey;	/* keys from the next level up */
	union xfs_btree_key	*nhkey;
	struct xfs_buf		*bp;
	int			ptr;

	ASSERT(cur->bc_flags & XFS_BTREE_OVERLAPPING);

	/* Exit if there aren't any parent levels to update. */
	if (level + 1 >= cur->bc_nlevels)
		return 0;

	trace_xfs_btree_updkeys(cur, level, bp0);

	lkey = &key;
	hkey = xfs_btree_high_key_from_key(cur, lkey);
	xfs_btree_get_keys(cur, block, lkey);
	for (level++; level < cur->bc_nlevels; level++) {
#ifdef DEBUG
		int		error;
#endif
		block = xfs_btree_get_block(cur, level, &bp);
		trace_xfs_btree_updkeys(cur, level, bp);
#ifdef DEBUG
		error = xfs_btree_check_block(cur, block, level, bp);
		if (error)
			return error;
#endif
		ptr = cur->bc_levels[level].ptr;
		nlkey = xfs_btree_key_addr(cur, ptr, block);
		nhkey = xfs_btree_high_key_addr(cur, ptr, block);
		if (!force_all &&
		    !(cur->bc_ops->diff_two_keys(cur, nlkey, lkey) != 0 ||
		      cur->bc_ops->diff_two_keys(cur, nhkey, hkey) != 0))
			break;
		xfs_btree_copy_keys(cur, nlkey, lkey, 1);
		xfs_btree_log_keys(cur, bp, ptr, ptr);
		if (level + 1 >= cur->bc_nlevels)
			break;
		xfs_btree_get_node_keys(cur, block, lkey);
	}

	return 0;
}

/* Update all the keys from some level in cursor back to the root. */
STATIC int
xfs_btree_updkeys_force(
	struct xfs_btree_cur	*cur,
	int			level)
{
	struct xfs_buf		*bp;
	struct xfs_btree_block	*block;

	block = xfs_btree_get_block(cur, level, &bp);
	return __xfs_btree_updkeys(cur, level, block, bp, true);
}

/*
 * Update the parent keys of the given level, progressing towards the root.
 */
STATIC int
xfs_btree_update_keys(
	struct xfs_btree_cur	*cur,
	int			level)
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;
	union xfs_btree_key	*kp;
	union xfs_btree_key	key;
	int			ptr;

	ASSERT(level >= 0);

	block = xfs_btree_get_block(cur, level, &bp);
	if (cur->bc_flags & XFS_BTREE_OVERLAPPING)
		return __xfs_btree_updkeys(cur, level, block, bp, false);

	/*
	 * Go up the tree from this level toward the root.
	 * At each level, update the key value to the value input.
	 * Stop when we reach a level where the cursor isn't pointing
	 * at the first entry in the block.
	 */
	xfs_btree_get_keys(cur, block, &key);
	for (level++, ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
#ifdef DEBUG
		int		error;
#endif
		block = xfs_btree_get_block(cur, level, &bp);
#ifdef DEBUG
		error = xfs_btree_check_block(cur, block, level, bp);
		if (error)
			return error;
#endif
		ptr = cur->bc_levels[level].ptr;
		kp = xfs_btree_key_addr(cur, ptr, block);
		xfs_btree_copy_keys(cur, kp, &key, 1);
		xfs_btree_log_keys(cur, bp, ptr, ptr);
	}

	return 0;
}

/*
 * Update the record referred to by cur to the value in the
 * given record. This either works (return 0) or gets an
 * EFSCORRUPTED error.
 */
int
xfs_btree_update(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;
	int			error;
	int			ptr;
	union xfs_btree_rec	*rp;

	/* Pick up the current block. */
	block = xfs_btree_get_block(cur, 0, &bp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, 0, bp);
	if (error)
		goto error0;
#endif
	/* Get the address of the rec to be updated. */
	ptr = cur->bc_levels[0].ptr;
	rp = xfs_btree_rec_addr(cur, ptr, block);

	/* Fill in the new contents and log them. */
	xfs_btree_copy_recs(cur, rp, rec, 1);
	xfs_btree_log_recs(cur, bp, ptr, ptr);

	/*
	 * If we are tracking the last record in the tree and
	 * we are at the far right edge of the tree, update it.
	 */
	if (xfs_btree_is_lastrec(cur, block, 0)) {
		cur->bc_ops->update_lastrec(cur, block, rec,
					    ptr, LASTREC_UPDATE);
	}

	/* Pass new key value up to our parent. */
	if (xfs_btree_needs_key_update(cur, ptr)) {
		error = xfs_btree_update_keys(cur, 0);
		if (error)
			goto error0;
	}

	return 0;

error0:
	return error;
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int					/* error */
xfs_btree_lshift(
	struct xfs_btree_cur	*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	struct xfs_buf		*lbp;		/* left buffer pointer */
	struct xfs_btree_block	*left;		/* left btree block */
	int			lrecs;		/* left record count */
	struct xfs_buf		*rbp;		/* right buffer pointer */
	struct xfs_btree_block	*right;		/* right btree block */
	struct xfs_btree_cur	*tcur;		/* temporary btree cursor */
	int			rrecs;		/* right record count */
	union xfs_btree_ptr	lptr;		/* left btree pointer */
	union xfs_btree_key	*rkp = NULL;	/* right btree key */
	union xfs_btree_ptr	*rpp = NULL;	/* right address pointer */
	union xfs_btree_rec	*rrp = NULL;	/* right record pointer */
	int			error;		/* error return value */
	int			i;

	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    level == cur->bc_nlevels - 1)
		goto out0;

	/* Set up variables for this block as "right". */
	right = xfs_btree_get_block(cur, level, &rbp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, right, level, rbp);
	if (error)
		goto error0;
#endif

	/* If we've got no left sibling then we can't shift an entry left. */
	xfs_btree_get_sibling(cur, right, &lptr, XFS_BB_LEFTSIB);
	if (xfs_btree_ptr_is_null(cur, &lptr))
		goto out0;

	/*
	 * If the cursor entry is the one that would be moved, don't
	 * do it... it's too complicated.
	 */
	if (cur->bc_levels[level].ptr <= 1)
		goto out0;

	/* Set up the left neighbor as "left". */
	error = xfs_btree_read_buf_block(cur, &lptr, 0, &left, &lbp);
	if (error)
		goto error0;

	/* If it's full, it can't take another entry. */
	lrecs = xfs_btree_get_numrecs(left);
	if (lrecs == cur->bc_ops->get_maxrecs(cur, level))
		goto out0;

	rrecs = xfs_btree_get_numrecs(right);

	/*
	 * We add one entry to the left side and remove one for the right side.
	 * Account for it here, the changes will be updated on disk and logged
	 * later.
	 */
	lrecs++;
	rrecs--;

	XFS_BTREE_STATS_INC(cur, lshift);
	XFS_BTREE_STATS_ADD(cur, moves, 1);

	/*
	 * If non-leaf, copy a key and a ptr to the left block.
	 * Log the changes to the left block.
	 */
	if (level > 0) {
		/* It's a non-leaf.  Move keys and pointers. */
		union xfs_btree_key	*lkp;	/* left btree key */
		union xfs_btree_ptr	*lpp;	/* left address pointer */

		lkp = xfs_btree_key_addr(cur, lrecs, left);
		rkp = xfs_btree_key_addr(cur, 1, right);

		lpp = xfs_btree_ptr_addr(cur, lrecs, left);
		rpp = xfs_btree_ptr_addr(cur, 1, right);

		error = xfs_btree_debug_check_ptr(cur, rpp, 0, level);
		if (error)
			goto error0;

		xfs_btree_copy_keys(cur, lkp, rkp, 1);
		xfs_btree_copy_ptrs(cur, lpp, rpp, 1);

		xfs_btree_log_keys(cur, lbp, lrecs, lrecs);
		xfs_btree_log_ptrs(cur, lbp, lrecs, lrecs);

		ASSERT(cur->bc_ops->keys_inorder(cur,
			xfs_btree_key_addr(cur, lrecs - 1, left), lkp));
	} else {
		/* It's a leaf.  Move records.  */
		union xfs_btree_rec	*lrp;	/* left record pointer */

		lrp = xfs_btree_rec_addr(cur, lrecs, left);
		rrp = xfs_btree_rec_addr(cur, 1, right);

		xfs_btree_copy_recs(cur, lrp, rrp, 1);
		xfs_btree_log_recs(cur, lbp, lrecs, lrecs);

		ASSERT(cur->bc_ops->recs_inorder(cur,
			xfs_btree_rec_addr(cur, lrecs - 1, left), lrp));
	}

	xfs_btree_set_numrecs(left, lrecs);
	xfs_btree_log_block(cur, lbp, XFS_BB_NUMRECS);

	xfs_btree_set_numrecs(right, rrecs);
	xfs_btree_log_block(cur, rbp, XFS_BB_NUMRECS);

	/*
	 * Slide the contents of right down one entry.
	 */
	XFS_BTREE_STATS_ADD(cur, moves, rrecs - 1);
	if (level > 0) {
		/* It's a nonleaf. operate on keys and ptrs */
		for (i = 0; i < rrecs; i++) {
			error = xfs_btree_debug_check_ptr(cur, rpp, i + 1, level);
			if (error)
				goto error0;
		}

		xfs_btree_shift_keys(cur,
				xfs_btree_key_addr(cur, 2, right),
				-1, rrecs);
		xfs_btree_shift_ptrs(cur,
				xfs_btree_ptr_addr(cur, 2, right),
				-1, rrecs);

		xfs_btree_log_keys(cur, rbp, 1, rrecs);
		xfs_btree_log_ptrs(cur, rbp, 1, rrecs);
	} else {
		/* It's a leaf. operate on records */
		xfs_btree_shift_recs(cur,
			xfs_btree_rec_addr(cur, 2, right),
			-1, rrecs);
		xfs_btree_log_recs(cur, rbp, 1, rrecs);
	}

	/*
	 * Using a temporary cursor, update the parent key values of the
	 * block on the left.
	 */
	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {
		error = xfs_btree_dup_cursor(cur, &tcur);
		if (error)
			goto error0;
		i = xfs_btree_firstrec(tcur, level);
		if (XFS_IS_CORRUPT(tcur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		error = xfs_btree_decrement(tcur, level, &i);
		if (error)
			goto error1;

		/* Update the parent high keys of the left block, if needed. */
		error = xfs_btree_update_keys(tcur, level);
		if (error)
			goto error1;

		xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	}

	/* Update the parent keys of the right block. */
	error = xfs_btree_update_keys(cur, level);
	if (error)
		goto error0;

	/* Slide the cursor value left one. */
	cur->bc_levels[level].ptr--;

	*stat = 1;
	return 0;

out0:
	*stat = 0;
	return 0;

error0:
	return error;

error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int					/* error */
xfs_btree_rshift(
	struct xfs_btree_cur	*cur,
	int			level,
	int			*stat)		/* success/failure */
{
	struct xfs_buf		*lbp;		/* left buffer pointer */
	struct xfs_btree_block	*left;		/* left btree block */
	struct xfs_buf		*rbp;		/* right buffer pointer */
	struct xfs_btree_block	*right;		/* right btree block */
	struct xfs_btree_cur	*tcur;		/* temporary btree cursor */
	union xfs_btree_ptr	rptr;		/* right block pointer */
	union xfs_btree_key	*rkp;		/* right btree key */
	int			rrecs;		/* right record count */
	int			lrecs;		/* left record count */
	int			error;		/* error return value */
	int			i;		/* loop counter */

	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (level == cur->bc_nlevels - 1))
		goto out0;

	/* Set up variables for this block as "left". */
	left = xfs_btree_get_block(cur, level, &lbp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, left, level, lbp);
	if (error)
		goto error0;
#endif

	/* If we've got no right sibling then we can't shift an entry right. */
	xfs_btree_get_sibling(cur, left, &rptr, XFS_BB_RIGHTSIB);
	if (xfs_btree_ptr_is_null(cur, &rptr))
		goto out0;

	/*
	 * If the cursor entry is the one that would be moved, don't
	 * do it... it's too complicated.
	 */
	lrecs = xfs_btree_get_numrecs(left);
	if (cur->bc_levels[level].ptr >= lrecs)
		goto out0;

	/* Set up the right neighbor as "right". */
	error = xfs_btree_read_buf_block(cur, &rptr, 0, &right, &rbp);
	if (error)
		goto error0;

	/* If it's full, it can't take another entry. */
	rrecs = xfs_btree_get_numrecs(right);
	if (rrecs == cur->bc_ops->get_maxrecs(cur, level))
		goto out0;

	XFS_BTREE_STATS_INC(cur, rshift);
	XFS_BTREE_STATS_ADD(cur, moves, rrecs);

	/*
	 * Make a hole at the start of the right neighbor block, then
	 * copy the last left block entry to the hole.
	 */
	if (level > 0) {
		/* It's a nonleaf. make a hole in the keys and ptrs */
		union xfs_btree_key	*lkp;
		union xfs_btree_ptr	*lpp;
		union xfs_btree_ptr	*rpp;

		lkp = xfs_btree_key_addr(cur, lrecs, left);
		lpp = xfs_btree_ptr_addr(cur, lrecs, left);
		rkp = xfs_btree_key_addr(cur, 1, right);
		rpp = xfs_btree_ptr_addr(cur, 1, right);

		for (i = rrecs - 1; i >= 0; i--) {
			error = xfs_btree_debug_check_ptr(cur, rpp, i, level);
			if (error)
				goto error0;
		}

		xfs_btree_shift_keys(cur, rkp, 1, rrecs);
		xfs_btree_shift_ptrs(cur, rpp, 1, rrecs);

		error = xfs_btree_debug_check_ptr(cur, lpp, 0, level);
		if (error)
			goto error0;

		/* Now put the new data in, and log it. */
		xfs_btree_copy_keys(cur, rkp, lkp, 1);
		xfs_btree_copy_ptrs(cur, rpp, lpp, 1);

		xfs_btree_log_keys(cur, rbp, 1, rrecs + 1);
		xfs_btree_log_ptrs(cur, rbp, 1, rrecs + 1);

		ASSERT(cur->bc_ops->keys_inorder(cur, rkp,
			xfs_btree_key_addr(cur, 2, right)));
	} else {
		/* It's a leaf. make a hole in the records */
		union xfs_btree_rec	*lrp;
		union xfs_btree_rec	*rrp;

		lrp = xfs_btree_rec_addr(cur, lrecs, left);
		rrp = xfs_btree_rec_addr(cur, 1, right);

		xfs_btree_shift_recs(cur, rrp, 1, rrecs);

		/* Now put the new data in, and log it. */
		xfs_btree_copy_recs(cur, rrp, lrp, 1);
		xfs_btree_log_recs(cur, rbp, 1, rrecs + 1);
	}

	/*
	 * Decrement and log left's numrecs, bump and log right's numrecs.
	 */
	xfs_btree_set_numrecs(left, --lrecs);
	xfs_btree_log_block(cur, lbp, XFS_BB_NUMRECS);

	xfs_btree_set_numrecs(right, ++rrecs);
	xfs_btree_log_block(cur, rbp, XFS_BB_NUMRECS);

	/*
	 * Using a temporary cursor, update the parent key values of the
	 * block on the right.
	 */
	error = xfs_btree_dup_cursor(cur, &tcur);
	if (error)
		goto error0;
	i = xfs_btree_lastrec(tcur, level);
	if (XFS_IS_CORRUPT(tcur->bc_mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}

	error = xfs_btree_increment(tcur, level, &i);
	if (error)
		goto error1;

	/* Update the parent high keys of the left block, if needed. */
	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {
		error = xfs_btree_update_keys(cur, level);
		if (error)
			goto error1;
	}

	/* Update the parent keys of the right block. */
	error = xfs_btree_update_keys(tcur, level);
	if (error)
		goto error1;

	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);

	*stat = 1;
	return 0;

out0:
	*stat = 0;
	return 0;

error0:
	return error;

error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Split cur/level block in half.
 * Return new block number and the key to its first
 * record (to be inserted into parent).
 */
STATIC int					/* error */
__xfs_btree_split(
	struct xfs_btree_cur	*cur,
	int			level,
	union xfs_btree_ptr	*ptrp,
	union xfs_btree_key	*key,
	struct xfs_btree_cur	**curp,
	int			*stat)		/* success/failure */
{
	union xfs_btree_ptr	lptr;		/* left sibling block ptr */
	struct xfs_buf		*lbp;		/* left buffer pointer */
	struct xfs_btree_block	*left;		/* left btree block */
	union xfs_btree_ptr	rptr;		/* right sibling block ptr */
	struct xfs_buf		*rbp;		/* right buffer pointer */
	struct xfs_btree_block	*right;		/* right btree block */
	union xfs_btree_ptr	rrptr;		/* right-right sibling ptr */
	struct xfs_buf		*rrbp;		/* right-right buffer pointer */
	struct xfs_btree_block	*rrblock;	/* right-right btree block */
	int			lrecs;
	int			rrecs;
	int			src_index;
	int			error;		/* error return value */
	int			i;

	XFS_BTREE_STATS_INC(cur, split);

	/* Set up left block (current one). */
	left = xfs_btree_get_block(cur, level, &lbp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, left, level, lbp);
	if (error)
		goto error0;
#endif

	xfs_btree_buf_to_ptr(cur, lbp, &lptr);

	/* Allocate the new block. If we can't do it, we're toast. Give up. */
	error = cur->bc_ops->alloc_block(cur, &lptr, &rptr, stat);
	if (error)
		goto error0;
	if (*stat == 0)
		goto out0;
	XFS_BTREE_STATS_INC(cur, alloc);

	/* Set up the new block as "right". */
	error = xfs_btree_get_buf_block(cur, &rptr, &right, &rbp);
	if (error)
		goto error0;

	/* Fill in the btree header for the new right block. */
	xfs_btree_init_block_cur(cur, rbp, xfs_btree_get_level(left), 0);

	/*
	 * Split the entries between the old and the new block evenly.
	 * Make sure that if there's an odd number of entries now, that
	 * each new block will have the same number of entries.
	 */
	lrecs = xfs_btree_get_numrecs(left);
	rrecs = lrecs / 2;
	if ((lrecs & 1) && cur->bc_levels[level].ptr <= rrecs + 1)
		rrecs++;
	src_index = (lrecs - rrecs + 1);

	XFS_BTREE_STATS_ADD(cur, moves, rrecs);

	/* Adjust numrecs for the later get_*_keys() calls. */
	lrecs -= rrecs;
	xfs_btree_set_numrecs(left, lrecs);
	xfs_btree_set_numrecs(right, xfs_btree_get_numrecs(right) + rrecs);

	/*
	 * Copy btree block entries from the left block over to the
	 * new block, the right. Update the right block and log the
	 * changes.
	 */
	if (level > 0) {
		/* It's a non-leaf.  Move keys and pointers. */
		union xfs_btree_key	*lkp;	/* left btree key */
		union xfs_btree_ptr	*lpp;	/* left address pointer */
		union xfs_btree_key	*rkp;	/* right btree key */
		union xfs_btree_ptr	*rpp;	/* right address pointer */

		lkp = xfs_btree_key_addr(cur, src_index, left);
		lpp = xfs_btree_ptr_addr(cur, src_index, left);
		rkp = xfs_btree_key_addr(cur, 1, right);
		rpp = xfs_btree_ptr_addr(cur, 1, right);

		for (i = src_index; i < rrecs; i++) {
			error = xfs_btree_debug_check_ptr(cur, lpp, i, level);
			if (error)
				goto error0;
		}

		/* Copy the keys & pointers to the new block. */
		xfs_btree_copy_keys(cur, rkp, lkp, rrecs);
		xfs_btree_copy_ptrs(cur, rpp, lpp, rrecs);

		xfs_btree_log_keys(cur, rbp, 1, rrecs);
		xfs_btree_log_ptrs(cur, rbp, 1, rrecs);

		/* Stash the keys of the new block for later insertion. */
		xfs_btree_get_node_keys(cur, right, key);
	} else {
		/* It's a leaf.  Move records.  */
		union xfs_btree_rec	*lrp;	/* left record pointer */
		union xfs_btree_rec	*rrp;	/* right record pointer */

		lrp = xfs_btree_rec_addr(cur, src_index, left);
		rrp = xfs_btree_rec_addr(cur, 1, right);

		/* Copy records to the new block. */
		xfs_btree_copy_recs(cur, rrp, lrp, rrecs);
		xfs_btree_log_recs(cur, rbp, 1, rrecs);

		/* Stash the keys of the new block for later insertion. */
		xfs_btree_get_leaf_keys(cur, right, key);
	}

	/*
	 * Find the left block number by looking in the buffer.
	 * Adjust sibling pointers.
	 */
	xfs_btree_get_sibling(cur, left, &rrptr, XFS_BB_RIGHTSIB);
	xfs_btree_set_sibling(cur, right, &rrptr, XFS_BB_RIGHTSIB);
	xfs_btree_set_sibling(cur, right, &lptr, XFS_BB_LEFTSIB);
	xfs_btree_set_sibling(cur, left, &rptr, XFS_BB_RIGHTSIB);

	xfs_btree_log_block(cur, rbp, XFS_BB_ALL_BITS);
	xfs_btree_log_block(cur, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);

	/*
	 * If there's a block to the new block's right, make that block
	 * point back to right instead of to left.
	 */
	if (!xfs_btree_ptr_is_null(cur, &rrptr)) {
		error = xfs_btree_read_buf_block(cur, &rrptr,
							0, &rrblock, &rrbp);
		if (error)
			goto error0;
		xfs_btree_set_sibling(cur, rrblock, &rptr, XFS_BB_LEFTSIB);
		xfs_btree_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}

	/* Update the parent high keys of the left block, if needed. */
	if (cur->bc_flags & XFS_BTREE_OVERLAPPING) {
		error = xfs_btree_update_keys(cur, level);
		if (error)
			goto error0;
	}

	/*
	 * If the cursor is really in the right block, move it there.
	 * If it's just pointing past the last entry in left, then we'll
	 * insert there, so don't change anything in that case.
	 */
	if (cur->bc_levels[level].ptr > lrecs + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_levels[level].ptr -= lrecs;
	}
	/*
	 * If there are more levels, we'll need another cursor which refers
	 * the right block, no matter where this cursor was.
	 */
	if (level + 1 < cur->bc_nlevels) {
		error = xfs_btree_dup_cursor(cur, curp);
		if (error)
			goto error0;
		(*curp)->bc_levels[level + 1].ptr++;
	}
	*ptrp = rptr;
	*stat = 1;
	return 0;
out0:
	*stat = 0;
	return 0;

error0:
	return error;
}

#ifdef __KERNEL__
struct xfs_btree_split_args {
	struct xfs_btree_cur	*cur;
	int			level;
	union xfs_btree_ptr	*ptrp;
	union xfs_btree_key	*key;
	struct xfs_btree_cur	**curp;
	int			*stat;		/* success/failure */
	int			result;
	bool			kswapd;	/* allocation in kswapd context */
	struct completion	*done;
	struct work_struct	work;
};

/*
 * Stack switching interfaces for allocation
 */
static void
xfs_btree_split_worker(
	struct work_struct	*work)
{
	struct xfs_btree_split_args	*args = container_of(work,
						struct xfs_btree_split_args, work);
	unsigned long		pflags;
	unsigned long		new_pflags = 0;

	/*
	 * we are in a transaction context here, but may also be doing work
	 * in kswapd context, and hence we may need to inherit that state
	 * temporarily to ensure that we don't block waiting for memory reclaim
	 * in any way.
	 */
	if (args->kswapd)
		new_pflags |= PF_MEMALLOC | PF_KSWAPD;

	current_set_flags_nested(&pflags, new_pflags);
	xfs_trans_set_context(args->cur->bc_tp);

	args->result = __xfs_btree_split(args->cur, args->level, args->ptrp,
					 args->key, args->curp, args->stat);

	xfs_trans_clear_context(args->cur->bc_tp);
	current_restore_flags_nested(&pflags, new_pflags);

	/*
	 * Do not access args after complete() has run here. We don't own args
	 * and the owner may run and free args before we return here.
	 */
	complete(args->done);

}

/*
 * BMBT split requests often come in with little stack to work on. Push
 * them off to a worker thread so there is lots of stack to use. For the other
 * btree types, just call directly to avoid the context switch overhead here.
 */
STATIC int					/* error */
xfs_btree_split(
	struct xfs_btree_cur	*cur,
	int			level,
	union xfs_btree_ptr	*ptrp,
	union xfs_btree_key	*key,
	struct xfs_btree_cur	**curp,
	int			*stat)		/* success/failure */
{
	struct xfs_btree_split_args	args;
	DECLARE_COMPLETION_ONSTACK(done);

	if (cur->bc_btnum != XFS_BTNUM_BMAP)
		return __xfs_btree_split(cur, level, ptrp, key, curp, stat);

	args.cur = cur;
	args.level = level;
	args.ptrp = ptrp;
	args.key = key;
	args.curp = curp;
	args.stat = stat;
	args.done = &done;
	args.kswapd = current_is_kswapd();
	INIT_WORK_ONSTACK(&args.work, xfs_btree_split_worker);
	queue_work(xfs_alloc_wq, &args.work);
	wait_for_completion(&done);
	destroy_work_on_stack(&args.work);
	return args.result;
}
#else
#define xfs_btree_split	__xfs_btree_split
#endif /* __KERNEL__ */


/*
 * Copy the old inode root contents into a real block and make the
 * broot point to it.
 */
int						/* error */
xfs_btree_new_iroot(
	struct xfs_btree_cur	*cur,		/* btree cursor */
	int			*logflags,	/* logging flags for inode */
	int			*stat)		/* return status - 0 fail */
{
	struct xfs_buf		*cbp;		/* buffer for cblock */
	struct xfs_btree_block	*block;		/* btree block */
	struct xfs_btree_block	*cblock;	/* child btree block */
	union xfs_btree_key	*ckp;		/* child key pointer */
	union xfs_btree_ptr	*cpp;		/* child ptr pointer */
	union xfs_btree_key	*kp;		/* pointer to btree key */
	union xfs_btree_ptr	*pp;		/* pointer to block addr */
	union xfs_btree_ptr	nptr;		/* new block addr */
	int			level;		/* btree level */
	int			error;		/* error return code */
	int			i;		/* loop counter */

	XFS_BTREE_STATS_INC(cur, newroot);

	ASSERT(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE);

	level = cur->bc_nlevels - 1;

	block = xfs_btree_get_iroot(cur);
	pp = xfs_btree_ptr_addr(cur, 1, block);

	/* Allocate the new block. If we can't do it, we're toast. Give up. */
	error = cur->bc_ops->alloc_block(cur, pp, &nptr, stat);
	if (error)
		goto error0;
	if (*stat == 0)
		return 0;

	XFS_BTREE_STATS_INC(cur, alloc);

	/* Copy the root into a real block. */
	error = xfs_btree_get_buf_block(cur, &nptr, &cblock, &cbp);
	if (error)
		goto error0;

	/*
	 * we can't just memcpy() the root in for CRC enabled btree blocks.
	 * In that case have to also ensure the blkno remains correct
	 */
	memcpy(cblock, block, xfs_btree_block_len(cur));
	if (cur->bc_flags & XFS_BTREE_CRC_BLOCKS) {
		__be64 bno = cpu_to_be64(xfs_buf_daddr(cbp));
		if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
			cblock->bb_u.l.bb_blkno = bno;
		else
			cblock->bb_u.s.bb_blkno = bno;
	}

	be16_add_cpu(&block->bb_level, 1);
	xfs_btree_set_numrecs(block, 1);
	cur->bc_nlevels++;
	ASSERT(cur->bc_nlevels <= cur->bc_maxlevels);
	cur->bc_levels[level + 1].ptr = 1;

	kp = xfs_btree_key_addr(cur, 1, block);
	ckp = xfs_btree_key_addr(cur, 1, cblock);
	xfs_btree_copy_keys(cur, ckp, kp, xfs_btree_get_numrecs(cblock));

	cpp = xfs_btree_ptr_addr(cur, 1, cblock);
	for (i = 0; i < be16_to_cpu(cblock->bb_numrecs); i++) {
		error = xfs_btree_debug_check_ptr(cur, pp, i, level);
		if (error)
			goto error0;
	}

	xfs_btree_copy_ptrs(cur, cpp, pp, xfs_btree_get_numrecs(cblock));

	error = xfs_btree_debug_check_ptr(cur, &nptr, 0, level);
	if (error)
		goto error0;

	xfs_btree_copy_ptrs(cur, pp, &nptr, 1);

	xfs_iroot_realloc(cur->bc_ino.ip,
			  1 - xfs_btree_get_numrecs(cblock),
			  cur->bc_ino.whichfork);

	xfs_btree_setbuf(cur, level, cbp);

	/*
	 * Do all this logging at the end so that
	 * the root is at the right level.
	 */
	xfs_btree_log_block(cur, cbp, XFS_BB_ALL_BITS);
	xfs_btree_log_keys(cur, cbp, 1, be16_to_cpu(cblock->bb_numrecs));
	xfs_btree_log_ptrs(cur, cbp, 1, be16_to_cpu(cblock->bb_numrecs));

	*logflags |=
		XFS_ILOG_CORE | xfs_ilog_fbroot(cur->bc_ino.whichfork);
	*stat = 1;
	return 0;
error0:
	return error;
}

/*
 * Allocate a new root block, fill it in.
 */
STATIC int				/* error */
xfs_btree_new_root(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			*stat)	/* success/failure */
{
	struct xfs_btree_block	*block;	/* one half of the old root block */
	struct xfs_buf		*bp;	/* buffer containing block */
	int			error;	/* error return value */
	struct xfs_buf		*lbp;	/* left buffer pointer */
	struct xfs_btree_block	*left;	/* left btree block */
	struct xfs_buf		*nbp;	/* new (root) buffer */
	struct xfs_btree_block	*new;	/* new (root) btree block */
	int			nptr;	/* new value for key index, 1 or 2 */
	struct xfs_buf		*rbp;	/* right buffer pointer */
	struct xfs_btree_block	*right;	/* right btree block */
	union xfs_btree_ptr	rptr;
	union xfs_btree_ptr	lptr;

	XFS_BTREE_STATS_INC(cur, newroot);

	/* initialise our start point from the cursor */
	cur->bc_ops->init_ptr_from_cur(cur, &rptr);

	/* Allocate the new block. If we can't do it, we're toast. Give up. */
	error = cur->bc_ops->alloc_block(cur, &rptr, &lptr, stat);
	if (error)
		goto error0;
	if (*stat == 0)
		goto out0;
	XFS_BTREE_STATS_INC(cur, alloc);

	/* Set up the new block. */
	error = xfs_btree_get_buf_block(cur, &lptr, &new, &nbp);
	if (error)
		goto error0;

	/* Set the root in the holding structure  increasing the level by 1. */
	cur->bc_ops->set_root(cur, &lptr, 1);

	/*
	 * At the previous root level there are now two blocks: the old root,
	 * and the new block generated when it was split.  We don't know which
	 * one the cursor is pointing at, so we set up variables "left" and
	 * "right" for each case.
	 */
	block = xfs_btree_get_block(cur, cur->bc_nlevels - 1, &bp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, cur->bc_nlevels - 1, bp);
	if (error)
		goto error0;
#endif

	xfs_btree_get_sibling(cur, block, &rptr, XFS_BB_RIGHTSIB);
	if (!xfs_btree_ptr_is_null(cur, &rptr)) {
		/* Our block is left, pick up the right block. */
		lbp = bp;
		xfs_btree_buf_to_ptr(cur, lbp, &lptr);
		left = block;
		error = xfs_btree_read_buf_block(cur, &rptr, 0, &right, &rbp);
		if (error)
			goto error0;
		bp = rbp;
		nptr = 1;
	} else {
		/* Our block is right, pick up the left block. */
		rbp = bp;
		xfs_btree_buf_to_ptr(cur, rbp, &rptr);
		right = block;
		xfs_btree_get_sibling(cur, right, &lptr, XFS_BB_LEFTSIB);
		error = xfs_btree_read_buf_block(cur, &lptr, 0, &left, &lbp);
		if (error)
			goto error0;
		bp = lbp;
		nptr = 2;
	}

	/* Fill in the new block's btree header and log it. */
	xfs_btree_init_block_cur(cur, nbp, cur->bc_nlevels, 2);
	xfs_btree_log_block(cur, nbp, XFS_BB_ALL_BITS);
	ASSERT(!xfs_btree_ptr_is_null(cur, &lptr) &&
			!xfs_btree_ptr_is_null(cur, &rptr));

	/* Fill in the key data in the new root. */
	if (xfs_btree_get_level(left) > 0) {
		/*
		 * Get the keys for the left block's keys and put them directly
		 * in the parent block.  Do the same for the right block.
		 */
		xfs_btree_get_node_keys(cur, left,
				xfs_btree_key_addr(cur, 1, new));
		xfs_btree_get_node_keys(cur, right,
				xfs_btree_key_addr(cur, 2, new));
	} else {
		/*
		 * Get the keys for the left block's records and put them
		 * directly in the parent block.  Do the same for the right
		 * block.
		 */
		xfs_btree_get_leaf_keys(cur, left,
			xfs_btree_key_addr(cur, 1, new));
		xfs_btree_get_leaf_keys(cur, right,
			xfs_btree_key_addr(cur, 2, new));
	}
	xfs_btree_log_keys(cur, nbp, 1, 2);

	/* Fill in the pointer data in the new root. */
	xfs_btree_copy_ptrs(cur,
		xfs_btree_ptr_addr(cur, 1, new), &lptr, 1);
	xfs_btree_copy_ptrs(cur,
		xfs_btree_ptr_addr(cur, 2, new), &rptr, 1);
	xfs_btree_log_ptrs(cur, nbp, 1, 2);

	/* Fix up the cursor. */
	xfs_btree_setbuf(cur, cur->bc_nlevels, nbp);
	cur->bc_levels[cur->bc_nlevels].ptr = nptr;
	cur->bc_nlevels++;
	ASSERT(cur->bc_nlevels <= cur->bc_maxlevels);
	*stat = 1;
	return 0;
error0:
	return error;
out0:
	*stat = 0;
	return 0;
}

STATIC int
xfs_btree_make_block_unfull(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level,	/* btree level */
	int			numrecs,/* # of recs in block */
	int			*oindex,/* old tree index */
	int			*index,	/* new tree index */
	union xfs_btree_ptr	*nptr,	/* new btree ptr */
	struct xfs_btree_cur	**ncur,	/* new btree cursor */
	union xfs_btree_key	*key,	/* key of new block */
	int			*stat)
{
	int			error = 0;

	if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    level == cur->bc_nlevels - 1) {
		struct xfs_inode *ip = cur->bc_ino.ip;

		if (numrecs < cur->bc_ops->get_dmaxrecs(cur, level)) {
			/* A root block that can be made bigger. */
			xfs_iroot_realloc(ip, 1, cur->bc_ino.whichfork);
			*stat = 1;
		} else {
			/* A root block that needs replacing */
			int	logflags = 0;

			error = xfs_btree_new_iroot(cur, &logflags, stat);
			if (error || *stat == 0)
				return error;

			xfs_trans_log_inode(cur->bc_tp, ip, logflags);
		}

		return 0;
	}

	/* First, try shifting an entry to the right neighbor. */
	error = xfs_btree_rshift(cur, level, stat);
	if (error || *stat)
		return error;

	/* Next, try shifting an entry to the left neighbor. */
	error = xfs_btree_lshift(cur, level, stat);
	if (error)
		return error;

	if (*stat) {
		*oindex = *index = cur->bc_levels[level].ptr;
		return 0;
	}

	/*
	 * Next, try splitting the current block in half.
	 *
	 * If this works we have to re-set our variables because we
	 * could be in a different block now.
	 */
	error = xfs_btree_split(cur, level, nptr, key, ncur, stat);
	if (error || *stat == 0)
		return error;


	*index = cur->bc_levels[level].ptr;
	return 0;
}

/*
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int
xfs_btree_insrec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	int			level,	/* level to insert record at */
	union xfs_btree_ptr	*ptrp,	/* i/o: block number inserted */
	union xfs_btree_rec	*rec,	/* record to insert */
	union xfs_btree_key	*key,	/* i/o: block key for ptrp */
	struct xfs_btree_cur	**curp,	/* output: new cursor replacing cur */
	int			*stat)	/* success/failure */
{
	struct xfs_btree_block	*block;	/* btree block */
	struct xfs_buf		*bp;	/* buffer for block */
	union xfs_btree_ptr	nptr;	/* new block ptr */
	struct xfs_btree_cur	*ncur = NULL;	/* new btree cursor */
	union xfs_btree_key	nkey;	/* new block key */
	union xfs_btree_key	*lkey;
	int			optr;	/* old key/record index */
	int			ptr;	/* key/record index */
	int			numrecs;/* number of records */
	int			error;	/* error return value */
	int			i;
	xfs_daddr_t		old_bn;

	ncur = NULL;
	lkey = &nkey;

	/*
	 * If we have an external root pointer, and we've made it to the
	 * root level, allocate a new root block and we're done.
	 */
	if (!(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) &&
	    (level >= cur->bc_nlevels)) {
		error = xfs_btree_new_root(cur, stat);
		xfs_btree_set_ptr_null(cur, ptrp);

		return error;
	}

	/* If we're off the left edge, return failure. */
	ptr = cur->bc_levels[level].ptr;
	if (ptr == 0) {
		*stat = 0;
		return 0;
	}

	optr = ptr;

	XFS_BTREE_STATS_INC(cur, insrec);

	/* Get pointers to the btree buffer and block. */
	block = xfs_btree_get_block(cur, level, &bp);
	old_bn = bp ? xfs_buf_daddr(bp) : XFS_BUF_DADDR_NULL;
	numrecs = xfs_btree_get_numrecs(block);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, level, bp);
	if (error)
		goto error0;

	/* Check that the new entry is being inserted in the right place. */
	if (ptr <= numrecs) {
		if (level == 0) {
			ASSERT(cur->bc_ops->recs_inorder(cur, rec,
				xfs_btree_rec_addr(cur, ptr, block)));
		} else {
			ASSERT(cur->bc_ops->keys_inorder(cur, key,
				xfs_btree_key_addr(cur, ptr, block)));
		}
	}
#endif

	/*
	 * If the block is full, we can't insert the new entry until we
	 * make the block un-full.
	 */
	xfs_btree_set_ptr_null(cur, &nptr);
	if (numrecs == cur->bc_ops->get_maxrecs(cur, level)) {
		error = xfs_btree_make_block_unfull(cur, level, numrecs,
					&optr, &ptr, &nptr, &ncur, lkey, stat);
		if (error || *stat == 0)
			goto error0;
	}

	/*
	 * The current block may have changed if the block was
	 * previously full and we have just made space in it.
	 */
	block = xfs_btree_get_block(cur, level, &bp);
	numrecs = xfs_btree_get_numrecs(block);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, level, bp);
	if (error)
		goto error0;
#endif

	/*
	 * At this point we know there's room for our new entry in the block
	 * we're pointing at.
	 */
	XFS_BTREE_STATS_ADD(cur, moves, numrecs - ptr + 1);

	if (level > 0) {
		/* It's a nonleaf. make a hole in the keys and ptrs */
		union xfs_btree_key	*kp;
		union xfs_btree_ptr	*pp;

		kp = xfs_btree_key_addr(cur, ptr, block);
		pp = xfs_btree_ptr_addr(cur, ptr, block);

		for (i = numrecs - ptr; i >= 0; i--) {
			error = xfs_btree_debug_check_ptr(cur, pp, i, level);
			if (error)
				goto error0;
		}

		xfs_btree_shift_keys(cur, kp, 1, numrecs - ptr + 1);
		xfs_btree_shift_ptrs(cur, pp, 1, numrecs - ptr + 1);

		error = xfs_btree_debug_check_ptr(cur, ptrp, 0, level);
		if (error)
			goto error0;

		/* Now put the new data in, bump numrecs and log it. */
		xfs_btree_copy_keys(cur, kp, key, 1);
		xfs_btree_copy_ptrs(cur, pp, ptrp, 1);
		numrecs++;
		xfs_btree_set_numrecs(block, numrecs);
		xfs_btree_log_ptrs(cur, bp, ptr, numrecs);
		xfs_btree_log_keys(cur, bp, ptr, numrecs);
#ifdef DEBUG
		if (ptr < numrecs) {
			ASSERT(cur->bc_ops->keys_inorder(cur, kp,
				xfs_btree_key_addr(cur, ptr + 1, block)));
		}
#endif
	} else {
		/* It's a leaf. make a hole in the records */
		union xfs_btree_rec             *rp;

		rp = xfs_btree_rec_addr(cur, ptr, block);

		xfs_btree_shift_recs(cur, rp, 1, numrecs - ptr + 1);

		/* Now put the new data in, bump numrecs and log it. */
		xfs_btree_copy_recs(cur, rp, rec, 1);
		xfs_btree_set_numrecs(block, ++numrecs);
		xfs_btree_log_recs(cur, bp, ptr, numrecs);
#ifdef DEBUG
		if (ptr < numrecs) {
			ASSERT(cur->bc_ops->recs_inorder(cur, rp,
				xfs_btree_rec_addr(cur, ptr + 1, block)));
		}
#endif
	}

	/* Log the new number of records in the btree header. */
	xfs_btree_log_block(cur, bp, XFS_BB_NUMRECS);

	/*
	 * If we just inserted into a new tree block, we have to
	 * recalculate nkey here because nkey is out of date.
	 *
	 * Otherwise we're just updating an existing block (having shoved
	 * some records into the new tree block), so use the regular key
	 * update mechanism.
	 */
	if (bp && xfs_buf_daddr(bp) != old_bn) {
		xfs_btree_get_keys(cur, block, lkey);
	} else if (xfs_btree_needs_key_update(cur, optr)) {
		error = xfs_btree_update_keys(cur, level);
		if (error)
			goto error0;
	}

	/*
	 * If we are tracking the last record in the tree and
	 * we are at the far right edge of the tree, update it.
	 */
	if (xfs_btree_is_lastrec(cur, block, level)) {
		cur->bc_ops->update_lastrec(cur, block, rec,
					    ptr, LASTREC_INSREC);
	}

	/*
	 * Return the new block number, if any.
	 * If there is one, give back a record value and a cursor too.
	 */
	*ptrp = nptr;
	if (!xfs_btree_ptr_is_null(cur, &nptr)) {
		xfs_btree_copy_keys(cur, key, lkey, 1);
		*curp = ncur;
	}

	*stat = 1;
	return 0;

error0:
	if (ncur)
		xfs_btree_del_cursor(ncur, error);
	return error;
}

/*
 * Insert the record at the point referenced by cur.
 *
 * A multi-level split of the tree on insert will invalidate the original
 * cursor.  All callers of this function should assume that the cursor is
 * no longer valid and revalidate it.
 */
int
xfs_btree_insert(
	struct xfs_btree_cur	*cur,
	int			*stat)
{
	int			error;	/* error return value */
	int			i;	/* result value, 0 for failure */
	int			level;	/* current level number in btree */
	union xfs_btree_ptr	nptr;	/* new block number (split result) */
	struct xfs_btree_cur	*ncur;	/* new cursor (split result) */
	struct xfs_btree_cur	*pcur;	/* previous level's cursor */
	union xfs_btree_key	bkey;	/* key of block to insert */
	union xfs_btree_key	*key;
	union xfs_btree_rec	rec;	/* record to insert */

	level = 0;
	ncur = NULL;
	pcur = cur;
	key = &bkey;

	xfs_btree_set_ptr_null(cur, &nptr);

	/* Make a key out of the record data to be inserted, and save it. */
	cur->bc_ops->init_rec_from_cur(cur, &rec);
	cur->bc_ops->init_key_from_rec(key, &rec);

	/*
	 * Loop going up the tree, starting at the leaf level.
	 * Stop when we don't get a split block, that must mean that
	 * the insert is finished with this level.
	 */
	do {
		/*
		 * Insert nrec/nptr into this level of the tree.
		 * Note if we fail, nptr will be null.
		 */
		error = xfs_btree_insrec(pcur, level, &nptr, &rec, key,
				&ncur, &i);
		if (error) {
			if (pcur != cur)
				xfs_btree_del_cursor(pcur, XFS_BTREE_ERROR);
			goto error0;
		}

		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		level++;

		/*
		 * See if the cursor we just used is trash.
		 * Can't trash the caller's cursor, but otherwise we should
		 * if ncur is a new cursor or we're about to be done.
		 */
		if (pcur != cur &&
		    (ncur || xfs_btree_ptr_is_null(cur, &nptr))) {
			/* Save the state from the cursor before we trash it */
			if (cur->bc_ops->update_cursor)
				cur->bc_ops->update_cursor(pcur, cur);
			cur->bc_nlevels = pcur->bc_nlevels;
			xfs_btree_del_cursor(pcur, XFS_BTREE_NOERROR);
		}
		/* If we got a new cursor, switch to it. */
		if (ncur) {
			pcur = ncur;
			ncur = NULL;
		}
	} while (!xfs_btree_ptr_is_null(cur, &nptr));

	*stat = i;
	return 0;
error0:
	return error;
}

/*
 * Try to merge a non-leaf block back into the inode root.
 *
 * Note: the killroot names comes from the fact that we're effectively
 * killing the old root block.  But because we can't just delete the
 * inode we have to copy the single block it was pointing to into the
 * inode.
 */
STATIC int
xfs_btree_kill_iroot(
	struct xfs_btree_cur	*cur)
{
	int			whichfork = cur->bc_ino.whichfork;
	struct xfs_inode	*ip = cur->bc_ino.ip;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	struct xfs_btree_block	*block;
	struct xfs_btree_block	*cblock;
	union xfs_btree_key	*kp;
	union xfs_btree_key	*ckp;
	union xfs_btree_ptr	*pp;
	union xfs_btree_ptr	*cpp;
	struct xfs_buf		*cbp;
	int			level;
	int			index;
	int			numrecs;
	int			error;
#ifdef DEBUG
	union xfs_btree_ptr	ptr;
#endif
	int			i;

	ASSERT(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE);
	ASSERT(cur->bc_nlevels > 1);

	/*
	 * Don't deal with the root block needs to be a leaf case.
	 * We're just going to turn the thing back into extents anyway.
	 */
	level = cur->bc_nlevels - 1;
	if (level == 1)
		goto out0;

	/*
	 * Give up if the root has multiple children.
	 */
	block = xfs_btree_get_iroot(cur);
	if (xfs_btree_get_numrecs(block) != 1)
		goto out0;

	cblock = xfs_btree_get_block(cur, level - 1, &cbp);
	numrecs = xfs_btree_get_numrecs(cblock);

	/*
	 * Only do this if the next level will fit.
	 * Then the data must be copied up to the inode,
	 * instead of freeing the root you free the next level.
	 */
	if (numrecs > cur->bc_ops->get_dmaxrecs(cur, level))
		goto out0;

	XFS_BTREE_STATS_INC(cur, killroot);

#ifdef DEBUG
	xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_LEFTSIB);
	ASSERT(xfs_btree_ptr_is_null(cur, &ptr));
	xfs_btree_get_sibling(cur, block, &ptr, XFS_BB_RIGHTSIB);
	ASSERT(xfs_btree_ptr_is_null(cur, &ptr));
#endif

	index = numrecs - cur->bc_ops->get_maxrecs(cur, level);
	if (index) {
		xfs_iroot_realloc(cur->bc_ino.ip, index,
				  cur->bc_ino.whichfork);
		block = ifp->if_broot;
	}

	be16_add_cpu(&block->bb_numrecs, index);
	ASSERT(block->bb_numrecs == cblock->bb_numrecs);

	kp = xfs_btree_key_addr(cur, 1, block);
	ckp = xfs_btree_key_addr(cur, 1, cblock);
	xfs_btree_copy_keys(cur, kp, ckp, numrecs);

	pp = xfs_btree_ptr_addr(cur, 1, block);
	cpp = xfs_btree_ptr_addr(cur, 1, cblock);

	for (i = 0; i < numrecs; i++) {
		error = xfs_btree_debug_check_ptr(cur, cpp, i, level - 1);
		if (error)
			return error;
	}

	xfs_btree_copy_ptrs(cur, pp, cpp, numrecs);

	error = xfs_btree_free_block(cur, cbp);
	if (error)
		return error;

	cur->bc_levels[level - 1].bp = NULL;
	be16_add_cpu(&block->bb_level, -1);
	xfs_trans_log_inode(cur->bc_tp, ip,
		XFS_ILOG_CORE | xfs_ilog_fbroot(cur->bc_ino.whichfork));
	cur->bc_nlevels--;
out0:
	return 0;
}

/*
 * Kill the current root node, and replace it with it's only child node.
 */
STATIC int
xfs_btree_kill_root(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp,
	int			level,
	union xfs_btree_ptr	*newroot)
{
	int			error;

	XFS_BTREE_STATS_INC(cur, killroot);

	/*
	 * Update the root pointer, decreasing the level by 1 and then
	 * free the old root.
	 */
	cur->bc_ops->set_root(cur, newroot, -1);

	error = xfs_btree_free_block(cur, bp);
	if (error)
		return error;

	cur->bc_levels[level].bp = NULL;
	cur->bc_levels[level].ra = 0;
	cur->bc_nlevels--;

	return 0;
}

STATIC int
xfs_btree_dec_cursor(
	struct xfs_btree_cur	*cur,
	int			level,
	int			*stat)
{
	int			error;
	int			i;

	if (level > 0) {
		error = xfs_btree_decrement(cur, level, &i);
		if (error)
			return error;
	}

	*stat = 1;
	return 0;
}

/*
 * Single level of the btree record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int					/* error */
xfs_btree_delrec(
	struct xfs_btree_cur	*cur,		/* btree cursor */
	int			level,		/* level removing record from */
	int			*stat)		/* fail/done/go-on */
{
	struct xfs_btree_block	*block;		/* btree block */
	union xfs_btree_ptr	cptr;		/* current block ptr */
	struct xfs_buf		*bp;		/* buffer for block */
	int			error;		/* error return value */
	int			i;		/* loop counter */
	union xfs_btree_ptr	lptr;		/* left sibling block ptr */
	struct xfs_buf		*lbp;		/* left buffer pointer */
	struct xfs_btree_block	*left;		/* left btree block */
	int			lrecs = 0;	/* left record count */
	int			ptr;		/* key/record index */
	union xfs_btree_ptr	rptr;		/* right sibling block ptr */
	struct xfs_buf		*rbp;		/* right buffer pointer */
	struct xfs_btree_block	*right;		/* right btree block */
	struct xfs_btree_block	*rrblock;	/* right-right btree block */
	struct xfs_buf		*rrbp;		/* right-right buffer pointer */
	int			rrecs = 0;	/* right record count */
	struct xfs_btree_cur	*tcur;		/* temporary btree cursor */
	int			numrecs;	/* temporary numrec count */

	tcur = NULL;

	/* Get the index of the entry being deleted, check for nothing there. */
	ptr = cur->bc_levels[level].ptr;
	if (ptr == 0) {
		*stat = 0;
		return 0;
	}

	/* Get the buffer & block containing the record or key/ptr. */
	block = xfs_btree_get_block(cur, level, &bp);
	numrecs = xfs_btree_get_numrecs(block);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, level, bp);
	if (error)
		goto error0;
#endif

	/* Fail if we're off the end of the block. */
	if (ptr > numrecs) {
		*stat = 0;
		return 0;
	}

	XFS_BTREE_STATS_INC(cur, delrec);
	XFS_BTREE_STATS_ADD(cur, moves, numrecs - ptr);

	/* Excise the entries being deleted. */
	if (level > 0) {
		/* It's a nonleaf. operate on keys and ptrs */
		union xfs_btree_key	*lkp;
		union xfs_btree_ptr	*lpp;

		lkp = xfs_btree_key_addr(cur, ptr + 1, block);
		lpp = xfs_btree_ptr_addr(cur, ptr + 1, block);

		for (i = 0; i < numrecs - ptr; i++) {
			error = xfs_btree_debug_check_ptr(cur, lpp, i, level);
			if (error)
				goto error0;
		}

		if (ptr < numrecs) {
			xfs_btree_shift_keys(cur, lkp, -1, numrecs - ptr);
			xfs_btree_shift_ptrs(cur, lpp, -1, numrecs - ptr);
			xfs_btree_log_keys(cur, bp, ptr, numrecs - 1);
			xfs_btree_log_ptrs(cur, bp, ptr, numrecs - 1);
		}
	} else {
		/* It's a leaf. operate on records */
		if (ptr < numrecs) {
			xfs_btree_shift_recs(cur,
				xfs_btree_rec_addr(cur, ptr + 1, block),
				-1, numrecs - ptr);
			xfs_btree_log_recs(cur, bp, ptr, numrecs - 1);
		}
	}

	/*
	 * Decrement and log the number of entries in the block.
	 */
	xfs_btree_set_numrecs(block, --numrecs);
	xfs_btree_log_block(cur, bp, XFS_BB_NUMRECS);

	/*
	 * If we are tracking the last record in the tree and
	 * we are at the far right edge of the tree, update it.
	 */
	if (xfs_btree_is_lastrec(cur, block, level)) {
		cur->bc_ops->update_lastrec(cur, block, NULL,
					    ptr, LASTREC_DELREC);
	}

	/*
	 * We're at the root level.  First, shrink the root block in-memory.
	 * Try to get rid of the next level down.  If we can't then there's
	 * nothing left to do.
	 */
	if (level == cur->bc_nlevels - 1) {
		if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) {
			xfs_iroot_realloc(cur->bc_ino.ip, -1,
					  cur->bc_ino.whichfork);

			error = xfs_btree_kill_iroot(cur);
			if (error)
				goto error0;

			error = xfs_btree_dec_cursor(cur, level, stat);
			if (error)
				goto error0;
			*stat = 1;
			return 0;
		}

		/*
		 * If this is the root level, and there's only one entry left,
		 * and it's NOT the leaf level, then we can get rid of this
		 * level.
		 */
		if (numrecs == 1 && level > 0) {
			union xfs_btree_ptr	*pp;
			/*
			 * pp is still set to the first pointer in the block.
			 * Make it the new root of the btree.
			 */
			pp = xfs_btree_ptr_addr(cur, 1, block);
			error = xfs_btree_kill_root(cur, bp, level, pp);
			if (error)
				goto error0;
		} else if (level > 0) {
			error = xfs_btree_dec_cursor(cur, level, stat);
			if (error)
				goto error0;
		}
		*stat = 1;
		return 0;
	}

	/*
	 * If we deleted the leftmost entry in the block, update the
	 * key values above us in the tree.
	 */
	if (xfs_btree_needs_key_update(cur, ptr)) {
		error = xfs_btree_update_keys(cur, level);
		if (error)
			goto error0;
	}

	/*
	 * If the number of records remaining in the block is at least
	 * the minimum, we're done.
	 */
	if (numrecs >= cur->bc_ops->get_minrecs(cur, level)) {
		error = xfs_btree_dec_cursor(cur, level, stat);
		if (error)
			goto error0;
		return 0;
	}

	/*
	 * Otherwise, we have to move some records around to keep the
	 * tree balanced.  Look at the left and right sibling blocks to
	 * see if we can re-balance by moving only one record.
	 */
	xfs_btree_get_sibling(cur, block, &rptr, XFS_BB_RIGHTSIB);
	xfs_btree_get_sibling(cur, block, &lptr, XFS_BB_LEFTSIB);

	if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) {
		/*
		 * One child of root, need to get a chance to copy its contents
		 * into the root and delete it. Can't go up to next level,
		 * there's nothing to delete there.
		 */
		if (xfs_btree_ptr_is_null(cur, &rptr) &&
		    xfs_btree_ptr_is_null(cur, &lptr) &&
		    level == cur->bc_nlevels - 2) {
			error = xfs_btree_kill_iroot(cur);
			if (!error)
				error = xfs_btree_dec_cursor(cur, level, stat);
			if (error)
				goto error0;
			return 0;
		}
	}

	ASSERT(!xfs_btree_ptr_is_null(cur, &rptr) ||
	       !xfs_btree_ptr_is_null(cur, &lptr));

	/*
	 * Duplicate the cursor so our btree manipulations here won't
	 * disrupt the next level up.
	 */
	error = xfs_btree_dup_cursor(cur, &tcur);
	if (error)
		goto error0;

	/*
	 * If there's a right sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (!xfs_btree_ptr_is_null(cur, &rptr)) {
		/*
		 * Move the temp cursor to the last entry in the next block.
		 * Actually any entry but the first would suffice.
		 */
		i = xfs_btree_lastrec(tcur, level);
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		error = xfs_btree_increment(tcur, level, &i);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		i = xfs_btree_lastrec(tcur, level);
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		/* Grab a pointer to the block. */
		right = xfs_btree_get_block(tcur, level, &rbp);
#ifdef DEBUG
		error = xfs_btree_check_block(tcur, right, level, rbp);
		if (error)
			goto error0;
#endif
		/* Grab the current block number, for future use. */
		xfs_btree_get_sibling(tcur, right, &cptr, XFS_BB_LEFTSIB);

		/*
		 * If right block is full enough so that removing one entry
		 * won't make it too empty, and left-shifting an entry out
		 * of right to us works, we're done.
		 */
		if (xfs_btree_get_numrecs(right) - 1 >=
		    cur->bc_ops->get_minrecs(tcur, level)) {
			error = xfs_btree_lshift(tcur, level, &i);
			if (error)
				goto error0;
			if (i) {
				ASSERT(xfs_btree_get_numrecs(block) >=
				       cur->bc_ops->get_minrecs(tcur, level));

				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				tcur = NULL;

				error = xfs_btree_dec_cursor(cur, level, stat);
				if (error)
					goto error0;
				return 0;
			}
		}

		/*
		 * Otherwise, grab the number of records in right for
		 * future reference, and fix up the temp cursor to point
		 * to our block again (last record).
		 */
		rrecs = xfs_btree_get_numrecs(right);
		if (!xfs_btree_ptr_is_null(cur, &lptr)) {
			i = xfs_btree_firstrec(tcur, level);
			if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
				error = -EFSCORRUPTED;
				goto error0;
			}

			error = xfs_btree_decrement(tcur, level, &i);
			if (error)
				goto error0;
			if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
				error = -EFSCORRUPTED;
				goto error0;
			}
		}
	}

	/*
	 * If there's a left sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (!xfs_btree_ptr_is_null(cur, &lptr)) {
		/*
		 * Move the temp cursor to the first entry in the
		 * previous block.
		 */
		i = xfs_btree_firstrec(tcur, level);
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		error = xfs_btree_decrement(tcur, level, &i);
		if (error)
			goto error0;
		i = xfs_btree_firstrec(tcur, level);
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		/* Grab a pointer to the block. */
		left = xfs_btree_get_block(tcur, level, &lbp);
#ifdef DEBUG
		error = xfs_btree_check_block(cur, left, level, lbp);
		if (error)
			goto error0;
#endif
		/* Grab the current block number, for future use. */
		xfs_btree_get_sibling(tcur, left, &cptr, XFS_BB_RIGHTSIB);

		/*
		 * If left block is full enough so that removing one entry
		 * won't make it too empty, and right-shifting an entry out
		 * of left to us works, we're done.
		 */
		if (xfs_btree_get_numrecs(left) - 1 >=
		    cur->bc_ops->get_minrecs(tcur, level)) {
			error = xfs_btree_rshift(tcur, level, &i);
			if (error)
				goto error0;
			if (i) {
				ASSERT(xfs_btree_get_numrecs(block) >=
				       cur->bc_ops->get_minrecs(tcur, level));
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				tcur = NULL;
				if (level == 0)
					cur->bc_levels[0].ptr++;

				*stat = 1;
				return 0;
			}
		}

		/*
		 * Otherwise, grab the number of records in right for
		 * future reference.
		 */
		lrecs = xfs_btree_get_numrecs(left);
	}

	/* Delete the temp cursor, we're done with it. */
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	tcur = NULL;

	/* If here, we need to do a join to keep the tree balanced. */
	ASSERT(!xfs_btree_ptr_is_null(cur, &cptr));

	if (!xfs_btree_ptr_is_null(cur, &lptr) &&
	    lrecs + xfs_btree_get_numrecs(block) <=
			cur->bc_ops->get_maxrecs(cur, level)) {
		/*
		 * Set "right" to be the starting block,
		 * "left" to be the left neighbor.
		 */
		rptr = cptr;
		right = block;
		rbp = bp;
		error = xfs_btree_read_buf_block(cur, &lptr, 0, &left, &lbp);
		if (error)
			goto error0;

	/*
	 * If that won't work, see if we can join with the right neighbor block.
	 */
	} else if (!xfs_btree_ptr_is_null(cur, &rptr) &&
		   rrecs + xfs_btree_get_numrecs(block) <=
			cur->bc_ops->get_maxrecs(cur, level)) {
		/*
		 * Set "left" to be the starting block,
		 * "right" to be the right neighbor.
		 */
		lptr = cptr;
		left = block;
		lbp = bp;
		error = xfs_btree_read_buf_block(cur, &rptr, 0, &right, &rbp);
		if (error)
			goto error0;

	/*
	 * Otherwise, we can't fix the imbalance.
	 * Just return.  This is probably a logic error, but it's not fatal.
	 */
	} else {
		error = xfs_btree_dec_cursor(cur, level, stat);
		if (error)
			goto error0;
		return 0;
	}

	rrecs = xfs_btree_get_numrecs(right);
	lrecs = xfs_btree_get_numrecs(left);

	/*
	 * We're now going to join "left" and "right" by moving all the stuff
	 * in "right" to "left" and deleting "right".
	 */
	XFS_BTREE_STATS_ADD(cur, moves, rrecs);
	if (level > 0) {
		/* It's a non-leaf.  Move keys and pointers. */
		union xfs_btree_key	*lkp;	/* left btree key */
		union xfs_btree_ptr	*lpp;	/* left address pointer */
		union xfs_btree_key	*rkp;	/* right btree key */
		union xfs_btree_ptr	*rpp;	/* right address pointer */

		lkp = xfs_btree_key_addr(cur, lrecs + 1, left);
		lpp = xfs_btree_ptr_addr(cur, lrecs + 1, left);
		rkp = xfs_btree_key_addr(cur, 1, right);
		rpp = xfs_btree_ptr_addr(cur, 1, right);

		for (i = 1; i < rrecs; i++) {
			error = xfs_btree_debug_check_ptr(cur, rpp, i, level);
			if (error)
				goto error0;
		}

		xfs_btree_copy_keys(cur, lkp, rkp, rrecs);
		xfs_btree_copy_ptrs(cur, lpp, rpp, rrecs);

		xfs_btree_log_keys(cur, lbp, lrecs + 1, lrecs + rrecs);
		xfs_btree_log_ptrs(cur, lbp, lrecs + 1, lrecs + rrecs);
	} else {
		/* It's a leaf.  Move records.  */
		union xfs_btree_rec	*lrp;	/* left record pointer */
		union xfs_btree_rec	*rrp;	/* right record pointer */

		lrp = xfs_btree_rec_addr(cur, lrecs + 1, left);
		rrp = xfs_btree_rec_addr(cur, 1, right);

		xfs_btree_copy_recs(cur, lrp, rrp, rrecs);
		xfs_btree_log_recs(cur, lbp, lrecs + 1, lrecs + rrecs);
	}

	XFS_BTREE_STATS_INC(cur, join);

	/*
	 * Fix up the number of records and right block pointer in the
	 * surviving block, and log it.
	 */
	xfs_btree_set_numrecs(left, lrecs + rrecs);
	xfs_btree_get_sibling(cur, right, &cptr, XFS_BB_RIGHTSIB);
	xfs_btree_set_sibling(cur, left, &cptr, XFS_BB_RIGHTSIB);
	xfs_btree_log_block(cur, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);

	/* If there is a right sibling, point it to the remaining block. */
	xfs_btree_get_sibling(cur, left, &cptr, XFS_BB_RIGHTSIB);
	if (!xfs_btree_ptr_is_null(cur, &cptr)) {
		error = xfs_btree_read_buf_block(cur, &cptr, 0, &rrblock, &rrbp);
		if (error)
			goto error0;
		xfs_btree_set_sibling(cur, rrblock, &lptr, XFS_BB_LEFTSIB);
		xfs_btree_log_block(cur, rrbp, XFS_BB_LEFTSIB);
	}

	/* Free the deleted block. */
	error = xfs_btree_free_block(cur, rbp);
	if (error)
		goto error0;

	/*
	 * If we joined with the left neighbor, set the buffer in the
	 * cursor to the left block, and fix up the index.
	 */
	if (bp != lbp) {
		cur->bc_levels[level].bp = lbp;
		cur->bc_levels[level].ptr += lrecs;
		cur->bc_levels[level].ra = 0;
	}
	/*
	 * If we joined with the right neighbor and there's a level above
	 * us, increment the cursor at that level.
	 */
	else if ((cur->bc_flags & XFS_BTREE_ROOT_IN_INODE) ||
		   (level + 1 < cur->bc_nlevels)) {
		error = xfs_btree_increment(cur, level + 1, &i);
		if (error)
			goto error0;
	}

	/*
	 * Readjust the ptr at this level if it's not a leaf, since it's
	 * still pointing at the deletion point, which makes the cursor
	 * inconsistent.  If this makes the ptr 0, the caller fixes it up.
	 * We can't use decrement because it would change the next level up.
	 */
	if (level > 0)
		cur->bc_levels[level].ptr--;

	/*
	 * We combined blocks, so we have to update the parent keys if the
	 * btree supports overlapped intervals.  However,
	 * bc_levels[level + 1].ptr points to the old block so that the caller
	 * knows which record to delete.  Therefore, the caller must be savvy
	 * enough to call updkeys for us if we return stat == 2.  The other
	 * exit points from this function don't require deletions further up
	 * the tree, so they can call updkeys directly.
	 */

	/* Return value means the next level up has something to do. */
	*stat = 2;
	return 0;

error0:
	if (tcur)
		xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* error */
xfs_btree_delete(
	struct xfs_btree_cur	*cur,
	int			*stat)	/* success/failure */
{
	int			error;	/* error return value */
	int			level;
	int			i;
	bool			joined = false;

	/*
	 * Go up the tree, starting at leaf level.
	 *
	 * If 2 is returned then a join was done; go to the next level.
	 * Otherwise we are done.
	 */
	for (level = 0, i = 2; i == 2; level++) {
		error = xfs_btree_delrec(cur, level, &i);
		if (error)
			goto error0;
		if (i == 2)
			joined = true;
	}

	/*
	 * If we combined blocks as part of deleting the record, delrec won't
	 * have updated the parent high keys so we have to do that here.
	 */
	if (joined && (cur->bc_flags & XFS_BTREE_OVERLAPPING)) {
		error = xfs_btree_updkeys_force(cur, 0);
		if (error)
			goto error0;
	}

	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_levels[level].ptr == 0) {
				error = xfs_btree_decrement(cur, level, &i);
				if (error)
					goto error0;
				break;
			}
		}
	}

	*stat = i;
	return 0;
error0:
	return error;
}

/*
 * Get the data from the pointed-to record.
 */
int					/* error */
xfs_btree_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	union xfs_btree_rec	**recp,	/* output: btree record */
	int			*stat)	/* output: success/failure */
{
	struct xfs_btree_block	*block;	/* btree block */
	struct xfs_buf		*bp;	/* buffer pointer */
	int			ptr;	/* record number */
#ifdef DEBUG
	int			error;	/* error return value */
#endif

	ptr = cur->bc_levels[0].ptr;
	block = xfs_btree_get_block(cur, 0, &bp);

#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, 0, bp);
	if (error)
		return error;
#endif

	/*
	 * Off the right end or left end, return failure.
	 */
	if (ptr > xfs_btree_get_numrecs(block) || ptr <= 0) {
		*stat = 0;
		return 0;
	}

	/*
	 * Point to the record and extract its data.
	 */
	*recp = xfs_btree_rec_addr(cur, ptr, block);
	*stat = 1;
	return 0;
}

/* Visit a block in a btree. */
STATIC int
xfs_btree_visit_block(
	struct xfs_btree_cur		*cur,
	int				level,
	xfs_btree_visit_blocks_fn	fn,
	void				*data)
{
	struct xfs_btree_block		*block;
	struct xfs_buf			*bp;
	union xfs_btree_ptr		rptr;
	int				error;

	/* do right sibling readahead */
	xfs_btree_readahead(cur, level, XFS_BTCUR_RIGHTRA);
	block = xfs_btree_get_block(cur, level, &bp);

	/* process the block */
	error = fn(cur, level, data);
	if (error)
		return error;

	/* now read rh sibling block for next iteration */
	xfs_btree_get_sibling(cur, block, &rptr, XFS_BB_RIGHTSIB);
	if (xfs_btree_ptr_is_null(cur, &rptr))
		return -ENOENT;

	/*
	 * We only visit blocks once in this walk, so we have to avoid the
	 * internal xfs_btree_lookup_get_block() optimisation where it will
	 * return the same block without checking if the right sibling points
	 * back to us and creates a cyclic reference in the btree.
	 */
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (be64_to_cpu(rptr.l) == XFS_DADDR_TO_FSB(cur->bc_mp,
							xfs_buf_daddr(bp)))
			return -EFSCORRUPTED;
	} else {
		if (be32_to_cpu(rptr.s) == xfs_daddr_to_agbno(cur->bc_mp,
							xfs_buf_daddr(bp)))
			return -EFSCORRUPTED;
	}
	return xfs_btree_lookup_get_block(cur, level, &rptr, &block);
}


/* Visit every block in a btree. */
int
xfs_btree_visit_blocks(
	struct xfs_btree_cur		*cur,
	xfs_btree_visit_blocks_fn	fn,
	unsigned int			flags,
	void				*data)
{
	union xfs_btree_ptr		lptr;
	int				level;
	struct xfs_btree_block		*block = NULL;
	int				error = 0;

	cur->bc_ops->init_ptr_from_cur(cur, &lptr);

	/* for each level */
	for (level = cur->bc_nlevels - 1; level >= 0; level--) {
		/* grab the left hand block */
		error = xfs_btree_lookup_get_block(cur, level, &lptr, &block);
		if (error)
			return error;

		/* readahead the left most block for the next level down */
		if (level > 0) {
			union xfs_btree_ptr     *ptr;

			ptr = xfs_btree_ptr_addr(cur, 1, block);
			xfs_btree_readahead_ptr(cur, ptr, 1);

			/* save for the next iteration of the loop */
			xfs_btree_copy_ptrs(cur, &lptr, ptr, 1);

			if (!(flags & XFS_BTREE_VISIT_LEAVES))
				continue;
		} else if (!(flags & XFS_BTREE_VISIT_RECORDS)) {
			continue;
		}

		/* for each buffer in the level */
		do {
			error = xfs_btree_visit_block(cur, level, fn, data);
		} while (!error);

		if (error != -ENOENT)
			return error;
	}

	return 0;
}

/*
 * Change the owner of a btree.
 *
 * The mechanism we use here is ordered buffer logging. Because we don't know
 * how many buffers were are going to need to modify, we don't really want to
 * have to make transaction reservations for the worst case of every buffer in a
 * full size btree as that may be more space that we can fit in the log....
 *
 * We do the btree walk in the most optimal manner possible - we have sibling
 * pointers so we can just walk all the blocks on each level from left to right
 * in a single pass, and then move to the next level and do the same. We can
 * also do readahead on the sibling pointers to get IO moving more quickly,
 * though for slow disks this is unlikely to make much difference to performance
 * as the amount of CPU work we have to do before moving to the next block is
 * relatively small.
 *
 * For each btree block that we load, modify the owner appropriately, set the
 * buffer as an ordered buffer and log it appropriately. We need to ensure that
 * we mark the region we change dirty so that if the buffer is relogged in
 * a subsequent transaction the changes we make here as an ordered buffer are
 * correctly relogged in that transaction.  If we are in recovery context, then
 * just queue the modified buffer as delayed write buffer so the transaction
 * recovery completion writes the changes to disk.
 */
struct xfs_btree_block_change_owner_info {
	uint64_t		new_owner;
	struct list_head	*buffer_list;
};

static int
xfs_btree_block_change_owner(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*data)
{
	struct xfs_btree_block_change_owner_info	*bbcoi = data;
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;

	/* modify the owner */
	block = xfs_btree_get_block(cur, level, &bp);
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS) {
		if (block->bb_u.l.bb_owner == cpu_to_be64(bbcoi->new_owner))
			return 0;
		block->bb_u.l.bb_owner = cpu_to_be64(bbcoi->new_owner);
	} else {
		if (block->bb_u.s.bb_owner == cpu_to_be32(bbcoi->new_owner))
			return 0;
		block->bb_u.s.bb_owner = cpu_to_be32(bbcoi->new_owner);
	}

	/*
	 * If the block is a root block hosted in an inode, we might not have a
	 * buffer pointer here and we shouldn't attempt to log the change as the
	 * information is already held in the inode and discarded when the root
	 * block is formatted into the on-disk inode fork. We still change it,
	 * though, so everything is consistent in memory.
	 */
	if (!bp) {
		ASSERT(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE);
		ASSERT(level == cur->bc_nlevels - 1);
		return 0;
	}

	if (cur->bc_tp) {
		if (!xfs_trans_ordered_buf(cur->bc_tp, bp)) {
			xfs_btree_log_block(cur, bp, XFS_BB_OWNER);
			return -EAGAIN;
		}
	} else {
		xfs_buf_delwri_queue(bp, bbcoi->buffer_list);
	}

	return 0;
}

int
xfs_btree_change_owner(
	struct xfs_btree_cur	*cur,
	uint64_t		new_owner,
	struct list_head	*buffer_list)
{
	struct xfs_btree_block_change_owner_info	bbcoi;

	bbcoi.new_owner = new_owner;
	bbcoi.buffer_list = buffer_list;

	return xfs_btree_visit_blocks(cur, xfs_btree_block_change_owner,
			XFS_BTREE_VISIT_ALL, &bbcoi);
}

/* Verify the v5 fields of a long-format btree block. */
xfs_failaddr_t
xfs_btree_lblock_v5hdr_verify(
	struct xfs_buf		*bp,
	uint64_t		owner)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);

	if (!xfs_has_crc(mp))
		return __this_address;
	if (!uuid_equal(&block->bb_u.l.bb_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	if (block->bb_u.l.bb_blkno != cpu_to_be64(xfs_buf_daddr(bp)))
		return __this_address;
	if (owner != XFS_RMAP_OWN_UNKNOWN &&
	    be64_to_cpu(block->bb_u.l.bb_owner) != owner)
		return __this_address;
	return NULL;
}

/* Verify a long-format btree block. */
xfs_failaddr_t
xfs_btree_lblock_verify(
	struct xfs_buf		*bp,
	unsigned int		max_recs)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	xfs_fsblock_t		fsb;
	xfs_failaddr_t		fa;

	/* numrecs verification */
	if (be16_to_cpu(block->bb_numrecs) > max_recs)
		return __this_address;

	/* sibling pointer verification */
	fsb = XFS_DADDR_TO_FSB(mp, xfs_buf_daddr(bp));
	fa = xfs_btree_check_lblock_siblings(mp, NULL, -1, fsb,
			block->bb_u.l.bb_leftsib);
	if (!fa)
		fa = xfs_btree_check_lblock_siblings(mp, NULL, -1, fsb,
				block->bb_u.l.bb_rightsib);
	return fa;
}

/**
 * xfs_btree_sblock_v5hdr_verify() -- verify the v5 fields of a short-format
 *				      btree block
 *
 * @bp: buffer containing the btree block
 */
xfs_failaddr_t
xfs_btree_sblock_v5hdr_verify(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_perag	*pag = bp->b_pag;

	if (!xfs_has_crc(mp))
		return __this_address;
	if (!uuid_equal(&block->bb_u.s.bb_uuid, &mp->m_sb.sb_meta_uuid))
		return __this_address;
	if (block->bb_u.s.bb_blkno != cpu_to_be64(xfs_buf_daddr(bp)))
		return __this_address;
	if (pag && be32_to_cpu(block->bb_u.s.bb_owner) != pag->pag_agno)
		return __this_address;
	return NULL;
}

/**
 * xfs_btree_sblock_verify() -- verify a short-format btree block
 *
 * @bp: buffer containing the btree block
 * @max_recs: maximum records allowed in this btree node
 */
xfs_failaddr_t
xfs_btree_sblock_verify(
	struct xfs_buf		*bp,
	unsigned int		max_recs)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	xfs_agblock_t		agbno;
	xfs_failaddr_t		fa;

	/* numrecs verification */
	if (be16_to_cpu(block->bb_numrecs) > max_recs)
		return __this_address;

	/* sibling pointer verification */
	agbno = xfs_daddr_to_agbno(mp, xfs_buf_daddr(bp));
	fa = xfs_btree_check_sblock_siblings(bp->b_pag, NULL, -1, agbno,
			block->bb_u.s.bb_leftsib);
	if (!fa)
		fa = xfs_btree_check_sblock_siblings(bp->b_pag, NULL, -1, agbno,
				block->bb_u.s.bb_rightsib);
	return fa;
}

/*
 * For the given limits on leaf and keyptr records per block, calculate the
 * height of the tree needed to index the number of leaf records.
 */
unsigned int
xfs_btree_compute_maxlevels(
	const unsigned int	*limits,
	unsigned long long	records)
{
	unsigned long long	level_blocks = howmany_64(records, limits[0]);
	unsigned int		height = 1;

	while (level_blocks > 1) {
		level_blocks = howmany_64(level_blocks, limits[1]);
		height++;
	}

	return height;
}

/*
 * For the given limits on leaf and keyptr records per block, calculate the
 * number of blocks needed to index the given number of leaf records.
 */
unsigned long long
xfs_btree_calc_size(
	const unsigned int	*limits,
	unsigned long long	records)
{
	unsigned long long	level_blocks = howmany_64(records, limits[0]);
	unsigned long long	blocks = level_blocks;

	while (level_blocks > 1) {
		level_blocks = howmany_64(level_blocks, limits[1]);
		blocks += level_blocks;
	}

	return blocks;
}

/*
 * Given a number of available blocks for the btree to consume with records and
 * pointers, calculate the height of the tree needed to index all the records
 * that space can hold based on the number of pointers each interior node
 * holds.
 *
 * We start by assuming a single level tree consumes a single block, then track
 * the number of blocks each node level consumes until we no longer have space
 * to store the next node level. At this point, we are indexing all the leaf
 * blocks in the space, and there's no more free space to split the tree any
 * further. That's our maximum btree height.
 */
unsigned int
xfs_btree_space_to_height(
	const unsigned int	*limits,
	unsigned long long	leaf_blocks)
{
	unsigned long long	node_blocks = limits[1];
	unsigned long long	blocks_left = leaf_blocks - 1;
	unsigned int		height = 1;

	if (leaf_blocks < 1)
		return 0;

	while (node_blocks < blocks_left) {
		blocks_left -= node_blocks;
		node_blocks *= limits[1];
		height++;
	}

	return height;
}

/*
 * Query a regular btree for all records overlapping a given interval.
 * Start with a LE lookup of the key of low_rec and return all records
 * until we find a record with a key greater than the key of high_rec.
 */
STATIC int
xfs_btree_simple_query_range(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*low_key,
	const union xfs_btree_key	*high_key,
	xfs_btree_query_range_fn	fn,
	void				*priv)
{
	union xfs_btree_rec		*recp;
	union xfs_btree_key		rec_key;
	int64_t				diff;
	int				stat;
	bool				firstrec = true;
	int				error;

	ASSERT(cur->bc_ops->init_high_key_from_rec);
	ASSERT(cur->bc_ops->diff_two_keys);

	/*
	 * Find the leftmost record.  The btree cursor must be set
	 * to the low record used to generate low_key.
	 */
	stat = 0;
	error = xfs_btree_lookup(cur, XFS_LOOKUP_LE, &stat);
	if (error)
		goto out;

	/* Nothing?  See if there's anything to the right. */
	if (!stat) {
		error = xfs_btree_increment(cur, 0, &stat);
		if (error)
			goto out;
	}

	while (stat) {
		/* Find the record. */
		error = xfs_btree_get_rec(cur, &recp, &stat);
		if (error || !stat)
			break;

		/* Skip if high_key(rec) < low_key. */
		if (firstrec) {
			cur->bc_ops->init_high_key_from_rec(&rec_key, recp);
			firstrec = false;
			diff = cur->bc_ops->diff_two_keys(cur, low_key,
					&rec_key);
			if (diff > 0)
				goto advloop;
		}

		/* Stop if high_key < low_key(rec). */
		cur->bc_ops->init_key_from_rec(&rec_key, recp);
		diff = cur->bc_ops->diff_two_keys(cur, &rec_key, high_key);
		if (diff > 0)
			break;

		/* Callback */
		error = fn(cur, recp, priv);
		if (error)
			break;

advloop:
		/* Move on to the next record. */
		error = xfs_btree_increment(cur, 0, &stat);
		if (error)
			break;
	}

out:
	return error;
}

/*
 * Query an overlapped interval btree for all records overlapping a given
 * interval.  This function roughly follows the algorithm given in
 * "Interval Trees" of _Introduction to Algorithms_, which is section
 * 14.3 in the 2nd and 3rd editions.
 *
 * First, generate keys for the low and high records passed in.
 *
 * For any leaf node, generate the high and low keys for the record.
 * If the record keys overlap with the query low/high keys, pass the
 * record to the function iterator.
 *
 * For any internal node, compare the low and high keys of each
 * pointer against the query low/high keys.  If there's an overlap,
 * follow the pointer.
 *
 * As an optimization, we stop scanning a block when we find a low key
 * that is greater than the query's high key.
 */
STATIC int
xfs_btree_overlapped_query_range(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_key	*low_key,
	const union xfs_btree_key	*high_key,
	xfs_btree_query_range_fn	fn,
	void				*priv)
{
	union xfs_btree_ptr		ptr;
	union xfs_btree_ptr		*pp;
	union xfs_btree_key		rec_key;
	union xfs_btree_key		rec_hkey;
	union xfs_btree_key		*lkp;
	union xfs_btree_key		*hkp;
	union xfs_btree_rec		*recp;
	struct xfs_btree_block		*block;
	int64_t				ldiff;
	int64_t				hdiff;
	int				level;
	struct xfs_buf			*bp;
	int				i;
	int				error;

	/* Load the root of the btree. */
	level = cur->bc_nlevels - 1;
	cur->bc_ops->init_ptr_from_cur(cur, &ptr);
	error = xfs_btree_lookup_get_block(cur, level, &ptr, &block);
	if (error)
		return error;
	xfs_btree_get_block(cur, level, &bp);
	trace_xfs_btree_overlapped_query_range(cur, level, bp);
#ifdef DEBUG
	error = xfs_btree_check_block(cur, block, level, bp);
	if (error)
		goto out;
#endif
	cur->bc_levels[level].ptr = 1;

	while (level < cur->bc_nlevels) {
		block = xfs_btree_get_block(cur, level, &bp);

		/* End of node, pop back towards the root. */
		if (cur->bc_levels[level].ptr >
					be16_to_cpu(block->bb_numrecs)) {
pop_up:
			if (level < cur->bc_nlevels - 1)
				cur->bc_levels[level + 1].ptr++;
			level++;
			continue;
		}

		if (level == 0) {
			/* Handle a leaf node. */
			recp = xfs_btree_rec_addr(cur, cur->bc_levels[0].ptr,
					block);

			cur->bc_ops->init_high_key_from_rec(&rec_hkey, recp);
			ldiff = cur->bc_ops->diff_two_keys(cur, &rec_hkey,
					low_key);

			cur->bc_ops->init_key_from_rec(&rec_key, recp);
			hdiff = cur->bc_ops->diff_two_keys(cur, high_key,
					&rec_key);

			/*
			 * If (record's high key >= query's low key) and
			 *    (query's high key >= record's low key), then
			 * this record overlaps the query range; callback.
			 */
			if (ldiff >= 0 && hdiff >= 0) {
				error = fn(cur, recp, priv);
				if (error)
					break;
			} else if (hdiff < 0) {
				/* Record is larger than high key; pop. */
				goto pop_up;
			}
			cur->bc_levels[level].ptr++;
			continue;
		}

		/* Handle an internal node. */
		lkp = xfs_btree_key_addr(cur, cur->bc_levels[level].ptr, block);
		hkp = xfs_btree_high_key_addr(cur, cur->bc_levels[level].ptr,
				block);
		pp = xfs_btree_ptr_addr(cur, cur->bc_levels[level].ptr, block);

		ldiff = cur->bc_ops->diff_two_keys(cur, hkp, low_key);
		hdiff = cur->bc_ops->diff_two_keys(cur, high_key, lkp);

		/*
		 * If (pointer's high key >= query's low key) and
		 *    (query's high key >= pointer's low key), then
		 * this record overlaps the query range; follow pointer.
		 */
		if (ldiff >= 0 && hdiff >= 0) {
			level--;
			error = xfs_btree_lookup_get_block(cur, level, pp,
					&block);
			if (error)
				goto out;
			xfs_btree_get_block(cur, level, &bp);
			trace_xfs_btree_overlapped_query_range(cur, level, bp);
#ifdef DEBUG
			error = xfs_btree_check_block(cur, block, level, bp);
			if (error)
				goto out;
#endif
			cur->bc_levels[level].ptr = 1;
			continue;
		} else if (hdiff < 0) {
			/* The low key is larger than the upper range; pop. */
			goto pop_up;
		}
		cur->bc_levels[level].ptr++;
	}

out:
	/*
	 * If we don't end this function with the cursor pointing at a record
	 * block, a subsequent non-error cursor deletion will not release
	 * node-level buffers, causing a buffer leak.  This is quite possible
	 * with a zero-results range query, so release the buffers if we
	 * failed to return any results.
	 */
	if (cur->bc_levels[0].bp == NULL) {
		for (i = 0; i < cur->bc_nlevels; i++) {
			if (cur->bc_levels[i].bp) {
				xfs_trans_brelse(cur->bc_tp,
						cur->bc_levels[i].bp);
				cur->bc_levels[i].bp = NULL;
				cur->bc_levels[i].ptr = 0;
				cur->bc_levels[i].ra = 0;
			}
		}
	}

	return error;
}

/*
 * Query a btree for all records overlapping a given interval of keys.  The
 * supplied function will be called with each record found; return one of the
 * XFS_BTREE_QUERY_RANGE_{CONTINUE,ABORT} values or the usual negative error
 * code.  This function returns -ECANCELED, zero, or a negative error code.
 */
int
xfs_btree_query_range(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_irec	*low_rec,
	const union xfs_btree_irec	*high_rec,
	xfs_btree_query_range_fn	fn,
	void				*priv)
{
	union xfs_btree_rec		rec;
	union xfs_btree_key		low_key;
	union xfs_btree_key		high_key;

	/* Find the keys of both ends of the interval. */
	cur->bc_rec = *high_rec;
	cur->bc_ops->init_rec_from_cur(cur, &rec);
	cur->bc_ops->init_key_from_rec(&high_key, &rec);

	cur->bc_rec = *low_rec;
	cur->bc_ops->init_rec_from_cur(cur, &rec);
	cur->bc_ops->init_key_from_rec(&low_key, &rec);

	/* Enforce low key < high key. */
	if (cur->bc_ops->diff_two_keys(cur, &low_key, &high_key) > 0)
		return -EINVAL;

	if (!(cur->bc_flags & XFS_BTREE_OVERLAPPING))
		return xfs_btree_simple_query_range(cur, &low_key,
				&high_key, fn, priv);
	return xfs_btree_overlapped_query_range(cur, &low_key, &high_key,
			fn, priv);
}

/* Query a btree for all records. */
int
xfs_btree_query_all(
	struct xfs_btree_cur		*cur,
	xfs_btree_query_range_fn	fn,
	void				*priv)
{
	union xfs_btree_key		low_key;
	union xfs_btree_key		high_key;

	memset(&cur->bc_rec, 0, sizeof(cur->bc_rec));
	memset(&low_key, 0, sizeof(low_key));
	memset(&high_key, 0xFF, sizeof(high_key));

	return xfs_btree_simple_query_range(cur, &low_key, &high_key, fn, priv);
}

static int
xfs_btree_count_blocks_helper(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*data)
{
	xfs_extlen_t		*blocks = data;
	(*blocks)++;

	return 0;
}

/* Count the blocks in a btree and return the result in *blocks. */
int
xfs_btree_count_blocks(
	struct xfs_btree_cur	*cur,
	xfs_extlen_t		*blocks)
{
	*blocks = 0;
	return xfs_btree_visit_blocks(cur, xfs_btree_count_blocks_helper,
			XFS_BTREE_VISIT_ALL, blocks);
}

/* Compare two btree pointers. */
int64_t
xfs_btree_diff_two_ptrs(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*a,
	const union xfs_btree_ptr	*b)
{
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return (int64_t)be64_to_cpu(a->l) - be64_to_cpu(b->l);
	return (int64_t)be32_to_cpu(a->s) - be32_to_cpu(b->s);
}

/* If there's an extent, we're done. */
STATIC int
xfs_btree_has_record_helper(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	return -ECANCELED;
}

/* Is there a record covering a given range of keys? */
int
xfs_btree_has_record(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_irec	*low,
	const union xfs_btree_irec	*high,
	bool				*exists)
{
	int				error;

	error = xfs_btree_query_range(cur, low, high,
			&xfs_btree_has_record_helper, NULL);
	if (error == -ECANCELED) {
		*exists = true;
		return 0;
	}
	*exists = false;
	return error;
}

/* Are there more records in this btree? */
bool
xfs_btree_has_more_records(
	struct xfs_btree_cur	*cur)
{
	struct xfs_btree_block	*block;
	struct xfs_buf		*bp;

	block = xfs_btree_get_block(cur, 0, &bp);

	/* There are still records in this block. */
	if (cur->bc_levels[0].ptr < xfs_btree_get_numrecs(block))
		return true;

	/* There are more record blocks. */
	if (cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return block->bb_u.l.bb_rightsib != cpu_to_be64(NULLFSBLOCK);
	else
		return block->bb_u.s.bb_rightsib != cpu_to_be32(NULLAGBLOCK);
}

/* Set up all the btree cursor caches. */
int __init
xfs_btree_init_cur_caches(void)
{
	int		error;

	error = xfs_allocbt_init_cur_cache();
	if (error)
		return error;
	error = xfs_inobt_init_cur_cache();
	if (error)
		goto err;
	error = xfs_bmbt_init_cur_cache();
	if (error)
		goto err;
	error = xfs_rmapbt_init_cur_cache();
	if (error)
		goto err;
	error = xfs_refcountbt_init_cur_cache();
	if (error)
		goto err;

	return 0;
err:
	xfs_btree_destroy_cur_caches();
	return error;
}

/* Destroy all the btree cursor caches, if they've been allocated. */
void
xfs_btree_destroy_cur_caches(void)
{
	xfs_allocbt_destroy_cur_cache();
	xfs_inobt_destroy_cur_cache();
	xfs_bmbt_destroy_cur_cache();
	xfs_rmapbt_destroy_cur_cache();
	xfs_refcountbt_destroy_cur_cache();
}
