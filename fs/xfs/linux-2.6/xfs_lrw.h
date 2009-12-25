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

struct xfs_mount;
struct xfs_inode;
struct xfs_buf;

/* errors from xfsbdstrat() must be extracted from the buffer */
extern void xfsbdstrat(struct xfs_mount *, struct xfs_buf *);
extern int xfs_bdstrat_cb(struct xfs_buf *);
extern int xfs_dev_is_read_only(struct xfs_mount *, char *);

extern int xfs_zero_eof(struct xfs_inode *, xfs_off_t, xfs_fsize_t);

#endif	/* __XFS_LRW_H__ */
