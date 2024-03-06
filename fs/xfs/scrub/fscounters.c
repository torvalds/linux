// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_health.h"
#include "xfs_btree.h"
#include "xfs_ag.h"
#include "xfs_rtbitmap.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
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
	bool			frozen;
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

static inline int
xchk_fsfreeze(
	struct xfs_scrub	*sc)
{
	int			error;

	error = freeze_super(sc->mp->m_super, FREEZE_HOLDER_KERNEL);
	trace_xchk_fsfreeze(sc, error);
	return error;
}

static inline int
xchk_fsthaw(
	struct xfs_scrub	*sc)
{
	int			error;

	/* This should always succeed, we have a kernel freeze */
	error = thaw_super(sc->mp->m_super, FREEZE_HOLDER_KERNEL);
	trace_xchk_fsthaw(sc, error);
	return error;
}

/*
 * We couldn't stabilize the filesystem long enough to sample all the variables
 * that comprise the summary counters and compare them to the percpu counters.
 * We need to disable all writer threads, which means taking the first two
 * freeze levels to put userspace to sleep, and the third freeze level to
 * prevent background threads from starting new transactions.  Take one level
 * more to prevent other callers from unfreezing the filesystem while we run.
 */
STATIC int
xchk_fscounters_freeze(
	struct xfs_scrub	*sc)
{
	struct xchk_fscounters	*fsc = sc->buf;
	int			error = 0;

	if (sc->flags & XCHK_HAVE_FREEZE_PROT) {
		sc->flags &= ~XCHK_HAVE_FREEZE_PROT;
		mnt_drop_write_file(sc->file);
	}

	/* Try to grab a kernel freeze. */
	while ((error = xchk_fsfreeze(sc)) == -EBUSY) {
		if (xchk_should_terminate(sc, &error))
			return error;

		delay(HZ / 10);
	}
	if (error)
		return error;

	fsc->frozen = true;
	return 0;
}

/* Thaw the filesystem after checking or repairing fscounters. */
STATIC void
xchk_fscounters_cleanup(
	void			*buf)
{
	struct xchk_fscounters	*fsc = buf;
	struct xfs_scrub	*sc = fsc->sc;
	int			error;

	if (!fsc->frozen)
		return;

	error = xchk_fsthaw(sc);
	if (error)
		xfs_emerg(sc->mp, "still frozen after scrub, err=%d", error);
	else
		fsc->frozen = false;
}

int
xchk_setup_fscounters(
	struct xfs_scrub	*sc)
{
	struct xchk_fscounters	*fsc;
	int			error;

	/*
	 * If the AGF doesn't track btreeblks, we have to lock the AGF to count
	 * btree block usage by walking the actual btrees.
	 */
	if (!xfs_has_lazysbcount(sc->mp))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	sc->buf = kzalloc(sizeof(struct xchk_fscounters), XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -ENOMEM;
	sc->buf_cleanup = xchk_fscounters_cleanup;
	fsc = sc->buf;
	fsc->sc = sc;

	xfs_icount_range(sc->mp, &fsc->icount_min, &fsc->icount_max);

	/* We must get the incore counters set up before we can proceed. */
	error = xchk_fscount_warmup(sc);
	if (error)
		return error;

	/*
	 * Pause all writer activity in the filesystem while we're scrubbing to
	 * reduce the likelihood of background perturbations to the counters
	 * throwing off our calculations.
	 */
	if (sc->flags & XCHK_TRY_HARDER) {
		error = xchk_fscounters_freeze(sc);
		if (error)
			return error;
	}

	return xfs_trans_alloc_empty(sc->mp, &sc->tp);
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
		return -EDEADLOCK;
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
 *
 * If we're repairing then we require an exact match.
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

	/* Everything else is bad. */
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
	bool			try_again = false;
	int			error;

	/* Snapshot the percpu counters. */
	icount = percpu_counter_sum(&mp->m_icount);
	ifree = percpu_counter_sum(&mp->m_ifree);
	fdblocks = percpu_counter_sum(&mp->m_fdblocks);
	frextents = percpu_counter_sum(&mp->m_frextents);

	/* No negative values, please! */
	if (icount < 0 || ifree < 0)
		xchk_set_corrupt(sc);

	/*
	 * If the filesystem is not frozen, the counter summation calls above
	 * can race with xfs_mod_freecounter, which subtracts a requested space
	 * reservation from the counter and undoes the subtraction if that made
	 * the counter go negative.  Therefore, it's possible to see negative
	 * values here, and we should only flag that as a corruption if we
	 * froze the fs.  This is much more likely to happen with frextents
	 * since there are no reserved pools.
	 */
	if (fdblocks < 0 || frextents < 0) {
		if (!fsc->frozen)
			return -EDEADLOCK;

		xchk_set_corrupt(sc);
		return 0;
	}

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

	/* Count the free extents counter for rt volumes. */
	error = xchk_fscount_count_frextents(sc, fsc);
	if (!xchk_process_error(sc, 0, XFS_SB_BLOCK(mp), &error))
		return error;
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE)
		return 0;

	/*
	 * Compare the in-core counters with whatever we counted.  If the fs is
	 * frozen, we treat the discrepancy as a corruption because the freeze
	 * should have stabilized the counter values.  Otherwise, we need
	 * userspace to call us back having granted us freeze permission.
	 */
	if (!xchk_fscount_within_range(sc, icount, &mp->m_icount,
				fsc->icount)) {
		if (fsc->frozen)
			xchk_set_corrupt(sc);
		else
			try_again = true;
	}

	if (!xchk_fscount_within_range(sc, ifree, &mp->m_ifree, fsc->ifree)) {
		if (fsc->frozen)
			xchk_set_corrupt(sc);
		else
			try_again = true;
	}

	if (!xchk_fscount_within_range(sc, fdblocks, &mp->m_fdblocks,
			fsc->fdblocks)) {
		if (fsc->frozen)
			xchk_set_corrupt(sc);
		else
			try_again = true;
	}

	if (!xchk_fscount_within_range(sc, frextents, &mp->m_frextents,
			fsc->frextents)) {
		if (fsc->frozen)
			xchk_set_corrupt(sc);
		else
			try_again = true;
	}

	if (try_again)
		return -EDEADLOCK;

	return 0;
}
