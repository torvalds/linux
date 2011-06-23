#ifndef XFS_DISCARD_H
#define XFS_DISCARD_H 1

struct fstrim_range;
struct list_head;

extern int	xfs_ioc_trim(struct xfs_mount *, struct fstrim_range __user *);
extern int	xfs_discard_extents(struct xfs_mount *, struct list_head *);

#endif /* XFS_DISCARD_H */
