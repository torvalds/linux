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
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_btree_trace.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_error.h"

/*
 * Prototypes for internal functions.
 */

STATIC void xfs_alloc_log_block(xfs_trans_t *, xfs_buf_t *, int);
STATIC void xfs_alloc_log_keys(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_alloc_log_ptrs(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_alloc_log_recs(xfs_btree_cur_t *, xfs_buf_t *, int, int);

/*
 * Internal functions.
 */

/*
 * Single level of the xfs_alloc_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* error */
xfs_alloc_delrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level removing record from */
	int			*stat)	/* fail/done/go-on */
{
	xfs_agf_t		*agf;	/* allocation group freelist header */
	xfs_alloc_block_t	*block;	/* btree block record/key lives in */
	xfs_agblock_t		bno;	/* btree block number */
	xfs_buf_t		*bp;	/* buffer for block */
	int			error;	/* error return value */
	int			i;	/* loop index */
	xfs_alloc_key_t		key;	/* kp points here if block is level 0 */
	xfs_agblock_t		lbno;	/* left block's block number */
	xfs_buf_t		*lbp;	/* left block's buffer pointer */
	xfs_alloc_block_t	*left;	/* left btree block */
	xfs_alloc_key_t		*lkp=NULL;	/* left block key pointer */
	xfs_alloc_ptr_t		*lpp=NULL;	/* left block address pointer */
	int			lrecs=0;	/* number of records in left block */
	xfs_alloc_rec_t		*lrp;	/* left block record pointer */
	xfs_mount_t		*mp;	/* mount structure */
	int			ptr;	/* index in btree block for this rec */
	xfs_agblock_t		rbno;	/* right block's block number */
	xfs_buf_t		*rbp;	/* right block's buffer pointer */
	xfs_alloc_block_t	*right;	/* right btree block */
	xfs_alloc_key_t		*rkp;	/* right block key pointer */
	xfs_alloc_ptr_t		*rpp;	/* right block address pointer */
	int			rrecs=0;	/* number of records in right block */
	int			numrecs;
	xfs_alloc_rec_t		*rrp;	/* right block record pointer */
	xfs_btree_cur_t		*tcur;	/* temporary btree cursor */

	/*
	 * Get the index of the entry being deleted, check for nothing there.
	 */
	ptr = cur->bc_ptrs[level];
	if (ptr == 0) {
		*stat = 0;
		return 0;
	}
	/*
	 * Get the buffer & block containing the record or key/ptr.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, level, bp)))
		return error;
#endif
	/*
	 * Fail if we're off the end of the block.
	 */
	numrecs = be16_to_cpu(block->bb_numrecs);
	if (ptr > numrecs) {
		*stat = 0;
		return 0;
	}
	XFS_STATS_INC(xs_abt_delrec);
	/*
	 * It's a nonleaf.  Excise the key and ptr being deleted, by
	 * sliding the entries past them down one.
	 * Log the changed areas of the block.
	 */
	if (level > 0) {
		lkp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
		lpp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < numrecs; i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(lpp[i]), level)))
				return error;
		}
#endif
		if (ptr < numrecs) {
			memmove(&lkp[ptr - 1], &lkp[ptr],
				(numrecs - ptr) * sizeof(*lkp));
			memmove(&lpp[ptr - 1], &lpp[ptr],
				(numrecs - ptr) * sizeof(*lpp));
			xfs_alloc_log_ptrs(cur, bp, ptr, numrecs - 1);
			xfs_alloc_log_keys(cur, bp, ptr, numrecs - 1);
		}
	}
	/*
	 * It's a leaf.  Excise the record being deleted, by sliding the
	 * entries past it down one.  Log the changed areas of the block.
	 */
	else {
		lrp = XFS_ALLOC_REC_ADDR(block, 1, cur);
		if (ptr < numrecs) {
			memmove(&lrp[ptr - 1], &lrp[ptr],
				(numrecs - ptr) * sizeof(*lrp));
			xfs_alloc_log_recs(cur, bp, ptr, numrecs - 1);
		}
		/*
		 * If it's the first record in the block, we'll need a key
		 * structure to pass up to the next level (updkey).
		 */
		if (ptr == 1) {
			key.ar_startblock = lrp->ar_startblock;
			key.ar_blockcount = lrp->ar_blockcount;
			lkp = &key;
		}
	}
	/*
	 * Decrement and log the number of entries in the block.
	 */
	numrecs--;
	block->bb_numrecs = cpu_to_be16(numrecs);
	xfs_alloc_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
	/*
	 * See if the longest free extent in the allocation group was
	 * changed by this operation.  True if it's the by-size btree, and
	 * this is the leaf level, and there is no right sibling block,
	 * and this was the last record.
	 */
	agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);
	mp = cur->bc_mp;

	if (level == 0 &&
	    cur->bc_btnum == XFS_BTNUM_CNT &&
	    be32_to_cpu(block->bb_rightsib) == NULLAGBLOCK &&
	    ptr > numrecs) {
		ASSERT(ptr == numrecs + 1);
		/*
		 * There are still records in the block.  Grab the size
		 * from the last one.
		 */
		if (numrecs) {
			rrp = XFS_ALLOC_REC_ADDR(block, numrecs, cur);
			agf->agf_longest = rrp->ar_blockcount;
		}
		/*
		 * No free extents left.
		 */
		else
			agf->agf_longest = 0;
		mp->m_perag[be32_to_cpu(agf->agf_seqno)].pagf_longest =
			be32_to_cpu(agf->agf_longest);
		xfs_alloc_log_agf(cur->bc_tp, cur->bc_private.a.agbp,
			XFS_AGF_LONGEST);
	}
	/*
	 * Is this the root level?  If so, we're almost done.
	 */
	if (level == cur->bc_nlevels - 1) {
		/*
		 * If this is the root level,
		 * and there's only one entry left,
		 * and it's NOT the leaf level,
		 * then we can get rid of this level.
		 */
		if (numrecs == 1 && level > 0) {
			/*
			 * lpp is still set to the first pointer in the block.
			 * Make it the new root of the btree.
			 */
			bno = be32_to_cpu(agf->agf_roots[cur->bc_btnum]);
			agf->agf_roots[cur->bc_btnum] = *lpp;
			be32_add_cpu(&agf->agf_levels[cur->bc_btnum], -1);
			mp->m_perag[be32_to_cpu(agf->agf_seqno)].pagf_levels[cur->bc_btnum]--;
			/*
			 * Put this buffer/block on the ag's freelist.
			 */
			error = xfs_alloc_put_freelist(cur->bc_tp,
					cur->bc_private.a.agbp, NULL, bno, 1);
			if (error)
				return error;
			/*
			 * Since blocks move to the free list without the
			 * coordination used in xfs_bmap_finish, we can't allow
			 * block to be available for reallocation and
			 * non-transaction writing (user data) until we know
			 * that the transaction that moved it to the free list
			 * is permanently on disk. We track the blocks by
			 * declaring these blocks as "busy"; the busy list is
			 * maintained on a per-ag basis and each transaction
			 * records which entries should be removed when the
			 * iclog commits to disk. If a busy block is
			 * allocated, the iclog is pushed up to the LSN
			 * that freed the block.
			 */
			xfs_alloc_mark_busy(cur->bc_tp,
				be32_to_cpu(agf->agf_seqno), bno, 1);

			xfs_trans_agbtree_delta(cur->bc_tp, -1);
			xfs_alloc_log_agf(cur->bc_tp, cur->bc_private.a.agbp,
				XFS_AGF_ROOTS | XFS_AGF_LEVELS);
			/*
			 * Update the cursor so there's one fewer level.
			 */
			xfs_btree_setbuf(cur, level, NULL);
			cur->bc_nlevels--;
		} else if (level > 0 &&
			   (error = xfs_btree_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * If we deleted the leftmost entry in the block, update the
	 * key values above us in the tree.
	 */
	if (ptr == 1 && (error = xfs_btree_updkey(cur, (union xfs_btree_key *)lkp, level + 1)))
		return error;
	/*
	 * If the number of records remaining in the block is at least
	 * the minimum, we're done.
	 */
	if (numrecs >= XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * Otherwise, we have to move some records around to keep the
	 * tree balanced.  Look at the left and right sibling blocks to
	 * see if we can re-balance by moving only one record.
	 */
	rbno = be32_to_cpu(block->bb_rightsib);
	lbno = be32_to_cpu(block->bb_leftsib);
	bno = NULLAGBLOCK;
	ASSERT(rbno != NULLAGBLOCK || lbno != NULLAGBLOCK);
	/*
	 * Duplicate the cursor so our btree manipulations here won't
	 * disrupt the next level up.
	 */
	if ((error = xfs_btree_dup_cursor(cur, &tcur)))
		return error;
	/*
	 * If there's a right sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (rbno != NULLAGBLOCK) {
		/*
		 * Move the temp cursor to the last entry in the next block.
		 * Actually any entry but the first would suffice.
		 */
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_increment(tcur, level, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Grab a pointer to the block.
		 */
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
#ifdef DEBUG
		if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
			goto error0;
#endif
		/*
		 * Grab the current block number, for future use.
		 */
		bno = be32_to_cpu(right->bb_leftsib);
		/*
		 * If right block is full enough so that removing one entry
		 * won't make it too empty, and left-shifting an entry out
		 * of right to us works, we're done.
		 */
		if (be16_to_cpu(right->bb_numrecs) - 1 >=
		     XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
			if ((error = xfs_btree_lshift(tcur, level, &i)))
				goto error0;
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_ALLOC_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level > 0 &&
				    (error = xfs_btree_decrement(cur, level,
					    &i)))
					return error;
				*stat = 1;
				return 0;
			}
		}
		/*
		 * Otherwise, grab the number of records in right for
		 * future reference, and fix up the temp cursor to point
		 * to our block again (last record).
		 */
		rrecs = be16_to_cpu(right->bb_numrecs);
		if (lbno != NULLAGBLOCK) {
			i = xfs_btree_firstrec(tcur, level);
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			if ((error = xfs_btree_decrement(tcur, level, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		}
	}
	/*
	 * If there's a left sibling, see if it's ok to shift an entry
	 * out of it.
	 */
	if (lbno != NULLAGBLOCK) {
		/*
		 * Move the temp cursor to the first entry in the
		 * previous block.
		 */
		i = xfs_btree_firstrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if ((error = xfs_btree_decrement(tcur, level, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		xfs_btree_firstrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
#ifdef DEBUG
		if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
			goto error0;
#endif
		/*
		 * Grab the current block number, for future use.
		 */
		bno = be32_to_cpu(left->bb_rightsib);
		/*
		 * If left block is full enough so that removing one entry
		 * won't make it too empty, and right-shifting an entry out
		 * of left to us works, we're done.
		 */
		if (be16_to_cpu(left->bb_numrecs) - 1 >=
		     XFS_ALLOC_BLOCK_MINRECS(level, cur)) {
			if ((error = xfs_btree_rshift(tcur, level, &i)))
				goto error0;
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_ALLOC_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level == 0)
					cur->bc_ptrs[0]++;
				*stat = 1;
				return 0;
			}
		}
		/*
		 * Otherwise, grab the number of records in right for
		 * future reference.
		 */
		lrecs = be16_to_cpu(left->bb_numrecs);
	}
	/*
	 * Delete the temp cursor, we're done with it.
	 */
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	/*
	 * If here, we need to do a join to keep the tree balanced.
	 */
	ASSERT(bno != NULLAGBLOCK);
	/*
	 * See if we can join with the left neighbor block.
	 */
	if (lbno != NULLAGBLOCK &&
	    lrecs + numrecs <= XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "right" to be the starting block,
		 * "left" to be the left neighbor.
		 */
		rbno = bno;
		right = block;
		rrecs = be16_to_cpu(right->bb_numrecs);
		rbp = bp;
		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.a.agno, lbno, 0, &lbp,
				XFS_ALLOC_BTREE_REF)))
			return error;
		left = XFS_BUF_TO_ALLOC_BLOCK(lbp);
		lrecs = be16_to_cpu(left->bb_numrecs);
		if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
			return error;
	}
	/*
	 * If that won't work, see if we can join with the right neighbor block.
	 */
	else if (rbno != NULLAGBLOCK &&
		 rrecs + numrecs <= XFS_ALLOC_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "left" to be the starting block,
		 * "right" to be the right neighbor.
		 */
		lbno = bno;
		left = block;
		lrecs = be16_to_cpu(left->bb_numrecs);
		lbp = bp;
		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.a.agno, rbno, 0, &rbp,
				XFS_ALLOC_BTREE_REF)))
			return error;
		right = XFS_BUF_TO_ALLOC_BLOCK(rbp);
		rrecs = be16_to_cpu(right->bb_numrecs);
		if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
			return error;
	}
	/*
	 * Otherwise, we can't fix the imbalance.
	 * Just return.  This is probably a logic error, but it's not fatal.
	 */
	else {
		if (level > 0 && (error = xfs_btree_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * We're now going to join "left" and "right" by moving all the stuff
	 * in "right" to "left" and deleting "right".
	 */
	if (level > 0) {
		/*
		 * It's a non-leaf.  Move keys and pointers.
		 */
		lkp = XFS_ALLOC_KEY_ADDR(left, lrecs + 1, cur);
		lpp = XFS_ALLOC_PTR_ADDR(left, lrecs + 1, cur);
		rkp = XFS_ALLOC_KEY_ADDR(right, 1, cur);
		rpp = XFS_ALLOC_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < rrecs; i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(rpp[i]), level)))
				return error;
		}
#endif
		memcpy(lkp, rkp, rrecs * sizeof(*lkp));
		memcpy(lpp, rpp, rrecs * sizeof(*lpp));
		xfs_alloc_log_keys(cur, lbp, lrecs + 1, lrecs + rrecs);
		xfs_alloc_log_ptrs(cur, lbp, lrecs + 1, lrecs + rrecs);
	} else {
		/*
		 * It's a leaf.  Move records.
		 */
		lrp = XFS_ALLOC_REC_ADDR(left, lrecs + 1, cur);
		rrp = XFS_ALLOC_REC_ADDR(right, 1, cur);
		memcpy(lrp, rrp, rrecs * sizeof(*lrp));
		xfs_alloc_log_recs(cur, lbp, lrecs + 1, lrecs + rrecs);
	}
	/*
	 * If we joined with the left neighbor, set the buffer in the
	 * cursor to the left block, and fix up the index.
	 */
	if (bp != lbp) {
		xfs_btree_setbuf(cur, level, lbp);
		cur->bc_ptrs[level] += lrecs;
	}
	/*
	 * If we joined with the right neighbor and there's a level above
	 * us, increment the cursor at that level.
	 */
	else if (level + 1 < cur->bc_nlevels &&
		 (error = xfs_btree_increment(cur, level + 1, &i)))
		return error;
	/*
	 * Fix up the number of records in the surviving block.
	 */
	lrecs += rrecs;
	left->bb_numrecs = cpu_to_be16(lrecs);
	/*
	 * Fix up the right block pointer in the surviving block, and log it.
	 */
	left->bb_rightsib = right->bb_rightsib;
	xfs_alloc_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there is a right sibling now, make it point to the
	 * remaining block.
	 */
	if (be32_to_cpu(left->bb_rightsib) != NULLAGBLOCK) {
		xfs_alloc_block_t	*rrblock;
		xfs_buf_t		*rrbp;

		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.a.agno, be32_to_cpu(left->bb_rightsib), 0,
				&rrbp, XFS_ALLOC_BTREE_REF)))
			return error;
		rrblock = XFS_BUF_TO_ALLOC_BLOCK(rrbp);
		if ((error = xfs_btree_check_sblock(cur, rrblock, level, rrbp)))
			return error;
		rrblock->bb_leftsib = cpu_to_be32(lbno);
		xfs_alloc_log_block(cur->bc_tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * Free the deleting block by putting it on the freelist.
	 */
	error = xfs_alloc_put_freelist(cur->bc_tp,
					 cur->bc_private.a.agbp, NULL, rbno, 1);
	if (error)
		return error;
	/*
	 * Since blocks move to the free list without the coordination
	 * used in xfs_bmap_finish, we can't allow block to be available
	 * for reallocation and non-transaction writing (user data)
	 * until we know that the transaction that moved it to the free
	 * list is permanently on disk. We track the blocks by declaring
	 * these blocks as "busy"; the busy list is maintained on a
	 * per-ag basis and each transaction records which entries
	 * should be removed when the iclog commits to disk. If a
	 * busy block is allocated, the iclog is pushed up to the
	 * LSN that freed the block.
	 */
	xfs_alloc_mark_busy(cur->bc_tp, be32_to_cpu(agf->agf_seqno), bno, 1);
	xfs_trans_agbtree_delta(cur->bc_tp, -1);

	/*
	 * Adjust the current level's cursor so that we're left referring
	 * to the right node, after we're done.
	 * If this leaves the ptr value 0 our caller will fix it up.
	 */
	if (level > 0)
		cur->bc_ptrs[level]--;
	/*
	 * Return value means the next level up has something to do.
	 */
	*stat = 2;
	return 0;

error0:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_alloc_log_block(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			fields)	/* mask of fields: XFS_BB_... */
{
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	static const short	offsets[] = {	/* table of offsets */
		offsetof(xfs_alloc_block_t, bb_magic),
		offsetof(xfs_alloc_block_t, bb_level),
		offsetof(xfs_alloc_block_t, bb_numrecs),
		offsetof(xfs_alloc_block_t, bb_leftsib),
		offsetof(xfs_alloc_block_t, bb_rightsib),
		sizeof(xfs_alloc_block_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, first, last);
}

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_keys(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			kfirst,	/* index of first key to log */
	int			klast)	/* index of last key to log */
{
	xfs_alloc_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	xfs_alloc_key_t		*kp;	/* key pointer in btree block */
	int			last;	/* last byte offset logged */

	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	kp = XFS_ALLOC_KEY_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&kp[kfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&kp[klast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_alloc_log_ptrs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			pfirst,	/* index of first pointer to log */
	int			plast)	/* index of last pointer to log */
{
	xfs_alloc_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_alloc_ptr_t		*pp;	/* block-pointer pointer in btree blk */

	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	pp = XFS_ALLOC_PTR_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&pp[pfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&pp[plast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_alloc_log_recs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			rfirst,	/* index of first record to log */
	int			rlast)	/* index of last record to log */
{
	xfs_alloc_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_alloc_rec_t		*rp;	/* record pointer for btree block */


	block = XFS_BUF_TO_ALLOC_BLOCK(bp);
	rp = XFS_ALLOC_REC_ADDR(block, 1, cur);
#ifdef DEBUG
	{
		xfs_agf_t	*agf;
		xfs_alloc_rec_t	*p;

		agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);
		for (p = &rp[rfirst - 1]; p <= &rp[rlast - 1]; p++)
			ASSERT(be32_to_cpu(p->ar_startblock) +
			       be32_to_cpu(p->ar_blockcount) <=
			       be32_to_cpu(agf->agf_length));
	}
#endif
	first = (int)((xfs_caddr_t)&rp[rfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&rp[rlast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}


/*
 * Externally visible routines.
 */

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* error */
xfs_alloc_delete(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
	int		i;		/* result code */
	int		level;		/* btree level */

	/*
	 * Go up the tree, starting at leaf level.
	 * If 2 is returned then a join was done; go to the next level.
	 * Otherwise we are done.
	 */
	for (level = 0, i = 2; i == 2; level++) {
		if ((error = xfs_alloc_delrec(cur, level, &i)))
			return error;
	}
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				if ((error = xfs_btree_decrement(cur, level, &i)))
					return error;
				break;
			}
		}
	}
	*stat = i;
	return 0;
}

/*
 * Get the data from the pointed-to record.
 */
int					/* error */
xfs_alloc_get_rec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agblock_t		*bno,	/* output: starting block of extent */
	xfs_extlen_t		*len,	/* output: length of extent */
	int			*stat)	/* output: success/failure */
{
	xfs_alloc_block_t	*block;	/* btree block */
#ifdef DEBUG
	int			error;	/* error return value */
#endif
	int			ptr;	/* record number */

	ptr = cur->bc_ptrs[0];
	block = XFS_BUF_TO_ALLOC_BLOCK(cur->bc_bufs[0]);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, 0, cur->bc_bufs[0])))
		return error;
#endif
	/*
	 * Off the right end or left end, return failure.
	 */
	if (ptr > be16_to_cpu(block->bb_numrecs) || ptr <= 0) {
		*stat = 0;
		return 0;
	}
	/*
	 * Point to the record and extract its data.
	 */
	{
		xfs_alloc_rec_t		*rec;	/* record data */

		rec = XFS_ALLOC_REC_ADDR(block, ptr, cur);
		*bno = be32_to_cpu(rec->ar_startblock);
		*len = be32_to_cpu(rec->ar_blockcount);
	}
	*stat = 1;
	return 0;
}


STATIC struct xfs_btree_cur *
xfs_allocbt_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	return xfs_allocbt_init_cursor(cur->bc_mp, cur->bc_tp,
			cur->bc_private.a.agbp, cur->bc_private.a.agno,
			cur->bc_btnum);
}

STATIC void
xfs_allocbt_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			inc)
{
	struct xfs_buf		*agbp = cur->bc_private.a.agbp;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	xfs_agnumber_t		seqno = be32_to_cpu(agf->agf_seqno);
	int			btnum = cur->bc_btnum;

	ASSERT(ptr->s != 0);

	agf->agf_roots[btnum] = ptr->s;
	be32_add_cpu(&agf->agf_levels[btnum], inc);
	cur->bc_mp->m_perag[seqno].pagf_levels[btnum] += inc;

	xfs_alloc_log_agf(cur->bc_tp, agbp, XFS_AGF_ROOTS | XFS_AGF_LEVELS);
}

STATIC int
xfs_allocbt_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start,
	union xfs_btree_ptr	*new,
	int			length,
	int			*stat)
{
	int			error;
	xfs_agblock_t		bno;

	XFS_BTREE_TRACE_CURSOR(cur, XBT_ENTRY);

	/* Allocate the new block from the freelist. If we can't, give up.  */
	error = xfs_alloc_get_freelist(cur->bc_tp, cur->bc_private.a.agbp,
				       &bno, 1);
	if (error) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_ERROR);
		return error;
	}

	if (bno == NULLAGBLOCK) {
		XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
		*stat = 0;
		return 0;
	}

	xfs_trans_agbtree_delta(cur->bc_tp, 1);
	new->s = cpu_to_be32(bno);

	XFS_BTREE_TRACE_CURSOR(cur, XBT_EXIT);
	*stat = 1;
	return 0;
}

/*
 * Update the longest extent in the AGF
 */
STATIC void
xfs_allocbt_update_lastrec(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_block	*block,
	union xfs_btree_rec	*rec,
	int			ptr,
	int			reason)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);
	xfs_agnumber_t		seqno = be32_to_cpu(agf->agf_seqno);
	__be32			len;

	ASSERT(cur->bc_btnum == XFS_BTNUM_CNT);

	switch (reason) {
	case LASTREC_UPDATE:
		/*
		 * If this is the last leaf block and it's the last record,
		 * then update the size of the longest extent in the AG.
		 */
		if (ptr != xfs_btree_get_numrecs(block))
			return;
		len = rec->alloc.ar_blockcount;
		break;
	case LASTREC_INSREC:
		if (be32_to_cpu(rec->alloc.ar_blockcount) <=
		    be32_to_cpu(agf->agf_longest))
			return;
		len = rec->alloc.ar_blockcount;
		break;
	default:
		ASSERT(0);
		return;
	}

	agf->agf_longest = len;
	cur->bc_mp->m_perag[seqno].pagf_longest = be32_to_cpu(len);
	xfs_alloc_log_agf(cur->bc_tp, cur->bc_private.a.agbp, XFS_AGF_LONGEST);
}

STATIC int
xfs_allocbt_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	return cur->bc_mp->m_alloc_mxr[level != 0];
}

STATIC void
xfs_allocbt_init_key_from_rec(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	ASSERT(rec->alloc.ar_startblock != 0);

	key->alloc.ar_startblock = rec->alloc.ar_startblock;
	key->alloc.ar_blockcount = rec->alloc.ar_blockcount;
}

STATIC void
xfs_allocbt_init_rec_from_key(
	union xfs_btree_key	*key,
	union xfs_btree_rec	*rec)
{
	ASSERT(key->alloc.ar_startblock != 0);

	rec->alloc.ar_startblock = key->alloc.ar_startblock;
	rec->alloc.ar_blockcount = key->alloc.ar_blockcount;
}

STATIC void
xfs_allocbt_init_rec_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec)
{
	ASSERT(cur->bc_rec.a.ar_startblock != 0);

	rec->alloc.ar_startblock = cpu_to_be32(cur->bc_rec.a.ar_startblock);
	rec->alloc.ar_blockcount = cpu_to_be32(cur->bc_rec.a.ar_blockcount);
}

STATIC void
xfs_allocbt_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(cur->bc_private.a.agbp);

	ASSERT(cur->bc_private.a.agno == be32_to_cpu(agf->agf_seqno));
	ASSERT(agf->agf_roots[cur->bc_btnum] != 0);

	ptr->s = agf->agf_roots[cur->bc_btnum];
}

STATIC __int64_t
xfs_allocbt_key_diff(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key)
{
	xfs_alloc_rec_incore_t	*rec = &cur->bc_rec.a;
	xfs_alloc_key_t		*kp = &key->alloc;
	__int64_t		diff;

	if (cur->bc_btnum == XFS_BTNUM_BNO) {
		return (__int64_t)be32_to_cpu(kp->ar_startblock) -
				rec->ar_startblock;
	}

	diff = (__int64_t)be32_to_cpu(kp->ar_blockcount) - rec->ar_blockcount;
	if (diff)
		return diff;

	return (__int64_t)be32_to_cpu(kp->ar_startblock) - rec->ar_startblock;
}

#ifdef XFS_BTREE_TRACE
ktrace_t	*xfs_allocbt_trace_buf;

STATIC void
xfs_allocbt_trace_enter(
	struct xfs_btree_cur	*cur,
	const char		*func,
	char			*s,
	int			type,
	int			line,
	__psunsigned_t		a0,
	__psunsigned_t		a1,
	__psunsigned_t		a2,
	__psunsigned_t		a3,
	__psunsigned_t		a4,
	__psunsigned_t		a5,
	__psunsigned_t		a6,
	__psunsigned_t		a7,
	__psunsigned_t		a8,
	__psunsigned_t		a9,
	__psunsigned_t		a10)
{
	ktrace_enter(xfs_allocbt_trace_buf, (void *)(__psint_t)type,
		(void *)func, (void *)s, NULL, (void *)cur,
		(void *)a0, (void *)a1, (void *)a2, (void *)a3,
		(void *)a4, (void *)a5, (void *)a6, (void *)a7,
		(void *)a8, (void *)a9, (void *)a10);
}

STATIC void
xfs_allocbt_trace_cursor(
	struct xfs_btree_cur	*cur,
	__uint32_t		*s0,
	__uint64_t		*l0,
	__uint64_t		*l1)
{
	*s0 = cur->bc_private.a.agno;
	*l0 = cur->bc_rec.a.ar_startblock;
	*l1 = cur->bc_rec.a.ar_blockcount;
}

STATIC void
xfs_allocbt_trace_key(
	struct xfs_btree_cur	*cur,
	union xfs_btree_key	*key,
	__uint64_t		*l0,
	__uint64_t		*l1)
{
	*l0 = be32_to_cpu(key->alloc.ar_startblock);
	*l1 = be32_to_cpu(key->alloc.ar_blockcount);
}

STATIC void
xfs_allocbt_trace_record(
	struct xfs_btree_cur	*cur,
	union xfs_btree_rec	*rec,
	__uint64_t		*l0,
	__uint64_t		*l1,
	__uint64_t		*l2)
{
	*l0 = be32_to_cpu(rec->alloc.ar_startblock);
	*l1 = be32_to_cpu(rec->alloc.ar_blockcount);
	*l2 = 0;
}
#endif /* XFS_BTREE_TRACE */

static const struct xfs_btree_ops xfs_allocbt_ops = {
	.rec_len		= sizeof(xfs_alloc_rec_t),
	.key_len		= sizeof(xfs_alloc_key_t),

	.dup_cursor		= xfs_allocbt_dup_cursor,
	.set_root		= xfs_allocbt_set_root,
	.alloc_block		= xfs_allocbt_alloc_block,
	.update_lastrec		= xfs_allocbt_update_lastrec,
	.get_maxrecs		= xfs_allocbt_get_maxrecs,
	.init_key_from_rec	= xfs_allocbt_init_key_from_rec,
	.init_rec_from_key	= xfs_allocbt_init_rec_from_key,
	.init_rec_from_cur	= xfs_allocbt_init_rec_from_cur,
	.init_ptr_from_cur	= xfs_allocbt_init_ptr_from_cur,
	.key_diff		= xfs_allocbt_key_diff,

#ifdef XFS_BTREE_TRACE
	.trace_enter		= xfs_allocbt_trace_enter,
	.trace_cursor		= xfs_allocbt_trace_cursor,
	.trace_key		= xfs_allocbt_trace_key,
	.trace_record		= xfs_allocbt_trace_record,
#endif
};

/*
 * Allocate a new allocation btree cursor.
 */
struct xfs_btree_cur *			/* new alloc btree cursor */
xfs_allocbt_init_cursor(
	struct xfs_mount	*mp,		/* file system mount point */
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_buf		*agbp,		/* buffer for agf structure */
	xfs_agnumber_t		agno,		/* allocation group number */
	xfs_btnum_t		btnum)		/* btree identifier */
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agbp);
	struct xfs_btree_cur	*cur;

	ASSERT(btnum == XFS_BTNUM_BNO || btnum == XFS_BTNUM_CNT);

	cur = kmem_zone_zalloc(xfs_btree_cur_zone, KM_SLEEP);

	cur->bc_tp = tp;
	cur->bc_mp = mp;
	cur->bc_nlevels = be32_to_cpu(agf->agf_levels[btnum]);
	cur->bc_btnum = btnum;
	cur->bc_blocklog = mp->m_sb.sb_blocklog;

	cur->bc_ops = &xfs_allocbt_ops;
	if (btnum == XFS_BTNUM_CNT)
		cur->bc_flags = XFS_BTREE_LASTREC_UPDATE;

	cur->bc_private.a.agbp = agbp;
	cur->bc_private.a.agno = agno;

	return cur;
}
