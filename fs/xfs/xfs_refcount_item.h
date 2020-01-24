// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef	__XFS_REFCOUNT_ITEM_H__
#define	__XFS_REFCOUNT_ITEM_H__

/*
 * There are (currently) two pairs of refcount btree redo item types:
 * increase and decrease.  The log items for these are CUI (refcount
 * update intent) and CUD (refcount update done).  The redo item type
 * is encoded in the flags field of each xfs_map_extent.
 *
 * *I items should be recorded in the *first* of a series of rolled
 * transactions, and the *D items should be recorded in the same
 * transaction that records the associated refcountbt updates.
 *
 * Should the system crash after the commit of the first transaction
 * but before the commit of the final transaction in a series, log
 * recovery will use the redo information recorded by the intent items
 * to replay the refcountbt metadata updates.
 */

/* kernel only CUI/CUD definitions */

struct xfs_mount;
struct kmem_zone;

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_CUI_MAX_FAST_EXTENTS	16

/*
 * Define CUI flag bits. Manipulated by set/clear/test_bit operators.
 */
#define	XFS_CUI_RECOVERED		1

/*
 * This is the "refcount update intent" log item.  It is used to log
 * the fact that some reverse mappings need to change.  It is used in
 * conjunction with the "refcount update done" log item described
 * below.
 *
 * These log items follow the same rules as struct xfs_efi_log_item;
 * see the comments about that structure (in xfs_extfree_item.h) for
 * more details.
 */
struct xfs_cui_log_item {
	struct xfs_log_item		cui_item;
	atomic_t			cui_refcount;
	atomic_t			cui_next_extent;
	unsigned long			cui_flags;	/* misc flags */
	struct xfs_cui_log_format	cui_format;
};

static inline size_t
xfs_cui_log_item_sizeof(
	unsigned int		nr)
{
	return offsetof(struct xfs_cui_log_item, cui_format) +
			xfs_cui_log_format_sizeof(nr);
}

/*
 * This is the "refcount update done" log item.  It is used to log the
 * fact that some refcountbt updates mentioned in an earlier cui item
 * have been performed.
 */
struct xfs_cud_log_item {
	struct xfs_log_item		cud_item;
	struct xfs_cui_log_item		*cud_cuip;
	struct xfs_cud_log_format	cud_format;
};

extern struct kmem_zone	*xfs_cui_zone;
extern struct kmem_zone	*xfs_cud_zone;

struct xfs_cui_log_item *xfs_cui_init(struct xfs_mount *, uint);
void xfs_cui_item_free(struct xfs_cui_log_item *);
void xfs_cui_release(struct xfs_cui_log_item *);
int xfs_cui_recover(struct xfs_trans *parent_tp, struct xfs_cui_log_item *cuip);

#endif	/* __XFS_REFCOUNT_ITEM_H__ */
