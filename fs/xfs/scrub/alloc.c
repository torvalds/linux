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
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/repair.h"

/*
 * Set us up to scrub free space btrees.
 */
int
xchk_setup_ag_allocbt(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	error = xchk_setup_ag_btree(sc, false);
	if (error)
		return error;

	if (xchk_could_repair(sc))
		return xrep_setup_ag_allocbt(sc);

	return 0;
}

/* Free space btree scrubber. */

struct xchk_alloc {
	/* Previous free space extent. */
	struct xfs_alloc_rec_incore	prev;
};

/*
 * Ensure there's a corresponding cntbt/bnobt record matching this
 * bnobt/cntbt record, respectively.
 */
STATIC void
xchk_allocbt_xref_other(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	struct xfs_btree_cur	**pcur;
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	int			has_otherrec;
	int			error;

	if (sc->sm->sm_type == XFS_SCRUB_TYPE_BNOBT)
		pcur = &sc->sa.cnt_cur;
	else
		pcur = &sc->sa.bno_cur;
	if (!*pcur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_alloc_lookup_le(*pcur, agbno, len, &has_otherrec);
	if (!xchk_should_check_xref(sc, &error, pcur))
		return;
	if (!has_otherrec) {
		xchk_btree_xref_set_corrupt(sc, *pcur, 0);
		return;
	}

	error = xfs_alloc_get_rec(*pcur, &fbno, &flen, &has_otherrec);
	if (!xchk_should_check_xref(sc, &error, pcur))
		return;
	if (!has_otherrec) {
		xchk_btree_xref_set_corrupt(sc, *pcur, 0);
		return;
	}

	if (fbno != agbno || flen != len)
		xchk_btree_xref_set_corrupt(sc, *pcur, 0);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_allocbt_xref(
	struct xfs_scrub	*sc,
	const struct xfs_alloc_rec_incore *irec)
{
	xfs_agblock_t		agbno = irec->ar_startblock;
	xfs_extlen_t		len = irec->ar_blockcount;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_allocbt_xref_other(sc, agbno, len);
	xchk_xref_is_not_inode_chunk(sc, agbno, len);
	xchk_xref_has_no_owner(sc, agbno, len);
	xchk_xref_is_not_shared(sc, agbno, len);
	xchk_xref_is_not_cow_staging(sc, agbno, len);
}

/* Flag failures for records that could be merged. */
STATIC void
xchk_allocbt_mergeable(
	struct xchk_btree	*bs,
	struct xchk_alloc	*ca,
	const struct xfs_alloc_rec_incore *irec)
{
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	if (ca->prev.ar_blockcount > 0 &&
	    ca->prev.ar_startblock + ca->prev.ar_blockcount == irec->ar_startblock &&
	    ca->prev.ar_blockcount + irec->ar_blockcount < (uint32_t)~0U)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	memcpy(&ca->prev, irec, sizeof(*irec));
}

/* Scrub a bnobt/cntbt record. */
STATIC int
xchk_allocbt_rec(
	struct xchk_btree		*bs,
	const union xfs_btree_rec	*rec)
{
	struct xfs_alloc_rec_incore	irec;
	struct xchk_alloc	*ca = bs->private;

	xfs_alloc_btrec_to_irec(rec, &irec);
	if (xfs_alloc_check_irec(bs->cur->bc_ag.pag, &irec) != NULL) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	xchk_allocbt_mergeable(bs, ca, &irec);
	xchk_allocbt_xref(bs->sc, &irec);

	return 0;
}

/* Scrub one of the freespace btrees for some AG. */
int
xchk_allocbt(
	struct xfs_scrub	*sc)
{
	struct xchk_alloc	ca = { };
	struct xfs_btree_cur	*cur;

	switch (sc->sm->sm_type) {
	case XFS_SCRUB_TYPE_BNOBT:
		cur = sc->sa.bno_cur;
		break;
	case XFS_SCRUB_TYPE_CNTBT:
		cur = sc->sa.cnt_cur;
		break;
	default:
		ASSERT(0);
		return -EIO;
	}

	return xchk_btree(sc, cur, xchk_allocbt_rec, &XFS_RMAP_OINFO_AG, &ca);
}

/* xref check that the extent is not free */
void
xchk_xref_is_used_space(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!sc->sa.bno_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_alloc_has_records(sc->sa.bno_cur, agbno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.bno_cur))
		return;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		xchk_btree_xref_set_corrupt(sc, sc->sa.bno_cur, 0);
}
