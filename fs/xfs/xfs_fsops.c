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
#include "xfs_log_priv.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_trace.h"

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
static int
xfs_resizefs_init_new_ags(
	struct xfs_trans	*tp,
	struct aghdr_init_data	*id,
	xfs_agnumber_t		oagcount,
	xfs_agnumber_t		nagcount,
	xfs_rfsblock_t		delta,
	struct xfs_perag	*last_pag,
	bool			*lastag_extended)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_rfsblock_t		nb = mp->m_sb.sb_dblocks + delta;
	int			error;

	*lastag_extended = false;

	INIT_LIST_HEAD(&id->buffer_list);
	for (id->agno = nagcount - 1;
	     id->agno >= oagcount;
	     id->agno--, delta -= id->agsize) {

		if (id->agno == nagcount - 1)
			id->agsize = nb - (id->agno *
					(xfs_rfsblock_t)mp->m_sb.sb_agblocks);
		else
			id->agsize = mp->m_sb.sb_agblocks;

		error = xfs_ag_init_headers(mp, id);
		if (error) {
			xfs_buf_delwri_cancel(&id->buffer_list);
			return error;
		}
	}

	error = xfs_buf_delwri_submit(&id->buffer_list);
	if (error)
		return error;

	if (delta) {
		*lastag_extended = true;
		error = xfs_ag_extend_space(last_pag, tp, delta);
	}
	return error;
}

/*
 * growfs operations
 */
static int
xfs_growfs_data_private(
	struct xfs_mount	*mp,		/* mount point for filesystem */
	struct xfs_growfs_data	*in)		/* growfs data input struct */
{
	struct xfs_buf		*bp;
	int			error;
	xfs_agnumber_t		nagcount;
	xfs_agnumber_t		nagimax = 0;
	xfs_rfsblock_t		nb, nb_div, nb_mod;
	int64_t			delta;
	bool			lastag_extended;
	xfs_agnumber_t		oagcount;
	struct xfs_trans	*tp;
	struct aghdr_init_data	id = {};
	struct xfs_perag	*last_pag;

	nb = in->newblocks;
	error = xfs_sb_validate_fsb_count(&mp->m_sb, nb);
	if (error)
		return error;

	if (nb > mp->m_sb.sb_dblocks) {
		error = xfs_buf_read_uncached(mp->m_ddev_targp,
				XFS_FSB_TO_BB(mp, nb) - XFS_FSS_TO_BB(mp, 1),
				XFS_FSS_TO_BB(mp, 1), 0, &bp, NULL);
		if (error)
			return error;
		xfs_buf_relse(bp);
	}

	nb_div = nb;
	nb_mod = do_div(nb_div, mp->m_sb.sb_agblocks);
	nagcount = nb_div + (nb_mod != 0);
	if (nb_mod && nb_mod < XFS_MIN_AG_BLOCKS) {
		nagcount--;
		nb = (xfs_rfsblock_t)nagcount * mp->m_sb.sb_agblocks;
	}
	delta = nb - mp->m_sb.sb_dblocks;
	/*
	 * Reject filesystems with a single AG because they are not
	 * supported, and reject a shrink operation that would cause a
	 * filesystem to become unsupported.
	 */
	if (delta < 0 && nagcount < 2)
		return -EINVAL;

	oagcount = mp->m_sb.sb_agcount;
	/* allocate the new per-ag structures */
	if (nagcount > oagcount) {
		error = xfs_initialize_perag(mp, nagcount, nb, &nagimax);
		if (error)
			return error;
	} else if (nagcount < oagcount) {
		/* TODO: shrinking the entire AGs hasn't yet completed */
		return -EINVAL;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growdata,
			(delta > 0 ? XFS_GROWFS_SPACE_RES(mp) : -delta), 0,
			XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	last_pag = xfs_perag_get(mp, oagcount - 1);
	if (delta > 0) {
		error = xfs_resizefs_init_new_ags(tp, &id, oagcount, nagcount,
				delta, last_pag, &lastag_extended);
	} else {
		xfs_warn_mount(mp, XFS_OPSTATE_WARNED_SHRINK,
	"EXPERIMENTAL online shrink feature in use. Use at your own risk!");

		error = xfs_ag_shrink_space(last_pag, &tp, -delta);
	}
	xfs_perag_put(last_pag);
	if (error)
		goto out_trans_cancel;

	/*
	 * Update changed superblock fields transactionally. These are not
	 * seen by the rest of the world until the transaction commit applies
	 * them atomically to the superblock.
	 */
	if (nagcount > oagcount)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_AGCOUNT, nagcount - oagcount);
	if (delta)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_DBLOCKS, delta);
	if (id.nfree)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, id.nfree);

	/*
	 * Sync sb counters now to reflect the updated values. This is
	 * particularly important for shrink because the write verifier
	 * will fail if sb_fdblocks is ever larger than sb_dblocks.
	 */
	if (xfs_has_lazysbcount(mp))
		xfs_log_sb(tp);

	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp);
	if (error)
		return error;

	/* New allocation groups fully initialized, so update mount struct */
	if (nagimax)
		mp->m_maxagi = nagimax;
	xfs_set_low_space_thresholds(mp);
	mp->m_alloc_set_aside = xfs_alloc_set_aside(mp);

	if (delta > 0) {
		/*
		 * If we expanded the last AG, free the per-AG reservation
		 * so we can reinitialize it with the new size.
		 */
		if (lastag_extended) {
			struct xfs_perag	*pag;

			pag = xfs_perag_get(mp, id.agno);
			error = xfs_ag_resv_free(pag);
			xfs_perag_put(pag);
			if (error)
				return error;
		}
		/*
		 * Reserve AG metadata blocks. ENOSPC here does not mean there
		 * was a growfs failure, just that there still isn't space for
		 * new user data after the grow has been run.
		 */
		error = xfs_fs_reserve_ag_blocks(mp);
		if (error == -ENOSPC)
			error = 0;
	}
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
}

static int
xfs_growfs_log_private(
	struct xfs_mount	*mp,	/* mount point for filesystem */
	struct xfs_growfs_log	*in)	/* growfs log input struct */
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
	struct xfs_growfs_log	*in)
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
						xfs_fdblocks_unavailable(mp);
	cnt->freertx = percpu_counter_read_positive(&mp->m_frextents);
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
	 *
	 * The code below estimates how many blocks it can request from
	 * fdblocks to stash in the reserve pool.  This is a classic TOCTOU
	 * race since fdblocks updates are not always coordinated via
	 * m_sb_lock.  Set the reserve size even if there's not enough free
	 * space to fill it because mod_fdblocks will refill an undersized
	 * reserve when it can.
	 */
	free = percpu_counter_sum(&mp->m_fdblocks) -
						xfs_fdblocks_unavailable(mp);
	delta = request - mp->m_resblks;
	mp->m_resblks = request;
	if (delta > 0 && free > 0) {
		/*
		 * We'll either succeed in getting space from the free block
		 * count or we'll get an ENOSPC.  Don't set the reserved flag
		 * here - we don't want to reserve the extra reserve blocks
		 * from the reserve.
		 *
		 * The desired reserve size can change after we drop the lock.
		 * Use mod_fdblocks to put the space into the reserve or into
		 * fdblocks as appropriate.
		 */
		fdblks_delta = min(free, delta);
		spin_unlock(&mp->m_sb_lock);
		error = xfs_mod_fdblocks(mp, -fdblks_delta, 0);
		if (!error)
			xfs_mod_fdblocks(mp, fdblks_delta, 0);
		spin_lock(&mp->m_sb_lock);
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
 *
 * The shutdown state change is atomic, resulting in the first and only the
 * first shutdown call processing the shutdown. This means we only shutdown the
 * log once as it requires, and we don't spam the logs when multiple concurrent
 * shutdowns race to set the shutdown flags.
 */
void
xfs_do_force_shutdown(
	struct xfs_mount *mp,
	uint32_t	flags,
	char		*fname,
	int		lnnum)
{
	int		tag;
	const char	*why;


	if (test_and_set_bit(XFS_OPSTATE_SHUTDOWN, &mp->m_opstate)) {
		xlog_shutdown_wait(mp->m_log);
		return;
	}
	if (mp->m_sb_bp)
		mp->m_sb_bp->b_flags |= XBF_DONE;

	if (flags & SHUTDOWN_FORCE_UMOUNT)
		xfs_alert(mp, "User initiated shutdown received.");

	if (xlog_force_shutdown(mp->m_log, flags)) {
		tag = XFS_PTAG_SHUTDOWN_LOGERROR;
		why = "Log I/O Error";
	} else if (flags & SHUTDOWN_CORRUPT_INCORE) {
		tag = XFS_PTAG_SHUTDOWN_CORRUPT;
		why = "Corruption of in-memory data";
	} else if (flags & SHUTDOWN_CORRUPT_ONDISK) {
		tag = XFS_PTAG_SHUTDOWN_CORRUPT;
		why = "Corruption of on-disk metadata";
	} else if (flags & SHUTDOWN_DEVICE_REMOVED) {
		tag = XFS_PTAG_SHUTDOWN_IOERROR;
		why = "Block device removal";
	} else {
		tag = XFS_PTAG_SHUTDOWN_IOERROR;
		why = "Metadata I/O Error";
	}

	trace_xfs_force_shutdown(mp, tag, flags, fname, lnnum);

	xfs_alert_tag(mp, tag,
"%s (0x%x) detected at %pS (%s:%d).  Shutting down filesystem.",
			why, flags, __return_address, fname, lnnum);
	xfs_alert(mp,
		"Please unmount the filesystem and rectify the problem(s)");
	if (xfs_error_level >= XFS_ERRLEVEL_HIGH)
		xfs_stack_trace();
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
	for_each_perag(mp, agno, pag) {
		err2 = xfs_ag_resv_init(pag, NULL);
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

	for_each_perag(mp, agno, pag) {
		err2 = xfs_ag_resv_free(pag);
		if (err2 && !error)
			error = err2;
	}

	if (error)
		xfs_warn(mp,
	"Error %d freeing per-AG metadata reserve pool.", error);

	return error;
}
