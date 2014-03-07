/*
 * Copyright (c) 2013 Jie Liu.
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
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_ag.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_trans_space.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr_leaf.h"
#include "xfs_bmap_btree.h"

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

	size = xfs_attr_leaf_entsize_local_max(mp->m_sb.sb_blocksize) -
	       MAXNAMELEN - 1;
	nblks = XFS_DAENTER_SPACE_RES(mp, XFS_ATTR_FORK);
	nblks += XFS_B_TO_FSB(mp, size);
	nblks += XFS_NEXTENTADD_SPACE_RES(mp, size, XFS_ATTR_FORK);

	return  M_RES(mp)->tr_attrsetm.tr_logres +
		M_RES(mp)->tr_attrsetrt.tr_logres * nblks;
}

/*
 * Iterate over the log space reservation table to figure out and return
 * the maximum one in terms of the pre-calculated values which were done
 * at mount time.
 */
STATIC void
xfs_log_get_max_trans_res(
	struct xfs_mount	*mp,
	struct xfs_trans_res	*max_resp)
{
	struct xfs_trans_res	*resp;
	struct xfs_trans_res	*end_resp;
	int			log_space = 0;
	int			attr_space;

	attr_space = xfs_log_calc_max_attrsetm_res(mp);

	resp = (struct xfs_trans_res *)M_RES(mp);
	end_resp = (struct xfs_trans_res *)(M_RES(mp) + 1);
	for (; resp < end_resp; resp++) {
		int		tmp = resp->tr_logcount > 1 ?
				      resp->tr_logres * resp->tr_logcount :
				      resp->tr_logres;
		if (log_space < tmp) {
			log_space = tmp;
			*max_resp = *resp;		/* struct copy */
		}
	}

	if (attr_space > log_space) {
		*max_resp = M_RES(mp)->tr_attrsetm;	/* struct copy */
		max_resp->tr_logres = attr_space;
	}
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

	if (xfs_sb_version_haslogv2(&mp->m_sb) && mp->m_sb.sb_logsunit > 1)
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
