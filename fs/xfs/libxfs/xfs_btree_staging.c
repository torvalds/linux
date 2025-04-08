// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
#include "xfs_btree.h"
#include "xfs_trace.h"
#include "xfs_btree_staging.h"

/*
 * Staging Cursors and Fake Roots for Btrees
 * =========================================
 *
 * A staging btree cursor is a special type of btree cursor that callers must
 * use to construct a new btree index using the btree bulk loader code.  The
 * bulk loading code uses the staging btree cursor to abstract the details of
 * initializing new btree blocks and filling them with records or key/ptr
 * pairs.  Regular btree operations (e.g. queries and modifications) are not
 * supported with staging cursors, and callers must not invoke them.
 *
 * Fake root structures contain all the information about a btree that is under
 * construction by the bulk loading code.  Staging btree cursors point to fake
 * root structures instead of the usual AG header or inode structure.
 *
 * Callers are expected to initialize a fake root structure and pass it into
 * the _stage_cursor function for a specific btree type.  When bulk loading is
 * complete, callers should call the _commit_staged_btree function for that
 * specific btree type to commit the new btree into the filesystem.
 */

/*
 * Bulk Loading for AG Btrees
 * ==========================
 *
 * For a btree rooted in an AG header, pass a xbtree_afakeroot structure to the
 * staging cursor.  Callers should initialize this to zero.
 *
 * The _stage_cursor() function for a specific btree type should call
 * xfs_btree_stage_afakeroot to set up the in-memory cursor as a staging
 * cursor.  The corresponding _commit_staged_btree() function should log the
 * new root and call xfs_btree_commit_afakeroot() to transform the staging
 * cursor into a regular btree cursor.
 */

/*
 * Initialize a AG-rooted btree cursor with the given AG btree fake root.
 */
void
xfs_btree_stage_afakeroot(
	struct xfs_btree_cur		*cur,
	struct xbtree_afakeroot		*afake)
{
	ASSERT(!(cur->bc_flags & XFS_BTREE_STAGING));
	ASSERT(cur->bc_ops->type != XFS_BTREE_TYPE_INODE);
	ASSERT(cur->bc_tp == NULL);

	cur->bc_ag.afake = afake;
	cur->bc_nlevels = afake->af_levels;
	cur->bc_flags |= XFS_BTREE_STAGING;
}

/*
 * Transform an AG-rooted staging btree cursor back into a regular cursor by
 * substituting a real btree root for the fake one and restoring normal btree
 * cursor ops.  The caller must log the btree root change prior to calling
 * this.
 */
void
xfs_btree_commit_afakeroot(
	struct xfs_btree_cur		*cur,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp)
{
	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);
	ASSERT(cur->bc_tp == NULL);

	trace_xfs_btree_commit_afakeroot(cur);

	cur->bc_ag.afake = NULL;
	cur->bc_ag.agbp = agbp;
	cur->bc_flags &= ~XFS_BTREE_STAGING;
	cur->bc_tp = tp;
}

/*
 * Bulk Loading for Inode-Rooted Btrees
 * ====================================
 *
 * For a btree rooted in an inode fork, pass a xbtree_ifakeroot structure to
 * the staging cursor.  This structure should be initialized as follows:
 *
 * - if_fork_size field should be set to the number of bytes available to the
 *   fork in the inode.
 *
 * - if_fork should point to a freshly allocated struct xfs_ifork.
 *
 * - if_format should be set to the appropriate fork type (e.g.
 *   XFS_DINODE_FMT_BTREE).
 *
 * All other fields must be zero.
 *
 * The _stage_cursor() function for a specific btree type should call
 * xfs_btree_stage_ifakeroot to set up the in-memory cursor as a staging
 * cursor.  The corresponding _commit_staged_btree() function should log the
 * new root and call xfs_btree_commit_ifakeroot() to transform the staging
 * cursor into a regular btree cursor.
 */

/*
 * Initialize an inode-rooted btree cursor with the given inode btree fake
 * root.  The btree cursor's bc_ops will be overridden as needed to make the
 * staging functionality work.  If new_ops is not NULL, these new ops will be
 * passed out to the caller for further overriding.
 */
void
xfs_btree_stage_ifakeroot(
	struct xfs_btree_cur		*cur,
	struct xbtree_ifakeroot		*ifake)
{
	ASSERT(!(cur->bc_flags & XFS_BTREE_STAGING));
	ASSERT(cur->bc_ops->type == XFS_BTREE_TYPE_INODE);
	ASSERT(cur->bc_tp == NULL);

	cur->bc_ino.ifake = ifake;
	cur->bc_nlevels = ifake->if_levels;
	cur->bc_ino.forksize = ifake->if_fork_size;
	cur->bc_ino.whichfork = XFS_STAGING_FORK;
	cur->bc_flags |= XFS_BTREE_STAGING;
}

/*
 * Transform an inode-rooted staging btree cursor back into a regular cursor by
 * substituting a real btree root for the fake one and restoring normal btree
 * cursor ops.  The caller must log the btree root change prior to calling
 * this.
 */
void
xfs_btree_commit_ifakeroot(
	struct xfs_btree_cur		*cur,
	struct xfs_trans		*tp,
	int				whichfork)
{
	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);
	ASSERT(cur->bc_tp == NULL);

	trace_xfs_btree_commit_ifakeroot(cur);

	cur->bc_ino.ifake = NULL;
	cur->bc_ino.whichfork = whichfork;
	cur->bc_flags &= ~XFS_BTREE_STAGING;
	cur->bc_tp = tp;
}

/*
 * Bulk Loading of Staged Btrees
 * =============================
 *
 * This interface is used with a staged btree cursor to create a totally new
 * btree with a large number of records (i.e. more than what would fit in a
 * single root block).  When the creation is complete, the new root can be
 * linked atomically into the filesystem by committing the staged cursor.
 *
 * Creation of a new btree proceeds roughly as follows:
 *
 * The first step is to initialize an appropriate fake btree root structure and
 * then construct a staged btree cursor.  Refer to the block comments about
 * "Bulk Loading for AG Btrees" and "Bulk Loading for Inode-Rooted Btrees" for
 * more information about how to do this.
 *
 * The second step is to initialize a struct xfs_btree_bload context as
 * documented in the structure definition.
 *
 * The third step is to call xfs_btree_bload_compute_geometry to compute the
 * height of and the number of blocks needed to construct the btree.  See the
 * section "Computing the Geometry of the New Btree" for details about this
 * computation.
 *
 * In step four, the caller must allocate xfs_btree_bload.nr_blocks blocks and
 * save them for later use by ->claim_block().  Bulk loading requires all
 * blocks to be allocated beforehand to avoid ENOSPC failures midway through a
 * rebuild, and to minimize seek distances of the new btree.
 *
 * Step five is to call xfs_btree_bload() to start constructing the btree.
 *
 * The final step is to commit the staging btree cursor, which logs the new
 * btree root and turns the staging cursor into a regular cursor.  The caller
 * is responsible for cleaning up the previous btree blocks, if any.
 *
 * Computing the Geometry of the New Btree
 * =======================================
 *
 * The number of items placed in each btree block is computed via the following
 * algorithm: For leaf levels, the number of items for the level is nr_records
 * in the bload structure.  For node levels, the number of items for the level
 * is the number of blocks in the next lower level of the tree.  For each
 * level, the desired number of items per block is defined as:
 *
 * desired = max(minrecs, maxrecs - slack factor)
 *
 * The number of blocks for the level is defined to be:
 *
 * blocks = floor(nr_items / desired)
 *
 * Note this is rounded down so that the npb calculation below will never fall
 * below minrecs.  The number of items that will actually be loaded into each
 * btree block is defined as:
 *
 * npb =  nr_items / blocks
 *
 * Some of the leftmost blocks in the level will contain one extra record as
 * needed to handle uneven division.  If the number of records in any block
 * would exceed maxrecs for that level, blocks is incremented and npb is
 * recalculated.
 *
 * In other words, we compute the number of blocks needed to satisfy a given
 * loading level, then spread the items as evenly as possible.
 *
 * The height and number of fs blocks required to create the btree are computed
 * and returned via btree_height and nr_blocks.
 */

/*
 * Put a btree block that we're loading onto the ordered list and release it.
 * The btree blocks will be written to disk when bulk loading is finished.
 * If we reach the dirty buffer threshold, flush them to disk before
 * continuing.
 */
static int
xfs_btree_bload_drop_buf(
	struct xfs_btree_bload		*bbl,
	struct list_head		*buffers_list,
	struct xfs_buf			**bpp)
{
	struct xfs_buf			*bp = *bpp;
	int				error;

	if (!bp)
		return 0;

	/*
	 * Mark this buffer XBF_DONE (i.e. uptodate) so that a subsequent
	 * xfs_buf_read will not pointlessly reread the contents from the disk.
	 */
	bp->b_flags |= XBF_DONE;

	xfs_buf_delwri_queue_here(bp, buffers_list);
	xfs_buf_relse(bp);
	*bpp = NULL;
	bbl->nr_dirty++;

	if (!bbl->max_dirty || bbl->nr_dirty < bbl->max_dirty)
		return 0;

	error = xfs_buf_delwri_submit(buffers_list);
	if (error)
		return error;

	bbl->nr_dirty = 0;
	return 0;
}

/*
 * Allocate and initialize one btree block for bulk loading.
 *
 * The new btree block will have its level and numrecs fields set to the values
 * of the level and nr_this_block parameters, respectively.
 *
 * The caller should ensure that ptrp, bpp, and blockp refer to the left
 * sibling of the new block, if there is any.  On exit, ptrp, bpp, and blockp
 * will all point to the new block.
 */
STATIC int
xfs_btree_bload_prep_block(
	struct xfs_btree_cur		*cur,
	struct xfs_btree_bload		*bbl,
	struct list_head		*buffers_list,
	unsigned int			level,
	unsigned int			nr_this_block,
	union xfs_btree_ptr		*ptrp, /* in/out */
	struct xfs_buf			**bpp, /* in/out */
	struct xfs_btree_block		**blockp, /* in/out */
	void				*priv)
{
	union xfs_btree_ptr		new_ptr;
	struct xfs_buf			*new_bp;
	struct xfs_btree_block		*new_block;
	int				ret;

	if (xfs_btree_at_iroot(cur, level)) {
		struct xfs_ifork	*ifp = xfs_btree_ifork_ptr(cur);
		size_t			new_size;

		ASSERT(*bpp == NULL);

		/* Allocate a new incore btree root block. */
		new_size = bbl->iroot_size(cur, level, nr_this_block, priv);
		ifp->if_broot = kzalloc(new_size, GFP_KERNEL | __GFP_NOFAIL);
		ifp->if_broot_bytes = (int)new_size;

		/* Initialize it and send it out. */
		xfs_btree_init_block(cur->bc_mp, ifp->if_broot, cur->bc_ops,
				level, nr_this_block, cur->bc_ino.ip->i_ino);

		*bpp = NULL;
		*blockp = ifp->if_broot;
		xfs_btree_set_ptr_null(cur, ptrp);
		return 0;
	}

	/* Claim one of the caller's preallocated blocks. */
	xfs_btree_set_ptr_null(cur, &new_ptr);
	ret = bbl->claim_block(cur, &new_ptr, priv);
	if (ret)
		return ret;

	ASSERT(!xfs_btree_ptr_is_null(cur, &new_ptr));

	ret = xfs_btree_get_buf_block(cur, &new_ptr, &new_block, &new_bp);
	if (ret)
		return ret;

	/*
	 * The previous block (if any) is the left sibling of the new block,
	 * so set its right sibling pointer to the new block and drop it.
	 */
	if (*blockp)
		xfs_btree_set_sibling(cur, *blockp, &new_ptr, XFS_BB_RIGHTSIB);

	ret = xfs_btree_bload_drop_buf(bbl, buffers_list, bpp);
	if (ret)
		return ret;

	/* Initialize the new btree block. */
	xfs_btree_init_block_cur(cur, new_bp, level, nr_this_block);
	xfs_btree_set_sibling(cur, new_block, ptrp, XFS_BB_LEFTSIB);

	/* Set the out parameters. */
	*bpp = new_bp;
	*blockp = new_block;
	xfs_btree_copy_ptrs(cur, ptrp, &new_ptr, 1);
	return 0;
}

/* Load one leaf block. */
STATIC int
xfs_btree_bload_leaf(
	struct xfs_btree_cur		*cur,
	unsigned int			recs_this_block,
	xfs_btree_bload_get_records_fn	get_records,
	struct xfs_btree_block		*block,
	void				*priv)
{
	unsigned int			j = 1;
	int				ret;

	/* Fill the leaf block with records. */
	while (j <= recs_this_block) {
		ret = get_records(cur, j, block, recs_this_block - j + 1, priv);
		if (ret < 0)
			return ret;
		j += ret;
	}

	return 0;
}

/*
 * Load one node block with key/ptr pairs.
 *
 * child_ptr must point to a block within the next level down in the tree.  A
 * key/ptr entry will be created in the new node block to the block pointed to
 * by child_ptr.  On exit, child_ptr points to the next block on the child
 * level that needs processing.
 */
STATIC int
xfs_btree_bload_node(
	struct xfs_btree_cur	*cur,
	unsigned int		recs_this_block,
	union xfs_btree_ptr	*child_ptr,
	struct xfs_btree_block	*block)
{
	unsigned int		j;
	int			ret;

	/* Fill the node block with keys and pointers. */
	for (j = 1; j <= recs_this_block; j++) {
		union xfs_btree_key	child_key;
		union xfs_btree_ptr	*block_ptr;
		union xfs_btree_key	*block_key;
		struct xfs_btree_block	*child_block;
		struct xfs_buf		*child_bp;

		ASSERT(!xfs_btree_ptr_is_null(cur, child_ptr));

		/*
		 * Read the lower-level block in case the buffer for it has
		 * been reclaimed.  LRU refs will be set on the block, which is
		 * desirable if the new btree commits.
		 */
		ret = xfs_btree_read_buf_block(cur, child_ptr, 0, &child_block,
				&child_bp);
		if (ret)
			return ret;

		block_ptr = xfs_btree_ptr_addr(cur, j, block);
		xfs_btree_copy_ptrs(cur, block_ptr, child_ptr, 1);

		block_key = xfs_btree_key_addr(cur, j, block);
		xfs_btree_get_keys(cur, child_block, &child_key);
		xfs_btree_copy_keys(cur, block_key, &child_key, 1);

		xfs_btree_get_sibling(cur, child_block, child_ptr,
				XFS_BB_RIGHTSIB);
		xfs_buf_relse(child_bp);
	}

	return 0;
}

/*
 * Compute the maximum number of records (or keyptrs) per block that we want to
 * install at this level in the btree.  Caller is responsible for having set
 * @cur->bc_ino.forksize to the desired fork size, if appropriate.
 */
STATIC unsigned int
xfs_btree_bload_max_npb(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_bload	*bbl,
	unsigned int		level)
{
	unsigned int		ret;

	if (level == cur->bc_nlevels - 1 && cur->bc_ops->get_dmaxrecs)
		return cur->bc_ops->get_dmaxrecs(cur, level);

	ret = cur->bc_ops->get_maxrecs(cur, level);
	if (level == 0)
		ret -= bbl->leaf_slack;
	else
		ret -= bbl->node_slack;
	return ret;
}

/*
 * Compute the desired number of records (or keyptrs) per block that we want to
 * install at this level in the btree, which must be somewhere between minrecs
 * and max_npb.  The caller is free to install fewer records per block.
 */
STATIC unsigned int
xfs_btree_bload_desired_npb(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_bload	*bbl,
	unsigned int		level)
{
	unsigned int		npb = xfs_btree_bload_max_npb(cur, bbl, level);

	/* Root blocks are not subject to minrecs rules. */
	if (level == cur->bc_nlevels - 1)
		return max(1U, npb);

	return max_t(unsigned int, cur->bc_ops->get_minrecs(cur, level), npb);
}

/*
 * Compute the number of records to be stored in each block at this level and
 * the number of blocks for this level.  For leaf levels, we must populate an
 * empty root block even if there are no records, so we have to have at least
 * one block.
 */
STATIC void
xfs_btree_bload_level_geometry(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_bload	*bbl,
	unsigned int		level,
	uint64_t		nr_this_level,
	unsigned int		*avg_per_block,
	uint64_t		*blocks,
	uint64_t		*blocks_with_extra)
{
	uint64_t		npb;
	uint64_t		dontcare;
	unsigned int		desired_npb;
	unsigned int		maxnr;

	/*
	 * Compute the absolute maximum number of records that we can store in
	 * the ondisk block or inode root.
	 */
	if (cur->bc_ops->get_dmaxrecs)
		maxnr = cur->bc_ops->get_dmaxrecs(cur, level);
	else
		maxnr = cur->bc_ops->get_maxrecs(cur, level);

	/*
	 * Compute the number of blocks we need to fill each block with the
	 * desired number of records/keyptrs per block.  Because desired_npb
	 * could be minrecs, we use regular integer division (which rounds
	 * the block count down) so that in the next step the effective # of
	 * items per block will never be less than desired_npb.
	 */
	desired_npb = xfs_btree_bload_desired_npb(cur, bbl, level);
	*blocks = div64_u64_rem(nr_this_level, desired_npb, &dontcare);
	*blocks = max(1ULL, *blocks);

	/*
	 * Compute the number of records that we will actually put in each
	 * block, assuming that we want to spread the records evenly between
	 * the blocks.  Take care that the effective # of items per block (npb)
	 * won't exceed maxrecs even for the blocks that get an extra record,
	 * since desired_npb could be maxrecs, and in the previous step we
	 * rounded the block count down.
	 */
	npb = div64_u64_rem(nr_this_level, *blocks, blocks_with_extra);
	if (npb > maxnr || (npb == maxnr && *blocks_with_extra > 0)) {
		(*blocks)++;
		npb = div64_u64_rem(nr_this_level, *blocks, blocks_with_extra);
	}

	*avg_per_block = min_t(uint64_t, npb, nr_this_level);

	trace_xfs_btree_bload_level_geometry(cur, level, nr_this_level,
			*avg_per_block, desired_npb, *blocks,
			*blocks_with_extra);
}

/*
 * Ensure a slack value is appropriate for the btree.
 *
 * If the slack value is negative, set slack so that we fill the block to
 * halfway between minrecs and maxrecs.  Make sure the slack is never so large
 * that we can underflow minrecs.
 */
static void
xfs_btree_bload_ensure_slack(
	struct xfs_btree_cur	*cur,
	int			*slack,
	int			level)
{
	int			maxr;
	int			minr;

	maxr = cur->bc_ops->get_maxrecs(cur, level);
	minr = cur->bc_ops->get_minrecs(cur, level);

	/*
	 * If slack is negative, automatically set slack so that we load the
	 * btree block approximately halfway between minrecs and maxrecs.
	 * Generally, this will net us 75% loading.
	 */
	if (*slack < 0)
		*slack = maxr - ((maxr + minr) >> 1);

	*slack = min(*slack, maxr - minr);
}

/*
 * Prepare a btree cursor for a bulk load operation by computing the geometry
 * fields in bbl.  Caller must ensure that the btree cursor is a staging
 * cursor.  This function can be called multiple times.
 */
int
xfs_btree_bload_compute_geometry(
	struct xfs_btree_cur	*cur,
	struct xfs_btree_bload	*bbl,
	uint64_t		nr_records)
{
	const struct xfs_btree_ops *ops = cur->bc_ops;
	uint64_t		nr_blocks = 0;
	uint64_t		nr_this_level;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	/*
	 * Make sure that the slack values make sense for traditional leaf and
	 * node blocks.  Inode-rooted btrees will return different minrecs and
	 * maxrecs values for the root block (bc_nlevels == level - 1).  We're
	 * checking levels 0 and 1 here, so set bc_nlevels such that the btree
	 * code doesn't interpret either as the root level.
	 */
	cur->bc_nlevels = cur->bc_maxlevels - 1;
	xfs_btree_bload_ensure_slack(cur, &bbl->leaf_slack, 0);
	xfs_btree_bload_ensure_slack(cur, &bbl->node_slack, 1);

	bbl->nr_records = nr_this_level = nr_records;
	for (cur->bc_nlevels = 1; cur->bc_nlevels <= cur->bc_maxlevels;) {
		uint64_t	level_blocks;
		uint64_t	dontcare64;
		unsigned int	level = cur->bc_nlevels - 1;
		unsigned int	avg_per_block;

		xfs_btree_bload_level_geometry(cur, bbl, level, nr_this_level,
				&avg_per_block, &level_blocks, &dontcare64);

		if (ops->type == XFS_BTREE_TYPE_INODE) {
			/*
			 * If all the items we want to store at this level
			 * would fit in the inode root block, then we have our
			 * btree root and are done.
			 *
			 * Note that bmap btrees forbid records in the root.
			 */
			if ((level != 0 ||
			     (ops->geom_flags & XFS_BTGEO_IROOT_RECORDS)) &&
			    nr_this_level <= avg_per_block) {
				nr_blocks++;
				break;
			}

			/*
			 * Otherwise, we have to store all the items for this
			 * level in traditional btree blocks and therefore need
			 * another level of btree to point to those blocks.
			 *
			 * We have to re-compute the geometry for each level of
			 * an inode-rooted btree because the geometry differs
			 * between a btree root in an inode fork and a
			 * traditional btree block.
			 *
			 * This distinction is made in the btree code based on
			 * whether level == bc_nlevels - 1.  Based on the
			 * previous root block size check against the root
			 * block geometry, we know that we aren't yet ready to
			 * populate the root.  Increment bc_nevels and
			 * recalculate the geometry for a traditional
			 * block-based btree level.
			 */
			cur->bc_nlevels++;
			ASSERT(cur->bc_nlevels <= cur->bc_maxlevels);
			xfs_btree_bload_level_geometry(cur, bbl, level,
					nr_this_level, &avg_per_block,
					&level_blocks, &dontcare64);
		} else {
			/*
			 * If all the items we want to store at this level
			 * would fit in a single root block, we're done.
			 */
			if (nr_this_level <= avg_per_block) {
				nr_blocks++;
				break;
			}

			/* Otherwise, we need another level of btree. */
			cur->bc_nlevels++;
			ASSERT(cur->bc_nlevels <= cur->bc_maxlevels);
		}

		nr_blocks += level_blocks;
		nr_this_level = level_blocks;
	}

	if (cur->bc_nlevels > cur->bc_maxlevels)
		return -EOVERFLOW;

	bbl->btree_height = cur->bc_nlevels;
	if (ops->type == XFS_BTREE_TYPE_INODE)
		bbl->nr_blocks = nr_blocks - 1;
	else
		bbl->nr_blocks = nr_blocks;
	return 0;
}

/* Bulk load a btree given the parameters and geometry established in bbl. */
int
xfs_btree_bload(
	struct xfs_btree_cur		*cur,
	struct xfs_btree_bload		*bbl,
	void				*priv)
{
	struct list_head		buffers_list;
	union xfs_btree_ptr		child_ptr;
	union xfs_btree_ptr		ptr;
	struct xfs_buf			*bp = NULL;
	struct xfs_btree_block		*block = NULL;
	uint64_t			nr_this_level = bbl->nr_records;
	uint64_t			blocks;
	uint64_t			i;
	uint64_t			blocks_with_extra;
	uint64_t			total_blocks = 0;
	unsigned int			avg_per_block;
	unsigned int			level = 0;
	int				ret;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	INIT_LIST_HEAD(&buffers_list);
	cur->bc_nlevels = bbl->btree_height;
	xfs_btree_set_ptr_null(cur, &child_ptr);
	xfs_btree_set_ptr_null(cur, &ptr);
	bbl->nr_dirty = 0;

	xfs_btree_bload_level_geometry(cur, bbl, level, nr_this_level,
			&avg_per_block, &blocks, &blocks_with_extra);

	/* Load each leaf block. */
	for (i = 0; i < blocks; i++) {
		unsigned int		nr_this_block = avg_per_block;

		/*
		 * Due to rounding, btree blocks will not be evenly populated
		 * in most cases.  blocks_with_extra tells us how many blocks
		 * will receive an extra record to distribute the excess across
		 * the current level as evenly as possible.
		 */
		if (i < blocks_with_extra)
			nr_this_block++;

		ret = xfs_btree_bload_prep_block(cur, bbl, &buffers_list, level,
				nr_this_block, &ptr, &bp, &block, priv);
		if (ret)
			goto out;

		trace_xfs_btree_bload_block(cur, level, i, blocks, &ptr,
				nr_this_block);

		ret = xfs_btree_bload_leaf(cur, nr_this_block, bbl->get_records,
				block, priv);
		if (ret)
			goto out;

		/*
		 * Record the leftmost leaf pointer so we know where to start
		 * with the first node level.
		 */
		if (i == 0)
			xfs_btree_copy_ptrs(cur, &child_ptr, &ptr, 1);
	}
	total_blocks += blocks;

	ret = xfs_btree_bload_drop_buf(bbl, &buffers_list, &bp);
	if (ret)
		goto out;

	/* Populate the internal btree nodes. */
	for (level = 1; level < cur->bc_nlevels; level++) {
		union xfs_btree_ptr	first_ptr;

		nr_this_level = blocks;
		block = NULL;
		xfs_btree_set_ptr_null(cur, &ptr);

		xfs_btree_bload_level_geometry(cur, bbl, level, nr_this_level,
				&avg_per_block, &blocks, &blocks_with_extra);

		/* Load each node block. */
		for (i = 0; i < blocks; i++) {
			unsigned int	nr_this_block = avg_per_block;

			if (i < blocks_with_extra)
				nr_this_block++;

			ret = xfs_btree_bload_prep_block(cur, bbl,
					&buffers_list, level, nr_this_block,
					&ptr, &bp, &block, priv);
			if (ret)
				goto out;

			trace_xfs_btree_bload_block(cur, level, i, blocks,
					&ptr, nr_this_block);

			ret = xfs_btree_bload_node(cur, nr_this_block,
					&child_ptr, block);
			if (ret)
				goto out;

			/*
			 * Record the leftmost node pointer so that we know
			 * where to start the next node level above this one.
			 */
			if (i == 0)
				xfs_btree_copy_ptrs(cur, &first_ptr, &ptr, 1);
		}
		total_blocks += blocks;

		ret = xfs_btree_bload_drop_buf(bbl, &buffers_list, &bp);
		if (ret)
			goto out;

		xfs_btree_copy_ptrs(cur, &child_ptr, &first_ptr, 1);
	}

	/* Initialize the new root. */
	if (cur->bc_ops->type == XFS_BTREE_TYPE_INODE) {
		ASSERT(xfs_btree_ptr_is_null(cur, &ptr));
		cur->bc_ino.ifake->if_levels = cur->bc_nlevels;
		cur->bc_ino.ifake->if_blocks = total_blocks - 1;
	} else {
		cur->bc_ag.afake->af_root = be32_to_cpu(ptr.s);
		cur->bc_ag.afake->af_levels = cur->bc_nlevels;
		cur->bc_ag.afake->af_blocks = total_blocks;
	}

	/*
	 * Write the new blocks to disk.  If the ordered list isn't empty after
	 * that, then something went wrong and we have to fail.  This should
	 * never happen, but we'll check anyway.
	 */
	ret = xfs_buf_delwri_submit(&buffers_list);
	if (ret)
		goto out;
	if (!list_empty(&buffers_list)) {
		ASSERT(list_empty(&buffers_list));
		ret = -EIO;
	}

out:
	xfs_buf_delwri_cancel(&buffers_list);
	if (bp)
		xfs_buf_relse(bp);
	return ret;
}
