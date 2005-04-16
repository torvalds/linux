/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_REFCACHE_H__
#define __XFS_REFCACHE_H__

#ifdef HAVE_REFCACHE
/*
 * Maximum size (in inodes) for the NFS reference cache
 */
#define XFS_REFCACHE_SIZE_MAX	512

struct xfs_inode;
struct xfs_mount;

extern void xfs_refcache_insert(struct xfs_inode *);
extern void xfs_refcache_purge_ip(struct xfs_inode *);
extern void xfs_refcache_purge_mp(struct xfs_mount *);
extern void xfs_refcache_purge_some(struct xfs_mount *);
extern void xfs_refcache_resize(int);
extern void xfs_refcache_destroy(void);

extern void xfs_refcache_iunlock(struct xfs_inode *, uint);

#else

#define xfs_refcache_insert(ip)		do { } while (0)
#define xfs_refcache_purge_ip(ip)	do { } while (0)
#define xfs_refcache_purge_mp(mp)	do { } while (0)
#define xfs_refcache_purge_some(mp)	do { } while (0)
#define xfs_refcache_resize(size)	do { } while (0)
#define xfs_refcache_destroy()		do { } while (0)

#define xfs_refcache_iunlock(ip, flags)	xfs_iunlock(ip, flags)

#endif

#endif	/* __XFS_REFCACHE_H__ */
