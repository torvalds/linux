/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.
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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_alloc.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_itable.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"

STATIC void	xfs_trans_alloc_dqinfo(xfs_trans_t *);

/*
 * Add the locked dquot to the transaction.
 * The dquot must be locked, and it cannot be associated with any
 * transaction.
 */
void
xfs_trans_dqjoin(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	ASSERT(dqp->q_transp != tp);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(dqp->q_logitem.qli_dquot == dqp);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &dqp->q_logitem.qli_item);

	/*
	 * Initialize d_transp so we can later determine if this dquot is
	 * associated with this transaction.
	 */
	dqp->q_transp = tp;
}


/*
 * This is called to mark the dquot as needing
 * to be logged when the transaction is committed.  The dquot must
 * already be associated with the given transaction.
 * Note that it marks the entire transaction as dirty. In the ordinary
 * case, this gets called via xfs_trans_commit, after the transaction
 * is already dirty. However, there's nothing stop this from getting
 * called directly, as done by xfs_qm_scall_setqlim. Hence, the TRANS_DIRTY
 * flag.
 */
void
xfs_trans_log_dquot(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	ASSERT(dqp->q_transp == tp);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	tp->t_flags |= XFS_TRANS_DIRTY;
	dqp->q_logitem.qli_item.li_desc->lid_flags |= XFS_LID_DIRTY;
}

/*
 * Carry forward whatever is left of the quota blk reservation to
 * the spanky new transaction
 */
void
xfs_trans_dup_dqinfo(
	xfs_trans_t	*otp,
	xfs_trans_t	*ntp)
{
	xfs_dqtrx_t	*oq, *nq;
	int		i,j;
	xfs_dqtrx_t	*oqa, *nqa;

	if (!otp->t_dqinfo)
		return;

	xfs_trans_alloc_dqinfo(ntp);
	oqa = otp->t_dqinfo->dqa_usrdquots;
	nqa = ntp->t_dqinfo->dqa_usrdquots;

	/*
	 * Because the quota blk reservation is carried forward,
	 * it is also necessary to carry forward the DQ_DIRTY flag.
	 */
	if(otp->t_flags & XFS_TRANS_DQ_DIRTY)
		ntp->t_flags |= XFS_TRANS_DQ_DIRTY;

	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			if (oqa[i].qt_dquot == NULL)
				break;
			oq = &oqa[i];
			nq = &nqa[i];

			nq->qt_dquot = oq->qt_dquot;
			nq->qt_bcount_delta = nq->qt_icount_delta = 0;
			nq->qt_rtbcount_delta = 0;

			/*
			 * Transfer whatever is left of the reservations.
			 */
			nq->qt_blk_res = oq->qt_blk_res - oq->qt_blk_res_used;
			oq->qt_blk_res = oq->qt_blk_res_used;

			nq->qt_rtblk_res = oq->qt_rtblk_res -
				oq->qt_rtblk_res_used;
			oq->qt_rtblk_res = oq->qt_rtblk_res_used;

			nq->qt_ino_res = oq->qt_ino_res - oq->qt_ino_res_used;
			oq->qt_ino_res = oq->qt_ino_res_used;

		}
		oqa = otp->t_dqinfo->dqa_grpdquots;
		nqa = ntp->t_dqinfo->dqa_grpdquots;
	}
}

/*
 * Wrap around mod_dquot to account for both user and group quotas.
 */
void
xfs_trans_mod_dquot_byino(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		field,
	long		delta)
{
	xfs_mount_t	*mp = tp->t_mountp;

	if (!XFS_IS_QUOTA_RUNNING(mp) ||
	    !XFS_IS_QUOTA_ON(mp) ||
	    ip->i_ino == mp->m_sb.sb_uquotino ||
	    ip->i_ino == mp->m_sb.sb_gquotino)
		return;

	if (tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);

	if (XFS_IS_UQUOTA_ON(mp) && ip->i_udquot)
		(void) xfs_trans_mod_dquot(tp, ip->i_udquot, field, delta);
	if (XFS_IS_OQUOTA_ON(mp) && ip->i_gdquot)
		(void) xfs_trans_mod_dquot(tp, ip->i_gdquot, field, delta);
}

STATIC xfs_dqtrx_t *
xfs_trans_get_dqtrx(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp)
{
	int		i;
	xfs_dqtrx_t	*qa;

	qa = XFS_QM_ISUDQ(dqp) ?
		tp->t_dqinfo->dqa_usrdquots : tp->t_dqinfo->dqa_grpdquots;

	for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
		if (qa[i].qt_dquot == NULL ||
		    qa[i].qt_dquot == dqp)
			return &qa[i];
	}

	return NULL;
}

/*
 * Make the changes in the transaction structure.
 * The moral equivalent to xfs_trans_mod_sb().
 * We don't touch any fields in the dquot, so we don't care
 * if it's locked or not (most of the time it won't be).
 */
void
xfs_trans_mod_dquot(
	xfs_trans_t	*tp,
	xfs_dquot_t	*dqp,
	uint		field,
	long		delta)
{
	xfs_dqtrx_t	*qtrx;

	ASSERT(tp);
	ASSERT(XFS_IS_QUOTA_RUNNING(tp->t_mountp));
	qtrx = NULL;

	if (tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);
	/*
	 * Find either the first free slot or the slot that belongs
	 * to this dquot.
	 */
	qtrx = xfs_trans_get_dqtrx(tp, dqp);
	ASSERT(qtrx);
	if (qtrx->qt_dquot == NULL)
		qtrx->qt_dquot = dqp;

	switch (field) {

		/*
		 * regular disk blk reservation
		 */
	      case XFS_TRANS_DQ_RES_BLKS:
		qtrx->qt_blk_res += (ulong)delta;
		break;

		/*
		 * inode reservation
		 */
	      case XFS_TRANS_DQ_RES_INOS:
		qtrx->qt_ino_res += (ulong)delta;
		break;

		/*
		 * disk blocks used.
		 */
	      case XFS_TRANS_DQ_BCOUNT:
		if (qtrx->qt_blk_res && delta > 0) {
			qtrx->qt_blk_res_used += (ulong)delta;
			ASSERT(qtrx->qt_blk_res >= qtrx->qt_blk_res_used);
		}
		qtrx->qt_bcount_delta += delta;
		break;

	      case XFS_TRANS_DQ_DELBCOUNT:
		qtrx->qt_delbcnt_delta += delta;
		break;

		/*
		 * Inode Count
		 */
	      case XFS_TRANS_DQ_ICOUNT:
		if (qtrx->qt_ino_res && delta > 0) {
			qtrx->qt_ino_res_used += (ulong)delta;
			ASSERT(qtrx->qt_ino_res >= qtrx->qt_ino_res_used);
		}
		qtrx->qt_icount_delta += delta;
		break;

		/*
		 * rtblk reservation
		 */
	      case XFS_TRANS_DQ_RES_RTBLKS:
		qtrx->qt_rtblk_res += (ulong)delta;
		break;

		/*
		 * rtblk count
		 */
	      case XFS_TRANS_DQ_RTBCOUNT:
		if (qtrx->qt_rtblk_res && delta > 0) {
			qtrx->qt_rtblk_res_used += (ulong)delta;
			ASSERT(qtrx->qt_rtblk_res >= qtrx->qt_rtblk_res_used);
		}
		qtrx->qt_rtbcount_delta += delta;
		break;

	      case XFS_TRANS_DQ_DELRTBCOUNT:
		qtrx->qt_delrtb_delta += delta;
		break;

	      default:
		ASSERT(0);
	}
	tp->t_flags |= XFS_TRANS_DQ_DIRTY;
}


/*
 * Given an array of dqtrx structures, lock all the dquots associated
 * and join them to the transaction, provided they have been modified.
 * We know that the highest number of dquots (of one type - usr OR grp),
 * involved in a transaction is 2 and that both usr and grp combined - 3.
 * So, we don't attempt to make this very generic.
 */
STATIC void
xfs_trans_dqlockedjoin(
	xfs_trans_t	*tp,
	xfs_dqtrx_t	*q)
{
	ASSERT(q[0].qt_dquot != NULL);
	if (q[1].qt_dquot == NULL) {
		xfs_dqlock(q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
	} else {
		ASSERT(XFS_QM_TRANS_MAXDQS == 2);
		xfs_dqlock2(q[0].qt_dquot, q[1].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[1].qt_dquot);
	}
}


/*
 * Called by xfs_trans_commit() and similar in spirit to
 * xfs_trans_apply_sb_deltas().
 * Go thru all the dquots belonging to this transaction and modify the
 * INCORE dquot to reflect the actual usages.
 * Unreserve just the reservations done by this transaction.
 * dquot is still left locked at exit.
 */
void
xfs_trans_apply_dquot_deltas(
	xfs_trans_t		*tp)
{
	int			i, j;
	xfs_dquot_t		*dqp;
	xfs_dqtrx_t		*qtrx, *qa;
	xfs_disk_dquot_t	*d;
	long			totalbdelta;
	long			totalrtbdelta;

	if (!(tp->t_flags & XFS_TRANS_DQ_DIRTY))
		return;

	ASSERT(tp->t_dqinfo);
	qa = tp->t_dqinfo->dqa_usrdquots;
	for (j = 0; j < 2; j++) {
		if (qa[0].qt_dquot == NULL) {
			qa = tp->t_dqinfo->dqa_grpdquots;
			continue;
		}

		/*
		 * Lock all of the dquots and join them to the transaction.
		 */
		xfs_trans_dqlockedjoin(tp, qa);

		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * The array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;

			ASSERT(XFS_DQ_IS_LOCKED(dqp));
			ASSERT(dqp->q_transp == tp);

			/*
			 * adjust the actual number of blocks used
			 */
			d = &dqp->q_core;

			/*
			 * The issue here is - sometimes we don't make a blkquota
			 * reservation intentionally to be fair to users
			 * (when the amount is small). On the other hand,
			 * delayed allocs do make reservations, but that's
			 * outside of a transaction, so we have no
			 * idea how much was really reserved.
			 * So, here we've accumulated delayed allocation blks and
			 * non-delay blks. The assumption is that the
			 * delayed ones are always reserved (outside of a
			 * transaction), and the others may or may not have
			 * quota reservations.
			 */
			totalbdelta = qtrx->qt_bcount_delta +
				qtrx->qt_delbcnt_delta;
			totalrtbdelta = qtrx->qt_rtbcount_delta +
				qtrx->qt_delrtb_delta;
#ifdef DEBUG
			if (totalbdelta < 0)
				ASSERT(be64_to_cpu(d->d_bcount) >=
				       -totalbdelta);

			if (totalrtbdelta < 0)
				ASSERT(be64_to_cpu(d->d_rtbcount) >=
				       -totalrtbdelta);

			if (qtrx->qt_icount_delta < 0)
				ASSERT(be64_to_cpu(d->d_icount) >=
				       -qtrx->qt_icount_delta);
#endif
			if (totalbdelta)
				be64_add_cpu(&d->d_bcount, (xfs_qcnt_t)totalbdelta);

			if (qtrx->qt_icount_delta)
				be64_add_cpu(&d->d_icount, (xfs_qcnt_t)qtrx->qt_icount_delta);

			if (totalrtbdelta)
				be64_add_cpu(&d->d_rtbcount, (xfs_qcnt_t)totalrtbdelta);

			/*
			 * Get any default limits in use.
			 * Start/reset the timer(s) if needed.
			 */
			if (d->d_id) {
				xfs_qm_adjust_dqlimits(tp->t_mountp, d);
				xfs_qm_adjust_dqtimers(tp->t_mountp, d);
			}

			dqp->dq_flags |= XFS_DQ_DIRTY;
			/*
			 * add this to the list of items to get logged
			 */
			xfs_trans_log_dquot(tp, dqp);
			/*
			 * Take off what's left of the original reservation.
			 * In case of delayed allocations, there's no
			 * reservation that a transaction structure knows of.
			 */
			if (qtrx->qt_blk_res != 0) {
				if (qtrx->qt_blk_res != qtrx->qt_blk_res_used) {
					if (qtrx->qt_blk_res >
					    qtrx->qt_blk_res_used)
						dqp->q_res_bcount -= (xfs_qcnt_t)
							(qtrx->qt_blk_res -
							 qtrx->qt_blk_res_used);
					else
						dqp->q_res_bcount -= (xfs_qcnt_t)
							(qtrx->qt_blk_res_used -
							 qtrx->qt_blk_res);
				}
			} else {
				/*
				 * These blks were never reserved, either inside
				 * a transaction or outside one (in a delayed
				 * allocation). Also, this isn't always a
				 * negative number since we sometimes
				 * deliberately skip quota reservations.
				 */
				if (qtrx->qt_bcount_delta) {
					dqp->q_res_bcount +=
					      (xfs_qcnt_t)qtrx->qt_bcount_delta;
				}
			}
			/*
			 * Adjust the RT reservation.
			 */
			if (qtrx->qt_rtblk_res != 0) {
				if (qtrx->qt_rtblk_res != qtrx->qt_rtblk_res_used) {
					if (qtrx->qt_rtblk_res >
					    qtrx->qt_rtblk_res_used)
					       dqp->q_res_rtbcount -= (xfs_qcnt_t)
						       (qtrx->qt_rtblk_res -
							qtrx->qt_rtblk_res_used);
					else
					       dqp->q_res_rtbcount -= (xfs_qcnt_t)
						       (qtrx->qt_rtblk_res_used -
							qtrx->qt_rtblk_res);
				}
			} else {
				if (qtrx->qt_rtbcount_delta)
					dqp->q_res_rtbcount +=
					    (xfs_qcnt_t)qtrx->qt_rtbcount_delta;
			}

			/*
			 * Adjust the inode reservation.
			 */
			if (qtrx->qt_ino_res != 0) {
				ASSERT(qtrx->qt_ino_res >=
				       qtrx->qt_ino_res_used);
				if (qtrx->qt_ino_res > qtrx->qt_ino_res_used)
					dqp->q_res_icount -= (xfs_qcnt_t)
						(qtrx->qt_ino_res -
						 qtrx->qt_ino_res_used);
			} else {
				if (qtrx->qt_icount_delta)
					dqp->q_res_icount +=
					    (xfs_qcnt_t)qtrx->qt_icount_delta;
			}

			ASSERT(dqp->q_res_bcount >=
				be64_to_cpu(dqp->q_core.d_bcount));
			ASSERT(dqp->q_res_icount >=
				be64_to_cpu(dqp->q_core.d_icount));
			ASSERT(dqp->q_res_rtbcount >=
				be64_to_cpu(dqp->q_core.d_rtbcount));
		}
		/*
		 * Do the group quotas next
		 */
		qa = tp->t_dqinfo->dqa_grpdquots;
	}
}

/*
 * Release the reservations, and adjust the dquots accordingly.
 * This is called only when the transaction is being aborted. If by
 * any chance we have done dquot modifications incore (ie. deltas) already,
 * we simply throw those away, since that's the expected behavior
 * when a transaction is curtailed without a commit.
 */
void
xfs_trans_unreserve_and_mod_dquots(
	xfs_trans_t		*tp)
{
	int			i, j;
	xfs_dquot_t		*dqp;
	xfs_dqtrx_t		*qtrx, *qa;
	boolean_t		locked;

	if (!tp->t_dqinfo || !(tp->t_flags & XFS_TRANS_DQ_DIRTY))
		return;

	qa = tp->t_dqinfo->dqa_usrdquots;

	for (j = 0; j < 2; j++) {
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * We assume that the array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;
			/*
			 * Unreserve the original reservation. We don't care
			 * about the number of blocks used field, or deltas.
			 * Also we don't bother to zero the fields.
			 */
			locked = B_FALSE;
			if (qtrx->qt_blk_res) {
				xfs_dqlock(dqp);
				locked = B_TRUE;
				dqp->q_res_bcount -=
					(xfs_qcnt_t)qtrx->qt_blk_res;
			}
			if (qtrx->qt_ino_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = B_TRUE;
				}
				dqp->q_res_icount -=
					(xfs_qcnt_t)qtrx->qt_ino_res;
			}

			if (qtrx->qt_rtblk_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = B_TRUE;
				}
				dqp->q_res_rtbcount -=
					(xfs_qcnt_t)qtrx->qt_rtblk_res;
			}
			if (locked)
				xfs_dqunlock(dqp);

		}
		qa = tp->t_dqinfo->dqa_grpdquots;
	}
}

STATIC void
xfs_quota_warn(
	struct xfs_mount	*mp,
	struct xfs_dquot	*dqp,
	int			type)
{
	/* no warnings for project quotas - we just return ENOSPC later */
	if (dqp->dq_flags & XFS_DQ_PROJ)
		return;
	quota_send_warning((dqp->dq_flags & XFS_DQ_USER) ? USRQUOTA : GRPQUOTA,
			   be32_to_cpu(dqp->q_core.d_id), mp->m_super->s_dev,
			   type);
}

/*
 * This reserves disk blocks and inodes against a dquot.
 * Flags indicate if the dquot is to be locked here and also
 * if the blk reservation is for RT or regular blocks.
 * Sending in XFS_QMOPT_FORCE_RES flag skips the quota check.
 */
STATIC int
xfs_trans_dqresv(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_dquot_t	*dqp,
	long		nblks,
	long		ninos,
	uint		flags)
{
	xfs_qcnt_t	hardlimit;
	xfs_qcnt_t	softlimit;
	time_t		timer;
	xfs_qwarncnt_t	warns;
	xfs_qwarncnt_t	warnlimit;
	xfs_qcnt_t	count;
	xfs_qcnt_t	*resbcountp;
	xfs_quotainfo_t	*q = mp->m_quotainfo;


	xfs_dqlock(dqp);

	if (flags & XFS_TRANS_DQ_RES_BLKS) {
		hardlimit = be64_to_cpu(dqp->q_core.d_blk_hardlimit);
		if (!hardlimit)
			hardlimit = q->qi_bhardlimit;
		softlimit = be64_to_cpu(dqp->q_core.d_blk_softlimit);
		if (!softlimit)
			softlimit = q->qi_bsoftlimit;
		timer = be32_to_cpu(dqp->q_core.d_btimer);
		warns = be16_to_cpu(dqp->q_core.d_bwarns);
		warnlimit = dqp->q_mount->m_quotainfo->qi_bwarnlimit;
		resbcountp = &dqp->q_res_bcount;
	} else {
		ASSERT(flags & XFS_TRANS_DQ_RES_RTBLKS);
		hardlimit = be64_to_cpu(dqp->q_core.d_rtb_hardlimit);
		if (!hardlimit)
			hardlimit = q->qi_rtbhardlimit;
		softlimit = be64_to_cpu(dqp->q_core.d_rtb_softlimit);
		if (!softlimit)
			softlimit = q->qi_rtbsoftlimit;
		timer = be32_to_cpu(dqp->q_core.d_rtbtimer);
		warns = be16_to_cpu(dqp->q_core.d_rtbwarns);
		warnlimit = dqp->q_mount->m_quotainfo->qi_rtbwarnlimit;
		resbcountp = &dqp->q_res_rtbcount;
	}

	if ((flags & XFS_QMOPT_FORCE_RES) == 0 &&
	    dqp->q_core.d_id &&
	    ((XFS_IS_UQUOTA_ENFORCED(dqp->q_mount) && XFS_QM_ISUDQ(dqp)) ||
	     (XFS_IS_OQUOTA_ENFORCED(dqp->q_mount) &&
	      (XFS_QM_ISPDQ(dqp) || XFS_QM_ISGDQ(dqp))))) {
		if (nblks > 0) {
			/*
			 * dquot is locked already. See if we'd go over the
			 * hardlimit or exceed the timelimit if we allocate
			 * nblks.
			 */
			if (hardlimit > 0ULL &&
			    hardlimit <= nblks + *resbcountp) {
				xfs_quota_warn(mp, dqp, QUOTA_NL_BHARDWARN);
				goto error_return;
			}
			if (softlimit > 0ULL &&
			    softlimit <= nblks + *resbcountp) {
				if ((timer != 0 && get_seconds() > timer) ||
				    (warns != 0 && warns >= warnlimit)) {
					xfs_quota_warn(mp, dqp,
						       QUOTA_NL_BSOFTLONGWARN);
					goto error_return;
				}

				xfs_quota_warn(mp, dqp, QUOTA_NL_BSOFTWARN);
			}
		}
		if (ninos > 0) {
			count = be64_to_cpu(dqp->q_core.d_icount);
			timer = be32_to_cpu(dqp->q_core.d_itimer);
			warns = be16_to_cpu(dqp->q_core.d_iwarns);
			warnlimit = dqp->q_mount->m_quotainfo->qi_iwarnlimit;
			hardlimit = be64_to_cpu(dqp->q_core.d_ino_hardlimit);
			if (!hardlimit)
				hardlimit = q->qi_ihardlimit;
			softlimit = be64_to_cpu(dqp->q_core.d_ino_softlimit);
			if (!softlimit)
				softlimit = q->qi_isoftlimit;

			if (hardlimit > 0ULL && count >= hardlimit) {
				xfs_quota_warn(mp, dqp, QUOTA_NL_IHARDWARN);
				goto error_return;
			}
			if (softlimit > 0ULL && count >= softlimit) {
				if  ((timer != 0 && get_seconds() > timer) ||
				     (warns != 0 && warns >= warnlimit)) {
					xfs_quota_warn(mp, dqp,
						       QUOTA_NL_ISOFTLONGWARN);
					goto error_return;
				}
				xfs_quota_warn(mp, dqp, QUOTA_NL_ISOFTWARN);
			}
		}
	}

	/*
	 * Change the reservation, but not the actual usage.
	 * Note that q_res_bcount = q_core.d_bcount + resv
	 */
	(*resbcountp) += (xfs_qcnt_t)nblks;
	if (ninos != 0)
		dqp->q_res_icount += (xfs_qcnt_t)ninos;

	/*
	 * note the reservation amt in the trans struct too,
	 * so that the transaction knows how much was reserved by
	 * it against this particular dquot.
	 * We don't do this when we are reserving for a delayed allocation,
	 * because we don't have the luxury of a transaction envelope then.
	 */
	if (tp) {
		ASSERT(tp->t_dqinfo);
		ASSERT(flags & XFS_QMOPT_RESBLK_MASK);
		if (nblks != 0)
			xfs_trans_mod_dquot(tp, dqp,
					    flags & XFS_QMOPT_RESBLK_MASK,
					    nblks);
		if (ninos != 0)
			xfs_trans_mod_dquot(tp, dqp,
					    XFS_TRANS_DQ_RES_INOS,
					    ninos);
	}
	ASSERT(dqp->q_res_bcount >= be64_to_cpu(dqp->q_core.d_bcount));
	ASSERT(dqp->q_res_rtbcount >= be64_to_cpu(dqp->q_core.d_rtbcount));
	ASSERT(dqp->q_res_icount >= be64_to_cpu(dqp->q_core.d_icount));

	xfs_dqunlock(dqp);
	return 0;

error_return:
	xfs_dqunlock(dqp);
	if (flags & XFS_QMOPT_ENOSPC)
		return ENOSPC;
	return EDQUOT;
}


/*
 * Given dquot(s), make disk block and/or inode reservations against them.
 * The fact that this does the reservation against both the usr and
 * grp/prj quotas is important, because this follows a both-or-nothing
 * approach.
 *
 * flags = XFS_QMOPT_FORCE_RES evades limit enforcement. Used by chown.
 *	   XFS_QMOPT_ENOSPC returns ENOSPC not EDQUOT.  Used by pquota.
 *	   XFS_TRANS_DQ_RES_BLKS reserves regular disk blocks
 *	   XFS_TRANS_DQ_RES_RTBLKS reserves realtime disk blocks
 * dquots are unlocked on return, if they were not locked by caller.
 */
int
xfs_trans_reserve_quota_bydquots(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_dquot_t	*udqp,
	xfs_dquot_t	*gdqp,
	long		nblks,
	long		ninos,
	uint		flags)
{
	int		resvd = 0, error;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;

	if (tp && tp->t_dqinfo == NULL)
		xfs_trans_alloc_dqinfo(tp);

	ASSERT(flags & XFS_QMOPT_RESBLK_MASK);

	if (udqp) {
		error = xfs_trans_dqresv(tp, mp, udqp, nblks, ninos,
					(flags & ~XFS_QMOPT_ENOSPC));
		if (error)
			return error;
		resvd = 1;
	}

	if (gdqp) {
		error = xfs_trans_dqresv(tp, mp, gdqp, nblks, ninos, flags);
		if (error) {
			/*
			 * can't do it, so backout previous reservation
			 */
			if (resvd) {
				flags |= XFS_QMOPT_FORCE_RES;
				xfs_trans_dqresv(tp, mp, udqp,
						 -nblks, -ninos, flags);
			}
			return error;
		}
	}

	/*
	 * Didn't change anything critical, so, no need to log
	 */
	return 0;
}


/*
 * Lock the dquot and change the reservation if we can.
 * This doesn't change the actual usage, just the reservation.
 * The inode sent in is locked.
 */
int
xfs_trans_reserve_quota_nblks(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	long			nblks,
	long			ninos,
	uint			flags)
{
	struct xfs_mount	*mp = ip->i_mount;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;
	if (XFS_IS_PQUOTA_ON(mp))
		flags |= XFS_QMOPT_ENOSPC;

	ASSERT(ip->i_ino != mp->m_sb.sb_uquotino);
	ASSERT(ip->i_ino != mp->m_sb.sb_gquotino);

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT((flags & ~(XFS_QMOPT_FORCE_RES | XFS_QMOPT_ENOSPC)) ==
				XFS_TRANS_DQ_RES_RTBLKS ||
	       (flags & ~(XFS_QMOPT_FORCE_RES | XFS_QMOPT_ENOSPC)) ==
				XFS_TRANS_DQ_RES_BLKS);

	/*
	 * Reserve nblks against these dquots, with trans as the mediator.
	 */
	return xfs_trans_reserve_quota_bydquots(tp, mp,
						ip->i_udquot, ip->i_gdquot,
						nblks, ninos, flags);
}

/*
 * This routine is called to allocate a quotaoff log item.
 */
xfs_qoff_logitem_t *
xfs_trans_get_qoff_item(
	xfs_trans_t		*tp,
	xfs_qoff_logitem_t	*startqoff,
	uint			flags)
{
	xfs_qoff_logitem_t	*q;

	ASSERT(tp != NULL);

	q = xfs_qm_qoff_logitem_init(tp->t_mountp, startqoff, flags);
	ASSERT(q != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &q->qql_item);
	return q;
}


/*
 * This is called to mark the quotaoff logitem as needing
 * to be logged when the transaction is committed.  The logitem must
 * already be associated with the given transaction.
 */
void
xfs_trans_log_quotaoff_item(
	xfs_trans_t		*tp,
	xfs_qoff_logitem_t	*qlp)
{
	tp->t_flags |= XFS_TRANS_DIRTY;
	qlp->qql_item.li_desc->lid_flags |= XFS_LID_DIRTY;
}

STATIC void
xfs_trans_alloc_dqinfo(
	xfs_trans_t	*tp)
{
	tp->t_dqinfo = kmem_zone_zalloc(xfs_Gqm->qm_dqtrxzone, KM_SLEEP);
}

void
xfs_trans_free_dqinfo(
	xfs_trans_t	*tp)
{
	if (!tp->t_dqinfo)
		return;
	kmem_zone_free(xfs_Gqm->qm_dqtrxzone, tp->t_dqinfo);
	tp->t_dqinfo = NULL;
}
