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
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_ioctl.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_attr.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_fsops.h"
#include "xfs_discard.h"
#include "xfs_quota.h"
#include "xfs_export.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_symlink.h"
#include "xfs_dinode.h"
#include "xfs_trans.h"

#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/exportfs.h>

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
int
xfs_find_handle(
	unsigned int		cmd,
	xfs_fsop_handlereq_t	*hreq)
{
	int			hsize;
	xfs_handle_t		handle;
	struct inode		*inode;
	struct fd		f = {NULL};
	struct path		path;
	int			error;
	struct xfs_inode	*ip;

	if (cmd == XFS_IOC_FD_TO_HANDLE) {
		f = fdget(hreq->fd);
		if (!f.file)
			return -EBADF;
		inode = file_inode(f.file);
	} else {
		error = user_lpath((const char __user *)hreq->path, &path);
		if (error)
			return error;
		inode = path.dentry->d_inode;
	}
	ip = XFS_I(inode);

	/*
	 * We can only generate handles for inodes residing on a XFS filesystem,
	 * and only for regular files, directories or symbolic links.
	 */
	error = -EINVAL;
	if (inode->i_sb->s_magic != XFS_SB_MAGIC)
		goto out_put;

	error = -EBADF;
	if (!S_ISREG(inode->i_mode) &&
	    !S_ISDIR(inode->i_mode) &&
	    !S_ISLNK(inode->i_mode))
		goto out_put;


	memcpy(&handle.ha_fsid, ip->i_mount->m_fixedfsid, sizeof(xfs_fsid_t));

	if (cmd == XFS_IOC_PATH_TO_FSHANDLE) {
		/*
		 * This handle only contains an fsid, zero the rest.
		 */
		memset(&handle.ha_fid, 0, sizeof(handle.ha_fid));
		hsize = sizeof(xfs_fsid_t);
	} else {
		handle.ha_fid.fid_len = sizeof(xfs_fid_t) -
					sizeof(handle.ha_fid.fid_len);
		handle.ha_fid.fid_pad = 0;
		handle.ha_fid.fid_gen = ip->i_d.di_gen;
		handle.ha_fid.fid_ino = ip->i_ino;

		hsize = XFS_HSIZE(handle);
	}

	error = -EFAULT;
	if (copy_to_user(hreq->ohandle, &handle, hsize) ||
	    copy_to_user(hreq->ohandlen, &hsize, sizeof(__s32)))
		goto out_put;

	error = 0;

 out_put:
	if (cmd == XFS_IOC_FD_TO_HANDLE)
		fdput(f);
	else
		path_put(&path);
	return error;
}

/*
 * No need to do permission checks on the various pathname components
 * as the handle operations are privileged.
 */
STATIC int
xfs_handle_acceptable(
	void			*context,
	struct dentry		*dentry)
{
	return 1;
}

/*
 * Convert userspace handle data into a dentry.
 */
struct dentry *
xfs_handle_to_dentry(
	struct file		*parfilp,
	void __user		*uhandle,
	u32			hlen)
{
	xfs_handle_t		handle;
	struct xfs_fid64	fid;

	/*
	 * Only allow handle opens under a directory.
	 */
	if (!S_ISDIR(file_inode(parfilp)->i_mode))
		return ERR_PTR(-ENOTDIR);

	if (hlen != sizeof(xfs_handle_t))
		return ERR_PTR(-EINVAL);
	if (copy_from_user(&handle, uhandle, hlen))
		return ERR_PTR(-EFAULT);
	if (handle.ha_fid.fid_len !=
	    sizeof(handle.ha_fid) - sizeof(handle.ha_fid.fid_len))
		return ERR_PTR(-EINVAL);

	memset(&fid, 0, sizeof(struct fid));
	fid.ino = handle.ha_fid.fid_ino;
	fid.gen = handle.ha_fid.fid_gen;

	return exportfs_decode_fh(parfilp->f_path.mnt, (struct fid *)&fid, 3,
			FILEID_INO32_GEN | XFS_FILEID_TYPE_64FLAG,
			xfs_handle_acceptable, NULL);
}

STATIC struct dentry *
xfs_handlereq_to_dentry(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq)
{
	return xfs_handle_to_dentry(parfilp, hreq->ihandle, hreq->ihandlen);
}

int
xfs_open_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq)
{
	const struct cred	*cred = current_cred();
	int			error;
	int			fd;
	int			permflag;
	struct file		*filp;
	struct inode		*inode;
	struct dentry		*dentry;
	fmode_t			fmode;
	struct path		path;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dentry = xfs_handlereq_to_dentry(parfilp, hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	inode = dentry->d_inode;

	/* Restrict xfs_open_by_handle to directories & regular files. */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))) {
		error = -EPERM;
		goto out_dput;
	}

#if BITS_PER_LONG != 32
	hreq->oflags |= O_LARGEFILE;
#endif

	permflag = hreq->oflags;
	fmode = OPEN_FMODE(permflag);
	if ((!(permflag & O_APPEND) || (permflag & O_TRUNC)) &&
	    (fmode & FMODE_WRITE) && IS_APPEND(inode)) {
		error = -EPERM;
		goto out_dput;
	}

	if ((fmode & FMODE_WRITE) && IS_IMMUTABLE(inode)) {
		error = -EACCES;
		goto out_dput;
	}

	/* Can't write directories. */
	if (S_ISDIR(inode->i_mode) && (fmode & FMODE_WRITE)) {
		error = -EISDIR;
		goto out_dput;
	}

	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		error = fd;
		goto out_dput;
	}

	path.mnt = parfilp->f_path.mnt;
	path.dentry = dentry;
	filp = dentry_open(&path, hreq->oflags, cred);
	dput(dentry);
	if (IS_ERR(filp)) {
		put_unused_fd(fd);
		return PTR_ERR(filp);
	}

	if (S_ISREG(inode->i_mode)) {
		filp->f_flags |= O_NOATIME;
		filp->f_mode |= FMODE_NOCMTIME;
	}

	fd_install(fd, filp);
	return fd;

 out_dput:
	dput(dentry);
	return error;
}

int
xfs_readlink_by_handle(
	struct file		*parfilp,
	xfs_fsop_handlereq_t	*hreq)
{
	struct dentry		*dentry;
	__u32			olen;
	void			*link;
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dentry = xfs_handlereq_to_dentry(parfilp, hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	/* Restrict this handle operation to symlinks only. */
	if (!S_ISLNK(dentry->d_inode->i_mode)) {
		error = -EINVAL;
		goto out_dput;
	}

	if (copy_from_user(&olen, hreq->ohandlen, sizeof(__u32))) {
		error = -EFAULT;
		goto out_dput;
	}

	link = kmalloc(MAXPATHLEN+1, GFP_KERNEL);
	if (!link) {
		error = -ENOMEM;
		goto out_dput;
	}

	error = xfs_readlink(XFS_I(dentry->d_inode), link);
	if (error)
		goto out_kfree;
	error = readlink_copy(hreq->ohandle, olen, link);
	if (error)
		goto out_kfree;

 out_kfree:
	kfree(link);
 out_dput:
	dput(dentry);
	return error;
}

int
xfs_set_dmattrs(
	xfs_inode_t     *ip,
	u_int		evmask,
	u_int16_t	state)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_trans_t	*tp;
	int		error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_ichange, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = evmask;
	ip->i_d.di_dmstate  = state;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	error = xfs_trans_commit(tp, 0);

	return error;
}

STATIC int
xfs_fssetdm_by_handle(
	struct file		*parfilp,
	void			__user *arg)
{
	int			error;
	struct fsdmidata	fsd;
	xfs_fsop_setdm_handlereq_t dmhreq;
	struct dentry		*dentry;

	if (!capable(CAP_MKNOD))
		return -EPERM;
	if (copy_from_user(&dmhreq, arg, sizeof(xfs_fsop_setdm_handlereq_t)))
		return -EFAULT;

	error = mnt_want_write_file(parfilp);
	if (error)
		return error;

	dentry = xfs_handlereq_to_dentry(parfilp, &dmhreq.hreq);
	if (IS_ERR(dentry)) {
		mnt_drop_write_file(parfilp);
		return PTR_ERR(dentry);
	}

	if (IS_IMMUTABLE(dentry->d_inode) || IS_APPEND(dentry->d_inode)) {
		error = -EPERM;
		goto out;
	}

	if (copy_from_user(&fsd, dmhreq.data, sizeof(fsd))) {
		error = -EFAULT;
		goto out;
	}

	error = xfs_set_dmattrs(XFS_I(dentry->d_inode), fsd.fsd_dmevmask,
				 fsd.fsd_dmstate);

 out:
	mnt_drop_write_file(parfilp);
	dput(dentry);
	return error;
}

STATIC int
xfs_attrlist_by_handle(
	struct file		*parfilp,
	void			__user *arg)
{
	int			error = -ENOMEM;
	attrlist_cursor_kern_t	*cursor;
	xfs_fsop_attrlist_handlereq_t al_hreq;
	struct dentry		*dentry;
	char			*kbuf;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&al_hreq, arg, sizeof(xfs_fsop_attrlist_handlereq_t)))
		return -EFAULT;
	if (al_hreq.buflen < sizeof(struct attrlist) ||
	    al_hreq.buflen > XATTR_LIST_MAX)
		return -EINVAL;

	/*
	 * Reject flags, only allow namespaces.
	 */
	if (al_hreq.flags & ~(ATTR_ROOT | ATTR_SECURE))
		return -EINVAL;

	dentry = xfs_handlereq_to_dentry(parfilp, &al_hreq.hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	kbuf = kmem_zalloc_large(al_hreq.buflen, KM_SLEEP);
	if (!kbuf)
		goto out_dput;

	cursor = (attrlist_cursor_kern_t *)&al_hreq.pos;
	error = xfs_attr_list(XFS_I(dentry->d_inode), kbuf, al_hreq.buflen,
					al_hreq.flags, cursor);
	if (error)
		goto out_kfree;

	if (copy_to_user(al_hreq.buffer, kbuf, al_hreq.buflen))
		error = -EFAULT;

out_kfree:
	kmem_free(kbuf);
out_dput:
	dput(dentry);
	return error;
}

int
xfs_attrmulti_attr_get(
	struct inode		*inode,
	unsigned char		*name,
	unsigned char		__user *ubuf,
	__uint32_t		*len,
	__uint32_t		flags)
{
	unsigned char		*kbuf;
	int			error = -EFAULT;

	if (*len > XATTR_SIZE_MAX)
		return -EINVAL;
	kbuf = kmem_zalloc_large(*len, KM_SLEEP);
	if (!kbuf)
		return -ENOMEM;

	error = xfs_attr_get(XFS_I(inode), name, kbuf, (int *)len, flags);
	if (error)
		goto out_kfree;

	if (copy_to_user(ubuf, kbuf, *len))
		error = -EFAULT;

out_kfree:
	kmem_free(kbuf);
	return error;
}

int
xfs_attrmulti_attr_set(
	struct inode		*inode,
	unsigned char		*name,
	const unsigned char	__user *ubuf,
	__uint32_t		len,
	__uint32_t		flags)
{
	unsigned char		*kbuf;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	if (len > XATTR_SIZE_MAX)
		return -EINVAL;

	kbuf = memdup_user(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	return xfs_attr_set(XFS_I(inode), name, kbuf, len, flags);
}

int
xfs_attrmulti_attr_remove(
	struct inode		*inode,
	unsigned char		*name,
	__uint32_t		flags)
{
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	return xfs_attr_remove(XFS_I(inode), name, flags);
}

STATIC int
xfs_attrmulti_by_handle(
	struct file		*parfilp,
	void			__user *arg)
{
	int			error;
	xfs_attr_multiop_t	*ops;
	xfs_fsop_attrmulti_handlereq_t am_hreq;
	struct dentry		*dentry;
	unsigned int		i, size;
	unsigned char		*attr_name;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&am_hreq, arg, sizeof(xfs_fsop_attrmulti_handlereq_t)))
		return -EFAULT;

	/* overflow check */
	if (am_hreq.opcount >= INT_MAX / sizeof(xfs_attr_multiop_t))
		return -E2BIG;

	dentry = xfs_handlereq_to_dentry(parfilp, &am_hreq.hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	error = -E2BIG;
	size = am_hreq.opcount * sizeof(xfs_attr_multiop_t);
	if (!size || size > 16 * PAGE_SIZE)
		goto out_dput;

	ops = memdup_user(am_hreq.ops, size);
	if (IS_ERR(ops)) {
		error = PTR_ERR(ops);
		goto out_dput;
	}

	error = -ENOMEM;
	attr_name = kmalloc(MAXNAMELEN, GFP_KERNEL);
	if (!attr_name)
		goto out_kfree_ops;

	error = 0;
	for (i = 0; i < am_hreq.opcount; i++) {
		ops[i].am_error = strncpy_from_user((char *)attr_name,
				ops[i].am_attrname, MAXNAMELEN);
		if (ops[i].am_error == 0 || ops[i].am_error == MAXNAMELEN)
			error = -ERANGE;
		if (ops[i].am_error < 0)
			break;

		switch (ops[i].am_opcode) {
		case ATTR_OP_GET:
			ops[i].am_error = xfs_attrmulti_attr_get(
					dentry->d_inode, attr_name,
					ops[i].am_attrvalue, &ops[i].am_length,
					ops[i].am_flags);
			break;
		case ATTR_OP_SET:
			ops[i].am_error = mnt_want_write_file(parfilp);
			if (ops[i].am_error)
				break;
			ops[i].am_error = xfs_attrmulti_attr_set(
					dentry->d_inode, attr_name,
					ops[i].am_attrvalue, ops[i].am_length,
					ops[i].am_flags);
			mnt_drop_write_file(parfilp);
			break;
		case ATTR_OP_REMOVE:
			ops[i].am_error = mnt_want_write_file(parfilp);
			if (ops[i].am_error)
				break;
			ops[i].am_error = xfs_attrmulti_attr_remove(
					dentry->d_inode, attr_name,
					ops[i].am_flags);
			mnt_drop_write_file(parfilp);
			break;
		default:
			ops[i].am_error = -EINVAL;
		}
	}

	if (copy_to_user(am_hreq.ops, ops, size))
		error = -EFAULT;

	kfree(attr_name);
 out_kfree_ops:
	kfree(ops);
 out_dput:
	dput(dentry);
	return error;
}

int
xfs_ioc_space(
	struct xfs_inode	*ip,
	struct inode		*inode,
	struct file		*filp,
	int			ioflags,
	unsigned int		cmd,
	xfs_flock64_t		*bf)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct iattr		iattr;
	bool			setprealloc = false;
	bool			clrprealloc = false;
	int			error;

	/*
	 * Only allow the sys admin to reserve space unless
	 * unwritten extents are enabled.
	 */
	if (!xfs_sb_version_hasextflgbit(&ip->i_mount->m_sb) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (inode->i_flags & (S_IMMUTABLE|S_APPEND))
		return -EPERM;

	if (!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	error = mnt_want_write_file(filp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_IOLOCK_EXCL);

	switch (bf->l_whence) {
	case 0: /*SEEK_SET*/
		break;
	case 1: /*SEEK_CUR*/
		bf->l_start += filp->f_pos;
		break;
	case 2: /*SEEK_END*/
		bf->l_start += XFS_ISIZE(ip);
		break;
	default:
		error = -EINVAL;
		goto out_unlock;
	}

	/*
	 * length of <= 0 for resv/unresv/zero is invalid.  length for
	 * alloc/free is ignored completely and we have no idea what userspace
	 * might have set it to, so set it to zero to allow range
	 * checks to pass.
	 */
	switch (cmd) {
	case XFS_IOC_ZERO_RANGE:
	case XFS_IOC_RESVSP:
	case XFS_IOC_RESVSP64:
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_UNRESVSP64:
		if (bf->l_len <= 0) {
			error = -EINVAL;
			goto out_unlock;
		}
		break;
	default:
		bf->l_len = 0;
		break;
	}

	if (bf->l_start < 0 ||
	    bf->l_start > mp->m_super->s_maxbytes ||
	    bf->l_start + bf->l_len < 0 ||
	    bf->l_start + bf->l_len >= mp->m_super->s_maxbytes) {
		error = -EINVAL;
		goto out_unlock;
	}

	switch (cmd) {
	case XFS_IOC_ZERO_RANGE:
		error = xfs_zero_file_space(ip, bf->l_start, bf->l_len);
		if (!error)
			setprealloc = true;
		break;
	case XFS_IOC_RESVSP:
	case XFS_IOC_RESVSP64:
		error = xfs_alloc_file_space(ip, bf->l_start, bf->l_len,
						XFS_BMAPI_PREALLOC);
		if (!error)
			setprealloc = true;
		break;
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_UNRESVSP64:
		error = xfs_free_file_space(ip, bf->l_start, bf->l_len);
		break;
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP:
	case XFS_IOC_FREESP64:
		if (bf->l_start > XFS_ISIZE(ip)) {
			error = xfs_alloc_file_space(ip, XFS_ISIZE(ip),
					bf->l_start - XFS_ISIZE(ip), 0);
			if (error)
				goto out_unlock;
		}

		iattr.ia_valid = ATTR_SIZE;
		iattr.ia_size = bf->l_start;

		error = xfs_setattr_size(ip, &iattr);
		if (!error)
			clrprealloc = true;
		break;
	default:
		ASSERT(0);
		error = -EINVAL;
	}

	if (error)
		goto out_unlock;

	tp = xfs_trans_alloc(mp, XFS_TRANS_WRITEID);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_writeid, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		goto out_unlock;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	if (!(ioflags & XFS_IO_INVIS)) {
		ip->i_d.di_mode &= ~S_ISUID;
		if (ip->i_d.di_mode & S_IXGRP)
			ip->i_d.di_mode &= ~S_ISGID;
		xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	}

	if (setprealloc)
		ip->i_d.di_flags |= XFS_DIFLAG_PREALLOC;
	else if (clrprealloc)
		ip->i_d.di_flags &= ~XFS_DIFLAG_PREALLOC;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	if (filp->f_flags & O_DSYNC)
		xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0);

out_unlock:
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	mnt_drop_write_file(filp);
	return error;
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
		return -EIO;

	if (copy_from_user(&bulkreq, arg, sizeof(xfs_fsop_bulkreq_t)))
		return -EFAULT;

	if (copy_from_user(&inlast, bulkreq.lastip, sizeof(__s64)))
		return -EFAULT;

	if ((count = bulkreq.icount) <= 0)
		return -EINVAL;

	if (bulkreq.ubuffer == NULL)
		return -EINVAL;

	if (cmd == XFS_IOC_FSINUMBERS)
		error = xfs_inumbers(mp, &inlast, &count,
					bulkreq.ubuffer, xfs_inumbers_fmt);
	else if (cmd == XFS_IOC_FSBULKSTAT_SINGLE)
		error = xfs_bulkstat_one(mp, inlast, bulkreq.ubuffer,
					sizeof(xfs_bstat_t), NULL, &done);
	else	/* XFS_IOC_FSBULKSTAT */
		error = xfs_bulkstat(mp, &inlast, &count, xfs_bulkstat_one,
				     sizeof(xfs_bstat_t), bulkreq.ubuffer,
				     &done);

	if (error)
		return error;

	if (bulkreq.ocount != NULL) {
		if (copy_to_user(bulkreq.lastip, &inlast,
						sizeof(xfs_ino_t)))
			return -EFAULT;

		if (copy_to_user(bulkreq.ocount, &count, sizeof(count)))
			return -EFAULT;
	}

	return 0;
}

STATIC int
xfs_ioc_fsgeometry_v1(
	xfs_mount_t		*mp,
	void			__user *arg)
{
	xfs_fsop_geom_t         fsgeo;
	int			error;

	error = xfs_fs_geometry(mp, &fsgeo, 3);
	if (error)
		return error;

	/*
	 * Caller should have passed an argument of type
	 * xfs_fsop_geom_v1_t.  This is a proper subset of the
	 * xfs_fsop_geom_t that xfs_fs_geometry() fills in.
	 */
	if (copy_to_user(arg, &fsgeo, sizeof(xfs_fsop_geom_v1_t)))
		return -EFAULT;
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
		return error;

	if (copy_to_user(arg, &fsgeo, sizeof(fsgeo)))
		return -EFAULT;
	return 0;
}

/*
 * Linux extended inode flags interface.
 */

STATIC unsigned int
xfs_merge_ioc_xflags(
	unsigned int	flags,
	unsigned int	start)
{
	unsigned int	xflags = start;

	if (flags & FS_IMMUTABLE_FL)
		xflags |= XFS_XFLAG_IMMUTABLE;
	else
		xflags &= ~XFS_XFLAG_IMMUTABLE;
	if (flags & FS_APPEND_FL)
		xflags |= XFS_XFLAG_APPEND;
	else
		xflags &= ~XFS_XFLAG_APPEND;
	if (flags & FS_SYNC_FL)
		xflags |= XFS_XFLAG_SYNC;
	else
		xflags &= ~XFS_XFLAG_SYNC;
	if (flags & FS_NOATIME_FL)
		xflags |= XFS_XFLAG_NOATIME;
	else
		xflags &= ~XFS_XFLAG_NOATIME;
	if (flags & FS_NODUMP_FL)
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
		flags |= FS_IMMUTABLE_FL;
	if (di_flags & XFS_DIFLAG_APPEND)
		flags |= FS_APPEND_FL;
	if (di_flags & XFS_DIFLAG_SYNC)
		flags |= FS_SYNC_FL;
	if (di_flags & XFS_DIFLAG_NOATIME)
		flags |= FS_NOATIME_FL;
	if (di_flags & XFS_DIFLAG_NODUMP)
		flags |= FS_NODUMP_FL;
	return flags;
}

STATIC int
xfs_ioc_fsgetxattr(
	xfs_inode_t		*ip,
	int			attr,
	void			__user *arg)
{
	struct fsxattr		fa;

	memset(&fa, 0, sizeof(struct fsxattr));

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	fa.fsx_xflags = xfs_ip2xflags(ip);
	fa.fsx_extsize = ip->i_d.di_extsize << ip->i_mount->m_sb.sb_blocklog;
	fa.fsx_projid = xfs_get_projid(ip);

	if (attr) {
		if (ip->i_afp) {
			if (ip->i_afp->if_flags & XFS_IFEXTENTS)
				fa.fsx_nextents = ip->i_afp->if_bytes /
							sizeof(xfs_bmbt_rec_t);
			else
				fa.fsx_nextents = ip->i_d.di_anextents;
		} else
			fa.fsx_nextents = 0;
	} else {
		if (ip->i_df.if_flags & XFS_IFEXTENTS)
			fa.fsx_nextents = ip->i_df.if_bytes /
						sizeof(xfs_bmbt_rec_t);
		else
			fa.fsx_nextents = ip->i_d.di_nextents;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (copy_to_user(arg, &fa, sizeof(fa)))
		return -EFAULT;
	return 0;
}

STATIC void
xfs_set_diflags(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	unsigned int		di_flags;

	/* can't set PREALLOC this way, just preserve it */
	di_flags = (ip->i_d.di_flags & XFS_DIFLAG_PREALLOC);
	if (xflags & XFS_XFLAG_IMMUTABLE)
		di_flags |= XFS_DIFLAG_IMMUTABLE;
	if (xflags & XFS_XFLAG_APPEND)
		di_flags |= XFS_DIFLAG_APPEND;
	if (xflags & XFS_XFLAG_SYNC)
		di_flags |= XFS_DIFLAG_SYNC;
	if (xflags & XFS_XFLAG_NOATIME)
		di_flags |= XFS_DIFLAG_NOATIME;
	if (xflags & XFS_XFLAG_NODUMP)
		di_flags |= XFS_DIFLAG_NODUMP;
	if (xflags & XFS_XFLAG_NODEFRAG)
		di_flags |= XFS_DIFLAG_NODEFRAG;
	if (xflags & XFS_XFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;
	if (S_ISDIR(ip->i_d.di_mode)) {
		if (xflags & XFS_XFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (xflags & XFS_XFLAG_NOSYMLINKS)
			di_flags |= XFS_DIFLAG_NOSYMLINKS;
		if (xflags & XFS_XFLAG_EXTSZINHERIT)
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
		if (xflags & XFS_XFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(ip->i_d.di_mode)) {
		if (xflags & XFS_XFLAG_REALTIME)
			di_flags |= XFS_DIFLAG_REALTIME;
		if (xflags & XFS_XFLAG_EXTSIZE)
			di_flags |= XFS_DIFLAG_EXTSIZE;
	}

	ip->i_d.di_flags = di_flags;
}

STATIC void
xfs_diflags_to_linux(
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip);
	unsigned int		xflags = xfs_ip2xflags(ip);

	if (xflags & XFS_XFLAG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (xflags & XFS_XFLAG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
	if (xflags & XFS_XFLAG_SYNC)
		inode->i_flags |= S_SYNC;
	else
		inode->i_flags &= ~S_SYNC;
	if (xflags & XFS_XFLAG_NOATIME)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;
}

#define FSX_PROJID	1
#define FSX_EXTSIZE	2
#define FSX_XFLAGS	4
#define FSX_NONBLOCK	8

STATIC int
xfs_ioctl_setattr(
	xfs_inode_t		*ip,
	struct fsxattr		*fa,
	int			mask)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	unsigned int		lock_flags = 0;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_dquot	*olddquot = NULL;
	int			code;

	trace_xfs_ioctl_setattr(ip);

	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return -EROFS;
	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	/*
	 * Disallow 32bit project ids when projid32bit feature is not enabled.
	 */
	if ((mask & FSX_PROJID) && (fa->fsx_projid > (__uint16_t)-1) &&
			!xfs_sb_version_hasprojid32bit(&ip->i_mount->m_sb))
		return -EINVAL;

	/*
	 * If disk quotas is on, we make sure that the dquots do exist on disk,
	 * before we start any other transactions. Trying to do this later
	 * is messy. We don't care to take a readlock to look at the ids
	 * in inode here, because we can't hold it across the trans_reserve.
	 * If the IDs do change before we take the ilock, we're covered
	 * because the i_*dquot fields will get updated anyway.
	 */
	if (XFS_IS_QUOTA_ON(mp) && (mask & FSX_PROJID)) {
		code = xfs_qm_vop_dqalloc(ip, ip->i_d.di_uid,
					 ip->i_d.di_gid, fa->fsx_projid,
					 XFS_QMOPT_PQUOTA, &udqp, NULL, &pdqp);
		if (code)
			return code;
	}

	/*
	 * For the other attributes, we acquire the inode lock and
	 * first do an error checking pass.
	 */
	tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_NOT_SIZE);
	code = xfs_trans_reserve(tp, &M_RES(mp)->tr_ichange, 0, 0);
	if (code)
		goto error_return;

	lock_flags = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_flags);

	/*
	 * CAP_FOWNER overrides the following restrictions:
	 *
	 * The user ID of the calling process must be equal
	 * to the file owner ID, except in cases where the
	 * CAP_FSETID capability is applicable.
	 */
	if (!inode_owner_or_capable(VFS_I(ip))) {
		code = -EPERM;
		goto error_return;
	}

	/*
	 * Do a quota reservation only if projid is actually going to change.
	 * Only allow changing of projid from init_user_ns since it is a
	 * non user namespace aware identifier.
	 */
	if (mask & FSX_PROJID) {
		if (current_user_ns() != &init_user_ns) {
			code = -EINVAL;
			goto error_return;
		}

		if (XFS_IS_QUOTA_RUNNING(mp) &&
		    XFS_IS_PQUOTA_ON(mp) &&
		    xfs_get_projid(ip) != fa->fsx_projid) {
			ASSERT(tp);
			code = xfs_qm_vop_chown_reserve(tp, ip, udqp, NULL,
						pdqp, capable(CAP_FOWNER) ?
						XFS_QMOPT_FORCE_RES : 0);
			if (code)	/* out of quota */
				goto error_return;
		}
	}

	if (mask & FSX_EXTSIZE) {
		/*
		 * Can't change extent size if any extents are allocated.
		 */
		if (ip->i_d.di_nextents &&
		    ((ip->i_d.di_extsize << mp->m_sb.sb_blocklog) !=
		     fa->fsx_extsize)) {
			code = -EINVAL;	/* EFBIG? */
			goto error_return;
		}

		/*
		 * Extent size must be a multiple of the appropriate block
		 * size, if set at all. It must also be smaller than the
		 * maximum extent size supported by the filesystem.
		 *
		 * Also, for non-realtime files, limit the extent size hint to
		 * half the size of the AGs in the filesystem so alignment
		 * doesn't result in extents larger than an AG.
		 */
		if (fa->fsx_extsize != 0) {
			xfs_extlen_t    size;
			xfs_fsblock_t   extsize_fsb;

			extsize_fsb = XFS_B_TO_FSB(mp, fa->fsx_extsize);
			if (extsize_fsb > MAXEXTLEN) {
				code = -EINVAL;
				goto error_return;
			}

			if (XFS_IS_REALTIME_INODE(ip) ||
			    ((mask & FSX_XFLAGS) &&
			    (fa->fsx_xflags & XFS_XFLAG_REALTIME))) {
				size = mp->m_sb.sb_rextsize <<
				       mp->m_sb.sb_blocklog;
			} else {
				size = mp->m_sb.sb_blocksize;
				if (extsize_fsb > mp->m_sb.sb_agblocks / 2) {
					code = -EINVAL;
					goto error_return;
				}
			}

			if (fa->fsx_extsize % size) {
				code = -EINVAL;
				goto error_return;
			}
		}
	}


	if (mask & FSX_XFLAGS) {
		/*
		 * Can't change realtime flag if any extents are allocated.
		 */
		if ((ip->i_d.di_nextents || ip->i_delayed_blks) &&
		    (XFS_IS_REALTIME_INODE(ip)) !=
		    (fa->fsx_xflags & XFS_XFLAG_REALTIME)) {
			code = -EINVAL;	/* EFBIG? */
			goto error_return;
		}

		/*
		 * If realtime flag is set then must have realtime data.
		 */
		if ((fa->fsx_xflags & XFS_XFLAG_REALTIME)) {
			if ((mp->m_sb.sb_rblocks == 0) ||
			    (mp->m_sb.sb_rextsize == 0) ||
			    (ip->i_d.di_extsize % mp->m_sb.sb_rextsize)) {
				code = -EINVAL;
				goto error_return;
			}
		}

		/*
		 * Can't modify an immutable/append-only file unless
		 * we have appropriate permission.
		 */
		if ((ip->i_d.di_flags &
				(XFS_DIFLAG_IMMUTABLE|XFS_DIFLAG_APPEND) ||
		     (fa->fsx_xflags &
				(XFS_XFLAG_IMMUTABLE | XFS_XFLAG_APPEND))) &&
		    !capable(CAP_LINUX_IMMUTABLE)) {
			code = -EPERM;
			goto error_return;
		}
	}

	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 */
	if (mask & FSX_PROJID) {
		/*
		 * CAP_FSETID overrides the following restrictions:
		 *
		 * The set-user-ID and set-group-ID bits of a file will be
		 * cleared upon successful return from chown()
		 */
		if ((ip->i_d.di_mode & (S_ISUID|S_ISGID)) &&
		    !capable_wrt_inode_uidgid(VFS_I(ip), CAP_FSETID))
			ip->i_d.di_mode &= ~(S_ISUID|S_ISGID);

		/*
		 * Change the ownerships and register quota modifications
		 * in the transaction.
		 */
		if (xfs_get_projid(ip) != fa->fsx_projid) {
			if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_PQUOTA_ON(mp)) {
				olddquot = xfs_qm_vop_chown(tp, ip,
							&ip->i_pdquot, pdqp);
			}
			ASSERT(ip->i_d.di_version > 1);
			xfs_set_projid(ip, fa->fsx_projid);
		}

	}

	if (mask & FSX_XFLAGS) {
		xfs_set_diflags(ip, fa->fsx_xflags);
		xfs_diflags_to_linux(ip);
	}

	/*
	 * Only set the extent size hint if we've already determined that the
	 * extent size hint should be set on the inode. If no extent size flags
	 * are set on the inode then unconditionally clear the extent size hint.
	 */
	if (mask & FSX_EXTSIZE) {
		int	extsize = 0;

		if (ip->i_d.di_flags &
				(XFS_DIFLAG_EXTSIZE | XFS_DIFLAG_EXTSZINHERIT))
			extsize = fa->fsx_extsize >> mp->m_sb.sb_blocklog;
		ip->i_d.di_extsize = extsize;
	}

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	XFS_STATS_INC(xs_ig_attrchg);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 * This is slightly sub-optimal in that truncates require
	 * two sync transactions instead of one for wsync filesystems.
	 * One for the truncate and one for the timestamps since we
	 * don't want to change the timestamps unless we're sure the
	 * truncate worked.  Truncates are less than 1% of the laddis
	 * mix so this probably isn't worth the trouble to optimize.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);
	code = xfs_trans_commit(tp, 0);
	xfs_iunlock(ip, lock_flags);

	/*
	 * Release any dquot(s) the inode had kept before chown.
	 */
	xfs_qm_dqrele(olddquot);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(pdqp);

	return code;

 error_return:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(pdqp);
	xfs_trans_cancel(tp, 0);
	if (lock_flags)
		xfs_iunlock(ip, lock_flags);
	return code;
}

STATIC int
xfs_ioc_fssetxattr(
	xfs_inode_t		*ip,
	struct file		*filp,
	void			__user *arg)
{
	struct fsxattr		fa;
	unsigned int		mask;
	int error;

	if (copy_from_user(&fa, arg, sizeof(fa)))
		return -EFAULT;

	mask = FSX_XFLAGS | FSX_EXTSIZE | FSX_PROJID;
	if (filp->f_flags & (O_NDELAY|O_NONBLOCK))
		mask |= FSX_NONBLOCK;

	error = mnt_want_write_file(filp);
	if (error)
		return error;
	error = xfs_ioctl_setattr(ip, &fa, mask);
	mnt_drop_write_file(filp);
	return error;
}

STATIC int
xfs_ioc_getxflags(
	xfs_inode_t		*ip,
	void			__user *arg)
{
	unsigned int		flags;

	flags = xfs_di2lxflags(ip->i_d.di_flags);
	if (copy_to_user(arg, &flags, sizeof(flags)))
		return -EFAULT;
	return 0;
}

STATIC int
xfs_ioc_setxflags(
	xfs_inode_t		*ip,
	struct file		*filp,
	void			__user *arg)
{
	struct fsxattr		fa;
	unsigned int		flags;
	unsigned int		mask;
	int error;

	if (copy_from_user(&flags, arg, sizeof(flags)))
		return -EFAULT;

	if (flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL | \
		      FS_NOATIME_FL | FS_NODUMP_FL | \
		      FS_SYNC_FL))
		return -EOPNOTSUPP;

	mask = FSX_XFLAGS;
	if (filp->f_flags & (O_NDELAY|O_NONBLOCK))
		mask |= FSX_NONBLOCK;
	fa.fsx_xflags = xfs_merge_ioc_xflags(flags, xfs_ip2xflags(ip));

	error = mnt_want_write_file(filp);
	if (error)
		return error;
	error = xfs_ioctl_setattr(ip, &fa, mask);
	mnt_drop_write_file(filp);
	return error;
}

STATIC int
xfs_getbmap_format(void **ap, struct getbmapx *bmv, int *full)
{
	struct getbmap __user	*base = (struct getbmap __user *)*ap;

	/* copy only getbmap portion (not getbmapx) */
	if (copy_to_user(base, bmv, sizeof(struct getbmap)))
		return -EFAULT;

	*ap += sizeof(struct getbmap);
	return 0;
}

STATIC int
xfs_ioc_getbmap(
	struct xfs_inode	*ip,
	int			ioflags,
	unsigned int		cmd,
	void			__user *arg)
{
	struct getbmapx		bmx;
	int			error;

	if (copy_from_user(&bmx, arg, sizeof(struct getbmapx)))
		return -EFAULT;

	if (bmx.bmv_count < 2)
		return -EINVAL;

	bmx.bmv_iflags = (cmd == XFS_IOC_GETBMAPA ? BMV_IF_ATTRFORK : 0);
	if (ioflags & XFS_IO_INVIS)
		bmx.bmv_iflags |= BMV_IF_NO_DMAPI_READ;

	error = xfs_getbmap(ip, &bmx, xfs_getbmap_format,
			    (__force struct getbmap *)arg+1);
	if (error)
		return error;

	/* copy back header - only size of getbmap */
	if (copy_to_user(arg, &bmx, sizeof(struct getbmap)))
		return -EFAULT;
	return 0;
}

STATIC int
xfs_getbmapx_format(void **ap, struct getbmapx *bmv, int *full)
{
	struct getbmapx __user	*base = (struct getbmapx __user *)*ap;

	if (copy_to_user(base, bmv, sizeof(struct getbmapx)))
		return -EFAULT;

	*ap += sizeof(struct getbmapx);
	return 0;
}

STATIC int
xfs_ioc_getbmapx(
	struct xfs_inode	*ip,
	void			__user *arg)
{
	struct getbmapx		bmx;
	int			error;

	if (copy_from_user(&bmx, arg, sizeof(bmx)))
		return -EFAULT;

	if (bmx.bmv_count < 2)
		return -EINVAL;

	if (bmx.bmv_iflags & (~BMV_IF_VALID))
		return -EINVAL;

	error = xfs_getbmap(ip, &bmx, xfs_getbmapx_format,
			    (__force struct getbmapx *)arg+1);
	if (error)
		return error;

	/* copy back header */
	if (copy_to_user(arg, &bmx, sizeof(struct getbmapx)))
		return -EFAULT;

	return 0;
}

int
xfs_ioc_swapext(
	xfs_swapext_t	*sxp)
{
	xfs_inode_t     *ip, *tip;
	struct fd	f, tmp;
	int		error = 0;

	/* Pull information for the target fd */
	f = fdget((int)sxp->sx_fdtarget);
	if (!f.file) {
		error = -EINVAL;
		goto out;
	}

	if (!(f.file->f_mode & FMODE_WRITE) ||
	    !(f.file->f_mode & FMODE_READ) ||
	    (f.file->f_flags & O_APPEND)) {
		error = -EBADF;
		goto out_put_file;
	}

	tmp = fdget((int)sxp->sx_fdtmp);
	if (!tmp.file) {
		error = -EINVAL;
		goto out_put_file;
	}

	if (!(tmp.file->f_mode & FMODE_WRITE) ||
	    !(tmp.file->f_mode & FMODE_READ) ||
	    (tmp.file->f_flags & O_APPEND)) {
		error = -EBADF;
		goto out_put_tmp_file;
	}

	if (IS_SWAPFILE(file_inode(f.file)) ||
	    IS_SWAPFILE(file_inode(tmp.file))) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	ip = XFS_I(file_inode(f.file));
	tip = XFS_I(file_inode(tmp.file));

	if (ip->i_mount != tip->i_mount) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	if (ip->i_ino == tip->i_ino) {
		error = -EINVAL;
		goto out_put_tmp_file;
	}

	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		error = -EIO;
		goto out_put_tmp_file;
	}

	error = xfs_swap_extents(ip, tip, sxp);

 out_put_tmp_file:
	fdput(tmp);
 out_put_file:
	fdput(f);
 out:
	return error;
}

/*
 * Note: some of the ioctl's return positive numbers as a
 * byte count indicating success, such as readlink_by_handle.
 * So we don't "sign flip" like most other routines.  This means
 * true errors need to be returned as a negative value.
 */
long
xfs_file_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p)
{
	struct inode		*inode = file_inode(filp);
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	void			__user *arg = (void __user *)p;
	int			ioflags = 0;
	int			error;

	if (filp->f_mode & FMODE_NOCMTIME)
		ioflags |= XFS_IO_INVIS;

	trace_xfs_file_ioctl(ip);

	switch (cmd) {
	case FITRIM:
		return xfs_ioc_trim(mp, arg);
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:
	case XFS_IOC_RESVSP:
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:
	case XFS_IOC_RESVSP64:
	case XFS_IOC_UNRESVSP64:
	case XFS_IOC_ZERO_RANGE: {
		xfs_flock64_t		bf;

		if (copy_from_user(&bf, arg, sizeof(bf)))
			return -EFAULT;
		return xfs_ioc_space(ip, inode, filp, ioflags, cmd, &bf);
	}
	case XFS_IOC_DIOINFO: {
		struct dioattr	da;
		xfs_buftarg_t	*target =
			XFS_IS_REALTIME_INODE(ip) ?
			mp->m_rtdev_targp : mp->m_ddev_targp;

		da.d_mem =  da.d_miniosz = target->bt_logical_sectorsize;
		da.d_maxiosz = INT_MAX & ~(da.d_miniosz - 1);

		if (copy_to_user(arg, &da, sizeof(da)))
			return -EFAULT;
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

	case XFS_IOC_FSGETXATTR:
		return xfs_ioc_fsgetxattr(ip, 0, arg);
	case XFS_IOC_FSGETXATTRA:
		return xfs_ioc_fsgetxattr(ip, 1, arg);
	case XFS_IOC_FSSETXATTR:
		return xfs_ioc_fssetxattr(ip, filp, arg);
	case XFS_IOC_GETXFLAGS:
		return xfs_ioc_getxflags(ip, arg);
	case XFS_IOC_SETXFLAGS:
		return xfs_ioc_setxflags(ip, filp, arg);

	case XFS_IOC_FSSETDM: {
		struct fsdmidata	dmi;

		if (copy_from_user(&dmi, arg, sizeof(dmi)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;

		error = xfs_set_dmattrs(ip, dmi.fsd_dmevmask,
				dmi.fsd_dmstate);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_GETBMAP:
	case XFS_IOC_GETBMAPA:
		return xfs_ioc_getbmap(ip, ioflags, cmd, arg);

	case XFS_IOC_GETBMAPX:
		return xfs_ioc_getbmapx(ip, arg);

	case XFS_IOC_FD_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE:
	case XFS_IOC_PATH_TO_FSHANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(hreq)))
			return -EFAULT;
		return xfs_find_handle(cmd, &hreq);
	}
	case XFS_IOC_OPEN_BY_HANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
			return -EFAULT;
		return xfs_open_by_handle(filp, &hreq);
	}
	case XFS_IOC_FSSETDM_BY_HANDLE:
		return xfs_fssetdm_by_handle(filp, arg);

	case XFS_IOC_READLINK_BY_HANDLE: {
		xfs_fsop_handlereq_t	hreq;

		if (copy_from_user(&hreq, arg, sizeof(xfs_fsop_handlereq_t)))
			return -EFAULT;
		return xfs_readlink_by_handle(filp, &hreq);
	}
	case XFS_IOC_ATTRLIST_BY_HANDLE:
		return xfs_attrlist_by_handle(filp, arg);

	case XFS_IOC_ATTRMULTI_BY_HANDLE:
		return xfs_attrmulti_by_handle(filp, arg);

	case XFS_IOC_SWAPEXT: {
		struct xfs_swapext	sxp;

		if (copy_from_user(&sxp, arg, sizeof(xfs_swapext_t)))
			return -EFAULT;
		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_ioc_swapext(&sxp);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSCOUNTS: {
		xfs_fsop_counts_t out;

		error = xfs_fs_counts(mp, &out);
		if (error)
			return error;

		if (copy_to_user(arg, &out, sizeof(out)))
			return -EFAULT;
		return 0;
	}

	case XFS_IOC_SET_RESBLKS: {
		xfs_fsop_resblks_t inout;
		__uint64_t	   in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (mp->m_flags & XFS_MOUNT_RDONLY)
			return -EROFS;

		if (copy_from_user(&inout, arg, sizeof(inout)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;

		/* input parameter is passed in resblks field of structure */
		in = inout.resblks;
		error = xfs_reserve_blocks(mp, &in, &inout);
		mnt_drop_write_file(filp);
		if (error)
			return error;

		if (copy_to_user(arg, &inout, sizeof(inout)))
			return -EFAULT;
		return 0;
	}

	case XFS_IOC_GET_RESBLKS: {
		xfs_fsop_resblks_t out;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		error = xfs_reserve_blocks(mp, NULL, &out);
		if (error)
			return error;

		if (copy_to_user(arg, &out, sizeof(out)))
			return -EFAULT;

		return 0;
	}

	case XFS_IOC_FSGROWFSDATA: {
		xfs_growfs_data_t in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_data(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSGROWFSLOG: {
		xfs_growfs_log_t in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_log(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_FSGROWFSRT: {
		xfs_growfs_rt_t in;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		error = mnt_want_write_file(filp);
		if (error)
			return error;
		error = xfs_growfs_rt(mp, &in);
		mnt_drop_write_file(filp);
		return error;
	}

	case XFS_IOC_GOINGDOWN: {
		__uint32_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(in, (__uint32_t __user *)arg))
			return -EFAULT;

		return xfs_fs_goingdown(mp, in);
	}

	case XFS_IOC_ERROR_INJECTION: {
		xfs_error_injection_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		return xfs_errortag_add(in.errtag, mp);
	}

	case XFS_IOC_ERROR_CLEARALL:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		return xfs_errortag_clearall(mp, 1);

	case XFS_IOC_FREE_EOFBLOCKS: {
		struct xfs_fs_eofblocks eofb;
		struct xfs_eofblocks keofb;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (mp->m_flags & XFS_MOUNT_RDONLY)
			return -EROFS;

		if (copy_from_user(&eofb, arg, sizeof(eofb)))
			return -EFAULT;

		error = xfs_fs_eofblocks_from_user(&eofb, &keofb);
		if (error)
			return error;

		return xfs_icache_free_eofblocks(mp, &keofb);
	}

	default:
		return -ENOTTY;
	}
}
