/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_quota.h"
#include "xfs_trans_priv.h"
#include "xfs_trans_space.h"


STATIC void	xfs_trans_apply_sb_deltas(xfs_trans_t *);
STATIC uint	xfs_trans_count_vecs(xfs_trans_t *);
STATIC void	xfs_trans_fill_vecs(xfs_trans_t *, xfs_log_iovec_t *);
STATIC void	xfs_trans_uncommit(xfs_trans_t *, uint);
STATIC void	xfs_trans_committed(xfs_trans_t *, int);
STATIC void	xfs_trans_chunk_committed(xfs_log_item_chunk_t *, xfs_lsn_t, int);
STATIC void	xfs_trans_free(xfs_trans_t *);

kmem_zone_t	*xfs_trans_zone;


/*
 * Reservation functions here avoid a huge stack in xfs_trans_init
 * due to register overflow from temporaries in the calculations.
 */

STATIC uint
xfs_calc_write_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_WRITE_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_itruncate_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_ITRUNCATE_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_rename_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_RENAME_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_link_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_LINK_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_remove_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_REMOVE_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_symlink_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_SYMLINK_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_create_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_CREATE_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_mkdir_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_MKDIR_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_ifree_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_IFREE_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_ichange_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_ICHANGE_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_growdata_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_GROWDATA_LOG_RES(mp);
}

STATIC uint
xfs_calc_growrtalloc_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_GROWRTALLOC_LOG_RES(mp);
}

STATIC uint
xfs_calc_growrtzero_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_GROWRTZERO_LOG_RES(mp);
}

STATIC uint
xfs_calc_growrtfree_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_GROWRTFREE_LOG_RES(mp);
}

STATIC uint
xfs_calc_swrite_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_SWRITE_LOG_RES(mp);
}

STATIC uint
xfs_calc_writeid_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_WRITEID_LOG_RES(mp);
}

STATIC uint
xfs_calc_addafork_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_ADDAFORK_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_attrinval_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_ATTRINVAL_LOG_RES(mp);
}

STATIC uint
xfs_calc_attrset_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_ATTRSET_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_attrrm_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_ATTRRM_LOG_RES(mp) + XFS_DQUOT_LOGRES(mp);
}

STATIC uint
xfs_calc_clear_agi_bucket_reservation(xfs_mount_t *mp)
{
	return XFS_CALC_CLEAR_AGI_BUCKET_LOG_RES(mp);
}

/*
 * Initialize the precomputed transaction reservation values
 * in the mount structure.
 */
void
xfs_trans_init(
	xfs_mount_t	*mp)
{
	xfs_trans_reservations_t	*resp;

	resp = &(mp->m_reservations);
	resp->tr_write = xfs_calc_write_reservation(mp);
	resp->tr_itruncate = xfs_calc_itruncate_reservation(mp);
	resp->tr_rename = xfs_calc_rename_reservation(mp);
	resp->tr_link = xfs_calc_link_reservation(mp);
	resp->tr_remove = xfs_calc_remove_reservation(mp);
	resp->tr_symlink = xfs_calc_symlink_reservation(mp);
	resp->tr_create = xfs_calc_create_reservation(mp);
	resp->tr_mkdir = xfs_calc_mkdir_reservation(mp);
	resp->tr_ifree = xfs_calc_ifree_reservation(mp);
	resp->tr_ichange = xfs_calc_ichange_reservation(mp);
	resp->tr_growdata = xfs_calc_growdata_reservation(mp);
	resp->tr_swrite = xfs_calc_swrite_reservation(mp);
	resp->tr_writeid = xfs_calc_writeid_reservation(mp);
	resp->tr_addafork = xfs_calc_addafork_reservation(mp);
	resp->tr_attrinval = xfs_calc_attrinval_reservation(mp);
	resp->tr_attrset = xfs_calc_attrset_reservation(mp);
	resp->tr_attrrm = xfs_calc_attrrm_reservation(mp);
	resp->tr_clearagi = xfs_calc_clear_agi_bucket_reservation(mp);
	resp->tr_growrtalloc = xfs_calc_growrtalloc_reservation(mp);
	resp->tr_growrtzero = xfs_calc_growrtzero_reservation(mp);
	resp->tr_growrtfree = xfs_calc_growrtfree_reservation(mp);
}

/*
 * This routine is called to allocate a transaction structure.
 * The type parameter indicates the type of the transaction.  These
 * are enumerated in xfs_trans.h.
 *
 * Dynamically allocate the transaction structure from the transaction
 * zone, initialize it, and return it to the caller.
 */
xfs_trans_t *
xfs_trans_alloc(
	xfs_mount_t	*mp,
	uint		type)
{
	vfs_wait_for_freeze(XFS_MTOVFS(mp), SB_FREEZE_TRANS);
	return _xfs_trans_alloc(mp, type);
}

xfs_trans_t *
_xfs_trans_alloc(
	xfs_mount_t	*mp,
	uint		type)
{
	xfs_trans_t	*tp;

	atomic_inc(&mp->m_active_trans);

	tp = kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);
	tp->t_magic = XFS_TRANS_MAGIC;
	tp->t_type = type;
	tp->t_mountp = mp;
	tp->t_items_free = XFS_LIC_NUM_SLOTS;
	tp->t_busy_free = XFS_LBC_NUM_SLOTS;
	XFS_LIC_INIT(&(tp->t_items));
	XFS_LBC_INIT(&(tp->t_busy));
	return tp;
}

/*
 * This is called to create a new transaction which will share the
 * permanent log reservation of the given transaction.  The remaining
 * unused block and rt extent reservations are also inherited.  This
 * implies that the original transaction is no longer allowed to allocate
 * blocks.  Locks and log items, however, are no inherited.  They must
 * be added to the new transaction explicitly.
 */
xfs_trans_t *
xfs_trans_dup(
	xfs_trans_t	*tp)
{
	xfs_trans_t	*ntp;

	ntp = kmem_zone_zalloc(xfs_trans_zone, KM_SLEEP);

	/*
	 * Initialize the new transaction structure.
	 */
	ntp->t_magic = XFS_TRANS_MAGIC;
	ntp->t_type = tp->t_type;
	ntp->t_mountp = tp->t_mountp;
	ntp->t_items_free = XFS_LIC_NUM_SLOTS;
	ntp->t_busy_free = XFS_LBC_NUM_SLOTS;
	XFS_LIC_INIT(&(ntp->t_items));
	XFS_LBC_INIT(&(ntp->t_busy));

	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(tp->t_ticket != NULL);

	ntp->t_flags = XFS_TRANS_PERM_LOG_RES | (tp->t_flags & XFS_TRANS_RESERVE);
	ntp->t_ticket = tp->t_ticket;
	ntp->t_blk_res = tp->t_blk_res - tp->t_blk_res_used;
	tp->t_blk_res = tp->t_blk_res_used;
	ntp->t_rtx_res = tp->t_rtx_res - tp->t_rtx_res_used;
	tp->t_rtx_res = tp->t_rtx_res_used;
	ntp->t_pflags = tp->t_pflags;

	XFS_TRANS_DUP_DQINFO(tp->t_mountp, tp, ntp);

	atomic_inc(&tp->t_mountp->m_active_trans);
	return ntp;
}

/*
 * This is called to reserve free disk blocks and log space for the
 * given transaction.  This must be done before allocating any resources
 * within the transaction.
 *
 * This will return ENOSPC if there are not enough blocks available.
 * It will sleep waiting for available log space.
 * The only valid value for the flags parameter is XFS_RES_LOG_PERM, which
 * is used by long running transactions.  If any one of the reservations
 * fails then they will all be backed out.
 *
 * This does not do quota reservations. That typically is done by the
 * caller afterwards.
 */
int
xfs_trans_reserve(
	xfs_trans_t	*tp,
	uint		blocks,
	uint		logspace,
	uint		rtextents,
	uint		flags,
	uint		logcount)
{
	int		log_flags;
	int		error = 0;
	int		rsvd = (tp->t_flags & XFS_TRANS_RESERVE) != 0;

	/* Mark this thread as being in a transaction */
	current_set_flags_nested(&tp->t_pflags, PF_FSTRANS);

	/*
	 * Attempt to reserve the needed disk blocks by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (blocks > 0) {
		error = xfs_mod_incore_sb(tp->t_mountp, XFS_SBS_FDBLOCKS,
					  -blocks, rsvd);
		if (error != 0) {
			current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
			return (XFS_ERROR(ENOSPC));
		}
		tp->t_blk_res += blocks;
	}

	/*
	 * Reserve the log space needed for this transaction.
	 */
	if (logspace > 0) {
		ASSERT((tp->t_log_res == 0) || (tp->t_log_res == logspace));
		ASSERT((tp->t_log_count == 0) ||
			(tp->t_log_count == logcount));
		if (flags & XFS_TRANS_PERM_LOG_RES) {
			log_flags = XFS_LOG_PERM_RESERV;
			tp->t_flags |= XFS_TRANS_PERM_LOG_RES;
		} else {
			ASSERT(tp->t_ticket == NULL);
			ASSERT(!(tp->t_flags & XFS_TRANS_PERM_LOG_RES));
			log_flags = 0;
		}

		error = xfs_log_reserve(tp->t_mountp, logspace, logcount,
					&tp->t_ticket,
					XFS_TRANSACTION, log_flags, tp->t_type);
		if (error) {
			goto undo_blocks;
		}
		tp->t_log_res = logspace;
		tp->t_log_count = logcount;
	}

	/*
	 * Attempt to reserve the needed realtime extents by decrementing
	 * the number needed from the number available.  This will
	 * fail if the count would go below zero.
	 */
	if (rtextents > 0) {
		error = xfs_mod_incore_sb(tp->t_mountp, XFS_SBS_FREXTENTS,
					  -rtextents, rsvd);
		if (error) {
			error = XFS_ERROR(ENOSPC);
			goto undo_log;
		}
		tp->t_rtx_res += rtextents;
	}

	return 0;

	/*
	 * Error cases jump to one of these labels to undo any
	 * reservations which have already been performed.
	 */
undo_log:
	if (logspace > 0) {
		if (flags & XFS_TRANS_PERM_LOG_RES) {
			log_flags = XFS_LOG_REL_PERM_RESERV;
		} else {
			log_flags = 0;
		}
		xfs_log_done(tp->t_mountp, tp->t_ticket, NULL, log_flags);
		tp->t_ticket = NULL;
		tp->t_log_res = 0;
		tp->t_flags &= ~XFS_TRANS_PERM_LOG_RES;
	}

undo_blocks:
	if (blocks > 0) {
		(void) xfs_mod_incore_sb(tp->t_mountp, XFS_SBS_FDBLOCKS,
					 blocks, rsvd);
		tp->t_blk_res = 0;
	}

	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);

	return error;
}


/*
 * Record the indicated change to the given field for application
 * to the file system's superblock when the transaction commits.
 * For now, just store the change in the transaction structure.
 *
 * Mark the transaction structure to indicate that the superblock
 * needs to be updated before committing.
 */
void
xfs_trans_mod_sb(
	xfs_trans_t	*tp,
	uint		field,
	long		delta)
{

	switch (field) {
	case XFS_TRANS_SB_ICOUNT:
		tp->t_icount_delta += delta;
		break;
	case XFS_TRANS_SB_IFREE:
		tp->t_ifree_delta += delta;
		break;
	case XFS_TRANS_SB_FDBLOCKS:
		/*
		 * Track the number of blocks allocated in the
		 * transaction.  Make sure it does not exceed the
		 * number reserved.
		 */
		if (delta < 0) {
			tp->t_blk_res_used += (uint)-delta;
			ASSERT(tp->t_blk_res_used <= tp->t_blk_res);
		}
		tp->t_fdblocks_delta += delta;
		break;
	case XFS_TRANS_SB_RES_FDBLOCKS:
		/*
		 * The allocation has already been applied to the
		 * in-core superblock's counter.  This should only
		 * be applied to the on-disk superblock.
		 */
		ASSERT(delta < 0);
		tp->t_res_fdblocks_delta += delta;
		break;
	case XFS_TRANS_SB_FREXTENTS:
		/*
		 * Track the number of blocks allocated in the
		 * transaction.  Make sure it does not exceed the
		 * number reserved.
		 */
		if (delta < 0) {
			tp->t_rtx_res_used += (uint)-delta;
			ASSERT(tp->t_rtx_res_used <= tp->t_rtx_res);
		}
		tp->t_frextents_delta += delta;
		break;
	case XFS_TRANS_SB_RES_FREXTENTS:
		/*
		 * The allocation has already been applied to the
		 * in-core superblock's counter.  This should only
		 * be applied to the on-disk superblock.
		 */
		ASSERT(delta < 0);
		tp->t_res_frextents_delta += delta;
		break;
	case XFS_TRANS_SB_DBLOCKS:
		ASSERT(delta > 0);
		tp->t_dblocks_delta += delta;
		break;
	case XFS_TRANS_SB_AGCOUNT:
		ASSERT(delta > 0);
		tp->t_agcount_delta += delta;
		break;
	case XFS_TRANS_SB_IMAXPCT:
		tp->t_imaxpct_delta += delta;
		break;
	case XFS_TRANS_SB_REXTSIZE:
		tp->t_rextsize_delta += delta;
		break;
	case XFS_TRANS_SB_RBMBLOCKS:
		tp->t_rbmblocks_delta += delta;
		break;
	case XFS_TRANS_SB_RBLOCKS:
		tp->t_rblocks_delta += delta;
		break;
	case XFS_TRANS_SB_REXTENTS:
		tp->t_rextents_delta += delta;
		break;
	case XFS_TRANS_SB_REXTSLOG:
		tp->t_rextslog_delta += delta;
		break;
	default:
		ASSERT(0);
		return;
	}

	tp->t_flags |= (XFS_TRANS_SB_DIRTY | XFS_TRANS_DIRTY);
}

/*
 * xfs_trans_apply_sb_deltas() is called from the commit code
 * to bring the superblock buffer into the current transaction
 * and modify it as requested by earlier calls to xfs_trans_mod_sb().
 *
 * For now we just look at each field allowed to change and change
 * it if necessary.
 */
STATIC void
xfs_trans_apply_sb_deltas(
	xfs_trans_t	*tp)
{
	xfs_sb_t	*sbp;
	xfs_buf_t	*bp;
	int		whole = 0;

	bp = xfs_trans_getsb(tp, tp->t_mountp, 0);
	sbp = XFS_BUF_TO_SBP(bp);

	/*
	 * Check that superblock mods match the mods made to AGF counters.
	 */
	ASSERT((tp->t_fdblocks_delta + tp->t_res_fdblocks_delta) ==
	       (tp->t_ag_freeblks_delta + tp->t_ag_flist_delta +
		tp->t_ag_btree_delta));

	if (tp->t_icount_delta != 0) {
		INT_MOD(sbp->sb_icount, ARCH_CONVERT, tp->t_icount_delta);
	}
	if (tp->t_ifree_delta != 0) {
		INT_MOD(sbp->sb_ifree, ARCH_CONVERT, tp->t_ifree_delta);
	}

	if (tp->t_fdblocks_delta != 0) {
		INT_MOD(sbp->sb_fdblocks, ARCH_CONVERT, tp->t_fdblocks_delta);
	}
	if (tp->t_res_fdblocks_delta != 0) {
		INT_MOD(sbp->sb_fdblocks, ARCH_CONVERT, tp->t_res_fdblocks_delta);
	}

	if (tp->t_frextents_delta != 0) {
		INT_MOD(sbp->sb_frextents, ARCH_CONVERT, tp->t_frextents_delta);
	}
	if (tp->t_res_frextents_delta != 0) {
		INT_MOD(sbp->sb_frextents, ARCH_CONVERT, tp->t_res_frextents_delta);
	}
	if (tp->t_dblocks_delta != 0) {
		INT_MOD(sbp->sb_dblocks, ARCH_CONVERT, tp->t_dblocks_delta);
		whole = 1;
	}
	if (tp->t_agcount_delta != 0) {
		INT_MOD(sbp->sb_agcount, ARCH_CONVERT, tp->t_agcount_delta);
		whole = 1;
	}
	if (tp->t_imaxpct_delta != 0) {
		INT_MOD(sbp->sb_imax_pct, ARCH_CONVERT, tp->t_imaxpct_delta);
		whole = 1;
	}
	if (tp->t_rextsize_delta != 0) {
		INT_MOD(sbp->sb_rextsize, ARCH_CONVERT, tp->t_rextsize_delta);
		whole = 1;
	}
	if (tp->t_rbmblocks_delta != 0) {
		INT_MOD(sbp->sb_rbmblocks, ARCH_CONVERT, tp->t_rbmblocks_delta);
		whole = 1;
	}
	if (tp->t_rblocks_delta != 0) {
		INT_MOD(sbp->sb_rblocks, ARCH_CONVERT, tp->t_rblocks_delta);
		whole = 1;
	}
	if (tp->t_rextents_delta != 0) {
		INT_MOD(sbp->sb_rextents, ARCH_CONVERT, tp->t_rextents_delta);
		whole = 1;
	}
	if (tp->t_rextslog_delta != 0) {
		INT_MOD(sbp->sb_rextslog, ARCH_CONVERT, tp->t_rextslog_delta);
		whole = 1;
	}

	if (whole)
		/*
		 * Log the whole thing, the fields are noncontiguous.
		 */
		xfs_trans_log_buf(tp, bp, 0, sizeof(xfs_sb_t) - 1);
	else
		/*
		 * Since all the modifiable fields are contiguous, we
		 * can get away with this.
		 */
		xfs_trans_log_buf(tp, bp, offsetof(xfs_sb_t, sb_icount),
				  offsetof(xfs_sb_t, sb_frextents) +
				  sizeof(sbp->sb_frextents) - 1);

	XFS_MTOVFS(tp->t_mountp)->vfs_super->s_dirt = 1;
}

/*
 * xfs_trans_unreserve_and_mod_sb() is called to release unused
 * reservations and apply superblock counter changes to the in-core
 * superblock.
 *
 * This is done efficiently with a single call to xfs_mod_incore_sb_batch().
 */
STATIC void
xfs_trans_unreserve_and_mod_sb(
	xfs_trans_t	*tp)
{
	xfs_mod_sb_t	msb[14];	/* If you add cases, add entries */
	xfs_mod_sb_t	*msbp;
	/* REFERENCED */
	int		error;
	int		rsvd;

	msbp = msb;
	rsvd = (tp->t_flags & XFS_TRANS_RESERVE) != 0;

	/*
	 * Release any reserved blocks.  Any that were allocated
	 * will be taken back again by fdblocks_delta below.
	 */
	if (tp->t_blk_res > 0) {
		msbp->msb_field = XFS_SBS_FDBLOCKS;
		msbp->msb_delta = tp->t_blk_res;
		msbp++;
	}

	/*
	 * Release any reserved real time extents .  Any that were
	 * allocated will be taken back again by frextents_delta below.
	 */
	if (tp->t_rtx_res > 0) {
		msbp->msb_field = XFS_SBS_FREXTENTS;
		msbp->msb_delta = tp->t_rtx_res;
		msbp++;
	}

	/*
	 * Apply any superblock modifications to the in-core version.
	 * The t_res_fdblocks_delta and t_res_frextents_delta fields are
	 * explicitly NOT applied to the in-core superblock.
	 * The idea is that that has already been done.
	 */
	if (tp->t_flags & XFS_TRANS_SB_DIRTY) {
		if (tp->t_icount_delta != 0) {
			msbp->msb_field = XFS_SBS_ICOUNT;
			msbp->msb_delta = (int)tp->t_icount_delta;
			msbp++;
		}
		if (tp->t_ifree_delta != 0) {
			msbp->msb_field = XFS_SBS_IFREE;
			msbp->msb_delta = (int)tp->t_ifree_delta;
			msbp++;
		}
		if (tp->t_fdblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_FDBLOCKS;
			msbp->msb_delta = (int)tp->t_fdblocks_delta;
			msbp++;
		}
		if (tp->t_frextents_delta != 0) {
			msbp->msb_field = XFS_SBS_FREXTENTS;
			msbp->msb_delta = (int)tp->t_frextents_delta;
			msbp++;
		}
		if (tp->t_dblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_DBLOCKS;
			msbp->msb_delta = (int)tp->t_dblocks_delta;
			msbp++;
		}
		if (tp->t_agcount_delta != 0) {
			msbp->msb_field = XFS_SBS_AGCOUNT;
			msbp->msb_delta = (int)tp->t_agcount_delta;
			msbp++;
		}
		if (tp->t_imaxpct_delta != 0) {
			msbp->msb_field = XFS_SBS_IMAX_PCT;
			msbp->msb_delta = (int)tp->t_imaxpct_delta;
			msbp++;
		}
		if (tp->t_rextsize_delta != 0) {
			msbp->msb_field = XFS_SBS_REXTSIZE;
			msbp->msb_delta = (int)tp->t_rextsize_delta;
			msbp++;
		}
		if (tp->t_rbmblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_RBMBLOCKS;
			msbp->msb_delta = (int)tp->t_rbmblocks_delta;
			msbp++;
		}
		if (tp->t_rblocks_delta != 0) {
			msbp->msb_field = XFS_SBS_RBLOCKS;
			msbp->msb_delta = (int)tp->t_rblocks_delta;
			msbp++;
		}
		if (tp->t_rextents_delta != 0) {
			msbp->msb_field = XFS_SBS_REXTENTS;
			msbp->msb_delta = (int)tp->t_rextents_delta;
			msbp++;
		}
		if (tp->t_rextslog_delta != 0) {
			msbp->msb_field = XFS_SBS_REXTSLOG;
			msbp->msb_delta = (int)tp->t_rextslog_delta;
			msbp++;
		}
	}

	/*
	 * If we need to change anything, do it.
	 */
	if (msbp > msb) {
		error = xfs_mod_incore_sb_batch(tp->t_mountp, msb,
			(uint)(msbp - msb), rsvd);
		ASSERT(error == 0);
	}
}


/*
 * xfs_trans_commit
 *
 * Commit the given transaction to the log a/synchronously.
 *
 * XFS disk error handling mechanism is not based on a typical
 * transaction abort mechanism. Logically after the filesystem
 * gets marked 'SHUTDOWN', we can't let any new transactions
 * be durable - ie. committed to disk - because some metadata might
 * be inconsistent. In such cases, this returns an error, and the
 * caller may assume that all locked objects joined to the transaction
 * have already been unlocked as if the commit had succeeded.
 * Do not reference the transaction structure after this call.
 */
 /*ARGSUSED*/
int
_xfs_trans_commit(
	xfs_trans_t	*tp,
	uint		flags,
	xfs_lsn_t	*commit_lsn_p,
	int		*log_flushed)
{
	xfs_log_iovec_t		*log_vector;
	int			nvec;
	xfs_mount_t		*mp;
	xfs_lsn_t		commit_lsn;
	/* REFERENCED */
	int			error;
	int			log_flags;
	int			sync;
#define	XFS_TRANS_LOGVEC_COUNT	16
	xfs_log_iovec_t		log_vector_fast[XFS_TRANS_LOGVEC_COUNT];
	void			*commit_iclog;
	int			shutdown;

	commit_lsn = -1;

	/*
	 * Determine whether this commit is releasing a permanent
	 * log reservation or not.
	 */
	if (flags & XFS_TRANS_RELEASE_LOG_RES) {
		ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
		log_flags = XFS_LOG_REL_PERM_RESERV;
	} else {
		log_flags = 0;
	}
	mp = tp->t_mountp;

	/*
	 * If there is nothing to be logged by the transaction,
	 * then unlock all of the items associated with the
	 * transaction and free the transaction structure.
	 * Also make sure to return any reserved blocks to
	 * the free pool.
	 */
shut_us_down:
	shutdown = XFS_FORCED_SHUTDOWN(mp) ? EIO : 0;
	if (!(tp->t_flags & XFS_TRANS_DIRTY) || shutdown) {
		xfs_trans_unreserve_and_mod_sb(tp);
		/*
		 * It is indeed possible for the transaction to be
		 * not dirty but the dqinfo portion to be. All that
		 * means is that we have some (non-persistent) quota
		 * reservations that need to be unreserved.
		 */
		XFS_TRANS_UNRESERVE_AND_MOD_DQUOTS(mp, tp);
		if (tp->t_ticket) {
			commit_lsn = xfs_log_done(mp, tp->t_ticket,
							NULL, log_flags);
			if (commit_lsn == -1 && !shutdown)
				shutdown = XFS_ERROR(EIO);
		}
		current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
		xfs_trans_free_items(tp, shutdown? XFS_TRANS_ABORT : 0);
		xfs_trans_free_busy(tp);
		xfs_trans_free(tp);
		XFS_STATS_INC(xs_trans_empty);
		if (commit_lsn_p)
			*commit_lsn_p = commit_lsn;
		return (shutdown);
	}
	ASSERT(tp->t_ticket != NULL);

	/*
	 * If we need to update the superblock, then do it now.
	 */
	if (tp->t_flags & XFS_TRANS_SB_DIRTY) {
		xfs_trans_apply_sb_deltas(tp);
	}
	XFS_TRANS_APPLY_DQUOT_DELTAS(mp, tp);

	/*
	 * Ask each log item how many log_vector entries it will
	 * need so we can figure out how many to allocate.
	 * Try to avoid the kmem_alloc() call in the common case
	 * by using a vector from the stack when it fits.
	 */
	nvec = xfs_trans_count_vecs(tp);
	if (nvec == 0) {
		xfs_force_shutdown(mp, SHUTDOWN_LOG_IO_ERROR);
		goto shut_us_down;
	} else if (nvec <= XFS_TRANS_LOGVEC_COUNT) {
		log_vector = log_vector_fast;
	} else {
		log_vector = (xfs_log_iovec_t *)kmem_alloc(nvec *
						   sizeof(xfs_log_iovec_t),
						   KM_SLEEP);
	}

	/*
	 * Fill in the log_vector and pin the logged items, and
	 * then write the transaction to the log.
	 */
	xfs_trans_fill_vecs(tp, log_vector);

	error = xfs_log_write(mp, log_vector, nvec, tp->t_ticket, &(tp->t_lsn));

	/*
	 * The transaction is committed incore here, and can go out to disk
	 * at any time after this call.  However, all the items associated
	 * with the transaction are still locked and pinned in memory.
	 */
	commit_lsn = xfs_log_done(mp, tp->t_ticket, &commit_iclog, log_flags);

	tp->t_commit_lsn = commit_lsn;
	if (nvec > XFS_TRANS_LOGVEC_COUNT) {
		kmem_free(log_vector, nvec * sizeof(xfs_log_iovec_t));
	}

	if (commit_lsn_p)
		*commit_lsn_p = commit_lsn;

	/*
	 * If we got a log write error. Unpin the logitems that we
	 * had pinned, clean up, free trans structure, and return error.
	 */
	if (error || commit_lsn == -1) {
		current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
		xfs_trans_uncommit(tp, flags|XFS_TRANS_ABORT);
		return XFS_ERROR(EIO);
	}

	/*
	 * Once the transaction has committed, unused
	 * reservations need to be released and changes to
	 * the superblock need to be reflected in the in-core
	 * version.  Do that now.
	 */
	xfs_trans_unreserve_and_mod_sb(tp);

	sync = tp->t_flags & XFS_TRANS_SYNC;

	/*
	 * Tell the LM to call the transaction completion routine
	 * when the log write with LSN commit_lsn completes (e.g.
	 * when the transaction commit really hits the on-disk log).
	 * After this call we cannot reference tp, because the call
	 * can happen at any time and the call will free the transaction
	 * structure pointed to by tp.  The only case where we call
	 * the completion routine (xfs_trans_committed) directly is
	 * if the log is turned off on a debug kernel or we're
	 * running in simulation mode (the log is explicitly turned
	 * off).
	 */
	tp->t_logcb.cb_func = (void(*)(void*, int))xfs_trans_committed;
	tp->t_logcb.cb_arg = tp;

	/*
	 * We need to pass the iclog buffer which was used for the
	 * transaction commit record into this function, and attach
	 * the callback to it. The callback must be attached before
	 * the items are unlocked to avoid racing with other threads
	 * waiting for an item to unlock.
	 */
	shutdown = xfs_log_notify(mp, commit_iclog, &(tp->t_logcb));

	/*
	 * Mark this thread as no longer being in a transaction
	 */
	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);

	/*
	 * Once all the items of the transaction have been copied
	 * to the in core log and the callback is attached, the
	 * items can be unlocked.
	 *
	 * This will free descriptors pointing to items which were
	 * not logged since there is nothing more to do with them.
	 * For items which were logged, we will keep pointers to them
	 * so they can be unpinned after the transaction commits to disk.
	 * This will also stamp each modified meta-data item with
	 * the commit lsn of this transaction for dependency tracking
	 * purposes.
	 */
	xfs_trans_unlock_items(tp, commit_lsn);

	/*
	 * If we detected a log error earlier, finish committing
	 * the transaction now (unpin log items, etc).
	 *
	 * Order is critical here, to avoid using the transaction
	 * pointer after its been freed (by xfs_trans_committed
	 * either here now, or as a callback).  We cannot do this
	 * step inside xfs_log_notify as was done earlier because
	 * of this issue.
	 */
	if (shutdown)
		xfs_trans_committed(tp, XFS_LI_ABORTED);

	/*
	 * Now that the xfs_trans_committed callback has been attached,
	 * and the items are released we can finally allow the iclog to
	 * go to disk.
	 */
	error = xfs_log_release_iclog(mp, commit_iclog);

	/*
	 * If the transaction needs to be synchronous, then force the
	 * log out now and wait for it.
	 */
	if (sync) {
		if (!error) {
			error = _xfs_log_force(mp, commit_lsn,
				      XFS_LOG_FORCE | XFS_LOG_SYNC,
				      log_flushed);
		}
		XFS_STATS_INC(xs_trans_sync);
	} else {
		XFS_STATS_INC(xs_trans_async);
	}

	return (error);
}


/*
 * Total up the number of log iovecs needed to commit this
 * transaction.  The transaction itself needs one for the
 * transaction header.  Ask each dirty item in turn how many
 * it needs to get the total.
 */
STATIC uint
xfs_trans_count_vecs(
	xfs_trans_t	*tp)
{
	int			nvecs;
	xfs_log_item_desc_t	*lidp;

	nvecs = 1;
	lidp = xfs_trans_first_item(tp);
	ASSERT(lidp != NULL);

	/* In the non-debug case we need to start bailing out if we
	 * didn't find a log_item here, return zero and let trans_commit
	 * deal with it.
	 */
	if (lidp == NULL)
		return 0;

	while (lidp != NULL) {
		/*
		 * Skip items which aren't dirty in this transaction.
		 */
		if (!(lidp->lid_flags & XFS_LID_DIRTY)) {
			lidp = xfs_trans_next_item(tp, lidp);
			continue;
		}
		lidp->lid_size = IOP_SIZE(lidp->lid_item);
		nvecs += lidp->lid_size;
		lidp = xfs_trans_next_item(tp, lidp);
	}

	return nvecs;
}

/*
 * Called from the trans_commit code when we notice that
 * the filesystem is in the middle of a forced shutdown.
 */
STATIC void
xfs_trans_uncommit(
	xfs_trans_t	*tp,
	uint		flags)
{
	xfs_log_item_desc_t	*lidp;

	for (lidp = xfs_trans_first_item(tp);
	     lidp != NULL;
	     lidp = xfs_trans_next_item(tp, lidp)) {
		/*
		 * Unpin all but those that aren't dirty.
		 */
		if (lidp->lid_flags & XFS_LID_DIRTY)
			IOP_UNPIN_REMOVE(lidp->lid_item, tp);
	}

	xfs_trans_unreserve_and_mod_sb(tp);
	XFS_TRANS_UNRESERVE_AND_MOD_DQUOTS(tp->t_mountp, tp);

	xfs_trans_free_items(tp, flags);
	xfs_trans_free_busy(tp);
	xfs_trans_free(tp);
}

/*
 * Fill in the vector with pointers to data to be logged
 * by this transaction.  The transaction header takes
 * the first vector, and then each dirty item takes the
 * number of vectors it indicated it needed in xfs_trans_count_vecs().
 *
 * As each item fills in the entries it needs, also pin the item
 * so that it cannot be flushed out until the log write completes.
 */
STATIC void
xfs_trans_fill_vecs(
	xfs_trans_t		*tp,
	xfs_log_iovec_t		*log_vector)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_iovec_t		*vecp;
	uint			nitems;

	/*
	 * Skip over the entry for the transaction header, we'll
	 * fill that in at the end.
	 */
	vecp = log_vector + 1;		/* pointer arithmetic */

	nitems = 0;
	lidp = xfs_trans_first_item(tp);
	ASSERT(lidp != NULL);
	while (lidp != NULL) {
		/*
		 * Skip items which aren't dirty in this transaction.
		 */
		if (!(lidp->lid_flags & XFS_LID_DIRTY)) {
			lidp = xfs_trans_next_item(tp, lidp);
			continue;
		}
		/*
		 * The item may be marked dirty but not log anything.
		 * This can be used to get called when a transaction
		 * is committed.
		 */
		if (lidp->lid_size) {
			nitems++;
		}
		IOP_FORMAT(lidp->lid_item, vecp);
		vecp += lidp->lid_size;		/* pointer arithmetic */
		IOP_PIN(lidp->lid_item);
		lidp = xfs_trans_next_item(tp, lidp);
	}

	/*
	 * Now that we've counted the number of items in this
	 * transaction, fill in the transaction header.
	 */
	tp->t_header.th_magic = XFS_TRANS_HEADER_MAGIC;
	tp->t_header.th_type = tp->t_type;
	tp->t_header.th_num_items = nitems;
	log_vector->i_addr = (xfs_caddr_t)&tp->t_header;
	log_vector->i_len = sizeof(xfs_trans_header_t);
	XLOG_VEC_SET_TYPE(log_vector, XLOG_REG_TYPE_TRANSHDR);
}


/*
 * Unlock all of the transaction's items and free the transaction.
 * The transaction must not have modified any of its items, because
 * there is no way to restore them to their previous state.
 *
 * If the transaction has made a log reservation, make sure to release
 * it as well.
 */
void
xfs_trans_cancel(
	xfs_trans_t		*tp,
	int			flags)
{
	int			log_flags;
#ifdef DEBUG
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_t		*lip;
	int			i;
#endif
	xfs_mount_t		*mp = tp->t_mountp;

	/*
	 * See if the caller is being too lazy to figure out if
	 * the transaction really needs an abort.
	 */
	if ((flags & XFS_TRANS_ABORT) && !(tp->t_flags & XFS_TRANS_DIRTY))
		flags &= ~XFS_TRANS_ABORT;
	/*
	 * See if the caller is relying on us to shut down the
	 * filesystem.  This happens in paths where we detect
	 * corruption and decide to give up.
	 */
	if ((tp->t_flags & XFS_TRANS_DIRTY) && !XFS_FORCED_SHUTDOWN(mp)) {
		XFS_ERROR_REPORT("xfs_trans_cancel", XFS_ERRLEVEL_LOW, mp);
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
	}
#ifdef DEBUG
	if (!(flags & XFS_TRANS_ABORT)) {
		licp = &(tp->t_items);
		while (licp != NULL) {
			lidp = licp->lic_descs;
			for (i = 0; i < licp->lic_unused; i++, lidp++) {
				if (XFS_LIC_ISFREE(licp, i)) {
					continue;
				}

				lip = lidp->lid_item;
				if (!XFS_FORCED_SHUTDOWN(mp))
					ASSERT(!(lip->li_type == XFS_LI_EFD));
			}
			licp = licp->lic_next;
		}
	}
#endif
	xfs_trans_unreserve_and_mod_sb(tp);
	XFS_TRANS_UNRESERVE_AND_MOD_DQUOTS(mp, tp);

	if (tp->t_ticket) {
		if (flags & XFS_TRANS_RELEASE_LOG_RES) {
			ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
			log_flags = XFS_LOG_REL_PERM_RESERV;
		} else {
			log_flags = 0;
		}
		xfs_log_done(mp, tp->t_ticket, NULL, log_flags);
	}

	/* mark this thread as no longer being in a transaction */
	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);

	xfs_trans_free_items(tp, flags);
	xfs_trans_free_busy(tp);
	xfs_trans_free(tp);
}


/*
 * Free the transaction structure.  If there is more clean up
 * to do when the structure is freed, add it here.
 */
STATIC void
xfs_trans_free(
	xfs_trans_t	*tp)
{
	atomic_dec(&tp->t_mountp->m_active_trans);
	XFS_TRANS_FREE_DQINFO(tp->t_mountp, tp);
	kmem_zone_free(xfs_trans_zone, tp);
}


/*
 * THIS SHOULD BE REWRITTEN TO USE xfs_trans_next_item().
 *
 * This is typically called by the LM when a transaction has been fully
 * committed to disk.  It needs to unpin the items which have
 * been logged by the transaction and update their positions
 * in the AIL if necessary.
 * This also gets called when the transactions didn't get written out
 * because of an I/O error. Abortflag & XFS_LI_ABORTED is set then.
 *
 * Call xfs_trans_chunk_committed() to process the items in
 * each chunk.
 */
STATIC void
xfs_trans_committed(
	xfs_trans_t	*tp,
	int		abortflag)
{
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_chunk_t	*next_licp;
	xfs_log_busy_chunk_t	*lbcp;
	xfs_log_busy_slot_t	*lbsp;
	int			i;

	/*
	 * Call the transaction's completion callback if there
	 * is one.
	 */
	if (tp->t_callback != NULL) {
		tp->t_callback(tp, tp->t_callarg);
	}

	/*
	 * Special case the chunk embedded in the transaction.
	 */
	licp = &(tp->t_items);
	if (!(XFS_LIC_ARE_ALL_FREE(licp))) {
		xfs_trans_chunk_committed(licp, tp->t_lsn, abortflag);
	}

	/*
	 * Process the items in each chunk in turn.
	 */
	licp = licp->lic_next;
	while (licp != NULL) {
		ASSERT(!XFS_LIC_ARE_ALL_FREE(licp));
		xfs_trans_chunk_committed(licp, tp->t_lsn, abortflag);
		next_licp = licp->lic_next;
		kmem_free(licp, sizeof(xfs_log_item_chunk_t));
		licp = next_licp;
	}

	/*
	 * Clear all the per-AG busy list items listed in this transaction
	 */
	lbcp = &tp->t_busy;
	while (lbcp != NULL) {
		for (i = 0, lbsp = lbcp->lbc_busy; i < lbcp->lbc_unused; i++, lbsp++) {
			if (!XFS_LBC_ISFREE(lbcp, i)) {
				xfs_alloc_clear_busy(tp, lbsp->lbc_ag,
						     lbsp->lbc_idx);
			}
		}
		lbcp = lbcp->lbc_next;
	}
	xfs_trans_free_busy(tp);

	/*
	 * That's it for the transaction structure.  Free it.
	 */
	xfs_trans_free(tp);
}

/*
 * This is called to perform the commit processing for each
 * item described by the given chunk.
 *
 * The commit processing consists of unlocking items which were
 * held locked with the SYNC_UNLOCK attribute, calling the committed
 * routine of each logged item, updating the item's position in the AIL
 * if necessary, and unpinning each item.  If the committed routine
 * returns -1, then do nothing further with the item because it
 * may have been freed.
 *
 * Since items are unlocked when they are copied to the incore
 * log, it is possible for two transactions to be completing
 * and manipulating the same item simultaneously.  The AIL lock
 * will protect the lsn field of each item.  The value of this
 * field can never go backwards.
 *
 * We unpin the items after repositioning them in the AIL, because
 * otherwise they could be immediately flushed and we'd have to race
 * with the flusher trying to pull the item from the AIL as we add it.
 */
STATIC void
xfs_trans_chunk_committed(
	xfs_log_item_chunk_t	*licp,
	xfs_lsn_t		lsn,
	int			aborted)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_t		*lip;
	xfs_lsn_t		item_lsn;
	struct xfs_mount	*mp;
	int			i;
	SPLDECL(s);

	lidp = licp->lic_descs;
	for (i = 0; i < licp->lic_unused; i++, lidp++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			continue;
		}

		lip = lidp->lid_item;
		if (aborted)
			lip->li_flags |= XFS_LI_ABORTED;

		/*
		 * Send in the ABORTED flag to the COMMITTED routine
		 * so that it knows whether the transaction was aborted
		 * or not.
		 */
		item_lsn = IOP_COMMITTED(lip, lsn);

		/*
		 * If the committed routine returns -1, make
		 * no more references to the item.
		 */
		if (XFS_LSN_CMP(item_lsn, (xfs_lsn_t)-1) == 0) {
			continue;
		}

		/*
		 * If the returned lsn is greater than what it
		 * contained before, update the location of the
		 * item in the AIL.  If it is not, then do nothing.
		 * Items can never move backwards in the AIL.
		 *
		 * While the new lsn should usually be greater, it
		 * is possible that a later transaction completing
		 * simultaneously with an earlier one using the
		 * same item could complete first with a higher lsn.
		 * This would cause the earlier transaction to fail
		 * the test below.
		 */
		mp = lip->li_mountp;
		AIL_LOCK(mp,s);
		if (XFS_LSN_CMP(item_lsn, lip->li_lsn) > 0) {
			/*
			 * This will set the item's lsn to item_lsn
			 * and update the position of the item in
			 * the AIL.
			 *
			 * xfs_trans_update_ail() drops the AIL lock.
			 */
			xfs_trans_update_ail(mp, lip, item_lsn, s);
		} else {
			AIL_UNLOCK(mp, s);
		}

		/*
		 * Now that we've repositioned the item in the AIL,
		 * unpin it so it can be flushed. Pass information
		 * about buffer stale state down from the log item
		 * flags, if anyone else stales the buffer we do not
		 * want to pay any attention to it.
		 */
		IOP_UNPIN(lip, lidp->lid_flags & XFS_LID_BUF_STALE);
	}
}
