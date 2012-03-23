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
 *   qh->qh_lock
 *     qi->qi_dqlist_lock
 *       dquot->q_qlock (xfs_dqlock() and friends)
 *         dquot->q_flush (xfs_dqflock() and friends)
 *         xfs_Gqm->qm_dqfrlist_lock
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

static struct lock_class_key xfs_dquot_other_class;

/*
 * This is called to free all the memory associated with a dquot
 */
void
xfs_qm_dqdestroy(
	xfs_dquot_t	*dqp)
{
	ASSERT(list_empty(&dqp->q_freelist));

	mutex_destroy(&dqp->q_qlock);
	kmem_zone_free(xfs_Gqm->qm_dqzone, dqp);

	atomic_dec(&xfs_Gqm->qm_totaldquots);
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
	if (XFS_IS_THIS_QUOTA_OFF(dqp)) {
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
	if (XFS_IS_THIS_QUOTA_OFF(dqp)) {
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


	dqp = kmem_zone_zalloc(xfs_Gqm->qm_dqzone, KM_SLEEP);

	dqp->dq_flags = type;
	dqp->q_core.d_id = cpu_to_be32(id);
	dqp->q_mount = mp;
	INIT_LIST_HEAD(&dqp->q_freelist);
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

	atomic_inc(&xfs_Gqm->qm_totaldquots);

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
 * Lookup a dquot in the incore dquot hashtable. We keep two separate
 * hashtables for user and group dquots; and, these are global tables
 * inside the XQM, not per-filesystem tables.
 * The hash chain must be locked by caller, and it is left locked
 * on return. Returning dquot is locked.
 */
STATIC int
xfs_qm_dqlookup(
	xfs_mount_t		*mp,
	xfs_dqid_t		id,
	xfs_dqhash_t		*qh,
	xfs_dquot_t		**O_dqpp)
{
	xfs_dquot_t		*dqp;

	ASSERT(mutex_is_locked(&qh->qh_lock));

	/*
	 * Traverse the hashchain looking for a match
	 */
	list_for_each_entry(dqp, &qh->qh_list, q_hashlist) {
		/*
		 * We already have the hashlock. We don't need the
		 * dqlock to look at the id field of the dquot, since the
		 * id can't be modified without the hashlock anyway.
		 */
		if (be32_to_cpu(dqp->q_core.d_id) != id || dqp->q_mount != mp)
			continue;

		trace_xfs_dqlookup_found(dqp);

		xfs_dqlock(dqp);
		if (dqp->dq_flags & XFS_DQ_FREEING) {
			*O_dqpp = NULL;
			xfs_dqunlock(dqp);
			return -1;
		}

		dqp->q_nrefs++;

		/*
		 * move the dquot to the front of the hashchain
		 */
		list_move(&dqp->q_hashlist, &qh->qh_list);
		trace_xfs_dqlookup_done(dqp);
		*O_dqpp = dqp;
		return 0;
	}

	*O_dqpp = NULL;
	return 1;
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
	xfs_dquot_t	*dqp;
	xfs_dqhash_t	*h;
	uint		version;
	int		error;

	ASSERT(XFS_IS_QUOTA_RUNNING(mp));
	if ((! XFS_IS_UQUOTA_ON(mp) && type == XFS_DQ_USER) ||
	    (! XFS_IS_PQUOTA_ON(mp) && type == XFS_DQ_PROJ) ||
	    (! XFS_IS_GQUOTA_ON(mp) && type == XFS_DQ_GROUP)) {
		return (ESRCH);
	}
	h = XFS_DQ_HASH(mp, id, type);

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
		if (type == XFS_DQ_USER)
			ASSERT(ip->i_udquot == NULL);
		else
			ASSERT(ip->i_gdquot == NULL);
	}
#endif

restart:
	mutex_lock(&h->qh_lock);

	/*
	 * Look in the cache (hashtable).
	 * The chain is kept locked during lookup.
	 */
	switch (xfs_qm_dqlookup(mp, id, h, O_dqpp)) {
	case -1:
		XQM_STATS_INC(xqmstats.xs_qm_dquot_dups);
		mutex_unlock(&h->qh_lock);
		delay(1);
		goto restart;
	case 0:
		XQM_STATS_INC(xqmstats.xs_qm_dqcachehits);
		/*
		 * The dquot was found, moved to the front of the chain,
		 * taken off the freelist if it was on it, and locked
		 * at this point. Just unlock the hashchain and return.
		 */
		ASSERT(*O_dqpp);
		ASSERT(XFS_DQ_IS_LOCKED(*O_dqpp));
		mutex_unlock(&h->qh_lock);
		trace_xfs_dqget_hit(*O_dqpp);
		return 0;	/* success */
	default:
		XQM_STATS_INC(xqmstats.xs_qm_dqcachemisses);
		break;
	}

	/*
	 * Dquot cache miss. We don't want to keep the inode lock across
	 * a (potential) disk read. Also we don't want to deal with the lock
	 * ordering between quotainode and this inode. OTOH, dropping the inode
	 * lock here means dealing with a chown that can happen before
	 * we re-acquire the lock.
	 */
	if (ip)
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	/*
	 * Save the hashchain version stamp, and unlock the chain, so that
	 * we don't keep the lock across a disk read
	 */
	version = h->qh_version;
	mutex_unlock(&h->qh_lock);

	error = xfs_qm_dqread(mp, id, type, flags, &dqp);

	if (ip)
		xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (error)
		return error;

	/*
	 * Dquot lock comes after hashlock in the lock ordering
	 */
	if (ip) {
		/*
		 * A dquot could be attached to this inode by now, since
		 * we had dropped the ilock.
		 */
		if (type == XFS_DQ_USER) {
			if (!XFS_IS_UQUOTA_ON(mp)) {
				/* inode stays locked on return */
				xfs_qm_dqdestroy(dqp);
				return XFS_ERROR(ESRCH);
			}
			if (ip->i_udquot) {
				xfs_qm_dqdestroy(dqp);
				dqp = ip->i_udquot;
				xfs_dqlock(dqp);
				goto dqret;
			}
		} else {
			if (!XFS_IS_OQUOTA_ON(mp)) {
				/* inode stays locked on return */
				xfs_qm_dqdestroy(dqp);
				return XFS_ERROR(ESRCH);
			}
			if (ip->i_gdquot) {
				xfs_qm_dqdestroy(dqp);
				dqp = ip->i_gdquot;
				xfs_dqlock(dqp);
				goto dqret;
			}
		}
	}

	/*
	 * Hashlock comes after ilock in lock order
	 */
	mutex_lock(&h->qh_lock);
	if (version != h->qh_version) {
		xfs_dquot_t *tmpdqp;
		/*
		 * Now, see if somebody else put the dquot in the
		 * hashtable before us. This can happen because we didn't
		 * keep the hashchain lock. We don't have to worry about
		 * lock order between the two dquots here since dqp isn't
		 * on any findable lists yet.
		 */
		switch (xfs_qm_dqlookup(mp, id, h, &tmpdqp)) {
		case 0:
		case -1:
			/*
			 * Duplicate found, either in cache or on its way out.
			 * Just throw away the new dquot and start over.
			 */
			if (tmpdqp)
				xfs_qm_dqput(tmpdqp);
			mutex_unlock(&h->qh_lock);
			xfs_qm_dqdestroy(dqp);
			XQM_STATS_INC(xqmstats.xs_qm_dquot_dups);
			goto restart;
		default:
			break;
		}
	}

	/*
	 * Put the dquot at the beginning of the hash-chain and mp's list
	 * LOCK ORDER: hashlock, freelistlock, mplistlock, udqlock, gdqlock ..
	 */
	ASSERT(mutex_is_locked(&h->qh_lock));
	dqp->q_hash = h;
	list_add(&dqp->q_hashlist, &h->qh_list);
	h->qh_version++;

	/*
	 * Attach this dquot to this filesystem's list of all dquots,
	 * kept inside the mount structure in m_quotainfo field
	 */
	mutex_lock(&mp->m_quotainfo->qi_dqlist_lock);

	/*
	 * We return a locked dquot to the caller, with a reference taken
	 */
	xfs_dqlock(dqp);
	dqp->q_nrefs = 1;

	list_add(&dqp->q_mplist, &mp->m_quotainfo->qi_dqlist);
	mp->m_quotainfo->qi_dquots++;
	mutex_unlock(&mp->m_quotainfo->qi_dqlist_lock);
	mutex_unlock(&h->qh_lock);
 dqret:
	ASSERT((ip == NULL) || xfs_isilocked(ip, XFS_ILOCK_EXCL));
	trace_xfs_dqget_miss(dqp);
	*O_dqpp = dqp;
	return (0);
}


/*
 * Release a reference to the dquot (decrement ref-count)
 * and unlock it. If there is a group quota attached to this
 * dquot, carefully release that too without tripping over
 * deadlocks'n'stuff.
 */
void
xfs_qm_dqput(
	struct xfs_dquot	*dqp)
{
	struct xfs_dquot	*gdqp;

	ASSERT(dqp->q_nrefs > 0);
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	trace_xfs_dqput(dqp);

recurse:
	if (--dqp->q_nrefs > 0) {
		xfs_dqunlock(dqp);
		return;
	}

	trace_xfs_dqput_free(dqp);

	mutex_lock(&xfs_Gqm->qm_dqfrlist_lock);
	if (list_empty(&dqp->q_freelist)) {
		list_add_tail(&dqp->q_freelist, &xfs_Gqm->qm_dqfrlist);
		xfs_Gqm->qm_dqfrlist_cnt++;
	}
	mutex_unlock(&xfs_Gqm->qm_dqfrlist_lock);

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
	if (gdqp) {
		dqp = gdqp;
		goto recurse;
	}
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

void
xfs_dqunlock(
	xfs_dquot_t *dqp)
{
	xfs_dqunlock_nonotify(dqp);
	if (dqp->q_logitem.qli_dquot == dqp) {
		xfs_trans_unlocked_item(dqp->q_logitem.qli_item.li_ailp,
					&dqp->q_logitem.qli_item);
	}
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
 * Take a dquot out of the mount's dqlist as well as the hashlist.  This is
 * called via unmount as well as quotaoff, and the purge will always succeed.
 */
void
xfs_qm_dqpurge(
	struct xfs_dquot	*dqp)
{
	struct xfs_mount	*mp = dqp->q_mount;
	struct xfs_dqhash	*qh = dqp->q_hash;

	xfs_dqlock(dqp);

	/*
	 * If we're turning off quotas, we have to make sure that, for
	 * example, we don't delete quota disk blocks while dquots are
	 * in the process of getting written to those disk blocks.
	 * This dquot might well be on AIL, and we can't leave it there
	 * if we're turning off quotas. Basically, we need this flush
	 * lock, and are willing to block on it.
	 */
	if (!xfs_dqflock_nowait(dqp)) {
		/*
		 * Block on the flush lock after nudging dquot buffer,
		 * if it is incore.
		 */
		xfs_dqflock_pushbuf_wait(dqp);
	}

	/*
	 * If we are turning this type of quotas off, we don't care
	 * about the dirty metadata sitting in this dquot. OTOH, if
	 * we're unmounting, we do care, so we flush it and wait.
	 */
	if (XFS_DQ_IS_DIRTY(dqp)) {
		int	error;

		/*
		 * We don't care about getting disk errors here. We need
		 * to purge this dquot anyway, so we go ahead regardless.
		 */
		error = xfs_qm_dqflush(dqp, SYNC_WAIT);
		if (error)
			xfs_warn(mp, "%s: dquot %p flush failed",
				__func__, dqp);
		xfs_dqflock(dqp);
	}

	ASSERT(atomic_read(&dqp->q_pincount) == 0);
	ASSERT(XFS_FORCED_SHUTDOWN(mp) ||
	       !(dqp->q_logitem.qli_item.li_flags & XFS_LI_IN_AIL));

	xfs_dqfunlock(dqp);
	xfs_dqunlock(dqp);

	mutex_lock(&qh->qh_lock);
	list_del_init(&dqp->q_hashlist);
	qh->qh_version++;
	mutex_unlock(&qh->qh_lock);

	mutex_lock(&mp->m_quotainfo->qi_dqlist_lock);
	list_del_init(&dqp->q_mplist);
	mp->m_quotainfo->qi_dqreclaims++;
	mp->m_quotainfo->qi_dquots--;
	mutex_unlock(&mp->m_quotainfo->qi_dqlist_lock);

	/*
	 * We move dquots to the freelist as soon as their reference count
	 * hits zero, so it really should be on the freelist here.
	 */
	mutex_lock(&xfs_Gqm->qm_dqfrlist_lock);
	ASSERT(!list_empty(&dqp->q_freelist));
	list_del_init(&dqp->q_freelist);
	xfs_Gqm->qm_dqfrlist_cnt--;
	mutex_unlock(&xfs_Gqm->qm_dqfrlist_lock);

	xfs_qm_dqdestroy(dqp);
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
