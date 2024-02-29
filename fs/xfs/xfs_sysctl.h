// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2001-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SYSCTL_H__
#define __XFS_SYSCTL_H__

#include <linux/sysctl.h>

/*
 * Tunable xfs parameters
 */

typedef struct xfs_sysctl_val {
	int min;
	int val;
	int max;
} xfs_sysctl_val_t;

typedef struct xfs_param {
	xfs_sysctl_val_t sgid_inherit;	/* Inherit S_ISGID if process' GID is
					 * not a member of parent dir GID. */
	xfs_sysctl_val_t symlink_mode;	/* Link creat mode affected by umask */
	xfs_sysctl_val_t panic_mask;	/* bitmask to cause panic on errors. */
	xfs_sysctl_val_t error_level;	/* Degree of reporting for problems  */
	xfs_sysctl_val_t syncd_timer;	/* Interval between xfssyncd wakeups */
	xfs_sysctl_val_t stats_clear;	/* Reset all XFS statistics to zero. */
	xfs_sysctl_val_t inherit_sync;	/* Inherit the "sync" inode flag. */
	xfs_sysctl_val_t inherit_nodump;/* Inherit the "nodump" inode flag. */
	xfs_sysctl_val_t inherit_noatim;/* Inherit the "noatime" inode flag. */
	xfs_sysctl_val_t xfs_buf_timer;	/* Interval between xfsbufd wakeups. */
	xfs_sysctl_val_t xfs_buf_age;	/* Metadata buffer age before flush. */
	xfs_sysctl_val_t inherit_nosym;	/* Inherit the "nosymlinks" flag. */
	xfs_sysctl_val_t rotorstep;	/* inode32 AG rotoring control knob */
	xfs_sysctl_val_t inherit_nodfrg;/* Inherit the "nodefrag" inode flag. */
	xfs_sysctl_val_t fstrm_timer;	/* Filestream dir-AG assoc'n timeout. */
	xfs_sysctl_val_t blockgc_timer;	/* Interval between blockgc scans */
} xfs_param_t;

/*
 * xfs_error_level:
 *
 * How much error reporting will be done when internal problems are
 * encountered.  These problems normally return an EFSCORRUPTED to their
 * caller, with no other information reported.
 *
 * 0	No error reports
 * 1	Report EFSCORRUPTED errors that will cause a filesystem shutdown
 * 5	Report all EFSCORRUPTED errors (all of the above errors, plus any
 *	additional errors that are known to not cause shutdowns)
 *
 * xfs_panic_mask bit 0x8 turns the error reports into panics
 */

enum {
	/* XFS_REFCACHE_SIZE = 1 */
	/* XFS_REFCACHE_PURGE = 2 */
	/* XFS_RESTRICT_CHOWN = 3 */
	XFS_SGID_INHERIT = 4,
	XFS_SYMLINK_MODE = 5,
	XFS_PANIC_MASK = 6,
	XFS_ERRLEVEL = 7,
	XFS_SYNCD_TIMER = 8,
	/* XFS_PROBE_DMAPI = 9 */
	/* XFS_PROBE_IOOPS = 10 */
	/* XFS_PROBE_QUOTA = 11 */
	XFS_STATS_CLEAR = 12,
	XFS_INHERIT_SYNC = 13,
	XFS_INHERIT_NODUMP = 14,
	XFS_INHERIT_NOATIME = 15,
	XFS_BUF_TIMER = 16,
	XFS_BUF_AGE = 17,
	/* XFS_IO_BYPASS = 18 */
	XFS_INHERIT_NOSYM = 19,
	XFS_ROTORSTEP = 20,
	XFS_INHERIT_NODFRG = 21,
	XFS_FILESTREAM_TIMER = 22,
};

extern xfs_param_t	xfs_params;

struct xfs_globals {
#ifdef DEBUG
	int	pwork_threads;		/* parallel workqueue threads */
	bool	larp;			/* log attribute replay */
#endif
	int	bload_leaf_slack;	/* btree bulk load leaf slack */
	int	bload_node_slack;	/* btree bulk load node slack */
	int	log_recovery_delay;	/* log recovery delay (secs) */
	int	mount_delay;		/* mount setup delay (secs) */
	bool	bug_on_assert;		/* BUG() the kernel on assert failure */
	bool	always_cow;		/* use COW fork for all overwrites */
};
extern struct xfs_globals	xfs_globals;

#ifdef CONFIG_SYSCTL
extern int xfs_sysctl_register(void);
extern void xfs_sysctl_unregister(void);
#else
# define xfs_sysctl_register()		(0)
# define xfs_sysctl_unregister()	do { } while (0)
#endif /* CONFIG_SYSCTL */

#endif /* __XFS_SYSCTL_H__ */
