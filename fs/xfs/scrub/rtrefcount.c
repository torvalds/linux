// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs_platform.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_refcount.h"
#include "xfs_inode.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtgroup.h"
#include "xfs_metafile.h"
#include "xfs_rtrefcount_btree.h"
#include "xfs_rtalloc.h"
#include "xfs_ag.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/repair.h"
#include "scrub/refcount.h"

/* Set us up with the realtime refcount metadata locked. */
int
xchk_setup_rtrefcountbt(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	if (xchk_could_repair(sc)) {
		error = xrep_setup_rtrefcountbt(sc);
		if (error)
			return error;
	}

	error = xchk_rtgroup_init(sc, sc->sm->sm_agno);
	if (error)
		return error;

	error = xchk_setup_rt(sc);
	if (error)
		return error;

	error = xchk_install_live_inode(sc, rtg_refcount(sc->sr.rtg));
	if (error)
		return error;

	return xchk_rtgroup_lock(sc, XCHK_RTGLOCK_ALL);
}

/* Realtime Reference count btree scrubber. */

/* Use the rmap entries covering this extent to verify the refcount. */
STATIC void
xchk_rtrefcountbt_xref_rmap(
	struct xfs_scrub		*sc,
	const struct xfs_refcount_irec	*irec)
{
	struct xchk_refcnt_check	refchk = {
		.sc			= sc,
		.bno			= irec->rc_startblock,
		.len			= irec->rc_blockcount,
		.refcount		= irec->rc_refcount,
		.seen			= 0,
	};
	struct xfs_rmap_irec		low;
	struct xfs_rmap_irec		high;
	struct xchk_refcnt_frag		*frag;
	struct xchk_refcnt_frag		*n;
	int				error;

	if (!sc->sr.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Cross-reference with the rmapbt to confirm the refcount. */
	memset(&low, 0, sizeof(low));
	low.rm_startblock = irec->rc_startblock;
	memset(&high, 0xFF, sizeof(high));
	high.rm_startblock = irec->rc_startblock + irec->rc_blockcount - 1;

	INIT_LIST_HEAD(&refchk.fragments);
	error = xfs_rmap_query_range(sc->sr.rmap_cur, &low, &high,
			xchk_refcountbt_rmap_check, &refchk);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.rmap_cur))
		goto out_free;

	xchk_refcountbt_process_rmap_fragments(&refchk);
	if (irec->rc_refcount != refchk.seen)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);

out_free:
	list_for_each_entry_safe(frag, n, &refchk.fragments, list) {
		list_del(&frag->list);
		kfree(frag);
	}
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_rtrefcountbt_xref(
	struct xfs_scrub		*sc,
	const struct xfs_refcount_irec	*irec)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_rt_space(sc,
			xfs_rgbno_to_rtb(sc->sr.rtg, irec->rc_startblock),
			irec->rc_blockcount);
	xchk_rtrefcountbt_xref_rmap(sc, irec);
}

struct xchk_rtrefcbt_records {
	/* Previous refcount record. */
	struct xfs_refcount_irec	prev_rec;

	/* The next rtgroup block where we aren't expecting shared extents. */
	xfs_rgblock_t			next_unshared_rgbno;

	/* Number of CoW blocks we expect. */
	xfs_extlen_t			cow_blocks;

	/* Was the last record a shared or CoW staging extent? */
	enum xfs_refc_domain		prev_domain;
};

static inline bool
xchk_rtrefcount_mergeable(
	struct xchk_rtrefcbt_records	*rrc,
	const struct xfs_refcount_irec	*r2)
{
	const struct xfs_refcount_irec	*r1 = &rrc->prev_rec;

	/* Ignore if prev_rec is not yet initialized. */
	if (r1->rc_blockcount == 0)
		return false;

	if (r1->rc_domain != r2->rc_domain)
		return false;
	if (r1->rc_startblock + r1->rc_blockcount != r2->rc_startblock)
		return false;
	if (r1->rc_refcount != r2->rc_refcount)
		return false;
	if ((unsigned long long)r1->rc_blockcount + r2->rc_blockcount >
			XFS_REFC_LEN_MAX)
		return false;

	return true;
}

/* Flag failures for records that could be merged. */
STATIC void
xchk_rtrefcountbt_check_mergeable(
	struct xchk_btree		*bs,
	struct xchk_rtrefcbt_records	*rrc,
	const struct xfs_refcount_irec	*irec)
{
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	if (xchk_rtrefcount_mergeable(rrc, irec))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	memcpy(&rrc->prev_rec, irec, sizeof(struct xfs_refcount_irec));
}

/*
 * Make sure that a gap in the reference count records does not correspond to
 * overlapping records (i.e. shared extents) in the reverse mappings.
 */
static inline void
xchk_rtrefcountbt_xref_gaps(
	struct xfs_scrub	*sc,
	struct xchk_rtrefcbt_records *rrc,
	xfs_rgblock_t		bno)
{
	struct xfs_rmap_irec	low;
	struct xfs_rmap_irec	high;
	xfs_rgblock_t		next_bno = NULLRGBLOCK;
	int			error;

	if (bno <= rrc->next_unshared_rgbno || !sc->sr.rmap_cur ||
            xchk_skip_xref(sc->sm))
		return;

	memset(&low, 0, sizeof(low));
	low.rm_startblock = rrc->next_unshared_rgbno;
	memset(&high, 0xFF, sizeof(high));
	high.rm_startblock = bno - 1;

	error = xfs_rmap_query_range(sc->sr.rmap_cur, &low, &high,
			xchk_refcountbt_rmap_check_gap, &next_bno);
	if (error == -ECANCELED)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
	else
		xchk_should_check_xref(sc, &error, &sc->sr.rmap_cur);
}

/* Scrub a rtrefcountbt record. */
STATIC int
xchk_rtrefcountbt_rec(
	struct xchk_btree		*bs,
	const union xfs_btree_rec	*rec)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xchk_rtrefcbt_records	*rrc = bs->private;
	struct xfs_refcount_irec	irec;
	u32				mod;

	xfs_refcount_btrec_to_irec(rec, &irec);
	if (xfs_rtrefcount_check_irec(to_rtg(bs->cur->bc_group), &irec) !=
			NULL) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	/* We can only share full rt extents. */
	mod = xfs_rgbno_to_rtxoff(mp, irec.rc_startblock);
	if (mod)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	mod = xfs_extlen_to_rtxmod(mp, irec.rc_blockcount);
	if (mod)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (irec.rc_domain == XFS_REFC_DOMAIN_COW)
		rrc->cow_blocks += irec.rc_blockcount;

	/* Shared records always come before CoW records. */
	if (irec.rc_domain == XFS_REFC_DOMAIN_SHARED &&
	    rrc->prev_domain == XFS_REFC_DOMAIN_COW)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	rrc->prev_domain = irec.rc_domain;

	xchk_rtrefcountbt_check_mergeable(bs, rrc, &irec);
	xchk_rtrefcountbt_xref(bs->sc, &irec);

	/*
	 * If this is a record for a shared extent, check that all blocks
	 * between the previous record and this one have at most one reverse
	 * mapping.
	 */
	if (irec.rc_domain == XFS_REFC_DOMAIN_SHARED) {
		xchk_rtrefcountbt_xref_gaps(bs->sc, rrc, irec.rc_startblock);
		rrc->next_unshared_rgbno = irec.rc_startblock +
					   irec.rc_blockcount;
	}

	return 0;
}

/* Count the number of blocks used by the rtrefcount btree file in this AG. */
static int
xchk_rtrefcount_count_agblocks(
	struct xfs_scrub	*sc,
	xfs_agnumber_t		agno,
	const struct xfs_owner_info *btree_oinfo,
	xfs_filblks_t		*blocks)
{
	xfs_filblks_t		agblocks = 0;
	int			error;

	error = xchk_ag_init_existing(sc, agno);
	if (error)
		goto out_free;

	/*
	 * If we don't have an rmap cursor, we can't complete the cross
	 * referencing, so return EFSCORRUPTED to end the loop and trigger the
	 * XFAIL flag.
	 */
	if (!sc->sa.rmap_cur) {
		error = -EFSCORRUPTED;
		goto out_free;
	}

	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur, btree_oinfo,
			&agblocks);
	if (error)
		goto out_free;

	*blocks += agblocks;
out_free:
	xchk_ag_free(sc);
	return error;
}

/* Make sure we have as many refc blocks as the rmap says. */
STATIC void
xchk_rtrefcount_xref_rmap(
	struct xfs_scrub	*sc,
	const struct xfs_owner_info *btree_oinfo,
	xfs_extlen_t		cow_blocks)
{
	xfs_filblks_t		refcbt_blocks = 0;
	xfs_filblks_t		blocks = 1; /* one for the iroot */
	xfs_agnumber_t		agno;
	int			error = 0;

	if (!xfs_has_rmapbt(sc->mp) || xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many refcbt blocks as the rmap knows about. */
	error = xfs_btree_count_blocks(sc->sr.refc_cur, &refcbt_blocks);
	if (!xchk_btree_process_error(sc, sc->sr.refc_cur, 0, &error))
		return;

	for (agno = 0; agno < sc->mp->m_sb.sb_agcount; agno++) {
		error = xchk_rtrefcount_count_agblocks(sc, agno, btree_oinfo,
				&blocks);
		if (error)
			break;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		return;
	if (blocks != refcbt_blocks)
		xchk_fblock_xref_set_corrupt(sc, XFS_DATA_FORK, 0);

	if (!sc->sr.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many cow blocks as the rmap knows about. */
	error = xchk_count_rmap_ownedby_ag(sc, sc->sr.rmap_cur,
			&XFS_RMAP_OINFO_COW, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.rmap_cur))
		return;
	if (blocks != cow_blocks)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
}

/* Scrub the refcount btree for some rtgroup. */
int
xchk_rtrefcountbt(
	struct xfs_scrub	*sc)
{
	struct xfs_owner_info	btree_oinfo;
	struct xchk_rtrefcbt_records rrc = {
		.cow_blocks		= 0,
		.next_unshared_rgbno	= 0,
		.prev_domain		= XFS_REFC_DOMAIN_SHARED,
	};
	int			error;

	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	xfs_rmap_inode_bmbt_owner(&btree_oinfo, rtg_refcount(sc->sr.rtg),
			XFS_DATA_FORK);
	error = xchk_btree(sc, sc->sr.refc_cur, xchk_rtrefcountbt_rec,
			&btree_oinfo, &rrc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	/*
	 * Check that all blocks between the last refcount > 1 record and the
	 * end of the rtgroup have at most one reverse mapping.
	 */
	xchk_rtrefcountbt_xref_gaps(sc, &rrc,
			xfs_rtx_to_rgbno(sc->sr.rtg, sc->mp->m_sb.sb_rgextents));
	xchk_rtrefcount_xref_rmap(sc, &btree_oinfo, rrc.cow_blocks);

	return 0;
}

/* xref check that a cow staging extent is marked in the rtrefcountbt. */
void
xchk_xref_is_rt_cow_staging(
	struct xfs_scrub		*sc,
	xfs_rgblock_t			bno,
	xfs_extlen_t			len)
{
	struct xfs_refcount_irec	rc;
	int				has_refcount;
	int				error;

	if (!sc->sr.refc_cur || xchk_skip_xref(sc->sm))
		return;

	/* Find the CoW staging extent. */
	error = xfs_refcount_lookup_le(sc->sr.refc_cur, XFS_REFC_DOMAIN_COW,
			bno, &has_refcount);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.refc_cur))
		return;
	if (!has_refcount) {
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);
		return;
	}

	error = xfs_refcount_get_rec(sc->sr.refc_cur, &rc, &has_refcount);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.refc_cur))
		return;
	if (!has_refcount) {
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);
		return;
	}

	/* CoW lookup returned a shared extent record? */
	if (rc.rc_domain != XFS_REFC_DOMAIN_COW)
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);

	/* Can't start after bno */
	if (rc.rc_startblock > bno)
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);

	/* Must be at least as long as what was passed in */
	if (rc.rc_startblock + rc.rc_blockcount < bno + len)
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);
}

/*
 * xref check that the extent is not shared.  Only file data blocks
 * can have multiple owners.
 */
void
xchk_xref_is_not_rt_shared(
	struct xfs_scrub	*sc,
	xfs_rgblock_t		bno,
	xfs_extlen_t		len)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!sc->sr.refc_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_refcount_has_records(sc->sr.refc_cur,
			XFS_REFC_DOMAIN_SHARED, bno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.refc_cur))
		return;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);
}

/* xref check that the extent is not being used for CoW staging. */
void
xchk_xref_is_not_rt_cow_staging(
	struct xfs_scrub	*sc,
	xfs_rgblock_t		bno,
	xfs_extlen_t		len)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!sc->sr.refc_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_refcount_has_records(sc->sr.refc_cur, XFS_REFC_DOMAIN_COW,
			bno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.refc_cur))
		return;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);
}
