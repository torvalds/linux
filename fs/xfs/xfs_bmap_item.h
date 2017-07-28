/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
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
struct kmem_zone;

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_BUI_MAX_FAST_EXTENTS	1

/*
 * Define BUI flag bits. Manipulated by set/clear/test_bit operators.
 */
#define	XFS_BUI_RECOVERED		1

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
	unsigned long			bui_flags;	/* misc flags */
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

extern struct kmem_zone	*xfs_bui_zone;
extern struct kmem_zone	*xfs_bud_zone;

struct xfs_bui_log_item *xfs_bui_init(struct xfs_mount *);
struct xfs_bud_log_item *xfs_bud_init(struct xfs_mount *,
		struct xfs_bui_log_item *);
void xfs_bui_item_free(struct xfs_bui_log_item *);
void xfs_bui_release(struct xfs_bui_log_item *);
int xfs_bui_recover(struct xfs_mount *mp, struct xfs_bui_log_item *buip);

#endif	/* __XFS_BMAP_ITEM_H__ */
