/*
 * Copyright (c) 2012 Red Hat, Inc. All rights reserved.
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
#ifndef __XFS_SYMLINK_H
#define __XFS_SYMLINK_H 1

/*
 * The maximum pathlen is 1024 bytes. Since the minimum file system
 * blocksize is 512 bytes, we can get a max of 2 extents back from
 * bmapi.
 */
#define XFS_SYMLINK_MAPS 2

void xfs_symlink_local_to_remote(struct xfs_trans *tp, struct xfs_buf *bp,
				 struct xfs_inode *ip, struct xfs_ifork *ifp);

#ifdef __KERNEL__

int xfs_symlink(struct xfs_inode *dp, struct xfs_name *link_name,
		const char *target_path, umode_t mode, struct xfs_inode **ipp);
int xfs_readlink(struct xfs_inode *ip, char *link);
int xfs_inactive_symlink_rmt(struct xfs_inode *ip, struct xfs_trans **tpp);

#endif /* __KERNEL__ */
#endif /* __XFS_SYMLINK_H */
