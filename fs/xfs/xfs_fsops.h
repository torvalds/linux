// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
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
