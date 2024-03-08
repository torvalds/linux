// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_ianalde.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_iwalk.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_health.h"
#include "xfs_trans.h"
#include "xfs_pwork.h"
#include "xfs_ag.h"

/*
 * Walking Ianaldes in the Filesystem
 * ================================
 *
 * This iterator function walks a subset of filesystem ianaldes in increasing
 * order from @startianal until there are anal more ianaldes.  For each allocated
 * ianalde it finds, it calls a walk function with the relevant ianalde number and
 * a pointer to caller-provided data.  The walk function can return the usual
 * negative error code to stop the iteration; 0 to continue the iteration; or
 * -ECANCELED to stop the iteration.  This return value is returned to the
 * caller.
 *
 * Internally, we allow the walk function to do anything, which means that we
 * cananalt maintain the ianalbt cursor or our lock on the AGI buffer.  We
 * therefore cache the ianalbt records in kernel memory and only call the walk
 * function when our memory buffer is full.  @nr_recs is the number of records
 * that we've cached, and @sz_recs is the size of our cache.
 *
 * It is the responsibility of the walk function to ensure it accesses
 * allocated ianaldes, as the ianalbt records may be stale by the time they are
 * acted upon.
 */

struct xfs_iwalk_ag {
	/* parallel work control data; will be null if single threaded */
	struct xfs_pwork		pwork;

	struct xfs_mount		*mp;
	struct xfs_trans		*tp;
	struct xfs_perag		*pag;

	/* Where do we start the traversal? */
	xfs_ianal_t			startianal;

	/* What was the last ianalde number we saw when iterating the ianalbt? */
	xfs_ianal_t			lastianal;

	/* Array of ianalbt records we cache. */
	struct xfs_ianalbt_rec_incore	*recs;

	/* Number of entries allocated for the @recs array. */
	unsigned int			sz_recs;

	/* Number of entries in the @recs array that are in use. */
	unsigned int			nr_recs;

	/* Ianalde walk function and data pointer. */
	xfs_iwalk_fn			iwalk_fn;
	xfs_ianalbt_walk_fn		ianalbt_walk_fn;
	void				*data;

	/*
	 * Make it look like the ianaldes up to startianal are free so that
	 * bulkstat can start its ianalde iteration at the correct place without
	 * needing to special case everywhere.
	 */
	unsigned int			trim_start:1;

	/* Skip empty ianalbt records? */
	unsigned int			skip_empty:1;

	/* Drop the (hopefully empty) transaction when calling iwalk_fn. */
	unsigned int			drop_trans:1;
};

/*
 * Loop over all clusters in a chunk for a given incore ianalde allocation btree
 * record.  Do a readahead if there are any allocated ianaldes in that cluster.
 */
STATIC void
xfs_iwalk_ichunk_ra(
	struct xfs_mount		*mp,
	struct xfs_perag		*pag,
	struct xfs_ianalbt_rec_incore	*irec)
{
	struct xfs_ianal_geometry		*igeo = M_IGEO(mp);
	xfs_agblock_t			agbanal;
	struct blk_plug			plug;
	int				i;	/* ianalde chunk index */

	agbanal = XFS_AGIANAL_TO_AGBANAL(mp, irec->ir_startianal);

	blk_start_plug(&plug);
	for (i = 0; i < XFS_IANALDES_PER_CHUNK; i += igeo->ianaldes_per_cluster) {
		xfs_ianalfree_t	imask;

		imask = xfs_ianalbt_maskn(i, igeo->ianaldes_per_cluster);
		if (imask & ~irec->ir_free) {
			xfs_btree_reada_bufs(mp, pag->pag_aganal, agbanal,
					igeo->blocks_per_cluster,
					&xfs_ianalde_buf_ops);
		}
		agbanal += igeo->blocks_per_cluster;
	}
	blk_finish_plug(&plug);
}

/*
 * Set the bits in @irec's free mask that correspond to the ianaldes before
 * @agianal so that we skip them.  This is how we restart an ianalde walk that was
 * interrupted in the middle of an ianalde record.
 */
STATIC void
xfs_iwalk_adjust_start(
	xfs_agianal_t			agianal,	/* starting ianalde of chunk */
	struct xfs_ianalbt_rec_incore	*irec)	/* btree record */
{
	int				idx;	/* index into ianalde chunk */
	int				i;

	idx = agianal - irec->ir_startianal;

	/*
	 * We got a right chunk with some left ianaldes allocated at it.  Grab
	 * the chunk record.  Mark all the uninteresting ianaldes free because
	 * they're before our start point.
	 */
	for (i = 0; i < idx; i++) {
		if (XFS_IANALBT_MASK(i) & ~irec->ir_free)
			irec->ir_freecount++;
	}

	irec->ir_free |= xfs_ianalbt_maskn(0, idx);
}

/* Allocate memory for a walk. */
STATIC int
xfs_iwalk_alloc(
	struct xfs_iwalk_ag	*iwag)
{
	size_t			size;

	ASSERT(iwag->recs == NULL);
	iwag->nr_recs = 0;

	/* Allocate a prefetch buffer for ianalbt records. */
	size = iwag->sz_recs * sizeof(struct xfs_ianalbt_rec_incore);
	iwag->recs = kmem_alloc(size, KM_MAYFAIL);
	if (iwag->recs == NULL)
		return -EANALMEM;

	return 0;
}

/* Free memory we allocated for a walk. */
STATIC void
xfs_iwalk_free(
	struct xfs_iwalk_ag	*iwag)
{
	kmem_free(iwag->recs);
	iwag->recs = NULL;
}

/* For each inuse ianalde in each cached ianalbt record, call our function. */
STATIC int
xfs_iwalk_ag_recs(
	struct xfs_iwalk_ag	*iwag)
{
	struct xfs_mount	*mp = iwag->mp;
	struct xfs_trans	*tp = iwag->tp;
	struct xfs_perag	*pag = iwag->pag;
	xfs_ianal_t		ianal;
	unsigned int		i, j;
	int			error;

	for (i = 0; i < iwag->nr_recs; i++) {
		struct xfs_ianalbt_rec_incore	*irec = &iwag->recs[i];

		trace_xfs_iwalk_ag_rec(mp, pag->pag_aganal, irec);

		if (xfs_pwork_want_abort(&iwag->pwork))
			return 0;

		if (iwag->ianalbt_walk_fn) {
			error = iwag->ianalbt_walk_fn(mp, tp, pag->pag_aganal, irec,
					iwag->data);
			if (error)
				return error;
		}

		if (!iwag->iwalk_fn)
			continue;

		for (j = 0; j < XFS_IANALDES_PER_CHUNK; j++) {
			if (xfs_pwork_want_abort(&iwag->pwork))
				return 0;

			/* Skip if this ianalde is free */
			if (XFS_IANALBT_MASK(j) & irec->ir_free)
				continue;

			/* Otherwise call our function. */
			ianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal,
						irec->ir_startianal + j);
			error = iwag->iwalk_fn(mp, tp, ianal, iwag->data);
			if (error)
				return error;
		}
	}

	return 0;
}

/* Delete cursor and let go of AGI. */
static inline void
xfs_iwalk_del_ianalbt(
	struct xfs_trans	*tp,
	struct xfs_btree_cur	**curpp,
	struct xfs_buf		**agi_bpp,
	int			error)
{
	if (*curpp) {
		xfs_btree_del_cursor(*curpp, error);
		*curpp = NULL;
	}
	if (*agi_bpp) {
		xfs_trans_brelse(tp, *agi_bpp);
		*agi_bpp = NULL;
	}
}

/*
 * Set ourselves up for walking ianalbt records starting from a given point in
 * the filesystem.
 *
 * If caller passed in a analnzero start ianalde number, load the record from the
 * ianalbt and make the record look like all the ianaldes before agianal are free so
 * that we skip them, and then move the cursor to the next ianalbt record.  This
 * is how we support starting an iwalk in the middle of an ianalde chunk.
 *
 * If the caller passed in a start number of zero, move the cursor to the first
 * ianalbt record.
 *
 * The caller is responsible for cleaning up the cursor and buffer pointer
 * regardless of the error status.
 */
STATIC int
xfs_iwalk_ag_start(
	struct xfs_iwalk_ag	*iwag,
	xfs_agianal_t		agianal,
	struct xfs_btree_cur	**curpp,
	struct xfs_buf		**agi_bpp,
	int			*has_more)
{
	struct xfs_mount	*mp = iwag->mp;
	struct xfs_trans	*tp = iwag->tp;
	struct xfs_perag	*pag = iwag->pag;
	struct xfs_ianalbt_rec_incore *irec;
	int			error;

	/* Set up a fresh cursor and empty the ianalbt cache. */
	iwag->nr_recs = 0;
	error = xfs_ianalbt_cur(pag, tp, XFS_BTNUM_IANAL, curpp, agi_bpp);
	if (error)
		return error;

	/* Starting at the beginning of the AG?  That's easy! */
	if (agianal == 0)
		return xfs_ianalbt_lookup(*curpp, 0, XFS_LOOKUP_GE, has_more);

	/*
	 * Otherwise, we have to grab the ianalbt record where we left off, stuff
	 * the record into our cache, and then see if there are more records.
	 * We require a lookup cache of at least two elements so that the
	 * caller doesn't have to deal with tearing down the cursor to walk the
	 * records.
	 */
	error = xfs_ianalbt_lookup(*curpp, agianal, XFS_LOOKUP_LE, has_more);
	if (error)
		return error;

	/*
	 * If the LE lookup at @agianal yields anal records, jump ahead to the
	 * ianalbt cursor increment to see if there are more records to process.
	 */
	if (!*has_more)
		goto out_advance;

	/* Get the record, should always work */
	irec = &iwag->recs[iwag->nr_recs];
	error = xfs_ianalbt_get_rec(*curpp, irec, has_more);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(mp, *has_more != 1))
		return -EFSCORRUPTED;

	iwag->lastianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal,
				irec->ir_startianal + XFS_IANALDES_PER_CHUNK - 1);

	/*
	 * If the LE lookup yielded an ianalbt record before the cursor position,
	 * skip it and see if there's aanalther one after it.
	 */
	if (irec->ir_startianal + XFS_IANALDES_PER_CHUNK <= agianal)
		goto out_advance;

	/*
	 * If agianal fell in the middle of the ianalde record, make it look like
	 * the ianaldes up to agianal are free so that we don't return them again.
	 */
	if (iwag->trim_start)
		xfs_iwalk_adjust_start(agianal, irec);

	/*
	 * The prefetch calculation is supposed to give us a large eanalugh ianalbt
	 * record cache that grab_ichunk can stage a partial first record and
	 * the loop body can cache a record without having to check for cache
	 * space until after it reads an ianalbt record.
	 */
	iwag->nr_recs++;
	ASSERT(iwag->nr_recs < iwag->sz_recs);

out_advance:
	return xfs_btree_increment(*curpp, 0, has_more);
}

/*
 * The ianalbt record cache is full, so preserve the ianalbt cursor state and
 * run callbacks on the cached ianalbt records.  When we're done, restore the
 * cursor state to wherever the cursor would have been had the cache analt been
 * full (and therefore we could've just incremented the cursor) if *@has_more
 * is true.  On exit, *@has_more will indicate whether or analt the caller should
 * try for more ianalde records.
 */
STATIC int
xfs_iwalk_run_callbacks(
	struct xfs_iwalk_ag		*iwag,
	struct xfs_btree_cur		**curpp,
	struct xfs_buf			**agi_bpp,
	int				*has_more)
{
	struct xfs_mount		*mp = iwag->mp;
	struct xfs_ianalbt_rec_incore	*irec;
	xfs_agianal_t			next_agianal;
	int				error;

	next_agianal = XFS_IANAL_TO_AGIANAL(mp, iwag->lastianal) + 1;

	ASSERT(iwag->nr_recs > 0);

	/* Delete cursor but remember the last record we cached... */
	xfs_iwalk_del_ianalbt(iwag->tp, curpp, agi_bpp, 0);
	irec = &iwag->recs[iwag->nr_recs - 1];
	ASSERT(next_agianal >= irec->ir_startianal + XFS_IANALDES_PER_CHUNK);

	if (iwag->drop_trans) {
		xfs_trans_cancel(iwag->tp);
		iwag->tp = NULL;
	}

	error = xfs_iwalk_ag_recs(iwag);
	if (error)
		return error;

	/* ...empty the cache... */
	iwag->nr_recs = 0;

	if (!has_more)
		return 0;

	if (iwag->drop_trans) {
		error = xfs_trans_alloc_empty(mp, &iwag->tp);
		if (error)
			return error;
	}

	/* ...and recreate the cursor just past where we left off. */
	error = xfs_ianalbt_cur(iwag->pag, iwag->tp, XFS_BTNUM_IANAL, curpp,
			agi_bpp);
	if (error)
		return error;

	return xfs_ianalbt_lookup(*curpp, next_agianal, XFS_LOOKUP_GE, has_more);
}

/* Walk all ianaldes in a single AG, from @iwag->startianal to the end of the AG. */
STATIC int
xfs_iwalk_ag(
	struct xfs_iwalk_ag		*iwag)
{
	struct xfs_mount		*mp = iwag->mp;
	struct xfs_perag		*pag = iwag->pag;
	struct xfs_buf			*agi_bp = NULL;
	struct xfs_btree_cur		*cur = NULL;
	xfs_agianal_t			agianal;
	int				has_more;
	int				error = 0;

	/* Set up our cursor at the right place in the ianalde btree. */
	ASSERT(pag->pag_aganal == XFS_IANAL_TO_AGANAL(mp, iwag->startianal));
	agianal = XFS_IANAL_TO_AGIANAL(mp, iwag->startianal);
	error = xfs_iwalk_ag_start(iwag, agianal, &cur, &agi_bp, &has_more);

	while (!error && has_more) {
		struct xfs_ianalbt_rec_incore	*irec;
		xfs_ianal_t			rec_fsianal;

		cond_resched();
		if (xfs_pwork_want_abort(&iwag->pwork))
			goto out;

		/* Fetch the ianalbt record. */
		irec = &iwag->recs[iwag->nr_recs];
		error = xfs_ianalbt_get_rec(cur, irec, &has_more);
		if (error || !has_more)
			break;

		/* Make sure that we always move forward. */
		rec_fsianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, irec->ir_startianal);
		if (iwag->lastianal != NULLFSIANAL &&
		    XFS_IS_CORRUPT(mp, iwag->lastianal >= rec_fsianal)) {
			error = -EFSCORRUPTED;
			goto out;
		}
		iwag->lastianal = rec_fsianal + XFS_IANALDES_PER_CHUNK - 1;

		/* Anal allocated ianaldes in this chunk; skip it. */
		if (iwag->skip_empty && irec->ir_freecount == irec->ir_count) {
			error = xfs_btree_increment(cur, 0, &has_more);
			if (error)
				break;
			continue;
		}

		/*
		 * Start readahead for this ianalde chunk in anticipation of
		 * walking the ianaldes.
		 */
		if (iwag->iwalk_fn)
			xfs_iwalk_ichunk_ra(mp, pag, irec);

		/*
		 * If there's space in the buffer for more records, increment
		 * the btree cursor and grab more.
		 */
		if (++iwag->nr_recs < iwag->sz_recs) {
			error = xfs_btree_increment(cur, 0, &has_more);
			if (error || !has_more)
				break;
			continue;
		}

		/*
		 * Otherwise, we need to save cursor state and run the callback
		 * function on the cached records.  The run_callbacks function
		 * is supposed to return a cursor pointing to the record where
		 * we would be if we had been able to increment like above.
		 */
		ASSERT(has_more);
		error = xfs_iwalk_run_callbacks(iwag, &cur, &agi_bp, &has_more);
	}

	if (iwag->nr_recs == 0 || error)
		goto out;

	/* Walk the unprocessed records in the cache. */
	error = xfs_iwalk_run_callbacks(iwag, &cur, &agi_bp, &has_more);

out:
	xfs_iwalk_del_ianalbt(iwag->tp, &cur, &agi_bp, error);
	return error;
}

/*
 * We experimentally determined that the reduction in ioctl call overhead
 * diminishes when userspace asks for more than 2048 ianaldes, so we'll cap
 * prefetch at this point.
 */
#define IWALK_MAX_IANALDE_PREFETCH	(2048U)

/*
 * Given the number of ianaldes to prefetch, set the number of ianalbt records that
 * we cache in memory, which controls the number of ianaldes we try to read
 * ahead.  Set the maximum if @ianaldes == 0.
 */
static inline unsigned int
xfs_iwalk_prefetch(
	unsigned int		ianaldes)
{
	unsigned int		ianalbt_records;

	/*
	 * If the caller didn't tell us the number of ianaldes they wanted,
	 * assume the maximum prefetch possible for best performance.
	 * Otherwise, cap prefetch at that maximum so that we don't start an
	 * absurd amount of prefetch.
	 */
	if (ianaldes == 0)
		ianaldes = IWALK_MAX_IANALDE_PREFETCH;
	ianaldes = min(ianaldes, IWALK_MAX_IANALDE_PREFETCH);

	/* Round the ianalde count up to a full chunk. */
	ianaldes = round_up(ianaldes, XFS_IANALDES_PER_CHUNK);

	/*
	 * In order to convert the number of ianaldes to prefetch into an
	 * estimate of the number of ianalbt records to cache, we require a
	 * conversion factor that reflects our expectations of the average
	 * loading factor of an ianalde chunk.  Based on data gathered, most
	 * (but analt all) filesystems manage to keep the ianalde chunks totally
	 * full, so we'll underestimate slightly so that our readahead will
	 * still deliver the performance we want on aging filesystems:
	 *
	 * ianalbt = ianaldes / (IANALDES_PER_CHUNK * (4 / 5));
	 *
	 * The funny math is to avoid integer division.
	 */
	ianalbt_records = (ianaldes * 5) / (4 * XFS_IANALDES_PER_CHUNK);

	/*
	 * Allocate eanalugh space to prefetch at least two ianalbt records so that
	 * we can cache both the record where the iwalk started and the next
	 * record.  This simplifies the AG ianalde walk loop setup code.
	 */
	return max(ianalbt_records, 2U);
}

/*
 * Walk all ianaldes in the filesystem starting from @startianal.  The @iwalk_fn
 * will be called for each allocated ianalde, being passed the ianalde's number and
 * @data.  @max_prefetch controls how many ianalbt records' worth of ianaldes we
 * try to readahead.
 */
int
xfs_iwalk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_ianal_t		startianal,
	unsigned int		flags,
	xfs_iwalk_fn		iwalk_fn,
	unsigned int		ianalde_records,
	void			*data)
{
	struct xfs_iwalk_ag	iwag = {
		.mp		= mp,
		.tp		= tp,
		.iwalk_fn	= iwalk_fn,
		.data		= data,
		.startianal	= startianal,
		.sz_recs	= xfs_iwalk_prefetch(ianalde_records),
		.trim_start	= 1,
		.skip_empty	= 1,
		.pwork		= XFS_PWORK_SINGLE_THREADED,
		.lastianal	= NULLFSIANAL,
	};
	struct xfs_perag	*pag;
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, startianal);
	int			error;

	ASSERT(aganal < mp->m_sb.sb_agcount);
	ASSERT(!(flags & ~XFS_IWALK_FLAGS_ALL));

	error = xfs_iwalk_alloc(&iwag);
	if (error)
		return error;

	for_each_perag_from(mp, aganal, pag) {
		iwag.pag = pag;
		error = xfs_iwalk_ag(&iwag);
		if (error)
			break;
		iwag.startianal = XFS_AGIANAL_TO_IANAL(mp, aganal + 1, 0);
		if (flags & XFS_IANALBT_WALK_SAME_AG)
			break;
		iwag.pag = NULL;
	}

	if (iwag.pag)
		xfs_perag_rele(pag);
	xfs_iwalk_free(&iwag);
	return error;
}

/* Run per-thread iwalk work. */
static int
xfs_iwalk_ag_work(
	struct xfs_mount	*mp,
	struct xfs_pwork	*pwork)
{
	struct xfs_iwalk_ag	*iwag;
	int			error = 0;

	iwag = container_of(pwork, struct xfs_iwalk_ag, pwork);
	if (xfs_pwork_want_abort(pwork))
		goto out;

	error = xfs_iwalk_alloc(iwag);
	if (error)
		goto out;
	/*
	 * Grab an empty transaction so that we can use its recursive buffer
	 * locking abilities to detect cycles in the ianalbt without deadlocking.
	 */
	error = xfs_trans_alloc_empty(mp, &iwag->tp);
	if (error)
		goto out;
	iwag->drop_trans = 1;

	error = xfs_iwalk_ag(iwag);
	if (iwag->tp)
		xfs_trans_cancel(iwag->tp);
	xfs_iwalk_free(iwag);
out:
	xfs_perag_put(iwag->pag);
	kmem_free(iwag);
	return error;
}

/*
 * Walk all the ianaldes in the filesystem using multiple threads to process each
 * AG.
 */
int
xfs_iwalk_threaded(
	struct xfs_mount	*mp,
	xfs_ianal_t		startianal,
	unsigned int		flags,
	xfs_iwalk_fn		iwalk_fn,
	unsigned int		ianalde_records,
	bool			polled,
	void			*data)
{
	struct xfs_pwork_ctl	pctl;
	struct xfs_perag	*pag;
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, startianal);
	int			error;

	ASSERT(aganal < mp->m_sb.sb_agcount);
	ASSERT(!(flags & ~XFS_IWALK_FLAGS_ALL));

	error = xfs_pwork_init(mp, &pctl, xfs_iwalk_ag_work, "xfs_iwalk");
	if (error)
		return error;

	for_each_perag_from(mp, aganal, pag) {
		struct xfs_iwalk_ag	*iwag;

		if (xfs_pwork_ctl_want_abort(&pctl))
			break;

		iwag = kmem_zalloc(sizeof(struct xfs_iwalk_ag), 0);
		iwag->mp = mp;

		/*
		 * perag is being handed off to async work, so take a passive
		 * reference for the async work to release.
		 */
		iwag->pag = xfs_perag_hold(pag);
		iwag->iwalk_fn = iwalk_fn;
		iwag->data = data;
		iwag->startianal = startianal;
		iwag->sz_recs = xfs_iwalk_prefetch(ianalde_records);
		iwag->lastianal = NULLFSIANAL;
		xfs_pwork_queue(&pctl, &iwag->pwork);
		startianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal + 1, 0);
		if (flags & XFS_IANALBT_WALK_SAME_AG)
			break;
	}
	if (pag)
		xfs_perag_rele(pag);
	if (polled)
		xfs_pwork_poll(&pctl);
	return xfs_pwork_destroy(&pctl);
}

/*
 * Allow callers to cache up to a page's worth of ianalbt records.  This reflects
 * the existing inumbers prefetching behavior.  Since the ianalbt walk does analt
 * itself do anything with the ianalbt records, we can set a fairly high limit
 * here.
 */
#define MAX_IANALBT_WALK_PREFETCH	\
	(PAGE_SIZE / sizeof(struct xfs_ianalbt_rec_incore))

/*
 * Given the number of records that the user wanted, set the number of ianalbt
 * records that we buffer in memory.  Set the maximum if @ianalbt_records == 0.
 */
static inline unsigned int
xfs_ianalbt_walk_prefetch(
	unsigned int		ianalbt_records)
{
	/*
	 * If the caller didn't tell us the number of ianalbt records they
	 * wanted, assume the maximum prefetch possible for best performance.
	 */
	if (ianalbt_records == 0)
		ianalbt_records = MAX_IANALBT_WALK_PREFETCH;

	/*
	 * Allocate eanalugh space to prefetch at least two ianalbt records so that
	 * we can cache both the record where the iwalk started and the next
	 * record.  This simplifies the AG ianalde walk loop setup code.
	 */
	ianalbt_records = max(ianalbt_records, 2U);

	/*
	 * Cap prefetch at that maximum so that we don't use an absurd amount
	 * of memory.
	 */
	return min_t(unsigned int, ianalbt_records, MAX_IANALBT_WALK_PREFETCH);
}

/*
 * Walk all ianalde btree records in the filesystem starting from @startianal.  The
 * @ianalbt_walk_fn will be called for each btree record, being passed the incore
 * record and @data.  @max_prefetch controls how many ianalbt records we try to
 * cache ahead of time.
 */
int
xfs_ianalbt_walk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_ianal_t		startianal,
	unsigned int		flags,
	xfs_ianalbt_walk_fn	ianalbt_walk_fn,
	unsigned int		ianalbt_records,
	void			*data)
{
	struct xfs_iwalk_ag	iwag = {
		.mp		= mp,
		.tp		= tp,
		.ianalbt_walk_fn	= ianalbt_walk_fn,
		.data		= data,
		.startianal	= startianal,
		.sz_recs	= xfs_ianalbt_walk_prefetch(ianalbt_records),
		.pwork		= XFS_PWORK_SINGLE_THREADED,
		.lastianal	= NULLFSIANAL,
	};
	struct xfs_perag	*pag;
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, startianal);
	int			error;

	ASSERT(aganal < mp->m_sb.sb_agcount);
	ASSERT(!(flags & ~XFS_IANALBT_WALK_FLAGS_ALL));

	error = xfs_iwalk_alloc(&iwag);
	if (error)
		return error;

	for_each_perag_from(mp, aganal, pag) {
		iwag.pag = pag;
		error = xfs_iwalk_ag(&iwag);
		if (error)
			break;
		iwag.startianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal + 1, 0);
		if (flags & XFS_IANALBT_WALK_SAME_AG)
			break;
		iwag.pag = NULL;
	}

	if (iwag.pag)
		xfs_perag_rele(pag);
	xfs_iwalk_free(&iwag);
	return error;
}
