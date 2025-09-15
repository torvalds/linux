// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_log.h"
#include "xfs_trans_priv.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2_priv.h"
#include "xfs_dir2.h"
#include "xfs_attr.h"
#include "xfs_reflink.h"
#include "xfs_ag.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_exchmaps.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_bmap_util.h"
#include "xfs_rtrefcount_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/health.h"
#include "scrub/tempfile.h"

/* Common code for the metadata scrubbers. */

/*
 * Handling operational errors.
 *
 * The *_process_error() family of functions are used to process error return
 * codes from functions called as part of a scrub operation.
 *
 * If there's no error, we return true to tell the caller that it's ok
 * to move on to the next check in its list.
 *
 * For non-verifier errors (e.g. ENOMEM) we return false to tell the
 * caller that something bad happened, and we preserve *error so that
 * the caller can return the *error up the stack to userspace.
 *
 * Verifier errors (EFSBADCRC/EFSCORRUPTED) are recorded by setting
 * OFLAG_CORRUPT in sm_flags and the *error is cleared.  In other words,
 * we track verifier errors (and failed scrub checks) via OFLAG_CORRUPT,
 * not via return codes.  We return false to tell the caller that
 * something bad happened.  Since the error has been cleared, the caller
 * will (presumably) return that zero and scrubbing will move on to
 * whatever's next.
 *
 * ftrace can be used to record the precise metadata location and the
 * approximate code location of the failed operation.
 */

/* Check for operational errors. */
static bool
__xchk_process_error(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	int			*error,
	__u32			errflag,
	void			*ret_ip)
{
	switch (*error) {
	case 0:
		return true;
	case -EDEADLOCK:
	case -ECHRNG:
		/* Used to restart an op with deadlock avoidance. */
		trace_xchk_deadlock_retry(
				sc->ip ? sc->ip : XFS_I(file_inode(sc->file)),
				sc->sm, *error);
		break;
	case -ECANCELED:
		/*
		 * ECANCELED here means that the caller set one of the scrub
		 * outcome flags (corrupt, xfail, xcorrupt) and wants to exit
		 * quickly.  Set error to zero and do not continue.
		 */
		trace_xchk_op_error(sc, agno, bno, *error, ret_ip);
		*error = 0;
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Note the badness but don't abort. */
		sc->sm->sm_flags |= errflag;
		*error = 0;
		fallthrough;
	default:
		trace_xchk_op_error(sc, agno, bno, *error, ret_ip);
		break;
	}
	return false;
}

bool
xchk_process_error(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	int			*error)
{
	return __xchk_process_error(sc, agno, bno, error,
			XFS_SCRUB_OFLAG_CORRUPT, __return_address);
}

bool
xchk_process_rt_error(
	struct xfs_scrub	*sc,
	xfs_rgnumber_t		rgno,
	xfs_rgblock_t		rgbno,
	int			*error)
{
	return __xchk_process_error(sc, rgno, rgbno, error,
			XFS_SCRUB_OFLAG_CORRUPT, __return_address);
}

bool
xchk_xref_process_error(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	int			*error)
{
	return __xchk_process_error(sc, agno, bno, error,
			XFS_SCRUB_OFLAG_XFAIL, __return_address);
}

/* Check for operational errors for a file offset. */
static bool
__xchk_fblock_process_error(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_fileoff_t		offset,
	int			*error,
	__u32			errflag,
	void			*ret_ip)
{
	switch (*error) {
	case 0:
		return true;
	case -EDEADLOCK:
	case -ECHRNG:
		/* Used to restart an op with deadlock avoidance. */
		trace_xchk_deadlock_retry(sc->ip, sc->sm, *error);
		break;
	case -ECANCELED:
		/*
		 * ECANCELED here means that the caller set one of the scrub
		 * outcome flags (corrupt, xfail, xcorrupt) and wants to exit
		 * quickly.  Set error to zero and do not continue.
		 */
		trace_xchk_file_op_error(sc, whichfork, offset, *error,
				ret_ip);
		*error = 0;
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Note the badness but don't abort. */
		sc->sm->sm_flags |= errflag;
		*error = 0;
		fallthrough;
	default:
		trace_xchk_file_op_error(sc, whichfork, offset, *error,
				ret_ip);
		break;
	}
	return false;
}

bool
xchk_fblock_process_error(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_fileoff_t		offset,
	int			*error)
{
	return __xchk_fblock_process_error(sc, whichfork, offset, error,
			XFS_SCRUB_OFLAG_CORRUPT, __return_address);
}

bool
xchk_fblock_xref_process_error(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_fileoff_t		offset,
	int			*error)
{
	return __xchk_fblock_process_error(sc, whichfork, offset, error,
			XFS_SCRUB_OFLAG_XFAIL, __return_address);
}

/*
 * Handling scrub corruption/optimization/warning checks.
 *
 * The *_set_{corrupt,preen,warning}() family of functions are used to
 * record the presence of metadata that is incorrect (corrupt), could be
 * optimized somehow (preen), or should be flagged for administrative
 * review but is not incorrect (warn).
 *
 * ftrace can be used to record the precise metadata location and
 * approximate code location of the failed check.
 */

/* Record a block which could be optimized. */
void
xchk_block_set_preen(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_PREEN;
	trace_xchk_block_preen(sc, xfs_buf_daddr(bp), __return_address);
}

/*
 * Record an inode which could be optimized.  The trace data will
 * include the block given by bp if bp is given; otherwise it will use
 * the block location of the inode record itself.
 */
void
xchk_ino_set_preen(
	struct xfs_scrub	*sc,
	xfs_ino_t		ino)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_PREEN;
	trace_xchk_ino_preen(sc, ino, __return_address);
}

/* Record something being wrong with the filesystem primary superblock. */
void
xchk_set_corrupt(
	struct xfs_scrub	*sc)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_fs_error(sc, 0, __return_address);
}

/* Record a corrupt block. */
void
xchk_block_set_corrupt(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_block_error(sc, xfs_buf_daddr(bp), __return_address);
}

#ifdef CONFIG_XFS_QUOTA
/* Record a corrupt quota counter. */
void
xchk_qcheck_set_corrupt(
	struct xfs_scrub	*sc,
	unsigned int		dqtype,
	xfs_dqid_t		id)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_qcheck_error(sc, dqtype, id, __return_address);
}
#endif

/* Record a corruption while cross-referencing. */
void
xchk_block_xref_set_corrupt(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XCORRUPT;
	trace_xchk_block_error(sc, xfs_buf_daddr(bp), __return_address);
}

/*
 * Record a corrupt inode.  The trace data will include the block given
 * by bp if bp is given; otherwise it will use the block location of the
 * inode record itself.
 */
void
xchk_ino_set_corrupt(
	struct xfs_scrub	*sc,
	xfs_ino_t		ino)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_ino_error(sc, ino, __return_address);
}

/* Record a corruption while cross-referencing with an inode. */
void
xchk_ino_xref_set_corrupt(
	struct xfs_scrub	*sc,
	xfs_ino_t		ino)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XCORRUPT;
	trace_xchk_ino_error(sc, ino, __return_address);
}

/* Record corruption in a block indexed by a file fork. */
void
xchk_fblock_set_corrupt(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_fileoff_t		offset)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_fblock_error(sc, whichfork, offset, __return_address);
}

/* Record a corruption while cross-referencing a fork block. */
void
xchk_fblock_xref_set_corrupt(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_fileoff_t		offset)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XCORRUPT;
	trace_xchk_fblock_error(sc, whichfork, offset, __return_address);
}

/*
 * Warn about inodes that need administrative review but is not
 * incorrect.
 */
void
xchk_ino_set_warning(
	struct xfs_scrub	*sc,
	xfs_ino_t		ino)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_WARNING;
	trace_xchk_ino_warning(sc, ino, __return_address);
}

/* Warn about a block indexed by a file fork that needs review. */
void
xchk_fblock_set_warning(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_fileoff_t		offset)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_WARNING;
	trace_xchk_fblock_warning(sc, whichfork, offset, __return_address);
}

/* Signal an incomplete scrub. */
void
xchk_set_incomplete(
	struct xfs_scrub	*sc)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_INCOMPLETE;
	trace_xchk_incomplete(sc, __return_address);
}

/*
 * rmap scrubbing -- compute the number of blocks with a given owner,
 * at least according to the reverse mapping data.
 */

struct xchk_rmap_ownedby_info {
	const struct xfs_owner_info	*oinfo;
	xfs_filblks_t			*blocks;
};

STATIC int
xchk_count_rmap_ownedby_irec(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xchk_rmap_ownedby_info	*sroi = priv;
	bool				irec_attr;
	bool				oinfo_attr;

	irec_attr = rec->rm_flags & XFS_RMAP_ATTR_FORK;
	oinfo_attr = sroi->oinfo->oi_flags & XFS_OWNER_INFO_ATTR_FORK;

	if (rec->rm_owner != sroi->oinfo->oi_owner)
		return 0;

	if (XFS_RMAP_NON_INODE_OWNER(rec->rm_owner) || irec_attr == oinfo_attr)
		(*sroi->blocks) += rec->rm_blockcount;

	return 0;
}

/*
 * Calculate the number of blocks the rmap thinks are owned by something.
 * The caller should pass us an rmapbt cursor.
 */
int
xchk_count_rmap_ownedby_ag(
	struct xfs_scrub		*sc,
	struct xfs_btree_cur		*cur,
	const struct xfs_owner_info	*oinfo,
	xfs_filblks_t			*blocks)
{
	struct xchk_rmap_ownedby_info	sroi = {
		.oinfo			= oinfo,
		.blocks			= blocks,
	};

	*blocks = 0;
	return xfs_rmap_query_all(cur, xchk_count_rmap_ownedby_irec,
			&sroi);
}

/*
 * AG scrubbing
 *
 * These helpers facilitate locking an allocation group's header
 * buffers, setting up cursors for all btrees that are present, and
 * cleaning everything up once we're through.
 */

/* Decide if we want to return an AG header read failure. */
static inline bool
want_ag_read_header_failure(
	struct xfs_scrub	*sc,
	unsigned int		type)
{
	/* Return all AG header read failures when scanning btrees. */
	if (sc->sm->sm_type != XFS_SCRUB_TYPE_AGF &&
	    sc->sm->sm_type != XFS_SCRUB_TYPE_AGFL &&
	    sc->sm->sm_type != XFS_SCRUB_TYPE_AGI)
		return true;
	/*
	 * If we're scanning a given type of AG header, we only want to
	 * see read failures from that specific header.  We'd like the
	 * other headers to cross-check them, but this isn't required.
	 */
	if (sc->sm->sm_type == type)
		return true;
	return false;
}

/*
 * Grab the AG header buffers for the attached perag structure.
 *
 * The headers should be released by xchk_ag_free, but as a fail safe we attach
 * all the buffers we grab to the scrub transaction so they'll all be freed
 * when we cancel it.
 */
static inline int
xchk_perag_read_headers(
	struct xfs_scrub	*sc,
	struct xchk_ag		*sa)
{
	int			error;

	error = xfs_ialloc_read_agi(sa->pag, sc->tp, 0, &sa->agi_bp);
	if (error && want_ag_read_header_failure(sc, XFS_SCRUB_TYPE_AGI))
		return error;

	error = xfs_alloc_read_agf(sa->pag, sc->tp, 0, &sa->agf_bp);
	if (error && want_ag_read_header_failure(sc, XFS_SCRUB_TYPE_AGF))
		return error;

	return 0;
}

/*
 * Grab the AG headers for the attached perag structure and wait for pending
 * intents to drain.
 */
int
xchk_perag_drain_and_lock(
	struct xfs_scrub	*sc)
{
	struct xchk_ag		*sa = &sc->sa;
	int			error = 0;

	ASSERT(sa->pag != NULL);
	ASSERT(sa->agi_bp == NULL);
	ASSERT(sa->agf_bp == NULL);

	do {
		if (xchk_should_terminate(sc, &error))
			return error;

		error = xchk_perag_read_headers(sc, sa);
		if (error)
			return error;

		/*
		 * If we've grabbed an inode for scrubbing then we assume that
		 * holding its ILOCK will suffice to coordinate with any intent
		 * chains involving this inode.
		 */
		if (sc->ip)
			return 0;

		/*
		 * Decide if this AG is quiet enough for all metadata to be
		 * consistent with each other.  XFS allows the AG header buffer
		 * locks to cycle across transaction rolls while processing
		 * chains of deferred ops, which means that there could be
		 * other threads in the middle of processing a chain of
		 * deferred ops.  For regular operations we are careful about
		 * ordering operations to prevent collisions between threads
		 * (which is why we don't need a per-AG lock), but scrub and
		 * repair have to serialize against chained operations.
		 *
		 * We just locked all the AG headers buffers; now take a look
		 * to see if there are any intents in progress.  If there are,
		 * drop the AG headers and wait for the intents to drain.
		 * Since we hold all the AG header locks for the duration of
		 * the scrub, this is the only time we have to sample the
		 * intents counter; any threads increasing it after this point
		 * can't possibly be in the middle of a chain of AG metadata
		 * updates.
		 *
		 * Obviously, this should be slanted against scrub and in favor
		 * of runtime threads.
		 */
		if (!xfs_group_intent_busy(pag_group(sa->pag)))
			return 0;

		if (sa->agf_bp) {
			xfs_trans_brelse(sc->tp, sa->agf_bp);
			sa->agf_bp = NULL;
		}

		if (sa->agi_bp) {
			xfs_trans_brelse(sc->tp, sa->agi_bp);
			sa->agi_bp = NULL;
		}

		if (!(sc->flags & XCHK_FSGATES_DRAIN))
			return -ECHRNG;
		error = xfs_group_intent_drain(pag_group(sa->pag));
		if (error == -ERESTARTSYS)
			error = -EINTR;
	} while (!error);

	return error;
}

/*
 * Grab the per-AG structure, grab all AG header buffers, and wait until there
 * aren't any pending intents.  Returns -ENOENT if we can't grab the perag
 * structure.
 */
int
xchk_ag_read_headers(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	struct xchk_ag		*sa)
{
	struct xfs_mount	*mp = sc->mp;

	ASSERT(!sa->pag);
	sa->pag = xfs_perag_get(mp, agno);
	if (!sa->pag)
		return -ENOENT;

	return xchk_perag_drain_and_lock(sc);
}

/* Release all the AG btree cursors. */
void
xchk_ag_btcur_free(
	struct xchk_ag		*sa)
{
	if (sa->refc_cur)
		xfs_btree_del_cursor(sa->refc_cur, XFS_BTREE_ERROR);
	if (sa->rmap_cur)
		xfs_btree_del_cursor(sa->rmap_cur, XFS_BTREE_ERROR);
	if (sa->fino_cur)
		xfs_btree_del_cursor(sa->fino_cur, XFS_BTREE_ERROR);
	if (sa->ino_cur)
		xfs_btree_del_cursor(sa->ino_cur, XFS_BTREE_ERROR);
	if (sa->cnt_cur)
		xfs_btree_del_cursor(sa->cnt_cur, XFS_BTREE_ERROR);
	if (sa->bno_cur)
		xfs_btree_del_cursor(sa->bno_cur, XFS_BTREE_ERROR);

	sa->refc_cur = NULL;
	sa->rmap_cur = NULL;
	sa->fino_cur = NULL;
	sa->ino_cur = NULL;
	sa->bno_cur = NULL;
	sa->cnt_cur = NULL;
}

/* Initialize all the btree cursors for an AG. */
void
xchk_ag_btcur_init(
	struct xfs_scrub	*sc,
	struct xchk_ag		*sa)
{
	struct xfs_mount	*mp = sc->mp;

	if (sa->agf_bp) {
		/* Set up a bnobt cursor for cross-referencing. */
		sa->bno_cur = xfs_bnobt_init_cursor(mp, sc->tp, sa->agf_bp,
				sa->pag);
		xchk_ag_btree_del_cursor_if_sick(sc, &sa->bno_cur,
				XFS_SCRUB_TYPE_BNOBT);

		/* Set up a cntbt cursor for cross-referencing. */
		sa->cnt_cur = xfs_cntbt_init_cursor(mp, sc->tp, sa->agf_bp,
				sa->pag);
		xchk_ag_btree_del_cursor_if_sick(sc, &sa->cnt_cur,
				XFS_SCRUB_TYPE_CNTBT);

		/* Set up a rmapbt cursor for cross-referencing. */
		if (xfs_has_rmapbt(mp)) {
			sa->rmap_cur = xfs_rmapbt_init_cursor(mp, sc->tp,
					sa->agf_bp, sa->pag);
			xchk_ag_btree_del_cursor_if_sick(sc, &sa->rmap_cur,
					XFS_SCRUB_TYPE_RMAPBT);
		}

		/* Set up a refcountbt cursor for cross-referencing. */
		if (xfs_has_reflink(mp)) {
			sa->refc_cur = xfs_refcountbt_init_cursor(mp, sc->tp,
					sa->agf_bp, sa->pag);
			xchk_ag_btree_del_cursor_if_sick(sc, &sa->refc_cur,
					XFS_SCRUB_TYPE_REFCNTBT);
		}
	}

	if (sa->agi_bp) {
		/* Set up a inobt cursor for cross-referencing. */
		sa->ino_cur = xfs_inobt_init_cursor(sa->pag, sc->tp,
				sa->agi_bp);
		xchk_ag_btree_del_cursor_if_sick(sc, &sa->ino_cur,
				XFS_SCRUB_TYPE_INOBT);

		/* Set up a finobt cursor for cross-referencing. */
		if (xfs_has_finobt(mp)) {
			sa->fino_cur = xfs_finobt_init_cursor(sa->pag, sc->tp,
					sa->agi_bp);
			xchk_ag_btree_del_cursor_if_sick(sc, &sa->fino_cur,
					XFS_SCRUB_TYPE_FINOBT);
		}
	}
}

/* Release the AG header context and btree cursors. */
void
xchk_ag_free(
	struct xfs_scrub	*sc,
	struct xchk_ag		*sa)
{
	xchk_ag_btcur_free(sa);
	xrep_reset_perag_resv(sc);
	if (sa->agf_bp) {
		xfs_trans_brelse(sc->tp, sa->agf_bp);
		sa->agf_bp = NULL;
	}
	if (sa->agi_bp) {
		xfs_trans_brelse(sc->tp, sa->agi_bp);
		sa->agi_bp = NULL;
	}
	if (sa->pag) {
		xfs_perag_put(sa->pag);
		sa->pag = NULL;
	}
}

/*
 * For scrub, grab the perag structure, the AGI, and the AGF headers, in that
 * order.  Locking order requires us to get the AGI before the AGF.  We use the
 * transaction to avoid deadlocking on crosslinked metadata buffers; either the
 * caller passes one in (bmap scrub) or we have to create a transaction
 * ourselves.  Returns ENOENT if the perag struct cannot be grabbed.
 */
int
xchk_ag_init(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	struct xchk_ag		*sa)
{
	int			error;

	error = xchk_ag_read_headers(sc, agno, sa);
	if (error)
		return error;

	xchk_ag_btcur_init(sc, sa);
	return 0;
}

#ifdef CONFIG_XFS_RT
/*
 * For scrubbing a realtime group, grab all the in-core resources we'll need to
 * check the metadata, which means taking the ILOCK of the realtime group's
 * metadata inodes.  Callers must not join these inodes to the transaction with
 * non-zero lockflags or concurrency problems will result.  The @rtglock_flags
 * argument takes XFS_RTGLOCK_* flags.
 */
int
xchk_rtgroup_init(
	struct xfs_scrub	*sc,
	xfs_rgnumber_t		rgno,
	struct xchk_rt		*sr)
{
	ASSERT(sr->rtg == NULL);
	ASSERT(sr->rtlock_flags == 0);

	sr->rtg = xfs_rtgroup_get(sc->mp, rgno);
	if (!sr->rtg)
		return -ENOENT;
	return 0;
}

/* Lock all the rt group metadata inode ILOCKs and wait for intents. */
int
xchk_rtgroup_lock(
	struct xfs_scrub	*sc,
	struct xchk_rt		*sr,
	unsigned int		rtglock_flags)
{
	int			error = 0;

	ASSERT(sr->rtg != NULL);

	/*
	 * If we're /only/ locking the rtbitmap in shared mode, then we're
	 * obviously not trying to compare records in two metadata inodes.
	 * There's no need to drain intents here because the caller (most
	 * likely the rgsuper scanner) doesn't need that level of consistency.
	 */
	if (rtglock_flags == XFS_RTGLOCK_BITMAP_SHARED) {
		xfs_rtgroup_lock(sr->rtg, rtglock_flags);
		sr->rtlock_flags = rtglock_flags;
		return 0;
	}

	do {
		if (xchk_should_terminate(sc, &error))
			return error;

		xfs_rtgroup_lock(sr->rtg, rtglock_flags);

		/*
		 * If we've grabbed a non-metadata file for scrubbing, we
		 * assume that holding its ILOCK will suffice to coordinate
		 * with any rt intent chains involving this inode.
		 */
		if (sc->ip && !xfs_is_internal_inode(sc->ip))
			break;

		/*
		 * Decide if the rt group is quiet enough for all metadata to
		 * be consistent with each other.  Regular file IO doesn't get
		 * to lock all the rt inodes at the same time, which means that
		 * there could be other threads in the middle of processing a
		 * chain of deferred ops.
		 *
		 * We just locked all the metadata inodes for this rt group;
		 * now take a look to see if there are any intents in progress.
		 * If there are, drop the rt group inode locks and wait for the
		 * intents to drain.  Since we hold the rt group inode locks
		 * for the duration of the scrub, this is the only time we have
		 * to sample the intents counter; any threads increasing it
		 * after this point can't possibly be in the middle of a chain
		 * of rt metadata updates.
		 *
		 * Obviously, this should be slanted against scrub and in favor
		 * of runtime threads.
		 */
		if (!xfs_group_intent_busy(rtg_group(sr->rtg)))
			break;

		xfs_rtgroup_unlock(sr->rtg, rtglock_flags);

		if (!(sc->flags & XCHK_FSGATES_DRAIN))
			return -ECHRNG;
		error = xfs_group_intent_drain(rtg_group(sr->rtg));
		if (error) {
			if (error == -ERESTARTSYS)
				error = -EINTR;
			return error;
		}
	} while (1);

	sr->rtlock_flags = rtglock_flags;

	if (xfs_has_rtrmapbt(sc->mp) && (rtglock_flags & XFS_RTGLOCK_RMAP))
		sr->rmap_cur = xfs_rtrmapbt_init_cursor(sc->tp, sr->rtg);

	if (xfs_has_rtreflink(sc->mp) && (rtglock_flags & XFS_RTGLOCK_REFCOUNT))
		sr->refc_cur = xfs_rtrefcountbt_init_cursor(sc->tp, sr->rtg);

	return 0;
}

/*
 * Free all the btree cursors and other incore data relating to the realtime
 * group.  This has to be done /before/ committing (or cancelling) the scrub
 * transaction.
 */
void
xchk_rtgroup_btcur_free(
	struct xchk_rt		*sr)
{
	if (sr->rmap_cur)
		xfs_btree_del_cursor(sr->rmap_cur, XFS_BTREE_ERROR);
	if (sr->refc_cur)
		xfs_btree_del_cursor(sr->refc_cur, XFS_BTREE_ERROR);

	sr->refc_cur = NULL;
	sr->rmap_cur = NULL;
}

/*
 * Unlock the realtime group.  This must be done /after/ committing (or
 * cancelling) the scrub transaction.
 */
void
xchk_rtgroup_unlock(
	struct xchk_rt		*sr)
{
	ASSERT(sr->rtg != NULL);

	if (sr->rtlock_flags) {
		xfs_rtgroup_unlock(sr->rtg, sr->rtlock_flags);
		sr->rtlock_flags = 0;
	}
}

/*
 * Unlock the realtime group and release its resources.  This must be done
 * /after/ committing (or cancelling) the scrub transaction.
 */
void
xchk_rtgroup_free(
	struct xfs_scrub	*sc,
	struct xchk_rt		*sr)
{
	ASSERT(sr->rtg != NULL);

	xchk_rtgroup_unlock(sr);

	xfs_rtgroup_put(sr->rtg);
	sr->rtg = NULL;
}
#endif /* CONFIG_XFS_RT */

/* Per-scrubber setup functions */

void
xchk_trans_cancel(
	struct xfs_scrub	*sc)
{
	xfs_trans_cancel(sc->tp);
	sc->tp = NULL;
}

void
xchk_trans_alloc_empty(
	struct xfs_scrub	*sc)
{
	sc->tp = xfs_trans_alloc_empty(sc->mp);
}

/*
 * Grab an empty transaction so that we can re-grab locked buffers if
 * one of our btrees turns out to be cyclic.
 *
 * If we're going to repair something, we need to ask for the largest possible
 * log reservation so that we can handle the worst case scenario for metadata
 * updates while rebuilding a metadata item.  We also need to reserve as many
 * blocks in the head transaction as we think we're going to need to rebuild
 * the metadata object.
 */
int
xchk_trans_alloc(
	struct xfs_scrub	*sc,
	uint			resblks)
{
	if (sc->sm->sm_flags & XFS_SCRUB_IFLAG_REPAIR)
		return xfs_trans_alloc(sc->mp, &M_RES(sc->mp)->tr_itruncate,
				resblks, 0, 0, &sc->tp);

	xchk_trans_alloc_empty(sc);
	return 0;
}

/* Set us up with a transaction and an empty context. */
int
xchk_setup_fs(
	struct xfs_scrub	*sc)
{
	uint			resblks;

	resblks = xrep_calc_ag_resblks(sc);
	return xchk_trans_alloc(sc, resblks);
}

/* Set us up with a transaction and an empty context to repair rt metadata. */
int
xchk_setup_rt(
	struct xfs_scrub	*sc)
{
	return xchk_trans_alloc(sc, xrep_calc_rtgroup_resblks(sc));
}

/* Set us up with AG headers and btree cursors. */
int
xchk_setup_ag_btree(
	struct xfs_scrub	*sc,
	bool			force_log)
{
	struct xfs_mount	*mp = sc->mp;
	int			error;

	/*
	 * If the caller asks us to checkpont the log, do so.  This
	 * expensive operation should be performed infrequently and only
	 * as a last resort.  Any caller that sets force_log should
	 * document why they need to do so.
	 */
	if (force_log) {
		error = xchk_checkpoint_log(mp);
		if (error)
			return error;
	}

	error = xchk_setup_fs(sc);
	if (error)
		return error;

	return xchk_ag_init(sc, sc->sm->sm_agno, &sc->sa);
}

/* Push everything out of the log onto disk. */
int
xchk_checkpoint_log(
	struct xfs_mount	*mp)
{
	int			error;

	error = xfs_log_force(mp, XFS_LOG_SYNC);
	if (error)
		return error;
	xfs_ail_push_all_sync(mp->m_ail);
	return 0;
}

/* Verify that an inode is allocated ondisk, then return its cached inode. */
int
xchk_iget(
	struct xfs_scrub	*sc,
	xfs_ino_t		inum,
	struct xfs_inode	**ipp)
{
	ASSERT(sc->tp != NULL);

	return xfs_iget(sc->mp, sc->tp, inum, XCHK_IGET_FLAGS, 0, ipp);
}

/*
 * Try to grab an inode in a manner that avoids races with physical inode
 * allocation.  If we can't, return the locked AGI buffer so that the caller
 * can single-step the loading process to see where things went wrong.
 * Callers must have a valid scrub transaction.
 *
 * If the iget succeeds, return 0, a NULL AGI, and the inode.
 *
 * If the iget fails, return the error, the locked AGI, and a NULL inode.  This
 * can include -EINVAL and -ENOENT for invalid inode numbers or inodes that are
 * no longer allocated; or any other corruption or runtime error.
 *
 * If the AGI read fails, return the error, a NULL AGI, and NULL inode.
 *
 * If a fatal signal is pending, return -EINTR, a NULL AGI, and a NULL inode.
 */
int
xchk_iget_agi(
	struct xfs_scrub	*sc,
	xfs_ino_t		inum,
	struct xfs_buf		**agi_bpp,
	struct xfs_inode	**ipp)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_trans	*tp = sc->tp;
	struct xfs_perag	*pag;
	int			error;

	ASSERT(sc->tp != NULL);

again:
	*agi_bpp = NULL;
	*ipp = NULL;
	error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	/*
	 * Attach the AGI buffer to the scrub transaction to avoid deadlocks
	 * in the iget cache miss path.
	 */
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, inum));
	error = xfs_ialloc_read_agi(pag, tp, 0, agi_bpp);
	xfs_perag_put(pag);
	if (error)
		return error;

	error = xfs_iget(mp, tp, inum, XFS_IGET_NORETRY | XCHK_IGET_FLAGS, 0,
			ipp);
	if (error == -EAGAIN) {
		/*
		 * The inode may be in core but temporarily unavailable and may
		 * require the AGI buffer before it can be returned.  Drop the
		 * AGI buffer and retry the lookup.
		 *
		 * Incore lookup will fail with EAGAIN on a cache hit if the
		 * inode is queued to the inactivation list.  The inactivation
		 * worker may remove the inode from the unlinked list and hence
		 * needs the AGI.
		 *
		 * Hence xchk_iget_agi() needs to drop the AGI lock on EAGAIN
		 * to allow inodegc to make progress and move the inode to
		 * IRECLAIMABLE state where xfs_iget will be able to return it
		 * again if it can lock the inode.
		 */
		xfs_trans_brelse(tp, *agi_bpp);
		delay(1);
		goto again;
	}
	if (error)
		return error;

	/* We got the inode, so we can release the AGI. */
	ASSERT(*ipp != NULL);
	xfs_trans_brelse(tp, *agi_bpp);
	*agi_bpp = NULL;
	return 0;
}

#ifdef CONFIG_XFS_QUOTA
/*
 * Try to attach dquots to this inode if we think we might want to repair it.
 * Callers must not hold any ILOCKs.  If the dquots are broken and cannot be
 * attached, a quotacheck will be scheduled.
 */
int
xchk_ino_dqattach(
	struct xfs_scrub	*sc)
{
	ASSERT(sc->tp != NULL);
	ASSERT(sc->ip != NULL);

	if (!xchk_could_repair(sc))
		return 0;

	return xrep_ino_dqattach(sc);
}
#endif

/* Install an inode that we opened by handle for scrubbing. */
int
xchk_install_handle_inode(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	if (VFS_I(ip)->i_generation != sc->sm->sm_gen) {
		xchk_irele(sc, ip);
		return -ENOENT;
	}

	sc->ip = ip;
	return 0;
}

/*
 * Install an already-referenced inode for scrubbing.  Get our own reference to
 * the inode to make disposal simpler.  The inode must not be in I_FREEING or
 * I_WILL_FREE state!
 */
int
xchk_install_live_inode(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	if (!igrab(VFS_I(ip))) {
		xchk_ino_set_corrupt(sc, ip->i_ino);
		return -EFSCORRUPTED;
	}

	sc->ip = ip;
	return 0;
}

/*
 * In preparation to scrub metadata structures that hang off of an inode,
 * grab either the inode referenced in the scrub control structure or the
 * inode passed in.  If the inumber does not reference an allocated inode
 * record, the function returns ENOENT to end the scrub early.  The inode
 * is not locked.
 */
int
xchk_iget_for_scrubbing(
	struct xfs_scrub	*sc)
{
	struct xfs_imap		imap;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag;
	struct xfs_buf		*agi_bp;
	struct xfs_inode	*ip_in = XFS_I(file_inode(sc->file));
	struct xfs_inode	*ip = NULL;
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, sc->sm->sm_ino);
	int			error;

	ASSERT(sc->tp == NULL);

	/* We want to scan the inode we already had opened. */
	if (sc->sm->sm_ino == 0 || sc->sm->sm_ino == ip_in->i_ino)
		return xchk_install_live_inode(sc, ip_in);

	/*
	 * On pre-metadir filesystems, reject internal metadata files.  For
	 * metadir filesystems, limited scrubbing of any file in the metadata
	 * directory tree by handle is allowed, because that is the only way to
	 * validate the lack of parent pointers in the sb-root metadata inodes.
	 */
	if (!xfs_has_metadir(mp) && xfs_is_sb_inum(mp, sc->sm->sm_ino))
		return -ENOENT;
	/* Reject obviously bad inode numbers. */
	if (!xfs_verify_ino(sc->mp, sc->sm->sm_ino))
		return -ENOENT;

	/* Try a safe untrusted iget. */
	error = xchk_iget_safe(sc, sc->sm->sm_ino, &ip);
	if (!error)
		return xchk_install_handle_inode(sc, ip);
	if (error == -ENOENT)
		return error;
	if (error != -EINVAL)
		goto out_error;

	/*
	 * EINVAL with IGET_UNTRUSTED probably means one of several things:
	 * userspace gave us an inode number that doesn't correspond to fs
	 * space; the inode btree lacks a record for this inode; or there is a
	 * record, and it says this inode is free.
	 *
	 * We want to look up this inode in the inobt to distinguish two
	 * scenarios: (1) the inobt says the inode is free, in which case
	 * there's nothing to do; and (2) the inobt says the inode is
	 * allocated, but loading it failed due to corruption.
	 *
	 * Allocate a transaction and grab the AGI to prevent inobt activity
	 * in this AG.  Retry the iget in case someone allocated a new inode
	 * after the first iget failed.
	 */
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out_error;

	error = xchk_iget_agi(sc, sc->sm->sm_ino, &agi_bp, &ip);
	if (error == 0) {
		/* Actually got the inode, so install it. */
		xchk_trans_cancel(sc);
		return xchk_install_handle_inode(sc, ip);
	}
	if (error == -ENOENT)
		goto out_gone;
	if (error != -EINVAL)
		goto out_cancel;

	/* Ensure that we have protected against inode allocation/freeing. */
	if (agi_bp == NULL) {
		ASSERT(agi_bp != NULL);
		error = -ECANCELED;
		goto out_cancel;
	}

	/*
	 * Untrusted iget failed a second time.  Let's try an inobt lookup.
	 * If the inobt thinks this the inode neither can exist inside the
	 * filesystem nor is allocated, return ENOENT to signal that the check
	 * can be skipped.
	 *
	 * If the lookup returns corruption, we'll mark this inode corrupt and
	 * exit to userspace.  There's little chance of fixing anything until
	 * the inobt is straightened out, but there's nothing we can do here.
	 *
	 * If the lookup encounters any other error, exit to userspace.
	 *
	 * If the lookup succeeds, something else must be very wrong in the fs
	 * such that setting up the incore inode failed in some strange way.
	 * Treat those as corruptions.
	 */
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, sc->sm->sm_ino));
	if (!pag) {
		error = -EFSCORRUPTED;
		goto out_cancel;
	}

	error = xfs_imap(pag, sc->tp, sc->sm->sm_ino, &imap,
			XFS_IGET_UNTRUSTED);
	xfs_perag_put(pag);
	if (error == -EINVAL || error == -ENOENT)
		goto out_gone;
	if (!error)
		error = -EFSCORRUPTED;

out_cancel:
	xchk_trans_cancel(sc);
out_error:
	trace_xchk_op_error(sc, agno, XFS_INO_TO_AGBNO(mp, sc->sm->sm_ino),
			error, __return_address);
	return error;
out_gone:
	/* The file is gone, so there's nothing to check. */
	xchk_trans_cancel(sc);
	return -ENOENT;
}

/* Release an inode, possibly dropping it in the process. */
void
xchk_irele(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	if (sc->tp) {
		/*
		 * If we are in a transaction, we /cannot/ drop the inode
		 * ourselves, because the VFS will trigger writeback, which
		 * can require a transaction.  Clear DONTCACHE to force the
		 * inode to the LRU, where someone else can take care of
		 * dropping it.
		 *
		 * Note that when we grabbed our reference to the inode, it
		 * could have had an active ref and DONTCACHE set if a sysadmin
		 * is trying to coerce a change in file access mode.  icache
		 * hits do not clear DONTCACHE, so we must do it here.
		 */
		spin_lock(&VFS_I(ip)->i_lock);
		VFS_I(ip)->i_state &= ~I_DONTCACHE;
		spin_unlock(&VFS_I(ip)->i_lock);
	}

	xfs_irele(ip);
}

/*
 * Set us up to scrub metadata mapped by a file's fork.  Callers must not use
 * this to operate on user-accessible regular file data because the MMAPLOCK is
 * not taken.
 */
int
xchk_setup_inode_contents(
	struct xfs_scrub	*sc,
	unsigned int		resblks)
{
	int			error;

	error = xchk_iget_for_scrubbing(sc);
	if (error)
		return error;

	error = xrep_tempfile_adjust_directory_tree(sc);
	if (error)
		return error;

	/* Lock the inode so the VFS cannot touch this file. */
	xchk_ilock(sc, XFS_IOLOCK_EXCL);

	error = xchk_trans_alloc(sc, resblks);
	if (error)
		goto out;

	error = xchk_ino_dqattach(sc);
	if (error)
		goto out;

	xchk_ilock(sc, XFS_ILOCK_EXCL);
out:
	/* scrub teardown will unlock and release the inode for us */
	return error;
}

void
xchk_ilock(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	xfs_ilock(sc->ip, ilock_flags);
	sc->ilock_flags |= ilock_flags;
}

bool
xchk_ilock_nowait(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	if (xfs_ilock_nowait(sc->ip, ilock_flags)) {
		sc->ilock_flags |= ilock_flags;
		return true;
	}

	return false;
}

void
xchk_iunlock(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	sc->ilock_flags &= ~ilock_flags;
	xfs_iunlock(sc->ip, ilock_flags);
}

/*
 * Predicate that decides if we need to evaluate the cross-reference check.
 * If there was an error accessing the cross-reference btree, just delete
 * the cursor and skip the check.
 */
bool
xchk_should_check_xref(
	struct xfs_scrub	*sc,
	int			*error,
	struct xfs_btree_cur	**curpp)
{
	/* No point in xref if we already know we're corrupt. */
	if (xchk_skip_xref(sc->sm))
		return false;

	if (*error == 0)
		return true;

	if (curpp) {
		/* If we've already given up on xref, just bail out. */
		if (!*curpp)
			return false;

		/* xref error, delete cursor and bail out. */
		xfs_btree_del_cursor(*curpp, XFS_BTREE_ERROR);
		*curpp = NULL;
	}

	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XFAIL;
	trace_xchk_xref_error(sc, *error, __return_address);

	/*
	 * Errors encountered during cross-referencing with another
	 * data structure should not cause this scrubber to abort.
	 */
	*error = 0;
	return false;
}

/* Run the structure verifiers on in-memory buffers to detect bad memory. */
void
xchk_buffer_recheck(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp)
{
	xfs_failaddr_t		fa;

	if (bp->b_ops == NULL) {
		xchk_block_set_corrupt(sc, bp);
		return;
	}
	if (bp->b_ops->verify_struct == NULL) {
		xchk_set_incomplete(sc);
		return;
	}
	fa = bp->b_ops->verify_struct(bp);
	if (!fa)
		return;
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_block_error(sc, xfs_buf_daddr(bp), fa);
}

static inline int
xchk_metadata_inode_subtype(
	struct xfs_scrub	*sc,
	unsigned int		scrub_type)
{
	struct xfs_scrub_subord	*sub;
	int			error;

	sub = xchk_scrub_create_subord(sc, scrub_type);
	error = sub->sc.ops->scrub(&sub->sc);
	xchk_scrub_free_subord(sub);
	return error;
}

/*
 * Scrub the attr/data forks of a metadata inode.  The metadata inode must be
 * pointed to by sc->ip and the ILOCK must be held.
 */
int
xchk_metadata_inode_forks(
	struct xfs_scrub	*sc)
{
	bool			shared;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* Check the inode record. */
	error = xchk_metadata_inode_subtype(sc, XFS_SCRUB_TYPE_INODE);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	/* Metadata inodes don't live on the rt device. */
	if (sc->ip->i_diflags & XFS_DIFLAG_REALTIME) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		return 0;
	}

	/* They should never participate in reflink. */
	if (xfs_is_reflink_inode(sc->ip)) {
		xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		return 0;
	}

	/* Invoke the data fork scrubber. */
	error = xchk_metadata_inode_subtype(sc, XFS_SCRUB_TYPE_BMBTD);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	/* Look for incorrect shared blocks. */
	if (xfs_has_reflink(sc->mp)) {
		error = xfs_reflink_inode_has_shared_extents(sc->tp, sc->ip,
				&shared);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0,
				&error))
			return error;
		if (shared)
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
	}

	/*
	 * Metadata files can only have extended attributes on metadir
	 * filesystems, either for parent pointers or for actual xattr data.
	 */
	if (xfs_inode_hasattr(sc->ip)) {
		if (!xfs_has_metadir(sc->mp)) {
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
			return 0;
		}

		error = xchk_metadata_inode_subtype(sc, XFS_SCRUB_TYPE_BMBTA);
		if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
			return error;
	}

	return 0;
}

/*
 * Enable filesystem hooks (i.e. runtime code patching) before starting a scrub
 * operation.  Callers must not hold any locks that intersect with the CPU
 * hotplug lock (e.g. writeback locks) because code patching must halt the CPUs
 * to change kernel code.
 */
void
xchk_fsgates_enable(
	struct xfs_scrub	*sc,
	unsigned int		scrub_fsgates)
{
	ASSERT(!(scrub_fsgates & ~XCHK_FSGATES_ALL));
	ASSERT(!(sc->flags & scrub_fsgates));

	trace_xchk_fsgates_enable(sc, scrub_fsgates);

	if (scrub_fsgates & XCHK_FSGATES_DRAIN)
		xfs_defer_drain_wait_enable();

	if (scrub_fsgates & XCHK_FSGATES_QUOTA)
		xfs_dqtrx_hook_enable();

	if (scrub_fsgates & XCHK_FSGATES_DIRENTS)
		xfs_dir_hook_enable();

	if (scrub_fsgates & XCHK_FSGATES_RMAP)
		xfs_rmap_hook_enable();

	sc->flags |= scrub_fsgates;
}

/*
 * Decide if this is this a cached inode that's also allocated.  The caller
 * must hold a reference to an AG and the AGI buffer lock to prevent inodes
 * from being allocated or freed.
 *
 * Look up an inode by number in the given file system.  If the inode number
 * is invalid, return -EINVAL.  If the inode is not in cache, return -ENODATA.
 * If the inode is being reclaimed, return -ENODATA because we know the inode
 * cache cannot be updating the ondisk metadata.
 *
 * Otherwise, the incore inode is the one we want, and it is either live,
 * somewhere in the inactivation machinery, or reclaimable.  The inode is
 * allocated if i_mode is nonzero.  In all three cases, the cached inode will
 * be more up to date than the ondisk inode buffer, so we must use the incore
 * i_mode.
 */
int
xchk_inode_is_allocated(
	struct xfs_scrub	*sc,
	xfs_agino_t		agino,
	bool			*inuse)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag = sc->sa.pag;
	xfs_ino_t		ino;
	struct xfs_inode	*ip;
	int			error;

	/* caller must hold perag reference */
	if (pag == NULL) {
		ASSERT(pag != NULL);
		return -EINVAL;
	}

	/* caller must have AGI buffer */
	if (sc->sa.agi_bp == NULL) {
		ASSERT(sc->sa.agi_bp != NULL);
		return -EINVAL;
	}

	/* reject inode numbers outside existing AGs */
	ino = xfs_agino_to_ino(pag, agino);
	if (!xfs_verify_ino(mp, ino))
		return -EINVAL;

	error = -ENODATA;
	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, agino);
	if (!ip) {
		/* cache miss */
		goto out_rcu;
	}

	/*
	 * If the inode number doesn't match, the incore inode got reused
	 * during an RCU grace period and the radix tree hasn't been updated.
	 * This isn't the inode we want.
	 */
	spin_lock(&ip->i_flags_lock);
	if (ip->i_ino != ino)
		goto out_skip;

	trace_xchk_inode_is_allocated(ip);

	/*
	 * We have an incore inode that matches the inode we want, and the
	 * caller holds the perag structure and the AGI buffer.  Let's check
	 * our assumptions below:
	 */

#ifdef DEBUG
	/*
	 * (1) If the incore inode is live (i.e. referenced from the dcache),
	 * it will not be INEW, nor will it be in the inactivation or reclaim
	 * machinery.  The ondisk inode had better be allocated.  This is the
	 * most trivial case.
	 */
	if (!(ip->i_flags & (XFS_NEED_INACTIVE | XFS_INEW | XFS_IRECLAIMABLE |
			     XFS_INACTIVATING))) {
		/* live inode */
		ASSERT(VFS_I(ip)->i_mode != 0);
	}

	/*
	 * If the incore inode is INEW, there are several possibilities:
	 *
	 * (2) For a file that is being created, note that we allocate the
	 * ondisk inode before allocating, initializing, and adding the incore
	 * inode to the radix tree.
	 *
	 * (3) If the incore inode is being recycled, the inode has to be
	 * allocated because we don't allow freed inodes to be recycled.
	 * Recycling doesn't touch i_mode.
	 */
	if (ip->i_flags & XFS_INEW) {
		/* created on disk already or recycling */
		ASSERT(VFS_I(ip)->i_mode != 0);
	}

	/*
	 * (4) If the inode is queued for inactivation (NEED_INACTIVE) but
	 * inactivation has not started (!INACTIVATING), it is still allocated.
	 */
	if ((ip->i_flags & XFS_NEED_INACTIVE) &&
	    !(ip->i_flags & XFS_INACTIVATING)) {
		/* definitely before difree */
		ASSERT(VFS_I(ip)->i_mode != 0);
	}
#endif

	/*
	 * If the incore inode is undergoing inactivation (INACTIVATING), there
	 * are two possibilities:
	 *
	 * (5) It is before the point where it would get freed ondisk, in which
	 * case i_mode is still nonzero.
	 *
	 * (6) It has already been freed, in which case i_mode is zero.
	 *
	 * We don't take the ILOCK here, but difree and dialloc update the AGI,
	 * and we've taken the AGI buffer lock, which prevents that from
	 * happening.
	 */

	/*
	 * (7) Inodes undergoing inactivation (INACTIVATING) or queued for
	 * reclaim (IRECLAIMABLE) could be allocated or free.  i_mode still
	 * reflects the ondisk state.
	 */

	/*
	 * (8) If the inode is in IFLUSHING, it's safe to query i_mode because
	 * the flush code uses i_mode to format the ondisk inode.
	 */

	/*
	 * (9) If the inode is in IRECLAIM and was reachable via the radix
	 * tree, it still has the same i_mode as it did before it entered
	 * reclaim.  The inode object is still alive because we hold the RCU
	 * read lock.
	 */

	*inuse = VFS_I(ip)->i_mode != 0;
	error = 0;

out_skip:
	spin_unlock(&ip->i_flags_lock);
out_rcu:
	rcu_read_unlock();
	return error;
}

/* Is this inode a root directory for either tree? */
bool
xchk_inode_is_dirtree_root(const struct xfs_inode *ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	return ip == mp->m_rootip ||
		(xfs_has_metadir(mp) && ip == mp->m_metadirip);
}

/* Does the superblock point down to this inode? */
bool
xchk_inode_is_sb_rooted(const struct xfs_inode *ip)
{
	return xchk_inode_is_dirtree_root(ip) ||
	       xfs_is_sb_inum(ip->i_mount, ip->i_ino);
}

/* What is the root directory inumber for this inode? */
xfs_ino_t
xchk_inode_rootdir_inum(const struct xfs_inode *ip)
{
	struct xfs_mount	*mp = ip->i_mount;

	if (xfs_is_metadir_inode(ip))
		return mp->m_metadirip->i_ino;
	return mp->m_rootip->i_ino;
}

static int
xchk_meta_btree_count_blocks(
	struct xfs_scrub	*sc,
	xfs_extnum_t		*nextents,
	xfs_filblks_t		*count)
{
	struct xfs_btree_cur	*cur;
	int			error;

	if (!sc->sr.rtg) {
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	switch (sc->ip->i_metatype) {
	case XFS_METAFILE_RTRMAP:
		cur = xfs_rtrmapbt_init_cursor(sc->tp, sc->sr.rtg);
		break;
	case XFS_METAFILE_RTREFCOUNT:
		cur = xfs_rtrefcountbt_init_cursor(sc->tp, sc->sr.rtg);
		break;
	default:
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	error = xfs_btree_count_blocks(cur, count);
	xfs_btree_del_cursor(cur, error);
	if (!error) {
		*nextents = 0;
		(*count)--;	/* don't count the btree iroot */
	}
	return error;
}

/* Count the blocks used by a file, even if it's a metadata inode. */
int
xchk_inode_count_blocks(
	struct xfs_scrub	*sc,
	int			whichfork,
	xfs_extnum_t		*nextents,
	xfs_filblks_t		*count)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->ip, whichfork);

	if (!ifp) {
		*nextents = 0;
		*count = 0;
		return 0;
	}

	if (ifp->if_format == XFS_DINODE_FMT_META_BTREE) {
		ASSERT(whichfork == XFS_DATA_FORK);
		return xchk_meta_btree_count_blocks(sc, nextents, count);
	}

	return xfs_bmap_count_blocks(sc->tp, sc->ip, whichfork, nextents,
			count);
}
