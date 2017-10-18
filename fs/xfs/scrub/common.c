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
#include "xfs_alloc_btree.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"

/* Common code for the metadata scrubbers. */

/*
 * Handling operational errors.
 *
 * The *_process_error() family of functions are used to process error return
 * codes from functions called as part of a scrub operation.
 *
 * If there's no error, we return true to tell the caller that it's ok
 * to move on to the next check in its list.
 *
 * For non-verifier errors (e.g. ENOMEM) we return false to tell the
 * caller that something bad happened, and we preserve *error so that
 * the caller can return the *error up the stack to userspace.
 *
 * Verifier errors (EFSBADCRC/EFSCORRUPTED) are recorded by setting
 * OFLAG_CORRUPT in sm_flags and the *error is cleared.  In other words,
 * we track verifier errors (and failed scrub checks) via OFLAG_CORRUPT,
 * not via return codes.  We return false to tell the caller that
 * something bad happened.  Since the error has been cleared, the caller
 * will (presumably) return that zero and scrubbing will move on to
 * whatever's next.
 *
 * ftrace can be used to record the precise metadata location and the
 * approximate code location of the failed operation.
 */

/* Check for operational errors. */
bool
xfs_scrub_process_error(
	struct xfs_scrub_context	*sc,
	xfs_agnumber_t			agno,
	xfs_agblock_t			bno,
	int				*error)
{
	switch (*error) {
	case 0:
		return true;
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
		trace_xfs_scrub_op_error(sc, agno, bno, *error,
				__return_address);
		break;
	}
	return false;
}

/* Check for operational errors for a file offset. */
bool
xfs_scrub_fblock_process_error(
	struct xfs_scrub_context	*sc,
	int				whichfork,
	xfs_fileoff_t			offset,
	int				*error)
{
	switch (*error) {
	case 0:
		return true;
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
		trace_xfs_scrub_file_op_error(sc, whichfork, offset, *error,
				__return_address);
		break;
	}
	return false;
}

/*
 * Handling scrub corruption/optimization/warning checks.
 *
 * The *_set_{corrupt,preen,warning}() family of functions are used to
 * record the presence of metadata that is incorrect (corrupt), could be
 * optimized somehow (preen), or should be flagged for administrative
 * review but is not incorrect (warn).
 *
 * ftrace can be used to record the precise metadata location and
 * approximate code location of the failed check.
 */

/* Record a block which could be optimized. */
void
xfs_scrub_block_set_preen(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_PREEN;
	trace_xfs_scrub_block_preen(sc, bp->b_bn, __return_address);
}

/*
 * Record an inode which could be optimized.  The trace data will
 * include the block given by bp if bp is given; otherwise it will use
 * the block location of the inode record itself.
 */
void
xfs_scrub_ino_set_preen(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_PREEN;
	trace_xfs_scrub_ino_preen(sc, sc->ip->i_ino, bp ? bp->b_bn : 0,
			__return_address);
}

/* Record a corrupt block. */
void
xfs_scrub_block_set_corrupt(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xfs_scrub_block_error(sc, bp->b_bn, __return_address);
}

/*
 * Record a corrupt inode.  The trace data will include the block given
 * by bp if bp is given; otherwise it will use the block location of the
 * inode record itself.
 */
void
xfs_scrub_ino_set_corrupt(
	struct xfs_scrub_context	*sc,
	xfs_ino_t			ino,
	struct xfs_buf			*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xfs_scrub_ino_error(sc, ino, bp ? bp->b_bn : 0, __return_address);
}

/* Record corruption in a block indexed by a file fork. */
void
xfs_scrub_fblock_set_corrupt(
	struct xfs_scrub_context	*sc,
	int				whichfork,
	xfs_fileoff_t			offset)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_CORRUPT;
	trace_xfs_scrub_fblock_error(sc, whichfork, offset, __return_address);
}

/*
 * Warn about inodes that need administrative review but is not
 * incorrect.
 */
void
xfs_scrub_ino_set_warning(
	struct xfs_scrub_context	*sc,
	struct xfs_buf			*bp)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_WARNING;
	trace_xfs_scrub_ino_warning(sc, sc->ip->i_ino, bp ? bp->b_bn : 0,
			__return_address);
}

/* Warn about a block indexed by a file fork that needs review. */
void
xfs_scrub_fblock_set_warning(
	struct xfs_scrub_context	*sc,
	int				whichfork,
	xfs_fileoff_t			offset)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_WARNING;
	trace_xfs_scrub_fblock_warning(sc, whichfork, offset, __return_address);
}

/* Signal an incomplete scrub. */
void
xfs_scrub_set_incomplete(
	struct xfs_scrub_context	*sc)
{
	sc->sm->sm_flags |= XFS_SCRUB_OFLAG_INCOMPLETE;
	trace_xfs_scrub_incomplete(sc, __return_address);
}

/* Per-scrubber setup functions */

/* Set us up with a transaction and an empty context. */
int
xfs_scrub_setup_fs(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	return xfs_scrub_trans_alloc(sc->sm, sc->mp, &sc->tp);
}
