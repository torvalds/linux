// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/rculist.h>
#include <linux/wait.h>
#include <linux/memcontrol.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

#include <linux/atomic.h>

/*
 * Final freeing of a group
 */
static void fsnotify_final_destroy_group(struct fsnotify_group *group)
{
	if (group->ops->free_group_priv)
		group->ops->free_group_priv(group);

	mem_cgroup_put(group->memcg);
	mutex_destroy(&group->mark_mutex);

	kfree(group);
}

/*
 * Stop queueing new events for this group. Once this function returns
 * fsnotify_add_event() will not add any new events to the group's queue.
 */
void fsnotify_group_stop_queueing(struct fsnotify_group *group)
{
	spin_lock(&group->notification_lock);
	group->shutdown = true;
	spin_unlock(&group->notification_lock);
}

/*
 * Trying to get rid of a group. Remove all marks, flush all events and release
 * the group reference.
 * Note that another thread calling fsnotify_clear_marks_by_group() may still
 * hold a ref to the group.
 */
void fsnotify_destroy_group(struct fsnotify_group *group)
{
	/*
	 * Stop queueing new events. The code below is careful enough to not
	 * require this but fanotify needs to stop queuing events even before
	 * fsnotify_destroy_group() is called and this makes the other callers
	 * of fsnotify_destroy_group() to see the same behavior.
	 */
	fsnotify_group_stop_queueing(group);

	/* Clear all marks for this group and queue them for destruction */
	fsnotify_clear_marks_by_group(group, FSNOTIFY_OBJ_TYPE_ANY);

	/*
	 * Some marks can still be pinned when waiting for response from
	 * userspace. Wait for those now. fsnotify_prepare_user_wait() will
	 * not succeed now so this wait is race-free.
	 */
	wait_event(group->notification_waitq, !atomic_read(&group->user_waits));

	/*
	 * Wait until all marks get really destroyed. We could actually destroy
	 * them ourselves instead of waiting for worker to do it, however that
	 * would be racy as worker can already be processing some marks before
	 * we even entered fsnotify_destroy_group().
	 */
	fsnotify_wait_marks_destroyed();

	/*
	 * Since we have waited for fsnotify_mark_srcu in
	 * fsnotify_mark_destroy_list() there can be no outstanding event
	 * notification against this group. So clearing the notification queue
	 * of all events is reliable now.
	 */
	fsnotify_flush_notify(group);

	/*
	 * Destroy overflow event (we cannot use fsnotify_destroy_event() as
	 * that deliberately ignores overflow events.
	 */
	if (group->overflow_event)
		group->ops->free_event(group, group->overflow_event);

	fsnotify_put_group(group);
}

/*
 * Get reference to a group.
 */
void fsnotify_get_group(struct fsnotify_group *group)
{
	refcount_inc(&group->refcnt);
}

/*
 * Drop a reference to a group.  Free it if it's through.
 */
void fsnotify_put_group(struct fsnotify_group *group)
{
	if (refcount_dec_and_test(&group->refcnt))
		fsnotify_final_destroy_group(group);
}
EXPORT_SYMBOL_GPL(fsnotify_put_group);

static struct fsnotify_group *__fsnotify_alloc_group(
				const struct fsnotify_ops *ops,
				int flags, gfp_t gfp)
{
	static struct lock_class_key nofs_marks_lock;
	struct fsnotify_group *group;

	group = kzalloc(sizeof(struct fsnotify_group), gfp);
	if (!group)
		return ERR_PTR(-ENOMEM);

	/* set to 0 when there a no external references to this group */
	refcount_set(&group->refcnt, 1);
	atomic_set(&group->user_waits, 0);

	spin_lock_init(&group->notification_lock);
	INIT_LIST_HEAD(&group->notification_list);
	init_waitqueue_head(&group->notification_waitq);
	group->max_events = UINT_MAX;

	mutex_init(&group->mark_mutex);
	INIT_LIST_HEAD(&group->marks_list);

	group->ops = ops;
	group->flags = flags;
	/*
	 * For most backends, eviction of inode with a mark is not expected,
	 * because marks hold a refcount on the inode against eviction.
	 *
	 * Use a different lockdep class for groups that support evictable
	 * inode marks, because with evictable marks, mark_mutex is NOT
	 * fs-reclaim safe - the mutex is taken when evicting inodes.
	 */
	if (flags & FSNOTIFY_GROUP_NOFS)
		lockdep_set_class(&group->mark_mutex, &nofs_marks_lock);

	return group;
}

/*
 * Create a new fsnotify_group and hold a reference for the group returned.
 */
struct fsnotify_group *fsnotify_alloc_group(const struct fsnotify_ops *ops,
					    int flags)
{
	gfp_t gfp = (flags & FSNOTIFY_GROUP_USER) ? GFP_KERNEL_ACCOUNT :
						    GFP_KERNEL;

	return __fsnotify_alloc_group(ops, flags, gfp);
}
EXPORT_SYMBOL_GPL(fsnotify_alloc_group);

int fsnotify_fasync(int fd, struct file *file, int on)
{
	struct fsnotify_group *group = file->private_data;

	return fasync_helper(fd, file, on, &group->fsn_fa) >= 0 ? 0 : -EIO;
}
