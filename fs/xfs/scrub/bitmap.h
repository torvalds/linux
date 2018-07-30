// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_SCRUB_BITMAP_H__
#define __XFS_SCRUB_BITMAP_H__

struct xrep_extent {
	struct list_head	list;
	xfs_fsblock_t		fsbno;
	xfs_extlen_t		len;
};

struct xrep_extent_list {
	struct list_head	list;
};

static inline void
xrep_init_extent_list(
	struct xrep_extent_list		*exlist)
{
	INIT_LIST_HEAD(&exlist->list);
}

#define for_each_xrep_extent_safe(rbe, n, exlist) \
	list_for_each_entry_safe((rbe), (n), &(exlist)->list, list)
int xrep_collect_btree_extent(struct xfs_scrub *sc,
		struct xrep_extent_list *btlist, xfs_fsblock_t fsbno,
		xfs_extlen_t len);
void xrep_cancel_btree_extents(struct xfs_scrub *sc,
		struct xrep_extent_list *btlist);
int xrep_subtract_extents(struct xfs_scrub *sc,
		struct xrep_extent_list *exlist,
		struct xrep_extent_list *sublist);

#endif	/* __XFS_SCRUB_BITMAP_H__ */
