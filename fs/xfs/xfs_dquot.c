// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_space.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_bmap_btree.h"
#include "xfs_error.h"

/*
 * Lock order:
 *
 * ip->i_lock
 *   qi->qi_tree_lock
 *     dquot->q_qlock (xfs_dqlock() and friends)
 *       dquot->q_flush (xfs_dqflock() and friends)
 *       qi->qi_lru_lock
 *
 * If two dquots need to be locked the order is user before group/project,
 * otherwise by the lowest id first, see xfs_dqlock2.
 */

struct kmem_cache		*xfs_dqtrx_cache;
static struct kmem_cache	*xfs_dquot_cache;

static struct lock_class_key xfs_dquot_group_class;
static struct lock_class_key xfs_dquot_project_class;

/*
 * This is called to free all the memory associated with a dquot
 */
void
xfs_qm_dqdestroy(
	struct xfs_dquot	*dqp)
{
	ASSERT(list_empty(&dqp->q_lru));

	kmem_free(dqp->q_logitem.qli_item.li_lv_shadow);
	mutex_destroy(&dqp->q_qlock);

	XFS_STATS_DEC(dqp->q_mount, xs_qm_dquot);
	kmem_cache_free(xfs_dquot_cache, dqp);
}

/*
 * If default limits are in force, push them into the dquot now.
 * We overwrite the dquot limits only if they are zero and this
 * is not the root dquot.
 */
void
xfs_qm_adjust_dqlimits(
	struct xfs_dquot	*dq)
{
	struct xfs_mount	*mp = dq->q_mount;
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_def_quota	*defq;
	int			prealloc = 0;

	ASSERT(dq->q_id);
	defq = xfs_get_defquota(q, xfs_dquot_type(dq));

	if (!dq->q_blk.softlimit) {
		dq->q_blk.softlimit = defq->blk.soft;
		prealloc = 1;
	}
	if (!dq->q_blk.hardlimit) {
		dq->q_blk.hardlimit = defq->blk.hard;
		prealloc = 1;
	}
	if (!dq->q_ino.softlimit)
		dq->q_ino.softlimit = defq->ino.soft;
	if (!dq->q_ino.hardlimit)
		dq->q_ino.hardlimit = defq->ino.hard;
	if (!dq->q_rtb.softlimit)
		dq->q_rtb.softlimit = defq->rtb.soft;
	if (!dq->q_rtb.hardlimit)
		dq->q_rtb.hardlimit = defq->rtb.hard;

	if (prealloc)
		xfs_dquot_set_prealloc_limits(dq);
}

/* Set the expiration time of a quota's grace period. */
time64_t
xfs_dquot_set_timeout(
	struct xfs_mount	*mp,
	time64_t		timeout)
{
	struct xfs_quotainfo	*qi = mp->m_quotainfo;

	return clamp_t(time64_t, timeout, qi->qi_expiry_min,
					  qi->qi_expiry_max);
}

/* Set the length of the default grace period. */
time64_t
xfs_dquot_set_grace_period(
	time64_t		grace)
{
	return clamp_t(time64_t, grace, XFS_DQ_GRACE_MIN, XFS_DQ_GRACE_MAX);
}

/*
 * Determine if this quota counter is over either limit and set the quota
 * timers as appropriate.
 */
static inline void
xfs_qm_adjust_res_timer(
	struct xfs_mount	*mp,
	struct xfs_dquot_res	*res,
	struct xfs_quota_limits	*qlim)
{
	ASSERT(res->hardlimit == 0 || res->softlimit <= res->hardlimit);

	if ((res->softlimit && res->count > res->softlimit) ||
	    (res->hardlimit && res->count > res->hardlimit)) {
		if (res->timer == 0)
			res->timer = xfs_dquot_set_timeout(mp,
					ktime_get_real_seconds() + qlim->time);
	} else {
		res->timer = 0;
	}
}

/*
 * Check the limits and timers of a dquot and start or reset timers
 * if necessary.
 * This gets called even when quota enforcement is OFF, which makes our
 * life a little less complicated. (We just don't reject any quota
 * reservations in that case, when enforcement is off).
 * We also return 0 as the values of the timers in Q_GETQUOTA calls, when
 * enforcement's off.
 * In contrast, warnings are a little different in that they don't
 * 'automatically' get started when limits get exceeded.  They do
 * get reset to zero, however, when we find the count to be under
 * the soft limit (they are only ever set non-zero via userspace).
 */
void
xfs_qm_adjust_dqtimers(
	struct xfs_dquot	*dq)
{
	struct xfs_mount	*mp = dq->q_mount;
	struct xfs_quotainfo	*qi = mp->m_quotainfo;
	struct xfs_def_quota	*defq;

	ASSERT(dq->q_id);
	defq = xfs_get_defquota(qi, xfs_dquot_type(dq));

	xfs_qm_adjust_res_timer(dq->q_mount, &dq->q_blk, &defq->blk);
	xfs_qm_adjust_res_timer(dq->q_mount, &dq->q_ino, &defq->ino);
	xfs_qm_adjust_res_timer(dq->q_mount, &dq->q_rtb, &defq->rtb);
}

/*
 * initialize a buffer full of dquots and log the whole thing
 */
STATIC void
xfs_qm_init_dquot_blk(
	struct xfs_trans	*tp,
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	struct xfs_buf		*bp)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_dqblk	*d;
	xfs_dqid_t		curid;
	unsigned int		qflag;
	unsigned int		blftype;
	int			i;

	ASSERT(tp);
	ASSERT(xfs_buf_islocked(bp));

	switch (type) {
	case XFS_DQTYPE_USER:
		qflag = XFS_UQUOTA_CHKD;
		blftype = XFS_BLF_UDQUOT_BUF;
		break;
	case XFS_DQTYPE_PROJ:
		qflag = XFS_PQUOTA_CHKD;
		blftype = XFS_BLF_PDQUOT_BUF;
		break;
	case XFS_DQTYPE_GROUP:
		qflag = XFS_GQUOTA_CHKD;
		blftype = XFS_BLF_GDQUOT_BUF;
		break;
	default:
		ASSERT(0);
		return;
	}

	d = bp->b_addr;

	/*
	 * ID of the first dquot in the block - id's are zero based.
	 */
	curid = id - (id % q->qi_dqperchunk);
	memset(d, 0, BBTOB(q->qi_dqchunklen));
	for (i = 0; i < q->qi_dqperchunk; i++, d++, curid++) {
		d->dd_diskdq.d_magic = cpu_to_be16(XFS_DQUOT_MAGIC);
		d->dd_diskdq.d_version = XFS_DQUOT_VERSION;
		d->dd_diskdq.d_id = cpu_to_be32(curid);
		d->dd_diskdq.d_type = type;
		if (curid > 0 && xfs_has_bigtime(mp))
			d->dd_diskdq.d_type |= XFS_DQTYPE_BIGTIME;
		if (xfs_has_crc(mp)) {
			uuid_copy(&d->dd_uuid, &mp->m_sb.sb_meta_uuid);
			xfs_update_cksum((char *)d, sizeof(struct xfs_dqblk),
					 XFS_DQUOT_CRC_OFF);
		}
	}

	xfs_trans_dquot_buf(tp, bp, blftype);

	/*
	 * quotacheck uses delayed writes to update all the dquots on disk in an
	 * efficient manner instead of logging the individual dquot changes as
	 * they are made. However if we log the buffer allocated here and crash
	 * after quotacheck while the logged initialisation is still in the
	 * active region of the log, log recovery can replay the dquot buffer
	 * initialisation over the top of the checked dquots and corrupt quota
	 * accounting.
	 *
	 * To avoid this problem, quotacheck cannot log the initialised buffer.
	 * We must still dirty the buffer and write it back before the
	 * allocation transaction clears the log. Therefore, mark the buffer as
	 * ordered instead of logging it directly. This is safe for quotacheck
	 * because it detects and repairs allocated but initialized dquot blocks
	 * in the quota inodes.
	 */
	if (!(mp->m_qflags & qflag))
		xfs_trans_ordered_buf(tp, bp);
	else
		xfs_trans_log_buf(tp, bp, 0, BBTOB(q->qi_dqchunklen) - 1);
}

/*
 * Initialize the dynamic speculative preallocation thresholds. The lo/hi
 * watermarks correspond to the soft and hard limits by default. If a soft limit
 * is not specified, we use 95% of the hard limit.
 */
void
xfs_dquot_set_prealloc_limits(struct xfs_dquot *dqp)
{
	uint64_t space;

	dqp->q_prealloc_hi_wmark = dqp->q_blk.hardlimit;
	dqp->q_prealloc_lo_wmark = dqp->q_blk.softlimit;
	if (!dqp->q_prealloc_lo_wmark) {
		dqp->q_prealloc_lo_wmark = dqp->q_prealloc_hi_wmark;
		do_div(dqp->q_prealloc_lo_wmark, 100);
		dqp->q_prealloc_lo_wmark *= 95;
	}

	space = dqp->q_prealloc_hi_wmark;

	do_div(space, 100);
	dqp->q_low_space[XFS_QLOWSP_1_PCNT] = space;
	dqp->q_low_space[XFS_QLOWSP_3_PCNT] = space * 3;
	dqp->q_low_space[XFS_QLOWSP_5_PCNT] = space * 5;
}

/*
 * Ensure that the given in-core dquot has a buffer on disk backing it, and
 * return the buffer locked and held. This is called when the bmapi finds a
 * hole.
 */
STATIC int
xfs_dquot_disk_alloc(
	struct xfs_dquot	*dqp,
	struct xfs_buf		**bpp)
{
	struct xfs_bmbt_irec	map;
	struct xfs_trans	*tp;
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_buf		*bp;
	xfs_dqtype_t		qtype = xfs_dquot_type(dqp);
	struct xfs_inode	*quotip = xfs_quota_inode(mp, qtype);
	int			nmaps = 1;
	int			error;

	trace_xfs_dqalloc(dqp);

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_qm_dqalloc,
			XFS_QM_DQALLOC_SPACE_RES(mp), 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(quotip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, quotip, 0);

	if (!xfs_this_quota_on(dqp->q_mount, qtype)) {
		/*
		 * Return if this type of quotas is turned off while we didn't
		 * have an inode lock
		 */
		error = -ESRCH;
		goto err_cancel;
	}

	error = xfs_iext_count_may_overflow(quotip, XFS_DATA_FORK,
			XFS_IEXT_ADD_NOSPLIT_CNT);
	if (error == -EFBIG)
		error = xfs_iext_count_upgrade(tp, quotip,
				XFS_IEXT_ADD_NOSPLIT_CNT);
	if (error)
		goto err_cancel;

	/* Create the block mapping. */
	error = xfs_bmapi_write(tp, quotip, dqp->q_fileoffset,
			XFS_DQUOT_CLUSTER_SIZE_FSB, XFS_BMAPI_METADATA, 0, &map,
			&nmaps);
	if (error)
		goto err_cancel;

	ASSERT(map.br_blockcount == XFS_DQUOT_CLUSTER_SIZE_FSB);
	ASSERT(nmaps == 1);
	ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
	       (map.br_startblock != HOLESTARTBLOCK));

	/*
	 * Keep track of the blkno to save a lookup later
	 */
	dqp->q_blkno = XFS_FSB_TO_DADDR(mp, map.br_startblock);

	/* now we can just get the buffer (there's nothing to read yet) */
	error = xfs_trans_get_buf(tp, mp->m_ddev_targp, dqp->q_blkno,
			mp->m_quotainfo->qi_dqchunklen, 0, &bp);
	if (error)
		goto err_cancel;
	bp->b_ops = &xfs_dquot_buf_ops;

	/*
	 * Make a chunk of dquots out of this buffer and log
	 * the entire thing.
	 */
	xfs_qm_init_dquot_blk(tp, mp, dqp->q_id, qtype, bp);
	xfs_buf_set_ref(bp, XFS_DQUOT_REF);

	/*
	 * Hold the buffer and join it to the dfops so that we'll still own
	 * the buffer when we return to the caller.  The buffer disposal on
	 * error must be paid attention to very carefully, as it has been
	 * broken since commit efa092f3d4c6 "[XFS] Fixes a bug in the quota
	 * code when allocating a new dquot record" in 2005, and the later
	 * conversion to xfs_defer_ops in commit 310a75a3c6c747 failed to keep
	 * the buffer locked across the _defer_finish call.  We can now do
	 * this correctly with xfs_defer_bjoin.
	 *
	 * Above, we allocated a disk block for the dquot information and used
	 * get_buf to initialize the dquot. If the _defer_finish fails, the old
	 * transaction is gone but the new buffer is not joined or held to any
	 * transaction, so we must _buf_relse it.
	 *
	 * If everything succeeds, the caller of this function is returned a
	 * buffer that is locked and held to the transaction.  The caller
	 * is responsible for unlocking any buffer passed back, either
	 * manually or by committing the transaction.  On error, the buffer is
	 * released and not passed back.
	 *
	 * Keep the quota inode ILOCKed until after the transaction commit to
	 * maintain the atomicity of bmap/rmap updates.
	 */
	xfs_trans_bhold(tp, bp);
	error = xfs_trans_commit(tp);
	xfs_iunlock(quotip, XFS_ILOCK_EXCL);
	if (error) {
		xfs_buf_relse(bp);
		return error;
	}

	*bpp = bp;
	return 0;

err_cancel:
	xfs_trans_cancel(tp);
	xfs_iunlock(quotip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Read in the in-core dquot's on-disk metadata and return the buffer.
 * Returns ENOENT to signal a hole.
 */
STATIC int
xfs_dquot_disk_read(
	struct xfs_mount	*mp,
	struct xfs_dquot	*dqp,
	struct xfs_buf		**bpp)
{
	struct xfs_bmbt_irec	map;
	struct xfs_buf		*bp;
	xfs_dqtype_t		qtype = xfs_dquot_type(dqp);
	struct xfs_inode	*quotip = xfs_quota_inode(mp, qtype);
	uint			lock_mode;
	int			nmaps = 1;
	int			error;

	lock_mode = xfs_ilock_data_map_shared(quotip);
	if (!xfs_this_quota_on(mp, qtype)) {
		/*
		 * Return if this type of quotas is turned off while we
		 * didn't have the quota inode lock.
		 */
		xfs_iunlock(quotip, lock_mode);
		return -ESRCH;
	}

	/*
	 * Find the block map; no allocations yet
	 */
	error = xfs_bmapi_read(quotip, dqp->q_fileoffset,
			XFS_DQUOT_CLUSTER_SIZE_FSB, &map, &nmaps, 0);
	xfs_iunlock(quotip, lock_mode);
	if (error)
		return error;

	ASSERT(nmaps == 1);
	ASSERT(map.br_blockcount >= 1);
	ASSERT(map.br_startblock != DELAYSTARTBLOCK);
	if (map.br_startblock == HOLESTARTBLOCK)
		return -ENOENT;

	trace_xfs_dqtobp_read(dqp);

	/*
	 * store the blkno etc so that we don't have to do the
	 * mapping all the time
	 */
	dqp->q_blkno = XFS_FSB_TO_DADDR(mp, map.br_startblock);

	error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp, dqp->q_blkno,
			mp->m_quotainfo->qi_dqchunklen, 0, &bp,
			&xfs_dquot_buf_ops);
	if (error) {
		ASSERT(bp == NULL);
		return error;
	}

	ASSERT(xfs_buf_islocked(bp));
	xfs_buf_set_ref(bp, XFS_DQUOT_REF);
	*bpp = bp;

	return 0;
}

/* Allocate and initialize everything we need for an incore dquot. */
STATIC struct xfs_dquot *
xfs_dquot_alloc(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type)
{
	struct xfs_dquot	*dqp;

	dqp = kmem_cache_zalloc(xfs_dquot_cache, GFP_KERNEL | __GFP_NOFAIL);

	dqp->q_type = type;
	dqp->q_id = id;
	dqp->q_mount = mp;
	INIT_LIST_HEAD(&dqp->q_lru);
	mutex_init(&dqp->q_qlock);
	init_waitqueue_head(&dqp->q_pinwait);
	dqp->q_fileoffset = (xfs_fileoff_t)id / mp->m_quotainfo->qi_dqperchunk;
	/*
	 * Offset of dquot in the (fixed sized) dquot chunk.
	 */
	dqp->q_bufoffset = (id % mp->m_quotainfo->qi_dqperchunk) *
			sizeof(struct xfs_dqblk);

	/*
	 * Because we want to use a counting completion, complete
	 * the flush completion once to allow a single access to
	 * the flush completion without blocking.
	 */
	init_completion(&dqp->q_flush);
	complete(&dqp->q_flush);

	/*
	 * Make sure group quotas have a different lock class than user
	 * quotas.
	 */
	switch (type) {
	case XFS_DQTYPE_USER:
		/* uses the default lock class */
		break;
	case XFS_DQTYPE_GROUP:
		lockdep_set_class(&dqp->q_qlock, &xfs_dquot_group_class);
		break;
	case XFS_DQTYPE_PROJ:
		lockdep_set_class(&dqp->q_qlock, &xfs_dquot_project_class);
		break;
	default:
		ASSERT(0);
		break;
	}

	xfs_qm_dquot_logitem_init(dqp);

	XFS_STATS_INC(mp, xs_qm_dquot);
	return dqp;
}

/* Check the ondisk dquot's id and type match what the incore dquot expects. */
static bool
xfs_dquot_check_type(
	struct xfs_dquot	*dqp,
	struct xfs_disk_dquot	*ddqp)
{
	uint8_t			ddqp_type;
	uint8_t			dqp_type;

	ddqp_type = ddqp->d_type & XFS_DQTYPE_REC_MASK;
	dqp_type = xfs_dquot_type(dqp);

	if (be32_to_cpu(ddqp->d_id) != dqp->q_id)
		return false;

	/*
	 * V5 filesystems always expect an exact type match.  V4 filesystems
	 * expect an exact match for user dquots and for non-root group and
	 * project dquots.
	 */
	if (xfs_has_crc(dqp->q_mount) ||
	    dqp_type == XFS_DQTYPE_USER || dqp->q_id != 0)
		return ddqp_type == dqp_type;

	/*
	 * V4 filesystems support either group or project quotas, but not both
	 * at the same time.  The non-user quota file can be switched between
	 * group and project quota uses depending on the mount options, which
	 * means that we can encounter the other type when we try to load quota
	 * defaults.  Quotacheck will soon reset the entire quota file
	 * (including the root dquot) anyway, but don't log scary corruption
	 * reports to dmesg.
	 */
	return ddqp_type == XFS_DQTYPE_GROUP || ddqp_type == XFS_DQTYPE_PROJ;
}

/* Copy the in-core quota fields in from the on-disk buffer. */
STATIC int
xfs_dquot_from_disk(
	struct xfs_dquot	*dqp,
	struct xfs_buf		*bp)
{
	struct xfs_dqblk	*dqb = xfs_buf_offset(bp, dqp->q_bufoffset);
	struct xfs_disk_dquot	*ddqp = &dqb->dd_diskdq;

	/*
	 * Ensure that we got the type and ID we were looking for.
	 * Everything else was checked by the dquot buffer verifier.
	 */
	if (!xfs_dquot_check_type(dqp, ddqp)) {
		xfs_alert_tag(bp->b_mount, XFS_PTAG_VERIFIER_ERROR,
			  "Metadata corruption detected at %pS, quota %u",
			  __this_address, dqp->q_id);
		xfs_alert(bp->b_mount, "Unmount and run xfs_repair");
		return -EFSCORRUPTED;
	}

	/* copy everything from disk dquot to the incore dquot */
	dqp->q_type = ddqp->d_type;
	dqp->q_blk.hardlimit = be64_to_cpu(ddqp->d_blk_hardlimit);
	dqp->q_blk.softlimit = be64_to_cpu(ddqp->d_blk_softlimit);
	dqp->q_ino.hardlimit = be64_to_cpu(ddqp->d_ino_hardlimit);
	dqp->q_ino.softlimit = be64_to_cpu(ddqp->d_ino_softlimit);
	dqp->q_rtb.hardlimit = be64_to_cpu(ddqp->d_rtb_hardlimit);
	dqp->q_rtb.softlimit = be64_to_cpu(ddqp->d_rtb_softlimit);

	dqp->q_blk.count = be64_to_cpu(ddqp->d_bcount);
	dqp->q_ino.count = be64_to_cpu(ddqp->d_icount);
	dqp->q_rtb.count = be64_to_cpu(ddqp->d_rtbcount);

	dqp->q_blk.timer = xfs_dquot_from_disk_ts(ddqp, ddqp->d_btimer);
	dqp->q_ino.timer = xfs_dquot_from_disk_ts(ddqp, ddqp->d_itimer);
	dqp->q_rtb.timer = xfs_dquot_from_disk_ts(ddqp, ddqp->d_rtbtimer);

	/*
	 * Reservation counters are defined as reservation plus current usage
	 * to avoid having to add every time.
	 */
	dqp->q_blk.reserved = dqp->q_blk.count;
	dqp->q_ino.reserved = dqp->q_ino.count;
	dqp->q_rtb.reserved = dqp->q_rtb.count;

	/* initialize the dquot speculative prealloc thresholds */
	xfs_dquot_set_prealloc_limits(dqp);
	return 0;
}

/* Copy the in-core quota fields into the on-disk buffer. */
void
xfs_dquot_to_disk(
	struct xfs_disk_dquot	*ddqp,
	struct xfs_dquot	*dqp)
{
	ddqp->d_magic = cpu_to_be16(XFS_DQUOT_MAGIC);
	ddqp->d_version = XFS_DQUOT_VERSION;
	ddqp->d_type = dqp->q_type;
	ddqp->d_id = cpu_to_be32(dqp->q_id);
	ddqp->d_pad0 = 0;
	ddqp->d_pad = 0;

	ddqp->d_blk_hardlimit = cpu_to_be64(dqp->q_blk.hardlimit);
	ddqp->d_blk_softlimit = cpu_to_be64(dqp->q_blk.softlimit);
	ddqp->d_ino_hardlimit = cpu_to_be64(dqp->q_ino.hardlimit);
	ddqp->d_ino_softlimit = cpu_to_be64(dqp->q_ino.softlimit);
	ddqp->d_rtb_hardlimit = cpu_to_be64(dqp->q_rtb.hardlimit);
	ddqp->d_rtb_softlimit = cpu_to_be64(dqp->q_rtb.softlimit);

	ddqp->d_bcount = cpu_to_be64(dqp->q_blk.count);
	ddqp->d_icount = cpu_to_be64(dqp->q_ino.count);
	ddqp->d_rtbcount = cpu_to_be64(dqp->q_rtb.count);

	ddqp->d_bwarns = 0;
	ddqp->d_iwarns = 0;
	ddqp->d_rtbwarns = 0;

	ddqp->d_btimer = xfs_dquot_to_disk_ts(dqp, dqp->q_blk.timer);
	ddqp->d_itimer = xfs_dquot_to_disk_ts(dqp, dqp->q_ino.timer);
	ddqp->d_rtbtimer = xfs_dquot_to_disk_ts(dqp, dqp->q_rtb.timer);
}

/*
 * Read in the ondisk dquot using dqtobp() then copy it to an incore version,
 * and release the buffer immediately.  If @can_alloc is true, fill any
 * holes in the on-disk metadata.
 */
static int
xfs_qm_dqread(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	bool			can_alloc,
	struct xfs_dquot	**dqpp)
{
	struct xfs_dquot	*dqp;
	struct xfs_buf		*bp;
	int			error;

	dqp = xfs_dquot_alloc(mp, id, type);
	trace_xfs_dqread(dqp);

	/* Try to read the buffer, allocating if necessary. */
	error = xfs_dquot_disk_read(mp, dqp, &bp);
	if (error == -ENOENT && can_alloc)
		error = xfs_dquot_disk_alloc(dqp, &bp);
	if (error)
		goto err;

	/*
	 * At this point we should have a clean locked buffer.  Copy the data
	 * to the incore dquot and release the buffer since the incore dquot
	 * has its own locking protocol so we needn't tie up the buffer any
	 * further.
	 */
	ASSERT(xfs_buf_islocked(bp));
	error = xfs_dquot_from_disk(dqp, bp);
	xfs_buf_relse(bp);
	if (error)
		goto err;

	*dqpp = dqp;
	return error;

err:
	trace_xfs_dqread_fail(dqp);
	xfs_qm_dqdestroy(dqp);
	*dqpp = NULL;
	return error;
}

/*
 * Advance to the next id in the current chunk, or if at the
 * end of the chunk, skip ahead to first id in next allocated chunk
 * using the SEEK_DATA interface.
 */
static int
xfs_dq_get_next_id(
	struct xfs_mount	*mp,
	xfs_dqtype_t		type,
	xfs_dqid_t		*id)
{
	struct xfs_inode	*quotip = xfs_quota_inode(mp, type);
	xfs_dqid_t		next_id = *id + 1; /* simple advance */
	uint			lock_flags;
	struct xfs_bmbt_irec	got;
	struct xfs_iext_cursor	cur;
	xfs_fsblock_t		start;
	int			error = 0;

	/* If we'd wrap past the max ID, stop */
	if (next_id < *id)
		return -ENOENT;

	/* If new ID is within the current chunk, advancing it sufficed */
	if (next_id % mp->m_quotainfo->qi_dqperchunk) {
		*id = next_id;
		return 0;
	}

	/* Nope, next_id is now past the current chunk, so find the next one */
	start = (xfs_fsblock_t)next_id / mp->m_quotainfo->qi_dqperchunk;

	lock_flags = xfs_ilock_data_map_shared(quotip);
	error = xfs_iread_extents(NULL, quotip, XFS_DATA_FORK);
	if (error)
		return error;

	if (xfs_iext_lookup_extent(quotip, &quotip->i_df, start, &cur, &got)) {
		/* contiguous chunk, bump startoff for the id calculation */
		if (got.br_startoff < start)
			got.br_startoff = start;
		*id = got.br_startoff * mp->m_quotainfo->qi_dqperchunk;
	} else {
		error = -ENOENT;
	}

	xfs_iunlock(quotip, lock_flags);

	return error;
}

/*
 * Look up the dquot in the in-core cache.  If found, the dquot is returned
 * locked and ready to go.
 */
static struct xfs_dquot *
xfs_qm_dqget_cache_lookup(
	struct xfs_mount	*mp,
	struct xfs_quotainfo	*qi,
	struct radix_tree_root	*tree,
	xfs_dqid_t		id)
{
	struct xfs_dquot	*dqp;

restart:
	mutex_lock(&qi->qi_tree_lock);
	dqp = radix_tree_lookup(tree, id);
	if (!dqp) {
		mutex_unlock(&qi->qi_tree_lock);
		XFS_STATS_INC(mp, xs_qm_dqcachemisses);
		return NULL;
	}

	xfs_dqlock(dqp);
	if (dqp->q_flags & XFS_DQFLAG_FREEING) {
		xfs_dqunlock(dqp);
		mutex_unlock(&qi->qi_tree_lock);
		trace_xfs_dqget_freeing(dqp);
		delay(1);
		goto restart;
	}

	dqp->q_nrefs++;
	mutex_unlock(&qi->qi_tree_lock);

	trace_xfs_dqget_hit(dqp);
	XFS_STATS_INC(mp, xs_qm_dqcachehits);
	return dqp;
}

/*
 * Try to insert a new dquot into the in-core cache.  If an error occurs the
 * caller should throw away the dquot and start over.  Otherwise, the dquot
 * is returned locked (and held by the cache) as if there had been a cache
 * hit.
 */
static int
xfs_qm_dqget_cache_insert(
	struct xfs_mount	*mp,
	struct xfs_quotainfo	*qi,
	struct radix_tree_root	*tree,
	xfs_dqid_t		id,
	struct xfs_dquot	*dqp)
{
	int			error;

	mutex_lock(&qi->qi_tree_lock);
	error = radix_tree_insert(tree, id, dqp);
	if (unlikely(error)) {
		/* Duplicate found!  Caller must try again. */
		mutex_unlock(&qi->qi_tree_lock);
		trace_xfs_dqget_dup(dqp);
		return error;
	}

	/* Return a locked dquot to the caller, with a reference taken. */
	xfs_dqlock(dqp);
	dqp->q_nrefs = 1;

	qi->qi_dquots++;
	mutex_unlock(&qi->qi_tree_lock);

	return 0;
}

/* Check our input parameters. */
static int
xfs_qm_dqget_checks(
	struct xfs_mount	*mp,
	xfs_dqtype_t		type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		if (!XFS_IS_UQUOTA_ON(mp))
			return -ESRCH;
		return 0;
	case XFS_DQTYPE_GROUP:
		if (!XFS_IS_GQUOTA_ON(mp))
			return -ESRCH;
		return 0;
	case XFS_DQTYPE_PROJ:
		if (!XFS_IS_PQUOTA_ON(mp))
			return -ESRCH;
		return 0;
	default:
		WARN_ON_ONCE(0);
		return -EINVAL;
	}
}

/*
 * Given the file system, id, and type (UDQUOT/GDQUOT/PDQUOT), return a
 * locked dquot, doing an allocation (if requested) as needed.
 */
int
xfs_qm_dqget(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	bool			can_alloc,
	struct xfs_dquot	**O_dqpp)
{
	struct xfs_quotainfo	*qi = mp->m_quotainfo;
	struct radix_tree_root	*tree = xfs_dquot_tree(qi, type);
	struct xfs_dquot	*dqp;
	int			error;

	error = xfs_qm_dqget_checks(mp, type);
	if (error)
		return error;

restart:
	dqp = xfs_qm_dqget_cache_lookup(mp, qi, tree, id);
	if (dqp) {
		*O_dqpp = dqp;
		return 0;
	}

	error = xfs_qm_dqread(mp, id, type, can_alloc, &dqp);
	if (error)
		return error;

	error = xfs_qm_dqget_cache_insert(mp, qi, tree, id, dqp);
	if (error) {
		/*
		 * Duplicate found. Just throw away the new dquot and start
		 * over.
		 */
		xfs_qm_dqdestroy(dqp);
		XFS_STATS_INC(mp, xs_qm_dquot_dups);
		goto restart;
	}

	trace_xfs_dqget_miss(dqp);
	*O_dqpp = dqp;
	return 0;
}

/*
 * Given a dquot id and type, read and initialize a dquot from the on-disk
 * metadata.  This function is only for use during quota initialization so
 * it ignores the dquot cache assuming that the dquot shrinker isn't set up.
 * The caller is responsible for _qm_dqdestroy'ing the returned dquot.
 */
int
xfs_qm_dqget_uncached(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	struct xfs_dquot	**dqpp)
{
	int			error;

	error = xfs_qm_dqget_checks(mp, type);
	if (error)
		return error;

	return xfs_qm_dqread(mp, id, type, 0, dqpp);
}

/* Return the quota id for a given inode and type. */
xfs_dqid_t
xfs_qm_id_for_quotatype(
	struct xfs_inode	*ip,
	xfs_dqtype_t		type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return i_uid_read(VFS_I(ip));
	case XFS_DQTYPE_GROUP:
		return i_gid_read(VFS_I(ip));
	case XFS_DQTYPE_PROJ:
		return ip->i_projid;
	}
	ASSERT(0);
	return 0;
}

/*
 * Return the dquot for a given inode and type.  If @can_alloc is true, then
 * allocate blocks if needed.  The inode's ILOCK must be held and it must not
 * have already had an inode attached.
 */
int
xfs_qm_dqget_inode(
	struct xfs_inode	*ip,
	xfs_dqtype_t		type,
	bool			can_alloc,
	struct xfs_dquot	**O_dqpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_quotainfo	*qi = mp->m_quotainfo;
	struct radix_tree_root	*tree = xfs_dquot_tree(qi, type);
	struct xfs_dquot	*dqp;
	xfs_dqid_t		id;
	int			error;

	error = xfs_qm_dqget_checks(mp, type);
	if (error)
		return error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(xfs_inode_dquot(ip, type) == NULL);

	id = xfs_qm_id_for_quotatype(ip, type);

restart:
	dqp = xfs_qm_dqget_cache_lookup(mp, qi, tree, id);
	if (dqp) {
		*O_dqpp = dqp;
		return 0;
	}

	/*
	 * Dquot cache miss. We don't want to keep the inode lock across
	 * a (potential) disk read. Also we don't want to deal with the lock
	 * ordering between quotainode and this inode. OTOH, dropping the inode
	 * lock here means dealing with a chown that can happen before
	 * we re-acquire the lock.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	error = xfs_qm_dqread(mp, id, type, can_alloc, &dqp);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	if (error)
		return error;

	/*
	 * A dquot could be attached to this inode by now, since we had
	 * dropped the ilock.
	 */
	if (xfs_this_quota_on(mp, type)) {
		struct xfs_dquot	*dqp1;

		dqp1 = xfs_inode_dquot(ip, type);
		if (dqp1) {
			xfs_qm_dqdestroy(dqp);
			dqp = dqp1;
			xfs_dqlock(dqp);
			goto dqret;
		}
	} else {
		/* inode stays locked on return */
		xfs_qm_dqdestroy(dqp);
		return -ESRCH;
	}

	error = xfs_qm_dqget_cache_insert(mp, qi, tree, id, dqp);
	if (error) {
		/*
		 * Duplicate found. Just throw away the new dquot and start
		 * over.
		 */
		xfs_qm_dqdestroy(dqp);
		XFS_STATS_INC(mp, xs_qm_dquot_dups);
		goto restart;
	}

dqret:
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	trace_xfs_dqget_miss(dqp);
	*O_dqpp = dqp;
	return 0;
}

/*
 * Starting at @id and progressing upwards, look for an initialized incore
 * dquot, lock it, and return it.
 */
int
xfs_qm_dqget_next(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	xfs_dqtype_t		type,
	struct xfs_dquot	**dqpp)
{
	struct xfs_dquot	*dqp;
	int			error = 0;

	*dqpp = NULL;
	for (; !error; error = xfs_dq_get_next_id(mp, type, &id)) {
		error = xfs_qm_dqget(mp, id, type, false, &dqp);
		if (error == -ENOENT)
			continue;
		else if (error != 0)
			break;

		if (!XFS_IS_DQUOT_UNINITIALIZED(dqp)) {
			*dqpp = dqp;
			return 0;
		}

		xfs_qm_dqput(dqp);
	}

	return error;
}

/*
 * Release a reference to the dquot (decrement ref-count) and unlock it.
 *
 * If there is a group quota attached to this dquot, carefully release that
 * too without tripping over deadlocks'n'stuff.
 */
void
xfs_qm_dqput(
	struct xfs_dquot	*dqp)
{
	ASSERT(dqp->q_nrefs > 0);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	trace_xfs_dqput(dqp);

	if (--dqp->q_nrefs == 0) {
		struct xfs_quotainfo	*qi = dqp->q_mount->m_quotainfo;
		trace_xfs_dqput_free(dqp);

		if (list_lru_add(&qi->qi_lru, &dqp->q_lru))
			XFS_STATS_INC(dqp->q_mount, xs_qm_dquot_unused);
	}
	xfs_dqunlock(dqp);
}

/*
 * Release a dquot. Flush it if dirty, then dqput() it.
 * dquot must not be locked.
 */
void
xfs_qm_dqrele(
	struct xfs_dquot	*dqp)
{
	if (!dqp)
		return;

	trace_xfs_dqrele(dqp);

	xfs_dqlock(dqp);
	/*
	 * We don't care to flush it if the dquot is dirty here.
	 * That will create stutters that we want to avoid.
	 * Instead we do a delayed write when we try to reclaim
	 * a dirty dquot. Also xfs_sync will take part of the burden...
	 */
	xfs_qm_dqput(dqp);
}

/*
 * This is the dquot flushing I/O completion routine.  It is called
 * from interrupt level when the buffer containing the dquot is
 * flushed to disk.  It is responsible for removing the dquot logitem
 * from the AIL if it has not been re-logged, and unlocking the dquot's
 * flush lock. This behavior is very similar to that of inodes..
 */
static void
xfs_qm_dqflush_done(
	struct xfs_log_item	*lip)
{
	struct xfs_dq_logitem	*qip = (struct xfs_dq_logitem *)lip;
	struct xfs_dquot	*dqp = qip->qli_dquot;
	struct xfs_ail		*ailp = lip->li_ailp;
	xfs_lsn_t		tail_lsn;

	/*
	 * We only want to pull the item from the AIL if its
	 * location in the log has not changed since we started the flush.
	 * Thus, we only bother if the dquot's lsn has
	 * not changed. First we check the lsn outside the lock
	 * since it's cheaper, and then we recheck while
	 * holding the lock before removing the dquot from the AIL.
	 */
	if (test_bit(XFS_LI_IN_AIL, &lip->li_flags) &&
	    ((lip->li_lsn == qip->qli_flush_lsn) ||
	     test_bit(XFS_LI_FAILED, &lip->li_flags))) {

		spin_lock(&ailp->ail_lock);
		xfs_clear_li_failed(lip);
		if (lip->li_lsn == qip->qli_flush_lsn) {
			/* xfs_ail_update_finish() drops the AIL lock */
			tail_lsn = xfs_ail_delete_one(ailp, lip);
			xfs_ail_update_finish(ailp, tail_lsn);
		} else {
			spin_unlock(&ailp->ail_lock);
		}
	}

	/*
	 * Release the dq's flush lock since we're done with it.
	 */
	xfs_dqfunlock(dqp);
}

void
xfs_buf_dquot_iodone(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip, *n;

	list_for_each_entry_safe(lip, n, &bp->b_li_list, li_bio_list) {
		list_del_init(&lip->li_bio_list);
		xfs_qm_dqflush_done(lip);
	}
}

void
xfs_buf_dquot_io_fail(
	struct xfs_buf		*bp)
{
	struct xfs_log_item	*lip;

	spin_lock(&bp->b_mount->m_ail->ail_lock);
	list_for_each_entry(lip, &bp->b_li_list, li_bio_list)
		xfs_set_li_failed(lip, bp);
	spin_unlock(&bp->b_mount->m_ail->ail_lock);
}

/* Check incore dquot for errors before we flush. */
static xfs_failaddr_t
xfs_qm_dqflush_check(
	struct xfs_dquot	*dqp)
{
	xfs_dqtype_t		type = xfs_dquot_type(dqp);

	if (type != XFS_DQTYPE_USER &&
	    type != XFS_DQTYPE_GROUP &&
	    type != XFS_DQTYPE_PROJ)
		return __this_address;

	if (dqp->q_id == 0)
		return NULL;

	if (dqp->q_blk.softlimit && dqp->q_blk.count > dqp->q_blk.softlimit &&
	    !dqp->q_blk.timer)
		return __this_address;

	if (dqp->q_ino.softlimit && dqp->q_ino.count > dqp->q_ino.softlimit &&
	    !dqp->q_ino.timer)
		return __this_address;

	if (dqp->q_rtb.softlimit && dqp->q_rtb.count > dqp->q_rtb.softlimit &&
	    !dqp->q_rtb.timer)
		return __this_address;

	/* bigtime flag should never be set on root dquots */
	if (dqp->q_type & XFS_DQTYPE_BIGTIME) {
		if (!xfs_has_bigtime(dqp->q_mount))
			return __this_address;
		if (dqp->q_id == 0)
			return __this_address;
	}

	return NULL;
}

/*
 * Write a modified dquot to disk.
 * The dquot must be locked and the flush lock too taken by caller.
 * The flush lock will not be unlocked until the dquot reaches the disk,
 * but the dquot is free to be unlocked and modified by the caller
 * in the interim. Dquot is still locked on return. This behavior is
 * identical to that of inodes.
 */
int
xfs_qm_dqflush(
	struct xfs_dquot	*dqp,
	struct xfs_buf		**bpp)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_log_item	*lip = &dqp->q_logitem.qli_item;
	struct xfs_buf		*bp;
	struct xfs_dqblk	*dqblk;
	xfs_failaddr_t		fa;
	int			error;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(!completion_done(&dqp->q_flush));

	trace_xfs_dqflush(dqp);

	*bpp = NULL;

	xfs_qm_dqunpin_wait(dqp);

	/*
	 * Get the buffer containing the on-disk dquot
	 */
	error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp, dqp->q_blkno,
				   mp->m_quotainfo->qi_dqchunklen, XBF_TRYLOCK,
				   &bp, &xfs_dquot_buf_ops);
	if (error == -EAGAIN)
		goto out_unlock;
	if (error)
		goto out_abort;

	fa = xfs_qm_dqflush_check(dqp);
	if (fa) {
		xfs_alert(mp, "corrupt dquot ID 0x%x in memory at %pS",
				dqp->q_id, fa);
		xfs_buf_relse(bp);
		error = -EFSCORRUPTED;
		goto out_abort;
	}

	/* Flush the incore dquot to the ondisk buffer. */
	dqblk = xfs_buf_offset(bp, dqp->q_bufoffset);
	xfs_dquot_to_disk(&dqblk->dd_diskdq, dqp);

	/*
	 * Clear the dirty field and remember the flush lsn for later use.
	 */
	dqp->q_flags &= ~XFS_DQFLAG_DIRTY;

	xfs_trans_ail_copy_lsn(mp->m_ail, &dqp->q_logitem.qli_flush_lsn,
					&dqp->q_logitem.qli_item.li_lsn);

	/*
	 * copy the lsn into the on-disk dquot now while we have the in memory
	 * dquot here. This can't be done later in the write verifier as we
	 * can't get access to the log item at that point in time.
	 *
	 * We also calculate the CRC here so that the on-disk dquot in the
	 * buffer always has a valid CRC. This ensures there is no possibility
	 * of a dquot without an up-to-date CRC getting to disk.
	 */
	if (xfs_has_crc(mp)) {
		dqblk->dd_lsn = cpu_to_be64(dqp->q_logitem.qli_item.li_lsn);
		xfs_update_cksum((char *)dqblk, sizeof(struct xfs_dqblk),
				 XFS_DQUOT_CRC_OFF);
	}

	/*
	 * Attach the dquot to the buffer so that we can remove this dquot from
	 * the AIL and release the flush lock once the dquot is synced to disk.
	 */
	bp->b_flags |= _XBF_DQUOTS;
	list_add_tail(&dqp->q_logitem.qli_item.li_bio_list, &bp->b_li_list);

	/*
	 * If the buffer is pinned then push on the log so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (xfs_buf_ispinned(bp)) {
		trace_xfs_dqflush_force(dqp);
		xfs_log_force(mp, 0);
	}

	trace_xfs_dqflush_done(dqp);
	*bpp = bp;
	return 0;

out_abort:
	dqp->q_flags &= ~XFS_DQFLAG_DIRTY;
	xfs_trans_ail_delete(lip, 0);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
out_unlock:
	xfs_dqfunlock(dqp);
	return error;
}

/*
 * Lock two xfs_dquot structures.
 *
 * To avoid deadlocks we always lock the quota structure with
 * the lowerd id first.
 */
void
xfs_dqlock2(
	struct xfs_dquot	*d1,
	struct xfs_dquot	*d2)
{
	if (d1 && d2) {
		ASSERT(d1 != d2);
		if (d1->q_id > d2->q_id) {
			mutex_lock(&d2->q_qlock);
			mutex_lock_nested(&d1->q_qlock, XFS_QLOCK_NESTED);
		} else {
			mutex_lock(&d1->q_qlock);
			mutex_lock_nested(&d2->q_qlock, XFS_QLOCK_NESTED);
		}
	} else if (d1) {
		mutex_lock(&d1->q_qlock);
	} else if (d2) {
		mutex_lock(&d2->q_qlock);
	}
}

int __init
xfs_qm_init(void)
{
	xfs_dquot_cache = kmem_cache_create("xfs_dquot",
					  sizeof(struct xfs_dquot),
					  0, 0, NULL);
	if (!xfs_dquot_cache)
		goto out;

	xfs_dqtrx_cache = kmem_cache_create("xfs_dqtrx",
					     sizeof(struct xfs_dquot_acct),
					     0, 0, NULL);
	if (!xfs_dqtrx_cache)
		goto out_free_dquot_cache;

	return 0;

out_free_dquot_cache:
	kmem_cache_destroy(xfs_dquot_cache);
out:
	return -ENOMEM;
}

void
xfs_qm_exit(void)
{
	kmem_cache_destroy(xfs_dqtrx_cache);
	kmem_cache_destroy(xfs_dquot_cache);
}

/*
 * Iterate every dquot of a particular type.  The caller must ensure that the
 * particular quota type is active.  iter_fn can return negative error codes,
 * or -ECANCELED to indicate that it wants to stop iterating.
 */
int
xfs_qm_dqiterate(
	struct xfs_mount	*mp,
	xfs_dqtype_t		type,
	xfs_qm_dqiterate_fn	iter_fn,
	void			*priv)
{
	struct xfs_dquot	*dq;
	xfs_dqid_t		id = 0;
	int			error;

	do {
		error = xfs_qm_dqget_next(mp, id, type, &dq);
		if (error == -ENOENT)
			return 0;
		if (error)
			return error;

		error = iter_fn(dq, type, priv);
		id = dq->q_id + 1;
		xfs_qm_dqput(dq);
	} while (error == 0 && id != 0);

	return error;
}
