/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2024-2026 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_HEALTHMON_H__
#define __XFS_HEALTHMON_H__

struct xfs_healthmon {
	/*
	 * Weak reference to the xfs filesystem that is being monitored.  It
	 * will be set to zero when the filesystem detaches from the monitor.
	 * Do not dereference this pointer.
	 */
	uintptr_t			mount_cookie;

	/*
	 * Device number of the filesystem being monitored.  This is for
	 * consistent tracing even after unmount.
	 */
	dev_t				dev;

	/*
	 * Reference count of this structure.  The open healthmon fd holds one
	 * ref, the xfs_mount holds another ref if it points to this object,
	 * and running event handlers hold their own refs.
	 */
	refcount_t			ref;
};

void xfs_healthmon_unmount(struct xfs_mount *mp);

long xfs_ioc_health_monitor(struct file *file,
		struct xfs_health_monitor __user *arg);

#endif /* __XFS_HEALTHMON_H__ */
