/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_trans_space.h"
#include "xfs_trace.h"
#include "xfs_error.h"
#include "xfs_extent_busy.h"

/*
 * Lookup the first record less than or equal to [bno, len, owner, offset]
 * in the btree given by cur.
 */
int
xfs_rmap_lookup_le(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	int			*stat)
{
	cur->bc_rec.r.rm_startblock = bno;
	cur->bc_rec.r.rm_blockcount = len;
	cur->bc_rec.r.rm_owner = owner;
	cur->bc_rec.r.rm_offset = offset;
	cur->bc_rec.r.rm_flags = flags;
	return xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Lookup the record exactly matching [bno, len, owner, offset]
 * in the btree given by cur.
 */
int
xfs_rmap_lookup_eq(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	int			*stat)
{
	cur->bc_rec.r.rm_startblock = bno;
	cur->bc_rec.r.rm_blockcount = len;
	cur->bc_rec.r.rm_owner = owner;
	cur->bc_rec.r.rm_offset = offset;
	cur->bc_rec.r.rm_flags = flags;
	return xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
}

/*
 * Update the record referred to by cur to the value given
 * by [bno, len, owner, offset].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_rmap_update(
	struct xfs_btree_cur	*cur,
	struct xfs_rmap_irec	*irec)
{
	union xfs_btree_rec	rec;

	rec.rmap.rm_startblock = cpu_to_be32(irec->rm_startblock);
	rec.rmap.rm_blockcount = cpu_to_be32(irec->rm_blockcount);
	rec.rmap.rm_owner = cpu_to_be64(irec->rm_owner);
	rec.rmap.rm_offset = cpu_to_be64(
			xfs_rmap_irec_offset_pack(irec));
	return xfs_btree_update(cur, &rec);
}

static int
xfs_rmap_btrec_to_irec(
	union xfs_btree_rec	*rec,
	struct xfs_rmap_irec	*irec)
{
	irec->rm_flags = 0;
	irec->rm_startblock = be32_to_cpu(rec->rmap.rm_startblock);
	irec->rm_blockcount = be32_to_cpu(rec->rmap.rm_blockcount);
	irec->rm_owner = be64_to_cpu(rec->rmap.rm_owner);
	return xfs_rmap_irec_offset_unpack(be64_to_cpu(rec->rmap.rm_offset),
			irec);
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_rmap_get_rec(
	struct xfs_btree_cur	*cur,
	struct xfs_rmap_irec	*irec,
	int			*stat)
{
	union xfs_btree_rec	*rec;
	int			error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || !*stat)
		return error;

	return xfs_rmap_btrec_to_irec(rec, irec);
}

int
xfs_rmap_free(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	struct xfs_owner_info	*oinfo)
{
	struct xfs_mount	*mp = tp->t_mountp;
	int			error = 0;

	if (!xfs_sb_version_hasrmapbt(&mp->m_sb))
		return 0;

	trace_xfs_rmap_unmap(mp, agno, bno, len, false, oinfo);
	if (1)
		goto out_error;
	trace_xfs_rmap_unmap_done(mp, agno, bno, len, false, oinfo);
	return 0;

out_error:
	trace_xfs_rmap_unmap_error(mp, agno, error, _RET_IP_);
	return error;
}

int
xfs_rmap_alloc(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	struct xfs_owner_info	*oinfo)
{
	struct xfs_mount	*mp = tp->t_mountp;
	int			error = 0;

	if (!xfs_sb_version_hasrmapbt(&mp->m_sb))
		return 0;

	trace_xfs_rmap_map(mp, agno, bno, len, false, oinfo);
	if (1)
		goto out_error;
	trace_xfs_rmap_map_done(mp, agno, bno, len, false, oinfo);
	return 0;

out_error:
	trace_xfs_rmap_map_error(mp, agno, error, _RET_IP_);
	return error;
}
