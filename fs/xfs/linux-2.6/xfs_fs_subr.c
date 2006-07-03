/*
 * Copyright (c) 2000-2002,2005-2006 Silicon Graphics, Inc.
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

int  fs_noerr(void) { return 0; }
int  fs_nosys(void) { return ENOSYS; }
void fs_noval(void) { return; }

void
fs_tosspages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	bhv_vnode_t	*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = vn_to_inode(vp);

	if (VN_CACHED(vp))
		truncate_inode_pages(ip->i_mapping, first);
}

void
fs_flushinval_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	bhv_vnode_t	*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = vn_to_inode(vp);

	if (VN_CACHED(vp)) {
		if (VN_TRUNC(vp))
			VUNTRUNCATE(vp);
		filemap_write_and_wait(ip->i_mapping);
		truncate_inode_pages(ip->i_mapping, first);
	}
}

int
fs_flush_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	uint64_t	flags,
	int		fiopt)
{
	bhv_vnode_t	*vp = BHV_TO_VNODE(bdp);
	struct inode	*ip = vn_to_inode(vp);

	if (VN_DIRTY(vp)) {
		if (VN_TRUNC(vp))
			VUNTRUNCATE(vp);
		filemap_fdatawrite(ip->i_mapping);
		if (flags & XFS_B_ASYNC)
			return 0;
		filemap_fdatawait(ip->i_mapping);
	}
	return 0;
}
