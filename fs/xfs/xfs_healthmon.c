// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2024-2026 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs_platform.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trace.h"
#include "xfs_ag.h"
#include "xfs_btree.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_quota_defs.h"
#include "xfs_rtgroup.h"
#include "xfs_health.h"
#include "xfs_healthmon.h"
#include "xfs_fsops.h"
#include "xfs_notify_failure.h"
#include "xfs_file.h"
#include "xfs_ioctl.h"

#include <linux/anon_inodes.h>
#include <linux/eventpoll.h>
#include <linux/poll.h>
#include <linux/fserror.h>

/*
 * Live Health Monitoring
 * ======================
 *
 * Autonomous self-healing of XFS filesystems requires a means for the kernel
 * to send filesystem health events to a monitoring daemon in userspace.  To
 * accomplish this, we establish a thread_with_file kthread object to handle
 * translating internal events about filesystem health into a format that can
 * be parsed easily by userspace.  When those internal events occur, the core
 * filesystem code calls this health monitor to convey the events to userspace.
 * Userspace reads events from the file descriptor returned by the ioctl.
 *
 * The healthmon abstraction has a weak reference to the host filesystem mount
 * so that the queueing and processing of the events do not pin the mount and
 * cannot slow down the main filesystem.  The healthmon object can exist past
 * the end of the filesystem mount.
 */

/* sign of a detached health monitor */
#define DETACHED_MOUNT_COOKIE		((uintptr_t)0)

/* Constrain the number of event objects that can build up in memory. */
#define XFS_HEALTHMON_MAX_EVENTS	(SZ_32K / \
					 sizeof(struct xfs_healthmon_event))

/* Constrain the size of the output buffer for read_iter. */
#define XFS_HEALTHMON_MAX_OUTBUF	SZ_64K

/* spinlock for atomically updating xfs_mount <-> xfs_healthmon pointers */
static DEFINE_SPINLOCK(xfs_healthmon_lock);

/* Grab a reference to the healthmon object for a given mount, if any. */
static struct xfs_healthmon *
xfs_healthmon_get(
	struct xfs_mount		*mp)
{
	struct xfs_healthmon		*hm;

	rcu_read_lock();
	hm = mp->m_healthmon;
	if (hm && !refcount_inc_not_zero(&hm->ref))
		hm = NULL;
	rcu_read_unlock();

	return hm;
}

/*
 * Release the reference to a healthmon object.  If there are no more holders,
 * free the health monitor after an RCU grace period to eliminate possibility
 * of races with xfs_healthmon_get.
 */
static void
xfs_healthmon_put(
	struct xfs_healthmon		*hm)
{
	if (refcount_dec_and_test(&hm->ref)) {
		struct xfs_healthmon_event	*event;
		struct xfs_healthmon_event	*next = hm->first_event;

		while ((event = next) != NULL) {
			trace_xfs_healthmon_drop(hm, event);
			next = event->next;
			kfree(event);
		}

		kfree(hm->unmount_event);
		kfree(hm->buffer);
		mutex_destroy(&hm->lock);
		kfree_rcu_mightsleep(hm);
	}
}

/* Attach a health monitor to an xfs_mount.  Only one allowed at a time. */
STATIC int
xfs_healthmon_attach(
	struct xfs_mount	*mp,
	struct xfs_healthmon	*hm)
{
	spin_lock(&xfs_healthmon_lock);
	if (mp->m_healthmon != NULL) {
		spin_unlock(&xfs_healthmon_lock);
		return -EEXIST;
	}

	refcount_inc(&hm->ref);
	mp->m_healthmon = hm;
	hm->mount_cookie = (uintptr_t)mp->m_super;
	spin_unlock(&xfs_healthmon_lock);

	return 0;
}

/* Detach a xfs mount from a specific healthmon instance. */
STATIC void
xfs_healthmon_detach(
	struct xfs_healthmon	*hm)
{
	spin_lock(&xfs_healthmon_lock);
	if (hm->mount_cookie == DETACHED_MOUNT_COOKIE) {
		spin_unlock(&xfs_healthmon_lock);
		return;
	}

	XFS_M((struct super_block *)hm->mount_cookie)->m_healthmon = NULL;
	hm->mount_cookie = DETACHED_MOUNT_COOKIE;
	spin_unlock(&xfs_healthmon_lock);

	trace_xfs_healthmon_detach(hm);
	xfs_healthmon_put(hm);
}

static inline void xfs_healthmon_bump_events(struct xfs_healthmon *hm)
{
	hm->events++;
	hm->total_events++;
}

static inline void xfs_healthmon_bump_lost(struct xfs_healthmon *hm)
{
	hm->lost_prev_event++;
	hm->total_lost++;
}

/*
 * If possible, merge a new event into an existing event.  Returns whether or
 * not it merged anything.
 */
static bool
xfs_healthmon_merge_events(
	struct xfs_healthmon_event		*existing,
	const struct xfs_healthmon_event	*new)
{
	if (!existing)
		return false;

	/* type and domain must match to merge events */
	if (existing->type != new->type ||
	    existing->domain != new->domain)
		return false;

	switch (existing->type) {
	case XFS_HEALTHMON_RUNNING:
	case XFS_HEALTHMON_UNMOUNT:
		/* should only ever be one of these events anyway */
		return false;

	case XFS_HEALTHMON_LOST:
		existing->lostcount += new->lostcount;
		return true;

	case XFS_HEALTHMON_SICK:
	case XFS_HEALTHMON_CORRUPT:
	case XFS_HEALTHMON_HEALTHY:
		switch (existing->domain) {
		case XFS_HEALTHMON_FS:
			existing->fsmask |= new->fsmask;
			return true;
		case XFS_HEALTHMON_AG:
		case XFS_HEALTHMON_RTGROUP:
			if (existing->group == new->group){
				existing->grpmask |= new->grpmask;
				return true;
			}
			return false;
		case XFS_HEALTHMON_INODE:
			if (existing->ino == new->ino &&
			    existing->gen == new->gen) {
				existing->imask |= new->imask;
				return true;
			}
			return false;
		default:
			ASSERT(0);
			return false;
		}
		return false;

	case XFS_HEALTHMON_SHUTDOWN:
		/* yes, we can race to shutdown */
		existing->flags |= new->flags;
		return true;

	case XFS_HEALTHMON_MEDIA_ERROR:
		/* physically adjacent errors can merge */
		if (existing->daddr + existing->bbcount == new->daddr) {
			existing->bbcount += new->bbcount;
			return true;
		}
		if (new->daddr + new->bbcount == existing->daddr) {
			existing->daddr = new->daddr;
			existing->bbcount += new->bbcount;
			return true;
		}
		return false;

	case XFS_HEALTHMON_BUFREAD:
	case XFS_HEALTHMON_BUFWRITE:
	case XFS_HEALTHMON_DIOREAD:
	case XFS_HEALTHMON_DIOWRITE:
	case XFS_HEALTHMON_DATALOST:
		/* logically adjacent file ranges can merge */
		if (existing->fino != new->fino || existing->fgen != new->fgen)
			return false;

		if (existing->fpos + existing->flen == new->fpos) {
			existing->flen += new->flen;
			return true;
		}

		if (new->fpos + new->flen == existing->fpos) {
			existing->fpos = new->fpos;
			existing->flen += new->flen;
			return true;
		}
		return false;
	}

	return false;
}

/* Insert an event onto the start of the queue. */
static inline void
__xfs_healthmon_insert(
	struct xfs_healthmon		*hm,
	struct xfs_healthmon_event	*event)
{
	struct timespec64		now;

	ktime_get_coarse_real_ts64(&now);
	event->time_ns = (now.tv_sec * NSEC_PER_SEC) + now.tv_nsec;

	event->next = hm->first_event;
	if (!hm->first_event)
		hm->first_event = event;
	if (!hm->last_event)
		hm->last_event = event;
	xfs_healthmon_bump_events(hm);
	wake_up(&hm->wait);

	trace_xfs_healthmon_insert(hm, event);
}

/* Push an event onto the end of the queue. */
static inline void
__xfs_healthmon_push(
	struct xfs_healthmon		*hm,
	struct xfs_healthmon_event	*event)
{
	struct timespec64		now;

	ktime_get_coarse_real_ts64(&now);
	event->time_ns = (now.tv_sec * NSEC_PER_SEC) + now.tv_nsec;

	if (!hm->first_event)
		hm->first_event = event;
	if (hm->last_event)
		hm->last_event->next = event;
	hm->last_event = event;
	event->next = NULL;
	xfs_healthmon_bump_events(hm);
	wake_up(&hm->wait);

	trace_xfs_healthmon_push(hm, event);
}

/* Deal with any previously lost events */
static int
xfs_healthmon_clear_lost_prev(
	struct xfs_healthmon		*hm)
{
	struct xfs_healthmon_event	lost_event = {
		.type			= XFS_HEALTHMON_LOST,
		.domain			= XFS_HEALTHMON_MOUNT,
		.lostcount		= hm->lost_prev_event,
	};
	struct xfs_healthmon_event	*event = NULL;

	if (xfs_healthmon_merge_events(hm->last_event, &lost_event)) {
		trace_xfs_healthmon_merge(hm, hm->last_event);
		wake_up(&hm->wait);
		goto cleared;
	}

	if (hm->events < XFS_HEALTHMON_MAX_EVENTS)
		event = kmemdup(&lost_event, sizeof(struct xfs_healthmon_event),
				GFP_NOFS);
	if (!event)
		return -ENOMEM;

	__xfs_healthmon_push(hm, event);
cleared:
	hm->lost_prev_event = 0;
	return 0;
}

/*
 * Push an event onto the end of the list after dealing with lost events and
 * possibly full queues.
 */
STATIC int
xfs_healthmon_push(
	struct xfs_healthmon			*hm,
	const struct xfs_healthmon_event	*template)
{
	struct xfs_healthmon_event		*event = NULL;
	int					error = 0;

	/*
	 * Locklessly check if the health monitor has already detached from the
	 * mount.  If so, ignore the event.  If we race with deactivation,
	 * we'll queue the event but never send it.
	 */
	if (hm->mount_cookie == DETACHED_MOUNT_COOKIE)
		return -ESHUTDOWN;

	mutex_lock(&hm->lock);

	/* Report previously lost events before we do anything else */
	if (hm->lost_prev_event) {
		error = xfs_healthmon_clear_lost_prev(hm);
		if (error)
			goto out_unlock;
	}

	/* Try to merge with the newest event */
	if (xfs_healthmon_merge_events(hm->last_event, template)) {
		trace_xfs_healthmon_merge(hm, hm->last_event);
		wake_up(&hm->wait);
		goto out_unlock;
	}

	/* Only create a heap event object if we're not already at capacity. */
	if (hm->events < XFS_HEALTHMON_MAX_EVENTS)
		event = kmemdup(template, sizeof(struct xfs_healthmon_event),
				GFP_NOFS);
	if (!event) {
		/* No memory means we lose the event */
		trace_xfs_healthmon_lost_event(hm);
		xfs_healthmon_bump_lost(hm);
		error = -ENOMEM;
		goto out_unlock;
	}

	__xfs_healthmon_push(hm, event);

out_unlock:
	mutex_unlock(&hm->lock);
	return error;
}

/*
 * Report that the filesystem is being unmounted, then detach the xfs mount
 * from this healthmon instance.
 */
void
xfs_healthmon_unmount(
	struct xfs_mount		*mp)
{
	struct xfs_healthmon		*hm = xfs_healthmon_get(mp);

	if (!hm)
		return;

	trace_xfs_healthmon_report_unmount(hm);

	/*
	 * Insert the unmount notification at the start of the event queue so
	 * that userspace knows the filesystem went away as soon as possible.
	 * There's nothing actionable for userspace after an unmount.  Once
	 * we've inserted the unmount event, hm no longer owns that event.
	 */
	__xfs_healthmon_insert(hm, hm->unmount_event);
	hm->unmount_event = NULL;

	xfs_healthmon_detach(hm);
	xfs_healthmon_put(hm);
}

/* Compute the reporting mask for non-unmount metadata health events. */
static inline unsigned int
metadata_event_mask(
	struct xfs_healthmon		*hm,
	enum xfs_healthmon_type		type,
	unsigned int			old_mask,
	unsigned int			new_mask)
{
	/* If we want all events, return all events. */
	if (hm->verbose)
		return new_mask;

	switch (type) {
	case XFS_HEALTHMON_SICK:
		/* Always report runtime corruptions */
		return new_mask;
	case XFS_HEALTHMON_CORRUPT:
		/* Only report new fsck errors */
		return new_mask & ~old_mask;
	case XFS_HEALTHMON_HEALTHY:
		/* Only report healthy metadata that got fixed */
		return new_mask & old_mask;
	default:
		ASSERT(0);
		break;
	}

	return 0;
}

/* Report XFS_FS_SICK_* events to healthmon */
void
xfs_healthmon_report_fs(
	struct xfs_mount		*mp,
	enum xfs_healthmon_type		type,
	unsigned int			old_mask,
	unsigned int			new_mask)
{
	struct xfs_healthmon_event	event = {
		.type			= type,
		.domain			= XFS_HEALTHMON_FS,
	};
	struct xfs_healthmon		*hm = xfs_healthmon_get(mp);

	if (!hm)
		return;

	event.fsmask = metadata_event_mask(hm, type, old_mask, new_mask) &
			~XFS_SICK_FS_SECONDARY;
	trace_xfs_healthmon_report_fs(hm, old_mask, new_mask, &event);

	if (event.fsmask)
		xfs_healthmon_push(hm, &event);

	xfs_healthmon_put(hm);
}

/* Report XFS_SICK_(AG|RG)* flags to healthmon */
void
xfs_healthmon_report_group(
	struct xfs_group		*xg,
	enum xfs_healthmon_type		type,
	unsigned int			old_mask,
	unsigned int			new_mask)
{
	struct xfs_healthmon_event	event = {
		.type			= type,
		.group			= xg->xg_gno,
	};
	struct xfs_healthmon		*hm = xfs_healthmon_get(xg->xg_mount);

	if (!hm)
		return;

	switch (xg->xg_type) {
	case XG_TYPE_RTG:
		event.domain = XFS_HEALTHMON_RTGROUP;
		event.grpmask = metadata_event_mask(hm, type, old_mask,
						    new_mask) &
				~XFS_SICK_RG_SECONDARY;
		break;
	case XG_TYPE_AG:
		event.domain = XFS_HEALTHMON_AG;
		event.grpmask = metadata_event_mask(hm, type, old_mask,
						    new_mask) &
				~XFS_SICK_AG_SECONDARY;
		break;
	default:
		ASSERT(0);
		break;
	}

	trace_xfs_healthmon_report_group(hm, old_mask, new_mask, &event);

	if (event.grpmask)
		xfs_healthmon_push(hm, &event);

	xfs_healthmon_put(hm);
}

/* Report XFS_SICK_INO_* flags to healthmon */
void
xfs_healthmon_report_inode(
	struct xfs_inode		*ip,
	enum xfs_healthmon_type		type,
	unsigned int			old_mask,
	unsigned int			new_mask)
{
	struct xfs_healthmon_event	event = {
		.type			= type,
		.domain			= XFS_HEALTHMON_INODE,
		.ino			= ip->i_ino,
		.gen			= VFS_I(ip)->i_generation,
	};
	struct xfs_healthmon		*hm = xfs_healthmon_get(ip->i_mount);

	if (!hm)
		return;

	event.imask = metadata_event_mask(hm, type, old_mask, new_mask) &
			~XFS_SICK_INO_SECONDARY;
	trace_xfs_healthmon_report_inode(hm, old_mask, event.imask, &event);

	if (event.imask)
		xfs_healthmon_push(hm, &event);

	xfs_healthmon_put(hm);
}

/* Add a shutdown event to the reporting queue. */
void
xfs_healthmon_report_shutdown(
	struct xfs_mount		*mp,
	uint32_t			flags)
{
	struct xfs_healthmon_event	event = {
		.type			= XFS_HEALTHMON_SHUTDOWN,
		.domain			= XFS_HEALTHMON_MOUNT,
		.flags			= flags,
	};
	struct xfs_healthmon		*hm = xfs_healthmon_get(mp);

	if (!hm)
		return;

	trace_xfs_healthmon_report_shutdown(hm, flags);

	xfs_healthmon_push(hm, &event);
	xfs_healthmon_put(hm);
}

static inline enum xfs_healthmon_domain
media_error_domain(
	enum xfs_device			fdev)
{
	switch (fdev) {
	case XFS_DEV_DATA:
		return XFS_HEALTHMON_DATADEV;
	case XFS_DEV_LOG:
		return XFS_HEALTHMON_LOGDEV;
	case XFS_DEV_RT:
		return XFS_HEALTHMON_RTDEV;
	}

	ASSERT(0);
	return 0;
}

/* Add a media error event to the reporting queue. */
void
xfs_healthmon_report_media(
	struct xfs_mount		*mp,
	enum xfs_device			fdev,
	xfs_daddr_t			daddr,
	uint64_t			bbcount)
{
	struct xfs_healthmon_event	event = {
		.type			= XFS_HEALTHMON_MEDIA_ERROR,
		.domain			= media_error_domain(fdev),
		.daddr			= daddr,
		.bbcount		= bbcount,
	};
	struct xfs_healthmon		*hm = xfs_healthmon_get(mp);

	if (!hm)
		return;

	trace_xfs_healthmon_report_media(hm, fdev, &event);

	xfs_healthmon_push(hm, &event);
	xfs_healthmon_put(hm);
}

static inline enum xfs_healthmon_type file_ioerr_type(enum fserror_type action)
{
	switch (action) {
	case FSERR_BUFFERED_READ:
		return XFS_HEALTHMON_BUFREAD;
	case FSERR_BUFFERED_WRITE:
		return XFS_HEALTHMON_BUFWRITE;
	case FSERR_DIRECTIO_READ:
		return XFS_HEALTHMON_DIOREAD;
	case FSERR_DIRECTIO_WRITE:
		return XFS_HEALTHMON_DIOWRITE;
	case FSERR_DATA_LOST:
		return XFS_HEALTHMON_DATALOST;
	case FSERR_METADATA:
		/* filtered out by xfs_fs_report_error */
		break;
	}

	ASSERT(0);
	return -1;
}

/* Add a file io error event to the reporting queue. */
void
xfs_healthmon_report_file_ioerror(
	struct xfs_inode		*ip,
	const struct fserror_event	*p)
{
	struct xfs_healthmon_event	event = {
		.type			= file_ioerr_type(p->type),
		.domain			= XFS_HEALTHMON_FILERANGE,
		.fino			= ip->i_ino,
		.fgen			= VFS_I(ip)->i_generation,
		.fpos			= p->pos,
		.flen			= p->len,
		/* send positive error number to userspace */
		.error			= -p->error,
	};
	struct xfs_healthmon		*hm = xfs_healthmon_get(ip->i_mount);

	if (!hm)
		return;

	trace_xfs_healthmon_report_file_ioerror(hm, p);

	xfs_healthmon_push(hm, &event);
	xfs_healthmon_put(hm);
}

static inline void
xfs_healthmon_reset_outbuf(
	struct xfs_healthmon		*hm)
{
	hm->buftail = 0;
	hm->bufhead = 0;
}

struct flags_map {
	unsigned int		in_mask;
	unsigned int		out_mask;
};

static const struct flags_map shutdown_map[] = {
	{ SHUTDOWN_META_IO_ERROR,	XFS_HEALTH_SHUTDOWN_META_IO_ERROR },
	{ SHUTDOWN_LOG_IO_ERROR,	XFS_HEALTH_SHUTDOWN_LOG_IO_ERROR },
	{ SHUTDOWN_FORCE_UMOUNT,	XFS_HEALTH_SHUTDOWN_FORCE_UMOUNT },
	{ SHUTDOWN_CORRUPT_INCORE,	XFS_HEALTH_SHUTDOWN_CORRUPT_INCORE },
	{ SHUTDOWN_CORRUPT_ONDISK,	XFS_HEALTH_SHUTDOWN_CORRUPT_ONDISK },
	{ SHUTDOWN_DEVICE_REMOVED,	XFS_HEALTH_SHUTDOWN_DEVICE_REMOVED },
};

static inline unsigned int
__map_flags(
	const struct flags_map	*map,
	size_t			array_len,
	unsigned int		flags)
{
	const struct flags_map	*m;
	unsigned int		ret = 0;

	for (m = map; m < map + array_len; m++) {
		if (flags & m->in_mask)
			ret |= m->out_mask;
	}

	return ret;
}

#define map_flags(map, flags) __map_flags((map), ARRAY_SIZE(map), (flags))

static inline unsigned int shutdown_mask(unsigned int in)
{
	return map_flags(shutdown_map, in);
}

static const unsigned int domain_map[] = {
	[XFS_HEALTHMON_MOUNT]		= XFS_HEALTH_MONITOR_DOMAIN_MOUNT,
	[XFS_HEALTHMON_FS]		= XFS_HEALTH_MONITOR_DOMAIN_FS,
	[XFS_HEALTHMON_AG]		= XFS_HEALTH_MONITOR_DOMAIN_AG,
	[XFS_HEALTHMON_INODE]		= XFS_HEALTH_MONITOR_DOMAIN_INODE,
	[XFS_HEALTHMON_RTGROUP]		= XFS_HEALTH_MONITOR_DOMAIN_RTGROUP,
	[XFS_HEALTHMON_DATADEV]		= XFS_HEALTH_MONITOR_DOMAIN_DATADEV,
	[XFS_HEALTHMON_RTDEV]		= XFS_HEALTH_MONITOR_DOMAIN_RTDEV,
	[XFS_HEALTHMON_LOGDEV]		= XFS_HEALTH_MONITOR_DOMAIN_LOGDEV,
	[XFS_HEALTHMON_FILERANGE]	= XFS_HEALTH_MONITOR_DOMAIN_FILERANGE,
};

static const unsigned int type_map[] = {
	[XFS_HEALTHMON_RUNNING]		= XFS_HEALTH_MONITOR_TYPE_RUNNING,
	[XFS_HEALTHMON_LOST]		= XFS_HEALTH_MONITOR_TYPE_LOST,
	[XFS_HEALTHMON_SICK]		= XFS_HEALTH_MONITOR_TYPE_SICK,
	[XFS_HEALTHMON_CORRUPT]		= XFS_HEALTH_MONITOR_TYPE_CORRUPT,
	[XFS_HEALTHMON_HEALTHY]		= XFS_HEALTH_MONITOR_TYPE_HEALTHY,
	[XFS_HEALTHMON_UNMOUNT]		= XFS_HEALTH_MONITOR_TYPE_UNMOUNT,
	[XFS_HEALTHMON_SHUTDOWN]	= XFS_HEALTH_MONITOR_TYPE_SHUTDOWN,
	[XFS_HEALTHMON_MEDIA_ERROR]	= XFS_HEALTH_MONITOR_TYPE_MEDIA_ERROR,
	[XFS_HEALTHMON_BUFREAD]		= XFS_HEALTH_MONITOR_TYPE_BUFREAD,
	[XFS_HEALTHMON_BUFWRITE]	= XFS_HEALTH_MONITOR_TYPE_BUFWRITE,
	[XFS_HEALTHMON_DIOREAD]		= XFS_HEALTH_MONITOR_TYPE_DIOREAD,
	[XFS_HEALTHMON_DIOWRITE]	= XFS_HEALTH_MONITOR_TYPE_DIOWRITE,
	[XFS_HEALTHMON_DATALOST]	= XFS_HEALTH_MONITOR_TYPE_DATALOST,
};

/* Render event as a V0 structure */
STATIC int
xfs_healthmon_format_v0(
	struct xfs_healthmon		*hm,
	const struct xfs_healthmon_event *event)
{
	struct xfs_health_monitor_event	hme = {
		.time_ns		= event->time_ns,
	};

	trace_xfs_healthmon_format(hm, event);

	if (event->domain < 0 || event->domain >= ARRAY_SIZE(domain_map) ||
	    event->type < 0   || event->type >= ARRAY_SIZE(type_map))
		return -EFSCORRUPTED;

	hme.domain = domain_map[event->domain];
	hme.type = type_map[event->type];

	/* fill in the event-specific details */
	switch (event->domain) {
	case XFS_HEALTHMON_MOUNT:
		switch (event->type) {
		case XFS_HEALTHMON_LOST:
			hme.e.lost.count = event->lostcount;
			break;
		case XFS_HEALTHMON_SHUTDOWN:
			hme.e.shutdown.reasons = shutdown_mask(event->flags);
			break;
		default:
			break;
		}
		break;
	case XFS_HEALTHMON_FS:
		hme.e.fs.mask = xfs_healthmon_fs_mask(event->fsmask);
		break;
	case XFS_HEALTHMON_RTGROUP:
		hme.e.group.mask = xfs_healthmon_rtgroup_mask(event->grpmask);
		hme.e.group.gno = event->group;
		break;
	case XFS_HEALTHMON_AG:
		hme.e.group.mask = xfs_healthmon_perag_mask(event->grpmask);
		hme.e.group.gno = event->group;
		break;
	case XFS_HEALTHMON_INODE:
		hme.e.inode.mask = xfs_healthmon_inode_mask(event->imask);
		hme.e.inode.ino = event->ino;
		hme.e.inode.gen = event->gen;
		break;
	case XFS_HEALTHMON_DATADEV:
	case XFS_HEALTHMON_LOGDEV:
	case XFS_HEALTHMON_RTDEV:
		hme.e.media.daddr = event->daddr;
		hme.e.media.bbcount = event->bbcount;
		break;
	case XFS_HEALTHMON_FILERANGE:
		hme.e.filerange.ino = event->fino;
		hme.e.filerange.gen = event->fgen;
		hme.e.filerange.pos = event->fpos;
		hme.e.filerange.len = event->flen;
		hme.e.filerange.error = abs(event->error);
		break;
	default:
		break;
	}

	ASSERT(hm->bufhead + sizeof(hme) <= hm->bufsize);

	/* copy formatted object to the outbuf */
	if (hm->bufhead + sizeof(hme) <= hm->bufsize) {
		memcpy(hm->buffer + hm->bufhead, &hme, sizeof(hme));
		hm->bufhead += sizeof(hme);
	}

	return 0;
}

/* How many bytes are waiting in the outbuf to be copied? */
static inline size_t
xfs_healthmon_outbuf_bytes(
	struct xfs_healthmon	*hm)
{
	if (hm->bufhead > hm->buftail)
		return hm->bufhead - hm->buftail;
	return 0;
}

/*
 * Do we have something for userspace to read?  This can mean unmount events,
 * events pending in the queue, or pending bytes in the outbuf.
 */
static inline bool
xfs_healthmon_has_eventdata(
	struct xfs_healthmon	*hm)
{
	/*
	 * If the health monitor is already detached from the xfs_mount, we
	 * want reads to return 0 bytes even if there are no events, because
	 * userspace interprets that as EOF.  If we race with deactivation,
	 * read_iter will take the necessary locks to discover that there are
	 * no events to send.
	 */
	if (hm->mount_cookie == DETACHED_MOUNT_COOKIE)
		return true;

	/*
	 * Either there are events waiting to be formatted into the buffer, or
	 * there's unread bytes in the buffer.
	 */
	return hm->events > 0 || xfs_healthmon_outbuf_bytes(hm) > 0;
}

/* Try to copy the rest of the outbuf to the iov iter. */
STATIC ssize_t
xfs_healthmon_copybuf(
	struct xfs_healthmon	*hm,
	struct iov_iter		*to)
{
	size_t			to_copy;
	size_t			w = 0;

	trace_xfs_healthmon_copybuf(hm, to);

	to_copy = xfs_healthmon_outbuf_bytes(hm);
	if (to_copy) {
		w = copy_to_iter(hm->buffer + hm->buftail, to_copy, to);
		if (!w)
			return -EFAULT;

		hm->buftail += w;
	}

	/*
	 * Nothing left to copy?  Reset the output buffer cursors to the start
	 * since there's no live data in the buffer.
	 */
	if (xfs_healthmon_outbuf_bytes(hm) == 0)
		xfs_healthmon_reset_outbuf(hm);
	return w;
}

/*
 * Return a health monitoring event for formatting into the output buffer if
 * there's enough space in the outbuf and an event waiting for us.  Caller
 * must hold i_rwsem on the healthmon file.
 */
static inline struct xfs_healthmon_event *
xfs_healthmon_format_pop(
	struct xfs_healthmon	*hm)
{
	struct xfs_healthmon_event *event;

	if (hm->bufhead + sizeof(*event) > hm->bufsize)
		return NULL;

	mutex_lock(&hm->lock);
	event = hm->first_event;
	if (event) {
		if (hm->last_event == event)
			hm->last_event = NULL;
		hm->first_event = event->next;
		hm->events--;

		trace_xfs_healthmon_pop(hm, event);
	}
	mutex_unlock(&hm->lock);
	return event;
}

/* Allocate formatting buffer */
STATIC int
xfs_healthmon_alloc_outbuf(
	struct xfs_healthmon	*hm,
	size_t			user_bufsize)
{
	void			*outbuf;
	size_t			bufsize =
		min(XFS_HEALTHMON_MAX_OUTBUF, max(PAGE_SIZE, user_bufsize));

	outbuf = kzalloc(bufsize, GFP_KERNEL);
	if (!outbuf) {
		if (bufsize == PAGE_SIZE)
			return -ENOMEM;

		bufsize = PAGE_SIZE;
		outbuf = kzalloc(bufsize, GFP_KERNEL);
		if (!outbuf)
			return -ENOMEM;
	}

	hm->buffer = outbuf;
	hm->bufsize = bufsize;
	hm->bufhead = 0;
	hm->buftail = 0;

	return 0;
}

/*
 * Convey queued event data to userspace.  First copy any remaining bytes in
 * the outbuf, then format the oldest event into the outbuf and copy that too.
 */
STATIC ssize_t
xfs_healthmon_read_iter(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct file		*file = iocb->ki_filp;
	struct inode		*inode = file_inode(file);
	struct xfs_healthmon	*hm = file->private_data;
	struct xfs_healthmon_event *event;
	size_t			copied = 0;
	ssize_t			ret = 0;

	if (file->f_flags & O_NONBLOCK) {
		if (!xfs_healthmon_has_eventdata(hm) || !inode_trylock(inode))
			return -EAGAIN;
	} else {
		ret = wait_event_interruptible(hm->wait,
				xfs_healthmon_has_eventdata(hm));
		if (ret)
			return ret;

		inode_lock(inode);
	}

	if (hm->bufsize == 0) {
		ret = xfs_healthmon_alloc_outbuf(hm, iov_iter_count(to));
		if (ret)
			goto out_unlock;
	}

	trace_xfs_healthmon_read_start(hm);

	/*
	 * If there's anything left in the output buffer, copy that before
	 * formatting more events.
	 */
	ret = xfs_healthmon_copybuf(hm, to);
	if (ret < 0)
		goto out_unlock;
	copied += ret;

	while (iov_iter_count(to) > 0) {
		/* Format the next events into the outbuf until it's full. */
		while ((event = xfs_healthmon_format_pop(hm)) != NULL) {
			ret = xfs_healthmon_format_v0(hm, event);
			kfree(event);
			if (ret)
				goto out_unlock;
		}

		/* Copy anything formatted into outbuf to userspace */
		ret = xfs_healthmon_copybuf(hm, to);
		if (ret <= 0)
			break;

		copied += ret;
	}

out_unlock:
	trace_xfs_healthmon_read_finish(hm);
	inode_unlock(inode);
	return copied ?: ret;
}

/* Poll for available events. */
STATIC __poll_t
xfs_healthmon_poll(
	struct file			*file,
	struct poll_table_struct	*wait)
{
	struct xfs_healthmon		*hm = file->private_data;
	__poll_t			mask = 0;

	poll_wait(file, &hm->wait, wait);

	if (xfs_healthmon_has_eventdata(hm))
		mask |= EPOLLIN;
	return mask;
}

/* Free the health monitoring information. */
STATIC int
xfs_healthmon_release(
	struct inode		*inode,
	struct file		*file)
{
	struct xfs_healthmon	*hm = file->private_data;

	trace_xfs_healthmon_release(hm);

	/*
	 * We might be closing the healthmon file before the filesystem
	 * unmounts, because userspace processes can terminate at any time and
	 * for any reason.  Null out xfs_mount::m_healthmon so that another
	 * process can create another health monitor file.
	 */
	xfs_healthmon_detach(hm);

	/*
	 * Wake up any readers that might be left.  There shouldn't be any
	 * because the only users of the waiter are read and poll.
	 */
	wake_up_all(&hm->wait);

	xfs_healthmon_put(hm);
	return 0;
}

/* Validate ioctl parameters. */
static inline bool
xfs_healthmon_validate(
	const struct xfs_health_monitor	*hmo)
{
	if (hmo->flags & ~XFS_HEALTH_MONITOR_ALL)
		return false;
	if (hmo->format != XFS_HEALTH_MONITOR_FMT_V0)
		return false;
	if (memchr_inv(&hmo->pad, 0, sizeof(hmo->pad)))
		return false;
	return true;
}

/* Emit some data about the health monitoring fd. */
static void
xfs_healthmon_show_fdinfo(
	struct seq_file		*m,
	struct file		*file)
{
	struct xfs_healthmon	*hm = file->private_data;

	mutex_lock(&hm->lock);
	seq_printf(m, "state:\t%s\ndev:\t%d:%d\nformat:\tv0\nevents:\t%llu\nlost:\t%llu\n",
			hm->mount_cookie == DETACHED_MOUNT_COOKIE ?
				"dead" : "alive",
			MAJOR(hm->dev), MINOR(hm->dev),
			hm->total_events,
			hm->total_lost);
	mutex_unlock(&hm->lock);
}

/* Reconfigure the health monitor. */
STATIC long
xfs_healthmon_reconfigure(
	struct file			*file,
	unsigned int			cmd,
	void __user			*arg)
{
	struct xfs_health_monitor	hmo;
	struct xfs_healthmon		*hm = file->private_data;

	if (copy_from_user(&hmo, arg, sizeof(hmo)))
		return -EFAULT;

	if (!xfs_healthmon_validate(&hmo))
		return -EINVAL;

	mutex_lock(&hm->lock);
	hm->verbose = !!(hmo.flags & XFS_HEALTH_MONITOR_VERBOSE);
	mutex_unlock(&hm->lock);

	return 0;
}

/* Does the fd point to the same filesystem as the one we're monitoring? */
STATIC long
xfs_healthmon_file_on_monitored_fs(
	struct file			*file,
	unsigned int			cmd,
	void __user			*arg)
{
	struct xfs_health_file_on_monitored_fs hms;
	struct xfs_healthmon		*hm = file->private_data;
	struct inode			*hms_inode;

	if (copy_from_user(&hms, arg, sizeof(hms)))
		return -EFAULT;

	if (hms.flags)
		return -EINVAL;

	CLASS(fd, hms_fd)(hms.fd);
	if (fd_empty(hms_fd))
		return -EBADF;

	hms_inode = file_inode(fd_file(hms_fd));
	mutex_lock(&hm->lock);
	if (hm->mount_cookie != (uintptr_t)hms_inode->i_sb) {
		mutex_unlock(&hm->lock);
		return -ESTALE;
	}

	mutex_unlock(&hm->lock);
	return 0;
}

/* Handle ioctls for the health monitoring thread. */
STATIC long
xfs_healthmon_ioctl(
	struct file			*file,
	unsigned int			cmd,
	unsigned long			p)
{
	void __user			*arg = (void __user *)p;

	switch (cmd) {
	case XFS_IOC_HEALTH_MONITOR:
		return xfs_healthmon_reconfigure(file, cmd, arg);
	case XFS_IOC_HEALTH_FD_ON_MONITORED_FS:
		return xfs_healthmon_file_on_monitored_fs(file, cmd, arg);
	default:
		break;
	}

	return -ENOTTY;
}

static const struct file_operations xfs_healthmon_fops = {
	.owner		= THIS_MODULE,
	.show_fdinfo	= xfs_healthmon_show_fdinfo,
	.read_iter	= xfs_healthmon_read_iter,
	.poll		= xfs_healthmon_poll,
	.release	= xfs_healthmon_release,
	.unlocked_ioctl	= xfs_healthmon_ioctl,
};

/*
 * Create a health monitoring file.  Returns an index to the fd table or a
 * negative errno.
 */
long
xfs_ioc_health_monitor(
	struct file			*file,
	struct xfs_health_monitor __user *arg)
{
	struct xfs_health_monitor	hmo;
	struct xfs_healthmon_event	*running_event;
	struct xfs_healthmon		*hm;
	struct xfs_inode		*ip = XFS_I(file_inode(file));
	struct xfs_mount		*mp = ip->i_mount;
	int				ret;

	/*
	 * The only intended user of the health monitoring system should be the
	 * xfs_healer daemon running on behalf of the whole filesystem in the
	 * initial user namespace.  IOWs, we don't allow unprivileged userspace
	 * (they can use fsnotify) nor do we allow containers.
	 */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (ip->i_ino != mp->m_sb.sb_rootino)
		return -EPERM;
	if (current_user_ns() != &init_user_ns)
		return -EPERM;

	if (copy_from_user(&hmo, arg, sizeof(hmo)))
		return -EFAULT;

	if (!xfs_healthmon_validate(&hmo))
		return -EINVAL;

	hm = kzalloc(sizeof(*hm), GFP_KERNEL);
	if (!hm)
		return -ENOMEM;
	hm->dev = mp->m_super->s_dev;
	refcount_set(&hm->ref, 1);

	mutex_init(&hm->lock);
	init_waitqueue_head(&hm->wait);

	if (hmo.flags & XFS_HEALTH_MONITOR_VERBOSE)
		hm->verbose = true;

	/* Queue up the first event that lets the client know we're running. */
	running_event = kzalloc(sizeof(struct xfs_healthmon_event), GFP_NOFS);
	if (!running_event) {
		ret = -ENOMEM;
		goto out_hm;
	}
	running_event->type = XFS_HEALTHMON_RUNNING;
	running_event->domain = XFS_HEALTHMON_MOUNT;
	__xfs_healthmon_insert(hm, running_event);

	/*
	 * Preallocate the unmount event so that we can't fail to notify the
	 * filesystem later.  This is key for triggering fast exit of the
	 * xfs_healer daemon.
	 */
	hm->unmount_event = kzalloc(sizeof(struct xfs_healthmon_event),
			GFP_NOFS);
	if (!hm->unmount_event) {
		ret = -ENOMEM;
		goto out_hm;
	}
	hm->unmount_event->type = XFS_HEALTHMON_UNMOUNT;
	hm->unmount_event->domain = XFS_HEALTHMON_MOUNT;

	/*
	 * Try to attach this health monitor to the xfs_mount.  The monitor is
	 * considered live and will receive events if this succeeds.
	 */
	ret = xfs_healthmon_attach(mp, hm);
	if (ret)
		goto out_hm;

	/*
	 * Create the anonymous file and install a fd for it.  If it succeeds,
	 * the file owns hm and can go away at any time, so we must not access
	 * it again.  This must go last because we can't undo a fd table
	 * installation.
	 */
	ret = anon_inode_getfd("xfs_healthmon", &xfs_healthmon_fops, hm,
			O_CLOEXEC | O_RDONLY);
	if (ret < 0)
		goto out_mp;

	trace_xfs_healthmon_create(mp->m_super->s_dev, hmo.flags, hmo.format);

	return ret;

out_mp:
	xfs_healthmon_detach(hm);
out_hm:
	ASSERT(refcount_read(&hm->ref) == 1);
	xfs_healthmon_put(hm);
	return ret;
}
