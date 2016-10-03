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
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_refcount_btree.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "xfs_refcount.h"

/*
 * Look up the first record less than or equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_le(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur->bc_mp, cur->bc_private.a.agno, bno,
			XFS_LOOKUP_LE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	return xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Look up the first record greater than or equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_ge(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur->bc_mp, cur->bc_private.a.agno, bno,
			XFS_LOOKUP_GE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	return xfs_btree_lookup(cur, XFS_LOOKUP_GE, stat);
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_refcount_get_rec(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec,
	int				*stat)
{
	union xfs_btree_rec	*rec;
	int			error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (!error && *stat == 1) {
		irec->rc_startblock = be32_to_cpu(rec->refc.rc_startblock);
		irec->rc_blockcount = be32_to_cpu(rec->refc.rc_blockcount);
		irec->rc_refcount = be32_to_cpu(rec->refc.rc_refcount);
		trace_xfs_refcount_get(cur->bc_mp, cur->bc_private.a.agno,
				irec);
	}
	return error;
}

/*
 * Update the record referred to by cur to the value given
 * by [bno, len, refcount].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_refcount_update(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec)
{
	union xfs_btree_rec	rec;
	int			error;

	trace_xfs_refcount_update(cur->bc_mp, cur->bc_private.a.agno, irec);
	rec.refc.rc_startblock = cpu_to_be32(irec->rc_startblock);
	rec.refc.rc_blockcount = cpu_to_be32(irec->rc_blockcount);
	rec.refc.rc_refcount = cpu_to_be32(irec->rc_refcount);
	error = xfs_btree_update(cur, &rec);
	if (error)
		trace_xfs_refcount_update_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Insert the record referred to by cur to the value given
 * by [bno, len, refcount].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_refcount_insert(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec,
	int				*i)
{
	int				error;

	trace_xfs_refcount_insert(cur->bc_mp, cur->bc_private.a.agno, irec);
	cur->bc_rec.rc.rc_startblock = irec->rc_startblock;
	cur->bc_rec.rc.rc_blockcount = irec->rc_blockcount;
	cur->bc_rec.rc.rc_refcount = irec->rc_refcount;
	error = xfs_btree_insert(cur, i);
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, *i == 1, out_error);
out_error:
	if (error)
		trace_xfs_refcount_insert_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Remove the record referred to by cur, then set the pointer to the spot
 * where the record could be re-inserted, in case we want to increment or
 * decrement the cursor.
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_refcount_delete(
	struct xfs_btree_cur	*cur,
	int			*i)
{
	struct xfs_refcount_irec	irec;
	int			found_rec;
	int			error;

	error = xfs_refcount_get_rec(cur, &irec, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);
	trace_xfs_refcount_delete(cur->bc_mp, cur->bc_private.a.agno, &irec);
	error = xfs_btree_delete(cur, i);
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, *i == 1, out_error);
	if (error)
		goto out_error;
	error = xfs_refcount_lookup_ge(cur, irec.rc_startblock, &found_rec);
out_error:
	if (error)
		trace_xfs_refcount_delete_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}
