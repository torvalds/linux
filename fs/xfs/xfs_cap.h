/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_CAP_H__
#define __XFS_CAP_H__

/*
 * Capabilities
 */
typedef __uint64_t xfs_cap_value_t;

typedef struct xfs_cap_set {
	xfs_cap_value_t	cap_effective;	/* use in capability checks */
	xfs_cap_value_t	cap_permitted;	/* combined with file attrs */
	xfs_cap_value_t	cap_inheritable;/* pass through exec */
} xfs_cap_set_t;

/* On-disk XFS extended attribute names */
#define SGI_CAP_FILE	"SGI_CAP_FILE"
#define SGI_CAP_FILE_SIZE	(sizeof(SGI_CAP_FILE)-1)
#define SGI_CAP_LINUX	"SGI_CAP_LINUX"
#define SGI_CAP_LINUX_SIZE	(sizeof(SGI_CAP_LINUX)-1)

/*
 * For Linux, we take the bitfields directly from capability.h
 * and no longer attempt to keep this attribute ondisk compatible
 * with IRIX.  Since this attribute is only set on exectuables,
 * it just doesn't make much sense to try.  We do use a different
 * named attribute though, to avoid confusion.
 */

#ifdef __KERNEL__

#ifdef CONFIG_FS_POSIX_CAP

#include <linux/posix_cap_xattr.h>

struct vnode;

extern int xfs_cap_vhascap(struct vnode *);
extern int xfs_cap_vset(struct vnode *, void *, size_t);
extern int xfs_cap_vget(struct vnode *, void *, size_t);
extern int xfs_cap_vremove(struct vnode *vp);

#define _CAP_EXISTS		xfs_cap_vhascap

#else
#define xfs_cap_vset(v,p,sz)	(-EOPNOTSUPP)
#define xfs_cap_vget(v,p,sz)	(-EOPNOTSUPP)
#define xfs_cap_vremove(v)	(-EOPNOTSUPP)
#define _CAP_EXISTS		(NULL)
#endif

#endif	/* __KERNEL__ */

#endif  /* __XFS_CAP_H__ */
