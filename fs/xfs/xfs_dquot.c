/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
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
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_space.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"
#include "xfs_trace.h"

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

#ifdef DEBUG
xfs_buftarg_t *xfs_dqerror_target;
int xfs_do_dqerror;
int xfs_dqreq_num;
int xfs_dqerror_mod = 33;
#endif

struct kmem_zone		*xfs_qm_dqtrxzone;
static struct kmem_zone		*xfs_qm_dqzone;

static struct lock_class_key xfs_dquot_other_class;

/*
 * This is called to free all the memory associated with a dquot
 */
void
xfs_qm_dqdestroy(
	xfs_dquot_t	*dqp)
{
	ASSERT(list_empty(&dqp->q_lru));

	mutex_destroy(&dqp->q_qlock);
	kmem_zone_free(xfs_qm_dqzone, dqp);

	XFS_STATS_DEC(xs_qm_dquot);
}

/*
 * If default limits are in force, push them into the dquot now.
 * We overwrite the dquot limits only if they are zero and this
 * is not the root dquot.
 */
void
xfs_qm_adjust_dqlimits(
	xfs_mount_t		*mp,
	xfs_disk_dquot_t	*d)
{
	xfs_quotainfo_t		*q = mp->m_quotainfo;

	ASSERT(d->d_id);

	if (q->qi_bsoftlimit && !d->d_blk_softlimit)
		d->d_blk_softlimit = cpu_to_be64(q->qi_bsoftlimit);
	if (q->qi_bhardlimit && !d->d_blk_hardlimit)
		d->d_blk_hardlimit = cpu_to_be64(q->qi_bhardlimit);
	if (q->qi_isoftlimit && !d->d_ino_softlimit)
		d->d_ino_softlimit = cpu_to_be64(q->qi_isoftlimit);
	if (q->qi_ihardlimit && !d->d_ino_hardlimit)
		d->d_ino_hardlimit = cpu_to_be64(q->qi_ihardlimit);
	if (q->qi_rtbsoftlimit && !d->d_rtb_softlimit)
		d->d_rtb_softlimit = cpu_to_be64(q->qi_rtbsoftlimit);
	if (q->qi_rtbhardlimit && !d->d_rtb_hardlimit)
		d->d_rtb_hardlimit = cpu_to_be64(q->qi_rtbhardlimit);
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
	xfs_mount_t		*mp,
	xfs_disk_dquot_t	*d)
{
	ASSERT(d->d_id);

#ifdef DEBUG
	if (d->d_blk_hardlimit)
		ASSERT(be64_to_cpu(d->d_blk_softlimit) <=
		       be64_to_cpu(d->d_blk_hardlimit));
	if (d->d_ino_hardlimit)
		ASSERT(be64_to_cpu(d->d_ino_softlimit) <=
		       be64_to_cpu(d->d_ino_hardlimit));
	if (d->d_rtb_hardlimit)
		ASSERT(be64_to_cpu(d->d_rtb_softlimit) <=
		       be64_to_cpu(d->d_rtb_hardlimit));
#endif

	if (!d->d_btimer) {
		if ((d->d_blk_softlimit &&
		     (be64_to_cpu(d->d_bcount) >
		      be64_to_cpu(d->d_blk_softlimit))) ||
		    (d->d_blk_hardlimit &&
		     (be64_to_cpu(d->d_bcount) >
		      be64_to_cpu(d->d_blk_hardlimit)))) {
			d->d_btimer = cpu_to_be32(get_seconds() +
					mp->m_quotainfo->qi_btimelimit);
		} else {
			d->d_bwarns = 0;
		}
	} else {
		if ((!d->d_blk_softlimit ||
		     (be64_to_cpu(d->d_bcount) <=
		      be64_to_cpu(d->d_blk_softlimit))) &&
		    (!d->d_blk_hardlimit ||
		    (be64_to_cpu(d->d_bcount) <=
		     be64_to_cpu(d->d_blk_hardlimit)))) {
			d->d_btimer = 0;
		}
	}

	if (!d->d_itimer) {
		if ((d->d_ino_softlimit &&
		     (be64_to_cpu(d->d_icount) >
		      be64_to_cpu(d->d_ino_softlimit))) ||
		    (d->d_ino_hardlimit &&
		     (be64_to_cpu(d->d_icount) >
		      be64_to_cpu(d->d_ino_hardlimit)))) {
			d->d_itimer = cpu_to_be32(get_seconds() +
					mp->m_quotainfo->qi_itimelimit);
		} else {
			d->d_iwarns = 0;
		}
	} else {
		if ((!d->d_ino_softlimit ||
		     (be64_to_cpu(d->d_icount) <=
		      be64_to_cpu(d->d_ino_softlimit)))  &&
		    (!d->d_ino_hardlimit ||
		     (be64_to_cpu(d->d_icount) <=
		      be64_to_cpu(d->d_ino_hardlimit)))) {
			d->d_itimer = 0;
		}
	}

	if (!d->d_rtbtimer) {
		if ((d->d_rtb_softlimit &&
		     (be64_to_cpu(d->d_rtbcount) >
		      be64_to_cpu(d->d_rtb_softlimit))) ||
		    (d->d_rtb_hardlimit &&
		     (be64_to_cpu(d->d_rtbcount) >
		      be64_to_cpu(d->d_rtb_hardlimit)))) {
			d->d_rtbtimer = cpu_to_be32(get_seconds() +
					mp->m_quotainfo->qi_rtbtimelimit);
		} else {
			d->d_rtbwarns = 0;
		}
	} else {
		if ((!d->d_rtb_softlimit ||
		     (be64_to_cpu(d->d_rtbcount) <=
		      be64_to_cpu(d->d_rtb_softlimit))) &&
		    (!d->d_rtb_hardlimit ||
		     (be64_to_cpu(d->d_rtbcount) <=
		      be64_to_cpu(d->d_rtb_hardlimit)))) {
			d->d_rtbtimer = 0;
		}
	}
}

/*
 * initialize a buffer full of dquots and log the whole thing
 */
STATIC void
xfs_qm_init_dquot_blk(
	xfs_trans_t	*tp,
	xfs_mount_t	*mp,
	xfs_dqid_t	id,
	uint		type,
	xfs_buf_t	*bp)
{
	struct xfs_quotainfo	*q = mp->m_quotainfo;
	xfs_dqblk_t	*d;
	int		curid, i;

	ASSERT(tp);
	ASSERT(xfs_buf_islocked(bp));

	d = bp->b_addr;

	/*
	 * ID of the first dquot in the block - id's are zero based.
	 */
	curid = id - (id % q->qi_dqperchunk);
	ASSERT(curid >= 0);
	memset(d, 0, BBTOB(q->qi_dqchunklen));
	for (i = 0; i < q->qi_dqperchunk; i++, d++, curid++) {
		d->dd_diskdq.d_magic = cpu_to_be16(XFS_DQUOT_MAGIC);
		d->dd_diskdq.d_version = XFS_DQUOT_VERSION;
		d->dd_diskdq.d_id = cpu_to_be32(curid);
		d->dd_diskdq.d_flags = type;
	}

	xfs_trans_dquot_buf(tp, bp,
			    (type & XFS_DQ_USER ? XFS_BLF_UDQUOT_BUF :
			    ((type & XFS_DQ_PROJ) ? XFS_BLF_PDQUOT_BUF :
			     XFS_BLF_GDQUOT_BUF)));
	xfs_trans_log_buf(tp, bp, 0, BBTOB(q->qi_dqchunklen) - 1);
}



/*
 * Allocate a block and fill it with dquots.
 * This is called when the bmapi finds a hole.
 */
STATIC int
xfs_qm_dqalloc(
	xfs_trans_t	**tpp,
	xfs_mount_t	*mp,
	xfs_dquot_t	*dqp,
	xfs_inode_t	*quotip,
	xfs_fileoff_t	offset_fsb,
	xfs_buf_t	**O_bpp)
{
	xfs_fsblock_t	firstblock;
	xfs_bmap_free_t flist;
	xfs_bmbt_irec_t map;
	int		nmaps, error, committed;
	xfs_buf_t	*bp;
	xfs_trans_t	*tp = *tpp;

	ASSERT(tp != NULL);

	trace_xfs_dqalloc(dqp);

	/*
	 * Initialize the bmap freelist prior to calling bmapi code.
	 */
	xfs_bmap_init(&flist, &firstblock);
	xfs_ilock(quotip, XFS_ILOCK_EXCL);
	/*
	 * Return if this type of quotas is turned off while we didn't
	 * have an inode lock
	 */
	if (!xfs_this_quota_on(dqp->q_mount, dqp->dq_flags)) {
		xfs_iunlock(quotip, XFS_ILOCK_EXCL);
		return (ESRCH);
	}

	xfs_trans_ijoin(tp, quotip, XFS_ILOCK_EXCL);
	nmaps = 1;
	error = xfs_bmapi_write(tp, quotip, offset_fsb,
				XFS_DQUOT_CLUSTER_SIZE_FSB, XFS_BMAPI_METADATA,
				&firstblock, XFS_QM_DQALLOC_SPACE_RES(mp),
				&map, &nmaps, &flist);
	if (error)
		goto error0;
	ASSERT(map.br_blockcount == XFS_DQUOT_CLUSTER_SIZE_FSB);
	ASSERT(nmaps == 1);
	ASSERT((map.br_startblock != DELAYSTARTBLOCK) &&
	       (map.br_startblock != HOLESTARTBLOCK));

	/*
	 * Keep track of the blkno to save a lookup later
	 */
	dqp->q_blkno = XFS_FSB_TO_DADDR(mp, map.br_startblock);

	/* now we can just get the buffer (there's nothing to read yet) */
	bp = xfs_trans_get_buf(tp, mp->m_ddev_targp,
			       dqp->q_blkno,
			       mp->m_quotainfo->qi_dqchunklen,
			       0);

	error = xfs_buf_geterror(bp);
	if (error)
		goto error1;

	/*
	 * Make a chunk of dquots out of this buffer and log
	 * the entire thing.
	 */
	xfs_qm_init_dquot_blk(tp, mp, be32_to_cpu(dqp->q_core.d_id),
			      dqp->dq_flags & XFS_DQ_ALLTYPES, bp);

	/*
	 * xfs_bmap_finish() may commit the current transaction and
	 * start a second transaction if the freelist is not empty.
	 *
	 * Since we still want to modify this buffer, we need to
	 * ensure that the buffer is not released on commit of
	 * the first transaction and ensure the buffer is added to the
	 * second transaction.
	 *
	 * If there is only one transaction then don't stop the buffer
	 * from being released when it commits later on.
	 */

	xfs_trans_bhold(tp, bp);

	if ((error = xfs_bmap_finish(tpp, &flist, &committed))) {
		goto error1;
	}

	if (committed) {
		tp = *tpp;
		xfs_trans_bjoin(tp, bp);
	} else {
		xfs_trans_bhold_release(tp, bp);
	}

	*O_bpp = bp;
	return 0;

      error1:
	xfs_bmap_cancel(&flist);
      error0:
	xfs_iunlock(quotip, XFS_ILOCK_EXCL);

	return (error);
}

/*
 * Maps a dquot to the buffer containing its on-disk version.
 * This returns a ptr to the buffer containing the on-disk dquot
 * in the bpp param, and a ptr to the on-disk dquot within that buffer
 */
STATIC int
xfs_qm_dqtobp(
	xfs_trans_t		**tpp,
	xfs_dquot_t		*dqp,
	xfs_disk_dquot_t	**O_ddpp,
	xfs_buf_t		**O_bpp,
	uint			flags)
{
	xfs_bmbt_irec_t map;
	int		nmaps = 1, error;
	xfs_buf_t	*bp;
	xfs_inode_t	*quotip = XFS_DQ_TO_QIP(dqp);
	xfs_mount_t	*mp = dqp->q_mount;
	xfs_disk_dquot_t *ddq;
	xfs_dqid_t	id = be32_to_cpu(dqp->q_core.d_id);
	xfs_trans_t	*tp = (tpp ? *tpp : NULL);

	dqp->q_fileoffset = (xfs_fileoff_t)id / mp->m_quotainfo->qi_dqperchunk;

	xfs_ilock(quotip, XFS_ILOCK_SHARED);
	if (!xfs_this_quota_on(dqp->q_mount, dqp->dq_flags)) {
		/*
		 * Return if this type of quotas is turned off while we
		 * didn't have the quota inode lock.
		 */
		xfs_iunlock(quotip, XFS_ILOCK_SHARED);
		return ESRCH;
	}

	/*
	 * Find the block map; no allocations yet
	 */
	error = xfs_bmapi_read(quotip, dqp->q_fileoffset,
			       XFS_DQUOT_CLUSTER_SIZE_FSB, &map, &nmaps, 0);

	xfs_iunlock(quotip, XFS_ILOCK_SHARED);
	if (error)
		return error;

	ASSERT(nmaps == 1);
	ASSERT(map.br_blockcount == 1);

	/*
	 * Offset of dquot in the (fixed sized) dquot chunk.
	 */
	dqp->q_bufoffset = (id % mp->m_quotainfo->qi_dqperchunk) *
		sizeof(xfs_dqblk_t);

	ASSERT(map.br_startblock != DELAYSTARTBLOCK);
	if (map.br_startblock == HOLESTARTBLOCK) {
		/*
		 * We don't allocate unless we're asked to
		 */
		if (!(flags & XFS_QMOPT_DQALLOC))
			return ENOENT;

		ASSERT(tp);
		error = xfs_qm_dqalloc(tpp, mp, dqp, quotip,
					dqp->q_fileoffset, &bp);
		if (error)
			return error;
		tp = *tpp;
	} else {
		trace_xfs_dqtobp_read(dqp);

		/*
		 * store the blkno etc so that we don't have to do the
		 * mapping all the time
		 */
		dqp->q_blkno = XFS_FSB_TO_DADDR(mp, map.br_startblock);

		error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
					   dqp->q_blkno,
					   mp->m_quotainfo->qi_dqchunklen,
					   0, &bp);
		if (error || !bp)
			return XFS_ERROR(error);
	}

	ASSERT(xfs_buf_islocked(bp));

	/*
	 * calculate the location of the dquot inside the buffer.
	 */
	ddq = bp->b_addr + dqp->q_bufoffset;

	/*
	 * A simple sanity check in case we got a corrupted dquot...
	 */
	error = xfs_qm_dqcheck(mp, ddq, id, dqp->dq_flags & XFS_DQ_ALLTYPES,
			   flags & (XFS_QMOPT_DQREPAIR|XFS_QMOPT_DOWARN),
			   "dqtobp");
	if (error) {
		if (!(flags & XFS_QMOPT_DQREPAIR)) {
			xfs_trans_brelse(tp, bp);
			return XFS_ERROR(EIO);
		}
	}

	*O_bpp = bp;
	*O_ddpp = ddq;

	return (0);
}


/*
 * Read in the ondisk dquot using dqtobp() then copy it to an incore version,
 * and release the buffer immediately.
 *
 * If XFS_QMOPT_DQALLOC is set, allocate a dquot on disk if it needed.
 */
int
xfs_qm_dqread(
	struct xfs_mount	*mp,
	xfs_dqid_t		id,
	uint			type,
	uint			flags,
	struct xfs_dquot	**O_dqpp)
{
	struct xfs_dquot	*dqp;
	struct xfs_disk_dquot	*ddqp;
	struct xfs_buf		*bp;
	struct xfs_trans	*tp = NULL;
	int			error;
	int			cancelflags = 0;


	dqp = kmem_zone_zalloc(xfs_qm_dqzone, KM_SLEEP);

	dqp->dq_flags = type;
	dqp->q_core.d_id = cpu_to_be32(id);
	dqp->q_mount = mp;
	INIT_LIST_HEAD(&dqp->q_lru);
	mutex_init(&dqp->q_qlock);
	init_waitqueue_head(&dqp->q_pinwait);

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
	if (!(type & XFS_DQ_USER))
		lockdep_set_class(&dqp->q_qlock, &xfs_dquot_other_class);

	XFS_STATS_INC(xs_qm_dquot);

	trace_xfs_dqread(dqp);

	if (flags & XFS_QMOPT_DQALLOC) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_QM_DQALLOC);
		error = xfs_trans_reserve(tp, XFS_QM_DQALLOC_SPACE_RES(mp),
				XFS_WRITE_LOG_RES(mp) +
				/*
				 * Round the chunklen up to the next multiple
				 * of 128 (buf log item chunk size)).
				 */
				BBTOB(mp->m_quotainfo->qi_dqchunklen) - 1 + 128,
				0,
				XFS_TRANS_PERM_LOG_RES,
				XFS_WRITE_LOG_COUNT);
		if (error)
			goto error1;
		cancelflags = XFS_TRANS_RELEASE_LOG_RES;
	}

	/*
	 * get a pointer to the on-disk dquot and the buffer containing it
	 * dqp already knows its own type (GROUP/USER).
	 */
	error = xfs_qm_dqtobp(&tp, dqp, &ddqp, &bp, flags);
	if (error) {
		/*
		 * This can happen if quotas got turned off (ESRCH),
		 * or if the dquot didn't exist on disk and we ask to
		 * allocate (ENOENT).
		 */
		trace_xfs_dqread_fail(dqp);
		cancelflags |= XFS_TRANS_ABORT;
		goto error1;
	}

	/* copy everything from disk dquot to the incore dquot */
	memcpy(&dqp->q_core, ddqp, sizeof(xfs_disk_dquot_t));
	xfs_qm_dquot_logitem_init(dqp);

	/*
	 * Reservation counters are defined as reservation plus current usage
	 * to avoid having to add every time.
	 */
	dqp->q_res_bcount = be64_to_cpu(ddqp->d_bcount);
	dqp->q_res_icount = be64_to_cpu(ddqp->d_icount);
	dqp->q_res_rtbcount = be64_to_cpu(ddqp->d_rtbcount);

	/* Mark the buf so that this will stay incore a little longer */
	xfs_buf_set_ref(bp, XFS_DQUOT_REF);

	/*
	 * We got the buffer with a xfs_trans_read_buf() (in dqtobp())
	 * So we need to release with xfs_trans_brelse().
	 * The strategy here is identical to that of inodes; we lock
	 * the dquot in xfs_qm_dqget() before making it accessible to
	 * others. This is because dquots, like inodes, need a good level of
	 * concurrency, and we don't want to take locks on the entire buffers
	 * for dquot accesses.
	 * Note also that the dquot buffer may even be dirty at this point, if
	 * this particular dquot was repaired. We still aren't afraid to
	 * brelse it because we have the changes incore.
	 */
	ASSERT(xfs_buf_islocked(bp));
	xfs_trans_brelse(tp, bp);

	if (tp) {
		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		if (error)
			goto error0;
	}

	*O_dqpp = dqp;
	return error;

error1:
	if (tp)
		xfs_trans_cancel(tp, cancelflags);
error0:
	xfs_qm_dqdestroy(dqp);
	*O_dqpp = NULL;
	return error;
}

/*
 * Given the file system, inode OR id, and type (UDQUOT/GDQUOT), return a
 * a locked dquot, doing an allocation (if requested) as needed.
 * When both an inode and an id are given, the inode's id takes precedence.
 * That is, if the id changes while we don't hold the ilock inside this
 * function, the new dquot is returned, not necessarily the one requested
 * in the id argument.
 */
int
xfs_qm_dqget(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,	  /* locked inode (optional) */
	xfs_dqid_t	id,	  /* uid/projid/gid depending on type */
	uint		type,	  /* XFS_DQ_USER/XFS_DQ_PROJ/XFS_DQ_GROUP */
	uint		flags,	  /* DQALLOC, DQSUSER, DQREPAIR, DOWARN */
	xfs_dquot_t	**O_dqpp) /* OUT : locked incore dquot */
{
	struct xfs_quotainfo	*qi = mp->m_quotainfo;
	struct radix_tree_root *tree = XFS_DQUOT_TREE(qi, type);
	struct xfs_dquot	*dqp;
	int			error;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));
	if ((! XFS_IS_UQUOTA_ON(mp) && type == XFS_DQ_USER) ||
	    (! XFS_IS_PQUOTA_ON(mp) && type == XFS_DQ_PROJ) ||
	    (! XFS_IS_GQUOTA_ON(mp) && type == XFS_DQ_GROUP)) {
		return (ESRCH);
	}

#ifdef DEBUG
	if (xfs_do_dqerror) {
		if ((xfs_dqerror_target == mp->m_ddev_targp) &&
		    (xfs_dqreq_num++ % xfs_dqerror_mod) == 0) {
			xfs_debug(mp, "Returning error in dqget");
			return (EIO);
		}
	}

	ASSERT(type == XFS_DQ_USER ||
	       type == XFS_DQ_PROJ ||
	       type == XFS_DQ_GROUP);
	if (ip) {
		ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
		ASSERT(xfs_inode_dquot(ip, type) == NULL);
	}
#endif

restart:
	mutex_lock(&qi->qi_tree_lock);
	dqp = radix_tree_lookup(tree, id);
	if (dqp) {
		xfs_dqlock(dqp);
		if (dqp->dq_flags & XFS_DQ_FREEING) {
			xfs_dqunlock(dqp);
			mutex_unlock(&qi->qi_tree_lock);
			trace_xfs_dqget_freeing(dqp);
			delay(1);
			goto restart;
		}

		dqp->q_nrefs++;
		mutex_unlock(&qi->qi_tree_lock);

		trace_xfs_dqget_hit(dqp);
		XFS_STATS_INC(xs_qm_dqcachehits);
		*O_dqpp = dqp;
		return 0;
	}
	mutex_unlock(&qi->qi_tree_lock);
	XFS_STATS_INC(xs_qm_dqcachemisses);

	/*
	 * Dquot cache miss. We don't want to keep the inode lock across
	 * a (potential) disk read. Also we don't want to deal with the lock
	 * ordering between quotainode and this inode. OTOH, dropping the inode
	 * lock here means dealing with a chown that can happen before
	 * we re-acquire the lock.
	 */
	if (ip)
		xfs_iunlock(ip, XFS_ILOCK_EXCL);

	error = xfs_qm_dqread(mp, id, type, flags, &dqp);

	if (ip)
		xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (error)
		return error;

	if (ip) {
		/*
		 * A dquot could be attached to this inode by now, since
		 * we had dropped the ilock.
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
			return XFS_ERROR(ESRCH);
		}
	}

	mutex_lock(&qi->qi_tree_lock);
	error = -radix_tree_insert(tree, id, dqp);
	if (unlikely(error)) {
		WARN_ON(error != EEXIST);

		/*
		 * Duplicate found. Just throw away the new dquot and start
		 * over.
		 */
		mutex_unlock(&qi->qi_tree_lock);
		trace_xfs_dqget_dup(dqp);
		xfs_qm_dqdestroy(dqp);
		XFS_STATS_INC(xs_qm_dquot_dups);
		goto restart;
	}

	/*
	 * We return a locked dquot to the caller, with a reference taken
	 */
	xfs_dqlock(dqp);
	dqp->q_nrefs = 1;

	qi->qi_dquots++;
	mutex_unlock(&qi->qi_tree_lock);

 dqret:
	ASSERT((ip == NULL) || xfs_isilocked(ip, XFS_ILOCK_EXCL));
	trace_xfs_dqget_miss(dqp);
	*O_dqpp = dqp;
	return (0);
}


STATIC void
xfs_qm_dqput_final(
	struct xfs_dquot	*dqp)
{
	struct xfs_quotainfo	*qi = dqp->q_mount->m_quotainfo;
	struct xfs_dquot	*gdqp;

	trace_xfs_dqput_free(dqp);

	mutex_lock(&qi->qi_lru_lock);
	if (list_empty(&dqp->q_lru)) {
		list_add_tail(&dqp->q_lru, &qi->qi_lru_list);
		qi->qi_lru_count++;
		XFS_STATS_INC(xs_qm_dquot_unused);
	}
	mutex_unlock(&qi->qi_lru_lock);

	/*
	 * If we just added a udquot to the freelist, then we want to release
	 * the gdquot reference that it (probably) has. Otherwise it'll keep
	 * the gdquot from getting reclaimed.
	 */
	gdqp = dqp->q_gdquot;
	if (gdqp) {
		xfs_dqlock(gdqp);
		dqp->q_gdquot = NULL;
	}
	xfs_dqunlock(dqp);

	/*
	 * If we had a group quota hint, release it now.
	 */
	if (gdqp)
		xfs_qm_dqput(gdqp);
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

	if (--dqp->q_nrefs > 0)
		xfs_dqunlock(dqp);
	else
		xfs_qm_dqput_final(dqp);
}

/*
 * Release a dquot. Flush it if dirty, then dqput() it.
 * dquot must not be locked.
 */
void
xfs_qm_dqrele(
	xfs_dquot_t	*dqp)
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
STATIC void
xfs_qm_dqflush_done(
	struct xfs_buf		*bp,
	struct xfs_log_item	*lip)
{
	xfs_dq_logitem_t	*qip = (struct xfs_dq_logitem *)lip;
	xfs_dquot_t		*dqp = qip->qli_dquot;
	struct xfs_ail		*ailp = lip->li_ailp;

	/*
	 * We only want to pull the item from the AIL if its
	 * location in the log has not changed since we started the flush.
	 * Thus, we only bother if the dquot's lsn has
	 * not changed. First we check the lsn outside the lock
	 * since it's cheaper, and then we recheck while
	 * holding the lock before removing the dquot from the AIL.
	 */
	if ((lip->li_flags & XFS_LI_IN_AIL) &&
	    lip->li_lsn == qip->qli_flush_lsn) {

		/* xfs_trans_ail_delete() drops the AIL lock. */
		spin_lock(&ailp->xa_lock);
		if (lip->li_lsn == qip->qli_flush_lsn)
			xfs_trans_ail_delete(ailp, lip);
		else
			spin_unlock(&ailp->xa_lock);
	}

	/*
	 * Release the dq's flush lock since we're done with it.
	 */
	xfs_dqfunlock(dqp);
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
	xfs_dquot_t		*dqp,
	uint			flags)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_buf		*bp;
	struct xfs_disk_dquot	*ddqp;
	int			error;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(!completion_done(&dqp->q_flush));

	trace_xfs_dqflush(dqp);

	/*
	 * If not dirty, or it's pinned and we are not supposed to block, nada.
	 */
	if (!XFS_DQ_IS_DIRTY(dqp) ||
	    ((flags & SYNC_TRYLOCK) && atomic_read(&dqp->q_pincount) > 0)) {
		xfs_dqfunlock(dqp);
		return 0;
	}
	xfs_qm_dqunpin_wait(dqp);

	/*
	 * This may have been unpinned because the filesystem is shutting
	 * down forcibly. If that's the case we must not write this dquot
	 * to disk, because the log record didn't make it to disk!
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		dqp->dq_flags &= ~XFS_DQ_DIRTY;
		xfs_dqfunlock(dqp);
		return XFS_ERROR(EIO);
	}

	/*
	 * Get the buffer containing the on-disk dquot
	 */
	error = xfs_trans_read_buf(mp, NULL, mp->m_ddev_targp, dqp->q_blkno,
				   mp->m_quotainfo->qi_dqchunklen, 0, &bp);
	if (error) {
		ASSERT(error != ENOENT);
		xfs_dqfunlock(dqp);
		return error;
	}

	/*
	 * Calculate the location of the dquot inside the buffer.
	 */
	ddqp = bp->b_addr + dqp->q_bufoffset;

	/*
	 * A simple sanity check in case we got a corrupted dquot..
	 */
	error = xfs_qm_dqcheck(mp, &dqp->q_core, be32_to_cpu(ddqp->d_id), 0,
			   XFS_QMOPT_DOWARN, "dqflush (incore copy)");
	if (error) {
		xfs_buf_relse(bp);
		xfs_dqfunlock(dqp);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return XFS_ERROR(EIO);
	}

	/* This is the only portion of data that needs to persist */
	memcpy(ddqp, &dqp->q_core, sizeof(xfs_disk_dquot_t));

	/*
	 * Clear the dirty field and remember the flush lsn for later use.
	 */
	dqp->dq_flags &= ~XFS_DQ_DIRTY;

	xfs_trans_ail_copy_lsn(mp->m_ail, &dqp->q_logitem.qli_flush_lsn,
					&dqp->q_logitem.qli_item.li_lsn);

	/*
	 * Attach an iodone routine so that we can remove this dquot from the
	 * AIL and release the flush lock once the dquot is synced to disk.
	 */
	xfs_buf_attach_iodone(bp, xfs_qm_dqflush_done,
				  &dqp->q_logitem.qli_item);

	/*
	 * If the buffer is pinned then push on the log so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (xfs_buf_ispinned(bp)) {
		trace_xfs_dqflush_force(dqp);
		xfs_log_force(mp, 0);
	}

	if (flags & SYNC_WAIT)
		error = xfs_bwrite(bp);
	else
		xfs_buf_delwri_queue(bp);

	xfs_buf_relse(bp);

	trace_xfs_dqflush_done(dqp);

	/*
	 * dqp is still locked, but caller is free to unlock it now.
	 */
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
	xfs_dquot_t	*d1,
	xfs_dquot_t	*d2)
{
	if (d1 && d2) {
		ASSERT(d1 != d2);
		if (be32_to_cpu(d1->q_core.d_id) >
		    be32_to_cpu(d2->q_core.d_id)) {
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

/*
 * Give the buffer a little push if it is incore and
 * wait on the flush lock.
 */
void
xfs_dqflock_pushbuf_wait(
	xfs_dquot_t	*dqp)
{
	xfs_mount_t	*mp = dqp->q_mount;
	xfs_buf_t	*bp;

	/*
	 * Check to see if the dquot has been flushed delayed
	 * write.  If so, grab its buffer and send it
	 * out immediately.  We'll be able to acquire
	 * the flush lock when the I/O completes.
	 */
	bp = xfs_incore(mp->m_ddev_targp, dqp->q_blkno,
			mp->m_quotainfo->qi_dqchunklen, XBF_TRYLOCK);
	if (!bp)
		goto out_lock;

	if (XFS_BUF_ISDELAYWRITE(bp)) {
		if (xfs_buf_ispinned(bp))
			xfs_log_force(mp, 0);
		xfs_buf_delwri_promote(bp);
		wake_up_process(bp->b_target->bt_task);
	}
	xfs_buf_relse(bp);
out_lock:
	xfs_dqflock(dqp);
}

int __init
xfs_qm_init(void)
{
	xfs_qm_dqzone =
		kmem_zone_init(sizeof(struct xfs_dquot), "xfs_dquot");
	if (!xfs_qm_dqzone)
		goto out;

	xfs_qm_dqtrxzone =
		kmem_zone_init(sizeof(struct xfs_dquot_acct), "xfs_dqtrx");
	if (!xfs_qm_dqtrxzone)
		goto out_free_dqzone;

	return 0;

out_free_dqzone:
	kmem_zone_destroy(xfs_qm_dqzone);
out:
	return -ENOMEM;
}

void __exit
xfs_qm_exit(void)
{
	kmem_zone_destroy(xfs_qm_dqtrxzone);
	kmem_zone_destroy(xfs_qm_dqzone);
}
