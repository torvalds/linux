/*
 * Copyright (c) 2008 Silicon Graphics, Inc.
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
#ifndef __XFS_IOCTL_H__
#define __XFS_IOCTL_H__

extern int
xfs_ioc_space(
	struct xfs_inode	*ip,
	struct inode		*inode,
	struct file		*filp,
	int			ioflags,
	unsigned int		cmd,
	xfs_flock64_t		*bf);

extern int
xfs_find_handle(
	unsigned int		cmd,
	xfs_fsop_handlereq_t	*hreq);

extern int
xfs_open_by_handle(
	xfs_mount_t		*mp,
	xfs_fsop_handlereq_t	*hreq,
	struct file		*parfilp,
	struct inode		*parinode);

extern int
xfs_readlink_by_handle(
	xfs_mount_t		*mp,
	xfs_fsop_handlereq_t	*hreq,
	struct inode		*parinode);
#endif
