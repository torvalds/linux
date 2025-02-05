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
#include "xfs_ialloc.h"
#include "xfs_ag.h"
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
 * Live Quotacheck
 * ===============
 *
 * Quota counters are "summary" metadata, in the sense that they are computed
 * as the summation of the block usage counts for every file on the filesystem.
 * Therefore, we compute the correct icount, bcount, and rtbcount values by
 * creating a shadow quota counter structure and walking every inode.
 */

/* Track the quota deltas for a dquot in a transaction. */
struct xqcheck_dqtrx {
	xfs_dqtype_t		q_type;
	xfs_dqid_t		q_id;

	int64_t			icount_delta;

	int64_t			bcount_delta;
	int64_t			delbcnt_delta;

	int64_t			rtbcount_delta;
	int64_t			delrtb_delta;
};

#define XQCHECK_MAX_NR_DQTRXS	(XFS_QM_TRANS_DQTYPES * XFS_QM_TRANS_MAXDQS)

/*
 * Track the quota deltas for all dquots attached to a transaction if the
 * quota deltas are being applied to an inode that we already scanned.
 */
struct xqcheck_dqacct {
	struct rhash_head	hash;
	uintptr_t		tx_id;
	struct xqcheck_dqtrx	dqtrx[XQCHECK_MAX_NR_DQTRXS];
	unsigned int		refcount;
};

/* Free a shadow dquot accounting structure. */
static void
xqcheck_dqacct_free(
	void			*ptr,
	void			*arg)
{
	struct xqcheck_dqacct	*dqa = ptr;

	kfree(dqa);
}

/* Set us up to scrub quota counters. */
int
xchk_setup_quotacheck(
	struct xfs_scrub	*sc)
{
	if (!XFS_IS_QUOTA_ON(sc->mp))
		return -ENOENT;

	xchk_fsgates_enable(sc, XCHK_FSGATES_QUOTA);

	sc->buf = kzalloc(sizeof(struct xqcheck), XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -ENOMEM;

	return xchk_setup_fs(sc);
}

/*
 * Part 1: Collecting dquot resource usage counts.  For each xfs_dquot attached
 * to each inode, we create a shadow dquot, and compute the inode count and add
 * the data/rt block usage from what we see.
 *
 * To avoid false corruption reports in part 2, any failure in this part must
 * set the INCOMPLETE flag even when a negative errno is returned.  This care
 * must be taken with certain errno values (i.e. EFSBADCRC, EFSCORRUPTED,
 * ECANCELED) that are absorbed into a scrub state flag update by
 * xchk_*_process_error.  Scrub and repair share the same incore data
 * structures, so the INCOMPLETE flag is critical to prevent a repair based on
 * insufficient information.
 *
 * Because we are scanning a live filesystem, it's possible that another thread
 * will try to update the quota counters for an inode that we've already
 * scanned.  This will cause our counts to be incorrect.  Therefore, we hook
 * the live transaction code in two places: (1) when the callers update the
 * per-transaction dqtrx structure to log quota counter updates; and (2) when
 * transaction commit actually logs those updates to the incore dquot.  By
 * shadowing transaction updates in this manner, live quotacheck can ensure
 * by locking the dquot and the shadow structure that its own copies are not
 * out of date.  Because the hook code runs in a different process context from
 * the scrub code and the scrub state flags are not accessed atomically,
 * failures in the hook code must abort the iscan and the scrubber must notice
 * the aborted scan and set the incomplete flag.
 *
 * Note that we use srcu notifier hooks to minimize the overhead when live
 * quotacheck is /not/ running.
 */

/* Update an incore dquot counter information from a live update. */
static int
xqcheck_update_incore_counts(
	struct xqcheck		*xqc,
	struct xfarray		*counts,
	xfs_dqid_t		id,
	int64_t			inodes,
	int64_t			nblks,
	int64_t			rtblks)
{
	struct xqcheck_dquot	xcdq;
	int			error;

	error = xfarray_load_sparse(counts, id, &xcdq);
	if (error)
		return error;

	xcdq.flags |= XQCHECK_DQUOT_WRITTEN;
	xcdq.icount += inodes;
	xcdq.bcount += nblks;
	xcdq.rtbcount += rtblks;

	error = xfarray_store(counts, id, &xcdq);
	if (error == -EFBIG) {
		/*
		 * EFBIG means we tried to store data at too high a byte offset
		 * in the sparse array.  IOWs, we cannot complete the check and
		 * must notify userspace that the check was incomplete.
		 */
		error = -ECANCELED;
	}
	return error;
}

/* Decide if this is the shadow dquot accounting structure for a transaction. */
static int
xqcheck_dqacct_obj_cmpfn(
	struct rhashtable_compare_arg	*arg,
	const void			*obj)
{
	const uintptr_t			*tx_idp = arg->key;
	const struct xqcheck_dqacct	*dqa = obj;

	if (dqa->tx_id != *tx_idp)
		return 1;
	return 0;
}

static const struct rhashtable_params xqcheck_dqacct_hash_params = {
	.min_size		= 32,
	.key_len		= sizeof(uintptr_t),
	.key_offset		= offsetof(struct xqcheck_dqacct, tx_id),
	.head_offset		= offsetof(struct xqcheck_dqacct, hash),
	.automatic_shrinking	= true,
	.obj_cmpfn		= xqcheck_dqacct_obj_cmpfn,
};

/* Find a shadow dqtrx slot for the given dquot. */
STATIC struct xqcheck_dqtrx *
xqcheck_get_dqtrx(
	struct xqcheck_dqacct	*dqa,
	xfs_dqtype_t		q_type,
	xfs_dqid_t		q_id)
{
	int			i;

	for (i = 0; i < XQCHECK_MAX_NR_DQTRXS; i++) {
		if (dqa->dqtrx[i].q_type == 0 ||
		    (dqa->dqtrx[i].q_type == q_type &&
		     dqa->dqtrx[i].q_id == q_id))
			return &dqa->dqtrx[i];
	}

	return NULL;
}

/*
 * Create and fill out a quota delta tracking structure to shadow the updates
 * going on in the regular quota code.
 */
static int
xqcheck_mod_live_ino_dqtrx(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_mod_ino_dqtrx_params *p = data;
	struct xqcheck			*xqc;
	struct xqcheck_dqacct		*dqa;
	struct xqcheck_dqtrx		*dqtrx;
	int				error;

	xqc = container_of(nb, struct xqcheck, qhook.mod_hook.nb);

	/* Skip quota reservation fields. */
	switch (action) {
	case XFS_TRANS_DQ_BCOUNT:
	case XFS_TRANS_DQ_DELBCOUNT:
	case XFS_TRANS_DQ_ICOUNT:
	case XFS_TRANS_DQ_RTBCOUNT:
	case XFS_TRANS_DQ_DELRTBCOUNT:
		break;
	default:
		return NOTIFY_DONE;
	}

	/* Ignore dqtrx updates for quota types we don't care about. */
	switch (p->q_type) {
	case XFS_DQTYPE_USER:
		if (!xqc->ucounts)
			return NOTIFY_DONE;
		break;
	case XFS_DQTYPE_GROUP:
		if (!xqc->gcounts)
			return NOTIFY_DONE;
		break;
	case XFS_DQTYPE_PROJ:
		if (!xqc->pcounts)
			return NOTIFY_DONE;
		break;
	default:
		return NOTIFY_DONE;
	}

	/* Skip inodes that haven't been scanned yet. */
	if (!xchk_iscan_want_live_update(&xqc->iscan, p->ino))
		return NOTIFY_DONE;

	/* Make a shadow quota accounting tracker for this transaction. */
	mutex_lock(&xqc->lock);
	dqa = rhashtable_lookup_fast(&xqc->shadow_dquot_acct, &p->tx_id,
			xqcheck_dqacct_hash_params);
	if (!dqa) {
		dqa = kzalloc(sizeof(struct xqcheck_dqacct), XCHK_GFP_FLAGS);
		if (!dqa)
			goto out_abort;

		dqa->tx_id = p->tx_id;
		error = rhashtable_insert_fast(&xqc->shadow_dquot_acct,
				&dqa->hash, xqcheck_dqacct_hash_params);
		if (error)
			goto out_abort;
	}

	/* Find the shadow dqtrx (or an empty slot) here. */
	dqtrx = xqcheck_get_dqtrx(dqa, p->q_type, p->q_id);
	if (!dqtrx)
		goto out_abort;
	if (dqtrx->q_type == 0) {
		dqtrx->q_type = p->q_type;
		dqtrx->q_id = p->q_id;
		dqa->refcount++;
	}

	/* Update counter */
	switch (action) {
	case XFS_TRANS_DQ_BCOUNT:
		dqtrx->bcount_delta += p->delta;
		break;
	case XFS_TRANS_DQ_DELBCOUNT:
		dqtrx->delbcnt_delta += p->delta;
		break;
	case XFS_TRANS_DQ_ICOUNT:
		dqtrx->icount_delta += p->delta;
		break;
	case XFS_TRANS_DQ_RTBCOUNT:
		dqtrx->rtbcount_delta += p->delta;
		break;
	case XFS_TRANS_DQ_DELRTBCOUNT:
		dqtrx->delrtb_delta += p->delta;
		break;
	}

	mutex_unlock(&xqc->lock);
	return NOTIFY_DONE;

out_abort:
	xchk_iscan_abort(&xqc->iscan);
	mutex_unlock(&xqc->lock);
	return NOTIFY_DONE;
}

/*
 * Apply the transaction quota deltas to our shadow quota accounting info when
 * the regular quota code are doing the same.
 */
static int
xqcheck_apply_live_dqtrx(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_apply_dqtrx_params	*p = data;
	struct xqcheck			*xqc;
	struct xqcheck_dqacct		*dqa;
	struct xqcheck_dqtrx		*dqtrx;
	struct xfarray			*counts;
	int				error;

	xqc = container_of(nb, struct xqcheck, qhook.apply_hook.nb);

	/* Map the dquot type to an incore counter object. */
	switch (p->q_type) {
	case XFS_DQTYPE_USER:
		counts = xqc->ucounts;
		break;
	case XFS_DQTYPE_GROUP:
		counts = xqc->gcounts;
		break;
	case XFS_DQTYPE_PROJ:
		counts = xqc->pcounts;
		break;
	default:
		return NOTIFY_DONE;
	}

	if (xchk_iscan_aborted(&xqc->iscan) || counts == NULL)
		return NOTIFY_DONE;

	/*
	 * Find the shadow dqtrx for this transaction and dquot, if any deltas
	 * need to be applied here.  If not, we're finished early.
	 */
	mutex_lock(&xqc->lock);
	dqa = rhashtable_lookup_fast(&xqc->shadow_dquot_acct, &p->tx_id,
			xqcheck_dqacct_hash_params);
	if (!dqa)
		goto out_unlock;
	dqtrx = xqcheck_get_dqtrx(dqa, p->q_type, p->q_id);
	if (!dqtrx || dqtrx->q_type == 0)
		goto out_unlock;

	/* Update our shadow dquot if we're committing. */
	if (action == XFS_APPLY_DQTRX_COMMIT) {
		error = xqcheck_update_incore_counts(xqc, counts, p->q_id,
				dqtrx->icount_delta,
				dqtrx->bcount_delta + dqtrx->delbcnt_delta,
				dqtrx->rtbcount_delta + dqtrx->delrtb_delta);
		if (error)
			goto out_abort;
	}

	/* Free the shadow accounting structure if that was the last user. */
	dqa->refcount--;
	if (dqa->refcount == 0) {
		error = rhashtable_remove_fast(&xqc->shadow_dquot_acct,
				&dqa->hash, xqcheck_dqacct_hash_params);
		if (error)
			goto out_abort;
		xqcheck_dqacct_free(dqa, NULL);
	}

	mutex_unlock(&xqc->lock);
	return NOTIFY_DONE;

out_abort:
	xchk_iscan_abort(&xqc->iscan);
out_unlock:
	mutex_unlock(&xqc->lock);
	return NOTIFY_DONE;
}

/* Record this inode's quota usage in our shadow quota counter data. */
STATIC int
xqcheck_collect_inode(
	struct xqcheck		*xqc,
	struct xfs_inode	*ip)
{
	struct xfs_trans	*tp = xqc->sc->tp;
	xfs_filblks_t		nblks, rtblks;
	uint			ilock_flags = 0;
	xfs_dqid_t		id;
	bool			isreg = S_ISREG(VFS_I(ip)->i_mode);
	int			error = 0;

	if (xfs_is_metadir_inode(ip) ||
	    xfs_is_quota_inode(&tp->t_mountp->m_sb, ip->i_ino)) {
		/*
		 * Quota files are never counted towards quota, so we do not
		 * need to take the lock.  Files do not switch between the
		 * metadata and regular directory trees without a reallocation,
		 * so we do not need to ILOCK them either.
		 */
		xchk_iscan_mark_visited(&xqc->iscan, ip);
		return 0;
	}

	/* Figure out the data / rt device block counts. */
	xfs_ilock(ip, XFS_IOLOCK_SHARED);
	if (isreg)
		xfs_ilock(ip, XFS_MMAPLOCK_SHARED);
	if (XFS_IS_REALTIME_INODE(ip)) {
		/*
		 * Read in the data fork for rt files so that _count_blocks
		 * can count the number of blocks allocated from the rt volume.
		 * Inodes do not track that separately.
		 */
		ilock_flags = xfs_ilock_data_map_shared(ip);
		error = xfs_iread_extents(tp, ip, XFS_DATA_FORK);
		if (error)
			goto out_abort;
	} else {
		ilock_flags = XFS_ILOCK_SHARED;
		xfs_ilock(ip, XFS_ILOCK_SHARED);
	}
	xfs_inode_count_blocks(tp, ip, &nblks, &rtblks);

	if (xchk_iscan_aborted(&xqc->iscan)) {
		error = -ECANCELED;
		goto out_incomplete;
	}

	/* Update the shadow dquot counters. */
	mutex_lock(&xqc->lock);
	if (xqc->ucounts) {
		id = xfs_qm_id_for_quotatype(ip, XFS_DQTYPE_USER);
		error = xqcheck_update_incore_counts(xqc, xqc->ucounts, id, 1,
				nblks, rtblks);
		if (error)
			goto out_mutex;
	}

	if (xqc->gcounts) {
		id = xfs_qm_id_for_quotatype(ip, XFS_DQTYPE_GROUP);
		error = xqcheck_update_incore_counts(xqc, xqc->gcounts, id, 1,
				nblks, rtblks);
		if (error)
			goto out_mutex;
	}

	if (xqc->pcounts) {
		id = xfs_qm_id_for_quotatype(ip, XFS_DQTYPE_PROJ);
		error = xqcheck_update_incore_counts(xqc, xqc->pcounts, id, 1,
				nblks, rtblks);
		if (error)
			goto out_mutex;
	}
	mutex_unlock(&xqc->lock);

	xchk_iscan_mark_visited(&xqc->iscan, ip);
	goto out_ilock;

out_mutex:
	mutex_unlock(&xqc->lock);
out_abort:
	xchk_iscan_abort(&xqc->iscan);
out_incomplete:
	xchk_set_incomplete(xqc->sc);
out_ilock:
	xfs_iunlock(ip, ilock_flags);
	if (isreg)
		xfs_iunlock(ip, XFS_MMAPLOCK_SHARED);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return error;
}

/* Walk all the allocated inodes and run a quota scan on them. */
STATIC int
xqcheck_collect_counts(
	struct xqcheck		*xqc)
{
	struct xfs_scrub	*sc = xqc->sc;
	struct xfs_inode	*ip;
	int			error;

	/*
	 * Set up for a potentially lengthy filesystem scan by reducing our
	 * transaction resource usage for the duration.  Specifically:
	 *
	 * Cancel the transaction to release the log grant space while we scan
	 * the filesystem.
	 *
	 * Create a new empty transaction to eliminate the possibility of the
	 * inode scan deadlocking on cyclical metadata.
	 *
	 * We pass the empty transaction to the file scanning function to avoid
	 * repeatedly cycling empty transactions.  This can be done without
	 * risk of deadlock between sb_internal and the IOLOCK (we take the
	 * IOLOCK to quiesce the file before scanning) because empty
	 * transactions do not take sb_internal.
	 */
	xchk_trans_cancel(sc);
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	while ((error = xchk_iscan_iter(&xqc->iscan, &ip)) == 1) {
		error = xqcheck_collect_inode(xqc, ip);
		xchk_irele(sc, ip);
		if (error)
			break;

		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&xqc->iscan);
	if (error) {
		xchk_set_incomplete(sc);
		/*
		 * If we couldn't grab an inode that was busy with a state
		 * change, change the error code so that we exit to userspace
		 * as quickly as possible.
		 */
		if (error == -EBUSY)
			return -ECANCELED;
		return error;
	}

	/*
	 * Switch out for a real transaction in preparation for building a new
	 * tree.
	 */
	xchk_trans_cancel(sc);
	return xchk_setup_fs(sc);
}

/*
 * Part 2: Comparing dquot resource counters.  Walk each xfs_dquot, comparing
 * the resource usage counters against our shadow dquots; and then walk each
 * shadow dquot (that wasn't covered in the first part), comparing it against
 * the xfs_dquot.
 */

/*
 * Check the dquot data against what we observed.  Caller must hold the dquot
 * lock.
 */
STATIC int
xqcheck_compare_dquot(
	struct xqcheck		*xqc,
	xfs_dqtype_t		dqtype,
	struct xfs_dquot	*dq)
{
	struct xqcheck_dquot	xcdq;
	struct xfarray		*counts = xqcheck_counters_for(xqc, dqtype);
	int			error;

	if (xchk_iscan_aborted(&xqc->iscan)) {
		xchk_set_incomplete(xqc->sc);
		return -ECANCELED;
	}

	mutex_lock(&xqc->lock);
	error = xfarray_load_sparse(counts, dq->q_id, &xcdq);
	if (error)
		goto out_unlock;

	if (xcdq.icount != dq->q_ino.count)
		xchk_qcheck_set_corrupt(xqc->sc, dqtype, dq->q_id);

	if (xcdq.bcount != dq->q_blk.count)
		xchk_qcheck_set_corrupt(xqc->sc, dqtype, dq->q_id);

	if (xcdq.rtbcount != dq->q_rtb.count)
		xchk_qcheck_set_corrupt(xqc->sc, dqtype, dq->q_id);

	xcdq.flags |= (XQCHECK_DQUOT_COMPARE_SCANNED | XQCHECK_DQUOT_WRITTEN);
	error = xfarray_store(counts, dq->q_id, &xcdq);
	if (error == -EFBIG) {
		/*
		 * EFBIG means we tried to store data at too high a byte offset
		 * in the sparse array.  IOWs, we cannot complete the check and
		 * must notify userspace that the check was incomplete.  This
		 * should never happen outside of the collection phase.
		 */
		xchk_set_incomplete(xqc->sc);
		error = -ECANCELED;
	}
	mutex_unlock(&xqc->lock);
	if (error)
		return error;

	if (xqc->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return -ECANCELED;

	return 0;

out_unlock:
	mutex_unlock(&xqc->lock);
	return error;
}

/*
 * Walk all the observed dquots, and make sure there's a matching incore
 * dquot and that its counts match ours.
 */
STATIC int
xqcheck_walk_observations(
	struct xqcheck		*xqc,
	xfs_dqtype_t		dqtype)
{
	struct xqcheck_dquot	xcdq;
	struct xfs_dquot	*dq;
	struct xfarray		*counts = xqcheck_counters_for(xqc, dqtype);
	xfarray_idx_t		cur = XFARRAY_CURSOR_INIT;
	int			error;

	mutex_lock(&xqc->lock);
	while ((error = xfarray_iter(counts, &cur, &xcdq)) == 1) {
		xfs_dqid_t	id = cur - 1;

		if (xcdq.flags & XQCHECK_DQUOT_COMPARE_SCANNED)
			continue;

		mutex_unlock(&xqc->lock);

		error = xfs_qm_dqget(xqc->sc->mp, id, dqtype, false, &dq);
		if (error == -ENOENT) {
			xchk_qcheck_set_corrupt(xqc->sc, dqtype, id);
			return 0;
		}
		if (error)
			return error;

		error = xqcheck_compare_dquot(xqc, dqtype, dq);
		xfs_qm_dqput(dq);
		if (error)
			return error;

		if (xchk_should_terminate(xqc->sc, &error))
			return error;

		mutex_lock(&xqc->lock);
	}
	mutex_unlock(&xqc->lock);

	return error;
}

/* Compare the quota counters we observed against the live dquots. */
STATIC int
xqcheck_compare_dqtype(
	struct xqcheck		*xqc,
	xfs_dqtype_t		dqtype)
{
	struct xchk_dqiter	cursor = { };
	struct xfs_scrub	*sc = xqc->sc;
	struct xfs_dquot	*dq;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* If the quota CHKD flag is cleared, we need to repair this quota. */
	if (!(xfs_quota_chkd_flag(dqtype) & sc->mp->m_qflags)) {
		xchk_qcheck_set_corrupt(xqc->sc, dqtype, 0);
		return 0;
	}

	/* Compare what we observed against the actual dquots. */
	xchk_dqiter_init(&cursor, sc, dqtype);
	while ((error = xchk_dquot_iter(&cursor, &dq)) == 1) {
		error = xqcheck_compare_dquot(xqc, dqtype, dq);
		xfs_qm_dqput(dq);
		if (error)
			break;
	}
	if (error)
		return error;

	/* Walk all the observed dquots and compare to the incore ones. */
	return xqcheck_walk_observations(xqc, dqtype);
}

/* Tear down everything associated with a quotacheck. */
static void
xqcheck_teardown_scan(
	void			*priv)
{
	struct xqcheck		*xqc = priv;
	struct xfs_quotainfo	*qi = xqc->sc->mp->m_quotainfo;

	/* Discourage any hook functions that might be running. */
	xchk_iscan_abort(&xqc->iscan);

	/*
	 * As noted above, the apply hook is responsible for cleaning up the
	 * shadow dquot accounting data when a transaction completes.  The mod
	 * hook must be removed before the apply hook so that we don't
	 * mistakenly leave an active shadow account for the mod hook to get
	 * its hands on.  No hooks should be running after these functions
	 * return.
	 */
	xfs_dqtrx_hook_del(qi, &xqc->qhook);

	if (xqc->shadow_dquot_acct.key_len) {
		rhashtable_free_and_destroy(&xqc->shadow_dquot_acct,
				xqcheck_dqacct_free, NULL);
		xqc->shadow_dquot_acct.key_len = 0;
	}

	if (xqc->pcounts) {
		xfarray_destroy(xqc->pcounts);
		xqc->pcounts = NULL;
	}

	if (xqc->gcounts) {
		xfarray_destroy(xqc->gcounts);
		xqc->gcounts = NULL;
	}

	if (xqc->ucounts) {
		xfarray_destroy(xqc->ucounts);
		xqc->ucounts = NULL;
	}

	xchk_iscan_teardown(&xqc->iscan);
	mutex_destroy(&xqc->lock);
	xqc->sc = NULL;
}

/*
 * Scan all inodes in the entire filesystem to generate quota counter data.
 * If the scan is successful, the quota data will be left alive for a repair.
 * If any error occurs, we'll tear everything down.
 */
STATIC int
xqcheck_setup_scan(
	struct xfs_scrub	*sc,
	struct xqcheck		*xqc)
{
	char			*descr;
	struct xfs_quotainfo	*qi = sc->mp->m_quotainfo;
	unsigned long long	max_dquots = XFS_DQ_ID_MAX + 1ULL;
	int			error;

	ASSERT(xqc->sc == NULL);
	xqc->sc = sc;

	mutex_init(&xqc->lock);

	/* Retry iget every tenth of a second for up to 30 seconds. */
	xchk_iscan_start(sc, 30000, 100, &xqc->iscan);

	error = -ENOMEM;
	if (xfs_this_quota_on(sc->mp, XFS_DQTYPE_USER)) {
		descr = xchk_xfile_descr(sc, "user dquot records");
		error = xfarray_create(descr, max_dquots,
				sizeof(struct xqcheck_dquot), &xqc->ucounts);
		kfree(descr);
		if (error)
			goto out_teardown;
	}

	if (xfs_this_quota_on(sc->mp, XFS_DQTYPE_GROUP)) {
		descr = xchk_xfile_descr(sc, "group dquot records");
		error = xfarray_create(descr, max_dquots,
				sizeof(struct xqcheck_dquot), &xqc->gcounts);
		kfree(descr);
		if (error)
			goto out_teardown;
	}

	if (xfs_this_quota_on(sc->mp, XFS_DQTYPE_PROJ)) {
		descr = xchk_xfile_descr(sc, "project dquot records");
		error = xfarray_create(descr, max_dquots,
				sizeof(struct xqcheck_dquot), &xqc->pcounts);
		kfree(descr);
		if (error)
			goto out_teardown;
	}

	/*
	 * Set up hash table to map transactions to our internal shadow dqtrx
	 * structures.
	 */
	error = rhashtable_init(&xqc->shadow_dquot_acct,
			&xqcheck_dqacct_hash_params);
	if (error)
		goto out_teardown;

	/*
	 * Hook into the quota code.  The hook only triggers for inodes that
	 * were already scanned, and the scanner thread takes each inode's
	 * ILOCK, which means that any in-progress inode updates will finish
	 * before we can scan the inode.
	 *
	 * The apply hook (which removes the shadow dquot accounting struct)
	 * must be installed before the mod hook so that we never fail to catch
	 * the end of a quota update sequence and leave stale shadow data.
	 */
	ASSERT(sc->flags & XCHK_FSGATES_QUOTA);
	xfs_dqtrx_hook_setup(&xqc->qhook, xqcheck_mod_live_ino_dqtrx,
			xqcheck_apply_live_dqtrx);

	error = xfs_dqtrx_hook_add(qi, &xqc->qhook);
	if (error)
		goto out_teardown;

	/* Use deferred cleanup to pass the quota count data to repair. */
	sc->buf_cleanup = xqcheck_teardown_scan;
	return 0;

out_teardown:
	xqcheck_teardown_scan(xqc);
	return error;
}

/* Scrub all counters for a given quota type. */
int
xchk_quotacheck(
	struct xfs_scrub	*sc)
{
	struct xqcheck		*xqc = sc->buf;
	int			error = 0;

	/* Check quota counters on the live filesystem. */
	error = xqcheck_setup_scan(sc, xqc);
	if (error)
		return error;

	/* Walk all inodes, picking up quota information. */
	error = xqcheck_collect_counts(xqc);
	if (!xchk_xref_process_error(sc, 0, 0, &error))
		return error;

	/* Fail fast if we're not playing with a full dataset. */
	if (xchk_iscan_aborted(&xqc->iscan))
		xchk_set_incomplete(sc);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE)
		return 0;

	/* Compare quota counters. */
	if (xqc->ucounts) {
		error = xqcheck_compare_dqtype(xqc, XFS_DQTYPE_USER);
		if (!xchk_xref_process_error(sc, 0, 0, &error))
			return error;
	}
	if (xqc->gcounts) {
		error = xqcheck_compare_dqtype(xqc, XFS_DQTYPE_GROUP);
		if (!xchk_xref_process_error(sc, 0, 0, &error))
			return error;
	}
	if (xqc->pcounts) {
		error = xqcheck_compare_dqtype(xqc, XFS_DQTYPE_PROJ);
		if (!xchk_xref_process_error(sc, 0, 0, &error))
			return error;
	}

	/* Check one last time for an incomplete dataset. */
	if (xchk_iscan_aborted(&xqc->iscan))
		xchk_set_incomplete(sc);

	return 0;
}
