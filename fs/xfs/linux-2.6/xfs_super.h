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
#ifndef __XFS_SUPER_H__
#define __XFS_SUPER_H__

#include <linux/exportfs.h>

#ifdef CONFIG_XFS_QUOTA
extern void xfs_qm_init(void);
extern void xfs_qm_exit(void);
# define vfs_initquota()	xfs_qm_init()
# define vfs_exitquota()	xfs_qm_exit()
#else
# define vfs_initquota()	do { } while (0)
# define vfs_exitquota()	do { } while (0)
#endif

#ifdef CONFIG_XFS_POSIX_ACL
# define XFS_ACL_STRING		"ACLs, "
# define set_posix_acl_flag(sb)	((sb)->s_flags |= MS_POSIXACL)
#else
# define XFS_ACL_STRING
# define set_posix_acl_flag(sb)	do { } while (0)
#endif

#define XFS_SECURITY_STRING	"security attributes, "

#ifdef CONFIG_XFS_RT
# define XFS_REALTIME_STRING	"realtime, "
#else
# define XFS_REALTIME_STRING
#endif

#if XFS_BIG_BLKNOS
# if XFS_BIG_INUMS
#  define XFS_BIGFS_STRING	"large block/inode numbers, "
# else
#  define XFS_BIGFS_STRING	"large block numbers, "
# endif
#else
# define XFS_BIGFS_STRING
#endif

#ifdef CONFIG_XFS_TRACE
# define XFS_TRACE_STRING	"tracing, "
#else
# define XFS_TRACE_STRING
#endif

#ifdef CONFIG_XFS_DMAPI
# define XFS_DMAPI_STRING	"dmapi support, "
#else
# define XFS_DMAPI_STRING
#endif

#ifdef DEBUG
# define XFS_DBG_STRING		"debug"
#else
# define XFS_DBG_STRING		"no debug"
#endif

#define XFS_BUILD_OPTIONS	XFS_ACL_STRING \
				XFS_SECURITY_STRING \
				XFS_REALTIME_STRING \
				XFS_BIGFS_STRING \
				XFS_TRACE_STRING \
				XFS_DMAPI_STRING \
				XFS_DBG_STRING /* DBG must be last */

struct xfs_inode;
struct xfs_mount;
struct xfs_buftarg;
struct block_device;

extern __uint64_t xfs_max_file_offset(unsigned int);

extern void xfs_blkdev_issue_flush(struct xfs_buftarg *);

extern const struct export_operations xfs_export_operations;
extern struct xattr_handler *xfs_xattr_handlers[];

#define XFS_M(sb)		((struct xfs_mount *)((sb)->s_fs_info))

#endif	/* __XFS_SUPER_H__ */
