#ifndef XFS_SYNC_H
#define XFS_SYNC_H 1

int xfs_sync(struct xfs_mount *mp, int flags);
int xfs_syncsub(struct xfs_mount *mp, int flags, int *bypassed);

#endif
