// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_ag.h"
#include "xfs_error.h"
#include "xfs_bit.h"
#include "xfs_icache.h"
#include "scrub/scrub.h"
#include "scrub/iscan.h"
#include "scrub/common.h"
#include "scrub/trace.h"

/*
 * Live File Scan
 * ==============
 *
 * Live file scans walk every inode in a live filesystem.  This is more or
 * less like a regular iwalk, except that when we're advancing the scan cursor,
 * we must ensure that inodes cannot be added or deleted anywhere between the
 * old cursor value and the new cursor value.  If we're advancing the cursor
 * by one inode, the caller must hold that inode; if we're finding the next
 * inode to scan, we must grab the AGI and hold it until we've updated the
 * scan cursor.
 *
 * Callers are expected to use this code to scan all files in the filesystem to
 * construct a new metadata index of some kind.  The scan races against other
 * live updates, which means there must be a provision to update the new index
 * when updates are made to inodes that already been scanned.  The iscan lock
 * can be used in live update hook code to stop the scan and protect this data
 * structure.
 *
 * To keep the new index up to date with other metadata updates being made to
 * the live filesystem, it is assumed that the caller will add hooks as needed
 * to be notified when a metadata update occurs.  The inode scanner must tell
 * the hook code when an inode has been visited with xchk_iscan_mark_visit.
 * Hook functions can use xchk_iscan_want_live_update to decide if the
 * scanner's observations must be updated.
 */

/*
 * Set *cursor to the next allocated inode after whatever it's set to now.
 * If there are no more inodes in this AG, cursor is set to NULLAGINO.
 */
STATIC int
xchk_iscan_find_next(
	struct xchk_iscan	*iscan,
	struct xfs_buf		*agi_bp,
	struct xfs_perag	*pag,
	xfs_agino_t		*cursor)
{
	struct xfs_scrub	*sc = iscan->sc;
	struct xfs_inobt_rec_incore	rec;
	struct xfs_btree_cur	*cur;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_trans	*tp = sc->tp;
	xfs_agnumber_t		agno = pag->pag_agno;
	xfs_agino_t		lastino = NULLAGINO;
	xfs_agino_t		first, last;
	xfs_agino_t		agino = *cursor;
	int			has_rec;
	int			error;

	/* If the cursor is beyond the end of this AG, move to the next one. */
	xfs_agino_range(mp, agno, &first, &last);
	if (agino > last) {
		*cursor = NULLAGINO;
		return 0;
	}

	/*
	 * Look up the inode chunk for the current cursor position.  If there
	 * is no chunk here, we want the next one.
	 */
	cur = xfs_inobt_init_cursor(pag, tp, agi_bp, XFS_BTNUM_INO);
	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &has_rec);
	if (!error && !has_rec)
		error = xfs_btree_increment(cur, 0, &has_rec);
	for (; !error; error = xfs_btree_increment(cur, 0, &has_rec)) {
		xfs_inofree_t	allocmask;

		/*
		 * If we've run out of inobt records in this AG, move the
		 * cursor on to the next AG and exit.  The caller can try
		 * again with the next AG.
		 */
		if (!has_rec) {
			*cursor = NULLAGINO;
			break;
		}

		error = xfs_inobt_get_rec(cur, &rec, &has_rec);
		if (error)
			break;
		if (!has_rec) {
			error = -EFSCORRUPTED;
			break;
		}

		/* Make sure that we always move forward. */
		if (lastino != NULLAGINO &&
		    XFS_IS_CORRUPT(mp, lastino >= rec.ir_startino)) {
			error = -EFSCORRUPTED;
			break;
		}
		lastino = rec.ir_startino + XFS_INODES_PER_CHUNK - 1;

		/*
		 * If this record only covers inodes that come before the
		 * cursor, advance to the next record.
		 */
		if (rec.ir_startino + XFS_INODES_PER_CHUNK <= agino)
			continue;

		/*
		 * If the incoming lookup put us in the middle of an inobt
		 * record, mark it and the previous inodes "free" so that the
		 * search for allocated inodes will start at the cursor.
		 * We don't care about ir_freecount here.
		 */
		if (agino >= rec.ir_startino)
			rec.ir_free |= xfs_inobt_maskn(0,
						agino + 1 - rec.ir_startino);

		/*
		 * If there are allocated inodes in this chunk, find them
		 * and update the scan cursor.
		 */
		allocmask = ~rec.ir_free;
		if (hweight64(allocmask) > 0) {
			int	next = xfs_lowbit64(allocmask);

			ASSERT(next >= 0);
			*cursor = rec.ir_startino + next;
			break;
		}
	}

	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * Advance both the scan and the visited cursors.
 *
 * The inumber address space for a given filesystem is sparse, which means that
 * the scan cursor can jump a long ways in a single iter() call.  There are no
 * inodes in these sparse areas, so we must move the visited cursor forward at
 * the same time so that the scan user can receive live updates for inodes that
 * may get created once we release the AGI buffer.
 */
static inline void
xchk_iscan_move_cursor(
	struct xchk_iscan	*iscan,
	xfs_agnumber_t		agno,
	xfs_agino_t		agino)
{
	struct xfs_scrub	*sc = iscan->sc;
	struct xfs_mount	*mp = sc->mp;
	xfs_ino_t		cursor, visited;

	BUILD_BUG_ON(XFS_MAXINUMBER == NULLFSINO);

	/*
	 * Special-case ino == 0 here so that we never set visited_ino to
	 * NULLFSINO when wrapping around EOFS, for that will let through all
	 * live updates.
	 */
	cursor = XFS_AGINO_TO_INO(mp, agno, agino);
	if (cursor == 0)
		visited = XFS_MAXINUMBER;
	else
		visited = cursor - 1;

	mutex_lock(&iscan->lock);
	iscan->cursor_ino = cursor;
	iscan->__visited_ino = visited;
	trace_xchk_iscan_move_cursor(iscan);
	mutex_unlock(&iscan->lock);
}

/*
 * Prepare to return agno/agino to the iscan caller by moving the lastino
 * cursor to the previous inode.  Do this while we still hold the AGI so that
 * no other threads can create or delete inodes in this AG.
 */
static inline void
xchk_iscan_finish(
	struct xchk_iscan	*iscan)
{
	mutex_lock(&iscan->lock);
	iscan->cursor_ino = NULLFSINO;

	/* All live updates will be applied from now on */
	iscan->__visited_ino = NULLFSINO;

	mutex_unlock(&iscan->lock);
}

/*
 * Advance ino to the next inode that the inobt thinks is allocated, being
 * careful to jump to the next AG if we've reached the right end of this AG's
 * inode btree.  Advancing ino effectively means that we've pushed the inode
 * scan forward, so set the iscan cursor to (ino - 1) so that our live update
 * predicates will track inode allocations in that part of the inode number
 * key space once we release the AGI buffer.
 *
 * Returns 1 if there's a new inode to examine, 0 if we've run out of inodes,
 * -ECANCELED if the live scan aborted, or the usual negative errno.
 */
STATIC int
xchk_iscan_advance(
	struct xchk_iscan	*iscan,
	struct xfs_perag	**pagp,
	struct xfs_buf		**agi_bpp)
{
	struct xfs_scrub	*sc = iscan->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*agi_bp;
	struct xfs_perag	*pag;
	xfs_agnumber_t		agno;
	xfs_agino_t		agino;
	int			ret;

	ASSERT(iscan->cursor_ino >= iscan->__visited_ino);

	do {
		if (xchk_iscan_aborted(iscan))
			return -ECANCELED;

		agno = XFS_INO_TO_AGNO(mp, iscan->cursor_ino);
		pag = xfs_perag_get(mp, agno);
		if (!pag)
			return -ECANCELED;

		ret = xfs_ialloc_read_agi(pag, sc->tp, &agi_bp);
		if (ret)
			goto out_pag;

		agino = XFS_INO_TO_AGINO(mp, iscan->cursor_ino);
		ret = xchk_iscan_find_next(iscan, agi_bp, pag, &agino);
		if (ret)
			goto out_buf;

		if (agino != NULLAGINO) {
			/*
			 * Found the next inode in this AG, so return it along
			 * with the AGI buffer and the perag structure to
			 * ensure it cannot go away.
			 */
			xchk_iscan_move_cursor(iscan, agno, agino);
			*agi_bpp = agi_bp;
			*pagp = pag;
			return 1;
		}

		/*
		 * Did not find any more inodes in this AG, move on to the next
		 * AG.
		 */
		agno = (agno + 1) % mp->m_sb.sb_agcount;
		xchk_iscan_move_cursor(iscan, agno, 0);
		xfs_trans_brelse(sc->tp, agi_bp);
		xfs_perag_put(pag);

		trace_xchk_iscan_advance_ag(iscan);
	} while (iscan->cursor_ino != iscan->scan_start_ino);

	xchk_iscan_finish(iscan);
	return 0;

out_buf:
	xfs_trans_brelse(sc->tp, agi_bp);
out_pag:
	xfs_perag_put(pag);
	return ret;
}

/*
 * Grabbing the inode failed, so we need to back up the scan and ask the caller
 * to try to _advance the scan again.  Returns -EBUSY if we've run out of retry
 * opportunities, -ECANCELED if the process has a fatal signal pending, or
 * -EAGAIN if we should try again.
 */
STATIC int
xchk_iscan_iget_retry(
	struct xchk_iscan	*iscan,
	bool			wait)
{
	ASSERT(iscan->cursor_ino == iscan->__visited_ino + 1);

	if (!iscan->iget_timeout ||
	    time_is_before_jiffies(iscan->__iget_deadline))
		return -EBUSY;

	if (wait) {
		unsigned long	relax;

		/*
		 * Sleep for a period of time to let the rest of the system
		 * catch up.  If we return early, someone sent a kill signal to
		 * the calling process.
		 */
		relax = msecs_to_jiffies(iscan->iget_retry_delay);
		trace_xchk_iscan_iget_retry_wait(iscan);

		if (schedule_timeout_killable(relax) ||
		    xchk_iscan_aborted(iscan))
			return -ECANCELED;
	}

	iscan->cursor_ino--;
	return -EAGAIN;
}

/*
 * Grab an inode as part of an inode scan.  While scanning this inode, the
 * caller must ensure that no other threads can modify the inode until a call
 * to xchk_iscan_visit succeeds.
 *
 * Returns 0 and an incore inode; -EAGAIN if the caller should call again
 * xchk_iscan_advance; -EBUSY if we couldn't grab an inode; -ECANCELED if
 * there's a fatal signal pending; or some other negative errno.
 */
STATIC int
xchk_iscan_iget(
	struct xchk_iscan	*iscan,
	struct xfs_perag	*pag,
	struct xfs_buf		*agi_bp,
	struct xfs_inode	**ipp)
{
	struct xfs_scrub	*sc = iscan->sc;
	struct xfs_mount	*mp = sc->mp;
	int			error;

	error = xfs_iget(sc->mp, sc->tp, iscan->cursor_ino, XFS_IGET_NORETRY,
			0, ipp);
	xfs_trans_brelse(sc->tp, agi_bp);
	xfs_perag_put(pag);

	trace_xchk_iscan_iget(iscan, error);

	if (error == -ENOENT || error == -EAGAIN) {
		/*
		 * It's possible that this inode has lost all of its links but
		 * hasn't yet been inactivated.  If we don't have a transaction
		 * or it's not writable, flush the inodegc workers and wait.
		 */
		xfs_inodegc_flush(mp);
		return xchk_iscan_iget_retry(iscan, true);
	}

	if (error == -EINVAL) {
		/*
		 * We thought the inode was allocated, but the inode btree
		 * lookup failed, which means that it was freed since the last
		 * time we advanced the cursor.  Back up and try again.  This
		 * should never happen since still hold the AGI buffer from the
		 * inobt check, but we need to be careful about infinite loops.
		 */
		return xchk_iscan_iget_retry(iscan, false);
	}

	return error;
}

/*
 * Advance the inode scan cursor to the next allocated inode and return the
 * incore inode structure associated with it.
 *
 * Returns 1 if there's a new inode to examine, 0 if we've run out of inodes,
 * -ECANCELED if the live scan aborted, -EBUSY if the incore inode could not be
 * grabbed, or the usual negative errno.
 *
 * If the function returns -EBUSY and the caller can handle skipping an inode,
 * it may call this function again to continue the scan with the next allocated
 * inode.
 */
int
xchk_iscan_iter(
	struct xchk_iscan	*iscan,
	struct xfs_inode	**ipp)
{
	struct xfs_scrub	*sc = iscan->sc;
	int			ret;

	if (iscan->iget_timeout)
		iscan->__iget_deadline = jiffies +
					 msecs_to_jiffies(iscan->iget_timeout);

	do {
		struct xfs_buf	*agi_bp = NULL;
		struct xfs_perag *pag = NULL;

		ret = xchk_iscan_advance(iscan, &pag, &agi_bp);
		if (ret != 1)
			return ret;

		if (xchk_iscan_aborted(iscan)) {
			xfs_trans_brelse(sc->tp, agi_bp);
			xfs_perag_put(pag);
			ret = -ECANCELED;
			break;
		}

		ret = xchk_iscan_iget(iscan, pag, agi_bp, ipp);
	} while (ret == -EAGAIN);

	if (!ret)
		return 1;

	return ret;
}


/* Mark this inode scan finished and release resources. */
void
xchk_iscan_teardown(
	struct xchk_iscan	*iscan)
{
	xchk_iscan_finish(iscan);
	mutex_destroy(&iscan->lock);
}

/* Pick an AG from which to start a scan. */
static inline xfs_ino_t
xchk_iscan_rotor(
	struct xfs_mount	*mp)
{
	static atomic_t		agi_rotor;
	unsigned int		r = atomic_inc_return(&agi_rotor) - 1;

	/*
	 * Rotoring *backwards* through the AGs, so we add one here before
	 * subtracting from the agcount to arrive at an AG number.
	 */
	r = (r % mp->m_sb.sb_agcount) + 1;

	return XFS_AGINO_TO_INO(mp, mp->m_sb.sb_agcount - r, 0);
}

/*
 * Set ourselves up to start an inode scan.  If the @iget_timeout and
 * @iget_retry_delay parameters are set, the scan will try to iget each inode
 * for @iget_timeout milliseconds.  If an iget call indicates that the inode is
 * waiting to be inactivated, the CPU will relax for @iget_retry_delay
 * milliseconds after pushing the inactivation workers.
 */
void
xchk_iscan_start(
	struct xfs_scrub	*sc,
	unsigned int		iget_timeout,
	unsigned int		iget_retry_delay,
	struct xchk_iscan	*iscan)
{
	xfs_ino_t		start_ino;

	start_ino = xchk_iscan_rotor(sc->mp);

	iscan->sc = sc;
	clear_bit(XCHK_ISCAN_OPSTATE_ABORTED, &iscan->__opstate);
	iscan->iget_timeout = iget_timeout;
	iscan->iget_retry_delay = iget_retry_delay;
	iscan->__visited_ino = start_ino;
	iscan->cursor_ino = start_ino;
	iscan->scan_start_ino = start_ino;
	mutex_init(&iscan->lock);

	trace_xchk_iscan_start(iscan, start_ino);
}

/*
 * Mark this inode as having been visited.  Callers must hold a sufficiently
 * exclusive lock on the inode to prevent concurrent modifications.
 */
void
xchk_iscan_mark_visited(
	struct xchk_iscan	*iscan,
	struct xfs_inode	*ip)
{
	mutex_lock(&iscan->lock);
	iscan->__visited_ino = ip->i_ino;
	trace_xchk_iscan_visit(iscan);
	mutex_unlock(&iscan->lock);
}

/*
 * Do we need a live update for this inode?  This is true if the scanner thread
 * has visited this inode and the scan hasn't been aborted due to errors.
 * Callers must hold a sufficiently exclusive lock on the inode to prevent
 * scanners from reading any inode metadata.
 */
bool
xchk_iscan_want_live_update(
	struct xchk_iscan	*iscan,
	xfs_ino_t		ino)
{
	bool			ret = false;

	if (xchk_iscan_aborted(iscan))
		return false;

	mutex_lock(&iscan->lock);

	trace_xchk_iscan_want_live_update(iscan, ino);

	/* Scan is finished, caller should receive all updates. */
	if (iscan->__visited_ino == NULLFSINO) {
		ret = true;
		goto unlock;
	}

	/*
	 * The visited cursor hasn't yet wrapped around the end of the FS.  If
	 * @ino is inside the starred range, the caller should receive updates:
	 *
	 * 0 ------------ S ************ V ------------ EOFS
	 */
	if (iscan->scan_start_ino <= iscan->__visited_ino) {
		if (ino >= iscan->scan_start_ino &&
		    ino <= iscan->__visited_ino)
			ret = true;

		goto unlock;
	}

	/*
	 * The visited cursor wrapped around the end of the FS.  If @ino is
	 * inside the starred range, the caller should receive updates:
	 *
	 * 0 ************ V ------------ S ************ EOFS
	 */
	if (ino >= iscan->scan_start_ino || ino <= iscan->__visited_ino)
		ret = true;

unlock:
	mutex_unlock(&iscan->lock);
	return ret;
}
