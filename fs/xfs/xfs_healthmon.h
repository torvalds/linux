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

	/* lock for event list and event counters */
	struct mutex			lock;

	/* list of event objects */
	struct xfs_healthmon_event	*first_event;
	struct xfs_healthmon_event	*last_event;

	/* number of events in the list */
	unsigned int			events;

	/* do we want all events? */
	bool				verbose:1;

	/* waiter so read/poll can sleep until the arrival of events */
	struct wait_queue_head		wait;

	/*
	 * Buffer for formatting events for a read_iter call.  Events are
	 * formatted into the buffer at bufhead, and buftail determines where
	 * to start a copy_iter to get those events to userspace.  All buffer
	 * fields are protected by inode_lock.
	 */
	char				*buffer;
	size_t				bufsize;
	size_t				bufhead;
	size_t				buftail;

	/* did we lose previous events? */
	unsigned long long		lost_prev_event;

	/* total counts of events observed and lost events */
	unsigned long long		total_events;
	unsigned long long		total_lost;
};

void xfs_healthmon_unmount(struct xfs_mount *mp);

enum xfs_healthmon_type {
	XFS_HEALTHMON_RUNNING,	/* monitor running */
	XFS_HEALTHMON_LOST,	/* message lost */
};

enum xfs_healthmon_domain {
	XFS_HEALTHMON_MOUNT,	/* affects the whole fs */
};

struct xfs_healthmon_event {
	struct xfs_healthmon_event	*next;

	enum xfs_healthmon_type		type;
	enum xfs_healthmon_domain	domain;

	uint64_t			time_ns;

	union {
		/* lost events */
		struct {
			uint64_t	lostcount;
		};
	};
};

long xfs_ioc_health_monitor(struct file *file,
		struct xfs_health_monitor __user *arg);

#endif /* __XFS_HEALTHMON_H__ */
