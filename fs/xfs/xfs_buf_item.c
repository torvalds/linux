/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_inode.h"


kmem_zone_t	*xfs_buf_item_zone;

static inline struct xfs_buf_log_item *BUF_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_buf_log_item, bli_item);
}

STATIC void	xfs_buf_do_callbacks(struct xfs_buf *bp);

static inline int
xfs_buf_log_format_size(
	struct xfs_buf_log_format *blfp)
{
	return offsetof(struct xfs_buf_log_format, blf_data_map) +
			(blfp->blf_map_size * sizeof(blfp->blf_data_map[0]));
}

/*
 * This returns the number of log iovecs needed to log the
 * given buf log item.
 *
 * It calculates this as 1 iovec for the buf log format structure
 * and 1 for each stretch of non-contiguous chunks to be logged.
 * Contiguous chunks are logged in a single iovec.
 *
 * If the XFS_BLI_STALE flag has been set, then log nothing.
 */
STATIC void
xfs_buf_item_size_segment(
	struct xfs_buf_log_item	*bip,
	struct xfs_buf_log_format *blfp,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_buf		*bp = bip->bli_buf;
	int			next_bit;
	int			last_bit;

	last_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size, 0);
	if (last_bit == -1)
		return;

	/*
	 * initial count for a dirty buffer is 2 vectors - the format structure
	 * and the first dirty region.
	 */
	*nvecs += 2;
	*nbytes += xfs_buf_log_format_size(blfp) + XFS_BLF_CHUNK;

	while (last_bit != -1) {
		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		next_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size,
					last_bit + 1);
		/*
		 * If we run out of bits, leave the loop,
		 * else if we find a new set of bits bump the number of vecs,
		 * else keep scanning the current set of bits.
		 */
		if (next_bit == -1) {
			break;
		} else if (next_bit != last_bit + 1) {
			last_bit = next_bit;
			(*nvecs)++;
		} else if (xfs_buf_offset(bp, next_bit * XFS_BLF_CHUNK) !=
			   (xfs_buf_offset(bp, last_bit * XFS_BLF_CHUNK) +
			    XFS_BLF_CHUNK)) {
			last_bit = next_bit;
			(*nvecs)++;
		} else {
			last_bit++;
		}
		*nbytes += XFS_BLF_CHUNK;
	}
}

/*
 * This returns the number of log iovecs needed to log the given buf log item.
 *
 * It calculates this as 1 iovec for the buf log format structure and 1 for each
 * stretch of non-contiguous chunks to be logged.  Contiguous chunks are logged
 * in a single iovec.
 *
 * Discontiguous buffers need a format structure per region that that is being
 * logged. This makes the changes in the buffer appear to log recovery as though
 * they came from separate buffers, just like would occur if multiple buffers
 * were used instead of a single discontiguous buffer. This enables
 * discontiguous buffers to be in-memory constructs, completely transparent to
 * what ends up on disk.
 *
 * If the XFS_BLI_STALE flag has been set, then log nothing but the buf log
 * format structures.
 */
STATIC void
xfs_buf_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	int			i;

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * The buffer is stale, so all we need to log
		 * is the buf log format structure with the
		 * cancel flag in it.
		 */
		trace_xfs_buf_item_size_stale(bip);
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);
		*nvecs += bip->bli_format_count;
		for (i = 0; i < bip->bli_format_count; i++) {
			*nbytes += xfs_buf_log_format_size(&bip->bli_formats[i]);
		}
		return;
	}

	ASSERT(bip->bli_flags & XFS_BLI_LOGGED);

	if (bip->bli_flags & XFS_BLI_ORDERED) {
		/*
		 * The buffer has been logged just to order it.
		 * It is not being included in the transaction
		 * commit, so no vectors are used at all.
		 */
		trace_xfs_buf_item_size_ordered(bip);
		*nvecs = XFS_LOG_VEC_ORDERED;
		return;
	}

	/*
	 * the vector count is based on the number of buffer vectors we have
	 * dirty bits in. This will only be greater than one when we have a
	 * compound buffer with more than one segment dirty. Hence for compound
	 * buffers we need to track which segment the dirty bits correspond to,
	 * and when we move from one segment to the next increment the vector
	 * count for the extra buf log format structure that will need to be
	 * written.
	 */
	for (i = 0; i < bip->bli_format_count; i++) {
		xfs_buf_item_size_segment(bip, &bip->bli_formats[i],
					  nvecs, nbytes);
	}
	trace_xfs_buf_item_size(bip);
}

static inline void
xfs_buf_item_copy_iovec(
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp,
	struct xfs_buf		*bp,
	uint			offset,
	int			first_bit,
	uint			nbits)
{
	offset += first_bit * XFS_BLF_CHUNK;
	xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_BCHUNK,
			xfs_buf_offset(bp, offset),
			nbits * XFS_BLF_CHUNK);
}

static inline bool
xfs_buf_item_straddle(
	struct xfs_buf		*bp,
	uint			offset,
	int			next_bit,
	int			last_bit)
{
	return xfs_buf_offset(bp, offset + (next_bit << XFS_BLF_SHIFT)) !=
		(xfs_buf_offset(bp, offset + (last_bit << XFS_BLF_SHIFT)) +
		 XFS_BLF_CHUNK);
}

static void
xfs_buf_item_format_segment(
	struct xfs_buf_log_item	*bip,
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp,
	uint			offset,
	struct xfs_buf_log_format *blfp)
{
	struct xfs_buf	*bp = bip->bli_buf;
	uint		base_size;
	int		first_bit;
	int		last_bit;
	int		next_bit;
	uint		nbits;

	/* copy the flags across from the base format item */
	blfp->blf_flags = bip->__bli_format.blf_flags;

	/*
	 * Base size is the actual size of the ondisk structure - it reflects
	 * the actual size of the dirty bitmap rather than the size of the in
	 * memory structure.
	 */
	base_size = xfs_buf_log_format_size(blfp);

	first_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size, 0);
	if (!(bip->bli_flags & XFS_BLI_STALE) && first_bit == -1) {
		/*
		 * If the map is not be dirty in the transaction, mark
		 * the size as zero and do not advance the vector pointer.
		 */
		return;
	}

	blfp = xlog_copy_iovec(lv, vecp, XLOG_REG_TYPE_BFORMAT, blfp, base_size);
	blfp->blf_size = 1;

	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * The buffer is stale, so all we need to log
		 * is the buf log format structure with the
		 * cancel flag in it.
		 */
		trace_xfs_buf_item_format_stale(bip);
		ASSERT(blfp->blf_flags & XFS_BLF_CANCEL);
		return;
	}


	/*
	 * Fill in an iovec for each set of contiguous chunks.
	 */
	last_bit = first_bit;
	nbits = 1;
	for (;;) {
		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		next_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size,
					(uint)last_bit + 1);
		/*
		 * If we run out of bits fill in the last iovec and get out of
		 * the loop.  Else if we start a new set of bits then fill in
		 * the iovec for the series we were looking at and start
		 * counting the bits in the new one.  Else we're still in the
		 * same set of bits so just keep counting and scanning.
		 */
		if (next_bit == -1) {
			xfs_buf_item_copy_iovec(lv, vecp, bp, offset,
						first_bit, nbits);
			blfp->blf_size++;
			break;
		} else if (next_bit != last_bit + 1 ||
		           xfs_buf_item_straddle(bp, offset, next_bit, last_bit)) {
			xfs_buf_item_copy_iovec(lv, vecp, bp, offset,
						first_bit, nbits);
			blfp->blf_size++;
			first_bit = next_bit;
			last_bit = next_bit;
			nbits = 1;
		} else {
			last_bit++;
			nbits++;
		}
	}
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given log buf item.  It fills the first entry with a buf log
 * format structure, and the rest point to contiguous chunks
 * within the buffer.
 */
STATIC void
xfs_buf_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	struct xfs_log_iovec	*vecp = NULL;
	uint			offset = 0;
	int			i;

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT((bip->bli_flags & XFS_BLI_LOGGED) ||
	       (bip->bli_flags & XFS_BLI_STALE));
	ASSERT((bip->bli_flags & XFS_BLI_STALE) ||
	       (xfs_blft_from_flags(&bip->__bli_format) > XFS_BLFT_UNKNOWN_BUF
	        && xfs_blft_from_flags(&bip->__bli_format) < XFS_BLFT_MAX_BUF));


	/*
	 * If it is an inode buffer, transfer the in-memory state to the
	 * format flags and clear the in-memory state.
	 *
	 * For buffer based inode allocation, we do not transfer
	 * this state if the inode buffer allocation has not yet been committed
	 * to the log as setting the XFS_BLI_INODE_BUF flag will prevent
	 * correct replay of the inode allocation.
	 *
	 * For icreate item based inode allocation, the buffers aren't written
	 * to the journal during allocation, and hence we should always tag the
	 * buffer as an inode buffer so that the correct unlinked list replay
	 * occurs during recovery.
	 */
	if (bip->bli_flags & XFS_BLI_INODE_BUF) {
		if (xfs_sb_version_hascrc(&lip->li_mountp->m_sb) ||
		    !((bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF) &&
		      xfs_log_item_in_current_chkpt(lip)))
			bip->__bli_format.blf_flags |= XFS_BLF_INODE_BUF;
		bip->bli_flags &= ~XFS_BLI_INODE_BUF;
	}

	if ((bip->bli_flags & (XFS_BLI_ORDERED|XFS_BLI_STALE)) ==
							XFS_BLI_ORDERED) {
		/*
		 * The buffer has been logged just to order it.  It is not being
		 * included in the transaction commit, so don't format it.
		 */
		trace_xfs_buf_item_format_ordered(bip);
		return;
	}

	for (i = 0; i < bip->bli_format_count; i++) {
		xfs_buf_item_format_segment(bip, lv, &vecp, offset,
					    &bip->bli_formats[i]);
		offset += BBTOB(bp->b_maps[i].bm_len);
	}

	/*
	 * Check to make sure everything is consistent.
	 */
	trace_xfs_buf_item_format(bip);
}

/*
 * This is called to pin the buffer associated with the buf log item in memory
 * so it cannot be written out.
 *
 * We also always take a reference to the buffer log item here so that the bli
 * is held while the item is pinned in memory. This means that we can
 * unconditionally drop the reference count a transaction holds when the
 * transaction is completed.
 */
STATIC void
xfs_buf_item_pin(
	struct xfs_log_item	*lip)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	ASSERT((bip->bli_flags & XFS_BLI_LOGGED) ||
	       (bip->bli_flags & XFS_BLI_ORDERED) ||
	       (bip->bli_flags & XFS_BLI_STALE));

	trace_xfs_buf_item_pin(bip);

	atomic_inc(&bip->bli_refcount);
	atomic_inc(&bip->bli_buf->b_pin_count);
}

/*
 * This is called to unpin the buffer associated with the buf log
 * item which was previously pinned with a call to xfs_buf_item_pin().
 *
 * Also drop the reference to the buf item for the current transaction.
 * If the XFS_BLI_STALE flag is set and we are the last reference,
 * then free up the buf log item and unlock the buffer.
 *
 * If the remove flag is set we are called from uncommit in the
 * forced-shutdown path.  If that is true and the reference count on
 * the log item is going to drop to zero we need to free the item's
 * descriptor in the transaction.
 */
STATIC void
xfs_buf_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	xfs_buf_t	*bp = bip->bli_buf;
	struct xfs_ail	*ailp = lip->li_ailp;
	int		stale = bip->bli_flags & XFS_BLI_STALE;
	int		freed;

	ASSERT(bp->b_fspriv == bip);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	trace_xfs_buf_item_unpin(bip);

	freed = atomic_dec_and_test(&bip->bli_refcount);

	if (atomic_dec_and_test(&bp->b_pin_count))
		wake_up_all(&bp->b_waiters);

	if (freed && stale) {
		ASSERT(bip->bli_flags & XFS_BLI_STALE);
		ASSERT(xfs_buf_islocked(bp));
		ASSERT(bp->b_flags & XBF_STALE);
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);

		trace_xfs_buf_item_unpin_stale(bip);

		if (remove) {
			/*
			 * If we are in a transaction context, we have to
			 * remove the log item from the transaction as we are
			 * about to release our reference to the buffer.  If we
			 * don't, the unlock that occurs later in
			 * xfs_trans_uncommit() will try to reference the
			 * buffer which we no longer have a hold on.
			 */
			if (lip->li_desc)
				xfs_trans_del_item(lip);

			/*
			 * Since the transaction no longer refers to the buffer,
			 * the buffer should no longer refer to the transaction.
			 */
			bp->b_transp = NULL;
		}

		/*
		 * If we get called here because of an IO error, we may
		 * or may not have the item on the AIL. xfs_trans_ail_delete()
		 * will take care of that situation.
		 * xfs_trans_ail_delete() drops the AIL lock.
		 */
		if (bip->bli_flags & XFS_BLI_STALE_INODE) {
			xfs_buf_do_callbacks(bp);
			bp->b_fspriv = NULL;
			bp->b_iodone = NULL;
		} else {
			spin_lock(&ailp->xa_lock);
			xfs_trans_ail_delete(ailp, lip, SHUTDOWN_LOG_IO_ERROR);
			xfs_buf_item_relse(bp);
			ASSERT(bp->b_fspriv == NULL);
		}
		xfs_buf_relse(bp);
	} else if (freed && remove) {
		/*
		 * There are currently two references to the buffer - the active
		 * LRU reference and the buf log item. What we are about to do
		 * here - simulate a failed IO completion - requires 3
		 * references.
		 *
		 * The LRU reference is removed by the xfs_buf_stale() call. The
		 * buf item reference is removed by the xfs_buf_iodone()
		 * callback that is run by xfs_buf_do_callbacks() during ioend
		 * processing (via the bp->b_iodone callback), and then finally
		 * the ioend processing will drop the IO reference if the buffer
		 * is marked XBF_ASYNC.
		 *
		 * Hence we need to take an additional reference here so that IO
		 * completion processing doesn't free the buffer prematurely.
		 */
		xfs_buf_lock(bp);
		xfs_buf_hold(bp);
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_ioerror(bp, -EIO);
		bp->b_flags &= ~XBF_DONE;
		xfs_buf_stale(bp);
		xfs_buf_ioend(bp);
	}
}

/*
 * Buffer IO error rate limiting. Limit it to no more than 10 messages per 30
 * seconds so as to not spam logs too much on repeated detection of the same
 * buffer being bad..
 */

static DEFINE_RATELIMIT_STATE(xfs_buf_write_fail_rl_state, 30 * HZ, 10);

STATIC uint
xfs_buf_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	uint			rval = XFS_ITEM_SUCCESS;

	if (xfs_buf_ispinned(bp))
		return XFS_ITEM_PINNED;
	if (!xfs_buf_trylock(bp)) {
		/*
		 * If we have just raced with a buffer being pinned and it has
		 * been marked stale, we could end up stalling until someone else
		 * issues a log force to unpin the stale buffer. Check for the
		 * race condition here so xfsaild recognizes the buffer is pinned
		 * and queues a log force to move it along.
		 */
		if (xfs_buf_ispinned(bp))
			return XFS_ITEM_PINNED;
		return XFS_ITEM_LOCKED;
	}

	ASSERT(!(bip->bli_flags & XFS_BLI_STALE));

	trace_xfs_buf_item_push(bip);

	/* has a previous flush failed due to IO errors? */
	if ((bp->b_flags & XBF_WRITE_FAIL) &&
	    ___ratelimit(&xfs_buf_write_fail_rl_state, "XFS: Failing async write")) {
		xfs_warn(bp->b_target->bt_mount,
"Failing async write on buffer block 0x%llx. Retrying async write.",
			 (long long)bp->b_bn);
	}

	if (!xfs_buf_delwri_queue(bp, buffer_list))
		rval = XFS_ITEM_FLUSHING;
	xfs_buf_unlock(bp);
	return rval;
}

/*
 * Release the buffer associated with the buf log item.  If there is no dirty
 * logged data associated with the buffer recorded in the buf log item, then
 * free the buf log item and remove the reference to it in the buffer.
 *
 * This call ignores the recursion count.  It is only called when the buffer
 * should REALLY be unlocked, regardless of the recursion count.
 *
 * We unconditionally drop the transaction's reference to the log item. If the
 * item was logged, then another reference was taken when it was pinned, so we
 * can safely drop the transaction reference now.  This also allows us to avoid
 * potential races with the unpin code freeing the bli by not referencing the
 * bli after we've dropped the reference count.
 *
 * If the XFS_BLI_HOLD flag is set in the buf log item, then free the log item
 * if necessary but do not unlock the buffer.  This is for support of
 * xfs_trans_bhold(). Make sure the XFS_BLI_HOLD field is cleared if we don't
 * free the item.
 */
STATIC void
xfs_buf_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	bool			clean;
	bool			aborted;
	int			flags;

	/* Clear the buffer's association with this transaction. */
	bp->b_transp = NULL;

	/*
	 * If this is a transaction abort, don't return early.  Instead, allow
	 * the brelse to happen.  Normally it would be done for stale
	 * (cancelled) buffers at unpin time, but we'll never go through the
	 * pin/unpin cycle if we abort inside commit.
	 */
	aborted = (lip->li_flags & XFS_LI_ABORTED) ? true : false;
	/*
	 * Before possibly freeing the buf item, copy the per-transaction state
	 * so we can reference it safely later after clearing it from the
	 * buffer log item.
	 */
	flags = bip->bli_flags;
	bip->bli_flags &= ~(XFS_BLI_LOGGED | XFS_BLI_HOLD | XFS_BLI_ORDERED);

	/*
	 * If the buf item is marked stale, then don't do anything.  We'll
	 * unlock the buffer and free the buf item when the buffer is unpinned
	 * for the last time.
	 */
	if (flags & XFS_BLI_STALE) {
		trace_xfs_buf_item_unlock_stale(bip);
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);
		if (!aborted) {
			atomic_dec(&bip->bli_refcount);
			return;
		}
	}

	trace_xfs_buf_item_unlock(bip);

	/*
	 * If the buf item isn't tracking any data, free it, otherwise drop the
	 * reference we hold to it. If we are aborting the transaction, this may
	 * be the only reference to the buf item, so we free it anyway
	 * regardless of whether it is dirty or not. A dirty abort implies a
	 * shutdown, anyway.
	 *
	 * Ordered buffers are dirty but may have no recorded changes, so ensure
	 * we only release clean items here.
	 */
	clean = (flags & XFS_BLI_DIRTY) ? false : true;
	if (clean) {
		int i;
		for (i = 0; i < bip->bli_format_count; i++) {
			if (!xfs_bitmap_empty(bip->bli_formats[i].blf_data_map,
				     bip->bli_formats[i].blf_map_size)) {
				clean = false;
				break;
			}
		}
	}

	/*
	 * Clean buffers, by definition, cannot be in the AIL. However, aborted
	 * buffers may be in the AIL regardless of dirty state. An aborted
	 * transaction that invalidates a buffer already in the AIL may have
	 * marked it stale and cleared the dirty state, for example.
	 *
	 * Therefore if we are aborting a buffer and we've just taken the last
	 * reference away, we have to check if it is in the AIL before freeing
	 * it. We need to free it in this case, because an aborted transaction
	 * has already shut the filesystem down and this is the last chance we
	 * will have to do so.
	 */
	if (atomic_dec_and_test(&bip->bli_refcount)) {
		if (aborted) {
			ASSERT(XFS_FORCED_SHUTDOWN(lip->li_mountp));
			xfs_trans_ail_remove(lip, SHUTDOWN_LOG_IO_ERROR);
			xfs_buf_item_relse(bp);
		} else if (clean)
			xfs_buf_item_relse(bp);
	}

	if (!(flags & XFS_BLI_HOLD))
		xfs_buf_relse(bp);
}

/*
 * This is called to find out where the oldest active copy of the
 * buf log item in the on disk log resides now that the last log
 * write of it completed at the given lsn.
 * We always re-log all the dirty data in a buffer, so usually the
 * latest copy in the on disk log is the only one that matters.  For
 * those cases we simply return the given lsn.
 *
 * The one exception to this is for buffers full of newly allocated
 * inodes.  These buffers are only relogged with the XFS_BLI_INODE_BUF
 * flag set, indicating that only the di_next_unlinked fields from the
 * inodes in the buffers will be replayed during recovery.  If the
 * original newly allocated inode images have not yet been flushed
 * when the buffer is so relogged, then we need to make sure that we
 * keep the old images in the 'active' portion of the log.  We do this
 * by returning the original lsn of that transaction here rather than
 * the current one.
 */
STATIC xfs_lsn_t
xfs_buf_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);

	trace_xfs_buf_item_committed(bip);

	if ((bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF) && lip->li_lsn != 0)
		return lip->li_lsn;
	return lsn;
}

STATIC void
xfs_buf_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		commit_lsn)
{
}

/*
 * This is the ops vector shared by all buf log items.
 */
static const struct xfs_item_ops xfs_buf_item_ops = {
	.iop_size	= xfs_buf_item_size,
	.iop_format	= xfs_buf_item_format,
	.iop_pin	= xfs_buf_item_pin,
	.iop_unpin	= xfs_buf_item_unpin,
	.iop_unlock	= xfs_buf_item_unlock,
	.iop_committed	= xfs_buf_item_committed,
	.iop_push	= xfs_buf_item_push,
	.iop_committing = xfs_buf_item_committing
};

STATIC int
xfs_buf_item_get_format(
	struct xfs_buf_log_item	*bip,
	int			count)
{
	ASSERT(bip->bli_formats == NULL);
	bip->bli_format_count = count;

	if (count == 1) {
		bip->bli_formats = &bip->__bli_format;
		return 0;
	}

	bip->bli_formats = kmem_zalloc(count * sizeof(struct xfs_buf_log_format),
				KM_SLEEP);
	if (!bip->bli_formats)
		return -ENOMEM;
	return 0;
}

STATIC void
xfs_buf_item_free_format(
	struct xfs_buf_log_item	*bip)
{
	if (bip->bli_formats != &bip->__bli_format) {
		kmem_free(bip->bli_formats);
		bip->bli_formats = NULL;
	}
}

/*
 * Allocate a new buf log item to go with the given buffer.
 * Set the buffer's b_fsprivate field to point to the new
 * buf log item.  If there are other item's attached to the
 * buffer (see xfs_buf_attach_iodone() below), then put the
 * buf log item at the front.
 */
int
xfs_buf_item_init(
	struct xfs_buf	*bp,
	struct xfs_mount *mp)
{
	struct xfs_log_item	*lip = bp->b_fspriv;
	struct xfs_buf_log_item	*bip;
	int			chunks;
	int			map_size;
	int			error;
	int			i;

	/*
	 * Check to see if there is already a buf log item for
	 * this buffer.  If there is, it is guaranteed to be
	 * the first.  If we do already have one, there is
	 * nothing to do here so return.
	 */
	ASSERT(bp->b_target->bt_mount == mp);
	if (lip != NULL && lip->li_type == XFS_LI_BUF)
		return 0;

	bip = kmem_zone_zalloc(xfs_buf_item_zone, KM_SLEEP);
	xfs_log_item_init(mp, &bip->bli_item, XFS_LI_BUF, &xfs_buf_item_ops);
	bip->bli_buf = bp;

	/*
	 * chunks is the number of XFS_BLF_CHUNK size pieces the buffer
	 * can be divided into. Make sure not to truncate any pieces.
	 * map_size is the size of the bitmap needed to describe the
	 * chunks of the buffer.
	 *
	 * Discontiguous buffer support follows the layout of the underlying
	 * buffer. This makes the implementation as simple as possible.
	 */
	error = xfs_buf_item_get_format(bip, bp->b_map_count);
	ASSERT(error == 0);
	if (error) {	/* to stop gcc throwing set-but-unused warnings */
		kmem_zone_free(xfs_buf_item_zone, bip);
		return error;
	}


	for (i = 0; i < bip->bli_format_count; i++) {
		chunks = DIV_ROUND_UP(BBTOB(bp->b_maps[i].bm_len),
				      XFS_BLF_CHUNK);
		map_size = DIV_ROUND_UP(chunks, NBWORD);

		bip->bli_formats[i].blf_type = XFS_LI_BUF;
		bip->bli_formats[i].blf_blkno = bp->b_maps[i].bm_bn;
		bip->bli_formats[i].blf_len = bp->b_maps[i].bm_len;
		bip->bli_formats[i].blf_map_size = map_size;
	}

	/*
	 * Put the buf item into the list of items attached to the
	 * buffer at the front.
	 */
	if (bp->b_fspriv)
		bip->bli_item.li_bio_list = bp->b_fspriv;
	bp->b_fspriv = bip;
	xfs_buf_hold(bp);
	return 0;
}


/*
 * Mark bytes first through last inclusive as dirty in the buf
 * item's bitmap.
 */
static void
xfs_buf_item_log_segment(
	uint			first,
	uint			last,
	uint			*map)
{
	uint		first_bit;
	uint		last_bit;
	uint		bits_to_set;
	uint		bits_set;
	uint		word_num;
	uint		*wordp;
	uint		bit;
	uint		end_bit;
	uint		mask;

	/*
	 * Convert byte offsets to bit numbers.
	 */
	first_bit = first >> XFS_BLF_SHIFT;
	last_bit = last >> XFS_BLF_SHIFT;

	/*
	 * Calculate the total number of bits to be set.
	 */
	bits_to_set = last_bit - first_bit + 1;

	/*
	 * Get a pointer to the first word in the bitmap
	 * to set a bit in.
	 */
	word_num = first_bit >> BIT_TO_WORD_SHIFT;
	wordp = &map[word_num];

	/*
	 * Calculate the starting bit in the first word.
	 */
	bit = first_bit & (uint)(NBWORD - 1);

	/*
	 * First set any bits in the first word of our range.
	 * If it starts at bit 0 of the word, it will be
	 * set below rather than here.  That is what the variable
	 * bit tells us. The variable bits_set tracks the number
	 * of bits that have been set so far.  End_bit is the number
	 * of the last bit to be set in this word plus one.
	 */
	if (bit) {
		end_bit = MIN(bit + bits_to_set, (uint)NBWORD);
		mask = ((1U << (end_bit - bit)) - 1) << bit;
		*wordp |= mask;
		wordp++;
		bits_set = end_bit - bit;
	} else {
		bits_set = 0;
	}

	/*
	 * Now set bits a whole word at a time that are between
	 * first_bit and last_bit.
	 */
	while ((bits_to_set - bits_set) >= NBWORD) {
		*wordp |= 0xffffffff;
		bits_set += NBWORD;
		wordp++;
	}

	/*
	 * Finally, set any bits left to be set in one last partial word.
	 */
	end_bit = bits_to_set - bits_set;
	if (end_bit) {
		mask = (1U << end_bit) - 1;
		*wordp |= mask;
	}
}

/*
 * Mark bytes first through last inclusive as dirty in the buf
 * item's bitmap.
 */
void
xfs_buf_item_log(
	xfs_buf_log_item_t	*bip,
	uint			first,
	uint			last)
{
	int			i;
	uint			start;
	uint			end;
	struct xfs_buf		*bp = bip->bli_buf;

	/*
	 * walk each buffer segment and mark them dirty appropriately.
	 */
	start = 0;
	for (i = 0; i < bip->bli_format_count; i++) {
		if (start > last)
			break;
		end = start + BBTOB(bp->b_maps[i].bm_len) - 1;

		/* skip to the map that includes the first byte to log */
		if (first > end) {
			start += BBTOB(bp->b_maps[i].bm_len);
			continue;
		}

		/*
		 * Trim the range to this segment and mark it in the bitmap.
		 * Note that we must convert buffer offsets to segment relative
		 * offsets (e.g., the first byte of each segment is byte 0 of
		 * that segment).
		 */
		if (first < start)
			first = start;
		if (end > last)
			end = last;
		xfs_buf_item_log_segment(first - start, end - start,
					 &bip->bli_formats[i].blf_data_map[0]);

		start += BBTOB(bp->b_maps[i].bm_len);
	}
}


/*
 * Return 1 if the buffer has been logged or ordered in a transaction (at any
 * point, not just the current transaction) and 0 if not.
 */
uint
xfs_buf_item_dirty(
	xfs_buf_log_item_t	*bip)
{
	return (bip->bli_flags & XFS_BLI_DIRTY);
}

STATIC void
xfs_buf_item_free(
	xfs_buf_log_item_t	*bip)
{
	xfs_buf_item_free_format(bip);
	kmem_free(bip->bli_item.li_lv_shadow);
	kmem_zone_free(xfs_buf_item_zone, bip);
}

/*
 * This is called when the buf log item is no longer needed.  It should
 * free the buf log item associated with the given buffer and clear
 * the buffer's pointer to the buf log item.  If there are no more
 * items in the list, clear the b_iodone field of the buffer (see
 * xfs_buf_attach_iodone() below).
 */
void
xfs_buf_item_relse(
	xfs_buf_t	*bp)
{
	xfs_buf_log_item_t	*bip = bp->b_fspriv;

	trace_xfs_buf_item_relse(bp, _RET_IP_);
	ASSERT(!(bip->bli_item.li_flags & XFS_LI_IN_AIL));

	bp->b_fspriv = bip->bli_item.li_bio_list;
	if (bp->b_fspriv == NULL)
		bp->b_iodone = NULL;

	xfs_buf_rele(bp);
	xfs_buf_item_free(bip);
}


/*
 * Add the given log item with its callback to the list of callbacks
 * to be called when the buffer's I/O completes.  If it is not set
 * already, set the buffer's b_iodone() routine to be
 * xfs_buf_iodone_callbacks() and link the log item into the list of
 * items rooted at b_fsprivate.  Items are always added as the second
 * entry in the list if there is a first, because the buf item code
 * assumes that the buf log item is first.
 */
void
xfs_buf_attach_iodone(
	xfs_buf_t	*bp,
	void		(*cb)(xfs_buf_t *, xfs_log_item_t *),
	xfs_log_item_t	*lip)
{
	xfs_log_item_t	*head_lip;

	ASSERT(xfs_buf_islocked(bp));

	lip->li_cb = cb;
	head_lip = bp->b_fspriv;
	if (head_lip) {
		lip->li_bio_list = head_lip->li_bio_list;
		head_lip->li_bio_list = lip;
	} else {
		bp->b_fspriv = lip;
	}

	ASSERT(bp->b_iodone == NULL ||
	       bp->b_iodone == xfs_buf_iodone_callbacks);
	bp->b_iodone = xfs_buf_iodone_callbacks;
}

/*
 * We can have many callbacks on a buffer. Running the callbacks individually
 * can cause a lot of contention on the AIL lock, so we allow for a single
 * callback to be able to scan the remaining lip->li_bio_list for other items
 * of the same type and callback to be processed in the first call.
 *
 * As a result, the loop walking the callback list below will also modify the
 * list. it removes the first item from the list and then runs the callback.
 * The loop then restarts from the new head of the list. This allows the
 * callback to scan and modify the list attached to the buffer and we don't
 * have to care about maintaining a next item pointer.
 */
STATIC void
xfs_buf_do_callbacks(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip;

	while ((lip = bp->b_fspriv) != NULL) {
		bp->b_fspriv = lip->li_bio_list;
		ASSERT(lip->li_cb != NULL);
		/*
		 * Clear the next pointer so we don't have any
		 * confusion if the item is added to another buf.
		 * Don't touch the log item after calling its
		 * callback, because it could have freed itself.
		 */
		lip->li_bio_list = NULL;
		lip->li_cb(bp, lip);
	}
}

/*
 * Invoke the error state callback for each log item affected by the failed I/O.
 *
 * If a metadata buffer write fails with a non-permanent error, the buffer is
 * eventually resubmitted and so the completion callbacks are not run. The error
 * state may need to be propagated to the log items attached to the buffer,
 * however, so the next AIL push of the item knows hot to handle it correctly.
 */
STATIC void
xfs_buf_do_callbacks_fail(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*next;
	struct xfs_log_item	*lip = bp->b_fspriv;
	struct xfs_ail		*ailp = lip->li_ailp;

	spin_lock(&ailp->xa_lock);
	for (; lip; lip = next) {
		next = lip->li_bio_list;
		if (lip->li_ops->iop_error)
			lip->li_ops->iop_error(lip, bp);
	}
	spin_unlock(&ailp->xa_lock);
}

static bool
xfs_buf_iodone_callback_error(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip = bp->b_fspriv;
	struct xfs_mount	*mp = lip->li_mountp;
	static ulong		lasttime;
	static xfs_buftarg_t	*lasttarg;
	struct xfs_error_cfg	*cfg;

	/*
	 * If we've already decided to shutdown the filesystem because of
	 * I/O errors, there's no point in giving this a retry.
	 */
	if (XFS_FORCED_SHUTDOWN(mp))
		goto out_stale;

	if (bp->b_target != lasttarg ||
	    time_after(jiffies, (lasttime + 5*HZ))) {
		lasttime = jiffies;
		xfs_buf_ioerror_alert(bp, __func__);
	}
	lasttarg = bp->b_target;

	/* synchronous writes will have callers process the error */
	if (!(bp->b_flags & XBF_ASYNC))
		goto out_stale;

	trace_xfs_buf_item_iodone_async(bp, _RET_IP_);
	ASSERT(bp->b_iodone != NULL);

	cfg = xfs_error_get_cfg(mp, XFS_ERR_METADATA, bp->b_error);

	/*
	 * If the write was asynchronous then no one will be looking for the
	 * error.  If this is the first failure of this type, clear the error
	 * state and write the buffer out again. This means we always retry an
	 * async write failure at least once, but we also need to set the buffer
	 * up to behave correctly now for repeated failures.
	 */
	if (!(bp->b_flags & (XBF_STALE | XBF_WRITE_FAIL)) ||
	     bp->b_last_error != bp->b_error) {
		bp->b_flags |= (XBF_WRITE | XBF_DONE | XBF_WRITE_FAIL);
		bp->b_last_error = bp->b_error;
		if (cfg->retry_timeout != XFS_ERR_RETRY_FOREVER &&
		    !bp->b_first_retry_time)
			bp->b_first_retry_time = jiffies;

		xfs_buf_ioerror(bp, 0);
		xfs_buf_submit(bp);
		return true;
	}

	/*
	 * Repeated failure on an async write. Take action according to the
	 * error configuration we have been set up to use.
	 */

	if (cfg->max_retries != XFS_ERR_RETRY_FOREVER &&
	    ++bp->b_retries > cfg->max_retries)
			goto permanent_error;
	if (cfg->retry_timeout != XFS_ERR_RETRY_FOREVER &&
	    time_after(jiffies, cfg->retry_timeout + bp->b_first_retry_time))
			goto permanent_error;

	/* At unmount we may treat errors differently */
	if ((mp->m_flags & XFS_MOUNT_UNMOUNTING) && mp->m_fail_unmount)
		goto permanent_error;

	/*
	 * Still a transient error, run IO completion failure callbacks and let
	 * the higher layers retry the buffer.
	 */
	xfs_buf_do_callbacks_fail(bp);
	xfs_buf_ioerror(bp, 0);
	xfs_buf_relse(bp);
	return true;

	/*
	 * Permanent error - we need to trigger a shutdown if we haven't already
	 * to indicate that inconsistency will result from this action.
	 */
permanent_error:
	xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
out_stale:
	xfs_buf_stale(bp);
	bp->b_flags |= XBF_DONE;
	trace_xfs_buf_error_relse(bp, _RET_IP_);
	return false;
}

/*
 * This is the iodone() function for buffers which have had callbacks attached
 * to them by xfs_buf_attach_iodone(). We need to iterate the items on the
 * callback list, mark the buffer as having no more callbacks and then push the
 * buffer through IO completion processing.
 */
void
xfs_buf_iodone_callbacks(
	struct xfs_buf		*bp)
{
	/*
	 * If there is an error, process it. Some errors require us
	 * to run callbacks after failure processing is done so we
	 * detect that and take appropriate action.
	 */
	if (bp->b_error && xfs_buf_iodone_callback_error(bp))
		return;

	/*
	 * Successful IO or permanent error. Either way, we can clear the
	 * retry state here in preparation for the next error that may occur.
	 */
	bp->b_last_error = 0;
	bp->b_retries = 0;
	bp->b_first_retry_time = 0;

	xfs_buf_do_callbacks(bp);
	bp->b_fspriv = NULL;
	bp->b_iodone = NULL;
	xfs_buf_ioend(bp);
}

/*
 * This is the iodone() function for buffers which have been
 * logged.  It is called when they are eventually flushed out.
 * It should remove the buf item from the AIL, and free the buf item.
 * It is called by xfs_buf_iodone_callbacks() above which will take
 * care of cleaning up the buffer itself.
 */
void
xfs_buf_iodone(
	struct xfs_buf		*bp,
	struct xfs_log_item	*lip)
{
	struct xfs_ail		*ailp = lip->li_ailp;

	ASSERT(BUF_ITEM(lip)->bli_buf == bp);

	xfs_buf_rele(bp);

	/*
	 * If we are forcibly shutting down, this may well be
	 * off the AIL already. That's because we simulate the
	 * log-committed callbacks to unpin these buffers. Or we may never
	 * have put this item on AIL because of the transaction was
	 * aborted forcibly. xfs_trans_ail_delete() takes care of these.
	 *
	 * Either way, AIL is useless if we're forcing a shutdown.
	 */
	spin_lock(&ailp->xa_lock);
	xfs_trans_ail_delete(ailp, lip, SHUTDOWN_CORRUPT_INCORE);
	xfs_buf_item_free(BUF_ITEM(lip));
}

/*
 * Requeue a failed buffer for writeback
 *
 * Return true if the buffer has been re-queued properly, false otherwise
 */
bool
xfs_buf_resubmit_failed_buffers(
	struct xfs_buf		*bp,
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	struct xfs_log_item	*next;

	/*
	 * Clear XFS_LI_FAILED flag from all items before resubmit
	 *
	 * XFS_LI_FAILED set/clear is protected by xa_lock, caller  this
	 * function already have it acquired
	 */
	for (; lip; lip = next) {
		next = lip->li_bio_list;
		xfs_clear_li_failed(lip);
	}

	/* Add this buffer back to the delayed write list */
	return xfs_buf_delwri_queue(bp, buffer_list);
}
