/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
 * with IRIX.  Since this attribute is only set on executables,
 * it just doesn't make much sense to try.  We do use a different
 * named attribute though, to avoid confusion.
 */

#ifdef __KERNEL__

#ifdef CONFIG_FS_POSIX_CAP

#include <linux/posix_cap_xattr.h>

struct bhv_vnode;

extern int xfs_cap_vhascap(struct bhv_vnode *);
extern int xfs_cap_vset(struct bhv_vnode *, void *, size_t);
extern int xfs_cap_vget(struct bhv_vnode *, void *, size_t);
extern int xfs_cap_vremove(struct bhv_vnode *);

#define _CAP_EXISTS		xfs_cap_vhascap

#else
#define xfs_cap_vset(v,p,sz)	(-EOPNOTSUPP)
#define xfs_cap_vget(v,p,sz)	(-EOPNOTSUPP)
#define xfs_cap_vremove(v)	(-EOPNOTSUPP)
#define _CAP_EXISTS		(NULL)
#endif

#endif	/* __KERNEL__ */

#endif  /* __XFS_CAP_H__ */
