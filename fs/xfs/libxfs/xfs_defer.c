/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_trace.h"

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
 *
 * The key here is that we must log an intent item for all pending
 * work items every time we roll the transaction, and that we must log
 * a done item as soon as the work is completed.  With this mechanism
 * we can perform complex remapping operations, chaining intent items
 * as needed.
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
 * | Done reducing refcount for extent (C, B)        |
 * | Increase refcount for extent (E, B)             |
 * | Done increasing refcount for extent (E, B)      |
 * | Intent to free extent (C, B)                    |
 * | Intent to free extent (F, 1) (refcountbt block) |
 * | Intent to remove rmap (F, 1, REFC)              |
 * +-------------------------------------------------+
 * | Remove rmap (X, C, A, B)                        | t3
 * | Done removing rmap (X, C, A, B)                 |
 * | Add rmap (X, E, A, B)                           |
 * | Done adding rmap (X, E, A, B)                   |
 * | Remove rmap (F, 1, REFC)                        |
 * | Done removing rmap (F, 1, REFC)                 |
 * +-------------------------------------------------+
 * | Free extent (C, B)                              | t4
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
 */

static const struct xfs_defer_op_type *defer_op_types[XFS_DEFER_OPS_TYPE_MAX];

/*
 * For each pending item in the intake list, log its intent item and the
 * associated extents, then add the entire intake list to the end of
 * the pending list.
 */
STATIC void
xfs_defer_intake_work(
	struct xfs_trans		*tp,
	struct xfs_defer_ops		*dop)
{
	struct list_head		*li;
	struct xfs_defer_pending	*dfp;

	list_for_each_entry(dfp, &dop->dop_intake, dfp_list) {
		trace_xfs_defer_intake_work(tp->t_mountp, dfp);
		dfp->dfp_intent = dfp->dfp_type->create_intent(tp,
				dfp->dfp_count);
		list_sort(tp->t_mountp, &dfp->dfp_work,
				dfp->dfp_type->diff_items);
		list_for_each(li, &dfp->dfp_work)
			dfp->dfp_type->log_item(tp, dfp->dfp_intent, li);
	}

	list_splice_tail_init(&dop->dop_intake, &dop->dop_pending);
}

/* Abort all the intents that were committed. */
STATIC void
xfs_defer_trans_abort(
	struct xfs_trans		*tp,
	struct xfs_defer_ops		*dop,
	int				error)
{
	struct xfs_defer_pending	*dfp;

	trace_xfs_defer_trans_abort(tp->t_mountp, dop);
	/*
	 * If the transaction was committed, drop the intent reference
	 * since we're bailing out of here. The other reference is
	 * dropped when the intent hits the AIL.  If the transaction
	 * was not committed, the intent is freed by the intent item
	 * unlock handler on abort.
	 */
	if (!dop->dop_committed)
		return;

	/* Abort intent items. */
	list_for_each_entry(dfp, &dop->dop_pending, dfp_list) {
		trace_xfs_defer_pending_abort(tp->t_mountp, dfp);
		if (dfp->dfp_committed)
			dfp->dfp_type->abort_intent(dfp->dfp_intent);
	}

	/* Shut down FS. */
	xfs_force_shutdown(tp->t_mountp, (error == -EFSCORRUPTED) ?
			SHUTDOWN_CORRUPT_INCORE : SHUTDOWN_META_IO_ERROR);
}

/* Roll a transaction so we can do some deferred op processing. */
STATIC int
xfs_defer_trans_roll(
	struct xfs_trans		**tp,
	struct xfs_defer_ops		*dop,
	struct xfs_inode		*ip)
{
	int				i;
	int				error;

	/* Log all the joined inodes except the one we passed in. */
	for (i = 0; i < XFS_DEFER_OPS_NR_INODES && dop->dop_inodes[i]; i++) {
		if (dop->dop_inodes[i] == ip)
			continue;
		xfs_trans_log_inode(*tp, dop->dop_inodes[i], XFS_ILOG_CORE);
	}

	trace_xfs_defer_trans_roll((*tp)->t_mountp, dop);

	/* Roll the transaction. */
	error = xfs_trans_roll(tp, ip);
	if (error) {
		trace_xfs_defer_trans_roll_error((*tp)->t_mountp, dop, error);
		xfs_defer_trans_abort(*tp, dop, error);
		return error;
	}
	dop->dop_committed = true;

	/* Rejoin the joined inodes except the one we passed in. */
	for (i = 0; i < XFS_DEFER_OPS_NR_INODES && dop->dop_inodes[i]; i++) {
		if (dop->dop_inodes[i] == ip)
			continue;
		xfs_trans_ijoin(*tp, dop->dop_inodes[i], 0);
	}

	return error;
}

/* Do we have any work items to finish? */
bool
xfs_defer_has_unfinished_work(
	struct xfs_defer_ops		*dop)
{
	return !list_empty(&dop->dop_pending) || !list_empty(&dop->dop_intake);
}

/*
 * Add this inode to the deferred op.  Each joined inode is relogged
 * each time we roll the transaction, in addition to any inode passed
 * to xfs_defer_finish().
 */
int
xfs_defer_join(
	struct xfs_defer_ops		*dop,
	struct xfs_inode		*ip)
{
	int				i;

	for (i = 0; i < XFS_DEFER_OPS_NR_INODES; i++) {
		if (dop->dop_inodes[i] == ip)
			return 0;
		else if (dop->dop_inodes[i] == NULL) {
			dop->dop_inodes[i] = ip;
			return 0;
		}
	}

	return -EFSCORRUPTED;
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
xfs_defer_finish(
	struct xfs_trans		**tp,
	struct xfs_defer_ops		*dop,
	struct xfs_inode		*ip)
{
	struct xfs_defer_pending	*dfp;
	struct list_head		*li;
	struct list_head		*n;
	void				*done_item = NULL;
	void				*state;
	int				error = 0;
	void				(*cleanup_fn)(struct xfs_trans *, void *, int);

	ASSERT((*tp)->t_flags & XFS_TRANS_PERM_LOG_RES);

	trace_xfs_defer_finish((*tp)->t_mountp, dop);

	/* Until we run out of pending work to finish... */
	while (xfs_defer_has_unfinished_work(dop)) {
		/* Log intents for work items sitting in the intake. */
		xfs_defer_intake_work(*tp, dop);

		/* Roll the transaction. */
		error = xfs_defer_trans_roll(tp, dop, ip);
		if (error)
			goto out;

		/* Mark all pending intents as committed. */
		list_for_each_entry_reverse(dfp, &dop->dop_pending, dfp_list) {
			if (dfp->dfp_committed)
				break;
			trace_xfs_defer_pending_commit((*tp)->t_mountp, dfp);
			dfp->dfp_committed = true;
		}

		/* Log an intent-done item for the first pending item. */
		dfp = list_first_entry(&dop->dop_pending,
				struct xfs_defer_pending, dfp_list);
		trace_xfs_defer_pending_finish((*tp)->t_mountp, dfp);
		done_item = dfp->dfp_type->create_done(*tp, dfp->dfp_intent,
				dfp->dfp_count);
		cleanup_fn = dfp->dfp_type->finish_cleanup;

		/* Finish the work items. */
		state = NULL;
		list_for_each_safe(li, n, &dfp->dfp_work) {
			list_del(li);
			dfp->dfp_count--;
			error = dfp->dfp_type->finish_item(*tp, dop, li,
					done_item, &state);
			if (error) {
				/*
				 * Clean up after ourselves and jump out.
				 * xfs_defer_cancel will take care of freeing
				 * all these lists and stuff.
				 */
				if (cleanup_fn)
					cleanup_fn(*tp, state, error);
				xfs_defer_trans_abort(*tp, dop, error);
				goto out;
			}
		}
		/* Done with the dfp, free it. */
		list_del(&dfp->dfp_list);
		kmem_free(dfp);

		if (cleanup_fn)
			cleanup_fn(*tp, state, error);
	}

out:
	if (error)
		trace_xfs_defer_finish_error((*tp)->t_mountp, dop, error);
	else
		trace_xfs_defer_finish_done((*tp)->t_mountp, dop);
	return error;
}

/*
 * Free up any items left in the list.
 */
void
xfs_defer_cancel(
	struct xfs_defer_ops		*dop)
{
	struct xfs_defer_pending	*dfp;
	struct xfs_defer_pending	*pli;
	struct list_head		*pwi;
	struct list_head		*n;

	trace_xfs_defer_cancel(NULL, dop);

	/*
	 * Free the pending items.  Caller should already have arranged
	 * for the intent items to be released.
	 */
	list_for_each_entry_safe(dfp, pli, &dop->dop_intake, dfp_list) {
		trace_xfs_defer_intake_cancel(NULL, dfp);
		list_del(&dfp->dfp_list);
		list_for_each_safe(pwi, n, &dfp->dfp_work) {
			list_del(pwi);
			dfp->dfp_count--;
			dfp->dfp_type->cancel_item(pwi);
		}
		ASSERT(dfp->dfp_count == 0);
		kmem_free(dfp);
	}
	list_for_each_entry_safe(dfp, pli, &dop->dop_pending, dfp_list) {
		trace_xfs_defer_pending_cancel(NULL, dfp);
		list_del(&dfp->dfp_list);
		list_for_each_safe(pwi, n, &dfp->dfp_work) {
			list_del(pwi);
			dfp->dfp_count--;
			dfp->dfp_type->cancel_item(pwi);
		}
		ASSERT(dfp->dfp_count == 0);
		kmem_free(dfp);
	}
}

/* Add an item for later deferred processing. */
void
xfs_defer_add(
	struct xfs_defer_ops		*dop,
	enum xfs_defer_ops_type		type,
	struct list_head		*li)
{
	struct xfs_defer_pending	*dfp = NULL;

	/*
	 * Add the item to a pending item at the end of the intake list.
	 * If the last pending item has the same type, reuse it.  Else,
	 * create a new pending item at the end of the intake list.
	 */
	if (!list_empty(&dop->dop_intake)) {
		dfp = list_last_entry(&dop->dop_intake,
				struct xfs_defer_pending, dfp_list);
		if (dfp->dfp_type->type != type ||
		    (dfp->dfp_type->max_items &&
		     dfp->dfp_count >= dfp->dfp_type->max_items))
			dfp = NULL;
	}
	if (!dfp) {
		dfp = kmem_alloc(sizeof(struct xfs_defer_pending),
				KM_SLEEP | KM_NOFS);
		dfp->dfp_type = defer_op_types[type];
		dfp->dfp_committed = false;
		dfp->dfp_intent = NULL;
		dfp->dfp_count = 0;
		INIT_LIST_HEAD(&dfp->dfp_work);
		list_add_tail(&dfp->dfp_list, &dop->dop_intake);
	}

	list_add_tail(li, &dfp->dfp_work);
	dfp->dfp_count++;
}

/* Initialize a deferred operation list. */
void
xfs_defer_init_op_type(
	const struct xfs_defer_op_type	*type)
{
	defer_op_types[type->type] = type;
}

/* Initialize a deferred operation. */
void
xfs_defer_init(
	struct xfs_defer_ops		*dop,
	xfs_fsblock_t			*fbp)
{
	dop->dop_committed = false;
	dop->dop_low = false;
	memset(&dop->dop_inodes, 0, sizeof(dop->dop_inodes));
	*fbp = NULLFSBLOCK;
	INIT_LIST_HEAD(&dop->dop_intake);
	INIT_LIST_HEAD(&dop->dop_pending);
	trace_xfs_defer_init(NULL, dop);
}
