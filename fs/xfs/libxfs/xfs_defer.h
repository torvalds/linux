// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
 */
enum xfs_defer_ops_type {
	XFS_DEFER_OPS_TYPE_BMAP,
	XFS_DEFER_OPS_TYPE_REFCOUNT,
	XFS_DEFER_OPS_TYPE_RMAP,
	XFS_DEFER_OPS_TYPE_FREE,
	XFS_DEFER_OPS_TYPE_AGFL_FREE,
	XFS_DEFER_OPS_TYPE_MAX,
};

void xfs_defer_add(struct xfs_trans *tp, enum xfs_defer_ops_type type,
		struct list_head *h);
int xfs_defer_finish_noroll(struct xfs_trans **tp);
int xfs_defer_finish(struct xfs_trans **tp);
void xfs_defer_cancel(struct xfs_trans *);
void xfs_defer_move(struct xfs_trans *dtp, struct xfs_trans *stp);

/* Description of a deferred type. */
struct xfs_defer_op_type {
	enum xfs_defer_ops_type	type;
	unsigned int		max_items;
	void (*abort_intent)(void *);
	void *(*create_done)(struct xfs_trans *, void *, unsigned int);
	int (*finish_item)(struct xfs_trans *, struct list_head *, void *,
			void **);
	void (*finish_cleanup)(struct xfs_trans *, void *, int);
	void (*cancel_item)(struct list_head *);
	int (*diff_items)(void *, struct list_head *, struct list_head *);
	void *(*create_intent)(struct xfs_trans *, uint);
	void (*log_item)(struct xfs_trans *, void *, struct list_head *);
};

extern const struct xfs_defer_op_type xfs_bmap_update_defer_type;
extern const struct xfs_defer_op_type xfs_refcount_update_defer_type;
extern const struct xfs_defer_op_type xfs_rmap_update_defer_type;
extern const struct xfs_defer_op_type xfs_extent_free_defer_type;
extern const struct xfs_defer_op_type xfs_agfl_free_defer_type;

#endif /* __XFS_DEFER_H__ */
