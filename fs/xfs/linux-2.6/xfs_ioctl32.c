/*
 * Copyright (c) 2004 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <linux/config.h>
#include <linux/compat.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/ioctl32.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include "xfs.h"
#include "xfs_types.h"
#include "xfs_fs.h"
#include "xfs_vfs.h"
#include "xfs_vnode.h"
#include "xfs_dfrag.h"

#if defined(CONFIG_IA64) || defined(CONFIG_X86_64)
#define BROKEN_X86_ALIGNMENT
#else

typedef struct xfs_fsop_bulkreq32 {
	compat_uptr_t	lastip;		/* last inode # pointer		*/
	__s32		icount;		/* count of entries in buffer	*/
	compat_uptr_t	ubuffer;	/* user buffer for inode desc.	*/
	__s32		ocount;		/* output count pointer		*/
} xfs_fsop_bulkreq32_t;

static unsigned long
xfs_ioctl32_bulkstat(unsigned long arg)
{
	xfs_fsop_bulkreq32_t	__user *p32 = (void __user *)arg;
	xfs_fsop_bulkreq_t	__user *p = compat_alloc_user_space(sizeof(*p));
	u32			addr;

	if (get_user(addr, &p32->lastip) ||
	    put_user(compat_ptr(addr), &p->lastip) ||
	    copy_in_user(&p->icount, &p32->icount, sizeof(s32)) ||
	    get_user(addr, &p32->ubuffer) ||
	    put_user(compat_ptr(addr), &p->ubuffer) ||
	    get_user(addr, &p32->ocount) ||
	    put_user(compat_ptr(addr), &p->ocount))
		return -EFAULT;

	return (unsigned long)p;
}
#endif

static long
__xfs_compat_ioctl(int mode, struct file *f, unsigned cmd, unsigned long arg)
{
	int		error;
	struct inode *inode = f->f_dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	switch (cmd) {
	case XFS_IOC_DIOINFO:
	case XFS_IOC_FSGEOMETRY_V1:
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
	case XFS_IOC_FD_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE:
	case XFS_IOC_PATH_TO_HANDLE:
	case XFS_IOC_PATH_TO_FSHANDLE:
	case XFS_IOC_OPEN_BY_HANDLE:
	case XFS_IOC_FSSETDM_BY_HANDLE:
	case XFS_IOC_READLINK_BY_HANDLE:
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

#ifndef BROKEN_X86_ALIGNMENT
	/* xfs_flock_t and xfs_bstat_t have wrong u32 vs u64 alignment */
	case XFS_IOC_ALLOCSP:
	case XFS_IOC_FREESP:
	case XFS_IOC_RESVSP:
	case XFS_IOC_UNRESVSP:
	case XFS_IOC_ALLOCSP64:
	case XFS_IOC_FREESP64:
	case XFS_IOC_RESVSP64:
	case XFS_IOC_UNRESVSP64:
	case XFS_IOC_SWAPEXT:
		break;

	case XFS_IOC_FSBULKSTAT_SINGLE:
	case XFS_IOC_FSBULKSTAT:
	case XFS_IOC_FSINUMBERS:
		arg = xfs_ioctl32_bulkstat(arg);
		break;
#endif
	default:
		return -ENOIOCTLCMD;
	}

	VOP_IOCTL(vp, inode, f, mode, cmd, (void __user *)arg, error);
	VMODIFY(vp);

	return error;
}

long xfs_compat_ioctl(struct file *f, unsigned cmd, unsigned long arg)
{
	return __xfs_compat_ioctl(0, f, cmd, arg);
}

long xfs_compat_invis_ioctl(struct file *f, unsigned cmd, unsigned long arg)
{
	return __xfs_compat_ioctl(IO_INVIS, f, cmd, arg);
}
