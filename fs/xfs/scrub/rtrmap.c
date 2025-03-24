// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_inode.h"
#include "xfs_rtalloc.h"
#include "xfs_rtgroup.h"
#include "xfs_metafile.h"
#include "xfs_refcount.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"

/* Set us up with the realtime metadata locked. */
int
xchk_setup_rtrmapbt(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	if (xchk_could_repair(sc)) {
		error = xrep_setup_rtrmapbt(sc);
		if (error)
			return error;
	}

	error = xchk_rtgroup_init(sc, sc->sm->sm_agno, &sc->sr);
	if (error)
		return error;

	error = xchk_setup_rt(sc);
	if (error)
		return error;

	error = xchk_install_live_inode(sc, rtg_rmap(sc->sr.rtg));
	if (error)
		return error;

	return xchk_rtgroup_lock(sc, &sc->sr, XCHK_RTGLOCK_ALL);
}

/* Realtime reverse mapping. */

struct xchk_rtrmap {
	/*
	 * The furthest-reaching of the rmapbt records that we've already
	 * processed.  This enables us to detect overlapping records for space
	 * allocations that cannot be shared.
	 */
	struct xfs_rmap_irec	overlap_rec;

	/*
	 * The previous rmapbt record, so that we can check for two records
	 * that could be one.
	 */
	struct xfs_rmap_irec	prev_rec;
};

static inline bool
xchk_rtrmapbt_is_shareable(
	struct xfs_scrub		*sc,
	const struct xfs_rmap_irec	*irec)
{
	if (!xfs_has_rtreflink(sc->mp))
		return false;
	if (irec->rm_flags & XFS_RMAP_UNWRITTEN)
		return false;
	return true;
}

/* Flag failures for records that overlap but cannot. */
STATIC void
xchk_rtrmapbt_check_overlapping(
	struct xchk_btree		*bs,
	struct xchk_rtrmap		*cr,
	const struct xfs_rmap_irec	*irec)
{
	xfs_rtblock_t			pnext, inext;

	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	/* No previous record? */
	if (cr->overlap_rec.rm_blockcount == 0)
		goto set_prev;

	/* Do overlap_rec and irec overlap? */
	pnext = cr->overlap_rec.rm_startblock + cr->overlap_rec.rm_blockcount;
	if (pnext <= irec->rm_startblock)
		goto set_prev;

	/* Overlap is only allowed if both records are data fork mappings. */
	if (!xchk_rtrmapbt_is_shareable(bs->sc, &cr->overlap_rec) ||
	    !xchk_rtrmapbt_is_shareable(bs->sc, irec))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	/* Save whichever rmap record extends furthest. */
	inext = irec->rm_startblock + irec->rm_blockcount;
	if (pnext > inext)
		return;

set_prev:
	memcpy(&cr->overlap_rec, irec, sizeof(struct xfs_rmap_irec));
}

/* Decide if two reverse-mapping records can be merged. */
static inline bool
xchk_rtrmap_mergeable(
	struct xchk_rtrmap		*cr,
	const struct xfs_rmap_irec	*r2)
{
	const struct xfs_rmap_irec	*r1 = &cr->prev_rec;

	/* Ignore if prev_rec is not yet initialized. */
	if (cr->prev_rec.rm_blockcount == 0)
		return false;

	if (r1->rm_owner != r2->rm_owner)
		return false;
	if (r1->rm_startblock + r1->rm_blockcount != r2->rm_startblock)
		return false;
	if ((unsigned long long)r1->rm_blockcount + r2->rm_blockcount >
	    XFS_RMAP_LEN_MAX)
		return false;
	if (r1->rm_flags != r2->rm_flags)
		return false;
	return r1->rm_offset + r1->rm_blockcount == r2->rm_offset;
}

/* Flag failures for records that could be merged. */
STATIC void
xchk_rtrmapbt_check_mergeable(
	struct xchk_btree		*bs,
	struct xchk_rtrmap		*cr,
	const struct xfs_rmap_irec	*irec)
{
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	if (xchk_rtrmap_mergeable(cr, irec))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	memcpy(&cr->prev_rec, irec, sizeof(struct xfs_rmap_irec));
}

/* Cross-reference a rmap against the refcount btree. */
STATIC void
xchk_rtrmapbt_xref_rtrefc(
	struct xfs_scrub	*sc,
	struct xfs_rmap_irec	*irec)
{
	xfs_rgblock_t		fbno;
	xfs_extlen_t		flen;
	bool			is_inode;
	bool			is_bmbt;
	bool			is_attr;
	bool			is_unwritten;
	int			error;

	if (!sc->sr.refc_cur || xchk_skip_xref(sc->sm))
		return;

	is_inode = !XFS_RMAP_NON_INODE_OWNER(irec->rm_owner);
	is_bmbt = irec->rm_flags & XFS_RMAP_BMBT_BLOCK;
	is_attr = irec->rm_flags & XFS_RMAP_ATTR_FORK;
	is_unwritten = irec->rm_flags & XFS_RMAP_UNWRITTEN;

	/* If this is shared, must be a data fork extent. */
	error = xfs_refcount_find_shared(sc->sr.refc_cur, irec->rm_startblock,
			irec->rm_blockcount, &fbno, &flen, false);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.refc_cur))
		return;
	if (flen != 0 && (!is_inode || is_attr || is_bmbt || is_unwritten))
		xchk_btree_xref_set_corrupt(sc, sc->sr.refc_cur, 0);
}

/* Cross-reference with other metadata. */
STATIC void
xchk_rtrmapbt_xref(
	struct xfs_scrub	*sc,
	struct xfs_rmap_irec	*irec)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_rt_space(sc,
			xfs_rgbno_to_rtb(sc->sr.rtg, irec->rm_startblock),
			irec->rm_blockcount);
	if (irec->rm_owner == XFS_RMAP_OWN_COW)
		xchk_xref_is_cow_staging(sc, irec->rm_startblock,
				irec->rm_blockcount);
	else
		xchk_rtrmapbt_xref_rtrefc(sc, irec);
}

/* Scrub a realtime rmapbt record. */
STATIC int
xchk_rtrmapbt_rec(
	struct xchk_btree		*bs,
	const union xfs_btree_rec	*rec)
{
	struct xchk_rtrmap		*cr = bs->private;
	struct xfs_rmap_irec		irec;

	if (xfs_rmap_btrec_to_irec(rec, &irec) != NULL ||
	    xfs_rtrmap_check_irec(to_rtg(bs->cur->bc_group), &irec) != NULL) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	xchk_rtrmapbt_check_mergeable(bs, cr, &irec);
	xchk_rtrmapbt_check_overlapping(bs, cr, &irec);
	xchk_rtrmapbt_xref(bs->sc, &irec);
	return 0;
}

/* Scrub the realtime rmap btree. */
int
xchk_rtrmapbt(
	struct xfs_scrub	*sc)
{
	struct xfs_inode	*ip = rtg_rmap(sc->sr.rtg);
	struct xfs_owner_info	oinfo;
	struct xchk_rtrmap	cr = { };
	int			error;

	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	xfs_rmap_ino_bmbt_owner(&oinfo, ip->i_ino, XFS_DATA_FORK);
	return xchk_btree(sc, sc->sr.rmap_cur, xchk_rtrmapbt_rec, &oinfo, &cr);
}

/* xref check that the extent has no realtime reverse mapping at all */
void
xchk_xref_has_no_rt_owner(
	struct xfs_scrub	*sc,
	xfs_rgblock_t		bno,
	xfs_extlen_t		len)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!sc->sr.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_has_records(sc->sr.rmap_cur, bno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.rmap_cur))
		return;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
}

/* xref check that the extent is completely mapped */
void
xchk_xref_has_rt_owner(
	struct xfs_scrub	*sc,
	xfs_rgblock_t		bno,
	xfs_extlen_t		len)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!sc->sr.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_has_records(sc->sr.rmap_cur, bno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.rmap_cur))
		return;
	if (outcome != XBTREE_RECPACKING_FULL)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
}

/* xref check that the extent is only owned by a given owner */
void
xchk_xref_is_only_rt_owned_by(
	struct xfs_scrub		*sc,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_rmap_matches		res;
	int				error;

	if (!sc->sr.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_count_owners(sc->sr.rmap_cur, bno, len, oinfo, &res);
	if (!xchk_should_check_xref(sc, &error, &sc->sr.rmap_cur))
		return;
	if (res.matches != 1)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
	if (res.bad_non_owner_matches)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
	if (res.non_owner_matches)
		xchk_btree_xref_set_corrupt(sc, sc->sr.rmap_cur, 0);
}
