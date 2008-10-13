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
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_dir2_data.h"
#include "xfs_dir2_leaf.h"
#include "xfs_dir2_block.h"
#include "xfs_dir2_node.h"
#include "xfs_dir2_trace.h"
#include "xfs_error.h"

/*
 * Function declarations.
 */
static void xfs_dir2_free_log_header(xfs_trans_t *tp, xfs_dabuf_t *bp);
static int xfs_dir2_leafn_add(xfs_dabuf_t *bp, xfs_da_args_t *args, int index);
#ifdef DEBUG
static void xfs_dir2_leafn_check(xfs_inode_t *dp, xfs_dabuf_t *bp);
#else
#define	xfs_dir2_leafn_check(dp, bp)
#endif
static void xfs_dir2_leafn_moveents(xfs_da_args_t *args, xfs_dabuf_t *bp_s,
				    int start_s, xfs_dabuf_t *bp_d, int start_d,
				    int count);
static void xfs_dir2_leafn_rebalance(xfs_da_state_t *state,
				     xfs_da_state_blk_t *blk1,
				     xfs_da_state_blk_t *blk2);
static int xfs_dir2_leafn_remove(xfs_da_args_t *args, xfs_dabuf_t *bp,
				 int index, xfs_da_state_blk_t *dblk,
				 int *rval);
static int xfs_dir2_node_addname_int(xfs_da_args_t *args,
				     xfs_da_state_blk_t *fblk);

/*
 * Log entries from a freespace block.
 */
void
xfs_dir2_free_log_bests(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp,		/* freespace buffer */
	int			first,		/* first entry to log */
	int			last)		/* last entry to log */
{
	xfs_dir2_free_t		*free;		/* freespace structure */

	free = bp->data;
	ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
	xfs_da_log_buf(tp, bp,
		(uint)((char *)&free->bests[first] - (char *)free),
		(uint)((char *)&free->bests[last] - (char *)free +
		       sizeof(free->bests[0]) - 1));
}

/*
 * Log header from a freespace block.
 */
static void
xfs_dir2_free_log_header(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_dabuf_t		*bp)		/* freespace buffer */
{
	xfs_dir2_free_t		*free;		/* freespace structure */

	free = bp->data;
	ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
	xfs_da_log_buf(tp, bp, (uint)((char *)&free->hdr - (char *)free),
		(uint)(sizeof(xfs_dir2_free_hdr_t) - 1));
}

/*
 * Convert a leaf-format directory to a node-format directory.
 * We need to change the magic number of the leaf block, and copy
 * the freespace table out of the leaf block into its own block.
 */
int						/* error */
xfs_dir2_leaf_to_node(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_dabuf_t		*lbp)		/* leaf buffer */
{
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return value */
	xfs_dabuf_t		*fbp;		/* freespace buffer */
	xfs_dir2_db_t		fdb;		/* freespace block number */
	xfs_dir2_free_t		*free;		/* freespace structure */
	__be16			*from;		/* pointer to freespace entry */
	int			i;		/* leaf freespace index */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_tail_t	*ltp;		/* leaf tail structure */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			n;		/* count of live freespc ents */
	xfs_dir2_data_off_t	off;		/* freespace entry value */
	__be16			*to;		/* pointer to freespace entry */
	xfs_trans_t		*tp;		/* transaction pointer */

	xfs_dir2_trace_args_b("leaf_to_node", args, lbp);
	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	/*
	 * Add a freespace block to the directory.
	 */
	if ((error = xfs_dir2_grow_inode(args, XFS_DIR2_FREE_SPACE, &fdb))) {
		return error;
	}
	ASSERT(fdb == XFS_DIR2_FREE_FIRSTDB(mp));
	/*
	 * Get the buffer for the new freespace block.
	 */
	if ((error = xfs_da_get_buf(tp, dp, xfs_dir2_db_to_da(mp, fdb), -1, &fbp,
			XFS_DATA_FORK))) {
		return error;
	}
	ASSERT(fbp != NULL);
	free = fbp->data;
	leaf = lbp->data;
	ltp = xfs_dir2_leaf_tail_p(mp, leaf);
	/*
	 * Initialize the freespace block header.
	 */
	free->hdr.magic = cpu_to_be32(XFS_DIR2_FREE_MAGIC);
	free->hdr.firstdb = 0;
	ASSERT(be32_to_cpu(ltp->bestcount) <= (uint)dp->i_d.di_size / mp->m_dirblksize);
	free->hdr.nvalid = ltp->bestcount;
	/*
	 * Copy freespace entries from the leaf block to the new block.
	 * Count active entries.
	 */
	for (i = n = 0, from = xfs_dir2_leaf_bests_p(ltp), to = free->bests;
	     i < be32_to_cpu(ltp->bestcount); i++, from++, to++) {
		if ((off = be16_to_cpu(*from)) != NULLDATAOFF)
			n++;
		*to = cpu_to_be16(off);
	}
	free->hdr.nused = cpu_to_be32(n);
	leaf->hdr.info.magic = cpu_to_be16(XFS_DIR2_LEAFN_MAGIC);
	/*
	 * Log everything.
	 */
	xfs_dir2_leaf_log_header(tp, lbp);
	xfs_dir2_free_log_header(tp, fbp);
	xfs_dir2_free_log_bests(tp, fbp, 0, be32_to_cpu(free->hdr.nvalid) - 1);
	xfs_da_buf_done(fbp);
	xfs_dir2_leafn_check(dp, lbp);
	return 0;
}

/*
 * Add a leaf entry to a leaf block in a node-form directory.
 * The other work necessary is done from the caller.
 */
static int					/* error */
xfs_dir2_leafn_add(
	xfs_dabuf_t		*bp,		/* leaf buffer */
	xfs_da_args_t		*args,		/* operation arguments */
	int			index)		/* insertion pt for new entry */
{
	int			compact;	/* compacting stale leaves */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			highstale;	/* next stale entry */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	int			lfloghigh;	/* high leaf entry logging */
	int			lfloglow;	/* low leaf entry logging */
	int			lowstale;	/* previous stale entry */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_trans_t		*tp;		/* transaction pointer */

	xfs_dir2_trace_args_sb("leafn_add", args, index, bp);
	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	leaf = bp->data;

	/*
	 * Quick check just to make sure we are not going to index
	 * into other peoples memory
	 */
	if (index < 0)
		return XFS_ERROR(EFSCORRUPTED);

	/*
	 * If there are already the maximum number of leaf entries in
	 * the block, if there are no stale entries it won't fit.
	 * Caller will do a split.  If there are stale entries we'll do
	 * a compact.
	 */

	if (be16_to_cpu(leaf->hdr.count) == xfs_dir2_max_leaf_ents(mp)) {
		if (!leaf->hdr.stale)
			return XFS_ERROR(ENOSPC);
		compact = be16_to_cpu(leaf->hdr.stale) > 1;
	} else
		compact = 0;
	ASSERT(index == 0 || be32_to_cpu(leaf->ents[index - 1].hashval) <= args->hashval);
	ASSERT(index == be16_to_cpu(leaf->hdr.count) ||
	       be32_to_cpu(leaf->ents[index].hashval) >= args->hashval);

	if (args->op_flags & XFS_DA_OP_JUSTCHECK)
		return 0;

	/*
	 * Compact out all but one stale leaf entry.  Leaves behind
	 * the entry closest to index.
	 */
	if (compact) {
		xfs_dir2_leaf_compact_x1(bp, &index, &lowstale, &highstale,
			&lfloglow, &lfloghigh);
	}
	/*
	 * Set impossible logging indices for this case.
	 */
	else if (leaf->hdr.stale) {
		lfloglow = be16_to_cpu(leaf->hdr.count);
		lfloghigh = -1;
	}
	/*
	 * No stale entries, just insert a space for the new entry.
	 */
	if (!leaf->hdr.stale) {
		lep = &leaf->ents[index];
		if (index < be16_to_cpu(leaf->hdr.count))
			memmove(lep + 1, lep,
				(be16_to_cpu(leaf->hdr.count) - index) * sizeof(*lep));
		lfloglow = index;
		lfloghigh = be16_to_cpu(leaf->hdr.count);
		be16_add_cpu(&leaf->hdr.count, 1);
	}
	/*
	 * There are stale entries.  We'll use one for the new entry.
	 */
	else {
		/*
		 * If we didn't do a compact then we need to figure out
		 * which stale entry will be used.
		 */
		if (compact == 0) {
			/*
			 * Find first stale entry before our insertion point.
			 */
			for (lowstale = index - 1;
			     lowstale >= 0 &&
				be32_to_cpu(leaf->ents[lowstale].address) !=
				XFS_DIR2_NULL_DATAPTR;
			     lowstale--)
				continue;
			/*
			 * Find next stale entry after insertion point.
			 * Stop looking if the answer would be worse than
			 * lowstale already found.
			 */
			for (highstale = index;
			     highstale < be16_to_cpu(leaf->hdr.count) &&
				be32_to_cpu(leaf->ents[highstale].address) !=
				XFS_DIR2_NULL_DATAPTR &&
				(lowstale < 0 ||
				 index - lowstale - 1 >= highstale - index);
			     highstale++)
				continue;
		}
		/*
		 * Using the low stale entry.
		 * Shift entries up toward the stale slot.
		 */
		if (lowstale >= 0 &&
		    (highstale == be16_to_cpu(leaf->hdr.count) ||
		     index - lowstale - 1 < highstale - index)) {
			ASSERT(be32_to_cpu(leaf->ents[lowstale].address) ==
			       XFS_DIR2_NULL_DATAPTR);
			ASSERT(index - lowstale - 1 >= 0);
			if (index - lowstale - 1 > 0)
				memmove(&leaf->ents[lowstale],
					&leaf->ents[lowstale + 1],
					(index - lowstale - 1) * sizeof(*lep));
			lep = &leaf->ents[index - 1];
			lfloglow = MIN(lowstale, lfloglow);
			lfloghigh = MAX(index - 1, lfloghigh);
		}
		/*
		 * Using the high stale entry.
		 * Shift entries down toward the stale slot.
		 */
		else {
			ASSERT(be32_to_cpu(leaf->ents[highstale].address) ==
			       XFS_DIR2_NULL_DATAPTR);
			ASSERT(highstale - index >= 0);
			if (highstale - index > 0)
				memmove(&leaf->ents[index + 1],
					&leaf->ents[index],
					(highstale - index) * sizeof(*lep));
			lep = &leaf->ents[index];
			lfloglow = MIN(index, lfloglow);
			lfloghigh = MAX(highstale, lfloghigh);
		}
		be16_add_cpu(&leaf->hdr.stale, -1);
	}
	/*
	 * Insert the new entry, log everything.
	 */
	lep->hashval = cpu_to_be32(args->hashval);
	lep->address = cpu_to_be32(xfs_dir2_db_off_to_dataptr(mp,
				args->blkno, args->index));
	xfs_dir2_leaf_log_header(tp, bp);
	xfs_dir2_leaf_log_ents(tp, bp, lfloglow, lfloghigh);
	xfs_dir2_leafn_check(dp, bp);
	return 0;
}

#ifdef DEBUG
/*
 * Check internal consistency of a leafn block.
 */
void
xfs_dir2_leafn_check(
	xfs_inode_t	*dp,			/* incore directory inode */
	xfs_dabuf_t	*bp)			/* leaf buffer */
{
	int		i;			/* leaf index */
	xfs_dir2_leaf_t	*leaf;			/* leaf structure */
	xfs_mount_t	*mp;			/* filesystem mount point */
	int		stale;			/* count of stale leaves */

	leaf = bp->data;
	mp = dp->i_mount;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	ASSERT(be16_to_cpu(leaf->hdr.count) <= xfs_dir2_max_leaf_ents(mp));
	for (i = stale = 0; i < be16_to_cpu(leaf->hdr.count); i++) {
		if (i + 1 < be16_to_cpu(leaf->hdr.count)) {
			ASSERT(be32_to_cpu(leaf->ents[i].hashval) <=
			       be32_to_cpu(leaf->ents[i + 1].hashval));
		}
		if (be32_to_cpu(leaf->ents[i].address) == XFS_DIR2_NULL_DATAPTR)
			stale++;
	}
	ASSERT(be16_to_cpu(leaf->hdr.stale) == stale);
}
#endif	/* DEBUG */

/*
 * Return the last hash value in the leaf.
 * Stale entries are ok.
 */
xfs_dahash_t					/* hash value */
xfs_dir2_leafn_lasthash(
	xfs_dabuf_t	*bp,			/* leaf buffer */
	int		*count)			/* count of entries in leaf */
{
	xfs_dir2_leaf_t	*leaf;			/* leaf structure */

	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	if (count)
		*count = be16_to_cpu(leaf->hdr.count);
	if (!leaf->hdr.count)
		return 0;
	return be32_to_cpu(leaf->ents[be16_to_cpu(leaf->hdr.count) - 1].hashval);
}

/*
 * Look up a leaf entry for space to add a name in a node-format leaf block.
 * The extrablk in state is a freespace block.
 */
STATIC int
xfs_dir2_leafn_lookup_for_addname(
	xfs_dabuf_t		*bp,		/* leaf buffer */
	xfs_da_args_t		*args,		/* operation arguments */
	int			*indexp,	/* out: leaf entry index */
	xfs_da_state_t		*state)		/* state to fill in */
{
	xfs_dabuf_t		*curbp = NULL;	/* current data/free buffer */
	xfs_dir2_db_t		curdb = -1;	/* current data block number */
	xfs_dir2_db_t		curfdb = -1;	/* current free block number */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return value */
	int			fi;		/* free entry index */
	xfs_dir2_free_t		*free = NULL;	/* free block structure */
	int			index;		/* leaf entry index */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	int			length;		/* length of new data entry */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_dir2_db_t		newdb;		/* new data block number */
	xfs_dir2_db_t		newfdb;		/* new free block number */
	xfs_trans_t		*tp;		/* transaction pointer */

	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
#ifdef __KERNEL__
	ASSERT(be16_to_cpu(leaf->hdr.count) > 0);
#endif
	xfs_dir2_leafn_check(dp, bp);
	/*
	 * Look up the hash value in the leaf entries.
	 */
	index = xfs_dir2_leaf_search_hash(args, bp);
	/*
	 * Do we have a buffer coming in?
	 */
	if (state->extravalid) {
		/* If so, it's a free block buffer, get the block number. */
		curbp = state->extrablk.bp;
		curfdb = state->extrablk.blkno;
		free = curbp->data;
		ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
	}
	length = xfs_dir2_data_entsize(args->namelen);
	/*
	 * Loop over leaf entries with the right hash value.
	 */
	for (lep = &leaf->ents[index]; index < be16_to_cpu(leaf->hdr.count) &&
				be32_to_cpu(lep->hashval) == args->hashval;
				lep++, index++) {
		/*
		 * Skip stale leaf entries.
		 */
		if (be32_to_cpu(lep->address) == XFS_DIR2_NULL_DATAPTR)
			continue;
		/*
		 * Pull the data block number from the entry.
		 */
		newdb = xfs_dir2_dataptr_to_db(mp, be32_to_cpu(lep->address));
		/*
		 * For addname, we're looking for a place to put the new entry.
		 * We want to use a data block with an entry of equal
		 * hash value to ours if there is one with room.
		 *
		 * If this block isn't the data block we already have
		 * in hand, take a look at it.
		 */
		if (newdb != curdb) {
			curdb = newdb;
			/*
			 * Convert the data block to the free block
			 * holding its freespace information.
			 */
			newfdb = xfs_dir2_db_to_fdb(mp, newdb);
			/*
			 * If it's not the one we have in hand, read it in.
			 */
			if (newfdb != curfdb) {
				/*
				 * If we had one before, drop it.
				 */
				if (curbp)
					xfs_da_brelse(tp, curbp);
				/*
				 * Read the free block.
				 */
				error = xfs_da_read_buf(tp, dp,
						xfs_dir2_db_to_da(mp, newfdb),
						-1, &curbp, XFS_DATA_FORK);
				if (error)
					return error;
				free = curbp->data;
				ASSERT(be32_to_cpu(free->hdr.magic) ==
					XFS_DIR2_FREE_MAGIC);
				ASSERT((be32_to_cpu(free->hdr.firstdb) %
					XFS_DIR2_MAX_FREE_BESTS(mp)) == 0);
				ASSERT(be32_to_cpu(free->hdr.firstdb) <= curdb);
				ASSERT(curdb < be32_to_cpu(free->hdr.firstdb) +
					be32_to_cpu(free->hdr.nvalid));
			}
			/*
			 * Get the index for our entry.
			 */
			fi = xfs_dir2_db_to_fdindex(mp, curdb);
			/*
			 * If it has room, return it.
			 */
			if (unlikely(be16_to_cpu(free->bests[fi]) == NULLDATAOFF)) {
				XFS_ERROR_REPORT("xfs_dir2_leafn_lookup_int",
							XFS_ERRLEVEL_LOW, mp);
				if (curfdb != newfdb)
					xfs_da_brelse(tp, curbp);
				return XFS_ERROR(EFSCORRUPTED);
			}
			curfdb = newfdb;
			if (be16_to_cpu(free->bests[fi]) >= length)
				goto out;
		}
	}
	/* Didn't find any space */
	fi = -1;
out:
	ASSERT(args->op_flags & XFS_DA_OP_OKNOENT);
	if (curbp) {
		/* Giving back a free block. */
		state->extravalid = 1;
		state->extrablk.bp = curbp;
		state->extrablk.index = fi;
		state->extrablk.blkno = curfdb;
		state->extrablk.magic = XFS_DIR2_FREE_MAGIC;
	} else {
		state->extravalid = 0;
	}
	/*
	 * Return the index, that will be the insertion point.
	 */
	*indexp = index;
	return XFS_ERROR(ENOENT);
}

/*
 * Look up a leaf entry in a node-format leaf block.
 * The extrablk in state a data block.
 */
STATIC int
xfs_dir2_leafn_lookup_for_entry(
	xfs_dabuf_t		*bp,		/* leaf buffer */
	xfs_da_args_t		*args,		/* operation arguments */
	int			*indexp,	/* out: leaf entry index */
	xfs_da_state_t		*state)		/* state to fill in */
{
	xfs_dabuf_t		*curbp = NULL;	/* current data/free buffer */
	xfs_dir2_db_t		curdb = -1;	/* current data block number */
	xfs_dir2_data_entry_t	*dep;		/* data block entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return value */
	int			index;		/* leaf entry index */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_dir2_db_t		newdb;		/* new data block number */
	xfs_trans_t		*tp;		/* transaction pointer */
	enum xfs_dacmp		cmp;		/* comparison result */

	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
#ifdef __KERNEL__
	ASSERT(be16_to_cpu(leaf->hdr.count) > 0);
#endif
	xfs_dir2_leafn_check(dp, bp);
	/*
	 * Look up the hash value in the leaf entries.
	 */
	index = xfs_dir2_leaf_search_hash(args, bp);
	/*
	 * Do we have a buffer coming in?
	 */
	if (state->extravalid) {
		curbp = state->extrablk.bp;
		curdb = state->extrablk.blkno;
	}
	/*
	 * Loop over leaf entries with the right hash value.
	 */
	for (lep = &leaf->ents[index]; index < be16_to_cpu(leaf->hdr.count) &&
				be32_to_cpu(lep->hashval) == args->hashval;
				lep++, index++) {
		/*
		 * Skip stale leaf entries.
		 */
		if (be32_to_cpu(lep->address) == XFS_DIR2_NULL_DATAPTR)
			continue;
		/*
		 * Pull the data block number from the entry.
		 */
		newdb = xfs_dir2_dataptr_to_db(mp, be32_to_cpu(lep->address));
		/*
		 * Not adding a new entry, so we really want to find
		 * the name given to us.
		 *
		 * If it's a different data block, go get it.
		 */
		if (newdb != curdb) {
			/*
			 * If we had a block before that we aren't saving
			 * for a CI name, drop it
			 */
			if (curbp && (args->cmpresult == XFS_CMP_DIFFERENT ||
						curdb != state->extrablk.blkno))
				xfs_da_brelse(tp, curbp);
			/*
			 * If needing the block that is saved with a CI match,
			 * use it otherwise read in the new data block.
			 */
			if (args->cmpresult != XFS_CMP_DIFFERENT &&
					newdb == state->extrablk.blkno) {
				ASSERT(state->extravalid);
				curbp = state->extrablk.bp;
			} else {
				error = xfs_da_read_buf(tp, dp,
						xfs_dir2_db_to_da(mp, newdb),
						-1, &curbp, XFS_DATA_FORK);
				if (error)
					return error;
			}
			xfs_dir2_data_check(dp, curbp);
			curdb = newdb;
		}
		/*
		 * Point to the data entry.
		 */
		dep = (xfs_dir2_data_entry_t *)((char *)curbp->data +
			xfs_dir2_dataptr_to_off(mp, be32_to_cpu(lep->address)));
		/*
		 * Compare the entry and if it's an exact match, return
		 * EEXIST immediately. If it's the first case-insensitive
		 * match, store the block & inode number and continue looking.
		 */
		cmp = mp->m_dirnameops->compname(args, dep->name, dep->namelen);
		if (cmp != XFS_CMP_DIFFERENT && cmp != args->cmpresult) {
			/* If there is a CI match block, drop it */
			if (args->cmpresult != XFS_CMP_DIFFERENT &&
						curdb != state->extrablk.blkno)
				xfs_da_brelse(tp, state->extrablk.bp);
			args->cmpresult = cmp;
			args->inumber = be64_to_cpu(dep->inumber);
			*indexp = index;
			state->extravalid = 1;
			state->extrablk.bp = curbp;
			state->extrablk.blkno = curdb;
			state->extrablk.index = (int)((char *)dep -
							(char *)curbp->data);
			state->extrablk.magic = XFS_DIR2_DATA_MAGIC;
			if (cmp == XFS_CMP_EXACT)
				return XFS_ERROR(EEXIST);
		}
	}
	ASSERT(index == be16_to_cpu(leaf->hdr.count) ||
					(args->op_flags & XFS_DA_OP_OKNOENT));
	if (curbp) {
		if (args->cmpresult == XFS_CMP_DIFFERENT) {
			/* Giving back last used data block. */
			state->extravalid = 1;
			state->extrablk.bp = curbp;
			state->extrablk.index = -1;
			state->extrablk.blkno = curdb;
			state->extrablk.magic = XFS_DIR2_DATA_MAGIC;
		} else {
			/* If the curbp is not the CI match block, drop it */
			if (state->extrablk.bp != curbp)
				xfs_da_brelse(tp, curbp);
		}
	} else {
		state->extravalid = 0;
	}
	*indexp = index;
	return XFS_ERROR(ENOENT);
}

/*
 * Look up a leaf entry in a node-format leaf block.
 * If this is an addname then the extrablk in state is a freespace block,
 * otherwise it's a data block.
 */
int
xfs_dir2_leafn_lookup_int(
	xfs_dabuf_t		*bp,		/* leaf buffer */
	xfs_da_args_t		*args,		/* operation arguments */
	int			*indexp,	/* out: leaf entry index */
	xfs_da_state_t		*state)		/* state to fill in */
{
	if (args->op_flags & XFS_DA_OP_ADDNAME)
		return xfs_dir2_leafn_lookup_for_addname(bp, args, indexp,
							state);
	return xfs_dir2_leafn_lookup_for_entry(bp, args, indexp, state);
}

/*
 * Move count leaf entries from source to destination leaf.
 * Log entries and headers.  Stale entries are preserved.
 */
static void
xfs_dir2_leafn_moveents(
	xfs_da_args_t	*args,			/* operation arguments */
	xfs_dabuf_t	*bp_s,			/* source leaf buffer */
	int		start_s,		/* source leaf index */
	xfs_dabuf_t	*bp_d,			/* destination leaf buffer */
	int		start_d,		/* destination leaf index */
	int		count)			/* count of leaves to copy */
{
	xfs_dir2_leaf_t	*leaf_d;		/* destination leaf structure */
	xfs_dir2_leaf_t	*leaf_s;		/* source leaf structure */
	int		stale;			/* count stale leaves copied */
	xfs_trans_t	*tp;			/* transaction pointer */

	xfs_dir2_trace_args_bibii("leafn_moveents", args, bp_s, start_s, bp_d,
		start_d, count);
	/*
	 * Silently return if nothing to do.
	 */
	if (count == 0) {
		return;
	}
	tp = args->trans;
	leaf_s = bp_s->data;
	leaf_d = bp_d->data;
	/*
	 * If the destination index is not the end of the current
	 * destination leaf entries, open up a hole in the destination
	 * to hold the new entries.
	 */
	if (start_d < be16_to_cpu(leaf_d->hdr.count)) {
		memmove(&leaf_d->ents[start_d + count], &leaf_d->ents[start_d],
			(be16_to_cpu(leaf_d->hdr.count) - start_d) *
			sizeof(xfs_dir2_leaf_entry_t));
		xfs_dir2_leaf_log_ents(tp, bp_d, start_d + count,
			count + be16_to_cpu(leaf_d->hdr.count) - 1);
	}
	/*
	 * If the source has stale leaves, count the ones in the copy range
	 * so we can update the header correctly.
	 */
	if (leaf_s->hdr.stale) {
		int	i;			/* temp leaf index */

		for (i = start_s, stale = 0; i < start_s + count; i++) {
			if (be32_to_cpu(leaf_s->ents[i].address) == XFS_DIR2_NULL_DATAPTR)
				stale++;
		}
	} else
		stale = 0;
	/*
	 * Copy the leaf entries from source to destination.
	 */
	memcpy(&leaf_d->ents[start_d], &leaf_s->ents[start_s],
		count * sizeof(xfs_dir2_leaf_entry_t));
	xfs_dir2_leaf_log_ents(tp, bp_d, start_d, start_d + count - 1);
	/*
	 * If there are source entries after the ones we copied,
	 * delete the ones we copied by sliding the next ones down.
	 */
	if (start_s + count < be16_to_cpu(leaf_s->hdr.count)) {
		memmove(&leaf_s->ents[start_s], &leaf_s->ents[start_s + count],
			count * sizeof(xfs_dir2_leaf_entry_t));
		xfs_dir2_leaf_log_ents(tp, bp_s, start_s, start_s + count - 1);
	}
	/*
	 * Update the headers and log them.
	 */
	be16_add_cpu(&leaf_s->hdr.count, -(count));
	be16_add_cpu(&leaf_s->hdr.stale, -(stale));
	be16_add_cpu(&leaf_d->hdr.count, count);
	be16_add_cpu(&leaf_d->hdr.stale, stale);
	xfs_dir2_leaf_log_header(tp, bp_s);
	xfs_dir2_leaf_log_header(tp, bp_d);
	xfs_dir2_leafn_check(args->dp, bp_s);
	xfs_dir2_leafn_check(args->dp, bp_d);
}

/*
 * Determine the sort order of two leaf blocks.
 * Returns 1 if both are valid and leaf2 should be before leaf1, else 0.
 */
int						/* sort order */
xfs_dir2_leafn_order(
	xfs_dabuf_t	*leaf1_bp,		/* leaf1 buffer */
	xfs_dabuf_t	*leaf2_bp)		/* leaf2 buffer */
{
	xfs_dir2_leaf_t	*leaf1;			/* leaf1 structure */
	xfs_dir2_leaf_t	*leaf2;			/* leaf2 structure */

	leaf1 = leaf1_bp->data;
	leaf2 = leaf2_bp->data;
	ASSERT(be16_to_cpu(leaf1->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	ASSERT(be16_to_cpu(leaf2->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	if (be16_to_cpu(leaf1->hdr.count) > 0 &&
	    be16_to_cpu(leaf2->hdr.count) > 0 &&
	    (be32_to_cpu(leaf2->ents[0].hashval) < be32_to_cpu(leaf1->ents[0].hashval) ||
	     be32_to_cpu(leaf2->ents[be16_to_cpu(leaf2->hdr.count) - 1].hashval) <
	     be32_to_cpu(leaf1->ents[be16_to_cpu(leaf1->hdr.count) - 1].hashval)))
		return 1;
	return 0;
}

/*
 * Rebalance leaf entries between two leaf blocks.
 * This is actually only called when the second block is new,
 * though the code deals with the general case.
 * A new entry will be inserted in one of the blocks, and that
 * entry is taken into account when balancing.
 */
static void
xfs_dir2_leafn_rebalance(
	xfs_da_state_t		*state,		/* btree cursor */
	xfs_da_state_blk_t	*blk1,		/* first btree block */
	xfs_da_state_blk_t	*blk2)		/* second btree block */
{
	xfs_da_args_t		*args;		/* operation arguments */
	int			count;		/* count (& direction) leaves */
	int			isleft;		/* new goes in left leaf */
	xfs_dir2_leaf_t		*leaf1;		/* first leaf structure */
	xfs_dir2_leaf_t		*leaf2;		/* second leaf structure */
	int			mid;		/* midpoint leaf index */
#ifdef DEBUG
	int			oldstale;	/* old count of stale leaves */
#endif
	int			oldsum;		/* old total leaf count */
	int			swap;		/* swapped leaf blocks */

	args = state->args;
	/*
	 * If the block order is wrong, swap the arguments.
	 */
	if ((swap = xfs_dir2_leafn_order(blk1->bp, blk2->bp))) {
		xfs_da_state_blk_t	*tmp;	/* temp for block swap */

		tmp = blk1;
		blk1 = blk2;
		blk2 = tmp;
	}
	leaf1 = blk1->bp->data;
	leaf2 = blk2->bp->data;
	oldsum = be16_to_cpu(leaf1->hdr.count) + be16_to_cpu(leaf2->hdr.count);
#ifdef DEBUG
	oldstale = be16_to_cpu(leaf1->hdr.stale) + be16_to_cpu(leaf2->hdr.stale);
#endif
	mid = oldsum >> 1;
	/*
	 * If the old leaf count was odd then the new one will be even,
	 * so we need to divide the new count evenly.
	 */
	if (oldsum & 1) {
		xfs_dahash_t	midhash;	/* middle entry hash value */

		if (mid >= be16_to_cpu(leaf1->hdr.count))
			midhash = be32_to_cpu(leaf2->ents[mid - be16_to_cpu(leaf1->hdr.count)].hashval);
		else
			midhash = be32_to_cpu(leaf1->ents[mid].hashval);
		isleft = args->hashval <= midhash;
	}
	/*
	 * If the old count is even then the new count is odd, so there's
	 * no preferred side for the new entry.
	 * Pick the left one.
	 */
	else
		isleft = 1;
	/*
	 * Calculate moved entry count.  Positive means left-to-right,
	 * negative means right-to-left.  Then move the entries.
	 */
	count = be16_to_cpu(leaf1->hdr.count) - mid + (isleft == 0);
	if (count > 0)
		xfs_dir2_leafn_moveents(args, blk1->bp,
			be16_to_cpu(leaf1->hdr.count) - count, blk2->bp, 0, count);
	else if (count < 0)
		xfs_dir2_leafn_moveents(args, blk2->bp, 0, blk1->bp,
			be16_to_cpu(leaf1->hdr.count), count);
	ASSERT(be16_to_cpu(leaf1->hdr.count) + be16_to_cpu(leaf2->hdr.count) == oldsum);
	ASSERT(be16_to_cpu(leaf1->hdr.stale) + be16_to_cpu(leaf2->hdr.stale) == oldstale);
	/*
	 * Mark whether we're inserting into the old or new leaf.
	 */
	if (be16_to_cpu(leaf1->hdr.count) < be16_to_cpu(leaf2->hdr.count))
		state->inleaf = swap;
	else if (be16_to_cpu(leaf1->hdr.count) > be16_to_cpu(leaf2->hdr.count))
		state->inleaf = !swap;
	else
		state->inleaf =
			swap ^ (blk1->index <= be16_to_cpu(leaf1->hdr.count));
	/*
	 * Adjust the expected index for insertion.
	 */
	if (!state->inleaf)
		blk2->index = blk1->index - be16_to_cpu(leaf1->hdr.count);

	/*
	 * Finally sanity check just to make sure we are not returning a
	 * negative index
	 */
	if(blk2->index < 0) {
		state->inleaf = 1;
		blk2->index = 0;
		cmn_err(CE_ALERT,
			"xfs_dir2_leafn_rebalance: picked the wrong leaf? reverting original leaf: "
			"blk1->index %d\n",
			blk1->index);
	}
}

/*
 * Remove an entry from a node directory.
 * This removes the leaf entry and the data entry,
 * and updates the free block if necessary.
 */
static int					/* error */
xfs_dir2_leafn_remove(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_dabuf_t		*bp,		/* leaf buffer */
	int			index,		/* leaf entry index */
	xfs_da_state_blk_t	*dblk,		/* data block */
	int			*rval)		/* resulting block needs join */
{
	xfs_dir2_data_t		*data;		/* data block structure */
	xfs_dir2_db_t		db;		/* data block number */
	xfs_dabuf_t		*dbp;		/* data block buffer */
	xfs_dir2_data_entry_t	*dep;		/* data block entry */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry */
	int			longest;	/* longest data free entry */
	int			off;		/* data block entry offset */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needlog;	/* need to log data header */
	int			needscan;	/* need to rescan data frees */
	xfs_trans_t		*tp;		/* transaction pointer */

	xfs_dir2_trace_args_sb("leafn_remove", args, index, bp);
	dp = args->dp;
	tp = args->trans;
	mp = dp->i_mount;
	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	/*
	 * Point to the entry we're removing.
	 */
	lep = &leaf->ents[index];
	/*
	 * Extract the data block and offset from the entry.
	 */
	db = xfs_dir2_dataptr_to_db(mp, be32_to_cpu(lep->address));
	ASSERT(dblk->blkno == db);
	off = xfs_dir2_dataptr_to_off(mp, be32_to_cpu(lep->address));
	ASSERT(dblk->index == off);
	/*
	 * Kill the leaf entry by marking it stale.
	 * Log the leaf block changes.
	 */
	be16_add_cpu(&leaf->hdr.stale, 1);
	xfs_dir2_leaf_log_header(tp, bp);
	lep->address = cpu_to_be32(XFS_DIR2_NULL_DATAPTR);
	xfs_dir2_leaf_log_ents(tp, bp, index, index);
	/*
	 * Make the data entry free.  Keep track of the longest freespace
	 * in the data block in case it changes.
	 */
	dbp = dblk->bp;
	data = dbp->data;
	dep = (xfs_dir2_data_entry_t *)((char *)data + off);
	longest = be16_to_cpu(data->hdr.bestfree[0].length);
	needlog = needscan = 0;
	xfs_dir2_data_make_free(tp, dbp, off,
		xfs_dir2_data_entsize(dep->namelen), &needlog, &needscan);
	/*
	 * Rescan the data block freespaces for bestfree.
	 * Log the data block header if needed.
	 */
	if (needscan)
		xfs_dir2_data_freescan(mp, data, &needlog);
	if (needlog)
		xfs_dir2_data_log_header(tp, dbp);
	xfs_dir2_data_check(dp, dbp);
	/*
	 * If the longest data block freespace changes, need to update
	 * the corresponding freeblock entry.
	 */
	if (longest < be16_to_cpu(data->hdr.bestfree[0].length)) {
		int		error;		/* error return value */
		xfs_dabuf_t	*fbp;		/* freeblock buffer */
		xfs_dir2_db_t	fdb;		/* freeblock block number */
		int		findex;		/* index in freeblock entries */
		xfs_dir2_free_t	*free;		/* freeblock structure */
		int		logfree;	/* need to log free entry */

		/*
		 * Convert the data block number to a free block,
		 * read in the free block.
		 */
		fdb = xfs_dir2_db_to_fdb(mp, db);
		if ((error = xfs_da_read_buf(tp, dp, xfs_dir2_db_to_da(mp, fdb),
				-1, &fbp, XFS_DATA_FORK))) {
			return error;
		}
		free = fbp->data;
		ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
		ASSERT(be32_to_cpu(free->hdr.firstdb) ==
		       XFS_DIR2_MAX_FREE_BESTS(mp) *
		       (fdb - XFS_DIR2_FREE_FIRSTDB(mp)));
		/*
		 * Calculate which entry we need to fix.
		 */
		findex = xfs_dir2_db_to_fdindex(mp, db);
		longest = be16_to_cpu(data->hdr.bestfree[0].length);
		/*
		 * If the data block is now empty we can get rid of it
		 * (usually).
		 */
		if (longest == mp->m_dirblksize - (uint)sizeof(data->hdr)) {
			/*
			 * Try to punch out the data block.
			 */
			error = xfs_dir2_shrink_inode(args, db, dbp);
			if (error == 0) {
				dblk->bp = NULL;
				data = NULL;
			}
			/*
			 * We can get ENOSPC if there's no space reservation.
			 * In this case just drop the buffer and some one else
			 * will eventually get rid of the empty block.
			 */
			else if (error == ENOSPC && args->total == 0)
				xfs_da_buf_done(dbp);
			else
				return error;
		}
		/*
		 * If we got rid of the data block, we can eliminate that entry
		 * in the free block.
		 */
		if (data == NULL) {
			/*
			 * One less used entry in the free table.
			 */
			be32_add_cpu(&free->hdr.nused, -1);
			xfs_dir2_free_log_header(tp, fbp);
			/*
			 * If this was the last entry in the table, we can
			 * trim the table size back.  There might be other
			 * entries at the end referring to non-existent
			 * data blocks, get those too.
			 */
			if (findex == be32_to_cpu(free->hdr.nvalid) - 1) {
				int	i;		/* free entry index */

				for (i = findex - 1;
				     i >= 0 && be16_to_cpu(free->bests[i]) == NULLDATAOFF;
				     i--)
					continue;
				free->hdr.nvalid = cpu_to_be32(i + 1);
				logfree = 0;
			}
			/*
			 * Not the last entry, just punch it out.
			 */
			else {
				free->bests[findex] = cpu_to_be16(NULLDATAOFF);
				logfree = 1;
			}
			/*
			 * If there are no useful entries left in the block,
			 * get rid of the block if we can.
			 */
			if (!free->hdr.nused) {
				error = xfs_dir2_shrink_inode(args, fdb, fbp);
				if (error == 0) {
					fbp = NULL;
					logfree = 0;
				} else if (error != ENOSPC || args->total != 0)
					return error;
				/*
				 * It's possible to get ENOSPC if there is no
				 * space reservation.  In this case some one
				 * else will eventually get rid of this block.
				 */
			}
		}
		/*
		 * Data block is not empty, just set the free entry to
		 * the new value.
		 */
		else {
			free->bests[findex] = cpu_to_be16(longest);
			logfree = 1;
		}
		/*
		 * Log the free entry that changed, unless we got rid of it.
		 */
		if (logfree)
			xfs_dir2_free_log_bests(tp, fbp, findex, findex);
		/*
		 * Drop the buffer if we still have it.
		 */
		if (fbp)
			xfs_da_buf_done(fbp);
	}
	xfs_dir2_leafn_check(dp, bp);
	/*
	 * Return indication of whether this leaf block is emtpy enough
	 * to justify trying to join it with a neighbor.
	 */
	*rval =
		((uint)sizeof(leaf->hdr) +
		 (uint)sizeof(leaf->ents[0]) *
		 (be16_to_cpu(leaf->hdr.count) - be16_to_cpu(leaf->hdr.stale))) <
		mp->m_dir_magicpct;
	return 0;
}

/*
 * Split the leaf entries in the old block into old and new blocks.
 */
int						/* error */
xfs_dir2_leafn_split(
	xfs_da_state_t		*state,		/* btree cursor */
	xfs_da_state_blk_t	*oldblk,	/* original block */
	xfs_da_state_blk_t	*newblk)	/* newly created block */
{
	xfs_da_args_t		*args;		/* operation arguments */
	xfs_dablk_t		blkno;		/* new leaf block number */
	int			error;		/* error return value */
	xfs_mount_t		*mp;		/* filesystem mount point */

	/*
	 * Allocate space for a new leaf node.
	 */
	args = state->args;
	mp = args->dp->i_mount;
	ASSERT(args != NULL);
	ASSERT(oldblk->magic == XFS_DIR2_LEAFN_MAGIC);
	error = xfs_da_grow_inode(args, &blkno);
	if (error) {
		return error;
	}
	/*
	 * Initialize the new leaf block.
	 */
	error = xfs_dir2_leaf_init(args, xfs_dir2_da_to_db(mp, blkno),
		&newblk->bp, XFS_DIR2_LEAFN_MAGIC);
	if (error) {
		return error;
	}
	newblk->blkno = blkno;
	newblk->magic = XFS_DIR2_LEAFN_MAGIC;
	/*
	 * Rebalance the entries across the two leaves, link the new
	 * block into the leaves.
	 */
	xfs_dir2_leafn_rebalance(state, oldblk, newblk);
	error = xfs_da_blk_link(state, oldblk, newblk);
	if (error) {
		return error;
	}
	/*
	 * Insert the new entry in the correct block.
	 */
	if (state->inleaf)
		error = xfs_dir2_leafn_add(oldblk->bp, args, oldblk->index);
	else
		error = xfs_dir2_leafn_add(newblk->bp, args, newblk->index);
	/*
	 * Update last hashval in each block since we added the name.
	 */
	oldblk->hashval = xfs_dir2_leafn_lasthash(oldblk->bp, NULL);
	newblk->hashval = xfs_dir2_leafn_lasthash(newblk->bp, NULL);
	xfs_dir2_leafn_check(args->dp, oldblk->bp);
	xfs_dir2_leafn_check(args->dp, newblk->bp);
	return error;
}

/*
 * Check a leaf block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 */
int						/* error */
xfs_dir2_leafn_toosmall(
	xfs_da_state_t		*state,		/* btree cursor */
	int			*action)	/* resulting action to take */
{
	xfs_da_state_blk_t	*blk;		/* leaf block */
	xfs_dablk_t		blkno;		/* leaf block number */
	xfs_dabuf_t		*bp;		/* leaf buffer */
	int			bytes;		/* bytes in use */
	int			count;		/* leaf live entry count */
	int			error;		/* error return value */
	int			forward;	/* sibling block direction */
	int			i;		/* sibling counter */
	xfs_da_blkinfo_t	*info;		/* leaf block header */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	int			rval;		/* result from path_shift */

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[state->path.active - 1];
	info = blk->bp->data;
	ASSERT(be16_to_cpu(info->magic) == XFS_DIR2_LEAFN_MAGIC);
	leaf = (xfs_dir2_leaf_t *)info;
	count = be16_to_cpu(leaf->hdr.count) - be16_to_cpu(leaf->hdr.stale);
	bytes = (uint)sizeof(leaf->hdr) + count * (uint)sizeof(leaf->ents[0]);
	if (bytes > (state->blocksize >> 1)) {
		/*
		 * Blk over 50%, don't try to join.
		 */
		*action = 0;
		return 0;
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
		error = xfs_da_path_shift(state, &state->altpath, forward, 0,
			&rval);
		if (error)
			return error;
		*action = rval ? 2 : 0;
		return 0;
	}
	/*
	 * Examine each sibling block to see if we can coalesce with
	 * at least 25% free space to spare.  We need to figure out
	 * whether to merge with the forward or the backward block.
	 * We prefer coalescing with the lower numbered sibling so as
	 * to shrink a directory over time.
	 */
	forward = be32_to_cpu(info->forw) < be32_to_cpu(info->back);
	for (i = 0, bp = NULL; i < 2; forward = !forward, i++) {
		blkno = forward ? be32_to_cpu(info->forw) : be32_to_cpu(info->back);
		if (blkno == 0)
			continue;
		/*
		 * Read the sibling leaf block.
		 */
		if ((error =
		    xfs_da_read_buf(state->args->trans, state->args->dp, blkno,
			    -1, &bp, XFS_DATA_FORK))) {
			return error;
		}
		ASSERT(bp != NULL);
		/*
		 * Count bytes in the two blocks combined.
		 */
		leaf = (xfs_dir2_leaf_t *)info;
		count = be16_to_cpu(leaf->hdr.count) - be16_to_cpu(leaf->hdr.stale);
		bytes = state->blocksize - (state->blocksize >> 2);
		leaf = bp->data;
		ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
		count += be16_to_cpu(leaf->hdr.count) - be16_to_cpu(leaf->hdr.stale);
		bytes -= count * (uint)sizeof(leaf->ents[0]);
		/*
		 * Fits with at least 25% to spare.
		 */
		if (bytes >= 0)
			break;
		xfs_da_brelse(state->args->trans, bp);
	}
	/*
	 * Didn't like either block, give up.
	 */
	if (i >= 2) {
		*action = 0;
		return 0;
	}
	/*
	 * Done with the sibling leaf block here, drop the dabuf
	 * so path_shift can get it.
	 */
	xfs_da_buf_done(bp);
	/*
	 * Make altpath point to the block we want to keep (the lower
	 * numbered block) and path point to the block we want to drop.
	 */
	memcpy(&state->altpath, &state->path, sizeof(state->path));
	if (blkno < blk->blkno)
		error = xfs_da_path_shift(state, &state->altpath, forward, 0,
			&rval);
	else
		error = xfs_da_path_shift(state, &state->path, forward, 0,
			&rval);
	if (error) {
		return error;
	}
	*action = rval ? 0 : 1;
	return 0;
}

/*
 * Move all the leaf entries from drop_blk to save_blk.
 * This is done as part of a join operation.
 */
void
xfs_dir2_leafn_unbalance(
	xfs_da_state_t		*state,		/* cursor */
	xfs_da_state_blk_t	*drop_blk,	/* dead block */
	xfs_da_state_blk_t	*save_blk)	/* surviving block */
{
	xfs_da_args_t		*args;		/* operation arguments */
	xfs_dir2_leaf_t		*drop_leaf;	/* dead leaf structure */
	xfs_dir2_leaf_t		*save_leaf;	/* surviving leaf structure */

	args = state->args;
	ASSERT(drop_blk->magic == XFS_DIR2_LEAFN_MAGIC);
	ASSERT(save_blk->magic == XFS_DIR2_LEAFN_MAGIC);
	drop_leaf = drop_blk->bp->data;
	save_leaf = save_blk->bp->data;
	ASSERT(be16_to_cpu(drop_leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	ASSERT(be16_to_cpu(save_leaf->hdr.info.magic) == XFS_DIR2_LEAFN_MAGIC);
	/*
	 * If there are any stale leaf entries, take this opportunity
	 * to purge them.
	 */
	if (drop_leaf->hdr.stale)
		xfs_dir2_leaf_compact(args, drop_blk->bp);
	if (save_leaf->hdr.stale)
		xfs_dir2_leaf_compact(args, save_blk->bp);
	/*
	 * Move the entries from drop to the appropriate end of save.
	 */
	drop_blk->hashval = be32_to_cpu(drop_leaf->ents[be16_to_cpu(drop_leaf->hdr.count) - 1].hashval);
	if (xfs_dir2_leafn_order(save_blk->bp, drop_blk->bp))
		xfs_dir2_leafn_moveents(args, drop_blk->bp, 0, save_blk->bp, 0,
			be16_to_cpu(drop_leaf->hdr.count));
	else
		xfs_dir2_leafn_moveents(args, drop_blk->bp, 0, save_blk->bp,
			be16_to_cpu(save_leaf->hdr.count), be16_to_cpu(drop_leaf->hdr.count));
	save_blk->hashval = be32_to_cpu(save_leaf->ents[be16_to_cpu(save_leaf->hdr.count) - 1].hashval);
	xfs_dir2_leafn_check(args->dp, save_blk->bp);
}

/*
 * Top-level node form directory addname routine.
 */
int						/* error */
xfs_dir2_node_addname(
	xfs_da_args_t		*args)		/* operation arguments */
{
	xfs_da_state_blk_t	*blk;		/* leaf block for insert */
	int			error;		/* error return value */
	int			rval;		/* sub-return value */
	xfs_da_state_t		*state;		/* btree cursor */

	xfs_dir2_trace_args("node_addname", args);
	/*
	 * Allocate and initialize the state (btree cursor).
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_dirblksize;
	state->node_ents = state->mp->m_dir_node_ents;
	/*
	 * Look up the name.  We're not supposed to find it, but
	 * this gives us the insertion point.
	 */
	error = xfs_da_node_lookup_int(state, &rval);
	if (error)
		rval = error;
	if (rval != ENOENT) {
		goto done;
	}
	/*
	 * Add the data entry to a data block.
	 * Extravalid is set to a freeblock found by lookup.
	 */
	rval = xfs_dir2_node_addname_int(args,
		state->extravalid ? &state->extrablk : NULL);
	if (rval) {
		goto done;
	}
	blk = &state->path.blk[state->path.active - 1];
	ASSERT(blk->magic == XFS_DIR2_LEAFN_MAGIC);
	/*
	 * Add the new leaf entry.
	 */
	rval = xfs_dir2_leafn_add(blk->bp, args, blk->index);
	if (rval == 0) {
		/*
		 * It worked, fix the hash values up the btree.
		 */
		if (!(args->op_flags & XFS_DA_OP_JUSTCHECK))
			xfs_da_fixhashpath(state, &state->path);
	} else {
		/*
		 * It didn't work, we need to split the leaf block.
		 */
		if (args->total == 0) {
			ASSERT(rval == ENOSPC);
			goto done;
		}
		/*
		 * Split the leaf block and insert the new entry.
		 */
		rval = xfs_da_split(state);
	}
done:
	xfs_da_state_free(state);
	return rval;
}

/*
 * Add the data entry for a node-format directory name addition.
 * The leaf entry is added in xfs_dir2_leafn_add.
 * We may enter with a freespace block that the lookup found.
 */
static int					/* error */
xfs_dir2_node_addname_int(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_da_state_blk_t	*fblk)		/* optional freespace block */
{
	xfs_dir2_data_t		*data;		/* data block structure */
	xfs_dir2_db_t		dbno;		/* data block number */
	xfs_dabuf_t		*dbp;		/* data block buffer */
	xfs_dir2_data_entry_t	*dep;		/* data entry pointer */
	xfs_inode_t		*dp;		/* incore directory inode */
	xfs_dir2_data_unused_t	*dup;		/* data unused entry pointer */
	int			error;		/* error return value */
	xfs_dir2_db_t		fbno;		/* freespace block number */
	xfs_dabuf_t		*fbp;		/* freespace buffer */
	int			findex;		/* freespace entry index */
	xfs_dir2_free_t		*free=NULL;	/* freespace block structure */
	xfs_dir2_db_t		ifbno;		/* initial freespace block no */
	xfs_dir2_db_t		lastfbno=0;	/* highest freespace block no */
	int			length;		/* length of the new entry */
	int			logfree;	/* need to log free entry */
	xfs_mount_t		*mp;		/* filesystem mount point */
	int			needlog;	/* need to log data header */
	int			needscan;	/* need to rescan data frees */
	__be16			*tagp;		/* data entry tag pointer */
	xfs_trans_t		*tp;		/* transaction pointer */

	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	length = xfs_dir2_data_entsize(args->namelen);
	/*
	 * If we came in with a freespace block that means that lookup
	 * found an entry with our hash value.  This is the freespace
	 * block for that data entry.
	 */
	if (fblk) {
		fbp = fblk->bp;
		/*
		 * Remember initial freespace block number.
		 */
		ifbno = fblk->blkno;
		free = fbp->data;
		ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
		findex = fblk->index;
		/*
		 * This means the free entry showed that the data block had
		 * space for our entry, so we remembered it.
		 * Use that data block.
		 */
		if (findex >= 0) {
			ASSERT(findex < be32_to_cpu(free->hdr.nvalid));
			ASSERT(be16_to_cpu(free->bests[findex]) != NULLDATAOFF);
			ASSERT(be16_to_cpu(free->bests[findex]) >= length);
			dbno = be32_to_cpu(free->hdr.firstdb) + findex;
		}
		/*
		 * The data block looked at didn't have enough room.
		 * We'll start at the beginning of the freespace entries.
		 */
		else {
			dbno = -1;
			findex = 0;
		}
	}
	/*
	 * Didn't come in with a freespace block, so don't have a data block.
	 */
	else {
		ifbno = dbno = -1;
		fbp = NULL;
		findex = 0;
	}
	/*
	 * If we don't have a data block yet, we're going to scan the
	 * freespace blocks looking for one.  Figure out what the
	 * highest freespace block number is.
	 */
	if (dbno == -1) {
		xfs_fileoff_t	fo;		/* freespace block number */

		if ((error = xfs_bmap_last_offset(tp, dp, &fo, XFS_DATA_FORK)))
			return error;
		lastfbno = xfs_dir2_da_to_db(mp, (xfs_dablk_t)fo);
		fbno = ifbno;
	}
	/*
	 * While we haven't identified a data block, search the freeblock
	 * data for a good data block.  If we find a null freeblock entry,
	 * indicating a hole in the data blocks, remember that.
	 */
	while (dbno == -1) {
		/*
		 * If we don't have a freeblock in hand, get the next one.
		 */
		if (fbp == NULL) {
			/*
			 * Happens the first time through unless lookup gave
			 * us a freespace block to start with.
			 */
			if (++fbno == 0)
				fbno = XFS_DIR2_FREE_FIRSTDB(mp);
			/*
			 * If it's ifbno we already looked at it.
			 */
			if (fbno == ifbno)
				fbno++;
			/*
			 * If it's off the end we're done.
			 */
			if (fbno >= lastfbno)
				break;
			/*
			 * Read the block.  There can be holes in the
			 * freespace blocks, so this might not succeed.
			 * This should be really rare, so there's no reason
			 * to avoid it.
			 */
			if ((error = xfs_da_read_buf(tp, dp,
					xfs_dir2_db_to_da(mp, fbno), -2, &fbp,
					XFS_DATA_FORK))) {
				return error;
			}
			if (unlikely(fbp == NULL)) {
				continue;
			}
			free = fbp->data;
			ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
			findex = 0;
		}
		/*
		 * Look at the current free entry.  Is it good enough?
		 */
		if (be16_to_cpu(free->bests[findex]) != NULLDATAOFF &&
		    be16_to_cpu(free->bests[findex]) >= length)
			dbno = be32_to_cpu(free->hdr.firstdb) + findex;
		else {
			/*
			 * Are we done with the freeblock?
			 */
			if (++findex == be32_to_cpu(free->hdr.nvalid)) {
				/*
				 * Drop the block.
				 */
				xfs_da_brelse(tp, fbp);
				fbp = NULL;
				if (fblk && fblk->bp)
					fblk->bp = NULL;
			}
		}
	}
	/*
	 * If we don't have a data block, we need to allocate one and make
	 * the freespace entries refer to it.
	 */
	if (unlikely(dbno == -1)) {
		/*
		 * Not allowed to allocate, return failure.
		 */
		if ((args->op_flags & XFS_DA_OP_JUSTCHECK) ||
							args->total == 0) {
			/*
			 * Drop the freespace buffer unless it came from our
			 * caller.
			 */
			if ((fblk == NULL || fblk->bp == NULL) && fbp != NULL)
				xfs_da_buf_done(fbp);
			return XFS_ERROR(ENOSPC);
		}
		/*
		 * Allocate and initialize the new data block.
		 */
		if (unlikely((error = xfs_dir2_grow_inode(args,
							 XFS_DIR2_DATA_SPACE,
							 &dbno)) ||
		    (error = xfs_dir2_data_init(args, dbno, &dbp)))) {
			/*
			 * Drop the freespace buffer unless it came from our
			 * caller.
			 */
			if ((fblk == NULL || fblk->bp == NULL) && fbp != NULL)
				xfs_da_buf_done(fbp);
			return error;
		}
		/*
		 * If (somehow) we have a freespace block, get rid of it.
		 */
		if (fbp)
			xfs_da_brelse(tp, fbp);
		if (fblk && fblk->bp)
			fblk->bp = NULL;

		/*
		 * Get the freespace block corresponding to the data block
		 * that was just allocated.
		 */
		fbno = xfs_dir2_db_to_fdb(mp, dbno);
		if (unlikely(error = xfs_da_read_buf(tp, dp,
				xfs_dir2_db_to_da(mp, fbno), -2, &fbp,
				XFS_DATA_FORK))) {
			xfs_da_buf_done(dbp);
			return error;
  		}
		/*
		 * If there wasn't a freespace block, the read will
		 * return a NULL fbp.  Allocate and initialize a new one.
		 */
		if( fbp == NULL ) {
			if ((error = xfs_dir2_grow_inode(args, XFS_DIR2_FREE_SPACE,
							&fbno))) {
				return error;
			}

			if (unlikely(xfs_dir2_db_to_fdb(mp, dbno) != fbno)) {
				cmn_err(CE_ALERT,
					"xfs_dir2_node_addname_int: dir ino "
					"%llu needed freesp block %lld for\n"
					"  data block %lld, got %lld\n"
					"  ifbno %llu lastfbno %d\n",
					(unsigned long long)dp->i_ino,
					(long long)xfs_dir2_db_to_fdb(mp, dbno),
					(long long)dbno, (long long)fbno,
					(unsigned long long)ifbno, lastfbno);
				if (fblk) {
					cmn_err(CE_ALERT,
						" fblk 0x%p blkno %llu "
						"index %d magic 0x%x\n",
						fblk,
						(unsigned long long)fblk->blkno,
						fblk->index,
						fblk->magic);
				} else {
					cmn_err(CE_ALERT,
						" ... fblk is NULL\n");
				}
				XFS_ERROR_REPORT("xfs_dir2_node_addname_int",
						 XFS_ERRLEVEL_LOW, mp);
				return XFS_ERROR(EFSCORRUPTED);
			}

			/*
			 * Get a buffer for the new block.
			 */
			if ((error = xfs_da_get_buf(tp, dp,
						   xfs_dir2_db_to_da(mp, fbno),
						   -1, &fbp, XFS_DATA_FORK))) {
				return error;
			}
			ASSERT(fbp != NULL);

			/*
			 * Initialize the new block to be empty, and remember
			 * its first slot as our empty slot.
			 */
			free = fbp->data;
			free->hdr.magic = cpu_to_be32(XFS_DIR2_FREE_MAGIC);
			free->hdr.firstdb = cpu_to_be32(
				(fbno - XFS_DIR2_FREE_FIRSTDB(mp)) *
				XFS_DIR2_MAX_FREE_BESTS(mp));
			free->hdr.nvalid = 0;
			free->hdr.nused = 0;
		} else {
			free = fbp->data;
			ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
		}

		/*
		 * Set the freespace block index from the data block number.
		 */
		findex = xfs_dir2_db_to_fdindex(mp, dbno);
		/*
		 * If it's after the end of the current entries in the
		 * freespace block, extend that table.
		 */
		if (findex >= be32_to_cpu(free->hdr.nvalid)) {
			ASSERT(findex < XFS_DIR2_MAX_FREE_BESTS(mp));
			free->hdr.nvalid = cpu_to_be32(findex + 1);
			/*
			 * Tag new entry so nused will go up.
			 */
			free->bests[findex] = cpu_to_be16(NULLDATAOFF);
		}
		/*
		 * If this entry was for an empty data block
		 * (this should always be true) then update the header.
		 */
		if (be16_to_cpu(free->bests[findex]) == NULLDATAOFF) {
			be32_add_cpu(&free->hdr.nused, 1);
			xfs_dir2_free_log_header(tp, fbp);
		}
		/*
		 * Update the real value in the table.
		 * We haven't allocated the data entry yet so this will
		 * change again.
		 */
		data = dbp->data;
		free->bests[findex] = data->hdr.bestfree[0].length;
		logfree = 1;
	}
	/*
	 * We had a data block so we don't have to make a new one.
	 */
	else {
		/*
		 * If just checking, we succeeded.
		 */
		if (args->op_flags & XFS_DA_OP_JUSTCHECK) {
			if ((fblk == NULL || fblk->bp == NULL) && fbp != NULL)
				xfs_da_buf_done(fbp);
			return 0;
		}
		/*
		 * Read the data block in.
		 */
		if (unlikely(
		    error = xfs_da_read_buf(tp, dp, xfs_dir2_db_to_da(mp, dbno),
				-1, &dbp, XFS_DATA_FORK))) {
			if ((fblk == NULL || fblk->bp == NULL) && fbp != NULL)
				xfs_da_buf_done(fbp);
			return error;
		}
		data = dbp->data;
		logfree = 0;
	}
	ASSERT(be16_to_cpu(data->hdr.bestfree[0].length) >= length);
	/*
	 * Point to the existing unused space.
	 */
	dup = (xfs_dir2_data_unused_t *)
	      ((char *)data + be16_to_cpu(data->hdr.bestfree[0].offset));
	needscan = needlog = 0;
	/*
	 * Mark the first part of the unused space, inuse for us.
	 */
	xfs_dir2_data_use_free(tp, dbp, dup,
		(xfs_dir2_data_aoff_t)((char *)dup - (char *)data), length,
		&needlog, &needscan);
	/*
	 * Fill in the new entry and log it.
	 */
	dep = (xfs_dir2_data_entry_t *)dup;
	dep->inumber = cpu_to_be64(args->inumber);
	dep->namelen = args->namelen;
	memcpy(dep->name, args->name, dep->namelen);
	tagp = xfs_dir2_data_entry_tag_p(dep);
	*tagp = cpu_to_be16((char *)dep - (char *)data);
	xfs_dir2_data_log_entry(tp, dbp, dep);
	/*
	 * Rescan the block for bestfree if needed.
	 */
	if (needscan)
		xfs_dir2_data_freescan(mp, data, &needlog);
	/*
	 * Log the data block header if needed.
	 */
	if (needlog)
		xfs_dir2_data_log_header(tp, dbp);
	/*
	 * If the freespace entry is now wrong, update it.
	 */
	if (be16_to_cpu(free->bests[findex]) != be16_to_cpu(data->hdr.bestfree[0].length)) {
		free->bests[findex] = data->hdr.bestfree[0].length;
		logfree = 1;
	}
	/*
	 * Log the freespace entry if needed.
	 */
	if (logfree)
		xfs_dir2_free_log_bests(tp, fbp, findex, findex);
	/*
	 * If the caller didn't hand us the freespace block, drop it.
	 */
	if ((fblk == NULL || fblk->bp == NULL) && fbp != NULL)
		xfs_da_buf_done(fbp);
	/*
	 * Return the data block and offset in args, then drop the data block.
	 */
	args->blkno = (xfs_dablk_t)dbno;
	args->index = be16_to_cpu(*tagp);
	xfs_da_buf_done(dbp);
	return 0;
}

/*
 * Lookup an entry in a node-format directory.
 * All the real work happens in xfs_da_node_lookup_int.
 * The only real output is the inode number of the entry.
 */
int						/* error */
xfs_dir2_node_lookup(
	xfs_da_args_t	*args)			/* operation arguments */
{
	int		error;			/* error return value */
	int		i;			/* btree level */
	int		rval;			/* operation return value */
	xfs_da_state_t	*state;			/* btree cursor */

	xfs_dir2_trace_args("node_lookup", args);
	/*
	 * Allocate and initialize the btree cursor.
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_dirblksize;
	state->node_ents = state->mp->m_dir_node_ents;
	/*
	 * Fill in the path to the entry in the cursor.
	 */
	error = xfs_da_node_lookup_int(state, &rval);
	if (error)
		rval = error;
	else if (rval == ENOENT && args->cmpresult == XFS_CMP_CASE) {
		/* If a CI match, dup the actual name and return EEXIST */
		xfs_dir2_data_entry_t	*dep;

		dep = (xfs_dir2_data_entry_t *)((char *)state->extrablk.bp->
						data + state->extrablk.index);
		rval = xfs_dir_cilookup_result(args, dep->name, dep->namelen);
	}
	/*
	 * Release the btree blocks and leaf block.
	 */
	for (i = 0; i < state->path.active; i++) {
		xfs_da_brelse(args->trans, state->path.blk[i].bp);
		state->path.blk[i].bp = NULL;
	}
	/*
	 * Release the data block if we have it.
	 */
	if (state->extravalid && state->extrablk.bp) {
		xfs_da_brelse(args->trans, state->extrablk.bp);
		state->extrablk.bp = NULL;
	}
	xfs_da_state_free(state);
	return rval;
}

/*
 * Remove an entry from a node-format directory.
 */
int						/* error */
xfs_dir2_node_removename(
	xfs_da_args_t		*args)		/* operation arguments */
{
	xfs_da_state_blk_t	*blk;		/* leaf block */
	int			error;		/* error return value */
	int			rval;		/* operation return value */
	xfs_da_state_t		*state;		/* btree cursor */

	xfs_dir2_trace_args("node_removename", args);
	/*
	 * Allocate and initialize the btree cursor.
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_dirblksize;
	state->node_ents = state->mp->m_dir_node_ents;
	/*
	 * Look up the entry we're deleting, set up the cursor.
	 */
	error = xfs_da_node_lookup_int(state, &rval);
	if (error)
		rval = error;
	/*
	 * Didn't find it, upper layer screwed up.
	 */
	if (rval != EEXIST) {
		xfs_da_state_free(state);
		return rval;
	}
	blk = &state->path.blk[state->path.active - 1];
	ASSERT(blk->magic == XFS_DIR2_LEAFN_MAGIC);
	ASSERT(state->extravalid);
	/*
	 * Remove the leaf and data entries.
	 * Extrablk refers to the data block.
	 */
	error = xfs_dir2_leafn_remove(args, blk->bp, blk->index,
		&state->extrablk, &rval);
	if (error)
		return error;
	/*
	 * Fix the hash values up the btree.
	 */
	xfs_da_fixhashpath(state, &state->path);
	/*
	 * If we need to join leaf blocks, do it.
	 */
	if (rval && state->path.active > 1)
		error = xfs_da_join(state);
	/*
	 * If no errors so far, try conversion to leaf format.
	 */
	if (!error)
		error = xfs_dir2_node_to_leaf(state);
	xfs_da_state_free(state);
	return error;
}

/*
 * Replace an entry's inode number in a node-format directory.
 */
int						/* error */
xfs_dir2_node_replace(
	xfs_da_args_t		*args)		/* operation arguments */
{
	xfs_da_state_blk_t	*blk;		/* leaf block */
	xfs_dir2_data_t		*data;		/* data block structure */
	xfs_dir2_data_entry_t	*dep;		/* data entry changed */
	int			error;		/* error return value */
	int			i;		/* btree level */
	xfs_ino_t		inum;		/* new inode number */
	xfs_dir2_leaf_t		*leaf;		/* leaf structure */
	xfs_dir2_leaf_entry_t	*lep;		/* leaf entry being changed */
	int			rval;		/* internal return value */
	xfs_da_state_t		*state;		/* btree cursor */

	xfs_dir2_trace_args("node_replace", args);
	/*
	 * Allocate and initialize the btree cursor.
	 */
	state = xfs_da_state_alloc();
	state->args = args;
	state->mp = args->dp->i_mount;
	state->blocksize = state->mp->m_dirblksize;
	state->node_ents = state->mp->m_dir_node_ents;
	inum = args->inumber;
	/*
	 * Lookup the entry to change in the btree.
	 */
	error = xfs_da_node_lookup_int(state, &rval);
	if (error) {
		rval = error;
	}
	/*
	 * It should be found, since the vnodeops layer has looked it up
	 * and locked it.  But paranoia is good.
	 */
	if (rval == EEXIST) {
		/*
		 * Find the leaf entry.
		 */
		blk = &state->path.blk[state->path.active - 1];
		ASSERT(blk->magic == XFS_DIR2_LEAFN_MAGIC);
		leaf = blk->bp->data;
		lep = &leaf->ents[blk->index];
		ASSERT(state->extravalid);
		/*
		 * Point to the data entry.
		 */
		data = state->extrablk.bp->data;
		ASSERT(be32_to_cpu(data->hdr.magic) == XFS_DIR2_DATA_MAGIC);
		dep = (xfs_dir2_data_entry_t *)
		      ((char *)data +
		       xfs_dir2_dataptr_to_off(state->mp, be32_to_cpu(lep->address)));
		ASSERT(inum != be64_to_cpu(dep->inumber));
		/*
		 * Fill in the new inode number and log the entry.
		 */
		dep->inumber = cpu_to_be64(inum);
		xfs_dir2_data_log_entry(args->trans, state->extrablk.bp, dep);
		rval = 0;
	}
	/*
	 * Didn't find it, and we're holding a data block.  Drop it.
	 */
	else if (state->extravalid) {
		xfs_da_brelse(args->trans, state->extrablk.bp);
		state->extrablk.bp = NULL;
	}
	/*
	 * Release all the buffers in the cursor.
	 */
	for (i = 0; i < state->path.active; i++) {
		xfs_da_brelse(args->trans, state->path.blk[i].bp);
		state->path.blk[i].bp = NULL;
	}
	xfs_da_state_free(state);
	return rval;
}

/*
 * Trim off a trailing empty freespace block.
 * Return (in rvalp) 1 if we did it, 0 if not.
 */
int						/* error */
xfs_dir2_node_trim_free(
	xfs_da_args_t		*args,		/* operation arguments */
	xfs_fileoff_t		fo,		/* free block number */
	int			*rvalp)		/* out: did something */
{
	xfs_dabuf_t		*bp;		/* freespace buffer */
	xfs_inode_t		*dp;		/* incore directory inode */
	int			error;		/* error return code */
	xfs_dir2_free_t		*free;		/* freespace structure */
	xfs_mount_t		*mp;		/* filesystem mount point */
	xfs_trans_t		*tp;		/* transaction pointer */

	dp = args->dp;
	mp = dp->i_mount;
	tp = args->trans;
	/*
	 * Read the freespace block.
	 */
	if (unlikely(error = xfs_da_read_buf(tp, dp, (xfs_dablk_t)fo, -2, &bp,
			XFS_DATA_FORK))) {
		return error;
	}

	/*
	 * There can be holes in freespace.  If fo is a hole, there's
	 * nothing to do.
	 */
	if (bp == NULL) {
		return 0;
	}
	free = bp->data;
	ASSERT(be32_to_cpu(free->hdr.magic) == XFS_DIR2_FREE_MAGIC);
	/*
	 * If there are used entries, there's nothing to do.
	 */
	if (be32_to_cpu(free->hdr.nused) > 0) {
		xfs_da_brelse(tp, bp);
		*rvalp = 0;
		return 0;
	}
	/*
	 * Blow the block away.
	 */
	if ((error =
	    xfs_dir2_shrink_inode(args, xfs_dir2_da_to_db(mp, (xfs_dablk_t)fo),
		    bp))) {
		/*
		 * Can't fail with ENOSPC since that only happens with no
		 * space reservation, when breaking up an extent into two
		 * pieces.  This is the last block of an extent.
		 */
		ASSERT(error != ENOSPC);
		xfs_da_brelse(tp, bp);
		return error;
	}
	/*
	 * Return that we succeeded.
	 */
	*rvalp = 1;
	return 0;
}
