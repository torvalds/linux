// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_quota.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_error.h"


struct kmem_cache	*xfs_buf_item_cache;

static inline struct xfs_buf_log_item *BUF_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_buf_log_item, bli_item);
}

/* Is this log iovec plausibly large enough to contain the buffer log format? */
bool
xfs_buf_log_check_iovec(
	struct xfs_log_iovec		*iovec)
{
	struct xfs_buf_log_format	*blfp = iovec->i_addr;
	char				*bmp_end;
	char				*item_end;

	if (offsetof(struct xfs_buf_log_format, blf_data_map) > iovec->i_len)
		return false;

	item_end = (char *)iovec->i_addr + iovec->i_len;
	bmp_end = (char *)&blfp->blf_data_map[blfp->blf_map_size];
	return bmp_end <= item_end;
}

static inline int
xfs_buf_log_format_size(
	struct xfs_buf_log_format *blfp)
{
	return offsetof(struct xfs_buf_log_format, blf_data_map) +
			(blfp->blf_map_size * sizeof(blfp->blf_data_map[0]));
}

/*
 * Return the number of log iovecs and space needed to log the given buf log
 * item segment.
 *
 * It calculates this as 1 iovec for the buf log format structure and 1 for each
 * stretch of non-contiguous chunks to be logged.  Contiguous chunks are logged
 * in a single iovec.
 */
STATIC void
xfs_buf_item_size_segment(
	struct xfs_buf_log_item		*bip,
	struct xfs_buf_log_format	*blfp,
	uint				offset,
	int				*nvecs,
	int				*nbytes)
{
	int				first_bit;
	int				nbits;

	first_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size, 0);
	if (first_bit == -1)
		return;

	(*nvecs)++;
	*nbytes += xfs_buf_log_format_size(blfp);

	do {
		nbits = xfs_contig_bits(blfp->blf_data_map,
					blfp->blf_map_size, first_bit);
		ASSERT(nbits > 0);
		(*nvecs)++;
		*nbytes += nbits * XFS_BLF_CHUNK;

		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		first_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size,
					(uint)first_bit + nbits + 1);
	} while (first_bit != -1);

	return;
}

/*
 * Compute the worst case log item overhead for an invalidated buffer with the
 * given map count and block size.
 */
unsigned int
xfs_buf_inval_log_space(
	unsigned int	map_count,
	unsigned int	blocksize)
{
	unsigned int	chunks = DIV_ROUND_UP(blocksize, XFS_BLF_CHUNK);
	unsigned int	bitmap_size = DIV_ROUND_UP(chunks, NBWORD);
	unsigned int	ret =
		offsetof(struct xfs_buf_log_format, blf_data_map) +
			(bitmap_size * sizeof_field(struct xfs_buf_log_format,
						    blf_data_map[0]));

	return ret * map_count;
}

/*
 * Return the number of log iovecs and space needed to log the given buf log
 * item.
 *
 * Discontiguous buffers need a format structure per region that is being
 * logged. This makes the changes in the buffer appear to log recovery as though
 * they came from separate buffers, just like would occur if multiple buffers
 * were used instead of a single discontiguous buffer. This enables
 * discontiguous buffers to be in-memory constructs, completely transparent to
 * what ends up on disk.
 *
 * If the XFS_BLI_STALE flag has been set, then log nothing but the buf log
 * format structures. If the item has previously been logged and has dirty
 * regions, we do not relog them in stale buffers. This has the effect of
 * reducing the size of the relogged item by the amount of dirty data tracked
 * by the log item. This can result in the committing transaction reducing the
 * amount of space being consumed by the CIL.
 */
STATIC void
xfs_buf_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	int			i;
	int			bytes;
	uint			offset = 0;

	ASSERT(atomic_read(&bip->bli_refcount) > 0);
	if (bip->bli_flags & XFS_BLI_STALE) {
		/*
		 * The buffer is stale, so all we need to log is the buf log
		 * format structure with the cancel flag in it as we are never
		 * going to replay the changes tracked in the log item.
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
		 * The buffer has been logged just to order it. It is not being
		 * included in the transaction commit, so no vectors are used at
		 * all.
		 */
		trace_xfs_buf_item_size_ordered(bip);
		*nvecs = XFS_LOG_VEC_ORDERED;
		return;
	}

	/*
	 * The vector count is based on the number of buffer vectors we have
	 * dirty bits in. This will only be greater than one when we have a
	 * compound buffer with more than one segment dirty. Hence for compound
	 * buffers we need to track which segment the dirty bits correspond to,
	 * and when we move from one segment to the next increment the vector
	 * count for the extra buf log format structure that will need to be
	 * written.
	 */
	bytes = 0;
	for (i = 0; i < bip->bli_format_count; i++) {
		xfs_buf_item_size_segment(bip, &bip->bli_formats[i], offset,
					  nvecs, &bytes);
		offset += BBTOB(bp->b_maps[i].bm_len);
	}

	/*
	 * Round up the buffer size required to minimise the number of memory
	 * allocations that need to be done as this item grows when relogged by
	 * repeated modifications.
	 */
	*nbytes = round_up(bytes, 512);
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

static void
xfs_buf_item_format_segment(
	struct xfs_buf_log_item	*bip,
	struct xfs_log_vec	*lv,
	struct xfs_log_iovec	**vecp,
	uint			offset,
	struct xfs_buf_log_format *blfp)
{
	struct xfs_buf		*bp = bip->bli_buf;
	uint			base_size;
	int			first_bit;
	uint			nbits;

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
	do {
		ASSERT(first_bit >= 0);
		nbits = xfs_contig_bits(blfp->blf_data_map,
					blfp->blf_map_size, first_bit);
		ASSERT(nbits > 0);
		xfs_buf_item_copy_iovec(lv, vecp, bp, offset,
					first_bit, nbits);
		blfp->blf_size++;

		/*
		 * This takes the bit number to start looking from and
		 * returns the next set bit from there.  It returns -1
		 * if there are no more bits set or the start bit is
		 * beyond the end of the bitmap.
		 */
		first_bit = xfs_next_bit(blfp->blf_data_map, blfp->blf_map_size,
					(uint)first_bit + nbits + 1);
	} while (first_bit != -1);

	return;
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
	ASSERT(!(bip->bli_flags & XFS_BLI_ORDERED) ||
	       (bip->bli_flags & XFS_BLI_STALE));


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
		if (xfs_has_v3inodes(lip->li_log->l_mp) ||
		    !((bip->bli_flags & XFS_BLI_INODE_ALLOC_BUF) &&
		      xfs_log_item_in_current_chkpt(lip)))
			bip->__bli_format.blf_flags |= XFS_BLF_INODE_BUF;
		bip->bli_flags &= ~XFS_BLI_INODE_BUF;
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
 * We take a reference to the buffer log item here so that the BLI life cycle
 * extends at least until the buffer is unpinned via xfs_buf_item_unpin() and
 * inserted into the AIL.
 *
 * We also need to take a reference to the buffer itself as the BLI unpin
 * processing requires accessing the buffer after the BLI has dropped the final
 * BLI reference. See xfs_buf_item_unpin() for an explanation.
 * If unpins race to drop the final BLI reference and only the
 * BLI owns a reference to the buffer, then the loser of the race can have the
 * buffer fgreed from under it (e.g. on shutdown). Taking a buffer reference per
 * pin count ensures the life cycle of the buffer extends for as
 * long as we hold the buffer pin reference in xfs_buf_item_unpin().
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

	xfs_buf_hold(bip->bli_buf);
	atomic_inc(&bip->bli_refcount);
	atomic_inc(&bip->bli_buf->b_pin_count);
}

/*
 * This is called to unpin the buffer associated with the buf log item which was
 * previously pinned with a call to xfs_buf_item_pin().  We enter this function
 * with a buffer pin count, a buffer reference and a BLI reference.
 *
 * We must drop the BLI reference before we unpin the buffer because the AIL
 * doesn't acquire a BLI reference whenever it accesses it. Therefore if the
 * refcount drops to zero, the bli could still be AIL resident and the buffer
 * submitted for I/O at any point before we return. This can result in IO
 * completion freeing the buffer while we are still trying to access it here.
 * This race condition can also occur in shutdown situations where we abort and
 * unpin buffers from contexts other that journal IO completion.
 *
 * Hence we have to hold a buffer reference per pin count to ensure that the
 * buffer cannot be freed until we have finished processing the unpin operation.
 * The reference is taken in xfs_buf_item_pin(), and we must hold it until we
 * are done processing the buffer state. In the case of an abort (remove =
 * true) then we re-use the current pin reference as the IO reference we hand
 * off to IO failure handling.
 */
STATIC void
xfs_buf_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	int			stale = bip->bli_flags & XFS_BLI_STALE;
	int			freed;

	ASSERT(bp->b_log_item == bip);
	ASSERT(atomic_read(&bip->bli_refcount) > 0);

	trace_xfs_buf_item_unpin(bip);

	freed = atomic_dec_and_test(&bip->bli_refcount);
	if (atomic_dec_and_test(&bp->b_pin_count))
		wake_up_all(&bp->b_waiters);

	/*
	 * Nothing to do but drop the buffer pin reference if the BLI is
	 * still active.
	 */
	if (!freed) {
		xfs_buf_rele(bp);
		return;
	}

	if (stale) {
		ASSERT(bip->bli_flags & XFS_BLI_STALE);
		ASSERT(xfs_buf_islocked(bp));
		ASSERT(bp->b_flags & XBF_STALE);
		ASSERT(bip->__bli_format.blf_flags & XFS_BLF_CANCEL);
		ASSERT(list_empty(&lip->li_trans));
		ASSERT(!bp->b_transp);

		trace_xfs_buf_item_unpin_stale(bip);

		/*
		 * The buffer has been locked and referenced since it was marked
		 * stale so we own both lock and reference exclusively here. We
		 * do not need the pin reference any more, so drop it now so
		 * that we only have one reference to drop once item completion
		 * processing is complete.
		 */
		xfs_buf_rele(bp);

		/*
		 * If we get called here because of an IO error, we may or may
		 * not have the item on the AIL. xfs_trans_ail_delete() will
		 * take care of that situation. xfs_trans_ail_delete() drops
		 * the AIL lock.
		 */
		if (bip->bli_flags & XFS_BLI_STALE_INODE) {
			xfs_buf_item_done(bp);
			xfs_buf_inode_iodone(bp);
			ASSERT(list_empty(&bp->b_li_list));
		} else {
			xfs_trans_ail_delete(lip, SHUTDOWN_LOG_IO_ERROR);
			xfs_buf_item_relse(bp);
			ASSERT(bp->b_log_item == NULL);
		}
		xfs_buf_relse(bp);
		return;
	}

	if (remove) {
		/*
		 * We need to simulate an async IO failures here to ensure that
		 * the correct error completion is run on this buffer. This
		 * requires a reference to the buffer and for the buffer to be
		 * locked. We can safely pass ownership of the pin reference to
		 * the IO to ensure that nothing can free the buffer while we
		 * wait for the lock and then run the IO failure completion.
		 */
		xfs_buf_lock(bp);
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_ioend_fail(bp);
		return;
	}

	/*
	 * BLI has no more active references - it will be moved to the AIL to
	 * manage the remaining BLI/buffer life cycle. There is nothing left for
	 * us to do here so drop the pin reference to the buffer.
	 */
	xfs_buf_rele(bp);
}

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
	if (bp->b_flags & XBF_WRITE_FAIL) {
		xfs_buf_alert_ratelimited(bp, "XFS: Failing async write",
	    "Failing async write on buffer block 0x%llx. Retrying async write.",
					  (long long)xfs_buf_daddr(bp));
	}

	if (!xfs_buf_delwri_queue(bp, buffer_list))
		rval = XFS_ITEM_FLUSHING;
	xfs_buf_unlock(bp);
	return rval;
}

/*
 * Drop the buffer log item refcount and take appropriate action. This helper
 * determines whether the bli must be freed or not, since a decrement to zero
 * does not necessarily mean the bli is unused.
 *
 * Return true if the bli is freed, false otherwise.
 */
bool
xfs_buf_item_put(
	struct xfs_buf_log_item	*bip)
{
	struct xfs_log_item	*lip = &bip->bli_item;
	bool			aborted;
	bool			dirty;

	/* drop the bli ref and return if it wasn't the last one */
	if (!atomic_dec_and_test(&bip->bli_refcount))
		return false;

	/*
	 * We dropped the last ref and must free the item if clean or aborted.
	 * If the bli is dirty and non-aborted, the buffer was clean in the
	 * transaction but still awaiting writeback from previous changes. In
	 * that case, the bli is freed on buffer writeback completion.
	 */
	aborted = test_bit(XFS_LI_ABORTED, &lip->li_flags) ||
			xlog_is_shutdown(lip->li_log);
	dirty = bip->bli_flags & XFS_BLI_DIRTY;
	if (dirty && !aborted)
		return false;

	/*
	 * The bli is aborted or clean. An aborted item may be in the AIL
	 * regardless of dirty state.  For example, consider an aborted
	 * transaction that invalidated a dirty bli and cleared the dirty
	 * state.
	 */
	if (aborted)
		xfs_trans_ail_delete(lip, 0);
	xfs_buf_item_relse(bip->bli_buf);
	return true;
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
xfs_buf_item_release(
	struct xfs_log_item	*lip)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	bool			released;
	bool			hold = bip->bli_flags & XFS_BLI_HOLD;
	bool			stale = bip->bli_flags & XFS_BLI_STALE;
#if defined(DEBUG) || defined(XFS_WARN)
	bool			ordered = bip->bli_flags & XFS_BLI_ORDERED;
	bool			dirty = bip->bli_flags & XFS_BLI_DIRTY;
	bool			aborted = test_bit(XFS_LI_ABORTED,
						   &lip->li_flags);
#endif

	trace_xfs_buf_item_release(bip);

	/*
	 * The bli dirty state should match whether the blf has logged segments
	 * except for ordered buffers, where only the bli should be dirty.
	 */
	ASSERT((!ordered && dirty == xfs_buf_item_dirty_format(bip)) ||
	       (ordered && dirty && !xfs_buf_item_dirty_format(bip)));
	ASSERT(!stale || (bip->__bli_format.blf_flags & XFS_BLF_CANCEL));

	/*
	 * Clear the buffer's association with this transaction and
	 * per-transaction state from the bli, which has been copied above.
	 */
	bp->b_transp = NULL;
	bip->bli_flags &= ~(XFS_BLI_LOGGED | XFS_BLI_HOLD | XFS_BLI_ORDERED);

	/*
	 * Unref the item and unlock the buffer unless held or stale. Stale
	 * buffers remain locked until final unpin unless the bli is freed by
	 * the unref call. The latter implies shutdown because buffer
	 * invalidation dirties the bli and transaction.
	 */
	released = xfs_buf_item_put(bip);
	if (hold || (stale && !released))
		return;
	ASSERT(!stale || aborted);
	xfs_buf_relse(bp);
}

STATIC void
xfs_buf_item_committing(
	struct xfs_log_item	*lip,
	xfs_csn_t		seq)
{
	return xfs_buf_item_release(lip);
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

#ifdef DEBUG_EXPENSIVE
static int
xfs_buf_item_precommit(
	struct xfs_trans	*tp,
	struct xfs_log_item	*lip)
{
	struct xfs_buf_log_item	*bip = BUF_ITEM(lip);
	struct xfs_buf		*bp = bip->bli_buf;
	struct xfs_mount	*mp = bp->b_mount;
	xfs_failaddr_t		fa;

	if (!bp->b_ops || !bp->b_ops->verify_struct)
		return 0;
	if (bip->bli_flags & XFS_BLI_STALE)
		return 0;

	fa = bp->b_ops->verify_struct(bp);
	if (fa) {
		xfs_buf_verifier_error(bp, -EFSCORRUPTED, bp->b_ops->name,
				bp->b_addr, BBTOB(bp->b_length), fa);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		ASSERT(fa == NULL);
	}

	return 0;
}
#else
# define xfs_buf_item_precommit	NULL
#endif

static const struct xfs_item_ops xfs_buf_item_ops = {
	.iop_size	= xfs_buf_item_size,
	.iop_precommit	= xfs_buf_item_precommit,
	.iop_format	= xfs_buf_item_format,
	.iop_pin	= xfs_buf_item_pin,
	.iop_unpin	= xfs_buf_item_unpin,
	.iop_release	= xfs_buf_item_release,
	.iop_committing	= xfs_buf_item_committing,
	.iop_committed	= xfs_buf_item_committed,
	.iop_push	= xfs_buf_item_push,
};

STATIC void
xfs_buf_item_get_format(
	struct xfs_buf_log_item	*bip,
	int			count)
{
	ASSERT(bip->bli_formats == NULL);
	bip->bli_format_count = count;

	if (count == 1) {
		bip->bli_formats = &bip->__bli_format;
		return;
	}

	bip->bli_formats = kzalloc(count * sizeof(struct xfs_buf_log_format),
				GFP_KERNEL | __GFP_NOFAIL);
}

STATIC void
xfs_buf_item_free_format(
	struct xfs_buf_log_item	*bip)
{
	if (bip->bli_formats != &bip->__bli_format) {
		kfree(bip->bli_formats);
		bip->bli_formats = NULL;
	}
}

/*
 * Allocate a new buf log item to go with the given buffer.
 * Set the buffer's b_log_item field to point to the new
 * buf log item.
 */
int
xfs_buf_item_init(
	struct xfs_buf	*bp,
	struct xfs_mount *mp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	int			chunks;
	int			map_size;
	int			i;

	/*
	 * Check to see if there is already a buf log item for
	 * this buffer. If we do already have one, there is
	 * nothing to do here so return.
	 */
	ASSERT(bp->b_mount == mp);
	if (bip) {
		ASSERT(bip->bli_item.li_type == XFS_LI_BUF);
		ASSERT(!bp->b_transp);
		ASSERT(bip->bli_buf == bp);
		return 0;
	}

	bip = kmem_cache_zalloc(xfs_buf_item_cache, GFP_KERNEL | __GFP_NOFAIL);
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
	xfs_buf_item_get_format(bip, bp->b_map_count);

	for (i = 0; i < bip->bli_format_count; i++) {
		chunks = DIV_ROUND_UP(BBTOB(bp->b_maps[i].bm_len),
				      XFS_BLF_CHUNK);
		map_size = DIV_ROUND_UP(chunks, NBWORD);

		if (map_size > XFS_BLF_DATAMAP_SIZE) {
			kmem_cache_free(xfs_buf_item_cache, bip);
			xfs_err(mp,
	"buffer item dirty bitmap (%u uints) too small to reflect %u bytes!",
					map_size,
					BBTOB(bp->b_maps[i].bm_len));
			return -EFSCORRUPTED;
		}

		bip->bli_formats[i].blf_type = XFS_LI_BUF;
		bip->bli_formats[i].blf_blkno = bp->b_maps[i].bm_bn;
		bip->bli_formats[i].blf_len = bp->b_maps[i].bm_len;
		bip->bli_formats[i].blf_map_size = map_size;
	}

	bp->b_log_item = bip;
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

	ASSERT(first < XFS_BLF_DATAMAP_SIZE * XFS_BLF_CHUNK * NBWORD);
	ASSERT(last < XFS_BLF_DATAMAP_SIZE * XFS_BLF_CHUNK * NBWORD);

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
		end_bit = min(bit + bits_to_set, (uint)NBWORD);
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
		*wordp = 0xffffffff;
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
	struct xfs_buf_log_item	*bip,
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
 * Return true if the buffer has any ranges logged/dirtied by a transaction,
 * false otherwise.
 */
bool
xfs_buf_item_dirty_format(
	struct xfs_buf_log_item	*bip)
{
	int			i;

	for (i = 0; i < bip->bli_format_count; i++) {
		if (!xfs_bitmap_empty(bip->bli_formats[i].blf_data_map,
			     bip->bli_formats[i].blf_map_size))
			return true;
	}

	return false;
}

STATIC void
xfs_buf_item_free(
	struct xfs_buf_log_item	*bip)
{
	xfs_buf_item_free_format(bip);
	kvfree(bip->bli_item.li_lv_shadow);
	kmem_cache_free(xfs_buf_item_cache, bip);
}

/*
 * xfs_buf_item_relse() is called when the buf log item is no longer needed.
 */
void
xfs_buf_item_relse(
	struct xfs_buf	*bp)
{
	struct xfs_buf_log_item	*bip = bp->b_log_item;

	trace_xfs_buf_item_relse(bp, _RET_IP_);
	ASSERT(!test_bit(XFS_LI_IN_AIL, &bip->bli_item.li_flags));

	if (atomic_read(&bip->bli_refcount))
		return;
	bp->b_log_item = NULL;
	xfs_buf_rele(bp);
	xfs_buf_item_free(bip);
}

void
xfs_buf_item_done(
	struct xfs_buf		*bp)
{
	/*
	 * If we are forcibly shutting down, this may well be off the AIL
	 * already. That's because we simulate the log-committed callbacks to
	 * unpin these buffers. Or we may never have put this item on AIL
	 * because of the transaction was aborted forcibly.
	 * xfs_trans_ail_delete() takes care of these.
	 *
	 * Either way, AIL is useless if we're forcing a shutdown.
	 *
	 * Note that log recovery writes might have buffer items that are not on
	 * the AIL even when the file system is not shut down.
	 */
	xfs_trans_ail_delete(&bp->b_log_item->bli_item,
			     (bp->b_flags & _XBF_LOGRECOVERY) ? 0 :
			     SHUTDOWN_CORRUPT_INCORE);
	xfs_buf_item_relse(bp);
}
