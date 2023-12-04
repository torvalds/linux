// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_FSOPS_H__
#define	__XFS_FSOPS_H__

int xfs_growfs_data(struct xfs_mount *mp, struct xfs_growfs_data *in);
int xfs_growfs_log(struct xfs_mount *mp, struct xfs_growfs_log *in);
int xfs_reserve_blocks(struct xfs_mount *mp, uint64_t request);
int xfs_fs_goingdown(struct xfs_mount *mp, uint32_t inflags);

int xfs_fs_reserve_ag_blocks(struct xfs_mount *mp);
int xfs_fs_unreserve_ag_blocks(struct xfs_mount *mp);

#endif	/* __XFS_FSOPS_H__ */
