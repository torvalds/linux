// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2008 Dave Chinner
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_trace.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_log.h"

#ifdef DEBUG
/*
 * Check that the list is sorted as it should be.
 *
 * Called with the ail lock held, but we don't want to assert fail with it
 * held otherwise we'll lock everything up and won't be able to debug the
 * cause. Hence we sample and check the state under the AIL lock and return if
 * everything is fine, otherwise we drop the lock and run the ASSERT checks.
 * Asserts may not be fatal, so pick the lock back up and continue onwards.
 */
STATIC void
xfs_ail_check(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	struct xfs_log_item	*prev_lip;
	struct xfs_log_item	*next_lip;
	xfs_lsn_t		prev_lsn = NULLCOMMITLSN;
	xfs_lsn_t		next_lsn = NULLCOMMITLSN;
	xfs_lsn_t		lsn;
	bool			in_ail;


	if (list_empty(&ailp->ail_head))
		return;

	/*
	 * Sample then check the next and previous entries are valid.
	 */
	in_ail = test_bit(XFS_LI_IN_AIL, &lip->li_flags);
	prev_lip = list_entry(lip->li_ail.prev, struct xfs_log_item, li_ail);
	if (&prev_lip->li_ail != &ailp->ail_head)
		prev_lsn = prev_lip->li_lsn;
	next_lip = list_entry(lip->li_ail.next, struct xfs_log_item, li_ail);
	if (&next_lip->li_ail != &ailp->ail_head)
		next_lsn = next_lip->li_lsn;
	lsn = lip->li_lsn;

	if (in_ail &&
	    (prev_lsn == NULLCOMMITLSN || XFS_LSN_CMP(prev_lsn, lsn) <= 0) &&
	    (next_lsn == NULLCOMMITLSN || XFS_LSN_CMP(next_lsn, lsn) >= 0))
		return;

	spin_unlock(&ailp->ail_lock);
	ASSERT(in_ail);
	ASSERT(prev_lsn == NULLCOMMITLSN || XFS_LSN_CMP(prev_lsn, lsn) <= 0);
	ASSERT(next_lsn == NULLCOMMITLSN || XFS_LSN_CMP(next_lsn, lsn) >= 0);
	spin_lock(&ailp->ail_lock);
}
#else /* !DEBUG */
#define	xfs_ail_check(a,l)
#endif /* DEBUG */

/*
 * Return a pointer to the last item in the AIL.  If the AIL is empty, then
 * return NULL.
 */
static struct xfs_log_item *
xfs_ail_max(
	struct xfs_ail  *ailp)
{
	if (list_empty(&ailp->ail_head))
		return NULL;

	return list_entry(ailp->ail_head.prev, struct xfs_log_item, li_ail);
}

/*
 * Return a pointer to the item which follows the given item in the AIL.  If
 * the given item is the last item in the list, then return NULL.
 */
static struct xfs_log_item *
xfs_ail_next(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	if (lip->li_ail.next == &ailp->ail_head)
		return NULL;

	return list_first_entry(&lip->li_ail, struct xfs_log_item, li_ail);
}

/*
 * This is called by the log manager code to determine the LSN of the tail of
 * the log.  This is exactly the LSN of the first item in the AIL.  If the AIL
 * is empty, then this function returns 0.
 *
 * We need the AIL lock in order to get a coherent read of the lsn of the last
 * item in the AIL.
 */
xfs_lsn_t
xfs_ail_min_lsn(
	struct xfs_ail		*ailp)
{
	xfs_lsn_t		lsn = 0;
	struct xfs_log_item	*lip;

	spin_lock(&ailp->ail_lock);
	lip = xfs_ail_min(ailp);
	if (lip)
		lsn = lip->li_lsn;
	spin_unlock(&ailp->ail_lock);

	return lsn;
}

/*
 * Return the maximum lsn held in the AIL, or zero if the AIL is empty.
 */
static xfs_lsn_t
xfs_ail_max_lsn(
	struct xfs_ail		*ailp)
{
	xfs_lsn_t       	lsn = 0;
	struct xfs_log_item	*lip;

	spin_lock(&ailp->ail_lock);
	lip = xfs_ail_max(ailp);
	if (lip)
		lsn = lip->li_lsn;
	spin_unlock(&ailp->ail_lock);

	return lsn;
}

/*
 * The cursor keeps track of where our current traversal is up to by tracking
 * the next item in the list for us. However, for this to be safe, removing an
 * object from the AIL needs to invalidate any cursor that points to it. hence
 * the traversal cursor needs to be linked to the struct xfs_ail so that
 * deletion can search all the active cursors for invalidation.
 */
STATIC void
xfs_trans_ail_cursor_init(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur)
{
	cur->item = NULL;
	list_add_tail(&cur->list, &ailp->ail_cursors);
}

/*
 * Get the next item in the traversal and advance the cursor.  If the cursor
 * was invalidated (indicated by a lip of 1), restart the traversal.
 */
struct xfs_log_item *
xfs_trans_ail_cursor_next(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur)
{
	struct xfs_log_item	*lip = cur->item;

	if ((uintptr_t)lip & 1)
		lip = xfs_ail_min(ailp);
	if (lip)
		cur->item = xfs_ail_next(ailp, lip);
	return lip;
}

/*
 * When the traversal is complete, we need to remove the cursor from the list
 * of traversing cursors.
 */
void
xfs_trans_ail_cursor_done(
	struct xfs_ail_cursor	*cur)
{
	cur->item = NULL;
	list_del_init(&cur->list);
}

/*
 * Invalidate any cursor that is pointing to this item. This is called when an
 * item is removed from the AIL. Any cursor pointing to this object is now
 * invalid and the traversal needs to be terminated so it doesn't reference a
 * freed object. We set the low bit of the cursor item pointer so we can
 * distinguish between an invalidation and the end of the list when getting the
 * next item from the cursor.
 */
STATIC void
xfs_trans_ail_cursor_clear(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	struct xfs_ail_cursor	*cur;

	list_for_each_entry(cur, &ailp->ail_cursors, list) {
		if (cur->item == lip)
			cur->item = (struct xfs_log_item *)
					((uintptr_t)cur->item | 1);
	}
}

/*
 * Find the first item in the AIL with the given @lsn by searching in ascending
 * LSN order and initialise the cursor to point to the next item for a
 * ascending traversal.  Pass a @lsn of zero to initialise the cursor to the
 * first item in the AIL. Returns NULL if the list is empty.
 */
struct xfs_log_item *
xfs_trans_ail_cursor_first(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	xfs_lsn_t		lsn)
{
	struct xfs_log_item	*lip;

	xfs_trans_ail_cursor_init(ailp, cur);

	if (lsn == 0) {
		lip = xfs_ail_min(ailp);
		goto out;
	}

	list_for_each_entry(lip, &ailp->ail_head, li_ail) {
		if (XFS_LSN_CMP(lip->li_lsn, lsn) >= 0)
			goto out;
	}
	return NULL;

out:
	if (lip)
		cur->item = xfs_ail_next(ailp, lip);
	return lip;
}

static struct xfs_log_item *
__xfs_trans_ail_cursor_last(
	struct xfs_ail		*ailp,
	xfs_lsn_t		lsn)
{
	struct xfs_log_item	*lip;

	list_for_each_entry_reverse(lip, &ailp->ail_head, li_ail) {
		if (XFS_LSN_CMP(lip->li_lsn, lsn) <= 0)
			return lip;
	}
	return NULL;
}

/*
 * Find the last item in the AIL with the given @lsn by searching in descending
 * LSN order and initialise the cursor to point to that item.  If there is no
 * item with the value of @lsn, then it sets the cursor to the last item with an
 * LSN lower than @lsn.  Returns NULL if the list is empty.
 */
struct xfs_log_item *
xfs_trans_ail_cursor_last(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	xfs_lsn_t		lsn)
{
	xfs_trans_ail_cursor_init(ailp, cur);
	cur->item = __xfs_trans_ail_cursor_last(ailp, lsn);
	return cur->item;
}

/*
 * Splice the log item list into the AIL at the given LSN. We splice to the
 * tail of the given LSN to maintain insert order for push traversals. The
 * cursor is optional, allowing repeated updates to the same LSN to avoid
 * repeated traversals.  This should not be called with an empty list.
 */
static void
xfs_ail_splice(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct list_head	*list,
	xfs_lsn_t		lsn)
{
	struct xfs_log_item	*lip;

	ASSERT(!list_empty(list));

	/*
	 * Use the cursor to determine the insertion point if one is
	 * provided.  If not, or if the one we got is not valid,
	 * find the place in the AIL where the items belong.
	 */
	lip = cur ? cur->item : NULL;
	if (!lip || (uintptr_t)lip & 1)
		lip = __xfs_trans_ail_cursor_last(ailp, lsn);

	/*
	 * If a cursor is provided, we know we're processing the AIL
	 * in lsn order, and future items to be spliced in will
	 * follow the last one being inserted now.  Update the
	 * cursor to point to that last item, now while we have a
	 * reliable pointer to it.
	 */
	if (cur)
		cur->item = list_entry(list->prev, struct xfs_log_item, li_ail);

	/*
	 * Finally perform the splice.  Unless the AIL was empty,
	 * lip points to the item in the AIL _after_ which the new
	 * items should go.  If lip is null the AIL was empty, so
	 * the new items go at the head of the AIL.
	 */
	if (lip)
		list_splice(list, &lip->li_ail);
	else
		list_splice(list, &ailp->ail_head);
}

/*
 * Delete the given item from the AIL.  Return a pointer to the item.
 */
static void
xfs_ail_delete(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	xfs_ail_check(ailp, lip);
	list_del(&lip->li_ail);
	xfs_trans_ail_cursor_clear(ailp, lip);
}

static inline uint
xfsaild_push_item(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	/*
	 * If log item pinning is enabled, skip the push and track the item as
	 * pinned. This can help induce head-behind-tail conditions.
	 */
	if (XFS_TEST_ERROR(false, ailp->ail_mount, XFS_ERRTAG_LOG_ITEM_PIN))
		return XFS_ITEM_PINNED;

	/*
	 * Consider the item pinned if a push callback is not defined so the
	 * caller will force the log. This should only happen for intent items
	 * as they are unpinned once the associated done item is committed to
	 * the on-disk log.
	 */
	if (!lip->li_ops->iop_push)
		return XFS_ITEM_PINNED;
	return lip->li_ops->iop_push(lip, &ailp->ail_buf_list);
}

static long
xfsaild_push(
	struct xfs_ail		*ailp)
{
	xfs_mount_t		*mp = ailp->ail_mount;
	struct xfs_ail_cursor	cur;
	struct xfs_log_item	*lip;
	xfs_lsn_t		lsn;
	xfs_lsn_t		target;
	long			tout;
	int			stuck = 0;
	int			flushing = 0;
	int			count = 0;

	/*
	 * If we encountered pinned items or did not finish writing out all
	 * buffers the last time we ran, force the log first and wait for it
	 * before pushing again.
	 */
	if (ailp->ail_log_flush && ailp->ail_last_pushed_lsn == 0 &&
	    (!list_empty_careful(&ailp->ail_buf_list) ||
	     xfs_ail_min_lsn(ailp))) {
		ailp->ail_log_flush = 0;

		XFS_STATS_INC(mp, xs_push_ail_flush);
		xfs_log_force(mp, XFS_LOG_SYNC);
	}

	spin_lock(&ailp->ail_lock);

	/* barrier matches the ail_target update in xfs_ail_push() */
	smp_rmb();
	target = ailp->ail_target;
	ailp->ail_target_prev = target;

	lip = xfs_trans_ail_cursor_first(ailp, &cur, ailp->ail_last_pushed_lsn);
	if (!lip) {
		/*
		 * If the AIL is empty or our push has reached the end we are
		 * done now.
		 */
		xfs_trans_ail_cursor_done(&cur);
		spin_unlock(&ailp->ail_lock);
		goto out_done;
	}

	XFS_STATS_INC(mp, xs_push_ail);

	lsn = lip->li_lsn;
	while ((XFS_LSN_CMP(lip->li_lsn, target) <= 0)) {
		int	lock_result;

		/*
		 * Note that iop_push may unlock and reacquire the AIL lock.  We
		 * rely on the AIL cursor implementation to be able to deal with
		 * the dropped lock.
		 */
		lock_result = xfsaild_push_item(ailp, lip);
		switch (lock_result) {
		case XFS_ITEM_SUCCESS:
			XFS_STATS_INC(mp, xs_push_ail_success);
			trace_xfs_ail_push(lip);

			ailp->ail_last_pushed_lsn = lsn;
			break;

		case XFS_ITEM_FLUSHING:
			/*
			 * The item or its backing buffer is already beeing
			 * flushed.  The typical reason for that is that an
			 * inode buffer is locked because we already pushed the
			 * updates to it as part of inode clustering.
			 *
			 * We do not want to to stop flushing just because lots
			 * of items are already beeing flushed, but we need to
			 * re-try the flushing relatively soon if most of the
			 * AIL is beeing flushed.
			 */
			XFS_STATS_INC(mp, xs_push_ail_flushing);
			trace_xfs_ail_flushing(lip);

			flushing++;
			ailp->ail_last_pushed_lsn = lsn;
			break;

		case XFS_ITEM_PINNED:
			XFS_STATS_INC(mp, xs_push_ail_pinned);
			trace_xfs_ail_pinned(lip);

			stuck++;
			ailp->ail_log_flush++;
			break;
		case XFS_ITEM_LOCKED:
			XFS_STATS_INC(mp, xs_push_ail_locked);
			trace_xfs_ail_locked(lip);

			stuck++;
			break;
		default:
			ASSERT(0);
			break;
		}

		count++;

		/*
		 * Are there too many items we can't do anything with?
		 *
		 * If we we are skipping too many items because we can't flush
		 * them or they are already being flushed, we back off and
		 * given them time to complete whatever operation is being
		 * done. i.e. remove pressure from the AIL while we can't make
		 * progress so traversals don't slow down further inserts and
		 * removals to/from the AIL.
		 *
		 * The value of 100 is an arbitrary magic number based on
		 * observation.
		 */
		if (stuck > 100)
			break;

		lip = xfs_trans_ail_cursor_next(ailp, &cur);
		if (lip == NULL)
			break;
		lsn = lip->li_lsn;
	}
	xfs_trans_ail_cursor_done(&cur);
	spin_unlock(&ailp->ail_lock);

	if (xfs_buf_delwri_submit_nowait(&ailp->ail_buf_list))
		ailp->ail_log_flush++;

	if (!count || XFS_LSN_CMP(lsn, target) >= 0) {
out_done:
		/*
		 * We reached the target or the AIL is empty, so wait a bit
		 * longer for I/O to complete and remove pushed items from the
		 * AIL before we start the next scan from the start of the AIL.
		 */
		tout = 50;
		ailp->ail_last_pushed_lsn = 0;
	} else if (((stuck + flushing) * 100) / count > 90) {
		/*
		 * Either there is a lot of contention on the AIL or we are
		 * stuck due to operations in progress. "Stuck" in this case
		 * is defined as >90% of the items we tried to push were stuck.
		 *
		 * Backoff a bit more to allow some I/O to complete before
		 * restarting from the start of the AIL. This prevents us from
		 * spinning on the same items, and if they are pinned will all
		 * the restart to issue a log force to unpin the stuck items.
		 */
		tout = 20;
		ailp->ail_last_pushed_lsn = 0;
	} else {
		/*
		 * Assume we have more work to do in a short while.
		 */
		tout = 10;
	}

	return tout;
}

static int
xfsaild(
	void		*data)
{
	struct xfs_ail	*ailp = data;
	long		tout = 0;	/* milliseconds */

	current->flags |= PF_MEMALLOC;
	set_freezable();

	while (1) {
		if (tout && tout <= 20)
			set_current_state(TASK_KILLABLE);
		else
			set_current_state(TASK_INTERRUPTIBLE);

		/*
		 * Check kthread_should_stop() after we set the task state to
		 * guarantee that we either see the stop bit and exit or the
		 * task state is reset to runnable such that it's not scheduled
		 * out indefinitely and detects the stop bit at next iteration.
		 * A memory barrier is included in above task state set to
		 * serialize again kthread_stop().
		 */
		if (kthread_should_stop()) {
			__set_current_state(TASK_RUNNING);

			/*
			 * The caller forces out the AIL before stopping the
			 * thread in the common case, which means the delwri
			 * queue is drained. In the shutdown case, the queue may
			 * still hold relogged buffers that haven't been
			 * submitted because they were pinned since added to the
			 * queue.
			 *
			 * Log I/O error processing stales the underlying buffer
			 * and clears the delwri state, expecting the buf to be
			 * removed on the next submission attempt. That won't
			 * happen if we're shutting down, so this is the last
			 * opportunity to release such buffers from the queue.
			 */
			ASSERT(list_empty(&ailp->ail_buf_list) ||
			       XFS_FORCED_SHUTDOWN(ailp->ail_mount));
			xfs_buf_delwri_cancel(&ailp->ail_buf_list);
			break;
		}

		spin_lock(&ailp->ail_lock);

		/*
		 * Idle if the AIL is empty and we are not racing with a target
		 * update. We check the AIL after we set the task to a sleep
		 * state to guarantee that we either catch an ail_target update
		 * or that a wake_up resets the state to TASK_RUNNING.
		 * Otherwise, we run the risk of sleeping indefinitely.
		 *
		 * The barrier matches the ail_target update in xfs_ail_push().
		 */
		smp_rmb();
		if (!xfs_ail_min(ailp) &&
		    ailp->ail_target == ailp->ail_target_prev) {
			spin_unlock(&ailp->ail_lock);
			freezable_schedule();
			tout = 0;
			continue;
		}
		spin_unlock(&ailp->ail_lock);

		if (tout)
			freezable_schedule_timeout(msecs_to_jiffies(tout));

		__set_current_state(TASK_RUNNING);

		try_to_freeze();

		tout = xfsaild_push(ailp);
	}

	return 0;
}

/*
 * This routine is called to move the tail of the AIL forward.  It does this by
 * trying to flush items in the AIL whose lsns are below the given
 * threshold_lsn.
 *
 * The push is run asynchronously in a workqueue, which means the caller needs
 * to handle waiting on the async flush for space to become available.
 * We don't want to interrupt any push that is in progress, hence we only queue
 * work if we set the pushing bit approriately.
 *
 * We do this unlocked - we only need to know whether there is anything in the
 * AIL at the time we are called. We don't need to access the contents of
 * any of the objects, so the lock is not needed.
 */
void
xfs_ail_push(
	struct xfs_ail		*ailp,
	xfs_lsn_t		threshold_lsn)
{
	struct xfs_log_item	*lip;

	lip = xfs_ail_min(ailp);
	if (!lip || XFS_FORCED_SHUTDOWN(ailp->ail_mount) ||
	    XFS_LSN_CMP(threshold_lsn, ailp->ail_target) <= 0)
		return;

	/*
	 * Ensure that the new target is noticed in push code before it clears
	 * the XFS_AIL_PUSHING_BIT.
	 */
	smp_wmb();
	xfs_trans_ail_copy_lsn(ailp, &ailp->ail_target, &threshold_lsn);
	smp_wmb();

	wake_up_process(ailp->ail_task);
}

/*
 * Push out all items in the AIL immediately
 */
void
xfs_ail_push_all(
	struct xfs_ail  *ailp)
{
	xfs_lsn_t       threshold_lsn = xfs_ail_max_lsn(ailp);

	if (threshold_lsn)
		xfs_ail_push(ailp, threshold_lsn);
}

/*
 * Push out all items in the AIL immediately and wait until the AIL is empty.
 */
void
xfs_ail_push_all_sync(
	struct xfs_ail  *ailp)
{
	struct xfs_log_item	*lip;
	DEFINE_WAIT(wait);

	spin_lock(&ailp->ail_lock);
	while ((lip = xfs_ail_max(ailp)) != NULL) {
		prepare_to_wait(&ailp->ail_empty, &wait, TASK_UNINTERRUPTIBLE);
		ailp->ail_target = lip->li_lsn;
		wake_up_process(ailp->ail_task);
		spin_unlock(&ailp->ail_lock);
		schedule();
		spin_lock(&ailp->ail_lock);
	}
	spin_unlock(&ailp->ail_lock);

	finish_wait(&ailp->ail_empty, &wait);
}

/*
 * xfs_trans_ail_update - bulk AIL insertion operation.
 *
 * @xfs_trans_ail_update takes an array of log items that all need to be
 * positioned at the same LSN in the AIL. If an item is not in the AIL, it will
 * be added.  Otherwise, it will be repositioned  by removing it and re-adding
 * it to the AIL. If we move the first item in the AIL, update the log tail to
 * match the new minimum LSN in the AIL.
 *
 * This function takes the AIL lock once to execute the update operations on
 * all the items in the array, and as such should not be called with the AIL
 * lock held. As a result, once we have the AIL lock, we need to check each log
 * item LSN to confirm it needs to be moved forward in the AIL.
 *
 * To optimise the insert operation, we delete all the items from the AIL in
 * the first pass, moving them into a temporary list, then splice the temporary
 * list into the correct position in the AIL. This avoids needing to do an
 * insert operation on every item.
 *
 * This function must be called with the AIL lock held.  The lock is dropped
 * before returning.
 */
void
xfs_trans_ail_update_bulk(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct xfs_log_item	**log_items,
	int			nr_items,
	xfs_lsn_t		lsn) __releases(ailp->ail_lock)
{
	struct xfs_log_item	*mlip;
	int			mlip_changed = 0;
	int			i;
	LIST_HEAD(tmp);

	ASSERT(nr_items > 0);		/* Not required, but true. */
	mlip = xfs_ail_min(ailp);

	for (i = 0; i < nr_items; i++) {
		struct xfs_log_item *lip = log_items[i];
		if (test_and_set_bit(XFS_LI_IN_AIL, &lip->li_flags)) {
			/* check if we really need to move the item */
			if (XFS_LSN_CMP(lsn, lip->li_lsn) <= 0)
				continue;

			trace_xfs_ail_move(lip, lip->li_lsn, lsn);
			xfs_ail_delete(ailp, lip);
			if (mlip == lip)
				mlip_changed = 1;
		} else {
			trace_xfs_ail_insert(lip, 0, lsn);
		}
		lip->li_lsn = lsn;
		list_add(&lip->li_ail, &tmp);
	}

	if (!list_empty(&tmp))
		xfs_ail_splice(ailp, cur, &tmp, lsn);

	if (mlip_changed) {
		if (!XFS_FORCED_SHUTDOWN(ailp->ail_mount))
			xlog_assign_tail_lsn_locked(ailp->ail_mount);
		spin_unlock(&ailp->ail_lock);

		xfs_log_space_wake(ailp->ail_mount);
	} else {
		spin_unlock(&ailp->ail_lock);
	}
}

bool
xfs_ail_delete_one(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	struct xfs_log_item	*mlip = xfs_ail_min(ailp);

	trace_xfs_ail_delete(lip, mlip->li_lsn, lip->li_lsn);
	xfs_ail_delete(ailp, lip);
	xfs_clear_li_failed(lip);
	clear_bit(XFS_LI_IN_AIL, &lip->li_flags);
	lip->li_lsn = 0;

	return mlip == lip;
}

/**
 * Remove a log items from the AIL
 *
 * @xfs_trans_ail_delete_bulk takes an array of log items that all need to
 * removed from the AIL. The caller is already holding the AIL lock, and done
 * all the checks necessary to ensure the items passed in via @log_items are
 * ready for deletion. This includes checking that the items are in the AIL.
 *
 * For each log item to be removed, unlink it  from the AIL, clear the IN_AIL
 * flag from the item and reset the item's lsn to 0. If we remove the first
 * item in the AIL, update the log tail to match the new minimum LSN in the
 * AIL.
 *
 * This function will not drop the AIL lock until all items are removed from
 * the AIL to minimise the amount of lock traffic on the AIL. This does not
 * greatly increase the AIL hold time, but does significantly reduce the amount
 * of traffic on the lock, especially during IO completion.
 *
 * This function must be called with the AIL lock held.  The lock is dropped
 * before returning.
 */
void
xfs_trans_ail_delete(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip,
	int			shutdown_type) __releases(ailp->ail_lock)
{
	struct xfs_mount	*mp = ailp->ail_mount;
	bool			mlip_changed;

	if (!test_bit(XFS_LI_IN_AIL, &lip->li_flags)) {
		spin_unlock(&ailp->ail_lock);
		if (!XFS_FORCED_SHUTDOWN(mp)) {
			xfs_alert_tag(mp, XFS_PTAG_AILDELETE,
	"%s: attempting to delete a log item that is not in the AIL",
					__func__);
			xfs_force_shutdown(mp, shutdown_type);
		}
		return;
	}

	mlip_changed = xfs_ail_delete_one(ailp, lip);
	if (mlip_changed) {
		if (!XFS_FORCED_SHUTDOWN(mp))
			xlog_assign_tail_lsn_locked(mp);
		if (list_empty(&ailp->ail_head))
			wake_up_all(&ailp->ail_empty);
	}

	spin_unlock(&ailp->ail_lock);
	if (mlip_changed)
		xfs_log_space_wake(ailp->ail_mount);
}

int
xfs_trans_ail_init(
	xfs_mount_t	*mp)
{
	struct xfs_ail	*ailp;

	ailp = kmem_zalloc(sizeof(struct xfs_ail), KM_MAYFAIL);
	if (!ailp)
		return -ENOMEM;

	ailp->ail_mount = mp;
	INIT_LIST_HEAD(&ailp->ail_head);
	INIT_LIST_HEAD(&ailp->ail_cursors);
	spin_lock_init(&ailp->ail_lock);
	INIT_LIST_HEAD(&ailp->ail_buf_list);
	init_waitqueue_head(&ailp->ail_empty);

	ailp->ail_task = kthread_run(xfsaild, ailp, "xfsaild/%s",
			ailp->ail_mount->m_fsname);
	if (IS_ERR(ailp->ail_task))
		goto out_free_ailp;

	mp->m_ail = ailp;
	return 0;

out_free_ailp:
	kmem_free(ailp);
	return -ENOMEM;
}

void
xfs_trans_ail_destroy(
	xfs_mount_t	*mp)
{
	struct xfs_ail	*ailp = mp->m_ail;

	kthread_stop(ailp->ail_task);
	kmem_free(ailp);
}
