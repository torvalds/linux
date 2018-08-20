// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_inode_fork.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_attr_leaf.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/dabtree.h"

/* Directory/Attribute Btree */

/*
 * Check for da btree operation errors.  See the section about handling
 * operational errors in common.c.
 */
bool
xfs_scrub_da_process_error(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	int				*error)
{
	struct xfs_scrub_context	*sc = ds->sc;

	if (*error == 0)
		return true;

	switch (*error) {
	case -EDEADLOCK:
		/* Used to restart an op with deadlock avoidance. */
		trace_xfs_scrub_deadlock_retry(sc->ip, sc->sm, *error);
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Note the badness but don't abort. */
		sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
		*error = 0;
		/* fall through */
	default:
		trace_xfs_scrub_file_op_error(sc, ds->dargs.whichfork,
				xfs_dir2_da_to_db(ds->dargs.geo,
					ds->state->path.blk[level].blkno),
				*error, __return_address);
		break;
	}
	return false;
}

/*
 * Check for da btree corruption.  See the section about handling
 * operational errors in common.c.
 */
void
xfs_scrub_da_set_corrupt(
	struct xfs_scrub_da_btree	*ds,
	int				level)
{
	struct xfs_scrub_context	*sc = ds->sc;

	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;

	trace_xfs_scrub_fblock_error(sc, ds->dargs.whichfork,
			xfs_dir2_da_to_db(ds->dargs.geo,
				ds->state->path.blk[level].blkno),
			__return_address);
}

/* Find an entry at a certain level in a da btree. */
STATIC void *
xfs_scrub_da_btree_entry(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	int				rec)
{
	char				*ents;
	struct xfs_da_state_blk		*blk;
	void				*baddr;

	/* Dispatch the entry finding function. */
	blk = &ds->state->path.blk[level];
	baddr = blk->bp->b_addr;
	switch (blk->magic) {
	case XFS_ATTR_LEAF_MAGIC:
	case XFS_ATTR3_LEAF_MAGIC:
		ents = (char *)xfs_attr3_leaf_entryp(baddr);
		return ents + (rec * sizeof(struct xfs_attr_leaf_entry));
	case XFS_DIR2_LEAFN_MAGIC:
	case XFS_DIR3_LEAFN_MAGIC:
		ents = (char *)ds->dargs.dp->d_ops->leaf_ents_p(baddr);
		return ents + (rec * sizeof(struct xfs_dir2_leaf_entry));
	case XFS_DIR2_LEAF1_MAGIC:
	case XFS_DIR3_LEAF1_MAGIC:
		ents = (char *)ds->dargs.dp->d_ops->leaf_ents_p(baddr);
		return ents + (rec * sizeof(struct xfs_dir2_leaf_entry));
	case XFS_DA_NODE_MAGIC:
	case XFS_DA3_NODE_MAGIC:
		ents = (char *)ds->dargs.dp->d_ops->node_tree_p(baddr);
		return ents + (rec * sizeof(struct xfs_da_node_entry));
	}

	return NULL;
}

/* Scrub a da btree hash (key). */
int
xfs_scrub_da_btree_hash(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	__be32				*hashp)
{
	struct xfs_da_state_blk		*blks;
	struct xfs_da_node_entry	*entry;
	xfs_dahash_t			hash;
	xfs_dahash_t			parent_hash;

	/* Is this hash in order? */
	hash = be32_to_cpu(*hashp);
	if (hash < ds->hashes[level])
		xfs_scrub_da_set_corrupt(ds, level);
	ds->hashes[level] = hash;

	if (level == 0)
		return 0;

	/* Is this hash no larger than the parent hash? */
	blks = ds->state->path.blk;
	entry = xfs_scrub_da_btree_entry(ds, level - 1, blks[level - 1].index);
	parent_hash = be32_to_cpu(entry->hashval);
	if (parent_hash < hash)
		xfs_scrub_da_set_corrupt(ds, level);

	return 0;
}

/*
 * Check a da btree pointer.  Returns true if it's ok to use this
 * pointer.
 */
STATIC bool
xfs_scrub_da_btree_ptr_ok(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	xfs_dablk_t			blkno)
{
	if (blkno < ds->lowest || (ds->highest != 0 && blkno >= ds->highest)) {
		xfs_scrub_da_set_corrupt(ds, level);
		return false;
	}

	return true;
}

/*
 * The da btree scrubber can handle leaf1 blocks as a degenerate
 * form of leafn blocks.  Since the regular da code doesn't handle
 * leaf1, we must multiplex the verifiers.
 */
static void
xfs_scrub_da_btree_read_verify(
	struct xfs_buf		*bp)
{
	struct xfs_da_blkinfo	*info = bp->b_addr;

	switch (be16_to_cpu(info->magic)) {
	case XFS_DIR2_LEAF1_MAGIC:
	case XFS_DIR3_LEAF1_MAGIC:
		bp->b_ops = &xfs_dir3_leaf1_buf_ops;
		bp->b_ops->verify_read(bp);
		return;
	default:
		/*
		 * xfs_da3_node_buf_ops already know how to handle
		 * DA*_NODE, ATTR*_LEAF, and DIR*_LEAFN blocks.
		 */
		bp->b_ops = &xfs_da3_node_buf_ops;
		bp->b_ops->verify_read(bp);
		return;
	}
}
static void
xfs_scrub_da_btree_write_verify(
	struct xfs_buf		*bp)
{
	struct xfs_da_blkinfo	*info = bp->b_addr;

	switch (be16_to_cpu(info->magic)) {
	case XFS_DIR2_LEAF1_MAGIC:
	case XFS_DIR3_LEAF1_MAGIC:
		bp->b_ops = &xfs_dir3_leaf1_buf_ops;
		bp->b_ops->verify_write(bp);
		return;
	default:
		/*
		 * xfs_da3_node_buf_ops already know how to handle
		 * DA*_NODE, ATTR*_LEAF, and DIR*_LEAFN blocks.
		 */
		bp->b_ops = &xfs_da3_node_buf_ops;
		bp->b_ops->verify_write(bp);
		return;
	}
}
static void *
xfs_scrub_da_btree_verify(
	struct xfs_buf		*bp)
{
	struct xfs_da_blkinfo	*info = bp->b_addr;

	switch (be16_to_cpu(info->magic)) {
	case XFS_DIR2_LEAF1_MAGIC:
	case XFS_DIR3_LEAF1_MAGIC:
		bp->b_ops = &xfs_dir3_leaf1_buf_ops;
		return bp->b_ops->verify_struct(bp);
	default:
		bp->b_ops = &xfs_da3_node_buf_ops;
		return bp->b_ops->verify_struct(bp);
	}
}

static const struct xfs_buf_ops xfs_scrub_da_btree_buf_ops = {
	.name = "xfs_scrub_da_btree",
	.verify_read = xfs_scrub_da_btree_read_verify,
	.verify_write = xfs_scrub_da_btree_write_verify,
	.verify_struct = xfs_scrub_da_btree_verify,
};

/* Check a block's sibling. */
STATIC int
xfs_scrub_da_btree_block_check_sibling(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	int				direction,
	xfs_dablk_t			sibling)
{
	int				retval;
	int				error;

	memcpy(&ds->state->altpath, &ds->state->path,
			sizeof(ds->state->altpath));

	/*
	 * If the pointer is null, we shouldn't be able to move the upper
	 * level pointer anywhere.
	 */
	if (sibling == 0) {
		error = xfs_da3_path_shift(ds->state, &ds->state->altpath,
				direction, false, &retval);
		if (error == 0 && retval == 0)
			xfs_scrub_da_set_corrupt(ds, level);
		error = 0;
		goto out;
	}

	/* Move the alternate cursor one block in the direction given. */
	error = xfs_da3_path_shift(ds->state, &ds->state->altpath,
			direction, false, &retval);
	if (!xfs_scrub_da_process_error(ds, level, &error))
		return error;
	if (retval) {
		xfs_scrub_da_set_corrupt(ds, level);
		return error;
	}
	if (ds->state->altpath.blk[level].bp)
		xfs_scrub_buffer_recheck(ds->sc,
				ds->state->altpath.blk[level].bp);

	/* Compare upper level pointer to sibling pointer. */
	if (ds->state->altpath.blk[level].blkno != sibling)
		xfs_scrub_da_set_corrupt(ds, level);
	xfs_trans_brelse(ds->dargs.trans, ds->state->altpath.blk[level].bp);
out:
	return error;
}

/* Check a block's sibling pointers. */
STATIC int
xfs_scrub_da_btree_block_check_siblings(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	struct xfs_da_blkinfo		*hdr)
{
	xfs_dablk_t			forw;
	xfs_dablk_t			back;
	int				error = 0;

	forw = be32_to_cpu(hdr->forw);
	back = be32_to_cpu(hdr->back);

	/* Top level blocks should not have sibling pointers. */
	if (level == 0) {
		if (forw != 0 || back != 0)
			xfs_scrub_da_set_corrupt(ds, level);
		return 0;
	}

	/*
	 * Check back (left) and forw (right) pointers.  These functions
	 * absorb error codes for us.
	 */
	error = xfs_scrub_da_btree_block_check_sibling(ds, level, 0, back);
	if (error)
		goto out;
	error = xfs_scrub_da_btree_block_check_sibling(ds, level, 1, forw);

out:
	memset(&ds->state->altpath, 0, sizeof(ds->state->altpath));
	return error;
}

/* Load a dir/attribute block from a btree. */
STATIC int
xfs_scrub_da_btree_block(
	struct xfs_scrub_da_btree	*ds,
	int				level,
	xfs_dablk_t			blkno)
{
	struct xfs_da_state_blk		*blk;
	struct xfs_da_intnode		*node;
	struct xfs_da_node_entry	*btree;
	struct xfs_da3_blkinfo		*hdr3;
	struct xfs_da_args		*dargs = &ds->dargs;
	struct xfs_inode		*ip = ds->dargs.dp;
	xfs_ino_t			owner;
	int				*pmaxrecs;
	struct xfs_da3_icnode_hdr	nodehdr;
	int				error = 0;

	blk = &ds->state->path.blk[level];
	ds->state->path.active = level + 1;

	/* Release old block. */
	if (blk->bp) {
		xfs_trans_brelse(dargs->trans, blk->bp);
		blk->bp = NULL;
	}

	/* Check the pointer. */
	blk->blkno = blkno;
	if (!xfs_scrub_da_btree_ptr_ok(ds, level, blkno))
		goto out_nobuf;

	/* Read the buffer. */
	error = xfs_da_read_buf(dargs->trans, dargs->dp, blk->blkno, -2,
			&blk->bp, dargs->whichfork,
			&xfs_scrub_da_btree_buf_ops);
	if (!xfs_scrub_da_process_error(ds, level, &error))
		goto out_nobuf;
	if (blk->bp)
		xfs_scrub_buffer_recheck(ds->sc, blk->bp);

	/*
	 * We didn't find a dir btree root block, which means that
	 * there's no LEAF1/LEAFN tree (at least not where it's supposed
	 * to be), so jump out now.
	 */
	if (ds->dargs.whichfork == XFS_DATA_FORK && level == 0 &&
			blk->bp == NULL)
		goto out_nobuf;

	/* It's /not/ ok for attr trees not to have a da btree. */
	if (blk->bp == NULL) {
		xfs_scrub_da_set_corrupt(ds, level);
		goto out_nobuf;
	}

	hdr3 = blk->bp->b_addr;
	blk->magic = be16_to_cpu(hdr3->hdr.magic);
	pmaxrecs = &ds->maxrecs[level];

	/* We only started zeroing the header on v5 filesystems. */
	if (xfs_sb_version_hascrc(&ds->sc->mp->m_sb) && hdr3->hdr.pad)
		xfs_scrub_da_set_corrupt(ds, level);

	/* Check the owner. */
	if (xfs_sb_version_hascrc(&ip->i_mount->m_sb)) {
		owner = be64_to_cpu(hdr3->owner);
		if (owner != ip->i_ino)
			xfs_scrub_da_set_corrupt(ds, level);
	}

	/* Check the siblings. */
	error = xfs_scrub_da_btree_block_check_siblings(ds, level, &hdr3->hdr);
	if (error)
		goto out;

	/* Interpret the buffer. */
	switch (blk->magic) {
	case XFS_ATTR_LEAF_MAGIC:
	case XFS_ATTR3_LEAF_MAGIC:
		xfs_trans_buf_set_type(dargs->trans, blk->bp,
				XFS_BLFT_ATTR_LEAF_BUF);
		blk->magic = XFS_ATTR_LEAF_MAGIC;
		blk->hashval = xfs_attr_leaf_lasthash(blk->bp, pmaxrecs);
		if (ds->tree_level != 0)
			xfs_scrub_da_set_corrupt(ds, level);
		break;
	case XFS_DIR2_LEAFN_MAGIC:
	case XFS_DIR3_LEAFN_MAGIC:
		xfs_trans_buf_set_type(dargs->trans, blk->bp,
				XFS_BLFT_DIR_LEAFN_BUF);
		blk->magic = XFS_DIR2_LEAFN_MAGIC;
		blk->hashval = xfs_dir2_leaf_lasthash(ip, blk->bp, pmaxrecs);
		if (ds->tree_level != 0)
			xfs_scrub_da_set_corrupt(ds, level);
		break;
	case XFS_DIR2_LEAF1_MAGIC:
	case XFS_DIR3_LEAF1_MAGIC:
		xfs_trans_buf_set_type(dargs->trans, blk->bp,
				XFS_BLFT_DIR_LEAF1_BUF);
		blk->magic = XFS_DIR2_LEAF1_MAGIC;
		blk->hashval = xfs_dir2_leaf_lasthash(ip, blk->bp, pmaxrecs);
		if (ds->tree_level != 0)
			xfs_scrub_da_set_corrupt(ds, level);
		break;
	case XFS_DA_NODE_MAGIC:
	case XFS_DA3_NODE_MAGIC:
		xfs_trans_buf_set_type(dargs->trans, blk->bp,
				XFS_BLFT_DA_NODE_BUF);
		blk->magic = XFS_DA_NODE_MAGIC;
		node = blk->bp->b_addr;
		ip->d_ops->node_hdr_from_disk(&nodehdr, node);
		btree = ip->d_ops->node_tree_p(node);
		*pmaxrecs = nodehdr.count;
		blk->hashval = be32_to_cpu(btree[*pmaxrecs - 1].hashval);
		if (level == 0) {
			if (nodehdr.level >= XFS_DA_NODE_MAXDEPTH) {
				xfs_scrub_da_set_corrupt(ds, level);
				goto out_freebp;
			}
			ds->tree_level = nodehdr.level;
		} else {
			if (ds->tree_level != nodehdr.level) {
				xfs_scrub_da_set_corrupt(ds, level);
				goto out_freebp;
			}
		}

		/* XXX: Check hdr3.pad32 once we know how to fix it. */
		break;
	default:
		xfs_scrub_da_set_corrupt(ds, level);
		goto out_freebp;
	}

out:
	return error;
out_freebp:
	xfs_trans_brelse(dargs->trans, blk->bp);
	blk->bp = NULL;
out_nobuf:
	blk->blkno = 0;
	return error;
}

/* Visit all nodes and leaves of a da btree. */
int
xfs_scrub_da_btree(
	struct xfs_scrub_context	*sc,
	int				whichfork,
	xfs_scrub_da_btree_rec_fn	scrub_fn,
	void				*private)
{
	struct xfs_scrub_da_btree	ds = {};
	struct xfs_mount		*mp = sc->mp;
	struct xfs_da_state_blk		*blks;
	struct xfs_da_node_entry	*key;
	void				*rec;
	xfs_dablk_t			blkno;
	int				level;
	int				error;

	/* Skip short format data structures; no btree to scan. */
	if (XFS_IFORK_FORMAT(sc->ip, whichfork) != XFS_DINODE_FMT_EXTENTS &&
	    XFS_IFORK_FORMAT(sc->ip, whichfork) != XFS_DINODE_FMT_BTREE)
		return 0;

	/* Set up initial da state. */
	ds.dargs.dp = sc->ip;
	ds.dargs.whichfork = whichfork;
	ds.dargs.trans = sc->tp;
	ds.dargs.op_flags = XFS_DA_OP_OKNOENT;
	ds.state = xfs_da_state_alloc();
	ds.state->args = &ds.dargs;
	ds.state->mp = mp;
	ds.sc = sc;
	ds.private = private;
	if (whichfork == XFS_ATTR_FORK) {
		ds.dargs.geo = mp->m_attr_geo;
		ds.lowest = 0;
		ds.highest = 0;
	} else {
		ds.dargs.geo = mp->m_dir_geo;
		ds.lowest = ds.dargs.geo->leafblk;
		ds.highest = ds.dargs.geo->freeblk;
	}
	blkno = ds.lowest;
	level = 0;

	/* Find the root of the da tree, if present. */
	blks = ds.state->path.blk;
	error = xfs_scrub_da_btree_block(&ds, level, blkno);
	if (error)
		goto out_state;
	/*
	 * We didn't find a block at ds.lowest, which means that there's
	 * no LEAF1/LEAFN tree (at least not where it's supposed to be),
	 * so jump out now.
	 */
	if (blks[level].bp == NULL)
		goto out_state;

	blks[level].index = 0;
	while (level >= 0 && level < XFS_DA_NODE_MAXDEPTH) {
		/* Handle leaf block. */
		if (blks[level].magic != XFS_DA_NODE_MAGIC) {
			/* End of leaf, pop back towards the root. */
			if (blks[level].index >= ds.maxrecs[level]) {
				if (level > 0)
					blks[level - 1].index++;
				ds.tree_level++;
				level--;
				continue;
			}

			/* Dispatch record scrubbing. */
			rec = xfs_scrub_da_btree_entry(&ds, level,
					blks[level].index);
			error = scrub_fn(&ds, level, rec);
			if (error)
				break;
			if (xfs_scrub_should_terminate(sc, &error) ||
			    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
				break;

			blks[level].index++;
			continue;
		}


		/* End of node, pop back towards the root. */
		if (blks[level].index >= ds.maxrecs[level]) {
			if (level > 0)
				blks[level - 1].index++;
			ds.tree_level++;
			level--;
			continue;
		}

		/* Hashes in order for scrub? */
		key = xfs_scrub_da_btree_entry(&ds, level, blks[level].index);
		error = xfs_scrub_da_btree_hash(&ds, level, &key->hashval);
		if (error)
			goto out;

		/* Drill another level deeper. */
		blkno = be32_to_cpu(key->before);
		level++;
		ds.tree_level--;
		error = xfs_scrub_da_btree_block(&ds, level, blkno);
		if (error)
			goto out;
		if (blks[level].bp == NULL)
			goto out;

		blks[level].index = 0;
	}

out:
	/* Release all the buffers we're tracking. */
	for (level = 0; level < XFS_DA_NODE_MAXDEPTH; level++) {
		if (blks[level].bp == NULL)
			continue;
		xfs_trans_brelse(sc->tp, blks[level].bp);
		blks[level].bp = NULL;
	}

out_state:
	xfs_da_state_free(ds.state);
	return error;
}
