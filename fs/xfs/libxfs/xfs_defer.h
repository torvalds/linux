/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_DEFER_H__
#define	__XFS_DEFER_H__

struct xfs_btree_cur;
struct xfs_defer_op_type;
struct xfs_defer_capture;

/*
 * Header for deferred operation list.
 */
enum xfs_defer_ops_type {
	XFS_DEFER_OPS_TYPE_BMAP,
	XFS_DEFER_OPS_TYPE_REFCOUNT,
	XFS_DEFER_OPS_TYPE_RMAP,
	XFS_DEFER_OPS_TYPE_FREE,
	XFS_DEFER_OPS_TYPE_AGFL_FREE,
	XFS_DEFER_OPS_TYPE_MAX,
};

/*
 * Save a log intent item and a list of extents, so that we can replay
 * whatever action had to happen to the extent list and file the log done
 * item.
 */
struct xfs_defer_pending {
	struct list_head		dfp_list;	/* pending items */
	struct list_head		dfp_work;	/* work items */
	struct xfs_log_item		*dfp_intent;	/* log intent item */
	struct xfs_log_item		*dfp_done;	/* log done item */
	unsigned int			dfp_count;	/* # extent items */
	enum xfs_defer_ops_type		dfp_type;
};

void xfs_defer_add(struct xfs_trans *tp, enum xfs_defer_ops_type type,
		struct list_head *h);
int xfs_defer_finish_noroll(struct xfs_trans **tp);
int xfs_defer_finish(struct xfs_trans **tp);
void xfs_defer_cancel(struct xfs_trans *);
void xfs_defer_move(struct xfs_trans *dtp, struct xfs_trans *stp);

/* Description of a deferred type. */
struct xfs_defer_op_type {
	struct xfs_log_item *(*create_intent)(struct xfs_trans *tp,
			struct list_head *items, unsigned int count, bool sort);
	void (*abort_intent)(struct xfs_log_item *intent);
	struct xfs_log_item *(*create_done)(struct xfs_trans *tp,
			struct xfs_log_item *intent, unsigned int count);
	int (*finish_item)(struct xfs_trans *tp, struct xfs_log_item *done,
			struct list_head *item, struct xfs_btree_cur **state);
	void (*finish_cleanup)(struct xfs_trans *tp,
			struct xfs_btree_cur *state, int error);
	void (*cancel_item)(struct list_head *item);
	unsigned int		max_items;
};

extern const struct xfs_defer_op_type xfs_bmap_update_defer_type;
extern const struct xfs_defer_op_type xfs_refcount_update_defer_type;
extern const struct xfs_defer_op_type xfs_rmap_update_defer_type;
extern const struct xfs_defer_op_type xfs_extent_free_defer_type;
extern const struct xfs_defer_op_type xfs_agfl_free_defer_type;

/*
 * Deferred operation item relogging limits.
 */
#define XFS_DEFER_OPS_NR_INODES	2	/* join up to two inodes */
#define XFS_DEFER_OPS_NR_BUFS	2	/* join up to two buffers */

/* Resources that must be held across a transaction roll. */
struct xfs_defer_resources {
	/* held buffers */
	struct xfs_buf		*dr_bp[XFS_DEFER_OPS_NR_BUFS];

	/* inodes with no unlock flags */
	struct xfs_inode	*dr_ip[XFS_DEFER_OPS_NR_INODES];

	/* number of held buffers */
	unsigned short		dr_bufs;

	/* bitmap of ordered buffers */
	unsigned short		dr_ordered;

	/* number of held inodes */
	unsigned short		dr_inos;
};

/*
 * This structure enables a dfops user to detach the chain of deferred
 * operations from a transaction so that they can be continued later.
 */
struct xfs_defer_capture {
	/* List of other capture structures. */
	struct list_head	dfc_list;

	/* Deferred ops state saved from the transaction. */
	struct list_head	dfc_dfops;
	unsigned int		dfc_tpflags;

	/* Block reservations for the data and rt devices. */
	unsigned int		dfc_blkres;
	unsigned int		dfc_rtxres;

	/* Log reservation saved from the transaction. */
	unsigned int		dfc_logres;

	struct xfs_defer_resources dfc_held;
};

/*
 * Functions to capture a chain of deferred operations and continue them later.
 * This doesn't normally happen except log recovery.
 */
int xfs_defer_ops_capture_and_commit(struct xfs_trans *tp,
		struct list_head *capture_list);
void xfs_defer_ops_continue(struct xfs_defer_capture *d, struct xfs_trans *tp,
		struct xfs_defer_resources *dres);
void xfs_defer_ops_capture_free(struct xfs_mount *mp,
		struct xfs_defer_capture *d);
void xfs_defer_resources_rele(struct xfs_defer_resources *dres);

int __init xfs_defer_init_item_caches(void);
void xfs_defer_destroy_item_caches(void);

#endif /* __XFS_DEFER_H__ */
