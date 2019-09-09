// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SUPER_H__
#define __XFS_SUPER_H__

#include <linux/exportfs.h>

#ifdef CONFIG_XFS_QUOTA
extern int xfs_qm_init(void);
extern void xfs_qm_exit(void);
#else
# define xfs_qm_init()	(0)
# define xfs_qm_exit()	do { } while (0)
#endif

#ifdef CONFIG_XFS_POSIX_ACL
# define XFS_ACL_STRING		"ACLs, "
# define set_posix_acl_flag(sb)	((sb)->s_flags |= SB_POSIXACL)
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

#ifdef CONFIG_XFS_ONLINE_SCRUB
# define XFS_SCRUB_STRING	"scrub, "
#else
# define XFS_SCRUB_STRING
#endif

#ifdef CONFIG_XFS_ONLINE_REPAIR
# define XFS_REPAIR_STRING	"repair, "
#else
# define XFS_REPAIR_STRING
#endif

#ifdef CONFIG_XFS_WARN
# define XFS_WARN_STRING	"verbose warnings, "
#else
# define XFS_WARN_STRING
#endif

#ifdef DEBUG
# define XFS_DBG_STRING		"debug"
#else
# define XFS_DBG_STRING		"no debug"
#endif

#define XFS_VERSION_STRING	"SGI XFS"
#define XFS_BUILD_OPTIONS	XFS_ACL_STRING \
				XFS_SECURITY_STRING \
				XFS_REALTIME_STRING \
				XFS_SCRUB_STRING \
				XFS_REPAIR_STRING \
				XFS_WARN_STRING \
				XFS_DBG_STRING /* DBG must be last */

struct xfs_inode;
struct xfs_mount;
struct xfs_buftarg;
struct block_device;

extern void xfs_quiesce_attr(struct xfs_mount *mp);
extern void xfs_flush_inodes(struct xfs_mount *mp);
extern void xfs_blkdev_issue_flush(struct xfs_buftarg *);
extern xfs_agnumber_t xfs_set_inode_alloc(struct xfs_mount *,
					   xfs_agnumber_t agcount);

extern const struct export_operations xfs_export_operations;
extern const struct xattr_handler *xfs_xattr_handlers[];
extern const struct quotactl_ops xfs_quotactl_operations;

extern void xfs_reinit_percpu_counters(struct xfs_mount *mp);

extern struct workqueue_struct *xfs_discard_wq;

#define XFS_M(sb)		((struct xfs_mount *)((sb)->s_fs_info))

#endif	/* __XFS_SUPER_H__ */
