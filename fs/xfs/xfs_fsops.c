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
#include "xfs_rtalloc.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_rtrefcount_btree.h"
#include "xfs_metafile.h"

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
	xfs_agnumber_t		oagcount = mp->m_sb.sb_agcount;
	struct xfs_buf		*bp;
	int			error;
	xfs_agnumber_t		nagcount;
	xfs_agnumber_t		nagimax = 0;
	xfs_rfsblock_t		nb, nb_div, nb_mod;
	int64_t			delta;
	bool			lastag_extended = false;
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
				XFS_FSS_TO_BB(mp, 1), &bp, NULL);
		if (error)
			return error;
		xfs_buf_relse(bp);
	}

	/* Make sure the new fs size won't cause problems with the log. */
	error = xfs_growfs_check_rtgeom(mp, nb, mp->m_sb.sb_rblocks,
			mp->m_sb.sb_rextsize);
	if (error)
		return error;

	nb_div = nb;
	nb_mod = do_div(nb_div, mp->m_sb.sb_agblocks);
	if (nb_mod && nb_mod >= XFS_MIN_AG_BLOCKS)
		nb_div++;
	else if (nb_mod)
		nb = nb_div * mp->m_sb.sb_agblocks;

	if (nb_div > XFS_MAX_AGNUMBER + 1) {
		nb_div = XFS_MAX_AGNUMBER + 1;
		nb = nb_div * mp->m_sb.sb_agblocks;
	}
	nagcount = nb_div;
	delta = nb - mp->m_sb.sb_dblocks;
	/*
	 * Reject filesystems with a single AG because they are not
	 * supported, and reject a shrink operation that would cause a
	 * filesystem to become unsupported.
	 */
	if (delta < 0 && nagcount < 2)
		return -EINVAL;

	/* No work to do */
	if (delta == 0)
		return 0;

	/* TODO: shrinking the entire AGs hasn't yet completed */
	if (nagcount < oagcount)
		return -EINVAL;

	/* allocate the new per-ag structures */
	error = xfs_initialize_perag(mp, oagcount, nagcount, nb, &nagimax);
	if (error)
		return error;

	if (delta > 0)
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growdata,
				XFS_GROWFS_SPACE_RES(mp), 0, XFS_TRANS_RESERVE,
				&tp);
	else
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_growdata, -delta, 0,
				0, &tp);
	if (error)
		goto out_free_unused_perag;

	last_pag = xfs_perag_get(mp, oagcount - 1);
	if (delta > 0) {
		error = xfs_resizefs_init_new_ags(tp, &id, oagcount, nagcount,
				delta, last_pag, &lastag_extended);
	} else {
		xfs_warn_experimental(mp, XFS_EXPERIMENTAL_SHRINK);
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
			xfs_ag_resv_free(pag);
			xfs_perag_put(pag);
		}
		/*
		 * Reserve AG metadata blocks. ENOSPC here does not mean there
		 * was a growfs failure, just that there still isn't space for
		 * new user data after the grow has been run.
		 */
		error = xfs_fs_reserve_ag_blocks(mp);
		if (error == -ENOSPC)
			error = 0;

		/* Compute new maxlevels for rt btrees. */
		xfs_rtrmapbt_compute_maxlevels(mp);
		xfs_rtrefcountbt_compute_maxlevels(mp);
	}

	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
out_free_unused_perag:
	if (nagcount > oagcount)
		xfs_free_perag_range(mp, oagcount, nagcount);
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
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!mutex_trylock(&mp->m_growlock))
		return -EWOULDBLOCK;

	/* we can't grow the data section when an internal RT section exists */
	if (in->newblocks != mp->m_sb.sb_dblocks && mp->m_sb.sb_rtstart) {
		error = -EINVAL;
		goto out_unlock;
	}

	/* update imaxpct separately to the physical grow of the filesystem */
	if (in->imaxpct != mp->m_sb.sb_imax_pct) {
		error = xfs_growfs_imaxpct(mp, in->imaxpct);
		if (error)
			goto out_unlock;
	}

	if (in->newblocks != mp->m_sb.sb_dblocks) {
		error = xfs_growfs_data_private(mp, in);
		if (error)
			goto out_unlock;
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

	/*
	 * Increment the generation unconditionally, after trying to update the
	 * secondary superblocks, as the new size is live already at this point.
	 */
	mp->m_generation++;
out_unlock:
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
 * Reserve the requested number of blocks if available. Otherwise return
 * as many as possible to satisfy the request. The actual number
 * reserved are returned in outval.
 */
int
xfs_reserve_blocks(
	struct xfs_mount	*mp,
	enum xfs_free_counter	ctr,
	uint64_t		request)
{
	int64_t			lcounter, delta;
	int64_t			fdblks_delta = 0;
	int64_t			free;
	int			error = 0;

	ASSERT(ctr < XC_FREE_NR);

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
	if (mp->m_free[ctr].res_total > request) {
		lcounter = mp->m_free[ctr].res_avail - request;
		if (lcounter > 0) {		/* release unused blocks */
			fdblks_delta = lcounter;
			mp->m_free[ctr].res_avail -= lcounter;
		}
		mp->m_free[ctr].res_total = request;
		if (fdblks_delta) {
			spin_unlock(&mp->m_sb_lock);
			xfs_add_freecounter(mp, ctr, fdblks_delta);
			spin_lock(&mp->m_sb_lock);
		}

		goto out;
	}

	/*
	 * If the request is larger than the current reservation, reserve the
	 * blocks before we update the reserve counters. Sample m_free and
	 * perform a partial reservation if the request exceeds free space.
	 *
	 * The code below estimates how many blocks it can request from
	 * fdblocks to stash in the reserve pool.  This is a classic TOCTOU
	 * race since fdblocks updates are not always coordinated via
	 * m_sb_lock.  Set the reserve size even if there's not enough free
	 * space to fill it because mod_fdblocks will refill an undersized
	 * reserve when it can.
	 */
	free = xfs_sum_freecounter_raw(mp, ctr) -
		xfs_freecounter_unavailable(mp, ctr);
	delta = request - mp->m_free[ctr].res_total;
	mp->m_free[ctr].res_total = request;
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
		error = xfs_dec_freecounter(mp, ctr, fdblks_delta, 0);
		if (!error)
			xfs_add_freecounter(mp, ctr, fdblks_delta);
		spin_lock(&mp->m_sb_lock);
	}
out:
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
		if (!bdev_freeze(mp->m_super->s_bdev)) {
			xfs_force_shutdown(mp, SHUTDOWN_FORCE_UMOUNT);
			bdev_thaw(mp->m_super->s_bdev);
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


	if (xfs_set_shutdown(mp)) {
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
	struct xfs_perag	*pag = NULL;
	int			error = 0;
	int			err2;

	mp->m_finobt_nores = false;
	while ((pag = xfs_perag_next(mp, pag))) {
		err2 = xfs_ag_resv_init(pag, NULL);
		if (err2 && !error)
			error = err2;
	}

	if (error && error != -ENOSPC) {
		xfs_warn(mp,
	"Error %d reserving per-AG metadata reserve pool.", error);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		return error;
	}

	err2 = xfs_metafile_resv_init(mp);
	if (err2 && err2 != -ENOSPC) {
		xfs_warn(mp,
	"Error %d reserving realtime metadata reserve pool.", err2);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);

		if (!error)
			error = err2;
	}

	return error;
}

/*
 * Free space reserved for per-AG metadata.
 */
void
xfs_fs_unreserve_ag_blocks(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag = NULL;

	xfs_metafile_resv_free(mp);
	while ((pag = xfs_perag_next(mp, pag)))
		xfs_ag_resv_free(pag);
}
