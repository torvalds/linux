/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
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
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_ag_resv.h"
#include "xfs_trans_space.h"
#include "xfs_rmap_btree.h"
#include "xfs_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_ialloc_btree.h"

/*
 * Per-AG Block Reservations
 *
 * For some kinds of allocation group metadata structures, it is advantageous
 * to reserve a small number of blocks in each AG so that future expansions of
 * that data structure do not encounter ENOSPC because errors during a btree
 * split cause the filesystem to go offline.
 *
 * Prior to the introduction of reflink, this wasn't an issue because the free
 * space btrees maintain a reserve of space (the AGFL) to handle any expansion
 * that may be necessary; and allocations of other metadata (inodes, BMBT,
 * dir/attr) aren't restricted to a single AG.  However, with reflink it is
 * possible to allocate all the space in an AG, have subsequent reflink/CoW
 * activity expand the refcount btree, and discover that there's no space left
 * to handle that expansion.  Since we can calculate the maximum size of the
 * refcount btree, we can reserve space for it and avoid ENOSPC.
 *
 * Handling per-AG reservations consists of three changes to the allocator's
 * behavior:  First, because these reservations are always needed, we decrease
 * the ag_max_usable counter to reflect the size of the AG after the reserved
 * blocks are taken.  Second, the reservations must be reflected in the
 * fdblocks count to maintain proper accounting.  Third, each AG must maintain
 * its own reserved block counter so that we can calculate the amount of space
 * that must remain free to maintain the reservations.  Fourth, the "remaining
 * reserved blocks" count must be used when calculating the length of the
 * longest free extent in an AG and to clamp maxlen in the per-AG allocation
 * functions.  In other words, we maintain a virtual allocation via in-core
 * accounting tricks so that we don't have to clean up after a crash. :)
 *
 * Reserved blocks can be managed by passing one of the enum xfs_ag_resv_type
 * values via struct xfs_alloc_arg or directly to the xfs_free_extent
 * function.  It might seem a little funny to maintain a reservoir of blocks
 * to feed another reservoir, but the AGFL only holds enough blocks to get
 * through the next transaction.  The per-AG reservation is to ensure (we
 * hope) that each AG never runs out of blocks.  Each data structure wanting
 * to use the reservation system should update ask/used in xfs_ag_resv_init.
 */

/*
 * Are we critically low on blocks?  For now we'll define that as the number
 * of blocks we can get our hands on being less than 10% of what we reserved
 * or less than some arbitrary number (maximum btree height).
 */
bool
xfs_ag_resv_critical(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type)
{
	xfs_extlen_t			avail;
	xfs_extlen_t			orig;

	switch (type) {
	case XFS_AG_RESV_METADATA:
		avail = pag->pagf_freeblks - pag->pag_agfl_resv.ar_reserved;
		orig = pag->pag_meta_resv.ar_asked;
		break;
	case XFS_AG_RESV_AGFL:
		avail = pag->pagf_freeblks + pag->pagf_flcount -
			pag->pag_meta_resv.ar_reserved;
		orig = pag->pag_agfl_resv.ar_asked;
		break;
	default:
		ASSERT(0);
		return false;
	}

	trace_xfs_ag_resv_critical(pag, type, avail);

	/* Critically low if less than 10% or max btree height remains. */
	return XFS_TEST_ERROR(avail < orig / 10 || avail < XFS_BTREE_MAXLEVELS,
			pag->pag_mount, XFS_ERRTAG_AG_RESV_CRITICAL);
}

/*
 * How many blocks are reserved but not used, and therefore must not be
 * allocated away?
 */
xfs_extlen_t
xfs_ag_resv_needed(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type)
{
	xfs_extlen_t			len;

	len = pag->pag_meta_resv.ar_reserved + pag->pag_agfl_resv.ar_reserved;
	switch (type) {
	case XFS_AG_RESV_METADATA:
	case XFS_AG_RESV_AGFL:
		len -= xfs_perag_resv(pag, type)->ar_reserved;
		break;
	case XFS_AG_RESV_NONE:
		/* empty */
		break;
	default:
		ASSERT(0);
	}

	trace_xfs_ag_resv_needed(pag, type, len);

	return len;
}

/* Clean out a reservation */
static int
__xfs_ag_resv_free(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type)
{
	struct xfs_ag_resv		*resv;
	xfs_extlen_t			oldresv;
	int				error;

	trace_xfs_ag_resv_free(pag, type, 0);

	resv = xfs_perag_resv(pag, type);
	if (pag->pag_agno == 0)
		pag->pag_mount->m_ag_max_usable += resv->ar_asked;
	/*
	 * AGFL blocks are always considered "free", so whatever
	 * was reserved at mount time must be given back at umount.
	 */
	if (type == XFS_AG_RESV_AGFL)
		oldresv = resv->ar_orig_reserved;
	else
		oldresv = resv->ar_reserved;
	error = xfs_mod_fdblocks(pag->pag_mount, oldresv, true);
	resv->ar_reserved = 0;
	resv->ar_asked = 0;

	if (error)
		trace_xfs_ag_resv_free_error(pag->pag_mount, pag->pag_agno,
				error, _RET_IP_);
	return error;
}

/* Free a per-AG reservation. */
int
xfs_ag_resv_free(
	struct xfs_perag		*pag)
{
	int				error;
	int				err2;

	error = __xfs_ag_resv_free(pag, XFS_AG_RESV_AGFL);
	err2 = __xfs_ag_resv_free(pag, XFS_AG_RESV_METADATA);
	if (err2 && !error)
		error = err2;
	return error;
}

static int
__xfs_ag_resv_init(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type,
	xfs_extlen_t			ask,
	xfs_extlen_t			used)
{
	struct xfs_mount		*mp = pag->pag_mount;
	struct xfs_ag_resv		*resv;
	int				error;
	xfs_extlen_t			reserved;

	if (used > ask)
		ask = used;
	reserved = ask - used;

	error = xfs_mod_fdblocks(mp, -(int64_t)reserved, true);
	if (error) {
		trace_xfs_ag_resv_init_error(pag->pag_mount, pag->pag_agno,
				error, _RET_IP_);
		xfs_warn(mp,
"Per-AG reservation for AG %u failed.  Filesystem may run out of space.",
				pag->pag_agno);
		return error;
	}

	/*
	 * Reduce the maximum per-AG allocation length by however much we're
	 * trying to reserve for an AG.  Since this is a filesystem-wide
	 * counter, we only make the adjustment for AG 0.  This assumes that
	 * there aren't any AGs hungrier for per-AG reservation than AG 0.
	 */
	if (pag->pag_agno == 0)
		mp->m_ag_max_usable -= ask;

	resv = xfs_perag_resv(pag, type);
	resv->ar_asked = ask;
	resv->ar_reserved = resv->ar_orig_reserved = reserved;

	trace_xfs_ag_resv_init(pag, type, ask);
	return 0;
}

/* Create a per-AG block reservation. */
int
xfs_ag_resv_init(
	struct xfs_perag		*pag)
{
	struct xfs_mount		*mp = pag->pag_mount;
	xfs_agnumber_t			agno = pag->pag_agno;
	xfs_extlen_t			ask;
	xfs_extlen_t			used;
	int				error = 0;

	/* Create the metadata reservation. */
	if (pag->pag_meta_resv.ar_asked == 0) {
		ask = used = 0;

		error = xfs_refcountbt_calc_reserves(mp, agno, &ask, &used);
		if (error)
			goto out;

		error = xfs_finobt_calc_reserves(mp, agno, &ask, &used);
		if (error)
			goto out;

		error = __xfs_ag_resv_init(pag, XFS_AG_RESV_METADATA,
				ask, used);
		if (error) {
			/*
			 * Because we didn't have per-AG reservations when the
			 * finobt feature was added we might not be able to
			 * reserve all needed blocks.  Warn and fall back to the
			 * old and potentially buggy code in that case, but
			 * ensure we do have the reservation for the refcountbt.
			 */
			ask = used = 0;

			mp->m_inotbt_nores = true;

			error = xfs_refcountbt_calc_reserves(mp, agno, &ask,
					&used);
			if (error)
				goto out;

			error = __xfs_ag_resv_init(pag, XFS_AG_RESV_METADATA,
					ask, used);
			if (error)
				goto out;
		}
	}

	/* Create the AGFL metadata reservation */
	if (pag->pag_agfl_resv.ar_asked == 0) {
		ask = used = 0;

		error = xfs_rmapbt_calc_reserves(mp, agno, &ask, &used);
		if (error)
			goto out;

		error = __xfs_ag_resv_init(pag, XFS_AG_RESV_AGFL, ask, used);
		if (error)
			goto out;
	}

#ifdef DEBUG
	/* need to read in the AGF for the ASSERT below to work */
	error = xfs_alloc_pagf_init(pag->pag_mount, NULL, pag->pag_agno, 0);
	if (error)
		return error;

	ASSERT(xfs_perag_resv(pag, XFS_AG_RESV_METADATA)->ar_reserved +
	       xfs_perag_resv(pag, XFS_AG_RESV_AGFL)->ar_reserved <=
	       pag->pagf_freeblks + pag->pagf_flcount);
#endif
out:
	return error;
}

/* Allocate a block from the reservation. */
void
xfs_ag_resv_alloc_extent(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type,
	struct xfs_alloc_arg		*args)
{
	struct xfs_ag_resv		*resv;
	xfs_extlen_t			len;
	uint				field;

	trace_xfs_ag_resv_alloc_extent(pag, type, args->len);

	switch (type) {
	case XFS_AG_RESV_METADATA:
	case XFS_AG_RESV_AGFL:
		resv = xfs_perag_resv(pag, type);
		break;
	default:
		ASSERT(0);
		/* fall through */
	case XFS_AG_RESV_NONE:
		field = args->wasdel ? XFS_TRANS_SB_RES_FDBLOCKS :
				       XFS_TRANS_SB_FDBLOCKS;
		xfs_trans_mod_sb(args->tp, field, -(int64_t)args->len);
		return;
	}

	len = min_t(xfs_extlen_t, args->len, resv->ar_reserved);
	resv->ar_reserved -= len;
	if (type == XFS_AG_RESV_AGFL)
		return;
	/* Allocations of reserved blocks only need on-disk sb updates... */
	xfs_trans_mod_sb(args->tp, XFS_TRANS_SB_RES_FDBLOCKS, -(int64_t)len);
	/* ...but non-reserved blocks need in-core and on-disk updates. */
	if (args->len > len)
		xfs_trans_mod_sb(args->tp, XFS_TRANS_SB_FDBLOCKS,
				-((int64_t)args->len - len));
}

/* Free a block to the reservation. */
void
xfs_ag_resv_free_extent(
	struct xfs_perag		*pag,
	enum xfs_ag_resv_type		type,
	struct xfs_trans		*tp,
	xfs_extlen_t			len)
{
	xfs_extlen_t			leftover;
	struct xfs_ag_resv		*resv;

	trace_xfs_ag_resv_free_extent(pag, type, len);

	switch (type) {
	case XFS_AG_RESV_METADATA:
	case XFS_AG_RESV_AGFL:
		resv = xfs_perag_resv(pag, type);
		break;
	default:
		ASSERT(0);
		/* fall through */
	case XFS_AG_RESV_NONE:
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, (int64_t)len);
		return;
	}

	leftover = min_t(xfs_extlen_t, len, resv->ar_asked - resv->ar_reserved);
	resv->ar_reserved += leftover;
	if (type == XFS_AG_RESV_AGFL)
		return;
	/* Freeing into the reserved pool only requires on-disk update... */
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_RES_FDBLOCKS, len);
	/* ...but freeing beyond that requires in-core and on-disk update. */
	if (len > leftover)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, len - leftover);
}
