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
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_rtalloc.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_bmap.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_dfrag.h"
#include "xfs_fsops.h"

#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/pagemap.h>

/*
 * xfs_find_handle maps from userspace xfs_fsop_handlereq structure to
 * a file or fs handle.
 *
 * XFS_IOC_PATH_TO_FSHANDLE
 *    returns fs handle for a mount point or path within that mount point
 * XFS_IOC_FD_TO_HANDLE
 *    returns full handle for a FD opened in user space
 * XFS_IOC_PATH_TO_HANDLE
 *    returns full handle for a path
 */
STATIC int
xfs_find_handle(
	unsigned int		cmd,
	void			__user *arg)
{
	int			hsize;
	xfs_handle_t		handle;
	xfs_fsop_handlereq_t	hreq;
	struct inode		*inode;
	bhv_vnode_t		*vp;

	if (copy_from_user(&hreq, arg, sizeof(hreq)))
		return -XFS_ERROR(EFAULT);

	memset((char *)&handle, 0, sizeof(handle));

	switch (cmd) {
	case XFS_IOC_PATH_TO_FSHANDLE:
	case XFS_IOC_PATH_TO_HANDLE: {
		struct nameidata	nd;
		int			error;

		error = user_path_walk_link((const char __user *)hreq.path, &nd);
		if (error)
			return error;

		ASSERT(nd.dentry);
		ASSERT(nd.dentry->d_inode);
		inode = igrab(nd.dentry->d_inode);
		path_release(&nd);
		break;
	}

	case XFS_IOC_FD_TO_HANDLE: {
		struct file	*file;

		file = fget(hreq.fd);
		if (!file)
		    return -EBADF;

		ASSERT(file->f_dentry);
		ASSERT(file->f_dentry->d_inode);
		inode = igrab(file->f_dentry->d_inode);
		fput(file);
		break;
	}

	default:
		ASSERT(0);
		return -XFS_ERROR(EINVAL);
	}

	if (inode->i_sb->s_magic != XFS_SB_MAGIC) {
		/* we're not in XFS anymore, Toto */
		iput(inode);
		return -XFS_ERROR(EINVAL);
	}

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		break;
	default:
		iput(inode);
		return -XFS_ERROR(EBADF);
	}

	/* we need the vnode */
	vp = vn_from_inode(inode);

	/* now we can grab the fsid */
	memcpy(&handle.ha_fsid, vp->v_vfsp->vfs_altfsid, sizeof(xfs_fsid_t));
	hsize = sizeof(xfs_fsid_t);

	if (cmd != XFS_IOC_PATH_TO_FSHANDLE) {
		xfs_inode_t	*ip;
		int		lock_mode;

		/* need to get access to the xfs_inode to read the generation */
		ip = xfs_vtoi(vp);
		ASSERT(ip);
		lock_mode = xfs_ilock_map_shared(ip);

		/* fill in fid section of handle from inode */
		handle.ha_fid.xfs_fid_len = sizeof(xfs_fid_t) -
					    sizeof(handle.ha_fid.xfs_fid_len);
		handle.ha_fid.xfs_fid_pad = 0;
		handle.ha_fid.xfs_fid_gen = ip->i_d.di_gen;
		handle.ha_fid.xfs_fid_ino = ip->i_ino;

		xfs_iunlock_map_shared(ip, lock_mode);

		hsize = XFS_HSIZE(handle);
	}

	/* now copy our handle into the user buffer & write out the size */
	if (copy_to_user(hreq.ohandle, &handle, hsize) ||
	    copy_to_user(hreq.ohandlen, &hsize, sizeof(__s32))) {
		iput(inode);
		return -XFS_ERROR(EFAULT);
	}

	iput(inode);
	return 0;
}


/*
 * Convert userspace handle data into vnode (and inode).
 * We [ab]use the fact that all the fsop_handlereq ioctl calls
 * have a data structure argument whose first component is always
 * a xfs_fsop_handlereq_t, so we can cast to and from this type.
 * This allows us to optimise the copy_from_user calls and gives
 * a handy, shared routine.
 *
 * If no error, caller must always VN_RELE the returned vp.
 */
STATIC int
xfs_vget_fsop_handlereq(
	xfs_mount_t		*mp,
	struct inode		*parinode,	/* parent inode pointer    */
	xfs_fsop_handlereq_t	*hreq,
	bhv_vnode_t		**vp,
	struct inode		**inode)
{
	void			__user *hanp;
	size_t			hlen;
	xfs_fid_t		*xfid;
	xfs_handle_t		*handlep;
	xfs_handle_t		handle;
	xfs_inode_t		*ip;
	struct inode		*inodep;
	bhv_vnode_t		*vpp;
	xfs_ino_t		ino;
	__u32			igen;
	int			error;

	/*
	 * Only allow handle opens under a directory.
	 */
	if (!S_ISDIR(parinode->i_mode))
		return XFS_ERROR(ENOTDIR);

	hanp = hreq->ihandle;
	hlen = hreq->ihandlen;
	handlep = &handle;

	if (hlen < sizeof(handlep->ha_fsid) || hlen > sizeof(*handlep))
		return XFS_ERROR(EINVAL);
	if (copy_from_user(handlep, hanp, hlen))
		return XFS_ERROR(EFAULT);
	if (hlen < sizeof(*handlep))
		memset(((char *)handlep) + hlen, 0, sizeof(*handlep) - hlen);
	if (hlen > sizeof(handlep->ha_fsid)) {
		if (handlep->ha_fid.xfs_fid_len !=
				(hlen - sizeof(handlep->ha_fsid)
					- sizeof(handlep->ha_fid.xfs_fid_len))
		    || handlep->ha_fid.xfs_fid_pad)
			return XFS_ERROR(EINVAL);
	}

	/*
	 * Crack the handle, obtain the inode # & generation #
	 */
	xfid = (struct xfs_fid *)&handlep->ha_fid;
	if (xfid->xfs_fid_len == sizeof(*xfid) - sizeof(xfid->xfs_fid_len)) {
		ino  = xfid->xfs_fid_ino;
		igen = xfid->xfs_fid_gen;
	} else {
		return XFS_ERROR(EINVAL);
	}

	/*
	 * Get the XFS inode, building a vnode to go with it.
	 */
	error = xfs_iget(mp, NULL, ino, 0, XFS_ILOCK_SHARED, &ip, 0);
	if (error)
		return error;
	if (ip == NULL)
		return XFS_ERROR(EIO);
	if (ip->i_d.di_mode == 0 || ip->i_d.di_gen != igen) {
		xfs_iput_new(ip, XFS_ILOCK_SHARED);
		return XFS_ERROR(ENOENT);
	}

	vpp = XFS_ITOV(ip);
	inodep = vn_to_inode(vpp);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	*vp = vpp;
	*inode = inodep;
	return 0;
}

STATIC int
xfs_open_by_handle(
	xfs_mount_t		*mp,
	void			__user *arg,
	struct file		*parfilp,
	struct inode		*parinode)
{
	int			error;
	int			new_fd;
	int			permflag;
	struct file		*filp;
	struct inode		*inode;
	struct dentry		*dentry;
	bhv_vnode_t		*vp;
	xfs_fsop_handlereq_t	hreq;

	if (!capable(CAP_SYS_ADMIN))
		return -XFS_ERROR(EPERM);
	if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
		return -XFS_ERROR(EFAULT);

	error = xfs_vget_fsop_handlereq(mp, parinode, &hreq, &vp, &inode);
	if (error)
		return -error;

	/* Restrict xfs_open_by_handle to directories & regular files. */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))) {
		iput(inode);
		return -XFS_ERROR(EINVAL);
	}

#if BITS_PER_LONG != 32
	hreq.oflags |= O_LARGEFILE;
#endif
	/* Put open permission in namei format. */
	permflag = hreq.oflags;
	if ((permflag+1) & O_ACCMODE)
		permflag++;
	if (permflag & O_TRUNC)
		permflag |= 2;

	if ((!(permflag & O_APPEND) || (permflag & O_TRUNC)) &&
	    (permflag & FMODE_WRITE) && IS_APPEND(inode)) {
		iput(inode);
		return -XFS_ERROR(EPERM);
	}

	if ((permflag & FMODE_WRITE) && IS_IMMUTABLE(inode)) {
		iput(inode);
		return -XFS_ERROR(EACCES);
	}

	/* Can't write directories. */
	if ( S_ISDIR(inode->i_mode) && (permflag & FMODE_WRITE)) {
		iput(inode);
		return -XFS_ERROR(EISDIR);
	}

	if ((new_fd = get_unused_fd()) < 0) {
		iput(inode);
		return new_fd;
	}

	dentry = d_alloc_anon(inode);
	if (dentry == NULL) {
		iput(inode);
		put_unused_fd(new_fd);
		return -XFS_ERROR(ENOMEM);
	}

	/* Ensure umount returns EBUSY on umounts while this file is open. */
	mntget(parfilp->f_vfsmnt);

	/* Create file pointer. */
	filp = dentry_open(dentry, parfilp->f_vfsmnt, hreq.oflags);
	if (IS_ERR(filp)) {
		put_unused_fd(new_fd);
		return -XFS_ERROR(-PTR_ERR(filp));
	}
	if (inode->i_mode & S_IFREG) {
		/* invisible operation should not change atime */
		filp->f_flags |= O_NOATIME;
		filp->f_op = &xfs_invis_file_operations;
	}

	fd_install(new_fd, filp);
	return new_fd;
}

STATIC int
xfs_readlink_by_handle(
	xfs_mount_t		*mp,
	void			__user *arg,
	struct file		*parfilp,
	struct inode		*parinode)
{
	int			error;
	struct iovec		aiov;
	struct uio		auio;
	struct inode		*inode;
	xfs_fsop_handlereq_t	hreq;
	bhv_vnode_t		*vp;
	__u32			olen;

	if (!capable(CAP_SYS_ADMIN))
		return -XFS_ERROR(EPERM);
	if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
		return -XFS_ERROR(EFAULT);

	error = xfs_vget_fsop_handlereq(mp, parinode, &hreq, &vp, &inode);
	if (error)
		return -error;

	/* Restrict this handle operation to symlinks only. */
	if (!S_ISLNK(inode->i_mode)) {
		VN_RELE(vp);
		return -XFS_ERROR(EINVAL);
	}

	if (copy_from_user(&olen, hreq.ohandlen, sizeof(__u32))) {
		VN_RELE(vp);
		return -XFS_ERROR(EFAULT);
	}
	aiov.iov_len	= olen;
	aiov.iov_base	= hreq.ohandle;

	auio.uio_iov	= &aiov;
	auio.uio_iovcnt	= 1;
	auio.uio_offset	= 0;
	auio.uio_segflg	= UIO_USERSPACE;
	auio.uio_resid	= olen;

	error = bhv_vop_readlink(vp, &auio, IO_INVIS, NULL);
	VN_RELE(vp);
	if (error)
		return -error;

	return (olen - auio.uio_resid);
}

STATIC int
xfs_fssetdm_by_handle(
	xfs_mount_t		*mp,
	void			__user *arg,
	struct file		*parfilp,
	struct inode		*parinode)
{
	int			error;
	struct fsdmidata	fsd;
	xfs_fsop_setdm_handlereq_t dmhreq;
	struct inode		*inode;
	bhv_desc_t		*bdp;
	bhv_vnode_t		*vp;

	if (!capable(CAP_MKNOD))
		return -XFS_ERROR(EPERM);
	if (copy_from_user(&dmhreq, arg, sizeof(xfs_fsop_setdm_handlereq_t)))
		return -XFS_ERROR(EFAULT);

	error = xfs_vget_fsop_handlereq(mp, parinode, &dmhreq.hreq, &vp, &inode);
	if (error)
		return -error;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode)) {
		VN_RELE(vp);
		return -XFS_ERROR(EPERM);
	}

	if (copy_from_user(&fsd, dmhreq.data, sizeof(fsd))) {
		VN_RELE(vp);
		return -XFS_ERROR(EFAULT);
	}

	bdp = bhv_base_unlocked(VN_BHV_HEAD(vp));
	error = xfs_set_dmattrs(bdp, fsd.fsd_dmevmask, fsd.fsd_dmstate, NULL);

	VN_RELE(vp);
	if (error)
		return -error;
	return 0;
}

STATIC int
xfs_attrlist_by_handle(
	xfs_mount_t		*mp,
	void			__user *arg,
	struct file		*parfilp,
	struct inode		*parinode)
{
	int			error;
	attrlist_cursor_kern_t	*cursor;
	xfs_fsop_attrlist_handlereq_t al_hreq;
	struct inode		*inode;
	bhv_vnode_t		*vp;
	char			*kbuf;

	if (!capable(CAP_SYS_ADMIN))
		return -XFS_ERROR(EPERM);
	if (copy_from_user(&al_hreq, arg, sizeof(xfs_fsop_attrlist_handlereq_t)))
		return -XFS_ERROR(EFAULT);
	if (al_hreq.buflen > XATTR_LIST_MAX)
		return -XFS_ERROR(EINVAL);

	error = xfs_vget_fsop_handlereq(mp, parinode, &al_hreq.hreq,
			&vp, &inode);
	if (error)
		goto out;

	kbuf = kmalloc(al_hreq.buflen, GFP_KERNEL);
	if (!kbuf)
		goto out_vn_rele;

	cursor = (attrlist_cursor_kern_t *)&al_hreq.pos;
	error = bhv_vop_attr_list(vp, kbuf, al_hreq.buflen, al_hreq.flags,
					cursor, NULL);
	if (error)
		goto out_kfree;

	if (copy_to_user(al_hreq.buffer, kbuf, al_hreq.buflen))
		error = -EFAULT;

 out_kfree:
	kfree(kbuf);
 out_vn_rele:
	VN_RELE(vp);
 out:
	return -error;
}

STATIC int
xfs_attrmulti_attr_get(
	bhv_vnode_t		*vp,
	char			*name,
	char			__user *ubuf,
	__uint32_t		*len,
	__uint32_t		flags)
{
	char			*kbuf;
	int			error = EFAULT;
	
	if (*len > XATTR_SIZE_MAX)
		return EINVAL;
	kbuf = kmalloc(*len, GFP_KERNEL);
	if (!kbuf)
		return ENOMEM;

	error = bhv_vop_attr_get(vp, name, kbuf, len, flags, NULL);
	if (error)
		goto out_kfree;

	if (copy_to_user(ubuf, kbuf, *len))
		error = EFAULT;

 out_kfree:
	kfree(kbuf);
	return error;
}

STATIC int
xfs_attrmulti_attr_set(
	bhv_vnode_t		*vp,
	char			*name,
	const char		__user *ubuf,
	__uint32_t		len,
	__uint32_t		flags)
{
	char			*kbuf;
	int			error = EFAULT;

	if (IS_RDONLY(&vp->v_inode))
		return -EROFS;
	if (IS_IMMUTABLE(&vp->v_inode) || IS_APPEND(&vp->v_inode))
		return EPERM;
	if (len > XATTR_SIZE_MAX)
		return EINVAL;

	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf)
		return ENOMEM;

	if (copy_from_user(kbuf, ubuf, len))
		goto out_kfree;
			
	error = bhv_vop_attr_set(vp, name, kbuf, len, flags, NULL);

 out_kfree:
	kfree(kbuf);
	return error;
}

STATIC int
xfs_attrmulti_attr_remove(
	bhv_vnode_t		*vp,
	char			*name,
	__uint32_t		flags)
{
	if (IS_RDONLY(&vp->v_inode))
		return -EROFS;
	if (IS_IMMUTABLE(&vp->v_inode) || IS_APPEND(&vp->v_inode))
		return EPERM;
	return bhv_vop_attr_remove(vp, name, flags, NULL);
}

STATIC int
xfs_attrmulti_by_handle(
	xfs_mount_t		*mp,
	void			__user *arg,
	struct file		*parfilp,
	struct inode		*parinode)
{
	int			error;
	xfs_attr_multiop_t	*ops;
	xfs_fsop_attrmulti_handlereq_t am_hreq;
	struct inode		*inode;
	bhv_vnode_t		*vp;
	unsigned int		i, size;
	char			*attr_name;

	if (!capable(CAP_SYS_ADMIN))
		return -XFS_ERROR(EPERM);
	if (copy_from_user(&am_hreq, arg, sizeof(xfs_fsop_attrmulti_handlereq_t)))
		return -XFS_ERROR(EFAULT);

	error = xfs_vget_fsop_handlereq(mp, parinode, &am_hreq.hreq, &vp, &inode);
	if (error)
		goto out;

	error = E2BIG;
	size = am_hreq.opcount * sizeof(attr_multiop_t);
	if (!size || size > 16 * PAGE_SIZE)
		goto out_vn_rele;

	error = ENOMEM;
	ops = kmalloc(size, GFP_KERNEL);
	if (!ops)
		goto out_vn_rele;

	error = EFAULT;
	if (copy_from_user(ops, am_hreq.ops, size))
		goto out_kfree_ops;

	attr_name = kmalloc(MAXNAMELEN, GFP_KERNEL);
	if (!attr_name)
		goto out_kfree_ops;


	error = 0;
	for (i = 0; i < am_hreq.opcount; i++) {
		ops[i].am_error = strncpy_from_user(attr_name,
				ops[i].am_attrname, MAXNAMELEN);
		if (ops[i].am_error == 0 || ops[i].am_error == MAXNAMELEN)
			error = -ERANGE;
		if (ops[i].am_error < 0)
			break;

		switch (ops[i].am_opcode) {
		case ATTR_OP_GET:
			ops[i].am_error = xfs_attrmulti_attr_get(vp,
					attr_name, ops[i].am_attrvalue,
					&ops[i].am_length, ops[i].am_flags);
			break;
		case ATTR_OP_SET:
			ops[i].am_error = xfs_attrmulti_attr_set(vp,
					attr_name, ops[i].am_attrvalue,
					ops[i].am_length, ops[i].am_flags);
			break;
		case ATTR_OP_REMOVE:
			ops[i].am_error = xfs_attrmulti_attr_remove(vp,
					attr_name, ops[i].am_flags);
			break;
		default:
			ops[i].am_error = EINVAL;
		}
	}

	if (copy_to_user(am_hreq.ops, ops, size))
		error = XFS_ERROR(EFAULT);

	kfree(attr_name);
 out_kfree_ops:
	kfree(ops);
 out_vn_rele:
	VN_RELE(vp);
 out:
	return -error;
}

/* prototypes for a few of the stack-hungry cases that have
 * their own functions.  Functions are defined after their use
 * so gcc doesn't get fancy and inline them with -03 */

STATIC int
xfs_ioc_space(
	bhv_desc_t		*bdp,
	struct inode		*inode,
	struct file		*filp,
	int			flags,
	unsigned int		cmd,
	void			__user *arg);

STATIC int
xfs_ioc_bulkstat(
	xfs_mount_t		*mp,
	unsigned int		cmd,
	void			__user *arg);

STATIC int
xfs_ioc_fsgeometry_v1(
	xfs_mount_t		*mp,
	void			__user *arg);

STATIC int
xfs_ioc_fsgeometry(
	xfs_mount_t		*mp,
	void			__user *arg);

STATIC int
xfs_ioc_xattr(
	bhv_vnode_t		*vp,
	xfs_inode_t		*ip,
	struct file		*filp,
	unsigned int		cmd,
	void			__user *arg);

STATIC int
xfs_ioc_getbmap(
	bhv_desc_t		*bdp,
	struct file		*filp,
	int			flags,
	unsigned int		cmd,
	void			__user *arg);

STATIC int
xfs_ioc_getbmapx(
	bhv_desc_t		*bdp,
	void			__user *arg);

int
xfs_ioctl(
	bhv_desc_t		*bdp,
	struct inode		*inode,
	struct file		*filp,
	int			ioflags,
	unsigned int		cmd,
	void			__user *arg)
{
	int			error;
	bhv_vnode_t		*vp;
	xfs_inode_t		*ip;
	xfs_mount_t		*mp;

	vp = vn_from_inode(inode);

	vn_trace_entry(vp, "xfs_ioctl", (inst_t *)__return_address);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	switch (cmd) {

	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:
	case XFS_IOC_RESVSP:
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:
	case XFS_IOC_RESVSP64:
	case XFS_IOC_UNRESVSP64:
		/*
		 * Only allow the sys admin to reserve space unless
		 * unwritten extents are enabled.
		 */
		if (!XFS_SB_VERSION_HASEXTFLGBIT(&mp->m_sb) &&
		    !capable(CAP_SYS_ADMIN))
			return -EPERM;

		return xfs_ioc_space(bdp, inode, filp, ioflags, cmd, arg);

	case XFS_IOC_DIOINFO: {
		struct dioattr	da;
		xfs_buftarg_t	*target =
			(ip->i_d.di_flags & XFS_DIFLAG_REALTIME) ?
			mp->m_rtdev_targp : mp->m_ddev_targp;

		da.d_mem = da.d_miniosz = 1 << target->bt_sshift;
		da.d_maxiosz = INT_MAX & ~(da.d_miniosz - 1);

		if (copy_to_user(arg, &da, sizeof(da)))
			return -XFS_ERROR(EFAULT);
		return 0;
	}

	case XFS_IOC_FSBULKSTAT_SINGLE:
	case XFS_IOC_FSBULKSTAT:
	case XFS_IOC_FSINUMBERS:
		return xfs_ioc_bulkstat(mp, cmd, arg);

	case XFS_IOC_FSGEOMETRY_V1:
		return xfs_ioc_fsgeometry_v1(mp, arg);

	case XFS_IOC_FSGEOMETRY:
		return xfs_ioc_fsgeometry(mp, arg);

	case XFS_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);

	case XFS_IOC_GETXFLAGS:
	case XFS_IOC_SETXFLAGS:
	case XFS_IOC_FSGETXATTR:
	case XFS_IOC_FSSETXATTR:
	case XFS_IOC_FSGETXATTRA:
		return xfs_ioc_xattr(vp, ip, filp, cmd, arg);

	case XFS_IOC_FSSETDM: {
		struct fsdmidata	dmi;

		if (copy_from_user(&dmi, arg, sizeof(dmi)))
			return -XFS_ERROR(EFAULT);

		error = xfs_set_dmattrs(bdp, dmi.fsd_dmevmask, dmi.fsd_dmstate,
							NULL);
		return -error;
	}

	case XFS_IOC_GETBMAP:
	case XFS_IOC_GETBMAPA:
		return xfs_ioc_getbmap(bdp, filp, ioflags, cmd, arg);

	case XFS_IOC_GETBMAPX:
		return xfs_ioc_getbmapx(bdp, arg);

	case XFS_IOC_FD_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE:
	case XFS_IOC_PATH_TO_FSHANDLE:
		return xfs_find_handle(cmd, arg);

	case XFS_IOC_OPEN_BY_HANDLE:
		return xfs_open_by_handle(mp, arg, filp, inode);

	case XFS_IOC_FSSETDM_BY_HANDLE:
		return xfs_fssetdm_by_handle(mp, arg, filp, inode);

	case XFS_IOC_READLINK_BY_HANDLE:
		return xfs_readlink_by_handle(mp, arg, filp, inode);

	case XFS_IOC_ATTRLIST_BY_HANDLE:
		return xfs_attrlist_by_handle(mp, arg, filp, inode);

	case XFS_IOC_ATTRMULTI_BY_HANDLE:
		return xfs_attrmulti_by_handle(mp, arg, filp, inode);

	case XFS_IOC_SWAPEXT: {
		error = xfs_swapext((struct xfs_swapext __user *)arg);
		return -error;
	}

	case XFS_IOC_FSCOUNTS: {
		xfs_fsop_counts_t out;

		error = xfs_fs_counts(mp, &out);
		if (error)
			return -error;

		if (copy_to_user(arg, &out, sizeof(out)))
			return -XFS_ERROR(EFAULT);
		return 0;
	}

	case XFS_IOC_SET_RESBLKS: {
		xfs_fsop_resblks_t inout;
		__uint64_t	   in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&inout, arg, sizeof(inout)))
			return -XFS_ERROR(EFAULT);

		/* input parameter is passed in resblks field of structure */
		in = inout.resblks;
		error = xfs_reserve_blocks(mp, &in, &inout);
		if (error)
			return -error;

		if (copy_to_user(arg, &inout, sizeof(inout)))
			return -XFS_ERROR(EFAULT);
		return 0;
	}

	case XFS_IOC_GET_RESBLKS: {
		xfs_fsop_resblks_t out;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		error = xfs_reserve_blocks(mp, NULL, &out);
		if (error)
			return -error;

		if (copy_to_user(arg, &out, sizeof(out)))
			return -XFS_ERROR(EFAULT);

		return 0;
	}

	case XFS_IOC_FSGROWFSDATA: {
		xfs_growfs_data_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -XFS_ERROR(EFAULT);

		error = xfs_growfs_data(mp, &in);
		return -error;
	}

	case XFS_IOC_FSGROWFSLOG: {
		xfs_growfs_log_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -XFS_ERROR(EFAULT);

		error = xfs_growfs_log(mp, &in);
		return -error;
	}

	case XFS_IOC_FSGROWFSRT: {
		xfs_growfs_rt_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -XFS_ERROR(EFAULT);

		error = xfs_growfs_rt(mp, &in);
		return -error;
	}

	case XFS_IOC_FREEZE:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (inode->i_sb->s_frozen == SB_UNFROZEN)
			freeze_bdev(inode->i_sb->s_bdev);
		return 0;

	case XFS_IOC_THAW:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (inode->i_sb->s_frozen != SB_UNFROZEN)
			thaw_bdev(inode->i_sb->s_bdev, inode->i_sb);
		return 0;

	case XFS_IOC_GOINGDOWN: {
		__uint32_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(in, (__uint32_t __user *)arg))
			return -XFS_ERROR(EFAULT);

		error = xfs_fs_goingdown(mp, in);
		return -error;
	}

	case XFS_IOC_ERROR_INJECTION: {
		xfs_error_injection_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -XFS_ERROR(EFAULT);

		error = xfs_errortag_add(in.errtag, mp);
		return -error;
	}

	case XFS_IOC_ERROR_CLEARALL:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		error = xfs_errortag_clearall(mp);
		return -error;

	default:
		return -ENOTTY;
	}
}

STATIC int
xfs_ioc_space(
	bhv_desc_t		*bdp,
	struct inode		*inode,
	struct file		*filp,
	int			ioflags,
	unsigned int		cmd,
	void			__user *arg)
{
	xfs_flock64_t		bf;
	int			attr_flags = 0;
	int			error;

	if (inode->i_flags & (S_IMMUTABLE|S_APPEND))
		return -XFS_ERROR(EPERM);

	if (!(filp->f_mode & FMODE_WRITE))
		return -XFS_ERROR(EBADF);

	if (!S_ISREG(inode->i_mode))
		return -XFS_ERROR(EINVAL);

	if (copy_from_user(&bf, arg, sizeof(bf)))
		return -XFS_ERROR(EFAULT);

	if (filp->f_flags & (O_NDELAY|O_NONBLOCK))
		attr_flags |= ATTR_NONBLOCK;
	if (ioflags & IO_INVIS)
		attr_flags |= ATTR_DMI;

	error = xfs_change_file_space(bdp, cmd, &bf, filp->f_pos,
					      NULL, attr_flags);
	return -error;
}

STATIC int
xfs_ioc_bulkstat(
	xfs_mount_t		*mp,
	unsigned int		cmd,
	void			__user *arg)
{
	xfs_fsop_bulkreq_t	bulkreq;
	int			count;	/* # of records returned */
	xfs_ino_t		inlast;	/* last inode number */
	int			done;
	int			error;

	/* done = 1 if there are more stats to get and if bulkstat */
	/* should be called again (unused here, but used in dmapi) */

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -XFS_ERROR(EIO);

	if (copy_from_user(&bulkreq, arg, sizeof(xfs_fsop_bulkreq_t)))
		return -XFS_ERROR(EFAULT);

	if (copy_from_user(&inlast, bulkreq.lastip, sizeof(__s64)))
		return -XFS_ERROR(EFAULT);

	if ((count = bulkreq.icount) <= 0)
		return -XFS_ERROR(EINVAL);

	if (cmd == XFS_IOC_FSINUMBERS)
		error = xfs_inumbers(mp, &inlast, &count,
						bulkreq.ubuffer);
	else if (cmd == XFS_IOC_FSBULKSTAT_SINGLE)
		error = xfs_bulkstat_single(mp, &inlast,
						bulkreq.ubuffer, &done);
	else {	/* XFS_IOC_FSBULKSTAT */
		if (count == 1 && inlast != 0) {
			inlast++;
			error = xfs_bulkstat_single(mp, &inlast,
					bulkreq.ubuffer, &done);
		} else {
			error = xfs_bulkstat(mp, &inlast, &count,
				(bulkstat_one_pf)xfs_bulkstat_one, NULL,
				sizeof(xfs_bstat_t), bulkreq.ubuffer,
				BULKSTAT_FG_QUICK, &done);
		}
	}

	if (error)
		return -error;

	if (bulkreq.ocount != NULL) {
		if (copy_to_user(bulkreq.lastip, &inlast,
						sizeof(xfs_ino_t)))
			return -XFS_ERROR(EFAULT);

		if (copy_to_user(bulkreq.ocount, &count, sizeof(count)))
			return -XFS_ERROR(EFAULT);
	}

	return 0;
}

STATIC int
xfs_ioc_fsgeometry_v1(
	xfs_mount_t		*mp,
	void			__user *arg)
{
	xfs_fsop_geom_v1_t	fsgeo;
	int			error;

	error = xfs_fs_geometry(mp, (xfs_fsop_geom_t *)&fsgeo, 3);
	if (error)
		return -error;

	if (copy_to_user(arg, &fsgeo, sizeof(fsgeo)))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_ioc_fsgeometry(
	xfs_mount_t		*mp,
	void			__user *arg)
{
	xfs_fsop_geom_t		fsgeo;
	int			error;

	error = xfs_fs_geometry(mp, &fsgeo, 4);
	if (error)
		return -error;

	if (copy_to_user(arg, &fsgeo, sizeof(fsgeo)))
		return -XFS_ERROR(EFAULT);
	return 0;
}

/*
 * Linux extended inode flags interface.
 */
#define LINUX_XFLAG_SYNC	0x00000008 /* Synchronous updates */
#define LINUX_XFLAG_IMMUTABLE	0x00000010 /* Immutable file */
#define LINUX_XFLAG_APPEND	0x00000020 /* writes to file may only append */
#define LINUX_XFLAG_NODUMP	0x00000040 /* do not dump file */
#define LINUX_XFLAG_NOATIME	0x00000080 /* do not update atime */

STATIC unsigned int
xfs_merge_ioc_xflags(
	unsigned int	flags,
	unsigned int	start)
{
	unsigned int	xflags = start;

	if (flags & LINUX_XFLAG_IMMUTABLE)
		xflags |= XFS_XFLAG_IMMUTABLE;
	else
		xflags &= ~XFS_XFLAG_IMMUTABLE;
	if (flags & LINUX_XFLAG_APPEND)
		xflags |= XFS_XFLAG_APPEND;
	else
		xflags &= ~XFS_XFLAG_APPEND;
	if (flags & LINUX_XFLAG_SYNC)
		xflags |= XFS_XFLAG_SYNC;
	else
		xflags &= ~XFS_XFLAG_SYNC;
	if (flags & LINUX_XFLAG_NOATIME)
		xflags |= XFS_XFLAG_NOATIME;
	else
		xflags &= ~XFS_XFLAG_NOATIME;
	if (flags & LINUX_XFLAG_NODUMP)
		xflags |= XFS_XFLAG_NODUMP;
	else
		xflags &= ~XFS_XFLAG_NODUMP;

	return xflags;
}

STATIC unsigned int
xfs_di2lxflags(
	__uint16_t	di_flags)
{
	unsigned int	flags = 0;

	if (di_flags & XFS_DIFLAG_IMMUTABLE)
		flags |= LINUX_XFLAG_IMMUTABLE;
	if (di_flags & XFS_DIFLAG_APPEND)
		flags |= LINUX_XFLAG_APPEND;
	if (di_flags & XFS_DIFLAG_SYNC)
		flags |= LINUX_XFLAG_SYNC;
	if (di_flags & XFS_DIFLAG_NOATIME)
		flags |= LINUX_XFLAG_NOATIME;
	if (di_flags & XFS_DIFLAG_NODUMP)
		flags |= LINUX_XFLAG_NODUMP;
	return flags;
}

STATIC int
xfs_ioc_xattr(
	bhv_vnode_t		*vp,
	xfs_inode_t		*ip,
	struct file		*filp,
	unsigned int		cmd,
	void			__user *arg)
{
	struct fsxattr		fa;
	struct bhv_vattr	*vattr;
	int			error = 0;
	int			attr_flags;
	unsigned int		flags;

	vattr = kmalloc(sizeof(*vattr), GFP_KERNEL);
	if (unlikely(!vattr))
		return -ENOMEM;

	switch (cmd) {
	case XFS_IOC_FSGETXATTR: {
		vattr->va_mask = XFS_AT_XFLAGS | XFS_AT_EXTSIZE | \
				 XFS_AT_NEXTENTS | XFS_AT_PROJID;
		error = bhv_vop_getattr(vp, vattr, 0, NULL);
		if (unlikely(error)) {
			error = -error;
			break;
		}

		fa.fsx_xflags	= vattr->va_xflags;
		fa.fsx_extsize	= vattr->va_extsize;
		fa.fsx_nextents = vattr->va_nextents;
		fa.fsx_projid	= vattr->va_projid;

		if (copy_to_user(arg, &fa, sizeof(fa))) {
			error = -EFAULT;
			break;
		}
		break;
	}

	case XFS_IOC_FSSETXATTR: {
		if (copy_from_user(&fa, arg, sizeof(fa))) {
			error = -EFAULT;
			break;
		}

		attr_flags = 0;
		if (filp->f_flags & (O_NDELAY|O_NONBLOCK))
			attr_flags |= ATTR_NONBLOCK;

		vattr->va_mask = XFS_AT_XFLAGS | XFS_AT_EXTSIZE | XFS_AT_PROJID;
		vattr->va_xflags  = fa.fsx_xflags;
		vattr->va_extsize = fa.fsx_extsize;
		vattr->va_projid  = fa.fsx_projid;

		error = bhv_vop_setattr(vp, vattr, attr_flags, NULL);
		if (likely(!error))
			__vn_revalidate(vp, vattr);	/* update flags */
		error = -error;
		break;
	}

	case XFS_IOC_FSGETXATTRA: {
		vattr->va_mask = XFS_AT_XFLAGS | XFS_AT_EXTSIZE | \
				 XFS_AT_ANEXTENTS | XFS_AT_PROJID;
		error = bhv_vop_getattr(vp, vattr, 0, NULL);
		if (unlikely(error)) {
			error = -error;
			break;
		}

		fa.fsx_xflags	= vattr->va_xflags;
		fa.fsx_extsize	= vattr->va_extsize;
		fa.fsx_nextents = vattr->va_anextents;
		fa.fsx_projid	= vattr->va_projid;

		if (copy_to_user(arg, &fa, sizeof(fa))) {
			error = -EFAULT;
			break;
		}
		break;
	}

	case XFS_IOC_GETXFLAGS: {
		flags = xfs_di2lxflags(ip->i_d.di_flags);
		if (copy_to_user(arg, &flags, sizeof(flags)))
			error = -EFAULT;
		break;
	}

	case XFS_IOC_SETXFLAGS: {
		if (copy_from_user(&flags, arg, sizeof(flags))) {
			error = -EFAULT;
			break;
		}

		if (flags & ~(LINUX_XFLAG_IMMUTABLE | LINUX_XFLAG_APPEND | \
			      LINUX_XFLAG_NOATIME | LINUX_XFLAG_NODUMP | \
			      LINUX_XFLAG_SYNC)) {
			error = -EOPNOTSUPP;
			break;
		}

		attr_flags = 0;
		if (filp->f_flags & (O_NDELAY|O_NONBLOCK))
			attr_flags |= ATTR_NONBLOCK;

		vattr->va_mask = XFS_AT_XFLAGS;
		vattr->va_xflags = xfs_merge_ioc_xflags(flags,
							xfs_ip2xflags(ip));

		error = bhv_vop_setattr(vp, vattr, attr_flags, NULL);
		if (likely(!error))
			__vn_revalidate(vp, vattr);	/* update flags */
		error = -error;
		break;
	}

	default:
		error = -ENOTTY;
		break;
	}

	kfree(vattr);
	return error;
}

STATIC int
xfs_ioc_getbmap(
	bhv_desc_t		*bdp,
	struct file		*filp,
	int			ioflags,
	unsigned int		cmd,
	void			__user *arg)
{
	struct getbmap		bm;
	int			iflags;
	int			error;

	if (copy_from_user(&bm, arg, sizeof(bm)))
		return -XFS_ERROR(EFAULT);

	if (bm.bmv_count < 2)
		return -XFS_ERROR(EINVAL);

	iflags = (cmd == XFS_IOC_GETBMAPA ? BMV_IF_ATTRFORK : 0);
	if (ioflags & IO_INVIS)
		iflags |= BMV_IF_NO_DMAPI_READ;

	error = xfs_getbmap(bdp, &bm, (struct getbmap __user *)arg+1, iflags);
	if (error)
		return -error;

	if (copy_to_user(arg, &bm, sizeof(bm)))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_ioc_getbmapx(
	bhv_desc_t		*bdp,
	void			__user *arg)
{
	struct getbmapx		bmx;
	struct getbmap		bm;
	int			iflags;
	int			error;

	if (copy_from_user(&bmx, arg, sizeof(bmx)))
		return -XFS_ERROR(EFAULT);

	if (bmx.bmv_count < 2)
		return -XFS_ERROR(EINVAL);

	/*
	 * Map input getbmapx structure to a getbmap
	 * structure for xfs_getbmap.
	 */
	GETBMAP_CONVERT(bmx, bm);

	iflags = bmx.bmv_iflags;

	if (iflags & (~BMV_IF_VALID))
		return -XFS_ERROR(EINVAL);

	iflags |= BMV_IF_EXTENDED;

	error = xfs_getbmap(bdp, &bm, (struct getbmapx __user *)arg+1, iflags);
	if (error)
		return -error;

	GETBMAP_CONVERT(bm, bmx);

	if (copy_to_user(arg, &bmx, sizeof(bmx)))
		return -XFS_ERROR(EFAULT);

	return 0;
}
