
#include "xfs_linux.h"
#include "xfs_vnodeops.h"

#include "xfs_bmap_btree.h"
#include "xfs_inode.h"

STATIC int
xfs_bhv_open(
	bhv_desc_t	*bdp,
	cred_t		*credp)
{
	return xfs_open(XFS_BHVTOI(bdp));
}

STATIC int
xfs_bhv_getattr(
	bhv_desc_t	*bdp,
	bhv_vattr_t	*vap,
	int		flags,
	cred_t		*credp)
{
	return xfs_getattr(XFS_BHVTOI(bdp), vap, flags);
}

int
xfs_bhv_setattr(
	bhv_desc_t		*bdp,
	bhv_vattr_t		*vap,
	int			flags,
	cred_t			*credp)
{
	return xfs_setattr(XFS_BHVTOI(bdp), vap, flags, credp);
}

STATIC int
xfs_bhv_access(
	bhv_desc_t	*bdp,
	int		mode,
	cred_t		*credp)
{
	return xfs_access(XFS_BHVTOI(bdp), mode, credp);
}

STATIC int
xfs_bhv_readlink(
	bhv_desc_t	*bdp,
	char		*link)
{
	return xfs_readlink(XFS_BHVTOI(bdp), link);
}

STATIC int
xfs_bhv_fsync(
	bhv_desc_t	*bdp,
	int		flag,
	cred_t		*credp,
	xfs_off_t	start,
	xfs_off_t	stop)
{
	return xfs_fsync(XFS_BHVTOI(bdp), flag, start, stop);
}

STATIC int
xfs_bhv_release(
	bhv_desc_t	*bdp)
{
	return xfs_release(XFS_BHVTOI(bdp));
}

STATIC int
xfs_bhv_inactive(
	bhv_desc_t	*bdp,
	cred_t		*credp)
{
	return xfs_inactive(XFS_BHVTOI(bdp));
}

STATIC int
xfs_bhv_lookup(
	bhv_desc_t		*dir_bdp,
	bhv_vname_t		*dentry,
	bhv_vnode_t		**vpp,
	int			flags,
	bhv_vnode_t		*rdir,
	cred_t			*credp)
{
	return xfs_lookup(XFS_BHVTOI(dir_bdp), dentry, vpp);
}

STATIC int
xfs_bhv_create(
	bhv_desc_t		*dir_bdp,
	bhv_vname_t		*dentry,
	bhv_vattr_t		*vap,
	bhv_vnode_t		**vpp,
	cred_t			*credp)
{
	return xfs_create(XFS_BHVTOI(dir_bdp), dentry, vap, vpp, credp);
}

STATIC int
xfs_bhv_remove(
	bhv_desc_t		*dir_bdp,
	bhv_vname_t		*dentry,
	cred_t			*credp)
{
	return xfs_remove(XFS_BHVTOI(dir_bdp), dentry);
}

STATIC int
xfs_bhv_link(
	bhv_desc_t		*target_dir_bdp,
	bhv_vnode_t		*src_vp,
	bhv_vname_t		*dentry,
	cred_t			*credp)
{
	return xfs_link(XFS_BHVTOI(target_dir_bdp), src_vp, dentry);
}

STATIC int
xfs_bhv_mkdir(
	bhv_desc_t		*dir_bdp,
	bhv_vname_t		*dentry,
	bhv_vattr_t		*vap,
	bhv_vnode_t		**vpp,
	cred_t			*credp)
{
	return xfs_mkdir(XFS_BHVTOI(dir_bdp), dentry, vap, vpp, credp);
}

STATIC int
xfs_bhv_rmdir(
	bhv_desc_t		*dir_bdp,
	bhv_vname_t		*dentry,
	cred_t			*credp)
{
	return xfs_rmdir(XFS_BHVTOI(dir_bdp), dentry);
}

STATIC int
xfs_bhv_readdir(
	bhv_desc_t	*dir_bdp,
	void		*dirent,
	size_t		bufsize,
	xfs_off_t	*offset,
	filldir_t	filldir)
{
	return xfs_readdir(XFS_BHVTOI(dir_bdp), dirent, bufsize, offset, filldir);
}

STATIC int
xfs_bhv_symlink(
	bhv_desc_t		*dir_bdp,
	bhv_vname_t		*dentry,
	bhv_vattr_t		*vap,
	char			*target_path,
	bhv_vnode_t		**vpp,
	cred_t			*credp)
{
	return xfs_symlink(XFS_BHVTOI(dir_bdp), dentry, vap, target_path, vpp, credp);
}

STATIC int
xfs_bhv_fid2(
	bhv_desc_t	*bdp,
	fid_t		*fidp)
{
	return xfs_fid2(XFS_BHVTOI(bdp), fidp);
}

STATIC int
xfs_bhv_rwlock(
	bhv_desc_t	*bdp,
	bhv_vrwlock_t	locktype)
{
	return xfs_rwlock(XFS_BHVTOI(bdp), locktype);
}

STATIC void
xfs_bhv_rwunlock(
	bhv_desc_t	*bdp,
	bhv_vrwlock_t	locktype)
{
	xfs_rwunlock(XFS_BHVTOI(bdp), locktype);
}

STATIC int
xfs_bhv_inode_flush(
	bhv_desc_t	*bdp,
	int		flags)
{
	return xfs_inode_flush(XFS_BHVTOI(bdp), flags);
}

STATIC int
xfs_bhv_reclaim(
	bhv_desc_t	*bdp)
{
	return xfs_reclaim(XFS_BHVTOI(bdp));
}

STATIC int
xfs_bhv_rename(
	bhv_desc_t	*src_dir_bdp,
	bhv_vname_t	*src_vname,
	bhv_vnode_t	*target_dir_vp,
	bhv_vname_t	*target_vname,
	cred_t		*credp)
{
	return xfs_rename(XFS_BHVTOI(src_dir_bdp), src_vname,
			target_dir_vp, target_vname);
}

STATIC int
xfs_bhv_attr_get(
	bhv_desc_t	*bdp,
	const char	*name,
	char		*value,
	int		*valuelenp,
	int		flags,
	cred_t		*cred)
{
	return xfs_attr_get(XFS_BHVTOI(bdp), name, value, valuelenp,
			flags, cred);
}

STATIC int
xfs_bhv_attr_set(
	bhv_desc_t	*bdp,
	const char	*name,
	char		*value,
	int		valuelen,
	int		flags,
	cred_t		*cred)
{
	return xfs_attr_set(XFS_BHVTOI(bdp), name, value, valuelen,
			flags);
}

STATIC int
xfs_bhv_attr_remove(
	bhv_desc_t	*bdp,
	const char	*name,
	int		flags,
	cred_t		*cred)
{
	return xfs_attr_remove(XFS_BHVTOI(bdp), name, flags);
}

STATIC int
xfs_bhv_attr_list(
	bhv_desc_t	*bdp,
	char		*buffer,
	int		bufsize,
	int		flags,
	struct attrlist_cursor_kern *cursor,
	cred_t		*cred)
{
	return xfs_attr_list(XFS_BHVTOI(bdp), buffer, bufsize, flags,
			cursor);
}

STATIC int
xfs_bhv_ioctl(
	bhv_desc_t		*bdp,
	struct inode		*inode,
	struct file		*filp,
	int			ioflags,
	unsigned int		cmd,
	void			__user *arg)
{
	return xfs_ioctl(XFS_BHVTOI(bdp), filp, ioflags, cmd, arg);
}

STATIC ssize_t
xfs_bhv_read(
	bhv_desc_t		*bdp,
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned int		segs,
	loff_t			*offset,
	int			ioflags,
	cred_t			*credp)
{
	return xfs_read(XFS_BHVTOI(bdp), iocb, iovp, segs,
			offset, ioflags);
}

STATIC ssize_t
xfs_bhv_sendfile(
	bhv_desc_t		*bdp,
	struct file		*filp,
	loff_t			*offset,
	int			ioflags,
	size_t			count,
	read_actor_t		actor,
	void			*target,
	cred_t			*credp)
{
	return xfs_sendfile(XFS_BHVTOI(bdp), filp, offset, ioflags,
			count, actor, target);
}

STATIC ssize_t
xfs_bhv_splice_read(
	bhv_desc_t		*bdp,
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			count,
	int			flags,
	int			ioflags,
	cred_t			*credp)
{
	return xfs_splice_read(XFS_BHVTOI(bdp), infilp, ppos, pipe,
			count, flags, ioflags);
}

STATIC ssize_t
xfs_bhv_splice_write(
	bhv_desc_t		*bdp,
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			count,
	int			flags,
	int			ioflags,
	cred_t			*credp)
{
	return xfs_splice_write(XFS_BHVTOI(bdp), pipe, outfilp, ppos,
			count, flags, ioflags);
}

STATIC ssize_t
xfs_bhv_write(
	bhv_desc_t		*bdp,
	struct kiocb		*iocb,
	const struct iovec	*iovp,
	unsigned int		nsegs,
	loff_t			*offset,
	int			ioflags,
	cred_t			*credp)
{
	return xfs_write(XFS_BHVTOI(bdp), iocb, iovp, nsegs, offset,
			ioflags);
}

STATIC int
xfs_bhv_bmap(bhv_desc_t	*bdp,
	xfs_off_t	offset,
	ssize_t		count,
	int		flags,
	struct xfs_iomap *iomapp,
	int		*niomaps)
{
	return xfs_bmap(XFS_BHVTOI(bdp), offset, count, flags,
			iomapp, niomaps);
}

STATIC void
fs_tosspages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	xfs_tosspages(XFS_BHVTOI(bdp), first, last, fiopt);
}

STATIC int
fs_flushinval_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	int		fiopt)
{
	return xfs_flushinval_pages(XFS_BHVTOI(bdp), first, last,
			fiopt);
}

STATIC int
fs_flush_pages(
	bhv_desc_t	*bdp,
	xfs_off_t	first,
	xfs_off_t	last,
	uint64_t	flags,
	int		fiopt)
{
	return xfs_flush_pages(XFS_BHVTOI(bdp), first, last, flags,
			fiopt);
}

bhv_vnodeops_t xfs_vnodeops = {
	BHV_IDENTITY_INIT(VN_BHV_XFS,VNODE_POSITION_XFS),
	.vop_open		= xfs_bhv_open,
	.vop_read		= xfs_bhv_read,
#ifdef HAVE_SENDFILE
	.vop_sendfile		= xfs_bhv_sendfile,
#endif
#ifdef HAVE_SPLICE
	.vop_splice_read	= xfs_bhv_splice_read,
	.vop_splice_write	= xfs_bhv_splice_write,
#endif
	.vop_write		= xfs_bhv_write,
	.vop_ioctl		= xfs_bhv_ioctl,
	.vop_getattr		= xfs_bhv_getattr,
	.vop_setattr		= xfs_bhv_setattr,
	.vop_access		= xfs_bhv_access,
	.vop_lookup		= xfs_bhv_lookup,
	.vop_create		= xfs_bhv_create,
	.vop_remove		= xfs_bhv_remove,
	.vop_link		= xfs_bhv_link,
	.vop_rename		= xfs_bhv_rename,
	.vop_mkdir		= xfs_bhv_mkdir,
	.vop_rmdir		= xfs_bhv_rmdir,
	.vop_readdir		= xfs_bhv_readdir,
	.vop_symlink		= xfs_bhv_symlink,
	.vop_readlink		= xfs_bhv_readlink,
	.vop_fsync		= xfs_bhv_fsync,
	.vop_inactive		= xfs_bhv_inactive,
	.vop_fid2		= xfs_bhv_fid2,
	.vop_rwlock		= xfs_bhv_rwlock,
	.vop_rwunlock		= xfs_bhv_rwunlock,
	.vop_bmap		= xfs_bhv_bmap,
	.vop_reclaim		= xfs_bhv_reclaim,
	.vop_attr_get		= xfs_bhv_attr_get,
	.vop_attr_set		= xfs_bhv_attr_set,
	.vop_attr_remove	= xfs_bhv_attr_remove,
	.vop_attr_list		= xfs_bhv_attr_list,
	.vop_link_removed	= (vop_link_removed_t)fs_noval,
	.vop_vnode_change	= (vop_vnode_change_t)fs_noval,
	.vop_tosspages		= fs_tosspages,
	.vop_flushinval_pages	= fs_flushinval_pages,
	.vop_flush_pages	= fs_flush_pages,
	.vop_release		= xfs_bhv_release,
	.vop_iflush		= xfs_bhv_inode_flush,
};
