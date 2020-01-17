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
#include "xfs_iyesde.h"
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

/*
 * Walking Iyesdes in the Filesystem
 * ================================
 *
 * This iterator function walks a subset of filesystem iyesdes in increasing
 * order from @startiyes until there are yes more iyesdes.  For each allocated
 * iyesde it finds, it calls a walk function with the relevant iyesde number and
 * a pointer to caller-provided data.  The walk function can return the usual
 * negative error code to stop the iteration; 0 to continue the iteration; or
 * -ECANCELED to stop the iteration.  This return value is returned to the
 * caller.
 *
 * Internally, we allow the walk function to do anything, which means that we
 * canyest maintain the iyesbt cursor or our lock on the AGI buffer.  We
 * therefore cache the iyesbt records in kernel memory and only call the walk
 * function when our memory buffer is full.  @nr_recs is the number of records
 * that we've cached, and @sz_recs is the size of our cache.
 *
 * It is the responsibility of the walk function to ensure it accesses
 * allocated iyesdes, as the iyesbt records may be stale by the time they are
 * acted upon.
 */

struct xfs_iwalk_ag {
	/* parallel work control data; will be null if single threaded */
	struct xfs_pwork		pwork;

	struct xfs_mount		*mp;
	struct xfs_trans		*tp;

	/* Where do we start the traversal? */
	xfs_iyes_t			startiyes;

	/* Array of iyesbt records we cache. */
	struct xfs_iyesbt_rec_incore	*recs;

	/* Number of entries allocated for the @recs array. */
	unsigned int			sz_recs;

	/* Number of entries in the @recs array that are in use. */
	unsigned int			nr_recs;

	/* Iyesde walk function and data pointer. */
	xfs_iwalk_fn			iwalk_fn;
	xfs_iyesbt_walk_fn		iyesbt_walk_fn;
	void				*data;

	/*
	 * Make it look like the iyesdes up to startiyes are free so that
	 * bulkstat can start its iyesde iteration at the correct place without
	 * needing to special case everywhere.
	 */
	unsigned int			trim_start:1;

	/* Skip empty iyesbt records? */
	unsigned int			skip_empty:1;
};

/*
 * Loop over all clusters in a chunk for a given incore iyesde allocation btree
 * record.  Do a readahead if there are any allocated iyesdes in that cluster.
 */
STATIC void
xfs_iwalk_ichunk_ra(
	struct xfs_mount		*mp,
	xfs_agnumber_t			agyes,
	struct xfs_iyesbt_rec_incore	*irec)
{
	struct xfs_iyes_geometry		*igeo = M_IGEO(mp);
	xfs_agblock_t			agbyes;
	struct blk_plug			plug;
	int				i;	/* iyesde chunk index */

	agbyes = XFS_AGINO_TO_AGBNO(mp, irec->ir_startiyes);

	blk_start_plug(&plug);
	for (i = 0; i < XFS_INODES_PER_CHUNK; i += igeo->iyesdes_per_cluster) {
		xfs_iyesfree_t	imask;

		imask = xfs_iyesbt_maskn(i, igeo->iyesdes_per_cluster);
		if (imask & ~irec->ir_free) {
			xfs_btree_reada_bufs(mp, agyes, agbyes,
					igeo->blocks_per_cluster,
					&xfs_iyesde_buf_ops);
		}
		agbyes += igeo->blocks_per_cluster;
	}
	blk_finish_plug(&plug);
}

/*
 * Set the bits in @irec's free mask that correspond to the iyesdes before
 * @agiyes so that we skip them.  This is how we restart an iyesde walk that was
 * interrupted in the middle of an iyesde record.
 */
STATIC void
xfs_iwalk_adjust_start(
	xfs_agiyes_t			agiyes,	/* starting iyesde of chunk */
	struct xfs_iyesbt_rec_incore	*irec)	/* btree record */
{
	int				idx;	/* index into iyesde chunk */
	int				i;

	idx = agiyes - irec->ir_startiyes;

	/*
	 * We got a right chunk with some left iyesdes allocated at it.  Grab
	 * the chunk record.  Mark all the uninteresting iyesdes free because
	 * they're before our start point.
	 */
	for (i = 0; i < idx; i++) {
		if (XFS_INOBT_MASK(i) & ~irec->ir_free)
			irec->ir_freecount++;
	}

	irec->ir_free |= xfs_iyesbt_maskn(0, idx);
}

/* Allocate memory for a walk. */
STATIC int
xfs_iwalk_alloc(
	struct xfs_iwalk_ag	*iwag)
{
	size_t			size;

	ASSERT(iwag->recs == NULL);
	iwag->nr_recs = 0;

	/* Allocate a prefetch buffer for iyesbt records. */
	size = iwag->sz_recs * sizeof(struct xfs_iyesbt_rec_incore);
	iwag->recs = kmem_alloc(size, KM_MAYFAIL);
	if (iwag->recs == NULL)
		return -ENOMEM;

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

/* For each inuse iyesde in each cached iyesbt record, call our function. */
STATIC int
xfs_iwalk_ag_recs(
	struct xfs_iwalk_ag		*iwag)
{
	struct xfs_mount		*mp = iwag->mp;
	struct xfs_trans		*tp = iwag->tp;
	xfs_iyes_t			iyes;
	unsigned int			i, j;
	xfs_agnumber_t			agyes;
	int				error;

	agyes = XFS_INO_TO_AGNO(mp, iwag->startiyes);
	for (i = 0; i < iwag->nr_recs; i++) {
		struct xfs_iyesbt_rec_incore	*irec = &iwag->recs[i];

		trace_xfs_iwalk_ag_rec(mp, agyes, irec);

		if (xfs_pwork_want_abort(&iwag->pwork))
			return 0;

		if (iwag->iyesbt_walk_fn) {
			error = iwag->iyesbt_walk_fn(mp, tp, agyes, irec,
					iwag->data);
			if (error)
				return error;
		}

		if (!iwag->iwalk_fn)
			continue;

		for (j = 0; j < XFS_INODES_PER_CHUNK; j++) {
			if (xfs_pwork_want_abort(&iwag->pwork))
				return 0;

			/* Skip if this iyesde is free */
			if (XFS_INOBT_MASK(j) & irec->ir_free)
				continue;

			/* Otherwise call our function. */
			iyes = XFS_AGINO_TO_INO(mp, agyes, irec->ir_startiyes + j);
			error = iwag->iwalk_fn(mp, tp, iyes, iwag->data);
			if (error)
				return error;
		}
	}

	return 0;
}

/* Delete cursor and let go of AGI. */
static inline void
xfs_iwalk_del_iyesbt(
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
 * Set ourselves up for walking iyesbt records starting from a given point in
 * the filesystem.
 *
 * If caller passed in a yesnzero start iyesde number, load the record from the
 * iyesbt and make the record look like all the iyesdes before agiyes are free so
 * that we skip them, and then move the cursor to the next iyesbt record.  This
 * is how we support starting an iwalk in the middle of an iyesde chunk.
 *
 * If the caller passed in a start number of zero, move the cursor to the first
 * iyesbt record.
 *
 * The caller is responsible for cleaning up the cursor and buffer pointer
 * regardless of the error status.
 */
STATIC int
xfs_iwalk_ag_start(
	struct xfs_iwalk_ag	*iwag,
	xfs_agnumber_t		agyes,
	xfs_agiyes_t		agiyes,
	struct xfs_btree_cur	**curpp,
	struct xfs_buf		**agi_bpp,
	int			*has_more)
{
	struct xfs_mount	*mp = iwag->mp;
	struct xfs_trans	*tp = iwag->tp;
	struct xfs_iyesbt_rec_incore *irec;
	int			error;

	/* Set up a fresh cursor and empty the iyesbt cache. */
	iwag->nr_recs = 0;
	error = xfs_iyesbt_cur(mp, tp, agyes, XFS_BTNUM_INO, curpp, agi_bpp);
	if (error)
		return error;

	/* Starting at the beginning of the AG?  That's easy! */
	if (agiyes == 0)
		return xfs_iyesbt_lookup(*curpp, 0, XFS_LOOKUP_GE, has_more);

	/*
	 * Otherwise, we have to grab the iyesbt record where we left off, stuff
	 * the record into our cache, and then see if there are more records.
	 * We require a lookup cache of at least two elements so that the
	 * caller doesn't have to deal with tearing down the cursor to walk the
	 * records.
	 */
	error = xfs_iyesbt_lookup(*curpp, agiyes, XFS_LOOKUP_LE, has_more);
	if (error)
		return error;

	/*
	 * If the LE lookup at @agiyes yields yes records, jump ahead to the
	 * iyesbt cursor increment to see if there are more records to process.
	 */
	if (!*has_more)
		goto out_advance;

	/* Get the record, should always work */
	irec = &iwag->recs[iwag->nr_recs];
	error = xfs_iyesbt_get_rec(*curpp, irec, has_more);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(mp, *has_more != 1))
		return -EFSCORRUPTED;

	/*
	 * If the LE lookup yielded an iyesbt record before the cursor position,
	 * skip it and see if there's ayesther one after it.
	 */
	if (irec->ir_startiyes + XFS_INODES_PER_CHUNK <= agiyes)
		goto out_advance;

	/*
	 * If agiyes fell in the middle of the iyesde record, make it look like
	 * the iyesdes up to agiyes are free so that we don't return them again.
	 */
	if (iwag->trim_start)
		xfs_iwalk_adjust_start(agiyes, irec);

	/*
	 * The prefetch calculation is supposed to give us a large eyesugh iyesbt
	 * record cache that grab_ichunk can stage a partial first record and
	 * the loop body can cache a record without having to check for cache
	 * space until after it reads an iyesbt record.
	 */
	iwag->nr_recs++;
	ASSERT(iwag->nr_recs < iwag->sz_recs);

out_advance:
	return xfs_btree_increment(*curpp, 0, has_more);
}

/*
 * The iyesbt record cache is full, so preserve the iyesbt cursor state and
 * run callbacks on the cached iyesbt records.  When we're done, restore the
 * cursor state to wherever the cursor would have been had the cache yest been
 * full (and therefore we could've just incremented the cursor) if *@has_more
 * is true.  On exit, *@has_more will indicate whether or yest the caller should
 * try for more iyesde records.
 */
STATIC int
xfs_iwalk_run_callbacks(
	struct xfs_iwalk_ag		*iwag,
	xfs_agnumber_t			agyes,
	struct xfs_btree_cur		**curpp,
	struct xfs_buf			**agi_bpp,
	int				*has_more)
{
	struct xfs_mount		*mp = iwag->mp;
	struct xfs_trans		*tp = iwag->tp;
	struct xfs_iyesbt_rec_incore	*irec;
	xfs_agiyes_t			restart;
	int				error;

	ASSERT(iwag->nr_recs > 0);

	/* Delete cursor but remember the last record we cached... */
	xfs_iwalk_del_iyesbt(tp, curpp, agi_bpp, 0);
	irec = &iwag->recs[iwag->nr_recs - 1];
	restart = irec->ir_startiyes + XFS_INODES_PER_CHUNK - 1;

	error = xfs_iwalk_ag_recs(iwag);
	if (error)
		return error;

	/* ...empty the cache... */
	iwag->nr_recs = 0;

	if (!has_more)
		return 0;

	/* ...and recreate the cursor just past where we left off. */
	error = xfs_iyesbt_cur(mp, tp, agyes, XFS_BTNUM_INO, curpp, agi_bpp);
	if (error)
		return error;

	return xfs_iyesbt_lookup(*curpp, restart, XFS_LOOKUP_GE, has_more);
}

/* Walk all iyesdes in a single AG, from @iwag->startiyes to the end of the AG. */
STATIC int
xfs_iwalk_ag(
	struct xfs_iwalk_ag		*iwag)
{
	struct xfs_mount		*mp = iwag->mp;
	struct xfs_trans		*tp = iwag->tp;
	struct xfs_buf			*agi_bp = NULL;
	struct xfs_btree_cur		*cur = NULL;
	xfs_agnumber_t			agyes;
	xfs_agiyes_t			agiyes;
	int				has_more;
	int				error = 0;

	/* Set up our cursor at the right place in the iyesde btree. */
	agyes = XFS_INO_TO_AGNO(mp, iwag->startiyes);
	agiyes = XFS_INO_TO_AGINO(mp, iwag->startiyes);
	error = xfs_iwalk_ag_start(iwag, agyes, agiyes, &cur, &agi_bp, &has_more);

	while (!error && has_more) {
		struct xfs_iyesbt_rec_incore	*irec;

		cond_resched();
		if (xfs_pwork_want_abort(&iwag->pwork))
			goto out;

		/* Fetch the iyesbt record. */
		irec = &iwag->recs[iwag->nr_recs];
		error = xfs_iyesbt_get_rec(cur, irec, &has_more);
		if (error || !has_more)
			break;

		/* No allocated iyesdes in this chunk; skip it. */
		if (iwag->skip_empty && irec->ir_freecount == irec->ir_count) {
			error = xfs_btree_increment(cur, 0, &has_more);
			if (error)
				break;
			continue;
		}

		/*
		 * Start readahead for this iyesde chunk in anticipation of
		 * walking the iyesdes.
		 */
		if (iwag->iwalk_fn)
			xfs_iwalk_ichunk_ra(mp, agyes, irec);

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
		error = xfs_iwalk_run_callbacks(iwag, agyes, &cur, &agi_bp,
				&has_more);
	}

	if (iwag->nr_recs == 0 || error)
		goto out;

	/* Walk the unprocessed records in the cache. */
	error = xfs_iwalk_run_callbacks(iwag, agyes, &cur, &agi_bp, &has_more);

out:
	xfs_iwalk_del_iyesbt(tp, &cur, &agi_bp, error);
	return error;
}

/*
 * We experimentally determined that the reduction in ioctl call overhead
 * diminishes when userspace asks for more than 2048 iyesdes, so we'll cap
 * prefetch at this point.
 */
#define IWALK_MAX_INODE_PREFETCH	(2048U)

/*
 * Given the number of iyesdes to prefetch, set the number of iyesbt records that
 * we cache in memory, which controls the number of iyesdes we try to read
 * ahead.  Set the maximum if @iyesdes == 0.
 */
static inline unsigned int
xfs_iwalk_prefetch(
	unsigned int		iyesdes)
{
	unsigned int		iyesbt_records;

	/*
	 * If the caller didn't tell us the number of iyesdes they wanted,
	 * assume the maximum prefetch possible for best performance.
	 * Otherwise, cap prefetch at that maximum so that we don't start an
	 * absurd amount of prefetch.
	 */
	if (iyesdes == 0)
		iyesdes = IWALK_MAX_INODE_PREFETCH;
	iyesdes = min(iyesdes, IWALK_MAX_INODE_PREFETCH);

	/* Round the iyesde count up to a full chunk. */
	iyesdes = round_up(iyesdes, XFS_INODES_PER_CHUNK);

	/*
	 * In order to convert the number of iyesdes to prefetch into an
	 * estimate of the number of iyesbt records to cache, we require a
	 * conversion factor that reflects our expectations of the average
	 * loading factor of an iyesde chunk.  Based on data gathered, most
	 * (but yest all) filesystems manage to keep the iyesde chunks totally
	 * full, so we'll underestimate slightly so that our readahead will
	 * still deliver the performance we want on aging filesystems:
	 *
	 * iyesbt = iyesdes / (INODES_PER_CHUNK * (4 / 5));
	 *
	 * The funny math is to avoid integer division.
	 */
	iyesbt_records = (iyesdes * 5) / (4 * XFS_INODES_PER_CHUNK);

	/*
	 * Allocate eyesugh space to prefetch at least two iyesbt records so that
	 * we can cache both the record where the iwalk started and the next
	 * record.  This simplifies the AG iyesde walk loop setup code.
	 */
	return max(iyesbt_records, 2U);
}

/*
 * Walk all iyesdes in the filesystem starting from @startiyes.  The @iwalk_fn
 * will be called for each allocated iyesde, being passed the iyesde's number and
 * @data.  @max_prefetch controls how many iyesbt records' worth of iyesdes we
 * try to readahead.
 */
int
xfs_iwalk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_iyes_t		startiyes,
	unsigned int		flags,
	xfs_iwalk_fn		iwalk_fn,
	unsigned int		iyesde_records,
	void			*data)
{
	struct xfs_iwalk_ag	iwag = {
		.mp		= mp,
		.tp		= tp,
		.iwalk_fn	= iwalk_fn,
		.data		= data,
		.startiyes	= startiyes,
		.sz_recs	= xfs_iwalk_prefetch(iyesde_records),
		.trim_start	= 1,
		.skip_empty	= 1,
		.pwork		= XFS_PWORK_SINGLE_THREADED,
	};
	xfs_agnumber_t		agyes = XFS_INO_TO_AGNO(mp, startiyes);
	int			error;

	ASSERT(agyes < mp->m_sb.sb_agcount);
	ASSERT(!(flags & ~XFS_IWALK_FLAGS_ALL));

	error = xfs_iwalk_alloc(&iwag);
	if (error)
		return error;

	for (; agyes < mp->m_sb.sb_agcount; agyes++) {
		error = xfs_iwalk_ag(&iwag);
		if (error)
			break;
		iwag.startiyes = XFS_AGINO_TO_INO(mp, agyes + 1, 0);
		if (flags & XFS_INOBT_WALK_SAME_AG)
			break;
	}

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

	error = xfs_iwalk_ag(iwag);
	xfs_iwalk_free(iwag);
out:
	kmem_free(iwag);
	return error;
}

/*
 * Walk all the iyesdes in the filesystem using multiple threads to process each
 * AG.
 */
int
xfs_iwalk_threaded(
	struct xfs_mount	*mp,
	xfs_iyes_t		startiyes,
	unsigned int		flags,
	xfs_iwalk_fn		iwalk_fn,
	unsigned int		iyesde_records,
	bool			polled,
	void			*data)
{
	struct xfs_pwork_ctl	pctl;
	xfs_agnumber_t		agyes = XFS_INO_TO_AGNO(mp, startiyes);
	unsigned int		nr_threads;
	int			error;

	ASSERT(agyes < mp->m_sb.sb_agcount);
	ASSERT(!(flags & ~XFS_IWALK_FLAGS_ALL));

	nr_threads = xfs_pwork_guess_datadev_parallelism(mp);
	error = xfs_pwork_init(mp, &pctl, xfs_iwalk_ag_work, "xfs_iwalk",
			nr_threads);
	if (error)
		return error;

	for (; agyes < mp->m_sb.sb_agcount; agyes++) {
		struct xfs_iwalk_ag	*iwag;

		if (xfs_pwork_ctl_want_abort(&pctl))
			break;

		iwag = kmem_zalloc(sizeof(struct xfs_iwalk_ag), 0);
		iwag->mp = mp;
		iwag->iwalk_fn = iwalk_fn;
		iwag->data = data;
		iwag->startiyes = startiyes;
		iwag->sz_recs = xfs_iwalk_prefetch(iyesde_records);
		xfs_pwork_queue(&pctl, &iwag->pwork);
		startiyes = XFS_AGINO_TO_INO(mp, agyes + 1, 0);
		if (flags & XFS_INOBT_WALK_SAME_AG)
			break;
	}

	if (polled)
		xfs_pwork_poll(&pctl);
	return xfs_pwork_destroy(&pctl);
}

/*
 * Allow callers to cache up to a page's worth of iyesbt records.  This reflects
 * the existing inumbers prefetching behavior.  Since the iyesbt walk does yest
 * itself do anything with the iyesbt records, we can set a fairly high limit
 * here.
 */
#define MAX_INOBT_WALK_PREFETCH	\
	(PAGE_SIZE / sizeof(struct xfs_iyesbt_rec_incore))

/*
 * Given the number of records that the user wanted, set the number of iyesbt
 * records that we buffer in memory.  Set the maximum if @iyesbt_records == 0.
 */
static inline unsigned int
xfs_iyesbt_walk_prefetch(
	unsigned int		iyesbt_records)
{
	/*
	 * If the caller didn't tell us the number of iyesbt records they
	 * wanted, assume the maximum prefetch possible for best performance.
	 */
	if (iyesbt_records == 0)
		iyesbt_records = MAX_INOBT_WALK_PREFETCH;

	/*
	 * Allocate eyesugh space to prefetch at least two iyesbt records so that
	 * we can cache both the record where the iwalk started and the next
	 * record.  This simplifies the AG iyesde walk loop setup code.
	 */
	iyesbt_records = max(iyesbt_records, 2U);

	/*
	 * Cap prefetch at that maximum so that we don't use an absurd amount
	 * of memory.
	 */
	return min_t(unsigned int, iyesbt_records, MAX_INOBT_WALK_PREFETCH);
}

/*
 * Walk all iyesde btree records in the filesystem starting from @startiyes.  The
 * @iyesbt_walk_fn will be called for each btree record, being passed the incore
 * record and @data.  @max_prefetch controls how many iyesbt records we try to
 * cache ahead of time.
 */
int
xfs_iyesbt_walk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_iyes_t		startiyes,
	unsigned int		flags,
	xfs_iyesbt_walk_fn	iyesbt_walk_fn,
	unsigned int		iyesbt_records,
	void			*data)
{
	struct xfs_iwalk_ag	iwag = {
		.mp		= mp,
		.tp		= tp,
		.iyesbt_walk_fn	= iyesbt_walk_fn,
		.data		= data,
		.startiyes	= startiyes,
		.sz_recs	= xfs_iyesbt_walk_prefetch(iyesbt_records),
		.pwork		= XFS_PWORK_SINGLE_THREADED,
	};
	xfs_agnumber_t		agyes = XFS_INO_TO_AGNO(mp, startiyes);
	int			error;

	ASSERT(agyes < mp->m_sb.sb_agcount);
	ASSERT(!(flags & ~XFS_INOBT_WALK_FLAGS_ALL));

	error = xfs_iwalk_alloc(&iwag);
	if (error)
		return error;

	for (; agyes < mp->m_sb.sb_agcount; agyes++) {
		error = xfs_iwalk_ag(&iwag);
		if (error)
			break;
		iwag.startiyes = XFS_AGINO_TO_INO(mp, agyes + 1, 0);
		if (flags & XFS_INOBT_WALK_SAME_AG)
			break;
	}

	xfs_iwalk_free(&iwag);
	return error;
}
