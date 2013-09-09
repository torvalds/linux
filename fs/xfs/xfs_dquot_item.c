/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
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
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"

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
	struct xfs_log_iovec	*logvec)
{
	struct xfs_dq_logitem	*qlip = DQUOT_ITEM(lip);

	logvec->i_addr = &qlip->qli_format;
	logvec->i_len  = sizeof(xfs_dq_logformat_t);
	logvec->i_type = XLOG_REG_TYPE_QFORMAT;
	logvec++;
	logvec->i_addr = &qlip->qli_dquot->q_core;
	logvec->i_len  = sizeof(xfs_disk_dquot_t);
	logvec->i_type = XLOG_REG_TYPE_DQUOT;

	qlip->qli_format.qlf_size = 2;

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

STATIC xfs_lsn_t
xfs_qm_dquot_logitem_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	/*
	 * We always re-log the entire dquot when it becomes dirty,
	 * so, the latest copy _is_ the only one that matters.
	 */
	return lsn;
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
{
	struct xfs_dquot	*dqp = DQUOT_ITEM(lip)->qli_dquot;
	struct xfs_buf		*bp = NULL;
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

	spin_unlock(&lip->li_ailp->xa_lock);

	error = xfs_qm_dqflush(dqp, &bp);
	if (error) {
		xfs_warn(dqp->q_mount, "%s: push error %d on dqp %p",
			__func__, error, dqp);
	} else {
		if (!xfs_buf_delwri_queue(bp, buffer_list))
			rval = XFS_ITEM_FLUSHING;
		xfs_buf_relse(bp);
	}

	spin_lock(&lip->li_ailp->xa_lock);
out_unlock:
	xfs_dqunlock(dqp);
	return rval;
}

/*
 * Unlock the dquot associated with the log item.
 * Clear the fields of the dquot and dquot log item that
 * are specific to the current transaction.  If the
 * hold flags is set, do not unlock the dquot.
 */
STATIC void
xfs_qm_dquot_logitem_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_dquot	*dqp = DQUOT_ITEM(lip)->qli_dquot;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	/*
	 * Clear the transaction pointer in the dquot
	 */
	dqp->q_transp = NULL;

	/*
	 * dquots are never 'held' from getting unlocked at the end of
	 * a transaction.  Their locking and unlocking is hidden inside the
	 * transaction layer, within trans_commit. Hence, no LI_HOLD flag
	 * for the logitem.
	 */
	xfs_dqunlock(dqp);
}

/*
 * this needs to stamp an lsn into the dquot, I think.
 * rpc's that look at user dquot's would then have to
 * push on the dependency recorded in the dquot
 */
STATIC void
xfs_qm_dquot_logitem_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector for dquots
 */
static const struct xfs_item_ops xfs_dquot_item_ops = {
	.iop_size	= xfs_qm_dquot_logitem_size,
	.iop_format	= xfs_qm_dquot_logitem_format,
	.iop_pin	= xfs_qm_dquot_logitem_pin,
	.iop_unpin	= xfs_qm_dquot_logitem_unpin,
	.iop_unlock	= xfs_qm_dquot_logitem_unlock,
	.iop_committed	= xfs_qm_dquot_logitem_committed,
	.iop_push	= xfs_qm_dquot_logitem_push,
	.iop_committing = xfs_qm_dquot_logitem_committing
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
	lp->qli_format.qlf_type = XFS_LI_DQUOT;
	lp->qli_format.qlf_id = be32_to_cpu(dqp->q_core.d_id);
	lp->qli_format.qlf_blkno = dqp->q_blkno;
	lp->qli_format.qlf_len = 1;
	/*
	 * This is just the offset of this dquot within its buffer
	 * (which is currently 1 FSB and probably won't change).
	 * Hence 32 bits for this offset should be just fine.
	 * Alternatively, we can store (bufoffset / sizeof(xfs_dqblk_t))
	 * here, and recompute it at recovery time.
	 */
	lp->qli_format.qlf_boffset = (__uint32_t)dqp->q_bufoffset;
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

/*
 * This is called to fill in the vector of log iovecs for the
 * given quotaoff log item. We use only 1 iovec, and we point that
 * at the quotaoff_log_format structure embedded in the quotaoff item.
 * It is at this point that we assert that all of the extent
 * slots in the quotaoff item have been filled.
 */
STATIC void
xfs_qm_qoff_logitem_format(
	struct xfs_log_item	*lip,
	struct xfs_log_iovec	*log_vector)
{
	struct xfs_qoff_logitem	*qflip = QOFF_ITEM(lip);

	ASSERT(qflip->qql_format.qf_type == XFS_LI_QUOTAOFF);

	log_vector->i_addr = &qflip->qql_format;
	log_vector->i_len = sizeof(xfs_qoff_logitem_t);
	log_vector->i_type = XLOG_REG_TYPE_QUOTAOFF;
	qflip->qql_format.qf_size = 1;
}

/*
 * Pinning has no meaning for an quotaoff item, so just return.
 */
STATIC void
xfs_qm_qoff_logitem_pin(
	struct xfs_log_item	*lip)
{
}

/*
 * Since pinning has no meaning for an quotaoff item, unpinning does
 * not either.
 */
STATIC void
xfs_qm_qoff_logitem_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
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

/*
 * Quotaoff items have no locking or pushing, so return failure
 * so that the caller doesn't bother with us.
 */
STATIC void
xfs_qm_qoff_logitem_unlock(
	struct xfs_log_item	*lip)
{
}

/*
 * The quotaoff-start-item is logged only once and cannot be moved in the log,
 * so simply return the lsn at which it's been logged.
 */
STATIC xfs_lsn_t
xfs_qm_qoff_logitem_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	return lsn;
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
	spin_lock(&ailp->xa_lock);
	xfs_trans_ail_delete(ailp, &qfs->qql_item, SHUTDOWN_LOG_IO_ERROR);

	kmem_free(qfs);
	kmem_free(qfe);
	return (xfs_lsn_t)-1;
}

/*
 * XXX rcc - don't know quite what to do with this.  I think we can
 * just ignore it.  The only time that isn't the case is if we allow
 * the client to somehow see that quotas have been turned off in which
 * we can't allow that to get back until the quotaoff hits the disk.
 * So how would that happen?  Also, do we need different routines for
 * quotaoff start and quotaoff end?  I suspect the answer is yes but
 * to be sure, I need to look at the recovery code and see how quota off
 * recovery is handled (do we roll forward or back or do something else).
 * If we roll forwards or backwards, then we need two separate routines,
 * one that does nothing and one that stamps in the lsn that matters
 * (truly makes the quotaoff irrevocable).  If we do something else,
 * then maybe we don't need two.
 */
STATIC void
xfs_qm_qoff_logitem_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		commit_lsn)
{
}

static const struct xfs_item_ops xfs_qm_qoffend_logitem_ops = {
	.iop_size	= xfs_qm_qoff_logitem_size,
	.iop_format	= xfs_qm_qoff_logitem_format,
	.iop_pin	= xfs_qm_qoff_logitem_pin,
	.iop_unpin	= xfs_qm_qoff_logitem_unpin,
	.iop_unlock	= xfs_qm_qoff_logitem_unlock,
	.iop_committed	= xfs_qm_qoffend_logitem_committed,
	.iop_push	= xfs_qm_qoff_logitem_push,
	.iop_committing = xfs_qm_qoff_logitem_committing
};

/*
 * This is the ops vector shared by all quotaoff-start log items.
 */
static const struct xfs_item_ops xfs_qm_qoff_logitem_ops = {
	.iop_size	= xfs_qm_qoff_logitem_size,
	.iop_format	= xfs_qm_qoff_logitem_format,
	.iop_pin	= xfs_qm_qoff_logitem_pin,
	.iop_unpin	= xfs_qm_qoff_logitem_unpin,
	.iop_unlock	= xfs_qm_qoff_logitem_unlock,
	.iop_committed	= xfs_qm_qoff_logitem_committed,
	.iop_push	= xfs_qm_qoff_logitem_push,
	.iop_committing = xfs_qm_qoff_logitem_committing
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
	qf->qql_format.qf_type = XFS_LI_QUOTAOFF;
	qf->qql_format.qf_flags = flags;
	qf->qql_start_lip = start;
	return qf;
}
