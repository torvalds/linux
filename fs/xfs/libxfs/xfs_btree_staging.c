// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_btree.h"
#include "xfs_trace.h"
#include "xfs_btree_staging.h"

/*
 * Staging Cursors and Fake Roots for Btrees
 * =========================================
 *
 * A staging btree cursor is a special type of btree cursor that callers must
 * use to construct a new btree index using the btree bulk loader code.  The
 * bulk loading code uses the staging btree cursor to abstract the details of
 * initializing new btree blocks and filling them with records or key/ptr
 * pairs.  Regular btree operations (e.g. queries and modifications) are not
 * supported with staging cursors, and callers must not invoke them.
 *
 * Fake root structures contain all the information about a btree that is under
 * construction by the bulk loading code.  Staging btree cursors point to fake
 * root structures instead of the usual AG header or inode structure.
 *
 * Callers are expected to initialize a fake root structure and pass it into
 * the _stage_cursor function for a specific btree type.  When bulk loading is
 * complete, callers should call the _commit_staged_btree function for that
 * specific btree type to commit the new btree into the filesystem.
 */

/*
 * Don't allow staging cursors to be duplicated because they're supposed to be
 * kept private to a single thread.
 */
STATIC struct xfs_btree_cur *
xfs_btree_fakeroot_dup_cursor(
	struct xfs_btree_cur	*cur)
{
	ASSERT(0);
	return NULL;
}

/*
 * Don't allow block allocation for a staging cursor, because staging cursors
 * do not support regular btree modifications.
 *
 * Bulk loading uses a separate callback to obtain new blocks from a
 * preallocated list, which prevents ENOSPC failures during loading.
 */
STATIC int
xfs_btree_fakeroot_alloc_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*start_bno,
	union xfs_btree_ptr	*new_bno,
	int			*stat)
{
	ASSERT(0);
	return -EFSCORRUPTED;
}

/*
 * Don't allow block freeing for a staging cursor, because staging cursors
 * do not support regular btree modifications.
 */
STATIC int
xfs_btree_fakeroot_free_block(
	struct xfs_btree_cur	*cur,
	struct xfs_buf		*bp)
{
	ASSERT(0);
	return -EFSCORRUPTED;
}

/* Initialize a pointer to the root block from the fakeroot. */
STATIC void
xfs_btree_fakeroot_init_ptr_from_cur(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr)
{
	struct xbtree_afakeroot	*afake;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);

	afake = cur->bc_ag.afake;
	ptr->s = cpu_to_be32(afake->af_root);
}

/*
 * Bulk Loading for AG Btrees
 * ==========================
 *
 * For a btree rooted in an AG header, pass a xbtree_afakeroot structure to the
 * staging cursor.  Callers should initialize this to zero.
 *
 * The _stage_cursor() function for a specific btree type should call
 * xfs_btree_stage_afakeroot to set up the in-memory cursor as a staging
 * cursor.  The corresponding _commit_staged_btree() function should log the
 * new root and call xfs_btree_commit_afakeroot() to transform the staging
 * cursor into a regular btree cursor.
 */

/* Update the btree root information for a per-AG fake root. */
STATIC void
xfs_btree_afakeroot_set_root(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	int			inc)
{
	struct xbtree_afakeroot	*afake = cur->bc_ag.afake;

	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);
	afake->af_root = be32_to_cpu(ptr->s);
	afake->af_levels += inc;
}

/*
 * Initialize a AG-rooted btree cursor with the given AG btree fake root.
 * The btree cursor's bc_ops will be overridden as needed to make the staging
 * functionality work.
 */
void
xfs_btree_stage_afakeroot(
	struct xfs_btree_cur		*cur,
	struct xbtree_afakeroot		*afake)
{
	struct xfs_btree_ops		*nops;

	ASSERT(!(cur->bc_flags & XFS_BTREE_STAGING));
	ASSERT(!(cur->bc_flags & XFS_BTREE_ROOT_IN_INODE));
	ASSERT(cur->bc_tp == NULL);

	nops = kmem_alloc(sizeof(struct xfs_btree_ops), KM_NOFS);
	memcpy(nops, cur->bc_ops, sizeof(struct xfs_btree_ops));
	nops->alloc_block = xfs_btree_fakeroot_alloc_block;
	nops->free_block = xfs_btree_fakeroot_free_block;
	nops->init_ptr_from_cur = xfs_btree_fakeroot_init_ptr_from_cur;
	nops->set_root = xfs_btree_afakeroot_set_root;
	nops->dup_cursor = xfs_btree_fakeroot_dup_cursor;

	cur->bc_ag.afake = afake;
	cur->bc_nlevels = afake->af_levels;
	cur->bc_ops = nops;
	cur->bc_flags |= XFS_BTREE_STAGING;
}

/*
 * Transform an AG-rooted staging btree cursor back into a regular cursor by
 * substituting a real btree root for the fake one and restoring normal btree
 * cursor ops.  The caller must log the btree root change prior to calling
 * this.
 */
void
xfs_btree_commit_afakeroot(
	struct xfs_btree_cur		*cur,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	const struct xfs_btree_ops	*ops)
{
	ASSERT(cur->bc_flags & XFS_BTREE_STAGING);
	ASSERT(cur->bc_tp == NULL);

	trace_xfs_btree_commit_afakeroot(cur);

	kmem_free((void *)cur->bc_ops);
	cur->bc_ag.agbp = agbp;
	cur->bc_ops = ops;
	cur->bc_flags &= ~XFS_BTREE_STAGING;
	cur->bc_tp = tp;
}
