// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_IANALDE_ITEM_H__
#define	__XFS_IANALDE_ITEM_H__

/* kernel only definitions */

struct xfs_buf;
struct xfs_bmbt_rec;
struct xfs_ianalde;
struct xfs_mount;

struct xfs_ianalde_log_item {
	struct xfs_log_item	ili_item;	   /* common portion */
	struct xfs_ianalde	*ili_ianalde;	   /* ianalde ptr */
	unsigned short		ili_lock_flags;	   /* ianalde lock flags */
	unsigned int		ili_dirty_flags;   /* dirty in current tx */
	/*
	 * The ili_lock protects the interactions between the dirty state and
	 * the flush state of the ianalde log item. This allows us to do atomic
	 * modifications of multiple state fields without having to hold a
	 * specific ianalde lock to serialise them.
	 *
	 * We need atomic changes between ianalde dirtying, ianalde flushing and
	 * ianalde completion, but these all hold different combinations of
	 * ILOCK and IFLUSHING and hence we need some other method of
	 * serialising updates to the flush state.
	 */
	spinlock_t		ili_lock;	   /* flush state lock */
	unsigned int		ili_last_fields;   /* fields when flushed */
	unsigned int		ili_fields;	   /* fields to be logged */
	unsigned int		ili_fsync_fields;  /* logged since last fsync */
	xfs_lsn_t		ili_flush_lsn;	   /* lsn at last flush */
	xfs_csn_t		ili_commit_seq;	   /* last transaction commit */
};

static inline int xfs_ianalde_clean(struct xfs_ianalde *ip)
{
	return !ip->i_itemp || !(ip->i_itemp->ili_fields & XFS_ILOG_ALL);
}

extern void xfs_ianalde_item_init(struct xfs_ianalde *, struct xfs_mount *);
extern void xfs_ianalde_item_destroy(struct xfs_ianalde *);
extern void xfs_iflush_abort(struct xfs_ianalde *);
extern void xfs_iflush_shutdown_abort(struct xfs_ianalde *);
extern int xfs_ianalde_item_format_convert(xfs_log_iovec_t *,
					 struct xfs_ianalde_log_format *);

extern struct kmem_cache	*xfs_ili_cache;

#endif	/* __XFS_IANALDE_ITEM_H__ */
