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
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"
#include "xfs_log.h"
#include "xfs_error.h"

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
	struct xfs_disk_dquot	ddq;
	struct xfs_dq_logitem	*qlip = DQUOT_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;
	struct xfs_dq_logformat	*qlf;

	qlf = xlog_prepare_iovec(lv, &vecp, XLOG_REG_TYPE_QFORMAT);
	qlf->qlf_type = XFS_LI_DQUOT;
	qlf->qlf_size = 2;
	qlf->qlf_id = qlip->qli_dquot->q_id;
	qlf->qlf_blkno = qlip->qli_dquot->q_blkno;
	qlf->qlf_len = 1;
	qlf->qlf_boffset = qlip->qli_dquot->q_bufoffset;
	xlog_finish_iovec(lv, vecp, sizeof(struct xfs_dq_logformat));

	xfs_dquot_to_disk(&ddq, qlip->qli_dquot);

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_DQUOT, &ddq,
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

STATIC uint
xfs_qm_dquot_logitem_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
		__releases(&lip->li_ailp->ail_lock)
		__acquires(&lip->li_ailp->ail_lock)
{
	struct xfs_dq_logitem	*qlip = DQUOT_ITEM(lip);
	struct xfs_dquot	*dqp = qlip->qli_dquot;
	struct xfs_buf		*bp;
	uint			rval = XFS_ITEM_SUCCESS;
	int			error;

	if (atomic_read(&dqp->q_pincount) > 0)
		return XFS_ITEM_PINNED;

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

	error = xfs_dquot_use_attached_buf(dqp, &bp);
	if (error == -EAGAIN) {
		xfs_dqfunlock(dqp);
		rval = XFS_ITEM_LOCKED;
		goto out_relock_ail;
	}

	/*
	 * dqflush completes dqflock on error, and the delwri ioend does it on
	 * success.
	 */
	error = xfs_qm_dqflush(dqp, bp);
	if (!error) {
		if (!xfs_buf_delwri_queue(bp, buffer_list))
			rval = XFS_ITEM_FLUSHING;
	}
	xfs_buf_relse(bp);

out_relock_ail:
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
	xfs_csn_t		seq)
{
	return xfs_qm_dquot_logitem_release(lip);
}

#ifdef DEBUG_EXPENSIVE
static void
xfs_qm_dquot_logitem_precommit_check(
	struct xfs_dquot	*dqp)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_disk_dquot	ddq = { };
	xfs_failaddr_t		fa;

	xfs_dquot_to_disk(&ddq, dqp);
	fa = xfs_dquot_verify(mp, &ddq, dqp->q_id);
	if (fa) {
		XFS_CORRUPTION_ERROR("Bad dquot during logging",
				XFS_ERRLEVEL_LOW, mp, &ddq, sizeof(ddq));
		xfs_alert(mp,
 "Metadata corruption detected at %pS, dquot 0x%x",
				fa, dqp->q_id);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		ASSERT(fa == NULL);
	}
}
#else
# define xfs_qm_dquot_logitem_precommit_check(...)	((void)0)
#endif

static int
xfs_qm_dquot_logitem_precommit(
	struct xfs_trans	*tp,
	struct xfs_log_item	*lip)
{
	struct xfs_dq_logitem	*qlip = DQUOT_ITEM(lip);
	struct xfs_dquot	*dqp = qlip->qli_dquot;

	xfs_qm_dquot_logitem_precommit_check(dqp);

	return xfs_dquot_attach_buf(tp, dqp);
}

static const struct xfs_item_ops xfs_dquot_item_ops = {
	.iop_size	= xfs_qm_dquot_logitem_size,
	.iop_precommit	= xfs_qm_dquot_logitem_precommit,
	.iop_format	= xfs_qm_dquot_logitem_format,
	.iop_pin	= xfs_qm_dquot_logitem_pin,
	.iop_unpin	= xfs_qm_dquot_logitem_unpin,
	.iop_release	= xfs_qm_dquot_logitem_release,
	.iop_committing	= xfs_qm_dquot_logitem_committing,
	.iop_push	= xfs_qm_dquot_logitem_push,
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
	spin_lock_init(&lp->qli_lock);
	lp->qli_dquot = dqp;
	lp->qli_dirty = false;
}
