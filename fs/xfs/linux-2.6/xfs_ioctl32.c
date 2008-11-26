/*
 * Copyright (c) 2004-2005 Silicon Graphics, Inc.
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
#include <linux/compat.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_vnode.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_dfrag.h"
#include "xfs_vnodeops.h"
#include "xfs_fsops.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_ioctl.h"
#include "xfs_ioctl32.h"

#define  _NATIVE_IOC(cmd, type) \
	  _IOC(_IOC_DIR(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd), sizeof(type))

#ifdef BROKEN_X86_ALIGNMENT
STATIC int
xfs_compat_flock64_copyin(
	xfs_flock64_t		*bf,
	compat_xfs_flock64_t	__user *arg32)
{
	if (get_user(bf->l_type,	&arg32->l_type) ||
	    get_user(bf->l_whence,	&arg32->l_whence) ||
	    get_user(bf->l_start,	&arg32->l_start) ||
	    get_user(bf->l_len,		&arg32->l_len) ||
	    get_user(bf->l_sysid,	&arg32->l_sysid) ||
	    get_user(bf->l_pid,		&arg32->l_pid) ||
	    copy_from_user(bf->l_pad,	&arg32->l_pad,	4*sizeof(u32)))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_compat_ioc_fsgeometry_v1(
	struct xfs_mount	  *mp,
	compat_xfs_fsop_geom_v1_t __user *arg32)
{
	xfs_fsop_geom_t		  fsgeo;
	int			  error;

	error = xfs_fs_geometry(mp, &fsgeo, 3);
	if (error)
		return -error;
	/* The 32-bit variant simply has some padding at the end */
	if (copy_to_user(arg32, &fsgeo, sizeof(struct compat_xfs_fsop_geom_v1)))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_compat_growfs_data_copyin(
	struct xfs_growfs_data	 *in,
	compat_xfs_growfs_data_t __user *arg32)
{
	if (get_user(in->newblocks, &arg32->newblocks) ||
	    get_user(in->imaxpct,   &arg32->imaxpct))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_compat_growfs_rt_copyin(
	struct xfs_growfs_rt	 *in,
	compat_xfs_growfs_rt_t	__user *arg32)
{
	if (get_user(in->newblocks, &arg32->newblocks) ||
	    get_user(in->extsize,   &arg32->extsize))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_inumbers_fmt_compat(
	void			__user *ubuffer,
	const xfs_inogrp_t	*buffer,
	long			count,
	long			*written)
{
	compat_xfs_inogrp_t	__user *p32 = ubuffer;
	long			i;

	for (i = 0; i < count; i++) {
		if (put_user(buffer[i].xi_startino,   &p32[i].xi_startino) ||
		    put_user(buffer[i].xi_alloccount, &p32[i].xi_alloccount) ||
		    put_user(buffer[i].xi_allocmask,  &p32[i].xi_allocmask))
			return -XFS_ERROR(EFAULT);
	}
	*written = count * sizeof(*p32);
	return 0;
}

#else
#define xfs_inumbers_fmt_compat xfs_inumbers_fmt
#endif

STATIC int
xfs_ioctl32_bstime_copyin(
	xfs_bstime_t		*bstime,
	compat_xfs_bstime_t	__user *bstime32)
{
	compat_time_t		sec32;	/* tv_sec differs on 64 vs. 32 */

	if (get_user(sec32,		&bstime32->tv_sec)	||
	    get_user(bstime->tv_nsec,	&bstime32->tv_nsec))
		return -XFS_ERROR(EFAULT);
	bstime->tv_sec = sec32;
	return 0;
}

/* xfs_bstat_t has differing alignment on intel, & bstime_t sizes everywhere */
STATIC int
xfs_ioctl32_bstat_copyin(
	xfs_bstat_t		*bstat,
	compat_xfs_bstat_t	__user *bstat32)
{
	if (get_user(bstat->bs_ino,	&bstat32->bs_ino)	||
	    get_user(bstat->bs_mode,	&bstat32->bs_mode)	||
	    get_user(bstat->bs_nlink,	&bstat32->bs_nlink)	||
	    get_user(bstat->bs_uid,	&bstat32->bs_uid)	||
	    get_user(bstat->bs_gid,	&bstat32->bs_gid)	||
	    get_user(bstat->bs_rdev,	&bstat32->bs_rdev)	||
	    get_user(bstat->bs_blksize,	&bstat32->bs_blksize)	||
	    get_user(bstat->bs_size,	&bstat32->bs_size)	||
	    xfs_ioctl32_bstime_copyin(&bstat->bs_atime, &bstat32->bs_atime) ||
	    xfs_ioctl32_bstime_copyin(&bstat->bs_mtime, &bstat32->bs_mtime) ||
	    xfs_ioctl32_bstime_copyin(&bstat->bs_ctime, &bstat32->bs_ctime) ||
	    get_user(bstat->bs_blocks,	&bstat32->bs_size)	||
	    get_user(bstat->bs_xflags,	&bstat32->bs_size)	||
	    get_user(bstat->bs_extsize,	&bstat32->bs_extsize)	||
	    get_user(bstat->bs_extents,	&bstat32->bs_extents)	||
	    get_user(bstat->bs_gen,	&bstat32->bs_gen)	||
	    get_user(bstat->bs_projid,	&bstat32->bs_projid)	||
	    get_user(bstat->bs_dmevmask, &bstat32->bs_dmevmask)	||
	    get_user(bstat->bs_dmstate,	&bstat32->bs_dmstate)	||
	    get_user(bstat->bs_aextents, &bstat32->bs_aextents))
		return -XFS_ERROR(EFAULT);
	return 0;
}

/* XFS_IOC_FSBULKSTAT and friends */

STATIC int
xfs_bstime_store_compat(
	compat_xfs_bstime_t	__user *p32,
	const xfs_bstime_t	*p)
{
	__s32			sec32;

	sec32 = p->tv_sec;
	if (put_user(sec32, &p32->tv_sec) ||
	    put_user(p->tv_nsec, &p32->tv_nsec))
		return -XFS_ERROR(EFAULT);
	return 0;
}

STATIC int
xfs_bulkstat_one_fmt_compat(
	void			__user *ubuffer,
	const xfs_bstat_t	*buffer)
{
	compat_xfs_bstat_t	__user *p32 = ubuffer;

	if (put_user(buffer->bs_ino, &p32->bs_ino) ||
	    put_user(buffer->bs_mode, &p32->bs_mode) ||
	    put_user(buffer->bs_nlink, &p32->bs_nlink) ||
	    put_user(buffer->bs_uid, &p32->bs_uid) ||
	    put_user(buffer->bs_gid, &p32->bs_gid) ||
	    put_user(buffer->bs_rdev, &p32->bs_rdev) ||
	    put_user(buffer->bs_blksize, &p32->bs_blksize) ||
	    put_user(buffer->bs_size, &p32->bs_size) ||
	    xfs_bstime_store_compat(&p32->bs_atime, &buffer->bs_atime) ||
	    xfs_bstime_store_compat(&p32->bs_mtime, &buffer->bs_mtime) ||
	    xfs_bstime_store_compat(&p32->bs_ctime, &buffer->bs_ctime) ||
	    put_user(buffer->bs_blocks, &p32->bs_blocks) ||
	    put_user(buffer->bs_xflags, &p32->bs_xflags) ||
	    put_user(buffer->bs_extsize, &p32->bs_extsize) ||
	    put_user(buffer->bs_extents, &p32->bs_extents) ||
	    put_user(buffer->bs_gen, &p32->bs_gen) ||
	    put_user(buffer->bs_projid, &p32->bs_projid) ||
	    put_user(buffer->bs_dmevmask, &p32->bs_dmevmask) ||
	    put_user(buffer->bs_dmstate, &p32->bs_dmstate) ||
	    put_user(buffer->bs_aextents, &p32->bs_aextents))
		return -XFS_ERROR(EFAULT);
	return sizeof(*p32);
}

/* copied from xfs_ioctl.c */
STATIC int
xfs_ioc_bulkstat_compat(
	xfs_mount_t		*mp,
	unsigned int		cmd,
	void			__user *arg)
{
	compat_xfs_fsop_bulkreq_t __user *p32 = (void __user *)arg;
	u32			addr;
	xfs_fsop_bulkreq_t	bulkreq;
	int			count;	/* # of records returned */
	xfs_ino_t		inlast;	/* last inode number */
	int			done;
	int			error;

	/* done = 1 if there are more stats to get and if bulkstat */
	/* should be called again (unused here, but used in dmapi) */

	if (!capable(CAP_SYS_ADMIN))
		return -XFS_ERROR(EPERM);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -XFS_ERROR(EIO);

	if (get_user(addr, &p32->lastip))
		return -XFS_ERROR(EFAULT);
	bulkreq.lastip = compat_ptr(addr);
	if (get_user(bulkreq.icount, &p32->icount) ||
	    get_user(addr, &p32->ubuffer))
		return -XFS_ERROR(EFAULT);
	bulkreq.ubuffer = compat_ptr(addr);
	if (get_user(addr, &p32->ocount))
		return -XFS_ERROR(EFAULT);
	bulkreq.ocount = compat_ptr(addr);

	if (copy_from_user(&inlast, bulkreq.lastip, sizeof(__s64)))
		return -XFS_ERROR(EFAULT);

	if ((count = bulkreq.icount) <= 0)
		return -XFS_ERROR(EINVAL);

	if (bulkreq.ubuffer == NULL)
		return -XFS_ERROR(EINVAL);

	if (cmd == XFS_IOC_FSINUMBERS)
		error = xfs_inumbers(mp, &inlast, &count,
				bulkreq.ubuffer, xfs_inumbers_fmt_compat);
	else {
		/* declare a var to get a warning in case the type changes */
		bulkstat_one_fmt_pf formatter = xfs_bulkstat_one_fmt_compat;
		error = xfs_bulkstat(mp, &inlast, &count,
			xfs_bulkstat_one, formatter,
			sizeof(compat_xfs_bstat_t), bulkreq.ubuffer,
			BULKSTAT_FG_QUICK, &done);
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
xfs_compat_handlereq_copyin(
	xfs_fsop_handlereq_t		*hreq,
	compat_xfs_fsop_handlereq_t	__user *arg32)
{
	compat_xfs_fsop_handlereq_t	hreq32;

	if (copy_from_user(&hreq32, arg32, sizeof(compat_xfs_fsop_handlereq_t)))
		return -XFS_ERROR(EFAULT);

	hreq->fd = hreq32.fd;
	hreq->path = compat_ptr(hreq32.path);
	hreq->oflags = hreq32.oflags;
	hreq->ihandle = compat_ptr(hreq32.ihandle);
	hreq->ihandlen = hreq32.ihandlen;
	hreq->ohandle = compat_ptr(hreq32.ohandle);
	hreq->ohandlen = compat_ptr(hreq32.ohandlen);

	return 0;
}

STATIC long
xfs_compat_ioctl(
	xfs_inode_t	*ip,
	struct file	*filp,
	int		ioflags,
	unsigned	cmd,
	void		__user *arg)
{
	struct inode	*inode = filp->f_path.dentry->d_inode;
	xfs_mount_t	*mp = ip->i_mount;
	int		error;

	xfs_itrace_entry(XFS_I(inode));
	switch (cmd) {
	case XFS_IOC_DIOINFO:
	case XFS_IOC_FSGEOMETRY:
	case XFS_IOC_FSGETXATTR:
	case XFS_IOC_FSSETXATTR:
	case XFS_IOC_FSGETXATTRA:
	case XFS_IOC_FSSETDM:
	case XFS_IOC_GETBMAP:
	case XFS_IOC_GETBMAPA:
	case XFS_IOC_GETBMAPX:
/* not handled
	case XFS_IOC_FSSETDM_BY_HANDLE:
	case XFS_IOC_ATTRLIST_BY_HANDLE:
	case XFS_IOC_ATTRMULTI_BY_HANDLE:
*/
	case XFS_IOC_FSCOUNTS:
	case XFS_IOC_SET_RESBLKS:
	case XFS_IOC_GET_RESBLKS:
	case XFS_IOC_FSGROWFSDATA:
	case XFS_IOC_FSGROWFSLOG:
	case XFS_IOC_FSGROWFSRT:
	case XFS_IOC_FREEZE:
	case XFS_IOC_THAW:
	case XFS_IOC_GOINGDOWN:
	case XFS_IOC_ERROR_INJECTION:
	case XFS_IOC_ERROR_CLEARALL:
		break;

	case XFS_IOC_GETXFLAGS_32:
	case XFS_IOC_SETXFLAGS_32:
	case XFS_IOC_GETVERSION_32:
		cmd = _NATIVE_IOC(cmd, long);
		break;
	case XFS_IOC_SWAPEXT: {
		struct xfs_swapext	  sxp;
		struct compat_xfs_swapext __user *sxu = arg;

		/* Bulk copy in up to the sx_stat field, then grab bstat */
		if (copy_from_user(&sxp, sxu,
				   offsetof(xfs_swapext_t, sx_stat)) ||
		    xfs_ioctl32_bstat_copyin(&sxp.sx_stat, &sxu->sx_stat))
			return -XFS_ERROR(EFAULT);
		error = xfs_swapext(&sxp);
		return -error;
	}
#ifdef BROKEN_X86_ALIGNMENT
	/* xfs_flock_t has wrong u32 vs u64 alignment */
	case XFS_IOC_ALLOCSP_32:
	case XFS_IOC_FREESP_32:
	case XFS_IOC_ALLOCSP64_32:
	case XFS_IOC_FREESP64_32:
	case XFS_IOC_RESVSP_32:
	case XFS_IOC_UNRESVSP_32:
	case XFS_IOC_RESVSP64_32:
	case XFS_IOC_UNRESVSP64_32: {
		struct xfs_flock64	bf;

		if (xfs_compat_flock64_copyin(&bf, arg))
			return -XFS_ERROR(EFAULT);
		cmd = _NATIVE_IOC(cmd, struct xfs_flock64);
		return xfs_ioc_space(ip, inode, filp, ioflags, cmd, &bf);
	}
	case XFS_IOC_FSGEOMETRY_V1_32:
		return xfs_compat_ioc_fsgeometry_v1(mp, arg);
	case XFS_IOC_FSGROWFSDATA_32: {
		struct xfs_growfs_data	in;

		if (xfs_compat_growfs_data_copyin(&in, arg))
			return -XFS_ERROR(EFAULT);
		error = xfs_growfs_data(mp, &in);
		return -error;
	}
	case XFS_IOC_FSGROWFSRT_32: {
		struct xfs_growfs_rt	in;

		if (xfs_compat_growfs_rt_copyin(&in, arg))
			return -XFS_ERROR(EFAULT);
		error = xfs_growfs_rt(mp, &in);
		return -error;
	}
#else /* These are handled fine if no alignment issues */
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:
	case XFS_IOC_RESVSP:
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:
	case XFS_IOC_RESVSP64:
	case XFS_IOC_UNRESVSP64:
	case XFS_IOC_FSGEOMETRY_V1:
		break;
#endif
	case XFS_IOC_FSBULKSTAT_32:
	case XFS_IOC_FSBULKSTAT_SINGLE_32:
	case XFS_IOC_FSINUMBERS_32:
		cmd = _NATIVE_IOC(cmd, struct xfs_fsop_bulkreq);
		return xfs_ioc_bulkstat_compat(XFS_I(inode)->i_mount,
				cmd, (void __user*)arg);
	case XFS_IOC_FD_TO_HANDLE_32:
	case XFS_IOC_PATH_TO_HANDLE_32:
	case XFS_IOC_PATH_TO_FSHANDLE_32: {
		struct xfs_fsop_handlereq	hreq;

		if (xfs_compat_handlereq_copyin(&hreq, arg))
			return -XFS_ERROR(EFAULT);
		cmd = _NATIVE_IOC(cmd, struct xfs_fsop_handlereq);
		return xfs_find_handle(cmd, &hreq);
	}
	case XFS_IOC_OPEN_BY_HANDLE_32: {
		struct xfs_fsop_handlereq	hreq;

		if (xfs_compat_handlereq_copyin(&hreq, arg))
			return -XFS_ERROR(EFAULT);
		return xfs_open_by_handle(mp, &hreq, filp, inode);
	}
	case XFS_IOC_READLINK_BY_HANDLE_32: {
		struct xfs_fsop_handlereq	hreq;

		if (xfs_compat_handlereq_copyin(&hreq, arg))
			return -XFS_ERROR(EFAULT);
		return xfs_readlink_by_handle(mp, &hreq, inode);
	}
	default:
		return -XFS_ERROR(ENOIOCTLCMD);
	}

	error = xfs_ioctl(ip, filp, ioflags, cmd, arg);
	return error;
}

long
xfs_file_compat_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p)
{
	struct inode	*inode = filp->f_path.dentry->d_inode;

	return xfs_compat_ioctl(XFS_I(inode), filp, 0, cmd, (void __user *)p);
}

long
xfs_file_compat_invis_ioctl(
	struct file		*filp,
	unsigned int		cmd,
	unsigned long		p)
{
	struct inode	*inode = filp->f_path.dentry->d_inode;

	return xfs_compat_ioctl(XFS_I(inode), filp, IO_INVIS, cmd,
				(void __user *)p);
}
