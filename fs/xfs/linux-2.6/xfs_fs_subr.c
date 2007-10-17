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
#include "xfs_vnodeops.h"

/*
 * The following six includes are needed so that we can include
 * xfs_inode.h.  What a mess..
 */
#include "xfs_bmap_btree.h"
#include "xfs_inum.h"
#include "xfs_dir2.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"

#include "xfs_inode.h"

int  fs_noerr(void) { return 0; }
int  fs_nosys(void) { return ENOSYS; }
void fs_noval(void) { return; }

void
xfs_tosspages(
	xfs_inode_t	*ip,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	bhv_vnode_t	*vp = XFS_ITOV(ip);
	struct inode	*inode = vn_to_inode(vp);

	if (VN_CACHED(vp))
		truncate_inode_pages(inode->i_mapping, first);
}

int
xfs_flushinval_pages(
	xfs_inode_t	*ip,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	bhv_vnode_t	*vp = XFS_ITOV(ip);
	struct inode	*inode = vn_to_inode(vp);
	int		ret = 0;

	if (VN_CACHED(vp)) {
		xfs_iflags_clear(ip, XFS_ITRUNCATED);
		ret = filemap_write_and_wait(inode->i_mapping);
		if (!ret)
			truncate_inode_pages(inode->i_mapping, first);
	}
	return ret;
}

int
xfs_flush_pages(
	xfs_inode_t	*ip,
	xfs_off_t	first,
	xfs_off_t	last,
	uint64_t	flags,
	int		fiopt)
{
	bhv_vnode_t	*vp = XFS_ITOV(ip);
	struct inode	*inode = vn_to_inode(vp);
	int		ret = 0;
	int		ret2;

	if (VN_DIRTY(vp)) {
		xfs_iflags_clear(ip, XFS_ITRUNCATED);
		ret = filemap_fdatawrite(inode->i_mapping);
		if (flags & XFS_B_ASYNC)
			return ret;
		ret2 = filemap_fdatawait(inode->i_mapping);
		if (!ret)
			ret = ret2;
	}
	return ret;
}
