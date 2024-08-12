// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013 Jie Liu.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_trans_space.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_trace.h"

/*
 * Shortly after enabling the large extents count feature in 2023, longstanding
 * bugs were found in the code that computes the minimum log size.  Luckily,
 * the bugs resulted in over-estimates of that size, so there's no impact to
 * existing users.  However, we don't want to reduce the minimum log size
 * because that can create the situation where a newer mkfs writes a new
 * filesystem that an older kernel won't mount.
 *
 * Several years prior, we also discovered that the transaction reservations
 * for rmap and reflink operations were unnecessarily large.  That was fixed,
 * but the minimum log size computation was left alone to avoid the
 * compatibility problems noted above.  Fix that too.
 *
 * Therefore, we only may correct the computation starting with filesystem
 * features that didn't exist in 2023.  In other words, only turn this on if
 * the filesystem has parent pointers.
 *
 * This function can be called before the XFS_HAS_* flags have been set up,
 * (e.g. mkfs) so we must check the ondisk superblock.
 */
static inline bool
xfs_want_minlogsize_fixes(
	struct xfs_sb	*sb)
{
	return xfs_sb_is_v5(sb) &&
	       xfs_sb_has_incompat_feature(sb, XFS_SB_FEAT_INCOMPAT_PARENT);
}

/*
 * Calculate the maximum length in bytes that would be required for a local
 * attribute value as large attributes out of line are not logged.
 */
STATIC int
xfs_log_calc_max_attrsetm_res(
	struct xfs_mount	*mp)
{
	int			size;
	int			nblks;

	size = xfs_attr_leaf_entsize_local_max(mp->m_attr_geo->blksize) -
	       MAXNAMELEN - 1;
	nblks = XFS_DAENTER_SPACE_RES(mp, XFS_ATTR_FORK);
	nblks += XFS_B_TO_FSB(mp, size);

	/*
	 * If the feature set is new enough, correct a unit conversion error in
	 * the xattr transaction reservation code that resulted in oversized
	 * minimum log size computations.
	 */
	if (xfs_want_minlogsize_fixes(&mp->m_sb))
		size = XFS_B_TO_FSB(mp, size);

	nblks += XFS_NEXTENTADD_SPACE_RES(mp, size, XFS_ATTR_FORK);

	return  M_RES(mp)->tr_attrsetm.tr_logres +
		M_RES(mp)->tr_attrsetrt.tr_logres * nblks;
}

/*
 * Compute an alternate set of log reservation sizes for use exclusively with
 * minimum log size calculations.
 */
static void
xfs_log_calc_trans_resv_for_minlogblocks(
	struct xfs_mount	*mp,
	struct xfs_trans_resv	*resv)
{
	unsigned int		rmap_maxlevels = mp->m_rmap_maxlevels;

	/*
	 * If the feature set is new enough, drop the oversized minimum log
	 * size computation introduced by the original reflink code.
	 */
	if (xfs_want_minlogsize_fixes(&mp->m_sb)) {
		xfs_trans_resv_calc(mp, resv);
		return;
	}

	/*
	 * In the early days of rmap+reflink, we always set the rmap maxlevels
	 * to 9 even if the AG was small enough that it would never grow to
	 * that height.  Transaction reservation sizes influence the minimum
	 * log size calculation, which influences the size of the log that mkfs
	 * creates.  Use the old value here to ensure that newly formatted
	 * small filesystems will mount on older kernels.
	 */
	if (xfs_has_rmapbt(mp) && xfs_has_reflink(mp))
		mp->m_rmap_maxlevels = XFS_OLD_REFLINK_RMAP_MAXLEVELS;

	xfs_trans_resv_calc(mp, resv);

	if (xfs_has_reflink(mp)) {
		/*
		 * In the early days of reflink, typical log operation counts
		 * were greatly overestimated.
		 */
		resv->tr_write.tr_logcount = XFS_WRITE_LOG_COUNT_REFLINK;
		resv->tr_itruncate.tr_logcount =
				XFS_ITRUNCATE_LOG_COUNT_REFLINK;
		resv->tr_qm_dqalloc.tr_logcount = XFS_WRITE_LOG_COUNT_REFLINK;
	} else if (xfs_has_rmapbt(mp)) {
		/*
		 * In the early days of non-reflink rmap, the impact of rmapbt
		 * updates on log counts were not taken into account at all.
		 */
		resv->tr_write.tr_logcount = XFS_WRITE_LOG_COUNT;
		resv->tr_itruncate.tr_logcount = XFS_ITRUNCATE_LOG_COUNT;
		resv->tr_qm_dqalloc.tr_logcount = XFS_WRITE_LOG_COUNT;
	}

	/*
	 * In the early days of reflink, we did not use deferred refcount
	 * update log items, so log reservations must be recomputed using the
	 * old calculations.
	 */
	resv->tr_write.tr_logres =
			xfs_calc_write_reservation_minlogsize(mp);
	resv->tr_itruncate.tr_logres =
			xfs_calc_itruncate_reservation_minlogsize(mp);
	resv->tr_qm_dqalloc.tr_logres =
			xfs_calc_qm_dqalloc_reservation_minlogsize(mp);

	/* Put everything back the way it was.  This goes at the end. */
	mp->m_rmap_maxlevels = rmap_maxlevels;
}

/*
 * Iterate over the log space reservation table to figure out and return
 * the maximum one in terms of the pre-calculated values which were done
 * at mount time.
 */
void
xfs_log_get_max_trans_res(
	struct xfs_mount	*mp,
	struct xfs_trans_res	*max_resp)
{
	struct xfs_trans_resv	resv = {};
	struct xfs_trans_res	*resp;
	struct xfs_trans_res	*end_resp;
	unsigned int		i;
	int			log_space = 0;
	int			attr_space;

	attr_space = xfs_log_calc_max_attrsetm_res(mp);

	xfs_log_calc_trans_resv_for_minlogblocks(mp, &resv);

	resp = (struct xfs_trans_res *)&resv;
	end_resp = (struct xfs_trans_res *)(&resv + 1);
	for (i = 0; resp < end_resp; i++, resp++) {
		int		tmp = resp->tr_logcount > 1 ?
				      resp->tr_logres * resp->tr_logcount :
				      resp->tr_logres;

		trace_xfs_trans_resv_calc_minlogsize(mp, i, resp);
		if (log_space < tmp) {
			log_space = tmp;
			*max_resp = *resp;		/* struct copy */
		}
	}

	if (attr_space > log_space) {
		*max_resp = resv.tr_attrsetm;	/* struct copy */
		max_resp->tr_logres = attr_space;
	}
	trace_xfs_log_get_max_trans_res(mp, max_resp);
}

/*
 * Calculate the minimum valid log size for the given superblock configuration.
 * Used to calculate the minimum log size at mkfs time, and to determine if
 * the log is large enough or not at mount time. Returns the minimum size in
 * filesystem block size units.
 */
int
xfs_log_calc_minimum_size(
	struct xfs_mount	*mp)
{
	struct xfs_trans_res	tres = {0};
	int			max_logres;
	int			min_logblks = 0;
	int			lsunit = 0;

	xfs_log_get_max_trans_res(mp, &tres);

	max_logres = xfs_log_calc_unit_res(mp, tres.tr_logres);
	if (tres.tr_logcount > 1)
		max_logres *= tres.tr_logcount;

	if (xfs_has_logv2(mp) && mp->m_sb.sb_logsunit > 1)
		lsunit = BTOBB(mp->m_sb.sb_logsunit);

	/*
	 * Two factors should be taken into account for calculating the minimum
	 * log space.
	 * 1) The fundamental limitation is that no single transaction can be
	 *    larger than half size of the log.
	 *
	 *    From mkfs.xfs, this is considered by the XFS_MIN_LOG_FACTOR
	 *    define, which is set to 3. That means we can definitely fit
	 *    maximally sized 2 transactions in the log. We'll use this same
	 *    value here.
	 *
	 * 2) If the lsunit option is specified, a transaction requires 2 LSU
	 *    for the reservation because there are two log writes that can
	 *    require padding - the transaction data and the commit record which
	 *    are written separately and both can require padding to the LSU.
	 *    Consider that we can have an active CIL reservation holding 2*LSU,
	 *    but the CIL is not over a push threshold, in this case, if we
	 *    don't have enough log space for at one new transaction, which
	 *    includes another 2*LSU in the reservation, we will run into dead
	 *    loop situation in log space grant procedure. i.e.
	 *    xlog_grant_head_wait().
	 *
	 *    Hence the log size needs to be able to contain two maximally sized
	 *    and padded transactions, which is (2 * (2 * LSU + maxlres)).
	 *
	 * Also, the log size should be a multiple of the log stripe unit, round
	 * it up to lsunit boundary if lsunit is specified.
	 */
	if (lsunit) {
		min_logblks = roundup_64(BTOBB(max_logres), lsunit) +
			      2 * lsunit;
	} else
		min_logblks = BTOBB(max_logres) + 2 * BBSIZE;
	min_logblks *= XFS_MIN_LOG_FACTOR;

	return XFS_BB_TO_FSB(mp, min_logblks);
}
