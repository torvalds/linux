/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_dir.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"

STATIC void xfs_ail_insert(xfs_ail_entry_t *, xfs_log_item_t *);
STATIC xfs_log_item_t * xfs_ail_delete(xfs_ail_entry_t *, xfs_log_item_t *);
STATIC xfs_log_item_t * xfs_ail_min(xfs_ail_entry_t *);
STATIC xfs_log_item_t * xfs_ail_next(xfs_ail_entry_t *, xfs_log_item_t *);

#ifdef DEBUG
STATIC void xfs_ail_check(xfs_ail_entry_t *);
#else
#define	xfs_ail_check(a)
#endif /* DEBUG */


/*
 * This is called by the log manager code to determine the LSN
 * of the tail of the log.  This is exactly the LSN of the first
 * item in the AIL.  If the AIL is empty, then this function
 * returns 0.
 *
 * We need the AIL lock in order to get a coherent read of the
 * lsn of the last item in the AIL.
 */
xfs_lsn_t
xfs_trans_tail_ail(
	xfs_mount_t	*mp)
{
	xfs_lsn_t	lsn;
	xfs_log_item_t	*lip;
	SPLDECL(s);

	AIL_LOCK(mp,s);
	lip = xfs_ail_min(&(mp->m_ail));
	if (lip == NULL) {
		lsn = (xfs_lsn_t)0;
	} else {
		lsn = lip->li_lsn;
	}
	AIL_UNLOCK(mp, s);

	return lsn;
}

/*
 * xfs_trans_push_ail
 *
 * This routine is called to move the tail of the AIL
 * forward.  It does this by trying to flush items in the AIL
 * whose lsns are below the given threshold_lsn.
 *
 * The routine returns the lsn of the tail of the log.
 */
xfs_lsn_t
xfs_trans_push_ail(
	xfs_mount_t		*mp,
	xfs_lsn_t		threshold_lsn)
{
	xfs_lsn_t		lsn;
	xfs_log_item_t		*lip;
	int			gen;
	int			restarts;
	int			lock_result;
	int			flush_log;
	SPLDECL(s);

#define	XFS_TRANS_PUSH_AIL_RESTARTS	10

	AIL_LOCK(mp,s);
	lip = xfs_trans_first_ail(mp, &gen);
	if (lip == NULL || XFS_FORCED_SHUTDOWN(mp)) {
		/*
		 * Just return if the AIL is empty.
		 */
		AIL_UNLOCK(mp, s);
		return (xfs_lsn_t)0;
	}

	XFS_STATS_INC(xs_push_ail);

	/*
	 * While the item we are looking at is below the given threshold
	 * try to flush it out.  Make sure to limit the number of times
	 * we allow xfs_trans_next_ail() to restart scanning from the
	 * beginning of the list.  We'd like not to stop until we've at least
	 * tried to push on everything in the AIL with an LSN less than
	 * the given threshold. However, we may give up before that if
	 * we realize that we've been holding the AIL_LOCK for 'too long',
	 * blocking interrupts. Currently, too long is < 500us roughly.
	 */
	flush_log = 0;
	restarts = 0;
	while (((restarts < XFS_TRANS_PUSH_AIL_RESTARTS) &&
		(XFS_LSN_CMP(lip->li_lsn, threshold_lsn) < 0))) {
		/*
		 * If we can lock the item without sleeping, unlock
		 * the AIL lock and flush the item.  Then re-grab the
		 * AIL lock so we can look for the next item on the
		 * AIL.  Since we unlock the AIL while we flush the
		 * item, the next routine may start over again at the
		 * the beginning of the list if anything has changed.
		 * That is what the generation count is for.
		 *
		 * If we can't lock the item, either its holder will flush
		 * it or it is already being flushed or it is being relogged.
		 * In any of these case it is being taken care of and we
		 * can just skip to the next item in the list.
		 */
		lock_result = IOP_TRYLOCK(lip);
		switch (lock_result) {
		      case XFS_ITEM_SUCCESS:
			AIL_UNLOCK(mp, s);
			XFS_STATS_INC(xs_push_ail_success);
			IOP_PUSH(lip);
			AIL_LOCK(mp,s);
			break;

		      case XFS_ITEM_PUSHBUF:
			AIL_UNLOCK(mp, s);
			XFS_STATS_INC(xs_push_ail_pushbuf);
#ifdef XFSRACEDEBUG
			delay_for_intr();
			delay(300);
#endif
			ASSERT(lip->li_ops->iop_pushbuf);
			ASSERT(lip);
			IOP_PUSHBUF(lip);
			AIL_LOCK(mp,s);
			break;

		      case XFS_ITEM_PINNED:
			XFS_STATS_INC(xs_push_ail_pinned);
			flush_log = 1;
			break;

		      case XFS_ITEM_LOCKED:
			XFS_STATS_INC(xs_push_ail_locked);
			break;

		      case XFS_ITEM_FLUSHING:
			XFS_STATS_INC(xs_push_ail_flushing);
			break;

		      default:
			ASSERT(0);
			break;
		}

		lip = xfs_trans_next_ail(mp, lip, &gen, &restarts);
		if (lip == NULL) {
			break;
		}
		if (XFS_FORCED_SHUTDOWN(mp)) {
			/*
			 * Just return if we shut down during the last try.
			 */
			AIL_UNLOCK(mp, s);
			return (xfs_lsn_t)0;
		}

	}

	if (flush_log) {
		/*
		 * If something we need to push out was pinned, then
		 * push out the log so it will become unpinned and
		 * move forward in the AIL.
		 */
		AIL_UNLOCK(mp, s);
		XFS_STATS_INC(xs_push_ail_flush);
		xfs_log_force(mp, (xfs_lsn_t)0, XFS_LOG_FORCE);
		AIL_LOCK(mp, s);
	}

	lip = xfs_ail_min(&(mp->m_ail));
	if (lip == NULL) {
		lsn = (xfs_lsn_t)0;
	} else {
		lsn = lip->li_lsn;
	}

	AIL_UNLOCK(mp, s);
	return lsn;
}	/* xfs_trans_push_ail */


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
	xfs_mount_t	*mp,
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
	    XFS_FORCED_SHUTDOWN(mp)) {
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
	min_lip = xfs_ail_min(&mp->m_ail);

	if (min_lip == lip)
		xfs_log_move_tail(mp, 1);
}	/* xfs_trans_unlocked_item */


/*
 * Update the position of the item in the AIL with the new
 * lsn.  If it is not yet in the AIL, add it.  Otherwise, move
 * it to its new position by removing it and re-adding it.
 *
 * Wakeup anyone with an lsn less than the item's lsn.  If the item
 * we move in the AIL is the minimum one, update the tail lsn in the
 * log manager.
 *
 * Increment the AIL's generation count to indicate that the tree
 * has changed.
 *
 * This function must be called with the AIL lock held.  The lock
 * is dropped before returning, so the caller must pass in the
 * cookie returned by AIL_LOCK.
 */
void
xfs_trans_update_ail(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip,
	xfs_lsn_t	lsn,
	unsigned long	s)
{
	xfs_ail_entry_t		*ailp;
	xfs_log_item_t		*dlip=NULL;
	xfs_log_item_t		*mlip;	/* ptr to minimum lip */

	ailp = &(mp->m_ail);
	mlip = xfs_ail_min(ailp);

	if (lip->li_flags & XFS_LI_IN_AIL) {
		dlip = xfs_ail_delete(ailp, lip);
		ASSERT(dlip == lip);
	} else {
		lip->li_flags |= XFS_LI_IN_AIL;
	}

	lip->li_lsn = lsn;

	xfs_ail_insert(ailp, lip);
	mp->m_ail_gen++;

	if (mlip == dlip) {
		mlip = xfs_ail_min(&(mp->m_ail));
		AIL_UNLOCK(mp, s);
		xfs_log_move_tail(mp, mlip->li_lsn);
	} else {
		AIL_UNLOCK(mp, s);
	}


}	/* xfs_trans_update_ail */

/*
 * Delete the given item from the AIL.  It must already be in
 * the AIL.
 *
 * Wakeup anyone with an lsn less than item's lsn.    If the item
 * we delete in the AIL is the minimum one, update the tail lsn in the
 * log manager.
 *
 * Clear the IN_AIL flag from the item, reset its lsn to 0, and
 * bump the AIL's generation count to indicate that the tree
 * has changed.
 *
 * This function must be called with the AIL lock held.  The lock
 * is dropped before returning, so the caller must pass in the
 * cookie returned by AIL_LOCK.
 */
void
xfs_trans_delete_ail(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip,
	unsigned long	s)
{
	xfs_ail_entry_t		*ailp;
	xfs_log_item_t		*dlip;
	xfs_log_item_t		*mlip;

	if (lip->li_flags & XFS_LI_IN_AIL) {
		ailp = &(mp->m_ail);
		mlip = xfs_ail_min(ailp);
		dlip = xfs_ail_delete(ailp, lip);
		ASSERT(dlip == lip);


		lip->li_flags &= ~XFS_LI_IN_AIL;
		lip->li_lsn = 0;
		mp->m_ail_gen++;

		if (mlip == dlip) {
			mlip = xfs_ail_min(&(mp->m_ail));
			AIL_UNLOCK(mp, s);
			xfs_log_move_tail(mp, (mlip ? mlip->li_lsn : 0));
		} else {
			AIL_UNLOCK(mp, s);
		}
	}
	else {
		/*
		 * If the file system is not being shutdown, we are in
		 * serious trouble if we get to this stage.
		 */
		if (XFS_FORCED_SHUTDOWN(mp))
			AIL_UNLOCK(mp, s);
		else {
			xfs_cmn_err(XFS_PTAG_AILDELETE, CE_ALERT, mp,
				"xfs_trans_delete_ail: attempting to delete a log item that is not in the AIL");
			AIL_UNLOCK(mp, s);
			xfs_force_shutdown(mp, XFS_CORRUPT_INCORE);
		}
	}
}



/*
 * Return the item in the AIL with the smallest lsn.
 * Return the current tree generation number for use
 * in calls to xfs_trans_next_ail().
 */
xfs_log_item_t *
xfs_trans_first_ail(
	xfs_mount_t	*mp,
	int		*gen)
{
	xfs_log_item_t	*lip;

	lip = xfs_ail_min(&(mp->m_ail));
	*gen = (int)mp->m_ail_gen;

	return (lip);
}

/*
 * If the generation count of the tree has not changed since the
 * caller last took something from the AIL, then return the elmt
 * in the tree which follows the one given.  If the count has changed,
 * then return the minimum elmt of the AIL and bump the restarts counter
 * if one is given.
 */
xfs_log_item_t *
xfs_trans_next_ail(
	xfs_mount_t	*mp,
	xfs_log_item_t	*lip,
	int		*gen,
	int		*restarts)
{
	xfs_log_item_t	*nlip;

	ASSERT(mp && lip && gen);
	if (mp->m_ail_gen == *gen) {
		nlip = xfs_ail_next(&(mp->m_ail), lip);
	} else {
		nlip = xfs_ail_min(&(mp->m_ail));
		*gen = (int)mp->m_ail_gen;
		if (restarts != NULL) {
			XFS_STATS_INC(xs_push_ail_restarts);
			(*restarts)++;
		}
	}

	return (nlip);
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
void
xfs_trans_ail_init(
	xfs_mount_t	*mp)
{
	mp->m_ail.ail_forw = (xfs_log_item_t*)&(mp->m_ail);
	mp->m_ail.ail_back = (xfs_log_item_t*)&(mp->m_ail);
}

/*
 * Insert the given log item into the AIL.
 * We almost always insert at the end of the list, so on inserts
 * we search from the end of the list to find where the
 * new item belongs.
 */
STATIC void
xfs_ail_insert(
	xfs_ail_entry_t	*base,
	xfs_log_item_t	*lip)
/* ARGSUSED */
{
	xfs_log_item_t	*next_lip;

	/*
	 * If the list is empty, just insert the item.
	 */
	if (base->ail_back == (xfs_log_item_t*)base) {
		base->ail_forw = lip;
		base->ail_back = lip;
		lip->li_ail.ail_forw = (xfs_log_item_t*)base;
		lip->li_ail.ail_back = (xfs_log_item_t*)base;
		return;
	}

	next_lip = base->ail_back;
	while ((next_lip != (xfs_log_item_t*)base) &&
	       (XFS_LSN_CMP(next_lip->li_lsn, lip->li_lsn) > 0)) {
		next_lip = next_lip->li_ail.ail_back;
	}
	ASSERT((next_lip == (xfs_log_item_t*)base) ||
	       (XFS_LSN_CMP(next_lip->li_lsn, lip->li_lsn) <= 0));
	lip->li_ail.ail_forw = next_lip->li_ail.ail_forw;
	lip->li_ail.ail_back = next_lip;
	next_lip->li_ail.ail_forw = lip;
	lip->li_ail.ail_forw->li_ail.ail_back = lip;

	xfs_ail_check(base);
	return;
}

/*
 * Delete the given item from the AIL.  Return a pointer to the item.
 */
/*ARGSUSED*/
STATIC xfs_log_item_t *
xfs_ail_delete(
	xfs_ail_entry_t	*base,
	xfs_log_item_t	*lip)
/* ARGSUSED */
{
	lip->li_ail.ail_forw->li_ail.ail_back = lip->li_ail.ail_back;
	lip->li_ail.ail_back->li_ail.ail_forw = lip->li_ail.ail_forw;
	lip->li_ail.ail_forw = NULL;
	lip->li_ail.ail_back = NULL;

	xfs_ail_check(base);
	return lip;
}

/*
 * Return a pointer to the first item in the AIL.
 * If the AIL is empty, then return NULL.
 */
STATIC xfs_log_item_t *
xfs_ail_min(
	xfs_ail_entry_t	*base)
/* ARGSUSED */
{
	register xfs_log_item_t *forw = base->ail_forw;
	if (forw == (xfs_log_item_t*)base) {
		return NULL;
	}
	return forw;
}

/*
 * Return a pointer to the item which follows
 * the given item in the AIL.  If the given item
 * is the last item in the list, then return NULL.
 */
STATIC xfs_log_item_t *
xfs_ail_next(
	xfs_ail_entry_t	*base,
	xfs_log_item_t	*lip)
/* ARGSUSED */
{
	if (lip->li_ail.ail_forw == (xfs_log_item_t*)base) {
		return NULL;
	}
	return lip->li_ail.ail_forw;

}

#ifdef DEBUG
/*
 * Check that the list is sorted as it should be.
 */
STATIC void
xfs_ail_check(
	xfs_ail_entry_t *base)
{
	xfs_log_item_t	*lip;
	xfs_log_item_t	*prev_lip;

	lip = base->ail_forw;
	if (lip == (xfs_log_item_t*)base) {
		/*
		 * Make sure the pointers are correct when the list
		 * is empty.
		 */
		ASSERT(base->ail_back == (xfs_log_item_t*)base);
		return;
	}

	/*
	 * Walk the list checking forward and backward pointers,
	 * lsn ordering, and that every entry has the XFS_LI_IN_AIL
	 * flag set.
	 */
	prev_lip = (xfs_log_item_t*)base;
	while (lip != (xfs_log_item_t*)base) {
		if (prev_lip != (xfs_log_item_t*)base) {
			ASSERT(prev_lip->li_ail.ail_forw == lip);
			ASSERT(XFS_LSN_CMP(prev_lip->li_lsn, lip->li_lsn) <= 0);
		}
		ASSERT(lip->li_ail.ail_back == prev_lip);
		ASSERT((lip->li_flags & XFS_LI_IN_AIL) != 0);
		prev_lip = lip;
		lip = lip->li_ail.ail_forw;
	}
	ASSERT(lip == (xfs_log_item_t*)base);
	ASSERT(base->ail_back == prev_lip);
}
#endif /* DEBUG */
