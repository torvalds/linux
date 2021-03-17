// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_error.h"
#include "xfs_alloc.h"
#include "xfs_fsops.h"
#include "xfs_trans_space.h"
#include "xfs_log.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"

/*
 * growfs operations
 */
static int
xfs_growfs_data_private(
	xfs_mount_t		*mp,		/* mount point for filesystem */
	xfs_growfs_data_t	*in)		/* growfs data input struct */
{
	xfs_buf_t		*bp;
	int			error;
	xfs_agnumber_t		nagcount;
	xfs_agnumber_t		nagimax = 0;
	xfs_rfsblock_t		nb, nb_mod;
	xfs_rfsblock_t		new;
	xfs_agnumber_t		oagcount;
	xfs_trans_t		*tp;
	struct aghdr_init_data	id = {};

	nb = in->newblocks;
	if (nb < mp->m_sb.sb_dblocks)
		return -EINVAL;
	if ((error = xfs_sb_validate_fsb_count(&mp->m_sb, nb)))
		return error;
	error = xfs_buf_read_uncached(mp->m_ddev_targp,
				XFS_FSB_TO_BB(mp, nb) - XFS_FSS_TO_BB(mp, 1),
				XFS_FSS_TO_BB(mp, 1), 0, &bp, NULL);
	if (error)
		return error;
	xfs_buf_relse(bp);

	new = nb;	/* use new as a temporary here */
	nb_mod = do_div(new, mp->m_sb.sb_agblocks);
	nagcount = new + (nb_mod != 0);
	if (nb_mod && nb_mod < XFS_MIN_AG_BLOCKS) {
		nagcount--;
		nb = (xfs_rfsblock_t)nagcount * mp->m_sb.sb_agblocks;
		if (nb < mp->m_sb.sb_dblocks)
			return -EINVAL;
	}
	new = nb - mp->m_sb.sb_dblocks;
	oagcount = mp->m_sb.sb_agcount;

	/* allocate the new per-ag structures */
	if (nagcount > oagcount) {
		error = xfs_initialize_perag(mp, nagcount, &nagimax);
		if (error)
			return error;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growdata,
			XFS_GROWFS_SPACE_RES(mp), 0, XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	/*
	 * Write new AG headers to disk. Non-transactional, but need to be
	 * written and completed prior to the growfs transaction being logged.
	 * To do this, we use a delayed write buffer list and wait for
	 * submission and IO completion of the list as a whole. This allows the
	 * IO subsystem to merge all the AG headers in a single AG into a single
	 * IO and hide most of the latency of the IO from us.
	 *
	 * This also means that if we get an error whilst building the buffer
	 * list to write, we can cancel the entire list without having written
	 * anything.
	 */
	INIT_LIST_HEAD(&id.buffer_list);
	for (id.agno = nagcount - 1;
	     id.agno >= oagcount;
	     id.agno--, new -= id.agsize) {

		if (id.agno == nagcount - 1)
			id.agsize = nb -
				(id.agno * (xfs_rfsblock_t)mp->m_sb.sb_agblocks);
		else
			id.agsize = mp->m_sb.sb_agblocks;

		error = xfs_ag_init_headers(mp, &id);
		if (error) {
			xfs_buf_delwri_cancel(&id.buffer_list);
			goto out_trans_cancel;
		}
	}
	error = xfs_buf_delwri_submit(&id.buffer_list);
	if (error)
		goto out_trans_cancel;

	xfs_trans_agblocks_delta(tp, id.nfree);

	/* If there are new blocks in the old last AG, extend it. */
	if (new) {
		error = xfs_ag_extend_space(mp, tp, &id, new);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * Update changed superblock fields transactionally. These are not
	 * seen by the rest of the world until the transaction commit applies
	 * them atomically to the superblock.
	 */
	if (nagcount > oagcount)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_AGCOUNT, nagcount - oagcount);
	if (nb > mp->m_sb.sb_dblocks)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_DBLOCKS,
				 nb - mp->m_sb.sb_dblocks);
	if (id.nfree)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, id.nfree);
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp);
	if (error)
		return error;

	/* New allocation groups fully initialized, so update mount struct */
	if (nagimax)
		mp->m_maxagi = nagimax;
	xfs_set_low_space_thresholds(mp);
	mp->m_alloc_set_aside = xfs_alloc_set_aside(mp);

	/*
	 * If we expanded the last AG, free the per-AG reservation
	 * so we can reinitialize it with the new size.
	 */
	if (new) {
		struct xfs_perag	*pag;

		pag = xfs_perag_get(mp, id.agno);
		error = xfs_ag_resv_free(pag);
		xfs_perag_put(pag);
		if (error)
			return error;
	}

	/*
	 * Reserve AG metadata blocks. ENOSPC here does not mean there was a
	 * growfs failure, just that there still isn't space for new user data
	 * after the grow has been run.
	 */
	error = xfs_fs_reserve_ag_blocks(mp);
	if (error == -ENOSPC)
		error = 0;
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
}

static int
xfs_growfs_log_private(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_growfs_log_t	*in)	/* growfs log input struct */
{
	xfs_extlen_t		nb;

	nb = in->newblocks;
	if (nb < XFS_MIN_LOG_BLOCKS || nb < XFS_B_TO_FSB(mp, XFS_MIN_LOG_BYTES))
		return -EINVAL;
	if (nb == mp->m_sb.sb_logblocks &&
	    in->isint == (mp->m_sb.sb_logstart != 0))
		return -EINVAL;
	/*
	 * Moving the log is hard, need new interfaces to sync
	 * the log first, hold off all activity while moving it.
	 * Can have shorter or longer log in the same space,
	 * or transform internal to external log or vice versa.
	 */
	return -ENOSYS;
}

static int
xfs_growfs_imaxpct(
	struct xfs_mount	*mp,
	__u32			imaxpct)
{
	struct xfs_trans	*tp;
	int			dpct;
	int			error;

	if (imaxpct > 100)
		return -EINVAL;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growdata,
			XFS_GROWFS_SPACE_RES(mp), 0, XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	dpct = imaxpct - mp->m_sb.sb_imax_pct;
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IMAXPCT, dpct);
	xfs_trans_set_sync(tp);
	return xfs_trans_commit(tp);
}

/*
 * protected versions of growfs function acquire and release locks on the mount
 * point - exported through ioctls: XFS_IOC_FSGROWFSDATA, XFS_IOC_FSGROWFSLOG,
 * XFS_IOC_FSGROWFSRT
 */
int
xfs_growfs_data(
	struct xfs_mount	*mp,
	struct xfs_growfs_data	*in)
{
	int			error = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!mutex_trylock(&mp->m_growlock))
		return -EWOULDBLOCK;

	/* update imaxpct separately to the physical grow of the filesystem */
	if (in->imaxpct != mp->m_sb.sb_imax_pct) {
		error = xfs_growfs_imaxpct(mp, in->imaxpct);
		if (error)
			goto out_error;
	}

	if (in->newblocks != mp->m_sb.sb_dblocks) {
		error = xfs_growfs_data_private(mp, in);
		if (error)
			goto out_error;
	}

	/* Post growfs calculations needed to reflect new state in operations */
	if (mp->m_sb.sb_imax_pct) {
		uint64_t icount = mp->m_sb.sb_dblocks * mp->m_sb.sb_imax_pct;
		do_div(icount, 100);
		M_IGEO(mp)->maxicount = XFS_FSB_TO_INO(mp, icount);
	} else
		M_IGEO(mp)->maxicount = 0;

	/* Update secondary superblocks now the physical grow has completed */
	error = xfs_update_secondary_sbs(mp);

out_error:
	/*
	 * Increment the generation unconditionally, the error could be from
	 * updating the secondary superblocks, in which case the new size
	 * is live already.
	 */
	mp->m_generation++;
	mutex_unlock(&mp->m_growlock);
	return error;
}

int
xfs_growfs_log(
	xfs_mount_t		*mp,
	xfs_growfs_log_t	*in)
{
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!mutex_trylock(&mp->m_growlock))
		return -EWOULDBLOCK;
	error = xfs_growfs_log_private(mp, in);
	mutex_unlock(&mp->m_growlock);
	return error;
}

/*
 * exported through ioctl XFS_IOC_FSCOUNTS
 */

void
xfs_fs_counts(
	xfs_mount_t		*mp,
	xfs_fsop_counts_t	*cnt)
{
	cnt->allocino = percpu_counter_read_positive(&mp->m_icount);
	cnt->freeino = percpu_counter_read_positive(&mp->m_ifree);
	cnt->freedata = percpu_counter_read_positive(&mp->m_fdblocks) -
						mp->m_alloc_set_aside;

	spin_lock(&mp->m_sb_lock);
	cnt->freertx = mp->m_sb.sb_frextents;
	spin_unlock(&mp->m_sb_lock);
}

/*
 * exported through ioctl XFS_IOC_SET_RESBLKS & XFS_IOC_GET_RESBLKS
 *
 * xfs_reserve_blocks is called to set m_resblks
 * in the in-core mount table. The number of unused reserved blocks
 * is kept in m_resblks_avail.
 *
 * Reserve the requested number of blocks if available. Otherwise return
 * as many as possible to satisfy the request. The actual number
 * reserved are returned in outval
 *
 * A null inval pointer indicates that only the current reserved blocks
 * available  should  be returned no settings are changed.
 */

int
xfs_reserve_blocks(
	xfs_mount_t             *mp,
	uint64_t              *inval,
	xfs_fsop_resblks_t      *outval)
{
	int64_t			lcounter, delta;
	int64_t			fdblks_delta = 0;
	uint64_t		request;
	int64_t			free;
	int			error = 0;

	/* If inval is null, report current values and return */
	if (inval == (uint64_t *)NULL) {
		if (!outval)
			return -EINVAL;
		outval->resblks = mp->m_resblks;
		outval->resblks_avail = mp->m_resblks_avail;
		return 0;
	}

	request = *inval;

	/*
	 * With per-cpu counters, this becomes an interesting problem. we need
	 * to work out if we are freeing or allocation blocks first, then we can
	 * do the modification as necessary.
	 *
	 * We do this under the m_sb_lock so that if we are near ENOSPC, we will
	 * hold out any changes while we work out what to do. This means that
	 * the amount of free space can change while we do this, so we need to
	 * retry if we end up trying to reserve more space than is available.
	 */
	spin_lock(&mp->m_sb_lock);

	/*
	 * If our previous reservation was larger than the current value,
	 * then move any unused blocks back to the free pool. Modify the resblks
	 * counters directly since we shouldn't have any problems unreserving
	 * space.
	 */
	if (mp->m_resblks > request) {
		lcounter = mp->m_resblks_avail - request;
		if (lcounter  > 0) {		/* release unused blocks */
			fdblks_delta = lcounter;
			mp->m_resblks_avail -= lcounter;
		}
		mp->m_resblks = request;
		if (fdblks_delta) {
			spin_unlock(&mp->m_sb_lock);
			error = xfs_mod_fdblocks(mp, fdblks_delta, 0);
			spin_lock(&mp->m_sb_lock);
		}

		goto out;
	}

	/*
	 * If the request is larger than the current reservation, reserve the
	 * blocks before we update the reserve counters. Sample m_fdblocks and
	 * perform a partial reservation if the request exceeds free space.
	 */
	error = -ENOSPC;
	do {
		free = percpu_counter_sum(&mp->m_fdblocks) -
						mp->m_alloc_set_aside;
		if (free <= 0)
			break;

		delta = request - mp->m_resblks;
		lcounter = free - delta;
		if (lcounter < 0)
			/* We can't satisfy the request, just get what we can */
			fdblks_delta = free;
		else
			fdblks_delta = delta;

		/*
		 * We'll either succeed in getting space from the free block
		 * count or we'll get an ENOSPC. If we get a ENOSPC, it means
		 * things changed while we were calculating fdblks_delta and so
		 * we should try again to see if there is anything left to
		 * reserve.
		 *
		 * Don't set the reserved flag here - we don't want to reserve
		 * the extra reserve blocks from the reserve.....
		 */
		spin_unlock(&mp->m_sb_lock);
		error = xfs_mod_fdblocks(mp, -fdblks_delta, 0);
		spin_lock(&mp->m_sb_lock);
	} while (error == -ENOSPC);

	/*
	 * Update the reserve counters if blocks have been successfully
	 * allocated.
	 */
	if (!error && fdblks_delta) {
		mp->m_resblks += fdblks_delta;
		mp->m_resblks_avail += fdblks_delta;
	}

out:
	if (outval) {
		outval->resblks = mp->m_resblks;
		outval->resblks_avail = mp->m_resblks_avail;
	}

	spin_unlock(&mp->m_sb_lock);
	return error;
}

int
xfs_fs_goingdown(
	xfs_mount_t	*mp,
	uint32_t	inflags)
{
	switch (inflags) {
	case XFS_FSOP_GOING_FLAGS_DEFAULT: {
		if (!freeze_bdev(mp->m_super->s_bdev)) {
			xfs_force_shutdown(mp, SHUTDOWN_FORCE_UMOUNT);
			thaw_bdev(mp->m_super->s_bdev);
		}
		break;
	}
	case XFS_FSOP_GOING_FLAGS_LOGFLUSH:
		xfs_force_shutdown(mp, SHUTDOWN_FORCE_UMOUNT);
		break;
	case XFS_FSOP_GOING_FLAGS_NOLOGFLUSH:
		xfs_force_shutdown(mp,
				SHUTDOWN_FORCE_UMOUNT | SHUTDOWN_LOG_IO_ERROR);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Force a shutdown of the filesystem instantly while keeping the filesystem
 * consistent. We don't do an unmount here; just shutdown the shop, make sure
 * that absolutely nothing persistent happens to this filesystem after this
 * point.
 */
void
xfs_do_force_shutdown(
	struct xfs_mount *mp,
	int		flags,
	char		*fname,
	int		lnnum)
{
	bool		logerror = flags & SHUTDOWN_LOG_IO_ERROR;

	/*
	 * No need to duplicate efforts.
	 */
	if (XFS_FORCED_SHUTDOWN(mp) && !logerror)
		return;

	/*
	 * This flags XFS_MOUNT_FS_SHUTDOWN, makes sure that we don't
	 * queue up anybody new on the log reservations, and wakes up
	 * everybody who's sleeping on log reservations to tell them
	 * the bad news.
	 */
	if (xfs_log_force_umount(mp, logerror))
		return;

	if (flags & SHUTDOWN_FORCE_UMOUNT) {
		xfs_alert(mp,
"User initiated shutdown received. Shutting down filesystem");
		return;
	}

	xfs_notice(mp,
"%s(0x%x) called from line %d of file %s. Return address = "PTR_FMT,
		__func__, flags, lnnum, fname, __return_address);

	if (flags & SHUTDOWN_CORRUPT_INCORE) {
		xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_CORRUPT,
"Corruption of in-memory data detected.  Shutting down filesystem");
		if (XFS_ERRLEVEL_HIGH <= xfs_error_level)
			xfs_stack_trace();
	} else if (logerror) {
		xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_LOGERROR,
			"Log I/O Error Detected. Shutting down filesystem");
	} else {
		xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_IOERROR,
			"I/O Error Detected. Shutting down filesystem");
	}

	xfs_alert(mp,
		"Please unmount the filesystem and rectify the problem(s)");
}

/*
 * Reserve free space for per-AG metadata.
 */
int
xfs_fs_reserve_ag_blocks(
	struct xfs_mount	*mp)
{
	xfs_agnumber_t		agno;
	struct xfs_perag	*pag;
	int			error = 0;
	int			err2;

	mp->m_finobt_nores = false;
	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		pag = xfs_perag_get(mp, agno);
		err2 = xfs_ag_resv_init(pag, NULL);
		xfs_perag_put(pag);
		if (err2 && !error)
			error = err2;
	}

	if (error && error != -ENOSPC) {
		xfs_warn(mp,
	"Error %d reserving per-AG metadata reserve pool.", error);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
	}

	return error;
}

/*
 * Free space reserved for per-AG metadata.
 */
int
xfs_fs_unreserve_ag_blocks(
	struct xfs_mount	*mp)
{
	xfs_agnumber_t		agno;
	struct xfs_perag	*pag;
	int			error = 0;
	int			err2;

	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		pag = xfs_perag_get(mp, agno);
		err2 = xfs_ag_resv_free(pag);
		xfs_perag_put(pag);
		if (err2 && !error)
			error = err2;
	}

	if (error)
		xfs_warn(mp,
	"Error %d freeing per-AG metadata reserve pool.", error);

	return error;
}
