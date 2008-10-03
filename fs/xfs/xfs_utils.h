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
#ifndef __XFS_UTILS_H__
#define __XFS_UTILS_H__

extern int xfs_truncate_file(xfs_mount_t *, xfs_inode_t *);
extern int xfs_dir_ialloc(xfs_trans_t **, xfs_inode_t *, mode_t, xfs_nlink_t,
				xfs_dev_t, cred_t *, prid_t, int,
				xfs_inode_t **, int *);
extern int xfs_droplink(xfs_trans_t *, xfs_inode_t *);
extern int xfs_bumplink(xfs_trans_t *, xfs_inode_t *);
extern void xfs_bump_ino_vers2(xfs_trans_t *, xfs_inode_t *);

#endif	/* __XFS_UTILS_H__ */
