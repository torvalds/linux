// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_log.h"
#include "xfs_rmap.h"
#include "xfs_refcount.h"
#include "xfs_bmap.h"
#include "xfs_alloc.h"

static struct kmem_cache	*xfs_defer_pending_cache;

/*
 * Deferred Operations in XFS
 *
 * Due to the way locking rules work in XFS, certain transactions (block
 * mapping and unmapping, typically) have permanent reservations so that
 * we can roll the transaction to adhere to AG locking order rules and
 * to unlock buffers between metadata updates.  Prior to rmap/reflink,
 * the mapping code had a mechanism to perform these deferrals for
 * extents that were going to be freed; this code makes that facility
 * more generic.
 *
 * When adding the reverse mapping and reflink features, it became
 * necessary to perform complex remapping multi-transactions to comply
 * with AG locking order rules, and to be able to spread a single
 * refcount update operation (an operation on an n-block extent can
 * update as many as n records!) among multiple transactions.  XFS can
 * roll a transaction to facilitate this, but using this facility
 * requires us to log "intent" items in case log recovery needs to
 * redo the operation, and to log "done" items to indicate that redo
 * is not necessary.
 *
 * Deferred work is tracked in xfs_defer_pending items.  Each pending
 * item tracks one type of deferred work.  Incoming work items (which
 * have not yet had an intent logged) are attached to a pending item
 * on the dop_intake list, where they wait for the caller to finish
 * the deferred operations.
 *
 * Finishing a set of deferred operations is an involved process.  To
 * start, we define "rolling a deferred-op transaction" as follows:
 *
 * > For each xfs_defer_pending item on the dop_intake list,
 *   - Sort the work items in AG order.  XFS locking
 *     order rules require us to lock buffers in AG order.
 *   - Create a log intent item for that type.
 *   - Attach it to the pending item.
 *   - Move the pending item from the dop_intake list to the
 *     dop_pending list.
 * > Roll the transaction.
 *
 * NOTE: To avoid exceeding the transaction reservation, we limit the
 * number of items that we attach to a given xfs_defer_pending.
 *
 * The actual finishing process looks like this:
 *
 * > For each xfs_defer_pending in the dop_pending list,
 *   - Roll the deferred-op transaction as above.
 *   - Create a log done item for that type, and attach it to the
 *     log intent item.
 *   - For each work item attached to the log intent item,
 *     * Perform the described action.
 *     * Attach the work item to the log done item.
 *     * If the result of doing the work was -EAGAIN, ->finish work
 *       wants a new transaction.  See the "Requesting a Fresh
 *       Transaction while Finishing Deferred Work" section below for
 *       details.
 *
 * The key here is that we must log an intent item for all pending
 * work items every time we roll the transaction, and that we must log
 * a done item as soon as the work is completed.  With this mechanism
 * we can perform complex remapping operations, chaining intent items
 * as needed.
 *
 * Requesting a Fresh Transaction while Finishing Deferred Work
 *
 * If ->finish_item decides that it needs a fresh transaction to
 * finish the work, it must ask its caller (xfs_defer_finish) for a
 * continuation.  The most likely cause of this circumstance are the
 * refcount adjust functions deciding that they've logged enough items
 * to be at risk of exceeding the transaction reservation.
 *
 * To get a fresh transaction, we want to log the existing log done
 * item to prevent the log intent item from replaying, immediately log
 * a new log intent item with the unfinished work items, roll the
 * transaction, and re-call ->finish_item wherever it left off.  The
 * log done item and the new log intent item must be in the same
 * transaction or atomicity cannot be guaranteed; defer_finish ensures
 * that this happens.
 *
 * This requires some coordination between ->finish_item and
 * defer_finish.  Upon deciding to request a new transaction,
 * ->finish_item should update the current work item to reflect the
 * unfinished work.  Next, it should reset the log done item's list
 * count to the number of items finished, and return -EAGAIN.
 * defer_finish sees the -EAGAIN, logs the new log intent item
 * with the remaining work items, and leaves the xfs_defer_pending
 * item at the head of the dop_work queue.  Then it rolls the
 * transaction and picks up processing where it left off.  It is
 * required that ->finish_item must be careful to leave enough
 * transaction reservation to fit the new log intent item.
 *
 * This is an example of remapping the extent (E, E+B) into file X at
 * offset A and dealing with the extent (C, C+B) already being mapped
 * there:
 * +-------------------------------------------------+
 * | Unmap file X startblock C offset A length B     | t0
 * | Intent to reduce refcount for extent (C, B)     |
 * | Intent to remove rmap (X, C, A, B)              |
 * | Intent to free extent (D, 1) (bmbt block)       |
 * | Intent to map (X, A, B) at startblock E         |
 * +-------------------------------------------------+
 * | Map file X startblock E offset A length B       | t1
 * | Done mapping (X, E, A, B)                       |
 * | Intent to increase refcount for extent (E, B)   |
 * | Intent to add rmap (X, E, A, B)                 |
 * +-------------------------------------------------+
 * | Reduce refcount for extent (C, B)               | t2
 * | Done reducing refcount for extent (C, 9)        |
 * | Intent to reduce refcount for extent (C+9, B-9) |
 * | (ran out of space after 9 refcount updates)     |
 * +-------------------------------------------------+
 * | Reduce refcount for extent (C+9, B+9)           | t3
 * | Done reducing refcount for extent (C+9, B-9)    |
 * | Increase refcount for extent (E, B)             |
 * | Done increasing refcount for extent (E, B)      |
 * | Intent to free extent (C, B)                    |
 * | Intent to free extent (F, 1) (refcountbt block) |
 * | Intent to remove rmap (F, 1, REFC)              |
 * +-------------------------------------------------+
 * | Remove rmap (X, C, A, B)                        | t4
 * | Done removing rmap (X, C, A, B)                 |
 * | Add rmap (X, E, A, B)                           |
 * | Done adding rmap (X, E, A, B)                   |
 * | Remove rmap (F, 1, REFC)                        |
 * | Done removing rmap (F, 1, REFC)                 |
 * +-------------------------------------------------+
 * | Free extent (C, B)                              | t5
 * | Done freeing extent (C, B)                      |
 * | Free extent (D, 1)                              |
 * | Done freeing extent (D, 1)                      |
 * | Free extent (F, 1)                              |
 * | Done freeing extent (F, 1)                      |
 * +-------------------------------------------------+
 *
 * If we should crash before t2 commits, log recovery replays
 * the following intent items:
 *
 * - Intent to reduce refcount for extent (C, B)
 * - Intent to remove rmap (X, C, A, B)
 * - Intent to free extent (D, 1) (bmbt block)
 * - Intent to increase refcount for extent (E, B)
 * - Intent to add rmap (X, E, A, B)
 *
 * In the process of recovering, it should also generate and take care
 * of these intent items:
 *
 * - Intent to free extent (C, B)
 * - Intent to free extent (F, 1) (refcountbt block)
 * - Intent to remove rmap (F, 1, REFC)
 *
 * Note that the continuation requested between t2 and t3 is likely to
 * reoccur.
 */

static const struct xfs_defer_op_type *defer_op_types[] = {
	[XFS_DEFER_OPS_TYPE_BMAP]	= &xfs_bmap_update_defer_type,
	[XFS_DEFER_OPS_TYPE_REFCOUNT]	= &xfs_refcount_update_defer_type,
	[XFS_DEFER_OPS_TYPE_RMAP]	= &xfs_rmap_update_defer_type,
	[XFS_DEFER_OPS_TYPE_FREE]	= &xfs_extent_free_defer_type,
	[XFS_DEFER_OPS_TYPE_AGFL_FREE]	= &xfs_agfl_free_defer_type,
};

static void
xfs_defer_create_intent(
	struct xfs_trans		*tp,
	struct xfs_defer_pending	*dfp,
	bool				sort)
{
	const struct xfs_defer_op_type	*ops = defer_op_types[dfp->dfp_type];

	if (!dfp->dfp_intent)
		dfp->dfp_intent = ops->create_intent(tp, &dfp->dfp_work,
						     dfp->dfp_count, sort);
}

/*
 * For each pending item in the intake list, log its intent item and the
 * associated extents, then add the entire intake list to the end of
 * the pending list.
 */
STATIC void
xfs_defer_create_intents(
	struct xfs_trans		*tp)
{
	struct xfs_defer_pending	*dfp;

	list_for_each_entry(dfp, &tp->t_dfops, dfp_list) {
		trace_xfs_defer_create_intent(tp->t_mountp, dfp);
		xfs_defer_create_intent(tp, dfp, true);
	}
}

/* Abort all the intents that were committed. */
STATIC void
xfs_defer_trans_abort(
	struct xfs_trans		*tp,
	struct list_head		*dop_pending)
{
	struct xfs_defer_pending	*dfp;
	const struct xfs_defer_op_type	*ops;

	trace_xfs_defer_trans_abort(tp, _RET_IP_);

	/* Abort intent items that don't have a done item. */
	list_for_each_entry(dfp, dop_pending, dfp_list) {
		ops = defer_op_types[dfp->dfp_type];
		trace_xfs_defer_pending_abort(tp->t_mountp, dfp);
		if (dfp->dfp_intent && !dfp->dfp_done) {
			ops->abort_intent(dfp->dfp_intent);
			dfp->dfp_intent = NULL;
		}
	}
}

/*
 * Capture resources that the caller said not to release ("held") when the
 * transaction commits.  Caller is responsible for zero-initializing @dres.
 */
static int
xfs_defer_save_resources(
	struct xfs_defer_resources	*dres,
	struct xfs_trans		*tp)
{
	struct xfs_buf_log_item		*bli;
	struct xfs_inode_log_item	*ili;
	struct xfs_log_item		*lip;

	BUILD_BUG_ON(NBBY * sizeof(dres->dr_ordered) < XFS_DEFER_OPS_NR_BUFS);

	list_for_each_entry(lip, &tp->t_items, li_trans) {
		switch (lip->li_type) {
		case XFS_LI_BUF:
			bli = container_of(lip, struct xfs_buf_log_item,
					   bli_item);
			if (bli->bli_flags & XFS_BLI_HOLD) {
				if (dres->dr_bufs >= XFS_DEFER_OPS_NR_BUFS) {
					ASSERT(0);
					return -EFSCORRUPTED;
				}
				if (bli->bli_flags & XFS_BLI_ORDERED)
					dres->dr_ordered |=
							(1U << dres->dr_bufs);
				else
					xfs_trans_dirty_buf(tp, bli->bli_buf);
				dres->dr_bp[dres->dr_bufs++] = bli->bli_buf;
			}
			break;
		case XFS_LI_INODE:
			ili = container_of(lip, struct xfs_inode_log_item,
					   ili_item);
			if (ili->ili_lock_flags == 0) {
				if (dres->dr_inos >= XFS_DEFER_OPS_NR_INODES) {
					ASSERT(0);
					return -EFSCORRUPTED;
				}
				xfs_trans_log_inode(tp, ili->ili_inode,
						    XFS_ILOG_CORE);
				dres->dr_ip[dres->dr_inos++] = ili->ili_inode;
			}
			break;
		default:
			break;
		}
	}

	return 0;
}

/* Attach the held resources to the transaction. */
static void
xfs_defer_restore_resources(
	struct xfs_trans		*tp,
	struct xfs_defer_resources	*dres)
{
	unsigned short			i;

	/* Rejoin the joined inodes. */
	for (i = 0; i < dres->dr_inos; i++)
		xfs_trans_ijoin(tp, dres->dr_ip[i], 0);

	/* Rejoin the buffers and dirty them so the log moves forward. */
	for (i = 0; i < dres->dr_bufs; i++) {
		xfs_trans_bjoin(tp, dres->dr_bp[i]);
		if (dres->dr_ordered & (1U << i))
			xfs_trans_ordered_buf(tp, dres->dr_bp[i]);
		xfs_trans_bhold(tp, dres->dr_bp[i]);
	}
}

/* Roll a transaction so we can do some deferred op processing. */
STATIC int
xfs_defer_trans_roll(
	struct xfs_trans		**tpp)
{
	struct xfs_defer_resources	dres = { };
	int				error;

	error = xfs_defer_save_resources(&dres, *tpp);
	if (error)
		return error;

	trace_xfs_defer_trans_roll(*tpp, _RET_IP_);

	/*
	 * Roll the transaction.  Rolling always given a new transaction (even
	 * if committing the old one fails!) to hand back to the caller, so we
	 * join the held resources to the new transaction so that we always
	 * return with the held resources joined to @tpp, no matter what
	 * happened.
	 */
	error = xfs_trans_roll(tpp);

	xfs_defer_restore_resources(*tpp, &dres);

	if (error)
		trace_xfs_defer_trans_roll_error(*tpp, error);
	return error;
}

/*
 * Free up any items left in the list.
 */
static void
xfs_defer_cancel_list(
	struct xfs_mount		*mp,
	struct list_head		*dop_list)
{
	struct xfs_defer_pending	*dfp;
	struct xfs_defer_pending	*pli;
	struct list_head		*pwi;
	struct list_head		*n;
	const struct xfs_defer_op_type	*ops;

	/*
	 * Free the pending items.  Caller should already have arranged
	 * for the intent items to be released.
	 */
	list_for_each_entry_safe(dfp, pli, dop_list, dfp_list) {
		ops = defer_op_types[dfp->dfp_type];
		trace_xfs_defer_cancel_list(mp, dfp);
		list_del(&dfp->dfp_list);
		list_for_each_safe(pwi, n, &dfp->dfp_work) {
			list_del(pwi);
			dfp->dfp_count--;
			ops->cancel_item(pwi);
		}
		ASSERT(dfp->dfp_count == 0);
		kmem_cache_free(xfs_defer_pending_cache, dfp);
	}
}

/*
 * Prevent a log intent item from pinning the tail of the log by logging a
 * done item to release the intent item; and then log a new intent item.
 * The caller should provide a fresh transaction and roll it after we're done.
 */
static int
xfs_defer_relog(
	struct xfs_trans		**tpp,
	struct list_head		*dfops)
{
	struct xlog			*log = (*tpp)->t_mountp->m_log;
	struct xfs_defer_pending	*dfp;
	xfs_lsn_t			threshold_lsn = NULLCOMMITLSN;


	ASSERT((*tpp)->t_flags & XFS_TRANS_PERM_LOG_RES);

	list_for_each_entry(dfp, dfops, dfp_list) {
		/*
		 * If the log intent item for this deferred op is not a part of
		 * the current log checkpoint, relog the intent item to keep
		 * the log tail moving forward.  We're ok with this being racy
		 * because an incorrect decision means we'll be a little slower
		 * at pushing the tail.
		 */
		if (dfp->dfp_intent == NULL ||
		    xfs_log_item_in_current_chkpt(dfp->dfp_intent))
			continue;

		/*
		 * Figure out where we need the tail to be in order to maintain
		 * the minimum required free space in the log.  Only sample
		 * the log threshold once per call.
		 */
		if (threshold_lsn == NULLCOMMITLSN) {
			threshold_lsn = xlog_grant_push_threshold(log, 0);
			if (threshold_lsn == NULLCOMMITLSN)
				break;
		}
		if (XFS_LSN_CMP(dfp->dfp_intent->li_lsn, threshold_lsn) >= 0)
			continue;

		trace_xfs_defer_relog_intent((*tpp)->t_mountp, dfp);
		XFS_STATS_INC((*tpp)->t_mountp, defer_relog);
		dfp->dfp_intent = xfs_trans_item_relog(dfp->dfp_intent, *tpp);
	}

	if ((*tpp)->t_flags & XFS_TRANS_DIRTY)
		return xfs_defer_trans_roll(tpp);
	return 0;
}

/*
 * Log an intent-done item for the first pending intent, and finish the work
 * items.
 */
static int
xfs_defer_finish_one(
	struct xfs_trans		*tp,
	struct xfs_defer_pending	*dfp)
{
	const struct xfs_defer_op_type	*ops = defer_op_types[dfp->dfp_type];
	struct xfs_btree_cur		*state = NULL;
	struct list_head		*li, *n;
	int				error;

	trace_xfs_defer_pending_finish(tp->t_mountp, dfp);

	dfp->dfp_done = ops->create_done(tp, dfp->dfp_intent, dfp->dfp_count);
	list_for_each_safe(li, n, &dfp->dfp_work) {
		list_del(li);
		dfp->dfp_count--;
		error = ops->finish_item(tp, dfp->dfp_done, li, &state);
		if (error == -EAGAIN) {
			/*
			 * Caller wants a fresh transaction; put the work item
			 * back on the list and log a new log intent item to
			 * replace the old one.  See "Requesting a Fresh
			 * Transaction while Finishing Deferred Work" above.
			 */
			list_add(li, &dfp->dfp_work);
			dfp->dfp_count++;
			dfp->dfp_done = NULL;
			dfp->dfp_intent = NULL;
			xfs_defer_create_intent(tp, dfp, false);
		}

		if (error)
			goto out;
	}

	/* Done with the dfp, free it. */
	list_del(&dfp->dfp_list);
	kmem_cache_free(xfs_defer_pending_cache, dfp);
out:
	if (ops->finish_cleanup)
		ops->finish_cleanup(tp, state, error);
	return error;
}

/*
 * Finish all the pending work.  This involves logging intent items for
 * any work items that wandered in since the last transaction roll (if
 * one has even happened), rolling the transaction, and finishing the
 * work items in the first item on the logged-and-pending list.
 *
 * If an inode is provided, relog it to the new transaction.
 */
int
xfs_defer_finish_noroll(
	struct xfs_trans		**tp)
{
	struct xfs_defer_pending	*dfp;
	int				error = 0;
	LIST_HEAD(dop_pending);

	ASSERT((*tp)->t_flags & XFS_TRANS_PERM_LOG_RES);

	trace_xfs_defer_finish(*tp, _RET_IP_);

	/* Until we run out of pending work to finish... */
	while (!list_empty(&dop_pending) || !list_empty(&(*tp)->t_dfops)) {
		/*
		 * Deferred items that are created in the process of finishing
		 * other deferred work items should be queued at the head of
		 * the pending list, which puts them ahead of the deferred work
		 * that was created by the caller.  This keeps the number of
		 * pending work items to a minimum, which decreases the amount
		 * of time that any one intent item can stick around in memory,
		 * pinning the log tail.
		 */
		xfs_defer_create_intents(*tp);
		list_splice_init(&(*tp)->t_dfops, &dop_pending);

		error = xfs_defer_trans_roll(tp);
		if (error)
			goto out_shutdown;

		/* Possibly relog intent items to keep the log moving. */
		error = xfs_defer_relog(tp, &dop_pending);
		if (error)
			goto out_shutdown;

		dfp = list_first_entry(&dop_pending, struct xfs_defer_pending,
				       dfp_list);
		error = xfs_defer_finish_one(*tp, dfp);
		if (error && error != -EAGAIN)
			goto out_shutdown;
	}

	trace_xfs_defer_finish_done(*tp, _RET_IP_);
	return 0;

out_shutdown:
	xfs_defer_trans_abort(*tp, &dop_pending);
	xfs_force_shutdown((*tp)->t_mountp, SHUTDOWN_CORRUPT_INCORE);
	trace_xfs_defer_finish_error(*tp, error);
	xfs_defer_cancel_list((*tp)->t_mountp, &dop_pending);
	xfs_defer_cancel(*tp);
	return error;
}

int
xfs_defer_finish(
	struct xfs_trans	**tp)
{
	int			error;

	/*
	 * Finish and roll the transaction once more to avoid returning to the
	 * caller with a dirty transaction.
	 */
	error = xfs_defer_finish_noroll(tp);
	if (error)
		return error;
	if ((*tp)->t_flags & XFS_TRANS_DIRTY) {
		error = xfs_defer_trans_roll(tp);
		if (error) {
			xfs_force_shutdown((*tp)->t_mountp,
					   SHUTDOWN_CORRUPT_INCORE);
			return error;
		}
	}

	/* Reset LOWMODE now that we've finished all the dfops. */
	ASSERT(list_empty(&(*tp)->t_dfops));
	(*tp)->t_flags &= ~XFS_TRANS_LOWMODE;
	return 0;
}

void
xfs_defer_cancel(
	struct xfs_trans	*tp)
{
	struct xfs_mount	*mp = tp->t_mountp;

	trace_xfs_defer_cancel(tp, _RET_IP_);
	xfs_defer_cancel_list(mp, &tp->t_dfops);
}

/* Add an item for later deferred processing. */
void
xfs_defer_add(
	struct xfs_trans		*tp,
	enum xfs_defer_ops_type		type,
	struct list_head		*li)
{
	struct xfs_defer_pending	*dfp = NULL;
	const struct xfs_defer_op_type	*ops;

	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	BUILD_BUG_ON(ARRAY_SIZE(defer_op_types) != XFS_DEFER_OPS_TYPE_MAX);

	/*
	 * Add the item to a pending item at the end of the intake list.
	 * If the last pending item has the same type, reuse it.  Else,
	 * create a new pending item at the end of the intake list.
	 */
	if (!list_empty(&tp->t_dfops)) {
		dfp = list_last_entry(&tp->t_dfops,
				struct xfs_defer_pending, dfp_list);
		ops = defer_op_types[dfp->dfp_type];
		if (dfp->dfp_type != type ||
		    (ops->max_items && dfp->dfp_count >= ops->max_items))
			dfp = NULL;
	}
	if (!dfp) {
		dfp = kmem_cache_zalloc(xfs_defer_pending_cache,
				GFP_NOFS | __GFP_NOFAIL);
		dfp->dfp_type = type;
		dfp->dfp_intent = NULL;
		dfp->dfp_done = NULL;
		dfp->dfp_count = 0;
		INIT_LIST_HEAD(&dfp->dfp_work);
		list_add_tail(&dfp->dfp_list, &tp->t_dfops);
	}

	list_add_tail(li, &dfp->dfp_work);
	dfp->dfp_count++;
}

/*
 * Move deferred ops from one transaction to another and reset the source to
 * initial state. This is primarily used to carry state forward across
 * transaction rolls with pending dfops.
 */
void
xfs_defer_move(
	struct xfs_trans	*dtp,
	struct xfs_trans	*stp)
{
	list_splice_init(&stp->t_dfops, &dtp->t_dfops);

	/*
	 * Low free space mode was historically controlled by a dfops field.
	 * This meant that low mode state potentially carried across multiple
	 * transaction rolls. Transfer low mode on a dfops move to preserve
	 * that behavior.
	 */
	dtp->t_flags |= (stp->t_flags & XFS_TRANS_LOWMODE);
	stp->t_flags &= ~XFS_TRANS_LOWMODE;
}

/*
 * Prepare a chain of fresh deferred ops work items to be completed later.  Log
 * recovery requires the ability to put off until later the actual finishing
 * work so that it can process unfinished items recovered from the log in
 * correct order.
 *
 * Create and log intent items for all the work that we're capturing so that we
 * can be assured that the items will get replayed if the system goes down
 * before log recovery gets a chance to finish the work it put off.  The entire
 * deferred ops state is transferred to the capture structure and the
 * transaction is then ready for the caller to commit it.  If there are no
 * intent items to capture, this function returns NULL.
 *
 * If capture_ip is not NULL, the capture structure will obtain an extra
 * reference to the inode.
 */
static struct xfs_defer_capture *
xfs_defer_ops_capture(
	struct xfs_trans		*tp)
{
	struct xfs_defer_capture	*dfc;
	unsigned short			i;
	int				error;

	if (list_empty(&tp->t_dfops))
		return NULL;

	/* Create an object to capture the defer ops. */
	dfc = kmem_zalloc(sizeof(*dfc), KM_NOFS);
	INIT_LIST_HEAD(&dfc->dfc_list);
	INIT_LIST_HEAD(&dfc->dfc_dfops);

	xfs_defer_create_intents(tp);

	/* Move the dfops chain and transaction state to the capture struct. */
	list_splice_init(&tp->t_dfops, &dfc->dfc_dfops);
	dfc->dfc_tpflags = tp->t_flags & XFS_TRANS_LOWMODE;
	tp->t_flags &= ~XFS_TRANS_LOWMODE;

	/* Capture the remaining block reservations along with the dfops. */
	dfc->dfc_blkres = tp->t_blk_res - tp->t_blk_res_used;
	dfc->dfc_rtxres = tp->t_rtx_res - tp->t_rtx_res_used;

	/* Preserve the log reservation size. */
	dfc->dfc_logres = tp->t_log_res;

	error = xfs_defer_save_resources(&dfc->dfc_held, tp);
	if (error) {
		/*
		 * Resource capture should never fail, but if it does, we
		 * still have to shut down the log and release things
		 * properly.
		 */
		xfs_force_shutdown(tp->t_mountp, SHUTDOWN_CORRUPT_INCORE);
	}

	/*
	 * Grab extra references to the inodes and buffers because callers are
	 * expected to release their held references after we commit the
	 * transaction.
	 */
	for (i = 0; i < dfc->dfc_held.dr_inos; i++) {
		ASSERT(xfs_isilocked(dfc->dfc_held.dr_ip[i], XFS_ILOCK_EXCL));
		ihold(VFS_I(dfc->dfc_held.dr_ip[i]));
	}

	for (i = 0; i < dfc->dfc_held.dr_bufs; i++)
		xfs_buf_hold(dfc->dfc_held.dr_bp[i]);

	return dfc;
}

/* Release all resources that we used to capture deferred ops. */
void
xfs_defer_ops_capture_free(
	struct xfs_mount		*mp,
	struct xfs_defer_capture	*dfc)
{
	unsigned short			i;

	xfs_defer_cancel_list(mp, &dfc->dfc_dfops);

	for (i = 0; i < dfc->dfc_held.dr_bufs; i++)
		xfs_buf_relse(dfc->dfc_held.dr_bp[i]);

	for (i = 0; i < dfc->dfc_held.dr_inos; i++)
		xfs_irele(dfc->dfc_held.dr_ip[i]);

	kmem_free(dfc);
}

/*
 * Capture any deferred ops and commit the transaction.  This is the last step
 * needed to finish a log intent item that we recovered from the log.  If any
 * of the deferred ops operate on an inode, the caller must pass in that inode
 * so that the reference can be transferred to the capture structure.  The
 * caller must hold ILOCK_EXCL on the inode, and must unlock it before calling
 * xfs_defer_ops_continue.
 */
int
xfs_defer_ops_capture_and_commit(
	struct xfs_trans		*tp,
	struct list_head		*capture_list)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_defer_capture	*dfc;
	int				error;

	/* If we don't capture anything, commit transaction and exit. */
	dfc = xfs_defer_ops_capture(tp);
	if (!dfc)
		return xfs_trans_commit(tp);

	/* Commit the transaction and add the capture structure to the list. */
	error = xfs_trans_commit(tp);
	if (error) {
		xfs_defer_ops_capture_free(mp, dfc);
		return error;
	}

	list_add_tail(&dfc->dfc_list, capture_list);
	return 0;
}

/*
 * Attach a chain of captured deferred ops to a new transaction and free the
 * capture structure.  If an inode was captured, it will be passed back to the
 * caller with ILOCK_EXCL held and joined to the transaction with lockflags==0.
 * The caller now owns the inode reference.
 */
void
xfs_defer_ops_continue(
	struct xfs_defer_capture	*dfc,
	struct xfs_trans		*tp,
	struct xfs_defer_resources	*dres)
{
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(!(tp->t_flags & XFS_TRANS_DIRTY));

	/* Lock and join the captured inode to the new transaction. */
	if (dfc->dfc_held.dr_inos == 2)
		xfs_lock_two_inodes(dfc->dfc_held.dr_ip[0], XFS_ILOCK_EXCL,
				    dfc->dfc_held.dr_ip[1], XFS_ILOCK_EXCL);
	else if (dfc->dfc_held.dr_inos == 1)
		xfs_ilock(dfc->dfc_held.dr_ip[0], XFS_ILOCK_EXCL);
	xfs_defer_restore_resources(tp, &dfc->dfc_held);
	memcpy(dres, &dfc->dfc_held, sizeof(struct xfs_defer_resources));

	/* Move captured dfops chain and state to the transaction. */
	list_splice_init(&dfc->dfc_dfops, &tp->t_dfops);
	tp->t_flags |= dfc->dfc_tpflags;

	kmem_free(dfc);
}

/* Release the resources captured and continued during recovery. */
void
xfs_defer_resources_rele(
	struct xfs_defer_resources	*dres)
{
	unsigned short			i;

	for (i = 0; i < dres->dr_inos; i++) {
		xfs_iunlock(dres->dr_ip[i], XFS_ILOCK_EXCL);
		xfs_irele(dres->dr_ip[i]);
		dres->dr_ip[i] = NULL;
	}

	for (i = 0; i < dres->dr_bufs; i++) {
		xfs_buf_relse(dres->dr_bp[i]);
		dres->dr_bp[i] = NULL;
	}

	dres->dr_inos = 0;
	dres->dr_bufs = 0;
	dres->dr_ordered = 0;
}

static inline int __init
xfs_defer_init_cache(void)
{
	xfs_defer_pending_cache = kmem_cache_create("xfs_defer_pending",
			sizeof(struct xfs_defer_pending),
			0, 0, NULL);

	return xfs_defer_pending_cache != NULL ? 0 : -ENOMEM;
}

static inline void
xfs_defer_destroy_cache(void)
{
	kmem_cache_destroy(xfs_defer_pending_cache);
	xfs_defer_pending_cache = NULL;
}

/* Set up caches for deferred work items. */
int __init
xfs_defer_init_item_caches(void)
{
	int				error;

	error = xfs_defer_init_cache();
	if (error)
		return error;
	error = xfs_rmap_intent_init_cache();
	if (error)
		goto err;
	error = xfs_refcount_intent_init_cache();
	if (error)
		goto err;
	error = xfs_bmap_intent_init_cache();
	if (error)
		goto err;
	error = xfs_extfree_intent_init_cache();
	if (error)
		goto err;

	return 0;
err:
	xfs_defer_destroy_item_caches();
	return error;
}

/* Destroy all the deferred work item caches, if they've been allocated. */
void
xfs_defer_destroy_item_caches(void)
{
	xfs_extfree_intent_destroy_cache();
	xfs_bmap_intent_destroy_cache();
	xfs_refcount_intent_destroy_cache();
	xfs_rmap_intent_destroy_cache();
	xfs_defer_destroy_cache();
}
