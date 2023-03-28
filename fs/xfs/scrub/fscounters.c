// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_health.h"
#include "xfs_btree.h"
#include "xfs_ag.h"
#include "xfs_rtalloc.h"
#include "xfs_inode.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"

/*
 * FS Summary Counters
 * ===================
 *
 * The basics of filesystem summary counter checking are that we iterate the
 * AGs counting the number of free blocks, free space btree blocks, per-AG
 * reservations, inodes, delayed allocation reservations, and free inodes.
 * Then we compare what we computed against the in-core counters.
 *
 * However, the reality is that summary counters are a tricky beast to check.
 * While we /could/ freeze the filesystem and scramble around the AGs counting
 * the free blocks, in practice we prefer not do that for a scan because
 * freezing is costly.  To get around this, we added a per-cpu counter of the
 * delalloc reservations so that we can rotor around the AGs relatively
 * quickly, and we allow the counts to be slightly off because we're not taking
 * any locks while we do this.
 *
 * So the first thing we do is warm up the buffer cache in the setup routine by
 * walking all the AGs to make sure the incore per-AG structure has been
 * initialized.  The expected value calculation then iterates the incore per-AG
 * structures as quickly as it can.  We snapshot the percpu counters before and
 * after this operation and use the difference in counter values to guess at
 * our tolerance for mismatch between expected and actual counter values.
 */

struct xchk_fscounters {
	struct xfs_scrub	*sc;
	uint64_t		icount;
	uint64_t		ifree;
	uint64_t		fdblocks;
	uint64_t		frextents;
	unsigned long long	icount_min;
	unsigned long long	icount_max;
};

/*
 * Since the expected value computation is lockless but only browses incore
 * values, the percpu counters should be fairly close to each other.  However,
 * we'll allow ourselves to be off by at least this (arbitrary) amount.
 */
#define XCHK_FSCOUNT_MIN_VARIANCE	(512)

/*
 * Make sure the per-AG structure has been initialized from the on-disk header
 * contents and trust that the incore counters match the ondisk counters.  (The
 * AGF and AGI scrubbers check them, and a normal xfs_scrub run checks the
 * summary counters after checking all AG headers).  Do this from the setup
 * function so that the inner AG aggregation loop runs as quickly as possible.
 *
 * This function runs during the setup phase /before/ we start checking any
 * metadata.
 */
STATIC int
xchk_fscount_warmup(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*agi_bp = NULL;
	struct xfs_buf		*agf_bp = NULL;
	struct xfs_perag	*pag = NULL;
	xfs_agnumber_t		agno;
	int			error = 0;

	for_each_perag(mp, agno, pag) {
		if (xchk_should_terminate(sc, &error))
			break;
		if (xfs_perag_initialised_agi(pag) &&
		    xfs_perag_initialised_agf(pag))
			continue;

		/* Lock both AG headers. */
		error = xfs_ialloc_read_agi(pag, sc->tp, &agi_bp);
		if (error)
			break;
		error = xfs_alloc_read_agf(pag, sc->tp, 0, &agf_bp);
		if (error)
			break;

		/*
		 * These are supposed to be initialized by the header read
		 * function.
		 */
		if (!xfs_perag_initialised_agi(pag) ||
		    !xfs_perag_initialised_agf(pag)) {
			error = -EFSCORRUPTED;
			break;
		}

		xfs_buf_relse(agf_bp);
		agf_bp = NULL;
		xfs_buf_relse(agi_bp);
		agi_bp = NULL;
	}

	if (agf_bp)
		xfs_buf_relse(agf_bp);
	if (agi_bp)
		xfs_buf_relse(agi_bp);
	if (pag)
		xfs_perag_rele(pag);
	return error;
}

int
xchk_setup_fscounters(
	struct xfs_scrub	*sc)
{
	struct xchk_fscounters	*fsc;
	int			error;

	sc->buf = kzalloc(sizeof(struct xchk_fscounters), XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -ENOMEM;
	fsc = sc->buf;
	fsc->sc = sc;

	xfs_icount_range(sc->mp, &fsc->icount_min, &fsc->icount_max);

	/* We must get the incore counters set up before we can proceed. */
	error = xchk_fscount_warmup(sc);
	if (error)
		return error;

	/*
	 * Pause background reclaim while we're scrubbing to reduce the
	 * likelihood of background perturbations to the counters throwing off
	 * our calculations.
	 */
	xchk_stop_reaping(sc);

	return xchk_trans_alloc(sc, 0);
}

/*
 * Part 1: Collecting filesystem summary counts.  For each AG, we add its
 * summary counts (total inodes, free inodes, free data blocks) to an incore
 * copy of the overall filesystem summary counts.
 *
 * To avoid false corruption reports in part 2, any failure in this part must
 * set the INCOMPLETE flag even when a negative errno is returned.  This care
 * must be taken with certain errno values (i.e. EFSBADCRC, EFSCORRUPTED,
 * ECANCELED) that are absorbed into a scrub state flag update by
 * xchk_*_process_error.
 */

/* Count free space btree blocks manually for pre-lazysbcount filesystems. */
static int
xchk_fscount_btreeblks(
	struct xfs_scrub	*sc,
	struct xchk_fscounters	*fsc,
	xfs_agnumber_t		agno)
{
	xfs_extlen_t		blocks;
	int			error;

	error = xchk_ag_init_existing(sc, agno, &sc->sa);
	if (error)
		goto out_free;

	error = xfs_btree_count_blocks(sc->sa.bno_cur, &blocks);
	if (error)
		goto out_free;
	fsc->fdblocks += blocks - 1;

	error = xfs_btree_count_blocks(sc->sa.cnt_cur, &blocks);
	if (error)
		goto out_free;
	fsc->fdblocks += blocks - 1;

out_free:
	xchk_ag_free(sc, &sc->sa);
	return error;
}

/*
 * Calculate what the global in-core counters ought to be from the incore
 * per-AG structure.  Callers can compare this to the actual in-core counters
 * to estimate by how much both in-core and on-disk counters need to be
 * adjusted.
 */
STATIC int
xchk_fscount_aggregate_agcounts(
	struct xfs_scrub	*sc,
	struct xchk_fscounters	*fsc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_perag	*pag;
	uint64_t		delayed;
	xfs_agnumber_t		agno;
	int			tries = 8;
	int			error = 0;

retry:
	fsc->icount = 0;
	fsc->ifree = 0;
	fsc->fdblocks = 0;

	for_each_perag(mp, agno, pag) {
		if (xchk_should_terminate(sc, &error))
			break;

		/* This somehow got unset since the warmup? */
		if (!xfs_perag_initialised_agi(pag) ||
		    !xfs_perag_initialised_agf(pag)) {
			error = -EFSCORRUPTED;
			break;
		}

		/* Count all the inodes */
		fsc->icount += pag->pagi_count;
		fsc->ifree += pag->pagi_freecount;

		/* Add up the free/freelist/bnobt/cntbt blocks */
		fsc->fdblocks += pag->pagf_freeblks;
		fsc->fdblocks += pag->pagf_flcount;
		if (xfs_has_lazysbcount(sc->mp)) {
			fsc->fdblocks += pag->pagf_btreeblks;
		} else {
			error = xchk_fscount_btreeblks(sc, fsc, agno);
			if (error)
				break;
		}

		/*
		 * Per-AG reservations are taken out of the incore counters,
		 * so they must be left out of the free blocks computation.
		 */
		fsc->fdblocks -= pag->pag_meta_resv.ar_reserved;
		fsc->fdblocks -= pag->pag_rmapbt_resv.ar_orig_reserved;

	}
	if (pag)
		xfs_perag_rele(pag);
	if (error) {
		xchk_set_incomplete(sc);
		return error;
	}

	/*
	 * The global incore space reservation is taken from the incore
	 * counters, so leave that out of the computation.
	 */
	fsc->fdblocks -= mp->m_resblks_avail;

	/*
	 * Delayed allocation reservations are taken out of the incore counters
	 * but not recorded on disk, so leave them and their indlen blocks out
	 * of the computation.
	 */
	delayed = percpu_counter_sum(&mp->m_delalloc_blks);
	fsc->fdblocks -= delayed;

	trace_xchk_fscounters_calc(mp, fsc->icount, fsc->ifree, fsc->fdblocks,
			delayed);


	/* Bail out if the values we compute are totally nonsense. */
	if (fsc->icount < fsc->icount_min || fsc->icount > fsc->icount_max ||
	    fsc->fdblocks > mp->m_sb.sb_dblocks ||
	    fsc->ifree > fsc->icount_max)
		return -EFSCORRUPTED;

	/*
	 * If ifree > icount then we probably had some perturbation in the
	 * counters while we were calculating things.  We'll try a few times
	 * to maintain ifree <= icount before giving up.
	 */
	if (fsc->ifree > fsc->icount) {
		if (tries--)
			goto retry;
		xchk_set_incomplete(sc);
		return 0;
	}

	return 0;
}

#ifdef CONFIG_XFS_RT
STATIC int
xchk_fscount_add_frextent(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*rec,
	void				*priv)
{
	struct xchk_fscounters		*fsc = priv;
	int				error = 0;

	fsc->frextents += rec->ar_extcount;

	xchk_should_terminate(fsc->sc, &error);
	return error;
}

/* Calculate the number of free realtime extents from the realtime bitmap. */
STATIC int
xchk_fscount_count_frextents(
	struct xfs_scrub	*sc,
	struct xchk_fscounters	*fsc)
{
	struct xfs_mount	*mp = sc->mp;
	int			error;

	fsc->frextents = 0;
	if (!xfs_has_realtime(mp))
		return 0;

	xfs_ilock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	error = xfs_rtalloc_query_all(sc->mp, sc->tp,
			xchk_fscount_add_frextent, fsc);
	if (error) {
		xchk_set_incomplete(sc);
		goto out_unlock;
	}

out_unlock:
	xfs_iunlock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	return error;
}
#else
STATIC int
xchk_fscount_count_frextents(
	struct xfs_scrub	*sc,
	struct xchk_fscounters	*fsc)
{
	fsc->frextents = 0;
	return 0;
}
#endif /* CONFIG_XFS_RT */

/*
 * Part 2: Comparing filesystem summary counters.  All we have to do here is
 * sum the percpu counters and compare them to what we've observed.
 */

/*
 * Is the @counter reasonably close to the @expected value?
 *
 * We neither locked nor froze anything in the filesystem while aggregating the
 * per-AG data to compute the @expected value, which means that the counter
 * could have changed.  We know the @old_value of the summation of the counter
 * before the aggregation, and we re-sum the counter now.  If the expected
 * value falls between the two summations, we're ok.
 *
 * Otherwise, we /might/ have a problem.  If the change in the summations is
 * more than we want to tolerate, the filesystem is probably busy and we should
 * just send back INCOMPLETE and see if userspace will try again.
 */
static inline bool
xchk_fscount_within_range(
	struct xfs_scrub	*sc,
	const int64_t		old_value,
	struct percpu_counter	*counter,
	uint64_t		expected)
{
	int64_t			min_value, max_value;
	int64_t			curr_value = percpu_counter_sum(counter);

	trace_xchk_fscounters_within_range(sc->mp, expected, curr_value,
			old_value);

	/* Negative values are always wrong. */
	if (curr_value < 0)
		return false;

	/* Exact matches are always ok. */
	if (curr_value == expected)
		return true;

	min_value = min(old_value, curr_value);
	max_value = max(old_value, curr_value);

	/* Within the before-and-after range is ok. */
	if (expected >= min_value && expected <= max_value)
		return true;

	/*
	 * If the difference between the two summations is too large, the fs
	 * might just be busy and so we'll mark the scrub incomplete.  Return
	 * true here so that we don't mark the counter corrupt.
	 *
	 * XXX: In the future when userspace can grant scrub permission to
	 * quiesce the filesystem to solve the outsized variance problem, this
	 * check should be moved up and the return code changed to signal to
	 * userspace that we need quiesce permission.
	 */
	if (max_value - min_value >= XCHK_FSCOUNT_MIN_VARIANCE) {
		xchk_set_incomplete(sc);
		return true;
	}

	return false;
}

/* Check the superblock counters. */
int
xchk_fscounters(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xchk_fscounters	*fsc = sc->buf;
	int64_t			icount, ifree, fdblocks, frextents;
	int			error;

	/* Snapshot the percpu counters. */
	icount = percpu_counter_sum(&mp->m_icount);
	ifree = percpu_counter_sum(&mp->m_ifree);
	fdblocks = percpu_counter_sum(&mp->m_fdblocks);
	frextents = percpu_counter_sum(&mp->m_frextents);

	/* No negative values, please! */
	if (icount < 0 || ifree < 0 || fdblocks < 0 || frextents < 0)
		xchk_set_corrupt(sc);

	/* See if icount is obviously wrong. */
	if (icount < fsc->icount_min || icount > fsc->icount_max)
		xchk_set_corrupt(sc);

	/* See if fdblocks is obviously wrong. */
	if (fdblocks > mp->m_sb.sb_dblocks)
		xchk_set_corrupt(sc);

	/* See if frextents is obviously wrong. */
	if (frextents > mp->m_sb.sb_rextents)
		xchk_set_corrupt(sc);

	/*
	 * If ifree exceeds icount by more than the minimum variance then
	 * something's probably wrong with the counters.
	 */
	if (ifree > icount && ifree - icount > XCHK_FSCOUNT_MIN_VARIANCE)
		xchk_set_corrupt(sc);

	/* Walk the incore AG headers to calculate the expected counters. */
	error = xchk_fscount_aggregate_agcounts(sc, fsc);
	if (!xchk_process_error(sc, 0, XFS_SB_BLOCK(mp), &error))
		return error;
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE)
		return 0;

	/* Count the free extents counter for rt volumes. */
	error = xchk_fscount_count_frextents(sc, fsc);
	if (!xchk_process_error(sc, 0, XFS_SB_BLOCK(mp), &error))
		return error;
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE)
		return 0;

	/* Compare the in-core counters with whatever we counted. */
	if (!xchk_fscount_within_range(sc, icount, &mp->m_icount, fsc->icount))
		xchk_set_corrupt(sc);

	if (!xchk_fscount_within_range(sc, ifree, &mp->m_ifree, fsc->ifree))
		xchk_set_corrupt(sc);

	if (!xchk_fscount_within_range(sc, fdblocks, &mp->m_fdblocks,
			fsc->fdblocks))
		xchk_set_corrupt(sc);

	if (!xchk_fscount_within_range(sc, frextents, &mp->m_frextents,
			fsc->frextents))
		xchk_set_corrupt(sc);

	return 0;
}
