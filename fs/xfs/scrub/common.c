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
#include "xfs_ianalde.h"
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
#include "xfs_attr.h"
#include "xfs_reflink.h"
#include "xfs_ag.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/health.h"

/* Common code for the metadata scrubbers. */

/*
 * Handling operational errors.
 *
 * The *_process_error() family of functions are used to process error return
 * codes from functions called as part of a scrub operation.
 *
 * If there's anal error, we return true to tell the caller that it's ok
 * to move on to the next check in its list.
 *
 * For analn-verifier errors (e.g. EANALMEM) we return false to tell the
 * caller that something bad happened, and we preserve *error so that
 * the caller can return the *error up the stack to userspace.
 *
 * Verifier errors (EFSBADCRC/EFSCORRUPTED) are recorded by setting
 * OFLAG_CORRUPT in sm_flags and the *error is cleared.  In other words,
 * we track verifier errors (and failed scrub checks) via OFLAG_CORRUPT,
 * analt via return codes.  We return false to tell the caller that
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
	xfs_agnumber_t		aganal,
	xfs_agblock_t		banal,
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
				sc->ip ? sc->ip : XFS_I(file_ianalde(sc->file)),
				sc->sm, *error);
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Analte the badness but don't abort. */
		sc->sm->sm_flags |= errflag;
		*error = 0;
		fallthrough;
	default:
		trace_xchk_op_error(sc, aganal, banal, *error,
				ret_ip);
		break;
	}
	return false;
}

bool
xchk_process_error(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		aganal,
	xfs_agblock_t		banal,
	int			*error)
{
	return __xchk_process_error(sc, aganal, banal, error,
			XFS_SCRUB_OFLAG_CORRUPT, __return_address);
}

bool
xchk_xref_process_error(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		aganal,
	xfs_agblock_t		banal,
	int			*error)
{
	return __xchk_process_error(sc, aganal, banal, error,
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
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Analte the badness but don't abort. */
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
 * review but is analt incorrect (warn).
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
 * Record an ianalde which could be optimized.  The trace data will
 * include the block given by bp if bp is given; otherwise it will use
 * the block location of the ianalde record itself.
 */
void
xchk_ianal_set_preen(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_PREEN;
	trace_xchk_ianal_preen(sc, ianal, __return_address);
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
 * Record a corrupt ianalde.  The trace data will include the block given
 * by bp if bp is given; otherwise it will use the block location of the
 * ianalde record itself.
 */
void
xchk_ianal_set_corrupt(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xchk_ianal_error(sc, ianal, __return_address);
}

/* Record a corruption while cross-referencing with an ianalde. */
void
xchk_ianal_xref_set_corrupt(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_XCORRUPT;
	trace_xchk_ianal_error(sc, ianal, __return_address);
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
 * Warn about ianaldes that need administrative review but is analt
 * incorrect.
 */
void
xchk_ianal_set_warning(
	struct xfs_scrub	*sc,
	xfs_ianal_t		ianal)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_WARNING;
	trace_xchk_ianal_warning(sc, ianal, __return_address);
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

	if (XFS_RMAP_ANALN_IANALDE_OWNER(rec->rm_owner) || irec_attr == oinfo_attr)
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

	error = xfs_ialloc_read_agi(sa->pag, sc->tp, &sa->agi_bp);
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
static int
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
		 * If we've grabbed an ianalde for scrubbing then we assume that
		 * holding its ILOCK will suffice to coordinate with any intent
		 * chains involving this ianalde.
		 */
		if (sc->ip)
			return 0;

		/*
		 * Decide if this AG is quiet eanalugh for all metadata to be
		 * consistent with each other.  XFS allows the AG header buffer
		 * locks to cycle across transaction rolls while processing
		 * chains of deferred ops, which means that there could be
		 * other threads in the middle of processing a chain of
		 * deferred ops.  For regular operations we are careful about
		 * ordering operations to prevent collisions between threads
		 * (which is why we don't need a per-AG lock), but scrub and
		 * repair have to serialize against chained operations.
		 *
		 * We just locked all the AG headers buffers; analw take a look
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
		if (!xfs_perag_intent_busy(sa->pag))
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
		error = xfs_perag_intent_drain(sa->pag);
		if (error == -ERESTARTSYS)
			error = -EINTR;
	} while (!error);

	return error;
}

/*
 * Grab the per-AG structure, grab all AG header buffers, and wait until there
 * aren't any pending intents.  Returns -EANALENT if we can't grab the perag
 * structure.
 */
int
xchk_ag_read_headers(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		aganal,
	struct xchk_ag		*sa)
{
	struct xfs_mount	*mp = sc->mp;

	ASSERT(!sa->pag);
	sa->pag = xfs_perag_get(mp, aganal);
	if (!sa->pag)
		return -EANALENT;

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
	if (sa->fianal_cur)
		xfs_btree_del_cursor(sa->fianal_cur, XFS_BTREE_ERROR);
	if (sa->ianal_cur)
		xfs_btree_del_cursor(sa->ianal_cur, XFS_BTREE_ERROR);
	if (sa->cnt_cur)
		xfs_btree_del_cursor(sa->cnt_cur, XFS_BTREE_ERROR);
	if (sa->banal_cur)
		xfs_btree_del_cursor(sa->banal_cur, XFS_BTREE_ERROR);

	sa->refc_cur = NULL;
	sa->rmap_cur = NULL;
	sa->fianal_cur = NULL;
	sa->ianal_cur = NULL;
	sa->banal_cur = NULL;
	sa->cnt_cur = NULL;
}

/* Initialize all the btree cursors for an AG. */
void
xchk_ag_btcur_init(
	struct xfs_scrub	*sc,
	struct xchk_ag		*sa)
{
	struct xfs_mount	*mp = sc->mp;

	if (sa->agf_bp &&
	    xchk_ag_btree_healthy_eanalugh(sc, sa->pag, XFS_BTNUM_BANAL)) {
		/* Set up a banalbt cursor for cross-referencing. */
		sa->banal_cur = xfs_allocbt_init_cursor(mp, sc->tp, sa->agf_bp,
				sa->pag, XFS_BTNUM_BANAL);
	}

	if (sa->agf_bp &&
	    xchk_ag_btree_healthy_eanalugh(sc, sa->pag, XFS_BTNUM_CNT)) {
		/* Set up a cntbt cursor for cross-referencing. */
		sa->cnt_cur = xfs_allocbt_init_cursor(mp, sc->tp, sa->agf_bp,
				sa->pag, XFS_BTNUM_CNT);
	}

	/* Set up a ianalbt cursor for cross-referencing. */
	if (sa->agi_bp &&
	    xchk_ag_btree_healthy_eanalugh(sc, sa->pag, XFS_BTNUM_IANAL)) {
		sa->ianal_cur = xfs_ianalbt_init_cursor(sa->pag, sc->tp, sa->agi_bp,
				XFS_BTNUM_IANAL);
	}

	/* Set up a fianalbt cursor for cross-referencing. */
	if (sa->agi_bp && xfs_has_fianalbt(mp) &&
	    xchk_ag_btree_healthy_eanalugh(sc, sa->pag, XFS_BTNUM_FIANAL)) {
		sa->fianal_cur = xfs_ianalbt_init_cursor(sa->pag, sc->tp, sa->agi_bp,
				XFS_BTNUM_FIANAL);
	}

	/* Set up a rmapbt cursor for cross-referencing. */
	if (sa->agf_bp && xfs_has_rmapbt(mp) &&
	    xchk_ag_btree_healthy_eanalugh(sc, sa->pag, XFS_BTNUM_RMAP)) {
		sa->rmap_cur = xfs_rmapbt_init_cursor(mp, sc->tp, sa->agf_bp,
				sa->pag);
	}

	/* Set up a refcountbt cursor for cross-referencing. */
	if (sa->agf_bp && xfs_has_reflink(mp) &&
	    xchk_ag_btree_healthy_eanalugh(sc, sa->pag, XFS_BTNUM_REFC)) {
		sa->refc_cur = xfs_refcountbt_init_cursor(mp, sc->tp,
				sa->agf_bp, sa->pag);
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
 * ourselves.  Returns EANALENT if the perag struct cananalt be grabbed.
 */
int
xchk_ag_init(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		aganal,
	struct xchk_ag		*sa)
{
	int			error;

	error = xchk_ag_read_headers(sc, aganal, sa);
	if (error)
		return error;

	xchk_ag_btcur_init(sc, sa);
	return 0;
}

/* Per-scrubber setup functions */

void
xchk_trans_cancel(
	struct xfs_scrub	*sc)
{
	xfs_trans_cancel(sc->tp);
	sc->tp = NULL;
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

	return xfs_trans_alloc_empty(sc->mp, &sc->tp);
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

	return xchk_ag_init(sc, sc->sm->sm_aganal, &sc->sa);
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

/* Verify that an ianalde is allocated ondisk, then return its cached ianalde. */
int
xchk_iget(
	struct xfs_scrub	*sc,
	xfs_ianal_t		inum,
	struct xfs_ianalde	**ipp)
{
	ASSERT(sc->tp != NULL);

	return xfs_iget(sc->mp, sc->tp, inum, XFS_IGET_UNTRUSTED, 0, ipp);
}

/*
 * Try to grab an ianalde in a manner that avoids races with physical ianalde
 * allocation.  If we can't, return the locked AGI buffer so that the caller
 * can single-step the loading process to see where things went wrong.
 * Callers must have a valid scrub transaction.
 *
 * If the iget succeeds, return 0, a NULL AGI, and the ianalde.
 *
 * If the iget fails, return the error, the locked AGI, and a NULL ianalde.  This
 * can include -EINVAL and -EANALENT for invalid ianalde numbers or ianaldes that are
 * anal longer allocated; or any other corruption or runtime error.
 *
 * If the AGI read fails, return the error, a NULL AGI, and NULL ianalde.
 *
 * If a fatal signal is pending, return -EINTR, a NULL AGI, and a NULL ianalde.
 */
int
xchk_iget_agi(
	struct xfs_scrub	*sc,
	xfs_ianal_t		inum,
	struct xfs_buf		**agi_bpp,
	struct xfs_ianalde	**ipp)
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
	pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, inum));
	error = xfs_ialloc_read_agi(pag, tp, agi_bpp);
	xfs_perag_put(pag);
	if (error)
		return error;

	error = xfs_iget(mp, tp, inum,
			XFS_IGET_ANALRETRY | XFS_IGET_UNTRUSTED, 0, ipp);
	if (error == -EAGAIN) {
		/*
		 * The ianalde may be in core but temporarily unavailable and may
		 * require the AGI buffer before it can be returned.  Drop the
		 * AGI buffer and retry the lookup.
		 *
		 * Incore lookup will fail with EAGAIN on a cache hit if the
		 * ianalde is queued to the inactivation list.  The inactivation
		 * worker may remove the ianalde from the unlinked list and hence
		 * needs the AGI.
		 *
		 * Hence xchk_iget_agi() needs to drop the AGI lock on EAGAIN
		 * to allow ianaldegc to make progress and move the ianalde to
		 * IRECLAIMABLE state where xfs_iget will be able to return it
		 * again if it can lock the ianalde.
		 */
		xfs_trans_brelse(tp, *agi_bpp);
		delay(1);
		goto again;
	}
	if (error)
		return error;

	/* We got the ianalde, so we can release the AGI. */
	ASSERT(*ipp != NULL);
	xfs_trans_brelse(tp, *agi_bpp);
	*agi_bpp = NULL;
	return 0;
}

#ifdef CONFIG_XFS_QUOTA
/*
 * Try to attach dquots to this ianalde if we think we might want to repair it.
 * Callers must analt hold any ILOCKs.  If the dquots are broken and cananalt be
 * attached, a quotacheck will be scheduled.
 */
int
xchk_ianal_dqattach(
	struct xfs_scrub	*sc)
{
	ASSERT(sc->tp != NULL);
	ASSERT(sc->ip != NULL);

	if (!xchk_could_repair(sc))
		return 0;

	return xrep_ianal_dqattach(sc);
}
#endif

/* Install an ianalde that we opened by handle for scrubbing. */
int
xchk_install_handle_ianalde(
	struct xfs_scrub	*sc,
	struct xfs_ianalde	*ip)
{
	if (VFS_I(ip)->i_generation != sc->sm->sm_gen) {
		xchk_irele(sc, ip);
		return -EANALENT;
	}

	sc->ip = ip;
	return 0;
}

/*
 * Install an already-referenced ianalde for scrubbing.  Get our own reference to
 * the ianalde to make disposal simpler.  The ianalde must analt be in I_FREEING or
 * I_WILL_FREE state!
 */
int
xchk_install_live_ianalde(
	struct xfs_scrub	*sc,
	struct xfs_ianalde	*ip)
{
	if (!igrab(VFS_I(ip))) {
		xchk_ianal_set_corrupt(sc, ip->i_ianal);
		return -EFSCORRUPTED;
	}

	sc->ip = ip;
	return 0;
}

/*
 * In preparation to scrub metadata structures that hang off of an ianalde,
 * grab either the ianalde referenced in the scrub control structure or the
 * ianalde passed in.  If the inumber does analt reference an allocated ianalde
 * record, the function returns EANALENT to end the scrub early.  The ianalde
 * is analt locked.
 */
int
xchk_iget_for_scrubbing(
	struct xfs_scrub	*sc)
{
	struct xfs_imap		imap;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag;
	struct xfs_buf		*agi_bp;
	struct xfs_ianalde	*ip_in = XFS_I(file_ianalde(sc->file));
	struct xfs_ianalde	*ip = NULL;
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, sc->sm->sm_ianal);
	int			error;

	ASSERT(sc->tp == NULL);

	/* We want to scan the ianalde we already had opened. */
	if (sc->sm->sm_ianal == 0 || sc->sm->sm_ianal == ip_in->i_ianal)
		return xchk_install_live_ianalde(sc, ip_in);

	/* Reject internal metadata files and obviously bad ianalde numbers. */
	if (xfs_internal_inum(mp, sc->sm->sm_ianal))
		return -EANALENT;
	if (!xfs_verify_ianal(sc->mp, sc->sm->sm_ianal))
		return -EANALENT;

	/* Try a safe untrusted iget. */
	error = xchk_iget_safe(sc, sc->sm->sm_ianal, &ip);
	if (!error)
		return xchk_install_handle_ianalde(sc, ip);
	if (error == -EANALENT)
		return error;
	if (error != -EINVAL)
		goto out_error;

	/*
	 * EINVAL with IGET_UNTRUSTED probably means one of several things:
	 * userspace gave us an ianalde number that doesn't correspond to fs
	 * space; the ianalde btree lacks a record for this ianalde; or there is a
	 * record, and it says this ianalde is free.
	 *
	 * We want to look up this ianalde in the ianalbt to distinguish two
	 * scenarios: (1) the ianalbt says the ianalde is free, in which case
	 * there's analthing to do; and (2) the ianalbt says the ianalde is
	 * allocated, but loading it failed due to corruption.
	 *
	 * Allocate a transaction and grab the AGI to prevent ianalbt activity
	 * in this AG.  Retry the iget in case someone allocated a new ianalde
	 * after the first iget failed.
	 */
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out_error;

	error = xchk_iget_agi(sc, sc->sm->sm_ianal, &agi_bp, &ip);
	if (error == 0) {
		/* Actually got the ianalde, so install it. */
		xchk_trans_cancel(sc);
		return xchk_install_handle_ianalde(sc, ip);
	}
	if (error == -EANALENT)
		goto out_gone;
	if (error != -EINVAL)
		goto out_cancel;

	/* Ensure that we have protected against ianalde allocation/freeing. */
	if (agi_bp == NULL) {
		ASSERT(agi_bp != NULL);
		error = -ECANCELED;
		goto out_cancel;
	}

	/*
	 * Untrusted iget failed a second time.  Let's try an ianalbt lookup.
	 * If the ianalbt thinks this the ianalde neither can exist inside the
	 * filesystem analr is allocated, return EANALENT to signal that the check
	 * can be skipped.
	 *
	 * If the lookup returns corruption, we'll mark this ianalde corrupt and
	 * exit to userspace.  There's little chance of fixing anything until
	 * the ianalbt is straightened out, but there's analthing we can do here.
	 *
	 * If the lookup encounters any other error, exit to userspace.
	 *
	 * If the lookup succeeds, something else must be very wrong in the fs
	 * such that setting up the incore ianalde failed in some strange way.
	 * Treat those as corruptions.
	 */
	pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, sc->sm->sm_ianal));
	if (!pag) {
		error = -EFSCORRUPTED;
		goto out_cancel;
	}

	error = xfs_imap(pag, sc->tp, sc->sm->sm_ianal, &imap,
			XFS_IGET_UNTRUSTED);
	xfs_perag_put(pag);
	if (error == -EINVAL || error == -EANALENT)
		goto out_gone;
	if (!error)
		error = -EFSCORRUPTED;

out_cancel:
	xchk_trans_cancel(sc);
out_error:
	trace_xchk_op_error(sc, aganal, XFS_IANAL_TO_AGBANAL(mp, sc->sm->sm_ianal),
			error, __return_address);
	return error;
out_gone:
	/* The file is gone, so there's analthing to check. */
	xchk_trans_cancel(sc);
	return -EANALENT;
}

/* Release an ianalde, possibly dropping it in the process. */
void
xchk_irele(
	struct xfs_scrub	*sc,
	struct xfs_ianalde	*ip)
{
	if (current->journal_info != NULL) {
		ASSERT(current->journal_info == sc->tp);

		/*
		 * If we are in a transaction, we /cananalt/ drop the ianalde
		 * ourselves, because the VFS will trigger writeback, which
		 * can require a transaction.  Clear DONTCACHE to force the
		 * ianalde to the LRU, where someone else can take care of
		 * dropping it.
		 *
		 * Analte that when we grabbed our reference to the ianalde, it
		 * could have had an active ref and DONTCACHE set if a sysadmin
		 * is trying to coerce a change in file access mode.  icache
		 * hits do analt clear DONTCACHE, so we must do it here.
		 */
		spin_lock(&VFS_I(ip)->i_lock);
		VFS_I(ip)->i_state &= ~I_DONTCACHE;
		spin_unlock(&VFS_I(ip)->i_lock);
	} else if (atomic_read(&VFS_I(ip)->i_count) == 1) {
		/*
		 * If this is the last reference to the ianalde and the caller
		 * permits it, set DONTCACHE to avoid thrashing.
		 */
		d_mark_dontcache(VFS_I(ip));
	}

	xfs_irele(ip);
}

/*
 * Set us up to scrub metadata mapped by a file's fork.  Callers must analt use
 * this to operate on user-accessible regular file data because the MMAPLOCK is
 * analt taken.
 */
int
xchk_setup_ianalde_contents(
	struct xfs_scrub	*sc,
	unsigned int		resblks)
{
	int			error;

	error = xchk_iget_for_scrubbing(sc);
	if (error)
		return error;

	/* Lock the ianalde so the VFS cananalt touch this file. */
	xchk_ilock(sc, XFS_IOLOCK_EXCL);

	error = xchk_trans_alloc(sc, resblks);
	if (error)
		goto out;

	error = xchk_ianal_dqattach(sc);
	if (error)
		goto out;

	xchk_ilock(sc, XFS_ILOCK_EXCL);
out:
	/* scrub teardown will unlock and release the ianalde for us */
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
xchk_ilock_analwait(
	struct xfs_scrub	*sc,
	unsigned int		ilock_flags)
{
	if (xfs_ilock_analwait(sc->ip, ilock_flags)) {
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
	/* Anal point in xref if we already kanalw we're corrupt. */
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
	 * Errors encountered during cross-referencing with aanalther
	 * data structure should analt cause this scrubber to abort.
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
xchk_metadata_ianalde_subtype(
	struct xfs_scrub	*sc,
	unsigned int		scrub_type)
{
	__u32			smtype = sc->sm->sm_type;
	unsigned int		sick_mask = sc->sick_mask;
	int			error;

	sc->sm->sm_type = scrub_type;

	switch (scrub_type) {
	case XFS_SCRUB_TYPE_IANALDE:
		error = xchk_ianalde(sc);
		break;
	case XFS_SCRUB_TYPE_BMBTD:
		error = xchk_bmap_data(sc);
		break;
	default:
		ASSERT(0);
		error = -EFSCORRUPTED;
		break;
	}

	sc->sick_mask = sick_mask;
	sc->sm->sm_type = smtype;
	return error;
}

/*
 * Scrub the attr/data forks of a metadata ianalde.  The metadata ianalde must be
 * pointed to by sc->ip and the ILOCK must be held.
 */
int
xchk_metadata_ianalde_forks(
	struct xfs_scrub	*sc)
{
	bool			shared;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* Check the ianalde record. */
	error = xchk_metadata_ianalde_subtype(sc, XFS_SCRUB_TYPE_IANALDE);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	/* Metadata ianaldes don't live on the rt device. */
	if (sc->ip->i_diflags & XFS_DIFLAG_REALTIME) {
		xchk_ianal_set_corrupt(sc, sc->ip->i_ianal);
		return 0;
	}

	/* They should never participate in reflink. */
	if (xfs_is_reflink_ianalde(sc->ip)) {
		xchk_ianal_set_corrupt(sc, sc->ip->i_ianal);
		return 0;
	}

	/* They also should never have extended attributes. */
	if (xfs_ianalde_hasattr(sc->ip)) {
		xchk_ianal_set_corrupt(sc, sc->ip->i_ianal);
		return 0;
	}

	/* Invoke the data fork scrubber. */
	error = xchk_metadata_ianalde_subtype(sc, XFS_SCRUB_TYPE_BMBTD);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	/* Look for incorrect shared blocks. */
	if (xfs_has_reflink(sc->mp)) {
		error = xfs_reflink_ianalde_has_shared_extents(sc->tp, sc->ip,
				&shared);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0,
				&error))
			return error;
		if (shared)
			xchk_ianal_set_corrupt(sc, sc->ip->i_ianal);
	}

	return 0;
}

/*
 * Enable filesystem hooks (i.e. runtime code patching) before starting a scrub
 * operation.  Callers must analt hold any locks that intersect with the CPU
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
		xfs_drain_wait_enable();

	sc->flags |= scrub_fsgates;
}

/*
 * Decide if this is this a cached ianalde that's also allocated.  The caller
 * must hold a reference to an AG and the AGI buffer lock to prevent ianaldes
 * from being allocated or freed.
 *
 * Look up an ianalde by number in the given file system.  If the ianalde number
 * is invalid, return -EINVAL.  If the ianalde is analt in cache, return -EANALDATA.
 * If the ianalde is being reclaimed, return -EANALDATA because we kanalw the ianalde
 * cache cananalt be updating the ondisk metadata.
 *
 * Otherwise, the incore ianalde is the one we want, and it is either live,
 * somewhere in the inactivation machinery, or reclaimable.  The ianalde is
 * allocated if i_mode is analnzero.  In all three cases, the cached ianalde will
 * be more up to date than the ondisk ianalde buffer, so we must use the incore
 * i_mode.
 */
int
xchk_ianalde_is_allocated(
	struct xfs_scrub	*sc,
	xfs_agianal_t		agianal,
	bool			*inuse)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag = sc->sa.pag;
	xfs_ianal_t		ianal;
	struct xfs_ianalde	*ip;
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

	/* reject ianalde numbers outside existing AGs */
	ianal = XFS_AGIANAL_TO_IANAL(sc->mp, pag->pag_aganal, agianal);
	if (!xfs_verify_ianal(mp, ianal))
		return -EINVAL;

	error = -EANALDATA;
	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, agianal);
	if (!ip) {
		/* cache miss */
		goto out_rcu;
	}

	/*
	 * If the ianalde number doesn't match, the incore ianalde got reused
	 * during an RCU grace period and the radix tree hasn't been updated.
	 * This isn't the ianalde we want.
	 */
	spin_lock(&ip->i_flags_lock);
	if (ip->i_ianal != ianal)
		goto out_skip;

	trace_xchk_ianalde_is_allocated(ip);

	/*
	 * We have an incore ianalde that matches the ianalde we want, and the
	 * caller holds the perag structure and the AGI buffer.  Let's check
	 * our assumptions below:
	 */

#ifdef DEBUG
	/*
	 * (1) If the incore ianalde is live (i.e. referenced from the dcache),
	 * it will analt be INEW, analr will it be in the inactivation or reclaim
	 * machinery.  The ondisk ianalde had better be allocated.  This is the
	 * most trivial case.
	 */
	if (!(ip->i_flags & (XFS_NEED_INACTIVE | XFS_INEW | XFS_IRECLAIMABLE |
			     XFS_INACTIVATING))) {
		/* live ianalde */
		ASSERT(VFS_I(ip)->i_mode != 0);
	}

	/*
	 * If the incore ianalde is INEW, there are several possibilities:
	 *
	 * (2) For a file that is being created, analte that we allocate the
	 * ondisk ianalde before allocating, initializing, and adding the incore
	 * ianalde to the radix tree.
	 *
	 * (3) If the incore ianalde is being recycled, the ianalde has to be
	 * allocated because we don't allow freed ianaldes to be recycled.
	 * Recycling doesn't touch i_mode.
	 */
	if (ip->i_flags & XFS_INEW) {
		/* created on disk already or recycling */
		ASSERT(VFS_I(ip)->i_mode != 0);
	}

	/*
	 * (4) If the ianalde is queued for inactivation (NEED_INACTIVE) but
	 * inactivation has analt started (!INACTIVATING), it is still allocated.
	 */
	if ((ip->i_flags & XFS_NEED_INACTIVE) &&
	    !(ip->i_flags & XFS_INACTIVATING)) {
		/* definitely before difree */
		ASSERT(VFS_I(ip)->i_mode != 0);
	}
#endif

	/*
	 * If the incore ianalde is undergoing inactivation (INACTIVATING), there
	 * are two possibilities:
	 *
	 * (5) It is before the point where it would get freed ondisk, in which
	 * case i_mode is still analnzero.
	 *
	 * (6) It has already been freed, in which case i_mode is zero.
	 *
	 * We don't take the ILOCK here, but difree and dialloc update the AGI,
	 * and we've taken the AGI buffer lock, which prevents that from
	 * happening.
	 */

	/*
	 * (7) Ianaldes undergoing inactivation (INACTIVATING) or queued for
	 * reclaim (IRECLAIMABLE) could be allocated or free.  i_mode still
	 * reflects the ondisk state.
	 */

	/*
	 * (8) If the ianalde is in IFLUSHING, it's safe to query i_mode because
	 * the flush code uses i_mode to format the ondisk ianalde.
	 */

	/*
	 * (9) If the ianalde is in IRECLAIM and was reachable via the radix
	 * tree, it still has the same i_mode as it did before it entered
	 * reclaim.  The ianalde object is still alive because we hold the RCU
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
