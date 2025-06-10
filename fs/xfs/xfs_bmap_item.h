// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef	__XFS_BMAP_ITEM_H__
#define	__XFS_BMAP_ITEM_H__

/*
 * There are (currently) two pairs of bmap btree redo item types: map & unmap.
 * The common abbreviations for these are BUI (bmap update intent) and BUD
 * (bmap update done).  The redo item type is encoded in the flags field of
 * each xfs_map_extent.
 *
 * *I items should be recorded in the *first* of a series of rolled
 * transactions, and the *D items should be recorded in the same transaction
 * that records the associated bmbt updates.
 *
 * Should the system crash after the commit of the first transaction but
 * before the commit of the final transaction in a series, log recovery will
 * use the redo information recorded by the intent items to replay the
 * bmbt metadata updates in the non-first transaction.
 */

/* kernel only BUI/BUD definitions */

struct xfs_mount;
struct kmem_cache;

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_BUI_MAX_FAST_EXTENTS	1

/*
 * This is the "bmap update intent" log item.  It is used to log the fact that
 * some reverse mappings need to change.  It is used in conjunction with the
 * "bmap update done" log item described below.
 *
 * These log items follow the same rules as struct xfs_efi_log_item; see the
 * comments about that structure (in xfs_extfree_item.h) for more details.
 */
struct xfs_bui_log_item {
	struct xfs_log_item		bui_item;
	atomic_t			bui_refcount;
	atomic_t			bui_next_extent;
	struct xfs_bui_log_format	bui_format;
};

static inline size_t
xfs_bui_log_item_sizeof(
	unsigned int		nr)
{
	return offsetof(struct xfs_bui_log_item, bui_format) +
			xfs_bui_log_format_sizeof(nr);
}

/*
 * This is the "bmap update done" log item.  It is used to log the fact that
 * some bmbt updates mentioned in an earlier bui item have been performed.
 */
struct xfs_bud_log_item {
	struct xfs_log_item		bud_item;
	struct xfs_bui_log_item		*bud_buip;
	struct xfs_bud_log_format	bud_format;
};

extern struct kmem_cache	*xfs_bui_cache;
extern struct kmem_cache	*xfs_bud_cache;

struct xfs_bmap_intent;

void xfs_bmap_defer_add(struct xfs_trans *tp, struct xfs_bmap_intent *bi);

unsigned int xfs_bui_log_space(unsigned int nr);
unsigned int xfs_bud_log_space(void);

#endif	/* __XFS_BMAP_ITEM_H__ */
