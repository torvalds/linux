// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_trace.h"

/*
 * Check to see if a buffer matching the given parameters is already
 * a part of the given transaction.
 */
STATIC struct xfs_buf *
xfs_trans_buf_item_match(
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps)
{
	struct xfs_log_item	*lip;
	struct xfs_buf_log_item	*blip;
	int			len = 0;
	int			i;

	for (i = 0; i < nmaps; i++)
		len += map[i].bm_len;

	list_for_each_entry(lip, &tp->t_items, li_trans) {
		blip = (struct xfs_buf_log_item *)lip;
		if (blip->bli_item.li_type == XFS_LI_BUF &&
		    blip->bli_buf->b_target == target &&
		    XFS_BUF_ADDR(blip->bli_buf) == map[0].bm_bn &&
		    blip->bli_buf->b_length == len) {
			ASSERT(blip->bli_buf->b_map_count == nmaps);
			return blip->bli_buf;
		}
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

	ASSERT(bp->b_transp == NULL);

	/*
	 * The xfs_buf_log_item pointer is stored in b_log_item.  If
	 * it doesn't have one yet, then allocate one and initialize it.
	 * The checks to see if one is there are in xfs_buf_item_init().
	 */
	xfs_buf_item_init(bp, tp->t_mountp);
	bip = bp->b_log_item;
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->__bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	if (reset_recur)
		bip->bli_recur = 0;

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	atomic_inc(&bip->bli_refcount);

	/*
	 * Attach the item to the transaction so we can find it in
	 * xfs_trans_get_buf() and friends.
	 */
	xfs_trans_add_item(tp, &bip->bli_item);
	bp->b_transp = tp;

}

void
xfs_trans_bjoin(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp)
{
	_xfs_trans_bjoin(tp, bp, 0);
	trace_xfs_trans_bjoin(bp->b_log_item);
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
struct xfs_buf *
xfs_trans_get_buf_map(
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags)
{
	xfs_buf_t		*bp;
	struct xfs_buf_log_item	*bip;

	if (!tp)
		return xfs_buf_get_map(target, map, nmaps, flags);

	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	bp = xfs_trans_buf_item_match(tp, target, map, nmaps);
	if (bp != NULL) {
		ASSERT(xfs_buf_islocked(bp));
		if (XFS_FORCED_SHUTDOWN(tp->t_mountp)) {
			xfs_buf_stale(bp);
			bp->b_flags |= XBF_DONE;
		}

		ASSERT(bp->b_transp == tp);
		bip = bp->b_log_item;
		ASSERT(bip != NULL);
		ASSERT(atomic_read(&bip->bli_refcount) > 0);
		bip->bli_recur++;
		trace_xfs_trans_get_buf_recur(bip);
		return bp;
	}

	bp = xfs_buf_get_map(target, map, nmaps, flags);
	if (bp == NULL) {
		return NULL;
	}

	ASSERT(!bp->b_error);

	_xfs_trans_bjoin(tp, bp, 1);
	trace_xfs_trans_get_buf(bp->b_log_item);
	return bp;
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
xfs_trans_getsb(
	xfs_trans_t		*tp,
	struct xfs_mount	*mp,
	int			flags)
{
	xfs_buf_t		*bp;
	struct xfs_buf_log_item	*bip;

	/*
	 * Default to just trying to lock the superblock buffer
	 * if tp is NULL.
	 */
	if (tp == NULL)
		return xfs_getsb(mp, flags);

	/*
	 * If the superblock buffer already has this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the buffer to the caller.
	 */
	bp = mp->m_sb_bp;
	if (bp->b_transp == tp) {
		bip = bp->b_log_item;
		ASSERT(bip != NULL);
		ASSERT(atomic_read(&bip->bli_refcount) > 0);
		bip->bli_recur++;
		trace_xfs_trans_getsb_recur(bip);
		return bp;
	}

	bp = xfs_getsb(mp, flags);
	if (bp == NULL)
		return NULL;

	_xfs_trans_bjoin(tp, bp, 1);
	trace_xfs_trans_getsb(bp->b_log_item);
	return bp;
}

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
xfs_trans_read_buf_map(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp = NULL;
	struct xfs_buf_log_item	*bip;
	int			error;

	*bpp = NULL;
	/*
	 * If we find the buffer in the cache with this transaction
	 * pointer in its b_fsprivate2 field, then we know we already
	 * have it locked.  If it is already read in we just increment
	 * the lock recursion count and return the buffer to the caller.
	 * If the buffer is not yet read in, then we read it in, increment
	 * the lock recursion count, and return it to the caller.
	 */
	if (tp)
		bp = xfs_trans_buf_item_match(tp, target, map, nmaps);
	if (bp) {
		ASSERT(xfs_buf_islocked(bp));
		ASSERT(bp->b_transp == tp);
		ASSERT(bp->b_log_item != NULL);
		ASSERT(!bp->b_error);
		ASSERT(bp->b_flags & XBF_DONE);

		/*
		 * We never locked this buf ourselves, so we shouldn't
		 * brelse it either. Just get out.
		 */
		if (XFS_FORCED_SHUTDOWN(mp)) {
			trace_xfs_trans_read_buf_shut(bp, _RET_IP_);
			return -EIO;
		}

		/*
		 * Check if the caller is trying to read a buffer that is
		 * already attached to the transaction yet has no buffer ops
		 * assigned.  Ops are usually attached when the buffer is
		 * attached to the transaction, or by the read caller if
		 * special circumstances.  That didn't happen, which is not
		 * how this is supposed to go.
		 *
		 * If the buffer passes verification we'll let this go, but if
		 * not we have to shut down.  Let the transaction cleanup code
		 * release this buffer when it kills the tranaction.
		 */
		ASSERT(bp->b_ops != NULL);
		error = xfs_buf_ensure_ops(bp, ops);
		if (error) {
			xfs_buf_ioerror_alert(bp, __func__);

			if (tp->t_flags & XFS_TRANS_DIRTY)
				xfs_force_shutdown(tp->t_mountp,
						SHUTDOWN_META_IO_ERROR);

			/* bad CRC means corrupted metadata */
			if (error == -EFSBADCRC)
				error = -EFSCORRUPTED;
			return error;
		}

		bip = bp->b_log_item;
		bip->bli_recur++;

		ASSERT(atomic_read(&bip->bli_refcount) > 0);
		trace_xfs_trans_read_buf_recur(bip);
		ASSERT(bp->b_ops != NULL || ops == NULL);
		*bpp = bp;
		return 0;
	}

	bp = xfs_buf_read_map(target, map, nmaps, flags, ops);
	if (!bp) {
		if (!(flags & XBF_TRYLOCK))
			return -ENOMEM;
		return tp ? 0 : -EAGAIN;
	}

	/*
	 * If we've had a read error, then the contents of the buffer are
	 * invalid and should not be used. To ensure that a followup read tries
	 * to pull the buffer from disk again, we clear the XBF_DONE flag and
	 * mark the buffer stale. This ensures that anyone who has a current
	 * reference to the buffer will interpret it's contents correctly and
	 * future cache lookups will also treat it as an empty, uninitialised
	 * buffer.
	 */
	if (bp->b_error) {
		error = bp->b_error;
		if (!XFS_FORCED_SHUTDOWN(mp))
			xfs_buf_ioerror_alert(bp, __func__);
		bp->b_flags &= ~XBF_DONE;
		xfs_buf_stale(bp);

		if (tp && (tp->t_flags & XFS_TRANS_DIRTY))
			xfs_force_shutdown(tp->t_mountp, SHUTDOWN_META_IO_ERROR);
		xfs_buf_relse(bp);

		/* bad CRC means corrupted metadata */
		if (error == -EFSBADCRC)
			error = -EFSCORRUPTED;
		return error;
	}

	if (XFS_FORCED_SHUTDOWN(mp)) {
		xfs_buf_relse(bp);
		trace_xfs_trans_read_buf_shut(bp, _RET_IP_);
		return -EIO;
	}

	if (tp) {
		_xfs_trans_bjoin(tp, bp, 1);
		trace_xfs_trans_read_buf(bp->b_log_item);
	}
	ASSERT(bp->b_ops != NULL || ops == NULL);
	*bpp = bp;
	return 0;

}

/* Has this buffer been dirtied by anyone? */
bool
xfs_trans_buf_is_dirty(
	struct xfs_buf		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	if (!bip)
		return false;
	ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
	return test_bit(XFS_LI_DIRTY, &bip->bli_item.li_flags);
}

/*
 * Release a buffer previously joined to the transaction. If the buffer is
 * modified within this transaction, decrement the recursion count but do not
 * release the buffer even if the count goes to 0. If the buffer is not modified
 * within the transaction, decrement the recursion count and release the buffer
 * if the recursion count goes to 0.
 *
 * If the buffer is to be released and it was not already dirty before this
 * transaction began, then also free the buf_log_item associated with it.
 *
 * If the transaction pointer is NULL, this is a normal xfs_buf_relse() call.
 */
void
xfs_trans_brelse(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);

	if (!tp) {
		xfs_buf_relse(bp);
		return;
	}

	trace_xfs_trans_brelse(bip);
	ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	/*
	 * If the release is for a recursive lookup, then decrement the count
	 * and return.
	 */
	if (bip->bli_recur > 0) {
		bip->bli_recur--;
		return;
	}

	/*
	 * If the buffer is invalidated or dirty in this transaction, we can't
	 * release it until we commit.
	 */
	if (test_bit(XFS_LI_DIRTY, &bip->bli_item.li_flags))
		return;
	if (bip->bli_flags & XFS_BLI_STALE)
		return;

	/*
	 * Unlink the log item from the transaction and clear the hold flag, if
	 * set. We wouldn't want the next user of the buffer to get confused.
	 */
	ASSERT(!(bip->bli_flags & XFS_BLI_LOGGED));
	xfs_trans_del_item(&bip->bli_item);
	bip->bli_flags &= ~XFS_BLI_HOLD;

	/* drop the reference to the bli */
	xfs_buf_item_put(bip);

	bp->b_transp = NULL;
	xfs_buf_relse(bp);
}

/*
 * Mark the buffer as not needing to be unlocked when the buf item's
 * iop_unlock() routine is called.  The buffer must already be locked
 * and associated with the given transaction.
 */
/* ARGSUSED */
void
xfs_trans_bhold(
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->__bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_HOLD;
	trace_xfs_trans_bhold(bip);
}

/*
 * Cancel the previous buffer hold request made on this buffer
 * for this transaction.
 */
void
xfs_trans_bhold_release(
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));
	ASSERT(!(bip->__bli_format.blf_flags & XFS_BLF_CANCEL));
	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT(bip->bli_flags & XFS_BLI_HOLD);

	bip->bli_flags &= ~XFS_BLI_HOLD;
	trace_xfs_trans_bhold_release(bip);
}

/*
 * Mark a buffer dirty in the transaction.
 */
void
xfs_trans_dirty_buf(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(bp->b_iodone == NULL ||
	       bp->b_iodone == xfs_buf_iodone_callbacks);

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
	bp->b_flags |= XBF_DONE;

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	bp->b_iodone = xfs_buf_iodone_callbacks;
	bip->bli_item.li_cb = xfs_buf_iodone;

	/*
	 * If we invalidated the buffer within this transaction, then
	 * cancel the invalidation now that we're dirtying the buffer
	 * again.  There are no races with the code in xfs_buf_item_unpin(),
	 * because we have a reference to the buffer this entire time.
	 */
	if (bip->bli_flags & XFS_BLI_STALE) {
		bip->bli_flags &= ~XFS_BLI_STALE;
		ASSERT(bp->b_flags & XBF_STALE);
		bp->b_flags &= ~XBF_STALE;
		bip->__bli_format.blf_flags &= ~XFS_BLF_CANCEL;
	}
	bip->bli_flags |= XFS_BLI_DIRTY | XFS_BLI_LOGGED;

	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &bip->bli_item.li_flags);
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
xfs_trans_log_buf(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	uint			first,
	uint			last)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(first <= last && last < BBTOB(bp->b_length));
	ASSERT(!(bip->bli_flags & XFS_BLI_ORDERED));

	xfs_trans_dirty_buf(tp, bp);

	trace_xfs_trans_log_buf(bip);
	xfs_buf_item_log(bip, first, last);
}


/*
 * Invalidate a buffer that is being used within a transaction.
 *
 * Typically this is because the blocks in the buffer are being freed, so we
 * need to prevent it from being written out when we're done.  Allowing it
 * to be written again might overwrite data in the free blocks if they are
 * reallocated to a file.
 *
 * We prevent the buffer from being written out by marking it stale.  We can't
 * get rid of the buf log item at this point because the buffer may still be
 * pinned by another transaction.  If that is the case, then we'll wait until
 * the buffer is committed to disk for the last time (we can tell by the ref
 * count) and free it in xfs_buf_item_unpin().  Until that happens we will
 * keep the buffer locked so that the buffer and buf log item are not reused.
 *
 * We also set the XFS_BLF_CANCEL flag in the buf log format structure and log
 * the buf item.  This will be used at recovery time to determine that copies
 * of the buffer in the log before this should not be replayed.
 *
 * We mark the item descriptor and the transaction dirty so that we'll hold
 * the buffer until after the commit.
 *
 * Since we're invalidating the buffer, we also clear the state about which
 * parts of the buffer have been logged.  We also clear the flag indicating
 * that this is an inode buffer since the data in the buffer will no longer
 * be valid.
 *
 * We set the stale bit in the buffer as well since we're getting rid of it.
 */
void
xfs_trans_binval(
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	int			i;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	trace_xfs_trans_binval(bip);

	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * If the buffer is already invalidated, then
		 * just return.
		 */
		ASSERT(bp->b_flags & XBF_STALE);
		ASSERT(!(bip->bli_flags & (XFS_BLI_LOGGED | XFS_BLI_DIRTY)));
		ASSERT(!(bip->__bli_format.blf_flags & XFS_BLF_INODE_BUF));
		ASSERT(!(bip->__bli_format.blf_flags & XFS_BLFT_MASK));
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);
		ASSERT(test_bit(XFS_LI_DIRTY, &bip->bli_item.li_flags));
		ASSERT(tp->t_flags & XFS_TRANS_DIRTY);
		return;
	}

	xfs_buf_stale(bp);

	bip->bli_flags |= XFS_BLI_STALE;
	bip->bli_flags &= ~(XFS_BLI_INODE_BUF | XFS_BLI_LOGGED | XFS_BLI_DIRTY);
	bip->__bli_format.blf_flags &= ~XFS_BLF_INODE_BUF;
	bip->__bli_format.blf_flags |= XFS_BLF_CANCEL;
	bip->__bli_format.blf_flags &= ~XFS_BLFT_MASK;
	for (i = 0; i < bip->bli_format_count; i++) {
		memset(bip->bli_formats[i].blf_data_map, 0,
		       (bip->bli_formats[i].blf_map_size * sizeof(uint)));
	}
	set_bit(XFS_LI_DIRTY, &bip->bli_item.li_flags);
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
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_INODE_BUF;
	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_DINO_BUF);
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
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_STALE_INODE;
	bip->bli_item.li_cb = xfs_buf_iodone;
	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_DINO_BUF);
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
	xfs_trans_t		*tp,
	xfs_buf_t		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	bip->bli_flags |= XFS_BLI_INODE_ALLOC_BUF;
	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_DINO_BUF);
}

/*
 * Mark the buffer as ordered for this transaction. This means that the contents
 * of the buffer are not recorded in the transaction but it is tracked in the
 * AIL as though it was. This allows us to record logical changes in
 * transactions rather than the physical changes we make to the buffer without
 * changing writeback ordering constraints of metadata buffers.
 */
bool
xfs_trans_ordered_buf(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	if (xfs_buf_item_dirty_format(bip))
		return false;

	bip->bli_flags |= XFS_BLI_ORDERED;
	trace_xfs_buf_item_ordered(bip);

	/*
	 * We don't log a dirty range of an ordered buffer but it still needs
	 * to be marked dirty and that it has been logged.
	 */
	xfs_trans_dirty_buf(tp, bp);
	return true;
}

/*
 * Set the type of the buffer for log recovery so that it can correctly identify
 * and hence attach the correct buffer ops to the buffer after replay.
 */
void
xfs_trans_buf_set_type(
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	enum xfs_blft		type)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	if (!tp)
		return;

	ASSERT(bp->b_transp == tp);
	ASSERT(bip != NULL);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	xfs_blft_to_flags(&bip->__bli_format, type);
}

void
xfs_trans_buf_copy_type(
	struct xfs_buf		*dst_bp,
	struct xfs_buf		*src_bp)
{
	struct xfs_buf_log_item	*sbip = src_bp->b_log_item;
	struct xfs_buf_log_item	*dbip = dst_bp->b_log_item;
	enum xfs_blft		type;

	type = xfs_blft_from_flags(&sbip->__bli_format);
	xfs_blft_to_flags(&dbip->__bli_format, type);
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
	xfs_trans_t		*tp,
	xfs_buf_t		*bp,
	uint			type)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	ASSERT(type == XFS_BLF_UDQUOT_BUF ||
	       type == XFS_BLF_PDQUOT_BUF ||
	       type == XFS_BLF_GDQUOT_BUF);

	bip->__bli_format.blf_flags |= type;

	switch (type) {
	case XFS_BLF_UDQUOT_BUF:
		type = XFS_BLFT_UDQUOT_BUF;
		break;
	case XFS_BLF_PDQUOT_BUF:
		type = XFS_BLFT_PDQUOT_BUF;
		break;
	case XFS_BLF_GDQUOT_BUF:
		type = XFS_BLFT_GDQUOT_BUF;
		break;
	default:
		type = XFS_BLFT_UNKNOWN_BUF;
		break;
	}

	xfs_trans_buf_set_type(tp, bp, type);
}
