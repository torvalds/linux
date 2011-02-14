#ifndef XFS_DISCARD_H
#define XFS_DISCARD_H 1

struct fstrim_range;

extern int	xfs_ioc_trim(struct xfs_mount *, struct fstrim_range __user *);

#endif /* XFS_DISCARD_H */
