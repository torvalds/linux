/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef	__XFS_BUF_ITEM_H__
#define	__XFS_BUF_ITEM_H__

/* kernel only definitions */

/* buf log item flags */
#define	XFS_BLI_HOLD		0x01
#define	XFS_BLI_DIRTY		0x02
#define	XFS_BLI_STALE		0x04
#define	XFS_BLI_LOGGED		0x08
#define	XFS_BLI_INODE_ALLOC_BUF	0x10
#define XFS_BLI_STALE_INODE	0x20
#define	XFS_BLI_INODE_BUF	0x40
#define	XFS_BLI_ORDERED		0x80

#define XFS_BLI_FLAGS \
	{ XFS_BLI_HOLD,		"HOLD" }, \
	{ XFS_BLI_DIRTY,	"DIRTY" }, \
	{ XFS_BLI_STALE,	"STALE" }, \
	{ XFS_BLI_LOGGED,	"LOGGED" }, \
	{ XFS_BLI_INODE_ALLOC_BUF, "INODE_ALLOC" }, \
	{ XFS_BLI_STALE_INODE,	"STALE_INODE" }, \
	{ XFS_BLI_INODE_BUF,	"INODE_BUF" }, \
	{ XFS_BLI_ORDERED,	"ORDERED" }


struct xfs_buf;
struct xfs_mount;
struct xfs_buf_log_item;

/*
 * This is the in core log item structure used to track information
 * needed to log buffers.  It tracks how many times the lock has been
 * locked, and which 128 byte chunks of the buffer are dirty.
 */
typedef struct xfs_buf_log_item {
	xfs_log_item_t		bli_item;	/* common item structure */
	struct xfs_buf		*bli_buf;	/* real buffer pointer */
	unsigned int		bli_flags;	/* misc flags */
	unsigned int		bli_recur;	/* lock recursion count */
	atomic_t		bli_refcount;	/* cnt of tp refs */
	int			bli_format_count;	/* count of headers */
	struct xfs_buf_log_format *bli_formats;	/* array of in-log header ptrs */
	struct xfs_buf_log_format __bli_format;	/* embedded in-log header */
} xfs_buf_log_item_t;

int	xfs_buf_item_init(struct xfs_buf *, struct xfs_mount *);
void	xfs_buf_item_relse(struct xfs_buf *);
void	xfs_buf_item_log(xfs_buf_log_item_t *, uint, uint);
uint	xfs_buf_item_dirty(xfs_buf_log_item_t *);
void	xfs_buf_attach_iodone(struct xfs_buf *,
			      void(*)(struct xfs_buf *, xfs_log_item_t *),
			      xfs_log_item_t *);
void	xfs_buf_iodone_callbacks(struct xfs_buf *);
void	xfs_buf_iodone(struct xfs_buf *, struct xfs_log_item *);
bool	xfs_buf_resubmit_failed_buffers(struct xfs_buf *,
					struct xfs_log_item *,
					struct list_head *);

extern kmem_zone_t	*xfs_buf_item_zone;

#endif	/* __XFS_BUF_ITEM_H__ */
