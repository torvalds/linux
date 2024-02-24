// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
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
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_icache.h"
#include "xfs_bmap_util.h"
#include "xfs_iwalk.h"
#include "xfs_ialloc.h"
#include "xfs_sb.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/iscan.h"
#include "scrub/quota.h"
#include "scrub/quotacheck.h"
#include "scrub/trace.h"

/*
 * Live Quotacheck Repair
 * ======================
 *
 * Use the live quota counter information that we collected to replace the
 * counter values in the incore dquots.  A scrub->repair cycle should have left
 * the live data and hooks active, so this is safe so long as we make sure the
 * dquot is locked.
 */

/* Commit new counters to a dquot. */
static int
xqcheck_commit_dquot(
	struct xqcheck		*xqc,
	xfs_dqtype_t		dqtype,
	struct xfs_dquot	*dq)
{
	struct xqcheck_dquot	xcdq;
	struct xfarray		*counts = xqcheck_counters_for(xqc, dqtype);
	int64_t			delta;
	bool			dirty = false;
	int			error = 0;

	/* Unlock the dquot just long enough to allocate a transaction. */
	xfs_dqunlock(dq);
	error = xchk_trans_alloc(xqc->sc, 0);
	xfs_dqlock(dq);
	if (error)
		return error;

	xfs_trans_dqjoin(xqc->sc->tp, dq);

	if (xchk_iscan_aborted(&xqc->iscan)) {
		error = -ECANCELED;
		goto out_cancel;
	}

	mutex_lock(&xqc->lock);
	error = xfarray_load_sparse(counts, dq->q_id, &xcdq);
	if (error)
		goto out_unlock;

	/* Adjust counters as needed. */
	delta = (int64_t)xcdq.icount - dq->q_ino.count;
	if (delta) {
		dq->q_ino.reserved += delta;
		dq->q_ino.count += delta;
		dirty = true;
	}

	delta = (int64_t)xcdq.bcount - dq->q_blk.count;
	if (delta) {
		dq->q_blk.reserved += delta;
		dq->q_blk.count += delta;
		dirty = true;
	}

	delta = (int64_t)xcdq.rtbcount - dq->q_rtb.count;
	if (delta) {
		dq->q_rtb.reserved += delta;
		dq->q_rtb.count += delta;
		dirty = true;
	}

	xcdq.flags |= (XQCHECK_DQUOT_REPAIR_SCANNED | XQCHECK_DQUOT_WRITTEN);
	error = xfarray_store(counts, dq->q_id, &xcdq);
	if (error == -EFBIG) {
		/*
		 * EFBIG means we tried to store data at too high a byte offset
		 * in the sparse array.  IOWs, we cannot complete the repair
		 * and must cancel the whole operation.  This should never
		 * happen, but we need to catch it anyway.
		 */
		error = -ECANCELED;
	}
	mutex_unlock(&xqc->lock);
	if (error || !dirty)
		goto out_cancel;

	trace_xrep_quotacheck_dquot(xqc->sc->mp, dq->q_type, dq->q_id);

	/* Commit the dirty dquot to disk. */
	dq->q_flags |= XFS_DQFLAG_DIRTY;
	if (dq->q_id)
		xfs_qm_adjust_dqtimers(dq);
	xfs_trans_log_dquot(xqc->sc->tp, dq);

	/*
	 * Transaction commit unlocks the dquot, so we must re-lock it so that
	 * the caller can put the reference (which apparently requires a locked
	 * dquot).
	 */
	error = xrep_trans_commit(xqc->sc);
	xfs_dqlock(dq);
	return error;

out_unlock:
	mutex_unlock(&xqc->lock);
out_cancel:
	xchk_trans_cancel(xqc->sc);

	/* Re-lock the dquot so the caller can put the reference. */
	xfs_dqlock(dq);
	return error;
}

/* Commit new quota counters for a particular quota type. */
STATIC int
xqcheck_commit_dqtype(
	struct xqcheck		*xqc,
	unsigned int		dqtype)
{
	struct xchk_dqiter	cursor = { };
	struct xqcheck_dquot	xcdq;
	struct xfs_scrub	*sc = xqc->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfarray		*counts = xqcheck_counters_for(xqc, dqtype);
	struct xfs_dquot	*dq;
	xfarray_idx_t		cur = XFARRAY_CURSOR_INIT;
	int			error;

	/*
	 * Update the counters of every dquot that the quota file knows about.
	 */
	xchk_dqiter_init(&cursor, sc, dqtype);
	while ((error = xchk_dquot_iter(&cursor, &dq)) == 1) {
		error = xqcheck_commit_dquot(xqc, dqtype, dq);
		xfs_qm_dqput(dq);
		if (error)
			break;
	}
	if (error)
		return error;

	/*
	 * Make a second pass to deal with the dquots that we know about but
	 * the quota file previously did not know about.
	 */
	mutex_lock(&xqc->lock);
	while ((error = xfarray_iter(counts, &cur, &xcdq)) == 1) {
		xfs_dqid_t	id = cur - 1;

		if (xcdq.flags & XQCHECK_DQUOT_REPAIR_SCANNED)
			continue;

		mutex_unlock(&xqc->lock);

		/*
		 * Grab the dquot, allowing for dquot block allocation in a
		 * separate transaction.  We committed the scrub transaction
		 * in a previous step, so we will not be creating nested
		 * transactions here.
		 */
		error = xfs_qm_dqget(mp, id, dqtype, true, &dq);
		if (error)
			return error;

		error = xqcheck_commit_dquot(xqc, dqtype, dq);
		xfs_qm_dqput(dq);
		if (error)
			return error;

		mutex_lock(&xqc->lock);
	}
	mutex_unlock(&xqc->lock);

	return error;
}

/* Figure out quota CHKD flags for the running quota types. */
static inline unsigned int
xqcheck_chkd_flags(
	struct xfs_mount	*mp)
{
	unsigned int		ret = 0;

	if (XFS_IS_UQUOTA_ON(mp))
		ret |= XFS_UQUOTA_CHKD;
	if (XFS_IS_GQUOTA_ON(mp))
		ret |= XFS_GQUOTA_CHKD;
	if (XFS_IS_PQUOTA_ON(mp))
		ret |= XFS_PQUOTA_CHKD;
	return ret;
}

/* Commit the new dquot counters. */
int
xrep_quotacheck(
	struct xfs_scrub	*sc)
{
	struct xqcheck		*xqc = sc->buf;
	unsigned int		qflags = xqcheck_chkd_flags(sc->mp);
	int			error;

	/*
	 * Clear the CHKD flag for the running quota types and commit the scrub
	 * transaction so that we can allocate new quota block mappings if we
	 * have to.  If we crash after this point, the sb still has the CHKD
	 * flags cleared, so mount quotacheck will fix all of this up.
	 */
	xrep_update_qflags(sc, qflags, 0);
	error = xrep_trans_commit(sc);
	if (error)
		return error;

	/* Commit the new counters to the dquots. */
	if (xqc->ucounts) {
		error = xqcheck_commit_dqtype(xqc, XFS_DQTYPE_USER);
		if (error)
			return error;
	}
	if (xqc->gcounts) {
		error = xqcheck_commit_dqtype(xqc, XFS_DQTYPE_GROUP);
		if (error)
			return error;
	}
	if (xqc->pcounts) {
		error = xqcheck_commit_dqtype(xqc, XFS_DQTYPE_PROJ);
		if (error)
			return error;
	}

	/* Set the CHKD flags now that we've fixed quota counts. */
	error = xchk_trans_alloc(sc, 0);
	if (error)
		return error;

	xrep_update_qflags(sc, 0, qflags);
	return xrep_trans_commit(sc);
}
