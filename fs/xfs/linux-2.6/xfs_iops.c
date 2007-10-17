/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"

#include <linux/capability.h>
#include <linux/xattr.h>
#include <linux/namei.h>
#include <linux/security.h>

/*
 * Get a XFS inode from a given vnode.
 */
xfs_inode_t *
xfs_vtoi(
	bhv_vnode_t	*vp)
{
	bhv_desc_t      *bdp;

	bdp = bhv_lookup_range(VN_BHV_HEAD(vp),
			VNODE_POSITION_XFS, VNODE_POSITION_XFS);
	if (unlikely(bdp == NULL))
		return NULL;
	return XFS_BHVTOI(bdp);
}

/*
 * Bring the atime in the XFS inode uptodate.
 * Used before logging the inode to disk or when the Linux inode goes away.
 */
void
xfs_synchronize_atime(
	xfs_inode_t	*ip)
{
	bhv_vnode_t	*vp;

	vp = XFS_ITOV_NULL(ip);
	if (vp) {
		struct inode *inode = &vp->v_inode;
		ip->i_d.di_atime.t_sec = (__int32_t)inode->i_atime.tv_sec;
		ip->i_d.di_atime.t_nsec = (__int32_t)inode->i_atime.tv_nsec;
	}
}

/*
 * Change the requested timestamp in the given inode.
 * We don't lock across timestamp updates, and we don't log them but
 * we do record the fact that there is dirty information in core.
 *
 * NOTE -- callers MUST combine XFS_ICHGTIME_MOD or XFS_ICHGTIME_CHG
 *		with XFS_ICHGTIME_ACC to be sure that access time
 *		update will take.  Calling first with XFS_ICHGTIME_ACC
 *		and then XFS_ICHGTIME_MOD may fail to modify the access
 *		timestamp if the filesystem is mounted noacctm.
 */
void
xfs_ichgtime(
	xfs_inode_t	*ip,
	int		flags)
{
	struct inode	*inode = vn_to_inode(XFS_ITOV(ip));
	timespec_t	tv;

	nanotime(&tv);
	if (flags & XFS_ICHGTIME_MOD) {
		inode->i_mtime = tv;
		ip->i_d.di_mtime.t_sec = (__int32_t)tv.tv_sec;
		ip->i_d.di_mtime.t_nsec = (__int32_t)tv.tv_nsec;
	}
	if (flags & XFS_ICHGTIME_ACC) {
		inode->i_atime = tv;
		ip->i_d.di_atime.t_sec = (__int32_t)tv.tv_sec;
		ip->i_d.di_atime.t_nsec = (__int32_t)tv.tv_nsec;
	}
	if (flags & XFS_ICHGTIME_CHG) {
		inode->i_ctime = tv;
		ip->i_d.di_ctime.t_sec = (__int32_t)tv.tv_sec;
		ip->i_d.di_ctime.t_nsec = (__int32_t)tv.tv_nsec;
	}

	/*
	 * We update the i_update_core field _after_ changing
	 * the timestamps in order to coordinate properly with
	 * xfs_iflush() so that we don't lose timestamp updates.
	 * This keeps us from having to hold the inode lock
	 * while doing this.  We use the SYNCHRONIZE macro to
	 * ensure that the compiler does not reorder the update
	 * of i_update_core above the timestamp updates above.
	 */
	SYNCHRONIZE();
	ip->i_update_core = 1;
	if (!(inode->i_state & I_SYNC))
		mark_inode_dirty_sync(inode);
}

/*
 * Variant on the above which avoids querying the system clock
 * in situations where we know the Linux inode timestamps have
 * just been updated (and so we can update our inode cheaply).
 */
void
xfs_ichgtime_fast(
	xfs_inode_t	*ip,
	struct inode	*inode,
	int		flags)
{
	timespec_t	*tvp;

	/*
	 * Atime updates for read() & friends are handled lazily now, and
	 * explicit updates must go through xfs_ichgtime()
	 */
	ASSERT((flags & XFS_ICHGTIME_ACC) == 0);

	/*
	 * We're not supposed to change timestamps in readonly-mounted
	 * filesystems.  Throw it away if anyone asks us.
	 */
	if (unlikely(IS_RDONLY(inode)))
		return;

	if (flags & XFS_ICHGTIME_MOD) {
		tvp = &inode->i_mtime;
		ip->i_d.di_mtime.t_sec = (__int32_t)tvp->tv_sec;
		ip->i_d.di_mtime.t_nsec = (__int32_t)tvp->tv_nsec;
	}
	if (flags & XFS_ICHGTIME_CHG) {
		tvp = &inode->i_ctime;
		ip->i_d.di_ctime.t_sec = (__int32_t)tvp->tv_sec;
		ip->i_d.di_ctime.t_nsec = (__int32_t)tvp->tv_nsec;
	}

	/*
	 * We update the i_update_core field _after_ changing
	 * the timestamps in order to coordinate properly with
	 * xfs_iflush() so that we don't lose timestamp updates.
	 * This keeps us from having to hold the inode lock
	 * while doing this.  We use the SYNCHRONIZE macro to
	 * ensure that the compiler does not reorder the update
	 * of i_update_core above the timestamp updates above.
	 */
	SYNCHRONIZE();
	ip->i_update_core = 1;
	if (!(inode->i_state & I_SYNC))
		mark_inode_dirty_sync(inode);
}


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */
STATIC void
xfs_validate_fields(
	struct inode	*ip,
	bhv_vattr_t	*vattr)
{
	vattr->va_mask = XFS_AT_NLINK|XFS_AT_SIZE|XFS_AT_NBLOCKS;
	if (!bhv_vop_getattr(vn_from_inode(ip), vattr, ATTR_LAZY, NULL)) {
		ip->i_nlink = vattr->va_nlink;
		ip->i_blocks = vattr->va_nblocks;

		/* we're under i_sem so i_size can't change under us */
		if (i_size_read(ip) != vattr->va_size)
			i_size_write(ip, vattr->va_size);
	}
}

/*
 * Hook in SELinux.  This is not quite correct yet, what we really need
 * here (as we do for default ACLs) is a mechanism by which creation of
 * these attrs can be journalled at inode creation time (along with the
 * inode, of course, such that log replay can't cause these to be lost).
 */
STATIC int
xfs_init_security(
	bhv_vnode_t	*vp,
	struct inode	*dir)
{
	struct inode	*ip = vn_to_inode(vp);
	size_t		length;
	void		*value;
	char		*name;
	int		error;

	error = security_inode_init_security(ip, dir, &name, &value, &length);
	if (error) {
		if (error == -EOPNOTSUPP)
			return 0;
		return -error;
	}

	error = bhv_vop_attr_set(vp, name, value, length, ATTR_SECURE, NULL);
	if (!error)
		VMODIFY(vp);

	kfree(name);
	kfree(value);
	return error;
}

/*
 * Determine whether a process has a valid fs_struct (kernel daemons
 * like knfsd don't have an fs_struct).
 *
 * XXX(hch):  nfsd is broken, better fix it instead.
 */
STATIC_INLINE int
xfs_has_fs_struct(struct task_struct *task)
{
	return (task->fs != init_task.fs);
}

STATIC void
xfs_cleanup_inode(
	bhv_vnode_t	*dvp,
	bhv_vnode_t	*vp,
	struct dentry	*dentry,
	int		mode)
{
	struct dentry   teardown = {};

	/* Oh, the horror.
	 * If we can't add the ACL or we fail in
	 * xfs_init_security we must back out.
	 * ENOSPC can hit here, among other things.
	 */
	teardown.d_inode = vn_to_inode(vp);
	teardown.d_name = dentry->d_name;

	if (S_ISDIR(mode))
	  	bhv_vop_rmdir(dvp, &teardown, NULL);
	else
		bhv_vop_remove(dvp, &teardown, NULL);
	VN_RELE(vp);
}

STATIC int
xfs_vn_mknod(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	dev_t		rdev)
{
	struct inode	*ip;
	bhv_vattr_t	vattr = { 0 };
	bhv_vnode_t	*vp = NULL, *dvp = vn_from_inode(dir);
	xfs_acl_t	*default_acl = NULL;
	attrexists_t	test_default_acl = _ACL_DEFAULT_EXISTS;
	int		error;

	/*
	 * Irix uses Missed'em'V split, but doesn't want to see
	 * the upper 5 bits of (14bit) major.
	 */
	if (unlikely(!sysv_valid_dev(rdev) || MAJOR(rdev) & ~0x1ff))
		return -EINVAL;

	if (unlikely(test_default_acl && test_default_acl(dvp))) {
		if (!_ACL_ALLOC(default_acl)) {
			return -ENOMEM;
		}
		if (!_ACL_GET_DEFAULT(dvp, default_acl)) {
			_ACL_FREE(default_acl);
			default_acl = NULL;
		}
	}

	if (IS_POSIXACL(dir) && !default_acl && xfs_has_fs_struct(current))
		mode &= ~current->fs->umask;

	vattr.va_mask = XFS_AT_TYPE|XFS_AT_MODE;
	vattr.va_mode = mode;

	switch (mode & S_IFMT) {
	case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		vattr.va_rdev = sysv_encode_dev(rdev);
		vattr.va_mask |= XFS_AT_RDEV;
		/*FALLTHROUGH*/
	case S_IFREG:
		error = bhv_vop_create(dvp, dentry, &vattr, &vp, NULL);
		break;
	case S_IFDIR:
		error = bhv_vop_mkdir(dvp, dentry, &vattr, &vp, NULL);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (unlikely(!error)) {
		error = xfs_init_security(vp, dir);
		if (error)
			xfs_cleanup_inode(dvp, vp, dentry, mode);
	}

	if (unlikely(default_acl)) {
		if (!error) {
			error = _ACL_INHERIT(vp, &vattr, default_acl);
			if (!error)
				VMODIFY(vp);
			else
				xfs_cleanup_inode(dvp, vp, dentry, mode);
		}
		_ACL_FREE(default_acl);
	}

	if (likely(!error)) {
		ASSERT(vp);
		ip = vn_to_inode(vp);

		if (S_ISCHR(mode) || S_ISBLK(mode))
			ip->i_rdev = rdev;
		else if (S_ISDIR(mode))
			xfs_validate_fields(ip, &vattr);
		d_instantiate(dentry, ip);
		xfs_validate_fields(dir, &vattr);
	}
	return -error;
}

STATIC int
xfs_vn_create(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	struct nameidata *nd)
{
	return xfs_vn_mknod(dir, dentry, mode, 0);
}

STATIC int
xfs_vn_mkdir(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return xfs_vn_mknod(dir, dentry, mode|S_IFDIR, 0);
}

STATIC struct dentry *
xfs_vn_lookup(
	struct inode	*dir,
	struct dentry	*dentry,
	struct nameidata *nd)
{
	bhv_vnode_t	*vp = vn_from_inode(dir), *cvp;
	int		error;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	error = bhv_vop_lookup(vp, dentry, &cvp, 0, NULL, NULL);
	if (unlikely(error)) {
		if (unlikely(error != ENOENT))
			return ERR_PTR(-error);
		d_add(dentry, NULL);
		return NULL;
	}

	return d_splice_alias(vn_to_inode(cvp), dentry);
}

STATIC int
xfs_vn_link(
	struct dentry	*old_dentry,
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*ip;	/* inode of guy being linked to */
	bhv_vnode_t	*tdvp;	/* target directory for new name/link */
	bhv_vnode_t	*vp;	/* vp of name being linked */
	bhv_vattr_t	vattr;
	int		error;

	ip = old_dentry->d_inode;	/* inode being linked to */
	tdvp = vn_from_inode(dir);
	vp = vn_from_inode(ip);

	VN_HOLD(vp);
	error = bhv_vop_link(tdvp, vp, dentry, NULL);
	if (unlikely(error)) {
		VN_RELE(vp);
	} else {
		VMODIFY(tdvp);
		xfs_validate_fields(ip, &vattr);
		d_instantiate(dentry, ip);
	}
	return -error;
}

STATIC int
xfs_vn_unlink(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode;
	bhv_vnode_t	*dvp;	/* directory containing name to remove */
	bhv_vattr_t	vattr;
	int		error;

	inode = dentry->d_inode;
	dvp = vn_from_inode(dir);

	error = bhv_vop_remove(dvp, dentry, NULL);
	if (likely(!error)) {
		xfs_validate_fields(dir, &vattr);	/* size needs update */
		xfs_validate_fields(inode, &vattr);
	}
	return -error;
}

STATIC int
xfs_vn_symlink(
	struct inode	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	struct inode	*ip;
	bhv_vattr_t	va = { 0 };
	bhv_vnode_t	*dvp;	/* directory containing name of symlink */
	bhv_vnode_t	*cvp;	/* used to lookup symlink to put in dentry */
	int		error;

	dvp = vn_from_inode(dir);
	cvp = NULL;

	va.va_mode = S_IFLNK |
		(irix_symlink_mode ? 0777 & ~current->fs->umask : S_IRWXUGO);
	va.va_mask = XFS_AT_TYPE|XFS_AT_MODE;

	error = bhv_vop_symlink(dvp, dentry, &va, (char *)symname, &cvp, NULL);
	if (likely(!error && cvp)) {
		error = xfs_init_security(cvp, dir);
		if (likely(!error)) {
			ip = vn_to_inode(cvp);
			d_instantiate(dentry, ip);
			xfs_validate_fields(dir, &va);
			xfs_validate_fields(ip, &va);
		} else {
			xfs_cleanup_inode(dvp, cvp, dentry, 0);
		}
	}
	return -error;
}

STATIC int
xfs_vn_rmdir(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode = dentry->d_inode;
	bhv_vnode_t	*dvp = vn_from_inode(dir);
	bhv_vattr_t	vattr;
	int		error;

	error = bhv_vop_rmdir(dvp, dentry, NULL);
	if (likely(!error)) {
		xfs_validate_fields(inode, &vattr);
		xfs_validate_fields(dir, &vattr);
	}
	return -error;
}

STATIC int
xfs_vn_rename(
	struct inode	*odir,
	struct dentry	*odentry,
	struct inode	*ndir,
	struct dentry	*ndentry)
{
	struct inode	*new_inode = ndentry->d_inode;
	bhv_vnode_t	*fvp;	/* from directory */
	bhv_vnode_t	*tvp;	/* target directory */
	bhv_vattr_t	vattr;
	int		error;

	fvp = vn_from_inode(odir);
	tvp = vn_from_inode(ndir);

	error = bhv_vop_rename(fvp, odentry, tvp, ndentry, NULL);
	if (likely(!error)) {
		if (new_inode)
			xfs_validate_fields(new_inode, &vattr);
		xfs_validate_fields(odir, &vattr);
		if (ndir != odir)
			xfs_validate_fields(ndir, &vattr);
	}
	return -error;
}

/*
 * careful here - this function can get called recursively, so
 * we need to be very careful about how much stack we use.
 * uio is kmalloced for this reason...
 */
STATIC void *
xfs_vn_follow_link(
	struct dentry		*dentry,
	struct nameidata	*nd)
{
	bhv_vnode_t		*vp;
	uio_t			*uio;
	iovec_t			iov;
	int			error;
	char			*link;

	ASSERT(dentry);
	ASSERT(nd);

	link = kmalloc(MAXPATHLEN+1, GFP_KERNEL);
	if (!link) {
		nd_set_link(nd, ERR_PTR(-ENOMEM));
		return NULL;
	}

	uio = kmalloc(sizeof(uio_t), GFP_KERNEL);
	if (!uio) {
		kfree(link);
		nd_set_link(nd, ERR_PTR(-ENOMEM));
		return NULL;
	}

	vp = vn_from_inode(dentry->d_inode);

	iov.iov_base = link;
	iov.iov_len = MAXPATHLEN;

	uio->uio_iov = &iov;
	uio->uio_offset = 0;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_resid = MAXPATHLEN;
	uio->uio_iovcnt = 1;

	error = bhv_vop_readlink(vp, uio, 0, NULL);
	if (unlikely(error)) {
		kfree(link);
		link = ERR_PTR(-error);
	} else {
		link[MAXPATHLEN - uio->uio_resid] = '\0';
	}
	kfree(uio);

	nd_set_link(nd, link);
	return NULL;
}

STATIC void
xfs_vn_put_link(
	struct dentry	*dentry,
	struct nameidata *nd,
	void		*p)
{
	char		*s = nd_get_link(nd);

	if (!IS_ERR(s))
		kfree(s);
}

#ifdef CONFIG_XFS_POSIX_ACL
STATIC int
xfs_vn_permission(
	struct inode	*inode,
	int		mode,
	struct nameidata *nd)
{
	return -bhv_vop_access(vn_from_inode(inode), mode << 6, NULL);
}
#else
#define xfs_vn_permission NULL
#endif

STATIC int
xfs_vn_getattr(
	struct vfsmount	*mnt,
	struct dentry	*dentry,
	struct kstat	*stat)
{
	struct inode	*inode = dentry->d_inode;
	bhv_vnode_t	*vp = vn_from_inode(inode);
	bhv_vattr_t	vattr = { .va_mask = XFS_AT_STAT };
	int		error;

	error = bhv_vop_getattr(vp, &vattr, ATTR_LAZY, NULL);
	if (likely(!error)) {
		stat->size = i_size_read(inode);
		stat->dev = inode->i_sb->s_dev;
		stat->rdev = (vattr.va_rdev == 0) ? 0 :
				MKDEV(sysv_major(vattr.va_rdev) & 0x1ff,
				      sysv_minor(vattr.va_rdev));
		stat->mode = vattr.va_mode;
		stat->nlink = vattr.va_nlink;
		stat->uid = vattr.va_uid;
		stat->gid = vattr.va_gid;
		stat->ino = vattr.va_nodeid;
		stat->atime = vattr.va_atime;
		stat->mtime = vattr.va_mtime;
		stat->ctime = vattr.va_ctime;
		stat->blocks = vattr.va_nblocks;
		stat->blksize = vattr.va_blocksize;
	}
	return -error;
}

STATIC int
xfs_vn_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	struct inode	*inode = dentry->d_inode;
	unsigned int	ia_valid = attr->ia_valid;
	bhv_vnode_t	*vp = vn_from_inode(inode);
	bhv_vattr_t	vattr = { 0 };
	int		flags = 0;
	int		error;

	if (ia_valid & ATTR_UID) {
		vattr.va_mask |= XFS_AT_UID;
		vattr.va_uid = attr->ia_uid;
	}
	if (ia_valid & ATTR_GID) {
		vattr.va_mask |= XFS_AT_GID;
		vattr.va_gid = attr->ia_gid;
	}
	if (ia_valid & ATTR_SIZE) {
		vattr.va_mask |= XFS_AT_SIZE;
		vattr.va_size = attr->ia_size;
	}
	if (ia_valid & ATTR_ATIME) {
		vattr.va_mask |= XFS_AT_ATIME;
		vattr.va_atime = attr->ia_atime;
		inode->i_atime = attr->ia_atime;
	}
	if (ia_valid & ATTR_MTIME) {
		vattr.va_mask |= XFS_AT_MTIME;
		vattr.va_mtime = attr->ia_mtime;
	}
	if (ia_valid & ATTR_CTIME) {
		vattr.va_mask |= XFS_AT_CTIME;
		vattr.va_ctime = attr->ia_ctime;
	}
	if (ia_valid & ATTR_MODE) {
		vattr.va_mask |= XFS_AT_MODE;
		vattr.va_mode = attr->ia_mode;
		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			inode->i_mode &= ~S_ISGID;
	}

	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET))
		flags |= ATTR_UTIME;
#ifdef ATTR_NO_BLOCK
	if ((ia_valid & ATTR_NO_BLOCK))
		flags |= ATTR_NONBLOCK;
#endif

	error = bhv_vop_setattr(vp, &vattr, flags, NULL);
	if (likely(!error))
		__vn_revalidate(vp, &vattr);
	return -error;
}

STATIC void
xfs_vn_truncate(
	struct inode	*inode)
{
	block_truncate_page(inode->i_mapping, inode->i_size, xfs_get_blocks);
}

STATIC int
xfs_vn_setxattr(
	struct dentry	*dentry,
	const char	*name,
	const void	*data,
	size_t		size,
	int		flags)
{
	bhv_vnode_t	*vp = vn_from_inode(dentry->d_inode);
	char		*attr = (char *)name;
	attrnames_t	*namesp;
	int		xflags = 0;
	int		error;

	namesp = attr_lookup_namespace(attr, attr_namespaces, ATTR_NAMECOUNT);
	if (!namesp)
		return -EOPNOTSUPP;
	attr += namesp->attr_namelen;
	error = namesp->attr_capable(vp, NULL);
	if (error)
		return error;

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (flags & XATTR_CREATE)
		xflags |= ATTR_CREATE;
	if (flags & XATTR_REPLACE)
		xflags |= ATTR_REPLACE;
	xflags |= namesp->attr_flag;
	return namesp->attr_set(vp, attr, (void *)data, size, xflags);
}

STATIC ssize_t
xfs_vn_getxattr(
	struct dentry	*dentry,
	const char	*name,
	void		*data,
	size_t		size)
{
	bhv_vnode_t	*vp = vn_from_inode(dentry->d_inode);
	char		*attr = (char *)name;
	attrnames_t	*namesp;
	int		xflags = 0;
	ssize_t		error;

	namesp = attr_lookup_namespace(attr, attr_namespaces, ATTR_NAMECOUNT);
	if (!namesp)
		return -EOPNOTSUPP;
	attr += namesp->attr_namelen;
	error = namesp->attr_capable(vp, NULL);
	if (error)
		return error;

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (!size) {
		xflags |= ATTR_KERNOVAL;
		data = NULL;
	}
	xflags |= namesp->attr_flag;
	return namesp->attr_get(vp, attr, (void *)data, size, xflags);
}

STATIC ssize_t
xfs_vn_listxattr(
	struct dentry		*dentry,
	char			*data,
	size_t			size)
{
	bhv_vnode_t		*vp = vn_from_inode(dentry->d_inode);
	int			error, xflags = ATTR_KERNAMELS;
	ssize_t			result;

	if (!size)
		xflags |= ATTR_KERNOVAL;
	xflags |= capable(CAP_SYS_ADMIN) ? ATTR_KERNFULLS : ATTR_KERNORMALS;

	error = attr_generic_list(vp, data, size, xflags, &result);
	if (error < 0)
		return error;
	return result;
}

STATIC int
xfs_vn_removexattr(
	struct dentry	*dentry,
	const char	*name)
{
	bhv_vnode_t	*vp = vn_from_inode(dentry->d_inode);
	char		*attr = (char *)name;
	attrnames_t	*namesp;
	int		xflags = 0;
	int		error;

	namesp = attr_lookup_namespace(attr, attr_namespaces, ATTR_NAMECOUNT);
	if (!namesp)
		return -EOPNOTSUPP;
	attr += namesp->attr_namelen;
	error = namesp->attr_capable(vp, NULL);
	if (error)
		return error;
	xflags |= namesp->attr_flag;
	return namesp->attr_remove(vp, attr, xflags);
}


const struct inode_operations xfs_inode_operations = {
	.permission		= xfs_vn_permission,
	.truncate		= xfs_vn_truncate,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.setxattr		= xfs_vn_setxattr,
	.getxattr		= xfs_vn_getxattr,
	.listxattr		= xfs_vn_listxattr,
	.removexattr		= xfs_vn_removexattr,
};

const struct inode_operations xfs_dir_inode_operations = {
	.create			= xfs_vn_create,
	.lookup			= xfs_vn_lookup,
	.link			= xfs_vn_link,
	.unlink			= xfs_vn_unlink,
	.symlink		= xfs_vn_symlink,
	.mkdir			= xfs_vn_mkdir,
	.rmdir			= xfs_vn_rmdir,
	.mknod			= xfs_vn_mknod,
	.rename			= xfs_vn_rename,
	.permission		= xfs_vn_permission,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.setxattr		= xfs_vn_setxattr,
	.getxattr		= xfs_vn_getxattr,
	.listxattr		= xfs_vn_listxattr,
	.removexattr		= xfs_vn_removexattr,
};

const struct inode_operations xfs_symlink_inode_operations = {
	.readlink		= generic_readlink,
	.follow_link		= xfs_vn_follow_link,
	.put_link		= xfs_vn_put_link,
	.permission		= xfs_vn_permission,
	.getattr		= xfs_vn_getattr,
	.setattr		= xfs_vn_setattr,
	.setxattr		= xfs_vn_setxattr,
	.getxattr		= xfs_vn_getxattr,
	.listxattr		= xfs_vn_listxattr,
	.removexattr		= xfs_vn_removexattr,
};
