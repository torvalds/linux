// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_REFCOUNT_H__
#define __XFS_SCRUB_REFCOUNT_H__

/*
 * Ensure that types and macros match across rt and non-rt variants,
 * as this code is shared for both.
 */
static_assert(__same_type(xfs_agblock_t, xfs_rgblock_t));
static_assert(NULLAGBLOCK == NULLRGBLOCK);

/*
 * Confirming reference counts via reverse mappings.
 *
 * These helpers are shared by the AG (refcount.c) and realtime
 * (rtrefcount.c) refcount scrubbers.  Both the rmap and refcount incore
 * records use xfs_agblock_t for their start block regardless of whether the
 * data lives in an allocation group or a realtime group, so the fragment
 * bookkeeping here uses xfs_agblock_t for group-relative block numbers.
 */
struct xchk_refcnt_frag {
	struct list_head	list;
	struct xfs_rmap_irec	rm;
};

struct xchk_refcnt_check {
	struct xfs_scrub	*sc;
	struct list_head	fragments;

	/* refcount extent we're examining */
	xfs_agblock_t		bno;
	xfs_extlen_t		len;
	xfs_nlink_t		refcount;

	/* number of owners seen */
	xfs_nlink_t		seen;
};

int xchk_refcountbt_rmap_check(struct xfs_btree_cur *cur,
		const struct xfs_rmap_irec *rec, void *priv);
void xchk_refcountbt_process_rmap_fragments(struct xchk_refcnt_check *refchk);
int xchk_refcountbt_rmap_check_gap(struct xfs_btree_cur *cur,
		const struct xfs_rmap_irec *rec, void *priv);

/*
 * Compare two refcount records so that both the AG (refcount_repair.c) and
 * realtime (rtrefcount_repair.c) rebuilders sort staged records into the same
 * order as the ondisk records.
 */
int xrep_refc_extent_cmp(const void *a, const void *b);

#endif /* __XFS_SCRUB_REFCOUNT_H__ */
