/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
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
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_quota.h"
#include "xfs_trans.h"
#include "xfs_qm.h"
#include "xfs_error.h"
#include "xfs_cksum.h"
#include "xfs_trace.h"

int
xfs_calc_dquots_per_chunk(
	unsigned int		nbblks)	/* basic block units */
{
	unsigned int	ndquots;

	ASSERT(nbblks > 0);
	ndquots = BBTOB(nbblks);
	do_div(ndquots, sizeof(xfs_dqblk_t));

	return ndquots;
}

/*
 * Do some primitive error checking on ondisk dquot data structures.
 */
int
xfs_dqcheck(
	struct xfs_mount *mp,
	xfs_disk_dquot_t *ddq,
	xfs_dqid_t	 id,
	uint		 type,	  /* used only when IO_dorepair is true */
	uint		 flags,
	char		 *str)
{
	xfs_dqblk_t	 *d = (xfs_dqblk_t *)ddq;
	int		errs = 0;

	/*
	 * We can encounter an uninitialized dquot buffer for 2 reasons:
	 * 1. If we crash while deleting the quotainode(s), and those blks got
	 *    used for user data. This is because we take the path of regular
	 *    file deletion; however, the size field of quotainodes is never
	 *    updated, so all the tricks that we play in itruncate_finish
	 *    don't quite matter.
	 *
	 * 2. We don't play the quota buffers when there's a quotaoff logitem.
	 *    But the allocation will be replayed so we'll end up with an
	 *    uninitialized quota block.
	 *
	 * This is all fine; things are still consistent, and we haven't lost
	 * any quota information. Just don't complain about bad dquot blks.
	 */
	if (ddq->d_magic != cpu_to_be16(XFS_DQUOT_MAGIC)) {
		if (flags & XFS_QMOPT_DOWARN)
			xfs_alert(mp,
			"%s : XFS dquot ID 0x%x, magic 0x%x != 0x%x",
			str, id, be16_to_cpu(ddq->d_magic), XFS_DQUOT_MAGIC);
		errs++;
	}
	if (ddq->d_version != XFS_DQUOT_VERSION) {
		if (flags & XFS_QMOPT_DOWARN)
			xfs_alert(mp,
			"%s : XFS dquot ID 0x%x, version 0x%x != 0x%x",
			str, id, ddq->d_version, XFS_DQUOT_VERSION);
		errs++;
	}

	if (ddq->d_flags != XFS_DQ_USER &&
	    ddq->d_flags != XFS_DQ_PROJ &&
	    ddq->d_flags != XFS_DQ_GROUP) {
		if (flags & XFS_QMOPT_DOWARN)
			xfs_alert(mp,
			"%s : XFS dquot ID 0x%x, unknown flags 0x%x",
			str, id, ddq->d_flags);
		errs++;
	}

	if (id != -1 && id != be32_to_cpu(ddq->d_id)) {
		if (flags & XFS_QMOPT_DOWARN)
			xfs_alert(mp,
			"%s : ondisk-dquot 0x%p, ID mismatch: "
			"0x%x expected, found id 0x%x",
			str, ddq, id, be32_to_cpu(ddq->d_id));
		errs++;
	}

	if (!errs && ddq->d_id) {
		if (ddq->d_blk_softlimit &&
		    be64_to_cpu(ddq->d_bcount) >
				be64_to_cpu(ddq->d_blk_softlimit)) {
			if (!ddq->d_btimer) {
				if (flags & XFS_QMOPT_DOWARN)
					xfs_alert(mp,
			"%s : Dquot ID 0x%x (0x%p) BLK TIMER NOT STARTED",
					str, (int)be32_to_cpu(ddq->d_id), ddq);
				errs++;
			}
		}
		if (ddq->d_ino_softlimit &&
		    be64_to_cpu(ddq->d_icount) >
				be64_to_cpu(ddq->d_ino_softlimit)) {
			if (!ddq->d_itimer) {
				if (flags & XFS_QMOPT_DOWARN)
					xfs_alert(mp,
			"%s : Dquot ID 0x%x (0x%p) INODE TIMER NOT STARTED",
					str, (int)be32_to_cpu(ddq->d_id), ddq);
				errs++;
			}
		}
		if (ddq->d_rtb_softlimit &&
		    be64_to_cpu(ddq->d_rtbcount) >
				be64_to_cpu(ddq->d_rtb_softlimit)) {
			if (!ddq->d_rtbtimer) {
				if (flags & XFS_QMOPT_DOWARN)
					xfs_alert(mp,
			"%s : Dquot ID 0x%x (0x%p) RTBLK TIMER NOT STARTED",
					str, (int)be32_to_cpu(ddq->d_id), ddq);
				errs++;
			}
		}
	}

	if (!errs || !(flags & XFS_QMOPT_DQREPAIR))
		return errs;

	if (flags & XFS_QMOPT_DOWARN)
		xfs_notice(mp, "Re-initializing dquot ID 0x%x", id);

	/*
	 * Typically, a repair is only requested by quotacheck.
	 */
	ASSERT(id != -1);
	ASSERT(flags & XFS_QMOPT_DQREPAIR);
	memset(d, 0, sizeof(xfs_dqblk_t));

	d->dd_diskdq.d_magic = cpu_to_be16(XFS_DQUOT_MAGIC);
	d->dd_diskdq.d_version = XFS_DQUOT_VERSION;
	d->dd_diskdq.d_flags = type;
	d->dd_diskdq.d_id = cpu_to_be32(id);

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		uuid_copy(&d->dd_uuid, &mp->m_sb.sb_uuid);
		xfs_update_cksum((char *)d, sizeof(struct xfs_dqblk),
				 XFS_DQUOT_CRC_OFF);
	}

	return errs;
}

STATIC bool
xfs_dquot_buf_verify_crc(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	struct xfs_dqblk	*d = (struct xfs_dqblk *)bp->b_addr;
	int			ndquots;
	int			i;

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return true;

	/*
	 * if we are in log recovery, the quota subsystem has not been
	 * initialised so we have no quotainfo structure. In that case, we need
	 * to manually calculate the number of dquots in the buffer.
	 */
	if (mp->m_quotainfo)
		ndquots = mp->m_quotainfo->qi_dqperchunk;
	else
		ndquots = xfs_calc_dquots_per_chunk(
					XFS_BB_TO_FSB(mp, bp->b_length));

	for (i = 0; i < ndquots; i++, d++) {
		if (!xfs_verify_cksum((char *)d, sizeof(struct xfs_dqblk),
				 XFS_DQUOT_CRC_OFF))
			return false;
		if (!uuid_equal(&d->dd_uuid, &mp->m_sb.sb_uuid))
			return false;
	}
	return true;
}

STATIC bool
xfs_dquot_buf_verify(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	struct xfs_dqblk	*d = (struct xfs_dqblk *)bp->b_addr;
	xfs_dqid_t		id = 0;
	int			ndquots;
	int			i;

	/*
	 * if we are in log recovery, the quota subsystem has not been
	 * initialised so we have no quotainfo structure. In that case, we need
	 * to manually calculate the number of dquots in the buffer.
	 */
	if (mp->m_quotainfo)
		ndquots = mp->m_quotainfo->qi_dqperchunk;
	else
		ndquots = xfs_calc_dquots_per_chunk(bp->b_length);

	/*
	 * On the first read of the buffer, verify that each dquot is valid.
	 * We don't know what the id of the dquot is supposed to be, just that
	 * they should be increasing monotonically within the buffer. If the
	 * first id is corrupt, then it will fail on the second dquot in the
	 * buffer so corruptions could point to the wrong dquot in this case.
	 */
	for (i = 0; i < ndquots; i++) {
		struct xfs_disk_dquot	*ddq;
		int			error;

		ddq = &d[i].dd_diskdq;

		if (i == 0)
			id = be32_to_cpu(ddq->d_id);

		error = xfs_dqcheck(mp, ddq, id + i, 0, XFS_QMOPT_DOWARN,
				       "xfs_dquot_buf_verify");
		if (error)
			return false;
	}
	return true;
}

static void
xfs_dquot_buf_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;

	if (!xfs_dquot_buf_verify_crc(mp, bp))
		xfs_buf_ioerror(bp, EFSBADCRC);
	else if (!xfs_dquot_buf_verify(mp, bp))
		xfs_buf_ioerror(bp, EFSCORRUPTED);

	if (bp->b_error)
		xfs_verifier_error(bp);
}

/*
 * we don't calculate the CRC here as that is done when the dquot is flushed to
 * the buffer after the update is done. This ensures that the dquot in the
 * buffer always has an up-to-date CRC value.
 */
static void
xfs_dquot_buf_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount	*mp = bp->b_target->bt_mount;

	if (!xfs_dquot_buf_verify(mp, bp)) {
		xfs_buf_ioerror(bp, EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}
}

const struct xfs_buf_ops xfs_dquot_buf_ops = {
	.verify_read = xfs_dquot_buf_read_verify,
	.verify_write = xfs_dquot_buf_write_verify,
};

