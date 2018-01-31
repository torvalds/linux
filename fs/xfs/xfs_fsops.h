/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_FSOPS_H__
#define	__XFS_FSOPS_H__

extern int xfs_growfs_data(xfs_mount_t *mp, xfs_growfs_data_t *in);
extern int xfs_growfs_log(xfs_mount_t *mp, xfs_growfs_log_t *in);
extern int xfs_fs_counts(xfs_mount_t *mp, xfs_fsop_counts_t *cnt);
extern int xfs_reserve_blocks(xfs_mount_t *mp, uint64_t *inval,
				xfs_fsop_resblks_t *outval);
extern int xfs_fs_goingdown(xfs_mount_t *mp, uint32_t inflags);

extern int xfs_fs_reserve_ag_blocks(struct xfs_mount *mp);
extern int xfs_fs_unreserve_ag_blocks(struct xfs_mount *mp);

#endif	/* __XFS_FSOPS_H__ */
