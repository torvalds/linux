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
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/* btree scrubbing */

/*
 * Check for btree operation errors.  See the section about handling
 * operational errors in common.c.
 */
bool
xfs_scrub_btree_process_error(
	struct xfs_scrub_context	*sc,
	struct xfs_btree_cur		*cur,
	int				level,
	int				*error)
{
	if (*error == 0)
		return true;

	switch (*error) {
	case -EDEADLOCK:
		/* Used to restart an op with deadlock avoidance. */
		trace_xfs_scrub_deadlock_retry(sc->ip, sc->sm, *error);
		break;
	case -EFSBADCRC:
	case -EFSCORRUPTED:
		/* Note the badness but don't abort. */
		sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
		*error = 0;
		/* fall through */
	default:
		if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE)
			trace_xfs_scrub_ifork_btree_op_error(sc, cur, level,
					*error, __return_address);
		else
			trace_xfs_scrub_btree_op_error(sc, cur, level,
					*error, __return_address);
		break;
	}
	return false;
}

/* Record btree block corruption. */
void
xfs_scrub_btree_set_corrupt(
	struct xfs_scrub_context	*sc,
	struct xfs_btree_cur		*cur,
	int				level)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;

	if (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE)
		trace_xfs_scrub_ifork_btree_error(sc, cur, level,
				__return_address);
	else
		trace_xfs_scrub_btree_error(sc, cur, level,
				__return_address);
}

/*
 * Visit all nodes and leaves of a btree.  Check that all pointers and
 * records are in order, that the keys reflect the records, and use a callback
 * so that the caller can verify individual records.  The callback is the same
 * as the one for xfs_btree_query_range, so therefore this function also
 * returns XFS_BTREE_QUERY_RANGE_ABORT, zero, or a negative error code.
 */
int
xfs_scrub_btree(
	struct xfs_scrub_context	*sc,
	struct xfs_btree_cur		*cur,
	xfs_scrub_btree_rec_fn		scrub_fn,
	struct xfs_owner_info		*oinfo,
	void				*private)
{
	int				error = -EOPNOTSUPP;

	xfs_scrub_btree_process_error(sc, cur, 0, &error);
	return error;
}
