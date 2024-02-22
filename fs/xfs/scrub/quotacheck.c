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

/* Set us up to scrub quota counters. */
int
xchk_setup_quotacheck(
	struct xfs_scrub	*sc)
{
	/* Not ready for general consumption yet. */
	return -EOPNOTSUPP;

	if (!XFS_IS_QUOTA_ON(sc->mp))
		return -ENOENT;

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
 * xchk_*_process_error.
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

	if (xfs_is_quota_inode(&tp->t_mountp->m_sb, ip->i_ino)) {
		/*
		 * Quota files are never counted towards quota, so we do not
		 * need to take the lock.
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
			goto out_incomplete;
	} else {
		ilock_flags = XFS_ILOCK_SHARED;
		xfs_ilock(ip, XFS_ILOCK_SHARED);
	}
	xfs_inode_count_blocks(tp, ip, &nblks, &rtblks);

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
		 * should never happen, since we just read the record.
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

	return 0;
}
