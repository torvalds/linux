// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
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
#include "xfs_format.h"
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
#include "xfs_reflink.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/quota.h"
#include "scrub/trace.h"
#include "scrub/repair.h"

/*
 * Quota Repair
 * ============
 *
 * Quota repairs are fairly simplistic; we fix everything that the dquot
 * verifiers complain about, cap any counters or limits that make no sense,
 * and schedule a quotacheck if we had to fix anything.  We also repair any
 * data fork extent records that don't apply to metadata files.
 */

struct xrep_quota_info {
	struct xfs_scrub	*sc;
	bool			need_quotacheck;
};

/*
 * Allocate a new block into a sparse hole in the quota file backing this
 * dquot, initialize the block, and commit the whole mess.
 */
STATIC int
xrep_quota_item_fill_bmap_hole(
	struct xfs_scrub	*sc,
	struct xfs_dquot	*dq,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_buf		*bp;
	struct xfs_mount	*mp = sc->mp;
	int			nmaps = 1;
	int			error;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	/* Map a block into the file. */
	error = xfs_trans_reserve_more(sc->tp, XFS_QM_DQALLOC_SPACE_RES(mp),
			0);
	if (error)
		return error;

	error = xfs_bmapi_write(sc->tp, sc->ip, dq->q_fileoffset,
			XFS_DQUOT_CLUSTER_SIZE_FSB, XFS_BMAPI_METADATA, 0,
			irec, &nmaps);
	if (error)
		return error;
	if (nmaps != 1)
		return -ENOSPC;

	dq->q_blkno = XFS_FSB_TO_DADDR(mp, irec->br_startblock);

	trace_xrep_dquot_item_fill_bmap_hole(sc->mp, dq->q_type, dq->q_id);

	/* Initialize the new block. */
	error = xfs_trans_get_buf(sc->tp, mp->m_ddev_targp, dq->q_blkno,
			mp->m_quotainfo->qi_dqchunklen, 0, &bp);
	if (error)
		return error;
	bp->b_ops = &xfs_dquot_buf_ops;

	xfs_qm_init_dquot_blk(sc->tp, dq->q_id, dq->q_type, bp);
	xfs_buf_set_ref(bp, XFS_DQUOT_REF);

	/*
	 * Finish the mapping transactions and roll one more time to
	 * disconnect sc->ip from sc->tp.
	 */
	error = xrep_defer_finish(sc);
	if (error)
		return error;
	return xfs_trans_roll(&sc->tp);
}

/* Make sure there's a written block backing this dquot */
STATIC int
xrep_quota_item_bmap(
	struct xfs_scrub	*sc,
	struct xfs_dquot	*dq,
	bool			*dirty)
{
	struct xfs_bmbt_irec	irec;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_quotainfo	*qi = mp->m_quotainfo;
	xfs_fileoff_t		offset = dq->q_id / qi->qi_dqperchunk;
	int			nmaps = 1;
	int			error;

	/* The computed file offset should always be valid. */
	if (!xfs_verify_fileoff(mp, offset)) {
		ASSERT(xfs_verify_fileoff(mp, offset));
		return -EFSCORRUPTED;
	}
	dq->q_fileoffset = offset;

	error = xfs_bmapi_read(sc->ip, offset, 1, &irec, &nmaps, 0);
	if (error)
		return error;

	if (nmaps < 1 || !xfs_bmap_is_real_extent(&irec)) {
		/* Hole/delalloc extent; allocate a real block. */
		error = xrep_quota_item_fill_bmap_hole(sc, dq, &irec);
		if (error)
			return error;
	} else if (irec.br_state != XFS_EXT_NORM) {
		/* Unwritten extent, which we already took care of? */
		ASSERT(irec.br_state == XFS_EXT_NORM);
		return -EFSCORRUPTED;
	} else if (dq->q_blkno != XFS_FSB_TO_DADDR(mp, irec.br_startblock)) {
		/*
		 * If the cached daddr is incorrect, repair probably punched a
		 * hole out of the quota file and filled it back in with a new
		 * block.  Update the block mapping in the dquot.
		 */
		dq->q_blkno = XFS_FSB_TO_DADDR(mp, irec.br_startblock);
	}

	*dirty = true;
	return 0;
}

/* Reset quota timers if incorrectly set. */
static inline void
xrep_quota_item_timer(
	struct xfs_scrub		*sc,
	const struct xfs_dquot_res	*res,
	bool				*dirty)
{
	if ((res->softlimit && res->count > res->softlimit) ||
	    (res->hardlimit && res->count > res->hardlimit)) {
		if (!res->timer)
			*dirty = true;
	} else {
		if (res->timer)
			*dirty = true;
	}
}

/* Scrub the fields in an individual quota item. */
STATIC int
xrep_quota_item(
	struct xrep_quota_info	*rqi,
	struct xfs_dquot	*dq)
{
	struct xfs_scrub	*sc = rqi->sc;
	struct xfs_mount	*mp = sc->mp;
	xfs_ino_t		fs_icount;
	bool			dirty = false;
	int			error = 0;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	/*
	 * We might need to fix holes in the bmap record for the storage
	 * backing this dquot, so we need to lock the dquot and the quota file.
	 * dqiterate gave us a locked dquot, so drop the dquot lock to get the
	 * ILOCK_EXCL.
	 */
	xfs_dqunlock(dq);
	xchk_ilock(sc, XFS_ILOCK_EXCL);
	xfs_dqlock(dq);

	error = xrep_quota_item_bmap(sc, dq, &dirty);
	xchk_iunlock(sc, XFS_ILOCK_EXCL);
	if (error)
		return error;

	/* Check the limits. */
	if (dq->q_blk.softlimit > dq->q_blk.hardlimit) {
		dq->q_blk.softlimit = dq->q_blk.hardlimit;
		dirty = true;
	}

	if (dq->q_ino.softlimit > dq->q_ino.hardlimit) {
		dq->q_ino.softlimit = dq->q_ino.hardlimit;
		dirty = true;
	}

	if (dq->q_rtb.softlimit > dq->q_rtb.hardlimit) {
		dq->q_rtb.softlimit = dq->q_rtb.hardlimit;
		dirty = true;
	}

	/*
	 * Check that usage doesn't exceed physical limits.  However, on
	 * a reflink filesystem we're allowed to exceed physical space
	 * if there are no quota limits.  We don't know what the real number
	 * is, but we can make quotacheck find out for us.
	 */
	if (!xfs_has_reflink(mp) && dq->q_blk.count > mp->m_sb.sb_dblocks) {
		dq->q_blk.reserved -= dq->q_blk.count;
		dq->q_blk.reserved += mp->m_sb.sb_dblocks;
		dq->q_blk.count = mp->m_sb.sb_dblocks;
		rqi->need_quotacheck = true;
		dirty = true;
	}
	fs_icount = percpu_counter_sum(&mp->m_icount);
	if (dq->q_ino.count > fs_icount) {
		dq->q_ino.reserved -= dq->q_ino.count;
		dq->q_ino.reserved += fs_icount;
		dq->q_ino.count = fs_icount;
		rqi->need_quotacheck = true;
		dirty = true;
	}
	if (dq->q_rtb.count > mp->m_sb.sb_rblocks) {
		dq->q_rtb.reserved -= dq->q_rtb.count;
		dq->q_rtb.reserved += mp->m_sb.sb_rblocks;
		dq->q_rtb.count = mp->m_sb.sb_rblocks;
		rqi->need_quotacheck = true;
		dirty = true;
	}

	xrep_quota_item_timer(sc, &dq->q_blk, &dirty);
	xrep_quota_item_timer(sc, &dq->q_ino, &dirty);
	xrep_quota_item_timer(sc, &dq->q_rtb, &dirty);

	if (!dirty)
		return 0;

	trace_xrep_dquot_item(sc->mp, dq->q_type, dq->q_id);

	dq->q_flags |= XFS_DQFLAG_DIRTY;
	xfs_trans_dqjoin(sc->tp, dq);
	if (dq->q_id) {
		xfs_qm_adjust_dqlimits(dq);
		xfs_qm_adjust_dqtimers(dq);
	}
	xfs_trans_log_dquot(sc->tp, dq);
	error = xfs_trans_roll(&sc->tp);
	xfs_dqlock(dq);
	return error;
}

/* Fix a quota timer so that we can pass the verifier. */
STATIC void
xrep_quota_fix_timer(
	struct xfs_mount	*mp,
	const struct xfs_disk_dquot *ddq,
	__be64			softlimit,
	__be64			countnow,
	__be32			*timer,
	time64_t		timelimit)
{
	uint64_t		soft = be64_to_cpu(softlimit);
	uint64_t		count = be64_to_cpu(countnow);
	time64_t		new_timer;
	uint32_t		t;

	if (!soft || count <= soft || *timer != 0)
		return;

	new_timer = xfs_dquot_set_timeout(mp,
				ktime_get_real_seconds() + timelimit);
	if (ddq->d_type & XFS_DQTYPE_BIGTIME)
		t = xfs_dq_unix_to_bigtime(new_timer);
	else
		t = new_timer;

	*timer = cpu_to_be32(t);
}

/* Fix anything the verifiers complain about. */
STATIC int
xrep_quota_block(
	struct xfs_scrub	*sc,
	xfs_daddr_t		daddr,
	xfs_dqtype_t		dqtype,
	xfs_dqid_t		id)
{
	struct xfs_dqblk	*dqblk;
	struct xfs_disk_dquot	*ddq;
	struct xfs_quotainfo	*qi = sc->mp->m_quotainfo;
	struct xfs_def_quota	*defq = xfs_get_defquota(qi, dqtype);
	struct xfs_buf		*bp = NULL;
	enum xfs_blft		buftype = 0;
	int			i;
	int			error;

	error = xfs_trans_read_buf(sc->mp, sc->tp, sc->mp->m_ddev_targp, daddr,
			qi->qi_dqchunklen, 0, &bp, &xfs_dquot_buf_ops);
	switch (error) {
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Failed verifier, retry read with no ops. */
		error = xfs_trans_read_buf(sc->mp, sc->tp,
				sc->mp->m_ddev_targp, daddr, qi->qi_dqchunklen,
				0, &bp, NULL);
		if (error)
			return error;
		break;
	case 0:
		dqblk = bp->b_addr;
		ddq = &dqblk[0].dd_diskdq;

		/*
		 * If there's nothing that would impede a dqiterate, we're
		 * done.
		 */
		if ((ddq->d_type & XFS_DQTYPE_REC_MASK) != dqtype ||
		    id == be32_to_cpu(ddq->d_id)) {
			xfs_trans_brelse(sc->tp, bp);
			return 0;
		}
		break;
	default:
		return error;
	}

	/* Something's wrong with the block, fix the whole thing. */
	dqblk = bp->b_addr;
	bp->b_ops = &xfs_dquot_buf_ops;
	for (i = 0; i < qi->qi_dqperchunk; i++, dqblk++) {
		ddq = &dqblk->dd_diskdq;

		trace_xrep_disk_dquot(sc->mp, dqtype, id + i);

		ddq->d_magic = cpu_to_be16(XFS_DQUOT_MAGIC);
		ddq->d_version = XFS_DQUOT_VERSION;
		ddq->d_type = dqtype;
		ddq->d_id = cpu_to_be32(id + i);

		if (xfs_has_bigtime(sc->mp) && ddq->d_id)
			ddq->d_type |= XFS_DQTYPE_BIGTIME;

		xrep_quota_fix_timer(sc->mp, ddq, ddq->d_blk_softlimit,
				ddq->d_bcount, &ddq->d_btimer,
				defq->blk.time);

		xrep_quota_fix_timer(sc->mp, ddq, ddq->d_ino_softlimit,
				ddq->d_icount, &ddq->d_itimer,
				defq->ino.time);

		xrep_quota_fix_timer(sc->mp, ddq, ddq->d_rtb_softlimit,
				ddq->d_rtbcount, &ddq->d_rtbtimer,
				defq->rtb.time);

		/* We only support v5 filesystems so always set these. */
		uuid_copy(&dqblk->dd_uuid, &sc->mp->m_sb.sb_meta_uuid);
		xfs_update_cksum((char *)dqblk, sizeof(struct xfs_dqblk),
				 XFS_DQUOT_CRC_OFF);
		dqblk->dd_lsn = 0;
	}
	switch (dqtype) {
	case XFS_DQTYPE_USER:
		buftype = XFS_BLFT_UDQUOT_BUF;
		break;
	case XFS_DQTYPE_GROUP:
		buftype = XFS_BLFT_GDQUOT_BUF;
		break;
	case XFS_DQTYPE_PROJ:
		buftype = XFS_BLFT_PDQUOT_BUF;
		break;
	}
	xfs_trans_buf_set_type(sc->tp, bp, buftype);
	xfs_trans_log_buf(sc->tp, bp, 0, BBTOB(bp->b_length) - 1);
	return xrep_roll_trans(sc);
}

/*
 * Repair a quota file's data fork.  The function returns with the inode
 * joined.
 */
STATIC int
xrep_quota_data_fork(
	struct xfs_scrub	*sc,
	xfs_dqtype_t		dqtype)
{
	struct xfs_bmbt_irec	irec = { 0 };
	struct xfs_iext_cursor	icur;
	struct xfs_quotainfo	*qi = sc->mp->m_quotainfo;
	struct xfs_ifork	*ifp;
	xfs_fileoff_t		max_dqid_off;
	xfs_fileoff_t		off;
	xfs_fsblock_t		fsbno;
	bool			truncate = false;
	bool			joined = false;
	int			error = 0;

	error = xrep_metadata_inode_forks(sc);
	if (error)
		goto out;

	/* Check for data fork problems that apply only to quota files. */
	max_dqid_off = XFS_DQ_ID_MAX / qi->qi_dqperchunk;
	ifp = xfs_ifork_ptr(sc->ip, XFS_DATA_FORK);
	for_each_xfs_iext(ifp, &icur, &irec) {
		if (isnullstartblock(irec.br_startblock)) {
			error = -EFSCORRUPTED;
			goto out;
		}

		if (irec.br_startoff > max_dqid_off ||
		    irec.br_startoff + irec.br_blockcount - 1 > max_dqid_off) {
			truncate = true;
			break;
		}

		/* Convert unwritten extents to real ones. */
		if (irec.br_state == XFS_EXT_UNWRITTEN) {
			struct xfs_bmbt_irec	nrec;
			int			nmap = 1;

			if (!joined) {
				xfs_trans_ijoin(sc->tp, sc->ip, 0);
				joined = true;
			}

			error = xfs_bmapi_write(sc->tp, sc->ip,
					irec.br_startoff, irec.br_blockcount,
					XFS_BMAPI_CONVERT, 0, &nrec, &nmap);
			if (error)
				goto out;
			if (nmap != 1) {
				error = -ENOSPC;
				goto out;
			}
			ASSERT(nrec.br_startoff == irec.br_startoff);
			ASSERT(nrec.br_blockcount == irec.br_blockcount);

			error = xfs_defer_finish(&sc->tp);
			if (error)
				goto out;
		}
	}

	if (!joined) {
		xfs_trans_ijoin(sc->tp, sc->ip, 0);
		joined = true;
	}

	if (truncate) {
		/* Erase everything after the block containing the max dquot */
		error = xfs_bunmapi_range(&sc->tp, sc->ip, 0,
				max_dqid_off * sc->mp->m_sb.sb_blocksize,
				XFS_MAX_FILEOFF);
		if (error)
			goto out;

		/* Remove all CoW reservations. */
		error = xfs_reflink_cancel_cow_blocks(sc->ip, &sc->tp, 0,
				XFS_MAX_FILEOFF, true);
		if (error)
			goto out;
		sc->ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;

		/*
		 * Always re-log the inode so that our permanent transaction
		 * can keep on rolling it forward in the log.
		 */
		xfs_trans_log_inode(sc->tp, sc->ip, XFS_ILOG_CORE);
	}

	/* Now go fix anything that fails the verifiers. */
	for_each_xfs_iext(ifp, &icur, &irec) {
		for (fsbno = irec.br_startblock, off = irec.br_startoff;
		     fsbno < irec.br_startblock + irec.br_blockcount;
		     fsbno += XFS_DQUOT_CLUSTER_SIZE_FSB,
				off += XFS_DQUOT_CLUSTER_SIZE_FSB) {
			error = xrep_quota_block(sc,
					XFS_FSB_TO_DADDR(sc->mp, fsbno),
					dqtype, off * qi->qi_dqperchunk);
			if (error)
				goto out;
		}
	}

out:
	return error;
}

/*
 * Go fix anything in the quota items that we could have been mad about.  Now
 * that we've checked the quota inode data fork we have to drop ILOCK_EXCL to
 * use the regular dquot functions.
 */
STATIC int
xrep_quota_problems(
	struct xfs_scrub	*sc,
	xfs_dqtype_t		dqtype)
{
	struct xchk_dqiter	cursor = { };
	struct xrep_quota_info	rqi = { .sc = sc };
	struct xfs_dquot	*dq;
	int			error;

	xchk_dqiter_init(&cursor, sc, dqtype);
	while ((error = xchk_dquot_iter(&cursor, &dq)) == 1) {
		error = xrep_quota_item(&rqi, dq);
		xfs_qm_dqput(dq);
		if (error)
			break;
	}
	if (error)
		return error;

	/* Make a quotacheck happen. */
	if (rqi.need_quotacheck)
		xrep_force_quotacheck(sc, dqtype);
	return 0;
}

/* Repair all of a quota type's items. */
int
xrep_quota(
	struct xfs_scrub	*sc)
{
	xfs_dqtype_t		dqtype;
	int			error;

	dqtype = xchk_quota_to_dqtype(sc);

	/*
	 * Re-take the ILOCK so that we can fix any problems that we found
	 * with the data fork mappings, or with the dquot bufs themselves.
	 */
	if (!(sc->ilock_flags & XFS_ILOCK_EXCL))
		xchk_ilock(sc, XFS_ILOCK_EXCL);
	error = xrep_quota_data_fork(sc, dqtype);
	if (error)
		return error;

	/*
	 * Finish deferred items and roll the transaction to unjoin the quota
	 * inode from transaction so that we can unlock the quota inode; we
	 * play only with dquots from now on.
	 */
	error = xrep_defer_finish(sc);
	if (error)
		return error;
	error = xfs_trans_roll(&sc->tp);
	if (error)
		return error;
	xchk_iunlock(sc, sc->ilock_flags);

	/* Fix anything the dquot verifiers don't complain about. */
	error = xrep_quota_problems(sc, dqtype);
	if (error)
		return error;

	return xrep_trans_commit(sc);
}
