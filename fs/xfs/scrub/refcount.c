// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_refcount.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"

/*
 * Set us up to scrub reference count btrees.
 */
int
xchk_setup_ag_refcountbt(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	return xchk_setup_ag_btree(sc, ip, false);
}

/* Reference count btree scrubber. */

/*
 * Confirming Reference Counts via Reverse Mappings
 *
 * We want to count the reverse mappings overlapping a refcount record
 * (bno, len, refcount), allowing for the possibility that some of the
 * overlap may come from smaller adjoining reverse mappings, while some
 * comes from single extents which overlap the range entirely.  The
 * outer loop is as follows:
 *
 * 1. For all reverse mappings overlapping the refcount extent,
 *    a. If a given rmap completely overlaps, mark it as seen.
 *    b. Otherwise, record the fragment (in agbno order) for later
 *       processing.
 *
 * Once we've seen all the rmaps, we know that for all blocks in the
 * refcount record we want to find $refcount owners and we've already
 * visited $seen extents that overlap all the blocks.  Therefore, we
 * need to find ($refcount - $seen) owners for every block in the
 * extent; call that quantity $target_nr.  Proceed as follows:
 *
 * 2. Pull the first $target_nr fragments from the list; all of them
 *    should start at or before the start of the extent.
 *    Call this subset of fragments the working set.
 * 3. Until there are no more unprocessed fragments,
 *    a. Find the shortest fragments in the set and remove them.
 *    b. Note the block number of the end of these fragments.
 *    c. Pull the same number of fragments from the list.  All of these
 *       fragments should start at the block number recorded in the
 *       previous step.
 *    d. Put those fragments in the set.
 * 4. Check that there are $target_nr fragments remaining in the list,
 *    and that they all end at or beyond the end of the refcount extent.
 *
 * If the refcount is correct, all the check conditions in the algorithm
 * should always hold true.  If not, the refcount is incorrect.
 */
struct xchk_refcnt_frag {
	struct list_head	list;
	struct xfs_rmap_irec	rm;
};

struct xchk_refcnt_check {
	struct xfs_scrub	*sc;
	struct list_head	fragments;

	/* refcount extent we're examining */
	xfs_agblock_t		bno;
	xfs_extlen_t		len;
	xfs_nlink_t		refcount;

	/* number of owners seen */
	xfs_nlink_t		seen;
};

/*
 * Decide if the given rmap is large enough that we can redeem it
 * towards refcount verification now, or if it's a fragment, in
 * which case we'll hang onto it in the hopes that we'll later
 * discover that we've collected exactly the correct number of
 * fragments as the refcountbt says we should have.
 */
STATIC int
xchk_refcountbt_rmap_check(
	struct xfs_btree_cur		*cur,
	struct xfs_rmap_irec		*rec,
	void				*priv)
{
	struct xchk_refcnt_check	*refchk = priv;
	struct xchk_refcnt_frag		*frag;
	xfs_agblock_t			rm_last;
	xfs_agblock_t			rc_last;
	int				error = 0;

	if (xchk_should_terminate(refchk->sc, &error))
		return error;

	rm_last = rec->rm_startblock + rec->rm_blockcount - 1;
	rc_last = refchk->bno + refchk->len - 1;

	/* Confirm that a single-owner refc extent is a CoW stage. */
	if (refchk->refcount == 1 && rec->rm_owner != XFS_RMAP_OWN_COW) {
		xchk_btree_xref_set_corrupt(refchk->sc, cur, 0);
		return 0;
	}

	if (rec->rm_startblock <= refchk->bno && rm_last >= rc_last) {
		/*
		 * The rmap overlaps the refcount record, so we can confirm
		 * one refcount owner seen.
		 */
		refchk->seen++;
	} else {
		/*
		 * This rmap covers only part of the refcount record, so
		 * save the fragment for later processing.  If the rmapbt
		 * is healthy each rmap_irec we see will be in agbno order
		 * so we don't need insertion sort here.
		 */
		frag = kmem_alloc(sizeof(struct xchk_refcnt_frag),
				KM_MAYFAIL);
		if (!frag)
			return -ENOMEM;
		memcpy(&frag->rm, rec, sizeof(frag->rm));
		list_add_tail(&frag->list, &refchk->fragments);
	}

	return 0;
}

/*
 * Given a bunch of rmap fragments, iterate through them, keeping
 * a running tally of the refcount.  If this ever deviates from
 * what we expect (which is the refcountbt's refcount minus the
 * number of extents that totally covered the refcountbt extent),
 * we have a refcountbt error.
 */
STATIC void
xchk_refcountbt_process_rmap_fragments(
	struct xchk_refcnt_check	*refchk)
{
	struct list_head		worklist;
	struct xchk_refcnt_frag		*frag;
	struct xchk_refcnt_frag		*n;
	xfs_agblock_t			bno;
	xfs_agblock_t			rbno;
	xfs_agblock_t			next_rbno;
	xfs_nlink_t			nr;
	xfs_nlink_t			target_nr;

	target_nr = refchk->refcount - refchk->seen;
	if (target_nr == 0)
		return;

	/*
	 * There are (refchk->rc.rc_refcount - refchk->nr refcount)
	 * references we haven't found yet.  Pull that many off the
	 * fragment list and figure out where the smallest rmap ends
	 * (and therefore the next rmap should start).  All the rmaps
	 * we pull off should start at or before the beginning of the
	 * refcount record's range.
	 */
	INIT_LIST_HEAD(&worklist);
	rbno = NULLAGBLOCK;
	nr = 1;

	/* Make sure the fragments actually /are/ in agbno order. */
	bno = 0;
	list_for_each_entry(frag, &refchk->fragments, list) {
		if (frag->rm.rm_startblock < bno)
			goto done;
		bno = frag->rm.rm_startblock;
	}

	/*
	 * Find all the rmaps that start at or before the refc extent,
	 * and put them on the worklist.
	 */
	list_for_each_entry_safe(frag, n, &refchk->fragments, list) {
		if (frag->rm.rm_startblock > refchk->bno)
			goto done;
		bno = frag->rm.rm_startblock + frag->rm.rm_blockcount;
		if (bno < rbno)
			rbno = bno;
		list_move_tail(&frag->list, &worklist);
		if (nr == target_nr)
			break;
		nr++;
	}

	/*
	 * We should have found exactly $target_nr rmap fragments starting
	 * at or before the refcount extent.
	 */
	if (nr != target_nr)
		goto done;

	while (!list_empty(&refchk->fragments)) {
		/* Discard any fragments ending at rbno from the worklist. */
		nr = 0;
		next_rbno = NULLAGBLOCK;
		list_for_each_entry_safe(frag, n, &worklist, list) {
			bno = frag->rm.rm_startblock + frag->rm.rm_blockcount;
			if (bno != rbno) {
				if (bno < next_rbno)
					next_rbno = bno;
				continue;
			}
			list_del(&frag->list);
			kmem_free(frag);
			nr++;
		}

		/* Try to add nr rmaps starting at rbno to the worklist. */
		list_for_each_entry_safe(frag, n, &refchk->fragments, list) {
			bno = frag->rm.rm_startblock + frag->rm.rm_blockcount;
			if (frag->rm.rm_startblock != rbno)
				goto done;
			list_move_tail(&frag->list, &worklist);
			if (next_rbno > bno)
				next_rbno = bno;
			nr--;
			if (nr == 0)
				break;
		}

		/*
		 * If we get here and nr > 0, this means that we added fewer
		 * items to the worklist than we discarded because the fragment
		 * list ran out of items.  Therefore, we cannot maintain the
		 * required refcount.  Something is wrong, so we're done.
		 */
		if (nr)
			goto done;

		rbno = next_rbno;
	}

	/*
	 * Make sure the last extent we processed ends at or beyond
	 * the end of the refcount extent.
	 */
	if (rbno < refchk->bno + refchk->len)
		goto done;

	/* Actually record us having seen the remaining refcount. */
	refchk->seen = refchk->refcount;
done:
	/* Delete fragments and work list. */
	list_for_each_entry_safe(frag, n, &worklist, list) {
		list_del(&frag->list);
		kmem_free(frag);
	}
	list_for_each_entry_safe(frag, n, &refchk->fragments, list) {
		list_del(&frag->list);
		kmem_free(frag);
	}
}

/* Use the rmap entries covering this extent to verify the refcount. */
STATIC void
xchk_refcountbt_xref_rmap(
	struct xfs_scrub		*sc,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	xfs_nlink_t			refcount)
{
	struct xchk_refcnt_check	refchk = {
		.sc = sc,
		.bno = bno,
		.len = len,
		.refcount = refcount,
		.seen = 0,
	};
	struct xfs_rmap_irec		low;
	struct xfs_rmap_irec		high;
	struct xchk_refcnt_frag		*frag;
	struct xchk_refcnt_frag		*n;
	int				error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Cross-reference with the rmapbt to confirm the refcount. */
	memset(&low, 0, sizeof(low));
	low.rm_startblock = bno;
	memset(&high, 0xFF, sizeof(high));
	high.rm_startblock = bno + len - 1;

	INIT_LIST_HEAD(&refchk.fragments);
	error = xfs_rmap_query_range(sc->sa.rmap_cur, &low, &high,
			&xchk_refcountbt_rmap_check, &refchk);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		goto out_free;

	xchk_refcountbt_process_rmap_fragments(&refchk);
	if (refcount != refchk.seen)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);

out_free:
	list_for_each_entry_safe(frag, n, &refchk.fragments, list) {
		list_del(&frag->list);
		kmem_free(frag);
	}
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_refcountbt_xref(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len,
	xfs_nlink_t		refcount)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_space(sc, agbno, len);
	xchk_xref_is_not_inode_chunk(sc, agbno, len);
	xchk_refcountbt_xref_rmap(sc, agbno, len, refcount);
}

/* Scrub a refcountbt record. */
STATIC int
xchk_refcountbt_rec(
	struct xchk_btree	*bs,
	union xfs_btree_rec	*rec)
{
	struct xfs_mount	*mp = bs->cur->bc_mp;
	xfs_agblock_t		*cow_blocks = bs->private;
	xfs_agnumber_t		agno = bs->cur->bc_ag.agno;
	xfs_agblock_t		bno;
	xfs_extlen_t		len;
	xfs_nlink_t		refcount;
	bool			has_cowflag;

	bno = be32_to_cpu(rec->refc.rc_startblock);
	len = be32_to_cpu(rec->refc.rc_blockcount);
	refcount = be32_to_cpu(rec->refc.rc_refcount);

	/* Only CoW records can have refcount == 1. */
	has_cowflag = (bno & XFS_REFC_COW_START);
	if ((refcount == 1 && !has_cowflag) || (refcount != 1 && has_cowflag))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	if (has_cowflag)
		(*cow_blocks) += len;

	/* Check the extent. */
	bno &= ~XFS_REFC_COW_START;
	if (bno + len <= bno ||
	    !xfs_verify_agbno(mp, agno, bno) ||
	    !xfs_verify_agbno(mp, agno, bno + len - 1))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (refcount == 0)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	xchk_refcountbt_xref(bs->sc, bno, len, refcount);

	return 0;
}

/* Make sure we have as many refc blocks as the rmap says. */
STATIC void
xchk_refcount_xref_rmap(
	struct xfs_scrub	*sc,
	xfs_filblks_t		cow_blocks)
{
	xfs_extlen_t		refcbt_blocks = 0;
	xfs_filblks_t		blocks;
	int			error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many refcbt blocks as the rmap knows about. */
	error = xfs_btree_count_blocks(sc->sa.refc_cur, &refcbt_blocks);
	if (!xchk_btree_process_error(sc, sc->sa.refc_cur, 0, &error))
		return;
	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_REFC, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (blocks != refcbt_blocks)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);

	/* Check that we saw as many cow blocks as the rmap knows about. */
	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_COW, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (blocks != cow_blocks)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}

/* Scrub the refcount btree for some AG. */
int
xchk_refcountbt(
	struct xfs_scrub	*sc)
{
	xfs_agblock_t		cow_blocks = 0;
	int			error;

	error = xchk_btree(sc, sc->sa.refc_cur, xchk_refcountbt_rec,
			&XFS_RMAP_OINFO_REFC, &cow_blocks);
	if (error)
		return error;

	xchk_refcount_xref_rmap(sc, cow_blocks);

	return 0;
}

/* xref check that a cow staging extent is marked in the refcountbt. */
void
xchk_xref_is_cow_staging(
	struct xfs_scrub		*sc,
	xfs_agblock_t			agbno,
	xfs_extlen_t			len)
{
	struct xfs_refcount_irec	rc;
	bool				has_cowflag;
	int				has_refcount;
	int				error;

	if (!sc->sa.refc_cur || xchk_skip_xref(sc->sm))
		return;

	/* Find the CoW staging extent. */
	error = xfs_refcount_lookup_le(sc->sa.refc_cur,
			agbno + XFS_REFC_COW_START, &has_refcount);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.refc_cur))
		return;
	if (!has_refcount) {
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);
		return;
	}

	error = xfs_refcount_get_rec(sc->sa.refc_cur, &rc, &has_refcount);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.refc_cur))
		return;
	if (!has_refcount) {
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);
		return;
	}

	/* CoW flag must be set, refcount must be 1. */
	has_cowflag = (rc.rc_startblock & XFS_REFC_COW_START);
	if (!has_cowflag || rc.rc_refcount != 1)
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);

	/* Must be at least as long as what was passed in */
	if (rc.rc_blockcount < len)
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);
}

/*
 * xref check that the extent is not shared.  Only file data blocks
 * can have multiple owners.
 */
void
xchk_xref_is_not_shared(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	bool			shared;
	int			error;

	if (!sc->sa.refc_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_refcount_has_record(sc->sa.refc_cur, agbno, len, &shared);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.refc_cur))
		return;
	if (shared)
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);
}
