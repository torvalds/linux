/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
