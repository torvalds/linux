// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "xfs_ag.h"

/*
 * Set us up to scrub free space btrees.
 */
int
xchk_setup_ag_allocbt(
	struct xfs_scrub	*sc)
{
	return xchk_setup_ag_btree(sc, false);
}

/* Free space btree scrubber. */
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
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_allocbt_xref_other(sc, agbno, len);
	xchk_xref_is_not_inode_chunk(sc, agbno, len);
	xchk_xref_has_no_owner(sc, agbno, len);
	xchk_xref_is_not_shared(sc, agbno, len);
}

/* Scrub a bnobt/cntbt record. */
STATIC int
xchk_allocbt_rec(
	struct xchk_btree	*bs,
	const union xfs_btree_rec *rec)
{
	struct xfs_mount	*mp = bs->cur->bc_mp;
	xfs_agnumber_t		agno = bs->cur->bc_ag.pag->pag_agno;
	xfs_agblock_t		bno;
	xfs_extlen_t		len;

	bno = be32_to_cpu(rec->alloc.ar_startblock);
	len = be32_to_cpu(rec->alloc.ar_blockcount);

	if (bno + len <= bno ||
	    !xfs_verify_agbno(mp, agno, bno) ||
	    !xfs_verify_agbno(mp, agno, bno + len - 1))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	xchk_allocbt_xref(bs->sc, bno, len);

	return 0;
}

/* Scrub the freespace btrees for some AG. */
STATIC int
xchk_allocbt(
	struct xfs_scrub	*sc,
	xfs_btnum_t		which)
{
	struct xfs_btree_cur	*cur;

	cur = which == XFS_BTNUM_BNO ? sc->sa.bno_cur : sc->sa.cnt_cur;
	return xchk_btree(sc, cur, xchk_allocbt_rec, &XFS_RMAP_OINFO_AG, NULL);
}

int
xchk_bnobt(
	struct xfs_scrub	*sc)
{
	return xchk_allocbt(sc, XFS_BTNUM_BNO);
}

int
xchk_cntbt(
	struct xfs_scrub	*sc)
{
	return xchk_allocbt(sc, XFS_BTNUM_CNT);
}

/* xref check that the extent is not free */
void
xchk_xref_is_used_space(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	bool			is_freesp;
	int			error;

	if (!sc->sa.bno_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_alloc_has_record(sc->sa.bno_cur, agbno, len, &is_freesp);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.bno_cur))
		return;
	if (is_freesp)
		xchk_btree_xref_set_corrupt(sc, sc->sa.bno_cur, 0);
}
