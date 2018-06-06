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
#include "xfs_trans.h"
#include "xfs_pnfs.h"
#include "xfs_acl.h"
#include "xfs_btree.h"
#include <linux/fsmap.h>
#include "xfs_fsmap.h"
#include "scrub/xfs_scrub.h"
#include "xfs_sb.h"

#include <linux/capability.h>
#include <linux/cred.h>
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
		inode = d_inode(path.dentry);
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
		handle.ha_fid.fid_gen = inode->i_generation;
		handle.ha_fid.fid_ino = ip->i_ino;
		hsize = sizeof(xfs_handle_t);
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
	inode = d_inode(dentry);

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
		error = -EPERM;
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
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dentry = xfs_handlereq_to_dentry(parfilp, hreq);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	/* Restrict this handle operation to symlinks only. */
	if (!d_is_symlink(dentry)) {
		error = -EINVAL;
		goto out_dput;
	}

	if (copy_from_user(&olen, hreq->ohandlen, sizeof(__u32))) {
		error = -EFAULT;
		goto out_dput;
	}

	error = vfs_readlink(dentry, hreq->ohandle, olen);

 out_dput:
	dput(dentry);
	return error;
}

int
xfs_set_dmattrs(
	xfs_inode_t     *ip,
	uint		evmask,
	uint16_t	state)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_trans_t	*tp;
	int		error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = evmask;
	ip->i_d.di_dmstate  = state;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	error = xfs_trans_commit(tp);

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

	if (IS_IMMUTABLE(d_inode(dentry)) || IS_APPEND(d_inode(dentry))) {
		error = -EPERM;
		goto out;
	}

	if (copy_from_user(&fsd, dmhreq.data, sizeof(fsd))) {
		error = -EFAULT;
		goto out;
	}

	error = xfs_set_dmattrs(XFS_I(d_inode(dentry)), fsd.fsd_dmevmask,
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
	struct xfs_fsop_attrlist_handlereq __user	*p = arg;
	xfs_fsop_attrlist_handlereq_t al_hreq;
	struct dentry		*dentry;
	char			*kbuf;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (copy_from_user(&al_hreq, arg, sizeof(xfs_fsop_attrlist_handlereq_t)))
		return -EFAULT;
	if (al_hreq.buflen < sizeof(struct attrlist) ||
	    al_hreq.buflen > XFS_XATTR_LIST_MAX)
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
	error = xfs_attr_list(XFS_I(d_inode(dentry)), kbuf, al_hreq.buflen,
					al_hreq.flags, cursor);
	if (error)
		goto out_kfree;

	if (copy_to_user(&p->pos, cursor, sizeof(attrlist_cursor_kern_t))) {
		error = -EFAULT;
		goto out_kfree;
	}

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
	uint32_t		*len,
	uint32_t		flags)
{
	unsigned char		*kbuf;
	int			error = -EFAULT;

	if (*len > XFS_XATTR_SIZE_MAX)
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
	uint32_t		len,
	uint32_t		flags)
{
	unsigned char		*kbuf;
	int			error;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	if (len > XFS_XATTR_SIZE_MAX)
		return -EINVAL;

	kbuf = memdup_user(ubuf, len);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	error = xfs_attr_set(XFS_I(inode), name, kbuf, len, flags);
	if (!error)
		xfs_forget_acl(inode, name, flags);
	kfree(kbuf);
	return error;
}

int
xfs_attrmulti_attr_remove(
	struct inode		*inode,
	unsigned char		*name,
	uint32_t		flags)
{
	int			error;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;
	error = xfs_attr_remove(XFS_I(inode), name, flags);
	if (!error)
		xfs_forget_acl(inode, name, flags);
	return error;
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
					d_inode(dentry), attr_name,
					ops[i].am_attrvalue, &ops[i].am_length,
					ops[i].am_flags);
			break;
		case ATTR_OP_SET:
			ops[i].am_error = mnt_want_write_file(parfilp);
			if (ops[i].am_error)
				break;
			ops[i].am_error = xfs_attrmulti_attr_set(
					d_inode(dentry), attr_name,
					ops[i].am_attrvalue, ops[i].am_length,
					ops[i].am_flags);
			mnt_drop_write_file(parfilp);
			break;
		case ATTR_OP_REMOVE:
			ops[i].am_error = mnt_want_write_file(parfilp);
			if (ops[i].am_error)
				break;
			ops[i].am_error = xfs_attrmulti_attr_remove(
					d_inode(dentry), attr_name,
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
	struct file		*filp,
	unsigned int		cmd,
	xfs_flock64_t		*bf)
{
	struct inode		*inode = file_inode(filp);
	struct xfs_inode	*ip = XFS_I(inode);
	struct iattr		iattr;
	enum xfs_prealloc_flags	flags = 0;
	uint			iolock = XFS_IOLOCK_EXCL;
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

	if (filp->f_flags & O_DSYNC)
		flags |= XFS_PREALLOC_SYNC;
	if (filp->f_mode & FMODE_NOCMTIME)
		flags |= XFS_PREALLOC_INVISIBLE;

	error = mnt_want_write_file(filp);
	if (error)
		return error;

	xfs_ilock(ip, iolock);
	error = xfs_break_layouts(inode, &iolock);
	if (error)
		goto out_unlock;

	xfs_ilock(ip, XFS_MMAPLOCK_EXCL);
	iolock |= XFS_MMAPLOCK_EXCL;

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
	    bf->l_start > inode->i_sb->s_maxbytes ||
	    bf->l_start + bf->l_len < 0 ||
	    bf->l_start + bf->l_len >= inode->i_sb->s_maxbytes) {
		error = -EINVAL;
		goto out_unlock;
	}

	switch (cmd) {
	case XFS_IOC_ZERO_RANGE:
		flags |= XFS_PREALLOC_SET;
		error = xfs_zero_file_space(ip, bf->l_start, bf->l_len);
		break;
	case XFS_IOC_RESVSP:
	case XFS_IOC_RESVSP64:
		flags |= XFS_PREALLOC_SET;
		error = xfs_alloc_file_space(ip, bf->l_start, bf->l_len,
						XFS_BMAPI_PREALLOC);
		break;
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_UNRESVSP64:
		error = xfs_free_file_space(ip, bf->l_start, bf->l_len);
		break;
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP:
	case XFS_IOC_FREESP64:
		flags |= XFS_PREALLOC_CLEAR;
		if (bf->l_start > XFS_ISIZE(ip)) {
			error = xfs_alloc_file_space(ip, XFS_ISIZE(ip),
					bf->l_start - XFS_ISIZE(ip), 0);
			if (error)
				goto out_unlock;
		}

		iattr.ia_valid = ATTR_SIZE;
		iattr.ia_size = bf->l_start;

		error = xfs_vn_setattr_size(file_dentry(filp), &iattr);
		break;
	default:
		ASSERT(0);
		error = -EINVAL;
	}

	if (error)
		goto out_unlock;

	error = xfs_update_prealloc_flags(ip, flags);

out_unlock:
	xfs_iunlock(ip, iolock);
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

	error = xfs_fs_geometry(&mp->m_sb, &fsgeo, 3);
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

	error = xfs_fs_geometry(&mp->m_sb, &fsgeo, 4);
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
		xflags |= FS_XFLAG_IMMUTABLE;
	else
		xflags &= ~FS_XFLAG_IMMUTABLE;
	if (flags & FS_APPEND_FL)
		xflags |= FS_XFLAG_APPEND;
	else
		xflags &= ~FS_XFLAG_APPEND;
	if (flags & FS_SYNC_FL)
		xflags |= FS_XFLAG_SYNC;
	else
		xflags &= ~FS_XFLAG_SYNC;
	if (flags & FS_NOATIME_FL)
		xflags |= FS_XFLAG_NOATIME;
	else
		xflags &= ~FS_XFLAG_NOATIME;
	if (flags & FS_NODUMP_FL)
		xflags |= FS_XFLAG_NODUMP;
	else
		xflags &= ~FS_XFLAG_NODUMP;

	return xflags;
}

STATIC unsigned int
xfs_di2lxflags(
	uint16_t	di_flags)
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
	fa.fsx_cowextsize = ip->i_d.di_cowextsize <<
			ip->i_mount->m_sb.sb_blocklog;
	fa.fsx_projid = xfs_get_projid(ip);

	if (attr) {
		if (ip->i_afp) {
			if (ip->i_afp->if_flags & XFS_IFEXTENTS)
				fa.fsx_nextents = xfs_iext_count(ip->i_afp);
			else
				fa.fsx_nextents = ip->i_d.di_anextents;
		} else
			fa.fsx_nextents = 0;
	} else {
		if (ip->i_df.if_flags & XFS_IFEXTENTS)
			fa.fsx_nextents = xfs_iext_count(&ip->i_df);
		else
			fa.fsx_nextents = ip->i_d.di_nextents;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (copy_to_user(arg, &fa, sizeof(fa)))
		return -EFAULT;
	return 0;
}

STATIC uint16_t
xfs_flags2diflags(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	/* can't set PREALLOC this way, just preserve it */
	uint16_t		di_flags =
		(ip->i_d.di_flags & XFS_DIFLAG_PREALLOC);

	if (xflags & FS_XFLAG_IMMUTABLE)
		di_flags |= XFS_DIFLAG_IMMUTABLE;
	if (xflags & FS_XFLAG_APPEND)
		di_flags |= XFS_DIFLAG_APPEND;
	if (xflags & FS_XFLAG_SYNC)
		di_flags |= XFS_DIFLAG_SYNC;
	if (xflags & FS_XFLAG_NOATIME)
		di_flags |= XFS_DIFLAG_NOATIME;
	if (xflags & FS_XFLAG_NODUMP)
		di_flags |= XFS_DIFLAG_NODUMP;
	if (xflags & FS_XFLAG_NODEFRAG)
		di_flags |= XFS_DIFLAG_NODEFRAG;
	if (xflags & FS_XFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;
	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (xflags & FS_XFLAG_NOSYMLINKS)
			di_flags |= XFS_DIFLAG_NOSYMLINKS;
		if (xflags & FS_XFLAG_EXTSZINHERIT)
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
		if (xflags & FS_XFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_REALTIME)
			di_flags |= XFS_DIFLAG_REALTIME;
		if (xflags & FS_XFLAG_EXTSIZE)
			di_flags |= XFS_DIFLAG_EXTSIZE;
	}

	return di_flags;
}

STATIC uint64_t
xfs_flags2diflags2(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	uint64_t		di_flags2 =
		(ip->i_d.di_flags2 & XFS_DIFLAG2_REFLINK);

	if (xflags & FS_XFLAG_DAX)
		di_flags2 |= XFS_DIFLAG2_DAX;
	if (xflags & FS_XFLAG_COWEXTSIZE)
		di_flags2 |= XFS_DIFLAG2_COWEXTSIZE;

	return di_flags2;
}

STATIC void
xfs_diflags_to_linux(
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip);
	unsigned int		xflags = xfs_ip2xflags(ip);

	if (xflags & FS_XFLAG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (xflags & FS_XFLAG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
	if (xflags & FS_XFLAG_SYNC)
		inode->i_flags |= S_SYNC;
	else
		inode->i_flags &= ~S_SYNC;
	if (xflags & FS_XFLAG_NOATIME)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;
#if 0	/* disabled until the flag switching races are sorted out */
	if (xflags & FS_XFLAG_DAX)
		inode->i_flags |= S_DAX;
	else
		inode->i_flags &= ~S_DAX;
#endif
}

static int
xfs_ioctl_setattr_xflags(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	struct fsxattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	uint64_t		di_flags2;

	/* Can't change realtime flag if any extents are allocated. */
	if ((ip->i_d.di_nextents || ip->i_delayed_blks) &&
	    XFS_IS_REALTIME_INODE(ip) != (fa->fsx_xflags & FS_XFLAG_REALTIME))
		return -EINVAL;

	/* If realtime flag is set then must have realtime device */
	if (fa->fsx_xflags & FS_XFLAG_REALTIME) {
		if (mp->m_sb.sb_rblocks == 0 || mp->m_sb.sb_rextsize == 0 ||
		    (ip->i_d.di_extsize % mp->m_sb.sb_rextsize))
			return -EINVAL;
	}

	/* Clear reflink if we are actually able to set the rt flag. */
	if ((fa->fsx_xflags & FS_XFLAG_REALTIME) && xfs_is_reflink_inode(ip))
		ip->i_d.di_flags2 &= ~XFS_DIFLAG2_REFLINK;

	/* Don't allow us to set DAX mode for a reflinked file for now. */
	if ((fa->fsx_xflags & FS_XFLAG_DAX) && xfs_is_reflink_inode(ip))
		return -EINVAL;

	/*
	 * Can't modify an immutable/append-only file unless
	 * we have appropriate permission.
	 */
	if (((ip->i_d.di_flags & (XFS_DIFLAG_IMMUTABLE | XFS_DIFLAG_APPEND)) ||
	     (fa->fsx_xflags & (FS_XFLAG_IMMUTABLE | FS_XFLAG_APPEND))) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		return -EPERM;

	/* diflags2 only valid for v3 inodes. */
	di_flags2 = xfs_flags2diflags2(ip, fa->fsx_xflags);
	if (di_flags2 && ip->i_d.di_version < 3)
		return -EINVAL;

	ip->i_d.di_flags = xfs_flags2diflags(ip, fa->fsx_xflags);
	ip->i_d.di_flags2 = di_flags2;

	xfs_diflags_to_linux(ip);
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	XFS_STATS_INC(mp, xs_ig_attrchg);
	return 0;
}

/*
 * If we are changing DAX flags, we have to ensure the file is clean and any
 * cached objects in the address space are invalidated and removed. This
 * requires us to lock out other IO and page faults similar to a truncate
 * operation. The locks need to be held until the transaction has been committed
 * so that the cache invalidation is atomic with respect to the DAX flag
 * manipulation.
 */
static int
xfs_ioctl_setattr_dax_invalidate(
	struct xfs_inode	*ip,
	struct fsxattr		*fa,
	int			*join_flags)
{
	struct inode		*inode = VFS_I(ip);
	struct super_block	*sb = inode->i_sb;
	int			error;

	*join_flags = 0;

	/*
	 * It is only valid to set the DAX flag on regular files and
	 * directories on filesystems where the block size is equal to the page
	 * size. On directories it serves as an inherit hint.
	 */
	if (fa->fsx_xflags & FS_XFLAG_DAX) {
		if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
			return -EINVAL;
		if (!bdev_dax_supported(xfs_find_bdev_for_inode(VFS_I(ip)),
				sb->s_blocksize))
			return -EINVAL;
	}

	/* If the DAX state is not changing, we have nothing to do here. */
	if ((fa->fsx_xflags & FS_XFLAG_DAX) && IS_DAX(inode))
		return 0;
	if (!(fa->fsx_xflags & FS_XFLAG_DAX) && !IS_DAX(inode))
		return 0;

	/* lock, flush and invalidate mapping in preparation for flag change */
	xfs_ilock(ip, XFS_MMAPLOCK_EXCL | XFS_IOLOCK_EXCL);
	error = filemap_write_and_wait(inode->i_mapping);
	if (error)
		goto out_unlock;
	error = invalidate_inode_pages2(inode->i_mapping);
	if (error)
		goto out_unlock;

	*join_flags = XFS_MMAPLOCK_EXCL | XFS_IOLOCK_EXCL;
	return 0;

out_unlock:
	xfs_iunlock(ip, XFS_MMAPLOCK_EXCL | XFS_IOLOCK_EXCL);
	return error;

}

/*
 * Set up the transaction structure for the setattr operation, checking that we
 * have permission to do so. On success, return a clean transaction and the
 * inode locked exclusively ready for further operation specific checks. On
 * failure, return an error without modifying or locking the inode.
 *
 * The inode might already be IO locked on call. If this is the case, it is
 * indicated in @join_flags and we take full responsibility for ensuring they
 * are unlocked from now on. Hence if we have an error here, we still have to
 * unlock them. Otherwise, once they are joined to the transaction, they will
 * be unlocked on commit/cancel.
 */
static struct xfs_trans *
xfs_ioctl_setattr_get_trans(
	struct xfs_inode	*ip,
	int			join_flags)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error = -EROFS;

	if (mp->m_flags & XFS_MOUNT_RDONLY)
		goto out_unlock;
	error = -EIO;
	if (XFS_FORCED_SHUTDOWN(mp))
		goto out_unlock;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		return ERR_PTR(error);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | join_flags);
	join_flags = 0;

	/*
	 * CAP_FOWNER overrides the following restrictions:
	 *
	 * The user ID of the calling process must be equal to the file owner
	 * ID, except in cases where the CAP_FSETID capability is applicable.
	 */
	if (!inode_owner_or_capable(VFS_I(ip))) {
		error = -EPERM;
		goto out_cancel;
	}

	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	return tp;

out_cancel:
	xfs_trans_cancel(tp);
out_unlock:
	if (join_flags)
		xfs_iunlock(ip, join_flags);
	return ERR_PTR(error);
}

/*
 * extent size hint validation is somewhat cumbersome. Rules are:
 *
 * 1. extent size hint is only valid for directories and regular files
 * 2. FS_XFLAG_EXTSIZE is only valid for regular files
 * 3. FS_XFLAG_EXTSZINHERIT is only valid for directories.
 * 4. can only be changed on regular files if no extents are allocated
 * 5. can be changed on directories at any time
 * 6. extsize hint of 0 turns off hints, clears inode flags.
 * 7. Extent size must be a multiple of the appropriate block size.
 * 8. for non-realtime files, the extent size hint must be limited
 *    to half the AG size to avoid alignment extending the extent beyond the
 *    limits of the AG.
 *
 * Please keep this function in sync with xfs_scrub_inode_extsize.
 */
static int
xfs_ioctl_setattr_check_extsize(
	struct xfs_inode	*ip,
	struct fsxattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;

	if ((fa->fsx_xflags & FS_XFLAG_EXTSIZE) && !S_ISREG(VFS_I(ip)->i_mode))
		return -EINVAL;

	if ((fa->fsx_xflags & FS_XFLAG_EXTSZINHERIT) &&
	    !S_ISDIR(VFS_I(ip)->i_mode))
		return -EINVAL;

	if (S_ISREG(VFS_I(ip)->i_mode) && ip->i_d.di_nextents &&
	    ((ip->i_d.di_extsize << mp->m_sb.sb_blocklog) != fa->fsx_extsize))
		return -EINVAL;

	if (fa->fsx_extsize != 0) {
		xfs_extlen_t    size;
		xfs_fsblock_t   extsize_fsb;

		extsize_fsb = XFS_B_TO_FSB(mp, fa->fsx_extsize);
		if (extsize_fsb > MAXEXTLEN)
			return -EINVAL;

		if (XFS_IS_REALTIME_INODE(ip) ||
		    (fa->fsx_xflags & FS_XFLAG_REALTIME)) {
			size = mp->m_sb.sb_rextsize << mp->m_sb.sb_blocklog;
		} else {
			size = mp->m_sb.sb_blocksize;
			if (extsize_fsb > mp->m_sb.sb_agblocks / 2)
				return -EINVAL;
		}

		if (fa->fsx_extsize % size)
			return -EINVAL;
	} else
		fa->fsx_xflags &= ~(FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT);

	return 0;
}

/*
 * CoW extent size hint validation rules are:
 *
 * 1. CoW extent size hint can only be set if reflink is enabled on the fs.
 *    The inode does not have to have any shared blocks, but it must be a v3.
 * 2. FS_XFLAG_COWEXTSIZE is only valid for directories and regular files;
 *    for a directory, the hint is propagated to new files.
 * 3. Can be changed on files & directories at any time.
 * 4. CoW extsize hint of 0 turns off hints, clears inode flags.
 * 5. Extent size must be a multiple of the appropriate block size.
 * 6. The extent size hint must be limited to half the AG size to avoid
 *    alignment extending the extent beyond the limits of the AG.
 *
 * Please keep this function in sync with xfs_scrub_inode_cowextsize.
 */
static int
xfs_ioctl_setattr_check_cowextsize(
	struct xfs_inode	*ip,
	struct fsxattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;

	if (!(fa->fsx_xflags & FS_XFLAG_COWEXTSIZE))
		return 0;

	if (!xfs_sb_version_hasreflink(&ip->i_mount->m_sb) ||
	    ip->i_d.di_version != 3)
		return -EINVAL;

	if (!S_ISREG(VFS_I(ip)->i_mode) && !S_ISDIR(VFS_I(ip)->i_mode))
		return -EINVAL;

	if (fa->fsx_cowextsize != 0) {
		xfs_extlen_t    size;
		xfs_fsblock_t   cowextsize_fsb;

		cowextsize_fsb = XFS_B_TO_FSB(mp, fa->fsx_cowextsize);
		if (cowextsize_fsb > MAXEXTLEN)
			return -EINVAL;

		size = mp->m_sb.sb_blocksize;
		if (cowextsize_fsb > mp->m_sb.sb_agblocks / 2)
			return -EINVAL;

		if (fa->fsx_cowextsize % size)
			return -EINVAL;
	} else
		fa->fsx_xflags &= ~FS_XFLAG_COWEXTSIZE;

	return 0;
}

static int
xfs_ioctl_setattr_check_projid(
	struct xfs_inode	*ip,
	struct fsxattr		*fa)
{
	/* Disallow 32bit project ids if projid32bit feature is not enabled. */
	if (fa->fsx_projid > (uint16_t)-1 &&
	    !xfs_sb_version_hasprojid32bit(&ip->i_mount->m_sb))
		return -EINVAL;

	/*
	 * Project Quota ID state is only allowed to change from within the init
	 * namespace. Enforce that restriction only if we are trying to change
	 * the quota ID state. Everything else is allowed in user namespaces.
	 */
	if (current_user_ns() == &init_user_ns)
		return 0;

	if (xfs_get_projid(ip) != fa->fsx_projid)
		return -EINVAL;
	if ((fa->fsx_xflags & FS_XFLAG_PROJINHERIT) !=
	    (ip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT))
		return -EINVAL;

	return 0;
}

STATIC int
xfs_ioctl_setattr(
	xfs_inode_t		*ip,
	struct fsxattr		*fa)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_dquot	*olddquot = NULL;
	int			code;
	int			join_flags = 0;

	trace_xfs_ioctl_setattr(ip);

	code = xfs_ioctl_setattr_check_projid(ip, fa);
	if (code)
		return code;

	/*
	 * If disk quotas is on, we make sure that the dquots do exist on disk,
	 * before we start any other transactions. Trying to do this later
	 * is messy. We don't care to take a readlock to look at the ids
	 * in inode here, because we can't hold it across the trans_reserve.
	 * If the IDs do change before we take the ilock, we're covered
	 * because the i_*dquot fields will get updated anyway.
	 */
	if (XFS_IS_QUOTA_ON(mp)) {
		code = xfs_qm_vop_dqalloc(ip, ip->i_d.di_uid,
					 ip->i_d.di_gid, fa->fsx_projid,
					 XFS_QMOPT_PQUOTA, &udqp, NULL, &pdqp);
		if (code)
			return code;
	}

	/*
	 * Changing DAX config may require inode locking for mapping
	 * invalidation. These need to be held all the way to transaction commit
	 * or cancel time, so need to be passed through to
	 * xfs_ioctl_setattr_get_trans() so it can apply them to the join call
	 * appropriately.
	 */
	code = xfs_ioctl_setattr_dax_invalidate(ip, fa, &join_flags);
	if (code)
		goto error_free_dquots;

	tp = xfs_ioctl_setattr_get_trans(ip, join_flags);
	if (IS_ERR(tp)) {
		code = PTR_ERR(tp);
		goto error_free_dquots;
	}


	if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_PQUOTA_ON(mp) &&
	    xfs_get_projid(ip) != fa->fsx_projid) {
		code = xfs_qm_vop_chown_reserve(tp, ip, udqp, NULL, pdqp,
				capable(CAP_FOWNER) ?  XFS_QMOPT_FORCE_RES : 0);
		if (code)	/* out of quota */
			goto error_trans_cancel;
	}

	code = xfs_ioctl_setattr_check_extsize(ip, fa);
	if (code)
		goto error_trans_cancel;

	code = xfs_ioctl_setattr_check_cowextsize(ip, fa);
	if (code)
		goto error_trans_cancel;

	code = xfs_ioctl_setattr_xflags(tp, ip, fa);
	if (code)
		goto error_trans_cancel;

	/*
	 * Change file ownership.  Must be the owner or privileged.  CAP_FSETID
	 * overrides the following restrictions:
	 *
	 * The set-user-ID and set-group-ID bits of a file will be cleared upon
	 * successful return from chown()
	 */

	if ((VFS_I(ip)->i_mode & (S_ISUID|S_ISGID)) &&
	    !capable_wrt_inode_uidgid(VFS_I(ip), CAP_FSETID))
		VFS_I(ip)->i_mode &= ~(S_ISUID|S_ISGID);

	/* Change the ownerships and register project quota modifications */
	if (xfs_get_projid(ip) != fa->fsx_projid) {
		if (XFS_IS_QUOTA_RUNNING(mp) && XFS_IS_PQUOTA_ON(mp)) {
			olddquot = xfs_qm_vop_chown(tp, ip,
						&ip->i_pdquot, pdqp);
		}
		ASSERT(ip->i_d.di_version > 1);
		xfs_set_projid(ip, fa->fsx_projid);
	}

	/*
	 * Only set the extent size hint if we've already determined that the
	 * extent size hint should be set on the inode. If no extent size flags
	 * are set on the inode then unconditionally clear the extent size hint.
	 */
	if (ip->i_d.di_flags & (XFS_DIFLAG_EXTSIZE | XFS_DIFLAG_EXTSZINHERIT))
		ip->i_d.di_extsize = fa->fsx_extsize >> mp->m_sb.sb_blocklog;
	else
		ip->i_d.di_extsize = 0;
	if (ip->i_d.di_version == 3 &&
	    (ip->i_d.di_flags2 & XFS_DIFLAG2_COWEXTSIZE))
		ip->i_d.di_cowextsize = fa->fsx_cowextsize >>
				mp->m_sb.sb_blocklog;
	else
		ip->i_d.di_cowextsize = 0;

	code = xfs_trans_commit(tp);

	/*
	 * Release any dquot(s) the inode had kept before chown.
	 */
	xfs_qm_dqrele(olddquot);
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(pdqp);

	return code;

error_trans_cancel:
	xfs_trans_cancel(tp);
error_free_dquots:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(pdqp);
	return code;
}

STATIC int
xfs_ioc_fssetxattr(
	xfs_inode_t		*ip,
	struct file		*filp,
	void			__user *arg)
{
	struct fsxattr		fa;
	int error;

	if (copy_from_user(&fa, arg, sizeof(fa)))
		return -EFAULT;

	error = mnt_want_write_file(filp);
	if (error)
		return error;
	error = xfs_ioctl_setattr(ip, &fa);
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
	struct xfs_inode	*ip,
	struct file		*filp,
	void			__user *arg)
{
	struct xfs_trans	*tp;
	struct fsxattr		fa;
	unsigned int		flags;
	int			join_flags = 0;
	int			error;

	if (copy_from_user(&flags, arg, sizeof(flags)))
		return -EFAULT;

	if (flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL | \
		      FS_NOATIME_FL | FS_NODUMP_FL | \
		      FS_SYNC_FL))
		return -EOPNOTSUPP;

	fa.fsx_xflags = xfs_merge_ioc_xflags(flags, xfs_ip2xflags(ip));

	error = mnt_want_write_file(filp);
	if (error)
		return error;

	/*
	 * Changing DAX config may require inode locking for mapping
	 * invalidation. These need to be held all the way to transaction commit
	 * or cancel time, so need to be passed through to
	 * xfs_ioctl_setattr_get_trans() so it can apply them to the join call
	 * appropriately.
	 */
	error = xfs_ioctl_setattr_dax_invalidate(ip, &fa, &join_flags);
	if (error)
		goto out_drop_write;

	tp = xfs_ioctl_setattr_get_trans(ip, join_flags);
	if (IS_ERR(tp)) {
		error = PTR_ERR(tp);
		goto out_drop_write;
	}

	error = xfs_ioctl_setattr_xflags(tp, ip, &fa);
	if (error) {
		xfs_trans_cancel(tp);
		goto out_drop_write;
	}

	error = xfs_trans_commit(tp);
out_drop_write:
	mnt_drop_write_file(filp);
	return error;
}

static bool
xfs_getbmap_format(
	struct kgetbmap		*p,
	struct getbmapx __user	*u,
	size_t			recsize)
{
	if (put_user(p->bmv_offset, &u->bmv_offset) ||
	    put_user(p->bmv_block, &u->bmv_block) ||
	    put_user(p->bmv_length, &u->bmv_length) ||
	    put_user(0, &u->bmv_count) ||
	    put_user(0, &u->bmv_entries))
		return false;
	if (recsize < sizeof(struct getbmapx))
		return true;
	if (put_user(0, &u->bmv_iflags) ||
	    put_user(p->bmv_oflags, &u->bmv_oflags) ||
	    put_user(0, &u->bmv_unused1) ||
	    put_user(0, &u->bmv_unused2))
		return false;
	return true;
}

STATIC int
xfs_ioc_getbmap(
	struct file		*file,
	unsigned int		cmd,
	void			__user *arg)
{
	struct getbmapx		bmx = { 0 };
	struct kgetbmap		*buf;
	size_t			recsize;
	int			error, i;

	switch (cmd) {
	case XFS_IOC_GETBMAPA:
		bmx.bmv_iflags = BMV_IF_ATTRFORK;
		/*FALLTHRU*/
	case XFS_IOC_GETBMAP:
		if (file->f_mode & FMODE_NOCMTIME)
			bmx.bmv_iflags |= BMV_IF_NO_DMAPI_READ;
		/* struct getbmap is a strict subset of struct getbmapx. */
		recsize = sizeof(struct getbmap);
		break;
	case XFS_IOC_GETBMAPX:
		recsize = sizeof(struct getbmapx);
		break;
	default:
		return -EINVAL;
	}

	if (copy_from_user(&bmx, arg, recsize))
		return -EFAULT;

	if (bmx.bmv_count < 2)
		return -EINVAL;
	if (bmx.bmv_count > ULONG_MAX / recsize)
		return -ENOMEM;

	buf = kmem_zalloc_large(bmx.bmv_count * sizeof(*buf), 0);
	if (!buf)
		return -ENOMEM;

	error = xfs_getbmap(XFS_I(file_inode(file)), &bmx, buf);
	if (error)
		goto out_free_buf;

	error = -EFAULT;
	if (copy_to_user(arg, &bmx, recsize))
		goto out_free_buf;
	arg += recsize;

	for (i = 0; i < bmx.bmv_entries; i++) {
		if (!xfs_getbmap_format(buf + i, arg, recsize))
			goto out_free_buf;
		arg += recsize;
	}

	error = 0;
out_free_buf:
	kmem_free(buf);
	return 0;
}

struct getfsmap_info {
	struct xfs_mount	*mp;
	struct fsmap_head __user *data;
	unsigned int		idx;
	__u32			last_flags;
};

STATIC int
xfs_getfsmap_format(struct xfs_fsmap *xfm, void *priv)
{
	struct getfsmap_info	*info = priv;
	struct fsmap		fm;

	trace_xfs_getfsmap_mapping(info->mp, xfm);

	info->last_flags = xfm->fmr_flags;
	xfs_fsmap_from_internal(&fm, xfm);
	if (copy_to_user(&info->data->fmh_recs[info->idx++], &fm,
			sizeof(struct fsmap)))
		return -EFAULT;

	return 0;
}

STATIC int
xfs_ioc_getfsmap(
	struct xfs_inode	*ip,
	struct fsmap_head	__user *arg)
{
	struct getfsmap_info	info = { NULL };
	struct xfs_fsmap_head	xhead = {0};
	struct fsmap_head	head;
	bool			aborted = false;
	int			error;

	if (copy_from_user(&head, arg, sizeof(struct fsmap_head)))
		return -EFAULT;
	if (memchr_inv(head.fmh_reserved, 0, sizeof(head.fmh_reserved)) ||
	    memchr_inv(head.fmh_keys[0].fmr_reserved, 0,
		       sizeof(head.fmh_keys[0].fmr_reserved)) ||
	    memchr_inv(head.fmh_keys[1].fmr_reserved, 0,
		       sizeof(head.fmh_keys[1].fmr_reserved)))
		return -EINVAL;

	xhead.fmh_iflags = head.fmh_iflags;
	xhead.fmh_count = head.fmh_count;
	xfs_fsmap_to_internal(&xhead.fmh_keys[0], &head.fmh_keys[0]);
	xfs_fsmap_to_internal(&xhead.fmh_keys[1], &head.fmh_keys[1]);

	trace_xfs_getfsmap_low_key(ip->i_mount, &xhead.fmh_keys[0]);
	trace_xfs_getfsmap_high_key(ip->i_mount, &xhead.fmh_keys[1]);

	info.mp = ip->i_mount;
	info.data = arg;
	error = xfs_getfsmap(ip->i_mount, &xhead, xfs_getfsmap_format, &info);
	if (error == XFS_BTREE_QUERY_RANGE_ABORT) {
		error = 0;
		aborted = true;
	} else if (error)
		return error;

	/* If we didn't abort, set the "last" flag in the last fmx */
	if (!aborted && info.idx) {
		info.last_flags |= FMR_OF_LAST;
		if (copy_to_user(&info.data->fmh_recs[info.idx - 1].fmr_flags,
				&info.last_flags, sizeof(info.last_flags)))
			return -EFAULT;
	}

	/* copy back header */
	head.fmh_entries = xhead.fmh_entries;
	head.fmh_oflags = xhead.fmh_oflags;
	if (copy_to_user(arg, &head, sizeof(struct fsmap_head)))
		return -EFAULT;

	return 0;
}

STATIC int
xfs_ioc_scrub_metadata(
	struct xfs_inode		*ip,
	void				__user *arg)
{
	struct xfs_scrub_metadata	scrub;
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&scrub, arg, sizeof(scrub)))
		return -EFAULT;

	error = xfs_scrub_metadata(ip, &scrub);
	if (error)
		return error;

	if (copy_to_user(arg, &scrub, sizeof(scrub)))
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

	/*
	 * We need to ensure that the fds passed in point to XFS inodes
	 * before we cast and access them as XFS structures as we have no
	 * control over what the user passes us here.
	 */
	if (f.file->f_op != &xfs_file_operations ||
	    tmp.file->f_op != &xfs_file_operations) {
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

static int
xfs_ioc_getlabel(
	struct xfs_mount	*mp,
	char			__user *user_label)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	char			label[XFSLABEL_MAX + 1];

	/* Paranoia */
	BUILD_BUG_ON(sizeof(sbp->sb_fname) > FSLABEL_MAX);

	spin_lock(&mp->m_sb_lock);
	strncpy(label, sbp->sb_fname, sizeof(sbp->sb_fname));
	spin_unlock(&mp->m_sb_lock);

	/* xfs on-disk label is 12 chars, be sure we send a null to user */
	label[XFSLABEL_MAX] = '\0';
	if (copy_to_user(user_label, label, sizeof(sbp->sb_fname)))
		return -EFAULT;
	return 0;
}

static int
xfs_ioc_setlabel(
	struct file		*filp,
	struct xfs_mount	*mp,
	char			__user *newlabel)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	char			label[XFSLABEL_MAX + 1];
	size_t			len;
	int			error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*
	 * The generic ioctl allows up to FSLABEL_MAX chars, but XFS is much
	 * smaller, at 12 bytes.  We copy one more to be sure we find the
	 * (required) NULL character to test the incoming label length.
	 * NB: The on disk label doesn't need to be null terminated.
	 */
	if (copy_from_user(label, newlabel, XFSLABEL_MAX + 1))
		return -EFAULT;
	len = strnlen(label, XFSLABEL_MAX + 1);
	if (len > sizeof(sbp->sb_fname))
		return -EINVAL;

	error = mnt_want_write_file(filp);
	if (error)
		return error;

	spin_lock(&mp->m_sb_lock);
	memset(sbp->sb_fname, 0, sizeof(sbp->sb_fname));
	strncpy(sbp->sb_fname, label, sizeof(sbp->sb_fname));
	spin_unlock(&mp->m_sb_lock);

	/*
	 * Now we do several things to satisfy userspace.
	 * In addition to normal logging of the primary superblock, we also
	 * immediately write these changes to sector zero for the primary, then
	 * update all backup supers (as xfs_db does for a label change), then
	 * invalidate the block device page cache.  This is so that any prior
	 * buffered reads from userspace (i.e. from blkid) are invalidated,
	 * and userspace will see the newly-written label.
	 */
	error = xfs_sync_sb_buf(mp);
	if (error)
		goto out;
	/*
	 * growfs also updates backup supers so lock against that.
	 */
	mutex_lock(&mp->m_growlock);
	error = xfs_update_secondary_sbs(mp);
	mutex_unlock(&mp->m_growlock);

	invalidate_bdev(mp->m_ddev_targp->bt_bdev);

out:
	mnt_drop_write_file(filp);
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
	int			error;

	trace_xfs_file_ioctl(ip);

	switch (cmd) {
	case FITRIM:
		return xfs_ioc_trim(mp, arg);
	case FS_IOC_GETFSLABEL:
		return xfs_ioc_getlabel(mp, arg);
	case FS_IOC_SETFSLABEL:
		return xfs_ioc_setlabel(filp, mp, arg);
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
		return xfs_ioc_space(filp, cmd, &bf);
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
	case XFS_IOC_GETBMAPX:
		return xfs_ioc_getbmap(filp, cmd, arg);

	case FS_IOC_GETFSMAP:
		return xfs_ioc_getfsmap(ip, arg);

	case XFS_IOC_SCRUB_METADATA:
		return xfs_ioc_scrub_metadata(ip, arg);

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
		uint64_t	   in;

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
		uint32_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(in, (uint32_t __user *)arg))
			return -EFAULT;

		return xfs_fs_goingdown(mp, in);
	}

	case XFS_IOC_ERROR_INJECTION: {
		xfs_error_injection_t in;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (copy_from_user(&in, arg, sizeof(in)))
			return -EFAULT;

		return xfs_errortag_add(mp, in.errtag);
	}

	case XFS_IOC_ERROR_CLEARALL:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		return xfs_errortag_clearall(mp);

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
