
#include "xfs_linux.h"
#include "xfs_vfsops.h"

#include "xfs_inum.h"
#include "xfs_dmapi.h"
#include "xfs_sb.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_ag.h"
#include "xfs_mount.h"


STATIC int
xfs_bhv_mount(
	struct bhv_desc		*bhvp,
	struct xfs_mount_args	*args,
	cred_t			*credp)
{
	return xfs_mount(XFS_BHVTOM(bhvp), args, credp);
}

STATIC int
xfs_bhv_unmount(
	bhv_desc_t	*bdp,
	int		flags,
	cred_t		*credp)
{
	return xfs_unmount(XFS_BHVTOM(bdp), flags, credp);
}

STATIC int
xfs_bhv_mntupdate(
	bhv_desc_t			*bdp,
	int				*flags,
	struct xfs_mount_args		*args)
{
	return xfs_mntupdate(XFS_BHVTOM(bdp), flags, args);
}

STATIC int
xfs_bhv_root(
	bhv_desc_t	*bdp,
	bhv_vnode_t	**vpp)
{
	return xfs_root(XFS_BHVTOM(bdp), vpp);
}

STATIC int
xfs_bhv_statvfs(
	bhv_desc_t	*bdp,
	bhv_statvfs_t	*statp,
	bhv_vnode_t	*vp)
{
	return xfs_statvfs(XFS_BHVTOM(bdp), statp, vp);
}

STATIC int
xfs_bhv_sync(
	bhv_desc_t	*bdp,
	int		flags,
	cred_t		*credp)
{
	return xfs_sync(XFS_BHVTOM(bdp), flags);
}

STATIC int
xfs_bhv_vget(
	bhv_desc_t	*bdp,
	bhv_vnode_t	**vpp,
	fid_t		*fidp)
{
	return xfs_vget(XFS_BHVTOM(bdp), vpp, fidp);
}

STATIC int
xfs_bhv_parseargs(
	struct bhv_desc		*bhv,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	return xfs_parseargs(XFS_BHVTOM(bhv), options, args, update);
}

STATIC int
xfs_bhv_showargs(
	struct bhv_desc		*bhv,
	struct seq_file		*m)
{
	return xfs_showargs(XFS_BHVTOM(bhv), m);
}

STATIC void
xfs_bhv_freeze(
	bhv_desc_t	*bdp)
{
	return xfs_freeze(XFS_BHVTOM(bdp));
}

STATIC void
xfs_bhv_force_shutdown(
	bhv_desc_t	*bdp,
	int		flags,
	char		*fname,
	int		lnnum)
{
	return xfs_do_force_shutdown(XFS_BHVTOM(bdp), flags, fname, lnnum);
}

STATIC struct inode *
xfs_bhv_get_inode(
	bhv_desc_t	*bdp,
	xfs_ino_t	ino,
	int		flags)
{
	return xfs_get_inode(XFS_BHVTOM(bdp), ino, flags);
}

STATIC void
xfs_bhv_initialize_vnode(
	bhv_desc_t		*bdp,
	bhv_vnode_t		*vp,
	struct xfs_inode	*ip,
	int			unlock)
{
	return xfs_initialize_vnode(XFS_BHVTOM(bdp), vp, ip, unlock);
}

bhv_vfsops_t xfs_vfsops = {
	BHV_IDENTITY_INIT(VFS_BHV_XFS,VFS_POSITION_XFS),
	.vfs_parseargs		= xfs_bhv_parseargs,
	.vfs_showargs		= xfs_bhv_showargs,
	.vfs_mount		= xfs_bhv_mount,
	.vfs_unmount		= xfs_bhv_unmount,
	.vfs_mntupdate		= xfs_bhv_mntupdate,
	.vfs_root		= xfs_bhv_root,
	.vfs_statvfs		= xfs_bhv_statvfs,
	.vfs_sync		= xfs_bhv_sync,
	.vfs_vget		= xfs_bhv_vget,
	.vfs_get_inode		= xfs_bhv_get_inode,
	.vfs_init_vnode		= xfs_bhv_initialize_vnode,
	.vfs_force_shutdown	= xfs_bhv_force_shutdown,
	.vfs_freeze		= xfs_bhv_freeze,
};
