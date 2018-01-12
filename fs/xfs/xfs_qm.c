/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_quota.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap_util.h"
#include "xfs_trans.h"
#include "xfs_trans_space.h"
#include "xfs_qm.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_cksum.h"

/*
 * The global quota manager. There is only one of these for the entire
 * system, _not_ one per file system. XQM keeps track of the overall
 * quota functionality, including maintaining the freelist and hash
 * tables of dquots.
 */
STATIC int	xfs_qm_init_quotainos(xfs_mount_t *);
STATIC int	xfs_qm_init_quotainfo(xfs_mount_t *);

STATIC void	xfs_qm_destroy_quotainos(xfs_quotainfo_t *qi);
STATIC void	xfs_qm_dqfree_one(struct xfs_dquot *dqp);
/*
 * We use the batch lookup interface to iterate over the dquots as it
 * currently is the only interface into the radix tree code that allows
 * fuzzy lookups instead of exact matches.  Holding the lock over multiple
 * operations is fine as all callers are used either during mount/umount
 * or quotaoff.
 */
#define XFS_DQ_LOOKUP_BATCH	32

STATIC int
xfs_qm_dquot_walk(
	struct xfs_mount	*mp,
	int			type,
	int			(*execute)(struct xfs_dquot *dqp, void *data),
	void			*data)
{
	struct xfs_quotainfo	*qi = mp->m_quotainfo;
	struct radix_tree_root	*tree = xfs_dquot_tree(qi, type);
	uint32_t		next_index;
	int			last_error = 0;
	int			skipped;
	int			nr_found;

restart:
	skipped = 0;
	next_index = 0;
	nr_found = 0;

	while (1) {
		struct xfs_dquot *batch[XFS_DQ_LOOKUP_BATCH];
		int		error = 0;
		int		i;

		mutex_lock(&qi->qi_tree_lock);
		nr_found = radix_tree_gang_lookup(tree, (void **)batch,
					next_index, XFS_DQ_LOOKUP_BATCH);
		if (!nr_found) {
			mutex_unlock(&qi->qi_tree_lock);
			break;
		}

		for (i = 0; i < nr_found; i++) {
			struct xfs_dquot *dqp = batch[i];

			next_index = be32_to_cpu(dqp->q_core.d_id) + 1;

			error = execute(batch[i], data);
			if (error == -EAGAIN) {
				skipped++;
				continue;
			}
			if (error && last_error != -EFSCORRUPTED)
				last_error = error;
		}

		mutex_unlock(&qi->qi_tree_lock);

		/* bail out if the filesystem is corrupted.  */
		if (last_error == -EFSCORRUPTED) {
			skipped = 0;
			break;
		}
		/* we're done if id overflows back to zero */
		if (!next_index)
			break;
	}

	if (skipped) {
		delay(1);
		goto restart;
	}

	return last_error;
}


/*
 * Purge a dquot from all tracking data structures and free it.
 */
STATIC int
xfs_qm_dqpurge(
	struct xfs_dquot	*dqp,
	void			*data)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_quotainfo	*qi = mp->m_quotainfo;

	xfs_dqlock(dqp);
	if ((dqp->dq_flags & XFS_DQ_FREEING) || dqp->q_nrefs != 0) {
		xfs_dqunlock(dqp);
		return -EAGAIN;
	}

	dqp->dq_flags |= XFS_DQ_FREEING;

	xfs_dqflock(dqp);

	/*
	 * If we are turning this type of quotas off, we don't care
	 * about the dirty metadata sitting in this dquot. OTOH, if
	 * we're unmounting, we do care, so we flush it and wait.
	 */
	if (XFS_DQ_IS_DIRTY(dqp)) {
		struct xfs_buf	*bp = NULL;
		int		error;

		/*
		 * We don't care about getting disk errors here. We need
		 * to purge this dquot anyway, so we go ahead regardless.
		 */
		error = xfs_qm_dqflush(dqp, &bp);
		if (error) {
			xfs_warn(mp, "%s: dquot %p flush failed",
				__func__, dqp);
		} else {
			error = xfs_bwrite(bp);
			xfs_buf_relse(bp);
		}
		xfs_dqflock(dqp);
	}

	ASSERT(atomic_read(&dqp->q_pincount) == 0);
	ASSERT(XFS_FORCED_SHUTDOWN(mp) ||
	       !(dqp->q_logitem.qli_item.li_flags & XFS_LI_IN_AIL));

	xfs_dqfunlock(dqp);
	xfs_dqunlock(dqp);

	radix_tree_delete(xfs_dquot_tree(qi, dqp->q_core.d_flags),
			  be32_to_cpu(dqp->q_core.d_id));
	qi->qi_dquots--;

	/*
	 * We move dquots to the freelist as soon as their reference count
	 * hits zero, so it really should be on the freelist here.
	 */
	ASSERT(!list_empty(&dqp->q_lru));
	list_lru_del(&qi->qi_lru, &dqp->q_lru);
	XFS_STATS_DEC(mp, xs_qm_dquot_unused);

	xfs_qm_dqdestroy(dqp);
	return 0;
}

/*
 * Purge the dquot cache.
 */
void
xfs_qm_dqpurge_all(
	struct xfs_mount	*mp,
	uint			flags)
{
	if (flags & XFS_QMOPT_UQUOTA)
		xfs_qm_dquot_walk(mp, XFS_DQ_USER, xfs_qm_dqpurge, NULL);
	if (flags & XFS_QMOPT_GQUOTA)
		xfs_qm_dquot_walk(mp, XFS_DQ_GROUP, xfs_qm_dqpurge, NULL);
	if (flags & XFS_QMOPT_PQUOTA)
		xfs_qm_dquot_walk(mp, XFS_DQ_PROJ, xfs_qm_dqpurge, NULL);
}

/*
 * Just destroy the quotainfo structure.
 */
void
xfs_qm_unmount(
	struct xfs_mount	*mp)
{
	if (mp->m_quotainfo) {
		xfs_qm_dqpurge_all(mp, XFS_QMOPT_QUOTALL);
		xfs_qm_destroy_quotainfo(mp);
	}
}

/*
 * Called from the vfsops layer.
 */
void
xfs_qm_unmount_quotas(
	xfs_mount_t	*mp)
{
	/*
	 * Release the dquots that root inode, et al might be holding,
	 * before we flush quotas and blow away the quotainfo structure.
	 */
	ASSERT(mp->m_rootip);
	xfs_qm_dqdetach(mp->m_rootip);
	if (mp->m_rbmip)
		xfs_qm_dqdetach(mp->m_rbmip);
	if (mp->m_rsumip)
		xfs_qm_dqdetach(mp->m_rsumip);

	/*
	 * Release the quota inodes.
	 */
	if (mp->m_quotainfo) {
		if (mp->m_quotainfo->qi_uquotaip) {
			IRELE(mp->m_quotainfo->qi_uquotaip);
			mp->m_quotainfo->qi_uquotaip = NULL;
		}
		if (mp->m_quotainfo->qi_gquotaip) {
			IRELE(mp->m_quotainfo->qi_gquotaip);
			mp->m_quotainfo->qi_gquotaip = NULL;
		}
		if (mp->m_quotainfo->qi_pquotaip) {
			IRELE(mp->m_quotainfo->qi_pquotaip);
			mp->m_quotainfo->qi_pquotaip = NULL;
		}
	}
}

STATIC int
xfs_qm_dqattach_one(
	xfs_inode_t	*ip,
	xfs_dqid_t	id,
	uint		type,
	uint		doalloc,
	xfs_dquot_t	**IO_idqpp)
{
	xfs_dquot_t	*dqp;
	int		error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	error = 0;

	/*
	 * See if we already have it in the inode itself. IO_idqpp is &i_udquot
	 * or &i_gdquot. This made the code look weird, but made the logic a lot
	 * simpler.
	 */
	dqp = *IO_idqpp;
	if (dqp) {
		trace_xfs_dqattach_found(dqp);
		return 0;
	}

	/*
	 * Find the dquot from somewhere. This bumps the reference count of
	 * dquot and returns it locked.  This can return ENOENT if dquot didn't
	 * exist on disk and we didn't ask it to allocate; ESRCH if quotas got
	 * turned off suddenly.
	 */
	error = xfs_qm_dqget(ip->i_mount, ip, id, type,
			     doalloc | XFS_QMOPT_DOWARN, &dqp);
	if (error)
		return error;

	trace_xfs_dqattach_get(dqp);

	/*
	 * dqget may have dropped and re-acquired the ilock, but it guarantees
	 * that the dquot returned is the one that should go in the inode.
	 */
	*IO_idqpp = dqp;
	xfs_dqunlock(dqp);
	return 0;
}

static bool
xfs_qm_need_dqattach(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	if (!XFS_IS_QUOTA_RUNNING(mp))
		return false;
	if (!XFS_IS_QUOTA_ON(mp))
		return false;
	if (!XFS_NOT_DQATTACHED(mp, ip))
		return false;
	if (xfs_is_quota_inode(&mp->m_sb, ip->i_ino))
		return false;
	return true;
}

/*
 * Given a locked inode, attach dquot(s) to it, taking U/G/P-QUOTAON
 * into account.
 * If XFS_QMOPT_DQALLOC, the dquot(s) will be allocated if needed.
 * Inode may get unlocked and relocked in here, and the caller must deal with
 * the consequences.
 */
int
xfs_qm_dqattach_locked(
	xfs_inode_t	*ip,
	uint		flags)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error = 0;

	if (!xfs_qm_need_dqattach(ip))
		return 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	if (XFS_IS_UQUOTA_ON(mp) && !ip->i_udquot) {
		error = xfs_qm_dqattach_one(ip, ip->i_d.di_uid, XFS_DQ_USER,
						flags & XFS_QMOPT_DQALLOC,
						&ip->i_udquot);
		if (error)
			goto done;
		ASSERT(ip->i_udquot);
	}

	if (XFS_IS_GQUOTA_ON(mp) && !ip->i_gdquot) {
		error = xfs_qm_dqattach_one(ip, ip->i_d.di_gid, XFS_DQ_GROUP,
						flags & XFS_QMOPT_DQALLOC,
						&ip->i_gdquot);
		if (error)
			goto done;
		ASSERT(ip->i_gdquot);
	}

	if (XFS_IS_PQUOTA_ON(mp) && !ip->i_pdquot) {
		error = xfs_qm_dqattach_one(ip, xfs_get_projid(ip), XFS_DQ_PROJ,
						flags & XFS_QMOPT_DQALLOC,
						&ip->i_pdquot);
		if (error)
			goto done;
		ASSERT(ip->i_pdquot);
	}

done:
	/*
	 * Don't worry about the dquots that we may have attached before any
	 * error - they'll get detached later if it has not already been done.
	 */
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	return error;
}

int
xfs_qm_dqattach(
	struct xfs_inode	*ip,
	uint			flags)
{
	int			error;

	if (!xfs_qm_need_dqattach(ip))
		return 0;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	error = xfs_qm_dqattach_locked(ip, flags);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	return error;
}

/*
 * Release dquots (and their references) if any.
 * The inode should be locked EXCL except when this's called by
 * xfs_ireclaim.
 */
void
xfs_qm_dqdetach(
	xfs_inode_t	*ip)
{
	if (!(ip->i_udquot || ip->i_gdquot || ip->i_pdquot))
		return;

	trace_xfs_dquot_dqdetach(ip);

	ASSERT(!xfs_is_quota_inode(&ip->i_mount->m_sb, ip->i_ino));
	if (ip->i_udquot) {
		xfs_qm_dqrele(ip->i_udquot);
		ip->i_udquot = NULL;
	}
	if (ip->i_gdquot) {
		xfs_qm_dqrele(ip->i_gdquot);
		ip->i_gdquot = NULL;
	}
	if (ip->i_pdquot) {
		xfs_qm_dqrele(ip->i_pdquot);
		ip->i_pdquot = NULL;
	}
}

struct xfs_qm_isolate {
	struct list_head	buffers;
	struct list_head	dispose;
};

static enum lru_status
xfs_qm_dquot_isolate(
	struct list_head	*item,
	struct list_lru_one	*lru,
	spinlock_t		*lru_lock,
	void			*arg)
		__releases(lru_lock) __acquires(lru_lock)
{
	struct xfs_dquot	*dqp = container_of(item,
						struct xfs_dquot, q_lru);
	struct xfs_qm_isolate	*isol = arg;

	if (!xfs_dqlock_nowait(dqp))
		goto out_miss_busy;

	/*
	 * This dquot has acquired a reference in the meantime remove it from
	 * the freelist and try again.
	 */
	if (dqp->q_nrefs) {
		xfs_dqunlock(dqp);
		XFS_STATS_INC(dqp->q_mount, xs_qm_dqwants);

		trace_xfs_dqreclaim_want(dqp);
		list_lru_isolate(lru, &dqp->q_lru);
		XFS_STATS_DEC(dqp->q_mount, xs_qm_dquot_unused);
		return LRU_REMOVED;
	}

	/*
	 * If the dquot is dirty, flush it. If it's already being flushed, just
	 * skip it so there is time for the IO to complete before we try to
	 * reclaim it again on the next LRU pass.
	 */
	if (!xfs_dqflock_nowait(dqp)) {
		xfs_dqunlock(dqp);
		goto out_miss_busy;
	}

	if (XFS_DQ_IS_DIRTY(dqp)) {
		struct xfs_buf	*bp = NULL;
		int		error;

		trace_xfs_dqreclaim_dirty(dqp);

		/* we have to drop the LRU lock to flush the dquot */
		spin_unlock(lru_lock);

		error = xfs_qm_dqflush(dqp, &bp);
		if (error) {
			xfs_warn(dqp->q_mount, "%s: dquot %p flush failed",
				 __func__, dqp);
			goto out_unlock_dirty;
		}

		xfs_buf_delwri_queue(bp, &isol->buffers);
		xfs_buf_relse(bp);
		goto out_unlock_dirty;
	}
	xfs_dqfunlock(dqp);

	/*
	 * Prevent lookups now that we are past the point of no return.
	 */
	dqp->dq_flags |= XFS_DQ_FREEING;
	xfs_dqunlock(dqp);

	ASSERT(dqp->q_nrefs == 0);
	list_lru_isolate_move(lru, &dqp->q_lru, &isol->dispose);
	XFS_STATS_DEC(dqp->q_mount, xs_qm_dquot_unused);
	trace_xfs_dqreclaim_done(dqp);
	XFS_STATS_INC(dqp->q_mount, xs_qm_dqreclaims);
	return LRU_REMOVED;

out_miss_busy:
	trace_xfs_dqreclaim_busy(dqp);
	XFS_STATS_INC(dqp->q_mount, xs_qm_dqreclaim_misses);
	return LRU_SKIP;

out_unlock_dirty:
	trace_xfs_dqreclaim_busy(dqp);
	XFS_STATS_INC(dqp->q_mount, xs_qm_dqreclaim_misses);
	xfs_dqunlock(dqp);
	spin_lock(lru_lock);
	return LRU_RETRY;
}

static unsigned long
xfs_qm_shrink_scan(
	struct shrinker		*shrink,
	struct shrink_control	*sc)
{
	struct xfs_quotainfo	*qi = container_of(shrink,
					struct xfs_quotainfo, qi_shrinker);
	struct xfs_qm_isolate	isol;
	unsigned long		freed;
	int			error;

	if ((sc->gfp_mask & (__GFP_FS|__GFP_DIRECT_RECLAIM)) != (__GFP_FS|__GFP_DIRECT_RECLAIM))
		return 0;

	INIT_LIST_HEAD(&isol.buffers);
	INIT_LIST_HEAD(&isol.dispose);

	freed = list_lru_shrink_walk(&qi->qi_lru, sc,
				     xfs_qm_dquot_isolate, &isol);

	error = xfs_buf_delwri_submit(&isol.buffers);
	if (error)
		xfs_warn(NULL, "%s: dquot reclaim failed", __func__);

	while (!list_empty(&isol.dispose)) {
		struct xfs_dquot	*dqp;

		dqp = list_first_entry(&isol.dispose, struct xfs_dquot, q_lru);
		list_del_init(&dqp->q_lru);
		xfs_qm_dqfree_one(dqp);
	}

	return freed;
}

static unsigned long
xfs_qm_shrink_count(
	struct shrinker		*shrink,
	struct shrink_control	*sc)
{
	struct xfs_quotainfo	*qi = container_of(shrink,
					struct xfs_quotainfo, qi_shrinker);

	return list_lru_shrink_count(&qi->qi_lru, sc);
}

STATIC void
xfs_qm_set_defquota(
	xfs_mount_t	*mp,
	uint		type,
	xfs_quotainfo_t	*qinf)
{
	xfs_dquot_t		*dqp;
	struct xfs_def_quota    *defq;
	int			error;

	error = xfs_qm_dqread(mp, 0, type, XFS_QMOPT_DOWARN, &dqp);

	if (!error) {
		xfs_disk_dquot_t        *ddqp = &dqp->q_core;

		defq = xfs_get_defquota(dqp, qinf);

		/*
		 * Timers and warnings have been already set, let's just set the
		 * default limits for this quota type
		 */
		defq->bhardlimit = be64_to_cpu(ddqp->d_blk_hardlimit);
		defq->bsoftlimit = be64_to_cpu(ddqp->d_blk_softlimit);
		defq->ihardlimit = be64_to_cpu(ddqp->d_ino_hardlimit);
		defq->isoftlimit = be64_to_cpu(ddqp->d_ino_softlimit);
		defq->rtbhardlimit = be64_to_cpu(ddqp->d_rtb_hardlimit);
		defq->rtbsoftlimit = be64_to_cpu(ddqp->d_rtb_softlimit);
		xfs_qm_dqdestroy(dqp);
	}
}

/*
 * This initializes all the quota information that's kept in the
 * mount structure
 */
STATIC int
xfs_qm_init_quotainfo(
	xfs_mount_t	*mp)
{
	xfs_quotainfo_t *qinf;
	int		error;
	xfs_dquot_t	*dqp;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	qinf = mp->m_quotainfo = kmem_zalloc(sizeof(xfs_quotainfo_t), KM_SLEEP);

	error = list_lru_init(&qinf->qi_lru);
	if (error)
		goto out_free_qinf;

	/*
	 * See if quotainodes are setup, and if not, allocate them,
	 * and change the superblock accordingly.
	 */
	error = xfs_qm_init_quotainos(mp);
	if (error)
		goto out_free_lru;

	INIT_RADIX_TREE(&qinf->qi_uquota_tree, GFP_NOFS);
	INIT_RADIX_TREE(&qinf->qi_gquota_tree, GFP_NOFS);
	INIT_RADIX_TREE(&qinf->qi_pquota_tree, GFP_NOFS);
	mutex_init(&qinf->qi_tree_lock);

	/* mutex used to serialize quotaoffs */
	mutex_init(&qinf->qi_quotaofflock);

	/* Precalc some constants */
	qinf->qi_dqchunklen = XFS_FSB_TO_BB(mp, XFS_DQUOT_CLUSTER_SIZE_FSB);
	qinf->qi_dqperchunk = xfs_calc_dquots_per_chunk(qinf->qi_dqchunklen);

	mp->m_qflags |= (mp->m_sb.sb_qflags & XFS_ALL_QUOTA_CHKD);

	/*
	 * We try to get the limits from the superuser's limits fields.
	 * This is quite hacky, but it is standard quota practice.
	 *
	 * Since we may not have done a quotacheck by this point, just read
	 * the dquot without attaching it to any hashtables or lists.
	 *
	 * Timers and warnings are globally set by the first timer found in
	 * user/group/proj quota types, otherwise a default value is used.
	 * This should be split into different fields per quota type.
	 */
	error = xfs_qm_dqread(mp, 0,
			XFS_IS_UQUOTA_RUNNING(mp) ? XFS_DQ_USER :
			 (XFS_IS_GQUOTA_RUNNING(mp) ? XFS_DQ_GROUP :
			  XFS_DQ_PROJ),
			XFS_QMOPT_DOWARN, &dqp);

	if (!error) {
		xfs_disk_dquot_t	*ddqp = &dqp->q_core;

		/*
		 * The warnings and timers set the grace period given to
		 * a user or group before he or she can not perform any
		 * more writing. If it is zero, a default is used.
		 */
		qinf->qi_btimelimit = ddqp->d_btimer ?
			be32_to_cpu(ddqp->d_btimer) : XFS_QM_BTIMELIMIT;
		qinf->qi_itimelimit = ddqp->d_itimer ?
			be32_to_cpu(ddqp->d_itimer) : XFS_QM_ITIMELIMIT;
		qinf->qi_rtbtimelimit = ddqp->d_rtbtimer ?
			be32_to_cpu(ddqp->d_rtbtimer) : XFS_QM_RTBTIMELIMIT;
		qinf->qi_bwarnlimit = ddqp->d_bwarns ?
			be16_to_cpu(ddqp->d_bwarns) : XFS_QM_BWARNLIMIT;
		qinf->qi_iwarnlimit = ddqp->d_iwarns ?
			be16_to_cpu(ddqp->d_iwarns) : XFS_QM_IWARNLIMIT;
		qinf->qi_rtbwarnlimit = ddqp->d_rtbwarns ?
			be16_to_cpu(ddqp->d_rtbwarns) : XFS_QM_RTBWARNLIMIT;
		xfs_qm_dqdestroy(dqp);
	} else {
		qinf->qi_btimelimit = XFS_QM_BTIMELIMIT;
		qinf->qi_itimelimit = XFS_QM_ITIMELIMIT;
		qinf->qi_rtbtimelimit = XFS_QM_RTBTIMELIMIT;
		qinf->qi_bwarnlimit = XFS_QM_BWARNLIMIT;
		qinf->qi_iwarnlimit = XFS_QM_IWARNLIMIT;
		qinf->qi_rtbwarnlimit = XFS_QM_RTBWARNLIMIT;
	}

	if (XFS_IS_UQUOTA_RUNNING(mp))
		xfs_qm_set_defquota(mp, XFS_DQ_USER, qinf);
	if (XFS_IS_GQUOTA_RUNNING(mp))
		xfs_qm_set_defquota(mp, XFS_DQ_GROUP, qinf);
	if (XFS_IS_PQUOTA_RUNNING(mp))
		xfs_qm_set_defquota(mp, XFS_DQ_PROJ, qinf);

	qinf->qi_shrinker.count_objects = xfs_qm_shrink_count;
	qinf->qi_shrinker.scan_objects = xfs_qm_shrink_scan;
	qinf->qi_shrinker.seeks = DEFAULT_SEEKS;
	qinf->qi_shrinker.flags = SHRINKER_NUMA_AWARE;

	error = register_shrinker(&qinf->qi_shrinker);
	if (error)
		goto out_free_inos;

	return 0;

out_free_inos:
	mutex_destroy(&qinf->qi_quotaofflock);
	mutex_destroy(&qinf->qi_tree_lock);
	xfs_qm_destroy_quotainos(qinf);
out_free_lru:
	list_lru_destroy(&qinf->qi_lru);
out_free_qinf:
	kmem_free(qinf);
	mp->m_quotainfo = NULL;
	return error;
}

/*
 * Gets called when unmounting a filesystem or when all quotas get
 * turned off.
 * This purges the quota inodes, destroys locks and frees itself.
 */
void
xfs_qm_destroy_quotainfo(
	xfs_mount_t	*mp)
{
	xfs_quotainfo_t *qi;

	qi = mp->m_quotainfo;
	ASSERT(qi != NULL);

	unregister_shrinker(&qi->qi_shrinker);
	list_lru_destroy(&qi->qi_lru);
	xfs_qm_destroy_quotainos(qi);
	mutex_destroy(&qi->qi_tree_lock);
	mutex_destroy(&qi->qi_quotaofflock);
	kmem_free(qi);
	mp->m_quotainfo = NULL;
}

/*
 * Create an inode and return with a reference already taken, but unlocked
 * This is how we create quota inodes
 */
STATIC int
xfs_qm_qino_alloc(
	xfs_mount_t	*mp,
	xfs_inode_t	**ip,
	uint		flags)
{
	xfs_trans_t	*tp;
	int		error;
	int		committed;
	bool		need_alloc = true;

	*ip = NULL;
	/*
	 * With superblock that doesn't have separate pquotino, we
	 * share an inode between gquota and pquota. If the on-disk
	 * superblock has GQUOTA and the filesystem is now mounted
	 * with PQUOTA, just use sb_gquotino for sb_pquotino and
	 * vice-versa.
	 */
	if (!xfs_sb_version_has_pquotino(&mp->m_sb) &&
			(flags & (XFS_QMOPT_PQUOTA|XFS_QMOPT_GQUOTA))) {
		xfs_ino_t ino = NULLFSINO;

		if ((flags & XFS_QMOPT_PQUOTA) &&
			     (mp->m_sb.sb_gquotino != NULLFSINO)) {
			ino = mp->m_sb.sb_gquotino;
			ASSERT(mp->m_sb.sb_pquotino == NULLFSINO);
		} else if ((flags & XFS_QMOPT_GQUOTA) &&
			     (mp->m_sb.sb_pquotino != NULLFSINO)) {
			ino = mp->m_sb.sb_pquotino;
			ASSERT(mp->m_sb.sb_gquotino == NULLFSINO);
		}
		if (ino != NULLFSINO) {
			error = xfs_iget(mp, NULL, ino, 0, 0, ip);
			if (error)
				return error;
			mp->m_sb.sb_gquotino = NULLFSINO;
			mp->m_sb.sb_pquotino = NULLFSINO;
			need_alloc = false;
		}
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_create,
			XFS_QM_QINOCREATE_SPACE_RES(mp), 0, 0, &tp);
	if (error)
		return error;

	if (need_alloc) {
		error = xfs_dir_ialloc(&tp, NULL, S_IFREG, 1, 0, 0, ip,
				&committed);
		if (error) {
			xfs_trans_cancel(tp);
			return error;
		}
	}

	/*
	 * Make the changes in the superblock, and log those too.
	 * sbfields arg may contain fields other than *QUOTINO;
	 * VERSIONNUM for example.
	 */
	spin_lock(&mp->m_sb_lock);
	if (flags & XFS_QMOPT_SBVERSION) {
		ASSERT(!xfs_sb_version_hasquota(&mp->m_sb));

		xfs_sb_version_addquota(&mp->m_sb);
		mp->m_sb.sb_uquotino = NULLFSINO;
		mp->m_sb.sb_gquotino = NULLFSINO;
		mp->m_sb.sb_pquotino = NULLFSINO;

		/* qflags will get updated fully _after_ quotacheck */
		mp->m_sb.sb_qflags = mp->m_qflags & XFS_ALL_QUOTA_ACCT;
	}
	if (flags & XFS_QMOPT_UQUOTA)
		mp->m_sb.sb_uquotino = (*ip)->i_ino;
	else if (flags & XFS_QMOPT_GQUOTA)
		mp->m_sb.sb_gquotino = (*ip)->i_ino;
	else
		mp->m_sb.sb_pquotino = (*ip)->i_ino;
	spin_unlock(&mp->m_sb_lock);
	xfs_log_sb(tp);

	error = xfs_trans_commit(tp);
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		xfs_alert(mp, "%s failed (error %d)!", __func__, error);
	}
	if (need_alloc)
		xfs_finish_inode_setup(*ip);
	return error;
}


STATIC void
xfs_qm_reset_dqcounts(
	xfs_mount_t	*mp,
	xfs_buf_t	*bp,
	xfs_dqid_t	id,
	uint		type)
{
	struct xfs_dqblk	*dqb;
	int			j;

	trace_xfs_reset_dqcounts(bp, _RET_IP_);

	/*
	 * Reset all counters and timers. They'll be
	 * started afresh by xfs_qm_quotacheck.
	 */
#ifdef DEBUG
	j = (int)XFS_FSB_TO_B(mp, XFS_DQUOT_CLUSTER_SIZE_FSB) /
		sizeof(xfs_dqblk_t);
	ASSERT(mp->m_quotainfo->qi_dqperchunk == j);
#endif
	dqb = bp->b_addr;
	for (j = 0; j < mp->m_quotainfo->qi_dqperchunk; j++) {
		struct xfs_disk_dquot	*ddq;

		ddq = (struct xfs_disk_dquot *)&dqb[j];

		/*
		 * Do a sanity check, and if needed, repair the dqblk. Don't
		 * output any warnings because it's perfectly possible to
		 * find uninitialised dquot blks. See comment in xfs_dqcheck.
		 */
		xfs_dqcheck(mp, ddq, id+j, type, XFS_QMOPT_DQREPAIR,
			    "xfs_quotacheck");
		/*
		 * Reset type in case we are reusing group quota file for
		 * project quotas or vice versa
		 */
		ddq->d_flags = type;
		ddq->d_bcount = 0;
		ddq->d_icount = 0;
		ddq->d_rtbcount = 0;
		ddq->d_btimer = 0;
		ddq->d_itimer = 0;
		ddq->d_rtbtimer = 0;
		ddq->d_bwarns = 0;
		ddq->d_iwarns = 0;
		ddq->d_rtbwarns = 0;

		if (xfs_sb_version_hascrc(&mp->m_sb)) {
			xfs_update_cksum((char *)&dqb[j],
					 sizeof(struct xfs_dqblk),
					 XFS_DQUOT_CRC_OFF);
		}
	}
}

STATIC int
xfs_qm_dqiter_bufs(
	struct xfs_mount	*mp,
	xfs_dqid_t		firstid,
	xfs_fsblock_t		bno,
	xfs_filblks_t		blkcnt,
	uint			flags,
	struct list_head	*buffer_list)
{
	struct xfs_buf		*bp;
	int			error;
	int			type;

	ASSERT(blkcnt > 0);
	type = flags & XFS_QMOPT_UQUOTA ? XFS_DQ_USER :
		(flags & XFS_QMOPT_PQUOTA ? XFS_DQ_PROJ : XFS_DQ_GROUP);
	error = 0;

	/*
	 * Blkcnt arg can be a very big number, and might even be
	 * larger than the log itself. So, we have to break it up into
	 * manageable-sized transactions.
	 * Note that we don't start a permanent transaction here; we might
	 * not be able to get a log reservation for the whole thing up front,
	 * and we don't really care to either, because we just discard
	 * everything if we were to crash in the middle of this loop.
	 */
	while (blkcnt--) {
		error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp,
			      XFS_FSB_TO_DADDR(mp, bno),
			      mp->m_quotainfo->qi_dqchunklen, 0, &bp,
			      &xfs_dquot_buf_ops);

		/*
		 * CRC and validation errors will return a EFSCORRUPTED here. If
		 * this occurs, re-read without CRC validation so that we can
		 * repair the damage via xfs_qm_reset_dqcounts(). This process
		 * will leave a trace in the log indicating corruption has
		 * been detected.
		 */
		if (error == -EFSCORRUPTED) {
			error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp,
				      XFS_FSB_TO_DADDR(mp, bno),
				      mp->m_quotainfo->qi_dqchunklen, 0, &bp,
				      NULL);
		}

		if (error)
			break;

		/*
		 * A corrupt buffer might not have a verifier attached, so
		 * make sure we have the correct one attached before writeback
		 * occurs.
		 */
		bp->b_ops = &xfs_dquot_buf_ops;
		xfs_qm_reset_dqcounts(mp, bp, firstid, type);
		xfs_buf_delwri_queue(bp, buffer_list);
		xfs_buf_relse(bp);

		/* goto the next block. */
		bno++;
		firstid += mp->m_quotainfo->qi_dqperchunk;
	}

	return error;
}

/*
 * Iterate over all allocated USR/GRP/PRJ dquots in the system, calling a
 * caller supplied function for every chunk of dquots that we find.
 */
STATIC int
xfs_qm_dqiterate(
	struct xfs_mount	*mp,
	struct xfs_inode	*qip,
	uint			flags,
	struct list_head	*buffer_list)
{
	struct xfs_bmbt_irec	*map;
	int			i, nmaps;	/* number of map entries */
	int			error;		/* return value */
	xfs_fileoff_t		lblkno;
	xfs_filblks_t		maxlblkcnt;
	xfs_dqid_t		firstid;
	xfs_fsblock_t		rablkno;
	xfs_filblks_t		rablkcnt;

	error = 0;
	/*
	 * This looks racy, but we can't keep an inode lock across a
	 * trans_reserve. But, this gets called during quotacheck, and that
	 * happens only at mount time which is single threaded.
	 */
	if (qip->i_d.di_nblocks == 0)
		return 0;

	map = kmem_alloc(XFS_DQITER_MAP_SIZE * sizeof(*map), KM_SLEEP);

	lblkno = 0;
	maxlblkcnt = XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	do {
		uint		lock_mode;

		nmaps = XFS_DQITER_MAP_SIZE;
		/*
		 * We aren't changing the inode itself. Just changing
		 * some of its data. No new blocks are added here, and
		 * the inode is never added to the transaction.
		 */
		lock_mode = xfs_ilock_data_map_shared(qip);
		error = xfs_bmapi_read(qip, lblkno, maxlblkcnt - lblkno,
				       map, &nmaps, 0);
		xfs_iunlock(qip, lock_mode);
		if (error)
			break;

		ASSERT(nmaps <= XFS_DQITER_MAP_SIZE);
		for (i = 0; i < nmaps; i++) {
			ASSERT(map[i].br_startblock != DELAYSTARTBLOCK);
			ASSERT(map[i].br_blockcount);


			lblkno += map[i].br_blockcount;

			if (map[i].br_startblock == HOLESTARTBLOCK)
				continue;

			firstid = (xfs_dqid_t) map[i].br_startoff *
				mp->m_quotainfo->qi_dqperchunk;
			/*
			 * Do a read-ahead on the next extent.
			 */
			if ((i+1 < nmaps) &&
			    (map[i+1].br_startblock != HOLESTARTBLOCK)) {
				rablkcnt =  map[i+1].br_blockcount;
				rablkno = map[i+1].br_startblock;
				while (rablkcnt--) {
					xfs_buf_readahead(mp->m_ddev_targp,
					       XFS_FSB_TO_DADDR(mp, rablkno),
					       mp->m_quotainfo->qi_dqchunklen,
					       &xfs_dquot_buf_ops);
					rablkno++;
				}
			}
			/*
			 * Iterate thru all the blks in the extent and
			 * reset the counters of all the dquots inside them.
			 */
			error = xfs_qm_dqiter_bufs(mp, firstid,
						   map[i].br_startblock,
						   map[i].br_blockcount,
						   flags, buffer_list);
			if (error)
				goto out;
		}
	} while (nmaps > 0);

out:
	kmem_free(map);
	return error;
}

/*
 * Called by dqusage_adjust in doing a quotacheck.
 *
 * Given the inode, and a dquot id this updates both the incore dqout as well
 * as the buffer copy. This is so that once the quotacheck is done, we can
 * just log all the buffers, as opposed to logging numerous updates to
 * individual dquots.
 */
STATIC int
xfs_qm_quotacheck_dqadjust(
	struct xfs_inode	*ip,
	xfs_dqid_t		id,
	uint			type,
	xfs_qcnt_t		nblks,
	xfs_qcnt_t		rtblks)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_dquot	*dqp;
	int			error;

	error = xfs_qm_dqget(mp, ip, id, type,
			     XFS_QMOPT_DQALLOC | XFS_QMOPT_DOWARN, &dqp);
	if (error) {
		/*
		 * Shouldn't be able to turn off quotas here.
		 */
		ASSERT(error != -ESRCH);
		ASSERT(error != -ENOENT);
		return error;
	}

	trace_xfs_dqadjust(dqp);

	/*
	 * Adjust the inode count and the block count to reflect this inode's
	 * resource usage.
	 */
	be64_add_cpu(&dqp->q_core.d_icount, 1);
	dqp->q_res_icount++;
	if (nblks) {
		be64_add_cpu(&dqp->q_core.d_bcount, nblks);
		dqp->q_res_bcount += nblks;
	}
	if (rtblks) {
		be64_add_cpu(&dqp->q_core.d_rtbcount, rtblks);
		dqp->q_res_rtbcount += rtblks;
	}

	/*
	 * Set default limits, adjust timers (since we changed usages)
	 *
	 * There are no timers for the default values set in the root dquot.
	 */
	if (dqp->q_core.d_id) {
		xfs_qm_adjust_dqlimits(mp, dqp);
		xfs_qm_adjust_dqtimers(mp, &dqp->q_core);
	}

	dqp->dq_flags |= XFS_DQ_DIRTY;
	xfs_qm_dqput(dqp);
	return 0;
}

/*
 * callback routine supplied to bulkstat(). Given an inumber, find its
 * dquots and update them to account for resources taken by that inode.
 */
/* ARGSUSED */
STATIC int
xfs_qm_dqusage_adjust(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		__user *buffer,	/* not used */
	int		ubsize,		/* not used */
	int		*ubused,	/* not used */
	int		*res)		/* result code value */
{
	xfs_inode_t	*ip;
	xfs_qcnt_t	nblks;
	xfs_filblks_t	rtblks = 0;	/* total rt blks */
	int		error;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * rootino must have its resources accounted for, not so with the quota
	 * inodes.
	 */
	if (xfs_is_quota_inode(&mp->m_sb, ino)) {
		*res = BULKSTAT_RV_NOTHING;
		return -EINVAL;
	}

	/*
	 * We don't _need_ to take the ilock EXCL. However, the xfs_qm_dqget
	 * interface expects the inode to be exclusively locked because that's
	 * the case in all other instances. It's OK that we do this because
	 * quotacheck is done only at mount time.
	 */
	error = xfs_iget(mp, NULL, ino, XFS_IGET_DONTCACHE, XFS_ILOCK_EXCL,
			 &ip);
	if (error) {
		*res = BULKSTAT_RV_NOTHING;
		return error;
	}

	ASSERT(ip->i_delayed_blks == 0);

	if (XFS_IS_REALTIME_INODE(ip)) {
		struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);

		if (!(ifp->if_flags & XFS_IFEXTENTS)) {
			error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
			if (error)
				goto error0;
		}

		xfs_bmap_count_leaves(ifp, &rtblks);
	}

	nblks = (xfs_qcnt_t)ip->i_d.di_nblocks - rtblks;

	/*
	 * Add the (disk blocks and inode) resources occupied by this
	 * inode to its dquots. We do this adjustment in the incore dquot,
	 * and also copy the changes to its buffer.
	 * We don't care about putting these changes in a transaction
	 * envelope because if we crash in the middle of a 'quotacheck'
	 * we have to start from the beginning anyway.
	 * Once we're done, we'll log all the dquot bufs.
	 *
	 * The *QUOTA_ON checks below may look pretty racy, but quotachecks
	 * and quotaoffs don't race. (Quotachecks happen at mount time only).
	 */
	if (XFS_IS_UQUOTA_ON(mp)) {
		error = xfs_qm_quotacheck_dqadjust(ip, ip->i_d.di_uid,
						   XFS_DQ_USER, nblks, rtblks);
		if (error)
			goto error0;
	}

	if (XFS_IS_GQUOTA_ON(mp)) {
		error = xfs_qm_quotacheck_dqadjust(ip, ip->i_d.di_gid,
						   XFS_DQ_GROUP, nblks, rtblks);
		if (error)
			goto error0;
	}

	if (XFS_IS_PQUOTA_ON(mp)) {
		error = xfs_qm_quotacheck_dqadjust(ip, xfs_get_projid(ip),
						   XFS_DQ_PROJ, nblks, rtblks);
		if (error)
			goto error0;
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	IRELE(ip);
	*res = BULKSTAT_RV_DIDONE;
	return 0;

error0:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	IRELE(ip);
	*res = BULKSTAT_RV_GIVEUP;
	return error;
}

STATIC int
xfs_qm_flush_one(
	struct xfs_dquot	*dqp,
	void			*data)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct list_head	*buffer_list = data;
	struct xfs_buf		*bp = NULL;
	int			error = 0;

	xfs_dqlock(dqp);
	if (dqp->dq_flags & XFS_DQ_FREEING)
		goto out_unlock;
	if (!XFS_DQ_IS_DIRTY(dqp))
		goto out_unlock;

	/*
	 * The only way the dquot is already flush locked by the time quotacheck
	 * gets here is if reclaim flushed it before the dqadjust walk dirtied
	 * it for the final time. Quotacheck collects all dquot bufs in the
	 * local delwri queue before dquots are dirtied, so reclaim can't have
	 * possibly queued it for I/O. The only way out is to push the buffer to
	 * cycle the flush lock.
	 */
	if (!xfs_dqflock_nowait(dqp)) {
		/* buf is pinned in-core by delwri list */
		DEFINE_SINGLE_BUF_MAP(map, dqp->q_blkno,
				      mp->m_quotainfo->qi_dqchunklen);
		bp = _xfs_buf_find(mp->m_ddev_targp, &map, 1, 0, NULL);
		if (!bp) {
			error = -EINVAL;
			goto out_unlock;
		}
		xfs_buf_unlock(bp);

		xfs_buf_delwri_pushbuf(bp, buffer_list);
		xfs_buf_rele(bp);

		error = -EAGAIN;
		goto out_unlock;
	}

	error = xfs_qm_dqflush(dqp, &bp);
	if (error)
		goto out_unlock;

	xfs_buf_delwri_queue(bp, buffer_list);
	xfs_buf_relse(bp);
out_unlock:
	xfs_dqunlock(dqp);
	return error;
}

/*
 * Walk thru all the filesystem inodes and construct a consistent view
 * of the disk quota world. If the quotacheck fails, disable quotas.
 */
STATIC int
xfs_qm_quotacheck(
	xfs_mount_t	*mp)
{
	int			done, count, error, error2;
	xfs_ino_t		lastino;
	size_t			structsz;
	uint			flags;
	LIST_HEAD		(buffer_list);
	struct xfs_inode	*uip = mp->m_quotainfo->qi_uquotaip;
	struct xfs_inode	*gip = mp->m_quotainfo->qi_gquotaip;
	struct xfs_inode	*pip = mp->m_quotainfo->qi_pquotaip;

	count = INT_MAX;
	structsz = 1;
	lastino = 0;
	flags = 0;

	ASSERT(uip || gip || pip);
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	xfs_notice(mp, "Quotacheck needed: Please wait.");

	/*
	 * First we go thru all the dquots on disk, USR and GRP/PRJ, and reset
	 * their counters to zero. We need a clean slate.
	 * We don't log our changes till later.
	 */
	if (uip) {
		error = xfs_qm_dqiterate(mp, uip, XFS_QMOPT_UQUOTA,
					 &buffer_list);
		if (error)
			goto error_return;
		flags |= XFS_UQUOTA_CHKD;
	}

	if (gip) {
		error = xfs_qm_dqiterate(mp, gip, XFS_QMOPT_GQUOTA,
					 &buffer_list);
		if (error)
			goto error_return;
		flags |= XFS_GQUOTA_CHKD;
	}

	if (pip) {
		error = xfs_qm_dqiterate(mp, pip, XFS_QMOPT_PQUOTA,
					 &buffer_list);
		if (error)
			goto error_return;
		flags |= XFS_PQUOTA_CHKD;
	}

	do {
		/*
		 * Iterate thru all the inodes in the file system,
		 * adjusting the corresponding dquot counters in core.
		 */
		error = xfs_bulkstat(mp, &lastino, &count,
				     xfs_qm_dqusage_adjust,
				     structsz, NULL, &done);
		if (error)
			break;

	} while (!done);

	/*
	 * We've made all the changes that we need to make incore.  Flush them
	 * down to disk buffers if everything was updated successfully.
	 */
	if (XFS_IS_UQUOTA_ON(mp)) {
		error = xfs_qm_dquot_walk(mp, XFS_DQ_USER, xfs_qm_flush_one,
					  &buffer_list);
	}
	if (XFS_IS_GQUOTA_ON(mp)) {
		error2 = xfs_qm_dquot_walk(mp, XFS_DQ_GROUP, xfs_qm_flush_one,
					   &buffer_list);
		if (!error)
			error = error2;
	}
	if (XFS_IS_PQUOTA_ON(mp)) {
		error2 = xfs_qm_dquot_walk(mp, XFS_DQ_PROJ, xfs_qm_flush_one,
					   &buffer_list);
		if (!error)
			error = error2;
	}

	error2 = xfs_buf_delwri_submit(&buffer_list);
	if (!error)
		error = error2;

	/*
	 * We can get this error if we couldn't do a dquot allocation inside
	 * xfs_qm_dqusage_adjust (via bulkstat). We don't care about the
	 * dirty dquots that might be cached, we just want to get rid of them
	 * and turn quotaoff. The dquots won't be attached to any of the inodes
	 * at this point (because we intentionally didn't in dqget_noattach).
	 */
	if (error) {
		xfs_qm_dqpurge_all(mp, XFS_QMOPT_QUOTALL);
		goto error_return;
	}

	/*
	 * If one type of quotas is off, then it will lose its
	 * quotachecked status, since we won't be doing accounting for
	 * that type anymore.
	 */
	mp->m_qflags &= ~XFS_ALL_QUOTA_CHKD;
	mp->m_qflags |= flags;

 error_return:
	xfs_buf_delwri_cancel(&buffer_list);

	if (error) {
		xfs_warn(mp,
	"Quotacheck: Unsuccessful (Error %d): Disabling quotas.",
			error);
		/*
		 * We must turn off quotas.
		 */
		ASSERT(mp->m_quotainfo != NULL);
		xfs_qm_destroy_quotainfo(mp);
		if (xfs_mount_reset_sbqflags(mp)) {
			xfs_warn(mp,
				"Quotacheck: Failed to reset quota flags.");
		}
	} else
		xfs_notice(mp, "Quotacheck: Done.");
	return error;
}

/*
 * This is called from xfs_mountfs to start quotas and initialize all
 * necessary data structures like quotainfo.  This is also responsible for
 * running a quotacheck as necessary.  We are guaranteed that the superblock
 * is consistently read in at this point.
 *
 * If we fail here, the mount will continue with quota turned off. We don't
 * need to inidicate success or failure at all.
 */
void
xfs_qm_mount_quotas(
	struct xfs_mount	*mp)
{
	int			error = 0;
	uint			sbf;

	/*
	 * If quotas on realtime volumes is not supported, we disable
	 * quotas immediately.
	 */
	if (mp->m_sb.sb_rextents) {
		xfs_notice(mp, "Cannot turn on quotas for realtime filesystem");
		mp->m_qflags = 0;
		goto write_changes;
	}

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	/*
	 * Allocate the quotainfo structure inside the mount struct, and
	 * create quotainode(s), and change/rev superblock if necessary.
	 */
	error = xfs_qm_init_quotainfo(mp);
	if (error) {
		/*
		 * We must turn off quotas.
		 */
		ASSERT(mp->m_quotainfo == NULL);
		mp->m_qflags = 0;
		goto write_changes;
	}
	/*
	 * If any of the quotas are not consistent, do a quotacheck.
	 */
	if (XFS_QM_NEED_QUOTACHECK(mp)) {
		error = xfs_qm_quotacheck(mp);
		if (error) {
			/* Quotacheck failed and disabled quotas. */
			return;
		}
	}
	/*
	 * If one type of quotas is off, then it will lose its
	 * quotachecked status, since we won't be doing accounting for
	 * that type anymore.
	 */
	if (!XFS_IS_UQUOTA_ON(mp))
		mp->m_qflags &= ~XFS_UQUOTA_CHKD;
	if (!XFS_IS_GQUOTA_ON(mp))
		mp->m_qflags &= ~XFS_GQUOTA_CHKD;
	if (!XFS_IS_PQUOTA_ON(mp))
		mp->m_qflags &= ~XFS_PQUOTA_CHKD;

 write_changes:
	/*
	 * We actually don't have to acquire the m_sb_lock at all.
	 * This can only be called from mount, and that's single threaded. XXX
	 */
	spin_lock(&mp->m_sb_lock);
	sbf = mp->m_sb.sb_qflags;
	mp->m_sb.sb_qflags = mp->m_qflags & XFS_MOUNT_QUOTA_ALL;
	spin_unlock(&mp->m_sb_lock);

	if (sbf != (mp->m_qflags & XFS_MOUNT_QUOTA_ALL)) {
		if (xfs_sync_sb(mp, false)) {
			/*
			 * We could only have been turning quotas off.
			 * We aren't in very good shape actually because
			 * the incore structures are convinced that quotas are
			 * off, but the on disk superblock doesn't know that !
			 */
			ASSERT(!(XFS_IS_QUOTA_RUNNING(mp)));
			xfs_alert(mp, "%s: Superblock update failed!",
				__func__);
		}
	}

	if (error) {
		xfs_warn(mp, "Failed to initialize disk quotas.");
		return;
	}
}

/*
 * This is called after the superblock has been read in and we're ready to
 * iget the quota inodes.
 */
STATIC int
xfs_qm_init_quotainos(
	xfs_mount_t	*mp)
{
	struct xfs_inode	*uip = NULL;
	struct xfs_inode	*gip = NULL;
	struct xfs_inode	*pip = NULL;
	int			error;
	uint			flags = 0;

	ASSERT(mp->m_quotainfo);

	/*
	 * Get the uquota and gquota inodes
	 */
	if (xfs_sb_version_hasquota(&mp->m_sb)) {
		if (XFS_IS_UQUOTA_ON(mp) &&
		    mp->m_sb.sb_uquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_uquotino > 0);
			error = xfs_iget(mp, NULL, mp->m_sb.sb_uquotino,
					     0, 0, &uip);
			if (error)
				return error;
		}
		if (XFS_IS_GQUOTA_ON(mp) &&
		    mp->m_sb.sb_gquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_gquotino > 0);
			error = xfs_iget(mp, NULL, mp->m_sb.sb_gquotino,
					     0, 0, &gip);
			if (error)
				goto error_rele;
		}
		if (XFS_IS_PQUOTA_ON(mp) &&
		    mp->m_sb.sb_pquotino != NULLFSINO) {
			ASSERT(mp->m_sb.sb_pquotino > 0);
			error = xfs_iget(mp, NULL, mp->m_sb.sb_pquotino,
					     0, 0, &pip);
			if (error)
				goto error_rele;
		}
	} else {
		flags |= XFS_QMOPT_SBVERSION;
	}

	/*
	 * Create the three inodes, if they don't exist already. The changes
	 * made above will get added to a transaction and logged in one of
	 * the qino_alloc calls below.  If the device is readonly,
	 * temporarily switch to read-write to do this.
	 */
	if (XFS_IS_UQUOTA_ON(mp) && uip == NULL) {
		error = xfs_qm_qino_alloc(mp, &uip,
					      flags | XFS_QMOPT_UQUOTA);
		if (error)
			goto error_rele;

		flags &= ~XFS_QMOPT_SBVERSION;
	}
	if (XFS_IS_GQUOTA_ON(mp) && gip == NULL) {
		error = xfs_qm_qino_alloc(mp, &gip,
					  flags | XFS_QMOPT_GQUOTA);
		if (error)
			goto error_rele;

		flags &= ~XFS_QMOPT_SBVERSION;
	}
	if (XFS_IS_PQUOTA_ON(mp) && pip == NULL) {
		error = xfs_qm_qino_alloc(mp, &pip,
					  flags | XFS_QMOPT_PQUOTA);
		if (error)
			goto error_rele;
	}

	mp->m_quotainfo->qi_uquotaip = uip;
	mp->m_quotainfo->qi_gquotaip = gip;
	mp->m_quotainfo->qi_pquotaip = pip;

	return 0;

error_rele:
	if (uip)
		IRELE(uip);
	if (gip)
		IRELE(gip);
	if (pip)
		IRELE(pip);
	return error;
}

STATIC void
xfs_qm_destroy_quotainos(
	xfs_quotainfo_t	*qi)
{
	if (qi->qi_uquotaip) {
		IRELE(qi->qi_uquotaip);
		qi->qi_uquotaip = NULL; /* paranoia */
	}
	if (qi->qi_gquotaip) {
		IRELE(qi->qi_gquotaip);
		qi->qi_gquotaip = NULL;
	}
	if (qi->qi_pquotaip) {
		IRELE(qi->qi_pquotaip);
		qi->qi_pquotaip = NULL;
	}
}

STATIC void
xfs_qm_dqfree_one(
	struct xfs_dquot	*dqp)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_quotainfo	*qi = mp->m_quotainfo;

	mutex_lock(&qi->qi_tree_lock);
	radix_tree_delete(xfs_dquot_tree(qi, dqp->q_core.d_flags),
			  be32_to_cpu(dqp->q_core.d_id));

	qi->qi_dquots--;
	mutex_unlock(&qi->qi_tree_lock);

	xfs_qm_dqdestroy(dqp);
}

/* --------------- utility functions for vnodeops ---------------- */


/*
 * Given an inode, a uid, gid and prid make sure that we have
 * allocated relevant dquot(s) on disk, and that we won't exceed inode
 * quotas by creating this file.
 * This also attaches dquot(s) to the given inode after locking it,
 * and returns the dquots corresponding to the uid and/or gid.
 *
 * in	: inode (unlocked)
 * out	: udquot, gdquot with references taken and unlocked
 */
int
xfs_qm_vop_dqalloc(
	struct xfs_inode	*ip,
	xfs_dqid_t		uid,
	xfs_dqid_t		gid,
	prid_t			prid,
	uint			flags,
	struct xfs_dquot	**O_udqpp,
	struct xfs_dquot	**O_gdqpp,
	struct xfs_dquot	**O_pdqpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_dquot	*uq = NULL;
	struct xfs_dquot	*gq = NULL;
	struct xfs_dquot	*pq = NULL;
	int			error;
	uint			lockflags;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;

	lockflags = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lockflags);

	if ((flags & XFS_QMOPT_INHERIT) && XFS_INHERIT_GID(ip))
		gid = ip->i_d.di_gid;

	/*
	 * Attach the dquot(s) to this inode, doing a dquot allocation
	 * if necessary. The dquot(s) will not be locked.
	 */
	if (XFS_NOT_DQATTACHED(mp, ip)) {
		error = xfs_qm_dqattach_locked(ip, XFS_QMOPT_DQALLOC);
		if (error) {
			xfs_iunlock(ip, lockflags);
			return error;
		}
	}

	if ((flags & XFS_QMOPT_UQUOTA) && XFS_IS_UQUOTA_ON(mp)) {
		if (ip->i_d.di_uid != uid) {
			/*
			 * What we need is the dquot that has this uid, and
			 * if we send the inode to dqget, the uid of the inode
			 * takes priority over what's sent in the uid argument.
			 * We must unlock inode here before calling dqget if
			 * we're not sending the inode, because otherwise
			 * we'll deadlock by doing trans_reserve while
			 * holding ilock.
			 */
			xfs_iunlock(ip, lockflags);
			error = xfs_qm_dqget(mp, NULL, uid,
						 XFS_DQ_USER,
						 XFS_QMOPT_DQALLOC |
						 XFS_QMOPT_DOWARN,
						 &uq);
			if (error) {
				ASSERT(error != -ENOENT);
				return error;
			}
			/*
			 * Get the ilock in the right order.
			 */
			xfs_dqunlock(uq);
			lockflags = XFS_ILOCK_SHARED;
			xfs_ilock(ip, lockflags);
		} else {
			/*
			 * Take an extra reference, because we'll return
			 * this to caller
			 */
			ASSERT(ip->i_udquot);
			uq = xfs_qm_dqhold(ip->i_udquot);
		}
	}
	if ((flags & XFS_QMOPT_GQUOTA) && XFS_IS_GQUOTA_ON(mp)) {
		if (ip->i_d.di_gid != gid) {
			xfs_iunlock(ip, lockflags);
			error = xfs_qm_dqget(mp, NULL, gid,
						 XFS_DQ_GROUP,
						 XFS_QMOPT_DQALLOC |
						 XFS_QMOPT_DOWARN,
						 &gq);
			if (error) {
				ASSERT(error != -ENOENT);
				goto error_rele;
			}
			xfs_dqunlock(gq);
			lockflags = XFS_ILOCK_SHARED;
			xfs_ilock(ip, lockflags);
		} else {
			ASSERT(ip->i_gdquot);
			gq = xfs_qm_dqhold(ip->i_gdquot);
		}
	}
	if ((flags & XFS_QMOPT_PQUOTA) && XFS_IS_PQUOTA_ON(mp)) {
		if (xfs_get_projid(ip) != prid) {
			xfs_iunlock(ip, lockflags);
			error = xfs_qm_dqget(mp, NULL, (xfs_dqid_t)prid,
						 XFS_DQ_PROJ,
						 XFS_QMOPT_DQALLOC |
						 XFS_QMOPT_DOWARN,
						 &pq);
			if (error) {
				ASSERT(error != -ENOENT);
				goto error_rele;
			}
			xfs_dqunlock(pq);
			lockflags = XFS_ILOCK_SHARED;
			xfs_ilock(ip, lockflags);
		} else {
			ASSERT(ip->i_pdquot);
			pq = xfs_qm_dqhold(ip->i_pdquot);
		}
	}
	if (uq)
		trace_xfs_dquot_dqalloc(ip);

	xfs_iunlock(ip, lockflags);
	if (O_udqpp)
		*O_udqpp = uq;
	else
		xfs_qm_dqrele(uq);
	if (O_gdqpp)
		*O_gdqpp = gq;
	else
		xfs_qm_dqrele(gq);
	if (O_pdqpp)
		*O_pdqpp = pq;
	else
		xfs_qm_dqrele(pq);
	return 0;

error_rele:
	xfs_qm_dqrele(gq);
	xfs_qm_dqrele(uq);
	return error;
}

/*
 * Actually transfer ownership, and do dquot modifications.
 * These were already reserved.
 */
xfs_dquot_t *
xfs_qm_vop_chown(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_dquot_t	**IO_olddq,
	xfs_dquot_t	*newdq)
{
	xfs_dquot_t	*prevdq;
	uint		bfield = XFS_IS_REALTIME_INODE(ip) ?
				 XFS_TRANS_DQ_RTBCOUNT : XFS_TRANS_DQ_BCOUNT;


	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(XFS_IS_QUOTA_RUNNING(ip->i_mount));

	/* old dquot */
	prevdq = *IO_olddq;
	ASSERT(prevdq);
	ASSERT(prevdq != newdq);

	xfs_trans_mod_dquot(tp, prevdq, bfield, -(ip->i_d.di_nblocks));
	xfs_trans_mod_dquot(tp, prevdq, XFS_TRANS_DQ_ICOUNT, -1);

	/* the sparkling new dquot */
	xfs_trans_mod_dquot(tp, newdq, bfield, ip->i_d.di_nblocks);
	xfs_trans_mod_dquot(tp, newdq, XFS_TRANS_DQ_ICOUNT, 1);

	/*
	 * Take an extra reference, because the inode is going to keep
	 * this dquot pointer even after the trans_commit.
	 */
	*IO_olddq = xfs_qm_dqhold(newdq);

	return prevdq;
}

/*
 * Quota reservations for setattr(AT_UID|AT_GID|AT_PROJID).
 */
int
xfs_qm_vop_chown_reserve(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_dquot	*udqp,
	struct xfs_dquot	*gdqp,
	struct xfs_dquot	*pdqp,
	uint			flags)
{
	struct xfs_mount	*mp = ip->i_mount;
	uint			delblks, blkflags, prjflags = 0;
	struct xfs_dquot	*udq_unres = NULL;
	struct xfs_dquot	*gdq_unres = NULL;
	struct xfs_dquot	*pdq_unres = NULL;
	struct xfs_dquot	*udq_delblks = NULL;
	struct xfs_dquot	*gdq_delblks = NULL;
	struct xfs_dquot	*pdq_delblks = NULL;
	int			error;


	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	delblks = ip->i_delayed_blks;
	blkflags = XFS_IS_REALTIME_INODE(ip) ?
			XFS_QMOPT_RES_RTBLKS : XFS_QMOPT_RES_REGBLKS;

	if (XFS_IS_UQUOTA_ON(mp) && udqp &&
	    ip->i_d.di_uid != be32_to_cpu(udqp->q_core.d_id)) {
		udq_delblks = udqp;
		/*
		 * If there are delayed allocation blocks, then we have to
		 * unreserve those from the old dquot, and add them to the
		 * new dquot.
		 */
		if (delblks) {
			ASSERT(ip->i_udquot);
			udq_unres = ip->i_udquot;
		}
	}
	if (XFS_IS_GQUOTA_ON(ip->i_mount) && gdqp &&
	    ip->i_d.di_gid != be32_to_cpu(gdqp->q_core.d_id)) {
		gdq_delblks = gdqp;
		if (delblks) {
			ASSERT(ip->i_gdquot);
			gdq_unres = ip->i_gdquot;
		}
	}

	if (XFS_IS_PQUOTA_ON(ip->i_mount) && pdqp &&
	    xfs_get_projid(ip) != be32_to_cpu(pdqp->q_core.d_id)) {
		prjflags = XFS_QMOPT_ENOSPC;
		pdq_delblks = pdqp;
		if (delblks) {
			ASSERT(ip->i_pdquot);
			pdq_unres = ip->i_pdquot;
		}
	}

	error = xfs_trans_reserve_quota_bydquots(tp, ip->i_mount,
				udq_delblks, gdq_delblks, pdq_delblks,
				ip->i_d.di_nblocks, 1,
				flags | blkflags | prjflags);
	if (error)
		return error;

	/*
	 * Do the delayed blks reservations/unreservations now. Since, these
	 * are done without the help of a transaction, if a reservation fails
	 * its previous reservations won't be automatically undone by trans
	 * code. So, we have to do it manually here.
	 */
	if (delblks) {
		/*
		 * Do the reservations first. Unreservation can't fail.
		 */
		ASSERT(udq_delblks || gdq_delblks || pdq_delblks);
		ASSERT(udq_unres || gdq_unres || pdq_unres);
		error = xfs_trans_reserve_quota_bydquots(NULL, ip->i_mount,
			    udq_delblks, gdq_delblks, pdq_delblks,
			    (xfs_qcnt_t)delblks, 0,
			    flags | blkflags | prjflags);
		if (error)
			return error;
		xfs_trans_reserve_quota_bydquots(NULL, ip->i_mount,
				udq_unres, gdq_unres, pdq_unres,
				-((xfs_qcnt_t)delblks), 0, blkflags);
	}

	return 0;
}

int
xfs_qm_vop_rename_dqattach(
	struct xfs_inode	**i_tab)
{
	struct xfs_mount	*mp = i_tab[0]->i_mount;
	int			i;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return 0;

	for (i = 0; (i < 4 && i_tab[i]); i++) {
		struct xfs_inode	*ip = i_tab[i];
		int			error;

		/*
		 * Watch out for duplicate entries in the table.
		 */
		if (i == 0 || ip != i_tab[i-1]) {
			if (XFS_NOT_DQATTACHED(mp, ip)) {
				error = xfs_qm_dqattach(ip, 0);
				if (error)
					return error;
			}
		}
	}
	return 0;
}

void
xfs_qm_vop_create_dqattach(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct xfs_dquot	*udqp,
	struct xfs_dquot	*gdqp,
	struct xfs_dquot	*pdqp)
{
	struct xfs_mount	*mp = tp->t_mountp;

	if (!XFS_IS_QUOTA_RUNNING(mp) || !XFS_IS_QUOTA_ON(mp))
		return;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(XFS_IS_QUOTA_RUNNING(mp));

	if (udqp && XFS_IS_UQUOTA_ON(mp)) {
		ASSERT(ip->i_udquot == NULL);
		ASSERT(ip->i_d.di_uid == be32_to_cpu(udqp->q_core.d_id));

		ip->i_udquot = xfs_qm_dqhold(udqp);
		xfs_trans_mod_dquot(tp, udqp, XFS_TRANS_DQ_ICOUNT, 1);
	}
	if (gdqp && XFS_IS_GQUOTA_ON(mp)) {
		ASSERT(ip->i_gdquot == NULL);
		ASSERT(ip->i_d.di_gid == be32_to_cpu(gdqp->q_core.d_id));
		ip->i_gdquot = xfs_qm_dqhold(gdqp);
		xfs_trans_mod_dquot(tp, gdqp, XFS_TRANS_DQ_ICOUNT, 1);
	}
	if (pdqp && XFS_IS_PQUOTA_ON(mp)) {
		ASSERT(ip->i_pdquot == NULL);
		ASSERT(xfs_get_projid(ip) == be32_to_cpu(pdqp->q_core.d_id));

		ip->i_pdquot = xfs_qm_dqhold(pdqp);
		xfs_trans_mod_dquot(tp, pdqp, XFS_TRANS_DQ_ICOUNT, 1);
	}
}

