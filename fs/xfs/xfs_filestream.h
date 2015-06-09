/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
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
#ifndef __XFS_FILESTREAM_H__
#define __XFS_FILESTREAM_H__

struct xfs_mount;
struct xfs_inode;
struct xfs_bmalloca;

int xfs_filestream_mount(struct xfs_mount *mp);
void xfs_filestream_unmount(struct xfs_mount *mp);
void xfs_filestream_deassociate(struct xfs_inode *ip);
xfs_agnumber_t xfs_filestream_lookup_ag(struct xfs_inode *ip);
int xfs_filestream_new_ag(struct xfs_bmalloca *ap, xfs_agnumber_t *agp);
int xfs_filestream_peek_ag(struct xfs_mount *mp, xfs_agnumber_t agno);

static inline int
xfs_inode_is_filestream(
	struct xfs_inode	*ip)
{
	return (ip->i_mount->m_flags & XFS_MOUNT_FILESTREAMS) ||
		(ip->i_d.di_flags & XFS_DIFLAG_FILESTREAM);
}

#endif /* __XFS_FILESTREAM_H__ */
