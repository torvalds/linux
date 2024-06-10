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
 * Save a log intent item and a list of extents, so that we can replay
 * whatever action had to happen to the extent list and file the log done
 * item.
 */
struct xfs_defer_pending {
	struct list_head		dfp_list;	/* pending items */
	struct list_head		dfp_work;	/* work items */
	struct xfs_log_item		*dfp_intent;	/* log intent item */
	struct xfs_log_item		*dfp_done;	/* log done item */
	const struct xfs_defer_op_type	*dfp_ops;
	unsigned int			dfp_count;	/* # extent items */
	unsigned int			dfp_flags;
};

/*
 * Create a log intent item for this deferred item, but don't actually finish
 * the work.  Caller must clear this before the final transaction commit.
 */
#define XFS_DEFER_PAUSED	(1U << 0)

#define XFS_DEFER_PENDING_STRINGS \
	{ XFS_DEFER_PAUSED,	"paused" }

void xfs_defer_item_pause(struct xfs_trans *tp, struct xfs_defer_pending *dfp);
void xfs_defer_item_unpause(struct xfs_trans *tp, struct xfs_defer_pending *dfp);

struct xfs_defer_pending *xfs_defer_add(struct xfs_trans *tp, struct list_head *h,
		const struct xfs_defer_op_type *ops);
int xfs_defer_finish_noroll(struct xfs_trans **tp);
int xfs_defer_finish(struct xfs_trans **tp);
int xfs_defer_finish_one(struct xfs_trans *tp, struct xfs_defer_pending *dfp);
void xfs_defer_cancel(struct xfs_trans *);
void xfs_defer_move(struct xfs_trans *dtp, struct xfs_trans *stp);

/* Description of a deferred type. */
struct xfs_defer_op_type {
	const char		*name;
	unsigned int		max_items;
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
	int (*recover_work)(struct xfs_defer_pending *dfp,
			    struct list_head *capture_list);
	struct xfs_log_item *(*relog_intent)(struct xfs_trans *tp,
			struct xfs_log_item *intent,
			struct xfs_log_item *done_item);
};

extern const struct xfs_defer_op_type xfs_bmap_update_defer_type;
extern const struct xfs_defer_op_type xfs_refcount_update_defer_type;
extern const struct xfs_defer_op_type xfs_rmap_update_defer_type;
extern const struct xfs_defer_op_type xfs_extent_free_defer_type;
extern const struct xfs_defer_op_type xfs_agfl_free_defer_type;
extern const struct xfs_defer_op_type xfs_attr_defer_type;
extern const struct xfs_defer_op_type xfs_exchmaps_defer_type;

/*
 * Deferred operation item relogging limits.
 */

/*
 * Rename w/ parent pointers can require up to 5 inodes with deferred ops to
 * be joined to the transaction: src_dp, target_dp, src_ip, target_ip, and wip.
 * These inodes are locked in sorted order by their inode numbers
 */
#define XFS_DEFER_OPS_NR_INODES	5
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
void xfs_defer_ops_capture_abort(struct xfs_mount *mp,
		struct xfs_defer_capture *d);
void xfs_defer_resources_rele(struct xfs_defer_resources *dres);

void xfs_defer_start_recovery(struct xfs_log_item *lip,
		struct list_head *r_dfops, const struct xfs_defer_op_type *ops);
void xfs_defer_cancel_recovery(struct xfs_mount *mp,
		struct xfs_defer_pending *dfp);
int xfs_defer_finish_recovery(struct xfs_mount *mp,
		struct xfs_defer_pending *dfp, struct list_head *capture_list);

static inline void
xfs_defer_add_item(
	struct xfs_defer_pending	*dfp,
	struct list_head		*work)
{
	list_add_tail(work, &dfp->dfp_work);
	dfp->dfp_count++;
}

int __init xfs_defer_init_item_caches(void);
void xfs_defer_destroy_item_caches(void);

void xfs_defer_add_barrier(struct xfs_trans *tp);

#endif /* __XFS_DEFER_H__ */
