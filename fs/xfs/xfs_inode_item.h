// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_INODE_ITEM_H__
#define	__XFS_INODE_ITEM_H__

/* kernel only definitions */

struct xfs_buf;
struct xfs_bmbt_rec;
struct xfs_inode;
struct xfs_mount;

struct xfs_inode_log_item {
	struct xfs_log_item	ili_item;	   /* common portion */
	struct xfs_inode	*ili_inode;	   /* inode ptr */
	unsigned short		ili_lock_flags;	   /* inode lock flags */
	/*
	 * The ili_lock protects the interactions between the dirty state and
	 * the flush state of the inode log item. This allows us to do atomic
	 * modifications of multiple state fields without having to hold a
	 * specific inode lock to serialise them.
	 *
	 * We need atomic changes between inode dirtying, inode flushing and
	 * inode completion, but these all hold different combinations of
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

static inline int xfs_inode_clean(struct xfs_inode *ip)
{
	return !ip->i_itemp || !(ip->i_itemp->ili_fields & XFS_ILOG_ALL);
}

extern void xfs_inode_item_init(struct xfs_inode *, struct xfs_mount *);
extern void xfs_inode_item_destroy(struct xfs_inode *);
extern void xfs_iflush_abort(struct xfs_inode *);
extern int xfs_inode_item_format_convert(xfs_log_iovec_t *,
					 struct xfs_inode_log_format *);

extern struct kmem_cache	*xfs_ili_cache;

#endif	/* __XFS_INODE_ITEM_H__ */
