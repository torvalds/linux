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
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/*
 * Set us up to scrub reference count btrees.
 */
int
xfs_scrub_setup_ag_refcountbt(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	return xfs_scrub_setup_ag_btree(sc, ip, false);
}

/* Reference count btree scrubber. */

/* Scrub a refcountbt record. */
STATIC int
xfs_scrub_refcountbt_rec(
	struct xfs_scrub_btree		*bs,
	union xfs_btree_rec		*rec)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	xfs_agnumber_t			agno = bs->cur->bc_private.a.agno;
	xfs_agblock_t			bno;
	xfs_extlen_t			len;
	xfs_nlink_t			refcount;
	bool				has_cowflag;
	int				error = 0;

	bno = be32_to_cpu(rec->refc.rc_startblock);
	len = be32_to_cpu(rec->refc.rc_blockcount);
	refcount = be32_to_cpu(rec->refc.rc_refcount);

	/* Only CoW records can have refcount == 1. */
	has_cowflag = (bno & XFS_REFC_COW_START);
	if ((refcount == 1 && !has_cowflag) || (refcount != 1 && has_cowflag))
		xfs_scrub_btree_set_corrupt(bs->sc, bs->cur, 0);

	/* Check the extent. */
	bno &= ~XFS_REFC_COW_START;
	if (bno + len <= bno ||
	    !xfs_verify_agbno(mp, agno, bno) ||
	    !xfs_verify_agbno(mp, agno, bno + len - 1))
		xfs_scrub_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (refcount == 0)
		xfs_scrub_btree_set_corrupt(bs->sc, bs->cur, 0);

	return error;
}

/* Scrub the refcount btree for some AG. */
int
xfs_scrub_refcountbt(
	struct xfs_scrub_context	*sc)
{
	struct xfs_owner_info		oinfo;

	xfs_rmap_ag_owner(&oinfo, XFS_RMAP_OWN_REFC);
	return xfs_scrub_btree(sc, sc->sa.refc_cur, xfs_scrub_refcountbt_rec,
			&oinfo, NULL);
}
