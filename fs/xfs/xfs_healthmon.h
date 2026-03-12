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

	/* preallocated event for unmount */
	struct xfs_healthmon_event	*unmount_event;

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
	XFS_HEALTHMON_UNMOUNT,	/* filesystem is unmounting */

	/* filesystem shutdown */
	XFS_HEALTHMON_SHUTDOWN,

	/* metadata health events */
	XFS_HEALTHMON_SICK,	/* runtime corruption observed */
	XFS_HEALTHMON_CORRUPT,	/* fsck reported corruption */
	XFS_HEALTHMON_HEALTHY,	/* fsck reported healthy structure */

	/* media errors */
	XFS_HEALTHMON_MEDIA_ERROR,

	/* file range events */
	XFS_HEALTHMON_BUFREAD,
	XFS_HEALTHMON_BUFWRITE,
	XFS_HEALTHMON_DIOREAD,
	XFS_HEALTHMON_DIOWRITE,
	XFS_HEALTHMON_DATALOST,
};

enum xfs_healthmon_domain {
	XFS_HEALTHMON_MOUNT,	/* affects the whole fs */

	/* metadata health events */
	XFS_HEALTHMON_FS,	/* main filesystem metadata */
	XFS_HEALTHMON_AG,	/* allocation group metadata */
	XFS_HEALTHMON_INODE,	/* inode metadata */
	XFS_HEALTHMON_RTGROUP,	/* realtime group metadata */

	/* media errors */
	XFS_HEALTHMON_DATADEV,
	XFS_HEALTHMON_RTDEV,
	XFS_HEALTHMON_LOGDEV,

	/* file range events */
	XFS_HEALTHMON_FILERANGE,
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
		/* fs/rt metadata */
		struct {
			/* XFS_SICK_* flags */
			unsigned int	fsmask;
		};
		/* ag/rtgroup metadata */
		struct {
			/* XFS_SICK_(AG|RG)* flags */
			unsigned int	grpmask;
			unsigned int	group;
		};
		/* inode metadata */
		struct {
			/* XFS_SICK_INO_* flags */
			unsigned int	imask;
			uint32_t	gen;
			xfs_ino_t	ino;
		};
		/* shutdown */
		struct {
			unsigned int	flags;
		};
		/* media errors */
		struct {
			xfs_daddr_t	daddr;
			uint64_t	bbcount;
		};
		/* file range events */
		struct {
			xfs_ino_t	fino;
			loff_t		fpos;
			uint64_t	flen;
			uint32_t	fgen;
			int		error;
		};
	};
};

void xfs_healthmon_report_fs(struct xfs_mount *mp,
		enum xfs_healthmon_type type, unsigned int old_mask,
		unsigned int new_mask);
void xfs_healthmon_report_group(struct xfs_group *xg,
		enum xfs_healthmon_type type, unsigned int old_mask,
		unsigned int new_mask);
void xfs_healthmon_report_inode(struct xfs_inode *ip,
		enum xfs_healthmon_type type, unsigned int old_mask,
		unsigned int new_mask);

void xfs_healthmon_report_shutdown(struct xfs_mount *mp, uint32_t flags);

void xfs_healthmon_report_media(struct xfs_mount *mp, enum xfs_device fdev,
		xfs_daddr_t daddr, uint64_t bbcount);

void xfs_healthmon_report_file_ioerror(struct xfs_inode *ip,
		const struct fserror_event *p);

long xfs_ioc_health_monitor(struct file *file,
		struct xfs_health_monitor __user *arg);

#endif /* __XFS_HEALTHMON_H__ */
