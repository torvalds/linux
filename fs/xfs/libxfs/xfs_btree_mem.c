/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_btree.h"
#include "xfs_error.h"
#include "xfs_buf_mem.h"
#include "xfs_btree_mem.h"
#include "xfs_ag.h"
#include "xfs_buf_item.h"
#include "xfs_trace.h"

/* Set the root of an in-memory btree. */
void
xfbtree_set_root(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*ptr,
	int				inc)
{
	ASSERT(cur->bc_ops->type == XFS_BTREE_TYPE_MEM);

	cur->bc_mem.xfbtree->root = *ptr;
	cur->bc_mem.xfbtree->nlevels += inc;
}

/* Initialize a pointer from the in-memory btree header. */
void
xfbtree_init_ptr_from_cur(
	struct xfs_btree_cur		*cur,
	union xfs_btree_ptr		*ptr)
{
	ASSERT(cur->bc_ops->type == XFS_BTREE_TYPE_MEM);

	*ptr = cur->bc_mem.xfbtree->root;
}

/* Duplicate an in-memory btree cursor. */
struct xfs_btree_cur *
xfbtree_dup_cursor(
	struct xfs_btree_cur		*cur)
{
	struct xfs_btree_cur		*ncur;

	ASSERT(cur->bc_ops->type == XFS_BTREE_TYPE_MEM);

	ncur = xfs_btree_alloc_cursor(cur->bc_mp, cur->bc_tp, cur->bc_ops,
			cur->bc_maxlevels, cur->bc_cache);
	ncur->bc_flags = cur->bc_flags;
	ncur->bc_nlevels = cur->bc_nlevels;
	ncur->bc_mem.xfbtree = cur->bc_mem.xfbtree;

	if (cur->bc_mem.pag)
		ncur->bc_mem.pag = xfs_perag_hold(cur->bc_mem.pag);

	return ncur;
}

/* Close the btree xfile and release all resources. */
void
xfbtree_destroy(
	struct xfbtree		*xfbt)
{
	xfs_buftarg_drain(xfbt->target);
}

/* Compute the number of bytes available for records. */
static inline unsigned int
xfbtree_rec_bytes(
	struct xfs_mount		*mp,
	const struct xfs_btree_ops	*ops)
{
	return XMBUF_BLOCKSIZE - XFS_BTREE_LBLOCK_CRC_LEN;
}

/* Initialize an empty leaf block as the btree root. */
STATIC int
xfbtree_init_leaf_block(
	struct xfs_mount		*mp,
	struct xfbtree			*xfbt,
	const struct xfs_btree_ops	*ops)
{
	struct xfs_buf			*bp;
	xfbno_t				bno = xfbt->highest_bno++;
	int				error;

	error = xfs_buf_get(xfbt->target, xfbno_to_daddr(bno), XFBNO_BBSIZE,
			&bp);
	if (error)
		return error;

	trace_xfbtree_create_root_buf(xfbt, bp);

	bp->b_ops = ops->buf_ops;
	xfs_btree_init_buf(mp, bp, ops, 0, 0, xfbt->owner);
	xfs_buf_relse(bp);

	xfbt->root.l = cpu_to_be64(bno);
	return 0;
}

/*
 * Create an in-memory btree root that can be used with the given xmbuf.
 * Callers must set xfbt->owner.
 */
int
xfbtree_init(
	struct xfs_mount		*mp,
	struct xfbtree			*xfbt,
	struct xfs_buftarg		*btp,
	const struct xfs_btree_ops	*ops)
{
	unsigned int			blocklen = xfbtree_rec_bytes(mp, ops);
	unsigned int			keyptr_len;
	int				error;

	/* Requires a long-format CRC-format btree */
	if (!xfs_has_crc(mp)) {
		ASSERT(xfs_has_crc(mp));
		return -EINVAL;
	}
	if (ops->ptr_len != XFS_BTREE_LONG_PTR_LEN) {
		ASSERT(ops->ptr_len == XFS_BTREE_LONG_PTR_LEN);
		return -EINVAL;
	}

	memset(xfbt, 0, sizeof(*xfbt));
	xfbt->target = btp;

	/* Set up min/maxrecs for this btree. */
	keyptr_len = ops->key_len + sizeof(__be64);
	xfbt->maxrecs[0] = blocklen / ops->rec_len;
	xfbt->maxrecs[1] = blocklen / keyptr_len;
	xfbt->minrecs[0] = xfbt->maxrecs[0] / 2;
	xfbt->minrecs[1] = xfbt->maxrecs[1] / 2;
	xfbt->highest_bno = 0;
	xfbt->nlevels = 1;

	/* Initialize the empty btree. */
	error = xfbtree_init_leaf_block(mp, xfbt, ops);
	if (error)
		goto err_freesp;

	trace_xfbtree_init(mp, xfbt, ops);

	return 0;

err_freesp:
	xfs_buftarg_drain(xfbt->target);
	return error;
}

/* Allocate a block to our in-memory btree. */
int
xfbtree_alloc_block(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_ptr	*start,
	union xfs_btree_ptr		*new,
	int				*stat)
{
	struct xfbtree			*xfbt = cur->bc_mem.xfbtree;
	xfbno_t				bno = xfbt->highest_bno++;

	ASSERT(cur->bc_ops->type == XFS_BTREE_TYPE_MEM);

	trace_xfbtree_alloc_block(xfbt, cur, bno);

	/* Fail if the block address exceeds the maximum for the buftarg. */
	if (!xfbtree_verify_bno(xfbt, bno)) {
		ASSERT(xfbtree_verify_bno(xfbt, bno));
		*stat = 0;
		return 0;
	}

	new->l = cpu_to_be64(bno);
	*stat = 1;
	return 0;
}

/* Free a block from our in-memory btree. */
int
xfbtree_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	struct xfbtree		*xfbt = cur->bc_mem.xfbtree;
	xfs_daddr_t		daddr = xfs_buf_daddr(bp);
	xfbno_t			bno = xfs_daddr_to_xfbno(daddr);

	ASSERT(cur->bc_ops->type == XFS_BTREE_TYPE_MEM);

	trace_xfbtree_free_block(xfbt, cur, bno);

	if (bno + 1 == xfbt->highest_bno)
		xfbt->highest_bno--;

	return 0;
}

/* Return the minimum number of records for a btree block. */
int
xfbtree_get_minrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	struct xfbtree		*xfbt = cur->bc_mem.xfbtree;

	return xfbt->minrecs[level != 0];
}

/* Return the maximum number of records for a btree block. */
int
xfbtree_get_maxrecs(
	struct xfs_btree_cur	*cur,
	int			level)
{
	struct xfbtree		*xfbt = cur->bc_mem.xfbtree;

	return xfbt->maxrecs[level != 0];
}

/* If this log item is a buffer item that came from the xfbtree, return it. */
static inline struct xfs_buf *
xfbtree_buf_match(
	struct xfbtree			*xfbt,
	const struct xfs_log_item	*lip)
{
	const struct xfs_buf_log_item	*bli;
	struct xfs_buf			*bp;

	if (lip->li_type != XFS_LI_BUF)
		return NULL;

	bli = container_of(lip, struct xfs_buf_log_item, bli_item);
	bp = bli->bli_buf;
	if (bp->b_target != xfbt->target)
		return NULL;

	return bp;
}

/*
 * Commit changes to the incore btree immediately by writing all dirty xfbtree
 * buffers to the backing xfile.  This detaches all xfbtree buffers from the
 * transaction, even on failure.  The buffer locks are dropped between the
 * delwri queue and submit, so the caller must synchronize btree access.
 *
 * Normally we'd let the buffers commit with the transaction and get written to
 * the xfile via the log, but online repair stages ephemeral btrees in memory
 * and uses the btree_staging functions to write new btrees to disk atomically.
 * The in-memory btree (and its backing store) are discarded at the end of the
 * repair phase, which means that xfbtree buffers cannot commit with the rest
 * of a transaction.
 *
 * In other words, online repair only needs the transaction to collect buffer
 * pointers and to avoid buffer deadlocks, not to guarantee consistency of
 * updates.
 */
int
xfbtree_trans_commit(
	struct xfbtree		*xfbt,
	struct xfs_trans	*tp)
{
	struct xfs_log_item	*lip, *n;
	bool			tp_dirty = false;
	int			error = 0;

	/*
	 * For each xfbtree buffer attached to the transaction, write the dirty
	 * buffers to the xfile and release them.
	 */
	list_for_each_entry_safe(lip, n, &tp->t_items, li_trans) {
		struct xfs_buf	*bp = xfbtree_buf_match(xfbt, lip);

		if (!bp) {
			if (test_bit(XFS_LI_DIRTY, &lip->li_flags))
				tp_dirty |= true;
			continue;
		}

		trace_xfbtree_trans_commit_buf(xfbt, bp);

		xmbuf_trans_bdetach(tp, bp);

		/*
		 * If the buffer fails verification, note the failure but
		 * continue walking the transaction items so that we remove all
		 * ephemeral btree buffers.
		 */
		if (!error)
			error = xmbuf_finalize(bp);

		xfs_buf_relse(bp);
	}

	/*
	 * Reset the transaction's dirty flag to reflect the dirty state of the
	 * log items that are still attached.
	 */
	tp->t_flags = (tp->t_flags & ~XFS_TRANS_DIRTY) |
			(tp_dirty ? XFS_TRANS_DIRTY : 0);

	return error;
}

/*
 * Cancel changes to the incore btree by detaching all the xfbtree buffers.
 * Changes are not undone, so callers must not access the btree ever again.
 */
void
xfbtree_trans_cancel(
	struct xfbtree		*xfbt,
	struct xfs_trans	*tp)
{
	struct xfs_log_item	*lip, *n;
	bool			tp_dirty = false;

	list_for_each_entry_safe(lip, n, &tp->t_items, li_trans) {
		struct xfs_buf	*bp = xfbtree_buf_match(xfbt, lip);

		if (!bp) {
			if (test_bit(XFS_LI_DIRTY, &lip->li_flags))
				tp_dirty |= true;
			continue;
		}

		trace_xfbtree_trans_cancel_buf(xfbt, bp);

		xmbuf_trans_bdetach(tp, bp);
		xfs_buf_relse(bp);
	}

	/*
	 * Reset the transaction's dirty flag to reflect the dirty state of the
	 * log items that are still attached.
	 */
	tp->t_flags = (tp->t_flags & ~XFS_TRANS_DIRTY) |
			(tp_dirty ? XFS_TRANS_DIRTY : 0);
}
