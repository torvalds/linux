#ifndef _XFS_VFSOPS_H
#define _XFS_VFSOPS_H 1

struct cred;
struct xfs_fid;
struct inode;
struct kstatfs;
struct xfs_mount;
struct xfs_mount_args;

int xfs_mount(struct xfs_mount *mp, struct xfs_mount_args *args,
		struct cred *credp);
int xfs_unmount(struct xfs_mount *mp, int flags, struct cred *credp);
int xfs_mntupdate(struct xfs_mount *mp, int *flags,
		struct xfs_mount_args *args);
int xfs_sync(struct xfs_mount *mp, int flags);
void xfs_do_force_shutdown(struct xfs_mount *mp, int flags, char *fname,
		int lnnum);
void xfs_attr_quiesce(struct xfs_mount *mp);

#endif /* _XFS_VFSOPS_H */
