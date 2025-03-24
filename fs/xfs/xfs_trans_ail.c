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
#include "xfs_log_priv.h"

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
	__must_hold(&ailp->ail_lock)
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
static xfs_lsn_t
__xfs_ail_min_lsn(
	struct xfs_ail		*ailp)
{
	struct xfs_log_item	*lip = xfs_ail_min(ailp);

	if (lip)
		return lip->li_lsn;
	return 0;
}

xfs_lsn_t
xfs_ail_min_lsn(
	struct xfs_ail		*ailp)
{
	xfs_lsn_t		lsn;

	spin_lock(&ailp->ail_lock);
	lsn = __xfs_ail_min_lsn(ailp);
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

/*
 * Requeue a failed buffer for writeback.
 *
 * We clear the log item failed state here as well, but we have to be careful
 * about reference counts because the only active reference counts on the buffer
 * may be the failed log items. Hence if we clear the log item failed state
 * before queuing the buffer for IO we can release all active references to
 * the buffer and free it, leading to use after free problems in
 * xfs_buf_delwri_queue. It makes no difference to the buffer or log items which
 * order we process them in - the buffer is locked, and we own the buffer list
 * so nothing on them is going to change while we are performing this action.
 *
 * Hence we can safely queue the buffer for IO before we clear the failed log
 * item state, therefore  always having an active reference to the buffer and
 * avoiding the transient zero-reference state that leads to use-after-free.
 */
static inline int
xfsaild_resubmit_item(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	struct xfs_buf		*bp = lip->li_buf;

	if (!xfs_buf_trylock(bp))
		return XFS_ITEM_LOCKED;

	if (!xfs_buf_delwri_queue(bp, buffer_list)) {
		xfs_buf_unlock(bp);
		return XFS_ITEM_FLUSHING;
	}

	/* protected by ail_lock */
	list_for_each_entry(lip, &bp->b_li_list, li_bio_list)
		clear_bit(XFS_LI_FAILED, &lip->li_flags);
	xfs_buf_unlock(bp);
	return XFS_ITEM_SUCCESS;
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
	if (XFS_TEST_ERROR(false, ailp->ail_log->l_mp, XFS_ERRTAG_LOG_ITEM_PIN))
		return XFS_ITEM_PINNED;

	/*
	 * Consider the item pinned if a push callback is not defined so the
	 * caller will force the log. This should only happen for intent items
	 * as they are unpinned once the associated done item is committed to
	 * the on-disk log.
	 */
	if (!lip->li_ops->iop_push)
		return XFS_ITEM_PINNED;
	if (test_bit(XFS_LI_FAILED, &lip->li_flags))
		return xfsaild_resubmit_item(lip, &ailp->ail_buf_list);
	return lip->li_ops->iop_push(lip, &ailp->ail_buf_list);
}

/*
 * Compute the LSN that we'd need to push the log tail towards in order to have
 * at least 25% of the log space free.  If the log free space already meets this
 * threshold, this function returns the lowest LSN in the AIL to slowly keep
 * writeback ticking over and the tail of the log moving forward.
 */
static xfs_lsn_t
xfs_ail_calc_push_target(
	struct xfs_ail		*ailp)
{
	struct xlog		*log = ailp->ail_log;
	struct xfs_log_item	*lip;
	xfs_lsn_t		target_lsn;
	xfs_lsn_t		max_lsn;
	xfs_lsn_t		min_lsn;
	int32_t			free_bytes;
	uint32_t		target_block;
	uint32_t		target_cycle;

	lockdep_assert_held(&ailp->ail_lock);

	lip = xfs_ail_max(ailp);
	if (!lip)
		return NULLCOMMITLSN;

	max_lsn = lip->li_lsn;
	min_lsn = __xfs_ail_min_lsn(ailp);

	/*
	 * If we are supposed to push all the items in the AIL, we want to push
	 * to the current head. We then clear the push flag so that we don't
	 * keep pushing newly queued items beyond where the push all command was
	 * run. If the push waiter wants to empty the ail, it should queue
	 * itself on the ail_empty wait queue.
	 */
	if (test_and_clear_bit(XFS_AIL_OPSTATE_PUSH_ALL, &ailp->ail_opstate))
		return max_lsn;

	/* If someone wants the AIL empty, keep pushing everything we have. */
	if (waitqueue_active(&ailp->ail_empty))
		return max_lsn;

	/*
	 * Background pushing - attempt to keep 25% of the log free and if we
	 * have that much free retain the existing target.
	 */
	free_bytes = log->l_logsize - xlog_lsn_sub(log, max_lsn, min_lsn);
	if (free_bytes >= log->l_logsize >> 2)
		return ailp->ail_target;

	target_cycle = CYCLE_LSN(min_lsn);
	target_block = BLOCK_LSN(min_lsn) + (log->l_logBBsize >> 2);
	if (target_block >= log->l_logBBsize) {
		target_block -= log->l_logBBsize;
		target_cycle += 1;
	}
	target_lsn = xlog_assign_lsn(target_cycle, target_block);

	/* Cap the target to the highest LSN known to be in the AIL. */
	if (XFS_LSN_CMP(target_lsn, max_lsn) > 0)
		return max_lsn;

	/* If the existing target is higher than the new target, keep it. */
	if (XFS_LSN_CMP(ailp->ail_target, target_lsn) >= 0)
		return ailp->ail_target;
	return target_lsn;
}

static long
xfsaild_push(
	struct xfs_ail		*ailp)
{
	struct xfs_mount	*mp = ailp->ail_log->l_mp;
	struct xfs_ail_cursor	cur;
	struct xfs_log_item	*lip;
	xfs_lsn_t		lsn;
	long			tout;
	int			stuck = 0;
	int			flushing = 0;
	int			count = 0;

	/*
	 * If we encountered pinned items or did not finish writing out all
	 * buffers the last time we ran, force a background CIL push to get the
	 * items unpinned in the near future. We do not wait on the CIL push as
	 * that could stall us for seconds if there is enough background IO
	 * load. Stalling for that long when the tail of the log is pinned and
	 * needs flushing will hard stop the transaction subsystem when log
	 * space runs out.
	 */
	if (ailp->ail_log_flush && ailp->ail_last_pushed_lsn == 0 &&
	    (!list_empty_careful(&ailp->ail_buf_list) ||
	     xfs_ail_min_lsn(ailp))) {
		ailp->ail_log_flush = 0;

		XFS_STATS_INC(mp, xs_push_ail_flush);
		xlog_cil_flush(ailp->ail_log);
	}

	spin_lock(&ailp->ail_lock);
	WRITE_ONCE(ailp->ail_target, xfs_ail_calc_push_target(ailp));
	if (ailp->ail_target == NULLCOMMITLSN)
		goto out_done;

	/* we're done if the AIL is empty or our push has reached the end */
	lip = xfs_trans_ail_cursor_first(ailp, &cur, ailp->ail_last_pushed_lsn);
	if (!lip)
		goto out_done_cursor;

	XFS_STATS_INC(mp, xs_push_ail);

	ASSERT(ailp->ail_target != NULLCOMMITLSN);

	lsn = lip->li_lsn;
	while ((XFS_LSN_CMP(lip->li_lsn, ailp->ail_target) <= 0)) {
		int	lock_result;

		if (test_bit(XFS_LI_FLUSHING, &lip->li_flags))
			goto next_item;

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
			 * The item or its backing buffer is already being
			 * flushed.  The typical reason for that is that an
			 * inode buffer is locked because we already pushed the
			 * updates to it as part of inode clustering.
			 *
			 * We do not want to stop flushing just because lots
			 * of items are already being flushed, but we need to
			 * re-try the flushing relatively soon if most of the
			 * AIL is being flushed.
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
		 * If we are skipping too many items because we can't flush
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

next_item:
		lip = xfs_trans_ail_cursor_next(ailp, &cur);
		if (lip == NULL)
			break;
		if (lip->li_lsn != lsn && count > 1000)
			break;
		lsn = lip->li_lsn;
	}

out_done_cursor:
	xfs_trans_ail_cursor_done(&cur);
out_done:
	spin_unlock(&ailp->ail_lock);

	if (xfs_buf_delwri_submit_nowait(&ailp->ail_buf_list))
		ailp->ail_log_flush++;

	if (!count || XFS_LSN_CMP(lsn, ailp->ail_target) >= 0) {
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
		tout = 0;
	}

	return tout;
}

static int
xfsaild(
	void		*data)
{
	struct xfs_ail	*ailp = data;
	long		tout = 0;	/* milliseconds */
	unsigned int	noreclaim_flag;

	noreclaim_flag = memalloc_noreclaim_save();
	set_freezable();

	while (1) {
		/*
		 * Long waits of 50ms or more occur when we've run out of items
		 * to push, so we only want uninterruptible state if we're
		 * actually blocked on something.
		 */
		if (tout && tout <= 20)
			set_current_state(TASK_KILLABLE|TASK_FREEZABLE);
		else
			set_current_state(TASK_INTERRUPTIBLE|TASK_FREEZABLE);

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
			       xlog_is_shutdown(ailp->ail_log));
			xfs_buf_delwri_cancel(&ailp->ail_buf_list);
			break;
		}

		/* Idle if the AIL is empty. */
		spin_lock(&ailp->ail_lock);
		if (!xfs_ail_min(ailp) && list_empty(&ailp->ail_buf_list)) {
			spin_unlock(&ailp->ail_lock);
			schedule();
			tout = 0;
			continue;
		}
		spin_unlock(&ailp->ail_lock);

		if (tout)
			schedule_timeout(msecs_to_jiffies(tout));

		__set_current_state(TASK_RUNNING);

		try_to_freeze();

		tout = xfsaild_push(ailp);
	}

	memalloc_noreclaim_restore(noreclaim_flag);
	return 0;
}

/*
 * Push out all items in the AIL immediately and wait until the AIL is empty.
 */
void
xfs_ail_push_all_sync(
	struct xfs_ail  *ailp)
{
	DEFINE_WAIT(wait);

	spin_lock(&ailp->ail_lock);
	while (xfs_ail_max(ailp) != NULL) {
		prepare_to_wait(&ailp->ail_empty, &wait, TASK_UNINTERRUPTIBLE);
		wake_up_process(ailp->ail_task);
		spin_unlock(&ailp->ail_lock);
		schedule();
		spin_lock(&ailp->ail_lock);
	}
	spin_unlock(&ailp->ail_lock);

	finish_wait(&ailp->ail_empty, &wait);
}

void
__xfs_ail_assign_tail_lsn(
	struct xfs_ail		*ailp)
{
	struct xlog		*log = ailp->ail_log;
	xfs_lsn_t		tail_lsn;

	assert_spin_locked(&ailp->ail_lock);

	if (xlog_is_shutdown(log))
		return;

	tail_lsn = __xfs_ail_min_lsn(ailp);
	if (!tail_lsn)
		tail_lsn = ailp->ail_head_lsn;

	WRITE_ONCE(log->l_tail_space,
			xlog_lsn_sub(log, ailp->ail_head_lsn, tail_lsn));
	trace_xfs_log_assign_tail_lsn(log, tail_lsn);
	atomic64_set(&log->l_tail_lsn, tail_lsn);
}

/*
 * Callers should pass the original tail lsn so that we can detect if the tail
 * has moved as a result of the operation that was performed. If the caller
 * needs to force a tail space update, it should pass NULLCOMMITLSN to bypass
 * the "did the tail LSN change?" checks. If the caller wants to avoid a tail
 * update (e.g. it knows the tail did not change) it should pass an @old_lsn of
 * 0.
 */
void
xfs_ail_update_finish(
	struct xfs_ail		*ailp,
	xfs_lsn_t		old_lsn) __releases(ailp->ail_lock)
{
	struct xlog		*log = ailp->ail_log;

	/* If the tail lsn hasn't changed, don't do updates or wakeups. */
	if (!old_lsn || old_lsn == __xfs_ail_min_lsn(ailp)) {
		spin_unlock(&ailp->ail_lock);
		return;
	}

	__xfs_ail_assign_tail_lsn(ailp);
	if (list_empty(&ailp->ail_head))
		wake_up_all(&ailp->ail_empty);
	spin_unlock(&ailp->ail_lock);
	xfs_log_space_wake(log->l_mp);
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
	xfs_lsn_t		tail_lsn = 0;
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
			if (mlip == lip && !tail_lsn)
				tail_lsn = lip->li_lsn;

			xfs_ail_delete(ailp, lip);
		} else {
			trace_xfs_ail_insert(lip, 0, lsn);
		}
		lip->li_lsn = lsn;
		list_add_tail(&lip->li_ail, &tmp);
	}

	if (!list_empty(&tmp))
		xfs_ail_splice(ailp, cur, &tmp, lsn);

	/*
	 * If this is the first insert, wake up the push daemon so it can
	 * actively scan for items to push. We also need to do a log tail
	 * LSN update to ensure that it is correctly tracked by the log, so
	 * set the tail_lsn to NULLCOMMITLSN so that xfs_ail_update_finish()
	 * will see that the tail lsn has changed and will update the tail
	 * appropriately.
	 */
	if (!mlip) {
		wake_up_process(ailp->ail_task);
		tail_lsn = NULLCOMMITLSN;
	}

	xfs_ail_update_finish(ailp, tail_lsn);
}

/* Insert a log item into the AIL. */
void
xfs_trans_ail_insert(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	spin_lock(&ailp->ail_lock);
	xfs_trans_ail_update_bulk(ailp, NULL, &lip, 1, lsn);
}

/*
 * Delete one log item from the AIL.
 *
 * If this item was at the tail of the AIL, return the LSN of the log item so
 * that we can use it to check if the LSN of the tail of the log has moved
 * when finishing up the AIL delete process in xfs_ail_update_finish().
 */
xfs_lsn_t
xfs_ail_delete_one(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	struct xfs_log_item	*mlip = xfs_ail_min(ailp);
	xfs_lsn_t		lsn = lip->li_lsn;

	trace_xfs_ail_delete(lip, mlip->li_lsn, lip->li_lsn);
	xfs_ail_delete(ailp, lip);
	clear_bit(XFS_LI_IN_AIL, &lip->li_flags);
	lip->li_lsn = 0;

	if (mlip == lip)
		return lsn;
	return 0;
}

void
xfs_trans_ail_delete(
	struct xfs_log_item	*lip,
	int			shutdown_type)
{
	struct xfs_ail		*ailp = lip->li_ailp;
	struct xlog		*log = ailp->ail_log;
	xfs_lsn_t		tail_lsn;

	spin_lock(&ailp->ail_lock);
	if (!test_bit(XFS_LI_IN_AIL, &lip->li_flags)) {
		spin_unlock(&ailp->ail_lock);
		if (shutdown_type && !xlog_is_shutdown(log)) {
			xfs_alert_tag(log->l_mp, XFS_PTAG_AILDELETE,
	"%s: attempting to delete a log item that is not in the AIL",
					__func__);
			xlog_force_shutdown(log, shutdown_type);
		}
		return;
	}

	/* xfs_ail_update_finish() drops the AIL lock */
	xfs_clear_li_failed(lip);
	tail_lsn = xfs_ail_delete_one(ailp, lip);
	xfs_ail_update_finish(ailp, tail_lsn);
}

int
xfs_trans_ail_init(
	xfs_mount_t	*mp)
{
	struct xfs_ail	*ailp;

	ailp = kzalloc(sizeof(struct xfs_ail),
			GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!ailp)
		return -ENOMEM;

	ailp->ail_log = mp->m_log;
	INIT_LIST_HEAD(&ailp->ail_head);
	INIT_LIST_HEAD(&ailp->ail_cursors);
	spin_lock_init(&ailp->ail_lock);
	INIT_LIST_HEAD(&ailp->ail_buf_list);
	init_waitqueue_head(&ailp->ail_empty);

	ailp->ail_task = kthread_run(xfsaild, ailp, "xfsaild/%s",
				mp->m_super->s_id);
	if (IS_ERR(ailp->ail_task))
		goto out_free_ailp;

	mp->m_ail = ailp;
	return 0;

out_free_ailp:
	kfree(ailp);
	return -ENOMEM;
}

void
xfs_trans_ail_destroy(
	xfs_mount_t	*mp)
{
	struct xfs_ail	*ailp = mp->m_ail;

	kthread_stop(ailp->ail_task);
	kfree(ailp);
}
