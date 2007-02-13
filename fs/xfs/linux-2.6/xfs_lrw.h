/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_LRW_H__
#define __XFS_LRW_H__

struct bhv_desc;
struct bhv_vnode;
struct xfs_mount;
struct xfs_iocore;
struct xfs_inode;
struct xfs_bmbt_irec;
struct xfs_buf;
struct xfs_iomap;

#if defined(XFS_RW_TRACE)
/*
 * Defines for the trace mechanisms in xfs_lrw.c.
 */
#define	XFS_RW_KTRACE_SIZE	128

#define	XFS_READ_ENTER		1
#define	XFS_WRITE_ENTER		2
#define XFS_IOMAP_READ_ENTER	3
#define	XFS_IOMAP_WRITE_ENTER	4
#define	XFS_IOMAP_READ_MAP	5
#define	XFS_IOMAP_WRITE_MAP	6
#define	XFS_IOMAP_WRITE_NOSPACE	7
#define	XFS_ITRUNC_START	8
#define	XFS_ITRUNC_FINISH1	9
#define	XFS_ITRUNC_FINISH2	10
#define	XFS_CTRUNC1		11
#define	XFS_CTRUNC2		12
#define	XFS_CTRUNC3		13
#define	XFS_CTRUNC4		14
#define	XFS_CTRUNC5		15
#define	XFS_CTRUNC6		16
#define	XFS_BUNMAP		17
#define	XFS_INVAL_CACHED	18
#define	XFS_DIORD_ENTER		19
#define	XFS_DIOWR_ENTER		20
#define	XFS_SENDFILE_ENTER	21
#define	XFS_WRITEPAGE_ENTER	22
#define	XFS_RELEASEPAGE_ENTER	23
#define	XFS_INVALIDPAGE_ENTER	24
#define	XFS_IOMAP_ALLOC_ENTER	25
#define	XFS_IOMAP_ALLOC_MAP	26
#define	XFS_IOMAP_UNWRITTEN	27
#define XFS_SPLICE_READ_ENTER	28
#define XFS_SPLICE_WRITE_ENTER	29
extern void xfs_rw_enter_trace(int, struct xfs_iocore *,
				void *, size_t, loff_t, int);
extern void xfs_inval_cached_trace(struct xfs_iocore *,
				xfs_off_t, xfs_off_t, xfs_off_t, xfs_off_t);
#else
#define xfs_rw_enter_trace(tag, io, data, size, offset, ioflags)
#define xfs_inval_cached_trace(io, offset, len, first, last)
#endif

/*
 * Maximum count of bmaps used by read and write paths.
 */
#define	XFS_MAX_RW_NBMAPS	4

extern int xfs_bmap(struct bhv_desc *, xfs_off_t, ssize_t, int,
			struct xfs_iomap *, int *);
extern int xfsbdstrat(struct xfs_mount *, struct xfs_buf *);
extern int xfs_bdstrat_cb(struct xfs_buf *);
extern int xfs_dev_is_read_only(struct xfs_mount *, char *);

extern int xfs_zero_eof(struct bhv_vnode *, struct xfs_iocore *, xfs_off_t,
				xfs_fsize_t);
extern ssize_t xfs_read(struct bhv_desc *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
extern ssize_t xfs_write(struct bhv_desc *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
extern ssize_t xfs_sendfile(struct bhv_desc *, struct file *,
				loff_t *, int, size_t, read_actor_t,
				void *, struct cred *);
extern ssize_t xfs_splice_read(struct bhv_desc *, struct file *, loff_t *,
				struct pipe_inode_info *, size_t, int, int,
				struct cred *);
extern ssize_t xfs_splice_write(struct bhv_desc *, struct pipe_inode_info *,
				struct file *, loff_t *, size_t, int, int,
				struct cred *);

#endif	/* __XFS_LRW_H__ */
