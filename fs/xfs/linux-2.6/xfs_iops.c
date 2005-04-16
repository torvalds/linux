/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"

#include <linux/xattr.h>
#include <linux/namei.h>


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */
STATIC void
validate_fields(
	struct inode	*ip)
{
	vnode_t		*vp = LINVFS_GET_VP(ip);
	vattr_t		va;
	int		error;

	va.va_mask = XFS_AT_NLINK|XFS_AT_SIZE|XFS_AT_NBLOCKS;
	VOP_GETATTR(vp, &va, ATTR_LAZY, NULL, error);
	if (likely(!error)) {
		ip->i_nlink = va.va_nlink;
		ip->i_blocks = va.va_nblocks;

		/* we're under i_sem so i_size can't change under us */
		if (i_size_read(ip) != va.va_size)
			i_size_write(ip, va.va_size);
	}
}

/*
 * Determine whether a process has a valid fs_struct (kernel daemons
 * like knfsd don't have an fs_struct).
 *
 * XXX(hch):  nfsd is broken, better fix it instead.
 */
STATIC inline int
has_fs_struct(struct task_struct *task)
{
	return (task->fs != init_task.fs);
}

STATIC int
linvfs_mknod(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	dev_t		rdev)
{
	struct inode	*ip;
	vattr_t		va;
	vnode_t		*vp = NULL, *dvp = LINVFS_GET_VP(dir);
	xfs_acl_t	*default_acl = NULL;
	attrexists_t	test_default_acl = _ACL_DEFAULT_EXISTS;
	int		error;

	/*
	 * Irix uses Missed'em'V split, but doesn't want to see
	 * the upper 5 bits of (14bit) major.
	 */
	if (!sysv_valid_dev(rdev) || MAJOR(rdev) & ~0x1ff)
		return -EINVAL;

	if (test_default_acl && test_default_acl(dvp)) {
		if (!_ACL_ALLOC(default_acl))
			return -ENOMEM;
		if (!_ACL_GET_DEFAULT(dvp, default_acl)) {
			_ACL_FREE(default_acl);
			default_acl = NULL;
		}
	}

	if (IS_POSIXACL(dir) && !default_acl && has_fs_struct(current))
		mode &= ~current->fs->umask;

	memset(&va, 0, sizeof(va));
	va.va_mask = XFS_AT_TYPE|XFS_AT_MODE;
	va.va_type = IFTOVT(mode);
	va.va_mode = mode;

	switch (mode & S_IFMT) {
	case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		va.va_rdev = sysv_encode_dev(rdev);
		va.va_mask |= XFS_AT_RDEV;
		/*FALLTHROUGH*/
	case S_IFREG:
		VOP_CREATE(dvp, dentry, &va, &vp, NULL, error);
		break;
	case S_IFDIR:
		VOP_MKDIR(dvp, dentry, &va, &vp, NULL, error);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (default_acl) {
		if (!error) {
			error = _ACL_INHERIT(vp, &va, default_acl);
			if (!error) {
				VMODIFY(vp);
			} else {
				struct dentry	teardown = {};
				int		err2;

				/* Oh, the horror.
				 * If we can't add the ACL we must back out.
				 * ENOSPC can hit here, among other things.
				 */
				teardown.d_inode = ip = LINVFS_GET_IP(vp);
				teardown.d_name = dentry->d_name;

				vn_mark_bad(vp);
				
				if (S_ISDIR(mode))
					VOP_RMDIR(dvp, &teardown, NULL, err2);
				else
					VOP_REMOVE(dvp, &teardown, NULL, err2);
				VN_RELE(vp);
			}
		}
		_ACL_FREE(default_acl);
	}

	if (!error) {
		ASSERT(vp);
		ip = LINVFS_GET_IP(vp);

		if (S_ISCHR(mode) || S_ISBLK(mode))
			ip->i_rdev = rdev;
		else if (S_ISDIR(mode))
			validate_fields(ip);
		d_instantiate(dentry, ip);
		validate_fields(dir);
	}
	return -error;
}

STATIC int
linvfs_create(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	struct nameidata *nd)
{
	return linvfs_mknod(dir, dentry, mode, 0);
}

STATIC int
linvfs_mkdir(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return linvfs_mknod(dir, dentry, mode|S_IFDIR, 0);
}

STATIC struct dentry *
linvfs_lookup(
	struct inode	*dir,
	struct dentry	*dentry,
	struct nameidata *nd)
{
	struct vnode	*vp = LINVFS_GET_VP(dir), *cvp;
	int		error;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	VOP_LOOKUP(vp, dentry, &cvp, 0, NULL, NULL, error);
	if (error) {
		if (unlikely(error != ENOENT))
			return ERR_PTR(-error);
		d_add(dentry, NULL);
		return NULL;
	}

	return d_splice_alias(LINVFS_GET_IP(cvp), dentry);
}

STATIC int
linvfs_link(
	struct dentry	*old_dentry,
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*ip;	/* inode of guy being linked to */
	vnode_t		*tdvp;	/* target directory for new name/link */
	vnode_t		*vp;	/* vp of name being linked */
	int		error;

	ip = old_dentry->d_inode;	/* inode being linked to */
	if (S_ISDIR(ip->i_mode))
		return -EPERM;

	tdvp = LINVFS_GET_VP(dir);
	vp = LINVFS_GET_VP(ip);

	VOP_LINK(tdvp, vp, dentry, NULL, error);
	if (!error) {
		VMODIFY(tdvp);
		VN_HOLD(vp);
		validate_fields(ip);
		d_instantiate(dentry, ip);
	}
	return -error;
}

STATIC int
linvfs_unlink(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode;
	vnode_t		*dvp;	/* directory containing name to remove */
	int		error;

	inode = dentry->d_inode;
	dvp = LINVFS_GET_VP(dir);

	VOP_REMOVE(dvp, dentry, NULL, error);
	if (!error) {
		validate_fields(dir);	/* For size only */
		validate_fields(inode);
	}

	return -error;
}

STATIC int
linvfs_symlink(
	struct inode	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	struct inode	*ip;
	vattr_t		va;
	vnode_t		*dvp;	/* directory containing name of symlink */
	vnode_t		*cvp;	/* used to lookup symlink to put in dentry */
	int		error;

	dvp = LINVFS_GET_VP(dir);
	cvp = NULL;

	memset(&va, 0, sizeof(va));
	va.va_type = VLNK;
	va.va_mode = irix_symlink_mode ? 0777 & ~current->fs->umask : S_IRWXUGO;
	va.va_mask = XFS_AT_TYPE|XFS_AT_MODE;

	error = 0;
	VOP_SYMLINK(dvp, dentry, &va, (char *)symname, &cvp, NULL, error);
	if (!error && cvp) {
		ASSERT(cvp->v_type == VLNK);
		ip = LINVFS_GET_IP(cvp);
		d_instantiate(dentry, ip);
		validate_fields(dir);
		validate_fields(ip); /* size needs update */
	}
	return -error;
}

STATIC int
linvfs_rmdir(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*dvp = LINVFS_GET_VP(dir);
	int		error;

	VOP_RMDIR(dvp, dentry, NULL, error);
	if (!error) {
		validate_fields(inode);
		validate_fields(dir);
	}
	return -error;
}

STATIC int
linvfs_rename(
	struct inode	*odir,
	struct dentry	*odentry,
	struct inode	*ndir,
	struct dentry	*ndentry)
{
	struct inode	*new_inode = ndentry->d_inode;
	vnode_t		*fvp;	/* from directory */
	vnode_t		*tvp;	/* target directory */
	int		error;

	fvp = LINVFS_GET_VP(odir);
	tvp = LINVFS_GET_VP(ndir);

	VOP_RENAME(fvp, odentry, tvp, ndentry, NULL, error);
	if (error)
		return -error;

	if (new_inode)
		validate_fields(new_inode);

	validate_fields(odir);
	if (ndir != odir)
		validate_fields(ndir);
	return 0;
}

/*
 * careful here - this function can get called recursively, so
 * we need to be very careful about how much stack we use.
 * uio is kmalloced for this reason...
 */
STATIC int
linvfs_follow_link(
	struct dentry		*dentry,
	struct nameidata	*nd)
{
	vnode_t			*vp;
	uio_t			*uio;
	iovec_t			iov;
	int			error;
	char			*link;

	ASSERT(dentry);
	ASSERT(nd);

	link = (char *)kmalloc(MAXNAMELEN+1, GFP_KERNEL);
	if (!link) {
		nd_set_link(nd, ERR_PTR(-ENOMEM));
		return 0;
	}

	uio = (uio_t *)kmalloc(sizeof(uio_t), GFP_KERNEL);
	if (!uio) {
		kfree(link);
		nd_set_link(nd, ERR_PTR(-ENOMEM));
		return 0;
	}

	vp = LINVFS_GET_VP(dentry->d_inode);

	iov.iov_base = link;
	iov.iov_len = MAXNAMELEN;

	uio->uio_iov = &iov;
	uio->uio_offset = 0;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_resid = MAXNAMELEN;
	uio->uio_iovcnt = 1;

	VOP_READLINK(vp, uio, 0, NULL, error);
	if (error) {
		kfree(link);
		link = ERR_PTR(-error);
	} else {
		link[MAXNAMELEN - uio->uio_resid] = '\0';
	}
	kfree(uio);

	nd_set_link(nd, link);
	return 0;
}

static void linvfs_put_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s = nd_get_link(nd);
	if (!IS_ERR(s))
		kfree(s);
}

#ifdef CONFIG_XFS_POSIX_ACL
STATIC int
linvfs_permission(
	struct inode	*inode,
	int		mode,
	struct nameidata *nd)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;

	mode <<= 6;		/* convert from linux to vnode access bits */
	VOP_ACCESS(vp, mode, NULL, error);
	return -error;
}
#else
#define linvfs_permission NULL
#endif

STATIC int
linvfs_getattr(
	struct vfsmount	*mnt,
	struct dentry	*dentry,
	struct kstat	*stat)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error = 0;

	if (unlikely(vp->v_flag & VMODIFIED))
		error = vn_revalidate(vp);
	if (!error)
		generic_fillattr(inode, stat);
	return 0;
}

STATIC int
linvfs_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	struct inode	*inode = dentry->d_inode;
	unsigned int	ia_valid = attr->ia_valid;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vattr_t		vattr;
	int		flags = 0;
	int		error;

	memset(&vattr, 0, sizeof(vattr_t));
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

	VOP_SETATTR(vp, &vattr, flags, NULL, error);
	if (error)
		return -error;
	vn_revalidate(vp);
	return error;
}

STATIC void
linvfs_truncate(
	struct inode	*inode)
{
	block_truncate_page(inode->i_mapping, inode->i_size, linvfs_get_block);
}

STATIC int
linvfs_setxattr(
	struct dentry	*dentry,
	const char	*name,
	const void	*data,
	size_t		size,
	int		flags)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);
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
linvfs_getxattr(
	struct dentry	*dentry,
	const char	*name,
	void		*data,
	size_t		size)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);
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
linvfs_listxattr(
	struct dentry		*dentry,
	char			*data,
	size_t			size)
{
	vnode_t			*vp = LINVFS_GET_VP(dentry->d_inode);
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
linvfs_removexattr(
	struct dentry	*dentry,
	const char	*name)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);
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


struct inode_operations linvfs_file_inode_operations = {
	.permission		= linvfs_permission,
	.truncate		= linvfs_truncate,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_dir_inode_operations = {
	.create			= linvfs_create,
	.lookup			= linvfs_lookup,
	.link			= linvfs_link,
	.unlink			= linvfs_unlink,
	.symlink		= linvfs_symlink,
	.mkdir			= linvfs_mkdir,
	.rmdir			= linvfs_rmdir,
	.mknod			= linvfs_mknod,
	.rename			= linvfs_rename,
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_symlink_inode_operations = {
	.readlink		= generic_readlink,
	.follow_link		= linvfs_follow_link,
	.put_link		= linvfs_put_link,
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};
