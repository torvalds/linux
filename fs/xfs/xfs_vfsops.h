#ifndef _XFS_VFSOPS_H
#define _XFS_VFSOPS_H 1

struct cred;
struct xfs_fid;
struct inode;
struct kstatfs;
struct xfs_mount;
struct xfs_mount_args;

void xfs_do_force_shutdown(struct xfs_mount *mp, int flags, char *fname,
		int lnnum);

#endif /* _XFS_VFSOPS_H */
