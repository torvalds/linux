// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_quota.h"
#include "xfs_qm.h"
#include "xfs_trace.h"
#include "xfs_error.h"
#include "xfs_health.h"

STATIC void	xfs_trans_alloc_dqinfo(xfs_trans_t *);

/*
 * Add the locked dquot to the transaction.
 * The dquot must be locked, and it cannot be associated with any
 * transaction.
 */
void
xfs_trans_dqjoin(
	struct xfs_trans	*tp,
	struct xfs_dquot	*dqp)
{
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(dqp->q_logitem.qli_dquot == dqp);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &dqp->q_logitem.qli_item);
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
	struct xfs_trans	*tp,
	struct xfs_dquot	*dqp)
{
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	/* Upgrade the dquot to bigtime format if possible. */
	if (dqp->q_id != 0 &&
	    xfs_has_bigtime(tp->t_mountp) &&
	    !(dqp->q_type & XFS_DQTYPE_BIGTIME))
		dqp->q_type |= XFS_DQTYPE_BIGTIME;

	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &dqp->q_logitem.qli_item.li_flags);
}

/*
 * Carry forward whatever is left of the quota blk reservation to
 * the spanky new transaction
 */
void
xfs_trans_dup_dqinfo(
	struct xfs_trans	*otp,
	struct xfs_trans	*ntp)
{
	struct xfs_dqtrx	*oq, *nq;
	int			i, j;
	struct xfs_dqtrx	*oqa, *nqa;
	uint64_t		blk_res_used;

	if (!otp->t_dqinfo)
		return;

	xfs_trans_alloc_dqinfo(ntp);

	for (j = 0; j < XFS_QM_TRANS_DQTYPES; j++) {
		oqa = otp->t_dqinfo->dqs[j];
		nqa = ntp->t_dqinfo->dqs[j];
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			blk_res_used = 0;

			if (oqa[i].qt_dquot == NULL)
				break;
			oq = &oqa[i];
			nq = &nqa[i];

			if (oq->qt_blk_res && oq->qt_bcount_delta > 0)
				blk_res_used = oq->qt_bcount_delta;

			nq->qt_dquot = oq->qt_dquot;
			nq->qt_bcount_delta = nq->qt_icount_delta = 0;
			nq->qt_rtbcount_delta = 0;

			/*
			 * Transfer whatever is left of the reservations.
			 */
			nq->qt_blk_res = oq->qt_blk_res - blk_res_used;
			oq->qt_blk_res = blk_res_used;

			nq->qt_rtblk_res = oq->qt_rtblk_res -
				oq->qt_rtblk_res_used;
			oq->qt_rtblk_res = oq->qt_rtblk_res_used;

			nq->qt_ino_res = oq->qt_ino_res - oq->qt_ino_res_used;
			oq->qt_ino_res = oq->qt_ino_res_used;

		}
	}
}

#ifdef CONFIG_XFS_LIVE_HOOKS
/*
 * Use a static key here to reduce the overhead of quota live updates.  If the
 * compiler supports jump labels, the static branch will be replaced by a nop
 * sled when there are no hook users.  Online fsck is currently the only
 * caller, so this is a reasonable tradeoff.
 *
 * Note: Patching the kernel code requires taking the cpu hotplug lock.  Other
 * parts of the kernel allocate memory with that lock held, which means that
 * XFS callers cannot hold any locks that might be used by memory reclaim or
 * writeback when calling the static_branch_{inc,dec} functions.
 */
DEFINE_STATIC_XFS_HOOK_SWITCH(xfs_dqtrx_hooks_switch);

void
xfs_dqtrx_hook_disable(void)
{
	xfs_hooks_switch_off(&xfs_dqtrx_hooks_switch);
}

void
xfs_dqtrx_hook_enable(void)
{
	xfs_hooks_switch_on(&xfs_dqtrx_hooks_switch);
}

/* Schedule a transactional dquot update on behalf of an inode. */
void
xfs_trans_mod_ino_dquot(
	struct xfs_trans		*tp,
	struct xfs_inode		*ip,
	struct xfs_dquot		*dqp,
	unsigned int			field,
	int64_t				delta)
{
	ASSERT(!xfs_is_metadir_inode(ip) || XFS_IS_DQDETACHED(ip));

	xfs_trans_mod_dquot(tp, dqp, field, delta);

	if (xfs_hooks_switched_on(&xfs_dqtrx_hooks_switch)) {
		struct xfs_mod_ino_dqtrx_params	p = {
			.tx_id		= (uintptr_t)tp,
			.ino		= ip->i_ino,
			.q_type		= xfs_dquot_type(dqp),
			.q_id		= dqp->q_id,
			.delta		= delta
		};
		struct xfs_quotainfo	*qi = tp->t_mountp->m_quotainfo;

		xfs_hooks_call(&qi->qi_mod_ino_dqtrx_hooks, field, &p);
	}
}

/* Call the specified functions during a dquot counter update. */
int
xfs_dqtrx_hook_add(
	struct xfs_quotainfo	*qi,
	struct xfs_dqtrx_hook	*hook)
{
	int			error;

	/*
	 * Transactional dquot updates first call the mod hook when changes
	 * are attached to the transaction and then call the apply hook when
	 * those changes are committed (or canceled).
	 *
	 * The apply hook must be installed before the mod hook so that we
	 * never fail to catch the end of a quota update sequence.
	 */
	error = xfs_hooks_add(&qi->qi_apply_dqtrx_hooks, &hook->apply_hook);
	if (error)
		goto out;

	error = xfs_hooks_add(&qi->qi_mod_ino_dqtrx_hooks, &hook->mod_hook);
	if (error)
		goto out_apply;

	return 0;

out_apply:
	xfs_hooks_del(&qi->qi_apply_dqtrx_hooks, &hook->apply_hook);
out:
	return error;
}

/* Stop calling the specified function during a dquot counter update. */
void
xfs_dqtrx_hook_del(
	struct xfs_quotainfo	*qi,
	struct xfs_dqtrx_hook	*hook)
{
	/*
	 * The mod hook must be removed before apply hook to avoid giving the
	 * hook consumer with an incomplete update.  No hooks should be running
	 * after these functions return.
	 */
	xfs_hooks_del(&qi->qi_mod_ino_dqtrx_hooks, &hook->mod_hook);
	xfs_hooks_del(&qi->qi_apply_dqtrx_hooks, &hook->apply_hook);
}

/* Configure dquot update hook functions. */
void
xfs_dqtrx_hook_setup(
	struct xfs_dqtrx_hook	*hook,
	notifier_fn_t		mod_fn,
	notifier_fn_t		apply_fn)
{
	xfs_hook_setup(&hook->mod_hook, mod_fn);
	xfs_hook_setup(&hook->apply_hook, apply_fn);
}
#endif /* CONFIG_XFS_LIVE_HOOKS */

/*
 * Wrap around mod_dquot to account for both user and group quotas.
 */
void
xfs_trans_mod_dquot_byino(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		field,
	int64_t		delta)
{
	xfs_mount_t	*mp = tp->t_mountp;

	if (!XFS_IS_QUOTA_ON(mp) ||
	    xfs_is_quota_inode(&mp->m_sb, ip->i_ino))
		return;

	ASSERT(!xfs_is_metadir_inode(ip) || XFS_IS_DQDETACHED(ip));

	if (XFS_IS_UQUOTA_ON(mp) && ip->i_udquot)
		xfs_trans_mod_ino_dquot(tp, ip, ip->i_udquot, field, delta);
	if (XFS_IS_GQUOTA_ON(mp) && ip->i_gdquot)
		xfs_trans_mod_ino_dquot(tp, ip, ip->i_gdquot, field, delta);
	if (XFS_IS_PQUOTA_ON(mp) && ip->i_pdquot)
		xfs_trans_mod_ino_dquot(tp, ip, ip->i_pdquot, field, delta);
}

STATIC struct xfs_dqtrx *
xfs_trans_get_dqtrx(
	struct xfs_trans	*tp,
	struct xfs_dquot	*dqp)
{
	int			i;
	struct xfs_dqtrx	*qa;

	switch (xfs_dquot_type(dqp)) {
	case XFS_DQTYPE_USER:
		qa = tp->t_dqinfo->dqs[XFS_QM_TRANS_USR];
		break;
	case XFS_DQTYPE_GROUP:
		qa = tp->t_dqinfo->dqs[XFS_QM_TRANS_GRP];
		break;
	case XFS_DQTYPE_PROJ:
		qa = tp->t_dqinfo->dqs[XFS_QM_TRANS_PRJ];
		break;
	default:
		return NULL;
	}

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
	struct xfs_trans	*tp,
	struct xfs_dquot	*dqp,
	uint			field,
	int64_t			delta)
{
	struct xfs_dqtrx	*qtrx;

	ASSERT(tp);
	ASSERT(XFS_IS_QUOTA_ON(tp->t_mountp));
	qtrx = NULL;

	if (!delta)
		return;

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

	trace_xfs_trans_mod_dquot_before(qtrx);
	trace_xfs_trans_mod_dquot(tp, dqp, field, delta);

	switch (field) {
	/* regular disk blk reservation */
	case XFS_TRANS_DQ_RES_BLKS:
		qtrx->qt_blk_res += delta;
		break;

	/* inode reservation */
	case XFS_TRANS_DQ_RES_INOS:
		qtrx->qt_ino_res += delta;
		break;

	/* disk blocks used. */
	case XFS_TRANS_DQ_BCOUNT:
		qtrx->qt_bcount_delta += delta;
		break;

	case XFS_TRANS_DQ_DELBCOUNT:
		qtrx->qt_delbcnt_delta += delta;
		break;

	/* Inode Count */
	case XFS_TRANS_DQ_ICOUNT:
		if (qtrx->qt_ino_res && delta > 0) {
			qtrx->qt_ino_res_used += delta;
			ASSERT(qtrx->qt_ino_res >= qtrx->qt_ino_res_used);
		}
		qtrx->qt_icount_delta += delta;
		break;

	/* rtblk reservation */
	case XFS_TRANS_DQ_RES_RTBLKS:
		qtrx->qt_rtblk_res += delta;
		break;

	/* rtblk count */
	case XFS_TRANS_DQ_RTBCOUNT:
		if (qtrx->qt_rtblk_res && delta > 0) {
			qtrx->qt_rtblk_res_used += delta;
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

	trace_xfs_trans_mod_dquot_after(qtrx);
}


/*
 * Given an array of dqtrx structures, lock all the dquots associated and join
 * them to the transaction, provided they have been modified.
 */
STATIC void
xfs_trans_dqlockedjoin(
	struct xfs_trans	*tp,
	struct xfs_dqtrx	*q)
{
	unsigned int		i;
	ASSERT(q[0].qt_dquot != NULL);
	if (q[1].qt_dquot == NULL) {
		xfs_dqlock(q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
	} else if (q[2].qt_dquot == NULL) {
		xfs_dqlock2(q[0].qt_dquot, q[1].qt_dquot);
		xfs_trans_dqjoin(tp, q[0].qt_dquot);
		xfs_trans_dqjoin(tp, q[1].qt_dquot);
	} else {
		xfs_dqlockn(q);
		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			if (q[i].qt_dquot == NULL)
				break;
			xfs_trans_dqjoin(tp, q[i].qt_dquot);
		}
	}
}

/* Apply dqtrx changes to the quota reservation counters. */
static inline void
xfs_apply_quota_reservation_deltas(
	struct xfs_dquot_res	*res,
	uint64_t		reserved,
	int64_t			res_used,
	int64_t			count_delta)
{
	if (reserved != 0) {
		/*
		 * Subtle math here: If reserved > res_used (the normal case),
		 * we're simply subtracting the unused transaction quota
		 * reservation from the dquot reservation.
		 *
		 * If, however, res_used > reserved, then we have allocated
		 * more quota blocks than were reserved for the transaction.
		 * We must add that excess to the dquot reservation since it
		 * tracks (usage + resv) and by definition we didn't reserve
		 * that excess.
		 */
		res->reserved -= abs(reserved - res_used);
	} else if (count_delta != 0) {
		/*
		 * These blks were never reserved, either inside a transaction
		 * or outside one (in a delayed allocation). Also, this isn't
		 * always a negative number since we sometimes deliberately
		 * skip quota reservations.
		 */
		res->reserved += count_delta;
	}
}

#ifdef CONFIG_XFS_LIVE_HOOKS
/* Call downstream hooks now that it's time to apply dquot deltas. */
static inline void
xfs_trans_apply_dquot_deltas_hook(
	struct xfs_trans		*tp,
	struct xfs_dquot		*dqp)
{
	if (xfs_hooks_switched_on(&xfs_dqtrx_hooks_switch)) {
		struct xfs_apply_dqtrx_params	p = {
			.tx_id		= (uintptr_t)tp,
			.q_type		= xfs_dquot_type(dqp),
			.q_id		= dqp->q_id,
		};
		struct xfs_quotainfo	*qi = tp->t_mountp->m_quotainfo;

		xfs_hooks_call(&qi->qi_apply_dqtrx_hooks,
				XFS_APPLY_DQTRX_COMMIT, &p);
	}
}
#else
# define xfs_trans_apply_dquot_deltas_hook(tp, dqp)	((void)0)
#endif /* CONFIG_XFS_LIVE_HOOKS */

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
	struct xfs_trans	*tp)
{
	int			i, j;
	struct xfs_dquot	*dqp;
	struct xfs_dqtrx	*qtrx, *qa;
	int64_t			totalbdelta;
	int64_t			totalrtbdelta;

	if (!tp->t_dqinfo)
		return;

	ASSERT(tp->t_dqinfo);
	for (j = 0; j < XFS_QM_TRANS_DQTYPES; j++) {
		qa = tp->t_dqinfo->dqs[j];
		if (qa[0].qt_dquot == NULL)
			continue;

		/*
		 * Lock all of the dquots and join them to the transaction.
		 */
		xfs_trans_dqlockedjoin(tp, qa);

		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			uint64_t	blk_res_used;

			qtrx = &qa[i];
			/*
			 * The array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;

			ASSERT(XFS_DQ_IS_LOCKED(dqp));

			xfs_trans_apply_dquot_deltas_hook(tp, dqp);

			/*
			 * adjust the actual number of blocks used
			 */

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

			if (totalbdelta != 0 || totalrtbdelta != 0 ||
			    qtrx->qt_icount_delta != 0) {
				trace_xfs_trans_apply_dquot_deltas_before(dqp);
				trace_xfs_trans_apply_dquot_deltas(qtrx);
			}

#ifdef DEBUG
			if (totalbdelta < 0)
				ASSERT(dqp->q_blk.count >= -totalbdelta);

			if (totalrtbdelta < 0)
				ASSERT(dqp->q_rtb.count >= -totalrtbdelta);

			if (qtrx->qt_icount_delta < 0)
				ASSERT(dqp->q_ino.count >= -qtrx->qt_icount_delta);
#endif
			if (totalbdelta)
				dqp->q_blk.count += totalbdelta;

			if (qtrx->qt_icount_delta)
				dqp->q_ino.count += qtrx->qt_icount_delta;

			if (totalrtbdelta)
				dqp->q_rtb.count += totalrtbdelta;

			if (totalbdelta != 0 || totalrtbdelta != 0 ||
			    qtrx->qt_icount_delta != 0)
				trace_xfs_trans_apply_dquot_deltas_after(dqp);

			/*
			 * Get any default limits in use.
			 * Start/reset the timer(s) if needed.
			 */
			if (dqp->q_id) {
				xfs_qm_adjust_dqlimits(dqp);
				xfs_qm_adjust_dqtimers(dqp);
			}

			dqp->q_flags |= XFS_DQFLAG_DIRTY;
			/*
			 * add this to the list of items to get logged
			 */
			xfs_trans_log_dquot(tp, dqp);
			/*
			 * Take off what's left of the original reservation.
			 * In case of delayed allocations, there's no
			 * reservation that a transaction structure knows of.
			 */
			blk_res_used = max_t(int64_t, 0, qtrx->qt_bcount_delta);
			xfs_apply_quota_reservation_deltas(&dqp->q_blk,
					qtrx->qt_blk_res, blk_res_used,
					qtrx->qt_bcount_delta);

			/*
			 * Adjust the RT reservation.
			 */
			xfs_apply_quota_reservation_deltas(&dqp->q_rtb,
					qtrx->qt_rtblk_res,
					qtrx->qt_rtblk_res_used,
					qtrx->qt_rtbcount_delta);

			/*
			 * Adjust the inode reservation.
			 */
			ASSERT(qtrx->qt_ino_res >= qtrx->qt_ino_res_used);
			xfs_apply_quota_reservation_deltas(&dqp->q_ino,
					qtrx->qt_ino_res,
					qtrx->qt_ino_res_used,
					qtrx->qt_icount_delta);

			ASSERT(dqp->q_blk.reserved >= dqp->q_blk.count);
			ASSERT(dqp->q_ino.reserved >= dqp->q_ino.count);
			ASSERT(dqp->q_rtb.reserved >= dqp->q_rtb.count);

			/*
			 * We've applied the count changes and given back
			 * whatever reservation we didn't use.  Zero out the
			 * dqtrx fields.
			 */
			qtrx->qt_blk_res = 0;
			qtrx->qt_bcount_delta = 0;
			qtrx->qt_delbcnt_delta = 0;

			qtrx->qt_rtblk_res = 0;
			qtrx->qt_rtblk_res_used = 0;
			qtrx->qt_rtbcount_delta = 0;
			qtrx->qt_delrtb_delta = 0;

			qtrx->qt_ino_res = 0;
			qtrx->qt_ino_res_used = 0;
			qtrx->qt_icount_delta = 0;
		}
	}
}

#ifdef CONFIG_XFS_LIVE_HOOKS
/* Call downstream hooks now that it's time to cancel dquot deltas. */
static inline void
xfs_trans_unreserve_and_mod_dquots_hook(
	struct xfs_trans		*tp,
	struct xfs_dquot		*dqp)
{
	if (xfs_hooks_switched_on(&xfs_dqtrx_hooks_switch)) {
		struct xfs_apply_dqtrx_params	p = {
			.tx_id		= (uintptr_t)tp,
			.q_type		= xfs_dquot_type(dqp),
			.q_id		= dqp->q_id,
		};
		struct xfs_quotainfo	*qi = tp->t_mountp->m_quotainfo;

		xfs_hooks_call(&qi->qi_apply_dqtrx_hooks,
				XFS_APPLY_DQTRX_UNRESERVE, &p);
	}
}
#else
# define xfs_trans_unreserve_and_mod_dquots_hook(tp, dqp)	((void)0)
#endif /* CONFIG_XFS_LIVE_HOOKS */

/*
 * Release the reservations, and adjust the dquots accordingly.
 * This is called only when the transaction is being aborted. If by
 * any chance we have done dquot modifications incore (ie. deltas) already,
 * we simply throw those away, since that's the expected behavior
 * when a transaction is curtailed without a commit.
 */
void
xfs_trans_unreserve_and_mod_dquots(
	struct xfs_trans	*tp,
	bool			already_locked)
{
	int			i, j;
	struct xfs_dquot	*dqp;
	struct xfs_dqtrx	*qtrx, *qa;
	bool			locked;

	if (!tp->t_dqinfo)
		return;

	for (j = 0; j < XFS_QM_TRANS_DQTYPES; j++) {
		qa = tp->t_dqinfo->dqs[j];

		for (i = 0; i < XFS_QM_TRANS_MAXDQS; i++) {
			qtrx = &qa[i];
			/*
			 * We assume that the array of dquots is filled
			 * sequentially, not sparsely.
			 */
			if ((dqp = qtrx->qt_dquot) == NULL)
				break;

			xfs_trans_unreserve_and_mod_dquots_hook(tp, dqp);

			/*
			 * Unreserve the original reservation. We don't care
			 * about the number of blocks used field, or deltas.
			 * Also we don't bother to zero the fields.
			 */
			locked = already_locked;
			if (qtrx->qt_blk_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = true;
				}
				dqp->q_blk.reserved -=
					(xfs_qcnt_t)qtrx->qt_blk_res;
			}
			if (qtrx->qt_ino_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = true;
				}
				dqp->q_ino.reserved -=
					(xfs_qcnt_t)qtrx->qt_ino_res;
			}

			if (qtrx->qt_rtblk_res) {
				if (!locked) {
					xfs_dqlock(dqp);
					locked = true;
				}
				dqp->q_rtb.reserved -=
					(xfs_qcnt_t)qtrx->qt_rtblk_res;
			}
			if (locked && !already_locked)
				xfs_dqunlock(dqp);

		}
	}
}

STATIC void
xfs_quota_warn(
	struct xfs_mount	*mp,
	struct xfs_dquot	*dqp,
	int			type)
{
	enum quota_type		qtype;

	switch (xfs_dquot_type(dqp)) {
	case XFS_DQTYPE_PROJ:
		qtype = PRJQUOTA;
		break;
	case XFS_DQTYPE_USER:
		qtype = USRQUOTA;
		break;
	case XFS_DQTYPE_GROUP:
		qtype = GRPQUOTA;
		break;
	default:
		return;
	}

	quota_send_warning(make_kqid(&init_user_ns, qtype, dqp->q_id),
			   mp->m_super->s_dev, type);
}

/*
 * Decide if we can make an additional reservation against a quota resource.
 * Returns an inode QUOTA_NL_ warning code and whether or not it's fatal.
 *
 * Note that we assume that the numeric difference between the inode and block
 * warning codes will always be 3 since it's userspace ABI now, and will never
 * decrease the quota reservation, so the *BELOW messages are irrelevant.
 */
static inline int
xfs_dqresv_check(
	struct xfs_dquot_res	*res,
	struct xfs_quota_limits	*qlim,
	int64_t			delta,
	bool			*fatal)
{
	xfs_qcnt_t		hardlimit = res->hardlimit;
	xfs_qcnt_t		softlimit = res->softlimit;
	xfs_qcnt_t		total_count = res->reserved + delta;

	BUILD_BUG_ON(QUOTA_NL_BHARDWARN     != QUOTA_NL_IHARDWARN + 3);
	BUILD_BUG_ON(QUOTA_NL_BSOFTLONGWARN != QUOTA_NL_ISOFTLONGWARN + 3);
	BUILD_BUG_ON(QUOTA_NL_BSOFTWARN     != QUOTA_NL_ISOFTWARN + 3);

	*fatal = false;
	if (delta <= 0)
		return QUOTA_NL_NOWARN;

	if (!hardlimit)
		hardlimit = qlim->hard;
	if (!softlimit)
		softlimit = qlim->soft;

	if (hardlimit && total_count > hardlimit) {
		*fatal = true;
		return QUOTA_NL_IHARDWARN;
	}

	if (softlimit && total_count > softlimit) {
		time64_t	now = ktime_get_real_seconds();

		if (res->timer != 0 && now > res->timer) {
			*fatal = true;
			return QUOTA_NL_ISOFTLONGWARN;
		}

		return QUOTA_NL_ISOFTWARN;
	}

	return QUOTA_NL_NOWARN;
}

/*
 * This reserves disk blocks and inodes against a dquot.
 * Flags indicate if the dquot is to be locked here and also
 * if the blk reservation is for RT or regular blocks.
 * Sending in XFS_QMOPT_FORCE_RES flag skips the quota check.
 */
STATIC int
xfs_trans_dqresv(
	struct xfs_trans	*tp,
	struct xfs_mount	*mp,
	struct xfs_dquot	*dqp,
	int64_t			nblks,
	long			ninos,
	uint			flags)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	struct xfs_def_quota	*defq;
	struct xfs_dquot_res	*blkres;
	struct xfs_quota_limits	*qlim;

	xfs_dqlock(dqp);

	defq = xfs_get_defquota(q, xfs_dquot_type(dqp));

	if (flags & XFS_TRANS_DQ_RES_BLKS) {
		blkres = &dqp->q_blk;
		qlim = &defq->blk;
	} else {
		blkres = &dqp->q_rtb;
		qlim = &defq->rtb;
	}

	if ((flags & XFS_QMOPT_FORCE_RES) == 0 && dqp->q_id &&
	    xfs_dquot_is_enforced(dqp)) {
		int		quota_nl;
		bool		fatal;

		/*
		 * dquot is locked already. See if we'd go over the hardlimit
		 * or exceed the timelimit if we'd reserve resources.
		 */
		quota_nl = xfs_dqresv_check(blkres, qlim, nblks, &fatal);
		if (quota_nl != QUOTA_NL_NOWARN) {
			/*
			 * Quota block warning codes are 3 more than the inode
			 * codes, which we check above.
			 */
			xfs_quota_warn(mp, dqp, quota_nl + 3);
			if (fatal)
				goto error_return;
		}

		quota_nl = xfs_dqresv_check(&dqp->q_ino, &defq->ino, ninos,
				&fatal);
		if (quota_nl != QUOTA_NL_NOWARN) {
			xfs_quota_warn(mp, dqp, quota_nl);
			if (fatal)
				goto error_return;
		}
	}

	/*
	 * Change the reservation, but not the actual usage.
	 * Note that q_blk.reserved = q_blk.count + resv
	 */
	blkres->reserved += (xfs_qcnt_t)nblks;
	dqp->q_ino.reserved += (xfs_qcnt_t)ninos;

	/*
	 * note the reservation amt in the trans struct too,
	 * so that the transaction knows how much was reserved by
	 * it against this particular dquot.
	 * We don't do this when we are reserving for a delayed allocation,
	 * because we don't have the luxury of a transaction envelope then.
	 */
	if (tp) {
		ASSERT(flags & XFS_QMOPT_RESBLK_MASK);
		xfs_trans_mod_dquot(tp, dqp, flags & XFS_QMOPT_RESBLK_MASK,
				    nblks);
		xfs_trans_mod_dquot(tp, dqp, XFS_TRANS_DQ_RES_INOS, ninos);
	}

	if (XFS_IS_CORRUPT(mp, dqp->q_blk.reserved < dqp->q_blk.count) ||
	    XFS_IS_CORRUPT(mp, dqp->q_rtb.reserved < dqp->q_rtb.count) ||
	    XFS_IS_CORRUPT(mp, dqp->q_ino.reserved < dqp->q_ino.count))
		goto error_corrupt;

	xfs_dqunlock(dqp);
	return 0;

error_return:
	xfs_dqunlock(dqp);
	if (xfs_dquot_type(dqp) == XFS_DQTYPE_PROJ)
		return -ENOSPC;
	return -EDQUOT;
error_corrupt:
	xfs_dqunlock(dqp);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
	xfs_fs_mark_sick(mp, XFS_SICK_FS_QUOTACHECK);
	return -EFSCORRUPTED;
}


/*
 * Given dquot(s), make disk block and/or inode reservations against them.
 * The fact that this does the reservation against user, group and
 * project quotas is important, because this follows a all-or-nothing
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
	struct xfs_trans	*tp,
	struct xfs_mount	*mp,
	struct xfs_dquot	*udqp,
	struct xfs_dquot	*gdqp,
	struct xfs_dquot	*pdqp,
	int64_t			nblks,
	long			ninos,
	uint			flags)
{
	int		error;

	if (!XFS_IS_QUOTA_ON(mp))
		return 0;

	ASSERT(flags & XFS_QMOPT_RESBLK_MASK);

	if (udqp) {
		error = xfs_trans_dqresv(tp, mp, udqp, nblks, ninos, flags);
		if (error)
			return error;
	}

	if (gdqp) {
		error = xfs_trans_dqresv(tp, mp, gdqp, nblks, ninos, flags);
		if (error)
			goto unwind_usr;
	}

	if (pdqp) {
		error = xfs_trans_dqresv(tp, mp, pdqp, nblks, ninos, flags);
		if (error)
			goto unwind_grp;
	}

	/*
	 * Didn't change anything critical, so, no need to log
	 */
	return 0;

unwind_grp:
	flags |= XFS_QMOPT_FORCE_RES;
	if (gdqp)
		xfs_trans_dqresv(tp, mp, gdqp, -nblks, -ninos, flags);
unwind_usr:
	flags |= XFS_QMOPT_FORCE_RES;
	if (udqp)
		xfs_trans_dqresv(tp, mp, udqp, -nblks, -ninos, flags);
	return error;
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
	int64_t			dblocks,
	int64_t			rblocks,
	bool			force)
{
	struct xfs_mount	*mp = ip->i_mount;
	unsigned int		qflags = 0;
	int			error;

	if (!XFS_IS_QUOTA_ON(mp))
		return 0;
	if (xfs_is_metadir_inode(ip))
		return 0;

	ASSERT(!xfs_is_quota_inode(&mp->m_sb, ip->i_ino));
	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);

	if (force)
		qflags |= XFS_QMOPT_FORCE_RES;

	/* Reserve data device quota against the inode's dquots. */
	error = xfs_trans_reserve_quota_bydquots(tp, mp, ip->i_udquot,
			ip->i_gdquot, ip->i_pdquot, dblocks, 0,
			XFS_QMOPT_RES_REGBLKS | qflags);
	if (error)
		return error;

	/* Do the same but for realtime blocks. */
	error = xfs_trans_reserve_quota_bydquots(tp, mp, ip->i_udquot,
			ip->i_gdquot, ip->i_pdquot, rblocks, 0,
			XFS_QMOPT_RES_RTBLKS | qflags);
	if (error) {
		xfs_trans_reserve_quota_bydquots(tp, mp, ip->i_udquot,
				ip->i_gdquot, ip->i_pdquot, -dblocks, 0,
				XFS_QMOPT_RES_REGBLKS);
		return error;
	}

	return 0;
}

/* Change the quota reservations for an inode creation activity. */
int
xfs_trans_reserve_quota_icreate(
	struct xfs_trans	*tp,
	struct xfs_dquot	*udqp,
	struct xfs_dquot	*gdqp,
	struct xfs_dquot	*pdqp,
	int64_t			dblocks)
{
	struct xfs_mount	*mp = tp->t_mountp;

	if (!XFS_IS_QUOTA_ON(mp))
		return 0;

	return xfs_trans_reserve_quota_bydquots(tp, mp, udqp, gdqp, pdqp,
			dblocks, 1, XFS_QMOPT_RES_REGBLKS);
}

STATIC void
xfs_trans_alloc_dqinfo(
	xfs_trans_t	*tp)
{
	tp->t_dqinfo = kmem_cache_zalloc(xfs_dqtrx_cache,
					 GFP_KERNEL | __GFP_NOFAIL);
}

void
xfs_trans_free_dqinfo(
	xfs_trans_t	*tp)
{
	if (!tp->t_dqinfo)
		return;
	kmem_cache_free(xfs_dqtrx_cache, tp->t_dqinfo);
	tp->t_dqinfo = NULL;
}

int
xfs_quota_reserve_blkres(
	struct xfs_inode	*ip,
	int64_t			blocks)
{
	if (XFS_IS_REALTIME_INODE(ip))
		return xfs_trans_reserve_quota_nblks(NULL, ip, 0, blocks,
				false);
	return xfs_trans_reserve_quota_nblks(NULL, ip, blocks, 0, false);
}
