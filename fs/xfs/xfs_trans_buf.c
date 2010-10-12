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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_trace.h"

/*
 * Check to see if a buffer matching the given parameters is already
 * a part of the given transaction.
 */
STATIC struct xfs_buf *
xfs_trans_buf_item_match(
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	int			len)
{
	struct xfs_log_item_desc *lidp;
	struct xfs_buf_log_item	*blip;

	len = BBTOB(len);
	list_for_each_entry(lidp, &tp->t_items, lid_trans) {
		blip = (struct xfs_buf_log_item *)lidp->lid_item;
		if (blip->bli_item.li_type == XFS_LI_BUF &&
		    XFS_BUF_TARGET(blip->bli_buf) == target &&
		    XFS_BUF_ADDR(blip->bli_buf) == blkno &&
		    XFS_BUF_COUNT(blip->bli_buf) == len)
			return blip->bli_buf;
	}

	return NULL;
}

/*
 * Add the locked buffer to the transaction.
 *
 * The buffer must be locked, and it cannot be associated with any
 * transaction.
 *
 * If the buffer does not yet have a buf log item associated with it,
 * then allocate one for it.  Then add the buf item to the transaction.
 */
STATIC void
_xfs_trans_bjoin(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	int			reset_recur)
{
	struct xfs_buf_log_item	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, void *) == NULL);

	/*
	 * The xfs_buf_log_item pointer is stored in b_fsprivate.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);
	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	if (reset_recur)
		bip->bli_recur = 0;

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	atomic_inc(&bip->bli_refcount);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &bip->bli_item);

	/*
	 * Initialize b_fsprivate2 so we can find it with incore_match()
	 * in xfs_trans_get_buf() and friends above.
	 */
	XFS_BUF_SET_FSPRIVATE2(bp, tp);

}

void
xfs_trans_bjoin(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp)
{
	_xfs_trans_bjoin(tp, bp, 0);
	trace_xfs_trans_bjoin(bp->b_fspriv);
}

/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * If the transaction pointer is NULL, make this just a normal
 * get_buf() call.
 */
xfs_buf_t *
xfs_trans_get_buf(xfs_trans_t	*tp,
		  xfs_buftarg_t	*target_dev,
		  xfs_daddr_t	blkno,
		  int		len,
		  uint		flags)
{
	xfs_buf_t		*bp;
	xfs_buf_log_item_t	*bip;

	if (flags == 0)
		flags = XBF_LOCK | XBF_MAPPED;

	/*
	 * Default to a normal get_buf() call if the tp is NULL.
	 */
	if (tp == NULL)
		return xfs_buf_get(target_dev, blkno, len,
				   flags | XBF_DONT_BLOCK);

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	bp = xfs_trans_buf_item_match(tp, target_dev, blkno, len);
	if (bp != NULL) {
		ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);
		if (XFS_FORCED_SHUTDOWN(tp->t_mountp))
			XFS_BUF_SUPER_STALE(bp);

		/*
		 * If the buffer is stale then it was binval'ed
		 * since last read.  This doesn't matter since the
		 * caller isn't allowed to use the data anyway.
		 */
		else if (XFS_BUF_ISSTALE(bp))
			ASSERT(!XFS_BUF_ISDELAYWRITE(bp));

		ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
		bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
		ASSERT(bip != NULL);
		ASSERT(atomic_read(&bip->bli_refcount) > 0);
		bip->bli_recur++;
		trace_xfs_trans_get_buf_recur(bip);
		return (bp);
	}

	/*
	 * We always specify the XBF_DONT_BLOCK flag within a transaction
	 * so that get_buf does not try to push out a delayed write buffer
	 * which might cause another transaction to take place (if the
	 * buffer was delayed alloc).  Such recursive transactions can
	 * easily deadlock with our current transaction as well as cause
	 * us to run out of stack space.
	 */
	bp = xfs_buf_get(target_dev, blkno, len, flags | XBF_DONT_BLOCK);
	if (bp == NULL) {
		return NULL;
	}

	ASSERT(!XFS_BUF_GETERROR(bp));

	_xfs_trans_bjoin(tp, bp, 1);
	trace_xfs_trans_get_buf(bp->b_fspriv);
	return (bp);
}

/*
 * Get and lock the superblock buffer of this file system for the
 * given transaction.
 *
 * We don't need to use incore_match() here, because the superblock
 * buffer is a private buffer which we keep a pointer to in the
 * mount structure.
 */
xfs_buf_t *
xfs_trans_getsb(xfs_trans_t	*tp,
		struct xfs_mount *mp,
		int		flags)
{
	xfs_buf_t		*bp;
	xfs_buf_log_item_t	*bip;

	/*
	 * Default to just trying to lock the superblock buffer
	 * if tp is NULL.
	 */
	if (tp == NULL) {
		return (xfs_getsb(mp, flags));
	}

	/*
	 * If the superblock buffer already has this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	bp = mp->m_sb_bp;
	if (XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp) {
		bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t*);
		ASSERT(bip != NULL);
		ASSERT(atomic_read(&bip->bli_refcount) > 0);
		bip->bli_recur++;
		trace_xfs_trans_getsb_recur(bip);
		return (bp);
	}

	bp = xfs_getsb(mp, flags);
	if (bp == NULL)
		return NULL;

	_xfs_trans_bjoin(tp, bp, 1);
	trace_xfs_trans_getsb(bp->b_fspriv);
	return (bp);
}

#ifdef DEBUG
xfs_buftarg_t *xfs_error_target;
int	xfs_do_error;
int	xfs_req_num;
int	xfs_error_mod = 33;
#endif

/*
 * Get and lock the buffer for the caller if it is not already
 * locked within the given transaction.  If it has not yet been
 * read in, read it from disk. If it is already locked
 * within the transaction and already read in, just increment its
 * lock recursion count and return a pointer to it.
 *
 * If the transaction pointer is NULL, make this just a normal
 * read_buf() call.
 */
int
xfs_trans_read_buf(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_buftarg_t	*target,
	xfs_daddr_t	blkno,
	int		len,
	uint		flags,
	xfs_buf_t	**bpp)
{
	xfs_buf_t		*bp;
	xfs_buf_log_item_t	*bip;
	int			error;

	if (flags == 0)
		flags = XBF_LOCK | XBF_MAPPED;

	/*
	 * Default to a normal get_buf() call if the tp is NULL.
	 */
	if (tp == NULL) {
		bp = xfs_buf_read(target, blkno, len, flags | XBF_DONT_BLOCK);
		if (!bp)
			return (flags & XBF_TRYLOCK) ?
					EAGAIN : XFS_ERROR(ENOMEM);

		if (XFS_BUF_GETERROR(bp) != 0) {
			xfs_ioerror_alert("xfs_trans_read_buf", mp,
					  bp, blkno);
			error = XFS_BUF_GETERROR(bp);
			xfs_buf_relse(bp);
			return error;
		}
#ifdef DEBUG
		if (xfs_do_error) {
			if (xfs_error_target == target) {
				if (((xfs_req_num++) % xfs_error_mod) == 0) {
					xfs_buf_relse(bp);
					cmn_err(CE_DEBUG, "Returning error!\n");
					return XFS_ERROR(EIO);
				}
			}
		}
#endif
		if (XFS_FORCED_SHUTDOWN(mp))
			goto shutdown_abort;
		*bpp = bp;
		return 0;
	}

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  If it is already read in we just increment
	 * the lock recursion count and return the buffer to the caller.
	 * If the buffer is not yet read in, then we read it in, increment
	 * the lock recursion count, and return it to the caller.
	 */
	bp = xfs_trans_buf_item_match(tp, target, blkno, len);
	if (bp != NULL) {
		ASSERT(XFS_BUF_VALUSEMA(bp) <= 0);
		ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
		ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);
		ASSERT((XFS_BUF_ISERROR(bp)) == 0);
		if (!(XFS_BUF_ISDONE(bp))) {
			trace_xfs_trans_read_buf_io(bp, _RET_IP_);
			ASSERT(!XFS_BUF_ISASYNC(bp));
			XFS_BUF_READ(bp);
			xfsbdstrat(tp->t_mountp, bp);
			error = xfs_iowait(bp);
			if (error) {
				xfs_ioerror_alert("xfs_trans_read_buf", mp,
						  bp, blkno);
				xfs_buf_relse(bp);
				/*
				 * We can gracefully recover from most read
				 * errors. Ones we can't are those that happen
				 * after the transaction's already dirty.
				 */
				if (tp->t_flags & XFS_TRANS_DIRTY)
					xfs_force_shutdown(tp->t_mountp,
							SHUTDOWN_META_IO_ERROR);
				return error;
			}
		}
		/*
		 * We never locked this buf ourselves, so we shouldn't
		 * brelse it either. Just get out.
		 */
		if (XFS_FORCED_SHUTDOWN(mp)) {
			trace_xfs_trans_read_buf_shut(bp, _RET_IP_);
			*bpp = NULL;
			return XFS_ERROR(EIO);
		}


		bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t*);
		bip->bli_recur++;

		ASSERT(atomic_read(&bip->bli_refcount) > 0);
		trace_xfs_trans_read_buf_recur(bip);
		*bpp = bp;
		return 0;
	}

	/*
	 * We always specify the XBF_DONT_BLOCK flag within a transaction
	 * so that get_buf does not try to push out a delayed write buffer
	 * which might cause another transaction to take place (if the
	 * buffer was delayed alloc).  Such recursive transactions can
	 * easily deadlock with our current transaction as well as cause
	 * us to run out of stack space.
	 */
	bp = xfs_buf_read(target, blkno, len, flags | XBF_DONT_BLOCK);
	if (bp == NULL) {
		*bpp = NULL;
		return 0;
	}
	if (XFS_BUF_GETERROR(bp) != 0) {
	    XFS_BUF_SUPER_STALE(bp);
		error = XFS_BUF_GETERROR(bp);

		xfs_ioerror_alert("xfs_trans_read_buf", mp,
				  bp, blkno);
		if (tp->t_flags & XFS_TRANS_DIRTY)
			xfs_force_shutdown(tp->t_mountp, SHUTDOWN_META_IO_ERROR);
		xfs_buf_relse(bp);
		return error;
	}
#ifdef DEBUG
	if (xfs_do_error && !(tp->t_flags & XFS_TRANS_DIRTY)) {
		if (xfs_error_target == target) {
			if (((xfs_req_num++) % xfs_error_mod) == 0) {
				xfs_force_shutdown(tp->t_mountp,
						   SHUTDOWN_META_IO_ERROR);
				xfs_buf_relse(bp);
				cmn_err(CE_DEBUG, "Returning trans error!\n");
				return XFS_ERROR(EIO);
			}
		}
	}
#endif
	if (XFS_FORCED_SHUTDOWN(mp))
		goto shutdown_abort;

	_xfs_trans_bjoin(tp, bp, 1);
	trace_xfs_trans_read_buf(bp->b_fspriv);

	*bpp = bp;
	return 0;

shutdown_abort:
	/*
	 * the theory here is that buffer is good but we're
	 * bailing out because the filesystem is being forcibly
	 * shut down.  So we should leave the b_flags alone since
	 * the buffer's not staled and just get out.
	 */
#if defined(DEBUG)
	if (XFS_BUF_ISSTALE(bp) && XFS_BUF_ISDELAYWRITE(bp))
		cmn_err(CE_NOTE, "about to pop assert, bp == 0x%p", bp);
#endif
	ASSERT((XFS_BUF_BFLAGS(bp) & (XBF_STALE|XBF_DELWRI)) !=
				     (XBF_STALE|XBF_DELWRI));

	trace_xfs_trans_read_buf_shut(bp, _RET_IP_);
	xfs_buf_relse(bp);
	*bpp = NULL;
	return XFS_ERROR(EIO);
}


/*
 * Release the buffer bp which was previously acquired with one of the
 * xfs_trans_... buffer allocation routines if the buffer has not
 * been modified within this transaction.  If the buffer is modified
 * within this transaction, do decrement the recursion count but do
 * not release the buffer even if the count goes to 0.  If the buffer is not
 * modified within the transaction, decrement the recursion count and
 * release the buffer if the recursion count goes to 0.
 *
 * If the buffer is to be released and it was not modified before
 * this transaction began, then free the buf_log_item associated with it.
 *
 * If the transaction pointer is NULL, make this just a normal
 * brelse() call.
 */
void
xfs_trans_brelse(xfs_trans_t	*tp,
		 xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;
	xfs_log_item_t		*lip;

	/*
	 * Default to a normal brelse() call if the tp is NULL.
	 */
	if (tp == NULL) {
		ASSERT(XFS_BUF_FSPRIVATE2(bp, void *) == NULL);
		/*
		 * If there's a buf log item attached to the buffer,
		 * then let the AIL know that the buffer is being
		 * unlocked.
		 */
		if (XFS_BUF_FSPRIVATE(bp, void *) != NULL) {
			lip = XFS_BUF_FSPRIVATE(bp, xfs_log_item_t *);
			if (lip->li_type == XFS_LI_BUF) {
				bip = XFS_BUF_FSPRIVATE(bp,xfs_buf_log_item_t*);
				xfs_trans_unlocked_item(bip->bli_item.li_ailp,
							lip);
			}
		}
		xfs_buf_relse(bp);
		return;
	}

	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	trace_xfs_trans_brelse(bip);

	/*
	 * If the release is just for a recursive lock,
	 * then decrement the count and return.
	 */
	if (bip->bli_recur > 0) {
		bip->bli_recur--;
		return;
	}

	/*
	 * If the buffer is dirty within this transaction, we can't
	 * release it until we commit.
	 */
	if (bip->bli_item.li_desc->lid_flags & XFS_LID_DIRTY)
		return;

	/*
	 * If the buffer has been invalidated, then we can't release
	 * it until the transaction commits to disk unless it is re-dirtied
	 * as part of this transaction.  This prevents us from pulling
	 * the item from the AIL before we should.
	 */
	if (bip->bli_flags & XFS_BLI_STALE)
		return;

	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));

	/*
	 * Free up the log item descriptor tracking the released item.
	 */
	xfs_trans_del_item(&bip->bli_item);

	/*
	 * Clear the hold flag in the buf log item if it is set.
	 * We wouldn't want the next user of the buffer to
	 * get confused.
	 */
	if (bip->bli_flags & XFS_BLI_HOLD) {
		bip->bli_flags &= ~XFS_BLI_HOLD;
	}

	/*
	 * Drop our reference to the buf log item.
	 */
	atomic_dec(&bip->bli_refcount);

	/*
	 * If the buf item is not tracking data in the log, then
	 * we must free it before releasing the buffer back to the
	 * free pool.  Before releasing the buffer to the free pool,
	 * clear the transaction pointer in b_fsprivate2 to dissolve
	 * its relation to this transaction.
	 */
	if (!xfs_buf_item_dirty(bip)) {
/***
		ASSERT(bp->b_pincount == 0);
***/
		ASSERT(atomic_read(&bip->bli_refcount) == 0);
		ASSERT(!(bip->bli_item.li_flags & XFS_LI_IN_AIL));
		ASSERT(!(bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF));
		xfs_buf_item_relse(bp);
		bip = NULL;
	}
	XFS_BUF_SET_FSPRIVATE2(bp, NULL);

	/*
	 * If we've still got a buf log item on the buffer, then
	 * tell the AIL that the buffer is being unlocked.
	 */
	if (bip != NULL) {
		xfs_trans_unlocked_item(bip->bli_item.li_ailp,
					(xfs_log_item_t*)bip);
	}

	xfs_buf_relse(bp);
	return;
}

/*
 * Mark the buffer as not needing to be unlocked when the buf item's
 * IOP_UNLOCK() routine is called.  The buffer must already be locked
 * and associated with the given transaction.
 */
/* ARGSUSED */
void
xfs_trans_bhold(xfs_trans_t	*tp,
		xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	bip->bli_flags |= XFS_BLI_HOLD;
	trace_xfs_trans_bhold(bip);
}

/*
 * Cancel the previous buffer hold request made on this buffer
 * for this transaction.
 */
void
xfs_trans_bhold_release(xfs_trans_t	*tp,
			xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT(bip->bli_flags & XFS_BLI_HOLD);
	bip->bli_flags &= ~XFS_BLI_HOLD;

	trace_xfs_trans_bhold_release(bip);
}

/*
 * This is called to mark bytes first through last inclusive of the given
 * buffer as needing to be logged when the transaction is committed.
 * The buffer must already be associated with the given transaction.
 *
 * First and last are numbers relative to the beginning of this buffer,
 * so the first byte in the buffer is numbered 0 regardless of the
 * value of b_blkno.
 */
void
xfs_trans_log_buf(xfs_trans_t	*tp,
		  xfs_buf_t	*bp,
		  uint		first,
		  uint		last)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);
	ASSERT((first <= last) && (last < XFS_BUF_COUNT(bp)));
	ASSERT((XFS_BUF_IODONE_FUNC(bp) == NULL) ||
	       (XFS_BUF_IODONE_FUNC(bp) == xfs_buf_iodone_callbacks));

	/*
	 * Mark the buffer as needing to be written out eventually,
	 * and set its iodone function to remove the buffer's buf log
	 * item from the AIL and free it when the buffer is flushed
	 * to disk.  See xfs_buf_attach_iodone() for more details
	 * on li_cb and xfs_buf_iodone_callbacks().
	 * If we end up aborting this transaction, we trap this buffer
	 * inside the b_bdstrat callback so that this won't get written to
	 * disk.
	 */
	XFS_BUF_DELAYWRITE(bp);
	XFS_BUF_DONE(bp);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	XFS_BUF_SET_IODONE_FUNC(bp, xfs_buf_iodone_callbacks);
	bip->bli_item.li_cb = xfs_buf_iodone;

	trace_xfs_trans_log_buf(bip);

	/*
	 * If we invalidated the buffer within this transaction, then
	 * cancel the invalidation now that we're dirtying the buffer
	 * again.  There are no races with the code in xfs_buf_item_unpin(),
	 * because we have a reference to the buffer this entire time.
	 */
	if (bip->bli_flags & XFS_BLI_STALE) {
		bip->bli_flags &= ~XFS_BLI_STALE;
		ASSERT(XFS_BUF_ISSTALE(bp));
		XFS_BUF_UNSTALE(bp);
		bip->bli_format.blf_flags &= ~XFS_BLF_CANCEL;
	}

	tp->t_flags |= XFS_TRANS_DIRTY;
	bip->bli_item.li_desc->lid_flags |= XFS_LID_DIRTY;
	bip->bli_flags |= XFS_BLI_LOGGED;
	xfs_buf_item_log(bip, first, last);
}


/*
 * This called to invalidate a buffer that is being used within
 * a transaction.  Typically this is because the blocks in the
 * buffer are being freed, so we need to prevent it from being
 * written out when we're done.  Allowing it to be written again
 * might overwrite data in the free blocks if they are reallocated
 * to a file.
 *
 * We prevent the buffer from being written out by clearing the
 * B_DELWRI flag.  We can't always
 * get rid of the buf log item at this point, though, because
 * the buffer may still be pinned by another transaction.  If that
 * is the case, then we'll wait until the buffer is committed to
 * disk for the last time (we can tell by the ref count) and
 * free it in xfs_buf_item_unpin().  Until it is cleaned up we
 * will keep the buffer locked so that the buffer and buf log item
 * are not reused.
 */
void
xfs_trans_binval(
	xfs_trans_t	*tp,
	xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	trace_xfs_trans_binval(bip);

	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * If the buffer is already invalidated, then
		 * just return.
		 */
		ASSERT(!(XFS_BUF_ISDELAYWRITE(bp)));
		ASSERT(XFS_BUF_ISSTALE(bp));
		ASSERT(!(bip->bli_flags & (XFS_BLI_LOGGED | XFS_BLI_DIRTY)));
		ASSERT(!(bip->bli_format.blf_flags & XFS_BLF_INODE_BUF));
		ASSERT(bip->bli_format.blf_flags & XFS_BLF_CANCEL);
		ASSERT(bip->bli_item.li_desc->lid_flags & XFS_LID_DIRTY);
		ASSERT(tp->t_flags & XFS_TRANS_DIRTY);
		return;
	}

	/*
	 * Clear the dirty bit in the buffer and set the STALE flag
	 * in the buf log item.  The STALE flag will be used in
	 * xfs_buf_item_unpin() to determine if it should clean up
	 * when the last reference to the buf item is given up.
	 * We set the XFS_BLF_CANCEL flag in the buf log format structure
	 * and log the buf item.  This will be used at recovery time
	 * to determine that copies of the buffer in the log before
	 * this should not be replayed.
	 * We mark the item descriptor and the transaction dirty so
	 * that we'll hold the buffer until after the commit.
	 *
	 * Since we're invalidating the buffer, we also clear the state
	 * about which parts of the buffer have been logged.  We also
	 * clear the flag indicating that this is an inode buffer since
	 * the data in the buffer will no longer be valid.
	 *
	 * We set the stale bit in the buffer as well since we're getting
	 * rid of it.
	 */
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_STALE(bp);
	bip->bli_flags |= XFS_BLI_STALE;
	bip->bli_flags &= ~(XFS_BLI_INODE_BUF | XFS_BLI_LOGGED | XFS_BLI_DIRTY);
	bip->bli_format.blf_flags &= ~XFS_BLF_INODE_BUF;
	bip->bli_format.blf_flags |= XFS_BLF_CANCEL;
	memset((char *)(bip->bli_format.blf_data_map), 0,
	      (bip->bli_format.blf_map_size * sizeof(uint)));
	bip->bli_item.li_desc->lid_flags |= XFS_LID_DIRTY;
	tp->t_flags |= XFS_TRANS_DIRTY;
}

/*
 * This call is used to indicate that the buffer contains on-disk inodes which
 * must be handled specially during recovery.  They require special handling
 * because only the di_next_unlinked from the inodes in the buffer should be
 * recovered.  The rest of the data in the buffer is logged via the inodes
 * themselves.
 *
 * All we do is set the XFS_BLI_INODE_BUF flag in the items flags so it can be
 * transferred to the buffer's log format structure so that we'll know what to
 * do at recovery time.
 */
void
xfs_trans_inode_buf(
	xfs_trans_t	*tp,
	xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_INODE_BUF;
}

/*
 * This call is used to indicate that the buffer is going to
 * be staled and was an inode buffer. This means it gets
 * special processing during unpin - where any inodes 
 * associated with the buffer should be removed from ail.
 * There is also special processing during recovery,
 * any replay of the inodes in the buffer needs to be
 * prevented as the buffer may have been reused.
 */
void
xfs_trans_stale_inode_buf(
	xfs_trans_t	*tp,
	xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_STALE_INODE;
	bip->bli_item.li_cb = xfs_buf_iodone;
}

/*
 * Mark the buffer as being one which contains newly allocated
 * inodes.  We need to make sure that even if this buffer is
 * relogged as an 'inode buf' we still recover all of the inode
 * images in the face of a crash.  This works in coordination with
 * xfs_buf_item_committed() to ensure that the buffer remains in the
 * AIL at its original location even after it has been relogged.
 */
/* ARGSUSED */
void
xfs_trans_inode_alloc_buf(
	xfs_trans_t	*tp,
	xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_INODE_ALLOC_BUF;
}


/*
 * Similar to xfs_trans_inode_buf(), this marks the buffer as a cluster of
 * dquots. However, unlike in inode buffer recovery, dquot buffers get
 * recovered in their entirety. (Hence, no XFS_BLI_DQUOT_ALLOC_BUF flag).
 * The only thing that makes dquot buffers different from regular
 * buffers is that we must not replay dquot bufs when recovering
 * if a _corresponding_ quotaoff has happened. We also have to distinguish
 * between usr dquot bufs and grp dquot bufs, because usr and grp quotas
 * can be turned off independently.
 */
/* ARGSUSED */
void
xfs_trans_dquot_buf(
	xfs_trans_t	*tp,
	xfs_buf_t	*bp,
	uint		type)
{
	xfs_buf_log_item_t	*bip;

	ASSERT(XFS_BUF_ISBUSY(bp));
	ASSERT(XFS_BUF_FSPRIVATE2(bp, xfs_trans_t *) == tp);
	ASSERT(XFS_BUF_FSPRIVATE(bp, void *) != NULL);
	ASSERT(type == XFS_BLF_UDQUOT_BUF ||
	       type == XFS_BLF_PDQUOT_BUF ||
	       type == XFS_BLF_GDQUOT_BUF);

	bip = XFS_BUF_FSPRIVATE(bp, xfs_buf_log_item_t *);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_format.blf_flags |= type;
}
