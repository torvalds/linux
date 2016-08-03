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
