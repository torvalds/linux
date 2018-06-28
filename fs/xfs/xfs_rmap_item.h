// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef	__XFS_RMAP_ITEM_H__
#define	__XFS_RMAP_ITEM_H__

/*
 * There are (currently) three pairs of rmap btree redo item types: map, unmap,
 * and convert.  The common abbreviations for these are RUI (rmap update
 * intent) and RUD (rmap update done).  The redo item type is encoded in the
 * flags field of each xfs_map_extent.
 *
 * *I items should be recorded in the *first* of a series of rolled
 * transactions, and the *D items should be recorded in the same transaction
 * that records the associated rmapbt updates.  Typically, the first
 * transaction will record a bmbt update, followed by some number of
 * transactions containing rmapbt updates, and finally transactions with any
 * bnobt/cntbt updates.
 *
 * Should the system crash after the commit of the first transaction but
 * before the commit of the final transaction in a series, log recovery will
 * use the redo information recorded by the intent items to replay the
 * (rmapbt/bnobt/cntbt) metadata updates in the non-first transaction.
 */

/* kernel only RUI/RUD definitions */

struct xfs_mount;
struct kmem_zone;

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_RUI_MAX_FAST_EXTENTS	16

/*
 * Define RUI flag bits. Manipulated by set/clear/test_bit operators.
 */
#define	XFS_RUI_RECOVERED		1

/*
 * This is the "rmap update intent" log item.  It is used to log the fact that
 * some reverse mappings need to change.  It is used in conjunction with the
 * "rmap update done" log item described below.
 *
 * These log items follow the same rules as struct xfs_efi_log_item; see the
 * comments about that structure (in xfs_extfree_item.h) for more details.
 */
struct xfs_rui_log_item {
	struct xfs_log_item		rui_item;
	atomic_t			rui_refcount;
	atomic_t			rui_next_extent;
	unsigned long			rui_flags;	/* misc flags */
	struct xfs_rui_log_format	rui_format;
};

static inline size_t
xfs_rui_log_item_sizeof(
	unsigned int		nr)
{
	return offsetof(struct xfs_rui_log_item, rui_format) +
			xfs_rui_log_format_sizeof(nr);
}

/*
 * This is the "rmap update done" log item.  It is used to log the fact that
 * some rmapbt updates mentioned in an earlier rui item have been performed.
 */
struct xfs_rud_log_item {
	struct xfs_log_item		rud_item;
	struct xfs_rui_log_item		*rud_ruip;
	struct xfs_rud_log_format	rud_format;
};

extern struct kmem_zone	*xfs_rui_zone;
extern struct kmem_zone	*xfs_rud_zone;

struct xfs_rui_log_item *xfs_rui_init(struct xfs_mount *, uint);
struct xfs_rud_log_item *xfs_rud_init(struct xfs_mount *,
		struct xfs_rui_log_item *);
int xfs_rui_copy_format(struct xfs_log_iovec *buf,
		struct xfs_rui_log_format *dst_rui_fmt);
void xfs_rui_item_free(struct xfs_rui_log_item *);
void xfs_rui_release(struct xfs_rui_log_item *);
int xfs_rui_recover(struct xfs_mount *mp, struct xfs_rui_log_item *ruip);

#endif	/* __XFS_RMAP_ITEM_H__ */
