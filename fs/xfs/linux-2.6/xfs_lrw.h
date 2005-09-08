/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_LRW_H__
#define __XFS_LRW_H__

struct vnode;
struct bhv_desc;
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
#define	XFS_BUNMAPI		17
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

extern int xfs_zero_eof(struct vnode *, struct xfs_iocore *, xfs_off_t,
				xfs_fsize_t, xfs_fsize_t);
extern ssize_t xfs_read(struct bhv_desc *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
extern ssize_t xfs_write(struct bhv_desc *, struct kiocb *,
				const struct iovec *, unsigned int,
				loff_t *, int, struct cred *);
extern ssize_t xfs_sendfile(struct bhv_desc *, struct file *,
				loff_t *, int, size_t, read_actor_t,
				void *, struct cred *);

extern int xfs_dev_is_read_only(struct xfs_mount *, char *);

#define XFS_FSB_TO_DB_IO(io,fsb) \
		(((io)->io_flags & XFS_IOCORE_RT) ? \
		 XFS_FSB_TO_BB((io)->io_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((io)->io_mount, (fsb)))

#endif	/* __XFS_LRW_H__ */
