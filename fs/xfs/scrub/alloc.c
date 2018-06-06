/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
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
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_alloc.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/*
 * Set us up to scrub free space btrees.
 */
int
xfs_scrub_setup_ag_allocbt(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	return xfs_scrub_setup_ag_btree(sc, ip, false);
}

/* Free space btree scrubber. */
/*
 * Ensure there's a corresponding cntbt/bnobt record matching this
 * bnobt/cntbt record, respectively.
 */
STATIC void
xfs_scrub_allocbt_xref_other(
	struct xfs_scrub_context	*sc,
	xfs_agblock_t			agbno,
	xfs_extlen_t			len)
{
	struct xfs_btree_cur		**pcur;
	xfs_agblock_t			fbno;
	xfs_extlen_t			flen;
	int				has_otherrec;
	int				error;

	if (sc->sm->sm_type == XFS_SCRUB_TYPE_BNOBT)
		pcur = &sc->sa.cnt_cur;
	else
		pcur = &sc->sa.bno_cur;
	if (!*pcur || xfs_scrub_skip_xref(sc->sm))
		return;

	error = xfs_alloc_lookup_le(*pcur, agbno, len, &has_otherrec);
	if (!xfs_scrub_should_check_xref(sc, &error, pcur))
		return;
	if (!has_otherrec) {
		xfs_scrub_btree_xref_set_corrupt(sc, *pcur, 0);
		return;
	}

	error = xfs_alloc_get_rec(*pcur, &fbno, &flen, &has_otherrec);
	if (!xfs_scrub_should_check_xref(sc, &error, pcur))
		return;
	if (!has_otherrec) {
		xfs_scrub_btree_xref_set_corrupt(sc, *pcur, 0);
		return;
	}

	if (fbno != agbno || flen != len)
		xfs_scrub_btree_xref_set_corrupt(sc, *pcur, 0);
}

/* Cross-reference with the other btrees. */
STATIC void
xfs_scrub_allocbt_xref(
	struct xfs_scrub_context	*sc,
	xfs_agblock_t			agbno,
	xfs_extlen_t			len)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xfs_scrub_allocbt_xref_other(sc, agbno, len);
	xfs_scrub_xref_is_not_inode_chunk(sc, agbno, len);
	xfs_scrub_xref_has_no_owner(sc, agbno, len);
	xfs_scrub_xref_is_not_shared(sc, agbno, len);
}

/* Scrub a bnobt/cntbt record. */
STATIC int
xfs_scrub_allocbt_rec(
	struct xfs_scrub_btree		*bs,
	union xfs_btree_rec		*rec)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	xfs_agnumber_t			agno = bs->cur->bc_private.a.agno;
	xfs_agblock_t			bno;
	xfs_extlen_t			len;
	int				error = 0;

	bno = be32_to_cpu(rec->alloc.ar_startblock);
	len = be32_to_cpu(rec->alloc.ar_blockcount);

	if (bno + len <= bno ||
	    !xfs_verify_agbno(mp, agno, bno) ||
	    !xfs_verify_agbno(mp, agno, bno + len - 1))
		xfs_scrub_btree_set_corrupt(bs->sc, bs->cur, 0);

	xfs_scrub_allocbt_xref(bs->sc, bno, len);

	return error;
}

/* Scrub the freespace btrees for some AG. */
STATIC int
xfs_scrub_allocbt(
	struct xfs_scrub_context	*sc,
	xfs_btnum_t			which)
{
	struct xfs_owner_info		oinfo;
	struct xfs_btree_cur		*cur;

	xfs_rmap_ag_owner(&oinfo, XFS_RMAP_OWN_AG);
	cur = which == XFS_BTNUM_BNO ? sc->sa.bno_cur : sc->sa.cnt_cur;
	return xfs_scrub_btree(sc, cur, xfs_scrub_allocbt_rec, &oinfo, NULL);
}

int
xfs_scrub_bnobt(
	struct xfs_scrub_context	*sc)
{
	return xfs_scrub_allocbt(sc, XFS_BTNUM_BNO);
}

int
xfs_scrub_cntbt(
	struct xfs_scrub_context	*sc)
{
	return xfs_scrub_allocbt(sc, XFS_BTNUM_CNT);
}

/* xref check that the extent is not free */
void
xfs_scrub_xref_is_used_space(
	struct xfs_scrub_context	*sc,
	xfs_agblock_t			agbno,
	xfs_extlen_t			len)
{
	bool				is_freesp;
	int				error;

	if (!sc->sa.bno_cur || xfs_scrub_skip_xref(sc->sm))
		return;

	error = xfs_alloc_has_record(sc->sa.bno_cur, agbno, len, &is_freesp);
	if (!xfs_scrub_should_check_xref(sc, &error, &sc->sa.bno_cur))
		return;
	if (is_freesp)
		xfs_scrub_btree_xref_set_corrupt(sc, sc->sa.bno_cur, 0);
}
