/* SPDX-License-Identifier: GPL-2.0 */
#ifndef XFS_DISCARD_H
#define XFS_DISCARD_H 1

struct fstrim_range;
struct xfs_mount;
struct xfs_busy_extents;

int xfs_discard_extents(struct xfs_mount *mp, struct xfs_busy_extents *busy);
int xfs_ioc_trim(struct xfs_mount *mp, struct fstrim_range __user *fstrim);

#endif /* XFS_DISCARD_H */
