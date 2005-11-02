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
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_error.h"

STATIC void xfs_inobt_log_block(xfs_trans_t *, xfs_buf_t *, int);
STATIC void xfs_inobt_log_keys(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_inobt_log_ptrs(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC void xfs_inobt_log_recs(xfs_btree_cur_t *, xfs_buf_t *, int, int);
STATIC int xfs_inobt_lshift(xfs_btree_cur_t *, int, int *);
STATIC int xfs_inobt_newroot(xfs_btree_cur_t *, int *);
STATIC int xfs_inobt_rshift(xfs_btree_cur_t *, int, int *);
STATIC int xfs_inobt_split(xfs_btree_cur_t *, int, xfs_agblock_t *,
		xfs_inobt_key_t *, xfs_btree_cur_t **, int *);
STATIC int xfs_inobt_updkey(xfs_btree_cur_t *, xfs_inobt_key_t *, int);

/*
 * Single level of the xfs_inobt_delete record deletion routine.
 * Delete record pointed to by cur/level.
 * Remove the record from its block then rebalance the tree.
 * Return 0 for error, 1 for done, 2 to go on to the next level.
 */
STATIC int				/* error */
xfs_inobt_delrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level removing record from */
	int			*stat)	/* fail/done/go-on */
{
	xfs_buf_t		*agbp;	/* buffer for a.g. inode header */
	xfs_mount_t		*mp;	/* mount structure */
	xfs_agi_t		*agi;	/* allocation group inode header */
	xfs_inobt_block_t	*block;	/* btree block record/key lives in */
	xfs_agblock_t		bno;	/* btree block number */
	xfs_buf_t		*bp;	/* buffer for block */
	int			error;	/* error return value */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* kp points here if block is level 0 */
	xfs_inobt_key_t		*kp = NULL;	/* pointer to btree keys */
	xfs_agblock_t		lbno;	/* left block's block number */
	xfs_buf_t		*lbp;	/* left block's buffer pointer */
	xfs_inobt_block_t	*left;	/* left btree block */
	xfs_inobt_key_t		*lkp;	/* left block key pointer */
	xfs_inobt_ptr_t		*lpp;	/* left block address pointer */
	int			lrecs = 0;	/* number of records in left block */
	xfs_inobt_rec_t		*lrp;	/* left block record pointer */
	xfs_inobt_ptr_t		*pp = NULL;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_agblock_t		rbno;	/* right block's block number */
	xfs_buf_t		*rbp;	/* right block's buffer pointer */
	xfs_inobt_block_t	*right;	/* right btree block */
	xfs_inobt_key_t		*rkp;	/* right block key pointer */
	xfs_inobt_rec_t		*rp;	/* pointer to btree records */
	xfs_inobt_ptr_t		*rpp;	/* right block address pointer */
	int			rrecs = 0;	/* number of records in right block */
	int			numrecs;
	xfs_inobt_rec_t		*rrp;	/* right block record pointer */
	xfs_btree_cur_t		*tcur;	/* temporary btree cursor */

	mp = cur->bc_mp;

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
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
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
	/*
	 * It's a nonleaf.  Excise the key and ptr being deleted, by
	 * sliding the entries past them down one.
	 * Log the changed areas of the block.
	 */
	if (level > 0) {
		kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
		pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
#ifdef DEBUG
		for (i = ptr; i < numrecs; i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(pp[i]), level)))
				return error;
		}
#endif
		if (ptr < numrecs) {
			memmove(&kp[ptr - 1], &kp[ptr],
				(numrecs - ptr) * sizeof(*kp));
			memmove(&pp[ptr - 1], &pp[ptr],
				(numrecs - ptr) * sizeof(*kp));
			xfs_inobt_log_keys(cur, bp, ptr, numrecs - 1);
			xfs_inobt_log_ptrs(cur, bp, ptr, numrecs - 1);
		}
	}
	/*
	 * It's a leaf.  Excise the record being deleted, by sliding the
	 * entries past it down one.  Log the changed areas of the block.
	 */
	else {
		rp = XFS_INOBT_REC_ADDR(block, 1, cur);
		if (ptr < numrecs) {
			memmove(&rp[ptr - 1], &rp[ptr],
				(numrecs - ptr) * sizeof(*rp));
			xfs_inobt_log_recs(cur, bp, ptr, numrecs - 1);
		}
		/*
		 * If it's the first record in the block, we'll need a key
		 * structure to pass up to the next level (updkey).
		 */
		if (ptr == 1) {
			key.ir_startino = rp->ir_startino;
			kp = &key;
		}
	}
	/*
	 * Decrement and log the number of entries in the block.
	 */
	numrecs--;
	block->bb_numrecs = cpu_to_be16(numrecs);
	xfs_inobt_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
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
			agbp = cur->bc_private.i.agbp;
			agi = XFS_BUF_TO_AGI(agbp);
			/*
			 * pp is still set to the first pointer in the block.
			 * Make it the new root of the btree.
			 */
			bno = be32_to_cpu(agi->agi_root);
			agi->agi_root = *pp;
			be32_add(&agi->agi_level, -1);
			/*
			 * Free the block.
			 */
			if ((error = xfs_free_extent(cur->bc_tp,
				XFS_AGB_TO_FSB(mp, cur->bc_private.i.agno, bno), 1)))
				return error;
			xfs_trans_binval(cur->bc_tp, bp);
			xfs_ialloc_log_agi(cur->bc_tp, agbp,
				XFS_AGI_ROOT | XFS_AGI_LEVEL);
			/*
			 * Update the cursor so there's one fewer level.
			 */
			cur->bc_bufs[level] = NULL;
			cur->bc_nlevels--;
		} else if (level > 0 &&
			   (error = xfs_inobt_decrement(cur, level, &i)))
			return error;
		*stat = 1;
		return 0;
	}
	/*
	 * If we deleted the leftmost entry in the block, update the
	 * key values above us in the tree.
	 */
	if (ptr == 1 && (error = xfs_inobt_updkey(cur, kp, level + 1)))
		return error;
	/*
	 * If the number of records remaining in the block is at least
	 * the minimum, we're done.
	 */
	if (numrecs >= XFS_INOBT_BLOCK_MINRECS(level, cur)) {
		if (level > 0 &&
		    (error = xfs_inobt_decrement(cur, level, &i)))
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
		if ((error = xfs_inobt_increment(tcur, level, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		i = xfs_btree_lastrec(tcur, level);
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		/*
		 * Grab a pointer to the block.
		 */
		rbp = tcur->bc_bufs[level];
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
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
		     XFS_INOBT_BLOCK_MINRECS(level, cur)) {
			if ((error = xfs_inobt_lshift(tcur, level, &i)))
				goto error0;
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_INOBT_BLOCK_MINRECS(level, cur));
				xfs_btree_del_cursor(tcur,
						     XFS_BTREE_NOERROR);
				if (level > 0 &&
				    (error = xfs_inobt_decrement(cur, level,
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
			xfs_btree_firstrec(tcur, level);
			if ((error = xfs_inobt_decrement(tcur, level, &i)))
				goto error0;
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
		xfs_btree_firstrec(tcur, level);
		if ((error = xfs_inobt_decrement(tcur, level, &i)))
			goto error0;
		xfs_btree_firstrec(tcur, level);
		/*
		 * Grab a pointer to the block.
		 */
		lbp = tcur->bc_bufs[level];
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
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
		     XFS_INOBT_BLOCK_MINRECS(level, cur)) {
			if ((error = xfs_inobt_rshift(tcur, level, &i)))
				goto error0;
			if (i) {
				ASSERT(be16_to_cpu(block->bb_numrecs) >=
				       XFS_INOBT_BLOCK_MINRECS(level, cur));
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
	    lrecs + numrecs <= XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "right" to be the starting block,
		 * "left" to be the left neighbor.
		 */
		rbno = bno;
		right = block;
		rrecs = be16_to_cpu(right->bb_numrecs);
		rbp = bp;
		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.i.agno, lbno, 0, &lbp,
				XFS_INO_BTREE_REF)))
			return error;
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
		lrecs = be16_to_cpu(left->bb_numrecs);
		if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
			return error;
	}
	/*
	 * If that won't work, see if we can join with the right neighbor block.
	 */
	else if (rbno != NULLAGBLOCK &&
		 rrecs + numrecs <= XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * Set "left" to be the starting block,
		 * "right" to be the right neighbor.
		 */
		lbno = bno;
		left = block;
		lrecs = be16_to_cpu(left->bb_numrecs);
		lbp = bp;
		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.i.agno, rbno, 0, &rbp,
				XFS_INO_BTREE_REF)))
			return error;
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
		rrecs = be16_to_cpu(right->bb_numrecs);
		if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
			return error;
	}
	/*
	 * Otherwise, we can't fix the imbalance.
	 * Just return.  This is probably a logic error, but it's not fatal.
	 */
	else {
		if (level > 0 && (error = xfs_inobt_decrement(cur, level, &i)))
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
		lkp = XFS_INOBT_KEY_ADDR(left, lrecs + 1, cur);
		lpp = XFS_INOBT_PTR_ADDR(left, lrecs + 1, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < rrecs; i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(rpp[i]), level)))
				return error;
		}
#endif
		memcpy(lkp, rkp, rrecs * sizeof(*lkp));
		memcpy(lpp, rpp, rrecs * sizeof(*lpp));
		xfs_inobt_log_keys(cur, lbp, lrecs + 1, lrecs + rrecs);
		xfs_inobt_log_ptrs(cur, lbp, lrecs + 1, lrecs + rrecs);
	} else {
		/*
		 * It's a leaf.  Move records.
		 */
		lrp = XFS_INOBT_REC_ADDR(left, lrecs + 1, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		memcpy(lrp, rrp, rrecs * sizeof(*lrp));
		xfs_inobt_log_recs(cur, lbp, lrecs + 1, lrecs + rrecs);
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
		 (error = xfs_alloc_increment(cur, level + 1, &i)))
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
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there is a right sibling now, make it point to the
	 * remaining block.
	 */
	if (be32_to_cpu(left->bb_rightsib) != NULLAGBLOCK) {
		xfs_inobt_block_t	*rrblock;
		xfs_buf_t		*rrbp;

		if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
				cur->bc_private.i.agno, be32_to_cpu(left->bb_rightsib), 0,
				&rrbp, XFS_INO_BTREE_REF)))
			return error;
		rrblock = XFS_BUF_TO_INOBT_BLOCK(rrbp);
		if ((error = xfs_btree_check_sblock(cur, rrblock, level, rrbp)))
			return error;
		rrblock->bb_leftsib = cpu_to_be32(lbno);
		xfs_inobt_log_block(cur->bc_tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * Free the deleting block.
	 */
	if ((error = xfs_free_extent(cur->bc_tp, XFS_AGB_TO_FSB(mp,
				     cur->bc_private.i.agno, rbno), 1)))
		return error;
	xfs_trans_binval(cur->bc_tp, rbp);
	/*
	 * Readjust the ptr at this level if it's not a leaf, since it's
	 * still pointing at the deletion point, which makes the cursor
	 * inconsistent.  If this makes the ptr 0, the caller fixes it up.
	 * We can't use decrement because it would change the next level up.
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
 * Insert one record/level.  Return information to the caller
 * allowing the next level up to proceed if necessary.
 */
STATIC int				/* error */
xfs_inobt_insrec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to insert record at */
	xfs_agblock_t		*bnop,	/* i/o: block number inserted */
	xfs_inobt_rec_t		*recp,	/* i/o: record data inserted */
	xfs_btree_cur_t		**curp,	/* output: new cursor replacing cur */
	int			*stat)	/* success/failure */
{
	xfs_inobt_block_t	*block;	/* btree block record/key lives in */
	xfs_buf_t		*bp;	/* buffer for block */
	int			error;	/* error return value */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* key value being inserted */
	xfs_inobt_key_t		*kp=NULL;	/* pointer to btree keys */
	xfs_agblock_t		nbno;	/* block number of allocated block */
	xfs_btree_cur_t		*ncur;	/* new cursor to be used at next lvl */
	xfs_inobt_key_t		nkey;	/* new key value, from split */
	xfs_inobt_rec_t		nrec;	/* new record value, for caller */
	int			numrecs;
	int			optr;	/* old ptr value */
	xfs_inobt_ptr_t		*pp;	/* pointer to btree addresses */
	int			ptr;	/* index in btree block for this rec */
	xfs_inobt_rec_t		*rp=NULL;	/* pointer to btree records */

	/*
	 * GCC doesn't understand the (arguably complex) control flow in
	 * this function and complains about uninitialized structure fields
	 * without this.
	 */
	memset(&nrec, 0, sizeof(nrec));

	/*
	 * If we made it to the root level, allocate a new root block
	 * and we're done.
	 */
	if (level >= cur->bc_nlevels) {
		error = xfs_inobt_newroot(cur, &i);
		*bnop = NULLAGBLOCK;
		*stat = i;
		return error;
	}
	/*
	 * Make a key out of the record data to be inserted, and save it.
	 */
	key.ir_startino = recp->ir_startino; /* INT_: direct copy */
	optr = ptr = cur->bc_ptrs[level];
	/*
	 * If we're off the left edge, return failure.
	 */
	if (ptr == 0) {
		*stat = 0;
		return 0;
	}
	/*
	 * Get pointers to the btree buffer and block.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	numrecs = be16_to_cpu(block->bb_numrecs);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, level, bp)))
		return error;
	/*
	 * Check that the new entry is being inserted in the right place.
	 */
	if (ptr <= numrecs) {
		if (level == 0) {
			rp = XFS_INOBT_REC_ADDR(block, ptr, cur);
			xfs_btree_check_rec(cur->bc_btnum, recp, rp);
		} else {
			kp = XFS_INOBT_KEY_ADDR(block, ptr, cur);
			xfs_btree_check_key(cur->bc_btnum, &key, kp);
		}
	}
#endif
	nbno = NULLAGBLOCK;
	ncur = (xfs_btree_cur_t *)0;
	/*
	 * If the block is full, we can't insert the new entry until we
	 * make the block un-full.
	 */
	if (numrecs == XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		/*
		 * First, try shifting an entry to the right neighbor.
		 */
		if ((error = xfs_inobt_rshift(cur, level, &i)))
			return error;
		if (i) {
			/* nothing */
		}
		/*
		 * Next, try shifting an entry to the left neighbor.
		 */
		else {
			if ((error = xfs_inobt_lshift(cur, level, &i)))
				return error;
			if (i) {
				optr = ptr = cur->bc_ptrs[level];
			} else {
				/*
				 * Next, try splitting the current block
				 * in half. If this works we have to
				 * re-set our variables because
				 * we could be in a different block now.
				 */
				if ((error = xfs_inobt_split(cur, level, &nbno,
						&nkey, &ncur, &i)))
					return error;
				if (i) {
					bp = cur->bc_bufs[level];
					block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
					if ((error = xfs_btree_check_sblock(cur,
							block, level, bp)))
						return error;
#endif
					ptr = cur->bc_ptrs[level];
					nrec.ir_startino = nkey.ir_startino; /* INT_: direct copy */
				} else {
					/*
					 * Otherwise the insert fails.
					 */
					*stat = 0;
					return 0;
				}
			}
		}
	}
	/*
	 * At this point we know there's room for our new entry in the block
	 * we're pointing at.
	 */
	numrecs = be16_to_cpu(block->bb_numrecs);
	if (level > 0) {
		/*
		 * It's a non-leaf entry.  Make a hole for the new data
		 * in the key and ptr regions of the block.
		 */
		kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
		pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
#ifdef DEBUG
		for (i = numrecs; i >= ptr; i--) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(pp[i - 1]), level)))
				return error;
		}
#endif
		memmove(&kp[ptr], &kp[ptr - 1],
			(numrecs - ptr + 1) * sizeof(*kp));
		memmove(&pp[ptr], &pp[ptr - 1],
			(numrecs - ptr + 1) * sizeof(*pp));
		/*
		 * Now stuff the new data in, bump numrecs and log the new data.
		 */
#ifdef DEBUG
		if ((error = xfs_btree_check_sptr(cur, *bnop, level)))
			return error;
#endif
		kp[ptr - 1] = key; /* INT_: struct copy */
		pp[ptr - 1] = cpu_to_be32(*bnop);
		numrecs++;
		block->bb_numrecs = cpu_to_be16(numrecs);
		xfs_inobt_log_keys(cur, bp, ptr, numrecs);
		xfs_inobt_log_ptrs(cur, bp, ptr, numrecs);
	} else {
		/*
		 * It's a leaf entry.  Make a hole for the new record.
		 */
		rp = XFS_INOBT_REC_ADDR(block, 1, cur);
		memmove(&rp[ptr], &rp[ptr - 1],
			(numrecs - ptr + 1) * sizeof(*rp));
		/*
		 * Now stuff the new record in, bump numrecs
		 * and log the new data.
		 */
		rp[ptr - 1] = *recp; /* INT_: struct copy */
		numrecs++;
		block->bb_numrecs = cpu_to_be16(numrecs);
		xfs_inobt_log_recs(cur, bp, ptr, numrecs);
	}
	/*
	 * Log the new number of records in the btree header.
	 */
	xfs_inobt_log_block(cur->bc_tp, bp, XFS_BB_NUMRECS);
#ifdef DEBUG
	/*
	 * Check that the key/record is in the right place, now.
	 */
	if (ptr < numrecs) {
		if (level == 0)
			xfs_btree_check_rec(cur->bc_btnum, rp + ptr - 1,
				rp + ptr);
		else
			xfs_btree_check_key(cur->bc_btnum, kp + ptr - 1,
				kp + ptr);
	}
#endif
	/*
	 * If we inserted at the start of a block, update the parents' keys.
	 */
	if (optr == 1 && (error = xfs_inobt_updkey(cur, &key, level + 1)))
		return error;
	/*
	 * Return the new block number, if any.
	 * If there is one, give back a record value and a cursor too.
	 */
	*bnop = nbno;
	if (nbno != NULLAGBLOCK) {
		*recp = nrec; /* INT_: struct copy */
		*curp = ncur;
	}
	*stat = 1;
	return 0;
}

/*
 * Log header fields from a btree block.
 */
STATIC void
xfs_inobt_log_block(
	xfs_trans_t		*tp,	/* transaction pointer */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			fields)	/* mask of fields: XFS_BB_... */
{
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	static const short	offsets[] = {	/* table of offsets */
		offsetof(xfs_inobt_block_t, bb_magic),
		offsetof(xfs_inobt_block_t, bb_level),
		offsetof(xfs_inobt_block_t, bb_numrecs),
		offsetof(xfs_inobt_block_t, bb_leftsib),
		offsetof(xfs_inobt_block_t, bb_rightsib),
		sizeof(xfs_inobt_block_t)
	};

	xfs_btree_offsets(fields, offsets, XFS_BB_NUM_BITS, &first, &last);
	xfs_trans_log_buf(tp, bp, first, last);
}

/*
 * Log keys from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_keys(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			kfirst,	/* index of first key to log */
	int			klast)	/* index of last key to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	xfs_inobt_key_t		*kp;	/* key pointer in btree block */
	int			last;	/* last byte offset logged */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	kp = XFS_INOBT_KEY_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&kp[kfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&kp[klast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Log block pointer fields from a btree block (nonleaf).
 */
STATIC void
xfs_inobt_log_ptrs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			pfirst,	/* index of first pointer to log */
	int			plast)	/* index of last pointer to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_inobt_ptr_t		*pp;	/* block-pointer pointer in btree blk */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	pp = XFS_INOBT_PTR_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&pp[pfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&pp[plast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Log records from a btree block (leaf).
 */
STATIC void
xfs_inobt_log_recs(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_buf_t		*bp,	/* buffer containing btree block */
	int			rfirst,	/* index of first record to log */
	int			rlast)	/* index of last record to log */
{
	xfs_inobt_block_t	*block;	/* btree block to log from */
	int			first;	/* first byte offset logged */
	int			last;	/* last byte offset logged */
	xfs_inobt_rec_t		*rp;	/* record pointer for btree block */

	block = XFS_BUF_TO_INOBT_BLOCK(bp);
	rp = XFS_INOBT_REC_ADDR(block, 1, cur);
	first = (int)((xfs_caddr_t)&rp[rfirst - 1] - (xfs_caddr_t)block);
	last = (int)(((xfs_caddr_t)&rp[rlast] - 1) - (xfs_caddr_t)block);
	xfs_trans_log_buf(cur->bc_tp, bp, first, last);
}

/*
 * Lookup the record.  The cursor is made to point to it, based on dir.
 * Return 0 if can't find any such record, 1 for success.
 */
STATIC int				/* error */
xfs_inobt_lookup(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_lookup_t		dir,	/* <=, ==, or >= */
	int			*stat)	/* success/failure */
{
	xfs_agblock_t		agbno;	/* a.g. relative btree block number */
	xfs_agnumber_t		agno;	/* allocation group number */
	xfs_inobt_block_t	*block=NULL;	/* current btree block */
	__int64_t		diff;	/* difference for the current key */
	int			error;	/* error return value */
	int			keyno=0;	/* current key number */
	int			level;	/* level in the btree */
	xfs_mount_t		*mp;	/* file system mount point */

	/*
	 * Get the allocation group header, and the root block number.
	 */
	mp = cur->bc_mp;
	{
		xfs_agi_t	*agi;	/* a.g. inode header */

		agi = XFS_BUF_TO_AGI(cur->bc_private.i.agbp);
		agno = be32_to_cpu(agi->agi_seqno);
		agbno = be32_to_cpu(agi->agi_root);
	}
	/*
	 * Iterate over each level in the btree, starting at the root.
	 * For each level above the leaves, find the key we need, based
	 * on the lookup record, then follow the corresponding block
	 * pointer down to the next level.
	 */
	for (level = cur->bc_nlevels - 1, diff = 1; level >= 0; level--) {
		xfs_buf_t	*bp;	/* buffer pointer for btree block */
		xfs_daddr_t	d;	/* disk address of btree block */

		/*
		 * Get the disk address we're looking for.
		 */
		d = XFS_AGB_TO_DADDR(mp, agno, agbno);
		/*
		 * If the old buffer at this level is for a different block,
		 * throw it away, otherwise just use it.
		 */
		bp = cur->bc_bufs[level];
		if (bp && XFS_BUF_ADDR(bp) != d)
			bp = (xfs_buf_t *)0;
		if (!bp) {
			/*
			 * Need to get a new buffer.  Read it, then
			 * set it in the cursor, releasing the old one.
			 */
			if ((error = xfs_btree_read_bufs(mp, cur->bc_tp,
					agno, agbno, 0, &bp, XFS_INO_BTREE_REF)))
				return error;
			xfs_btree_setbuf(cur, level, bp);
			/*
			 * Point to the btree block, now that we have the buffer
			 */
			block = XFS_BUF_TO_INOBT_BLOCK(bp);
			if ((error = xfs_btree_check_sblock(cur, block, level,
					bp)))
				return error;
		} else
			block = XFS_BUF_TO_INOBT_BLOCK(bp);
		/*
		 * If we already had a key match at a higher level, we know
		 * we need to use the first entry in this block.
		 */
		if (diff == 0)
			keyno = 1;
		/*
		 * Otherwise we need to search this block.  Do a binary search.
		 */
		else {
			int		high;	/* high entry number */
			xfs_inobt_key_t	*kkbase=NULL;/* base of keys in block */
			xfs_inobt_rec_t	*krbase=NULL;/* base of records in block */
			int		low;	/* low entry number */

			/*
			 * Get a pointer to keys or records.
			 */
			if (level > 0)
				kkbase = XFS_INOBT_KEY_ADDR(block, 1, cur);
			else
				krbase = XFS_INOBT_REC_ADDR(block, 1, cur);
			/*
			 * Set low and high entry numbers, 1-based.
			 */
			low = 1;
			if (!(high = be16_to_cpu(block->bb_numrecs))) {
				/*
				 * If the block is empty, the tree must
				 * be an empty leaf.
				 */
				ASSERT(level == 0 && cur->bc_nlevels == 1);
				cur->bc_ptrs[0] = dir != XFS_LOOKUP_LE;
				*stat = 0;
				return 0;
			}
			/*
			 * Binary search the block.
			 */
			while (low <= high) {
				xfs_agino_t	startino;	/* key value */

				/*
				 * keyno is average of low and high.
				 */
				keyno = (low + high) >> 1;
				/*
				 * Get startino.
				 */
				if (level > 0) {
					xfs_inobt_key_t	*kkp;

					kkp = kkbase + keyno - 1;
					startino = INT_GET(kkp->ir_startino, ARCH_CONVERT);
				} else {
					xfs_inobt_rec_t	*krp;

					krp = krbase + keyno - 1;
					startino = INT_GET(krp->ir_startino, ARCH_CONVERT);
				}
				/*
				 * Compute difference to get next direction.
				 */
				diff = (__int64_t)
					startino - cur->bc_rec.i.ir_startino;
				/*
				 * Less than, move right.
				 */
				if (diff < 0)
					low = keyno + 1;
				/*
				 * Greater than, move left.
				 */
				else if (diff > 0)
					high = keyno - 1;
				/*
				 * Equal, we're done.
				 */
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
			agbno = be32_to_cpu(*XFS_INOBT_PTR_ADDR(block, keyno, cur));
#ifdef DEBUG
			if ((error = xfs_btree_check_sptr(cur, agbno, level)))
				return error;
#endif
			cur->bc_ptrs[level] = keyno;
		}
	}
	/*
	 * Done with the search.
	 * See if we need to adjust the results.
	 */
	if (dir != XFS_LOOKUP_LE && diff < 0) {
		keyno++;
		/*
		 * If ge search and we went off the end of the block, but it's
		 * not the last block, we're in the wrong block.
		 */
		if (dir == XFS_LOOKUP_GE &&
		    keyno > be16_to_cpu(block->bb_numrecs) &&
		    be32_to_cpu(block->bb_rightsib) != NULLAGBLOCK) {
			int	i;

			cur->bc_ptrs[0] = keyno;
			if ((error = xfs_inobt_increment(cur, 0, &i)))
				return error;
			ASSERT(i == 1);
			*stat = 1;
			return 0;
		}
	}
	else if (dir == XFS_LOOKUP_LE && diff > 0)
		keyno--;
	cur->bc_ptrs[0] = keyno;
	/*
	 * Return if we succeeded or not.
	 */
	if (keyno == 0 || keyno > be16_to_cpu(block->bb_numrecs))
		*stat = 0;
	else
		*stat = ((dir != XFS_LOOKUP_EQ) || (diff == 0));
	return 0;
}

/*
 * Move 1 record left from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* error */
xfs_inobt_lshift(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to shift record on */
	int			*stat)	/* success/failure */
{
	int			error;	/* error return value */
#ifdef DEBUG
	int			i;	/* loop index */
#endif
	xfs_inobt_key_t		key;	/* key value for leaf level upward */
	xfs_buf_t		*lbp;	/* buffer for left neighbor block */
	xfs_inobt_block_t	*left;	/* left neighbor btree block */
	xfs_inobt_key_t		*lkp=NULL;	/* key pointer for left block */
	xfs_inobt_ptr_t		*lpp;	/* address pointer for left block */
	xfs_inobt_rec_t		*lrp=NULL;	/* record pointer for left block */
	int			nrec;	/* new number of left block entries */
	xfs_buf_t		*rbp;	/* buffer for right (current) block */
	xfs_inobt_block_t	*right;	/* right (current) btree block */
	xfs_inobt_key_t		*rkp=NULL;	/* key pointer for right block */
	xfs_inobt_ptr_t		*rpp=NULL;	/* address pointer for right block */
	xfs_inobt_rec_t		*rrp=NULL;	/* record pointer for right block */

	/*
	 * Set up variables for this block as "right".
	 */
	rbp = cur->bc_bufs[level];
	right = XFS_BUF_TO_INOBT_BLOCK(rbp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
		return error;
#endif
	/*
	 * If we've got no left sibling then we can't shift an entry left.
	 */
	if (be32_to_cpu(right->bb_leftsib) == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}
	/*
	 * If the cursor entry is the one that would be moved, don't
	 * do it... it's too complicated.
	 */
	if (cur->bc_ptrs[level] <= 1) {
		*stat = 0;
		return 0;
	}
	/*
	 * Set up the left neighbor as "left".
	 */
	if ((error = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, be32_to_cpu(right->bb_leftsib),
			0, &lbp, XFS_INO_BTREE_REF)))
		return error;
	left = XFS_BUF_TO_INOBT_BLOCK(lbp);
	if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
		return error;
	/*
	 * If it's full, it can't take another entry.
	 */
	if (be16_to_cpu(left->bb_numrecs) == XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		*stat = 0;
		return 0;
	}
	nrec = be16_to_cpu(left->bb_numrecs) + 1;
	/*
	 * If non-leaf, copy a key and a ptr to the left block.
	 */
	if (level > 0) {
		lkp = XFS_INOBT_KEY_ADDR(left, nrec, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		*lkp = *rkp;
		xfs_inobt_log_keys(cur, lbp, nrec, nrec);
		lpp = XFS_INOBT_PTR_ADDR(left, nrec, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(*rpp), level)))
			return error;
#endif
		*lpp = *rpp; /* INT_: no-change copy */
		xfs_inobt_log_ptrs(cur, lbp, nrec, nrec);
	}
	/*
	 * If leaf, copy a record to the left block.
	 */
	else {
		lrp = XFS_INOBT_REC_ADDR(left, nrec, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		*lrp = *rrp;
		xfs_inobt_log_recs(cur, lbp, nrec, nrec);
	}
	/*
	 * Bump and log left's numrecs, decrement and log right's numrecs.
	 */
	be16_add(&left->bb_numrecs, 1);
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, lkp - 1, lkp);
	else
		xfs_btree_check_rec(cur->bc_btnum, lrp - 1, lrp);
#endif
	be16_add(&right->bb_numrecs, -1);
	xfs_inobt_log_block(cur->bc_tp, rbp, XFS_BB_NUMRECS);
	/*
	 * Slide the contents of right down one entry.
	 */
	if (level > 0) {
#ifdef DEBUG
		for (i = 0; i < be16_to_cpu(right->bb_numrecs); i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(rpp[i + 1]),
					level)))
				return error;
		}
#endif
		memmove(rkp, rkp + 1, be16_to_cpu(right->bb_numrecs) * sizeof(*rkp));
		memmove(rpp, rpp + 1, be16_to_cpu(right->bb_numrecs) * sizeof(*rpp));
		xfs_inobt_log_keys(cur, rbp, 1, be16_to_cpu(right->bb_numrecs));
		xfs_inobt_log_ptrs(cur, rbp, 1, be16_to_cpu(right->bb_numrecs));
	} else {
		memmove(rrp, rrp + 1, be16_to_cpu(right->bb_numrecs) * sizeof(*rrp));
		xfs_inobt_log_recs(cur, rbp, 1, be16_to_cpu(right->bb_numrecs));
		key.ir_startino = rrp->ir_startino; /* INT_: direct copy */
		rkp = &key;
	}
	/*
	 * Update the parent key values of right.
	 */
	if ((error = xfs_inobt_updkey(cur, rkp, level + 1)))
		return error;
	/*
	 * Slide the cursor value left one.
	 */
	cur->bc_ptrs[level]--;
	*stat = 1;
	return 0;
}

/*
 * Allocate a new root block, fill it in.
 */
STATIC int				/* error */
xfs_inobt_newroot(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			*stat)	/* success/failure */
{
	xfs_agi_t		*agi;	/* a.g. inode header */
	xfs_alloc_arg_t		args;	/* allocation argument structure */
	xfs_inobt_block_t	*block;	/* one half of the old root block */
	xfs_buf_t		*bp;	/* buffer containing block */
	int			error;	/* error return value */
	xfs_inobt_key_t		*kp;	/* btree key pointer */
	xfs_agblock_t		lbno;	/* left block number */
	xfs_buf_t		*lbp;	/* left buffer pointer */
	xfs_inobt_block_t	*left;	/* left btree block */
	xfs_buf_t		*nbp;	/* new (root) buffer */
	xfs_inobt_block_t	*new;	/* new (root) btree block */
	int			nptr;	/* new value for key index, 1 or 2 */
	xfs_inobt_ptr_t		*pp;	/* btree address pointer */
	xfs_agblock_t		rbno;	/* right block number */
	xfs_buf_t		*rbp;	/* right buffer pointer */
	xfs_inobt_block_t	*right;	/* right btree block */
	xfs_inobt_rec_t		*rp;	/* btree record pointer */

	ASSERT(cur->bc_nlevels < XFS_IN_MAXLEVELS(cur->bc_mp));

	/*
	 * Get a block & a buffer.
	 */
	agi = XFS_BUF_TO_AGI(cur->bc_private.i.agbp);
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_private.i.agno,
		be32_to_cpu(agi->agi_root));
	args.mod = args.minleft = args.alignment = args.total = args.wasdel =
		args.isfl = args.userdata = args.minalignslop = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;
	if ((error = xfs_alloc_vextent(&args)))
		return error;
	/*
	 * None available, we fail.
	 */
	if (args.fsbno == NULLFSBLOCK) {
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	nbp = xfs_btree_get_bufs(args.mp, args.tp, args.agno, args.agbno, 0);
	new = XFS_BUF_TO_INOBT_BLOCK(nbp);
	/*
	 * Set the root data in the a.g. inode structure.
	 */
	agi->agi_root = cpu_to_be32(args.agbno);
	be32_add(&agi->agi_level, 1);
	xfs_ialloc_log_agi(args.tp, cur->bc_private.i.agbp,
		XFS_AGI_ROOT | XFS_AGI_LEVEL);
	/*
	 * At the previous root level there are now two blocks: the old
	 * root, and the new block generated when it was split.
	 * We don't know which one the cursor is pointing at, so we
	 * set up variables "left" and "right" for each case.
	 */
	bp = cur->bc_bufs[cur->bc_nlevels - 1];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, cur->bc_nlevels - 1, bp)))
		return error;
#endif
	if (be32_to_cpu(block->bb_rightsib) != NULLAGBLOCK) {
		/*
		 * Our block is left, pick up the right block.
		 */
		lbp = bp;
		lbno = XFS_DADDR_TO_AGBNO(args.mp, XFS_BUF_ADDR(lbp));
		left = block;
		rbno = be32_to_cpu(left->bb_rightsib);
		if ((error = xfs_btree_read_bufs(args.mp, args.tp, args.agno,
				rbno, 0, &rbp, XFS_INO_BTREE_REF)))
			return error;
		bp = rbp;
		right = XFS_BUF_TO_INOBT_BLOCK(rbp);
		if ((error = xfs_btree_check_sblock(cur, right,
				cur->bc_nlevels - 1, rbp)))
			return error;
		nptr = 1;
	} else {
		/*
		 * Our block is right, pick up the left block.
		 */
		rbp = bp;
		rbno = XFS_DADDR_TO_AGBNO(args.mp, XFS_BUF_ADDR(rbp));
		right = block;
		lbno = be32_to_cpu(right->bb_leftsib);
		if ((error = xfs_btree_read_bufs(args.mp, args.tp, args.agno,
				lbno, 0, &lbp, XFS_INO_BTREE_REF)))
			return error;
		bp = lbp;
		left = XFS_BUF_TO_INOBT_BLOCK(lbp);
		if ((error = xfs_btree_check_sblock(cur, left,
				cur->bc_nlevels - 1, lbp)))
			return error;
		nptr = 2;
	}
	/*
	 * Fill in the new block's btree header and log it.
	 */
	new->bb_magic = cpu_to_be32(xfs_magics[cur->bc_btnum]);
	new->bb_level = cpu_to_be16(cur->bc_nlevels);
	new->bb_numrecs = cpu_to_be16(2);
	new->bb_leftsib = cpu_to_be32(NULLAGBLOCK);
	new->bb_rightsib = cpu_to_be32(NULLAGBLOCK);
	xfs_inobt_log_block(args.tp, nbp, XFS_BB_ALL_BITS);
	ASSERT(lbno != NULLAGBLOCK && rbno != NULLAGBLOCK);
	/*
	 * Fill in the key data in the new root.
	 */
	kp = XFS_INOBT_KEY_ADDR(new, 1, cur);
	if (be16_to_cpu(left->bb_level) > 0) {
		kp[0] = *XFS_INOBT_KEY_ADDR(left, 1, cur); /* INT_: struct copy */
		kp[1] = *XFS_INOBT_KEY_ADDR(right, 1, cur); /* INT_: struct copy */
	} else {
		rp = XFS_INOBT_REC_ADDR(left, 1, cur);
		INT_COPY(kp[0].ir_startino, rp->ir_startino, ARCH_CONVERT);
		rp = XFS_INOBT_REC_ADDR(right, 1, cur);
		INT_COPY(kp[1].ir_startino, rp->ir_startino, ARCH_CONVERT);
	}
	xfs_inobt_log_keys(cur, nbp, 1, 2);
	/*
	 * Fill in the pointer data in the new root.
	 */
	pp = XFS_INOBT_PTR_ADDR(new, 1, cur);
	pp[0] = cpu_to_be32(lbno);
	pp[1] = cpu_to_be32(rbno);
	xfs_inobt_log_ptrs(cur, nbp, 1, 2);
	/*
	 * Fix up the cursor.
	 */
	xfs_btree_setbuf(cur, cur->bc_nlevels, nbp);
	cur->bc_ptrs[cur->bc_nlevels] = nptr;
	cur->bc_nlevels++;
	*stat = 1;
	return 0;
}

/*
 * Move 1 record right from cur/level if possible.
 * Update cur to reflect the new path.
 */
STATIC int				/* error */
xfs_inobt_rshift(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to shift record on */
	int			*stat)	/* success/failure */
{
	int			error;	/* error return value */
	int			i;	/* loop index */
	xfs_inobt_key_t		key;	/* key value for leaf level upward */
	xfs_buf_t		*lbp;	/* buffer for left (current) block */
	xfs_inobt_block_t	*left;	/* left (current) btree block */
	xfs_inobt_key_t		*lkp;	/* key pointer for left block */
	xfs_inobt_ptr_t		*lpp;	/* address pointer for left block */
	xfs_inobt_rec_t		*lrp;	/* record pointer for left block */
	xfs_buf_t		*rbp;	/* buffer for right neighbor block */
	xfs_inobt_block_t	*right;	/* right neighbor btree block */
	xfs_inobt_key_t		*rkp;	/* key pointer for right block */
	xfs_inobt_ptr_t		*rpp;	/* address pointer for right block */
	xfs_inobt_rec_t		*rrp=NULL;	/* record pointer for right block */
	xfs_btree_cur_t		*tcur;	/* temporary cursor */

	/*
	 * Set up variables for this block as "left".
	 */
	lbp = cur->bc_bufs[level];
	left = XFS_BUF_TO_INOBT_BLOCK(lbp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
		return error;
#endif
	/*
	 * If we've got no right sibling then we can't shift an entry right.
	 */
	if (be32_to_cpu(left->bb_rightsib) == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}
	/*
	 * If the cursor entry is the one that would be moved, don't
	 * do it... it's too complicated.
	 */
	if (cur->bc_ptrs[level] >= be16_to_cpu(left->bb_numrecs)) {
		*stat = 0;
		return 0;
	}
	/*
	 * Set up the right neighbor as "right".
	 */
	if ((error = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
			cur->bc_private.i.agno, be32_to_cpu(left->bb_rightsib),
			0, &rbp, XFS_INO_BTREE_REF)))
		return error;
	right = XFS_BUF_TO_INOBT_BLOCK(rbp);
	if ((error = xfs_btree_check_sblock(cur, right, level, rbp)))
		return error;
	/*
	 * If it's full, it can't take another entry.
	 */
	if (be16_to_cpu(right->bb_numrecs) == XFS_INOBT_BLOCK_MAXRECS(level, cur)) {
		*stat = 0;
		return 0;
	}
	/*
	 * Make a hole at the start of the right neighbor block, then
	 * copy the last left block entry to the hole.
	 */
	if (level > 0) {
		lkp = XFS_INOBT_KEY_ADDR(left, be16_to_cpu(left->bb_numrecs), cur);
		lpp = XFS_INOBT_PTR_ADDR(left, be16_to_cpu(left->bb_numrecs), cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = be16_to_cpu(right->bb_numrecs) - 1; i >= 0; i--) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(rpp[i]), level)))
				return error;
		}
#endif
		memmove(rkp + 1, rkp, be16_to_cpu(right->bb_numrecs) * sizeof(*rkp));
		memmove(rpp + 1, rpp, be16_to_cpu(right->bb_numrecs) * sizeof(*rpp));
#ifdef DEBUG
		if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(*lpp), level)))
			return error;
#endif
		*rkp = *lkp; /* INT_: no change copy */
		*rpp = *lpp; /* INT_: no change copy */
		xfs_inobt_log_keys(cur, rbp, 1, be16_to_cpu(right->bb_numrecs) + 1);
		xfs_inobt_log_ptrs(cur, rbp, 1, be16_to_cpu(right->bb_numrecs) + 1);
	} else {
		lrp = XFS_INOBT_REC_ADDR(left, be16_to_cpu(left->bb_numrecs), cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		memmove(rrp + 1, rrp, be16_to_cpu(right->bb_numrecs) * sizeof(*rrp));
		*rrp = *lrp;
		xfs_inobt_log_recs(cur, rbp, 1, be16_to_cpu(right->bb_numrecs) + 1);
		key.ir_startino = rrp->ir_startino; /* INT_: direct copy */
		rkp = &key;
	}
	/*
	 * Decrement and log left's numrecs, bump and log right's numrecs.
	 */
	be16_add(&left->bb_numrecs, -1);
	xfs_inobt_log_block(cur->bc_tp, lbp, XFS_BB_NUMRECS);
	be16_add(&right->bb_numrecs, 1);
#ifdef DEBUG
	if (level > 0)
		xfs_btree_check_key(cur->bc_btnum, rkp, rkp + 1);
	else
		xfs_btree_check_rec(cur->bc_btnum, rrp, rrp + 1);
#endif
	xfs_inobt_log_block(cur->bc_tp, rbp, XFS_BB_NUMRECS);
	/*
	 * Using a temporary cursor, update the parent key values of the
	 * block on the right.
	 */
	if ((error = xfs_btree_dup_cursor(cur, &tcur)))
		return error;
	xfs_btree_lastrec(tcur, level);
	if ((error = xfs_inobt_increment(tcur, level, &i)) ||
	    (error = xfs_inobt_updkey(tcur, rkp, level + 1))) {
		xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
		return error;
	}
	xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
	*stat = 1;
	return 0;
}

/*
 * Split cur/level block in half.
 * Return new block number and its first record (to be inserted into parent).
 */
STATIC int				/* error */
xfs_inobt_split(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level to split */
	xfs_agblock_t		*bnop,	/* output: block number allocated */
	xfs_inobt_key_t		*keyp,	/* output: first key of new block */
	xfs_btree_cur_t		**curp,	/* output: new cursor */
	int			*stat)	/* success/failure */
{
	xfs_alloc_arg_t		args;	/* allocation argument structure */
	int			error;	/* error return value */
	int			i;	/* loop index/record number */
	xfs_agblock_t		lbno;	/* left (current) block number */
	xfs_buf_t		*lbp;	/* buffer for left block */
	xfs_inobt_block_t	*left;	/* left (current) btree block */
	xfs_inobt_key_t		*lkp;	/* left btree key pointer */
	xfs_inobt_ptr_t		*lpp;	/* left btree address pointer */
	xfs_inobt_rec_t		*lrp;	/* left btree record pointer */
	xfs_buf_t		*rbp;	/* buffer for right block */
	xfs_inobt_block_t	*right;	/* right (new) btree block */
	xfs_inobt_key_t		*rkp;	/* right btree key pointer */
	xfs_inobt_ptr_t		*rpp;	/* right btree address pointer */
	xfs_inobt_rec_t		*rrp;	/* right btree record pointer */

	/*
	 * Set up left block (current one).
	 */
	lbp = cur->bc_bufs[level];
	args.tp = cur->bc_tp;
	args.mp = cur->bc_mp;
	lbno = XFS_DADDR_TO_AGBNO(args.mp, XFS_BUF_ADDR(lbp));
	/*
	 * Allocate the new block.
	 * If we can't do it, we're toast.  Give up.
	 */
	args.fsbno = XFS_AGB_TO_FSB(args.mp, cur->bc_private.i.agno, lbno);
	args.mod = args.minleft = args.alignment = args.total = args.wasdel =
		args.isfl = args.userdata = args.minalignslop = 0;
	args.minlen = args.maxlen = args.prod = 1;
	args.type = XFS_ALLOCTYPE_NEAR_BNO;
	if ((error = xfs_alloc_vextent(&args)))
		return error;
	if (args.fsbno == NULLFSBLOCK) {
		*stat = 0;
		return 0;
	}
	ASSERT(args.len == 1);
	rbp = xfs_btree_get_bufs(args.mp, args.tp, args.agno, args.agbno, 0);
	/*
	 * Set up the new block as "right".
	 */
	right = XFS_BUF_TO_INOBT_BLOCK(rbp);
	/*
	 * "Left" is the current (according to the cursor) block.
	 */
	left = XFS_BUF_TO_INOBT_BLOCK(lbp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, left, level, lbp)))
		return error;
#endif
	/*
	 * Fill in the btree header for the new block.
	 */
	right->bb_magic = cpu_to_be32(xfs_magics[cur->bc_btnum]);
	right->bb_level = left->bb_level;
	right->bb_numrecs = cpu_to_be16(be16_to_cpu(left->bb_numrecs) / 2);
	/*
	 * Make sure that if there's an odd number of entries now, that
	 * each new block will have the same number of entries.
	 */
	if ((be16_to_cpu(left->bb_numrecs) & 1) &&
	    cur->bc_ptrs[level] <= be16_to_cpu(right->bb_numrecs) + 1)
		be16_add(&right->bb_numrecs, 1);
	i = be16_to_cpu(left->bb_numrecs) - be16_to_cpu(right->bb_numrecs) + 1;
	/*
	 * For non-leaf blocks, copy keys and addresses over to the new block.
	 */
	if (level > 0) {
		lkp = XFS_INOBT_KEY_ADDR(left, i, cur);
		lpp = XFS_INOBT_PTR_ADDR(left, i, cur);
		rkp = XFS_INOBT_KEY_ADDR(right, 1, cur);
		rpp = XFS_INOBT_PTR_ADDR(right, 1, cur);
#ifdef DEBUG
		for (i = 0; i < be16_to_cpu(right->bb_numrecs); i++) {
			if ((error = xfs_btree_check_sptr(cur, be32_to_cpu(lpp[i]), level)))
				return error;
		}
#endif
		memcpy(rkp, lkp, be16_to_cpu(right->bb_numrecs) * sizeof(*rkp));
		memcpy(rpp, lpp, be16_to_cpu(right->bb_numrecs) * sizeof(*rpp));
		xfs_inobt_log_keys(cur, rbp, 1, be16_to_cpu(right->bb_numrecs));
		xfs_inobt_log_ptrs(cur, rbp, 1, be16_to_cpu(right->bb_numrecs));
		*keyp = *rkp;
	}
	/*
	 * For leaf blocks, copy records over to the new block.
	 */
	else {
		lrp = XFS_INOBT_REC_ADDR(left, i, cur);
		rrp = XFS_INOBT_REC_ADDR(right, 1, cur);
		memcpy(rrp, lrp, be16_to_cpu(right->bb_numrecs) * sizeof(*rrp));
		xfs_inobt_log_recs(cur, rbp, 1, be16_to_cpu(right->bb_numrecs));
		keyp->ir_startino = rrp->ir_startino; /* INT_: direct copy */
	}
	/*
	 * Find the left block number by looking in the buffer.
	 * Adjust numrecs, sibling pointers.
	 */
	be16_add(&left->bb_numrecs, -(be16_to_cpu(right->bb_numrecs)));
	right->bb_rightsib = left->bb_rightsib;
	left->bb_rightsib = cpu_to_be32(args.agbno);
	right->bb_leftsib = cpu_to_be32(lbno);
	xfs_inobt_log_block(args.tp, rbp, XFS_BB_ALL_BITS);
	xfs_inobt_log_block(args.tp, lbp, XFS_BB_NUMRECS | XFS_BB_RIGHTSIB);
	/*
	 * If there's a block to the new block's right, make that block
	 * point back to right instead of to left.
	 */
	if (be32_to_cpu(right->bb_rightsib) != NULLAGBLOCK) {
		xfs_inobt_block_t	*rrblock;	/* rr btree block */
		xfs_buf_t		*rrbp;		/* buffer for rrblock */

		if ((error = xfs_btree_read_bufs(args.mp, args.tp, args.agno,
				be32_to_cpu(right->bb_rightsib), 0, &rrbp,
				XFS_INO_BTREE_REF)))
			return error;
		rrblock = XFS_BUF_TO_INOBT_BLOCK(rrbp);
		if ((error = xfs_btree_check_sblock(cur, rrblock, level, rrbp)))
			return error;
		rrblock->bb_leftsib = cpu_to_be32(args.agbno);
		xfs_inobt_log_block(args.tp, rrbp, XFS_BB_LEFTSIB);
	}
	/*
	 * If the cursor is really in the right block, move it there.
	 * If it's just pointing past the last entry in left, then we'll
	 * insert there, so don't change anything in that case.
	 */
	if (cur->bc_ptrs[level] > be16_to_cpu(left->bb_numrecs) + 1) {
		xfs_btree_setbuf(cur, level, rbp);
		cur->bc_ptrs[level] -= be16_to_cpu(left->bb_numrecs);
	}
	/*
	 * If there are more levels, we'll need another cursor which refers
	 * the right block, no matter where this cursor was.
	 */
	if (level + 1 < cur->bc_nlevels) {
		if ((error = xfs_btree_dup_cursor(cur, curp)))
			return error;
		(*curp)->bc_ptrs[level + 1]++;
	}
	*bnop = args.agbno;
	*stat = 1;
	return 0;
}

/*
 * Update keys at all levels from here to the root along the cursor's path.
 */
STATIC int				/* error */
xfs_inobt_updkey(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_inobt_key_t		*keyp,	/* new key value to update to */
	int			level)	/* starting level for update */
{
	int			ptr;	/* index of key in block */

	/*
	 * Go up the tree from this level toward the root.
	 * At each level, update the key value to the value input.
	 * Stop when we reach a level where the cursor isn't pointing
	 * at the first entry in the block.
	 */
	for (ptr = 1; ptr == 1 && level < cur->bc_nlevels; level++) {
		xfs_buf_t		*bp;	/* buffer for block */
		xfs_inobt_block_t	*block;	/* btree block */
#ifdef DEBUG
		int			error;	/* error return value */
#endif
		xfs_inobt_key_t		*kp;	/* ptr to btree block keys */

		bp = cur->bc_bufs[level];
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
		if ((error = xfs_btree_check_sblock(cur, block, level, bp)))
			return error;
#endif
		ptr = cur->bc_ptrs[level];
		kp = XFS_INOBT_KEY_ADDR(block, ptr, cur);
		*kp = *keyp;
		xfs_inobt_log_keys(cur, bp, ptr, ptr);
	}
	return 0;
}

/*
 * Externally visible routines.
 */

/*
 * Decrement cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* error */
xfs_inobt_decrement(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level in btree, 0 is leaf */
	int			*stat)	/* success/failure */
{
	xfs_inobt_block_t	*block;	/* btree block */
	int			error;
	int			lev;	/* btree level */

	ASSERT(level < cur->bc_nlevels);
	/*
	 * Read-ahead to the left at this level.
	 */
	xfs_btree_readahead(cur, level, XFS_BTCUR_LEFTRA);
	/*
	 * Decrement the ptr at this level.  If we're still in the block
	 * then we're done.
	 */
	if (--cur->bc_ptrs[level] > 0) {
		*stat = 1;
		return 0;
	}
	/*
	 * Get a pointer to the btree block.
	 */
	block = XFS_BUF_TO_INOBT_BLOCK(cur->bc_bufs[level]);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, level,
			cur->bc_bufs[level])))
		return error;
#endif
	/*
	 * If we just went off the left edge of the tree, return failure.
	 */
	if (be32_to_cpu(block->bb_leftsib) == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}
	/*
	 * March up the tree decrementing pointers.
	 * Stop when we don't go off the left edge of a block.
	 */
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		if (--cur->bc_ptrs[lev] > 0)
			break;
		/*
		 * Read-ahead the left block, we're going to read it
		 * in the next loop.
		 */
		xfs_btree_readahead(cur, lev, XFS_BTCUR_LEFTRA);
	}
	/*
	 * If we went off the root then we are seriously confused.
	 */
	ASSERT(lev < cur->bc_nlevels);
	/*
	 * Now walk back down the tree, fixing up the cursor's buffer
	 * pointers and key numbers.
	 */
	for (block = XFS_BUF_TO_INOBT_BLOCK(cur->bc_bufs[lev]); lev > level; ) {
		xfs_agblock_t	agbno;	/* block number of btree block */
		xfs_buf_t	*bp;	/* buffer containing btree block */

		agbno = be32_to_cpu(*XFS_INOBT_PTR_ADDR(block, cur->bc_ptrs[lev], cur));
		if ((error = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
				cur->bc_private.i.agno, agbno, 0, &bp,
				XFS_INO_BTREE_REF)))
			return error;
		lev--;
		xfs_btree_setbuf(cur, lev, bp);
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		if ((error = xfs_btree_check_sblock(cur, block, lev, bp)))
			return error;
		cur->bc_ptrs[lev] = be16_to_cpu(block->bb_numrecs);
	}
	*stat = 1;
	return 0;
}

/*
 * Delete the record pointed to by cur.
 * The cursor refers to the place where the record was (could be inserted)
 * when the operation returns.
 */
int					/* error */
xfs_inobt_delete(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		*stat)		/* success/failure */
{
	int		error;
	int		i;		/* result code */
	int		level;		/* btree level */

	/*
	 * Go up the tree, starting at leaf level.
	 * If 2 is returned then a join was done; go to the next level.
	 * Otherwise we are done.
	 */
	for (level = 0, i = 2; i == 2; level++) {
		if ((error = xfs_inobt_delrec(cur, level, &i)))
			return error;
	}
	if (i == 0) {
		for (level = 1; level < cur->bc_nlevels; level++) {
			if (cur->bc_ptrs[level] == 0) {
				if ((error = xfs_inobt_decrement(cur, level, &i)))
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
xfs_inobt_get_rec(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agino_t		*ino,	/* output: starting inode of chunk */
	__int32_t		*fcnt,	/* output: number of free inodes */
	xfs_inofree_t		*free,	/* output: free inode mask */
	int			*stat)	/* output: success/failure */
{
	xfs_inobt_block_t	*block;	/* btree block */
	xfs_buf_t		*bp;	/* buffer containing btree block */
#ifdef DEBUG
	int			error;	/* error return value */
#endif
	int			ptr;	/* record number */
	xfs_inobt_rec_t		*rec;	/* record data */

	bp = cur->bc_bufs[0];
	ptr = cur->bc_ptrs[0];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, 0, bp)))
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
	rec = XFS_INOBT_REC_ADDR(block, ptr, cur);
	*ino = INT_GET(rec->ir_startino, ARCH_CONVERT);
	*fcnt = INT_GET(rec->ir_freecount, ARCH_CONVERT);
	*free = INT_GET(rec->ir_free, ARCH_CONVERT);
	*stat = 1;
	return 0;
}

/*
 * Increment cursor by one record at the level.
 * For nonzero levels the leaf-ward information is untouched.
 */
int					/* error */
xfs_inobt_increment(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	int			level,	/* level in btree, 0 is leaf */
	int			*stat)	/* success/failure */
{
	xfs_inobt_block_t	*block;	/* btree block */
	xfs_buf_t		*bp;	/* buffer containing btree block */
	int			error;	/* error return value */
	int			lev;	/* btree level */

	ASSERT(level < cur->bc_nlevels);
	/*
	 * Read-ahead to the right at this level.
	 */
	xfs_btree_readahead(cur, level, XFS_BTCUR_RIGHTRA);
	/*
	 * Get a pointer to the btree block.
	 */
	bp = cur->bc_bufs[level];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, level, bp)))
		return error;
#endif
	/*
	 * Increment the ptr at this level.  If we're still in the block
	 * then we're done.
	 */
	if (++cur->bc_ptrs[level] <= be16_to_cpu(block->bb_numrecs)) {
		*stat = 1;
		return 0;
	}
	/*
	 * If we just went off the right edge of the tree, return failure.
	 */
	if (be32_to_cpu(block->bb_rightsib) == NULLAGBLOCK) {
		*stat = 0;
		return 0;
	}
	/*
	 * March up the tree incrementing pointers.
	 * Stop when we don't go off the right edge of a block.
	 */
	for (lev = level + 1; lev < cur->bc_nlevels; lev++) {
		bp = cur->bc_bufs[lev];
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
		if ((error = xfs_btree_check_sblock(cur, block, lev, bp)))
			return error;
#endif
		if (++cur->bc_ptrs[lev] <= be16_to_cpu(block->bb_numrecs))
			break;
		/*
		 * Read-ahead the right block, we're going to read it
		 * in the next loop.
		 */
		xfs_btree_readahead(cur, lev, XFS_BTCUR_RIGHTRA);
	}
	/*
	 * If we went off the root then we are seriously confused.
	 */
	ASSERT(lev < cur->bc_nlevels);
	/*
	 * Now walk back down the tree, fixing up the cursor's buffer
	 * pointers and key numbers.
	 */
	for (bp = cur->bc_bufs[lev], block = XFS_BUF_TO_INOBT_BLOCK(bp);
	     lev > level; ) {
		xfs_agblock_t	agbno;	/* block number of btree block */

		agbno = be32_to_cpu(*XFS_INOBT_PTR_ADDR(block, cur->bc_ptrs[lev], cur));
		if ((error = xfs_btree_read_bufs(cur->bc_mp, cur->bc_tp,
				cur->bc_private.i.agno, agbno, 0, &bp,
				XFS_INO_BTREE_REF)))
			return error;
		lev--;
		xfs_btree_setbuf(cur, lev, bp);
		block = XFS_BUF_TO_INOBT_BLOCK(bp);
		if ((error = xfs_btree_check_sblock(cur, block, lev, bp)))
			return error;
		cur->bc_ptrs[lev] = 1;
	}
	*stat = 1;
	return 0;
}

/*
 * Insert the current record at the point referenced by cur.
 * The cursor may be inconsistent on return if splits have been done.
 */
int					/* error */
xfs_inobt_insert(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	int		*stat)		/* success/failure */
{
	int		error;		/* error return value */
	int		i;		/* result value, 0 for failure */
	int		level;		/* current level number in btree */
	xfs_agblock_t	nbno;		/* new block number (split result) */
	xfs_btree_cur_t	*ncur;		/* new cursor (split result) */
	xfs_inobt_rec_t	nrec;		/* record being inserted this level */
	xfs_btree_cur_t	*pcur;		/* previous level's cursor */

	level = 0;
	nbno = NULLAGBLOCK;
	INT_SET(nrec.ir_startino, ARCH_CONVERT, cur->bc_rec.i.ir_startino);
	INT_SET(nrec.ir_freecount, ARCH_CONVERT, cur->bc_rec.i.ir_freecount);
	INT_SET(nrec.ir_free, ARCH_CONVERT, cur->bc_rec.i.ir_free);
	ncur = (xfs_btree_cur_t *)0;
	pcur = cur;
	/*
	 * Loop going up the tree, starting at the leaf level.
	 * Stop when we don't get a split block, that must mean that
	 * the insert is finished with this level.
	 */
	do {
		/*
		 * Insert nrec/nbno into this level of the tree.
		 * Note if we fail, nbno will be null.
		 */
		if ((error = xfs_inobt_insrec(pcur, level++, &nbno, &nrec, &ncur,
				&i))) {
			if (pcur != cur)
				xfs_btree_del_cursor(pcur, XFS_BTREE_ERROR);
			return error;
		}
		/*
		 * See if the cursor we just used is trash.
		 * Can't trash the caller's cursor, but otherwise we should
		 * if ncur is a new cursor or we're about to be done.
		 */
		if (pcur != cur && (ncur || nbno == NULLAGBLOCK)) {
			cur->bc_nlevels = pcur->bc_nlevels;
			xfs_btree_del_cursor(pcur, XFS_BTREE_NOERROR);
		}
		/*
		 * If we got a new cursor, switch to it.
		 */
		if (ncur) {
			pcur = ncur;
			ncur = (xfs_btree_cur_t *)0;
		}
	} while (nbno != NULLAGBLOCK);
	*stat = i;
	return 0;
}

/*
 * Lookup the record equal to ino in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup_eq(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agino_t	ino,		/* starting inode of chunk */
	__int32_t	fcnt,		/* free inode count */
	xfs_inofree_t	free,		/* free inode mask */
	int		*stat)		/* success/failure */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = fcnt;
	cur->bc_rec.i.ir_free = free;
	return xfs_inobt_lookup(cur, XFS_LOOKUP_EQ, stat);
}

/*
 * Lookup the first record greater than or equal to ino
 * in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup_ge(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agino_t	ino,		/* starting inode of chunk */
	__int32_t	fcnt,		/* free inode count */
	xfs_inofree_t	free,		/* free inode mask */
	int		*stat)		/* success/failure */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = fcnt;
	cur->bc_rec.i.ir_free = free;
	return xfs_inobt_lookup(cur, XFS_LOOKUP_GE, stat);
}

/*
 * Lookup the first record less than or equal to ino
 * in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup_le(
	xfs_btree_cur_t	*cur,		/* btree cursor */
	xfs_agino_t	ino,		/* starting inode of chunk */
	__int32_t	fcnt,		/* free inode count */
	xfs_inofree_t	free,		/* free inode mask */
	int		*stat)		/* success/failure */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = fcnt;
	cur->bc_rec.i.ir_free = free;
	return xfs_inobt_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Update the record referred to by cur, to the value given
 * by [ino, fcnt, free].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
int					/* error */
xfs_inobt_update(
	xfs_btree_cur_t		*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	__int32_t		fcnt,	/* free inode count */
	xfs_inofree_t		free)	/* free inode mask */
{
	xfs_inobt_block_t	*block;	/* btree block to update */
	xfs_buf_t		*bp;	/* buffer containing btree block */
	int			error;	/* error return value */
	int			ptr;	/* current record number (updating) */
	xfs_inobt_rec_t		*rp;	/* pointer to updated record */

	/*
	 * Pick up the current block.
	 */
	bp = cur->bc_bufs[0];
	block = XFS_BUF_TO_INOBT_BLOCK(bp);
#ifdef DEBUG
	if ((error = xfs_btree_check_sblock(cur, block, 0, bp)))
		return error;
#endif
	/*
	 * Get the address of the rec to be updated.
	 */
	ptr = cur->bc_ptrs[0];
	rp = XFS_INOBT_REC_ADDR(block, ptr, cur);
	/*
	 * Fill in the new contents and log them.
	 */
	INT_SET(rp->ir_startino, ARCH_CONVERT, ino);
	INT_SET(rp->ir_freecount, ARCH_CONVERT, fcnt);
	INT_SET(rp->ir_free, ARCH_CONVERT, free);
	xfs_inobt_log_recs(cur, bp, ptr, ptr);
	/*
	 * Updating first record in leaf. Pass new key value up to our parent.
	 */
	if (ptr == 1) {
		xfs_inobt_key_t	key;	/* key containing [ino] */

		INT_SET(key.ir_startino, ARCH_CONVERT, ino);
		if ((error = xfs_inobt_updkey(cur, &key, 1)))
			return error;
	}
	return 0;
}
