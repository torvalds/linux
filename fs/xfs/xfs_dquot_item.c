// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
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
#include "xfs_quota.h"
#include "xfs_error.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"
#include "xfs_log.h"

static inline struct xfs_dq_logitem *DQUOT_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_dq_logitem, qli_item);
}

/*
 * returns the number of iovecs needed to log the given dquot item.
 */
STATIC void
xfs_qm_dquot_logitem_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 2;
	*nbytes += sizeof(struct xfs_dq_logformat) +
		   sizeof(struct xfs_disk_dquot);
}

/*
 * fills in the vector of log iovecs for the given dquot log item.
 */
STATIC void
xfs_qm_dquot_logitem_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_dq_logitem	*qlip = DQUOT_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;
	struct xfs_dq_logformat	*qlf;

	qlf = xlog_prepare_iovec(lv, &vecp, XLOG_REG_TYPE_QFORMAT);
	qlf->qlf_type = XFS_LI_DQUOT;
	qlf->qlf_size = 2;
	qlf->qlf_id = be32_to_cpu(qlip->qli_dquot->q_core.d_id);
	qlf->qlf_blkno = qlip->qli_dquot->q_blkno;
	qlf->qlf_len = 1;
	qlf->qlf_boffset = qlip->qli_dquot->q_bufoffset;
	xlog_finish_iovec(lv, vecp, sizeof(struct xfs_dq_logformat));

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_DQUOT,
			&qlip->qli_dquot->q_core,
			sizeof(struct xfs_disk_dquot));
}

/*
 * Increment the pin count of the given dquot.
 */
STATIC void
xfs_qm_dquot_logitem_pin(
	struct xfs_log_item	*lip)
{
	struct xfs_dquot	*dqp = DQUOT_ITEM(lip)->qli_dquot;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	atomic_inc(&dqp->q_pincount);
}

/*
 * Decrement the pin count of the given dquot, and wake up
 * anyone in xfs_dqwait_unpin() if the count goes to 0.	 The
 * dquot must have been previously pinned with a call to
 * xfs_qm_dquot_logitem_pin().
 */
STATIC void
xfs_qm_dquot_logitem_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_dquot	*dqp = DQUOT_ITEM(lip)->qli_dquot;

	ASSERT(atomic_read(&dqp->q_pincount) > 0);
	if (atomic_dec_and_test(&dqp->q_pincount))
		wake_up(&dqp->q_pinwait);
}

/*
 * This is called to wait for the given dquot to be unpinned.
 * Most of these pin/unpin routines are plagiarized from inode code.
 */
void
xfs_qm_dqunpin_wait(
	struct xfs_dquot	*dqp)
{
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	if (atomic_read(&dqp->q_pincount) == 0)
		return;

	/*
	 * Give the log a push so we don't wait here too long.
	 */
	xfs_log_force(dqp->q_mount, 0);
	wait_event(dqp->q_pinwait, (atomic_read(&dqp->q_pincount) == 0));
}

/*
 * Callback used to mark a buffer with XFS_LI_FAILED when items in the buffer
 * have been failed during writeback
 *
 * this informs the AIL that the dquot is already flush locked on the next push,
 * and acquires a hold on the buffer to ensure that it isn't reclaimed before
 * dirty data makes it to disk.
 */
STATIC void
xfs_dquot_item_error(
	struct xfs_log_item	*lip,
	struct xfs_buf		*bp)
{
	ASSERT(!completion_done(&DQUOT_ITEM(lip)->qli_dquot->q_flush));
	xfs_set_li_failed(lip, bp);
}

STATIC uint
xfs_qm_dquot_logitem_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
		__releases(&lip->li_ailp->ail_lock)
		__acquires(&lip->li_ailp->ail_lock)
{
	struct xfs_dquot	*dqp = DQUOT_ITEM(lip)->qli_dquot;
	struct xfs_buf		*bp = lip->li_buf;
	uint			rval = XFS_ITEM_SUCCESS;
	int			error;

	if (atomic_read(&dqp->q_pincount) > 0)
		return XFS_ITEM_PINNED;

	/*
	 * The buffer containing this item failed to be written back
	 * previously. Resubmit the buffer for IO
	 */
	if (test_bit(XFS_LI_FAILED, &lip->li_flags)) {
		if (!xfs_buf_trylock(bp))
			return XFS_ITEM_LOCKED;

		if (!xfs_buf_resubmit_failed_buffers(bp, buffer_list))
			rval = XFS_ITEM_FLUSHING;

		xfs_buf_unlock(bp);
		return rval;
	}

	if (!xfs_dqlock_nowait(dqp))
		return XFS_ITEM_LOCKED;

	/*
	 * Re-check the pincount now that we stabilized the value by
	 * taking the quota lock.
	 */
	if (atomic_read(&dqp->q_pincount) > 0) {
		rval = XFS_ITEM_PINNED;
		goto out_unlock;
	}

	/*
	 * Someone else is already flushing the dquot.  Nothing we can do
	 * here but wait for the flush to finish and remove the item from
	 * the AIL.
	 */
	if (!xfs_dqflock_nowait(dqp)) {
		rval = XFS_ITEM_FLUSHING;
		goto out_unlock;
	}

	spin_unlock(&lip->li_ailp->ail_lock);

	error = xfs_qm_dqflush(dqp, &bp);
	if (!error) {
		if (!xfs_buf_delwri_queue(bp, buffer_list))
			rval = XFS_ITEM_FLUSHING;
		xfs_buf_relse(bp);
	}

	spin_lock(&lip->li_ailp->ail_lock);
out_unlock:
	xfs_dqunlock(dqp);
	return rval;
}

STATIC void
xfs_qm_dquot_logitem_release(
	struct xfs_log_item	*lip)
{
	struct xfs_dquot	*dqp = DQUOT_ITEM(lip)->qli_dquot;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	/*
	 * dquots are never 'held' from getting unlocked at the end of
	 * a transaction.  Their locking and unlocking is hidden inside the
	 * transaction layer, within trans_commit. Hence, no LI_HOLD flag
	 * for the logitem.
	 */
	xfs_dqunlock(dqp);
}

STATIC void
xfs_qm_dquot_logitem_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		commit_lsn)
{
	return xfs_qm_dquot_logitem_release(lip);
}

/*
 * This is the ops vector for dquots
 */
static const struct xfs_item_ops xfs_dquot_item_ops = {
	.iop_size	= xfs_qm_dquot_logitem_size,
	.iop_format	= xfs_qm_dquot_logitem_format,
	.iop_pin	= xfs_qm_dquot_logitem_pin,
	.iop_unpin	= xfs_qm_dquot_logitem_unpin,
	.iop_release	= xfs_qm_dquot_logitem_release,
	.iop_committing	= xfs_qm_dquot_logitem_committing,
	.iop_push	= xfs_qm_dquot_logitem_push,
	.iop_error	= xfs_dquot_item_error
};

/*
 * Initialize the dquot log item for a newly allocated dquot.
 * The dquot isn't locked at this point, but it isn't on any of the lists
 * either, so we don't care.
 */
void
xfs_qm_dquot_logitem_init(
	struct xfs_dquot	*dqp)
{
	struct xfs_dq_logitem	*lp = &dqp->q_logitem;

	xfs_log_item_init(dqp->q_mount, &lp->qli_item, XFS_LI_DQUOT,
					&xfs_dquot_item_ops);
	lp->qli_dquot = dqp;
}

/*------------------  QUOTAOFF LOG ITEMS  -------------------*/

static inline struct xfs_qoff_logitem *QOFF_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_qoff_logitem, qql_item);
}


/*
 * This returns the number of iovecs needed to log the given quotaoff item.
 * We only need 1 iovec for an quotaoff item.  It just logs the
 * quotaoff_log_format structure.
 */
STATIC void
xfs_qm_qoff_logitem_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_qoff_logitem);
}

STATIC void
xfs_qm_qoff_logitem_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_qoff_logitem	*qflip = QOFF_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;
	struct xfs_qoff_logformat *qlf;

	qlf = xlog_prepare_iovec(lv, &vecp, XLOG_REG_TYPE_QUOTAOFF);
	qlf->qf_type = XFS_LI_QUOTAOFF;
	qlf->qf_size = 1;
	qlf->qf_flags = qflip->qql_flags;
	xlog_finish_iovec(lv, vecp, sizeof(struct xfs_qoff_logitem));
}

/*
 * There isn't much you can do to push a quotaoff item.  It is simply
 * stuck waiting for the log to be flushed to disk.
 */
STATIC uint
xfs_qm_qoff_logitem_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_LOCKED;
}

STATIC xfs_lsn_t
xfs_qm_qoffend_logitem_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_qoff_logitem	*qfe = QOFF_ITEM(lip);
	struct xfs_qoff_logitem	*qfs = qfe->qql_start_lip;
	struct xfs_ail		*ailp = qfs->qql_item.li_ailp;

	/*
	 * Delete the qoff-start logitem from the AIL.
	 * xfs_trans_ail_delete() drops the AIL lock.
	 */
	spin_lock(&ailp->ail_lock);
	xfs_trans_ail_delete(ailp, &qfs->qql_item, SHUTDOWN_LOG_IO_ERROR);

	kmem_free(qfs->qql_item.li_lv_shadow);
	kmem_free(lip->li_lv_shadow);
	kmem_free(qfs);
	kmem_free(qfe);
	return (xfs_lsn_t)-1;
}

static const struct xfs_item_ops xfs_qm_qoffend_logitem_ops = {
	.iop_size	= xfs_qm_qoff_logitem_size,
	.iop_format	= xfs_qm_qoff_logitem_format,
	.iop_committed	= xfs_qm_qoffend_logitem_committed,
	.iop_push	= xfs_qm_qoff_logitem_push,
};

/*
 * This is the ops vector shared by all quotaoff-start log items.
 */
static const struct xfs_item_ops xfs_qm_qoff_logitem_ops = {
	.iop_size	= xfs_qm_qoff_logitem_size,
	.iop_format	= xfs_qm_qoff_logitem_format,
	.iop_push	= xfs_qm_qoff_logitem_push,
};

/*
 * Allocate and initialize an quotaoff item of the correct quota type(s).
 */
struct xfs_qoff_logitem *
xfs_qm_qoff_logitem_init(
	struct xfs_mount	*mp,
	struct xfs_qoff_logitem	*start,
	uint			flags)
{
	struct xfs_qoff_logitem	*qf;

	qf = kmem_zalloc(sizeof(struct xfs_qoff_logitem), KM_SLEEP);

	xfs_log_item_init(mp, &qf->qql_item, XFS_LI_QUOTAOFF, start ?
			&xfs_qm_qoffend_logitem_ops : &xfs_qm_qoff_logitem_ops);
	qf->qql_item.li_mountp = mp;
	qf->qql_start_lip = start;
	qf->qql_flags = flags;
	return qf;
}
