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
#include "xfs_vfs.h"
#include "xfs_vnode.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_dfrag.h"
#include "xfs_vnodeops.h"

#define  _NATIVE_IOC(cmd, type) \
	  _IOC(_IOC_DIR(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd), sizeof(type))

#if defined(CONFIG_IA64) || defined(CONFIG_X86_64)
#define BROKEN_X86_ALIGNMENT
#define _PACKED __attribute__((packed))
/* on ia32 l_start is on a 32-bit boundary */
typedef struct xfs_flock64_32 {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start	__attribute__((packed));
			/* len == 0 means until end of file */
	__s64		l_len __attribute__((packed));
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area */
} xfs_flock64_32_t;

#define XFS_IOC_ALLOCSP_32	_IOW ('X', 10, struct xfs_flock64_32)
#define XFS_IOC_FREESP_32	_IOW ('X', 11, struct xfs_flock64_32)
#define XFS_IOC_ALLOCSP64_32	_IOW ('X', 36, struct xfs_flock64_32)
#define XFS_IOC_FREESP64_32	_IOW ('X', 37, struct xfs_flock64_32)
#define XFS_IOC_RESVSP_32	_IOW ('X', 40, struct xfs_flock64_32)
#define XFS_IOC_UNRESVSP_32	_IOW ('X', 41, struct xfs_flock64_32)
#define XFS_IOC_RESVSP64_32	_IOW ('X', 42, struct xfs_flock64_32)
#define XFS_IOC_UNRESVSP64_32	_IOW ('X', 43, struct xfs_flock64_32)

/* just account for different alignment */
STATIC unsigned long
xfs_ioctl32_flock(
	unsigned long		arg)
{
	xfs_flock64_32_t	__user *p32 = (void __user *)arg;
	xfs_flock64_t		__user *p = compat_alloc_user_space(sizeof(*p));

	if (copy_in_user(&p->l_type,	&p32->l_type,	sizeof(s16)) ||
	    copy_in_user(&p->l_whence,	&p32->l_whence, sizeof(s16)) ||
	    copy_in_user(&p->l_start,	&p32->l_start,	sizeof(s64)) ||
	    copy_in_user(&p->l_len,	&p32->l_len,	sizeof(s64)) ||
	    copy_in_user(&p->l_sysid,	&p32->l_sysid,	sizeof(s32)) ||
	    copy_in_user(&p->l_pid,	&p32->l_pid,	sizeof(u32)) ||
	    copy_in_user(&p->l_pad,	&p32->l_pad,	4*sizeof(u32)))
		return -EFAULT;

	return (unsigned long)p;
}

typedef struct compat_xfs_fsop_geom_v1 {
	__u32		blocksize;	/* filesystem (data) block size */
	__u32		rtextsize;	/* realtime extent size		*/
	__u32		agblocks;	/* fsblocks in an AG		*/
	__u32		agcount;	/* number of allocation groups	*/
	__u32		logblocks;	/* fsblocks in the log		*/
	__u32		sectsize;	/* (data) sector size, bytes	*/
	__u32		inodesize;	/* inode size in bytes		*/
	__u32		imaxpct;	/* max allowed inode space(%)	*/
	__u64		datablocks;	/* fsblocks in data subvolume	*/
	__u64		rtblocks;	/* fsblocks in realtime subvol	*/
	__u64		rtextents;	/* rt extents in realtime subvol*/
	__u64		logstart;	/* starting fsblock of the log	*/
	unsigned char	uuid[16];	/* unique id of the filesystem	*/
	__u32		sunit;		/* stripe unit, fsblocks	*/
	__u32		swidth;		/* stripe width, fsblocks	*/
	__s32		version;	/* structure version		*/
	__u32		flags;		/* superblock version flags	*/
	__u32		logsectsize;	/* log sector size, bytes	*/
	__u32		rtsectsize;	/* realtime sector size, bytes	*/
	__u32		dirblocksize;	/* directory block size, bytes	*/
} __attribute__((packed)) compat_xfs_fsop_geom_v1_t;

#define XFS_IOC_FSGEOMETRY_V1_32  \
	_IOR ('X', 100, struct compat_xfs_fsop_geom_v1)

STATIC unsigned long xfs_ioctl32_geom_v1(unsigned long arg)
{
	compat_xfs_fsop_geom_v1_t __user *p32 = (void __user *)arg;
	xfs_fsop_geom_v1_t __user *p = compat_alloc_user_space(sizeof(*p));

	if (copy_in_user(p, p32, sizeof(*p32)))
		return -EFAULT;
	return (unsigned long)p;
}

typedef struct compat_xfs_inogrp {
	__u64		xi_startino;	/* starting inode number	*/
	__s32		xi_alloccount;	/* # bits set in allocmask	*/
	__u64		xi_allocmask;	/* mask of allocated inodes	*/
} __attribute__((packed)) compat_xfs_inogrp_t;

STATIC int xfs_inumbers_fmt_compat(
	void __user *ubuffer,
	const xfs_inogrp_t *buffer,
	long count,
	long *written)
{
	compat_xfs_inogrp_t __user *p32 = ubuffer;
	long i;

	for (i = 0; i < count; i++) {
		if (put_user(buffer[i].xi_startino,   &p32[i].xi_startino) ||
		    put_user(buffer[i].xi_alloccount, &p32[i].xi_alloccount) ||
		    put_user(buffer[i].xi_allocmask,  &p32[i].xi_allocmask))
			return -EFAULT;
	}
	*written = count * sizeof(*p32);
	return 0;
}

#else

#define xfs_inumbers_fmt_compat xfs_inumbers_fmt
#define _PACKED

#endif

/* XFS_IOC_FSBULKSTAT and friends */

typedef struct compat_xfs_bstime {
	__s32		tv_sec;		/* seconds		*/
	__s32		tv_nsec;	/* and nanoseconds	*/
} compat_xfs_bstime_t;

STATIC int xfs_bstime_store_compat(
	compat_xfs_bstime_t __user *p32,
	const xfs_bstime_t *p)
{
	__s32 sec32;

	sec32 = p->tv_sec;
	if (put_user(sec32, &p32->tv_sec) ||
	    put_user(p->tv_nsec, &p32->tv_nsec))
		return -EFAULT;
	return 0;
}

typedef struct compat_xfs_bstat {
	__u64		bs_ino;		/* inode number			*/
	__u16		bs_mode;	/* type and mode		*/
	__u16		bs_nlink;	/* number of links		*/
	__u32		bs_uid;		/* user id			*/
	__u32		bs_gid;		/* group id			*/
	__u32		bs_rdev;	/* device value			*/
	__s32		bs_blksize;	/* block size			*/
	__s64		bs_size;	/* file size			*/
	compat_xfs_bstime_t bs_atime;	/* access time			*/
	compat_xfs_bstime_t bs_mtime;	/* modify time			*/
	compat_xfs_bstime_t bs_ctime;	/* inode change time		*/
	int64_t		bs_blocks;	/* number of blocks		*/
	__u32		bs_xflags;	/* extended flags		*/
	__s32		bs_extsize;	/* extent size			*/
	__s32		bs_extents;	/* number of extents		*/
	__u32		bs_gen;		/* generation count		*/
	__u16		bs_projid;	/* project id			*/
	unsigned char	bs_pad[14];	/* pad space, unused		*/
	__u32		bs_dmevmask;	/* DMIG event mask		*/
	__u16		bs_dmstate;	/* DMIG state info		*/
	__u16		bs_aextents;	/* attribute number of extents	*/
} _PACKED compat_xfs_bstat_t;

STATIC int xfs_bulkstat_one_fmt_compat(
	void			__user *ubuffer,
	const xfs_bstat_t	*buffer)
{
	compat_xfs_bstat_t __user *p32 = ubuffer;

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
		return -EFAULT;
	return sizeof(*p32);
}



typedef struct compat_xfs_fsop_bulkreq {
	compat_uptr_t	lastip;		/* last inode # pointer		*/
	__s32		icount;		/* count of entries in buffer	*/
	compat_uptr_t	ubuffer;	/* user buffer for inode desc.	*/
	compat_uptr_t	ocount;		/* output count pointer		*/
} compat_xfs_fsop_bulkreq_t;

#define XFS_IOC_FSBULKSTAT_32 \
	_IOWR('X', 101, struct compat_xfs_fsop_bulkreq)
#define XFS_IOC_FSBULKSTAT_SINGLE_32 \
	_IOWR('X', 102, struct compat_xfs_fsop_bulkreq)
#define XFS_IOC_FSINUMBERS_32 \
	_IOWR('X', 103, struct compat_xfs_fsop_bulkreq)

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
		return -EPERM;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -XFS_ERROR(EIO);

	if (get_user(addr, &p32->lastip))
		return -EFAULT;
	bulkreq.lastip = compat_ptr(addr);
	if (get_user(bulkreq.icount, &p32->icount) ||
	    get_user(addr, &p32->ubuffer))
		return -EFAULT;
	bulkreq.ubuffer = compat_ptr(addr);
	if (get_user(addr, &p32->ocount))
		return -EFAULT;
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



typedef struct compat_xfs_fsop_handlereq {
	__u32		fd;		/* fd for FD_TO_HANDLE		*/
	compat_uptr_t	path;		/* user pathname		*/
	__u32		oflags;		/* open flags			*/
	compat_uptr_t	ihandle;	/* user supplied handle		*/
	__u32		ihandlen;	/* user supplied length		*/
	compat_uptr_t	ohandle;	/* user buffer for handle	*/
	compat_uptr_t	ohandlen;	/* user buffer length		*/
} compat_xfs_fsop_handlereq_t;

#define XFS_IOC_PATH_TO_FSHANDLE_32 \
	_IOWR('X', 104, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_PATH_TO_HANDLE_32 \
	_IOWR('X', 105, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_FD_TO_HANDLE_32 \
	_IOWR('X', 106, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_OPEN_BY_HANDLE_32 \
	_IOWR('X', 107, struct compat_xfs_fsop_handlereq)
#define XFS_IOC_READLINK_BY_HANDLE_32 \
	_IOWR('X', 108, struct compat_xfs_fsop_handlereq)

STATIC unsigned long xfs_ioctl32_fshandle(unsigned long arg)
{
	compat_xfs_fsop_handlereq_t __user *p32 = (void __user *)arg;
	xfs_fsop_handlereq_t __user *p = compat_alloc_user_space(sizeof(*p));
	u32 addr;

	if (copy_in_user(&p->fd, &p32->fd, sizeof(__u32)) ||
	    get_user(addr, &p32->path) ||
	    put_user(compat_ptr(addr), &p->path) ||
	    copy_in_user(&p->oflags, &p32->oflags, sizeof(__u32)) ||
	    get_user(addr, &p32->ihandle) ||
	    put_user(compat_ptr(addr), &p->ihandle) ||
	    copy_in_user(&p->ihandlen, &p32->ihandlen, sizeof(__u32)) ||
	    get_user(addr, &p32->ohandle) ||
	    put_user(compat_ptr(addr), &p->ohandle) ||
	    get_user(addr, &p32->ohandlen) ||
	    put_user(compat_ptr(addr), &p->ohandlen))
		return -EFAULT;

	return (unsigned long)p;
}


STATIC long
xfs_compat_ioctl(
	int		mode,
	struct file	*file,
	unsigned	cmd,
	unsigned long	arg)
{
	struct inode	*inode = file->f_path.dentry->d_inode;
	int		error;

	switch (cmd) {
	case XFS_IOC_DIOINFO:
	case XFS_IOC_FSGEOMETRY:
	case XFS_IOC_GETVERSION:
	case XFS_IOC_GETXFLAGS:
	case XFS_IOC_SETXFLAGS:
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

#ifdef BROKEN_X86_ALIGNMENT
	/* xfs_flock_t has wrong u32 vs u64 alignment */
	case XFS_IOC_ALLOCSP_32:
	case XFS_IOC_FREESP_32:
	case XFS_IOC_ALLOCSP64_32:
	case XFS_IOC_FREESP64_32:
	case XFS_IOC_RESVSP_32:
	case XFS_IOC_UNRESVSP_32:
	case XFS_IOC_RESVSP64_32:
	case XFS_IOC_UNRESVSP64_32:
		arg = xfs_ioctl32_flock(arg);
		cmd = _NATIVE_IOC(cmd, struct xfs_flock64);
		break;
	case XFS_IOC_FSGEOMETRY_V1_32:
		arg = xfs_ioctl32_geom_v1(arg);
		cmd = _NATIVE_IOC(cmd, struct xfs_fsop_geom_v1);
		break;

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

	/* xfs_bstat_t still has wrong u32 vs u64 alignment */
	case XFS_IOC_SWAPEXT:
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
	case XFS_IOC_PATH_TO_FSHANDLE_32:
	case XFS_IOC_OPEN_BY_HANDLE_32:
	case XFS_IOC_READLINK_BY_HANDLE_32:
		arg = xfs_ioctl32_fshandle(arg);
		cmd = _NATIVE_IOC(cmd, struct xfs_fsop_handlereq);
		break;
	default:
		return -ENOIOCTLCMD;
	}

	error = xfs_ioctl(XFS_I(inode), file, mode, cmd, (void __user *)arg);
	xfs_iflags_set(XFS_I(inode), XFS_IMODIFIED);

	return error;
}

long
xfs_file_compat_ioctl(
	struct file		*file,
	unsigned		cmd,
	unsigned long		arg)
{
	return xfs_compat_ioctl(0, file, cmd, arg);
}

long
xfs_file_compat_invis_ioctl(
	struct file		*file,
	unsigned		cmd,
	unsigned long		arg)
{
	return xfs_compat_ioctl(IO_INVIS, file, cmd, arg);
}
