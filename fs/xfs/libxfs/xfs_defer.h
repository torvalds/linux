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
#ifndef __XFS_DEFER_H__
#define	__XFS_DEFER_H__

struct xfs_defer_op_type;

/*
 * Save a log intent item and a list of extents, so that we can replay
 * whatever action had to happen to the extent list and file the log done
 * item.
 */
struct xfs_defer_pending {
	const struct xfs_defer_op_type	*dfp_type;	/* function pointers */
	struct list_head		dfp_list;	/* pending items */
	void				*dfp_intent;	/* log intent item */
	void				*dfp_done;	/* log done item */
	struct list_head		dfp_work;	/* work items */
	unsigned int			dfp_count;	/* # extent items */
};

/*
 * Header for deferred operation list.
 *
 * dop_low is used by the allocator to activate the lowspace algorithm -
 * when free space is running low the extent allocator may choose to
 * allocate an extent from an AG without leaving sufficient space for
 * a btree split when inserting the new extent.  In this case the allocator
 * will enable the lowspace algorithm which is supposed to allow further
 * allocations (such as btree splits and newroots) to allocate from
 * sequential AGs.  In order to avoid locking AGs out of order the lowspace
 * algorithm will start searching for free space from AG 0.  If the correct
 * transaction reservations have been made then this algorithm will eventually
 * find all the space it needs.
 */
enum xfs_defer_ops_type {
	XFS_DEFER_OPS_TYPE_BMAP,
	XFS_DEFER_OPS_TYPE_REFCOUNT,
	XFS_DEFER_OPS_TYPE_RMAP,
	XFS_DEFER_OPS_TYPE_FREE,
	XFS_DEFER_OPS_TYPE_MAX,
};

#define XFS_DEFER_OPS_NR_INODES	2	/* join up to two inodes */

struct xfs_defer_ops {
	bool			dop_committed;	/* did any trans commit? */
	bool			dop_low;	/* alloc in low mode */
	struct list_head	dop_intake;	/* unlogged pending work */
	struct list_head	dop_pending;	/* logged pending work */

	/* relog these inodes with each roll */
	struct xfs_inode	*dop_inodes[XFS_DEFER_OPS_NR_INODES];
};

void xfs_defer_add(struct xfs_defer_ops *dop, enum xfs_defer_ops_type type,
		struct list_head *h);
int xfs_defer_finish(struct xfs_trans **tp, struct xfs_defer_ops *dop,
		struct xfs_inode *ip);
void xfs_defer_cancel(struct xfs_defer_ops *dop);
void xfs_defer_init(struct xfs_defer_ops *dop, xfs_fsblock_t *fbp);
bool xfs_defer_has_unfinished_work(struct xfs_defer_ops *dop);
int xfs_defer_join(struct xfs_defer_ops *dop, struct xfs_inode *ip);

/* Description of a deferred type. */
struct xfs_defer_op_type {
	enum xfs_defer_ops_type	type;
	unsigned int		max_items;
	void (*abort_intent)(void *);
	void *(*create_done)(struct xfs_trans *, void *, unsigned int);
	int (*finish_item)(struct xfs_trans *, struct xfs_defer_ops *,
			struct list_head *, void *, void **);
	void (*finish_cleanup)(struct xfs_trans *, void *, int);
	void (*cancel_item)(struct list_head *);
	int (*diff_items)(void *, struct list_head *, struct list_head *);
	void *(*create_intent)(struct xfs_trans *, uint);
	void (*log_item)(struct xfs_trans *, void *, struct list_head *);
};

void xfs_defer_init_op_type(const struct xfs_defer_op_type *type);

#endif /* __XFS_DEFER_H__ */
