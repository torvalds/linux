/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2008 Dave Chinner
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
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"

#ifdef DEBUG
/*
 * Check that the list is sorted as it should be.
 */
STATIC void
xfs_ail_check(
	struct xfs_ail	*ailp,
	xfs_log_item_t	*lip)
{
	xfs_log_item_t	*prev_lip;

	if (list_empty(&ailp->xa_ail))
		return;

	/*
	 * Check the next and previous entries are valid.
	 */
	ASSERT((lip->li_flags & XFS_LI_IN_AIL) != 0);
	prev_lip = list_entry(lip->li_ail.prev, xfs_log_item_t, li_ail);
	if (&prev_lip->li_ail != &ailp->xa_ail)
		ASSERT(XFS_LSN_CMP(prev_lip->li_lsn, lip->li_lsn) <= 0);

	prev_lip = list_entry(lip->li_ail.next, xfs_log_item_t, li_ail);
	if (&prev_lip->li_ail != &ailp->xa_ail)
		ASSERT(XFS_LSN_CMP(prev_lip->li_lsn, lip->li_lsn) >= 0);


#ifdef XFS_TRANS_DEBUG
	/*
	 * Walk the list checking lsn ordering, and that every entry has the
	 * XFS_LI_IN_AIL flag set. This is really expensive, so only do it
	 * when specifically debugging the transaction subsystem.
	 */
	prev_lip = list_entry(&ailp->xa_ail, xfs_log_item_t, li_ail);
	list_for_each_entry(lip, &ailp->xa_ail, li_ail) {
		if (&prev_lip->li_ail != &ailp->xa_ail)
			ASSERT(XFS_LSN_CMP(prev_lip->li_lsn, lip->li_lsn) <= 0);
		ASSERT((lip->li_flags & XFS_LI_IN_AIL) != 0);
		prev_lip = lip;
	}
#endif /* XFS_TRANS_DEBUG */
}
#else /* !DEBUG */
#define	xfs_ail_check(a,l)
#endif /* DEBUG */

/*
 * Return a pointer to the first item in the AIL.  If the AIL is empty, then
 * return NULL.
 */
static xfs_log_item_t *
xfs_ail_min(
	struct xfs_ail  *ailp)
{
	if (list_empty(&ailp->xa_ail))
		return NULL;

	return list_first_entry(&ailp->xa_ail, xfs_log_item_t, li_ail);
}

 /*
 * Return a pointer to the last item in the AIL.  If the AIL is empty, then
 * return NULL.
 */
static xfs_log_item_t *
xfs_ail_max(
	struct xfs_ail  *ailp)
{
	if (list_empty(&ailp->xa_ail))
		return NULL;

	return list_entry(ailp->xa_ail.prev, xfs_log_item_t, li_ail);
}

/*
 * Return a pointer to the item which follows the given item in the AIL.  If
 * the given item is the last item in the list, then return NULL.
 */
static xfs_log_item_t *
xfs_ail_next(
	struct xfs_ail  *ailp,
	xfs_log_item_t  *lip)
{
	if (lip->li_ail.next == &ailp->xa_ail)
		return NULL;

	return list_first_entry(&lip->li_ail, xfs_log_item_t, li_ail);
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
	struct xfs_ail	*ailp)
{
	xfs_lsn_t	lsn = 0;
	xfs_log_item_t	*lip;

	spin_lock(&ailp->xa_lock);
	lip = xfs_ail_min(ailp);
	if (lip)
		lsn = lip->li_lsn;
	spin_unlock(&ailp->xa_lock);

	return lsn;
}

/*
 * Return the maximum lsn held in the AIL, or zero if the AIL is empty.
 */
static xfs_lsn_t
xfs_ail_max_lsn(
	struct xfs_ail  *ailp)
{
	xfs_lsn_t       lsn = 0;
	xfs_log_item_t  *lip;

	spin_lock(&ailp->xa_lock);
	lip = xfs_ail_max(ailp);
	if (lip)
		lsn = lip->li_lsn;
	spin_unlock(&ailp->xa_lock);

	return lsn;
}

/*
 * AIL traversal cursor initialisation.
 *
 * The cursor keeps track of where our current traversal is up
 * to by tracking the next ƣtem in the list for us. However, for
 * this to be safe, removing an object from the AIL needs to invalidate
 * any cursor that points to it. hence the traversal cursor needs to
 * be linked to the struct xfs_ail so that deletion can search all the
 * active cursors for invalidation.
 *
 * We don't link the push cursor because it is embedded in the struct
 * xfs_ail and hence easily findable.
 */
STATIC void
xfs_trans_ail_cursor_init(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur)
{
	cur->item = NULL;
	if (cur == &ailp->xa_cursors)
		return;

	cur->next = ailp->xa_cursors.next;
	ailp->xa_cursors.next = cur;
}

/*
 * Set the cursor to the next item, because when we look
 * up the cursor the current item may have been freed.
 */
STATIC void
xfs_trans_ail_cursor_set(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct xfs_log_item	*lip)
{
	if (lip)
		cur->item = xfs_ail_next(ailp, lip);
}

/*
 * Get the next item in the traversal and advance the cursor.
 * If the cursor was invalidated (inidicated by a lip of 1),
 * restart the traversal.
 */
struct xfs_log_item *
xfs_trans_ail_cursor_next(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur)
{
	struct xfs_log_item	*lip = cur->item;

	if ((__psint_t)lip & 1)
		lip = xfs_ail_min(ailp);
	xfs_trans_ail_cursor_set(ailp, cur, lip);
	return lip;
}

/*
 * Now that the traversal is complete, we need to remove the cursor
 * from the list of traversing cursors. Avoid removing the embedded
 * push cursor, but use the fact it is always present to make the
 * list deletion simple.
 */
void
xfs_trans_ail_cursor_done(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*done)
{
	struct xfs_ail_cursor	*prev = NULL;
	struct xfs_ail_cursor	*cur;

	done->item = NULL;
	if (done == &ailp->xa_cursors)
		return;
	prev = &ailp->xa_cursors;
	for (cur = prev->next; cur; prev = cur, cur = prev->next) {
		if (cur == done) {
			prev->next = cur->next;
			break;
		}
	}
	ASSERT(cur);
}

/*
 * Invalidate any cursor that is pointing to this item. This is
 * called when an item is removed from the AIL. Any cursor pointing
 * to this object is now invalid and the traversal needs to be
 * terminated so it doesn't reference a freed object. We set the
 * cursor item to a value of 1 so we can distinguish between an
 * invalidation and the end of the list when getting the next item
 * from the cursor.
 */
STATIC void
xfs_trans_ail_cursor_clear(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip)
{
	struct xfs_ail_cursor	*cur;

	/* need to search all cursors */
	for (cur = &ailp->xa_cursors; cur; cur = cur->next) {
		if (cur->item == lip)
			cur->item = (struct xfs_log_item *)
					((__psint_t)cur->item | 1);
	}
}

/*
 * Initialise the cursor to the first item in the AIL with the given @lsn.
 * This searches the list from lowest LSN to highest. Pass a @lsn of zero
 * to initialise the cursor to the first item in the AIL.
 */
xfs_log_item_t *
xfs_trans_ail_cursor_first(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	xfs_lsn_t		lsn)
{
	xfs_log_item_t		*lip;

	xfs_trans_ail_cursor_init(ailp, cur);
	lip = xfs_ail_min(ailp);
	if (lsn == 0)
		goto out;

	list_for_each_entry(lip, &ailp->xa_ail, li_ail) {
		if (XFS_LSN_CMP(lip->li_lsn, lsn) >= 0)
			goto out;
	}
	lip = NULL;
out:
	xfs_trans_ail_cursor_set(ailp, cur, lip);
	return lip;
}

/*
 * Initialise the cursor to the last item in the AIL with the given @lsn.
 * This searches the list from highest LSN to lowest. If there is no item with
 * the value of @lsn, then it sets the cursor to the last item with an LSN lower
 * than @lsn.
 */
static struct xfs_log_item *
__xfs_trans_ail_cursor_last(
	struct xfs_ail		*ailp,
	xfs_lsn_t		lsn)
{
	xfs_log_item_t		*lip;

	list_for_each_entry_reverse(lip, &ailp->xa_ail, li_ail) {
		if (XFS_LSN_CMP(lip->li_lsn, lsn) <= 0)
			return lip;
	}
	return NULL;
}

/*
 * Initialise the cursor to the last item in the AIL with the given @lsn.
 * This searches the list from highest LSN to lowest.
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
 * splice the log item list into the AIL at the given LSN. We splice to the
 * tail of the given LSN to maintain insert order for push traversals. The
 * cursor is optional, allowing repeated updates to the same LSN to avoid
 * repeated traversals.
 */
static void
xfs_ail_splice(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct list_head	*list,
	xfs_lsn_t		lsn)
{
	struct xfs_log_item	*lip = cur ? cur->item : NULL;
	struct xfs_log_item	*next_lip;

	/*
	 * Get a new cursor if we don't have a placeholder or the existing one
	 * has been invalidated.
	 */
	if (!lip || (__psint_t)lip & 1) {
		lip = __xfs_trans_ail_cursor_last(ailp, lsn);

		if (!lip) {
			/* The list is empty, so just splice and return.  */
			if (cur)
				cur->item = NULL;
			list_splice(list, &ailp->xa_ail);
			return;
		}
	}

	/*
	 * Our cursor points to the item we want to insert _after_, so we have
	 * to update the cursor to point to the end of the list we are splicing
	 * in so that it points to the correct location for the next splice.
	 * i.e. before the splice
	 *
	 *  lsn -> lsn -> lsn + x -> lsn + x ...
	 *          ^
	 *          | cursor points here
	 *
	 * After the splice we have:
	 *
	 *  lsn -> lsn -> lsn -> lsn -> .... -> lsn -> lsn + x -> lsn + x ...
	 *          ^                            ^
	 *          | cursor points here         | needs to move here
	 *
	 * So we set the cursor to the last item in the list to be spliced
	 * before we execute the splice, resulting in the cursor pointing to
	 * the correct item after the splice occurs.
	 */
	if (cur) {
		next_lip = list_entry(list->prev, struct xfs_log_item, li_ail);
		cur->item = next_lip;
	}
	list_splice(list, &lip->li_ail);
}

/*
 * Delete the given item from the AIL.  Return a pointer to the item.
 */
static void
xfs_ail_delete(
	struct xfs_ail  *ailp,
	xfs_log_item_t  *lip)
{
	xfs_ail_check(ailp, lip);
	list_del(&lip->li_ail);
	xfs_trans_ail_cursor_clear(ailp, lip);
}

static long
xfsaild_push(
	struct xfs_ail		*ailp)
{
	xfs_mount_t		*mp = ailp->xa_mount;
	struct xfs_ail_cursor	*cur = &ailp->xa_cursors;
	xfs_log_item_t		*lip;
	xfs_lsn_t		lsn;
	xfs_lsn_t		target;
	long			tout = 10;
	int			flush_log = 0;
	int			stuck = 0;
	int			count = 0;
	int			push_xfsbufd = 0;

	spin_lock(&ailp->xa_lock);
	target = ailp->xa_target;
	xfs_trans_ail_cursor_init(ailp, cur);
	lip = xfs_trans_ail_cursor_first(ailp, cur, ailp->xa_last_pushed_lsn);
	if (!lip || XFS_FORCED_SHUTDOWN(mp)) {
		/*
		 * AIL is empty or our push has reached the end.
		 */
		xfs_trans_ail_cursor_done(ailp, cur);
		spin_unlock(&ailp->xa_lock);
		goto out_done;
	}

	XFS_STATS_INC(xs_push_ail);

	/*
	 * While the item we are looking at is below the given threshold
	 * try to flush it out. We'd like not to stop until we've at least
	 * tried to push on everything in the AIL with an LSN less than
	 * the given threshold.
	 *
	 * However, we will stop after a certain number of pushes and wait
	 * for a reduced timeout to fire before pushing further. This
	 * prevents use from spinning when we can't do anything or there is
	 * lots of contention on the AIL lists.
	 */
	lsn = lip->li_lsn;
	while ((XFS_LSN_CMP(lip->li_lsn, target) <= 0)) {
		int	lock_result;
		/*
		 * If we can lock the item without sleeping, unlock the AIL
		 * lock and flush the item.  Then re-grab the AIL lock so we
		 * can look for the next item on the AIL. List changes are
		 * handled by the AIL lookup functions internally
		 *
		 * If we can't lock the item, either its holder will flush it
		 * or it is already being flushed or it is being relogged.  In
		 * any of these case it is being taken care of and we can just
		 * skip to the next item in the list.
		 */
		lock_result = IOP_TRYLOCK(lip);
		spin_unlock(&ailp->xa_lock);
		switch (lock_result) {
		case XFS_ITEM_SUCCESS:
			XFS_STATS_INC(xs_push_ail_success);
			IOP_PUSH(lip);
			ailp->xa_last_pushed_lsn = lsn;
			break;

		case XFS_ITEM_PUSHBUF:
			XFS_STATS_INC(xs_push_ail_pushbuf);

			if (!IOP_PUSHBUF(lip)) {
				stuck++;
				flush_log = 1;
			} else {
				ailp->xa_last_pushed_lsn = lsn;
			}
			push_xfsbufd = 1;
			break;

		case XFS_ITEM_PINNED:
			XFS_STATS_INC(xs_push_ail_pinned);
			stuck++;
			flush_log = 1;
			break;

		case XFS_ITEM_LOCKED:
			XFS_STATS_INC(xs_push_ail_locked);
			stuck++;
			break;

		default:
			ASSERT(0);
			break;
		}

		spin_lock(&ailp->xa_lock);
		/* should we bother continuing? */
		if (XFS_FORCED_SHUTDOWN(mp))
			break;
		ASSERT(mp->m_log);

		count++;

		/*
		 * Are there too many items we can't do anything with?
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

		lip = xfs_trans_ail_cursor_next(ailp, cur);
		if (lip == NULL)
			break;
		lsn = lip->li_lsn;
	}
	xfs_trans_ail_cursor_done(ailp, cur);
	spin_unlock(&ailp->xa_lock);

	if (flush_log) {
		/*
		 * If something we need to push out was pinned, then
		 * push out the log so it will become unpinned and
		 * move forward in the AIL.
		 */
		XFS_STATS_INC(xs_push_ail_flush);
		xfs_log_force(mp, 0);
	}

	if (push_xfsbufd) {
		/* we've got delayed write buffers to flush */
		wake_up_process(mp->m_ddev_targp->bt_task);
	}

	/* assume we have more work to do in a short while */
out_done:
	if (!count) {
		/* We're past our target or empty, so idle */
		ailp->xa_last_pushed_lsn = 0;

		tout = 50;
	} else if (XFS_LSN_CMP(lsn, target) >= 0) {
		/*
		 * We reached the target so wait a bit longer for I/O to
		 * complete and remove pushed items from the AIL before we
		 * start the next scan from the start of the AIL.
		 */
		tout = 50;
		ailp->xa_last_pushed_lsn = 0;
	} else if ((stuck * 100) / count > 90) {
		/*
		 * Either there is a lot of contention on the AIL or we
		 * are stuck due to operations in progress. "Stuck" in this
		 * case is defined as >90% of the items we tried to push
		 * were stuck.
		 *
		 * Backoff a bit more to allow some I/O to complete before
		 * continuing from where we were.
		 */
		tout = 20;
	}

	return tout;
}

static int
xfsaild(
	void		*data)
{
	struct xfs_ail	*ailp = data;
	long		tout = 0;	/* milliseconds */

	while (!kthread_should_stop()) {
		if (tout && tout <= 20)
			__set_current_state(TASK_KILLABLE);
		else
			__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(tout ?
				 msecs_to_jiffies(tout) : MAX_SCHEDULE_TIMEOUT);

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
	struct xfs_ail	*ailp,
	xfs_lsn_t	threshold_lsn)
{
	xfs_log_item_t	*lip;

	lip = xfs_ail_min(ailp);
	if (!lip || XFS_FORCED_SHUTDOWN(ailp->xa_mount) ||
	    XFS_LSN_CMP(threshold_lsn, ailp->xa_target) <= 0)
		return;

	/*
	 * Ensure that the new target is noticed in push code before it clears
	 * the XFS_AIL_PUSHING_BIT.
	 */
	smp_wmb();
	xfs_trans_ail_copy_lsn(ailp, &ailp->xa_target, &threshold_lsn);
	smp_wmb();

	wake_up_process(ailp->xa_task);
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
 * This is to be called when an item is unlocked that may have
 * been in the AIL.  It will wake up the first member of the AIL
 * wait list if this item's unlocking might allow it to progress.
 * If the item is in the AIL, then we need to get the AIL lock
 * while doing our checking so we don't race with someone going
 * to sleep waiting for this event in xfs_trans_push_ail().
 */
void
xfs_trans_unlocked_item(
	struct xfs_ail	*ailp,
	xfs_log_item_t	*lip)
{
	xfs_log_item_t	*min_lip;

	/*
	 * If we're forcibly shutting down, we may have
	 * unlocked log items arbitrarily. The last thing
	 * we want to do is to move the tail of the log
	 * over some potentially valid data.
	 */
	if (!(lip->li_flags & XFS_LI_IN_AIL) ||
	    XFS_FORCED_SHUTDOWN(ailp->xa_mount)) {
		return;
	}

	/*
	 * This is the one case where we can call into xfs_ail_min()
	 * without holding the AIL lock because we only care about the
	 * case where we are at the tail of the AIL.  If the object isn't
	 * at the tail, it doesn't matter what result we get back.  This
	 * is slightly racy because since we were just unlocked, we could
	 * go to sleep between the call to xfs_ail_min and the call to
	 * xfs_log_move_tail, have someone else lock us, commit to us disk,
	 * move us out of the tail of the AIL, and then we wake up.  However,
	 * the call to xfs_log_move_tail() doesn't do anything if there's
	 * not enough free space to wake people up so we're safe calling it.
	 */
	min_lip = xfs_ail_min(ailp);

	if (min_lip == lip)
		xfs_log_move_tail(ailp->xa_mount, 1);
}	/* xfs_trans_unlocked_item */

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
	xfs_lsn_t		lsn) __releases(ailp->xa_lock)
{
	xfs_log_item_t		*mlip;
	xfs_lsn_t		tail_lsn;
	int			mlip_changed = 0;
	int			i;
	LIST_HEAD(tmp);

	mlip = xfs_ail_min(ailp);

	for (i = 0; i < nr_items; i++) {
		struct xfs_log_item *lip = log_items[i];
		if (lip->li_flags & XFS_LI_IN_AIL) {
			/* check if we really need to move the item */
			if (XFS_LSN_CMP(lsn, lip->li_lsn) <= 0)
				continue;

			xfs_ail_delete(ailp, lip);
			if (mlip == lip)
				mlip_changed = 1;
		} else {
			lip->li_flags |= XFS_LI_IN_AIL;
		}
		lip->li_lsn = lsn;
		list_add(&lip->li_ail, &tmp);
	}

	xfs_ail_splice(ailp, cur, &tmp, lsn);

	if (!mlip_changed) {
		spin_unlock(&ailp->xa_lock);
		return;
	}

	/*
	 * It is not safe to access mlip after the AIL lock is dropped, so we
	 * must get a copy of li_lsn before we do so.  This is especially
	 * important on 32-bit platforms where accessing and updating 64-bit
	 * values like li_lsn is not atomic.
	 */
	mlip = xfs_ail_min(ailp);
	tail_lsn = mlip->li_lsn;
	spin_unlock(&ailp->xa_lock);
	xfs_log_move_tail(ailp->xa_mount, tail_lsn);
}

/*
 * xfs_trans_ail_delete_bulk - remove multiple log items from the AIL
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
xfs_trans_ail_delete_bulk(
	struct xfs_ail		*ailp,
	struct xfs_log_item	**log_items,
	int			nr_items) __releases(ailp->xa_lock)
{
	xfs_log_item_t		*mlip;
	xfs_lsn_t		tail_lsn;
	int			mlip_changed = 0;
	int			i;

	mlip = xfs_ail_min(ailp);

	for (i = 0; i < nr_items; i++) {
		struct xfs_log_item *lip = log_items[i];
		if (!(lip->li_flags & XFS_LI_IN_AIL)) {
			struct xfs_mount	*mp = ailp->xa_mount;

			spin_unlock(&ailp->xa_lock);
			if (!XFS_FORCED_SHUTDOWN(mp)) {
				xfs_alert_tag(mp, XFS_PTAG_AILDELETE,
		"%s: attempting to delete a log item that is not in the AIL",
						__func__);
				xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
			}
			return;
		}

		xfs_ail_delete(ailp, lip);
		lip->li_flags &= ~XFS_LI_IN_AIL;
		lip->li_lsn = 0;
		if (mlip == lip)
			mlip_changed = 1;
	}

	if (!mlip_changed) {
		spin_unlock(&ailp->xa_lock);
		return;
	}

	/*
	 * It is not safe to access mlip after the AIL lock is dropped, so we
	 * must get a copy of li_lsn before we do so.  This is especially
	 * important on 32-bit platforms where accessing and updating 64-bit
	 * values like li_lsn is not atomic. It is possible we've emptied the
	 * AIL here, so if that is the case, pass an LSN of 0 to the tail move.
	 */
	mlip = xfs_ail_min(ailp);
	tail_lsn = mlip ? mlip->li_lsn : 0;
	spin_unlock(&ailp->xa_lock);
	xfs_log_move_tail(ailp->xa_mount, tail_lsn);
}

/*
 * The active item list (AIL) is a doubly linked list of log
 * items sorted by ascending lsn.  The base of the list is
 * a forw/back pointer pair embedded in the xfs mount structure.
 * The base is initialized with both pointers pointing to the
 * base.  This case always needs to be distinguished, because
 * the base has no lsn to look at.  We almost always insert
 * at the end of the list, so on inserts we search from the
 * end of the list to find where the new item belongs.
 */

/*
 * Initialize the doubly linked list to point only to itself.
 */
int
xfs_trans_ail_init(
	xfs_mount_t	*mp)
{
	struct xfs_ail	*ailp;

	ailp = kmem_zalloc(sizeof(struct xfs_ail), KM_MAYFAIL);
	if (!ailp)
		return ENOMEM;

	ailp->xa_mount = mp;
	INIT_LIST_HEAD(&ailp->xa_ail);
	spin_lock_init(&ailp->xa_lock);

	ailp->xa_task = kthread_run(xfsaild, ailp, "xfsaild/%s",
			ailp->xa_mount->m_fsname);
	if (IS_ERR(ailp->xa_task))
		goto out_free_ailp;

	mp->m_ail = ailp;
	return 0;

out_free_ailp:
	kmem_free(ailp);
	return ENOMEM;
}

void
xfs_trans_ail_destroy(
	xfs_mount_t	*mp)
{
	struct xfs_ail	*ailp = mp->m_ail;

	kthread_stop(ailp->xa_task);
	kmem_free(ailp);
}
