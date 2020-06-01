// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_TRANS_PRIV_H__
#define	__XFS_TRANS_PRIV_H__

struct xfs_log_item;
struct xfs_mount;
struct xfs_trans;
struct xfs_ail;
struct xfs_log_vec;


void	xfs_trans_init(struct xfs_mount *);
void	xfs_trans_add_item(struct xfs_trans *, struct xfs_log_item *);
void	xfs_trans_del_item(struct xfs_log_item *);
void	xfs_trans_unreserve_and_mod_sb(struct xfs_trans *tp);

void	xfs_trans_committed_bulk(struct xfs_ail *ailp, struct xfs_log_vec *lv,
				xfs_lsn_t commit_lsn, bool aborted);
/*
 * AIL traversal cursor.
 *
 * Rather than using a generation number for detecting changes in the ail, use
 * a cursor that is protected by the ail lock. The aild cursor exists in the
 * struct xfs_ail, but other traversals can declare it on the stack and link it
 * to the ail list.
 *
 * When an object is deleted from or moved int the AIL, the cursor list is
 * searched to see if the object is a designated cursor item. If it is, it is
 * deleted from the cursor so that the next time the cursor is used traversal
 * will return to the start.
 *
 * This means a traversal colliding with a removal will cause a restart of the
 * list scan, rather than any insertion or deletion anywhere in the list. The
 * low bit of the item pointer is set if the cursor has been invalidated so
 * that we can tell the difference between invalidation and reaching the end
 * of the list to trigger traversal restarts.
 */
struct xfs_ail_cursor {
	struct list_head	list;
	struct xfs_log_item	*item;
};

/*
 * Private AIL structures.
 *
 * Eventually we need to drive the locking in here as well.
 */
struct xfs_ail {
	struct xfs_mount	*ail_mount;
	struct task_struct	*ail_task;
	struct list_head	ail_head;
	xfs_lsn_t		ail_target;
	xfs_lsn_t		ail_target_prev;
	struct list_head	ail_cursors;
	spinlock_t		ail_lock;
	xfs_lsn_t		ail_last_pushed_lsn;
	int			ail_log_flush;
	struct list_head	ail_buf_list;
	wait_queue_head_t	ail_empty;
};

/*
 * From xfs_trans_ail.c
 */
void	xfs_trans_ail_update_bulk(struct xfs_ail *ailp,
				struct xfs_ail_cursor *cur,
				struct xfs_log_item **log_items, int nr_items,
				xfs_lsn_t lsn) __releases(ailp->ail_lock);
/*
 * Return a pointer to the first item in the AIL.  If the AIL is empty, then
 * return NULL.
 */
static inline struct xfs_log_item *
xfs_ail_min(
	struct xfs_ail  *ailp)
{
	return list_first_entry_or_null(&ailp->ail_head, struct xfs_log_item,
					li_ail);
}

static inline void
xfs_trans_ail_update(
	struct xfs_ail		*ailp,
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn) __releases(ailp->ail_lock)
{
	xfs_trans_ail_update_bulk(ailp, NULL, &lip, 1, lsn);
}

xfs_lsn_t xfs_ail_delete_one(struct xfs_ail *ailp, struct xfs_log_item *lip);
void xfs_ail_update_finish(struct xfs_ail *ailp, xfs_lsn_t old_lsn)
			__releases(ailp->ail_lock);
void xfs_trans_ail_delete(struct xfs_ail *ailp, struct xfs_log_item *lip,
		int shutdown_type);

static inline void
xfs_trans_ail_remove(
	struct xfs_log_item	*lip,
	int			shutdown_type)
{
	struct xfs_ail		*ailp = lip->li_ailp;

	spin_lock(&ailp->ail_lock);
	/* xfs_trans_ail_delete() drops the AIL lock */
	if (test_bit(XFS_LI_IN_AIL, &lip->li_flags))
		xfs_trans_ail_delete(ailp, lip, shutdown_type);
	else
		spin_unlock(&ailp->ail_lock);
}

void			xfs_ail_push(struct xfs_ail *, xfs_lsn_t);
void			xfs_ail_push_all(struct xfs_ail *);
void			xfs_ail_push_all_sync(struct xfs_ail *);
struct xfs_log_item	*xfs_ail_min(struct xfs_ail  *ailp);
xfs_lsn_t		xfs_ail_min_lsn(struct xfs_ail *ailp);

struct xfs_log_item *	xfs_trans_ail_cursor_first(struct xfs_ail *ailp,
					struct xfs_ail_cursor *cur,
					xfs_lsn_t lsn);
struct xfs_log_item *	xfs_trans_ail_cursor_last(struct xfs_ail *ailp,
					struct xfs_ail_cursor *cur,
					xfs_lsn_t lsn);
struct xfs_log_item *	xfs_trans_ail_cursor_next(struct xfs_ail *ailp,
					struct xfs_ail_cursor *cur);
void			xfs_trans_ail_cursor_done(struct xfs_ail_cursor *cur);

#if BITS_PER_LONG != 64
static inline void
xfs_trans_ail_copy_lsn(
	struct xfs_ail	*ailp,
	xfs_lsn_t	*dst,
	xfs_lsn_t	*src)
{
	ASSERT(sizeof(xfs_lsn_t) == 8);	/* don't lock if it shrinks */
	spin_lock(&ailp->ail_lock);
	*dst = *src;
	spin_unlock(&ailp->ail_lock);
}
#else
static inline void
xfs_trans_ail_copy_lsn(
	struct xfs_ail	*ailp,
	xfs_lsn_t	*dst,
	xfs_lsn_t	*src)
{
	ASSERT(sizeof(xfs_lsn_t) == 8);
	*dst = *src;
}
#endif

static inline void
xfs_clear_li_failed(
	struct xfs_log_item	*lip)
{
	struct xfs_buf	*bp = lip->li_buf;

	ASSERT(test_bit(XFS_LI_IN_AIL, &lip->li_flags));
	lockdep_assert_held(&lip->li_ailp->ail_lock);

	if (test_and_clear_bit(XFS_LI_FAILED, &lip->li_flags)) {
		lip->li_buf = NULL;
		xfs_buf_rele(bp);
	}
}

static inline void
xfs_set_li_failed(
	struct xfs_log_item	*lip,
	struct xfs_buf		*bp)
{
	lockdep_assert_held(&lip->li_ailp->ail_lock);

	if (!test_and_set_bit(XFS_LI_FAILED, &lip->li_flags)) {
		xfs_buf_hold(bp);
		lip->li_buf = bp;
	}
}

#endif	/* __XFS_TRANS_PRIV_H__ */
