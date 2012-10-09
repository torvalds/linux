/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir2.h"
#include "xfs_dir2_format.h"
#include "xfs_dir2_priv.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"
#include "xfs_trace.h"

/*
 * xfs_da_btree.c
 *
 * Routines to implement directories as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC int xfs_da_root_split(xfs_da_state_t *state,
					    xfs_da_state_blk_t *existing_root,
					    xfs_da_state_blk_t *new_child);
STATIC int xfs_da_node_split(xfs_da_state_t *state,
					    xfs_da_state_blk_t *existing_blk,
					    xfs_da_state_blk_t *split_blk,
					    xfs_da_state_blk_t *blk_to_add,
					    int treelevel,
					    int *result);
STATIC void xfs_da_node_rebalance(xfs_da_state_t *state,
					 xfs_da_state_blk_t *node_blk_1,
					 xfs_da_state_blk_t *node_blk_2);
STATIC void xfs_da_node_add(xfs_da_state_t *state,
				   xfs_da_state_blk_t *old_node_blk,
				   xfs_da_state_blk_t *new_node_blk);

/*
 * Routines used for shrinking the Btree.
 */
STATIC int xfs_da_root_join(xfs_da_state_t *state,
					   xfs_da_state_blk_t *root_blk);
STATIC int xfs_da_node_toosmall(xfs_da_state_t *state, int *retval);
STATIC void xfs_da_node_remove(xfs_da_state_t *state,
					      xfs_da_state_blk_t *drop_blk);
STATIC void xfs_da_node_unbalance(xfs_da_state_t *state,
					 xfs_da_state_blk_t *src_node_blk,
					 xfs_da_state_blk_t *dst_node_blk);

/*
 * Utility routines.
 */
STATIC uint	xfs_da_node_lasthash(struct xfs_buf *bp, int *count);
STATIC int	xfs_da_node_order(struct xfs_buf *node1_bp,
				  struct xfs_buf *node2_bp);
STATIC int	xfs_da_blk_unlink(xfs_da_state_t *state,
				  xfs_da_state_blk_t *drop_blk,
				  xfs_da_state_blk_t *save_blk);
STATIC void	xfs_da_state_kill_altpath(xfs_da_state_t *state);

/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of an intermediate node.
 */
int
xfs_da_node_create(xfs_da_args_t *args, xfs_dablk_t blkno, int level,
				 struct xfs_buf **bpp, int whichfork)
{
	xfs_da_intnode_t *node;
	struct xfs_buf *bp;
	int error;
	xfs_trans_t *tp;

	trace_xfs_da_node_create(args);

	tp = args->trans;
	error = xfs_da_get_buf(tp, args->dp, blkno, -1, &bp, whichfork);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	node = bp->b_addr;
	node->hdr.info.forw = 0;
	node->hdr.info.back = 0;
	node->hdr.info.magic = cpu_to_be16(XFS_DA_NODE_MAGIC);
	node->hdr.info.pad = 0;
	node->hdr.count = 0;
	node->hdr.level = cpu_to_be16(level);

	xfs_trans_log_buf(tp, bp,
		XFS_DA_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

	*bpp = bp;
	return(0);
}

/*
 * Split a leaf node, rebalance, then possibly split
 * intermediate nodes, rebalance, etc.
 */
int							/* error */
xfs_da_split(xfs_da_state_t *state)
{
	xfs_da_state_blk_t *oldblk, *newblk, *addblk;
	xfs_da_intnode_t *node;
	struct xfs_buf *bp;
	int max, action, error, i;

	trace_xfs_da_split(state->args);

	/*
	 * Walk back up the tree splitting/inserting/adjusting as necessary.
	 * If we need to insert and there isn't room, split the node, then
	 * decide which fragment to insert the new block from below into.
	 * Note that we may split the root this way, but we need more fixup.
	 */
	max = state->path.active - 1;
	ASSERT((max >= 0) && (max < XFS_DA_NODE_MAXDEPTH));
	ASSERT(state->path.blk[max].magic == XFS_ATTR_LEAF_MAGIC ||
	       state->path.blk[max].magic == XFS_DIR2_LEAFN_MAGIC);

	addblk = &state->path.blk[max];		/* initial dummy value */
	for (i = max; (i >= 0) && addblk; state->path.active--, i--) {
		oldblk = &state->path.blk[i];
		newblk = &state->altpath.blk[i];

		/*
		 * If a leaf node then
		 *     Allocate a new leaf node, then rebalance across them.
		 * else if an intermediate node then
		 *     We split on the last layer, must we split the node?
		 */
		switch (oldblk->magic) {
		case XFS_ATTR_LEAF_MAGIC:
			error = xfs_attr_leaf_split(state, oldblk, newblk);
			if ((error != 0) && (error != ENOSPC)) {
				return(error);	/* GROT: attr is inconsistent */
			}
			if (!error) {
				addblk = newblk;
				break;
			}
			/*
			 * Entry wouldn't fit, split the leaf again.
			 */
			state->extravalid = 1;
			if (state->inleaf) {
				state->extraafter = 0;	/* before newblk */
				trace_xfs_attr_leaf_split_before(state->args);
				error = xfs_attr_leaf_split(state, oldblk,
							    &state->extrablk);
			} else {
				state->extraafter = 1;	/* after newblk */
				trace_xfs_attr_leaf_split_after(state->args);
				error = xfs_attr_leaf_split(state, newblk,
							    &state->extrablk);
			}
			if (error)
				return(error);	/* GROT: attr inconsistent */
			addblk = newblk;
			break;
		case XFS_DIR2_LEAFN_MAGIC:
			error = xfs_dir2_leafn_split(state, oldblk, newblk);
			if (error)
				return error;
			addblk = newblk;
			break;
		case XFS_DA_NODE_MAGIC:
			error = xfs_da_node_split(state, oldblk, newblk, addblk,
							 max - i, &action);
			addblk->bp = NULL;
			if (error)
				return(error);	/* GROT: dir is inconsistent */
			/*
			 * Record the newly split block for the next time thru?
			 */
			if (action)
				addblk = newblk;
			else
				addblk = NULL;
			break;
		}

		/*
		 * Update the btree to show the new hashval for this child.
		 */
		xfs_da_fixhashpath(state, &state->path);
	}
	if (!addblk)
		return(0);

	/*
	 * Split the root node.
	 */
	ASSERT(state->path.active == 0);
	oldblk = &state->path.blk[0];
	error = xfs_da_root_split(state, oldblk, addblk);
	if (error) {
		addblk->bp = NULL;
		return(error);	/* GROT: dir is inconsistent */
	}

	/*
	 * Update pointers to the node which used to be block 0 and
	 * just got bumped because of the addition of a new root node.
	 * There might be three blocks involved if a double split occurred,
	 * and the original block 0 could be at any position in the list.
	 */

	node = oldblk->bp->b_addr;
	if (node->hdr.info.forw) {
		if (be32_to_cpu(node->hdr.info.forw) == addblk->blkno) {
			bp = addblk->bp;
		} else {
			ASSERT(state->extravalid);
			bp = state->extrablk.bp;
		}
		node = bp->b_addr;
		node->hdr.info.back = cpu_to_be32(oldblk->blkno);
		xfs_trans_log_buf(state->args->trans, bp,
		    XFS_DA_LOGRANGE(node, &node->hdr.info,
		    sizeof(node->hdr.info)));
	}
	node = oldblk->bp->b_addr;
	if (node->hdr.info.back) {
		if (be32_to_cpu(node->hdr.info.back) == addblk->blkno) {
			bp = addblk->bp;
		} else {
			ASSERT(state->extravalid);
			bp = state->extrablk.bp;
		}
		node = bp->b_addr;
		node->hdr.info.forw = cpu_to_be32(oldblk->blkno);
		xfs_trans_log_buf(state->args->trans, bp,
		    XFS_DA_LOGRANGE(node, &node->hdr.info,
		    sizeof(node->hdr.info)));
	}
	addblk->bp = NULL;
	return(0);
}

/*
 * Split the root.  We have to create a new root and point to the two
 * parts (the split old root) that we just created.  Copy block zero to
 * the EOF, extending the inode in process.
 */
STATIC int						/* error */
xfs_da_root_split(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				 xfs_da_state_blk_t *blk2)
{
	xfs_da_intnode_t *node, *oldroot;
	xfs_da_args_t *args;
	xfs_dablk_t blkno;
	struct xfs_buf *bp;
	int error, size;
	xfs_inode_t *dp;
	xfs_trans_t *tp;
	xfs_mount_t *mp;
	xfs_dir2_leaf_t *leaf;

	trace_xfs_da_root_split(state->args);

	/*
	 * Copy the existing (incorrect) block from the root node position
	 * to a free space somewhere.
	 */
	args = state->args;
	ASSERT(args != NULL);
	error = xfs_da_grow_inode(args, &blkno);
	if (error)
		return(error);
	dp = args->dp;
	tp = args->trans;
	mp = state->mp;
	error = xfs_da_get_buf(tp, dp, blkno, -1, &bp, args->whichfork);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	node = bp->b_addr;
	oldroot = blk1->bp->b_addr;
	if (oldroot->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC)) {
		size = (int)((char *)&oldroot->btree[be16_to_cpu(oldroot->hdr.count)] -
			     (char *)oldroot);
	} else {
		ASSERT(oldroot->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC));
		leaf = (xfs_dir2_leaf_t *)oldroot;
		size = (int)((char *)&leaf->ents[be16_to_cpu(leaf->hdr.count)] -
			     (char *)leaf);
	}
	memcpy(node, oldroot, size);
	xfs_trans_log_buf(tp, bp, 0, size - 1);
	blk1->bp = bp;
	blk1->blkno = blkno;

	/*
	 * Set up the new root node.
	 */
	error = xfs_da_node_create(args,
		(args->whichfork == XFS_DATA_FORK) ? mp->m_dirleafblk : 0,
		be16_to_cpu(node->hdr.level) + 1, &bp, args->whichfork);
	if (error)
		return(error);
	node = bp->b_addr;
	node->btree[0].hashval = cpu_to_be32(blk1->hashval);
	node->btree[0].before = cpu_to_be32(blk1->blkno);
	node->btree[1].hashval = cpu_to_be32(blk2->hashval);
	node->btree[1].before = cpu_to_be32(blk2->blkno);
	node->hdr.count = cpu_to_be16(2);

#ifdef DEBUG
	if (oldroot->hdr.info.magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC)) {
		ASSERT(blk1->blkno >= mp->m_dirleafblk &&
		       blk1->blkno < mp->m_dirfreeblk);
		ASSERT(blk2->blkno >= mp->m_dirleafblk &&
		       blk2->blkno < mp->m_dirfreeblk);
	}
#endif

	/* Header is already logged by xfs_da_node_create */
	xfs_trans_log_buf(tp, bp,
		XFS_DA_LOGRANGE(node, node->btree,
			sizeof(xfs_da_node_entry_t) * 2));

	return(0);
}

/*
 * Split the node, rebalance, then add the new entry.
 */
STATIC int						/* error */
xfs_da_node_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
				 xfs_da_state_blk_t *newblk,
				 xfs_da_state_blk_t *addblk,
				 int treelevel, int *result)
{
	xfs_da_intnode_t *node;
	xfs_dablk_t blkno;
	int newcount, error;
	int useextra;

	trace_xfs_da_node_split(state->args);

	node = oldblk->bp->b_addr;
	ASSERT(node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));

	/*
	 * With V2 dirs the extra block is data or freespace.
	 */
	useextra = state->extravalid && state->args->whichfork == XFS_ATTR_FORK;
	newcount = 1 + useextra;
	/*
	 * Do we have to split the node?
	 */
	if ((be16_to_cpu(node->hdr.count) + newcount) > state->node_ents) {
		/*
		 * Allocate a new node, add to the doubly linked chain of
		 * nodes, then move some of our excess entries into it.
		 */
		error = xfs_da_grow_inode(state->args, &blkno);
		if (error)
			return(error);	/* GROT: dir is inconsistent */

		error = xfs_da_node_create(state->args, blkno, treelevel,
					   &newblk->bp, state->args->whichfork);
		if (error)
			return(error);	/* GROT: dir is inconsistent */
		newblk->blkno = blkno;
		newblk->magic = XFS_DA_NODE_MAGIC;
		xfs_da_node_rebalance(state, oldblk, newblk);
		error = xfs_da_blk_link(state, oldblk, newblk);
		if (error)
			return(error);
		*result = 1;
	} else {
		*result = 0;
	}

	/*
	 * Insert the new entry(s) into the correct block
	 * (updating last hashval in the process).
	 *
	 * xfs_da_node_add() inserts BEFORE the given index,
	 * and as a result of using node_lookup_int() we always
	 * point to a valid entry (not after one), but a split
	 * operation always results in a new block whose hashvals
	 * FOLLOW the current block.
	 *
	 * If we had double-split op below us, then add the extra block too.
	 */
	node = oldblk->bp->b_addr;
	if (oldblk->index <= be16_to_cpu(node->hdr.count)) {
		oldblk->index++;
		xfs_da_node_add(state, oldblk, addblk);
		if (useextra) {
			if (state->extraafter)
				oldblk->index++;
			xfs_da_node_add(state, oldblk, &state->extrablk);
			state->extravalid = 0;
		}
	} else {
		newblk->index++;
		xfs_da_node_add(state, newblk, addblk);
		if (useextra) {
			if (state->extraafter)
				newblk->index++;
			xfs_da_node_add(state, newblk, &state->extrablk);
			state->extravalid = 0;
		}
	}

	return(0);
}

/*
 * Balance the btree elements between two intermediate nodes,
 * usually one full and one empty.
 *
 * NOTE: if blk2 is empty, then it will get the upper half of blk1.
 */
STATIC void
xfs_da_node_rebalance(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				     xfs_da_state_blk_t *blk2)
{
	xfs_da_intnode_t *node1, *node2, *tmpnode;
	xfs_da_node_entry_t *btree_s, *btree_d;
	int count, tmp;
	xfs_trans_t *tp;

	trace_xfs_da_node_rebalance(state->args);

	node1 = blk1->bp->b_addr;
	node2 = blk2->bp->b_addr;
	/*
	 * Figure out how many entries need to move, and in which direction.
	 * Swap the nodes around if that makes it simpler.
	 */
	if ((be16_to_cpu(node1->hdr.count) > 0) && (be16_to_cpu(node2->hdr.count) > 0) &&
	    ((be32_to_cpu(node2->btree[0].hashval) < be32_to_cpu(node1->btree[0].hashval)) ||
	     (be32_to_cpu(node2->btree[be16_to_cpu(node2->hdr.count)-1].hashval) <
	      be32_to_cpu(node1->btree[be16_to_cpu(node1->hdr.count)-1].hashval)))) {
		tmpnode = node1;
		node1 = node2;
		node2 = tmpnode;
	}
	ASSERT(node1->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	ASSERT(node2->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	count = (be16_to_cpu(node1->hdr.count) - be16_to_cpu(node2->hdr.count)) / 2;
	if (count == 0)
		return;
	tp = state->args->trans;
	/*
	 * Two cases: high-to-low and low-to-high.
	 */
	if (count > 0) {
		/*
		 * Move elements in node2 up to make a hole.
		 */
		if ((tmp = be16_to_cpu(node2->hdr.count)) > 0) {
			tmp *= (uint)sizeof(xfs_da_node_entry_t);
			btree_s = &node2->btree[0];
			btree_d = &node2->btree[count];
			memmove(btree_d, btree_s, tmp);
		}

		/*
		 * Move the req'd B-tree elements from high in node1 to
		 * low in node2.
		 */
		be16_add_cpu(&node2->hdr.count, count);
		tmp = count * (uint)sizeof(xfs_da_node_entry_t);
		btree_s = &node1->btree[be16_to_cpu(node1->hdr.count) - count];
		btree_d = &node2->btree[0];
		memcpy(btree_d, btree_s, tmp);
		be16_add_cpu(&node1->hdr.count, -count);
	} else {
		/*
		 * Move the req'd B-tree elements from low in node2 to
		 * high in node1.
		 */
		count = -count;
		tmp = count * (uint)sizeof(xfs_da_node_entry_t);
		btree_s = &node2->btree[0];
		btree_d = &node1->btree[be16_to_cpu(node1->hdr.count)];
		memcpy(btree_d, btree_s, tmp);
		be16_add_cpu(&node1->hdr.count, count);
		xfs_trans_log_buf(tp, blk1->bp,
			XFS_DA_LOGRANGE(node1, btree_d, tmp));

		/*
		 * Move elements in node2 down to fill the hole.
		 */
		tmp  = be16_to_cpu(node2->hdr.count) - count;
		tmp *= (uint)sizeof(xfs_da_node_entry_t);
		btree_s = &node2->btree[count];
		btree_d = &node2->btree[0];
		memmove(btree_d, btree_s, tmp);
		be16_add_cpu(&node2->hdr.count, -count);
	}

	/*
	 * Log header of node 1 and all current bits of node 2.
	 */
	xfs_trans_log_buf(tp, blk1->bp,
		XFS_DA_LOGRANGE(node1, &node1->hdr, sizeof(node1->hdr)));
	xfs_trans_log_buf(tp, blk2->bp,
		XFS_DA_LOGRANGE(node2, &node2->hdr,
			sizeof(node2->hdr) +
			sizeof(node2->btree[0]) * be16_to_cpu(node2->hdr.count)));

	/*
	 * Record the last hashval from each block for upward propagation.
	 * (note: don't use the swapped node pointers)
	 */
	node1 = blk1->bp->b_addr;
	node2 = blk2->bp->b_addr;
	blk1->hashval = be32_to_cpu(node1->btree[be16_to_cpu(node1->hdr.count)-1].hashval);
	blk2->hashval = be32_to_cpu(node2->btree[be16_to_cpu(node2->hdr.count)-1].hashval);

	/*
	 * Adjust the expected index for insertion.
	 */
	if (blk1->index >= be16_to_cpu(node1->hdr.count)) {
		blk2->index = blk1->index - be16_to_cpu(node1->hdr.count);
		blk1->index = be16_to_cpu(node1->hdr.count) + 1;	/* make it invalid */
	}
}

/*
 * Add a new entry to an intermediate node.
 */
STATIC void
xfs_da_node_add(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
			       xfs_da_state_blk_t *newblk)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	int tmp;

	trace_xfs_da_node_add(state->args);

	node = oldblk->bp->b_addr;
	ASSERT(node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	ASSERT((oldblk->index >= 0) && (oldblk->index <= be16_to_cpu(node->hdr.count)));
	ASSERT(newblk->blkno != 0);
	if (state->args->whichfork == XFS_DATA_FORK)
		ASSERT(newblk->blkno >= state->mp->m_dirleafblk &&
		       newblk->blkno < state->mp->m_dirfreeblk);

	/*
	 * We may need to make some room before we insert the new node.
	 */
	tmp = 0;
	btree = &node->btree[ oldblk->index ];
	if (oldblk->index < be16_to_cpu(node->hdr.count)) {
		tmp = (be16_to_cpu(node->hdr.count) - oldblk->index) * (uint)sizeof(*btree);
		memmove(btree + 1, btree, tmp);
	}
	btree->hashval = cpu_to_be32(newblk->hashval);
	btree->before = cpu_to_be32(newblk->blkno);
	xfs_trans_log_buf(state->args->trans, oldblk->bp,
		XFS_DA_LOGRANGE(node, btree, tmp + sizeof(*btree)));
	be16_add_cpu(&node->hdr.count, 1);
	xfs_trans_log_buf(state->args->trans, oldblk->bp,
		XFS_DA_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

	/*
	 * Copy the last hash value from the oldblk to propagate upwards.
	 */
	oldblk->hashval = be32_to_cpu(node->btree[be16_to_cpu(node->hdr.count)-1 ].hashval);
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

/*
 * Deallocate an empty leaf node, remove it from its parent,
 * possibly deallocating that block, etc...
 */
int
xfs_da_join(xfs_da_state_t *state)
{
	xfs_da_state_blk_t *drop_blk, *save_blk;
	int action, error;

	trace_xfs_da_join(state->args);

	action = 0;
	drop_blk = &state->path.blk[ state->path.active-1 ];
	save_blk = &state->altpath.blk[ state->path.active-1 ];
	ASSERT(state->path.blk[0].magic == XFS_DA_NODE_MAGIC);
	ASSERT(drop_blk->magic == XFS_ATTR_LEAF_MAGIC ||
	       drop_blk->magic == XFS_DIR2_LEAFN_MAGIC);

	/*
	 * Walk back up the tree joining/deallocating as necessary.
	 * When we stop dropping blocks, break out.
	 */
	for (  ; state->path.active >= 2; drop_blk--, save_blk--,
		 state->path.active--) {
		/*
		 * See if we can combine the block with a neighbor.
		 *   (action == 0) => no options, just leave
		 *   (action == 1) => coalesce, then unlink
		 *   (action == 2) => block empty, unlink it
		 */
		switch (drop_blk->magic) {
		case XFS_ATTR_LEAF_MAGIC:
			error = xfs_attr_leaf_toosmall(state, &action);
			if (error)
				return(error);
			if (action == 0)
				return(0);
			xfs_attr_leaf_unbalance(state, drop_blk, save_blk);
			break;
		case XFS_DIR2_LEAFN_MAGIC:
			error = xfs_dir2_leafn_toosmall(state, &action);
			if (error)
				return error;
			if (action == 0)
				return 0;
			xfs_dir2_leafn_unbalance(state, drop_blk, save_blk);
			break;
		case XFS_DA_NODE_MAGIC:
			/*
			 * Remove the offending node, fixup hashvals,
			 * check for a toosmall neighbor.
			 */
			xfs_da_node_remove(state, drop_blk);
			xfs_da_fixhashpath(state, &state->path);
			error = xfs_da_node_toosmall(state, &action);
			if (error)
				return(error);
			if (action == 0)
				return 0;
			xfs_da_node_unbalance(state, drop_blk, save_blk);
			break;
		}
		xfs_da_fixhashpath(state, &state->altpath);
		error = xfs_da_blk_unlink(state, drop_blk, save_blk);
		xfs_da_state_kill_altpath(state);
		if (error)
			return(error);
		error = xfs_da_shrink_inode(state->args, drop_blk->blkno,
							 drop_blk->bp);
		drop_blk->bp = NULL;
		if (error)
			return(error);
	}
	/*
	 * We joined all the way to the top.  If it turns out that
	 * we only have one entry in the root, make the child block
	 * the new root.
	 */
	xfs_da_node_remove(state, drop_blk);
	xfs_da_fixhashpath(state, &state->path);
	error = xfs_da_root_join(state, &state->path.blk[0]);
	return(error);
}

#ifdef	DEBUG
static void
xfs_da_blkinfo_onlychild_validate(struct xfs_da_blkinfo *blkinfo, __u16 level)
{
	__be16	magic = blkinfo->magic;

	if (level == 1) {
		ASSERT(magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC) ||
		       magic == cpu_to_be16(XFS_ATTR_LEAF_MAGIC));
	} else
		ASSERT(magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	ASSERT(!blkinfo->forw);
	ASSERT(!blkinfo->back);
}
#else	/* !DEBUG */
#define	xfs_da_blkinfo_onlychild_validate(blkinfo, level)
#endif	/* !DEBUG */

/*
 * We have only one entry in the root.  Copy the only remaining child of
 * the old root to block 0 as the new root node.
 */
STATIC int
xfs_da_root_join(xfs_da_state_t *state, xfs_da_state_blk_t *root_blk)
{
	xfs_da_intnode_t *oldroot;
	xfs_da_args_t *args;
	xfs_dablk_t child;
	struct xfs_buf *bp;
	int error;

	trace_xfs_da_root_join(state->args);

	args = state->args;
	ASSERT(args != NULL);
	ASSERT(root_blk->magic == XFS_DA_NODE_MAGIC);
	oldroot = root_blk->bp->b_addr;
	ASSERT(oldroot->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	ASSERT(!oldroot->hdr.info.forw);
	ASSERT(!oldroot->hdr.info.back);

	/*
	 * If the root has more than one child, then don't do anything.
	 */
	if (be16_to_cpu(oldroot->hdr.count) > 1)
		return(0);

	/*
	 * Read in the (only) child block, then copy those bytes into
	 * the root block's buffer and free the original child block.
	 */
	child = be32_to_cpu(oldroot->btree[0].before);
	ASSERT(child != 0);
	error = xfs_da_read_buf(args->trans, args->dp, child, -1, &bp,
					     args->whichfork);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	xfs_da_blkinfo_onlychild_validate(bp->b_addr,
					be16_to_cpu(oldroot->hdr.level));

	memcpy(root_blk->bp->b_addr, bp->b_addr, state->blocksize);
	xfs_trans_log_buf(args->trans, root_blk->bp, 0, state->blocksize - 1);
	error = xfs_da_shrink_inode(args, child, bp);
	return(error);
}

/*
 * Check a node block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 */
STATIC int
xfs_da_node_toosmall(xfs_da_state_t *state, int *action)
{
	xfs_da_intnode_t *node;
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *info;
	int count, forward, error, retval, i;
	xfs_dablk_t blkno;
	struct xfs_buf *bp;

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	info = blk->bp->b_addr;
	ASSERT(info->magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	node = (xfs_da_intnode_t *)info;
	count = be16_to_cpu(node->hdr.count);
	if (count > (state->node_ents >> 1)) {
		*action = 0;	/* blk over 50%, don't try to join */
		return(0);	/* blk over 50%, don't try to join */
	}

	/*
	 * Check for the degenerate case of the block being empty.
	 * If the block is empty, we'll simply delete it, no need to
	 * coalesce it with a sibling block.  We choose (arbitrarily)
	 * to merge with the forward block unless it is NULL.
	 */
	if (count == 0) {
		/*
		 * Make altpath point to the block we want to keep and
		 * path point to the block we want to drop (this one).
		 */
		forward = (info->forw != 0);
		memcpy(&state->altpath, &state->path, sizeof(state->path));
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error)
			return(error);
		if (retval) {
			*action = 0;
		} else {
			*action = 2;
		}
		return(0);
	}

	/*
	 * Examine each sibling block to see if we can coalesce with
	 * at least 25% free space to spare.  We need to figure out
	 * whether to merge with the forward or the backward block.
	 * We prefer coalescing with the lower numbered sibling so as
	 * to shrink a directory over time.
	 */
	/* start with smaller blk num */
	forward = (be32_to_cpu(info->forw) < be32_to_cpu(info->back));
	for (i = 0; i < 2; forward = !forward, i++) {
		if (forward)
			blkno = be32_to_cpu(info->forw);
		else
			blkno = be32_to_cpu(info->back);
		if (blkno == 0)
			continue;
		error = xfs_da_read_buf(state->args->trans, state->args->dp,
					blkno, -1, &bp, state->args->whichfork);
		if (error)
			return(error);
		ASSERT(bp != NULL);

		node = (xfs_da_intnode_t *)info;
		count  = state->node_ents;
		count -= state->node_ents >> 2;
		count -= be16_to_cpu(node->hdr.count);
		node = bp->b_addr;
		ASSERT(node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
		count -= be16_to_cpu(node->hdr.count);
		xfs_trans_brelse(state->args->trans, bp);
		if (count >= 0)
			break;	/* fits with at least 25% to spare */
	}
	if (i >= 2) {
		*action = 0;
		return(0);
	}

	/*
	 * Make altpath point to the block we want to keep (the lower
	 * numbered block) and path point to the block we want to drop.
	 */
	memcpy(&state->altpath, &state->path, sizeof(state->path));
	if (blkno < blk->blkno) {
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error) {
			return(error);
		}
		if (retval) {
			*action = 0;
			return(0);
		}
	} else {
		error = xfs_da_path_shift(state, &state->path, forward,
						 0, &retval);
		if (error) {
			return(error);
		}
		if (retval) {
			*action = 0;
			return(0);
		}
	}
	*action = 1;
	return(0);
}

/*
 * Walk back up the tree adjusting hash values as necessary,
 * when we stop making changes, return.
 */
void
xfs_da_fixhashpath(xfs_da_state_t *state, xfs_da_state_path_t *path)
{
	xfs_da_state_blk_t *blk;
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	xfs_dahash_t lasthash=0;
	int level, count;

	level = path->active-1;
	blk = &path->blk[ level ];
	switch (blk->magic) {
	case XFS_ATTR_LEAF_MAGIC:
		lasthash = xfs_attr_leaf_lasthash(blk->bp, &count);
		if (count == 0)
			return;
		break;
	case XFS_DIR2_LEAFN_MAGIC:
		lasthash = xfs_dir2_leafn_lasthash(blk->bp, &count);
		if (count == 0)
			return;
		break;
	case XFS_DA_NODE_MAGIC:
		lasthash = xfs_da_node_lasthash(blk->bp, &count);
		if (count == 0)
			return;
		break;
	}
	for (blk--, level--; level >= 0; blk--, level--) {
		node = blk->bp->b_addr;
		ASSERT(node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
		btree = &node->btree[ blk->index ];
		if (be32_to_cpu(btree->hashval) == lasthash)
			break;
		blk->hashval = lasthash;
		btree->hashval = cpu_to_be32(lasthash);
		xfs_trans_log_buf(state->args->trans, blk->bp,
				  XFS_DA_LOGRANGE(node, btree, sizeof(*btree)));

		lasthash = be32_to_cpu(node->btree[be16_to_cpu(node->hdr.count)-1].hashval);
	}
}

/*
 * Remove an entry from an intermediate node.
 */
STATIC void
xfs_da_node_remove(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk)
{
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	int tmp;

	trace_xfs_da_node_remove(state->args);

	node = drop_blk->bp->b_addr;
	ASSERT(drop_blk->index < be16_to_cpu(node->hdr.count));
	ASSERT(drop_blk->index >= 0);

	/*
	 * Copy over the offending entry, or just zero it out.
	 */
	btree = &node->btree[drop_blk->index];
	if (drop_blk->index < (be16_to_cpu(node->hdr.count)-1)) {
		tmp  = be16_to_cpu(node->hdr.count) - drop_blk->index - 1;
		tmp *= (uint)sizeof(xfs_da_node_entry_t);
		memmove(btree, btree + 1, tmp);
		xfs_trans_log_buf(state->args->trans, drop_blk->bp,
		    XFS_DA_LOGRANGE(node, btree, tmp));
		btree = &node->btree[be16_to_cpu(node->hdr.count)-1];
	}
	memset((char *)btree, 0, sizeof(xfs_da_node_entry_t));
	xfs_trans_log_buf(state->args->trans, drop_blk->bp,
	    XFS_DA_LOGRANGE(node, btree, sizeof(*btree)));
	be16_add_cpu(&node->hdr.count, -1);
	xfs_trans_log_buf(state->args->trans, drop_blk->bp,
	    XFS_DA_LOGRANGE(node, &node->hdr, sizeof(node->hdr)));

	/*
	 * Copy the last hash value from the block to propagate upwards.
	 */
	btree--;
	drop_blk->hashval = be32_to_cpu(btree->hashval);
}

/*
 * Unbalance the btree elements between two intermediate nodes,
 * move all Btree elements from one node into another.
 */
STATIC void
xfs_da_node_unbalance(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				     xfs_da_state_blk_t *save_blk)
{
	xfs_da_intnode_t *drop_node, *save_node;
	xfs_da_node_entry_t *btree;
	int tmp;
	xfs_trans_t *tp;

	trace_xfs_da_node_unbalance(state->args);

	drop_node = drop_blk->bp->b_addr;
	save_node = save_blk->bp->b_addr;
	ASSERT(drop_node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	ASSERT(save_node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	tp = state->args->trans;

	/*
	 * If the dying block has lower hashvals, then move all the
	 * elements in the remaining block up to make a hole.
	 */
	if ((be32_to_cpu(drop_node->btree[0].hashval) < be32_to_cpu(save_node->btree[ 0 ].hashval)) ||
	    (be32_to_cpu(drop_node->btree[be16_to_cpu(drop_node->hdr.count)-1].hashval) <
	     be32_to_cpu(save_node->btree[be16_to_cpu(save_node->hdr.count)-1].hashval)))
	{
		btree = &save_node->btree[be16_to_cpu(drop_node->hdr.count)];
		tmp = be16_to_cpu(save_node->hdr.count) * (uint)sizeof(xfs_da_node_entry_t);
		memmove(btree, &save_node->btree[0], tmp);
		btree = &save_node->btree[0];
		xfs_trans_log_buf(tp, save_blk->bp,
			XFS_DA_LOGRANGE(save_node, btree,
				(be16_to_cpu(save_node->hdr.count) + be16_to_cpu(drop_node->hdr.count)) *
				sizeof(xfs_da_node_entry_t)));
	} else {
		btree = &save_node->btree[be16_to_cpu(save_node->hdr.count)];
		xfs_trans_log_buf(tp, save_blk->bp,
			XFS_DA_LOGRANGE(save_node, btree,
				be16_to_cpu(drop_node->hdr.count) *
				sizeof(xfs_da_node_entry_t)));
	}

	/*
	 * Move all the B-tree elements from drop_blk to save_blk.
	 */
	tmp = be16_to_cpu(drop_node->hdr.count) * (uint)sizeof(xfs_da_node_entry_t);
	memcpy(btree, &drop_node->btree[0], tmp);
	be16_add_cpu(&save_node->hdr.count, be16_to_cpu(drop_node->hdr.count));

	xfs_trans_log_buf(tp, save_blk->bp,
		XFS_DA_LOGRANGE(save_node, &save_node->hdr,
			sizeof(save_node->hdr)));

	/*
	 * Save the last hashval in the remaining block for upward propagation.
	 */
	save_blk->hashval = be32_to_cpu(save_node->btree[be16_to_cpu(save_node->hdr.count)-1].hashval);
}

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Walk down the Btree looking for a particular filename, filling
 * in the state structure as we go.
 *
 * We will set the state structure to point to each of the elements
 * in each of the nodes where either the hashval is or should be.
 *
 * We support duplicate hashval's so for each entry in the current
 * node that could contain the desired hashval, descend.  This is a
 * pruned depth-first tree search.
 */
int							/* error */
xfs_da_node_lookup_int(xfs_da_state_t *state, int *result)
{
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *curr;
	xfs_da_intnode_t *node;
	xfs_da_node_entry_t *btree;
	xfs_dablk_t blkno;
	int probe, span, max, error, retval;
	xfs_dahash_t hashval, btreehashval;
	xfs_da_args_t *args;

	args = state->args;

	/*
	 * Descend thru the B-tree searching each level for the right
	 * node to use, until the right hashval is found.
	 */
	blkno = (args->whichfork == XFS_DATA_FORK)? state->mp->m_dirleafblk : 0;
	for (blk = &state->path.blk[0], state->path.active = 1;
			 state->path.active <= XFS_DA_NODE_MAXDEPTH;
			 blk++, state->path.active++) {
		/*
		 * Read the next node down in the tree.
		 */
		blk->blkno = blkno;
		error = xfs_da_read_buf(args->trans, args->dp, blkno,
					-1, &blk->bp, args->whichfork);
		if (error) {
			blk->blkno = 0;
			state->path.active--;
			return(error);
		}
		curr = blk->bp->b_addr;
		blk->magic = be16_to_cpu(curr->magic);
		ASSERT(blk->magic == XFS_DA_NODE_MAGIC ||
		       blk->magic == XFS_DIR2_LEAFN_MAGIC ||
		       blk->magic == XFS_ATTR_LEAF_MAGIC);

		/*
		 * Search an intermediate node for a match.
		 */
		if (blk->magic == XFS_DA_NODE_MAGIC) {
			node = blk->bp->b_addr;
			max = be16_to_cpu(node->hdr.count);
			blk->hashval = be32_to_cpu(node->btree[max-1].hashval);

			/*
			 * Binary search.  (note: small blocks will skip loop)
			 */
			probe = span = max / 2;
			hashval = args->hashval;
			for (btree = &node->btree[probe]; span > 4;
				   btree = &node->btree[probe]) {
				span /= 2;
				btreehashval = be32_to_cpu(btree->hashval);
				if (btreehashval < hashval)
					probe += span;
				else if (btreehashval > hashval)
					probe -= span;
				else
					break;
			}
			ASSERT((probe >= 0) && (probe < max));
			ASSERT((span <= 4) || (be32_to_cpu(btree->hashval) == hashval));

			/*
			 * Since we may have duplicate hashval's, find the first
			 * matching hashval in the node.
			 */
			while ((probe > 0) && (be32_to_cpu(btree->hashval) >= hashval)) {
				btree--;
				probe--;
			}
			while ((probe < max) && (be32_to_cpu(btree->hashval) < hashval)) {
				btree++;
				probe++;
			}

			/*
			 * Pick the right block to descend on.
			 */
			if (probe == max) {
				blk->index = max-1;
				blkno = be32_to_cpu(node->btree[max-1].before);
			} else {
				blk->index = probe;
				blkno = be32_to_cpu(btree->before);
			}
		} else if (blk->magic == XFS_ATTR_LEAF_MAGIC) {
			blk->hashval = xfs_attr_leaf_lasthash(blk->bp, NULL);
			break;
		} else if (blk->magic == XFS_DIR2_LEAFN_MAGIC) {
			blk->hashval = xfs_dir2_leafn_lasthash(blk->bp, NULL);
			break;
		}
	}

	/*
	 * A leaf block that ends in the hashval that we are interested in
	 * (final hashval == search hashval) means that the next block may
	 * contain more entries with the same hashval, shift upward to the
	 * next leaf and keep searching.
	 */
	for (;;) {
		if (blk->magic == XFS_DIR2_LEAFN_MAGIC) {
			retval = xfs_dir2_leafn_lookup_int(blk->bp, args,
							&blk->index, state);
		} else if (blk->magic == XFS_ATTR_LEAF_MAGIC) {
			retval = xfs_attr_leaf_lookup_int(blk->bp, args);
			blk->index = args->index;
			args->blkno = blk->blkno;
		} else {
			ASSERT(0);
			return XFS_ERROR(EFSCORRUPTED);
		}
		if (((retval == ENOENT) || (retval == ENOATTR)) &&
		    (blk->hashval == args->hashval)) {
			error = xfs_da_path_shift(state, &state->path, 1, 1,
							 &retval);
			if (error)
				return(error);
			if (retval == 0) {
				continue;
			} else if (blk->magic == XFS_ATTR_LEAF_MAGIC) {
				/* path_shift() gives ENOENT */
				retval = XFS_ERROR(ENOATTR);
			}
		}
		break;
	}
	*result = retval;
	return(0);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Link a new block into a doubly linked list of blocks (of whatever type).
 */
int							/* error */
xfs_da_blk_link(xfs_da_state_t *state, xfs_da_state_blk_t *old_blk,
			       xfs_da_state_blk_t *new_blk)
{
	xfs_da_blkinfo_t *old_info, *new_info, *tmp_info;
	xfs_da_args_t *args;
	int before=0, error;
	struct xfs_buf *bp;

	/*
	 * Set up environment.
	 */
	args = state->args;
	ASSERT(args != NULL);
	old_info = old_blk->bp->b_addr;
	new_info = new_blk->bp->b_addr;
	ASSERT(old_blk->magic == XFS_DA_NODE_MAGIC ||
	       old_blk->magic == XFS_DIR2_LEAFN_MAGIC ||
	       old_blk->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(old_blk->magic == be16_to_cpu(old_info->magic));
	ASSERT(new_blk->magic == be16_to_cpu(new_info->magic));
	ASSERT(old_blk->magic == new_blk->magic);

	switch (old_blk->magic) {
	case XFS_ATTR_LEAF_MAGIC:
		before = xfs_attr_leaf_order(old_blk->bp, new_blk->bp);
		break;
	case XFS_DIR2_LEAFN_MAGIC:
		before = xfs_dir2_leafn_order(old_blk->bp, new_blk->bp);
		break;
	case XFS_DA_NODE_MAGIC:
		before = xfs_da_node_order(old_blk->bp, new_blk->bp);
		break;
	}

	/*
	 * Link blocks in appropriate order.
	 */
	if (before) {
		/*
		 * Link new block in before existing block.
		 */
		trace_xfs_da_link_before(args);
		new_info->forw = cpu_to_be32(old_blk->blkno);
		new_info->back = old_info->back;
		if (old_info->back) {
			error = xfs_da_read_buf(args->trans, args->dp,
						be32_to_cpu(old_info->back),
						-1, &bp, args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = bp->b_addr;
			ASSERT(be16_to_cpu(tmp_info->magic) == be16_to_cpu(old_info->magic));
			ASSERT(be32_to_cpu(tmp_info->forw) == old_blk->blkno);
			tmp_info->forw = cpu_to_be32(new_blk->blkno);
			xfs_trans_log_buf(args->trans, bp, 0, sizeof(*tmp_info)-1);
		}
		old_info->back = cpu_to_be32(new_blk->blkno);
	} else {
		/*
		 * Link new block in after existing block.
		 */
		trace_xfs_da_link_after(args);
		new_info->forw = old_info->forw;
		new_info->back = cpu_to_be32(old_blk->blkno);
		if (old_info->forw) {
			error = xfs_da_read_buf(args->trans, args->dp,
						be32_to_cpu(old_info->forw),
						-1, &bp, args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = bp->b_addr;
			ASSERT(tmp_info->magic == old_info->magic);
			ASSERT(be32_to_cpu(tmp_info->back) == old_blk->blkno);
			tmp_info->back = cpu_to_be32(new_blk->blkno);
			xfs_trans_log_buf(args->trans, bp, 0, sizeof(*tmp_info)-1);
		}
		old_info->forw = cpu_to_be32(new_blk->blkno);
	}

	xfs_trans_log_buf(args->trans, old_blk->bp, 0, sizeof(*tmp_info) - 1);
	xfs_trans_log_buf(args->trans, new_blk->bp, 0, sizeof(*tmp_info) - 1);
	return(0);
}

/*
 * Compare two intermediate nodes for "order".
 */
STATIC int
xfs_da_node_order(
	struct xfs_buf	*node1_bp,
	struct xfs_buf	*node2_bp)
{
	xfs_da_intnode_t *node1, *node2;

	node1 = node1_bp->b_addr;
	node2 = node2_bp->b_addr;
	ASSERT(node1->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC) &&
	       node2->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	if ((be16_to_cpu(node1->hdr.count) > 0) && (be16_to_cpu(node2->hdr.count) > 0) &&
	    ((be32_to_cpu(node2->btree[0].hashval) <
	      be32_to_cpu(node1->btree[0].hashval)) ||
	     (be32_to_cpu(node2->btree[be16_to_cpu(node2->hdr.count)-1].hashval) <
	      be32_to_cpu(node1->btree[be16_to_cpu(node1->hdr.count)-1].hashval)))) {
		return(1);
	}
	return(0);
}

/*
 * Pick up the last hashvalue from an intermediate node.
 */
STATIC uint
xfs_da_node_lasthash(
	struct xfs_buf	*bp,
	int		*count)
{
	xfs_da_intnode_t *node;

	node = bp->b_addr;
	ASSERT(node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
	if (count)
		*count = be16_to_cpu(node->hdr.count);
	if (!node->hdr.count)
		return(0);
	return be32_to_cpu(node->btree[be16_to_cpu(node->hdr.count)-1].hashval);
}

/*
 * Unlink a block from a doubly linked list of blocks.
 */
STATIC int						/* error */
xfs_da_blk_unlink(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				 xfs_da_state_blk_t *save_blk)
{
	xfs_da_blkinfo_t *drop_info, *save_info, *tmp_info;
	xfs_da_args_t *args;
	struct xfs_buf *bp;
	int error;

	/*
	 * Set up environment.
	 */
	args = state->args;
	ASSERT(args != NULL);
	save_info = save_blk->bp->b_addr;
	drop_info = drop_blk->bp->b_addr;
	ASSERT(save_blk->magic == XFS_DA_NODE_MAGIC ||
	       save_blk->magic == XFS_DIR2_LEAFN_MAGIC ||
	       save_blk->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(save_blk->magic == be16_to_cpu(save_info->magic));
	ASSERT(drop_blk->magic == be16_to_cpu(drop_info->magic));
	ASSERT(save_blk->magic == drop_blk->magic);
	ASSERT((be32_to_cpu(save_info->forw) == drop_blk->blkno) ||
	       (be32_to_cpu(save_info->back) == drop_blk->blkno));
	ASSERT((be32_to_cpu(drop_info->forw) == save_blk->blkno) ||
	       (be32_to_cpu(drop_info->back) == save_blk->blkno));

	/*
	 * Unlink the leaf block from the doubly linked chain of leaves.
	 */
	if (be32_to_cpu(save_info->back) == drop_blk->blkno) {
		trace_xfs_da_unlink_back(args);
		save_info->back = drop_info->back;
		if (drop_info->back) {
			error = xfs_da_read_buf(args->trans, args->dp,
						be32_to_cpu(drop_info->back),
						-1, &bp, args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = bp->b_addr;
			ASSERT(tmp_info->magic == save_info->magic);
			ASSERT(be32_to_cpu(tmp_info->forw) == drop_blk->blkno);
			tmp_info->forw = cpu_to_be32(save_blk->blkno);
			xfs_trans_log_buf(args->trans, bp, 0,
						    sizeof(*tmp_info) - 1);
		}
	} else {
		trace_xfs_da_unlink_forward(args);
		save_info->forw = drop_info->forw;
		if (drop_info->forw) {
			error = xfs_da_read_buf(args->trans, args->dp,
						be32_to_cpu(drop_info->forw),
						-1, &bp, args->whichfork);
			if (error)
				return(error);
			ASSERT(bp != NULL);
			tmp_info = bp->b_addr;
			ASSERT(tmp_info->magic == save_info->magic);
			ASSERT(be32_to_cpu(tmp_info->back) == drop_blk->blkno);
			tmp_info->back = cpu_to_be32(save_blk->blkno);
			xfs_trans_log_buf(args->trans, bp, 0,
						    sizeof(*tmp_info) - 1);
		}
	}

	xfs_trans_log_buf(args->trans, save_blk->bp, 0, sizeof(*save_info) - 1);
	return(0);
}

/*
 * Move a path "forward" or "!forward" one block at the current level.
 *
 * This routine will adjust a "path" to point to the next block
 * "forward" (higher hashvalues) or "!forward" (lower hashvals) in the
 * Btree, including updating pointers to the intermediate nodes between
 * the new bottom and the root.
 */
int							/* error */
xfs_da_path_shift(xfs_da_state_t *state, xfs_da_state_path_t *path,
				 int forward, int release, int *result)
{
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *info;
	xfs_da_intnode_t *node;
	xfs_da_args_t *args;
	xfs_dablk_t blkno=0;
	int level, error;

	/*
	 * Roll up the Btree looking for the first block where our
	 * current index is not at the edge of the block.  Note that
	 * we skip the bottom layer because we want the sibling block.
	 */
	args = state->args;
	ASSERT(args != NULL);
	ASSERT(path != NULL);
	ASSERT((path->active > 0) && (path->active < XFS_DA_NODE_MAXDEPTH));
	level = (path->active-1) - 1;	/* skip bottom layer in path */
	for (blk = &path->blk[level]; level >= 0; blk--, level--) {
		ASSERT(blk->bp != NULL);
		node = blk->bp->b_addr;
		ASSERT(node->hdr.info.magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
		if (forward && (blk->index < be16_to_cpu(node->hdr.count)-1)) {
			blk->index++;
			blkno = be32_to_cpu(node->btree[blk->index].before);
			break;
		} else if (!forward && (blk->index > 0)) {
			blk->index--;
			blkno = be32_to_cpu(node->btree[blk->index].before);
			break;
		}
	}
	if (level < 0) {
		*result = XFS_ERROR(ENOENT);	/* we're out of our tree */
		ASSERT(args->op_flags & XFS_DA_OP_OKNOENT);
		return(0);
	}

	/*
	 * Roll down the edge of the subtree until we reach the
	 * same depth we were at originally.
	 */
	for (blk++, level++; level < path->active; blk++, level++) {
		/*
		 * Release the old block.
		 * (if it's dirty, trans won't actually let go)
		 */
		if (release)
			xfs_trans_brelse(args->trans, blk->bp);

		/*
		 * Read the next child block.
		 */
		blk->blkno = blkno;
		error = xfs_da_read_buf(args->trans, args->dp, blkno, -1,
						     &blk->bp, args->whichfork);
		if (error)
			return(error);
		ASSERT(blk->bp != NULL);
		info = blk->bp->b_addr;
		ASSERT(info->magic == cpu_to_be16(XFS_DA_NODE_MAGIC) ||
		       info->magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC) ||
		       info->magic == cpu_to_be16(XFS_ATTR_LEAF_MAGIC));
		blk->magic = be16_to_cpu(info->magic);
		if (blk->magic == XFS_DA_NODE_MAGIC) {
			node = (xfs_da_intnode_t *)info;
			blk->hashval = be32_to_cpu(node->btree[be16_to_cpu(node->hdr.count)-1].hashval);
			if (forward)
				blk->index = 0;
			else
				blk->index = be16_to_cpu(node->hdr.count)-1;
			blkno = be32_to_cpu(node->btree[blk->index].before);
		} else {
			ASSERT(level == path->active-1);
			blk->index = 0;
			switch(blk->magic) {
			case XFS_ATTR_LEAF_MAGIC:
				blk->hashval = xfs_attr_leaf_lasthash(blk->bp,
								      NULL);
				break;
			case XFS_DIR2_LEAFN_MAGIC:
				blk->hashval = xfs_dir2_leafn_lasthash(blk->bp,
								       NULL);
				break;
			default:
				ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC ||
				       blk->magic == XFS_DIR2_LEAFN_MAGIC);
				break;
			}
		}
	}
	*result = 0;
	return(0);
}


/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Implement a simple hash on a character string.
 * Rotate the hash value by 7 bits, then XOR each character in.
 * This is implemented with some source-level loop unrolling.
 */
xfs_dahash_t
xfs_da_hashname(const __uint8_t *name, int namelen)
{
	xfs_dahash_t hash;

	/*
	 * Do four characters at a time as long as we can.
	 */
	for (hash = 0; namelen >= 4; namelen -= 4, name += 4)
		hash = (name[0] << 21) ^ (name[1] << 14) ^ (name[2] << 7) ^
		       (name[3] << 0) ^ rol32(hash, 7 * 4);

	/*
	 * Now do the rest of the characters.
	 */
	switch (namelen) {
	case 3:
		return (name[0] << 14) ^ (name[1] << 7) ^ (name[2] << 0) ^
		       rol32(hash, 7 * 3);
	case 2:
		return (name[0] << 7) ^ (name[1] << 0) ^ rol32(hash, 7 * 2);
	case 1:
		return (name[0] << 0) ^ rol32(hash, 7 * 1);
	default: /* case 0: */
		return hash;
	}
}

enum xfs_dacmp
xfs_da_compname(
	struct xfs_da_args *args,
	const unsigned char *name,
	int		len)
{
	return (args->namelen == len && memcmp(args->name, name, len) == 0) ?
					XFS_CMP_EXACT : XFS_CMP_DIFFERENT;
}

static xfs_dahash_t
xfs_default_hashname(
	struct xfs_name	*name)
{
	return xfs_da_hashname(name->name, name->len);
}

const struct xfs_nameops xfs_default_nameops = {
	.hashname	= xfs_default_hashname,
	.compname	= xfs_da_compname
};

int
xfs_da_grow_inode_int(
	struct xfs_da_args	*args,
	xfs_fileoff_t		*bno,
	int			count)
{
	struct xfs_trans	*tp = args->trans;
	struct xfs_inode	*dp = args->dp;
	int			w = args->whichfork;
	xfs_drfsbno_t		nblks = dp->i_d.di_nblocks;
	struct xfs_bmbt_irec	map, *mapp;
	int			nmap, error, got, i, mapi;

	/*
	 * Find a spot in the file space to put the new block.
	 */
	error = xfs_bmap_first_unused(tp, dp, count, bno, w);
	if (error)
		return error;

	/*
	 * Try mapping it in one filesystem block.
	 */
	nmap = 1;
	ASSERT(args->firstblock != NULL);
	error = xfs_bmapi_write(tp, dp, *bno, count,
			xfs_bmapi_aflag(w)|XFS_BMAPI_METADATA|XFS_BMAPI_CONTIG,
			args->firstblock, args->total, &map, &nmap,
			args->flist);
	if (error)
		return error;

	ASSERT(nmap <= 1);
	if (nmap == 1) {
		mapp = &map;
		mapi = 1;
	} else if (nmap == 0 && count > 1) {
		xfs_fileoff_t		b;
		int			c;

		/*
		 * If we didn't get it and the block might work if fragmented,
		 * try without the CONTIG flag.  Loop until we get it all.
		 */
		mapp = kmem_alloc(sizeof(*mapp) * count, KM_SLEEP);
		for (b = *bno, mapi = 0; b < *bno + count; ) {
			nmap = MIN(XFS_BMAP_MAX_NMAP, count);
			c = (int)(*bno + count - b);
			error = xfs_bmapi_write(tp, dp, b, c,
					xfs_bmapi_aflag(w)|XFS_BMAPI_METADATA,
					args->firstblock, args->total,
					&mapp[mapi], &nmap, args->flist);
			if (error)
				goto out_free_map;
			if (nmap < 1)
				break;
			mapi += nmap;
			b = mapp[mapi - 1].br_startoff +
			    mapp[mapi - 1].br_blockcount;
		}
	} else {
		mapi = 0;
		mapp = NULL;
	}

	/*
	 * Count the blocks we got, make sure it matches the total.
	 */
	for (i = 0, got = 0; i < mapi; i++)
		got += mapp[i].br_blockcount;
	if (got != count || mapp[0].br_startoff != *bno ||
	    mapp[mapi - 1].br_startoff + mapp[mapi - 1].br_blockcount !=
	    *bno + count) {
		error = XFS_ERROR(ENOSPC);
		goto out_free_map;
	}

	/* account for newly allocated blocks in reserved blocks total */
	args->total -= dp->i_d.di_nblocks - nblks;

out_free_map:
	if (mapp != &map)
		kmem_free(mapp);
	return error;
}

/*
 * Add a block to the btree ahead of the file.
 * Return the new block number to the caller.
 */
int
xfs_da_grow_inode(
	struct xfs_da_args	*args,
	xfs_dablk_t		*new_blkno)
{
	xfs_fileoff_t		bno;
	int			count;
	int			error;

	trace_xfs_da_grow_inode(args);

	if (args->whichfork == XFS_DATA_FORK) {
		bno = args->dp->i_mount->m_dirleafblk;
		count = args->dp->i_mount->m_dirblkfsbs;
	} else {
		bno = 0;
		count = 1;
	}

	error = xfs_da_grow_inode_int(args, &bno, count);
	if (!error)
		*new_blkno = (xfs_dablk_t)bno;
	return error;
}

/*
 * Ick.  We need to always be able to remove a btree block, even
 * if there's no space reservation because the filesystem is full.
 * This is called if xfs_bunmapi on a btree block fails due to ENOSPC.
 * It swaps the target block with the last block in the file.  The
 * last block in the file can always be removed since it can't cause
 * a bmap btree split to do that.
 */
STATIC int
xfs_da_swap_lastblock(
	xfs_da_args_t	*args,
	xfs_dablk_t	*dead_blknop,
	struct xfs_buf	**dead_bufp)
{
	xfs_dablk_t dead_blkno, last_blkno, sib_blkno, par_blkno;
	struct xfs_buf *dead_buf, *last_buf, *sib_buf, *par_buf;
	xfs_fileoff_t lastoff;
	xfs_inode_t *ip;
	xfs_trans_t *tp;
	xfs_mount_t *mp;
	int error, w, entno, level, dead_level;
	xfs_da_blkinfo_t *dead_info, *sib_info;
	xfs_da_intnode_t *par_node, *dead_node;
	xfs_dir2_leaf_t *dead_leaf2;
	xfs_dahash_t dead_hash;

	trace_xfs_da_swap_lastblock(args);

	dead_buf = *dead_bufp;
	dead_blkno = *dead_blknop;
	tp = args->trans;
	ip = args->dp;
	w = args->whichfork;
	ASSERT(w == XFS_DATA_FORK);
	mp = ip->i_mount;
	lastoff = mp->m_dirfreeblk;
	error = xfs_bmap_last_before(tp, ip, &lastoff, w);
	if (error)
		return error;
	if (unlikely(lastoff == 0)) {
		XFS_ERROR_REPORT("xfs_da_swap_lastblock(1)", XFS_ERRLEVEL_LOW,
				 mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	/*
	 * Read the last block in the btree space.
	 */
	last_blkno = (xfs_dablk_t)lastoff - mp->m_dirblkfsbs;
	if ((error = xfs_da_read_buf(tp, ip, last_blkno, -1, &last_buf, w)))
		return error;
	/*
	 * Copy the last block into the dead buffer and log it.
	 */
	memcpy(dead_buf->b_addr, last_buf->b_addr, mp->m_dirblksize);
	xfs_trans_log_buf(tp, dead_buf, 0, mp->m_dirblksize - 1);
	dead_info = dead_buf->b_addr;
	/*
	 * Get values from the moved block.
	 */
	if (dead_info->magic == cpu_to_be16(XFS_DIR2_LEAFN_MAGIC)) {
		dead_leaf2 = (xfs_dir2_leaf_t *)dead_info;
		dead_level = 0;
		dead_hash = be32_to_cpu(dead_leaf2->ents[be16_to_cpu(dead_leaf2->hdr.count) - 1].hashval);
	} else {
		ASSERT(dead_info->magic == cpu_to_be16(XFS_DA_NODE_MAGIC));
		dead_node = (xfs_da_intnode_t *)dead_info;
		dead_level = be16_to_cpu(dead_node->hdr.level);
		dead_hash = be32_to_cpu(dead_node->btree[be16_to_cpu(dead_node->hdr.count) - 1].hashval);
	}
	sib_buf = par_buf = NULL;
	/*
	 * If the moved block has a left sibling, fix up the pointers.
	 */
	if ((sib_blkno = be32_to_cpu(dead_info->back))) {
		if ((error = xfs_da_read_buf(tp, ip, sib_blkno, -1, &sib_buf, w)))
			goto done;
		sib_info = sib_buf->b_addr;
		if (unlikely(
		    be32_to_cpu(sib_info->forw) != last_blkno ||
		    sib_info->magic != dead_info->magic)) {
			XFS_ERROR_REPORT("xfs_da_swap_lastblock(2)",
					 XFS_ERRLEVEL_LOW, mp);
			error = XFS_ERROR(EFSCORRUPTED);
			goto done;
		}
		sib_info->forw = cpu_to_be32(dead_blkno);
		xfs_trans_log_buf(tp, sib_buf,
			XFS_DA_LOGRANGE(sib_info, &sib_info->forw,
					sizeof(sib_info->forw)));
		sib_buf = NULL;
	}
	/*
	 * If the moved block has a right sibling, fix up the pointers.
	 */
	if ((sib_blkno = be32_to_cpu(dead_info->forw))) {
		if ((error = xfs_da_read_buf(tp, ip, sib_blkno, -1, &sib_buf, w)))
			goto done;
		sib_info = sib_buf->b_addr;
		if (unlikely(
		       be32_to_cpu(sib_info->back) != last_blkno ||
		       sib_info->magic != dead_info->magic)) {
			XFS_ERROR_REPORT("xfs_da_swap_lastblock(3)",
					 XFS_ERRLEVEL_LOW, mp);
			error = XFS_ERROR(EFSCORRUPTED);
			goto done;
		}
		sib_info->back = cpu_to_be32(dead_blkno);
		xfs_trans_log_buf(tp, sib_buf,
			XFS_DA_LOGRANGE(sib_info, &sib_info->back,
					sizeof(sib_info->back)));
		sib_buf = NULL;
	}
	par_blkno = mp->m_dirleafblk;
	level = -1;
	/*
	 * Walk down the tree looking for the parent of the moved block.
	 */
	for (;;) {
		if ((error = xfs_da_read_buf(tp, ip, par_blkno, -1, &par_buf, w)))
			goto done;
		par_node = par_buf->b_addr;
		if (unlikely(par_node->hdr.info.magic !=
		    cpu_to_be16(XFS_DA_NODE_MAGIC) ||
		    (level >= 0 && level != be16_to_cpu(par_node->hdr.level) + 1))) {
			XFS_ERROR_REPORT("xfs_da_swap_lastblock(4)",
					 XFS_ERRLEVEL_LOW, mp);
			error = XFS_ERROR(EFSCORRUPTED);
			goto done;
		}
		level = be16_to_cpu(par_node->hdr.level);
		for (entno = 0;
		     entno < be16_to_cpu(par_node->hdr.count) &&
		     be32_to_cpu(par_node->btree[entno].hashval) < dead_hash;
		     entno++)
			continue;
		if (unlikely(entno == be16_to_cpu(par_node->hdr.count))) {
			XFS_ERROR_REPORT("xfs_da_swap_lastblock(5)",
					 XFS_ERRLEVEL_LOW, mp);
			error = XFS_ERROR(EFSCORRUPTED);
			goto done;
		}
		par_blkno = be32_to_cpu(par_node->btree[entno].before);
		if (level == dead_level + 1)
			break;
		xfs_trans_brelse(tp, par_buf);
		par_buf = NULL;
	}
	/*
	 * We're in the right parent block.
	 * Look for the right entry.
	 */
	for (;;) {
		for (;
		     entno < be16_to_cpu(par_node->hdr.count) &&
		     be32_to_cpu(par_node->btree[entno].before) != last_blkno;
		     entno++)
			continue;
		if (entno < be16_to_cpu(par_node->hdr.count))
			break;
		par_blkno = be32_to_cpu(par_node->hdr.info.forw);
		xfs_trans_brelse(tp, par_buf);
		par_buf = NULL;
		if (unlikely(par_blkno == 0)) {
			XFS_ERROR_REPORT("xfs_da_swap_lastblock(6)",
					 XFS_ERRLEVEL_LOW, mp);
			error = XFS_ERROR(EFSCORRUPTED);
			goto done;
		}
		if ((error = xfs_da_read_buf(tp, ip, par_blkno, -1, &par_buf, w)))
			goto done;
		par_node = par_buf->b_addr;
		if (unlikely(
		    be16_to_cpu(par_node->hdr.level) != level ||
		    par_node->hdr.info.magic != cpu_to_be16(XFS_DA_NODE_MAGIC))) {
			XFS_ERROR_REPORT("xfs_da_swap_lastblock(7)",
					 XFS_ERRLEVEL_LOW, mp);
			error = XFS_ERROR(EFSCORRUPTED);
			goto done;
		}
		entno = 0;
	}
	/*
	 * Update the parent entry pointing to the moved block.
	 */
	par_node->btree[entno].before = cpu_to_be32(dead_blkno);
	xfs_trans_log_buf(tp, par_buf,
		XFS_DA_LOGRANGE(par_node, &par_node->btree[entno].before,
				sizeof(par_node->btree[entno].before)));
	*dead_blknop = last_blkno;
	*dead_bufp = last_buf;
	return 0;
done:
	if (par_buf)
		xfs_trans_brelse(tp, par_buf);
	if (sib_buf)
		xfs_trans_brelse(tp, sib_buf);
	xfs_trans_brelse(tp, last_buf);
	return error;
}

/*
 * Remove a btree block from a directory or attribute.
 */
int
xfs_da_shrink_inode(
	xfs_da_args_t	*args,
	xfs_dablk_t	dead_blkno,
	struct xfs_buf	*dead_buf)
{
	xfs_inode_t *dp;
	int done, error, w, count;
	xfs_trans_t *tp;
	xfs_mount_t *mp;

	trace_xfs_da_shrink_inode(args);

	dp = args->dp;
	w = args->whichfork;
	tp = args->trans;
	mp = dp->i_mount;
	if (w == XFS_DATA_FORK)
		count = mp->m_dirblkfsbs;
	else
		count = 1;
	for (;;) {
		/*
		 * Remove extents.  If we get ENOSPC for a dir we have to move
		 * the last block to the place we want to kill.
		 */
		if ((error = xfs_bunmapi(tp, dp, dead_blkno, count,
				xfs_bmapi_aflag(w)|XFS_BMAPI_METADATA,
				0, args->firstblock, args->flist,
				&done)) == ENOSPC) {
			if (w != XFS_DATA_FORK)
				break;
			if ((error = xfs_da_swap_lastblock(args, &dead_blkno,
					&dead_buf)))
				break;
		} else {
			break;
		}
	}
	xfs_trans_binval(tp, dead_buf);
	return error;
}

/*
 * See if the mapping(s) for this btree block are valid, i.e.
 * don't contain holes, are logically contiguous, and cover the whole range.
 */
STATIC int
xfs_da_map_covers_blocks(
	int		nmap,
	xfs_bmbt_irec_t	*mapp,
	xfs_dablk_t	bno,
	int		count)
{
	int		i;
	xfs_fileoff_t	off;

	for (i = 0, off = bno; i < nmap; i++) {
		if (mapp[i].br_startblock == HOLESTARTBLOCK ||
		    mapp[i].br_startblock == DELAYSTARTBLOCK) {
			return 0;
		}
		if (off != mapp[i].br_startoff) {
			return 0;
		}
		off += mapp[i].br_blockcount;
	}
	return off == bno + count;
}

/*
 * Convert a struct xfs_bmbt_irec to a struct xfs_buf_map.
 *
 * For the single map case, it is assumed that the caller has provided a pointer
 * to a valid xfs_buf_map.  For the multiple map case, this function will
 * allocate the xfs_buf_map to hold all the maps and replace the caller's single
 * map pointer with the allocated map.
 */
static int
xfs_buf_map_from_irec(
	struct xfs_mount	*mp,
	struct xfs_buf_map	**mapp,
	unsigned int		*nmaps,
	struct xfs_bmbt_irec	*irecs,
	unsigned int		nirecs)
{
	struct xfs_buf_map	*map;
	int			i;

	ASSERT(*nmaps == 1);
	ASSERT(nirecs >= 1);

	if (nirecs > 1) {
		map = kmem_zalloc(nirecs * sizeof(struct xfs_buf_map), KM_SLEEP);
		if (!map)
			return ENOMEM;
		*mapp = map;
	}

	*nmaps = nirecs;
	map = *mapp;
	for (i = 0; i < *nmaps; i++) {
		ASSERT(irecs[i].br_startblock != DELAYSTARTBLOCK &&
		       irecs[i].br_startblock != HOLESTARTBLOCK);
		map[i].bm_bn = XFS_FSB_TO_DADDR(mp, irecs[i].br_startblock);
		map[i].bm_len = XFS_FSB_TO_BB(mp, irecs[i].br_blockcount);
	}
	return 0;
}

/*
 * Map the block we are given ready for reading. There are three possible return
 * values:
 *	-1 - will be returned if we land in a hole and mappedbno == -2 so the
 *	     caller knows not to execute a subsequent read.
 *	 0 - if we mapped the block successfully
 *	>0 - positive error number if there was an error.
 */
static int
xfs_dabuf_map(
	struct xfs_trans	*trans,
	struct xfs_inode	*dp,
	xfs_dablk_t		bno,
	xfs_daddr_t		mappedbno,
	int			whichfork,
	struct xfs_buf_map	**map,
	int			*nmaps)
{
	struct xfs_mount	*mp = dp->i_mount;
	int			nfsb;
	int			error = 0;
	struct xfs_bmbt_irec	irec;
	struct xfs_bmbt_irec	*irecs = &irec;
	int			nirecs;

	ASSERT(map && *map);
	ASSERT(*nmaps == 1);

	nfsb = (whichfork == XFS_DATA_FORK) ? mp->m_dirblkfsbs : 1;

	/*
	 * Caller doesn't have a mapping.  -2 means don't complain
	 * if we land in a hole.
	 */
	if (mappedbno == -1 || mappedbno == -2) {
		/*
		 * Optimize the one-block case.
		 */
		if (nfsb != 1)
			irecs = kmem_zalloc(sizeof(irec) * nfsb, KM_SLEEP);

		nirecs = nfsb;
		error = xfs_bmapi_read(dp, (xfs_fileoff_t)bno, nfsb, irecs,
				       &nirecs, xfs_bmapi_aflag(whichfork));
		if (error)
			goto out;
	} else {
		irecs->br_startblock = XFS_DADDR_TO_FSB(mp, mappedbno);
		irecs->br_startoff = (xfs_fileoff_t)bno;
		irecs->br_blockcount = nfsb;
		irecs->br_state = 0;
		nirecs = 1;
	}

	if (!xfs_da_map_covers_blocks(nirecs, irecs, bno, nfsb)) {
		error = mappedbno == -2 ? -1 : XFS_ERROR(EFSCORRUPTED);
		if (unlikely(error == EFSCORRUPTED)) {
			if (xfs_error_level >= XFS_ERRLEVEL_LOW) {
				int i;
				xfs_alert(mp, "%s: bno %lld dir: inode %lld",
					__func__, (long long)bno,
					(long long)dp->i_ino);
				for (i = 0; i < *nmaps; i++) {
					xfs_alert(mp,
"[%02d] br_startoff %lld br_startblock %lld br_blockcount %lld br_state %d",
						i,
						(long long)irecs[i].br_startoff,
						(long long)irecs[i].br_startblock,
						(long long)irecs[i].br_blockcount,
						irecs[i].br_state);
				}
			}
			XFS_ERROR_REPORT("xfs_da_do_buf(1)",
					 XFS_ERRLEVEL_LOW, mp);
		}
		goto out;
	}
	error = xfs_buf_map_from_irec(mp, map, nmaps, irecs, nirecs);
out:
	if (irecs != &irec)
		kmem_free(irecs);
	return error;
}

/*
 * Get a buffer for the dir/attr block.
 */
int
xfs_da_get_buf(
	struct xfs_trans	*trans,
	struct xfs_inode	*dp,
	xfs_dablk_t		bno,
	xfs_daddr_t		mappedbno,
	struct xfs_buf		**bpp,
	int			whichfork)
{
	struct xfs_buf		*bp;
	struct xfs_buf_map	map;
	struct xfs_buf_map	*mapp;
	int			nmap;
	int			error;

	*bpp = NULL;
	mapp = &map;
	nmap = 1;
	error = xfs_dabuf_map(trans, dp, bno, mappedbno, whichfork,
				&mapp, &nmap);
	if (error) {
		/* mapping a hole is not an error, but we don't continue */
		if (error == -1)
			error = 0;
		goto out_free;
	}

	bp = xfs_trans_get_buf_map(trans, dp->i_mount->m_ddev_targp,
				    mapp, nmap, 0);
	error = bp ? bp->b_error : XFS_ERROR(EIO);
	if (error) {
		xfs_trans_brelse(trans, bp);
		goto out_free;
	}

	*bpp = bp;

out_free:
	if (mapp != &map)
		kmem_free(mapp);

	return error;
}

/*
 * Get a buffer for the dir/attr block, fill in the contents.
 */
int
xfs_da_read_buf(
	struct xfs_trans	*trans,
	struct xfs_inode	*dp,
	xfs_dablk_t		bno,
	xfs_daddr_t		mappedbno,
	struct xfs_buf		**bpp,
	int			whichfork)
{
	struct xfs_buf		*bp;
	struct xfs_buf_map	map;
	struct xfs_buf_map	*mapp;
	int			nmap;
	int			error;

	*bpp = NULL;
	mapp = &map;
	nmap = 1;
	error = xfs_dabuf_map(trans, dp, bno, mappedbno, whichfork,
				&mapp, &nmap);
	if (error) {
		/* mapping a hole is not an error, but we don't continue */
		if (error == -1)
			error = 0;
		goto out_free;
	}

	error = xfs_trans_read_buf_map(dp->i_mount, trans,
					dp->i_mount->m_ddev_targp,
					mapp, nmap, 0, &bp);
	if (error)
		goto out_free;

	if (whichfork == XFS_ATTR_FORK)
		xfs_buf_set_ref(bp, XFS_ATTR_BTREE_REF);
	else
		xfs_buf_set_ref(bp, XFS_DIR_BTREE_REF);

	/*
	 * This verification code will be moved to a CRC verification callback
	 * function so just leave it here unchanged until then.
	 */
	{
		xfs_dir2_data_hdr_t	*hdr = bp->b_addr;
		xfs_dir2_free_t		*free = bp->b_addr;
		xfs_da_blkinfo_t	*info = bp->b_addr;
		uint			magic, magic1;
		struct xfs_mount	*mp = dp->i_mount;

		magic = be16_to_cpu(info->magic);
		magic1 = be32_to_cpu(hdr->magic);
		if (unlikely(
		    XFS_TEST_ERROR((magic != XFS_DA_NODE_MAGIC) &&
				   (magic != XFS_ATTR_LEAF_MAGIC) &&
				   (magic != XFS_DIR2_LEAF1_MAGIC) &&
				   (magic != XFS_DIR2_LEAFN_MAGIC) &&
				   (magic1 != XFS_DIR2_BLOCK_MAGIC) &&
				   (magic1 != XFS_DIR2_DATA_MAGIC) &&
				   (free->hdr.magic != cpu_to_be32(XFS_DIR2_FREE_MAGIC)),
				mp, XFS_ERRTAG_DA_READ_BUF,
				XFS_RANDOM_DA_READ_BUF))) {
			trace_xfs_da_btree_corrupt(bp, _RET_IP_);
			XFS_CORRUPTION_ERROR("xfs_da_do_buf(2)",
					     XFS_ERRLEVEL_LOW, mp, info);
			error = XFS_ERROR(EFSCORRUPTED);
			xfs_trans_brelse(trans, bp);
			goto out_free;
		}
	}
	*bpp = bp;
out_free:
	if (mapp != &map)
		kmem_free(mapp);

	return error;
}

/*
 * Readahead the dir/attr block.
 */
xfs_daddr_t
xfs_da_reada_buf(
	struct xfs_trans	*trans,
	struct xfs_inode	*dp,
	xfs_dablk_t		bno,
	int			whichfork)
{
	xfs_daddr_t		mappedbno = -1;
	struct xfs_buf_map	map;
	struct xfs_buf_map	*mapp;
	int			nmap;
	int			error;

	mapp = &map;
	nmap = 1;
	error = xfs_dabuf_map(trans, dp, bno, -1, whichfork,
				&mapp, &nmap);
	if (error) {
		/* mapping a hole is not an error, but we don't continue */
		if (error == -1)
			error = 0;
		goto out_free;
	}

	mappedbno = mapp[0].bm_bn;
	xfs_buf_readahead_map(dp->i_mount->m_ddev_targp, mapp, nmap);

out_free:
	if (mapp != &map)
		kmem_free(mapp);

	if (error)
		return -1;
	return mappedbno;
}

kmem_zone_t *xfs_da_state_zone;	/* anchor for state struct zone */

/*
 * Allocate a dir-state structure.
 * We don't put them on the stack since they're large.
 */
xfs_da_state_t *
xfs_da_state_alloc(void)
{
	return kmem_zone_zalloc(xfs_da_state_zone, KM_NOFS);
}

/*
 * Kill the altpath contents of a da-state structure.
 */
STATIC void
xfs_da_state_kill_altpath(xfs_da_state_t *state)
{
	int	i;

	for (i = 0; i < state->altpath.active; i++)
		state->altpath.blk[i].bp = NULL;
	state->altpath.active = 0;
}

/*
 * Free a da-state structure.
 */
void
xfs_da_state_free(xfs_da_state_t *state)
{
	xfs_da_state_kill_altpath(state);
#ifdef DEBUG
	memset((char *)state, 0, sizeof(*state));
#endif /* DEBUG */
	kmem_zone_free(xfs_da_state_zone, state);
}
