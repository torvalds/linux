// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_BUF_ITEM_H__
#define	__XFS_BUF_ITEM_H__

/* kernel only definitions */

struct xfs_buf;
struct xfs_mount;

/* buf log item flags */
#define	XFS_BLI_HOLD		(1u << 0)
#define	XFS_BLI_DIRTY		(1u << 1)
#define	XFS_BLI_STALE		(1u << 2)
#define	XFS_BLI_LOGGED		(1u << 3)
#define	XFS_BLI_INODE_ALLOC_BUF	(1u << 4)
#define XFS_BLI_STALE_INODE	(1u << 5)
#define	XFS_BLI_INODE_BUF	(1u << 6)
#define	XFS_BLI_ORDERED		(1u << 7)

#define XFS_BLI_FLAGS \
	{ XFS_BLI_HOLD,		"HOLD" }, \
	{ XFS_BLI_DIRTY,	"DIRTY" }, \
	{ XFS_BLI_STALE,	"STALE" }, \
	{ XFS_BLI_LOGGED,	"LOGGED" }, \
	{ XFS_BLI_INODE_ALLOC_BUF, "INODE_ALLOC" }, \
	{ XFS_BLI_STALE_INODE,	"STALE_INODE" }, \
	{ XFS_BLI_INODE_BUF,	"INODE_BUF" }, \
	{ XFS_BLI_ORDERED,	"ORDERED" }

/*
 * This is the in core log item structure used to track information
 * needed to log buffers.  It tracks how many times the lock has been
 * locked, and which 128 byte chunks of the buffer are dirty.
 */
struct xfs_buf_log_item {
	struct xfs_log_item	bli_item;	/* common item structure */
	struct xfs_buf		*bli_buf;	/* real buffer pointer */
	unsigned int		bli_flags;	/* misc flags */
	unsigned int		bli_recur;	/* lock recursion count */
	atomic_t		bli_refcount;	/* cnt of tp refs */
	int			bli_format_count;	/* count of headers */
	struct xfs_buf_log_format *bli_formats;	/* array of in-log header ptrs */
	struct xfs_buf_log_format __bli_format;	/* embedded in-log header */
};

int	xfs_buf_item_init(struct xfs_buf *, struct xfs_mount *);
void	xfs_buf_item_done(struct xfs_buf *bp);
void	xfs_buf_item_put(struct xfs_buf_log_item *bip);
void	xfs_buf_item_log(struct xfs_buf_log_item *, uint, uint);
bool	xfs_buf_item_dirty_format(struct xfs_buf_log_item *);
void	xfs_buf_inode_iodone(struct xfs_buf *);
#ifdef CONFIG_XFS_QUOTA
void	xfs_buf_dquot_iodone(struct xfs_buf *);
#else
static inline void xfs_buf_dquot_iodone(struct xfs_buf *bp)
{
}
#endif /* CONFIG_XFS_QUOTA */
void	xfs_buf_iodone(struct xfs_buf *);
bool	xfs_buf_log_check_iovec(struct kvec *iovec);

unsigned int xfs_buf_inval_log_space(unsigned int map_count,
		unsigned int blocksize);

extern struct kmem_cache	*xfs_buf_item_cache;

#endif	/* __XFS_BUF_ITEM_H__ */
