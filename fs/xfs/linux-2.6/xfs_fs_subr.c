/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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

#include "xfs.h"

/*
 * Stub for no-op vnode operations that return error status.
 */
int
fs_noerr(void)
{
	return 0;
}

/*
 * Operation unsupported under this file system.
 */
int
fs_nosys(void)
{
	return ENOSYS;
}

/*
 * Stub for inactive, strategy, and read/write lock/unlock.  Does nothing.
 */
/* ARGSUSED */
void
fs_noval(void)
{
}

/*
 * vnode pcache layer for vnode_tosspages.
 * 'last' parameter unused but left in for IRIX compatibility
 */
void
fs_tosspages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = LINVFS_GET_IP(vp);

	if (VN_CACHED(vp))
		truncate_inode_pages(ip->i_mapping, first);
}


/*
 * vnode pcache layer for vnode_flushinval_pages.
 * 'last' parameter unused but left in for IRIX compatibility
 */
void
fs_flushinval_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = LINVFS_GET_IP(vp);

	if (VN_CACHED(vp)) {
		filemap_fdatawrite(ip->i_mapping);
		filemap_fdatawait(ip->i_mapping);

		truncate_inode_pages(ip->i_mapping, first);
	}
}

/*
 * vnode pcache layer for vnode_flush_pages.
 * 'last' parameter unused but left in for IRIX compatibility
 */
int
fs_flush_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	uint64_t	flags,
	int		fiopt)
{
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = LINVFS_GET_IP(vp);

	if (VN_CACHED(vp)) {
		filemap_fdatawrite(ip->i_mapping);
		if (flags & XFS_B_ASYNC)
			return 0;
		filemap_fdatawait(ip->i_mapping);
	}

	return 0;
}
