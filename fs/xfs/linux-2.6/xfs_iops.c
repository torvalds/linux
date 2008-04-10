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
#include "xfs_vnodeops.h"

#include <linux/capability.h>
#include <linux/xattr.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/falloc.h>

/*
 * Bring the atime in the XFS inode uptodate.
 * Used before logging the inode to disk or when the Linux inode goes away.
 */
void
xfs_synchronize_atime(
	xfs_inode_t	*ip)
{
	struct inode	*inode = ip->i_vnode;

	if (inode) {
		ip->i_d.di_atime.t_sec = (__int32_t)inode->i_atime.tv_sec;
		ip->i_d.di_atime.t_nsec = (__int32_t)inode->i_atime.tv_nsec;
	}
}

/*
 * If the linux inode exists, mark it dirty.
 * Used when commiting a dirty inode into a transaction so that
 * the inode will get written back by the linux code
 */
void
xfs_mark_inode_dirty_sync(
	xfs_inode_t	*ip)
{
	struct inode	*inode = ip->i_vnode;

	if (inode)
		mark_inode_dirty_sync(inode);
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
	if (!(inode->i_state & I_NEW))
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
	if (!(inode->i_state & I_NEW))
		mark_inode_dirty_sync(inode);
}


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */
STATIC void
xfs_validate_fields(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);
	loff_t size;

	/* we're under i_sem so i_size can't change under us */
	size = XFS_ISIZE(ip);
	if (i_size_read(inode) != size)
		i_size_write(inode, size);
}

/*
 * Hook in SELinux.  This is not quite correct yet, what we really need
 * here (as we do for default ACLs) is a mechanism by which creation of
 * these attrs can be journalled at inode creation time (along with the
 * inode, of course, such that log replay can't cause these to be lost).
 */
STATIC int
xfs_init_security(
	struct inode	*inode,
	struct inode	*dir)
{
	struct xfs_inode *ip = XFS_I(inode);
	size_t		length;
	void		*value;
	char		*name;
	int		error;

	error = security_inode_init_security(inode, dir, &name,
					     &value, &length);
	if (error) {
		if (error == -EOPNOTSUPP)
			return 0;
		return -error;
	}

	error = xfs_attr_set(ip, name, value, length, ATTR_SECURE);
	if (!error)
		xfs_iflags_set(ip, XFS_IMODIFIED);

	kfree(name);
	kfree(value);
	return error;
}

static void
xfs_dentry_to_name(
	struct xfs_name	*namep,
	struct dentry	*dentry)
{
	namep->name = dentry->d_name.name;
	namep->len = dentry->d_name.len;
}

STATIC void
xfs_cleanup_inode(
	struct inode	*dir,
	struct inode	*inode,
	struct dentry	*dentry,
	int		mode)
{
	struct xfs_name	teardown;

	/* Oh, the horror.
	 * If we can't add the ACL or we fail in
	 * xfs_init_security we must back out.
	 * ENOSPC can hit here, among other things.
	 */
	xfs_dentry_to_name(&teardown, dentry);

	if (S_ISDIR(mode))
		xfs_rmdir(XFS_I(dir), &teardown, XFS_I(inode));
	else
		xfs_remove(XFS_I(dir), &teardown, XFS_I(inode));
	iput(inode);
}

STATIC int
xfs_vn_mknod(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	dev_t		rdev)
{
	struct inode	*inode;
	struct xfs_inode *ip = NULL;
	xfs_acl_t	*default_acl = NULL;
	struct xfs_name	name;
	attrexists_t	test_default_acl = _ACL_DEFAULT_EXISTS;
	int		error;

	/*
	 * Irix uses Missed'em'V split, but doesn't want to see
	 * the upper 5 bits of (14bit) major.
	 */
	if (unlikely(!sysv_valid_dev(rdev) || MAJOR(rdev) & ~0x1ff))
		return -EINVAL;

	if (test_default_acl && test_default_acl(dir)) {
		if (!_ACL_ALLOC(default_acl)) {
			return -ENOMEM;
		}
		if (!_ACL_GET_DEFAULT(dir, default_acl)) {
			_ACL_FREE(default_acl);
			default_acl = NULL;
		}
	}

	xfs_dentry_to_name(&name, dentry);

	if (IS_POSIXACL(dir) && !default_acl)
		mode &= ~current->fs->umask;

	switch (mode & S_IFMT) {
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		rdev = sysv_encode_dev(rdev);
	case S_IFREG:
		error = xfs_create(XFS_I(dir), &name, mode, rdev, &ip, NULL);
		break;
	case S_IFDIR:
		error = xfs_mkdir(XFS_I(dir), &name, mode, &ip, NULL);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (unlikely(error))
		goto out_free_acl;

	inode = ip->i_vnode;

	error = xfs_init_security(inode, dir);
	if (unlikely(error))
		goto out_cleanup_inode;

	if (default_acl) {
		error = _ACL_INHERIT(inode, mode, default_acl);
		if (unlikely(error))
			goto out_cleanup_inode;
		xfs_iflags_set(ip, XFS_IMODIFIED);
		_ACL_FREE(default_acl);
	}


	if (S_ISDIR(mode))
		xfs_validate_fields(inode);
	d_instantiate(dentry, inode);
	xfs_validate_fields(dir);
	return -error;

 out_cleanup_inode:
	xfs_cleanup_inode(dir, inode, dentry, mode);
 out_free_acl:
	if (default_acl)
		_ACL_FREE(default_acl);
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
	struct xfs_inode *cip;
	struct xfs_name	name;
	int		error;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	xfs_dentry_to_name(&name, dentry);
	error = xfs_lookup(XFS_I(dir), &name, &cip);
	if (unlikely(error)) {
		if (unlikely(error != ENOENT))
			return ERR_PTR(-error);
		d_add(dentry, NULL);
		return NULL;
	}

	return d_splice_alias(cip->i_vnode, dentry);
}

STATIC int
xfs_vn_link(
	struct dentry	*old_dentry,
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode;	/* inode of guy being linked to */
	struct xfs_name	name;
	int		error;

	inode = old_dentry->d_inode;
	xfs_dentry_to_name(&name, dentry);

	igrab(inode);
	error = xfs_link(XFS_I(dir), XFS_I(inode), &name);
	if (unlikely(error)) {
		iput(inode);
		return -error;
	}

	xfs_iflags_set(XFS_I(dir), XFS_IMODIFIED);
	xfs_validate_fields(inode);
	d_instantiate(dentry, inode);
	return 0;
}

STATIC int
xfs_vn_unlink(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode;
	struct xfs_name	name;
	int		error;

	inode = dentry->d_inode;
	xfs_dentry_to_name(&name, dentry);

	error = xfs_remove(XFS_I(dir), &name, XFS_I(inode));
	if (likely(!error)) {
		xfs_validate_fields(dir);	/* size needs update */
		xfs_validate_fields(inode);
	}
	return -error;
}

STATIC int
xfs_vn_symlink(
	struct inode	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	struct inode	*inode;
	struct xfs_inode *cip = NULL;
	struct xfs_name	name;
	int		error;
	mode_t		mode;

	mode = S_IFLNK |
		(irix_symlink_mode ? 0777 & ~current->fs->umask : S_IRWXUGO);
	xfs_dentry_to_name(&name, dentry);

	error = xfs_symlink(XFS_I(dir), &name, symname, mode, &cip, NULL);
	if (unlikely(error))
		goto out;

	inode = cip->i_vnode;

	error = xfs_init_security(inode, dir);
	if (unlikely(error))
		goto out_cleanup_inode;

	d_instantiate(dentry, inode);
	xfs_validate_fields(dir);
	xfs_validate_fields(inode);
	return 0;

 out_cleanup_inode:
	xfs_cleanup_inode(dir, inode, dentry, 0);
 out:
	return -error;
}

STATIC int
xfs_vn_rmdir(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode = dentry->d_inode;
	struct xfs_name	name;
	int		error;

	xfs_dentry_to_name(&name, dentry);

	error = xfs_rmdir(XFS_I(dir), &name, XFS_I(inode));
	if (likely(!error)) {
		xfs_validate_fields(inode);
		xfs_validate_fields(dir);
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
	struct xfs_name	oname;
	struct xfs_name	nname;
	int		error;

	xfs_dentry_to_name(&oname, odentry);
	xfs_dentry_to_name(&nname, ndentry);

	error = xfs_rename(XFS_I(odir), &oname, XFS_I(odentry->d_inode),
							XFS_I(ndir), &nname);
	if (likely(!error)) {
		if (new_inode)
			xfs_validate_fields(new_inode);
		xfs_validate_fields(odir);
		if (ndir != odir)
			xfs_validate_fields(ndir);
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
	char			*link;
	int			error = -ENOMEM;

	link = kmalloc(MAXPATHLEN+1, GFP_KERNEL);
	if (!link)
		goto out_err;

	error = -xfs_readlink(XFS_I(dentry->d_inode), link);
	if (unlikely(error))
		goto out_kfree;

	nd_set_link(nd, link);
	return NULL;

 out_kfree:
	kfree(link);
 out_err:
	nd_set_link(nd, ERR_PTR(error));
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
xfs_check_acl(
	struct inode		*inode,
	int			mask)
{
	struct xfs_inode	*ip = XFS_I(inode);
	int			error;

	xfs_itrace_entry(ip);

	if (XFS_IFORK_Q(ip)) {
		error = xfs_acl_iaccess(ip, mask, NULL);
		if (error != -1)
			return -error;
	}

	return -EAGAIN;
}

STATIC int
xfs_vn_permission(
	struct inode		*inode,
	int			mask,
	struct nameidata	*nd)
{
	return generic_permission(inode, mask, xfs_check_acl);
}
#else
#define xfs_vn_permission NULL
#endif

STATIC int
xfs_vn_getattr(
	struct vfsmount		*mnt,
	struct dentry		*dentry,
	struct kstat		*stat)
{
	struct inode		*inode = dentry->d_inode;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;

	xfs_itrace_entry(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	stat->size = XFS_ISIZE(ip);
	stat->dev = inode->i_sb->s_dev;
	stat->mode = ip->i_d.di_mode;
	stat->nlink = ip->i_d.di_nlink;
	stat->uid = ip->i_d.di_uid;
	stat->gid = ip->i_d.di_gid;
	stat->ino = ip->i_ino;
#if XFS_BIG_INUMS
	stat->ino += mp->m_inoadd;
#endif
	stat->atime = inode->i_atime;
	stat->mtime.tv_sec = ip->i_d.di_mtime.t_sec;
	stat->mtime.tv_nsec = ip->i_d.di_mtime.t_nsec;
	stat->ctime.tv_sec = ip->i_d.di_ctime.t_sec;
	stat->ctime.tv_nsec = ip->i_d.di_ctime.t_nsec;
	stat->blocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);


	switch (inode->i_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		stat->blksize = BLKDEV_IOSIZE;
		stat->rdev = MKDEV(sysv_major(ip->i_df.if_u2.if_rdev) & 0x1ff,
				   sysv_minor(ip->i_df.if_u2.if_rdev));
		break;
	default:
		if (XFS_IS_REALTIME_INODE(ip)) {
			/*
			 * If the file blocks are being allocated from a
			 * realtime volume, then return the inode's realtime
			 * extent size or the realtime volume's extent size.
			 */
			stat->blksize =
				xfs_get_extsz_hint(ip) << mp->m_sb.sb_blocklog;
		} else
			stat->blksize = xfs_preferred_iosize(mp);
		stat->rdev = 0;
		break;
	}

	return 0;
}

STATIC int
xfs_vn_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	struct inode	*inode = dentry->d_inode;
	unsigned int	ia_valid = attr->ia_valid;
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

	error = xfs_setattr(XFS_I(inode), &vattr, flags, NULL);
	if (likely(!error))
		vn_revalidate(vn_from_inode(inode));
	return -error;
}

/*
 * block_truncate_page can return an error, but we can't propagate it
 * at all here. Leave a complaint + stack trace in the syslog because
 * this could be bad. If it is bad, we need to propagate the error further.
 */
STATIC void
xfs_vn_truncate(
	struct inode	*inode)
{
	int	error;
	error = block_truncate_page(inode->i_mapping, inode->i_size,
							xfs_get_blocks);
	WARN_ON(error);
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

STATIC long
xfs_vn_fallocate(
	struct inode	*inode,
	int		mode,
	loff_t		offset,
	loff_t		len)
{
	long		error;
	loff_t		new_size = 0;
	xfs_flock64_t	bf;
	xfs_inode_t	*ip = XFS_I(inode);

	/* preallocation on directories not yet supported */
	error = -ENODEV;
	if (S_ISDIR(inode->i_mode))
		goto out_error;

	bf.l_whence = 0;
	bf.l_start = offset;
	bf.l_len = len;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	error = xfs_change_file_space(ip, XFS_IOC_RESVSP, &bf,
						0, NULL, ATTR_NOLOCK);
	if (!error && !(mode & FALLOC_FL_KEEP_SIZE) &&
	    offset + len > i_size_read(inode))
		new_size = offset + len;

	/* Change file size if needed */
	if (new_size) {
		bhv_vattr_t	va;

		va.va_mask = XFS_AT_SIZE;
		va.va_size = new_size;
		error = xfs_setattr(ip, &va, ATTR_NOLOCK, NULL);
	}

	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
out_error:
	return error;
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
	.fallocate		= xfs_vn_fallocate,
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
