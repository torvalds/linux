/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_inode_fork.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_dquot.h"
#include "xfs_dquot_item.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"

/* Convert a scrub type code to a DQ flag, or return 0 if error. */
static inline uint
xfs_scrub_quota_to_dqtype(
	struct xfs_scrub_context	*sc)
{
	switch (sc->sm->sm_type) {
	case XFS_SCRUB_TYPE_UQUOTA:
		return XFS_DQ_USER;
	case XFS_SCRUB_TYPE_GQUOTA:
		return XFS_DQ_GROUP;
	case XFS_SCRUB_TYPE_PQUOTA:
		return XFS_DQ_PROJ;
	default:
		return 0;
	}
}

/* Set us up to scrub a quota. */
int
xfs_scrub_setup_quota(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	uint				dqtype;

	/*
	 * If userspace gave us an AG number or inode data, they don't
	 * know what they're doing.  Get out.
	 */
	if (sc->sm->sm_agno || sc->sm->sm_ino || sc->sm->sm_gen)
		return -EINVAL;

	dqtype = xfs_scrub_quota_to_dqtype(sc);
	if (dqtype == 0)
		return -EINVAL;
	if (!xfs_this_quota_on(sc->mp, dqtype))
		return -ENOENT;
	return 0;
}

/* Quotas. */

/* Scrub the fields in an individual quota item. */
STATIC void
xfs_scrub_quota_item(
	struct xfs_scrub_context	*sc,
	uint				dqtype,
	struct xfs_dquot		*dq,
	xfs_dqid_t			id)
{
	struct xfs_mount		*mp = sc->mp;
	struct xfs_disk_dquot		*d = &dq->q_core;
	struct xfs_quotainfo		*qi = mp->m_quotainfo;
	xfs_fileoff_t			offset;
	unsigned long long		bsoft;
	unsigned long long		isoft;
	unsigned long long		rsoft;
	unsigned long long		bhard;
	unsigned long long		ihard;
	unsigned long long		rhard;
	unsigned long long		bcount;
	unsigned long long		icount;
	unsigned long long		rcount;
	xfs_ino_t			fs_icount;

	offset = id / qi->qi_dqperchunk;

	/*
	 * We fed $id and DQNEXT into the xfs_qm_dqget call, which means
	 * that the actual dquot we got must either have the same id or
	 * the next higher id.
	 */
	if (id > be32_to_cpu(d->d_id))
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	/* Did we get the dquot type we wanted? */
	if (dqtype != (d->d_flags & XFS_DQ_ALLTYPES))
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	if (d->d_pad0 != cpu_to_be32(0) || d->d_pad != cpu_to_be16(0))
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	/* Check the limits. */
	bhard = be64_to_cpu(d->d_blk_hardlimit);
	ihard = be64_to_cpu(d->d_ino_hardlimit);
	rhard = be64_to_cpu(d->d_rtb_hardlimit);

	bsoft = be64_to_cpu(d->d_blk_softlimit);
	isoft = be64_to_cpu(d->d_ino_softlimit);
	rsoft = be64_to_cpu(d->d_rtb_softlimit);

	/*
	 * Warn if the hard limits are larger than the fs.
	 * Administrators can do this, though in production this seems
	 * suspect, which is why we flag it for review.
	 *
	 * Complain about corruption if the soft limit is greater than
	 * the hard limit.
	 */
	if (bhard > mp->m_sb.sb_dblocks)
		xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK, offset);
	if (bsoft > bhard)
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	if (ihard > mp->m_maxicount)
		xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK, offset);
	if (isoft > ihard)
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	if (rhard > mp->m_sb.sb_rblocks)
		xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK, offset);
	if (rsoft > rhard)
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	/* Check the resource counts. */
	bcount = be64_to_cpu(d->d_bcount);
	icount = be64_to_cpu(d->d_icount);
	rcount = be64_to_cpu(d->d_rtbcount);
	fs_icount = percpu_counter_sum(&mp->m_icount);

	/*
	 * Check that usage doesn't exceed physical limits.  However, on
	 * a reflink filesystem we're allowed to exceed physical space
	 * if there are no quota limits.
	 */
	if (xfs_sb_version_hasreflink(&mp->m_sb)) {
		if (mp->m_sb.sb_dblocks < bcount)
			xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK,
					offset);
	} else {
		if (mp->m_sb.sb_dblocks < bcount)
			xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK,
					offset);
	}
	if (icount > fs_icount || rcount > mp->m_sb.sb_rblocks)
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, offset);

	/*
	 * We can violate the hard limits if the admin suddenly sets a
	 * lower limit than the actual usage.  However, we flag it for
	 * admin review.
	 */
	if (id != 0 && bhard != 0 && bcount > bhard)
		xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK, offset);
	if (id != 0 && ihard != 0 && icount > ihard)
		xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK, offset);
	if (id != 0 && rhard != 0 && rcount > rhard)
		xfs_scrub_fblock_set_warning(sc, XFS_DATA_FORK, offset);
}

/* Scrub all of a quota type's items. */
int
xfs_scrub_quota(
	struct xfs_scrub_context	*sc)
{
	struct xfs_bmbt_irec		irec = { 0 };
	struct xfs_mount		*mp = sc->mp;
	struct xfs_inode		*ip;
	struct xfs_quotainfo		*qi = mp->m_quotainfo;
	struct xfs_dquot		*dq;
	xfs_fileoff_t			max_dqid_off;
	xfs_fileoff_t			off = 0;
	xfs_dqid_t			id = 0;
	uint				dqtype;
	int				nimaps;
	int				error = 0;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return -ENOENT;

	mutex_lock(&qi->qi_quotaofflock);
	dqtype = xfs_scrub_quota_to_dqtype(sc);
	if (!xfs_this_quota_on(sc->mp, dqtype)) {
		error = -ENOENT;
		goto out_unlock_quota;
	}

	/* Attach to the quota inode and set sc->ip so that reporting works. */
	ip = xfs_quota_inode(sc->mp, dqtype);
	sc->ip = ip;

	/* Look for problem extents. */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		xfs_scrub_ino_set_corrupt(sc, sc->ip->i_ino, NULL);
		goto out_unlock_inode;
	}
	max_dqid_off = ((xfs_dqid_t)-1) / qi->qi_dqperchunk;
	while (1) {
		if (xfs_scrub_should_terminate(sc, &error))
			break;

		off = irec.br_startoff + irec.br_blockcount;
		nimaps = 1;
		error = xfs_bmapi_read(ip, off, -1, &irec, &nimaps,
				XFS_BMAPI_ENTIRE);
		if (!xfs_scrub_fblock_process_error(sc, XFS_DATA_FORK, off,
				&error))
			goto out_unlock_inode;
		if (!nimaps)
			break;
		if (irec.br_startblock == HOLESTARTBLOCK)
			continue;

		/* Check the extent record doesn't point to crap. */
		if (irec.br_startblock + irec.br_blockcount <=
		    irec.br_startblock)
			xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK,
					irec.br_startoff);
		if (!xfs_verify_fsbno(mp, irec.br_startblock) ||
		    !xfs_verify_fsbno(mp, irec.br_startblock +
					irec.br_blockcount - 1))
			xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK,
					irec.br_startoff);

		/*
		 * Unwritten extents or blocks mapped above the highest
		 * quota id shouldn't happen.
		 */
		if (isnullstartblock(irec.br_startblock) ||
		    irec.br_startoff > max_dqid_off ||
		    irec.br_startoff + irec.br_blockcount > max_dqid_off + 1)
			xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, off);
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Check all the quota items. */
	while (id < ((xfs_dqid_t)-1ULL)) {
		if (xfs_scrub_should_terminate(sc, &error))
			break;

		error = xfs_qm_dqget(mp, NULL, id, dqtype, XFS_QMOPT_DQNEXT,
				&dq);
		if (error == -ENOENT)
			break;
		if (!xfs_scrub_fblock_process_error(sc, XFS_DATA_FORK,
				id * qi->qi_dqperchunk, &error))
			break;

		xfs_scrub_quota_item(sc, dqtype, dq, id);

		id = be32_to_cpu(dq->q_core.d_id) + 1;
		xfs_qm_dqput(dq);
		if (!id)
			break;
	}

out:
	/* We set sc->ip earlier, so make sure we clear it now. */
	sc->ip = NULL;
out_unlock_quota:
	mutex_unlock(&qi->qi_quotaofflock);
	return error;

out_unlock_inode:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	goto out;
}
