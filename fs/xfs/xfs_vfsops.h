#ifndef _XFS_VFSOPS_H
#define _XFS_VFSOPS_H 1

struct cred;
struct fid;
struct inode;
struct kstatfs;
struct xfs_mount;
struct xfs_mount_args;

int xfs_mount(struct xfs_mount *mp, struct xfs_mount_args *args,
		struct cred *credp);
int xfs_unmount(struct xfs_mount *mp, int flags, struct cred *credp);
int xfs_mntupdate(struct xfs_mount *mp, int *flags,
		struct xfs_mount_args *args);
int xfs_root(struct xfs_mount *mp, bhv_vnode_t **vpp);
int xfs_statvfs(struct xfs_mount *mp, struct kstatfs *statp,
		bhv_vnode_t *vp);
int xfs_sync(struct xfs_mount *mp, int flags);
int xfs_vget(struct xfs_mount *mp, bhv_vnode_t **vpp, struct fid *fidp);
int xfs_parseargs(struct xfs_mount *mp, char *options,
		struct xfs_mount_args *args, int update);
int xfs_showargs(struct xfs_mount *mp, struct seq_file *m);
void xfs_freeze(struct xfs_mount *mp);
void xfs_do_force_shutdown(struct xfs_mount *mp, int flags, char *fname,
		int lnnum);

#endif /* _XFS_VFSOPS_H */
