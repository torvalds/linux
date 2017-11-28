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
	struct file		*filp,
	unsigned int		cmd,
	xfs_flock64_t		*bf);

int
xfs_ioc_swapext(
	xfs_swapext_t	*sxp);

extern int
xfs_find_handle(
	unsigned int		cmd,
	xfs_fsop_handlereq_t	*hreq);

extern int
xfs_open_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq);

extern int
xfs_readlink_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq);

extern int
xfs_attrmulti_attr_get(
	struct inode		*inode,
	unsigned char		*name,
	unsigned char		__user *ubuf,
	uint32_t		*len,
	uint32_t		flags);

extern int
xfs_attrmulti_attr_set(
	struct inode		*inode,
	unsigned char		*name,
	const unsigned char	__user *ubuf,
	uint32_t		len,
	uint32_t		flags);

extern int
xfs_attrmulti_attr_remove(
	struct inode		*inode,
	unsigned char		*name,
	uint32_t		flags);

extern struct dentry *
xfs_handle_to_dentry(
	struct file		*parfilp,
	void __user		*uhandle,
	u32			hlen);

extern long
xfs_file_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p);

extern long
xfs_file_compat_ioctl(
	struct file		*file,
	unsigned int		cmd,
	unsigned long		arg);

extern int
xfs_set_dmattrs(
	struct xfs_inode	*ip,
	uint			evmask,
	uint16_t		state);

#endif
